#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Isolated helper integration. */
#include "symbol/helper.h"
#include "symbol/protocol.h"
#include "symbol/worker.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/file.h>
#include <unistd.h>

int __real_clock_gettime(clockid_t clock_id, struct timespec *time);
int __wrap_clock_gettime(clockid_t clock_id, struct timespec *time);
void *__real_malloc(size_t length);
void *__real_calloc(size_t count, size_t length);
void *__wrap_malloc(size_t length);
void *__wrap_calloc(size_t count, size_t length);

static bool fake_clock;
static const char *production_binary;
static uint64_t clock_ns = UINT64_C(100000000000);
static size_t fail_malloc_size;
static bool fail_calloc;
static int clock_file = -1;
static char clock_path[] = "/tmp/the-bugger-symbol-clock-XXXXXX";

int __wrap_clock_gettime(clockid_t clock_id, struct timespec *time)
{
    if (!fake_clock || clock_id != CLOCK_MONOTONIC)
        return __real_clock_gettime(clock_id, time);
    *time = (struct timespec){
        .tv_sec  = (time_t)(clock_ns / UINT64_C(1000000000)),
        .tv_nsec = (long)(clock_ns % UINT64_C(1000000000))
    };
    return 0;
}


void *__wrap_malloc(size_t length)
{
    return fail_malloc_size && fail_malloc_size == length ? NULL : __real_malloc(length);
}


void *__wrap_calloc(size_t count, size_t length)
{
    return fail_calloc ? NULL : __real_calloc(count, length);
}


typedef struct {
    TbSymbolWorkIdentity_t identities[128];
    TbSymbolOutcome_e     outcomes[128];
    char                  values[128][32];
    size_t                count;
    size_t                blocked_attempts;
    bool                  blocked;
} Observer_t;

static bool emit(void *context, const TbSymbolWorkIdentity_t *identity, TbSymbolHelperResult_t result)
{
    Observer_t *observer = context;
    if (observer->blocked) {
        ++observer->blocked_attempts;
        return false;
    }
    CHECK(observer->count < 128 && result.length < sizeof(observer->values[0]));
    size_t index = observer->count++;
    observer->identities[index] = *identity;
    observer->outcomes[index] = result.outcome;
    if (result.length)
        memcpy(observer->values[index], result.payload, result.length);
    CHECK(result.descriptor == -1);
    return true;
}


static TbSymbolWorkIdentity_t identity(uint64_t request, uint64_t consumer)
{
    return (TbSymbolWorkIdentity_t){
        .request    = request,
        .consumer   = consumer,
        .revision   = 47,
        .generation = 19
    };
}


static TbSymbolAdmission_e submit(TbSymbolHelper_t *helper, TbSymbolWorkIdentity_t captured,
                                  uint32_t operation, TbSymbolPriority_e priority, bool heavy,
                                  const void *arguments, size_t length)
{
    TbSymbolHelperRequest_t request = {
        .identity  = captured,
        .operation = operation,
        .priority  = priority,
        .heavy     = heavy,
        .arguments = arguments,
        .length    = length
    };
    return tb_symbol_helper_submit(helper, &request);
}


static void pump(TbSymbolHelper_t *helper, Observer_t *observer)
{
    if (fake_clock && clock_file >= 0) {
        CHECK(flock(clock_file, LOCK_EX) == 0);
        CHECK(pwrite(clock_file, &clock_ns, sizeof(clock_ns), 0) == (ssize_t)sizeof(clock_ns));
        CHECK(flock(clock_file, LOCK_UN) == 0);
    }
    tb_symbol_helper_pump(helper, emit, observer);
    struct pollfd fds[2];
    size_t count = tb_symbol_helper_pollfds(helper, fds);
    CHECK(count <= 2 && poll(fds, (nfds_t)count, tb_symbol_helper_timeout(helper, 2)) >= 0);
}


static void ready(TbSymbolHelper_t *helper, Observer_t *observer)
{
    for (unsigned i = 0; i < 2000 && tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_STARTING; ++i)
        pump(helper, observer);
    if (tb_symbol_helper_state(helper) != TB_SYMBOL_HELPER_READY)
        fprintf(stderr, "helper start: %s\n", tb_symbol_helper_error(helper));
    CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_READY);
}


static void results(TbSymbolHelper_t *helper, Observer_t *observer, size_t count)
{
    for (unsigned i = 0; i < 2000 && observer->count < count; ++i)
        pump(helper, observer);
    if (observer->count != count) {
        fprintf(stderr, "helper results: %zu/%zu, %s\n", observer->count, count,
                tb_symbol_helper_error(helper));
    }
    CHECK(observer->count == count);
}


static void close_helper(TbSymbolHelper_t *helper, Observer_t *observer)
{
    tb_symbol_helper_close(helper);
    for (unsigned i = 0; i < 2000; ++i) {
        if (tb_symbol_helper_destroy(helper))
            return;
        if (fake_clock)
            clock_ns += UINT64_C(1000000);
        pump(helper, observer);
    }
    CHECK(false);
}


