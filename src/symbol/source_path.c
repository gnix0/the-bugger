#include "symbol/source_path.h"

#include <string.h>

enum {
    SOURCE_PATH_BUILD,
    SOURCE_PATH_RULE,
    SOURCE_PATH_MATCH,
    SOURCE_PATH_SELECT,
    SOURCE_PATH_COPY
};

static bool valid_text(TbSymbolSourcePathText_t text)
{
    return !text.bytes || text.data;
}


static bool absolute(TbSymbolSourcePathText_t text)
{
    return text.bytes && text.data[0] == '/';
}


TbSymbolSourcePathStatus_e tb_symbol_source_path_init(TbSymbolSourcePath_t *path,
    TbSymbolSourcePathRequest_t request, unsigned char *recorded, size_t recorded_capacity,
    unsigned char *local, size_t local_capacity)
{
    if (!path || !recorded || !local || !recorded_capacity || !local_capacity ||
        !valid_text(request.file) || !valid_text(request.directory) || !valid_text(request.compilation) ||
        !valid_text(request.image_directory) || (request.rule_count && !request.rules) ||
        request.rule_count > SIZE_MAX / sizeof(*request.rules) ||
        (request.selected_rule != SIZE_MAX && request.selected_rule >= request.rule_count)) {
        return TB_SYMBOL_SOURCE_PATH_INVALID;
    }

    TbSymbolSourcePath_t candidate = {
        .request = request,
        .view = {
            .recorded      = recorded,
            .local         = local,
            .first_rule    = SIZE_MAX,
            .second_rule   = SIZE_MAX,
            .selected_rule = SIZE_MAX
        },
        .recorded          = recorded,
        .local             = local,
        .recorded_capacity = recorded_capacity,
        .local_capacity    = local_capacity,
        .status            = TB_SYMBOL_SOURCE_PATH_PENDING,
        .initialized       = true
    };
    if (!request.file.bytes) {
        candidate.status = TB_SYMBOL_SOURCE_PATH_DEPENDENCY;
        recorded[0] = local[0] = 0;
        *path = candidate;
        return path->status;
    }

    TbSymbolSourcePathText_t directory = request.directory_current ?
                                         (TbSymbolSourcePathText_t) {0} : request.directory;
    if (!absolute(request.file)) {
        if (!absolute(directory) && !request.directory_unknown) {
            if (request.compilation.bytes && !absolute(request.compilation)) {
                if (absolute(request.image_directory)) {
                    candidate.parts[candidate.part_count++] = request.image_directory;
                } else {
                    candidate.need_image = true;
                }
            }
            if (request.compilation.bytes)
                candidate.parts[candidate.part_count++] = request.compilation;
        }
        if (directory.bytes)
            candidate.parts[candidate.part_count++] = directory;
    }
    candidate.parts[candidate.part_count++] = request.file;

    *path = candidate;

    return path->status;
}


static bool usable_byte(unsigned char byte)
{
    return byte != 0 && byte != '\n' && byte != '\r';
}


static bool append(TbSymbolSourcePath_t *path, unsigned char byte)
{
    if (path->view.recorded_bytes == path->recorded_capacity - 1) {
        path->status = TB_SYMBOL_SOURCE_PATH_RESOURCE;
        return false;
    }
    if (!usable_byte(byte)) {
        path->status = TB_SYMBOL_SOURCE_PATH_INVALID;
        return false;
    }

    path->recorded[path->view.recorded_bytes++] = byte;

    return true;
}


static void build(TbSymbolSourcePath_t *path)
{
    if (path->part == path->part_count) {
        path->recorded[path->view.recorded_bytes] = 0;
        path->phase = SOURCE_PATH_RULE;
        return;
    }

    TbSymbolSourcePathText_t part = path->parts[path->part];
    if (!path->cursor && path->part && !path->joining && path->view.recorded_bytes &&
        path->recorded[path->view.recorded_bytes - 1] != '/') {
        if (append(path, '/'))
            path->joining = true;
        return;
    }
    if (path->cursor < part.bytes) {
        if (append(path, part.data[path->cursor]))
            ++path->cursor;
    } else {
        ++path->part;
        path->cursor  = 0;
        path->joining = false;
    }
}


static void next_rule(TbSymbolSourcePath_t *path)
{
    if (path->rule == path->request.rule_count) {
        path->phase = SOURCE_PATH_SELECT;
        return;
    }

    TbSymbolSourcePathRule_t rule = path->request.rules[path->rule];
    if (!valid_text(rule.from) || !valid_text(rule.to) || !rule.from.bytes || !absolute(rule.to)) {
        path->status = TB_SYMBOL_SOURCE_PATH_INVALID;
        return;
    }
    path->rule_cursor  = 0;
    path->rule_matches = rule.from.bytes <= path->view.recorded_bytes;
    path->phase        = SOURCE_PATH_MATCH;
}


