#include "symbol/references.h"

static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.bytes == right.bytes && left.key.device == right.key.device &&
           left.key.inode == right.key.inode;
}


TbSymbolReferenceStatus_e tb_symbol_reference_prepare(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                                                      TbSymbolReferenceSelection_t selection,
                                                      TbSymbolReference_t *reference)
{
    if (!reference || !units.entries || selection.unit_index >= units.count || !tree.entries ||
        selection.die_index >= tree.count || !tree.attributes || !same_backing(units.backing, tree.backing) ||
        units.section_index != tree.section_index ||
        units.entries[selection.unit_index].offset != tree.unit_offset ||
        !units.entries[selection.unit_index].header_available) {
        return TB_SYMBOL_REFERENCE_INVALID;
    }
    const TbSymbolDieEntry_t *die = &tree.entries[selection.die_index];
    uint64_t index;
    if (selection.attribute_index >= die->attribute_count ||
        !tb_symbol_add(die->first_attribute, selection.attribute_index, &index) ||
        index >= tree.attribute_count) {
        return TB_SYMBOL_REFERENCE_INVALID;
    }
    const TbSymbolDieAttribute_t *attribute = &tree.attributes[index];
    TbSymbolReferenceKind_e kind;
    switch (attribute->value.form) {
    case TB_SYMBOL_FORM_REF1:
    case TB_SYMBOL_FORM_REF2:
    case TB_SYMBOL_FORM_REF4:
    case TB_SYMBOL_FORM_REF8:
    case TB_SYMBOL_FORM_REF_UDATA:
        kind = TB_SYMBOL_REFERENCE_UNIT;
        break;
    case TB_SYMBOL_FORM_REF_ADDR:
        kind = TB_SYMBOL_REFERENCE_INFO;
        break;
    case TB_SYMBOL_FORM_REF_SIG8:
        kind = TB_SYMBOL_REFERENCE_SIGNATURE;
        break;
    case TB_SYMBOL_FORM_REF_SUP4:
    case TB_SYMBOL_FORM_REF_SUP8:
        kind = TB_SYMBOL_REFERENCE_SUPPLEMENTARY;
        break;
    default:
        return TB_SYMBOL_REFERENCE_NOT_REFERENCE;
    }
    if (!attribute->value.numeric || attribute->value.is_signed)
        return TB_SYMBOL_REFERENCE_RANGE;
    TbSymbolReference_t candidate = {
        .backing          = tree.backing,
        .kind             = kind,
        .form             = attribute->value.form,
        .value            = attribute->value.bits,
        .source_section   = tree.section_index,
        .source_unit      = tree.unit_offset,
        .source_record    = die->offset,
        .source_attribute = attribute->offset
    };
    if (kind == TB_SYMBOL_REFERENCE_UNIT) {
        uint64_t body;
        const TbSymbolUnitEntry_t *unit = &units.entries[selection.unit_index];
        if (!tb_symbol_add(unit->offset, unit->header.header_bytes, &body) ||
            !tb_symbol_add(unit->offset, candidate.value, &candidate.target_offset) ||
            candidate.target_offset < body || candidate.target_offset >= unit->extent.end_offset) {
            return TB_SYMBOL_REFERENCE_RANGE;
        }
    }
    *reference = candidate;
    return TB_SYMBOL_REFERENCE_READY;
}


TbSymbolReferenceStatus_e tb_symbol_reference_unit(TbSymbolReference_t reference, TbSymbolDiesView_t tree,
                                                   TbSymbolReferenceTarget_t *target)
{
    if (!target)
        return TB_SYMBOL_REFERENCE_INVALID;
    if (reference.kind == TB_SYMBOL_REFERENCE_SUPPLEMENTARY)
        return TB_SYMBOL_REFERENCE_UNSUPPORTED;
    if (reference.kind == TB_SYMBOL_REFERENCE_INFO || reference.kind == TB_SYMBOL_REFERENCE_SIGNATURE)
        return TB_SYMBOL_REFERENCE_DEPENDENCY;
    if (reference.kind != TB_SYMBOL_REFERENCE_UNIT)
        return TB_SYMBOL_REFERENCE_INVALID;
    return tb_symbol_reference_target((TbSymbolReferenceNeed_t){
        .backing       = reference.backing,
        .section_index = reference.source_section,
        .unit_offset   = reference.source_unit,
        .record_offset = reference.target_offset
    }, tree, target);
}


TbSymbolReferenceStatus_e tb_symbol_reference_target(TbSymbolReferenceNeed_t need, TbSymbolDiesView_t tree,
                                                     TbSymbolReferenceTarget_t *target)
{
    if (!target)
        return TB_SYMBOL_REFERENCE_INVALID;
    if (!tree.entries || !tree.count || !same_backing(need.backing, tree.backing) ||
        need.section_index != tree.section_index || need.unit_offset != tree.unit_offset) {
        return TB_SYMBOL_REFERENCE_DEPENDENCY;
    }
    uint64_t begin = 0;
    uint64_t end = tree.count;
    while (begin < end) {
        uint64_t middle = begin + (end - begin) / 2;
        if (tree.entries[middle].offset < need.record_offset)
            begin = middle + 1;
        else
            end = middle;
    }
    if (begin == tree.count || tree.entries[begin].offset != need.record_offset)
        return TB_SYMBOL_REFERENCE_MISSING_DIE;
    *target = (TbSymbolReferenceTarget_t){
        .backing       = tree.backing,
        .section_index = tree.section_index,
        .unit_offset   = tree.unit_offset,
        .record_offset = tree.entries[begin].offset,
        .entry_index   = begin
    };
    return TB_SYMBOL_REFERENCE_FOUND;
}


