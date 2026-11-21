#include "symbol/graph.h"
#include "../support/check.h"

#include <stdlib.h>
#include <string.h>

#define FIXTURE_NODES 8

static TbSymbolUnitEntry_t unit;
static TbSymbolUnitsView_t units;
static TbSymbolDieEntry_t entries[FIXTURE_NODES];
static TbSymbolDieAttribute_t attributes[FIXTURE_NODES * 2];
static TbSymbolDiesView_t tree;

static void fixture(size_t count)
{
    CHECK(count && count <= FIXTURE_NODES);
    memset(entries, 0, sizeof(entries));
    memset(attributes, 0, sizeof(attributes));
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
            .offset          = 12 + 16 * i,
            .end_offset      = 28 + 16 * i,
            .parent          = UINT64_MAX,
            .code            = 1,
            .tag             = 0x2e,
            .first_attribute = i * 2
        };
    }
    tree = (TbSymbolDiesView_t){
        .entries         = entries,
        .count           = count,
        .attributes      = attributes,
        .attribute_count = count * 2,
        .section_index   = 1,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .abbrev_section  = 2,
        .backing         = units.backing
    };
}


static void link_nodes(size_t from, size_t to, TbSymbolRelationKind_e kind)
{
    TbSymbolDieEntry_t *entry = &entries[from];
    CHECK(entry->attribute_count < 2);
    uint64_t index = entry->first_attribute + entry->attribute_count++;
    attributes[index] = (TbSymbolDieAttribute_t){
        .name          = kind == TB_SYMBOL_RELATION_SPECIFICATION ? 0x47
                             : kind == TB_SYMBOL_RELATION_ORIGIN ? 0x31 : 0x54,
        .declared_form = TB_SYMBOL_FORM_REF4,
        .offset        = entry->offset + 1 + (entry->attribute_count - 1) * 4,
        .value         = {
            .form          = TB_SYMBOL_FORM_REF4,
            .bytes         = 4,
            .payload_bytes = 4,
            .bits          = entries[to].offset,
            .numeric       = true
        }
    };
    attributes[index].value.payload_offset = attributes[index].offset;
}


static TbSymbolReferenceTarget_t root(void)
{
    return (TbSymbolReferenceTarget_t){
        .backing       = units.backing,
        .section_index = tree.section_index,
        .record_offset = entries[0].offset
    };
}


static TbSymbolGraphStatus_e run(TbSymbolGraph_t *graph, size_t slice)
{
    for (size_t turn = 0; turn < 100000; ++turn) {
        TbSymbolGraphNeed_t need;
        TbSymbolGraphView_t view;
        if (graph->status != TB_SYMBOL_GRAPH_READY)
            CHECK(!tb_symbol_graph_view(graph, &view));
        if (tb_symbol_graph_need(graph, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE) {
                CHECK(tb_symbol_graph_source(graph, &units, &tree, 0) == TB_SYMBOL_GRAPH_PENDING);
            } else {
                TbSymbolGraphStatus_e status = tb_symbol_graph_target(graph, &units, &tree);
                if (status != TB_SYMBOL_GRAPH_PENDING)
                    return status;
            }
        }
        TbSymbolGraphStatus_e status = tb_symbol_graph_step(graph, slice);
        if (status != TB_SYMBOL_GRAPH_PENDING && status != TB_SYMBOL_GRAPH_SOURCE &&
            status != TB_SYMBOL_GRAPH_TARGET) {
            return status;
        }
    }
    CHECK(false);
    return TB_SYMBOL_GRAPH_INVALID;
}


