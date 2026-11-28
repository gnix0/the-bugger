#include "symbol/relations.h"
#include "../support/check.h"

#include <stdlib.h>
#include <string.h>

static TbSymbolUnitEntry_t units[2];
static TbSymbolUnitsView_t inventory;
static TbSymbolDieEntry_t entries[3];
static TbSymbolDieEntry_t remote_entries[2];
static TbSymbolDieAttribute_t attributes[4];
static TbSymbolDiesView_t tree;
static TbSymbolDiesView_t remote_tree;
static TbSymbolRelationSelection_t selection;

static uint64_t name(TbSymbolRelationKind_e kind)
{
    switch (kind) {
    case TB_SYMBOL_RELATION_SPECIFICATION:
        return 0x47;
    case TB_SYMBOL_RELATION_ORIGIN:
        return 0x31;
    case TB_SYMBOL_RELATION_EXTENSION:
        return 0x54;
    default:
        return 0;
    }
}


static void fixture(TbSymbolRelationKind_e kind, uint64_t form)
{
    for (size_t i = 0; i < 2; ++i) {
        units[i] = (TbSymbolUnitEntry_t){
            .offset           = i * 200,
            .extent           = {
                .payload_bytes = 196,
                .total_bytes   = 200,
                .end_offset    = (i + 1) * 200,
                .length_bytes  = 4,
                .offset_bytes  = 4
            },
            .header           = {
                .kind          = TB_SYMBOL_DWARF_COMPILE,
                .version       = 5,
                .unit_type     = 1,
                .address_bytes = 8,
                .abbrev_offset = i * 100,
                .header_bytes  = 12
            },
            .header_available = true
        };
        units[i].header.extent = units[i].extent;
    }
    inventory = (TbSymbolUnitsView_t){
        .entries       = units,
        .count         = 2,
        .section_index = 1,
        .kind          = TB_SYMBOL_DWARF_INFO,
        .backing       = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        }
    };
    size_t bytes = form == TB_SYMBOL_FORM_REF1 || form == TB_SYMBOL_FORM_REF_UDATA ? 1
                     : form == TB_SYMBOL_FORM_REF2 ? 2
                         : form == TB_SYMBOL_FORM_REF8 || form == TB_SYMBOL_FORM_REF_SIG8 ||
                                   form == TB_SYMBOL_FORM_REF_SUP8 ? 8 : 4;
    attributes[0] = (TbSymbolDieAttribute_t){
        .name          = name(kind),
        .declared_form = form,
        .offset        = 14,
        .value         = {
            .form           = form,
            .bytes          = bytes,
            .payload_offset = 14,
            .payload_bytes  = bytes,
            .bits           = 14 + bytes,
            .numeric        = true
        }
    };
    entries[0] = (TbSymbolDieEntry_t){
        .offset     = 12,
        .end_offset = 13,
        .parent     = UINT64_MAX,
        .code       = 1,
        .tag        = 0x11,
        .children   = true
    };
    entries[1] = (TbSymbolDieEntry_t){
        .offset          = 13,
        .end_offset      = 14 + bytes,
        .parent          = 0,
        .code            = 2,
        .tag             = kind == TB_SYMBOL_RELATION_EXTENSION ? 0x39 : 0x34,
        .first_attribute = 0,
        .attribute_count = 1
    };
    entries[2] = (TbSymbolDieEntry_t){
        .offset          = 14 + bytes,
        .end_offset      = 15 + bytes,
        .parent          = 0,
        .code            = 3,
        .tag             = entries[1].tag,
        .first_attribute = 1
    };
    tree = (TbSymbolDiesView_t){
        .entries         = entries,
        .attributes      = attributes,
        .count           = 3,
        .attribute_count = 1,
        .max_depth       = 1,
        .section_index   = 1,
        .unit_offset     = 0,
        .abbrev_section  = 2,
        .abbrev_offset   = 0,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing         = inventory.backing
    };
    remote_entries[0] = entries[0];
    remote_entries[0].offset = 212;
    remote_entries[0].end_offset = 213;
    remote_entries[1] = entries[2];
    remote_entries[1].offset = 213;
    remote_entries[1].end_offset = 214;
    remote_entries[1].first_attribute = 0;
    remote_tree = tree;
    remote_tree.entries = remote_entries;
    remote_tree.count = 2;
    remote_tree.attributes = NULL;
    remote_tree.attribute_count = 0;
    remote_tree.unit_offset = 200;
    remote_tree.abbrev_offset = 100;
    selection = (TbSymbolRelationSelection_t){
        .unit_index = 0,
        .die_index  = 1,
        .kind       = kind
    };
    if (form == TB_SYMBOL_FORM_REF_ADDR)
        attributes[0].value.bits = remote_entries[1].offset;
}


