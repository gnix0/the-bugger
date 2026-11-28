#ifndef THE_BUGGER_SYMBOL_CONSTANTS_H
#define THE_BUGGER_SYMBOL_CONSTANTS_H

#include "symbol/attributes.h"

typedef enum {
    TB_SYMBOL_CONSTANT_DECL_COLUMN = 0x39,
    TB_SYMBOL_CONSTANT_DECL_FILE   = 0x3a,
    TB_SYMBOL_CONSTANT_DECL_LINE   = 0x3b,
    TB_SYMBOL_CONSTANT_LANGUAGE    = 0x13,
    TB_SYMBOL_CONSTANT_INLINE      = 0x20,
    TB_SYMBOL_CONSTANT_CALL_COLUMN = 0x57,
    TB_SYMBOL_CONSTANT_CALL_FILE   = 0x58,
    TB_SYMBOL_CONSTANT_CALL_LINE   = 0x59
} TbSymbolConstantField_e;

typedef enum {
    TB_SYMBOL_CONSTANT_PENDING,
    TB_SYMBOL_CONSTANT_SCHEMA,
    TB_SYMBOL_CONSTANT_FOUND,
    TB_SYMBOL_CONSTANT_ABSENT,
    TB_SYMBOL_CONSTANT_CONFLICT,
    TB_SYMBOL_CONSTANT_UNSUPPORTED,
    TB_SYMBOL_CONSTANT_DEPENDENCY,
    TB_SYMBOL_CONSTANT_MALFORMED,
    TB_SYMBOL_CONSTANT_INVALID
} TbSymbolConstantStatus_e;

typedef enum {
    TB_SYMBOL_LANGUAGE_OTHER,
    TB_SYMBOL_LANGUAGE_C,
    TB_SYMBOL_LANGUAGE_CPP
} TbSymbolLanguageFamily_e;

typedef enum {
    TB_SYMBOL_INLINE_NOT_INLINED          = 0,
    TB_SYMBOL_INLINE_INLINED              = 1,
    TB_SYMBOL_INLINE_DECLARED_NOT_INLINED = 2,
    TB_SYMBOL_INLINE_DECLARED_INLINED     = 3
} TbSymbolInlineKind_e;

/* abbreviation and schema_entry apply only when from_abbreviation is true.
 * The original DIE attribute is preserved, including a direct implicit form's
 * zero footprint and lack of an inline numeric payload. */
typedef struct {
    TbSymbolAttributeResult_t selection;
    TbSymbolDieAttribute_t    attribute;
    uint64_t                  abbrev_section;
    uint64_t                  abbrev_offset;
    uint64_t                  value;
    TbSymbolAbbrevAttribute_t abbreviation;
    uint64_t                  schema_entry;
    bool                      from_abbreviation;
} TbSymbolConstantView_t;

/* Private caller-owned/accounted state. Complete tree/framing/context evidence
 * stays pinned through completion/discard. No allocation, I/O or inheritance. */
typedef struct {
    TbSymbolAttributes_t          lookup;
    const TbSymbolDieAttribute_t *attributes;
    TbSymbolConstantView_t        view;
    TbSymbolDieEntry_t            entry;
    TbSymbolAbbrevView_t          schema;
    uint64_t                      schema_cursor;
    bool                          schema_available;
    TbSymbolConstantStatus_e      status;
    bool                          initialized;
} TbSymbolConstant_t;

/* LANGUAGE selects a compile/partial/type unit's physical root; INLINE selects
 * a subprogram; DECL_FILE/LINE/COLUMN select direct declaration coordinates
 * on a physical DIE (inheritance remains a separate selection). CALL_FILE/LINE/COLUMN
 * select a concrete inline instance's direct call-site metadata. File indexes and zero values remain uninterpreted until
 * the applicable line-table version/dependencies are validated by SE07.
 * Failed initialization preserves state. No unit-header evidence
 * or language inheritance is created by this direct attribute interpreter. */
TbSymbolConstantStatus_e tb_symbol_constant_init(TbSymbolConstant_t *constant, TbSymbolDiesView_t tree,
                                                 uint64_t die_index, TbSymbolConstantField_e field);
/* 1..256 attributes or schema entries; invalid arguments preserve state.
 * A complete uniqueness scan precedes interpretation. Outer job owns revisions,
 * cancellation and absolute deadlines. */
TbSymbolConstantStatus_e tb_symbol_constant_step(TbSymbolConstant_t *constant, size_t max_steps);
/* SCHEMA requests the exact retained abbreviation table for a direct implicit
 * constant. Wrong table/context preserves the request; NULL reports dependency.
 * Successful supply remains borrowed through discard. Entry ownership is then
 * checked in bounded steps, without discovery or I/O. */
TbSymbolConstantStatus_e tb_symbol_constant_schema(TbSymbolConstant_t *constant,
                                                   const TbSymbolAbbrevView_t *schema);
/* FOUND only. Nonnegative data1/2/4/8, udata, sdata and implicit_const values;
 * direct implicit values retain their separate abbreviation provenance;
 * framed data16 and wider unnormalized implicit values remain unsupported.
 * No successful prefix, truncation or name/class/runtime claim. Value zero
 * differs from ABSENT. */
bool tb_symbol_constant_view(const TbSymbolConstant_t *constant, TbSymbolConstantView_t *view);
/* Complete selection diagnostics after terminal outcomes and while requesting
 * SCHEMA; no successful value follows until that dependency is validated. */
bool tb_symbol_constant_selection(const TbSymbolConstant_t *constant, TbSymbolAttributeResult_t *selection);
/* Explicit C/C++ language code set only; OTHER includes unknown/unsupported
 * languages, never permission to use C++ qualification rules. */
TbSymbolLanguageFamily_e tb_symbol_language_family(uint64_t language);
/* Unknown inline codes preserve output. Every nonzero standard code, including
 * DECLARED_NOT_INLINED, identifies an abstract inline root under DWARF's rule;
 * none alone proves a concrete instance or standalone emitted code. */
bool tb_symbol_inline_kind(uint64_t value, TbSymbolInlineKind_e *kind);

#endif // THE_BUGGER_SYMBOL_CONSTANTS_H
