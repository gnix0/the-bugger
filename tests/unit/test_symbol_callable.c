#include "symbol/callable.h"
#include "../support/check.h"

#include <string.h>

static TbSymbolCallable_t               state;
static TbSymbolCallableInput_t          input;
static TbSymbolCallableParameter_t      arena[8];
static TbSymbolGraphNode_t              callables[2];
static TbSymbolGraphNode_t              formals[2][2];
static TbSymbolGraphNode_t              mapped_graphs[2][2];
static TbSymbolParameter_t              parameters[2][2];
static TbSymbolParametersView_t         scopes[2];
static TbSymbolParameterSourceSlot_t    sources[2];
static TbSymbolParameterLinkFact_t      correspondence[4];
static TbSymbolParameterLink_t          links[2];
static TbSymbolDieEntry_t               entries[12];
static TbSymbolScopeNode_t              lexical[2];
static TbSymbolContextAnchor_t          anchor;
static TbSymbolQualifiedNode_t          names[2];
static TbSymbolInheritedTextFact_t      name_facts[2];
static TbSymbolTypeRecipeNode_t         base_nodes[1];
static TbSymbolTypeRecipeNode_t         object_nodes[4];
static TbSymbolGraphNode_t              type_graphs[5];
static TbSymbolTypeRecipeView_t         base_type;
static TbSymbolTypeRecipeView_t         object_type;
static TbSymbolInheritedReferenceView_t references[4];
static TbSymbolInheritedReferenceFact_t reference_facts[4][2];
static TbSymbolInheritSlot_t            reference_slots[4][2];
static TbSymbolInheritedFlagView_t      flags[5];
static TbSymbolInheritedFlagSlot_t      flag_slots[5][2];
static bool                             object_present;
static bool                             object_artificial;
static bool                             argument_artificial;
static bool                             ref_qualified;
static bool                             rvalue_qualified;
static bool                             prototyped;
static bool                             type_missing;
static bool                             type_alternatives;
static bool                             return_absent;
static size_t                           type_requests;
static size_t                           fail_type_request;
static TbSymbolTypeRecipeStatus_e       type_failure;
/* Completed-child models isolate semantic composition. Encoded native fixtures
 * separately exercise every decoder and ownership dependency. */
static TbSymbolReferenceTarget_t record(uint64_t index)
{
    return (TbSymbolReferenceTarget_t) {
        .backing       = {
            .key   = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 4096
        },
        .section_index = 1,
        .unit_offset   = 0,
        .record_offset = 16 + index * 8,
        .entry_index   = index
    };
}


static TbSymbolGraphNode_t node(uint64_t index)
{
    uint64_t parent = entries[index].parent;
    return (TbSymbolGraphNode_t) {
        .record = record(index),
        .context = {
            .tag            = entries[index].tag,
            .version        = 5,
            .unit_kind      = TB_SYMBOL_DWARF_COMPILE,
            .abbrev_section = 2,
            .parent_record  = parent == UINT64_MAX ? UINT64_MAX : entries[parent].offset,
            .parent_tag     = parent == UINT64_MAX ? 0 : entries[parent].tag
        },
        .complete = true
    };
}


