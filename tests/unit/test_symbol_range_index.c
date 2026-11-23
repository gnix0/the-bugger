#include "symbol/range_index.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static TbSymbolRangeIndex_t index_state;
static TbSymbolRangeBoundary_t entries[2048];

static TbSymbolRangeIndexStatus_e decode(TbSymbolRangeIndexRequest_t request,
    const unsigned char *bytes, size_t length, size_t chunk, size_t capacity)
{
    CHECK(tb_symbol_range_index_init(&index_state, request, entries, capacity) ==
          TB_SYMBOL_RANGE_INDEX_PENDING);
    size_t cursor = 0;
    while (index_state.status == TB_SYMBOL_RANGE_INDEX_PENDING) {
        uint64_t before = index_state.progress;
        if (cursor == length) {
            (void)tb_symbol_range_index_finish(&index_state);
        } else {
            size_t remaining = length - cursor;
            size_t consumed = SIZE_MAX;
            (void)tb_symbol_range_index_feed(&index_state, bytes + cursor,
                                            remaining < chunk ? remaining : chunk, &consumed);
            CHECK(consumed <= 256 && consumed <= remaining);
            cursor += consumed;
        }
        CHECK(index_state.progress - before <= 256);
        CHECK(index_state.status != TB_SYMBOL_RANGE_INDEX_PENDING || index_state.progress > before);
    }
    CHECK(cursor <= length);
    return index_state.status;
}


static TbSymbolRangeIndexRequest_t request_for(unsigned version, unsigned width, size_t bytes)
{
    return (TbSymbolRangeIndexRequest_t) {
        .offset        = 71,
        .end_offset    = 71 + bytes,
        .byte_order    = TB_SYMBOL_LITTLE_ENDIAN,
        .version       = version,
        .address_bytes = width
    };
}


static void all_opcodes(void)
{
    /* One list containing all seven non-end kinds, then padding/empty lists.
     * Numeric interpretation is intentionally unnecessary (unbound indexes,
     * missing CU base, reversed intervals). All entry boundaries remain exact. */
    const unsigned char bytes[] = {
        1, 0x81, 0, 2, 9, 2, 3, 9, 8, 4, 0x82, 0, 1,
        5, 0, 0, 6, 9, 0, 2, 0, 7, 9, 0, 0x80, 1, 0, 0, 0
    };
    const uint64_t starts[] = {0, 3, 6, 9, 13, 16, 21, 26, 27, 28};
    const uint64_t ends[] = {3, 6, 9, 13, 16, 21, 26, 27, 28, 29};
    for (size_t chunk = 1; chunk <= sizeof(bytes) + 1; ++chunk) {
        CHECK(decode(request_for(5, 2, sizeof(bytes)), bytes, sizeof(bytes), chunk, 10) ==
              TB_SYMBOL_RANGE_INDEX_COMPLETE);
        TbSymbolRangeIndexView_t view;
        CHECK(tb_symbol_range_index_view(&index_state, &view) && view.count == 10);
        for (size_t i = 0; i < 10; ++i) {
            CHECK(view.entries[i].offset == 71 + starts[i]);
            CHECK(view.entries[i].end_offset == 71 + ends[i]);
            CHECK(view.entries[i].list_end == 71 + (i < 8 ? 27 : ends[i]));
        }
        for (size_t offset = 0; offset <= sizeof(bytes); ++offset) {
            TbSymbolRangeBoundary_t found = {0};
            bool expected = false;
            for (size_t i = 0; i < 10; ++i)
                expected = expected || starts[i] == offset;
            CHECK(tb_symbol_range_index_find(view, 71 + offset, &found) == expected);
            if (!expected)
                CHECK(found.offset == 0);
        }
    }
}


static void legacy(void)
{
    for (unsigned width = 1; width <= 8; width *= 2) {
        for (unsigned order = 0; order < 2; ++order) {
            unsigned char bytes[48] = {0};
            memset(bytes, 255, width);
            bytes[width] = 4;
            bytes[2 * width] = 2;
            bytes[3 * width] = 9;
            TbSymbolRangeIndexRequest_t request = request_for(4, width, 6 * width);
            request.byte_order = order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN;
            CHECK(decode(request, bytes, 6 * width, 1, 3) == TB_SYMBOL_RANGE_INDEX_COMPLETE);
            TbSymbolRangeIndexView_t view;
            CHECK(tb_symbol_range_index_view(&index_state, &view) && view.count == 3);
            CHECK(view.entries[0].kind == TB_SYMBOL_RANGE_BASE);
            CHECK(view.entries[1].kind == TB_SYMBOL_RANGE_OFFSET_PAIR);
            CHECK(view.entries[2].kind == TB_SYMBOL_RANGE_END);
            for (size_t i = 0; i < 3; ++i) {
                CHECK(view.entries[i].offset == 71 + 2 * width * i);
                CHECK(view.entries[i].list_end == request.end_offset);
            }
        }
    }
}


