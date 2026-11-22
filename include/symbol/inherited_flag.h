#ifndef THE_BUGGER_SYMBOL_INHERITED_FLAG_H
#define THE_BUGGER_SYMBOL_INHERITED_FLAG_H

#include "symbol/inherit.h"

typedef enum {
    TB_SYMBOL_FLAG_ARTIFICIAL       = 0x34,
    TB_SYMBOL_FLAG_PROTOTYPED       = 0x27,
    TB_SYMBOL_FLAG_REFERENCE        = 0x77,
    TB_SYMBOL_FLAG_RVALUE_REFERENCE = 0x78
} TbSymbolInheritedFlagField_e;

typedef enum {
    TB_SYMBOL_FLAG_PENDING,
    TB_SYMBOL_FLAG_SOURCE,
    TB_SYMBOL_FLAG_FOUND,
    TB_SYMBOL_FLAG_ABSENT,
    TB_SYMBOL_FLAG_CONFLICT,
    TB_SYMBOL_FLAG_DEPENDENCY,
    TB_SYMBOL_FLAG_RESOURCE,
    TB_SYMBOL_FLAG_UNSUPPORTED,
    TB_SYMBOL_FLAG_MALFORMED,
    TB_SYMBOL_FLAG_INVALID
} TbSymbolInheritedFlagStatus_e;

typedef enum {
    TB_SYMBOL_FLAG_UNVISITED,
    TB_SYMBOL_FLAG_QUEUED,
    TB_SYMBOL_FLAG_EXAMINED,
    TB_SYMBOL_FLAG_OWNER
} TbSymbolInheritedFlagVisit_e;

/* EXAMINED retains both missing fields and physically encoded zero flags.
 * Only nonzero validated flags are semantic owners. Unvisited slots make no
 * field claim, even though their relationship nodes have been validated. */
typedef struct {
    TbSymbolInheritedFlagVisit_e visit;
    TbSymbolAttributeResult_t    selection;
    TbSymbolDieAttribute_t       attribute;
} TbSymbolInheritedFlagSlot_t;

typedef struct {
    TbSymbolReferenceTarget_t          selected;
    TbSymbolGraphView_t                graph;
    const TbSymbolInheritedFlagSlot_t *slots;
    uint64_t                           name;
    size_t                             owner_count;
    bool                               value;
    bool                               value_available;
} TbSymbolInheritedFlagView_t;

typedef enum {
    TB_SYMBOL_FLAG_FAILURE_NONE,
    TB_SYMBOL_FLAG_FAILURE_SCOPE,
    TB_SYMBOL_FLAG_FAILURE_SOURCE,
    TB_SYMBOL_FLAG_FAILURE_FIELD,
    TB_SYMBOL_FLAG_FAILURE_VALUE,
    TB_SYMBOL_FLAG_FAILURE_EDGE,
    TB_SYMBOL_FLAG_FAILURE_PROGRESS
} TbSymbolInheritedFlagFailureReason_e;

typedef struct {
    TbSymbolInheritedFlagFailureReason_e reason;
    TbSymbolReferenceTarget_t            record;
    TbSymbolAttributeResult_t            selection;
    TbSymbolDieAttribute_t               attribute;
    size_t                               node_index;
    bool                                 selection_available;
    bool                                 attribute_available;
} TbSymbolInheritedFlagFailure_t;

/* Caller owns/accounts state and full slot capacity. Borrow the complete
 * unchanged graph and its source/context evidence through discard. Supplied
 * source views remain borrowed until the next SOURCE request or completion.
 * No allocation, I/O, recursion or discovery. No production capacity default. */
typedef struct {
    TbSymbolGraphView_t            graph;
    TbSymbolInheritedFlagSlot_t   *slots;
    const TbSymbolDieAttribute_t  *attributes;
    TbSymbolAttributes_t           lookup;
    TbSymbolInheritedFlagFailure_t failure;
    TbSymbolInheritedFlagStatus_e  status;
    uint64_t                       name;
    uint64_t                       progress;
    size_t                         initializing;
    size_t                         finding;
    size_t                         active;
    size_t                         scan;
    size_t                         owners;
    unsigned                       edge;
    unsigned                       phase;
    bool                           initialized;
} TbSymbolInheritedFlag_t;

/* Only the four named fields; declaration/sibling/runtime fields are excluded.
 * Invalid/unsupported/resource initialization preserves old state and slots. */
TbSymbolInheritedFlagStatus_e tb_symbol_inherited_flag_init(TbSymbolInheritedFlag_t *flag,
                                                            TbSymbolGraphView_t graph, uint64_t name,
                                                            TbSymbolInheritedFlagSlot_t *slots,
                                                            size_t capacity);
/* 1..256 bounded initialization, lookup, field or edge actions. Outer job owns
 * cancellation, interests, revisions and absolute monotonic deadlines. */
TbSymbolInheritedFlagStatus_e tb_symbol_inherited_flag_step(TbSymbolInheritedFlag_t *flag, size_t max_steps);
bool tb_symbol_inherited_flag_need(const TbSymbolInheritedFlag_t *flag, TbSymbolInheritNeed_t *need);
/* Wrong captured context preserves SOURCE. NULL explicitly reports a missing
 * dependency. A zero flag is semantic absence and continues inheritance. */
TbSymbolInheritedFlagStatus_e tb_symbol_inherited_flag_source(TbSymbolInheritedFlag_t *flag,
                                                              const TbSymbolUnitsView_t *units,
                                                              const TbSymbolDiesView_t *tree,
                                                              uint64_t unit_index);
/* FOUND/ABSENT only. FOUND means semantic presence (true), retaining all distinct
 * physical owners. ABSENT supplies no false override. Zero encodings remain in
 * EXAMINED slots. Value-class proof does not establish tag/language applicability,
 * complete prototypes, method qualifiers, frontend ownership or action authority. */
bool tb_symbol_inherited_flag_view(const TbSymbolInheritedFlag_t *flag, TbSymbolInheritedFlagView_t *view);
bool tb_symbol_inherited_flag_owner(const TbSymbolInheritedFlag_t *flag, size_t node_index,
                                     TbSymbolInheritOwner_t *owner);
/* Failure only. Selections/attributes are diagnostic, not a successful prefix. */
bool tb_symbol_inherited_flag_failure(const TbSymbolInheritedFlag_t *flag,
                                       TbSymbolInheritedFlagFailure_t *failure);
uint64_t tb_symbol_inherited_flag_progress(const TbSymbolInheritedFlag_t *flag);

#endif // THE_BUGGER_SYMBOL_INHERITED_FLAG_H
