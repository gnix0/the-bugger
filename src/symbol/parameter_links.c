#include "symbol/parameter_links.h"

typedef enum {
    LINKS_SCOPE,
    LINKS_VALIDATE_PARAMETER,
    LINKS_REQUEST,
    LINKS_NODE,
    LINKS_FIND,
    LINKS_REACH_CLEAR,
    LINKS_REACH_EDGE,
    LINKS_REACH_FIND,
    LINKS_DUPLICATE
} LinksPhase_e;

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset && left.entry_index == right.entry_index;
}


static bool parent_matches(const TbSymbolGraphNode_t *parameter, const TbSymbolGraphNode_t *callable)
{
    TbSymbolReferenceTarget_t parent = parameter->record;
    parent.record_offset = parameter->context.parent_record;
    parent.entry_index   = callable->record.entry_index;

    return same_record(parent, callable->record) && parameter->context.parent_tag == callable->context.tag &&
           parameter->context.version == callable->context.version &&
           parameter->context.unit_kind == callable->context.unit_kind &&
           parameter->context.abbrev_section == callable->context.abbrev_section &&
           parameter->context.abbrev_offset == callable->context.abbrev_offset;
}


TbSymbolParameterLinksStatus_e tb_symbol_parameter_links_init(
    TbSymbolParameterLinks_t *state, TbSymbolGraphView_t callables,
    const TbSymbolParametersView_t *scopes, size_t scope_count,
    TbSymbolParameterLinkFact_t *facts, size_t fact_capacity,
    TbSymbolParameterLink_t *links, size_t link_capacity,
    bool *reachable, size_t reachable_capacity)
{
    if (!state || !callables.nodes || !callables.count || !scopes || scope_count != callables.count ||
        !facts || !links || !reachable || reachable_capacity > SIZE_MAX / sizeof(*reachable) ||
        callables.count > SIZE_MAX / sizeof(*callables.nodes) ||
        scope_count > SIZE_MAX / sizeof(*scopes) || fact_capacity > SIZE_MAX / sizeof(*facts) ||
        link_capacity > SIZE_MAX / sizeof(*links)) {
        return TB_SYMBOL_PARAMETER_LINKS_INVALID;
    }

    if (reachable_capacity < callables.count)
        return TB_SYMBOL_PARAMETER_LINKS_RESOURCE;

    *state = (TbSymbolParameterLinks_t) {
        .callables     = callables,
        .scopes        = scopes,
        .facts         = facts,
        .links         = links,
        .reachable     = reachable,
        .status        = TB_SYMBOL_PARAMETER_LINKS_PENDING,
        .fact_capacity = fact_capacity,
        .link_capacity = link_capacity,
        .phase         = LINKS_SCOPE,
        .initialized   = true
    };

    return state->status;
}


static void fail(TbSymbolParameterLinks_t *state, TbSymbolParameterLinksStatus_e status)
{
    state->status  = status;
    state->failure = (TbSymbolParameterLinksFailure_t) {
        .status          = status,
        .scope_index     = state->scope_index,
        .parameter_index = state->parameter_index,
        .graph_node      = state->phase >= LINKS_NODE ? state->graph_node : SIZE_MAX,
        .previous_link   = SIZE_MAX
    };
}


static void scope(TbSymbolParameterLinks_t *state)
{
    if (state->scope_index == state->callables.count) {
        state->scope_index     = 0;
        state->parameter_index = 0;
        state->phase           = LINKS_REQUEST;
        return;
    }

    const TbSymbolGraphNode_t      *node = &state->callables.nodes[state->scope_index];
    const TbSymbolParametersView_t *view = &state->scopes[state->scope_index];
    if (!node->complete || (node->context.tag != 0x2e && node->context.tag != 0x1d) ||
        !same_record(node->record, view->selected) || (view->count && !view->parameters) ||
        view->count > SIZE_MAX / sizeof(*view->parameters)) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_INVALID);
        return;
    }

    state->phase = LINKS_VALIDATE_PARAMETER;
}


