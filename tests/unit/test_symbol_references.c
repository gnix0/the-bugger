#include "symbol/references.h"
#include "../support/check.h"

#include <stdlib.h>
#include <string.h>

static TbSymbolUnitEntry_t unit;
static TbSymbolUnitsView_t units;
static TbSymbolDieEntry_t entries[3];
static TbSymbolDieAttribute_t attributes[6];
static TbSymbolDiesView_t tree;

static void fixture(void)
{
    unit = (TbSymbolUnitEntry_t){
        .offset = 1000,
        .extent = {
            .total_bytes  = 1000,
            .end_offset   = 2000,
            .length_bytes = 4,
            .offset_bytes = 4
        },
        .header = {
            .kind          = TB_SYMBOL_DWARF_COMPILE,
            .version       = 5,
            .unit_type     = 1,
            .address_bytes = 8,
            .header_bytes  = 12
        },
        .header_available = true
    };
    unit.header.extent = unit.extent;
    units = (TbSymbolUnitsView_t){
        .entries       = &unit,
        .count         = 1,
        .section_index = 3,
        .kind          = TB_SYMBOL_DWARF_INFO,
        .backing       = {
            .key = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        }
    };
    for (unsigned i = 0; i < 3; ++i) {
        entries[i] = (TbSymbolDieEntry_t){
            .offset          = i == 0 ? 1012U : i == 1 ? 1027U : 1032U,
            .end_offset      = i == 0 ? 1027U : i == 1 ? 1032U : 1037U,
            .parent          = i ? 0 : UINT64_MAX,
            .children        = !i,
            .first_attribute = i == 0 ? 0U : i == 1 ? 4U : 5U,
            .attribute_count = i == 0 ? 4U : 1U
        };
    }
    for (unsigned i = 0; i < 6; ++i) {
        attributes[i] = (TbSymbolDieAttribute_t){
            .name          = 0x49,
            .declared_form = TB_SYMBOL_FORM_REF4,
            .offset        = i < 4 ? 1013 + i * 4 : i == 4 ? 1028U : 1033U,
            .value         = {
                .form           = TB_SYMBOL_FORM_REF4,
                .bytes          = 4,
                .payload_offset = i < 4 ? 1013 + i * 4 : i == 4 ? 1028U : 1033U,
                .payload_bytes  = 4,
                .bits           = 27,
                .numeric        = true
            }
        };
    }
    attributes[1].value.bits = 32;
    attributes[2].declared_form = TB_SYMBOL_FORM_DATA4;
    attributes[2].value.form = TB_SYMBOL_FORM_DATA4;
    attributes[3].declared_form = TB_SYMBOL_FORM_INDIRECT;
    attributes[3].value.form = TB_SYMBOL_FORM_REF1;
    attributes[3].value.indirect = true;
    attributes[3].value.bytes = 2;
    attributes[3].value.payload_offset = 1026;
    attributes[3].value.payload_bytes = 1;
    tree = (TbSymbolDiesView_t){
        .entries         = entries,
        .attributes      = attributes,
        .count           = 3,
        .attribute_count = 6,
        .max_depth       = 1,
        .section_index   = 3,
        .unit_offset     = 1000,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing         = units.backing
    };
}


static TbSymbolReferenceStatus_e prepare(uint64_t die, uint64_t attribute, TbSymbolReference_t *reference)
{
    return tb_symbol_reference_prepare(units, tree, (TbSymbolReferenceSelection_t){
        .unit_index      = 0,
        .die_index       = die,
        .attribute_index = attribute
    }, reference);
}


static void prepare_failure(TbSymbolReferenceStatus_e expected, uint64_t die, uint64_t attribute)
{
    TbSymbolReference_t reference;
    unsigned char before[sizeof(reference)];
    memset(&reference, 0xa5, sizeof(reference));
    memcpy(before, &reference, sizeof(reference));
    CHECK(prepare(die, attribute, &reference) == expected);
    CHECK(!memcmp(before, &reference, sizeof(reference)));
}


static void lookup_failure(TbSymbolReference_t reference, TbSymbolDiesView_t selected,
                           TbSymbolReferenceStatus_e expected)
{
    TbSymbolReferenceTarget_t target;
    unsigned char before[sizeof(target)];
    memset(&target, 0xa5, sizeof(target));
    memcpy(before, &target, sizeof(target));
    CHECK(tb_symbol_reference_unit(reference, selected, &target) == expected);
    CHECK(!memcmp(before, &target, sizeof(target)));
}


