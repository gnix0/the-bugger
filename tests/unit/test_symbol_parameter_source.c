#include "symbol/parameter_source.h"
#include "../support/check.h"

#include <string.h>

#define SCOPES  4
#define ENTRIES 520

static TbSymbolGraphNode_t           nodes[SCOPES];
static TbSymbolDieEntry_t            entries[SCOPES][ENTRIES];
static TbSymbolDieAttribute_t        attributes[SCOPES][3];
static TbSymbolUnitEntry_t           headers[SCOPES];
static TbSymbolUnitsView_t           units[SCOPES];
static TbSymbolDiesView_t            trees[SCOPES];
static TbSymbolParametersView_t      inventories[SCOPES];
static TbSymbolParameter_t           parameters[SCOPES][ENTRIES];
static TbSymbolParameterLinkFact_t   facts[SCOPES * ENTRIES];
static TbSymbolParameterLink_t       links[SCOPES * ENTRIES];
static TbSymbolParameterLinksView_t  correspondence;
static TbSymbolParameterSourceSlot_t slots[SCOPES];
static TbSymbolParameterSource_t     state;
static size_t                        count;

/* Authored complete child views exercise selection separately from the native
 * encoded graph/inventory/correspondence composition. */
static void fixture(size_t scopes)
{
    memset(nodes, 0, sizeof(nodes));
    memset(entries, 0, sizeof(entries));
    memset(attributes, 0, sizeof(attributes));
    memset(facts, 0, sizeof(facts));
    memset(links, 0, sizeof(links));
    count = scopes;

    for (size_t i = 0; i < count; ++i) {
        const uint64_t tags[] = {0x11, 0x02, 0x2e, 0x05, 0x16, 0x05, 0x18, 0x2e};
        const uint64_t parents[] = {UINT64_MAX, 0, 1, 2, 2, 2, 2, 0};
        for (size_t j = 0; j < 8; ++j) {
            entries[i][j] = (TbSymbolDieEntry_t) {
                .offset = 64 * i + 16 + j * 8,
                .parent = parents[j],
                .tag    = tags[j]
            };
        }

        trees[i] = (TbSymbolDiesView_t) {
            .entries        = entries[i],
            .attributes     = attributes[i],
            .count          = 8,
            .section_index  = 2,
            .unit_offset    = 64 * i,
            .abbrev_section = 3,
            .abbrev_offset  = 32 * i,
            .kind           = TB_SYMBOL_DWARF_COMPILE,
            .backing        = {
                .key   = {
                    .device = 1,
                    .inode  = 2
                },
                .bytes = 65536
            }
        };
        headers[i] = (TbSymbolUnitEntry_t) {
            .offset           = trees[i].unit_offset,
            .header           = {
                .version = 5,
                .kind    = TB_SYMBOL_DWARF_COMPILE
            },
            .header_available = true
        };
        units[i] = (TbSymbolUnitsView_t) {
            .entries       = &headers[i],
            .count         = 1,
            .section_index = trees[i].section_index,
            .backing       = trees[i].backing
        };
        nodes[i] = (TbSymbolGraphNode_t) {
            .record = {
                .backing       = trees[i].backing,
                .section_index = trees[i].section_index,
                .unit_offset   = trees[i].unit_offset,
                .record_offset = entries[i][2].offset,
                .entry_index   = 2
            },
            .context = {
                .tag            = 0x2e,
                .version        = 5,
                .unit_kind      = TB_SYMBOL_DWARF_COMPILE,
                .abbrev_section = trees[i].abbrev_section,
                .abbrev_offset  = trees[i].abbrev_offset,
                .parent_record  = entries[i][1].offset,
                .parent_tag     = 0x02
            },
            .complete = true
        };
    }

    correspondence = (TbSymbolParameterLinksView_t) {
        .callables = {
            .nodes = nodes,
            .count = count
        },
        .scopes    = inventories,
        .facts     = facts,
        .links     = links
    };
}


static void relationship(size_t source, unsigned kind, size_t target)
{
    nodes[source].present |= 1u << kind;
    nodes[source].edges[kind].target = nodes[target].record;
}


static void reduction(size_t scope)
{
    trees[scope].attribute_count = entries[scope][1].attribute_count = 1;
    attributes[scope][0] = (TbSymbolDieAttribute_t) {
        .name  = 0x69,
        .value = {
            .form    = TB_SYMBOL_FORM_REF_SIG8,
            .bits    = 7,
            .numeric = true
        }
    };
}


