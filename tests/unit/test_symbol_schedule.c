#include "symbol/schedule.h"
#include "../support/check.h"

#include <stdlib.h>
#include <string.h>

static bool fail_allocation;

void *__real_calloc(size_t count, size_t length);
void *__wrap_calloc(size_t count, size_t length);

void *__wrap_calloc(size_t count, size_t length)
{
    return fail_allocation ? NULL : __real_calloc(count, length);
}


typedef struct {
    unsigned steps;
    unsigned finish;
    unsigned destroyed;
    bool     regress;
} WorkState_t;

typedef struct {
    TbSymbolWorkIdentity_t identities[TB_SYMBOL_MAX_INTERESTS];
    TbSymbolWorkResult_t   results[TB_SYMBOL_MAX_INTERESTS];
    size_t                 count;
    bool                   blocked;
} Observer_t;

static TbSymbolWorkResult_t step(void *context, uint64_t deadline, size_t max_bytes, unsigned max_steps)
{
    WorkState_t *work = context;
    CHECK(deadline >= 100 && max_bytes <= 65536 && max_steps <= 256);
    ++work->steps;
    return (TbSymbolWorkResult_t){
        .outcome  = work->steps == work->finish ? TB_SYMBOL_SUCCESS : TB_SYMBOL_PENDING,
        .progress = work->regress && work->steps > 1 ? 0 : work->steps
    };
}


static void destroy(void *context)
{
    WorkState_t *work = context;
    CHECK(!work->destroyed);
    ++work->destroyed;
}


static TbSymbolWork_t work_for(WorkState_t *state, uint64_t request, uint64_t consumer,
                               TbSymbolPriority_e priority)
{
    return (TbSymbolWork_t){
        .identity = {
            .request    = request,
            .consumer   = consumer,
            .revision   = 7,
            .generation = 3
        },
        .priority = priority,
        .step     = step,
        .destroy  = destroy,
        .context  = state
    };
}


static bool emit(void *context, const TbSymbolWorkIdentity_t *identity, TbSymbolWorkResult_t result,
                 void *work_context)
{
    if (result.outcome != TB_SYMBOL_CANCELLED)
        CHECK(work_context);
    Observer_t *observer = context;
    if (observer->blocked)
        return false;
    CHECK(observer->count < TB_SYMBOL_MAX_INTERESTS);
    observer->identities[observer->count] = *identity;
    observer->results[observer->count++]  = result;
    return true;
}


static void pump(TbSymbolSchedule_t *schedule, Observer_t *observer)
{
    CHECK(tb_symbol_schedule_pump(schedule, 100, 5000000, emit, observer));
}


static void shared_lifetime(void)
{
    TbSymbolSchedule_t *schedule = tb_symbol_schedule_create(64, 128, NULL);
    CHECK(schedule);
    WorkState_t state = {
        .finish = 2
    };
    TbSymbolWork_t work = work_for(&state, 1, 1, TB_SYMBOL_CONSTRUCTION);
    CHECK(tb_symbol_schedule_add(schedule, &work));
    CHECK(!tb_symbol_schedule_add(schedule, &work));
    TbSymbolWorkIdentity_t other = {
        .request    = 2,
        .consumer   = 2,
        .revision   = 7,
        .generation = 3
    };
    TbSymbolWorkIdentity_t stale = other;
    stale.generation++;
    CHECK(!tb_symbol_schedule_attach(schedule, 1, stale));
    stale = other;
    stale.revision++;
    CHECK(!tb_symbol_schedule_attach(schedule, 1, stale));
    CHECK(tb_symbol_schedule_attach(schedule, 1, other));
    CHECK(tb_symbol_schedule_jobs(schedule) == 1 && tb_symbol_schedule_interests(schedule) == 2);
    CHECK(tb_symbol_schedule_cancel(schedule, work.identity));
    CHECK(!state.destroyed);
    Observer_t observer = {0};
    pump(schedule, &observer);
    CHECK(observer.count == 1 && observer.results[0].outcome == TB_SYMBOL_CANCELLED);
    CHECK(observer.identities[0].request == 1 && state.steps == 1);
    observer.blocked = true;
    pump(schedule, &observer);
    CHECK(state.steps == 2 && !state.destroyed);
    CHECK(!tb_symbol_schedule_pump(schedule, 100, 5000000, emit, &observer));
    CHECK(!state.destroyed && tb_symbol_schedule_jobs(schedule) == 1);
    observer.blocked = false;
    pump(schedule, &observer);
    CHECK(observer.count == 2 && observer.results[1].outcome == TB_SYMBOL_SUCCESS);
    CHECK(observer.identities[1].request == 2 && observer.identities[1].consumer == 2);
    CHECK(state.destroyed == 1 && !tb_symbol_schedule_jobs(schedule));
    CHECK(!tb_symbol_schedule_cancel(schedule, other));
    CHECK(!tb_symbol_schedule_add(schedule, &work));
    tb_symbol_schedule_destroy(schedule);
    CHECK(state.destroyed == 1);
}


