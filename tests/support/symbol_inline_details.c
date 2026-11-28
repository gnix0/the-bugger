#include "symbol_inline_details.h"
#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct InlineCapture_t {
    struct InlineCapture_t *next;
    size_t                  bytes;
    TbSymbolGraphNode_t      nodes[];
} InlineCapture_t;

typedef struct {
    TbSymbolInlineDetails_t details;
    TbSymbolGraph_t         graph;
    TbSymbolGraphNode_t     scratch[64];
} InlineWork_t;

static uint64_t inventories;
static uint64_t instances;
static uint64_t call_coordinates;
static uint64_t partials;
static uint64_t unavailable;
static uint64_t exhausted;

static TbSymbolGraphStatus_e build_graph(InlineWork_t *work, TbSymbolReferenceTarget_t root,
                                         TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                                         uint64_t unit_index)
{
    TbSymbolGraphStatus_e status = tb_symbol_graph_init(&work->graph, root, work->scratch, 64);
    CHECK(status == TB_SYMBOL_GRAPH_SOURCE);
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&work->graph, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE) {
                bool available = need.record.section_index == tree.section_index &&
                                 need.record.unit_offset == tree.unit_offset;
                (void)tb_symbol_graph_source(&work->graph, available ? &units : NULL,
                                             available ? &tree : NULL, unit_index);
            } else {
                TbSymbolRelationNeed_t target;
                TbSymbolRelationStatus_e result = tb_symbol_relation_need(need.source, &units, &target);
                bool available = result == TB_SYMBOL_RELATION_READY &&
                                 target.target.section_index == tree.section_index &&
                                 target.target.unit_offset == tree.unit_offset;
                (void)tb_symbol_graph_target(&work->graph, &units, available ? &tree : NULL);
            }
        }
        status = tb_symbol_graph_step(&work->graph, 31);
        if (status != TB_SYMBOL_GRAPH_PENDING && status != TB_SYMBOL_GRAPH_SOURCE &&
            status != TB_SYMBOL_GRAPH_TARGET) {
            return status;
        }
    }
    CHECK(false);
    return TB_SYMBOL_GRAPH_INVALID;
}


static void check_coordinates(const TbSymbolInlineDetail_t *instance, TbSymbolDiesView_t tree,
                               TbSymbolAbbrevView_t schema)
{
    const TbSymbolDieEntry_t *entry = &tree.entries[instance->record.entry_index];
    for (unsigned field = 0; field < TB_SYMBOL_INLINE_COORDINATES; ++field) {
        uint64_t name = field == 0 ? 0x58 : field == 1 ? 0x59 : 0x57;
        const TbSymbolDieAttribute_t *found = NULL;
        uint64_t ordinal = UINT64_MAX;
        for (uint64_t i = 0; i < entry->attribute_count; ++i) {
            uint64_t index = entry->first_attribute + i;
            if (tree.attributes[index].name == name) {
                CHECK(!found);
                found   = &tree.attributes[index];
                ordinal = index;
            }
        }

        const TbSymbolInlineCoordinate_t *coordinate = &instance->coordinates[field];
        CHECK(coordinate->selection.name == name);
        CHECK(coordinate->selection.record_offset == entry->offset);
        if (!found) {
            CHECK(coordinate->status == TB_SYMBOL_CONSTANT_ABSENT);
            continue;
        }
        CHECK(coordinate->selection.matches == 1 && coordinate->selection.first_index == ordinal);
        CHECK(coordinate->status == TB_SYMBOL_CONSTANT_FOUND);
        uint64_t expected = found->value.bits;
        if (found->value.form == TB_SYMBOL_FORM_IMPLICIT_CONST && !found->value.indirect) {
            CHECK(found->schema_attribute < schema.attribute_count);
            const TbSymbolAbbrevAttribute_t *value = &schema.attributes[found->schema_attribute];
            CHECK(value->implicit_fits && value->implicit_value >= 0);
            expected = (uint64_t)value->implicit_value;
            CHECK(coordinate->constant.from_abbreviation);
        }
        CHECK(coordinate->constant.value == expected);
        ++call_coordinates;
    }
}