static void check_diamond(void)
{
    fixture(4);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    link_nodes(1, 3, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(2, 3, TB_SYMBOL_RELATION_SPECIFICATION);
    for (size_t slice = 1; slice <= 256; slice *= 2) {
        TbSymbolGraphNode_t nodes[4];
        TbSymbolGraph_t graph;
        CHECK(tb_symbol_graph_init(&graph, root(), nodes, 4) == TB_SYMBOL_GRAPH_SOURCE);
        CHECK(run(&graph, slice) == TB_SYMBOL_GRAPH_READY);
        TbSymbolGraphView_t view;
        CHECK(tb_symbol_graph_view(&graph, &view));
        CHECK(view.count == 4 && view.nodes == nodes);
        CHECK(nodes[0].record.record_offset == entries[0].offset);
        CHECK(nodes[1].record.record_offset == entries[1].offset);
        CHECK(nodes[2].record.record_offset == entries[3].offset);
        CHECK(nodes[3].record.record_offset == entries[2].offset);
        CHECK(nodes[0].present == 3 && nodes[1].present == 1 && nodes[2].present == 0);
        CHECK(nodes[3].edges[0].target.record_offset == nodes[2].record.record_offset);
        CHECK(nodes[1].edges[0].target.record_offset == nodes[2].record.record_offset);
        CHECK(nodes[0].edges[0].source.reference.source_attribute == attributes[0].offset);
        for (size_t i = 0; i < 4; ++i)
            CHECK(nodes[i].complete);
        CHECK(tb_symbol_graph_progress(&graph) > 0);
        TbSymbolGraphFailure_t failure;
        CHECK(!tb_symbol_graph_failure(&graph, &failure));
    }
}


static void check_cycles(void)
{
    for (size_t length = 1; length <= FIXTURE_NODES; ++length) {
        fixture(length);
        for (size_t i = 0; i < length; ++i)
            link_nodes(i, (i + 1) % length, i % 2 ? TB_SYMBOL_RELATION_ORIGIN
                                                : TB_SYMBOL_RELATION_SPECIFICATION);
        TbSymbolGraphNode_t nodes[FIXTURE_NODES];
        TbSymbolGraph_t graph;
        CHECK(tb_symbol_graph_init(&graph, root(), nodes, FIXTURE_NODES) == TB_SYMBOL_GRAPH_SOURCE);
        CHECK(run(&graph, 1) == TB_SYMBOL_GRAPH_CYCLE);
        TbSymbolGraphFailure_t failure;
        CHECK(tb_symbol_graph_failure(&graph, &failure));
        CHECK(failure.node_index == length - 1 && failure.cycle_index == 0);
        CHECK(failure.source_available && failure.selection_available);
        CHECK(failure.record.record_offset == entries[length - 1].offset);
        CHECK(failure.source.reference.source_record == failure.record.record_offset);
        CHECK(failure.relation_status == (length == 1 ? TB_SYMBOL_RELATION_SELF_CYCLE
                                                      : TB_SYMBOL_RELATION_FOUND));
        TbSymbolGraphView_t view = {
            .count = 99
        };
        CHECK(!tb_symbol_graph_view(&graph, &view) && view.count == 99);
    }
    fixture(4);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    link_nodes(2, 3, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(3, 2, TB_SYMBOL_RELATION_ORIGIN);
    TbSymbolGraphNode_t nodes[4];
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 4) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 256) == TB_SYMBOL_GRAPH_CYCLE);
    CHECK(nodes[1].complete);
    TbSymbolGraphFailure_t failure;
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.node_index == 3 && failure.cycle_index == 2);
}


static void check_failures(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    TbSymbolGraphNode_t nodes[2];
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 1) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 256) == TB_SYMBOL_GRAPH_RESOURCE);
    CHECK(graph.count == 1 && !nodes[0].complete);
    CHECK(tb_symbol_graph_step(&graph, 1) == TB_SYMBOL_GRAPH_RESOURCE);
    TbSymbolGraphFailure_t failure;
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.node_index == 0 && failure.relation_index == 0);

    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(tb_symbol_graph_source(&graph, NULL, &tree, 0) == TB_SYMBOL_GRAPH_DEPENDENCY);
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.relation_status == TB_SYMBOL_RELATION_DEPENDENCY);
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(tb_symbol_graph_source(&graph, &units, &tree, 0) == TB_SYMBOL_GRAPH_PENDING);
    CHECK(tb_symbol_graph_step(&graph, 256) == TB_SYMBOL_GRAPH_TARGET);
    CHECK(tb_symbol_graph_target(&graph, &units, NULL) == TB_SYMBOL_GRAPH_DEPENDENCY);

    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 1) == TB_SYMBOL_GRAPH_RELATION);
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.relation_status == TB_SYMBOL_RELATION_CONFLICT);
    CHECK(!failure.source_available && failure.selection_available && failure.selection.matches == 2);
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    entries[1].tag = 0x34;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 256) == TB_SYMBOL_GRAPH_RELATION);
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.relation_status == TB_SYMBOL_RELATION_TAG_MISMATCH);
}


