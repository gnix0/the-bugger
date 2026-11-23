#define _POSIX_C_SOURCE 200809L
#include "symbol/line_files.h"
#include "symbol/line_reference.h"
#include "symbol/strings.h"
#include "symbol/ranges.h"
#include "../support/check.h"

#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static Elf_Data *section(Elf *elf, const char *name)
{
    size_t strings;
    CHECK(elf_getshdrstrndx(elf, &strings) == 0);
    Elf_Data *found = NULL;
    Elf_Scn *current = NULL;
    while ((current = elf_nextscn(elf, current))) {
        GElf_Shdr header;
        CHECK(gelf_getshdr(current, &header));
        const char *text = elf_strptr(elf, strings, header.sh_name);
        CHECK(text);
        if (strcmp(text, name))
            continue;
        CHECK(!found && !(header.sh_flags & SHF_COMPRESSED));
        found = elf_getdata(current, NULL);
        CHECK(found && found->d_off == 0 && found->d_size == header.sh_size);
        CHECK(!elf_getdata(current, found));
    }

    CHECK(found && found->d_buf && found->d_size);
    return found;
}


typedef struct {
    TbSymbolAttributeResult_t source;
    TbSymbolRangesRequest_t   request;
    TbSymbolRange_t           intervals[16];
    size_t                    count;
    uint64_t                  line_offset;
} RangeFixture_t;

typedef struct {
    TbSymbolBudget_t              budget;
    TbSymbolSections_t           *sections;
    TbSymbolSectionsView_t        directory;
    TbSymbolLineReferenceView_t   references[16];
    size_t                        reference_count;
    RangeFixture_t                ranges[16];
    size_t                        range_count;
    uint64_t                      line_section;
    uint64_t                      string_section;
    uint64_t                      line_string_section;
} SourceFixture_t;

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
        if (strcmp(text, name))
            continue;
        CHECK(found == UINT64_MAX);
        found = elf_ndxscn(current);
    }

    return found;
}


static const TbSymbolDieAttribute_t *root_attribute(TbSymbolDiesView_t tree, uint64_t name,
                                                    TbSymbolAttributeResult_t *scope)
{
    TbSymbolAttributes_t lookup;
    CHECK(tb_symbol_attributes_init(&lookup, tree, (TbSymbolAttributeSelection_t) {
        .die_index = 0,
        .name      = name
    }) == TB_SYMBOL_ATTRIBUTE_PENDING);
    TbSymbolAttributeStatus_e status = TB_SYMBOL_ATTRIBUTE_PENDING;
    while (status == TB_SYMBOL_ATTRIBUTE_PENDING)
        status = tb_symbol_attributes_step(&lookup, 31);
    CHECK(status == TB_SYMBOL_ATTRIBUTE_UNIQUE || status == TB_SYMBOL_ATTRIBUTE_MISSING);
    CHECK(tb_symbol_attributes_result(&lookup, scope) == status);
    return status == TB_SYMBOL_ATTRIBUTE_UNIQUE ? &tree.attributes[scope->first_index] : NULL;
}


