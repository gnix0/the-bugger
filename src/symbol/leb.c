#include "symbol/leb.h"

bool tb_symbol_leb_init(TbSymbolLeb_t *leb, bool is_signed)
{
    if (!leb)
        return false;
    *leb = (TbSymbolLeb_t){
        .initialized = true,
        .is_signed   = is_signed,
        .high_zero   = true,
        .high_one    = true
    };
    return true;
}


TbSymbolReadStatus_e tb_symbol_leb_feed(TbSymbolLeb_t *leb, const void *bytes, size_t length,
                                      size_t *consumed)
{
    if (!leb || !leb->initialized || !consumed || (length && !bytes))
        return TB_SYMBOL_READ_INVALID;
    if (leb->failed)
        return TB_SYMBOL_READ_OVERFLOW;
    if (leb->done) {
        *consumed = 0;
        return TB_SYMBOL_READ_OK;
    }
    TbSymbolLeb_t candidate = *leb;
    const unsigned char *input = bytes;
    size_t count = length < 256 ? length : 256;
    for (size_t i = 0; i < count; ++i) {
        if (candidate.bytes == UINT64_MAX) {
            leb->failed = true;
            return TB_SYMBOL_READ_OVERFLOW;
        }
        unsigned byte = input[i];
        uint64_t payload = byte & 0x7fU;
        if (candidate.bytes < 9) {
            candidate.bits |= payload << (unsigned)(candidate.bytes * 7);
        } else if (candidate.bytes == 9) {
            candidate.bits |= (payload & 1U) << 63U;
            candidate.high_zero = candidate.high_zero && (payload & 0x7eU) == 0;
            candidate.high_one = candidate.high_one && (payload & 0x7eU) == 0x7eU;
        } else {
            candidate.high_zero = candidate.high_zero && payload == 0;
            candidate.high_one = candidate.high_one && payload == 0x7fU;
        }
        ++candidate.bytes;
        if (!(byte & 0x80U)) {
            if (candidate.is_signed && candidate.bytes < 10 && (byte & 0x40U))
                candidate.bits |= UINT64_MAX << (unsigned)(candidate.bytes * 7);
            candidate.done = true;
            *leb = candidate;
            *consumed = i + 1;
            return TB_SYMBOL_READ_OK;
        }
    }
    *leb = candidate;
    *consumed = count;
    return TB_SYMBOL_READ_LIMIT;
}


TbSymbolReadStatus_e tb_symbol_leb_finish(const TbSymbolLeb_t *leb)
{
    if (!leb || !leb->initialized)
        return TB_SYMBOL_READ_INVALID;
    if (leb->failed)
        return TB_SYMBOL_READ_OVERFLOW;
    return leb->done ? TB_SYMBOL_READ_OK : TB_SYMBOL_READ_TRUNCATED;
}


bool tb_symbol_leb_view(const TbSymbolLeb_t *leb, TbSymbolLebView_t *view)
{
    if (!view || tb_symbol_leb_finish(leb) != TB_SYMBOL_READ_OK)
        return false;
    bool negative = leb->is_signed && (leb->bits & (UINT64_C(1) << 63U));
    bool fits     = negative ? leb->high_one : leb->high_zero;
    *view = (TbSymbolLebView_t){
        .bytes     = leb->bytes,
        .bits      = fits ? leb->bits : 0,
        .is_signed = leb->is_signed,
        .fits      = fits
    };
    return true;
}


bool tb_symbol_leb_uint(const TbSymbolLeb_t *leb, uint64_t *value)
{
    TbSymbolLebView_t view;
    if (!value || !tb_symbol_leb_view(leb, &view) || view.is_signed || !view.fits)
        return false;
    *value = view.bits;
    return true;
}


bool tb_symbol_leb_sint(const TbSymbolLeb_t *leb, int64_t *value)
{
    TbSymbolLebView_t view;
    if (!value || !tb_symbol_leb_view(leb, &view) || !view.is_signed || !view.fits)
        return false;
    *value = view.bits <= INT64_MAX ? (int64_t)view.bits : -1 - (int64_t)(UINT64_MAX - view.bits);
    return true;
}
