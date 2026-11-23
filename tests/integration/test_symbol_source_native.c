#define _POSIX_C_SOURCE 200809L
#include "symbol/source_file.h"
#include "symbol/source_path.h"
#include "symbol/text.h"
#include "symbol/source_check.h"
#include "symbol/source_prepare.h"
#include "../support/check.h"

#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t section_index(Elf *elf, const char *name)
{
    size_t strings;
    CHECK(elf_getshdrstrndx(elf, &strings) == 0);
    uint64_t found = UINT64_MAX;
    Elf_Scn *current = NULL;
    while ((current = elf_nextscn(elf, current))) {
        GElf_Shdr header;
        CHECK(gelf_getshdr(current, &header));
        const char *text = elf_strptr(elf, strings, header.sh_name);
        CHECK(text);
        if (!strcmp(text, name)) {
            CHECK(found == UINT64_MAX);
            found = elf_ndxscn(current);
        }
    }
    return found;
}


static int frozen_fixture(const char *path, char *temporary)
{
    int input = open(path, O_RDONLY | O_CLOEXEC);
    int writer = mkstemp(temporary);
    CHECK(input >= 0 && writer >= 0);
    unsigned char buffer[4096];
    ssize_t count;
    while ((count = read(input, buffer, sizeof(buffer))) > 0)
        CHECK(write(writer, buffer, (size_t)count) == count);
    CHECK(count == 0 && close(input) == 0 && fchmod(writer, 0400) == 0 && close(writer) == 0);
    int descriptor = open(temporary, O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    return descriptor;
}



static TbSymbolSourcePathText_t text_bytes(Elf *elf, TbSymbolStringView_t string)
{
    CHECK(string.section_index != UINT64_MAX);
    Elf_Data *data = elf_getdata(elf_getscn(elf, string.section_index), NULL);
    CHECK(data && data->d_buf && string.offset < data->d_size && string.bytes < data->d_size - string.offset);
    const unsigned char *bytes = (const unsigned char *)data->d_buf + string.offset;
    CHECK(bytes[string.bytes] == 0 && !memchr(bytes, 0, (size_t)string.bytes));
    return (TbSymbolSourcePathText_t) {
        .data  = bytes,
        .bytes = (size_t)string.bytes
    };
}


static TbSymbolSourcePathText_t resolve_text(TbSymbolSourceTextRef_t reference,
    TbSymbolSectionsView_t directory, int descriptor, TbSymbolBudget_t *budget, Elf *elf)
{
    if (reference.status == TB_SYMBOL_SOURCE_TEXT_ABSENT)
        return (TbSymbolSourcePathText_t) {0};

    CHECK(reference.status == TB_SYMBOL_SOURCE_TEXT_READY && reference.selection.descriptor == -1);
    size_t before = budget->used;
    reference.selection.descriptor = descriptor;
    TbSymbolString_t *string = tb_symbol_string_create(directory, reference.selection, budget);
    CHECK(string);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    while (outcome == TB_SYMBOL_PENDING)
        outcome = tb_symbol_string_step(string, 257);
    TbSymbolStringView_t view;
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_string_view(string, &view));
    TbSymbolSourcePathText_t result = text_bytes(elf, view);
    tb_symbol_string_destroy(string);
    CHECK(budget->used == before);

    return result;
}