static void fixture(void)
{
    tb_symbol_callable_discard(&state);
    memset(&input, 0, sizeof(input));
    memset(sources, 0, sizeof(sources));
    memset(lexical, 0, sizeof(lexical));
    memset(names, 0, sizeof(names));
    memset(correspondence, 0, sizeof(correspondence));
    memset(references, 0, sizeof(references));
    memset(reference_facts, 0, sizeof(reference_facts));
    memset(reference_slots, 0, sizeof(reference_slots));
    memset(flags, 0, sizeof(flags));
    memset(flag_slots, 0, sizeof(flag_slots));
    const uint64_t tags[] = {0x11, 0x02, 0x2e, 0x05, 0x05, 0x24, 0x0f, 0x26, 0x35, 0x2e, 0x05, 0x05};
    const uint64_t parents[] = {UINT64_MAX, 0, 1, 2, 2, 0, 0, 0, 0, 1, 9, 9};
    for (size_t i = 0; i < 12; ++i) {
        entries[i] = (TbSymbolDieEntry_t) {
            .offset = 16 + i * 8,
            .tag    = tags[i],
            .parent = parents[i]
        };
    }

    for (size_t i = 0; i < 2; ++i) {
        callables[i] = node(i ? 9 : 2);
        for (size_t j = 0; j < 2; ++j) {
            uint64_t index = (i ? 10 : 3) + j;
            formals[i][j] = node(index);
            parameters[i][j] = (TbSymbolParameter_t) {.record = record(index)};
            correspondence[i * 2 + j] = (TbSymbolParameterLinkFact_t) {
                .graph           = {
                    .nodes = &formals[i][j],
                    .count = 1
                },
                .scope_index     = i,
                .parameter_index = j
            };
        }
        scopes[i] = (TbSymbolParametersView_t) {
            .selected   = callables[i].record,
            .parameters = parameters[i],
            .count      = 2
        };
        sources[i].tree = (TbSymbolDiesView_t) {
            .entries       = entries,
            .count         = 12,
            .section_index = 1,
            .backing       = record(0).backing
        };
    }

    input.source = (TbSymbolParameterSourceView_t) {
        .correspondence = {
            .callables = {
                .nodes = callables,
                .count = 1
            },
            .scopes    = scopes,
            .facts     = correspondence,
            .links     = links,
            .fact_count = 2
        },
        .slots          = sources,
        .selected_scope = 0
    };
    anchor = (TbSymbolContextAnchor_t) {
        .record         = callables[0].record,
        .context        = callables[0].context,
        .kind           = TB_SYMBOL_CONTEXT_LEXICAL_PARENT,
        .parent         = record(1),
        .parent_context = node(1).context
    };
    lexical[0] = (TbSymbolScopeNode_t) {
        .record            = callables[0].record,
        .relationships     = input.source.correspondence.callables,
        .contexts          = {
            .selected = callables[0].record,
            .anchors  = &anchor,
            .count    = 1
        },
        .context_available = true,
        .complete          = true
    };
    lexical[1] = (TbSymbolScopeNode_t) {
        .record   = record(1),
        .complete = true
    };
    for (size_t i = 0; i < 2; ++i) {
        names[i] = (TbSymbolQualifiedNode_t) {
            .name        = {
                .selected = lexical[i].record,
                .facts    = &name_facts[i],
                .count    = 1
            },
            .parent_node = i ? SIZE_MAX : 1,
            .complete    = true
        };
    }
    input.language = (TbSymbolConstantView_t) {
        .selection = {
            .backing       = record(0).backing,
            .section_index = 1,
            .record_offset = entries[0].offset,
            .name          = TB_SYMBOL_CONSTANT_LANGUAGE,
            .matches       = 1
        },
        .value = 0x04
    };
    input.name = (TbSymbolQualifiedView_t) {
        .selected       = callables[0].record,
        .scope          = {
            .nodes      = lexical,
            .node_count = 2
        },
        .nodes          = names,
        .count          = 2,
        .languages      = &input.language,
        .language_count = 1
    };

    const uint64_t indexes[] = {5, 6, 7, 1, 8};
    for (size_t i = 0; i < 5; ++i)
        type_graphs[i] = node(indexes[i]);
    base_nodes[0] = (TbSymbolTypeRecipeNode_t) {
        .record = record(5),
        .graph  = {
            .nodes = &type_graphs[0],
            .count = 1
        },
        .kind   = TB_SYMBOL_TYPE_BASE
    };
    base_type = (TbSymbolTypeRecipeView_t) {
        .selected = record(5),
        .nodes    = base_nodes,
        .count    = 1,
        .complete = true
    };
    const TbSymbolTypeRecipeKind_e kinds[] = {
        TB_SYMBOL_TYPE_POINTER, TB_SYMBOL_TYPE_CONST, TB_SYMBOL_TYPE_RECORD
    };
    for (size_t i = 0; i < 3; ++i) {
        object_nodes[i] = (TbSymbolTypeRecipeNode_t) {
            .record = type_graphs[i + 1].record,
            .graph  = {
                .nodes = &type_graphs[i + 1],
                .count = 1
            },
            .kind   = kinds[i]
        };
    }
    object_type = (TbSymbolTypeRecipeView_t) {
        .selected = record(6),
        .nodes    = object_nodes,
        .count    = 3,
        .complete = true
    };

    object_present       = true;
    object_artificial    = true;
    argument_artificial  = false;
    ref_qualified        = false;
    rvalue_qualified     = false;
    prototyped           = true;
    type_missing         = false;
    type_alternatives    = false;
    return_absent        = false;
    type_requests        = 0;
    fail_type_request    = SIZE_MAX;
    type_failure         = TB_SYMBOL_TYPE_RESOURCE;
}


