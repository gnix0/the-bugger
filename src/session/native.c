#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Linux pidfds/PTY. */
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
               JOB_INTERRUPT_CLOSE, JOB_RELEASE } Job;
struct TbNative {
    TbGdb *gdb;
    TbContext *context;
    TbCode *code;
    TbBreakpoints *breakpoints;
    TbRegisters *registers;
    TbMemory *memory;
    TbVariables *variables;
    TbNativeState state;
    bool attach, configured, waiting, completed, success, want_close, failed;
    bool alive, launch_requested, owned, recovery_sent;
    Job job;
    int stage, master, slave, pidfd;
    pid_t target_pid, owned_pid;
    uint64_t revision, close_deadline, partial_time[5], recovery_deadline;
    pid_t memory_pid;
    uint64_t memory_stop;
    char executable[PATH_MAX], directory[PATH_MAX], debugger[PATH_MAX], tty[PATH_MAX];
    TbNativeWords arguments, environment;
    char message[TB_NATIVE_LOG_WIDTH], reply_error[TB_NATIVE_LOG_WIDTH];
    char code_message[192];
    char break_message[192];
    char logs[TB_NATIVE_LOG_LINES][TB_NATIVE_LOG_WIDTH];
    size_t first, count;
    char partial[5][TB_NATIVE_LOG_WIDTH - 24];
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

static void message(TbNative *native, const char *text)
{
    safe_text(native->message, sizeof native->message, text, strlen(text));
    ++native->revision;
}

static void line(TbNative *native, const char *prefix, const char *text)
{
    size_t slot = (native->first + native->count) % TB_NATIVE_LOG_LINES;
    if (native->count == TB_NATIVE_LOG_LINES) { native->first = (native->first + 1) % TB_NATIVE_LOG_LINES; }
    else { ++native->count; }
    char row[TB_NATIVE_LOG_WIDTH];
    (void)snprintf(row, sizeof row, "%.20s%.234s", prefix, text);
    memcpy(native->logs[slot], row, strlen(row) + 1);
    ++native->revision;
}

static void diagnostic(TbNative *native, const char *text)
{
    message(native, text);
    line(native, "[session] ", native->message);
}

static void partial_flush(TbNative *native, size_t channel)
{
    static const char *const prefixes[] = {"[gdb] ", "[target] ", "[gdb-log] ", "[gdb-stderr] ", "[program] "};
    if (native->partial_length[channel] != 0) {
        native->partial[channel][native->partial_length[channel]] = '\0';
        line(native, prefixes[channel], native->partial[channel]);
        native->partial_length[channel] = 0;
    }
}

static void stream(TbNative *native, size_t channel, const char *bytes, size_t length)
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

static void state_set(TbNative *native, TbNativeState state)
{
    if (native->state != state) { native->state = state; ++native->revision; }
}

bool tb_native_words(const char *input, TbNativeWords *words, char *error, size_t capacity)
{
    if (input == NULL || words == NULL || error == NULL || capacity == 0) { return false; }
    memset(words, 0, sizeof *words);
    size_t at = 0;
    while (input[at] != '\0') {
        while (isspace((unsigned char)input[at])) { ++at; }
        if (input[at] == '\0') { break; }
        if (words->count == TB_NATIVE_WORDS) { (void)snprintf(error, capacity, "At most %d arguments/environment entries.", TB_NATIVE_WORDS); return false; }
        char quote = 0; size_t used = 0;
        while (input[at] != '\0' && (quote != 0 || !isspace((unsigned char)input[at]))) {
            char c = input[at++];
            if ((c == '\'' || c == '"') && (quote == 0 || quote == c)) { quote = quote == 0 ? c : 0; continue; }
            if (c == '\\' && quote != '\'') {
                if (input[at] == '\0') { (void)snprintf(error, capacity, "Trailing backslash in arguments."); return false; }
                c = input[at++];
            }
            if (used + 1 == TB_NATIVE_WORD_BYTES) { (void)snprintf(error, capacity, "Argument/environment entry too long."); return false; }
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
    TbNative *native = context;
    ++native->revision;
    if (tb_break_event(native->breakpoints, event)) { return; }
    if (tb_registers_event(native->registers, event)) { return; }
    if (tb_memory_event(native->memory, event)) { return; }
    if (tb_variables_event(native->variables, event)) { return; }
    if (tb_code_event(native->code, event)) { return; }
    if (tb_context_event(native->context, event)) { return; }
    if (event->kind == TB_GDB_DIAGNOSTIC) {
        char text[TB_NATIVE_LOG_WIDTH], safe[160];
        safe_text(safe, sizeof safe, event->text, event->length);
        (void)snprintf(text, sizeof text, "%s%s%s", safe, event->status ? ": " : "", event->status ? strerror(event->status) : "");
        diagnostic(native, text);
    } else if (event->kind == TB_GDB_STDERR) { stream(native, 3, event->text, event->length); }
    else if (event->kind == TB_GDB_REPLY || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED) {
        native->waiting = false; native->completed = true;
        native->success = event->kind == TB_GDB_REPLY &&
                          (strcmp(event->record->klass, "done") == 0 || strcmp(event->record->klass, "running") == 0);
        native->reply_error[0] = '\0';
        if (!native->success) {
            const TbMiValue *value = event->record ? tb_mi_find(event->record->results, "msg") : NULL;
            if (value != NULL && value->type == TB_MI_STRING) {
                safe_text(native->reply_error, sizeof native->reply_error, value->string.data, value->string.length);
            } else { (void)snprintf(native->reply_error, sizeof native->reply_error, "%s", event->kind == TB_GDB_TIMEOUT ? "GDB operation timed out; it was not retried." : "GDB operation was cancelled."); }
        }
    } else if (event->kind == TB_GDB_STATE && native->configured && !native->want_close) {
        TbGdbState state = tb_gdb_state(native->gdb);
        if (state == TB_GDB_RUNNING) { state_set(native, TB_NATIVE_RUNNING); }
        else if (state == TB_GDB_STOPPED) { state_set(native, TB_NATIVE_STOPPED); }
        else if (state == TB_GDB_EXITED) { state_set(native, TB_NATIVE_EXITED); }
    }
    if (event->record == NULL) { return; }
    const TbMiRecord *record = event->record;
    if (record->kind == TB_MI_CONSOLE || record->kind == TB_MI_TARGET || record->kind == TB_MI_LOG) {
        stream(native, record->kind == TB_MI_CONSOLE ? 0 : record->kind == TB_MI_TARGET ? 1 : 2,
               record->stream.data, record->stream.length);
    } else if (record->kind == TB_MI_NOTIFY && strcmp(record->klass, "thread-group-started") == 0) {
        native->target_pid = record_pid(tb_mi_find(record->results, "pid"));
        native->alive = native->target_pid > 0;
        if (native->launch_requested && !native->attach && native->target_pid > 0) {
            native->owned = true; native->owned_pid = native->target_pid;
            if (native->pidfd >= 0) { (void)close(native->pidfd); }
            native->pidfd = (int)syscall(SYS_pidfd_open, native->target_pid, 0);
            if (native->pidfd < 0) { diagnostic(native, "Cannot open inferior pidfd; crash cleanup cannot be guaranteed."); }
        }
        native->launch_requested = false;
    } else if (record->kind == TB_MI_NOTIFY && strcmp(record->klass, "thread-group-exited") == 0) {
        native->alive = false;
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
            if (!native->want_close) { state_set(native, TB_NATIVE_EXITED); }
        } else if (!native->want_close) { state_set(native, TB_NATIVE_STOPPED); }
    }
}

TbNative *tb_native_create(void)
{
    TbNative *native = calloc(1, sizeof *native);
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
    if (!native->variables) {
        tb_memory_destroy(native->memory); tb_registers_destroy(native->registers);
        tb_break_destroy(native->breakpoints); tb_code_destroy(native->code);
        tb_context_destroy(native->context); (void)tb_gdb_destroy(native->gdb); free(native); return NULL;
    }
    return native;
}

static void close_fds(TbNative *native)
{
    if (native->master >= 0) { (void)close(native->master); native->master = -1; }
    if (native->slave >= 0) { (void)close(native->slave); native->slave = -1; }
    if (native->pidfd >= 0) { (void)close(native->pidfd); native->pidfd = -1; }
}

bool tb_native_destroy(TbNative *native)
{
    if (native == NULL) { return true; }
    if (tb_native_active(native)) { return false; }
    if (!tb_gdb_destroy(native->gdb)) { return false; }
    tb_variables_destroy(native->variables); tb_memory_destroy(native->memory); tb_registers_destroy(native->registers); tb_break_destroy(native->breakpoints); tb_code_destroy(native->code); tb_context_destroy(native->context); close_fds(native); free(native); return true;
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

bool tb_native_start(TbNative *native, const TbNativeConfig *config)
{
    if (native == NULL || config == NULL || tb_native_active(native)) { return false; }
    native->failed = false;
    if (!copy_path(native->executable, sizeof native->executable, config->executable) ||
        !copy_path(native->directory, sizeof native->directory, config->directory) ||
        !copy_path(native->debugger, sizeof native->debugger, config->gdb && *config->gdb ? config->gdb : "gdb")) {
        diagnostic(native, "Path is too long."); return false;
    }
    native->attach = config->attach;
    native->target_pid = config->pid;
    if ((config->attach && (config->pid <= 1 || config->pid == getpid() ||
                           (kill(config->pid, 0) != 0 && errno == ESRCH))) ||
        (!config->attach && !elf(native->executable, true)) ||
        (config->attach && native->executable[0] && !elf(native->executable, false))) {
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
    if (!config->attach && ((config->arguments && config->arguments->count > TB_NATIVE_WORDS) ||
        (config->environment && config->environment->count > TB_NATIVE_WORDS))) { diagnostic(native, "Too many arguments/environment entries."); return false; }
    native->arguments = !config->attach && config->arguments ? *config->arguments : (TbNativeWords){0};
    native->environment = !config->attach && config->environment ? *config->environment : (TbNativeWords){0};
    for (size_t i = 0; i < native->arguments.count; ++i) {
        if (memchr(native->arguments.values[i], '\0', TB_NATIVE_WORD_BYTES) == NULL) { diagnostic(native, "Argument is not terminated."); return false; }
    }
    for (size_t i = 0; i < native->environment.count; ++i) {
        const char *value = native->environment.values[i];
        if (memchr(value, '\0', TB_NATIVE_WORD_BYTES) == NULL) { diagnostic(native, "Environment entry is not terminated."); return false; }
        if (strchr(value, '\n') || strchr(value, '\r')) { diagnostic(native, "Environment entries cannot contain record delimiters."); return false; }
        size_t at = 0;
        while ((value[at] >= 'A' && value[at] <= 'Z') || (value[at] >= 'a' && value[at] <= 'z') ||
               value[at] == '_' || (at != 0 && value[at] >= '0' && value[at] <= '9')) { ++at; }
        if (at == 0 || value[at] != '=') { diagnostic(native, "Environment entries must be NAME=value; no shell syntax."); return false; }
    }
    close_fds(native);
    native->first = native->count = 0;
    memset(native->partial_length, 0, sizeof native->partial_length);
    native->want_close = native->configured = native->waiting = native->completed = false;
    native->alive = native->owned = native->launch_requested = native->recovery_sent = false;
    native->stage = 0; native->job = JOB_NONE;
    native->code_message[0] = '\0';
    native->break_message[0] = '\0';
    tb_context_reset(native->context);
    tb_code_reset(native->code);
    tb_break_reset(native->breakpoints);
    tb_registers_reset(native->registers);
    tb_memory_reset(native->memory);
    tb_variables_reset(native->variables);
    native->memory_pid = 0; native->memory_stop = 0;
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
    state_set(native, TB_NATIVE_STARTING);
    diagnostic(native, native->attach ? "Starting GDB to attach; ptrace permissions will be checked by GDB." : "Starting GDB; Continue will run the loaded executable.");
    if (!tb_gdb_start(native->gdb, native->debugger, 3000)) {
        state_set(native, TB_NATIVE_FAILED); close_fds(native); return false;
    }
    return true;
}

static bool issue(TbNative *native, Job job, const char *operation, const char *const args[], size_t count)
{
    native->job = job; native->completed = false;
    native->waiting = tb_gdb_request(native->gdb, operation, args, count, TB_GDB_SESSION, 1, 3000, NULL);
    if (!native->waiting) { diagnostic(native, "GDB request rejected; session may be closing."); }
    if (!native->waiting && job == JOB_SETUP) { native->failed = true; tb_native_close(native); }
    return native->waiting;
}

bool tb_native_continue(TbNative *native)
{
    if (native->waiting || native->want_close || !native->configured || tb_break_busy(native->breakpoints)) { return false; }
    tb_context_refresh(native->context);
    if (native->state == TB_NATIVE_READY) {
        native->launch_requested = !native->attach;
        return issue(native, JOB_RUN, "-exec-run", NULL, 0);
    }
    return native->state == TB_NATIVE_STOPPED && issue(native, JOB_RUN, "-exec-continue", NULL, 0);
}

bool tb_native_pause(TbNative *native)
{
    return !native->waiting && !native->want_close && native->state == TB_NATIVE_RUNNING &&
           issue(native, JOB_PAUSE, "-exec-interrupt", NULL, 0);
}

bool tb_native_step(TbNative *native, TbStep step, bool instruction)
{
    const TbContextSnapshot *context = tb_context_snapshot(native->context);
    if (native->waiting || native->want_close || !native->configured || tb_break_busy(native->breakpoints) ||
        native->state != TB_NATIVE_STOPPED || context->stale || !context->ready ||
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

bool tb_native_console(TbNative *native, const char *command)
{
    if (tb_break_busy(native->breakpoints)) { diagnostic(native, "Wait for the pending breakpoint operation."); return false; }
    if (native->waiting || native->want_close || !native->configured) {
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
    tb_context_refresh(native->context);
    bool accepted = issue(native, JOB_CONSOLE, "-interpreter-exec", args, 2);
    const char *verb = command;
    while (*verb == ' ' || *verb == '\t') { ++verb; }
    size_t length = strcspn(verb, " \t");
    bool launch = (length > 0 && length <= 3 && strncmp(verb, "run", length) == 0) ||
                  (length == 5 && strncmp(verb, "start", length) == 0) ||
                  (length == 6 && strncmp(verb, "starti", length) == 0);
    if (accepted && launch && !native->attach) { native->launch_requested = true; }
    return accepted;
}

bool tb_native_save_log(TbNative *native, const char *path)
{
    if (path == NULL || !*path) { diagnostic(native, "Choose a log destination path."); return false; }
    for (size_t i = 0; i < 5; ++i) { partial_flush(native, i); }
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) {
        char text[256]; (void)snprintf(text, sizeof text, "Cannot create log (existing files are preserved): %s", strerror(errno)); diagnostic(native, text); return false;
    }
    bool okay = true;
    for (size_t i = 0; i < native->count && okay; ++i) {
        const char *text = tb_native_log_line(native, i); size_t at = 0, length = strlen(text);
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

void tb_native_close(TbNative *native)
{
    if (!tb_native_active(native)) { return; }
    if (!native->want_close) { native->close_deadline = tb_gdb_now_ms() + 4000; }
    native->want_close = true;
    state_set(native, TB_NATIVE_CLOSING);
}

static void setup(TbNative *native)
{
    const char *const confirm[] = {"confirm", "off"};
    const char *const shell[] = {"startup-with-shell", "off"};
    const char *const autoload[] = {"auto-load", "off"};
    const char *const page[] = {"pagination", "off"};
    const char *const no_random[] = {"disable-randomization", "off"};
    const char *const path[] = {native->executable};
    const char *const cwd[] = {native->directory};
    const char *const tty[] = {native->tty};
    if (native->stage < 5) {
        const char *const *settings[] = {confirm, shell, autoload, page, no_random};
        (void)issue(native, JOB_SETUP, "-gdb-set", settings[native->stage], 2);
    } else if (native->stage == 5) {
        if (*native->directory) { (void)issue(native, JOB_SETUP, "-environment-cd", cwd, 1); }
        else { ++native->stage; }
    } else if (native->stage == 6) {
        if (*native->executable) { (void)issue(native, JOB_SETUP, "-file-exec-and-symbols", path, 1); }
        else { ++native->stage; }
    } else if (native->stage == 7 && !native->attach) { (void)issue(native, JOB_SETUP, "-inferior-tty-set", tty, 1); }
    else if (native->stage == 8 && !native->attach) {
        const char *args[TB_NATIVE_WORDS];
        for (size_t i = 0; i < native->arguments.count; ++i) { args[i] = native->arguments.values[i]; }
        (void)issue(native, JOB_SETUP, "-exec-arguments", args, native->arguments.count);
    } else if (native->stage >= 9 && (size_t)(native->stage - 9) < native->environment.count && !native->attach) {
        const char *const args[] = {"environment", native->environment.values[native->stage - 9]};
        (void)issue(native, JOB_SETUP, "-gdb-set", args, 2);
    } else if (native->stage < 9 + (int)native->environment.count) { ++native->stage; }
    else if (native->attach && native->stage == 9 + (int)native->environment.count) {
        char pid[32]; (void)snprintf(pid, sizeof pid, "%ld", (long)native->target_pid);
        const char *const args[] = {pid};
        (void)issue(native, JOB_SETUP, "-target-attach", args, 1);
    } else {
        native->configured = true;
        state_set(native, native->attach ? TB_NATIVE_STOPPED : TB_NATIVE_READY);
        diagnostic(native, native->attach ? "Attached and stopped; exit will detach/resume this process." : "ELF loaded. Continue starts the program.");
    }
}

static void process_reply(TbNative *native)
{
    if (!native->completed) { return; }
    native->completed = false;
    if (!native->success) {
        diagnostic(native, native->reply_error);
        if (native->job == JOB_SETUP) {
            if (native->attach) { diagnostic(native, "Attach/setup failed: check PID ownership, Yama ptrace_scope, container policy and capabilities; no permission settings were changed."); }
            if (!native->want_close) { native->failed = true; tb_native_close(native); }
        } else if (native->job == JOB_RELEASE || native->job == JOB_INTERRUPT_CLOSE) {
            diagnostic(native, "Target release was not confirmed; closing debugger with this limitation.");
            tb_gdb_close(native->gdb);
        }
        if (native->job == JOB_RUN || native->job == JOB_CONSOLE) { native->launch_requested = false; }
    } else if (native->job == JOB_SETUP) {
        if (native->attach && native->stage == 9 + (int)native->environment.count) { native->alive = true; }
        ++native->stage;
    }
    else if (native->job == JOB_RELEASE) { native->alive = false; tb_gdb_close(native->gdb); }
    else if (native->job == JOB_CONSOLE) {
        if (native->success) { message(native, "GDB command completed."); }
        tb_context_refresh(native->context);
    }
    native->job = JOB_NONE;
}

static void release_target(TbNative *native)
{
    if (native->waiting) { return; }
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

/* Linux metadata belongs to this adapter. No target bytes are probed here. */
static void memory_metadata(TbNative *native)
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
    tb_memory_order(native->memory, order);
    TbMemoryRegion regions[TB_MEMORY_REGIONS]; size_t count = 0;
    (void)snprintf(path, sizeof path, "/proc/%ld/maps", (long)pid); FILE *maps = fopen(path, "re");
    char row[PATH_MAX + 256]; unsigned budget = 4096;
    while (maps && budget-- && count < TB_MEMORY_REGIONS && fgets(row, sizeof row, maps)) {
        unsigned long long begin, end, inode; char permissions[5]; int used = 0;
        if (sscanf(row, "%llx-%llx %4s %*s %*s %llu %n", &begin, &end, permissions, &inode, &used) != 4 ||
            !used || permissions[0] != 'r' || end <= begin) { continue; }
        char *name = row + used; name[strcspn(name, "\n")] = 0;
        bool ordinary = inode == 0 && (!*name || *name == '[');
        struct stat info;
        if (*name == '/' && strncmp(name, "/dev/", 5) && strncmp(name, "/sys/", 5) && strncmp(name, "/proc/", 6) &&
            stat(name, &info) == 0 && S_ISREG(info.st_mode)) { ordinary = true; }
        if (ordinary) { regions[count++] = (TbMemoryRegion){begin, end}; }
    }
    if (maps) { (void)fclose(maps); }
    tb_memory_regions(native->memory, regions, count);
}

void tb_native_pump(TbNative *native, int wait_ms)
{
    if (native == NULL) { return; }
    tb_gdb_pump(native->gdb, wait_ms);
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
    tb_code_native(native->code, native->target_pid, native->executable);
    memory_metadata(native);
    TbGdbState gdb = tb_gdb_state(native->gdb);
    if (gdb == TB_GDB_DISCONNECTED) {
        if (native->owned && native->pidfd >= 0) {
            struct pollfd owned = {native->pidfd, POLLIN, 0};
            int ready = poll(&owned, 1, wait_ms < 0 || wait_ms > 25 ? 25 : wait_ms);
            if (ready < 0) {
                if (errno == EINTR) { return; }
                diagnostic(native, "Cannot verify owned inferior exit through pidfd.");
                native->failed = true; native->owned = false;
                state_set(native, TB_NATIVE_FAILED); close_fds(native); return;
            }
            if (ready == 0) {
                if (!native->recovery_sent) {
                    diagnostic(native, "Debugger disconnected before owned inferior exit; terminating it through its stable pidfd.");
                    if (syscall(SYS_pidfd_send_signal, native->pidfd, SIGKILL, NULL, 0) != 0) {
                        diagnostic(native, "Owned inferior recovery failed; target exit is UNVERIFIED.");
                        native->failed = true; native->owned = false;
                        state_set(native, TB_NATIVE_FAILED); close_fds(native); return;
                    }
                    native->recovery_sent = true;
                    native->recovery_deadline = tb_gdb_now_ms() + 1000;
                }
                if (tb_gdb_now_ms() >= native->recovery_deadline) {
                    diagnostic(native, "Owned inferior exit remains UNVERIFIED after recovery timeout.");
                    native->failed = true; native->owned = false;
                    state_set(native, TB_NATIVE_FAILED); close_fds(native);
                }
                return;
            }
        }
        if (native->state != TB_NATIVE_IDLE && native->state != TB_NATIVE_FAILED && native->state != TB_NATIVE_CLOSED) {
            if (!native->want_close) { native->failed = true; diagnostic(native, "Debugger disconnected unexpectedly."); }
            state_set(native, native->failed ? TB_NATIVE_FAILED : TB_NATIVE_CLOSED);
        }
        close_fds(native); return;
    }
    if (native->want_close) {
        if (tb_gdb_now_ms() >= native->close_deadline && gdb != TB_GDB_CLOSING) {
            diagnostic(native, "Target release timed out; attached-target state is not confirmed.");
            tb_gdb_close(native->gdb);
        } else { release_target(native); }
    } else if (!native->configured && !native->waiting && gdb == TB_GDB_READY) { setup(native); }
    else if (native->configured && !native->waiting) {
        tb_break_pump(native->breakpoints);
        tb_context_pump(native->context); tb_code_pump(native->code); tb_registers_pump(native->registers); tb_memory_pump(native->memory); tb_variables_pump(native->variables);
    }
    if (strcmp(native->code_message, tb_code_message(native->code)) != 0) {
        (void)snprintf(native->code_message, sizeof native->code_message, "%s", tb_code_message(native->code));
        diagnostic(native, native->code_message);
    }
    if (strcmp(native->break_message, tb_break_message(native->breakpoints)) != 0) {
        (void)snprintf(native->break_message, sizeof native->break_message, "%s", tb_break_message(native->breakpoints));
        if (*native->break_message) { diagnostic(native, native->break_message); }
    }
}

size_t tb_native_pollfds(const TbNative *native, struct pollfd fds[5])
{
    (void)tb_gdb_pollfds(native->gdb, fds);
    fds[3] = (struct pollfd){native->master, POLLIN, 0};
    fds[4] = (struct pollfd){native->owned ? native->pidfd : -1, POLLIN, 0};
    return 5;
}
int tb_native_timeout(const TbNative *native, int maximum_ms) { return tb_native_active(native) ? tb_gdb_timeout(native->gdb, maximum_ms > 25 ? 25 : maximum_ms) : maximum_ms; }
TbNativeState tb_native_state(const TbNative *native) { return native->state; }
const char *tb_native_state_name(const TbNative *native)
{
    static const char *const names[] = {"Idle", "Connecting", "Loaded", "Running", "Stopped", "Exited", "Closing", "Closed", "Failed"};
    return names[native->state];
}
const char *tb_native_message(const TbNative *native) { return native->message; }
uint64_t tb_native_revision(const TbNative *native) { return native->revision + tb_context_revision(native->context) + tb_code_revision(native->code) + tb_break_revision(native->breakpoints) + tb_registers_revision(native->registers) + tb_memory_revision(native->memory) + tb_variables_revision(native->variables); }
TbRegisters *tb_native_registers(TbNative *native) { return native->registers; }
TbMemory *tb_native_memory(TbNative *native) { return native->memory; }
TbVariables *tb_native_variables(TbNative *native) { return native->variables; }
size_t tb_native_log_count(const TbNative *native) { return native->count; }
const char *tb_native_log_line(const TbNative *native, size_t row) { return row < native->count ? native->logs[(native->first + row) % TB_NATIVE_LOG_LINES] : ""; }
pid_t tb_native_target_pid(const TbNative *native) { return native->target_pid; }
pid_t tb_native_debugger_pid(const TbNative *native) { return tb_gdb_pid(native->gdb); }
bool tb_native_attached(const TbNative *native) { return native->attach; }
TbContext *tb_native_context(TbNative *native) { return native->context; }
TbCode *tb_native_code(TbNative *native) { return native->code; }
TbBreakpoints *tb_native_breakpoints(TbNative *native) { return native->breakpoints; }
bool tb_native_run_at(TbNative *native, TbBreakLocation location)
{
    const TbContextSnapshot *context = tb_context_snapshot(native->context);
    if (native->waiting || native->want_close || native->state != TB_NATIVE_STOPPED || context->stale || !context->ready) {
        diagnostic(native, "Run to Selected needs a fresh stopped context."); return false;
    }
    return tb_break_run_at(native->breakpoints, location, context->selected_thread);
}
bool tb_native_run_to(TbNative *native, const char *location)
{
    return tb_native_run_at(native, (TbBreakLocation){.text = location});
}
bool tb_native_active(const TbNative *native)
{
    return tb_gdb_state(native->gdb) != TB_GDB_DISCONNECTED ||
           (native->state != TB_NATIVE_IDLE && native->state != TB_NATIVE_FAILED && native->state != TB_NATIVE_CLOSED);
}
