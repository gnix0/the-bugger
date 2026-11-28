#include "symbol/inline_details.h"

#include <string.h>

enum {
    DETAILS_SCAN,
    DETAILS_GRAPH,
    DETAILS_COORDINATE,
    DETAILS_CONSTANT
};

static TbSymbolReferenceTarget_t record(const TbSymbolInlineDetails_t *details, uint64_t index)
{
    return (TbSymbolReferenceTarget_t) {
        .backing       = details->view.tree.backing,
        .section_index = details->view.tree.section_index,
        .unit_offset   = details->view.tree.unit_offset,
        .record_offset = details->view.tree.entries[index].offset,
        .entry_index   = index
    };
}


static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset && left.entry_index == right.entry_index;
}


static TbSymbolConstantField_e coordinate_field(unsigned index)
{
    static const TbSymbolConstantField_e fields[TB_SYMBOL_INLINE_COORDINATES] = {
        TB_SYMBOL_CONSTANT_CALL_FILE,
        TB_SYMBOL_CONSTANT_CALL_LINE,
        TB_SYMBOL_CONSTANT_CALL_COLUMN
    };

    return fields[index];
}


TbSymbolInlineDetailsStatus_e tb_symbol_inline_details_init(TbSymbolInlineDetails_t *details,
    TbSymbolUnitsView_t units, TbSymbolDiesView_t tree, uint64_t unit_index,
    TbSymbolInlineDetail_t *arena, size_t capacity)
{
    if (!details || !arena || capacity > SIZE_MAX / sizeof(*arena) || !units.entries ||
        units.count > SIZE_MAX / sizeof(*units.entries) || unit_index >= units.count ||
        !tree.entries || !tree.count || tree.count > SIZE_MAX / sizeof(*tree.entries) ||
        tree.attribute_count > SIZE_MAX / sizeof(*tree.attributes) ||
        (tree.attribute_count && !tree.attributes)) {
        return TB_SYMBOL_INLINE_DETAILS_INVALID;
    }

    const TbSymbolUnitEntry_t *unit = &units.entries[unit_index];
    uint64_t root;
    uint64_t tag = tree.kind == TB_SYMBOL_DWARF_COMPILE ? 0x11 :
                   tree.kind == TB_SYMBOL_DWARF_PARTIAL ? 0x3c :
                   tree.kind == TB_SYMBOL_DWARF_TYPE ? 0x41 : 0;
    bool compatible = unit->header.kind == tree.kind ||
        (unit->header.version == 4 && unit->header.kind == TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL &&
         (tree.kind == TB_SYMBOL_DWARF_COMPILE || tree.kind == TB_SYMBOL_DWARF_PARTIAL));
    if (!tag || !unit->header_available || !compatible ||
        (unit->header.version != 4 && unit->header.version != 5) ||
        unit->offset != tree.unit_offset || units.section_index != tree.section_index ||
        units.backing.key.device != tree.backing.key.device ||
        units.backing.key.inode != tree.backing.key.inode || units.backing.bytes != tree.backing.bytes ||
        unit->header.abbrev_offset != tree.abbrev_offset ||
        !tb_symbol_add(unit->offset, unit->header.header_bytes, &root) ||
        root >= tree.backing.bytes || tree.entries[0].offset != root ||
        tree.entries[0].parent != UINT64_MAX || tree.entries[0].tag != tag) {
        return TB_SYMBOL_INLINE_DETAILS_INVALID;
    }

    *details = (TbSymbolInlineDetails_t) {
        .view = {
            .units      = units,
            .tree       = tree,
            .unit_index = unit_index,
            .instances  = arena
        },
        .arena       = arena,
        .status      = TB_SYMBOL_INLINE_DETAILS_PENDING,
        .cursor      = 1,
        .capacity    = capacity,
        .phase       = DETAILS_SCAN,
        .initialized = true
    };

    return details->status;
}