static void start(void)
{
    CHECK(tb_symbol_callable_init(&state, input, arena, 8) == TB_SYMBOL_CALLABLE_PENDING);
}


static void supply(TbSymbolCallableNeed_t need)
{
    if (need.kind == TB_SYMBOL_CALLABLE_TYPE) {
        ++type_requests;
        if (type_requests == fail_type_request) {
            (void)tb_symbol_callable_type(&state, need.record, type_failure, NULL);
        } else {
            TbSymbolTypeRecipeView_t *type = need.record.entry_index == 6 ? &object_type : &base_type;
            CHECK(tb_symbol_callable_type(&state, need.record,
                type->complete ? TB_SYMBOL_TYPE_READY : TB_SYMBOL_TYPE_PARTIAL, type) ==
                  TB_SYMBOL_CALLABLE_PENDING);
        }
        return;
    }

    size_t selected = input.source.selected_scope;
    bool formal = need.record.entry_index == parameters[selected][0].record.entry_index ||
                  need.record.entry_index == parameters[selected][1].record.entry_index;
    bool object = need.record.entry_index == parameters[selected][0].record.entry_index;
    if (need.kind == TB_SYMBOL_CALLABLE_FLAG) {
        size_t slot = formal ? (object ? 3 : 4) : need.field == TB_SYMBOL_FLAG_REFERENCE ? 0 :
                      need.field == TB_SYMBOL_FLAG_RVALUE_REFERENCE ? 1 : 2;
        bool value = formal ? (object ? object_artificial : argument_artificial) :
                     need.field == TB_SYMBOL_FLAG_REFERENCE ? ref_qualified :
                     need.field == TB_SYMBOL_FLAG_RVALUE_REFERENCE ? rvalue_qualified : prototyped;
        flags[slot] = (TbSymbolInheritedFlagView_t) {
            .selected        = need.record,
            .graph           = need.graph,
            .slots           = flag_slots[slot],
            .name            = need.field,
            .owner_count     = value ? 1 : 0,
            .value           = value,
            .value_available = value
        };
        CHECK(tb_symbol_callable_flag(&state, need.record,
            value ? TB_SYMBOL_FLAG_FOUND : TB_SYMBOL_FLAG_ABSENT, &flags[slot]) ==
              TB_SYMBOL_CALLABLE_PENDING);
        return;
    }

    CHECK(need.kind == TB_SYMBOL_CALLABLE_REFERENCES);
    size_t slot = formal ? (object ? 2 : 3) : need.field == TB_SYMBOL_INHERIT_TYPE ? 0 : 1;
    bool present = need.field == TB_SYMBOL_INHERIT_OBJECT_POINTER ? object_present :
                   formal ? !type_missing : !return_absent;
    references[slot] = (TbSymbolInheritedReferenceView_t) {
        .selected = need.record,
        .graph    = need.graph,
        .slots    = reference_slots[slot],
        .facts    = reference_facts[slot],
        .count    = present ? 1 : 0,
        .name     = need.field
    };
    reference_facts[slot][0].target = need.field == TB_SYMBOL_INHERIT_OBJECT_POINTER ? record(3) :
                                    formal && object ? record(6) : record(5);
    if (formal && type_alternatives) {
        references[slot].count = 2;
        reference_facts[slot][1].target = record(8);
    }
    CHECK(tb_symbol_callable_references(&state, need.record,
        present ? TB_SYMBOL_INHERITED_REFERENCE_FOUND : TB_SYMBOL_INHERITED_REFERENCE_ABSENT,
        &references[slot]) == TB_SYMBOL_CALLABLE_PENDING);
}


static TbSymbolCallableStatus_e run(size_t slice)
{
    for (unsigned i = 0; i < 100000; ++i) {
        TbSymbolCallableView_t view;
        CHECK(!tb_symbol_callable_view(&state, &view));
        TbSymbolCallableNeed_t need;
        if (tb_symbol_callable_need(&state, &need))
            supply(need);
        if (state.status >= TB_SYMBOL_CALLABLE_CONFLICT)
            return state.status;

        uint64_t before = tb_symbol_callable_progress(&state);
        TbSymbolCallableStatus_e status = tb_symbol_callable_step(&state, slice);
        CHECK(tb_symbol_callable_progress(&state) > before);
        CHECK(tb_symbol_callable_progress(&state) - before <= slice);
        if (status == TB_SYMBOL_CALLABLE_READY || status == TB_SYMBOL_CALLABLE_PARTIAL ||
            status >= TB_SYMBOL_CALLABLE_CONFLICT) {
            return status;
        }
    }
    CHECK(false);
    return TB_SYMBOL_CALLABLE_INVALID;
}


