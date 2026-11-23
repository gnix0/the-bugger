#include "symbol/line_reference.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static TbSymbolUnitEntry_t unit;
static TbSymbolDieEntry_t root;
static TbSymbolDieAttribute_t attributes[520];
static TbSymbolUnitsView_t units;
static TbSymbolDiesView_t tree;

static void fixture(unsigned version, unsigned width)
{
    unit = (TbSymbolUnitEntry_t) {
        .offset = 60,
        .header = {
            .extent = {
                .end_offset   = 10000,
                .offset_bytes = width
            },
            .kind          = TB_SYMBOL_DWARF_COMPILE,
            .version       = (uint16_t)version,
            .address_bytes = 4,
            .header_bytes  = 24,
            .abbrev_offset = 17
        },
        .header_available = true
    };
    root = (TbSymbolDieEntry_t) {
        .offset          = 84,
        .end_offset      = 9000,
        .parent          = UINT64_MAX,
        .tag             = 0x11,
        .attribute_count = 520
    };
    units = (TbSymbolUnitsView_t) {
        .entries       = &unit,
        .count         = 1,
        .section_index = 5,
        .backing        = {
            .key   = { .device = 3, .inode = 19 },
            .bytes = 20000
        }
    };
    tree = (TbSymbolDiesView_t) {
        .entries         = &root,
        .attributes      = attributes,
        .count           = 1,
        .attribute_count = 520,
        .section_index   = 5,
        .unit_offset     = 60,
        .abbrev_section  = 2,
        .abbrev_offset   = 17,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing         = units.backing
    };
    for (size_t i = 0; i < 520; ++i) {
        attributes[i] = (TbSymbolDieAttribute_t) {
            .schema_attribute = i,
            .name             = 3,
            .declared_form    = TB_SYMBOL_FORM_SEC_OFFSET,
            .offset           = 85 + i * 16,
            .value = {
                .form           = TB_SYMBOL_FORM_SEC_OFFSET,
                .bytes          = width,
                .payload_offset = 85 + i * 16,
                .payload_bytes  = width,
                .bits           = 73,
                .numeric        = true
            }
        };
    }
    attributes[519].name = 0x10;
}


static TbSymbolLineReferenceStatus_e run(TbSymbolLineReference_t *reference, size_t steps)
{
    TbSymbolLineReferenceStatus_e status = TB_SYMBOL_LINE_REFERENCE_PENDING;
    for (unsigned turn = 0; turn < 1000 && status == TB_SYMBOL_LINE_REFERENCE_PENDING; ++turn) {
        TbSymbolLineReferenceView_t unavailable;
        CHECK(!tb_symbol_line_reference_view(reference, &unavailable));
        status = tb_symbol_line_reference_step(reference, steps);
    }
    return status;
}


static void check_references(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        for (unsigned width = 4; width <= 8; width += 4) {
            for (size_t steps = 1; steps <= 256; steps *= 2) {
                fixture(version, width);
                TbSymbolLineReference_t reference;
                CHECK(tb_symbol_line_reference_init(&reference, units, tree, 0) ==
                      TB_SYMBOL_LINE_REFERENCE_PENDING);
                CHECK(run(&reference, steps) == TB_SYMBOL_LINE_REFERENCE_FOUND);
                TbSymbolLineReferenceView_t view;
                CHECK(tb_symbol_line_reference_view(&reference, &view));
                CHECK(view.line_offset == 73 && view.source.first_index == 519);
                CHECK(view.source.section_index == 5 && view.source.unit_offset == 60);
                CHECK(view.source.record_offset == 84 && view.source.backing.key.inode == 19);
                CHECK(view.attribute.offset == attributes[519].offset && view.abbrev_section == 2);
                CHECK(view.unit.address_bytes == 4 && view.unit.extent.offset_bytes == width);
                tb_symbol_line_reference_discard(&reference);
                CHECK(!tb_symbol_line_reference_view(&reference, &view));
            }
        }
    }

    fixture(4, 4);
    unit.header.kind = TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
    tree.kind = TB_SYMBOL_DWARF_PARTIAL;
    root.tag = 0x3c;
    TbSymbolLineReference_t reference;
    CHECK(tb_symbol_line_reference_init(&reference, units, tree, 0) == TB_SYMBOL_LINE_REFERENCE_PENDING);
    CHECK(run(&reference, 256) == TB_SYMBOL_LINE_REFERENCE_FOUND);

    fixture(5, 8);
    unit.header.kind = tree.kind = TB_SYMBOL_DWARF_TYPE;
    root.tag = 0x41;
    attributes[519].declared_form = TB_SYMBOL_FORM_INDIRECT;
    attributes[519].value.indirect = true;
    attributes[519].value.bytes++;
    attributes[519].value.payload_offset++;
    CHECK(tb_symbol_line_reference_init(&reference, units, tree, 0) == TB_SYMBOL_LINE_REFERENCE_PENDING);
    CHECK(run(&reference, 256) == TB_SYMBOL_LINE_REFERENCE_FOUND);
}


