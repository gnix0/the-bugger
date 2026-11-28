#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected descriptor fixtures. */
#include "symbol/inherited_text.h"
#include "../support/check.h"

#include <elf.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RECORDS 5
#define FIELDS 6

static unsigned char image[16384];
static unsigned fault;
static unsigned allocations;

void *__real_calloc(size_t count, size_t size);
void *__real_malloc(size_t bytes);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_malloc(size_t bytes);

void *__wrap_calloc(size_t count, size_t size)
{
    if (fault && ++allocations == fault)
        return NULL;
    return __real_calloc(count, size);
}


void *__wrap_malloc(size_t bytes)
{
    if (fault && ++allocations == fault)
        return NULL;
    return __real_malloc(bytes);
}


typedef struct {
    TbSymbolBudget_t                 budget;
    TbSymbolSections_t             *sections;
    TbSymbolSectionsView_t          directory;
    TbSymbolUnitEntry_t             headers[2];
    TbSymbolDieEntry_t              remote_entries[2];
    TbSymbolDieAttribute_t          remote_attributes[2];
    TbSymbolDiesView_t              remote_tree;
    bool                           cross_unit;
    TbSymbolUnitsView_t             units;
    TbSymbolDieEntry_t              entries[RECORDS];
    TbSymbolDieAttribute_t          fields[RECORDS][FIELDS];
    TbSymbolDieAttribute_t          attributes[RECORDS * FIELDS];
    TbSymbolDiesView_t              tree;
    TbSymbolGraphNode_t             nodes[8];
    TbSymbolGraph_t                 graph;
    TbSymbolGraphView_t             view;
    TbSymbolInheritedTextSelection_t selection;
    TbSymbolByteOrder_e             order;
    unsigned                       format;
    unsigned                       version;
    unsigned                       records;
    int                            descriptor;
    char                           path[64];
    size_t                         baseline;
} Scope_t;

static void put(Scope_t *scope, size_t at, unsigned bytes, uint64_t value)
{
    CHECK(bytes <= 8 && at <= sizeof(image) && bytes <= sizeof(image) - at);
    for (unsigned i = 0; i < bytes; ++i) {
        unsigned index = scope->order == TB_SYMBOL_LITTLE_ENDIAN ? i : bytes - i - 1;
        image[at + index] = (unsigned char)(value >> (i * 8));
    }
}


static void section(Scope_t *scope, unsigned index, uint64_t offset, uint64_t bytes)
{
    size_t at = 128 + (size_t)index * 64;
    put(scope, at + 4, 4, SHT_PROGBITS);
    put(scope, at + 24, 8, offset);
    put(scope, at + 32, 8, bytes);
    put(scope, at + 48, 8, 1);
}


static void field(Scope_t *scope, unsigned record, uint64_t name, uint64_t form, uint64_t bits)
{
    CHECK(record < scope->records && scope->entries[record].attribute_count < FIELDS);
    TbSymbolDieEntry_t *entry = &scope->entries[record];
    uint64_t index = entry->attribute_count++;
    uint64_t start = entry->end_offset;
    uint64_t actual = form == TB_SYMBOL_FORM_INDIRECT ? TB_SYMBOL_FORM_STRP : form;
    uint64_t payload = start;
    if (form == TB_SYMBOL_FORM_INDIRECT)
        image[1024 + payload++] = (unsigned char)actual;
    unsigned bytes = actual == TB_SYMBOL_FORM_STRING ? 6
                         : actual == TB_SYMBOL_FORM_STRX1 ? 1
                             : actual == TB_SYMBOL_FORM_REF4 || actual == TB_SYMBOL_FORM_DATA4 ? 4
                                 : scope->format;
    if (actual == TB_SYMBOL_FORM_STRING)
        memcpy(image + 1024 + payload, "alpha", 6);
    else
        put(scope, 1024 + (size_t)payload, bytes, bits);
    entry->end_offset = payload + bytes;
    scope->fields[record][index] = (TbSymbolDieAttribute_t){
        .schema_attribute = index,
        .name             = name,
        .declared_form    = form,
        .offset           = start,
        .value            = {
            .form           = actual,
            .bytes          = entry->end_offset - start,
            .payload_offset = payload,
            .payload_bytes  = actual == TB_SYMBOL_FORM_STRING ? 5 : bytes,
            .bits           = actual == TB_SYMBOL_FORM_STRING ? 0 : bits,
            .numeric        = actual != TB_SYMBOL_FORM_STRING,
            .indirect       = form == TB_SYMBOL_FORM_INDIRECT
        }
    };
}


