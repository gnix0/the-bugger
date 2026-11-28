#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected local fixtures. */
#include "symbol/sections.h"
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
static long fail_after = -1;
static int read_error;
static bool short_read;
static bool eof;
static size_t allowance;
static size_t inspected;
static unsigned reads;
static char retained_path[64];
static const char names[] = "\0.alpha\0.shstrtab\0.beta\0";

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


static void section(unsigned index, uint32_t type, uint32_t name, uint64_t offset)
{
    size_t at = section_at(index);
    put(at, 4, name);
    put(at + 4, 4, type);
    put(at + (width == 4 ? 16 : 24), width, offset);
    put(at + (width == 4 ? 20 : 32), width, 32);
    put(at + (width == 4 ? 32 : 48), width, 1);
}


static void fixture(void)
{
    memset(image, 0, sizeof(image));
    image_bytes = 10000;
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
    put(width == 4 ? 50 : 62, 2, 3);
    section(1, SHT_PROGBITS, 1, 4352);
    section(2, SHT_NOBITS, 18, width == 4 ? UINT32_MAX : UINT64_MAX);
    section(3, SHT_STRTAB, 8, 8192);
    section(4, SHT_PROGBITS, 1, 4096);
    put(section_at(3) + (width == 4 ? 20 : 32), width, sizeof(names));
    /* An inactive record's other fields carry no payload or name meaning. */
    put(section_at(5), 4, UINT32_MAX);
    put(section_at(5) + 8, width, SHF_GROUP | SHF_COMPRESSED);
    put(section_at(5) + (width == 4 ? 16 : 24), width, width == 4 ? UINT32_MAX : UINT64_MAX);
    memcpy(image + 8192, names, sizeof(names));
}


static int backing(TbSymbolStorageInfo_t *expected)
{
    char path[] = "/tmp/tb-symbol-sections-XXXXXX";
    int writer = mkstemp(path);
    CHECK(writer >= 0);
    size_t written = image_bytes < sizeof(image) ? image_bytes : sizeof(image);
    CHECK(write(writer, image, written) == (ssize_t)written);
    if (image_bytes > written)
        CHECK(!ftruncate(writer, (off_t)image_bytes));
    CHECK(!fchmod(writer, 0400));
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0 && !close(writer));
    struct stat info;
    CHECK(!fstat(descriptor, &info));
    *expected = (TbSymbolStorageInfo_t){
        .key = {
            .device = (uint64_t)info.st_dev,
            .inode  = (uint64_t)info.st_ino
        },
        .bytes = (uint64_t)info.st_size
    };
    /* Retain a name while borrowed descriptors close; final unlink is explicit. */
    CHECK(!retained_path[0]);
    memcpy(retained_path, path, sizeof(path));
    return descriptor;
}


static void cleanup(int descriptor)
{
    CHECK(fcntl(descriptor, F_GETFD) >= 0);
    CHECK(!close(descriptor) && !unlink(retained_path));
    retained_path[0] = 0;
}


static TbSymbolOutcome_e step(TbSymbolSections_t *sections, size_t limit)
{
    allowance = limit;
    inspected = 0;
    reads = 0;
    TbSymbolOutcome_e outcome = tb_symbol_sections_step(sections, limit);
    CHECK(inspected <= limit && reads <= 1);
    return outcome;
}


static TbSymbolOutcome_e finish(TbSymbolSections_t *sections, size_t limit)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = step(sections, limit);
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


static void no_view(TbSymbolSections_t *sections)
{
    TbSymbolSectionsView_t view;
    memset(&view, 0xa5, sizeof(view));
    unsigned char original[sizeof(view)];
    memcpy(original, &view, sizeof(view));
    CHECK(!tb_symbol_sections_view(sections, &view));
    CHECK(!memcmp(original, &view, sizeof(view)));
}


static void rejected(TbSymbolOutcome_e outcome, TbSymbolSectionsFailure_e reason)
{
    TbSymbolStorageInfo_t info;
    int descriptor = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 256 * 1024));
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections && finish(sections, 64) == outcome);
    CHECK(tb_symbol_sections_failure(sections).reason == reason);
    no_view(sections);
    tb_symbol_sections_destroy(sections);
    CHECK(!budget.used);
    cleanup(descriptor);
}


static void success(TbSymbolSectionNames_e expected_names, size_t limit)
{
    TbSymbolStorageInfo_t info;
    int descriptor = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 256 * 1024));
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_PENDING; ++i) {
        no_view(sections);
        outcome = step(sections, limit);
    }
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolSectionsView_t view;
    CHECK(tb_symbol_sections_view(sections, &view));
    CHECK(view.count == 6 && view.names == expected_names);
    CHECK(view.header->address_bytes == width && view.header->byte_order == order);
    CHECK(view.entries[1].header.offset == 4352 && view.entries[4].header.offset == 4096);
    CHECK(view.entries[2].header.type == SHT_NOBITS && view.entries[5].header.type == SHT_NULL);
    CHECK(!view.entries[0].has_name && !view.entries[5].has_name);
    if (expected_names == TB_SYMBOL_SECTION_NAMES_AVAILABLE && view.entries[1].header.name_offset) {
        CHECK(view.entries[1].has_name && view.entries[4].has_name);
        CHECK(view.entries[1].name_bytes == 6 && view.entries[4].name_bytes == 6);
        CHECK(view.entries[1].name_offset == 8193 && view.entries[4].name_offset == 8193);
        CHECK(view.entries[2].name_bytes == 5 && view.entries[3].name_bytes == 9);
    } else {
        for (uint64_t i = 0; i < view.count; ++i)
            CHECK(!view.entries[i].has_name);
    }
    CHECK(tb_symbol_sections_progress(sections) > 0);
    tb_symbol_sections_destroy(sections);
    CHECK(!budget.used);
    cleanup(descriptor);
}


