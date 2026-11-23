#include "symbol/ranges.h"

#include <string.h>

enum {
    RANGES_RECORD,
    RANGES_OPCODE,
    RANGES_FIXED,
    RANGES_LEB,
    RANGES_APPLY,
    RANGES_SORT,
    RANGES_CHECK
};

static uint64_t maximum(const TbSymbolRanges_t *ranges)
{
    unsigned width = ranges->view.request.address_bytes;
    return width == 8 ? UINT64_MAX : (UINT64_C(1) << (8U * width)) - 1;
}


static void fail(TbSymbolRanges_t *ranges, TbSymbolRangesStatus_e status)
{
    ranges->status  = status;
    ranges->failure = (TbSymbolRangesFailure_t) {
        .status             = status,
        .offset             = ranges->cursor,
        .record_offset      = ranges->candidate.offset,
        .record_index       = ranges->view.record_count,
        .conflicting_record = SIZE_MAX,
        .operand            = ranges->operand
    };
}


TbSymbolRangesStatus_e tb_symbol_ranges_init(TbSymbolRanges_t *ranges, TbSymbolRangesRequest_t request,
    TbSymbolRangeRecord_t *records, size_t record_capacity, TbSymbolRange_t *intervals, size_t range_capacity)
{
    if (!ranges || !records || !intervals || record_capacity > SIZE_MAX / sizeof(*records) ||
        range_capacity > SIZE_MAX / sizeof(*intervals) || request.offset >= request.end_offset ||
        (request.byte_order != TB_SYMBOL_LITTLE_ENDIAN && request.byte_order != TB_SYMBOL_BIG_ENDIAN)) {
        return TB_SYMBOL_RANGES_INVALID;
    }

    if ((request.version != 4 && request.version != 5) || request.segment_bytes ||
        (request.address_bytes != 1 && request.address_bytes != 2 &&
         request.address_bytes != 4 && request.address_bytes != 8)) {
        return TB_SYMBOL_RANGES_UNSUPPORTED;
    }

    if (request.addresses_available &&
        (request.addresses.kind != TB_SYMBOL_TABLE_ADDRESSES || request.addresses.segment_bytes ||
         request.addresses.address_bytes != request.address_bytes ||
         request.addresses.byte_order != request.byte_order)) {
        return TB_SYMBOL_RANGES_INVALID;
    }

    if (request.base_available && request.address_bytes < 8 &&
        request.base >> (8U * request.address_bytes)) {
        return TB_SYMBOL_RANGES_INVALID;
    }

    *ranges = (TbSymbolRanges_t) {
        .view = {
            .request = request,
            .records = records,
            .ranges  = intervals
        },
        .records         = records,
        .ranges          = intervals,
        .status          = TB_SYMBOL_RANGES_PENDING,
        .cursor          = request.offset,
        .base            = request.base,
        .base_record     = UINT64_MAX,
        .record_capacity = record_capacity,
        .range_capacity  = range_capacity,
        .base_available  = request.base_available,
        .initialized     = true
    };

    return ranges->status;
}


static void prepare_operand(TbSymbolRanges_t *ranges)
{
    TbSymbolRangeKind_e kind = ranges->candidate.kind;
    bool fixed = ranges->view.request.version == 4 || kind == TB_SYMBOL_RANGE_BASE ||
                 kind == TB_SYMBOL_RANGE_ADDRESS_PAIR ||
                 (kind == TB_SYMBOL_RANGE_ADDRESS_LENGTH && ranges->operand == 0);

    ranges->candidate.operand_offsets[ranges->operand] = ranges->cursor;

    ranges->received    = 0;
    ranges->accumulator = 0;
    ranges->phase       = fixed ? RANGES_FIXED : RANGES_LEB;

    if (!fixed)
        (void)tb_symbol_leb_init(&ranges->leb, false);
}


static void start_record(TbSymbolRanges_t *ranges)
{
    ranges->candidate = (TbSymbolRangeRecord_t) {
        .offset        = ranges->cursor,
        .kind          = TB_SYMBOL_RANGE_OFFSET_PAIR,
        .operand_count = 2
    };
    ranges->operand = 0;

    if (ranges->cursor == ranges->view.request.end_offset) {
        fail(ranges, TB_SYMBOL_RANGES_MALFORMED);
        return;
    }

    if (ranges->view.record_count == ranges->record_capacity) {
        fail(ranges, TB_SYMBOL_RANGES_RESOURCE);
        return;
    }

    if (ranges->view.request.version == 4)
        prepare_operand(ranges);
    else
        ranges->phase = RANGES_OPCODE;
}


