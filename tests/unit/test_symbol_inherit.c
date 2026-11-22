#include "symbol/inherit.h"
#include "../support/check.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define NODES 4
#define FIELDS 4

static TbSymbolUnitEntry_t unit;
static TbSymbolUnitsView_t units;
static TbSymbolDieEntry_t entries[NODES];
static TbSymbolDieAttribute_t fields[NODES][FIELDS];
static TbSymbolDieAttribute_t attributes[NODES * FIELDS];
static TbSymbolDiesView_t tree;
static TbSymbolGraphNode_t nodes[NODES];
static TbSymbolGraph_t graph;
static TbSymbolGraphView_t view;
static unsigned supplied;

static void fixture(size_t count)
{
    CHECK(count && count <= NODES);
    memset(entries, 0, sizeof(entries));
    memset(fields, 0, sizeof(fields));
    unit = (TbSymbolUnitEntry_t){
        .extent           = {
            .total_bytes   = 1000,
            .payload_bytes = 996,
            .end_offset    = 1000,
            .length_bytes  = 4,
            .offset_bytes  = 4
        },
        .header           = {
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
        .section_index = 1,
        .kind          = TB_SYMBOL_DWARF_INFO,
        .backing       = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 2000
        }
    };
    for (size_t i = 0; i < count; ++i) {
        entries[i] = (TbSymbolDieEntry_t){
            .offset     = 12 + 32 * i,
            .end_offset = 44 + 32 * i,
            .parent     = UINT64_MAX,
            .code       = 1,
            .tag        = 0x2e
        };
    }
    tree = (TbSymbolDiesView_t){
        .entries        = entries,
        .count          = count,
        .attributes     = attributes,
        .section_index  = 1,
        .kind           = TB_SYMBOL_DWARF_COMPILE,
        .abbrev_section = 2,
        .backing        = units.backing
    };
    supplied = 0;
}


static void add_field(size_t node, uint64_t name, uint64_t form, uint64_t bits)
{
    CHECK(node < tree.count && entries[node].attribute_count < FIELDS);
    uint64_t index = entries[node].attribute_count++;
    fields[node][index] = (TbSymbolDieAttribute_t){
        .name          = name,
        .declared_form = form,
        .offset        = entries[node].offset + 1 + index * 4,
        .value         = {
            .form          = form,
            .bytes         = 4,
            .payload_bytes = 4,
            .bits          = bits,
            .numeric       = true
        }
    };
    fields[node][index].value.payload_offset = fields[node][index].offset;
}


static void link_nodes(size_t from, size_t to, TbSymbolRelationKind_e kind)
{
    add_field(from, kind == TB_SYMBOL_RELATION_SPECIFICATION ? 0x47
                        : kind == TB_SYMBOL_RELATION_ORIGIN ? 0x31 : 0x54,
              TB_SYMBOL_FORM_REF4, entries[to].offset);
}


static void seal(void)
{
    size_t count = 0;
    for (size_t i = 0; i < tree.count; ++i) {
        entries[i].first_attribute = count;
        for (size_t j = 0; j < entries[i].attribute_count; ++j)
            attributes[count++] = fields[i][j];
    }
    tree.attribute_count = count;
    CHECK(tb_symbol_graph_init(&graph, (TbSymbolReferenceTarget_t){
        .backing       = tree.backing,
        .section_index = tree.section_index,
        .record_offset = entries[0].offset
    }, nodes, NODES) == TB_SYMBOL_GRAPH_SOURCE);
    for (unsigned turn = 0; turn < 1000 && graph.status != TB_SYMBOL_GRAPH_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&graph, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE)
                CHECK(tb_symbol_graph_source(&graph, &units, &tree, 0) == TB_SYMBOL_GRAPH_PENDING);
            else
                CHECK(tb_symbol_graph_target(&graph, &units, &tree) == TB_SYMBOL_GRAPH_PENDING);
        }
        (void)tb_symbol_graph_step(&graph, 7);
    }
    CHECK(tb_symbol_graph_view(&graph, &view));
}


