#include "symbol/entities.h"
#include "../support/check.h"

static TbSymbolEntity_t       entity;
static TbSymbolDieEntry_t     entries[2];
static TbSymbolDieAttribute_t attributes[4];
static TbSymbolDiesView_t     tree;

static void fixture(uint64_t tag, unsigned declaration)
{
    entries[0] = (TbSymbolDieEntry_t){
        .offset     = 12,
        .end_offset = 16,
        .parent     = UINT64_MAX,
        .code       = 1,
        .tag        = 0x11,
        .children   = true
    };
    entries[1] = (TbSymbolDieEntry_t){
        .offset          = 32,
        .end_offset      = 64,
        .parent          = 0,
        .code            = 2,
        .tag             = tag,
        .attribute_count = declaration ? 1 : 0
    };
    attributes[0] = (TbSymbolDieAttribute_t){
        .name          = 0x3c,
        .declared_form = TB_SYMBOL_FORM_FLAG,
        .offset        = 33,
        .value         = {
            .form    = TB_SYMBOL_FORM_FLAG,
            .bits    = declaration == 1 ? 0 : 255,
            .numeric = true
        }
    };
    tree = (TbSymbolDiesView_t){
        .entries         = entries,
        .attributes      = attributes,
        .count           = 2,
        .attribute_count = declaration ? 1 : 0,
        .section_index   = 2,
        .abbrev_section  = 3,
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


static TbSymbolEntityStatus_e complete(void)
{
    CHECK(tb_symbol_entity_init(&entity, tree, 1) == TB_SYMBOL_ENTITY_PENDING);
    for (unsigned i = 0; i < 20; ++i) {
        TbSymbolEntityRole_e role = TB_SYMBOL_ROLE_OTHER;
        CHECK(!tb_symbol_entity_role(&entity, &role));
        CHECK(role == TB_SYMBOL_ROLE_OTHER);
        TbSymbolEntityStatus_e status = tb_symbol_entity_step(&entity, 1);
        if (status != TB_SYMBOL_ENTITY_PENDING)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_ENTITY_INVALID;
}


int main(void)
{
    const uint64_t tags[] = {0x2e, 0x34};
    for (size_t kind = 0; kind < 2; ++kind) {
        for (unsigned declaration = 0; declaration < 3; ++declaration) {
            fixture(tags[kind], declaration);
            /* Reference presence/direction does not change the selected
             * record's own declaration flag; target validation is separate. */
            attributes[tree.attribute_count] = (TbSymbolDieAttribute_t){
                .name          = 0x47,
                .declared_form = TB_SYMBOL_FORM_REF4,
                .offset        = 34,
                .value         = {
                    .form    = TB_SYMBOL_FORM_REF4,
                    .bits    = 128,
                    .numeric = true
                }
            };
            ++entries[1].attribute_count;
            ++tree.attribute_count;
            attributes[tree.attribute_count] = attributes[tree.attribute_count - 1];
            attributes[tree.attribute_count].name = 0x31;
            ++attributes[tree.attribute_count].offset;
            ++entries[1].attribute_count;
            ++tree.attribute_count;
            CHECK(complete() == TB_SYMBOL_ENTITY_READY);
            TbSymbolEntityRole_e role;
            CHECK(tb_symbol_entity_role(&entity, &role));
            CHECK(role == (declaration == 2 ? TB_SYMBOL_ROLE_DECLARATION : TB_SYMBOL_ROLE_DEFINITION));
            TbSymbolEntityView_t view;
            CHECK(tb_symbol_entity_view(&entity, &view));
            CHECK(view.fields[TB_SYMBOL_ENTITY_SPECIFICATION].selection.matches == 1);
            CHECK(view.fields[TB_SYMBOL_ENTITY_ORIGIN].selection.matches == 1);
            CHECK(view.declaration.present == (declaration != 0));
            CHECK(!view.external.present);
        }
    }
    fixture(0x1d, 0);
    CHECK(complete() == TB_SYMBOL_ENTITY_READY);
    TbSymbolEntityRole_e role;
    CHECK(tb_symbol_entity_role(&entity, &role) && role == TB_SYMBOL_ROLE_INLINE_INSTANCE);
    fixture(0x13, 2);
    CHECK(complete() == TB_SYMBOL_ENTITY_READY);
    CHECK(tb_symbol_entity_role(&entity, &role) && role == TB_SYMBOL_ROLE_OTHER);
    fixture(0x34, 2);
    attributes[0].value.form = TB_SYMBOL_FORM_DATA1;
    CHECK(complete() == TB_SYMBOL_ENTITY_MALFORMED);
    CHECK(!tb_symbol_entity_role(&entity, &role));
    fixture(0x34, 2);
    attributes[1] = attributes[0];
    attributes[1].offset++;
    ++entries[1].attribute_count;
    ++tree.attribute_count;
    CHECK(complete() == TB_SYMBOL_ENTITY_CONFLICT);
    CHECK(!tb_symbol_entity_role(&entity, &role));
    role = TB_SYMBOL_ROLE_OTHER;
    CHECK(!tb_symbol_entity_role(&entity, &role) && role == TB_SYMBOL_ROLE_OTHER);
    CHECK(!tb_symbol_entity_role(NULL, &role));
    CHECK(!tb_symbol_entity_role(&entity, NULL));
    puts("PASS direct declaration roles, reference independence, inline/other categories and failures");
    return 0;
}
