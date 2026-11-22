#include "symbol/inherited_reference.h"
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
        .extent            = {
            .total_bytes   = 1024 + 64 * count,
            .payload_bytes = 1020 + 64 * count,
            .end_offset    = 1024 + 64 * count,
            .length_bytes  = 4,
            .offset_bytes  = 4
        },
        .header            = {
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
        .entries        = &unit,
        .count          = 1,
        .section_index  = 1,
        .kind           = TB_SYMBOL_DWARF_INFO,
        .backing        = {
            .key        = {
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
        .name              = name,
        .declared_form     = form,
        .offset            = entries[node].offset + 1 + index * 4,
        .value             = {
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


static TbSymbolInheritedReference_t     reference;
static TbSymbolInheritSlot_t            slots[NODES];
static TbSymbolInheritedReferenceFact_t facts[NODES];

static void begin(uint64_t name)
{
    seal();
    CHECK(tb_symbol_inherited_reference_init(&reference, view, name, slots, NODES,
                                              facts, NODES, &units, NULL, 0) ==
          TB_SYMBOL_INHERITED_REFERENCE_PENDING);
}


static TbSymbolInheritedReferenceStatus_e run(void)
{
    for (unsigned turn = 0; turn < 100000; ++turn) {
        TbSymbolInheritedReferenceSource_t source;
        if (tb_symbol_inherited_reference_source_need(&reference, &source)) {
            ++supplied;
            (void)tb_symbol_inherited_reference_source(&reference, &units, &tree, 0);
        }
        TbSymbolReferenceNeed_t need;
        if (tb_symbol_inherited_reference_target_need(&reference, &need))
            (void)tb_symbol_inherited_reference_target(&reference, &tree);
        TbSymbolInheritedReferenceStatus_e status = tb_symbol_inherited_reference_step(&reference, 1);
        if (status != TB_SYMBOL_INHERITED_REFERENCE_PENDING &&
            status != TB_SYMBOL_INHERITED_REFERENCE_SOURCE && status != TB_SYMBOL_INHERITED_REFERENCE_TARGET)
            return status;
        CHECK(!tb_symbol_inherited_reference_view(&reference, &(TbSymbolInheritedReferenceView_t){0}));
    }
    CHECK(false);
    return TB_SYMBOL_INHERITED_REFERENCE_INVALID;
}


static void check_targets(void)
{
    fixture(3);
    link_nodes(0, 1, TB_SYMBOL_RELATION_ORIGIN);
    add_field(1, 0x49, TB_SYMBOL_FORM_REF4, entries[2].offset);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_FOUND);
    CHECK(supplied);
    TbSymbolInheritedReferenceView_t result;
    CHECK(tb_symbol_inherited_reference_view(&reference, &result));
    CHECK(result.count == 1 && result.facts == facts && result.name == 0x49);
    CHECK(facts[0].node_index == 1 && facts[0].target.entry_index == 2);
    CHECK(facts[0].reference.source_record == entries[1].offset);
    CHECK(facts[0].reference.source_attribute == fields[1][0].offset);

    for (unsigned equal = 0; equal < 2; ++equal) {
        fixture(5);
        link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
        link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
        add_field(1, 0x49, TB_SYMBOL_FORM_REF4, entries[3].offset);
        add_field(2, 0x49, TB_SYMBOL_FORM_REF4, entries[equal ? 3 : 4].offset);
        begin(0x49);
        CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_FOUND);
        CHECK(tb_symbol_inherited_reference_view(&reference, &result) && result.count == 2);
        CHECK(facts[0].node_index != facts[1].node_index);
        CHECK((facts[0].target.record_offset == facts[1].target.record_offset) == (equal != 0));
    }

    fixture(2);
    add_field(0, 0x64, TB_SYMBOL_FORM_REF4, entries[1].offset);
    begin(0x64);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_FOUND);
    CHECK(tb_symbol_inherited_reference_view(&reference, &result) && result.count == 1);
    CHECK(facts[0].target.entry_index == 1 && result.name == 0x64);
}


static void check_failures(void)
{
    fixture(1);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_ABSENT);
    TbSymbolInheritedReferenceView_t absence;
    CHECK(tb_symbol_inherited_reference_view(&reference, &absence));
    CHECK(!absence.count && absence.graph.nodes == nodes && absence.slots == slots);
    CHECK(absence.slots[0].visit == TB_SYMBOL_INHERIT_EXAMINED);
    fixture(1);
    add_field(0, 0x49, TB_SYMBOL_FORM_DATA4, 12);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_MALFORMED);
    fixture(1);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF4, 13);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_MALFORMED);
    fixture(1);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF_SUP4, 12);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED);
    fixture(1);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF_SIG8, 42);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY);
    fixture(1);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF4, 12);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF4, 12);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_CONFLICT);
    TbSymbolInheritedReferenceFailure_t failure;
    CHECK(tb_symbol_inherited_reference_failure(&reference, &failure));
    CHECK(failure.owner.selection.matches == 2);
}


