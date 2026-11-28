#include "symbol/relations.h"

typedef struct {
    uint64_t tag;
    uint16_t version;
} TagProfile_t;

static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.bytes == right.bytes && left.key.device == right.key.device &&
           left.key.inode == right.key.inode;
}


static uint64_t attribute_name(TbSymbolRelationKind_e kind)
{
    switch (kind) {
    case TB_SYMBOL_RELATION_SPECIFICATION:
        return 0x47;
    case TB_SYMBOL_RELATION_ORIGIN:
        return 0x31;
    case TB_SYMBOL_RELATION_EXTENSION:
        return 0x54;
    default:
        return 0;
    }
}


static TbSymbolRelationStatus_e reference_status(TbSymbolReferenceStatus_e status)
{
    switch (status) {
    case TB_SYMBOL_REFERENCE_READY:
        return TB_SYMBOL_RELATION_READY;
    case TB_SYMBOL_REFERENCE_FOUND:
        return TB_SYMBOL_RELATION_FOUND;
    case TB_SYMBOL_REFERENCE_RANGE:
        return TB_SYMBOL_RELATION_RANGE;
    case TB_SYMBOL_REFERENCE_DEPENDENCY:
        return TB_SYMBOL_RELATION_DEPENDENCY;
    case TB_SYMBOL_REFERENCE_UNSUPPORTED:
        return TB_SYMBOL_RELATION_UNSUPPORTED;
    case TB_SYMBOL_REFERENCE_MISSING_DIE:
        return TB_SYMBOL_RELATION_MISSING_TARGET;
    case TB_SYMBOL_REFERENCE_NOT_REFERENCE:
        return TB_SYMBOL_RELATION_MALFORMED;
    default:
        return TB_SYMBOL_RELATION_INVALID;
    }
}


TbSymbolRelationStatus_e tb_symbol_relation_init(TbSymbolRelation_t *relation, TbSymbolUnitsView_t units,
                                                 TbSymbolDiesView_t tree,
                                                 TbSymbolRelationSelection_t selection)
{
    TbSymbolAttributes_t lookup;
    if (!relation || !units.entries || selection.unit_index >= units.count ||
        !units.entries[selection.unit_index].header_available ||
        (units.entries[selection.unit_index].header.version != 4 &&
         units.entries[selection.unit_index].header.version != 5) ||
        units.section_index != tree.section_index ||
        units.entries[selection.unit_index].offset != tree.unit_offset ||
        !same_backing(units.backing, tree.backing) ||
        tb_symbol_attributes_init(&lookup, tree, (TbSymbolAttributeSelection_t) {
            .die_index = selection.die_index,
            .name      = attribute_name(selection.kind)
        }) != TB_SYMBOL_ATTRIBUTE_PENDING) {
        return TB_SYMBOL_RELATION_INVALID;
    }

    *relation = (TbSymbolRelation_t) {
        .units            = units,
        .tree             = tree,
        .selection        = selection,
        .lookup           = lookup,
        .status           = TB_SYMBOL_RELATION_PENDING,
        .reference_status = TB_SYMBOL_REFERENCE_INVALID,
        .initialized      = true
    };

    return relation->status;
}


TbSymbolRelationStatus_e tb_symbol_relation_step(TbSymbolRelation_t *relation, size_t max_steps)
{
    if (!relation || !relation->initialized)
        return TB_SYMBOL_RELATION_INVALID;

    if (relation->status != TB_SYMBOL_RELATION_PENDING)
        return relation->status;

    TbSymbolAttributeStatus_e status = tb_symbol_attributes_step(&relation->lookup, max_steps);
    if (status == TB_SYMBOL_ATTRIBUTE_PENDING)
        return relation->status;

    if (status != TB_SYMBOL_ATTRIBUTE_UNIQUE) {
        if (status == TB_SYMBOL_ATTRIBUTE_MISSING)
            relation->status = TB_SYMBOL_RELATION_ABSENT;
        else if (status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS)
            relation->status = TB_SYMBOL_RELATION_CONFLICT;
        else
            relation->status = TB_SYMBOL_RELATION_INVALID;
        return relation->status;
    }

    uint64_t index;
    if (!tb_symbol_attributes_unique(&relation->lookup, &index)) {
        relation->status = TB_SYMBOL_RELATION_INVALID;
        return relation->status;
    }

    TbSymbolReferenceSelection_t selected = {
        .unit_index      = relation->selection.unit_index,
        .die_index       = relation->selection.die_index,
        .attribute_index = index - relation->tree.entries[relation->selection.die_index].first_attribute
    };

    relation->reference_status = tb_symbol_reference_prepare(relation->units, relation->tree, selected,
                                                              &relation->source.reference);
    relation->status = reference_status(relation->reference_status);

    if (relation->status == TB_SYMBOL_RELATION_READY) {
        relation->source.kind              = relation->selection.kind;
        relation->source.source_tag        = relation->tree.entries[relation->selection.die_index].tag;
        relation->source.source_unit_index = relation->selection.unit_index;
        relation->source.source_version    = relation->units.entries[selected.unit_index].header.version;
        relation->source.source_unit_kind  = relation->tree.kind;
        relation->source.abbrev_section    = relation->tree.abbrev_section;
        relation->source.abbrev_offset     = relation->tree.abbrev_offset;
        relation->source.attribute         = relation->lookup.result;
        relation->source.field             = relation->tree.attributes[index];
    }

    return relation->status;
}


