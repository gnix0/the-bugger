#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Linux pidfds/PTY. */
#include "session.h"
#include "native.h"

#include <ctype.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <termios.h>
#include <unistd.h>

typedef enum { JOB_NONE, JOB_SETUP, JOB_RUN, JOB_PAUSE, JOB_CONSOLE,
               JOB_INTERRUPT_CLOSE, JOB_RELEASE, JOB_REMOTE_CONNECT, JOB_REMOTE_UNITS, JOB_REMOTE_WIDTH, JOB_REMOTE_ENDIAN, JOB_REMOTE_FRAME, JOB_REMOTE_DISCONNECT, JOB_TARGET, JOB_CLOSE_POLICY, JOB_TARGET_FLUSH } Job;
struct TbSession {
    bool setup_active, setup_unacknowledged;
    TbGdb *gdb;
    TbOpenOcd *server;
    bool close_resume, close_prepared, target_uncertain;
    int target_action; uint64_t target_deadline;
    TbSessionKind kind;
    TbSessionCapabilities capabilities;
    bool target_connected, remote_metadata_dirty, close_interrupt_sent;
    char endpoint[256];
    TbMemoryRegion ordinary_regions[TB_MEMORY_REGIONS];
    size_t ordinary_region_count;
    TbContext *context;
    TbCode *code;
    TbBreakpoints *breakpoints;
    TbRegisters *registers;
    TbMemory *memory;
    TbVariables *variables;
    TbSessionState state;
    bool attach, configured, waiting, completed, success, want_close, failed, reply_cancelled;
    bool alive, launch_requested, owned, recovery_sent;
    Job job;
    int stage, master, slave, pidfd;
    pid_t target_pid, owned_pid;
    uint64_t revision, close_deadline, partial_time[5], recovery_deadline;
    uint64_t mmio_configuration;
    pid_t memory_pid;
    uint64_t memory_stop;
    char executable[PATH_MAX], directory[PATH_MAX], debugger[PATH_MAX], tty[PATH_MAX];
    TbSessionWords arguments, environment;
    char message[TB_SESSION_LOG_WIDTH], reply_error[TB_SESSION_LOG_WIDTH];
    char code_message[192];
    char break_message[192];
    char memory_message[192];
    char logs[TB_SESSION_LOG_LINES][TB_SESSION_LOG_WIDTH];
    size_t first, count;
    char partial[5][TB_SESSION_LOG_WIDTH - 24];
    size_t partial_length[5];
};

static void safe_text(char *output, size_t capacity, const char *bytes, size_t length)
{
    size_t at = 0;
    for (size_t i = 0; i < length && at + 1 < capacity; ++i) {
        unsigned char c = (unsigned char)bytes[i];
        if (c >= 32 && c < 127) { output[at++] = (char)c; }
        else if (at + 4 < capacity) {
            static const char hex[] = "0123456789abcdef";
            output[at++] = '\\'; output[at++] = 'x';
            output[at++] = hex[c / 16]; output[at++] = hex[c % 16];
        } else { break; }
    }
    output[at] = '\0';
}

static void message(TbSession *native, const char *text)
{
    safe_text(native->message, sizeof native->message, text, strlen(text));
    ++native->revision;
}

static void line(TbSession *native, const char *prefix, const char *text)
{
    size_t slot = (native->first + native->count) % TB_SESSION_LOG_LINES;
    if (native->count == TB_SESSION_LOG_LINES) { native->first = (native->first + 1) % TB_SESSION_LOG_LINES; }
    else { ++native->count; }
    char row[TB_SESSION_LOG_WIDTH];
    (void)snprintf(row, sizeof row, "%.20s%.234s", prefix, text);
    memcpy(native->logs[slot], row, strlen(row) + 1);
    ++native->revision;
}

static void server_log(void *context, const char *text)
{ line(context, "[openocd] ", text); }

static void diagnostic(TbSession *native, const char *text)
{
    message(native, text);
    line(native, "[session] ", native->message);
}

static void partial_flush(TbSession *native, size_t channel)
{
    static const char *const prefixes[] = {"[gdb] ", "[target] ", "[gdb-log] ", "[gdb-stderr] ", "[program] "};
    if (native->partial_length[channel] != 0) {
        native->partial[channel][native->partial_length[channel]] = '\0';
        line(native, prefixes[channel], native->partial[channel]);
        native->partial_length[channel] = 0;
    }
}

static void stream(TbSession *native, size_t channel, const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] == '\n') { partial_flush(native, channel); continue; }
        char escaped[5] = {0}; safe_text(escaped, sizeof escaped, bytes + i, 1);
        size_t count = strnlen(escaped, sizeof escaped - 1);
        if (native->partial_length[channel] + count >= sizeof native->partial[channel]) { partial_flush(native, channel); }
        memcpy(native->partial[channel] + native->partial_length[channel], escaped, count);
        native->partial_length[channel] += count;
    }
    native->partial_time[channel] = tb_gdb_now_ms();
}

static void state_set(TbSession *native, TbSessionState state)
{
    if (native->state != state) { native->state = state; ++native->revision; }
}

static void configuration_changed(TbSession *session)
{
    ++session->mmio_configuration;
    (void)tb_memory_backend_set(session->memory, NULL);
    tb_code_image_invalidate(session->code);
}

bool tb_session_words(const char *input, TbSessionWords *words, char *error, size_t capacity)
{
    if (input == NULL || words == NULL || error == NULL || capacity == 0) { return false; }
    memset(words, 0, sizeof *words);
    size_t at = 0;
    while (input[at] != '\0') {
        while (isspace((unsigned char)input[at])) { ++at; }
        if (input[at] == '\0') { break; }
        if (words->count == TB_SESSION_WORDS) { (void)snprintf(error, capacity, "At most %d arguments/environment entries.", TB_SESSION_WORDS); return false; }
        char quote = 0; size_t used = 0;
        while (input[at] != '\0' && (quote != 0 || !isspace((unsigned char)input[at]))) {
            char c = input[at++];
            if ((c == '\'' || c == '"') && (quote == 0 || quote == c)) {
                if (quote == 0) { quote = c; }
                else { quote = '\0'; }
                continue;
            }
            if (c == '\\' && quote != '\'') {
                if (input[at] == '\0') { (void)snprintf(error, capacity, "Trailing backslash in arguments."); return false; }
                c = input[at++];
            }
            if (used + 1 == TB_SESSION_WORD_BYTES) { (void)snprintf(error, capacity, "Argument/environment entry too long."); return false; }
            words->values[words->count][used++] = c;
        }
        if (quote != 0) { (void)snprintf(error, capacity, "Unclosed quote in arguments/environment."); return false; }
        words->values[words->count++][used] = '\0';
    }
    error[0] = '\0';
    return true;
}

static bool mi_string(const TbMiValue *value, const char *text)
{
    return value != NULL && value->type == TB_MI_STRING &&
           value->string.length == strlen(text) && memcmp(value->string.data, text, value->string.length) == 0;
}

static pid_t record_pid(const TbMiValue *value)
{
    if (value == NULL || value->type != TB_MI_STRING || value->string.length == 0) { return 0; }
    uint64_t number = 0;
    for (size_t i = 0; i < value->string.length; ++i) {
        unsigned char c = (unsigned char)value->string.data[i];
        if (c < '0' || c > '9' || number > ((uint64_t)INT_MAX - (c - '0')) / 10) { return 0; }
        number = number * 10 + c - '0';
    }
    return (pid_t)number;
}

