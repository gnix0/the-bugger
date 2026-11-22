#include "symbol/scope_graph.h"

typedef enum {
    SCOPE_VALIDATE,
    SCOPE_RESERVE,
    SCOPE_FILL,
    SCOPE_TRAVERSE,
    SCOPE_LOOKUP
} ScopePhase_e;

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset;
}


static bool same_context(TbSymbolGraphContext_t left, TbSymbolGraphContext_t right)
{
    return left.tag == right.tag && left.version == right.version && left.unit_kind == right.unit_kind &&
           left.abbrev_section == right.abbrev_section && left.abbrev_offset == right.abbrev_offset &&
           left.parent_record == right.parent_record && left.parent_tag == right.parent_tag;
}


TbSymbolScopeStatus_e tb_symbol_scope_init(TbSymbolScope_t *scope, TbSymbolReferenceTarget_t root,
                                          TbSymbolScopeNode_t *nodes, size_t node_capacity,
                                          TbSymbolScopeEdge_t *edges, size_t edge_capacity)
{
    if (!scope || !nodes || !edges || node_capacity > SIZE_MAX / sizeof(*nodes) ||
        edge_capacity > SIZE_MAX / sizeof(*edges) || root.record_offset >= root.backing.bytes) {
        return TB_SYMBOL_SCOPE_INVALID;
    }

    if (!node_capacity || !edge_capacity)
        return TB_SYMBOL_SCOPE_RESOURCE;

    nodes[0] = (TbSymbolScopeNode_t) {
        .record           = root,
        .traversal_parent = SIZE_MAX
    };

    *scope = (TbSymbolScope_t) {
        .nodes         = nodes,
        .edges         = edges,
        .node_capacity = node_capacity,
        .edge_capacity = edge_capacity,
        .node_count    = 1,
        .status        = TB_SYMBOL_SCOPE_SOURCE,
        .initialized   = true
    };

    return scope->status;
}


static void fail(TbSymbolScope_t *scope, TbSymbolScopeStatus_e status)
{
    scope->status = status;

    const TbSymbolScopeNode_t *node = &scope->nodes[scope->active];

    scope->failure = (TbSymbolScopeFailure_t) {
        .source_node   = scope->active,
        .anchor_index  = scope->phase == SCOPE_LOOKUP && node->next_edge < node->contexts.count
                             ? node->next_edge : SIZE_MAX,
        .previous_node = SIZE_MAX,
        .record        = node->record
    };

    if (scope->phase == SCOPE_LOOKUP && node->next_edge < node->contexts.count)
        scope->failure.record = node->contexts.anchors[node->next_edge].parent;
}


bool tb_symbol_scope_need(const TbSymbolScope_t *scope, TbSymbolScopeNeed_t *need)
{
    if (!scope || !scope->initialized || !need || scope->status != TB_SYMBOL_SCOPE_SOURCE ||
        scope->active >= scope->node_count) {
        return false;
    }

    const TbSymbolScopeNode_t *node = &scope->nodes[scope->active];

    *need = (TbSymbolScopeNeed_t) {
        .record            = node->record,
        .context           = node->context,
        .context_available = node->context_available
    };

    return true;
}


TbSymbolScopeStatus_e tb_symbol_scope_source(TbSymbolScope_t *scope,
                                            const TbSymbolGraphView_t *relationships,
                                            const TbSymbolContextView_t *contexts)
{
    if (!scope || !scope->initialized || scope->status != TB_SYMBOL_SCOPE_SOURCE ||
        scope->active >= scope->node_count) {
        return TB_SYMBOL_SCOPE_INVALID;
    }

    if (!relationships || !contexts) {
        fail(scope, TB_SYMBOL_SCOPE_DEPENDENCY);
        return scope->status;
    }

    TbSymbolScopeNode_t *node = &scope->nodes[scope->active];
    if (!relationships->nodes || !relationships->count || !contexts->anchors || !contexts->count ||
        contexts->count > relationships->count ||
        !same_record(node->record, relationships->nodes[0].record) ||
        relationships->nodes[0].record.entry_index != node->record.entry_index ||
        !same_record(node->record, contexts->selected) ||
        contexts->selected.entry_index != node->record.entry_index ||
        (node->context_available && !same_context(node->context, relationships->nodes[0].context))) {
        return TB_SYMBOL_SCOPE_INVALID;
    }

    node->relationships     = *relationships;
    node->contexts          = *contexts;
    node->context           = relationships->nodes[0].context;
    node->context_available = true;
    scope->scan             = 0;
    scope->anchor_scan      = 0;
    scope->phase            = SCOPE_VALIDATE;
    scope->status           = TB_SYMBOL_SCOPE_PENDING;

    return scope->status;
}