TbSymbolReferenceStatus_e tb_symbol_reference_info(TbSymbolReference_t reference, TbSymbolUnitsView_t units,
                                                   TbSymbolReferenceNeed_t *need)
{
    if (!need || reference.kind != TB_SYMBOL_REFERENCE_INFO || units.kind != TB_SYMBOL_DWARF_INFO ||
        (units.count && !units.entries)) {
        return TB_SYMBOL_REFERENCE_INVALID;
    }
    if (!same_backing(reference.backing, units.backing))
        return TB_SYMBOL_REFERENCE_DEPENDENCY;
    uint64_t begin = 0;
    uint64_t end = units.count;
    while (begin < end) {
        uint64_t middle = begin + (end - begin) / 2;
        if (units.entries[middle].offset <= reference.value)
            begin = middle + 1;
        else
            end = middle;
    }
    if (!begin)
        return TB_SYMBOL_REFERENCE_RANGE;
    uint64_t index = begin - 1;
    const TbSymbolUnitEntry_t *unit = &units.entries[index];
    if (reference.value >= unit->extent.end_offset)
        return TB_SYMBOL_REFERENCE_RANGE;
    if (!unit->header_available)
        return TB_SYMBOL_REFERENCE_UNSUPPORTED;
    uint64_t body;
    if (!tb_symbol_add(unit->offset, unit->header.header_bytes, &body) || reference.value < body)
        return TB_SYMBOL_REFERENCE_RANGE;
    *need = (TbSymbolReferenceNeed_t){
        .backing       = units.backing,
        .section_index = units.section_index,
        .unit_index    = index,
        .unit_offset   = unit->offset,
        .record_offset = reference.value
    };
    return TB_SYMBOL_REFERENCE_READY;
}


TbSymbolReferenceStatus_e tb_symbol_type_reference_init(TbSymbolTypeReference_t *lookup,
                                                        TbSymbolReference_t reference,
                                                        const TbSymbolUnitsView_t *scopes, size_t count)
{
    if (!lookup || reference.kind != TB_SYMBOL_REFERENCE_SIGNATURE || (count && !scopes))
        return TB_SYMBOL_REFERENCE_INVALID;
    *lookup = (TbSymbolTypeReference_t){
        .scopes      = scopes,
        .scope_count = count,
        .reference   = reference,
        .status      = TB_SYMBOL_REFERENCE_PENDING,
        .initialized = true
    };
    return lookup->status;
}


static void type_candidate(TbSymbolTypeReference_t *lookup, TbSymbolUnitsView_t units,
                           const TbSymbolUnitEntry_t *unit)
{
    uint64_t offset;
    uint64_t body;
    if (!tb_symbol_add(unit->offset, unit->header.type_offset, &offset) ||
        !tb_symbol_add(unit->offset, unit->header.header_bytes, &body) ||
        offset < body || offset >= unit->extent.end_offset) {
        lookup->status = TB_SYMBOL_REFERENCE_RANGE;
        return;
    }
    TbSymbolReferenceNeed_t candidate = {
        .backing       = units.backing,
        .section_index = units.section_index,
        .unit_index    = lookup->unit_index,
        .unit_offset   = unit->offset,
        .record_offset = offset
    };
    if (!lookup->found) {
        lookup->candidate = candidate;
        lookup->found = true;
    } else if (lookup->candidate.section_index != candidate.section_index ||
               lookup->candidate.unit_offset != candidate.unit_offset ||
               lookup->candidate.record_offset != candidate.record_offset) {
        lookup->conflict = true;
    }
}


TbSymbolReferenceStatus_e tb_symbol_type_reference_step(TbSymbolTypeReference_t *lookup)
{
    if (!lookup || !lookup->initialized)
        return TB_SYMBOL_REFERENCE_INVALID;
    if (lookup->status != TB_SYMBOL_REFERENCE_PENDING)
        return lookup->status;
    if (lookup->scope_index == lookup->scope_count) {
        lookup->status = lookup->conflict ? TB_SYMBOL_REFERENCE_CONFLICT
                             : lookup->coverage ? TB_SYMBOL_REFERENCE_COVERAGE
                                  : lookup->found ? TB_SYMBOL_REFERENCE_READY
                                                  : TB_SYMBOL_REFERENCE_MISSING_TYPE;
        return lookup->status;
    }
    TbSymbolUnitsView_t units = lookup->scopes[lookup->scope_index];
    if ((units.count && !units.entries) ||
        (units.kind != TB_SYMBOL_DWARF_INFO && units.kind != TB_SYMBOL_DWARF_TYPES) ||
        !same_backing(lookup->reference.backing, units.backing)) {
        lookup->status = TB_SYMBOL_REFERENCE_INVALID;
        return lookup->status;
    }
    if (lookup->unit_index == units.count) {
        ++lookup->scope_index;
        lookup->unit_index = 0;
        return lookup->status;
    }
    const TbSymbolUnitEntry_t *unit = &units.entries[lookup->unit_index];
    if (!unit->header_available) {
        lookup->coverage = true;
    } else if (unit->header.kind == TB_SYMBOL_DWARF_TYPE && unit->header.has_signature &&
               unit->header.signature == lookup->reference.value) {
        type_candidate(lookup, units, unit);
    }
    ++lookup->unit_index;
    return lookup->status;
}


bool tb_symbol_type_reference_need(const TbSymbolTypeReference_t *lookup, TbSymbolReferenceNeed_t *need)
{
    if (!lookup || !lookup->initialized || !need || lookup->status != TB_SYMBOL_REFERENCE_READY)
        return false;
    *need = lookup->candidate;
    return true;
}
