/* Linux pipe2 and atomic close-on-exec descriptors. */
#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) */
#include "controller.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef struct {
    char *wire;
    size_t length, written;
    uint64_t token, cookie, deadline;
    TbGdbGeneration generation;
    TbGdbScope scope;
    bool setup;
} Request;

struct TbGdb {
    TbGdbEmit emit;
    void *context;
    TbGdbState state;
    TbGdbGeneration generation;
    pid_t pid;
    int input, output, error, version, shutdown_stage;
    uint64_t next_token, startup_deadline, shutdown_deadline, drain_deadline;
    char *executable;
    char startup_error[1024];
    size_t startup_error_length;
    bool prompted, closing, protocol_failed;
    unsigned startup_ms, setup_left;
    TbMiParser parser;
    Request requests[TB_GDB_MAX_PENDING];
};

uint64_t tb_gdb_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) { abort(); }
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void event(TbGdb *gdb, TbGdbEventKind kind, const Request *request,
                  const TbMiRecord *record, const char *text, size_t length, int status)
{
    TbGdbEvent value = {.kind = kind, .generation = gdb->generation,
                       .record = record, .text = text, .length = length, .status = status};
    if (request != NULL) {
        value.token = request->token;
        value.cookie = request->cookie;
        value.generation = request->generation;
    }
    gdb->emit(gdb->context, &value);
}

static void diagnostic(TbGdb *gdb, const char *text, int error)
{
    event(gdb, TB_GDB_DIAGNOSTIC, NULL, NULL, text, strlen(text), error);
}

static void state_set(TbGdb *gdb, TbGdbState state)
{
    if (gdb->state == state) { return; }
    gdb->state = state;
    event(gdb, TB_GDB_STATE, NULL, NULL, NULL, 0, 0);
}

static void fd_close(int *fd)
{
    if (*fd >= 0) { (void)close(*fd); *fd = -1; }
}

static void complete(TbGdb *gdb, Request *request, TbGdbEventKind kind,
                     const TbMiRecord *record)
{
    if (!request->setup) { event(gdb, kind, request, record, NULL, 0, 0); }
    free(request->wire);
    memset(request, 0, sizeof *request);
}

static void cancel_all(TbGdb *gdb)
{
    for (size_t i = 0; i < TB_GDB_MAX_PENDING; ++i) {
        if (gdb->requests[i].wire != NULL) {
            complete(gdb, &gdb->requests[i], TB_GDB_CANCELLED, NULL);
        }
    }
}

static void shutting_down(TbGdb *gdb, bool immediate)
{
    gdb->closing = true;
    state_set(gdb, TB_GDB_CLOSING);
    cancel_all(gdb);
    fd_close(&gdb->input);
    gdb->shutdown_deadline = tb_gdb_now_ms() + (immediate ? 0 : 300);
}

static void broken(TbGdb *gdb, const char *text, int error)
{
    if (gdb->closing) { return; }
    diagnostic(gdb, text, error);
    shutting_down(gdb, true);
}

TbGdb *tb_gdb_create(TbGdbEmit emit, void *context)
{
    if (emit == NULL) { return NULL; }
    TbGdb *gdb = calloc(1, sizeof *gdb);
    if (gdb != NULL) {
        gdb->emit = emit;
        gdb->context = context;
        gdb->input = gdb->output = gdb->error = -1;
    }
    return gdb;
}

bool tb_gdb_destroy(TbGdb *gdb)
{
    if (gdb == NULL) { return true; }
    if (gdb->pid != 0 || gdb->state != TB_GDB_DISCONNECTED) { return false; }
    cancel_all(gdb);
    fd_close(&gdb->input); fd_close(&gdb->output); fd_close(&gdb->error);
    tb_mi_parser_destroy(&gdb->parser);
    free(gdb->executable);
    free(gdb);
    return true;
}

/* Keep all pipe endpoints above stdio, even if the host closed stdin/out/err. */
static bool lift(int fds[2])
{
    for (size_t i = 0; i < 2; ++i) {
        if (fds[i] < 3) {
            int moved = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
            if (moved < 0) { return false; }
            (void)close(fds[i]); fds[i] = moved;
        }
    }
    return true;
}

