#include "symbol/constants.h"
#include "symbol/form.h"
#include "../support/check.h"

#include <string.h>

static TbSymbolDieEntry_t entry;
static TbSymbolDieAttribute_t attributes[600];
static TbSymbolDiesView_t tree;
static TbSymbolConstant_t constant;
static TbSymbolAbbrevEntry_t schema_entry;
static TbSymbolAbbrevAttribute_t schema_attributes[600];
static TbSymbolAbbrevView_t schema;

static void fixture(TbSymbolConstantField_e field, uint64_t form, uint64_t bits)
{
    entry = (TbSymbolDieEntry_t){
        .offset          = 12,
        .end_offset      = 2048,
        .parent          = UINT64_MAX,
        .code            = 1,
        .tag             = field == TB_SYMBOL_CONSTANT_LANGUAGE ? 0x11 : 0x2e,
        .attribute_count = 1
    };
    attributes[0] = (TbSymbolDieAttribute_t){
        .name          = (uint64_t)field,
        .declared_form = form,
        .offset        = 13,
        .value         = {
            .form      = form,
            .bits      = bits,
            .numeric   = true,
            .is_signed = form == TB_SYMBOL_FORM_SDATA || form == TB_SYMBOL_FORM_IMPLICIT_CONST
        }
    };
    tree = (TbSymbolDiesView_t){
        .entries         = &entry,
        .attributes      = attributes,
        .count           = 1,
        .attribute_count = 1,
        .section_index   = 2,
        .abbrev_section  = 3,
        .abbrev_offset   = 17,
        .backing         = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        }
    };
    schema_entry = (TbSymbolAbbrevEntry_t){
        .code            = entry.code,
        .tag             = entry.tag,
        .offset          = 17,
        .end_offset      = 40,
        .attribute_count = 1
    };
    schema_attributes[0] = (TbSymbolAbbrevAttribute_t){
        .name            = (uint64_t)field,
        .form            = form,
        .offset          = 18,
        .implicit_offset = 20,
        .implicit_bytes  = 10,
        .implicit_value  = bits > INT64_MAX ? INT64_MIN : (int64_t)bits,
        .implicit_fits   = true
    };
    schema = (TbSymbolAbbrevView_t){
        .entries         = &schema_entry,
        .attributes      = schema_attributes,
        .count           = 1,
        .attribute_count = 1,
        .section_index   = 3,
        .table_offset    = 17,
        .end_offset      = 128,
        .version         = 5,
        .backing         = tree.backing
    };
    if (form == TB_SYMBOL_FORM_IMPLICIT_CONST) {
        attributes[0].value.bits = 0;
        attributes[0].value.numeric = false;
        attributes[0].value.is_signed = false;
    }
    CHECK(tb_symbol_constant_init(&constant, tree, 0, field) == TB_SYMBOL_CONSTANT_PENDING);
}


static void unavailable(void)
{
    TbSymbolConstantView_t result;
    unsigned char before[sizeof(result)];
    memset(&result, 0xa5, sizeof(result));
    memcpy(before, &result, sizeof(before));
    CHECK(!tb_symbol_constant_view(&constant, &result));
    CHECK(!memcmp(before, &result, sizeof(before)));
}