static void source_fixture(SourceFixture_t *fixture, Elf *elf, int descriptor)
{
    struct stat info;
    CHECK(fstat(descriptor, &info) == 0 && info.st_size > 0);
    *fixture = (SourceFixture_t) {
        .line_section        = section_index(elf, ".debug_line"),
        .string_section      = section_index(elf, ".debug_str"),
        .line_string_section = section_index(elf, ".debug_line_str")
    };
    CHECK(tb_symbol_budget_init(&fixture->budget, 1024 * 1024));
    fixture->sections = tb_symbol_sections_create((TbSymbolStorageInfo_t) {
        .key = {
            .device = (uint64_t)info.st_dev,
            .inode  = (uint64_t)info.st_ino
        },
        .bytes = (uint64_t)info.st_size
    }, descriptor, &fixture->budget);
    CHECK(fixture->sections);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    while (outcome == TB_SYMBOL_PENDING)
        outcome = tb_symbol_sections_step(fixture->sections, 257);
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(fixture->sections, &fixture->directory));
    TbSymbolUnits_t *units = tb_symbol_units_create(fixture->directory, (TbSymbolUnitSelection_t) {
        .section_index = section_index(elf, ".debug_info"),
        .kind          = TB_SYMBOL_DWARF_INFO,
        .descriptor    = descriptor
    }, &fixture->budget);
    CHECK(units);
    outcome = TB_SYMBOL_PENDING;
    while (outcome == TB_SYMBOL_PENDING)
        outcome = tb_symbol_units_step(units, 257);
    TbSymbolUnitsView_t inventory;
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_units_view(units, &inventory));
    CHECK(inventory.count && inventory.count <= 16);

    for (uint64_t i = 0; i < inventory.count; ++i) {
        CHECK(inventory.entries[i].header_available);
        TbSymbolAbbrev_t *abbrev = tb_symbol_abbrev_create(fixture->directory, (TbSymbolAbbrevSelection_t) {
            .section_index = section_index(elf, ".debug_abbrev"),
            .table_offset  = inventory.entries[i].header.abbrev_offset,
            .version       = inventory.entries[i].header.version,
            .descriptor    = descriptor
        }, &fixture->budget);
        CHECK(abbrev);
        outcome = TB_SYMBOL_PENDING;
        while (outcome == TB_SYMBOL_PENDING)
            outcome = tb_symbol_abbrev_step(abbrev, 257);
        CHECK(outcome == TB_SYMBOL_SUCCESS);
        TbSymbolDies_t *dies = tb_symbol_dies_create(fixture->directory, inventory,
            (TbSymbolDieSelection_t) {
                .unit_index = i,
                .descriptor = descriptor
            }, abbrev, &fixture->budget);
        CHECK(dies);
        outcome = TB_SYMBOL_PENDING;
        while (outcome == TB_SYMBOL_PENDING)
            outcome = tb_symbol_dies_step(dies, 257);
        TbSymbolDiesView_t tree;
        CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_dies_view(dies, &tree));
        TbSymbolLineReference_t reference;
        CHECK(tb_symbol_line_reference_init(&reference, inventory, tree, i) ==
              TB_SYMBOL_LINE_REFERENCE_PENDING);
        TbSymbolLineReferenceStatus_e status = TB_SYMBOL_LINE_REFERENCE_PENDING;
        while (status == TB_SYMBOL_LINE_REFERENCE_PENDING)
            status = tb_symbol_line_reference_step(&reference, 31);
        CHECK(status == TB_SYMBOL_LINE_REFERENCE_FOUND);
        CHECK(tb_symbol_line_reference_view(&reference, &fixture->references[fixture->reference_count++]));
        TbSymbolAttributeResult_t range_scope;
        const TbSymbolDieAttribute_t *range = root_attribute(tree, 0x55, &range_scope);
        if (range) {
            CHECK(fixture->range_count < 16);
            CHECK(range->value.form == TB_SYMBOL_FORM_SEC_OFFSET && range->value.numeric);
            TbSymbolAttributeResult_t base_scope;
            const TbSymbolDieAttribute_t *base = root_attribute(tree, 0x11, &base_scope);
            CHECK(base && base->value.form == TB_SYMBOL_FORM_ADDR && base->value.numeric);
            fixture->ranges[fixture->range_count++] = (RangeFixture_t) {
                .source = range_scope,
                .request = {
                    .offset         = range->value.bits,
                    .base           = base->value.bits,
                    .byte_order     = fixture->directory.header->byte_order,
                    .version        = inventory.entries[i].header.version,
                    .address_bytes  = inventory.entries[i].header.address_bytes,
                    .base_available = true
                },
                .line_offset = fixture->references[fixture->reference_count - 1].line_offset
            };
        }

        tb_symbol_line_reference_discard(&reference);
        tb_symbol_dies_destroy(dies);
        tb_symbol_abbrev_destroy(abbrev);
    }

    tb_symbol_units_destroy(units);
}