static bool spawn_peer(TbGdb *gdb)
{
    int in[2] = {-1, -1}, out[2] = {-1, -1}, err[2] = {-1, -1};
    int failure = 0;
    if (pipe2(in, O_CLOEXEC) != 0 ||
        pipe2(out, O_CLOEXEC) != 0 || pipe2(err, O_CLOEXEC) != 0 ||
        !lift(in) || !lift(out) || !lift(err)) { failure = errno; }
    int all[] = {in[0], in[1], out[0], out[1], err[0], err[1]};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    bool actions_ok = false, attr_ok = false;
    if (failure == 0) {
        failure = posix_spawn_file_actions_init(&actions);
        actions_ok = failure == 0;
    }
    if (failure == 0) {
        failure = posix_spawnattr_init(&attr);
        attr_ok = failure == 0;
    }
    if (failure == 0) { failure = posix_spawn_file_actions_adddup2(&actions, in[0], 0); }
    if (failure == 0) { failure = posix_spawn_file_actions_adddup2(&actions, out[1], 1); }
    if (failure == 0) { failure = posix_spawn_file_actions_adddup2(&actions, err[1], 2); }
    for (size_t i = 0; failure == 0 && i < sizeof all / sizeof all[0]; ++i) {
        failure = posix_spawn_file_actions_addclose(&actions, all[i]);
    }
    sigset_t empty, defaults;
    (void)sigemptyset(&empty); (void)sigemptyset(&defaults);
    (void)sigaddset(&defaults, SIGPIPE); (void)sigaddset(&defaults, SIGINT);
    (void)sigaddset(&defaults, SIGTERM); (void)sigaddset(&defaults, SIGHUP);
    if (failure == 0) { failure = posix_spawnattr_setsigmask(&attr, &empty); }
    if (failure == 0) { failure = posix_spawnattr_setsigdefault(&attr, &defaults); }
    if (failure == 0) { failure = posix_spawnattr_setpgroup(&attr, 0); }
    if (failure == 0) {
        failure = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK |
                                          POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP);
    }
    char *const argv[] = {gdb->executable, "--quiet", "--nx",
                         gdb->version == 3 ? "--interpreter=mi3" : "--interpreter=mi2", NULL};
    int parents[] = {in[1], out[0], err[0]};
    for (size_t i = 0; failure == 0 && i < 3; ++i) {
        int flags = fcntl(parents[i], F_GETFL);
        if (flags < 0 || fcntl(parents[i], F_SETFL, (int)((unsigned)flags | (unsigned)O_NONBLOCK)) < 0) {
            failure = errno;
        }
    }
    if (failure == 0) {
        failure = posix_spawnp(&gdb->pid, gdb->executable, &actions, &attr, argv, environ);
        if (failure != 0) { gdb->pid = 0; }
    }
    if (actions_ok) { (void)posix_spawn_file_actions_destroy(&actions); }
    if (attr_ok) { (void)posix_spawnattr_destroy(&attr); }
    if (failure == 0) {
        gdb->input = in[1]; gdb->output = out[0]; gdb->error = err[0];
        (void)close(in[0]); (void)close(out[1]); (void)close(err[1]);
    } else {
        for (size_t i = 0; i < sizeof all / sizeof all[0]; ++i) {
            if (all[i] >= 0) { (void)close(all[i]); }
        }
        diagnostic(gdb, "cannot spawn debugger executable", failure);
        state_set(gdb, TB_GDB_DISCONNECTED);
    }
    return failure == 0;
}

bool tb_gdb_start(TbGdb *gdb, const char *executable, unsigned startup_ms)
{
    if (gdb == NULL || executable == NULL || executable[0] == '\0' ||
        startup_ms == 0 || gdb->state != TB_GDB_DISCONNECTED) { return false; }
    char *copy = strdup(executable);
    if (copy == NULL) { diagnostic(gdb, "out of memory copying debugger path", ENOMEM); return false; }
    free(gdb->executable); gdb->executable = copy;
    tb_mi_parser_destroy(&gdb->parser);
    ++gdb->generation.session;
    gdb->generation.stop = gdb->generation.context = 0;
    gdb->next_token = 0; gdb->version = 3;
    gdb->startup_ms = startup_ms;
    gdb->startup_deadline = tb_gdb_now_ms() + startup_ms;
    gdb->prompted = gdb->closing = gdb->protocol_failed = false;
    gdb->setup_left = 0; gdb->shutdown_stage = 0;
    gdb->startup_error_length = 0; gdb->startup_error[0] = '\0';
    state_set(gdb, TB_GDB_STARTING);
    return spawn_peer(gdb);
}