static void check_source_digest(Elf *elf, uint64_t line_section, TbSymbolSourceFileView_t source,
    const unsigned char *path)
{
    CHECK(strstr((const char *)path, "/tests/fixtures/native/symbols/"));
    FILE *file = fopen((const char *)path, "rb");
    CHECK(file);
    unsigned char bytes[8192];
    size_t count = fread(bytes, 1, sizeof(bytes), file);
    CHECK(!ferror(file) && feof(file) && count && count < sizeof(bytes) && fclose(file) == 0);
    Elf_Data *line = elf_getdata(elf_getscn(elf, line_section), NULL);
    CHECK(line && source.md5_offset <= line->d_size && 16 <= line->d_size - source.md5_offset);
    const unsigned char *expected = (const unsigned char *)line->d_buf + source.md5_offset;
    static uint64_t identity;
    TbSymbolSourceBytes_t snapshot = {
        .data     = bytes,
        .bytes    = count,
        .identity = ++identity,
        .revision = 1,
        .complete = true
    };
    TbSymbolSourceCheck_t check;
    TbSymbolSourceCheckStatus_e status = tb_symbol_source_check_init(&check, source, snapshot, expected);
    while (status == TB_SYMBOL_SOURCE_CHECK_PENDING)
        status = tb_symbol_source_check_step(&check, snapshot, 7);
    TbSymbolSourceCheckView_t view;
    CHECK(status == TB_SYMBOL_SOURCE_CHECK_MATCH && tb_symbol_source_check_view(&check, snapshot, &view));
    printf("CHECKSUM path=%s digest=", path);
    for (unsigned i = 0; i < 16; ++i)
        printf("%02x", view.calculated[i]);
    puts(" match=1");

    ++snapshot.revision;
    CHECK(!tb_symbol_source_check_view(&check, snapshot, &view));
    CHECK(tb_symbol_source_check_step(&check, snapshot, 7) == TB_SYMBOL_SOURCE_CHECK_STALE);
    bytes[0] ^= 1;
    status = tb_symbol_source_check_init(&check, source, snapshot, expected);
    while (status == TB_SYMBOL_SOURCE_CHECK_PENDING)
        status = tb_symbol_source_check_step(&check, snapshot, 7);
    CHECK(status == TB_SYMBOL_SOURCE_CHECK_MISMATCH &&
          tb_symbol_source_check_view(&check, snapshot, &view));
    tb_symbol_source_check_discard(&check);
}


