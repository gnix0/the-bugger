#include "symbol/source_prepare.h"

static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.key.device == right.key.device && left.key.inode == right.key.inode &&
           left.bytes == right.bytes;
}


static bool same_unit(TbSymbolAttributeResult_t left, TbSymbolAttributeResult_t right)
{
    return same_backing(left.backing, right.backing) && left.section_index == right.section_index &&
           left.unit_offset == right.unit_offset;
}


TbSymbolSourcePrepareStatus_e tb_symbol_source_coordinates(TbSymbolConstantView_t file,
    TbSymbolConstantView_t line, const TbSymbolConstantView_t *column, TbSymbolSourceCoordinates_t *view)
{
    bool declaration = file.selection.name == TB_SYMBOL_CONSTANT_DECL_FILE;
    uint64_t line_name = declaration ? TB_SYMBOL_CONSTANT_DECL_LINE : TB_SYMBOL_CONSTANT_CALL_LINE;
    uint64_t column_name = declaration ? TB_SYMBOL_CONSTANT_DECL_COLUMN : TB_SYMBOL_CONSTANT_CALL_COLUMN;
    if (!view || (!declaration && file.selection.name != TB_SYMBOL_CONSTANT_CALL_FILE) ||
        file.selection.matches != 1 || line.selection.matches != 1 || line.selection.name != line_name ||
        (column && (column->selection.matches != 1 || column->selection.name != column_name))) {
        return TB_SYMBOL_SOURCE_PREPARE_INVALID;
    }
    if (!same_unit(file.selection, line.selection) ||
        (column && !same_unit(file.selection, column->selection))) {
        return TB_SYMBOL_SOURCE_PREPARE_AMBIGUOUS;
    }
    if (!file.value || !line.value)
        return TB_SYMBOL_SOURCE_PREPARE_ABSENT;

    *view = (TbSymbolSourceCoordinates_t) {
        .file             = file,
        .line             = line,
        .kind             = declaration ? TB_SYMBOL_SOURCE_FILE_DECLARATION : TB_SYMBOL_SOURCE_FILE_CALL,
        .column_available = column != NULL
    };
    if (column)
        view->column = *column;

    return TB_SYMBOL_SOURCE_PREPARE_READY;
}


static bool same_table(TbSymbolIndexedTable_t left, TbSymbolIndexedTable_t right)
{
    return left.kind == right.kind && left.table_offset == right.table_offset &&
           left.entries_offset == right.entries_offset && left.lists_offset == right.lists_offset &&
           left.count == right.count && left.byte_order == right.byte_order &&
           left.address_bytes == right.address_bytes && left.segment_bytes == right.segment_bytes &&
           left.extent.end_offset == right.extent.end_offset &&
           left.extent.total_bytes == right.extent.total_bytes &&
           left.extent.payload_bytes == right.extent.payload_bytes &&
           left.extent.offset_bytes == right.extent.offset_bytes &&
           left.extent.length_bytes == right.extent.length_bytes;
}


static bool ranges_match(TbSymbolCodeLocationsView_t code, TbSymbolSourceRanges_t ranges)
{
    TbSymbolRangesRequest_t expected;
    if (tb_symbol_code_locations_ranges(code, ranges.index, ranges.table, &expected) !=
        TB_SYMBOL_CODE_LOCATIONS_COMPLETE) {
        return false;
    }

    TbSymbolRangesRequest_t actual = ranges.ranges.request;
    return actual.offset == expected.offset && actual.end_offset == expected.end_offset &&
           actual.base == expected.base && actual.base_available == expected.base_available &&
           actual.version == expected.version && actual.byte_order == expected.byte_order &&
           actual.address_bytes == expected.address_bytes && actual.segment_bytes == expected.segment_bytes &&
           actual.addresses_available == expected.addresses_available &&
           (!actual.addresses_available || same_table(actual.addresses, expected.addresses)) &&
           ranges.ranges.end_offset == expected.end_offset &&
           (!ranges.ranges.record_count || ranges.ranges.records) &&
           (!ranges.ranges.range_count || ranges.ranges.ranges);
}


