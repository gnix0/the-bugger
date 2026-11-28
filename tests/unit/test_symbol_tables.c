#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected descriptor fixtures. */
#include "symbol/tables.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned char image[131072];
static char retained[64];
static unsigned elf_width;
static TbSymbolByteOrder_e order;
static uint64_t used;
static long fail_after = -1;
static int read_error;
static bool short_read;
static bool early_eof;
static size_t inspected;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size);
ssize_t __real_pread(int descriptor, void *bytes, size_t length, off_t offset);
ssize_t __wrap_pread(int descriptor, void *bytes, size_t length, off_t offset);

void *__wrap_calloc(size_t count, size_t size)
{
    return fail_after >= 0 && fail_after-- == 0 ? NULL : __real_calloc(count, size);
}


void *__wrap_malloc(size_t size)
{
    return fail_after >= 0 && fail_after-- == 0 ? NULL : __real_malloc(size);
}


ssize_t __wrap_pread(int descriptor, void *bytes, size_t length, off_t offset)
{
    inspected += length;
    CHECK(length && inspected <= 64);

    if (read_error) {
        errno = read_error;
        read_error = 0;
        return -1;
    }
    if (early_eof)
        return 0;
    if (short_read && length > 1)
        length = 1;

    return __real_pread(descriptor, bytes, length, offset);
}


static void put(size_t at, unsigned width, uint64_t value)
{
    CHECK(width <= 8 && at <= sizeof(image) && width <= sizeof(image) - at);

    for (unsigned i = 0; i < width; ++i) {
        unsigned index = order == TB_SYMBOL_LITTLE_ENDIAN ? i : width - 1 - i;
        image[at + index] = (unsigned char)(value >> (8 * i));
    }
}


static size_t selected(void)
{
    return 128 + (elf_width == 4 ? 40U : 64U);
}


static void fixture(void)
{
    memset(image, 0, sizeof(image));
    used = 0;
    memcpy(image, ELFMAG, SELFMAG);
    image[EI_CLASS] = elf_width == 4 ? ELFCLASS32 : ELFCLASS64;
    image[EI_DATA] = order == TB_SYMBOL_LITTLE_ENDIAN ? ELFDATA2LSB : ELFDATA2MSB;
    image[EI_VERSION] = EV_CURRENT;
    put(16, 2, ET_EXEC);
    put(18, 2, EM_RISCV);
    put(20, 4, EV_CURRENT);
    put(elf_width == 4 ? 32 : 40, elf_width, 128);
    put(elf_width == 4 ? 40 : 52, 2, elf_width == 4 ? 52 : 64);
    put(elf_width == 4 ? 46 : 58, 2, elf_width == 4 ? 40 : 64);
    put(elf_width == 4 ? 48 : 60, 2, 2);
    put(selected() + 4, 4, SHT_PROGBITS);
    put(selected() + (elf_width == 4 ? 16 : 24), elf_width, 512);
    put(selected() + (elf_width == 4 ? 32 : 48), elf_width, 1);
}


typedef struct {
    TbSymbolIndexedTableKind_e kind;
    unsigned                  offsets;
    unsigned                  addresses;
    unsigned                  selectors;
    unsigned                  version;
    unsigned                  count;
} TableSpec_t;

static uint64_t append(TableSpec_t spec)
{
    size_t at = 512 + (size_t)used;
    unsigned initial = spec.offsets == 4 ? 4 : 12;
    unsigned prefix = initial + (spec.kind == TB_SYMBOL_TABLE_ADDRESSES ? 4U : 8U);
    unsigned stride = spec.kind == TB_SYMBOL_TABLE_ADDRESSES ? spec.addresses + spec.selectors : spec.offsets;
    uint64_t total = spec.version == 5 ? prefix + (uint64_t)spec.count * stride +
                                        (spec.kind == TB_SYMBOL_TABLE_RANGES ? spec.count : 0U) : initial + 2;
    if (spec.offsets == 4) {
        put(at, 4, total - 4);
    } else {
        put(at, 4, UINT32_MAX);
        put(at + 4, 8, total - 12);
    }
    put(at + initial, 2, spec.version);
    if (spec.version == 5) {
        put(at + initial + 2, 1, spec.addresses);
        put(at + initial + 3, 1, spec.selectors);
        if (spec.kind == TB_SYMBOL_TABLE_RANGES)
            put(at + initial + 4, 4, spec.count);
        for (unsigned i = 0; i < spec.count; ++i) {
            size_t slot = at + prefix + (size_t)i * stride;
            if (spec.kind == TB_SYMBOL_TABLE_ADDRESSES) {
                if (spec.selectors)
                    put(slot, spec.selectors, 1);
                put(slot + spec.selectors, spec.addresses, 16 + i);
            } else {
                put(slot, spec.offsets, (uint64_t)spec.count * spec.offsets + i);
            }
        }
    }

    uint64_t entries = used + prefix;
    used += total;
    put(selected() + (elf_width == 4 ? 20 : 32), elf_width, used);
    return entries;
}


