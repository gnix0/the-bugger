#include "symbol/parameter_links.h"
#include "../support/check.h"

#include <string.h>

#define SCOPES 3
#define PARAMETERS 520

static TbSymbolGraphNode_t         callables[SCOPES];
static TbSymbolParameter_t         parameters[SCOPES][PARAMETERS];
static TbSymbolParametersView_t    scopes[SCOPES];
static TbSymbolGraphNode_t         graphs[SCOPES][PARAMETERS][3];
static size_t                      graph_counts[SCOPES][PARAMETERS];
static TbSymbolParameterLinkFact_t facts[PARAMETERS];
static TbSymbolParameterLink_t     links[PARAMETERS];
static TbSymbolParameterLinks_t    state;
static size_t                      scope_count;
static bool                        reachable[SCOPES];

/* Authored completed-child views test reconciliation, separately from encoded
 * metadata decoding and the native graph/inventory composition checks. */
static void fixture(void)
{
    memset(callables, 0, sizeof(callables));
    memset(parameters, 0, sizeof(parameters));
    memset(scopes, 0, sizeof(scopes));
    memset(graphs, 0, sizeof(graphs));
    memset(graph_counts, 0, sizeof(graph_counts));
    scope_count = SCOPES;

    for (size_t s = 0; s < SCOPES; ++s) {
        callables[s].record = (TbSymbolReferenceTarget_t) {
            .backing = {
                .key = {
                    .device = 1,
                    .inode  = 2
                },
                .bytes = 1024 * 1024
            },
            .section_index = 1,
            .unit_offset   = 8192 * s,
            .record_offset = 8192 * s + 12,
            .entry_index   = 1
        };
        callables[s].context = (TbSymbolGraphContext_t) {
            .tag            = 0x2e,
            .version        = 5,
            .unit_kind      = TB_SYMBOL_DWARF_COMPILE,
            .abbrev_section = 2,
            .abbrev_offset  = 64 * s,
            .parent_tag     = 0x11
        };
        callables[s].complete = true;
        scopes[s] = (TbSymbolParametersView_t) {
            .selected   = callables[s].record,
            .parameters = parameters[s],
            .count      = 2
        };

        for (size_t p = 0; p < PARAMETERS; ++p) {
            TbSymbolParameter_t *parameter = &parameters[s][p];
            parameter->record = callables[s].record;
            parameter->record.record_offset += 8 * (p + 1);
            parameter->record.entry_index += p + 1;
            parameter->direct = (TbSymbolEntityView_t) {
                .backing        = parameter->record.backing,
                .section_index  = parameter->record.section_index,
                .unit_offset    = parameter->record.unit_offset,
                .record_offset  = parameter->record.record_offset,
                .parent_index   = callables[s].record.entry_index,
                .tag            = 0x05,
                .abbrev_section = 2,
                .abbrev_offset  = 64 * s
            };
            graphs[s][p][0] = (TbSymbolGraphNode_t) {
                .record   = parameter->record,
                .context  = callables[s].context,
                .parent   = SIZE_MAX,
                .complete = true
            };
            graphs[s][p][0].context.tag           = 0x05;
            graphs[s][p][0].context.parent_record = callables[s].record.record_offset;
            graphs[s][p][0].context.parent_tag    = callables[s].context.tag;
            graph_counts[s][p] = 1;
        }
    }

    callables[0].present = 1U << TB_SYMBOL_RELATION_ORIGIN;
    callables[0].edges[TB_SYMBOL_RELATION_ORIGIN].target = callables[1].record;
    callables[1].present = 1U << TB_SYMBOL_RELATION_SPECIFICATION;
    callables[1].edges[TB_SYMBOL_RELATION_SPECIFICATION].target = callables[2].record;

    graphs[0][0][1] = graphs[1][0][0];
    graphs[0][0][2] = graphs[2][0][0];
    graph_counts[0][0] = 3;
    graphs[0][1][1] = graphs[2][1][0];
    graph_counts[0][1] = 2;
    graphs[1][0][1] = graphs[2][0][0];
    graph_counts[1][0] = 2;
}


static TbSymbolParameterLinksStatus_e init(size_t fact_capacity, size_t link_capacity)
{
    return tb_symbol_parameter_links_init(&state, (TbSymbolGraphView_t) {
        .nodes = callables,
        .count = scope_count
    }, scopes, scope_count, facts, fact_capacity, links, link_capacity, reachable, SCOPES);
}


