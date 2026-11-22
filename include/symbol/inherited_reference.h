#ifndef THE_BUGGER_SYMBOL_INHERITED_REFERENCE_H
#define THE_BUGGER_SYMBOL_INHERITED_REFERENCE_H

#include "symbol/inherit.h"

typedef enum {
    TB_SYMBOL_INHERITED_REFERENCE_PENDING,
    TB_SYMBOL_INHERITED_REFERENCE_SOURCE,
    TB_SYMBOL_INHERITED_REFERENCE_TARGET,
    TB_SYMBOL_INHERITED_REFERENCE_FOUND,
    TB_SYMBOL_INHERITED_REFERENCE_ABSENT,
    TB_SYMBOL_INHERITED_REFERENCE_CONFLICT,
    TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY,
    TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED,
    TB_SYMBOL_INHERITED_REFERENCE_MALFORMED,
    TB_SYMBOL_INHERITED_REFERENCE_RESOURCE,
    TB_SYMBOL_INHERITED_REFERENCE_INVALID
} TbSymbolInheritedReferenceStatus_e;

typedef struct {
    size_t                    node_index;
    TbSymbolAttributeResult_t selection;
    TbSymbolDieAttribute_t    attribute;
    TbSymbolReference_t       reference;
    TbSymbolReferenceTarget_t target;
} TbSymbolInheritedReferenceFact_t;

typedef struct {
    TbSymbolReferenceTarget_t               selected;
    TbSymbolGraphView_t                     graph;
    const TbSymbolInheritSlot_t            *slots;
    const TbSymbolInheritedReferenceFact_t *facts;
    size_t                                  count;
    uint64_t                                name;
} TbSymbolInheritedReferenceView_t;

typedef enum {
    TB_SYMBOL_INHERITED_REFERENCE_OWNER_SOURCE,
    TB_SYMBOL_INHERITED_REFERENCE_VALUE_SOURCE
} TbSymbolInheritedReferenceSourceKind_e;

typedef struct {
    TbSymbolInheritedReferenceSourceKind_e kind;
    TbSymbolInheritNeed_t                  source;
} TbSymbolInheritedReferenceSource_t;

typedef struct {
    TbSymbolInheritedReferenceStatus_e status;
    TbSymbolInheritFailure_t           owner;
    TbSymbolInheritOwner_t             field;
    TbSymbolReferenceStatus_e          reference_status;
    TbSymbolReference_t                reference;
    TbSymbolReferenceNeed_t            target;
    size_t                             node_index;
    bool                               field_available;
    bool                               reference_available;
    bool                               target_available;
} TbSymbolInheritedReferenceFailure_t;

/* Caller-owned/accounted state and both full arenas. Borrow the successful graph,
 * explicit info/type inventories and every supplied complete source/target view
 * through discard. No discovery, allocation, I/O or recursion. No production
 * capacity default; the outer job owns revisions, interests and deadlines. */
typedef struct {
    TbSymbolInherit_t                   owners;
    TbSymbolTypeReference_t             signature;
    TbSymbolInheritedReferenceFact_t   *facts;
    const TbSymbolUnitsView_t          *info;
    const TbSymbolUnitsView_t          *type_scopes;
    TbSymbolReferenceNeed_t             need;
    TbSymbolInheritedReferenceFact_t    candidate;
    TbSymbolInheritedReferenceFailure_t failure;
    TbSymbolInheritedReferenceStatus_e  status;
    size_t                              type_scope_count;
    size_t                              next_owner;
    size_t                              count;
    uint64_t                            progress;
    unsigned                            phase;
    bool                                initialized;
} TbSymbolInheritedReference_t;

/* type or object_pointer only. NULL info denotes an unavailable .debug_info
 * inventory. A zero type_scope_count denotes unavailable type-signature scopes.
 * Both workspace capacities cover graph.count; checked rejection preserves old
 * state and arenas. Each owner resolves independently, with no type equivalence. */
TbSymbolInheritedReferenceStatus_e tb_symbol_inherited_reference_init(
    TbSymbolInheritedReference_t *reference, TbSymbolGraphView_t graph, uint64_t name,
    TbSymbolInheritSlot_t *slots, size_t slot_capacity,
    TbSymbolInheritedReferenceFact_t *facts, size_t fact_capacity,
    const TbSymbolUnitsView_t *info, const TbSymbolUnitsView_t *type_scopes, size_t type_scope_count);
/* 1..256 bounded owner, selection or signature-inventory actions. */
TbSymbolInheritedReferenceStatus_e tb_symbol_inherited_reference_step(
    TbSymbolInheritedReference_t *reference, size_t max_steps);
bool tb_symbol_inherited_reference_source_need(const TbSymbolInheritedReference_t *reference,
                                                TbSymbolInheritedReferenceSource_t *need);
/* Wrong captured context/attribute preserves the source request; NULL reports
 * a missing dependency. No source or target supply performs I/O. */
TbSymbolInheritedReferenceStatus_e tb_symbol_inherited_reference_source(
    TbSymbolInheritedReference_t *reference, const TbSymbolUnitsView_t *units,
    const TbSymbolDiesView_t *tree, uint64_t unit_index);
bool tb_symbol_inherited_reference_target_need(const TbSymbolInheritedReference_t *reference,
                                                TbSymbolReferenceNeed_t *need);
TbSymbolInheritedReferenceStatus_e tb_symbol_inherited_reference_target(
    TbSymbolInheritedReference_t *reference, const TbSymbolDiesView_t *tree);
/* FOUND/ABSENT only, after every owner resolves or complete absence is proved.
 * graph/slots retain physical owner/absence derivations. Distinct owners remain
 * distinct even if their physical targets agree. No canonical type, valid object formal,
 * complete signature, frontend evidence ownership or runtime authority follows. */
bool tb_symbol_inherited_reference_view(const TbSymbolInheritedReference_t *reference,
                                         TbSymbolInheritedReferenceView_t *view);
bool tb_symbol_inherited_reference_failure(const TbSymbolInheritedReference_t *reference,
                                            TbSymbolInheritedReferenceFailure_t *failure);
uint64_t tb_symbol_inherited_reference_progress(const TbSymbolInheritedReference_t *reference);

#endif // THE_BUGGER_SYMBOL_INHERITED_REFERENCE_H
