#ifndef THE_BUGGER_SYMBOL_SOURCE_CHECK_H
#define THE_BUGGER_SYMBOL_SOURCE_CHECK_H

#include "symbol/md5.h"
#include "symbol/source_file.h"

/* The owner retains immutable raw bytes used to derive the displayed text.
 * Identity/revision are nonzero and never reused. Advance revision BEFORE any
 * byte, length, completeness or selection change; keep the old lease alive until
 * pending checks detach. Escaped/normalized renderer output is not this input. */
typedef struct {
    const unsigned char *data;
    size_t               bytes;
    uint64_t             identity;
    uint64_t             revision;
    bool                 complete;
} TbSymbolSourceBytes_t;

typedef enum {
    TB_SYMBOL_SOURCE_CHECK_PENDING,
    TB_SYMBOL_SOURCE_CHECK_RECORDED,
    TB_SYMBOL_SOURCE_CHECK_MATCH,
    TB_SYMBOL_SOURCE_CHECK_MISMATCH,
    TB_SYMBOL_SOURCE_CHECK_STALE,
    TB_SYMBOL_SOURCE_CHECK_INVALID,
    TB_SYMBOL_SOURCE_CHECK_RESOURCE
} TbSymbolSourceCheckStatus_e;

typedef struct {
    TbSymbolSourceFileView_t    file;
    TbSymbolSourceBytes_t       source;
    unsigned char               recorded[TB_MD5_DIGEST_BYTES];
    unsigned char               calculated[TB_MD5_DIGEST_BYTES];
    TbSymbolSourceCheckStatus_e status;
    bool                        digest_available;
} TbSymbolSourceCheckView_t;

/* Caller owns/accounts state and retains the source/metadata leases. No I/O,
 * allocation, source acquisition or frontend publication. A MATCH applies only
 * to the captured raw displayed-byte snapshot and recorded MD5 profile; neither
 * pathname/stat observations nor this digest prove atomic source acquisition,
 * runtime correspondence or permission. No digest is computed for partial data. */
typedef struct {
    TbSymbolSourceCheckView_t view;
    TbMd5_t                   hash;
    size_t                    cursor;
    bool                      initialized;
} TbSymbolSourceCheck_t;

/* recorded is exactly the validated file.md5_offset span in the retained line
 * section when md5_available; otherwise NULL. Missing expected bytes are INVALID,
 * never a request to discover/open a file. Invalid init preserves old state. */
TbSymbolSourceCheckStatus_e tb_symbol_source_check_init(TbSymbolSourceCheck_t *check,
    TbSymbolSourceFileView_t file, TbSymbolSourceBytes_t source, const unsigned char *recorded);
/* 1..65536 bytes. current describes the still-retained displayed-byte lease;
 * changes finalize STALE. Outer owner enforces cancellation and deadlines. */
TbSymbolSourceCheckStatus_e tb_symbol_source_check_step(TbSymbolSourceCheck_t *check,
    TbSymbolSourceBytes_t current, size_t max_bytes);
/* RECORDED/MATCH/MISMATCH only, and only while the exact lease is current.
 * RECORDED allows qualified browsing with absent digest or incomplete source;
 * a failed, cancelled or stale check cannot expose a verified prefix. */
bool tb_symbol_source_check_view(const TbSymbolSourceCheck_t *check,
    TbSymbolSourceBytes_t current, TbSymbolSourceCheckView_t *view);
void tb_symbol_source_check_discard(TbSymbolSourceCheck_t *check);

#endif // THE_BUGGER_SYMBOL_SOURCE_CHECK_H
