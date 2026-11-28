#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected local fixtures. */
#include "symbol/symbols.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned width;
static TbSymbolByteOrder_e order;
static unsigned char image[131072];
static size_t image_bytes;
static uint64_t payload_base;
static char retained_path[64];
static long fail_after = -1;
static size_t allowance;
static size_t inspected;
static unsigned reads;
static int read_error;
static bool short_read;
static bool eof;
static const char names[] = "\0alpha\0other\0";

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size);
ssize_t __real_pread(int fd, void *bytes, size_t length, off_t offset);
ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset);
void *__real_memchr(const void *bytes, int value, size_t length);
void *__wrap_memchr(const void *bytes, int value, size_t length);

static bool allocation_failed(void)
{
    if (fail_after < 0)
        return false;
    return fail_after-- == 0;
}


void *__wrap_calloc(size_t count, size_t size)
{
    return allocation_failed() ? NULL : __real_calloc(count, size);
}


void *__wrap_malloc(size_t size)
{
    return allocation_failed() ? NULL : __real_malloc(size);
}


ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset)
{
    ++reads;
    inspected += length;
    CHECK(length && inspected <= allowance);
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


void *__wrap_memchr(const void *bytes, int value, size_t length)
{
    inspected += length;
    CHECK(inspected <= allowance);
    return __real_memchr(bytes, value, length);
}


static void put(size_t offset, unsigned bytes, uint64_t value)
{
    CHECK(offset <= sizeof(image) && bytes <= sizeof(image) - offset);
    for (unsigned i = 0; i < bytes; ++i) {
        unsigned index = order == TB_SYMBOL_LITTLE_ENDIAN ? i : bytes - 1 - i;
        image[offset + index] = (unsigned char)(value >> (8 * i));
    }
}


static size_t section_at(unsigned index)
{
    return 128 + index * (width == 4 ? 40U : 64U);
}


static void section(unsigned index, uint32_t type, uint64_t offset, uint64_t bytes)
{
    size_t at = section_at(index);
    put(at + 4, 4, type);
    put(at + (width == 4 ? 16 : 24), width, offset + payload_base);
    put(at + (width == 4 ? 20 : 32), width, bytes);
    put(at + (width == 4 ? 32 : 48), width, 1);
}


static size_t symbol_at(unsigned index)
{
    return 4096 + index * (width == 4 ? 16U : 24U);
}


static void symbol(unsigned index, uint32_t name, uint8_t info, uint16_t reference)
{
    size_t at = symbol_at(index);
    put(at, 4, name);
    put(at + (width == 4 ? 12 : 4), 1, info);
    put(at + (width == 4 ? 14 : 6), 2, reference);
    put(at + (width == 4 ? 4 : 8), width, index ? 0x1010 : 0);
    put(at + (width == 4 ? 8 : 16), width, index ? 4 : 0);
}


static void fixture(void)
{
    memset(image, 0, sizeof(image));
    image_bytes = 16384;
    payload_base = 0;
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
    put(width == 4 ? 48 : 60, 2, 6);
    unsigned stride = width == 4 ? 16 : 24;
    section(1, SHT_SYMTAB, 4096, 3 * stride);
    put(section_at(1) + (width == 4 ? 24 : 40), 4, 2);
    put(section_at(1) + (width == 4 ? 28 : 44), 4, 2);
    put(section_at(1) + (width == 4 ? 36 : 56), width, stride);
    section(2, SHT_STRTAB, 8192, sizeof(names));
    section(4, SHT_DYNSYM, 4352, 3 * stride);
    put(section_at(4) + (width == 4 ? 24 : 40), 4, 2);
    put(section_at(4) + (width == 4 ? 28 : 44), 4, 2);
    put(section_at(4) + (width == 4 ? 36 : 56), width, stride);
    section(5, SHT_PROGBITS, 6144, 64);
    symbol(0, 0, 0, SHN_UNDEF);
    symbol(1, 1, ELF64_ST_INFO(STB_LOCAL, STT_OBJECT), 5);
    symbol(2, 7, ELF64_ST_INFO(STB_GLOBAL, STT_FUNC), SHN_UNDEF);
    memcpy(image + 4352, image + 4096, 3 * stride);
    memcpy(image + 8192, names, sizeof(names));
}


static void index_table(void)
{
    section(3, SHT_SYMTAB_SHNDX, 12288, 12);
    put(section_at(3) + (width == 4 ? 24 : 40), 4, 1);
    put(section_at(3) + (width == 4 ? 36 : 56), width, 4);
}


static int backing(TbSymbolStorageInfo_t *expected)
{
    char path[] = "/tmp/tb-symbol-symbols-XXXXXX";
    int writer = mkstemp(path);
    CHECK(writer >= 0);
    CHECK(!ftruncate(writer, (off_t)image_bytes));
    if (!payload_base) {
        CHECK(pwrite(writer, image, image_bytes, 0) == (ssize_t)image_bytes);
    } else {
        size_t prefix = section_at(6);
        CHECK(pwrite(writer, image, prefix, 0) == (ssize_t)prefix);
        size_t offsets[] = {4096, 4352, 6144, 8192, 12288};
        size_t lengths[] = {72, 72, 64, sizeof(names), 12};
        if (width == 4) {
            lengths[0] = 48;
            lengths[1] = 48;
        }
        for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
            CHECK(pwrite(writer, image + offsets[i], lengths[i], (off_t)(payload_base + offsets[i])) ==
                  (ssize_t)lengths[i]);
        }
        unsigned char target[64] = {0};
        size_t target_bytes = width == 4 ? 40 : 64;
        /* A physical ordinary section whose index equals the raw ABS marker. */
        size_t saved = section_at(5);
        memcpy(target, image + saved, target_bytes);
        for (unsigned i = 0; i < 4; ++i) {
            unsigned index = order == TB_SYMBOL_LITTLE_ENDIAN ? i : 3 - i;
            target[4 + index] = (unsigned char)((unsigned)SHT_NOBITS >> (8 * i));
        }
        CHECK(pwrite(writer, target, target_bytes, (off_t)(128 + UINT64_C(0xfff1) * target_bytes)) ==
              (ssize_t)target_bytes);
    }
    CHECK(!fchmod(writer, 0400));
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0 && !close(writer));
    struct stat info;
    CHECK(!fstat(fd, &info));
    *expected = (TbSymbolStorageInfo_t){
        .key = {
            .device = (uint64_t)info.st_dev,
            .inode  = (uint64_t)info.st_ino
        },
        .bytes = (uint64_t)info.st_size
    };
    CHECK(!retained_path[0]);
    memcpy(retained_path, path, sizeof(path));
    return fd;
}


