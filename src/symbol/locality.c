#include "symbol/locality.h"

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset && left.entry_index == right.entry_index;
}


TbSymbolLocalityStatus_e tb_symbol_locality_init(TbSymbolLocality_t *locality, TbSymbolScopeView_t scope,
                                                 TbSymbolLocalitySlot_t *slots, size_t capacity)
{
    if (!locality || !scope.nodes || !scope.node_count || !scope.edges || !scope.edge_count || !slots ||
        scope.node_count > SIZE_MAX / sizeof(*scope.nodes) ||
        scope.edge_count > SIZE_MAX / sizeof(*scope.edges) || capacity > SIZE_MAX / sizeof(*slots)) {
        return TB_SYMBOL_LOCALITY_INVALID;
    }
    if (capacity < scope.node_count)
        return TB_SYMBOL_LOCALITY_RESOURCE;
    *locality = (TbSymbolLocality_t){
        .scope       = scope,
        .slots       = slots,
        .status      = TB_SYMBOL_LOCALITY_PENDING,
        .capacity    = capacity,
        .initialized = true
    };
    return locality->status;
}


static void fail(TbSymbolLocality_t *locality, TbSymbolLocalityStatus_e status,
                 TbSymbolLocalityFailure_e reason, size_t anchor, uint64_t tag)
{
    locality->status  = status;
    locality->failure = (TbSymbolLocalityFailure_t){
        .reason       = reason,
        .node_index   = locality->active,
        .anchor_index = anchor,
        .first_anchor = SIZE_MAX,
        .parent_tag   = tag
    };
    if (locality->active < locality->scope.node_count) {
        locality->failure.record = locality->scope.nodes[locality->active].record;
        if (locality->active < locality->initializing)
            locality->failure.first_anchor = locality->slots[locality->active].first_anchor;
    }
}


static bool valid_node(TbSymbolLocality_t *locality, size_t index)
{
    if (index >= locality->scope.node_count)
        return false;
    const TbSymbolScopeNode_t *node = &locality->scope.nodes[index];
    return node->complete && node->context_available && node->contexts.anchors && node->contexts.count &&
           node->relationships.nodes && node->relationships.count &&
           node->first_edge < locality->scope.edge_count &&
           node->contexts.count <= locality->scope.edge_count - node->first_edge &&
           same_record(node->record, node->contexts.selected) &&
           same_record(node->record, node->relationships.nodes[0].record) &&
           node->context.tag == node->relationships.nodes[0].context.tag;
}


static void accept(TbSymbolLocality_t *locality, TbSymbolScopeLocality_e value)
{
    TbSymbolLocalitySlot_t *slot = &locality->slots[locality->active];
    if (!slot->has_value) {
        slot->value        = value;
        slot->has_value    = true;
        slot->first_anchor = slot->next_anchor;
    } else if (slot->value != value) {
        fail(locality, TB_SYMBOL_LOCALITY_CONFLICT, TB_SYMBOL_LOCALITY_AGREEMENT,
             slot->next_anchor, 0);
        return;
    }
    ++slot->next_anchor;
}


static void edge(TbSymbolLocality_t *locality)
{
    TbSymbolLocalitySlot_t    *slot = &locality->slots[locality->active];
    const TbSymbolScopeNode_t *node = &locality->scope.nodes[locality->active];
    const TbSymbolScopeEdge_t *parent = &locality->scope.edges[node->first_edge + slot->next_anchor];
    const TbSymbolContextAnchor_t *anchor = &node->contexts.anchors[slot->next_anchor];
    if (parent->anchor_index != slot->next_anchor) {
        fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_SCOPE, slot->next_anchor, 0);
        return;
    }
    if (anchor->kind == TB_SYMBOL_CONTEXT_UNIT_PARENT || anchor->kind == TB_SYMBOL_CONTEXT_UNIT_ROOT) {
        if (parent->target_node != SIZE_MAX) {
            fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_SCOPE, slot->next_anchor, 0);
        } else {
            accept(locality, TB_SYMBOL_SCOPE_NONLOCAL);
        }
        return;
    }
    if (anchor->kind != TB_SYMBOL_CONTEXT_LEXICAL_PARENT || !valid_node(locality, parent->target_node) ||
        !same_record(anchor->parent, locality->scope.nodes[parent->target_node].record)) {
        fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_SCOPE, slot->next_anchor, 0);
        return;
    }
    TbSymbolLocalitySlot_t *target = &locality->slots[parent->target_node];
    uint64_t tag = locality->scope.nodes[parent->target_node].context.tag;
    if (tag != anchor->parent_context.tag) {
        fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_SCOPE, slot->next_anchor, tag);
        return;
    }
    if (target->visit == TB_SYMBOL_LOCALITY_ACTIVE) {
        fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_CYCLE, slot->next_anchor, tag);
        return;
    }
    switch (tag) {
    case 0x2e:
    case 0x1d:
    case 0x0b:
    case 0x25:
    case 0x32:
        accept(locality, TB_SYMBOL_SCOPE_LOCAL);
        break;
    case 0x39:
    case 0x02:
    case 0x13:
    case 0x17:
        if (target->visit == TB_SYMBOL_LOCALITY_COMPLETE) {
            accept(locality, target->value);
        } else {
            target->parent = locality->active;
            target->visit  = TB_SYMBOL_LOCALITY_ACTIVE;
            locality->active = parent->target_node;
        }
        break;
    default:
        fail(locality, TB_SYMBOL_LOCALITY_UNSUPPORTED, TB_SYMBOL_LOCALITY_PARENT_TAG,
             slot->next_anchor, tag);
        break;
    }
}


