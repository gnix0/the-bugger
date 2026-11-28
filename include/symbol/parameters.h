#ifndef THE_BUGGER_SYMBOL_PARAMETERS_H
#define THE_BUGGER_SYMBOL_PARAMETERS_H

#include "symbol/entities.h"
#include "symbol/references.h"

typedef enum {
    TB_SYMBOL_PARAMETERS_PENDING,
    TB_SYMBOL_PARAMETERS_READY,
    TB_SYMBOL_PARAMETERS_RESOURCE,
    TB_SYMBOL_PARAMETERS_CONFLICT,
    TB_SYMBOL_PARAMETERS_MALFORMED,
    TB_SYMBOL_PARAMETERS_INVALID
} TbSymbolParametersStatus_e;

/* Physical child identity and direct facts, not inherited parameter semantics.
 * Formal entries and unspecified-parameters markers remain in source order. */
typedef struct {
    TbSymbolReferenceTarget_t record;
    TbSymbolEntityView_t      direct;
    bool                      unspecified;
} TbSymbolParameter_t;

typedef struct {
    TbSymbolReferenceTarget_t  selected;
    const TbSymbolParameter_t *parameters;
    size_t                     count;
} TbSymbolParametersView_t;

typedef enum {
    TB_SYMBOL_PARAMETERS_NONE,
    TB_SYMBOL_PARAMETERS_SCOPE,
    TB_SYMBOL_PARAMETERS_ENTITY,
    TB_SYMBOL_PARAMETERS_CAPACITY,
    TB_SYMBOL_PARAMETERS_PROGRESS
} TbSymbolParametersFailure_e;

typedef struct {
    TbSymbolParametersFailure_e reason;
    TbSymbolReferenceTarget_t   selected;
    TbSymbolReferenceTarget_t   record;
    TbSymbolEntityField_e       field;
    TbSymbolAttributeResult_t   selection;
    uint64_t                    entry_index;
    bool                        record_available;
    bool                        selection_available;
} TbSymbolParametersFailure_t;

/* Invalid initialization preserves old state and arena. The arena is nonnull,
 * including at zero capacity. Borrow a complete unchanged tree through discard.
 * Caller accounts state and the full arena capacity; no allocation, I/O, recursion
 * or default capacity.
 * A completed direct inventory is not a complete inherited function signature. */
typedef struct {
    TbSymbolDiesView_t          tree;
    TbSymbolEntity_t            entity;
    TbSymbolParameter_t        *parameters;
    TbSymbolParametersStatus_e  status;
    TbSymbolParametersFailure_e failure;
    uint64_t                    selected;
    uint64_t                    cursor;
    uint64_t                    progress;
    size_t                      capacity;
    size_t                      count;
    bool                        inspecting;
    bool                        initialized;
} TbSymbolParameters_t;

TbSymbolParametersStatus_e tb_symbol_parameters_init(TbSymbolParameters_t *parameters,
                                                     TbSymbolDiesView_t tree, uint64_t selected,
                                                     TbSymbolParameter_t *arena, size_t capacity);
/* 1..256 bounded tree/attribute actions. No view before the whole direct child
 * scope completes; failures expose no successful prefix. Outer job owns captured
 * revisions, cancellation, consumer interest and absolute deadlines. */
TbSymbolParametersStatus_e tb_symbol_parameters_step(TbSymbolParameters_t *parameters, size_t max_steps);
bool tb_symbol_parameters_view(const TbSymbolParameters_t *parameters, TbSymbolParametersView_t *view);
/* Failure only. field/selection are meaningful only when selection_available;
 * record requires record_available. Diagnostics never publish an inventory. */
bool tb_symbol_parameters_failure(const TbSymbolParameters_t *parameters,
                                   TbSymbolParametersFailure_t *failure);
uint64_t tb_symbol_parameters_progress(const TbSymbolParameters_t *parameters);

#endif // THE_BUGGER_SYMBOL_PARAMETERS_H