static void check_invalid(void)
{
    fixture(1);
    TbSymbolGraphNode_t nodes[2];
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    TbSymbolGraph_t saved = graph;
    TbSymbolGraphNode_t saved_node = nodes[0];
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 0) == TB_SYMBOL_GRAPH_INVALID);
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, SIZE_MAX / sizeof(*nodes) + 1) ==
          TB_SYMBOL_GRAPH_INVALID);
    TbSymbolReferenceTarget_t bad_root = root();
    bad_root.record_offset = bad_root.unit_offset;
    CHECK(tb_symbol_graph_init(&graph, bad_root, nodes, 2) == TB_SYMBOL_GRAPH_INVALID);
    CHECK(!memcmp(&saved, &graph, sizeof(graph)));
    CHECK(!memcmp(&saved_node, nodes, sizeof(saved_node)));
    CHECK(tb_symbol_graph_step(&graph, 0) == TB_SYMBOL_GRAPH_INVALID);
    CHECK(tb_symbol_graph_step(&graph, 257) == TB_SYMBOL_GRAPH_INVALID);
    CHECK(tb_symbol_graph_source(&graph, &units, &tree, 1) == TB_SYMBOL_GRAPH_INVALID);
    TbSymbolDiesView_t wrong = tree;
    ++wrong.backing.key.inode;
    CHECK(tb_symbol_graph_source(&graph, &units, &wrong, 0) == TB_SYMBOL_GRAPH_INVALID);
    CHECK(!memcmp(&saved, &graph, sizeof(graph)));
    uint64_t old_parent = entries[0].parent;
    entries[0].parent = 0;
    CHECK(tb_symbol_graph_source(&graph, &units, &tree, 0) == TB_SYMBOL_GRAPH_INVALID);
    entries[0].parent = old_parent;
    CHECK(tb_symbol_graph_target(&graph, &units, &tree) == TB_SYMBOL_GRAPH_INVALID);
    TbSymbolGraphNeed_t need;
    CHECK(tb_symbol_graph_need(&graph, &need) && need.kind == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 1) == TB_SYMBOL_GRAPH_READY);
    memset(&need, 0xa5, sizeof(need));
    TbSymbolGraphNeed_t saved_need = need;
    CHECK(!tb_symbol_graph_need(&graph, &need) && !memcmp(&need, &saved_need, sizeof(need)));
    CHECK(tb_symbol_graph_source(&graph, &units, &tree, 0) == TB_SYMBOL_GRAPH_INVALID);
    CHECK(!tb_symbol_graph_need(NULL, &need));
    CHECK(tb_symbol_graph_step(NULL, 1) == TB_SYMBOL_GRAPH_INVALID);
    CHECK(!tb_symbol_graph_progress(NULL));
}


static void check_unsupported_and_malformed(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    entries[0].tag = 0x4109;
    TbSymbolGraphNode_t nodes[2];
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 1) == TB_SYMBOL_GRAPH_RELATION);
    TbSymbolGraphFailure_t failure;
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.relation_status == TB_SYMBOL_RELATION_UNSUPPORTED && failure.source_available);
    CHECK(failure.source.source_tag == 0x4109 && failure.source.field.name == 0x31);
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    attributes[0].value.form = TB_SYMBOL_FORM_DATA4;
    attributes[0].declared_form = TB_SYMBOL_FORM_DATA4;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 256) == TB_SYMBOL_GRAPH_RELATION);
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.relation_status == TB_SYMBOL_RELATION_MALFORMED);
    CHECK(failure.selection_available && !failure.source_available);
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    ++attributes[0].value.bits;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 1) == TB_SYMBOL_GRAPH_RELATION);
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.relation_status == TB_SYMBOL_RELATION_MISSING_TARGET && failure.source_available);
}