static TbSymbolInheritStatus_e run(TbSymbolInherit_t *inherit, size_t slice)
{
    for (unsigned turn = 0; turn < 10000; ++turn) {
        TbSymbolInheritNeed_t need;
        TbSymbolInheritResult_t incomplete;
        CHECK(!tb_symbol_inherit_result(inherit, &incomplete));
        if (tb_symbol_inherit_need(inherit, &need)) {
            ++supplied;
            CHECK(tb_symbol_inherit_source(inherit, &units, &tree, 0) == TB_SYMBOL_INHERIT_PENDING);
        }
        TbSymbolInheritStatus_e status = tb_symbol_inherit_step(inherit, slice);
        if (status != TB_SYMBOL_INHERIT_PENDING && status != TB_SYMBOL_INHERIT_SOURCE)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_INHERIT_INVALID;
}


static void check_chain(void)
{
    fixture(3);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    link_nodes(1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    add_field(2, 0x03, TB_SYMBOL_FORM_STRING, 0);
    seal();
    for (size_t slice = 1; slice <= 256; slice *= 2) {
        TbSymbolInheritSlot_t slots[NODES];
        TbSymbolInherit_t inherit;
        CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
        CHECK(run(&inherit, slice) == TB_SYMBOL_INHERIT_UNIQUE);
        TbSymbolInheritResult_t result;
        CHECK(tb_symbol_inherit_result(&inherit, &result) && result.owner_count == 1);
        CHECK(result.name == 0x03);
        TbSymbolInheritOwner_t owner;
        CHECK(tb_symbol_inherit_owner(&inherit, 2, &owner));
        CHECK(owner.node_index == 2 && owner.selection.record_offset == entries[2].offset);
        CHECK(owner.attribute.name == 0x03 && owner.selection.name == 0x03);
        CHECK(!tb_symbol_inherit_owner(&inherit, 0, &owner));
        CHECK(tb_symbol_inherit_progress(&inherit) > 0);
    }
}


static void check_shared_and_multiple(void)
{
    fixture(4);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    link_nodes(1, 3, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(2, 3, TB_SYMBOL_RELATION_SPECIFICATION);
    add_field(3, 0x3a, TB_SYMBOL_FORM_DATA4, 5);
    seal();
    TbSymbolInheritSlot_t slots[NODES];
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x3a, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_UNIQUE);
    TbSymbolInheritOwner_t owner;
    CHECK(tb_symbol_inherit_owner(&inherit, 2, &owner));
    CHECK(owner.selection.record_offset == entries[3].offset && owner.attribute.value.bits == 5);
    fixture(3);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    add_field(1, 0x3a, TB_SYMBOL_FORM_DATA4, 5);
    add_field(2, 0x3a, TB_SYMBOL_FORM_DATA4, 5);
    seal();
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x3a, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(run(&inherit, 256) == TB_SYMBOL_INHERIT_MULTIPLE);
    TbSymbolInheritResult_t result;
    CHECK(tb_symbol_inherit_result(&inherit, &result) && result.owner_count == 2);
    CHECK(tb_symbol_inherit_owner(&inherit, 1, &owner) && owner.attribute.value.bits == 5);
    CHECK(tb_symbol_inherit_owner(&inherit, 2, &owner) && owner.attribute.value.bits == 5);
}


static void check_override_and_absence(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    add_field(0, 0x03, TB_SYMBOL_FORM_STRING, 0);
    add_field(1, 0x03, TB_SYMBOL_FORM_STRING, 9);
    seal();
    TbSymbolInheritSlot_t slots[NODES];
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_UNIQUE && supplied == 1);
    TbSymbolInheritOwner_t owner;
    CHECK(tb_symbol_inherit_owner(&inherit, 0, &owner) && !owner.attribute.value.bits);
    CHECK(!tb_symbol_inherit_owner(&inherit, 1, &owner));
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x49, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_ABSENT);
    TbSymbolInheritResult_t result;
    CHECK(tb_symbol_inherit_result(&inherit, &result) && !result.owner_count);
}


static void check_conflict_and_dependency(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    add_field(1, 0x03, TB_SYMBOL_FORM_STRING, 8);
    add_field(1, 0x03, TB_SYMBOL_FORM_STRING, 8);
    seal();
    TbSymbolInheritSlot_t slots[NODES];
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_CONFLICT);
    TbSymbolInheritFailure_t failure;
    CHECK(tb_symbol_inherit_failure(&inherit, &failure) && failure.selection_available);
    CHECK(failure.selection.matches == 2 && failure.record.record_offset == entries[1].offset);
    TbSymbolInheritResult_t result;
    TbSymbolInheritOwner_t owner;
    CHECK(!tb_symbol_inherit_result(&inherit, &result));
    CHECK(!tb_symbol_inherit_owner(&inherit, 1, &owner));
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(tb_symbol_inherit_step(&inherit, 256) == TB_SYMBOL_INHERIT_SOURCE);
    CHECK(tb_symbol_inherit_source(&inherit, NULL, &tree, 0) == TB_SYMBOL_INHERIT_DEPENDENCY);
    CHECK(tb_symbol_inherit_failure(&inherit, &failure) && !failure.selection_available);
}


