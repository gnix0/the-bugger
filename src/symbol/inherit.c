#include "symbol/inherit.h"

typedef enum {
    INHERIT_INITIALIZE,
    INHERIT_FIND,
    INHERIT_FIELD,
    INHERIT_EDGES
} InheritPhase_e;

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset;
}


static bool supported_field(uint64_t name)
{
    return name == TB_SYMBOL_INHERIT_NAME || name == TB_SYMBOL_INHERIT_LINKAGE_NAME ||
           name == TB_SYMBOL_INHERIT_TYPE || name == TB_SYMBOL_INHERIT_DECL_FILE ||
           name == TB_SYMBOL_INHERIT_DECL_LINE || name == TB_SYMBOL_INHERIT_DECL_COLUMN;
}


TbSymbolInheritStatus_e tb_symbol_inherit_init(TbSymbolInherit_t *inherit, TbSymbolGraphView_t graph,
                                               uint64_t name, TbSymbolInheritSlot_t *slots,
                                               size_t capacity)
{
    if (!inherit || !graph.nodes || !graph.count || !slots || capacity > SIZE_MAX / sizeof(*slots))
        return TB_SYMBOL_INHERIT_INVALID;
    if (!supported_field(name))
        return TB_SYMBOL_INHERIT_UNSUPPORTED;
    if (capacity < graph.count)
        return TB_SYMBOL_INHERIT_RESOURCE;
    *inherit = (TbSymbolInherit_t){
        .graph       = graph,
        .slots       = slots,
        .status      = TB_SYMBOL_INHERIT_PENDING,
        .name        = name,
        .phase       = INHERIT_INITIALIZE,
        .initialized = true
    };
    return inherit->status;
}


static void initialize(TbSymbolInherit_t *inherit)
{
    size_t index = inherit->initializing;
    if (!inherit->graph.nodes[index].complete) {
        inherit->failure.record = inherit->graph.nodes[index].record;
        inherit->status         = TB_SYMBOL_INHERIT_INVALID;
        return;
    }
    inherit->slots[index] = (TbSymbolInheritSlot_t){0};
    ++inherit->initializing;
    if (inherit->initializing == inherit->graph.count) {
        inherit->slots[0].visit = TB_SYMBOL_INHERIT_QUEUED;
        inherit->phase         = INHERIT_FIND;
    }
}


static void find_source(TbSymbolInherit_t *inherit)
{
    if (inherit->finding == inherit->graph.count) {
        inherit->status = inherit->owners == 0 ? TB_SYMBOL_INHERIT_ABSENT
                              : inherit->owners == 1 ? TB_SYMBOL_INHERIT_UNIQUE : TB_SYMBOL_INHERIT_MULTIPLE;
        return;
    }
    size_t index = inherit->finding++;
    if (inherit->slots[index].visit == TB_SYMBOL_INHERIT_QUEUED) {
        inherit->active = index;
        inherit->status = TB_SYMBOL_INHERIT_SOURCE;
    }
}


bool tb_symbol_inherit_need(const TbSymbolInherit_t *inherit, TbSymbolInheritNeed_t *need)
{
    if (!inherit || !inherit->initialized || !need || inherit->status != TB_SYMBOL_INHERIT_SOURCE)
        return false;
    *need = (TbSymbolInheritNeed_t){
        .record     = inherit->graph.nodes[inherit->active].record,
        .context    = inherit->graph.nodes[inherit->active].context,
        .name       = inherit->name,
        .node_index = inherit->active
    };
    return true;
}