static void check_late_conflict(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    const size_t count = 1001;
    TbSymbolDieAttribute_t *fields = calloc(count, sizeof(*fields));
    CHECK(fields);
    fields[0] = attributes[0];
    for (size_t i = 1; i < count; ++i) {
        fields[i] = attributes[0];
        fields[i].name = i + 1 == count ? 0x47 : 0x10000;
        fields[i].offset += i * 4;
        fields[i].value.payload_offset = fields[i].offset;
    }
    TbSymbolDiesView_t scope = tree;
    scope.attributes = fields;
    scope.attribute_count = count;
    scope.backing.bytes = 10000;
    TbSymbolUnitsView_t inventory = units;
    inventory.backing = scope.backing;
    entries[0].attribute_count = count;
    entries[0].end_offset = 5000;
    TbSymbolReferenceTarget_t first = root();
    first.backing = scope.backing;
    TbSymbolGraphNode_t node;
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, first, &node, 1) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(tb_symbol_graph_source(&graph, &inventory, &scope, 0) == TB_SYMBOL_GRAPH_PENDING);
    for (size_t i = 0; i < count - 1; ++i) {
        CHECK(tb_symbol_graph_step(&graph, 1) == TB_SYMBOL_GRAPH_PENDING);
        TbSymbolGraphNeed_t need;
        CHECK(!tb_symbol_graph_need(&graph, &need));
    }
    CHECK(tb_symbol_graph_step(&graph, 1) == TB_SYMBOL_GRAPH_RELATION);
    TbSymbolGraphFailure_t failure;
    CHECK(tb_symbol_graph_failure(&graph, &failure));
    CHECK(failure.relation_status == TB_SYMBOL_RELATION_CONFLICT);
    CHECK(failure.selection_available && failure.selection.second_index == count - 1);
    free(fields);
}


static void check_namespace(void)
{
    fixture(3);
    for (size_t i = 0; i < 3; ++i)
        entries[i].tag = 0x39;
    link_nodes(0, 1, TB_SYMBOL_RELATION_EXTENSION);
    link_nodes(1, 2, TB_SYMBOL_RELATION_EXTENSION);
    TbSymbolGraphNode_t nodes[3];
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 3) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 1) == TB_SYMBOL_GRAPH_READY);
    CHECK(nodes[0].present == 4 && nodes[1].present == 4 && nodes[2].present == 0);
}


