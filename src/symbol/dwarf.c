#include "symbol/dwarf.h"

TbSymbolDwarfStatus_e tb_symbol_dwarf_length(TbSymbolDwarfSpan_t span, const void *bytes, size_t length,
                                           TbSymbolDwarfLength_t *extent)
{
    if (!bytes || !extent)
        return TB_SYMBOL_DWARF_INVALID;
    TbSymbolReader_t reader;
    if (tb_symbol_reader_init(&reader, bytes, length < 12 ? length : 12, span.byte_order) !=
        TB_SYMBOL_READ_OK) {
        return TB_SYMBOL_DWARF_INVALID;
    }
    uint64_t value;
    if (tb_symbol_reader_uint(&reader, 4, &value) != TB_SYMBOL_READ_OK)
        return TB_SYMBOL_DWARF_MALFORMED;
    TbSymbolDwarfLength_t candidate = {
        .payload_bytes = value,
        .length_bytes  = 4,
        .offset_bytes  = 4
    };
    if (value == UINT32_MAX) {
        if (tb_symbol_reader_uint(&reader, 8, &candidate.payload_bytes) != TB_SYMBOL_READ_OK)
            return TB_SYMBOL_DWARF_MALFORMED;
        candidate.length_bytes = 12;
        candidate.offset_bytes = 8;
    } else if (value >= UINT32_C(0xfffffff0)) {
        return TB_SYMBOL_DWARF_MALFORMED;
    }
    if (!tb_symbol_add(candidate.payload_bytes, candidate.length_bytes, &candidate.total_bytes) ||
        !tb_symbol_range(span.unit_offset, candidate.total_bytes, span.section_bytes) ||
        !tb_symbol_add(span.unit_offset, candidate.total_bytes, &candidate.end_offset)) {
        return TB_SYMBOL_DWARF_MALFORMED;
    }
    *extent = candidate;
    return TB_SYMBOL_DWARF_OK;
}


static bool number(TbSymbolReader_t *reader, unsigned width, uint64_t *value)
{
    return tb_symbol_reader_uint(reader, width, value) == TB_SYMBOL_READ_OK;
}


TbSymbolDwarfStatus_e tb_symbol_dwarf_unit(TbSymbolDwarfSection_e section, TbSymbolDwarfSpan_t span,
                                         const void *bytes, size_t length, TbSymbolDwarfUnit_t *unit)
{
    if (!unit || (section != TB_SYMBOL_DWARF_INFO && section != TB_SYMBOL_DWARF_TYPES))
        return TB_SYMBOL_DWARF_INVALID;
    TbSymbolDwarfUnit_t candidate = {0};
    TbSymbolDwarfStatus_e status = tb_symbol_dwarf_length(span, bytes, length, &candidate.extent);
    if (status != TB_SYMBOL_DWARF_OK)
        return status;

    /* A prefix from the next contribution cannot complete a short unit header. */
    size_t available = length < 40 ? length : 40;
    if (candidate.extent.total_bytes < available)
        available = (size_t)candidate.extent.total_bytes;
    TbSymbolReader_t reader;
    if (tb_symbol_reader_init(&reader, bytes, available, span.byte_order) != TB_SYMBOL_READ_OK)
        return TB_SYMBOL_DWARF_INVALID;
    const unsigned char *encoded_length;
    if (tb_symbol_reader_bytes(&reader, candidate.extent.length_bytes, &encoded_length) != TB_SYMBOL_READ_OK)
        return TB_SYMBOL_DWARF_MALFORMED;
    uint64_t version;
    if (!number(&reader, 2, &version) || !version)
        return TB_SYMBOL_DWARF_MALFORMED;
    if (version != 4 && version != 5)
        return TB_SYMBOL_DWARF_UNSUPPORTED;
    candidate.version = (uint16_t)version;

    uint64_t address_bytes;
    if (version == 4) {
        if (!number(&reader, candidate.extent.offset_bytes, &candidate.abbrev_offset) ||
            !number(&reader, 1, &address_bytes)) {
            return TB_SYMBOL_DWARF_MALFORMED;
        }
        candidate.kind = section == TB_SYMBOL_DWARF_TYPES ? TB_SYMBOL_DWARF_TYPE
                                                         : TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
    } else {
        if (section != TB_SYMBOL_DWARF_INFO)
            return TB_SYMBOL_DWARF_UNSUPPORTED;
        uint64_t unit_type;
        if (!number(&reader, 1, &unit_type))
            return TB_SYMBOL_DWARF_MALFORMED;
        candidate.unit_type = (uint8_t)unit_type;
        switch (unit_type) {
        case 1:
            candidate.kind = TB_SYMBOL_DWARF_COMPILE;
            break;
        case 2:
            candidate.kind = TB_SYMBOL_DWARF_TYPE;
            break;
        case 3:
            candidate.kind = TB_SYMBOL_DWARF_PARTIAL;
            break;
        default:
            return TB_SYMBOL_DWARF_UNSUPPORTED;
        }
        if (!number(&reader, 1, &address_bytes) ||
            !number(&reader, candidate.extent.offset_bytes, &candidate.abbrev_offset)) {
            return TB_SYMBOL_DWARF_MALFORMED;
        }
    }
    if (!address_bytes)
        return TB_SYMBOL_DWARF_MALFORMED;
    candidate.address_bytes = (uint8_t)address_bytes;
    if (candidate.kind == TB_SYMBOL_DWARF_TYPE) {
        if (!number(&reader, 8, &candidate.signature) ||
            !number(&reader, candidate.extent.offset_bytes, &candidate.type_offset)) {
            return TB_SYMBOL_DWARF_MALFORMED;
        }
        candidate.has_signature = true;
    }
    candidate.header_bytes = (unsigned)reader.offset;
    if (candidate.header_bytes >= candidate.extent.total_bytes ||
        (candidate.has_signature && (candidate.type_offset < candidate.header_bytes ||
                                    candidate.type_offset >= candidate.extent.total_bytes))) {
        return TB_SYMBOL_DWARF_MALFORMED;
    }
    *unit = candidate;
    return TB_SYMBOL_DWARF_OK;
}