static void validate_parameter(TbSymbolParameterLinks_t *state)
{
    const TbSymbolParametersView_t *view = &state->scopes[state->scope_index];
    if (state->parameter_index == view->count) {
        ++state->scope_index;
        state->parameter_index = 0;
        state->phase           = LINKS_SCOPE;
        return;
    }

    const TbSymbolParameter_t  *parameter = &view->parameters[state->parameter_index];
    const TbSymbolEntityView_t *direct    = &parameter->direct;
    TbSymbolReferenceTarget_t record = {
        .backing       = direct->backing,
        .section_index = direct->section_index,
        .unit_offset   = direct->unit_offset,
        .record_offset = direct->record_offset,
        .entry_index   = parameter->record.entry_index
    };
    if (!same_record(record, parameter->record) || direct->parent_index != view->selected.entry_index ||
        (direct->tag != 0x05 && direct->tag != 0x18) || parameter->unspecified != (direct->tag == 0x18) ||
        parameter->record.entry_index <= view->selected.entry_index ||
        parameter->record.backing.key.device != view->selected.backing.key.device ||
        parameter->record.backing.key.inode != view->selected.backing.key.inode ||
        parameter->record.backing.bytes != view->selected.backing.bytes ||
        parameter->record.section_index != view->selected.section_index ||
        parameter->record.unit_offset != view->selected.unit_offset ||
        direct->abbrev_section != state->callables.nodes[state->scope_index].context.abbrev_section ||
        direct->abbrev_offset != state->callables.nodes[state->scope_index].context.abbrev_offset ||
        (state->parameter_index &&
         (view->parameters[state->parameter_index - 1].record.entry_index >= parameter->record.entry_index ||
          view->parameters[state->parameter_index - 1].record.record_offset >=
              parameter->record.record_offset))) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_INVALID);
        return;
    }

    ++state->parameter_index;
}


static void request(TbSymbolParameterLinks_t *state)
{
    if (state->scope_index == state->callables.count) {
        state->status = TB_SYMBOL_PARAMETER_LINKS_READY;
        return;
    }

    const TbSymbolParametersView_t *view = &state->scopes[state->scope_index];
    if (state->parameter_index == view->count) {
        ++state->scope_index;
        state->parameter_index = 0;
        return;
    }

    if (view->parameters[state->parameter_index].unspecified) {
        ++state->parameter_index;
    } else if (state->fact_count == state->fact_capacity) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_RESOURCE);
    } else {
        state->status = TB_SYMBOL_PARAMETER_LINKS_GRAPH;
    }
}


static void node(TbSymbolParameterLinks_t *state)
{
    if (state->graph_node == state->pending.count) {
        ++state->fact_count;
        ++state->parameter_index;
        state->phase = LINKS_REQUEST;
        return;
    }

    const TbSymbolGraphNode_t *target = &state->pending.nodes[state->graph_node];
    if (!target->complete || target->context.tag != 0x05) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_INVALID);
        return;
    }

    state->find_scope     = 0;
    state->find_parameter = 0;
    state->phase          = LINKS_FIND;
}


static void find(TbSymbolParameterLinks_t *state)
{
    if (state->find_scope == state->callables.count) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_CONFLICT);
        return;
    }

    const TbSymbolGraphNode_t      *target = &state->pending.nodes[state->graph_node];
    const TbSymbolParametersView_t *view   = &state->scopes[state->find_scope];
    if (!parent_matches(target, &state->callables.nodes[state->find_scope]) ||
        state->find_parameter == view->count) {
        ++state->find_scope;
        state->find_parameter = 0;
        return;
    }

    const TbSymbolParameter_t *parameter = &view->parameters[state->find_parameter];
    if (!same_record(parameter->record, target->record)) {
        ++state->find_parameter;
        return;
    }

    if (parameter->unspecified) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_CONFLICT);
        return;
    }

    state->candidate_scope     = state->find_scope;
    state->candidate_parameter = state->find_parameter;
    state->previous_link       = 0;
    state->reach_clear         = 0;
    state->phase               = LINKS_REACH_CLEAR;
}