static void emit(void *context, const TbGdbEvent *event)
{
    TbSession *native = context;
    ++native->revision;
    if (tb_break_event(native->breakpoints, event)) { return; }
    if (tb_registers_event(native->registers, event)) { return; }
    if (tb_memory_event(native->memory, event)) { return; }
    if (tb_variables_event(native->variables, event)) { return; }
    if (tb_code_event(native->code, event)) { return; }
    if (tb_context_event(native->context, event)) { return; }
    if (event->kind == TB_GDB_RECONCILED && native->target_action == 4) {
        native->target_action = 0; native->target_uncertain = false;
        diagnostic(native, "OpenOCD command completed; stopped state verified through GDB. Target script behavior remains backend-dependent.");
    }
    if (event->kind == TB_GDB_DIAGNOSTIC) {
        char text[TB_SESSION_LOG_WIDTH], safe[160];
        safe_text(safe, sizeof safe, event->text, event->length);
        (void)snprintf(text, sizeof text, "%s%s%s", safe, event->status ? ": " : "", event->status ? strerror(event->status) : "");
        diagnostic(native, text);
    } else if (event->kind == TB_GDB_STDERR) { stream(native, 3, event->text, event->length); }
    else if (event->kind == TB_GDB_REPLY || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED) {
        native->waiting = false; native->completed = true;
        native->reply_cancelled = event->kind == TB_GDB_CANCELLED;
        native->success = event->kind == TB_GDB_REPLY &&
                          (strcmp(event->record->klass, "done") == 0 || strcmp(event->record->klass, "running") == 0 ||
                           (native->job == JOB_REMOTE_CONNECT && strcmp(event->record->klass, "connected") == 0));
        if (native->success && native->job >= JOB_REMOTE_UNITS && native->job <= JOB_REMOTE_FRAME) {
            const TbMiValue *value = tb_mi_find(event->record->results, "value");
            if (native->job == JOB_REMOTE_UNITS) { native->capabilities.octet_addressing = mi_string(value, "0"); }
            else if (native->job == JOB_REMOTE_WIDTH) {
                native->capabilities.address_bits = mi_string(value, "4") ? 32 : mi_string(value, "8") ? 64 : 0;
            } else if (native->job == JOB_REMOTE_ENDIAN) {
                native->capabilities.byte_order = mi_string(value, "little") ? TB_MEMORY_LITTLE :
                                                  mi_string(value, "big") ? TB_MEMORY_BIG : TB_MEMORY_UNKNOWN;
            } else if (native->job == JOB_REMOTE_FRAME) {
                const TbMiValue *frame = tb_mi_find(event->record->results, "frame");
                const TbMiValue *arch = frame ? tb_mi_find(frame->children, "arch") : NULL;
                if (arch && arch->type == TB_MI_STRING) {
                    safe_text(native->capabilities.architecture, sizeof native->capabilities.architecture, arch->string.data, arch->string.length);
                }
            }
        }
        native->reply_error[0] = '\0';
        if (!native->success) {
            const TbMiValue *value = event->record ? tb_mi_find(event->record->results, "msg") : NULL;
            if (value != NULL && value->type == TB_MI_STRING) {
                safe_text(native->reply_error, sizeof native->reply_error, value->string.data, value->string.length);
            } else { (void)snprintf(native->reply_error, sizeof native->reply_error, "%s", event->kind == TB_GDB_TIMEOUT ? "GDB operation timed out; it was not retried." : "GDB operation was cancelled."); }
        }
    } else if (event->kind == TB_GDB_STATE && native->configured && !native->want_close) {
        TbGdbState state = tb_gdb_state(native->gdb);
        if (state == TB_GDB_RUNNING) { state_set(native, TB_SESSION_RUNNING); }
        else if (state == TB_GDB_STOPPED) { state_set(native, TB_SESSION_STOPPED); }
        else if (state == TB_GDB_EXITED) { state_set(native, TB_SESSION_EXITED); }
    }
    if (event->record == NULL) { return; }
    const TbMiRecord *record = event->record;
    if (record->kind == TB_MI_CONSOLE || record->kind == TB_MI_TARGET || record->kind == TB_MI_LOG) {
        stream(native, record->kind == TB_MI_CONSOLE ? 0 : record->kind == TB_MI_TARGET ? 1 : 2,
               record->stream.data, record->stream.length);
    } else if (record->kind == TB_MI_NOTIFY && strcmp(record->klass, "thread-group-started") == 0) {
        configuration_changed(native);
        if (native->kind == TB_SESSION_LOCAL) {
            native->target_pid = record_pid(tb_mi_find(record->results, "pid"));
            native->alive = native->target_pid > 0;
        } else {
            const TbMiValue *id = tb_mi_find(record->results, "pid");
            if (id && id->type == TB_MI_STRING) {
                safe_text(native->capabilities.target_id, sizeof native->capabilities.target_id, id->string.data, id->string.length);
            }
            native->target_pid = 0; native->alive = true;
            if (native->configured) { native->remote_metadata_dirty = true; }
        }
        if (native->launch_requested && !native->attach && native->target_pid > 0) {
            native->owned = true; native->owned_pid = native->target_pid;
            if (native->pidfd >= 0) { (void)close(native->pidfd); }
            native->pidfd = (int)syscall(SYS_pidfd_open, native->target_pid, 0);
            if (native->pidfd < 0) { diagnostic(native, "Cannot open inferior pidfd; crash cleanup cannot be guaranteed."); }
        }
        native->launch_requested = false;
    } else if (record->kind == TB_MI_NOTIFY && strcmp(record->klass, "thread-group-exited") == 0) {
        configuration_changed(native);
        native->alive = false;
        if (native->kind != TB_SESSION_LOCAL) {
            /* A detached/exited inferior is not proof that its server died.
             * Revoke read evidence before any further model work. */
            native->ordinary_region_count = 0;
            tb_memory_target(native->memory, false, 0);
            tb_memory_regions(native->memory, NULL, 0);
            if (!native->want_close) {
                state_set(native, TB_SESSION_READY);
                diagnostic(native, "Remote inferior unavailable. Reconnect or explicitly attach through Console; target state is unverified.");
            }
        }
        if (!native->want_close) {
            /* Detach notifications do not prove process death. Respect a
             * deliberate console detach instead of retaining kill ownership. */
            native->owned = false;
            if (native->pidfd >= 0) { (void)close(native->pidfd); native->pidfd = -1; }
        }
    } else if (record->kind == TB_MI_EXEC && strcmp(record->klass, "stopped") == 0) {
        const TbMiValue *reason = tb_mi_find(record->results, "reason");
        char text[192] = "Target stopped";
        if (reason != NULL && reason->type == TB_MI_STRING) { safe_text(text, sizeof text, reason->string.data, reason->string.length); }
        diagnostic(native, text);
        if (mi_string(reason, "exited-normally") || mi_string(reason, "exited") || mi_string(reason, "exited-signalled")) {
            native->alive = false;
            if (!native->want_close) { state_set(native, TB_SESSION_EXITED); }
        } else if (!native->want_close && (native->configured || native->kind == TB_SESSION_LOCAL)) { state_set(native, TB_SESSION_STOPPED); }
    }
}

TbSession *tb_session_create(void)
{
    TbSession *native = calloc(1, sizeof *native);
    if (native == NULL) { return NULL; }
    native->master = native->slave = native->pidfd = -1;
    native->gdb = tb_gdb_create(emit, native);
    if (native->gdb == NULL) { free(native); return NULL; }
    native->context = tb_context_create(native->gdb);
    if (native->context == NULL) { (void)tb_gdb_destroy(native->gdb); free(native); return NULL; }
    native->code = tb_code_create(native->gdb, native->context);
    if (native->code == NULL) {
        tb_context_destroy(native->context); (void)tb_gdb_destroy(native->gdb); free(native); return NULL;
    }
    native->breakpoints = tb_break_create(native->gdb);
    if (!native->breakpoints) {
        tb_code_destroy(native->code); tb_context_destroy(native->context);
        (void)tb_gdb_destroy(native->gdb); free(native); return NULL;
    }
    native->registers = tb_registers_create(native->gdb, native->context);
    if (!native->registers) {
        tb_break_destroy(native->breakpoints); tb_code_destroy(native->code);
        tb_context_destroy(native->context); (void)tb_gdb_destroy(native->gdb); free(native); return NULL;
    }
    native->memory = tb_memory_create(native->gdb, native->context);
    if (!native->memory) {
        tb_registers_destroy(native->registers); tb_break_destroy(native->breakpoints); tb_code_destroy(native->code);
        tb_context_destroy(native->context); (void)tb_gdb_destroy(native->gdb); free(native); return NULL;
    }
    native->variables = tb_variables_create(native->gdb, native->context);
    tb_memory_cache(native->memory, tb_code_cache(native->code));
    if (!native->variables) {
        tb_memory_destroy(native->memory); tb_registers_destroy(native->registers);
        tb_break_destroy(native->breakpoints); tb_code_destroy(native->code);
        tb_context_destroy(native->context); (void)tb_gdb_destroy(native->gdb); free(native); return NULL;
    }
    native->server = tb_openocd_create(server_log, native);
    if (!native->server) { (void)tb_session_destroy(native); return NULL; }
    return native;
}

