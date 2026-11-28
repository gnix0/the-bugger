#include "symbol/inline_scope.h"
#include "../support/check.h"

#include <string.h>

static TbSymbolInlineScope_t scope;
static TbSymbolInlineScopeFact_t facts[600];
static TbSymbolDieEntry_t entries[600];
static TbSymbolDieAttribute_t attributes[600];
static TbSymbolUnitEntry_t unit;
static TbSymbolUnitsView_t units;
static TbSymbolDiesView_t tree;
static unsigned char codes[600];

static void fixture(size_t count)
{
    CHECK(count && count <= 600);
    memset(entries, 0, sizeof(entries));
    memset(attributes, 0, sizeof(attributes));
    memset(codes, 0xff, sizeof(codes));
    tree = (TbSymbolDiesView_t){
        .entries         = entries,
        .attributes      = attributes,
        .count           = count,
        .section_index   = 2,
        .abbrev_section  = 3,
        .abbrev_offset   = 17,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing         = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 65536
        }
    };
    unit = (TbSymbolUnitEntry_t){
        .header           = {
            .kind          = TB_SYMBOL_DWARF_COMPILE,
            .version       = 5,
            .header_bytes  = 12,
            .abbrev_offset = 17
        },
        .header_available = true
    };
    units = (TbSymbolUnitsView_t){
        .entries       = &unit,
        .count         = 1,
        .section_index = 2,
        .backing       = tree.backing
    };
    for (size_t i = 0; i < count; ++i) {
        entries[i] = (TbSymbolDieEntry_t){
            .offset     = 12 + 32 * i,
            .end_offset = 28 + 32 * i,
            .parent     = i ? i - 1 : UINT64_MAX,
            .code       = i + 1,
            .tag        = i ? 0xb : 0x11,
            .children   = i + 1 < count
        };
    }
}


static void rebuild(void)
{
    size_t count = 0;
    for (size_t i = 0; i < tree.count; ++i) {
        entries[i].first_attribute = count;
        entries[i].attribute_count = codes[i] == 0xff ? 0 : 1;
        if (codes[i] == 0xff)
            continue;
        attributes[count++] = (TbSymbolDieAttribute_t){
            .name          = TB_SYMBOL_CONSTANT_INLINE,
            .declared_form = TB_SYMBOL_FORM_DATA1,
            .offset        = entries[i].offset + 1,
            .value         = {
                .form    = TB_SYMBOL_FORM_DATA1,
                .bits    = codes[i],
                .numeric = true
            }
        };
    }
    tree.attribute_count = count;
}


