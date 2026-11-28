#ifndef THE_BUGGER_SYMBOL_RANGES_H
#define THE_BUGGER_SYMBOL_RANGES_H

#include "symbol/indexed_tables.h"
#include "symbol/leb.h"

typedef enum {
    TB_SYMBOL_RANGE_END,
    TB_SYMBOL_RANGE_BASE_INDEX,
    TB_SYMBOL_RANGE_INDEX_PAIR,
    TB_SYMBOL_RANGE_INDEX_LENGTH,
    TB_SYMBOL_RANGE_OFFSET_PAIR,
    TB_SYMBOL_RANGE_BASE,
    TB_SYMBOL_RANGE_ADDRESS_PAIR,
    TB_SYMBOL_RANGE_ADDRESS_LENGTH
} TbSymbolRangeKind_e;

typedef struct {
    uint64_t            offset;
    uint64_t            end_offset;
    TbSymbolRangeKind_e kind;
    uint64_t            operands[2];
    uint64_t            operand_offsets[2];
    uint64_t            operand_bytes[2];
    uint64_t            addresses[2];
    unsigned            operand_count;
    bool                indexed[2];
    bool                resolved[2];
} TbSymbolRangeRecord_t;

typedef struct {
    uint64_t begin;
    uint64_t end;
    uint64_t base;
    uint64_t base_record;
    size_t   record_index;
    bool     relative;
} TbSymbolRange_t;

typedef struct {
    uint64_t               offset;
    uint64_t               end_offset;
    uint64_t               base;
    TbSymbolByteOrder_e    byte_order;
    unsigned               version;
    unsigned               address_bytes;
    unsigned               segment_bytes;
    bool                   base_available;
    bool                   addresses_available;
    TbSymbolIndexedTable_t addresses;
} TbSymbolRangesRequest_t;

typedef enum {
    TB_SYMBOL_RANGES_PENDING,
    TB_SYMBOL_RANGES_ADDRESS,
    TB_SYMBOL_RANGES_COMPLETE,
    TB_SYMBOL_RANGES_INVALID,
    TB_SYMBOL_RANGES_MALFORMED,
    TB_SYMBOL_RANGES_UNSUPPORTED,
    TB_SYMBOL_RANGES_DEPENDENCY,
    TB_SYMBOL_RANGES_RESOURCE
} TbSymbolRangesStatus_e;

typedef struct {
    TbSymbolRangesRequest_t      request;
    const TbSymbolRangeRecord_t *records;
    const TbSymbolRange_t       *ranges;
    size_t                       record_count;
    size_t                       range_count;
    uint64_t                     end_offset;
} TbSymbolRangesView_t;

typedef struct {
    TbSymbolRangesStatus_e status;
    uint64_t               offset;
    uint64_t               record_offset;
    size_t                 record_index;
    size_t                 conflicting_record;
    unsigned               operand;
} TbSymbolRangesFailure_t;

/* Allocation/I/O-free, stationary caller-owned/accounted state and full arenas.
 * Inputs, outputs and state do not alias. Request offset is an explicitly
 * selected list start; end_offset is its checked enclosing section/contribution
 * bound, not a guessed list length. Caller proves physical list boundaries,
 * section roles, relocations, associated CU/base and address-table relationships
 * against the same pinned frozen content. No source/runtime/access claims.
 * Both versions retain every base/end record and zero-length interval. DWARF4
 * records retain two encoded operands even for normalized BASE/END kinds.
 * base_available distinguishes a validated zero base from an absent CU base. */
typedef struct {
    TbSymbolRangesView_t    view;
    TbSymbolRangeRecord_t  *records;
    TbSymbolRange_t        *ranges;
    TbSymbolRangeRecord_t   candidate;
    TbSymbolLeb_t           leb;
    TbSymbolRangesFailure_t failure;
    TbSymbolRangesStatus_e  status;
    uint64_t                cursor;
    uint64_t                progress;
    uint64_t                accumulator;
    uint64_t                base;
    uint64_t                base_record;
    uint64_t                last_end;
    size_t                  record_capacity;
    size_t                  range_capacity;
    size_t                  build;
    size_t                  remaining;
    size_t                  heap;
    size_t                  root;
    size_t                  checking;
    size_t                  last_record;
    unsigned                phase;
    unsigned                operand;
    unsigned                received;
    bool                    base_available;
    bool                    sifting;
    bool                    previous;
    bool                    eof;
    bool                    initialized;
} TbSymbolRanges_t;

TbSymbolRangesStatus_e tb_symbol_ranges_init(TbSymbolRanges_t *ranges, TbSymbolRangesRequest_t request,
    TbSymbolRangeRecord_t *records, size_t record_capacity, TbSymbolRange_t *intervals,
    size_t range_capacity);
/* At most 256 byte/interpretation/heap/overlap actions. Internal work may
 * consume zero bytes; compare progress. Stops at the end marker without
 * consuming any next-list bytes. Sorting is O(n log n), with bounded sifts.
 * Outer jobs own revisions, cancellation, interests and absolute deadlines. */
TbSymbolRangesStatus_e tb_symbol_ranges_feed(TbSymbolRanges_t *ranges, const void *bytes,
                                             size_t length, size_t *consumed);
/* Declare input EOF and perform one bounded service slice. May remain PENDING
 * while sorting, or ADDRESS while awaiting an explicit dependency. Further byte
 * input after EOF is invalid; incomplete framing is malformed. */
TbSymbolRangesStatus_e tb_symbol_ranges_finish(TbSymbolRanges_t *ranges);
/* ADDRESS only. Exact requested index in the captured validated address table.
 * No I/O or implicit section discovery. Wrong supplies preserve the request;
 * NULL reports an unavailable dependency. A successful entry remains retained
 * as numeric/physical evidence in the candidate record and captured table. */
bool tb_symbol_ranges_address_need(const TbSymbolRanges_t *ranges, uint64_t *index);
TbSymbolRangesStatus_e tb_symbol_ranges_address(TbSymbolRanges_t *ranges,
                                                const TbSymbolIndexedEntry_t *entry);
/* COMPLETE only, borrowed through discard. Intervals sorted by begin/end and
 * physical record ordinal; unordered input is valid, overlap is malformed.
 * Empty intervals remain distinct and do not overlap anything. Raw records
 * retain encoded order. Failure/cancellation never exposes a successful prefix.
 * Neither interval ordering nor first address proves a standalone entry point. */
bool tb_symbol_ranges_view(const TbSymbolRanges_t *ranges, TbSymbolRangesView_t *view);
bool tb_symbol_ranges_failure(const TbSymbolRanges_t *ranges, TbSymbolRangesFailure_t *failure);
uint64_t tb_symbol_ranges_progress(const TbSymbolRanges_t *ranges);
void tb_symbol_ranges_discard(TbSymbolRanges_t *ranges);

#endif // THE_BUGGER_SYMBOL_RANGES_H
