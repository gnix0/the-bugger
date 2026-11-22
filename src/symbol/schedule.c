#include "symbol/schedule.h"
#include "symbol/wire.h"

#include <stdlib.h>


typedef struct {
    TbSymbolWork_t       work;
    TbSymbolWorkResult_t result;
    uint64_t             turn;
    size_t               active_interests;
    size_t               interests;
    bool                 used;
    bool                 done;
    bool                 destroyed;
} Job_t;

typedef struct {
    TbSymbolWorkIdentity_t identity;
    size_t                 job;
    bool                   used;
    bool                   cancelled;
} Interest_t;

struct tb_symbol_schedule {
    Job_t             jobs[TB_SYMBOL_MAX_JOBS];
    Interest_t        interests[TB_SYMBOL_MAX_INTERESTS];
    size_t            max_jobs;
    size_t            max_interests;
    size_t            job_count;
    size_t            interest_count;
    size_t            next_publication;
    uint64_t          turn;
    uint64_t          last_request;
    TbSymbolBudget_t *aggregate;
    unsigned          interactive_turns;
};

static void destroy_work(Job_t *job)
{
    if (!job->destroyed) {
        if (job->work.destroy)
            job->work.destroy(job->work.context);
        job->work.context = NULL;
        job->destroyed    = true;
    }
}


TbSymbolSchedule_t *tb_symbol_schedule_create(size_t max_jobs, size_t max_interests,
                                             TbSymbolBudget_t *aggregate)
{
    if (!max_jobs || max_jobs > TB_SYMBOL_MAX_JOBS || !max_interests ||
        max_interests > TB_SYMBOL_MAX_INTERESTS) {
        return NULL;
    }

    if (aggregate && !tb_symbol_budget_reserve(aggregate, sizeof(TbSymbolSchedule_t)))
        return NULL;

    TbSymbolSchedule_t *schedule = calloc(1, sizeof(*schedule));
    if (schedule) {
        schedule->aggregate     = aggregate;
        schedule->max_jobs      = max_jobs;
        schedule->max_interests = max_interests;
    } else if (aggregate) {
        (void)tb_symbol_budget_release(aggregate, sizeof(TbSymbolSchedule_t));
    }

    return schedule;
}


void tb_symbol_schedule_destroy(TbSymbolSchedule_t *schedule)
{
    if (!schedule)
        return;

    for (size_t i = 0; i < schedule->max_jobs; ++i) {
        if (schedule->jobs[i].used)
            destroy_work(&schedule->jobs[i]);
    }

    TbSymbolBudget_t *aggregate = schedule->aggregate;

    free(schedule);

    if (aggregate)
        (void)tb_symbol_budget_release(aggregate, sizeof(TbSymbolSchedule_t));
}


static bool identity_valid(TbSymbolWorkIdentity_t identity)
{
    return identity.request && identity.consumer && identity.generation;
}


static Interest_t *free_interest(TbSymbolSchedule_t *schedule, uint64_t consumer)
{
    Interest_t *available = NULL;
    for (size_t i = 0; i < schedule->max_interests; ++i) {
        Interest_t *interest = &schedule->interests[i];
        if (interest->used && !interest->cancelled && interest->identity.consumer == consumer)
            return NULL;

        if (!interest->used && !available)
            available = interest;
    }

    return available;
}


static void attach_interest(TbSymbolSchedule_t *schedule, Interest_t *interest, size_t index,
                            TbSymbolWorkIdentity_t identity)
{
    *interest = (Interest_t) {
        .identity = identity,
        .job      = index,
        .used     = true
    };

    ++schedule->jobs[index].active_interests;
    ++schedule->jobs[index].interests;
    ++schedule->interest_count;
}


bool tb_symbol_schedule_add(TbSymbolSchedule_t *schedule, const TbSymbolWork_t *work)
{
    if (!schedule || !work || !work->step || !identity_valid(work->identity) ||
        (work->priority != TB_SYMBOL_INTERACTIVE && work->priority != TB_SYMBOL_CONSTRUCTION) ||
        schedule->turn == UINT64_MAX || work->identity.request <= schedule->last_request) {
        return false;
    }

    size_t index = schedule->max_jobs;
    for (size_t i = 0; i < schedule->max_jobs; ++i) {
        if (schedule->jobs[i].used && schedule->jobs[i].work.identity.request == work->identity.request)
            return false;

        if (!schedule->jobs[i].used && index == schedule->max_jobs)
            index = i;
    }

    Interest_t *interest = free_interest(schedule, work->identity.consumer);
    if (index == schedule->max_jobs || !interest)
        return false;

    schedule->jobs[index] = (Job_t) {
        .work = *work,
        .turn = ++schedule->turn,
        .used = true
    };

    attach_interest(schedule, interest, index, work->identity);
    ++schedule->job_count;
    schedule->last_request = work->identity.request;

    return true;
}


bool tb_symbol_schedule_attach(TbSymbolSchedule_t *schedule, uint64_t job_id,
                                TbSymbolWorkIdentity_t identity)
{
    if (!schedule || !identity_valid(identity) || identity.request <= schedule->last_request)
        return false;

    Interest_t *interest = free_interest(schedule, identity.consumer);
    if (!interest)
        return false;

    for (size_t i = 0; i < schedule->max_jobs; ++i) {
        Job_t *job = &schedule->jobs[i];
        if (job->used && !job->done && job->work.identity.request == job_id &&
            job->work.priority == TB_SYMBOL_CONSTRUCTION &&
            job->work.identity.revision == identity.revision &&
            job->work.identity.generation == identity.generation) {
            attach_interest(schedule, interest, i, identity);
            schedule->last_request = identity.request;
            return true;
        }
    }

    return false;
}


