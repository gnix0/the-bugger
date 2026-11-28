#include "symbol/parameters.h"
#include "../support/check.h"

#include <string.h>

static TbSymbolDieEntry_t     entries[520];
static TbSymbolDieAttribute_t attributes[3];
static TbSymbolDiesView_t     tree;
static TbSymbolParameters_t   parameters;
static TbSymbolParameter_t    arena[8];

static void fixture(void)
{
    memset(entries, 0, sizeof(entries));
    memset(attributes, 0, sizeof(attributes));
    const uint64_t tags[] = {0x11, 0x2e, 0x05, 0x13, 0x05, 0x05, 0x18, 0x2e};
    const uint64_t parents[] = {UINT64_MAX, 0, 1, 1, 3, 1, 1, 0};
    for (size_t i = 0; i < 8; ++i) {
        entries[i] = (TbSymbolDieEntry_t){
            .offset     = 12 + 8 * i,
            .end_offset = 16 + 8 * i,
            .parent     = parents[i],
            .code       = 1,
            .tag        = tags[i],
            .children   = i == 0 || i == 1 || i == 3
        };
    }
    tree = (TbSymbolDiesView_t){
        .entries        = entries,
        .attributes     = attributes,
        .count          = 8,
        .section_index  = 2,
        .unit_offset    = 0,
        .abbrev_section = 3,
        .kind           = TB_SYMBOL_DWARF_COMPILE,
        .backing        = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        }
    };
}


static TbSymbolParametersStatus_e complete(size_t capacity)
{
    CHECK(tb_symbol_parameters_init(&parameters, tree, 1, arena, capacity) == TB_SYMBOL_PARAMETERS_PENDING);
    for (unsigned step = 0; step < 256; ++step) {
        TbSymbolParametersView_t view;
        CHECK(!tb_symbol_parameters_view(&parameters, &view));
        uint64_t before = tb_symbol_parameters_progress(&parameters);
        TbSymbolParametersStatus_e status = tb_symbol_parameters_step(&parameters, 1);
        CHECK(tb_symbol_parameters_progress(&parameters) == before + 1);
        if (status != TB_SYMBOL_PARAMETERS_PENDING)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_PARAMETERS_INVALID;
}


static void check_order(void)
{
    fixture();
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_READY);
    TbSymbolParametersView_t view;
    CHECK(tb_symbol_parameters_view(&parameters, &view) && view.count == 3);
    const uint64_t indexes[] = {2, 5, 6};
    for (size_t i = 0; i < 3; ++i) {
        CHECK(view.parameters[i].record.entry_index == indexes[i]);
        CHECK(view.parameters[i].record.record_offset == entries[indexes[i]].offset);
        CHECK(view.parameters[i].record.backing.key.inode == tree.backing.key.inode);
        CHECK(view.parameters[i].direct.tag == entries[indexes[i]].tag);
        CHECK(view.parameters[i].unspecified == (i == 2));
    }
    CHECK(view.selected.entry_index == 1);
    CHECK(view.selected.record_offset == entries[1].offset);
    CHECK(tb_symbol_parameters_step(&parameters, 256) == TB_SYMBOL_PARAMETERS_READY);
    CHECK(complete(2) == TB_SYMBOL_PARAMETERS_RESOURCE);
    TbSymbolParametersFailure_t failure;
    CHECK(tb_symbol_parameters_failure(&parameters, &failure));
    CHECK(failure.reason == TB_SYMBOL_PARAMETERS_CAPACITY && failure.entry_index == 6);
    CHECK(failure.record_available && failure.record.entry_index == 6);
    CHECK(!failure.selection_available);
    CHECK(!tb_symbol_parameters_view(&parameters, &view));
    CHECK(complete(0) == TB_SYMBOL_PARAMETERS_RESOURCE);
    CHECK(!tb_symbol_parameters_view(&parameters, &view));

    CHECK(tb_symbol_parameters_init(&parameters, tree, 7, arena, 0) == TB_SYMBOL_PARAMETERS_PENDING);
    CHECK(tb_symbol_parameters_step(&parameters, 1) == TB_SYMBOL_PARAMETERS_READY);
    CHECK(tb_symbol_parameters_view(&parameters, &view) && !view.count);
    entries[1].tag = 0x15;
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_READY);
    entries[1].tag = 0x1d;
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_READY);
}


