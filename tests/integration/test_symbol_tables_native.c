#define _POSIX_C_SOURCE 200809L
#include "symbol/code_locations.h"
#include "../support/check.h"

#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    TbSymbolTables_t     *owner;
    TbSymbolTablesView_t  view;
} TableScope_t;

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


static uint64_t raw_word(Elf *elf, uint64_t section, uint64_t offset, unsigned width,
    TbSymbolByteOrder_e order)
{
    Elf_Data *bytes = elf_getdata(elf_getscn(elf, section), NULL);
    CHECK(bytes && bytes->d_buf && width <= 8 && offset <= bytes->d_size &&
          width <= bytes->d_size - (size_t)offset);
    const unsigned char *input = (const unsigned char *)bytes->d_buf + offset;
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) {
        unsigned index = order == TB_SYMBOL_LITTLE_ENDIAN ? width - 1 - i : i;
        value = value * 256 + input[index];
    }
    return value;
}


static TbSymbolTableValueView_t entry(TbSymbolSectionsView_t directory, TbSymbolTableBindingView_t binding,
    uint64_t index, int descriptor, TbSymbolBudget_t *budget, Elf *elf)
{
    size_t before = budget->used;
    TbSymbolTableValue_t *value = tb_symbol_table_value_create(directory, binding,
        (TbSymbolTableValueSelection_t) {
            .index      = index,
            .descriptor = descriptor
        }, budget);
    CHECK(value);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    while (outcome == TB_SYMBOL_PENDING)
        outcome = tb_symbol_table_value_step(value, 257);
    TbSymbolTableValueView_t view;
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_table_value_view(value, &view));
    unsigned width = binding.table.kind == TB_SYMBOL_TABLE_ADDRESSES ? binding.table.address_bytes :
                                                                      binding.table.extent.offset_bytes;
    CHECK(view.entry.value == raw_word(elf, binding.section_index, view.entry.offset, width,
                                       binding.table.byte_order));
    if (binding.table.kind == TB_SYMBOL_TABLE_RANGES)
        CHECK(view.entry.target_offset == binding.table.entries_offset + view.entry.value);
    printf("ENTRY kind=%u table=0x%llx index=%llu value=0x%llx target=0x%llx\n",
           (unsigned)binding.table.kind, (unsigned long long)binding.table.table_offset,
           (unsigned long long)index, (unsigned long long)view.entry.value,
           (unsigned long long)view.entry.target_offset);
    tb_symbol_table_value_destroy(value);
    CHECK(budget->used == before);
    return view;
}


static TbSymbolCodeView_t code_view(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
    TbSymbolCodeSelection_t selection, const TbSymbolAbbrev_t *abbrev)
{
    TbSymbolCode_t code;
    CHECK(tb_symbol_code_init(&code, units, tree, selection) == TB_SYMBOL_CODE_PENDING);
    TbSymbolCodeStatus_e status = TB_SYMBOL_CODE_PENDING;
    while (status == TB_SYMBOL_CODE_PENDING || status == TB_SYMBOL_CODE_SCHEMA) {
        if (status == TB_SYMBOL_CODE_SCHEMA) {
            TbSymbolAbbrevView_t schema;
            CHECK(tb_symbol_abbrev_view(abbrev, &schema));
            status = tb_symbol_code_schema(&code, &schema);
        } else {
            status = tb_symbol_code_step(&code, 31);
        }
    }
    TbSymbolCodeView_t view;
    CHECK(status == TB_SYMBOL_CODE_COMPLETE && tb_symbol_code_view(&code, &view));
    tb_symbol_code_discard(&code);
    return view;
}


static void executable_storage(Elf *elf, uint64_t begin, uint64_t end)
{
    if (begin == end)
        return;

    size_t count;
    CHECK(elf_getphdrnum(elf, &count) == 0);
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        GElf_Phdr segment;
        CHECK(gelf_getphdr(elf, (int)i, &segment));
        uint64_t limit;
        if (segment.p_type == PT_LOAD && (segment.p_flags & PF_X) &&
            tb_symbol_add(segment.p_vaddr, segment.p_filesz, &limit) &&
            begin >= segment.p_vaddr && end <= limit) {
            found = true;
        }
    }
    CHECK(found);
}


