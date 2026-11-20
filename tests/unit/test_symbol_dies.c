#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Descriptor fixture flags. */
#include "symbol/dies.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define INFO_OFFSET 1024U
#define ABBREV_OFFSET 131072U

static unsigned char image[262144];
static unsigned width;
static unsigned offset_width;
static unsigned version;
static TbSymbolByteOrder_e byte_order;
static bool type_unit;
static uint64_t body_cursor;
static uint64_t header_bytes;
static uint64_t abbrev_bytes;
static uint64_t selected_type;
static uint64_t selected_unit;
static unsigned fail_allocation;
static unsigned allocations;
static unsigned read_mode;
static unsigned reads;
static size_t inspected;
static bool retain_writer;
static bool mutate_on_allocate;
static int mutation_fd = -1;

void *__real_calloc(size_t count, size_t size);
void *__real_malloc(size_t size);
ssize_t __real_pread(int fd, void *bytes, size_t count, off_t offset);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_malloc(size_t size);
ssize_t __wrap_pread(int fd, void *bytes, size_t count, off_t offset);

void *__wrap_calloc(size_t count, size_t size)
{
    if (fail_allocation && ++allocations == fail_allocation)
        return NULL;
    return __real_calloc(count, size);
}


void *__wrap_malloc(size_t size)
{
    if (mutate_on_allocate) {
        unsigned char changed = 99;
        CHECK(mutation_fd >= 0 && pwrite(mutation_fd, &changed, 1,
                                         (off_t)(INFO_OFFSET + header_bytes)) == 1);
        struct stat status;
        CHECK(!fstat(mutation_fd, &status));
        struct timespec times[] = {status.st_atim, status.st_mtim};
        ++times[1].tv_sec;
        CHECK(!futimens(mutation_fd, times));
        mutate_on_allocate = false;
    }
    if (fail_allocation && ++allocations == fail_allocation)
        return NULL;
    return __real_malloc(size);
}