static bool operation_valid(const char *operation)
{
    if (operation == NULL || operation[0] != '-' || operation[1] == '\0') { return false; }
    for (size_t i = 1; operation[i] != '\0'; ++i) {
        if ((operation[i] < 'a' || operation[i] > 'z') && operation[i] != '-') { return false; }
        if (i > 128) { return false; }
    }
    return true;
}

static bool enqueue(TbGdb *gdb, const char *operation, const char *const args[],
                    size_t count, TbGdbScope scope, uint64_t cookie,
                    unsigned timeout_ms, uint64_t *token, bool setup)
{
    if (!operation_valid(operation) || count > 128 || (count != 0 && args == NULL) ||
        timeout_ms == 0 || scope < TB_GDB_SESSION || scope > TB_GDB_CONTEXT ||
        gdb->next_token == UINT64_MAX) { return false; }
    Request *request = NULL;
    for (size_t i = 0; i < TB_GDB_MAX_PENDING; ++i) {
        if (gdb->requests[i].wire == NULL) { request = &gdb->requests[i]; break; }
    }
    if (request == NULL) { diagnostic(gdb, "debugger request queue is full", ENOBUFS); return false; }
    char *wire = malloc(TB_GDB_MAX_COMMAND + 1);
    if (wire == NULL) { diagnostic(gdb, "out of memory queuing command", ENOMEM); return false; }
    uint64_t next = gdb->next_token + 1;
    int prefix = snprintf(wire, TB_GDB_MAX_COMMAND + 1, "%" PRIu64 "%s", next, operation);
    size_t length = (size_t)prefix;
    bool valid = prefix > 0 && length < TB_GDB_MAX_COMMAND;
    /* GDB's set/show MI commands consume a CLI-style raw argument tail.
     * Quoting the setting name can return ^done without changing anything.
     * Setting names are restricted; tail bytes cannot introduce a new record.
     * Environment assignment values are literal, including spaces/quotes. */
    bool setting = strcmp(operation, "-gdb-set") == 0 || strcmp(operation, "-gdb-show") == 0;
    if (setting && (count == 0 || args[0] == NULL || args[0][0] == '\0')) { valid = false; }
    for (size_t i = 0; valid && i < count; ++i) {
        if (args[i] == NULL || length + 3 >= TB_GDB_MAX_COMMAND) { valid = false; break; }
        if (setting) {
            wire[length++] = ' ';
            for (size_t j = 0; args[i][j] != '\0'; ++j) {
                unsigned char c = (unsigned char)args[i][j];
                bool name = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_';
                if (c == '\r' || c == '\n' || (i == 0 && !name) || length + 1 >= TB_GDB_MAX_COMMAND) {
                    valid = false; break;
                }
                wire[length++] = (char)c;
            }
            continue;
        }
        bool plain = args[i][0] != '\0';
        for (size_t j = 0; plain && args[i][j] != '\0'; ++j) {
            unsigned char c = (unsigned char)args[i][j];
            plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || strchr("_./:=+-", (int)c) != NULL;
        }
        if (plain) {
            size_t bytes = strlen(args[i]);
            if (bytes + length + 2 >= TB_GDB_MAX_COMMAND) { valid = false; break; }
            wire[length++] = ' ';
            memcpy(wire + length, args[i], bytes); length += bytes;
            continue;
        }
        wire[length++] = ' '; wire[length++] = '"';
        const unsigned char *argument = (const unsigned char *)args[i];
        for (size_t j = 0; argument[j] != 0; ++j) {
            unsigned char c = argument[j];
            size_t needed = c == '"' || c == '\\' ? 2 : c < 32 || c >= 127 ? 4 : 1;
            if (length + needed + 2 >= TB_GDB_MAX_COMMAND) { valid = false; break; }
            if (needed == 4) {
                wire[length++] = '\\'; wire[length++] = (char)('0' + c / 64);
                wire[length++] = (char)('0' + (c / 8) % 8);
                wire[length++] = (char)('0' + c % 8);
            } else {
                if (needed == 2) { wire[length++] = '\\'; }
                wire[length++] = (char)c;
            }
        }
        if (valid) { wire[length++] = '"'; }
    }
    if (!valid) { free(wire); diagnostic(gdb, "invalid or oversized MI command", EINVAL); return false; }
    wire[length++] = '\n'; wire[length] = '\0';
    *request = (Request){.wire = wire, .length = length, .token = next, .cookie = cookie,
                         .deadline = tb_gdb_now_ms() + timeout_ms,
                         .generation = gdb->generation, .scope = scope, .setup = setup};
    gdb->next_token = next;
    if (token != NULL) { *token = next; }
    return true;
}