static void check_line(TbSymbolSectionsView_t directory, TbSymbolLineReferenceView_t reference,
    TbSymbolTextView_t comp_dir, TbSymbolSourceSections_t roles, Elf *elf, int descriptor,
    const TbSymbolCodeLocationsView_t *locations, size_t location_count,
    TbSymbolBudget_t *budget, size_t *source_count, size_t *checksums)
{
    Elf_Data *data = elf_getdata(elf_getscn(elf, roles.line), NULL);
    CHECK(data && data->d_buf && data->d_size < 1024 * 1024);
    TbSymbolLineHeader_t header;
    uint64_t cursor = 0;
    for (;;) {
        CHECK(cursor < data->d_size);
        CHECK(tb_symbol_line_header((TbSymbolDwarfSpan_t) {
            .section_bytes = data->d_size,
            .unit_offset   = cursor,
            .byte_order    = directory.header->byte_order
        }, (const unsigned char *)data->d_buf + cursor, data->d_size - (size_t)cursor, &header) ==
              TB_SYMBOL_DWARF_OK);
        if (cursor == reference.line_offset)
            break;
        CHECK(header.extent.end_offset <= reference.line_offset);
        cursor = header.extent.end_offset;
    }
    size_t capacity = data->d_size + 1;
    TbSymbolLineEntry_t *entries = calloc(capacity, sizeof(*entries));
    TbSymbolLineField_t *fields = calloc(capacity, sizeof(*fields));
    TbSymbolLineRow_t *rows = calloc(capacity, sizeof(*rows));
    TbSymbolLineFile_t *events = calloc(capacity, sizeof(*events));
    CHECK(entries && fields && rows && events);
    TbSymbolLineFiles_t files;
    CHECK(tb_symbol_line_files_init(&files, header, entries, capacity, fields, capacity) ==
          TB_SYMBOL_LINE_FILES_PENDING);
    cursor = header.entries_offset;
    while (files.status == TB_SYMBOL_LINE_FILES_PENDING) {
        size_t consumed;
        uint64_t progress = files.progress;
        (void)tb_symbol_line_files_feed(&files, (const unsigned char *)data->d_buf + cursor,
                                        data->d_size - (size_t)cursor, &consumed);
        CHECK(files.progress > progress && consumed <= 256);
        cursor += consumed;
    }
    TbSymbolLineFilesView_t prologue;
    CHECK(files.status == TB_SYMBOL_LINE_FILES_COMPLETE && tb_symbol_line_files_view(&files, &prologue));
    CHECK(cursor == header.program_offset);
    TbSymbolLine_t line;
    CHECK(tb_symbol_line_init(&line, header, reference.unit.address_bytes,
                              rows, capacity, events, capacity) ==
          TB_SYMBOL_LINE_PENDING);
    while (line.status == TB_SYMBOL_LINE_PENDING) {
        size_t consumed;
        (void)tb_symbol_line_feed(&line, (const unsigned char *)data->d_buf + cursor,
                                  data->d_size - (size_t)cursor, &consumed);
        CHECK(consumed && consumed <= 256);
        cursor += consumed;
    }
    TbSymbolLineView_t program;
    CHECK(line.status == TB_SYMBOL_LINE_COMPLETE && tb_symbol_line_view(&line, &program));
    CHECK(cursor == header.extent.end_offset);
    for (size_t i = 0; i < program.row_count; ++i) {
        TbSymbolSourceFile_t selected;
        TbSymbolSourceFileStatus_e status = tb_symbol_source_file_init(&selected, directory, reference,
            prologue, program, roles, (TbSymbolSourceFileSelection_t) {
                .kind  = TB_SYMBOL_SOURCE_FILE_ROW,
                .index = i
            });
        while (status == TB_SYMBOL_SOURCE_FILE_PENDING)
            status = tb_symbol_source_file_step(&selected, 3);
        if (program.rows[i].end_sequence) {
            CHECK(status == TB_SYMBOL_SOURCE_FILE_ABSENT);
            continue;
        }
        TbSymbolSourceFileView_t source;
        CHECK(status == TB_SYMBOL_SOURCE_FILE_FOUND && tb_symbol_source_file_view(&selected, &source));
        TbSymbolSourcePathRequest_t request = {
            .file              = resolve_text(source.path, directory, descriptor, budget, elf),
            .directory         = resolve_text(source.directory, directory, descriptor, budget, elf),
            .compilation       = header.version == 5 ?
                                  resolve_text(source.compilation, directory, descriptor, budget, elf) :
                                  text_bytes(elf, comp_dir.text),
            .selected_rule     = SIZE_MAX,
            .directory_current = source.directory_current,
            .directory_unknown = !source.directory_available
        };
        unsigned char recorded[4096];
        unsigned char local[4096];
        TbSymbolSourcePath_t path;
        TbSymbolSourcePathStatus_e outcome = tb_symbol_source_path_init(&path, request,
            recorded, sizeof(recorded), local, sizeof(local));
        while (outcome == TB_SYMBOL_SOURCE_PATH_PENDING)
            outcome = tb_symbol_source_path_step(&path, 17);
        TbSymbolSourcePathView_t view;
        CHECK(outcome == TB_SYMBOL_SOURCE_PATH_COMPLETE && tb_symbol_source_path_view(&path, &view));
        CHECK(view.local_available && !view.mapped && view.recorded_bytes == view.local_bytes);
        CHECK(!memcmp(recorded, local, view.local_bytes));
        CHECK(strstr((const char *)local, "/tests/fixtures/native/symbols/"));
        unsigned char alternate_recorded[4096];
        unsigned char alternate_local[4096];
        TbSymbolSourcePath_t alternate_path;
        TbSymbolSourceCandidate_t primary = {
            .path   = view,
            .status = outcome
        };
        TbSymbolSourceCandidate_t alternate = {0};
        if (header.version == 5) {
            request.compilation = text_bytes(elf, comp_dir.text);
            alternate.status = tb_symbol_source_path_init(&alternate_path, request,
                alternate_recorded, sizeof(alternate_recorded), alternate_local, sizeof(alternate_local));
            while (alternate.status == TB_SYMBOL_SOURCE_PATH_PENDING)
                alternate.status = tb_symbol_source_path_step(&alternate_path, 17);
            CHECK(alternate.status == TB_SYMBOL_SOURCE_PATH_COMPLETE &&
                  tb_symbol_source_path_view(&alternate_path, &alternate.path));
        }
        TbSymbolSourceContext_t context;
        TbSymbolSourceContextStatus_e context_status = tb_symbol_source_context_init(&context, source,
            primary, header.version == 5 ? &alternate : NULL, &comp_dir, SIZE_MAX);
        while (context_status == TB_SYMBOL_SOURCE_CONTEXT_PENDING)
            context_status = tb_symbol_source_context_step(&context, 17);
        TbSymbolSourcePrepareRequest_t preparation = {
            .program       = program,
            .path_capacity = 4096,
            .line_limit    = INT_MAX
        };
        CHECK(context_status == TB_SYMBOL_SOURCE_CONTEXT_READY &&
              tb_symbol_source_context_view(&context, &preparation.context));
        TbSymbolSourcePrepare_t prepared;
        TbSymbolSourcePrepareStatus_e prepared_status = tb_symbol_source_prepare_init(&prepared,
            preparation, NULL, 0);
        if (!program.rows[i].line) {
            CHECK(prepared_status == TB_SYMBOL_SOURCE_PREPARE_ABSENT);
        } else {
            while (prepared_status == TB_SYMBOL_SOURCE_PREPARE_PENDING)
                prepared_status = tb_symbol_source_prepare_step(&prepared, 7);
            TbSymbolSourcePrepareView_t prepared_view;
            CHECK(prepared_status == TB_SYMBOL_SOURCE_PREPARE_READY &&
                  tb_symbol_source_prepare_view(&prepared, &prepared_view));
            CHECK(prepared_view.line == program.rows[i].line && !prepared_view.count);
            tb_symbol_source_prepare_discard(&prepared);
            for (size_t k = 0; k < location_count; ++k) {
                preparation.code = &locations[k];
                TbSymbolSourceLocation_t position;
                prepared_status = tb_symbol_source_prepare_init(&prepared, preparation, &position, 1);
                while (prepared_status == TB_SYMBOL_SOURCE_PREPARE_PENDING)
                    prepared_status = tb_symbol_source_prepare_step(&prepared, 7);
                CHECK(prepared_status == TB_SYMBOL_SOURCE_PREPARE_READY &&
                      tb_symbol_source_prepare_view(&prepared, &prepared_view));
                bool inside = program.rows[i].address >= locations[k].low &&
                              program.rows[i].address < locations[k].end;
                CHECK(prepared_view.count == (inside ? 1U : 0U));
                if (inside) {
                    const TbSymbolAttributeResult_t *record =
                        &locations[k].code.fields[TB_SYMBOL_CODE_LOW].selection;
                    CHECK(position.row == i && position.position.address == program.rows[i].address);
                    printf("PREPARED unit=0x%llx die=0x%llx row=%zu address=0x%llx\n",
                           (unsigned long long)reference.source.unit_offset,
                           (unsigned long long)record->record_offset,
                           i, (unsigned long long)position.position.address);
                }
                tb_symbol_source_prepare_discard(&prepared);
            }
        }
        tb_symbol_source_context_discard(&context);
        if (header.version == 5)
            tb_symbol_source_path_discard(&alternate_path);
        printf("SOURCE unit=0x%llx table=0x%llx row=%zu file=%llu entry=0x%llx path=%s\n",
               (unsigned long long)reference.source.unit_offset, (unsigned long long)reference.line_offset,
               i, (unsigned long long)source.file_index, (unsigned long long)source.file_offset, local);
        if (source.md5_available) {
            check_source_digest(elf, roles.line, source, local);
            ++*checksums;
        }
        ++*source_count;
        tb_symbol_source_path_discard(&path);
        tb_symbol_source_file_discard(&selected);
    }
    tb_symbol_line_discard(&line);
    tb_symbol_line_files_discard(&files);
    free(events);
    free(rows);
    free(fields);
    free(entries);
}


