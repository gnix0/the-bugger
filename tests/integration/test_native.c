#define _POSIX_C_SOURCE 200809L
#include "native.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void dump(TbNative *native)
{
    for (size_t i = 0; i < tb_native_log_count(native); ++i) { fprintf(stderr, "%s\n", tb_native_log_line(native, i)); }
}

static bool has(TbNative *native, const char *text)
{
    for (size_t i = 0; i < tb_native_log_count(native); ++i) {
        if (strstr(tb_native_log_line(native, i), text)) { return true; }
    }
    return false;
}

static void state(TbNative *native, TbNativeState expected)
{
    uint64_t until = tb_gdb_now_ms() + 8000;
    while (tb_native_state(native) != expected && tb_gdb_now_ms() < until) { tb_native_pump(native, 15); }
    if (tb_native_state(native) != expected) { dump(native); fprintf(stderr, "actual=%s expected=%d\n", tb_native_state_name(native), (int)expected); CHECK(false); }
}

static void output(TbNative *native, const char *text)
{
    uint64_t until = tb_gdb_now_ms() + 6000;
    while (!has(native, text) && tb_gdb_now_ms() < until) { tb_native_pump(native, 15); }
    if (!has(native, text)) { dump(native); fprintf(stderr, "missing output: %s\n", text); CHECK(false); }
}

static void close_session(TbNative *native)
{
    tb_native_close(native); state(native, TB_NATIVE_CLOSED);
    CHECK(!tb_native_active(native));
}

static void launch(const char *fixture, const char *cpp)
{
    TbNative *native = tb_native_create(); CHECK(native);
    char directory[] = "/tmp/the bugger native-XXXXXX"; CHECK(mkdtemp(directory));
    TbNativeWords arguments, environment; char error[128];
    CHECK(tb_native_words("'hello world' '' '$HOME;$(false)' 'quote\"and\\slash' --control-bytes", &arguments, error, sizeof error));
    CHECK(tb_native_words("'THE_BUGGER_TEST=env with spaces;$(false)'", &environment, error, sizeof error));
    TbNativeConfig config = {.gdb = "gdb", .executable = fixture, .directory = directory,
                             .arguments = &arguments, .environment = &environment};
    CHECK(tb_native_start(native, &config)); state(native, TB_NATIVE_READY);
    CHECK(tb_native_continue(native)); state(native, TB_NATIVE_RUNNING);
    output(native, "arg[1]=<hello world>"); output(native, "arg[2]=<>");
    output(native, "arg[3]=<$HOME;$(false)>"); output(native, "arg[4]=<quote\"and\\slash>");
    output(native, "ENV=<env with spaces;$(false)>"); output(native, directory);
    output(native, "STDERR marker"); output(native, "CONTROL=\\x1b[2J");
    CHECK(tb_native_console(native, "echo PARTIAL-NO-NEWLINE"));
    output(native, "[gdb] PARTIAL-NO-NEWLINE");
    for (size_t i = 0; i < tb_native_log_count(native); ++i) { CHECK(!strchr(tb_native_log_line(native, i), '\033')); }
    CHECK(tb_native_pause(native)); state(native, TB_NATIVE_STOPPED);
    CHECK(tb_native_console(native, "info program")); output(native, "stopped");
    /* Wait for the command result before the next explicit command. */
    for (int i = 0; i < 20; ++i) { tb_native_pump(native, 10); }
    CHECK(tb_native_console(native, "this-command-does-not-exist")); output(native, "Undefined command");
    CHECK(tb_native_state(native) == TB_NATIVE_STOPPED);
    CHECK(!tb_native_console(native, "echo one\nquit"));
    char path[512]; (void)snprintf(path, sizeof path, "%s/log.txt", directory);
    CHECK(tb_native_save_log(native, path));
    struct stat info; CHECK(stat(path, &info) == 0 && (info.st_mode & 0777) == 0600 && info.st_size > 0);
    off_t original_size = info.st_size;
    CHECK(!tb_native_save_log(native, path)); CHECK(stat(path, &info) == 0 && info.st_size == original_size);
    CHECK(!tb_native_save_log(native, "/nonexistent/the-bugger/log.txt"));
    struct rlimit original; CHECK(getrlimit(RLIMIT_FSIZE, &original) == 0);
    struct rlimit limit = original; limit.rlim_cur = 1;
    struct sigaction ignore = {.sa_handler = SIG_IGN}, saved;
    CHECK(sigemptyset(&ignore.sa_mask) == 0 && sigaction(SIGXFSZ, &ignore, &saved) == 0);
    CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    char failed[512]; (void)snprintf(failed, sizeof failed, "%s/partial.txt", directory);
    CHECK(!tb_native_save_log(native, failed));
    CHECK(setrlimit(RLIMIT_FSIZE, &original) == 0 && sigaction(SIGXFSZ, &saved, NULL) == 0);
    CHECK(has(native, "Log write failed"));
    pid_t inferior = tb_native_target_pid(native);
    close_session(native); CHECK(kill(inferior, 0) == -1 && errno == ESRCH);
    CHECK(unlink(path) == 0 && unlink(failed) == 0 && rmdir(directory) == 0);

    config.directory = "."; config.environment = NULL;
    CHECK(tb_native_words("--ignore-hup", &arguments, error, sizeof error));
    config.arguments = &arguments;
    CHECK(tb_native_start(native, &config)); state(native, TB_NATIVE_READY);
    CHECK(tb_native_console(native, "run")); state(native, TB_NATIVE_RUNNING);
    output(native, "arg[1]=<--ignore-hup>");
    inferior = tb_native_target_pid(native);
    close_session(native); CHECK(kill(inferior, 0) == -1 && errno == ESRCH);
    config.arguments = NULL; config.executable = cpp;
    CHECK(tb_native_start(native, &config)); state(native, TB_NATIVE_READY);
    CHECK(tb_native_continue(native)); state(native, TB_NATIVE_EXITED); output(native, "CPP=<C++ fixture> sum=42");
    close_session(native);
    config.gdb = "/nonexistent/gdb";
    CHECK(!tb_native_start(native, &config) && !tb_native_active(native));
    CHECK(tb_native_destroy(native));
    puts("PASS M05 actual GDB C/C++ launch, argv/cwd/env, output isolation, pause/console/log errors, exit and owned cleanup");
}

