#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected file fixtures. */
#include "symbol/qualified.h"
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
static bool     check_comparison_bounds;
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


void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
    if (fault && ++allocations == fault)
        return NULL;
    return __real_malloc(size);
}


void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
    if (fault && ++allocations == fault)
        return NULL;
    return __real_calloc(count, size);
}


static int      read_error;
static bool     short_read;
static bool     early_eof;
static unsigned read_calls;
static unsigned interruptions;
static unsigned stalled_steps;

ssize_t __real_pread(int descriptor, void *bytes, size_t length, off_t offset);
ssize_t __wrap_pread(int descriptor, void *bytes, size_t length, off_t offset);

ssize_t __wrap_pread(int descriptor, void *bytes, size_t length, off_t offset)
{
    ++read_calls;
    if (interruptions) {
        --interruptions;
        errno = EINTR;
        return -1;
    }
    if (read_error) {
        errno      = read_error;
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
    TbSymbolSections_t      *sections;
    TbSymbolSectionsView_t   directory;
    TbSymbolStringView_t     strings[8];
    TbSymbolNameOrderInput_t input;
    int                      descriptor;
    char                     path[64];
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
    image[EI_CLASS]   = ELFCLASS64;
    image[EI_DATA]    = ELFDATA2LSB;
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
    size_t        length = 0;
    CHECK(count <= 8);
    for (size_t i = 0; i < count; ++i) {
        size_t size = strlen(names[i]) + 1;
        CHECK(size <= sizeof(bytes) - length);
        memcpy(bytes + length, names[i], size);
        length += size;
    }
    memset(bytes + length, 0, sizeof(bytes) - length);
    open_scope(scope, bytes, 120000);
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

#define NODE_CAPACITY 520
static TbSymbolScopeNode_t         nodes[NODE_CAPACITY];
static TbSymbolScopeEdge_t         edges[2 * NODE_CAPACITY];
static TbSymbolGraphNode_t         graphs[NODE_CAPACITY][3];
static TbSymbolContextAnchor_t     anchors[NODE_CAPACITY][2];
static TbSymbolInheritedTextFact_t facts[NODE_CAPACITY];
static TbSymbolScopeView_t         graph;
static Scope_t                     source;
static uint64_t                    language_code;

static TbSymbolReferenceTarget_t record(size_t index)
{
    return (TbSymbolReferenceTarget_t){
        .backing       = source.directory.backing,
        .section_index = 1,
        .record_offset = 1000 + 32 * index,
        .entry_index   = index + 1
    };
}


static void fixture(const char *const names[], size_t count)
{
    CHECK(count && count <= 8);
    open_names(&source, names, count);
    memset(nodes, 0, sizeof(nodes));
    memset(edges, 0, sizeof(edges));
    memset(graphs, 0, sizeof(graphs));
    memset(anchors, 0, sizeof(anchors));
    memset(facts, 0, sizeof(facts));
    language_code = 0x21;
    graph = (TbSymbolScopeView_t){
        .nodes      = nodes,
        .node_count = count,
        .edges      = edges,
        .edge_count = count
    };
    for (size_t i = 0; i < count; ++i) {
        TbSymbolReferenceTarget_t selected = record(i);
        TbSymbolReferenceTarget_t root     = selected;
        root.record_offset                 = 12;
        root.entry_index                   = 0;
        TbSymbolReferenceTarget_t parent   = i + 1 == count ? root : record(i + 1);
        TbSymbolGraphContext_t context = {
            .tag            = i ? 0x39 : 0x2e,
            .version        = 5,
            .unit_kind      = TB_SYMBOL_DWARF_COMPILE,
            .abbrev_section = 1,
            .parent_record  = parent.record_offset,
            .parent_tag     = i + 1 == count ? 0x11 : 0x39
        };
        graphs[i][0] = (TbSymbolGraphNode_t){
            .record   = selected,
            .context  = context,
            .complete = true
        };
        anchors[i][0] = (TbSymbolContextAnchor_t){
            .record    = selected,
            .context   = context,
            .kind      = i + 1 == count ? TB_SYMBOL_CONTEXT_UNIT_PARENT : TB_SYMBOL_CONTEXT_LEXICAL_PARENT,
            .parent    = parent,
            .unit_root = root
        };
        nodes[i] = (TbSymbolScopeNode_t){
            .record            = selected,
            .context           = context,
            .relationships     = {
                .nodes = graphs[i],
                .count = 1
            },
            .contexts          = {
                .selected = selected,
                .anchors  = anchors[i],
                .count    = 1
            },
            .first_edge        = i,
            .context_available = true,
            .complete          = true
        };
        edges[i] = (TbSymbolScopeEdge_t){
            .target_node = i + 1 == count ? SIZE_MAX : i + 1
        };
        facts[i] = (TbSymbolInheritedTextFact_t){
            .selection = {
                .backing       = selected.backing,
                .section_index = 1,
                .record_offset = selected.record_offset,
                .name          = 3,
                .matches       = 1,
                .first_offset  = selected.record_offset + 1
            },
            .text      = {
                .backing        = selected.backing,
                .source_section = 1,
                .source_record  = selected.record_offset,
                .abbrev_section = 1,
                .attribute      = {
                    .name   = 3,
                    .offset = selected.record_offset + 1
                },
                .text           = source.strings[i]
            }
        };
    }
}


static TbSymbolInheritedTextView_t name_source(TbSymbolQualifiedNeed_t need)
{
    return (TbSymbolInheritedTextView_t){
        .selected = need.record,
        .facts    = &facts[need.node_index],
        .count    = 1
    };
}


static TbSymbolConstantView_t language_source(TbSymbolQualifiedNeed_t need)
{
    return (TbSymbolConstantView_t){
        .selection      = {
            .backing       = need.record.backing,
            .section_index = need.record.section_index,
            .unit_offset   = need.record.unit_offset,
            .record_offset = need.record.record_offset,
            .name          = TB_SYMBOL_CONSTANT_LANGUAGE,
            .matches       = 1,
            .first_offset  = need.record.record_offset + 1
        },
        .attribute      = {
            .name   = TB_SYMBOL_CONSTANT_LANGUAGE,
            .offset = need.record.record_offset + 1
        },
        .abbrev_section = need.context.abbrev_section,
        .abbrev_offset  = need.context.abbrev_offset,
        .value          = language_code
    };
}


static TbSymbolQualifiedStatus_e run_qualified(TbSymbolQualified_t *qualified)
{
    for (unsigned turn = 0; turn < 3000000; ++turn) {
        TbSymbolQualifiedView_t view;
        CHECK(!tb_symbol_qualified_view(qualified, &view));
        TbSymbolQualifiedNeed_t need;
        if (tb_symbol_qualified_need(qualified, &need)) {
            if (need.kind == TB_SYMBOL_QUALIFIED_NAME) {
                TbSymbolInheritedTextView_t value = name_source(need);
                TbSymbolQualifiedStatus_e status = tb_symbol_qualified_name(qualified,
                                                                           TB_SYMBOL_INHERITED_TEXT_FOUND,
                                                                           &value);
                if (status != TB_SYMBOL_QUALIFIED_PENDING)
                    return status;
            } else {
                TbSymbolConstantView_t    value = language_source(need);
                TbSymbolQualifiedStatus_e status = tb_symbol_qualified_language(qualified,
                                                                                TB_SYMBOL_CONSTANT_FOUND,
                                                                                &value);
                if (status != TB_SYMBOL_QUALIFIED_PENDING)
                    return status;
            }
        }
        read_calls = 0;
        check_comparison_bounds = true;
        uint64_t progress = tb_symbol_qualified_progress(qualified);
        unsigned before_interruptions = interruptions;
        TbSymbolQualifiedStatus_e status = tb_symbol_qualified_step(qualified, 7);
        if (before_interruptions != interruptions &&
            progress == tb_symbol_qualified_progress(qualified)) {
            ++stalled_steps;
        }
        check_comparison_bounds = false;
        CHECK(read_calls <= 1);
        if (status != TB_SYMBOL_QUALIFIED_PENDING && status != TB_SYMBOL_QUALIFIED_SOURCE)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_QUALIFIED_INVALID;
}


static void check_chain(void)
{
    const char *names[] = {"method", "Box<N::Target>", "N"};
    fixture(names, 3);
    nodes[1].context.tag     = 0x13;
    graphs[1][0].context.tag = 0x13;
    TbSymbolBudget_t construction;
    CHECK(tb_symbol_budget_init(&construction, 1024 * 1024));
    TbSymbolQualified_t *qualified = tb_symbol_qualified_create(source.directory, graph, source.descriptor,
                                                               &construction);
    CHECK(qualified && run_qualified(qualified) == TB_SYMBOL_QUALIFIED_READY);
    TbSymbolQualifiedView_t view;
    CHECK(tb_symbol_qualified_view(qualified, &view));
    CHECK(view.count == 3 && view.nodes[0].part_count == 3 && view.nodes[0].parent_node == 1);
    CHECK(view.nodes[0].bytes == strlen("N::Box<N::Target>::method"));
    CHECK(view.nodes[1].parent_node == 2 && view.nodes[2].parent_node == SIZE_MAX);
    CHECK(view.language_count == 3 && view.languages[0].value == language_code);
    CHECK(tb_symbol_qualified_progress(qualified));
    tb_symbol_qualified_destroy(qualified);
    CHECK(!construction.used);
    close_scope(&source);
}


static TbSymbolQualified_t *start(TbSymbolBudget_t *budget)
{
    CHECK(tb_symbol_budget_init(budget, 1024 * 1024));
    TbSymbolQualified_t *qualified = tb_symbol_qualified_create(source.directory, graph, source.descriptor,
                                                               budget);
    CHECK(qualified);
    return qualified;
}


static void finish(TbSymbolQualified_t *qualified, TbSymbolBudget_t *budget)
{
    tb_symbol_qualified_destroy(qualified);
    CHECK(!budget->used);
    CHECK(fcntl(source.descriptor, F_GETFD) >= 0);
    close_scope(&source);
}


static void diamond(const char *flat)
{
    const char *names[] = {"f", flat, "R", "Q"};
    fixture(names, 4);
    /* Two physical derivations lead to the same complete prefix bytes,
     * while preserving distinct component boundaries and owner records. */
    for (size_t i = 3; i > 0; --i) {
        edges[i + 1]        = edges[i];
        nodes[i].first_edge = i + 1;
    }
    graph.edge_count                   = 5;
    edges[0].target_node               = 1;
    edges[1] = (TbSymbolScopeEdge_t){
        .anchor_index = 1,
        .target_node  = 2
    };
    edges[2].target_node               = SIZE_MAX;
    graphs[1][0].context.parent_record = 12;
    graphs[1][0].context.parent_tag    = 0x11;
    nodes[1].context                   = graphs[1][0].context;
    anchors[1][0].kind                 = TB_SYMBOL_CONTEXT_UNIT_PARENT;
    anchors[1][0].parent               = anchors[1][0].unit_root;
    anchors[1][0].context              = nodes[1].context;
    graphs[0][1]                       = graphs[0][0];
    graphs[0][1].record                = record(100);
    graphs[0][1].context.parent_record = record(2).record_offset;
    anchors[0][1]                      = anchors[0][0];
    anchors[0][1].record               = graphs[0][1].record;
    anchors[0][1].context              = graphs[0][1].context;
    anchors[0][1].parent               = record(2);
    nodes[0].relationships.count       = 2;
    nodes[0].contexts.count            = 2;
}


static void check_agreement(void)
{
    diamond("Q::R");
    TbSymbolBudget_t budget;
    TbSymbolQualified_t *qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_READY);
    TbSymbolQualifiedView_t view;
    CHECK(tb_symbol_qualified_view(qualified, &view));
    CHECK(view.count == 4 && view.scope.edge_count == 5 && view.language_count == 5);
    CHECK(view.nodes[0].parent_node == 1 && view.nodes[0].bytes == strlen("Q::R::f"));
    CHECK(view.nodes[1].part_count == 1 && view.nodes[2].part_count == 2);
    CHECK(view.scope.nodes[0].contexts.count == 2 && comparisons);
    finish(qualified, &budget);

    diamond("Q::R");
    facts[1].text.text = source.strings[3];
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_CONFLICT);
    CHECK(tb_symbol_qualified_failure(qualified).reason == TB_SYMBOL_QUALIFIED_AGREEMENT);
    CHECK(tb_symbol_qualified_failure(qualified).anchor_index == 1);
    finish(qualified, &budget);

    diamond("Z::R");
    /* Equal length does not establish complete byte agreement. */
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_CONFLICT);
    CHECK(tb_symbol_qualified_failure(qualified).reason == TB_SYMBOL_QUALIFIED_AGREEMENT);
    finish(qualified, &budget);

    diamond("Q::R");
    edges[1].target_node               = 1;
    anchors[0][1].parent               = record(1);
    graphs[0][1].context.parent_record = record(1).record_offset;
    anchors[0][1].context              = graphs[0][1].context;
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_READY);
    finish(qualified, &budget);
}


