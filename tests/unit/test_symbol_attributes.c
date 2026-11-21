#include "symbol/attributes.h"
#include "../support/check.h"

#include <stdlib.h>
#include <string.h>

static TbSymbolDieEntry_t entries[2];
static TbSymbolDieAttribute_t attributes[6];
static TbSymbolDiesView_t tree;

static void fixture(void)
{
    entries[0] = (TbSymbolDieEntry_t){
        .offset          = 112,
        .end_offset      = 116,
        .parent          = UINT64_MAX,
        .first_attribute = 0,
        .attribute_count = 3,
        .children        = true
    };
    entries[1] = (TbSymbolDieEntry_t){
        .offset          = 116,
        .end_offset      = 120,
        .parent          = 0,
        .first_attribute = 3,
        .attribute_count = 3
    };
    const uint64_t names[] = {0x3c, 0x3f, 0x3c, 0x3f, 0x49, 0x3f};
    for (size_t i = 0; i < 6; ++i) {
        attributes[i] = (TbSymbolDieAttribute_t){
            .name          = names[i],
            .declared_form = TB_SYMBOL_FORM_FLAG,
            .offset        = i < 3 ? 113 + i : 114 + i,
            .value         = {
                .form           = TB_SYMBOL_FORM_FLAG,
                .bytes          = 1,
                .payload_offset = i < 3 ? 113 + i : 114 + i,
                .payload_bytes  = 1,
                .bits           = 1,
                .numeric        = true
            }
        };
    }
    tree = (TbSymbolDiesView_t){
        .entries         = entries,
        .attributes      = attributes,
        .count           = 2,
        .attribute_count = 6,
        .max_depth       = 1,
        .section_index   = 7,
        .unit_offset     = 100,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing         = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        }
    };
}


static void unavailable(const TbSymbolAttributes_t *lookup, TbSymbolAttributeStatus_e expected)
{
    TbSymbolAttributeResult_t result;
    unsigned char before[sizeof(result)];
    memset(&result, 0xa5, sizeof(result));
    memcpy(before, &result, sizeof(result));
    CHECK(tb_symbol_attributes_result(lookup, &result) == expected);
    CHECK(!memcmp(before, &result, sizeof(result)));
    uint64_t index = 37;
    CHECK(!tb_symbol_attributes_unique(lookup, &index) && index == 37);
}


static TbSymbolAttributeResult_t query(TbSymbolAttributeSelection_t selection,
                                       TbSymbolAttributeStatus_e expected, size_t allowance)
{
    TbSymbolAttributes_t lookup;
    CHECK(tb_symbol_attributes_init(&lookup, tree, selection) == TB_SYMBOL_ATTRIBUTE_PENDING);
    TbSymbolAttributeStatus_e status = TB_SYMBOL_ATTRIBUTE_PENDING;
    uint64_t steps = 0;
    while (status == TB_SYMBOL_ATTRIBUTE_PENDING) {
        unavailable(&lookup, TB_SYMBOL_ATTRIBUTE_PENDING);
        uint64_t before = lookup.cursor;
        status = tb_symbol_attributes_step(&lookup, allowance);
        CHECK(lookup.cursor >= before && lookup.cursor - before <= allowance);
        CHECK(++steps <= tree.attribute_count + 1);
    }
    CHECK(status == expected && tb_symbol_attributes_step(&lookup, allowance) == expected);
    TbSymbolAttributeResult_t result;
    CHECK(tb_symbol_attributes_result(&lookup, &result) == expected);
    CHECK(result.section_index == tree.section_index && result.unit_offset == tree.unit_offset);
    CHECK(result.record_offset == tree.entries[selection.die_index].offset && result.name == selection.name);
    CHECK(result.backing.key.inode == tree.backing.key.inode);
    uint64_t index = 37;
    CHECK(tb_symbol_attributes_unique(&lookup, &index) == (expected == TB_SYMBOL_ATTRIBUTE_UNIQUE));
    CHECK(index == (expected == TB_SYMBOL_ATTRIBUTE_UNIQUE ? result.first_index : 37));
    return result;
}


static void scoped_selection(void)
{
    fixture();
    const size_t allowances[] = {1, 2, 256};
    for (size_t i = 0; i < sizeof(allowances) / sizeof(allowances[0]); ++i) {
        TbSymbolAttributeResult_t result = query((TbSymbolAttributeSelection_t){
            .die_index = 0,
            .name      = 0x3f
        }, TB_SYMBOL_ATTRIBUTE_UNIQUE, allowances[i]);
        CHECK(result.matches == 1 && result.first_index == 1 && result.first_offset == 114);
        CHECK(result.second_index == UINT64_MAX && result.second_offset == UINT64_MAX);
        result = query((TbSymbolAttributeSelection_t){
            .die_index = 0,
            .name      = 0x3c
        }, TB_SYMBOL_ATTRIBUTE_AMBIGUOUS, allowances[i]);
        CHECK(result.matches == 2 && result.first_index == 0 && result.second_index == 2);
        CHECK(result.first_offset == 113 && result.second_offset == 115);
        CHECK(attributes[0].value.bits == attributes[2].value.bits);
        result = query((TbSymbolAttributeSelection_t){
            .die_index = 0,
            .name      = 0x49
        }, TB_SYMBOL_ATTRIBUTE_MISSING, allowances[i]);
        CHECK(!result.matches && result.first_index == UINT64_MAX && result.second_index == UINT64_MAX);
        result = query((TbSymbolAttributeSelection_t){
            .die_index = 1,
            .name      = 0x49
        }, TB_SYMBOL_ATTRIBUTE_UNIQUE, allowances[i]);
        CHECK(result.matches == 1 && result.first_index == 4 && result.first_offset == 118);
        result = query((TbSymbolAttributeSelection_t){
            .die_index = 1,
            .name      = 0x3f
        }, TB_SYMBOL_ATTRIBUTE_AMBIGUOUS, allowances[i]);
        CHECK(result.matches == 2 && result.first_index == 3 && result.second_index == 5);
        CHECK(result.first_offset == 117 && result.second_offset == 119);
    }
}


