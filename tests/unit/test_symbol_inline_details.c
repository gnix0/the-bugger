#include "symbol/inline_details.h"
#include "../support/check.h"

#include <string.h>

static TbSymbolInlineDetails_t details;
static TbSymbolInlineDetail_t arena[600];
static TbSymbolDieEntry_t entries[603];
static TbSymbolDieAttribute_t attributes[1800];
static TbSymbolGraphNode_t nodes[600][2];
static TbSymbolUnitEntry_t unit;
static TbSymbolUnitsView_t units;
static TbSymbolDiesView_t tree;
static TbSymbolAbbrevEntry_t abbreviation;
static TbSymbolAbbrevAttribute_t schema_attribute;
static TbSymbolAbbrevView_t schema;

static TbSymbolReferenceTarget_t record(size_t index)
{
    return (TbSymbolReferenceTarget_t) {
        .backing       = tree.backing,
        .section_index = tree.section_index,
        .unit_offset   = tree.unit_offset,
        .record_offset = entries[index].offset,
        .entry_index   = index
    };
}


static void fixture(size_t count)
{
    tb_symbol_inline_details_discard(&details);
    CHECK(count <= 600);
    memset(entries, 0, sizeof(entries));
    memset(attributes, 0, sizeof(attributes));
    memset(nodes, 0, sizeof(nodes));
    tree = (TbSymbolDiesView_t) {
        .entries         = entries,
        .attributes      = attributes,
        .count           = count + 3,
        .attribute_count = count * 3,
        .section_index   = 2,
        .abbrev_section  = 3,
        .abbrev_offset   = 17,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing = {
            .key = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 65536
        }
    };
    unit = (TbSymbolUnitEntry_t) {
        .header = {
            .kind          = TB_SYMBOL_DWARF_COMPILE,
            .version       = 5,
            .header_bytes  = 12,
            .abbrev_offset = 17
        },
        .header_available = true
    };
    units = (TbSymbolUnitsView_t) {
        .entries       = &unit,
        .count         = 1,
        .section_index = 2,
        .backing       = tree.backing
    };
    for (size_t i = 0; i < tree.count; ++i) {
        entries[i] = (TbSymbolDieEntry_t) {
            .offset          = 12 + 32 * i,
            .end_offset      = 28 + 32 * i,
            .parent          = i ? 0 : UINT64_MAX,
            .code            = i + 1,
            .tag             = i ? (i < 3 ? 0x2e : 0x1d) : 0x11,
            .first_attribute = i < 3 ? 0 : (i - 3) * 3,
            .attribute_count = i < 3 ? 0 : 3
        };
    }
    for (size_t i = 0; i < count; ++i) {
        entries[i + 3].parent = i ? i + 2 : 1;
        for (unsigned j = 0; j < 3; ++j) {
            attributes[i * 3 + j] = (TbSymbolDieAttribute_t) {
                .name          = j == 0 ? 0x58 : j == 1 ? 0x59 : 0x57,
                .declared_form = TB_SYMBOL_FORM_UDATA,
                .offset        = entries[i + 3].offset + j + 1,
                .value = {
                    .form    = TB_SYMBOL_FORM_UDATA,
                    .bits    = j == 0 ? 7 : j == 1 ? 42 : 0,
                    .numeric = true
                }
            };
        }
        nodes[i][0] = (TbSymbolGraphNode_t) {
            .record = record(i + 3),
            .context = {
                .tag            = 0x1d,
                .version        = 5,
                .unit_kind      = tree.kind,
                .abbrev_section = tree.abbrev_section,
                .abbrev_offset  = tree.abbrev_offset,
                .parent_record  = entries[entries[i + 3].parent].offset,
                .parent_tag     = entries[entries[i + 3].parent].tag
            },
            .present  = 1u << (unsigned)TB_SYMBOL_RELATION_ORIGIN,
            .complete = true
        };
        nodes[i][0].edges[TB_SYMBOL_RELATION_ORIGIN].target = record(2);
        nodes[i][1] = (TbSymbolGraphNode_t) {
            .record   = record(2),
            .complete = true
        };
    }
}


