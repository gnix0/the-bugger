#include "symbol/inherited_flag.h"

typedef enum {
    FLAG_INITIALIZE,
    FLAG_FIND,
    FLAG_FIELD,
    FLAG_EDGES
} FlagPhase_e;

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset;
}


static bool supported(uint64_t name)
{
    return name == TB_SYMBOL_FLAG_PROTOTYPED || name == TB_SYMBOL_FLAG_ARTIFICIAL ||
           name == TB_SYMBOL_FLAG_REFERENCE || name == TB_SYMBOL_FLAG_RVALUE_REFERENCE;
}


TbSymbolInheritedFlagStatus_e tb_symbol_inherited_flag_init(TbSymbolInheritedFlag_t *flag,
                                                            TbSymbolGraphView_t graph, uint64_t name,
                                                            TbSymbolInheritedFlagSlot_t *slots,
                                                            size_t capacity)
{
    if (!flag || !graph.nodes || !graph.count || !slots ||
        graph.count > SIZE_MAX / sizeof(*graph.nodes) || capacity > SIZE_MAX / sizeof(*slots)) {
        return TB_SYMBOL_FLAG_INVALID;
    }
    if (!supported(name))
        return TB_SYMBOL_FLAG_UNSUPPORTED;
    if (capacity < graph.count)
        return TB_SYMBOL_FLAG_RESOURCE;
    *flag = (TbSymbolInheritedFlag_t){
        .graph       = graph,
        .slots       = slots,
        .status      = TB_SYMBOL_FLAG_PENDING,
        .name        = name,
        .phase       = FLAG_INITIALIZE,
        .initialized = true
    };
    return flag->status;
}


static void fail(TbSymbolInheritedFlag_t *flag, TbSymbolInheritedFlagStatus_e status,
                 TbSymbolInheritedFlagFailureReason_e reason, size_t index)
{
    flag->status  = status;
    flag->failure = (TbSymbolInheritedFlagFailure_t){
        .reason     = reason,
        .node_index = index
    };
    if (index < flag->graph.count)
        flag->failure.record = flag->graph.nodes[index].record;
}


static void initialize(TbSymbolInheritedFlag_t *flag)
{
    size_t index = flag->initializing;
    if (!flag->graph.nodes[index].complete ||
        (flag->graph.nodes[index].present & ~((1U << (unsigned)TB_SYMBOL_GRAPH_RELATIONS) - 1U))) {
        fail(flag, TB_SYMBOL_FLAG_INVALID, TB_SYMBOL_FLAG_FAILURE_SCOPE, index);
        return;
    }
    flag->slots[index] = (TbSymbolInheritedFlagSlot_t){0};
    ++flag->initializing;
    if (flag->initializing == flag->graph.count) {
        flag->slots[0].visit = TB_SYMBOL_FLAG_QUEUED;
        flag->phase         = FLAG_FIND;
    }
}


static void find_source(TbSymbolInheritedFlag_t *flag)
{
    if (flag->finding == flag->graph.count) {
        flag->status = flag->owners ? TB_SYMBOL_FLAG_FOUND : TB_SYMBOL_FLAG_ABSENT;
        return;
    }
    size_t index = flag->finding++;
    if (flag->slots[index].visit == TB_SYMBOL_FLAG_QUEUED) {
        flag->active = index;
        flag->status = TB_SYMBOL_FLAG_SOURCE;
    }
}


bool tb_symbol_inherited_flag_need(const TbSymbolInheritedFlag_t *flag, TbSymbolInheritNeed_t *need)
{
    if (!flag || !flag->initialized || !need || flag->status != TB_SYMBOL_FLAG_SOURCE)
        return false;
    *need = (TbSymbolInheritNeed_t){
        .record     = flag->graph.nodes[flag->active].record,
        .context    = flag->graph.nodes[flag->active].context,
        .name       = flag->name,
        .node_index = flag->active
    };
    return true;
}


