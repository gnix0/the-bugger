#include "symbol/code_locations.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static TbSymbolCodeLocationsRequest_t selection;
static TbSymbolTableBindingView_t address_binding;
static TbSymbolTableValueView_t low_value;
static TbSymbolTableValueView_t high_value;
static TbSymbolTableValueView_t root_value;
static TbSymbolTableValueView_t list_value;

static void fixed(unsigned char *bytes, unsigned width, uint64_t value, TbSymbolByteOrder_e order)
{
    for (unsigned i = 0; i < width; ++i) {
        unsigned at = order == TB_SYMBOL_LITTLE_ENDIAN ? i : width - 1 - i;
        bytes[at] = (unsigned char)(value >> (8U * i));
    }
}


static TbSymbolIndexedTable_t table(TbSymbolIndexedTableKind_e kind, unsigned width, unsigned format,
    TbSymbolByteOrder_e order)
{
    unsigned char bytes[64] = {0};
    unsigned length = format == 4 ? 4 : 12;
    unsigned header = length + (kind == TB_SYMBOL_TABLE_RANGES ? 8 : 4);
    unsigned size = header + (kind == TB_SYMBOL_TABLE_RANGES ? format + 4 : 4 * width);
    fixed(bytes, 4, format == 4 ? size - length : UINT32_MAX, order);
    if (format == 8)
        fixed(bytes + 4, 8, size - length, order);
    fixed(bytes + length, 2, 5, order);
    bytes[length + 2] = (unsigned char)width;
    if (kind == TB_SYMBOL_TABLE_RANGES)
        fixed(bytes + length + 4, 4, 1, order);
    TbSymbolIndexedTable_t result;
    CHECK(tb_symbol_indexed_table_header(kind, (TbSymbolDwarfSpan_t) {
        .section_bytes = 64 + size,
        .unit_offset   = 64,
        .byte_order    = order
    }, bytes, header, &result) == TB_SYMBOL_DWARF_OK);
    return result;
}


static TbSymbolTableValueView_t value(TbSymbolTableBindingView_t binding, uint64_t index, uint64_t number)
{
    unsigned char bytes[8];
    unsigned width = binding.table.kind == TB_SYMBOL_TABLE_ADDRESSES ? binding.table.address_bytes :
                                                                       binding.table.extent.offset_bytes;
    fixed(bytes, width, number, binding.table.byte_order);
    TbSymbolTableValueView_t result = {
        .binding = binding
    };
    CHECK(tb_symbol_indexed_table_entry(binding.table, index, bytes, width, &result.entry) ==
          TB_SYMBOL_DWARF_OK);
    return result;
}


static void fixture(unsigned width, unsigned format, TbSymbolByteOrder_e order)
{
    selection = (TbSymbolCodeLocationsRequest_t) {
        .byte_order = order,
        .root = {
            .unit = {
                .extent = {
                    .end_offset   = 300,
                    .offset_bytes = format
                },
                .version       = 5,
                .address_bytes = (uint8_t)width,
                .header_bytes  = 10
            }
        }
    };
    for (unsigned i = 0; i < TB_SYMBOL_CODE_FIELDS; ++i) {
        selection.root.fields[i].selection = (TbSymbolAttributeResult_t) {
            .backing = {
                .key = {
                    .device = 1,
                    .inode  = 2
                },
                .bytes = 4096
            },
            .section_index = 3,
            .unit_offset   = 100,
            .record_offset = 110
        };
        selection.root.fields[i].attribute.offset = 111 + i;
    }
    selection.code = selection.root;
    selection.code.die_index = 1;
    for (unsigned i = 0; i < TB_SYMBOL_CODE_FIELDS; ++i)
        selection.code.fields[i].selection.record_offset = 160;
    selection.code.fields[TB_SYMBOL_CODE_LOW].kind = TB_SYMBOL_CODE_ADDRESS_INDEX;
    selection.code.fields[TB_SYMBOL_CODE_HIGH].kind = TB_SYMBOL_CODE_ADDRESS_INDEX;
    selection.code.fields[TB_SYMBOL_CODE_HIGH].value = 1;
    selection.root.fields[TB_SYMBOL_CODE_LOW].kind = TB_SYMBOL_CODE_ADDRESS_INDEX;
    selection.root.fields[TB_SYMBOL_CODE_LOW].value = 2;
    address_binding = (TbSymbolTableBindingView_t) {
        .unit          = selection.root.unit,
        .table         = table(TB_SYMBOL_TABLE_ADDRESSES, width, format, order),
        .section_index = 7
    };
    selection.root.fields[TB_SYMBOL_CODE_ADDR_BASE].kind = TB_SYMBOL_CODE_TABLE_BASE;
    selection.root.fields[TB_SYMBOL_CODE_ADDR_BASE].value = address_binding.table.entries_offset;
    address_binding.base = selection.root.fields[TB_SYMBOL_CODE_ADDR_BASE];
    low_value  = value(address_binding, 0, 10);
    high_value = value(address_binding, 1, 30);
    root_value = value(address_binding, 2, 60);
    selection.addresses = &address_binding;
    selection.low       = &low_value;
    selection.high      = &high_value;
    selection.root_low  = &root_value;
}


