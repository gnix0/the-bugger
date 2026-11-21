#include "symbol/context.h"
#include "symbol/inherit.h"
#include "../support/check.h"

#include <string.h>

#define FIXTURE_NODES 520
#define FIXTURE_FIELDS 4

static TbSymbolUnitEntry_t units[2];
static TbSymbolUnitsView_t inventory;
static TbSymbolDieEntry_t entries[FIXTURE_NODES];
static TbSymbolDieAttribute_t attributes[FIXTURE_NODES * FIXTURE_FIELDS];
static TbSymbolDiesView_t tree;
static TbSymbolGraphNode_t nodes[FIXTURE_NODES];
static TbSymbolGraphView_t graph;
static TbSymbolContextAnchor_t anchors[FIXTURE_NODES];
static TbSymbolContext_t context;
static TbSymbolDieEntry_t remote_entries[3];
static TbSymbolDiesView_t remote_tree;

static void fixture(size_t count)
{
    CHECK(count && count <= FIXTURE_NODES);
    memset(entries, 0, sizeof(entries));
    memset(attributes, 0, sizeof(attributes));
    memset(units, 0, sizeof(units));
    memset(&remote_tree, 0, sizeof(remote_tree));
    units[0] = (TbSymbolUnitEntry_t){
        .extent           = {
            .payload_bytes = 32000 - 4,
            .total_bytes   = 32000,
            .end_offset    = 32000,
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
    units[0].header.extent = units[0].extent;
    inventory = (TbSymbolUnitsView_t){
        .entries       = units,
        .count         = 1,
        .section_index = 1,
        .kind          = TB_SYMBOL_DWARF_INFO,
        .backing       = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 100000
        }
    };
    for (size_t i = 0; i < count; ++i) {
        entries[i] = (TbSymbolDieEntry_t){
            .offset          = 12 + 32 * i,
            .end_offset      = 13 + 32 * i,
            .parent          = i ? 0 : UINT64_MAX,
            .code            = i + 1,
            .tag             = i ? 0x2e : 0x11,
            .first_attribute = i * FIXTURE_FIELDS,
            .children        = !i
        };
    }
    tree = (TbSymbolDiesView_t){
        .entries         = entries,
        .attributes      = attributes,
        .count           = count,
        .attribute_count = count * FIXTURE_FIELDS,
        .section_index   = 1,
        .abbrev_section  = 2,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing         = inventory.backing
    };
}


static void field(size_t index, uint64_t name, uint64_t form, uint64_t bits)
{
    TbSymbolDieEntry_t *entry = &entries[index];
    CHECK(entry->attribute_count < FIXTURE_FIELDS);
    uint64_t at = entry->first_attribute + entry->attribute_count++;
    attributes[at] = (TbSymbolDieAttribute_t){
        .schema_attribute = entry->attribute_count - 1,
        .name             = name,
        .declared_form    = form,
        .offset           = entry->offset + 1 + (entry->attribute_count - 1) * 4,
        .value            = {
            .form          = form,
            .bytes         = 4,
            .payload_bytes = 4,
            .bits          = bits,
            .numeric       = true
        }
    };
    attributes[at].value.payload_offset = attributes[at].offset;
    entry->end_offset = attributes[at].offset + attributes[at].value.bytes;
}


static void link(size_t from, size_t to, TbSymbolRelationKind_e kind)
{
    field(from, kind == TB_SYMBOL_RELATION_SPECIFICATION ? 0x47
                    : kind == TB_SYMBOL_RELATION_ORIGIN ? 0x31 : 0x54,
          TB_SYMBOL_FORM_REF4, entries[to].offset);
}


static void build_graph(size_t selected)
{
    uint64_t count = 0;
    for (uint64_t i = 0; i < tree.count; ++i) {
        uint64_t old = entries[i].first_attribute;
        if (entries[i].parent != UINT64_MAX && entries[i].parent < i)
            entries[entries[i].parent].children = true;
        memmove(attributes + count, attributes + old,
                (size_t)entries[i].attribute_count * sizeof(*attributes));
        entries[i].first_attribute = count;
        count += entries[i].attribute_count;
    }
    tree.attribute_count = count;
    TbSymbolGraph_t cursor;
    CHECK(tb_symbol_graph_init(&cursor, (TbSymbolReferenceTarget_t){
        .backing       = inventory.backing,
        .section_index = 1,
        .record_offset = entries[selected].offset,
        .entry_index   = selected
    }, nodes, FIXTURE_NODES) == TB_SYMBOL_GRAPH_SOURCE);
    bool completed = false;
    for (unsigned turn = 0; turn < 2000000 && !completed; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&cursor, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE) {
                bool remote = need.record.unit_offset != 0;
                CHECK(tb_symbol_graph_source(&cursor, &inventory, remote ? &remote_tree : &tree,
                                              remote ? 1 : 0) == TB_SYMBOL_GRAPH_PENDING);
            } else {
                CHECK(tb_symbol_graph_target(&cursor, &inventory,
                                              inventory.count == 2 ? &remote_tree : &tree) ==
                      TB_SYMBOL_GRAPH_PENDING);
            }
        }
        TbSymbolGraphStatus_e status = tb_symbol_graph_step(&cursor, 31);
        completed = status == TB_SYMBOL_GRAPH_READY;
        CHECK(completed || status == TB_SYMBOL_GRAPH_PENDING || status == TB_SYMBOL_GRAPH_SOURCE ||
              status == TB_SYMBOL_GRAPH_TARGET);
    }
    CHECK(completed && tb_symbol_graph_view(&cursor, &graph));
    CHECK(tb_symbol_context_init(&context, graph, anchors, FIXTURE_NODES) == TB_SYMBOL_CONTEXT_PENDING);
}


