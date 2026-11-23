#include "symbol/code_locations.h"

static bool same_backing(TbSymbolStorageInfo_t first, TbSymbolStorageInfo_t second)
{
    return first.key.device == second.key.device && first.key.inode == second.key.inode &&
           first.bytes == second.bytes;
}


static bool same_extent(TbSymbolDwarfLength_t first, TbSymbolDwarfLength_t second)
{
    return first.end_offset == second.end_offset && first.total_bytes == second.total_bytes &&
           first.payload_bytes == second.payload_bytes && first.offset_bytes == second.offset_bytes &&
           first.length_bytes == second.length_bytes;
}


static bool same_unit(TbSymbolDwarfUnit_t first, TbSymbolDwarfUnit_t second)
{
    return same_extent(first.extent, second.extent) && first.kind == second.kind &&
           first.version == second.version && first.unit_type == second.unit_type &&
           first.address_bytes == second.address_bytes && first.header_bytes == second.header_bytes &&
           first.abbrev_offset == second.abbrev_offset && first.signature == second.signature &&
           first.type_offset == second.type_offset && first.has_signature == second.has_signature;
}


static bool same_table(TbSymbolIndexedTable_t first, TbSymbolIndexedTable_t second)
{
    return first.kind == second.kind && first.table_offset == second.table_offset &&
           first.entries_offset == second.entries_offset && first.lists_offset == second.lists_offset &&
           first.count == second.count && first.byte_order == second.byte_order &&
           first.address_bytes == second.address_bytes && first.segment_bytes == second.segment_bytes &&
           same_extent(first.extent, second.extent);
}


static bool binding_matches(TbSymbolTableBindingView_t binding, TbSymbolCodeView_t root,
    TbSymbolIndexedTableKind_e kind, TbSymbolByteOrder_e byte_order)
{
    unsigned field = kind == TB_SYMBOL_TABLE_ADDRESSES ? TB_SYMBOL_CODE_ADDR_BASE :
                                                       TB_SYMBOL_CODE_RNGLISTS_BASE;
    TbSymbolCodeFact_t base = root.fields[field];

    return root.unit.version == 5 && binding.table.kind == kind &&
           binding.table.address_bytes == root.unit.address_bytes &&
           binding.table.byte_order == byte_order &&
           same_unit(binding.unit, root.unit) &&
           base.kind == TB_SYMBOL_CODE_TABLE_BASE && binding.base.kind == base.kind &&
           binding.base.value == base.value && binding.table.entries_offset == base.value &&
           same_backing(binding.base.selection.backing, base.selection.backing) &&
           binding.base.selection.section_index == base.selection.section_index &&
           binding.base.selection.unit_offset == base.selection.unit_offset &&
           binding.base.selection.record_offset == base.selection.record_offset &&
           binding.base.attribute.offset == base.attribute.offset;
}


static TbSymbolCodeLocationsStatus_e resolve(TbSymbolCodeFact_t fact, TbSymbolCodeView_t root,
    TbSymbolByteOrder_e byte_order, const TbSymbolTableBindingView_t *addresses,
    const TbSymbolTableValueView_t *value, uint64_t *resolved)
{
    if (fact.kind == TB_SYMBOL_CODE_ADDRESS) {
        if (value)
            return TB_SYMBOL_CODE_LOCATIONS_INVALID;
        *resolved = fact.value;
        return TB_SYMBOL_CODE_LOCATIONS_COMPLETE;
    }

    if (fact.kind != TB_SYMBOL_CODE_ADDRESS_INDEX)
        return TB_SYMBOL_CODE_LOCATIONS_INVALID;
    if (!addresses)
        return TB_SYMBOL_CODE_LOCATIONS_DEPENDENCY;
    if (addresses->table.segment_bytes)
        return TB_SYMBOL_CODE_LOCATIONS_UNSUPPORTED;
    if (fact.value >= addresses->table.count)
        return TB_SYMBOL_CODE_LOCATIONS_MALFORMED;
    if (!value)
        return TB_SYMBOL_CODE_LOCATIONS_DEPENDENCY;
    if (!binding_matches(value->binding, root, TB_SYMBOL_TABLE_ADDRESSES, byte_order) ||
        addresses->section_index != value->binding.section_index ||
        addresses->contribution_index != value->binding.contribution_index ||
        !same_table(addresses->table, value->binding.table) ||
        !same_table(value->entry.table, value->binding.table)) {
        return TB_SYMBOL_CODE_LOCATIONS_INVALID;
    }

    uint64_t offset;
    if (value->entry.index != fact.value ||
        !tb_symbol_indexed_table_offset(&value->entry.table, fact.value, &offset) ||
        value->entry.offset != offset || value->entry.target_offset != UINT64_MAX) {
        return TB_SYMBOL_CODE_LOCATIONS_INVALID;
    }
    *resolved = value->entry.value;

    return TB_SYMBOL_CODE_LOCATIONS_COMPLETE;
}


