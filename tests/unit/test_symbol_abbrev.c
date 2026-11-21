#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected retained-file fixtures. */
#include "symbol/abbrev.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned char image[131072];
static unsigned width;
static TbSymbolByteOrder_e order;
static uint64_t payload_bytes;
static char retained_path[64];
static long fail_after = -1;
static bool short_read;
static bool eof;
static int read_error;
static size_t inspected;
static unsigned reads;
static int mutation_fd = -1;
static bool mutate_on_malloc;

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
    if (mutate_on_malloc) {
        unsigned char replacement = 0xff;
        CHECK(mutation_fd >= 0 && pwrite(mutation_fd, &replacement, 1, 512) == 1);
        mutate_on_malloc = false;
    }
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


static void byte(unsigned value)
{
    CHECK(payload_bytes < sizeof(image) - 512 && value <= 255);
    image[512 + payload_bytes++] = (unsigned char)value;
    put(selected() + (width == 4 ? 20 : 32), width, payload_bytes);
}


static void uleb(uint64_t value)
{
    do {
        unsigned part = (unsigned)(value & 0x7fU);
        value >>= 7;
        byte(part | (value ? 0x80U : 0));
    } while (value);
}


static void sleb(int64_t value)
{
    uint64_t bits = (uint64_t)value;
    for (;;) {
        unsigned part = (unsigned)(bits & 0x7fU);
        bits >>= 7;
        if (value < 0)
            bits |= UINT64_MAX << 57;
        bool done = (bits == 0 && !(part & 0x40U)) || (bits == UINT64_MAX && (part & 0x40U));
        byte(part | (done ? 0 : 0x80U));
        if (done)
            return;
    }
}


static void declaration(uint64_t code)
{
    uleb(code);
    uleb(0x11);
    byte(1);
}


static void attribute(uint64_t form)
{
    uleb(3);
    uleb(form);
}


static void entry_end(void)
{
    uleb(0);
    uleb(0);
}


static void simple(void)
{
    fixture();
    declaration(7);
    attribute(TB_SYMBOL_FORM_STRING);
    entry_end();
    declaration(1);
    attribute(TB_SYMBOL_FORM_IMPLICIT_CONST);
    sleb(-17);
    entry_end();
    uleb(0);
}


