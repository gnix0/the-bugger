#include "symbol/reader.h"
#include "symbol/identity.h"
#include "../support/check.h"

#include <limits.h>
#include <string.h>

static TbSymbolReader_t reader_for(const void *data, size_t length, TbSymbolByteOrder_e order)
{
    TbSymbolReader_t reader;
    CHECK(tb_symbol_reader_init(&reader, data, length, order) == TB_SYMBOL_READ_OK);
    return reader;
}


static void arithmetic(void)
{
    uint64_t result = 123;
    CHECK(tb_symbol_add(UINT64_MAX, 0, &result) && result == UINT64_MAX);
    CHECK(!tb_symbol_add(UINT64_MAX, 1, &result) && result == UINT64_MAX);
    CHECK(!tb_symbol_add(1, 1, NULL));
    CHECK(tb_symbol_multiply(UINT64_MAX, 1, &result) && result == UINT64_MAX);
    CHECK(!tb_symbol_multiply(UINT64_MAX, 2, &result) && result == UINT64_MAX);
    CHECK(tb_symbol_multiply(UINT64_MAX, 0, &result) && !result);
    CHECK(!tb_symbol_multiply(0, 0, NULL));
    CHECK(tb_symbol_range(UINT64_MAX, 0, UINT64_MAX));
    CHECK(!tb_symbol_range(UINT64_MAX, 1, UINT64_MAX));
    CHECK(!tb_symbol_range(UINT64_MAX, 2, 1));
    CHECK(tb_symbol_range(1, UINT64_MAX - 1, UINT64_MAX));
    CHECK(tb_symbol_range(0, 0, 0));
    CHECK(!tb_symbol_range(1, 0, 0));
}


static void integers(void)
{
    const unsigned char bytes[] = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
    const unsigned      widths[] = {1, 2, 4, 8};
    const uint64_t      little[] = {0x88, 0x7788, 0x55667788, UINT64_C(0x1122334455667788)};
    const uint64_t      big[]    = {0x88, 0x8877, 0x88776655, UINT64_C(0x8877665544332211)};
    for (unsigned order = 0; order < 2; ++order) {
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
            TbSymbolReader_t reader = reader_for(bytes, sizeof(bytes), (TbSymbolByteOrder_e)order);
            uint64_t         value  = 0;
            CHECK(tb_symbol_reader_uint(&reader, widths[i], &value) == TB_SYMBOL_READ_OK);
            CHECK(value == (order == TB_SYMBOL_LITTLE_ENDIAN ? little[i] : big[i]));
            CHECK(reader.offset == widths[i]);

            for (size_t prefix = 0; prefix < widths[i]; ++prefix) {
                reader = reader_for(bytes, prefix, (TbSymbolByteOrder_e)order);
                value  = 123;
                CHECK(tb_symbol_reader_uint(&reader, widths[i], &value) == TB_SYMBOL_READ_TRUNCATED);
                CHECK(!reader.offset && value == 123);
            }

            unsigned char minimum[8] = {0};
            unsigned      high       = order == TB_SYMBOL_BIG_ENDIAN ? 0 : widths[i] - 1;
            minimum[high] = 0x80;
            reader = reader_for(minimum, widths[i], (TbSymbolByteOrder_e)order);
            int64_t signed_value = 0;
            int64_t expected = widths[i] == 8 ? INT64_MIN : -(INT64_C(1) << (widths[i] * 8 - 1));
            CHECK(tb_symbol_reader_sint(&reader, widths[i], &signed_value) == TB_SYMBOL_READ_OK);
            CHECK(signed_value == expected);

            memset(minimum, 0xff, sizeof(minimum));
            reader = reader_for(minimum, widths[i], (TbSymbolByteOrder_e)order);
            CHECK(tb_symbol_reader_sint(&reader, widths[i], &signed_value) == TB_SYMBOL_READ_OK);
            CHECK(signed_value == -1);
        }
    }
    for (unsigned i = 0; i <= 255; ++i) {
        unsigned char    byte   = (unsigned char)i;
        TbSymbolReader_t reader = reader_for(&byte, 1, TB_SYMBOL_BIG_ENDIAN);
        int64_t          value  = 0;
        CHECK(tb_symbol_reader_sint(&reader, 1, &value) == TB_SYMBOL_READ_OK);
        CHECK(value == (i < 128 ? (int64_t)i : (int64_t)i - 256));
    }
}


typedef struct {
    unsigned char bytes[10];
    size_t        length;
    uint64_t      unsigned_value;
    int64_t       signed_value;
    bool          signed_only;
} LebCase_t;


