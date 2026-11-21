#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected descriptor fixtures. */
#include "symbol/text.h"
#include "../support/check.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned char image[8192];
static unsigned width;
static unsigned format;
static unsigned version;
static TbSymbolByteOrder_e order;
static unsigned fault;
static unsigned allocations;
static unsigned reads;
static size_t inspected;
static unsigned read_mode;

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


static void section(unsigned index, uint64_t offset, uint64_t bytes)
{
    size_t at = 128 + (size_t)index * (width == 4 ? 40U : 64U);
    put(at + 4, 4, SHT_PROGBITS);
    put(at + (width == 4 ? 16U : 24U), width, offset);
    put(at + (width == 4 ? 20U : 32U), width, bytes);
    put(at + (width == 4 ? 32U : 48U), width, 1);
}


typedef struct {
    TbSymbolBudget_t        budget;
    TbSymbolSections_t     *owner;
    TbSymbolSectionsView_t  directory;
    TbSymbolUnitEntry_t     unit;
    TbSymbolUnitsView_t     units;
    TbSymbolDieEntry_t      entries[2];
    TbSymbolDieAttribute_t  attributes[4];
    TbSymbolDiesView_t      tree;
    TbSymbolTextSelection_t selection;
    int                    descriptor;
    char                   path[64];
    size_t                 baseline;
} Scope_t;

