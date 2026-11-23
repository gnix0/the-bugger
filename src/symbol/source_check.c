#include "symbol/source_check.h"

#include <string.h>

static bool same_source(TbSymbolSourceBytes_t left, TbSymbolSourceBytes_t right)
{
    return left.identity == right.identity && left.revision == right.revision &&
           left.data == right.data && left.bytes == right.bytes && left.complete == right.complete;
}


TbSymbolSourceCheckStatus_e tb_symbol_source_check_init(TbSymbolSourceCheck_t *check,
    TbSymbolSourceFileView_t file, TbSymbolSourceBytes_t source, const unsigned char *recorded)
{
    if (!check || !source.identity || !source.revision || (!source.data && source.bytes) ||
        (file.md5_available != (recorded != NULL))) {
        return TB_SYMBOL_SOURCE_CHECK_INVALID;
    }

    TbSymbolSourceCheck_t candidate = {
        .view = {
            .file   = file,
            .source = source,
            .status = TB_SYMBOL_SOURCE_CHECK_RECORDED
        },
        .initialized = true
    };
    if (file.md5_available) {
        memcpy(candidate.view.recorded, recorded, TB_MD5_DIGEST_BYTES);
        candidate.view.digest_available = true;
        if (source.complete) {
            if (source.bytes > UINT64_MAX / 8)
                return TB_SYMBOL_SOURCE_CHECK_RESOURCE;

            (void)tb_md5_init(&candidate.hash);
            candidate.view.status = TB_SYMBOL_SOURCE_CHECK_PENDING;
        }
    }
    *check = candidate;

    return check->view.status;
}


TbSymbolSourceCheckStatus_e tb_symbol_source_check_step(TbSymbolSourceCheck_t *check,
    TbSymbolSourceBytes_t current, size_t max_bytes)
{
    if (!check || !check->initialized || !max_bytes || max_bytes > 65536)
        return TB_SYMBOL_SOURCE_CHECK_INVALID;

    if (!same_source(check->view.source, current)) {
        check->view.status = TB_SYMBOL_SOURCE_CHECK_STALE;
        return check->view.status;
    }
    if (check->view.status != TB_SYMBOL_SOURCE_CHECK_PENDING)
        return check->view.status;

    size_t remaining = current.bytes - check->cursor;
    size_t count = remaining < max_bytes ? remaining : max_bytes;
    const unsigned char *bytes = count ? current.data + check->cursor : NULL;
    if (!tb_md5_update(&check->hash, bytes, count)) {
        check->view.status = TB_SYMBOL_SOURCE_CHECK_RESOURCE;
        return check->view.status;
    }
    check->cursor += count;
    if (check->cursor != current.bytes)
        return check->view.status;

    if (!tb_md5_final(&check->hash, current.bytes, check->view.calculated)) {
        check->view.status = TB_SYMBOL_SOURCE_CHECK_RESOURCE;
    } else {
        check->view.status = memcmp(check->view.recorded, check->view.calculated, TB_MD5_DIGEST_BYTES) ?
                             TB_SYMBOL_SOURCE_CHECK_MISMATCH : TB_SYMBOL_SOURCE_CHECK_MATCH;
    }

    return check->view.status;
}


bool tb_symbol_source_check_view(const TbSymbolSourceCheck_t *check,
    TbSymbolSourceBytes_t current, TbSymbolSourceCheckView_t *view)
{
    if (!check || !check->initialized || !view || !same_source(check->view.source, current) ||
        (check->view.status != TB_SYMBOL_SOURCE_CHECK_RECORDED &&
         check->view.status != TB_SYMBOL_SOURCE_CHECK_MATCH &&
         check->view.status != TB_SYMBOL_SOURCE_CHECK_MISMATCH)) {
        return false;
    }

    *view = check->view;

    return true;
}


void tb_symbol_source_check_discard(TbSymbolSourceCheck_t *check)
{
    if (check)
        *check = (TbSymbolSourceCheck_t) {0};
}