static size_t check_locations(TbSymbolSectionsView_t directory, TbSymbolCodeView_t code,
    TbSymbolCodeView_t root, const TbSymbolTableBindingView_t *bindings, const bool *bound,
    TableScope_t ranges, int descriptor, TbSymbolBudget_t *budget, Elf *elf)
{
    TbSymbolTableValueView_t dependencies[4] = {0};
    TbSymbolCodeLocationsRequest_t selection = {
        .code       = code,
        .root       = root,
        .byte_order = directory.header->byte_order,
        .addresses  = bound[0] ? &bindings[0] : NULL
    };
    const TbSymbolCodeFact_t *facts[] = {
        &code.fields[TB_SYMBOL_CODE_LOW], &code.fields[TB_SYMBOL_CODE_HIGH],
        &root.fields[TB_SYMBOL_CODE_LOW], &code.fields[TB_SYMBOL_CODE_RANGES]
    };
    const TbSymbolTableValueView_t **destinations[] = {
        &selection.low, &selection.high, &selection.root_low, &selection.range
    };
    for (unsigned i = 0; i < 4; ++i) {
        if (facts[i]->kind != TB_SYMBOL_CODE_ADDRESS_INDEX && facts[i]->kind != TB_SYMBOL_CODE_LIST_INDEX)
            continue;
        unsigned kind = i == 3 ? 1U : 0U;
        CHECK(bound[kind]);
        dependencies[i] = entry(directory, bindings[kind], facts[i]->value, descriptor, budget, elf);
        *destinations[i] = &dependencies[i];
    }

    TbSymbolCodeLocationsView_t locations;
    CHECK(tb_symbol_code_locations_prepare(selection, &locations) == TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
    uint64_t die = code.fields[TB_SYMBOL_CODE_LOW].selection.record_offset;
    size_t count = 0;
    if (locations.interval_available) {
        executable_storage(elf, locations.low, locations.end);
        printf("LOCATION die=0x%llx begin=0x%llx end=0x%llx\n", (unsigned long long)die,
               (unsigned long long)locations.low, (unsigned long long)locations.end);
        ++count;
    }
    if (!locations.list_available)
        return count;

    TbSymbolIndexedTable_t table = {0};
    TbSymbolRangeIndexRequest_t scope = {
        .byte_order    = directory.header->byte_order,
        .version       = code.unit.version,
        .address_bytes = code.unit.address_bytes
    };
    uint64_t section = UINT64_MAX;
    if (code.unit.version == 5) {
        bool found = false;
        for (uint64_t i = 0; i < ranges.view.count; ++i) {
            const TbSymbolTableContribution_t *contribution = &ranges.view.entries[i];
            CHECK(contribution->header_available);
            if (locations.list_offset >= contribution->table.lists_offset &&
                locations.list_offset < contribution->table.extent.end_offset) {
                CHECK(!found);
                table = contribution->table;
                found = true;
            }
        }
        CHECK(found && table.address_bytes == code.unit.address_bytes && !table.segment_bytes);
        scope.offset     = table.lists_offset;
        scope.end_offset = table.extent.end_offset;
        section          = ranges.view.section_index;
    } else {
        section = section_index(elf, ".debug_ranges");
        CHECK(section != UINT64_MAX);
        scope.end_offset = directory.entries[section].header.bytes;
    }
    Elf_Data *bytes = elf_getdata(elf_getscn(elf, section), NULL);
    CHECK(bytes && bytes->d_buf && scope.end_offset <= bytes->d_size);
    size_t capacity = (size_t)(scope.end_offset - scope.offset);
    TbSymbolRangeBoundary_t *boundaries = calloc(capacity, sizeof(*boundaries));
    TbSymbolRangeRecord_t *records = calloc(capacity, sizeof(*records));
    TbSymbolRange_t *intervals = calloc(capacity, sizeof(*intervals));
    CHECK(boundaries && records && intervals);
    TbSymbolRangeIndex_t index;
    CHECK(tb_symbol_range_index_init(&index, scope, boundaries, capacity) == TB_SYMBOL_RANGE_INDEX_PENDING);
    size_t cursor = (size_t)scope.offset;
    while (index.status == TB_SYMBOL_RANGE_INDEX_PENDING) {
        size_t consumed;
        uint64_t progress = index.progress;
        (void)tb_symbol_range_index_feed(&index, (const unsigned char *)bytes->d_buf + cursor,
                                        (size_t)scope.end_offset - cursor, &consumed);
        CHECK(consumed <= 256 && index.progress > progress);
        cursor += consumed;
    }
    TbSymbolRangeIndexView_t inventory;
    CHECK(index.status == TB_SYMBOL_RANGE_INDEX_COMPLETE && tb_symbol_range_index_view(&index, &inventory));
    TbSymbolRangesRequest_t request;
    CHECK(tb_symbol_code_locations_ranges(locations, inventory, code.unit.version == 5 ? &table : NULL,
                                           &request) == TB_SYMBOL_CODE_LOCATIONS_COMPLETE);
    TbSymbolRanges_t decoder;
    CHECK(tb_symbol_ranges_init(&decoder, request, records, capacity, intervals, capacity) ==
          TB_SYMBOL_RANGES_PENDING);
    cursor = (size_t)request.offset;
    while (decoder.status == TB_SYMBOL_RANGES_PENDING || decoder.status == TB_SYMBOL_RANGES_ADDRESS) {
        if (decoder.status == TB_SYMBOL_RANGES_ADDRESS) {
            uint64_t needed;
            CHECK(bound[0] && tb_symbol_ranges_address_need(&decoder, &needed));
            TbSymbolTableValueView_t value = entry(directory, bindings[0], needed, descriptor, budget, elf);
            (void)tb_symbol_ranges_address(&decoder, &value.entry);
        } else {
            size_t consumed;
            uint64_t progress = decoder.progress;
            (void)tb_symbol_ranges_feed(&decoder, (const unsigned char *)bytes->d_buf + cursor,
                                        (size_t)request.end_offset - cursor, &consumed);
            CHECK(consumed <= 256 && decoder.progress > progress);
            cursor += consumed;
        }
    }
    TbSymbolRangesView_t view;
    CHECK(decoder.status == TB_SYMBOL_RANGES_COMPLETE && tb_symbol_ranges_view(&decoder, &view));
    CHECK(view.end_offset == request.end_offset && cursor == request.end_offset);
    for (size_t i = 0; i < view.range_count; ++i) {
        executable_storage(elf, view.ranges[i].begin, view.ranges[i].end);
        printf("LOCATION die=0x%llx begin=0x%llx end=0x%llx\n", (unsigned long long)die,
               (unsigned long long)view.ranges[i].begin, (unsigned long long)view.ranges[i].end);
        ++count;
    }
    tb_symbol_ranges_discard(&decoder);
    tb_symbol_range_index_discard(&index);
    free(intervals);
    free(records);
    free(boundaries);

    return count;
}


int main(int argc, char **argv)
{
    CHECK((argc == 2 || (argc == 3 && !strcmp(argv[2], "--indexed"))) &&
          elf_version(EV_CURRENT) != EV_NONE);
    char temporary[] = "/tmp/tb-symbol-tables-native-XXXXXX";
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

    TableScope_t scopes[2] = {0};
    const char *names[] = {".debug_addr", ".debug_rnglists"};
    size_t contributions = 0;
    for (unsigned kind = 0; kind < 2; ++kind) {
        uint64_t index = section_index(elf, names[kind]);
        if (index == UINT64_MAX)
            continue;
        scopes[kind].owner = tb_symbol_tables_create(directory, (TbSymbolTablesSelection_t) {
            .section_index = index,
            .kind          = (TbSymbolIndexedTableKind_e)kind,
            .descriptor    = descriptor
        }, &budget);
        CHECK(scopes[kind].owner);
        outcome = TB_SYMBOL_PENDING;
        while (outcome == TB_SYMBOL_PENDING)
            outcome = tb_symbol_tables_step(scopes[kind].owner, 257);
        CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_tables_view(scopes[kind].owner, &scopes[kind].view));
        CHECK(!scopes[kind].view.unsupported);
        contributions += (size_t)scopes[kind].view.count;
        for (uint64_t i = 0; i < scopes[kind].view.count; ++i) {
            const TbSymbolIndexedTable_t *table = &scopes[kind].view.entries[i].table;
            printf("TABLE kind=%u offset=0x%llx entries=0x%llx count=%llu\n", kind,
                   (unsigned long long)table->table_offset, (unsigned long long)table->entries_offset,
                   (unsigned long long)table->count);
        }
    }
    CHECK(contributions || section_index(elf, ".debug_ranges") != UINT64_MAX);

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
    size_t locations = 0;
    size_t indexed = 0;
    size_t values = 0;
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
        TbSymbolCodeView_t root = code_view(inventory, tree, (TbSymbolCodeSelection_t) {
            .unit_index = i
        }, abbrev);
        TbSymbolTableBindingView_t bindings[2] = {0};
        bool bound[2] = {0};
        for (unsigned kind = 0; kind < 2; ++kind) {
            if (!scopes[kind].owner)
                continue;
            TbSymbolTableBinding_t binding;
            TbSymbolTableBindStatus_e status =
                tb_symbol_table_binding_init(&binding, scopes[kind].view, root);
            while (status == TB_SYMBOL_TABLE_BIND_PENDING)
                status = tb_symbol_table_binding_step(&binding, 31);
            CHECK(status == TB_SYMBOL_TABLE_BIND_FOUND || status == TB_SYMBOL_TABLE_BIND_DEPENDENCY);
            if (status == TB_SYMBOL_TABLE_BIND_FOUND) {
                CHECK(tb_symbol_table_binding_view(&binding, &bindings[kind]));
                bound[kind] = true;
                for (uint64_t index = 0; index < bindings[kind].table.count; ++index) {
                    (void)entry(directory, bindings[kind], index, descriptor, &budget, elf);
                    ++values;
                }
            }
            tb_symbol_table_binding_discard(&binding);
        }
        for (uint64_t die = 0; die < tree.count; ++die) {
            TbSymbolCodeView_t code = code_view(inventory, tree, (TbSymbolCodeSelection_t) {
                .unit_index = i,
                .die_index  = die
            }, abbrev);
            locations += check_locations(directory, code, root, bindings, bound, scopes[1], descriptor,
                                         &budget, elf);
            for (unsigned field = TB_SYMBOL_CODE_LOW; field <= TB_SYMBOL_CODE_RANGES; ++field) {
                const TbSymbolCodeFact_t *fact = &code.fields[field];
                if (fact->kind != TB_SYMBOL_CODE_ADDRESS_INDEX && fact->kind != TB_SYMBOL_CODE_LIST_INDEX)
                    continue;
                unsigned kind = fact->kind == TB_SYMBOL_CODE_ADDRESS_INDEX ? 0U : 1U;
                CHECK(bound[kind] &&
                      fact->selection.unit_offset == bindings[kind].base.selection.unit_offset);
                (void)entry(directory, bindings[kind], fact->value, descriptor, &budget, elf);
                ++indexed;
            }
        }
        tb_symbol_dies_destroy(dies);
        tb_symbol_abbrev_destroy(abbrev);
    }
    CHECK(locations && (argc == 2 || indexed));
    tb_symbol_units_destroy(units);
    for (unsigned kind = 0; kind < 2; ++kind)
        tb_symbol_tables_destroy(scopes[kind].owner);
    tb_symbol_sections_destroy(sections);
    CHECK(budget.used == 0 && elf_end(elf) == 0 && close(descriptor) == 0 && unlink(temporary) == 0);
    printf("PASS native indexed tables: %zu contributions, %zu bound entries, %zu indexed code facts\n",
           contributions, values, indexed);
    printf("PASS composed code locations: %zu intervals in executable file-backed segments\n", locations);
    return 0;
}