static void check_profiles(void)
{
    const char *names[] = {"f", "N"};
    TbSymbolBudget_t budget;
    fixture(names, 1);
    language_code = 2;
    TbSymbolQualified_t *qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_READY);
    finish(qualified, &budget);

    fixture(names, 2);
    language_code = 2;
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_UNSUPPORTED);
    finish(qualified, &budget);

    fixture(names, 1);
    language_code = 0xffff;
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_UNSUPPORTED);
    finish(qualified, &budget);

    fixture(names, 2);
    nodes[1].context.tag     = 0x2e;
    graphs[1][0].context.tag = 0x2e;
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_UNSUPPORTED);
    CHECK(tb_symbol_qualified_failure(qualified).reason == TB_SYMBOL_QUALIFIED_PARENT_PROFILE);
    finish(qualified, &budget);

    const char *empty[] = {"", "N"};
    fixture(empty, 2);
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_UNNAMED);
    finish(qualified, &budget);

    const char *anonymous[] = {"f", ""};
    fixture(anonymous, 2);
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_UNSUPPORTED);
    finish(qualified, &budget);
}


static TbSymbolQualifiedNeed_t await_need(TbSymbolQualified_t *qualified)
{
    TbSymbolQualifiedNeed_t need;
    for (unsigned i = 0; i < 10000; ++i) {
        if (tb_symbol_qualified_need(qualified, &need))
            return need;
        TbSymbolQualifiedStatus_e status = tb_symbol_qualified_step(qualified, 31);
        CHECK(status == TB_SYMBOL_QUALIFIED_PENDING || status == TB_SYMBOL_QUALIFIED_SOURCE);
    }
    CHECK(false);
    return (TbSymbolQualifiedNeed_t){0};
}