static void check_direct_facts(void)
{
    fixture();
    entries[2].attribute_count = tree.attribute_count = 1;
    attributes[0] = (TbSymbolDieAttribute_t){
        .name          = 0x49,
        .declared_form = TB_SYMBOL_FORM_REF4,
        .offset        = entries[2].offset + 1,
        .value         = {
            .form    = TB_SYMBOL_FORM_REF4,
            .bits    = entries[3].offset,
            .numeric = true
        }
    };
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_READY);
    TbSymbolParametersView_t view;
    CHECK(tb_symbol_parameters_view(&parameters, &view));
    CHECK(view.parameters[0].direct.fields[TB_SYMBOL_ENTITY_TYPE].selection.matches == 1);
    CHECK(view.parameters[0].direct.fields[TB_SYMBOL_ENTITY_TYPE].attribute.value.bits == entries[3].offset);
    CHECK(view.parameters[1].direct.fields[TB_SYMBOL_ENTITY_TYPE].selection.matches == 0);
    attributes[1] = attributes[0];
    ++attributes[1].offset;
    entries[2].attribute_count = tree.attribute_count = 2;
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_CONFLICT);
    TbSymbolParametersFailure_t failure;
    CHECK(tb_symbol_parameters_failure(&parameters, &failure));
    CHECK(failure.reason == TB_SYMBOL_PARAMETERS_ENTITY && failure.entry_index == 2);
    CHECK(failure.selection_available && failure.field == TB_SYMBOL_ENTITY_TYPE);
    CHECK(failure.selection.matches == 2);
    CHECK(!tb_symbol_parameters_view(&parameters, &view));
    attributes[0].name = 0x34;
    entries[2].attribute_count = tree.attribute_count = 1;
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_MALFORMED);
    CHECK(!tb_symbol_parameters_view(&parameters, &view));
}