static TbSymbolCallableView_t completed(void)
{
    TbSymbolCallableView_t view;
    CHECK(tb_symbol_callable_view(&state, &view));
    return view;
}


static void check_methods(void)
{
    fixture();
    start();
    CHECK(run(1) == TB_SYMBOL_CALLABLE_READY);
    TbSymbolCallableView_t view = completed();
    CHECK(view.parameter_count == 2 && view.object_parameter == 0);
    CHECK(view.member == TB_SYMBOL_CALLABLE_INSTANCE_MEMBER && view.method_cv_available);
    CHECK(view.method_const && !view.method_volatile && view.ref_qualifier_available);
    CHECK(view.parameters[0].kind == TB_SYMBOL_CALLABLE_OBJECT_PARAMETER);
    CHECK(view.parameters[1].kind == TB_SYMBOL_CALLABLE_EXPLICIT_PARAMETER);
    CHECK(view.result.recipe_available && view.result.recipe.nodes == base_nodes);

    fixture();
    object_nodes[0].kind = TB_SYMBOL_TYPE_CONST;
    object_nodes[1].kind = TB_SYMBOL_TYPE_POINTER;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_READY);
    view = completed();
    CHECK(view.method_cv_available && !view.method_const); /* Pointer-object const is not method const. */

    fixture();
    object_nodes[1].kind = TB_SYMBOL_TYPE_VOLATILE;
    ref_qualified = true;
    start();
    CHECK(run(256) == TB_SYMBOL_CALLABLE_READY);
    view = completed();
    CHECK(view.method_volatile && !view.method_const && view.reference.value);

    fixture();
    object_present = object_artificial = false;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_READY);
    CHECK(completed().member == TB_SYMBOL_CALLABLE_STATIC_MEMBER);
    CHECK(completed().parameters[0].kind == TB_SYMBOL_CALLABLE_EXPLICIT_PARAMETER);

    fixture();
    ref_qualified = rvalue_qualified = true;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_CONFLICT);
    CHECK(!tb_symbol_callable_view(&state, &view));
}


static void check_partial(void)
{
    fixture();
    object_artificial = false;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_OBJECT_ARTIFICIAL);
    CHECK(completed().parameters[0].kind == TB_SYMBOL_CALLABLE_EXPLICIT_PARAMETER);

    fixture();
    argument_artificial = true;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().parameters[1].kind == TB_SYMBOL_CALLABLE_ARTIFICIAL_ARGUMENT);

    fixture();
    callables[0].context.version = 4;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_REF_PROFILE);
    CHECK(!completed().ref_qualifier_available);

    fixture();
    object_nodes[2].record = record(8); /* A same-looking name cannot establish class identity. */
    object_nodes[2].graph.nodes = &type_graphs[4];
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_CLASS_CONTEXT);
    CHECK(!completed().method_cv_available);

    fixture();
    object_nodes[1].kind = TB_SYMBOL_TYPE_ALIAS;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_OBJECT_TYPE);

    fixture();
    type_missing = true;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_MISSING_TYPE);

    fixture();
    type_alternatives = true;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_ALTERNATIVE_TYPES);
    CHECK(completed().parameters[0].type.references.count == 2);

    fixture();
    fail_type_request = 2;
    type_failure = TB_SYMBOL_TYPE_UNSUPPORTED;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().parameters[0].type.recipe_status == TB_SYMBOL_TYPE_UNSUPPORTED);

    fixture();
    input.source.selected_scope = SIZE_MAX;
    input.source.reasons = TB_SYMBOL_PARAMETER_SOURCE_NO_LIST;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(!completed().parameter_count && !completed().children_available &&
          !completed().root_fields_available);
}


