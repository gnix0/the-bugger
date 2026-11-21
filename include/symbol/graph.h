#ifndef THE_BUGGER_SYMBOL_GRAPH_H
#define THE_BUGGER_SYMBOL_GRAPH_H

#include "symbol/relations.h"

#define TB_SYMBOL_GRAPH_RELATIONS 3

typedef enum {
    TB_SYMBOL_GRAPH_PENDING,
    TB_SYMBOL_GRAPH_SOURCE,
    TB_SYMBOL_GRAPH_TARGET,
    TB_SYMBOL_GRAPH_READY,
    TB_SYMBOL_GRAPH_CYCLE,
    TB_SYMBOL_GRAPH_DEPENDENCY,
    TB_SYMBOL_GRAPH_RELATION,
    TB_SYMBOL_GRAPH_RESOURCE,
    TB_SYMBOL_GRAPH_INVALID
} TbSymbolGraphStatus_e;

typedef struct {
    uint64_t               tag;
    uint16_t               version;
    TbSymbolDwarfUnitKind_e unit_kind;
    uint64_t               abbrev_section;
    uint64_t               abbrev_offset;
    uint64_t               parent_record;
    uint64_t               parent_tag;
} TbSymbolGraphContext_t;

/* Workspace, not published evidence. Callers reserve/account the entire array
 * and state, pin the captured context and do not modify them until discard.
 * No production capacity default: sizing remains part of SE10. */
typedef struct {
    TbSymbolReferenceTarget_t record;
    TbSymbolGraphContext_t    context;
    TbSymbolRelationEdge_t    edges[TB_SYMBOL_GRAPH_RELATIONS];
    size_t                    parent;
    unsigned                  next_edge;
    unsigned                  present;
    bool                      complete;
} TbSymbolGraphNode_t;

typedef struct {
    TbSymbolGraphStatus_e     kind;
    TbSymbolReferenceTarget_t record;
    TbSymbolRelationSource_t  source;
} TbSymbolGraphNeed_t;

typedef struct {
    const TbSymbolGraphNode_t *nodes;
    size_t                    count;
} TbSymbolGraphView_t;

typedef struct {
    size_t                    node_index;
    unsigned                  relation_index;
    size_t                    cycle_index;
    TbSymbolRelationStatus_e  relation_status;
    TbSymbolReferenceTarget_t record;
    TbSymbolRelationSource_t  source;
    TbSymbolAttributeResult_t selection;
    TbSymbolReferenceStatus_e reference_status;
    bool                      source_available;
    bool                      selection_available;
} TbSymbolGraphFailure_t;

/* Initialize before use; fields are private working state. No allocation/I/O,
 * recursion, discovery or callbacks. Each supplied source is borrowed until the
 * next SOURCE request or completion/discard; workspace lasts until discard.
 * Caller owns cancellation, deadlines and captured-context revision checks. */
typedef struct {
    TbSymbolGraphNode_t      *nodes;
    size_t                   capacity;
    size_t                   count;
    size_t                   active;
    size_t                   scan;
    TbSymbolRelation_t       relation;
    TbSymbolGraphStatus_e    status;
    TbSymbolGraphFailure_t   failure;
    uint64_t                 progress;
    unsigned                 collecting;
    bool                     traversing;
    bool                     scanning;
    bool                     initialized;
} TbSymbolGraph_t;

/* Root comes from a validated complete tree/reference. Failed initialization
 * changes neither state nor workspace. Capacity is a checked caller-owned
 * construction allowance; exhaustion cannot produce a successful prefix. */
TbSymbolGraphStatus_e tb_symbol_graph_init(TbSymbolGraph_t *graph, TbSymbolReferenceTarget_t root,
                                           TbSymbolGraphNode_t *nodes, size_t capacity);
/* 1..256 bounded attribute examinations or traversal actions. Visited-record
 * lookup is incremental linear scanning; total work can be quadratic in graph
 * size. Absolute deadlines and production sizing remain outer-job obligations. */
TbSymbolGraphStatus_e tb_symbol_graph_step(TbSymbolGraph_t *graph, size_t max_steps);
/* Need is available only at SOURCE/TARGET; invalid requests preserve output. */
bool tb_symbol_graph_need(const TbSymbolGraph_t *graph, TbSymbolGraphNeed_t *need);
/* Explicit complete source dependencies. NULL views finalize DEPENDENCY.
 * Other invalid supplies preserve the pending request so it can be corrected.
 * Unit selection, physical coordinates and incoming target provenance must agree. */
TbSymbolGraphStatus_e tb_symbol_graph_source(TbSymbolGraph_t *graph, const TbSymbolUnitsView_t *units,
                                             const TbSymbolDiesView_t *tree, uint64_t unit_index);
/* Explicit selected info inventory (when needed) and target tree. Uses the scoped
 * relation validator; no implicit inventory selection or target decoding. */
TbSymbolGraphStatus_e tb_symbol_graph_target(TbSymbolGraph_t *graph, const TbSymbolUnitsView_t *info,
                                             const TbSymbolDiesView_t *tree);
/* READY only: complete reachable graph for the captured three relation kinds.
 * A shared completed node is not a cycle. Every direct edge retains provenance;
 * records are never canonicalized. No inheritance, lexical-context equivalence,
 * relocation correspondence, runtime binding or action authority follows. */
bool tb_symbol_graph_view(const TbSymbolGraph_t *graph, TbSymbolGraphView_t *view);
/* Agreement with a completed node and complete source views, not fresh decoding.
 * Checks physical coordinates and captured tag/version/unit/abbreviation/parent
 * context, including DWARF 4 info header refinement. No allocation or I/O. */
bool tb_symbol_graph_node_matches(const TbSymbolGraphNode_t *node, TbSymbolUnitsView_t units,
                                  TbSymbolDiesView_t tree, uint64_t unit_index);
/* Failed graph diagnostics only. Raw source/selection availability is explicit;
 * a source dependency failure has no selected outgoing field. Workspace can be
 * inspected diagnostically, but it is not a successfully validated graph. */
bool tb_symbol_graph_failure(const TbSymbolGraph_t *graph, TbSymbolGraphFailure_t *failure);
uint64_t tb_symbol_graph_progress(const TbSymbolGraph_t *graph);

#endif // THE_BUGGER_SYMBOL_GRAPH_H
