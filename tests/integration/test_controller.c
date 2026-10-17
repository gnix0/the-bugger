#define _POSIX_C_SOURCE 200809L
#include "controller.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    TbGdb *gdb;
    size_t replies, stale, timeouts, cancelled, diagnostics, exits, records, stderr_bytes;
    uint64_t cookies[256];
    size_t cookies_count;
    bool command_error, killed, term, unexpected_exit, oversized, fallback;
    size_t echo_bytes;
} Observer;

static void emit(void *context, const TbGdbEvent *event)
{
    Observer *observer = context;
    switch (event->kind) {
    case TB_GDB_REPLY:
        CHECK(event->record != NULL && event->token != 0);
        ++observer->replies;
        if (observer->cookies_count < 256) { observer->cookies[observer->cookies_count++] = event->cookie; }
        observer->command_error |= strcmp(event->record->klass, "error") == 0;
        const TbMiValue *bytes = tb_mi_find(event->record->results, "bytes");
        if (bytes != NULL) { observer->echo_bytes = (size_t)strtoul(bytes->string.data, NULL, 10); }
        break;
    case TB_GDB_STALE: ++observer->stale; break;
    case TB_GDB_TIMEOUT: ++observer->timeouts; break;
    case TB_GDB_CANCELLED: ++observer->cancelled; break;
    case TB_GDB_DIAGNOSTIC:
        ++observer->diagnostics;
        CHECK(event->text != NULL && event->length != 0);
        observer->oversized |= event->status == EINVAL;
        observer->fallback |= strstr(event->text, "retrying MI2") != NULL;
        break;
    case TB_GDB_STDERR: observer->stderr_bytes += event->length; break;
    case TB_GDB_CHILD_EXIT:
        ++observer->exits;
        observer->killed |= WIFSIGNALED(event->status) && WTERMSIG(event->status) == SIGKILL;
        observer->term |= WIFSIGNALED(event->status) && WTERMSIG(event->status) == SIGTERM;
        observer->unexpected_exit |= WIFEXITED(event->status) && WEXITSTATUS(event->status) == 13;
        break;
    case TB_GDB_RECORD: ++observer->records; break;
    case TB_GDB_STATE: case TB_GDB_RECONCILED: break;
    }
}

static void pump_until(Observer *observer, TbGdbState state, unsigned maximum_ms)
{
    uint64_t deadline = tb_gdb_now_ms() + maximum_ms;
    while (tb_gdb_state(observer->gdb) != state && tb_gdb_now_ms() < deadline) {
        tb_gdb_pump(observer->gdb, 20);
    }
    if (tb_gdb_state(observer->gdb) != state) {
        fprintf(stderr, "state %d expected %d, diagnostics %zu\n", (int)tb_gdb_state(observer->gdb),
                (int)state, observer->diagnostics);
        CHECK(false);
    }
}

static void pending_done(Observer *observer)
{
    uint64_t deadline = tb_gdb_now_ms() + 3000;
    while (tb_gdb_pending(observer->gdb) != 0 && tb_gdb_now_ms() < deadline) {
        tb_gdb_pump(observer->gdb, 20);
    }
    CHECK(tb_gdb_pending(observer->gdb) == 0);
}

static Observer create(void)
{
    Observer observer = {0};
    return observer;
}

static void start(Observer *observer, const char *peer, const char *mode)
{
    CHECK(setenv("TB_FAKE_MODE", mode, 1) == 0);
    if (observer->gdb == NULL) { observer->gdb = tb_gdb_create(emit, observer); }
    CHECK(observer->gdb != NULL);
    CHECK(tb_gdb_start(observer->gdb, peer, 1000));
    pump_until(observer, TB_GDB_READY, 3000);
    CHECK(tb_gdb_pending(observer->gdb) == 0);
    CHECK(!tb_gdb_destroy(observer->gdb));
}

static void close_peer(Observer *observer)
{
    pid_t pid = tb_gdb_pid(observer->gdb);
    tb_gdb_close(observer->gdb);
    pump_until(observer, TB_GDB_DISCONNECTED, 3000);
    CHECK(tb_gdb_pid(observer->gdb) == 0 && tb_gdb_pending(observer->gdb) == 0);
    if (pid > 0) { CHECK(waitpid(pid, NULL, WNOHANG) == -1 && errno == ECHILD); }
}

