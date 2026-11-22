#include "symbol/callable.h"

typedef enum {
    CALL_LANGUAGE,
    CALL_CONTEXT,
    CALL_VERSIONS,
    CALL_RETURN,
    CALL_REFERENCE_COMPARE,
    CALL_OBJECT,
    CALL_OBJECT_FIND,
    CALL_OBJECT_FACT,
    CALL_OBJECT_LINK,
    CALL_LVALUE,
    CALL_RVALUE,
    CALL_PROTOTYPED,
    CALL_PARAMETER,
    CALL_PARAMETER_GRAPH,
    CALL_ARTIFICIAL,
    CALL_PARAMETER_TYPE,
    CALL_PARAMETER_ADVANCE,
    CALL_METHOD,
    CALL_OBJECT_OUTER,
    CALL_OBJECT_POINTEE,
    CALL_CLASS,
    CALL_CLASS_FIND,
    CALL_DONE
} CallablePhase_e;

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset && left.entry_index == right.entry_index;
}


static bool same_graph_root(TbSymbolGraphView_t left, TbSymbolGraphView_t right)
{
    if (!left.nodes || !left.count || !right.nodes || !right.count ||
        !left.nodes[0].complete || !right.nodes[0].complete ||
        !same_record(left.nodes[0].record, right.nodes[0].record)) {
        return false;
    }

    const TbSymbolGraphContext_t *a = &left.nodes[0].context;
    const TbSymbolGraphContext_t *b = &right.nodes[0].context;
    return a->tag == b->tag && a->version == b->version && a->unit_kind == b->unit_kind &&
           a->abbrev_section == b->abbrev_section && a->abbrev_offset == b->abbrev_offset &&
           a->parent_record == b->parent_record && a->parent_tag == b->parent_tag;
}


static void fail(TbSymbolCallable_t *state, TbSymbolCallableStatus_e status)
{
    state->status  = status;
    state->failure = (TbSymbolCallableFailure_t) {
        .status          = status,
        .need            = state->need,
        .parameter_index = state->parameter_index
    };
}


static void request(TbSymbolCallable_t *state, TbSymbolCallableStatus_e kind,
                    TbSymbolGraphView_t graph, uint64_t field)
{
    state->need = (TbSymbolCallableNeed_t) {
        .kind   = kind,
        .record = graph.nodes[0].record,
        .graph  = graph,
        .field  = field
    };
    state->status = kind;
}


static bool class_tag(uint64_t tag)
{
    return tag == 0x02 || tag == 0x13 || tag == 0x17;
}


