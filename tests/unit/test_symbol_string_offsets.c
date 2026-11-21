#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected descriptor fixtures. */
#include "symbol/string_offsets.h"
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


static uint64_t table(unsigned format, uint64_t count, uint64_t seed)
{
    uint64_t start = payload_bytes;
    unsigned initial = format == 4 ? 4 : 12;
    uint64_t bytes = initial + 4 + count * format;
    CHECK(bytes <= sizeof(image) - 512 - payload_bytes);
    if (format == 8)
        put(512 + start, 4, UINT32_MAX);
    put(512 + start + (format == 4 ? 0U : 4U), format, bytes - initial);
    put(512 + start + initial, 2, 5);
    put(512 + start + initial + 2, 2, 0);
    uint64_t base = start + initial + 4;
    for (uint64_t i = 0; i < count; ++i)
        put(512 + base + i * format, format, seed + i);
    payload_bytes += bytes;
    put(selected() + (width == 4 ? 20 : 32), width, payload_bytes);
    return base;
}


static TbSymbolStringOffsets_t *create(Scope_t *scope, uint64_t base, uint64_t index, unsigned format)
{
    reads = 0;
    TbSymbolStringOffsetSelection_t selection = {
        .section_index = 1,
        .base          = base,
        .index         = index,
        .offset_bytes  = format,
        .descriptor    = scope->descriptor
    };
    TbSymbolStringOffsets_t *offsets = tb_symbol_string_offsets_create(scope->view, selection,
                                                                     &scope->budget);
    CHECK(!reads);
    first_byte = 512;
    end_byte = 512 + payload_bytes;
    return offsets;
}


