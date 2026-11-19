#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Atomic socket flags. */
#include "symbol/helper.h"
#include "symbol/process.h"
#include "symbol/worker.h"
#include "symbol/protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    TbSymbolHelper_t      *owner;
    TbSymbolWorkIdentity_t identity;
    unsigned char         *buffer;
    size_t                 length;
    uint64_t               generation;
    uint64_t               admitted_ns;
    uint64_t               first_service_ns;
    uint64_t               progress_ns;
    uint64_t               progress;
    uint64_t               cancel_deadline_ns;
    TbSymbolOutcome_e      outcome;
    int                    descriptor;
    bool                   used;
    bool                   scheduler_owned;
    bool                   sent;
    bool                   serviced;
    bool                   heavy;
    bool                   complete;
    bool                   cancel_pending;
} HelperJob_t;

struct tb_symbol_helper {
    TbSymbolHelperConfig_t config;
    TbSymbolBudget_t       storage;
    TbSymbolBudget_t       ipc;
    TbSymbolProcess_t      process;
    TbSymbolWire_t        *wire;
    TbSymbolSchedule_t    *schedule;
    HelperJob_t            jobs[TB_SYMBOL_MAX_JOBS];
    TbSymbolHelperState_e  state;
    uint64_t               generation;
    uint64_t               last_request;
    uint64_t               sequence;
    uint64_t               now_ns;
    uint64_t               startup_deadline_ns;
    bool                   shutdown_pending;
    bool                   ready_received;
    const char            *error;
};

static uint64_t after(uint64_t now, uint64_t delay)
{
    return delay > UINT64_MAX - now ? UINT64_MAX : now + delay;
}


static bool now_ns(uint64_t *now)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) || time.tv_sec < 0 ||
        time.tv_nsec < 0 || time.tv_nsec >= 1000000000 ||
        (uint64_t)time.tv_sec > (UINT64_MAX - (uint64_t)time.tv_nsec) / UINT64_C(1000000000)) {
        return false;
    }
    *now = (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
    return true;
}


void tb_symbol_helper_defaults(TbSymbolHelperConfig_t *config)
{
    if (!config)
        return;
    *config = (TbSymbolHelperConfig_t){
        .frontend_bytes   = (size_t)128 * 1024 * 1024,
        .ipc_bytes        = TB_SYMBOL_WIRE_DEFAULT_MAX,
        .max_jobs         = TB_SYMBOL_MAX_JOBS,
        .max_interests    = TB_SYMBOL_MAX_INTERESTS,
        .checkpoint_bytes = TB_SYMBOL_WIRE_IO_BYTES,
        .checkpoint_steps = TB_SYMBOL_WIRE_IO_STEPS,
        .slice_ns         = UINT64_C(5000000),
        .startup_ns       = UINT64_C(3000000000),
        .queue_ns         = UINT64_C(30000000000),
        .ready_ns         = UINT64_C(5000000000),
        .heavy_ns         = UINT64_C(120000000000),
        .progress_ns      = UINT64_C(5000000000),
        .cancel_ns        = UINT64_C(250000000),
        .term_ns          = UINT64_C(250000000)
    };
}


static bool environment_value(const char *name, uint64_t scale, uint64_t *value)
{
    const char *text = getenv(name);
    if (!text)
        return true;
    uint64_t number = 0;
    if (!*text)
        return false;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9')
            return false;
        unsigned digit = (unsigned)(*text - '0');
        if (number > (UINT64_MAX - digit) / 10)
            return false;
        number = number * 10 + digit;
    }
    if (!number || number > UINT64_MAX / scale)
        return false;
    *value = number * scale;
    return true;
}


static bool environment_size(const char *name, uint64_t scale, size_t *value)
{
    uint64_t number = *value;
    if (!environment_value(name, scale, &number) || number > SIZE_MAX)
        return false;
    *value = (size_t)number;
    return true;
}