static void match(TbSymbolSourcePath_t *path)
{
    TbSymbolSourcePathRule_t rule = path->request.rules[path->rule];
    if (path->rule_cursor < rule.from.bytes) {
        unsigned char byte = rule.from.data[path->rule_cursor];
        if (!usable_byte(byte)) {
            path->status = TB_SYMBOL_SOURCE_PATH_INVALID;
            return;
        }
        if (path->rule_cursor >= path->view.recorded_bytes ||
            byte != path->recorded[path->rule_cursor]) {
            path->rule_matches = false;
        }
        ++path->rule_cursor;
        return;
    }

    bool boundary = rule.from.bytes == path->view.recorded_bytes ||
                    rule.from.data[rule.from.bytes - 1] == '/' ||
                    (rule.from.bytes < path->view.recorded_bytes && path->recorded[rule.from.bytes] == '/');
    if (path->rule_matches && boundary) {
        if (!path->view.matching_rules)
            path->view.first_rule = path->rule;
        else if (path->view.matching_rules == 1)
            path->view.second_rule = path->rule;
        ++path->view.matching_rules;
        if (path->request.selected_rule == path->rule)
            path->view.selected_rule = path->rule;
    }
    ++path->rule;
    path->phase = SOURCE_PATH_RULE;
}


static void select_rule(TbSymbolSourcePath_t *path)
{
    if (path->request.selected_rule != SIZE_MAX) {
        if (path->view.selected_rule == SIZE_MAX) {
            path->status = TB_SYMBOL_SOURCE_PATH_DEPENDENCY;
            return;
        }
    } else if (path->view.matching_rules > 1) {
        path->status = TB_SYMBOL_SOURCE_PATH_AMBIGUOUS;
        return;
    } else {
        path->view.selected_rule = path->view.first_rule;
    }

    size_t selected = path->view.selected_rule;
    if (selected == SIZE_MAX) {
        if (path->need_image ||
            (path->request.directory_unknown && !absolute(path->request.file)) || path->recorded[0] != '/') {
            path->status = TB_SYMBOL_SOURCE_PATH_DEPENDENCY;
            return;
        }
        path->suffix = 0;
    } else {
        path->view.mapped = true;
        TbSymbolSourcePathRule_t rule = path->request.rules[selected];
        path->suffix = rule.from.bytes;
        /* Replacing a prefix ending in '/' retains the separator it consumed. */
        if (rule.from.data[rule.from.bytes - 1] == '/' && path->suffix < path->view.recorded_bytes &&
            rule.to.data[rule.to.bytes - 1] != '/') {
            --path->suffix;
        }
    }
    path->cursor = 0;
    path->phase  = SOURCE_PATH_COPY;
}


static void copy(TbSymbolSourcePath_t *path)
{
    TbSymbolSourcePathText_t prefix = {0};
    if (path->view.mapped)
        prefix = path->request.rules[path->view.selected_rule].to;
    unsigned char byte;
    if (path->cursor < prefix.bytes) {
        byte = prefix.data[path->cursor++];
    } else if (path->suffix < path->view.recorded_bytes) {
        byte = path->recorded[path->suffix++];
    } else {
        path->local[path->output_cursor] = 0;
        path->view.local_bytes          = path->output_cursor;
        path->view.local_available      = true;
        path->status                   = TB_SYMBOL_SOURCE_PATH_COMPLETE;
        return;
    }

    if (!usable_byte(byte)) {
        path->status = TB_SYMBOL_SOURCE_PATH_INVALID;
    } else if (path->output_cursor == path->local_capacity - 1) {
        path->status = TB_SYMBOL_SOURCE_PATH_RESOURCE;
    } else {
        path->local[path->output_cursor++] = byte;
    }
}


TbSymbolSourcePathStatus_e tb_symbol_source_path_step(TbSymbolSourcePath_t *path, size_t max_steps)
{
    if (!path || !path->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_SOURCE_PATH_INVALID;

    for (size_t i = 0; i < max_steps && path->status == TB_SYMBOL_SOURCE_PATH_PENDING; ++i) {
        if (path->progress == UINT64_MAX) {
            path->status = TB_SYMBOL_SOURCE_PATH_RESOURCE;
            break;
        }
        ++path->progress;
        switch (path->phase) {
        case SOURCE_PATH_BUILD:
            build(path);
            break;
        case SOURCE_PATH_RULE:
            next_rule(path);
            break;
        case SOURCE_PATH_MATCH:
            match(path);
            break;
        case SOURCE_PATH_SELECT:
            select_rule(path);
            break;
        case SOURCE_PATH_COPY:
            copy(path);
            break;
        default:
            path->status = TB_SYMBOL_SOURCE_PATH_INVALID;
            break;
        }
    }

    return path->status;
}


bool tb_symbol_source_path_view(const TbSymbolSourcePath_t *path, TbSymbolSourcePathView_t *view)
{
    if (!path || !path->initialized || !view ||
        (path->status != TB_SYMBOL_SOURCE_PATH_COMPLETE && path->status != TB_SYMBOL_SOURCE_PATH_DEPENDENCY &&
         path->status != TB_SYMBOL_SOURCE_PATH_AMBIGUOUS)) {
        return false;
    }

    *view = path->view;

    return true;
}


void tb_symbol_source_path_discard(TbSymbolSourcePath_t *path)
{
    if (path)
        memset(path, 0, sizeof(*path));
}
