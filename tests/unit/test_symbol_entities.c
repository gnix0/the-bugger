#include "symbol/entities.h"
#include "../support/check.h"

#include <stdlib.h>
#include <string.h>

static const uint64_t names[TB_SYMBOL_ENTITY_FIELDS] = {3, 0x6e, 0x3c, 0x3f, 0x34, 0x20,
                                                        0x47, 0x31, 0x49};
static TbSymbolDieEntry_t entry;
static TbSymbolDieAttribute_t attributes[TB_SYMBOL_ENTITY_FIELDS + 1];
static TbSymbolDiesView_t tree;

static void fixture(uint64_t tag)
{
    entry = (TbSymbolDieEntry_t){
        .offset          = 12,
        .end_offset      = 128,
        .parent          = UINT64_MAX,
        .tag             = tag,
        .attribute_count = TB_SYMBOL_ENTITY_FIELDS
    };
    for (size_t i = 0; i < TB_SYMBOL_ENTITY_FIELDS; ++i) {
        attributes[i] = (TbSymbolDieAttribute_t){
            .schema_attribute = i,
            .name             = names[i],
            .declared_form    = TB_SYMBOL_FORM_FLAG,
            .offset           = 13 + i,
            .value            = {
                .form           = TB_SYMBOL_FORM_FLAG,
                .bytes          = 1,
                .payload_offset = 13 + i,
                .payload_bytes  = 1,
                .bits           = i == TB_SYMBOL_ENTITY_DECLARATION ? 0 : 255,
                .numeric        = true
            }
        };
    }
    tree = (TbSymbolDiesView_t){
        .entries         = &entry,
        .attributes      = attributes,
        .count           = 1,
        .attribute_count = TB_SYMBOL_ENTITY_FIELDS,
        .section_index   = 2,
        .unit_offset     = 0,
        .abbrev_section  = 3,
        .abbrev_offset   = 14,
        .backing         = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        }
    };
}


static void unavailable(const TbSymbolEntity_t *entity)
{
    TbSymbolEntityView_t view;
    unsigned char before[sizeof(view)];
    memset(&view, 0xa5, sizeof(view));
    memcpy(before, &view, sizeof(view));
    CHECK(!tb_symbol_entity_view(entity, &view));
    CHECK(!memcmp(before, &view, sizeof(view)));
}


static TbSymbolEntityStatus_e complete(TbSymbolEntity_t *entity, size_t allowance)
{
    TbSymbolEntityStatus_e status = TB_SYMBOL_ENTITY_PENDING;
    for (uint64_t turn = 0; turn <= tree.attribute_count && status == TB_SYMBOL_ENTITY_PENDING; ++turn) {
        unavailable(entity);
        uint64_t before = entity->cursor;
        status = tb_symbol_entity_step(entity, allowance);
        CHECK(entity->cursor >= before && entity->cursor - before <= allowance);
    }
    CHECK(status != TB_SYMBOL_ENTITY_PENDING);
    CHECK(tb_symbol_entity_step(entity, allowance) == status);
    return status;
}


static void direct_facts(void)
{
    const uint64_t tags[] = {0x2e, 0x34, 0x1d, 0x05, UINT64_MAX};
    const TbSymbolEntityKind_e kinds[] = {TB_SYMBOL_ENTITY_FUNCTION, TB_SYMBOL_ENTITY_DATA,
                                          TB_SYMBOL_ENTITY_INLINE_INSTANCE, TB_SYMBOL_ENTITY_OTHER,
                                          TB_SYMBOL_ENTITY_OTHER};
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        fixture(tags[i]);
        TbSymbolEntity_t entity;
        CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
        CHECK(complete(&entity, 1) == TB_SYMBOL_ENTITY_READY);
        TbSymbolEntityView_t view;
        CHECK(tb_symbol_entity_view(&entity, &view));
        CHECK(view.kind == kinds[i] && view.tag == tags[i]);
        CHECK(view.record_offset == entry.offset && view.parent_index == UINT64_MAX);
        CHECK(view.backing.key.inode == 2 && view.section_index == 2 && !view.unit_offset);
        CHECK(view.abbrev_section == 3 && view.abbrev_offset == 14);
        CHECK(view.declaration.present && !view.declaration.value);
        CHECK(view.external.present && view.external.value);
        CHECK(view.artificial.present && view.artificial.value);
        for (size_t field = 0; field < TB_SYMBOL_ENTITY_FIELDS; ++field) {
            CHECK(view.fields[field].selection.matches == 1);
            CHECK(view.fields[field].selection.first_index == field);
            CHECK(view.fields[field].selection.first_offset == attributes[field].offset);
            CHECK(view.fields[field].selection.second_index == UINT64_MAX);
            CHECK(view.fields[field].selection.name == names[field]);
            CHECK(view.fields[field].attribute.name == names[field]);
            CHECK(view.fields[field].attribute.offset == attributes[field].offset);
        }
        TbSymbolEntityField_e failed = TB_SYMBOL_ENTITY_FIELDS;
        TbSymbolAttributeResult_t selection;
        CHECK(!tb_symbol_entity_failure(&entity, &failed, &selection));
        CHECK(failed == TB_SYMBOL_ENTITY_FIELDS);
    }
    fixture(0x2e);
    entry.attribute_count = 0;
    tree.attributes = NULL;
    tree.attribute_count = 0;
    TbSymbolEntity_t entity;
    CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
    CHECK(complete(&entity, 256) == TB_SYMBOL_ENTITY_READY);
    TbSymbolEntityView_t view;
    CHECK(tb_symbol_entity_view(&entity, &view));
    CHECK(!view.declaration.present && !view.external.present && !view.artificial.present);
    for (size_t field = 0; field < TB_SYMBOL_ENTITY_FIELDS; ++field) {
        CHECK(!view.fields[field].selection.matches);
        CHECK(view.fields[field].selection.first_index == UINT64_MAX);
    }
}


