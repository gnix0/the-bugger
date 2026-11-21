#ifndef THE_BUGGER_SYMBOL_CONTEXT_H
#define THE_BUGGER_SYMBOL_CONTEXT_H

#include "symbol/graph.h"

typedef enum {
    TB_SYMBOL_CONTEXT_PENDING,
    TB_SYMBOL_CONTEXT_SOURCE,
    TB_SYMBOL_CONTEXT_READY,
    TB_SYMBOL_CONTEXT_DEPENDENCY,
    TB_SYMBOL_CONTEXT_RESOURCE,
    TB_SYMBOL_CONTEXT_INVALID
} TbSymbolContextStatus_e;

typedef enum {
    TB_SYMBOL_CONTEXT_LEXICAL_PARENT,
    TB_SYMBOL_CONTEXT_UNIT_PARENT,
    TB_SYMBOL_CONTEXT_UNIT_ROOT
} TbSymbolContextAnchorKind_e;

typedef struct {
    size_t                      node_index;
    TbSymbolReferenceTarget_t   record;
    TbSymbolGraphContext_t      context;
    TbSymbolContextAnchorKind_e kind;
    TbSymbolReferenceTarget_t   parent;
    TbSymbolGraphContext_t      parent_context;
    TbSymbolReferenceTarget_t   unit_root;
    TbSymbolUnitEntry_t         unit;
} TbSymbolContextAnchor_t;

typedef struct {
    TbSymbolReferenceTarget_t      selected;
    const TbSymbolContextAnchor_t *anchors;
    size_t                         count;
} TbSymbolContextView_t;

typedef enum {
    TB_SYMBOL_CONTEXT_NONE,
    TB_SYMBOL_CONTEXT_GRAPH,
    TB_SYMBOL_CONTEXT_SOURCE_MISSING,
    TB_SYMBOL_CONTEXT_PROGRESS
} TbSymbolContextFailure_e;

typedef struct {
    TbSymbolContextFailure_e  reason;
    size_t                    node_index;
    TbSymbolReferenceTarget_t record;
} TbSymbolContextFailure_t;

/* Private cursor state. Borrows a complete, unchanged relationship graph and
 * caller-accounted anchor storage through discard. Supplied complete sources
 * remain borrowed until the next SOURCE request or completion/discard. No
 * allocation, I/O, discovery, recursion or callbacks. Outer job owns captured
 * revisions, consumer interests, cancellation and absolute deadlines. */
typedef struct {
    TbSymbolGraphView_t      graph;
    TbSymbolContextAnchor_t *anchors;
    size_t                   capacity;
    size_t                   count;
    size_t                   scan;
    uint64_t                 progress;
    TbSymbolContextStatus_e  status;
    TbSymbolContextFailure_t failure;
    bool                     validating;
    bool                     initialized;
} TbSymbolContext_t;

/* At least graph.count slots; reserve/account all state and capacity, including
 * unused slots. No production default. Failed initialization changes nothing. */
TbSymbolContextStatus_e tb_symbol_context_init(TbSymbolContext_t *context, TbSymbolGraphView_t graph,
                                              TbSymbolContextAnchor_t *anchors, size_t capacity);
/* 1..256 bounded validation/scan actions. Invalid arguments preserve state. */
TbSymbolContextStatus_e tb_symbol_context_step(TbSymbolContext_t *context, size_t max_steps);
bool tb_symbol_context_need(const TbSymbolContext_t *context, TbSymbolGraphNeed_t *need);
/* Explicit source for a terminal relationship node. NULL finalizes DEPENDENCY;
 * wrong physical/unit/abbreviation/parent context preserves the pending request.
 * Unit-root shape and coordinates must agree with the complete selected unit. */
TbSymbolContextStatus_e tb_symbol_context_source(TbSymbolContext_t *context,
                                                const TbSymbolUnitsView_t *units,
                                                const TbSymbolDiesView_t *tree, uint64_t unit_index);
/* READY only. All terminal relation nodes retain their own anchor, even when
 * their parents agree. A shared terminal occurs once. parent/parent_context are
 * meaningful except for UNIT_ROOT; unit_root and unit are always meaningful.
 * Relations supply context even when the selected record has its own name.
 * This is one context-expansion layer, not a qualified name or equivalence proof.
 * Recursive consumers must check mixed lexical/relationship dependency cycles,
 * interpret scope/language profiles and validate full name agreement separately.
 * No independent evidence, nonlocality, runtime binding or authority follows. */
bool tb_symbol_context_view(const TbSymbolContext_t *context, TbSymbolContextView_t *view);
bool tb_symbol_context_failure(const TbSymbolContext_t *context, TbSymbolContextFailure_t *failure);
uint64_t tb_symbol_context_progress(const TbSymbolContext_t *context);

#endif // THE_BUGGER_SYMBOL_CONTEXT_H
