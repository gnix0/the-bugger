#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected retained-file fixtures. */
#include "symbol/units.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    unsigned version;
    unsigned offsets;
    unsigned type;
    unsigned addresses;
    uint64_t total;
} UnitSpec_t;

static unsigned char image[131072];
static unsigned width;
static TbSymbolByteOrder_e order;
static uint64_t payload_bytes;
static char retained_path[64];
static long fail_after = -1;
static int read_error;
static bool short_read;
static bool eof;
static size_t inspected;
static unsigned reads;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size);
ssize_t __real_pread(int fd, void *bytes, size_t length, off_t offset);
ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset);

void *__wrap_calloc(size_t count, size_t size)
{
    return fail_after >= 0 && fail_after-- == 0 ? NULL : __real_calloc(count, size);
}


void *__wrap_malloc(size_t size)
{
    return fail_after >= 0 && fail_after-- == 0 ? NULL : __real_malloc(size);
}


ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset)
{
    ++reads;
    inspected += length;
    CHECK(length && inspected <= 64);
    if (read_error) {
        errno = read_error;
        read_error = 0;
        return -1;
    }
    if (eof)
        return 0;
    if (short_read && length > 1)
        length = 1;
    return __real_pread(fd, bytes, length, offset);
}


static void put(size_t at, unsigned bytes, uint64_t value)
{
    CHECK(bytes <= 8 && at <= sizeof(image) && bytes <= sizeof(image) - at);
    for (unsigned i = 0; i < bytes; ++i) {
        unsigned index = order == TB_SYMBOL_LITTLE_ENDIAN ? i : bytes - 1 - i;
        image[at + index] = (unsigned char)(value >> (8 * i));
    }
}


static size_t selected(void)
{
    return 128 + (width == 4 ? 40U : 64U);
}


static void fixture(void)
{
    memset(image, 0, sizeof(image));
    payload_bytes = 0;
    memcpy(image, ELFMAG, SELFMAG);
    image[EI_CLASS] = width == 4 ? ELFCLASS32 : ELFCLASS64;
    image[EI_DATA] = order == TB_SYMBOL_LITTLE_ENDIAN ? ELFDATA2LSB : ELFDATA2MSB;
    image[EI_VERSION] = EV_CURRENT;
    put(16, 2, ET_EXEC);
    put(18, 2, EM_RISCV);
    put(20, 4, EV_CURRENT);
    put(width == 4 ? 32 : 40, width, 128);
    put(width == 4 ? 40 : 52, 2, width == 4 ? 52 : 64);
    put(width == 4 ? 46 : 58, 2, width == 4 ? 40 : 64);
    put(width == 4 ? 48 : 60, 2, 2);
    put(selected() + 4, 4, SHT_PROGBITS);
    put(selected() + (width == 4 ? 16 : 24), width, 512);
    put(selected() + (width == 4 ? 32 : 48), width, 1);
}


static uint64_t append(UnitSpec_t spec)
{
    uint64_t relative = payload_bytes;
    size_t at = 512 + (size_t)relative;
    unsigned initial = spec.offsets == 4 ? 4 : 12;
    unsigned base = spec.version == 5 ? 4 + spec.offsets : 3 + spec.offsets;
    unsigned header = initial + base + (spec.type == 2 ? 8 + spec.offsets : 0);
    uint64_t total = spec.total ? spec.total : header + 2;
    CHECK(total > header && total <= sizeof(image) - at);
    if (spec.offsets == 4) {
        put(at, 4, total - 4);
    } else {
        put(at, 4, UINT32_MAX);
        put(at + 4, 8, total - 12);
    }
    put(at + initial, 2, spec.version);
    if (spec.version == 5) {
        put(at + initial + 2, 1, spec.type);
        put(at + initial + 3, 1, spec.addresses);
        put(at + initial + 4, spec.offsets, 0x1234);
    } else {
        put(at + initial + 2, spec.offsets, 0x1234);
        put(at + initial + 2 + spec.offsets, 1, spec.addresses);
    }
    if (spec.type == 2) {
        put(at + initial + base, 8, UINT64_C(0x0123456789abcdef));
        put(at + initial + base + 8, spec.offsets, header + 1);
    }
    image[at + header] = 1;
    payload_bytes += total;
    put(selected() + (width == 4 ? 20 : 32), width, payload_bytes);
    return relative;
}


static int backing(TbSymbolStorageInfo_t *expected)
{
    char path[] = "/tmp/tb-symbol-units-XXXXXX";
    int writer = mkstemp(path);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(!fchmod(writer, 0400) && !close(writer));
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    struct stat info;
    CHECK(!fstat(fd, &info));
    *expected = (TbSymbolStorageInfo_t){
        .key = {
            .device = (uint64_t)info.st_dev,
            .inode  = (uint64_t)info.st_ino
        },
        .bytes = sizeof(image)
    };
    CHECK(strlen(path) < sizeof(retained_path));
    strcpy(retained_path, path);
    return fd;
}


