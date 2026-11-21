#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected file fixtures. */
#include "symbol/name_order.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
    TbSymbolStringView_t       strings[8];
    TbSymbolNameOrderInput_t   input;
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
    CHECK(tb_symbol_budget_reserve(&scope->budget, sizeof(*scope)));
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

}


static void close_scope(Scope_t *scope)
{
    tb_symbol_sections_destroy(scope->sections);
    CHECK(tb_symbol_budget_release(&scope->budget, sizeof(*scope)));
    CHECK(!scope->budget.used);
    CHECK(!close(scope->descriptor) && !unlink(scope->path));
}


static void open_names(Scope_t *scope, const char *const names[], size_t count)
{
    unsigned char bytes[131072];
    size_t length = 0;
    CHECK(count <= 8);
    for (size_t i = 0; i < count; ++i) {
        size_t size = strlen(names[i]) + 1;
        CHECK(size <= sizeof(bytes) - length);
        memcpy(bytes + length, names[i], size);
        length += size;
    }
    open_scope(scope, bytes, length);
    uint64_t at = 0;
    for (size_t i = 0; i < count; ++i) {
        TbSymbolString_t *string = tb_symbol_string_create(scope->directory, (TbSymbolStringSelection_t){
            .section_index = 1,
            .offset        = at,
            .end_offset    = length + 1,
            .descriptor    = scope->descriptor
        }, &scope->budget);
        CHECK(string);
        TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
        for (unsigned turn = 0; turn < 100000 && outcome == TB_SYMBOL_PENDING; ++turn)
            outcome = tb_symbol_string_step(string, 257);
        CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_string_view(string, &scope->strings[i]));
        CHECK(scope->strings[i].bytes == strlen(names[i]));
        at += scope->strings[i].bytes + 1;
        /* Adopt the pointer-free completed span before retiring child scratch.
         * This scope retains its source directory, frozen bytes and descriptor. */
        tb_symbol_string_destroy(string);
    }
    scope->input = (TbSymbolNameOrderInput_t){
        .directory  = scope->directory,
        .components = scope->strings,
        .count      = count,
        .descriptor = scope->descriptor
    };
}


static TbSymbolOutcome_e run(TbSymbolNameOrder_t *comparison, size_t slice)
{
    for (unsigned turn = 0; turn < 3000000; ++turn) {
        TbSymbolStringOrder_e order;
        CHECK(!tb_symbol_name_order_result(comparison, &order));
        read_calls = 0;
        check_comparison_bounds = true;
        TbSymbolOutcome_e outcome = tb_symbol_name_order_step(comparison, slice);
        check_comparison_bounds = false;
        CHECK(read_calls <= 1);
        if (outcome != TB_SYMBOL_PENDING)
            return outcome;
    }
    CHECK(false);
    return TB_SYMBOL_OPERATIONAL_FAILURE;
}


static void check_pair(const char *const left[], size_t left_count,
                       const char *const right[], size_t right_count, TbSymbolStringOrder_e expected)
{
    Scope_t scopes[2];
    open_names(&scopes[0], left, left_count);
    open_names(&scopes[1], right, right_count);
    TbSymbolNameOrderInput_t inputs[2] = {scopes[0].input, scopes[1].input};
    TbSymbolBudget_t construction;
    CHECK(tb_symbol_budget_init(&construction, 1024 * 1024));
    const size_t slices[] = {1, 7, 65536};
    for (size_t i = 0; i < sizeof(slices) / sizeof(*slices); ++i) {
        TbSymbolNameOrder_t *comparison = tb_symbol_name_order_create(inputs, &construction);
        CHECK(comparison);
        unsigned before = comparisons;
        CHECK(run(comparison, slices[i]) == TB_SYMBOL_SUCCESS);
        bool left_nonempty = left_count > 1 || (left_count && left[0][0]);
        bool right_nonempty = right_count > 1 || (right_count && right[0][0]);
        if (left_nonempty && right_nonempty)
            CHECK(comparisons > before);
        TbSymbolStringOrder_e order;
        CHECK(tb_symbol_name_order_result(comparison, &order) && order == expected);
        CHECK(tb_symbol_name_order_progress(comparison) > 0);
        tb_symbol_name_order_destroy(comparison);
        CHECK(!construction.used);
    }
    close_scope(&scopes[0]);
    close_scope(&scopes[1]);
}