static void real_worker(void)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    CHECK(tb_symbol_helper_config_valid(&config));
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, config.frontend_bytes));
    TbSymbolHelper_t *helper = tb_symbol_helper_create(&config, &budget);
    CHECK(helper && budget.used && tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_IDLE);
    struct pollfd fds[2];
    CHECK(!tb_symbol_helper_pollfds(helper, fds) && tb_symbol_helper_timeout(helper, 100) == 100);
    CHECK(tb_symbol_helper_destroy(helper) && !budget.used);
    helper = tb_symbol_helper_create(&config, &budget);
    CHECK(helper && tb_symbol_helper_start(helper, production_binary));
    Observer_t observer = {0};
    ready(helper, &observer);
    CHECK(submit(helper, identity(1, 1), 900, TB_SYMBOL_CONSTRUCTION, true, NULL, 0) ==
          TB_SYMBOL_ADMITTED);
    CHECK(tb_symbol_helper_attach(helper, 1, identity(2, 2)));
    CHECK(tb_symbol_helper_cancel(helper, identity(1, 1)));
    /* Replacing the creator's interest must neither detach another pane nor
     * collide with the worker's internal consumer/dispatch ordering. */
    CHECK(submit(helper, identity(3, 1), 901, TB_SYMBOL_INTERACTIVE, false, "arg", 3) ==
          TB_SYMBOL_ADMITTED);
    results(helper, &observer, 3);
    CHECK(observer.identities[0].request == 1 && observer.outcomes[0] == TB_SYMBOL_CANCELLED);
    unsigned unsupported = 0;
    for (size_t i = 1; i < observer.count; ++i) {
        CHECK(observer.outcomes[i] == TB_SYMBOL_UNSUPPORTED);
        CHECK(observer.identities[i].generation == 19 && observer.identities[i].revision == 47);
        CHECK(observer.identities[i].request == 2 || observer.identities[i].request == 3);
        ++unsupported;
    }
    CHECK(unsupported == 2 && !tb_symbol_helper_jobs(helper));
    CHECK(submit(helper, identity(3, 3), 902, TB_SYMBOL_INTERACTIVE, false, NULL, 0) ==
          TB_SYMBOL_INVALID);
    close_helper(helper, &observer);
    CHECK(!budget.used);
}


static void saturation(void)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    TbSymbolHelper_t *helper = tb_symbol_helper_create(&config, NULL);
    CHECK(helper && tb_symbol_helper_start(helper, NULL));
    Observer_t observer = {0};
    ready(helper, &observer);
    for (uint64_t i = 1; i <= 64; ++i)
        CHECK(submit(helper, identity(i, i), 990, TB_SYMBOL_CONSTRUCTION, true, NULL, 0) ==
              TB_SYMBOL_ADMITTED);
    CHECK(tb_symbol_helper_jobs(helper) == 64);
    CHECK(submit(helper, identity(65, 65), 990, TB_SYMBOL_CONSTRUCTION, true, NULL, 0) ==
          TB_SYMBOL_BUSY);
    for (uint64_t i = 1; i <= 64; ++i)
        CHECK(tb_symbol_helper_cancel(helper, identity(i, i)));
    results(helper, &observer, 64);
    for (size_t i = 0; i < observer.count; ++i)
        CHECK(observer.outcomes[i] == TB_SYMBOL_CANCELLED);
    CHECK(!tb_symbol_helper_jobs(helper));
    close_helper(helper, &observer);
}


static void allocation_failures(void)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, config.frontend_bytes));
    fail_calloc = true;
    CHECK(!tb_symbol_helper_create(&config, &budget) && !budget.used);
    fail_calloc = false;
    TbSymbolHelper_t *helper = tb_symbol_helper_create(&config, &budget);
    CHECK(helper && tb_symbol_helper_start(helper, NULL));
    Observer_t observer = {0};
    ready(helper, &observer);
    size_t baseline = budget.used;
    fail_malloc_size = TB_SYMBOL_REQUEST_BYTES + 4;
    CHECK(submit(helper, identity(1, 1), 990, TB_SYMBOL_INTERACTIVE, false, "fail", 4) ==
          TB_SYMBOL_NO_RESOURCE);
    CHECK(budget.used == baseline && !tb_symbol_helper_jobs(helper));
    fail_malloc_size = 0;
    CHECK(submit(helper, identity(2, 1), 990, TB_SYMBOL_INTERACTIVE, false, "fail", 4) ==
          TB_SYMBOL_ADMITTED);
    fail_malloc_size = TB_SYMBOL_REQUEST_BYTES + 4;
    results(helper, &observer, 1);
    fail_malloc_size = 0;
    CHECK(observer.outcomes[0] == TB_SYMBOL_RESOURCE_FAILURE);
    CHECK(!tb_symbol_helper_jobs(helper));
    close_helper(helper, &observer);
    CHECK(!budget.used);
}


static TbSymbolHelper_t *fake_helper(const char *mode, TbSymbolHelperConfig_t *config, Observer_t *observer)
{
    CHECK(setenv("TB_TEST_SYMBOL_HELPER", mode, 1) == 0);
    if (fake_clock) {
        if (clock_file < 0) {
            clock_file = mkstemp(clock_path);
            CHECK(clock_file >= 0 && fcntl(clock_file, F_SETFD, FD_CLOEXEC) == 0);
        }
        CHECK(pwrite(clock_file, &clock_ns, sizeof(clock_ns), 0) == (ssize_t)sizeof(clock_ns));
        CHECK(setenv("TB_TEST_SYMBOL_CLOCK_FILE", clock_path, 1) == 0);
    } else {
        CHECK(unsetenv("TB_TEST_SYMBOL_CLOCK_FILE") == 0);
    }
    TbSymbolHelper_t *helper = tb_symbol_helper_create(config, NULL);
    CHECK(helper && tb_symbol_helper_start(helper, NULL));
    if (strcmp(mode, "startup") != 0)
        ready(helper, observer);
    return helper;
}


static TbSymbolHelperTiming_t serviced(TbSymbolHelper_t *helper, Observer_t *observer)
{
    TbSymbolHelperTiming_t timing = {0};
    for (unsigned i = 0; i < 2000; ++i) {
        CHECK(tb_symbol_helper_timing(helper, 1, &timing));
        if (timing.serviced)
            return timing;
        pump(helper, observer);
    }
    CHECK(false);
    return timing;
}