static void check_boundaries(void)
{
    fixture();
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_READY);
    unsigned char before[sizeof(parameters)];
    memcpy(before, &parameters, sizeof(before));
    CHECK(tb_symbol_parameters_step(&parameters, 0) == TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(tb_symbol_parameters_step(&parameters, 257) == TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(!memcmp(before, &parameters, sizeof(before)));
    CHECK(tb_symbol_parameters_init(&parameters, tree, 0, arena, 8) == TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(tb_symbol_parameters_init(&parameters, tree, 8, arena, 8) == TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(tb_symbol_parameters_init(&parameters, tree, 1, arena, SIZE_MAX / sizeof(*arena) + 1) ==
          TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(!memcmp(before, &parameters, sizeof(before)));
    entries[4].parent = 4;
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_INVALID);
    fixture();
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_READY);
    uint64_t total = tb_symbol_parameters_progress(&parameters);
    for (uint64_t cancel = 0; cancel <= total; ++cancel) {
        CHECK(tb_symbol_parameters_init(&parameters, tree, 1, arena, 8) == TB_SYMBOL_PARAMETERS_PENDING);
        for (uint64_t step = 0; step < cancel; ++step)
            (void)tb_symbol_parameters_step(&parameters, 1);
        TbSymbolParametersView_t view;
        CHECK(tb_symbol_parameters_view(&parameters, &view) == (cancel == total));
        memset(&parameters, 0xa5, sizeof(parameters));
    }
}


static void check_large_scope(void)
{
    fixture();
    tree.count = 516;
    tree.backing.bytes = 65536;
    for (size_t i = 3; i < 514; ++i) {
        entries[i] = (TbSymbolDieEntry_t){
            .offset     = 12 + 8 * i,
            .end_offset = 16 + 8 * i,
            .parent     = i == 3 ? 1 : i - 1,
            .code       = 1,
            .tag        = i == 513 ? 0x05 : 0x0b,
            .children   = i != 513
        };
    }
    entries[514] = (TbSymbolDieEntry_t){
        .offset     = 12 + 8 * 514,
        .end_offset = 16 + 8 * 514,
        .parent     = 1,
        .code       = 1,
        .tag        = 0x05
    };
    entries[515] = (TbSymbolDieEntry_t){
        .offset     = 12 + 8 * 515,
        .end_offset = 16 + 8 * 515,
        .parent     = 0,
        .code       = 1,
        .tag        = 0x2e
    };
    CHECK(tb_symbol_parameters_init(&parameters, tree, 1, arena, 8) == TB_SYMBOL_PARAMETERS_PENDING);
    TbSymbolParametersStatus_e status = TB_SYMBOL_PARAMETERS_PENDING;
    for (unsigned step = 0; step < 1024 && status == TB_SYMBOL_PARAMETERS_PENDING; ++step)
        status = tb_symbol_parameters_step(&parameters, 1);
    CHECK(status == TB_SYMBOL_PARAMETERS_READY);
    TbSymbolParametersView_t view;
    CHECK(tb_symbol_parameters_view(&parameters, &view) && view.count == 2);
    CHECK(view.parameters[0].record.entry_index == 2 && view.parameters[1].record.entry_index == 514);
    CHECK(tb_symbol_parameters_progress(&parameters) > 512);
}


static void check_diagnostics(void)
{
    fixture();
    TbSymbolParametersFailure_t failure;
    unsigned char               diagnostic[sizeof(failure)];
    memset(&failure, 0xa5, sizeof(failure));
    memcpy(diagnostic, &failure, sizeof(diagnostic));
    CHECK(tb_symbol_parameters_init(&parameters, tree, 1, arena, 8) == TB_SYMBOL_PARAMETERS_PENDING);
    CHECK(!tb_symbol_parameters_failure(&parameters, &failure));
    CHECK(!memcmp(diagnostic, &failure, sizeof(diagnostic)));
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_READY);
    CHECK(!tb_symbol_parameters_failure(&parameters, &failure));
    CHECK(!memcmp(diagnostic, &failure, sizeof(diagnostic)));

    unsigned char before[sizeof(parameters)];
    memcpy(before, &parameters, sizeof(before));
    tree.count = SIZE_MAX / sizeof(*tree.entries) + 1;
    CHECK(tb_symbol_parameters_init(&parameters, tree, 1, arena, 8) == TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(!memcmp(before, &parameters, sizeof(before)));
    fixture();
    tree.attribute_count = SIZE_MAX / sizeof(*tree.attributes) + 1;
    CHECK(tb_symbol_parameters_init(&parameters, tree, 1, arena, 8) == TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(!memcmp(before, &parameters, sizeof(before)));
    fixture();
    CHECK(tb_symbol_parameters_init(&parameters, tree, 1, NULL, 0) == TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(!memcmp(before, &parameters, sizeof(before)));
    CHECK(tb_symbol_parameters_init(&parameters, tree, 7, arena, 0) == TB_SYMBOL_PARAMETERS_PENDING);
    parameters.progress = UINT64_MAX;
    CHECK(tb_symbol_parameters_step(&parameters, 1) == TB_SYMBOL_PARAMETERS_INVALID);
    CHECK(tb_symbol_parameters_failure(&parameters, &failure));
    CHECK(failure.reason == TB_SYMBOL_PARAMETERS_PROGRESS && failure.selected.entry_index == 7);
    CHECK(!failure.record_available && !failure.selection_available);

    fixture();
    entries[5].tag = 0x18;
    CHECK(complete(3) == TB_SYMBOL_PARAMETERS_READY);
    TbSymbolParametersView_t view;
    CHECK(tb_symbol_parameters_view(&parameters, &view) && view.count == 3);
    CHECK(view.parameters[1].unspecified && view.parameters[2].unspecified);
    CHECK(view.parameters[1].record.entry_index != view.parameters[2].record.entry_index);
}


int main(void)
{
    check_order();
    check_direct_facts();
    check_boundaries();
    check_large_scope();
    check_diagnostics();
    puts("PASS ordered physical parameters: interspersed/nested children, direct facts and failures");
    return 0;
}