static void check_late_dependency(void)
{
    fixture(3);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    add_field(1, 0x03, TB_SYMBOL_FORM_STRING, 8);
    add_field(2, 0x03, TB_SYMBOL_FORM_STRING, 9);
    seal();
    TbSymbolInheritSlot_t slots[NODES];
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    bool failed = false;
    for (unsigned turn = 0; turn < 1000 && !failed; ++turn) {
        TbSymbolInheritNeed_t need;
        if (tb_symbol_inherit_need(&inherit, &need)) {
            if (need.node_index == 2) {
                CHECK(slots[1].visit == TB_SYMBOL_INHERIT_OWNER);
                CHECK(tb_symbol_inherit_source(&inherit, NULL, NULL, 0) == TB_SYMBOL_INHERIT_DEPENDENCY);
                failed = true;
            } else {
                CHECK(tb_symbol_inherit_source(&inherit, &units, &tree, 0) == TB_SYMBOL_INHERIT_PENDING);
            }
        }
        if (!failed)
            (void)tb_symbol_inherit_step(&inherit, 1);
    }
    CHECK(failed);
    TbSymbolInheritResult_t result;
    TbSymbolInheritOwner_t owner;
    CHECK(!tb_symbol_inherit_result(&inherit, &result));
    CHECK(!tb_symbol_inherit_owner(&inherit, 1, &owner));
}


static void check_invalid(void)
{
    fixture(1);
    seal();
    TbSymbolInheritSlot_t slots[NODES];
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    TbSymbolInherit_t saved = inherit;
    for (uint64_t name = 0; name < 0x100; ++name) {
        if (name != 0x03 && name != 0x6e && name != 0x49 && name != 0x3a && name != 0x3b &&
            name != 0x39 && name != 0x64) {
            CHECK(tb_symbol_inherit_init(&inherit, view, name, slots, NODES) ==
                  TB_SYMBOL_INHERIT_UNSUPPORTED);
            CHECK(!memcmp(&saved, &inherit, sizeof(inherit)));
        }
    }
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, 0) == TB_SYMBOL_INHERIT_RESOURCE);
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, SIZE_MAX / sizeof(*slots) + 1) ==
          TB_SYMBOL_INHERIT_INVALID);
    CHECK(!memcmp(&saved, &inherit, sizeof(inherit)));
    CHECK(tb_symbol_inherit_step(&inherit, 0) == TB_SYMBOL_INHERIT_INVALID);
    CHECK(tb_symbol_inherit_step(&inherit, 257) == TB_SYMBOL_INHERIT_INVALID);
    CHECK(tb_symbol_inherit_step(&inherit, 256) == TB_SYMBOL_INHERIT_SOURCE);
    saved = inherit;
    TbSymbolDiesView_t wrong = tree;
    ++wrong.backing.key.inode;
    CHECK(tb_symbol_inherit_source(&inherit, &units, &wrong, 0) == TB_SYMBOL_INHERIT_INVALID);
    CHECK(!memcmp(&saved, &inherit, sizeof(inherit)));
    CHECK(tb_symbol_inherit_source(&inherit, &units, &tree, 1) == TB_SYMBOL_INHERIT_INVALID);
    ++unit.header.version;
    CHECK(tb_symbol_inherit_source(&inherit, &units, &tree, 0) == TB_SYMBOL_INHERIT_INVALID);
    --unit.header.version;
    CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_ABSENT);
}