static void cleanup(int fd)
{
    CHECK(fcntl(fd, F_GETFD) >= 0 && !close(fd) && !unlink(retained_path));
    retained_path[0] = 0;
}


static TbSymbolSections_t *directory(int fd, TbSymbolStorageInfo_t info, TbSymbolBudget_t *budget,
                                      TbSymbolSectionsView_t *view)
{
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, fd, budget);
    CHECK(sections);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_PENDING; ++i) {
        allowance = 65536;
        inspected = 0;
        reads = 0;
        outcome = tb_symbol_sections_step(sections, allowance);
        CHECK(inspected <= allowance && reads <= 1);
    }
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(sections, view));
    return sections;
}


static TbSymbolOutcome_e step(TbSymbolSymbols_t *symbols, size_t limit)
{
    allowance = limit;
    inspected = 0;
    reads = 0;
    TbSymbolOutcome_e outcome = tb_symbol_symbols_step(symbols, limit);
    CHECK(inspected <= limit && reads <= 1);
    return outcome;
}


static TbSymbolOutcome_e finish(TbSymbolSymbols_t *symbols)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = step(symbols, 257);
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


static void no_view(TbSymbolSymbols_t *symbols)
{
    TbSymbolSymbolsView_t view;
    memset(&view, 0xa5, sizeof(view));
    unsigned char original[sizeof(view)];
    memcpy(original, &view, sizeof(view));
    CHECK(!tb_symbol_symbols_view(symbols, &view) && !memcmp(original, &view, sizeof(view)));
}