static void advance(TbSymbolLocality_t *locality)
{
    if (!valid_node(locality, locality->active)) {
        fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_SCOPE, SIZE_MAX, 0);
        return;
    }
    TbSymbolLocalitySlot_t    *slot = &locality->slots[locality->active];
    const TbSymbolScopeNode_t *node = &locality->scope.nodes[locality->active];
    if (slot->next_anchor < node->contexts.count) {
        edge(locality);
    } else if (slot->next_anchor != node->contexts.count || !slot->has_value) {
        fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_SCOPE, slot->next_anchor, 0);
    } else {
        slot->visit = TB_SYMBOL_LOCALITY_COMPLETE;
        if (slot->parent == SIZE_MAX) {
            if (locality->active != 0)
                fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_SCOPE, SIZE_MAX, 0);
            else
                locality->status = TB_SYMBOL_LOCALITY_READY;
        } else {
            locality->active = slot->parent;
        }
    }
}


TbSymbolLocalityStatus_e tb_symbol_locality_step(TbSymbolLocality_t *locality, size_t max_steps)
{
    if (!locality || !locality->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_LOCALITY_INVALID;
    if (locality->status != TB_SYMBOL_LOCALITY_PENDING)
        return locality->status;
    for (size_t step = 0; step < max_steps && locality->status == TB_SYMBOL_LOCALITY_PENDING; ++step) {
        if (locality->progress == UINT64_MAX) {
            fail(locality, TB_SYMBOL_LOCALITY_INVALID, TB_SYMBOL_LOCALITY_PROGRESS, SIZE_MAX, 0);
            break;
        }
        if (locality->initializing < locality->scope.node_count) {
            locality->slots[locality->initializing++] = (TbSymbolLocalitySlot_t){
                .parent       = SIZE_MAX,
                .first_anchor = SIZE_MAX
            };
            if (locality->initializing == locality->scope.node_count)
                locality->slots[0].visit = TB_SYMBOL_LOCALITY_ACTIVE;
        } else {
            advance(locality);
        }
        ++locality->progress;
    }
    return locality->status;
}


bool tb_symbol_locality_view(const TbSymbolLocality_t *locality, TbSymbolLocalityView_t *view)
{
    if (!locality || !locality->initialized || !view || locality->status != TB_SYMBOL_LOCALITY_READY)
        return false;
    *view = (TbSymbolLocalityView_t){
        .selected = locality->scope.nodes[0].record,
        .value    = locality->slots[0].value,
        .scope    = locality->scope,
        .slots    = locality->slots,
        .count    = locality->scope.node_count
    };
    return true;
}


bool tb_symbol_locality_failure(const TbSymbolLocality_t *locality, TbSymbolLocalityFailure_t *failure)
{
    if (!locality || !locality->initialized || !failure || locality->status == TB_SYMBOL_LOCALITY_PENDING ||
        locality->status == TB_SYMBOL_LOCALITY_READY) {
        return false;
    }
    *failure = locality->failure;
    return true;
}


uint64_t tb_symbol_locality_progress(const TbSymbolLocality_t *locality)
{
    return locality && locality->initialized ? locality->progress : 0;
}