static TbSymbolInlineScopeStatus_e complete(uint64_t selected, size_t capacity)
{
    CHECK(tb_symbol_inline_scope_init(&scope, units, tree, 0, selected, facts, capacity) ==
          TB_SYMBOL_INLINE_SCOPE_PENDING);
    for (unsigned step = 0; step < 100000; ++step) {
        TbSymbolInlineScopeView_t view;
        CHECK(!tb_symbol_inline_scope_view(&scope, &view));
        TbSymbolInlineScopeStatus_e status = tb_symbol_inline_scope_step(&scope, 1);
        if (status != TB_SYMBOL_INLINE_SCOPE_PENDING)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_INLINE_SCOPE_INVALID;
}


static void check_roots(void)
{
    for (unsigned code = 0; code <= 4; ++code) {
        fixture(3);
        entries[1].tag = 0x2e;
        entries[2].tag = 0x34;
        codes[1] = (unsigned char)code;
        rebuild();
        CHECK(complete(2, 600) == (code <= 3 ? TB_SYMBOL_INLINE_SCOPE_READY
                                            : TB_SYMBOL_INLINE_SCOPE_UNSUPPORTED));
        TbSymbolInlineScopeView_t view;
        if (code <= 3) {
            CHECK(tb_symbol_inline_scope_view(&scope, &view));
            CHECK(view.selected.entry_index == 2 && view.count == 1);
            CHECK(view.abstract_available == (code != 0));
            CHECK(!view.selected_abstract && !view.concrete_available);
            CHECK(view.facts[0].status == TB_SYMBOL_CONSTANT_FOUND);
            CHECK(view.facts[0].constant.value == code);
            if (code)
                CHECK(view.abstract_root.entry_index == 1);
            CHECK(complete(1, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
            CHECK(tb_symbol_inline_scope_view(&scope, &view));
            CHECK(view.selected_abstract == (code != 0));
        } else {
            CHECK(!tb_symbol_inline_scope_view(&scope, &view));
        }
    }
    fixture(5);
    entries[1].tag = 0x2e;
    entries[2].tag = 0x2e;
    entries[3].tag = 0x1d;
    entries[4].tag = 0x34;
    codes[1] = 1;
    codes[2] = 2;
    rebuild();
    CHECK(complete(4, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    TbSymbolInlineScopeView_t view;
    CHECK(tb_symbol_inline_scope_view(&scope, &view));
    CHECK(view.abstract_root.entry_index == 2 && view.concrete_root.entry_index == 3);
    CHECK(view.count == 2 && !view.selected_concrete && !view.selected_abstract);
    CHECK(complete(3, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    CHECK(tb_symbol_inline_scope_view(&scope, &view) && view.selected_concrete);
}


static void check_absence_and_depth(void)
{
    fixture(512);
    for (size_t i = 1; i < 511; ++i)
        entries[i].tag = 0x2e;
    entries[511].tag = 0x34;
    codes[1] = 3;
    rebuild();
    CHECK(complete(511, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    TbSymbolInlineScopeView_t view;
    CHECK(tb_symbol_inline_scope_view(&scope, &view));
    CHECK(view.count == 510 && view.abstract_root.entry_index == 1);
    CHECK(view.facts[0].status == TB_SYMBOL_CONSTANT_ABSENT);
    CHECK(view.facts[509].constant.value == 3);
    CHECK(complete(511, 509) == TB_SYMBOL_INLINE_SCOPE_RESOURCE);
    CHECK(!tb_symbol_inline_scope_view(&scope, &view));
    unit.header.version = 4;
    unit.header.kind = TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
    CHECK(complete(511, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    tree.kind = TB_SYMBOL_DWARF_PARTIAL;
    entries[0].tag = 0x3c;
    CHECK(complete(511, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
}


static void check_failures(void)
{
    TbSymbolInlineScopeView_t view;
    fixture(3);
    entries[1].tag = 0x2e;
    codes[1] = 0;
    rebuild();
    CHECK(complete(2, 0) == TB_SYMBOL_INLINE_SCOPE_RESOURCE);
    CHECK(!tb_symbol_inline_scope_view(&scope, &view));
    attributes[0].value.form = TB_SYMBOL_FORM_FLAG;
    CHECK(complete(2, 600) == TB_SYMBOL_INLINE_SCOPE_MALFORMED);
    CHECK(!tb_symbol_inline_scope_view(&scope, &view));
    rebuild();
    attributes[1] = attributes[0];
    attributes[1].offset++;
    entries[1].attribute_count = 2;
    entries[2].first_attribute = 2;
    tree.attribute_count = 2;
    CHECK(complete(2, 600) == TB_SYMBOL_INLINE_SCOPE_CONFLICT);
    CHECK(!tb_symbol_inline_scope_view(&scope, &view));
    rebuild();
    entries[2].parent = 2;
    CHECK(complete(2, 600) == TB_SYMBOL_INLINE_SCOPE_INVALID);
    CHECK(!tb_symbol_inline_scope_view(&scope, &view));

    fixture(1);
    CHECK(complete(0, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    unsigned char before[sizeof(scope)];
    memcpy(before, &scope, sizeof(before));
    CHECK(tb_symbol_inline_scope_init(&scope, units, tree, 0, 1, facts, 600) ==
          TB_SYMBOL_INLINE_SCOPE_INVALID);
    CHECK(!memcmp(&scope, before, sizeof(scope)));
    CHECK(tb_symbol_inline_scope_step(&scope, 0) == TB_SYMBOL_INLINE_SCOPE_INVALID);
    CHECK(!memcmp(&scope, before, sizeof(scope)));
    TbSymbolDiesView_t invalid_tree = tree;
    invalid_tree.count = SIZE_MAX / sizeof(*entries) + 1;
    CHECK(tb_symbol_inline_scope_init(&scope, units, invalid_tree, 0, 0, facts, 600) ==
          TB_SYMBOL_INLINE_SCOPE_INVALID);
    CHECK(!memcmp(&scope, before, sizeof(scope)));
    invalid_tree = tree;
    invalid_tree.attribute_count = SIZE_MAX / sizeof(*attributes) + 1;
    CHECK(tb_symbol_inline_scope_init(&scope, units, invalid_tree, 0, 0, facts, 600) ==
          TB_SYMBOL_INLINE_SCOPE_INVALID);
    CHECK(!memcmp(&scope, before, sizeof(scope)));
    unit.header.version = 3;
    CHECK(tb_symbol_inline_scope_init(&scope, units, tree, 0, 0, facts, 600) ==
          TB_SYMBOL_INLINE_SCOPE_INVALID);
    CHECK(!memcmp(&scope, before, sizeof(scope)));
}


static void check_schema(void)
{
    fixture(2);
    entries[1].tag = 0x2e;
    codes[1] = 2;
    rebuild();
    attributes[0].declared_form = TB_SYMBOL_FORM_IMPLICIT_CONST;
    attributes[0].value = (TbSymbolFormView_t){
        .form = TB_SYMBOL_FORM_IMPLICIT_CONST
    };
    TbSymbolAbbrevEntry_t entry = {
        .code            = entries[1].code,
        .tag             = entries[1].tag,
        .offset          = 17,
        .end_offset      = 40,
        .attribute_count = 1
    };
    TbSymbolAbbrevAttribute_t attribute = {
        .name            = TB_SYMBOL_CONSTANT_INLINE,
        .form            = TB_SYMBOL_FORM_IMPLICIT_CONST,
        .offset          = 18,
        .implicit_offset = 20,
        .implicit_bytes  = 1,
        .implicit_value  = 2,
        .implicit_fits   = true
    };
    TbSymbolAbbrevView_t schema = {
        .entries         = &entry,
        .attributes      = &attribute,
        .count           = 1,
        .attribute_count = 1,
        .section_index   = 3,
        .table_offset    = 17,
        .end_offset      = 128,
        .version         = 5,
        .backing         = tree.backing
    };
    CHECK(complete(1, 600) == TB_SYMBOL_INLINE_SCOPE_SCHEMA);
    TbSymbolAbbrevView_t wrong = schema;
    wrong.table_offset++;
    CHECK(tb_symbol_inline_scope_schema(&scope, &wrong) == TB_SYMBOL_INLINE_SCOPE_INVALID);
    CHECK(scope.status == TB_SYMBOL_INLINE_SCOPE_SCHEMA);
    CHECK(tb_symbol_inline_scope_schema(&scope, &schema) == TB_SYMBOL_INLINE_SCOPE_PENDING);
    CHECK(tb_symbol_inline_scope_step(&scope, 1) == TB_SYMBOL_INLINE_SCOPE_PENDING);
    CHECK(tb_symbol_inline_scope_step(&scope, 1) == TB_SYMBOL_INLINE_SCOPE_READY);
    TbSymbolInlineScopeView_t view;
    CHECK(tb_symbol_inline_scope_view(&scope, &view));
    CHECK(view.selected_abstract && view.facts[0].constant.from_abbreviation);
    CHECK(complete(1, 600) == TB_SYMBOL_INLINE_SCOPE_SCHEMA);
    CHECK(tb_symbol_inline_scope_schema(&scope, NULL) == TB_SYMBOL_INLINE_SCOPE_DEPENDENCY);
    CHECK(!tb_symbol_inline_scope_view(&scope, &view));
}


static void check_physical_only(void)
{
    fixture(4);
    entries[1].tag = 0x2e;
    entries[2].tag = 0x2e;
    entries[3].tag = 0x34;
    codes[1] = 1;
    codes[2] = 0;
    rebuild();
    CHECK(complete(3, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    TbSymbolInlineScopeView_t view;
    CHECK(tb_symbol_inline_scope_view(&scope, &view));
    CHECK(view.abstract_available && view.abstract_root.entry_index == 1);
    CHECK(view.count == 2 && view.facts[0].constant.value == 0);

    fixture(3);
    entries[1].tag = 0x2e;
    entries[2].tag = 0x2e;
    entries[2].parent = 0;
    codes[1] = 3;
    rebuild();
    entries[2].attribute_count = 1;
    attributes[1] = (TbSymbolDieAttribute_t){
        .name          = 0x31,
        .declared_form = TB_SYMBOL_FORM_REF4,
        .offset        = entries[2].offset + 1,
        .value         = {
            .form    = TB_SYMBOL_FORM_REF4,
            .bits    = entries[1].offset,
            .numeric = true
        }
    };
    tree.attribute_count = 2;
    CHECK(complete(2, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    CHECK(tb_symbol_inline_scope_view(&scope, &view));
    CHECK(!view.abstract_available && !view.selected_abstract && view.count == 1);
    CHECK(view.facts[0].status == TB_SYMBOL_CONSTANT_ABSENT);

    fixture(6);
    entries[1].tag = 0x2e;
    entries[2].tag = 0x1d;
    entries[3].tag = 0xb;
    entries[4].tag = 0x1d;
    entries[5].tag = 0x34;
    rebuild();
    CHECK(complete(5, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    CHECK(tb_symbol_inline_scope_view(&scope, &view));
    CHECK(view.concrete_available && view.concrete_root.entry_index == 4 && !view.selected_concrete);
}


static void check_failure_evidence(void)
{
    fixture(2);
    entries[1].tag = 0x2e;
    codes[1] = 4;
    rebuild();
    CHECK(complete(1, 600) == TB_SYMBOL_INLINE_SCOPE_UNSUPPORTED);
    TbSymbolInlineScopeFailure_t failure;
    CHECK(tb_symbol_inline_scope_failure(&scope, &failure));
    CHECK(failure.status == TB_SYMBOL_INLINE_SCOPE_UNSUPPORTED && failure.record_available);
    CHECK(failure.record.entry_index == 1 && failure.constant_available && failure.selection_available);
    CHECK(failure.constant_status == TB_SYMBOL_CONSTANT_FOUND && failure.selection.matches == 1);
    CHECK(failure.value_available && failure.constant.value == 4);
    CHECK(failure.selection.record_offset == entries[1].offset);
    CHECK(complete(1, 0) == TB_SYMBOL_INLINE_SCOPE_RESOURCE);
    CHECK(tb_symbol_inline_scope_failure(&scope, &failure));
    CHECK(failure.record.entry_index == 1 && !failure.constant_available && !failure.selection_available);
    codes[1] = 0;
    rebuild();
    CHECK(complete(1, 600) == TB_SYMBOL_INLINE_SCOPE_READY);
    CHECK(!tb_symbol_inline_scope_failure(&scope, &failure));
}


int main(void)
{
    check_roots();
    check_absence_and_depth();
    check_failures();
    check_schema();
    check_physical_only();
    check_failure_evidence();
    puts("PASS physical inline ancestry, nearest roots, independent markers and depth");
    return 0;
}