static void check_failures(void)
{
    fixture();
    fail_type_request = 3;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_RESOURCE);
    TbSymbolCallableFailure_t failure;
    CHECK(tb_symbol_callable_failure(&state, &failure));
    CHECK(failure.parameter_index == 1 && failure.need.record.entry_index == 5);

    fixture();
    start();
    while (state.status == TB_SYMBOL_CALLABLE_PENDING)
        (void)tb_symbol_callable_step(&state, 1);
    CHECK(state.status == TB_SYMBOL_CALLABLE_REFERENCES);
    unsigned char before[sizeof(state)];
    memcpy(before, &state, sizeof(before));
    TbSymbolReferenceTarget_t wrong = state.need.record;
    ++wrong.record_offset;
    CHECK(tb_symbol_callable_references(&state, wrong, TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY, NULL) ==
          TB_SYMBOL_CALLABLE_INVALID);
    CHECK(!memcmp(before, &state, sizeof(before)));
    CHECK(tb_symbol_callable_references(&state, state.need.record,
        TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY, NULL) == TB_SYMBOL_CALLABLE_DEPENDENCY);

    fixture();
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_READY);
    memcpy(before, &state, sizeof(before));
    unsigned char arena_before[sizeof(arena)];
    memcpy(arena_before, arena, sizeof(arena_before));
    CHECK(tb_symbol_callable_init(&state, input, arena, 1) == TB_SYMBOL_CALLABLE_RESOURCE);
    CHECK(tb_symbol_callable_init(&state, input, NULL, 8) == TB_SYMBOL_CALLABLE_INVALID);
    TbSymbolCallableInput_t oversized = input;
    oversized.source.correspondence.callables.count = SIZE_MAX;
    CHECK(tb_symbol_callable_init(&state, oversized, arena, 8) == TB_SYMBOL_CALLABLE_INVALID);
    CHECK(tb_symbol_callable_step(&state, 0) == TB_SYMBOL_CALLABLE_INVALID);
    CHECK(tb_symbol_callable_step(&state, 257) == TB_SYMBOL_CALLABLE_INVALID);
    CHECK(!memcmp(before, &state, sizeof(before)) && !memcmp(arena_before, arena, sizeof(arena_before)));
    tb_symbol_callable_discard(&state);
    CHECK(!memcmp(arena_before, arena, sizeof(arena_before)) && !tb_symbol_callable_progress(&state));

    fixture();
    start();
    state.progress = UINT64_MAX;
    CHECK(tb_symbol_callable_step(&state, 1) == TB_SYMBOL_CALLABLE_INVALID);
}


static void check_origin_object(void)
{
    fixture();
    callables[0].present = 1u << (unsigned)TB_SYMBOL_RELATION_ORIGIN;
    callables[0].edges[TB_SYMBOL_RELATION_ORIGIN].target = callables[1].record;
    input.source.correspondence.callables.count = 2;
    input.source.correspondence.fact_count = 4;
    input.source.correspondence.link_count = 2;
    input.source.selected_scope = 1;
    lexical[0].relationships = input.source.correspondence.callables;
    for (size_t i = 0; i < 2; ++i) {
        mapped_graphs[i][0] = formals[0][i];
        mapped_graphs[i][1] = formals[1][i];
        mapped_graphs[i][0].present = 1u << (unsigned)TB_SYMBOL_RELATION_ORIGIN;
        mapped_graphs[i][0].edges[TB_SYMBOL_RELATION_ORIGIN].target = formals[1][i].record;
        correspondence[i].graph = (TbSymbolGraphView_t) {
            .nodes = mapped_graphs[i],
            .count = 2
        };
        correspondence[i].first_link = i;
        correspondence[i].link_count = 1;
        links[i] = (TbSymbolParameterLink_t) {
            .source_scope     = 0,
            .source_parameter = i,
            .target_scope     = 1,
            .target_parameter = i,
            .graph_node       = 1
        };
    }
    start();
    CHECK(run(1) == TB_SYMBOL_CALLABLE_READY);
    CHECK(completed().parameters[0].record.entry_index == 10);
    CHECK(completed().object.facts[0].target.entry_index == 3);
    CHECK(completed().parameters[0].kind == TB_SYMBOL_CALLABLE_OBJECT_PARAMETER);

    correspondence[0].link_count = 0;
    start();
    CHECK(run(1) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_OBJECT_MAPPING);
    CHECK(completed().object_parameter == SIZE_MAX);
    CHECK(completed().parameters[0].kind == TB_SYMBOL_CALLABLE_ARTIFICIAL_ARGUMENT);
}