static void exact_identity(void)
{
    fixture();
    TbSymbolReference_t first;
    TbSymbolReference_t second;
    CHECK(prepare(0, 0, &first) == TB_SYMBOL_REFERENCE_READY);
    CHECK(prepare(0, 1, &second) == TB_SYMBOL_REFERENCE_READY);
    CHECK(first.source_section == 3 && first.source_unit == 1000 && first.source_record == 1012);
    CHECK(first.source_attribute == attributes[0].offset && second.source_attribute == attributes[1].offset);
    CHECK(first.target_offset == 1027 && second.target_offset == 1032);
    TbSymbolReferenceTarget_t left;
    TbSymbolReferenceTarget_t right;
    CHECK(tb_symbol_reference_unit(first, tree, &left) == TB_SYMBOL_REFERENCE_FOUND);
    CHECK(tb_symbol_reference_unit(second, tree, &right) == TB_SYMBOL_REFERENCE_FOUND);
    CHECK(left.entry_index == 1 && right.entry_index == 2 && left.unit_offset == 1000);
    CHECK(left.record_offset == 1027 && right.record_offset == 1032 && left.section_index == 3);
    prepare_failure(TB_SYMBOL_REFERENCE_NOT_REFERENCE, 0, 2);
    CHECK(prepare(0, 3, &first) == TB_SYMBOL_REFERENCE_READY);
    CHECK(first.form == TB_SYMBOL_FORM_REF1);
    CHECK(tb_symbol_reference_unit(first, tree, &left) == TB_SYMBOL_REFERENCE_FOUND && left.entry_index == 1);
    TbSymbolDiesView_t reopened = tree;
    ++reopened.backing.key.inode;
    lookup_failure(first, reopened, TB_SYMBOL_REFERENCE_DEPENDENCY);
    reopened = tree;
    ++reopened.unit_offset;
    lookup_failure(first, reopened, TB_SYMBOL_REFERENCE_DEPENDENCY);
    reopened = tree;
    ++reopened.section_index;
    lookup_failure(first, reopened, TB_SYMBOL_REFERENCE_DEPENDENCY);
    reopened = (TbSymbolDiesView_t){0};
    lookup_failure(first, reopened, TB_SYMBOL_REFERENCE_DEPENDENCY);
}


static void forms_and_bounds(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_REF1, TB_SYMBOL_FORM_REF2, TB_SYMBOL_FORM_REF4,
                              TB_SYMBOL_FORM_REF8, TB_SYMBOL_FORM_REF_UDATA};
    for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        fixture();
        attributes[0].value.form = forms[i];
        TbSymbolReference_t reference;
        CHECK(prepare(0, 0, &reference) == TB_SYMBOL_REFERENCE_READY);
        TbSymbolReferenceTarget_t target;
        CHECK(tb_symbol_reference_unit(reference, tree, &target) == TB_SYMBOL_REFERENCE_FOUND);
        const uint64_t bad[] = {0, 11, 1000, UINT64_MAX};
        for (size_t j = 0; j < sizeof(bad) / sizeof(bad[0]); ++j) {
            attributes[0].value.bits = bad[j];
            prepare_failure(TB_SYMBOL_REFERENCE_RANGE, 0, 0);
        }
        attributes[0].value.bits = 21;
        CHECK(prepare(0, 0, &reference) == TB_SYMBOL_REFERENCE_READY);
        lookup_failure(reference, tree, TB_SYMBOL_REFERENCE_MISSING_DIE);
        attributes[0].value.numeric = false;
        prepare_failure(TB_SYMBOL_REFERENCE_RANGE, 0, 0);
        attributes[0].value.numeric = true;
        attributes[0].value.is_signed = true;
        prepare_failure(TB_SYMBOL_REFERENCE_RANGE, 0, 0);
    }
    const uint64_t dependencies[] = {TB_SYMBOL_FORM_REF_ADDR, TB_SYMBOL_FORM_REF_SIG8,
                                     TB_SYMBOL_FORM_REF_SUP4, TB_SYMBOL_FORM_REF_SUP8};
    for (size_t i = 0; i < sizeof(dependencies) / sizeof(dependencies[0]); ++i) {
        fixture();
        attributes[0].value.form = dependencies[i];
        attributes[0].value.bits = UINT64_MAX;
        TbSymbolReference_t reference;
        CHECK(prepare(0, 0, &reference) == TB_SYMBOL_REFERENCE_READY);
        CHECK(reference.value == UINT64_MAX && !reference.target_offset);
        TbSymbolReferenceKind_e kind = i == 0 ? TB_SYMBOL_REFERENCE_INFO
                                         : i == 1 ? TB_SYMBOL_REFERENCE_SIGNATURE
                                                  : TB_SYMBOL_REFERENCE_SUPPLEMENTARY;
        CHECK(reference.kind == kind);
        TbSymbolReferenceStatus_e expected = i < 2 ? TB_SYMBOL_REFERENCE_DEPENDENCY
                                                     : TB_SYMBOL_REFERENCE_UNSUPPORTED;
        lookup_failure(reference, tree, expected);
    }
}


