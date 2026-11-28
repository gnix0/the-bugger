#ifndef THE_BUGGER_SYMBOL_TEXT_H
#define THE_BUGGER_SYMBOL_TEXT_H

#include "symbol/attributes.h"
#include "symbol/string_offsets.h"
#include "symbol/strings.h"
#include "symbol/units.h"

typedef struct tb_symbol_text TbSymbolText_t;

typedef enum {
    TB_SYMBOL_TEXT_PENDING,
    TB_SYMBOL_TEXT_FOUND,
    TB_SYMBOL_TEXT_ABSENT,
    TB_SYMBOL_TEXT_CONFLICT,
    TB_SYMBOL_TEXT_DEPENDENCY,
    TB_SYMBOL_TEXT_UNSUPPORTED,
    TB_SYMBOL_TEXT_MALFORMED,
    TB_SYMBOL_TEXT_RESOURCE,
    TB_SYMBOL_TEXT_OPERATIONAL
} TbSymbolTextOutcome_e;

typedef struct {
    uint64_t unit_index;
    uint64_t die_index;
    uint64_t name;
    uint64_t string_section;
    uint64_t line_string_section;
    uint64_t offset_section;
    int      descriptor;
} TbSymbolTextSelection_t;

typedef struct {
    TbSymbolStorageInfo_t       backing;
    uint64_t                   source_section;
    uint64_t                   source_unit;
    uint64_t                   source_record;
    uint64_t                   abbrev_section;
    uint64_t                   abbrev_offset;
    TbSymbolDieAttribute_t     attribute;
    TbSymbolStringView_t       text;
    bool                       indexed;
    TbSymbolDieAttribute_t     base_attribute;
    TbSymbolStringOffsetView_t offsets;
} TbSymbolTextView_t;

typedef enum {
    TB_SYMBOL_TEXT_NONE,
    TB_SYMBOL_TEXT_REQUEST,
    TB_SYMBOL_TEXT_FIELD_ABSENT,
    TB_SYMBOL_TEXT_FIELD_AMBIGUOUS,
    TB_SYMBOL_TEXT_BASE_ABSENT,
    TB_SYMBOL_TEXT_BASE_AMBIGUOUS,
    TB_SYMBOL_TEXT_FORM,
    TB_SYMBOL_TEXT_VALUE,
    TB_SYMBOL_TEXT_VERSION,
    TB_SYMBOL_TEXT_STRING_SECTION,
    TB_SYMBOL_TEXT_LINE_STRING_SECTION,
    TB_SYMBOL_TEXT_OFFSET_SECTION,
    TB_SYMBOL_TEXT_OFFSETS,
    TB_SYMBOL_TEXT_STRING,
    TB_SYMBOL_TEXT_SPAN,
    TB_SYMBOL_TEXT_ALLOCATION
} TbSymbolTextFailure_e;

typedef struct {
    TbSymbolTextFailure_e         reason;
    TbSymbolAttributeResult_t     attributes;
    TbSymbolStringOffsetFailure_t offsets;
    TbSymbolStringFailure_t       string;
} TbSymbolTextFailure_t;

/* One selected string-class attribute from complete retained views. UINT64_MAX
 * explicitly marks an unavailable string/line-string/offset section. Caller
 * establishes section roles; constructor checks exact source backing/unit and
 * performs no I/O. Borrows all views, descriptor and content lease; owns only
 * checked construction state and child decoders. No discovery or inheritance. */
TbSymbolText_t *tb_symbol_text_create(TbSymbolSectionsView_t directory, TbSymbolUnitsView_t units,
                                     TbSymbolDiesView_t tree, TbSymbolTextSelection_t selection,
                                     TbSymbolBudget_t *construction);
TbSymbolTextOutcome_e tb_symbol_text_step(TbSymbolText_t *text, size_t max_bytes);
/* FOUND only, borrowed until destruction. Empty text is FOUND with length zero;
 * ABSENT is a missing attribute, never an empty-name fallback. Duplicate fields
 * or bases remain conflicts even when values agree. Indexed provenance fields
 * are meaningful only when indexed is true. No full text allocation, Unicode
 * validation, semantic naming, frontend guarantee or action authority follows. */
bool tb_symbol_text_view(const TbSymbolText_t *text, TbSymbolTextView_t *view);
TbSymbolTextFailure_t tb_symbol_text_failure(const TbSymbolText_t *text);
uint64_t tb_symbol_text_progress(const TbSymbolText_t *text);
void tb_symbol_text_destroy(TbSymbolText_t *text);

#endif // THE_BUGGER_SYMBOL_TEXT_H