static void check_foreign_targets(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        fixture(2);
        if (!mode) {
            link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
            fields[0][0].declared_form = TB_SYMBOL_FORM_REF_ADDR;
            fields[0][0].value.form = TB_SYMBOL_FORM_REF_ADDR;
            fields[0][0].value.bits = 512;
            add_field(1, 0x49, TB_SYMBOL_FORM_REF4, 44);
        } else {
            add_field(0, 0x49, mode == 1 ? TB_SYMBOL_FORM_REF_ADDR : TB_SYMBOL_FORM_REF_SIG8,
                      mode == 1 ? 544 : 0x1234);
        }
        size_t cursor = 0;
        for (size_t i = 0; i < tree.count; ++i) {
            entries[i].first_attribute = cursor;
            for (size_t j = 0; j < entries[i].attribute_count; ++j)
                attributes[cursor++] = fields[i][j];
        }
        tree.attribute_count = cursor;
        TbSymbolDieEntry_t foreign[2] = {entries[1], entries[1]};
        foreign[0].offset = mode == 2 ? 524 : 512;
        foreign[0].end_offset = mode == 2 ? 525 : 520;
        if (mode == 2) {
            foreign[0].tag = 0x41;
            foreign[0].children = true;
            foreign[1].parent = 0;
        }
        foreign[1].offset = 544;
        foreign[1].end_offset = 552;
        foreign[1].first_attribute = cursor;
        foreign[1].attribute_count = 0;
        foreign[1].tag = 0x24;
        if (!mode) {
            attributes[foreign[0].first_attribute].offset = 513;
            attributes[foreign[0].first_attribute].value.payload_offset = 513;
        }
        TbSymbolDiesView_t remote = tree;
        remote.entries = foreign;
        remote.count = 2;
        remote.unit_offset = 500;
        remote.abbrev_offset = 100;
        remote.kind = mode == 2 ? TB_SYMBOL_DWARF_TYPE : TB_SYMBOL_DWARF_COMPILE;
        TbSymbolUnitEntry_t headers[2] = {unit, unit};
        headers[0].extent.end_offset = 500;
        headers[0].extent.total_bytes = 500;
        headers[0].extent.payload_bytes = 496;
        headers[0].header.extent = headers[0].extent;
        headers[1].offset = 500;
        headers[1].extent = headers[0].extent;
        headers[1].extent.end_offset = 1000;
        headers[1].header.extent = headers[1].extent;
        headers[1].header.kind = remote.kind;
        headers[1].header.unit_type = mode == 2 ? 2 : 1;
        headers[1].header.signature = 0x1234;
        headers[1].header.has_signature = mode == 2;
        headers[1].header.header_bytes = mode == 2 ? 24 : 12;
        headers[1].header.type_offset = 44;
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
                    bool is_foreign = need.record.unit_offset == 500;
                    CHECK(tb_symbol_graph_source(&graph, &inventory, is_foreign ? &remote : &tree,
                                                  is_foreign ? 1 : 0) == TB_SYMBOL_GRAPH_PENDING);
                } else {
                    CHECK(tb_symbol_graph_target(&graph, &inventory, &remote) == TB_SYMBOL_GRAPH_PENDING);
                }
            }
            (void)tb_symbol_graph_step(&graph, 7);
        }
        CHECK(tb_symbol_graph_view(&graph, &view));
        CHECK(tb_symbol_inherited_reference_init(&reference, view, 0x49, slots, NODES,
                                                  facts, NODES, &inventory, &inventory, 1) ==
              TB_SYMBOL_INHERITED_REFERENCE_PENDING);
        for (unsigned turn = 0; turn < 1000; ++turn) {
            TbSymbolInheritedReferenceSource_t source;
            if (tb_symbol_inherited_reference_source_need(&reference, &source)) {
                bool is_foreign = source.source.record.unit_offset == 500;
                (void)tb_symbol_inherited_reference_source(&reference, &inventory,
                                                           is_foreign ? &remote : &tree, is_foreign ? 1 : 0);
            }
            TbSymbolReferenceNeed_t need;
            if (tb_symbol_inherited_reference_target_need(&reference, &need)) {
                CHECK(need.unit_offset == 500 && need.record_offset == 544);
                unsigned char before[sizeof(reference)];
                memcpy(before, &reference, sizeof(before));
                CHECK(tb_symbol_inherited_reference_target(&reference, &tree) ==
                      TB_SYMBOL_INHERITED_REFERENCE_INVALID);
                CHECK(!memcmp(before, &reference, sizeof(before)));
                CHECK(tb_symbol_inherited_reference_target(&reference, &remote) ==
                      TB_SYMBOL_INHERITED_REFERENCE_PENDING);
            }
            TbSymbolInheritedReferenceStatus_e status = tb_symbol_inherited_reference_step(&reference, 1);
            if (status == TB_SYMBOL_INHERITED_REFERENCE_FOUND)
                break;
            CHECK(status == TB_SYMBOL_INHERITED_REFERENCE_PENDING ||
                  status == TB_SYMBOL_INHERITED_REFERENCE_SOURCE ||
                  status == TB_SYMBOL_INHERITED_REFERENCE_TARGET);
        }
        TbSymbolInheritedReferenceView_t result;
        CHECK(tb_symbol_inherited_reference_view(&reference, &result) && result.count == 1);
        CHECK(facts[0].target.unit_offset == 500 && facts[0].target.record_offset == 544);
        CHECK(facts[0].target.entry_index == 1);
        CHECK(facts[0].reference.source_unit == (mode ? 0 : 500));
    }
}