static void wide_and_segmented(void)
{
    unsigned char wide[1024];
    wide[0] = 1;
    memset(wide + 1, 0x81, 900);
    wide[901] = 1;
    wide[902] = 0;
    CHECK(decode(request_for(5, 8, 903), wide, 903, sizeof(wide), 2) ==
          TB_SYMBOL_RANGE_INDEX_COMPLETE);
    CHECK(entries[0].end_offset == 71 + 902 && entries[0].list_end == 71 + 903);

    unsigned char segmented[1022] = {6};
    TbSymbolRangeIndexRequest_t request = request_for(5, 255, sizeof(segmented));
    request.segment_bytes = 255;
    CHECK(decode(request, segmented, sizeof(segmented), 11, 2) == TB_SYMBOL_RANGE_INDEX_COMPLETE);
    CHECK(entries[0].end_offset == 71 + 1021 && entries[1].kind == TB_SYMBOL_RANGE_END);
}


static void failures(void)
{
    const unsigned char bytes[] = {4, 1, 2, 0};
    for (size_t length = 1; length < sizeof(bytes); ++length) {
        CHECK(decode(request_for(5, 8, sizeof(bytes)), bytes, length, 1, 2) ==
              TB_SYMBOL_RANGE_INDEX_MALFORMED);
        TbSymbolRangeIndexView_t view = {0};
        CHECK(!tb_symbol_range_index_view(&index_state, &view) && view.count == 0);
    }
    CHECK(decode(request_for(5, 8, sizeof(bytes)), bytes, sizeof(bytes), 4, 1) ==
          TB_SYMBOL_RANGE_INDEX_RESOURCE);
    CHECK(decode(request_for(5, 8, 3), bytes, sizeof(bytes), 4, 2) ==
          TB_SYMBOL_RANGE_INDEX_MALFORMED);
    const unsigned char unknown[] = {0, 8, 0};
    CHECK(decode(request_for(5, 8, 3), unknown, 3, 3, 3) == TB_SYMBOL_RANGE_INDEX_UNSUPPORTED);
    TbSymbolRangeIndexView_t view;
    CHECK(!tb_symbol_range_index_view(&index_state, &view));

    CHECK(decode(request_for(5, 8, 0), NULL, 0, 1, 0) == TB_SYMBOL_RANGE_INDEX_COMPLETE);
    CHECK(tb_symbol_range_index_view(&index_state, &view) && !view.count);
    TbSymbolRangeIndex_t saved = index_state;
    TbSymbolRangeIndexRequest_t invalid = request_for(5, 0, 1);
    CHECK(tb_symbol_range_index_init(&index_state, invalid, entries, 1) == TB_SYMBOL_RANGE_INDEX_INVALID);
    CHECK(!memcmp(&index_state, &saved, sizeof(saved)));
    invalid.address_bytes = 8;
    invalid.version = 6;
    CHECK(tb_symbol_range_index_init(&index_state, invalid, entries, 1) ==
          TB_SYMBOL_RANGE_INDEX_UNSUPPORTED);
    CHECK(!memcmp(&index_state, &saved, sizeof(saved)));
    size_t consumed = 99;
    CHECK(tb_symbol_range_index_feed(&index_state, bytes, 1, &consumed) == TB_SYMBOL_RANGE_INDEX_INVALID);
    CHECK(consumed == 99 && !memcmp(&index_state, &saved, sizeof(saved)));
    CHECK(tb_symbol_range_index_finish(NULL) == TB_SYMBOL_RANGE_INDEX_INVALID);
}