static void empty_and_invalid(void)
{
    fixture();
    entries[0].attribute_count = 0;
    tree.attribute_count = 0;
    tree.attributes = NULL;
    TbSymbolAttributeResult_t result = query((TbSymbolAttributeSelection_t){
        .die_index = 0,
        .name      = 3
    }, TB_SYMBOL_ATTRIBUTE_MISSING, 1);
    CHECK(!result.matches && result.first_offset == UINT64_MAX && result.second_offset == UINT64_MAX);
    TbSymbolAttributes_t lookup = {0};
    unavailable(&lookup, TB_SYMBOL_ATTRIBUTE_INVALID);
    CHECK(tb_symbol_attributes_step(NULL, 1) == TB_SYMBOL_ATTRIBUTE_INVALID);
    CHECK(tb_symbol_attributes_step(&lookup, 1) == TB_SYMBOL_ATTRIBUTE_INVALID);
    CHECK(tb_symbol_attributes_init(NULL, tree, (TbSymbolAttributeSelection_t){
        .die_index = 0,
        .name      = 3
    }) == TB_SYMBOL_ATTRIBUTE_INVALID);
    fixture();
    const TbSymbolAttributeSelection_t bad[] = {
        {
            .die_index = 2,
            .name      = 3
        },
        {
            .die_index = 0,
            .name      = 0
        }
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        TbSymbolAttributes_t before = lookup;
        CHECK(tb_symbol_attributes_init(&lookup, tree, bad[i]) == TB_SYMBOL_ATTRIBUTE_INVALID);
        CHECK(!memcmp(&lookup, &before, sizeof(lookup)));
    }
    entries[0].first_attribute = UINT64_MAX;
    CHECK(tb_symbol_attributes_init(&lookup, tree, (TbSymbolAttributeSelection_t){
        .die_index = 0,
        .name      = 3
    }) == TB_SYMBOL_ATTRIBUTE_INVALID);
    fixture();
    CHECK(tb_symbol_attributes_init(&lookup, tree, (TbSymbolAttributeSelection_t){
        .die_index = 0,
        .name      = 0x3f
    }) == TB_SYMBOL_ATTRIBUTE_PENDING);
    CHECK(tb_symbol_attributes_step(&lookup, 1) == TB_SYMBOL_ATTRIBUTE_PENDING);
    CHECK(tb_symbol_attributes_step(&lookup, 0) == TB_SYMBOL_ATTRIBUTE_INVALID);
    unavailable(&lookup, TB_SYMBOL_ATTRIBUTE_INVALID);
}


static void late_ambiguity(void)
{
    fixture();
    size_t count = 100001;
    TbSymbolDieAttribute_t *many = calloc(count, sizeof(*many));
    CHECK(many);
    for (size_t i = 0; i < count; ++i) {
        many[i] = (TbSymbolDieAttribute_t){
            .name          = i ? 0x49U : 0x3fU,
            .declared_form = TB_SYMBOL_FORM_FLAG,
            .offset        = 113 + i,
            .value         = {
                .form           = TB_SYMBOL_FORM_FLAG,
                .bytes          = 1,
                .payload_offset = 113 + i,
                .payload_bytes  = 1,
                .numeric        = true
            }
        };
    }
    tree.attributes = many;
    tree.attribute_count = count;
    tree.backing.bytes = 1024 * 1024;
    entries[0].attribute_count = count;
    entries[0].end_offset = 113 + count;
    tree.count = 1;
    TbSymbolAttributeResult_t result = query((TbSymbolAttributeSelection_t){
        .die_index = 0,
        .name      = 0x3f
    }, TB_SYMBOL_ATTRIBUTE_UNIQUE, 256);
    CHECK(result.matches == 1 && !result.first_index);
    many[count - 1].name = 0x3f;
    result = query((TbSymbolAttributeSelection_t){
        .die_index = 0,
        .name      = 0x3f
    }, TB_SYMBOL_ATTRIBUTE_AMBIGUOUS, 256);
    CHECK(result.matches == 2 && !result.first_index && result.second_index == count - 1);
    free(many);
}


int main(void)
{
    scoped_selection();
    empty_and_invalid();
    late_ambiguity();
    puts("PASS bounded scoped attributes, equal-value ambiguity and complete-only selection");
    return 0;
}