static void indexed_and_direct(void)
{
    for (unsigned width = 1; width <= 8; width *= 2) {
        for (unsigned format = 4; format <= 8; format *= 2) {
            for (unsigned order = 0; order < 2; ++order) {
                fixture(width, format, order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN);
                TbSymbolCodeLocationsView_t view;
                CHECK(tb_symbol_code_locations_prepare(selection, &view) ==
                      TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
                CHECK(view.interval_available && view.low == 10 && view.end == 30 && view.base == 60);
                CHECK(view.dependency_available[0] && view.dependency_available[1] &&
                      view.dependency_available[2]);
                selection.high = NULL;
                selection.code.fields[TB_SYMBOL_CODE_HIGH].kind = TB_SYMBOL_CODE_LENGTH;
                selection.code.fields[TB_SYMBOL_CODE_HIGH].value = 20;
                CHECK(tb_symbol_code_locations_prepare(selection, &view) ==
                      TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
                CHECK(view.end == 30 && !view.dependency_available[1]);
                selection.code.fields[TB_SYMBOL_CODE_HIGH].value = 0;
                CHECK(tb_symbol_code_locations_prepare(selection, &view) ==
                      TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
                CHECK(view.interval_available && view.low == view.end);
                selection.code.fields[TB_SYMBOL_CODE_HIGH].kind = TB_SYMBOL_CODE_MISSING;
                CHECK(tb_symbol_code_locations_prepare(selection, &view) ==
                      TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
                CHECK(view.low_available && !view.interval_available);
            }
        }
    }
}


static void exact_dependencies(void)
{
    fixture(8, 4, TB_SYMBOL_LITTLE_ENDIAN);
    TbSymbolCodeLocationsView_t view = {0};
    TbSymbolCodeLocationsView_t saved = view;
    selection.low = NULL;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_DEPENDENCY);
    CHECK(!memcmp(&view, &saved, sizeof(view)));
    selection.low = &low_value;
    ++low_value.binding.base.selection.backing.key.inode;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_INVALID);
    --low_value.binding.base.selection.backing.key.inode;
    ++low_value.binding.section_index;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_INVALID);
    --low_value.binding.section_index;
    ++low_value.binding.contribution_index;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_INVALID);
    --low_value.binding.contribution_index;
    ++low_value.entry.offset;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_INVALID);
    --low_value.entry.offset;
    ++selection.root.fields[TB_SYMBOL_CODE_LOW].selection.unit_offset;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_INVALID);
    --selection.root.fields[TB_SYMBOL_CODE_LOW].selection.unit_offset;
    ++selection.root.die_index;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_INVALID);
    --selection.root.die_index;
    CHECK(!memcmp(&view, &saved, sizeof(view)));

    fixture(1, 4, TB_SYMBOL_LITTLE_ENDIAN);
    selection.code.fields[TB_SYMBOL_CODE_HIGH].kind = TB_SYMBOL_CODE_LENGTH;
    selection.code.fields[TB_SYMBOL_CODE_HIGH].value = 246;
    selection.high = NULL;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_MALFORMED);
    selection.code.fields[TB_SYMBOL_CODE_HIGH].value = 245;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_COMPLETE &&
          view.end == 255);
    high_value = value(address_binding, 1, 9);
    selection.code.fields[TB_SYMBOL_CODE_HIGH].kind = TB_SYMBOL_CODE_ADDRESS_INDEX;
    selection.high = &high_value;
    selection.code.fields[TB_SYMBOL_CODE_HIGH].value = 1;
    CHECK(tb_symbol_code_locations_prepare(selection, &view) == TB_SYMBOL_CODE_LOCATIONS_MALFORMED);
}


