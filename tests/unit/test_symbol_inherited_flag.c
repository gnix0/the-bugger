#include "symbol/inherited_flag.h"
#include "../support/check.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define NODES 520
#define FIELDS 4

static TbSymbolUnitEntry_t    unit;
static TbSymbolUnitsView_t    units;
static TbSymbolDieEntry_t     entries[NODES];
static TbSymbolDieAttribute_t fields[NODES][FIELDS];
static TbSymbolDieAttribute_t attributes[NODES * FIELDS];
static TbSymbolDiesView_t     tree;
static TbSymbolGraphNode_t    nodes[NODES];
static TbSymbolGraph_t        graph;
static TbSymbolGraphView_t    view;
static unsigned               supplied;

static void fixture(size_t count)
{
    CHECK(count && count <= NODES);
    memset(entries, 0, sizeof(entries));
    memset(fields, 0, sizeof(fields));
    unit = (TbSymbolUnitEntry_t){
        .extent           = {
            .total_bytes   = 1024 + 64 * count,
            .payload_bytes = 1020 + 64 * count,
            .end_offset    = 1024 + 64 * count,
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
    units.backing.bytes = 2048 + 128 * count;
    tree.backing        = units.backing;
    supplied            = 0;
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
    for (unsigned turn = 0; turn < 1000000 && graph.status != TB_SYMBOL_GRAPH_READY; ++turn) {
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


static TbSymbolInheritedFlag_t flag;
static TbSymbolInheritedFlagSlot_t slots[NODES];

static TbSymbolInheritedFlagStatus_e run(void)
{
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolInheritNeed_t need;
        if (tb_symbol_inherited_flag_need(&flag, &need)) {
            ++supplied;
            CHECK(need.name == TB_SYMBOL_FLAG_ARTIFICIAL);
            CHECK(tb_symbol_inherited_flag_source(&flag, &units, &tree, 0) == TB_SYMBOL_FLAG_PENDING);
        }
        TbSymbolInheritedFlagStatus_e status = tb_symbol_inherited_flag_step(&flag, 1);
        if (status != TB_SYMBOL_FLAG_PENDING && status != TB_SYMBOL_FLAG_SOURCE)
            return status;
        CHECK(!tb_symbol_inherited_flag_view(&flag, &(TbSymbolInheritedFlagView_t){0}));
    }
    CHECK(false);
    return TB_SYMBOL_FLAG_INVALID;
}


static void start(void)
{
    seal();
    CHECK(tb_symbol_inherited_flag_init(&flag, view, TB_SYMBOL_FLAG_ARTIFICIAL, slots, NODES) ==
          TB_SYMBOL_FLAG_PENDING);
}


static void check_direct(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 1);
    add_field(1, 0x34, TB_SYMBOL_FORM_DATA1, 42);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_FOUND && supplied == 1);
    TbSymbolInheritedFlagView_t result;
    CHECK(tb_symbol_inherited_flag_view(&flag, &result));
    CHECK(result.value_available && result.value && result.owner_count == 1);
    CHECK(result.slots[1].visit == TB_SYMBOL_FLAG_UNVISITED);
    TbSymbolInheritOwner_t owner;
    CHECK(tb_symbol_inherited_flag_owner(&flag, 0, &owner));
    CHECK(owner.attribute.value.bits == 1);
    CHECK(!tb_symbol_inherited_flag_owner(&flag, 1, &owner));

    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_ABSENT && supplied == 2);
    CHECK(tb_symbol_inherited_flag_view(&flag, &result));
    CHECK(!result.value_available && result.owner_count == 0);
}


