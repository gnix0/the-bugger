#include "symbol/line.h"

#include <string.h>

enum {
    LINE_OPCODE,
    LINE_ULEB,
    LINE_SLEB,
    LINE_FIXED,
    LINE_EXTENT,
    LINE_EXTENDED,
    LINE_ADDRESS,
    LINE_DISCRIMINATOR,
    LINE_FILE_NAME,
    LINE_FILE_VALUE,
    LINE_UNKNOWN_ARGUMENT,
    LINE_UNKNOWN_EXTENDED
};

static bool number(TbSymbolReader_t *reader, unsigned width, uint64_t *value)
{
    return tb_symbol_reader_uint(reader, width, value) == TB_SYMBOL_READ_OK;
}


static unsigned standard_limit(unsigned version)
{
    return version == 2 ? 9 : 12;
}


static bool valid_counts(const TbSymbolLineHeader_t *header)
{
    static const unsigned char expected[] = {0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    for (unsigned opcode = 1; opcode < header->opcode_base && opcode <= standard_limit(header->version);
         ++opcode) {
        if (header->operand_counts[opcode] != expected[opcode])
            return false;
    }

    return true;
}


TbSymbolDwarfStatus_e tb_symbol_line_header(TbSymbolDwarfSpan_t span, const void *bytes, size_t length,
                                           TbSymbolLineHeader_t *header)
{
    if (!header)
        return TB_SYMBOL_DWARF_INVALID;

    TbSymbolLineHeader_t candidate = {
        .table_offset       = span.unit_offset,
        .byte_order         = span.byte_order,
        .maximum_operations = 1
    };
    TbSymbolDwarfStatus_e status = tb_symbol_dwarf_length(span, bytes, length, &candidate.extent);
    if (status != TB_SYMBOL_DWARF_OK)
        return status;

    size_t available = length < TB_SYMBOL_LINE_PREFIX_BYTES ? length : TB_SYMBOL_LINE_PREFIX_BYTES;
    if (candidate.extent.total_bytes < available)
        available = (size_t)candidate.extent.total_bytes;
    TbSymbolReader_t reader;
    if (tb_symbol_reader_init(&reader, bytes, available, span.byte_order) != TB_SYMBOL_READ_OK)
        return TB_SYMBOL_DWARF_INVALID;

    const unsigned char *ignored;
    uint64_t value;
    if (tb_symbol_reader_bytes(&reader, candidate.extent.length_bytes, &ignored) != TB_SYMBOL_READ_OK ||
        !number(&reader, 2, &value) || !value) {
        return TB_SYMBOL_DWARF_MALFORMED;
    }
    if (value < 2 || value > 5)
        return TB_SYMBOL_DWARF_UNSUPPORTED;
    candidate.version = (unsigned)value;

    if (candidate.version == 5) {
        if (!number(&reader, 1, &value) || !value)
            return TB_SYMBOL_DWARF_MALFORMED;
        candidate.address_bytes = (unsigned)value;
        if (!number(&reader, 1, &value))
            return TB_SYMBOL_DWARF_MALFORMED;
        candidate.segment_bytes = (unsigned)value;
    }

    uint64_t prologue_bytes;
    uint64_t fixed_end;
    if (!number(&reader, candidate.extent.offset_bytes, &prologue_bytes) ||
        !tb_symbol_add(span.unit_offset, reader.offset, &fixed_end) ||
        !tb_symbol_add(fixed_end, prologue_bytes, &candidate.program_offset) ||
        candidate.program_offset > candidate.extent.end_offset) {
        return TB_SYMBOL_DWARF_MALFORMED;
    }

    uint64_t prefix_bytes = candidate.program_offset - span.unit_offset;
    if (prefix_bytes < reader.length)
        reader.length = (size_t)prefix_bytes;
    if (!number(&reader, 1, &value) || !value)
        return TB_SYMBOL_DWARF_MALFORMED;
    candidate.minimum_instruction = (unsigned)value;
    if (candidate.version >= 4) {
        if (!number(&reader, 1, &value) || !value)
            return TB_SYMBOL_DWARF_MALFORMED;
        candidate.maximum_operations = (unsigned)value;
    }
    if (!number(&reader, 1, &value))
        return TB_SYMBOL_DWARF_MALFORMED;
    candidate.default_is_stmt = value != 0;
    if (!number(&reader, 1, &value))
        return TB_SYMBOL_DWARF_MALFORMED;
    candidate.line_base = value < 128 ? (int)value : (int)value - 256;
    if (!number(&reader, 1, &value) || !value)
        return TB_SYMBOL_DWARF_MALFORMED;
    candidate.line_range = (unsigned)value;
    if (!number(&reader, 1, &value) || !value)
        return TB_SYMBOL_DWARF_MALFORMED;
    candidate.opcode_base = (unsigned)value;

    const unsigned char *counts;
    if (tb_symbol_reader_bytes(&reader, candidate.opcode_base - 1, &counts) != TB_SYMBOL_READ_OK)
        return TB_SYMBOL_DWARF_MALFORMED;
    memcpy(candidate.operand_counts + 1, counts, candidate.opcode_base - 1);
    if (!valid_counts(&candidate) ||
        !tb_symbol_add(span.unit_offset, reader.offset, &candidate.entries_offset)) {
        return TB_SYMBOL_DWARF_MALFORMED;
    }

    *header = candidate;
    return TB_SYMBOL_DWARF_OK;
}


static void reset_registers(TbSymbolLine_t *line, uint64_t sequence)
{
    line->registers = (TbSymbolLineRow_t) {
        .file     = 1,
        .line     = 1,
        .sequence = sequence,
        .is_stmt  = line->view.header.default_is_stmt
    };
}


TbSymbolLineStatus_e tb_symbol_line_init(TbSymbolLine_t *line, TbSymbolLineHeader_t header,
    unsigned address_bytes, TbSymbolLineRow_t *rows, size_t row_capacity,
    TbSymbolLineFile_t *files, size_t file_capacity)
{
    if (!line || !rows || !files || !address_bytes || row_capacity > SIZE_MAX / sizeof(*rows) ||
        file_capacity > SIZE_MAX / sizeof(*files) || header.version < 2 || header.version > 5 ||
        !header.minimum_instruction || header.minimum_instruction > 255 ||
        !header.maximum_operations || header.maximum_operations > 255 ||
        (header.version < 4 && header.maximum_operations != 1) ||
        !header.line_range || header.line_range > 255 || header.line_base < -128 || header.line_base > 127 ||
        !header.opcode_base || header.opcode_base > 255 || !valid_counts(&header) ||
        (header.byte_order != TB_SYMBOL_LITTLE_ENDIAN && header.byte_order != TB_SYMBOL_BIG_ENDIAN) ||
        header.table_offset >= header.entries_offset || header.entries_offset > header.program_offset ||
        header.program_offset > header.extent.end_offset ||
        (header.version == 5 && (!header.address_bytes || header.address_bytes != address_bytes)) ||
        (header.version < 5 && header.address_bytes)) {
        return TB_SYMBOL_LINE_INVALID;
    }

    if (header.segment_bytes ||
        (address_bytes != 1 && address_bytes != 2 && address_bytes != 4 && address_bytes != 8)) {
        return TB_SYMBOL_LINE_UNSUPPORTED;
    }

    *line = (TbSymbolLine_t) {
        .view = {
            .header        = header,
            .rows          = rows,
            .files         = files,
            .address_bytes = address_bytes
        },
        .rows          = rows,
        .files         = files,
        .status        = header.program_offset == header.extent.end_offset ?
                             TB_SYMBOL_LINE_COMPLETE : TB_SYMBOL_LINE_PENDING,
        .cursor        = header.program_offset,
        .row_capacity  = row_capacity,
        .file_capacity = file_capacity,
        .initialized   = true
    };
    reset_registers(line, 0);

    return line->status;
}


static void fail(TbSymbolLine_t *line, TbSymbolLineStatus_e status, TbSymbolLineFailure_e reason)
{
    line->status = status;
    line->failure = (TbSymbolLineFailure_t) {
        .reason             = reason,
        .instruction_offset = line->registers.instruction_offset,
        .byte_offset        = line->cursor,
        .opcode             = line->opcode,
        .extended_opcode    = line->extended_opcode
    };
}


static bool advance_address(TbSymbolLine_t *line, uint64_t bytes)
{
    uint64_t value;
    if (!tb_symbol_add(line->registers.address, bytes, &value) ||
        (line->view.address_bytes < 8 && value >> (8U * line->view.address_bytes))) {
        fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_ADDRESS);
        return false;
    }

    line->registers.address = value;
    return true;
}


