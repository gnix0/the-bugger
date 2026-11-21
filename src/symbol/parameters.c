#include "symbol/parameters.h"

static TbSymbolReferenceTarget_t record(const TbSymbolParameters_t *parameters, uint64_t index)
{
    return (TbSymbolReferenceTarget_t){
        .backing       = parameters->tree.backing,
        .section_index = parameters->tree.section_index,
        .unit_offset   = parameters->tree.unit_offset,
        .record_offset = parameters->tree.entries[index].offset,
        .entry_index   = index
    };
}


TbSymbolParametersStatus_e tb_symbol_parameters_init(TbSymbolParameters_t *parameters,
                                                     TbSymbolDiesView_t tree, uint64_t selected,
                                                     TbSymbolParameter_t *arena, size_t capacity)
{
    if (!parameters || !tree.entries || selected >= tree.count || !arena ||
        tree.count > SIZE_MAX / sizeof(*tree.entries) ||
        tree.attribute_count > SIZE_MAX / sizeof(*tree.attributes) ||
        (tree.attribute_count && !tree.attributes) || capacity > SIZE_MAX / sizeof(*arena)) {
        return TB_SYMBOL_PARAMETERS_INVALID;
    }
    uint64_t tag = tree.entries[selected].tag;
    if (tag != 0x2e && tag != 0x15 && tag != 0x1d)
        return TB_SYMBOL_PARAMETERS_INVALID;
    *parameters = (TbSymbolParameters_t){
        .tree        = tree,
        .parameters  = arena,
        .status      = TB_SYMBOL_PARAMETERS_PENDING,
        .selected    = selected,
        .cursor      = selected + 1,
        .capacity    = capacity,
        .initialized = true
    };
    return parameters->status;
}


static void fail(TbSymbolParameters_t *parameters, TbSymbolParametersStatus_e status,
                 TbSymbolParametersFailure_e failure)
{
    parameters->status  = status;
    parameters->failure = failure;
}


static void inspect(TbSymbolParameters_t *parameters)
{
    TbSymbolEntityStatus_e status = tb_symbol_entity_step(&parameters->entity, 1);
    if (status == TB_SYMBOL_ENTITY_PENDING)
        return;
    if (status != TB_SYMBOL_ENTITY_READY) {
        TbSymbolParametersStatus_e outcome = TB_SYMBOL_PARAMETERS_INVALID;
        if (status == TB_SYMBOL_ENTITY_CONFLICT)
            outcome = TB_SYMBOL_PARAMETERS_CONFLICT;
        else if (status == TB_SYMBOL_ENTITY_MALFORMED)
            outcome = TB_SYMBOL_PARAMETERS_MALFORMED;
        fail(parameters, outcome, TB_SYMBOL_PARAMETERS_ENTITY);
        return;
    }
    TbSymbolEntityView_t view;
    if (!tb_symbol_entity_view(&parameters->entity, &view)) {
        fail(parameters, TB_SYMBOL_PARAMETERS_INVALID, TB_SYMBOL_PARAMETERS_ENTITY);
        return;
    }
    parameters->parameters[parameters->count++] = (TbSymbolParameter_t){
        .record      = record(parameters, parameters->cursor),
        .direct      = view,
        .unspecified = view.tag == 0x18
    };
    ++parameters->cursor;
    parameters->inspecting = false;
}


static void advance(TbSymbolParameters_t *parameters)
{
    if (parameters->cursor == parameters->tree.count) {
        parameters->status = TB_SYMBOL_PARAMETERS_READY;
        return;
    }
    const TbSymbolDieEntry_t *entry = &parameters->tree.entries[parameters->cursor];
    if (entry->parent >= parameters->cursor) {
        fail(parameters, TB_SYMBOL_PARAMETERS_INVALID, TB_SYMBOL_PARAMETERS_SCOPE);
    } else if (entry->parent < parameters->selected) {
        parameters->status = TB_SYMBOL_PARAMETERS_READY;
    } else if (entry->parent != parameters->selected || (entry->tag != 0x05 && entry->tag != 0x18)) {
        ++parameters->cursor;
    } else if (parameters->count == parameters->capacity) {
        fail(parameters, TB_SYMBOL_PARAMETERS_RESOURCE, TB_SYMBOL_PARAMETERS_CAPACITY);
    } else {
        parameters->inspecting =
            tb_symbol_entity_init(&parameters->entity, parameters->tree, parameters->cursor) ==
            TB_SYMBOL_ENTITY_PENDING;
        if (!parameters->inspecting)
            fail(parameters, TB_SYMBOL_PARAMETERS_INVALID, TB_SYMBOL_PARAMETERS_ENTITY);
    }
}


TbSymbolParametersStatus_e tb_symbol_parameters_step(TbSymbolParameters_t *parameters, size_t max_steps)
{
    if (!parameters || !parameters->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_PARAMETERS_INVALID;
    if (parameters->status != TB_SYMBOL_PARAMETERS_PENDING)
        return parameters->status;
    for (size_t step = 0; step < max_steps && parameters->status == TB_SYMBOL_PARAMETERS_PENDING; ++step) {
        if (parameters->progress == UINT64_MAX) {
            fail(parameters, TB_SYMBOL_PARAMETERS_INVALID, TB_SYMBOL_PARAMETERS_PROGRESS);
            break;
        }
        if (parameters->inspecting)
            inspect(parameters);
        else
            advance(parameters);
        ++parameters->progress;
    }
    return parameters->status;
}


bool tb_symbol_parameters_view(const TbSymbolParameters_t *parameters, TbSymbolParametersView_t *view)
{
    if (!parameters || !parameters->initialized || !view || parameters->status != TB_SYMBOL_PARAMETERS_READY)
        return false;
    *view = (TbSymbolParametersView_t){
        .selected   = record(parameters, parameters->selected),
        .parameters = parameters->parameters,
        .count      = parameters->count
    };
    return true;
}


bool tb_symbol_parameters_failure(const TbSymbolParameters_t *parameters,
                                   TbSymbolParametersFailure_t *failure)
{
    if (!parameters || !parameters->initialized || !failure ||
        parameters->status == TB_SYMBOL_PARAMETERS_PENDING ||
        parameters->status == TB_SYMBOL_PARAMETERS_READY) {
        return false;
    }
    *failure = (TbSymbolParametersFailure_t){
        .reason      = parameters->failure,
        .selected    = record(parameters, parameters->selected),
        .entry_index = parameters->cursor
    };
    if (parameters->cursor < parameters->tree.count) {
        failure->record           = record(parameters, parameters->cursor);
        failure->record_available = true;
    }
    if (parameters->failure == TB_SYMBOL_PARAMETERS_ENTITY) {
        failure->selection_available = tb_symbol_entity_failure(&parameters->entity, &failure->field,
                                                                &failure->selection);
    }
    return true;
}


uint64_t tb_symbol_parameters_progress(const TbSymbolParameters_t *parameters)
{
    return parameters && parameters->initialized ? parameters->progress : 0;
}