static void reap_evidence(void)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    Observer_t observer = {0};
    TbSymbolHelper_t *helper = tb_symbol_helper_create(&config, NULL);
    CHECK(helper && !tb_symbol_helper_process_generation(helper));
    CHECK(!tb_symbol_helper_reaped_generation(helper));
    for (uint64_t generation = 1; generation <= 2; ++generation) {
        CHECK(tb_symbol_helper_start(helper, production_binary));
        CHECK(tb_symbol_helper_process_generation(helper) == generation);
        CHECK(tb_symbol_helper_reaped_generation(helper) == generation - 1);
        ready(helper, &observer);
        tb_symbol_helper_close(helper);
        CHECK(tb_symbol_helper_reaped_generation(helper) == generation - 1);
        for (unsigned i = 0; i < 2000 && tb_symbol_helper_state(helper) != TB_SYMBOL_HELPER_IDLE; ++i)
            pump(helper, &observer);
        CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_IDLE);
        CHECK(tb_symbol_helper_reaped_generation(helper) == generation);
    }
    CHECK(tb_symbol_helper_destroy(helper));

    helper = fake_helper("crash", &config, &observer);
    struct sigaction ignored = {0};
    struct sigaction previous;
    ignored.sa_handler = SIG_IGN;
    CHECK(!sigemptyset(&ignored.sa_mask));
    CHECK(!sigaction(SIGCHLD, &ignored, &previous));
    CHECK(previous.sa_handler == SIG_DFL);
    CHECK(submit(helper, identity(2, 2), 990, TB_SYMBOL_INTERACTIVE, false, NULL, 0) == TB_SYMBOL_ADMITTED);
    results(helper, &observer, 1);
    CHECK(observer.outcomes[0] == TB_SYMBOL_OPERATIONAL_FAILURE);
    struct pollfd fds[2];
    for (unsigned i = 0; i < 2000 && tb_symbol_helper_pollfds(helper, fds); ++i)
        pump(helper, &observer);
    CHECK(!tb_symbol_helper_pollfds(helper, fds));
    CHECK(tb_symbol_helper_process_generation(helper) == 1);
    CHECK(!tb_symbol_helper_reaped_generation(helper));
    CHECK(!sigaction(SIGCHLD, &previous, NULL));
    CHECK(!tb_symbol_helper_start(helper, production_binary));
    CHECK(tb_symbol_helper_destroy(helper));
    CHECK(!unsetenv("TB_TEST_SYMBOL_HELPER"));
    CHECK(!tb_symbol_helper_process_generation(NULL) && !tb_symbol_helper_reaped_generation(NULL));
}


typedef struct {
    Observer_t observer;
    size_t     attempts;
    size_t     released;
    bool       blocked;
    bool       seen[TB_SYMBOL_MAX_JOBS];
} ReleaseObserver_t;

static bool emit_owned(void *context, const TbSymbolWorkIdentity_t *captured, TbSymbolHelperResult_t result)
{
    ReleaseObserver_t *observer = context;
    return emit(&observer->observer, captured, result);
}


static bool released(void *context, const TbSymbolWorkIdentity_t *captured, uint64_t generation)
{
    ReleaseObserver_t *observer = context;
    bool delivered = false;
    for (size_t i = 0; i < observer->observer.count; ++i)
        delivered |= observer->observer.identities[i].request == captured->request;
    CHECK(delivered && captured->request && captured->request <= TB_SYMBOL_MAX_JOBS);
    CHECK(captured->consumer == captured->request);
    CHECK(captured->generation == 19 && captured->revision == 47 && generation == 1);
    ++observer->attempts;
    if (observer->blocked)
        return false;
    CHECK(!observer->seen[captured->request - 1]);
    observer->seen[captured->request - 1] = true;
    ++observer->released;
    return true;
}


static void pump_owned(TbSymbolHelper_t *helper, ReleaseObserver_t *observer)
{
    tb_symbol_helper_pump_with_release(helper, emit_owned, released, observer);
    struct pollfd fds[2];
    size_t count = tb_symbol_helper_pollfds(helper, fds);
    CHECK(count <= 2 && poll(fds, (nfds_t)count, tb_symbol_helper_timeout(helper, 2)) >= 0);
}