static bool advance_operations(TbSymbolLine_t *line, uint64_t operations)
{
    uint64_t instructions;
    uint64_t bytes;
    unsigned maximum   = line->view.header.maximum_operations;
    uint64_t remainder = line->registers.operation + operations % maximum;

    /* Quotient/remainder splitting avoids overflowing a valid VLIW advance
     * merely because its intermediate operation count exceeds 64 bits. */
    if (!tb_symbol_add(operations / maximum, remainder / maximum, &instructions) ||
        !tb_symbol_multiply(instructions, line->view.header.minimum_instruction, &bytes)) {
        fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_ADDRESS);
        return false;
    }
    if (!advance_address(line, bytes))
        return false;

    line->registers.operation = remainder % maximum;
    return true;
}


static bool advance_line(TbSymbolLine_t *line, int64_t delta)
{
    uint64_t magnitude = delta < 0 ? (uint64_t)(-(delta + 1)) + 1 : (uint64_t)delta;
    uint64_t value = line->registers.line;
    if ((delta < 0 && value < magnitude) || (delta >= 0 && !tb_symbol_add(value, magnitude, &value))) {
        fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_NUMBER);
        return false;
    }

    line->registers.line = delta < 0 ? value - magnitude : value;
    return true;
}


static void emit(TbSymbolLine_t *line, bool end)
{
    if (line->view.row_count == line->row_capacity) {
        fail(line, TB_SYMBOL_LINE_RESOURCE, TB_SYMBOL_LINE_ROWS);
        return;
    }

    if (line->view.row_count) {
        const TbSymbolLineRow_t *previous = &line->rows[line->view.row_count - 1];
        if (!previous->end_sequence &&
            (line->registers.address < previous->address ||
             (line->registers.address == previous->address &&
              line->registers.operation < previous->operation))) {
            fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_ORDER);
            return;
        }
    }

    line->registers.end_sequence = end;
    line->rows[line->view.row_count++] = line->registers;
    if (end) {
        reset_registers(line, line->registers.sequence + 1);
        line->sequence_open = false;
    } else {
        line->registers.basic_block    = false;
        line->registers.prologue_end   = false;
        line->registers.epilogue_begin = false;
        line->registers.discriminator  = 0;
    }
}


