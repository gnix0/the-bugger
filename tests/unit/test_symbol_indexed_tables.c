#include "symbol/indexed_tables.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static unsigned char image[256];
static size_t used;
static unsigned format;
static TbSymbolByteOrder_e order;

static void fixed(unsigned width, uint64_t value)
{
    CHECK(width <= sizeof(image) - used);
    for (unsigned i = 0; i < width; ++i) {
        unsigned index = order == TB_SYMBOL_LITTLE_ENDIAN ? i : width - 1 - i;
        image[used + index] = (unsigned char)(value >> (8U * i));
    }
    used += width;
}


static void start(unsigned offset_bytes, unsigned address_bytes, unsigned selectors,
                   TbSymbolIndexedTableKind_e kind)
{
    memset(image, 0, sizeof(image));
    format = offset_bytes;
    used = 0;
    if (format == 8) {
        fixed(4, UINT32_MAX);
        fixed(8, 0);
    } else {
        fixed(4, 0);
    }
    fixed(2, 5);
    fixed(1, address_bytes);
    fixed(1, selectors);
    if (kind == TB_SYMBOL_TABLE_RANGES)
        fixed(4, 3);
}


static TbSymbolDwarfStatus_e decode(TbSymbolIndexedTableKind_e kind, TbSymbolIndexedTable_t *table)
{
    size_t count = used;
    used = format == 8 ? 4 : 0;
    fixed(format, count - (format == 8 ? 12 : 4));
    used = count;
    return tb_symbol_indexed_table_header(kind, (TbSymbolDwarfSpan_t) {
        .section_bytes = 37 + count,
        .unit_offset   = 37,
        .byte_order    = order
    }, image, count, table);
}


static void check_tables(void)
{
    for (unsigned endian = 0; endian < 2; ++endian) {
        order = (TbSymbolByteOrder_e)endian;
        for (unsigned offset_bytes = 4; offset_bytes <= 8; offset_bytes += 4) {
            for (unsigned address_bytes = 1; address_bytes <= 8; address_bytes *= 2) {
                start(offset_bytes, address_bytes, 0, TB_SYMBOL_TABLE_ADDRESSES);
                for (unsigned value = 0; value < 4; ++value)
                    fixed(address_bytes, 10 + value * 20);
                TbSymbolIndexedTable_t table;
                CHECK(decode(TB_SYMBOL_TABLE_ADDRESSES, &table) == TB_SYMBOL_DWARF_OK);
                CHECK(table.count == 4 && table.address_bytes == address_bytes);
                CHECK(table.extent.offset_bytes == offset_bytes && table.lists_offset == 37 + used);
                for (uint64_t index = 0; index < table.count; ++index) {
                    uint64_t offset;
                    CHECK(tb_symbol_indexed_table_offset(&table, index, &offset));
                    TbSymbolIndexedEntry_t entry;
                    CHECK(tb_symbol_indexed_table_entry(table, index, image + offset - 37,
                          used - (size_t)(offset - 37), &entry) == TB_SYMBOL_DWARF_OK);
                    CHECK(entry.value == 10 + index * 20 && entry.offset == offset && entry.index == index);
                    CHECK(entry.target_offset == UINT64_MAX);
                }
                uint64_t unchanged = 123;
                CHECK(!tb_symbol_indexed_table_offset(&table, 4, &unchanged) && unchanged == 123);

                start(offset_bytes, address_bytes, 0, TB_SYMBOL_TABLE_RANGES);
                for (unsigned i = 0; i < 3; ++i)
                    fixed(offset_bytes, 3 * offset_bytes + i);
                fixed(1, 0); fixed(1, 0); fixed(1, 0);
                CHECK(decode(TB_SYMBOL_TABLE_RANGES, &table) == TB_SYMBOL_DWARF_OK);
                CHECK(table.count == 3 && table.lists_offset == table.entries_offset + 3 * offset_bytes);
                for (uint64_t index = 0; index < table.count; ++index) {
                    uint64_t offset;
                    CHECK(tb_symbol_indexed_table_offset(&table, index, &offset));
                    TbSymbolIndexedEntry_t entry;
                    CHECK(tb_symbol_indexed_table_entry(table, index, image + offset - 37,
                          used - (size_t)(offset - 37), &entry) == TB_SYMBOL_DWARF_OK);
                    CHECK(entry.value == 3 * offset_bytes + index);
                    CHECK(entry.target_offset == table.lists_offset + index);
                }
            }
        }
    }
}


