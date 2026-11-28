#ifndef THE_BUGGER_SYMBOL_SYMBOLS_H
#define THE_BUGGER_SYMBOL_SYMBOLS_H

#include "symbol/sections.h"
#include "symbol/identity.h"

typedef struct tb_symbol_symbols TbSymbolSymbols_t;

typedef struct {
    uint64_t section_index;
    int      descriptor;
} TbSymbolTableSelection_t;

typedef enum {
    TB_SYMBOL_ELF_REF_UNDEFINED,
    TB_SYMBOL_ELF_REF_ORDINARY,
    TB_SYMBOL_ELF_REF_ABSOLUTE,
    TB_SYMBOL_ELF_REF_COMMON,
    TB_SYMBOL_ELF_REF_RESERVED
} TbSymbolElfReference_e;

typedef struct {
    TbSymbolElfSymbol_t    raw;
    uint64_t              record_offset;
    TbSymbolElfReference_e reference;
    uint64_t              section_index;
    uint64_t              name_offset;
    uint64_t              name_bytes;
    bool                  has_name;
} TbSymbolElfEntry_t;

typedef struct {
    const TbSymbolElfEntry_t *entries;
    uint64_t                 count;
    uint64_t                 table_section;
    TbSymbolRecordKind_e     kind;
} TbSymbolSymbolsView_t;

typedef enum {
    TB_SYMBOL_SYMBOLS_NONE,
    TB_SYMBOL_SYMBOLS_REQUEST,
    TB_SYMBOL_SYMBOLS_TABLE,
    TB_SYMBOL_SYMBOLS_STRINGS,
    TB_SYMBOL_SYMBOLS_INDEX_TABLE,
    TB_SYMBOL_SYMBOLS_INDEX_MISSING,
    TB_SYMBOL_SYMBOLS_INDEX_CONFLICT,
    TB_SYMBOL_SYMBOLS_INDEX_VALUE,
    TB_SYMBOL_SYMBOLS_NULL_ENTRY,
    TB_SYMBOL_SYMBOLS_LOCAL_ORDER,
    TB_SYMBOL_SYMBOLS_NAME,
    TB_SYMBOL_SYMBOLS_READ,
    TB_SYMBOL_SYMBOLS_RESOURCE
} TbSymbolSymbolsFailure_e;

typedef struct {
    TbSymbolSymbolsFailure_e reason;
    uint64_t                record_index;
    TbSymbolFileFailure_t   read;
    int                     system_error;
} TbSymbolSymbolsFailure_t;

/* A complete selected SYMTAB/DYNSYM scope, not a language-semantic catalog.
 * Borrows the complete directory and its exact backing descriptor/content lease
 * until destruction. All owned memory is charged to the required caller budget.
 * Construction performs no I/O. The directory's backing identity is retained;
 * no reopened pathname or same-sized replacement can supply these bytes. */
TbSymbolSymbols_t *tb_symbol_symbols_create(TbSymbolSectionsView_t directory,
                                           TbSymbolTableSelection_t selection,
                                           TbSymbolBudget_t *construction);
/* One bounded reader/record/name/dependency action; max_bytes is 64..65536.
 * The outer job owns identity, cancellation, absolute deadlines and supervision. */
TbSymbolOutcome_e tb_symbol_symbols_step(TbSymbolSymbols_t *symbols, size_t max_bytes);
/* Complete scope only, borrowed until destruction. Includes string boundaries,
 * names, null/local ordering and supported section-index dependencies. Unknown
 * framed symbol types/bindings and all st_other bits remain raw. A resolved
 * ordinary index is meaningful only for REF_ORDINARY; special raw indexes remain
 * in raw.section_index. Extended ordinary indexes never become special markers.
 * No type representation, storage/code footprint, runtime binding or access
 * permission follows. Physical identity still needs an admitted content ID.
 * Unsupported compression exposes no guessed records; callers retain coverage
 * limitations rather than treating unsupported as a complete empty inventory. */
bool tb_symbol_symbols_view(const TbSymbolSymbols_t *symbols, TbSymbolSymbolsView_t *view);
TbSymbolSymbolsFailure_t tb_symbol_symbols_failure(const TbSymbolSymbols_t *symbols);
uint64_t tb_symbol_symbols_progress(const TbSymbolSymbols_t *symbols);
void tb_symbol_symbols_destroy(TbSymbolSymbols_t *symbols);

#endif // THE_BUGGER_SYMBOL_SYMBOLS_H
