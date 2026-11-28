#include "symbol/line_files.h"
#include "../support/check.h"

#include <string.h>

static unsigned char image[16384];
static size_t used;
static TbSymbolLineHeader_t header;
static TbSymbolLineFiles_t files;
static TbSymbolLineEntry_t entries[600];
static TbSymbolLineField_t fields[2400];

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


static void fixed(unsigned width, uint64_t value)
{
    for (unsigned i = 0; i < width; ++i) {
        unsigned index = header.byte_order == TB_SYMBOL_LITTLE_ENDIAN ? i : width - 1 - i;
        image[used + index] = (unsigned char)(value >> (8U * i));
    }
    used += width;
}


static void text(const char *value)
{
    do {
        byte((unsigned char)*value);
    } while (*value++);
}


static void fixture(unsigned version, unsigned width, TbSymbolByteOrder_e order)
{
    tb_symbol_line_files_discard(&files);
    memset(image, 0, sizeof(image));
    used = 0;
    /* Completed fixed-prefix models isolate variable-prologue interpretation;
     * the native harness composes this with actual encoded fixed prefixes. */
    header = (TbSymbolLineHeader_t) {
        .extent = {
            .offset_bytes = width
        },
        .table_offset  = 50,
        .entries_offset = 80,
        .byte_order    = order,
        .version       = version,
        .address_bytes = version == 5 ? 8 : 0
    };
}


static void seal(size_t entry_capacity, size_t field_capacity)
{
    header.program_offset = header.entries_offset + used;
    header.extent.end_offset = header.program_offset + 3;
    CHECK(tb_symbol_line_files_init(&files, header, entries, entry_capacity, fields, field_capacity) ==
          TB_SYMBOL_LINE_FILES_PENDING);
}


static TbSymbolLineFilesStatus_e run(size_t chunk)
{
    size_t cursor = 0;
    for (unsigned turn = 0; turn < 100000; ++turn) {
        TbSymbolLineFilesView_t unavailable;
        CHECK(!tb_symbol_line_files_view(&files, &unavailable));
        size_t count = used - cursor < chunk ? used - cursor : chunk;
        size_t consumed;
        uint64_t before = tb_symbol_line_files_progress(&files);
        TbSymbolLineFilesStatus_e status =
            tb_symbol_line_files_feed(&files, image + cursor, count, &consumed);
        CHECK(consumed <= count && consumed <= 256);
        CHECK(files.progress - before <= 256);
        CHECK(consumed || files.progress > before);
        cursor += consumed;
        if (status != TB_SYMBOL_LINE_FILES_PENDING)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_LINE_FILES_INVALID;
}


static TbSymbolLineFilesView_t completed(void)
{
    TbSymbolLineFilesView_t view;
    CHECK(tb_symbol_line_files_view(&files, &view));
    return view;
}


static void legacy(void)
{
    text("src"); text("include"); byte(0);
    text("main.c"); uleb(1); uleb(123); uleb(77);
    text("main.c"); uleb(2); uleb(0); uleb(0);
    byte(0);
}


static void modern(uint64_t path_form)
{
    byte(1); uleb(1); uleb(TB_SYMBOL_FORM_STRING);
    uleb(2); text("src"); text("include");
    byte(5);
    uleb(1); uleb(path_form);
    uleb(2); uleb(TB_SYMBOL_FORM_DATA1);
    uleb(3); uleb(TB_SYMBOL_FORM_UDATA);
    uleb(4); uleb(TB_SYMBOL_FORM_DATA2);
    uleb(5); uleb(TB_SYMBOL_FORM_DATA16);
    uleb(2);
    for (unsigned i = 0; i < 2; ++i) {
        if (path_form == TB_SYMBOL_FORM_STRING)
            text("main.c");
        else if (path_form == TB_SYMBOL_FORM_STRX)
            uleb(i);
        else
            fixed(header.extent.offset_bytes, UINT32_C(0x12345678));
        byte(i); uleb(123); fixed(2, 77);
        for (unsigned j = 0; j < 16; ++j)
            byte(17 * i + j);
    }
}


static void check_versions(void)
{
    for (unsigned version = 2; version <= 5; ++version) {
        for (unsigned width = 4; width <= 8; width += 4) {
            for (unsigned order = 0; order < 2; ++order) {
                fixture(version, width, (TbSymbolByteOrder_e)order);
                if (version < 5)
                    legacy();
                else
                    modern(TB_SYMBOL_FORM_STRING);
                for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
                    seal(600, 2400);
                    CHECK(run(chunk) == TB_SYMBOL_LINE_FILES_COMPLETE);
                    TbSymbolLineFilesView_t view = completed();
                    CHECK(view.directory_count == 2 && view.file_count == 2);
                    CHECK(view.field_count == (version == 5 ? 12 : 10));
                    CHECK(entries[0].index == (version == 5 ? 0 : 1));
                    CHECK(entries[2].index == (version == 5 ? 0 : 1));
                    CHECK(entries[3].index == (version == 5 ? 1 : 2));
                    CHECK(entries[2].directory == false && entries[1].directory);
                    CHECK(fields[entries[2].first_field].value.payload_bytes == 6);
                    CHECK(fields[entries[2].first_field + 3].value.bits == 77);
                    CHECK(entries[2].offset != entries[3].offset);
                    CHECK(view.directory_format[0].offset == (version < 5 ? UINT64_MAX : 81));
                    if (version == 5) {
                        const TbSymbolLineField_t *checksum = &fields[entries[2].first_field + 4];
                        CHECK(checksum->value.form == TB_SYMBOL_FORM_DATA16);
                        CHECK(checksum->value.payload_bytes == 16);
                        CHECK(!memcmp(image + checksum->offset - header.entries_offset,
                                      "\x00\x01\x02\x03\x04\x05\x06\x07"
                                      "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f", 16));
                    }
                }
            }
        }
    }
}


