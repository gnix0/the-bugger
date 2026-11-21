#ifndef THE_BUGGER_SYMBOL_SCHEDULE_H
#define THE_BUGGER_SYMBOL_SCHEDULE_H

#include "symbol/budget.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TB_SYMBOL_MAX_JOBS      = 64,
    TB_SYMBOL_MAX_INTERESTS = 128,
    TB_SYMBOL_WORK_STEPS    = 64
};

typedef enum {
    TB_SYMBOL_PENDING,
    TB_SYMBOL_SUCCESS,
    TB_SYMBOL_UNSUPPORTED,
    TB_SYMBOL_CANCELLED,
    TB_SYMBOL_STALE,
    TB_SYMBOL_RESOURCE_FAILURE,
    TB_SYMBOL_MALFORMED,
    TB_SYMBOL_OPERATIONAL_FAILURE
} TbSymbolOutcome_e;

typedef enum {
    TB_SYMBOL_INTERACTIVE,
    TB_SYMBOL_CONSTRUCTION
} TbSymbolPriority_e;

typedef struct {
    uint64_t request;
    uint64_t consumer;
    uint64_t revision;
    uint64_t generation;
} TbSymbolWorkIdentity_t;

typedef struct {
    TbSymbolOutcome_e outcome;
    uint64_t          progress;
} TbSymbolWorkResult_t;

/* Work must honor the byte/step ceilings and monotonic deadline, then yield.
 * The supervisor separately bounds blocked syscalls. Progress is an absolute
 * counter of actual operation advancement, not elapsed time or heartbeats. */
typedef TbSymbolWorkResult_t (*tb_symbol_work_fn)(void *context, uint64_t deadline_ns,
                                               size_t max_bytes, unsigned max_steps);
typedef void (*tb_symbol_work_destroy_fn)(void *context);

typedef struct {
    TbSymbolWorkIdentity_t    identity;
    TbSymbolPriority_e        priority;
    tb_symbol_work_fn         step;
    tb_symbol_work_destroy_fn destroy;
    void                     *context;
} TbSymbolWork_t;

/* Return false for backpressure. Completion remains owned until accepted.
 * Events and work_context are borrowed. Cancelled work may have NULL context.
 * Do not reenter or mutate this scheduler from callbacks. */
typedef bool (*tb_symbol_work_emit_fn)(void *context, const TbSymbolWorkIdentity_t *identity,
                                     TbSymbolWorkResult_t result, void *work_context);
typedef struct tb_symbol_schedule TbSymbolSchedule_t;

TbSymbolSchedule_t *tb_symbol_schedule_create(size_t max_jobs, size_t max_interests,
                                             TbSymbolBudget_t *aggregate);
void tb_symbol_schedule_destroy(TbSymbolSchedule_t *schedule);
/* Success adopts context/destroy; failure leaves them with caller. Request IDs are
 * assigned in increasing order by the caller, independently for every interest.
 * Completed work stays owned until publication; cancellation may retire it earlier. */
bool tb_symbol_schedule_add(TbSymbolSchedule_t *schedule, const TbSymbolWork_t *work);
/* Explicitly attach to an already identified shared construction. This never
 * deduplicates by names/digests. Requires the same captured revision/generation. */
bool tb_symbol_schedule_attach(TbSymbolSchedule_t *schedule, uint64_t job,
                                TbSymbolWorkIdentity_t identity);
bool tb_symbol_schedule_cancel(TbSymbolSchedule_t *schedule, TbSymbolWorkIdentity_t identity);
void tb_symbol_schedule_cancel_all(TbSymbolSchedule_t *schedule);
/* One bounded publication attempt and service turn. Four interactive turns at most before
 * a runnable construction turn; oldest turn first within each class. New work
 * enters behind previously admitted runnable work of its class. */
bool tb_symbol_schedule_pump(TbSymbolSchedule_t *schedule, uint64_t now_ns, uint64_t slice_ns,
                            tb_symbol_work_emit_fn emit, void *context);
size_t tb_symbol_schedule_jobs(const TbSymbolSchedule_t *schedule);
size_t tb_symbol_schedule_interests(const TbSymbolSchedule_t *schedule);
size_t tb_symbol_schedule_overhead(void);
size_t tb_symbol_schedule_allocated(const TbSymbolSchedule_t *schedule);

#endif // THE_BUGGER_SYMBOL_SCHEDULE_H
