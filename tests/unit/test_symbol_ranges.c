#include "symbol/ranges.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static unsigned char image[32768];
static size_t used;
static TbSymbolRangesRequest_t request;
static TbSymbolRanges_t ranges;
static TbSymbolRangeRecord_t records[1024];
static TbSymbolRange_t intervals[1024];
static const uint64_t address_values[] = {10, 20, 80, 84};

static void fixed(unsigned width, uint64_t value)
{
    CHECK(width <= sizeof(image) - used);
    for (unsigned i = 0; i < width; ++i) {
        unsigned index = request.byte_order == TB_SYMBOL_LITTLE_ENDIAN ? i : width - 1 - i;
        image[used + index] = (unsigned char)(value >> (8U * i));
    }
    used += width;
}


static void uleb(uint64_t value)
{
    do {
        unsigned byte = (unsigned)(value & 0x7fU);
        value >>= 7U;
        fixed(1, byte | (value ? 0x80U : 0));
    } while (value);
}


static void fixture(unsigned version, unsigned width, TbSymbolByteOrder_e order)
{
    memset(image, 0, sizeof(image));
    used = 0;
    request = (TbSymbolRangesRequest_t) {
        .offset         = 53,
        .base           = 16,
        .byte_order     = order,
        .version        = version,
        .address_bytes  = width,
        .base_available = true
    };
}


static void pair(uint64_t first, uint64_t second)
{
    if (request.version == 5) {
        fixed(1, TB_SYMBOL_RANGE_OFFSET_PAIR);
        uleb(first); uleb(second);
    } else {
        fixed(request.address_bytes, first); fixed(request.address_bytes, second);
    }
}


static void base(uint64_t value)
{
    if (request.version == 5)
        fixed(1, TB_SYMBOL_RANGE_BASE);
    else
        fixed(request.address_bytes, request.address_bytes == 8 ? UINT64_MAX :
                                        (UINT64_C(1) << (8U * request.address_bytes)) - 1);
    fixed(request.address_bytes, value);
}


static void end(void)
{
    fixed(1, 0);
    if (request.version == 4) {
        for (unsigned i = 1; i < 2 * request.address_bytes; ++i)
            fixed(1, 0);
    }
}


static void seal(size_t record_capacity, size_t range_capacity)
{
    request.end_offset = request.offset + used + 8;
    CHECK(tb_symbol_ranges_init(&ranges, request, records, record_capacity, intervals, range_capacity) ==
          TB_SYMBOL_RANGES_PENDING);
}


static void address_table(void)
{
    size_t saved_used = used;
    unsigned char saved[sizeof(image)];
    memcpy(saved, image, used);
    used = 0;
    fixed(4, 4 + 4 * request.address_bytes);
    fixed(2, 5); fixed(1, request.address_bytes); fixed(1, 0);
    for (unsigned i = 0; i < 4; ++i)
        fixed(request.address_bytes, address_values[i]);
    CHECK(tb_symbol_indexed_table_header(TB_SYMBOL_TABLE_ADDRESSES, (TbSymbolDwarfSpan_t) {
        .section_bytes = 71 + used,
        .unit_offset   = 71,
        .byte_order    = request.byte_order
    }, image, used, &request.addresses) == TB_SYMBOL_DWARF_OK);
    request.addresses_available = true;
    memcpy(image, saved, saved_used);
    used = saved_used;
}


static TbSymbolIndexedEntry_t entry_for(uint64_t index)
{
    CHECK(index < 4);
    unsigned char bytes[8] = {0};
    for (unsigned i = 0; i < request.address_bytes; ++i) {
        unsigned position = request.byte_order == TB_SYMBOL_LITTLE_ENDIAN ? i : request.address_bytes - 1 - i;
        bytes[position] = (unsigned char)(address_values[index] >> (8U * i));
    }
    TbSymbolIndexedEntry_t entry;
    CHECK(tb_symbol_indexed_table_entry(request.addresses, index, bytes, sizeof(bytes), &entry) ==
          TB_SYMBOL_DWARF_OK);
    return entry;
}