static TbSymbolConstantStatus_e complete(size_t slice)
{
    TbSymbolConstantStatus_e status;
    for (size_t turn = 0; turn < 1000; ++turn) {
        unavailable();
        if (constant.status == TB_SYMBOL_CONSTANT_SCHEMA)
            CHECK(tb_symbol_constant_schema(&constant, &schema) == TB_SYMBOL_CONSTANT_PENDING);
        uint64_t before = constant.lookup.cursor;
        uint64_t schema_before = constant.schema_cursor;
        status = tb_symbol_constant_step(&constant, slice);
        CHECK(constant.lookup.cursor >= before && constant.lookup.cursor - before <= slice);
        CHECK(constant.schema_cursor >= schema_before && constant.schema_cursor - schema_before <= slice);
        if (status != TB_SYMBOL_CONSTANT_PENDING && status != TB_SYMBOL_CONSTANT_SCHEMA)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_CONSTANT_INVALID;
}


static void check_constants(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_DATA1, TB_SYMBOL_FORM_DATA2, TB_SYMBOL_FORM_DATA4,
                             TB_SYMBOL_FORM_DATA8, TB_SYMBOL_FORM_UDATA, TB_SYMBOL_FORM_SDATA,
                             TB_SYMBOL_FORM_IMPLICIT_CONST};
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
        fixture(TB_SYMBOL_CONSTANT_LANGUAGE, forms[i], 0x2a);
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
        TbSymbolConstantView_t view;
        CHECK(tb_symbol_constant_view(&constant, &view));
        CHECK(view.value == 0x2a && view.selection.matches == 1);
        CHECK(view.selection.record_offset == entry.offset && view.attribute.offset == 13);
        CHECK(view.abbrev_section == 3 && view.abbrev_offset == 17);
        CHECK(tb_symbol_language_family(view.value) == TB_SYMBOL_LANGUAGE_CPP);
        CHECK(tb_symbol_constant_step(&constant, 256) == TB_SYMBOL_CONSTANT_FOUND);
    }
    for (uint64_t value = 0; value < 5; ++value) {
        fixture(TB_SYMBOL_CONSTANT_INLINE, TB_SYMBOL_FORM_DATA1, value);
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
        TbSymbolInlineKind_e kind = TB_SYMBOL_INLINE_NOT_INLINED;
        if (value <= 3) {
            CHECK(tb_symbol_inline_kind(value, &kind));
            CHECK((uint64_t)kind == value);
        } else {
            CHECK(!tb_symbol_inline_kind(value, &kind));
            CHECK(kind == TB_SYMBOL_INLINE_NOT_INLINED);
        }
    }
    fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_UDATA, UINT64_MAX);
    CHECK(complete(256) == TB_SYMBOL_CONSTANT_FOUND);
    TbSymbolConstantView_t view;
    CHECK(tb_symbol_constant_view(&constant, &view) && view.value == UINT64_MAX);
    CHECK(tb_symbol_language_family(view.value) == TB_SYMBOL_LANGUAGE_OTHER);
}


static void check_failures(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_FLAG, TB_SYMBOL_FORM_FLAG_PRESENT, TB_SYMBOL_FORM_ADDR,
                             TB_SYMBOL_FORM_REF4, TB_SYMBOL_FORM_SEC_OFFSET, TB_SYMBOL_FORM_EXPRLOC,
                             TB_SYMBOL_FORM_STRING, TB_SYMBOL_FORM_ADDRX, TB_SYMBOL_FORM_STRX};
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
        fixture(TB_SYMBOL_CONSTANT_LANGUAGE, forms[i], 1);
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_MALFORMED);
        unavailable();
    }
    for (unsigned corruption = 0; corruption < 4; ++corruption) {
        fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_DATA1, 1);
        switch (corruption) {
        case 0:
            attributes[0].value.numeric = false;
            break;
        case 1:
            attributes[0].value.is_signed = true;
            break;
        case 2:
            attributes[0].value.bits = 256;
            break;
        case 3:
            attributes[0].value.form = TB_SYMBOL_FORM_SDATA;
            attributes[0].value.is_signed = true;
            attributes[0].value.bits = UINT64_MAX;
            break;
        }
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_MALFORMED);
        unavailable();
        TbSymbolAttributeResult_t selection;
        CHECK(tb_symbol_constant_selection(&constant, &selection) && selection.first_offset == 13);
    }
    fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_DATA16, 1);
    attributes[0].value.numeric = false;
    CHECK(complete(1) == TB_SYMBOL_CONSTANT_UNSUPPORTED);
    unavailable();
    fixture(TB_SYMBOL_CONSTANT_INLINE, TB_SYMBOL_FORM_DATA1, 0);
    entry.attribute_count = 0;
    CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_INLINE) ==
          TB_SYMBOL_CONSTANT_PENDING);
    CHECK(complete(1) == TB_SYMBOL_CONSTANT_ABSENT);
    unavailable();
    TbSymbolAttributeResult_t selection;
    CHECK(tb_symbol_constant_selection(&constant, &selection) && selection.matches == 0);
}