ssize_t __wrap_pread(int fd, void *bytes, size_t count, off_t offset)
{
    ++reads;
    inspected += count;
    if (read_mode == 1 && count > 3)
        count = 3;
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


static void put(uint64_t at, unsigned bytes, uint64_t value)
{
    CHECK(bytes <= 8 && at + bytes <= sizeof(image));
    for (unsigned i = 0; i < bytes; ++i) {
        unsigned shift = byte_order == TB_SYMBOL_LITTLE_ENDIAN ? i * 8 : (bytes - i - 1) * 8;
        image[at + i] = (unsigned char)(value >> shift);
    }
}


static void section(unsigned index, uint64_t offset, uint64_t bytes)
{
    uint64_t at = (width == 4 ? 52U : 64U) + (uint64_t)index * (width == 4 ? 40U : 64U);
    put(at + 4, 4, SHT_PROGBITS);
    put(at + (width == 4 ? 16U : 24U), width, offset);
    put(at + (width == 4 ? 20U : 32U), width, bytes);
    put(at + (width == 4 ? 32U : 48U), width, 1);
}


static void body_byte(unsigned byte)
{
    CHECK(INFO_OFFSET + body_cursor < ABBREV_OFFSET);
    image[INFO_OFFSET + body_cursor++] = (unsigned char)byte;
}


static void body_string(const char *text)
{
    do {
        body_byte((unsigned char)*text);
    } while (*text++);
}


static void variable(const char *name)
{
    body_byte(2);
    body_string(name);
    put(INFO_OFFSET + body_cursor, 4, 0);
    body_cursor += 4;
}


static void fixture(void)
{
    memset(image, 0, sizeof(image));
    memcpy(image, "\177ELF", 4);
    image[EI_CLASS] = width == 4 ? ELFCLASS32 : ELFCLASS64;
    image[EI_DATA] = byte_order == TB_SYMBOL_LITTLE_ENDIAN ? ELFDATA2LSB : ELFDATA2MSB;
    image[EI_VERSION] = EV_CURRENT;
    put(16, 2, ET_EXEC);
    put(18, 2, EM_X86_64);
    put(20, 4, EV_CURRENT);
    put(width == 4 ? 32U : 40U, width, width == 4 ? 52U : 64U);
    put(width == 4 ? 40U : 52U, 2, width == 4 ? 52U : 64U);
    put(width == 4 ? 46U : 58U, 2, width == 4 ? 40U : 64U);
    put(width == 4 ? 48U : 60U, 2, 3);
    unsigned initial = offset_width == 4 ? 4 : 12;
    if (initial == 12)
        put(INFO_OFFSET, 4, UINT32_MAX);
    put(INFO_OFFSET + initial, 2, version);
    uint64_t at = initial + 2;
    if (version == 5) {
        put(INFO_OFFSET + at++, 1, type_unit ? 2U : 1U);
        put(INFO_OFFSET + at++, 1, 3);
    }
    put(INFO_OFFSET + at, offset_width, 0);
    at += offset_width;
    if (version == 4)
        put(INFO_OFFSET + at++, 1, 3);
    if (type_unit) {
        put(INFO_OFFSET + at, 8, 1);
        at += 8;
        at += offset_width;
    }
    header_bytes = at;
    body_cursor = at;
    selected_type = 0;
    const unsigned char schema[] = {
        1, 0x11, 1, 3, TB_SYMBOL_FORM_STRING, 0, 0,
        2, 0x34, 0, 3, TB_SYMBOL_FORM_STRING, 0x49, TB_SYMBOL_FORM_REF4, 0, 0,
        3, 0x13, 1, 0, 0,
        0
    };
    memcpy(image + ABBREV_OFFSET, schema, sizeof(schema));
    if (type_unit)
        image[ABBREV_OFFSET + 1] = 0x41;
    abbrev_bytes = sizeof(schema);
}


static void finish_fixture(void)
{
    unsigned initial = offset_width == 4 ? 4 : 12;
    put(INFO_OFFSET + (initial == 4 ? 0U : 4U), offset_width, body_cursor - initial);
    if (type_unit)
        put(INFO_OFFSET + header_bytes - offset_width, offset_width, selected_type);
    section(1, INFO_OFFSET, body_cursor);
    section(2, ABBREV_OFFSET, abbrev_bytes);
}


static void simple(void)
{
    fixture();
    body_byte(1);
    body_string("cu");
    selected_type = body_cursor;
    body_byte(3);
    variable("field");
    body_byte(0);
    variable("global");
    body_byte(0);
    body_byte(0);
    body_byte(0);
    finish_fixture();
}


typedef struct {
    TbSymbolBudget_t       budget;
    TbSymbolSections_t    *directory;
    TbSymbolUnits_t       *units;
    TbSymbolAbbrev_t      *abbrev;
    TbSymbolSectionsView_t directory_view;
    TbSymbolUnitsView_t    units_view;
    int                   descriptor;
    char                  path[64];
    size_t                baseline;
} Scope_t;

static void open_scope(Scope_t *scope)
{
    *scope = (Scope_t){0};
    CHECK(tb_symbol_budget_init(&scope->budget, 8 * 1024 * 1024));
    strcpy(scope->path, "/tmp/tb-symbol-dies-XXXXXX");
    int writer = mkstemp(scope->path);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(!fchmod(writer, 0400));
    if (retain_writer)
        mutation_fd = writer;
    else
        CHECK(!close(writer));
    scope->descriptor = open(scope->path, O_RDONLY | O_CLOEXEC);
    CHECK(scope->descriptor >= 0);
    struct stat status;
    CHECK(!fstat(scope->descriptor, &status));
    scope->directory = tb_symbol_sections_create((TbSymbolStorageInfo_t){
        .key = {
            .device = (uint64_t)status.st_dev,
            .inode  = (uint64_t)status.st_ino
        },
        .bytes = sizeof(image)
    }, scope->descriptor, &scope->budget);
    CHECK(scope->directory);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 10000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_sections_step(scope->directory, 64);
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(scope->directory, &scope->directory_view));
    scope->units = tb_symbol_units_create(scope->directory_view, (TbSymbolUnitSelection_t){
        .section_index = 1,
        .kind          = type_unit && version == 4 ? TB_SYMBOL_DWARF_TYPES : TB_SYMBOL_DWARF_INFO,
        .descriptor    = scope->descriptor
    }, &scope->budget);
    CHECK(scope->units);
    outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 10000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_units_step(scope->units, 64);
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_units_view(scope->units, &scope->units_view));
    CHECK(scope->units_view.count == selected_unit + 1);
    scope->abbrev = tb_symbol_abbrev_create(scope->directory_view, (TbSymbolAbbrevSelection_t){
        .section_index = 2,
        .table_offset  = 0,
        .version       = version,
        .descriptor    = scope->descriptor
    }, &scope->budget);
    CHECK(scope->abbrev);
    outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 10000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_abbrev_step(scope->abbrev, 64);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    scope->baseline = scope->budget.used;
}