static void unavailable(TbSymbolStringOffsets_t *offsets)
{
    TbSymbolStringOffsetView_t view;
    unsigned char before[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(before, &view, sizeof(view));
    CHECK(!tb_symbol_string_offsets_view(offsets, &view) && !memcmp(before, &view, sizeof(view)));
}


static TbSymbolOutcome_e step(TbSymbolStringOffsets_t *offsets)
{
    reads = 0;
    inspected = 0;
    check_bounds = true;
    TbSymbolOutcome_e outcome = tb_symbol_string_offsets_step(offsets, 64);
    check_bounds = false;
    CHECK(reads <= 1 && inspected <= 64);
    if (outcome != TB_SYMBOL_SUCCESS)
        unavailable(offsets);
    return outcome;
}


static TbSymbolOutcome_e complete(TbSymbolStringOffsets_t *offsets)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = step(offsets);
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


typedef struct {
    uint64_t                      base;
    uint64_t                      index;
    unsigned                      format;
    TbSymbolOutcome_e             outcome;
    TbSymbolStringOffsetFailure_e reason;
    uint64_t                      count;
    uint64_t                      value;
} Expected_t;

static void check(Expected_t expected)
{
    Scope_t scope;
    open_scope(&scope);
    TbSymbolStringOffsets_t *offsets = create(&scope, expected.base, expected.index, expected.format);
    CHECK(offsets);
    unavailable(offsets);
    TbSymbolOutcome_e outcome = complete(offsets);
    if (outcome != expected.outcome || tb_symbol_string_offsets_failure(offsets).reason != expected.reason) {
        fprintf(stderr, "string offset expected %u/%u got %u/%u\n", expected.outcome, expected.reason,
                outcome, tb_symbol_string_offsets_failure(offsets).reason);
    }
    CHECK(outcome == expected.outcome && tb_symbol_string_offsets_failure(offsets).reason == expected.reason);
    if (outcome == TB_SYMBOL_SUCCESS) {
        TbSymbolStringOffsetView_t view;
        CHECK(tb_symbol_string_offsets_view(offsets, &view));
        CHECK(view.base == expected.base && view.index == expected.index &&
              view.offset_bytes == expected.format);
        CHECK(view.count == expected.count && view.string_offset == expected.value);
        CHECK(view.entry_offset == view.base + view.index * view.offset_bytes);
        CHECK(view.contribution_offset == view.base - (view.offset_bytes == 4 ? 8U : 16U));
        CHECK(view.end_offset == view.base + view.count * view.offset_bytes);
        CHECK(view.section_index == 1 && view.backing.key.inode == scope.view.backing.key.inode);
    }
    tb_symbol_string_offsets_destroy(offsets);
    close_scope(&scope);
}


static void layouts(void)
{
    fixture(0);
    uint64_t first = table(4, 3, 10);
    uint64_t second = table(8, 2, UINT32_MAX);
    check((Expected_t){
        .base    = first,
        .index   = 2,
        .format  = 4,
        .outcome = TB_SYMBOL_SUCCESS,
        .count   = 3,
        .value   = 12
    });
    check((Expected_t){
        .base    = second,
        .index   = 1,
        .format  = 8,
        .outcome = TB_SYMBOL_SUCCESS,
        .count   = 2,
        .value   = UINT64_C(1) << 32U
    });
    check((Expected_t){
        .base    = first,
        .index   = 3,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_INDEX
    });
    check((Expected_t){
        .base    = second,
        .index   = UINT64_MAX,
        .format  = 8,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_INDEX
    });
    check((Expected_t){
        .base    = first,
        .format  = 8,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_FORMAT
    });
    put(512 + 4, 2, 6);
    check((Expected_t){
        .base    = first,
        .format  = 4,
        .outcome = TB_SYMBOL_UNSUPPORTED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_VERSION
    });
    check((Expected_t){
        .base    = second,
        .index   = 1,
        .format  = 8,
        .outcome = TB_SYMBOL_SUCCESS,
        .count   = 2,
        .value   = UINT64_C(1) << 32U
    });
    fixture(0);
    first = table(4, 3, 0);
    put(512 + first, 4, 8);
    put(512 + first + 4, 2, 5);
    put(512 + first + 6, 2, 0);
    put(512 + first + 8, 4, 42);
    check((Expected_t){
        .base    = first + 8,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_BASE
    });
    fixture(0);
    first = table(8, 1, UINT64_MAX);
    check((Expected_t){
        .base    = first,
        .format  = 8,
        .outcome = TB_SYMBOL_SUCCESS,
        .count   = 1,
        .value   = UINT64_MAX
    });
    fixture(0);
    first = table(4, 0, 0);
    check((Expected_t){
        .base    = first,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_INDEX
    });
}


static void malformed(void)
{
    fixture(0);
    uint64_t base = table(4, 1, 42);
    put(512 + 6, 2, 1);
    check((Expected_t){
        .base    = base,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_PADDING
    });
    fixture(11);
    put(512, 4, 7);
    put(516, 2, 5);
    put(518, 2, 0);
    check((Expected_t){
        .base    = 8,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_HEADER
    });
    fixture(6);
    put(512, 4, 2);
    put(516, 2, 5);
    (void)table(4, 2, 1);
    check((Expected_t){
        .base    = 4,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_HEADER
    });
    fixture(4);
    put(512, 4, 0xfffffff0U);
    check((Expected_t){
        .base    = 4,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_LENGTH
    });
    fixture(1);
    check((Expected_t){
        .base    = 1,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_LENGTH
    });
    fixture(0);
    base = table(4, 1, 42);
    check((Expected_t){
        .base    = payload_bytes + 1,
        .format  = 4,
        .outcome = TB_SYMBOL_MALFORMED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_BASE
    });
    put(selected() + 8, width, SHF_COMPRESSED);
    check((Expected_t){
        .base    = base,
        .format  = 4,
        .outcome = TB_SYMBOL_UNSUPPORTED,
        .reason  = TB_SYMBOL_STRING_OFFSETS_SECTION
    });
}


static void failures(void)
{
    fixture(0);
    uint64_t base = table(4, 3, 42);
    Scope_t scope;
    open_scope(&scope);
    for (fault = 1; fault <= 2; ++fault) {
        allocations = 0;
        CHECK(!create(&scope, base, 1, 4));
        CHECK(allocations >= fault && scope.budget.used == scope.baseline);
    }
    fault = 0;
    size_t old_limit = scope.budget.limit;
    scope.budget.limit = scope.baseline;
    CHECK(!create(&scope, base, 1, 4) && scope.budget.used == scope.baseline);
    scope.budget.limit = old_limit;
    for (unsigned stop = 0; stop < 128; ++stop) {
        TbSymbolStringOffsets_t *offsets = create(&scope, base, 1, 4);
        CHECK(offsets);
        for (unsigned i = 0; i < stop; ++i)
            (void)step(offsets);
        tb_symbol_string_offsets_destroy(offsets);
        CHECK(scope.budget.used == scope.baseline);
    }
    for (unsigned mode = 1; mode <= 4; ++mode) {
        TbSymbolStringOffsets_t *offsets = create(&scope, base, 1, 4);
        CHECK(offsets);
        read_mode = mode;
        if (mode == 2) {
            uint64_t progress = 0;
            bool stalled = false;
            for (unsigned i = 0; i < 32; ++i) {
                CHECK(step(offsets) == TB_SYMBOL_PENDING);
                uint64_t next = tb_symbol_string_offsets_progress(offsets);
                if (reads && next == progress)
                    stalled = true;
                progress = next;
            }
            CHECK(stalled);
            read_mode = 0;
            CHECK(complete(offsets) == TB_SYMBOL_SUCCESS);
        } else {
            CHECK(complete(offsets) == (mode == 1 ? TB_SYMBOL_SUCCESS : TB_SYMBOL_OPERATIONAL_FAILURE));
        }
        read_mode = 0;
        tb_symbol_string_offsets_destroy(offsets);
    }
    TbSymbolStringOffsets_t *offsets = create(&scope, base, 1, 4);
    CHECK(offsets && tb_symbol_string_offsets_step(offsets, 63) == TB_SYMBOL_OPERATIONAL_FAILURE);
    unavailable(offsets);
    tb_symbol_string_offsets_destroy(offsets);
    close_scope(&scope);
}


static void many(void)
{
    fixture(0);
    for (unsigned i = 0; i < 1000; ++i)
        (void)table(i % 2 ? 4 : 8, 1, i);
    uint64_t base = table(8, 2, 10000);
    check((Expected_t){
        .base    = base,
        .index   = 1,
        .format  = 8,
        .outcome = TB_SYMBOL_SUCCESS,
        .count   = 2,
        .value   = 10001
    });
}


int main(void)
{
    for (width = 4; width <= 8; width += 4) {
        for (unsigned byte_order = 0; byte_order < 2; ++byte_order) {
            order = (TbSymbolByteOrder_e)byte_order;
            layouts();
            malformed();
        }
    }
    width = 8;
    order = TB_SYMBOL_LITTLE_ENDIAN;
    failures();
    many();
    puts("PASS bounded DWARF5 string-offset contributions, exact bases and selected entries");
    return 0;
}