static void opcode(TbSymbolRanges_t *ranges, unsigned char byte)
{
    if (byte > TB_SYMBOL_RANGE_ADDRESS_LENGTH) {
        fail(ranges, TB_SYMBOL_RANGES_UNSUPPORTED);
        return;
    }

    TbSymbolRangeRecord_t *record = &ranges->candidate;
    record->kind          = (TbSymbolRangeKind_e)byte;
    record->operand_count = byte == TB_SYMBOL_RANGE_END ? 0 :
                            (byte == TB_SYMBOL_RANGE_BASE || byte == TB_SYMBOL_RANGE_BASE_INDEX ? 1 : 2);
    record->indexed[0]    = byte == TB_SYMBOL_RANGE_BASE_INDEX || byte == TB_SYMBOL_RANGE_INDEX_PAIR ||
                            byte == TB_SYMBOL_RANGE_INDEX_LENGTH;
    record->indexed[1]    = byte == TB_SYMBOL_RANGE_INDEX_PAIR;

    if (record->operand_count)
        prepare_operand(ranges);
    else
        ranges->phase = RANGES_APPLY;
}


static void complete_operand(TbSymbolRanges_t *ranges, uint64_t value)
{
    ranges->candidate.operands[ranges->operand] = value;
    if (++ranges->operand < ranges->candidate.operand_count) {
        prepare_operand(ranges);
    } else {
        if (ranges->view.request.version == 4) {
            if (ranges->candidate.operands[0] == maximum(ranges))
                ranges->candidate.kind = TB_SYMBOL_RANGE_BASE;
            else if (!ranges->candidate.operands[0] && !ranges->candidate.operands[1])
                ranges->candidate.kind = TB_SYMBOL_RANGE_END;
        }
        ranges->phase = RANGES_APPLY;
    }
}


static void operand_byte(TbSymbolRanges_t *ranges, unsigned char byte)
{
    ++ranges->candidate.operand_bytes[ranges->operand];
    if (ranges->phase == RANGES_FIXED) {
        unsigned shift = ranges->view.request.byte_order == TB_SYMBOL_LITTLE_ENDIAN ?
                             ranges->received : ranges->view.request.address_bytes - 1 - ranges->received;
        ranges->accumulator |= (uint64_t)byte << (8U * shift);
        if (++ranges->received == ranges->view.request.address_bytes)
            complete_operand(ranges, ranges->accumulator);
        return;
    }

    size_t               consumed;
    TbSymbolReadStatus_e status = tb_symbol_leb_feed(&ranges->leb, &byte, 1, &consumed);
    if (status == TB_SYMBOL_READ_LIMIT)
        return;

    uint64_t value;
    if (status != TB_SYMBOL_READ_OK || !tb_symbol_leb_uint(&ranges->leb, &value))
        fail(ranges, TB_SYMBOL_RANGES_UNSUPPORTED);
    else
        complete_operand(ranges, value);
}


static bool resolve_indexes(TbSymbolRanges_t *ranges)
{
    for (unsigned i = 0; i < ranges->candidate.operand_count; ++i) {
        if (!ranges->candidate.indexed[i] || ranges->candidate.resolved[i])
            continue;

        ranges->operand = i;
        if (!ranges->view.request.addresses_available) {
            fail(ranges, TB_SYMBOL_RANGES_DEPENDENCY);
        } else if (ranges->candidate.operands[i] >= ranges->view.request.addresses.count) {
            fail(ranges, TB_SYMBOL_RANGES_MALFORMED);
        } else {
            ranges->status = TB_SYMBOL_RANGES_ADDRESS;
        }
        return false;
    }

    return true;
}


