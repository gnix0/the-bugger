#include "symbol/indexed_tables.h"

static bool uint_field(TbSymbolReader_t *reader, unsigned width, uint64_t *value)
{
    return tb_symbol_reader_uint(reader, width, value) == TB_SYMBOL_READ_OK;
}


TbSymbolDwarfStatus_e tb_symbol_indexed_table_header(TbSymbolIndexedTableKind_e kind,
    TbSymbolDwarfSpan_t span, const void *bytes, size_t length, TbSymbolIndexedTable_t *table)
{
    if (!table || (kind != TB_SYMBOL_TABLE_ADDRESSES && kind != TB_SYMBOL_TABLE_RANGES))
        return TB_SYMBOL_DWARF_INVALID;

    TbSymbolDwarfLength_t extent;
    TbSymbolDwarfStatus_e status = tb_symbol_dwarf_length(span, bytes, length, &extent);
    if (status != TB_SYMBOL_DWARF_OK)
        return status;

    unsigned version_end = extent.length_bytes + 2;
    if (version_end > extent.total_bytes || version_end > length)
        return TB_SYMBOL_DWARF_MALFORMED;

    TbSymbolReader_t reader;
    if (tb_symbol_reader_init(&reader, bytes, length < 20 ? length : 20, span.byte_order) !=
        TB_SYMBOL_READ_OK) {
        return TB_SYMBOL_DWARF_INVALID;
    }

    const unsigned char *initial_length;
    if (tb_symbol_reader_bytes(&reader, extent.length_bytes, &initial_length) != TB_SYMBOL_READ_OK)
        return TB_SYMBOL_DWARF_MALFORMED;

    uint64_t version;
    if (!uint_field(&reader, 2, &version))
        return TB_SYMBOL_DWARF_MALFORMED;
    if (version != 5)
        return TB_SYMBOL_DWARF_UNSUPPORTED;

    unsigned prefix = extent.length_bytes + (kind == TB_SYMBOL_TABLE_RANGES ? 8U : 4U);
    if (prefix > extent.total_bytes || prefix > length)
        return TB_SYMBOL_DWARF_MALFORMED;

    uint64_t address;
    uint64_t segment;
    uint64_t count = 0;
    if (!uint_field(&reader, 1, &address) || !uint_field(&reader, 1, &segment) ||
        (kind == TB_SYMBOL_TABLE_RANGES && !uint_field(&reader, 4, &count))) {
        return TB_SYMBOL_DWARF_MALFORMED;
    }

    if (!address)
        return TB_SYMBOL_DWARF_MALFORMED;

    uint64_t entries = span.unit_offset + prefix;
    uint64_t lists   = extent.end_offset;
    if (kind == TB_SYMBOL_TABLE_ADDRESSES) {
        unsigned stride = (unsigned)(address + segment);
        if ((extent.end_offset - entries) % stride)
            return TB_SYMBOL_DWARF_MALFORMED;

        count = (extent.end_offset - entries) / stride;
    } else {
        uint64_t offset_bytes;
        if (!tb_symbol_multiply(count, extent.offset_bytes, &offset_bytes) ||
            !tb_symbol_add(entries, offset_bytes, &lists) || lists > extent.end_offset) {
            return TB_SYMBOL_DWARF_MALFORMED;
        }
    }

    *table = (TbSymbolIndexedTable_t) {
        .kind           = kind,
        .extent         = extent,
        .table_offset   = span.unit_offset,
        .entries_offset = entries,
        .lists_offset   = lists,
        .count          = count,
        .byte_order     = span.byte_order,
        .address_bytes  = (unsigned)address,
        .segment_bytes  = (unsigned)segment
    };

    return TB_SYMBOL_DWARF_OK;
}


bool tb_symbol_indexed_table_offset(const TbSymbolIndexedTable_t *table, uint64_t index,
                                     uint64_t *offset)
{
    if (!table || !offset || index >= table->count ||
        (table->kind != TB_SYMBOL_TABLE_ADDRESSES && table->kind != TB_SYMBOL_TABLE_RANGES)) {
        return false;
    }

    unsigned stride = table->kind == TB_SYMBOL_TABLE_ADDRESSES ?
                          table->address_bytes + table->segment_bytes : table->extent.offset_bytes;
    uint64_t relative;
    uint64_t candidate;
    if (!stride || !tb_symbol_multiply(index, stride, &relative) ||
        !tb_symbol_add(table->entries_offset, relative, &candidate) ||
        !tb_symbol_range(candidate, stride, table->extent.end_offset)) {
        return false;
    }

    *offset = candidate;
    return true;
}


TbSymbolDwarfStatus_e tb_symbol_indexed_table_entry(TbSymbolIndexedTable_t table, uint64_t index,
    const void *bytes, size_t length, TbSymbolIndexedEntry_t *entry)
{
    uint64_t offset;
    if (!entry || !tb_symbol_indexed_table_offset(&table, index, &offset))
        return TB_SYMBOL_DWARF_INVALID;

    unsigned width = table.kind == TB_SYMBOL_TABLE_ADDRESSES ? table.address_bytes :
                                                             table.extent.offset_bytes;
    if ((table.kind == TB_SYMBOL_TABLE_ADDRESSES && table.segment_bytes) ||
        (width != 1 && width != 2 && width != 4 && width != 8)) {
        return TB_SYMBOL_DWARF_UNSUPPORTED;
    }

    TbSymbolReader_t reader;
    uint64_t         value;
    if (tb_symbol_reader_init(&reader, bytes, length, table.byte_order) != TB_SYMBOL_READ_OK)
        return TB_SYMBOL_DWARF_INVALID;

    if (!uint_field(&reader, width, &value))
        return TB_SYMBOL_DWARF_MALFORMED;

    uint64_t target = UINT64_MAX;
    if (table.kind == TB_SYMBOL_TABLE_RANGES &&
        (!tb_symbol_add(table.entries_offset, value, &target) || target < table.lists_offset ||
         target >= table.extent.end_offset)) {
        return TB_SYMBOL_DWARF_MALFORMED;
    }

    *entry = (TbSymbolIndexedEntry_t) {
        .table         = table,
        .index         = index,
        .offset        = offset,
        .value         = value,
        .target_offset = target
    };

    return TB_SYMBOL_DWARF_OK;
}
