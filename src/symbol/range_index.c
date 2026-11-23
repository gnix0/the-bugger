#include "symbol/range_index.h"

#include <string.h>

enum {
    RANGE_INDEX_RECORD,
    RANGE_INDEX_OPCODE,
    RANGE_INDEX_FIXED,
    RANGE_INDEX_LEB,
    RANGE_INDEX_LINK
};

TbSymbolRangeIndexStatus_e tb_symbol_range_index_init(TbSymbolRangeIndex_t *index,
    TbSymbolRangeIndexRequest_t request, TbSymbolRangeBoundary_t *entries, size_t capacity)
{
    if (!index || (capacity && !entries) || capacity > SIZE_MAX / sizeof(*entries) ||
        request.offset > request.end_offset || !request.address_bytes || request.address_bytes > 255 ||
        request.segment_bytes > 255 ||
        (request.byte_order != TB_SYMBOL_LITTLE_ENDIAN && request.byte_order != TB_SYMBOL_BIG_ENDIAN)) {
        return TB_SYMBOL_RANGE_INDEX_INVALID;
    }

    if ((request.version != 4 && request.version != 5) ||
        (request.version == 4 && request.segment_bytes)) {
        return TB_SYMBOL_RANGE_INDEX_UNSUPPORTED;
    }

    *index = (TbSymbolRangeIndex_t) {
        .view = {
            .request = request,
            .entries = entries
        },
        .entries     = entries,
        .status      = TB_SYMBOL_RANGE_INDEX_PENDING,
        .cursor      = request.offset,
        .capacity    = capacity,
        .initialized = true
    };

    return index->status;
}


static void fail(TbSymbolRangeIndex_t *index, TbSymbolRangeIndexStatus_e status)
{
    index->status         = status;
    index->failure_offset = index->cursor;
}


static void operand(TbSymbolRangeIndex_t *index)
{
    TbSymbolRangeKind_e kind = index->candidate.kind;
    bool fixed = index->view.request.version == 4 || kind == TB_SYMBOL_RANGE_BASE ||
                 kind == TB_SYMBOL_RANGE_ADDRESS_PAIR ||
                 (kind == TB_SYMBOL_RANGE_ADDRESS_LENGTH && index->operand == 0);

    if (fixed) {
        index->remaining = index->view.request.address_bytes + index->view.request.segment_bytes;
        index->phase     = RANGE_INDEX_FIXED;
    } else {
        (void)tb_symbol_leb_init(&index->leb, false);
        index->phase = RANGE_INDEX_LEB;
    }
}


static void publish_record(TbSymbolRangeIndex_t *index)
{
    index->candidate.end_offset = index->cursor;
    index->entries[index->view.count++] = index->candidate;

    if (index->candidate.kind == TB_SYMBOL_RANGE_END) {
        index->linking = index->view.count;
        index->phase   = RANGE_INDEX_LINK;
    } else {
        index->phase = RANGE_INDEX_RECORD;
    }
}


static void next_operand(TbSymbolRangeIndex_t *index)
{
    if (++index->operand < index->operands) {
        operand(index);
        return;
    }

    if (index->view.request.version == 4) {
        if (index->first_maximum)
            index->candidate.kind = TB_SYMBOL_RANGE_BASE;
        else if (index->first_zero && index->second_zero)
            index->candidate.kind = TB_SYMBOL_RANGE_END;
    }

    publish_record(index);
}


static void start_record(TbSymbolRangeIndex_t *index)
{
    if (index->cursor == index->view.request.end_offset) {
        index->status = index->list_begin == index->view.count ? TB_SYMBOL_RANGE_INDEX_COMPLETE :
                                                                TB_SYMBOL_RANGE_INDEX_MALFORMED;
        index->failure_offset = index->cursor;
        return;
    }

    if (index->view.count == index->capacity) {
        fail(index, TB_SYMBOL_RANGE_INDEX_RESOURCE);
        return;
    }

    index->candidate = (TbSymbolRangeBoundary_t) {
        .offset = index->cursor,
        .kind   = TB_SYMBOL_RANGE_OFFSET_PAIR
    };
    index->operand       = 0;
    index->operands      = 2;
    index->first_zero    = true;
    index->second_zero   = true;
    index->first_maximum = true;

    if (index->view.request.version == 4)
        operand(index);
    else
        index->phase = RANGE_INDEX_OPCODE;
}


static void opcode(TbSymbolRangeIndex_t *index, unsigned char byte)
{
    if (byte > TB_SYMBOL_RANGE_ADDRESS_LENGTH) {
        fail(index, TB_SYMBOL_RANGE_INDEX_UNSUPPORTED);
        return;
    }

    index->candidate.kind = (TbSymbolRangeKind_e)byte;
    index->operands = byte == TB_SYMBOL_RANGE_END ? 0 :
                      (byte == TB_SYMBOL_RANGE_BASE || byte == TB_SYMBOL_RANGE_BASE_INDEX ? 1 : 2);

    if (index->operands)
        operand(index);
    else
        publish_record(index);
}