bool tb_gdb_request(TbGdb *gdb, const char *operation, const char *const args[],
                    size_t count, TbGdbScope scope, uint64_t cookie,
                    unsigned timeout_ms, uint64_t *token)
{
    if (gdb == NULL || gdb->closing || gdb->state == TB_GDB_STARTING ||
        gdb->state == TB_GDB_DISCONNECTED ||
        (scope != TB_GDB_SESSION && gdb->state != TB_GDB_STOPPED)) { return false; }
    return enqueue(gdb, operation, args, count, scope, cookie, timeout_ms, token, false);
}

static bool obsolete(const TbGdb *gdb, const Request *request)
{
    return request->generation.session != gdb->generation.session ||
           (request->scope != TB_GDB_SESSION && request->generation.stop != gdb->generation.stop) ||
           (request->scope == TB_GDB_CONTEXT && request->generation.context != gdb->generation.context);
}

void tb_gdb_context_changed(TbGdb *gdb)
{
    if (gdb != NULL) { ++gdb->generation.context; }
}

static void execution_state(TbGdb *gdb, TbGdbState state)
{
    if (state != TB_GDB_RUNNING || gdb->state != state) {
        ++gdb->generation.stop;
        ++gdb->generation.context;
    }
    state_set(gdb, state);
}

static void parsed(void *context, TbMiRecord *record, const TbMiError *error)
{
    TbGdb *gdb = context;
    if (error != NULL) {
        gdb->protocol_failed = true;
        diagnostic(gdb, error->message, EPROTO);
        return; /* Parser callback cannot destroy/reinitialize its parser. */
    }
    if (gdb->closing || gdb->protocol_failed) { tb_mi_record_free(record); return; }
    if (record->kind == TB_MI_PROMPT && !gdb->prompted) {
        gdb->prompted = true;
        const char *const async[] = {"mi-async", "on"};
        const char *const all_stop[] = {"non-stop", "off"};
        gdb->setup_left = 2;
        if (!enqueue(gdb, "-gdb-set", async, 2, TB_GDB_SESSION, 0, gdb->startup_ms, NULL, true) ||
            !enqueue(gdb, "-gdb-set", all_stop, 2, TB_GDB_SESSION, 0, gdb->startup_ms, NULL, true)) {
            gdb->protocol_failed = true;
        }
    } else if (record->kind == TB_MI_RESULT) {
        Request *request = NULL;
        for (size_t i = 0; i < TB_GDB_MAX_PENDING; ++i) {
            Request *candidate = &gdb->requests[i];
            if (candidate->wire != NULL && candidate->written == candidate->length && record->token != NULL) {
                char token[32];
                (void)snprintf(token, sizeof token, "%" PRIu64, candidate->token);
                if (strcmp(token, record->token) == 0) { request = candidate; break; }
            }
        }
        if (request == NULL) {
            event(gdb, TB_GDB_STALE, NULL, record, "unmatched or late MI token", 26, 0);
        } else if (tb_gdb_now_ms() >= request->deadline) {
            bool setup = request->setup;
            complete(gdb, request, TB_GDB_TIMEOUT, NULL);
            event(gdb, TB_GDB_STALE, NULL, record, "MI reply arrived after deadline", 31, 0);
            if (setup) { gdb->protocol_failed = true; }
        } else {
            bool stale = obsolete(gdb, request);
            bool setup = request->setup;
            bool exiting = !setup && !stale && strcmp(record->klass, "exit") == 0;
            if (setup && strcmp(record->klass, "done") != 0) {
                diagnostic(gdb, "debugger rejected asynchronous all-stop setup", EPROTO);
                gdb->protocol_failed = true;
            }
            if (!setup && !stale && request->generation.stop == gdb->generation.stop &&
                strcmp(record->klass, "running") == 0) {
                execution_state(gdb, TB_GDB_RUNNING);
            }
            complete(gdb, request, stale ? TB_GDB_STALE : TB_GDB_REPLY, record);
            if (exiting) { shutting_down(gdb, false); }
            if (setup && !gdb->protocol_failed && --gdb->setup_left == 0) {
                state_set(gdb, TB_GDB_READY);
            }
        }
    } else if (record->kind == TB_MI_EXEC && gdb->state != TB_GDB_STARTING) {
        if (strcmp(record->klass, "running") == 0) { execution_state(gdb, TB_GDB_RUNNING); }
        else if (strcmp(record->klass, "stopped") == 0) {
            const TbMiValue *reason = tb_mi_find(record->results, "reason");
            bool exited = reason != NULL && reason->type == TB_MI_STRING &&
                          reason->string.length >= 6 && memcmp(reason->string.data, "exited", 6) == 0;
            execution_state(gdb, exited ? TB_GDB_EXITED : TB_GDB_STOPPED);
        }
        event(gdb, TB_GDB_RECORD, NULL, record, NULL, 0, 0);
    } else { event(gdb, TB_GDB_RECORD, NULL, record, NULL, 0, 0); }
    tb_mi_record_free(record);
}