static int backing(TbSymbolStorageInfo_t *expected, bool retain_writer)
{
    char path[] = "/tmp/tb-symbol-abbrev-XXXXXX";
    int writer = mkstemp(path);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(!fchmod(writer, 0400));
    if (retain_writer)
        mutation_fd = writer;
    else
        CHECK(!close(writer));
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


static TbSymbolOutcome_e step(TbSymbolAbbrev_t *abbrev)
{
    uint64_t before = tb_symbol_abbrev_progress(abbrev);
    inspected = 0;
    reads = 0;
    TbSymbolOutcome_e outcome = tb_symbol_abbrev_step(abbrev, 64);
    CHECK(reads <= 1 && inspected <= 64 && tb_symbol_abbrev_progress(abbrev) >= before);
    return outcome;
}


static void no_view(TbSymbolAbbrev_t *abbrev)
{
    TbSymbolAbbrevView_t view;
    unsigned char original[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(original, &view, sizeof(view));
    CHECK(!tb_symbol_abbrev_view(abbrev, &view) && !memcmp(original, &view, sizeof(view)));
    const TbSymbolAbbrevEntry_t *entry = NULL;
    CHECK(tb_symbol_abbrev_find(abbrev, 1, &entry) == TB_SYMBOL_ABBREV_UNAVAILABLE && !entry);
}


static TbSymbolOutcome_e finish(TbSymbolAbbrev_t *abbrev)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 3000000 && outcome == TB_SYMBOL_PENDING; ++turn) {
        outcome = step(abbrev);
        if (outcome == TB_SYMBOL_PENDING)
            no_view(abbrev);
    }
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


typedef struct {
    uint64_t                 offset;
    unsigned                 version;
    TbSymbolOutcome_e        outcome;
    TbSymbolAbbrevFailure_e  failure;
    uint64_t                 entries;
    uint64_t                 attributes;
    bool                     wide_constants;
} Expectation_t;

static void check_scope(Expectation_t expected)
{
    TbSymbolStorageInfo_t info;
    int fd = backing(&info, false);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
    TbSymbolSectionsView_t view;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &view);
    size_t baseline = budget.used;
    TbSymbolAbbrev_t *abbrev = tb_symbol_abbrev_create(view, (TbSymbolAbbrevSelection_t){
        .section_index = 1,
        .table_offset  = expected.offset,
        .version       = expected.version,
        .descriptor    = fd
    }, &budget);
    CHECK(abbrev);
    no_view(abbrev);
    TbSymbolOutcome_e outcome = finish(abbrev);
    if (outcome != expected.outcome || tb_symbol_abbrev_failure(abbrev).reason != expected.failure) {
        fprintf(stderr, "abbreviation scope: expected %u/%u, got %u/%u at %llu\n", expected.outcome,
                expected.failure, outcome, tb_symbol_abbrev_failure(abbrev).reason,
                (unsigned long long)tb_symbol_abbrev_failure(abbrev).offset);
    }
    CHECK(outcome == expected.outcome && tb_symbol_abbrev_failure(abbrev).reason == expected.failure);
    if (outcome == TB_SYMBOL_SUCCESS) {
        TbSymbolAbbrevView_t completed;
        CHECK(tb_symbol_abbrev_view(abbrev, &completed));
        CHECK(completed.count == expected.entries && completed.attribute_count == expected.attributes);
        CHECK(completed.table_offset == expected.offset && completed.end_offset <= payload_bytes);
        CHECK(completed.section_index == 1 && completed.version == expected.version);
        CHECK(completed.backing.key.inode == info.key.inode);
        if (expected.wide_constants) {
            CHECK(completed.attribute_count == 2);
            CHECK(completed.attributes[0].implicit_bytes == 70000);
            CHECK(completed.attributes[0].implicit_fits && completed.attributes[0].implicit_value == -1);
            CHECK(completed.attributes[1].implicit_bytes == 10);
            CHECK(!completed.attributes[1].implicit_fits && !completed.attributes[1].implicit_value);
        }
        uint64_t previous = expected.offset;
        uint64_t attributes = 0;
        for (uint64_t i = 0; i < completed.count; ++i) {
            const TbSymbolAbbrevEntry_t *entry = &completed.entries[i];
            CHECK(entry->offset == previous && entry->end_offset > entry->offset);
            CHECK(entry->first_attribute == attributes);
            CHECK(entry->attribute_count <= completed.attribute_count - attributes);
            const TbSymbolAbbrevEntry_t *found = NULL;
            CHECK(tb_symbol_abbrev_find(abbrev, entry->code, &found) == TB_SYMBOL_ABBREV_FOUND &&
                  found == entry);
            for (uint64_t j = 0; j < entry->attribute_count; ++j) {
                const TbSymbolAbbrevAttribute_t *attr = &completed.attributes[attributes + j];
                CHECK(attr->name && attr->form && attr->offset > entry->offset);
                if (attr->form == TB_SYMBOL_FORM_IMPLICIT_CONST) {
                    CHECK(attr->implicit_offset > attr->offset && attr->implicit_bytes);
                    CHECK(attr->implicit_offset + attr->implicit_bytes < entry->end_offset);
                    if (!attr->implicit_fits)
                        CHECK(!attr->implicit_value);
                }
            }
            previous = entry->end_offset;
            attributes += entry->attribute_count;
        }
        CHECK(attributes == completed.attribute_count);
        const TbSymbolAbbrevEntry_t *missing = NULL;
        CHECK(tb_symbol_abbrev_find(abbrev, UINT64_C(0xfffffffffffffffe), &missing) ==
              TB_SYMBOL_ABBREV_MISSING && !missing);
        CHECK(tb_symbol_abbrev_find(abbrev, 0, &missing) == TB_SYMBOL_ABBREV_INVALID && !missing);
    } else {
        no_view(abbrev);
        if (expected.failure == TB_SYMBOL_ABBREV_CODE_CONFLICT)
            CHECK(tb_symbol_abbrev_failure(abbrev).offset > tb_symbol_abbrev_failure(abbrev).related_offset);
    }
    tb_symbol_abbrev_destroy(abbrev);
    CHECK(budget.used == baseline && fcntl(fd, F_GETFD) >= 0);
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used && !close(fd) && !unlink(retained_path));
}