static bool root_matches(TbSymbolCodeView_t code, TbSymbolCodeView_t root)
{
    TbSymbolAttributeResult_t first  = code.fields[TB_SYMBOL_CODE_LOW].selection;
    TbSymbolAttributeResult_t second = root.fields[TB_SYMBOL_CODE_LOW].selection;
    uint64_t offset;

    return !root.die_index && same_backing(first.backing, second.backing) &&
           first.section_index == second.section_index && first.unit_offset == second.unit_offset &&
           same_unit(code.unit, root.unit) &&
           code.abbrev_section == root.abbrev_section && code.abbrev_offset == root.abbrev_offset &&
           tb_symbol_add(second.unit_offset, root.unit.header_bytes, &offset) &&
           second.record_offset == offset;
}


TbSymbolCodeLocationsStatus_e tb_symbol_code_locations_prepare(TbSymbolCodeLocationsRequest_t request,
    TbSymbolCodeLocationsView_t *view)
{
    if (!view || !root_matches(request.code, request.root) ||
        (request.byte_order != TB_SYMBOL_LITTLE_ENDIAN && request.byte_order != TB_SYMBOL_BIG_ENDIAN)) {
        return TB_SYMBOL_CODE_LOCATIONS_INVALID;
    }

    unsigned width = request.code.unit.address_bytes;
    if ((width != 1 && width != 2 && width != 4 && width != 8) ||
        (request.code.unit.version != 4 && request.code.unit.version != 5)) {
        return TB_SYMBOL_CODE_LOCATIONS_UNSUPPORTED;
    }

    if (request.addresses &&
        !binding_matches(*request.addresses, request.root, TB_SYMBOL_TABLE_ADDRESSES, request.byte_order)) {
        return TB_SYMBOL_CODE_LOCATIONS_INVALID;
    }

    TbSymbolCodeLocationsView_t candidate = {
        .code       = request.code,
        .root       = request.root,
        .byte_order = request.byte_order
    };
    if (request.addresses) {
        candidate.addresses           = *request.addresses;
        candidate.addresses_available = true;
    }

    const TbSymbolTableValueView_t *values[] = {request.low, request.high, request.root_low, request.range};
    TbSymbolCodeFact_t facts[] = {
        request.code.fields[TB_SYMBOL_CODE_LOW],
        request.code.fields[TB_SYMBOL_CODE_HIGH],
        request.root.fields[TB_SYMBOL_CODE_LOW]
    };
    uint64_t resolved[3] = {0};
    uint64_t maximum = width == 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8U)) - 1;
    for (unsigned i = 0; i < 3; ++i) {
        if (facts[i].kind == TB_SYMBOL_CODE_MISSING || (i == 1 && facts[i].kind == TB_SYMBOL_CODE_LENGTH)) {
            if (values[i])
                return TB_SYMBOL_CODE_LOCATIONS_INVALID;
            continue;
        }
        TbSymbolCodeLocationsStatus_e status = resolve(facts[i], request.root, request.byte_order,
                                                       request.addresses, values[i], &resolved[i]);
        if (status != TB_SYMBOL_CODE_LOCATIONS_COMPLETE)
            return status;
        if (resolved[i] > maximum)
            return TB_SYMBOL_CODE_LOCATIONS_MALFORMED;
    }

    candidate.low_available  = facts[0].kind != TB_SYMBOL_CODE_MISSING;
    candidate.base_available = facts[2].kind != TB_SYMBOL_CODE_MISSING;
    candidate.low            = resolved[0];
    candidate.base           = resolved[2];
    if (facts[1].kind != TB_SYMBOL_CODE_MISSING) {
        if (!candidate.low_available)
            return TB_SYMBOL_CODE_LOCATIONS_MALFORMED;
        if (facts[1].kind == TB_SYMBOL_CODE_LENGTH) {
            if (facts[1].value > maximum - candidate.low)
                return TB_SYMBOL_CODE_LOCATIONS_MALFORMED;
            resolved[1] = candidate.low + facts[1].value;
        }
        if (resolved[1] < candidate.low)
            return TB_SYMBOL_CODE_LOCATIONS_MALFORMED;
        candidate.end                = resolved[1];
        candidate.interval_available = true;
    }

    TbSymbolCodeFact_t ranges = request.code.fields[TB_SYMBOL_CODE_RANGES];
    if (ranges.kind == TB_SYMBOL_CODE_LIST_INDEX) {
        if (!request.range)
            return TB_SYMBOL_CODE_LOCATIONS_DEPENDENCY;
        TbSymbolTableValueView_t value = *request.range;
        uint64_t offset;
        uint64_t target;
        if (!binding_matches(value.binding, request.root, TB_SYMBOL_TABLE_RANGES, request.byte_order) ||
            !same_table(value.entry.table, value.binding.table) || value.entry.index != ranges.value ||
            !tb_symbol_indexed_table_offset(&value.entry.table, ranges.value, &offset) ||
            value.entry.offset != offset ||
            !tb_symbol_add(value.binding.table.entries_offset, value.entry.value, &target) ||
            target != value.entry.target_offset || target < value.binding.table.lists_offset ||
            target >= value.binding.table.extent.end_offset) {
            return TB_SYMBOL_CODE_LOCATIONS_INVALID;
        }
        candidate.list_offset = target;
    } else if (ranges.kind == TB_SYMBOL_CODE_LIST_OFFSET) {
        if (request.range)
            return TB_SYMBOL_CODE_LOCATIONS_INVALID;
        candidate.list_offset = ranges.value;
    } else if (ranges.kind != TB_SYMBOL_CODE_MISSING || request.range) {
        return TB_SYMBOL_CODE_LOCATIONS_INVALID;
    }

    candidate.list_available = ranges.kind != TB_SYMBOL_CODE_MISSING;
    if (candidate.list_available && candidate.interval_available)
        return TB_SYMBOL_CODE_LOCATIONS_MALFORMED;
    for (unsigned i = 0; i < 4; ++i) {
        if (values[i]) {
            candidate.dependencies[i]         = *values[i];
            candidate.dependency_available[i]  = true;
        }
    }

    *view = candidate;

    return TB_SYMBOL_CODE_LOCATIONS_COMPLETE;
}