static void fairness(void)
{
    TbSymbolSchedule_t *schedule = tb_symbol_schedule_create(64, 128, NULL);
    CHECK(schedule);
    WorkState_t interactive[8] = {0};
    WorkState_t construction[3] = {0};
    uint64_t next = 0;
    for (size_t i = 0; i < 3; ++i) {
        ++next;
        TbSymbolWork_t work = work_for(&construction[i], next, next + 100, TB_SYMBOL_CONSTRUCTION);
        CHECK(tb_symbol_schedule_add(schedule, &work));
    }
    for (size_t i = 0; i < 8; ++i) {
        ++next;
        TbSymbolWork_t work = work_for(&interactive[i], next, next + 100, TB_SYMBOL_INTERACTIVE);
        CHECK(tb_symbol_schedule_add(schedule, &work));
    }
    Observer_t observer = {0};
    for (unsigned turn = 0; turn < 30; ++turn) {
        pump(schedule, &observer);
        if (turn == 4)
            CHECK(construction[0].steps == 1);
        if (turn == 9)
            CHECK(construction[1].steps == 1);
        if (turn == 14)
            CHECK(construction[2].steps == 1);
    }
    CHECK(construction[0].steps == 2 && construction[1].steps == 2 && construction[2].steps == 2);
    for (size_t i = 0; i < 8; ++i)
        CHECK(interactive[i].steps == 3);
    tb_symbol_schedule_destroy(schedule);
    for (size_t i = 0; i < 8; ++i)
        CHECK(interactive[i].destroyed == 1);
    for (size_t i = 0; i < 3; ++i)
        CHECK(construction[i].destroyed == 1);
}


static void arrival_fairness(void)
{
    TbSymbolSchedule_t *schedule = tb_symbol_schedule_create(64, 128, NULL);
    CHECK(schedule);
    WorkState_t construction = {0};
    WorkState_t arrivals[50] = {0};
    TbSymbolWork_t work = work_for(&construction, 1, 1, TB_SYMBOL_CONSTRUCTION);
    CHECK(tb_symbol_schedule_add(schedule, &work));
    Observer_t observer = {0};
    for (size_t i = 0; i < 50; ++i) {
        arrivals[i].finish = 1;
        work = work_for(&arrivals[i], i + 2, i + 2, TB_SYMBOL_INTERACTIVE);
        CHECK(tb_symbol_schedule_add(schedule, &work));
        pump(schedule, &observer);
        CHECK(construction.steps >= (i + 1) / 5);
    }
    CHECK(construction.steps == 10);
    tb_symbol_schedule_destroy(schedule);
    CHECK(construction.destroyed == 1);
    for (size_t i = 0; i < 50; ++i)
        CHECK(arrivals[i].destroyed == 1);
}


