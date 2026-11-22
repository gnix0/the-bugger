#include "symbol/inherited_reference.h"

typedef enum {
    REFERENCE_OWNERS,
    REFERENCE_FIND,
    REFERENCE_VALUE,
    REFERENCE_SIGNATURE,
    REFERENCE_TARGET
} ReferencePhase_e;

static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.key.device == right.key.device && left.key.inode == right.key.inode &&
           left.bytes == right.bytes;
}


static bool same_attribute(TbSymbolDieAttribute_t left, TbSymbolDieAttribute_t right)
{
    return left.schema_attribute == right.schema_attribute && left.name == right.name &&
           left.declared_form == right.declared_form && left.offset == right.offset &&
           left.value.form == right.value.form && left.value.bytes == right.value.bytes &&
           left.value.payload_offset == right.value.payload_offset &&
           left.value.payload_bytes == right.value.payload_bytes && left.value.bits == right.value.bits &&
           left.value.numeric == right.value.numeric && left.value.is_signed == right.value.is_signed &&
           left.value.indirect == right.value.indirect;
}


TbSymbolInheritedReferenceStatus_e tb_symbol_inherited_reference_init(
    TbSymbolInheritedReference_t *reference, TbSymbolGraphView_t graph, uint64_t name,
    TbSymbolInheritSlot_t *slots, size_t slot_capacity,
    TbSymbolInheritedReferenceFact_t *facts, size_t fact_capacity,
    const TbSymbolUnitsView_t *info, const TbSymbolUnitsView_t *type_scopes, size_t type_scope_count)
{
    if (!reference || !facts || !graph.nodes || !graph.count ||
        graph.count > SIZE_MAX / sizeof(*graph.nodes) || fact_capacity > SIZE_MAX / sizeof(*facts) ||
        type_scope_count > SIZE_MAX / sizeof(*type_scopes) || (type_scope_count && !type_scopes)) {
        return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    }
    if (name != TB_SYMBOL_INHERIT_TYPE && name != TB_SYMBOL_INHERIT_OBJECT_POINTER)
        return TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED;
    if (fact_capacity < graph.count)
        return TB_SYMBOL_INHERITED_REFERENCE_RESOURCE;
    TbSymbolInherit_t       owners;
    TbSymbolInheritStatus_e status = tb_symbol_inherit_init(&owners, graph, name, slots, slot_capacity);
    if (status != TB_SYMBOL_INHERIT_PENDING) {
        if (status == TB_SYMBOL_INHERIT_RESOURCE)
            return TB_SYMBOL_INHERITED_REFERENCE_RESOURCE;
        if (status == TB_SYMBOL_INHERIT_UNSUPPORTED)
            return TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED;
        return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    }
    *reference = (TbSymbolInheritedReference_t){
        .owners           = owners,
        .facts            = facts,
        .info             = info,
        .type_scopes      = type_scopes,
        .status           = TB_SYMBOL_INHERITED_REFERENCE_PENDING,
        .type_scope_count = type_scope_count,
        .phase            = REFERENCE_OWNERS,
        .initialized      = true
    };
    return reference->status;
}


static TbSymbolInheritedReferenceStatus_e fail(TbSymbolInheritedReference_t *reference,
                                               TbSymbolInheritedReferenceStatus_e status,
                                               TbSymbolReferenceStatus_e reason)
{
    reference->status = status;
    reference->failure = (TbSymbolInheritedReferenceFailure_t){
        .status           = status,
        .reference_status = reason,
        .field            = {
            .node_index   = reference->candidate.node_index,
            .selection    = reference->candidate.selection,
            .attribute    = reference->candidate.attribute
        },
        .field_available     = reference->phase >= REFERENCE_VALUE,
        .reference           = reference->candidate.reference,
        .target              = reference->need,
        .node_index          = reference->candidate.node_index,
        .reference_available = reference->phase >= REFERENCE_SIGNATURE,
        .target_available    = reference->phase == REFERENCE_TARGET
    };
    return status;
}