static void begin_leb(TbSymbolLine_t *line, unsigned phase, bool is_signed)
{
    (void)tb_symbol_leb_init(&line->leb, is_signed);
    line->phase = phase;
}


static void opcode(TbSymbolLine_t *line, unsigned byte)
{
    line->registers.instruction_offset = line->cursor - 1;
    line->sequence_open                = true;
    line->opcode                       = byte;
    line->extended_opcode              = 0;
    if (!byte) {
        begin_leb(line, LINE_EXTENT, false);
    } else if (byte >= line->view.header.opcode_base) {
        unsigned adjusted = byte - line->view.header.opcode_base;
        int delta = line->view.header.line_base + (int)(adjusted % line->view.header.line_range);
        if (advance_operations(line, adjusted / line->view.header.line_range) && advance_line(line, delta))
            emit(line, false);
    } else if (byte > standard_limit(line->view.header.version)) {
        line->remaining = line->view.header.operand_counts[byte];
        if (line->remaining)
            begin_leb(line, LINE_UNKNOWN_ARGUMENT, false);
        else
            fail(line, TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_EXTENSION);
    } else {
        switch (byte) {
        case 1:
            emit(line, false);
            break;
        case 2:
        case 4:
        case 5:
        case 12:
            begin_leb(line, LINE_ULEB, false);
            break;
        case 3:
            begin_leb(line, LINE_SLEB, true);
            break;
        case 6:
            line->registers.is_stmt = !line->registers.is_stmt;
            break;
        case 7:
            line->registers.basic_block = true;
            break;
        case 8:
            (void)advance_operations(line,
                (255 - line->view.header.opcode_base) / line->view.header.line_range);
            break;
        case 9:
            line->width       = 2;
            line->received    = 0;
            line->accumulator = 0;
            line->phase       = LINE_FIXED;
            break;
        case 10:
            line->registers.prologue_end = true;
            break;
        case 11:
            line->registers.epilogue_begin = true;
            break;
        default:
            fail(line, TB_SYMBOL_LINE_INVALID, TB_SYMBOL_LINE_OPERAND);
            break;
        }
    }
}


