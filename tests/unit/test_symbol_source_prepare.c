#include "symbol/source_prepare.h"
#include "../support/check.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static TbSymbolSourcePrepareRequest_t request;
static TbSymbolLineRow_t rows[8];
static TbSymbolCodeLocationsView_t code;
static TbSymbolSourceCoordinates_t coordinates;
static TbSymbolSourceLocation_t output[8];
static TbSymbolSourcePrepare_t prepare;

static TbSymbolConstantView_t constant(TbSymbolConstantField_e field, uint64_t value)
{
    TbSymbolDieEntry_t entry = {
        .offset          = 120,
        .end_offset      = 160,
        .tag             = field >= TB_SYMBOL_CONSTANT_CALL_COLUMN ? 0x1d : 0x34,
        .attribute_count = 1
    };
    TbSymbolDieAttribute_t attribute = {
        .name          = field,
        .offset        = 121,
        .declared_form = TB_SYMBOL_FORM_UDATA,
        .value = {
            .form    = TB_SYMBOL_FORM_UDATA,
            .bits    = value,
            .numeric = true
        }
    };
    TbSymbolDiesView_t tree = {
        .entries         = &entry,
        .attributes      = &attribute,
        .count           = 1,
        .attribute_count = 1,
        .section_index   = 3,
        .unit_offset     = 100,
        .backing = {
            .key = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        }
    };
    TbSymbolConstant_t decoder;
    CHECK(tb_symbol_constant_init(&decoder, tree, 0, field) == TB_SYMBOL_CONSTANT_PENDING);
    CHECK(tb_symbol_constant_step(&decoder, 1) == TB_SYMBOL_CONSTANT_FOUND);
    TbSymbolConstantView_t view;
    CHECK(tb_symbol_constant_view(&decoder, &view));
    return view;
}


static void fixture(void)
{
    TbSymbolConstantView_t file = constant(TB_SYMBOL_CONSTANT_DECL_FILE, 1);
    TbSymbolConstantView_t line = constant(TB_SYMBOL_CONSTANT_DECL_LINE, 7);
    TbSymbolConstantView_t column = constant(TB_SYMBOL_CONSTANT_DECL_COLUMN, 4);
    CHECK(tb_symbol_source_coordinates(file, line, &column, &coordinates) == TB_SYMBOL_SOURCE_PREPARE_READY);
    request = (TbSymbolSourcePrepareRequest_t) {
        .context = {
            .file = {
                .reference = {
                    .source = file.selection,
                    .unit = {
                        .version       = 4,
                        .address_bytes = 1
                    },
                    .line_offset = 32
                },
                .selection = {
                    .kind  = TB_SYMBOL_SOURCE_FILE_DECLARATION,
                    .index = 1
                },
                .file_index   = 1,
                .line_version = 4
            },
            .candidates = {{
                .path = {
                    .recorded        = (const unsigned char *)"/source/shared.h",
                    .local           = (const unsigned char *)"/local/shared.h",
                    .recorded_bytes  = 16,
                    .local_bytes     = 15,
                    .local_available = true
                },
                .status = TB_SYMBOL_SOURCE_PATH_COMPLETE
            }},
            .count    = 1,
            .selected = 0
        },
        .program = {
            .header = {
                .table_offset = 32,
                .version      = 4
            },
            .rows          = rows,
            .row_count     = 8,
            .address_bytes = 1
        },
        .coordinates   = &coordinates,
        .code          = &code,
        .path_capacity = 4096,
        .line_limit    = INT_MAX
    };
    for (size_t i = 0; i < 8; ++i) {
        rows[i] = (TbSymbolLineRow_t) {
            .address            = 10 + i,
            .file               = 1,
            .line               = 7,
            .column             = i,
            .instruction_offset = 60 + i,
            .is_stmt            = true
        };
    }
    rows[0].file = 2;
    rows[1].line = 8;
    rows[3].address = rows[2].address;
    rows[3].operation = 1;
    rows[6].line = 0;
    rows[7].end_sequence = true;
    code = (TbSymbolCodeLocationsView_t) {
        .code = {
            .unit = request.context.file.reference.unit
        },
        .byte_order         = TB_SYMBOL_LITTLE_ENDIAN,
        .low                = 12,
        .end                = 15,
        .interval_available = true
    };
    code.code.fields[TB_SYMBOL_CODE_LOW].selection = file.selection;
}


static TbSymbolSourcePrepareStatus_e finish(void)
{
    TbSymbolSourcePrepareStatus_e status = prepare.status;
    for (size_t i = 0; i < 10000 && status == TB_SYMBOL_SOURCE_PREPARE_PENDING; ++i) {
        TbSymbolSourcePrepareView_t unavailable;
        CHECK(!tb_symbol_source_prepare_view(&prepare, &unavailable));
        uint64_t progress = tb_symbol_source_prepare_progress(&prepare);
        status = tb_symbol_source_prepare_step(&prepare, 1);
        CHECK(tb_symbol_source_prepare_progress(&prepare) == progress + 1);
    }
    return status;
}