static TbSymbolInheritedReferenceStatus_e reference_failure(TbSymbolInheritedReference_t *reference,
                                                            TbSymbolReferenceStatus_e status)
{
    TbSymbolInheritedReferenceStatus_e outcome = TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    if (status == TB_SYMBOL_REFERENCE_DEPENDENCY || status == TB_SYMBOL_REFERENCE_MISSING_TYPE)
        outcome = TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY;
    else if (status == TB_SYMBOL_REFERENCE_UNSUPPORTED || status == TB_SYMBOL_REFERENCE_COVERAGE)
        outcome = TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED;
    else if (status == TB_SYMBOL_REFERENCE_CONFLICT)
        outcome = TB_SYMBOL_INHERITED_REFERENCE_CONFLICT;
    else if (status == TB_SYMBOL_REFERENCE_NOT_REFERENCE || status == TB_SYMBOL_REFERENCE_RANGE ||
             status == TB_SYMBOL_REFERENCE_MISSING_DIE)
        outcome = TB_SYMBOL_INHERITED_REFERENCE_MALFORMED;
    return fail(reference, outcome, status);
}


static void accept(TbSymbolInheritedReference_t *reference, TbSymbolReferenceTarget_t target)
{
    reference->candidate.target = target;
    reference->facts[reference->count++] = reference->candidate;
    reference->phase = REFERENCE_FIND;
    reference->status = TB_SYMBOL_INHERITED_REFERENCE_PENDING;
}


static void owners(TbSymbolInheritedReference_t *reference)
{
    TbSymbolInheritStatus_e status = tb_symbol_inherit_step(&reference->owners, 1);
    if (status == TB_SYMBOL_INHERIT_PENDING)
        return;
    if (status == TB_SYMBOL_INHERIT_SOURCE) {
        reference->status = TB_SYMBOL_INHERITED_REFERENCE_SOURCE;
        return;
    }
    if (status == TB_SYMBOL_INHERIT_ABSENT) {
        reference->status = TB_SYMBOL_INHERITED_REFERENCE_ABSENT;
        return;
    }
    if (status == TB_SYMBOL_INHERIT_UNIQUE || status == TB_SYMBOL_INHERIT_MULTIPLE) {
        reference->phase = REFERENCE_FIND;
        return;
    }
    TbSymbolInheritedReferenceStatus_e outcome = TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    if (status == TB_SYMBOL_INHERIT_CONFLICT)
        outcome = TB_SYMBOL_INHERITED_REFERENCE_CONFLICT;
    else if (status == TB_SYMBOL_INHERIT_DEPENDENCY)
        outcome = TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY;
    else if (status == TB_SYMBOL_INHERIT_RESOURCE)
        outcome = TB_SYMBOL_INHERITED_REFERENCE_RESOURCE;
    else if (status == TB_SYMBOL_INHERIT_UNSUPPORTED)
        outcome = TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED;
    (void)fail(reference, outcome, TB_SYMBOL_REFERENCE_INVALID);
    (void)tb_symbol_inherit_failure(&reference->owners, &reference->failure.owner);
}


static void find(TbSymbolInheritedReference_t *reference)
{
    if (reference->next_owner == reference->owners.graph.count) {
        reference->status = TB_SYMBOL_INHERITED_REFERENCE_FOUND;
        return;
    }
    size_t                 index = reference->next_owner++;
    TbSymbolInheritOwner_t owner;
    if (!tb_symbol_inherit_owner(&reference->owners, index, &owner))
        return;
    reference->candidate = (TbSymbolInheritedReferenceFact_t){
        .node_index = index,
        .selection  = owner.selection,
        .attribute  = owner.attribute
    };
    reference->phase = REFERENCE_VALUE;
    reference->status = TB_SYMBOL_INHERITED_REFERENCE_SOURCE;
}


static void signature(TbSymbolInheritedReference_t *reference)
{
    TbSymbolReferenceStatus_e status = tb_symbol_type_reference_step(&reference->signature);
    if (status == TB_SYMBOL_REFERENCE_PENDING)
        return;
    if (status != TB_SYMBOL_REFERENCE_READY ||
        !tb_symbol_type_reference_need(&reference->signature, &reference->need)) {
        (void)reference_failure(reference, status);
        return;
    }
    reference->phase = REFERENCE_TARGET;
    reference->status = TB_SYMBOL_INHERITED_REFERENCE_TARGET;
}