static void start(size_t capacity)
{
    CHECK(tb_symbol_inline_details_init(&details, units, tree, 0, arena, capacity) ==
          TB_SYMBOL_INLINE_DETAILS_PENDING);
}


static void supply(void)
{
    TbSymbolReferenceTarget_t need;
    CHECK(tb_symbol_inline_details_need(&details, &need));
    TbSymbolGraphView_t graph = {
        .nodes = nodes[need.entry_index - 3],
        .count = 2
    };
    CHECK(tb_symbol_inline_details_graph(&details, need, TB_SYMBOL_GRAPH_READY, &graph, NULL) ==
          TB_SYMBOL_INLINE_DETAILS_PENDING);
}


static TbSymbolInlineDetailsStatus_e run(size_t slice)
{
    for (size_t turn = 0; turn < 100000; ++turn) {
        TbSymbolInlineDetailsView_t unavailable;
        CHECK(!tb_symbol_inline_details_view(&details, &unavailable));
        if (details.status == TB_SYMBOL_INLINE_DETAILS_GRAPH)
            supply();
        else if (details.status == TB_SYMBOL_INLINE_DETAILS_SCHEMA)
            CHECK(tb_symbol_inline_details_schema(&details, &schema) == TB_SYMBOL_INLINE_DETAILS_PENDING);

        uint64_t before = tb_symbol_inline_details_progress(&details);
        TbSymbolInlineDetailsStatus_e status = tb_symbol_inline_details_step(&details, slice);
        CHECK(tb_symbol_inline_details_progress(&details) > before);
        CHECK(tb_symbol_inline_details_progress(&details) - before <= slice);
        if (status != TB_SYMBOL_INLINE_DETAILS_PENDING && status != TB_SYMBOL_INLINE_DETAILS_GRAPH &&
            status != TB_SYMBOL_INLINE_DETAILS_SCHEMA) {
            return status;
        }
    }
    CHECK(false);
    return TB_SYMBOL_INLINE_DETAILS_INVALID;
}


static TbSymbolInlineDetailsView_t completed(void)
{
    TbSymbolInlineDetailsView_t view;
    CHECK(tb_symbol_inline_details_view(&details, &view));
    return view;
}


