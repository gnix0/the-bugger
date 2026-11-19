#ifndef THE_BUGGER_SYMBOL_FILE_H
#define THE_BUGGER_SYMBOL_FILE_H

#include "symbol/storage.h"

typedef struct tb_symbol_file TbSymbolFile_t;

typedef struct {
    uint64_t offset;
    size_t   bytes;
} TbSymbolFileRange_t;

typedef struct {
    const unsigned char *data;
    uint64_t             offset;
    size_t               bytes;
} TbSymbolFileView_t;

typedef enum {
    TB_SYMBOL_FILE_NONE,
    TB_SYMBOL_FILE_REQUEST,
    TB_SYMBOL_FILE_DESCRIPTOR,
    TB_SYMBOL_FILE_CHANGED,
    TB_SYMBOL_FILE_IO
} TbSymbolFileFailure_e;

typedef struct {
    TbSymbolFileFailure_e reason;
    int                   system_error;
} TbSymbolFileFailure_t;

/* Helper-only scratch reader for already frozen, pinned backing. Construction
 * reserves all owned storage against the required budget and performs no I/O.
 * The descriptor is borrowed until destruction. Its owner preserves the exact
 * file, content dependency and lease, and closes it only after reader retirement.
 * Descriptor checks cannot establish that every staging writer was closed; the
 * acquisition ownership invariant remains required. No source coherence,
 * metadata validity, runtime binding or target access is established here. */
TbSymbolFile_t *tb_symbol_file_create(TbSymbolStorageInfo_t expected, int descriptor,
                                     TbSymbolBudget_t *construction);
/* One window at a time, 1..65536 bytes wholly within the captured file extent.
 * Rejection preserves the existing state/view. Acceptance invalidates the old
 * borrowed view immediately. Pending/failed requests cannot be replaced or retried
 * on this reader; cancellation retires the reader through its outer job owner. */
bool tb_symbol_file_request(TbSymbolFile_t *file, TbSymbolFileRange_t range);
/* One filesystem syscall per step; max_bytes is 1..65536. Partial reads and EINTR
 * yield without publishing a prefix. The parent supervises blocked syscalls. */
TbSymbolOutcome_e tb_symbol_file_step(TbSymbolFile_t *file, size_t max_bytes);
/* Full requested bytes only, borrowed until the next accepted request/destroy.
 * Failure/pending leaves output unchanged. No independent lifetime is granted. */
bool tb_symbol_file_view(const TbSymbolFile_t *file, TbSymbolFileView_t *view);
TbSymbolFileFailure_t tb_symbol_file_failure(const TbSymbolFile_t *file);
uint64_t tb_symbol_file_progress(const TbSymbolFile_t *file);
void tb_symbol_file_destroy(TbSymbolFile_t *file);

#endif // THE_BUGGER_SYMBOL_FILE_H
