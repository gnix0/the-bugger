#include "symbol/type_recipe.h"
#include "../support/check.h"

#include <string.h>

#define NODES 512

static TbSymbolGraphNode_t              graphs[NODES];
static TbSymbolInheritSlot_t            slots[NODES];
static TbSymbolInheritedReferenceFact_t references[NODES];
static TbSymbolTypeRecipeNode_t         nodes[NODES];
static TbSymbolQualifiedNode_t          qualified[NODES];
static TbSymbolScopeNode_t              scopes[NODES];
static TbSymbolTypeRecipe_t             recipe;
static size_t                           count;

/* Authored completed-child views exercise coordinator behavior. They do not
 * replace encoded DWARF fixtures or actual graph/name/reference validation. */
static void fixture(size_t length)
{
    CHECK(length && length <= NODES);
    count = length;
    memset(graphs, 0, sizeof(graphs));
    memset(slots, 0, sizeof(slots));
    memset(references, 0, sizeof(references));
    memset(qualified, 0, sizeof(qualified));
    memset(scopes, 0, sizeof(scopes));
    for (size_t i = 0; i < count; ++i) {
        graphs[i].record = (TbSymbolReferenceTarget_t){
            .backing       = {
                .key   = {
                    .device = 1,
                    .inode  = 2
                },
                .bytes = 1024 * 1024
            },
            .section_index = 1,
            .record_offset = 12 + 16 * i,
            .entry_index   = i
        };
        graphs[i].context.tag     = i + 1 == count ? 0x24 : 0x0f;
        graphs[i].context.version = 5;
        graphs[i].complete        = true;
        graphs[i].parent          = SIZE_MAX;

        scopes[i].record        = graphs[i].record;
        scopes[i].context       = graphs[i].context;
        scopes[i].relationships = (TbSymbolGraphView_t){
            .nodes = &graphs[i],
            .count = 1
        };
        scopes[i].complete = true;

        qualified[i].complete    = true;
        qualified[i].parent_node = SIZE_MAX;
        if (i + 1 < count) {
            references[i].node_index = 0;
            references[i].target     = graphs[i].record;
            references[i].target.record_offset += 16;
            ++references[i].target.entry_index;
            slots[i].visit = TB_SYMBOL_INHERIT_OWNER;
        }
    }
    CHECK(tb_symbol_type_recipe_init(&recipe, (TbSymbolGraphView_t){
        .nodes = graphs,
        .count = 1
    }, nodes, NODES) == TB_SYMBOL_TYPE_PENDING);
}


static TbSymbolTypeRecipeStatus_e run(void)
{
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolReferenceTarget_t need;
        if (tb_symbol_type_recipe_need(&recipe, &need)) {
            size_t index = (size_t)need.entry_index;
            CHECK(index < count);
            TbSymbolGraphView_t graph = {
                .nodes = &graphs[index],
                .count = 1
            };
            if (recipe.status == TB_SYMBOL_TYPE_GRAPH) {
                CHECK(tb_symbol_type_recipe_graph(&recipe, &graph) == TB_SYMBOL_TYPE_PENDING);
            } else if (recipe.status == TB_SYMBOL_TYPE_NAME) {
                TbSymbolQualifiedView_t name = {
                    .selected = need,
                    .scope    = {
                        .nodes      = &scopes[index],
                        .node_count = 1
                    },
                    .nodes    = &qualified[index],
                    .count    = 1
                };
                CHECK(tb_symbol_type_recipe_name(&recipe, need, TB_SYMBOL_QUALIFIED_READY, &name) ==
                      TB_SYMBOL_TYPE_READY);
            } else {
                TbSymbolInheritedReferenceView_t refs = {
                    .selected = need,
                    .graph    = graph,
                    .slots    = &slots[index],
                    .facts    = &references[index],
                    .count    = index + 1 < count ? 1 : 0,
                    .name     = TB_SYMBOL_INHERIT_TYPE
                };
                (void)tb_symbol_type_recipe_references(&recipe, need, refs.count
                    ? TB_SYMBOL_INHERITED_REFERENCE_FOUND : TB_SYMBOL_INHERITED_REFERENCE_ABSENT, &refs);
            }
        }
        TbSymbolTypeRecipeStatus_e status = tb_symbol_type_recipe_step(&recipe, 1);
        if (status != TB_SYMBOL_TYPE_PENDING && status != TB_SYMBOL_TYPE_GRAPH &&
            status != TB_SYMBOL_TYPE_NAME && status != TB_SYMBOL_TYPE_REFERENCES)
            return status;
        CHECK(!tb_symbol_type_recipe_view(&recipe, &(TbSymbolTypeRecipeView_t){0}));
    }
    CHECK(false);
    return TB_SYMBOL_TYPE_INVALID;
}


static void check_chain(void)
{
    fixture(512);
    CHECK(run() == TB_SYMBOL_TYPE_READY);
    TbSymbolTypeRecipeView_t view;
    CHECK(tb_symbol_type_recipe_view(&recipe, &view) && view.complete && view.count == 512);
    for (size_t i = 0; i < view.count; ++i) {
        CHECK(view.nodes[i].record.entry_index == i);
        CHECK(view.nodes[i].kind == (i + 1 == view.count ? TB_SYMBOL_TYPE_BASE : TB_SYMBOL_TYPE_POINTER));
        CHECK(view.nodes[i].name_available == (i + 1 == view.count));
        CHECK(view.nodes[i].references_available == (i + 1 != view.count));
    }
}