static void check_long_boundary(void)
{
    char *prefix = malloc(65538);
    char *flat = malloc(65541);
    CHECK(prefix && flat);
    memset(prefix, 'x', 65537);
    prefix[65537] = 0;
    memcpy(flat, prefix, 65537);
    memcpy(flat + 65537, "::y", 4);
    const char *left[] = {prefix, "y"};
    const char *right[] = {flat};
    check_pair(left, 2, right, 1, TB_SYMBOL_STRING_EQUAL);
    flat[65539] = 'z';
    check_pair(left, 2, right, 1, TB_SYMBOL_STRING_BEFORE);
    free(flat);
    free(prefix);
}


static void check_failures(void)
{
    Scope_t scopes[2];
    const char *left[] = {"A", "B"};
    const char *right[] = {"Z", "B"};
    open_names(&scopes[0], left, 2);
    open_names(&scopes[1], right, 2);
    TbSymbolNameOrderInput_t inputs[2] = {scopes[0].input, scopes[1].input};
    TbSymbolBudget_t construction;
    CHECK(tb_symbol_budget_init(&construction, 1024 * 1024));
    fault = UINT_MAX;
    allocations = 0;
    TbSymbolNameOrder_t *comparison = tb_symbol_name_order_create(inputs, &construction);
    CHECK(comparison);
    unsigned count = allocations;
    fault = 0;
    size_t capacity = construction.used;
    tb_symbol_name_order_destroy(comparison);
    CHECK(!construction.used);
    for (unsigned selected = 1; selected <= count; ++selected) {
        fault = selected;
        allocations = 0;
        CHECK(!tb_symbol_name_order_create(inputs, &construction));
        CHECK(!construction.used);
    }
    fault = 0;
    CHECK(tb_symbol_budget_init(&construction, capacity - 1));
    CHECK(!tb_symbol_name_order_create(inputs, &construction) && !construction.used);
    CHECK(tb_symbol_budget_init(&construction, 1024 * 1024));
    scopes[1].strings[1].end_offset = scopes[1].strings[1].offset + scopes[1].strings[1].bytes;
    comparison = tb_symbol_name_order_create(inputs, &construction);
    CHECK(comparison);
    CHECK(run(comparison, 7) == TB_SYMBOL_MALFORMED);
    TbSymbolNameOrderFailure_t failure = tb_symbol_name_order_failure(comparison);
    CHECK(failure.input == 1 && failure.component == 1 && failure.reason == TB_SYMBOL_STRING_ORDER_RANGE);
    tb_symbol_name_order_destroy(comparison);
    ++scopes[1].strings[1].end_offset;
    comparison = tb_symbol_name_order_create(inputs, &construction);
    CHECK(comparison);
    uint64_t progress = tb_symbol_name_order_progress(comparison);
    CHECK(tb_symbol_name_order_step(comparison, 0) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_name_order_step(comparison, 65537) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_name_order_progress(comparison) == progress);
    read_error = EIO;
    CHECK(run(comparison, 7) == TB_SYMBOL_OPERATIONAL_FAILURE);
    failure = tb_symbol_name_order_failure(comparison);
    CHECK(failure.reason == TB_SYMBOL_STRING_ORDER_READ && failure.read.system_error == EIO);
    tb_symbol_name_order_destroy(comparison);
    CHECK(!construction.used);
    comparison = tb_symbol_name_order_create(inputs, &construction);
    CHECK(comparison);
    read_error = EINTR;
    short_read = true;
    CHECK(run(comparison, 7) == TB_SYMBOL_SUCCESS);
    short_read = false;
    tb_symbol_name_order_destroy(comparison);
    CHECK(!construction.used);
    comparison = tb_symbol_name_order_create(inputs, &construction);
    CHECK(comparison);
    read_error = ENOMEM;
    CHECK(run(comparison, 7) == TB_SYMBOL_RESOURCE_FAILURE);
    failure = tb_symbol_name_order_failure(comparison);
    CHECK(failure.reason == TB_SYMBOL_STRING_ORDER_READ && failure.read.system_error == ENOMEM);
    tb_symbol_name_order_destroy(comparison);
    CHECK(!construction.used);
    comparison = tb_symbol_name_order_create(inputs, &construction);
    CHECK(comparison);
    unsigned interrupted = 0;
    for (unsigned turn = 0; turn < 1000 && interrupted < 3; ++turn) {
        read_error = EINTR;
        read_calls = 0;
        uint64_t before = tb_symbol_name_order_progress(comparison);
        CHECK(tb_symbol_name_order_step(comparison, 7) == TB_SYMBOL_PENDING);
        if (read_calls) {
            CHECK(read_calls == 1 && tb_symbol_name_order_progress(comparison) == before);
            ++interrupted;
        }
    }
    CHECK(interrupted == 3);
    read_error = 0;
    tb_symbol_name_order_destroy(comparison);
    CHECK(!construction.used);
    comparison = tb_symbol_name_order_create(inputs, &construction);
    CHECK(comparison);
    early_eof = true;
    CHECK(run(comparison, 7) == TB_SYMBOL_OPERATIONAL_FAILURE);
    early_eof = false;
    failure = tb_symbol_name_order_failure(comparison);
    CHECK(failure.reason == TB_SYMBOL_STRING_ORDER_READ);
    tb_symbol_name_order_destroy(comparison);
    for (unsigned mode = 0; mode < 2; ++mode) {
        int descriptor = open(scopes[0].path, mode ? O_RDONLY : O_PATH | O_CLOEXEC);
        CHECK(descriptor >= 0);
        inputs[0].descriptor = descriptor;
        comparison = tb_symbol_name_order_create(inputs, &construction);
        CHECK(comparison);
        CHECK(run(comparison, 7) == TB_SYMBOL_OPERATIONAL_FAILURE);
        failure = tb_symbol_name_order_failure(comparison);
        CHECK(failure.input == 0 && failure.reason == TB_SYMBOL_STRING_ORDER_READ);
        CHECK(failure.read.reason == TB_SYMBOL_FILE_DESCRIPTOR);
        tb_symbol_name_order_destroy(comparison);
        CHECK(fcntl(descriptor, F_GETFD) >= 0 && !close(descriptor));
    }
    CHECK(!construction.used);
    close_scope(&scopes[0]);
    close_scope(&scopes[1]);
}