static void check_owners(void)
{
    fixture(3);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    add_field(1, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    add_field(2, 0x34, TB_SYMBOL_FORM_FLAG, 255);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_FOUND);
    TbSymbolInheritedFlagView_t result;
    CHECK(tb_symbol_inherited_flag_view(&flag, &result));
    CHECK(result.value_available && result.value && result.owner_count == 2);
    TbSymbolInheritOwner_t one;
    TbSymbolInheritOwner_t two;
    CHECK(tb_symbol_inherited_flag_owner(&flag, 1, &one));
    CHECK(tb_symbol_inherited_flag_owner(&flag, 2, &two));
    CHECK(one.attribute.value.form != two.attribute.value.form);

    fixture(3);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    add_field(1, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    add_field(2, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_FOUND);
    CHECK(tb_symbol_inherited_flag_view(&flag, &result) && result.owner_count == 1 && result.value);
    CHECK(result.slots[2].visit == TB_SYMBOL_FLAG_EXAMINED);
    CHECK(result.slots[2].selection.matches == 1 && result.slots[2].attribute.value.bits == 0);

    fixture(4);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    link_nodes(1, 3, TB_SYMBOL_RELATION_ORIGIN);
    link_nodes(2, 3, TB_SYMBOL_RELATION_SPECIFICATION);
    add_field(3, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_FOUND);
    CHECK(tb_symbol_inherited_flag_view(&flag, &result) && result.owner_count == 1);
}


static void check_failures(void)
{
    fixture(1);
    add_field(0, 0x34, TB_SYMBOL_FORM_DATA1, 1);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_MALFORMED);
    TbSymbolInheritedFlagFailure_t failure;
    CHECK(tb_symbol_inherited_flag_failure(&flag, &failure) && failure.attribute_available);
    CHECK(failure.attribute.value.form == TB_SYMBOL_FORM_DATA1);

    fixture(1);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_CONFLICT);
    CHECK(tb_symbol_inherited_flag_failure(&flag, &failure));
    CHECK(failure.selection_available && failure.selection.matches == 2);

    fixture(1);
    start();
    while (tb_symbol_inherited_flag_step(&flag, 1) == TB_SYMBOL_FLAG_PENDING) {}
    CHECK(flag.status == TB_SYMBOL_FLAG_SOURCE);
    TbSymbolUnitsView_t wrong = units;
    ++wrong.backing.key.inode;
    unsigned char before[sizeof(flag)];
    memcpy(before, &flag, sizeof(before));
    CHECK(tb_symbol_inherited_flag_source(&flag, &wrong, &tree, 0) == TB_SYMBOL_FLAG_INVALID);
    CHECK(!memcmp(before, &flag, sizeof(before)));
    CHECK(tb_symbol_inherited_flag_init(&flag, view, 0x3c, slots, NODES) == TB_SYMBOL_FLAG_UNSUPPORTED);
    CHECK(!memcmp(before, &flag, sizeof(before)));
    CHECK(tb_symbol_inherited_flag_source(&flag, NULL, NULL, 0) == TB_SYMBOL_FLAG_DEPENDENCY);
    CHECK(tb_symbol_inherited_flag_failure(&flag, &failure) &&
          failure.reason == TB_SYMBOL_FLAG_FAILURE_SOURCE);
}


static void check_value_classes(void)
{
    const uint64_t names[] = {0x34, 0x27, 0x77, 0x78};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        fixture(1);
        add_field(0, names[i], TB_SYMBOL_FORM_FLAG_PRESENT, 1);
        seal();
        CHECK(tb_symbol_inherited_flag_init(&flag, view, names[i], slots, NODES) == TB_SYMBOL_FLAG_PENDING);
        for (unsigned step = 0; step < 1000; ++step) {
            TbSymbolInheritNeed_t need;
            if (tb_symbol_inherited_flag_need(&flag, &need)) {
                CHECK(need.name == names[i]);
                CHECK(tb_symbol_inherited_flag_source(&flag, &units, &tree, 0) == TB_SYMBOL_FLAG_PENDING);
            }
            TbSymbolInheritedFlagStatus_e status = tb_symbol_inherited_flag_step(&flag, 1);
            if (status != TB_SYMBOL_FLAG_SOURCE && status != TB_SYMBOL_FLAG_PENDING)
                break;
        }
        CHECK(flag.status == TB_SYMBOL_FLAG_FOUND);
        TbSymbolInheritedFlagView_t result;
        CHECK(tb_symbol_inherited_flag_view(&flag, &result) && result.value && result.name == names[i]);
    }
    const uint64_t bad_bits[] = {0, 2, UINT64_MAX};
    for (size_t i = 0; i < sizeof(bad_bits) / sizeof(*bad_bits); ++i) {
        fixture(1);
        add_field(0, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, bad_bits[i]);
        start();
        CHECK(run() == TB_SYMBOL_FLAG_MALFORMED);
    }
    fixture(1);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 256);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_MALFORMED);
    fixture(1);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 1);
    fields[0][0].value.is_signed = true;
    start();
    CHECK(run() == TB_SYMBOL_FLAG_MALFORMED);
}


static void check_zero_presence(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    add_field(1, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_FOUND && supplied == 2);
    TbSymbolInheritedFlagView_t result;
    CHECK(tb_symbol_inherited_flag_view(&flag, &result));
    CHECK(result.value && result.value_available && result.owner_count == 1);
    CHECK(result.slots[0].visit == TB_SYMBOL_FLAG_EXAMINED);
    CHECK(result.slots[0].selection.matches == 1 && result.slots[0].attribute.value.bits == 0);
    TbSymbolInheritOwner_t owner;
    CHECK(!tb_symbol_inherited_flag_owner(&flag, 0, &owner));
    CHECK(tb_symbol_inherited_flag_owner(&flag, 1, &owner));

    fixture(1);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_ABSENT);
    CHECK(tb_symbol_inherited_flag_view(&flag, &result) && !result.value_available);
    CHECK(result.slots[0].visit == TB_SYMBOL_FLAG_EXAMINED && result.slots[0].selection.matches == 1);

    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    add_field(1, 0x34, TB_SYMBOL_FORM_DATA1, 1);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_MALFORMED);
    TbSymbolInheritedFlagFailure_t failure;
    CHECK(tb_symbol_inherited_flag_failure(&flag, &failure));
    CHECK(failure.node_index == 1 && failure.attribute_available);
    CHECK(!tb_symbol_inherited_flag_view(&flag, &result));

    fixture(1);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_CONFLICT);
    CHECK(tb_symbol_inherited_flag_failure(&flag, &failure) && failure.selection.matches == 2);
}