static TbSymbolParameterLinksStatus_e run(size_t slice)
{
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        if (state.status == TB_SYMBOL_PARAMETER_LINKS_GRAPH) {
            size_t                    scope_index;
            size_t                    parameter_index;
            TbSymbolReferenceTarget_t record;
            CHECK(tb_symbol_parameter_links_need(&state, &scope_index, &parameter_index, &record));
            CHECK(record.record_offset == parameters[scope_index][parameter_index].record.record_offset);
            CHECK(tb_symbol_parameter_links_graph(&state, &(TbSymbolGraphView_t) {
                .nodes = graphs[scope_index][parameter_index],
                .count = graph_counts[scope_index][parameter_index]
            }) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
        }

        TbSymbolParameterLinksStatus_e status = tb_symbol_parameter_links_step(&state, slice);
        if (status != TB_SYMBOL_PARAMETER_LINKS_PENDING && status != TB_SYMBOL_PARAMETER_LINKS_GRAPH)
            return status;
    }

    CHECK(false);
    return TB_SYMBOL_PARAMETER_LINKS_INVALID;
}


static void correspondence(void)
{
    fixture();
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(1) == TB_SYMBOL_PARAMETER_LINKS_READY);
    TbSymbolParameterLinksView_t view;
    CHECK(tb_symbol_parameter_links_view(&state, &view));
    CHECK(view.scopes == scopes && view.fact_count == 6 && view.link_count == 4);
    CHECK(view.links[0].source_scope == 0 && view.links[0].source_parameter == 0);
    CHECK(view.links[0].target_scope == 1 && view.links[0].target_parameter == 0);
    CHECK(view.links[0].graph_node == 1);
    CHECK(view.links[1].target_scope == 2 && view.links[1].graph_node == 2);
    CHECK(view.links[2].source_parameter == 1 && view.links[2].target_parameter == 1);
    CHECK(view.links[3].source_scope == 1 && view.links[3].target_scope == 2);
    CHECK(view.facts[0].first_link == 0 && view.facts[0].link_count == 2);
    CHECK(view.facts[2].first_link == 3 && view.facts[2].link_count == 1);
    CHECK(view.facts[5].link_count == 0);
    CHECK(!tb_symbol_parameter_links_failure(&state, &(TbSymbolParameterLinksFailure_t) {0}));
}


static void independent_inventories(void)
{
    fixture();
    scopes[0].count = 1;
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(256) == TB_SYMBOL_PARAMETER_LINKS_READY);
    TbSymbolParameterLinksView_t view;
    CHECK(tb_symbol_parameter_links_view(&state, &view));
    CHECK(view.fact_count == 5 && view.link_count == 3 && view.scopes[1].count == 2);

    fixture();
    scopes[2].count = 0;
    for (size_t s = 0; s < SCOPES; ++s) {
        for (size_t p = 0; p < PARAMETERS; ++p)
            graph_counts[s][p] = 1;
    }
    CHECK(init(PARAMETERS, 0) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_LINKS_READY);
    CHECK(tb_symbol_parameter_links_view(&state, &view));
    CHECK(view.fact_count == 4 && !view.link_count && !view.scopes[2].count);

    fixture();
    scope_count = 1;
    callables[0].present = 0;
    scopes[0].count = 512;
    for (size_t p = 0; p < PARAMETERS; ++p)
        graph_counts[0][p] = 1;
    parameters[0][511].unspecified = true;
    parameters[0][511].direct.tag = 0x18;
    CHECK(init(PARAMETERS, 0) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(127) == TB_SYMBOL_PARAMETER_LINKS_READY);
    CHECK(tb_symbol_parameter_links_view(&state, &view));
    CHECK(view.fact_count == 511 && !view.link_count && view.scopes[0].parameters[511].unspecified);
}


static void failures(void)
{
    fixture();
    callables[0].present = 1U << TB_SYMBOL_RELATION_ORIGIN;
    callables[0].edges[TB_SYMBOL_RELATION_ORIGIN].target = callables[1].record;
    callables[1].present = 1U << TB_SYMBOL_RELATION_SPECIFICATION;
    callables[1].edges[TB_SYMBOL_RELATION_SPECIFICATION].target = callables[2].record;
    graphs[1][0][1] = graphs[0][0][0];
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_LINKS_CONFLICT);

    fixture();
    graphs[0][1][1].context.parent_record += 1;
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_LINKS_CONFLICT);
    CHECK(state.link_count == 2);
    CHECK(!tb_symbol_parameter_links_view(&state, &(TbSymbolParameterLinksView_t) {0}));

    fixture();
    graphs[0][1][1] = graphs[1][0][0];
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(3) == TB_SYMBOL_PARAMETER_LINKS_CONFLICT);
    TbSymbolParameterLinksFailure_t failure;
    CHECK(tb_symbol_parameter_links_failure(&state, &failure));
    CHECK(failure.scope_index == 0 && failure.parameter_index == 1 && failure.previous_link == 0);

    fixture();
    graphs[0][0][2] = graphs[1][1][0];
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_LINKS_CONFLICT);
    CHECK(tb_symbol_parameter_links_failure(&state, &failure));
    CHECK(failure.scope_index == 0 && failure.parameter_index == 0 && failure.previous_link == 0);

    fixture();
    CHECK(init(PARAMETERS, 1) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_LINKS_RESOURCE);
    CHECK(!tb_symbol_parameter_links_view(&state, &(TbSymbolParameterLinksView_t) {0}));

    fixture();
    CHECK(init(1, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_LINKS_RESOURCE);

    fixture();
    parameters[1][0].direct.abbrev_offset += 1;
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_LINKS_INVALID && !state.fact_count);

    fixture();
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    state.progress = UINT64_MAX;
    CHECK(tb_symbol_parameter_links_step(&state, 1) == TB_SYMBOL_PARAMETER_LINKS_INVALID);
}


