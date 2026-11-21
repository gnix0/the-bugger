#include "symbol/form.h"
#include "../support/check.h"

#include <string.h>

static unsigned char encoded[71000];
static TbSymbolFormRequest_t request;

static void unavailable(const TbSymbolForm_t *form)
{
    TbSymbolFormView_t view;
    unsigned char before[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(before, &view, sizeof(view));
    CHECK(!tb_symbol_form_view(form, &view) && !memcmp(before, &view, sizeof(view)));
}


static TbSymbolFormView_t decode(size_t bytes, size_t payload_offset, size_t payload_bytes)
{
    const size_t chunks[] = {1, 2, 3, 7, 64, 256, 1024};
    TbSymbolFormView_t result = {0};
    for (size_t test = 0; test < sizeof(chunks) / sizeof(chunks[0]); ++test) {
        TbSymbolForm_t form;
        request.available = bytes + 1;
        TbSymbolFormStatus_e status = tb_symbol_form_init(&form, request);
        CHECK(status == (bytes ? TB_SYMBOL_FORM_PENDING : TB_SYMBOL_FORM_COMPLETE));
        size_t offset = 0;
        while (status == TB_SYMBOL_FORM_PENDING) {
            unavailable(&form);
            size_t count = bytes + 1 - offset;
            if (count > chunks[test])
                count = chunks[test];
            size_t taken = SIZE_MAX;
            status = tb_symbol_form_feed(&form, encoded + offset, count, &taken);
            CHECK(taken && taken <= count && taken <= 256);
            offset += taken;
            CHECK(offset <= bytes);
        }
        CHECK(status == TB_SYMBOL_FORM_COMPLETE && offset == bytes);
        CHECK(tb_symbol_form_finish(&form) == TB_SYMBOL_FORM_COMPLETE);
        CHECK(tb_symbol_form_view(&form, &result));
        CHECK(result.bytes == bytes && result.payload_offset == payload_offset &&
              result.payload_bytes == payload_bytes);
        size_t taken = SIZE_MAX;
        CHECK(tb_symbol_form_feed(&form, encoded + bytes, 1, &taken) == TB_SYMBOL_FORM_COMPLETE && !taken);
    }
    return result;
}


static unsigned fixed_width(uint64_t code)
{
    switch (code) {
    case TB_SYMBOL_FORM_ADDR:
        return request.address_bytes;
    case TB_SYMBOL_FORM_DATA1:
    case TB_SYMBOL_FORM_FLAG:
    case TB_SYMBOL_FORM_REF1:
    case TB_SYMBOL_FORM_STRX1:
    case TB_SYMBOL_FORM_ADDRX1:
        return 1;
    case TB_SYMBOL_FORM_DATA2:
    case TB_SYMBOL_FORM_REF2:
    case TB_SYMBOL_FORM_STRX2:
    case TB_SYMBOL_FORM_ADDRX2:
        return 2;
    case TB_SYMBOL_FORM_STRX3:
    case TB_SYMBOL_FORM_ADDRX3:
        return 3;
    case TB_SYMBOL_FORM_DATA4:
    case TB_SYMBOL_FORM_REF4:
    case TB_SYMBOL_FORM_REF_SUP4:
    case TB_SYMBOL_FORM_STRX4:
    case TB_SYMBOL_FORM_ADDRX4:
        return 4;
    case TB_SYMBOL_FORM_DATA8:
    case TB_SYMBOL_FORM_REF8:
    case TB_SYMBOL_FORM_REF_SIG8:
    case TB_SYMBOL_FORM_REF_SUP8:
        return 8;
    case TB_SYMBOL_FORM_DATA16:
        return 16;
    case TB_SYMBOL_FORM_STRP:
    case TB_SYMBOL_FORM_REF_ADDR:
    case TB_SYMBOL_FORM_SEC_OFFSET:
    case TB_SYMBOL_FORM_STRP_SUP:
    case TB_SYMBOL_FORM_LINE_STRP:
        return request.offset_bytes;
    default:
        return 0;
    }
}


static void all_forms(void)
{
    for (uint64_t code = 1; code <= TB_SYMBOL_FORM_ADDRX4; ++code) {
        if (code == 2 || (request.version == 4 && code >= TB_SYMBOL_FORM_STRX &&
                         code != TB_SYMBOL_FORM_REF_SIG8)) {
            continue;
        }
        memset(encoded, 0x31, sizeof(encoded));
        request.form = code;
        unsigned width = fixed_width(code);
        TbSymbolFormView_t view;
        if (width) {
            view = decode(width, 0, width);
            CHECK(view.numeric == (width <= 8));
            if (width <= 8) {
                uint64_t value = 0;
                for (unsigned i = 0; i < width; ++i)
                    value = (value << 8) | 0x31U;
                CHECK(view.bits == value);
            } else {
                CHECK(!view.bits);
            }
        } else if (code == TB_SYMBOL_FORM_STRING) {
            encoded[4] = 0;
            view = decode(5, 0, 4);
            CHECK(!view.numeric);
        } else if (code == TB_SYMBOL_FORM_FLAG_PRESENT || code == TB_SYMBOL_FORM_IMPLICIT_CONST) {
            view = decode(0, 0, 0);
            CHECK(view.numeric == (code == TB_SYMBOL_FORM_FLAG_PRESENT));
            CHECK(view.bits == (code == TB_SYMBOL_FORM_FLAG_PRESENT ? 1U : 0U));
        } else if (code == TB_SYMBOL_FORM_BLOCK || code == TB_SYMBOL_FORM_EXPRLOC ||
                   code == TB_SYMBOL_FORM_BLOCK1 || code == TB_SYMBOL_FORM_BLOCK2 ||
                   code == TB_SYMBOL_FORM_BLOCK4) {
            unsigned prefix = code == TB_SYMBOL_FORM_BLOCK2 ? 2 : code == TB_SYMBOL_FORM_BLOCK4 ? 4 : 1;
            memset(encoded, 0, prefix);
            encoded[request.byte_order == TB_SYMBOL_BIG_ENDIAN ? prefix - 1 : 0] = 3;
            if (code == TB_SYMBOL_FORM_BLOCK || code == TB_SYMBOL_FORM_EXPRLOC)
                encoded[0] = 3;
            view = decode(prefix + 3, prefix, 3);
            CHECK(!view.numeric);
        } else if (code == TB_SYMBOL_FORM_INDIRECT) {
            encoded[0] = TB_SYMBOL_FORM_DATA1;
            view = decode(2, 1, 1);
            CHECK(view.indirect && view.form == TB_SYMBOL_FORM_DATA1 && view.bits == 0x31);
        } else {
            encoded[0] = 0x81;
            encoded[1] = 1;
            view = decode(2, 0, 2);
            CHECK(view.numeric && view.bits == 129);
            CHECK(view.is_signed == (code == TB_SYMBOL_FORM_SDATA));
        }
        CHECK(view.form == (code == TB_SYMBOL_FORM_INDIRECT ? TB_SYMBOL_FORM_DATA1 : code));
    }
}


static void order_and_boundaries(void)
{
    request.form = TB_SYMBOL_FORM_STRX3;
    encoded[0] = 0x12;
    encoded[1] = 0x34;
    encoded[2] = 0x56;
    TbSymbolFormView_t view = decode(3, 0, 3);
    CHECK(view.bits == (request.byte_order == TB_SYMBOL_LITTLE_ENDIAN ? 0x563412U : 0x123456U));
    const uint64_t codes[] = {TB_SYMBOL_FORM_DATA8, TB_SYMBOL_FORM_STRING, TB_SYMBOL_FORM_BLOCK,
                              TB_SYMBOL_FORM_UDATA, TB_SYMBOL_FORM_INDIRECT};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        request.form = codes[i];
        for (uint64_t available = 0; available < 8; ++available) {
            memset(encoded, 0x80, sizeof(encoded));
            request.available = available;
            TbSymbolForm_t form;
            TbSymbolFormStatus_e status = tb_symbol_form_init(&form, request);
            size_t taken = SIZE_MAX;
            if (status == TB_SYMBOL_FORM_PENDING)
                status = tb_symbol_form_feed(&form, encoded, sizeof(encoded), &taken);
            CHECK(status == TB_SYMBOL_FORM_TRUNCATED);
            unavailable(&form);
        }
    }
    request.form = TB_SYMBOL_FORM_STRING;
    request.available = 100;
    TbSymbolForm_t form;
    CHECK(tb_symbol_form_init(&form, request) == TB_SYMBOL_FORM_PENDING);
    size_t taken;
    CHECK(tb_symbol_form_feed(&form, encoded, 1, &taken) == TB_SYMBOL_FORM_PENDING);
    CHECK(tb_symbol_form_finish(&form) == TB_SYMBOL_FORM_TRUNCATED);
    unavailable(&form);
}


