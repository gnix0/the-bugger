#include "symbol/parameter_source.h"

typedef enum {
    SOURCE_INITIALIZE,
    SOURCE_REQUEST,
    SOURCE_CHILDREN,
    SOURCE_ANCESTOR,
    SOURCE_SIGNATURE,
    SOURCE_EDGE,
    SOURCE_FIND,
    SOURCE_RESOLVE,
    SOURCE_FACT,
    SOURCE_LINK
} SourcePhase_e;

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset && left.entry_index == right.entry_index;
}


TbSymbolParameterSourceStatus_e tb_symbol_parameter_source_init(
    TbSymbolParameterSource_t *state, TbSymbolParameterLinksView_t correspondence,
    TbSymbolParameterSourceSlot_t *slots, size_t capacity)
{
    if (!state || !correspondence.callables.nodes || !correspondence.callables.count ||
        !correspondence.scopes || !slots ||
        (correspondence.fact_count && !correspondence.facts) ||
        (correspondence.link_count && !correspondence.links) ||
        correspondence.callables.count > SIZE_MAX / sizeof(*correspondence.callables.nodes) ||
        correspondence.callables.count > SIZE_MAX / sizeof(*correspondence.scopes) ||
        correspondence.fact_count > SIZE_MAX / sizeof(*correspondence.facts) ||
        correspondence.link_count > SIZE_MAX / sizeof(*correspondence.links) ||
        capacity > SIZE_MAX / sizeof(*slots)) {
        return TB_SYMBOL_PARAMETER_SOURCE_INVALID;
    }

    if (capacity < correspondence.callables.count)
        return TB_SYMBOL_PARAMETER_SOURCE_RESOURCE;

    *state = (TbSymbolParameterSource_t) {
        .correspondence       = correspondence,
        .slots                = slots,
        .status               = TB_SYMBOL_PARAMETER_SOURCE_PENDING,
        .cursor               = UINT64_MAX,
        .first_unlinked_scope = SIZE_MAX,
        .first_unlinked       = SIZE_MAX,
        .phase                = SOURCE_INITIALIZE,
        .initialized          = true
    };

    return state->status;
}


static void fail(TbSymbolParameterSource_t *state, TbSymbolParameterSourceStatus_e status)
{
    state->status  = status;
    state->failure = (TbSymbolParameterSourceFailure_t) {
        .status          = status,
        .scope_index     = state->scope_index,
        .entry_index     = state->cursor,
        .attribute_index = UINT64_MAX
    };
}


static void initialize(TbSymbolParameterSource_t *state)
{
    if (state->scope_index == state->correspondence.callables.count) {
        state->scope_index = 0;
        state->phase       = SOURCE_REQUEST;
        return;
    }

    const TbSymbolGraphNode_t      *node  = &state->correspondence.callables.nodes[state->scope_index];
    const TbSymbolParametersView_t *scope = &state->correspondence.scopes[state->scope_index];
    if (!node->complete || (node->context.tag != 0x2e && node->context.tag != 0x1d) ||
        (node->present & ~3u) || !same_record(node->record, scope->selected) ||
        (scope->count && !scope->parameters) || scope->count > SIZE_MAX / sizeof(*scope->parameters)) {
        fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
        return;
    }

    state->slots[state->scope_index++] = (TbSymbolParameterSourceSlot_t) {
        .targets             = {SIZE_MAX, SIZE_MAX},
        .selected_scope      = SIZE_MAX,
        .first_signature     = UINT64_MAX,
        .signature_attribute = UINT64_MAX,
        .first_child         = UINT64_MAX
    };
}


/* These standard constructs do not add source formal parameters to this direct
 * list. In particular, vendor parameter packs are deliberately not ignored. */
static bool non_parameter(const TbSymbolDieEntry_t *entry, unsigned version)
{
    switch (entry->tag) {
    case 0x01: case 0x02: case 0x04: case 0x0a: case 0x0b: case 0x0f: case 0x10:
    case 0x13: case 0x15: case 0x16: case 0x17: case 0x1d: case 0x1f: case 0x21:
    case 0x24: case 0x26: case 0x2e: case 0x2f: case 0x30: case 0x34: case 0x35:
    case 0x37: case 0x3a: case 0x3b: case 0x3d: case 0x42:
        return true;
    case 0x44: case 0x47:
        return version >= 5;
    default:
        return false;
    }
}