static void check_range_storage(Elf *elf, TbSymbolRange_t range)
{
    if (range.begin == range.end)
        return;

    size_t count;
    CHECK(elf_getphdrnum(elf, &count) == 0);
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        GElf_Phdr segment;
        CHECK(gelf_getphdr(elf, (int)i, &segment));
        uint64_t end;
        CHECK(tb_symbol_add(segment.p_vaddr, segment.p_filesz, &end));
        if (segment.p_type == PT_LOAD && (segment.p_flags & PF_X) &&
            range.begin >= segment.p_vaddr && range.end <= end) {
            found = true;
        }
    }
    CHECK(found);
}


static void check_range_lists(SourceFixture_t *fixture, Elf *elf, unsigned version)
{
    CHECK(fixture->range_count);
    Elf_Data *data = section(elf, version == 4 ? ".debug_ranges" : ".debug_rnglists");
    CHECK(data->d_size < 1024 * 1024);
    size_t capacity = data->d_size + 1;
    TbSymbolRangeRecord_t *records = calloc(capacity, sizeof(*records));
    TbSymbolRange_t *intervals = calloc(capacity, sizeof(*intervals));
    CHECK(records && intervals);
    uint64_t offset = 0;
    size_t lists = 0;
    size_t count = 0;
    while (offset < data->d_size) {
        uint64_t limit = data->d_size;
        unsigned contribution_width = 0;
        if (version == 5) {
            TbSymbolIndexedTable_t table;
            CHECK(tb_symbol_indexed_table_header(TB_SYMBOL_TABLE_RANGES, (TbSymbolDwarfSpan_t) {
                .section_bytes = data->d_size,
                .unit_offset   = offset,
                .byte_order    = fixture->directory.header->byte_order
            }, (const unsigned char *)data->d_buf + offset, data->d_size - (size_t)offset, &table) ==
                  TB_SYMBOL_DWARF_OK);
            /* This GCC profile uses sec_offset and no offset array. Indexed
             * address/range forms have separate authored coverage. */
            CHECK(table.count == 0 && table.segment_bytes == 0);
            contribution_width = table.address_bytes;
            offset = table.lists_offset;
            limit = table.extent.end_offset;
        }
        while (offset < limit) {
            RangeFixture_t *selected = NULL;
            for (size_t i = 0; i < fixture->range_count; ++i) {
                if (fixture->ranges[i].request.offset == offset) {
                    CHECK(!selected);
                    selected = &fixture->ranges[i];
                }
            }
            CHECK(selected);
            CHECK(version == 4 || selected->request.address_bytes == contribution_width);
            selected->request.end_offset = limit;
            TbSymbolRanges_t ranges;
            CHECK(tb_symbol_ranges_init(&ranges, selected->request, records, capacity,
                  intervals, capacity) == TB_SYMBOL_RANGES_PENDING);
            size_t cursor = (size_t)offset;
            while (ranges.status == TB_SYMBOL_RANGES_PENDING) {
                size_t consumed;
                uint64_t before = ranges.progress;
                (void)tb_symbol_ranges_feed(&ranges, (const unsigned char *)data->d_buf + cursor,
                                             (size_t)limit - cursor, &consumed);
                CHECK(consumed <= 256 && (consumed || ranges.progress > before));
                cursor += consumed;
            }
            TbSymbolRangesView_t view;
            CHECK(ranges.status == TB_SYMBOL_RANGES_COMPLETE && tb_symbol_ranges_view(&ranges, &view));
            CHECK(view.end_offset == cursor && view.range_count && view.range_count <= 16);
            selected->count = view.range_count;
            for (size_t i = 0; i < view.range_count; ++i) {
                selected->intervals[i] = view.ranges[i];
                check_range_storage(elf, view.ranges[i]);
                printf("RANGE v%u list=0x%llx begin=0x%llx end=0x%llx\n", version,
                       (unsigned long long)offset, (unsigned long long)view.ranges[i].begin,
                       (unsigned long long)view.ranges[i].end);
            }
            count += view.range_count;
            ++lists;
            offset = view.end_offset;
            tb_symbol_ranges_discard(&ranges);
            if (version == 4)
                break;
        }
    }
    CHECK(lists == fixture->range_count);
    printf("PASS native v%u ranges: %zu CU lists, %zu intervals in executable file-backed segments\n",
           version, lists, count);
    free(intervals);
    free(records);
}