static void flatten(char *destination, size_t capacity, const char *const names[], size_t count)
{
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(names[i]);
        if (i) {
            CHECK(capacity - used > 2);
            memcpy(destination + used, "::", 2);
            used += 2;
        }
        CHECK(length < capacity - used);
        memcpy(destination + used, names[i], length);
        used += length;
    }
    destination[used] = 0;
}


static void check_reference_sequences(void)
{
    uint32_t state = 0x1a2b3c4d;
    static const char alphabet[] = "ab:_<> ";
    for (unsigned trial = 0; trial < 32; ++trial) {
        char components[2][4][64];
        const char *names[2][4];
        size_t counts[2];
        char reference[2][512];
        for (unsigned side = 0; side < 2; ++side) {
            state = state * 1664525u + 1013904223u;
            counts[side] = state % 5;
            for (size_t i = 0; i < counts[side]; ++i) {
                state = state * 1664525u + 1013904223u;
                size_t length = state % 64;
                for (size_t at = 0; at < length; ++at) {
                    state = state * 1664525u + 1013904223u;
                    components[side][i][at] = alphabet[state % (sizeof(alphabet) - 1)];
                }
                components[side][i][length] = 0;
                names[side][i] = components[side][i];
            }
            flatten(reference[side], sizeof(reference[side]), names[side], counts[side]);
        }
        int order = strcmp(reference[0], reference[1]);
        check_pair(names[0], counts[0], names[1], counts[1],
                   order < 0 ? TB_SYMBOL_STRING_BEFORE : order > 0 ? TB_SYMBOL_STRING_AFTER
                                                                  : TB_SYMBOL_STRING_EQUAL);
        const char *flat[] = {reference[0]};
        check_pair(names[0], counts[0], flat, 1, TB_SYMBOL_STRING_EQUAL);
    }
}