static void close_fds(TbSession *native)
{
    if (native->master >= 0) { (void)close(native->master); native->master = -1; }
    if (native->slave >= 0) { (void)close(native->slave); native->slave = -1; }
    if (native->pidfd >= 0) { (void)close(native->pidfd); native->pidfd = -1; }
}

bool tb_session_destroy(TbSession *native)
{
    if (native == NULL) { return true; }
    if (tb_session_active(native) || tb_gdb_pid(native->gdb) || tb_gdb_pending(native->gdb)) { return false; }
    if (!tb_openocd_destroy(native->server)) { return false; }
    native->server = NULL;
    /* Memory releases its MMIO reservation through the still-live controller.
     * Disconnected, reaped and drained above means destroy cannot emit replies. */
    tb_memory_destroy(native->memory); native->memory = NULL;
    if (!tb_gdb_destroy(native->gdb)) { return false; }
    tb_variables_destroy(native->variables); tb_registers_destroy(native->registers); tb_break_destroy(native->breakpoints); tb_code_destroy(native->code); tb_context_destroy(native->context); close_fds(native); free(native); return true;
}

static bool elf(const char *path, bool executable)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) { return false; }
    unsigned char magic[4]; struct stat info;
    bool valid = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && read(fd, magic, sizeof magic) == 4 &&
                 memcmp(magic, "\177ELF", 4) == 0;
    (void)close(fd);
    return valid && (!executable || access(path, X_OK) == 0);
}

static bool copy_path(char *target, size_t size, const char *source)
{
    if (source == NULL) { source = ""; }
    size_t length = strlen(source);
    if (length >= size) { return false; }
    memcpy(target, source, length + 1); return true;
}

bool tb_session_start(TbSession *native, const TbSessionConfig *config)
{
    if (native == NULL || config == NULL || tb_session_active(native)) { return false; }
    native->failed = false;
    if (config->kind < TB_SESSION_LOCAL || config->kind > TB_SESSION_EXTENDED_REMOTE) { return false; }
    if (config->kind != TB_SESSION_LOCAL && !tb_session_endpoint(config->endpoint)) {
        diagnostic(native, "Use a TCP host:port endpoint (IPv6 in brackets); commands and device paths are not accepted."); return false;
    }
    if (config->ordinary_region_count > TB_MEMORY_REGIONS || (config->ordinary_region_count && !config->ordinary_regions)) { return false; }
    for (size_t i = 0; i < config->ordinary_region_count; ++i) {
        if (config->ordinary_regions[i].begin >= config->ordinary_regions[i].end) { return false; }
    }
    if (config->openocd && config->kind == TB_SESSION_LOCAL) { return false; }
    if (config->openocd && config->openocd->start_server &&
        (strnlen(config->openocd->endpoint, sizeof config->openocd->endpoint) == sizeof config->openocd->endpoint ||
         strcmp(config->endpoint, config->openocd->endpoint) != 0)) {
        diagnostic(native, "GDB endpoint must match the reviewed owned-server plan."); return false;
    }
    native->kind = config->kind;
    if (!copy_path(native->endpoint, sizeof native->endpoint, config->endpoint)) { return false; }
    if (!copy_path(native->executable, sizeof native->executable, config->executable) ||
        !copy_path(native->directory, sizeof native->directory, config->directory) ||
        !copy_path(native->debugger, sizeof native->debugger, config->gdb && *config->gdb ? config->gdb : "gdb")) {
        diagnostic(native, "Path is too long."); return false;
    }
    native->attach = config->attach || config->kind != TB_SESSION_LOCAL;
    native->target_pid = config->kind == TB_SESSION_LOCAL ? config->pid : 0;
    if ((config->kind == TB_SESSION_LOCAL && config->attach && (config->pid <= 1 || config->pid == getpid() ||
                           (kill(config->pid, 0) != 0 && errno == ESRCH))) ||
        (config->kind == TB_SESSION_LOCAL && !config->attach && !elf(native->executable, true)) ||
        ((config->attach || config->kind != TB_SESSION_LOCAL) && native->executable[0] && !elf(native->executable, false))) {
        diagnostic(native, config->attach ? "PID is absent/invalid or symbols are not a readable ELF." : "Choose a readable executable ELF (paths may include spaces)."); return false;
    }
    if (native->executable[0]) {
        char resolved[PATH_MAX];
        if (realpath(native->executable, resolved) == NULL) { diagnostic(native, "Cannot resolve ELF path."); return false; }
        (void)copy_path(native->executable, sizeof native->executable, resolved);
    }
    if (native->directory[0]) {
        struct stat info;
        if (stat(native->directory, &info) != 0 || !S_ISDIR(info.st_mode) || access(native->directory, X_OK) != 0) {
            diagnostic(native, "Working directory is absent or inaccessible."); return false;
        }
    }
    if (!config->attach && ((config->arguments && config->arguments->count > TB_SESSION_WORDS) ||
        (config->environment && config->environment->count > TB_SESSION_WORDS))) { diagnostic(native, "Too many arguments/environment entries."); return false; }
    native->arguments = !config->attach && config->arguments ? *config->arguments : (TbSessionWords){0};
    native->environment = !config->attach && config->environment ? *config->environment : (TbSessionWords){0};
    for (size_t i = 0; i < native->arguments.count; ++i) {
        if (memchr(native->arguments.values[i], '\0', TB_SESSION_WORD_BYTES) == NULL) { diagnostic(native, "Argument is not terminated."); return false; }
    }
    for (size_t i = 0; i < native->environment.count; ++i) {
        const char *value = native->environment.values[i];
        if (memchr(value, '\0', TB_SESSION_WORD_BYTES) == NULL) { diagnostic(native, "Environment entry is not terminated."); return false; }
        if (strchr(value, '\n') || strchr(value, '\r')) { diagnostic(native, "Environment entries cannot contain record delimiters."); return false; }
        size_t at = 0;
        while ((value[at] >= 'A' && value[at] <= 'Z') || (value[at] >= 'a' && value[at] <= 'z') ||
               value[at] == '_' || (at != 0 && value[at] >= '0' && value[at] <= '9')) { ++at; }
        if (at == 0 || value[at] != '=') { diagnostic(native, "Environment entries must be NAME=value; no shell syntax."); return false; }
    }
    close_fds(native);
    native->first = native->count = 0;
    memset(native->partial_length, 0, sizeof native->partial_length);
    native->setup_active = native->setup_unacknowledged = false;
    native->want_close = native->configured = native->waiting = native->completed = false;
    native->alive = native->owned = native->launch_requested = native->recovery_sent = false;
    native->stage = 0; native->job = JOB_NONE;
    native->target_connected = native->remote_metadata_dirty = false;
    native->close_interrupt_sent = native->close_resume = native->close_prepared = native->target_uncertain = false;
    native->target_action = 0; native->target_uncertain = false;
    configuration_changed(native);
    memset(&native->capabilities, 0, sizeof native->capabilities);
    native->capabilities.local_process = native->kind == TB_SESSION_LOCAL;
    native->capabilities.openocd = config->openocd != NULL;
    (void)copy_path(native->capabilities.symbol_image, sizeof native->capabilities.symbol_image, native->executable);
    native->ordinary_region_count = config->ordinary_region_count;
    if (config->ordinary_region_count) {
        memcpy(native->ordinary_regions, config->ordinary_regions, config->ordinary_region_count * sizeof native->ordinary_regions[0]);
    }
    native->code_message[0] = '\0';
    native->break_message[0] = '\0';
    native->memory_message[0] = '\0';
    tb_context_reset(native->context);
    tb_code_reset(native->code);
    tb_code_native(native->code, 0, native->executable);
    tb_break_reset(native->breakpoints);
    tb_registers_reset(native->registers);
    tb_memory_reset(native->memory);
    tb_variables_reset(native->variables);
    native->memory_pid = 0; native->memory_stop = 0;
    tb_memory_target(native->memory, native->kind == TB_SESSION_LOCAL, 64);
    if (!native->attach) {
        native->master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
        bool ready = native->master >= 0 && grantpt(native->master) == 0 && unlockpt(native->master) == 0 &&
                     ptsname_r(native->master, native->tty, sizeof native->tty) == 0;
        if (ready) { native->slave = open(native->tty, O_RDWR | O_NOCTTY | O_CLOEXEC); }
        if (!ready || native->slave < 0) {
            diagnostic(native, "Cannot create inferior output PTY."); close_fds(native); return false;
        }
        struct termios terminal;
        if (tcgetattr(native->slave, &terminal) != 0) {
            diagnostic(native, "Cannot inspect inferior output PTY."); close_fds(native); return false;
        }
        terminal.c_oflag &= ~(tcflag_t)ONLCR;
        if (tcsetattr(native->slave, TCSANOW, &terminal) != 0) {
            diagnostic(native, "Cannot configure inferior output PTY."); close_fds(native); return false;
        }
    }
    state_set(native, TB_SESSION_STARTING);
    diagnostic(native, native->kind != TB_SESSION_LOCAL ? (config->openocd && config->openocd->start_server ? "Starting owned OpenOCD and GDB; no automatic flash, reset or resume commands." : "Starting GDB for an existing server; no server process is launched.") : native->attach ? "Starting GDB to attach; ptrace permissions will be checked by GDB." : "Starting GDB; Continue will run the loaded executable.");
    if (config->openocd && config->openocd->start_server) {
        if (!tb_openocd_start(native->server, config->openocd)) {
            diagnostic(native, tb_openocd_message(native->server)); state_set(native, TB_SESSION_FAILED); return false;
        }
        native->capabilities.server_owned = true;
    }
    if (!tb_gdb_start(native->gdb, native->debugger, 3000)) {
        native->failed = true; tb_openocd_stop(native->server); state_set(native, TB_SESSION_FAILED); close_fds(native); return false;
    }
    return true;
}