static void invalid_formats(void)
{
    fixture();
    image_bytes = 8;
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_HEADER);
    fixture();
    put(16, 2, ET_REL);
    rejected(TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_SECTIONS_HEADER);
    fixture();
    put(width == 4 ? 50 : 62, 2, 6);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_LAYOUT);
    fixture();
    put(section_at(0) + 4, 4, SHT_PROGBITS);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_LAYOUT);
    fixture();
    put(width == 4 ? 32 : 40, width, width == 4 ? UINT32_MAX : UINT64_MAX);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_READ);
    fixture();
    put(width == 4 ? 46 : 58, 2, (width == 4 ? 40 : 64) + 1);
    rejected(TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_SECTIONS_LAYOUT);
}


static void invalid_records(void)
{
    size_t offset_at = width == 4 ? 16 : 24;
    fixture();
    put(section_at(1) + offset_at, width, 4096 + 16);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_OVERLAP);
    fixture();
    put(section_at(1) + offset_at, width, 128);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_OVERLAP);
    fixture();
    put(section_at(1) + offset_at, width, 0);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_OVERLAP);
    fixture();
    put(section_at(1) + 8, width, SHF_GROUP);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_RECORD);
    fixture();
    put(section_at(2) + 8, width, SHF_COMPRESSED);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_RECORD);
    fixture();
    put(section_at(3) + 8, width, SHF_COMPRESSED | SHF_ALLOC);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_RECORD);
}


static void name_cases(void)
{
    fixture();
    image[8192] = 'X';
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_NAMES);
    fixture();
    image[8192 + sizeof(names) - 1] = 'X';
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_NAMES);
    fixture();
    put(section_at(1), 4, sizeof(names));
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_NAMES);
    fixture();
    put(section_at(3) + 4, 4, SHT_PROGBITS);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_NAMES);
    fixture();
    put(width == 4 ? 50 : 62, 2, 0);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_NAMES);
    for (unsigned i = 1; i < 5; ++i)
        put(section_at(i), 4, 0);
    success(TB_SYMBOL_SECTION_NAMES_ABSENT, 64);
    fixture();
    put(section_at(3) + 8, width, SHF_COMPRESSED);
    image[8192] = 'X';
    success(TB_SYMBOL_SECTION_NAMES_COMPRESSED, 65536);
    fixture();
    for (unsigned i = 1; i < 5; ++i)
        put(section_at(i), 4, 0);
    put(section_at(3) + (width == 4 ? 20 : 32), width, 0);
    success(TB_SYMBOL_SECTION_NAMES_AVAILABLE, 64);
}


static void long_names(void)
{
    fixture();
    const size_t length = 70000;
    image_bytes = 8192 + length + 2;
    memset(image + 8193, 'X', length);
    image[8193 + length] = 0;
    put(section_at(3) + (width == 4 ? 20 : 32), width, length + 2);
    put(section_at(2), 4, 0);
    put(section_at(3), 4, 0);
    put(section_at(4), 4, 2);
    TbSymbolStorageInfo_t info;
    int descriptor = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 256 * 1024));
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections && finish(sections, 257) == TB_SYMBOL_SUCCESS);
    TbSymbolSectionsView_t view;
    CHECK(tb_symbol_sections_view(sections, &view));
    CHECK(view.entries[1].has_name && view.entries[1].name_bytes == length);
    CHECK(view.entries[4].has_name && view.entries[4].name_bytes == length - 1);
    CHECK(view.entries[1].name_offset == 8193 && view.entries[4].name_offset == 8194);
    tb_symbol_sections_destroy(sections);
    CHECK(!budget.used);
    cleanup(descriptor);
}


static void shuffled_records(void)
{
    fixture();
    memset(image + 64, 0, sizeof(image) - 64);
    const unsigned count = 129;
    image_bytes = 32768;
    put(width == 4 ? 48 : 60, 2, count);
    put(width == 4 ? 50 : 62, 2, 0);
    for (unsigned i = 1; i < count; ++i)
        section(i, SHT_PROGBITS, 0, 16384 + ((i * 37) % 128) * 64);
    TbSymbolStorageInfo_t info;
    int descriptor = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 256 * 1024));
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections && finish(sections, 64) == TB_SYMBOL_SUCCESS);
    TbSymbolSectionsView_t view;
    CHECK(tb_symbol_sections_view(sections, &view) && view.count == count);
    CHECK(view.names == TB_SYMBOL_SECTION_NAMES_ABSENT);
    for (unsigned i = 1; i < count; ++i)
        CHECK(view.entries[i].header.offset == 16384 + ((i * 37) % 128) * 64);
    tb_symbol_sections_destroy(sections);
    CHECK(!budget.used);
    cleanup(descriptor);
    put(section_at(73) + (width == 4 ? 16 : 24), width, 16384 + ((17 * 37) % 128) * 64);
    rejected(TB_SYMBOL_MALFORMED, TB_SYMBOL_SECTIONS_OVERLAP);
}