static void leb(void)
{
    const LebCase_t cases[] = {
        {
            .bytes  = {0},
            .length = 1
        },
        {
            .bytes          = {0x3f},
            .length         = 1,
            .unsigned_value = 63,
            .signed_value   = 63
        },
        {
            .bytes          = {0xe5, 0x8e, 0x26},
            .length         = 3,
            .unsigned_value = 624485,
            .signed_value   = 624485
        },
        {
            .bytes        = {0x9b, 0xf1, 0x59},
            .length       = 3,
            .signed_value = -624485,
            .signed_only  = true
        },
        {
            .bytes  = {0x80, 0},
            .length = 2
        },
        {
            .bytes        = {0xff, 0x7f},
            .length       = 2,
            .signed_value = -1,
            .signed_only  = true
        },
        {
            .bytes          = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1},
            .length         = 10,
            .unsigned_value = UINT64_MAX
        },
        {
            .bytes        = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0},
            .length       = 10,
            .signed_value = INT64_MAX,
            .signed_only  = true
        },
        {
            .bytes        = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f},
            .length       = 10,
            .signed_value = INT64_MIN,
            .signed_only  = true
        }
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const LebCase_t *item = &cases[i];
        for (size_t prefix = 0; prefix <= item->length; ++prefix) {
            TbSymbolReader_t reader = reader_for(item->bytes, prefix, TB_SYMBOL_BIG_ENDIAN);
            uint64_t         u      = 42;
            int64_t          s      = 42;
            if (!item->signed_only) {
                TbSymbolReadStatus_e status = tb_symbol_reader_uleb(&reader, &u);
                CHECK(status == (prefix == item->length ? TB_SYMBOL_READ_OK : TB_SYMBOL_READ_TRUNCATED));
                CHECK(u == (prefix == item->length ? item->unsigned_value : 42));
                CHECK(reader.offset == (prefix == item->length ? prefix : 0));
            }
            if (item->signed_only || item->length < 10) {
                reader = reader_for(item->bytes, prefix, TB_SYMBOL_LITTLE_ENDIAN);
                TbSymbolReadStatus_e status = tb_symbol_reader_sleb(&reader, &s);
                CHECK(status == (prefix == item->length ? TB_SYMBOL_READ_OK : TB_SYMBOL_READ_TRUNCATED));
                CHECK(s == (prefix == item->length ? item->signed_value : 42));
                CHECK(reader.offset == (prefix == item->length ? prefix : 0));
            }
        }
    }
    unsigned char overflow[11];
    memset(overflow, 0xff, sizeof(overflow));
    const unsigned char last[] = {2, 0x7e, 0x80, 0xff};
    for (size_t i = 0; i < sizeof(last); ++i) {
        overflow[9] = last[i];
        TbSymbolReader_t reader = reader_for(overflow, sizeof(overflow), TB_SYMBOL_LITTLE_ENDIAN);
        uint64_t         u      = 123;
        int64_t          s      = 123;
        CHECK(tb_symbol_reader_uleb(&reader, &u) == TB_SYMBOL_READ_OVERFLOW);
        CHECK(tb_symbol_reader_sleb(&reader, &s) == TB_SYMBOL_READ_OVERFLOW);
        CHECK(!reader.offset && u == 123 && s == 123);
    }
}


