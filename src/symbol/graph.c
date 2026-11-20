#include "symbol/graph.h"

_Static_assert(TB_SYMBOL_RELATION_SPECIFICATION == 0 && TB_SYMBOL_RELATION_ORIGIN == 1 &&
               TB_SYMBOL_RELATION_EXTENSION == 2, "graph edge slots follow relation kind order");

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset;
}


static bool valid_record(TbSymbolReferenceTarget_t record)
{
    return record.backing.bytes && record.section_index != UINT64_MAX &&
           record.unit_offset < record.record_offset && record.record_offset < record.backing.bytes &&
           record.entry_index != UINT64_MAX;
}


TbSymbolGraphStatus_e tb_symbol_graph_init(TbSymbolGraph_t *graph, TbSymbolReferenceTarget_t root,
                                           TbSymbolGraphNode_t *nodes, size_t capacity)
{
    if (!graph || !nodes || !capacity || capacity > SIZE_MAX / sizeof(*nodes) || !valid_record(root))
        return TB_SYMBOL_GRAPH_INVALID;
    *graph = (TbSymbolGraph_t){
        .nodes       = nodes,
        .capacity    = capacity,
        .count       = 1,
        .status      = TB_SYMBOL_GRAPH_SOURCE,
        .initialized = true
    };
    nodes[0] = (TbSymbolGraphNode_t){
        .record = root,
        .parent = SIZE_MAX
    };
    return graph->status;
}


static TbSymbolGraphStatus_e fail(TbSymbolGraph_t *graph, TbSymbolRelationStatus_e status)
{
    graph->failure = (TbSymbolGraphFailure_t){
        .node_index      = graph->active,
        .relation_index  = graph->traversing ? graph->nodes[graph->active].next_edge
                                            : graph->collecting,
        .cycle_index     = SIZE_MAX,
        .relation_status = status
    };
    if (status == TB_SYMBOL_RELATION_SELF_CYCLE) {
        graph->status              = TB_SYMBOL_GRAPH_CYCLE;
        graph->failure.cycle_index = graph->active;
    } else if (status == TB_SYMBOL_RELATION_DEPENDENCY) {
        graph->status = TB_SYMBOL_GRAPH_DEPENDENCY;
    } else {
        graph->status = TB_SYMBOL_GRAPH_RELATION;
    }
    return graph->status;
}


static TbSymbolGraphContext_t target_context(const TbSymbolRelationEdge_t *edge)
{
    return (TbSymbolGraphContext_t){
        .tag            = edge->target_tag,
        .version        = edge->target_version,
        .unit_kind      = edge->target_unit_kind,
        .abbrev_section = edge->target_abbrev_section,
        .abbrev_offset  = edge->target_abbrev_offset,
        .parent_record  = edge->target_parent_record,
        .parent_tag     = edge->target_parent_tag
    };
}


static bool same_context(TbSymbolGraphContext_t left, TbSymbolGraphContext_t right)
{
    return left.tag == right.tag && left.version == right.version && left.unit_kind == right.unit_kind &&
           left.abbrev_section == right.abbrev_section && left.abbrev_offset == right.abbrev_offset &&
           left.parent_record == right.parent_record && left.parent_tag == right.parent_tag;
}


TbSymbolGraphStatus_e tb_symbol_graph_source(TbSymbolGraph_t *graph, const TbSymbolUnitsView_t *units,
                                             const TbSymbolDiesView_t *tree, uint64_t unit_index)
{
    if (!graph || !graph->initialized || graph->status != TB_SYMBOL_GRAPH_SOURCE)
        return TB_SYMBOL_GRAPH_INVALID;
    if (!units || !tree)
        return fail(graph, TB_SYMBOL_RELATION_DEPENDENCY);
    TbSymbolGraphNode_t *node = &graph->nodes[graph->active];
    TbSymbolReferenceTarget_t target;
    TbSymbolReferenceNeed_t need = {
        .backing       = node->record.backing,
        .section_index = node->record.section_index,
        .unit_offset   = node->record.unit_offset,
        .record_offset = node->record.record_offset
    };
    if (tb_symbol_reference_target(need, *tree, &target) != TB_SYMBOL_REFERENCE_FOUND ||
        target.entry_index != node->record.entry_index) {
        return TB_SYMBOL_GRAPH_INVALID;
    }
    const TbSymbolDieEntry_t *entry = &tree->entries[target.entry_index];
    if (entry->parent != UINT64_MAX && entry->parent >= target.entry_index)
        return TB_SYMBOL_GRAPH_INVALID;
    TbSymbolRelation_t relation;
    if (tb_symbol_relation_init(&relation, *units, *tree, (TbSymbolRelationSelection_t){
        .unit_index = unit_index,
        .die_index  = target.entry_index,
        .kind       = TB_SYMBOL_RELATION_SPECIFICATION
    }) != TB_SYMBOL_RELATION_PENDING) {
        return TB_SYMBOL_GRAPH_INVALID;
    }
    TbSymbolGraphContext_t context = {
        .tag            = entry->tag,
        .version        = units->entries[unit_index].header.version,
        .unit_kind      = tree->kind,
        .abbrev_section = tree->abbrev_section,
        .abbrev_offset  = tree->abbrev_offset,
        .parent_record  = entry->parent == UINT64_MAX ? UINT64_MAX : tree->entries[entry->parent].offset,
        .parent_tag     = entry->parent == UINT64_MAX ? 0 : tree->entries[entry->parent].tag
    };
    if (node->parent != SIZE_MAX && !same_context(context, node->context))
        return TB_SYMBOL_GRAPH_INVALID;
    node->context     = context;
    graph->relation   = relation;
    graph->collecting = 0;
    graph->traversing = false;
    graph->status     = TB_SYMBOL_GRAPH_PENDING;
    return graph->status;
}