static void extended_count(void)
{
    fixture();
    memset(image + 64, 0, sizeof(image) - 64);
    const uint64_t count = 70000;
    image_bytes = 128 + (size_t)count * (width == 4 ? 40 : 64);
    put(width == 4 ? 48 : 60, 2, 0);
    put(width == 4 ? 50 : 62, 2, 0);
    put(section_at(0) + (width == 4 ? 20 : 32), width, count);
    rejected(TB_SYMBOL_RESOURCE_FAILURE, TB_SYMBOL_SECTIONS_RESOURCE);
    TbSymbolStorageInfo_t info;
    int descriptor = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 16 * 1024 * 1024));
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections && finish(sections, 65536) == TB_SYMBOL_SUCCESS);
    TbSymbolSectionsView_t view;
    CHECK(tb_symbol_sections_view(sections, &view) && view.count == count);
    CHECK(view.entries[count - 1].header.type == SHT_NULL);
    CHECK(view.names == TB_SYMBOL_SECTION_NAMES_ABSENT);
    tb_symbol_sections_destroy(sections);
    CHECK(!budget.used);
    cleanup(descriptor);
}


static void ownership_and_faults(void)
{
    fixture();
    TbSymbolStorageInfo_t info;
    int descriptor = backing(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 256 * 1024));
    for (long failure = 0; failure < 4; ++failure) {
        fail_after = failure;
        TbSymbolSections_t *sections = tb_symbol_sections_create(info, descriptor, &budget);
        if (sections) {
            CHECK(finish(sections, 64) == TB_SYMBOL_RESOURCE_FAILURE);
            no_view(sections);
            tb_symbol_sections_destroy(sections);
        }
        CHECK(!budget.used && fcntl(descriptor, F_GETFD) >= 0);
    }
    fail_after = -1;
    TbSymbolSections_t *sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections);
    budget.limit = budget.used;
    CHECK(finish(sections, 64) == TB_SYMBOL_RESOURCE_FAILURE);
    no_view(sections);
    tb_symbol_sections_destroy(sections);
    CHECK(!budget.used);
    budget.limit = 256 * 1024;
    for (unsigned stage = 0; stage < 300; stage += 3) {
        sections = tb_symbol_sections_create(info, descriptor, &budget);
        CHECK(sections);
        for (unsigned i = 0; i < stage; ++i) {
            TbSymbolOutcome_e outcome = step(sections, 64);
            CHECK(outcome == TB_SYMBOL_PENDING || outcome == TB_SYMBOL_SUCCESS);
        }
        tb_symbol_sections_destroy(sections);
        CHECK(!budget.used && fcntl(descriptor, F_GETFD) >= 0);
    }
    sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections);
    read_error = EINTR;
    bool interrupted = false;
    for (unsigned i = 0; i < 32 && !interrupted; ++i) {
        uint64_t before = tb_symbol_sections_progress(sections);
        CHECK(step(sections, 64) == TB_SYMBOL_PENDING);
        if (reads) {
            CHECK(before == tb_symbol_sections_progress(sections));
            interrupted = true;
        }
    }
    CHECK(interrupted && !read_error);
    short_read = true;
    CHECK(finish(sections, 64) == TB_SYMBOL_SUCCESS);
    short_read = false;
    tb_symbol_sections_destroy(sections);
    CHECK(!budget.used);
    read_error = EIO;
    sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections && finish(sections, 64) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_sections_failure(sections).reason == TB_SYMBOL_SECTIONS_READ);
    CHECK(tb_symbol_sections_failure(sections).read.system_error == EIO);
    no_view(sections);
    tb_symbol_sections_destroy(sections);
    eof = true;
    sections = tb_symbol_sections_create(info, descriptor, &budget);
    CHECK(sections && finish(sections, 64) == TB_SYMBOL_OPERATIONAL_FAILURE);
    no_view(sections);
    tb_symbol_sections_destroy(sections);
    eof = false;
    CHECK(!budget.used);
    cleanup(descriptor);
}


int main(void)
{
    for (unsigned bytes = 4; bytes <= 8; bytes += 4) {
        width = bytes;
        for (unsigned byte_order = 0; byte_order < 2; ++byte_order) {
            order = byte_order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN;
            fixture();
            success(TB_SYMBOL_SECTION_NAMES_AVAILABLE, 64);
            invalid_formats();
            invalid_records();
            name_cases();
            long_names();
        }
    }
    shuffled_records();
    extended_count();
    ownership_and_faults();
    puts("PASS atomic bounded ELF section directories, names, overlap checks and ownership failures");
    return 0;
}