static void invalid(void)
{
    fixture();
    prepare_failure(TB_SYMBOL_REFERENCE_INVALID, 3, 0);
    prepare_failure(TB_SYMBOL_REFERENCE_INVALID, 0, 4);
    TbSymbolReference_t reference;
    CHECK(prepare(0, 0, NULL) == TB_SYMBOL_REFERENCE_INVALID);
    ++units.backing.key.inode;
    prepare_failure(TB_SYMBOL_REFERENCE_INVALID, 0, 0);
    fixture();
    unit.header_available = false;
    prepare_failure(TB_SYMBOL_REFERENCE_INVALID, 0, 0);
    fixture();
    entries[0].first_attribute = UINT64_MAX;
    prepare_failure(TB_SYMBOL_REFERENCE_INVALID, 0, 1);
    reference = (TbSymbolReference_t){
        .kind = TB_SYMBOL_REFERENCE_UNIT
    };
    CHECK(tb_symbol_reference_unit(reference, tree, NULL) == TB_SYMBOL_REFERENCE_INVALID);
    reference.kind = (TbSymbolReferenceKind_e)99;
    lookup_failure(reference, tree, TB_SYMBOL_REFERENCE_INVALID);
}


static void info_references(void)
{
    fixture();
    TbSymbolUnitEntry_t contributions[3];
    for (unsigned i = 0; i < 3; ++i) {
        contributions[i] = unit;
        contributions[i].offset += i * 1000;
        contributions[i].extent.end_offset += i * 1000;
        contributions[i].header.extent = contributions[i].extent;
    }
    TbSymbolUnitsView_t selected = units;
    selected.entries = contributions;
    selected.count = 3;
    TbSymbolReference_t reference = {
        .backing = units.backing,
        .kind    = TB_SYMBOL_REFERENCE_INFO,
        .value   = 3027
    };
    TbSymbolReferenceNeed_t need;
    CHECK(tb_symbol_reference_info(reference, selected, &need) == TB_SYMBOL_REFERENCE_READY);
    CHECK(need.unit_index == 2 && need.unit_offset == 3000 && need.record_offset == 3027);
    TbSymbolDieEntry_t relocated[3];
    memcpy(relocated, entries, sizeof(relocated));
    for (unsigned i = 0; i < 3; ++i) {
        relocated[i].offset += 2000;
        relocated[i].end_offset += 2000;
    }
    TbSymbolDieAttribute_t relocated_attributes[6];
    memcpy(relocated_attributes, attributes, sizeof(relocated_attributes));
    for (unsigned i = 0; i < 6; ++i) {
        relocated_attributes[i].offset += 2000;
        relocated_attributes[i].value.payload_offset += 2000;
    }
    TbSymbolDiesView_t target_tree = tree;
    target_tree.entries = relocated;
    target_tree.attributes = relocated_attributes;
    target_tree.unit_offset = 3000;
    TbSymbolReferenceTarget_t target;
    CHECK(tb_symbol_reference_target(need, target_tree, &target) == TB_SYMBOL_REFERENCE_FOUND);
    CHECK(target.entry_index == 1 && target.unit_offset == 3000 && target.record_offset == 3027);
    CHECK(tb_symbol_reference_target(need, tree, &target) == TB_SYMBOL_REFERENCE_DEPENDENCY);
    const uint64_t bad[] = {0, 999, 1000, 2001, 3000, 4000, UINT64_MAX};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        reference.value = bad[i];
        memset(&need, 0xa5, sizeof(need));
        unsigned char before[sizeof(need)];
        memcpy(before, &need, sizeof(need));
        CHECK(tb_symbol_reference_info(reference, selected, &need) == TB_SYMBOL_REFERENCE_RANGE);
        CHECK(!memcmp(before, &need, sizeof(need)));
    }
    reference.value = 3030;
    CHECK(tb_symbol_reference_info(reference, selected, &need) == TB_SYMBOL_REFERENCE_READY);
    CHECK(tb_symbol_reference_target(need, target_tree, &target) == TB_SYMBOL_REFERENCE_MISSING_DIE);
    contributions[2].header_available = false;
    CHECK(tb_symbol_reference_info(reference, selected, &need) == TB_SYMBOL_REFERENCE_UNSUPPORTED);
    selected.kind = TB_SYMBOL_DWARF_TYPES;
    CHECK(tb_symbol_reference_info(reference, selected, &need) == TB_SYMBOL_REFERENCE_INVALID);
    selected.kind = TB_SYMBOL_DWARF_INFO;
    ++selected.backing.key.inode;
    CHECK(tb_symbol_reference_info(reference, selected, &need) == TB_SYMBOL_REFERENCE_DEPENDENCY);
}


