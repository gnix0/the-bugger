#include "symbol/code_attributes.h"
#include "../support/check.h"

#include <string.h>

static TbSymbolUnitEntry_t unit;
static TbSymbolUnitsView_t units;
static TbSymbolDieEntry_t entries[2];
static TbSymbolDieAttribute_t attributes[530];
static TbSymbolDiesView_t tree;
static TbSymbolCode_t code;
static TbSymbolAbbrevEntry_t schema_entries[520];
static TbSymbolAbbrevAttribute_t schema_fields[530];
static TbSymbolAbbrevView_t schema;

static void attribute(size_t index, uint64_t name, uint64_t form, uint64_t value, unsigned bytes)
{
    attributes[index] = (TbSymbolDieAttribute_t) {
        .schema_attribute = index,
        .name             = name,
        .declared_form    = form,
        .offset           = 90 + index * 16,
        .value = {
            .form           = form,
            .bytes          = bytes,
            .payload_offset = 90 + index * 16,
            .payload_bytes  = bytes,
            .bits           = value,
            .numeric        = true,
            .is_signed      = form == TB_SYMBOL_FORM_SDATA
        }
    };
}


static void fixture(unsigned version, unsigned offset_bytes, unsigned address_bytes)
{
    unit = (TbSymbolUnitEntry_t) {
        .offset = 60,
        .header = {
            .extent = {
                .end_offset   = 9000,
                .offset_bytes = offset_bytes
            },
            .kind          = TB_SYMBOL_DWARF_COMPILE,
            .version       = (uint16_t)version,
            .address_bytes = (uint8_t)address_bytes,
            .header_bytes  = 24,
            .abbrev_offset = 17
        },
        .header_available = true
    };
    units = (TbSymbolUnitsView_t) {
        .entries       = &unit,
        .count         = 1,
        .section_index = 5,
        .backing = {
            .key = {
                .device = 7,
                .inode  = 19
            },
            .bytes = 12000
        }
    };
    entries[0] = (TbSymbolDieEntry_t) {
        .offset          = 84,
        .end_offset      = 8500,
        .parent          = UINT64_MAX,
        .code            = 3,
        .tag             = 0x11,
        .attribute_count = 520,
        .children        = true
    };
    entries[1] = (TbSymbolDieEntry_t) {
        .offset          = 8500,
        .end_offset      = 8700,
        .parent          = 0,
        .code            = 4,
        .tag             = 0x2e,
        .first_attribute = 520
    };
    tree = (TbSymbolDiesView_t) {
        .entries         = entries,
        .attributes      = attributes,
        .count           = 2,
        .attribute_count = 520,
        .section_index   = 5,
        .unit_offset     = 60,
        .abbrev_section  = 2,
        .abbrev_offset   = 17,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing         = units.backing
    };
    for (size_t i = 0; i < 520; ++i)
        attribute(i, 3, TB_SYMBOL_FORM_DATA1, 1, 1);
    attribute(1, 0x11, TB_SYMBOL_FORM_ADDR, 100, address_bytes);
    attribute(519, 0x12, TB_SYMBOL_FORM_DATA1, 20, 1);
}


static TbSymbolCodeStatus_e run(size_t steps)
{
    TbSymbolCodeStatus_e status = TB_SYMBOL_CODE_PENDING;
    for (unsigned turn = 0; turn < 10000 && status == TB_SYMBOL_CODE_PENDING; ++turn) {
        TbSymbolCodeView_t unavailable;
        CHECK(!tb_symbol_code_view(&code, &unavailable));
        status = tb_symbol_code_step(&code, steps);
    }
    return status;
}


static void start(void)
{
    CHECK(tb_symbol_code_init(&code, units, tree, (TbSymbolCodeSelection_t) {0}) == TB_SYMBOL_CODE_PENDING);
}


static TbSymbolCodeView_t completed(void)
{
    TbSymbolCodeView_t view;
    CHECK(tb_symbol_code_view(&code, &view));
    CHECK(view.fields[TB_SYMBOL_CODE_LOW].selection.record_offset == 84);
    CHECK(view.fields[TB_SYMBOL_CODE_LOW].selection.backing.key.inode == 19);
    CHECK(view.unit.address_bytes == unit.header.address_bytes);
    return view;
}