static void check_interrupted_lifetimes(void)
{
    Scope_t scopes[2];
    const char *left[] = {"N", "X<A::B>", "operator A::B"};
    const char *right[] = {"N::X<A::B>::operator A::B"};
    open_names(&scopes[0], left, 3);
    open_names(&scopes[1], right, 1);
    size_t source_used[2] = {scopes[0].budget.used, scopes[1].budget.used};
    TbSymbolNameOrderInput_t inputs[2] = {scopes[0].input, scopes[1].input};
    TbSymbolBudget_t construction;
    CHECK(tb_symbol_budget_init(&construction, 1024 * 1024));
    bool complete = false;
    for (unsigned cut = 0; cut < 1000 && !complete; ++cut) {
        read_calls = 0;
        TbSymbolNameOrder_t *comparison = tb_symbol_name_order_create(inputs, &construction);
        CHECK(comparison && !read_calls);
        for (unsigned step = 0; step < cut; ++step) {
            TbSymbolStringOrder_e order = TB_SYMBOL_STRING_AFTER;
            CHECK(!tb_symbol_name_order_result(comparison, &order) && order == TB_SYMBOL_STRING_AFTER);
            TbSymbolOutcome_e outcome = tb_symbol_name_order_step(comparison, 7);
            if (outcome != TB_SYMBOL_PENDING) {
                CHECK(outcome == TB_SYMBOL_SUCCESS);
                CHECK(tb_symbol_name_order_result(comparison, &order) && order == TB_SYMBOL_STRING_EQUAL);
                complete = true;
                break;
            }
        }
        read_calls = 0;
        tb_symbol_name_order_destroy(comparison);
        CHECK(!read_calls && !construction.used);
        for (unsigned side = 0; side < 2; ++side) {
            CHECK(scopes[side].budget.used == source_used[side]);
            CHECK(fcntl(scopes[side].descriptor, F_GETFD) >= 0);
        }
    }
    CHECK(complete);
    close_scope(&scopes[0]);
    close_scope(&scopes[1]);
}


static void check_length_accounting(void)
{
    Scope_t scope;
    const char *names[] = {"A"};
    open_names(&scope, names, 1);
    TbSymbolElfHeader_t header = *scope.directory.header;
    TbSymbolSectionEntry_t sections[2];
    memcpy(sections, scope.directory.entries, sizeof(sections));
    header.file_bytes = INT64_MAX;
    sections[1].header.bytes = INT64_MAX - sections[1].header.offset;
    TbSymbolSectionsView_t directory = scope.directory;
    directory.header = &header;
    directory.entries = sections;
    directory.backing.bytes = INT64_MAX;
    TbSymbolStringView_t parts[3];
    for (size_t i = 0; i < 3; ++i) {
        parts[i] = (TbSymbolStringView_t){
            .backing       = directory.backing,
            .section_index = 1,
            .bytes         = sections[1].header.bytes - 1,
            .end_offset    = sections[1].header.bytes
        };
    }
    TbSymbolNameOrderInput_t inputs[2] = {
        {
            .directory  = directory,
            .components = parts,
            .count      = 3,
            .descriptor = scope.descriptor
        },
        scope.input
    };
    TbSymbolBudget_t construction;
    CHECK(tb_symbol_budget_init(&construction, 1024 * 1024));
    /* Synthetic coordinates test derived-length overflow before file access;
     * this is not a claim that an enormous backing file was created/decoded. */
    TbSymbolNameOrder_t *comparison = tb_symbol_name_order_create(inputs, &construction);
    CHECK(comparison);
    read_calls = 0;
    CHECK(run(comparison, 7) == TB_SYMBOL_RESOURCE_FAILURE && !read_calls);
    TbSymbolNameOrderFailure_t failure = tb_symbol_name_order_failure(comparison);
    CHECK(failure.input == 0 && failure.component == 2 && failure.reason == TB_SYMBOL_STRING_ORDER_RANGE);
    tb_symbol_name_order_destroy(comparison);
    CHECK(!construction.used);
    inputs[0] = scope.input;
    inputs[0].count = SIZE_MAX;
    CHECK(!tb_symbol_name_order_create(inputs, &construction) && !construction.used);
    inputs[0] = scope.input;
    inputs[0].components = NULL;
    CHECK(!tb_symbol_name_order_create(inputs, &construction) && !construction.used);
    inputs[0] = scope.input;
    inputs[0].descriptor = -1;
    CHECK(!tb_symbol_name_order_create(inputs, &construction) && !construction.used);
    close_scope(&scope);
}


