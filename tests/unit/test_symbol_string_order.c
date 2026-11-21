#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected file fixtures. */
#include "symbol/string_order.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned fault;
static unsigned allocations;
static bool check_comparison_bounds;
static unsigned comparisons;

int __real_memcmp(const void *left, const void *right, size_t count);
int __wrap_memcmp(const void *left, const void *right, size_t count);

int __wrap_memcmp(const void *left, const void *right, size_t count)
{
    if (check_comparison_bounds) {
        CHECK(count && count <= 32768);
        ++comparisons;
    }
    return __real_memcmp(left, right, count);
}


void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
    if (fault && ++allocations == fault)
        return NULL;
    return __real_calloc(count, size);
}


static int read_error;
static bool short_read;
static bool early_eof;
static unsigned read_calls;

ssize_t __real_pread(int descriptor, void *bytes, size_t length, off_t offset);
ssize_t __wrap_pread(int descriptor, void *bytes, size_t length, off_t offset);

ssize_t __wrap_pread(int descriptor, void *bytes, size_t length, off_t offset)
{
    ++read_calls;
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


typedef struct {
    TbSymbolBudget_t           budget;
    TbSymbolSections_t        *sections;
    TbSymbolSectionsView_t     directory;
    TbSymbolString_t          *string;
    TbSymbolStringOrderInput_t input;
    int                        descriptor;
    char                       path[64];
} Scope_t;

static unsigned char image[131072];

static void put(size_t at, unsigned bytes, uint64_t value)
{
    for (unsigned i = 0; i < bytes; ++i)
        image[at + i] = (unsigned char)(value >> (i * 8));
}


static void open_scope(Scope_t *scope, const unsigned char *bytes, size_t length)
{
    CHECK(length < sizeof(image) - 512);
    memset(image, 0, sizeof(image));
    memcpy(image, ELFMAG, SELFMAG);
    image[EI_CLASS] = ELFCLASS64;
    image[EI_DATA] = ELFDATA2LSB;
    image[EI_VERSION] = EV_CURRENT;
    put(16, 2, ET_EXEC);
    put(18, 2, EM_X86_64);
    put(20, 4, EV_CURRENT);
    put(40, 8, 128);
    put(52, 2, 64);
    put(58, 2, 64);
    put(60, 2, 2);
    put(196, 4, SHT_PROGBITS);
    put(216, 8, 512);
    put(224, 8, length + 1);
    put(240, 8, 1);
    if (length)
        memcpy(image + 512, bytes, length);
    *scope = (Scope_t){0};
    CHECK(tb_symbol_budget_init(&scope->budget, 2 * 1024 * 1024));
    strcpy(scope->path, "/tmp/tb-symbol-order-XXXXXX");
    int writer = mkstemp(scope->path);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(!fchmod(writer, 0400) && !close(writer));
    scope->descriptor = open(scope->path, O_RDONLY | O_CLOEXEC);
    CHECK(scope->descriptor >= 0);
    struct stat status;
    CHECK(!fstat(scope->descriptor, &status));
    scope->sections = tb_symbol_sections_create((TbSymbolStorageInfo_t){
        .key   = {
            .device = (uint64_t)status.st_dev,
            .inode  = (uint64_t)status.st_ino
        },
        .bytes = sizeof(image)
    }, scope->descriptor, &scope->budget);
    CHECK(scope->sections);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 100000 && outcome == TB_SYMBOL_PENDING; ++turn)
        outcome = tb_symbol_sections_step(scope->sections, 257);
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(scope->sections, &scope->directory));
    scope->string = tb_symbol_string_create(scope->directory, (TbSymbolStringSelection_t){
        .section_index = 1,
        .end_offset    = length + 1,
        .descriptor    = scope->descriptor
    }, &scope->budget);
    CHECK(scope->string);
    outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 100000 && outcome == TB_SYMBOL_PENDING; ++turn)
        outcome = tb_symbol_string_step(scope->string, 257);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    scope->input.directory = scope->directory;
    scope->input.descriptor = scope->descriptor;
    CHECK(tb_symbol_string_view(scope->string, &scope->input.string));
    CHECK(scope->input.string.bytes == length);
}


static void close_scope(Scope_t *scope)
{
    tb_symbol_string_destroy(scope->string);
    tb_symbol_sections_destroy(scope->sections);
    CHECK(!scope->budget.used);
    CHECK(!close(scope->descriptor) && !unlink(scope->path));
}