static void direct_and_failures(void)
{
    fixture();
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(finish() == TB_SYMBOL_SOURCE_PREPARE_READY);
    TbSymbolSourcePrepareView_t view;
    CHECK(tb_symbol_source_prepare_view(&prepare, &view));
    CHECK(view.count == 3 && view.line == 7 && view.column == 4);
    CHECK(output[0].row == 2 && output[1].row == 3 && output[2].row == 4);
    CHECK(output[0].position.address == output[1].position.address && output[1].position.operation == 1);
    CHECK(output[0].range == SIZE_MAX);

    /* Arena exhaustion cannot expose the candidate prefix or alter a prior copy. */
    TbSymbolSourceLocation_t retained = output[0];
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_PREPARE_RESOURCE);
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 2) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(finish() == TB_SYMBOL_SOURCE_PREPARE_RESOURCE);
    CHECK(!tb_symbol_source_prepare_view(&prepare, &view) && retained.row == 2);
    request.code = NULL;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, NULL, 0) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(finish() == TB_SYMBOL_SOURCE_PREPARE_READY && tb_symbol_source_prepare_view(&prepare, &view));
    CHECK(view.count == 0 && view.line == 7);

    fixture();
    request.context.file.selection.kind = TB_SYMBOL_SOURCE_FILE_ROW;
    request.context.file.selection.index = 3;
    request.coordinates = NULL;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(finish() == TB_SYMBOL_SOURCE_PREPARE_READY && prepare.view.count == 1 && output[0].row == 3);
    request.context.file.selection.index = 7;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_ABSENT);
    request.context.file.selection.index = 6;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_ABSENT);
    fixture();
    request.path_capacity = 15;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_RESOURCE);
    request.path_capacity = 16;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    coordinates.line.value = (uint64_t)INT_MAX + 1;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) ==
          TB_SYMBOL_SOURCE_PREPARE_UNSUPPORTED);
    coordinates.line.value = 7;
    ++coordinates.file.selection.unit_offset;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_AMBIGUOUS);
    --coordinates.file.selection.unit_offset;
    ++request.context.file.reference.source.unit_offset;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_INVALID);
    fixture();
    request.context.selected = SIZE_MAX;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_INVALID);
    fixture();
    ++code.code.fields[TB_SYMBOL_CODE_LOW].selection.backing.key.inode;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_INVALID);
    fixture();
    code.interval_available = false;
    code.low_available = true;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(finish() == TB_SYMBOL_SOURCE_PREPARE_READY && !prepare.view.count);
    fixture();
    code.list_available = true;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_DEPENDENCY);
    fixture();
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(tb_symbol_source_prepare_step(&prepare, 1) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    tb_symbol_source_prepare_discard(&prepare);
    CHECK(!tb_symbol_source_prepare_view(&prepare, &view));
    CHECK(tb_symbol_source_prepare_step(&prepare, 1) == TB_SYMBOL_SOURCE_PREPARE_INVALID);
}


static void disjoint(unsigned version, TbSymbolByteOrder_e order)
{
    fixture();
    code.code.unit.version = (uint16_t)version;
    request.context.file.reference.unit.version = (uint16_t)version;
    code.byte_order = order;
    code.interval_available = false;
    code.list_available = true;
    code.list_offset = 100;
    code.base_available = true;
    const unsigned char legacy[] = {12, 13, 15, 17, 0, 0};
    const unsigned char modern[] = {6, 12, 13, 6, 15, 17, 0};
    const unsigned char *bytes = version == 4 ? legacy : modern;
    size_t length = version == 4 ? sizeof(legacy) : sizeof(modern);
    TbSymbolRangeBoundary_t boundaries[3];
    TbSymbolRangeIndex_t index;
    CHECK(tb_symbol_range_index_init(&index, (TbSymbolRangeIndexRequest_t) {
        .offset        = 100,
        .end_offset    = 100 + length,
        .byte_order    = order,
        .version       = version,
        .address_bytes = 1
    }, boundaries, 3) == TB_SYMBOL_RANGE_INDEX_PENDING);
    TbSymbolRangeIndexStatus_e status = TB_SYMBOL_RANGE_INDEX_PENDING;
    size_t at = 0;
    while (status == TB_SYMBOL_RANGE_INDEX_PENDING) {
        size_t consumed;
        status = tb_symbol_range_index_feed(&index, bytes + at, length - at, &consumed);
        at += consumed;
    }
    TbSymbolSourceRanges_t selection = {0};
    CHECK(status == TB_SYMBOL_RANGE_INDEX_COMPLETE && tb_symbol_range_index_view(&index, &selection.index));
    TbSymbolIndexedTable_t table;
    if (version == 5) {
        unsigned char header[12] = {0};
        header[order == TB_SYMBOL_LITTLE_ENDIAN ? 0 : 3] = 15;
        header[order == TB_SYMBOL_LITTLE_ENDIAN ? 4 : 5] = 5;
        header[6] = 1;
        CHECK(tb_symbol_indexed_table_header(TB_SYMBOL_TABLE_RANGES, (TbSymbolDwarfSpan_t) {
            .section_bytes = 107,
            .unit_offset   = 88,
            .byte_order    = order
        }, header, sizeof(header), &table) == TB_SYMBOL_DWARF_OK);
        selection.table = &table;
    }
    TbSymbolRangesRequest_t range_request;
    CHECK(tb_symbol_code_locations_ranges(code, selection.index, selection.table, &range_request) ==
          TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
    TbSymbolRangeRecord_t records[3];
    TbSymbolRange_t intervals[2];
    TbSymbolRanges_t ranges;
    CHECK(tb_symbol_ranges_init(&ranges, range_request, records, 3, intervals, 2) ==
          TB_SYMBOL_RANGES_PENDING);
    TbSymbolRangesStatus_e semantic = TB_SYMBOL_RANGES_PENDING;
    at = 0;
    while (semantic == TB_SYMBOL_RANGES_PENDING) {
        size_t consumed;
        semantic = tb_symbol_ranges_feed(&ranges, bytes + at, length - at, &consumed);
        at += consumed;
    }
    CHECK(semantic == TB_SYMBOL_RANGES_COMPLETE && tb_symbol_ranges_view(&ranges, &selection.ranges));
    request.ranges = &selection;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(finish() == TB_SYMBOL_SOURCE_PREPARE_READY && prepare.view.count == 3);
    CHECK(output[0].row == 2 && output[1].row == 3 && output[2].row == 5);
    CHECK(output[0].range == 0 && output[2].range == 1);
    ++selection.ranges.request.base;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_INVALID);
    --selection.ranges.request.base;
    ++selection.ranges.end_offset;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, output, 8) == TB_SYMBOL_SOURCE_PREPARE_INVALID);
}