static void unavailable(const TbSymbolRelation_t *relation)
{
    TbSymbolRelationSource_t source;
    unsigned char before[sizeof(source)];
    memset(&source, 0xa5, sizeof(source));
    memcpy(before, &source, sizeof(source));
    CHECK(!tb_symbol_relation_source(relation, &source));
    CHECK(!memcmp(before, &source, sizeof(source)));
}


static TbSymbolRelation_t query(TbSymbolRelationStatus_e expected)
{
    TbSymbolRelation_t relation;
    CHECK(tb_symbol_relation_init(&relation, inventory, tree, selection) == TB_SYMBOL_RELATION_PENDING);
    TbSymbolRelationStatus_e status = TB_SYMBOL_RELATION_PENDING;
    for (uint64_t turn = 0; turn <= tree.attribute_count && status == TB_SYMBOL_RELATION_PENDING; ++turn) {
        unavailable(&relation);
        uint64_t before = relation.lookup.cursor;
        status = tb_symbol_relation_step(&relation, 1);
        CHECK(relation.lookup.cursor >= before && relation.lookup.cursor - before <= 1);
    }
    CHECK(status == expected && tb_symbol_relation_step(&relation, 1) == expected);
    if (status != TB_SYMBOL_RELATION_READY)
        unavailable(&relation);
    TbSymbolAttributeResult_t attribute;
    TbSymbolReferenceStatus_e reference;
    CHECK(tb_symbol_relation_diagnostic(&relation, &attribute, &reference));
    CHECK(attribute.name == name(selection.kind));
    CHECK(attribute.record_offset == entries[selection.die_index].offset);
    return relation;
}


static TbSymbolRelationSource_t source(void)
{
    TbSymbolRelation_t relation = query(TB_SYMBOL_RELATION_READY);
    TbSymbolRelationSource_t result;
    CHECK(tb_symbol_relation_source(&relation, &result));
    CHECK(result.source_tag == entries[selection.die_index].tag && result.kind == selection.kind);
    CHECK(result.source_unit_kind == tree.kind);
    CHECK(result.source_version == units[selection.unit_index].header.version);
    CHECK(result.abbrev_section == tree.abbrev_section && result.abbrev_offset == tree.abbrev_offset);
    CHECK(result.attribute.first_index == entries[selection.die_index].first_attribute);
    CHECK(result.field.offset == attributes[entries[selection.die_index].first_attribute].offset);
    CHECK(result.reference.source_record == entries[selection.die_index].offset);
    return result;
}


static TbSymbolRelationStatus_e target(TbSymbolRelationNeed_t need, const TbSymbolDiesView_t *selected)
{
    TbSymbolRelationEdge_t edge;
    unsigned char before[sizeof(edge)];
    memset(&edge, 0xa5, sizeof(edge));
    memcpy(before, &edge, sizeof(edge));
    TbSymbolRelationStatus_e status = tb_symbol_relation_target(need, selected, &edge);
    if (status == TB_SYMBOL_RELATION_FOUND) {
        CHECK(selected);
        CHECK(edge.source.reference.source_record == entries[selection.die_index].offset);
        CHECK(edge.target.section_index == selected->section_index);
        CHECK(edge.target.unit_offset == selected->unit_offset);
        CHECK(edge.target.record_offset == selected->entries[edge.target.entry_index].offset);
        CHECK(edge.target_tag == selected->entries[edge.target.entry_index].tag);
        CHECK(edge.target_unit_kind == selected->kind && edge.target_version == need.target_version);
        CHECK(edge.target_abbrev_section == selected->abbrev_section);
        CHECK(edge.target_abbrev_offset == selected->abbrev_offset);
    } else {
        CHECK(!memcmp(before, &edge, sizeof(edge)));
    }
    return status;
}


