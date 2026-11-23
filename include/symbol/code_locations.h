#ifndef THE_BUGGER_SYMBOL_CODE_LOCATIONS_H
#define THE_BUGGER_SYMBOL_CODE_LOCATIONS_H

#include "symbol/range_index.h"
#include "symbol/tables.h"

typedef struct {
    TbSymbolCodeView_t                code;
    TbSymbolCodeView_t                root;
    TbSymbolByteOrder_e               byte_order;
    const TbSymbolTableBindingView_t *addresses;
    const TbSymbolTableValueView_t   *low;
    const TbSymbolTableValueView_t   *high;
    const TbSymbolTableValueView_t   *root_low;
    const TbSymbolTableValueView_t   *range;
} TbSymbolCodeLocationsRequest_t;

typedef enum {
    TB_SYMBOL_CODE_LOCATIONS_COMPLETE,
    TB_SYMBOL_CODE_LOCATIONS_DEPENDENCY,
    TB_SYMBOL_CODE_LOCATIONS_MALFORMED,
    TB_SYMBOL_CODE_LOCATIONS_UNSUPPORTED,
    TB_SYMBOL_CODE_LOCATIONS_INVALID
} TbSymbolCodeLocationsStatus_e;

typedef struct {
    TbSymbolCodeView_t         code;
    TbSymbolCodeView_t         root;
    TbSymbolByteOrder_e        byte_order;
    TbSymbolTableBindingView_t addresses;
    TbSymbolTableValueView_t   dependencies[4];
    uint64_t                  low;
    uint64_t                  end;
    uint64_t                  base;
    uint64_t                  list_offset;
    bool                      dependency_available[4];
    bool                      addresses_available;
    bool                      low_available;
    bool                      interval_available;
    bool                      base_available;
    bool                      list_available;
} TbSymbolCodeLocationsView_t;

/* Pure, constant bounded composition of complete direct code/root views and
 * selected entries. The caller retains/accounts all input evidence and output.
 * Byte order comes from the same retained ELF directory.
 * Exact same-unit root and root-base binding required; no inherited/fallback
 * names, inferred zero bases or runtime mappings. Missing required indexed entries are
 * DEPENDENCY. Wrong physical bindings are INVALID. Failed outputs unchanged.
 * COMPLETE means resolved metadata, including absence, not executable storage,
 * instruction/standalone-entry proof, Source preparation or action authority. */
TbSymbolCodeLocationsStatus_e tb_symbol_code_locations_prepare(TbSymbolCodeLocationsRequest_t request,
    TbSymbolCodeLocationsView_t *view);
/* Compose an exact list-entry boundary with the selected location and CU base.
 * The complete index must cover the same retained section: for v5, exactly the
 * supplied proved contribution's list area; for v4, an explicitly established
 * same-width scope. Caller retains physical section/content/relocation evidence.
 * A rnglistx dependency must select that exact table. Semantic decoding still
 * checks arithmetic, index dependencies and overlap before any interval view.
 * No I/O, allocation, runtime access or first-range-as-entry inference. */
TbSymbolCodeLocationsStatus_e tb_symbol_code_locations_ranges(TbSymbolCodeLocationsView_t locations,
    TbSymbolRangeIndexView_t index, const TbSymbolIndexedTable_t *table, TbSymbolRangesRequest_t *request);

#endif // THE_BUGGER_SYMBOL_CODE_LOCATIONS_H