static void bounded_linking(void)
{
    unsigned char bytes[1801];
    for (size_t i = 0; i < 600; ++i) {
        bytes[3 * i] = 4;
        bytes[3 * i + 1] = 0;
        bytes[3 * i + 2] = 0;
    }
    bytes[1800] = 0;
    CHECK(decode(request_for(5, 8, sizeof(bytes)), bytes, sizeof(bytes), sizeof(bytes), 601) ==
          TB_SYMBOL_RANGE_INDEX_COMPLETE);
    for (size_t i = 0; i < 601; ++i)
        CHECK(entries[i].list_end == 71 + sizeof(bytes));
    CHECK(index_state.progress > 2400);

    TbSymbolRangeIndexRequest_t request = request_for(5, 8, 1);
    request.offset = UINT64_MAX - 1;
    request.end_offset = UINT64_MAX;
    const unsigned char end[] = {0, 8};
    CHECK(decode(request, end, 2, 2, 1) == TB_SYMBOL_RANGE_INDEX_COMPLETE);
    CHECK(entries[0].list_end == UINT64_MAX);
    tb_symbol_range_index_discard(&index_state);
    TbSymbolRangeIndexView_t view;
    CHECK(!tb_symbol_range_index_view(&index_state, &view));
}


static void invalid_and_partial_state(void)
{
    const unsigned char bytes[] = {4, 0x80, 1, 2, 0};
    TbSymbolRangeIndexRequest_t request = request_for(5, 8, sizeof(bytes));
    CHECK(tb_symbol_range_index_init(&index_state, request, entries, 2) == TB_SYMBOL_RANGE_INDEX_PENDING);
    size_t consumed;
    CHECK(tb_symbol_range_index_feed(&index_state, bytes, 1, &consumed) == TB_SYMBOL_RANGE_INDEX_PENDING);
    CHECK(consumed == 1);
    uint64_t progress = index_state.progress;
    CHECK(tb_symbol_range_index_feed(&index_state, NULL, 0, &consumed) == TB_SYMBOL_RANGE_INDEX_PENDING);
    CHECK(!consumed && index_state.progress == progress);
    TbSymbolRangeIndex_t saved = index_state;
    consumed = 99;
    CHECK(tb_symbol_range_index_feed(&index_state, NULL, 1, &consumed) == TB_SYMBOL_RANGE_INDEX_INVALID);
    CHECK(consumed == 99 && !memcmp(&index_state, &saved, sizeof(saved)));
    CHECK(tb_symbol_range_index_finish(&index_state) == TB_SYMBOL_RANGE_INDEX_MALFORMED);
    CHECK(tb_symbol_range_index_feed(&index_state, bytes + 1, 4, &consumed) == TB_SYMBOL_RANGE_INDEX_INVALID);

    CHECK(tb_symbol_range_index_init(&index_state, request, entries, 2) == TB_SYMBOL_RANGE_INDEX_PENDING);
    index_state.phase = 99;
    CHECK(tb_symbol_range_index_feed(&index_state, NULL, 0, &consumed) == TB_SYMBOL_RANGE_INDEX_INVALID);
    CHECK(consumed == 0);
    CHECK(tb_symbol_range_index_init(&index_state, request, entries, 2) == TB_SYMBOL_RANGE_INDEX_PENDING);
    index_state.progress = UINT64_MAX;
    CHECK(tb_symbol_range_index_feed(&index_state, bytes, 1, &consumed) == TB_SYMBOL_RANGE_INDEX_RESOURCE);
    CHECK(consumed == 0);

    CHECK(tb_symbol_range_index_init(&index_state, request, entries, 2) == TB_SYMBOL_RANGE_INDEX_PENDING);
    CHECK(tb_symbol_range_index_feed(&index_state, bytes, 2, &consumed) == TB_SYMBOL_RANGE_INDEX_PENDING);
    tb_symbol_range_index_discard(&index_state);
    TbSymbolRangeIndexView_t view;
    CHECK(!tb_symbol_range_index_view(&index_state, &view));

    const unsigned char legacy[] = {0, 0, 0, 0};
    for (size_t length = 1; length < sizeof(legacy); ++length) {
        CHECK(decode(request_for(4, 2, sizeof(legacy)), legacy, length, 1, 1) ==
              TB_SYMBOL_RANGE_INDEX_MALFORMED);
    }
    const unsigned char fixed[] = {6, 1, 2, 3};
    CHECK(decode(request_for(5, 2, sizeof(fixed)), fixed, sizeof(fixed), 1, 2) ==
          TB_SYMBOL_RANGE_INDEX_MALFORMED);
    tb_symbol_range_index_discard(&index_state);
}


int main(void)
{
    all_opcodes();
    legacy();
    wide_and_segmented();
    failures();
    bounded_linking();
    invalid_and_partial_state();
    puts("PASS range boundaries, suffixes, padding, wide framing, bounded linking and failure atomicity");
    return 0;
}