static void slices_and_strings(void)
{
    const unsigned char data[] = {'a', 0, 0xff, 'b', 0};
    TbSymbolReader_t    reader = reader_for(data, sizeof(data), TB_SYMBOL_BIG_ENDIAN);
    TbSymbolReader_t    slice  = {0};
    CHECK(tb_symbol_reader_slice(&reader, 2, &slice) == TB_SYMBOL_READ_OK);
    CHECK(reader.offset == 2 && slice.data == data && slice.length == 2);
    CHECK(slice.byte_order == TB_SYMBOL_BIG_ENDIAN && !slice.offset);
    const char *text   = NULL;
    size_t      length = 999;
    CHECK(tb_symbol_reader_string(&slice, 1, &text, &length) == TB_SYMBOL_READ_LIMIT);
    CHECK(!text && length == 999 && !slice.offset);
    CHECK(tb_symbol_reader_string(&slice, 3, &text, &length) == TB_SYMBOL_READ_OK);
    CHECK(text == (const char *)data && length == 1 && slice.offset == 2);
    CHECK(tb_symbol_reader_string(&reader, SIZE_MAX, &text, &length) == TB_SYMBOL_READ_OK);
    CHECK(length == 2 && (unsigned char)text[0] == 0xff && reader.offset == sizeof(data));

    const unsigned char *bytes = data;
    CHECK(tb_symbol_reader_bytes(&reader, 0, &bytes) == TB_SYMBOL_READ_OK);
    CHECK(bytes == data + sizeof(data));
    CHECK(tb_symbol_reader_bytes(&reader, SIZE_MAX, &bytes) == TB_SYMBOL_READ_TRUNCATED);
    CHECK(bytes == data + sizeof(data) && reader.offset == sizeof(data));
    CHECK(tb_symbol_reader_slice(&reader, SIZE_MAX, &slice) == TB_SYMBOL_READ_TRUNCATED);
    CHECK(slice.length == 2 && slice.offset == 2);
    CHECK(tb_symbol_reader_slice(&reader, 0, &reader) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_string(&reader, 1, &text, &length) == TB_SYMBOL_READ_TRUNCATED);
    CHECK(length == 2);

    reader = reader_for(NULL, 0, TB_SYMBOL_LITTLE_ENDIAN);
    CHECK(tb_symbol_reader_bytes(&reader, 0, &bytes) == TB_SYMBOL_READ_OK && !bytes);
    CHECK(tb_symbol_reader_slice(&reader, 0, &slice) == TB_SYMBOL_READ_OK && !slice.data);
    CHECK(tb_symbol_reader_string(&reader, 0, &text, &length) == TB_SYMBOL_READ_LIMIT);
    CHECK(tb_symbol_reader_init(&reader, NULL, 1, TB_SYMBOL_LITTLE_ENDIAN) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_init(&reader, data, 1, (TbSymbolByteOrder_e)2) == TB_SYMBOL_READ_INVALID);
    CHECK(!reader.data && !reader.length);
    uint64_t value = 123;
    CHECK(tb_symbol_reader_uint(&reader, 3, &value) == TB_SYMBOL_READ_INVALID && value == 123);
    CHECK(tb_symbol_reader_uint(NULL, 8, &value) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_uint(&reader, 1, NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_sint(&reader, 1, NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_uleb(&reader, NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_sleb(&reader, NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_bytes(&reader, 0, NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_string(&reader, 0, NULL, &length) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_string(&reader, 0, &text, NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_slice(&reader, 0, NULL) == TB_SYMBOL_READ_INVALID);
    CHECK(tb_symbol_reader_init(NULL, NULL, 0, TB_SYMBOL_LITTLE_ENDIAN) == TB_SYMBOL_READ_INVALID);
}


static void identities(void)
{
    TbSymbolRecordId_t record = {
        .content_id    = 1,
        .kind          = TB_SYMBOL_RECORD_DIE,
        .section_index = 4,
        .unit_offset   = 16,
        .record_offset = 32
    };
    CHECK(tb_symbol_record_valid(&record));
    CHECK(tb_symbol_record_equal(&record, &record));
    TbSymbolRecordId_t other = record;
    other.content_id = 2;
    CHECK(!tb_symbol_record_equal(&record, &other));
    other = record;
    other.section_index++;
    CHECK(!tb_symbol_record_equal(&record, &other));
    other = record;
    other.unit_offset++;
    CHECK(!tb_symbol_record_equal(&record, &other));
    other = record;
    other.record_offset++;
    CHECK(!tb_symbol_record_equal(&record, &other));
    record.unit_offset = 0;
    other              = record;
    other.kind         = TB_SYMBOL_RECORD_SYMTAB;
    CHECK(tb_symbol_record_valid(&other) && !tb_symbol_record_equal(&record, &other));
    record.kind = TB_SYMBOL_RECORD_DYNSYM;
    CHECK(!tb_symbol_record_equal(&record, &other));
    other.unit_offset = 1;
    CHECK(!tb_symbol_record_valid(&other));
    other = record;
    other.content_id = 0;
    CHECK(!tb_symbol_record_equal(&other, &other));
    other = record;
    other.section_index = 0;
    CHECK(!tb_symbol_record_valid(&other));
    other = record;
    other.kind = TB_SYMBOL_RECORD_INVALID;
    CHECK(!tb_symbol_record_valid(&other));
    other = record;
    other.kind = TB_SYMBOL_RECORD_DIE;
    other.unit_offset = other.record_offset + 1;
    CHECK(!tb_symbol_record_valid(&other));
    CHECK(!tb_symbol_record_valid(NULL) && !tb_symbol_record_equal(NULL, &record));
}


int main(void)
{
    arithmetic();
    integers();
    leb();
    slices_and_strings();
    identities();
    puts("PASS checked symbol readers, arithmetic, LEB128, bounded strings and physical record coordinates");
    return 0;
}
