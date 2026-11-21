#ifndef THE_BUGGER_SYMBOL_ATTRIBUTES_H
#define THE_BUGGER_SYMBOL_ATTRIBUTES_H

#include "symbol/dies.h"

typedef enum {
    TB_SYMBOL_ATTRIBUTE_PENDING,
    TB_SYMBOL_ATTRIBUTE_UNIQUE,
    TB_SYMBOL_ATTRIBUTE_MISSING,
    TB_SYMBOL_ATTRIBUTE_AMBIGUOUS,
    TB_SYMBOL_ATTRIBUTE_INVALID
} TbSymbolAttributeStatus_e;

typedef struct {
    uint64_t die_index;
    uint64_t name;
} TbSymbolAttributeSelection_t;

typedef struct {
    TbSymbolStorageInfo_t backing;
    uint64_t             section_index;
    uint64_t             unit_offset;
    uint64_t             record_offset;
    uint64_t             name;
    uint64_t             matches;
    uint64_t             first_index;
    uint64_t             second_index;
    uint64_t             first_offset;
    uint64_t             second_offset;
} TbSymbolAttributeResult_t;

/* Initialize before use; do not modify fields directly. Borrows the complete
 * tree's attribute array until completion/discard. Caller accounts embedded state
 * and retains the tree's evidence/context. No allocation, I/O or inheritance. */
typedef struct {
    const TbSymbolDieAttribute_t *attributes;
    uint64_t                     cursor;
    uint64_t                     end;
    TbSymbolAttributeResult_t     result;
    TbSymbolAttributeStatus_e     status;
    bool                         initialized;
} TbSymbolAttributes_t;

TbSymbolAttributeStatus_e tb_symbol_attributes_init(TbSymbolAttributes_t *lookup, TbSymbolDiesView_t tree,
                                                    TbSymbolAttributeSelection_t selection);
/* 1..256 inspected attributes per call; no candidate before the whole selected
 * DIE scope is examined. Outer job owns cancellation, deadlines and revisions. */
TbSymbolAttributeStatus_e tb_symbol_attributes_step(TbSymbolAttributes_t *lookup, size_t max_steps);
/* Complete diagnostics for UNIQUE/MISSING/AMBIGUOUS only. Missing indexes/offsets
 * are UINT64_MAX. Equal values do not collapse duplicate physical attributes.
 * Only UNIQUE permits interpreting a single attribute. No type/name validity,
 * dependency resolution, frontend ownership or action authority follows. */
TbSymbolAttributeStatus_e tb_symbol_attributes_result(const TbSymbolAttributes_t *lookup,
                                                      TbSymbolAttributeResult_t *result);
/* Unique borrowed-array index only; output unchanged for every other state. */
bool tb_symbol_attributes_unique(const TbSymbolAttributes_t *lookup, uint64_t *index);

#endif // THE_BUGGER_SYMBOL_ATTRIBUTES_H