static int backing(TbSymbolStorageInfo_t *info)
{
    strcpy(retained, "/tmp/tb-symbol-tables-XXXXXX");
    int writer = mkstemp(retained);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(fchmod(writer, 0400) == 0 && close(writer) == 0);
    int descriptor = open(retained, O_RDONLY | O_CLOEXEC);
    struct stat status;
    CHECK(descriptor >= 0 && fstat(descriptor, &status) == 0);
    *info = (TbSymbolStorageInfo_t) {
        .key = {
            .device = (uint64_t)status.st_dev,
            .inode  = (uint64_t)status.st_ino
        },
        .bytes = (uint64_t)status.st_size
    };
    return descriptor;
}


static TbSymbolSections_t *directory(int descriptor, TbSymbolStorageInfo_t info, TbSymbolBudget_t *budget,
    TbSymbolSectionsView_t *view)
{
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, descriptor, budget);
    CHECK(sections);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    while (outcome == TB_SYMBOL_PENDING) {
        inspected = 0;
        outcome = tb_symbol_sections_step(sections, 64);
    }
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(sections, view));
    return sections;
}


static TbSymbolOutcome_e finish(TbSymbolTables_t *tables)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned step = 0; step < 100000 && outcome == TB_SYMBOL_PENDING; ++step) {
        TbSymbolTablesView_t unavailable;
        CHECK(!tb_symbol_tables_view(tables, &unavailable));
        inspected = 0;
        outcome = tb_symbol_tables_step(tables, 64);
    }
    return outcome;
}


static TbSymbolCodeView_t root(TbSymbolTablesView_t tables, uint64_t base, unsigned addresses)
{
    TbSymbolCodeView_t code = {
        .unit = {
            .version       = 5,
            .address_bytes = (uint8_t)addresses,
            .header_bytes  = 24
        }
    };
    TbSymbolCodeField_e field = tables.kind == TB_SYMBOL_TABLE_ADDRESSES ? TB_SYMBOL_CODE_ADDR_BASE :
                                                                        TB_SYMBOL_CODE_RNGLISTS_BASE;
    code.fields[field] = (TbSymbolCodeFact_t) {
        .selection = {
            .backing       = tables.backing,
            .section_index = 7,
            .unit_offset   = 60,
            .record_offset = 84,
            .name          = tables.kind == TB_SYMBOL_TABLE_ADDRESSES ? 0x73 : 0x74,
            .matches       = 1
        },
        .value = base,
        .kind  = TB_SYMBOL_CODE_TABLE_BASE
    };
    return code;
}


static TbSymbolTableBindStatus_e bind(TbSymbolTableBinding_t *binding, TbSymbolTablesView_t tables,
    TbSymbolCodeView_t code)
{
    TbSymbolTableBindStatus_e status = tb_symbol_table_binding_init(binding, tables, code);
    while (status == TB_SYMBOL_TABLE_BIND_PENDING)
        status = tb_symbol_table_binding_step(binding, 1);
    return status;
}


static TbSymbolOutcome_e read_value(TbSymbolTableValue_t *value)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    while (outcome == TB_SYMBOL_PENDING) {
        TbSymbolTableValueView_t unavailable;
        CHECK(!tb_symbol_table_value_view(value, &unavailable));
        inspected = 0;
        outcome = tb_symbol_table_value_step(value, 64);
    }
    return outcome;
}