TbSymbolInheritedFlagStatus_e tb_symbol_inherited_flag_source(TbSymbolInheritedFlag_t *flag,
                                                              const TbSymbolUnitsView_t *units,
                                                              const TbSymbolDiesView_t *tree,
                                                              uint64_t unit_index)
{
    if (!flag || !flag->initialized || flag->status != TB_SYMBOL_FLAG_SOURCE)
        return TB_SYMBOL_FLAG_INVALID;
    const TbSymbolGraphNode_t *node = &flag->graph.nodes[flag->active];
    if (!units || !tree) {
        fail(flag, TB_SYMBOL_FLAG_DEPENDENCY, TB_SYMBOL_FLAG_FAILURE_SOURCE, flag->active);
        return flag->status;
    }
    TbSymbolAttributes_t lookup;
    if (!tb_symbol_graph_node_matches(node, *units, *tree, unit_index) ||
        tb_symbol_attributes_init(&lookup, *tree, (TbSymbolAttributeSelection_t){
            .die_index = node->record.entry_index,
            .name      = flag->name
        }) != TB_SYMBOL_ATTRIBUTE_PENDING) {
        return TB_SYMBOL_FLAG_INVALID;
    }
    flag->lookup     = lookup;
    flag->attributes = tree->attributes;
    flag->phase      = FLAG_FIELD;
    flag->status     = TB_SYMBOL_FLAG_PENDING;
    return flag->status;
}


static void finish_node(TbSymbolInheritedFlag_t *flag)
{
    flag->finding = 0;
    flag->phase   = FLAG_FIND;
}


static bool value_valid(TbSymbolFormView_t value)
{
    return value.numeric && !value.is_signed &&
           ((value.form == TB_SYMBOL_FORM_FLAG && value.bits <= UINT8_MAX) ||
            (value.form == TB_SYMBOL_FORM_FLAG_PRESENT && value.bits == 1));
}


static void field(TbSymbolInheritedFlag_t *flag)
{
    TbSymbolAttributeStatus_e status = tb_symbol_attributes_step(&flag->lookup, 1);
    if (status == TB_SYMBOL_ATTRIBUTE_PENDING)
        return;
    TbSymbolInheritedFlagSlot_t *slot = &flag->slots[flag->active];
    if (status != TB_SYMBOL_ATTRIBUTE_UNIQUE && status != TB_SYMBOL_ATTRIBUTE_MISSING) {
        fail(flag, status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS ? TB_SYMBOL_FLAG_CONFLICT : TB_SYMBOL_FLAG_INVALID,
             TB_SYMBOL_FLAG_FAILURE_FIELD, flag->active);
        TbSymbolAttributeStatus_e diagnostic = tb_symbol_attributes_result(
            &flag->lookup, &flag->failure.selection);
        flag->failure.selection_available = diagnostic == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS;
        return;
    }
    if (tb_symbol_attributes_result(&flag->lookup, &slot->selection) != status) {
        fail(flag, TB_SYMBOL_FLAG_INVALID, TB_SYMBOL_FLAG_FAILURE_FIELD, flag->active);
        return;
    }
    if (status == TB_SYMBOL_ATTRIBUTE_UNIQUE) {
        uint64_t index;
        if (!tb_symbol_attributes_unique(&flag->lookup, &index)) {
            fail(flag, TB_SYMBOL_FLAG_INVALID, TB_SYMBOL_FLAG_FAILURE_FIELD, flag->active);
            return;
        }
        slot->attribute = flag->attributes[index];
        if (!value_valid(slot->attribute.value)) {
            fail(flag, TB_SYMBOL_FLAG_MALFORMED, TB_SYMBOL_FLAG_FAILURE_VALUE, flag->active);
            flag->failure.selection           = slot->selection;
            flag->failure.attribute           = slot->attribute;
            flag->failure.selection_available = true;
            flag->failure.attribute_available = true;
            return;
        }
        if (slot->attribute.value.bits) {
            slot->visit = TB_SYMBOL_FLAG_OWNER;
            ++flag->owners;
            finish_node(flag);
            return;
        }
    }
    /* A physical zero is semantic absence, not an override of inherited presence. */
    slot->visit = TB_SYMBOL_FLAG_EXAMINED;
    flag->edge  = 0;
    flag->scan  = 0;
    flag->phase = FLAG_EDGES;
}


