#ifndef THE_BUGGER_SYMBOL_COMPANION_LOAD_H
#define THE_BUGGER_SYMBOL_COMPANION_LOAD_H

#include "symbol/companion.h"
#include "symbol/helper.h"

typedef struct tb_symbol_companion_load TbSymbolCompanionLoad_t;

typedef struct {
    const char           *path;
    TbSymbolStorageKey_t  key;
    uint64_t              file_bytes;
} TbSymbolCompanionStorage_t;

typedef enum {
    TB_SYMBOL_COMPANION_LOAD_ACQUIRE,
    TB_SYMBOL_COMPANION_LOAD_INSPECT,
    TB_SYMBOL_COMPANION_LOAD_READY,
    TB_SYMBOL_COMPANION_LOAD_CLEANUP,
    TB_SYMBOL_COMPANION_LOAD_BLOCKED,
    TB_SYMBOL_COMPANION_LOAD_DONE
} TbSymbolCompanionLoadPhase_e;

typedef struct {
    TbSymbolCompanionLoadPhase_e phase;
    TbSymbolOutcome_e            outcome;
    TbSymbolCompanionRelation_e  relation;
    uint64_t                     primary;
    uint64_t                     candidate;
    uint64_t                     relationship;
    bool                         outstanding;
    bool                         cancel_required;
    int                          system_error;
    uint32_t                     detail;
    TbSymbolOutcome_e            cleanup_outcome;
    int                          cleanup_system_error;
    uint32_t                     cleanup_detail;
} TbSymbolCompanionLoadInfo_t;

/* One explicitly selected candidate: use companion_paths to enumerate a captured
 * discovery scope, then explicitly start each chosen attempt. No fallback/retry
 * inside this coordinator. Copies the complete absolute candidate and storage
 * namespace spelling; source access only through the supervised helper.
 * Allocates ownership before preparing the independent artifact context/ticket.
 * Candidate context is coordinator-private until commit, not an independently
 * opened artifact for query scope. Do not mutate/close it outside this owner.
 * The caller retains storage namespace ownership through transaction retirement. */
TbSymbolOutcome_e tb_symbol_companion_load_begin(TbSymbolCompanions_t *companions,
    TbSymbolCompanionFact_t *primary, TbSymbolSourcePathText_t candidate, TbSymbolCompanionMatch_e method,
    TbSymbolCompanionStorage_t storage, TbSymbolCompanionLoad_t **load);
/* Plan one job without submitting it. arguments must outlive helper admission
 * copying; no pointers/ABI structs cross the wire. One outstanding job per load.
 * Caller captures the actual helper generation, routes result/release together,
 * and uses a fresh helper request identity for every job. Confirmed nonadmission
 * is finished as a matching failure with no remote retirement obligation. */
TbSymbolOutcome_e tb_symbol_companion_load_request(TbSymbolCompanionLoad_t *load,
    TbSymbolWorkIdentity_t identity, uint64_t process_generation, void *arguments, size_t capacity,
    TbSymbolHelperRequest_t *request);
/* Record exact acquisition offer before granting growth. Failure requires
 * cancellation/retirement, not an unreserved grant. Returned authorization is
 * single-use. Late offers are still recorded for exact cleanup, without a grant. */
TbSymbolOutcome_e tb_symbol_companion_load_offer(TbSymbolCompanionLoad_t *load,
    TbSymbolWorkIdentity_t identity, uint64_t process_generation, TbSymbolAcquireOffer_t offer);
/* Call after matching worker release/reap proof. A matching completion consumes
 * any caller-owned descriptor (duplicate received backing with CLOEXEC first),
 * setting descriptor=-1 even on rejection. Wrong scope consumes nothing.
 * Result bytes remain borrowed through this call. No helper reentry from emit.
 * Cleanup failure blocks the transaction and preserves its exact charges;
 * destroy refuses it. Retry is an explicit separate request, never automatic. */
TbSymbolOutcome_e tb_symbol_companion_load_finish(TbSymbolCompanionLoad_t *load,
    TbSymbolWorkIdentity_t identity, uint64_t process_generation, TbSymbolHelperResult_t *result);
/* READY only. Reserves all ownership earlier and checks captured primary
 * revision, current facts and exact relationship before one publication step.
 * Failure retires only the candidate. No evaluation, target read or GDB action. */
TbSymbolOutcome_e tb_symbol_companion_load_commit(TbSymbolCompanionLoad_t *load);
bool tb_symbol_companion_load_cancel(TbSymbolCompanionLoad_t *load);
bool tb_symbol_companion_load_retry_cleanup(TbSymbolCompanionLoad_t *load);
TbSymbolCompanionLoadInfo_t tb_symbol_companion_load_info(const TbSymbolCompanionLoad_t *load);
/* DONE only, or NULL. Unfinished candidate/job/cleanup ownership cannot vanish. */
bool tb_symbol_companion_load_destroy(TbSymbolCompanionLoad_t *load);

#endif // THE_BUGGER_SYMBOL_COMPANION_LOAD_H