static void check_fixture(TbSymbolOutcome_e outcome, TbSymbolSymbolsFailure_e reason)
{
    TbSymbolStorageInfo_t info;
    int fd = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 16 * 1024 * 1024));
    TbSymbolSectionsView_t sections;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &sections);
    size_t baseline = budget.used;
    TbSymbolSymbols_t *symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
        .section_index = 1,
        .descriptor    = fd
    }, &budget);
    CHECK(symbols);
    no_view(symbols);
    CHECK(finish(symbols) == outcome && tb_symbol_symbols_failure(symbols).reason == reason);
    if (outcome != TB_SYMBOL_SUCCESS) {
        no_view(symbols);
    } else {
        TbSymbolSymbolsView_t view;
        CHECK(tb_symbol_symbols_view(symbols, &view));
        CHECK(view.kind == TB_SYMBOL_RECORD_SYMTAB && view.table_section == 1 && view.count == 3);
        CHECK(!view.entries[0].has_name && view.entries[0].reference == TB_SYMBOL_ELF_REF_UNDEFINED);
        CHECK(view.entries[1].record_offset == (width == 4 ? 16U : 24U));
        CHECK(view.entries[1].raw.name_offset == 1 && view.entries[1].has_name);
        CHECK(view.entries[1].name_offset == payload_base + 8193);
        if (payload_base) {
            CHECK(view.entries[1].raw.section_index == SHN_XINDEX);
            CHECK(view.entries[1].reference == TB_SYMBOL_ELF_REF_ORDINARY);
            CHECK(view.entries[1].section_index == 0xfff1);
        }
    }
    tb_symbol_symbols_destroy(symbols);
    CHECK(budget.used == baseline && fcntl(fd, F_GETFD) >= 0);
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used);
    cleanup(fd);
}


static void invalid_cases(void)
{
    size_t link = width == 4 ? 24 : 40;
    size_t info = width == 4 ? 28 : 44;
    size_t size = width == 4 ? 20 : 32;
    size_t stride = width == 4 ? 36 : 56;
    size_t index = width == 4 ? 14 : 6;
    fixture();
    put(section_at(1) + link, 4, 0);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_STRINGS);
    fixture();
    put(section_at(1) + link, 4, 5);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_STRINGS);
    fixture();
    put(section_at(1) + info, 4, 4);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_LOCAL_ORDER);
    fixture();
    put(section_at(1) + stride, width, 1);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_TABLE);
    fixture();
    put(section_at(1) + size, width, 0);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_TABLE);
    fixture();
    put(symbol_at(0), 4, 1);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_NULL_ENTRY);
    fixture();
    put(symbol_at(1) + (width == 4 ? 12 : 4), 1, ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT));
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_LOCAL_ORDER);
    fixture();
    put(symbol_at(2) + (width == 4 ? 12 : 4), 1, ELF64_ST_INFO(STB_LOCAL, STT_FUNC));
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_LOCAL_ORDER);
    fixture();
    put(symbol_at(1) + index, 2, 6);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_INDEX_VALUE);
    fixture();
    put(symbol_at(1), 4, sizeof(names));
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_NAME);
    fixture();
    image[8192] = 'X';
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_STRINGS);
    fixture();
    image[8192 + sizeof(names) - 1] = 'X';
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_STRINGS);
    fixture();
    put(section_at(1) + 8, width, SHF_COMPRESSED);
    check_fixture(TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_SYMBOLS_TABLE);
    fixture();
    put(section_at(2) + 8, width, SHF_COMPRESSED);
    check_fixture(TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_SYMBOLS_STRINGS);
    fixture();
    put(symbol_at(1) + index, 2, SHN_XINDEX);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_INDEX_MISSING);
    fixture();
    index_table();
    put(section_at(3) + size, width, 8);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_INDEX_TABLE);
    fixture();
    index_table();
    put(section_at(3) + size, width, 24);
    put(section_at(3) + stride, width, 8);
    check_fixture(TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_SYMBOLS_INDEX_TABLE);
    fixture();
    index_table();
    put(12288 + 4, 4, 5);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_INDEX_VALUE);
    fixture();
    index_table();
    put(section_at(3) + 8, width, SHF_COMPRESSED);
    put(section_at(3) + size, width, 8);
    check_fixture(TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_SYMBOLS_INDEX_TABLE);
    fixture();
    index_table();
    put(section_at(4) + 4, 4, SHT_SYMTAB_SHNDX);
    put(section_at(4) + link, 4, 1);
    put(section_at(4) + info, 4, 0);
    put(section_at(4) + size, width, 12);
    put(section_at(4) + stride, width, 4);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_INDEX_CONFLICT);
}