static TbSymbolRangesStatus_e run(size_t chunk)
{
    size_t cursor = 0;
    for (unsigned turn = 0; turn < 100000; ++turn) {
        TbSymbolRangesView_t unavailable;
        CHECK(!tb_symbol_ranges_view(&ranges, &unavailable));
        uint64_t before = ranges.progress;
        if (ranges.status == TB_SYMBOL_RANGES_ADDRESS) {
            uint64_t index;
            CHECK(tb_symbol_ranges_address_need(&ranges, &index));
            TbSymbolIndexedEntry_t entry = entry_for(index);
            CHECK(tb_symbol_ranges_address(&ranges, &entry) == TB_SYMBOL_RANGES_PENDING);
        } else {
            size_t available = used - cursor;
            if (available > chunk)
                available = chunk;
            size_t consumed;
            (void)tb_symbol_ranges_feed(&ranges, image + cursor, available, &consumed);
            CHECK(consumed <= available && consumed <= 256 && ranges.progress - before <= 256);
            cursor += consumed;
        }
        CHECK(ranges.progress > before || ranges.status != TB_SYMBOL_RANGES_PENDING);
        if (ranges.status != TB_SYMBOL_RANGES_PENDING && ranges.status != TB_SYMBOL_RANGES_ADDRESS) {
            CHECK(cursor <= used);
            return ranges.status;
        }
    }
    CHECK(false);
    return TB_SYMBOL_RANGES_INVALID;
}


static TbSymbolRangesView_t completed(void)
{
    TbSymbolRangesView_t view;
    CHECK(tb_symbol_ranges_view(&ranges, &view));
    CHECK(view.end_offset == request.offset + used);
    return view;
}


static void check_versions(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        for (unsigned width = 1; width <= 8; width *= 2) {
            for (unsigned endian = 0; endian < 2; ++endian) {
                fixture(version, width, (TbSymbolByteOrder_e)endian);
                pair(20, 24); pair(0, 4); pair(2, 2); pair(4, 8);
                base(100); pair(0, 3); end();
                for (size_t chunk = 1; chunk <= 256; chunk *= 2) {
                    seal(7, 5);
                    CHECK(run(chunk) == TB_SYMBOL_RANGES_COMPLETE);
                    TbSymbolRangesView_t view = completed();
                    CHECK(view.record_count == 7 && view.range_count == 5);
                    CHECK(intervals[0].begin == 16 && intervals[0].end == 20 &&
                          intervals[0].record_index == 1);
                    CHECK(intervals[1].begin == 18 && intervals[1].end == 18);
                    CHECK(intervals[2].begin == 20 && intervals[2].end == 24);
                    CHECK(intervals[3].begin == 36 && intervals[3].end == 40);
                    CHECK(intervals[4].begin == 100 && intervals[4].end == 103);
                    CHECK(intervals[4].base_record == records[4].offset && intervals[4].relative);
                    CHECK(records[6].kind == TB_SYMBOL_RANGE_END);
                    CHECK(records[0].offset == request.offset && records[6].end_offset == view.end_offset);
                    CHECK(records[0].operand_count == 2 && records[0].operand_offsets[0] > 0);
                    CHECK(records[6].operand_count == (version == 4 ? 2U : 0U));
                }
            }
        }
    }
}


static void indexed_program(void)
{
    fixed(1, TB_SYMBOL_RANGE_BASE_INDEX); uleb(0);
    pair(1, 3);
    fixed(1, TB_SYMBOL_RANGE_INDEX_PAIR); uleb(2); uleb(3);
    fixed(1, TB_SYMBOL_RANGE_INDEX_LENGTH); uleb(1); uleb(2);
    base(40); pair(0, 3);
    fixed(1, TB_SYMBOL_RANGE_ADDRESS_PAIR);
    fixed(request.address_bytes, 100); fixed(request.address_bytes, 104);
    fixed(1, TB_SYMBOL_RANGE_ADDRESS_LENGTH); fixed(request.address_bytes, 110); uleb(4);
    fixed(1, TB_SYMBOL_RANGE_ADDRESS_LENGTH); fixed(request.address_bytes, 101); uleb(0);
    end();
}