static void direct_links(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_REF1, TB_SYMBOL_FORM_REF2, TB_SYMBOL_FORM_REF4,
                              TB_SYMBOL_FORM_REF8, TB_SYMBOL_FORM_REF_UDATA};
    for (unsigned kind = 0; kind <= TB_SYMBOL_RELATION_EXTENSION; ++kind) {
        for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); ++i) {
            fixture((TbSymbolRelationKind_e)kind, forms[i]);
            TbSymbolRelationNeed_t need;
            CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
            CHECK(need.target.record_offset == entries[2].offset);
            CHECK(target(need, &tree) == TB_SYMBOL_RELATION_FOUND);
        }
    }
    fixture(TB_SYMBOL_RELATION_ORIGIN, TB_SYMBOL_FORM_REF4);
    entries[1].tag = 0x1d;
    entries[2].tag = 0x2e;
    TbSymbolRelationNeed_t need;
    CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_FOUND);
    entries[2].tag = 0x1d;
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_TAG_MISMATCH);
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    entries[2].tag = 0x2e;
    CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_TAG_MISMATCH);
    fixture(TB_SYMBOL_RELATION_EXTENSION, TB_SYMBOL_FORM_REF4);
    entries[1].tag = 0x34;
    entries[2].tag = 0x34;
    CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_TAG_MISMATCH);
    fixture(TB_SYMBOL_RELATION_ORIGIN, TB_SYMBOL_FORM_REF4);
    attributes[0].value.bits = entries[1].offset;
    CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_SELF_CYCLE);
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    attributes[0].value.bits = entries[2].offset + 1;
    CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_MISSING_TARGET);
    CHECK(target(need, NULL) == TB_SYMBOL_RELATION_DEPENDENCY);
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
    entries[2].parent = 999;
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_INVALID);
    entries[2].parent = 0;
    need.target_version = 4;
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_INVALID);
}


static void member_parent(bool remote)
{
    static TbSymbolDieEntry_t nested[4];
    static TbSymbolDieEntry_t nested_remote[3];
    TbSymbolDiesView_t *selected = remote ? &remote_tree : &tree;
    TbSymbolDieEntry_t *nodes = remote ? nested_remote : nested;
    size_t index = (size_t)selected->count - 1;
    memcpy(nodes, selected->entries, (size_t)selected->count * sizeof(*nodes));
    nodes[index].tag = 0x02;
    nodes[index].children = true;
    nodes[index + 1] = nodes[index];
    ++nodes[index + 1].offset;
    ++nodes[index + 1].end_offset;
    nodes[index + 1].code = 4;
    nodes[index + 1].parent = index;
    nodes[index + 1].tag = 0x0d;
    nodes[index + 1].children = false;
    selected->entries = nodes;
    selected->max_depth = 2;
    ++selected->count;
    attributes[0].value.bits = nodes[index + 1].offset;
}