static void saturation_and_control(void)
{
    CHECK(!tb_symbol_schedule_create(0, 128, NULL));
    CHECK(!tb_symbol_schedule_create(65, 128, NULL));
    CHECK(!tb_symbol_schedule_create(64, 129, NULL));
    TbSymbolSchedule_t *schedule = tb_symbol_schedule_create(64, 128, NULL);
    CHECK(schedule && tb_symbol_schedule_allocated(schedule) < 64 * 1024);
    WorkState_t states[65] = {0};
    TbSymbolWork_t jobs[65];
    for (size_t i = 0; i < 65; ++i) {
        jobs[i] = work_for(&states[i], i + 1, i + 1, TB_SYMBOL_CONSTRUCTION);
        CHECK(tb_symbol_schedule_add(schedule, &jobs[i]) == (i < 64));
    }
    CHECK(!states[64].destroyed && tb_symbol_schedule_jobs(schedule) == 64);
    for (size_t i = 0; i < 64; ++i) {
        TbSymbolWorkIdentity_t interest = {
            .request    = 100 + i,
            .consumer   = 100 + i,
            .revision   = 7,
            .generation = 3
        };
        CHECK(tb_symbol_schedule_attach(schedule, i + 1, interest));
    }
    CHECK(tb_symbol_schedule_interests(schedule) == 128);
    TbSymbolWorkIdentity_t rejected = {
        .request    = 200,
        .consumer   = 200,
        .revision   = 7,
        .generation = 3
    };
    CHECK(!tb_symbol_schedule_attach(schedule, 1, rejected));
    for (size_t i = 0; i < 64; ++i)
        CHECK(tb_symbol_schedule_cancel(schedule, jobs[i].identity));
    CHECK(tb_symbol_schedule_jobs(schedule) == 64);
    Observer_t observer = {0};
    for (size_t i = 0; i < 64; ++i)
        pump(schedule, &observer);
    CHECK(observer.count == 64 && tb_symbol_schedule_interests(schedule) == 64);
    CHECK(!states[0].destroyed);
    tb_symbol_schedule_destroy(schedule);
    for (size_t i = 0; i < 64; ++i)
        CHECK(states[i].destroyed == 1);
}


static void cancellation_and_progress(void)
{
    TbSymbolSchedule_t *schedule = tb_symbol_schedule_create(1, 2, NULL);
    CHECK(schedule);
    WorkState_t state = {
        .regress = true
    };
    TbSymbolWork_t work = work_for(&state, 1, 1, TB_SYMBOL_INTERACTIVE);
    CHECK(tb_symbol_schedule_add(schedule, &work));
    Observer_t observer = {0};
    pump(schedule, &observer);
    pump(schedule, &observer);
    CHECK(!state.destroyed);
    pump(schedule, &observer);
    CHECK(observer.results[0].outcome == TB_SYMBOL_OPERATIONAL_FAILURE && state.destroyed == 1);
    WorkState_t cancelled = {0};
    work = work_for(&cancelled, 2, 1, TB_SYMBOL_CONSTRUCTION);
    CHECK(tb_symbol_schedule_add(schedule, &work));
    CHECK(tb_symbol_schedule_cancel(schedule, work.identity));
    CHECK(cancelled.destroyed == 1 && !cancelled.steps);
    pump(schedule, &observer);
    CHECK(observer.results[1].outcome == TB_SYMBOL_CANCELLED);
    tb_symbol_schedule_destroy(schedule);
    CHECK(cancelled.destroyed == 1);
}


static void shared_budget(void)
{
    TbSymbolBudget_t aggregate;
    CHECK(tb_symbol_budget_init(&aggregate, SIZE_MAX));
    fail_allocation = true;
    CHECK(!tb_symbol_schedule_create(64, 128, &aggregate) && !aggregate.used);
    fail_allocation = false;
    TbSymbolSchedule_t *first = tb_symbol_schedule_create(64, 128, &aggregate);
    CHECK(first);
    size_t fixed = tb_symbol_schedule_allocated(first);
    CHECK(aggregate.used == fixed);
    aggregate.limit = fixed * 2 - 1;
    CHECK(!tb_symbol_schedule_create(1, 1, &aggregate) && aggregate.used == fixed);
    aggregate.limit++;
    TbSymbolSchedule_t *second = tb_symbol_schedule_create(1, 1, &aggregate);
    CHECK(second && aggregate.used == fixed * 2);
    tb_symbol_schedule_destroy(first);
    CHECK(aggregate.used == fixed);
    tb_symbol_schedule_destroy(second);
    CHECK(!aggregate.used);
}


int main(void)
{
    shared_budget();
    shared_lifetime();
    fairness();
    arrival_fairness();
    saturation_and_control();
    cancellation_and_progress();
    puts("PASS symbol scheduler: bounded admission, shared interests, fairness, "
         "cancellation and retained publication");
    return 0;
}