static void check_indexed(void)
{
    for (unsigned width = 1; width <= 8; width *= 2) {
        for (unsigned endian = 0; endian < 2; ++endian) {
            fixture(5, width, (TbSymbolByteOrder_e)endian);
            request.base_available = false;
            address_table(); indexed_program(); seal(10, 7);
            CHECK(run(1) == TB_SYMBOL_RANGES_COMPLETE && completed().range_count == 7);
            CHECK(intervals[0].begin == 11 && intervals[1].begin == 20 && intervals[2].begin == 40);
            CHECK(intervals[3].begin == 80 && intervals[4].begin == 100);
            CHECK(intervals[5].begin == 101 && intervals[5].end == 101 && intervals[6].begin == 110);
            CHECK(records[0].addresses[0] == 10 && records[0].operands[0] == 0);
            CHECK(records[2].addresses[0] == 80 && records[2].addresses[1] == 84);
        }
    }

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    address_table(); fixed(1, TB_SYMBOL_RANGE_BASE_INDEX); uleb(0); end(); seal(2, 0);
    size_t consumed;
    CHECK(tb_symbol_ranges_feed(&ranges, image, used, &consumed) == TB_SYMBOL_RANGES_ADDRESS);
    CHECK(consumed == 2);
    TbSymbolIndexedEntry_t entry = entry_for(0);
    entry.table.table_offset++;
    TbSymbolRanges_t old = ranges;
    CHECK(tb_symbol_ranges_address(&ranges, &entry) == TB_SYMBOL_RANGES_INVALID);
    CHECK(!memcmp(&old, &ranges, sizeof(ranges)));
    CHECK(tb_symbol_ranges_address(&ranges, NULL) == TB_SYMBOL_RANGES_DEPENDENCY);

    request.addresses_available = false;
    seal(2, 0);
    CHECK(run(256) == TB_SYMBOL_RANGES_DEPENDENCY);
    request.addresses_available = true;
    image[1] = 4;
    seal(2, 0);
    CHECK(run(256) == TB_SYMBOL_RANGES_MALFORMED);
}


static void check_failures(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        fixture(version, 4, TB_SYMBOL_LITTLE_ENDIAN);
        pair(0, 4); end(); seal(1, 1);
        CHECK(run(1) == TB_SYMBOL_RANGES_RESOURCE);
        seal(2, 0);
        CHECK(run(1) == TB_SYMBOL_RANGES_RESOURCE);
        request.base_available = false;
        seal(2, 1);
        CHECK(run(1) == TB_SYMBOL_RANGES_DEPENDENCY);

        fixture(version, 4, TB_SYMBOL_LITTLE_ENDIAN);
        base(UINT32_MAX - 1); pair(0, 2); end(); seal(3, 1);
        CHECK(run(1) == TB_SYMBOL_RANGES_MALFORMED);
        fixture(version, 8, TB_SYMBOL_LITTLE_ENDIAN);
        base(UINT64_MAX); pair(1, 2); end(); seal(3, 1);
        CHECK(run(1) == TB_SYMBOL_RANGES_MALFORMED);
        fixture(version, 4, TB_SYMBOL_LITTLE_ENDIAN);
        pair(5, 4); end(); seal(2, 1);
        CHECK(run(1) == TB_SYMBOL_RANGES_MALFORMED);

        fixture(version, 4, TB_SYMBOL_LITTLE_ENDIAN);
        pair(20, 30); pair(0, 25); end(); seal(3, 2);
        CHECK(run(1) == TB_SYMBOL_RANGES_MALFORMED);
        TbSymbolRangesFailure_t failure;
        CHECK(tb_symbol_ranges_failure(&ranges, &failure));
        CHECK(failure.record_index == 0 && failure.conflicting_record == 1);
        TbSymbolRangesView_t unavailable;
        CHECK(!tb_symbol_ranges_view(&ranges, &unavailable));
    }

    fixture(5, 8, TB_SYMBOL_BIG_ENDIAN);
    fixed(1, TB_SYMBOL_RANGE_ADDRESS_LENGTH); fixed(8, UINT64_MAX); uleb(1); end(); seal(2, 1);
    CHECK(run(1) == TB_SYMBOL_RANGES_MALFORMED);
    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    fixed(1, 8); end(); seal(2, 1);
    CHECK(run(1) == TB_SYMBOL_RANGES_UNSUPPORTED);
    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    fixed(1, TB_SYMBOL_RANGE_OFFSET_PAIR);
    for (unsigned i = 0; i < 10; ++i)
        fixed(1, 0xff);
    fixed(1, 0); seal(2, 1);
    CHECK(run(1) == TB_SYMBOL_RANGES_UNSUPPORTED);
}