static bool issue(TbSession *native, Job job, const char *operation, const char *const args[], size_t count)
{
    native->job = job; native->completed = false;
    native->waiting = tb_gdb_request(native->gdb, operation, args, count, TB_GDB_SESSION, 1, 3000, NULL);
    if (!native->waiting) { diagnostic(native, "GDB request rejected; session may be closing."); }
    if (!native->waiting && job == JOB_SETUP) { native->failed = true; tb_session_close(native); }
    return native->waiting;
}
static bool register_read_pending(TbSession *native)
{
    if (!tb_memory_register_busy(native->memory)) { return false; }
    diagnostic(native, "Wait for the explicit register read to finish before changing debugger state."); return true;
}

void tb_session_setup_interlock(TbSession *s, bool active, bool unacknowledged)
{
    s->setup_active = active; s->setup_unacknowledged = unacknowledged;
    tb_context_interlock(s->context, active); tb_break_interlock(s->breakpoints, active);
}
static bool setup_blocked(TbSession *s, bool execution)
{
    if (!s->setup_active && !(execution && s->setup_unacknowledged)) { return false; }
    diagnostic(s, s->setup_active ? "Wait for active setup restoration; Pause and close remain available." :
               "Acknowledge unresolved enabled setup breakpoints in Setup before execution."); return true;
}
bool tb_session_continue(TbSession *native)
{
    if (setup_blocked(native, true)) { return false; }
    if (register_read_pending(native)) { return false; }
    if (tb_code_restoring(native->code)) { diagnostic(native, "Wait for profile source settings to finish applying."); return false; }
    if (native->waiting || native->want_close || native->target_action || !native->configured || tb_break_busy(native->breakpoints)) { return false; }
    tb_context_refresh(native->context);
    if (native->state == TB_SESSION_READY && native->kind != TB_SESSION_LOCAL) {
        diagnostic(native, "No stopped remote inferior. Use Console for an explicit remote launch/attach."); return false;
    }
    if (native->state == TB_SESSION_READY) {
        native->launch_requested = native->kind == TB_SESSION_LOCAL && !native->attach;
        return issue(native, JOB_RUN, "-exec-run", NULL, 0);
    }
    return native->state == TB_SESSION_STOPPED && issue(native, JOB_RUN, "-exec-continue", NULL, 0);
}

bool tb_session_pause(TbSession *native)
{
    if (register_read_pending(native)) { return false; }
    return !native->waiting && !native->want_close && native->state == TB_SESSION_RUNNING &&
           issue(native, JOB_PAUSE, "-exec-interrupt", NULL, 0);
}

bool tb_session_step(TbSession *native, TbStep step, bool instruction)
{
    if (setup_blocked(native, true)) { return false; }
    if (register_read_pending(native)) { return false; }
    if (tb_code_restoring(native->code)) { diagnostic(native, "Wait for profile source settings to finish applying."); return false; }
    const TbContextSnapshot *context = tb_context_snapshot(native->context);
    if (native->waiting || native->want_close || native->target_action || !native->configured || tb_break_busy(native->breakpoints) ||
        native->state != TB_SESSION_STOPPED || context->stale || !context->ready ||
        !*context->selected_thread || step < TB_STEP_OVER || step > TB_STEP_OUT) {
        diagnostic(native, "Stepping needs a fresh stopped context. Use Continue/Pause or Console start first."); return false;
    }
    const char *operation = step == TB_STEP_OUT ? "-exec-finish" :
                            step == TB_STEP_INTO ? (instruction ? "-exec-step-instruction" : "-exec-step") :
                            instruction ? "-exec-next-instruction" : "-exec-next";
    const char *const args[] = {"--thread", context->selected_thread, "--frame", "0"};
    tb_context_refresh(native->context);
    return issue(native, JOB_RUN, operation, args, 4);
}

bool tb_session_console(TbSession *native, const char *command)
{
    if (setup_blocked(native, true)) { return false; }
    if (register_read_pending(native)) { return false; }
    if (tb_code_restoring(native->code)) { diagnostic(native, "Wait for profile source settings to finish applying."); return false; }
    if (tb_break_busy(native->breakpoints)) { diagnostic(native, "Wait for the pending breakpoint operation."); return false; }
    if (native->waiting || native->want_close || native->target_action || !native->configured) {
        diagnostic(native, "Wait for the current operation and a ready session before sending a command."); return false;
    }
    if (command == NULL || !*command) { diagnostic(native, "Enter a GDB console command."); return false; }
    if (strchr(command, '\n') || strchr(command, '\r')) { diagnostic(native, "Enter one GDB command without newlines."); return false; }
    /* Keep the MI interpreter under frontend control; manual target operations
     * still travel through MI and remain deliberate user operations. */
    if (strncmp(command, "interpreter", 11) == 0 || strncmp(command, "set mi-async", 12) == 0 ||
        strncmp(command, "set non-stop", 12) == 0) { diagnostic(native, "Interpreter/async mode is managed by the frontend."); return false; }
    char safe[192]; safe_text(safe, sizeof safe, command, strlen(command)); line(native, "[command] ", safe);
    const char *const args[] = {"console", command};
    bool accepted = issue(native, JOB_CONSOLE, "-interpreter-exec", args, 2);
    if (accepted) {
        /* Request acceptance only queues bytes; transmission happens in the
         * next GDB pump. Invalidate before that pump, even for harmless-looking
         * aliases/scripts. Setup confirmation and per-view MMIO consent both
         * expire; retained range definitions are not permission to read. */
        configuration_changed(native);
        tb_context_refresh(native->context);
    }
    const char *verb = command;
    while (*verb == ' ' || *verb == '\t') { ++verb; }
    size_t length = strcspn(verb, " \t");
    bool launch = (length > 0 && length <= 3 && strncmp(verb, "run", length) == 0) ||
                  (length == 5 && strncmp(verb, "start", length) == 0) ||
                  (length == 6 && strncmp(verb, "starti", length) == 0);
    if (accepted && launch && !native->attach) { native->launch_requested = true; }
    return accepted;
}

