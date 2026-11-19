#ifndef THE_BUGGER_SYMBOL_ACQUIRE_H
#define THE_BUGGER_SYMBOL_ACQUIRE_H

#include "symbol/schedule.h"
#include "symbol/sha256.h"

typedef struct tb_symbol_acquire TbSymbolAcquire_t;

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
    uint64_t      bytes;
    unsigned char digest[TB_SHA256_DIGEST_BYTES];
    int           descriptor;
} TbSymbolAcquiredBytes_t;

/* Helper-only filesystem work. Creation allocates accounted state but performs
 * no I/O; borrowed NUL-terminated paths must outlive the operation. Each step
 * performs at most one filesystem operation and inspects at most max_bytes of
 * file data (1..65536). Filesystem calls can block: the parent supervises them.
 * This component establishes frozen retained bytes, never source coherence,
 * ELF/DWARF validity, physical content identity or permission to access a target. */
TbSymbolAcquire_t *tb_symbol_acquire_create(const char *origin, const char *storage_directory,
                                           uint64_t file_limit, TbSymbolBudget_t *construction);
TbSymbolOutcome_e tb_symbol_acquire_step(TbSymbolAcquire_t *acquisition, size_t max_bytes);
/* No storage is created or grown until the exact offered size is reserved by
 * the parent and explicitly granted. Granting never changes the offered size. */
bool tb_symbol_acquire_offer(const TbSymbolAcquire_t *acquisition, uint64_t *bytes);
bool tb_symbol_acquire_grant(TbSymbolAcquire_t *acquisition, uint64_t bytes);
uint64_t tb_symbol_acquire_progress(const TbSymbolAcquire_t *acquisition);
TbSymbolAcquireFailure_e tb_symbol_acquire_failure(const TbSymbolAcquire_t *acquisition, int *system_error);
/* Success transfers the read-only close-on-exec descriptor once. Hashing starts
 * only after supported staging writers have closed, and covers all frozen bytes.
 * An unfinished/failed operation never modifies the output or exposes a digest.
 * Source-stat/path checks detect some mutations; unchanged checks do not prove
 * point-in-time coherence. Required structural validation belongs to the decoder. */
bool tb_symbol_acquire_take(TbSymbolAcquire_t *acquisition, TbSymbolAcquiredBytes_t *bytes);
/* Run in helper context: closes all still-owned descriptors before releasing
 * construction accounting. Cancellation does not publish a partial candidate. */
void tb_symbol_acquire_destroy(TbSymbolAcquire_t *acquisition);

#endif // THE_BUGGER_SYMBOL_ACQUIRE_H