static size_t check_prologue(SourceFixture_t *fixture, int descriptor, Elf *elf, Elf_Data *data,
                             TbSymbolLineHeader_t header)
{
    size_t capacity = data->d_size + 1;
    TbSymbolLineEntry_t *entries = calloc(capacity, sizeof(*entries));
    TbSymbolLineField_t *fields = calloc(capacity, sizeof(*fields));
    TbSymbolLineFiles_t files;
    CHECK(entries && fields);
    CHECK(tb_symbol_line_files_init(&files, header, entries, capacity, fields, capacity) ==
          TB_SYMBOL_LINE_FILES_PENDING);
    size_t cursor = (size_t)header.entries_offset;
    while (files.status == TB_SYMBOL_LINE_FILES_PENDING) {
        size_t consumed;
        uint64_t before = files.progress;
        (void)tb_symbol_line_files_feed(&files, (const unsigned char *)data->d_buf + cursor,
                                        data->d_size - cursor, &consumed);
        CHECK(consumed <= 256 && (consumed || files.progress > before));
        cursor += consumed;
    }
    TbSymbolLineFilesView_t view;
    CHECK(files.status == TB_SYMBOL_LINE_FILES_COMPLETE && cursor == header.program_offset);
    CHECK(tb_symbol_line_files_view(&files, &view) && view.file_count);
    for (size_t i = 0; i < view.directory_count + view.file_count; ++i) {
        const TbSymbolLineEntry_t *entry = &view.entries[i];
        CHECK(entry->directory == (i < view.directory_count));
        CHECK(entry->index == i - (entry->directory ? 0 : view.directory_count) +
                             (header.version < 5 ? 1 : 0));
        for (unsigned j = 0; j < entry->field_count; ++j) {
            const TbSymbolLineField_t *field = &view.fields[entry->first_field + j];
            if (field->content != 1)
                continue;
            uint64_t selected = fixture->line_section;
            uint64_t offset = field->offset + field->value.payload_offset;
            uint64_t end = offset + field->value.payload_bytes + 1;
            if (field->value.form != TB_SYMBOL_FORM_STRING) {
                CHECK(field->value.form == TB_SYMBOL_FORM_STRP ||
                      field->value.form == TB_SYMBOL_FORM_LINE_STRP);
                selected = field->value.form == TB_SYMBOL_FORM_STRP ? fixture->string_section :
                                                                    fixture->line_string_section;
                CHECK(selected < fixture->directory.count);
                offset = field->value.bits;
                end = fixture->directory.entries[selected].header.bytes;
            }
            CHECK(selected < fixture->directory.count);
            TbSymbolString_t *string = tb_symbol_string_create(fixture->directory,
                (TbSymbolStringSelection_t) {
                    .section_index = selected,
                    .offset        = offset,
                    .end_offset    = end,
                    .descriptor    = descriptor
                }, &fixture->budget);
            CHECK(string);
            TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
            while (outcome == TB_SYMBOL_PENDING)
                outcome = tb_symbol_string_step(string, 257);
            TbSymbolStringView_t text;
            CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_string_view(string, &text));
            Elf_Data *expected = elf_getdata(elf_getscn(elf, selected), NULL);
            CHECK(expected && offset < expected->d_size);
            const char *raw = (const char *)expected->d_buf + offset;
            const char *nul = memchr(raw, 0, expected->d_size - (size_t)offset);
            CHECK(nul && text.bytes == (uint64_t)(nul - raw));
            tb_symbol_string_destroy(string);
        }
    }

    size_t count = view.file_count;
    tb_symbol_line_files_discard(&files);
    free(fields);
    free(entries);
    return count;
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
    CHECK(count == 0 && close(input) == 0);
    CHECK(fchmod(writer, 0400) == 0 && close(writer) == 0);
    int reader = open(temporary, O_RDONLY | O_CLOEXEC);
    CHECK(reader >= 0);
    return reader;
}