static void check_cross_unit(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    add_field(1, 0x3a, TB_SYMBOL_FORM_DATA4, 7);
    fields[0][0].declared_form = TB_SYMBOL_FORM_REF_ADDR;
    fields[0][0].value.form = TB_SYMBOL_FORM_REF_ADDR;
    fields[0][0].value.bits = 512;
    size_t cursor = 0;
    for (size_t i = 0; i < 2; ++i) {
        entries[i].first_attribute = cursor;
        for (size_t j = 0; j < entries[i].attribute_count; ++j)
            attributes[cursor++] = fields[i][j];
    }
    tree.attribute_count = cursor;
    TbSymbolDieEntry_t remote_entry = entries[1];
    remote_entry.offset = 512;
    remote_entry.end_offset = 520;
    attributes[remote_entry.first_attribute].offset = 513;
    attributes[remote_entry.first_attribute].value.payload_offset = 513;
    TbSymbolDiesView_t remote = tree;
    remote.entries = &remote_entry;
    remote.count = 1;
    remote.unit_offset = 500;
    remote.abbrev_offset = 100;
    TbSymbolUnitEntry_t headers[2] = {unit, unit};
    headers[0].extent.end_offset = 500;
    headers[0].extent.total_bytes = 500;
    headers[0].extent.payload_bytes = 496;
    headers[0].header.extent = headers[0].extent;
    headers[1].offset = 500;
    headers[1].extent = headers[0].extent;
    headers[1].extent.end_offset = 1000;
    headers[1].header.extent = headers[1].extent;
    TbSymbolUnitsView_t inventory = units;
    inventory.entries = headers;
    inventory.count = 2;
    CHECK(tb_symbol_graph_init(&graph, (TbSymbolReferenceTarget_t){
        .backing       = tree.backing,
        .section_index = tree.section_index,
        .record_offset = entries[0].offset
    }, nodes, NODES) == TB_SYMBOL_GRAPH_SOURCE);
    for (unsigned turn = 0; turn < 1000 && graph.status != TB_SYMBOL_GRAPH_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&graph, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE) {
                bool foreign = need.record.unit_offset == 500;
                CHECK(tb_symbol_graph_source(&graph, &inventory, foreign ? &remote : &tree,
                                              foreign ? 1 : 0) == TB_SYMBOL_GRAPH_PENDING);
            } else {
                CHECK(tb_symbol_graph_target(&graph, &inventory, &remote) == TB_SYMBOL_GRAPH_PENDING);
            }
        }
        (void)tb_symbol_graph_step(&graph, 7);
    }
    CHECK(tb_symbol_graph_view(&graph, &view));
    TbSymbolInheritSlot_t slots[NODES];
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x3a, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    for (unsigned turn = 0; turn < 1000 && inherit.status != TB_SYMBOL_INHERIT_UNIQUE; ++turn) {
        TbSymbolInheritNeed_t need;
        if (tb_symbol_inherit_need(&inherit, &need)) {
            bool foreign = need.record.unit_offset == 500;
            CHECK(tb_symbol_inherit_source(&inherit, &inventory, foreign ? &remote : &tree,
                                            foreign ? 1 : 0) == TB_SYMBOL_INHERIT_PENDING);
        }
        TbSymbolInheritStatus_e status = tb_symbol_inherit_step(&inherit, 1);
        CHECK(status == TB_SYMBOL_INHERIT_PENDING || status == TB_SYMBOL_INHERIT_SOURCE ||
              status == TB_SYMBOL_INHERIT_UNIQUE);
    }
    TbSymbolInheritOwner_t owner;
    CHECK(tb_symbol_inherit_owner(&inherit, 1, &owner));
    CHECK(owner.selection.unit_offset == 500 && owner.selection.record_offset == 512);
    CHECK(owner.attribute.value.bits == 7 && owner.attribute.name == 0x3a);
    CHECK(nodes[0].record.unit_offset == 0 && nodes[1].context.abbrev_offset == 100);
}


static void check_namespace(void)
{
    fixture(2);
    entries[0].tag = 0x39;
    entries[1].tag = 0x39;
    link_nodes(0, 1, TB_SYMBOL_RELATION_EXTENSION);
    add_field(1, 0x03, TB_SYMBOL_FORM_STRING, 42);
    seal();
    TbSymbolInheritSlot_t slots[NODES];
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_UNIQUE);
    TbSymbolInheritOwner_t owner;
    CHECK(tb_symbol_inherit_owner(&inherit, 1, &owner) && owner.attribute.value.bits == 42);
    nodes[0].complete = false;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(tb_symbol_inherit_step(&inherit, 1) == TB_SYMBOL_INHERIT_INVALID);
    TbSymbolInheritResult_t result;
    CHECK(!tb_symbol_inherit_result(&inherit, &result));
    CHECK(!tb_symbol_inherit_owner(&inherit, 1, &owner));
    TbSymbolInheritFailure_t failure;
    CHECK(tb_symbol_inherit_failure(&inherit, &failure) && !failure.selection_available);
    CHECK(failure.record.record_offset == entries[0].offset);
}