TbSymbolCodeLocationsStatus_e tb_symbol_code_locations_ranges(TbSymbolCodeLocationsView_t locations,
    TbSymbolRangeIndexView_t index, const TbSymbolIndexedTable_t *table, TbSymbolRangesRequest_t *request)
{
    if (!request || !locations.list_available || index.request.version != locations.code.unit.version ||
        index.request.address_bytes != locations.code.unit.address_bytes ||
        index.request.byte_order != locations.byte_order) {
        return TB_SYMBOL_CODE_LOCATIONS_INVALID;
    }
    if (index.request.segment_bytes)
        return TB_SYMBOL_CODE_LOCATIONS_UNSUPPORTED;

    if (locations.code.unit.version == 5) {
        if (!table)
            return TB_SYMBOL_CODE_LOCATIONS_DEPENDENCY;
        if (table->kind != TB_SYMBOL_TABLE_RANGES || table->address_bytes != index.request.address_bytes ||
            table->segment_bytes != index.request.segment_bytes ||
            table->byte_order != index.request.byte_order ||
            table->lists_offset != index.request.offset ||
            table->extent.end_offset != index.request.end_offset ||
            (locations.dependency_available[3] &&
             !same_table(locations.dependencies[3].binding.table, *table))) {
            return TB_SYMBOL_CODE_LOCATIONS_INVALID;
        }
    } else if (table) {
        return TB_SYMBOL_CODE_LOCATIONS_INVALID;
    }

    TbSymbolRangeBoundary_t boundary;
    if (!tb_symbol_range_index_find(index, locations.list_offset, &boundary))
        return TB_SYMBOL_CODE_LOCATIONS_MALFORMED;

    TbSymbolRangesRequest_t candidate = {
        .offset              = boundary.offset,
        .end_offset          = boundary.list_end,
        .base                = locations.base,
        .byte_order          = index.request.byte_order,
        .version             = index.request.version,
        .address_bytes       = index.request.address_bytes,
        .segment_bytes       = index.request.segment_bytes,
        .base_available      = locations.base_available,
        .addresses_available = locations.addresses_available,
        .addresses           = locations.addresses.table
    };
    *request = candidate;

    return TB_SYMBOL_CODE_LOCATIONS_COMPLETE;
}