static pid_t external(const char *fixture, bool allow, int *reader)
{
    int fds[2]; CHECK(pipe(fds) == 0);
    pid_t pid = fork(); CHECK(pid >= 0);
    if (pid == 0) {
        (void)close(fds[0]); CHECK(dup2(fds[1], 1) >= 0 && dup2(fds[1], 2) >= 0); (void)close(fds[1]);
        execl(fixture, fixture, allow ? "--allow-attach" : "--deny-attach", (char *)NULL); _exit(127);
    }
    (void)close(fds[1]); *reader = fds[0];
    char initial[8192] = {0}; size_t used = 0;
    while (!strstr(initial, "tick 0\n") && used + 1 < sizeof initial) {
        struct pollfd output_fd = {*reader, POLLIN, 0};
        CHECK(poll(&output_fd, 1, 2000) > 0);
        CHECK(read(*reader, initial + used, 1) == 1);
        CHECK(initial[used] != '\033' && initial[used] != '\a');
        initial[++used] = '\0';
    }
    char expected[64]; (void)snprintf(expected, sizeof expected, "FIXTURE PID=%ld\n", (long)pid);
    CHECK(strncmp(initial, expected, strlen(expected)) == 0 && strstr(initial, "tick 0\n"));
    return pid;
}

static void attach(const char *fixture)
{
    for (int allowed = 1; allowed >= 0; --allowed) {
        int reader; pid_t pid = external(fixture, allowed != 0, &reader);
        TbNative *native = tb_native_create(); CHECK(native);
        TbNativeConfig config = {.attach = true, .pid = pid, .gdb = "gdb", .executable = ""};
        CHECK(tb_native_start(native, &config));
        if (allowed) {
            state(native, TB_NATIVE_STOPPED);
            close_session(native);
            CHECK(kill(pid, 0) == 0);
            int flags = fcntl(reader, F_GETFL); CHECK(flags >= 0 && fcntl(reader, F_SETFL, flags | O_NONBLOCK) == 0);
            char bytes[4096]; while (read(reader, bytes, sizeof bytes) > 0) { }
            struct pollfd output_fd = {reader, POLLIN, 0};
            CHECK(poll(&output_fd, 1, 2000) > 0 && read(reader, bytes, sizeof bytes) > 0); /* Resumed heartbeat. */
        } else {
            state(native, TB_NATIVE_FAILED);
            CHECK(has(native, "ptrace") || has(native, "Operation not permitted"));
            CHECK(kill(pid, 0) == 0);
        }
        CHECK(tb_native_destroy(native));
        CHECK(kill(pid, SIGTERM) == 0 && waitpid(pid, NULL, 0) == pid); (void)close(reader);
    }
    puts("PASS M05 permitted attach, detached process resumes/survives, and denied ptrace diagnostics");
}

