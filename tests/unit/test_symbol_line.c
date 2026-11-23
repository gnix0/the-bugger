#include "symbol/line.h"
#include "../support/check.h"

#include <string.h>

static unsigned char image[8192];
static TbSymbolLineHeader_t header;
static TbSymbolLine_t line;
static TbSymbolLineRow_t rows[600];
static TbSymbolLineFile_t files[8];
static TbSymbolDwarfSpan_t span;
static size_t prefix;
static size_t used;
static size_t length_at;
static unsigned offset_bytes;
static unsigned address_bytes;

static void put(size_t at, unsigned width, uint64_t value)
{
    for (unsigned i = 0; i < width; ++i) {
        unsigned index = span.byte_order == TB_SYMBOL_LITTLE_ENDIAN ? i : width - 1 - i;
        image[at + index] = (unsigned char)(value >> (8U * i));
    }
}


static void byte(unsigned value)
{
    CHECK(used < sizeof(image));
    image[used++] = (unsigned char)value;
}


static void uleb(uint64_t value)
{
    do {
        unsigned part = (unsigned)(value & 0x7fU);
        value >>= 7U;
        byte(part | (value ? 0x80U : 0));
    } while (value);
}


static void fixture(unsigned version, unsigned format, unsigned width, TbSymbolByteOrder_e order)
{
    tb_symbol_line_discard(&line);
    memset(image, 0, sizeof(image));
    span = (TbSymbolDwarfSpan_t) {
        .unit_offset = 37,
        .byte_order  = order
    };
    offset_bytes  = format;
    address_bytes = width;
    used = format == 8 ? 12 : 4;
    put(used, 2, version);
    used += 2;
    if (version == 5) {
        byte(width);
        byte(0);
    }
    length_at = used;
    used += format;
    byte(1);
    if (version >= 4)
        byte(1);
    byte(1);
    byte(251); /* -5 */
    byte(14);
    byte(13);
    static const unsigned char counts[] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    for (size_t i = 0; i < sizeof(counts); ++i)
        byte(counts[i]);
    /* This target isolates fixed-prefix/program decoding. Variable prologue
     * bytes remain uninterpreted; complete file tables are a separate layer. */
    byte(0);
    byte(0);
    prefix = used;
    put(length_at, format, prefix - length_at - format);
}


static void address(uint64_t value)
{
    byte(0);
    uleb(address_bytes + 1);
    byte(2);
    CHECK(used + address_bytes <= sizeof(image));
    put(used, address_bytes, value);
    used += address_bytes;
}


static void end(void)
{
    byte(0);
    byte(1);
    byte(1);
}


static void seal(void)
{
    unsigned initial = offset_bytes == 8 ? 12 : 4;
    if (offset_bytes == 8) {
        put(0, 4, UINT32_MAX);
        put(4, 8, used - initial);
    } else {
        put(0, 4, used - initial);
    }
    span.section_bytes = span.unit_offset + used;
    CHECK(tb_symbol_line_header(span, image, used, &header) == TB_SYMBOL_DWARF_OK);
    CHECK(header.program_offset == span.unit_offset + prefix);
}


static void start(size_t row_capacity, size_t file_capacity)
{
    CHECK(tb_symbol_line_init(&line, header, address_bytes, rows, row_capacity, files, file_capacity) ==
          (used == prefix ? TB_SYMBOL_LINE_COMPLETE : TB_SYMBOL_LINE_PENDING));
}


static TbSymbolLineStatus_e run(size_t chunk)
{
    size_t cursor = prefix;
    TbSymbolLineStatus_e status = line.status;
    while (status == TB_SYMBOL_LINE_PENDING) {
        CHECK(cursor < used);
        TbSymbolLineView_t unavailable;
        CHECK(!tb_symbol_line_view(&line, &unavailable));
        size_t request = used - cursor < chunk ? used - cursor : chunk;
        size_t consumed = SIZE_MAX;
        uint64_t before = tb_symbol_line_progress(&line);
        status = tb_symbol_line_feed(&line, image + cursor, request, &consumed);
        CHECK(consumed && consumed <= request && consumed <= 256);
        CHECK(tb_symbol_line_progress(&line) - before == consumed);
        cursor += consumed;
    }
    return status;
}


static TbSymbolLineView_t completed(void)
{
    TbSymbolLineView_t view;
    CHECK(tb_symbol_line_view(&line, &view));
    return view;
}


