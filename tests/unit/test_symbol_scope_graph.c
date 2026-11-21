#include "symbol/scope_graph.h"
#include "../support/check.h"

#include <string.h>

#define FIXTURE_NODES 520
#define FIXTURE_FIELDS 4

static TbSymbolUnitEntry_t units[2];
static TbSymbolUnitsView_t inventory;
static TbSymbolDieEntry_t entries[FIXTURE_NODES];
static TbSymbolDieAttribute_t attributes[FIXTURE_NODES * FIXTURE_FIELDS];
static TbSymbolDiesView_t tree;

static void fixture(size_t count)
{
    CHECK(count && count <= FIXTURE_NODES);
    memset(entries, 0, sizeof(entries));
    memset(attributes, 0, sizeof(attributes));
    memset(units, 0, sizeof(units));
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


#define PROOF_CAPACITY 520
#define PROOF_NODES 8

typedef struct {
    TbSymbolGraphNode_t     nodes[PROOF_NODES];
    TbSymbolContextAnchor_t anchors[PROOF_NODES];
    TbSymbolGraphView_t     graph;
    TbSymbolContextView_t   contexts;
    bool                   available;
} Proof_t;

static Proof_t proofs[PROOF_CAPACITY];
static TbSymbolScopeNode_t scope_nodes[PROOF_CAPACITY];
static TbSymbolScopeEdge_t scope_edges[2 * PROOF_CAPACITY];
static TbSymbolScope_t scope;
static bool conflict_claim;
static bool missing_claim;
static size_t proof_count;
static TbSymbolDieEntry_t remote_entries[3];
static TbSymbolDieAttribute_t remote_attributes[1];
static TbSymbolDiesView_t remote_tree;

static Proof_t *proof(TbSymbolReferenceTarget_t selected)
{
    for (size_t i = 0; i < proof_count; ++i) {
        const TbSymbolReferenceTarget_t *record = &proofs[i].graph.nodes[0].record;
        if (record->unit_offset == selected.unit_offset && record->record_offset == selected.record_offset)
            return &proofs[i];
    }
    CHECK(proof_count < PROOF_CAPACITY);
    Proof_t *input = &proofs[proof_count++];
    TbSymbolGraph_t relationships;
    CHECK(tb_symbol_graph_init(&relationships, selected, input->nodes, PROOF_NODES) ==
          TB_SYMBOL_GRAPH_SOURCE);
    for (unsigned turn = 0; turn < 10000 && relationships.status != TB_SYMBOL_GRAPH_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&relationships, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE) {
                bool remote = need.record.unit_offset != 0;
                CHECK(tb_symbol_graph_source(&relationships, &inventory, remote ? &remote_tree : &tree,
                                              remote ? 1 : 0) == TB_SYMBOL_GRAPH_PENDING);
            } else {
                TbSymbolRelationNeed_t target;
                CHECK(tb_symbol_relation_need(need.source, &inventory, &target) == TB_SYMBOL_RELATION_READY);
                CHECK(tb_symbol_graph_target(&relationships, &inventory,
                                              target.target.unit_offset ? &remote_tree : &tree) ==
                      TB_SYMBOL_GRAPH_PENDING);
            }
        }
        (void)tb_symbol_graph_step(&relationships, 31);
    }
    CHECK(tb_symbol_graph_view(&relationships, &input->graph));
    TbSymbolContext_t contexts;
    CHECK(tb_symbol_context_init(&contexts, input->graph, input->anchors, PROOF_NODES) ==
          TB_SYMBOL_CONTEXT_PENDING);
    for (unsigned turn = 0; turn < 10000 && contexts.status != TB_SYMBOL_CONTEXT_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_context_need(&contexts, &need)) {
            bool remote = need.record.unit_offset != 0;
            CHECK(tb_symbol_context_source(&contexts, &inventory, remote ? &remote_tree : &tree,
                                            remote ? 1 : 0) == TB_SYMBOL_CONTEXT_PENDING);
        }
        (void)tb_symbol_context_step(&contexts, 31);
    }
    CHECK(tb_symbol_context_view(&contexts, &input->contexts));
    input->available = true;
    return input;
}