static void destroy(Observer *observer)
{
    close_peer(observer);
    CHECK(tb_gdb_destroy(observer->gdb));
    observer->gdb = NULL;
}

static void request(Observer *observer, const char *operation, TbGdbScope scope, uint64_t cookie)
{
    CHECK(tb_gdb_request(observer->gdb, operation, NULL, 0, scope, cookie, 1500, NULL));
}

static void normal(const char *peer)
{
    Observer observer = create();
    start(&observer, peer, "fragment");
    CHECK(tb_gdb_mi_version(observer.gdb) == 3);
    CHECK(!tb_gdb_request(observer.gdb, "-hold", NULL, 0, TB_GDB_STOP, 0, 50, NULL));
    request(&observer, "-hold", TB_GDB_SESSION, 10);
    request(&observer, "-release", TB_GDB_SESSION, 20);
    pending_done(&observer);
    CHECK(observer.replies == 2 && observer.cookies[0] == 20 && observer.cookies[1] == 10);
    request(&observer, "-error", TB_GDB_SESSION, 30);
    request(&observer, "-unknown", TB_GDB_SESSION, 40);
    pending_done(&observer);
    CHECK(observer.command_error && observer.stale == 1 && observer.replies == 4);
    CHECK(tb_gdb_state(observer.gdb) == TB_GDB_READY);

    const char argument[] = "space \"quote\" \\ slash\n$(false); | && \033\377";
    const char *const args[] = {argument};
    CHECK(tb_gdb_request(observer.gdb, "-echo", args, 1, TB_GDB_SESSION, 50, 1500, NULL));
    pending_done(&observer);
    CHECK(observer.replies == 5 && observer.echo_bytes == sizeof argument - 1);
    CHECK(!tb_gdb_request(observer.gdb, "-bad\n-exec-run", NULL, 0, TB_GDB_SESSION, 0, 100, NULL));
    CHECK(!tb_gdb_request(observer.gdb, "-bad operation", NULL, 0, TB_GDB_SESSION, 0, 100, NULL));
    CHECK(!tb_gdb_request(observer.gdb, "-echo", NULL, 1, TB_GDB_SESSION, 0, 100, NULL));
    char large[TB_GDB_MAX_COMMAND + 10];
    memset(large, 'x', sizeof large); large[sizeof large - 1] = '\0';
    const char *const too_large[] = {large};
    CHECK(!tb_gdb_request(observer.gdb, "-echo", too_large, 1, TB_GDB_SESSION, 0, 100, NULL));
    CHECK(observer.oversized);
    const char *const injected[] = {"environment", "TEST=value\n99-exec-run"};
    CHECK(!tb_gdb_request(observer.gdb, "-gdb-set", injected, 2, TB_GDB_SESSION, 0, 100, NULL));

    request(&observer, "-stop", TB_GDB_SESSION, 60);
    pump_until(&observer, TB_GDB_STOPPED, 3000);
    TbGdbGeneration before = tb_gdb_generation(observer.gdb);
    request(&observer, "-hold", TB_GDB_CONTEXT, 70);
    tb_gdb_pump(observer.gdb, 0); /* Send before invalidating selected context. */
    tb_gdb_context_changed(observer.gdb);
    request(&observer, "-release", TB_GDB_SESSION, 80);
    pending_done(&observer);
    CHECK(observer.stale == 2);
    request(&observer, "-hold", TB_GDB_CONTEXT, 71);
    tb_gdb_context_changed(observer.gdb); /* Obsolete while still queued. */
    pending_done(&observer);
    CHECK(observer.stale == 3);
    CHECK(tb_gdb_generation(observer.gdb).context > before.context);
    request(&observer, "-hold", TB_GDB_STOP, 90);
    request(&observer, "-run", TB_GDB_SESSION, 100);
    pump_until(&observer, TB_GDB_RUNNING, 3000);
    CHECK(tb_gdb_generation(observer.gdb).stop == before.stop + 1);
    request(&observer, "-release", TB_GDB_SESSION, 110);
    pending_done(&observer);
    CHECK(observer.stale == 4);
    request(&observer, "-stop", TB_GDB_SESSION, 120);
    pump_until(&observer, TB_GDB_STOPPED, 3000);
    request(&observer, "-reordered-run", TB_GDB_SESSION, 130);
    pending_done(&observer);
    CHECK(tb_gdb_state(observer.gdb) == TB_GDB_STOPPED); /* Late ^running cannot undo *stopped. */
    request(&observer, "-target-exit", TB_GDB_SESSION, 140);
    pump_until(&observer, TB_GDB_EXITED, 3000);
    destroy(&observer);
    puts("PASS M04 tokens/reordered replies, quoting, async state and obsolete stop/context generations");
}