static bool mmio_review_ready(const TbSession *s)
{
    return s && s->kind != TB_SESSION_LOCAL && s->configured && !s->remote_metadata_dirty && !s->waiting && !s->want_close &&
           !s->target_action && !tb_memory_register_busy(s->memory) && s->capabilities.connected &&
           s->capabilities.octet_addressing && s->capabilities.address_bits == 32 &&
           tb_gdb_state(s->gdb) == TB_GDB_STOPPED;
}

bool tb_session_mmio_review(TbSession *s, TbMemoryBackend *review)
{
    if (!review || !mmio_review_ready(s)) { return false; }
    TbMemoryBackend candidate = {.target = "stm32f4x.cpu", .device = "STM32F446 / Cortex-M4 / ST-LINK",
        .firmware = "ST-LINK V2J48M35 (API v2)", .configuration = s->mmio_configuration,
        .widths = (unsigned)TB_MEMORY_WIDTH8 | (unsigned)TB_MEMORY_WIDTH16 | (unsigned)TB_MEMORY_WIDTH32};
    int n = snprintf(candidate.server, sizeof candidate.server,
        "OpenOCD 0.12.0-01004-g9ea7f3d64-dirty; -f board/st_nucleo_f4.cfg; GDB %s", s->endpoint);
    if (n < 0 || (size_t)n >= sizeof candidate.server || !tb_memory_backend_set(s->memory, &candidate)) { return false; }
    *review = candidate; return true;
}

bool tb_session_mmio_confirm(TbSession *s, const TbMemoryBackend *review)
{
    return review && mmio_review_ready(s) && review->configuration == s->mmio_configuration &&
           tb_memory_backend_confirm(s->memory, review);
}

bool tb_session_save_log(TbSession *native, const char *path)
{
    if (path == NULL || !*path) { diagnostic(native, "Choose a log destination path."); return false; }
    for (size_t i = 0; i < 5; ++i) { partial_flush(native, i); }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) {
        char text[256]; (void)snprintf(text, sizeof text, "Cannot create log (existing files are preserved): %s", strerror(errno)); diagnostic(native, text); return false;
    }
    bool okay = true;
    for (size_t i = 0; i < native->count && okay; ++i) {
        const char *text = tb_session_log_line(native, i); size_t at = 0, length = strlen(text);
        while (at < length) {
            ssize_t written = write(fd, text + at, length - at);
            if (written < 0 && errno == EINTR) { continue; }
            if (written <= 0) { okay = false; break; }
            at += (size_t)written;
        }
        if (okay && write(fd, "\n", 1) != 1) { okay = false; }
    }
    if (close(fd) != 0) { okay = false; }
    diagnostic(native, okay ? "Log saved (bounded retained history only)." : "Log write failed; destination may contain a partial log.");
    return okay;
}

void tb_session_close(TbSession *native)
{
    if (!tb_session_active(native)) { return; }
    if (!native->want_close) {
        native->close_deadline = tb_gdb_now_ms() + 4000;
        configuration_changed(native);
    }
    native->want_close = true;
    state_set(native, TB_SESSION_CLOSING);
    if (native->kind != TB_SESSION_LOCAL && !native->configured && !native->target_connected) {
        tb_gdb_abort(native->gdb);
    }
}

static bool target_action(TbSession *s, int action)
{
    if (action != 1 && setup_blocked(s, true)) { return false; }
    if (register_read_pending(s)) { return false; }
    if (!s->capabilities.openocd || !s->configured || (action != 1 && tb_code_restoring(s->code)) || tb_break_busy(s->breakpoints) || s->waiting || s->want_close || s->target_action ||
        (s->state != TB_SESSION_STOPPED && s->state != TB_SESSION_RUNNING)) {
        diagnostic(s, "OpenOCD action unavailable; connect a target and wait for pending operations."); return false;
    }
    s->target_action = action; s->target_deadline = tb_gdb_now_ms() + 10000;
    tb_context_refresh(s->context);
    if (s->state == TB_SESSION_RUNNING && !issue(s, JOB_PAUSE, "-exec-interrupt", NULL, 0)) { s->target_action = 0; return false; }
    return true;
}
bool tb_session_reset_halt(TbSession *s) { return target_action(s, 2); }
bool tb_session_halt(TbSession *s) { return target_action(s, 1); }
void tb_session_disconnect(TbSession *s, bool resume)
{
    if (s->capabilities.openocd) { s->close_resume = resume; }
    tb_session_close(s);
}

static void remote_setup(TbSession *session);
static void setup(TbSession *native)
{
    if (native->kind != TB_SESSION_LOCAL) { remote_setup(native); return; }
    const char *const confirm[] = {"confirm", "off"};
    const char *const shell[] = {"startup-with-shell", "off"};
    const char *const autoload[] = {"auto-load", "off"};
    const char *const page[] = {"pagination", "off"};
    const char *const frame_args[] = {"print", "frame-arguments", "none"};
    const char *const no_random[] = {"disable-randomization", "off"};
    const char *const path[] = {native->executable};
    const char *const cwd[] = {native->directory};
    const char *const tty[] = {native->tty};
    if (native->stage < 6) {
        const char *const *settings[] = {confirm, shell, autoload, page, no_random, frame_args};
        (void)issue(native, JOB_SETUP, "-gdb-set", settings[native->stage], native->stage == 5 ? 3 : 2);
    } else if (native->stage == 6) {
        if (*native->directory) { (void)issue(native, JOB_SETUP, "-environment-cd", cwd, 1); }
        else { ++native->stage; }
    } else if (native->stage == 7) {
        if (*native->executable) { (void)issue(native, JOB_SETUP, "-file-exec-and-symbols", path, 1); }
        else { ++native->stage; }
    } else if (native->stage == 8 && !native->attach) { (void)issue(native, JOB_SETUP, "-inferior-tty-set", tty, 1); }
    else if (native->stage == 9 && !native->attach) {
        const char *args[TB_SESSION_WORDS];
        for (size_t i = 0; i < native->arguments.count; ++i) { args[i] = native->arguments.values[i]; }
        (void)issue(native, JOB_SETUP, "-exec-arguments", args, native->arguments.count);
    } else if (native->stage >= 10 && (size_t)(native->stage - 10) < native->environment.count && !native->attach) {
        const char *const args[] = {"environment", native->environment.values[native->stage - 10]};
        (void)issue(native, JOB_SETUP, "-gdb-set", args, 2);
    } else if (native->stage < 10 + (int)native->environment.count) { ++native->stage; }
    else if (native->kind == TB_SESSION_LOCAL && native->attach && native->stage == 10 + (int)native->environment.count) {
        char pid[32]; (void)snprintf(pid, sizeof pid, "%ld", (long)native->target_pid);
        const char *const args[] = {pid};
        (void)issue(native, JOB_SETUP, "-target-attach", args, 1);
    } else {
        native->configured = true; native->capabilities.connected = true;
        state_set(native, native->attach ? TB_SESSION_STOPPED : TB_SESSION_READY);
        diagnostic(native, native->attach ? "Attached and stopped; exit will detach/resume this process." : "ELF loaded. Continue starts the program.");
    }
}

