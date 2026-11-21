#ifndef THE_BUGGER_SYMBOL_REFERENCES_H
#define THE_BUGGER_SYMBOL_REFERENCES_H

#include "symbol/dies.h"

typedef enum {
    TB_SYMBOL_REFERENCE_UNIT,
    TB_SYMBOL_REFERENCE_INFO,
    TB_SYMBOL_REFERENCE_SIGNATURE,
    TB_SYMBOL_REFERENCE_SUPPLEMENTARY
} TbSymbolReferenceKind_e;

typedef enum {
    TB_SYMBOL_REFERENCE_READY,
    TB_SYMBOL_REFERENCE_PENDING,
    TB_SYMBOL_REFERENCE_FOUND,
    TB_SYMBOL_REFERENCE_NOT_REFERENCE,
    TB_SYMBOL_REFERENCE_INVALID,
    TB_SYMBOL_REFERENCE_RANGE,
    TB_SYMBOL_REFERENCE_MISSING_DIE,
    TB_SYMBOL_REFERENCE_DEPENDENCY,
    TB_SYMBOL_REFERENCE_UNSUPPORTED,
    TB_SYMBOL_REFERENCE_COVERAGE,
    TB_SYMBOL_REFERENCE_MISSING_TYPE,
    TB_SYMBOL_REFERENCE_CONFLICT
} TbSymbolReferenceStatus_e;

typedef struct {
    TbSymbolStorageInfo_t   backing;
    TbSymbolReferenceKind_e kind;
    uint64_t                form;
    uint64_t                value;
    uint64_t                source_section;
    uint64_t                source_unit;
    uint64_t                source_record;
    uint64_t                source_attribute;
    uint64_t                target_offset;
} TbSymbolReference_t;

typedef struct {
    TbSymbolStorageInfo_t backing;
    uint64_t              section_index;
    uint64_t              unit_offset;
    uint64_t              record_offset;
    uint64_t              entry_index;
} TbSymbolReferenceTarget_t;

typedef struct {
    uint64_t unit_index;
    uint64_t die_index;
    uint64_t attribute_index;
} TbSymbolReferenceSelection_t;

typedef struct {
    TbSymbolStorageInfo_t backing;
    uint64_t              section_index;
    uint64_t              unit_index;
    uint64_t              unit_offset;
    uint64_t              record_offset;
} TbSymbolReferenceNeed_t;

/* Borrowed fixed inventory set; caller accounts/pins the array and inventories.
 * Initialize before use; do not modify fields directly. No input pointers escape
 * that lifetime. Each step inspects at most one header or advances one scope. */
typedef struct {
    const TbSymbolUnitsView_t *scopes;
    size_t                     scope_count;
    size_t                     scope_index;
    uint64_t                   unit_index;
    TbSymbolReference_t        reference;
    TbSymbolReferenceNeed_t    candidate;
    TbSymbolReferenceStatus_e  status;
    bool                       initialized;
    bool                       found;
    bool                       conflict;
    bool                       coverage;
} TbSymbolTypeReference_t;

/* Complete borrowed inventories only. Select an attribute within its exact DIE,
 * never by name. Allocation/I/O-free; no discovery or target access. UNIT offsets
 * are checked against the exact source contribution; other kinds retain raw
 * operands and require their separate dependency resolution. Failure leaves
 * output unchanged. Supplementary references are described, not supported. */
TbSymbolReferenceStatus_e tb_symbol_reference_prepare(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                                                      TbSymbolReferenceSelection_t selection,
                                                      TbSymbolReference_t *reference);
/* UNIT references only, using a complete tree for the exact captured backing and
 * unit. Binary lookup performs at most 64 iterations. Other kinds return explicit
 * dependency/unsupported outcomes, never a guessed section or signature target.
 * FOUND proves a physical DIE boundary, not semantic type/specification validity,
 * canonical identity, frontend evidence lifetime, runtime binding or authority. */
TbSymbolReferenceStatus_e tb_symbol_reference_unit(TbSymbolReference_t reference, TbSymbolDiesView_t tree,
                                                   TbSymbolReferenceTarget_t *target);

/* Explicit selected same-backing info inventory. READY selects a required unit,
 * not a validated DIE target. No arbitrary cross-object association is inferred.
 * Raw offset lookup does not establish applicable relocation correspondence. */
TbSymbolReferenceStatus_e tb_symbol_reference_info(TbSymbolReference_t reference, TbSymbolUnitsView_t units,
                                                   TbSymbolReferenceNeed_t *need);
TbSymbolReferenceStatus_e tb_symbol_reference_target(TbSymbolReferenceNeed_t need, TbSymbolDiesView_t tree,
                                                     TbSymbolReferenceTarget_t *target);
/* Scan the entire captured signature inventory before exposing a candidate.
 * Distinct physical matches conflict; repeated views of the same physical unit
 * do not merge different definitions. Unsupported headers prevent a claim of
 * complete coverage or unique resolution. No rescan/discovery/decoding is initiated. */
TbSymbolReferenceStatus_e tb_symbol_type_reference_init(TbSymbolTypeReference_t *lookup,
                                                        TbSymbolReference_t reference,
                                                        const TbSymbolUnitsView_t *scopes, size_t count);
TbSymbolReferenceStatus_e tb_symbol_type_reference_step(TbSymbolTypeReference_t *lookup);
bool tb_symbol_type_reference_need(const TbSymbolTypeReference_t *lookup, TbSymbolReferenceNeed_t *need);

#endif // THE_BUGGER_SYMBOL_REFERENCES_H
