#include "symbol/companion_paths.h"

#include <string.h>

enum {
    PATH_VALIDATE,
    PATH_SELECT,
    PATH_COPY,
    PATH_HEX,
    PATH_SUFFIX
};

static bool valid_span(TbSymbolSourcePathText_t text)
{
    return !text.bytes || text.data;
}


TbSymbolCompanionPathStatus_e tb_symbol_companion_paths_init(TbSymbolCompanionPaths_t *paths,
    TbSymbolCompanionPathsRequest_t request, unsigned char *output, size_t capacity)
{
    if (!paths || !output || !capacity || !valid_span(request.origin) ||
        !valid_span(request.debuglink) || !valid_span(request.build_id) ||
        (request.root_count && !request.roots)) {
        return TB_SYMBOL_COMPANION_PATH_INVALID;
    }
    if (request.root_count > TB_SYMBOL_COMPANION_ROOTS ||
        request.origin.bytes >= TB_SYMBOL_COMPANION_PATH_BYTES ||
        request.debuglink.bytes >= TB_SYMBOL_COMPANION_PATH_BYTES ||
        request.build_id.bytes > TB_SYMBOL_COMPANION_ID_BYTES) {
        return TB_SYMBOL_COMPANION_PATH_RESOURCE;
    }
    if (!request.origin.bytes || request.origin.data[0] != '/')
        return TB_SYMBOL_COMPANION_PATH_UNSUPPORTED;

    *paths = (TbSymbolCompanionPaths_t) {
        .request     = request,
        .output      = output,
        .capacity    = capacity,
        .total       = (request.debuglink.bytes ? 2 + request.root_count : 0) +
                       (request.build_id.bytes ? request.root_count : 0),
        .status      = TB_SYMBOL_COMPANION_PATH_PENDING,
        .initialized = true
    };

    return paths->status;
}


static void validate(TbSymbolCompanionPaths_t *paths)
{
    if (paths->input == paths->request.root_count + 2) {
        if (!paths->directory_bytes || paths->directory_bytes == paths->request.origin.bytes) {
            paths->status = TB_SYMBOL_COMPANION_PATH_UNSUPPORTED;
        } else {
            paths->phase = PATH_SELECT;
        }
        return;
    }

    TbSymbolSourcePathText_t text = paths->input == 0 ? paths->request.origin :
                                   paths->input == 1 ? paths->request.debuglink :
                                   paths->request.roots[paths->input - 2];
    if (!valid_span(text) || (paths->input >= 2 && (!text.bytes || text.data[0] != '/'))) {
        paths->status = TB_SYMBOL_COMPANION_PATH_INVALID;
        return;
    }
    if (text.bytes >= TB_SYMBOL_COMPANION_PATH_BYTES) {
        paths->status = TB_SYMBOL_COMPANION_PATH_RESOURCE;
        return;
    }
    if (paths->cursor < text.bytes) {
        unsigned char byte = text.data[paths->cursor++];
        if (!byte || byte == '\r' || byte == '\n' || (paths->input == 1 && byte == '/')) {
            paths->status = TB_SYMBOL_COMPANION_PATH_UNSUPPORTED;
        } else if (!paths->input && byte == '/') {
            paths->directory_bytes = paths->cursor;
        }
        return;
    }
    if (paths->input == 1 && ((text.bytes == 1 && text.data[0] == '.') ||
        (text.bytes == 2 && text.data[0] == '.' && text.data[1] == '.'))) {
        paths->status = TB_SYMBOL_COMPANION_PATH_UNSUPPORTED;
        return;
    }

    ++paths->input;
    paths->cursor = 0;
}


static void select_path(TbSymbolCompanionPaths_t *paths)
{
    if (paths->ordinal == paths->total) {
        paths->status = TB_SYMBOL_COMPANION_PATH_END;
        return;
    }

    TbSymbolSourcePathText_t directory = {
        .data  = paths->request.origin.data,
        .bytes = paths->directory_bytes
    };
    size_t debug_count = paths->request.debuglink.bytes ? 2 + paths->request.root_count : 0;
    size_t root = SIZE_MAX;
    TbSymbolCompanionPathKind_e kind;
    paths->part_count = 0;
    if (paths->ordinal < debug_count) {
        if (paths->ordinal < 2) {
            paths->parts[paths->part_count++] = directory;
            kind = paths->ordinal ? TB_SYMBOL_COMPANION_DOT_DEBUG : TB_SYMBOL_COMPANION_BESIDE;
            if (paths->ordinal) {
                paths->parts[paths->part_count++] = (TbSymbolSourcePathText_t) {
                    .data  = (const unsigned char *)".debug/",
                    .bytes = 7
                };
            }
        } else {
            kind = TB_SYMBOL_COMPANION_MIRROR;
            root = paths->ordinal - 2;
            paths->parts[paths->part_count++] = paths->request.roots[root];
            paths->parts[paths->part_count++] = directory;
        }
        paths->parts[paths->part_count++] = paths->request.debuglink;
    } else {
        kind = TB_SYMBOL_COMPANION_BUILD_ID;
        root = paths->ordinal - debug_count;
        paths->parts[paths->part_count++] = paths->request.roots[root];
        paths->parts[paths->part_count++] = (TbSymbolSourcePathText_t) {
            .data  = (const unsigned char *)"/.build-id/",
            .bytes = 11
        };
    }
    paths->view = (TbSymbolCompanionPathView_t) {
        .path    = paths->output,
        .ordinal = paths->ordinal,
        .root    = root,
        .kind    = kind
    };
    paths->part = paths->cursor = paths->output_bytes = 0;
    paths->phase = PATH_COPY;
}