static void process_reply(TbSession *native)
{
    if (!native->completed) { return; }
    native->completed = false;
    if (!native->success && native->want_close && native->reply_cancelled) {
        native->job = JOB_NONE; return;
    }
    if (!native->success) {
        diagnostic(native, native->reply_error);
        if (native->kind != TB_SESSION_LOCAL && native->job >= JOB_REMOTE_UNITS && native->job <= JOB_REMOTE_FRAME) {
            /* Unsupported metadata stays unknown; never substitute host data. */
            ++native->stage; native->job = JOB_NONE; return;
        }
        if (native->kind != TB_SESSION_LOCAL && (native->job == JOB_SETUP || native->job == JOB_INTERRUPT_CLOSE || native->job == JOB_CLOSE_POLICY || (native->job >= JOB_REMOTE_CONNECT && native->job <= JOB_REMOTE_DISCONNECT))) {
            native->failed = true; native->target_connected = false;
            diagnostic(native, "Remote operation failed; closing only this GDB. Server/target state is unverified.");
            tb_gdb_abort(native->gdb); native->job = JOB_NONE; return;
        }
        if (native->job == JOB_SETUP) {
            if (native->attach) { diagnostic(native, "Attach/setup failed: check PID ownership, Yama ptrace_scope, container policy and capabilities; no permission settings were changed."); }
            if (!native->want_close) { native->failed = true; tb_session_close(native); }
        } else if (native->job == JOB_RELEASE || native->job == JOB_INTERRUPT_CLOSE) {
            diagnostic(native, "Target release was not confirmed; closing debugger with this limitation.");
            tb_gdb_close(native->gdb);
        }
        if ((native->job == JOB_TARGET || native->job == JOB_TARGET_FLUSH || native->job == JOB_PAUSE) && native->target_action) {
            native->target_action = 0; native->alive = false; native->target_uncertain = true;
            tb_memory_target(native->memory, false, 0); tb_memory_regions(native->memory, NULL, 0);
            state_set(native, TB_SESSION_READY);
        }
        if (native->job == JOB_RUN || native->job == JOB_CONSOLE) { native->launch_requested = false; }
    } else if (native->job == JOB_TARGET) {
        native->target_action = 3;
    } else if (native->job == JOB_TARGET_FLUSH) {
        native->target_action = 4; native->target_deadline = tb_gdb_now_ms() + 1500;
        if (!tb_gdb_verify_stop(native->gdb)) { native->target_deadline = 0; }
    } else if (native->job == JOB_CLOSE_POLICY) { native->close_prepared = true;
    } else if (native->job == JOB_REMOTE_DISCONNECT) {
        native->target_connected = native->alive = false;
        native->capabilities.connected = false; tb_gdb_close(native->gdb);
    } else if (native->job == JOB_REMOTE_CONNECT) {
        native->target_connected = true; native->capabilities.connected = true; ++native->stage;
    } else if (native->job >= JOB_REMOTE_UNITS && native->job <= JOB_REMOTE_FRAME) { ++native->stage;
    } else if (native->job == JOB_SETUP) {
        if (native->attach && native->stage == 10 + (int)native->environment.count) { native->alive = true; }
        ++native->stage;
    }
    else if (native->job == JOB_RELEASE) { native->alive = false; tb_gdb_close(native->gdb); }
    else if (native->job == JOB_CONSOLE) {
        if (native->success) { message(native, "GDB command completed."); }
        tb_context_refresh(native->context);
    }
    native->job = JOB_NONE;
}

static void release_target(TbSession *native)
{
    if (native->waiting || tb_memory_register_busy(native->memory) || tb_gdb_state(native->gdb) == TB_GDB_CLOSING) { return; }
    if (native->kind != TB_SESSION_LOCAL) {
        if (native->target_connected) {
            if (native->target_uncertain) {
                native->failed = true; native->target_uncertain = false;
                diagnostic(native, "Target halt/resume disposition remains unverified; releasing connection only.");
            }
            if (tb_gdb_state(native->gdb) == TB_GDB_RUNNING &&
                !(native->capabilities.openocd && native->close_resume && native->close_prepared)) {
                /* ^done acknowledges the request, not the later *stopped.
                 * Wait for that notification without a retry loop. */
                if (!native->close_interrupt_sent) {
                    native->close_interrupt_sent = issue(native, JOB_INTERRUPT_CLOSE, "-exec-interrupt", NULL, 0);
                }
            } else if (native->capabilities.openocd && native->alive && !native->close_prepared) {
                const char *const args[] = {"console", native->close_resume ? "monitor resume" : "monitor halt"};
                tb_context_refresh(native->context); (void)issue(native, JOB_CLOSE_POLICY, "-interpreter-exec", args, 2);
            } else {
                (void)issue(native, JOB_REMOTE_DISCONNECT, "-target-disconnect", NULL, 0);
            }
        } else { tb_gdb_abort(native->gdb); }
        return;
    }
    if (!native->alive) { tb_gdb_close(native->gdb); return; }
    TbGdbState state = tb_gdb_state(native->gdb);
    if (state == TB_GDB_RUNNING) { (void)issue(native, JOB_INTERRUPT_CLOSE, "-exec-interrupt", NULL, 0); }
    else if (state == TB_GDB_STOPPED) {
        if (native->owned && native->target_pid == native->owned_pid) {
            const char *const args[] = {"console", "kill"};
            (void)issue(native, JOB_RELEASE, "-interpreter-exec", args, 2);
        } else { (void)issue(native, JOB_RELEASE, "-target-detach", NULL, 0); }
    }
}

static void memory_metadata(TbSession *native)
{
    uint64_t stop = tb_gdb_generation(native->gdb).stop; pid_t pid = native->target_pid;
    if (pid <= 1 || tb_gdb_state(native->gdb) != TB_GDB_STOPPED ||
        (native->memory_pid == pid && native->memory_stop == stop)) { return; }
    native->memory_pid = pid; native->memory_stop = stop;
    char path[64]; (void)snprintf(path, sizeof path, "/proc/%ld/exe", (long)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK); unsigned char ident[EI_NIDENT];
    ssize_t bytes = fd >= 0 ? read(fd, ident, sizeof ident) : -1;
    if (fd >= 0) { (void)close(fd); }
    TbMemoryOrder order = TB_MEMORY_UNKNOWN;
    if (bytes == EI_NIDENT && memcmp(ident, ELFMAG, SELFMAG) == 0 &&
        (ident[EI_CLASS] == ELFCLASS32 || ident[EI_CLASS] == ELFCLASS64)) {
        order = ident[EI_DATA] == ELFDATA2LSB ? TB_MEMORY_LITTLE : ident[EI_DATA] == ELFDATA2MSB ? TB_MEMORY_BIG : TB_MEMORY_UNKNOWN;
    }
    native->capabilities.byte_order = order;
    native->capabilities.octet_addressing = bytes == EI_NIDENT && order != TB_MEMORY_UNKNOWN;
    native->capabilities.address_bits = bytes == EI_NIDENT ? (ident[EI_CLASS] == ELFCLASS32 ? 32U : ident[EI_CLASS] == ELFCLASS64 ? 64U : 0U) : 0U;
    tb_memory_order(native->memory, order);
    TbMemoryRegion regions[TB_MEMORY_REGIONS]; size_t count = 0;
    (void)snprintf(path, sizeof path, "/proc/%ld/maps", (long)pid); FILE *maps = fopen(path, "re");
    char row[PATH_MAX + 256]; unsigned budget = 4096;
    bool discard = false;
    while (maps && budget > 0 && count < TB_MEMORY_REGIONS && fgets(row, sizeof row, maps)) {
        --budget;
        bool complete = strchr(row, '\n') != NULL;
        if (discard) { discard = !complete; continue; }
        if (!complete) { discard = true; continue; }
        TbMemoryRegion region;
        if (tb_native_memory_region(row, &region)) { regions[count++] = region; }
    }
    if (maps) { (void)fclose(maps); }
    tb_memory_regions(native->memory, regions, count);
}