static uint64_t unit_tag(TbSymbolDwarfUnitKind_e kind)
{
    switch (kind) {
    case TB_SYMBOL_DWARF_COMPILE:
        return 0x11;
    case TB_SYMBOL_DWARF_PARTIAL:
        return 0x3c;
    case TB_SYMBOL_DWARF_TYPE:
        return 0x41;
    default:
        return 0;
    }
}


static bool anchor_matches(const TbSymbolGraphNode_t *record, const TbSymbolContextAnchor_t *anchor)
{
    if (!same_record(anchor->record, record->record) ||
        anchor->record.entry_index != record->record.entry_index ||
        !same_context(anchor->context, record->context) || !anchor->unit.header_available ||
        anchor->unit.offset != record->record.unit_offset ||
        anchor->unit.header.version != record->context.version ||
        anchor->unit.header.abbrev_offset != record->context.abbrev_offset) {
        return false;
    }

    TbSymbolDwarfUnitKind_e kind = anchor->unit.header.kind;
    if (kind != record->context.unit_kind &&
        !(anchor->unit.header.version == 4 && kind == TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL &&
          (record->context.unit_kind == TB_SYMBOL_DWARF_COMPILE ||
           record->context.unit_kind == TB_SYMBOL_DWARF_PARTIAL))) {
        return false;
    }

    uint64_t tag = unit_tag(record->context.unit_kind);
    uint64_t root;
    uint64_t end;
    if (!tag || !tb_symbol_add(anchor->unit.offset, anchor->unit.header.header_bytes, &root) ||
        !tb_symbol_add(anchor->unit.offset, anchor->unit.extent.total_bytes, &end) ||
        end != anchor->unit.extent.end_offset || root > record->record.record_offset ||
        record->record.record_offset >= end) {
        return false;
    }

    TbSymbolReferenceTarget_t expected = record->record;

    expected.record_offset = root;

    if (!same_record(anchor->unit_root, expected) || anchor->unit_root.entry_index != 0)
        return false;

    if (anchor->kind == TB_SYMBOL_CONTEXT_UNIT_ROOT) {
        return record->record.entry_index == 0 && record->record.record_offset == root &&
               record->context.tag == tag && record->context.parent_record == UINT64_MAX &&
               record->context.parent_tag == 0;
    }

    expected.record_offset = record->context.parent_record;

    if (!same_record(anchor->parent, expected) ||
        anchor->parent.record_offset >= record->record.record_offset ||
        anchor->parent.entry_index >= record->record.entry_index ||
        anchor->parent_context.tag != record->context.parent_tag ||
        anchor->parent_context.version != record->context.version ||
        anchor->parent_context.unit_kind != record->context.unit_kind ||
        anchor->parent_context.abbrev_section != record->context.abbrev_section ||
        anchor->parent_context.abbrev_offset != record->context.abbrev_offset) {
        return false;
    }

    if (anchor->kind == TB_SYMBOL_CONTEXT_UNIT_PARENT) {
        return anchor->parent.entry_index == 0 && anchor->parent.record_offset == root &&
               anchor->parent_context.tag == tag && anchor->parent_context.parent_record == UINT64_MAX &&
               anchor->parent_context.parent_tag == 0;
    }

    return anchor->kind == TB_SYMBOL_CONTEXT_LEXICAL_PARENT && anchor->parent.entry_index != 0 &&
           anchor->parent.record_offset > root && anchor->parent_context.parent_record >= root &&
           anchor->parent_context.parent_record < anchor->parent.record_offset;
}