static void check_numeric_boundaries(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_DATA1, TB_SYMBOL_FORM_DATA2, TB_SYMBOL_FORM_DATA4,
                             TB_SYMBOL_FORM_DATA8, TB_SYMBOL_FORM_UDATA, TB_SYMBOL_FORM_SDATA,
                             TB_SYMBOL_FORM_IMPLICIT_CONST};
    const uint64_t maxima[] = {UINT8_MAX, UINT16_MAX, UINT32_MAX, UINT64_MAX, UINT64_MAX,
                              INT64_MAX, INT64_MAX};
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
        fixture(TB_SYMBOL_CONSTANT_LANGUAGE, forms[i], maxima[i]);
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
        TbSymbolConstantView_t view;
        CHECK(tb_symbol_constant_view(&constant, &view) && view.value == maxima[i]);
        if (maxima[i] != UINT64_MAX) {
            fixture(TB_SYMBOL_CONSTANT_LANGUAGE, forms[i], maxima[i] + 1);
            CHECK(complete(1) == TB_SYMBOL_CONSTANT_MALFORMED);
            unavailable();
        }
        fixture(TB_SYMBOL_CONSTANT_INLINE, forms[i], 0);
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
        CHECK(tb_symbol_constant_view(&constant, &view) && view.value == 0);
        if (forms[i] == TB_SYMBOL_FORM_IMPLICIT_CONST)
            attributes[0].value.numeric = true;
        else
            attributes[0].value.is_signed = !attributes[0].value.is_signed;
        CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_INLINE) ==
              TB_SYMBOL_CONSTANT_PENDING);
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_MALFORMED);
        unavailable();
    }
}


static void check_complete_scope(void)
{
    fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_DATA1, 4);
    for (size_t i = 1; i < 512; ++i) {
        attributes[i] = attributes[0];
        attributes[i].name = i == 511 ? 0x13 : 0x03;
        attributes[i].offset = 13 + i;
    }
    tree.attribute_count = 512;
    entry.attribute_count = 512;
    CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
          TB_SYMBOL_CONSTANT_PENDING);
    CHECK(tb_symbol_constant_step(&constant, 256) == TB_SYMBOL_CONSTANT_PENDING);
    unavailable();
    TbSymbolAttributeResult_t selection;
    CHECK(!tb_symbol_constant_selection(&constant, &selection));
    CHECK(complete(1) == TB_SYMBOL_CONSTANT_CONFLICT);
    unavailable();
    CHECK(tb_symbol_constant_selection(&constant, &selection));
    CHECK(selection.matches == 2 && selection.first_offset == 13 && selection.second_offset == 524);
}


static void check_arguments(void)
{
    fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_DATA1, 4);
    unsigned char before[sizeof(constant)];
    memcpy(before, &constant, sizeof(before));
    CHECK(tb_symbol_constant_step(&constant, 0) == TB_SYMBOL_CONSTANT_INVALID);
    CHECK(tb_symbol_constant_step(&constant, 257) == TB_SYMBOL_CONSTANT_INVALID);
    CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_INLINE) ==
          TB_SYMBOL_CONSTANT_INVALID);
    CHECK(tb_symbol_constant_init(&constant, tree, 1, TB_SYMBOL_CONSTANT_LANGUAGE) ==
          TB_SYMBOL_CONSTANT_INVALID);
    CHECK(tb_symbol_constant_init(&constant, tree, 0, (TbSymbolConstantField_e)0x49) ==
          TB_SYMBOL_CONSTANT_INVALID);
    CHECK(!memcmp(before, &constant, sizeof(before)));
    TbSymbolDiesView_t invalid = tree;
    invalid.attributes = NULL;
    CHECK(tb_symbol_constant_init(&constant, invalid, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
          TB_SYMBOL_CONSTANT_INVALID);
    invalid = tree;
    invalid.entries = NULL;
    CHECK(tb_symbol_constant_init(&constant, invalid, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
          TB_SYMBOL_CONSTANT_INVALID);
    entry.parent = 0;
    CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
          TB_SYMBOL_CONSTANT_INVALID);
    entry.parent = UINT64_MAX;
    entry.first_attribute = 1;
    entry.attribute_count = UINT64_MAX;
    CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
          TB_SYMBOL_CONSTANT_INVALID);
    entry.first_attribute = 0;
    entry.attribute_count = 1;
    CHECK(!memcmp(before, &constant, sizeof(before)));
    const uint64_t tags[] = {0x11, 0x3c, 0x41};
    for (size_t i = 0; i < sizeof(tags) / sizeof(*tags); ++i) {
        entry.tag = tags[i];
        CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
              TB_SYMBOL_CONSTANT_PENDING);
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
    }
    CHECK(!tb_symbol_inline_kind(0, NULL));
    CHECK(!tb_symbol_inline_kind(UINT64_MAX, &(TbSymbolInlineKind_e){0}));
}