TbSymbolInheritedReferenceStatus_e tb_symbol_inherited_reference_step(
    TbSymbolInheritedReference_t *reference, size_t max_steps)
{
    if (!reference || !reference->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    for (size_t step = 0; step < max_steps &&
         reference->status == TB_SYMBOL_INHERITED_REFERENCE_PENDING; ++step) {
        if (reference->progress == UINT64_MAX)
            return fail(reference, TB_SYMBOL_INHERITED_REFERENCE_INVALID, TB_SYMBOL_REFERENCE_INVALID);
        ++reference->progress;
        if (reference->phase == REFERENCE_OWNERS)
            owners(reference);
        else if (reference->phase == REFERENCE_FIND)
            find(reference);
        else if (reference->phase == REFERENCE_SIGNATURE)
            signature(reference);
        else
            return fail(reference, TB_SYMBOL_INHERITED_REFERENCE_INVALID, TB_SYMBOL_REFERENCE_INVALID);
    }
    return reference->status;
}


bool tb_symbol_inherited_reference_source_need(const TbSymbolInheritedReference_t *reference,
                                                TbSymbolInheritedReferenceSource_t *need)
{
    if (!reference || !reference->initialized || !need ||
        reference->status != TB_SYMBOL_INHERITED_REFERENCE_SOURCE)
        return false;
    if (reference->phase == REFERENCE_OWNERS) {
        TbSymbolInheritNeed_t source;
        if (!tb_symbol_inherit_need(&reference->owners, &source))
            return false;
        *need = (TbSymbolInheritedReferenceSource_t){
            .kind   = TB_SYMBOL_INHERITED_REFERENCE_OWNER_SOURCE,
            .source = source
        };
    } else {
        const TbSymbolGraphNode_t *node = &reference->owners.graph.nodes[reference->candidate.node_index];
        *need = (TbSymbolInheritedReferenceSource_t){
            .kind           = TB_SYMBOL_INHERITED_REFERENCE_VALUE_SOURCE,
            .source         = {
                .record     = node->record,
                .context    = node->context,
                .name       = reference->owners.name,
                .node_index = reference->candidate.node_index
            }
        };
    }
    return true;
}


TbSymbolInheritedReferenceStatus_e tb_symbol_inherited_reference_source(
    TbSymbolInheritedReference_t *reference, const TbSymbolUnitsView_t *units,
    const TbSymbolDiesView_t *tree, uint64_t unit_index)
{
    if (!reference || !reference->initialized || reference->status != TB_SYMBOL_INHERITED_REFERENCE_SOURCE)
        return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    if (reference->phase == REFERENCE_OWNERS) {
        TbSymbolInheritStatus_e status = tb_symbol_inherit_source(&reference->owners, units, tree,
                                                                   unit_index);
        if (status == TB_SYMBOL_INHERIT_INVALID)
            return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
        if (status == TB_SYMBOL_INHERIT_DEPENDENCY) {
            (void)fail(reference, TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY, TB_SYMBOL_REFERENCE_DEPENDENCY);
            (void)tb_symbol_inherit_failure(&reference->owners, &reference->failure.owner);
            return reference->status;
        }
        reference->status = TB_SYMBOL_INHERITED_REFERENCE_PENDING;
        return reference->status;
    }
    if (!units || !tree)
        return fail(reference, TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY, TB_SYMBOL_REFERENCE_DEPENDENCY);
    const TbSymbolGraphNode_t *node = &reference->owners.graph.nodes[reference->candidate.node_index];
    if (!tb_symbol_graph_node_matches(node, *units, *tree, unit_index))
        return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    uint64_t                  index = reference->candidate.selection.first_index;
    const TbSymbolDieEntry_t *entry = &tree->entries[node->record.entry_index];
    if (!tree->attributes || index < entry->first_attribute ||
        index - entry->first_attribute >= entry->attribute_count || index >= tree->attribute_count ||
        !same_attribute(tree->attributes[index], reference->candidate.attribute)) {
        return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    }
    TbSymbolReferenceSelection_t selection = {
        .unit_index      = unit_index,
        .die_index       = node->record.entry_index,
        .attribute_index = reference->candidate.selection.first_index -
                           tree->entries[node->record.entry_index].first_attribute
    };
    TbSymbolReferenceStatus_e status = tb_symbol_reference_prepare(*units, *tree, selection,
                                                                   &reference->candidate.reference);
    if (status != TB_SYMBOL_REFERENCE_READY)
        return reference_failure(reference, status);
    reference->phase = REFERENCE_SIGNATURE;
    TbSymbolReference_t raw = reference->candidate.reference;
    if (raw.kind == TB_SYMBOL_REFERENCE_UNIT) {
        TbSymbolReferenceTarget_t target;
        status = tb_symbol_reference_unit(raw, *tree, &target);
        if (status != TB_SYMBOL_REFERENCE_FOUND)
            return reference_failure(reference, status);
        accept(reference, target);
    } else if (raw.kind == TB_SYMBOL_REFERENCE_INFO) {
        if (!reference->info)
            return reference_failure(reference, TB_SYMBOL_REFERENCE_DEPENDENCY);
        status = tb_symbol_reference_info(raw, *reference->info, &reference->need);
        if (status != TB_SYMBOL_REFERENCE_READY)
            return reference_failure(reference, status);
        reference->phase = REFERENCE_TARGET;
        reference->status = TB_SYMBOL_INHERITED_REFERENCE_TARGET;
    } else if (raw.kind == TB_SYMBOL_REFERENCE_SIGNATURE) {
        if (!reference->type_scope_count)
            return reference_failure(reference, TB_SYMBOL_REFERENCE_DEPENDENCY);
        status = tb_symbol_type_reference_init(&reference->signature, raw, reference->type_scopes,
                                                reference->type_scope_count);
        if (status != TB_SYMBOL_REFERENCE_PENDING)
            return reference_failure(reference, status);
        reference->status = TB_SYMBOL_INHERITED_REFERENCE_PENDING;
    } else {
        return reference_failure(reference, TB_SYMBOL_REFERENCE_UNSUPPORTED);
    }
    return reference->status;
}


bool tb_symbol_inherited_reference_target_need(const TbSymbolInheritedReference_t *reference,
                                                TbSymbolReferenceNeed_t *need)
{
    if (!reference || !reference->initialized || !need ||
        reference->status != TB_SYMBOL_INHERITED_REFERENCE_TARGET)
        return false;
    *need = reference->need;
    return true;
}


TbSymbolInheritedReferenceStatus_e tb_symbol_inherited_reference_target(
    TbSymbolInheritedReference_t *reference, const TbSymbolDiesView_t *tree)
{
    if (!reference || !reference->initialized || reference->status != TB_SYMBOL_INHERITED_REFERENCE_TARGET)
        return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    if (!tree)
        return reference_failure(reference, TB_SYMBOL_REFERENCE_DEPENDENCY);
    if (!same_backing(reference->need.backing, tree->backing) ||
        reference->need.section_index != tree->section_index ||
        reference->need.unit_offset != tree->unit_offset)
        return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
    TbSymbolReferenceTarget_t target;
    TbSymbolReferenceStatus_e status = tb_symbol_reference_target(reference->need, *tree, &target);
    if (status != TB_SYMBOL_REFERENCE_FOUND)
        return reference_failure(reference, status);
    accept(reference, target);
    return reference->status;
}


bool tb_symbol_inherited_reference_view(const TbSymbolInheritedReference_t *reference,
                                         TbSymbolInheritedReferenceView_t *view)
{
    if (!reference || !reference->initialized || !view ||
        (reference->status != TB_SYMBOL_INHERITED_REFERENCE_FOUND &&
         reference->status != TB_SYMBOL_INHERITED_REFERENCE_ABSENT)) {
        return false;
    }
    *view = (TbSymbolInheritedReferenceView_t){
        .selected = reference->owners.graph.nodes[0].record,
        .graph    = reference->owners.graph,
        .slots    = reference->owners.slots,
        .facts    = reference->facts,
        .count    = reference->count,
        .name     = reference->owners.name
    };
    return true;
}


bool tb_symbol_inherited_reference_failure(const TbSymbolInheritedReference_t *reference,
                                            TbSymbolInheritedReferenceFailure_t *failure)
{
    if (!reference || !reference->initialized || !failure ||
        reference->status == TB_SYMBOL_INHERITED_REFERENCE_PENDING ||
        reference->status == TB_SYMBOL_INHERITED_REFERENCE_SOURCE ||
        reference->status == TB_SYMBOL_INHERITED_REFERENCE_TARGET ||
        reference->status == TB_SYMBOL_INHERITED_REFERENCE_FOUND ||
        reference->status == TB_SYMBOL_INHERITED_REFERENCE_ABSENT) {
        return false;
    }
    *failure = reference->failure;
    return true;
}


uint64_t tb_symbol_inherited_reference_progress(const TbSymbolInheritedReference_t *reference)
{
    return reference && reference->initialized ? reference->progress : 0;
}
