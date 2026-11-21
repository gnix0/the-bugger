#ifndef THE_BUGGER_SYMBOL_ACQUIRE_H
#define THE_BUGGER_SYMBOL_ACQUIRE_H

#include "symbol/storage.h"
#include "symbol/sha256.h"

typedef struct tb_symbol_acquire TbSymbolAcquire_t;

typedef enum {
    TB_SYMBOL_COPY_PREFER_REFLINK,
    TB_SYMBOL_COPY_STREAM_ONLY
} TbSymbolCopyPolicy_e;

typedef enum {
    TB_SYMBOL_ACQUIRED_NONE,
    TB_SYMBOL_ACQUIRED_STREAM,
    TB_SYMBOL_ACQUIRED_REFLINK
} TbSymbolAcquisitionMethod_e;

typedef enum {
    TB_SYMBOL_ACQUIRE_NONE,
    TB_SYMBOL_ACQUIRE_CHANGED,
    TB_SYMBOL_ACQUIRE_FILE_LIMIT,
    TB_SYMBOL_ACQUIRE_NOT_REGULAR,
    TB_SYMBOL_ACQUIRE_STORAGE,
    TB_SYMBOL_ACQUIRE_IO,
    TB_SYMBOL_ACQUIRE_INTEGRITY
} TbSymbolAcquireFailure_e;

typedef struct {
    uint64_t             bytes;
    TbSymbolStorageKey_t key;
} TbSymbolAcquireOffer_t;

typedef struct {
    uint64_t             bytes;
    unsigned char        digest[TB_SHA256_DIGEST_BYTES];
    int                  descriptor;
    TbSymbolStorageKey_t key;
} TbSymbolAcquiredBytes_t;

/* Helper-only filesystem work. Creation allocates accounted state but performs
 * no I/O; borrowed origin/ready namespace must outlive the operation. Each step
 * performs at most one filesystem operation and inspects at most max_bytes of
 * file data in userspace (1..65536). A clone may process the full reserved extent
 * in the kernel. Filesystem calls can block: the parent supervises them.
 * This component establishes frozen retained bytes, never source coherence,
 * ELF/DWARF validity, physical content identity or permission to access a target. */
TbSymbolAcquire_t *tb_symbol_acquire_create(const char *origin, TbSymbolStorage_t *storage, uint64_t slot,
                                           uint64_t file_limit, TbSymbolBudget_t *construction);
/* Before opening the origin only. Default prefers a bounded clone where supported;
 * the streaming profile remains independently testable. Neither proves atomic
 * source-file coherence. A zero-length clone request is never issued. */
bool tb_symbol_acquire_copy_policy(TbSymbolAcquire_t *acquisition, TbSymbolCopyPolicy_e policy);
TbSymbolAcquisitionMethod_e tb_symbol_acquire_method(const TbSymbolAcquire_t *acquisition);
TbSymbolOutcome_e tb_symbol_acquire_step(TbSymbolAcquire_t *acquisition, size_t max_bytes);
/* The caller preallocates namespace/slot ownership and an unsized ticket before
 * discovery. The offer includes an identified empty staging file. No file data
 * is written until the exact size is reserved and explicitly granted. */
bool tb_symbol_acquire_offer(const TbSymbolAcquire_t *acquisition, uint64_t *bytes);
bool tb_symbol_acquire_grant(TbSymbolAcquire_t *acquisition, uint64_t bytes);
bool tb_symbol_acquire_storage_key(const TbSymbolAcquire_t *acquisition, TbSymbolStorageKey_t *key);
uint64_t tb_symbol_acquire_progress(const TbSymbolAcquire_t *acquisition);
TbSymbolAcquireFailure_e tb_symbol_acquire_failure(const TbSymbolAcquire_t *acquisition, int *system_error);
/* Success transfers the read-only close-on-exec descriptor once. Hashing starts
 * only after supported staging writers have closed, and covers all frozen bytes.
 * An unfinished/failed operation never modifies the output or exposes a digest.
 * Source-stat/path checks detect some mutations; unchanged checks do not prove
 * point-in-time coherence. Required structural validation belongs to the decoder. */
bool tb_symbol_acquire_take(TbSymbolAcquire_t *acquisition, TbSymbolAcquiredBytes_t *bytes);
/* Run in helper context: closes all still-owned descriptors before releasing
 * construction accounting. Named slot ownership and cleanup remain with the
 * caller even on failure; cancellation never publishes a partial candidate. */
void tb_symbol_acquire_destroy(TbSymbolAcquire_t *acquisition);

#endif // THE_BUGGER_SYMBOL_ACQUIRE_H