static void fail(TbSymbolInlineDetails_t *details, TbSymbolInlineDetailsStatus_e status)
{
    details->status         = status;
    details->failure.status = status;
    if (details->cursor < details->view.tree.count) {
        details->failure.record           = record(details, details->cursor);
        details->failure.record_available = true;
    }
}


static void scan(TbSymbolInlineDetails_t *details)
{
    if (details->cursor == details->view.tree.count) {
        details->status = details->view.partial ? TB_SYMBOL_INLINE_DETAILS_PARTIAL :
                                                TB_SYMBOL_INLINE_DETAILS_READY;
        return;
    }

    const TbSymbolDieEntry_t *entry = &details->view.tree.entries[details->cursor];
    if (entry->parent >= details->cursor) {
        fail(details, TB_SYMBOL_INLINE_DETAILS_INVALID);
    } else if (entry->tag != 0x1d) {
        ++details->cursor;
    } else if (details->view.count == details->capacity) {
        fail(details, TB_SYMBOL_INLINE_DETAILS_RESOURCE);
    } else {
        details->arena[details->view.count] = (TbSymbolInlineDetail_t) {
            .record     = record(details, details->cursor),
            .parent     = record(details, entry->parent),
            .parent_tag = details->view.tree.entries[entry->parent].tag
        };
        details->status = TB_SYMBOL_INLINE_DETAILS_GRAPH;
    }
}


static void check_graph(TbSymbolInlineDetails_t *details)
{
    TbSymbolInlineDetail_t *instance = &details->arena[details->view.count];
    if (details->graph_cursor < instance->relationships.count) {
        if (!instance->relationships.nodes[details->graph_cursor++].complete)
            fail(details, TB_SYMBOL_INLINE_DETAILS_INVALID);
        return;
    }

    instance->origin_available =
        (instance->relationships.nodes[0].present & (1u << (unsigned)TB_SYMBOL_RELATION_ORIGIN)) != 0;
    if (!instance->origin_available)
        details->view.partial = true;

    details->coordinate = 0;
    details->phase      = DETAILS_COORDINATE;
}


static void inspect_constant(TbSymbolInlineDetails_t *details)
{
    TbSymbolConstantStatus_e status = tb_symbol_constant_step(&details->constant, 1);
    if (status == TB_SYMBOL_CONSTANT_PENDING)
        return;

    if (status == TB_SYMBOL_CONSTANT_SCHEMA) {
        details->status = TB_SYMBOL_INLINE_DETAILS_SCHEMA;
        return;
    }

    if (status != TB_SYMBOL_CONSTANT_FOUND && status != TB_SYMBOL_CONSTANT_ABSENT &&
        status != TB_SYMBOL_CONSTANT_UNSUPPORTED) {
        details->failure.field           = coordinate_field(details->coordinate);
        details->failure.constant_status = status;
        details->failure.selection_available =
            tb_symbol_constant_selection(&details->constant, &details->failure.selection);
        TbSymbolInlineDetailsStatus_e outcome = TB_SYMBOL_INLINE_DETAILS_INVALID;
        if (status == TB_SYMBOL_CONSTANT_CONFLICT)
            outcome = TB_SYMBOL_INLINE_DETAILS_CONFLICT;
        else if (status == TB_SYMBOL_CONSTANT_MALFORMED)
            outcome = TB_SYMBOL_INLINE_DETAILS_MALFORMED;
        else if (status == TB_SYMBOL_CONSTANT_DEPENDENCY)
            outcome = TB_SYMBOL_INLINE_DETAILS_DEPENDENCY;
        fail(details, outcome);
        return;
    }

    TbSymbolInlineCoordinate_t *coordinate =
        &details->arena[details->view.count].coordinates[details->coordinate];
    coordinate->status = status;
    if (!tb_symbol_constant_selection(&details->constant, &coordinate->selection) ||
        (status == TB_SYMBOL_CONSTANT_FOUND &&
         !tb_symbol_constant_view(&details->constant, &coordinate->constant))) {
        fail(details, TB_SYMBOL_INLINE_DETAILS_INVALID);
        return;
    }

    if (status == TB_SYMBOL_CONSTANT_UNSUPPORTED)
        details->view.partial = true;

    ++details->coordinate;
    details->phase = DETAILS_COORDINATE;
}


