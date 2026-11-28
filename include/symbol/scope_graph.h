#ifndef THE_BUGGER_SYMBOL_SCOPE_GRAPH_H
#define THE_BUGGER_SYMBOL_SCOPE_GRAPH_H

#include "symbol/context.h"

typedef enum {
    TB_SYMBOL_SCOPE_PENDING,
    TB_SYMBOL_SCOPE_SOURCE,
    TB_SYMBOL_SCOPE_READY,
    TB_SYMBOL_SCOPE_CYCLE,
    TB_SYMBOL_SCOPE_CONFLICT,
    TB_SYMBOL_SCOPE_DEPENDENCY,
    TB_SYMBOL_SCOPE_RESOURCE,
    TB_SYMBOL_SCOPE_INVALID
} TbSymbolScopeStatus_e;

typedef struct {
    size_t anchor_index;
    size_t target_node;
} TbSymbolScopeEdge_t;

typedef struct {
    TbSymbolReferenceTarget_t record;
    TbSymbolGraphContext_t    context;
    TbSymbolGraphView_t       relationships;
    TbSymbolContextView_t     contexts;
    size_t                   traversal_parent;
    size_t                   first_edge;
    size_t                   next_edge;
    bool                     context_available;
    bool                     complete;
} TbSymbolScopeNode_t;

typedef struct {
    const TbSymbolScopeNode_t *nodes;
    size_t                    node_count;
    const TbSymbolScopeEdge_t *edges;
    size_t                    edge_count;
} TbSymbolScopeView_t;

typedef struct {
    TbSymbolReferenceTarget_t record;
    TbSymbolGraphContext_t    context;
    bool                     context_available;
} TbSymbolScopeNeed_t;

/* Failure coordinates refer to candidate nodes/anchors, not a published view.
 * anchor_index/previous_node are SIZE_MAX when no particular anchor/prior node
 * applies. record identifies a rejected terminal or a requested parent where
 * available, otherwise the current dependency root. */
typedef struct {
    size_t                   source_node;
    size_t                   anchor_index;
    size_t                   previous_node;
    TbSymbolReferenceTarget_t record;
} TbSymbolScopeFailure_t;

/* Private state; caller owns/accounts all state and both arena capacities.
 * Every supplied successful relationship/context view remains borrowed, pinned
 * and unchanged through discard. No allocation, I/O, discovery or recursion.
 * Outer job owns captured revisions, cancellation and absolute deadlines. */
typedef struct {
    TbSymbolScopeNode_t   *nodes;
    TbSymbolScopeEdge_t   *edges;
    size_t                node_capacity;
    size_t                edge_capacity;
    size_t                node_count;
    size_t                edge_count;
    size_t                active;
    size_t                scan;
    size_t                anchor_scan;
    size_t                lookup;
    size_t                filling;
    uint64_t              progress;
    unsigned              phase;
    TbSymbolScopeStatus_e  status;
    TbSymbolScopeFailure_t failure;
    bool                  initialized;
} TbSymbolScope_t;

/* Positive, representable arena capacities; failed initialization leaves state
 * and both arenas unchanged. No production capacity default. */
TbSymbolScopeStatus_e tb_symbol_scope_init(TbSymbolScope_t *scope, TbSymbolReferenceTarget_t root,
                                          TbSymbolScopeNode_t *nodes, size_t node_capacity,
                                          TbSymbolScopeEdge_t *edges, size_t edge_capacity);
/* 1..256 bounded actions; linear lookup can make total work quadratic. Invalid
 * arguments preserve state. Unit boundaries stop traversal; no filename lookup. */
TbSymbolScopeStatus_e tb_symbol_scope_step(TbSymbolScope_t *scope, size_t max_steps);
bool tb_symbol_scope_need(const TbSymbolScope_t *scope, TbSymbolScopeNeed_t *need);
/* NULL explicitly reports missing dependencies. Wrong root or captured context
 * preserves the request. Accepted candidates receive bounded complete validation
 * before traversal. No independently owned evidence or semantic names follow. */
TbSymbolScopeStatus_e tb_symbol_scope_source(TbSymbolScope_t *scope,
                                            const TbSymbolGraphView_t *relationships,
                                            const TbSymbolContextView_t *contexts);
/* READY only. Every context anchor has one edge; target_node == SIZE_MAX only
 * for typed unit boundaries. Shared completed physical records are reused;
 * distinct records never merge. No language-scope equivalence, qualified-name
 * agreement, nonlocality, runtime binding or access authority follows. */
bool tb_symbol_scope_view(const TbSymbolScope_t *scope, TbSymbolScopeView_t *view);
bool tb_symbol_scope_failure(const TbSymbolScope_t *scope, TbSymbolScopeFailure_t *failure);
uint64_t tb_symbol_scope_progress(const TbSymbolScope_t *scope);

#endif // THE_BUGGER_SYMBOL_SCOPE_GRAPH_H