void tb_session_pump(TbSession *native, int wait_ms)
{
    if (native == NULL) { return; }
    tb_openocd_pump(native->server);
    if (native->capabilities.server_owned && tb_openocd_failed(native->server) && !native->want_close) {
        native->failed = true; diagnostic(native, tb_openocd_message(native->server));
        tb_session_close(native); tb_gdb_abort(native->gdb);
    }
    tb_gdb_pump(native->gdb, wait_ms);
    tb_memory_transport(native->memory);
    if (native->master >= 0) {
        char buffer[4096]; size_t budget = TB_GDB_IO_BUDGET;
        while (budget != 0) {
            ssize_t length = read(native->master, buffer, budget < sizeof buffer ? budget : sizeof buffer);
            if (length <= 0) { break; }
            stream(native, 4, buffer, (size_t)length); budget -= (size_t)length;
        }
    }
    for (size_t i = 0; i < 5; ++i) {
        if (tb_gdb_now_ms() - native->partial_time[i] >= 100 || native->want_close) { partial_flush(native, i); }
    }
    process_reply(native);
    if (native->kind != TB_SESSION_LOCAL && native->remote_metadata_dirty && !native->waiting && !native->want_close) {
        native->remote_metadata_dirty = false; native->configured = false; native->stage = 8;
        native->capabilities.octet_addressing = false; native->capabilities.address_bits = 0;
        native->capabilities.byte_order = TB_MEMORY_UNKNOWN; native->capabilities.architecture[0] = 0;
        native->ordinary_region_count = 0;
        tb_memory_target(native->memory, false, 0); tb_memory_regions(native->memory, NULL, 0);
    }
    if (native->kind == TB_SESSION_LOCAL) {
        tb_code_native(native->code, native->target_pid, native->executable);
        memory_metadata(native);
    }
    TbGdbState gdb = tb_gdb_state(native->gdb);
    if (gdb == TB_GDB_DISCONNECTED) {
        if (native->kind != TB_SESSION_LOCAL && native->capabilities.connected) {
            native->target_connected = native->capabilities.connected = native->alive = false;
            tb_memory_target(native->memory, false, 0);
            tb_memory_regions(native->memory, NULL, 0);
        }
        native->capabilities.connected = false;
        if (native->owned && native->pidfd >= 0) {
            struct pollfd owned = {native->pidfd, POLLIN, 0};
            int ready = poll(&owned, 1, wait_ms < 0 || wait_ms > 25 ? 25 : wait_ms);
            if (ready < 0) {
                if (errno == EINTR) { return; }
                diagnostic(native, "Cannot verify owned inferior exit through pidfd.");
                native->failed = true; native->owned = false;
                state_set(native, TB_SESSION_FAILED); close_fds(native); return;
            }
            if (ready == 0) {
                if (!native->recovery_sent) {
                    diagnostic(native, "Debugger disconnected before owned inferior exit; terminating it through its stable pidfd.");
                    if (syscall(SYS_pidfd_send_signal, native->pidfd, SIGKILL, NULL, 0) != 0) {
                        diagnostic(native, "Owned inferior recovery failed; target exit is UNVERIFIED.");
                        native->failed = true; native->owned = false;
                        state_set(native, TB_SESSION_FAILED); close_fds(native); return;
                    }
                    native->recovery_sent = true;
                    native->recovery_deadline = tb_gdb_now_ms() + 1000;
                }
                if (tb_gdb_now_ms() >= native->recovery_deadline) {
                    diagnostic(native, "Owned inferior exit remains UNVERIFIED after recovery timeout.");
                    native->failed = true; native->owned = false;
                    state_set(native, TB_SESSION_FAILED); close_fds(native);
                }
                return;
            }
        }
        if (native->state != TB_SESSION_IDLE && native->state != TB_SESSION_FAILED && native->state != TB_SESSION_CLOSED) {
            if (!native->want_close) { native->failed = true; diagnostic(native, "Debugger disconnected unexpectedly."); }
            state_set(native, native->failed ? TB_SESSION_FAILED : TB_SESSION_CLOSED);
        }
        close_fds(native);
        if (native->capabilities.server_owned) {
            tb_openocd_stop(native->server);
            if (tb_openocd_active(native->server)) { state_set(native, TB_SESSION_CLOSING); return; }
            state_set(native, native->failed ? TB_SESSION_FAILED : TB_SESSION_CLOSED);
        }
        return;
    }
    if (native->want_close) {
        if (tb_gdb_now_ms() >= native->close_deadline && gdb != TB_GDB_CLOSING) {
            diagnostic(native, "Target release timed out; attached-target state is not confirmed.");
            if (native->kind == TB_SESSION_LOCAL) { tb_gdb_close(native->gdb); }
            else { native->failed = true; tb_gdb_abort(native->gdb); }
        } else { release_target(native); }
    } else if (!native->configured && !native->waiting &&
             (gdb == TB_GDB_READY || (native->kind != TB_SESSION_LOCAL && gdb != TB_GDB_STARTING && gdb != TB_GDB_CLOSING))) { setup(native); }
    else if (native->target_action && !native->waiting) {
        if (tb_gdb_now_ms() >= native->target_deadline) {
            native->target_action = 0; native->alive = false; native->target_uncertain = true;
            tb_memory_target(native->memory, false, 0); tb_memory_regions(native->memory, NULL, 0);
            state_set(native, TB_SESSION_READY);
            diagnostic(native, "OpenOCD action could not verify a fresh stop; reconnect before inspecting target values.");
        } else if (gdb == TB_GDB_STOPPED && native->target_action < 4) {
            bool flush = native->target_action == 3;
            const char *const args[] = {"console", flush ? "maintenance flush register-cache" : native->target_action == 2 ? "monitor reset halt" : "monitor halt"};
            if (!issue(native, flush ? JOB_TARGET_FLUSH : JOB_TARGET, "-interpreter-exec", args, 2)) { native->target_deadline = 0; }
        }
    }
    else if (native->configured && !native->waiting && (native->kind == TB_SESSION_LOCAL || native->alive)) {
        if (tb_memory_register_busy(native->memory)) { tb_memory_pump(native->memory); }
        else {
        tb_break_pump(native->breakpoints);
        tb_context_pump(native->context);
        const TbContextSnapshot *snapshot = tb_context_snapshot(native->context);
        if (snapshot->ready && !snapshot->stale && *snapshot->selected.architecture) {
            (void)snprintf(native->capabilities.architecture, sizeof native->capabilities.architecture, "%s", snapshot->selected.architecture);
        }
        tb_code_pump(native->code); tb_registers_pump(native->registers); tb_memory_pump(native->memory); tb_variables_pump(native->variables);
        }
    }
    if (strcmp(native->code_message, tb_code_message(native->code)) != 0) {
        (void)snprintf(native->code_message, sizeof native->code_message, "%s", tb_code_message(native->code));
        diagnostic(native, native->code_message);
    }
    if (strcmp(native->break_message, tb_break_message(native->breakpoints)) != 0) {
        (void)snprintf(native->break_message, sizeof native->break_message, "%s", tb_break_message(native->breakpoints));
        if (*native->break_message) { diagnostic(native, native->break_message); }
    }
    if (strcmp(native->memory_message, tb_memory_mmio_message(native->memory)) != 0) {
        (void)snprintf(native->memory_message, sizeof native->memory_message, "%s", tb_memory_mmio_message(native->memory));
        if (*native->memory_message) { diagnostic(native, native->memory_message); }
    }
}

size_t tb_session_pollfds(const TbSession *native, struct pollfd fds[5])
{
    (void)tb_gdb_pollfds(native->gdb, fds);
    fds[3] = (struct pollfd){native->capabilities.server_owned ? tb_openocd_fd(native->server) : native->master, POLLIN, 0};
    fds[4] = (struct pollfd){native->owned ? native->pidfd : -1, POLLIN, 0};
    return 5;
}
int tb_session_timeout(const TbSession *native, int maximum_ms) { return tb_session_active(native) ? tb_gdb_timeout(native->gdb, maximum_ms > 25 ? 25 : maximum_ms) : maximum_ms; }
TbSessionState tb_session_state(const TbSession *native) { return native->state; }
const char *tb_session_state_name(const TbSession *native)
{
    if (native->kind != TB_SESSION_LOCAL && native->state == TB_SESSION_READY) { return "Connected"; }
    static const char *const names[] = {"Idle", "Connecting", "Loaded", "Running", "Stopped", "Exited", "Closing", "Closed", "Failed"};
    return names[native->state];
}
const char *tb_session_message(const TbSession *native) { return native->message; }
uint64_t tb_session_revision(const TbSession *native) { return native->revision + tb_context_revision(native->context) + tb_code_revision(native->code) + tb_break_revision(native->breakpoints) + tb_registers_revision(native->registers) + tb_memory_revision(native->memory) + tb_variables_revision(native->variables); }
TbRegisters *tb_session_registers(TbSession *native) { return native->registers; }
TbMemory *tb_session_memory(TbSession *native) { return native->memory; }
TbVariables *tb_session_variables(TbSession *native) { return native->variables; }
size_t tb_session_log_count(const TbSession *native) { return native->count; }
const char *tb_session_log_line(const TbSession *native, size_t row) { return row < native->count ? native->logs[(native->first + row) % TB_SESSION_LOG_LINES] : ""; }
pid_t tb_session_target_pid(const TbSession *native) { return native->target_pid; }
pid_t tb_session_debugger_pid(const TbSession *native) { return tb_gdb_pid(native->gdb); }
bool tb_session_attached(const TbSession *native) { return native->attach; }
TbContext *tb_session_context(TbSession *native) { return native->context; }
TbCode *tb_session_code(TbSession *native) { return native->code; }
TbBreakpoints *tb_session_breakpoints(TbSession *native) { return native->breakpoints; }
bool tb_session_run_at(TbSession *native, TbBreakLocation location)
{
    if (setup_blocked(native, true)) { return false; }
    if (register_read_pending(native)) { return false; }
    const TbContextSnapshot *context = tb_context_snapshot(native->context);
    if (native->waiting || native->want_close || native->target_action || native->state != TB_SESSION_STOPPED || context->stale || !context->ready) {
        diagnostic(native, "Run to Selected needs a fresh stopped context."); return false;
    }
    return tb_break_run_at(native->breakpoints, location, context->selected_thread);
}
bool tb_session_run_to(TbSession *native, const char *location)
{
    return tb_session_run_at(native, (TbBreakLocation){.text = location});
}
bool tb_session_active(const TbSession *native)
{
    return tb_openocd_active(native->server) || tb_gdb_state(native->gdb) != TB_GDB_DISCONNECTED ||
           (native->state != TB_SESSION_IDLE && native->state != TB_SESSION_FAILED && native->state != TB_SESSION_CLOSED);
}

