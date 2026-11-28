#ifndef THE_BUGGER_SYMBOL_LOCALITY_H
#define THE_BUGGER_SYMBOL_LOCALITY_H

#include "symbol/scope_graph.h"

typedef enum {
    TB_SYMBOL_LOCALITY_PENDING,
    TB_SYMBOL_LOCALITY_READY,
    TB_SYMBOL_LOCALITY_RESOURCE,
    TB_SYMBOL_LOCALITY_UNSUPPORTED,
    TB_SYMBOL_LOCALITY_CONFLICT,
    TB_SYMBOL_LOCALITY_INVALID
} TbSymbolLocalityStatus_e;

typedef enum {
    TB_SYMBOL_SCOPE_NONLOCAL,
    TB_SYMBOL_SCOPE_LOCAL
} TbSymbolScopeLocality_e;

typedef enum {
    TB_SYMBOL_LOCALITY_UNVISITED,
    TB_SYMBOL_LOCALITY_ACTIVE,
    TB_SYMBOL_LOCALITY_COMPLETE
} TbSymbolLocalityVisit_e;

/* Private workspace, accounted for its full capacity. Only COMPLETE slots have
 * a validated value. Transparent parents retain their physical graph identities;
 * a local barrier does not require interpretation of its own outer context. */
typedef struct {
    size_t                  parent;
    size_t                  next_anchor;
    size_t                  first_anchor;
    TbSymbolScopeLocality_e value;
    TbSymbolLocalityVisit_e visit;
    bool                    has_value;
} TbSymbolLocalitySlot_t;

typedef struct {
    TbSymbolReferenceTarget_t     selected;
    TbSymbolScopeLocality_e       value;
    TbSymbolScopeView_t           scope;
    const TbSymbolLocalitySlot_t *slots;
    size_t                        count;
} TbSymbolLocalityView_t;

typedef enum {
    TB_SYMBOL_LOCALITY_NONE,
    TB_SYMBOL_LOCALITY_SCOPE,
    TB_SYMBOL_LOCALITY_PARENT_TAG,
    TB_SYMBOL_LOCALITY_AGREEMENT,
    TB_SYMBOL_LOCALITY_CYCLE,
    TB_SYMBOL_LOCALITY_PROGRESS
} TbSymbolLocalityFailure_e;

typedef struct {
    TbSymbolLocalityFailure_e reason;
    TbSymbolReferenceTarget_t record;
    size_t                    node_index;
    size_t                    anchor_index;
    size_t                    first_anchor;
    uint64_t                  parent_tag;
} TbSymbolLocalityFailure_t;

/* Caller-owned/accounted cursor and arena. Borrows the successful unchanged
 * scope graph and all underlying graph/context evidence through discard. No
 * names/language rendering, allocation, I/O, recursion or runtime access.
 * LOCAL/NONLOCAL describes logical declaration context, not storage duration,
 * existence, definition role, address mapping or authority. */
typedef struct {
    TbSymbolScopeView_t       scope;
    TbSymbolLocalitySlot_t   *slots;
    TbSymbolLocalityFailure_t failure;
    TbSymbolLocalityStatus_e  status;
    size_t                    capacity;
    size_t                    initializing;
    size_t                    active;
    uint64_t                  progress;
    bool                      initialized;
} TbSymbolLocality_t;

TbSymbolLocalityStatus_e tb_symbol_locality_init(TbSymbolLocality_t *locality, TbSymbolScopeView_t scope,
                                                 TbSymbolLocalitySlot_t *slots, size_t capacity);
/* 1..256 bounded initialization, edge or traversal actions. Failed requests
 * preserve the current operation. Outer job owns captured revisions, request
 * identity, cancellation, consumer interests and absolute deadlines. */
TbSymbolLocalityStatus_e tb_symbol_locality_step(TbSymbolLocality_t *locality, size_t max_steps);
/* READY only. Typed unit boundaries are nonlocal. Namespace/class/struct/union
 * parents propagate their containing context without requiring names. Functions,
 * concrete-inline scopes and lexical/try/catch blocks are local barriers. Other
 * parent tags remain unsupported; every required alternative must agree. */
bool tb_symbol_locality_view(const TbSymbolLocality_t *locality, TbSymbolLocalityView_t *view);
bool tb_symbol_locality_failure(const TbSymbolLocality_t *locality, TbSymbolLocalityFailure_t *failure);
uint64_t tb_symbol_locality_progress(const TbSymbolLocality_t *locality);

#endif // THE_BUGGER_SYMBOL_LOCALITY_H