static TbSymbolCodeView_t direct_code(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
    uint64_t unit_index, uint64_t die_index, const TbSymbolAbbrev_t *abbrev)
{
    TbSymbolCode_t code;
    TbSymbolCodeStatus_e status = tb_symbol_code_init(&code, units, tree, (TbSymbolCodeSelection_t) {
        .unit_index = unit_index,
        .die_index  = die_index
    });
    while (status == TB_SYMBOL_CODE_PENDING || status == TB_SYMBOL_CODE_SCHEMA) {
        if (status == TB_SYMBOL_CODE_SCHEMA) {
            TbSymbolAbbrevView_t schema;
            CHECK(tb_symbol_abbrev_view(abbrev, &schema));
            status = tb_symbol_code_schema(&code, &schema);
        } else {
            status = tb_symbol_code_step(&code, 17);
        }
    }
    TbSymbolCodeView_t view;
    CHECK(status == TB_SYMBOL_CODE_COMPLETE && tb_symbol_code_view(&code, &view));
    tb_symbol_code_discard(&code);
    return view;
}


int main(int argc, char **argv)
{
    CHECK((argc == 2 || (argc == 3 && !strcmp(argv[2], "--md5"))) && elf_version(EV_CURRENT) != EV_NONE);
    char temporary[] = "/tmp/tb-symbol-source-native-XXXXXX";
    int descriptor = frozen_fixture(argv[1], temporary);
    struct stat info;
    CHECK(fstat(descriptor, &info) == 0 && info.st_size > 0);
    Elf *elf = elf_begin(descriptor, ELF_C_READ, NULL);
    CHECK(elf);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 4 * 1024 * 1024));
    TbSymbolSections_t *sections = tb_symbol_sections_create((TbSymbolStorageInfo_t) {
        .key = {
            .device = (uint64_t)info.st_dev,
            .inode  = (uint64_t)info.st_ino
        },
        .bytes = (uint64_t)info.st_size
    }, descriptor, &budget);
    CHECK(sections);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    while (outcome == TB_SYMBOL_PENDING)
        outcome = tb_symbol_sections_step(sections, 257);
    TbSymbolSectionsView_t directory;
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(sections, &directory));
    TbSymbolSourceSections_t roles = {
        .line         = section_index(elf, ".debug_line"),
        .strings      = section_index(elf, ".debug_str"),
        .line_strings = section_index(elf, ".debug_line_str")
    };
    TbSymbolUnits_t *units = tb_symbol_units_create(directory, (TbSymbolUnitSelection_t) {
        .section_index = section_index(elf, ".debug_info"),
        .kind          = TB_SYMBOL_DWARF_INFO,
        .descriptor    = descriptor
    }, &budget);
    CHECK(units);
    outcome = TB_SYMBOL_PENDING;
    while (outcome == TB_SYMBOL_PENDING)
        outcome = tb_symbol_units_step(units, 257);
    TbSymbolUnitsView_t inventory;
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_units_view(units, &inventory));
    size_t sources = 0;
    size_t checksums = 0;
    for (uint64_t i = 0; i < inventory.count; ++i) {
        CHECK(inventory.entries[i].header_available);
        TbSymbolAbbrev_t *abbrev = tb_symbol_abbrev_create(directory, (TbSymbolAbbrevSelection_t) {
            .section_index = section_index(elf, ".debug_abbrev"),
            .table_offset  = inventory.entries[i].header.abbrev_offset,
            .version       = inventory.entries[i].header.version,
            .descriptor    = descriptor
        }, &budget);
        CHECK(abbrev);
        outcome = TB_SYMBOL_PENDING;
        while (outcome == TB_SYMBOL_PENDING)
            outcome = tb_symbol_abbrev_step(abbrev, 257);
        CHECK(outcome == TB_SYMBOL_SUCCESS);
        TbSymbolDies_t *dies = tb_symbol_dies_create(directory, inventory, (TbSymbolDieSelection_t) {
            .unit_index = i,
            .descriptor = descriptor
        }, abbrev, &budget);
        CHECK(dies);
        outcome = TB_SYMBOL_PENDING;
        while (outcome == TB_SYMBOL_PENDING)
            outcome = tb_symbol_dies_step(dies, 257);
        TbSymbolDiesView_t tree;
        CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_dies_view(dies, &tree));
        TbSymbolLineReference_t reference;
        TbSymbolLineReferenceStatus_e status = tb_symbol_line_reference_init(&reference, inventory, tree, i);
        while (status == TB_SYMBOL_LINE_REFERENCE_PENDING)
            status = tb_symbol_line_reference_step(&reference, 17);
        TbSymbolLineReferenceView_t selected;
        CHECK(status == TB_SYMBOL_LINE_REFERENCE_FOUND &&
              tb_symbol_line_reference_view(&reference, &selected));
        TbSymbolText_t *directory_text = tb_symbol_text_create(directory, inventory, tree,
            (TbSymbolTextSelection_t) {
                .unit_index          = i,
                .name                = 0x1b,
                .string_section      = roles.strings,
                .line_string_section = roles.line_strings,
                .offset_section      = section_index(elf, ".debug_str_offsets"),
                .descriptor          = descriptor
            }, &budget);
        CHECK(directory_text);
        TbSymbolTextOutcome_e text_status = TB_SYMBOL_TEXT_PENDING;
        while (text_status == TB_SYMBOL_TEXT_PENDING)
            text_status = tb_symbol_text_step(directory_text, 257);
        TbSymbolTextView_t comp_dir;
        CHECK(text_status == TB_SYMBOL_TEXT_FOUND && tb_symbol_text_view(directory_text, &comp_dir));
        CHECK(comp_dir.source_unit == selected.source.unit_offset &&
              comp_dir.source_record == tree.entries[0].offset);
        TbSymbolCodeView_t root = direct_code(inventory, tree, i, 0, abbrev);
        CHECK(tree.count <= SIZE_MAX / sizeof(TbSymbolCodeLocationsView_t));
        TbSymbolCodeLocationsView_t *locations = calloc((size_t)tree.count, sizeof(*locations));
        CHECK(locations);
        size_t location_count = 0;
        for (uint64_t die = 1; die < tree.count; ++die) {
            if (tree.entries[die].tag != 0x2e)
                continue;

            TbSymbolCodeView_t function = direct_code(inventory, tree, i, die, abbrev);
            /* Indexed/disjoint paths have their full separate native harness;
             * this composition check adds exact direct function-row binding. */
            if (!function.interval_available || root.fields[TB_SYMBOL_CODE_LOW].kind ==
                                                TB_SYMBOL_CODE_ADDRESS_INDEX)
                continue;

            TbSymbolCodeLocationsStatus_e located = tb_symbol_code_locations_prepare(
                (TbSymbolCodeLocationsRequest_t) {
                    .code       = function,
                    .root       = root,
                    .byte_order = directory.header->byte_order
                }, &locations[location_count]);
            CHECK(located == TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
            CHECK(locations[location_count].interval_available && !locations[location_count].list_available);
            ++location_count;
        }
        check_line(directory, selected, comp_dir, roles, elf, descriptor, locations, location_count,
                   &budget, &sources, &checksums);
        free(locations);
        tb_symbol_text_destroy(directory_text);
        tb_symbol_line_reference_discard(&reference);
        tb_symbol_dies_destroy(dies);
        tb_symbol_abbrev_destroy(abbrev);
    }
    CHECK(sources && (argc == 2 || checksums));
    tb_symbol_units_destroy(units);
    tb_symbol_sections_destroy(sections);
    CHECK(budget.used == 0 && elf_end(elf) == 0 && close(descriptor) == 0 && unlink(temporary) == 0);
    printf("PASS source provenance: %zu rows, %zu recorded MD5 matches and changed-byte mismatches\n",
           sources, checksums);
    return 0;
}