static void check_limits_and_lifetimes(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        fixture(version, 4, TB_SYMBOL_LITTLE_ENDIAN);
        request.base_available = false;
        base(0); pair(1, 4); end(); seal(3, 1);
        CHECK(run(1) == TB_SYMBOL_RANGES_COMPLETE && completed().range_count == 1);
        CHECK(intervals[0].begin == 1 && intervals[0].end == 4);
        CHECK(records[0].kind == TB_SYMBOL_RANGE_BASE);
    }

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    end(); seal(1, 0);
    CHECK(run(1) == TB_SYMBOL_RANGES_COMPLETE && completed().range_count == 0);
    seal(1, 0);
    size_t trailing;
    CHECK(tb_symbol_ranges_feed(&ranges, image, used + 8, &trailing) == TB_SYMBOL_RANGES_COMPLETE);
    CHECK(trailing == used);

    fixture(5, 4, TB_SYMBOL_LITTLE_ENDIAN);
    for (unsigned i = 520; i > 0; --i) {
        fixed(1, TB_SYMBOL_RANGE_ADDRESS_PAIR);
        fixed(4, 4 * i); fixed(4, 4 * i + 2);
    }
    end(); seal(521, 520);
    CHECK(run(4096) == TB_SYMBOL_RANGES_COMPLETE && completed().range_count == 520);
    CHECK(intervals[0].begin == 4 && intervals[519].begin == 2080);
    CHECK(intervals[0].record_index == 519 && intervals[519].record_index == 0);

    seal(521, 520);
    size_t consumed;
    size_t cursor = 0;
    while (cursor < used) {
        (void)tb_symbol_ranges_feed(&ranges, image + cursor, used - cursor, &consumed);
        cursor += consumed;
    }
    unsigned slices = 0;
    while (ranges.status == TB_SYMBOL_RANGES_PENDING) {
        uint64_t before = ranges.progress;
        (void)tb_symbol_ranges_finish(&ranges);
        CHECK(ranges.progress - before <= 256);
        ++slices;
    }
    CHECK(slices > 1 && ranges.status == TB_SYMBOL_RANGES_COMPLETE);

    fixture(4, 4, TB_SYMBOL_LITTLE_ENDIAN);
    pair(1, 5); base(100); pair(2, 3); end(); seal(4, 2);
    for (size_t boundary = 0; boundary < used; ++boundary) {
        seal(4, 2);
        cursor = 0;
        while (cursor < boundary) {
            (void)tb_symbol_ranges_feed(&ranges, image + cursor, boundary - cursor, &consumed);
            cursor += consumed;
        }
        TbSymbolRangesStatus_e status = tb_symbol_ranges_finish(&ranges);
        while (status == TB_SYMBOL_RANGES_PENDING)
            status = tb_symbol_ranges_finish(&ranges);
        CHECK(status == TB_SYMBOL_RANGES_MALFORMED);
    }

    seal(4, 2);
    TbSymbolRanges_t old = ranges;
    CHECK(tb_symbol_ranges_init(&ranges, request, records, SIZE_MAX, intervals, 2) ==
          TB_SYMBOL_RANGES_INVALID);
    CHECK(!memcmp(&old, &ranges, sizeof(ranges)));
    request.segment_bytes = 1;
    CHECK(tb_symbol_ranges_init(&ranges, request, records, 4, intervals, 2) == TB_SYMBOL_RANGES_UNSUPPORTED);
    CHECK(!memcmp(&old, &ranges, sizeof(ranges)));
    request.segment_bytes = 0;
    seal(4, 2);
    ranges.phase = ~0U;
    CHECK(tb_symbol_ranges_finish(&ranges) == TB_SYMBOL_RANGES_INVALID);
    TbSymbolRangesView_t invalid_view;
    CHECK(!tb_symbol_ranges_view(&ranges, &invalid_view));

    seal(4, 2);
    ranges.phase = ~0U;
    CHECK(tb_symbol_ranges_feed(&ranges, image, used, &consumed) == TB_SYMBOL_RANGES_INVALID);
    CHECK(consumed == 0 && !tb_symbol_ranges_view(&ranges, &invalid_view));

    for (size_t boundary = 0; boundary <= used; ++boundary) {
        seal(4, 2);
        (void)tb_symbol_ranges_feed(&ranges, image, boundary, &consumed);
        TbSymbolRangeRecord_t retained = records[0];
        tb_symbol_ranges_discard(&ranges);
        CHECK(!memcmp(&retained, &records[0], sizeof(retained)));
        TbSymbolRangesView_t unavailable;
        CHECK(!tb_symbol_ranges_view(&ranges, &unavailable));
    }
}


int main(void)
{
    check_versions();
    check_indexed();
    check_failures();
    check_limits_and_lifetimes();
    puts("PASS bounded DWARF ranges, explicit address dependencies, unordered intervals "
         "and failure atomicity");
    return 0;
}
