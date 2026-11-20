#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Native file fixture reads. */
#include "symbol/abbrev.h"
#include "symbol/elf.h"
#include "symbol/file.h"
#include "symbol/sections.h"
#include "symbol/symbols.h"
#include "symbol/units.h"
#include "symbol/acquire.h"
#include "symbol/admission.h"
#include "../support/check.h"

#include <gelf.h>
#include <libelf.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static TbSymbolBudget_t budget;
static TbSymbolAdmissionLedger_t *ledger;
static TbSymbolStorage_t *storage;
static uint64_t next_slot;

static void read_record(TbSymbolFile_t *file, uint64_t offset, size_t length, unsigned char *bytes)
{
    CHECK(length <= 64);
    CHECK(tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = offset,
        .bytes  = length
    }));
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 128 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_file_step(file, 31);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolFileView_t view;
    CHECK(tb_symbol_file_view(file, &view) && view.offset == offset && view.bytes == length);
    memcpy(bytes, view.data, length);
}


typedef struct {
    uint64_t total;
    uint64_t unsupported;
    uint64_t abbreviations;
} UnitCounts_t;

static UnitCounts_t check_units(TbSymbolSectionsView_t directory, TbSymbolUnitSelection_t selection,
                                uint64_t abbreviation_section)
{
    TbSymbolUnits_t *units = tb_symbol_units_create(directory, selection, &budget);
    CHECK(units);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 1000000 && outcome == TB_SYMBOL_PENDING; ++turn)
        outcome = tb_symbol_units_step(units, 257);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolUnitsView_t view;
    CHECK(tb_symbol_units_view(units, &view));
    CHECK(view.section_index == selection.section_index && view.kind == selection.kind);
    uint64_t cursor = 0;
    uint64_t unsupported = 0;
    uint64_t abbreviations = 0;
    for (uint64_t i = 0; i < view.count; ++i) {
        const TbSymbolUnitEntry_t *entry = &view.entries[i];
        CHECK(entry->offset == cursor && entry->extent.total_bytes);
        if (entry->header_available) {
            CHECK(entry->header.extent.end_offset == entry->extent.end_offset);
            CHECK(entry->header.version == 4 || entry->header.version == 5);
            CHECK(entry->header.header_bytes < entry->extent.total_bytes);
            CHECK(abbreviation_section < directory.count);
            TbSymbolAbbrev_t *abbrev = tb_symbol_abbrev_create(directory, (TbSymbolAbbrevSelection_t){
                .section_index = abbreviation_section,
                .table_offset  = entry->header.abbrev_offset,
                .version       = entry->header.version,
                .descriptor    = selection.descriptor
            }, &budget);
            CHECK(abbrev);
            TbSymbolOutcome_e decoded = TB_SYMBOL_PENDING;
            for (unsigned turn = 0; turn < 1000000 && decoded == TB_SYMBOL_PENDING; ++turn)
                decoded = tb_symbol_abbrev_step(abbrev, 257);
            CHECK(decoded == TB_SYMBOL_SUCCESS);
            TbSymbolAbbrevView_t table;
            CHECK(tb_symbol_abbrev_view(abbrev, &table));
            CHECK(table.table_offset == entry->header.abbrev_offset && table.count);
            for (uint64_t code = 0; code < table.count; ++code) {
                const TbSymbolAbbrevEntry_t *found = NULL;
                CHECK(tb_symbol_abbrev_find(abbrev, table.entries[code].code, &found) ==
                      TB_SYMBOL_ABBREV_FOUND);
                CHECK(found == &table.entries[code]);
            }
            abbreviations += table.count;
            tb_symbol_abbrev_destroy(abbrev);
        } else {
            ++unsupported;
            CHECK(!entry->header.version && !entry->header.header_bytes);
        }
        cursor = entry->extent.end_offset;
    }
    CHECK(cursor == directory.entries[selection.section_index].header.bytes);
    CHECK(unsupported == view.unsupported);
    UnitCounts_t counts = {
        .total       = view.count,
        .unsupported = view.unsupported,
        .abbreviations = abbreviations
    };
    tb_symbol_units_destroy(units);
    return counts;
}


static void compare_section(TbSymbolElfSection_t actual, GElf_Shdr expected)
{
    CHECK(actual.name_offset == expected.sh_name && actual.type == expected.sh_type);
    CHECK(actual.flags == expected.sh_flags && actual.address == expected.sh_addr);
    CHECK(actual.offset == expected.sh_offset && actual.bytes == expected.sh_size);
    CHECK(actual.link == expected.sh_link && actual.info == expected.sh_info);
    CHECK(actual.alignment == expected.sh_addralign && actual.entry_bytes == expected.sh_entsize);
}


