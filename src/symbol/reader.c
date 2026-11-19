#include "symbol/reader.h"

#include <limits.h>

_Static_assert(CHAR_BIT == 8, "Symbol decoding requires eight-bit bytes");

bool tb_symbol_add(uint64_t left, uint64_t right, uint64_t *result)
{
    if (!result || right > UINT64_MAX - left)
        return false;

    *result = left + right;
    return true;
}


bool tb_symbol_multiply(uint64_t left, uint64_t right, uint64_t *result)
{
    if (!result || (right && left > UINT64_MAX / right))
        return false;

    *result = left * right;
    return true;
}


bool tb_symbol_range(uint64_t offset, uint64_t length, uint64_t extent)
{
    return offset <= extent && length <= extent - offset;
}


static bool reader_valid(const TbSymbolReader_t *reader)
{
    return reader && (reader->data || !reader->length) && reader->offset <= reader->length &&
           (reader->byte_order == TB_SYMBOL_LITTLE_ENDIAN || reader->byte_order == TB_SYMBOL_BIG_ENDIAN);
}


TbSymbolReadStatus_e tb_symbol_reader_init(TbSymbolReader_t *reader, const void *data, size_t length,
                                          TbSymbolByteOrder_e byte_order)
{
    if (!reader || (!data && length) ||
        (byte_order != TB_SYMBOL_LITTLE_ENDIAN && byte_order != TB_SYMBOL_BIG_ENDIAN)) {
        return TB_SYMBOL_READ_INVALID;
    }

    *reader = (TbSymbolReader_t){
        .data       = data,
        .length     = length,
        .offset     = 0,
        .byte_order = byte_order
    };
    return TB_SYMBOL_READ_OK;
}


TbSymbolReadStatus_e tb_symbol_reader_bytes(TbSymbolReader_t *reader, size_t length,
                                           const unsigned char **bytes)
{
    if (!reader_valid(reader) || !bytes)
        return TB_SYMBOL_READ_INVALID;
    if (length > reader->length - reader->offset)
        return TB_SYMBOL_READ_TRUNCATED;

    *bytes = reader->data ? reader->data + reader->offset : NULL;
    reader->offset += length;
    return TB_SYMBOL_READ_OK;
}


TbSymbolReadStatus_e tb_symbol_reader_uint(TbSymbolReader_t *reader, unsigned width, uint64_t *value)
{
    if (!value || (width != 1 && width != 2 && width != 4 && width != 8))
        return TB_SYMBOL_READ_INVALID;

    const unsigned char *bytes;
    TbSymbolReadStatus_e status = tb_symbol_reader_bytes(reader, width, &bytes);
    if (status != TB_SYMBOL_READ_OK)
        return status;

    uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i) {
        unsigned index = reader->byte_order == TB_SYMBOL_BIG_ENDIAN ? i : width - 1 - i;
        result = (result << 8U) | bytes[index];
    }
    *value = result;
    return TB_SYMBOL_READ_OK;
}


static int64_t signed_bits(uint64_t bits)
{
    return bits <= INT64_MAX ? (int64_t)bits : -1 - (int64_t)(~bits);
}


TbSymbolReadStatus_e tb_symbol_reader_sint(TbSymbolReader_t *reader, unsigned width, int64_t *value)
{
    if (!value)
        return TB_SYMBOL_READ_INVALID;

    uint64_t             bits;
    TbSymbolReadStatus_e status = tb_symbol_reader_uint(reader, width, &bits);
    if (status != TB_SYMBOL_READ_OK)
        return status;

    unsigned count = width * 8;
    if (count < 64 && (bits & (UINT64_C(1) << (count - 1))))
        bits |= UINT64_MAX << count;
    *value = signed_bits(bits);
    return TB_SYMBOL_READ_OK;
}


static TbSymbolReadStatus_e read_leb(TbSymbolReader_t *reader, bool is_signed, uint64_t *value)
{
    if (!reader_valid(reader) || !value)
        return TB_SYMBOL_READ_INVALID;

    TbSymbolReader_t cursor = *reader;
    uint64_t         bits   = 0;
    for (unsigned i = 0; i < 10; ++i) {
        uint64_t             byte;
        TbSymbolReadStatus_e status = tb_symbol_reader_uint(&cursor, 1, &byte);
        if (status != TB_SYMBOL_READ_OK)
            return status;

        uint64_t payload = byte & 0x7fU;
        if (i == 9 && ((byte & 0x80U) ||
                       (is_signed ? payload != 0 && payload != 0x7f : payload > 1))) {
            return TB_SYMBOL_READ_OVERFLOW;
        }

        bits |= (i == 9 ? payload & 1U : payload) << (i * 7);
        if (!(byte & 0x80U)) {
            unsigned count = (i + 1) * 7;
            if (is_signed && count < 64 && (byte & 0x40U))
                bits |= UINT64_MAX << count;
            *value         = bits;
            reader->offset = cursor.offset;
            return TB_SYMBOL_READ_OK;
        }
    }
    return TB_SYMBOL_READ_OVERFLOW;
}


TbSymbolReadStatus_e tb_symbol_reader_uleb(TbSymbolReader_t *reader, uint64_t *value)
{
    return read_leb(reader, false, value);
}


TbSymbolReadStatus_e tb_symbol_reader_sleb(TbSymbolReader_t *reader, int64_t *value)
{
    if (!value)
        return TB_SYMBOL_READ_INVALID;

    uint64_t             bits;
    TbSymbolReadStatus_e status = read_leb(reader, true, &bits);
    if (status == TB_SYMBOL_READ_OK)
        *value = signed_bits(bits);
    return status;
}


TbSymbolReadStatus_e tb_symbol_reader_string(TbSymbolReader_t *reader, size_t max_bytes,
                                            const char **text, size_t *length)
{
    if (!reader_valid(reader) || !text || !length)
        return TB_SYMBOL_READ_INVALID;

    size_t available = reader->length - reader->offset;
    size_t count     = available < max_bytes ? available : max_bytes;
    for (size_t i = 0; i < count; ++i) {
        if (!reader->data[reader->offset + i]) {
            *text           = (const char *)reader->data + reader->offset;
            *length         = i;
            reader->offset += i + 1;
            return TB_SYMBOL_READ_OK;
        }
    }
    return available < max_bytes ? TB_SYMBOL_READ_TRUNCATED : TB_SYMBOL_READ_LIMIT;
}


TbSymbolReadStatus_e tb_symbol_reader_slice(TbSymbolReader_t *reader, size_t length,
                                           TbSymbolReader_t *slice)
{
    if (!slice || slice == reader)
        return TB_SYMBOL_READ_INVALID;

    const unsigned char *bytes;
    TbSymbolReadStatus_e status = tb_symbol_reader_bytes(reader, length, &bytes);
    if (status == TB_SYMBOL_READ_OK) {
        *slice = (TbSymbolReader_t){
            .data       = bytes,
            .length     = length,
            .offset     = 0,
            .byte_order = reader->byte_order
        };
    }
    return status;
}
