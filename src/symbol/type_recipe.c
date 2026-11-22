#include "symbol/type_recipe.h"

typedef enum {
    TYPE_ADOPT,
    TYPE_COMPARE,
    TYPE_CYCLE
} TypePhase_e;

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset;
}


static bool same_target(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return same_record(left, right) && left.entry_index == right.entry_index;
}


static bool same_context(TbSymbolGraphContext_t left, TbSymbolGraphContext_t right)
{
    return left.tag == right.tag && left.version == right.version && left.unit_kind == right.unit_kind &&
           left.abbrev_section == right.abbrev_section && left.abbrev_offset == right.abbrev_offset &&
           left.parent_record == right.parent_record && left.parent_tag == right.parent_tag;
}


static bool valid_graph(TbSymbolGraphView_t graph)
{
    return graph.nodes && graph.count && graph.count <= SIZE_MAX / sizeof(*graph.nodes) &&
           graph.nodes[0].complete && (graph.nodes[0].context.version == 4 ||
                                      graph.nodes[0].context.version == 5);
}


TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_init(TbSymbolTypeRecipe_t *recipe,
                                                       TbSymbolGraphView_t selected,
                                                       TbSymbolTypeRecipeNode_t *nodes, size_t capacity)
{
    if (!recipe || !valid_graph(selected) || !nodes || capacity > SIZE_MAX / sizeof(*nodes))
        return TB_SYMBOL_TYPE_INVALID;

    if (!capacity)
        return TB_SYMBOL_TYPE_RESOURCE;

    *recipe = (TbSymbolTypeRecipe_t) {
        .pending_graph = selected,
        .next          = selected.nodes[0].record,
        .nodes         = nodes,
        .status        = TB_SYMBOL_TYPE_PENDING,
        .capacity      = capacity,
        .phase         = TYPE_ADOPT,
        .initialized   = true
    };

    return recipe->status;
}


static TbSymbolTypeRecipeKind_e kind(uint64_t tag)
{
    switch (tag) {
    case 0x24:
        return TB_SYMBOL_TYPE_BASE;
    case 0x16:
        return TB_SYMBOL_TYPE_ALIAS;
    case 0x02:
    case 0x13:
    case 0x17:
        return TB_SYMBOL_TYPE_RECORD;
    case 0x04:
        return TB_SYMBOL_TYPE_ENUM;
    case 0x0f:
        return TB_SYMBOL_TYPE_POINTER;
    case 0x10:
        return TB_SYMBOL_TYPE_LVALUE_REFERENCE;
    case 0x42:
        return TB_SYMBOL_TYPE_RVALUE_REFERENCE;
    case 0x26:
        return TB_SYMBOL_TYPE_CONST;
    case 0x35:
        return TB_SYMBOL_TYPE_VOLATILE;
    case 0x37:
        return TB_SYMBOL_TYPE_RESTRICT;
    default:
        return TB_SYMBOL_TYPE_UNKNOWN;
    }
}


static bool non_type(TbSymbolGraphContext_t context)
{
    /* Later standard tag assignments do not classify a version-4 extension. */
    if (context.version == 4 && context.tag >= 0x44)
        return false;

    switch (context.tag) {
    case 0x03:
    case 0x05:
    case 0x08:
    case 0x0a:
    case 0x0b:
    case 0x0d:
    case 0x11:
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x1d:
    case 0x1e:
    case 0x22:
    case 0x23:
    case 0x25:
    case 0x27:
    case 0x28:
    case 0x2a:
    case 0x2b:
    case 0x2c:
    case 0x2e:
    case 0x30:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x36:
    case 0x39:
    case 0x3a:
    case 0x3c:
    case 0x3d:
    case 0x41:
    case 0x48:
    case 0x49:
    case 0x4a:
        return true;
    default:
        return false;
    }
}


static TbSymbolTypeRecipeStatus_e fail(TbSymbolTypeRecipe_t *recipe, TbSymbolTypeRecipeStatus_e status)
{
    recipe->status  = status;
    recipe->failure = (TbSymbolTypeRecipeFailure_t) {
        .status           = status,
        .record           = recipe->count ? recipe->nodes[recipe->count - 1].record : recipe->next,
        .node_index       = recipe->count ? recipe->count - 1 : 0,
        .other_node_index = SIZE_MAX
    };

    return status;
}