static void check_language_codes(void)
{
    const uint64_t c[] = {1, 2, 0xc, 0x1d, 0x2c, 0x3e};
    const uint64_t cpp[] = {4, 0x19, 0x1a, 0x21, 0x2a, 0x2b, 0x3a};
    const uint64_t other[] = {0, 3, 0x10, 0x11, 0x13, 0x1c, 0x29, 0x37, UINT64_MAX};
    for (size_t i = 0; i < sizeof(c) / sizeof(*c); ++i)
        CHECK(tb_symbol_language_family(c[i]) == TB_SYMBOL_LANGUAGE_C);
    for (size_t i = 0; i < sizeof(cpp) / sizeof(*cpp); ++i)
        CHECK(tb_symbol_language_family(cpp[i]) == TB_SYMBOL_LANGUAGE_CPP);
    for (size_t i = 0; i < sizeof(other) / sizeof(*other); ++i)
        CHECK(tb_symbol_language_family(other[i]) == TB_SYMBOL_LANGUAGE_OTHER);
}


static void check_framing(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_DATA1, TB_SYMBOL_FORM_DATA2, TB_SYMBOL_FORM_DATA4,
                             TB_SYMBOL_FORM_DATA8};
    for (unsigned order = 0; order < 2; ++order) {
        for (unsigned version = 4; version <= 5; ++version) {
            for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
                size_t width = (size_t)1 << i;
                unsigned char bytes[9] = {0};
                bytes[0] = (unsigned char)forms[i];
                bytes[order ? width : 1] = 0x2a;
                fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_INDIRECT, 0);
                TbSymbolForm_t form;
                CHECK(tb_symbol_form_init(&form, (TbSymbolFormRequest_t){
                    .form          = TB_SYMBOL_FORM_INDIRECT,
                    .available     = width + 1,
                    .version       = version,
                    .offset_bytes  = version == 4 ? 4 : 8,
                    .address_bytes = 4,
                    .byte_order    = order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN
                }) == TB_SYMBOL_FORM_PENDING);
                for (size_t at = 0; at <= width; ++at) {
                    size_t consumed = 0;
                    TbSymbolFormStatus_e status = tb_symbol_form_feed(&form, bytes + at, 1, &consumed);
                    CHECK(consumed == 1);
                    CHECK(status == (at == width ? TB_SYMBOL_FORM_COMPLETE : TB_SYMBOL_FORM_PENDING));
                }
                CHECK(tb_symbol_form_view(&form, &attributes[0].value));
                CHECK(attributes[0].value.indirect && attributes[0].value.form == forms[i]);
                CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
                TbSymbolConstantView_t view;
                CHECK(tb_symbol_constant_view(&constant, &view) && view.value == 0x2a);
                CHECK(view.attribute.declared_form == TB_SYMBOL_FORM_INDIRECT);
            }
        }
    }
}


static void check_leb_framing(void)
{
    const unsigned char values[][3] = {{0x2a, 0, 0}, {0x2a, 0, 0}, {0x7f, 0, 0}, {0xe5, 0x8e, 0x26}};
    const uint64_t forms[] = {TB_SYMBOL_FORM_UDATA, TB_SYMBOL_FORM_SDATA,
                             TB_SYMBOL_FORM_SDATA, TB_SYMBOL_FORM_UDATA};
    const size_t sizes[] = {1, 1, 1, 3};
    const uint64_t expected[] = {42, 42, UINT64_MAX, 624485};
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
        fixture(TB_SYMBOL_CONSTANT_LANGUAGE, forms[i], 0);
        TbSymbolForm_t form;
        CHECK(tb_symbol_form_init(&form, (TbSymbolFormRequest_t){
            .form          = forms[i],
            .available     = sizes[i],
            .version       = 5,
            .offset_bytes  = 8,
            .address_bytes = 4,
            .byte_order    = TB_SYMBOL_BIG_ENDIAN
        }) == TB_SYMBOL_FORM_PENDING);
        size_t consumed = 0;
        CHECK(tb_symbol_form_feed(&form, values[i], sizes[i], &consumed) == TB_SYMBOL_FORM_COMPLETE);
        CHECK(consumed == sizes[i]);
        CHECK(tb_symbol_form_view(&form, &attributes[0].value));
        CHECK(attributes[0].value.bits == expected[i]);
        TbSymbolConstantStatus_e status = complete(1);
        if (i == 2) {
            CHECK(status == TB_SYMBOL_CONSTANT_MALFORMED);
            unavailable();
        } else {
            CHECK(status == TB_SYMBOL_CONSTANT_FOUND);
            TbSymbolConstantView_t view;
            CHECK(tb_symbol_constant_view(&constant, &view) && view.value == expected[i]);
        }
    }
}