static void check_versions(void)
{
    for (unsigned version = 2; version <= 5; ++version) {
        for (unsigned format = 4; format <= 8; format += 4) {
            for (unsigned width = 1; width <= 8; width *= 2) {
                for (unsigned order = 0; order < 2; ++order) {
                    fixture(version, format, width, (TbSymbolByteOrder_e)order);
                    address(0x40);
                    byte(1);
                    byte(3); byte(5);
                    byte(2); byte(4);
                    byte(1);
                    end();
                    seal();
                    for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
                        start(600, 8);
                        CHECK(run(chunk) == TB_SYMBOL_LINE_COMPLETE);
                        TbSymbolLineView_t view = completed();
                        CHECK(view.row_count == 3 && view.file_count == 0);
                        CHECK(view.rows[0].address == 0x40 && view.rows[0].line == 1);
                        CHECK(view.rows[0].file == 1 && view.rows[0].is_stmt);
                        CHECK(view.rows[1].address == 0x44 && view.rows[1].line == 6);
                        CHECK(view.rows[2].end_sequence && view.rows[2].address == 0x44);
                    }
                }
            }
        }
    }
}


static void check_flags_and_sequences(void)
{
    fixture(5, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    address(100);
    byte(4); byte(0);
    byte(5); byte(7);
    byte(6); byte(7); byte(10); byte(11);
    byte(12); byte(3);
    byte(0); byte(2); byte(4); byte(9);
    byte(1);
    byte(18); /* special: no address advance, line delta zero */
    byte(8);  /* const_add_pc: (255 - 13) / 14 = 17 */
    byte(9); byte(2); byte(0);
    end();
    address(3);
    byte(1);
    end();
    seal();
    start(600, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_COMPLETE);
    TbSymbolLineView_t view = completed();
    CHECK(view.row_count == 5);
    CHECK(view.rows[0].file == 0 && view.rows[0].column == 7 && view.rows[0].isa == 3);
    CHECK(!view.rows[0].is_stmt && view.rows[0].basic_block && view.rows[0].prologue_end);
    CHECK(view.rows[0].epilogue_begin && view.rows[0].discriminator == 9);
    CHECK(!view.rows[1].basic_block && !view.rows[1].prologue_end && !view.rows[1].epilogue_begin);
    CHECK(view.rows[1].discriminator == 0 && view.rows[1].isa == 3);
    CHECK(view.rows[2].address == 119 && view.rows[2].end_sequence);
    CHECK(view.rows[3].address == 3 && view.rows[3].sequence == 1);
    CHECK(view.rows[3].file == 1 && view.rows[3].line == 1 && view.rows[3].is_stmt);
    CHECK(view.rows[3].column == 0 && view.rows[3].isa == 0);
}


static void check_vliw(void)
{
    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    image[length_at + offset_bytes + 1] = 3;
    address(100);
    byte(2); byte(2); byte(1);
    byte(2); byte(2); byte(1);
    byte(9); byte(2); byte(0); byte(1);
    end();
    seal();
    start(600, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_COMPLETE);
    CHECK(completed().rows[0].address == 100 && completed().rows[0].operation == 2);
    CHECK(completed().rows[1].address == 101 && completed().rows[1].operation == 1);
    CHECK(completed().rows[2].address == 103 && completed().rows[2].operation == 0);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    image[length_at + offset_bytes + 1] = 2;
    byte(2); byte(1);
    byte(2); uleb(UINT64_MAX);
    byte(1); end();
    seal();
    start(600, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_COMPLETE);
    CHECK(completed().rows[0].address == (UINT64_C(1) << 63U));
    CHECK(completed().rows[0].operation == 0);
}


static void check_files(void)
{
    fixture(4, 8, 4, TB_SYMBOL_BIG_ENDIAN);
    byte(0); byte(8); byte(3);
    byte('a'); byte('.'); byte('c'); byte(0);
    byte(2); byte(3); byte(4);
    byte(1); end();
    seal();
    start(600, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_COMPLETE);
    CHECK(completed().file_count == 1);
    CHECK(files[0].name_offset == header.program_offset + 3 && files[0].name_bytes == 3);
    CHECK(files[0].directory == 2 && files[0].timestamp == 3 && files[0].bytes == 4);
    start(600, 0);
    CHECK(run(1) == TB_SYMBOL_LINE_RESOURCE);
    TbSymbolLineFailure_t failure;
    CHECK(tb_symbol_line_failure(&line, &failure) && failure.reason == TB_SYMBOL_LINE_FILES);
}


static void expect_failure(TbSymbolLineStatus_e expected, TbSymbolLineFailure_e reason)
{
    seal();
    start(600, 8);
    CHECK(run(1) == expected);
    TbSymbolLineView_t unavailable;
    CHECK(!tb_symbol_line_view(&line, &unavailable));
    TbSymbolLineFailure_t failure;
    CHECK(tb_symbol_line_failure(&line, &failure) && failure.reason == reason);
}


static void check_failures(void)
{
    fixture(4, 4, 1, TB_SYMBOL_LITTLE_ENDIAN);
    address(255); byte(2); byte(1);
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_ADDRESS);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(3); byte(0x7e); /* line 1 - 2 */
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_NUMBER);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    address(20); byte(1); address(19); byte(1);
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_ORDER);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(1);
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_SEQUENCE);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(0);
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(2); byte(1); byte(0);
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(2); byte(4); byte(0x80);
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_FRAMING);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(2); byte(77); byte(0xff);
    expect_failure(TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_EXTENSION);

    fixture(2, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(10);
    expect_failure(TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_EXTENSION);

    fixture(5, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(1); byte(3);
    expect_failure(TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_EXTENSION);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(4);
    for (unsigned i = 0; i < 10; ++i)
        byte(0x80);
    byte(1);
    expect_failure(TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_OPERAND);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(1); byte(1); end(); seal();
    start(1, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_RESOURCE);
    TbSymbolLineView_t unavailable;
    CHECK(!tb_symbol_line_view(&line, &unavailable));
}


static void check_header_and_lifetimes(void)
{
    fixture(5, 8, 8, TB_SYMBOL_BIG_ENDIAN);
    byte(1); end(); seal();
    TbSymbolLineHeader_t saved = header;
    for (size_t length = 0; length < prefix - 2; ++length) {
        CHECK(tb_symbol_line_header(span, image, length, &header) == TB_SYMBOL_DWARF_MALFORMED);
        CHECK(!memcmp(&saved, &header, sizeof(header)));
    }
    put(length_at, 8, 1);
    CHECK(tb_symbol_line_header(span, image, used, &header) == TB_SYMBOL_DWARF_MALFORMED);
    put(length_at, 8, UINT64_MAX);
    CHECK(tb_symbol_line_header(span, image, used, &header) == TB_SYMBOL_DWARF_MALFORMED);
    header = saved;
    start(600, 8);
    TbSymbolLine_t old = line;
    CHECK(tb_symbol_line_init(&line, header, 4, rows, 600, files, 8) == TB_SYMBOL_LINE_INVALID);
    CHECK(tb_symbol_line_init(&line, header, 8, rows, SIZE_MAX, files, 8) == TB_SYMBOL_LINE_INVALID);
    CHECK(!memcmp(&line, &old, sizeof(line)));
    header.segment_bytes = 1;
    CHECK(tb_symbol_line_init(&line, header, 8, rows, 600, files, 8) == TB_SYMBOL_LINE_UNSUPPORTED);
    CHECK(!memcmp(&line, &old, sizeof(line)));
    header = saved;

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(1); end(); seal();
    for (size_t boundary = prefix; boundary < used; ++boundary) {
        start(600, 8);
        size_t consumed;
        CHECK(tb_symbol_line_feed(&line, image + prefix, boundary - prefix, &consumed) ==
              TB_SYMBOL_LINE_PENDING);
        CHECK(tb_symbol_line_finish(&line) == TB_SYMBOL_LINE_MALFORMED);
        TbSymbolLineView_t view;
        CHECK(!tb_symbol_line_view(&line, &view));
    }
    for (size_t boundary = prefix; boundary <= used; ++boundary) {
        start(600, 8);
        size_t consumed;
        (void)tb_symbol_line_feed(&line, image + prefix, boundary - prefix, &consumed);
        TbSymbolLineRow_t old_row = rows[0];
        tb_symbol_line_discard(&line);
        CHECK(!memcmp(&old_row, &rows[0], sizeof(old_row)));
        CHECK(tb_symbol_line_progress(&line) == 0);
    }

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    for (unsigned i = 0; i < 520; ++i)
        byte(1);
    end(); seal();
    start(600, 8);
    CHECK(run(4096) == TB_SYMBOL_LINE_COMPLETE && completed().row_count == 521);
    size_t consumed = 99;
    CHECK(tb_symbol_line_feed(&line, image, sizeof(image), &consumed) == TB_SYMBOL_LINE_COMPLETE);
    CHECK(consumed == 0);
}


static void check_boundaries(void)
{
    fixture(5, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    image[length_at + offset_bytes + 2] = 255;
    byte(1); end(); seal();
    start(600, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_COMPLETE && completed().rows[0].is_stmt);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(3); byte(0x7f); byte(1); end(); seal();
    start(600, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_COMPLETE && completed().rows[0].line == 0);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(3);
    for (unsigned i = 0; i < 9; ++i)
        byte(0x80);
    byte(0x7f); /* INT64_MIN: magnitude calculation must not overflow signed C. */
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_NUMBER);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        byte(3);
        for (unsigned i = 0; i < 9; ++i)
            byte(0xff);
        byte(0); /* INT64_MAX */
    }
    byte(1); end(); seal();
    start(600, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_COMPLETE && completed().rows[0].line == UINT64_MAX);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    address(UINT64_MAX); byte(9); byte(1); byte(0);
    expect_failure(TB_SYMBOL_LINE_MALFORMED, TB_SYMBOL_LINE_ADDRESS);

    fixture(5, 8, 8, TB_SYMBOL_LITTLE_ENDIAN);
    byte(1); end(); seal();
    for (size_t boundary = prefix; boundary <= used; ++boundary) {
        start(600, 8);
        size_t consumed;
        TbSymbolLineStatus_e status =
            tb_symbol_line_feed(&line, image + prefix, boundary - prefix, &consumed);
        CHECK(consumed == boundary - prefix);
        if (status == TB_SYMBOL_LINE_PENDING)
            status = tb_symbol_line_feed(&line, image + boundary, used - boundary, &consumed);
        CHECK(status == TB_SYMBOL_LINE_COMPLETE && completed().row_count == 2);
    }
    start(0, 8);
    CHECK(run(1) == TB_SYMBOL_LINE_RESOURCE);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    /* Raise opcode_base, retaining one declared extension with a ULEB operand. */
    size_t opcode_base_at = length_at + offset_bytes + 5;
    memmove(image + opcode_base_at + 14, image + opcode_base_at + 13, 2);
    image[opcode_base_at] = 14;
    image[opcode_base_at + 13] = 1;
    ++prefix;
    ++used;
    put(length_at, 4, prefix - length_at - 4);
    byte(13);
    for (unsigned i = 0; i < 520; ++i)
        byte(0x80);
    byte(0);
    expect_failure(TB_SYMBOL_LINE_UNSUPPORTED, TB_SYMBOL_LINE_EXTENSION);
    CHECK(tb_symbol_line_progress(&line) == 522);

    fixture(5, 8, 8, TB_SYMBOL_BIG_ENDIAN);
    /* Largest fixed prefix: 254 opcode counts; no variable-header validation. */
    opcode_base_at = length_at + offset_bytes + 5;
    image[opcode_base_at] = 255;
    used = opcode_base_at + 1 + 254;
    prefix = used;
    put(length_at, 8, prefix - length_at - 8);
    seal();
    CHECK(prefix == TB_SYMBOL_LINE_PREFIX_BYTES && header.entries_offset == header.program_offset);
    start(600, 8);
    CHECK(completed().row_count == 0);

    fixture(4, 4, 8, TB_SYMBOL_LITTLE_ENDIAN);
    end(); seal();
    start(600, 8);
    TbSymbolLine_t previous = line;
    CHECK(tb_symbol_line_init(&line, header, 0, rows, 600, files, 8) == TB_SYMBOL_LINE_INVALID);
    CHECK(!memcmp(&line, &previous, sizeof(line)));
    unsigned initial = 4;
    put(initial, 2, 6);
    TbSymbolLineHeader_t saved = header;
    CHECK(tb_symbol_line_header(span, image, used, &header) == TB_SYMBOL_DWARF_UNSUPPORTED);
    CHECK(!memcmp(&header, &saved, sizeof(header)));
    put(initial, 2, 4);
    image[length_at + offset_bytes + 6] = 1; /* copy must have zero operands */
    CHECK(tb_symbol_line_header(span, image, used, &header) == TB_SYMBOL_DWARF_MALFORMED);
}


int main(void)
{
    check_versions();
    check_flags_and_sequences();
    check_vliw();
    check_files();
    check_failures();
    check_header_and_lifetimes();
    check_boundaries();

    puts("PASS bounded line prefixes/programs: versions 2-5, widths, sequences, VLIW and failures");
    return 0;
}
