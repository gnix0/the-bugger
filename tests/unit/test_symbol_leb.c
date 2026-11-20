#include "symbol/leb.h"
#include "../support/check.h"

#include <string.h>

static unsigned char encoded[70000];

static size_t unsigned_encoding(uint64_t value)
{
    size_t count = 0;
    do {
        unsigned byte = (unsigned)(value & 0x7fU);
        value >>= 7;
        encoded[count++] = (unsigned char)(byte | (value ? 0x80U : 0));
    } while (value);
    return count;
}


static size_t signed_encoding(int64_t value)
{
    uint64_t bits = (uint64_t)value;
    size_t count = 0;
    for (;;) {
        unsigned byte = (unsigned)(bits & 0x7fU);
        bits >>= 7;
        if (value < 0)
            bits |= UINT64_MAX << 57;
        bool done = (bits == 0 && !(byte & 0x40U)) || (bits == UINT64_MAX && (byte & 0x40U));
        encoded[count++] = (unsigned char)(byte | (done ? 0 : 0x80U));
        if (done)
            return count;
    }
}


static void unavailable(const TbSymbolLeb_t *leb)
{
    TbSymbolLebView_t view;
    unsigned char before[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(before, &view, sizeof(view));
    CHECK(!tb_symbol_leb_view(leb, &view) && !memcmp(before, &view, sizeof(view)));
    uint64_t unsigned_value = 37;
    int64_t signed_value = 37;
    CHECK(!tb_symbol_leb_uint(leb, &unsigned_value) && unsigned_value == 37);
    CHECK(!tb_symbol_leb_sint(leb, &signed_value) && signed_value == 37);
}


static TbSymbolLebView_t decode(bool is_signed, size_t length, size_t chunk)
{
    TbSymbolLeb_t leb;
    CHECK(tb_symbol_leb_init(&leb, is_signed));
    unavailable(&leb);
    CHECK(tb_symbol_leb_finish(&leb) == TB_SYMBOL_READ_TRUNCATED);
    size_t offset = 0;
    while (offset < length) {
        size_t available = length - offset;
        if (available > chunk)
            available = chunk;
        size_t consumed = SIZE_MAX;
        TbSymbolReadStatus_e status = tb_symbol_leb_feed(&leb, encoded + offset, available, &consumed);
        CHECK(consumed && consumed <= available && consumed <= 256);
        offset += consumed;
        if (offset == length) {
            CHECK(status == TB_SYMBOL_READ_OK && tb_symbol_leb_finish(&leb) == TB_SYMBOL_READ_OK);
        } else {
            CHECK(status == TB_SYMBOL_READ_LIMIT && tb_symbol_leb_finish(&leb) == TB_SYMBOL_READ_TRUNCATED);
            unavailable(&leb);
        }
    }
    TbSymbolLebView_t view;
    CHECK(tb_symbol_leb_view(&leb, &view) && view.bytes == length && view.is_signed == is_signed);
    uint64_t unsigned_value = 37;
    int64_t signed_value = 37;
    CHECK(tb_symbol_leb_uint(&leb, &unsigned_value) == (!is_signed && view.fits));
    CHECK(tb_symbol_leb_sint(&leb, &signed_value) == (is_signed && view.fits));
    if (!view.fits || is_signed)
        CHECK(unsigned_value == 37);
    if (!view.fits || !is_signed)
        CHECK(signed_value == 37);
    size_t consumed = SIZE_MAX;
    CHECK(tb_symbol_leb_feed(&leb, encoded, length, &consumed) == TB_SYMBOL_READ_OK && !consumed);
    return view;
}


static void unsigned_values(void)
{
    const uint64_t values[] = {0, 1, 127, 128, 255, 16383, 16384, UINT32_MAX, INT64_MAX, UINT64_MAX};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        size_t length = unsigned_encoding(values[i]);
        TbSymbolReader_t reader;
        uint64_t expected;
        CHECK(tb_symbol_reader_init(&reader, encoded, length, TB_SYMBOL_BIG_ENDIAN) == TB_SYMBOL_READ_OK);
        CHECK(tb_symbol_reader_uleb(&reader, &expected) == TB_SYMBOL_READ_OK && expected == values[i]);
        for (size_t chunk = 1; chunk <= length + 1; ++chunk) {
            TbSymbolLebView_t view = decode(false, length, chunk);
            CHECK(view.fits && view.bits == expected);
        }
    }
}


