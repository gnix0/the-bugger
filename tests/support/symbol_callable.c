#include "symbol_callable.h"
#include "symbol/callable.h"
#include "check.h"

#include <string.h>

typedef struct native_capture_block {
    struct native_capture_block *next;
    size_t                       bytes;
    _Alignas(max_align_t) unsigned char payload[];
} NativeCaptureBlock_t;

static TbSymbolBudget_t          *budget;
static NativeCaptureBlock_t      *capture_blocks;
static size_t                     capture_owned;
static bool                       capture_name_active;
static bool                       capture_type_active;
static TbSymbolQualifiedView_t    captured_name;
static TbSymbolQualifiedStatus_e  captured_name_status;
static TbSymbolTypeRecipeView_t   captured_type;
static TbSymbolTypeRecipeStatus_e captured_type_status;
static uint64_t                   callable_ready;
static uint64_t                   callable_partial;
static uint64_t                   callable_unavailable;
static uint64_t                   callable_instance;


static void *capture_copy(const void *source, size_t bytes)
{
    if (!bytes)
        return NULL;

    CHECK(bytes <= SIZE_MAX - sizeof(NativeCaptureBlock_t));
    size_t owned = sizeof(NativeCaptureBlock_t) + bytes;
    CHECK(tb_symbol_budget_reserve(budget, owned));
    NativeCaptureBlock_t *block = malloc(owned);
    CHECK(block);
    block->next  = capture_blocks;
    block->bytes = owned;
    capture_blocks = block;
    CHECK(owned <= SIZE_MAX - capture_owned);
    capture_owned += owned;
    if (source)
        memcpy(block->payload, source, bytes);
    else
        memset(block->payload, 0, bytes);
    return block->payload;
}


static TbSymbolGraphView_t capture_graph(TbSymbolGraphView_t graph)
{
    CHECK(graph.count <= 64);
    graph.nodes = capture_copy(graph.nodes, graph.count * sizeof(*graph.nodes));
    return graph;
}


static TbSymbolInheritedReferenceView_t capture_references(TbSymbolInheritedReferenceView_t reference)
{
    reference.slots = capture_copy(reference.slots, reference.graph.count * sizeof(*reference.slots));
    reference.facts = capture_copy(reference.facts, reference.count * sizeof(*reference.facts));
    reference.graph = capture_graph(reference.graph);
    return reference;
}


static TbSymbolQualifiedView_t capture_qualified(TbSymbolQualifiedView_t name)
{
    CHECK(name.count <= 64 && name.scope.node_count == name.count && name.scope.edge_count <= 256);
    TbSymbolScopeNode_t *nodes = capture_copy(name.scope.nodes, name.scope.node_count * sizeof(*nodes));
    for (size_t i = 0; i < name.scope.node_count; ++i) {
        nodes[i].relationships = capture_graph(nodes[i].relationships);
        CHECK(nodes[i].contexts.count <= 64);
        nodes[i].contexts.anchors = capture_copy(nodes[i].contexts.anchors,
            nodes[i].contexts.count * sizeof(*nodes[i].contexts.anchors));
    }
    name.scope.nodes = nodes;
    name.scope.edges = capture_copy(name.scope.edges, name.scope.edge_count * sizeof(*name.scope.edges));
    TbSymbolQualifiedNode_t *parts = capture_copy(name.nodes, name.count * sizeof(*parts));
    for (size_t i = 0; i < name.count; ++i) {
        CHECK(parts[i].name.count <= 64);
        parts[i].name.facts = capture_copy(parts[i].name.facts,
            parts[i].name.count * sizeof(*parts[i].name.facts));
    }
    name.nodes = parts;
    name.languages = capture_copy(name.languages, name.language_count * sizeof(*name.languages));
    return name;
}


static TbSymbolTypeRecipeView_t capture_type(TbSymbolTypeRecipeView_t type)
{
    CHECK(type.count <= 64);
    TbSymbolTypeRecipeNode_t *nodes = capture_copy(type.nodes, type.count * sizeof(*nodes));
    for (size_t i = 0; i < type.count; ++i) {
        nodes[i].graph = capture_graph(nodes[i].graph);
        if (nodes[i].name_available)
            nodes[i].name = capture_qualified(nodes[i].name);
        if (nodes[i].references_available)
            nodes[i].references = capture_references(nodes[i].references);
    }
    type.nodes = nodes;
    return type;
}


size_t test_symbol_callable_owned(void)
{
    return capture_owned;
}


void test_symbol_callable_capture_name(TbSymbolQualifiedStatus_e status,
                                       const TbSymbolQualifiedView_t *name)
{
    if (!capture_name_active)
        return;

    captured_name_status = status;
    if (status == TB_SYMBOL_QUALIFIED_READY) {
        CHECK(name);
        captured_name = capture_qualified(*name);
    }
}


void test_symbol_callable_capture_type(TbSymbolTypeRecipeStatus_e status,
                                       const TbSymbolTypeRecipeView_t *type)
{
    if (!capture_type_active)
        return;

    captured_type_status = status;
    if (status == TB_SYMBOL_TYPE_READY || status == TB_SYMBOL_TYPE_PARTIAL) {
        CHECK(type);
        captured_type = capture_type(*type);
    }
}