static void versioned_members(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
        units[0].header.version = (uint16_t)version;
        if (version == 4) {
            units[0].header.header_bytes = 11;
            units[0].header.unit_type = 0;
            units[0].header.kind = TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
        }
        member_parent(false);
        TbSymbolRelationNeed_t need;
        CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
        CHECK(need.target_version == version);
        CHECK(target(need, &tree) == (version == 4 ? TB_SYMBOL_RELATION_FOUND
                                                 : TB_SYMBOL_RELATION_TAG_MISMATCH));
        if (version == 4) {
            TbSymbolDieEntry_t wrong_parent[4];
            memcpy(wrong_parent, tree.entries, sizeof(wrong_parent));
            wrong_parent[2].tag = 0x39;
            TbSymbolDiesView_t wrong = tree;
            wrong.entries = wrong_parent;
            CHECK(target(need, &wrong) == TB_SYMBOL_RELATION_TAG_MISMATCH);
        }
    }
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF_ADDR);
    units[1].header.version = 4;
    units[1].header.header_bytes = 11;
    units[1].header.unit_type = 0;
    units[1].header.kind = TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
    member_parent(true);
    TbSymbolRelationNeed_t need;
    CHECK(tb_symbol_relation_need(source(), &inventory, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(need.source.source_version == 5 && need.target_version == 4);
    CHECK(target(need, &remote_tree) == TB_SYMBOL_RELATION_FOUND);
}


static void independent_roles_and_edges(void)
{
    for (unsigned declared = 0; declared < 2; ++declared) {
        fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
        attributes[1] = (TbSymbolDieAttribute_t){
            .name          = 0x3c,
            .declared_form = TB_SYMBOL_FORM_FLAG,
            .offset        = entries[1].end_offset,
            .value         = {
                .form           = TB_SYMBOL_FORM_FLAG,
                .bytes          = 1,
                .payload_offset = entries[1].end_offset,
                .payload_bytes  = 1,
                .bits           = declared,
                .numeric        = true
            }
        };
        ++entries[1].attribute_count;
        ++entries[1].end_offset;
        ++entries[2].offset;
        entries[2].end_offset = entries[2].offset + 2;
        entries[2].first_attribute = 2;
        entries[2].attribute_count = 1;
        attributes[2] = attributes[1];
        attributes[2].offset = entries[2].offset + 1;
        attributes[2].value.payload_offset = attributes[2].offset;
        attributes[2].value.bits = !declared;
        attributes[0].value.bits = entries[2].offset;
        tree.attribute_count = 3;
        TbSymbolRelationNeed_t need;
        CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
        CHECK(target(need, &tree) == TB_SYMBOL_RELATION_FOUND);
    }
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    attributes[1] = attributes[0];
    attributes[1].offset = entries[2].offset + 1;
    attributes[1].value.payload_offset = attributes[1].offset;
    attributes[1].value.bits = entries[1].offset;
    entries[2].attribute_count = 1;
    entries[2].end_offset += 4;
    tree.attribute_count = 2;
    TbSymbolRelationNeed_t first;
    CHECK(tb_symbol_relation_need(source(), NULL, &first) == TB_SYMBOL_RELATION_READY);
    CHECK(target(first, &tree) == TB_SYMBOL_RELATION_FOUND);
    selection.die_index = 2;
    TbSymbolRelationNeed_t second;
    CHECK(tb_symbol_relation_need(source(), NULL, &second) == TB_SYMBOL_RELATION_READY);
    CHECK(target(second, &tree) == TB_SYMBOL_RELATION_FOUND);
    CHECK(first.target.record_offset == second.source.reference.source_record);
    CHECK(second.target.record_offset == first.source.reference.source_record);
    CHECK(first.source.reference.source_record != second.source.reference.source_record);

    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    attributes[0].declared_form = TB_SYMBOL_FORM_INDIRECT;
    attributes[0].value.indirect = true;
    ++attributes[0].value.bytes;
    ++attributes[0].value.payload_offset;
    ++entries[1].end_offset;
    ++entries[2].offset;
    ++entries[2].end_offset;
    attributes[0].value.bits = entries[2].offset;
    TbSymbolRelationNeed_t need;
    CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_FOUND);
}