bool tb_symbol_helper_environment(TbSymbolHelperConfig_t *config)
{
    if (!config)
        return false;
    TbSymbolHelperConfig_t candidate = *config;
    uint64_t steps = candidate.checkpoint_steps;
    if (!environment_size("TB_SYMBOL_FRONTEND_MIB", UINT64_C(1048576), &candidate.frontend_bytes) ||
        !environment_size("TB_SYMBOL_IPC_MIB", UINT64_C(1048576), &candidate.ipc_bytes) ||
        !environment_size("TB_SYMBOL_JOBS", 1, &candidate.max_jobs) ||
        !environment_size("TB_SYMBOL_INTERESTS", 1, &candidate.max_interests) ||
        !environment_size("TB_SYMBOL_CHECKPOINT_BYTES", 1, &candidate.checkpoint_bytes) ||
        !environment_value("TB_SYMBOL_CHECKPOINT_STEPS", 1, &steps) || steps > TB_SYMBOL_WIRE_IO_STEPS ||
        !environment_value("TB_SYMBOL_SLICE_MS", 1000000, &candidate.slice_ns) ||
        !environment_value("TB_SYMBOL_STARTUP_MS", 1000000, &candidate.startup_ns) ||
        !environment_value("TB_SYMBOL_QUEUE_MS", 1000000, &candidate.queue_ns) ||
        !environment_value("TB_SYMBOL_READY_MS", 1000000, &candidate.ready_ns) ||
        !environment_value("TB_SYMBOL_HEAVY_MS", 1000000, &candidate.heavy_ns) ||
        !environment_value("TB_SYMBOL_PROGRESS_MS", 1000000, &candidate.progress_ns) ||
        !environment_value("TB_SYMBOL_CANCEL_MS", 1000000, &candidate.cancel_ns) ||
        !environment_value("TB_SYMBOL_TERM_MS", 1000000, &candidate.term_ns)) {
        return false;
    }
    candidate.checkpoint_steps = (unsigned)steps;
    if (!tb_symbol_helper_config_valid(&candidate))
        return false;
    *config = candidate;
    return true;
}


bool tb_symbol_helper_config_valid(const TbSymbolHelperConfig_t *config)
{
    return config && config->frontend_bytes >= sizeof(TbSymbolHelper_t) &&
           config->ipc_bytes >= tb_symbol_worker_minimum_ipc() &&
           config->ipc_bytes <= config->frontend_bytes && config->max_jobs &&
           config->max_jobs <= TB_SYMBOL_MAX_JOBS && config->max_interests &&
           config->max_interests <= TB_SYMBOL_MAX_INTERESTS &&
           config->checkpoint_bytes >= 64 && config->checkpoint_bytes <= TB_SYMBOL_WIRE_IO_BYTES &&
           config->checkpoint_steps && config->checkpoint_steps <= TB_SYMBOL_WIRE_IO_STEPS &&
           config->slice_ns &&
           config->startup_ns && config->queue_ns && config->ready_ns && config->heavy_ns &&
           config->progress_ns && config->cancel_ns && config->term_ns;
}


TbSymbolHelper_t *tb_symbol_helper_create(const TbSymbolHelperConfig_t *config, TbSymbolBudget_t *aggregate)
{
    if (!tb_symbol_helper_config_valid(config))
        return NULL;
    TbSymbolBudget_t storage;
    bool initialized = aggregate ? tb_symbol_budget_init_child(&storage, config->frontend_bytes, aggregate)
                                 : tb_symbol_budget_init(&storage, config->frontend_bytes);
    if (!initialized || !tb_symbol_budget_reserve(&storage, sizeof(TbSymbolHelper_t)))
        return NULL;
    TbSymbolHelper_t *helper = calloc(1, sizeof(*helper));
    if (!helper) {
        (void)tb_symbol_budget_release(&storage, sizeof(TbSymbolHelper_t));
        return NULL;
    }
    helper->storage = storage;
    helper->config  = *config;
    helper->error   = "";
    if (!tb_symbol_budget_init_child(&helper->ipc, config->ipc_bytes, &helper->storage) ||
        !tb_symbol_process_init(&helper->process, config->cancel_ns, config->term_ns)) {
        free(helper);
        (void)tb_symbol_budget_release(&storage, sizeof(TbSymbolHelper_t));
        return NULL;
    }
    helper->schedule = tb_symbol_schedule_create(config->max_jobs, config->max_interests, &helper->storage);
    if (!helper->schedule) {
        free(helper);
        (void)tb_symbol_budget_release(&storage, sizeof(TbSymbolHelper_t));
        return NULL;
    }
    return helper;
}