static void validate(TbSymbolScope_t *scope)
{
    TbSymbolScopeNode_t *node = &scope->nodes[scope->active];
    if (scope->scan >= node->relationships.count) {
        if (scope->scan != node->relationships.count || scope->anchor_scan != node->contexts.count)
            fail(scope, TB_SYMBOL_SCOPE_INVALID);
        else
            scope->phase = SCOPE_RESERVE;
        return;
    }

    const TbSymbolGraphNode_t *record = &node->relationships.nodes[scope->scan];
    unsigned mask = (1u << (unsigned)TB_SYMBOL_GRAPH_RELATIONS) - 1u;
    if (!record->complete || (record->present & ~mask)) {
        fail(scope, TB_SYMBOL_SCOPE_INVALID);
        return;
    }

    if (!record->present) {
        if (scope->anchor_scan >= node->contexts.count) {
            fail(scope, TB_SYMBOL_SCOPE_INVALID);
            return;
        }

        const TbSymbolContextAnchor_t *anchor = &node->contexts.anchors[scope->anchor_scan];
        if (anchor->node_index != scope->scan || !anchor_matches(record, anchor)) {
            fail(scope, TB_SYMBOL_SCOPE_INVALID);
            scope->failure.anchor_index = scope->anchor_scan;
            scope->failure.record = record->record;

            return;
        }

        ++scope->anchor_scan;
    }
    ++scope->scan;
}


static void reserve(TbSymbolScope_t *scope)
{
    TbSymbolScopeNode_t *node = &scope->nodes[scope->active];
    if (scope->edge_count > scope->edge_capacity ||
        node->contexts.count > scope->edge_capacity - scope->edge_count) {
        fail(scope, TB_SYMBOL_SCOPE_RESOURCE);
        return;
    }

    node->first_edge = scope->edge_count;
    scope->edge_count += node->contexts.count;
    scope->filling = 0;
    scope->phase   = SCOPE_FILL;
}


static void fill(TbSymbolScope_t *scope)
{
    const TbSymbolScopeNode_t *node = &scope->nodes[scope->active];
    if (scope->filling == node->contexts.count) {
        scope->phase = SCOPE_TRAVERSE;
        return;
    }

    if (scope->filling > node->contexts.count || node->first_edge >= scope->edge_count ||
        scope->filling >= scope->edge_count - node->first_edge) {
        fail(scope, TB_SYMBOL_SCOPE_INVALID);
        return;
    }

    scope->edges[node->first_edge + scope->filling] = (TbSymbolScopeEdge_t) {
        .anchor_index = scope->filling,
        .target_node  = SIZE_MAX
    };

    ++scope->filling;
}


static void traverse(TbSymbolScope_t *scope)
{
    TbSymbolScopeNode_t *node = &scope->nodes[scope->active];
    if (node->next_edge >= node->contexts.count) {
        if (node->next_edge != node->contexts.count) {
            fail(scope, TB_SYMBOL_SCOPE_INVALID);
            return;
        }

        node->complete = true;

        if (node->traversal_parent == SIZE_MAX) {
            scope->status = TB_SYMBOL_SCOPE_READY;
        } else if (node->traversal_parent < scope->active) {
            scope->active = node->traversal_parent;
        } else {
            fail(scope, TB_SYMBOL_SCOPE_INVALID);
        }

        return;
    }

    const TbSymbolContextAnchor_t *anchor = &node->contexts.anchors[node->next_edge];
    if (anchor->kind != TB_SYMBOL_CONTEXT_LEXICAL_PARENT) {
        ++node->next_edge;
        return;
    }

    scope->lookup = 0;
    scope->phase  = SCOPE_LOOKUP;
}