TbSymbolSourcePrepareStatus_e tb_symbol_source_prepare_init(TbSymbolSourcePrepare_t *prepare,
    TbSymbolSourcePrepareRequest_t request, TbSymbolSourceLocation_t *arena, size_t capacity)
{
    TbSymbolSourceContextView_t context = request.context;
    TbSymbolSourceFileView_t file = context.file;
    TbSymbolLineView_t program = request.program;
    if (!prepare || (!arena && capacity) || !request.path_capacity || !request.line_limit ||
        !context.count || context.count > 2 || context.selected >= context.count ||
        context.candidates[context.selected].status != TB_SYMBOL_SOURCE_PATH_COMPLETE ||
        !context.candidates[context.selected].path.local_available ||
        (!program.rows && program.row_count) || program.header.table_offset != file.reference.line_offset ||
        program.header.version != file.line_version || file.line_version < 2 || file.line_version > 5 ||
        program.address_bytes != file.reference.unit.address_bytes ||
        (request.ranges && (!request.code || !request.code->list_available))) {
        return TB_SYMBOL_SOURCE_PREPARE_INVALID;
    }
    TbSymbolSourcePathView_t path = context.candidates[context.selected].path;
    if (!path.local || !path.local_bytes)
        return TB_SYMBOL_SOURCE_PREPARE_INVALID;
    if (path.local_bytes >= request.path_capacity || capacity > SIZE_MAX / sizeof(*arena))
        return TB_SYMBOL_SOURCE_PREPARE_RESOURCE;

    uint64_t line;
    uint64_t column;
    if (file.selection.kind == TB_SYMBOL_SOURCE_FILE_ROW) {
        if (request.coordinates || file.selection.index >= program.row_count)
            return TB_SYMBOL_SOURCE_PREPARE_INVALID;

        TbSymbolLineRow_t row = program.rows[file.selection.index];
        if (row.file != file.file_index)
            return TB_SYMBOL_SOURCE_PREPARE_INVALID;
        if (row.end_sequence || !row.line)
            return TB_SYMBOL_SOURCE_PREPARE_ABSENT;

        line = row.line;
        column = row.column;
    } else {
        if (!request.coordinates)
            return TB_SYMBOL_SOURCE_PREPARE_DEPENDENCY;

        TbSymbolSourceCoordinates_t coordinates;
        TbSymbolSourcePrepareStatus_e status = tb_symbol_source_coordinates(request.coordinates->file,
            request.coordinates->line, request.coordinates->column_available ?
            &request.coordinates->column : NULL, &coordinates);
        if (status != TB_SYMBOL_SOURCE_PREPARE_READY)
            return status;
        if (coordinates.kind != file.selection.kind || file.selection.index != file.file_index ||
            coordinates.file.value != file.file_index ||
            !same_unit(coordinates.file.selection, file.reference.source)) {
            return TB_SYMBOL_SOURCE_PREPARE_INVALID;
        }

        line = coordinates.line.value;
        column = coordinates.column_available ? coordinates.column.value : 0;
    }
    if (line > request.line_limit)
        return TB_SYMBOL_SOURCE_PREPARE_UNSUPPORTED;

    if (request.code) {
        TbSymbolCodeLocationsView_t code = *request.code;
        if (!same_backing(code.code.fields[TB_SYMBOL_CODE_LOW].selection.backing,
                          file.reference.source.backing)) {
            return TB_SYMBOL_SOURCE_PREPARE_INVALID;
        }
        if (code.list_available) {
            if (!request.ranges)
                return TB_SYMBOL_SOURCE_PREPARE_DEPENDENCY;
            if (!ranges_match(code, *request.ranges))
                return TB_SYMBOL_SOURCE_PREPARE_INVALID;
        }
    }

    *prepare = (TbSymbolSourcePrepare_t) {
        .view = {
            .request   = request,
            .locations = arena,
            .line      = line,
            .column    = column
        },
        .arena       = arena,
        .capacity    = capacity,
        .status      = TB_SYMBOL_SOURCE_PREPARE_PENDING,
        .initialized = true
    };

    return prepare->status;
}


