#ifndef THE_BUGGER_SYMBOL_STRING_OFFSETS_H
#define THE_BUGGER_SYMBOL_STRING_OFFSETS_H

#include "symbol/dwarf.h"
#include "symbol/sections.h"

typedef struct tb_symbol_string_offsets TbSymbolStringOffsets_t;

typedef struct {
    uint64_t section_index;
    uint64_t base;
    uint64_t index;
    unsigned offset_bytes;
    int      descriptor;
} TbSymbolStringOffsetSelection_t;

typedef struct {
    TbSymbolStorageInfo_t backing;
    uint64_t             section_index;
    uint64_t             contribution_offset;
    uint64_t             end_offset;
    uint64_t             base;
    uint64_t             count;
    uint64_t             index;
    uint64_t             entry_offset;
    uint64_t             string_offset;
    unsigned             offset_bytes;
} TbSymbolStringOffsetView_t;

typedef enum {
    TB_SYMBOL_STRING_OFFSETS_NONE,
    TB_SYMBOL_STRING_OFFSETS_REQUEST,
    TB_SYMBOL_STRING_OFFSETS_SECTION,
    TB_SYMBOL_STRING_OFFSETS_READ,
    TB_SYMBOL_STRING_OFFSETS_LENGTH,
    TB_SYMBOL_STRING_OFFSETS_HEADER,
    TB_SYMBOL_STRING_OFFSETS_VERSION,
    TB_SYMBOL_STRING_OFFSETS_PADDING,
    TB_SYMBOL_STRING_OFFSETS_FORMAT,
    TB_SYMBOL_STRING_OFFSETS_BASE,
    TB_SYMBOL_STRING_OFFSETS_INDEX
} TbSymbolStringOffsetFailure_e;

typedef struct {
    TbSymbolStringOffsetFailure_e reason;
    uint64_t                      contribution_offset;
    TbSymbolFileFailure_t          read;
} TbSymbolStringOffsetFailure_t;

/* One selected index in a DWARF5 string-offset contribution. Caller establishes
 * the exact section/CU relationship and supplies the CU offset width. Walks
 * contribution framing from zero to prove base is the first entry, rather than
 * guessing a header before base. Borrows directory/descriptor/content lease;
 * no I/O in creation, and all owned state/scratch use construction. */
TbSymbolStringOffsets_t *tb_symbol_string_offsets_create(TbSymbolSectionsView_t directory,
                                                         TbSymbolStringOffsetSelection_t selection,
                                                         TbSymbolBudget_t *construction);
TbSymbolOutcome_e tb_symbol_string_offsets_step(TbSymbolStringOffsets_t *offsets, size_t max_bytes);
/* Selected contribution/entry only; preceding unrelated contributions receive
 * framing examination, not semantic validation. No prefix on error. The raw
 * string offset still requires an explicit string section and termination proof.
 * No split/headerless table support, artifact-wide completeness, frontend-owned
 * evidence, source correspondence or runtime authority follows. */
bool tb_symbol_string_offsets_view(const TbSymbolStringOffsets_t *offsets, TbSymbolStringOffsetView_t *view);
TbSymbolStringOffsetFailure_t tb_symbol_string_offsets_failure(const TbSymbolStringOffsets_t *offsets);
uint64_t tb_symbol_string_offsets_progress(const TbSymbolStringOffsets_t *offsets);
void tb_symbol_string_offsets_destroy(TbSymbolStringOffsets_t *offsets);

#endif // THE_BUGGER_SYMBOL_STRING_OFFSETS_H