static int next_write(const TbGdb *gdb)
{
    int next = -1;
    for (size_t i = 0; i < TB_GDB_MAX_PENDING; ++i) {
        const Request *candidate = &gdb->requests[i];
        if (candidate->wire != NULL && candidate->written < candidate->length &&
            (next < 0 || candidate->token < gdb->requests[next].token)) { next = (int)i; }
    }
    return next;
}

size_t tb_gdb_pollfds(const TbGdb *gdb, struct pollfd fds[3])
{
    if (gdb == NULL || fds == NULL) { return 0; }
    fds[0] = (struct pollfd){gdb->output, POLLIN, 0};
    fds[1] = (struct pollfd){gdb->error, POLLIN, 0};
    fds[2] = (struct pollfd){gdb->input, next_write(gdb) >= 0 ? POLLOUT : 0, 0};
    return 3;
}

int tb_gdb_timeout(const TbGdb *gdb, int maximum_ms)
{
    if (gdb == NULL || gdb->state == TB_GDB_DISCONNECTED) { return maximum_ms; }
    int limit = maximum_ms < 0 || maximum_ms > 25 ? 25 : maximum_ms;
    uint64_t now = tb_gdb_now_ms();
    uint64_t deadline = now + (unsigned)limit;
    if (gdb->closing && gdb->pid > 0 && gdb->shutdown_deadline < deadline) {
        deadline = gdb->shutdown_deadline;
    }
    if (gdb->pid == 0 && gdb->drain_deadline < deadline) { deadline = gdb->drain_deadline; }
    if (!gdb->closing && gdb->state == TB_GDB_STARTING && gdb->startup_deadline < deadline) {
        deadline = gdb->startup_deadline;
    }
    for (size_t i = 0; i < TB_GDB_MAX_PENDING; ++i) {
        if (gdb->requests[i].wire != NULL && gdb->requests[i].deadline < deadline) {
            deadline = gdb->requests[i].deadline;
        }
    }
    return deadline <= now ? 0 : (int)(deadline - now);
}

static void read_pipe(TbGdb *gdb, bool stderr_pipe)
{
    int *fd = stderr_pipe ? &gdb->error : &gdb->output;
    char buffer[4096];
    size_t budget = TB_GDB_IO_BUDGET;
    while (*fd >= 0 && budget != 0) {
        size_t maximum = budget < sizeof buffer ? budget : sizeof buffer;
        ssize_t length = read(*fd, buffer, maximum);
        if (length > 0) {
            budget -= (size_t)length;
            if (stderr_pipe) {
                size_t room = sizeof gdb->startup_error - 1 - gdb->startup_error_length;
                size_t copied = (size_t)length < room ? (size_t)length : room;
                memcpy(gdb->startup_error + gdb->startup_error_length, buffer, copied);
                gdb->startup_error_length += copied;
                gdb->startup_error[gdb->startup_error_length] = '\0';
                event(gdb, TB_GDB_STDERR, NULL, NULL, buffer, (size_t)length, 0);
            } else {
                (void)tb_mi_feed(&gdb->parser, buffer, (size_t)length, parsed, gdb);
                if (gdb->protocol_failed) { broken(gdb, "invalid debugger protocol; closing session", EPROTO); break; }
            }
        } else if (length == 0) {
            fd_close(fd);
            if (!stderr_pipe) { (void)tb_mi_finish(&gdb->parser, parsed, gdb); }
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                broken(gdb, "cannot read debugger pipe", errno); fd_close(fd);
            }
            break;
        }
    }
}

