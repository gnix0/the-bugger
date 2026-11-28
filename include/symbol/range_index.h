#ifndef THE_BUGGER_SYMBOL_RANGE_INDEX_H
#define THE_BUGGER_SYMBOL_RANGE_INDEX_H

#include "symbol/ranges.h"

typedef struct {
    uint64_t            offset;
    uint64_t            end_offset;
    uint64_t            list_end;
    TbSymbolRangeKind_e kind;
} TbSymbolRangeBoundary_t;

typedef struct {
    uint64_t            offset;
    uint64_t            end_offset;
    TbSymbolByteOrder_e byte_order;
    unsigned            version;
    unsigned            address_bytes;
    unsigned            segment_bytes;
} TbSymbolRangeIndexRequest_t;

typedef struct {
    TbSymbolRangeIndexRequest_t     request;
    const TbSymbolRangeBoundary_t *entries;
    size_t                         count;
} TbSymbolRangeIndexView_t;

typedef enum {
    TB_SYMBOL_RANGE_INDEX_PENDING,
    TB_SYMBOL_RANGE_INDEX_COMPLETE,
    TB_SYMBOL_RANGE_INDEX_INVALID,
    TB_SYMBOL_RANGE_INDEX_MALFORMED,
    TB_SYMBOL_RANGE_INDEX_UNSUPPORTED,
    TB_SYMBOL_RANGE_INDEX_RESOURCE
} TbSymbolRangeIndexStatus_e;

/* Pure structural framing, not range interpretation. Borrow stationary caller
 * state/arena through discard. For DWARF5 select exactly one proved contribution's
 * list area; for DWARF4 the caller establishes the scope start and CU address
 * width. A legacy section need not have one global width. Retain same-content,
 * section/contribution and unit evidence independently. No allocation or I/O.
 * Every encoded entry is retained, including empty lists/padding. A reference
 * may select a suffix at an entry boundary; it never inherits an omitted base. */
typedef struct {
    TbSymbolRangeIndexView_t   view;
    TbSymbolRangeBoundary_t   *entries;
    TbSymbolRangeBoundary_t    candidate;
    TbSymbolLeb_t              leb;
    TbSymbolRangeIndexStatus_e status;
    uint64_t                  cursor;
    uint64_t                  progress;
    uint64_t                  failure_offset;
    size_t                    capacity;
    size_t                    list_begin;
    size_t                    linking;
    unsigned                  phase;
    unsigned                  operand;
    unsigned                  operands;
    unsigned                  remaining;
    bool                      first_zero;
    bool                      second_zero;
    bool                      first_maximum;
    bool                      eof;
    bool                      initialized;
} TbSymbolRangeIndex_t;

TbSymbolRangeIndexStatus_e tb_symbol_range_index_init(TbSymbolRangeIndex_t *index,
    TbSymbolRangeIndexRequest_t request, TbSymbolRangeBoundary_t *entries, size_t capacity);
/* At most 256 byte/record/link actions; may consume zero bytes during linking.
 * A wide terminated ULEB remains frameable without a numeric value. Unknown
 * opcodes are unsupported, never guessed/skipped. Stops at the exact scope end.
 * Outer jobs own revisions, interests, cancellation and absolute deadlines. */
TbSymbolRangeIndexStatus_e tb_symbol_range_index_feed(TbSymbolRangeIndex_t *index,
    const void *bytes, size_t length, size_t *consumed);
/* At actual input EOF. Completes pending linking in bounded slices; unfinished
 * operands or a missing end marker are malformed. */
TbSymbolRangeIndexStatus_e tb_symbol_range_index_finish(TbSymbolRangeIndex_t *index);
/* COMPLETE only; failures expose no trusted prefix. Borrowed until discard.
 * Boundaries establish framing only: no base/index resolution, interval arithmetic,
 * non-overlap, executable storage, source correspondence or runtime authority. */
bool tb_symbol_range_index_view(const TbSymbolRangeIndex_t *index, TbSymbolRangeIndexView_t *view);
/* Bounded binary search (at most size_t bit width) over a complete view.
 * Reject operand-interior and scope-end offsets; preserve output on failure. */
bool tb_symbol_range_index_find(TbSymbolRangeIndexView_t view, uint64_t offset,
    TbSymbolRangeBoundary_t *boundary);
void tb_symbol_range_index_discard(TbSymbolRangeIndex_t *index);

#endif // THE_BUGGER_SYMBOL_RANGE_INDEX_H