static bool append(TbSymbolCompanionPaths_t *paths, unsigned char byte)
{
    if (paths->output_bytes == paths->capacity - 1 ||
        paths->output_bytes == TB_SYMBOL_COMPANION_PATH_BYTES - 1) {
        paths->status = TB_SYMBOL_COMPANION_PATH_RESOURCE;
        return false;
    }

    paths->output[paths->output_bytes++] = byte;

    return true;
}


static void finish_path(TbSymbolCompanionPaths_t *paths)
{
    paths->output[paths->output_bytes] = 0;
    paths->view.bytes = paths->output_bytes;
    paths->status = TB_SYMBOL_COMPANION_PATH_READY;
}


static void copy_path(TbSymbolCompanionPaths_t *paths)
{
    if (paths->part == paths->part_count) {
        if (paths->view.kind == TB_SYMBOL_COMPANION_BUILD_ID) {
            paths->cursor = 0;
            paths->phase = PATH_HEX;
        } else {
            finish_path(paths);
        }
        return;
    }

    TbSymbolSourcePathText_t part = paths->parts[paths->part];
    if (paths->cursor < part.bytes) {
        /* Join exactly one boundary slash; preserve separators inside each part. */
        bool boundary = !paths->cursor && paths->part && part.data[0] == '/' && paths->output_bytes &&
                        paths->output[paths->output_bytes - 1] == '/';
        if (boundary || append(paths, part.data[paths->cursor])) {
            ++paths->cursor;
        }
    } else {
        ++paths->part;
        paths->cursor = 0;
    }
}


static void hex_path(TbSymbolCompanionPaths_t *paths)
{
    if (paths->cursor == paths->request.build_id.bytes * 2 + 1) {
        paths->cursor = 0;
        paths->phase = PATH_SUFFIX;
        return;
    }
    if (paths->cursor == 2) {
        if (append(paths, '/'))
            ++paths->cursor;
        return;
    }

    size_t nibble = paths->cursor < 2 ? paths->cursor : paths->cursor - 1;
    unsigned byte = paths->request.build_id.data[nibble / 2];
    unsigned value = nibble % 2 ? byte & 15U : byte >> 4U;
    if (append(paths, (unsigned char)"0123456789abcdef"[value]))
        ++paths->cursor;
}


TbSymbolCompanionPathStatus_e tb_symbol_companion_paths_step(TbSymbolCompanionPaths_t *paths,
    size_t max_steps)
{
    if (!paths || !paths->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_COMPANION_PATH_INVALID;

    for (size_t i = 0; i < max_steps && paths->status == TB_SYMBOL_COMPANION_PATH_PENDING; ++i) {
        switch (paths->phase) {
        case PATH_VALIDATE:
            validate(paths);
            break;
        case PATH_SELECT:
            select_path(paths);
            break;
        case PATH_COPY:
            copy_path(paths);
            break;
        case PATH_HEX:
            hex_path(paths);
            break;
        case PATH_SUFFIX:
            if (paths->cursor == 6)
                finish_path(paths);
            else if (append(paths, (unsigned char)".debug"[paths->cursor]))
                ++paths->cursor;
            break;
        default:
            paths->status = TB_SYMBOL_COMPANION_PATH_INVALID;
            break;
        }
    }

    return paths->status;
}


bool tb_symbol_companion_paths_view(const TbSymbolCompanionPaths_t *paths, TbSymbolCompanionPathView_t *view)
{
    if (!paths || !view || !paths->initialized || paths->status != TB_SYMBOL_COMPANION_PATH_READY)
        return false;

    *view = paths->view;

    return true;
}


bool tb_symbol_companion_paths_next(TbSymbolCompanionPaths_t *paths)
{
    if (!paths || !paths->initialized || paths->status != TB_SYMBOL_COMPANION_PATH_READY)
        return false;

    ++paths->ordinal;
    paths->phase = PATH_SELECT;
    paths->status = TB_SYMBOL_COMPANION_PATH_PENDING;

    return true;
}


void tb_symbol_companion_paths_discard(TbSymbolCompanionPaths_t *paths)
{
    if (paths)
        memset(paths, 0, sizeof(*paths));
}