static void prepare(Scope_t *scope, uint64_t field_form, uint64_t value, unsigned bases, unsigned fields,
                    bool empty)
{
    CHECK(bases <= 2 && fields <= 2);
    *scope = (Scope_t){0};
    memset(image, 0, sizeof(image));
    memcpy(image, ELFMAG, SELFMAG);
    image[EI_CLASS] = width == 4 ? ELFCLASS32 : ELFCLASS64;
    image[EI_DATA] = order == TB_SYMBOL_LITTLE_ENDIAN ? ELFDATA2LSB : ELFDATA2MSB;
    image[EI_VERSION] = EV_CURRENT;
    put(16, 2, ET_EXEC);
    put(18, 2, EM_RISCV);
    put(20, 4, EV_CURRENT);
    put(width == 4 ? 32U : 40U, width, 128);
    put(width == 4 ? 40U : 52U, 2, width == 4 ? 52U : 64U);
    put(width == 4 ? 46U : 58U, 2, width == 4 ? 40U : 64U);
    put(width == 4 ? 48U : 60U, 2, 5);
    section(1, 1024, 512);
    section(2, 2048, 13);
    section(3, 4096, (format == 4 ? 8U : 16U) + 2 * format);
    section(4, 6144, 5);
    memcpy(image + 2048, "hello\0\0world", 13);
    memcpy(image + 6144, "line", 5);
    unsigned initial = format == 4 ? 4 : 12;
    unsigned header_bytes = initial + (version == 5 ? 4U : 3U) + format;
    uint64_t base = initial + 4;
    if (format == 8) {
        put(1024, 4, UINT32_MAX);
        put(4096, 4, UINT32_MAX);
    }
    put(1024 + (format == 4 ? 0U : 4U), format, 512 - initial);
    put(1024 + initial, 2, version);
    if (version == 5) {
        put(1024 + initial + 2, 1, 1);
        put(1024 + initial + 3, 1, 8);
    } else {
        put(1024 + initial + 2 + format, 1, 8);
    }
    put(4096 + (format == 4 ? 0U : 4U), format, 4 + 2 * format);
    put(4096 + initial, 2, 5);
    put(4096 + base, format, 0);
    put(4096 + base + format, format, 7);
    scope->unit = (TbSymbolUnitEntry_t){
        .offset           = 0,
        .extent           = {
            .payload_bytes = 512 - initial,
            .total_bytes   = 512,
            .end_offset    = 512,
            .length_bytes  = initial,
            .offset_bytes  = format
        },
        .header           = {
            .kind          = version == 5 ? TB_SYMBOL_DWARF_COMPILE : TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL,
            .version       = (uint16_t)version,
            .unit_type     = version == 5 ? 1U : 0U,
            .address_bytes = 8,
            .header_bytes  = header_bytes
        },
        .header_available = true
    };
    scope->unit.header.extent = scope->unit.extent;
    uint64_t cursor = header_bytes;
    image[1024 + cursor++] = 1;
    for (unsigned i = 0; i < bases; ++i) {
        put(1024 + cursor, format, base);
        scope->attributes[i] = (TbSymbolDieAttribute_t){
            .name          = 0x72,
            .declared_form = TB_SYMBOL_FORM_SEC_OFFSET,
            .offset        = cursor,
            .value         = {
                .form           = TB_SYMBOL_FORM_SEC_OFFSET,
                .bytes          = format,
                .payload_offset = cursor,
                .payload_bytes  = format,
                .bits           = base,
                .numeric        = true
            }
        };
        cursor += format;
    }
    scope->entries[0] = (TbSymbolDieEntry_t){
        .offset          = header_bytes,
        .end_offset      = cursor,
        .parent          = UINT64_MAX,
        .tag             = 0x11,
        .first_attribute = 0,
        .attribute_count = bases,
        .children        = true
    };
    uint64_t child = cursor;
    image[1024 + cursor++] = 2;
    for (unsigned i = 0; i < fields; ++i) {
        uint64_t start = cursor;
        uint64_t actual = field_form == TB_SYMBOL_FORM_INDIRECT ? TB_SYMBOL_FORM_STRP : field_form;
        if (field_form == TB_SYMBOL_FORM_INDIRECT)
            image[1024 + cursor++] = (unsigned char)actual;
        uint64_t payload = cursor;
        if (actual == TB_SYMBOL_FORM_STRING) {
            size_t bytes = empty ? 1 : 6;
            memcpy(image + 1024 + cursor, empty ? "" : "hello", bytes);
            cursor += bytes;
        } else if (actual == TB_SYMBOL_FORM_STRX) {
            uint64_t remaining = value;
            do {
                unsigned byte = (unsigned)(remaining & 0x7fU);
                remaining >>= 7;
                image[1024 + cursor++] = (unsigned char)(byte | (remaining ? 0x80U : 0));
            } while (remaining);
        } else {
            unsigned bytes = actual >= TB_SYMBOL_FORM_STRX1 && actual <= TB_SYMBOL_FORM_STRX4
                                 ? (unsigned)(actual - TB_SYMBOL_FORM_STRX1 + 1)
                                 : actual == TB_SYMBOL_FORM_DATA4 ? 4 : format;
            put(1024 + cursor, bytes, value);
            cursor += bytes;
        }
        scope->attributes[bases + i] = (TbSymbolDieAttribute_t){
            .schema_attribute = bases + i,
            .name             = 3,
            .declared_form    = field_form,
            .offset           = start,
            .value            = {
                .form           = actual,
                .bytes          = cursor - start,
                .payload_offset = payload,
                .payload_bytes  = cursor - payload - (actual == TB_SYMBOL_FORM_STRING ? 1U : 0U),
                .bits           = value,
                .numeric        = actual != TB_SYMBOL_FORM_STRING,
                .indirect       = field_form == TB_SYMBOL_FORM_INDIRECT
            }
        };
    }
    scope->entries[1] = (TbSymbolDieEntry_t){
        .offset          = child,
        .end_offset      = cursor,
        .parent          = 0,
        .tag             = 0x34,
        .first_attribute = bases,
        .attribute_count = fields
    };
    scope->units = (TbSymbolUnitsView_t){
        .entries       = &scope->unit,
        .count         = 1,
        .section_index = 1,
        .kind          = TB_SYMBOL_DWARF_INFO
    };
    scope->tree = (TbSymbolDiesView_t){
        .entries         = scope->entries,
        .attributes      = scope->attributes,
        .count           = 2,
        .attribute_count = bases + fields,
        .max_depth       = 1,
        .section_index   = 1,
        .unit_offset     = 0,
        .kind            = TB_SYMBOL_DWARF_COMPILE
    };
    scope->selection = (TbSymbolTextSelection_t){
        .unit_index          = 0,
        .die_index           = 1,
        .name                = 3,
        .string_section      = 2,
        .line_string_section = 4,
        .offset_section      = 3
    };
}