static void partial(TbSymbolTypeRecipe_t *recipe, TbSymbolTypeRecipePartial_e reason)
{
    recipe->partial = reason;
    recipe->status  = TB_SYMBOL_TYPE_PARTIAL;
}


static void adopt(TbSymbolTypeRecipe_t *recipe)
{
    if (non_type(recipe->pending_graph.nodes[0].context)) {
        (void)fail(recipe, TB_SYMBOL_TYPE_MALFORMED);
        recipe->failure.record = recipe->next;
        recipe->failure.node_index = recipe->count;

        return;
    }

    if (recipe->count == recipe->capacity) {
        (void)fail(recipe, TB_SYMBOL_TYPE_RESOURCE);
        return;
    }

    TbSymbolTypeRecipeKind_e type = kind(recipe->pending_graph.nodes[0].context.tag);

    recipe->nodes[recipe->count++] = (TbSymbolTypeRecipeNode_t) {
        .record = recipe->next,
        .graph  = recipe->pending_graph,
        .kind   = type
    };

    if (type == TB_SYMBOL_TYPE_UNKNOWN)
        partial(recipe, TB_SYMBOL_TYPE_UNSUPPORTED_CONSTRUCTOR);
    else
        recipe->status = type <= TB_SYMBOL_TYPE_ENUM ? TB_SYMBOL_TYPE_NAME : TB_SYMBOL_TYPE_REFERENCES;
}


static void compare(TbSymbolTypeRecipe_t *recipe)
{
    TbSymbolInheritedReferenceView_t refs = recipe->nodes[recipe->count - 1].references;
    if (recipe->checking == refs.count) {
        if (recipe->alternatives) {
            partial(recipe, TB_SYMBOL_TYPE_ALTERNATIVE_TARGETS);
        } else {
            recipe->checking = 0;
            recipe->phase    = TYPE_CYCLE;
        }
        return;
    }

    TbSymbolReferenceTarget_t target = refs.facts[recipe->checking++].target;
    if (same_record(target, recipe->next) && target.entry_index != recipe->next.entry_index) {
        (void)fail(recipe, TB_SYMBOL_TYPE_INVALID);
        return;
    }

    if (!same_record(target, recipe->next))
        recipe->alternatives = true;
}


static void cycle(TbSymbolTypeRecipe_t *recipe)
{
    if (recipe->checking == recipe->count) {
        if (recipe->count == recipe->capacity)
            (void)fail(recipe, TB_SYMBOL_TYPE_RESOURCE);
        else
            recipe->status = TB_SYMBOL_TYPE_GRAPH;
        return;
    }

    size_t index = recipe->checking++;
    if (same_record(recipe->nodes[index].record, recipe->next)) {
        (void)fail(recipe, TB_SYMBOL_TYPE_CONFLICT);
        recipe->failure.other_node_index = index;
    }
}


TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_step(TbSymbolTypeRecipe_t *recipe, size_t max_steps)
{
    if (!recipe || !recipe->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_TYPE_INVALID;

    for (size_t step = 0; step < max_steps && recipe->status == TB_SYMBOL_TYPE_PENDING; ++step) {
        if (recipe->progress == UINT64_MAX)
            return fail(recipe, TB_SYMBOL_TYPE_INVALID);

        ++recipe->progress;

        if (recipe->phase == TYPE_ADOPT)
            adopt(recipe);
        else if (recipe->phase == TYPE_COMPARE)
            compare(recipe);
        else if (recipe->phase == TYPE_CYCLE)
            cycle(recipe);
        else
            return fail(recipe, TB_SYMBOL_TYPE_INVALID);
    }

    return recipe->status;
}


bool tb_symbol_type_recipe_need(const TbSymbolTypeRecipe_t *recipe, TbSymbolReferenceTarget_t *record)
{
    if (!recipe || !recipe->initialized || !record)
        return false;

    if (recipe->status == TB_SYMBOL_TYPE_GRAPH)
        *record = recipe->next;
    else if (recipe->status == TB_SYMBOL_TYPE_NAME || recipe->status == TB_SYMBOL_TYPE_REFERENCES)
        *record = recipe->nodes[recipe->count - 1].record;
    else
        return false;

    return     true;
}


TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_graph(TbSymbolTypeRecipe_t *recipe,
                                                       const TbSymbolGraphView_t *graph)
{
    if (!recipe || !recipe->initialized || recipe->status != TB_SYMBOL_TYPE_GRAPH)
        return TB_SYMBOL_TYPE_INVALID;

    if (!graph)
        return fail(recipe, TB_SYMBOL_TYPE_DEPENDENCY);

    if (!valid_graph(*graph) || !same_target(graph->nodes[0].record, recipe->next))
        return TB_SYMBOL_TYPE_INVALID;

    recipe->pending_graph = *graph;
    recipe->phase         = TYPE_ADOPT;
    recipe->status        = TB_SYMBOL_TYPE_PENDING;

    return recipe->status;
}


TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_name(TbSymbolTypeRecipe_t *recipe,
                                                      TbSymbolReferenceTarget_t record,
                                                      TbSymbolQualifiedStatus_e status,
                                                      const TbSymbolQualifiedView_t *name)
{
    if (!recipe || !recipe->initialized || recipe->status != TB_SYMBOL_TYPE_NAME ||
        !same_target(record, recipe->nodes[recipe->count - 1].record))
        return TB_SYMBOL_TYPE_INVALID;

    if (status == TB_SYMBOL_QUALIFIED_READY) {
        if (!name || !name->nodes || !name->count || !same_target(name->selected, record) ||
            name->count > SIZE_MAX / sizeof(*name->nodes) || !name->nodes[0].complete ||
            !name->scope.nodes || name->scope.node_count != name->count || !name->scope.nodes[0].complete ||
            !same_target(name->scope.nodes[0].record, record) ||
            !valid_graph(name->scope.nodes[0].relationships) ||
            !same_target(name->scope.nodes[0].relationships.nodes[0].record, record) ||
            !same_context(name->scope.nodes[0].context,
                          recipe->nodes[recipe->count - 1].graph.nodes[0].context) ||
            !same_context(name->scope.nodes[0].relationships.nodes[0].context,
                          recipe->nodes[recipe->count - 1].graph.nodes[0].context) ||
            name->scope.nodes[0].relationships.count != recipe->nodes[recipe->count - 1].graph.count) {
            return TB_SYMBOL_TYPE_INVALID;
        }

        recipe->nodes[recipe->count - 1].name = *name;
        recipe->nodes[recipe->count - 1].name_available = true;
        recipe->status = TB_SYMBOL_TYPE_READY;

        return recipe->status;
    }

    TbSymbolTypeRecipeStatus_e outcome;

    switch (status) {
    case TB_SYMBOL_QUALIFIED_UNNAMED:
    case TB_SYMBOL_QUALIFIED_UNSUPPORTED:
        outcome = TB_SYMBOL_TYPE_UNSUPPORTED;
        break;
    case TB_SYMBOL_QUALIFIED_CONFLICT:
        outcome = TB_SYMBOL_TYPE_CONFLICT;
        break;
    case TB_SYMBOL_QUALIFIED_DEPENDENCY:
        outcome = TB_SYMBOL_TYPE_DEPENDENCY;
        break;
    case TB_SYMBOL_QUALIFIED_MALFORMED:
        outcome = TB_SYMBOL_TYPE_MALFORMED;
        break;
    case TB_SYMBOL_QUALIFIED_RESOURCE:
        outcome = TB_SYMBOL_TYPE_RESOURCE;
        break;
    case TB_SYMBOL_QUALIFIED_OPERATIONAL:
        outcome = TB_SYMBOL_TYPE_OPERATIONAL;
        break;
    default:
        return TB_SYMBOL_TYPE_INVALID;
    }

    (void)fail(recipe, outcome);
    recipe->failure.name_status = status;

    return recipe->status;
}


TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_references(TbSymbolTypeRecipe_t *recipe,
                                                            TbSymbolReferenceTarget_t record,
                                                            TbSymbolInheritedReferenceStatus_e status,
                                                            const TbSymbolInheritedReferenceView_t *refs)
{
    if (!recipe || !recipe->initialized || recipe->status != TB_SYMBOL_TYPE_REFERENCES ||
        !same_target(record, recipe->nodes[recipe->count - 1].record))
        return TB_SYMBOL_TYPE_INVALID;

    if (status == TB_SYMBOL_INHERITED_REFERENCE_FOUND || status == TB_SYMBOL_INHERITED_REFERENCE_ABSENT) {
        if (!refs || !same_target(refs->selected, record) || refs->name != TB_SYMBOL_INHERIT_TYPE ||
            !valid_graph(refs->graph) || !same_target(refs->graph.nodes[0].record, record) || !refs->slots ||
            !same_context(refs->graph.nodes[0].context,
                          recipe->nodes[recipe->count - 1].graph.nodes[0].context) ||
            refs->graph.count != recipe->nodes[recipe->count - 1].graph.count ||
            refs->count > refs->graph.count || refs->count > SIZE_MAX / sizeof(*refs->facts) ||
            (refs->count && !refs->facts) ||
            (status == TB_SYMBOL_INHERITED_REFERENCE_FOUND) != (refs->count != 0)) {
            return TB_SYMBOL_TYPE_INVALID;
        }

        recipe->nodes[recipe->count - 1].references = *refs;
        recipe->nodes[recipe->count - 1].references_available = true;

        if (!refs->count) {
            partial(recipe, TB_SYMBOL_TYPE_MISSING_TYPE);
        } else {
            recipe->next         = refs->facts[0].target;
            recipe->checking     = 0;
            recipe->alternatives = false;
            recipe->phase        = TYPE_COMPARE;
            recipe->status       = TB_SYMBOL_TYPE_PENDING;
        }

        return recipe->status;
    }

    TbSymbolTypeRecipeStatus_e outcome;

    switch (status) {
    case TB_SYMBOL_INHERITED_REFERENCE_CONFLICT:
        outcome = TB_SYMBOL_TYPE_CONFLICT;
        break;
    case TB_SYMBOL_INHERITED_REFERENCE_DEPENDENCY:
        outcome = TB_SYMBOL_TYPE_DEPENDENCY;
        break;
    case TB_SYMBOL_INHERITED_REFERENCE_UNSUPPORTED:
        outcome = TB_SYMBOL_TYPE_UNSUPPORTED;
        break;
    case TB_SYMBOL_INHERITED_REFERENCE_MALFORMED:
        outcome = TB_SYMBOL_TYPE_MALFORMED;
        break;
    case TB_SYMBOL_INHERITED_REFERENCE_RESOURCE:
        outcome = TB_SYMBOL_TYPE_RESOURCE;
        break;
    default:
        return TB_SYMBOL_TYPE_INVALID;
    }

    (void)fail(recipe, outcome);
    recipe->failure.reference_status = status;

    return recipe->status;
}


bool tb_symbol_type_recipe_view(const TbSymbolTypeRecipe_t *recipe, TbSymbolTypeRecipeView_t *view)
{
    if (!recipe || !recipe->initialized || !view ||
        (recipe->status != TB_SYMBOL_TYPE_READY && recipe->status != TB_SYMBOL_TYPE_PARTIAL))
        return false;

    *view = (TbSymbolTypeRecipeView_t) {
        .selected = recipe->nodes[0].record,
        .nodes    = recipe->nodes,
        .count    = recipe->count,
        .partial  = recipe->partial,
        .complete = recipe->status == TB_SYMBOL_TYPE_READY
    };

    return true;
}


bool tb_symbol_type_recipe_failure(const TbSymbolTypeRecipe_t *recipe, TbSymbolTypeRecipeFailure_t *failure)
{
    if (!recipe || !recipe->initialized || !failure)
        return false;

    switch (recipe->status) {
    case TB_SYMBOL_TYPE_UNSUPPORTED:
    case TB_SYMBOL_TYPE_CONFLICT:
    case TB_SYMBOL_TYPE_DEPENDENCY:
    case TB_SYMBOL_TYPE_MALFORMED:
    case TB_SYMBOL_TYPE_RESOURCE:
    case TB_SYMBOL_TYPE_OPERATIONAL:
    case TB_SYMBOL_TYPE_INVALID:
        *failure = recipe->failure;
        return true;
    default:
        return false;
    }
}


uint64_t tb_symbol_type_recipe_progress(const TbSymbolTypeRecipe_t *recipe)
{
    return recipe && recipe->initialized ? recipe->progress : 0;
}


void tb_symbol_type_recipe_discard(TbSymbolTypeRecipe_t *recipe)
{
    if (recipe)
        *recipe = (TbSymbolTypeRecipe_t) {0};
}