static void release_interests(void)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    TbSymbolBudget_t storage;
    CHECK(tb_symbol_budget_init(&storage, config.frontend_bytes));
    ReleaseObserver_t observer = {
        .blocked = true
    };
    TbSymbolHelper_t *helper = tb_symbol_helper_create(&config, &storage);
    CHECK(helper && tb_symbol_helper_start(helper, production_binary));
    ready(helper, &observer.observer);
    for (uint64_t i = 1; i <= TB_SYMBOL_MAX_JOBS; ++i) {
        CHECK(submit(helper, identity(i, i), TB_SYMBOL_OPERATION_ACQUIRE,
                     TB_SYMBOL_CONSTRUCTION, true, NULL, 0) == TB_SYMBOL_ADMITTED);
    }
    CHECK(!tb_symbol_helper_attach(helper, 1, identity(1000, 1000)));
    for (unsigned i = 0; i < 4000 && observer.observer.count < TB_SYMBOL_MAX_JOBS; ++i)
        pump_owned(helper, &observer);
    CHECK(observer.observer.count == TB_SYMBOL_MAX_JOBS && observer.attempts && !observer.released);
    CHECK(tb_symbol_helper_jobs(helper) == TB_SYMBOL_MAX_JOBS);
    CHECK(submit(helper, identity(65, 65), 990, TB_SYMBOL_INTERACTIVE, false, NULL, 0) == TB_SYMBOL_BUSY);
    observer.blocked = false;
    for (unsigned i = 0; i < 4000 && observer.released < TB_SYMBOL_MAX_JOBS; ++i)
        pump_owned(helper, &observer);
    CHECK(observer.released == TB_SYMBOL_MAX_JOBS && !tb_symbol_helper_jobs(helper));
    CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_READY);
    close_helper(helper, &observer.observer);
    CHECK(!storage.used);

    observer = (ReleaseObserver_t){0};
    helper = tb_symbol_helper_create(&config, NULL);
    CHECK(helper && tb_symbol_helper_start(helper, production_binary));
    ready(helper, &observer.observer);
    CHECK(submit(helper, identity(1, 1), TB_SYMBOL_OPERATION_ACQUIRE,
                 TB_SYMBOL_CONSTRUCTION, true, NULL, 0) == TB_SYMBOL_ADMITTED);
    CHECK(tb_symbol_helper_cancel(helper, identity(1, 1)));
    for (unsigned i = 0; i < 2000 && !observer.released; ++i)
        pump_owned(helper, &observer);
    CHECK(observer.released == 1 && observer.observer.outcomes[0] == TB_SYMBOL_CANCELLED);
    CHECK(!tb_symbol_helper_jobs(helper) && !tb_symbol_helper_reaped_generation(helper));
    close_helper(helper, &observer.observer);

    observer = (ReleaseObserver_t){0};
    observer.observer.blocked = true;
    helper = tb_symbol_helper_create(&config, NULL);
    CHECK(helper && tb_symbol_helper_start(helper, production_binary));
    ready(helper, &observer.observer);
    CHECK(submit(helper, identity(1, 1), TB_SYMBOL_OPERATION_ACQUIRE,
                 TB_SYMBOL_CONSTRUCTION, true, NULL, 0) == TB_SYMBOL_ADMITTED);
    for (unsigned i = 0; i < 100; ++i)
        pump_owned(helper, &observer);
    CHECK(observer.observer.blocked_attempts && !observer.attempts);
    observer.observer.blocked = false;
    for (unsigned i = 0; i < 2000 && !observer.released; ++i)
        pump_owned(helper, &observer);
    CHECK(observer.released == 1 && observer.observer.count == 1);
    close_helper(helper, &observer.observer);

    observer = (ReleaseObserver_t){0};
    helper = fake_helper("success", &config, &observer.observer);
    CHECK(submit(helper, identity(1, 1), TB_SYMBOL_OPERATION_ACQUIRE,
                 TB_SYMBOL_CONSTRUCTION, true, NULL, 0) == TB_SYMBOL_ADMITTED);
    for (unsigned i = 0; i < 2000 && !observer.released; ++i)
        pump_owned(helper, &observer);
    CHECK(observer.released == 1 && observer.observer.count == 1);
    CHECK(observer.observer.outcomes[0] == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
    CHECK(strstr(tb_symbol_helper_error(helper), "cleanup acknowledgement"));
    CHECK(tb_symbol_helper_reaped_generation(helper) == 1);
    close_helper(helper, &observer.observer);
    CHECK(!unsetenv("TB_TEST_SYMBOL_HELPER"));

    const char *modes[] = {"settled-early", "settled-context", "settled-revision"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        observer = (ReleaseObserver_t){0};
        helper = fake_helper(modes[i], &config, &observer.observer);
        CHECK(submit(helper, identity(1, 1), TB_SYMBOL_OPERATION_ACQUIRE,
                     TB_SYMBOL_CONSTRUCTION, true, NULL, 0) == TB_SYMBOL_ADMITTED);
        for (unsigned turn = 0; turn < 2000 && !observer.released; ++turn)
            pump_owned(helper, &observer);
        CHECK(observer.released == 1 && observer.observer.count == 1);
        CHECK(observer.observer.outcomes[0] == (i ? TB_SYMBOL_SUCCESS : TB_SYMBOL_MALFORMED));
        if (i)
            CHECK(strcmp(observer.observer.values[0], "kept") == 0);
        CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
        CHECK(strstr(tb_symbol_helper_error(helper), "protocol"));
        CHECK(tb_symbol_helper_reaped_generation(helper) == 1);
        close_helper(helper, &observer.observer);
        CHECK(!unsetenv("TB_TEST_SYMBOL_HELPER"));
    }
}


static void deadline_failures(void)
{
    fake_clock = true;
    const char *modes[] = {"startup", "queue", "heartbeat"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        TbSymbolHelperConfig_t config;
        tb_symbol_helper_defaults(&config);
        config.startup_ns = i == 0 ? 1000 : UINT64_C(3000000000);
        config.queue_ns = i == 1 ? 1000 : UINT64_C(30000000000);
        config.progress_ns = i == 2 ? 1000 : UINT64_C(5000000000);
        Observer_t observer = {0};
        TbSymbolHelper_t *helper = fake_helper(modes[i], &config, &observer);
        if (i) {
            CHECK(submit(helper, identity(1, 1), 990, TB_SYMBOL_CONSTRUCTION, true, NULL, 0) ==
                  TB_SYMBOL_ADMITTED);
            if (i == 2)
                (void)serviced(helper, &observer);
        }
        clock_ns += 1000;
        pump(helper, &observer);
        CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
        CHECK(strstr(tb_symbol_helper_error(helper), i == 0 ? "startup" : i == 1 ? "queue" : "meaningful"));
        if (i) {
            results(helper, &observer, 1);
            CHECK(observer.outcomes[0] == TB_SYMBOL_OPERATIONAL_FAILURE);
        }
        close_helper(helper, &observer);
    }
    CHECK(unsetenv("TB_TEST_SYMBOL_HELPER") == 0);
    fake_clock = false;
}