static TbSymbolSections_t *directory(int fd, TbSymbolStorageInfo_t info, TbSymbolBudget_t *budget,
                                     TbSymbolSectionsView_t *view)
{
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, fd, budget);
    CHECK(sections);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 10000 && outcome == TB_SYMBOL_PENDING; ++turn) {
        inspected = 0;
        outcome = tb_symbol_sections_step(sections, 64);
    }
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(sections, view));
    return sections;
}


static TbSymbolOutcome_e step(TbSymbolUnits_t *units)
{
    uint64_t before = tb_symbol_units_progress(units);
    inspected = 0;
    reads = 0;
    TbSymbolOutcome_e outcome = tb_symbol_units_step(units, 64);
    CHECK(reads <= 1 && inspected <= 64 && tb_symbol_units_progress(units) >= before);
    return outcome;
}


static TbSymbolOutcome_e finish(TbSymbolUnits_t *units)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 1000000 && outcome == TB_SYMBOL_PENDING; ++turn)
        outcome = step(units);
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


static void no_view(TbSymbolUnits_t *units)
{
    TbSymbolUnitsView_t view;
    unsigned char original[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(original, &view, sizeof(view));
    CHECK(!tb_symbol_units_view(units, &view) && !memcmp(original, &view, sizeof(view)));
}


static void check_scope(TbSymbolDwarfSection_e kind, TbSymbolOutcome_e expected,
                        TbSymbolUnitsFailure_e reason, uint64_t count, uint64_t unsupported)
{
    TbSymbolStorageInfo_t info;
    int fd = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
    TbSymbolSectionsView_t view;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &view);
    size_t baseline = budget.used;
    TbSymbolUnits_t *units = tb_symbol_units_create(view, (TbSymbolUnitSelection_t){
        .section_index = 1,
        .kind          = kind,
        .descriptor    = fd
    }, &budget);
    CHECK(units);
    no_view(units);
    CHECK(finish(units) == expected && tb_symbol_units_failure(units).reason == reason);
    if (expected == TB_SYMBOL_SUCCESS) {
        TbSymbolUnitsView_t completed;
        CHECK(tb_symbol_units_view(units, &completed));
        CHECK(completed.count == count && completed.unsupported == unsupported && completed.kind == kind);
        CHECK(completed.section_index == 1 && completed.backing.key.inode == info.key.inode);
        uint64_t cursor = 0;
        uint64_t unavailable = 0;
        for (uint64_t i = 0; i < completed.count; ++i) {
            const TbSymbolUnitEntry_t *entry = &completed.entries[i];
            CHECK(entry->offset == cursor && entry->extent.total_bytes);
            if (entry->header_available) {
                CHECK(entry->header.extent.end_offset == entry->extent.end_offset);
                CHECK(entry->header.version == 4 || entry->header.version == 5);
            } else {
                ++unavailable;
                CHECK(!entry->header.version && !entry->header.header_bytes);
            }
            cursor = entry->extent.end_offset;
        }
        CHECK(cursor == payload_bytes && unavailable == unsupported);
    } else {
        no_view(units);
    }
    tb_symbol_units_destroy(units);
    CHECK(budget.used == baseline && fcntl(fd, F_GETFD) >= 0);
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used && !close(fd) && !unlink(retained_path));
}


static void cases(void)
{
    fixture();
    append((UnitSpec_t){
        .version = 4,
        .offsets = 4,
        .type = 1,
        .addresses = 8
    });
    append((UnitSpec_t){
        .version = 5,
        .offsets = 8,
        .type = 3,
        .addresses = 4,
        .total = 70000
    });
    append((UnitSpec_t){
        .version = 5,
        .offsets = 4,
        .type = 2,
        .addresses = 8
    });
    check_scope(TB_SYMBOL_DWARF_INFO, TB_SYMBOL_SUCCESS, TB_SYMBOL_UNITS_NONE, 3, 0);
    image[512 + 13 + 12 + 2] = 4;
    check_scope(TB_SYMBOL_DWARF_INFO, TB_SYMBOL_SUCCESS, TB_SYMBOL_UNITS_NONE, 3, 1);
    image[512 + 13 + 12 + 2] = 3;
    put(512 + 70013, 4, UINT32_C(0xfffffff0));
    check_scope(TB_SYMBOL_DWARF_INFO, TB_SYMBOL_MALFORMED, TB_SYMBOL_UNITS_LENGTH, 0, 0);
    fixture();
    append((UnitSpec_t){
        .version = 4,
        .offsets = 8,
        .type = 2,
        .addresses = 4
    });
    check_scope(TB_SYMBOL_DWARF_TYPES, TB_SYMBOL_SUCCESS, TB_SYMBOL_UNITS_NONE, 1, 0);
    image[512 + 12] = 0;
    image[512 + 13] = 0;
    check_scope(TB_SYMBOL_DWARF_TYPES, TB_SYMBOL_MALFORMED, TB_SYMBOL_UNITS_HEADER, 0, 0);
    fixture();
    check_scope(TB_SYMBOL_DWARF_INFO, TB_SYMBOL_SUCCESS, TB_SYMBOL_UNITS_NONE, 0, 0);
    put(selected() + 8, width, SHF_COMPRESSED);
    check_scope(TB_SYMBOL_DWARF_INFO, TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_UNITS_SECTION, 0, 0);
    fixture();
    for (unsigned i = 0; i < 5000; ++i) {
        append((UnitSpec_t){
            .version = 5,
            .offsets = 4,
            .type = 1,
            .addresses = 8
        });
    }
    check_scope(TB_SYMBOL_DWARF_INFO, TB_SYMBOL_SUCCESS, TB_SYMBOL_UNITS_NONE, 5000, 0);
}