static void check_legacy_kind(void)
{
    const TbSymbolDwarfUnitKind_e kinds[] = {TB_SYMBOL_DWARF_COMPILE, TB_SYMBOL_DWARF_PARTIAL};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        fixture(2);
        unit.header.version = 4;
        unit.header.kind = TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
        tree.kind = kinds[i];
        link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
        add_field(1, 0x03, TB_SYMBOL_FORM_STRING, 42);
        seal();
        TbSymbolInheritSlot_t slots[NODES];
        TbSymbolInherit_t inherit;
        CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
        CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_UNIQUE);
        TbSymbolInheritOwner_t owner;
        CHECK(tb_symbol_inherit_owner(&inherit, 1, &owner));
        CHECK(owner.attribute.value.bits == 42 && nodes[1].context.unit_kind == kinds[i]);
    }
    fixture(1);
    seal();
    TbSymbolInheritSlot_t slots[NODES];
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
    CHECK(tb_symbol_inherit_step(&inherit, 256) == TB_SYMBOL_INHERIT_SOURCE);
    unit.header.kind = TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL;
    CHECK(tb_symbol_inherit_source(&inherit, &units, &tree, 0) == TB_SYMBOL_INHERIT_INVALID);
    unit.header.kind = TB_SYMBOL_DWARF_TYPE;
    CHECK(tb_symbol_inherit_source(&inherit, &units, &tree, 0) == TB_SYMBOL_INHERIT_INVALID);
    unit.header.kind = TB_SYMBOL_DWARF_COMPILE;
    CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_ABSENT);
}