static void cross_unit_links(void)
{
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF_ADDR);
    TbSymbolRelationSource_t selected = source();
    TbSymbolRelationNeed_t need;
    memset(&need, 0xa5, sizeof(need));
    unsigned char before[sizeof(need)];
    memcpy(before, &need, sizeof(need));
    CHECK(tb_symbol_relation_need(selected, NULL, &need) == TB_SYMBOL_RELATION_DEPENDENCY);
    CHECK(!memcmp(before, &need, sizeof(need)));
    CHECK(tb_symbol_relation_need(selected, &inventory, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(need.target.unit_index == 1 && need.target.unit_offset == 200);
    CHECK(target(need, &remote_tree) == TB_SYMBOL_RELATION_FOUND);
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_DEPENDENCY);
    ++need.target.record_offset;
    CHECK(target(need, &remote_tree) == TB_SYMBOL_RELATION_INVALID);
    CHECK(tb_symbol_relation_need(selected, &inventory, &need) == TB_SYMBOL_RELATION_READY);
    ++remote_tree.backing.key.inode;
    CHECK(target(need, &remote_tree) == TB_SYMBOL_RELATION_DEPENDENCY);
    --remote_tree.backing.key.inode;
    TbSymbolUnitsView_t wrong = inventory;
    ++wrong.backing.key.inode;
    CHECK(tb_symbol_relation_need(selected, &wrong, &need) == TB_SYMBOL_RELATION_DEPENDENCY);
    wrong = inventory;
    wrong.kind = TB_SYMBOL_DWARF_TYPES;
    CHECK(tb_symbol_relation_need(selected, &wrong, &need) == TB_SYMBOL_RELATION_INVALID);
    wrong = inventory;
    wrong.count = 0;
    wrong.entries = NULL;
    CHECK(tb_symbol_relation_need(selected, &wrong, &need) == TB_SYMBOL_RELATION_RANGE);
    units[1].header_available = false;
    CHECK(tb_symbol_relation_need(selected, &inventory, &need) == TB_SYMBOL_RELATION_UNSUPPORTED);
    units[1].header_available = true;
    units[1].header.version = 6;
    CHECK(tb_symbol_relation_need(selected, &inventory, &need) == TB_SYMBOL_RELATION_UNSUPPORTED);
    units[1].header.version = 4;
    entries[1].tag = 0x47;
    remote_entries[1].tag = 0x47;
    CHECK(tb_symbol_relation_need(source(), &inventory, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(target(need, &remote_tree) == TB_SYMBOL_RELATION_UNSUPPORTED);
    const uint64_t unsupported[] = {TB_SYMBOL_FORM_REF_SIG8, TB_SYMBOL_FORM_REF_SUP4,
                                    TB_SYMBOL_FORM_REF_SUP8};
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        fixture(TB_SYMBOL_RELATION_ORIGIN, unsupported[i]);
        CHECK(tb_symbol_relation_need(source(), &inventory, &need) == TB_SYMBOL_RELATION_UNSUPPORTED);
    }
}


static void unsupported_tags(void)
{
    const uint64_t tags[] = {0x4109, 0x410a, 0x4080, 0, 0x14, 0x3e, 0x1000, UINT64_MAX};
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        fixture(TB_SYMBOL_RELATION_ORIGIN, TB_SYMBOL_FORM_REF4);
        entries[1].tag = tags[i];
        TbSymbolRelationSource_t selected = source();
        TbSymbolRelationNeed_t need;
        memset(&need, 0xa5, sizeof(need));
        unsigned char before[sizeof(need)];
        memcpy(before, &need, sizeof(need));
        CHECK(tb_symbol_relation_need(selected, NULL, &need) == TB_SYMBOL_RELATION_UNSUPPORTED);
        CHECK(!memcmp(before, &need, sizeof(need)));
        CHECK(selected.reference.source_record == entries[1].offset);
        CHECK(selected.reference.target_offset == entries[2].offset);
    }
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    entries[2].tag = 0x4109;
    TbSymbolRelationNeed_t need;
    CHECK(tb_symbol_relation_need(source(), NULL, &need) == TB_SYMBOL_RELATION_READY);
    CHECK(target(need, &tree) == TB_SYMBOL_RELATION_UNSUPPORTED);
}