static void pending_and_replacement(void)
{
    fixture();
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    while (state.status == TB_SYMBOL_PARAMETER_LINKS_PENDING)
        (void)tb_symbol_parameter_links_step(&state, 1);
    CHECK(state.status == TB_SYMBOL_PARAMETER_LINKS_GRAPH);
    TbSymbolParameterLinks_t old     = state;
    TbSymbolGraphNode_t      changed = graphs[0][0][0];
    ++changed.context.abbrev_offset;
    CHECK(tb_symbol_parameter_links_graph(&state, &(TbSymbolGraphView_t) {
        .nodes = &changed,
        .count = 1
    }) == TB_SYMBOL_PARAMETER_LINKS_INVALID);
    CHECK(!memcmp(&old, &state, sizeof(state)));
    CHECK(tb_symbol_parameter_links_step(&state, 0) == TB_SYMBOL_PARAMETER_LINKS_INVALID);
    CHECK(tb_symbol_parameter_links_step(&state, 257) == TB_SYMBOL_PARAMETER_LINKS_INVALID);
    CHECK(!memcmp(&old, &state, sizeof(state)));
    CHECK(tb_symbol_parameter_links_graph(&state, NULL) == TB_SYMBOL_PARAMETER_LINKS_DEPENDENCY);
    TbSymbolParameterLinksFailure_t failure;
    CHECK(tb_symbol_parameter_links_failure(&state, &failure));
    CHECK(failure.graph_node == SIZE_MAX && failure.previous_link == SIZE_MAX);

    fixture();
    CHECK(init(PARAMETERS, PARAMETERS) == TB_SYMBOL_PARAMETER_LINKS_PENDING);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_LINKS_READY);
    old = state;
    TbSymbolParameterLinkFact_t old_facts[PARAMETERS];
    TbSymbolParameterLink_t     old_links[PARAMETERS];
    memcpy(old_facts, facts, sizeof(facts));
    memcpy(old_links, links, sizeof(links));
    CHECK(tb_symbol_parameter_links_init(&state, (TbSymbolGraphView_t) {
        .nodes = callables,
        .count = SCOPES
    }, scopes, SCOPES - 1, facts, PARAMETERS, links, PARAMETERS, reachable, SCOPES) ==
        TB_SYMBOL_PARAMETER_LINKS_INVALID);
    CHECK(!memcmp(&old, &state, sizeof(state)));
    CHECK(!memcmp(old_facts, facts, sizeof(facts)) && !memcmp(old_links, links, sizeof(links)));
    bool old_reachable[SCOPES];
    memcpy(old_reachable, reachable, sizeof(reachable));
    CHECK(tb_symbol_parameter_links_init(&state, (TbSymbolGraphView_t) {
        .nodes = callables,
        .count = SCOPES
    }, scopes, SCOPES, facts, PARAMETERS, links, PARAMETERS, reachable, SCOPES - 1) ==
        TB_SYMBOL_PARAMETER_LINKS_RESOURCE);
    CHECK(!memcmp(&old, &state, sizeof(state)));
    CHECK(!memcmp(old_reachable, reachable, sizeof(reachable)));
    CHECK(tb_symbol_parameter_links_init(&state, (TbSymbolGraphView_t) {
        .nodes = callables,
        .count = SCOPES
    }, scopes, SCOPES, facts, SIZE_MAX, links, PARAMETERS, reachable, SCOPES) ==
        TB_SYMBOL_PARAMETER_LINKS_INVALID);
    CHECK(!memcmp(&old, &state, sizeof(state)));
    CHECK(!memcmp(old_facts, facts, sizeof(facts)) && !memcmp(old_links, links, sizeof(links)));
    tb_symbol_parameter_links_discard(&state);
    CHECK(!tb_symbol_parameter_links_view(&state, &(TbSymbolParameterLinksView_t) {0}));
    CHECK(!memcmp(old_facts, facts, sizeof(facts)) && !memcmp(old_links, links, sizeof(links)));
}


int main(void)
{
    correspondence();
    independent_inventories();
    failures();
    pending_and_replacement();

    return 0;
}