static void check_state_bounds(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    seal();
    for (unsigned variant = 0; variant < 3; ++variant) {
        TbSymbolInheritSlot_t slots[NODES];
        TbSymbolInherit_t inherit;
        CHECK(tb_symbol_inherit_init(&inherit, view, 0x03, slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
        CHECK(tb_symbol_inherit_step(&inherit, 256) == TB_SYMBOL_INHERIT_SOURCE);
        CHECK(tb_symbol_inherit_source(&inherit, &units, &tree, 0) == TB_SYMBOL_INHERIT_PENDING);
        for (unsigned turn = 0; turn < 10 && slots[0].visit != TB_SYMBOL_INHERIT_EXAMINED; ++turn)
            CHECK(tb_symbol_inherit_step(&inherit, 1) == TB_SYMBOL_INHERIT_PENDING);
        CHECK(slots[0].visit == TB_SYMBOL_INHERIT_EXAMINED);
        /* Fault injection: private traversal state must not escape its arrays. */
        if (variant == 0)
            inherit.edge = TB_SYMBOL_GRAPH_RELATIONS + 1;
        else if (variant == 1)
            inherit.edge = UINT_MAX;
        else
            inherit.scan = view.count + 1;
        CHECK(tb_symbol_inherit_step(&inherit, 1) == TB_SYMBOL_INHERIT_INVALID);
        TbSymbolInheritResult_t result;
        CHECK(!tb_symbol_inherit_result(&inherit, &result));
        TbSymbolInheritFailure_t failure;
        CHECK(tb_symbol_inherit_failure(&inherit, &failure) && !failure.selection_available);
        CHECK(failure.record.record_offset == entries[0].offset);
    }
}


static void check_fields(void)
{
    const uint64_t names[] = {0x03, 0x6e, 0x49, 0x64, 0x3a, 0x3b, 0x39};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        fixture(2);
        link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
        add_field(1, names[i], TB_SYMBOL_FORM_DATA4, 42);
        seal();
        TbSymbolInheritSlot_t slots[NODES];
        TbSymbolInherit_t inherit;
        CHECK(tb_symbol_inherit_init(&inherit, view, names[i], slots, NODES) == TB_SYMBOL_INHERIT_PENDING);
        CHECK(run(&inherit, 1) == TB_SYMBOL_INHERIT_UNIQUE);
        TbSymbolInheritOwner_t owner;
        CHECK(tb_symbol_inherit_owner(&inherit, 1, &owner));
        CHECK(owner.attribute.name == names[i] && owner.attribute.value.bits == 42);
        CHECK(owner.attribute.value.form == TB_SYMBOL_FORM_DATA4);
    }
}


static void check_deep(void)
{
    const size_t count = 513;
    TbSymbolDieEntry_t *records = calloc(count, sizeof(*records));
    TbSymbolDieAttribute_t *attrs = calloc(count, sizeof(*attrs));
    TbSymbolGraphNode_t *graph_nodes = malloc(count * sizeof(*graph_nodes));
    TbSymbolInheritSlot_t *slots = malloc(count * sizeof(*slots));
    CHECK(records && attrs && graph_nodes && slots);
    fixture(1);
    TbSymbolDiesView_t deep = tree;
    deep.entries = records;
    deep.count = count;
    deep.attributes = attrs;
    deep.attribute_count = count;
    deep.backing.bytes = 100000;
    TbSymbolUnitsView_t inventory = units;
    inventory.backing = deep.backing;
    TbSymbolUnitEntry_t header = unit;
    header.extent.end_offset = 100000;
    header.extent.total_bytes = 100000;
    header.extent.payload_bytes = 99996;
    header.header.extent = header.extent;
    inventory.entries = &header;
    for (size_t i = 0; i < count; ++i) {
        records[i] = (TbSymbolDieEntry_t){
            .offset          = 12 + i * 8,
            .end_offset      = 20 + i * 8,
            .parent          = UINT64_MAX,
            .code            = 1,
            .tag             = 0x2e,
            .first_attribute = i,
            .attribute_count = 1
        };
        attrs[i] = (TbSymbolDieAttribute_t){
            .name          = i + 1 == count ? 0x03 : 0x47,
            .declared_form = i + 1 == count ? TB_SYMBOL_FORM_DATA4 : TB_SYMBOL_FORM_REF4,
            .offset        = records[i].offset + 1,
            .value         = {
                .form          = i + 1 == count ? TB_SYMBOL_FORM_DATA4 : TB_SYMBOL_FORM_REF4,
                .bytes         = 4,
                .payload_bytes = 4,
                .bits          = i + 1 == count ? 42 : records[i].end_offset,
                .numeric       = true
            }
        };
        attrs[i].value.payload_offset = attrs[i].offset;
    }
    TbSymbolGraph_t traversal;
    CHECK(tb_symbol_graph_init(&traversal, (TbSymbolReferenceTarget_t){
        .backing       = deep.backing,
        .section_index = deep.section_index,
        .record_offset = records[0].offset
    }, graph_nodes, count) == TB_SYMBOL_GRAPH_SOURCE);
    for (unsigned turn = 0; turn < 1000000 && traversal.status != TB_SYMBOL_GRAPH_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&traversal, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE)
                CHECK(tb_symbol_graph_source(&traversal, &inventory, &deep, 0) == TB_SYMBOL_GRAPH_PENDING);
            else
                CHECK(tb_symbol_graph_target(&traversal, &inventory, &deep) == TB_SYMBOL_GRAPH_PENDING);
        }
        (void)tb_symbol_graph_step(&traversal, 1);
    }
    TbSymbolGraphView_t completed;
    CHECK(tb_symbol_graph_view(&traversal, &completed) && completed.count == count);
    TbSymbolInherit_t inherit;
    CHECK(tb_symbol_inherit_init(&inherit, completed, 0x03, slots, count) == TB_SYMBOL_INHERIT_PENDING);
    for (unsigned turn = 0; turn < 1000000 && inherit.status != TB_SYMBOL_INHERIT_UNIQUE; ++turn) {
        TbSymbolInheritNeed_t need;
        if (tb_symbol_inherit_need(&inherit, &need))
            CHECK(tb_symbol_inherit_source(&inherit, &inventory, &deep, 0) == TB_SYMBOL_INHERIT_PENDING);
        TbSymbolInheritStatus_e status = tb_symbol_inherit_step(&inherit, 1);
        CHECK(status == TB_SYMBOL_INHERIT_PENDING || status == TB_SYMBOL_INHERIT_SOURCE ||
              status == TB_SYMBOL_INHERIT_UNIQUE);
    }
    TbSymbolInheritOwner_t owner;
    CHECK(tb_symbol_inherit_owner(&inherit, count - 1, &owner));
    CHECK(owner.attribute.value.bits == 42 && owner.selection.record_offset == records[count - 1].offset);
    CHECK(tb_symbol_inherit_progress(&inherit) > count * count / 2);
    free(slots);
    free(graph_nodes);
    free(attrs);
    free(records);
}


int main(void)
{
    check_chain();
    check_shared_and_multiple();
    check_override_and_absence();
    check_conflict_and_dependency();
    check_late_dependency();
    check_invalid();
    check_cross_unit();
    check_namespace();
    check_legacy_kind();
    check_state_bounds();
    check_fields();
    check_deep();
    puts("PASS scoped inherited attribute owners, shared paths, conflicts and explicit dependencies");
    return 0;
}