static void buffer_free(HelperJob_t *job)
{
    if (job->buffer) {
        free(job->buffer);
        (void)tb_symbol_budget_release(&job->owner->ipc, job->length);
        job->buffer = NULL;
        job->length = 0;
    }
}


static void job_free(HelperJob_t *job)
{
    buffer_free(job);
    if (job->descriptor >= 0)
        (void)close(job->descriptor);
    *job = (HelperJob_t){0};
}


static void job_destroy(void *context)
{
    HelperJob_t *job = context;
    job->scheduler_owned = false;
    if (!job->sent || job->complete) {
        job_free(job);
    } else {
        job->cancel_pending = true;
        job->cancel_deadline_ns = after(job->owner->now_ns, job->owner->config.cancel_ns);
    }
}


static void fail(TbSymbolHelper_t *helper, TbSymbolOutcome_e outcome, const char *reason)
{
    if (helper->wire) {
        tb_symbol_wire_destroy(helper->wire);
        helper->wire = NULL;
    }
    helper->error = reason;
    helper->state = TB_SYMBOL_HELPER_UNAVAILABLE;
    tb_symbol_process_stop(&helper->process, helper->now_ns);
    for (size_t i = 0; i < TB_SYMBOL_MAX_JOBS; ++i) {
        HelperJob_t *job = &helper->jobs[i];
        if (!job->used || job->complete)
            continue;
        buffer_free(job);
        job->outcome = outcome;
        job->complete = true;
        job->cancel_pending = false;
        /* A cancelled remote job remains counted until actual reap. */
        if (!job->scheduler_owned && tb_symbol_process_released(&helper->process))
            job_free(job);
    }
}


bool tb_symbol_helper_start(TbSymbolHelper_t *helper, const char *path)
{
    if (!helper || (helper->state != TB_SYMBOL_HELPER_IDLE &&
                    helper->state != TB_SYMBOL_HELPER_UNAVAILABLE) ||
        !tb_symbol_process_released(&helper->process) || helper->generation == UINT64_MAX) {
        return false;
    }
    if (!now_ns(&helper->now_ns)) {
        helper->error = "Symbol helper monotonic clock unavailable";
        helper->state = TB_SYMBOL_HELPER_UNAVAILABLE;
        return false;
    }
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets)) {
        helper->error = "Symbol helper channel creation failed";
        helper->state = TB_SYMBOL_HELPER_UNAVAILABLE;
        return false;
    }
    helper->wire = tb_symbol_wire_create(sockets[0], helper->config.ipc_bytes, &helper->ipc);
    if (!helper->wire) {
        (void)close(sockets[0]);
        (void)close(sockets[1]);
        helper->error = "Symbol helper channel allocation failed";
        helper->state = TB_SYMBOL_HELPER_UNAVAILABLE;
        return false;
    }
    bool started = tb_symbol_process_start(&helper->process, path ? path : "/proc/self/exe", sockets[1]);
    (void)close(sockets[1]);
    if (!started) {
        fail(helper, TB_SYMBOL_OPERATIONAL_FAILURE, "Symbol helper spawn or process identity failed");
        return false;
    }
    ++helper->generation;
    helper->state = TB_SYMBOL_HELPER_STARTING;
    helper->ready_received = false;
    helper->shutdown_pending = false;
    helper->startup_deadline_ns = after(helper->now_ns, helper->config.startup_ns);
    helper->error = "";
    unsigned char bytes[TB_SYMBOL_HELLO_BYTES] = {0};
    tb_symbol_protocol_put(bytes, helper->config.max_jobs, 4);
    tb_symbol_protocol_put(bytes + 4, helper->config.max_interests, 4);
    tb_symbol_protocol_put(bytes + 8, helper->config.slice_ns, 8);
    tb_symbol_protocol_put(bytes + 16, helper->config.ipc_bytes, 8);
    tb_symbol_protocol_put(bytes + 24, helper->config.checkpoint_bytes, 4);
    tb_symbol_protocol_put(bytes + 28, helper->config.checkpoint_steps, 4);
    TbSymbolMessage_t message = {
        .kind       = TB_SYMBOL_MESSAGE_HELLO,
        .generation = helper->generation,
        .payload    = bytes,
        .length     = sizeof(bytes),
        .descriptor = -1
    };
    if (!tb_symbol_wire_send(helper->wire, &message)) {
        fail(helper, TB_SYMBOL_RESOURCE_FAILURE, "Symbol helper handshake allocation failed");
        return false;
    }
    return true;
}


