#include "symbol/source_file.h"
#include "../support/check.h"

#include <elf.h>
#include <stdio.h>
#include <string.h>

static TbSymbolElfHeader_t header;
static TbSymbolSectionEntry_t sections[4];
static TbSymbolSectionsView_t directory;
static TbSymbolLineReferenceView_t reference;
static TbSymbolLineFilesView_t prologue;
static TbSymbolLineView_t program;
static TbSymbolLineEntry_t entries[3];
static TbSymbolLineField_t fields[8];
static TbSymbolLineRow_t rows[3];
static TbSymbolLineFile_t events[1];
static TbSymbolSourceFile_t file;

static TbSymbolSourceSections_t roles(void)
{
    return (TbSymbolSourceSections_t) {
        .line         = 1,
        .strings      = 2,
        .line_strings = 3
    };
}


static TbSymbolLineField_t path_field(uint64_t offset, uint64_t form, uint64_t value)
{
    return (TbSymbolLineField_t) {
        .offset  = offset,
        .content = 1,
        .value = {
            .form           = form,
            .payload_offset = form == TB_SYMBOL_FORM_STRING ? 0 : 1,
            .payload_bytes  = 6,
            .bits           = value,
            .numeric        = form != TB_SYMBOL_FORM_STRING
        }
    };
}


static void fixture(unsigned version)
{
    memset(sections, 0, sizeof(sections));
    memset(fields, 0, sizeof(fields));
    header = (TbSymbolElfHeader_t) {
        .file_bytes = 4096,
        .byte_order = TB_SYMBOL_LITTLE_ENDIAN
    };

    for (unsigned i = 0; i < 4; ++i) {
        sections[i].header.type = SHT_PROGBITS;
        sections[i].header.bytes = 1024;
    }
    directory = (TbSymbolSectionsView_t) {
        .header  = &header,
        .entries = sections,
        .count   = 4,
        .backing = {
            .key = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        }
    };

    reference = (TbSymbolLineReferenceView_t) {
        .source = {
            .backing     = directory.backing,
            .unit_offset = 100
        },
        .unit = {
            .extent = {
                .end_offset = 300
            },
            .address_bytes = 8
        },
        .line_offset = 10
    };

    TbSymbolLineHeader_t line = {
        .extent = {
            .end_offset   = 150,
            .offset_bytes = 4
        },
        .table_offset  = 10,
        .entries_offset = 20,
        .program_offset = 100,
        .byte_order     = TB_SYMBOL_LITTLE_ENDIAN,
        .version        = version,
        .address_bytes  = version == 5 ? 8U : 0U
    };

    unsigned first = version == 5 ? 0U : 1U;
    entries[0] = (TbSymbolLineEntry_t) {
        .index       = first,
        .offset      = 20,
        .first_field = 0,
        .field_count = 1,
        .directory   = true
    };

    entries[1] = (TbSymbolLineEntry_t) {
        .index       = first + 1,
        .offset      = 30,
        .first_field = 1,
        .field_count = 1,
        .directory   = true
    };

    entries[2] = (TbSymbolLineEntry_t) {
        .index       = first,
        .offset      = 40,
        .first_field = 2,
        .field_count = version == 5 ? 5U : 4U
    };

    fields[0] = path_field(20, version == 5 ? TB_SYMBOL_FORM_STRP : TB_SYMBOL_FORM_STRING, 8);
    fields[1] = path_field(30, version == 5 ? TB_SYMBOL_FORM_LINE_STRP : TB_SYMBOL_FORM_STRING, 16);
    fields[2] = path_field(40, TB_SYMBOL_FORM_STRING, 0);
    for (unsigned i = 3; i < 6; ++i) {
        fields[i] = (TbSymbolLineField_t) {
            .offset  = 50 + i,
            .content = i - 1,
            .value = {
                .form    = TB_SYMBOL_FORM_UDATA,
                .bits    = i == 3 ? first : 0,
                .numeric = true
            }
        };
    }
    fields[6] = (TbSymbolLineField_t) {
        .offset  = 70,
        .content = 5,
        .value = {
            .form          = TB_SYMBOL_FORM_DATA16,
            .payload_bytes = 16
        }
    };

    prologue = (TbSymbolLineFilesView_t) {
        .header          = line,
        .entries         = entries,
        .fields          = fields,
        .directory_count = 2,
        .file_count      = 1,
        .field_count     = version == 5 ? 7U : 6U
    };

    rows[0] = (TbSymbolLineRow_t) {
        .file               = first,
        .instruction_offset = 110,
        .line               = 1
    };

    rows[1] = (TbSymbolLineRow_t) {
        .file               = first + 1,
        .instruction_offset = 130,
        .line               = 2
    };

    rows[2] = (TbSymbolLineRow_t) {
        .end_sequence = true
    };

    events[0] = (TbSymbolLineFile_t) {
        .instruction_offset = 120,
        .name_offset        = 122,
        .name_bytes         = 3,
        .directory          = 0,
        .timestamp          = 10,
        .bytes              = 11
    };

    program = (TbSymbolLineView_t) {
        .header        = line,
        .rows          = rows,
        .files         = events,
        .row_count     = 3,
        .file_count    = version < 5 ? 1 : 0,
        .address_bytes = 8
    };
}