static void buffered_progress(void)
{
    fake_clock = true;
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    config.progress_ns = 1000;
    Observer_t observer = {0};
    TbSymbolHelper_t *helper = fake_helper("old-progress", &config, &observer);
    CHECK(submit(helper, identity(1, 1), 990, TB_SYMBOL_CONSTRUCTION, true, NULL, 0) == TB_SYMBOL_ADMITTED);
    TbSymbolHelperTiming_t timing = serviced(helper, &observer);
    uint64_t first = timing.first_service_ns;
    clock_ns += 900;
    for (unsigned i = 0; i < 2000 && !timing.progress; ++i) {
        pump(helper, &observer);
        CHECK(tb_symbol_helper_timing(helper, 1, &timing));
    }
    CHECK(timing.progress && timing.last_progress_ns == first);
    clock_ns += 100;
    pump(helper, &observer);
    CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
    CHECK(strstr(tb_symbol_helper_error(helper), "meaningful"));
    results(helper, &observer, 1);
    close_helper(helper, &observer);
    CHECK(unsetenv("TB_TEST_SYMBOL_HELPER") == 0);
    fake_clock = false;
}


static void absolute_deadlines(void)
{
    fake_clock = true;
    for (unsigned heavy = 0; heavy < 2; ++heavy) {
        TbSymbolHelperConfig_t config;
        tb_symbol_helper_defaults(&config);
        config.ready_ns = 10000;
        config.heavy_ns = 20000;
        config.progress_ns = 5000;
        Observer_t observer = {0};
        TbSymbolHelper_t *helper = fake_helper("progress", &config, &observer);
        CHECK(submit(helper, identity(1, 1), 990, TB_SYMBOL_CONSTRUCTION, heavy != 0, NULL, 0) ==
              TB_SYMBOL_ADMITTED);
        TbSymbolHelperTiming_t timing = serviced(helper, &observer);
        uint64_t deadline = timing.first_service_ns + (heavy ? config.heavy_ns : config.ready_ns);
        CHECK(timing.operation_deadline_ns == deadline);
        while (tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_READY) {
            uint64_t progress = timing.progress;
            clock_ns += 4000;
            for (unsigned i = 0; i < 2000; ++i) {
                pump(helper, &observer);
                if (tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE)
                    break;
                CHECK(tb_symbol_helper_timing(helper, 1, &timing));
                CHECK(timing.operation_deadline_ns == deadline);
                if (timing.progress > progress && timing.last_progress_ns == clock_ns)
                    break;
                CHECK(i < 1999);
            }
        }
        CHECK(clock_ns >= deadline);
        CHECK(strstr(tb_symbol_helper_error(helper), "operation deadline"));
        results(helper, &observer, 1);
        CHECK(observer.outcomes[0] == TB_SYMBOL_OPERATIONAL_FAILURE);
        close_helper(helper, &observer);
    }
    CHECK(unsetenv("TB_TEST_SYMBOL_HELPER") == 0);
    fake_clock = false;
}


static void cancellation_escalation(void)
{
    fake_clock = true;
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    config.cancel_ns = 1000;
    config.term_ns = 1000;
    Observer_t observer = {0};
    TbSymbolHelper_t *helper = fake_helper("ignore", &config, &observer);
    CHECK(submit(helper, identity(1, 1), 990, TB_SYMBOL_CONSTRUCTION, true, NULL, 0) ==
          TB_SYMBOL_ADMITTED);
    (void)serviced(helper, &observer);
    CHECK(tb_symbol_helper_cancel(helper, identity(1, 1)));
    results(helper, &observer, 1);
    CHECK(observer.outcomes[0] == TB_SYMBOL_CANCELLED);
    TbSymbolHelperTiming_t timing;
    CHECK(tb_symbol_helper_timing(helper, 1, &timing) && timing.retiring);
    CHECK(timing.cancel_deadline_ns == clock_ns + config.cancel_ns);
    clock_ns += config.cancel_ns;
    pump(helper, &observer);
    CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
    CHECK(strstr(tb_symbol_helper_error(helper), "cancellation"));
    CHECK(tb_symbol_helper_jobs(helper) == 1 && !tb_symbol_helper_start(helper, NULL));
    pump(helper, &observer);
    clock_ns += config.term_ns - 1;
    pump(helper, &observer);
    CHECK(tb_symbol_helper_jobs(helper) == 1 && !tb_symbol_helper_start(helper, NULL));
    clock_ns++;
    pump(helper, &observer);
    struct pollfd fds[2];
    for (unsigned i = 0; i < 2000 && tb_symbol_helper_pollfds(helper, fds); ++i)
        pump(helper, &observer);
    CHECK(!tb_symbol_helper_pollfds(helper, fds) && !tb_symbol_helper_jobs(helper));
    CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
    CHECK(unsetenv("TB_TEST_SYMBOL_HELPER") == 0);
    fake_clock = false;
    CHECK(tb_symbol_helper_start(helper, NULL));
    ready(helper, &observer);
    CHECK(submit(helper, identity(2, 1), 990, TB_SYMBOL_INTERACTIVE, false, NULL, 0) ==
          TB_SYMBOL_ADMITTED);
    results(helper, &observer, 2);
    CHECK(observer.outcomes[1] == TB_SYMBOL_UNSUPPORTED);
    close_helper(helper, &observer);
    fake_clock = false;
}