static void link_records(Scope_t *scope, unsigned from, unsigned to, TbSymbolRelationKind_e kind)
{
    field(scope, from, kind == TB_SYMBOL_RELATION_SPECIFICATION ? 0x47
                           : kind == TB_SYMBOL_RELATION_ORIGIN ? 0x31 : 0x54,
          TB_SYMBOL_FORM_REF4, scope->entries[to].offset);
}


static void prepare(Scope_t *scope, unsigned format, unsigned version, TbSymbolByteOrder_e order,
                     unsigned records)
{
    CHECK((format == 4 || format == 8) && (version == 4 || version == 5));
    CHECK(records >= 2 && records <= RECORDS);
    *scope = (Scope_t){
        .order      = order,
        .format     = format,
        .version    = version,
        .records    = records,
        .descriptor = -1
    };
    memset(image, 0, sizeof(image));
    memcpy(image, ELFMAG, SELFMAG);
    image[EI_CLASS] = ELFCLASS64;
    image[EI_DATA] = order == TB_SYMBOL_LITTLE_ENDIAN ? ELFDATA2LSB : ELFDATA2MSB;
    image[EI_VERSION] = EV_CURRENT;
    put(scope, 16, 2, ET_EXEC);
    put(scope, 18, 2, EM_RISCV);
    put(scope, 20, 4, EV_CURRENT);
    put(scope, 40, 8, 128);
    put(scope, 52, 2, 64);
    put(scope, 58, 2, 64);
    put(scope, 60, 2, 5);
    unsigned initial = format == 4 ? 4 : 12;
    unsigned header = initial + (version == 5 ? 4U : 3U) + format;
    uint64_t base = initial + 4;
    section(scope, 1, 1024, 512);
    section(scope, 2, 4096, 18);
    section(scope, 3, 8192, initial + 4 + 2 * format);
    section(scope, 4, 12288, 6);
    memcpy(image + 4096, "alpha\0beta\0alpha\0\0", 18);
    memcpy(image + 12288, "alpha", 6);
    if (format == 8) {
        put(scope, 1024, 4, UINT32_MAX);
        put(scope, 8192, 4, UINT32_MAX);
    }
    put(scope, 1024 + (format == 4 ? 0U : 4U), format, 512 - initial);
    put(scope, 1024 + initial, 2, version);
    if (version == 5) {
        put(scope, 1024 + initial + 2, 1, 1);
        put(scope, 1024 + initial + 3, 1, 8);
    } else {
        put(scope, 1024 + initial + 2 + format, 1, 8);
    }
    put(scope, 8192 + (format == 4 ? 0U : 4U), format, 4 + 2 * format);
    put(scope, 8192 + initial, 2, 5);
    put(scope, 8192 + base, format, 0);
    put(scope, 8192 + base + format, format, 11);
    scope->headers[0] = (TbSymbolUnitEntry_t){
        .extent           = {
            .payload_bytes = 512 - initial,
            .total_bytes   = 512,
            .end_offset    = 512,
            .length_bytes  = initial,
            .offset_bytes  = format
        },
        .header           = {
            .kind          = version == 4 ? TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL : TB_SYMBOL_DWARF_COMPILE,
            .version       = (uint16_t)version,
            .unit_type     = version == 5 ? 1 : 0,
            .address_bytes = 8,
            .header_bytes  = header
        },
        .header_available = true
    };
    scope->headers[0].header.extent = scope->headers[0].extent;
    for (unsigned i = 0; i < records; ++i) {
        uint64_t offset = i ? i * 64U : header;
        image[1024 + offset] = (unsigned char)(i + 1);
        scope->entries[i] = (TbSymbolDieEntry_t){
            .offset     = offset,
            .end_offset = offset + 1,
            .parent     = i ? 0 : UINT64_MAX,
            .code       = i + 1,
            .tag        = i ? 0x2e : 0x11,
            .children   = !i
        };
    }
    field(scope, 0, 0x72, TB_SYMBOL_FORM_SEC_OFFSET, base);
}