static void check_sources(void)
{
    const char *names[] = {"f"};
    fixture(names, 1);
    TbSymbolBudget_t budget;
    TbSymbolQualified_t *qualified = start(&budget);
    TbSymbolQualifiedNeed_t need      = await_need(qualified);
    CHECK(need.kind == TB_SYMBOL_QUALIFIED_NAME);
    TbSymbolInheritedTextView_t value = name_source(need);
    value.selected.entry_index++;
    CHECK(tb_symbol_qualified_name(qualified, TB_SYMBOL_INHERITED_TEXT_FOUND, &value) ==
          TB_SYMBOL_QUALIFIED_INVALID);
    TbSymbolQualifiedNeed_t unchanged = await_need(qualified);
    CHECK(unchanged.record.entry_index == need.record.entry_index);
    value = name_source(need);
    CHECK(tb_symbol_qualified_name(qualified, TB_SYMBOL_INHERITED_TEXT_FOUND, &value) ==
          TB_SYMBOL_QUALIFIED_PENDING);
    need = await_need(qualified);
    CHECK(need.kind == TB_SYMBOL_QUALIFIED_LANGUAGE && need.record.record_offset == 12);
    TbSymbolConstantView_t language = language_source(need);
    language.selection.record_offset++;
    CHECK(tb_symbol_qualified_language(qualified, TB_SYMBOL_CONSTANT_FOUND, &language) ==
          TB_SYMBOL_QUALIFIED_INVALID);
    CHECK(tb_symbol_qualified_language(qualified, TB_SYMBOL_CONSTANT_ABSENT, NULL) ==
          TB_SYMBOL_QUALIFIED_UNSUPPORTED);
    finish(qualified, &budget);

    const TbSymbolInheritedTextStatus_e errors[] = {
        TB_SYMBOL_INHERITED_TEXT_ABSENT,     TB_SYMBOL_INHERITED_TEXT_CONFLICT,
        TB_SYMBOL_INHERITED_TEXT_DEPENDENCY, TB_SYMBOL_INHERITED_TEXT_UNSUPPORTED,
        TB_SYMBOL_INHERITED_TEXT_MALFORMED,  TB_SYMBOL_INHERITED_TEXT_RESOURCE,
        TB_SYMBOL_INHERITED_TEXT_OPERATIONAL
    };
    const TbSymbolQualifiedStatus_e expected[] = {
        TB_SYMBOL_QUALIFIED_UNNAMED, TB_SYMBOL_QUALIFIED_CONFLICT,
        TB_SYMBOL_QUALIFIED_DEPENDENCY, TB_SYMBOL_QUALIFIED_UNSUPPORTED,
        TB_SYMBOL_QUALIFIED_MALFORMED, TB_SYMBOL_QUALIFIED_RESOURCE,
        TB_SYMBOL_QUALIFIED_OPERATIONAL
    };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        fixture(names, 1);
        qualified = start(&budget);
        (void)await_need(qualified);
        CHECK(tb_symbol_qualified_name(qualified, errors[i], NULL) == expected[i]);
        TbSymbolQualifiedView_t view;
        CHECK(!tb_symbol_qualified_view(qualified, &view));
        finish(qualified, &budget);
    }
    fixture(names, 1);
    facts[0].text.source_record++;
    qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_INVALID);
    finish(qualified, &budget);
}