static void reach_clear(TbSymbolParameterLinks_t *state)
{
    if (state->reach_clear < state->callables.count) {
        state->reachable[state->reach_clear++] = false;
        return;
    }

    state->reachable[state->scope_index] = true;
    state->reach_node    = 0;
    state->reach_edge    = 0;
    state->reach_changed = false;
    state->phase        = LINKS_REACH_EDGE;
}


static void reach_edge(TbSymbolParameterLinks_t *state)
{
    if (state->reachable[state->candidate_scope]) {
        state->phase = LINKS_DUPLICATE;
        return;
    }

    if (state->reach_node == state->callables.count) {
        if (!state->reach_changed) {
            fail(state, TB_SYMBOL_PARAMETER_LINKS_CONFLICT);
        } else {
            state->reach_node    = 0;
            state->reach_edge    = 0;
            state->reach_changed = false;
        }
        return;
    }

    const TbSymbolGraphNode_t *owner = &state->callables.nodes[state->reach_node];
    if (!state->reachable[state->reach_node] || state->reach_edge == TB_SYMBOL_GRAPH_RELATIONS) {
        ++state->reach_node;
        state->reach_edge = 0;
    } else if (!(owner->present & (1U << state->reach_edge))) {
        ++state->reach_edge;
    } else {
        state->reach_find = 0;
        state->phase      = LINKS_REACH_FIND;
    }
}


static void reach_find(TbSymbolParameterLinks_t *state)
{
    if (state->reach_find == state->callables.count) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_INVALID);
        return;
    }

    const TbSymbolGraphNode_t *owner     = &state->callables.nodes[state->reach_node];
    size_t                     candidate = state->reach_find++;
    if (!same_record(owner->edges[state->reach_edge].target, state->callables.nodes[candidate].record))
        return;

    if (!state->reachable[candidate]) {
        state->reachable[candidate] = true;
        state->reach_changed        = true;
    }
    ++state->reach_edge;
    state->phase = LINKS_REACH_EDGE;
}


static void duplicate(TbSymbolParameterLinks_t *state)
{
    if (state->previous_link < state->link_count) {
        size_t                         index = state->previous_link++;
        const TbSymbolParameterLink_t *link  = &state->links[index];
        if (link->source_scope == state->scope_index && link->target_scope == state->candidate_scope &&
            (link->source_parameter == state->parameter_index ||
             link->target_parameter == state->candidate_parameter)) {
            fail(state, TB_SYMBOL_PARAMETER_LINKS_CONFLICT);
            state->failure.previous_link = index;
        }
        return;
    }

    if (state->link_count == state->link_capacity) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_RESOURCE);
        return;
    }

    state->links[state->link_count++] = (TbSymbolParameterLink_t) {
        .source_scope     = state->scope_index,
        .source_parameter = state->parameter_index,
        .target_scope     = state->candidate_scope,
        .target_parameter = state->candidate_parameter,
        .graph_node       = state->graph_node
    };
    ++state->facts[state->fact_count].link_count;
    ++state->graph_node;
    state->phase = LINKS_NODE;
}