static TbSymbolContextStatus_e run(size_t slice)
{
    for (unsigned turn = 0; turn < 100000; ++turn) {
        TbSymbolContextView_t view;
        CHECK(!tb_symbol_context_view(&context, &view));
        TbSymbolGraphNeed_t need;
        if (tb_symbol_context_need(&context, &need)) {
            bool remote = need.record.unit_offset != 0;
            TbSymbolContextStatus_e status = tb_symbol_context_source(&context, &inventory,
                                                                      remote ? &remote_tree : &tree,
                                                                      remote ? 1 : 0);
            if (status != TB_SYMBOL_CONTEXT_PENDING)
                return status;
        }
        TbSymbolContextStatus_e status = tb_symbol_context_step(&context, slice);
        if (status != TB_SYMBOL_CONTEXT_PENDING && status != TB_SYMBOL_CONTEXT_SOURCE)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_CONTEXT_INVALID;
}


static TbSymbolContextView_t result(size_t count)
{
    CHECK(run(1) == TB_SYMBOL_CONTEXT_READY);
    TbSymbolContextView_t view;
    CHECK(tb_symbol_context_view(&context, &view) && view.count == count);
    CHECK(view.selected.record_offset == graph.nodes[0].record.record_offset);
    for (size_t i = 0; i < view.count; ++i) {
        CHECK(view.anchors[i].node_index < graph.count);
        CHECK(!graph.nodes[view.anchors[i].node_index].present);
        CHECK(view.anchors[i].record.record_offset ==
              graph.nodes[view.anchors[i].node_index].record.record_offset);
    }
    return view;
}


static void check_context_and_name(void)
{
    fixture(4);
    entries[1].tag = 0x13;
    entries[2].parent = 1;
    link(3, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    field(3, 0x03, TB_SYMBOL_FORM_DATA4, 7);
    build_graph(3);
    TbSymbolContextView_t view = result(1);
    CHECK(view.selected.entry_index == 3 && view.anchors[0].record.entry_index == 2);
    CHECK(view.anchors[0].kind == TB_SYMBOL_CONTEXT_LEXICAL_PARENT);
    CHECK(view.anchors[0].parent.entry_index == 1 && view.anchors[0].parent_context.tag == 0x13);
    TbSymbolInherit_t inherit;
    TbSymbolInheritSlot_t slots[4];
    CHECK(tb_symbol_inherit_init(&inherit, graph, TB_SYMBOL_INHERIT_NAME, slots, 4) ==
          TB_SYMBOL_INHERIT_PENDING);
    TbSymbolInheritStatus_e status = TB_SYMBOL_INHERIT_PENDING;
    for (unsigned turn = 0; turn < 1000 && status != TB_SYMBOL_INHERIT_UNIQUE; ++turn) {
        TbSymbolInheritNeed_t need;
        if (tb_symbol_inherit_need(&inherit, &need))
            CHECK(tb_symbol_inherit_source(&inherit, &inventory, &tree, 0) == TB_SYMBOL_INHERIT_PENDING);
        status = tb_symbol_inherit_step(&inherit, 1);
    }
    CHECK(status == TB_SYMBOL_INHERIT_UNIQUE);
    TbSymbolInheritOwner_t owner;
    CHECK(tb_symbol_inherit_owner(&inherit, 0, &owner));
    CHECK(owner.attribute.value.form == TB_SYMBOL_FORM_DATA4);
    /* Raw name ownership and its value class do not determine declaration context. */
    CHECK(owner.node_index != view.anchors[0].node_index);

    fixture(5);
    entries[1].tag = 0x39;
    entries[2].parent = 1;
    entries[4].parent = 3;
    entries[4].tag = 0x1d;
    link(4, 2, TB_SYMBOL_RELATION_ORIGIN);
    build_graph(4);
    view = result(1);
    CHECK(view.anchors[0].parent.entry_index == 1 && view.anchors[0].parent_context.tag == 0x39);
    CHECK(view.anchors[0].parent.entry_index != entries[4].parent);
}


static void check_shared_and_distinct(void)
{
    fixture(4);
    link(1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    link(1, 3, TB_SYMBOL_RELATION_ORIGIN);
    build_graph(1);
    TbSymbolContextView_t view = result(2);
    CHECK(view.anchors[0].record.record_offset != view.anchors[1].record.record_offset);
    CHECK(view.anchors[0].parent.record_offset == view.anchors[1].parent.record_offset);
    CHECK(view.anchors[0].kind == TB_SYMBOL_CONTEXT_UNIT_PARENT);
    CHECK(view.anchors[0].parent_context.parent_record == UINT64_MAX);
    CHECK(view.anchors[0].unit_root.record_offset == entries[0].offset);

    fixture(5);
    link(1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    link(1, 3, TB_SYMBOL_RELATION_ORIGIN);
    link(2, 4, TB_SYMBOL_RELATION_SPECIFICATION);
    link(3, 4, TB_SYMBOL_RELATION_SPECIFICATION);
    build_graph(1);
    view = result(1);
    CHECK(view.anchors[0].record.entry_index == 4);

    fixture(3);
    entries[1].tag = 0x39;
    entries[2].tag = 0x39;
    link(2, 1, TB_SYMBOL_RELATION_EXTENSION);
    build_graph(2);
    view = result(1);
    CHECK(view.anchors[0].record.entry_index == 1 && view.anchors[0].parent.entry_index == 0);
}


static void check_unit_boundaries(void)
{
    const TbSymbolDwarfUnitKind_e kinds[] = {TB_SYMBOL_DWARF_COMPILE, TB_SYMBOL_DWARF_PARTIAL,
                                            TB_SYMBOL_DWARF_TYPE};
    const uint64_t tags[] = {0x11, 0x3c, 0x41};
    for (unsigned version = 4; version <= 5; ++version) {
        for (size_t kind = 0; kind < sizeof(kinds) / sizeof(kinds[0]); ++kind) {
            fixture(1);
            units[0].header.version = (uint16_t)version;
            units[0].header.kind = version == 4 && kind < 2
                                       ? TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL : kinds[kind];
            units[0].header.header_bytes = kind == 2 ? (version == 4 ? 23 : 24) : version == 4 ? 11 : 12;
            units[0].header.unit_type = version == 4 ? 0 : kind == 0 ? 1 : kind == 1 ? 3 : 2;
            entries[0].tag = tags[kind];
            entries[0].offset = units[0].header.header_bytes;
            entries[0].end_offset = entries[0].offset + 1;
            tree.kind = kinds[kind];
            inventory.kind = kind == 2 && version == 4 ? TB_SYMBOL_DWARF_TYPES : TB_SYMBOL_DWARF_INFO;
            build_graph(0);
            TbSymbolContextView_t view = result(1);
            CHECK(view.anchors[0].kind == TB_SYMBOL_CONTEXT_UNIT_ROOT);
            CHECK(view.anchors[0].unit_root.record_offset == entries[0].offset);
            CHECK(view.anchors[0].unit.header.version == version);
        }
    }
}


static void check_invalid_and_missing(void)
{
    fixture(4);
    link(1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    link(1, 3, TB_SYMBOL_RELATION_ORIGIN);
    build_graph(1);
    unsigned char before[sizeof(context)];
    memcpy(before, &context, sizeof(before));
    CHECK(tb_symbol_context_init(&context, graph, anchors, 1) == TB_SYMBOL_CONTEXT_RESOURCE);
    CHECK(!memcmp(&context, before, sizeof(before)));
    CHECK(tb_symbol_context_init(&context, graph, NULL, 4) == TB_SYMBOL_CONTEXT_INVALID);
    CHECK(tb_symbol_context_init(&context, graph, anchors, SIZE_MAX) == TB_SYMBOL_CONTEXT_INVALID);
    CHECK(!memcmp(&context, before, sizeof(before)));
    CHECK(tb_symbol_context_step(&context, 0) == TB_SYMBOL_CONTEXT_INVALID);
    CHECK(tb_symbol_context_step(&context, 257) == TB_SYMBOL_CONTEXT_INVALID);
    CHECK(!memcmp(&context, before, sizeof(before)));
    unsigned supplied = 0;
    for (unsigned turn = 0; turn < 1000; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_context_need(&context, &need)) {
            if (++supplied == 2) {
                CHECK(context.count == 1);
                CHECK(tb_symbol_context_source(&context, NULL, NULL, 0) == TB_SYMBOL_CONTEXT_DEPENDENCY);
                break;
            }
            TbSymbolDiesView_t wrong = tree;
            ++wrong.abbrev_offset;
            CHECK(tb_symbol_context_source(&context, &inventory, &wrong, 0) == TB_SYMBOL_CONTEXT_INVALID);
            ++units[0].header.header_bytes;
            CHECK(tb_symbol_context_source(&context, &inventory, &tree, 0) == TB_SYMBOL_CONTEXT_INVALID);
            --units[0].header.header_bytes;
            CHECK(tb_symbol_context_need(&context, &need));
            CHECK(tb_symbol_context_source(&context, &inventory, &tree, 0) == TB_SYMBOL_CONTEXT_PENDING);
        }
        CHECK(tb_symbol_context_step(&context, 1) != TB_SYMBOL_CONTEXT_READY);
    }
    CHECK(supplied == 2);
    TbSymbolContextView_t view;
    CHECK(!tb_symbol_context_view(&context, &view));
    TbSymbolContextFailure_t failure;
    CHECK(tb_symbol_context_failure(&context, &failure));
    CHECK(failure.reason == TB_SYMBOL_CONTEXT_SOURCE_MISSING);

    CHECK(tb_symbol_context_init(&context, graph, anchors, FIXTURE_NODES) == TB_SYMBOL_CONTEXT_PENDING);
    nodes[graph.count - 1].complete = false;
    CHECK(tb_symbol_context_step(&context, 256) == TB_SYMBOL_CONTEXT_INVALID);
    CHECK(!tb_symbol_context_view(&context, &view));
    CHECK(tb_symbol_context_failure(&context, &failure) && failure.reason == TB_SYMBOL_CONTEXT_GRAPH);
    nodes[graph.count - 1].complete = true;
    CHECK(tb_symbol_context_init(&context, graph, anchors, FIXTURE_NODES) == TB_SYMBOL_CONTEXT_PENDING);
    unsigned saved = nodes[0].present;
    nodes[0].present |= 8u;
    CHECK(tb_symbol_context_step(&context, 256) == TB_SYMBOL_CONTEXT_INVALID);
    CHECK(tb_symbol_context_failure(&context, &failure) && failure.reason == TB_SYMBOL_CONTEXT_GRAPH);
    nodes[0].present = saved;
    CHECK(tb_symbol_context_init(&context, graph, anchors, FIXTURE_NODES) == TB_SYMBOL_CONTEXT_PENDING);
    context.progress = UINT64_MAX;
    CHECK(tb_symbol_context_step(&context, 1) == TB_SYMBOL_CONTEXT_INVALID);
    CHECK(tb_symbol_context_failure(&context, &failure) && failure.reason == TB_SYMBOL_CONTEXT_PROGRESS);
}


static void check_cross_unit(void)
{
    fixture(2);
    units[0].extent.total_bytes = 512;
    units[0].extent.end_offset = 512;
    units[0].extent.payload_bytes = 508;
    units[0].header.extent = units[0].extent;
    units[1] = units[0];
    units[1].offset = 512;
    units[1].extent = (TbSymbolDwarfLength_t){
        .payload_bytes = 500,
        .total_bytes   = 512,
        .end_offset    = 1024,
        .length_bytes  = 12,
        .offset_bytes  = 8
    };
    units[1].header.extent = units[1].extent;
    units[1].header.header_bytes = 24;
    inventory.count = 2;
    field(1, 0x47, TB_SYMBOL_FORM_REF_ADDR, 600);
    remote_entries[0] = (TbSymbolDieEntry_t){
        .offset     = 536,
        .end_offset = 537,
        .parent     = UINT64_MAX,
        .code       = 1,
        .tag        = 0x11,
        .children   = true
    };
    remote_entries[1] = (TbSymbolDieEntry_t){
        .offset     = 560,
        .end_offset = 561,
        .parent     = 0,
        .code       = 2,
        .tag        = 0x39,
        .children   = true
    };
    remote_entries[2] = (TbSymbolDieEntry_t){
        .offset     = 600,
        .end_offset = 601,
        .parent     = 1,
        .code       = 3,
        .tag        = 0x2e
    };
    remote_tree = (TbSymbolDiesView_t){
        .entries       = remote_entries,
        .count         = 3,
        .section_index = 1,
        .unit_offset   = 512,
        .abbrev_section = 2,
        .kind           = TB_SYMBOL_DWARF_COMPILE,
        .backing        = inventory.backing
    };
    build_graph(1);
    TbSymbolContextView_t view = result(1);
    CHECK(view.selected.unit_offset == 0);
    CHECK(view.anchors[0].record.unit_offset == 512 && view.anchors[0].record.record_offset == 600);
    CHECK(view.anchors[0].parent.unit_offset == 512 && view.anchors[0].parent.record_offset == 560);
    CHECK(view.anchors[0].unit_root.record_offset == 536 && view.anchors[0].unit.extent.offset_bytes == 8);
}


static void check_recursive_boundary(void)
{
    fixture(3);
    entries[1].tag = 0x13;
    entries[2].tag = 0x13;
    entries[2].parent = 1;
    link(1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    build_graph(1);
    TbSymbolContextView_t view = result(1);
    CHECK(view.anchors[0].parent.record_offset == view.selected.record_offset);
    /* The acyclic relation graph alone cannot prove mixed dependency acyclicity.
     * A recursive qualified-context consumer must reject this returning parent. */
}


static void check_deep(void)
{
    fixture(513);
    for (size_t i = 1; i <= 256; ++i) {
        if (i < 256)
            link(i, i + 1, TB_SYMBOL_RELATION_SPECIFICATION);
        link(i, i + 256, TB_SYMBOL_RELATION_ORIGIN);
    }
    build_graph(1);
    CHECK(graph.count == 512);
    TbSymbolContextView_t view = result(256);
    for (size_t i = 0; i < view.count; ++i)
        CHECK(view.anchors[i].kind == TB_SYMBOL_CONTEXT_UNIT_PARENT);
    CHECK(tb_symbol_context_progress(&context) >= 2 * graph.count);
}


int main(void)
{
    check_context_and_name();
    check_shared_and_distinct();
    check_unit_boundaries();
    check_invalid_and_missing();
    check_cross_unit();
    check_recursive_boundary();
    check_deep();
    puts("PASS bounded declaration-context anchors, inline/specification context "
         "and distinct physical parents");
    return 0;
}