static TbSymbolWorkResult_t job_step(void *context, uint64_t deadline_ns,
                                    size_t max_bytes, unsigned max_steps)
{
    uint64_t current;
    if (!now_ns(&current)) {
        return (TbSymbolWorkResult_t){
            .outcome = TB_SYMBOL_OPERATIONAL_FAILURE
        };
    }
    if (current >= deadline_ns || max_bytes < TB_SYMBOL_REQUEST_BYTES || !max_steps)
        return (TbSymbolWorkResult_t){0};
    HelperJob_t *job = context;
    if (job->complete) {
        return (TbSymbolWorkResult_t){
            .outcome  = job->outcome,
            .progress = job->progress
        };
    }
    TbSymbolHelper_t *helper = job->owner;
    if (!job->sent && helper->state == TB_SYMBOL_HELPER_READY &&
        !tb_symbol_wire_sending(helper->wire)) {
        TbSymbolMessage_t message = tb_symbol_protocol_message(TB_SYMBOL_MESSAGE_REQUEST, job->identity,
                                                               job->generation, job->buffer, job->length);
        if (helper->sequence == UINT64_MAX) {
            buffer_free(job);
            job->complete = true;
            job->outcome = TB_SYMBOL_RESOURCE_FAILURE;
        } else {
            tb_symbol_protocol_put(job->buffer + 16, ++helper->sequence, 8);
            if (tb_symbol_wire_send(helper->wire, &message)) {
                buffer_free(job);
                job->sent = true;
            } else {
                buffer_free(job);
                job->complete = true;
                job->outcome = TB_SYMBOL_RESOURCE_FAILURE;
            }
        }
    }
    return (TbSymbolWorkResult_t){
        .outcome  = job->complete ? job->outcome : TB_SYMBOL_PENDING,
        .progress = job->progress
    };
}


TbSymbolAdmission_e tb_symbol_helper_submit(TbSymbolHelper_t *helper, const TbSymbolHelperRequest_t *request)
{
    if (!request)
        return TB_SYMBOL_INVALID;
    TbSymbolWorkIdentity_t identity = request->identity;
    uint32_t              operation = request->operation;
    TbSymbolPriority_e    priority  = request->priority;
    bool                  heavy     = request->heavy;
    const void           *arguments = request->arguments;
    size_t                length    = request->length;
    if (!helper || !identity.request || !identity.consumer || !identity.generation ||
        identity.request <= helper->last_request ||
        (priority != TB_SYMBOL_INTERACTIVE && priority != TB_SYMBOL_CONSTRUCTION) ||
        (length && !arguments) || length > TB_SYMBOL_WIRE_MAX_PAYLOAD - TB_SYMBOL_REQUEST_BYTES) {
        return TB_SYMBOL_INVALID;
    }
    if (helper->state != TB_SYMBOL_HELPER_READY && helper->state != TB_SYMBOL_HELPER_STARTING)
        return TB_SYMBOL_UNAVAILABLE;
    HelperJob_t *job = NULL;
    for (size_t i = 0; i < helper->config.max_jobs; ++i) {
        if (!helper->jobs[i].used) {
            job = &helper->jobs[i];
            break;
        }
    }
    if (!job)
        return TB_SYMBOL_BUSY;
    size_t bytes = length + TB_SYMBOL_REQUEST_BYTES;
    if (!tb_symbol_budget_reserve(&helper->ipc, bytes))
        return TB_SYMBOL_NO_RESOURCE;
    unsigned char *buffer = malloc(bytes);
    if (!buffer) {
        (void)tb_symbol_budget_release(&helper->ipc, bytes);
        return TB_SYMBOL_NO_RESOURCE;
    }
    memset(buffer, 0, TB_SYMBOL_REQUEST_BYTES);
    tb_symbol_protocol_put(buffer, operation, 4);
    buffer[4] = (unsigned char)priority;
    buffer[5] = heavy ? 1 : 0;
    tb_symbol_protocol_put(buffer + 8, identity.generation, 8);
    if (length)
        memcpy(buffer + TB_SYMBOL_REQUEST_BYTES, arguments, length);
    if (!now_ns(&helper->now_ns)) {
        free(buffer);
        (void)tb_symbol_budget_release(&helper->ipc, bytes);
        return TB_SYMBOL_UNAVAILABLE;
    }
    *job = (HelperJob_t){
        .owner           = helper,
        .identity        = identity,
        .buffer          = buffer,
        .length          = bytes,
        .generation      = helper->generation,
        .admitted_ns     = helper->now_ns,
        .descriptor      = -1,
        .used            = true,
        .scheduler_owned = true,
        .heavy           = heavy
    };
    TbSymbolWork_t work = {
        .identity = identity,
        .priority = priority,
        .step     = job_step,
        .destroy  = job_destroy,
        .context  = job
    };
    if (!tb_symbol_schedule_add(helper->schedule, &work)) {
        job_free(job);
        return TB_SYMBOL_BUSY;
    }
    helper->last_request = identity.request;
    return TB_SYMBOL_ADMITTED;
}