TbSymbolParameterLinksStatus_e tb_symbol_parameter_links_step(TbSymbolParameterLinks_t *state,
                                                              size_t max_steps)
{
    if (!state || !state->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_PARAMETER_LINKS_INVALID;

    for (size_t step = 0; step < max_steps && state->status == TB_SYMBOL_PARAMETER_LINKS_PENDING; ++step) {
        if (state->progress == UINT64_MAX) {
            fail(state, TB_SYMBOL_PARAMETER_LINKS_INVALID);
            break;
        }
        ++state->progress;

        switch (state->phase) {
        case LINKS_SCOPE:
            scope(state);
            break;
        case LINKS_VALIDATE_PARAMETER:
            validate_parameter(state);
            break;
        case LINKS_REQUEST:
            request(state);
            break;
        case LINKS_NODE:
            node(state);
            break;
        case LINKS_FIND:
            find(state);
            break;
        case LINKS_REACH_CLEAR:
            reach_clear(state);
            break;
        case LINKS_REACH_EDGE:
            reach_edge(state);
            break;
        case LINKS_REACH_FIND:
            reach_find(state);
            break;
        case LINKS_DUPLICATE:
            duplicate(state);
            break;
        default:
            fail(state, TB_SYMBOL_PARAMETER_LINKS_INVALID);
            break;
        }
    }

    return state->status;
}


bool tb_symbol_parameter_links_need(const TbSymbolParameterLinks_t *state, size_t *scope_index,
                                     size_t *parameter_index, TbSymbolReferenceTarget_t *record)
{
    if (!state || !state->initialized || state->status != TB_SYMBOL_PARAMETER_LINKS_GRAPH ||
        !scope_index || !parameter_index || !record)
        return false;

    *scope_index     = state->scope_index;
    *parameter_index = state->parameter_index;
    *record          = state->scopes[state->scope_index].parameters[state->parameter_index].record;

    return true;
}


TbSymbolParameterLinksStatus_e tb_symbol_parameter_links_graph(TbSymbolParameterLinks_t *state,
                                                               const TbSymbolGraphView_t *graph)
{
    if (!state || !state->initialized || state->status != TB_SYMBOL_PARAMETER_LINKS_GRAPH)
        return TB_SYMBOL_PARAMETER_LINKS_INVALID;

    if (!graph) {
        fail(state, TB_SYMBOL_PARAMETER_LINKS_DEPENDENCY);
        return state->status;
    }

    const TbSymbolParameter_t *parameter =
        &state->scopes[state->scope_index].parameters[state->parameter_index];
    if (!graph->nodes || !graph->count || graph->count > SIZE_MAX / sizeof(*graph->nodes) ||
        !graph->nodes[0].complete || graph->nodes[0].context.tag != 0x05 ||
        !same_record(graph->nodes[0].record, parameter->record) ||
        !parent_matches(&graph->nodes[0], &state->callables.nodes[state->scope_index])) {
        return TB_SYMBOL_PARAMETER_LINKS_INVALID;
    }

    state->pending = *graph;
    state->facts[state->fact_count] = (TbSymbolParameterLinkFact_t) {
        .graph           = *graph,
        .scope_index     = state->scope_index,
        .parameter_index = state->parameter_index,
        .first_link      = state->link_count
    };

    state->graph_node = 1;
    state->phase      = LINKS_NODE;
    state->status     = TB_SYMBOL_PARAMETER_LINKS_PENDING;

    return state->status;
}


bool tb_symbol_parameter_links_view(const TbSymbolParameterLinks_t *state,
                                     TbSymbolParameterLinksView_t *view)
{
    if (!state || !state->initialized || state->status != TB_SYMBOL_PARAMETER_LINKS_READY || !view)
        return false;

    *view = (TbSymbolParameterLinksView_t) {
        .callables  = state->callables,
        .scopes     = state->scopes,
        .facts      = state->facts,
        .links      = state->links,
        .fact_count = state->fact_count,
        .link_count = state->link_count
    };

    return true;
}


bool tb_symbol_parameter_links_failure(const TbSymbolParameterLinks_t *state,
                                        TbSymbolParameterLinksFailure_t *failure)
{
    if (!state || !state->initialized || !failure || state->status == TB_SYMBOL_PARAMETER_LINKS_PENDING ||
        state->status == TB_SYMBOL_PARAMETER_LINKS_GRAPH || state->status == TB_SYMBOL_PARAMETER_LINKS_READY)
        return false;

    *failure = state->failure;

    return true;
}


uint64_t tb_symbol_parameter_links_progress(const TbSymbolParameterLinks_t *state)
{
    return state && state->initialized ? state->progress : 0;
}


void tb_symbol_parameter_links_discard(TbSymbolParameterLinks_t *state)
{
    if (state)
        *state = (TbSymbolParameterLinks_t) {0};
}