static void protocol_identity(void)
{
    const char *modes[] = {"revision", "generation", "future-time"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        TbSymbolHelperConfig_t config;
        tb_symbol_helper_defaults(&config);
        Observer_t observer = {0};
        TbSymbolHelper_t *helper = fake_helper(modes[i], &config, &observer);
        CHECK(submit(helper, identity(1, 1), 990, TB_SYMBOL_CONSTRUCTION, true, NULL, 0) ==
              TB_SYMBOL_ADMITTED);
        results(helper, &observer, 1);
        CHECK(observer.outcomes[0] == TB_SYMBOL_MALFORMED);
        CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
        close_helper(helper, &observer);
    }
    CHECK(unsetenv("TB_TEST_SYMBOL_HELPER") == 0);
}


static void configuration(void)
{
    const char *names[] = {
        "TB_SYMBOL_FRONTEND_MIB", "TB_SYMBOL_IPC_MIB", "TB_SYMBOL_JOBS", "TB_SYMBOL_INTERESTS",
        "TB_SYMBOL_CHECKPOINT_BYTES", "TB_SYMBOL_CHECKPOINT_STEPS",
        "TB_SYMBOL_SLICE_MS", "TB_SYMBOL_STARTUP_MS", "TB_SYMBOL_QUEUE_MS", "TB_SYMBOL_READY_MS",
        "TB_SYMBOL_HEAVY_MS", "TB_SYMBOL_PROGRESS_MS", "TB_SYMBOL_CANCEL_MS", "TB_SYMBOL_TERM_MS"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        CHECK(unsetenv(names[i]) == 0);
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    unsigned char before[sizeof(config)];
    memcpy(before, &config, sizeof(before));
    const char *invalid[] = {"", "0", "-1", "+1", " 1", "1 ", "1x", "18446744073709551616"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(setenv("TB_SYMBOL_JOBS", invalid[i], 1) == 0);
        CHECK(!tb_symbol_helper_environment(&config));
        CHECK(memcmp(before, &config, sizeof(before)) == 0);
    }
    CHECK(setenv("TB_SYMBOL_JOBS", "65", 1) == 0);
    CHECK(!tb_symbol_helper_environment(&config));
    CHECK(unsetenv("TB_SYMBOL_JOBS") == 0);
    CHECK(setenv("TB_SYMBOL_READY_MS", "18446744073710", 1) == 0);
    CHECK(!tb_symbol_helper_environment(&config));
    CHECK(unsetenv("TB_SYMBOL_READY_MS") == 0);
    CHECK(setenv("TB_SYMBOL_IPC_MIB", "129", 1) == 0);
    CHECK(!tb_symbol_helper_environment(&config));
    CHECK(setenv("TB_SYMBOL_IPC_MIB", "2", 1) == 0);
    CHECK(setenv("TB_SYMBOL_FRONTEND_MIB", "16", 1) == 0);
    CHECK(setenv("TB_SYMBOL_JOBS", "32", 1) == 0);
    CHECK(setenv("TB_SYMBOL_INTERESTS", "64", 1) == 0);
    CHECK(setenv("TB_SYMBOL_CHECKPOINT_BYTES", "1024", 1) == 0);
    CHECK(setenv("TB_SYMBOL_CHECKPOINT_STEPS", "32", 1) == 0);
    CHECK(setenv("TB_SYMBOL_SLICE_MS", "3", 1) == 0);
    CHECK(setenv("TB_SYMBOL_STARTUP_MS", "17", 1) == 0);
    CHECK(tb_symbol_helper_environment(&config));
    CHECK(config.frontend_bytes == 16 * 1024 * 1024 && config.ipc_bytes == 2 * 1024 * 1024);
    CHECK(config.max_jobs == 32 && config.max_interests == 64);
    CHECK(config.slice_ns == 3000000 && config.startup_ns == 17000000);
    CHECK(config.checkpoint_bytes == 1024 && config.checkpoint_steps == 32);
    CHECK(setenv("TB_SYMBOL_CHECKPOINT_BYTES", "63", 1) == 0);
    CHECK(!tb_symbol_helper_environment(&config));
    CHECK(setenv("TB_SYMBOL_CHECKPOINT_BYTES", "1024", 1) == 0);
    CHECK(setenv("TB_SYMBOL_CHECKPOINT_STEPS", "257", 1) == 0);
    CHECK(!tb_symbol_helper_environment(&config));
    CHECK(setenv("TB_SYMBOL_CHECKPOINT_BYTES", "64", 1) == 0);
    CHECK(setenv("TB_SYMBOL_CHECKPOINT_STEPS", "1", 1) == 0);
    CHECK(tb_symbol_helper_environment(&config));
    CHECK(config.checkpoint_bytes == 64 && config.checkpoint_steps == 1);
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        CHECK(unsetenv(names[i]) == 0);
    config.startup_ns = UINT64_C(3000000000);
    TbSymbolHelper_t *helper = tb_symbol_helper_create(&config, NULL);
    CHECK(helper && tb_symbol_helper_start(helper, production_binary));
    Observer_t observer = {0};
    ready(helper, &observer);
    CHECK(submit(helper, identity(1, 1), 999, TB_SYMBOL_INTERACTIVE, false, NULL, 0) == TB_SYMBOL_ADMITTED);
    results(helper, &observer, 1);
    CHECK(observer.outcomes[0] == TB_SYMBOL_UNSUPPORTED);
    close_helper(helper, &observer);
}


static void crash_retention(void)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    Observer_t observer = {
        .blocked = true
    };
    TbSymbolHelper_t *helper = fake_helper("crash", &config, &observer);
    CHECK(submit(helper, identity(1, 1), 990, TB_SYMBOL_INTERACTIVE, false, NULL, 0) == TB_SYMBOL_ADMITTED);
    for (unsigned i = 0; i < 2000 && !observer.blocked_attempts; ++i)
        pump(helper, &observer);
    CHECK(observer.blocked_attempts && !observer.count);
    CHECK(submit(helper, identity(2, 2), 990, TB_SYMBOL_INTERACTIVE, false, NULL, 0) == TB_SYMBOL_ADMITTED);
    for (unsigned i = 0; i < 2000 && tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_READY; ++i)
        pump(helper, &observer);
    CHECK(tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
    observer.blocked = false;
    results(helper, &observer, 2);
    bool kept = false;
    bool failed = false;
    for (size_t i = 0; i < observer.count; ++i) {
        if (observer.identities[i].request == 1) {
            CHECK(observer.outcomes[i] == TB_SYMBOL_SUCCESS && strcmp(observer.values[i], "kept") == 0);
            kept = true;
        } else {
            CHECK(observer.identities[i].request == 2 &&
                  observer.outcomes[i] == TB_SYMBOL_OPERATIONAL_FAILURE);
            failed = true;
        }
    }
    CHECK(kept && failed);
    close_helper(helper, &observer);
    CHECK(unsetenv("TB_TEST_SYMBOL_HELPER") == 0);
}


static void result_allocation(void)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, config.frontend_bytes));
    CHECK(setenv("TB_TEST_SYMBOL_HELPER", "success", 1) == 0);
    TbSymbolHelper_t *helper = tb_symbol_helper_create(&config, &budget);
    CHECK(helper && tb_symbol_helper_start(helper, NULL));
    Observer_t observer = {0};
    ready(helper, &observer);
    size_t baseline = budget.used;
    CHECK(submit(helper, identity(1, 1), 990, TB_SYMBOL_INTERACTIVE, false, NULL, 0) == TB_SYMBOL_ADMITTED);
    fail_malloc_size = 4;
    results(helper, &observer, 1);
    fail_malloc_size = 0;
    CHECK(observer.outcomes[0] == TB_SYMBOL_RESOURCE_FAILURE && !observer.values[0][0]);
    CHECK(budget.used == baseline);
    close_helper(helper, &observer);
    CHECK(!budget.used && unsetenv("TB_TEST_SYMBOL_HELPER") == 0);
}