static void check_indirect_implicit(void)
{
    fixture(TB_SYMBOL_CONSTANT_INLINE, TB_SYMBOL_FORM_INDIRECT, 0);
    const unsigned char bytes[] = {TB_SYMBOL_FORM_IMPLICIT_CONST, 2};
    TbSymbolForm_t form;
    CHECK(tb_symbol_form_init(&form, (TbSymbolFormRequest_t){
        .form          = TB_SYMBOL_FORM_INDIRECT,
        .available     = sizeof(bytes),
        .version       = 5,
        .offset_bytes  = 4,
        .address_bytes = 8,
        .byte_order    = TB_SYMBOL_LITTLE_ENDIAN
    }) == TB_SYMBOL_FORM_PENDING);
    size_t consumed = 0;
    CHECK(tb_symbol_form_feed(&form, bytes, sizeof(bytes), &consumed) == TB_SYMBOL_FORM_COMPLETE);
    CHECK(consumed == sizeof(bytes) && tb_symbol_form_view(&form, &attributes[0].value));
    CHECK(attributes[0].value.indirect && attributes[0].value.numeric && attributes[0].value.is_signed);
    CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
    TbSymbolConstantView_t view;
    CHECK(tb_symbol_constant_view(&constant, &view) && view.value == 2 && !view.from_abbreviation);
    TbSymbolInlineKind_e kind;
    CHECK(tb_symbol_inline_kind(view.value, &kind) && kind == TB_SYMBOL_INLINE_DECLARED_NOT_INLINED);
}