static bool source_context(const TbSymbolGraphNode_t *node, TbSymbolUnitsView_t units,
                           TbSymbolDiesView_t tree, uint64_t unit_index)
{
    if (!units.entries || unit_index >= units.count || !units.entries[unit_index].header_available ||
        units.entries[unit_index].offset != node->record.unit_offset ||
        units.entries[unit_index].header.version != node->context.version ||
        units.section_index != node->record.section_index ||
        units.backing.key.device != node->record.backing.key.device ||
        units.backing.key.inode != node->record.backing.key.inode ||
        units.backing.bytes != node->record.backing.bytes || !tree.entries ||
        node->record.entry_index >= tree.count || tree.kind != node->context.unit_kind ||
        tree.abbrev_section != node->context.abbrev_section ||
        tree.abbrev_offset != node->context.abbrev_offset) {
        return false;
    }
    /* DWARF 4 info headers defer compilation/partial distinction to the root DIE. */
    TbSymbolDwarfUnitKind_e kind = units.entries[unit_index].header.kind;
    if (kind != node->context.unit_kind &&
        !(units.entries[unit_index].header.version == 4 && kind == TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL &&
          (node->context.unit_kind == TB_SYMBOL_DWARF_COMPILE ||
           node->context.unit_kind == TB_SYMBOL_DWARF_PARTIAL))) {
        return false;
    }
    TbSymbolReferenceTarget_t target = {
        .backing       = tree.backing,
        .section_index = tree.section_index,
        .unit_offset   = tree.unit_offset,
        .record_offset = tree.entries[node->record.entry_index].offset,
        .entry_index   = node->record.entry_index
    };
    if (!same_record(node->record, target))
        return false;
    const TbSymbolDieEntry_t *entry = &tree.entries[node->record.entry_index];
    if (entry->tag != node->context.tag ||
        (entry->parent != UINT64_MAX && entry->parent >= target.entry_index)) {
        return false;
    }
    uint64_t parent = entry->parent == UINT64_MAX ? UINT64_MAX : tree.entries[entry->parent].offset;
    uint64_t tag    = entry->parent == UINT64_MAX ? 0 : tree.entries[entry->parent].tag;
    return parent == node->context.parent_record && tag == node->context.parent_tag;
}


TbSymbolInheritStatus_e tb_symbol_inherit_source(TbSymbolInherit_t *inherit,
                                                 const TbSymbolUnitsView_t *units,
                                                 const TbSymbolDiesView_t *tree, uint64_t unit_index)
{
    if (!inherit || !inherit->initialized || inherit->status != TB_SYMBOL_INHERIT_SOURCE)
        return TB_SYMBOL_INHERIT_INVALID;
    const TbSymbolGraphNode_t *node = &inherit->graph.nodes[inherit->active];
    if (!units || !tree) {
        inherit->failure.record = node->record;
        inherit->status         = TB_SYMBOL_INHERIT_DEPENDENCY;
        return inherit->status;
    }
    TbSymbolAttributes_t lookup;
    if (!source_context(node, *units, *tree, unit_index) ||
        tb_symbol_attributes_init(&lookup, *tree, (TbSymbolAttributeSelection_t){
            .die_index = node->record.entry_index,
            .name      = inherit->name
        }) != TB_SYMBOL_ATTRIBUTE_PENDING) {
        return TB_SYMBOL_INHERIT_INVALID;
    }
    inherit->lookup     = lookup;
    inherit->attributes = tree->attributes;
    inherit->phase      = INHERIT_FIELD;
    inherit->status     = TB_SYMBOL_INHERIT_PENDING;
    return inherit->status;
}


static void finish_node(TbSymbolInherit_t *inherit)
{
    inherit->finding = 0;
    inherit->phase   = INHERIT_FIND;
}


static void field(TbSymbolInherit_t *inherit)
{
    TbSymbolAttributeStatus_e status = tb_symbol_attributes_step(&inherit->lookup, 1);
    if (status == TB_SYMBOL_ATTRIBUTE_PENDING)
        return;
    TbSymbolInheritSlot_t *slot = &inherit->slots[inherit->active];
    if (status == TB_SYMBOL_ATTRIBUTE_UNIQUE) {
        uint64_t index;
        if (!tb_symbol_attributes_unique(&inherit->lookup, &index)) {
            inherit->status = TB_SYMBOL_INHERIT_INVALID;
            return;
        }
        slot->selection = inherit->lookup.result;
        slot->attribute = inherit->attributes[index];
        slot->visit     = TB_SYMBOL_INHERIT_OWNER;
        ++inherit->owners;
        finish_node(inherit);
    } else if (status == TB_SYMBOL_ATTRIBUTE_MISSING) {
        slot->visit    = TB_SYMBOL_INHERIT_EXAMINED;
        inherit->edge  = 0;
        inherit->scan  = 0;
        inherit->phase = INHERIT_EDGES;
    } else {
        inherit->failure.record = inherit->graph.nodes[inherit->active].record;
        TbSymbolAttributeStatus_e selected = tb_symbol_attributes_result(&inherit->lookup,
                                                                          &inherit->failure.selection);
        inherit->failure.selection_available = selected == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS;
        inherit->status = status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS ? TB_SYMBOL_INHERIT_CONFLICT
                                                                 : TB_SYMBOL_INHERIT_INVALID;
    }
}