bool tb_symbol_relation_source(const TbSymbolRelation_t *relation, TbSymbolRelationSource_t *source)
{
    if (!relation || !relation->initialized || !source || relation->status != TB_SYMBOL_RELATION_READY)
        return false;

    *source = relation->source;

    return true;
}


bool tb_symbol_relation_diagnostic(const TbSymbolRelation_t *relation, TbSymbolAttributeResult_t *attribute,
                                   TbSymbolReferenceStatus_e *reference)
{
    if (!relation || !relation->initialized || !attribute || !reference ||
        relation->status == TB_SYMBOL_RELATION_PENDING || relation->status == TB_SYMBOL_RELATION_INVALID) {
        return false;
    }

    TbSymbolAttributeResult_t candidate;
    TbSymbolAttributeStatus_e status = tb_symbol_attributes_result(&relation->lookup, &candidate);
    if (status != TB_SYMBOL_ATTRIBUTE_UNIQUE && status != TB_SYMBOL_ATTRIBUTE_MISSING &&
        status != TB_SYMBOL_ATTRIBUTE_AMBIGUOUS) {
        return false;
    }

    *attribute = candidate;
    *reference = relation->reference_status;

    return true;
}


static bool source_valid(TbSymbolRelationSource_t source)
{
    return attribute_name(source.kind) && (source.source_version == 4 || source.source_version == 5) &&
           source.attribute.matches == 1 &&
           source.attribute.name == attribute_name(source.kind) &&
           source.attribute.first_index != UINT64_MAX && source.attribute.second_index == UINT64_MAX &&
           source.field.name == source.attribute.name &&
           source.field.offset == source.attribute.first_offset &&
           source.reference.source_attribute == source.field.offset &&
           source.reference.source_section == source.attribute.section_index &&
           source.reference.source_unit == source.attribute.unit_offset &&
           source.reference.source_record == source.attribute.record_offset &&
           same_backing(source.reference.backing, source.attribute.backing) &&
           source.reference.form == source.field.value.form &&
           source.reference.value == source.field.value.bits &&
           source.field.value.numeric && !source.field.value.is_signed;
}


static bool supported_tag(TagProfile_t profile)
{
    uint64_t tag = profile.tag;
    /* Extensions may reuse standard attributes for different relationships. */
    if (profile.version == 4 && tag >= 0x44)
        return false;

    return (tag >= 0x01 && tag <= 0x05) || tag == 0x08 || tag == 0x0a || tag == 0x0b || tag == 0x0d ||
           (tag >= 0x0f && tag <= 0x13) || (tag >= 0x15 && tag <= 0x3d) ||
           (tag >= 0x3f && tag <= 0x4b);
}


TbSymbolRelationStatus_e tb_symbol_relation_need(TbSymbolRelationSource_t source,
                                                 const TbSymbolUnitsView_t *info,
                                                 TbSymbolRelationNeed_t *need)
{
    if (!need || !source_valid(source))
        return TB_SYMBOL_RELATION_INVALID;

    if (!supported_tag((TagProfile_t) {
        .tag     = source.source_tag,
        .version = source.source_version
    }))
        return TB_SYMBOL_RELATION_UNSUPPORTED;

    TbSymbolRelationNeed_t candidate = {
        .source = source
    };

    if (source.reference.kind == TB_SYMBOL_REFERENCE_UNIT) {
        candidate.target_version = source.source_version;
        candidate.target = (TbSymbolReferenceNeed_t) {
            .backing       = source.reference.backing,
            .section_index = source.reference.source_section,
            .unit_index    = source.source_unit_index,
            .unit_offset   = source.reference.source_unit,
            .record_offset = source.reference.target_offset
        };
    } else if (source.reference.kind == TB_SYMBOL_REFERENCE_INFO) {
        if (!info)
            return TB_SYMBOL_RELATION_DEPENDENCY;

        TbSymbolReferenceStatus_e status = tb_symbol_reference_info(source.reference, *info,
                                                                    &candidate.target);
        if (status != TB_SYMBOL_REFERENCE_READY)
            return reference_status(status);

        candidate.target_version = info->entries[candidate.target.unit_index].header.version;

        if (candidate.target_version != 4 && candidate.target_version != 5)
            return TB_SYMBOL_RELATION_UNSUPPORTED;
    } else if (source.reference.kind == TB_SYMBOL_REFERENCE_SIGNATURE ||
               source.reference.kind == TB_SYMBOL_REFERENCE_SUPPLEMENTARY) {
        return TB_SYMBOL_RELATION_UNSUPPORTED;
    } else {
        return TB_SYMBOL_RELATION_INVALID;
    }
    *need = candidate;

    return TB_SYMBOL_RELATION_READY;
}