static void metadata_cases(void)
{
    uint16_t indexes[] = {SHN_UNDEF, SHN_ABS, SHN_COMMON, SHN_LOPROC, 5};
    TbSymbolElfReference_e references[] = {
        TB_SYMBOL_ELF_REF_UNDEFINED,
        TB_SYMBOL_ELF_REF_ABSOLUTE,
        TB_SYMBOL_ELF_REF_COMMON,
        TB_SYMBOL_ELF_REF_RESERVED,
        TB_SYMBOL_ELF_REF_ORDINARY
    };
    for (size_t i = 0; i < sizeof(indexes) / sizeof(indexes[0]); ++i) {
        fixture();
        symbol(1, 3, ELF64_ST_INFO(STB_LOCAL, STT_TLS), indexes[i]);
        put(symbol_at(1) + (width == 4 ? 13 : 5), 1, 0xab);
        TbSymbolStorageInfo_t info;
        int fd = backing(&info);
        TbSymbolBudget_t budget;
        CHECK(tb_symbol_budget_init(&budget, 512 * 1024));
        TbSymbolSectionsView_t sections;
        TbSymbolSections_t *owner = directory(fd, info, &budget, &sections);
        TbSymbolSymbols_t *symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
            .section_index = 1,
            .descriptor    = fd
        }, &budget);
        CHECK(symbols && finish(symbols) == TB_SYMBOL_SUCCESS);
        TbSymbolSymbolsView_t view;
        CHECK(tb_symbol_symbols_view(symbols, &view));
        const TbSymbolElfEntry_t *entry = &view.entries[1];
        CHECK(entry->reference == references[i] && entry->raw.section_index == indexes[i]);
        CHECK(entry->section_index == (references[i] == TB_SYMBOL_ELF_REF_ORDINARY ? 5U : 0U));
        CHECK(entry->raw.info == ELF64_ST_INFO(STB_LOCAL, STT_TLS) && entry->raw.other == 0xab);
        CHECK(entry->has_name && entry->name_bytes == 3 && entry->name_offset == 8195);
        tb_symbol_symbols_destroy(symbols);
        symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
            .section_index = 4,
            .descriptor    = fd
        }, &budget);
        CHECK(symbols && finish(symbols) == TB_SYMBOL_SUCCESS);
        CHECK(tb_symbol_symbols_view(symbols, &view));
        CHECK(view.kind == TB_SYMBOL_RECORD_DYNSYM && view.table_section == 4);
        CHECK(view.entries[1].raw.name_offset == 1 &&
              view.entries[1].record_offset == (width == 4 ? 16U : 24U));
        tb_symbol_symbols_destroy(symbols);
        tb_symbol_sections_destroy(owner);
        CHECK(!budget.used);
        cleanup(fd);
    }
}


static void long_name(void)
{
    fixture();
    const size_t length = 70000;
    image_bytes = 8192 + length + 2;
    memset(image + 8193, 'X', length);
    image[8193 + length] = 0;
    put(section_at(2) + (width == 4 ? 20 : 32), width, length + 2);
    put(symbol_at(2), 4, 2);
    TbSymbolStorageInfo_t info;
    int fd = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 512 * 1024));
    TbSymbolSectionsView_t sections;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &sections);
    TbSymbolSymbols_t *symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
        .section_index = 1,
        .descriptor    = fd
    }, &budget);
    CHECK(symbols && finish(symbols) == TB_SYMBOL_SUCCESS);
    TbSymbolSymbolsView_t view;
    CHECK(tb_symbol_symbols_view(symbols, &view));
    CHECK(view.entries[1].name_bytes == length && view.entries[2].name_bytes == length - 1);
    CHECK(view.entries[1].name_offset == 8193 && view.entries[2].name_offset == 8194);
    tb_symbol_symbols_destroy(symbols);
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used);
    cleanup(fd);
}


static void empty_strings(void)
{
    fixture();
    put(section_at(2) + (width == 4 ? 20 : 32), width, 0);
    put(symbol_at(1), 4, 0);
    put(symbol_at(2), 4, 0);
    TbSymbolStorageInfo_t info;
    int fd = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 512 * 1024));
    TbSymbolSectionsView_t sections;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &sections);
    TbSymbolSymbols_t *symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
        .section_index = 1,
        .descriptor    = fd
    }, &budget);
    CHECK(symbols && finish(symbols) == TB_SYMBOL_SUCCESS);
    TbSymbolSymbolsView_t view;
    CHECK(tb_symbol_symbols_view(symbols, &view));
    for (uint64_t i = 0; i < view.count; ++i)
        CHECK(!view.entries[i].has_name);
    tb_symbol_symbols_destroy(symbols);
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used);
    cleanup(fd);
}