static void check_inventory(void)
{
    fixture(3);
    start(3);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_READY);
    TbSymbolInlineDetailsView_t view = completed();
    CHECK(view.count == 3 && !view.partial);
    for (size_t i = 0; i < view.count; ++i) {
        const TbSymbolInlineDetail_t *instance = &view.instances[i];
        CHECK(instance->record.entry_index == i + 3);
        CHECK(instance->parent.entry_index == (i ? i + 2 : 1));
        CHECK(instance->parent_tag == (i ? 0x1d : 0x2e));
        CHECK(instance->origin_available && instance->relationships.nodes == nodes[i]);
        CHECK(instance->coordinates[0].constant.value == 7);
        CHECK(instance->coordinates[1].constant.value == 42);
        CHECK(instance->coordinates[2].constant.value == 0);
        CHECK(instance->coordinates[2].status == TB_SYMBOL_CONSTANT_FOUND);
    }

    fixture(0);
    start(0);
    CHECK(run(256) == TB_SYMBOL_INLINE_DETAILS_READY && completed().count == 0);

    fixture(520);
    start(600);
    CHECK(run(256) == TB_SYMBOL_INLINE_DETAILS_READY && completed().count == 520);
    CHECK(tb_symbol_inline_details_progress(&details) > 520);

    fixture(1);
    nodes[0][0].present = 0;
    attributes[0].name = 0x3a; /* Declaration file does not supply call_file. */
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_PARTIAL);
    CHECK(!completed().instances[0].origin_available);
    CHECK(completed().instances[0].coordinates[0].status == TB_SYMBOL_CONSTANT_ABSENT);

    fixture(1);
    attributes[2].value.form = TB_SYMBOL_FORM_DATA16;
    attributes[2].declared_form = TB_SYMBOL_FORM_DATA16;
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_PARTIAL);
    CHECK(completed().instances[0].coordinates[2].status == TB_SYMBOL_CONSTANT_UNSUPPORTED);
    CHECK(completed().instances[0].coordinates[2].selection.first_index == 2);

    fixture(1);
    attributes[0].value.bits = UINT64_MAX;
    attributes[1].value.bits = 0;
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_READY);
    CHECK(completed().instances[0].coordinates[0].constant.value == UINT64_MAX);
    CHECK(completed().instances[0].coordinates[1].status == TB_SYMBOL_CONSTANT_FOUND);
    CHECK(completed().instances[0].coordinates[1].constant.value == 0);

    fixture(1);
    unit.header.version = 4;
    unit.header.kind = TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
    nodes[0][0].context.version = 4;
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_READY);

    fixture(1);
    /* A completed child graph may retain an origin in another captured unit. */
    nodes[0][1].record.unit_offset = 4096;
    nodes[0][1].record.record_offset = 4108;
    nodes[0][0].edges[TB_SYMBOL_RELATION_ORIGIN].target = nodes[0][1].record;
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_READY);
    CHECK(completed().instances[0].relationships.nodes[1].record.unit_offset == 4096);
    CHECK(completed().instances[0].record.unit_offset == 0);
}


static void check_failures(void)
{
    fixture(2);
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_RESOURCE);
    TbSymbolInlineDetailsFailure_t failure;
    CHECK(tb_symbol_inline_details_failure(&details, &failure));
    CHECK(failure.record_available && failure.record.entry_index == 4);
    TbSymbolInlineDetailsView_t view;
    CHECK(!tb_symbol_inline_details_view(&details, &view));

    fixture(1);
    start(0);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_RESOURCE);

    fixture(2);
    attributes[5].name = 0x59;
    start(2);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_CONFLICT);
    CHECK(tb_symbol_inline_details_failure(&details, &failure));
    CHECK(failure.field == TB_SYMBOL_CONSTANT_CALL_LINE && failure.selection_available);
    CHECK(failure.selection.matches == 2 && failure.record.entry_index == 4);

    fixture(1);
    attributes[0].value.form = TB_SYMBOL_FORM_ADDR;
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_MALFORMED);

    fixture(1);
    nodes[0][1].complete = false;
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_INVALID);

    fixture(1);
    entries[3].parent = 3;
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_INVALID);
}