static void open_scope(Scope_t *scope)
{
    CHECK(tb_symbol_budget_init(&scope->budget, 2 * 1024 * 1024));
    strcpy(scope->path, "/tmp/tb-symbol-inherited-text-XXXXXX");
    int writer = mkstemp(scope->path);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(!fchmod(writer, 0400) && !close(writer));
    scope->descriptor = open(scope->path, O_RDONLY | O_CLOEXEC);
    CHECK(scope->descriptor >= 0);
    struct stat status;
    CHECK(!fstat(scope->descriptor, &status));
    TbSymbolStorageInfo_t backing = {
        .key   = {
            .device = (uint64_t)status.st_dev,
            .inode  = (uint64_t)status.st_ino
        },
        .bytes = sizeof(image)
    };
    scope->sections = tb_symbol_sections_create(backing, scope->descriptor, &scope->budget);
    CHECK(scope->sections);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 10000 && outcome == TB_SYMBOL_PENDING; ++turn)
        outcome = tb_symbol_sections_step(scope->sections, 257);
    CHECK(outcome == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(scope->sections, &scope->directory));
    uint64_t count = 0;
    for (unsigned i = 0; i < scope->records; ++i) {
        scope->entries[i].first_attribute = count;
        for (uint64_t j = 0; j < scope->entries[i].attribute_count; ++j)
            scope->attributes[count++] = scope->fields[i][j];
    }
    scope->units = (TbSymbolUnitsView_t){
        .entries       = scope->headers,
        .count         = scope->cross_unit ? 2 : 1,
        .section_index = 1,
        .kind          = TB_SYMBOL_DWARF_INFO,
        .backing       = backing
    };
    scope->tree = (TbSymbolDiesView_t){
        .entries         = scope->entries,
        .attributes      = scope->attributes,
        .count           = scope->records,
        .attribute_count = count,
        .section_index   = 1,
        .abbrev_section  = 0,
        .kind            = scope->entries[0].tag == 0x3c ? TB_SYMBOL_DWARF_PARTIAL
                                                        : TB_SYMBOL_DWARF_COMPILE,
        .backing         = backing
    };
    if (scope->cross_unit) {
        scope->remote_tree = (TbSymbolDiesView_t){
            .entries         = scope->remote_entries,
            .attributes      = scope->remote_attributes,
            .count           = 2,
            .attribute_count = 2,
            .section_index   = 1,
            .unit_offset     = 512,
            .abbrev_section  = 0,
            .kind            = TB_SYMBOL_DWARF_COMPILE,
            .backing         = backing
        };
    }
    CHECK(tb_symbol_budget_reserve(&scope->budget, sizeof(scope->nodes)));
    CHECK(tb_symbol_graph_init(&scope->graph, (TbSymbolReferenceTarget_t){
        .backing       = backing,
        .section_index = 1,
        .record_offset = scope->entries[1].offset,
        .entry_index   = 1
    }, scope->nodes, 8) == TB_SYMBOL_GRAPH_SOURCE);
    for (unsigned turn = 0; turn < 10000 && scope->graph.status != TB_SYMBOL_GRAPH_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&scope->graph, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE) {
                bool remote = need.record.unit_offset == 512;
                CHECK(tb_symbol_graph_source(&scope->graph, &scope->units,
                                              remote ? &scope->remote_tree : &scope->tree, remote ? 1 : 0) ==
                      TB_SYMBOL_GRAPH_PENDING);
            } else {
                CHECK(tb_symbol_graph_target(&scope->graph, &scope->units,
                                              scope->cross_unit ? &scope->remote_tree : &scope->tree) ==
                      TB_SYMBOL_GRAPH_PENDING);
            }
        }
        (void)tb_symbol_graph_step(&scope->graph, 7);
    }
    CHECK(tb_symbol_graph_view(&scope->graph, &scope->view));
    scope->selection = (TbSymbolInheritedTextSelection_t){
        .name                = 0x03,
        .string_section      = 2,
        .line_string_section = 4,
        .offset_section      = 3,
        .descriptor          = scope->descriptor
    };
    scope->baseline = scope->budget.used;
}