static void begin(size_t selected, size_t nodes_capacity, size_t edges_capacity)
{
    memset(proofs, 0, sizeof(proofs));
    proof_count = 0;
    conflict_claim = false;
    missing_claim = false;
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
    CHECK(tb_symbol_scope_init(&scope, (TbSymbolReferenceTarget_t){
        .backing       = tree.backing,
        .section_index = tree.section_index,
        .record_offset = entries[selected].offset,
        .entry_index   = selected
    }, scope_nodes, nodes_capacity, scope_edges, edges_capacity) == TB_SYMBOL_SCOPE_SOURCE);
}


static TbSymbolScopeStatus_e finish_scope(size_t slice)
{
    for (unsigned turn = 0; turn < 2000000; ++turn) {
        TbSymbolScopeView_t view;
        CHECK(!tb_symbol_scope_view(&scope, &view));
        TbSymbolScopeNeed_t need;
        if (tb_symbol_scope_need(&scope, &need)) {
            if (missing_claim && need.record.entry_index == 4)
                return tb_symbol_scope_source(&scope, NULL, NULL);
            Proof_t *input = proof(need.record);
            if (conflict_claim && need.record.entry_index == 5) {
                input->anchors[0].parent_context.parent_record = entries[0].offset;
                input->anchors[0].parent_context.parent_tag = entries[0].tag;
            }
            CHECK(tb_symbol_scope_source(&scope, &input->graph, &input->contexts) == TB_SYMBOL_SCOPE_PENDING);
        }
        TbSymbolScopeStatus_e status = tb_symbol_scope_step(&scope, slice);
        if (status != TB_SYMBOL_SCOPE_PENDING && status != TB_SYMBOL_SCOPE_SOURCE)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_SCOPE_INVALID;
}


static void diamond(void)
{
    fixture(7);
    entries[1].tag = 0x39;
    entries[2].tag = 0x39;
    entries[2].parent = 1;
    entries[3].parent = 2;
    entries[4].tag = 0x39;
    entries[4].parent = 1;
    entries[5].parent = 4;
    link(6, 3, TB_SYMBOL_RELATION_SPECIFICATION);
    link(6, 5, TB_SYMBOL_RELATION_ORIGIN);
}


static void check_sharing(void)
{
    diamond();
    begin(6, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_READY);
    TbSymbolScopeView_t view;
    CHECK(tb_symbol_scope_view(&scope, &view));
    CHECK(view.node_count == 4 && view.edge_count == 5);
    CHECK(view.nodes[0].record.entry_index == 6);
    CHECK(view.nodes[1].record.entry_index == 2);
    CHECK(view.nodes[2].record.entry_index == 1);
    CHECK(view.nodes[3].record.entry_index == 4);
    CHECK(view.edges[view.nodes[1].first_edge].target_node == 2);
    CHECK(view.edges[view.nodes[3].first_edge].target_node == 2);
    for (size_t i = 0; i < view.node_count; ++i)
        CHECK(view.nodes[i].complete);
}


static void check_mixed_cycles(void)
{
    fixture(3);
    entries[1].tag = 0x13;
    entries[2].tag = 0x13;
    entries[2].parent = 1;
    link(1, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    begin(1, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_CYCLE);
    TbSymbolScopeFailure_t failure;
    CHECK(tb_symbol_scope_failure(&scope, &failure));
    CHECK(failure.previous_node == 0 && failure.record.entry_index == 1);
    TbSymbolScopeView_t view;
    CHECK(!tb_symbol_scope_view(&scope, &view));

    fixture(5);
    for (size_t i = 1; i < 5; ++i)
        entries[i].tag = 0x13;
    entries[2].parent = 1;
    entries[4].parent = 3;
    link(1, 4, TB_SYMBOL_RELATION_SPECIFICATION);
    link(3, 2, TB_SYMBOL_RELATION_SPECIFICATION);
    begin(1, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_CYCLE);
    CHECK(tb_symbol_scope_failure(&scope, &failure));
    CHECK(failure.previous_node == 0 && scope.node_count == 2);
}


static void check_failures_scope(void)
{
    fixture(8);
    entries[1].tag = 0x39;
    entries[2].tag = 0x39;
    entries[3].tag = 0x39;
    entries[5].tag = 0x39;
    entries[2].parent = 1;
    entries[3].parent = 2;
    entries[4].parent = 3;
    entries[5].parent = 2;
    entries[6].parent = 5;
    link(7, 4, TB_SYMBOL_RELATION_SPECIFICATION);
    link(7, 6, TB_SYMBOL_RELATION_ORIGIN);
    begin(7, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
    conflict_claim = true;
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_CONFLICT);
    TbSymbolScopeFailure_t failure;
    CHECK(tb_symbol_scope_failure(&scope, &failure) && failure.previous_node == 2);
    diamond();
    begin(6, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
    missing_claim = true;
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_DEPENDENCY);
    TbSymbolScopeView_t view;
    CHECK(!tb_symbol_scope_view(&scope, &view));
    diamond();
    begin(6, 1, 2 * PROOF_CAPACITY);
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_RESOURCE);
    diamond();
    begin(6, PROOF_CAPACITY, 1);
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_RESOURCE);
}


