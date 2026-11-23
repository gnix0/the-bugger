#include "symbol/source_context.h"

static bool candidate_valid(TbSymbolSourceCandidate_t candidate)
{
    const TbSymbolSourcePathView_t *path = &candidate.path;
    if (!path->recorded || !path->recorded_bytes)
        return false;
    if (candidate.status == TB_SYMBOL_SOURCE_PATH_COMPLETE)
        return path->local_available && path->local && path->local_bytes;

    return !path->local_available &&
           (candidate.status == TB_SYMBOL_SOURCE_PATH_DEPENDENCY ||
            candidate.status == TB_SYMBOL_SOURCE_PATH_AMBIGUOUS);
}


static bool root_matches(TbSymbolSourceFileView_t file, TbSymbolTextView_t directory)
{
    const TbSymbolAttributeResult_t *root = &file.reference.source;
    return directory.backing.key.device == root->backing.key.device &&
           directory.backing.key.inode == root->backing.key.inode &&
           directory.backing.bytes == root->backing.bytes &&
           directory.source_section == root->section_index && directory.source_unit == root->unit_offset &&
           directory.source_record == root->record_offset && directory.attribute.name == 0x1b;
}


static TbSymbolSourceContextStatus_e candidate_status(TbSymbolSourceCandidate_t candidate)
{
    if (candidate.status == TB_SYMBOL_SOURCE_PATH_COMPLETE)
        return TB_SYMBOL_SOURCE_CONTEXT_READY;

    return candidate.status == TB_SYMBOL_SOURCE_PATH_AMBIGUOUS ?
           TB_SYMBOL_SOURCE_CONTEXT_AMBIGUOUS : TB_SYMBOL_SOURCE_CONTEXT_DEPENDENCY;
}


TbSymbolSourceContextStatus_e tb_symbol_source_context_init(TbSymbolSourceContext_t *context,
    TbSymbolSourceFileView_t file, TbSymbolSourceCandidate_t primary,
    const TbSymbolSourceCandidate_t *alternate, const TbSymbolTextView_t *unit_directory, size_t choice)
{
    if (!context || file.line_version < 2 || file.line_version > 5 ||
        file.path.status != TB_SYMBOL_SOURCE_TEXT_READY || !candidate_valid(primary) ||
        (alternate && (!unit_directory || file.line_version != 5 ||
                       !candidate_valid(*alternate))) ||
        (unit_directory && !root_matches(file, *unit_directory)) ||
        (choice != SIZE_MAX && choice >= (alternate ? 2U : 1U))) {
        return TB_SYMBOL_SOURCE_CONTEXT_INVALID;
    }

    *context = (TbSymbolSourceContext_t) {
        .view = {
            .file                     = file,
            .candidates               = {primary},
            .count                    = alternate ? 2 : 1,
            .selected                 = choice,
            .unit_directory_available = unit_directory != NULL
        },
        .status      = TB_SYMBOL_SOURCE_CONTEXT_PENDING,
        .initialized = true
    };
    if (unit_directory)
        context->view.unit_directory = *unit_directory;
    if (alternate)
        context->view.candidates[1] = *alternate;

    return context->status;
}


TbSymbolSourceContextStatus_e tb_symbol_source_context_step(TbSymbolSourceContext_t *context,
    size_t max_steps)
{
    if (!context || !context->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_SOURCE_CONTEXT_INVALID;
    if (context->status != TB_SYMBOL_SOURCE_CONTEXT_PENDING)
        return context->status;

    TbSymbolSourceContextView_t *view = &context->view;
    if (view->selected != SIZE_MAX || view->count == 1) {
        size_t selected = view->selected == SIZE_MAX ? 0 : view->selected;
        context->status = candidate_status(view->candidates[selected]);
        view->selected = context->status == TB_SYMBOL_SOURCE_CONTEXT_READY ? selected : SIZE_MAX;
        return context->status;
    }

    if (view->candidates[0].status != TB_SYMBOL_SOURCE_PATH_COMPLETE ||
        view->candidates[1].status != TB_SYMBOL_SOURCE_PATH_COMPLETE) {
        context->status = (view->candidates[0].status == TB_SYMBOL_SOURCE_PATH_AMBIGUOUS ||
                           view->candidates[1].status == TB_SYMBOL_SOURCE_PATH_AMBIGUOUS) ?
                          TB_SYMBOL_SOURCE_CONTEXT_AMBIGUOUS : TB_SYMBOL_SOURCE_CONTEXT_DEPENDENCY;
        return context->status;
    }

    const TbSymbolSourcePathView_t *left = &view->candidates[0].path;
    const TbSymbolSourcePathView_t *right = &view->candidates[1].path;
    for (size_t step = 0; step < max_steps; ++step) {
        size_t left_bytes = context->phase ? left->local_bytes : left->recorded_bytes;
        size_t right_bytes = context->phase ? right->local_bytes : right->recorded_bytes;
        const unsigned char *left_data = context->phase ? left->local : left->recorded;
        const unsigned char *right_data = context->phase ? right->local : right->recorded;
        if (left_bytes != right_bytes ||
            (context->cursor < left_bytes && left_data[context->cursor] != right_data[context->cursor])) {
            context->status = TB_SYMBOL_SOURCE_CONTEXT_AMBIGUOUS;
            break;
        }
        if (context->cursor < left_bytes) {
            ++context->cursor;
        } else if (!context->phase) {
            context->phase = 1;
            context->cursor = 0;
        } else {
            view->selected = 0;
            context->status = TB_SYMBOL_SOURCE_CONTEXT_READY;
            break;
        }
    }

    return context->status;
}


bool tb_symbol_source_context_view(const TbSymbolSourceContext_t *context, TbSymbolSourceContextView_t *view)
{
    if (!context || !context->initialized || !view ||
        (context->status != TB_SYMBOL_SOURCE_CONTEXT_READY &&
         context->status != TB_SYMBOL_SOURCE_CONTEXT_AMBIGUOUS &&
         context->status != TB_SYMBOL_SOURCE_CONTEXT_DEPENDENCY)) {
        return false;
    }

    *view = context->view;

    return true;
}


void tb_symbol_source_context_discard(TbSymbolSourceContext_t *context)
{
    if (context)
        *context = (TbSymbolSourceContext_t) {0};
}