static void ownership(void)
{
    fixture();
    append((UnitSpec_t){
        .version = 5,
        .offsets = 8,
        .type = 1,
        .addresses = 8
    });
    TbSymbolStorageInfo_t info;
    int fd = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 512 * 1024));
    TbSymbolSectionsView_t view;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &view);
    size_t baseline = budget.used;
    TbSymbolUnitSelection_t selection = {
        .section_index = 1,
        .kind          = TB_SYMBOL_DWARF_INFO,
        .descriptor    = fd
    };
    for (long failure = 0; failure < 3; ++failure) {
        fail_after = failure;
        TbSymbolUnits_t *units = tb_symbol_units_create(view, selection, &budget);
        if (units) {
            CHECK(finish(units) == TB_SYMBOL_RESOURCE_FAILURE);
            no_view(units);
            tb_symbol_units_destroy(units);
        }
        CHECK(budget.used == baseline);
    }
    fail_after = -1;
    TbSymbolUnits_t *units = tb_symbol_units_create(view, selection, &budget);
    CHECK(units);
    budget.limit = budget.used;
    CHECK(finish(units) == TB_SYMBOL_RESOURCE_FAILURE);
    no_view(units);
    tb_symbol_units_destroy(units);
    CHECK(budget.used == baseline);
    budget.limit = 512 * 1024;
    for (unsigned stage = 0; stage < 100; ++stage) {
        units = tb_symbol_units_create(view, selection, &budget);
        CHECK(units);
        for (unsigned i = 0; i < stage; ++i) {
            TbSymbolOutcome_e outcome = step(units);
            CHECK(outcome == TB_SYMBOL_PENDING || outcome == TB_SYMBOL_SUCCESS);
            if (outcome == TB_SYMBOL_PENDING)
                no_view(units);
        }
        tb_symbol_units_destroy(units);
        CHECK(budget.used == baseline);
    }
    units = tb_symbol_units_create(view, selection, &budget);
    CHECK(units);
    read_error = EINTR;
    bool interrupted = false;
    for (unsigned i = 0; i < 32 && !interrupted; ++i) {
        uint64_t before = tb_symbol_units_progress(units);
        CHECK(step(units) == TB_SYMBOL_PENDING);
        if (reads) {
            CHECK(before == tb_symbol_units_progress(units));
            interrupted = true;
        }
    }
    CHECK(interrupted && !read_error);
    short_read = true;
    CHECK(finish(units) == TB_SYMBOL_SUCCESS);
    short_read = false;
    tb_symbol_units_destroy(units);
    read_error = EIO;
    units = tb_symbol_units_create(view, selection, &budget);
    CHECK(units && finish(units) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_units_failure(units).read.system_error == EIO);
    no_view(units);
    tb_symbol_units_destroy(units);
    eof = true;
    units = tb_symbol_units_create(view, selection, &budget);
    CHECK(units && finish(units) == TB_SYMBOL_OPERATIONAL_FAILURE);
    no_view(units);
    tb_symbol_units_destroy(units);
    eof = false;
    CHECK(budget.used == baseline);
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used && !close(fd) && !unlink(retained_path));
}


static void empty_descriptor(void)
{
    fixture();
    TbSymbolStorageInfo_t info;
    int fd = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 512 * 1024));
    TbSymbolSectionsView_t view;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &view);
    size_t baseline = budget.used;
    int replacement = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(replacement >= 0);
    TbSymbolUnits_t *units = tb_symbol_units_create(view, (TbSymbolUnitSelection_t){
        .section_index = 1,
        .kind          = TB_SYMBOL_DWARF_INFO,
        .descriptor    = replacement
    }, &budget);
    CHECK(units && finish(units) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_units_failure(units).reason == TB_SYMBOL_UNITS_READ);
    CHECK(tb_symbol_units_failure(units).unit_offset == UINT64_MAX);
    no_view(units);
    tb_symbol_units_destroy(units);
    CHECK(budget.used == baseline && !close(replacement));
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used && !close(fd) && !unlink(retained_path));
}


int main(void)
{
    for (width = 4; width <= 8; width += 4) {
        for (unsigned byte_order = 0; byte_order < 2; ++byte_order) {
            order = byte_order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN;
            cases();
        }
    }
    width = 8;
    ownership();
    empty_descriptor();
    puts("PASS complete bounded DWARF header inventories, coverage, accounting and failure isolation");
    return 0;
}