TbSymbolCallableStatus_e tb_symbol_callable_init(TbSymbolCallable_t *state,
                                                TbSymbolCallableInput_t input,
                                                TbSymbolCallableParameter_t *parameters, size_t capacity)
{
    TbSymbolGraphView_t graph = input.source.correspondence.callables;
    if (!state || !graph.nodes || !graph.count || !input.source.slots ||
        !input.source.correspondence.scopes || !parameters ||
        graph.count > SIZE_MAX / sizeof(*graph.nodes) ||
        graph.count > SIZE_MAX / sizeof(*input.source.correspondence.scopes) ||
        graph.count > SIZE_MAX / sizeof(*input.source.slots) ||
        capacity > SIZE_MAX / sizeof(*parameters) || !input.name.nodes || !input.name.count ||
        !input.name.scope.nodes || !input.name.scope.node_count || !input.name.nodes[0].complete ||
        !input.name.scope.nodes[0].complete || !input.name.scope.nodes[0].contexts.count ||
        !input.name.scope.nodes[0].contexts.anchors ||
        input.name.scope.nodes[0].contexts.count >
            SIZE_MAX / sizeof(*input.name.scope.nodes[0].contexts.anchors) ||
        !same_record(graph.nodes[0].record, input.name.selected) ||
        !same_graph_root(graph, input.name.scope.nodes[0].relationships) ||
        input.name.count > SIZE_MAX / sizeof(*input.name.nodes) ||
        input.name.scope.node_count > SIZE_MAX / sizeof(*input.name.scope.nodes) ||
        input.name.count != input.name.scope.node_count ||
        input.name.language_count > SIZE_MAX / sizeof(*input.name.languages) ||
        !input.name.language_count || !input.name.languages ||
        (input.source.selected_scope != SIZE_MAX && input.source.selected_scope >= graph.count)) {
        return TB_SYMBOL_CALLABLE_INVALID;
    }

    size_t source = input.source.selected_scope == SIZE_MAX ? 0 : input.source.selected_scope;
    TbSymbolDiesView_t tree = input.source.slots[source].tree;
    TbSymbolReferenceTarget_t root = graph.nodes[source].record;
    if (!tree.entries || !tree.count || input.language.selection.name != TB_SYMBOL_CONSTANT_LANGUAGE ||
        input.language.selection.matches != 1) {
        return TB_SYMBOL_CALLABLE_INVALID;
    }

    root.entry_index   = 0;
    root.record_offset = tree.entries[0].offset;
    TbSymbolReferenceTarget_t language = {
        .backing       = input.language.selection.backing,
        .section_index = input.language.selection.section_index,
        .unit_offset   = input.language.selection.unit_offset,
        .record_offset = input.language.selection.record_offset,
        .entry_index   = 0
    };
    if (!same_record(root, language))
        return TB_SYMBOL_CALLABLE_INVALID;

    size_t count = input.source.selected_scope == SIZE_MAX ? 0 :
                   input.source.correspondence.scopes[source].count;
    if (capacity < count)
        return TB_SYMBOL_CALLABLE_RESOURCE;

    *state = (TbSymbolCallable_t) {
        .view = {
            .input              = input,
            .parameters         = parameters,
            .parameter_count    = count,
            .object_parameter   = SIZE_MAX,
            .member             = TB_SYMBOL_CALLABLE_UNKNOWN_MEMBER,
            .children_available = input.source.selected_scope != SIZE_MAX
        },
        .parameters       = parameters,
        .status           = TB_SYMBOL_CALLABLE_PENDING,
        .language         = tb_symbol_language_family(input.language.value),
        .parameter_index  = SIZE_MAX,
        .mapped_parameter = SIZE_MAX,
        .phase            = CALL_LANGUAGE,
        .initialized      = true
    };

    if (input.source.reasons)
        state->view.reasons |= TB_SYMBOL_CALLABLE_SOURCE_LIMIT;

    return state->status;
}


static void language(TbSymbolCallable_t *state)
{
    const TbSymbolQualifiedView_t *name = &state->view.input.name;
    if (state->language == TB_SYMBOL_LANGUAGE_OTHER || !state->view.children_available) {
        state->view.reasons |= state->language == TB_SYMBOL_LANGUAGE_OTHER
                                  ? TB_SYMBOL_CALLABLE_LANGUAGE_LIMIT : TB_SYMBOL_CALLABLE_SOURCE_LIMIT;
        state->view.parameter_count    = 0;
        state->view.children_available = false;
        state->phase = CALL_DONE;
        return;
    }

    if (state->scan == name->language_count) {
        state->scan  = 0;
        state->phase = CALL_CONTEXT;
        return;
    }

    if (tb_symbol_language_family(name->languages[state->scan++].value) != state->language)
        state->view.reasons |= TB_SYMBOL_CALLABLE_LANGUAGE_LIMIT;
}


static void context(TbSymbolCallable_t *state)
{
    TbSymbolContextView_t anchors = state->view.input.name.scope.nodes[0].contexts;
    if (state->scan == anchors.count) {
        if (state->class_seen && !state->ordinary_seen && state->language == TB_SYMBOL_LANGUAGE_CPP) {
            state->view.member = TB_SYMBOL_CALLABLE_STATIC_MEMBER;
        } else if (!state->class_seen) {
            state->view.member = TB_SYMBOL_CALLABLE_ORDINARY;
        } else {
            state->view.reasons |= TB_SYMBOL_CALLABLE_CLASS_CONTEXT;
        }

        state->scan  = 0;
        state->phase = CALL_VERSIONS;
        return;
    }

    const TbSymbolContextAnchor_t *anchor = &anchors.anchors[state->scan++];
    if (anchor->kind == TB_SYMBOL_CONTEXT_LEXICAL_PARENT && class_tag(anchor->parent_context.tag))
        state->class_seen = true;
    else
        state->ordinary_seen = true;
}