static void check_direct(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        for (unsigned offset_bytes = 4; offset_bytes <= 8; offset_bytes += 4) {
            for (unsigned address_bytes = 1; address_bytes <= 8; address_bytes *= 2) {
                for (size_t steps = 1; steps <= 256; steps *= 2) {
                    fixture(version, offset_bytes, address_bytes);
                    start();
                    CHECK(run(steps) == TB_SYMBOL_CODE_COMPLETE);
                    TbSymbolCodeView_t view = completed();
                    CHECK(view.interval_available && view.begin == 100 && view.end == 120);
                    CHECK(view.fields[TB_SYMBOL_CODE_HIGH].kind == TB_SYMBOL_CODE_LENGTH);
                    CHECK(view.fields[TB_SYMBOL_CODE_HIGH].selection.first_index == 519);
                    CHECK(view.fields[TB_SYMBOL_CODE_RANGES].kind == TB_SYMBOL_CODE_MISSING);
                    CHECK(view.fields[TB_SYMBOL_CODE_RANGES].selection.matches == 0);

                    attribute(519, 0x12, TB_SYMBOL_FORM_ADDR, 130, address_bytes);
                    attributes[519].declared_form = TB_SYMBOL_FORM_INDIRECT;
                    attributes[519].value.indirect = true;
                    attributes[519].value.bytes++;
                    attributes[519].value.payload_offset++;
                    start();
                    CHECK(run(steps) == TB_SYMBOL_CODE_COMPLETE);
                    view = completed();
                    CHECK(view.interval_available && view.end == 130);
                    CHECK(view.fields[TB_SYMBOL_CODE_HIGH].kind == TB_SYMBOL_CODE_ADDRESS);
                }
            }
        }
    }

    fixture(4, 4, 4);
    attributes[519].name = 3;
    start();
    CHECK(run(31) == TB_SYMBOL_CODE_COMPLETE && !completed().interval_available);
    attributes[1].name = 3;
    start();
    CHECK(run(31) == TB_SYMBOL_CODE_COMPLETE);
    CHECK(completed().fields[TB_SYMBOL_CODE_LOW].kind == TB_SYMBOL_CODE_MISSING);

    fixture(4, 4, 4);
    unit.header.kind = TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
    tree.kind = TB_SYMBOL_DWARF_PARTIAL;
    entries[0].tag = 0x3c;
    start();
    CHECK(run(256) == TB_SYMBOL_CODE_COMPLETE);
}


static void check_dependencies(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_ADDRX, TB_SYMBOL_FORM_ADDRX1, TB_SYMBOL_FORM_ADDRX2,
                             TB_SYMBOL_FORM_ADDRX3, TB_SYMBOL_FORM_ADDRX4};
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
        fixture(5, 8, 4);
        unsigned width = i ? (unsigned)i : 1;
        attribute(1, 0x11, forms[i], 2, width);
        attribute(519, 0x12, forms[i], 3, width);
        attribute(10, 0x73, TB_SYMBOL_FORM_SEC_OFFSET, 32, 8);
        attribute(11, 0x74, TB_SYMBOL_FORM_SEC_OFFSET, 64, 8);
        start();
        CHECK(run(31) == TB_SYMBOL_CODE_COMPLETE);
        TbSymbolCodeView_t view = completed();
        CHECK(!view.interval_available && view.fields[TB_SYMBOL_CODE_LOW].value == 2);
        CHECK(view.fields[TB_SYMBOL_CODE_LOW].kind == TB_SYMBOL_CODE_ADDRESS_INDEX);
        CHECK(view.fields[TB_SYMBOL_CODE_HIGH].kind == TB_SYMBOL_CODE_ADDRESS_INDEX);
        CHECK(view.fields[TB_SYMBOL_CODE_ADDR_BASE].kind == TB_SYMBOL_CODE_TABLE_BASE);
        CHECK(view.fields[TB_SYMBOL_CODE_RNGLISTS_BASE].value == 64);
    }

    for (unsigned version = 4; version <= 5; ++version) {
        fixture(version, 4, 4);
        attribute(519, 0x55, TB_SYMBOL_FORM_SEC_OFFSET, 80, 4);
        start();
        CHECK(run(256) == TB_SYMBOL_CODE_COMPLETE);
        TbSymbolCodeView_t view = completed();
        CHECK(!view.interval_available && view.fields[TB_SYMBOL_CODE_LOW].value == 100);
        CHECK(view.fields[TB_SYMBOL_CODE_RANGES].kind == TB_SYMBOL_CODE_LIST_OFFSET);
        CHECK(view.fields[TB_SYMBOL_CODE_RANGES].value == 80);
    }
    fixture(5, 4, 4);
    attribute(519, 0x55, TB_SYMBOL_FORM_RNGLISTX, 0, 1);
    start();
    CHECK(run(256) == TB_SYMBOL_CODE_COMPLETE);
    CHECK(completed().fields[TB_SYMBOL_CODE_RANGES].kind == TB_SYMBOL_CODE_LIST_INDEX);
    CHECK(completed().fields[TB_SYMBOL_CODE_RANGES].value == 0);
}