static void append(TbSymbolSourcePrepare_t *prepare, size_t range)
{
    if (prepare->view.count == prepare->capacity) {
        prepare->status = TB_SYMBOL_SOURCE_PREPARE_RESOURCE;
        return;
    }

    prepare->arena[prepare->view.count++] = (TbSymbolSourceLocation_t) {
        .position = prepare->view.request.program.rows[prepare->cursor],
        .row      = prepare->cursor,
        .range    = range
    };
    ++prepare->cursor;
    prepare->row_pending = false;
}


TbSymbolSourcePrepareStatus_e tb_symbol_source_prepare_step(TbSymbolSourcePrepare_t *prepare,
    size_t max_steps)
{
    if (!prepare || !prepare->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_SOURCE_PREPARE_INVALID;
    if (prepare->status != TB_SYMBOL_SOURCE_PREPARE_PENDING)
        return prepare->status;

    const TbSymbolSourcePrepareRequest_t *request = &prepare->view.request;
    TbSymbolSourceFileView_t file = request->context.file;
    for (size_t step = 0; step < max_steps && prepare->status == TB_SYMBOL_SOURCE_PREPARE_PENDING; ++step) {
        if (prepare->progress == UINT64_MAX) {
            prepare->status = TB_SYMBOL_SOURCE_PREPARE_RESOURCE;
            break;
        }
        ++prepare->progress;
        if (!request->code || prepare->cursor == request->program.row_count) {
            prepare->status = TB_SYMBOL_SOURCE_PREPARE_READY;
            break;
        }

        TbSymbolLineRow_t row = request->program.rows[prepare->cursor];
        if (!prepare->row_pending) {
            if (row.end_sequence || !row.line || row.file != file.file_index ||
                row.line != prepare->view.line ||
                (file.selection.kind == TB_SYMBOL_SOURCE_FILE_ROW &&
                 file.selection.index != prepare->cursor) ||
                (file.dynamic && row.instruction_offset <= file.event.instruction_offset)) {
                ++prepare->cursor;
                continue;
            }
            prepare->row_pending = true;
            prepare->range_cursor = 0;
        } else if (!request->code->list_available) {
            if (request->code->interval_available && row.address >= request->code->low &&
                row.address < request->code->end) {
                append(prepare, SIZE_MAX);
            } else {
                ++prepare->cursor;
                prepare->row_pending = false;
            }
        } else if (prepare->range_cursor == request->ranges->ranges.range_count) {
            ++prepare->cursor;
            prepare->row_pending = false;
        } else {
            size_t index = prepare->range_cursor++;
            TbSymbolRange_t range = request->ranges->ranges.ranges[index];
            if (row.address >= range.begin && row.address < range.end)
                append(prepare, index);
        }
    }

    return prepare->status;
}


bool tb_symbol_source_prepare_view(const TbSymbolSourcePrepare_t *prepare, TbSymbolSourcePrepareView_t *view)
{
    if (!prepare || !prepare->initialized || !view || prepare->status != TB_SYMBOL_SOURCE_PREPARE_READY)
        return false;

    *view = prepare->view;

    return true;
}


uint64_t tb_symbol_source_prepare_progress(const TbSymbolSourcePrepare_t *prepare)
{
    return prepare && prepare->initialized ? prepare->progress : 0;
}


void tb_symbol_source_prepare_discard(TbSymbolSourcePrepare_t *prepare)
{
    if (prepare)
        *prepare = (TbSymbolSourcePrepare_t) {0};
}