void test_symbol_inline_details_run(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                                    TbSymbolAbbrevView_t schema, uint64_t unit_index,
                                    TbSymbolBudget_t *budget)
{
    size_t before = budget->used;
    size_t count = 0;
    for (uint64_t i = 0; i < tree.count; ++i)
        count += tree.entries[i].tag == 0x1d;

    CHECK(count <= SIZE_MAX / sizeof(TbSymbolInlineDetail_t));
    CHECK(tb_symbol_budget_reserve(budget, sizeof(InlineWork_t)));
    InlineWork_t *work = calloc(1, sizeof(*work));
    CHECK(work);
    /* Keep construction space for retained origin graphs. A whole-unit inline
     * inventory can exceed the unchanged harness allowance during self-inspection;
     * exercise the explicit resource outcome rather than expanding that budget. */
    size_t capacity = (budget->limit - budget->used) / 2 / sizeof(TbSymbolInlineDetail_t);
    if (capacity > count)
        capacity = count;
    size_t arena_bytes = (capacity ? capacity : 1) * sizeof(TbSymbolInlineDetail_t);
    CHECK(tb_symbol_budget_reserve(budget, arena_bytes));
    TbSymbolInlineDetail_t *arena = calloc(capacity ? capacity : 1, sizeof(*arena));
    CHECK(arena);
    InlineCapture_t *captures = NULL;
    TbSymbolInlineDetailsStatus_e status =
        tb_symbol_inline_details_init(&work->details, units, tree, unit_index, arena, capacity);
    CHECK(status == TB_SYMBOL_INLINE_DETAILS_PENDING);

    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolInlineDetailsView_t unchecked;
        CHECK(!tb_symbol_inline_details_view(&work->details, &unchecked));
        TbSymbolReferenceTarget_t need;
        if (tb_symbol_inline_details_need(&work->details, &need)) {
            TbSymbolGraphStatus_e child = build_graph(work, need, units, tree, unit_index);
            TbSymbolGraphView_t graph;
            TbSymbolGraphFailure_t failure;
            const TbSymbolGraphFailure_t *diagnostic = NULL;
            if (child == TB_SYMBOL_GRAPH_READY) {
                CHECK(tb_symbol_graph_view(&work->graph, &graph));
                CHECK(graph.count <= (SIZE_MAX - sizeof(InlineCapture_t)) / sizeof(*graph.nodes));
                size_t bytes = sizeof(InlineCapture_t) + graph.count * sizeof(*graph.nodes);
                if (tb_symbol_budget_reserve(budget, bytes)) {
                    InlineCapture_t *copy = malloc(bytes);
                    CHECK(copy);
                    copy->next  = captures;
                    copy->bytes = bytes;
                    memcpy(copy->nodes, graph.nodes, graph.count * sizeof(*graph.nodes));
                    captures    = copy;
                    graph.nodes = copy->nodes;
                } else {
                    child = TB_SYMBOL_GRAPH_RESOURCE;
                }
            } else {
                CHECK(tb_symbol_graph_failure(&work->graph, &failure));
                diagnostic = &failure;
            }
            status = tb_symbol_inline_details_graph(&work->details, need, child,
                child == TB_SYMBOL_GRAPH_READY ? &graph : NULL, diagnostic);
        } else if (status == TB_SYMBOL_INLINE_DETAILS_SCHEMA) {
            status = tb_symbol_inline_details_schema(&work->details, &schema);
        }

        status = tb_symbol_inline_details_step(&work->details, 31);
        if (status != TB_SYMBOL_INLINE_DETAILS_PENDING && status != TB_SYMBOL_INLINE_DETAILS_GRAPH &&
            status != TB_SYMBOL_INLINE_DETAILS_SCHEMA) {
            break;
        }
    }

    if (status == TB_SYMBOL_INLINE_DETAILS_READY || status == TB_SYMBOL_INLINE_DETAILS_PARTIAL) {
        TbSymbolInlineDetailsView_t view;
        CHECK(tb_symbol_inline_details_view(&work->details, &view));
        CHECK(view.count == count);
        size_t ordinal = 0;
        for (uint64_t i = 0; i < tree.count; ++i) {
            if (tree.entries[i].tag != 0x1d)
                continue;
            const TbSymbolInlineDetail_t *instance = &view.instances[ordinal++];
            CHECK(instance->record.entry_index == i &&
                  instance->record.record_offset == tree.entries[i].offset);
            CHECK(instance->record.section_index == tree.section_index);
            CHECK(instance->parent.entry_index == tree.entries[i].parent);
            CHECK(instance->parent.record_offset == tree.entries[tree.entries[i].parent].offset);
            CHECK(instance->relationships.nodes[0].record.record_offset == instance->record.record_offset);
            CHECK(instance->origin_available);
            const TbSymbolRelationEdge_t *origin =
                &instance->relationships.nodes[0].edges[TB_SYMBOL_RELATION_ORIGIN];
            CHECK(origin->source.field.name == 0x31);
            check_coordinates(instance, tree, schema);
        }
        ++inventories;
        instances += view.count;
        partials += status == TB_SYMBOL_INLINE_DETAILS_PARTIAL;
    } else {
        CHECK(status == TB_SYMBOL_INLINE_DETAILS_DEPENDENCY ||
              status == TB_SYMBOL_INLINE_DETAILS_UNSUPPORTED ||
              status == TB_SYMBOL_INLINE_DETAILS_RESOURCE);
        TbSymbolInlineDetailsFailure_t failure;
        CHECK(tb_symbol_inline_details_failure(&work->details, &failure));
        CHECK(failure.record_available);
        TbSymbolInlineDetailsView_t unchecked;
        CHECK(!tb_symbol_inline_details_view(&work->details, &unchecked));
        if (status == TB_SYMBOL_INLINE_DETAILS_RESOURCE)
            ++exhausted;
        else
            ++unavailable;
    }

    tb_symbol_inline_details_discard(&work->details);
    while (captures) {
        InlineCapture_t *next = captures->next;
        size_t bytes = captures->bytes;
        free(captures);
        CHECK(tb_symbol_budget_release(budget, bytes));
        captures = next;
    }
    free(arena);
    CHECK(tb_symbol_budget_release(budget, arena_bytes));
    free(work);
    CHECK(tb_symbol_budget_release(budget, sizeof(*work)));
    CHECK(budget->used == before);
}


void test_symbol_inline_details_report(void)
{
    CHECK(inventories && instances && call_coordinates);
    printf("PASS native inline details: %llu inventories, %llu instances, %llu call coordinates, "
           "%llu partial, %llu unavailable, %llu exhausted\n",
           (unsigned long long)inventories, (unsigned long long)instances,
           (unsigned long long)call_coordinates, (unsigned long long)partials,
           (unsigned long long)unavailable, (unsigned long long)exhausted);
}