static void check_depth_and_bounds(void)
{
    fixture(512);
    for (size_t i = 0; i + 1 < 512; ++i) {
        link_nodes(i, i + 1, TB_SYMBOL_RELATION_ORIGIN);
        add_field(i, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    }
    add_field(511, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_FOUND && supplied == 512);
    TbSymbolInheritedFlagView_t result;
    CHECK(tb_symbol_inherited_flag_view(&flag, &result) && result.owner_count == 1);
    for (size_t i = 0; i + 1 < 512; ++i) {
        CHECK(result.slots[i].visit == TB_SYMBOL_FLAG_EXAMINED);
        CHECK(result.slots[i].selection.matches == 1 && result.slots[i].attribute.value.bits == 0);
    }
    unsigned char before[sizeof(flag)];
    memcpy(before, &flag, sizeof(before));
    CHECK(tb_symbol_inherited_flag_init(&flag, view, 0x34, slots, 511) == TB_SYMBOL_FLAG_RESOURCE);
    CHECK(!memcmp(before, &flag, sizeof(before)));
    CHECK(tb_symbol_inherited_flag_init(&flag, view, 0x34, slots, SIZE_MAX / sizeof(*slots) + 1) ==
          TB_SYMBOL_FLAG_INVALID);
    CHECK(!memcmp(before, &flag, sizeof(before)));
    CHECK(tb_symbol_inherited_flag_step(&flag, 0) == TB_SYMBOL_FLAG_INVALID);
    CHECK(tb_symbol_inherited_flag_step(&flag, 257) == TB_SYMBOL_FLAG_INVALID);
    CHECK(!memcmp(before, &flag, sizeof(before)));
    TbSymbolGraphView_t invalid = view;
    invalid.count = SIZE_MAX / sizeof(*invalid.nodes) + 1;
    CHECK(tb_symbol_inherited_flag_init(&flag, invalid, 0x34, slots, NODES) == TB_SYMBOL_FLAG_INVALID);
    CHECK(!memcmp(before, &flag, sizeof(before)));
}


static void check_restart(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    add_field(1, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    start();
    CHECK(run() == TB_SYMBOL_FLAG_FOUND);
    uint64_t total = tb_symbol_inherited_flag_progress(&flag);
    TbSymbolGraphNode_t original[2];
    memcpy(original, nodes, sizeof(original));
    for (uint64_t cancel = 0; cancel <= total; ++cancel) {
        CHECK(tb_symbol_inherited_flag_init(&flag, view, 0x34, slots, NODES) == TB_SYMBOL_FLAG_PENDING);
        for (uint64_t step = 0; step < cancel; ++step) {
            TbSymbolInheritNeed_t need;
            if (tb_symbol_inherited_flag_need(&flag, &need))
                CHECK(tb_symbol_inherited_flag_source(&flag, &units, &tree, 0) == TB_SYMBOL_FLAG_PENDING);
            (void)tb_symbol_inherited_flag_step(&flag, 1);
        }
        TbSymbolInheritedFlagView_t result;
        CHECK(tb_symbol_inherited_flag_view(&flag, &result) == (cancel == total));
        CHECK(!memcmp(original, nodes, sizeof(original)));
        memset(&flag, 0xa5, sizeof(flag));
    }
    CHECK(tb_symbol_inherited_flag_init(&flag, view, 0x34, slots, NODES) == TB_SYMBOL_FLAG_PENDING);
    flag.progress = UINT64_MAX;
    CHECK(tb_symbol_inherited_flag_step(&flag, 1) == TB_SYMBOL_FLAG_INVALID);
    TbSymbolInheritedFlagFailure_t failure;
    CHECK(tb_symbol_inherited_flag_failure(&flag, &failure));
    CHECK(failure.reason == TB_SYMBOL_FLAG_FAILURE_PROGRESS);
}


static void check_graph_failures(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    start();
    nodes[1].complete = false;
    CHECK(run() == TB_SYMBOL_FLAG_INVALID);
    TbSymbolInheritedFlagFailure_t failure;
    CHECK(tb_symbol_inherited_flag_failure(&flag, &failure));
    CHECK(failure.reason == TB_SYMBOL_FLAG_FAILURE_SCOPE && failure.node_index == 1);

    for (unsigned mismatch = 0; mismatch < 2; ++mismatch) {
        fixture(2);
        link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
        start();
        if (mismatch)
            ++nodes[0].edges[TB_SYMBOL_RELATION_ORIGIN].target.entry_index;
        else
            ++nodes[0].edges[TB_SYMBOL_RELATION_ORIGIN].target.record_offset;
        CHECK(run() == TB_SYMBOL_FLAG_INVALID);
        CHECK(tb_symbol_inherited_flag_failure(&flag, &failure));
        CHECK(failure.reason == TB_SYMBOL_FLAG_FAILURE_EDGE && failure.node_index == 0);
        TbSymbolInheritedFlagView_t result;
        memset(&result, 0xa5, sizeof(result));
        unsigned char before[sizeof(result)];
        memcpy(before, &result, sizeof(before));
        CHECK(!tb_symbol_inherited_flag_view(&flag, &result));
        CHECK(!memcmp(before, &result, sizeof(before)));
    }
}


static void check_cross_unit(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    add_field(0, 0x34, TB_SYMBOL_FORM_FLAG, 0);
    add_field(1, 0x34, TB_SYMBOL_FORM_FLAG_PRESENT, 1);
    fields[0][0].declared_form = TB_SYMBOL_FORM_REF_ADDR;
    fields[0][0].value.form    = TB_SYMBOL_FORM_REF_ADDR;
    fields[0][0].value.bits    = 512;
    size_t cursor = 0;
    for (size_t i = 0; i < 2; ++i) {
        entries[i].first_attribute = cursor;
        for (size_t j = 0; j < entries[i].attribute_count; ++j)
            attributes[cursor++] = fields[i][j];
    }
    tree.attribute_count = cursor;
    TbSymbolDieEntry_t remote_entry = entries[1];
    remote_entry.offset     = 512;
    remote_entry.end_offset = 520;
    attributes[remote_entry.first_attribute].offset = 513;
    attributes[remote_entry.first_attribute].value.payload_offset = 513;
    TbSymbolDiesView_t remote = tree;
    remote.entries       = &remote_entry;
    remote.count         = 1;
    remote.unit_offset   = 500;
    remote.abbrev_offset = 100;
    TbSymbolUnitEntry_t headers[2] = {unit, unit};
    headers[0].extent.end_offset    = 500;
    headers[0].extent.total_bytes   = 500;
    headers[0].extent.payload_bytes = 496;
    headers[0].header.extent        = headers[0].extent;
    headers[1].offset               = 500;
    headers[1].extent               = headers[0].extent;
    headers[1].extent.end_offset    = 1000;
    headers[1].header.extent        = headers[1].extent;
    TbSymbolUnitsView_t inventory = units;
    inventory.entries = headers;
    inventory.count   = 2;
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
    TbSymbolInheritedFlagSlot_t local_slots[NODES];
    TbSymbolInheritedFlag_t     inherited;
    CHECK(tb_symbol_inherited_flag_init(&inherited, view, 0x34, local_slots, NODES) ==
          TB_SYMBOL_FLAG_PENDING);
    for (unsigned turn = 0; turn < 1000 && inherited.status != TB_SYMBOL_FLAG_FOUND; ++turn) {
        TbSymbolInheritNeed_t need;
        if (tb_symbol_inherited_flag_need(&inherited, &need)) {
            bool foreign = need.record.unit_offset == 500;
            CHECK(tb_symbol_inherited_flag_source(&inherited, &inventory, foreign ? &remote : &tree,
                                                  foreign ? 1 : 0) == TB_SYMBOL_FLAG_PENDING);
        }
        TbSymbolInheritedFlagStatus_e status = tb_symbol_inherited_flag_step(&inherited, 1);
        CHECK(status == TB_SYMBOL_FLAG_PENDING || status == TB_SYMBOL_FLAG_SOURCE ||
              status == TB_SYMBOL_FLAG_FOUND);
    }
    TbSymbolInheritOwner_t owner;
    CHECK(tb_symbol_inherited_flag_owner(&inherited, 1, &owner));
    CHECK(owner.selection.unit_offset == 500 && owner.selection.record_offset == 512);
    CHECK(owner.attribute.value.bits == 1 && owner.attribute.name == 0x34);
    CHECK(nodes[0].record.unit_offset == 0 && nodes[1].context.abbrev_offset == 100);
}


int main(void)
{
    check_direct();
    check_owners();
    check_failures();
    check_value_classes();
    check_zero_presence();
    check_depth_and_bounds();
    check_restart();
    check_graph_failures();
    check_cross_unit();
    puts("PASS inherited flags: semantic presence, zero inheritance, physical owners and typed failures");
    return 0;
}