static void check_replacement_peak(void)
{
    Scope_t scopes[2];
    const char *left[] = {"A", "B"};
    const char *right[] = {"A::B"};
    open_names(&scopes[0], left, 2);
    open_names(&scopes[1], right, 1);
    TbSymbolNameOrderInput_t inputs[2] = {scopes[0].input, scopes[1].input};
    TbSymbolBudget_t construction;
    CHECK(tb_symbol_budget_init(&construction, 1024 * 1024));
    TbSymbolNameOrder_t *old = tb_symbol_name_order_create(inputs, &construction);
    CHECK(old);
    size_t owned = construction.used;
    tb_symbol_name_order_destroy(old);
    CHECK(!construction.used && owned <= SIZE_MAX / 2);
    CHECK(tb_symbol_budget_init(&construction, 2 * owned - 1));
    old = tb_symbol_name_order_create(inputs, &construction);
    CHECK(old && run(old, 7) == TB_SYMBOL_SUCCESS);
    CHECK(construction.used == owned);
    CHECK(!tb_symbol_name_order_create(inputs, &construction));
    CHECK(construction.used == owned);
    TbSymbolStringOrder_e order;
    CHECK(tb_symbol_name_order_result(old, &order) && order == TB_SYMBOL_STRING_EQUAL);
    tb_symbol_name_order_destroy(old);
    CHECK(!construction.used);
    close_scope(&scopes[0]);
    close_scope(&scopes[1]);
}


int main(void)
{
    const char *left[] = {"A", "B"};
    const char *right[] = {"A::B"};
    check_pair(left, 2, right, 1, TB_SYMBOL_STRING_EQUAL);
    const char *templates[] = {"N", "X<M::Y>", "operator A::B"};
    const char *flat[] = {"N::X<M::Y>::operator A::B"};
    check_pair(templates, 3, flat, 1, TB_SYMBOL_STRING_EQUAL);
    check_pair(NULL, 0, NULL, 0, TB_SYMBOL_STRING_EQUAL);
    const char *empty[] = {""};
    check_pair(NULL, 0, empty, 1, TB_SYMBOL_STRING_EQUAL);
    check_pair(empty, 1, left, 2, TB_SYMBOL_STRING_BEFORE);
    check_pair(left, 2, empty, 1, TB_SYMBOL_STRING_AFTER);
    const char *suffix[] = {"A", "B", "C"};
    check_pair(left, 2, suffix, 3, TB_SYMBOL_STRING_BEFORE);
    check_pair(suffix, 3, left, 2, TB_SYMBOL_STRING_AFTER);
    const char *empty_parts[] = {"", "", ""};
    const char *colons[] = {"::::"};
    check_pair(empty_parts, 3, colons, 1, TB_SYMBOL_STRING_EQUAL);
    const char *upper[] = {"A", "B"};
    const char *lower[] = {"a", "B"};
    check_pair(upper, 2, lower, 2, TB_SYMBOL_STRING_BEFORE);
    const char high_byte[] = {(char)0x80, 0};
    const char low_byte[] = {(char)0x7f, 0};
    const char *high[] = {high_byte};
    const char *low[] = {low_byte};
    check_pair(high, 1, low, 1, TB_SYMBOL_STRING_AFTER);
    const unsigned char composed[] = {0xc3, 0xa9, 0};
    const unsigned char decomposed[] = {'e', 0xcc, 0x81, 0};
    const char *first[] = {(const char *)composed};
    const char *second[] = {(const char *)decomposed};
    check_pair(first, 1, second, 1, TB_SYMBOL_STRING_AFTER);
    check_long_boundary();
    check_failures();
    check_reference_sequences();
    check_interrupted_lifetimes();
    check_length_accounting();
    check_replacement_peak();
    puts("PASS complete name byte order, source boundaries, bounded reads and failure cleanup");
    return 0;
}