static void check_replaced_owner(void)
{
    fixture(2);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF4, entries[1].offset);
    begin(0x49);
    TbSymbolInheritedReferenceSource_t source;
    for (unsigned turn = 0; turn < 1000; ++turn) {
        if (tb_symbol_inherited_reference_source_need(&reference, &source)) {
            if (source.kind == TB_SYMBOL_INHERITED_REFERENCE_VALUE_SOURCE)
                break;
            CHECK(tb_symbol_inherited_reference_source(&reference, &units, &tree, 0) ==
                  TB_SYMBOL_INHERITED_REFERENCE_PENDING);
        }
        CHECK(tb_symbol_inherited_reference_step(&reference, 1) == TB_SYMBOL_INHERITED_REFERENCE_PENDING ||
              reference.status == TB_SYMBOL_INHERITED_REFERENCE_SOURCE);
    }
    CHECK(tb_symbol_inherited_reference_source_need(&reference, &source));
    CHECK(source.kind == TB_SYMBOL_INHERITED_REFERENCE_VALUE_SOURCE);
    unsigned char before[sizeof(reference)];
    memcpy(before, &reference, sizeof(before));
    ++attributes[0].value.bits;
    CHECK(tb_symbol_inherited_reference_source(&reference, &units, &tree, 0) ==
          TB_SYMBOL_INHERITED_REFERENCE_INVALID);
    CHECK(!memcmp(before, &reference, sizeof(before)));
    --attributes[0].value.bits;
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_FOUND);
}