TbSymbolGraphStatus_e tb_symbol_graph_target(TbSymbolGraph_t *graph, const TbSymbolUnitsView_t *info,
                                             const TbSymbolDiesView_t *tree)
{
    if (!graph || !graph->initialized || graph->status != TB_SYMBOL_GRAPH_TARGET)
        return TB_SYMBOL_GRAPH_INVALID;
    TbSymbolRelationSource_t source;
    TbSymbolRelationNeed_t need;
    TbSymbolRelationEdge_t edge;
    if (!tb_symbol_relation_source(&graph->relation, &source))
        return TB_SYMBOL_GRAPH_INVALID;
    TbSymbolRelationStatus_e status = tb_symbol_relation_need(source, info, &need);
    if (status == TB_SYMBOL_RELATION_READY)
        status = tb_symbol_relation_target(need, tree, &edge);
    if (status != TB_SYMBOL_RELATION_FOUND)
        return fail(graph, status);
    graph->nodes[graph->active].edges[graph->collecting] = edge;
    graph->nodes[graph->active].present |= 1u << graph->collecting;
    graph->status = TB_SYMBOL_GRAPH_PENDING;
    return graph->status;
}


static void next_relation(TbSymbolGraph_t *graph)
{
    ++graph->collecting;
    if (graph->collecting == TB_SYMBOL_GRAPH_RELATIONS) {
        graph->traversing = true;
        return;
    }
    TbSymbolRelationSelection_t selection = graph->relation.selection;
    selection.kind = (TbSymbolRelationKind_e)graph->collecting;
    if (tb_symbol_relation_init(&graph->relation, graph->relation.units, graph->relation.tree, selection) !=
        TB_SYMBOL_RELATION_PENDING) {
        (void)fail(graph, TB_SYMBOL_RELATION_INVALID);
    }
}


static void collect(TbSymbolGraph_t *graph)
{
    /* A supplied target leaves its completed source query in place. */
    if (graph->nodes[graph->active].present & (1u << graph->collecting)) {
        next_relation(graph);
        return;
    }
    TbSymbolRelationStatus_e status = tb_symbol_relation_step(&graph->relation, 1);
    if (status == TB_SYMBOL_RELATION_ABSENT) {
        next_relation(graph);
    } else if (status == TB_SYMBOL_RELATION_READY) {
        graph->status = TB_SYMBOL_GRAPH_TARGET;
    } else if (status != TB_SYMBOL_RELATION_PENDING) {
        (void)fail(graph, status);
    }
}


static void cycle(TbSymbolGraph_t *graph, size_t target)
{
    graph->failure = (TbSymbolGraphFailure_t){
        .node_index      = graph->active,
        .relation_index  = graph->nodes[graph->active].next_edge,
        .cycle_index     = target,
        .relation_status = TB_SYMBOL_RELATION_FOUND
    };
    graph->status = TB_SYMBOL_GRAPH_CYCLE;
}