static void check_profile(TableSpec_t spec)
{
    fixture();
    uint64_t first = append(spec);
    TableSpec_t other = spec;
    other.offsets = spec.offsets == 4 ? 8 : 4;
    uint64_t second = append(other);
    other.version = 6;
    (void)append(other);
    other.version = 5;
    other.count = 0;
    uint64_t empty = append(other);
    (void)append(spec);
    TbSymbolStorageInfo_t info;
    int descriptor = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
    TbSymbolSectionsView_t sections;
    TbSymbolSections_t *owner = directory(descriptor, info, &budget, &sections);
    size_t baseline = budget.used;
    TbSymbolTables_t *tables = tb_symbol_tables_create(sections, (TbSymbolTablesSelection_t) {
        .section_index = 1,
        .kind          = spec.kind,
        .descriptor    = descriptor
    }, &budget);
    CHECK(tables && finish(tables) == TB_SYMBOL_SUCCESS);
    TbSymbolTablesView_t view;
    CHECK(tb_symbol_tables_view(tables, &view) && view.count == 5 && view.unsupported == 1);
    CHECK(view.entries[2].header_available == false && view.entries[2].extent.payload_bytes == 2);
    CHECK(view.entries[0].table.entries_offset == first && view.entries[1].table.entries_offset == second);
    CHECK(view.entries[1].table.extent.offset_bytes != view.entries[0].table.extent.offset_bytes);

    TbSymbolTableBinding_t binding;
    CHECK(bind(&binding, view, root(view, second, spec.addresses)) == TB_SYMBOL_TABLE_BIND_FOUND);
    TbSymbolTableBindingView_t selected_table;
    CHECK(tb_symbol_table_binding_view(&binding, &selected_table) && selected_table.contribution_index == 1);
    size_t before_values = budget.used;
    for (uint64_t index = 0; index < spec.count; ++index) {
        TbSymbolTableValue_t *value = tb_symbol_table_value_create(sections, selected_table,
            (TbSymbolTableValueSelection_t) {
                .index      = index,
                .descriptor = descriptor
            }, &budget);
        CHECK(value && read_value(value) == TB_SYMBOL_SUCCESS);
        TbSymbolTableValueView_t result;
        CHECK(tb_symbol_table_value_view(value, &result));
        CHECK(result.entry.index == index && result.binding.base.value == second);
        if (spec.kind == TB_SYMBOL_TABLE_ADDRESSES) {
            CHECK(result.entry.value == 16 + index && result.entry.target_offset == UINT64_MAX);
        } else {
            CHECK(result.entry.target_offset == view.entries[1].table.lists_offset + index);
        }
        tb_symbol_table_value_destroy(value);
        CHECK(budget.used == before_values);
    }
    CHECK(!tb_symbol_table_value_create(sections, selected_table, (TbSymbolTableValueSelection_t) {
        .index      = spec.count,
        .descriptor = descriptor
    }, &budget));
    CHECK(budget.used == before_values);
    CHECK(bind(&binding, view, root(view, empty, spec.addresses)) == TB_SYMBOL_TABLE_BIND_FOUND);
    CHECK(bind(&binding, view, root(view, first + 1, spec.addresses)) == TB_SYMBOL_TABLE_BIND_MALFORMED);
    CHECK(bind(&binding, view, root(view, first, spec.addresses == 1 ? 2U : 1U)) ==
          TB_SYMBOL_TABLE_BIND_MALFORMED);
    CHECK(bind(&binding, view, root(view, view.entries[2].offset + 1, spec.addresses)) ==
          TB_SYMBOL_TABLE_BIND_UNSUPPORTED);
    TbSymbolCodeView_t missing = root(view, first, spec.addresses);
    missing.fields[spec.kind == TB_SYMBOL_TABLE_ADDRESSES ? TB_SYMBOL_CODE_ADDR_BASE :
                                                         TB_SYMBOL_CODE_RNGLISTS_BASE].kind =
        TB_SYMBOL_CODE_MISSING;
    CHECK(bind(&binding, view, missing) == TB_SYMBOL_TABLE_BIND_DEPENDENCY);
    TbSymbolCodeView_t wrong = root(view, first, spec.addresses);
    wrong.die_index = 1;
    TbSymbolTableBinding_t old = binding;
    CHECK(tb_symbol_table_binding_init(&binding, view, wrong) == TB_SYMBOL_TABLE_BIND_INVALID);
    CHECK(!memcmp(&old, &binding, sizeof(binding)));
    tb_symbol_table_binding_discard(&binding);
    CHECK(!tb_symbol_table_binding_view(&binding, &selected_table));

    tb_symbol_tables_destroy(tables);
    CHECK(budget.used == baseline);
    tb_symbol_sections_destroy(owner);
    CHECK(budget.used == 0 && close(descriptor) == 0 && unlink(retained) == 0);
}


