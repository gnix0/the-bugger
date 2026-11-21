#ifndef THE_BUGGER_SYMBOL_UNITS_H
#define THE_BUGGER_SYMBOL_UNITS_H

#include "symbol/dwarf.h"
#include "symbol/sections.h"

typedef struct tb_symbol_units TbSymbolUnits_t;

typedef struct {
    uint64_t               section_index;
    TbSymbolDwarfSection_e kind;
    int                    descriptor;
} TbSymbolUnitSelection_t;

typedef struct {
    uint64_t               offset;
    TbSymbolDwarfLength_t   extent;
    TbSymbolDwarfUnit_t     header;
    bool                   header_available;
} TbSymbolUnitEntry_t;

typedef struct {
    const TbSymbolUnitEntry_t *entries;
    uint64_t                  count;
    uint64_t                  unsupported;
    uint64_t                  section_index;
    TbSymbolDwarfSection_e    kind;
    TbSymbolStorageInfo_t      backing;
} TbSymbolUnitsView_t;

typedef enum {
    TB_SYMBOL_UNITS_NONE,
    TB_SYMBOL_UNITS_REQUEST,
    TB_SYMBOL_UNITS_SECTION,
    TB_SYMBOL_UNITS_READ,
    TB_SYMBOL_UNITS_LENGTH,
    TB_SYMBOL_UNITS_HEADER,
    TB_SYMBOL_UNITS_RESOURCE,
    TB_SYMBOL_UNITS_RECONSTRUCTION
} TbSymbolUnitsFailure_e;

typedef struct {
    TbSymbolUnitsFailure_e reason;
    uint64_t               unit_offset;
    TbSymbolFileFailure_t   read;
    int                    system_error;
} TbSymbolUnitsFailure_t;

/* Complete header inventory for one explicitly classified info/types section.
 * Borrows the complete directory, exact descriptor and content lease through
 * destruction. The caller establishes section classification; no filename,
 * filesystem discovery or DWARF-absence inference occurs. All owned state,
 * scratch and exact entry capacity require the caller's construction account.
 * Creation performs no I/O and selects no production cache default. */
TbSymbolUnits_t *tb_symbol_units_create(TbSymbolSectionsView_t directory,
                                       TbSymbolUnitSelection_t selection, TbSymbolBudget_t *construction);
/* One bounded reader/contribution/construction action, max_bytes 64..65536.
 * Two passes count/reserve then populate; the outer job owns cancellation,
 * monotonic deadlines, process and consumer generations. */
TbSymbolOutcome_e tb_symbol_units_step(TbSymbolUnits_t *units, size_t max_bytes);
/* Complete selected scope only, borrowed until destruction. Every entry has
 * checked framing; header fields are available only when header_available.
 * Unsupported framed units remain represented with explicit coverage count.
 * Failure exposes no prefix. Empty sections still verify the exact descriptor.
 * These are header facts, not DIE/attribute/reference validation or independent
 * frontend guarantees. Physical identity still needs the admitted content ID. */
bool tb_symbol_units_view(const TbSymbolUnits_t *units, TbSymbolUnitsView_t *view);
TbSymbolUnitsFailure_t tb_symbol_units_failure(const TbSymbolUnits_t *units);
uint64_t tb_symbol_units_progress(const TbSymbolUnits_t *units);
void tb_symbol_units_destroy(TbSymbolUnits_t *units);

#endif // THE_BUGGER_SYMBOL_UNITS_H