static void check_scalar_forms(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_DATA1, TB_SYMBOL_FORM_DATA2, TB_SYMBOL_FORM_DATA4,
                             TB_SYMBOL_FORM_DATA8, TB_SYMBOL_FORM_UDATA, TB_SYMBOL_FORM_SDATA};
    const unsigned widths[] = {1, 2, 4, 8, 1, 1};
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
        fixture(5, 8, 8);
        attribute(519, 0x12, forms[i], 20, widths[i]);
        start();
        CHECK(run(256) == TB_SYMBOL_CODE_COMPLETE && completed().end == 120);
        CHECK(completed().fields[TB_SYMBOL_CODE_HIGH].attribute.value.form == forms[i]);
    }

    fixture(5, 4, 4);
    attribute(519, 0x12, TB_SYMBOL_FORM_IMPLICIT_CONST, 20, 1);
    attributes[519].declared_form = TB_SYMBOL_FORM_INDIRECT;
    attributes[519].value.indirect = true;
    attributes[519].value.is_signed = true;
    attributes[519].value.bytes++;
    attributes[519].value.payload_offset++;
    start();
    CHECK(run(256) == TB_SYMBOL_CODE_COMPLETE && completed().end == 120);
    CHECK(!completed().fields[TB_SYMBOL_CODE_HIGH].from_abbreviation);

    fixture(5, 4, 4);
    attribute(519, 0x12, TB_SYMBOL_FORM_DATA16, 0, 16);
    attributes[519].value.numeric = false;
    start();
    CHECK(run(256) == TB_SYMBOL_CODE_UNSUPPORTED);
}


static void implicit_fixture(void)
{
    fixture(5, 4, 4);
    attribute(519, 0x12, TB_SYMBOL_FORM_IMPLICIT_CONST, 0, 0);
    attributes[519].value.numeric = false;
    for (size_t i = 0; i < 520; ++i) {
        schema_entries[i] = (TbSymbolAbbrevEntry_t) {
            .code = 100 + i
        };
    }
    schema_entries[519] = (TbSymbolAbbrevEntry_t) {
        .code            = 3,
        .tag             = 0x11,
        .attribute_count = 520,
        .children        = true
    };
    schema_fields[519] = (TbSymbolAbbrevAttribute_t) {
        .name            = 0x12,
        .form            = TB_SYMBOL_FORM_IMPLICIT_CONST,
        .offset          = 20,
        .implicit_offset = 22,
        .implicit_bytes  = 1,
        .implicit_value  = 20,
        .implicit_fits   = true
    };
    schema = (TbSymbolAbbrevView_t) {
        .entries         = schema_entries,
        .attributes      = schema_fields,
        .count           = 520,
        .attribute_count = 520,
        .section_index   = 2,
        .table_offset    = 17,
        .end_offset      = 80,
        .version         = 5,
        .backing         = units.backing
    };
    start();
    CHECK(run(31) == TB_SYMBOL_CODE_SCHEMA);
}