static void close_scope(Scope_t *scope)
{
    CHECK(scope->budget.used == scope->baseline && fcntl(scope->descriptor, F_GETFD) >= 0);
    tb_symbol_abbrev_destroy(scope->abbrev);
    tb_symbol_units_destroy(scope->units);
    tb_symbol_sections_destroy(scope->directory);
    CHECK(!scope->budget.used && !close(scope->descriptor) && !unlink(scope->path));
}


static TbSymbolDies_t *create(Scope_t *scope)
{
    return tb_symbol_dies_create(scope->directory_view, scope->units_view, (TbSymbolDieSelection_t){
        .unit_index = selected_unit,
        .descriptor = scope->descriptor
    }, scope->abbrev, &scope->budget);
}


static void unavailable(TbSymbolDies_t *dies)
{
    TbSymbolDiesView_t view;
    unsigned char before[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(before, &view, sizeof(view));
    CHECK(!tb_symbol_dies_view(dies, &view) && !memcmp(before, &view, sizeof(view)));
}


static TbSymbolOutcome_e step(TbSymbolDies_t *dies)
{
    reads = 0;
    inspected = 0;
    TbSymbolOutcome_e outcome = tb_symbol_dies_step(dies, 64);
    CHECK(reads <= 1 && inspected <= 64);
    if (outcome != TB_SYMBOL_SUCCESS)
        unavailable(dies);
    return outcome;
}


static TbSymbolOutcome_e complete(TbSymbolDies_t *dies)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 3000000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = step(dies);
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


static void check_scope(TbSymbolOutcome_e expected, uint64_t count, uint64_t attributes, uint64_t depth)
{
    Scope_t scope;
    open_scope(&scope);
    reads = 0;
    TbSymbolDies_t *dies = create(&scope);
    CHECK(dies && !reads);
    unavailable(dies);
    TbSymbolOutcome_e outcome = complete(dies);
    if (outcome != expected) {
        TbSymbolDiesFailure_t failure = tb_symbol_dies_failure(dies);
        fprintf(stderr, "DIE outcome %u expected %u, reason %u at %llu\n", outcome, expected,
                failure.reason, (unsigned long long)failure.offset);
    }
    CHECK(outcome == expected);
    if (outcome == TB_SYMBOL_SUCCESS) {
        TbSymbolDiesView_t view;
        CHECK(tb_symbol_dies_view(dies, &view));
        CHECK(view.count == count && view.attribute_count == attributes && view.max_depth == depth);
        CHECK(view.abbrev_section == 2 && !view.abbrev_offset);
        CHECK(view.section_index == 1 && !view.unit_offset && view.kind ==
              (type_unit ? TB_SYMBOL_DWARF_TYPE : TB_SYMBOL_DWARF_COMPILE));
        uint64_t previous = header_bytes;
        uint64_t seen = 0;
        for (uint64_t i = 0; i < view.count; ++i) {
            const TbSymbolDieEntry_t *entry = &view.entries[i];
            CHECK(entry->offset >= previous && entry->end_offset > entry->offset);
            CHECK(entry->first_attribute == seen && entry->attribute_count <= attributes - seen);
            CHECK(entry->parent == UINT64_MAX ? !i : entry->parent < i);
            for (uint64_t j = 0; j < entry->attribute_count; ++j) {
                const TbSymbolDieAttribute_t *attr = &view.attributes[seen++];
                CHECK(attr->offset >= entry->offset && attr->offset <= entry->end_offset);
                CHECK(attr->value.payload_offset >= attr->offset);
                CHECK(attr->value.payload_offset + attr->value.payload_bytes <= entry->end_offset);
            }
            previous = entry->end_offset;
        }
        CHECK(seen == attributes);
        if (count == 4) {
            CHECK(view.entries[1].parent == 0 && view.entries[2].parent == 1 && view.entries[3].parent == 0);
        }
    }
    tb_symbol_dies_destroy(dies);
    close_scope(&scope);
}


static void failures(void)
{
    simple();
    image[INFO_OFFSET + header_bytes] = 99;
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    fixture();
    body_byte(1);
    body_string("cu");
    finish_fixture();
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    fixture();
    body_byte(1);
    body_string("cu");
    body_byte(0);
    body_byte(1);
    body_string("second");
    body_byte(0);
    finish_fixture();
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    fixture();
    body_byte(1);
    body_byte('x');
    finish_fixture();
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    fixture();
    body_byte(0);
    finish_fixture();
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    fixture();
    body_byte(1);
    body_string("cu");
    body_byte(0x80);
    finish_fixture();
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    simple();
    fixture();
    body_byte(1);
    body_string("cu");
    variable("valid");
    body_byte(99);
    finish_fixture();
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    simple();
    image[ABBREV_OFFSET + 1] = 0x7f;
    check_scope(TB_SYMBOL_UNSUPPORTED, 0, 0, 0);
    type_unit = true;
    simple();
    selected_type = header_bytes + 2;
    finish_fixture();
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    simple();
    image[ABBREV_OFFSET + 1] = 0x11;
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    type_unit = false;
    fixture();
    for (unsigned i = 0; i < 9; ++i)
        body_byte(0x80);
    body_byte(2);
    finish_fixture();
    check_scope(TB_SYMBOL_UNSUPPORTED, 0, 0, 0);
    fixture();
    body_byte(1);
    body_string("cu");
    body_byte(2);
    body_string("short");
    body_byte(1);
    finish_fixture();
    check_scope(TB_SYMBOL_MALFORMED, 0, 0, 0);
    simple();
    Scope_t scope;
    open_scope(&scope);
    TbSymbolUnitsView_t mismatched = scope.units_view;
    ++mismatched.backing.key.inode;
    CHECK(!tb_symbol_dies_create(scope.directory_view, mismatched, (TbSymbolDieSelection_t){
        .unit_index = 0,
        .descriptor = scope.descriptor
    }, scope.abbrev, &scope.budget));
    TbSymbolDies_t *limited = create(&scope);
    CHECK(limited);
    size_t old_limit = scope.budget.limit;
    scope.budget.limit = scope.budget.used;
    CHECK(complete(limited) == TB_SYMBOL_RESOURCE_FAILURE);
    tb_symbol_dies_destroy(limited);
    scope.budget.limit = old_limit;
    CHECK(scope.budget.used == scope.baseline);
    for (unsigned fault = 1; fault <= 5; ++fault) {
        fail_allocation = fault;
        allocations = 0;
        TbSymbolDies_t *dies = create(&scope);
        if (dies) {
            CHECK(complete(dies) == TB_SYMBOL_RESOURCE_FAILURE);
            tb_symbol_dies_destroy(dies);
        }
        fail_allocation = 0;
        CHECK(allocations >= fault && scope.budget.used == scope.baseline);
    }
    for (unsigned stop = 0; stop < 128; ++stop) {
        TbSymbolDies_t *dies = create(&scope);
        CHECK(dies);
        for (unsigned i = 0; i < stop; ++i)
            (void)step(dies);
        tb_symbol_dies_destroy(dies);
        CHECK(scope.budget.used == scope.baseline);
    }
    for (unsigned mode = 1; mode <= 4; ++mode) {
        TbSymbolDies_t *dies = create(&scope);
        CHECK(dies);
        read_mode = mode;
        if (mode == 2) {
            uint64_t progress = 0;
            bool stalled = false;
            for (unsigned turn = 0; turn < 32; ++turn) {
                CHECK(step(dies) == TB_SYMBOL_PENDING);
                uint64_t next = tb_symbol_dies_progress(dies);
                if (reads && next == progress)
                    stalled = true;
                progress = next;
            }
            CHECK(stalled);
            read_mode = 0;
            CHECK(complete(dies) == TB_SYMBOL_SUCCESS);
        } else {
            CHECK(complete(dies) == (mode == 1 ? TB_SYMBOL_SUCCESS : TB_SYMBOL_OPERATIONAL_FAILURE));
        }
        read_mode = 0;
        tb_symbol_dies_destroy(dies);
    }
    close_scope(&scope);
}


static void zero_footprints(void)
{
    fixture();
    const unsigned char schema[] = {
        1, 0x11, 0, 0x1c, TB_SYMBOL_FORM_IMPLICIT_CONST, 0x6f,
        0x3f, TB_SYMBOL_FORM_FLAG_PRESENT, 0, 0, 0
    };
    memcpy(image + ABBREV_OFFSET, schema, sizeof(schema));
    abbrev_bytes = sizeof(schema);
    body_byte(1);
    finish_fixture();
    check_scope(TB_SYMBOL_SUCCESS, 1, 2, 0);
    Scope_t scope;
    open_scope(&scope);
    TbSymbolDies_t *dies = create(&scope);
    CHECK(dies && complete(dies) == TB_SYMBOL_SUCCESS);
    TbSymbolDiesView_t view;
    CHECK(tb_symbol_dies_view(dies, &view));
    CHECK(!view.attributes[0].value.bytes && !view.attributes[0].value.numeric);
    CHECK(view.attributes[0].schema_attribute == 0 && view.attributes[0].value.payload_offset == body_cursor);
    CHECK(!view.attributes[1].value.bytes && view.attributes[1].value.numeric &&
          view.attributes[1].value.bits == 1);
    TbSymbolAbbrevView_t abbrev;
    CHECK(tb_symbol_abbrev_view(scope.abbrev, &abbrev) && abbrev.attributes[0].implicit_value == -17);
    tb_symbol_dies_destroy(dies);
    close_scope(&scope);
}


static void mutation(void)
{
    simple();
    retain_writer = true;
    Scope_t scope;
    open_scope(&scope);
    retain_writer = false;
    TbSymbolDies_t *dies = create(&scope);
    CHECK(dies);
    mutate_on_allocate = true;
    CHECK(complete(dies) == TB_SYMBOL_OPERATIONAL_FAILURE && !mutate_on_allocate);
    CHECK(tb_symbol_dies_failure(dies).read.reason == TB_SYMBOL_FILE_CHANGED);
    tb_symbol_dies_destroy(dies);
    CHECK(!close(mutation_fd));
    mutation_fd = -1;
    close_scope(&scope);
}


static void partial_units(void)
{
    for (version = 4; version <= 5; ++version) {
        simple();
        image[ABBREV_OFFSET + 1] = 0x3c;
        if (version == 5)
            put(INFO_OFFSET + 6, 1, 3);
        Scope_t scope;
        open_scope(&scope);
        TbSymbolDies_t *dies = create(&scope);
        CHECK(dies && complete(dies) == TB_SYMBOL_SUCCESS);
        TbSymbolDiesView_t view;
        CHECK(tb_symbol_dies_view(dies, &view) && view.kind == TB_SYMBOL_DWARF_PARTIAL);
        CHECK(scope.units_view.entries[0].header.kind ==
              (version == 4 ? TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL : TB_SYMBOL_DWARF_PARTIAL));
        tb_symbol_dies_destroy(dies);
        close_scope(&scope);
    }
    version = 5;
}


static void separate_units(void)
{
    simple();
    uint64_t unit_bytes = body_cursor;
    CHECK(INFO_OFFSET + unit_bytes * 2 < ABBREV_OFFSET);
    memcpy(image + INFO_OFFSET + unit_bytes, image + INFO_OFFSET, (size_t)unit_bytes);
    section(1, INFO_OFFSET, unit_bytes * 2);
    selected_unit = 1;
    Scope_t scope;
    open_scope(&scope);
    TbSymbolDies_t *second = create(&scope);
    CHECK(second && complete(second) == TB_SYMBOL_SUCCESS);
    selected_unit = 0;
    TbSymbolDies_t *first = create(&scope);
    CHECK(first && complete(first) == TB_SYMBOL_SUCCESS);
    TbSymbolDiesView_t left;
    TbSymbolDiesView_t right;
    CHECK(tb_symbol_dies_view(first, &left) && tb_symbol_dies_view(second, &right));
    CHECK(!left.unit_offset && right.unit_offset == unit_bytes);
    CHECK(left.count == right.count && left.attribute_count == right.attribute_count);
    for (uint64_t i = 0; i < left.count; ++i) {
        CHECK(right.entries[i].offset == left.entries[i].offset + unit_bytes);
        CHECK(right.entries[i].parent == left.entries[i].parent);
    }
    for (uint64_t i = 0; i < left.attribute_count; ++i) {
        CHECK(right.attributes[i].offset == left.attributes[i].offset + unit_bytes);
        CHECK(right.attributes[i].value.payload_offset ==
              left.attributes[i].value.payload_offset + unit_bytes);
    }
    tb_symbol_dies_destroy(first);
    CHECK(tb_symbol_dies_view(second, &right));
    tb_symbol_dies_destroy(second);
    close_scope(&scope);
}


static void large(void)
{
    fixture();
    body_byte(1);
    for (unsigned i = 0; i < 70000; ++i)
        body_byte('x');
    body_byte(0);
    body_byte(0);
    finish_fixture();
    check_scope(TB_SYMBOL_SUCCESS, 1, 1, 1);
    fixture();
    body_byte(1);
    body_string("cu");
    for (unsigned i = 0; i < 10000; ++i)
        body_byte(3);
    for (unsigned i = 0; i < 10001; ++i)
        body_byte(0);
    finish_fixture();
    check_scope(TB_SYMBOL_SUCCESS, 10001, 1, 10001);
    fixture();
    body_byte(1);
    body_string("cu");
    for (unsigned i = 0; i < 10000; ++i)
        variable("x");
    body_byte(0);
    finish_fixture();
    check_scope(TB_SYMBOL_SUCCESS, 10001, 20001, 1);
}


int main(void)
{
    for (width = 4; width <= 8; width += 4) {
        for (offset_width = 4; offset_width <= 8; offset_width += 4) {
            for (unsigned order = 0; order < 2; ++order) {
                byte_order = (TbSymbolByteOrder_e)order;
                for (version = 4; version <= 5; ++version) {
                    for (unsigned type = 0; type < 2; ++type) {
                        type_unit = type != 0;
                        simple();
                        check_scope(TB_SYMBOL_SUCCESS, 4, 5, 2);
                    }
                }
            }
        }
    }
    width = 8;
    offset_width = 4;
    version = 5;
    byte_order = TB_SYMBOL_LITTLE_ENDIAN;
    type_unit = false;
    failures();
    large();
    zero_footprints();
    separate_units();
    partial_units();
    mutation();
    puts("PASS complete bounded DIE trees, physical spans, depth and failure accounting");
    return 0;
}
