#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected descriptor fixtures. */
#include "symbol/strings.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned char image[131072];
static unsigned width;
static TbSymbolByteOrder_e order;
static uint64_t payload_bytes;
static unsigned fault;
static unsigned allocations;
static unsigned read_mode;
static unsigned reads;
static size_t inspected;
static bool check_bounds;
static uint64_t first_byte;
static uint64_t end_byte;

void *__real_calloc(size_t count, size_t size);
ssize_t __real_pread(int fd, void *bytes, size_t count, off_t offset);
void *__wrap_calloc(size_t count, size_t size);
ssize_t __wrap_pread(int fd, void *bytes, size_t count, off_t offset);

void *__wrap_calloc(size_t count, size_t size)
{
    if (fault && ++allocations == fault)
        return NULL;
    return __real_calloc(count, size);
}


ssize_t __wrap_pread(int fd, void *bytes, size_t count, off_t offset)
{
    ++reads;
    inspected += count;
    if (check_bounds) {
        CHECK(offset >= 0 && (uint64_t)offset >= first_byte && (uint64_t)offset <= end_byte);
        CHECK(count <= end_byte - (uint64_t)offset);
    }
    if (read_mode == 1 && count > 1)
        count = 1;
    if (read_mode == 2) {
        errno = EINTR;
        return -1;
    }
    if (read_mode == 3)
        return 0;
    if (read_mode == 4) {
        errno = EIO;
        return -1;
    }
    return __real_pread(fd, bytes, count, offset);
}


static void put(size_t at, unsigned bytes, uint64_t value)
{
    CHECK(bytes <= 8 && at <= sizeof(image) && bytes <= sizeof(image) - at);
    for (unsigned i = 0; i < bytes; ++i) {
        unsigned index = order == TB_SYMBOL_LITTLE_ENDIAN ? i : bytes - i - 1;
        image[at + index] = (unsigned char)(value >> (i * 8));
    }
}


static size_t selected(void)
{
    return 128 + (width == 4 ? 40U : 64U);
}


static void fixture(size_t bytes)
{
    memset(image, 0, sizeof(image));
    CHECK(bytes <= sizeof(image) - 512);
    payload_bytes = bytes;
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
    put(selected() + (width == 4 ? 20 : 32), width, bytes);
    put(selected() + (width == 4 ? 32 : 48), width, 1);
    memset(image + 512, 'x', bytes);
    if (bytes)
        image[512 + bytes - 1] = 0;
}


typedef struct {
    TbSymbolBudget_t       budget;
    TbSymbolSections_t    *owner;
    TbSymbolSectionsView_t view;
    int                   descriptor;
    char                  path[64];
    size_t                baseline;
} Scope_t;

static void open_scope(Scope_t *scope)
{
    *scope = (Scope_t){0};
    CHECK(tb_symbol_budget_init(&scope->budget, 2 * 1024 * 1024));
    strcpy(scope->path, "/tmp/tb-symbol-string-XXXXXX");
    int writer = mkstemp(scope->path);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(!fchmod(writer, 0400) && !close(writer));
    scope->descriptor = open(scope->path, O_RDONLY | O_CLOEXEC);
    CHECK(scope->descriptor >= 0);
    struct stat status;
    CHECK(!fstat(scope->descriptor, &status));
    scope->owner = tb_symbol_sections_create((TbSymbolStorageInfo_t){
        .key = {
            .device = (uint64_t)status.st_dev,
            .inode  = (uint64_t)status.st_ino
        },
        .bytes = sizeof(image)
    }, scope->descriptor, &scope->budget);
    CHECK(scope->owner);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 10000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_sections_step(scope->owner, 64);
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(scope->owner, &scope->view));
    scope->baseline = scope->budget.used;
}


static void close_scope(Scope_t *scope)
{
    CHECK(scope->budget.used == scope->baseline && fcntl(scope->descriptor, F_GETFD) >= 0);
    tb_symbol_sections_destroy(scope->owner);
    CHECK(!scope->budget.used && !close(scope->descriptor) && !unlink(scope->path));
}