/* Keep SIGPIPE policy local to this single-threaded controller's write.
 * Preserve a previously pending signal and restore the caller's mask. */
static ssize_t pipe_write(int fd, const char *data, size_t length)
{
    sigset_t blocked, old, pending;
    (void)sigemptyset(&blocked);
    (void)sigaddset(&blocked, SIGPIPE);
    if (sigprocmask(SIG_BLOCK, &blocked, &old) != 0) { return -1; }
    if (sigpending(&pending) != 0) {
        int saved = errno;
        (void)sigprocmask(SIG_SETMASK, &old, NULL);
        errno = saved;
        return -1;
    }
    ssize_t sent = write(fd, data, length);
    int saved = errno;
    if (sent < 0 && saved == EPIPE && sigismember(&pending, SIGPIPE) == 0) {
        const struct timespec zero = {0};
        while (sigtimedwait(&blocked, NULL, &zero) < 0 && errno == EINTR) { }
    }
    (void)sigprocmask(SIG_SETMASK, &old, NULL);
    errno = saved;
    return sent;
}

static void write_commands(TbGdb *gdb)
{
    size_t budget = TB_GDB_IO_BUDGET;
    int next;
    while (!gdb->closing && gdb->input >= 0 && budget != 0 && (next = next_write(gdb)) >= 0) {
        Request *request = &gdb->requests[next];
        if (request->written == 0 && obsolete(gdb, request)) {
            complete(gdb, request, TB_GDB_STALE, NULL);
            continue;
        }
        size_t amount = request->length - request->written;
        if (amount > budget) { amount = budget; }
        ssize_t sent = pipe_write(gdb->input, request->wire + request->written, amount);
        if (sent > 0) { request->written += (size_t)sent; budget -= (size_t)sent; }
        else {
            if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                broken(gdb, "cannot write debugger command", errno);
            }
            break;
        }
    }
}

void tb_gdb_close(TbGdb *gdb)
{
    if (gdb == NULL || gdb->state == TB_GDB_DISCONNECTED || gdb->closing) { return; }
    /* The native session releases its target before this process primitive.
     * Never turn debugger cleanup into process-group killing. */
    shutting_down(gdb, false);
}

static void deadlines(TbGdb *gdb)
{
    uint64_t now = tb_gdb_now_ms();
    if (!gdb->closing && gdb->state == TB_GDB_STARTING && now >= gdb->startup_deadline) {
        broken(gdb, "debugger startup timed out", ETIMEDOUT);
    }
    for (size_t i = 0; i < TB_GDB_MAX_PENDING && !gdb->closing; ++i) {
        Request *request = &gdb->requests[i];
        if (request->wire == NULL || now < request->deadline) { continue; }
        bool partial = request->written != 0 && request->written != request->length;
        bool setup = request->setup;
        complete(gdb, request, TB_GDB_TIMEOUT, NULL);
        if (partial || setup) { broken(gdb, "debugger command timed out during write/setup", ETIMEDOUT); }
    }
    if (gdb->closing && gdb->pid > 0 && now >= gdb->shutdown_deadline) {
        int signal_number = gdb->shutdown_stage == 0 ? SIGTERM : SIGKILL;
        if (kill(gdb->pid, signal_number) != 0 && errno != ESRCH) {
            diagnostic(gdb, "cannot signal owned debugger", errno);
        }
        ++gdb->shutdown_stage;
        gdb->shutdown_deadline = now + 200;
    }
}