static void check_cross_unit_scope(void)
{
    fixture(3);
    entries[1].tag = 0x13;
    entries[2].tag = 0x13;
    entries[2].parent = 1;
    units[0].extent = (TbSymbolDwarfLength_t){
        .payload_bytes = 508,
        .total_bytes   = 512,
        .end_offset    = 512,
        .length_bytes  = 4,
        .offset_bytes  = 4
    };
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
    field(1, 0x47, TB_SYMBOL_FORM_REF_ADDR, 576);
    remote_entries[0] = (TbSymbolDieEntry_t){
        .offset     = 536,
        .end_offset = 537,
        .parent     = UINT64_MAX,
        .code       = 1,
        .tag        = 0x11,
        .children   = true
    };
    remote_entries[1] = (TbSymbolDieEntry_t){
        .offset          = 560,
        .end_offset      = 569,
        .parent          = 0,
        .code            = 2,
        .tag             = 0x13,
        .attribute_count = 1,
        .children        = true
    };
    remote_entries[2] = (TbSymbolDieEntry_t){
        .offset          = 576,
        .end_offset      = 577,
        .parent          = 1,
        .code            = 3,
        .tag             = 0x13,
        .first_attribute = 1
    };
    remote_attributes[0] = (TbSymbolDieAttribute_t){
        .name          = 0x47,
        .declared_form = TB_SYMBOL_FORM_REF_ADDR,
        .offset        = 561,
        .value         = {
            .form           = TB_SYMBOL_FORM_REF_ADDR,
            .bytes          = 8,
            .payload_offset = 561,
            .payload_bytes  = 8,
            .bits           = entries[2].offset,
            .numeric        = true
        }
    };
    remote_tree = (TbSymbolDiesView_t){
        .entries         = remote_entries,
        .attributes      = remote_attributes,
        .count           = 3,
        .attribute_count = 1,
        .section_index   = 1,
        .unit_offset     = 512,
        .abbrev_section  = 2,
        .kind            = TB_SYMBOL_DWARF_COMPILE,
        .backing         = inventory.backing
    };
    begin(1, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_CYCLE);
    CHECK(scope.node_count == 2);
    CHECK(scope.nodes[0].record.unit_offset == 0 && scope.nodes[1].record.unit_offset == 512);
    TbSymbolScopeFailure_t failure;
    CHECK(tb_symbol_scope_failure(&scope, &failure));
    CHECK(failure.previous_node == 0 && failure.record.record_offset == entries[1].offset);
}