static void check_atomic_conflict(void)
{
    fixture(4);
    link_nodes(0, 1, TB_SYMBOL_RELATION_SPECIFICATION);
    link_nodes(0, 2, TB_SYMBOL_RELATION_ORIGIN);
    add_field(1, 0x49, TB_SYMBOL_FORM_REF4, entries[3].offset);
    add_field(2, 0x49, TB_SYMBOL_FORM_REF_SIG8, 42);
    begin(0x49);
    TbSymbolUnitEntry_t headers[3] = {unit, unit, unit};
    headers[0].extent.end_offset = 500;
    headers[0].extent.total_bytes = 500;
    headers[0].extent.payload_bytes = 496;
    headers[0].header.extent = headers[0].extent;
    for (size_t i = 1; i < 3; ++i) {
        headers[i].offset = 500 * i;
        headers[i].extent = headers[0].extent;
        headers[i].extent.end_offset = 500 * (i + 1);
        headers[i].header.extent = headers[i].extent;
        headers[i].header.kind = TB_SYMBOL_DWARF_TYPE;
        headers[i].header.unit_type = 2;
        headers[i].header.header_bytes = 24;
        headers[i].header.signature = 42;
        headers[i].header.has_signature = true;
        headers[i].header.type_offset = 32;
    }
    TbSymbolUnitsView_t inventory = units;
    inventory.entries = headers;
    inventory.count = 3;
    CHECK(tb_symbol_inherited_reference_init(&reference, view, 0x49, slots, NODES,
                                              facts, NODES, NULL, &inventory, 1) ==
          TB_SYMBOL_INHERITED_REFERENCE_PENDING);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_CONFLICT);
    CHECK(reference.count == 1);
    TbSymbolInheritedReferenceView_t result;
    CHECK(!tb_symbol_inherited_reference_view(&reference, &result));
    TbSymbolInheritedReferenceFailure_t failure;
    CHECK(tb_symbol_inherited_reference_failure(&reference, &failure));
    CHECK(failure.reference_available && failure.field_available && !failure.target_available);
    CHECK(failure.reference_status == TB_SYMBOL_REFERENCE_CONFLICT);
}


static void check_bounds_and_restart(void)
{
    fixture(2);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF4, entries[1].offset);
    begin(0x49);
    unsigned char before[sizeof(reference)];
    memcpy(before, &reference, sizeof(before));
    CHECK(tb_symbol_inherited_reference_init(&reference, view, 0x49, slots, NODES,
                                              facts, 0, NULL, NULL, 0) ==
          TB_SYMBOL_INHERITED_REFERENCE_RESOURCE);
    CHECK(!memcmp(before, &reference, sizeof(before)));
    CHECK(tb_symbol_inherited_reference_init(&reference, view, 0x03, slots, NODES,
                                              facts, NODES, NULL, NULL, 0) ==
          TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED);
    CHECK(!memcmp(before, &reference, sizeof(before)));
    CHECK(tb_symbol_inherited_reference_step(&reference, 0) == TB_SYMBOL_INHERITED_REFERENCE_INVALID);
    CHECK(tb_symbol_inherited_reference_step(&reference, 257) == TB_SYMBOL_INHERITED_REFERENCE_INVALID);
    CHECK(!memcmp(before, &reference, sizeof(before)));
    CHECK(tb_symbol_inherited_reference_init(&reference, view, 0x49, slots, NODES,
                                              facts, SIZE_MAX / sizeof(*facts) + 1, NULL, NULL, 0) ==
          TB_SYMBOL_INHERITED_REFERENCE_INVALID);
    CHECK(!memcmp(before, &reference, sizeof(before)));
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_FOUND);
    memcpy(before, &reference, sizeof(before));
    unsigned char previous_slots[sizeof(slots)];
    unsigned char previous_facts[sizeof(facts)];
    memcpy(previous_slots, slots, sizeof(slots));
    memcpy(previous_facts, facts, sizeof(facts));
    CHECK(tb_symbol_inherited_reference_init(&reference, view, 0x49, slots, NODES,
                                              facts, 0, NULL, NULL, 0) ==
          TB_SYMBOL_INHERITED_REFERENCE_RESOURCE);
    CHECK(!memcmp(before, &reference, sizeof(before)));
    CHECK(!memcmp(previous_slots, slots, sizeof(slots)));
    CHECK(!memcmp(previous_facts, facts, sizeof(facts)));
    TbSymbolInheritedReferenceView_t previous;
    CHECK(tb_symbol_inherited_reference_view(&reference, &previous) && previous.count == 1);
    uint64_t total = tb_symbol_inherited_reference_progress(&reference);
    for (uint64_t cancel = 0; cancel <= total; ++cancel) {
        CHECK(tb_symbol_inherited_reference_init(&reference, view, 0x49, slots, NODES,
                                                  facts, NODES, NULL, NULL, 0) ==
              TB_SYMBOL_INHERITED_REFERENCE_PENDING);
        for (uint64_t step = 0; step < cancel; ++step) {
            TbSymbolInheritedReferenceSource_t source;
            if (tb_symbol_inherited_reference_source_need(&reference, &source))
                CHECK(tb_symbol_inherited_reference_source(&reference, &units, &tree, 0) ==
                      TB_SYMBOL_INHERITED_REFERENCE_PENDING);
            (void)tb_symbol_inherited_reference_step(&reference, 1);
        }
        TbSymbolInheritedReferenceView_t result;
        CHECK(tb_symbol_inherited_reference_view(&reference, &result) == (cancel == total));
        memset(&reference, 0xa5, sizeof(reference));
    }
    begin(0x49);
    reference.progress = UINT64_MAX;
    CHECK(tb_symbol_inherited_reference_step(&reference, 1) == TB_SYMBOL_INHERITED_REFERENCE_INVALID);
}