static void check_complete_owners(void)
{
    diamond("Q::R");
    TbSymbolBudget_t budget;
    TbSymbolQualified_t *qualified = start(&budget);
    TbSymbolQualifiedNeed_t need = await_need(qualified);
    TbSymbolInheritedTextFact_t owners[2] = {facts[0], facts[0]};
    owners[1].node_index = 1;
    owners[1].selection.record_offset = graphs[0][1].record.record_offset;
    owners[1].selection.first_offset = graphs[0][1].record.record_offset + 1;
    owners[1].text.source_record = graphs[0][1].record.record_offset;
    owners[1].text.attribute.offset = graphs[0][1].record.record_offset + 1;
    TbSymbolInheritedTextView_t name = {
        .selected = need.record,
        .facts    = owners,
        .count    = 2
    };
    CHECK(tb_symbol_qualified_name(qualified, TB_SYMBOL_INHERITED_TEXT_FOUND, &name) ==
          TB_SYMBOL_QUALIFIED_PENDING);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_READY);
    TbSymbolQualifiedView_t view;
    CHECK(tb_symbol_qualified_view(qualified, &view));
    CHECK(view.nodes[0].name.facts == owners && view.nodes[0].name.count == 2);
    finish(qualified, &budget);

    diamond("Q::R");
    qualified = start(&budget);
    need = await_need(qualified);
    name.selected = need.record;
    owners[1].text.source_record++;
    CHECK(tb_symbol_qualified_name(qualified, TB_SYMBOL_INHERITED_TEXT_FOUND, &name) ==
          TB_SYMBOL_QUALIFIED_PENDING);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_INVALID);
    finish(qualified, &budget);
}