static void timeouts(const char *peer)
{
    Observer observer = create();
    start(&observer, peer, "normal");
    CHECK(tb_gdb_request(observer.gdb, "-hold", NULL, 0, TB_GDB_SESSION, 1, 35, NULL));
    pending_done(&observer);
    CHECK(observer.timeouts == 1 && tb_gdb_state(observer.gdb) == TB_GDB_READY);
    request(&observer, "-release", TB_GDB_SESSION, 2);
    pending_done(&observer);
    CHECK(observer.stale == 1 && observer.replies == 1);
    for (size_t i = 0; i < TB_GDB_MAX_PENDING; ++i) { request(&observer, "-hold", TB_GDB_SESSION, i); }
    CHECK(!tb_gdb_request(observer.gdb, "-hold", NULL, 0, TB_GDB_SESSION, 99, 1000, NULL));
    CHECK(tb_gdb_pending(observer.gdb) == TB_GDB_MAX_PENDING);
    close_peer(&observer);
    CHECK(observer.cancelled == TB_GDB_MAX_PENDING);
    uint64_t session = tb_gdb_generation(observer.gdb).session;
    start(&observer, peer, "normal");
    CHECK(tb_gdb_generation(observer.gdb).session > session);
    request(&observer, "-release", TB_GDB_SESSION, 3);
    pending_done(&observer);
    CHECK(observer.replies == 2);
    destroy(&observer);
    puts("PASS M04 deadlines, late replies, queue bound, cancellation and session restart");
}

static void startup_failures(const char *peer)
{
    const char *const modes[] = {"exit", "signal", "hang", "stubborn", "setup-error",
                                "late-fallback", "fallback-fail"};
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        Observer observer = create();
        observer.gdb = tb_gdb_create(emit, &observer);
        CHECK(setenv("TB_FAKE_MODE", modes[i], 1) == 0);
        CHECK(tb_gdb_start(observer.gdb, peer, 70));
        pid_t pid = tb_gdb_pid(observer.gdb);
        pump_until(&observer, TB_GDB_DISCONNECTED, 3000);
        CHECK(observer.diagnostics != 0 && observer.exits ==
              (strcmp(modes[i], "fallback-fail") == 0 ? 2U : 1U));
        CHECK(tb_gdb_mi_version(observer.gdb) ==
              (strcmp(modes[i], "fallback-fail") == 0 ? 2 : 3));
        CHECK(waitpid(pid, NULL, WNOHANG) == -1 && errno == ECHILD);
        if (strcmp(modes[i], "stubborn") == 0) { CHECK(observer.killed); }
        if (strcmp(modes[i], "signal") == 0) { CHECK(observer.term); }
        CHECK(tb_gdb_destroy(observer.gdb));
    }
    Observer observer = create();
    start(&observer, peer, "fallback");
    CHECK(tb_gdb_mi_version(observer.gdb) == 2 && observer.fallback && observer.exits == 1);
    destroy(&observer);
    observer = create(); observer.gdb = tb_gdb_create(emit, &observer);
    CHECK(!tb_gdb_start(observer.gdb, "/nonexistent/the-bugger-gdb", 100));
    CHECK(tb_gdb_pid(observer.gdb) == 0 && observer.diagnostics == 1);
    CHECK(tb_gdb_destroy(observer.gdb));
    puts("PASS M04 missing tool, startup/negotiation timeout/error, MI2 fallback and TERM/KILL reaping");
}