static void wide_and_indirect(void)
{
    request.form = TB_SYMBOL_FORM_STRING;
    memset(encoded, 0x42, 70000);
    encoded[70000] = 0;
    TbSymbolFormView_t view = decode(70001, 0, 70000);
    CHECK(!view.numeric);
    request.form = TB_SYMBOL_FORM_BLOCK4;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned shift = request.byte_order == TB_SYMBOL_LITTLE_ENDIAN ? i * 8 : (3 - i) * 8;
        encoded[i] = (unsigned char)(70000U >> shift);
    }
    view = decode(70004, 4, 70000);
    CHECK(!view.numeric);
    request.form = TB_SYMBOL_FORM_SDATA;
    memset(encoded, 0xff, 70000);
    encoded[69999] = 0x7f;
    view = decode(70000, 0, 70000);
    CHECK(view.numeric && view.is_signed && view.bits == UINT64_MAX);
    request.form = TB_SYMBOL_FORM_UDATA;
    view = decode(70000, 0, 70000);
    CHECK(!view.numeric && !view.bits);
    request.form = TB_SYMBOL_FORM_INDIRECT;
    memset(encoded, TB_SYMBOL_FORM_INDIRECT, 70000);
    encoded[70000] = TB_SYMBOL_FORM_IMPLICIT_CONST;
    encoded[70001] = 0x7e;
    view = decode(70002, 70001, 1);
    CHECK(view.form == TB_SYMBOL_FORM_IMPLICIT_CONST && view.indirect && view.is_signed &&
          view.numeric && view.bits == UINT64_MAX - 1);
    encoded[70000] = TB_SYMBOL_FORM_FLAG_PRESENT;
    view = decode(70001, 70001, 0);
    CHECK(view.numeric && view.bits == 1);
    encoded[0] = TB_SYMBOL_FORM_STRING;
    encoded[1] = 'x';
    encoded[2] = 0;
    view = decode(3, 1, 1);
    CHECK(view.indirect && !view.numeric);
    encoded[0] = TB_SYMBOL_FORM_BLOCK;
    encoded[1] = 0;
    view = decode(2, 2, 0);
    CHECK(view.indirect && !view.numeric);
    request.form = TB_SYMBOL_FORM_BLOCK;
    encoded[0] = 0;
    view = decode(1, 1, 0);
    CHECK(!view.numeric);
    memset(encoded, 0x80, 70000);
    encoded[69999] = 0;
    view = decode(70000, 70000, 0);
    CHECK(!view.numeric);
    memset(encoded, 0x80, 10);
    encoded[9] = 2;
    request.available = 100;
    TbSymbolForm_t form;
    CHECK(tb_symbol_form_init(&form, request) == TB_SYMBOL_FORM_PENDING);
    size_t taken;
    CHECK(tb_symbol_form_feed(&form, encoded, 10, &taken) == TB_SYMBOL_FORM_TRUNCATED && taken == 10);
    unavailable(&form);
    request.form = TB_SYMBOL_FORM_INDIRECT;
    CHECK(tb_symbol_form_init(&form, request) == TB_SYMBOL_FORM_PENDING);
    CHECK(tb_symbol_form_feed(&form, encoded, 10, &taken) == TB_SYMBOL_FORM_UNSUPPORTED && taken == 10);
    unavailable(&form);
}