bool tb_symbol_helper_attach(TbSymbolHelper_t *helper, uint64_t request, TbSymbolWorkIdentity_t identity)
{
    if (!helper || (helper->state != TB_SYMBOL_HELPER_READY &&
                    helper->state != TB_SYMBOL_HELPER_STARTING) ||
        identity.request <= helper->last_request) {
        return false;
    }
    if (!tb_symbol_schedule_attach(helper->schedule, request, identity))
        return false;
    helper->last_request = identity.request;
    return true;
}


bool tb_symbol_helper_cancel(TbSymbolHelper_t *helper, TbSymbolWorkIdentity_t identity)
{
    return helper && now_ns(&helper->now_ns) && tb_symbol_schedule_cancel(helper->schedule, identity);
}


static bool receive(TbSymbolHelper_t *helper, const TbSymbolMessage_t *message)
{
    if (message->generation != helper->generation)
        return false;
    if (!helper->ready_received) {
        if (message->kind != TB_SYMBOL_MESSAGE_READY || message->request || message->consumer ||
            message->revision || message->length || message->descriptor >= 0) {
            return false;
        }
        helper->ready_received = true;
        if (helper->state == TB_SYMBOL_HELPER_STARTING)
            helper->state = TB_SYMBOL_HELPER_READY;
        return true;
    }
    if (message->kind != TB_SYMBOL_MESSAGE_PROGRESS && message->kind != TB_SYMBOL_MESSAGE_RESULT)
        return false;
    HelperJob_t *job = NULL;
    for (size_t i = 0; i < helper->config.max_jobs; ++i) {
        if (helper->jobs[i].used && helper->jobs[i].identity.request == message->request) {
            job = &helper->jobs[i];
            break;
        }
    }
    if (!job)
        return message->request && message->request <= helper->last_request;
    if (!job->sent || job->complete || message->consumer != job->identity.consumer ||
        message->revision != job->identity.revision || job->generation != message->generation) {
        return false;
    }
    if (message->kind == TB_SYMBOL_MESSAGE_PROGRESS) {
        if (message->length != TB_SYMBOL_PROGRESS_BYTES || message->descriptor >= 0 ||
            tb_symbol_protocol_get(message->payload + 4, 4) ||
            tb_symbol_protocol_get(message->payload + 8, 8) != job->identity.generation) {
            return false;
        }
        uint64_t phase = tb_symbol_protocol_get(message->payload, 4);
        uint64_t progress = tb_symbol_protocol_get(message->payload + 16, 8);
        uint64_t event_ns = tb_symbol_protocol_get(message->payload + 24, 8);
        if (event_ns < job->admitted_ns || event_ns > helper->now_ns)
            return false;
        if (phase == TB_SYMBOL_PROGRESS_START) {
            if (job->serviced || progress)
                return false;
            job->serviced = true;
            job->first_service_ns = event_ns;
            job->progress_ns = event_ns;
        } else if (phase == TB_SYMBOL_PROGRESS_ADVANCE) {
            if (!job->serviced || progress < job->progress || event_ns < job->progress_ns)
                return false;
            if (progress > job->progress) {
                job->progress = progress;
                job->progress_ns = event_ns;
            }
        } else {
            return false;
        }
        return true;
    }
    if (message->length < TB_SYMBOL_RESULT_BYTES || tb_symbol_protocol_get(message->payload + 4, 4) ||
        tb_symbol_protocol_get(message->payload + 8, 8) != job->identity.generation) {
        return false;
    }
    uint64_t outcome = tb_symbol_protocol_get(message->payload, 4);
    size_t bytes = message->length - TB_SYMBOL_RESULT_BYTES;
    if (outcome <= TB_SYMBOL_PENDING || outcome > TB_SYMBOL_OPERATIONAL_FAILURE ||
        (outcome != TB_SYMBOL_SUCCESS && (bytes || message->descriptor >= 0)) ||
        (!job->serviced && outcome == TB_SYMBOL_SUCCESS)) {
        return false;
    }
    if (bytes && job->scheduler_owned) {
        if (!tb_symbol_budget_reserve(&helper->ipc, bytes)) {
            job->outcome = TB_SYMBOL_RESOURCE_FAILURE;
            job->complete = true;
            return true;
        }
        job->buffer = malloc(bytes);
        if (!job->buffer) {
            (void)tb_symbol_budget_release(&helper->ipc, bytes);
            job->outcome = TB_SYMBOL_RESOURCE_FAILURE;
            job->complete = true;
            return true;
        }
        memcpy(job->buffer, message->payload + TB_SYMBOL_RESULT_BYTES, bytes);
        job->length = bytes;
    }
    job->descriptor = tb_symbol_wire_take_descriptor(helper->wire);
    job->outcome = (TbSymbolOutcome_e)outcome;
    job->complete = true;
    job->cancel_pending = false;
    if (!job->scheduler_owned)
        job_free(job);
    return true;
}


