#include "symbol/locality.h"
#include "../support/check.h"

#include <string.h>

static TbSymbolScopeNode_t     nodes[520];
static TbSymbolScopeEdge_t     edges[1040];
static TbSymbolGraphNode_t     graphs[520][2];
static TbSymbolContextAnchor_t anchors[520][2];
static TbSymbolLocalitySlot_t  slots[520];
static TbSymbolScopeView_t     view;
static TbSymbolLocality_t      locality;

static TbSymbolReferenceTarget_t record(size_t index)
{
    return (TbSymbolReferenceTarget_t){
        .backing       = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 65536
        },
        .section_index = 2,
        .record_offset = 32 * (index + 1),
        .entry_index   = index + 1
    };
}


static void fixture(size_t count)
{
    CHECK(count && count <= 520);
    memset(nodes, 0, sizeof(nodes));
    memset(edges, 0, sizeof(edges));
    memset(graphs, 0, sizeof(graphs));
    memset(anchors, 0, sizeof(anchors));
    view = (TbSymbolScopeView_t){
        .nodes      = nodes,
        .node_count = count,
        .edges      = edges,
        .edge_count = count
    };
    TbSymbolReferenceTarget_t root = record(0);
    root.record_offset = 12;
    root.entry_index   = 0;
    for (size_t i = 0; i < count; ++i) {
        nodes[i] = (TbSymbolScopeNode_t){
            .record            = record(i),
            .context           = {
                .tag            = i ? 0x39 : 0x34,
                .version        = 5,
                .unit_kind      = TB_SYMBOL_DWARF_COMPILE,
                .abbrev_section = 3,
                .parent_record  = i + 1 == count ? 12 : record(i + 1).record_offset,
                .parent_tag     = i + 1 == count ? 0x11 : 0x39
            },
            .relationships     = {
                .nodes = graphs[i],
                .count = 1
            },
            .contexts          = {
                .selected = record(i),
                .anchors  = anchors[i],
                .count    = 1
            },
            .first_edge        = i,
            .context_available = true,
            .complete          = true
        };
    }
    for (size_t i = 0; i < count; ++i) {
        graphs[i][0] = (TbSymbolGraphNode_t){
            .record   = nodes[i].record,
            .context  = nodes[i].context,
            .complete = true
        };
        anchors[i][0] = (TbSymbolContextAnchor_t){
            .record         = nodes[i].record,
            .context        = nodes[i].context,
            .kind           = i + 1 == count ? TB_SYMBOL_CONTEXT_UNIT_PARENT
                                             : TB_SYMBOL_CONTEXT_LEXICAL_PARENT,
            .parent         = i + 1 == count ? root : record(i + 1),
            .parent_context = i + 1 == count ? (TbSymbolGraphContext_t){.tag = 0x11}
                                             : nodes[i + 1].context,
            .unit_root      = root
        };
        edges[i].target_node = i + 1 == count ? SIZE_MAX : i + 1;
    }
}


static void tag(size_t index, uint64_t value)
{
    nodes[index].context.tag      = value;
    graphs[index][0].context.tag  = value;
    anchors[index][0].context.tag = value;
    for (size_t i = 0; i < view.node_count; ++i) {
        for (size_t j = 0; j < nodes[i].contexts.count; ++j) {
            if (edges[nodes[i].first_edge + j].target_node == index)
                anchors[i][j].parent_context.tag = value;
        }
    }
}


