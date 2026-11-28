#ifndef THE_BUGGER_SYMBOL_LINE_REFERENCE_H
#define THE_BUGGER_SYMBOL_LINE_REFERENCE_H

#include "symbol/attributes.h"

typedef enum {
    TB_SYMBOL_LINE_REFERENCE_PENDING,
    TB_SYMBOL_LINE_REFERENCE_FOUND,
    TB_SYMBOL_LINE_REFERENCE_ABSENT,
    TB_SYMBOL_LINE_REFERENCE_CONFLICT,
    TB_SYMBOL_LINE_REFERENCE_MALFORMED,
    TB_SYMBOL_LINE_REFERENCE_INVALID
} TbSymbolLineReferenceStatus_e;

typedef struct {
    TbSymbolAttributeResult_t source;
    TbSymbolDieAttribute_t    attribute;
    TbSymbolDwarfUnit_t       unit;
    uint64_t                  abbrev_section;
    uint64_t                  abbrev_offset;
    uint64_t                  line_offset;
} TbSymbolLineReferenceView_t;

/* Caller-owned/accounted state, borrowing a complete tree and matching unit
 * inventory through discard. No inheritance, allocation, I/O or section search.
 * The direct root DW_AT_stmt_list is unique only after its whole scope is
 * examined. Equal duplicate values remain conflicts. */
typedef struct {
    TbSymbolAttributes_t         lookup;
    TbSymbolLineReferenceView_t   view;
    TbSymbolLineReferenceStatus_e status;
    uint64_t                     root_end;
    bool                         initialized;
} TbSymbolLineReference_t;

TbSymbolLineReferenceStatus_e tb_symbol_line_reference_init(TbSymbolLineReference_t *reference,
    TbSymbolUnitsView_t units, TbSymbolDiesView_t tree, uint64_t unit_index);
/* 1..256 attributes and constant-time final classification. Outer jobs retain
 * evidence, revisions, cancellation, consumer interests and deadlines. */
TbSymbolLineReferenceStatus_e tb_symbol_line_reference_step(TbSymbolLineReference_t *reference,
                                                            size_t max_steps);
/* FOUND only. line_offset is an unresolved section-relative dependency, not
 * proof of a .debug_line contribution boundary. The consumer must explicitly
 * select the same-content uncompressed section, validate contribution framing
 * and its full prologue/program, and retain all dependencies. CU address width
 * and line-table version/offset format remain independent. No source-file
 * correspondence, runtime location or action authority follows. */
bool tb_symbol_line_reference_view(const TbSymbolLineReference_t *reference,
                                    TbSymbolLineReferenceView_t *view);
/* Complete FOUND/ABSENT/CONFLICT diagnostics; outputs unchanged otherwise. */
bool tb_symbol_line_reference_scope(const TbSymbolLineReference_t *reference,
                                     TbSymbolAttributeResult_t *scope);
void tb_symbol_line_reference_discard(TbSymbolLineReference_t *reference);

#endif // THE_BUGGER_SYMBOL_LINE_REFERENCE_H
