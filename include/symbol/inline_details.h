#ifndef THE_BUGGER_SYMBOL_INLINE_DETAILS_H
#define THE_BUGGER_SYMBOL_INLINE_DETAILS_H

#include "symbol/constants.h"
#include "symbol/graph.h"

#define TB_SYMBOL_INLINE_COORDINATES 3

typedef enum {
    TB_SYMBOL_INLINE_DETAILS_PENDING,
    TB_SYMBOL_INLINE_DETAILS_GRAPH,
    TB_SYMBOL_INLINE_DETAILS_SCHEMA,
    TB_SYMBOL_INLINE_DETAILS_READY,
    TB_SYMBOL_INLINE_DETAILS_PARTIAL,
    TB_SYMBOL_INLINE_DETAILS_RESOURCE,
    TB_SYMBOL_INLINE_DETAILS_DEPENDENCY,
    TB_SYMBOL_INLINE_DETAILS_CONFLICT,
    TB_SYMBOL_INLINE_DETAILS_UNSUPPORTED,
    TB_SYMBOL_INLINE_DETAILS_MALFORMED,
    TB_SYMBOL_INLINE_DETAILS_INVALID
} TbSymbolInlineDetailsStatus_e;

typedef struct {
    TbSymbolConstantStatus_e  status;
    TbSymbolAttributeResult_t selection;
    TbSymbolConstantView_t    constant;
} TbSymbolInlineCoordinate_t;

/* Each physical instance appears once in DIE order, including nested instances.
 * Graphs retain exact specification/origin relationships without canonicalizing
 * their nodes. Coordinates are direct CALL_FILE, CALL_LINE, CALL_COLUMN, in that
 * order. Missing is distinct from zero; unsupported retains raw selection only.
 * No declaration coordinate is substituted for a missing call-site coordinate. */
typedef struct {
    TbSymbolReferenceTarget_t  record;
    TbSymbolReferenceTarget_t  parent;
    uint64_t                   parent_tag;
    TbSymbolGraphView_t        relationships;
    TbSymbolInlineCoordinate_t coordinates[TB_SYMBOL_INLINE_COORDINATES];
    bool                       origin_available;
} TbSymbolInlineDetail_t;

typedef struct {
    TbSymbolUnitsView_t           units;
    TbSymbolDiesView_t            tree;
    uint64_t                      unit_index;
    const TbSymbolInlineDetail_t *instances;
    size_t                        count;
    bool                          partial;
} TbSymbolInlineDetailsView_t;

typedef struct {
    TbSymbolInlineDetailsStatus_e status;
    TbSymbolReferenceTarget_t     record;
    TbSymbolConstantField_e       field;
    TbSymbolConstantStatus_e      constant_status;
    TbSymbolAttributeResult_t     selection;
    TbSymbolGraphStatus_e         graph_status;
    TbSymbolRelationStatus_e      relation_status;
    TbSymbolGraphFailure_t        graph_failure;
    bool                          record_available;
    bool                          selection_available;
    bool                          graph_failure_available;
} TbSymbolInlineDetailsFailure_t;

/* Caller owns/accounts stationary state, full arena capacity, source scope and
 * every supplied completed graph/schema through discard. No allocation, I/O,
 * discovery, recursion, fixed capacity or artifact-wide completeness claim.
 * This complete single-unit inventory is a metadata foundation for explicit
 * details/indexing, not a frontend window or an executable-location catalog. */
typedef struct {
    TbSymbolInlineDetailsView_t    view;
    TbSymbolInlineDetail_t        *arena;
    TbSymbolConstant_t             constant;
    TbSymbolInlineDetailsFailure_t failure;
    TbSymbolInlineDetailsStatus_e  status;
    uint64_t                       cursor;
    uint64_t                       progress;
    size_t                         capacity;
    size_t                         graph_cursor;
    unsigned                       coordinate;
    unsigned                       phase;
    bool                           initialized;
} TbSymbolInlineDetails_t;

TbSymbolInlineDetailsStatus_e tb_symbol_inline_details_init(TbSymbolInlineDetails_t *details,
    TbSymbolUnitsView_t units, TbSymbolDiesView_t tree, uint64_t unit_index,
    TbSymbolInlineDetail_t *arena, size_t capacity);
/* 1..256 tree, graph-node or constant-decoder actions. Outer job owns captured
 * revisions, request/consumer generations, cancellation and absolute deadlines.
 * No view before the entire scope completes; no failed successful prefix. */
TbSymbolInlineDetailsStatus_e tb_symbol_inline_details_step(TbSymbolInlineDetails_t *details,
                                                           size_t max_steps);
/* Exact record for GRAPH only. Wrong record/context/status leaves need intact.
 * READY requires a completed graph rooted at that instance. Other accepted
 * terminal graph statuses report failure, never absence or a partial inventory.
 * RELATION requires its completed failure diagnostic to distinguish malformed
 * metadata, conflict, unavailable dependencies and unsupported semantics. */
bool tb_symbol_inline_details_need(const TbSymbolInlineDetails_t *details, TbSymbolReferenceTarget_t *record);
TbSymbolInlineDetailsStatus_e tb_symbol_inline_details_graph(TbSymbolInlineDetails_t *details,
    TbSymbolReferenceTarget_t record, TbSymbolGraphStatus_e status, const TbSymbolGraphView_t *graph,
    const TbSymbolGraphFailure_t *failure);
TbSymbolInlineDetailsStatus_e tb_symbol_inline_details_schema(TbSymbolInlineDetails_t *details,
                                                             const TbSymbolAbbrevView_t *schema);
/* READY/PARTIAL only. Partial means a missing direct origin or a safely framed
 * unsupported coordinate. Absent coordinates alone do not make the inventory
 * partial. Graph/type/name/line/range/binding consumers retain their own gates. */
bool tb_symbol_inline_details_view(const TbSymbolInlineDetails_t *details, TbSymbolInlineDetailsView_t *view);
bool tb_symbol_inline_details_failure(const TbSymbolInlineDetails_t *details,
                                      TbSymbolInlineDetailsFailure_t *failure);
uint64_t tb_symbol_inline_details_progress(const TbSymbolInlineDetails_t *details);
void tb_symbol_inline_details_discard(TbSymbolInlineDetails_t *details);

#endif // THE_BUGGER_SYMBOL_INLINE_DETAILS_H