static void apply(TbSymbolRanges_t *ranges)
{
    if (!resolve_indexes(ranges))
        return;

    TbSymbolRangeRecord_t *record = &ranges->candidate;
    uint64_t               first  = record->indexed[0] ? record->addresses[0] : record->operands[0];
    uint64_t               second = record->indexed[1] ? record->addresses[1] : record->operands[1];

    record->end_offset = ranges->cursor;

    if (record->kind == TB_SYMBOL_RANGE_END) {
        ranges->view.end_offset = ranges->cursor;
        ranges->build           = ranges->view.range_count / 2;
        ranges->remaining       = ranges->view.range_count;
        ranges->phase           = RANGES_SORT;
    } else if (record->kind == TB_SYMBOL_RANGE_BASE || record->kind == TB_SYMBOL_RANGE_BASE_INDEX) {
        ranges->base           = ranges->view.request.version == 4 ? second : first;
        ranges->base_record    = record->offset;
        ranges->base_available = true;
        ranges->phase          = RANGES_RECORD;
    } else {
        if (record->kind == TB_SYMBOL_RANGE_OFFSET_PAIR) {
            if (!ranges->base_available) {
                fail(ranges, TB_SYMBOL_RANGES_DEPENDENCY);
                return;
            }
            if (!tb_symbol_add(ranges->base, first, &first) ||
                !tb_symbol_add(ranges->base, second, &second)) {
                fail(ranges, TB_SYMBOL_RANGES_MALFORMED);
                return;
            }
        } else if ((record->kind == TB_SYMBOL_RANGE_INDEX_LENGTH ||
                    record->kind == TB_SYMBOL_RANGE_ADDRESS_LENGTH) &&
                   !tb_symbol_add(first, second, &second)) {
            fail(ranges, TB_SYMBOL_RANGES_MALFORMED);
            return;
        }

        if (first > second || first > maximum(ranges) || second > maximum(ranges)) {
            fail(ranges, TB_SYMBOL_RANGES_MALFORMED);
            return;
        }

        if (ranges->view.range_count == ranges->range_capacity) {
            fail(ranges, TB_SYMBOL_RANGES_RESOURCE);
            return;
        }

        ranges->ranges[ranges->view.range_count++] = (TbSymbolRange_t) {
            .begin        = first,
            .end          = second,
            .base         = record->kind == TB_SYMBOL_RANGE_OFFSET_PAIR ? ranges->base : 0,
            .base_record  = record->kind == TB_SYMBOL_RANGE_OFFSET_PAIR ? ranges->base_record : UINT64_MAX,
            .record_index = ranges->view.record_count,
            .relative     = record->kind == TB_SYMBOL_RANGE_OFFSET_PAIR
        };
        ranges->phase = RANGES_RECORD;
    }

    ranges->records[ranges->view.record_count++] = *record;
}


static bool less(TbSymbolRange_t left, TbSymbolRange_t right)
{
    if (left.begin != right.begin)
        return left.begin < right.begin;
    if (left.end != right.end)
        return left.end < right.end;

    return left.record_index < right.record_index;
}


static void exchange(TbSymbolRanges_t *ranges, size_t other)
{
    TbSymbolRange_t saved = ranges->ranges[ranges->root];
    ranges->ranges[ranges->root] = ranges->ranges[other];
    ranges->ranges[other]        = saved;
}


static void sort_step(TbSymbolRanges_t *ranges)
{
    if (ranges->sifting) {
        /* A service action performs one heap level, with child arithmetic only
         * after proving this is not a leaf. Input order is not range order. */
        if (ranges->heap < 2 || ranges->root > (ranges->heap - 2) / 2) {
            ranges->sifting = false;
            return;
        }

        size_t child = ranges->root * 2 + 1;
        if (child + 1 < ranges->heap && less(ranges->ranges[child], ranges->ranges[child + 1]))
            ++child;

        if (less(ranges->ranges[ranges->root], ranges->ranges[child])) {
            exchange(ranges, child);
            ranges->root = child;
        } else {
            ranges->sifting = false;
        }
    } else if (ranges->build) {
        ranges->root    = --ranges->build;
        ranges->heap    = ranges->view.range_count;
        ranges->sifting = true;
    } else if (ranges->remaining > 1) {
        ranges->root = 0;
        exchange(ranges, --ranges->remaining);
        ranges->heap    = ranges->remaining;
        ranges->sifting = true;
    } else {
        ranges->phase = RANGES_CHECK;
    }
}


static void check_step(TbSymbolRanges_t *ranges)
{
    if (ranges->checking == ranges->view.range_count) {
        ranges->status = TB_SYMBOL_RANGES_COMPLETE;
        return;
    }

    const TbSymbolRange_t *current = &ranges->ranges[ranges->checking++];
    if (current->begin == current->end)
        return;

    if (ranges->previous && current->begin < ranges->last_end) {
        ranges->candidate = ranges->records[current->record_index];
        fail(ranges, TB_SYMBOL_RANGES_MALFORMED);
        ranges->failure.record_index       = current->record_index;
        ranges->failure.conflicting_record = ranges->last_record;
    } else {
        ranges->last_end    = current->end;
        ranges->last_record = current->record_index;
        ranges->previous    = true;
    }
}