static TbSymbolSourceFileStatus_e select_file(TbSymbolSourceFileKind_e kind, uint64_t index,
    TbSymbolSourceSections_t selected)
{
    TbSymbolSourceFileStatus_e status = tb_symbol_source_file_init(&file, directory, reference,
        prologue, program, selected, (TbSymbolSourceFileSelection_t) {
            .kind  = kind,
            .index = index
        });
    while (status == TB_SYMBOL_SOURCE_FILE_PENDING)
        status = tb_symbol_source_file_step(&file, 1);
    return status;
}


static void versions(void)
{
    for (unsigned version = 2; version <= 5; ++version) {
        fixture(version);
        CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_FOUND);
        TbSymbolSourceFileView_t view;
        CHECK(tb_symbol_source_file_view(&file, &view));
        CHECK(view.line_section == 1 && view.file_index == (version < 5 ? 1U : 0U) &&
              view.file_offset == 40);
        CHECK(view.path.status == TB_SYMBOL_SOURCE_TEXT_READY && view.path.selection.offset == 40 &&
              view.path.selection.end_offset == 47 && view.path.selection.descriptor == -1);
        CHECK(view.md5_available == (version == 5) && view.timestamp_available && view.timestamp == 0);
        if (version == 5)
            CHECK(view.md5_offset == 70);
        CHECK(view.directory_current == (version == 5));
        CHECK(view.compilation.status == (version == 5 ? TB_SYMBOL_SOURCE_TEXT_READY :
                                                        TB_SYMBOL_SOURCE_TEXT_ABSENT));
        CHECK(select_file(TB_SYMBOL_SOURCE_FILE_DECLARATION, 0, roles()) == TB_SYMBOL_SOURCE_FILE_ABSENT);
        CHECK(select_file(TB_SYMBOL_SOURCE_FILE_CALL, 0, roles()) == TB_SYMBOL_SOURCE_FILE_ABSENT);
        CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 2, roles()) == TB_SYMBOL_SOURCE_FILE_ABSENT);
        CHECK(select_file(TB_SYMBOL_SOURCE_FILE_INDEX, 0, roles()) ==
              (version == 5 ? TB_SYMBOL_SOURCE_FILE_FOUND : TB_SYMBOL_SOURCE_FILE_MALFORMED));
        if (version == 5)
            continue;
        CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 1, roles()) == TB_SYMBOL_SOURCE_FILE_FOUND);
        CHECK(tb_symbol_source_file_view(&file, &view) && view.dynamic && view.file_index == 2);
        CHECK(view.path.selection.offset == 122 && view.path.selection.end_offset == 126 &&
              view.directory_current && view.timestamp == 10 && view.bytes == 11 && !view.md5_available);
        rows[1].instruction_offset = 120;
        CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 1, roles()) == TB_SYMBOL_SOURCE_FILE_MALFORMED);
        CHECK(!tb_symbol_source_file_view(&file, &view));
    }
}


static void dependencies_and_failures(void)
{
    fixture(5);
    TbSymbolSourceSections_t selected = roles();
    selected.strings = UINT64_MAX;
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, selected) == TB_SYMBOL_SOURCE_FILE_FOUND);
    TbSymbolSourceFileView_t view;
    CHECK(tb_symbol_source_file_view(&file, &view) &&
          view.compilation.status == TB_SYMBOL_SOURCE_TEXT_DEPENDENCY);
    fields[2].value.form = TB_SYMBOL_FORM_STRX;
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_FOUND);
    CHECK(tb_symbol_source_file_view(&file, &view) && view.path.status == TB_SYMBOL_SOURCE_TEXT_UNSUPPORTED);
    fields[2] = path_field(99, TB_SYMBOL_FORM_STRING, 0);
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_MALFORMED);
    fixture(5);
    fields[3].value.bits = 2;
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_MALFORMED);
    fixture(5);
    fields[3].value.numeric = false;
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_FOUND);
    CHECK(tb_symbol_source_file_view(&file, &view) && view.partial && !view.directory_available);
    fixture(5);
    ++reference.source.backing.key.inode;
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_INVALID);
    fixture(5);
    ++program.header.program_offset;
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_INVALID);
    fixture(5);
    sections[1].header.flags = SHF_COMPRESSED;
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_UNSUPPORTED);
    fixture(5);
    CHECK(select_file(TB_SYMBOL_SOURCE_FILE_ROW, 0, roles()) == TB_SYMBOL_SOURCE_FILE_FOUND);
    TbSymbolSourceFile_t saved = file;
    CHECK(tb_symbol_source_file_step(&file, 257) == TB_SYMBOL_SOURCE_FILE_INVALID);
    CHECK(!memcmp(&file, &saved, sizeof(file)));
    tb_symbol_source_file_discard(&file);
    CHECK(!tb_symbol_source_file_view(&file, &view));
}


int main(void)
{
    versions();
    dependencies_and_failures();
    puts("PASS source indexes, exact metadata spans, dynamic visibility, dependencies and context failures");
    return 0;
}