static void check_failures(void)
{
    elf_width = 8;
    order = TB_SYMBOL_LITTLE_ENDIAN;
    TableSpec_t spec = {
        .kind      = TB_SYMBOL_TABLE_ADDRESSES,
        .offsets   = 4,
        .addresses = 4,
        .version   = 5,
        .count     = 4
    };
    for (unsigned fault = 0; fault < 8; ++fault) {
        fixture();
        (void)append(spec);
        if (fault == 0) {
            uint64_t late = used;
            (void)append(spec);
            put(512 + (size_t)late, 4, used);
        }
        if (fault == 1)
            put(518, 1, 0);
        if (fault == 2)
            put(selected() + 8, 8, SHF_COMPRESSED);
        TbSymbolStorageInfo_t info;
        int descriptor = backing(&info);
        TbSymbolBudget_t budget;
        CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
        TbSymbolSectionsView_t sections;
        TbSymbolSections_t *owner = directory(descriptor, info, &budget, &sections);
        TbSymbolTables_t *tables = tb_symbol_tables_create(sections, (TbSymbolTablesSelection_t) {
            .section_index = 1,
            .kind          = spec.kind,
            .descriptor    = descriptor
        }, &budget);
        CHECK(tables);
        if (fault == 3) read_error = EINTR;
        if (fault == 4) read_error = EIO;
        if (fault == 5) early_eof = true;
        if (fault == 6) short_read = true;
        if (fault == 7) CHECK(fcntl(descriptor, F_SETFD, 0) == 0);
        TbSymbolOutcome_e outcome = finish(tables);
        TbSymbolOutcome_e expected = fault < 2 ? TB_SYMBOL_MALFORMED :
                                     fault == 2 || fault == 7 ? TB_SYMBOL_UNSUPPORTED :
                                     fault == 4 || fault == 5 ? TB_SYMBOL_OPERATIONAL_FAILURE :
                                                              TB_SYMBOL_SUCCESS;
        CHECK(outcome == expected);
        early_eof = short_read = false;
        TbSymbolTablesView_t view;
        if (expected != TB_SYMBOL_SUCCESS)
            CHECK(!tb_symbol_tables_view(tables, &view));
        tb_symbol_tables_destroy(tables);
        tb_symbol_sections_destroy(owner);
        CHECK(budget.used == 0 && close(descriptor) == 0 && unlink(retained) == 0);
    }

    for (long failure = 0; failure < 4; ++failure) {
        fixture();
        (void)append(spec);
        TbSymbolStorageInfo_t info;
        int descriptor = backing(&info);
        TbSymbolBudget_t budget;
        CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
        TbSymbolSectionsView_t sections;
        TbSymbolSections_t *owner = directory(descriptor, info, &budget, &sections);
        size_t before = budget.used;
        fail_after = failure;
        TbSymbolTables_t *tables = tb_symbol_tables_create(sections, (TbSymbolTablesSelection_t) {
            .section_index = 1,
            .kind          = spec.kind,
            .descriptor    = descriptor
        }, &budget);
        if (tables)
            CHECK(finish(tables) == (failure == 2 ? TB_SYMBOL_RESOURCE_FAILURE : TB_SYMBOL_SUCCESS));
        else
            CHECK(failure < 2);
        fail_after = -1;
        if (tables && failure == 3) {
            TbSymbolTablesView_t view;
            CHECK(tb_symbol_tables_view(tables, &view));
            TbSymbolTableBinding_t binding;
            CHECK(bind(&binding, view, root(view, view.entries[0].table.entries_offset, 4)) ==
                  TB_SYMBOL_TABLE_BIND_FOUND);
            TbSymbolTableBindingView_t selected_table;
            CHECK(tb_symbol_table_binding_view(&binding, &selected_table));
            size_t before_values = budget.used;
            for (long allocation = 0; allocation < 3; ++allocation) {
                fail_after = allocation;
                TbSymbolTableValue_t *value = tb_symbol_table_value_create(sections, selected_table,
                    (TbSymbolTableValueSelection_t) {
                        .descriptor = descriptor
                    }, &budget);
                fail_after = -1;
                CHECK((value != NULL) == (allocation == 2));
                tb_symbol_table_value_destroy(value);
                CHECK(budget.used == before_values);
            }
            size_t ceiling = budget.limit;
            budget.limit = budget.used;
            CHECK(!tb_symbol_table_value_create(sections, selected_table,
                (TbSymbolTableValueSelection_t) {
                    .descriptor = descriptor
                }, &budget));
            CHECK(budget.used == before_values);
            budget.limit = ceiling;
        }
        tb_symbol_tables_destroy(tables);
        CHECK(budget.used == before);
        tb_symbol_sections_destroy(owner);
        CHECK(budget.used == 0 && close(descriptor) == 0 && unlink(retained) == 0);
    }
}