static void coordinate(TbSymbolInlineDetails_t *details)
{
    if (details->coordinate == TB_SYMBOL_INLINE_COORDINATES) {
        ++details->view.count;
        ++details->cursor;
        details->phase = DETAILS_SCAN;
    } else if (tb_symbol_constant_init(&details->constant, details->view.tree, details->cursor,
                                       coordinate_field(details->coordinate)) == TB_SYMBOL_CONSTANT_PENDING) {
        details->phase = DETAILS_CONSTANT;
    } else {
        fail(details, TB_SYMBOL_INLINE_DETAILS_INVALID);
    }
}


TbSymbolInlineDetailsStatus_e tb_symbol_inline_details_step(TbSymbolInlineDetails_t *details,
                                                           size_t max_steps)
{
    if (!details || !details->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_INLINE_DETAILS_INVALID;

    for (size_t step = 0; step < max_steps && details->status == TB_SYMBOL_INLINE_DETAILS_PENDING; ++step) {
        if (details->progress == UINT64_MAX) {
            fail(details, TB_SYMBOL_INLINE_DETAILS_INVALID);
            break;
        }

        ++details->progress;
        switch (details->phase) {
        case DETAILS_SCAN:
            scan(details);
            break;
        case DETAILS_GRAPH:
            check_graph(details);
            break;
        case DETAILS_COORDINATE:
            coordinate(details);
            break;
        case DETAILS_CONSTANT:
            inspect_constant(details);
            break;
        default:
            fail(details, TB_SYMBOL_INLINE_DETAILS_INVALID);
            break;
        }
    }

    return details->status;
}


bool tb_symbol_inline_details_need(const TbSymbolInlineDetails_t *details, TbSymbolReferenceTarget_t *need)
{
    if (!details || !details->initialized || !need || details->status != TB_SYMBOL_INLINE_DETAILS_GRAPH)
        return false;

    *need = record(details, details->cursor);
    return true;
}


static TbSymbolInlineDetailsStatus_e graph_failure(TbSymbolGraphStatus_e status,
                                                   const TbSymbolGraphFailure_t *failure)
{
    switch (status) {
    case TB_SYMBOL_GRAPH_CYCLE:
        return TB_SYMBOL_INLINE_DETAILS_CONFLICT;
    case TB_SYMBOL_GRAPH_DEPENDENCY:
        return TB_SYMBOL_INLINE_DETAILS_DEPENDENCY;
    case TB_SYMBOL_GRAPH_RESOURCE:
        return TB_SYMBOL_INLINE_DETAILS_RESOURCE;
    case TB_SYMBOL_GRAPH_RELATION:
        if (!failure)
            return TB_SYMBOL_INLINE_DETAILS_INVALID;
        switch (failure->relation_status) {
        case TB_SYMBOL_RELATION_UNSUPPORTED:
            return TB_SYMBOL_INLINE_DETAILS_UNSUPPORTED;
        case TB_SYMBOL_RELATION_DEPENDENCY:
            return TB_SYMBOL_INLINE_DETAILS_DEPENDENCY;
        case TB_SYMBOL_RELATION_CONFLICT:
        case TB_SYMBOL_RELATION_SELF_CYCLE:
            return TB_SYMBOL_INLINE_DETAILS_CONFLICT;
        case TB_SYMBOL_RELATION_MALFORMED:
        case TB_SYMBOL_RELATION_RANGE:
        case TB_SYMBOL_RELATION_MISSING_TARGET:
        case TB_SYMBOL_RELATION_TAG_MISMATCH:
            return TB_SYMBOL_INLINE_DETAILS_MALFORMED;
        default:
            return TB_SYMBOL_INLINE_DETAILS_INVALID;
        }
    default:
        return TB_SYMBOL_INLINE_DETAILS_INVALID;
    }
}


TbSymbolInlineDetailsStatus_e tb_symbol_inline_details_graph(TbSymbolInlineDetails_t *details,
    TbSymbolReferenceTarget_t selected, TbSymbolGraphStatus_e status, const TbSymbolGraphView_t *graph,
    const TbSymbolGraphFailure_t *failure)
{
    TbSymbolReferenceTarget_t need;
    if (!tb_symbol_inline_details_need(details, &need) || !same_record(need, selected))
        return TB_SYMBOL_INLINE_DETAILS_INVALID;

    if (status != TB_SYMBOL_GRAPH_READY) {
        TbSymbolInlineDetailsStatus_e outcome = graph_failure(status, failure);
        if (outcome == TB_SYMBOL_INLINE_DETAILS_INVALID)
            return outcome;

        details->failure.graph_status = status;
        if (failure) {
            details->failure.relation_status         = failure->relation_status;
            details->failure.graph_failure           = *failure;
            details->failure.graph_failure_available = true;
        }
        fail(details, outcome);
        return details->status;
    }

    if (!graph || !graph->nodes || !graph->count || graph->count > SIZE_MAX / sizeof(*graph->nodes) ||
        !same_record(need, graph->nodes[0].record) ||
        !tb_symbol_graph_node_matches(&graph->nodes[0], details->view.units,
                                      details->view.tree, details->view.unit_index)) {
        return TB_SYMBOL_INLINE_DETAILS_INVALID;
    }

    details->arena[details->view.count].relationships = *graph;
    details->graph_cursor = 0;
    details->phase        = DETAILS_GRAPH;
    details->status       = TB_SYMBOL_INLINE_DETAILS_PENDING;
    return details->status;
}


TbSymbolInlineDetailsStatus_e tb_symbol_inline_details_schema(TbSymbolInlineDetails_t *details,
                                                             const TbSymbolAbbrevView_t *schema)
{
    if (!details || !details->initialized || details->status != TB_SYMBOL_INLINE_DETAILS_SCHEMA)
        return TB_SYMBOL_INLINE_DETAILS_INVALID;

    TbSymbolConstantStatus_e status = tb_symbol_constant_schema(&details->constant, schema);
    if (status == TB_SYMBOL_CONSTANT_INVALID)
        return TB_SYMBOL_INLINE_DETAILS_INVALID;

    details->status = TB_SYMBOL_INLINE_DETAILS_PENDING;
    if (status == TB_SYMBOL_CONSTANT_DEPENDENCY) {
        details->failure.field           = coordinate_field(details->coordinate);
        details->failure.constant_status = status;
        details->failure.selection_available =
            tb_symbol_constant_selection(&details->constant, &details->failure.selection);
        fail(details, TB_SYMBOL_INLINE_DETAILS_DEPENDENCY);
    }

    return details->status;
}


bool tb_symbol_inline_details_view(const TbSymbolInlineDetails_t *details, TbSymbolInlineDetailsView_t *view)
{
    if (!details || !details->initialized || !view ||
        (details->status != TB_SYMBOL_INLINE_DETAILS_READY &&
         details->status != TB_SYMBOL_INLINE_DETAILS_PARTIAL)) {
        return false;
    }

    *view = details->view;
    return true;
}


bool tb_symbol_inline_details_failure(const TbSymbolInlineDetails_t *details,
                                      TbSymbolInlineDetailsFailure_t *failure)
{
    if (!details || !details->initialized || !failure || details->status < TB_SYMBOL_INLINE_DETAILS_RESOURCE)
        return false;

    *failure = details->failure;
    return true;
}


uint64_t tb_symbol_inline_details_progress(const TbSymbolInlineDetails_t *details)
{
    return details && details->initialized ? details->progress : 0;
}


void tb_symbol_inline_details_discard(TbSymbolInlineDetails_t *details)
{
    if (details)
        memset(details, 0, sizeof(*details));
}