static void check_implicit_schema(void)
{
    fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_IMPLICIT_CONST, 42);
    attributes[0].value.is_signed = true;
    CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
    fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_IMPLICIT_CONST, 42);
    attributes[1] = attributes[0];
    attributes[1].schema_attribute = 1;
    entry.attribute_count = 2;
    tree.attribute_count = 2;
    CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
          TB_SYMBOL_CONSTANT_PENDING);
    CHECK(tb_symbol_constant_step(&constant, 1) == TB_SYMBOL_CONSTANT_PENDING);
    CHECK(tb_symbol_constant_step(&constant, 1) == TB_SYMBOL_CONSTANT_CONFLICT);
    unavailable();
    TbSymbolAttributeResult_t duplicate;
    CHECK(tb_symbol_constant_selection(&constant, &duplicate));
    CHECK(duplicate.first_offset == duplicate.second_offset &&
          duplicate.first_index != duplicate.second_index);
    for (unsigned corruption = 0; corruption < 5; ++corruption) {
        fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_IMPLICIT_CONST, 42);
        CHECK(tb_symbol_constant_step(&constant, 1) == TB_SYMBOL_CONSTANT_SCHEMA);
        TbSymbolAbbrevView_t wrong = schema;
        switch (corruption) {
        case 0:
            ++wrong.backing.key.inode;
            break;
        case 1:
            ++wrong.section_index;
            break;
        case 2:
            ++wrong.table_offset;
            break;
        case 3:
            wrong.version = 4;
            break;
        case 4:
            wrong.attribute_count = 0;
            break;
        }
        unsigned char before[sizeof(constant)];
        memcpy(before, &constant, sizeof(before));
        CHECK(tb_symbol_constant_schema(&constant, &wrong) == TB_SYMBOL_CONSTANT_INVALID);
        CHECK(!memcmp(before, &constant, sizeof(before)));
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
        TbSymbolConstantView_t view;
        CHECK(tb_symbol_constant_view(&constant, &view) && view.value == 42 && view.from_abbreviation);
        CHECK(!view.attribute.value.numeric && !view.attribute.value.bytes);
        CHECK(view.abbreviation.implicit_offset == 20 && view.abbreviation.implicit_bytes == 10);
    }
    fixture(TB_SYMBOL_CONSTANT_INLINE, TB_SYMBOL_FORM_IMPLICIT_CONST, 2);
    CHECK(tb_symbol_constant_step(&constant, 1) == TB_SYMBOL_CONSTANT_SCHEMA);
    CHECK(tb_symbol_constant_schema(&constant, NULL) == TB_SYMBOL_CONSTANT_DEPENDENCY);
    unavailable();
    TbSymbolAttributeResult_t selection;
    CHECK(tb_symbol_constant_selection(&constant, &selection) && selection.matches == 1);
    fixture(TB_SYMBOL_CONSTANT_INLINE, TB_SYMBOL_FORM_IMPLICIT_CONST, 2);
    schema_attributes[0].implicit_fits = false;
    schema_attributes[0].implicit_bytes = 11;
    CHECK(complete(1) == TB_SYMBOL_CONSTANT_UNSUPPORTED);
    unavailable();
    fixture(TB_SYMBOL_CONSTANT_INLINE, TB_SYMBOL_FORM_IMPLICIT_CONST, 2);
    schema_attributes[0].implicit_value = -1;
    CHECK(complete(1) == TB_SYMBOL_CONSTANT_MALFORMED);
    unavailable();
    for (unsigned corruption = 0; corruption < 4; ++corruption) {
        fixture(TB_SYMBOL_CONSTANT_INLINE, TB_SYMBOL_FORM_IMPLICIT_CONST, 2);
        switch (corruption) {
        case 0:
            ++schema_entry.code;
            break;
        case 1:
            schema_entry.tag = 0x34;
            break;
        case 2:
            schema_entry.children = true;
            break;
        case 3:
            ++schema_entry.first_attribute;
            break;
        }
        CHECK(complete(1) == TB_SYMBOL_CONSTANT_MALFORMED);
        unavailable();
    }
}


static void check_long_schema(void)
{
    fixture(TB_SYMBOL_CONSTANT_LANGUAGE, TB_SYMBOL_FORM_IMPLICIT_CONST, 42);
    TbSymbolAbbrevEntry_t table[512];
    tree.backing.bytes = 100000;
    schema.backing = tree.backing;
    schema.entries = table;
    schema.count = 512;
    schema.attribute_count = 512;
    schema.end_offset = 17 + 512 * 12;
    entry.code = 512;
    attributes[0].schema_attribute = 511;
    for (size_t i = 0; i < 512; ++i) {
        table[i] = (TbSymbolAbbrevEntry_t){
            .code            = i + 1,
            .tag             = entry.tag,
            .offset          = 17 + 12 * i,
            .end_offset      = 26 + 12 * i,
            .first_attribute = i,
            .attribute_count = 1
        };
        schema_attributes[i] = (TbSymbolAbbrevAttribute_t){
            .name            = 0x13,
            .form            = TB_SYMBOL_FORM_IMPLICIT_CONST,
            .offset          = 21 + 12 * i,
            .implicit_offset = 23 + 12 * i,
            .implicit_bytes  = 1,
            .implicit_value  = 42,
            .implicit_fits   = true
        };
    }
    CHECK(tb_symbol_constant_init(&constant, tree, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
          TB_SYMBOL_CONSTANT_PENDING);
    CHECK(complete(1) == TB_SYMBOL_CONSTANT_FOUND);
    TbSymbolConstantView_t view;
    CHECK(tb_symbol_constant_view(&constant, &view) && view.schema_entry == 511 && view.value == 42);
    CHECK(view.attribute.schema_attribute == 511 && constant.schema_cursor == 512);
}


int main(void)
{
    check_constants();
    check_failures();
    check_numeric_boundaries();
    check_complete_scope();
    check_arguments();
    check_language_codes();
    check_framing();
    check_leb_framing();
    check_indirect_implicit();
    check_implicit_schema();
    check_long_schema();
    puts("PASS bounded language and inline constant interpretation");
    return 0;
}