static void transport_failures(const char *peer)
{
    const char *const commands[] = {"-eof", "-truncated", "-malformed", "-die", "-close-input"};
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        Observer observer = create();
        start(&observer, peer, "normal");
        pid_t pid = tb_gdb_pid(observer.gdb);
        request(&observer, commands[i], TB_GDB_SESSION, 1);
        pump_until(&observer, TB_GDB_DISCONNECTED, 3000);
        CHECK(observer.diagnostics != 0 && observer.exits == 1);
        CHECK(observer.cancelled >= 1);
        CHECK(waitpid(pid, NULL, WNOHANG) == -1 && errno == ECHILD);
        if (strcmp(commands[i], "-die") == 0) { CHECK(observer.unexpected_exit); }
        CHECK(tb_gdb_destroy(observer.gdb));
    }
    puts("PASS M04 EOF, truncated/malformed MI, child crash, closed command input and cancelled requests");
}

static void pressure(const char *peer)
{
    Observer observer = create();
    start(&observer, peer, "no-read");
    char data[12001]; memset(data, 'x', sizeof data - 1); data[sizeof data - 1] = '\0';
    const char *const args[] = {data};
    for (int i = 0; i < 48; ++i) {
        CHECK(tb_gdb_request(observer.gdb, "-echo", args, 1, TB_GDB_SESSION, (uint64_t)i, 2500, NULL));
    }
    size_t ticks = 0;
    uint64_t maximum = 0, deadline = tb_gdb_now_ms() + 4000;
    while (tb_gdb_pending(observer.gdb) != 0 && tb_gdb_now_ms() < deadline) {
        uint64_t start_time = tb_gdb_now_ms();
        tb_gdb_pump(observer.gdb, 10);
        uint64_t elapsed = tb_gdb_now_ms() - start_time;
        if (elapsed > maximum) { maximum = elapsed; }
        ++ticks;
    }
    CHECK(observer.replies == 48 && observer.timeouts == 0 && ticks > 5);
    destroy(&observer);
    observer = create(); start(&observer, peer, "normal");
    request(&observer, "-flood", TB_GDB_SESSION, 1);
    deadline = tb_gdb_now_ms() + 4000; ticks = 0;
    while (observer.replies == 0 && tb_gdb_now_ms() < deadline) {
        size_t old_bytes = observer.stderr_bytes, old_records = observer.records;
        uint64_t started = tb_gdb_now_ms();
        tb_gdb_pump(observer.gdb, 10);
        uint64_t elapsed = tb_gdb_now_ms() - started;
        if (elapsed > maximum) { maximum = elapsed; }
        CHECK(observer.stderr_bytes - old_bytes <= TB_GDB_IO_BUDGET);
        CHECK(observer.records - old_records <= TB_GDB_IO_BUDGET / 1024 + 1);
        ++ticks;
    }
    CHECK(observer.replies == 1 && observer.stderr_bytes >= 2 * 1024 * 1024 && ticks > 100);
    request(&observer, "-hold", TB_GDB_SESSION, 2);
    tb_gdb_pump(observer.gdb, 0);
    uint64_t before = tb_gdb_now_ms();
    tb_gdb_pump(observer.gdb, 20);
    uint64_t elapsed = tb_gdb_now_ms() - before;
    CHECK(elapsed >= 10); /* Idle pending input uses poll, rather than busy-spin. */
    destroy(&observer);
    printf("PASS M04 backpressure, short writes/flood fairness; maximum observed pump %" PRIu64 " ms\n", maximum);
}

static volatile sig_atomic_t alarm_seen;
static void alarm_handler(int number) { alarm_seen = number; }