static void inventories_build(void)
{
    correspondence.fact_count = 0;
    correspondence.link_count = 0;
    for (size_t i = 0; i < count; ++i) {
        TbSymbolParameters_t inventory;
        CHECK(tb_symbol_parameters_init(&inventory, trees[i], 2, parameters[i], ENTRIES) ==
              TB_SYMBOL_PARAMETERS_PENDING);
        while (inventory.status == TB_SYMBOL_PARAMETERS_PENDING)
            (void)tb_symbol_parameters_step(&inventory, 31);
        CHECK(tb_symbol_parameters_view(&inventory, &inventories[i]));

        for (size_t j = 0; j < inventories[i].count; ++j) {
            if (parameters[i][j].unspecified)
                continue;

            facts[correspondence.fact_count++] = (TbSymbolParameterLinkFact_t) {
                .scope_index     = i,
                .parameter_index = j
            };
        }
    }
}


static void correspondence_to(size_t target)
{
    for (size_t i = 0; i < correspondence.fact_count; ++i) {
        TbSymbolParameterLinkFact_t *fact = &facts[i];
        fact->first_link = correspondence.link_count;
        if (fact->scope_index == target)
            continue;

        links[correspondence.link_count++] = (TbSymbolParameterLink_t) {
            .source_scope     = fact->scope_index,
            .source_parameter = fact->parameter_index,
            .target_scope     = target,
            .target_parameter = fact->parameter_index
        };
        fact->link_count = 1;
    }

    CHECK(tb_symbol_parameter_source_init(&state, correspondence, slots, SCOPES) ==
          TB_SYMBOL_PARAMETER_SOURCE_PENDING);
}


static TbSymbolParameterSourceStatus_e run(size_t slice)
{
    for (unsigned i = 0; i < 1000000; ++i) {
        TbSymbolParameterSourceView_t view;
        CHECK(!tb_symbol_parameter_source_view(&state, &view));
        if (state.status == TB_SYMBOL_PARAMETER_SOURCE_SOURCE) {
            TbSymbolGraphNeed_t need;
            CHECK(tb_symbol_parameter_source_need(&state, &need));
            size_t index = state.scope_index;
            CHECK(need.record.unit_offset == trees[index].unit_offset);
            CHECK(tb_symbol_parameter_source_source(&state, &units[index], &trees[index], 0) ==
                  TB_SYMBOL_PARAMETER_SOURCE_PENDING);
        }

        uint64_t                         before = tb_symbol_parameter_source_progress(&state);
        TbSymbolParameterSourceStatus_e status = tb_symbol_parameter_source_step(&state, slice);
        CHECK(tb_symbol_parameter_source_progress(&state) > before);
        CHECK(tb_symbol_parameter_source_progress(&state) - before <= slice);
        if (status != TB_SYMBOL_PARAMETER_SOURCE_PENDING && status != TB_SYMBOL_PARAMETER_SOURCE_SOURCE)
            return status;
    }

    CHECK(false);
    return TB_SYMBOL_PARAMETER_SOURCE_INVALID;
}


static void start(void)
{
    inventories_build();
    CHECK(tb_symbol_parameter_source_init(&state, correspondence, slots, SCOPES) ==
          TB_SYMBOL_PARAMETER_SOURCE_PENDING);
}


static void selected(size_t index, unsigned reasons)
{
    TbSymbolParameterSourceView_t view;
    CHECK(tb_symbol_parameter_source_view(&state, &view));
    CHECK(view.selected_scope == index && view.reasons == reasons);
    CHECK(view.slots == slots && view.correspondence.scopes == inventories);
}