static void flag_values(void)
{
    for (unsigned value = 0; value <= UINT8_MAX; ++value) {
        fixture(0x2e);
        attributes[TB_SYMBOL_ENTITY_DECLARATION].value.bits = value;
        TbSymbolEntity_t entity;
        CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
        CHECK(complete(&entity, 256) == TB_SYMBOL_ENTITY_READY);
        TbSymbolEntityView_t view;
        CHECK(tb_symbol_entity_view(&entity, &view));
        CHECK(view.declaration.present && view.declaration.value == (value != 0));
    }
    fixture(0x34);
    attributes[TB_SYMBOL_ENTITY_EXTERNAL].declared_form = TB_SYMBOL_FORM_INDIRECT;
    attributes[TB_SYMBOL_ENTITY_EXTERNAL].value.form = TB_SYMBOL_FORM_FLAG_PRESENT;
    attributes[TB_SYMBOL_ENTITY_EXTERNAL].value.bits = 1;
    attributes[TB_SYMBOL_ENTITY_EXTERNAL].value.bytes = 1;
    attributes[TB_SYMBOL_ENTITY_EXTERNAL].value.payload_bytes = 0;
    attributes[TB_SYMBOL_ENTITY_EXTERNAL].value.indirect = true;
    TbSymbolEntity_t entity;
    CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
    CHECK(complete(&entity, 2) == TB_SYMBOL_ENTITY_READY);
    TbSymbolEntityView_t view;
    CHECK(tb_symbol_entity_view(&entity, &view) && view.external.present && view.external.value);
}


static void malformed_and_conflicting(void)
{
    for (unsigned change = 0; change < 5; ++change) {
        fixture(0x2e);
        TbSymbolFormView_t *value = &attributes[TB_SYMBOL_ENTITY_EXTERNAL].value;
        switch (change) {
        case 0:
            value->form = TB_SYMBOL_FORM_DATA1;
            break;
        case 1:
            value->numeric = false;
            break;
        case 2:
            value->is_signed = true;
            break;
        case 3:
            value->bits = 256;
            break;
        case 4:
            value->form = TB_SYMBOL_FORM_FLAG_PRESENT;
            value->bits = 0;
            break;
        }
        TbSymbolEntity_t entity;
        CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
        CHECK(complete(&entity, 256) == TB_SYMBOL_ENTITY_MALFORMED);
        unavailable(&entity);
        TbSymbolEntityField_e field;
        TbSymbolAttributeResult_t result;
        CHECK(tb_symbol_entity_failure(&entity, &field, &result));
        CHECK(field == TB_SYMBOL_ENTITY_EXTERNAL && result.matches == 1);
        CHECK(result.first_index == TB_SYMBOL_ENTITY_EXTERNAL);
    }
    for (size_t duplicate = 0; duplicate < TB_SYMBOL_ENTITY_FIELDS; ++duplicate) {
        fixture(0x2e);
        attributes[TB_SYMBOL_ENTITY_FIELDS] = attributes[duplicate];
        attributes[TB_SYMBOL_ENTITY_FIELDS].offset = 100;
        ++entry.attribute_count;
        ++tree.attribute_count;
        TbSymbolEntity_t entity;
        CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
        CHECK(complete(&entity, 1) == TB_SYMBOL_ENTITY_CONFLICT);
        unavailable(&entity);
        TbSymbolEntityField_e field;
        TbSymbolAttributeResult_t result;
        CHECK(tb_symbol_entity_failure(&entity, &field, &result));
        CHECK(field == (TbSymbolEntityField_e)duplicate && result.matches == 2);
        CHECK(result.first_index == duplicate && result.second_index == TB_SYMBOL_ENTITY_FIELDS);
        CHECK(result.second_offset == 100);
    }
}


