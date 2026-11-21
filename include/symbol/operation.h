#ifndef THE_BUGGER_SYMBOL_OPERATION_H
#define THE_BUGGER_SYMBOL_OPERATION_H

#include "symbol/acquire.h"
#include "symbol/protocol.h"

typedef struct tb_symbol_operation TbSymbolOperation_t;

typedef struct {
    TbSymbolBudget_t *control;
    TbSymbolBudget_t *decoder;
} TbSymbolOperationBudgets_t;

typedef struct {
    const unsigned char *payload;
    size_t               length;
    int                  descriptor;
    int                  system_error;
    uint32_t             detail;
    uint64_t             record_index;
    TbSymbolStorageKey_t key;
} TbSymbolOperationResult_t;

/* Private helper dispatcher. Arguments are borrowed through destruction.
 * Creation performs no I/O; call only after first-service framing has flushed.
 * Payload schemas use explicit protocol integers, never native C structures.
 * All integers are big-endian. A root/file key is two u64 values (device, inode).
 * Every path is a u32 byte count followed later by exactly one NUL-terminated
 * string, including that terminator; embedded NULs and trailing bytes fail.
 *
 * CREATE_STORAGE: path count, path.
 * ACQUIRE: u64 file ceiling, u64 slot, root key, origin/path counts, origin, path.
 * REMOVE_FILE: u64 slot, root key, file key, path count, path.
 * COMPARE: u64 extent, root key, two (u64 slot, file key), path count, path.
 * REMOVE_STORAGE: root key, path count, path.
 * DISCARD_EMPTY: u64 slot, root key, path count, path.
 * ELF_VALIDATE: u64 extent, u64 slot, root key, file key, u64 selected section,
 *               path count, path. UINT64_MAX selects the directory only.
 * No origin discovery or admission; the caller retains a backing/content lease
 * until worker retirement. Decoder allocations use the separate required decoder
 * account; NULL disables this operation before filesystem access.
 * Successful ELF validation returns 56 bytes: u64 section count, selected section,
 * symbol count; u32 object type, machine, address bytes, byte order (1 little,
 * 2 big), name coverage (0 absent, 1 available, 2 compressed), selected table
 * type (0 for directory only); u64 extent. This covers only the requested
 * structural/dependency scope, not program semantics or runtime authority.
 *
 * Successful acquisition returns u64 extent, file key and 32 digest bytes, with
 * one borrowed read-only descriptor. Creation returns a root key; comparison
 * returns one byte (0 unequal, 1 equal); cleanup returns an empty payload.
 * Results remain borrowed through destruction. Destruction closes local state,
 * never deletes retained files or replaces the caller's cleanup obligations. */
TbSymbolOperation_t *tb_symbol_operation_create(uint32_t operation, const unsigned char *arguments,
                                               size_t length, TbSymbolOperationBudgets_t budgets);
/* ELF validation requires a 64..65536-byte service allowance. */
TbSymbolWorkResult_t tb_symbol_operation_step(TbSymbolOperation_t *operation, size_t max_bytes);
bool tb_symbol_operation_offer(const TbSymbolOperation_t *operation, TbSymbolAcquireOffer_t *offer);
bool tb_symbol_operation_grant(TbSymbolOperation_t *operation, uint64_t bytes);
TbSymbolOperationResult_t tb_symbol_operation_result(const TbSymbolOperation_t *operation);
void tb_symbol_operation_destroy(TbSymbolOperation_t *operation);

#endif // THE_BUGGER_SYMBOL_OPERATION_H