static void check_file(const char *path)
{
    TbSymbolReservation_t *ticket = tb_symbol_admission_prepare(ledger);
    CHECK(ticket);
    TbSymbolAcquire_t *acquisition = tb_symbol_acquire_create(path, storage, ++next_slot,
                                                             TB_SYMBOL_FILE_BYTES_DEFAULT, &budget);
    CHECK(acquisition);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 100000 && outcome == TB_SYMBOL_PENDING; ++i) {
        uint64_t offered;
        if (tb_symbol_acquire_offer(acquisition, &offered)) {
            CHECK(tb_symbol_admission_assign(ticket, offered));
            CHECK(tb_symbol_acquire_grant(acquisition, offered));
        }
        outcome = tb_symbol_acquire_step(acquisition, 65536);
    }
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolAcquiredBytes_t acquired;
    CHECK(tb_symbol_acquire_take(acquisition, &acquired));
    tb_symbol_acquire_destroy(acquisition);
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(ticket, NULL);
    CHECK(backing);
    int fd = acquired.descriptor;
    TbSymbolFile_t *input = tb_symbol_file_create((TbSymbolStorageInfo_t){
        .key   = acquired.key,
        .bytes = acquired.bytes
    }, fd, &budget);
    CHECK(input);
    TbSymbolSections_t *directory = tb_symbol_sections_create((TbSymbolStorageInfo_t){
        .key   = acquired.key,
        .bytes = acquired.bytes
    }, fd, &budget);
    CHECK(directory);
    outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_sections_step(directory, 257);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolSectionsView_t directory_view;
    CHECK(tb_symbol_sections_view(directory, &directory_view));
    CHECK(directory_view.names == TB_SYMBOL_SECTION_NAMES_AVAILABLE);
    unsigned char bytes[64];
    read_record(input, 0, sizeof(bytes), bytes);
    TbSymbolElfHeader_t header;
    CHECK(tb_symbol_elf_header(acquired.bytes, bytes, sizeof(bytes), &header) == TB_SYMBOL_ELF_OK);
    Elf *reference = elf_begin(fd, ELF_C_READ, NULL);
    CHECK(reference);
    GElf_Ehdr expected_header;
    CHECK(gelf_getehdr(reference, &expected_header));
    CHECK(header.address_bytes == (gelf_getclass(reference) == ELFCLASS32 ? 4U : 8U));
    CHECK(header.os_abi == expected_header.e_ident[EI_OSABI]);
    CHECK(header.abi_version == expected_header.e_ident[EI_ABIVERSION]);
    CHECK(header.object_type == expected_header.e_type && header.machine == expected_header.e_machine);
    CHECK(header.flags == expected_header.e_flags && header.entry == expected_header.e_entry);
    CHECK(header.program_offset == expected_header.e_phoff &&
          header.section_offset == expected_header.e_shoff);
    CHECK(header.header_bytes == expected_header.e_ehsize &&
          header.program_stride == expected_header.e_phentsize);
    CHECK(header.section_stride == expected_header.e_shentsize);
    TbSymbolElfSection_t zero;
    CHECK(tb_symbol_range(header.section_offset, header.section_stride, header.file_bytes));
    read_record(input, header.section_offset, header.section_stride, bytes);
    CHECK(tb_symbol_elf_section(&header, bytes, header.section_stride, &zero) == TB_SYMBOL_ELF_OK);
    TbSymbolElfLayout_t layout;
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_OK);
    size_t sections;
    size_t programs;
    size_t names;
    CHECK(!elf_getshdrnum(reference, &sections) && !elf_getphdrnum(reference, &programs));
    CHECK(!elf_getshdrstrndx(reference, &names));
    CHECK(layout.sections.count == sections && layout.programs.count == programs &&
          layout.section_names == names);
    CHECK(directory_view.count == sections && directory_view.layout->section_names == names);
    CHECK(directory_view.header->address_bytes == header.address_bytes);
    CHECK(directory_view.header->byte_order == header.byte_order);
    uint64_t abbreviation_section = UINT64_MAX;
    for (size_t i = 0; i < sections; ++i) {
        const TbSymbolSectionEntry_t *entry = &directory_view.entries[i];
        if (!entry->has_name)
            continue;
        const char *name = elf_strptr(reference, names, entry->header.name_offset);
        CHECK(name);
        if (!strcmp(name, ".debug_abbrev")) {
            CHECK(abbreviation_section == UINT64_MAX);
            abbreviation_section = i;
        }
    }
    size_t symbols = 0;
    UnitCounts_t unit_counts = {0};
    for (size_t i = 0; i < sections; ++i) {
        uint64_t offset;
        CHECK(tb_symbol_elf_entry(&layout.sections, i, &offset));
        read_record(input, offset, (size_t)layout.sections.stride, bytes);
        TbSymbolElfSection_t actual;
        CHECK(tb_symbol_elf_section(&header, bytes, (size_t)layout.sections.stride, &actual) ==
              TB_SYMBOL_ELF_OK);
        Elf_Scn *section = elf_getscn(reference, i);
        GElf_Shdr expected;
        CHECK(section && gelf_getshdr(section, &expected));
        compare_section(actual, expected);
        const TbSymbolSectionEntry_t *validated = &directory_view.entries[i];
        compare_section(validated->header, expected);
        if (actual.type != SHT_NULL && actual.name_offset) {
            const char *expected_name = elf_strptr(reference, names, actual.name_offset);
            CHECK(expected_name && validated->has_name);
            CHECK(validated->name_bytes == strlen(expected_name));
            if (!strcmp(expected_name, ".debug_info") || !strcmp(expected_name, ".debug_types")) {
                UnitCounts_t counts = check_units(directory_view, (TbSymbolUnitSelection_t){
                    .section_index = i,
                    .kind          = !strcmp(expected_name, ".debug_info")
                                         ? TB_SYMBOL_DWARF_INFO : TB_SYMBOL_DWARF_TYPES,
                    .descriptor    = fd
                }, abbreviation_section);
                unit_counts.abbreviations += counts.abbreviations;
                unit_counts.total += counts.total;
                unit_counts.unsupported += counts.unsupported;
            }
            uint64_t compared = 0;
            while (compared < validated->name_bytes) {
                uint64_t remaining = validated->name_bytes - compared;
                size_t length = remaining < sizeof(bytes) ? (size_t)remaining : sizeof(bytes);
                read_record(input, validated->name_offset + compared, length, bytes);
                CHECK(!memcmp(bytes, expected_name + compared, length));
                compared += length;
            }
        } else {
            CHECK(!validated->has_name);
        }
        if (actual.type != SHT_SYMTAB && actual.type != SHT_DYNSYM)
            continue;
        CHECK(actual.entry_bytes == (header.address_bytes == 4 ? 16U : 24U));
        CHECK(!(actual.bytes % actual.entry_bytes));
        TbSymbolElfTable_t table = {
            .offset = actual.offset,
            .count  = actual.bytes / actual.entry_bytes,
            .stride = actual.entry_bytes
        };
        TbSymbolSymbols_t *validated_symbols = tb_symbol_symbols_create(directory_view,
                                                                       (TbSymbolTableSelection_t){
            .section_index = i,
            .descriptor    = fd
        }, &budget);
        CHECK(validated_symbols);
        outcome = TB_SYMBOL_PENDING;
        for (unsigned turn = 0; turn < 1000000 && outcome == TB_SYMBOL_PENDING; ++turn)
            outcome = tb_symbol_symbols_step(validated_symbols, 257);
        CHECK(outcome == TB_SYMBOL_SUCCESS);
        TbSymbolSymbolsView_t symbol_view;
        CHECK(tb_symbol_symbols_view(validated_symbols, &symbol_view));
        CHECK(symbol_view.count == table.count && symbol_view.table_section == i);
        CHECK(symbol_view.kind == (actual.type == SHT_SYMTAB
                                       ? TB_SYMBOL_RECORD_SYMTAB : TB_SYMBOL_RECORD_DYNSYM));
        Elf_Data *data = elf_getdata(section, NULL);
        CHECK(data);
        for (uint64_t entry = 0; entry < table.count; ++entry) {
            CHECK(entry <= INT_MAX && tb_symbol_elf_entry(&table, entry, &offset));
            read_record(input, offset, (size_t)table.stride, bytes);
            TbSymbolElfSymbol_t symbol;
            CHECK(tb_symbol_elf_symbol(&header, bytes, (size_t)table.stride, &symbol) == TB_SYMBOL_ELF_OK);
            GElf_Sym expected_symbol;
            CHECK(gelf_getsym(data, (int)entry, &expected_symbol));
            CHECK(symbol.name_offset == expected_symbol.st_name && symbol.info == expected_symbol.st_info);
            CHECK(symbol.other == expected_symbol.st_other &&
                  symbol.section_index == expected_symbol.st_shndx);
            CHECK(symbol.value == expected_symbol.st_value && symbol.bytes == expected_symbol.st_size);
            const TbSymbolElfEntry_t *validated_entry = &symbol_view.entries[entry];
            CHECK(validated_entry->record_offset == entry * table.stride);
            CHECK(validated_entry->raw.name_offset == symbol.name_offset &&
                  validated_entry->raw.info == symbol.info);
            CHECK(validated_entry->raw.other == symbol.other &&
                  validated_entry->raw.section_index == symbol.section_index);
            CHECK(validated_entry->raw.value == symbol.value && validated_entry->raw.bytes == symbol.bytes);
            if (symbol.name_offset) {
                const char *name = elf_strptr(reference, actual.link, symbol.name_offset);
                CHECK(name && validated_entry->has_name && validated_entry->name_bytes == strlen(name));
                uint64_t compared = 0;
                while (compared < validated_entry->name_bytes) {
                    uint64_t remaining = validated_entry->name_bytes - compared;
                    size_t length = remaining < sizeof(bytes) ? (size_t)remaining : sizeof(bytes);
                    read_record(input, validated_entry->name_offset + compared, length, bytes);
                    CHECK(!memcmp(bytes, name + compared, length));
                    compared += length;
                }
            } else {
                CHECK(!validated_entry->has_name);
            }
            ++symbols;
        }
        tb_symbol_symbols_destroy(validated_symbols);
    }
    for (size_t i = 0; i < programs; ++i) {
        uint64_t offset;
        CHECK(i <= INT_MAX && tb_symbol_elf_entry(&layout.programs, i, &offset));
        read_record(input, offset, (size_t)layout.programs.stride, bytes);
        TbSymbolElfProgram_t actual;
        CHECK(tb_symbol_elf_program(&header, bytes, (size_t)layout.programs.stride, &actual) ==
              TB_SYMBOL_ELF_OK);
        GElf_Phdr expected;
        CHECK(gelf_getphdr(reference, (int)i, &expected));
        CHECK(actual.type == expected.p_type && actual.flags == expected.p_flags);
        CHECK(actual.offset == expected.p_offset && actual.virtual_address == expected.p_vaddr);
        CHECK(actual.physical_address == expected.p_paddr && actual.file_bytes == expected.p_filesz);
        CHECK(actual.memory_bytes == expected.p_memsz && actual.alignment == expected.p_align);
    }
    CHECK(symbols && sections && programs && unit_counts.total > unit_counts.unsupported);
    printf("PASS native DWARF composition: %llu contributions, %llu unsupported, %llu abbreviations\n",
           (unsigned long long)unit_counts.total, (unsigned long long)unit_counts.unsupported,
           (unsigned long long)unit_counts.abbreviations);
    CHECK(!elf_end(reference));
    tb_symbol_sections_destroy(directory);
    tb_symbol_file_destroy(input);
    CHECK(!close(fd));
    CHECK(tb_symbol_admission_close(ticket) && tb_symbol_admission_evict(backing));
    CHECK(tb_symbol_storage_unlink(storage, next_slot, &acquired.key));
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    CHECK(!tb_symbol_admission_usage(ledger).bytes);
    printf("PASS native ELF record agreement with libelf: %zu sections, %zu programs, %zu symbols\n",
           sections, programs, symbols);
}


int main(int argc, char **argv)
{
    CHECK(argc >= 2 && elf_version(EV_CURRENT) != EV_NONE);
    char directory[] = "/tmp/tb-symbol-elf-native-XXXXXX";
    CHECK(mkdtemp(directory));
    char pool[256];
    CHECK(snprintf(pool, sizeof(pool), "%s/pool", directory) > 0);
    CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
    ledger = tb_symbol_admission_create(TB_SYMBOL_FILE_BYTES_DEFAULT, TB_SYMBOL_ADMISSION_BYTES_DEFAULT,
                                         &budget);
    storage = tb_symbol_storage_create(pool, NULL, &budget);
    CHECK(ledger && storage);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 16 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_storage_step(storage);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    for (int i = 1; i < argc; ++i)
        check_file(argv[i]);
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
    CHECK(tb_symbol_admission_destroy(ledger) && !budget.used);
    CHECK(!rmdir(directory));
    return 0;
}
