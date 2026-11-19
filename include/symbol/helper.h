#ifndef THE_BUGGER_SYMBOL_HELPER_H
#define THE_BUGGER_SYMBOL_HELPER_H

#include "symbol/schedule.h"

#include <poll.h>

typedef struct {
    size_t   frontend_bytes;
    size_t   ipc_bytes;
    size_t   max_jobs;
    size_t   max_interests;
    size_t   checkpoint_bytes;
    unsigned checkpoint_steps;
    uint64_t slice_ns;
    uint64_t startup_ns;
    uint64_t queue_ns;
    uint64_t ready_ns;
    uint64_t heavy_ns;
    uint64_t progress_ns;
    uint64_t cancel_ns;
    uint64_t term_ns;
} TbSymbolHelperConfig_t;

typedef struct {
    TbSymbolWorkIdentity_t identity;
    uint32_t               operation;
    TbSymbolPriority_e     priority;
    bool                   heavy;
    const void            *arguments;
    size_t                 length;
} TbSymbolHelperRequest_t;

typedef enum {
    TB_SYMBOL_HELPER_IDLE,
    TB_SYMBOL_HELPER_STARTING,
    TB_SYMBOL_HELPER_READY,
    TB_SYMBOL_HELPER_STOPPING,
    TB_SYMBOL_HELPER_UNAVAILABLE
} TbSymbolHelperState_e;

typedef enum {
    TB_SYMBOL_ADMITTED,
    TB_SYMBOL_BUSY,
    TB_SYMBOL_UNAVAILABLE,
    TB_SYMBOL_INVALID,
    TB_SYMBOL_NO_RESOURCE
} TbSymbolAdmission_e;

typedef struct {
    TbSymbolOutcome_e    outcome;
    const unsigned char *payload;
    size_t               length;
    int                  descriptor;
} TbSymbolHelperResult_t;

typedef struct {
    uint64_t admitted_ns;
    uint64_t queue_deadline_ns;
    uint64_t first_service_ns;
    uint64_t operation_deadline_ns;
    uint64_t last_progress_ns;
    uint64_t progress;
    uint64_t cancel_deadline_ns;
    bool     serviced;
    bool     retiring;
} TbSymbolHelperTiming_t;

/* Borrowed result, retained until accepted. Copy/duplicate required ownership
 * with close-on-exec before returning true. Framing is validated; metadata semantics remain the
 * consumer's responsibility. No reentry or mutation from this callback. */
typedef bool (*tb_symbol_helper_emit_fn)(void *context, const TbSymbolWorkIdentity_t *identity,
                                       TbSymbolHelperResult_t result);
typedef struct tb_symbol_helper TbSymbolHelper_t;

void tb_symbol_helper_defaults(TbSymbolHelperConfig_t *config);
/* Strict positive decimal startup environment; false leaves config unchanged. */
bool tb_symbol_helper_environment(TbSymbolHelperConfig_t *config);
bool tb_symbol_helper_config_valid(const TbSymbolHelperConfig_t *config);
/* Lazy: no process or filesystem acquisition. Optional aggregate outlives helper.
 * The frontend root account can also own independently retained publications. */
TbSymbolHelper_t *tb_symbol_helper_create(const TbSymbolHelperConfig_t *config, TbSymbolBudget_t *aggregate);
/* Does not implicitly abandon a child. False preserves the complete owner while
 * cancellation/reap or unconsumed interests remain. */
bool tb_symbol_helper_destroy(TbSymbolHelper_t *helper);
/* Explicit start/restart only; NULL path selects the running executable. */
bool tb_symbol_helper_start(TbSymbolHelper_t *helper, const char *path);
void tb_symbol_helper_close(TbSymbolHelper_t *helper);
TbSymbolAdmission_e tb_symbol_helper_submit(TbSymbolHelper_t *helper, const TbSymbolHelperRequest_t *request);
bool tb_symbol_helper_attach(TbSymbolHelper_t *helper, uint64_t request, TbSymbolWorkIdentity_t identity);
bool tb_symbol_helper_cancel(TbSymbolHelper_t *helper, TbSymbolWorkIdentity_t identity);
void tb_symbol_helper_pump(TbSymbolHelper_t *helper, tb_symbol_helper_emit_fn emit, void *context);
/* At most two descriptors. Caller supplies that capacity. */
size_t tb_symbol_helper_pollfds(const TbSymbolHelper_t *helper, struct pollfd *fds);
int tb_symbol_helper_timeout(const TbSymbolHelper_t *helper, int maximum_ms);
TbSymbolHelperState_e tb_symbol_helper_state(const TbSymbolHelper_t *helper);
const char *tb_symbol_helper_error(const TbSymbolHelper_t *helper);
size_t tb_symbol_helper_jobs(const TbSymbolHelper_t *helper);
/* Capture the process generation when admitting work. These are distinct from
 * the caller's frontend-context generation. Zero means no such evidence.
 * Confirmed reap proves release of worker ownership only: independently retained
 * frontend results, received descriptors and backing owners still need cleanup.
 * EOF, cancellation, a failed operation or uncertain/lost reap is insufficient. */
uint64_t tb_symbol_helper_process_generation(const TbSymbolHelper_t *helper);
uint64_t tb_symbol_helper_reaped_generation(const TbSymbolHelper_t *helper);
bool tb_symbol_helper_timing(const TbSymbolHelper_t *helper, uint64_t request,
                             TbSymbolHelperTiming_t *timing);

#endif // THE_BUGGER_SYMBOL_HELPER_H