static void check_references(void)
{
    const uint64_t forms[] = {
        TB_SYMBOL_FORM_STRP, TB_SYMBOL_FORM_LINE_STRP, TB_SYMBOL_FORM_STRP_SUP, TB_SYMBOL_FORM_STRX
    };
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
        fixture(5, 8, TB_SYMBOL_BIG_ENDIAN);
        modern(forms[i]); seal(600, 2400);
        CHECK(run(1) == (i < 2 ? TB_SYMBOL_LINE_FILES_COMPLETE : TB_SYMBOL_LINE_FILES_PARTIAL));
        TbSymbolLineFilesView_t view = completed();
        const TbSymbolLineField_t *path = &view.fields[view.entries[2].first_field];
        CHECK(path->value.form == forms[i]);
        CHECK(path->profile == (i < 2 ? TB_SYMBOL_LINE_FIELD_REFERENCE : TB_SYMBOL_LINE_FIELD_UNSUPPORTED));
        if (i < 3)
            CHECK(path->value.bits == UINT32_C(0x12345678));
    }
}


static void check_failures(void)
{
    fixture(4, 4, TB_SYMBOL_LITTLE_ENDIAN); legacy(); seal(3, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_RESOURCE);
    TbSymbolLineFilesView_t unavailable;
    CHECK(!tb_symbol_line_files_view(&files, &unavailable));

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN); modern(TB_SYMBOL_FORM_STRING); seal(600, 3);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_RESOURCE);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(2); uleb(1); uleb(TB_SYMBOL_FORM_STRING); uleb(1); uleb(TB_SYMBOL_FORM_STRING);
    seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_CONFLICT);
    TbSymbolLineFilesFailure_t failure;
    CHECK(tb_symbol_line_files_failure(&files, &failure));
    CHECK(failure.field_available && failure.content == 1 && failure.field_offset == 83);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(1); uleb(1); uleb(TB_SYMBOL_FORM_ADDR); seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_MALFORMED);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(1); uleb(1); uleb(0x77); seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_UNSUPPORTED);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); uleb(1); seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_MALFORMED);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(0); byte(0); byte(0); byte(0); seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_MALFORMED);

    fixture(4, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); text("broken.c"); byte(0x80); seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_MALFORMED);
}