static TbSymbolLocalityStatus_e complete(void)
{
    CHECK(tb_symbol_locality_init(&locality, view, slots, 520) == TB_SYMBOL_LOCALITY_PENDING);
    for (unsigned step = 0; step < 100000; ++step) {
        TbSymbolLocalityView_t result;
        CHECK(!tb_symbol_locality_view(&locality, &result));
        uint64_t progress = tb_symbol_locality_progress(&locality);
        TbSymbolLocalityStatus_e status = tb_symbol_locality_step(&locality, 1);
        CHECK(tb_symbol_locality_progress(&locality) == progress + 1);
        if (status != TB_SYMBOL_LOCALITY_PENDING)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_LOCALITY_INVALID;
}


static void check_profiles(void)
{
    fixture(1);
    CHECK(complete() == TB_SYMBOL_LOCALITY_READY);
    TbSymbolLocalityView_t result;
    CHECK(tb_symbol_locality_view(&locality, &result));
    CHECK(result.value == TB_SYMBOL_SCOPE_NONLOCAL);
    const uint64_t transparent[] = {0x39, 0x02, 0x13, 0x17};
    for (size_t i = 0; i < sizeof(transparent) / sizeof(*transparent); ++i) {
        fixture(2);
        tag(1, transparent[i]);
        CHECK(complete() == TB_SYMBOL_LOCALITY_READY);
        CHECK(tb_symbol_locality_view(&locality, &result));
        CHECK(result.value == TB_SYMBOL_SCOPE_NONLOCAL && result.slots[1].has_value);
    }
    const uint64_t barriers[] = {0x2e, 0x1d, 0x0b, 0x25, 0x32};
    for (size_t i = 0; i < sizeof(barriers) / sizeof(*barriers); ++i) {
        fixture(3);
        tag(1, barriers[i]);
        tag(2, 0x4000);
        CHECK(complete() == TB_SYMBOL_LOCALITY_READY);
        CHECK(tb_symbol_locality_view(&locality, &result));
        CHECK(result.value == TB_SYMBOL_SCOPE_LOCAL);
        CHECK(result.slots[1].visit == TB_SYMBOL_LOCALITY_UNVISITED);
        CHECK(result.slots[2].visit == TB_SYMBOL_LOCALITY_UNVISITED);
    }
    fixture(4);
    tag(2, 0x02);
    tag(3, 0x2e);
    CHECK(complete() == TB_SYMBOL_LOCALITY_READY);
    CHECK(tb_symbol_locality_view(&locality, &result));
    CHECK(result.value == TB_SYMBOL_SCOPE_LOCAL);

    fixture(2);
    tag(1, 0x4000);
    CHECK(complete() == TB_SYMBOL_LOCALITY_UNSUPPORTED);
    TbSymbolLocalityFailure_t failure;
    CHECK(tb_symbol_locality_failure(&locality, &failure));
    CHECK(failure.reason == TB_SYMBOL_LOCALITY_PARENT_TAG && failure.parent_tag == 0x4000);
}


static void diamond(void)
{
    fixture(4);
    for (size_t i = 3; i > 0; --i) {
        edges[i + 1] = edges[i];
        nodes[i].first_edge = i + 1;
    }
    view.edge_count = 5;
    edges[0].target_node = 1;
    edges[1] = (TbSymbolScopeEdge_t){
        .anchor_index = 1,
        .target_node  = 2
    };
    edges[2].target_node = 3;
    anchors[1][0].parent = nodes[3].record;
    anchors[1][0].parent_context = nodes[3].context;
    graphs[0][1] = graphs[0][0];
    graphs[0][1].record = record(100);
    anchors[0][1] = anchors[0][0];
    anchors[0][1].record = graphs[0][1].record;
    anchors[0][1].parent = nodes[2].record;
    anchors[0][1].parent_context = nodes[2].context;
    nodes[0].contexts.count = 2;
    nodes[0].relationships.count = 2;
}


static void check_derivations(void)
{
    diamond();
    CHECK(complete() == TB_SYMBOL_LOCALITY_READY);
    TbSymbolLocalityView_t result;
    CHECK(tb_symbol_locality_view(&locality, &result));
    CHECK(result.value == TB_SYMBOL_SCOPE_NONLOCAL);
    CHECK(result.scope.node_count == 4 && result.scope.edge_count == 5);
    CHECK(result.slots[1].visit == TB_SYMBOL_LOCALITY_COMPLETE);
    CHECK(result.slots[2].visit == TB_SYMBOL_LOCALITY_COMPLETE);
    CHECK(result.slots[3].visit == TB_SYMBOL_LOCALITY_COMPLETE);
    diamond();
    tag(2, 0x2e);
    CHECK(complete() == TB_SYMBOL_LOCALITY_CONFLICT);
    TbSymbolLocalityFailure_t failure;
    CHECK(tb_symbol_locality_failure(&locality, &failure));
    CHECK(failure.reason == TB_SYMBOL_LOCALITY_AGREEMENT && failure.node_index == 0);
    CHECK(failure.first_anchor == 0 && failure.anchor_index == 1);
    CHECK(!tb_symbol_locality_view(&locality, &result));

    fixture(2);
    tag(0, 0x39);
    edges[1].target_node = 0;
    anchors[1][0].kind = TB_SYMBOL_CONTEXT_LEXICAL_PARENT;
    anchors[1][0].parent = nodes[0].record;
    anchors[1][0].parent_context = nodes[0].context;
    CHECK(complete() == TB_SYMBOL_LOCALITY_INVALID);
    CHECK(tb_symbol_locality_failure(&locality, &failure) && failure.reason == TB_SYMBOL_LOCALITY_CYCLE);
}


static void check_boundaries(void)
{
    fixture(512);
    CHECK(complete() == TB_SYMBOL_LOCALITY_READY);
    TbSymbolLocalityView_t result;
    CHECK(tb_symbol_locality_view(&locality, &result));
    CHECK(result.value == TB_SYMBOL_SCOPE_NONLOCAL && result.count == 512);
    unsigned char before[sizeof(locality)];
    memcpy(before, &locality, sizeof(before));
    CHECK(tb_symbol_locality_init(&locality, view, slots, 511) == TB_SYMBOL_LOCALITY_RESOURCE);
    CHECK(!memcmp(before, &locality, sizeof(before)));
    CHECK(tb_symbol_locality_step(&locality, 0) == TB_SYMBOL_LOCALITY_INVALID);
    CHECK(!memcmp(before, &locality, sizeof(before)));
    CHECK(tb_symbol_locality_init(&locality, view, slots, SIZE_MAX / sizeof(*slots) + 1) ==
          TB_SYMBOL_LOCALITY_INVALID);
    CHECK(!memcmp(before, &locality, sizeof(before)));
    view.node_count = SIZE_MAX / sizeof(*view.nodes) + 1;
    CHECK(tb_symbol_locality_init(&locality, view, slots, 520) == TB_SYMBOL_LOCALITY_INVALID);
    CHECK(!memcmp(before, &locality, sizeof(before)));
    fixture(1);
    CHECK(tb_symbol_locality_init(&locality, view, slots, 0) == TB_SYMBOL_LOCALITY_RESOURCE);
    CHECK(!memcmp(before, &locality, sizeof(before)));
    view.edge_count = SIZE_MAX / sizeof(*view.edges) + 1;
    CHECK(tb_symbol_locality_init(&locality, view, slots, 520) == TB_SYMBOL_LOCALITY_INVALID);
    CHECK(!memcmp(before, &locality, sizeof(before)));
}


static void check_invalid_sources(void)
{
    TbSymbolLocalityFailure_t failure;
    fixture(2);
    nodes[1].record.record_offset++;
    CHECK(complete() == TB_SYMBOL_LOCALITY_INVALID);
    CHECK(tb_symbol_locality_failure(&locality, &failure));
    CHECK(failure.reason == TB_SYMBOL_LOCALITY_SCOPE && failure.node_index == 0);
    fixture(2);
    edges[0].anchor_index = 1;
    CHECK(complete() == TB_SYMBOL_LOCALITY_INVALID);
    fixture(2);
    anchors[0][0].parent_context.tag = 0x2e;
    CHECK(complete() == TB_SYMBOL_LOCALITY_INVALID);
    fixture(1);
    edges[0].target_node = 0;
    CHECK(complete() == TB_SYMBOL_LOCALITY_INVALID);
    fixture(2);
    nodes[1].complete = false;
    CHECK(complete() == TB_SYMBOL_LOCALITY_INVALID);
    fixture(1);
    nodes[0].contexts.selected.backing.key.inode++;
    CHECK(complete() == TB_SYMBOL_LOCALITY_INVALID);
    CHECK(!tb_symbol_locality_view(&locality, &(TbSymbolLocalityView_t){0}));
}


static void check_restart(void)
{
    fixture(4);
    CHECK(complete() == TB_SYMBOL_LOCALITY_READY);
    uint64_t total = tb_symbol_locality_progress(&locality);
    TbSymbolScopeNode_t original[4];
    memcpy(original, nodes, sizeof(original));
    for (uint64_t cancel = 0; cancel <= total; ++cancel) {
        CHECK(tb_symbol_locality_init(&locality, view, slots, 520) == TB_SYMBOL_LOCALITY_PENDING);
        for (uint64_t step = 0; step < cancel; ++step)
            (void)tb_symbol_locality_step(&locality, 1);
        TbSymbolLocalityView_t result;
        CHECK(tb_symbol_locality_view(&locality, &result) == (cancel == total));
        CHECK(!memcmp(original, nodes, sizeof(original)));
        /* Dropping caller-owned state needs no cleanup and leaves borrowed
         * sources intact. Reuse must initialize every slot before traversal. */
        memset(&locality, 0xa5, sizeof(locality));
    }
    CHECK(complete() == TB_SYMBOL_LOCALITY_READY);
    CHECK(tb_symbol_locality_init(&locality, view, slots, 520) == TB_SYMBOL_LOCALITY_PENDING);
    locality.progress = UINT64_MAX;
    CHECK(tb_symbol_locality_step(&locality, 1) == TB_SYMBOL_LOCALITY_INVALID);
    TbSymbolLocalityFailure_t failure;
    CHECK(tb_symbol_locality_failure(&locality, &failure));
    CHECK(failure.reason == TB_SYMBOL_LOCALITY_PROGRESS && failure.first_anchor == SIZE_MAX);
}


int main(void)
{
    check_profiles();
    check_derivations();
    check_boundaries();
    check_invalid_sources();
    check_restart();
    puts("PASS logical declaration locality: profiles, barriers, derivations, bounds and depth");
    return 0;
}