static void open_scope(Scope_t *scope)
{
    CHECK(tb_symbol_budget_init(&scope->budget, 2 * 1024 * 1024));
    strcpy(scope->path, "/tmp/tb-symbol-text-XXXXXX");
    int writer = mkstemp(scope->path);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(!fchmod(writer, 0400) && !close(writer));
    scope->descriptor = open(scope->path, O_RDONLY | O_CLOEXEC);
    CHECK(scope->descriptor >= 0);
    scope->selection.descriptor = scope->descriptor;
    struct stat status;
    CHECK(!fstat(scope->descriptor, &status));
    scope->owner = tb_symbol_sections_create((TbSymbolStorageInfo_t){
        .key   = {
            .device = (uint64_t)status.st_dev,
            .inode  = (uint64_t)status.st_ino
        },
        .bytes = sizeof(image)
    }, scope->descriptor, &scope->budget);
    CHECK(scope->owner);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 10000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_sections_step(scope->owner, 64);
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(scope->owner, &scope->directory));
    scope->units.backing = scope->directory.backing;
    scope->tree.backing = scope->directory.backing;
    scope->baseline = scope->budget.used;
}


static void close_scope(Scope_t *scope)
{
    CHECK(scope->budget.used == scope->baseline && fcntl(scope->descriptor, F_GETFD) >= 0);
    tb_symbol_sections_destroy(scope->owner);
    CHECK(!scope->budget.used && !close(scope->descriptor) && !unlink(scope->path));
}


static TbSymbolText_t *create(Scope_t *scope)
{
    reads = 0;
    TbSymbolText_t *text = tb_symbol_text_create(scope->directory, scope->units, scope->tree,
                                                scope->selection, &scope->budget);
    CHECK(!reads);
    return text;
}


static void unavailable(TbSymbolText_t *text)
{
    TbSymbolTextView_t view;
    unsigned char before[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(before, &view, sizeof(view));
    CHECK(!tb_symbol_text_view(text, &view) && !memcmp(before, &view, sizeof(view)));
}


static TbSymbolTextOutcome_e step(TbSymbolText_t *text)
{
    reads = 0;
    inspected = 0;
    TbSymbolTextOutcome_e outcome = tb_symbol_text_step(text, 64);
    CHECK(reads <= 1 && inspected <= 64);
    if (outcome != TB_SYMBOL_TEXT_FOUND)
        unavailable(text);
    return outcome;
}


static TbSymbolTextOutcome_e complete(TbSymbolText_t *text)
{
    TbSymbolTextOutcome_e outcome = TB_SYMBOL_TEXT_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_TEXT_PENDING; ++i)
        outcome = step(text);
    CHECK(outcome != TB_SYMBOL_TEXT_PENDING);
    return outcome;
}


static void check(Scope_t *scope, TbSymbolTextOutcome_e expected, TbSymbolTextFailure_e reason,
                  uint64_t target_section, uint64_t target_offset, uint64_t bytes)
{
    open_scope(scope);
    TbSymbolText_t *text = create(scope);
    CHECK(text);
    unavailable(text);
    TbSymbolTextOutcome_e outcome = complete(text);
    if (outcome != expected || tb_symbol_text_failure(text).reason != reason)
        fprintf(stderr, "text expected %u/%u got %u/%u\n", expected, reason, outcome,
                tb_symbol_text_failure(text).reason);
    CHECK(outcome == expected && tb_symbol_text_failure(text).reason == reason);
    if (expected == TB_SYMBOL_TEXT_FOUND) {
        TbSymbolTextView_t view;
        CHECK(tb_symbol_text_view(text, &view));
        CHECK(view.source_section == 1 && !view.source_unit &&
              view.source_record == scope->entries[1].offset);
        CHECK(view.attribute.offset == scope->attributes[scope->entries[1].first_attribute].offset);
        CHECK(view.attribute.name == 3 && view.text.section_index == target_section);
        CHECK(view.text.offset == target_offset && view.text.bytes == bytes);
        CHECK(view.backing.key.inode == scope->directory.backing.key.inode);
        if (view.indexed) {
            CHECK(view.base_attribute.offset == scope->attributes[0].offset);
            CHECK(view.offsets.index == view.attribute.value.bits);
            CHECK(view.offsets.string_offset == view.text.offset);
        }
    }
    tb_symbol_text_destroy(text);
    close_scope(scope);
}