static void check_partial_and_cycle(void)
{
    fixture(1);
    graphs[0].context.tag = 0x01;
    CHECK(run() == TB_SYMBOL_TYPE_PARTIAL);
    TbSymbolTypeRecipeView_t view;
    CHECK(tb_symbol_type_recipe_view(&recipe, &view));
    CHECK(!view.complete && view.count == 1 && view.partial == TB_SYMBOL_TYPE_UNSUPPORTED_CONSTRUCTOR);
    fixture(1);
    graphs[0].context.tag = 0x0f;
    CHECK(run() == TB_SYMBOL_TYPE_PARTIAL);
    CHECK(tb_symbol_type_recipe_view(&recipe, &view) && view.partial == TB_SYMBOL_TYPE_MISSING_TYPE);
    CHECK(view.nodes[0].references_available && !view.nodes[0].references.count);
    fixture(2);
    references[0].target = graphs[0].record;
    CHECK(run() == TB_SYMBOL_TYPE_CONFLICT);
    CHECK(!tb_symbol_type_recipe_view(&recipe, &view));
    TbSymbolTypeRecipeFailure_t failure;
    CHECK(tb_symbol_type_recipe_failure(&recipe, &failure) && failure.other_node_index == 0);
}


static void check_failures(void)
{
    fixture(2);
    CHECK(tb_symbol_type_recipe_init(&recipe, (TbSymbolGraphView_t){
        .nodes = graphs,
        .count = 1
    }, nodes, 1) == TB_SYMBOL_TYPE_PENDING);
    CHECK(run() == TB_SYMBOL_TYPE_RESOURCE);
    CHECK(!tb_symbol_type_recipe_view(&recipe, &(TbSymbolTypeRecipeView_t){0}));
    fixture(1);
    CHECK(tb_symbol_type_recipe_step(&recipe, 1) == TB_SYMBOL_TYPE_NAME);
    TbSymbolReferenceTarget_t need;
    CHECK(tb_symbol_type_recipe_need(&recipe, &need));
    unsigned char before[sizeof(recipe)];
    memcpy(before, &recipe, sizeof(before));
    TbSymbolReferenceTarget_t wrong = need;
    ++wrong.backing.key.inode;
    CHECK(tb_symbol_type_recipe_name(&recipe, wrong, TB_SYMBOL_QUALIFIED_DEPENDENCY, NULL) ==
          TB_SYMBOL_TYPE_INVALID);
    CHECK(!memcmp(before, &recipe, sizeof(before)));
    CHECK(tb_symbol_type_recipe_name(&recipe, need, TB_SYMBOL_QUALIFIED_DEPENDENCY, NULL) ==
          TB_SYMBOL_TYPE_DEPENDENCY);
    CHECK(!tb_symbol_type_recipe_view(&recipe, &(TbSymbolTypeRecipeView_t){0}));
    fixture(1);
    recipe.progress = UINT64_MAX;
    CHECK(tb_symbol_type_recipe_step(&recipe, 1) == TB_SYMBOL_TYPE_INVALID);
}


static void check_reference_alternatives(void)
{
    fixture(5);
    TbSymbolGraphView_t graph = {
        .nodes = graphs,
        .count = 3
    };
    TbSymbolInheritSlot_t            owners[3] = {0};
    TbSymbolInheritedReferenceFact_t facts[2] = {
        {
            .node_index = 1,
            .target     = graphs[3].record
        },
        {
            .node_index = 2,
            .target     = graphs[4].record
        }
    };
    graphs[0].present = (1U << TB_SYMBOL_RELATION_SPECIFICATION) | (1U << TB_SYMBOL_RELATION_ORIGIN);
    graphs[0].edges[TB_SYMBOL_RELATION_SPECIFICATION].target = graphs[1].record;
    graphs[0].edges[TB_SYMBOL_RELATION_ORIGIN].target = graphs[2].record;
    graphs[1].parent = 0;
    graphs[2].parent = 0;
    owners[0].visit = TB_SYMBOL_INHERIT_EXAMINED;
    owners[1].visit = TB_SYMBOL_INHERIT_OWNER;
    owners[2].visit = TB_SYMBOL_INHERIT_OWNER;
    CHECK(tb_symbol_type_recipe_init(&recipe, graph, nodes, NODES) == TB_SYMBOL_TYPE_PENDING);
    CHECK(tb_symbol_type_recipe_step(&recipe, 1) == TB_SYMBOL_TYPE_REFERENCES);
    TbSymbolInheritedReferenceView_t refs = {
        .selected = graphs[0].record,
        .graph    = graph,
        .slots    = owners,
        .facts    = facts,
        .count    = 2,
        .name     = TB_SYMBOL_INHERIT_TYPE
    };
    CHECK(tb_symbol_type_recipe_references(&recipe, graphs[0].record,
        TB_SYMBOL_INHERITED_REFERENCE_FOUND, &refs) == TB_SYMBOL_TYPE_PENDING);
    while (tb_symbol_type_recipe_step(&recipe, 1) == TB_SYMBOL_TYPE_PENDING)
        CHECK(!tb_symbol_type_recipe_view(&recipe, &(TbSymbolTypeRecipeView_t){0}));
    TbSymbolTypeRecipeView_t view;
    CHECK(tb_symbol_type_recipe_view(&recipe, &view) && !view.complete);
    CHECK(view.partial == TB_SYMBOL_TYPE_ALTERNATIVE_TARGETS && view.count == 1);
    CHECK(view.nodes[0].references.facts == facts && view.nodes[0].references.count == 2);
    tb_symbol_type_recipe_discard(&recipe);
}