static TbSymbolOutcome_e run(TbSymbolStringOrder_t *order, size_t slice, bool alternate)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 3000000 && outcome == TB_SYMBOL_PENDING; ++turn) {
        TbSymbolStringOrder_e result = TB_SYMBOL_STRING_AFTER;
        CHECK(!tb_symbol_string_order_result(order, &result) && result == TB_SYMBOL_STRING_AFTER);
        read_calls = 0;
        outcome = tb_symbol_string_order_step(order, alternate && turn % 2 ? 1 : slice);
        CHECK(read_calls <= 1);
    }
    return outcome;
}


static void check_pair(const unsigned char *left, size_t left_bytes, const unsigned char *right,
                        size_t right_bytes, TbSymbolStringOrder_e expected)
{
    Scope_t scopes[2];
    open_scope(&scopes[0], left, left_bytes);
    open_scope(&scopes[1], right, right_bytes);
    TbSymbolStringOrderInput_t inputs[2] = {scopes[0].input, scopes[1].input};
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
    const size_t slices[] = {1, 64, 257, 65536};
    for (size_t i = 0; i < sizeof(slices) / sizeof(slices[0]); ++i) {
        TbSymbolStringOrder_t *comparison = tb_symbol_string_order_create(inputs, &budget);
        CHECK(comparison);
        CHECK(tb_symbol_string_order_step(comparison, 0) == TB_SYMBOL_OPERATIONAL_FAILURE);
        CHECK(tb_symbol_string_order_step(comparison, 65537) == TB_SYMBOL_OPERATIONAL_FAILURE);
        check_comparison_bounds = true;
        comparisons = 0;
        CHECK(run(comparison, slices[i], i == 3) == TB_SYMBOL_SUCCESS);
        CHECK(comparisons);
        check_comparison_bounds = false;
        TbSymbolStringOrder_e result;
        CHECK(tb_symbol_string_order_result(comparison, &result) && result == expected);
        CHECK(tb_symbol_string_order_progress(comparison));
        tb_symbol_string_order_destroy(comparison);
        CHECK(!budget.used);
    }
    close_scope(&scopes[1]);
    close_scope(&scopes[0]);
}


static void check_failures(void)
{
    Scope_t scopes[2];
    open_scope(&scopes[0], (const unsigned char *)"alpha", 5);
    open_scope(&scopes[1], (const unsigned char *)"alpha", 5);
    TbSymbolStringOrderInput_t inputs[2] = {scopes[0].input, scopes[1].input};
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
    for (unsigned failure = 1; failure <= 3; ++failure) {
        allocations = 0;
        fault = failure;
        CHECK(!tb_symbol_string_order_create(inputs, &budget));
        CHECK(!budget.used && allocations == failure);
        fault = 0;
    }
    TbSymbolStringOrder_t *comparison = tb_symbol_string_order_create(inputs, &budget);
    CHECK(comparison);
    uint64_t required = budget.used;
    tb_symbol_string_order_destroy(comparison);
    CHECK(!budget.used && tb_symbol_budget_init(&budget, required - 1));
    CHECK(!tb_symbol_string_order_create(inputs, &budget) && !budget.used);
    CHECK(tb_symbol_budget_init(&budget, required));
    comparison = tb_symbol_string_order_create(inputs, &budget);
    CHECK(comparison && run(comparison, 257, false) == TB_SYMBOL_SUCCESS);
    tb_symbol_string_order_destroy(comparison);
    CHECK(!budget.used);

    inputs[0].string.end_offset = 5;
    comparison = tb_symbol_string_order_create(inputs, &budget);
    CHECK(comparison && run(comparison, 257, false) == TB_SYMBOL_MALFORMED);
    TbSymbolStringOrderFailure_t failure = tb_symbol_string_order_failure(comparison);
    CHECK(failure.reason == TB_SYMBOL_STRING_ORDER_RANGE && failure.input == 0);
    tb_symbol_string_order_destroy(comparison);
    inputs[0] = scopes[0].input;
    int bad = open(scopes[0].path, O_PATH | O_CLOEXEC);
    CHECK(bad >= 0);
    inputs[0].descriptor = bad;
    comparison = tb_symbol_string_order_create(inputs, &budget);
    CHECK(comparison && run(comparison, 257, false) == TB_SYMBOL_UNSUPPORTED);
    failure = tb_symbol_string_order_failure(comparison);
    CHECK(failure.reason == TB_SYMBOL_STRING_ORDER_READ && failure.input == 0);
    CHECK(failure.read.reason == TB_SYMBOL_FILE_DESCRIPTOR);
    tb_symbol_string_order_destroy(comparison);
    CHECK(fcntl(bad, F_GETFD) >= 0 && !close(bad));
    CHECK(!budget.used);
    close_scope(&scopes[1]);
    close_scope(&scopes[0]);
}