static void extended(TbSymbolLine_t *line, unsigned byte)
{
    line->extended_opcode = byte;
    if (byte == 1) {
        if (line->cursor != line->extended_end)
            fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);
        else
            emit(line, true);
        line->phase = LINE_OPCODE;
    } else if (byte == 2) {
        if (line->extended_end - line->cursor != line->view.address_bytes) {
            fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);
        } else {
            line->width       = line->view.address_bytes;
            line->received    = 0;
            line->accumulator = 0;
            line->phase       = LINE_ADDRESS;
        }
    } else if (byte == 3 && line->view.header.version < 5) {
        if (line->view.file_count == line->file_capacity) {
            fail(line, TB_SYMBOL_LINE_RESOURCE, TB_SYMBOL_LINE_FILES);
        } else {
            line->file = (TbSymbolLineFile_t) {
                .instruction_offset = line->registers.instruction_offset,
                .name_offset        = line->cursor
            };
            line->argument = 0;
            line->phase    = LINE_FILE_NAME;
        }
    } else if (byte == 4 && line->view.header.version >= 4) {
        begin_leb(line, LINE_DISCRIMINATOR, false);
    } else {
        line->phase = LINE_UNKNOWN_EXTENDED;
        if (line->cursor == line->extended_end)
            fail(line, TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_EXTENSION);
    }
}


static void leb_value(TbSymbolLine_t *line, uint64_t value)
{
    unsigned phase = line->phase;
    line->phase = LINE_OPCODE;
    if (phase == LINE_EXTENT) {
        if (!value || !tb_symbol_add(line->cursor, value, &line->extended_end) ||
            line->extended_end > line->view.header.extent.end_offset) {
            fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);
        } else {
            line->phase = LINE_EXTENDED;
        }
    } else if (phase == LINE_DISCRIMINATOR) {
        if (line->cursor != line->extended_end)
            fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);
        else
            line->registers.discriminator = value;
    } else if (phase == LINE_FILE_VALUE) {
        if (line->argument == 0)
            line->file.directory = value;
        else if (line->argument == 1)
            line->file.timestamp = value;
        else
            line->file.bytes = value;
        ++line->argument;
        if (line->argument == 3) {
            if (line->cursor != line->extended_end)
                fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);
            else
                line->files[line->view.file_count++] = line->file;
        } else {
            begin_leb(line, LINE_FILE_VALUE, false);
        }
    } else {
        switch (line->opcode) {
        case 2:
            (void)advance_operations(line, value);
            break;
        case 4:
            line->registers.file = value;
            break;
        case 5:
            line->registers.column = value;
            break;
        case 12:
            line->registers.isa = value;
            break;
        default:
            fail(line, TB_SYMBOL_LINE_INVALID, TB_SYMBOL_LINE_OPERAND);
            break;
        }
    }
}


static void leb(TbSymbolLine_t *line, unsigned char byte)
{
    size_t consumed;
    TbSymbolReadStatus_e status = tb_symbol_leb_feed(&line->leb, &byte, 1, &consumed);
    if (status == TB_SYMBOL_READ_LIMIT)
        return;
    if (status != TB_SYMBOL_READ_OK) {
        fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_OPERAND);
        return;
    }

    if (line->phase == LINE_UNKNOWN_ARGUMENT) {
        if (--line->remaining)
            begin_leb(line, LINE_UNKNOWN_ARGUMENT, false);
        else
            fail(line, TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_EXTENSION);
    } else if (line->phase == LINE_SLEB) {
        int64_t value;
        if (!tb_symbol_leb_sint(&line->leb, &value))
            fail(line, TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_OPERAND);
        else
            (void)advance_line(line, value);
        line->phase = LINE_OPCODE;
    } else {
        uint64_t value;
        if (!tb_symbol_leb_uint(&line->leb, &value))
            fail(line, TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_OPERAND);
        else
            leb_value(line, value);
    }
}