static void edges(TbSymbolInherit_t *inherit)
{
    const TbSymbolGraphNode_t *node = &inherit->graph.nodes[inherit->active];
    if (inherit->edge >= TB_SYMBOL_GRAPH_RELATIONS) {
        if (inherit->edge == TB_SYMBOL_GRAPH_RELATIONS) {
            finish_node(inherit);
        } else {
            inherit->failure.record = node->record;
            inherit->status         = TB_SYMBOL_INHERIT_INVALID;
        }
        return;
    }
    if (!(node->present & (1u << inherit->edge))) {
        ++inherit->edge;
        return;
    }
    if (inherit->scan >= inherit->graph.count) {
        inherit->failure.record = node->record;
        inherit->status         = TB_SYMBOL_INHERIT_INVALID;
        return;
    }
    size_t index = inherit->scan++;
    if (same_record(inherit->graph.nodes[index].record, node->edges[inherit->edge].target)) {
        if (inherit->slots[index].visit == TB_SYMBOL_INHERIT_UNVISITED)
            inherit->slots[index].visit = TB_SYMBOL_INHERIT_QUEUED;
        inherit->scan = 0;
        ++inherit->edge;
    }
}


TbSymbolInheritStatus_e tb_symbol_inherit_step(TbSymbolInherit_t *inherit, size_t max_steps)
{
    if (!inherit || !inherit->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_INHERIT_INVALID;
    for (size_t i = 0; i < max_steps && inherit->status == TB_SYMBOL_INHERIT_PENDING; ++i) {
        if (inherit->progress != UINT64_MAX)
            ++inherit->progress;
        switch (inherit->phase) {
        case INHERIT_INITIALIZE:
            initialize(inherit);
            break;
        case INHERIT_FIND:
            find_source(inherit);
            break;
        case INHERIT_FIELD:
            field(inherit);
            break;
        case INHERIT_EDGES:
            edges(inherit);
            break;
        default:
            inherit->status = TB_SYMBOL_INHERIT_INVALID;
            break;
        }
    }
    return inherit->status;
}


bool tb_symbol_inherit_result(const TbSymbolInherit_t *inherit, TbSymbolInheritResult_t *result)
{
    if (!inherit || !inherit->initialized || !result ||
        (inherit->status != TB_SYMBOL_INHERIT_ABSENT && inherit->status != TB_SYMBOL_INHERIT_UNIQUE &&
         inherit->status != TB_SYMBOL_INHERIT_MULTIPLE)) {
        return false;
    }
    *result = (TbSymbolInheritResult_t){
        .name        = inherit->name,
        .owner_count = inherit->owners
    };
    return true;
}


bool tb_symbol_inherit_owner(const TbSymbolInherit_t *inherit, size_t node_index,
                              TbSymbolInheritOwner_t *owner)
{
    TbSymbolInheritResult_t result;
    if (!owner || !tb_symbol_inherit_result(inherit, &result) || node_index >= inherit->graph.count ||
        inherit->slots[node_index].visit != TB_SYMBOL_INHERIT_OWNER) {
        return false;
    }
    *owner = (TbSymbolInheritOwner_t){
        .node_index = node_index,
        .selection  = inherit->slots[node_index].selection,
        .attribute  = inherit->slots[node_index].attribute
    };
    return true;
}


bool tb_symbol_inherit_failure(const TbSymbolInherit_t *inherit, TbSymbolInheritFailure_t *failure)
{
    if (!inherit || !inherit->initialized || !failure ||
        (inherit->status != TB_SYMBOL_INHERIT_CONFLICT && inherit->status != TB_SYMBOL_INHERIT_DEPENDENCY &&
         inherit->status != TB_SYMBOL_INHERIT_INVALID)) {
        return false;
    }
    *failure = inherit->failure;
    return true;
}


uint64_t tb_symbol_inherit_progress(const TbSymbolInherit_t *inherit)
{
    return inherit && inherit->initialized ? inherit->progress : 0;
}