static void check_wrong_name_context(void)
{
    fixture(1);
    CHECK(tb_symbol_type_recipe_step(&recipe, 1) == TB_SYMBOL_TYPE_NAME);
    TbSymbolQualifiedView_t name = {
        .selected = graphs[0].record,
        .scope    = {
            .nodes      = scopes,
            .node_count = 1
        },
        .nodes    = qualified,
        .count    = 1
    };
    ++scopes[0].context.abbrev_offset;
    unsigned char before[sizeof(recipe)];
    memcpy(before, &recipe, sizeof(before));
    CHECK(tb_symbol_type_recipe_name(&recipe, graphs[0].record, TB_SYMBOL_QUALIFIED_READY, &name) ==
          TB_SYMBOL_TYPE_INVALID);
    CHECK(!memcmp(before, &recipe, sizeof(before)));
    --scopes[0].context.abbrev_offset;
    CHECK(run() == TB_SYMBOL_TYPE_READY);
    memcpy(before, &recipe, sizeof(before));
    unsigned char previous[sizeof(nodes)];
    memcpy(previous, nodes, sizeof(nodes));
    CHECK(tb_symbol_type_recipe_init(&recipe, (TbSymbolGraphView_t){
        .nodes = graphs,
        .count = 1
    }, nodes, 0) == TB_SYMBOL_TYPE_RESOURCE);
    CHECK(!memcmp(before, &recipe, sizeof(before)));
    CHECK(!memcmp(previous, nodes, sizeof(nodes)));
    CHECK(tb_symbol_type_recipe_step(&recipe, 0) == TB_SYMBOL_TYPE_INVALID);
    CHECK(tb_symbol_type_recipe_step(&recipe, 257) == TB_SYMBOL_TYPE_INVALID);
    CHECK(!memcmp(before, &recipe, sizeof(before)));
    memcpy(previous, nodes, sizeof(nodes));
    tb_symbol_type_recipe_discard(&recipe);
    CHECK(!tb_symbol_type_recipe_view(&recipe, &(TbSymbolTypeRecipeView_t){0}));
    CHECK(!tb_symbol_type_recipe_need(&recipe, &(TbSymbolReferenceTarget_t){0}));
    CHECK(!memcmp(previous, nodes, sizeof(nodes)));
}


static void check_non_type_targets(void)
{
    const uint64_t tags[] = {0x05, 0x11, 0x2e, 0x34, 0x39, 0x41};
    for (size_t i = 0; i < sizeof(tags) / sizeof(*tags); ++i) {
        fixture(1);
        graphs[0].context.tag = tags[i];
        CHECK(run() == TB_SYMBOL_TYPE_MALFORMED);
        CHECK(!tb_symbol_type_recipe_view(&recipe, &(TbSymbolTypeRecipeView_t){0}));
        TbSymbolTypeRecipeFailure_t failure;
        CHECK(tb_symbol_type_recipe_failure(&recipe, &failure));
        CHECK(failure.record.record_offset == graphs[0].record.record_offset && failure.node_index == 0);
    }
    fixture(2);
    graphs[1].context.tag = 0x34;
    CHECK(run() == TB_SYMBOL_TYPE_MALFORMED);
    CHECK(!tb_symbol_type_recipe_view(&recipe, &(TbSymbolTypeRecipeView_t){0}));
    TbSymbolTypeRecipeFailure_t failure;
    CHECK(tb_symbol_type_recipe_failure(&recipe, &failure));
    CHECK(failure.record.record_offset == graphs[1].record.record_offset && failure.node_index == 1);

    fixture(1);
    graphs[0].context.tag = 0x48;
    graphs[0].context.version = 4;
    CHECK(run() == TB_SYMBOL_TYPE_PARTIAL);
    fixture(1);
    graphs[0].context.tag = 0x48;
    CHECK(run() == TB_SYMBOL_TYPE_MALFORMED);
    fixture(1);
    graphs[0].context.tag = 0x4080;
    CHECK(run() == TB_SYMBOL_TYPE_PARTIAL);
}


int main(void)
{
    check_non_type_targets();
    check_chain();
    check_partial_and_cycle();
    check_failures();
    check_reference_alternatives();
    check_wrong_name_context();
    puts("PASS type recipe coordinator: bounded chains, explicit partials and no failure prefix");
    return 0;
}