static void check_owner_language(void)
{
    const char *names[] = {"f"};
    fixture(names, 1);
    /* The selected record's own name does not replace the relationship's
     * terminal declaration context or its distinct owning unit's language. */
    graphs[0][1] = graphs[0][0];
    graphs[0][1].record.unit_offset = 40000;
    graphs[0][1].record.record_offset = 40100;
    graphs[0][1].record.entry_index = 1;
    graphs[0][1].context.version = 4;
    graphs[0][1].context.unit_kind = TB_SYMBOL_DWARF_PARTIAL;
    graphs[0][1].context.parent_record = 40012;
    graphs[0][1].context.parent_tag = 0x3c;
    nodes[0].relationships.count = 2;
    anchors[0][0].node_index = 1;
    anchors[0][0].record = graphs[0][1].record;
    anchors[0][0].context = graphs[0][1].context;
    anchors[0][0].unit_root.unit_offset = 40000;
    anchors[0][0].unit_root.record_offset = 40012;
    anchors[0][0].parent = anchors[0][0].unit_root;
    TbSymbolBudget_t budget;
    TbSymbolQualified_t *qualified = start(&budget);
    TbSymbolQualifiedNeed_t need = await_need(qualified);
    TbSymbolInheritedTextView_t name = name_source(need);
    CHECK(tb_symbol_qualified_name(qualified, TB_SYMBOL_INHERITED_TEXT_FOUND, &name) ==
          TB_SYMBOL_QUALIFIED_PENDING);
    need = await_need(qualified);
    CHECK(need.kind == TB_SYMBOL_QUALIFIED_LANGUAGE && need.record.unit_offset == 40000);
    CHECK(need.record.record_offset == 40012 && need.context.tag == 0x3c && need.context.version == 4);
    TbSymbolConstantView_t language = language_source(need);
    CHECK(tb_symbol_qualified_language(qualified, TB_SYMBOL_CONSTANT_FOUND, &language) ==
          TB_SYMBOL_QUALIFIED_PENDING);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_READY);
    finish(qualified, &budget);
}