static void extended_reference(void)
{
    fixture();
    index_table();
    payload_base = 8 * 1024 * 1024;
    image_bytes = (size_t)payload_base + 16384;
    put(width == 4 ? 48 : 60, 2, 0);
    put(section_at(0) + (width == 4 ? 20 : 32), width, 70000);
    for (unsigned i = 1; i < 6; ++i) {
        uint64_t offset = i == 1 ? 4096 : i == 2 ? 8192 : i == 3 ? 12288 : i == 4 ? 4352 : 6144;
        put(section_at(i) + (width == 4 ? 16 : 24), width, payload_base + offset);
    }
    put(symbol_at(1) + (width == 4 ? 14 : 6), 2, SHN_XINDEX);
    put(12288 + 4, 4, 0xfff1);
    check_fixture(TB_SYMBOL_SUCCESS, TB_SYMBOL_SYMBOLS_NONE);
    put(12288 + 4, 4, 70000);
    check_fixture(TB_SYMBOL_MALFORMED, TB_SYMBOL_SYMBOLS_INDEX_VALUE);
}


static void ownership_and_faults(void)
{
    fixture();
    TbSymbolStorageInfo_t info;
    int fd = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 512 * 1024));
    TbSymbolSectionsView_t sections;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &sections);
    size_t baseline = budget.used;
    for (long failure = 0; failure < 3; ++failure) {
        fail_after = failure;
        TbSymbolSymbols_t *symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
            .section_index = 1,
            .descriptor    = fd
        }, &budget);
        if (symbols) {
            CHECK(finish(symbols) == TB_SYMBOL_RESOURCE_FAILURE);
            no_view(symbols);
            tb_symbol_symbols_destroy(symbols);
        }
        CHECK(budget.used == baseline);
    }
    fail_after = -1;
    TbSymbolSymbols_t *symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
        .section_index = 1,
        .descriptor    = fd
    }, &budget);
    CHECK(symbols);
    budget.limit = budget.used;
    CHECK(finish(symbols) == TB_SYMBOL_RESOURCE_FAILURE);
    no_view(symbols);
    tb_symbol_symbols_destroy(symbols);
    CHECK(budget.used == baseline);
    budget.limit = 512 * 1024;
    for (unsigned stage = 0; stage < 250; stage += 3) {
        symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
            .section_index = 1,
            .descriptor    = fd
        }, &budget);
        CHECK(symbols);
        for (unsigned i = 0; i < stage; ++i) {
            TbSymbolOutcome_e outcome = step(symbols, 64);
            CHECK(outcome == TB_SYMBOL_PENDING || outcome == TB_SYMBOL_SUCCESS);
        }
        tb_symbol_symbols_destroy(symbols);
        CHECK(budget.used == baseline && fcntl(fd, F_GETFD) >= 0);
    }
    symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
        .section_index = 1,
        .descriptor    = fd
    }, &budget);
    CHECK(symbols);
    read_error = EINTR;
    bool interrupted = false;
    for (unsigned i = 0; i < 32 && !interrupted; ++i) {
        uint64_t before = tb_symbol_symbols_progress(symbols);
        CHECK(step(symbols, 64) == TB_SYMBOL_PENDING);
        if (reads) {
            CHECK(before == tb_symbol_symbols_progress(symbols));
            interrupted = true;
        }
    }
    CHECK(interrupted && !read_error);
    short_read = true;
    CHECK(finish(symbols) == TB_SYMBOL_SUCCESS);
    short_read = false;
    tb_symbol_symbols_destroy(symbols);
    read_error = EIO;
    symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
        .section_index = 1,
        .descriptor    = fd
    }, &budget);
    CHECK(symbols && finish(symbols) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_symbols_failure(symbols).read.system_error == EIO);
    no_view(symbols);
    tb_symbol_symbols_destroy(symbols);
    eof = true;
    symbols = tb_symbol_symbols_create(sections, (TbSymbolTableSelection_t){
        .section_index = 1,
        .descriptor    = fd
    }, &budget);
    CHECK(symbols && finish(symbols) == TB_SYMBOL_OPERATIONAL_FAILURE);
    no_view(symbols);
    tb_symbol_symbols_destroy(symbols);
    eof = false;
    CHECK(budget.used == baseline);
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used);
    cleanup(fd);
}


int main(void)
{
    for (unsigned bytes = 4; bytes <= 8; bytes += 4) {
        width = bytes;
        for (unsigned byte_order = 0; byte_order < 2; ++byte_order) {
            order = byte_order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN;
            fixture();
            check_fixture(TB_SYMBOL_SUCCESS, TB_SYMBOL_SYMBOLS_NONE);
            invalid_cases();
            metadata_cases();
            long_name();
            empty_strings();
        }
    }
    extended_reference();
    ownership_and_faults();
    puts("PASS bounded ELF symbol/string/index dependencies, physical distinctions and failure isolation");
    return 0;
}