static bool compatible(const TbSymbolRelationEdge_t *edge)
{
    switch (edge->source.kind) {
    case TB_SYMBOL_RELATION_SPECIFICATION:
        return edge->source.source_tag == edge->target_tag ||
               (edge->source.source_tag == 0x34 && edge->target_tag == 0x0d && edge->target_version == 4 &&
                (edge->target_parent_tag == 0x02 || edge->target_parent_tag == 0x13 ||
                 edge->target_parent_tag == 0x17));
    case TB_SYMBOL_RELATION_ORIGIN:
        if (edge->source.source_tag == 0x1d)
            return edge->target_tag == 0x2e;
        return edge->source.source_tag == edge->target_tag;
    case TB_SYMBOL_RELATION_EXTENSION:
        return edge->source.source_tag == 0x39 && edge->target_tag == 0x39;
    default:
        return false;
    }
}


TbSymbolRelationStatus_e tb_symbol_relation_target(TbSymbolRelationNeed_t need,
                                                   const TbSymbolDiesView_t *tree,
                                                   TbSymbolRelationEdge_t *edge)
{
    if (!edge || !source_valid(need.source) || (need.target_version != 4 && need.target_version != 5) ||
        !same_backing(need.source.reference.backing, need.target.backing)) {
        return TB_SYMBOL_RELATION_INVALID;
    }

    if (!supported_tag((TagProfile_t) {
        .tag     = need.source.source_tag,
        .version = need.source.source_version
    }))
        return TB_SYMBOL_RELATION_UNSUPPORTED;

    const TbSymbolReference_t *reference = &need.source.reference;
    if (reference->kind == TB_SYMBOL_REFERENCE_UNIT) {
        if (need.target_version != need.source.source_version ||
            need.target.section_index != reference->source_section ||
            need.target.unit_offset != reference->source_unit ||
            need.target.record_offset != reference->target_offset) {
            return TB_SYMBOL_RELATION_INVALID;
        }
    } else if (reference->kind == TB_SYMBOL_REFERENCE_INFO) {
        if (need.target.record_offset != reference->value)
            return TB_SYMBOL_RELATION_INVALID;
    } else {
        return TB_SYMBOL_RELATION_UNSUPPORTED;
    }

    if (!tree)
        return TB_SYMBOL_RELATION_DEPENDENCY;

    TbSymbolRelationEdge_t candidate = {
        .source               = need.source,
        .target_parent_record = UINT64_MAX
    };

    TbSymbolReferenceStatus_e status = tb_symbol_reference_target(need.target, *tree, &candidate.target);
    if (status != TB_SYMBOL_REFERENCE_FOUND)
        return reference_status(status);

    if (reference->source_section == candidate.target.section_index &&
        reference->source_unit == candidate.target.unit_offset &&
        reference->source_record == candidate.target.record_offset) {
        return TB_SYMBOL_RELATION_SELF_CYCLE;
    }

    const TbSymbolDieEntry_t *entry = &tree->entries[candidate.target.entry_index];
    if (entry->parent != UINT64_MAX) {
        if (entry->parent >= candidate.target.entry_index)
            return TB_SYMBOL_RELATION_INVALID;

        candidate.target_parent_record = tree->entries[entry->parent].offset;
        candidate.target_parent_tag    = tree->entries[entry->parent].tag;
    }
    candidate.target_tag            = entry->tag;
    candidate.target_version        = need.target_version;
    candidate.target_unit_kind      = tree->kind;
    candidate.target_abbrev_section = tree->abbrev_section;
    candidate.target_abbrev_offset  = tree->abbrev_offset;

    if (!supported_tag((TagProfile_t) {
        .tag     = candidate.target_tag,
        .version = candidate.target_version
    })) {
        return TB_SYMBOL_RELATION_UNSUPPORTED;
    }

    if (!compatible(&candidate))
        return TB_SYMBOL_RELATION_TAG_MISMATCH;

    *edge = candidate;

    return TB_SYMBOL_RELATION_FOUND;
}