static void versions(TbSymbolCallable_t *state)
{
    TbSymbolGraphView_t graph = state->view.input.source.correspondence.callables;
    if (state->scan == graph.count) {
        state->scan  = 0;
        state->phase = CALL_RETURN;
        return;
    }

    if (graph.nodes[state->scan++].context.version == 4)
        state->dwarf4 = true;
}


static void compare_references(TbSymbolCallable_t *state)
{
    TbSymbolCallableType_t *type = state->active_type;
    if (state->scan == type->references.count) {
        if (type->alternatives) {
            state->view.reasons |= TB_SYMBOL_CALLABLE_ALTERNATIVE_TYPES;
            state->phase = state->type_next;
        } else {
            state->need = (TbSymbolCallableNeed_t) {
                .kind   = TB_SYMBOL_CALLABLE_TYPE,
                .record = type->references.facts[0].target
            };
            state->status = TB_SYMBOL_CALLABLE_TYPE;
        }
        return;
    }

    if (!same_record(type->references.facts[0].target, type->references.facts[state->scan++].target))
        type->alternatives = true;
}


static void mapped_object(TbSymbolCallable_t *state, size_t parameter)
{
    if (parameter == SIZE_MAX) {
        state->object_failed = true;
    } else if (state->mapped_parameter == SIZE_MAX) {
        state->mapped_parameter = parameter;
    } else if (state->mapped_parameter != parameter) {
        state->object_failed = true;
    }

    ++state->object_fact;
    state->object_scope  = 0;
    state->object_formal = 0;
    state->phase         = CALL_OBJECT_FIND;
}


static void object_find(TbSymbolCallable_t *state)
{
    const TbSymbolParameterLinksView_t *input = &state->view.input.source.correspondence;
    if (state->object_fact == state->view.object.count) {
        if (state->object_failed) {
            state->view.reasons |= TB_SYMBOL_CALLABLE_OBJECT_MAPPING;
        } else {
            state->view.object_parameter = state->mapped_parameter;
        }

        state->phase = CALL_LVALUE;
        return;
    }

    if (state->object_scope == input->callables.count) {
        mapped_object(state, SIZE_MAX);
        return;
    }

    const TbSymbolParametersView_t *scope = &input->scopes[state->object_scope];
    if (state->object_formal == scope->count) {
        ++state->object_scope;
        state->object_formal = 0;
        return;
    }

    const TbSymbolParameter_t *parameter = &scope->parameters[state->object_formal];
    if (!parameter->unspecified &&
        same_record(parameter->record, state->view.object.facts[state->object_fact].target)) {
        if (state->object_scope == state->view.input.source.selected_scope) {
            mapped_object(state, state->object_formal);
        } else {
            state->fact_index = 0;
            state->phase      = CALL_OBJECT_FACT;
        }
    } else {
        ++state->object_formal;
    }
}


static void object_fact(TbSymbolCallable_t *state)
{
    const TbSymbolParameterLinksView_t *input = &state->view.input.source.correspondence;
    if (state->fact_index == input->fact_count) {
        mapped_object(state, SIZE_MAX);
        return;
    }

    const TbSymbolParameterLinkFact_t *fact = &input->facts[state->fact_index++];
    if (fact->scope_index == state->object_scope && fact->parameter_index == state->object_formal) {
        state->scan  = 0;
        state->phase = CALL_OBJECT_LINK;
    }
}


static void object_link(TbSymbolCallable_t *state)
{
    const TbSymbolParameterLinksView_t *input = &state->view.input.source.correspondence;
    const TbSymbolParameterLinkFact_t *fact = &input->facts[state->fact_index - 1];
    if (state->scan == fact->link_count) {
        mapped_object(state, SIZE_MAX);
        return;
    }

    const TbSymbolParameterLink_t *link = &input->links[fact->first_link + state->scan++];
    if (link->target_scope == state->view.input.source.selected_scope)
        mapped_object(state, link->target_parameter);
}