static void check_extensions_and_bounds(void)
{
    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(0);
    byte(3);
    uleb(1); uleb(TB_SYMBOL_FORM_STRING);
    uleb(0x2000); uleb(TB_SYMBOL_FORM_BLOCK1);
    uleb(0x2000); uleb(TB_SYMBOL_FORM_FLAG);
    byte(1); text("a.c"); byte(2); byte(0x12); byte(0x34); byte(0xff);
    seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_PARTIAL);
    CHECK(completed().field_count == 3 && fields[1].value.payload_bytes == 2);
    CHECK(fields[2].value.bits == 255 && fields[2].profile == TB_SYMBOL_LINE_FIELD_UNSUPPORTED);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(0); byte(1); byte(1); byte(TB_SYMBOL_FORM_STRING); uleb(520);
    for (unsigned i = 0; i < 520; ++i)
        text("same.c");
    seal(600, 2400);
    CHECK(run(4096) == TB_SYMBOL_LINE_FILES_COMPLETE && completed().file_count == 520);
    CHECK(entries[519].index == 519);

    fixture(4, 4, TB_SYMBOL_LITTLE_ENDIAN);
    for (unsigned i = 0; i < 520; ++i)
        byte('a');
    byte(0); byte(0); byte(0); seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_COMPLETE && fields[0].value.payload_bytes == 520);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(0); byte(255);
    uleb(1); uleb(TB_SYMBOL_FORM_STRING);
    for (unsigned i = 1; i < 255; ++i) {
        uleb(i == 254 ? 5 : 0x2000);
        uleb(i == 254 ? TB_SYMBOL_FORM_DATA16 : TB_SYMBOL_FORM_DATA1);
    }
    byte(1); text("last.c");
    for (unsigned i = 1; i < 254; ++i)
        byte(i);
    for (unsigned i = 0; i < 16; ++i)
        byte(i);
    seal(1, 255);
    CHECK(run(4096) == TB_SYMBOL_LINE_FILES_PARTIAL && completed().file_formats == 255);
    CHECK(completed().field_count == 255 && fields[254].content == 5);
    seal(1, 254);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_RESOURCE);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(0); byte(1); uleb(1); uleb(TB_SYMBOL_FORM_STRING); uleb(UINT64_MAX);
    seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_RESOURCE);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(1);
    for (unsigned i = 0; i < 10; ++i)
        byte(0xff);
    byte(0); seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_UNSUPPORTED);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    byte(0); byte(0); byte(0); byte(0); seal(0, 0);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_COMPLETE && completed().field_count == 0);
}


static void check_lifetimes(void)
{
    fixture(4, 4, TB_SYMBOL_LITTLE_ENDIAN); legacy(); seal(600, 2400);
    TbSymbolLineFiles_t old = files;
    TbSymbolLineEntry_t old_entry = entries[0];
    CHECK(tb_symbol_line_files_init(&files, header, entries, SIZE_MAX, fields, 2400) ==
          TB_SYMBOL_LINE_FILES_INVALID);
    CHECK(!memcmp(&files, &old, sizeof(files)) && !memcmp(&entries[0], &old_entry, sizeof(old_entry)));

    for (size_t boundary = 0; boundary < used; ++boundary) {
        seal(600, 2400);
        size_t cursor = 0;
        while (cursor < boundary) {
            size_t consumed;
            (void)tb_symbol_line_files_feed(&files, image + cursor, boundary - cursor, &consumed);
            cursor += consumed;
        }
        CHECK(tb_symbol_line_files_finish(&files) == TB_SYMBOL_LINE_FILES_MALFORMED);
        TbSymbolLineFilesView_t view;
        CHECK(!tb_symbol_line_files_view(&files, &view));
    }
    seal(600, 2400);
    CHECK(run(1) == TB_SYMBOL_LINE_FILES_COMPLETE);
    uint64_t total = files.progress;
    for (uint64_t prefix = 0; prefix <= total; ++prefix) {
        seal(600, 2400);
        size_t cursor = 0;
        while (files.progress < prefix && files.status == TB_SYMBOL_LINE_FILES_PENDING) {
            size_t consumed;
            (void)tb_symbol_line_files_feed(&files, image + cursor, cursor < used ? 1 : 0, &consumed);
            cursor += consumed;
        }
        old_entry = entries[0];
        tb_symbol_line_files_discard(&files);
        CHECK(!memcmp(&entries[0], &old_entry, sizeof(old_entry)));
        CHECK(tb_symbol_line_files_progress(&files) == 0);
    }
}


int main(void)
{
    check_versions();
    check_references();
    check_failures();
    check_extensions_and_bounds();
    check_lifetimes();

    puts("PASS bounded line prologues, physical file identities, raw checksums and failure atomicity");
    return 0;
}
