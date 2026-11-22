#ifndef THE_BUGGER_SYMBOL_PARAMETER_LINKS_H
#define THE_BUGGER_SYMBOL_PARAMETER_LINKS_H

#include "symbol/graph.h"
#include "symbol/parameters.h"

typedef enum {
    TB_SYMBOL_PARAMETER_LINKS_PENDING,
    TB_SYMBOL_PARAMETER_LINKS_GRAPH,
    TB_SYMBOL_PARAMETER_LINKS_READY,
    TB_SYMBOL_PARAMETER_LINKS_CONFLICT,
    TB_SYMBOL_PARAMETER_LINKS_DEPENDENCY,
    TB_SYMBOL_PARAMETER_LINKS_RESOURCE,
    TB_SYMBOL_PARAMETER_LINKS_INVALID
} TbSymbolParameterLinksStatus_e;

typedef struct {
    size_t source_scope;
    size_t source_parameter;
    size_t target_scope;
    size_t target_parameter;
    size_t graph_node;
} TbSymbolParameterLink_t;

typedef struct {
    TbSymbolGraphView_t graph;
    size_t              scope_index;
    size_t              parameter_index;
    size_t              first_link;
    size_t              link_count;
} TbSymbolParameterLinkFact_t;

typedef struct {
    TbSymbolGraphView_t                callables;
    const TbSymbolParametersView_t    *scopes;
    const TbSymbolParameterLinkFact_t *facts;
    const TbSymbolParameterLink_t     *links;
    size_t                             fact_count;
    size_t                             link_count;
} TbSymbolParameterLinksView_t;

/* graph_node is the supplied-graph cursor (possibly at its end), or SIZE_MAX
 * before graph supply. previous_link is SIZE_MAX without a prior conflicting
 * link. scope/parameter indices identify the cursor. */
typedef struct {
    TbSymbolParameterLinksStatus_e status;
    size_t                         scope_index;
    size_t                         parameter_index;
    size_t                         graph_node;
    size_t                         previous_link;
} TbSymbolParameterLinksFailure_t;

/* Caller owns/accounts state, both full arenas and the full reachability
 * workspace. Every callable node has one completed direct inventory at the same
 * index in scopes. Borrow all inputs,
 * arenas and supplied completed parameter graphs unchanged through discard.
 * State, arenas and workspace must not overlap one another or borrowed inputs.
 * No allocation, I/O, discovery, recursion or production capacity default.
 * Outer job owns revisions, cancellation, interests and absolute deadlines. */
typedef struct {
    TbSymbolGraphView_t              callables;
    const TbSymbolParametersView_t  *scopes;
    TbSymbolParameterLinkFact_t     *facts;
    TbSymbolParameterLink_t         *links;
    TbSymbolGraphView_t              pending;
    bool                            *reachable;
    TbSymbolParameterLinksFailure_t  failure;
    TbSymbolParameterLinksStatus_e   status;
    uint64_t                         progress;
    size_t                           fact_capacity;
    size_t                           link_capacity;
    size_t                           fact_count;
    size_t                           link_count;
    size_t                           scope_index;
    size_t                           parameter_index;
    size_t                           graph_node;
    size_t                           find_scope;
    size_t                           find_parameter;
    size_t                           previous_link;
    size_t                           candidate_scope;
    size_t                           candidate_parameter;
    size_t                           reach_clear;
    size_t                           reach_node;
    size_t                           reach_find;
    unsigned                         reach_edge;
    bool                             reach_changed;
    unsigned                         phase;
    bool                             initialized;
} TbSymbolParameterLinks_t;

/* Both arenas are nonnull even at zero capacity. Checked rejection preserves
 * old state, full arenas and reachability workspace. The workspace covers
 * callables.count slots. Inputs must come from completed child accessors. */
TbSymbolParameterLinksStatus_e tb_symbol_parameter_links_init(
    TbSymbolParameterLinks_t *state, TbSymbolGraphView_t callables,
    const TbSymbolParametersView_t *scopes, size_t scope_count,
    TbSymbolParameterLinkFact_t *facts, size_t fact_capacity,
    TbSymbolParameterLink_t *links, size_t link_capacity,
    bool *reachable, size_t reachable_capacity);
/* 1..256 bounded scope, parameter, target or duplicate-link checks per step.
 * Linear target/duplicate searches can make total work quadratic or worse;
 * outer deadlines and measured production limits remain required. */
TbSymbolParameterLinksStatus_e tb_symbol_parameter_links_step(TbSymbolParameterLinks_t *state,
                                                              size_t max_steps);
/* GRAPH only. Indices select an exact physical formal in the captured scope. */
bool tb_symbol_parameter_links_need(const TbSymbolParameterLinks_t *state, size_t *scope_index,
                                     size_t *parameter_index, TbSymbolReferenceTarget_t *record);
/* Wrong root/context preserves GRAPH. NULL finalizes DEPENDENCY. All successful
 * graph nodes must be formal parameters in the captured callable inventories. */
TbSymbolParameterLinksStatus_e tb_symbol_parameter_links_graph(TbSymbolParameterLinks_t *state,
                                                               const TbSymbolGraphView_t *graph);
/* READY only, after every inventory and supplied graph has been examined.
 * Each target callable is reachable from its source through captured callable
 * relationships. Within each source/target inventory pair, explicit links are
 * one-to-one; contradictory alternatives fail without a successful prefix.
 * Links retain exact physical derivations; unspecified markers stay in their
 * original inventories. No positional/name/type matching, source-list selection,
 * list concatenation, type equivalence or complete-signature claim follows. */
bool tb_symbol_parameter_links_view(const TbSymbolParameterLinks_t *state,
                                     TbSymbolParameterLinksView_t *view);
bool tb_symbol_parameter_links_failure(const TbSymbolParameterLinks_t *state,
                                        TbSymbolParameterLinksFailure_t *failure);
uint64_t tb_symbol_parameter_links_progress(const TbSymbolParameterLinks_t *state);
void tb_symbol_parameter_links_discard(TbSymbolParameterLinks_t *state);

#endif // THE_BUGGER_SYMBOL_PARAMETER_LINKS_H