static void bounded_rows(void)
{
    fixture();
    TbSymbolLineRow_t many[520];
    TbSymbolSourceLocation_t locations[520];
    for (size_t i = 0; i < 520; ++i)
        many[i] = rows[2];
    request.program.rows = many;
    request.program.row_count = 520;
    CHECK(tb_symbol_source_prepare_init(&prepare, request, locations, 520) ==
          TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(tb_symbol_source_prepare_step(&prepare, 256) == TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(prepare.progress == 256 && prepare.view.count == 128);
    CHECK(finish() == TB_SYMBOL_SOURCE_PREPARE_READY && prepare.view.count == 520);
    CHECK(locations[519].row == 519);
    CHECK(tb_symbol_source_prepare_init(&prepare, request, locations, 519) ==
          TB_SYMBOL_SOURCE_PREPARE_PENDING);
    CHECK(finish() == TB_SYMBOL_SOURCE_PREPARE_RESOURCE);
    CHECK(tb_symbol_source_prepare_init(&prepare, request, locations, 520) ==
          TB_SYMBOL_SOURCE_PREPARE_PENDING);
    prepare.progress = UINT64_MAX;
    CHECK(tb_symbol_source_prepare_step(&prepare, 1) == TB_SYMBOL_SOURCE_PREPARE_RESOURCE);
}


static void coordinate_contract(void)
{
    TbSymbolConstantView_t file = constant(TB_SYMBOL_CONSTANT_CALL_FILE, 1);
    TbSymbolConstantView_t line = constant(TB_SYMBOL_CONSTANT_CALL_LINE, 7);
    TbSymbolConstantView_t column = constant(TB_SYMBOL_CONSTANT_CALL_COLUMN, 4);
    TbSymbolSourceCoordinates_t view;
    CHECK(tb_symbol_source_coordinates(file, line, &column, &view) == TB_SYMBOL_SOURCE_PREPARE_READY);
    CHECK(view.kind == TB_SYMBOL_SOURCE_FILE_CALL && view.column_available);
    ++line.selection.unit_offset;
    CHECK(tb_symbol_source_coordinates(file, line, NULL, &view) == TB_SYMBOL_SOURCE_PREPARE_AMBIGUOUS);
    --line.selection.unit_offset;
    file.value = 0;
    CHECK(tb_symbol_source_coordinates(file, line, NULL, &view) == TB_SYMBOL_SOURCE_PREPARE_ABSENT);
    file.value = 1;
    line.value = 0;
    CHECK(tb_symbol_source_coordinates(file, line, NULL, &view) == TB_SYMBOL_SOURCE_PREPARE_ABSENT);
    line.value = 7;
    ++line.selection.record_offset;
    CHECK(tb_symbol_source_coordinates(file, line, NULL, &view) == TB_SYMBOL_SOURCE_PREPARE_READY);
    CHECK(view.file.selection.record_offset != view.line.selection.record_offset);
    ++file.selection.matches;
    CHECK(tb_symbol_source_coordinates(file, line, NULL, &view) == TB_SYMBOL_SOURCE_PREPARE_INVALID);
}


int main(void)
{
    direct_and_failures();
    for (unsigned version = 4; version <= 5; ++version) {
        disjoint(version, TB_SYMBOL_LITTLE_ENDIAN);
        disjoint(version, TB_SYMBOL_BIG_ENDIAN);
    }
    bounded_rows();
    coordinate_contract();
    puts("PASS exact Source coordinates, direct/disjoint row sets, limits, ambiguity and cancellation");
    return 0;
}