static void signed_values(void)
{
    const int64_t values[] = {INT64_MIN, -16385, -129, -128, -65, -64, -1, 0, 1, 63, 64, 127, INT64_MAX};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        size_t length = signed_encoding(values[i]);
        TbSymbolReader_t reader;
        int64_t expected;
        CHECK(tb_symbol_reader_init(&reader, encoded, length, TB_SYMBOL_LITTLE_ENDIAN) == TB_SYMBOL_READ_OK);
        CHECK(tb_symbol_reader_sleb(&reader, &expected) == TB_SYMBOL_READ_OK && expected == values[i]);
        for (size_t chunk = 1; chunk <= length + 1; ++chunk) {
            TbSymbolLebView_t view = decode(true, length, chunk);
            CHECK(view.fits && view.bits == (uint64_t)expected);
        }
    }
}


static void wide_values(void)
{
    memset(encoded, 0x80, sizeof(encoded));
    encoded[9] = 2;
    TbSymbolLebView_t view = decode(false, 10, 1);
    CHECK(!view.fits && !view.bits);
    encoded[9] = 1;
    view = decode(true, 10, 3);
    CHECK(!view.fits && !view.bits);
    memset(encoded, 0xff, 9);
    encoded[9] = 0x7e;
    view = decode(true, 10, 7);
    CHECK(!view.fits && !view.bits);

    memset(encoded, 0x80, sizeof(encoded));
    encoded[sizeof(encoded) - 1] = 0;
    view = decode(false, sizeof(encoded), sizeof(encoded));
    CHECK(view.fits && !view.bits);
    view = decode(true, sizeof(encoded), 19);
    CHECK(view.fits && !view.bits);
    memset(encoded, 0xff, sizeof(encoded));
    encoded[sizeof(encoded) - 1] = 0x7f;
    view = decode(true, sizeof(encoded), 256);
    CHECK(view.fits && view.bits == UINT64_MAX);
    view = decode(false, sizeof(encoded), 256);
    CHECK(!view.fits && !view.bits);
    encoded[0] = 0x80;
    view = decode(true, sizeof(encoded), 1);
    CHECK(view.fits && view.bits == UINT64_MAX - 127);
}


static void boundaries(void)
{
    TbSymbolLeb_t leb = {0};
    size_t consumed = 37;
    CHECK(tb_symbol_leb_feed(&leb, encoded, 1, &consumed) == TB_SYMBOL_READ_INVALID && consumed == 37);
    CHECK(!tb_symbol_leb_init(NULL, false));
    CHECK(tb_symbol_leb_init(&leb, false));
    TbSymbolLeb_t before = leb;
    CHECK(tb_symbol_leb_feed(&leb, NULL, 1, &consumed) == TB_SYMBOL_READ_INVALID && consumed == 37);
    CHECK(!memcmp(&before, &leb, sizeof(leb)));
    CHECK(tb_symbol_leb_feed(&leb, encoded, 1, NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_leb_feed(NULL, encoded, 1, &consumed) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_leb_feed(&leb, NULL, 0, &consumed) == TB_SYMBOL_READ_LIMIT && !consumed);
    CHECK(!memcmp(&before, &leb, sizeof(leb)));
    encoded[0] = 0x80;
    CHECK(tb_symbol_leb_feed(&leb, encoded, 1, &consumed) == TB_SYMBOL_READ_LIMIT && consumed == 1);
    unavailable(&leb);
    CHECK(tb_symbol_leb_finish(&leb) == TB_SYMBOL_READ_TRUNCATED);
    encoded[0] = 1;
    encoded[1] = 2;
    CHECK(tb_symbol_leb_feed(&leb, encoded, 2, &consumed) == TB_SYMBOL_READ_OK && consumed == 1);
    uint64_t value;
    CHECK(tb_symbol_leb_uint(&leb, &value) && value == 128);
    CHECK(tb_symbol_leb_init(&leb, false));
    CHECK(tb_symbol_leb_feed(&leb, encoded + 1, 1, &consumed) == TB_SYMBOL_READ_OK && consumed == 1);
    CHECK(tb_symbol_leb_uint(&leb, &value) && value == 2);
    CHECK(tb_symbol_leb_finish(NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(!tb_symbol_leb_view(&leb, NULL) && !tb_symbol_leb_uint(&leb, NULL));
    CHECK(!tb_symbol_leb_sint(&leb, NULL));
}


int main(void)
{
    unsigned_values();
    signed_values();
    wide_values();
    boundaries();
    puts("PASS bounded streaming LEB framing, numeric fit, arbitrary chunking and wide-value preservation");
    return 0;
}