static void check_entry_failures(void)
{
    for (unsigned fault = 0; fault < 11; ++fault) {
        fixture();
        TableSpec_t spec = {
            .kind      = fault == 0 ? TB_SYMBOL_TABLE_RANGES : TB_SYMBOL_TABLE_ADDRESSES,
            .offsets   = 4,
            .addresses = 4,
            .selectors = fault == 1 ? 1U : 0U,
            .version   = 5,
            .count     = 4
        };
        uint64_t base = append(spec);
        if (!fault)
            put(512 + (size_t)base, 4, 0);
        TbSymbolStorageInfo_t info;
        int descriptor = backing(&info);
        TbSymbolBudget_t budget;
        CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
        TbSymbolSectionsView_t sections;
        TbSymbolSections_t *owner = directory(descriptor, info, &budget, &sections);
        TbSymbolTables_t *tables = tb_symbol_tables_create(sections, (TbSymbolTablesSelection_t) {
            .section_index = 1,
            .kind          = spec.kind,
            .descriptor    = descriptor
        }, &budget);
        CHECK(tables && finish(tables) == TB_SYMBOL_SUCCESS);
        TbSymbolTablesView_t view;
        CHECK(tb_symbol_tables_view(tables, &view));
        TbSymbolTableBinding_t binding;
        CHECK(bind(&binding, view, root(view, base, 4)) == TB_SYMBOL_TABLE_BIND_FOUND);
        TbSymbolTableBindingView_t selected_table;
        CHECK(tb_symbol_table_binding_view(&binding, &selected_table));
        size_t before = budget.used;
        TbSymbolTableValue_t *value = tb_symbol_table_value_create(sections, selected_table,
            (TbSymbolTableValueSelection_t) {
                .descriptor = descriptor
            }, &budget);
        CHECK(value);
        switch (fault) {
        case 2: read_error = EIO; break;
        case 3: early_eof = true; break;
        case 4: read_error = EINTR; break;
        case 5: short_read = true; break;
        case 6: CHECK(fcntl(descriptor, F_SETFD, 0) == 0); break;
        case 7: {
            int replacement = open("/dev/null", O_RDONLY | O_CLOEXEC);
            CHECK(replacement >= 0 && dup3(replacement, descriptor, O_CLOEXEC) == descriptor);
            CHECK(close(replacement) == 0);
            break;
        }
        case 8: CHECK(tb_symbol_table_value_step(value, 64) == TB_SYMBOL_PENDING); break;
        case 9: CHECK(tb_symbol_table_value_step(value, 0) == TB_SYMBOL_OPERATIONAL_FAILURE); break;
        case 10: CHECK(tb_symbol_table_value_step(value, 65537) == TB_SYMBOL_OPERATIONAL_FAILURE); break;
        default: break;
        }
        if (fault != 8) {
            TbSymbolOutcome_e expected = fault == 0 ? TB_SYMBOL_MALFORMED :
                                         fault == 1 || fault == 6 ? TB_SYMBOL_UNSUPPORTED :
                                         fault == 4 || fault == 5 ? TB_SYMBOL_SUCCESS :
                                                                  TB_SYMBOL_OPERATIONAL_FAILURE;
            CHECK(read_value(value) == expected);
            if (expected != TB_SYMBOL_SUCCESS) {
                TbSymbolTableValueView_t unavailable;
                CHECK(!tb_symbol_table_value_view(value, &unavailable));
                CHECK(tb_symbol_table_value_failure(value).reason != TB_SYMBOL_TABLES_NONE);
            }
        }
        early_eof = short_read = false;
        tb_symbol_table_value_destroy(value);
        CHECK(budget.used == before);
        tb_symbol_tables_destroy(tables);
        tb_symbol_sections_destroy(owner);
        CHECK(budget.used == 0 && close(descriptor) == 0 && unlink(retained) == 0);
    }
}