static void check_selection(void)
{
    fixture(1);
    start();
    CHECK(run(1) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(0, 0);
    CHECK(slots[0].variadic && !slots[0].reduced);

    fixture(2);
    relationship(0, TB_SYMBOL_RELATION_SPECIFICATION, 1);
    start();
    correspondence_to(1);
    CHECK(run(7) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(1, 0);

    /* The definition remains a usable source when the declaration sits in a
     * signature container. Both reduced and retained-child declarations occur. */
    for (unsigned retain = 0; retain < 2; ++retain) {
        fixture(2);
        reduction(1);
        relationship(0, TB_SYMBOL_RELATION_SPECIFICATION, 1);
        if (!retain) {
            trees[1].count = 3;
        }
        start();
        CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_READY);
        selected(0, 0);
        CHECK(slots[1].reduced && slots[1].first_signature == 1);
        CHECK(slots[1].signature_attribute == 0 && slots[1].selected_scope == SIZE_MAX);
    }

    fixture(3);
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    relationship(1, TB_SYMBOL_RELATION_SPECIFICATION, 2);
    trees[0].count = 3; /* Concrete children may all be omitted. */
    start();
    correspondence_to(2);
    CHECK(run(256) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(2, 0);

    fixture(2);
    nodes[0].context.tag = entries[0][2].tag = 0x1d;
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    start();
    correspondence_to(1);
    CHECK(run(1) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(1, 0);

    fixture(2);
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    entries[0][5].tag = 0x34; /* One source parameter omitted in the concrete list. */
    start();
    correspondence_to(1);
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(1, 0);
    CHECK(inventories[0].count == 2 && inventories[1].count == 3);

    fixture(1);
    nodes[0].context.tag = entries[0][2].tag = 0x1d;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_PARTIAL);
    selected(SIZE_MAX, TB_SYMBOL_PARAMETER_SOURCE_NO_LIST);
}


static void check_partial(void)
{
    fixture(1);
    entries[0][4].tag = 0x4107; /* GNU formal-parameter pack. */
    start();
    CHECK(run(1) == TB_SYMBOL_PARAMETER_SOURCE_PARTIAL);
    selected(0, TB_SYMBOL_PARAMETER_SOURCE_CHILD);
    CHECK(slots[0].first_child == 4);

    fixture(2);
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_PARTIAL);
    selected(1, TB_SYMBOL_PARAMETER_SOURCE_UNLINKED);
    TbSymbolParameterSourceView_t view;
    CHECK(tb_symbol_parameter_source_view(&state, &view) && view.first_unlinked_parameter == 0);

    fixture(3);
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    relationship(0, TB_SYMBOL_RELATION_SPECIFICATION, 2);
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_PARTIAL);
    selected(SIZE_MAX, TB_SYMBOL_PARAMETER_SOURCE_NO_LIST | TB_SYMBOL_PARAMETER_SOURCE_ALTERNATIVES);
    CHECK(slots[1].selected_scope == 1 && slots[2].selected_scope == 2);

    fixture(3);
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    relationship(0, TB_SYMBOL_RELATION_SPECIFICATION, 2);
    relationship(1, TB_SYMBOL_RELATION_SPECIFICATION, 2);
    start();
    correspondence_to(2);
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(2, 0); /* Shared exact source is not an alternative. */

    fixture(3);
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    relationship(1, TB_SYMBOL_RELATION_SPECIFICATION, 2);
    trees[0].count = 3;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_PARTIAL);
    selected(2, TB_SYMBOL_PARAMETER_SOURCE_UNLINKED);
    CHECK(tb_symbol_parameter_source_view(&state, &view) && view.first_unlinked_scope == 1);

    fixture(2);
    reduction(1);
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_PARTIAL);
    selected(SIZE_MAX, TB_SYMBOL_PARAMETER_SOURCE_NO_LIST);

    fixture(1);
    trees[0].count = 3;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(0, 0); /* Empty physical source is not an empty C prototype claim. */

    fixture(1);
    entries[0][4].tag = 0x44;
    nodes[0].context.version = headers[0].header.version = 4;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_PARTIAL);
    selected(0, TB_SYMBOL_PARAMETER_SOURCE_CHILD);
    nodes[0].context.version = headers[0].header.version = 5;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(0, 0);
}


static void check_failures(void)
{
    fixture(1);
    entries[0][5].tag = 0x18;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_CONFLICT);
    TbSymbolParameterSourceFailure_t failure;
    CHECK(tb_symbol_parameter_source_failure(&state, &failure));
    CHECK(failure.scope_index == 0 && failure.entry_index == 6);

    fixture(1);
    entries[0][3].tag = 0x18;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_CONFLICT);
    CHECK(tb_symbol_parameter_source_failure(&state, &failure) && failure.entry_index == 5);

    fixture(2);
    reduction(1);
    attributes[1][1] = attributes[1][0];
    trees[1].attribute_count = entries[1][1].attribute_count = 2;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_CONFLICT);
    CHECK(tb_symbol_parameter_source_failure(&state, &failure) && failure.scope_index == 1);

    fixture(1);
    reduction(0);
    attributes[0][0].value.form = TB_SYMBOL_FORM_DATA8;
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_MALFORMED);
    CHECK(tb_symbol_parameter_source_failure(&state, &failure));
    CHECK(failure.entry_index == 1 && failure.attribute_index == 0);

    fixture(2);
    relationship(0, TB_SYMBOL_RELATION_ORIGIN, 1);
    relationship(1, TB_SYMBOL_RELATION_SPECIFICATION, 0);
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_INVALID);

    fixture(1);
    start();
    inventories[0].count = 0;
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_INVALID);

    TbSymbolParameterSourceView_t view;
    memset(&view, 0xa5, sizeof(view));
    unsigned char before[sizeof(view)];
    memcpy(before, &view, sizeof(before));
    CHECK(!tb_symbol_parameter_source_view(&state, &view));
    CHECK(!memcmp(before, &view, sizeof(before)));
}