static void check_pending_and_depth(void)
{
    fixture();
    start();
    TbSymbolCallableNeed_t need;
    for (unsigned turn = 0; turn < 10000; ++turn) {
        if (tb_symbol_callable_need(&state, &need)) {
            if (need.kind == TB_SYMBOL_CALLABLE_FLAG)
                break;
            supply(need);
        }
        (void)tb_symbol_callable_step(&state, 1);
    }
    CHECK(state.status == TB_SYMBOL_CALLABLE_FLAG);
    unsigned char previous[sizeof(state)];
    memcpy(previous, &state, sizeof(previous));
    TbSymbolGraphNode_t wrong_node = need.graph.nodes[0];
    ++wrong_node.context.abbrev_offset;
    TbSymbolInheritedFlagView_t wrong_flag = {
        .selected = need.record,
        .graph = {
            .nodes = &wrong_node,
            .count = 1
        },
        .slots = flag_slots[0],
        .name  = need.field
    };
    CHECK(tb_symbol_callable_flag(&state, need.record, TB_SYMBOL_FLAG_ABSENT, &wrong_flag) ==
          TB_SYMBOL_CALLABLE_INVALID);
    CHECK(!memcmp(previous, &state, sizeof(previous)));
    CHECK(run(1) == TB_SYMBOL_CALLABLE_READY);

    fixture();
    static TbSymbolTypeRecipeNode_t deep[520];
    for (size_t i = 0; i < 518; ++i) {
        deep[i] = object_nodes[0];
        deep[i].record = record(6 + i);
        deep[i].kind = TB_SYMBOL_TYPE_CONST;
    }
    deep[518] = object_nodes[0];
    deep[519] = object_nodes[2];
    object_type.nodes = deep;
    object_type.count = 520;
    start();
    CHECK(run(1) == TB_SYMBOL_CALLABLE_READY);
    CHECK(completed().method_cv_available && !completed().method_const);
    CHECK(tb_symbol_callable_progress(&state) > 520);

    fixture();
    start();
    CHECK(run(1) == TB_SYMBOL_CALLABLE_READY);
    uint64_t total = tb_symbol_callable_progress(&state);
    for (uint64_t prefix = 0; prefix <= total; ++prefix) {
        fixture();
        start();
        for (uint64_t i = 0; i < prefix; ++i) {
            if (tb_symbol_callable_need(&state, &need))
                supply(need);
            (void)tb_symbol_callable_step(&state, 1);
        }
        unsigned char arena_before[sizeof(arena)];
        memcpy(arena_before, arena, sizeof(arena_before));
        tb_symbol_callable_discard(&state);
        CHECK(!memcmp(arena_before, arena, sizeof(arena_before)));
        TbSymbolCallableView_t view;
        memset(&view, 0xa5, sizeof(view));
        unsigned char view_before[sizeof(view)];
        memcpy(view_before, &view, sizeof(view_before));
        CHECK(!tb_symbol_callable_view(&state, &view));
        CHECK(!memcmp(view_before, &view, sizeof(view_before)));
    }
}


static void check_prototypes(void)
{
    fixture();
    input.language.value = 0x0c; /* C99, with an ordinary unit-parent context. */
    anchor.kind = TB_SYMBOL_CONTEXT_UNIT_PARENT;
    anchor.parent = record(0);
    anchor.parent_context = node(0).context;
    object_present = object_artificial = false;
    scopes[0].count = 0;
    input.source.correspondence.fact_count = 0;
    return_absent = true;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_READY);
    CHECK(!completed().parameter_count && completed().result.absent && completed().root_fields_available);
    prototyped = false;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_PROTOTYPE);

    fixture();
    parameters[0][1].unspecified = true;
    scopes[0].count = 2;
    input.source.correspondence.fact_count = 1;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_READY);
    CHECK(completed().parameters[1].kind == TB_SYMBOL_CALLABLE_VARIADIC);
    CHECK(!completed().parameters[1].artificial_available);

    fixture();
    input.language.value = UINT64_MAX;
    start();
    CHECK(run(31) == TB_SYMBOL_CALLABLE_PARTIAL);
    CHECK(completed().reasons & TB_SYMBOL_CALLABLE_LANGUAGE_LIMIT);
    CHECK(!completed().parameter_count && !completed().root_fields_available);
}


int main(void)
{
    check_methods();
    check_partial();
    check_failures();
    check_origin_object();
    check_prototypes();
    check_pending_and_depth();
    puts("PASS descriptive callable composition, exact objects, "
         "pointer/pointee qualifiers and failure isolation");
    return 0;
}