static void edges(TbSymbolInheritedFlag_t *flag)
{
    const TbSymbolGraphNode_t *node = &flag->graph.nodes[flag->active];
    if (flag->edge >= TB_SYMBOL_GRAPH_RELATIONS) {
        if (flag->edge == TB_SYMBOL_GRAPH_RELATIONS)
            finish_node(flag);
        else
            fail(flag, TB_SYMBOL_FLAG_INVALID, TB_SYMBOL_FLAG_FAILURE_EDGE, flag->active);
        return;
    }
    if (!(node->present & (1U << flag->edge))) {
        ++flag->edge;
        return;
    }
    if (flag->scan >= flag->graph.count) {
        fail(flag, TB_SYMBOL_FLAG_INVALID, TB_SYMBOL_FLAG_FAILURE_EDGE, flag->active);
        return;
    }
    size_t                          index  = flag->scan++;
    const TbSymbolReferenceTarget_t *target = &node->edges[flag->edge].target;
    if (same_record(flag->graph.nodes[index].record, *target)) {
        if (flag->graph.nodes[index].record.entry_index != target->entry_index) {
            fail(flag, TB_SYMBOL_FLAG_INVALID, TB_SYMBOL_FLAG_FAILURE_EDGE, flag->active);
            return;
        }
        if (flag->slots[index].visit == TB_SYMBOL_FLAG_UNVISITED)
            flag->slots[index].visit = TB_SYMBOL_FLAG_QUEUED;
        flag->scan = 0;
        ++flag->edge;
    }
}


TbSymbolInheritedFlagStatus_e tb_symbol_inherited_flag_step(TbSymbolInheritedFlag_t *flag, size_t max_steps)
{
    if (!flag || !flag->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_FLAG_INVALID;
    for (size_t step = 0; step < max_steps && flag->status == TB_SYMBOL_FLAG_PENDING; ++step) {
        if (flag->progress == UINT64_MAX) {
            fail(flag, TB_SYMBOL_FLAG_INVALID, TB_SYMBOL_FLAG_FAILURE_PROGRESS, flag->active);
            break;
        }
        ++flag->progress;
        switch (flag->phase) {
        case FLAG_INITIALIZE:
            initialize(flag);
            break;
        case FLAG_FIND:
            find_source(flag);
            break;
        case FLAG_FIELD:
            field(flag);
            break;
        case FLAG_EDGES:
            edges(flag);
            break;
        default:
            fail(flag, TB_SYMBOL_FLAG_INVALID, TB_SYMBOL_FLAG_FAILURE_SCOPE, flag->active);
            break;
        }
    }
    return flag->status;
}


bool tb_symbol_inherited_flag_view(const TbSymbolInheritedFlag_t *flag, TbSymbolInheritedFlagView_t *view)
{
    if (!flag || !flag->initialized || !view ||
        (flag->status != TB_SYMBOL_FLAG_FOUND && flag->status != TB_SYMBOL_FLAG_ABSENT)) {
        return false;
    }
    *view = (TbSymbolInheritedFlagView_t){
        .selected        = flag->graph.nodes[0].record,
        .graph           = flag->graph,
        .slots           = flag->slots,
        .name            = flag->name,
        .owner_count     = flag->owners,
        .value           = flag->owners != 0,
        .value_available = flag->owners != 0
    };
    return true;
}


bool tb_symbol_inherited_flag_owner(const TbSymbolInheritedFlag_t *flag, size_t node_index,
                                     TbSymbolInheritOwner_t *owner)
{
    if (!flag || !flag->initialized || !owner || flag->status != TB_SYMBOL_FLAG_FOUND ||
        node_index >= flag->graph.count || flag->slots[node_index].visit != TB_SYMBOL_FLAG_OWNER) {
        return false;
    }
    *owner = (TbSymbolInheritOwner_t){
        .node_index = node_index,
        .selection  = flag->slots[node_index].selection,
        .attribute  = flag->slots[node_index].attribute
    };
    return true;
}


bool tb_symbol_inherited_flag_failure(const TbSymbolInheritedFlag_t *flag,
                                       TbSymbolInheritedFlagFailure_t *failure)
{
    if (!flag || !flag->initialized || !failure || flag->status == TB_SYMBOL_FLAG_PENDING ||
        flag->status == TB_SYMBOL_FLAG_SOURCE || flag->status == TB_SYMBOL_FLAG_FOUND ||
        flag->status == TB_SYMBOL_FLAG_ABSENT) {
        return false;
    }
    *failure = flag->failure;
    return true;
}


uint64_t tb_symbol_inherited_flag_progress(const TbSymbolInheritedFlag_t *flag)
{
    return flag && flag->initialized ? flag->progress : 0;
}