static void check_implicit(void)
{
    implicit_fixture();
    TbSymbolAbbrevView_t wrong = schema;
    wrong.backing.key.inode++;
    CHECK(tb_symbol_code_schema(&code, &wrong) == TB_SYMBOL_CODE_INVALID);
    CHECK(code.status == TB_SYMBOL_CODE_SCHEMA);
    CHECK(tb_symbol_code_schema(&code, &schema) == TB_SYMBOL_CODE_PENDING);
    CHECK(tb_symbol_code_step(&code, 256) == TB_SYMBOL_CODE_PENDING);
    CHECK(code.schema_cursor == 256);
    CHECK(run(31) == TB_SYMBOL_CODE_COMPLETE);
    TbSymbolCodeView_t view = completed();
    CHECK(view.interval_available && view.end == 120);
    CHECK(view.fields[TB_SYMBOL_CODE_HIGH].from_abbreviation);
    CHECK(view.fields[TB_SYMBOL_CODE_HIGH].schema_entry == 519);
    CHECK(view.fields[TB_SYMBOL_CODE_HIGH].attribute.value.bytes == 0);

    implicit_fixture();
    CHECK(tb_symbol_code_schema(&code, NULL) == TB_SYMBOL_CODE_DEPENDENCY);
    CHECK(!tb_symbol_code_view(&code, &view));
    for (unsigned fault = 0; fault < 4; ++fault) {
        implicit_fixture();
        switch (fault) {
        case 0: schema_fields[519].implicit_value = -1; break;
        case 1: schema_fields[519].implicit_fits = false; break;
        case 2: schema_entries[519].first_attribute++; break;
        case 3: schema_entries[519].code++; break;
        }
        CHECK(tb_symbol_code_schema(&code, &schema) == TB_SYMBOL_CODE_PENDING);
        CHECK(run(256) == (fault == 1 ? TB_SYMBOL_CODE_UNSUPPORTED : TB_SYMBOL_CODE_MALFORMED));
        CHECK(!tb_symbol_code_view(&code, &view));
    }
}


static void check_failures(void)
{
    fixture(5, 4, 4);
    attribute(500, 0x11, TB_SYMBOL_FORM_ADDR, 100, 4);
    start();
    CHECK(tb_symbol_code_step(&code, 256) == TB_SYMBOL_CODE_PENDING);
    CHECK(run(31) == TB_SYMBOL_CODE_CONFLICT);
    TbSymbolAttributeResult_t selected;
    CHECK(tb_symbol_code_selection(&code, TB_SYMBOL_CODE_LOW, &selected));
    CHECK(selected.matches == 2 && selected.first_index == 1 && selected.second_index == 500);

    for (unsigned fault = 0; fault < 11; ++fault) {
        fixture(5, 4, 1);
        switch (fault) {
        case 0: attributes[1].name = 3; break;
        case 1: attribute(0, 0x55, TB_SYMBOL_FORM_SEC_OFFSET, 80, 4); break;
        case 2: attributes[519].value.bits = 200; break;
        case 3: attribute(519, 0x12, TB_SYMBOL_FORM_ADDR, 90, 1); break;
        case 4: attribute(519, 0x12, TB_SYMBOL_FORM_SDATA, UINT64_MAX, 1); break;
        case 5: attributes[1].offset = UINT64_MAX; break;
        case 6: attributes[1].value.payload_offset++; break;
        case 7: attribute(1, 0x11, TB_SYMBOL_FORM_DATA1, 100, 1); break;
        case 8: attribute(0, 0x73, TB_SYMBOL_FORM_DATA4, 32, 4); break;
        case 9: attribute(1, 0x11, TB_SYMBOL_FORM_ADDR, 256, 1); break;
        case 10: attributes[519].declared_form = TB_SYMBOL_FORM_DATA8; break;
        }
        start();
        CHECK(run(256) == TB_SYMBOL_CODE_MALFORMED);
        TbSymbolCodeView_t view;
        CHECK(!tb_symbol_code_view(&code, &view));
    }

    fixture(5, 8, 8);
    attribute(1, 0x11, TB_SYMBOL_FORM_ADDR, UINT64_MAX - 1, 8);
    start();
    CHECK(run(256) == TB_SYMBOL_CODE_MALFORMED);
    attribute(519, 0x12, TB_SYMBOL_FORM_DATA1, 1, 1);
    start();
    CHECK(run(256) == TB_SYMBOL_CODE_COMPLETE && completed().end == UINT64_MAX);
    attribute(519, 0x12, TB_SYMBOL_FORM_DATA1, 0, 1);
    start();
    CHECK(run(256) == TB_SYMBOL_CODE_COMPLETE && completed().begin == completed().end);

    fixture(5, 4, 4);
    start();
    TbSymbolCode_t saved = code;
    units.backing.key.inode++;
    CHECK(tb_symbol_code_init(&code, units, tree, (TbSymbolCodeSelection_t) {0}) == TB_SYMBOL_CODE_INVALID);
    CHECK(!memcmp(&saved, &code, sizeof(code)));
    CHECK(tb_symbol_code_step(&code, 0) == TB_SYMBOL_CODE_INVALID);
    CHECK(!memcmp(&saved, &code, sizeof(code)));
    tb_symbol_code_discard(&code);
    TbSymbolCodeView_t view;
    CHECK(!tb_symbol_code_view(&code, &view));
}