static void invalid_sources(void)
{
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    entries[1].attribute_count = 0;
    (void)query(TB_SYMBOL_RELATION_ABSENT);
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    attributes[1] = attributes[0];
    attributes[1].offset += 4;
    attributes[1].value.payload_offset += 4;
    entries[1].attribute_count = 2;
    entries[1].end_offset += 4;
    entries[2].offset += 4;
    entries[2].end_offset += 4;
    entries[2].first_attribute = 2;
    attributes[0].value.bits = entries[2].offset;
    attributes[1].value.bits = entries[2].offset;
    tree.attribute_count = 2;
    TbSymbolRelation_t relation = query(TB_SYMBOL_RELATION_CONFLICT);
    CHECK(relation.lookup.result.matches == 2 &&
          relation.lookup.result.second_offset == attributes[1].offset);
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_DATA4);
    (void)query(TB_SYMBOL_RELATION_MALFORMED);
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    attributes[0].value.numeric = false;
    (void)query(TB_SYMBOL_RELATION_RANGE);
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    attributes[0].value.bits = 11;
    (void)query(TB_SYMBOL_RELATION_RANGE);
    attributes[0].value.bits = 200;
    (void)query(TB_SYMBOL_RELATION_RANGE);
    attributes[0].value.bits = UINT64_MAX;
    (void)query(TB_SYMBOL_RELATION_RANGE);
    fixture(TB_SYMBOL_RELATION_SPECIFICATION, TB_SYMBOL_FORM_REF4);
    TbSymbolRelationSource_t selected = source();
    TbSymbolRelationNeed_t need;
    ++selected.reference.source_record;
    CHECK(tb_symbol_relation_need(selected, NULL, &need) == TB_SYMBOL_RELATION_INVALID);
    CHECK(tb_symbol_relation_init(NULL, inventory, tree, selection) == TB_SYMBOL_RELATION_INVALID);
    CHECK(tb_symbol_relation_init(&relation, inventory, tree, selection) == TB_SYMBOL_RELATION_PENDING);
    unsigned char before[sizeof(relation)];
    memcpy(before, &relation, sizeof(relation));
    ++selection.unit_index;
    CHECK(tb_symbol_relation_init(&relation, inventory, tree, selection) == TB_SYMBOL_RELATION_INVALID);
    CHECK(!memcmp(before, &relation, sizeof(relation)));
    --selection.unit_index;
    CHECK(tb_symbol_relation_step(&relation, 0) == TB_SYMBOL_RELATION_INVALID);
    unavailable(&relation);
    CHECK(tb_symbol_relation_init(&relation, inventory, tree, selection) == TB_SYMBOL_RELATION_PENDING);
    CHECK(tb_symbol_relation_step(&relation, 257) == TB_SYMBOL_RELATION_INVALID);
    CHECK(tb_symbol_relation_step(NULL, 1) == TB_SYMBOL_RELATION_INVALID);
    CHECK(!tb_symbol_relation_source(NULL, NULL));
    CHECK(!tb_symbol_relation_diagnostic(NULL, NULL, NULL));
}


static void late_conflict(void)
{
    fixture(TB_SYMBOL_RELATION_ORIGIN, TB_SYMBOL_FORM_REF4);
    const uint64_t count = 100001;
    TbSymbolDieAttribute_t *large = calloc(count, sizeof(*large));
    CHECK(large);
    large[0] = attributes[0];
    for (uint64_t i = 1; i < count; ++i) {
        large[i] = (TbSymbolDieAttribute_t){
            .name          = 0x3f,
            .declared_form = TB_SYMBOL_FORM_FLAG,
            .offset        = 17 + i,
            .value         = {
                .form           = TB_SYMBOL_FORM_FLAG,
                .bytes          = 1,
                .payload_offset = 17 + i,
                .payload_bytes  = 1,
                .bits           = 1,
                .numeric        = true
            }
        };
    }
    large[count - 1] = attributes[0];
    large[count - 1].offset = 17 + count - 1;
    large[count - 1].value.payload_offset = large[count - 1].offset;
    entries[1].attribute_count = count;
    entries[1].end_offset = large[count - 1].offset + 4;
    entries[2].offset = entries[1].end_offset;
    entries[2].end_offset = entries[2].offset + 1;
    entries[2].first_attribute = count;
    large[0].value.bits = entries[2].offset;
    large[count - 1].value.bits = entries[2].offset;
    tree.attributes = large;
    tree.attribute_count = count;
    inventory.count = 1;
    units[0].extent.end_offset = entries[2].end_offset + 64;
    units[0].extent.total_bytes = units[0].extent.end_offset;
    units[0].extent.payload_bytes = units[0].extent.total_bytes - 4;
    units[0].header.extent = units[0].extent;
    tree.backing.bytes = units[0].extent.end_offset + 1024;
    inventory.backing = tree.backing;
    TbSymbolRelation_t relation = query(TB_SYMBOL_RELATION_CONFLICT);
    CHECK(relation.lookup.result.second_index == count - 1);
    free(large);
}


int main(void)
{
    direct_links();
    versioned_members();
    independent_roles_and_edges();
    cross_unit_links();
    unsupported_tags();
    invalid_sources();
    late_conflict();
    puts("PASS scoped direct relationships, explicit dependencies and exact target/tag checks");
    return 0;
}