static void check_supply_and_coverage(void)
{
    diamond();
    begin(6, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
    TbSymbolScopeNeed_t need;
    CHECK(tb_symbol_scope_need(&scope, &need));
    Proof_t *input = proof(need.record);
    TbSymbolGraphNode_t wrong = input->graph.nodes[0];
    ++wrong.record.record_offset;
    TbSymbolGraphView_t incorrect = {
        .nodes = &wrong,
        .count = input->graph.count
    };
    CHECK(tb_symbol_scope_source(&scope, &incorrect, &input->contexts) == TB_SYMBOL_SCOPE_INVALID);
    CHECK(tb_symbol_scope_need(&scope, &need));
    CHECK(need.record.entry_index == 6 && !need.context_available);
    unsigned char before[sizeof(scope)];
    memcpy(before, &scope, sizeof(before));
    CHECK(tb_symbol_scope_step(&scope, 0) == TB_SYMBOL_SCOPE_INVALID);
    CHECK(tb_symbol_scope_step(&scope, 257) == TB_SYMBOL_SCOPE_INVALID);
    CHECK(!memcmp(before, &scope, sizeof(before)));
    input->anchors[1].node_index = input->anchors[0].node_index;
    CHECK(tb_symbol_scope_source(&scope, &input->graph, &input->contexts) == TB_SYMBOL_SCOPE_PENDING);
    CHECK(tb_symbol_scope_step(&scope, 256) == TB_SYMBOL_SCOPE_INVALID);
    TbSymbolScopeFailure_t failure;
    CHECK(tb_symbol_scope_failure(&scope, &failure));
    CHECK(failure.source_node == 0 && failure.anchor_index == 1);
    CHECK(failure.record.record_offset == input->anchors[1].record.record_offset);
    TbSymbolScopeView_t view;
    CHECK(!tb_symbol_scope_view(&scope, &view));

    for (unsigned corruption = 0; corruption < 4; ++corruption) {
        diamond();
        begin(6, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
        CHECK(tb_symbol_scope_need(&scope, &need));
        input = proof(need.record);
        switch (corruption) {
        case 0:
            input->anchors[0].kind = TB_SYMBOL_CONTEXT_UNIT_PARENT;
            break;
        case 1:
            ++input->anchors[0].unit_root.record_offset;
            break;
        case 2:
            input->anchors[0].unit.header_available = false;
            break;
        case 3:
            ++input->anchors[0].parent_context.abbrev_offset;
            break;
        }
        CHECK(tb_symbol_scope_source(&scope, &input->graph, &input->contexts) ==
              TB_SYMBOL_SCOPE_PENDING);
        CHECK(tb_symbol_scope_step(&scope, 256) == TB_SYMBOL_SCOPE_INVALID);
        CHECK(!tb_symbol_scope_view(&scope, &view));
    }

    fixture(2);
    begin(1, 1, 1);
    CHECK(finish_scope(256) == TB_SYMBOL_SCOPE_READY);
    CHECK(tb_symbol_scope_view(&scope, &view) && view.node_count == 1 && view.edge_count == 1);
    CHECK(view.edges[0].target_node == SIZE_MAX);
    CHECK(view.nodes[0].contexts.anchors[0].kind == TB_SYMBOL_CONTEXT_UNIT_PARENT);
    TbSymbolReferenceTarget_t selected = view.nodes[0].record;
    memcpy(before, &scope, sizeof(before));
    CHECK(tb_symbol_scope_init(&scope, selected, scope_nodes, 0, scope_edges, 1) == TB_SYMBOL_SCOPE_RESOURCE);
    CHECK(tb_symbol_scope_init(&scope, selected, scope_nodes, 1, scope_edges, 0) == TB_SYMBOL_SCOPE_RESOURCE);
    CHECK(tb_symbol_scope_init(&scope, selected, scope_nodes, SIZE_MAX, scope_edges, 1) ==
          TB_SYMBOL_SCOPE_INVALID);
    CHECK(!memcmp(before, &scope, sizeof(before)));
}


static void check_deep_scope(void)
{
    fixture(513);
    for (size_t i = 1; i <= 512; ++i) {
        entries[i].tag = 0x39;
        entries[i].parent = i - 1;
    }
    begin(512, PROOF_CAPACITY, 2 * PROOF_CAPACITY);
    CHECK(finish_scope(1) == TB_SYMBOL_SCOPE_READY);
    TbSymbolScopeView_t view;
    CHECK(tb_symbol_scope_view(&scope, &view));
    CHECK(view.node_count == 512 && view.edge_count == 512);
    for (size_t i = 1; i < view.node_count; ++i)
        CHECK(view.nodes[i].traversal_parent == i - 1);
}


int main(void)
{
    check_sharing();
    check_mixed_cycles();
    check_failures_scope();
    check_cross_unit_scope();
    check_supply_and_coverage();
    check_deep_scope();
    puts("PASS mixed context cycles, shared completed parents and bounded scope graph construction");
    return 0;
}