static void check_reference_forms(void)
{
    const uint64_t forms[] = {TB_SYMBOL_FORM_REF1, TB_SYMBOL_FORM_REF2, TB_SYMBOL_FORM_REF4,
                              TB_SYMBOL_FORM_REF8, TB_SYMBOL_FORM_REF_UDATA};
    const uint64_t widths[] = {1, 2, 4, 8, 1};
    for (size_t i = 0; i < sizeof(forms) / sizeof(*forms); ++i) {
        for (unsigned indirect = 0; indirect < 2; ++indirect) {
            fixture(2);
            add_field(0, 0x49, forms[i], entries[1].offset);
            fields[0][0].value.bytes = widths[i] + indirect;
            fields[0][0].value.payload_bytes = widths[i];
            fields[0][0].value.indirect = indirect != 0;
            fields[0][0].value.payload_offset += indirect;
            if (indirect)
                fields[0][0].declared_form = TB_SYMBOL_FORM_INDIRECT;
            begin(0x49);
            CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_FOUND);
            TbSymbolInheritedReferenceView_t result;
            CHECK(tb_symbol_inherited_reference_view(&reference, &result) && result.count == 1);
            CHECK(facts[0].target.entry_index == 1 && facts[0].reference.form == forms[i]);
            CHECK(facts[0].attribute.value.indirect == (indirect != 0));
        }
    }
    for (unsigned invalid = 0; invalid < 2; ++invalid) {
        fixture(2);
        add_field(0, 0x49, TB_SYMBOL_FORM_REF4, entries[1].offset);
        if (invalid)
            fields[0][0].value.is_signed = true;
        else
            fields[0][0].value.numeric = false;
        begin(0x49);
        CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_MALFORMED);
        TbSymbolInheritedReferenceFailure_t failure;
        CHECK(tb_symbol_inherited_reference_failure(&reference, &failure));
        CHECK(failure.field_available && !failure.reference_available);
        CHECK(failure.field.selection.matches == 1 && failure.field.attribute.name == 0x49);
    }
    fixture(1);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF4, 0);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_MALFORMED);
    fixture(1);
    add_field(0, 0x49, TB_SYMBOL_FORM_REF_SIG8, 0);
    begin(0x49);
    CHECK(run() == TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY);
}


int main(void)
{
    check_targets();
    check_failures();
    check_foreign_targets();
    check_replaced_owner();
    check_atomic_conflict();
    check_bounds_and_restart();
    check_reference_forms();
    puts("PASS inherited references: exact targets, independent owners and typed failures");
    return 0;
}