static void check_file(const char *path, unsigned expected_version, bool require_disjoint)
{
    char temporary[] = "/tmp/tb-symbol-line-XXXXXX";
    int descriptor = frozen_fixture(path, temporary);
    CHECK(descriptor >= 0);
    Elf *elf = elf_begin(descriptor, ELF_C_READ, NULL);
    CHECK(elf);
    GElf_Ehdr container;
    CHECK(gelf_getehdr(elf, &container));
    CHECK(container.e_type == ET_EXEC || container.e_type == ET_DYN);
    CHECK(container.e_ident[EI_DATA] == ELFDATA2LSB || container.e_ident[EI_DATA] == ELFDATA2MSB);
    TbSymbolByteOrder_e order = container.e_ident[EI_DATA] == ELFDATA2LSB ?
                                   TB_SYMBOL_LITTLE_ENDIAN : TB_SYMBOL_BIG_ENDIAN;
    Elf_Data *info = section(elf, ".debug_info");
    TbSymbolDwarfUnit_t unit;
    CHECK(tb_symbol_dwarf_unit(TB_SYMBOL_DWARF_INFO, (TbSymbolDwarfSpan_t) {
        .section_bytes = info->d_size,
        .byte_order    = order
    }, info->d_buf, info->d_size, &unit) == TB_SYMBOL_DWARF_OK);
    CHECK(unit.address_bytes == 4 || unit.address_bytes == 8);

    Elf_Data *data = section(elf, ".debug_line");
    CHECK(data->d_size < 1024 * 1024);
    /* These maintained native fixtures use a uniform CU address width, checked
     * against every CU header below. This is a test profile, not a production
     * rule relating ELF class, target addresses and arbitrary line tables. */
    for (uint64_t offset = 0; offset < info->d_size;) {
        TbSymbolDwarfUnit_t other;
        CHECK(tb_symbol_dwarf_unit(TB_SYMBOL_DWARF_INFO, (TbSymbolDwarfSpan_t) {
            .section_bytes = info->d_size,
            .unit_offset   = offset,
            .byte_order    = order
        }, (const unsigned char *)info->d_buf + offset, info->d_size - (size_t)offset, &other) ==
              TB_SYMBOL_DWARF_OK);
        CHECK(other.address_bytes == unit.address_bytes);
        offset = other.extent.end_offset;
    }

    SourceFixture_t fixture;
    source_fixture(&fixture, elf, descriptor);
    check_range_lists(&fixture, elf, expected_version);
    if (require_disjoint) {
        bool disjoint = false;
        for (size_t i = 0; i < fixture.range_count; ++i) {
            if (fixture.ranges[i].count > 1)
                disjoint = true;
        }
        CHECK(disjoint);
    }
    size_t matched_references = 0;
    size_t capacity = data->d_size + 1;
    TbSymbolLineRow_t *rows = calloc(capacity, sizeof(*rows));
    TbSymbolLineFile_t *files = calloc(capacity, sizeof(*files));
    CHECK(rows && files);
    size_t table_count = 0;
    size_t row_count = 0;
    size_t sequence_count = 0;
    bool main_seen = false;
    uint64_t main_address = 0;
    Elf_Data *symbols = section(elf, ".symtab");
    size_t symbol_count = symbols->d_size / gelf_fsize(elf, ELF_T_SYM, 1, EV_CURRENT);
    size_t string_index = 0;
    Elf_Scn *scan = NULL;
    while ((scan = elf_nextscn(elf, scan))) {
        GElf_Shdr header;
        CHECK(gelf_getshdr(scan, &header));
        if (header.sh_type == SHT_SYMTAB)
            string_index = header.sh_link;
    }
    CHECK(string_index);
    for (size_t i = 0; i < symbol_count; ++i) {
        GElf_Sym symbol;
        CHECK(gelf_getsym(symbols, (int)i, &symbol));
        const char *name = elf_strptr(elf, string_index, symbol.st_name);
        CHECK(name);
        if (!strcmp(name, "main")) {
            CHECK(symbol.st_shndx != SHN_UNDEF && symbol.st_value);
            main_address = symbol.st_value;
        }
    }
    CHECK(main_address);

    for (uint64_t offset = 0; offset < data->d_size;) {
        TbSymbolLineHeader_t header;
        CHECK(tb_symbol_line_header((TbSymbolDwarfSpan_t) {
            .section_bytes = data->d_size,
            .unit_offset   = offset,
            .byte_order    = order
        }, (const unsigned char *)data->d_buf + offset, data->d_size - (size_t)offset, &header) ==
              TB_SYMBOL_DWARF_OK);
        CHECK(header.version == expected_version);
        size_t initial_files = check_prologue(&fixture, descriptor, elf, data, header);
        size_t matching = 0;
        for (size_t i = 0; i < fixture.reference_count; ++i) {
            if (fixture.references[i].line_offset != offset)
                continue;
            CHECK(fixture.references[i].unit.address_bytes == unit.address_bytes);
            CHECK(fixture.references[i].source.backing.key.inode == fixture.directory.backing.key.inode);
            ++matching;
        }
        CHECK(matching);
        matched_references += matching;
        TbSymbolLine_t line;
        CHECK(tb_symbol_line_init(&line, header, unit.address_bytes, rows, capacity, files, capacity) ==
              TB_SYMBOL_LINE_PENDING);
        size_t cursor = (size_t)header.program_offset;
        while (line.status == TB_SYMBOL_LINE_PENDING) {
            size_t consumed;
            (void)tb_symbol_line_feed(&line, (const unsigned char *)data->d_buf + cursor,
                                      data->d_size - cursor, &consumed);
            CHECK(consumed && consumed <= 256);
            cursor += consumed;
        }
        CHECK(line.status == TB_SYMBOL_LINE_COMPLETE && cursor == header.extent.end_offset);
        TbSymbolLineView_t view;
        CHECK(tb_symbol_line_view(&line, &view) && view.row_count);
        CHECK(view.rows[view.row_count - 1].end_sequence);
        for (size_t i = 0; i < view.row_count; ++i) {
            if (view.rows[i].end_sequence) {
                ++sequence_count;
            } else {
                CHECK(view.rows[i].file >= (header.version < 5 ? 1U : 0U));
                size_t available_files = initial_files;
                for (size_t event = 0; event < view.file_count; ++event) {
                    if (view.files[event].instruction_offset < view.rows[i].instruction_offset)
                        ++available_files;
                }
                CHECK(view.rows[i].file - (header.version < 5 ? 1U : 0U) < available_files);
                for (size_t group = 0; group < fixture.range_count; ++group) {
                    const RangeFixture_t *selected = &fixture.ranges[group];
                    if (selected->line_offset != offset)
                        continue;
                    bool covered = false;
                    for (size_t member = 0; member < selected->count; ++member) {
                        if (view.rows[i].address >= selected->intervals[member].begin &&
                            view.rows[i].address < selected->intervals[member].end) {
                            covered = true;
                        }
                    }
                    CHECK(covered);
                }
            }
            if (!view.rows[i].end_sequence && view.rows[i].address == main_address)
                main_seen = true;
        }
        row_count += view.row_count;
        ++table_count;
        tb_symbol_line_discard(&line);
        offset = header.extent.end_offset;
    }
    CHECK(table_count && row_count > table_count && sequence_count && main_seen);
    CHECK(matched_references == fixture.reference_count);
    tb_symbol_sections_destroy(fixture.sections);
    CHECK(fixture.budget.used == 0);
    printf("PASS native line programs v%u: %zu tables, %zu rows, %zu sequences, "
           "exact main address, validated files/CU references\n",
           expected_version, table_count, row_count, sequence_count);

    free(files);
    free(rows);
    CHECK(elf_end(elf) == 0);
    CHECK(close(descriptor) == 0 && unlink(temporary) == 0);
}


int main(int argc, char **argv)
{
    CHECK((argc == 3 || argc == 4) && elf_version(EV_CURRENT) != EV_NONE);
    if (argc == 4)
        CHECK(!strcmp(argv[3], "--disjoint"));
    check_file(argv[1], 4, argc == 4);
    check_file(argv[2], 5, argc == 4);
    return 0;
}