TbSymbolRangesStatus_e tb_symbol_ranges_feed(TbSymbolRanges_t *ranges, const void *bytes,
                                             size_t length, size_t *consumed)
{
    if (!ranges || !ranges->initialized || !consumed || (length && (!bytes || ranges->eof)))
        return TB_SYMBOL_RANGES_INVALID;

    const unsigned char *input = bytes;

    *consumed = 0;

    for (unsigned work = 0; work < 256 && ranges->status == TB_SYMBOL_RANGES_PENDING; ++work) {
        bool byte_phase = ranges->phase == RANGES_OPCODE || ranges->phase == RANGES_FIXED ||
                          ranges->phase == RANGES_LEB;
        if (byte_phase && (ranges->cursor == ranges->view.request.end_offset ||
                           (*consumed == length && ranges->eof))) {
            fail(ranges, TB_SYMBOL_RANGES_MALFORMED);
            break;
        }
        if (byte_phase && *consumed == length)
            break;

        if (ranges->progress == UINT64_MAX) {
            fail(ranges, TB_SYMBOL_RANGES_RESOURCE);
            break;
        }
        ++ranges->progress;

        if (ranges->phase == RANGES_RECORD) {
            start_record(ranges);
        } else if (ranges->phase == RANGES_APPLY) {
            apply(ranges);
        } else if (ranges->phase == RANGES_SORT) {
            sort_step(ranges);
        } else if (ranges->phase == RANGES_CHECK) {
            check_step(ranges);
        } else {
            unsigned char byte = input[(*consumed)++];
            ++ranges->cursor;
            if (ranges->phase == RANGES_OPCODE)
                opcode(ranges, byte);
            else
                operand_byte(ranges, byte);
        }
    }

    return ranges->status;
}


TbSymbolRangesStatus_e tb_symbol_ranges_finish(TbSymbolRanges_t *ranges)
{
    if (!ranges || !ranges->initialized)
        return TB_SYMBOL_RANGES_INVALID;

    ranges->eof = true;

    size_t consumed;
    return tb_symbol_ranges_feed(ranges, NULL, 0, &consumed);
}


bool tb_symbol_ranges_address_need(const TbSymbolRanges_t *ranges, uint64_t *index)
{
    if (!ranges || !ranges->initialized || !index || ranges->status != TB_SYMBOL_RANGES_ADDRESS)
        return false;

    *index = ranges->candidate.operands[ranges->operand];
    return true;
}


TbSymbolRangesStatus_e tb_symbol_ranges_address(TbSymbolRanges_t *ranges,
                                                const TbSymbolIndexedEntry_t *entry)
{
    if (!ranges || !ranges->initialized || ranges->status != TB_SYMBOL_RANGES_ADDRESS)
        return TB_SYMBOL_RANGES_INVALID;

    if (!entry) {
        fail(ranges, TB_SYMBOL_RANGES_DEPENDENCY);
        return ranges->status;
    }

    const TbSymbolIndexedTable_t *table = &ranges->view.request.addresses;
    const TbSymbolIndexedTable_t *other = &entry->table;
    uint64_t                     offset;

    if (!tb_symbol_indexed_table_offset(table, entry->index, &offset) || entry->offset != offset ||
        entry->target_offset != UINT64_MAX || entry->index != ranges->candidate.operands[ranges->operand] ||
        table->kind != other->kind || table->table_offset != other->table_offset ||
        table->entries_offset != other->entries_offset || table->lists_offset != other->lists_offset ||
        table->count != other->count || table->address_bytes != other->address_bytes ||
        table->segment_bytes != other->segment_bytes || table->byte_order != other->byte_order ||
        table->extent.end_offset != other->extent.end_offset ||
        table->extent.total_bytes != other->extent.total_bytes ||
        table->extent.payload_bytes != other->extent.payload_bytes ||
        table->extent.length_bytes != other->extent.length_bytes ||
        table->extent.offset_bytes != other->extent.offset_bytes || entry->value > maximum(ranges)) {
        return TB_SYMBOL_RANGES_INVALID;
    }

    if (ranges->progress == UINT64_MAX) {
        fail(ranges, TB_SYMBOL_RANGES_RESOURCE);
        return ranges->status;
    }

    ranges->candidate.addresses[ranges->operand] = entry->value;
    ranges->candidate.resolved[ranges->operand]  = true;

    ranges->status = TB_SYMBOL_RANGES_PENDING;
    ++ranges->progress;

    return ranges->status;
}


bool tb_symbol_ranges_view(const TbSymbolRanges_t *ranges, TbSymbolRangesView_t *view)
{
    if (!ranges || !ranges->initialized || !view || ranges->status != TB_SYMBOL_RANGES_COMPLETE)
        return false;

    *view = ranges->view;
    return true;
}


bool tb_symbol_ranges_failure(const TbSymbolRanges_t *ranges, TbSymbolRangesFailure_t *failure)
{
    if (!ranges || !ranges->initialized || !failure || ranges->status < TB_SYMBOL_RANGES_MALFORMED)
        return false;

    *failure = ranges->failure;
    return true;
}


uint64_t tb_symbol_ranges_progress(const TbSymbolRanges_t *ranges)
{
    return ranges && ranges->initialized ? ranges->progress : 0;
}


void tb_symbol_ranges_discard(TbSymbolRanges_t *ranges)
{
    if (ranges)
        memset(ranges, 0, sizeof(*ranges));
}
