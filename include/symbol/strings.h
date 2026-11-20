#ifndef THE_BUGGER_SYMBOL_STRINGS_H
#define THE_BUGGER_SYMBOL_STRINGS_H

#include "symbol/sections.h"

typedef struct tb_symbol_string TbSymbolString_t;

typedef struct {
    uint64_t section_index;
    uint64_t offset;
    uint64_t end_offset;
    int      descriptor;
} TbSymbolStringSelection_t;

typedef struct {
    TbSymbolStorageInfo_t backing;
    uint64_t             section_index;
    uint64_t             offset;
    uint64_t             bytes;
    uint64_t             end_offset;
} TbSymbolStringView_t;

typedef enum {
    TB_SYMBOL_STRING_NONE,
    TB_SYMBOL_STRING_REQUEST,
    TB_SYMBOL_STRING_SECTION,
    TB_SYMBOL_STRING_RANGE,
    TB_SYMBOL_STRING_READ,
    TB_SYMBOL_STRING_UNTERMINATED
} TbSymbolStringFailure_e;

typedef struct {
    TbSymbolStringFailure_e reason;
    uint64_t                offset;
    TbSymbolFileFailure_t    read;
} TbSymbolStringFailure_t;

/* One explicit bounded string in retained uncompressed metadata. Caller proves
 * the section/attribute relationship. Borrows directory/descriptor/content lease;
 * no acquisition/discovery or I/O in creation. end_offset is an exclusive checked
 * section-relative bound, never an implicit default. All owned state and reader
 * scratch use the required construction account. No full-string allocation. */
TbSymbolString_t *tb_symbol_string_create(TbSymbolSectionsView_t directory,
                                         TbSymbolStringSelection_t selection,
                                         TbSymbolBudget_t *construction);
/* One bounded reader/profile/scan action, max_bytes 64..65536. Outer job owns
 * cancellation, deadlines, consumer interest and captured context revisions. */
TbSymbolOutcome_e tb_symbol_string_step(TbSymbolString_t *string, size_t max_bytes);
/* Complete termination proof only, borrowed until destruction. bytes excludes
 * the terminator; end_offset retains the captured scan bound. Original bytes,
 * not locale/Unicode interpretation, remain authoritative. Raw coordinates do
 * not imply semantic names, source correspondence, frontend ownership or runtime
 * authority. No candidate prefix on failure/cancellation or before completion. */
bool tb_symbol_string_view(const TbSymbolString_t *string, TbSymbolStringView_t *view);
TbSymbolStringFailure_t tb_symbol_string_failure(const TbSymbolString_t *string);
uint64_t tb_symbol_string_progress(const TbSymbolString_t *string);
void tb_symbol_string_destroy(TbSymbolString_t *string);

#endif // THE_BUGGER_SYMBOL_STRINGS_H