static void traverse(TbSymbolGraph_t *graph)
{
    TbSymbolGraphNode_t *node = &graph->nodes[graph->active];
    if (node->next_edge == TB_SYMBOL_GRAPH_RELATIONS) {
        node->complete = true;
        if (node->parent == SIZE_MAX) {
            graph->status = TB_SYMBOL_GRAPH_READY;
        } else {
            graph->active = node->parent;
            ++graph->nodes[graph->active].next_edge;
        }
        return;
    }
    if (!(node->present & (1u << node->next_edge))) {
        ++node->next_edge;
        return;
    }
    const TbSymbolReferenceTarget_t target = node->edges[node->next_edge].target;
    if (!graph->scanning) {
        graph->scan     = 0;
        graph->scanning = true;
    }
    if (graph->scan < graph->count) {
        size_t index = graph->scan++;
        if (same_record(graph->nodes[index].record, target)) {
            graph->scanning = false;
            if (graph->nodes[index].record.entry_index != target.entry_index ||
                !same_context(graph->nodes[index].context, target_context(&node->edges[node->next_edge]))) {
                (void)fail(graph, TB_SYMBOL_RELATION_INVALID);
            } else if (!graph->nodes[index].complete) {
                cycle(graph, index);
            } else {
                ++node->next_edge;
            }
        }
        return;
    }
    graph->scanning = false;
    if (graph->count == graph->capacity) {
        graph->failure = (TbSymbolGraphFailure_t){
            .node_index      = graph->active,
            .relation_index  = node->next_edge,
            .cycle_index     = SIZE_MAX,
            .relation_status = TB_SYMBOL_RELATION_FOUND
        };
        graph->status = TB_SYMBOL_GRAPH_RESOURCE;
        return;
    }
    size_t index = graph->count++;
    graph->nodes[index] = (TbSymbolGraphNode_t){
        .record  = target,
        .context = target_context(&node->edges[node->next_edge]),
        .parent  = graph->active
    };
    graph->active = index;
    graph->status = TB_SYMBOL_GRAPH_SOURCE;
}


TbSymbolGraphStatus_e tb_symbol_graph_step(TbSymbolGraph_t *graph, size_t max_steps)
{
    if (!graph || !graph->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_GRAPH_INVALID;
    for (size_t i = 0; i < max_steps && graph->status == TB_SYMBOL_GRAPH_PENDING; ++i) {
        if (graph->progress != UINT64_MAX)
            ++graph->progress;
        if (graph->traversing)
            traverse(graph);
        else
            collect(graph);
    }
    return graph->status;
}


bool tb_symbol_graph_need(const TbSymbolGraph_t *graph, TbSymbolGraphNeed_t *need)
{
    if (!graph || !graph->initialized || !need ||
        (graph->status != TB_SYMBOL_GRAPH_SOURCE && graph->status != TB_SYMBOL_GRAPH_TARGET)) {
        return false;
    }
    TbSymbolGraphNeed_t candidate = {
        .kind   = graph->status,
        .record = graph->nodes[graph->active].record
    };
    if (graph->status == TB_SYMBOL_GRAPH_TARGET &&
        !tb_symbol_relation_source(&graph->relation, &candidate.source)) {
        return false;
    }
    *need = candidate;
    return true;
}


bool tb_symbol_graph_view(const TbSymbolGraph_t *graph, TbSymbolGraphView_t *view)
{
    if (!graph || !graph->initialized || !view || graph->status != TB_SYMBOL_GRAPH_READY)
        return false;
    *view = (TbSymbolGraphView_t){
        .nodes = graph->nodes,
        .count = graph->count
    };
    return true;
}


bool tb_symbol_graph_failure(const TbSymbolGraph_t *graph, TbSymbolGraphFailure_t *failure)
{
    if (!graph || !graph->initialized || !failure ||
        (graph->status != TB_SYMBOL_GRAPH_CYCLE && graph->status != TB_SYMBOL_GRAPH_DEPENDENCY &&
         graph->status != TB_SYMBOL_GRAPH_RELATION && graph->status != TB_SYMBOL_GRAPH_RESOURCE)) {
        return false;
    }
    TbSymbolGraphFailure_t candidate = graph->failure;
    const TbSymbolGraphNode_t *node = &graph->nodes[candidate.node_index];
    candidate.record = node->record;
    if (candidate.relation_index < TB_SYMBOL_GRAPH_RELATIONS &&
        (node->present & (1u << candidate.relation_index))) {
        candidate.source = node->edges[candidate.relation_index].source;
        candidate.source_available = true;
        candidate.selection = candidate.source.attribute;
        candidate.selection_available = true;
        candidate.reference_status = TB_SYMBOL_REFERENCE_READY;
    } else if (!graph->traversing) {
        candidate.source_available = tb_symbol_relation_source(&graph->relation, &candidate.source);
        candidate.selection_available = tb_symbol_relation_diagnostic(&graph->relation,
                                                                       &candidate.selection,
                                                                       &candidate.reference_status);
    }
    *failure = candidate;
    return true;
}


uint64_t tb_symbol_graph_progress(const TbSymbolGraph_t *graph)
{
    return graph && graph->initialized ? graph->progress : 0;
}
