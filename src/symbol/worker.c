#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Inherited channel descriptor flags. */
#include "symbol/worker.h"
#include "symbol/protocol.h"
#include "symbol/operation.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct worker Worker_t;

typedef struct {
    Worker_t                    *owner;
    unsigned char               *arguments;
    size_t                       length;
    TbSymbolWorkIdentity_t       identity;
    uint32_t                     operation;
    uint64_t                     sequence;
    TbSymbolOutcome_e            failure;
    TbSymbolOperation_t         *state;
    TbSymbolOperationResult_t    diagnostic;
    uint64_t                     progress;
    uint64_t                     reported_progress;
    bool                         offered;
    bool                         diagnosed;
    bool                         used;
    bool                         started;
    bool                         emitted;
    bool                         cleanup_pending;
} WorkerJob_t;

struct worker {
    TbSymbolWire_t     *wire;
    TbSymbolSchedule_t *schedule;
    TbSymbolBudget_t    ipc;
    TbSymbolBudget_t    decoder;
    WorkerJob_t         jobs[TB_SYMBOL_MAX_JOBS];
    uint64_t            generation;
    uint64_t            last_request;
    uint64_t            last_sequence;
    uint64_t            slice_ns;
    size_t              checkpoint_bytes;
    unsigned            checkpoint_steps;
    bool                configured;
    bool                stopping;
};