typedef struct {
    TbSymbolWorkIdentity_t identity;
    unsigned              phase;
    uint64_t              progress;
    uint64_t              start_ns;
    bool                  used;
} PeerJob_t;

static int fake_peer(const char *mode)
{
    if (fcntl(3, F_SETFD, FD_CLOEXEC))
        return 60;
    struct sigaction ignore = {0};
    ignore.sa_handler = SIG_IGN;
    (void)sigemptyset(&ignore.sa_mask);
    if (sigaction(SIGTERM, &ignore, NULL))
        return 61;
    TbSymbolWire_t *wire = tb_symbol_wire_create(3, TB_SYMBOL_WIRE_DEFAULT_MAX, NULL);
    if (!wire)
        return 62;
    const char *clock_source = getenv("TB_TEST_SYMBOL_CLOCK_FILE");
    int clock_fd = clock_source ? open(clock_source, O_RDONLY | O_CLOEXEC) : -1;
    if (clock_source && clock_fd < 0)
        return 71;
    uint64_t generation = 0;
    PeerJob_t jobs[64] = {0};
    bool ready_pending = false;
    for (;;) {
        TbSymbolWireStatus_e status = tb_symbol_wire_pump(wire);
        if (status == TB_SYMBOL_WIRE_EOF) {
            if (strcmp(mode, "ignore") == 0 || strcmp(mode, "startup") == 0) {
                for (;;)
                    (void)pause();
            }
            break;
        }
        if (status != TB_SYMBOL_WIRE_OK && status != TB_SYMBOL_WIRE_MESSAGE)
            return 63;
        if (status == TB_SYMBOL_WIRE_MESSAGE) {
            const TbSymbolMessage_t *message = tb_symbol_wire_message(wire);
            if (!generation) {
                if (message->kind != TB_SYMBOL_MESSAGE_HELLO || !message->generation)
                    return 64;
                generation = message->generation;
                ready_pending = strcmp(mode, "startup") != 0;
            } else if (message->kind == TB_SYMBOL_MESSAGE_REQUEST) {
                if (message->length < TB_SYMBOL_REQUEST_BYTES)
                    return 65;
                if (strcmp(mode, "crash") == 0 && message->request > 1)
                    _exit(42);
                PeerJob_t *job = NULL;
                for (size_t i = 0; i < 64; ++i) {
                    if (!jobs[i].used) {
                        job = &jobs[i];
                        break;
                    }
                }
                if (!job)
                    return 66;
                *job = (PeerJob_t){
                    .identity = {
                        .request    = message->request,
                        .consumer   = message->consumer,
                        .revision   = message->revision,
                        .generation = tb_symbol_protocol_get(message->payload + 8, 8)
                    },
                    .phase = strcmp(mode, "queue") == 0 ? 3 : 1,
                    .used = true
                };
            } else if (message->kind == TB_SYMBOL_MESSAGE_CANCEL) {
                if (strcmp(mode, "ignore") != 0) {
                    for (size_t i = 0; i < 64; ++i) {
                        if (jobs[i].used && jobs[i].identity.request == message->request)
                            jobs[i].phase = 4;
                    }
                }
            } else if (message->kind == TB_SYMBOL_MESSAGE_SHUTDOWN) {
                if (strcmp(mode, "ignore") != 0 && strcmp(mode, "startup") != 0)
                    break;
            } else {
                return 67;
            }
            tb_symbol_wire_consume(wire);
        }
        if (!tb_symbol_wire_sending(wire) && ready_pending) {
            TbSymbolMessage_t message = {
                .kind       = TB_SYMBOL_MESSAGE_READY,
                .generation = generation,
                .descriptor = -1
            };
            if (!tb_symbol_wire_send(wire, &message))
                return 68;
            ready_pending = false;
        } else if (!tb_symbol_wire_sending(wire)) {
            for (size_t i = 0; i < 64; ++i) {
                PeerJob_t *job = &jobs[i];
                if (!job->used)
                    continue;
                unsigned char bytes[TB_SYMBOL_PROGRESS_BYTES] = {0};
                TbSymbolMessageKind_e kind = TB_SYMBOL_MESSAGE_PROGRESS;
                size_t length = TB_SYMBOL_PROGRESS_BYTES;
                TbSymbolWorkIdentity_t captured = job->identity;
                if (job->phase == 1) {
                    tb_symbol_protocol_put(bytes, TB_SYMBOL_PROGRESS_START, 4);
                    job->phase = strcmp(mode, "crash") == 0 || strcmp(mode, "success") == 0 ||
                                 strncmp(mode, "settled-", 8) == 0 ? 2 : 3;
                    if (strcmp(mode, "revision") == 0)
                        ++captured.revision;
                } else if (job->phase == 2 || job->phase == 4) {
                    kind = TB_SYMBOL_MESSAGE_RESULT;
                    tb_symbol_protocol_put(bytes,
                                           job->phase == 4 ? TB_SYMBOL_CANCELLED : TB_SYMBOL_SUCCESS, 4);
                    length = TB_SYMBOL_RESULT_BYTES;
                    if (job->phase == 2) {
                        memcpy(bytes + TB_SYMBOL_RESULT_BYTES, "kept", 4);
                        length += 4;
                    }
                    if (strcmp(mode, "settled-early") == 0) {
                        kind = TB_SYMBOL_MESSAGE_SETTLED;
                        length = TB_SYMBOL_SETTLED_BYTES;
                        job->used = false;
                    } else if (strncmp(mode, "settled-", 8) == 0) {
                        job->phase = 5;
                    } else {
                        job->used = false;
                    }
                } else if (job->phase == 5) {
                    kind = TB_SYMBOL_MESSAGE_SETTLED;
                    length = TB_SYMBOL_SETTLED_BYTES;
                    job->used = false;
                    if (strcmp(mode, "settled-revision") == 0)
                        ++captured.revision;
                } else if (strcmp(mode, "heartbeat") == 0 || strcmp(mode, "progress") == 0 ||
                           strcmp(mode, "old-progress") == 0) {
                    tb_symbol_protocol_put(bytes, TB_SYMBOL_PROGRESS_ADVANCE, 4);
                    if (strcmp(mode, "heartbeat") != 0)
                        ++job->progress;
                    tb_symbol_protocol_put(bytes + 16, job->progress, 8);
                } else {
                    continue;
                }
                if (kind == TB_SYMBOL_MESSAGE_SETTLED) {
                    tb_symbol_protocol_put(bytes, captured.generation +
                                           (strcmp(mode, "settled-context") == 0 ? 1U : 0U), 8);
                } else {
                    tb_symbol_protocol_put(bytes + 8, captured.generation, 8);
                }
                if (kind == TB_SYMBOL_MESSAGE_PROGRESS) {
                    uint64_t event_ns;
                    if (clock_fd >= 0) {
                        if (flock(clock_fd, LOCK_SH) ||
                            pread(clock_fd, &event_ns, sizeof(event_ns), 0) != (ssize_t)sizeof(event_ns) ||
                            flock(clock_fd, LOCK_UN)) {
                            return 72;
                        }
                    } else {
                        struct timespec time;
                        if (clock_gettime(CLOCK_MONOTONIC, &time))
                            return 73;
                        event_ns = (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
                    }
                    if (tb_symbol_protocol_get(bytes, 4) == TB_SYMBOL_PROGRESS_START)
                        job->start_ns = event_ns;
                    if (strcmp(mode, "old-progress") == 0)
                        event_ns = job->start_ns;
                    if (strcmp(mode, "future-time") == 0)
                        event_ns += UINT64_C(1000000000000);
                    tb_symbol_protocol_put(bytes + 24, event_ns, 8);
                }
                TbSymbolMessage_t message = tb_symbol_protocol_message(kind, captured,
                                                                       generation, bytes, length);
                if (strcmp(mode, "generation") == 0)
                    ++message.generation;
                if (!tb_symbol_wire_send(wire, &message))
                    return 69;
                break;
            }
        }
        struct pollfd fd = tb_symbol_wire_pollfd(wire);
        if (poll(&fd, 1, 2) < 0 && errno != EINTR)
            return 70;
        if (strcmp(mode, "heartbeat") == 0 || strcmp(mode, "progress") == 0 ||
            strcmp(mode, "old-progress") == 0)
            (void)poll(NULL, 0, 1);
    }
    if (clock_fd >= 0)
        (void)close(clock_fd);
    tb_symbol_wire_destroy(wire);
    return 0;
}


int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "--symbol-worker") == 0) {
        const char *mode = getenv("TB_TEST_SYMBOL_HELPER");
        return mode ? fake_peer(mode) : tb_symbol_worker_main(3);
    }
    CHECK(argc == 1 || argc == 2);
    production_binary = argc == 2 ? argv[1] : NULL;
    configuration();
    reap_evidence();
    release_interests();
    real_worker();
    saturation();
    allocation_failures();
    deadline_failures();
    buffered_progress();
    absolute_deadlines();
    cancellation_escalation();
    protocol_identity();
    crash_retention();
    result_allocation();
    if (clock_file >= 0)
        CHECK(close(clock_file) == 0 && unlink(clock_path) == 0);
    puts("PASS symbol helper: real worker handshake, exact interests, "
         "priority ordering, saturation and faults");
    return 0;
}