static void cases(void)
{
    simple();
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_SUCCESS,
        .entries = 2,
        .attributes = 2
    });
    check_scope((Expectation_t){
        .version = 4,
        .outcome = TB_SYMBOL_UNSUPPORTED,
        .failure = TB_SYMBOL_ABBREV_FORM
    });
    image[512 + 2] = 2;
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_MALFORMED,
        .failure = TB_SYMBOL_ABBREV_CHILDREN
    });
    simple();
    image[512 + 5] = 1;
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_MALFORMED,
        .failure = TB_SYMBOL_ABBREV_TERMINATOR
    });
    simple();
    image[512 + 4] = 2;
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_UNSUPPORTED,
        .failure = TB_SYMBOL_ABBREV_FORM
    });
    fixture();
    declaration(1);
    entry_end();
    declaration(1);
    entry_end();
    uleb(0);
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_MALFORMED,
        .failure = TB_SYMBOL_ABBREV_CODE_CONFLICT
    });
    fixture();
    declaration(1);
    attribute(TB_SYMBOL_FORM_IMPLICIT_CONST);
    byte(0x80);
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_MALFORMED,
        .failure = TB_SYMBOL_ABBREV_TRUNCATED
    });
    fixture();
    for (unsigned i = 0; i < 9; ++i)
        byte(0x80);
    byte(2);
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_UNSUPPORTED,
        .failure = TB_SYMBOL_ABBREV_IDENTIFIER
    });
    fixture();
    uleb(0);
    uint64_t second = payload_bytes;
    declaration(UINT64_MAX);
    entry_end();
    uleb(0);
    check_scope((Expectation_t){
        .version = 4,
        .outcome = TB_SYMBOL_SUCCESS
    });
    check_scope((Expectation_t){
        .offset = second,
        .version = 4,
        .outcome = TB_SYMBOL_SUCCESS,
        .entries = 1
    });
    check_scope((Expectation_t){
        .offset = payload_bytes,
        .version = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .failure = TB_SYMBOL_ABBREV_RANGE
    });
}


static void wide_and_many(void)
{
    fixture();
    declaration(9);
    attribute(TB_SYMBOL_FORM_IMPLICIT_CONST);
    for (unsigned i = 0; i < 69999; ++i)
        byte(0xff);
    byte(0x7f);
    attribute(TB_SYMBOL_FORM_IMPLICIT_CONST);
    for (unsigned i = 0; i < 9; ++i)
        byte(0x80);
    byte(1);
    entry_end();
    uleb(0);
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_SUCCESS,
        .entries = 1,
        .attributes = 2,
        .wide_constants = true
    });
    fixture();
    declaration(1);
    uint64_t attributes = 0;
    for (uint64_t form = 1; form <= 0x2c; ++form) {
        if (form == 2)
            continue;
        attribute(form);
        if (form == TB_SYMBOL_FORM_IMPLICIT_CONST)
            sleb(INT64_MIN);
        ++attributes;
    }
    entry_end();
    uleb(0);
    check_scope((Expectation_t){
        .version = 5,
        .outcome = TB_SYMBOL_SUCCESS,
        .entries = 1,
        .attributes = attributes
    });
    fixture();
    for (uint64_t i = 0; i < 513; ++i) {
        declaration((i * 137) % 513 + 1);
        attribute(TB_SYMBOL_FORM_UDATA);
        entry_end();
    }
    uleb(0);
    check_scope((Expectation_t){
        .version = 4,
        .outcome = TB_SYMBOL_SUCCESS,
        .entries = 513,
        .attributes = 513
    });
}