static void layouts(void)
{
    Scope_t scope;
    prepare(&scope, TB_SYMBOL_FORM_STRING, 0, 1, 1, false);
    uint64_t payload = scope.attributes[1].value.payload_offset;
    check(&scope, TB_SYMBOL_TEXT_FOUND, TB_SYMBOL_TEXT_NONE, 1, payload, 5);
    prepare(&scope, TB_SYMBOL_FORM_STRING, 0, 1, 1, true);
    payload = scope.attributes[1].value.payload_offset;
    check(&scope, TB_SYMBOL_TEXT_FOUND, TB_SYMBOL_TEXT_NONE, 1, payload, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRP, 0, 1, 1, false);
    check(&scope, TB_SYMBOL_TEXT_FOUND, TB_SYMBOL_TEXT_NONE, 2, 0, 5);
    prepare(&scope, TB_SYMBOL_FORM_STRP, 6, 1, 1, false);
    check(&scope, TB_SYMBOL_TEXT_FOUND, TB_SYMBOL_TEXT_NONE, 2, 6, 0);
    prepare(&scope, TB_SYMBOL_FORM_INDIRECT, 7, 1, 1, false);
    check(&scope, TB_SYMBOL_TEXT_FOUND, TB_SYMBOL_TEXT_NONE, 2, 7, 5);
    if (version != 5)
        return;
    prepare(&scope, TB_SYMBOL_FORM_LINE_STRP, 0, 1, 1, false);
    check(&scope, TB_SYMBOL_TEXT_FOUND, TB_SYMBOL_TEXT_NONE, 4, 0, 4);
    const uint64_t indexed[] = {TB_SYMBOL_FORM_STRX, TB_SYMBOL_FORM_STRX1, TB_SYMBOL_FORM_STRX2,
                                TB_SYMBOL_FORM_STRX3, TB_SYMBOL_FORM_STRX4};
    for (size_t i = 0; i < sizeof(indexed) / sizeof(indexed[0]); ++i) {
        prepare(&scope, indexed[i], 1, 1, 1, false);
        check(&scope, TB_SYMBOL_TEXT_FOUND, TB_SYMBOL_TEXT_NONE, 2, 7, 5);
    }
}


static void unavailable_cases(void)
{
    Scope_t scope;
    prepare(&scope, TB_SYMBOL_FORM_STRP, 0, 1, 0, false);
    check(&scope, TB_SYMBOL_TEXT_ABSENT, TB_SYMBOL_TEXT_FIELD_ABSENT, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRP, 0, 1, 2, false);
    check(&scope, TB_SYMBOL_TEXT_CONFLICT, TB_SYMBOL_TEXT_FIELD_AMBIGUOUS, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 0, 1, false);
    check(&scope, TB_SYMBOL_TEXT_DEPENDENCY, TB_SYMBOL_TEXT_BASE_ABSENT, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 2, 1, false);
    check(&scope, TB_SYMBOL_TEXT_CONFLICT, TB_SYMBOL_TEXT_BASE_AMBIGUOUS, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 1, 1, false);
    scope.attributes[0].value.form = TB_SYMBOL_FORM_DATA4;
    check(&scope, TB_SYMBOL_TEXT_UNSUPPORTED, TB_SYMBOL_TEXT_FORM, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 1, 1, false);
    scope.attributes[0].value.is_signed = true;
    check(&scope, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_VALUE, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 1, 1, false);
    put(4096 + 8, format, 13);
    check(&scope, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_STRING, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 1, 1, false);
    scope.selection.string_section = UINT64_MAX;
    check(&scope, TB_SYMBOL_TEXT_DEPENDENCY, TB_SYMBOL_TEXT_STRING_SECTION, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 1, 1, false);
    scope.selection.offset_section = UINT64_MAX;
    check(&scope, TB_SYMBOL_TEXT_DEPENDENCY, TB_SYMBOL_TEXT_OFFSET_SECTION, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRP, 0, 1, 1, false);
    scope.selection.string_section = UINT64_MAX;
    check(&scope, TB_SYMBOL_TEXT_DEPENDENCY, TB_SYMBOL_TEXT_STRING_SECTION, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_LINE_STRP, 0, 1, 1, false);
    scope.selection.line_string_section = UINT64_MAX;
    check(&scope, TB_SYMBOL_TEXT_DEPENDENCY, TB_SYMBOL_TEXT_LINE_STRING_SECTION, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_DATA4, 0, 1, 1, false);
    check(&scope, TB_SYMBOL_TEXT_UNSUPPORTED, TB_SYMBOL_TEXT_FORM, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRP_SUP, 0, 1, 1, false);
    check(&scope, TB_SYMBOL_TEXT_UNSUPPORTED, TB_SYMBOL_TEXT_FORM, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 1, 1, false);
    scope.attributes[1].value.numeric = false;
    check(&scope, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_VALUE, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRING, 0, 1, 1, false);
    ++scope.attributes[1].value.bytes;
    check(&scope, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_SPAN, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRX, UINT64_MAX, 1, 1, false);
    check(&scope, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_OFFSETS, 0, 0, 0);
    prepare(&scope, TB_SYMBOL_FORM_STRP, 0, 1, 1, false);
    image[2048 + 5] = 'x';
    section(2, 2048, 6);
    check(&scope, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_STRING, 0, 0, 0);
    version = 4;
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 0, 1, 1, false);
    check(&scope, TB_SYMBOL_TEXT_UNSUPPORTED, TB_SYMBOL_TEXT_VERSION, 0, 0, 0);
    version = 5;
}