static TbSymbolReferenceStatus_e type_lookup(TbSymbolReference_t reference,
                                             const TbSymbolUnitsView_t *scopes, size_t count,
                                             TbSymbolReferenceNeed_t *selected)
{
    TbSymbolTypeReference_t lookup;
    CHECK(tb_symbol_type_reference_init(&lookup, reference, scopes, count) == TB_SYMBOL_REFERENCE_PENDING);
    uint64_t limit = count + 1;
    for (size_t i = 0; i < count; ++i)
        limit += scopes[i].count;
    TbSymbolReferenceStatus_e status = TB_SYMBOL_REFERENCE_PENDING;
    uint64_t steps = 0;
    while (status == TB_SYMBOL_REFERENCE_PENDING) {
        TbSymbolReferenceNeed_t need;
        unsigned char before[sizeof(need)];
        memset(&need, 0xa5, sizeof(need));
        memcpy(before, &need, sizeof(need));
        CHECK(!tb_symbol_type_reference_need(&lookup, &need) && !memcmp(before, &need, sizeof(need)));
        status = tb_symbol_type_reference_step(&lookup);
        CHECK(++steps <= limit);
    }
    CHECK(tb_symbol_type_reference_step(&lookup) == status);
    if (status == TB_SYMBOL_REFERENCE_READY) {
        CHECK(tb_symbol_type_reference_need(&lookup, selected));
    } else {
        TbSymbolReferenceNeed_t need;
        unsigned char before[sizeof(need)];
        memset(&need, 0xa5, sizeof(need));
        memcpy(before, &need, sizeof(need));
        CHECK(!tb_symbol_type_reference_need(&lookup, &need) && !memcmp(before, &need, sizeof(need)));
    }
    return status;
}


static void type_references(void)
{
    fixture();
    unit.header.kind = TB_SYMBOL_DWARF_TYPE;
    unit.header.unit_type = 2;
    unit.header.header_bytes = 24;
    for (unsigned i = 0; i < 3; ++i) {
        entries[i].offset += 12;
        entries[i].end_offset += 12;
    }
    for (unsigned i = 0; i < 6; ++i) {
        attributes[i].offset += 12;
        attributes[i].value.payload_offset += 12;
    }
    tree.kind = TB_SYMBOL_DWARF_TYPE;
    entries[0].tag = 0x41;
    entries[2].tag = 0x24;
    unit.header.has_signature = true;
    unit.header.signature = 0x123456;
    unit.header.type_offset = 44;
    TbSymbolUnitsView_t scopes[] = {units, units};
    TbSymbolReference_t reference = {
        .backing = units.backing,
        .kind    = TB_SYMBOL_REFERENCE_SIGNATURE,
        .value   = 0x123456
    };
    TbSymbolReferenceNeed_t need;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_READY);
    CHECK(need.section_index == 3 && need.unit_offset == 1000 && need.record_offset == 1044);
    TbSymbolReferenceTarget_t target;
    CHECK(tb_symbol_reference_target(need, tree, &target) == TB_SYMBOL_REFERENCE_FOUND &&
          target.entry_index == 2);
    ++scopes[1].section_index;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_CONFLICT);
    scopes[1] = scopes[0];
    TbSymbolUnitEntry_t distinct = unit;
    distinct.offset += 1000;
    distinct.extent.end_offset += 1000;
    distinct.header.extent = distinct.extent;
    scopes[1].entries = &distinct;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_CONFLICT);
    distinct.header.signature = 0x654321;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_READY);
    distinct.header_available = false;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_COVERAGE);
    reference.value = 0;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_COVERAGE);
    distinct.header_available = true;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_MISSING_TYPE);
    CHECK(type_lookup(reference, NULL, 0, &need) == TB_SYMBOL_REFERENCE_MISSING_TYPE);
    reference.value = unit.header.signature;
    ++scopes[1].backing.key.inode;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_INVALID);
    scopes[1] = scopes[0];
    unit.header.type_offset = 1000;
    CHECK(type_lookup(reference, scopes, 2, &need) == TB_SYMBOL_REFERENCE_RANGE);
    TbSymbolTypeReference_t lookup = {0};
    CHECK(tb_symbol_type_reference_step(&lookup) == TB_SYMBOL_REFERENCE_INVALID);
    CHECK(!tb_symbol_type_reference_need(&lookup, &need));
    CHECK(tb_symbol_type_reference_init(NULL, reference, scopes, 2) == TB_SYMBOL_REFERENCE_INVALID);
    CHECK(tb_symbol_type_reference_init(&lookup, reference, NULL, 2) == TB_SYMBOL_REFERENCE_INVALID);
    CHECK(tb_symbol_type_reference_step(NULL) == TB_SYMBOL_REFERENCE_INVALID);
}