static void check_contexts(void)
{
    for (unsigned fault = 0; fault < 9; ++fault) {
        fixture(5, 4, 4);
        start();
        TbSymbolCode_t saved = code;
        switch (fault) {
        case 0: units.section_index++; break;
        case 1: tree.unit_offset++; break;
        case 2: tree.abbrev_offset++; break;
        case 3: entries[0].parent = 0; break;
        case 4: entries[0].tag = 0x2e; break;
        case 5: entries[0].offset++; break;
        case 6: entries[0].end_offset = 9001; break;
        case 7: unit.header_available = false; break;
        case 8: entries[0].attribute_count = 521; break;
        }
        CHECK(tb_symbol_code_init(&code, units, tree, (TbSymbolCodeSelection_t) {0}) ==
              TB_SYMBOL_CODE_INVALID);
        CHECK(!memcmp(&saved, &code, sizeof(code)));
    }

    fixture(5, 4, 4);
    attribute(520, 0x11, TB_SYMBOL_FORM_ADDR, 140, 4);
    attribute(521, 0x12, TB_SYMBOL_FORM_DATA1, 20, 1);
    attributes[520].offset = attributes[520].value.payload_offset = 8510;
    attributes[521].offset = attributes[521].value.payload_offset = 8520;
    entries[1].attribute_count = 2;
    tree.attribute_count = 522;
    CHECK(tb_symbol_code_init(&code, units, tree, (TbSymbolCodeSelection_t) {
        .die_index = 1
    }) == TB_SYMBOL_CODE_PENDING);
    CHECK(run(1) == TB_SYMBOL_CODE_COMPLETE);
    TbSymbolCodeView_t view;
    CHECK(tb_symbol_code_view(&code, &view));
    CHECK(view.die_index == 1 && view.begin == 140 && view.end == 160);

    attributes[521].name = 0x73;
    CHECK(tb_symbol_code_init(&code, units, tree, (TbSymbolCodeSelection_t) {
        .die_index = 1
    }) == TB_SYMBOL_CODE_PENDING);
    CHECK(run(1) == TB_SYMBOL_CODE_MALFORMED);

    fixture(5, 4, 4);
    start();
    CHECK(run(256) == TB_SYMBOL_CODE_COMPLETE);
    TbSymbolCodeView_t before;
    memset(&before, 0xa5, sizeof(before));
    view = before;
    tb_symbol_code_discard(&code);
    CHECK(!tb_symbol_code_view(&code, &view));
    CHECK(!memcmp(&before, &view, sizeof(view)));
}


int main(void)
{
    check_direct();
    check_dependencies();
    check_scalar_forms();
    check_implicit();
    check_failures();
    check_contexts();
    puts("PASS direct code attributes, exact CU context, indexed dependencies, high-PC lengths "
         "and implicit abbreviation provenance");
    return 0;
}