static void children(TbSymbolParameterSource_t *state)
{
    TbSymbolParameterSourceSlot_t  *slot  = &state->slots[state->scope_index];
    const TbSymbolParametersView_t *scope = &state->correspondence.scopes[state->scope_index];
    if (state->cursor == slot->tree.count ||
        slot->tree.entries[state->cursor].parent < scope->selected.entry_index) {
        if (state->parameter_index != scope->count) {
            fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
            return;
        }

        state->ancestor = scope->selected.entry_index;
        state->phase    = SOURCE_ANCESTOR;
        return;
    }

    const TbSymbolDieEntry_t *entry = &slot->tree.entries[state->cursor];
    if (entry->parent >= state->cursor) {
        fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
        return;
    }

    if (entry->parent == scope->selected.entry_index) {
        if (entry->tag == 0x05 || entry->tag == 0x18) {
            if (state->parameter_index == scope->count ||
                scope->parameters[state->parameter_index].record.entry_index != state->cursor ||
                scope->parameters[state->parameter_index].record.record_offset != entry->offset ||
                scope->parameters[state->parameter_index].unspecified != (entry->tag == 0x18)) {
                fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
                return;
            }

            if (slot->variadic) {
                fail(state, TB_SYMBOL_PARAMETER_SOURCE_CONFLICT);
                return;
            }

            slot->variadic = entry->tag == 0x18;
            ++state->parameter_index;
        } else if (!non_parameter(entry,
                       state->correspondence.callables.nodes[state->scope_index].context.version) &&
                   slot->first_child == UINT64_MAX) {
            slot->first_child = state->cursor;
        }
    }

    ++state->cursor;
}


static void ancestor(TbSymbolParameterSource_t *state)
{
    TbSymbolParameterSourceSlot_t *slot = &state->slots[state->scope_index];
    uint64_t parent = slot->tree.entries[state->ancestor].parent;
    if (parent == UINT64_MAX) {
        state->edge  = 0;
        state->phase = SOURCE_EDGE;
        return;
    }

    if (parent >= state->ancestor) {
        fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
        return;
    }

    state->ancestor = parent;
    uint64_t tag = slot->tree.entries[parent].tag;
    if (tag != 0x02 && tag != 0x13 && tag != 0x17)
        return;

    TbSymbolAttributeSelection_t selection = {
        .die_index = parent,
        .name      = 0x69
    };
    if (tb_symbol_attributes_init(&state->signature, slot->tree, selection) != TB_SYMBOL_ATTRIBUTE_PENDING) {
        fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
        return;
    }

    state->phase = SOURCE_SIGNATURE;
}


static void signature(TbSymbolParameterSource_t *state)
{
    TbSymbolAttributeStatus_e status = tb_symbol_attributes_step(&state->signature, 1);
    if (status == TB_SYMBOL_ATTRIBUTE_PENDING)
        return;

    if (status != TB_SYMBOL_ATTRIBUTE_UNIQUE && status != TB_SYMBOL_ATTRIBUTE_MISSING) {
        fail(state, status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS ? TB_SYMBOL_PARAMETER_SOURCE_CONFLICT
                                                          : TB_SYMBOL_PARAMETER_SOURCE_INVALID);
        state->failure.entry_index = state->ancestor;
        return;
    }

    if (status == TB_SYMBOL_ATTRIBUTE_UNIQUE) {
        uint64_t index;
        TbSymbolParameterSourceSlot_t *slot = &state->slots[state->scope_index];
        if (!tb_symbol_attributes_unique(&state->signature, &index)) {
            fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
            return;
        }

        const TbSymbolFormView_t *value = &slot->tree.attributes[index].value;
        if (value->form != TB_SYMBOL_FORM_REF_SIG8 || !value->numeric) {
            fail(state, TB_SYMBOL_PARAMETER_SOURCE_MALFORMED);
            state->failure.entry_index     = state->ancestor;
            state->failure.attribute_index = index;
            return;
        }

        slot->reduced = true;
        if (slot->first_signature == UINT64_MAX) {
            slot->first_signature     = state->ancestor;
            slot->signature_attribute = index;
        }
    }

    state->phase = SOURCE_ANCESTOR;
}


static void edge(TbSymbolParameterSource_t *state)
{
    if (state->edge == 2) {
        ++state->scope_index;
        state->phase = SOURCE_REQUEST;
        return;
    }

    if (state->correspondence.callables.nodes[state->scope_index].present & (1u << state->edge)) {
        state->find  = 0;
        state->phase = SOURCE_FIND;
    } else {
        ++state->edge;
    }
}


static void find(TbSymbolParameterSource_t *state)
{
    TbSymbolGraphView_t graph = state->correspondence.callables;
    if (state->find == graph.count) {
        fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
        return;
    }

    TbSymbolReferenceTarget_t target = graph.nodes[state->scope_index].edges[state->edge].target;
    if (same_record(target, graph.nodes[state->find].record)) {
        state->slots[state->scope_index].targets[state->edge++] = state->find;
        state->phase = SOURCE_EDGE;
    }
    ++state->find;
}