static void check_read_failures(void)
{
    Scope_t scopes[2];
    open_scope(&scopes[0], (const unsigned char *)"alpha", 5);
    open_scope(&scopes[1], (const unsigned char *)"alpha", 5);
    TbSymbolStringOrderInput_t inputs[2] = {scopes[0].input, scopes[1].input};
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
    TbSymbolStringOrder_t *comparison = tb_symbol_string_order_create(inputs, &budget);
    CHECK(comparison);
    short_read = true;
    read_error = EINTR;
    CHECK(run(comparison, 257, false) == TB_SYMBOL_SUCCESS);
    short_read = false;
    tb_symbol_string_order_destroy(comparison);
    for (unsigned input = 0; input < 2; ++input) {
        int no_exec = open(scopes[input].path, O_RDONLY);
        CHECK(no_exec >= 0);
        inputs[input].descriptor = no_exec;
        comparison = tb_symbol_string_order_create(inputs, &budget);
        CHECK(comparison && run(comparison, 257, false) == TB_SYMBOL_UNSUPPORTED);
        TbSymbolStringOrderFailure_t failure = tb_symbol_string_order_failure(comparison);
        CHECK(failure.reason == TB_SYMBOL_STRING_ORDER_READ && failure.input == input);
        CHECK(failure.read.reason == TB_SYMBOL_FILE_DESCRIPTOR);
        tb_symbol_string_order_destroy(comparison);
        CHECK(fcntl(no_exec, F_GETFD) >= 0 && !close(no_exec));
        inputs[input] = scopes[input].input;
    }
    comparison = tb_symbol_string_order_create(inputs, &budget);
    CHECK(comparison);
    early_eof = true;
    CHECK(run(comparison, 257, false) == TB_SYMBOL_OPERATIONAL_FAILURE);
    early_eof = false;
    CHECK(tb_symbol_string_order_failure(comparison).read.reason == TB_SYMBOL_FILE_CHANGED);
    tb_symbol_string_order_destroy(comparison);
    comparison = tb_symbol_string_order_create(inputs, &budget);
    CHECK(comparison);
    read_error = EIO;
    CHECK(run(comparison, 257, false) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_string_order_failure(comparison).read.reason == TB_SYMBOL_FILE_IO);
    tb_symbol_string_order_destroy(comparison);
    CHECK(!budget.used);
    close_scope(&scopes[1]);
    close_scope(&scopes[0]);
}


int main(void)
{
    const unsigned char high[] = {0xff, 0x80};
    const unsigned char low[] = {0x80, 0xff};
    check_pair((const unsigned char *)"alpha", 5, (const unsigned char *)"alpha", 5, TB_SYMBOL_STRING_EQUAL);
    check_pair((const unsigned char *)"alpha", 5, (const unsigned char *)"alpine", 6,
               TB_SYMBOL_STRING_BEFORE);
    check_pair((const unsigned char *)"alpha", 5, (const unsigned char *)"alphabet", 8,
               TB_SYMBOL_STRING_BEFORE);
    check_pair(NULL, 0, NULL, 0, TB_SYMBOL_STRING_EQUAL);
    check_pair(NULL, 0, (const unsigned char *)"a", 1, TB_SYMBOL_STRING_BEFORE);
    check_pair((const unsigned char *)"a", 1, NULL, 0, TB_SYMBOL_STRING_AFTER);
    check_pair(high, sizeof(high), low, sizeof(low), TB_SYMBOL_STRING_AFTER);
    unsigned char *left = malloc(100001);
    unsigned char *right = malloc(100001);
    CHECK(left && right);
    memset(left, 'q', 100001);
    memset(right, 'q', 100001);
    right[100000] = 'r';
    check_pair(left, 100001, right, 100001, TB_SYMBOL_STRING_BEFORE);
    check_pair(left, 100001, left, 100001, TB_SYMBOL_STRING_EQUAL);
    free(right);
    free(left);
    check_failures();
    check_read_failures();
    puts("PASS complete retained string byte order, long prefixes, bounded slices and owned scratch");
    return 0;
}