static void parameter(TbSymbolCallable_t *state)
{
    if (state->parameter_index == SIZE_MAX)
        state->parameter_index = 0;

    if (state->parameter_index == state->view.parameter_count) {
        state->phase = CALL_METHOD;
        return;
    }

    const TbSymbolParametersView_t *source = &state->view.input.source.correspondence.scopes[
        state->view.input.source.selected_scope];
    const TbSymbolParameter_t *input = &source->parameters[state->parameter_index];
    state->parameters[state->parameter_index] = (TbSymbolCallableParameter_t) {
        .record = input->record,
        .kind   = input->unspecified ? TB_SYMBOL_CALLABLE_VARIADIC : TB_SYMBOL_CALLABLE_EXPLICIT_PARAMETER
    };

    if (input->unspecified) {
        state->phase = CALL_PARAMETER_ADVANCE;
    } else {
        state->fact_index = 0;
        state->phase      = CALL_PARAMETER_GRAPH;
    }
}


static void parameter_graph(TbSymbolCallable_t *state)
{
    const TbSymbolParameterLinksView_t *input = &state->view.input.source.correspondence;
    if (state->fact_index == input->fact_count) {
        fail(state, TB_SYMBOL_CALLABLE_DEPENDENCY);
        return;
    }

    const TbSymbolParameterLinkFact_t *fact = &input->facts[state->fact_index++];
    if (fact->scope_index == state->view.input.source.selected_scope &&
        fact->parameter_index == state->parameter_index) {
        state->parameters[state->parameter_index].graph = fact->graph;
        state->phase = CALL_ARTIFICIAL;
    }
}


static void method(TbSymbolCallable_t *state)
{
    bool reference = state->view.reference.value;
    bool rvalue    = state->view.rvalue_reference.value;
    if (reference && rvalue) {
        fail(state, TB_SYMBOL_CALLABLE_CONFLICT);
        return;
    }

    if (state->language == TB_SYMBOL_LANGUAGE_C && !state->view.prototyped.value)
        state->view.reasons |= TB_SYMBOL_CALLABLE_PROTOTYPE;

    if (state->view.object_parameter == SIZE_MAX) {
        if (state->view.object.count || reference || rvalue) {
            state->view.member = TB_SYMBOL_CALLABLE_UNKNOWN_MEMBER;
            state->view.reasons |= TB_SYMBOL_CALLABLE_OBJECT_MAPPING;
        }

        state->phase = CALL_DONE;
        return;
    }

    TbSymbolCallableParameter_t *object = &state->parameters[state->view.object_parameter];
    if (object->kind != TB_SYMBOL_CALLABLE_OBJECT_PARAMETER) {
        state->view.member = TB_SYMBOL_CALLABLE_UNKNOWN_MEMBER;
        state->view.reasons |= TB_SYMBOL_CALLABLE_OBJECT_ARTIFICIAL;
        state->phase = CALL_DONE;
        return;
    }

    if (!state->class_seen || state->ordinary_seen || state->language != TB_SYMBOL_LANGUAGE_CPP) {
        state->view.member = TB_SYMBOL_CALLABLE_UNKNOWN_MEMBER;
        state->view.reasons |= TB_SYMBOL_CALLABLE_CLASS_CONTEXT;
        state->phase = CALL_DONE;
        return;
    }

    state->view.member = TB_SYMBOL_CALLABLE_INSTANCE_MEMBER;
    state->view.ref_qualifier_available = !state->dwarf4;
    if (state->dwarf4)
        state->view.reasons |= TB_SYMBOL_CALLABLE_REF_PROFILE;

    if (!object->type.recipe_available || !object->type.recipe.complete) {
        state->view.reasons |= TB_SYMBOL_CALLABLE_OBJECT_TYPE;
        state->phase = CALL_DONE;
        return;
    }

    state->scan  = 0;
    state->phase = CALL_OBJECT_OUTER;
}