static void callable_references(TestSymbolCallableSource_t source, TbSymbolCallable_t *callable,
                                 TbSymbolCallableNeed_t need)
{
    TbSymbolInheritedReference_t reference;
    CHECK(tb_symbol_budget_reserve(budget, sizeof(reference)));
    size_t count = need.graph.count;
    TbSymbolInheritSlot_t *slots = capture_copy(NULL, count * sizeof(*slots));
    TbSymbolInheritedReferenceFact_t *facts = capture_copy(NULL, count * sizeof(*facts));
    CHECK(tb_symbol_inherited_reference_init(&reference, need.graph, need.field, slots, count,
        facts, count, &source.units, NULL, 0) == TB_SYMBOL_INHERITED_REFERENCE_PENDING);
    for (unsigned turn = 0; turn < 100000; ++turn) {
        TbSymbolInheritedReferenceSource_t next;
        if (tb_symbol_inherited_reference_source_need(&reference, &next)) {
            CHECK(next.source.record.unit_offset == source.tree.unit_offset);
            (void)tb_symbol_inherited_reference_source(&reference, &source.units, &source.tree,
                source.unit_index);
        }
        TbSymbolReferenceNeed_t target;
        if (tb_symbol_inherited_reference_target_need(&reference, &target)) {
            (void)tb_symbol_inherited_reference_target(&reference,
                target.unit_offset == source.tree.unit_offset ? &source.tree : NULL);
        }
        (void)tb_symbol_inherited_reference_step(&reference, 31);
        if (reference.status != TB_SYMBOL_INHERITED_REFERENCE_PENDING &&
            reference.status != TB_SYMBOL_INHERITED_REFERENCE_SOURCE &&
            reference.status != TB_SYMBOL_INHERITED_REFERENCE_TARGET) {
            break;
        }
    }
    TbSymbolInheritedReferenceView_t view;
    bool available = tb_symbol_inherited_reference_view(&reference, &view);
    (void)tb_symbol_callable_references(callable, need.record, reference.status, available ? &view : NULL);
    CHECK(tb_symbol_budget_release(budget, sizeof(reference)));
}


static void callable_flag(TestSymbolCallableSource_t source, TbSymbolCallable_t *callable,
                          TbSymbolCallableNeed_t need)
{
    TbSymbolInheritedFlag_t flag;
    CHECK(tb_symbol_budget_reserve(budget, sizeof(flag)));
    TbSymbolInheritedFlagSlot_t *slots = capture_copy(NULL, need.graph.count * sizeof(*slots));
    CHECK(tb_symbol_inherited_flag_init(&flag, need.graph, need.field, slots, need.graph.count) ==
          TB_SYMBOL_FLAG_PENDING);
    for (unsigned turn = 0; turn < 100000; ++turn) {
        TbSymbolInheritNeed_t next;
        if (tb_symbol_inherited_flag_need(&flag, &next)) {
            CHECK(next.record.unit_offset == source.tree.unit_offset);
            (void)tb_symbol_inherited_flag_source(&flag, &source.units, &source.tree, source.unit_index);
        }
        (void)tb_symbol_inherited_flag_step(&flag, 31);
        if (flag.status != TB_SYMBOL_FLAG_PENDING && flag.status != TB_SYMBOL_FLAG_SOURCE)
            break;
    }
    TbSymbolInheritedFlagView_t view;
    bool available = tb_symbol_inherited_flag_view(&flag, &view);
    (void)tb_symbol_callable_flag(callable, need.record, flag.status, available ? &view : NULL);
    CHECK(tb_symbol_budget_release(budget, sizeof(flag)));
}