static void check_supplies(void)
{
    fixture(1);
    start(1);
    CHECK(tb_symbol_inline_details_step(&details, 256) == TB_SYMBOL_INLINE_DETAILS_GRAPH);
    TbSymbolReferenceTarget_t need;
    CHECK(tb_symbol_inline_details_need(&details, &need));
    TbSymbolReferenceTarget_t wrong = need;
    ++wrong.backing.key.inode;
    CHECK(tb_symbol_inline_details_graph(&details, wrong, TB_SYMBOL_GRAPH_RESOURCE, NULL, NULL) ==
          TB_SYMBOL_INLINE_DETAILS_INVALID);
    CHECK(details.status == TB_SYMBOL_INLINE_DETAILS_GRAPH);
    TbSymbolGraphView_t graph = {
        .nodes = nodes[0],
        .count = 2
    };
    ++nodes[0][0].context.abbrev_offset;
    CHECK(tb_symbol_inline_details_graph(&details, need, TB_SYMBOL_GRAPH_READY, &graph, NULL) ==
          TB_SYMBOL_INLINE_DETAILS_INVALID);
    --nodes[0][0].context.abbrev_offset;
    CHECK(details.status == TB_SYMBOL_INLINE_DETAILS_GRAPH);
    CHECK(tb_symbol_inline_details_graph(&details, need, TB_SYMBOL_GRAPH_RELATION, NULL, NULL) ==
          TB_SYMBOL_INLINE_DETAILS_INVALID);

    TbSymbolGraphFailure_t failure = {
        .relation_status = TB_SYMBOL_RELATION_MALFORMED
    };
    CHECK(tb_symbol_inline_details_graph(&details, need, TB_SYMBOL_GRAPH_RELATION, NULL, &failure) ==
          TB_SYMBOL_INLINE_DETAILS_MALFORMED);
    TbSymbolInlineDetailsFailure_t reported;
    CHECK(tb_symbol_inline_details_failure(&details, &reported));
    CHECK(reported.graph_failure_available && reported.relation_status == TB_SYMBOL_RELATION_MALFORMED);
    TbSymbolInlineDetailsView_t view;
    CHECK(!tb_symbol_inline_details_view(&details, &view));

    const TbSymbolGraphStatus_e statuses[] = {
        TB_SYMBOL_GRAPH_CYCLE, TB_SYMBOL_GRAPH_DEPENDENCY, TB_SYMBOL_GRAPH_RESOURCE, TB_SYMBOL_GRAPH_RELATION
    };
    const TbSymbolInlineDetailsStatus_e outcomes[] = {
        TB_SYMBOL_INLINE_DETAILS_CONFLICT, TB_SYMBOL_INLINE_DETAILS_DEPENDENCY,
        TB_SYMBOL_INLINE_DETAILS_RESOURCE, TB_SYMBOL_INLINE_DETAILS_UNSUPPORTED
    };
    for (size_t i = 0; i < sizeof(statuses) / sizeof(*statuses); ++i) {
        fixture(1);
        start(1);
        CHECK(tb_symbol_inline_details_step(&details, 256) == TB_SYMBOL_INLINE_DETAILS_GRAPH);
        CHECK(tb_symbol_inline_details_need(&details, &need));
        failure.relation_status = TB_SYMBOL_RELATION_UNSUPPORTED;
        CHECK(tb_symbol_inline_details_graph(&details, need, statuses[i], NULL, &failure) == outcomes[i]);
        CHECK(!tb_symbol_inline_details_view(&details, &view));
    }

    fixture(2);
    start(2);
    for (unsigned turn = 0; turn < 1000; ++turn) {
        if (details.status == TB_SYMBOL_INLINE_DETAILS_GRAPH && details.cursor == 4)
            break;
        if (details.status == TB_SYMBOL_INLINE_DETAILS_GRAPH)
            supply();
        (void)tb_symbol_inline_details_step(&details, 1);
    }
    CHECK(details.view.count == 1 && tb_symbol_inline_details_need(&details, &need));
    CHECK(tb_symbol_inline_details_graph(&details, need, TB_SYMBOL_GRAPH_DEPENDENCY, NULL, NULL) ==
          TB_SYMBOL_INLINE_DETAILS_DEPENDENCY);
    CHECK(!tb_symbol_inline_details_view(&details, &view));
}