static void failures(void)
{
    Scope_t scope;
    prepare(&scope, TB_SYMBOL_FORM_STRX1, 1, 1, 1, false);
    open_scope(&scope);
    for (fault = 1; fault <= 5; ++fault) {
        allocations = 0;
        TbSymbolText_t *text = create(&scope);
        if (text) {
            CHECK(complete(text) == TB_SYMBOL_TEXT_RESOURCE);
            tb_symbol_text_destroy(text);
        }
        CHECK(allocations >= fault && scope.budget.used == scope.baseline);
    }
    fault = 0;
    for (unsigned stop = 0; stop < 128; ++stop) {
        TbSymbolText_t *text = create(&scope);
        CHECK(text);
        for (unsigned i = 0; i < stop; ++i)
            (void)step(text);
        tb_symbol_text_destroy(text);
        CHECK(scope.budget.used == scope.baseline);
    }
    TbSymbolText_t *limited = create(&scope);
    CHECK(limited);
    size_t old_limit = scope.budget.limit;
    scope.budget.limit = scope.budget.used;
    CHECK(complete(limited) == TB_SYMBOL_TEXT_RESOURCE);
    tb_symbol_text_destroy(limited);
    scope.budget.limit = old_limit;
    CHECK(scope.budget.used == scope.baseline);
    for (unsigned mode = 1; mode <= 4; ++mode) {
        TbSymbolText_t *text = create(&scope);
        CHECK(text);
        read_mode = mode;
        if (mode == 2) {
            uint64_t progress = 0;
            bool stalled = false;
            for (unsigned i = 0; i < 64; ++i) {
                CHECK(step(text) == TB_SYMBOL_TEXT_PENDING);
                uint64_t next = tb_symbol_text_progress(text);
                if (reads && next == progress)
                    stalled = true;
                progress = next;
            }
            CHECK(stalled);
            read_mode = 0;
            CHECK(complete(text) == TB_SYMBOL_TEXT_FOUND);
        } else {
            CHECK(complete(text) == (mode == 1 ? TB_SYMBOL_TEXT_FOUND : TB_SYMBOL_TEXT_OPERATIONAL));
        }
        read_mode = 0;
        tb_symbol_text_destroy(text);
    }
    TbSymbolUnitsView_t wrong = scope.units;
    ++wrong.backing.key.inode;
    CHECK(!tb_symbol_text_create(scope.directory, wrong, scope.tree, scope.selection, &scope.budget));
    CHECK(scope.budget.used == scope.baseline);
    int replacement = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(replacement >= 0);
    scope.selection.descriptor = replacement;
    TbSymbolText_t *text = create(&scope);
    CHECK(text && complete(text) == TB_SYMBOL_TEXT_OPERATIONAL);
    tb_symbol_text_destroy(text);
    CHECK(!close(replacement));
    scope.selection.descriptor = scope.descriptor;
    close_scope(&scope);
}


int main(void)
{
    for (width = 4; width <= 8; width += 4) {
        for (format = 4; format <= 8; format += 4) {
            for (unsigned byte_order = 0; byte_order < 2; ++byte_order) {
                order = (TbSymbolByteOrder_e)byte_order;
                for (version = 4; version <= 5; ++version)
                    layouts();
            }
        }
    }
    width = 8;
    format = 4;
    order = TB_SYMBOL_LITTLE_ENDIAN;
    version = 5;
    unavailable_cases();
    failures();
    puts("PASS atomic text attributes, scoped string dependencies and construction ownership");
    return 0;
}