TbSessionKind tb_session_kind(const TbSession *session) { return session->kind; }
const TbSessionCapabilities *tb_session_capabilities(const TbSession *session) { return &session->capabilities; }

bool tb_session_endpoint(const char *endpoint)
{
    if (!endpoint || strlen(endpoint) >= 256) { return false; }
    const char *port = NULL;
    if (*endpoint == '[') {
        const char *close = strchr(endpoint, ']');
        if (!close || close == endpoint + 1 || close[1] != ':') { return false; }
        for (const char *p = endpoint + 1; p < close; ++p) {
            unsigned char c = (unsigned char)*p;
            if (!isxdigit(c) && c != ':' && c != '.' && c != '%') { return false; }
        }
        port = close + 2;
    } else {
        const char *colon = strchr(endpoint, ':');
        if (!colon || colon == endpoint || !isalnum((unsigned char)*endpoint)) { return false; }
        for (const char *p = endpoint; p < colon; ++p) {
            unsigned char c = (unsigned char)*p;
            if (!isalnum(c) && c != '.' && c != '-' && c != '_') { return false; }
        }
        port = colon + 1;
    }
    unsigned value = 0;
    if (!*port) { return false; }
    for (; *port; ++port) {
        if (*port < '0' || *port > '9' || value > (65535U - (unsigned)(*port - '0')) / 10U) { return false; }
        value = value * 10U + (unsigned)(*port - '0');
    }
    return value > 0;
}

static unsigned architecture_machine(const char *architecture)
{
    if (strstr(architecture, "aarch64")) { return EM_AARCH64; }
    if (strstr(architecture, "arm")) { return EM_ARM; }
    if (strstr(architecture, "xtensa")) { return EM_XTENSA; }
    if (strstr(architecture, "riscv")) { return EM_RISCV; }
    if (strstr(architecture, "x86-64")) { return EM_X86_64; }
    if (strstr(architecture, "i386")) { return EM_386; }
    return EM_NONE;
}

static bool remote_image(TbSession *session)
{
    if (!*session->executable) { return true; }
    unsigned char header[20];
    int fd = open(session->executable, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    ssize_t n = fd >= 0 ? read(fd, header, sizeof header) : -1;
    if (fd >= 0) { (void)close(fd); }
    if (n != sizeof header || memcmp(header, ELFMAG, SELFMAG) != 0) { return false; }
    TbMemoryOrder order = header[EI_DATA] == ELFDATA2LSB ? TB_MEMORY_LITTLE :
                          header[EI_DATA] == ELFDATA2MSB ? TB_MEMORY_BIG : TB_MEMORY_UNKNOWN;
    unsigned bits = header[EI_CLASS] == ELFCLASS32 ? 32 : header[EI_CLASS] == ELFCLASS64 ? 64 : 0;
    unsigned machine = order == TB_MEMORY_LITTLE ? (unsigned)header[18] + 256U * header[19] : 256U * header[18] + header[19];
    unsigned actual = architecture_machine(session->capabilities.architecture);
    if ((actual != EM_NONE && actual != machine) || !bits || order == TB_MEMORY_UNKNOWN || (session->capabilities.address_bits && bits != session->capabilities.address_bits) ||
        (session->capabilities.byte_order != TB_MEMORY_UNKNOWN && session->capabilities.byte_order != order)) { return false; }
    /* The explicit symbol file provides byte order, not MMIO authorization or
     * proof that its runtime load addresses match the remote image. */
    if (session->capabilities.byte_order == TB_MEMORY_UNKNOWN) { session->capabilities.byte_order = order; }
    return true;
}

static void remote_setup(TbSession *session)
{
    static const char *const confirm[] = {"confirm", "off"};
    static const char *const autoload[] = {"auto-load", "off"};
    static const char *const pagination[] = {"pagination", "off"};
    static const char *const args_off[] = {"print", "frame-arguments", "none"};
    static const char *const connect_timeout[] = {"tcp", "connect-timeout", "2"};
    static const char *const remote_timeout[] = {"remotetimeout", "2"};
    const char *const *settings[] = {confirm, autoload, pagination, args_off, connect_timeout, remote_timeout};
    if (session->stage < 6) {
        (void)issue(session, JOB_SETUP, "-gdb-set", settings[session->stage], session->stage == 3 || session->stage == 4 ? 3 : 2);
    } else if (session->stage == 6) {
        if (*session->executable) {
            const char *const args[] = {session->executable};
            (void)issue(session, JOB_SETUP, "-file-exec-and-symbols", args, 1);
        } else { ++session->stage; }
    } else if (session->stage == 7) {
        if (session->capabilities.server_owned && !tb_openocd_ready(session->server)) { return; }
        const char *const args[] = {session->kind == TB_SESSION_EXTENDED_REMOTE ? "extended-remote" : "remote", session->endpoint};
        (void)issue(session, JOB_REMOTE_CONNECT, "-target-select", args, 2);
    } else if (session->stage == 8 || session->stage == 9) {
        const char *const args[] = {"--language", "c", session->stage == 8 ? "(unsigned int)(unsigned char)256" : "sizeof(void*)"};
        (void)issue(session, session->stage == 8 ? JOB_REMOTE_UNITS : JOB_REMOTE_WIDTH, "-data-evaluate-expression", args, 3);
    } else if (session->stage == 10) {
        const char *const args[] = {"endian"};
        (void)issue(session, JOB_REMOTE_ENDIAN, "-gdb-show", args, 1);
    } else if (session->stage == 11) {
        (void)issue(session, JOB_REMOTE_FRAME, "-stack-info-frame", NULL, 0);
    } else {
        if (!remote_image(session)) {
            session->failed = true;
            diagnostic(session, "Symbol image and remote target metadata disagree; disconnecting without programming or reset.");
            tb_session_close(session); return;
        }
        tb_memory_target(session->memory, session->capabilities.octet_addressing, session->capabilities.address_bits);
        tb_memory_order(session->memory, session->capabilities.byte_order);
        tb_memory_regions(session->memory, session->ordinary_regions, session->ordinary_region_count);
        session->configured = true;
        TbGdbState state = tb_gdb_state(session->gdb);
        state_set(session, !session->alive ? TB_SESSION_READY : state == TB_GDB_STOPPED ? TB_SESSION_STOPPED : state == TB_GDB_RUNNING ? TB_SESSION_RUNNING : TB_SESSION_READY);
        diagnostic(session, "Connected to existing GDB server. No server ownership; unknown memory regions require explicit reads.");
    }
}
