#ifndef THE_BUGGER_SYMBOL_CODE_ATTRIBUTES_H
#define THE_BUGGER_SYMBOL_CODE_ATTRIBUTES_H

#include "symbol/attributes.h"

typedef enum {
    TB_SYMBOL_CODE_LOW,
    TB_SYMBOL_CODE_HIGH,
    TB_SYMBOL_CODE_RANGES,
    TB_SYMBOL_CODE_ADDR_BASE,
    TB_SYMBOL_CODE_RNGLISTS_BASE,
    TB_SYMBOL_CODE_FIELDS
} TbSymbolCodeField_e;

typedef enum {
    TB_SYMBOL_CODE_MISSING,
    TB_SYMBOL_CODE_ADDRESS,
    TB_SYMBOL_CODE_ADDRESS_INDEX,
    TB_SYMBOL_CODE_LENGTH,
    TB_SYMBOL_CODE_LIST_OFFSET,
    TB_SYMBOL_CODE_LIST_INDEX,
    TB_SYMBOL_CODE_TABLE_BASE
} TbSymbolCodeValueKind_e;

typedef enum {
    TB_SYMBOL_CODE_PENDING,
    TB_SYMBOL_CODE_SCHEMA,
    TB_SYMBOL_CODE_COMPLETE,
    TB_SYMBOL_CODE_CONFLICT,
    TB_SYMBOL_CODE_MALFORMED,
    TB_SYMBOL_CODE_UNSUPPORTED,
    TB_SYMBOL_CODE_DEPENDENCY,
    TB_SYMBOL_CODE_INVALID
} TbSymbolCodeStatus_e;

typedef struct {
    uint64_t unit_index;
    uint64_t die_index;
} TbSymbolCodeSelection_t;

/* Abbreviation fields apply only when from_abbreviation is true; the original
 * zero-footprint DIE attribute remains unchanged. */
typedef struct {
    TbSymbolAttributeResult_t selection;
    TbSymbolDieAttribute_t    attribute;
    TbSymbolAbbrevAttribute_t abbreviation;
    uint64_t                  schema_entry;
    uint64_t                  value;
    TbSymbolCodeValueKind_e    kind;
    bool                      from_abbreviation;
} TbSymbolCodeFact_t;

typedef struct {
    TbSymbolCodeFact_t  fields[TB_SYMBOL_CODE_FIELDS];
    TbSymbolDwarfUnit_t unit;
    uint64_t           abbrev_section;
    uint64_t           abbrev_offset;
    uint64_t           die_index;
    uint64_t           begin;
    uint64_t           end;
    bool               interval_available;
} TbSymbolCodeView_t;

/* Caller-owned/accounted state. Borrow the complete tree and matching unit
 * inventory through discard; retain any supplied abbreviation table too.
 * Interpret only direct physical attributes, without inheritance, allocation,
 * I/O, section discovery, relocation or runtime access. */
typedef struct {
    TbSymbolAttributes_t          lookup[TB_SYMBOL_CODE_FIELDS];
    TbSymbolCodeView_t            view;
    TbSymbolDieEntry_t            entry;
    TbSymbolAbbrevView_t          schema;
    const TbSymbolDieAttribute_t *attributes;
    uint64_t                      schema_cursor;
    unsigned                      field;
    TbSymbolCodeStatus_e           status;
    bool                          schema_available;
    bool                          initialized;
} TbSymbolCode_t;

TbSymbolCodeStatus_e tb_symbol_code_init(TbSymbolCode_t *code, TbSymbolUnitsView_t units,
    TbSymbolDiesView_t tree, TbSymbolCodeSelection_t selection);
/* 1..256 inspected attributes or abbreviation entries per call. Every selected
 * scope completes before any view. Outer jobs own revisions, cancellation,
 * consumer interests and absolute deadlines. */
TbSymbolCodeStatus_e tb_symbol_code_step(TbSymbolCode_t *code, size_t max_steps);
/* SCHEMA only: request is the selected high-PC direct implicit constant's exact
 * abbreviation table. Wrong context preserves the request; NULL reports missing
 * dependency. Supply performs no I/O; ownership is checked in bounded steps. */
TbSymbolCodeStatus_e tb_symbol_code_schema(TbSymbolCode_t *code, const TbSymbolAbbrevView_t *schema);
/* COMPLETE only, including proven absence. Address/list indexes and section
 * offsets remain explicit unresolved dependencies, never numeric addresses or
 * proved list boundaries. CU bases apply only to this selected root. A direct
 * low/high pair can yield an artifact interval; low alone is not an extent.
 * No instruction boundary, standalone entry, source correspondence, runtime
 * binding or action authority follows. Outputs remain unchanged on failure. */
bool tb_symbol_code_view(const TbSymbolCode_t *code, TbSymbolCodeView_t *view);
/* Completed selection diagnostics, even when subsequent interpretation fails.
 * Pending scopes are unavailable. Equal duplicate values remain conflicts. */
bool tb_symbol_code_selection(const TbSymbolCode_t *code, TbSymbolCodeField_e field,
    TbSymbolAttributeResult_t *selection);
void tb_symbol_code_discard(TbSymbolCode_t *code);

#endif // THE_BUGGER_SYMBOL_CODE_ATTRIBUTES_H