static void resolve(TbSymbolParameterSource_t *state)
{
    size_t count = state->correspondence.callables.count;
    if (state->scope_index == count) {
        if (!state->remaining) {
            state->scope_index = 0;
            state->reasons     = state->slots[0].reasons;
            state->phase       = SOURCE_FACT;
        } else if (!state->changed) {
            fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
        } else {
            state->scope_index = 0;
            state->changed     = false;
        }
        return;
    }

    size_t                         index         = state->scope_index++;
    TbSymbolParameterSourceSlot_t *slot          = &state->slots[index];
    size_t                         specification = slot->targets[TB_SYMBOL_RELATION_SPECIFICATION];
    size_t                         origin        = slot->targets[TB_SYMBOL_RELATION_ORIGIN];
    if (slot->resolved || (specification != SIZE_MAX && !state->slots[specification].resolved) ||
        (origin != SIZE_MAX && !state->slots[origin].resolved)) {
        return;
    }

    if (origin == SIZE_MAX && state->correspondence.callables.nodes[index].context.tag == 0x1d) {
        slot->selected_scope = SIZE_MAX;
    } else if (origin != SIZE_MAX) {
        slot->selected_scope = state->slots[origin].selected_scope;
        slot->reasons        = state->slots[origin].reasons;
        if (specification != SIZE_MAX) {
            const TbSymbolParameterSourceSlot_t *other = &state->slots[specification];
            if ((other->reasons & (unsigned)TB_SYMBOL_PARAMETER_SOURCE_ALTERNATIVES) ||
                (other->selected_scope != SIZE_MAX && other->selected_scope != slot->selected_scope)) {
                slot->selected_scope = SIZE_MAX;
                slot->reasons |= (unsigned)TB_SYMBOL_PARAMETER_SOURCE_ALTERNATIVES;
            }
        }
    } else if (specification != SIZE_MAX &&
               (state->slots[specification].selected_scope != SIZE_MAX ||
                (state->slots[specification].reasons & (unsigned)TB_SYMBOL_PARAMETER_SOURCE_ALTERNATIVES))) {
        slot->selected_scope = state->slots[specification].selected_scope;
        slot->reasons        = state->slots[specification].reasons;
    } else if (!slot->reduced && state->correspondence.callables.nodes[index].context.tag != 0x1d) {
        slot->selected_scope = index;
        slot->reasons        = slot->first_child == UINT64_MAX ? 0u : TB_SYMBOL_PARAMETER_SOURCE_CHILD;
    }

    if (slot->selected_scope == SIZE_MAX)
        slot->reasons |= (unsigned)TB_SYMBOL_PARAMETER_SOURCE_NO_LIST;

    slot->resolved = true;
    --state->remaining;
    state->changed = true;
}


static void fact(TbSymbolParameterSource_t *state)
{
    const TbSymbolParameterLinksView_t *input = &state->correspondence;
    if (state->fact_index == input->fact_count) {
        state->status = state->reasons ? TB_SYMBOL_PARAMETER_SOURCE_PARTIAL
                                       : TB_SYMBOL_PARAMETER_SOURCE_READY;
        return;
    }

    const TbSymbolParameterLinkFact_t *current = &input->facts[state->fact_index];
    if (current->first_link > input->link_count ||
        current->link_count > input->link_count - current->first_link ||
        current->scope_index >= input->callables.count ||
        current->parameter_index >= input->scopes[current->scope_index].count) {
        fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
        return;
    }

    size_t selected = state->slots[current->scope_index].selected_scope;
    if (selected == SIZE_MAX || selected == current->scope_index) {
        ++state->fact_index;
        return;
    }

    state->link_index = 0;
    state->linked     = false;
    state->phase      = SOURCE_LINK;
}


static void link(TbSymbolParameterSource_t *state)
{
    const TbSymbolParameterLinkFact_t *current = &state->correspondence.facts[state->fact_index];
    if (state->link_index == current->link_count) {
        if (!state->linked) {
            state->reasons |= (unsigned)TB_SYMBOL_PARAMETER_SOURCE_UNLINKED;
            state->slots[current->scope_index].reasons |= (unsigned)TB_SYMBOL_PARAMETER_SOURCE_UNLINKED;
            if (state->first_unlinked == SIZE_MAX) {
                state->first_unlinked_scope = current->scope_index;
                state->first_unlinked       = current->parameter_index;
            }
        }

        ++state->fact_index;
        state->phase = SOURCE_FACT;
        return;
    }

    const TbSymbolParameterLink_t *current_link =
        &state->correspondence.links[current->first_link + state->link_index++];
    if (current_link->target_scope == state->slots[current->scope_index].selected_scope)
        state->linked = true;
}