static void object_type(TbSymbolCallable_t *state)
{
    const TbSymbolTypeRecipeView_t *type = &state->parameters[state->view.object_parameter].type.recipe;
    if (state->scan == type->count) {
        state->view.reasons |= TB_SYMBOL_CALLABLE_OBJECT_TYPE;
        state->phase = CALL_DONE;
        return;
    }

    TbSymbolTypeRecipeKind_e kind = type->nodes[state->scan].kind;
    if (kind == TB_SYMBOL_TYPE_CONST || kind == TB_SYMBOL_TYPE_VOLATILE || kind == TB_SYMBOL_TYPE_RESTRICT) {
        if (state->phase == CALL_OBJECT_POINTEE) {
            if (kind == TB_SYMBOL_TYPE_CONST)
                state->view.method_const = true;
            else if (kind == TB_SYMBOL_TYPE_VOLATILE)
                state->view.method_volatile = true;
            else
                state->view.reasons |= TB_SYMBOL_CALLABLE_OBJECT_TYPE;
        }
        ++state->scan;
    } else if (state->phase == CALL_OBJECT_OUTER && kind == TB_SYMBOL_TYPE_POINTER) {
        ++state->scan;
        state->phase = CALL_OBJECT_POINTEE;
    } else if (state->phase == CALL_OBJECT_POINTEE && kind == TB_SYMBOL_TYPE_RECORD &&
               state->scan + 1 == type->count) {
        state->anchor_index = 0;
        state->phase        = CALL_CLASS;
    } else {
        state->view.reasons |= TB_SYMBOL_CALLABLE_OBJECT_TYPE;
        state->phase = CALL_DONE;
    }
}


static void class_anchor(TbSymbolCallable_t *state)
{
    TbSymbolContextView_t anchors = state->view.input.name.scope.nodes[0].contexts;
    if (state->anchor_index == anchors.count) {
        state->view.method_cv_available =
            !(state->view.reasons & (TB_SYMBOL_CALLABLE_OBJECT_TYPE | TB_SYMBOL_CALLABLE_CLASS_CONTEXT));
        state->phase = CALL_DONE;
        return;
    }

    state->class_node = 0;
    state->phase      = CALL_CLASS_FIND;
}


static void class_find(TbSymbolCallable_t *state)
{
    TbSymbolContextView_t anchors = state->view.input.name.scope.nodes[0].contexts;
    const TbSymbolTypeRecipeView_t *type = &state->parameters[state->view.object_parameter].type.recipe;
    TbSymbolGraphView_t graph = type->nodes[state->scan].graph;
    if (state->class_node == graph.count) {
        state->view.reasons |= TB_SYMBOL_CALLABLE_CLASS_CONTEXT;
        state->phase = CALL_DONE;
        return;
    }

    if (same_record(anchors.anchors[state->anchor_index].parent, graph.nodes[state->class_node++].record)) {
        ++state->anchor_index;
        state->phase = CALL_CLASS;
    }
}