static TbSymbolString_t *create(Scope_t *scope, uint64_t offset, uint64_t end)
{
    reads = 0;
    TbSymbolString_t *string = tb_symbol_string_create(scope->view, (TbSymbolStringSelection_t){
        .section_index = 1,
        .offset        = offset,
        .end_offset    = end,
        .descriptor    = scope->descriptor
    }, &scope->budget);
    CHECK(!reads);
    first_byte = 512 + offset;
    end_byte = 512 + end;
    return string;
}


static void unavailable(TbSymbolString_t *string)
{
    TbSymbolStringView_t view;
    unsigned char before[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(before, &view, sizeof(view));
    CHECK(!tb_symbol_string_view(string, &view) && !memcmp(before, &view, sizeof(view)));
}


static TbSymbolOutcome_e step(TbSymbolString_t *string, size_t allowance)
{
    reads = 0;
    inspected = 0;
    check_bounds = true;
    TbSymbolOutcome_e outcome = tb_symbol_string_step(string, allowance);
    check_bounds = false;
    CHECK(reads <= 1 && inspected <= allowance);
    if (outcome != TB_SYMBOL_SUCCESS)
        unavailable(string);
    return outcome;
}


static TbSymbolOutcome_e complete(TbSymbolString_t *string, size_t allowance)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = step(string, allowance);
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


static void check(uint64_t offset, uint64_t end, uint64_t bytes, TbSymbolOutcome_e expected,
                  TbSymbolStringFailure_e reason)
{
    Scope_t scope;
    open_scope(&scope);
    const size_t allowances[] = {64, 257, 65536};
    for (size_t i = 0; i < sizeof(allowances) / sizeof(allowances[0]); ++i) {
        TbSymbolString_t *string = create(&scope, offset, end);
        CHECK(string);
        unavailable(string);
        CHECK(complete(string, allowances[i]) == expected);
        CHECK(tb_symbol_string_failure(string).reason == reason);
        if (expected == TB_SYMBOL_SUCCESS) {
            TbSymbolStringView_t view;
            CHECK(tb_symbol_string_view(string, &view));
            CHECK(view.section_index == 1 && view.offset == offset && view.end_offset == end);
            CHECK(view.bytes == bytes && view.backing.key.inode == scope.view.backing.key.inode);
            CHECK(view.offset + view.bytes < view.end_offset && !image[512 + view.offset + view.bytes]);
        }
        tb_symbol_string_destroy(string);
        CHECK(scope.budget.used == scope.baseline);
    }
    close_scope(&scope);
}


static void boundaries(void)
{
    fixture(6);
    memcpy(image + 512, "hello", 6);
    check(0, 6, 5, TB_SYMBOL_SUCCESS, TB_SYMBOL_STRING_NONE);
    check(2, 6, 3, TB_SYMBOL_SUCCESS, TB_SYMBOL_STRING_NONE);
    check(5, 6, 0, TB_SYMBOL_SUCCESS, TB_SYMBOL_STRING_NONE);
    check(0, 5, 0, TB_SYMBOL_MALFORMED, TB_SYMBOL_STRING_UNTERMINATED);
    check(6, 6, 0, TB_SYMBOL_MALFORMED, TB_SYMBOL_STRING_RANGE);
    check(0, 7, 0, TB_SYMBOL_MALFORMED, TB_SYMBOL_STRING_RANGE);
    check(UINT64_MAX, UINT64_MAX, 0, TB_SYMBOL_MALFORMED, TB_SYMBOL_STRING_RANGE);
    fixture(4);
    image[512] = 0xff;
    image[513] = 0xfe;
    check(0, 4, 3, TB_SYMBOL_SUCCESS, TB_SYMBOL_STRING_NONE);
    fixture(70001);
    check(0, 70001, 70000, TB_SYMBOL_SUCCESS, TB_SYMBOL_STRING_NONE);
    check(0, 70000, 0, TB_SYMBOL_MALFORMED, TB_SYMBOL_STRING_UNTERMINATED);
    fixture(0);
    check(0, 0, 0, TB_SYMBOL_MALFORMED, TB_SYMBOL_STRING_RANGE);
    fixture(6);
    put(selected() + 8, width, SHF_COMPRESSED);
    check(0, 6, 0, TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_STRING_SECTION);
    fixture(6);
    put(selected() + 4, 4, SHT_NOBITS);
    check(0, 6, 0, TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_STRING_SECTION);
    fixture(6);
    put(selected() + 4, 4, SHT_STRTAB);
    check(0, 6, 5, TB_SYMBOL_SUCCESS, TB_SYMBOL_STRING_NONE);
}


static void failures(void)
{
    fixture(6);
    Scope_t scope;
    open_scope(&scope);
    for (fault = 1; fault <= 2; ++fault) {
        allocations = 0;
        CHECK(!create(&scope, 0, payload_bytes));
        CHECK(allocations >= fault && scope.budget.used == scope.baseline);
    }
    fault = 0;
    size_t old_limit = scope.budget.limit;
    scope.budget.limit = scope.baseline;
    CHECK(!create(&scope, 0, payload_bytes) && scope.budget.used == scope.baseline);
    scope.budget.limit = old_limit;
    for (unsigned stop = 0; stop < 64; ++stop) {
        TbSymbolString_t *string = create(&scope, 0, payload_bytes);
        CHECK(string);
        for (unsigned i = 0; i < stop; ++i)
            (void)step(string, 64);
        tb_symbol_string_destroy(string);
        CHECK(scope.budget.used == scope.baseline);
    }
    for (unsigned mode = 1; mode <= 4; ++mode) {
        TbSymbolString_t *string = create(&scope, 0, payload_bytes);
        CHECK(string);
        read_mode = mode;
        if (mode == 2) {
            uint64_t progress = 0;
            bool stalled = false;
            for (unsigned i = 0; i < 32; ++i) {
                CHECK(step(string, 64) == TB_SYMBOL_PENDING);
                uint64_t next = tb_symbol_string_progress(string);
                if (reads && next == progress)
                    stalled = true;
                progress = next;
            }
            CHECK(stalled);
            read_mode = 0;
            CHECK(complete(string, 64) == TB_SYMBOL_SUCCESS);
        } else {
            CHECK(complete(string, 64) == (mode == 1 ? TB_SYMBOL_SUCCESS : TB_SYMBOL_OPERATIONAL_FAILURE));
        }
        read_mode = 0;
        tb_symbol_string_destroy(string);
    }
    TbSymbolString_t *string = create(&scope, 0, payload_bytes);
    CHECK(string && tb_symbol_string_step(string, 63) == TB_SYMBOL_OPERATIONAL_FAILURE);
    unavailable(string);
    tb_symbol_string_destroy(string);
    int replacement = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(replacement >= 0);
    string = tb_symbol_string_create(scope.view, (TbSymbolStringSelection_t){
        .section_index = 1,
        .offset        = 0,
        .end_offset    = payload_bytes,
        .descriptor    = replacement
    }, &scope.budget);
    CHECK(string && complete(string, 64) == TB_SYMBOL_OPERATIONAL_FAILURE);
    tb_symbol_string_destroy(string);
    CHECK(!close(replacement));
    close_scope(&scope);
}


int main(void)
{
    for (width = 4; width <= 8; width += 4) {
        for (unsigned byte_order = 0; byte_order < 2; ++byte_order) {
            order = (TbSymbolByteOrder_e)byte_order;
            boundaries();
        }
    }
    width = 8;
    order = TB_SYMBOL_LITTLE_ENDIAN;
    failures();
    puts("PASS bounded retained strings, exact spans, raw bytes and failure ownership");
    return 0;
}