size_t tb_symbol_worker_minimum_ipc(void)
{
    return sizeof(Worker_t) + tb_symbol_schedule_overhead() + tb_symbol_wire_overhead() +
           TB_SYMBOL_REQUEST_BYTES;
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


static void job_destroy(void *context)
{
    WorkerJob_t *job = context;
    if (job->state)
        job->diagnostic = tb_symbol_operation_result(job->state);
    tb_symbol_operation_destroy(job->state);
    job->state = NULL;
    job->diagnostic.payload = NULL;
    job->diagnostic.descriptor = -1;

    if (job->arguments) {
        free(job->arguments);
        (void)tb_symbol_budget_release(&job->owner->ipc, job->length);
    }
    job->arguments = NULL;

    if (job->emitted) {
        if (tb_symbol_protocol_owned(job->operation))
            job->cleanup_pending = true;
        else
            *job = (WorkerJob_t) {0};
    }
}


static TbSymbolWorkResult_t job_step(void *context, uint64_t deadline_ns,
                                    size_t max_bytes, unsigned max_steps)
{
    WorkerJob_t *job = context;
    uint64_t service_ns;
    if (!now_ns(&service_ns)) {
        return (TbSymbolWorkResult_t) {
            .outcome = TB_SYMBOL_OPERATIONAL_FAILURE
        };
    }

    if (max_bytes > job->owner->checkpoint_bytes)
        max_bytes = job->owner->checkpoint_bytes;

    if (max_steps > job->owner->checkpoint_steps)
        max_steps = job->owner->checkpoint_steps;

    if (service_ns >= deadline_ns || !max_bytes || !max_steps)
        return (TbSymbolWorkResult_t) {0};

    if (!job->started) {
        unsigned char bytes[TB_SYMBOL_PROGRESS_BYTES] = {0};

        tb_symbol_protocol_put(bytes, TB_SYMBOL_PROGRESS_START, 4);
        tb_symbol_protocol_put(bytes + 8, job->identity.generation, 8);
        tb_symbol_protocol_put(bytes + 24, service_ns, 8);

        TbSymbolMessage_t message = tb_symbol_protocol_message(TB_SYMBOL_MESSAGE_PROGRESS, job->identity,
                                                               job->owner->generation, bytes, sizeof(bytes));
        if (tb_symbol_wire_send(job->owner->wire, &message))
            job->started = true;

        return (TbSymbolWorkResult_t) {0};
    }

    /* Flush first-service evidence before any potentially blocking operation. */
    TbSymbolWorkResult_t pending = {
        .progress = job->progress
    };

    if (tb_symbol_wire_sending(job->owner->wire))
        return pending;

    if (job->failure || !tb_symbol_protocol_owned(job->operation)) {
        return (TbSymbolWorkResult_t) {
            .outcome  = job->failure ? job->failure : TB_SYMBOL_UNSUPPORTED,
            .progress = job->progress
        };
    }

    if (!job->state) {
        job->state = tb_symbol_operation_create(job->operation, job->arguments, job->length,
                                                (TbSymbolOperationBudgets_t) {
            .control = &job->owner->ipc,
            .decoder = job->owner->decoder.limit ? &job->owner->decoder : NULL
        });

        if (!job->state) {
            return (TbSymbolWorkResult_t) {
                .outcome  = TB_SYMBOL_RESOURCE_FAILURE,
                .progress = job->progress
            };
        }
    }

    if (job->progress > job->reported_progress) {
        unsigned char bytes[TB_SYMBOL_PROGRESS_BYTES] = {0};

        tb_symbol_protocol_put(bytes, TB_SYMBOL_PROGRESS_ADVANCE, 4);
        tb_symbol_protocol_put(bytes + 8, job->identity.generation, 8);
        tb_symbol_protocol_put(bytes + 16, job->progress, 8);
        tb_symbol_protocol_put(bytes + 24, service_ns, 8);

        TbSymbolMessage_t message = tb_symbol_protocol_message(TB_SYMBOL_MESSAGE_PROGRESS, job->identity,
                                                               job->owner->generation, bytes, sizeof(bytes));
        if (tb_symbol_wire_send(job->owner->wire, &message))
            job->reported_progress = job->progress;

        return pending;
    }

    TbSymbolAcquireOffer_t offer;
    if (!job->offered && tb_symbol_operation_offer(job->state, &offer)) {
        unsigned char bytes[TB_SYMBOL_OFFER_BYTES];

        tb_symbol_protocol_put(bytes, job->identity.generation, 8);
        tb_symbol_protocol_put(bytes + 8, offer.bytes, 8);
        tb_symbol_protocol_put(bytes + 16, offer.key.device, 8);
        tb_symbol_protocol_put(bytes + 24, offer.key.inode, 8);

        TbSymbolMessage_t message = tb_symbol_protocol_message(TB_SYMBOL_MESSAGE_OFFER, job->identity,
                                                               job->owner->generation, bytes, sizeof(bytes));
        if (tb_symbol_wire_send(job->owner->wire, &message))
            job->offered = true;

        return pending;
    }

    TbSymbolWorkResult_t result = tb_symbol_operation_step(job->state, max_bytes);

    job->progress = result.progress;

    return result;
}


static bool emit(void *context, const TbSymbolWorkIdentity_t *identity, TbSymbolWorkResult_t result,
                 void *work_context)
{
    Worker_t *worker = context;
    WorkerJob_t *job = NULL;
    for (size_t i = 0; i < TB_SYMBOL_MAX_JOBS; ++i) {
        if (worker->jobs[i].used && worker->jobs[i].sequence == identity->request) {
            job = &worker->jobs[i];
            break;
        }
    }

    if (!job)
        return false;

    if (tb_symbol_wire_sending(worker->wire))
        return false;

    TbSymbolOperationResult_t facts = job->state ? tb_symbol_operation_result(job->state) : job->diagnostic;

    if (job->failure)
        result.outcome = job->failure;

    if (result.outcome != TB_SYMBOL_SUCCESS && !job->diagnosed && (facts.system_error || facts.detail)) {
        unsigned char diagnostic[TB_SYMBOL_DIAGNOSTIC_BYTES];

        tb_symbol_protocol_put(diagnostic, job->identity.generation, 8);
        tb_symbol_protocol_put(diagnostic + 8, (uint32_t)facts.system_error, 4);
        tb_symbol_protocol_put(diagnostic + 12, facts.detail, 4);
        tb_symbol_protocol_put(diagnostic + 16, facts.key.device, 8);
        tb_symbol_protocol_put(diagnostic + 24, facts.key.inode, 8);
        tb_symbol_protocol_put(diagnostic + 32, facts.record_index, 8);

        TbSymbolMessage_t message = tb_symbol_protocol_message(TB_SYMBOL_MESSAGE_DIAGNOSTIC, job->identity,
                                                               worker->generation, diagnostic,
                                                               sizeof(diagnostic));
        if (tb_symbol_wire_send(worker->wire, &message))
            job->diagnosed = true;

        return false;
    }

    unsigned char  inline_bytes[TB_SYMBOL_RESULT_BYTES + 56] = {0};
    unsigned char *bytes         = inline_bytes;
    size_t         allocated     = 0;
    size_t         payload_bytes = result.outcome == TB_SYMBOL_SUCCESS ? facts.length : 0;
    int            error         = 0;

    if (payload_bytes > TB_SYMBOL_WIRE_MAX_PAYLOAD - TB_SYMBOL_RESULT_BYTES) {
        error = EFBIG;
    } else if (payload_bytes > sizeof(inline_bytes) - TB_SYMBOL_RESULT_BYTES) {
        size_t needed = TB_SYMBOL_RESULT_BYTES + payload_bytes;
        if (!tb_symbol_budget_reserve(&worker->ipc, needed)) {
            error = ENOMEM;
        } else {
            bytes = malloc(needed);
            if (!bytes) {
                (void)tb_symbol_budget_release(&worker->ipc, needed);
                error = ENOMEM;
            } else {
                allocated = needed;
                memset(bytes, 0, TB_SYMBOL_RESULT_BYTES);
            }
        }
    }
    if (error) {
        job->failure = TB_SYMBOL_RESOURCE_FAILURE;
        job->diagnostic = (TbSymbolOperationResult_t) {
            .descriptor   = -1,
            .system_error = error,
            .detail       = TB_SYMBOL_DETAIL_PUBLICATION |
                            (facts.detail & (TB_SYMBOL_DETAIL_CREATED | TB_SYMBOL_DETAIL_HAS_KEY)),
            .key          = facts.key
        };
        tb_symbol_operation_destroy(job->state);
        job->state = NULL;
        job->diagnosed = false;

        return false;
    }

    tb_symbol_protocol_put(bytes, result.outcome, 4);
    tb_symbol_protocol_put(bytes + 8, job->identity.generation, 8);

    size_t length = TB_SYMBOL_RESULT_BYTES + payload_bytes;
    if (payload_bytes)
        memcpy(bytes + TB_SYMBOL_RESULT_BYTES, facts.payload, payload_bytes);

    TbSymbolMessage_t message = tb_symbol_protocol_message(TB_SYMBOL_MESSAGE_RESULT, job->identity,
                                                           worker->generation, bytes, length);

    if (result.outcome == TB_SYMBOL_SUCCESS && job->state && facts.descriptor >= 0) {
        message.descriptor = facts.descriptor;
        message.descriptor_role = TB_SYMBOL_DESCRIPTOR_BACKING;
    }
    errno = 0;

    bool sent = tb_symbol_wire_send(worker->wire, &message);
    error = errno;
    if (allocated) {
        free(bytes);
        (void)tb_symbol_budget_release(&worker->ipc, allocated);
    }
    if (!sent) {
        /* Reserved inline failure framing must remain possible when candidate
         * serialization or descriptor duplication exhausts resources. */
        job->failure = !error || error == ENOMEM || error == EMFILE || error == ENFILE
                         ? TB_SYMBOL_RESOURCE_FAILURE : TB_SYMBOL_OPERATIONAL_FAILURE;
        job->diagnostic = (TbSymbolOperationResult_t) {
            .descriptor   = -1,
            .system_error = error,
            .detail       = TB_SYMBOL_DETAIL_PUBLICATION
        };

        if (job->state) {
            job->diagnostic.key = facts.key;
            job->diagnostic.detail |= facts.detail & (TB_SYMBOL_DETAIL_CREATED | TB_SYMBOL_DETAIL_HAS_KEY);
            tb_symbol_operation_destroy(job->state);
            job->state = NULL;
        }
        job->diagnosed = false;

        return false;
    }

    job->emitted = true;

    if (!work_context) {
        if (tb_symbol_protocol_owned(job->operation))
            job->cleanup_pending = true;
        else
            *job = (WorkerJob_t) {0};
    }

    return true;
}


static void release_operation(Worker_t *worker)
{
    if (tb_symbol_wire_sending(worker->wire))
        return;

    for (size_t i = 0; i < TB_SYMBOL_MAX_JOBS; ++i) {
        WorkerJob_t *job = &worker->jobs[i];
        if (!job->used || !job->cleanup_pending)
            continue;

        unsigned char bytes[TB_SYMBOL_SETTLED_BYTES];

        tb_symbol_protocol_put(bytes, job->identity.generation, 8);

        TbSymbolMessage_t message = tb_symbol_protocol_message(TB_SYMBOL_MESSAGE_SETTLED, job->identity,
                                                               worker->generation, bytes, sizeof(bytes));
        if (tb_symbol_wire_send(worker->wire, &message))
            *job = (WorkerJob_t) {0};

        return;
    }
}


static bool operation_pending(const Worker_t *worker)
{
    for (size_t i = 0; i < TB_SYMBOL_MAX_JOBS; ++i) {
        if (worker->jobs[i].used)
            return true;
    }

    return false;
}


static bool runnable(const Worker_t *worker)
{
    for (size_t i = 0; i < TB_SYMBOL_MAX_JOBS; ++i) {
        const WorkerJob_t *job = &worker->jobs[i];
        if (!job->used || job->cleanup_pending || job->emitted)
            continue;

        TbSymbolAcquireOffer_t offer;
        if (!job->state || !job->offered || job->progress > job->reported_progress ||
            !tb_symbol_operation_offer(job->state, &offer)) {
            return true;
        }
    }

    return false;
}


static bool configure(Worker_t *worker, const TbSymbolMessage_t *message)
{
    if (message->kind != TB_SYMBOL_MESSAGE_HELLO || message->request || message->consumer ||
        message->revision || !message->generation || message->length != TB_SYMBOL_HELLO_BYTES ||
        message->descriptor >= 0) {
        return false;
    }

    uint64_t jobs = tb_symbol_protocol_get(message->payload, 4);
    uint64_t interests = tb_symbol_protocol_get(message->payload + 4, 4);
    uint64_t slice = tb_symbol_protocol_get(message->payload + 8, 8);
    uint64_t ipc = tb_symbol_protocol_get(message->payload + 16, 8);
    uint64_t checkpoint_bytes = tb_symbol_protocol_get(message->payload + 24, 4);
    uint64_t checkpoint_steps = tb_symbol_protocol_get(message->payload + 28, 4);
    uint64_t decoder = tb_symbol_protocol_get(message->payload + 32, 8);
    if (!jobs || jobs > TB_SYMBOL_MAX_JOBS || !interests || interests > TB_SYMBOL_MAX_INTERESTS ||
        !slice || ipc > SIZE_MAX || ipc < worker->ipc.used || decoder > SIZE_MAX ||
        checkpoint_bytes < 64 || checkpoint_bytes > TB_SYMBOL_WIRE_IO_BYTES ||
        !checkpoint_steps || checkpoint_steps > TB_SYMBOL_WIRE_IO_STEPS) {
        return false;
    }

    worker->schedule = tb_symbol_schedule_create((size_t)jobs, (size_t)jobs, &worker->ipc);

    if (!worker->schedule || ipc < worker->ipc.used)
        return false;

    if (decoder && !tb_symbol_budget_init(&worker->decoder, (size_t)decoder))
        return false;

    worker->generation = message->generation;
    worker->slice_ns = slice;
    worker->checkpoint_bytes = (size_t)checkpoint_bytes;
    worker->checkpoint_steps = (unsigned)checkpoint_steps;
    worker->ipc.limit = (size_t)ipc;
    worker->configured = true;

    TbSymbolMessage_t ready = {
        .kind       = TB_SYMBOL_MESSAGE_READY,
        .generation = worker->generation,
        .descriptor = -1
    };

    return tb_symbol_wire_send(worker->wire, &ready);
}


static bool request(Worker_t *worker, const TbSymbolMessage_t *message)
{
    if (!message->request || !message->consumer ||
        message->length < TB_SYMBOL_REQUEST_BYTES || message->descriptor >= 0 ||
        message->payload[4] > TB_SYMBOL_CONSTRUCTION || message->payload[5] > 1 ||
        message->payload[6] || message->payload[7]) {
        return false;
    }

    TbSymbolWorkIdentity_t identity = {
        .request    = message->request,
        .consumer   = message->consumer,
        .revision   = message->revision,
        .generation = tb_symbol_protocol_get(message->payload + 8, 8)
    };

    uint64_t sequence = tb_symbol_protocol_get(message->payload + 16, 8);
    if (!identity.generation || sequence <= worker->last_sequence)
        return false;

    WorkerJob_t *job = NULL;
    for (size_t i = 0; i < TB_SYMBOL_MAX_JOBS; ++i) {
        if (worker->jobs[i].used && worker->jobs[i].identity.request == message->request)
            return false;

        if (!worker->jobs[i].used && !job)
            job = &worker->jobs[i];
    }

    if (!job)
        return false;

    *job = (WorkerJob_t) {
        .owner     = worker,
        .identity  = identity,
        .sequence  = sequence,
        .operation = (uint32_t)tb_symbol_protocol_get(message->payload, 4),
        .length    = message->length - TB_SYMBOL_REQUEST_BYTES,
        .used      = true
    };

    if (job->length) {
        if (tb_symbol_budget_reserve(&worker->ipc, job->length)) {
            job->arguments = malloc(job->length);

            if (!job->arguments)
                (void)tb_symbol_budget_release(&worker->ipc, job->length);
        }

        if (job->arguments)
            memcpy(job->arguments, message->payload + TB_SYMBOL_REQUEST_BYTES, job->length);
        else
            job->failure = TB_SYMBOL_RESOURCE_FAILURE;
    }

    TbSymbolWork_t work = {
        .identity = {
            .request    = sequence,
            .consumer   = sequence,
            .revision   = identity.revision,
            .generation = identity.generation
        },
        .priority = (TbSymbolPriority_e)message->payload[4],
        .step     = job_step,
        .destroy  = job_destroy,
        .context  = job
    };

    if (!tb_symbol_schedule_add(worker->schedule, &work)) {
        job->emitted = true;
        job_destroy(job);
        return false;
    }

    worker->last_sequence = sequence;

    if (identity.request > worker->last_request)
        worker->last_request = identity.request;

    return true;
}


static bool receive(Worker_t *worker, const TbSymbolMessage_t *message)
{
    if (!worker->configured)
        return configure(worker, message);

    if (message->generation != worker->generation || message->descriptor >= 0)
        return false;

    if (message->kind == TB_SYMBOL_MESSAGE_REQUEST)
        return !worker->stopping && request(worker, message);

    if (message->kind == TB_SYMBOL_MESSAGE_SHUTDOWN) {
        if (message->request || message->consumer || message->revision || message->length)
            return false;

        worker->stopping = true;
        tb_symbol_schedule_cancel_all(worker->schedule);

        return true;
    }

    if (message->kind == TB_SYMBOL_MESSAGE_GRANT) {
        if (worker->stopping || message->length != TB_SYMBOL_GRANT_BYTES)
            return false;

        for (size_t i = 0; i < TB_SYMBOL_MAX_JOBS; ++i) {
            WorkerJob_t *job = &worker->jobs[i];
            if (job->used && !job->emitted && job->offered && job->identity.request == message->request &&
                job->identity.consumer == message->consumer && job->identity.revision == message->revision &&
                job->identity.generation == tb_symbol_protocol_get(message->payload, 8)) {
                return tb_symbol_operation_grant(job->state, tb_symbol_protocol_get(message->payload + 8, 8));
            }
        }

        return false;
    }

    if (message->kind != TB_SYMBOL_MESSAGE_CANCEL || !message->request || !message->consumer ||
        message->request > worker->last_request || message->length != TB_SYMBOL_CANCEL_BYTES) {
        return false;
    }

    TbSymbolWorkIdentity_t identity = {
        .request    = message->request,
        .consumer   = message->consumer,
        .revision   = message->revision,
        .generation = tb_symbol_protocol_get(message->payload, 8)
    };

    /* Completed IDs are never reused; a cancel racing a sent result is inert. */
    if (!identity.generation)
        return false;

    for (size_t i = 0; i < TB_SYMBOL_MAX_JOBS; ++i) {
        WorkerJob_t *job = &worker->jobs[i];
        if (job->used && job->identity.request == identity.request &&
            job->identity.consumer == identity.consumer && job->identity.revision == identity.revision &&
            job->identity.generation == identity.generation) {
            identity.request = job->sequence;
            identity.consumer = job->sequence;
            (void)tb_symbol_schedule_cancel(worker->schedule, identity);
            break;
        }
    }

    return true;
}


int tb_symbol_worker_main(int socket_fd)
{
    int flags = fcntl(socket_fd, F_GETFD);
    if (flags < 0 || fcntl(socket_fd, F_SETFD, (int)((unsigned)flags | (unsigned)FD_CLOEXEC)) < 0) {
        if (socket_fd >= 0)
            (void)close(socket_fd);
        return EXIT_FAILURE;
    }

    Worker_t worker = {0};
    if (!tb_symbol_budget_init(&worker.ipc, TB_SYMBOL_WIRE_DEFAULT_MAX) ||
        !tb_symbol_budget_reserve(&worker.ipc, sizeof(worker))) {
        (void)close(socket_fd);
        return EXIT_FAILURE;
    }

    worker.wire = tb_symbol_wire_create(socket_fd, SIZE_MAX, &worker.ipc);

    if (!worker.wire) {
        (void)close(socket_fd);
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    for (;;) {
        TbSymbolWireStatus_e status = tb_symbol_wire_pump(worker.wire);
        if (status == TB_SYMBOL_WIRE_EOF) {
            result = EXIT_SUCCESS;
            break;
        }

        if (status != TB_SYMBOL_WIRE_OK && status != TB_SYMBOL_WIRE_MESSAGE)
            break;

        if (status == TB_SYMBOL_WIRE_MESSAGE) {
            if (!receive(&worker, tb_symbol_wire_message(worker.wire)))
                break;

            tb_symbol_wire_consume(worker.wire);
        }

        uint64_t now;
        if (!now_ns(&now))
            break;

        release_operation(&worker);

        if (worker.schedule)
            (void)tb_symbol_schedule_pump(worker.schedule, now, worker.slice_ns, emit, &worker);

        if (worker.stopping && !tb_symbol_schedule_jobs(worker.schedule) &&
            !operation_pending(&worker) && !tb_symbol_wire_sending(worker.wire)) {
            result = EXIT_SUCCESS;
            break;
        }

        struct pollfd fd = tb_symbol_wire_pollfd(worker.wire);
        /* An offered acquisition is dormant until a grant/cancel arrives. */
        int timeout = worker.schedule && runnable(&worker) && !tb_symbol_wire_sending(worker.wire) ? 0 : 100;
        if (poll(&fd, 1, timeout) < 0 && errno != EINTR)
            break;
    }

    tb_symbol_schedule_destroy(worker.schedule);
    tb_symbol_wire_destroy(worker.wire);
    (void)tb_symbol_budget_release(&worker.ipc, sizeof(worker));

    return worker.ipc.used || worker.decoder.used ? EXIT_FAILURE : result;
}