static void close_scope(Scope_t *scope)
{
    CHECK(scope->budget.used == scope->baseline);
    CHECK(tb_symbol_budget_release(&scope->budget, sizeof(scope->nodes)));
    tb_symbol_sections_destroy(scope->sections);
    CHECK(!scope->budget.used);
    CHECK(!close(scope->descriptor) && !unlink(scope->path));
}


static TbSymbolInheritedText_t *create(Scope_t *scope)
{
    return tb_symbol_inherited_text_create(scope->directory, scope->view, scope->selection, &scope->budget);
}


static TbSymbolInheritedTextStatus_e run(Scope_t *scope, TbSymbolInheritedText_t *text, size_t slice)
{
    for (unsigned turn = 0; turn < 100000; ++turn) {
        TbSymbolInheritedTextView_t incomplete;
        CHECK(!tb_symbol_inherited_text_view(text, &incomplete));
        TbSymbolInheritedTextNeed_t need;
        if (tb_symbol_inherited_text_need(text, &need)) {
            CHECK(need.source.record.section_index == scope->tree.section_index);
            bool remote = need.source.record.unit_offset == 512;
            TbSymbolInheritedTextStatus_e status = tb_symbol_inherited_text_source(
                text, &scope->units, remote ? &scope->remote_tree : &scope->tree, remote ? 1 : 0);
            if (status != TB_SYMBOL_INHERITED_TEXT_PENDING)
                return status;
        }
        TbSymbolInheritedTextStatus_e status = tb_symbol_inherited_text_step(text, slice);
        if (status != TB_SYMBOL_INHERITED_TEXT_PENDING && status != TB_SYMBOL_INHERITED_TEXT_SOURCE)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_INHERITED_TEXT_INVALID;
}


static void check_equal_values(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        for (unsigned format = 4; format <= 8; format += 4) {
            for (unsigned order = TB_SYMBOL_LITTLE_ENDIAN; order <= TB_SYMBOL_BIG_ENDIAN; ++order) {
                Scope_t scope;
                prepare(&scope, format, version, (TbSymbolByteOrder_e)order, 4);
                link_records(&scope, 1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
                link_records(&scope, 1, 3, TB_SYMBOL_RELATION_ORIGIN);
                field(&scope, 2, 0x03, TB_SYMBOL_FORM_STRP, 0);
                field(&scope, 3, 0x03, version == 5 ? TB_SYMBOL_FORM_STRX1 : TB_SYMBOL_FORM_STRING, 1);
                open_scope(&scope);
                TbSymbolInheritedText_t *text = create(&scope);
                CHECK(text && run(&scope, text, 64) == TB_SYMBOL_INHERITED_TEXT_FOUND);
                TbSymbolInheritedTextView_t view;
                CHECK(tb_symbol_inherited_text_view(text, &view));
                CHECK(view.selected.record_offset == scope.entries[1].offset && view.count == 2);
                CHECK(view.facts[0].node_index != view.facts[1].node_index);
                CHECK(view.facts[0].text.source_record == scope.entries[2].offset);
                CHECK(view.facts[1].text.source_record == scope.entries[3].offset);
                for (size_t i = 0; i < view.count; ++i) {
                    CHECK(view.facts[i].text.text.bytes == 5);
                    CHECK(view.facts[i].unit.header.extent.offset_bytes == format);
                    CHECK(view.facts[i].unit_root.record_offset == scope.entries[0].offset);
                    CHECK(view.facts[i].root_attribute_count == 1);
                }
                CHECK(view.facts[1].text.indexed == (version == 5));
                CHECK(tb_symbol_inherited_text_progress(text));
                tb_symbol_inherited_text_destroy(text);
                close_scope(&scope);
            }
        }
    }
}


static void check_disagreement_and_absence(void)
{
    Scope_t scope;
    prepare(&scope, 4, 5, TB_SYMBOL_LITTLE_ENDIAN, 4);
    link_records(&scope, 1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    link_records(&scope, 1, 3, TB_SYMBOL_RELATION_ORIGIN);
    field(&scope, 2, 0x03, TB_SYMBOL_FORM_STRP, 0);
    field(&scope, 3, 0x03, TB_SYMBOL_FORM_STRP, 6);
    open_scope(&scope);
    TbSymbolInheritedText_t *text = create(&scope);
    CHECK(text && run(&scope, text, 257) == TB_SYMBOL_INHERITED_TEXT_CONFLICT);
    TbSymbolInheritedTextFailure_t failure = tb_symbol_inherited_text_failure(text);
    CHECK(failure.reason == TB_SYMBOL_INHERITED_TEXT_DISAGREEMENT);
    CHECK(failure.node_index != failure.other_node_index);
    TbSymbolInheritedTextView_t view;
    CHECK(!tb_symbol_inherited_text_view(text, &view));
    tb_symbol_inherited_text_destroy(text);
    scope.selection.name = 0x6e;
    text = create(&scope);
    CHECK(text && run(&scope, text, 257) == TB_SYMBOL_INHERITED_TEXT_ABSENT);
    CHECK(!tb_symbol_inherited_text_view(text, &view));
    tb_symbol_inherited_text_destroy(text);
    close_scope(&scope);

    prepare(&scope, 4, 5, TB_SYMBOL_LITTLE_ENDIAN, 2);
    field(&scope, 1, 0x03, TB_SYMBOL_FORM_STRP, 17);
    open_scope(&scope);
    text = create(&scope);
    CHECK(text && run(&scope, text, 257) == TB_SYMBOL_INHERITED_TEXT_FOUND);
    CHECK(tb_symbol_inherited_text_view(text, &view) && view.count == 1 && !view.facts[0].text.text.bytes);
    tb_symbol_inherited_text_destroy(text);
    close_scope(&scope);
}


static void check_cross_unit(void)
{
    for (unsigned order = TB_SYMBOL_LITTLE_ENDIAN; order <= TB_SYMBOL_BIG_ENDIAN; ++order) {
        Scope_t scope;
        prepare(&scope, 4, 5, (TbSymbolByteOrder_e)order, 2);
        scope.cross_unit = true;
        section(&scope, 1, 1024, 1024);
        section(&scope, 3, 8192, 48);
        field(&scope, 1, 0x47, TB_SYMBOL_FORM_REF_ADDR, 576);
        put(&scope, 1536, 4, UINT32_MAX);
        put(&scope, 1540, 8, 500);
        put(&scope, 1548, 2, 5);
        put(&scope, 1550, 1, 1);
        put(&scope, 1551, 1, 8);
        put(&scope, 1560, 1, 1);
        put(&scope, 1561, 8, 32);
        put(&scope, 1600, 1, 2);
        put(&scope, 1601, 1, 0);
        put(&scope, 8208, 4, UINT32_MAX);
        put(&scope, 8212, 8, 20);
        put(&scope, 8220, 2, 5);
        put(&scope, 8224, 8, 6);
        put(&scope, 8232, 8, 11);
        scope.headers[1] = scope.headers[0];
        scope.headers[1].offset = 512;
        scope.headers[1].extent = (TbSymbolDwarfLength_t){
            .payload_bytes = 500,
            .total_bytes   = 512,
            .end_offset    = 1024,
            .length_bytes  = 12,
            .offset_bytes  = 8
        };
        scope.headers[1].header.extent = scope.headers[1].extent;
        scope.headers[1].header.header_bytes = 24;
        scope.remote_entries[0] = (TbSymbolDieEntry_t){
            .offset          = 536,
            .end_offset      = 545,
            .parent          = UINT64_MAX,
            .code            = 1,
            .tag             = 0x11,
            .attribute_count = 1,
            .children        = true
        };
        scope.remote_entries[1] = (TbSymbolDieEntry_t){
            .offset          = 576,
            .end_offset      = 578,
            .parent          = 0,
            .code            = 2,
            .tag             = 0x2e,
            .first_attribute = 1,
            .attribute_count = 1
        };
        scope.remote_attributes[0] = (TbSymbolDieAttribute_t){
            .name          = 0x72,
            .declared_form = TB_SYMBOL_FORM_SEC_OFFSET,
            .offset        = 537,
            .value         = {
                .form           = TB_SYMBOL_FORM_SEC_OFFSET,
                .bytes          = 8,
                .payload_offset = 537,
                .payload_bytes  = 8,
                .bits           = 32,
                .numeric        = true
            }
        };
        scope.remote_attributes[1] = (TbSymbolDieAttribute_t){
            .name          = 0x03,
            .declared_form = TB_SYMBOL_FORM_STRX1,
            .offset        = 577,
            .value         = {
                .form           = TB_SYMBOL_FORM_STRX1,
                .bytes          = 1,
                .payload_offset = 577,
                .payload_bytes  = 1,
                .numeric        = true
            }
        };
        open_scope(&scope);
        TbSymbolInheritedText_t *text = create(&scope);
        CHECK(text && run(&scope, text, 64) == TB_SYMBOL_INHERITED_TEXT_FOUND);
        TbSymbolInheritedTextView_t view;
        CHECK(tb_symbol_inherited_text_view(text, &view) && view.count == 1);
        CHECK(view.selected.unit_offset == 0 && view.selected.record_offset == 64);
        CHECK(view.facts[0].unit.offset == 512 && view.facts[0].unit.header.extent.offset_bytes == 8);
        CHECK(view.facts[0].unit_root.record_offset == 536);
        CHECK(view.facts[0].text.source_unit == 512 && view.facts[0].text.source_record == 576);
        CHECK(view.facts[0].text.indexed && view.facts[0].text.text.offset == 6);
        CHECK(view.facts[0].text.text.bytes == 4);
        CHECK(view.facts[0].text.offsets.base == 32 && view.facts[0].text.offsets.offset_bytes == 8);
        CHECK(view.facts[0].text.offsets.contribution_offset == 16);
        CHECK(view.facts[0].selection.unit_offset == 512);
        tb_symbol_inherited_text_destroy(text);
        close_scope(&scope);
    }
}


static void check_interruption_and_dependencies(void)
{
    Scope_t scope;
    prepare(&scope, 4, 5, TB_SYMBOL_LITTLE_ENDIAN, 4);
    link_records(&scope, 1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    link_records(&scope, 1, 3, TB_SYMBOL_RELATION_ORIGIN);
    field(&scope, 2, 0x03, TB_SYMBOL_FORM_STRP, 0);
    field(&scope, 3, 0x03, TB_SYMBOL_FORM_STRX1, 1);
    open_scope(&scope);
    bool completed = false;
    for (unsigned stop = 0; stop < 1000 && !completed; ++stop) {
        TbSymbolInheritedText_t *text = create(&scope);
        CHECK(text);
        for (unsigned turn = 0; turn < stop; ++turn) {
            TbSymbolInheritedTextNeed_t need;
            if (tb_symbol_inherited_text_need(text, &need)) {
                TbSymbolDiesView_t wrong = scope.tree;
                ++wrong.abbrev_offset;
                CHECK(tb_symbol_inherited_text_source(text, &scope.units, &wrong, 0) ==
                      TB_SYMBOL_INHERITED_TEXT_INVALID);
                TbSymbolInheritedTextNeed_t unchanged;
                CHECK(tb_symbol_inherited_text_need(text, &unchanged));
                CHECK(unchanged.kind == need.kind && unchanged.source.node_index == need.source.node_index);
                CHECK(tb_symbol_inherited_text_source(text, &scope.units, &scope.tree, 0) ==
                      TB_SYMBOL_INHERITED_TEXT_PENDING);
            }
            uint64_t progress = tb_symbol_inherited_text_progress(text);
            CHECK(tb_symbol_inherited_text_step(text, 63) == TB_SYMBOL_INHERITED_TEXT_INVALID);
            CHECK(tb_symbol_inherited_text_step(text, 65537) == TB_SYMBOL_INHERITED_TEXT_INVALID);
            CHECK(tb_symbol_inherited_text_progress(text) == progress);
            TbSymbolInheritedTextStatus_e status = tb_symbol_inherited_text_step(text, 64);
            completed = status == TB_SYMBOL_INHERITED_TEXT_FOUND;
            CHECK(completed || status == TB_SYMBOL_INHERITED_TEXT_PENDING ||
                  status == TB_SYMBOL_INHERITED_TEXT_SOURCE);
            if (completed)
                break;
        }
        tb_symbol_inherited_text_destroy(text);
        CHECK(scope.budget.used == scope.baseline);
    }
    CHECK(completed);
    TbSymbolInheritedText_t *text = create(&scope);
    CHECK(text);
    unsigned values = 0;
    bool missing = false;
    for (unsigned turn = 0; turn < 1000 && !missing; ++turn) {
        TbSymbolInheritedTextNeed_t need;
        if (tb_symbol_inherited_text_need(text, &need)) {
            if (need.kind == TB_SYMBOL_INHERITED_TEXT_VALUE_SOURCE && ++values == 2) {
                CHECK(tb_symbol_inherited_text_source(text, NULL, NULL, 0) ==
                      TB_SYMBOL_INHERITED_TEXT_DEPENDENCY);
                missing = true;
                break;
            }
            CHECK(tb_symbol_inherited_text_source(text, &scope.units, &scope.tree, 0) ==
                  TB_SYMBOL_INHERITED_TEXT_PENDING);
        }
        TbSymbolInheritedTextStatus_e status = tb_symbol_inherited_text_step(text, 64);
        CHECK(status == TB_SYMBOL_INHERITED_TEXT_PENDING || status == TB_SYMBOL_INHERITED_TEXT_SOURCE);
    }
    CHECK(missing);
    TbSymbolInheritedTextView_t view;
    CHECK(!tb_symbol_inherited_text_view(text, &view));
    CHECK(tb_symbol_inherited_text_failure(text).reason == TB_SYMBOL_INHERITED_TEXT_VALUE);
    tb_symbol_inherited_text_destroy(text);
    close_scope(&scope);
}


static void check_failures(void)
{
    Scope_t scope;
    prepare(&scope, 4, 5, TB_SYMBOL_LITTLE_ENDIAN, 2);
    field(&scope, 1, 0x03, TB_SYMBOL_FORM_DATA4, 0);
    open_scope(&scope);
    TbSymbolInheritedText_t *text = create(&scope);
    CHECK(text && run(&scope, text, 257) == TB_SYMBOL_INHERITED_TEXT_MALFORMED);
    CHECK(tb_symbol_inherited_text_failure(text).reason == TB_SYMBOL_INHERITED_TEXT_FORM);
    tb_symbol_inherited_text_destroy(text);
    close_scope(&scope);

    prepare(&scope, 4, 5, TB_SYMBOL_LITTLE_ENDIAN, 4);
    link_records(&scope, 1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    link_records(&scope, 1, 3, TB_SYMBOL_RELATION_ORIGIN);
    field(&scope, 2, 0x03, TB_SYMBOL_FORM_STRP, 0);
    field(&scope, 3, 0x03, TB_SYMBOL_FORM_STRX1, 1);
    open_scope(&scope);
    bool success = false;
    for (unsigned failure = 1; failure < 64 && !success; ++failure) {
        fault = failure;
        allocations = 0;
        text = create(&scope);
        if (text) {
            TbSymbolInheritedTextStatus_e status = run(&scope, text, 257);
            success = status == TB_SYMBOL_INHERITED_TEXT_FOUND;
            CHECK(success || status == TB_SYMBOL_INHERITED_TEXT_RESOURCE);
            tb_symbol_inherited_text_destroy(text);
        }
        fault = 0;
        CHECK(scope.budget.used == scope.baseline);
    }
    CHECK(success);
    scope.selection.offset_section = UINT64_MAX;
    text = create(&scope);
    CHECK(text && run(&scope, text, 257) == TB_SYMBOL_INHERITED_TEXT_DEPENDENCY);
    TbSymbolInheritedTextView_t view;
    CHECK(!tb_symbol_inherited_text_view(text, &view));
    tb_symbol_inherited_text_destroy(text);
    close_scope(&scope);

    prepare(&scope, 4, 5, TB_SYMBOL_LITTLE_ENDIAN, 2);
    field(&scope, 1, 0x03, TB_SYMBOL_FORM_STRP, 0);
    field(&scope, 1, 0x03, TB_SYMBOL_FORM_STRP, 0);
    open_scope(&scope);
    text = create(&scope);
    CHECK(text && run(&scope, text, 257) == TB_SYMBOL_INHERITED_TEXT_CONFLICT);
    CHECK(tb_symbol_inherited_text_failure(text).reason == TB_SYMBOL_INHERITED_TEXT_OWNERS);
    CHECK(!tb_symbol_inherited_text_view(text, &view));
    tb_symbol_inherited_text_destroy(text);
    close_scope(&scope);

    prepare(&scope, 4, 5, TB_SYMBOL_LITTLE_ENDIAN, 2);
    field(&scope, 1, 0x03, TB_SYMBOL_FORM_STRP_SUP, 0);
    open_scope(&scope);
    text = create(&scope);
    CHECK(text && run(&scope, text, 257) == TB_SYMBOL_INHERITED_TEXT_UNSUPPORTED);
    CHECK(tb_symbol_inherited_text_failure(text).reason == TB_SYMBOL_INHERITED_TEXT_FORM);
    CHECK(!tb_symbol_inherited_text_view(text, &view));
    tb_symbol_inherited_text_destroy(text);
    close_scope(&scope);
}


static void check_partial_units(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        Scope_t scope;
        prepare(&scope, 4, version, TB_SYMBOL_LITTLE_ENDIAN, 2);
        scope.entries[0].tag = 0x3c;
        if (version == 5) {
            scope.headers[0].header.kind = TB_SYMBOL_DWARF_PARTIAL;
            scope.headers[0].header.unit_type = 3;
            put(&scope, 1024 + scope.headers[0].extent.length_bytes + 2, 1, 3);
        }
        field(&scope, 1, 0x03, TB_SYMBOL_FORM_STRP, 0);
        open_scope(&scope);
        TbSymbolInheritedText_t *text = create(&scope);
        CHECK(text && run(&scope, text, 64) == TB_SYMBOL_INHERITED_TEXT_FOUND);
        TbSymbolInheritedTextView_t view;
        CHECK(tb_symbol_inherited_text_view(text, &view) && view.count == 1);
        CHECK(view.facts[0].text.text.bytes == 5 && scope.tree.kind == TB_SYMBOL_DWARF_PARTIAL);
        CHECK(view.facts[0].unit.header.kind == (version == 4 ? TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL
                                                            : TB_SYMBOL_DWARF_PARTIAL));
        tb_symbol_inherited_text_destroy(text);
        close_scope(&scope);
    }
}


static void check_reader_rejection(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_STRP, TB_SYMBOL_FORM_STRX1};
    const int flags[] = {O_PATH | O_CLOEXEC, O_RDONLY};
    for (size_t form = 0; form < sizeof(forms) / sizeof(forms[0]); ++form) {
        Scope_t scope;
        prepare(&scope, 4, 5, TB_SYMBOL_LITTLE_ENDIAN, 2);
        field(&scope, 1, 0x03, forms[form], 0);
        open_scope(&scope);
        for (size_t mode = 0; mode < sizeof(flags) / sizeof(flags[0]); ++mode) {
            int rejected = open(scope.path, flags[mode]);
            CHECK(rejected >= 0);
            scope.selection.descriptor = rejected;
            TbSymbolInheritedText_t *text = create(&scope);
            CHECK(text && run(&scope, text, 64) == TB_SYMBOL_INHERITED_TEXT_OPERATIONAL);
            TbSymbolInheritedTextFailure_t failure = tb_symbol_inherited_text_failure(text);
            CHECK(failure.reason == TB_SYMBOL_INHERITED_TEXT_VALUE);
            if (form) {
                CHECK(failure.text.reason == TB_SYMBOL_TEXT_OFFSETS);
                CHECK(failure.text.offsets.reason == TB_SYMBOL_STRING_OFFSETS_READ);
                CHECK(failure.text.offsets.read.reason == TB_SYMBOL_FILE_DESCRIPTOR);
            } else {
                CHECK(failure.text.reason == TB_SYMBOL_TEXT_STRING);
                CHECK(failure.text.string.reason == TB_SYMBOL_STRING_READ);
                CHECK(failure.text.string.read.reason == TB_SYMBOL_FILE_DESCRIPTOR);
            }
            TbSymbolInheritedTextView_t view;
            CHECK(!tb_symbol_inherited_text_view(text, &view));
            tb_symbol_inherited_text_destroy(text);
            CHECK(fcntl(rejected, F_GETFD) >= 0 && !close(rejected));
            CHECK(scope.budget.used == scope.baseline);
        }
        close_scope(&scope);
    }
}


int main(void)
{
    check_equal_values();
    check_disagreement_and_absence();
    check_cross_unit();
    check_interruption_and_dependencies();
    check_failures();
    check_reader_rejection();
    check_partial_units();
    puts("PASS inherited text byte agreement, distinct provenance, missing values and failure ownership");
    return 0;
}