static void invalid(void)
{
    TbSymbolForm_t form = {0};
    size_t taken = 37;
    CHECK(tb_symbol_form_feed(&form, encoded, 1, &taken) == TB_SYMBOL_FORM_INVALID && taken == 37);
    CHECK(tb_symbol_form_finish(NULL) == TB_SYMBOL_FORM_INVALID);
    CHECK(!tb_symbol_form_view(NULL, NULL));
    request.form = TB_SYMBOL_FORM_DATA1;
    request.available = 1;
    CHECK(tb_symbol_form_init(NULL, request) == TB_SYMBOL_FORM_INVALID);
    CHECK(tb_symbol_form_init(&form, request) == TB_SYMBOL_FORM_PENDING);
    TbSymbolForm_t before = form;
    CHECK(tb_symbol_form_feed(&form, NULL, 1, &taken) == TB_SYMBOL_FORM_INVALID && taken == 37);
    CHECK(!memcmp(&form, &before, sizeof(form)));
    CHECK(tb_symbol_form_feed(&form, encoded, 1, NULL) == TB_SYMBOL_FORM_INVALID);
    request.offset_bytes = 3;
    CHECK(tb_symbol_form_init(&form, request) == TB_SYMBOL_FORM_INVALID);
    CHECK(!memcmp(&form, &before, sizeof(form)));
    request.offset_bytes = 4;
    request.form = 2;
    CHECK(tb_symbol_form_init(&form, request) == TB_SYMBOL_FORM_UNSUPPORTED);
    unavailable(&form);
    request.version = 4;
    request.form = TB_SYMBOL_FORM_IMPLICIT_CONST;
    CHECK(tb_symbol_form_init(&form, request) == TB_SYMBOL_FORM_UNSUPPORTED);
}


int main(void)
{
    const unsigned addresses[] = {1, 2, 3, 4, 8, 16, 255};
    for (unsigned version = 4; version <= 5; ++version) {
        for (unsigned order = 0; order < 2; ++order) {
            for (unsigned width = 4; width <= 8; width += 4) {
                for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
                    request = (TbSymbolFormRequest_t){
                        .version       = version,
                        .offset_bytes  = width,
                        .address_bytes = addresses[i],
                        .byte_order    = (TbSymbolByteOrder_e)order
                    };
                    all_forms();
                }
            }
            if (version == 5) {
                order_and_boundaries();
                wide_and_indirect();
            }
        }
    }
    invalid();
    puts("PASS bounded DWARF form framing, exact footprints, indirect chains and wide values");
    return 0;
}