static void check_allocations(void)
{
    const char *names[] = {"f", "N"};
    fixture(names, 2);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    for (unsigned i = 1; i <= 5; ++i) {
        fault       = i;
        allocations = 0;
        CHECK(!tb_symbol_qualified_create(source.directory, graph, source.descriptor, &budget));
        fault = 0;
        CHECK(!budget.used);
    }
    TbSymbolQualified_t *old = tb_symbol_qualified_create(source.directory, graph, source.descriptor,
                                                          &budget);
    CHECK(old && run_qualified(old) == TB_SYMBOL_QUALIFIED_READY);
    size_t retained = budget.used;
    budget.limit    = retained * 2 - 1;
    CHECK(!tb_symbol_qualified_create(source.directory, graph, source.descriptor, &budget));
    CHECK(budget.used == retained);
    TbSymbolQualifiedView_t view;
    CHECK(tb_symbol_qualified_view(old, &view));
    tb_symbol_qualified_destroy(old);
    CHECK(!budget.used);
    close_scope(&source);

    bool completed = false;
    for (unsigned limit = 0; limit < 1000; ++limit) {
        diamond("Q::R");
        TbSymbolQualified_t *qualified = start(&budget);
        for (unsigned turn = 0; turn < limit; ++turn) {
            TbSymbolQualifiedNeed_t need;
            if (tb_symbol_qualified_need(qualified, &need)) {
                if (need.kind == TB_SYMBOL_QUALIFIED_NAME) {
                    TbSymbolInheritedTextView_t name = name_source(need);
                    CHECK(tb_symbol_qualified_name(qualified, TB_SYMBOL_INHERITED_TEXT_FOUND, &name) ==
                          TB_SYMBOL_QUALIFIED_PENDING);
                } else {
                    TbSymbolConstantView_t language = language_source(need);
                    CHECK(tb_symbol_qualified_language(qualified, TB_SYMBOL_CONSTANT_FOUND, &language) ==
                          TB_SYMBOL_QUALIFIED_PENDING);
                }
            }
            TbSymbolQualifiedStatus_e status = tb_symbol_qualified_step(qualified, 7);
            if (status == TB_SYMBOL_QUALIFIED_READY) {
                completed = true;
                break;
            }
            CHECK(status == TB_SYMBOL_QUALIFIED_PENDING || status == TB_SYMBOL_QUALIFIED_SOURCE);
        }
        finish(qualified, &budget);
        if (completed)
            break;
    }
    CHECK(completed);
}