static void lists(void)
{
    for (unsigned format = 4; format <= 8; format *= 2) {
        fixture(4, format, TB_SYMBOL_BIG_ENDIAN);
        selection.high = NULL;
        selection.code.fields[TB_SYMBOL_CODE_HIGH].kind = TB_SYMBOL_CODE_MISSING;
        TbSymbolTableBindingView_t binding = {
            .unit          = selection.root.unit,
            .table         = table(TB_SYMBOL_TABLE_RANGES, 4, format, TB_SYMBOL_BIG_ENDIAN),
            .section_index = 8
        };
        selection.root.fields[TB_SYMBOL_CODE_RNGLISTS_BASE].kind = TB_SYMBOL_CODE_TABLE_BASE;
        selection.root.fields[TB_SYMBOL_CODE_RNGLISTS_BASE].value = binding.table.entries_offset;
        binding.base = selection.root.fields[TB_SYMBOL_CODE_RNGLISTS_BASE];
        list_value = value(binding, 0, format);
        selection.range = &list_value;
        selection.code.fields[TB_SYMBOL_CODE_RANGES].kind = TB_SYMBOL_CODE_LIST_INDEX;
        TbSymbolCodeLocationsView_t locations;
        CHECK(tb_symbol_code_locations_prepare(selection, &locations) == TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
        CHECK(locations.list_offset == binding.table.lists_offset);
        unsigned char bytes[] = {4, 1, 2, 0};
        TbSymbolRangeBoundary_t boundaries[2];
        TbSymbolRangeIndex_t index;
        CHECK(tb_symbol_range_index_init(&index, (TbSymbolRangeIndexRequest_t) {
            .offset        = binding.table.lists_offset,
            .end_offset    = binding.table.extent.end_offset,
            .byte_order    = TB_SYMBOL_BIG_ENDIAN,
            .version       = 5,
            .address_bytes = 4
        }, boundaries, 2) == TB_SYMBOL_RANGE_INDEX_PENDING);
        size_t consumed;
        CHECK(tb_symbol_range_index_feed(&index, bytes, sizeof(bytes), &consumed) ==
              TB_SYMBOL_RANGE_INDEX_COMPLETE);
        CHECK(consumed == sizeof(bytes));
        TbSymbolRangeIndexView_t inventory;
        CHECK(tb_symbol_range_index_view(&index, &inventory));
        TbSymbolRangesRequest_t request;
        CHECK(tb_symbol_code_locations_ranges(locations, inventory, &binding.table, &request) ==
              TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
        CHECK(request.offset == locations.list_offset &&
              request.end_offset == binding.table.extent.end_offset &&
              request.base_available && request.base == 60 && request.addresses_available);
        TbSymbolRangeRecord_t records[2];
        TbSymbolRange_t intervals[1];
        TbSymbolRanges_t decoder;
        CHECK(tb_symbol_ranges_init(&decoder, request, records, 2, intervals, 1) == TB_SYMBOL_RANGES_PENDING);
        CHECK(tb_symbol_ranges_feed(&decoder, bytes, sizeof(bytes), &consumed) == TB_SYMBOL_RANGES_COMPLETE);
        TbSymbolRangesView_t view;
        CHECK(tb_symbol_ranges_view(&decoder, &view) && view.range_count == 1);
        CHECK(view.ranges[0].begin == 61 && view.ranges[0].end == 62);

        ++locations.list_offset;
        CHECK(tb_symbol_code_locations_ranges(locations, inventory, &binding.table, &request) ==
              TB_SYMBOL_CODE_LOCATIONS_MALFORMED);
        --locations.list_offset;
        ++binding.table.table_offset;
        CHECK(tb_symbol_code_locations_ranges(locations, inventory, &binding.table, &request) ==
              TB_SYMBOL_CODE_LOCATIONS_INVALID);
        CHECK(tb_symbol_code_locations_ranges(locations, inventory, NULL, &request) ==
              TB_SYMBOL_CODE_LOCATIONS_DEPENDENCY);
        ++list_value.binding.base.attribute.offset;
        CHECK(tb_symbol_code_locations_prepare(selection, &locations) == TB_SYMBOL_CODE_LOCATIONS_INVALID);
    }
}


static void legacy_and_absence(void)
{
    fixture(1, 4, TB_SYMBOL_LITTLE_ENDIAN);
    selection.code.unit.version = 4;
    selection.root.unit.version = 4;
    selection.code.fields[TB_SYMBOL_CODE_LOW].kind = TB_SYMBOL_CODE_MISSING;
    selection.code.fields[TB_SYMBOL_CODE_HIGH].kind = TB_SYMBOL_CODE_MISSING;
    selection.code.fields[TB_SYMBOL_CODE_RANGES].kind = TB_SYMBOL_CODE_LIST_OFFSET;
    selection.code.fields[TB_SYMBOL_CODE_RANGES].value = 71;
    selection.root.fields[TB_SYMBOL_CODE_LOW].kind = TB_SYMBOL_CODE_ADDRESS;
    selection.root.fields[TB_SYMBOL_CODE_LOW].value = 0;
    selection.addresses = NULL;
    selection.low = NULL;
    selection.high = NULL;
    selection.root_low = NULL;
    TbSymbolCodeLocationsView_t locations;
    CHECK(tb_symbol_code_locations_prepare(selection, &locations) == TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
    CHECK(!locations.low_available && !locations.interval_available && locations.base_available &&
          locations.base == 0 && locations.list_available);
    const unsigned char bytes[] = {1, 2, 0, 0};
    TbSymbolRangeBoundary_t boundaries[2];
    TbSymbolRangeIndex_t index;
    CHECK(tb_symbol_range_index_init(&index, (TbSymbolRangeIndexRequest_t) {
        .offset        = 71,
        .end_offset    = 75,
        .byte_order    = TB_SYMBOL_LITTLE_ENDIAN,
        .version       = 4,
        .address_bytes = 1
    }, boundaries, 2) == TB_SYMBOL_RANGE_INDEX_PENDING);
    size_t consumed;
    CHECK(tb_symbol_range_index_feed(&index, bytes, sizeof(bytes), &consumed) ==
          TB_SYMBOL_RANGE_INDEX_COMPLETE);
    TbSymbolRangeIndexView_t inventory;
    CHECK(tb_symbol_range_index_view(&index, &inventory));
    TbSymbolRangesRequest_t request;
    CHECK(tb_symbol_code_locations_ranges(locations, inventory, NULL, &request) ==
          TB_SYMBOL_CODE_LOCATIONS_COMPLETE && request.base_available && !request.addresses_available);
    selection.root.fields[TB_SYMBOL_CODE_LOW].kind = TB_SYMBOL_CODE_MISSING;
    CHECK(tb_symbol_code_locations_prepare(selection, &locations) == TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
    CHECK(!locations.base_available);
    CHECK(tb_symbol_code_locations_ranges(locations, inventory, NULL, &request) ==
          TB_SYMBOL_CODE_LOCATIONS_COMPLETE && !request.base_available);
    TbSymbolRangeRecord_t records[2];
    TbSymbolRange_t intervals[1];
    TbSymbolRanges_t decoder;
    CHECK(tb_symbol_ranges_init(&decoder, request, records, 2, intervals, 1) == TB_SYMBOL_RANGES_PENDING);
    CHECK(tb_symbol_ranges_feed(&decoder, bytes, sizeof(bytes), &consumed) == TB_SYMBOL_RANGES_DEPENDENCY);

    /* An independently selected end entry is a valid empty suffix. */
    selection.code.fields[TB_SYMBOL_CODE_RANGES].value = 73;
    CHECK(tb_symbol_code_locations_prepare(selection, &locations) == TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
    CHECK(tb_symbol_code_locations_ranges(locations, inventory, NULL, &request) ==
          TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
    CHECK(tb_symbol_ranges_init(&decoder, request, records, 2, intervals, 1) == TB_SYMBOL_RANGES_PENDING);
    CHECK(tb_symbol_ranges_feed(&decoder, bytes + 2, 2, &consumed) == TB_SYMBOL_RANGES_COMPLETE);
    TbSymbolRangesView_t view;
    CHECK(tb_symbol_ranges_view(&decoder, &view) && !view.range_count);

    fixture(8, 4, TB_SYMBOL_LITTLE_ENDIAN);
    selection.code.fields[TB_SYMBOL_CODE_LOW].value = 4;
    selection.low = NULL;
    CHECK(tb_symbol_code_locations_prepare(selection, &locations) == TB_SYMBOL_CODE_LOCATIONS_MALFORMED);
    fixture(8, 4, TB_SYMBOL_LITTLE_ENDIAN);
    selection.code.fields[TB_SYMBOL_CODE_LOW].kind = TB_SYMBOL_CODE_ADDRESS;
    selection.code.fields[TB_SYMBOL_CODE_LOW].value = 10;
    selection.low = NULL;
    CHECK(tb_symbol_code_locations_prepare(selection, &locations) == TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
}


int main(void)
{
    indexed_and_direct();
    exact_dependencies();
    lists();
    legacy_and_absence();
    puts("PASS exact code/root bindings, indexed/direct PCs, boundaries, overflow and range composition");
    return 0;
}