static void large_scope(void)
{
    fixture(0x34);
    const uint64_t count = 100001;
    TbSymbolDieAttribute_t *large = calloc(count, sizeof(*large));
    CHECK(large);
    for (uint64_t i = 0; i < count; ++i) {
        large[i] = attributes[TB_SYMBOL_ENTITY_EXTERNAL];
        large[i].name = 0x2000;
        large[i].offset = 13 + i;
        large[i].value.payload_offset = 13 + i;
    }
    large[0].name = 0x3f;
    large[count - 1].name = 0x3f;
    tree.attributes = large;
    tree.attribute_count = count;
    entry.attribute_count = count;
    entry.end_offset = 13 + count;
    tree.backing.bytes = entry.end_offset + 128;
    TbSymbolEntity_t entity;
    CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
    CHECK(complete(&entity, 256) == TB_SYMBOL_ENTITY_CONFLICT);
    TbSymbolEntityField_e field;
    TbSymbolAttributeResult_t result;
    CHECK(tb_symbol_entity_failure(&entity, &field, &result));
    CHECK(field == TB_SYMBOL_ENTITY_EXTERNAL && result.second_index == count - 1);
    free(large);
}


static void distinct_scopes(void)
{
    fixture(0x34);
    TbSymbolDieEntry_t entries[2] = {
        entry,
        entry
    };
    entries[0].offset = 12;
    entries[0].end_offset = 13;
    entries[0].attribute_count = 0;
    entries[0].children = true;
    entries[0].tag = 0x11;
    entries[1].offset = 13;
    entries[1].parent = 0;
    tree.entries = entries;
    tree.count = 2;
    for (size_t i = 0; i < TB_SYMBOL_ENTITY_FIELDS; ++i)
        ++attributes[i].offset;
    TbSymbolEntity_t root;
    TbSymbolEntity_t child;
    CHECK(tb_symbol_entity_init(&root, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
    CHECK(tb_symbol_entity_init(&child, tree, 1) == TB_SYMBOL_ENTITY_PENDING);
    CHECK(complete(&root, 1) == TB_SYMBOL_ENTITY_READY);
    CHECK(complete(&child, 1) == TB_SYMBOL_ENTITY_READY);
    TbSymbolEntityView_t root_view;
    TbSymbolEntityView_t child_view;
    CHECK(tb_symbol_entity_view(&root, &root_view));
    CHECK(tb_symbol_entity_view(&child, &child_view));
    CHECK(root_view.record_offset != child_view.record_offset);
    CHECK(root_view.kind == TB_SYMBOL_ENTITY_OTHER && child_view.kind == TB_SYMBOL_ENTITY_DATA);
    CHECK(!root_view.external.present && child_view.external.present);
    CHECK(child_view.parent_index == 0);
    unsigned char before[sizeof(child)];
    memcpy(before, &child, sizeof(child));
    CHECK(tb_symbol_entity_init(&child, tree, 2) == TB_SYMBOL_ENTITY_INVALID);
    CHECK(!memcmp(before, &child, sizeof(child)));
}


static void invalid_requests(void)
{
    fixture(0x2e);
    TbSymbolEntity_t entity;
    memset(&entity, 0, sizeof(entity));
    CHECK(tb_symbol_entity_step(&entity, 1) == TB_SYMBOL_ENTITY_INVALID);
    CHECK(tb_symbol_entity_init(NULL, tree, 0) == TB_SYMBOL_ENTITY_INVALID);
    CHECK(tb_symbol_entity_init(&entity, tree, 1) == TB_SYMBOL_ENTITY_INVALID);
    CHECK(!entity.initialized);
    CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
    CHECK(tb_symbol_entity_step(&entity, 257) == TB_SYMBOL_ENTITY_INVALID);
    unavailable(&entity);
    CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_PENDING);
    CHECK(tb_symbol_entity_step(&entity, 0) == TB_SYMBOL_ENTITY_INVALID);
    tree.entries = NULL;
    CHECK(tb_symbol_entity_init(&entity, tree, 0) == TB_SYMBOL_ENTITY_INVALID);
    CHECK(tb_symbol_entity_step(NULL, 1) == TB_SYMBOL_ENTITY_INVALID);
    CHECK(!tb_symbol_entity_view(NULL, NULL));
    CHECK(!tb_symbol_entity_failure(NULL, NULL, NULL));
}


int main(void)
{
    direct_facts();
    flag_values();
    malformed_and_conflicting();
    large_scope();
    distinct_scopes();
    invalid_requests();
    puts("PASS exact entry facts, complete field uniqueness and direct DWARF flag semantics");
    return 0;
}