static void check_cross_unit(void)
{
    fixture(2);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    attributes[0].declared_form = TB_SYMBOL_FORM_REF_ADDR;
    attributes[0].value.form = TB_SYMBOL_FORM_REF_ADDR;
    attributes[0].value.bits = 512;
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
    TbSymbolDieEntry_t remote_entry = {
        .offset     = 512,
        .end_offset = 513,
        .parent     = UINT64_MAX,
        .code       = 1,
        .tag        = 0x2e
    };
    TbSymbolDiesView_t remote = tree;
    remote.entries = &remote_entry;
    remote.count = 1;
    remote.unit_offset = 500;
    remote.attribute_count = 0;
    remote.abbrev_offset = 100;
    TbSymbolGraphNode_t nodes[2];
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(tb_symbol_graph_source(&graph, &inventory, &tree, 0) == TB_SYMBOL_GRAPH_PENDING);
    CHECK(tb_symbol_graph_step(&graph, 256) == TB_SYMBOL_GRAPH_TARGET);
    TbSymbolGraphNeed_t need;
    CHECK(tb_symbol_graph_need(&graph, &need));
    CHECK(need.source.reference.kind == TB_SYMBOL_REFERENCE_INFO);
    CHECK(tb_symbol_graph_target(&graph, &inventory, &remote) == TB_SYMBOL_GRAPH_PENDING);
    CHECK(tb_symbol_graph_step(&graph, 256) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(tb_symbol_graph_need(&graph, &need));
    CHECK(need.record.unit_offset == 500 && need.record.record_offset == 512);
    TbSymbolGraph_t saved = graph;
    headers[1].header.version = 4;
    CHECK(tb_symbol_graph_source(&graph, &inventory, &remote, 1) == TB_SYMBOL_GRAPH_INVALID);
    CHECK(!memcmp(&saved, &graph, sizeof(graph)));
    headers[1].header.version = 5;
    ++remote.abbrev_offset;
    CHECK(tb_symbol_graph_source(&graph, &inventory, &remote, 1) == TB_SYMBOL_GRAPH_INVALID);
    --remote.abbrev_offset;
    CHECK(tb_symbol_graph_source(&graph, &inventory, &remote, 1) == TB_SYMBOL_GRAPH_PENDING);
    CHECK(tb_symbol_graph_step(&graph, 256) == TB_SYMBOL_GRAPH_READY);
    CHECK(nodes[0].edges[0].target.unit_offset == 500 && nodes[1].context.abbrev_offset == 100);
    CHECK(nodes[0].record.record_offset != nodes[1].record.record_offset);

    CHECK(tb_symbol_graph_init(&graph, root(), nodes, 2) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(tb_symbol_graph_source(&graph, &inventory, &tree, 0) == TB_SYMBOL_GRAPH_PENDING);
    CHECK(tb_symbol_graph_step(&graph, 256) == TB_SYMBOL_GRAPH_TARGET);
    CHECK(tb_symbol_graph_target(&graph, NULL, &remote) == TB_SYMBOL_GRAPH_DEPENDENCY);
}


static void check_deep(void)
{
    const size_t count = 1001;
    TbSymbolDieEntry_t *records = calloc(count, sizeof(*records));
    TbSymbolDieAttribute_t *fields = calloc(count - 1, sizeof(*fields));
    TbSymbolGraphNode_t *nodes = malloc(count * sizeof(*nodes));
    CHECK(records && fields && nodes);
    fixture(1);
    TbSymbolDiesView_t deep = tree;
    deep.entries = records;
    deep.count = count;
    deep.attributes = fields;
    deep.attribute_count = count - 1;
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
            .attribute_count = i + 1 == count ? 0 : 1
        };
        if (i + 1 < count) {
            fields[i] = (TbSymbolDieAttribute_t){
                .name          = 0x47,
                .declared_form = TB_SYMBOL_FORM_REF4,
                .offset        = records[i].offset + 1,
                .value         = {
                    .form          = TB_SYMBOL_FORM_REF4,
                    .bytes         = 4,
                    .payload_bytes = 4,
                    .bits          = records[i].end_offset,
                    .numeric       = true
                }
            };
            fields[i].value.payload_offset = fields[i].offset;
        }
    }
    TbSymbolGraph_t graph;
    TbSymbolReferenceTarget_t first = root();
    first.backing = deep.backing;
    CHECK(tb_symbol_graph_init(&graph, first, nodes, count) == TB_SYMBOL_GRAPH_SOURCE);
    for (size_t turn = 0; turn < 1000000 && graph.status != TB_SYMBOL_GRAPH_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&graph, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE)
                CHECK(tb_symbol_graph_source(&graph, &inventory, &deep, 0) == TB_SYMBOL_GRAPH_PENDING);
            else
                CHECK(tb_symbol_graph_target(&graph, &inventory, &deep) == TB_SYMBOL_GRAPH_PENDING);
        }
        TbSymbolGraphStatus_e status = tb_symbol_graph_step(&graph, 1);
        CHECK(status == TB_SYMBOL_GRAPH_READY || status == TB_SYMBOL_GRAPH_PENDING ||
              status == TB_SYMBOL_GRAPH_SOURCE || status == TB_SYMBOL_GRAPH_TARGET);
    }
    TbSymbolGraphView_t view;
    CHECK(tb_symbol_graph_view(&graph, &view) && view.count == count);
    for (size_t i = 0; i < count; ++i)
        CHECK(nodes[i].complete && nodes[i].record.record_offset == records[i].offset);
    CHECK(tb_symbol_graph_progress(&graph) > count * count / 2);
    free(nodes);
    free(fields);
    free(records);
}


static void check_node_source(void)
{
    fixture(1);
    TbSymbolGraphNode_t node;
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, root(), &node, 1) == TB_SYMBOL_GRAPH_SOURCE);
    CHECK(run(&graph, 1) == TB_SYMBOL_GRAPH_READY);
    CHECK(tb_symbol_graph_node_matches(&node, units, tree, 0));
    CHECK(!tb_symbol_graph_node_matches(NULL, units, tree, 0));
    CHECK(!tb_symbol_graph_node_matches(&node, units, tree, 1));
    node.complete = false;
    CHECK(!tb_symbol_graph_node_matches(&node, units, tree, 0));
    node.complete = true;
    TbSymbolDiesView_t wrong = tree;
    ++wrong.abbrev_offset;
    CHECK(!tb_symbol_graph_node_matches(&node, units, wrong, 0));
    wrong = tree;
    ++wrong.backing.key.inode;
    CHECK(!tb_symbol_graph_node_matches(&node, units, wrong, 0));
    CHECK(tb_symbol_graph_node_matches(&node, units, tree, 0));
}


int main(void)
{
    check_diamond();
    check_cycles();
    check_failures();
    check_invalid();
    check_unsupported_and_malformed();
    check_late_conflict();
    check_namespace();
    check_cross_unit();
    check_deep();
    check_node_source();
    puts("PASS bounded physical relationship traversal, shared paths, cycles and failed-prefix isolation");
    return 0;
}