static void reap(TbGdb *gdb)
{
    int status = 0;
    pid_t child = waitpid(gdb->pid, &status, WNOHANG);
    if (child == 0 || (child < 0 && errno == EINTR)) { return; }
    if (child < 0 && errno != ECHILD) { broken(gdb, "cannot reap debugger", errno); return; }
    /* Clear ownership BEFORE callbacks or any future signal, including ECHILD
     * if an embedding application's SIGCHLD handler already reaped the child. */
    gdb->pid = 0;
    gdb->drain_deadline = tb_gdb_now_ms() + 200;
    if (child < 0) {
        status = -1;
        diagnostic(gdb, "debugger child was reaped outside controller", ECHILD);
    }
    event(gdb, TB_GDB_CHILD_EXIT, NULL, NULL, NULL, 0, status);
}

void tb_gdb_pump(TbGdb *gdb, int wait_ms)
{
    if (gdb == NULL || gdb->state == TB_GDB_DISCONNECTED) { return; }
    if (gdb->pid > 0) { reap(gdb); }
    deadlines(gdb);
    struct pollfd fds[3];
    (void)tb_gdb_pollfds(gdb, fds);
    int ready = poll(fds, 3, tb_gdb_timeout(gdb, wait_ms));
    if (ready < 0 && errno == EINTR) { return; }
    if (ready < 0) { broken(gdb, "debugger poll failed", errno); }
    deadlines(gdb); /* Never transmit queued commands after their deadline. */
    if (fds[0].revents != 0) { read_pipe(gdb, false); }
    if (fds[1].revents != 0) { read_pipe(gdb, true); }
    if (fds[2].revents != 0) { write_commands(gdb); }
    const unsigned input_errors = (unsigned)POLLERR | (unsigned)POLLHUP | (unsigned)POLLNVAL;
    if (((unsigned)fds[2].revents & input_errors) != 0) {
        if (!gdb->prompted && gdb->state == TB_GDB_STARTING) {
            fd_close(&gdb->input); /* Drain startup diagnostics before deciding fallback. */
        } else { broken(gdb, "debugger command input closed", EPIPE); }
    }
    if (gdb->pid > 0) { reap(gdb); }
    if (gdb->pid == 0 && tb_gdb_now_ms() >= gdb->drain_deadline &&
        (gdb->output >= 0 || gdb->error >= 0)) {
        diagnostic(gdb, "closing pipes retained after debugger exit", EPIPE);
        fd_close(&gdb->output); fd_close(&gdb->error);
    }
    if (gdb->pid == 0 && gdb->output < 0 && gdb->error < 0) {
        bool fallback = !gdb->closing && !gdb->prompted && gdb->version == 3 &&
                        strstr(gdb->startup_error, "mi3") != NULL &&
                        strstr(gdb->startup_error, "Interpreter") != NULL &&
                        strstr(gdb->startup_error, "unrecognized") != NULL;
        fd_close(&gdb->input);
        if (fallback) {
            diagnostic(gdb, "MI3 interpreter unavailable; retrying MI2 before target connection", 0);
            tb_mi_parser_destroy(&gdb->parser);
            gdb->version = 2;
            gdb->startup_deadline = tb_gdb_now_ms() + gdb->startup_ms;
            (void)spawn_peer(gdb);
        } else {
            if (!gdb->closing) { diagnostic(gdb, "debugger exited or closed its output", 0); }
            cancel_all(gdb);
            tb_mi_parser_destroy(&gdb->parser);
            state_set(gdb, TB_GDB_DISCONNECTED);
        }
    } else if (!gdb->closing && gdb->output < 0 && gdb->prompted) {
        broken(gdb, "debugger MI output reached EOF", EPIPE);
    }
    if (gdb->state != TB_GDB_DISCONNECTED) { deadlines(gdb); }
}

TbGdbState tb_gdb_state(const TbGdb *gdb) { return gdb->state; }
TbGdbGeneration tb_gdb_generation(const TbGdb *gdb) { return gdb->generation; }
pid_t tb_gdb_pid(const TbGdb *gdb) { return gdb->pid; }
int tb_gdb_mi_version(const TbGdb *gdb) { return gdb->version; }
size_t tb_gdb_pending(const TbGdb *gdb)
{
    size_t count = 0;
    for (size_t i = 0; i < TB_GDB_MAX_PENDING; ++i) { count += gdb->requests[i].wire != NULL; }
    return count;
}