static void check_lifetime(void)
{
    fixture(1);
    start();
    CHECK(run(31) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    unsigned char previous[sizeof(state)];
    unsigned char arena[sizeof(slots)];
    memcpy(previous, &state, sizeof(previous));
    memcpy(arena, slots, sizeof(arena));
    CHECK(tb_symbol_parameter_source_init(&state, correspondence, slots, 0) ==
          TB_SYMBOL_PARAMETER_SOURCE_RESOURCE);
    CHECK(tb_symbol_parameter_source_init(&state, correspondence, NULL, SCOPES) ==
          TB_SYMBOL_PARAMETER_SOURCE_INVALID);
    CHECK(tb_symbol_parameter_source_step(&state, 0) == TB_SYMBOL_PARAMETER_SOURCE_INVALID);
    CHECK(tb_symbol_parameter_source_step(&state, 257) == TB_SYMBOL_PARAMETER_SOURCE_INVALID);
    CHECK(!memcmp(previous, &state, sizeof(previous)) && !memcmp(arena, slots, sizeof(arena)));

    start();
    while (state.status == TB_SYMBOL_PARAMETER_SOURCE_PENDING)
        (void)tb_symbol_parameter_source_step(&state, 1);
    CHECK(state.status == TB_SYMBOL_PARAMETER_SOURCE_SOURCE);
    memcpy(previous, &state, sizeof(previous));
    TbSymbolDiesView_t wrong = trees[0];
    ++wrong.abbrev_offset;
    CHECK(tb_symbol_parameter_source_source(&state, &units[0], &wrong, 0) ==
          TB_SYMBOL_PARAMETER_SOURCE_INVALID);
    CHECK(!memcmp(previous, &state, sizeof(previous)));
    CHECK(tb_symbol_parameter_source_source(&state, NULL, NULL, 0) ==
          TB_SYMBOL_PARAMETER_SOURCE_DEPENDENCY);
    TbSymbolParameterSourceFailure_t failure;
    CHECK(tb_symbol_parameter_source_failure(&state, &failure));
    CHECK(failure.scope_index == 0 && failure.entry_index == 2);

    start();
    state.progress = UINT64_MAX;
    CHECK(tb_symbol_parameter_source_step(&state, 1) == TB_SYMBOL_PARAMETER_SOURCE_INVALID);
    memcpy(arena, slots, sizeof(arena));
    tb_symbol_parameter_source_discard(&state);
    CHECK(!tb_symbol_parameter_source_progress(&state) && !memcmp(arena, slots, sizeof(arena)));
}


static void check_large_scope(void)
{
    fixture(1);
    for (size_t i = 3; i < ENTRIES; ++i) {
        entries[0][i] = (TbSymbolDieEntry_t) {
            .offset = 16 + 8 * i,
            .parent = 2,
            .tag    = 0x05
        };
    }
    trees[0].count = ENTRIES;
    start();
    CHECK(run(1) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    selected(0, 0);
    CHECK(inventories[0].count == ENTRIES - 3);
    CHECK(tb_symbol_parameter_source_progress(&state) > ENTRIES);

    /* Every suspended prefix can be abandoned without modifying its borrowed
     * source or retaining independent authority over the caller's arena. */
    fixture(1);
    start();
    CHECK(run(1) == TB_SYMBOL_PARAMETER_SOURCE_READY);
    uint64_t total = tb_symbol_parameter_source_progress(&state);
    for (uint64_t prefix = 0; prefix <= total; ++prefix) {
        start();
        for (uint64_t i = 0; i < prefix; ++i) {
            if (state.status == TB_SYMBOL_PARAMETER_SOURCE_SOURCE) {
                CHECK(tb_symbol_parameter_source_source(&state, &units[0], &trees[0], 0) ==
                      TB_SYMBOL_PARAMETER_SOURCE_PENDING);
            }
            (void)tb_symbol_parameter_source_step(&state, 1);
        }

        TbSymbolParameterSourceSlot_t before[SCOPES];
        memcpy(before, slots, sizeof(before));
        tb_symbol_parameter_source_discard(&state);
        CHECK(!memcmp(before, slots, sizeof(before)));
    }
}


int main(void)
{
    check_selection();
    check_partial();
    check_failures();
    check_lifetime();
    check_large_scope();
    puts("PASS physical callable parameter-source selection, reduced declarations, "
         "coverage and failure isolation");
    return 0;
}