TbSymbolParameterSourceStatus_e tb_symbol_parameter_source_step(TbSymbolParameterSource_t *state,
                                                               size_t max_steps)
{
    if (!state || !state->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_PARAMETER_SOURCE_INVALID;

    for (size_t i = 0; i < max_steps && state->status == TB_SYMBOL_PARAMETER_SOURCE_PENDING; ++i) {
        if (state->progress == UINT64_MAX) {
            fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID);
            break;
        }

        switch (state->phase) {
        case SOURCE_INITIALIZE: initialize(state); break;
        case SOURCE_REQUEST:
            if (state->scope_index == state->correspondence.callables.count) {
                state->scope_index = 0;
                state->remaining   = state->correspondence.callables.count;
                state->phase       = SOURCE_RESOLVE;
            } else {
                state->cursor = state->correspondence.callables.nodes[state->scope_index].record.entry_index;
                state->status = TB_SYMBOL_PARAMETER_SOURCE_SOURCE;
            }
            break;
        case SOURCE_CHILDREN: children(state); break;
        case SOURCE_ANCESTOR: ancestor(state); break;
        case SOURCE_SIGNATURE: signature(state); break;
        case SOURCE_EDGE: edge(state); break;
        case SOURCE_FIND: find(state); break;
        case SOURCE_RESOLVE: resolve(state); break;
        case SOURCE_FACT: fact(state); break;
        case SOURCE_LINK: link(state); break;
        default: fail(state, TB_SYMBOL_PARAMETER_SOURCE_INVALID); break;
        }
        ++state->progress;
    }

    return state->status;
}


bool tb_symbol_parameter_source_need(const TbSymbolParameterSource_t *state, TbSymbolGraphNeed_t *need)
{
    if (!state || !state->initialized || state->status != TB_SYMBOL_PARAMETER_SOURCE_SOURCE || !need)
        return false;

    *need = (TbSymbolGraphNeed_t) {
        .kind   = TB_SYMBOL_GRAPH_SOURCE,
        .record = state->correspondence.callables.nodes[state->scope_index].record
    };

    return true;
}


TbSymbolParameterSourceStatus_e tb_symbol_parameter_source_source(
    TbSymbolParameterSource_t *state, const TbSymbolUnitsView_t *units,
    const TbSymbolDiesView_t *tree, uint64_t unit_index)
{
    if (!state || !state->initialized || state->status != TB_SYMBOL_PARAMETER_SOURCE_SOURCE)
        return TB_SYMBOL_PARAMETER_SOURCE_INVALID;

    if (!units || !tree) {
        fail(state, TB_SYMBOL_PARAMETER_SOURCE_DEPENDENCY);
        return state->status;
    }

    const TbSymbolGraphNode_t *node = &state->correspondence.callables.nodes[state->scope_index];
    if (units->count > SIZE_MAX / sizeof(*units->entries) ||
        tree->count > SIZE_MAX / sizeof(*tree->entries) ||
        tree->attribute_count > SIZE_MAX / sizeof(*tree->attributes) ||
        (tree->attribute_count && !tree->attributes) ||
        !tb_symbol_graph_node_matches(node, *units, *tree, unit_index)) {
        return TB_SYMBOL_PARAMETER_SOURCE_INVALID;
    }

    state->slots[state->scope_index].tree = *tree;
    state->cursor          = node->record.entry_index + 1;
    state->parameter_index = 0;
    state->phase           = SOURCE_CHILDREN;
    state->status          = TB_SYMBOL_PARAMETER_SOURCE_PENDING;

    return state->status;
}


bool tb_symbol_parameter_source_view(const TbSymbolParameterSource_t *state,
                                      TbSymbolParameterSourceView_t *view)
{
    if (!state || !state->initialized || !view ||
        (state->status != TB_SYMBOL_PARAMETER_SOURCE_READY &&
         state->status != TB_SYMBOL_PARAMETER_SOURCE_PARTIAL)) {
        return false;
    }

    *view = (TbSymbolParameterSourceView_t) {
        .correspondence           = state->correspondence,
        .slots                    = state->slots,
        .selected_scope           = state->slots[0].selected_scope,
        .first_unlinked_scope     = state->first_unlinked_scope,
        .first_unlinked_parameter = state->first_unlinked,
        .reasons                  = state->reasons
    };

    return true;
}


bool tb_symbol_parameter_source_failure(const TbSymbolParameterSource_t *state,
                                         TbSymbolParameterSourceFailure_t *failure)
{
    if (!state || !state->initialized || !failure || state->status < TB_SYMBOL_PARAMETER_SOURCE_CONFLICT)
        return false;

    *failure = state->failure;
    return true;
}


uint64_t tb_symbol_parameter_source_progress(const TbSymbolParameterSource_t *state)
{
    return state && state->initialized ? state->progress : 0;
}


void tb_symbol_parameter_source_discard(TbSymbolParameterSource_t *state)
{
    if (state)
        *state = (TbSymbolParameterSource_t) {0};
}