static void check_failures(void)
{
    order = TB_SYMBOL_LITTLE_ENDIAN;
    TbSymbolIndexedTable_t table = {0};
    start(4, 0, 0, TB_SYMBOL_TABLE_ADDRESSES);
    CHECK(decode(TB_SYMBOL_TABLE_ADDRESSES, &table) == TB_SYMBOL_DWARF_MALFORMED);
    start(4, 4, 0, TB_SYMBOL_TABLE_ADDRESSES);
    fixed(1, 1);
    CHECK(decode(TB_SYMBOL_TABLE_ADDRESSES, &table) == TB_SYMBOL_DWARF_MALFORMED);
    start(4, 4, 0, TB_SYMBOL_TABLE_ADDRESSES);
    image[4] = 4;
    CHECK(decode(TB_SYMBOL_TABLE_ADDRESSES, &table) == TB_SYMBOL_DWARF_UNSUPPORTED);

    for (unsigned kind = 0; kind < 2; ++kind) {
        start(8, 4, 0, (TbSymbolIndexedTableKind_e)kind);
        if (kind) {
            for (unsigned i = 0; i < 3; ++i)
                fixed(8, 24 + i);
            fixed(1, 0); fixed(1, 0); fixed(1, 0);
        }
        CHECK(decode((TbSymbolIndexedTableKind_e)kind, &table) == TB_SYMBOL_DWARF_OK);
        TbSymbolIndexedTable_t old = table;
        unsigned prefix = kind ? 20 : 16;
        for (size_t available = 0; available < prefix; ++available) {
            CHECK(tb_symbol_indexed_table_header((TbSymbolIndexedTableKind_e)kind, (TbSymbolDwarfSpan_t) {
                .section_bytes = 37 + used,
                .unit_offset   = 37,
                .byte_order    = order
            }, image, available, &table) == TB_SYMBOL_DWARF_MALFORMED);
            CHECK(!memcmp(&old, &table, sizeof(table)));
        }
    }

    for (unsigned selectors = 0; selectors <= 2; selectors += 2) {
        start(4, selectors ? 4 : 3, selectors, TB_SYMBOL_TABLE_ADDRESSES);
        for (unsigned i = 0; i < (selectors ? 6U : 3U); ++i)
            fixed(1, i);
        CHECK(decode(TB_SYMBOL_TABLE_ADDRESSES, &table) == TB_SYMBOL_DWARF_OK && table.count == 1);
        TbSymbolIndexedEntry_t entry = {0};
        CHECK(tb_symbol_indexed_table_entry(table, 0, image + 8, used - 8, &entry) ==
              TB_SYMBOL_DWARF_UNSUPPORTED);
    }

    start(4, 4, 0, TB_SYMBOL_TABLE_RANGES);
    for (unsigned i = 0; i < 3; ++i)
        fixed(4, 12 + i);
    fixed(1, 0); fixed(1, 0); fixed(1, 0);
    CHECK(decode(TB_SYMBOL_TABLE_RANGES, &table) == TB_SYMBOL_DWARF_OK);
    TbSymbolIndexedEntry_t entry = {0};
    TbSymbolIndexedEntry_t saved = entry;
    unsigned char invalid[8] = {0};
    CHECK(tb_symbol_indexed_table_entry(table, 0, invalid, 4, &entry) == TB_SYMBOL_DWARF_MALFORMED);
    memset(invalid, 0xff, sizeof(invalid));
    CHECK(tb_symbol_indexed_table_entry(table, 0, invalid, 4, &entry) == TB_SYMBOL_DWARF_MALFORMED);
    CHECK(tb_symbol_indexed_table_entry(table, 0, image + 12, 3, &entry) == TB_SYMBOL_DWARF_MALFORMED);
    CHECK(!memcmp(&saved, &entry, sizeof(entry)));

    used = 13;
    CHECK(decode(TB_SYMBOL_TABLE_RANGES, &table) == TB_SYMBOL_DWARF_MALFORMED);
}


int main(void)
{
    check_tables();
    check_failures();
    puts("PASS independent DWARF address/range tables, checked indexes and preserved failed outputs");
    return 0;
}