typedef struct {
    tb_symbol_helper_emit_fn emit;
    void                    *context;
} Emit_t;

static bool emit_result(void *context, const TbSymbolWorkIdentity_t *identity,
                        TbSymbolWorkResult_t result, void *work_context)
{
    Emit_t *observer = context;
    HelperJob_t *job = work_context;
    TbSymbolHelperResult_t completion = {
        .outcome    = result.outcome,
        .descriptor = -1
    };
    if (job && result.outcome == TB_SYMBOL_SUCCESS) {
        completion.payload = job->buffer;
        completion.length = job->length;
        completion.descriptor = job->descriptor;
    }
    return observer->emit && observer->emit(observer->context, identity, completion);
}


void tb_symbol_helper_close(TbSymbolHelper_t *helper)
{
    if (!helper)
        return;
    (void)now_ns(&helper->now_ns);
    tb_symbol_schedule_cancel_all(helper->schedule);
    helper->shutdown_pending = helper->wire != NULL;
    tb_symbol_process_stop(&helper->process, helper->now_ns);
    helper->state = TB_SYMBOL_HELPER_STOPPING;
}


static void control(TbSymbolHelper_t *helper)
{
    if (!helper->wire || tb_symbol_wire_sending(helper->wire))
        return;
    if (helper->shutdown_pending) {
        TbSymbolMessage_t message = {
            .kind       = TB_SYMBOL_MESSAGE_SHUTDOWN,
            .generation = helper->generation,
            .descriptor = -1
        };
        if (tb_symbol_wire_send(helper->wire, &message))
            helper->shutdown_pending = false;
        return;
    }
    for (size_t i = 0; i < helper->config.max_jobs; ++i) {
        HelperJob_t *job = &helper->jobs[i];
        if (!job->used || !job->cancel_pending)
            continue;
        unsigned char bytes[TB_SYMBOL_CANCEL_BYTES];
        tb_symbol_protocol_put(bytes, job->identity.generation, 8);
        TbSymbolMessage_t message = tb_symbol_protocol_message(TB_SYMBOL_MESSAGE_CANCEL, job->identity,
                                                               job->generation, bytes, sizeof(bytes));
        if (tb_symbol_wire_send(helper->wire, &message))
            job->cancel_pending = false;
        return;
    }
}