static void fixed_byte(TbSymbolRangeIndex_t *index, unsigned char byte)
{
    if (!index->operand) {
        index->first_zero    = index->first_zero && byte == 0;
        index->first_maximum = index->first_maximum && byte == 255;
    } else {
        index->second_zero = index->second_zero && byte == 0;
    }

    if (!--index->remaining)
        next_operand(index);
}


TbSymbolRangeIndexStatus_e tb_symbol_range_index_feed(TbSymbolRangeIndex_t *index,
    const void *bytes, size_t length, size_t *consumed)
{
    if (!index || !index->initialized || !consumed || (length && (!bytes || index->eof)))
        return TB_SYMBOL_RANGE_INDEX_INVALID;

    *consumed = 0;
    if (index->status != TB_SYMBOL_RANGE_INDEX_PENDING)
        return index->status;

    const unsigned char *input = bytes;
    for (unsigned step = 0; step < 256 && index->status == TB_SYMBOL_RANGE_INDEX_PENDING; ++step) {
        if (index->progress == UINT64_MAX) {
            fail(index, TB_SYMBOL_RANGE_INDEX_RESOURCE);
            break;
        }

        bool byte_phase = index->phase == RANGE_INDEX_OPCODE || index->phase == RANGE_INDEX_FIXED ||
                          index->phase == RANGE_INDEX_LEB;
        if (byte_phase && index->cursor != index->view.request.end_offset && *consumed == length)
            break;

        ++index->progress;
        if (index->phase == RANGE_INDEX_RECORD) {
            start_record(index);
            continue;
        }

        if (index->phase == RANGE_INDEX_LINK) {
            if (index->linking > index->list_begin) {
                index->entries[--index->linking].list_end = index->cursor;
            } else {
                index->list_begin = index->view.count;
                index->phase      = RANGE_INDEX_RECORD;
            }
            continue;
        }

        if (index->phase != RANGE_INDEX_OPCODE && index->phase != RANGE_INDEX_FIXED &&
            index->phase != RANGE_INDEX_LEB) {
            fail(index, TB_SYMBOL_RANGE_INDEX_INVALID);
            break;
        }

        if (index->cursor == index->view.request.end_offset) {
            fail(index, TB_SYMBOL_RANGE_INDEX_MALFORMED);
            break;
        }

        if (*consumed == length)
            break;

        unsigned char byte = input[(*consumed)++];
        ++index->cursor;
        if (index->phase == RANGE_INDEX_OPCODE) {
            opcode(index, byte);
        } else if (index->phase == RANGE_INDEX_FIXED) {
            fixed_byte(index, byte);
        } else {
            size_t taken;
            TbSymbolReadStatus_e status = tb_symbol_leb_feed(&index->leb, &byte, 1, &taken);
            if (status == TB_SYMBOL_READ_OK)
                next_operand(index);
            else if (status != TB_SYMBOL_READ_LIMIT)
                fail(index, TB_SYMBOL_RANGE_INDEX_RESOURCE);
        }
    }

    return index->status;
}


TbSymbolRangeIndexStatus_e tb_symbol_range_index_finish(TbSymbolRangeIndex_t *index)
{
    if (!index || !index->initialized)
        return TB_SYMBOL_RANGE_INDEX_INVALID;

    index->eof = true;

    size_t consumed;
    TbSymbolRangeIndexStatus_e status = tb_symbol_range_index_feed(index, NULL, 0, &consumed);
    if (status == TB_SYMBOL_RANGE_INDEX_PENDING &&
        (index->phase == RANGE_INDEX_OPCODE || index->phase == RANGE_INDEX_FIXED ||
         index->phase == RANGE_INDEX_LEB)) {
        fail(index, TB_SYMBOL_RANGE_INDEX_MALFORMED);
        status = index->status;
    }

    return status;
}


bool tb_symbol_range_index_view(const TbSymbolRangeIndex_t *index, TbSymbolRangeIndexView_t *view)
{
    if (!index || !index->initialized || index->status != TB_SYMBOL_RANGE_INDEX_COMPLETE || !view)
        return false;

    *view = index->view;

    return true;
}


bool tb_symbol_range_index_find(TbSymbolRangeIndexView_t view, uint64_t offset,
    TbSymbolRangeBoundary_t *boundary)
{
    if (!boundary || (view.count && !view.entries) || offset < view.request.offset ||
        offset >= view.request.end_offset) {
        return false;
    }

    size_t low  = 0;
    size_t high = view.count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (view.entries[middle].offset < offset)
            low = middle + 1;
        else
            high = middle;
    }

    if (low == view.count || view.entries[low].offset != offset)
        return false;

    *boundary = view.entries[low];

    return true;
}


void tb_symbol_range_index_discard(TbSymbolRangeIndex_t *index)
{
    if (index)
        memset(index, 0, sizeof(*index));
}