static void interruptions(const char *peer)
{
    Observer observer = create(); start(&observer, peer, "normal");
    struct sigaction action = {.sa_handler = alarm_handler}, old;
    CHECK(sigemptyset(&action.sa_mask) == 0 && sigaction(SIGALRM, &action, &old) == 0);
    struct itimerval timer = {.it_value = {.tv_usec = 5000}};
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    tb_gdb_pump(observer.gdb, 25);
    CHECK(alarm_seen == SIGALRM && tb_gdb_state(observer.gdb) == TB_GDB_READY);
    CHECK(sigaction(SIGALRM, &old, NULL) == 0);
    pid_t pid = tb_gdb_pid(observer.gdb);
    CHECK(kill(pid, SIGTERM) == 0 && waitpid(pid, NULL, 0) == pid);
    pump_until(&observer, TB_GDB_DISCONNECTED, 3000);
    CHECK(tb_gdb_pid(observer.gdb) == 0 && observer.diagnostics != 0);
    CHECK(tb_gdb_destroy(observer.gdb));

    /* A partly transmitted command cannot be retried after its deadline. */
    observer = create(); start(&observer, peer, "no-read");
    char large[TB_GDB_MAX_COMMAND - 100];
    memset(large, 'x', sizeof large); large[sizeof large - 1] = '\0';
    const char *const args[] = {large};
    for (int i = 0; i < 32; ++i) {
        CHECK(tb_gdb_request(observer.gdb, "-echo", args, 1, TB_GDB_SESSION, (uint64_t)i, 45, NULL));
    }
    pump_until(&observer, TB_GDB_DISCONNECTED, 3000);
    CHECK(observer.timeouts >= 1 && observer.diagnostics >= 1);
    CHECK(tb_gdb_destroy(observer.gdb));
    puts("PASS M04 EINTR, externally reaped child ownership and partial-write timeout shutdown");
}

static void resources(const char *peer)
{
    /* An unrelated child must remain alive/unreaped after controller cleanup. */
    pid_t external = fork(); CHECK(external >= 0);
    if (external == 0) { for (;;) { pause(); } }
    for (int i = 0; i < 12; ++i) {
        Observer observer = create(); start(&observer, peer, "inherited-fd"); destroy(&observer);
    }
    CHECK(kill(external, 0) == 0);
    CHECK(kill(external, SIGTERM) == 0 && waitpid(external, NULL, 0) == external);
    /* Resource exhaustion must clean every partially created endpoint. */
    struct rlimit original;
    CHECK(getrlimit(RLIMIT_NOFILE, &original) == 0);
    for (rlim_t limit = 4; limit <= 8; limit += 2) {
        struct rlimit limited = original; limited.rlim_cur = limit;
        CHECK(setrlimit(RLIMIT_NOFILE, &limited) == 0);
        Observer observer = create(); observer.gdb = tb_gdb_create(emit, &observer);
        CHECK(!tb_gdb_start(observer.gdb, peer, 100));
        CHECK(tb_gdb_destroy(observer.gdb));
        CHECK(setrlimit(RLIMIT_NOFILE, &original) == 0);
    }
    char directory[] = "/tmp/the bugger controller-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[256]; (void)snprintf(path, sizeof path, "%s/fake;$(false) peer", directory);
    CHECK(symlink(peer, path) == 0);
    Observer observer = create(); start(&observer, path, "normal"); destroy(&observer);
    CHECK(unlink(path) == 0 && rmdir(directory) == 0);

    pid_t closed_stdio = fork(); CHECK(closed_stdio >= 0);
    if (closed_stdio == 0) {
        for (int fd = 0; fd < 3; ++fd) { (void)close(fd); }
        Observer closed = create(); start(&closed, peer, "normal"); destroy(&closed);
        _exit(0);
    }
    int status;
    CHECK(waitpid(closed_stdio, &status, 0) == closed_stdio && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    puts("PASS M04 repeated lifecycle, descriptor isolation/exhaustion, literal paths, closed stdio and unrelated child survival");
}

int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    CHECK(tb_gdb_create(NULL, NULL) == NULL && tb_gdb_destroy(NULL));
    normal(argv[1]); timeouts(argv[1]); startup_failures(argv[1]);
    transport_failures(argv[1]); pressure(argv[1]); interruptions(argv[1]); resources(argv[1]);
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    return 0;
}