static void deadlines(TbSymbolHelper_t *helper)
{
    if (helper->state == TB_SYMBOL_HELPER_STARTING && helper->now_ns >= helper->startup_deadline_ns) {
        fail(helper, TB_SYMBOL_OPERATIONAL_FAILURE, "Symbol helper startup deadline exceeded");
        return;
    }
    if (helper->state != TB_SYMBOL_HELPER_READY && helper->state != TB_SYMBOL_HELPER_STARTING)
        return;
    for (size_t i = 0; i < helper->config.max_jobs; ++i) {
        HelperJob_t *job = &helper->jobs[i];
        if (!job->used || job->complete)
            continue;
        const char *reason = NULL;
        if (!job->scheduler_owned && helper->now_ns >= job->cancel_deadline_ns)
            reason = "Symbol helper cancellation deadline exceeded";
        else if (!job->serviced && helper->now_ns >= after(job->admitted_ns, helper->config.queue_ns))
            reason = "Symbol helper queue deadline exceeded";
        else if (job->serviced && helper->now_ns >= after(job->first_service_ns,
                                                          job->heavy ? helper->config.heavy_ns
                                                                     : helper->config.ready_ns))
            reason = "Symbol helper operation deadline exceeded";
        else if (job->serviced && helper->now_ns >= after(job->progress_ns, helper->config.progress_ns))
            reason = "Symbol helper made no meaningful progress";
        if (reason) {
            bool cancellation_expired = !job->scheduler_owned;
            fail(helper, TB_SYMBOL_OPERATIONAL_FAILURE, reason);
            if (cancellation_expired && helper->process.state == TB_SYMBOL_PROCESS_STOPPING)
                helper->process.deadline_ns = helper->now_ns;
            return;
        }
    }
}


void tb_symbol_helper_pump(TbSymbolHelper_t *helper, tb_symbol_helper_emit_fn emit, void *context)
{
    if (!helper)
        return;
    if (!now_ns(&helper->now_ns)) {
        fail(helper, TB_SYMBOL_OPERATIONAL_FAILURE, "Symbol helper monotonic clock unavailable");
        return;
    }
    tb_symbol_process_pump(&helper->process, helper->now_ns);
    if (!helper->process.wait_owned && helper->wire) {
        if (helper->state == TB_SYMBOL_HELPER_STOPPING) {
            tb_symbol_wire_destroy(helper->wire);
            helper->wire = NULL;
        } else {
            fail(helper, TB_SYMBOL_OPERATIONAL_FAILURE, "Symbol helper exited before completion");
        }
    }
    deadlines(helper);
    if (helper->wire) {
        TbSymbolWireStatus_e status = tb_symbol_wire_pump(helper->wire);
        if (status == TB_SYMBOL_WIRE_MESSAGE) {
            if (!receive(helper, tb_symbol_wire_message(helper->wire)))
                fail(helper, TB_SYMBOL_MALFORMED, "Symbol helper protocol or captured identity mismatch");
            else
                tb_symbol_wire_consume(helper->wire);
        } else if (status == TB_SYMBOL_WIRE_EOF && helper->state == TB_SYMBOL_HELPER_STOPPING) {
            tb_symbol_wire_destroy(helper->wire);
            helper->wire = NULL;
        } else if (status != TB_SYMBOL_WIRE_OK) {
            TbSymbolOutcome_e outcome = status == TB_SYMBOL_WIRE_RESOURCE ? TB_SYMBOL_RESOURCE_FAILURE
                                      : status == TB_SYMBOL_WIRE_PROTOCOL ? TB_SYMBOL_MALFORMED
                                                                         : TB_SYMBOL_OPERATIONAL_FAILURE;
            fail(helper, outcome, "Symbol helper channel failed");
        }
    }
    deadlines(helper);
    control(helper);
    Emit_t observer = {
        .emit    = emit,
        .context = context
    };
    (void)tb_symbol_schedule_pump(helper->schedule, helper->now_ns, helper->config.slice_ns,
                                 emit_result, &observer);
    if (tb_symbol_process_released(&helper->process)) {
        for (size_t i = 0; i < helper->config.max_jobs; ++i) {
            if (helper->jobs[i].used && !helper->jobs[i].scheduler_owned)
                job_free(&helper->jobs[i]);
        }
        if (helper->state == TB_SYMBOL_HELPER_STOPPING)
            helper->state = TB_SYMBOL_HELPER_IDLE;
    }
}