static void check_empty_and_bounded(void)
{
    for (unsigned count = 0; count <= 520; count += 520) {
        fixture();
        uint64_t base = 0;
        for (unsigned i = 0; i < count; ++i) {
            base = append((TableSpec_t) {
                .kind      = TB_SYMBOL_TABLE_ADDRESSES,
                .offsets   = i % 2 ? 8U : 4U,
                .addresses = 4,
                .version   = 5,
                .count     = 1
            });
        }
        TbSymbolStorageInfo_t info;
        int descriptor = backing(&info);
        TbSymbolBudget_t budget;
        CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
        TbSymbolSectionsView_t sections;
        TbSymbolSections_t *owner = directory(descriptor, info, &budget, &sections);
        size_t before = budget.used;
        for (unsigned slice = 0; slice < 24; ++slice) {
            TbSymbolTables_t *pending = tb_symbol_tables_create(sections, (TbSymbolTablesSelection_t) {
                .section_index = 1,
                .kind          = TB_SYMBOL_TABLE_ADDRESSES,
                .descriptor    = descriptor
            }, &budget);
            CHECK(pending);
            for (unsigned step = 0; step < slice; ++step) {
                inspected = 0;
                (void)tb_symbol_tables_step(pending, 64);
            }
            tb_symbol_tables_destroy(pending);
            CHECK(budget.used == before);
        }
        TbSymbolTables_t *tables = tb_symbol_tables_create(sections, (TbSymbolTablesSelection_t) {
            .section_index = 1,
            .kind          = TB_SYMBOL_TABLE_ADDRESSES,
            .descriptor    = descriptor
        }, &budget);
        CHECK(tables && finish(tables) == TB_SYMBOL_SUCCESS);
        TbSymbolTablesView_t view;
        CHECK(tb_symbol_tables_view(tables, &view) && view.count == count);
        if (count) {
            TbSymbolTableBinding_t binding;
            CHECK(tb_symbol_table_binding_init(&binding, view, root(view, base, 4)) ==
                  TB_SYMBOL_TABLE_BIND_PENDING);
            TbSymbolTableBinding_t old = binding;
            CHECK(tb_symbol_table_binding_step(&binding, 0) == TB_SYMBOL_TABLE_BIND_INVALID);
            CHECK(!memcmp(&old, &binding, sizeof(binding)));
            CHECK(tb_symbol_table_binding_step(&binding, 256) == TB_SYMBOL_TABLE_BIND_PENDING);
            CHECK(binding.cursor == 256);
            CHECK(tb_symbol_table_binding_step(&binding, 256) == TB_SYMBOL_TABLE_BIND_PENDING);
            CHECK(binding.cursor == 512);
            CHECK(tb_symbol_table_binding_step(&binding, 256) == TB_SYMBOL_TABLE_BIND_FOUND);
        }
        tb_symbol_tables_destroy(tables);
        if (!count) {
            tables = tb_symbol_tables_create(sections, (TbSymbolTablesSelection_t) {
                .section_index = 1,
                .kind          = TB_SYMBOL_TABLE_ADDRESSES,
                .descriptor    = descriptor
            }, &budget);
            CHECK(tables && fcntl(descriptor, F_SETFD, 0) == 0);
            CHECK(finish(tables) == TB_SYMBOL_UNSUPPORTED);
            CHECK(tb_symbol_tables_failure(tables).read.reason == TB_SYMBOL_FILE_DESCRIPTOR);
            tb_symbol_tables_destroy(tables);
            CHECK(fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0);
        }
        tb_symbol_sections_destroy(owner);
        CHECK(budget.used == 0 && close(descriptor) == 0 && unlink(retained) == 0);
    }
}


int main(void)
{
    for (elf_width = 4; elf_width <= 8; elf_width += 4) {
        for (unsigned endian = 0; endian < 2; ++endian) {
            order = (TbSymbolByteOrder_e)endian;
            for (unsigned offsets = 4; offsets <= 8; offsets += 4) {
                for (unsigned addresses = 1; addresses <= 8; addresses *= 2) {
                    for (unsigned kind = 0; kind < 2; ++kind) {
                        check_profile((TableSpec_t) {
                            .kind      = (TbSymbolIndexedTableKind_e)kind,
                            .offsets   = offsets,
                            .addresses = addresses,
                            .version   = 5,
                            .count     = 4
                        });
                    }
                }
            }
        }
    }
    check_failures();
    check_entry_failures();
    check_empty_and_bounded();
    puts("PASS sequential indexed-table inventories, exact CU bases, retained entry reads "
         "and failure cleanup");
    return 0;
}