bool tb_symbol_schedule_cancel(TbSymbolSchedule_t *schedule, TbSymbolWorkIdentity_t identity)
{
    if (!schedule || !identity_valid(identity))
        return false;

    for (size_t i = 0; i < schedule->max_interests; ++i) {
        Interest_t *interest = &schedule->interests[i];
        if (!interest->used || interest->cancelled || interest->identity.request != identity.request ||
            interest->identity.consumer != identity.consumer ||
            interest->identity.revision != identity.revision ||
            interest->identity.generation != identity.generation) {
            continue;
        }

        interest->cancelled = true;

        Job_t *job = &schedule->jobs[interest->job];

        --job->active_interests;

        if (!job->active_interests && !job->done) {
            job->done           = true;
            job->result.outcome = TB_SYMBOL_CANCELLED;
            destroy_work(job);
        }

        return true;
    }

    return false;
}


void tb_symbol_schedule_cancel_all(TbSymbolSchedule_t *schedule)
{
    if (!schedule)
        return;

    for (size_t i = 0; i < schedule->max_interests; ++i) {
        Interest_t *interest = &schedule->interests[i];
        if (!interest->used || interest->cancelled)
            continue;

        interest->cancelled = true;

        Job_t *job = &schedule->jobs[interest->job];

        --job->active_interests;

        if (!job->active_interests && !job->done) {
            job->done = true;
            job->result.outcome = TB_SYMBOL_CANCELLED;
            destroy_work(job);
        }
    }
}


static bool publish(TbSymbolSchedule_t *schedule, tb_symbol_work_emit_fn emit, void *context)
{
    for (size_t offset = 0; offset < schedule->max_interests; ++offset) {
        size_t index = (schedule->next_publication + offset) % schedule->max_interests;
        Interest_t *interest = &schedule->interests[index];
        if (!interest->used)
            continue;

        Job_t *job = &schedule->jobs[interest->job];
        if (!job->done && !interest->cancelled)
            continue;

        TbSymbolWorkResult_t result = job->result;

        if (interest->cancelled)
            result.outcome = TB_SYMBOL_CANCELLED;
        schedule->next_publication = (index + 1) % schedule->max_interests;

        if (!emit(context, &interest->identity, result, job->work.context))
            return false;

        if (!interest->cancelled)
            --job->active_interests;
        interest->used = false;
        --schedule->interest_count;
        --job->interests;

        if (!job->interests) {
            destroy_work(job);
            *job = (Job_t) {0};
            --schedule->job_count;
        }

        return true;
    }

    return false;
}


bool tb_symbol_schedule_pump(TbSymbolSchedule_t *schedule, uint64_t now_ns, uint64_t slice_ns,
                            tb_symbol_work_emit_fn emit, void *context)
{
    if (!schedule || !emit || !slice_ns)
        return false;

    bool published = publish(schedule, emit, context);

    Job_t *interactive  = NULL;
    Job_t *construction = NULL;
    for (size_t i = 0; i < schedule->max_jobs; ++i) {
        Job_t *job = &schedule->jobs[i];
        if (!job->used || job->done)
            continue;

        Job_t **candidate = job->work.priority == TB_SYMBOL_INTERACTIVE ? &interactive : &construction;
        if (!*candidate || job->turn < (*candidate)->turn)
            *candidate = job;
    }

    Job_t *job = interactive && (!construction || schedule->interactive_turns < 4)
               ? interactive : construction;
    if (!job)
        return published;

    if (schedule->turn == UINT64_MAX) {
        job->result.outcome = TB_SYMBOL_RESOURCE_FAILURE;
        job->done           = true;
        return true;
    }

    if (job == construction)
        schedule->interactive_turns = 0;
    else if (schedule->interactive_turns < 4)
        ++schedule->interactive_turns;
    job->turn = ++schedule->turn;

    uint64_t deadline = slice_ns > UINT64_MAX - now_ns ? UINT64_MAX : now_ns + slice_ns;
    TbSymbolWorkResult_t result = job->work.step(job->work.context, deadline,
                                                TB_SYMBOL_WIRE_IO_BYTES, TB_SYMBOL_WORK_STEPS);
    if (result.outcome < TB_SYMBOL_PENDING || result.outcome > TB_SYMBOL_OPERATIONAL_FAILURE ||
        result.progress < job->result.progress) {
        result = (TbSymbolWorkResult_t) {
            .outcome  = TB_SYMBOL_OPERATIONAL_FAILURE,
            .progress = job->result.progress
        };
    }
    job->result = result;

    if (result.outcome != TB_SYMBOL_PENDING)
        job->done = true;

    return true;
}


size_t tb_symbol_schedule_jobs(const TbSymbolSchedule_t *schedule)
{
    return schedule ? schedule->job_count : 0;
}


size_t tb_symbol_schedule_interests(const TbSymbolSchedule_t *schedule)
{
    return schedule ? schedule->interest_count : 0;
}


size_t tb_symbol_schedule_overhead(void)
{
    return sizeof(TbSymbolSchedule_t);
}


size_t tb_symbol_schedule_allocated(const TbSymbolSchedule_t *schedule)
{
    return schedule ? sizeof(*schedule) : 0;
}