bool tb_symbol_helper_destroy(TbSymbolHelper_t *helper)
{
    if (!helper)
        return true;
    if (!tb_symbol_process_released(&helper->process) || tb_symbol_schedule_jobs(helper->schedule))
        return false;
    for (size_t i = 0; i < helper->config.max_jobs; ++i) {
        if (helper->jobs[i].used)
            job_free(&helper->jobs[i]);
    }
    tb_symbol_wire_destroy(helper->wire);
    tb_symbol_schedule_destroy(helper->schedule);
    TbSymbolBudget_t storage = helper->storage;
    free(helper);
    (void)tb_symbol_budget_release(&storage, sizeof(TbSymbolHelper_t));
    return true;
}


size_t tb_symbol_helper_pollfds(const TbSymbolHelper_t *helper, struct pollfd *fds)
{
    if (!helper || !fds)
        return 0;
    size_t count = 0;
    if (helper->wire)
        fds[count++] = tb_symbol_wire_pollfd(helper->wire);
    if (helper->process.pidfd >= 0) {
        fds[count++] = (struct pollfd){
            .fd     = helper->process.pidfd,
            .events = POLLIN
        };
    }
    return count;
}


int tb_symbol_helper_timeout(const TbSymbolHelper_t *helper, int maximum_ms)
{
    if (maximum_ms < 0)
        maximum_ms = 0;
    if (!helper)
        return maximum_ms;
    bool active = helper->process.wait_owned || tb_symbol_schedule_jobs(helper->schedule);
    return active && maximum_ms > 5 ? 5 : maximum_ms;
}


TbSymbolHelperState_e tb_symbol_helper_state(const TbSymbolHelper_t *helper)
{
    return helper ? helper->state : TB_SYMBOL_HELPER_UNAVAILABLE;
}


const char *tb_symbol_helper_error(const TbSymbolHelper_t *helper)
{
    return helper ? helper->error : "Symbol helper unavailable";
}


size_t tb_symbol_helper_jobs(const TbSymbolHelper_t *helper)
{
    if (!helper)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < helper->config.max_jobs; ++i)
        count += helper->jobs[i].used ? 1 : 0;
    return count;
}


bool tb_symbol_helper_timing(const TbSymbolHelper_t *helper, uint64_t request, TbSymbolHelperTiming_t *timing)
{
    if (!helper || !request || !timing)
        return false;
    for (size_t i = 0; i < helper->config.max_jobs; ++i) {
        const HelperJob_t *job = &helper->jobs[i];
        if (!job->used || job->identity.request != request)
            continue;
        *timing = (TbSymbolHelperTiming_t){
            .admitted_ns           = job->admitted_ns,
            .queue_deadline_ns     = after(job->admitted_ns, helper->config.queue_ns),
            .first_service_ns      = job->first_service_ns,
            .operation_deadline_ns = job->serviced ? after(job->first_service_ns,
                                                          job->heavy ? helper->config.heavy_ns
                                                                     : helper->config.ready_ns) : 0,
            .last_progress_ns      = job->progress_ns,
            .progress              = job->progress,
            .cancel_deadline_ns    = job->cancel_deadline_ns,
            .serviced              = job->serviced,
            .retiring              = !job->scheduler_owned
        };
        return true;
    }
    return false;
}