static void lookup(TbSymbolScope_t *scope)
{
    TbSymbolScopeNode_t *node = &scope->nodes[scope->active];
    if (node->next_edge >= node->contexts.count || node->first_edge >= scope->edge_count ||
        node->next_edge >= scope->edge_count - node->first_edge || scope->lookup > scope->node_count) {
        fail(scope, TB_SYMBOL_SCOPE_INVALID);
        return;
    }

    const TbSymbolContextAnchor_t *anchor = &node->contexts.anchors[node->next_edge];
    TbSymbolScopeEdge_t *edge = &scope->edges[node->first_edge + node->next_edge];

    if (scope->lookup == scope->node_count) {
        if (scope->node_count >= scope->node_capacity) {
            fail(scope, TB_SYMBOL_SCOPE_RESOURCE);
            return;
        }

        size_t index = scope->node_count++;

        scope->nodes[index] = (TbSymbolScopeNode_t) {
            .record            = anchor->parent,
            .context           = anchor->parent_context,
            .traversal_parent  = scope->active,
            .context_available = true
        };

        edge->target_node = index;
        ++node->next_edge;
        scope->active = index;
        scope->status = TB_SYMBOL_SCOPE_SOURCE;

        return;
    }

    size_t previous = scope->lookup++;
    const TbSymbolScopeNode_t *found = &scope->nodes[previous];
    if (!same_record(found->record, anchor->parent))
        return;

    if (found->record.entry_index != anchor->parent.entry_index ||
        !same_context(found->context, anchor->parent_context)) {
        fail(scope, TB_SYMBOL_SCOPE_CONFLICT);
        scope->failure.previous_node = previous;
    } else if (!found->complete) {
        fail(scope, TB_SYMBOL_SCOPE_CYCLE);
        scope->failure.previous_node = previous;
    } else {
        edge->target_node = previous;
        ++node->next_edge;
        scope->phase = SCOPE_TRAVERSE;
    }
}


TbSymbolScopeStatus_e tb_symbol_scope_step(TbSymbolScope_t *scope, size_t max_steps)
{
    if (!scope || !scope->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_SCOPE_INVALID;

    for (size_t step = 0; step < max_steps && scope->status == TB_SYMBOL_SCOPE_PENDING; ++step) {
        if (scope->active >= scope->node_count || scope->node_count > scope->node_capacity ||
            scope->progress == UINT64_MAX) {
            scope->status = TB_SYMBOL_SCOPE_INVALID;
            scope->failure = (TbSymbolScopeFailure_t) {
                .source_node   = scope->active < scope->node_count ? scope->active : SIZE_MAX,
                .anchor_index  = SIZE_MAX,
                .previous_node = SIZE_MAX,
                .record        = scope->nodes[0].record
            };

            break;
        }

        switch (scope->phase) {
        case SCOPE_VALIDATE:
            validate(scope);
            break;
        case SCOPE_RESERVE:
            reserve(scope);
            break;
        case SCOPE_FILL:
            fill(scope);
            break;
        case SCOPE_TRAVERSE:
            traverse(scope);
            break;
        case SCOPE_LOOKUP:
            lookup(scope);
            break;
        default:
            fail(scope, TB_SYMBOL_SCOPE_INVALID);
            break;
        }

        ++scope->progress;
    }

    return scope->status;
}


bool tb_symbol_scope_view(const TbSymbolScope_t *scope, TbSymbolScopeView_t *view)
{
    if (!scope || !scope->initialized || !view || scope->status != TB_SYMBOL_SCOPE_READY)
        return false;

    *view = (TbSymbolScopeView_t) {
        .nodes      = scope->nodes,
        .node_count = scope->node_count,
        .edges      = scope->edges,
        .edge_count = scope->edge_count
    };

    return true;
}


bool tb_symbol_scope_failure(const TbSymbolScope_t *scope, TbSymbolScopeFailure_t *failure)
{
    if (!scope || !scope->initialized || !failure ||
        (scope->status != TB_SYMBOL_SCOPE_CYCLE && scope->status != TB_SYMBOL_SCOPE_CONFLICT &&
         scope->status != TB_SYMBOL_SCOPE_DEPENDENCY && scope->status != TB_SYMBOL_SCOPE_RESOURCE &&
         scope->status != TB_SYMBOL_SCOPE_INVALID)) {
        return false;
    }

    *failure = scope->failure;

    return true;
}


uint64_t tb_symbol_scope_progress(const TbSymbolScope_t *scope)
{
    return scope && scope->initialized ? scope->progress : 0;
}