static void flood(const char *fixture)
{
    TbNative *native = tb_native_create(); CHECK(native);
    TbNativeWords arguments; char error[64]; CHECK(tb_native_words("--flood", &arguments, error, sizeof error));
    TbNativeConfig config = {.gdb = "gdb", .executable = fixture, .arguments = &arguments};
    CHECK(tb_native_start(native, &config)); state(native, TB_NATIVE_READY); CHECK(tb_native_continue(native));
    output(native, "FLOOD END"); CHECK(tb_native_log_count(native) == TB_NATIVE_LOG_LINES);
    CHECK(tb_native_pause(native)); state(native, TB_NATIVE_STOPPED); close_session(native);
    CHECK(tb_native_destroy(native));
    puts("PASS M05 program output flood with bounded retained history and responsive pause/cleanup");
}

static void engine_failure(const char *fixture)
{
    TbNative *native = tb_native_create(); CHECK(native);
    TbNativeConfig config = {.gdb = "gdb", .executable = fixture};
    CHECK(tb_native_start(native, &config)); state(native, TB_NATIVE_READY);
    CHECK(tb_native_continue(native)); state(native, TB_NATIVE_RUNNING);
    pid_t owned = tb_native_target_pid(native), debugger = tb_native_debugger_pid(native);
    CHECK(owned > 0 && debugger > 0 && kill(debugger, SIGKILL) == 0);
    state(native, TB_NATIVE_FAILED);
    uint64_t reaped = tb_gdb_now_ms() + 1000;
    while (kill(owned, 0) == 0 && tb_gdb_now_ms() < reaped) { (void)poll(NULL, 0, 10); }
    CHECK(kill(owned, 0) == -1 && errno == ESRCH);
    CHECK(!has(native, "UNVERIFIED") && tb_native_destroy(native));
    int reader; pid_t external_pid = external(fixture, true, &reader);
    native = tb_native_create(); CHECK(native);
    config = (TbNativeConfig){.gdb = "gdb", .attach = true, .pid = external_pid, .executable = ""};
    CHECK(tb_native_start(native, &config)); state(native, TB_NATIVE_STOPPED);
    CHECK(kill(tb_native_debugger_pid(native), SIGKILL) == 0);
    state(native, TB_NATIVE_FAILED); CHECK(kill(external_pid, 0) == 0);
    CHECK(tb_native_destroy(native));
    CHECK(kill(external_pid, SIGTERM) == 0 && waitpid(external_pid, NULL, 0) == external_pid);
    (void)close(reader);
    puts("PASS M05 GDB crash: owned-inferior exit verification and attached-process survival");
}

int main(int argc, char *argv[])
{
    CHECK(argc == 3);
    launch(argv[1], argv[2]); attach(argv[1]); flood(argv[1]); engine_failure(argv[1]);
    return 0;
}
