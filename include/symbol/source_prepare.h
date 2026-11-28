#ifndef THE_BUGGER_SYMBOL_SOURCE_PREPARE_H
#define THE_BUGGER_SYMBOL_SOURCE_PREPARE_H

#include "symbol/code_locations.h"
#include "symbol/constants.h"
#include "symbol/source_context.h"

typedef enum {
    TB_SYMBOL_SOURCE_PREPARE_PENDING,
    TB_SYMBOL_SOURCE_PREPARE_READY,
    TB_SYMBOL_SOURCE_PREPARE_ABSENT,
    TB_SYMBOL_SOURCE_PREPARE_AMBIGUOUS,
    TB_SYMBOL_SOURCE_PREPARE_DEPENDENCY,
    TB_SYMBOL_SOURCE_PREPARE_UNSUPPORTED,
    TB_SYMBOL_SOURCE_PREPARE_INVALID,
    TB_SYMBOL_SOURCE_PREPARE_RESOURCE
} TbSymbolSourcePrepareStatus_e;

typedef struct {
    TbSymbolConstantView_t   file;
    TbSymbolConstantView_t   line;
    TbSymbolConstantView_t   column;
    TbSymbolSourceFileKind_e kind;
    bool                     column_available;
} TbSymbolSourceCoordinates_t;

/* Compose complete validated declaration or direct call constants. Independent
 * inherited owners must already have been selected through their retained graph;
 * this function neither follows relationships nor selects among alternatives.
 * File/line/column must belong to the SAME physical unit's numbering context;
 * cross-unit mixtures are AMBIGUOUS. Every original attribute remains retained.
 * Zero file/line is ABSENT, not file zero or a nearest-line proposal. */
TbSymbolSourcePrepareStatus_e tb_symbol_source_coordinates(TbSymbolConstantView_t file,
    TbSymbolConstantView_t line, const TbSymbolConstantView_t *column, TbSymbolSourceCoordinates_t *view);

typedef struct {
    TbSymbolRangesView_t          ranges;
    TbSymbolRangeIndexView_t      index;
    const TbSymbolIndexedTable_t *table;
} TbSymbolSourceRanges_t;

typedef struct {
    TbSymbolSourceContextView_t        context;
    TbSymbolLineView_t                 program;
    const TbSymbolSourceCoordinates_t *coordinates;
    const TbSymbolCodeLocationsView_t *code;
    const TbSymbolSourceRanges_t      *ranges;
    size_t                             path_capacity;
    uint64_t                           line_limit;
} TbSymbolSourcePrepareRequest_t;

/* Exact artifact row coordinates, never a runtime/instruction-boundary proof.
 * Repeated addresses and VLIW operation indexes retain distinct row identities.
 * range is the decoded interval ordinal, SIZE_MAX for a direct low/high pair. */
typedef struct {
    TbSymbolLineRow_t position;
    size_t            row;
    size_t            range;
} TbSymbolSourceLocation_t;

typedef struct {
    TbSymbolSourcePrepareRequest_t  request;
    const TbSymbolSourceLocation_t *locations;
    size_t                          count;
    uint64_t                        line;
    uint64_t                        column;
} TbSymbolSourcePrepareView_t;

/* Pure caller-owned/accounted metadata preparation. Retain complete context,
 * program, coordinates, code/range evidence and buffers unchanged through
 * discard. Input/state/arena do not overlap. No production capacity defaults,
 * allocation, I/O, Source opening, MI or frontend publication. With no code,
 * prepare recorded Source browsing only (zero compiled locations). With code,
 * match exact file+line rows against its explicit nonempty half-open ranges;
 * no first-range entry point, basename or nearby-line fallback. */
typedef struct {
    TbSymbolSourcePrepareView_t   view;
    TbSymbolSourceLocation_t     *arena;
    size_t                        capacity;
    size_t                        cursor;
    size_t                        range_cursor;
    uint64_t                      progress;
    TbSymbolSourcePrepareStatus_e status;
    bool                          row_pending;
    bool                          initialized;
} TbSymbolSourcePrepare_t;

/* path_capacity includes NUL; line_limit is the receiving consumer's supported
 * line representation. Existing Source uses TB_CODE_PATH and INT_MAX. Oversize
 * paths/lines fail without truncation or narrowing. Failed init preserves state.
 * Ranges must be the complete semantic decode of the EXACT derived code list;
 * callers retain the selected section's content/relocation evidence. */
TbSymbolSourcePrepareStatus_e tb_symbol_source_prepare_init(TbSymbolSourcePrepare_t *prepare,
    TbSymbolSourcePrepareRequest_t request, TbSymbolSourceLocation_t *arena, size_t capacity);
/* 1..256 row/range/transition actions. No successful prefix on exhaustion or
 * cancellation; discard candidate state, leaving independent publications alone.
 * Outer job owns revisions, consumer interest, cancellation and deadlines. */
TbSymbolSourcePrepareStatus_e tb_symbol_source_prepare_step(TbSymbolSourcePrepare_t *prepare,
    size_t max_steps);
bool tb_symbol_source_prepare_view(const TbSymbolSourcePrepare_t *prepare, TbSymbolSourcePrepareView_t *view);
uint64_t tb_symbol_source_prepare_progress(const TbSymbolSourcePrepare_t *prepare);
void tb_symbol_source_prepare_discard(TbSymbolSourcePrepare_t *prepare);

#endif // THE_BUGGER_SYMBOL_SOURCE_PREPARE_H