void test_symbol_callable_run(TestSymbolCallableSource_t source, TbSymbolParameterSourceView_t parameters,
                               TestSymbolCallableProviders_t providers, TbSymbolBudget_t *construction)
{
    TbSymbolCallable_t callable;
    TbSymbolCallableParameter_t arguments[64];
    TbSymbolConstant_t language;
    CHECK(construction && !budget && providers.name && providers.type);
    budget = construction;
    size_t before = budget->used;
    size_t charge = sizeof(callable) + sizeof(arguments) + sizeof(language) +
                    sizeof(captured_name) + sizeof(captured_type);
    CHECK(!capture_blocks && !capture_owned);
    CHECK(tb_symbol_budget_reserve(budget, charge));
    captured_name_status = TB_SYMBOL_QUALIFIED_INVALID;
    capture_name_active = true;
    providers.name(providers.context, parameters.correspondence.callables.nodes[0].record);
    capture_name_active = false;
    if (captured_name_status == TB_SYMBOL_QUALIFIED_READY) {
        CHECK(tb_symbol_constant_init(&language, source.tree, 0, TB_SYMBOL_CONSTANT_LANGUAGE) ==
              TB_SYMBOL_CONSTANT_PENDING);
        for (unsigned turn = 0; turn < 100000; ++turn) {
            if (language.status == TB_SYMBOL_CONSTANT_SCHEMA)
                (void)tb_symbol_constant_schema(&language, &source.schema);
            (void)tb_symbol_constant_step(&language, 31);
            if (language.status != TB_SYMBOL_CONSTANT_PENDING &&
                language.status != TB_SYMBOL_CONSTANT_SCHEMA)
                break;
        }
        TbSymbolCallableInput_t input = {
            .source = parameters,
            .name   = captured_name
        };
        CHECK(tb_symbol_constant_view(&language, &input.language));
        CHECK(tb_symbol_callable_init(&callable, input, arguments, 64) == TB_SYMBOL_CALLABLE_PENDING);
        for (unsigned turn = 0; turn < 1000000; ++turn) {
            TbSymbolCallableNeed_t need;
            if (tb_symbol_callable_need(&callable, &need)) {
                if (need.kind == TB_SYMBOL_CALLABLE_REFERENCES) {
                    callable_references(source, &callable, need);
                } else if (need.kind == TB_SYMBOL_CALLABLE_FLAG) {
                    callable_flag(source, &callable, need);
                } else {
                    CHECK(need.kind == TB_SYMBOL_CALLABLE_TYPE);
                    captured_type_status = TB_SYMBOL_TYPE_UNSUPPORTED;
                    capture_type_active = true;
                    providers.type(providers.context, need.record);
                    capture_type_active = false;
                    (void)tb_symbol_callable_type(&callable, need.record, captured_type_status,
                        captured_type_status == TB_SYMBOL_TYPE_READY ||
                        captured_type_status == TB_SYMBOL_TYPE_PARTIAL ? &captured_type : NULL);
                }
            }
            (void)tb_symbol_callable_step(&callable, 31);
            if (callable.status >= TB_SYMBOL_CALLABLE_READY)
                break;
        }
        TbSymbolCallableView_t view;
        if (tb_symbol_callable_view(&callable, &view)) {
            CHECK((callable.status == TB_SYMBOL_CALLABLE_READY) == (view.reasons == 0));
            if (view.children_available) {
                CHECK(view.parameter_count ==
                      parameters.correspondence.scopes[parameters.selected_scope].count);
                for (size_t i = 0; i < view.parameter_count; ++i) {
                    const TbSymbolParameter_t *selected =
                        &parameters.correspondence.scopes[parameters.selected_scope].parameters[i];
                    CHECK(view.parameters[i].record.record_offset == selected->record.record_offset);
                    if (view.parameters[i].kind == TB_SYMBOL_CALLABLE_OBJECT_PARAMETER) {
                        CHECK(i == view.object_parameter && view.parameters[i].artificial.value);
                        CHECK(view.object.count);
                    }
                }
            }
            callable_ready += callable.status == TB_SYMBOL_CALLABLE_READY;
            callable_partial += callable.status == TB_SYMBOL_CALLABLE_PARTIAL;
            callable_instance += view.member == TB_SYMBOL_CALLABLE_INSTANCE_MEMBER;
        } else {
            if (callable.status != TB_SYMBOL_CALLABLE_UNSUPPORTED &&
                callable.status != TB_SYMBOL_CALLABLE_DEPENDENCY) {
                fprintf(stderr, "callable status=%u root=%llu phase=%u param=%zu field=%llu\n",
                    callable.status,
                    (unsigned long long)parameters.correspondence.callables.nodes[0].record.record_offset,
                    callable.phase, callable.parameter_index, (unsigned long long)callable.need.field);
            }
            CHECK(callable.status == TB_SYMBOL_CALLABLE_UNSUPPORTED ||
                  callable.status == TB_SYMBOL_CALLABLE_DEPENDENCY);
            ++callable_unavailable;
        }
        tb_symbol_callable_discard(&callable);
    } else {
        ++callable_unavailable;
    }
    while (capture_blocks) {
        NativeCaptureBlock_t *block = capture_blocks;
        capture_blocks = block->next;
        size_t owned = block->bytes;
        free(block);
        CHECK(tb_symbol_budget_release(budget, owned) && capture_owned >= owned);
        capture_owned -= owned;
    }
    CHECK(!capture_owned && !capture_name_active && !capture_type_active);
    CHECK(tb_symbol_budget_release(budget, charge) && budget->used == before);
    captured_name = (TbSymbolQualifiedView_t) {0};
    captured_type = (TbSymbolTypeRecipeView_t) {0};
    captured_name_status = TB_SYMBOL_QUALIFIED_INVALID;
    captured_type_status = TB_SYMBOL_TYPE_INVALID;
    budget = NULL;
}


void test_symbol_callable_report(void)
{
    CHECK(callable_ready && callable_partial && callable_instance);
    printf("PASS native callable recipes: %llu ready, %llu partial, "
           "%llu unavailable, %llu instance members\n",
           (unsigned long long)callable_ready, (unsigned long long)callable_partial,
           (unsigned long long)callable_unavailable, (unsigned long long)callable_instance);
}