static void check_schema(void)
{
    fixture(1);
    attributes[0].declared_form = TB_SYMBOL_FORM_IMPLICIT_CONST;
    attributes[0].value = (TbSymbolFormView_t) {
        .form = TB_SYMBOL_FORM_IMPLICIT_CONST
    };
    abbreviation = (TbSymbolAbbrevEntry_t) {
        .code            = entries[3].code,
        .tag             = entries[3].tag,
        .attribute_count = 3
    };
    schema_attribute = (TbSymbolAbbrevAttribute_t) {
        .name            = 0x58,
        .form            = TB_SYMBOL_FORM_IMPLICIT_CONST,
        .offset          = 18,
        .implicit_offset = 20,
        .implicit_bytes  = 1,
        .implicit_value  = 9,
        .implicit_fits   = true
    };
    schema = (TbSymbolAbbrevView_t) {
        .entries         = &abbreviation,
        .attributes      = &schema_attribute,
        .count           = 1,
        .attribute_count = 1,
        .section_index   = 3,
        .table_offset    = 17,
        .end_offset      = 128,
        .version         = 5,
        .backing         = tree.backing
    };
    start(1);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_READY);
    CHECK(completed().instances[0].coordinates[0].constant.value == 9);
    CHECK(completed().instances[0].coordinates[0].constant.from_abbreviation);

    start(1);
    while (details.status != TB_SYMBOL_INLINE_DETAILS_SCHEMA) {
        if (details.status == TB_SYMBOL_INLINE_DETAILS_GRAPH)
            supply();
        CHECK(tb_symbol_inline_details_step(&details, 1) <= TB_SYMBOL_INLINE_DETAILS_SCHEMA);
    }
    ++schema.table_offset;
    CHECK(tb_symbol_inline_details_schema(&details, &schema) == TB_SYMBOL_INLINE_DETAILS_INVALID);
    CHECK(details.status == TB_SYMBOL_INLINE_DETAILS_SCHEMA);
    CHECK(tb_symbol_inline_details_schema(&details, NULL) == TB_SYMBOL_INLINE_DETAILS_DEPENDENCY);
}


static void check_lifetime(void)
{
    fixture(2);
    start(2);
    TbSymbolInlineDetails_t old = details;
    TbSymbolInlineDetail_t old_arena[600];
    memcpy(old_arena, arena, sizeof(arena));
    CHECK(tb_symbol_inline_details_init(&details, units, tree, 1, arena, 2) ==
          TB_SYMBOL_INLINE_DETAILS_INVALID);
    CHECK(tb_symbol_inline_details_init(&details, units, tree, 0, arena, SIZE_MAX) ==
          TB_SYMBOL_INLINE_DETAILS_INVALID);
    CHECK(!memcmp(&old, &details, sizeof(old)) && !memcmp(old_arena, arena, sizeof(arena)));
    CHECK(tb_symbol_inline_details_step(&details, 0) == TB_SYMBOL_INLINE_DETAILS_INVALID);
    CHECK(tb_symbol_inline_details_step(&details, 257) == TB_SYMBOL_INLINE_DETAILS_INVALID);
    CHECK(run(1) == TB_SYMBOL_INLINE_DETAILS_READY);
    uint64_t total = tb_symbol_inline_details_progress(&details);
    for (uint64_t prefix = 0; prefix <= total; ++prefix) {
        start(2);
        for (uint64_t step = 0; step < prefix; ++step) {
            if (details.status == TB_SYMBOL_INLINE_DETAILS_GRAPH)
                supply();
            (void)tb_symbol_inline_details_step(&details, 1);
        }
        memcpy(old_arena, arena, sizeof(arena));
        tb_symbol_inline_details_discard(&details);
        CHECK(!memcmp(old_arena, arena, sizeof(arena)));
        CHECK(tb_symbol_inline_details_progress(&details) == 0);
        TbSymbolInlineDetailsView_t view;
        memset(&view, 0xa5, sizeof(view));
        unsigned char saved[sizeof(view)];
        memcpy(saved, &view, sizeof(view));
        CHECK(!tb_symbol_inline_details_view(&details, &view));
        CHECK(!memcmp(saved, &view, sizeof(view)));
    }

    start(2);
    details.progress = UINT64_MAX;
    CHECK(tb_symbol_inline_details_step(&details, 1) == TB_SYMBOL_INLINE_DETAILS_INVALID);
}


int main(void)
{
    check_inventory();
    check_failures();
    check_supplies();
    check_schema();
    check_lifetime();

    puts("PASS complete physical inline inventories, direct call sites, dependencies and cancellation");
    return 0;
}