static void late_type_conflict(void)
{
    fixture();
    size_t count = 100001;
    TbSymbolUnitEntry_t *many = calloc(count, sizeof(*many));
    CHECK(many);
    for (size_t i = 0; i < count; ++i) {
        many[i] = unit;
        many[i].offset = i * 1000;
        many[i].extent.end_offset = (i + 1) * 1000;
        many[i].header.extent = many[i].extent;
        many[i].header.kind = TB_SYMBOL_DWARF_TYPE;
        many[i].header.unit_type = 2;
        many[i].header.header_bytes = 24;
        many[i].header.type_offset = 44;
        many[i].header.has_signature = true;
        many[i].header.signature = i ? 0x654321U : 0x123456U;
    }
    units.backing.bytes = 1024 * 1024 * 1024;
    units.entries = many;
    units.count = count;
    TbSymbolReference_t reference = {
        .backing = units.backing,
        .kind    = TB_SYMBOL_REFERENCE_SIGNATURE,
        .value   = 0x123456
    };
    TbSymbolReferenceNeed_t need;
    CHECK(type_lookup(reference, &units, 1, &need) == TB_SYMBOL_REFERENCE_READY);
    CHECK(!need.unit_offset && need.record_offset == 44);
    many[count - 1].header.signature = reference.value;
    CHECK(type_lookup(reference, &units, 1, &need) == TB_SYMBOL_REFERENCE_CONFLICT);
    many[count - 1].header_available = false;
    CHECK(type_lookup(reference, &units, 1, &need) == TB_SYMBOL_REFERENCE_COVERAGE);
    free(many);
}


static void large(void)
{
    fixture();
    size_t count = 100001;
    TbSymbolDieEntry_t *many = calloc(count, sizeof(*many));
    CHECK(many);
    for (size_t i = 0; i < count; ++i)
        many[i] = (TbSymbolDieEntry_t){
            .offset     = 1012 + i * 2,
            .end_offset = 1014 + i * 2,
            .parent     = i ? 0 : UINT64_MAX,
            .children   = !i
        };
    tree.backing.bytes = 1024 * 1024;
    tree.entries = many;
    tree.attributes = NULL;
    tree.attribute_count = 0;
    tree.count = count;
    TbSymbolReference_t reference = {
        .backing        = tree.backing,
        .kind           = TB_SYMBOL_REFERENCE_UNIT,
        .source_section = tree.section_index,
        .source_unit    = tree.unit_offset
    };
    const uint64_t indexes[] = {0, 1, 49999, 50000, 99999, 100000};
    for (size_t i = 0; i < sizeof(indexes) / sizeof(indexes[0]); ++i) {
        reference.target_offset = many[indexes[i]].offset;
        TbSymbolReferenceTarget_t target;
        CHECK(tb_symbol_reference_unit(reference, tree, &target) == TB_SYMBOL_REFERENCE_FOUND);
        CHECK(target.entry_index == indexes[i] && target.record_offset == reference.target_offset);
        ++reference.target_offset;
        lookup_failure(reference, tree, TB_SYMBOL_REFERENCE_MISSING_DIE);
    }
    reference.target_offset = 999;
    lookup_failure(reference, tree, TB_SYMBOL_REFERENCE_MISSING_DIE);
    free(many);
}


int main(void)
{
    exact_identity();
    forms_and_bounds();
    invalid();
    large();
    info_references();
    type_references();
    late_type_conflict();
    puts("PASS exact DWARF reference coordinates, cross-unit needs, signature ambiguity and coverage");
    return 0;
}