static void fixed(TbSymbolLine_t *line, unsigned byte)
{
    unsigned index = line->view.header.byte_order == TB_SYMBOL_LITTLE_ENDIAN ?
                         line->received : line->width - 1 - line->received;
    line->accumulator |= (uint64_t)byte << (index * 8U);
    if (++line->received != line->width)
        return;

    if (line->phase == LINE_ADDRESS)
        line->registers.address = line->accumulator;
    else
        (void)advance_address(line, line->accumulator);
    line->registers.operation = 0;
    line->phase              = LINE_OPCODE;
}


static bool in_extended(unsigned phase)
{
    return phase == LINE_EXTENDED || phase == LINE_ADDRESS || phase == LINE_DISCRIMINATOR ||
           phase == LINE_FILE_NAME || phase == LINE_FILE_VALUE || phase == LINE_UNKNOWN_EXTENDED;
}


static void complete(TbSymbolLine_t *line)
{
    if (line->phase != LINE_OPCODE)
        fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);
    else if (line->sequence_open)
        fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_SEQUENCE);
    else
        line->status = TB_SYMBOL_LINE_COMPLETE;
}


TbSymbolLineStatus_e tb_symbol_line_feed(TbSymbolLine_t *line, const void *bytes, size_t length,
                                        size_t *consumed)
{
    if (!line || !line->initialized || !consumed || (length && !bytes))
        return TB_SYMBOL_LINE_INVALID;

    *consumed = 0;
    const unsigned char *input = bytes;
    size_t limit = length < 256 ? length : 256;
    while (*consumed < limit && line->status == TB_SYMBOL_LINE_PENDING) {
        unsigned phase = line->phase;
        unsigned char byte = input[(*consumed)++];
        ++line->cursor;
        switch (phase) {
        case LINE_OPCODE:
            opcode(line, byte);
            break;
        case LINE_EXTENDED:
            extended(line, byte);
            break;
        case LINE_FIXED:
        case LINE_ADDRESS:
            fixed(line, byte);
            break;
        case LINE_FILE_NAME:
            if (byte)
                ++line->file.name_bytes;
            else
                begin_leb(line, LINE_FILE_VALUE, false);
            break;
        case LINE_UNKNOWN_EXTENDED:
            if (line->cursor == line->extended_end)
                fail(line, TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_EXTENSION);
            break;
        default:
            leb(line, byte);
            break;
        }

        if (line->status == TB_SYMBOL_LINE_PENDING && in_extended(line->phase) &&
            line->cursor == line->extended_end) {
            fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);
        }
        if (line->status == TB_SYMBOL_LINE_PENDING && line->cursor == line->view.header.extent.end_offset)
            complete(line);
    }

    return line->status;
}


TbSymbolLineStatus_e tb_symbol_line_finish(TbSymbolLine_t *line)
{
    if (!line || !line->initialized)
        return TB_SYMBOL_LINE_INVALID;

    if (line->status == TB_SYMBOL_LINE_PENDING)
        fail(line, TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);

    return line->status;
}


bool tb_symbol_line_view(const TbSymbolLine_t *line, TbSymbolLineView_t *view)
{
    if (!line || !line->initialized || !view || line->status != TB_SYMBOL_LINE_COMPLETE)
        return false;

    *view = line->view;

    return true;
}


bool tb_symbol_line_failure(const TbSymbolLine_t *line, TbSymbolLineFailure_t *failure)
{
    if (!line || !line->initialized || !failure || line->status < TB_SYMBOL_LINE_MALFORMED)
        return false;

    *failure = line->failure;

    return true;
}


uint64_t tb_symbol_line_progress(const TbSymbolLine_t *line)
{
    return line && line->initialized ? line->cursor - line->view.header.program_offset : 0;
}


void tb_symbol_line_discard(TbSymbolLine_t *line)
{
    if (line)
        memset(line, 0, sizeof(*line));
}