static void ownership(void)
{
    simple();
    TbSymbolStorageInfo_t info;
    int fd = backing(&info, true);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 512 * 1024));
    TbSymbolSectionsView_t view;
    TbSymbolSections_t *owner = directory(fd, info, &budget, &view);
    size_t baseline = budget.used;
    TbSymbolAbbrevSelection_t selection = {
        .section_index = 1,
        .version       = 5,
        .descriptor    = fd
    };
    for (long failure = 0; failure < 5; ++failure) {
        fail_after = failure;
        TbSymbolAbbrev_t *abbrev = tb_symbol_abbrev_create(view, selection, &budget);
        if (abbrev) {
            CHECK(finish(abbrev) == TB_SYMBOL_RESOURCE_FAILURE);
            no_view(abbrev);
            tb_symbol_abbrev_destroy(abbrev);
        }
        CHECK(budget.used == baseline);
    }
    fail_after = -1;
    TbSymbolAbbrev_t *abbrev = tb_symbol_abbrev_create(view, selection, &budget);
    CHECK(abbrev);
    budget.limit = budget.used;
    CHECK(finish(abbrev) == TB_SYMBOL_RESOURCE_FAILURE);
    no_view(abbrev);
    tb_symbol_abbrev_destroy(abbrev);
    CHECK(budget.used == baseline);
    budget.limit = 512 * 1024;
    for (unsigned stage = 0; stage < 128; ++stage) {
        abbrev = tb_symbol_abbrev_create(view, selection, &budget);
        CHECK(abbrev);
        for (unsigned i = 0; i < stage; ++i) {
            TbSymbolOutcome_e outcome = step(abbrev);
            CHECK(outcome == TB_SYMBOL_PENDING || outcome == TB_SYMBOL_SUCCESS);
            if (outcome == TB_SYMBOL_PENDING)
                no_view(abbrev);
        }
        tb_symbol_abbrev_destroy(abbrev);
        CHECK(budget.used == baseline && fcntl(fd, F_GETFD) >= 0);
    }
    abbrev = tb_symbol_abbrev_create(view, selection, &budget);
    CHECK(abbrev);
    read_error = EINTR;
    bool interrupted = false;
    for (unsigned i = 0; i < 32 && !interrupted; ++i) {
        uint64_t before = tb_symbol_abbrev_progress(abbrev);
        CHECK(step(abbrev) == TB_SYMBOL_PENDING);
        if (reads) {
            CHECK(before == tb_symbol_abbrev_progress(abbrev));
            interrupted = true;
        }
    }
    CHECK(interrupted && !read_error);
    short_read = true;
    CHECK(finish(abbrev) == TB_SYMBOL_SUCCESS);
    short_read = false;
    tb_symbol_abbrev_destroy(abbrev);
    read_error = EIO;
    abbrev = tb_symbol_abbrev_create(view, selection, &budget);
    CHECK(abbrev && finish(abbrev) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_abbrev_failure(abbrev).read.system_error == EIO);
    no_view(abbrev);
    tb_symbol_abbrev_destroy(abbrev);
    eof = true;
    abbrev = tb_symbol_abbrev_create(view, selection, &budget);
    CHECK(abbrev && finish(abbrev) == TB_SYMBOL_OPERATIONAL_FAILURE);
    no_view(abbrev);
    tb_symbol_abbrev_destroy(abbrev);
    eof = false;
    abbrev = tb_symbol_abbrev_create(view, selection, &budget);
    CHECK(abbrev);
    mutate_on_malloc = true;
    CHECK(finish(abbrev) == TB_SYMBOL_OPERATIONAL_FAILURE && !mutate_on_malloc);
    CHECK(tb_symbol_abbrev_failure(abbrev).reason == TB_SYMBOL_ABBREV_READ);
    no_view(abbrev);
    tb_symbol_abbrev_destroy(abbrev);
    CHECK(budget.used == baseline && !close(mutation_fd));
    mutation_fd = -1;
    tb_symbol_sections_destroy(owner);
    CHECK(!budget.used && !close(fd) && !unlink(retained_path));
}


int main(void)
{
    for (unsigned bytes = 4; bytes <= 8; bytes += 4) {
        width = bytes;
        for (unsigned byte_order = 0; byte_order < 2; ++byte_order) {
            order = byte_order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN;
            cases();
            wide_and_many();
        }
    }
    ownership();
    puts("PASS complete bounded abbreviation scopes, wide constants, lookup and failure isolation");
    return 0;
}