static void check_comparison_failures(void)
{
    for (unsigned i = 6; i <= 8; ++i) {
        diamond("Q::R");
        TbSymbolBudget_t budget;
        CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
        allocations = 0;
        fault       = i;
        TbSymbolQualified_t *qualified = tb_symbol_qualified_create(source.directory, graph,
                                                                   source.descriptor, &budget);
        CHECK(qualified && run_qualified(qualified) == TB_SYMBOL_QUALIFIED_RESOURCE);
        fault = 0;
        finish(qualified, &budget);
    }
    const int errors[] = {EIO, ENOMEM, EINTR};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        diamond("Q::R");
        TbSymbolBudget_t budget;
        TbSymbolQualified_t *qualified = start(&budget);
        read_error                       = errors[i];
        TbSymbolQualifiedStatus_e status = run_qualified(qualified);
        CHECK(status == (errors[i] == EINTR    ? TB_SYMBOL_QUALIFIED_READY
                          : errors[i] == ENOMEM ? TB_SYMBOL_QUALIFIED_RESOURCE
                                              : TB_SYMBOL_QUALIFIED_OPERATIONAL));
        finish(qualified, &budget);
    }
    diamond("Q::R");
    TbSymbolBudget_t budget;
    TbSymbolQualified_t *qualified = start(&budget);
    interruptions = 32;
    stalled_steps = 0;
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_READY);
    CHECK(!interruptions && stalled_steps >= 30);
    finish(qualified, &budget);

    diamond("Q::R");
    qualified = start(&budget);
    early_eof = true;
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_OPERATIONAL);
    early_eof = false;
    finish(qualified, &budget);
}


static void check_deep(void)
{
    const char *names[] = {"f", "N"};
    fixture(names, 2);
    const size_t count = 512;
    graph.node_count   = count;
    graph.edge_count   = count;
    for (size_t i = 0; i < count; ++i) {
        if (i > 1) {
            nodes[i]      = nodes[1];
            graphs[i][0]  = graphs[1][0];
            anchors[i][0] = anchors[1][0];
            facts[i]      = facts[1];
        }
        TbSymbolReferenceTarget_t selected = record(i);
        TbSymbolReferenceTarget_t root     = selected;
        root.record_offset                 = 12;
        root.entry_index                   = 0;
        TbSymbolReferenceTarget_t parent   = i + 1 == count ? root : record(i + 1);
        nodes[i].record                    = selected;
        nodes[i].first_edge                = i;
        nodes[i].context.parent_record     = parent.record_offset;
        nodes[i].context.parent_tag        = i + 1 == count ? 0x11 : 0x39;
        graphs[i][0].record                = selected;
        graphs[i][0].context               = nodes[i].context;
        nodes[i].relationships.nodes       = graphs[i];
        nodes[i].contexts.selected         = selected;
        nodes[i].contexts.anchors          = anchors[i];
        anchors[i][0].record               = selected;
        anchors[i][0].context              = nodes[i].context;
        anchors[i][0].kind = i + 1 == count ? TB_SYMBOL_CONTEXT_UNIT_PARENT
                                           : TB_SYMBOL_CONTEXT_LEXICAL_PARENT;
        anchors[i][0].parent             = parent;
        anchors[i][0].unit_root          = root;
        edges[i].target_node             = i + 1 == count ? SIZE_MAX : i + 1;
        facts[i].selection.record_offset = selected.record_offset;
        facts[i].selection.first_offset  = selected.record_offset + 1;
        facts[i].text.source_record      = selected.record_offset;
        facts[i].text.attribute.offset   = selected.record_offset + 1;
    }
    TbSymbolBudget_t budget;
    TbSymbolQualified_t *qualified = start(&budget);
    CHECK(run_qualified(qualified) == TB_SYMBOL_QUALIFIED_READY);
    TbSymbolQualifiedView_t view;
    CHECK(tb_symbol_qualified_view(qualified, &view));
    CHECK(view.nodes[0].part_count == count && view.nodes[0].bytes == 3 * (count - 1) + 1);
    finish(qualified, &budget);
}


int main(void)
{
    check_chain();
    check_agreement();
    check_profiles();
    check_sources();
    check_complete_owners();
    check_owner_language();
    check_allocations();
    check_comparison_failures();
    check_deep();
    puts("PASS qualified metadata recipes, agreement, profiles, sources and allocation cleanup");
    return 0;
}
