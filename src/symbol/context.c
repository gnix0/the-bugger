#include "symbol/context.h"

TbSymbolContextStatus_e tb_symbol_context_init(TbSymbolContext_t *context, TbSymbolGraphView_t graph,
                                              TbSymbolContextAnchor_t *anchors, size_t capacity)
{
    if (!context || !graph.nodes || !graph.count || !anchors || capacity > SIZE_MAX / sizeof(*anchors))
        return TB_SYMBOL_CONTEXT_INVALID;

    if (capacity < graph.count)
        return TB_SYMBOL_CONTEXT_RESOURCE;

    *context = (TbSymbolContext_t) {
        .graph       = graph,
        .anchors     = anchors,
        .capacity    = capacity,
        .status      = TB_SYMBOL_CONTEXT_PENDING,
        .validating  = true,
        .initialized = true
    };

    return context->status;
}


static void fail(TbSymbolContext_t *context, TbSymbolContextStatus_e status,
                  TbSymbolContextFailure_e reason)
{
    context->status  = status;
    context->failure = (TbSymbolContextFailure_t) {
        .reason     = reason,
        .node_index = context->scan,
        .record     = context->graph.nodes[context->scan < context->graph.count ? context->scan : 0].record
    };
}


TbSymbolContextStatus_e tb_symbol_context_step(TbSymbolContext_t *context, size_t max_steps)
{
    if (!context || !context->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_CONTEXT_INVALID;

    for (size_t step = 0; step < max_steps && context->status == TB_SYMBOL_CONTEXT_PENDING; ++step) {
        if (context->progress == UINT64_MAX) {
            fail(context, TB_SYMBOL_CONTEXT_INVALID, TB_SYMBOL_CONTEXT_PROGRESS);
            break;
        }

        if (context->scan >= context->graph.count) {
            if (context->validating && context->scan == context->graph.count) {
                context->validating = false;
                context->scan       = 0;
            } else if (context->scan == context->graph.count && context->count) {
                context->status = TB_SYMBOL_CONTEXT_READY;
            } else {
                fail(context, TB_SYMBOL_CONTEXT_INVALID, TB_SYMBOL_CONTEXT_GRAPH);
            }
        } else if (context->validating) {
            const TbSymbolGraphNode_t *node = &context->graph.nodes[context->scan];
            unsigned mask = (1u << (unsigned)TB_SYMBOL_GRAPH_RELATIONS) - 1u;
            if (!node->complete || (node->present & ~mask))
                fail(context, TB_SYMBOL_CONTEXT_INVALID, TB_SYMBOL_CONTEXT_GRAPH);
            else
                ++context->scan;
        } else if (!context->graph.nodes[context->scan].present) {
            context->status = TB_SYMBOL_CONTEXT_SOURCE;
        } else {
            ++context->scan;
        }
        ++context->progress;
    }

    return context->status;
}


bool tb_symbol_context_need(const TbSymbolContext_t *context, TbSymbolGraphNeed_t *need)
{
    if (!context || !context->initialized || !need || context->status != TB_SYMBOL_CONTEXT_SOURCE ||
        context->scan >= context->graph.count)
        return false;

    *need = (TbSymbolGraphNeed_t) {
        .kind   = TB_SYMBOL_GRAPH_SOURCE,
        .record = context->graph.nodes[context->scan].record
    };

    return true;
}


static uint64_t root_tag(TbSymbolDwarfUnitKind_e kind)
{
    switch (kind) {
    case TB_SYMBOL_DWARF_COMPILE:
        return 0x11;
    case TB_SYMBOL_DWARF_PARTIAL:
        return 0x3c;
    case TB_SYMBOL_DWARF_TYPE:
        return 0x41;
    default:
        return 0;
    }
}


static TbSymbolReferenceTarget_t record(TbSymbolDiesView_t tree, uint64_t index)
{
    return (TbSymbolReferenceTarget_t) {
        .backing       = tree.backing,
        .section_index = tree.section_index,
        .unit_offset   = tree.unit_offset,
        .record_offset = tree.entries[index].offset,
        .entry_index   = index
    };
}


static TbSymbolGraphContext_t parent_context(TbSymbolDiesView_t tree, const TbSymbolUnitEntry_t *unit,
                                            uint64_t index)
{
    const TbSymbolDieEntry_t *entry = &tree.entries[index];

    return (TbSymbolGraphContext_t) {
        .tag            = entry->tag,
        .version        = unit->header.version,
        .unit_kind      = tree.kind,
        .abbrev_section = tree.abbrev_section,
        .abbrev_offset  = tree.abbrev_offset,
        .parent_record  = entry->parent == UINT64_MAX ? UINT64_MAX : tree.entries[entry->parent].offset,
        .parent_tag     = entry->parent == UINT64_MAX ? 0 : tree.entries[entry->parent].tag
    };
}


TbSymbolContextStatus_e tb_symbol_context_source(TbSymbolContext_t *context,
                                                const TbSymbolUnitsView_t *units,
                                                const TbSymbolDiesView_t *tree, uint64_t unit_index)
{
    if (!context || !context->initialized || context->status != TB_SYMBOL_CONTEXT_SOURCE ||
        context->scan >= context->graph.count) {
        return TB_SYMBOL_CONTEXT_INVALID;
    }

    if (!units || !tree) {
        fail(context, TB_SYMBOL_CONTEXT_DEPENDENCY, TB_SYMBOL_CONTEXT_SOURCE_MISSING);
        return context->status;
    }

    const TbSymbolGraphNode_t *node = &context->graph.nodes[context->scan];
    if (!tb_symbol_graph_node_matches(node, *units, *tree, unit_index))
        return TB_SYMBOL_CONTEXT_INVALID;

    const TbSymbolUnitEntry_t *unit = &units->entries[unit_index];
    uint64_t root_offset;
    uint64_t tag = root_tag(tree->kind);
    if (!tag || !tb_symbol_add(unit->offset, unit->header.header_bytes, &root_offset) ||
        tree->entries[0].offset != root_offset || tree->entries[0].tag != tag ||
        tree->entries[0].parent != UINT64_MAX || context->count >= context->capacity) {
        return TB_SYMBOL_CONTEXT_INVALID;
    }

    const TbSymbolDieEntry_t *entry = &tree->entries[node->record.entry_index];
    TbSymbolContextAnchor_t candidate = {
        .node_index = context->scan,
        .record     = node->record,
        .context    = node->context,
        .kind       = TB_SYMBOL_CONTEXT_UNIT_ROOT,
        .unit_root  = record(*tree, 0),
        .unit       = *unit
    };

    if (entry->parent == UINT64_MAX) {
        if (node->record.entry_index != 0)
            return TB_SYMBOL_CONTEXT_INVALID;
    } else {
        uint64_t parent = entry->parent;
        if (tree->entries[parent].parent != UINT64_MAX && tree->entries[parent].parent >= parent)
            return TB_SYMBOL_CONTEXT_INVALID;

        candidate.kind           = parent == 0 ? TB_SYMBOL_CONTEXT_UNIT_PARENT
                                               : TB_SYMBOL_CONTEXT_LEXICAL_PARENT;
        candidate.parent         = record(*tree, parent);
        candidate.parent_context = parent_context(*tree, unit, parent);
    }

    if (context->progress == UINT64_MAX) {
        fail(context, TB_SYMBOL_CONTEXT_INVALID, TB_SYMBOL_CONTEXT_PROGRESS);
        return context->status;
    }

    context->anchors[context->count++] = candidate;
    ++context->progress;
    ++context->scan;
    context->status = TB_SYMBOL_CONTEXT_PENDING;

    return context->status;
}


bool tb_symbol_context_view(const TbSymbolContext_t *context, TbSymbolContextView_t *view)
{
    if (!context || !context->initialized || !view || context->status != TB_SYMBOL_CONTEXT_READY)
        return false;

    *view = (TbSymbolContextView_t) {
        .selected = context->graph.nodes[0].record,
        .anchors  = context->anchors,
        .count    = context->count
    };

    return true;
}


bool tb_symbol_context_failure(const TbSymbolContext_t *context, TbSymbolContextFailure_t *failure)
{
    if (!context || !context->initialized || !failure ||
        (context->status != TB_SYMBOL_CONTEXT_DEPENDENCY && context->status != TB_SYMBOL_CONTEXT_INVALID)) {
        return false;
    }

    *failure = context->failure;

    return true;
}


uint64_t tb_symbol_context_progress(const TbSymbolContext_t *context)
{
    return context && context->initialized ? context->progress : 0;
}