static void check_failures(void)
{
    TbSymbolLineReference_t reference;
    fixture(5, 4);
    attributes[519].name = 3;
    CHECK(tb_symbol_line_reference_init(&reference, units, tree, 0) == TB_SYMBOL_LINE_REFERENCE_PENDING);
    CHECK(run(&reference, 256) == TB_SYMBOL_LINE_REFERENCE_ABSENT);
    TbSymbolAttributeResult_t scope;
    CHECK(tb_symbol_line_reference_scope(&reference, &scope) && scope.matches == 0);

    attributes[0].name = attributes[519].name = 0x10;
    CHECK(tb_symbol_line_reference_init(&reference, units, tree, 0) == TB_SYMBOL_LINE_REFERENCE_PENDING);
    CHECK(tb_symbol_line_reference_step(&reference, 256) == TB_SYMBOL_LINE_REFERENCE_PENDING);
    CHECK(run(&reference, 256) == TB_SYMBOL_LINE_REFERENCE_CONFLICT);
    CHECK(tb_symbol_line_reference_scope(&reference, &scope) && scope.matches == 2);
    CHECK(scope.first_index == 0 && scope.second_index == 519);

    for (unsigned fault = 0; fault < 7; ++fault) {
        fixture(5, 4);
        switch (fault) {
        case 0: attributes[519].value.form = TB_SYMBOL_FORM_DATA4; break;
        case 1: attributes[519].value.numeric = false; break;
        case 2: attributes[519].value.is_signed = true; break;
        case 3: attributes[519].value.payload_bytes = 8; break;
        case 4: attributes[519].offset = UINT64_MAX; break;
        case 5: attributes[519].value.payload_offset++; break;
        case 6: attributes[519].value.bytes = 1000; break;
        }
        CHECK(tb_symbol_line_reference_init(&reference, units, tree, 0) == TB_SYMBOL_LINE_REFERENCE_PENDING);
        CHECK(run(&reference, 256) == TB_SYMBOL_LINE_REFERENCE_MALFORMED);
        TbSymbolLineReferenceView_t view;
        CHECK(!tb_symbol_line_reference_view(&reference, &view));
    }

    for (unsigned fault = 0; fault < 8; ++fault) {
        fixture(5, 4);
        CHECK(tb_symbol_line_reference_init(&reference, units, tree, 0) == TB_SYMBOL_LINE_REFERENCE_PENDING);
        TbSymbolLineReference_t saved = reference;
        switch (fault) {
        case 0: tree.backing.key.inode++; break;
        case 1: tree.unit_offset++; break;
        case 2: root.parent = 0; break;
        case 3: root.tag = 0x2e; break;
        case 4: unit.header.address_bytes = 0; break;
        case 5: tree.abbrev_offset++; break;
        case 6: root.offset++; break;
        case 7: root.attribute_count = 521; break;
        }
        CHECK(tb_symbol_line_reference_init(&reference, units, tree, 0) == TB_SYMBOL_LINE_REFERENCE_INVALID);
        CHECK(!memcmp(&reference, &saved, sizeof(reference)));
    }
}


int main(void)
{
    check_references();
    check_failures();
    puts("PASS bounded explicit CU line references, physical provenance, absence and conflicts");
    return 0;
}