TbSymbolCallableStatus_e tb_symbol_callable_step(TbSymbolCallable_t *state, size_t max_steps)
{
    if (!state || !state->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_CALLABLE_INVALID;

    for (size_t i = 0; i < max_steps && state->status == TB_SYMBOL_CALLABLE_PENDING; ++i) {
        if (state->progress == UINT64_MAX) {
            fail(state, TB_SYMBOL_CALLABLE_INVALID);
            break;
        }

        TbSymbolGraphView_t root = state->view.input.source.correspondence.callables;
        switch (state->phase) {
        case CALL_LANGUAGE: language(state); break;
        case CALL_CONTEXT: context(state); break;
        case CALL_VERSIONS: versions(state); break;
        case CALL_RETURN:
            request(state, TB_SYMBOL_CALLABLE_REFERENCES, root, TB_SYMBOL_INHERIT_TYPE);
            break;
        case CALL_REFERENCE_COMPARE: compare_references(state); break;
        case CALL_OBJECT:
            request(state, TB_SYMBOL_CALLABLE_REFERENCES, root, TB_SYMBOL_INHERIT_OBJECT_POINTER);
            break;
        case CALL_OBJECT_FIND: object_find(state); break;
        case CALL_OBJECT_FACT: object_fact(state); break;
        case CALL_OBJECT_LINK: object_link(state); break;
        case CALL_LVALUE:
            request(state, TB_SYMBOL_CALLABLE_FLAG, root, TB_SYMBOL_FLAG_REFERENCE);
            break;
        case CALL_RVALUE:
            request(state, TB_SYMBOL_CALLABLE_FLAG, root, TB_SYMBOL_FLAG_RVALUE_REFERENCE);
            break;
        case CALL_PROTOTYPED:
            request(state, TB_SYMBOL_CALLABLE_FLAG, root, TB_SYMBOL_FLAG_PROTOTYPED);
            break;
        case CALL_PARAMETER: parameter(state); break;
        case CALL_PARAMETER_GRAPH: parameter_graph(state); break;
        case CALL_ARTIFICIAL:
            request(state, TB_SYMBOL_CALLABLE_FLAG, state->parameters[state->parameter_index].graph,
                    TB_SYMBOL_FLAG_ARTIFICIAL);
            break;
        case CALL_PARAMETER_TYPE:
            request(state, TB_SYMBOL_CALLABLE_REFERENCES, state->parameters[state->parameter_index].graph,
                    TB_SYMBOL_INHERIT_TYPE);
            break;
        case CALL_PARAMETER_ADVANCE:
            ++state->parameter_index;
            state->phase = CALL_PARAMETER;
            break;
        case CALL_METHOD: method(state); break;
        case CALL_OBJECT_OUTER: case CALL_OBJECT_POINTEE: object_type(state); break;
        case CALL_CLASS: class_anchor(state); break;
        case CALL_CLASS_FIND: class_find(state); break;
        case CALL_DONE:
            state->status = state->view.reasons ? TB_SYMBOL_CALLABLE_PARTIAL : TB_SYMBOL_CALLABLE_READY;
            break;
        default: fail(state, TB_SYMBOL_CALLABLE_INVALID); break;
        }
        ++state->progress;
    }

    return state->status;
}


bool tb_symbol_callable_need(const TbSymbolCallable_t *state, TbSymbolCallableNeed_t *need)
{
    if (!state || !state->initialized || !need ||
        (state->status != TB_SYMBOL_CALLABLE_REFERENCES && state->status != TB_SYMBOL_CALLABLE_FLAG &&
         state->status != TB_SYMBOL_CALLABLE_TYPE)) {
        return false;
    }

    *need = state->need;
    return true;
}


TbSymbolCallableStatus_e tb_symbol_callable_references(TbSymbolCallable_t *state,
    TbSymbolReferenceTarget_t record, TbSymbolInheritedReferenceStatus_e status,
    const TbSymbolInheritedReferenceView_t *references)
{
    if (!state || !state->initialized || state->status != TB_SYMBOL_CALLABLE_REFERENCES ||
        !same_record(record, state->need.record))
        return TB_SYMBOL_CALLABLE_INVALID;

    if (status != TB_SYMBOL_INHERITED_REFERENCE_FOUND && status != TB_SYMBOL_INHERITED_REFERENCE_ABSENT) {
        switch (status) {
        case TB_SYMBOL_INHERITED_REFERENCE_CONFLICT: fail(state, TB_SYMBOL_CALLABLE_CONFLICT); break;
        case TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY: fail(state, TB_SYMBOL_CALLABLE_DEPENDENCY); break;
        case TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED: fail(state, TB_SYMBOL_CALLABLE_UNSUPPORTED); break;
        case TB_SYMBOL_INHERITED_REFERENCE_MALFORMED: fail(state, TB_SYMBOL_CALLABLE_MALFORMED); break;
        case TB_SYMBOL_INHERITED_REFERENCE_RESOURCE: fail(state, TB_SYMBOL_CALLABLE_RESOURCE); break;
        default: return TB_SYMBOL_CALLABLE_INVALID;
        }
        return state->status;
    }

    if (!references || references->name != state->need.field ||
        !same_record(references->selected, state->need.record) ||
        !same_graph_root(references->graph, state->need.graph) || !references->slots ||
        references->count > SIZE_MAX / sizeof(*references->facts) ||
        (references->count && !references->facts) ||
        ((status == TB_SYMBOL_INHERITED_REFERENCE_FOUND) != (references->count != 0))) {
        return TB_SYMBOL_CALLABLE_INVALID;
    }

    state->status = TB_SYMBOL_CALLABLE_PENDING;
    if (state->phase == CALL_OBJECT) {
        state->view.object = *references;
        state->phase       = CALL_OBJECT_FIND;
        return state->status;
    }

    bool result = state->phase == CALL_RETURN;
    state->active_type = result ? &state->view.result : &state->parameters[state->parameter_index].type;
    state->active_type->references = *references;
    state->active_type->absent = references->count == 0;
    state->type_next = result ? CALL_OBJECT : CALL_PARAMETER_ADVANCE;
    if (!references->count) {
        if (!result)
            state->view.reasons |= TB_SYMBOL_CALLABLE_MISSING_TYPE;
        state->phase = state->type_next;
    } else {
        state->scan  = 0;
        state->phase = CALL_REFERENCE_COMPARE;
    }

    return state->status;
}


TbSymbolCallableStatus_e tb_symbol_callable_flag(TbSymbolCallable_t *state,
    TbSymbolReferenceTarget_t record, TbSymbolInheritedFlagStatus_e status,
    const TbSymbolInheritedFlagView_t *flag)
{
    if (!state || !state->initialized || state->status != TB_SYMBOL_CALLABLE_FLAG ||
        !same_record(record, state->need.record))
        return TB_SYMBOL_CALLABLE_INVALID;

    if (status != TB_SYMBOL_FLAG_FOUND && status != TB_SYMBOL_FLAG_ABSENT) {
        switch (status) {
        case TB_SYMBOL_FLAG_CONFLICT: fail(state, TB_SYMBOL_CALLABLE_CONFLICT); break;
        case TB_SYMBOL_FLAG_DEPENDENCY: fail(state, TB_SYMBOL_CALLABLE_DEPENDENCY); break;
        case TB_SYMBOL_FLAG_UNSUPPORTED: fail(state, TB_SYMBOL_CALLABLE_UNSUPPORTED); break;
        case TB_SYMBOL_FLAG_MALFORMED: fail(state, TB_SYMBOL_CALLABLE_MALFORMED); break;
        case TB_SYMBOL_FLAG_RESOURCE: fail(state, TB_SYMBOL_CALLABLE_RESOURCE); break;
        default: return TB_SYMBOL_CALLABLE_INVALID;
        }
        return state->status;
    }

    if (!flag || flag->name != state->need.field || !same_record(flag->selected, state->need.record) ||
        !same_graph_root(flag->graph, state->need.graph) || !flag->slots ||
        flag->value_available != flag->value ||
        ((status == TB_SYMBOL_FLAG_FOUND) != flag->value) ||
        ((status == TB_SYMBOL_FLAG_FOUND) != (flag->owner_count != 0))) {
        return TB_SYMBOL_CALLABLE_INVALID;
    }

    switch (state->phase) {
    case CALL_LVALUE:
        state->view.reference = *flag;
        state->phase = CALL_RVALUE;
        break;
    case CALL_RVALUE:
        state->view.rvalue_reference = *flag;
        state->phase = CALL_PROTOTYPED;
        break;
    case CALL_PROTOTYPED:
        state->view.prototyped = *flag;
        state->view.root_fields_available = true;
        state->phase = CALL_PARAMETER;
        break;
    case CALL_ARTIFICIAL: {
        TbSymbolCallableParameter_t *parameter = &state->parameters[state->parameter_index];
        parameter->artificial           = *flag;
        parameter->artificial_available = true;
        if (flag->value) {
            if (state->parameter_index == state->view.object_parameter) {
                parameter->kind = TB_SYMBOL_CALLABLE_OBJECT_PARAMETER;
            } else {
                parameter->kind = TB_SYMBOL_CALLABLE_ARTIFICIAL_ARGUMENT;
                state->view.reasons |= TB_SYMBOL_CALLABLE_ARTIFICIAL_PARAMETER;
                if (state->view.member == TB_SYMBOL_CALLABLE_STATIC_MEMBER)
                    state->view.member = TB_SYMBOL_CALLABLE_UNKNOWN_MEMBER;
            }
        }
        state->phase = CALL_PARAMETER_TYPE;
        break;
    }
    default: return TB_SYMBOL_CALLABLE_INVALID;
    }
    state->status = TB_SYMBOL_CALLABLE_PENDING;

    return state->status;
}


TbSymbolCallableStatus_e tb_symbol_callable_type(TbSymbolCallable_t *state,
    TbSymbolReferenceTarget_t record, TbSymbolTypeRecipeStatus_e status,
    const TbSymbolTypeRecipeView_t *type)
{
    if (!state || !state->initialized || state->status != TB_SYMBOL_CALLABLE_TYPE ||
        !same_record(record, state->need.record))
        return TB_SYMBOL_CALLABLE_INVALID;

    if (status == TB_SYMBOL_TYPE_UNSUPPORTED) {
        state->active_type->recipe_status = status;
        state->view.reasons |= TB_SYMBOL_CALLABLE_TYPE_LIMIT;
    } else if (status == TB_SYMBOL_TYPE_READY || status == TB_SYMBOL_TYPE_PARTIAL) {
        if (!type || !type->nodes || !type->count || type->count > SIZE_MAX / sizeof(*type->nodes) ||
            !same_record(type->selected, state->need.record) ||
            !same_record(type->nodes[0].record, state->need.record) ||
            ((status == TB_SYMBOL_TYPE_READY) != type->complete)) {
            return TB_SYMBOL_CALLABLE_INVALID;
        }

        state->active_type->recipe           = *type;
        state->active_type->recipe_status    = status;
        state->active_type->recipe_available = true;
        if (!type->complete)
            state->view.reasons |= TB_SYMBOL_CALLABLE_TYPE_LIMIT;
    } else {
        switch (status) {
        case TB_SYMBOL_TYPE_CONFLICT: fail(state, TB_SYMBOL_CALLABLE_CONFLICT); break;
        case TB_SYMBOL_TYPE_DEPENDENCY: fail(state, TB_SYMBOL_CALLABLE_DEPENDENCY); break;
        case TB_SYMBOL_TYPE_MALFORMED: fail(state, TB_SYMBOL_CALLABLE_MALFORMED); break;
        case TB_SYMBOL_TYPE_RESOURCE: fail(state, TB_SYMBOL_CALLABLE_RESOURCE); break;
        case TB_SYMBOL_TYPE_OPERATIONAL: fail(state, TB_SYMBOL_CALLABLE_OPERATIONAL); break;
        default: return TB_SYMBOL_CALLABLE_INVALID;
        }
        return state->status;
    }

    state->phase  = state->type_next;
    state->status = TB_SYMBOL_CALLABLE_PENDING;
    return state->status;
}


bool tb_symbol_callable_view(const TbSymbolCallable_t *state, TbSymbolCallableView_t *view)
{
    if (!state || !state->initialized || !view ||
        (state->status != TB_SYMBOL_CALLABLE_READY && state->status != TB_SYMBOL_CALLABLE_PARTIAL)) {
        return false;
    }

    *view = state->view;
    return true;
}


bool tb_symbol_callable_failure(const TbSymbolCallable_t *state, TbSymbolCallableFailure_t *failure)
{
    if (!state || !state->initialized || !failure || state->status < TB_SYMBOL_CALLABLE_CONFLICT)
        return false;

    *failure = state->failure;
    return true;
}


uint64_t tb_symbol_callable_progress(const TbSymbolCallable_t *state)
{
    return state && state->initialized ? state->progress : 0;
}


void tb_symbol_callable_discard(TbSymbolCallable_t *state)
{
    if (state)
        *state = (TbSymbolCallable_t) {0};
}
