#ifndef THE_BUGGER_SYMBOL_STORAGE_H
#define THE_BUGGER_SYMBOL_STORAGE_H

#include "symbol/schedule.h"

typedef struct {
    uint64_t device;
    uint64_t inode;
} TbSymbolStorageKey_t;

typedef struct {
    TbSymbolStorageKey_t key;
    uint64_t             bytes;
} TbSymbolStorageInfo_t;

typedef struct tb_symbol_storage TbSymbolStorage_t;

/* Helper-only private filesystem namespace, distinct from content identity.
 * The frontend owns the namespace/slot grants and their cleanup obligations.
 * Creation allocates no file and performs no I/O. path is borrowed until destroy.
 * NULL expected creates an exclusively new owner-only directory; a supplied
 * key reopens only that exact protected directory after helper restart. */
TbSymbolStorage_t *tb_symbol_storage_create(const char *path, const TbSymbolStorageKey_t *expected,
                                           TbSymbolBudget_t *construction);
/* One filesystem operation per step; calls can block under parent supervision. */
TbSymbolOutcome_e tb_symbol_storage_step(TbSymbolStorage_t *storage);
bool tb_symbol_storage_key(const TbSymbolStorage_t *storage, TbSymbolStorageKey_t *key);
bool tb_symbol_storage_created(const TbSymbolStorage_t *storage);
int tb_symbol_storage_error(const TbSymbolStorage_t *storage);
/* Namespace identity can survive helper restart. Destruction closes the local
 * directory descriptor and frees metadata; it never claims filesystem cleanup. */
void tb_symbol_storage_destroy(TbSymbolStorage_t *storage);

/* Qualified local backing types, not source-coherence or reflink proof. */
bool tb_symbol_storage_local_type(unsigned long type);

/* Ready namespace only. Nonzero slot IDs must be granted once, never reused;
 * every entry is exclusively engine-owned. There is no directory scan, arbitrary
 * leaf name, symlink following or recursive deletion. Call only in the helper,
 * with a full logical reservation before any file growth. File operations use
 * at most four filesystem calls; each is independently supervised by the parent.
 * Returned descriptors are close-on-exec and owned by the caller. */
int tb_symbol_storage_writer(TbSymbolStorage_t *storage, uint64_t slot);
bool tb_symbol_storage_info(TbSymbolStorage_t *storage, uint64_t slot, TbSymbolStorageInfo_t *info);
int tb_symbol_storage_reader(TbSymbolStorage_t *storage, uint64_t slot,
                             const TbSymbolStorageInfo_t *expected);
/* Before unlink, close frontend descriptors while the file is still linked and
 * retire dependent worker leases. Final potentially blocking deletion belongs to
 * the helper. Missing entries are idempotent cleanup, never equality evidence.
 * An identity mismatch cannot delete a replacement file. */
bool tb_symbol_storage_unlink(TbSymbolStorage_t *storage, uint64_t slot,
                              const TbSymbolStorageKey_t *expected);
/* Only after all slots and references are retired. Refuses nonempty/replaced
 * directories; namespace cleanup is separate from helper process reaping. */
bool tb_symbol_storage_remove(TbSymbolStorage_t *storage);

#endif // THE_BUGGER_SYMBOL_STORAGE_H
