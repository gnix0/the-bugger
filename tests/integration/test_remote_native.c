#define _POSIX_C_SOURCE 200809L
#include "session.h"
#include "../support/check.h"
#include "../support/terminal.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t server = -1, fixture = -1;
static TbSession *session;
static void cleanup(void)
{
    if (session) {
        tb_session_close(session); uint64_t until = tb_gdb_now_ms() + 5000;
        while (tb_session_active(session) && tb_gdb_now_ms() < until) { tb_session_pump(session, 1); }
        (void)tb_session_destroy(session); session = NULL;
    }
    /* These children are owned by the test harness, never by the frontend. */
    if (server > 0) { (void)kill(server, SIGKILL); (void)waitpid(server, NULL, 0); server = -1; }
    if (fixture > 0) { (void)kill(fixture, SIGKILL); (void)waitpid(fixture, NULL, 0); fixture = -1; }
}
static pid_t spawn(char *const args[], const char *ready)
{
    char path[] = "/tmp/the-bugger-remote-log-XXXXXX";
    int log = mkstemp(path); CHECK(log >= 0); CHECK(unlink(path) == 0);
    CHECK(fcntl(log, F_SETFD, FD_CLOEXEC) == 0);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        /* gdbserver delivers all-stop Ctrl-C to the inferior process group.
         * Match a separately launched shell job instead of sharing the harness. */
        CHECK(setpgid(0, 0) == 0);
        CHECK(dup2(log, STDOUT_FILENO) >= 0 && dup2(log, STDERR_FILENO) >= 0);
        (void)close(log); execvp(args[0], args); _exit(127);
    }
    char output[8192] = {0}; uint64_t until = tb_gdb_now_ms() + 5000;
    while (!strstr(output, ready) && tb_gdb_now_ms() < until) {
        ssize_t n = pread(log, output, sizeof output - 1, 0);
        if (n >= 0) { output[n] = 0; }
        if (!strstr(output, ready)) { (void)poll(NULL, 0, 10); }
    }
    CHECK(close(log) == 0); /* Child retains its own private regular log. */
    if (!strstr(output, ready)) {
        fprintf(stderr, "Server/fixture startup: %s\n", output);
        (void)kill(child, SIGKILL); (void)waitpid(child, NULL, 0); CHECK(false);
    }
    return child;
}

static void state(TbSessionState expected)
{
    uint64_t until = tb_gdb_now_ms() + 5000;
    while (tb_session_state(session) != expected && tb_gdb_now_ms() < until) { tb_session_pump(session, 1); }
    if (tb_session_state(session) != expected) {
        for (size_t i = 0; i < tb_session_log_count(session); ++i) { fprintf(stderr, "%s\n", tb_session_log_line(session, i)); }
        fprintf(stderr, "state=%s message=%s\n", tb_session_state_name(session), tb_session_message(session)); CHECK(false);
    }
}
static void stopped(void)
{
    const TbContextSnapshot *s = tb_context_snapshot(tb_session_context(session));
    uint64_t until = tb_gdb_now_ms() + 5000;
    do { tb_session_pump(session, 1); }
    while ((tb_session_state(session) != TB_SESSION_STOPPED || !s->ready || s->stale) && tb_gdb_now_ms() < until);
    if (tb_session_state(session) != TB_SESSION_STOPPED || !s->ready || s->stale) {
        for (size_t i = 0; i < tb_session_log_count(session); ++i) { fprintf(stderr, "%s\n", tb_session_log_line(session, i)); }
        fprintf(stderr, "context ready=%d stale=%d error=%s state=%s\n", s->ready, s->stale, s->error, tb_session_state_name(session));
        CHECK(false);
    }
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 || argc == 3 || argc == 4); CHECK(atexit(cleanup) == 0);
    char *const program[] = {argv[1], NULL}; fixture = spawn(program, "REMOTE fixture ready");
    int fd = socket(AF_INET, SOCK_STREAM, 0); CHECK(fd >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(fd, (struct sockaddr *)&address, sizeof address) == 0);
    socklen_t length = sizeof address; CHECK(getsockname(fd, (struct sockaddr *)&address, &length) == 0); CHECK(close(fd) == 0);
    char endpoint[64]; (void)snprintf(endpoint, sizeof endpoint, "127.0.0.1:%u", ntohs(address.sin_port));
    char *const command[] = {"gdbserver", "--multi", endpoint, NULL}; server = spawn(command, "Listening on port");
    session = tb_session_create(); CHECK(session);
    TbSessionConfig config = {.kind = TB_SESSION_EXTENDED_REMOTE, .gdb = "gdb", .endpoint = endpoint, .executable = argv[1]};
    CHECK(tb_session_start(session, &config)); state(TB_SESSION_READY);
    char attach[64]; (void)snprintf(attach, sizeof attach, "attach %ld", (long)fixture);
    tb_context_visible(tb_session_context(session), true, false);
    CHECK(tb_session_console(session, attach)); stopped();
    CHECK(tb_session_target_pid(session) == 0 && !tb_session_capabilities(session)->server_owned);
    TbMemory *memory = tb_session_memory(session); tb_memory_visible(memory, 0, true);
    CHECK(tb_memory_length(memory, 0, 32)); CHECK(tb_memory_address(memory, 0, "&remote_buffer"));
    uint64_t until = tb_gdb_now_ms() + 5000;
    while (tb_memory_view(memory, 0)->pending && tb_gdb_now_ms() < until) { tb_session_pump(session, 1); }
    CHECK(tb_memory_view(memory, 0)->ready && memcmp(tb_memory_view(memory, 0)->bytes, "REMOTE bounded memory", 21) == 0);
    tb_session_close(session); state(TB_SESSION_CLOSED);
    CHECK(kill(server, 0) == 0 && kill(fixture, 0) == 0);
    if (argc != 3) {
        config.kind = TB_SESSION_REMOTE;
        CHECK(tb_session_start(session, &config)); state(TB_SESSION_STOPPED);
        CHECK(tb_session_target_pid(session) == 0);
        CHECK(tb_session_continue(session)); state(TB_SESSION_RUNNING);
        tb_session_close(session); state(TB_SESSION_CLOSED);
        CHECK(kill(server, 0) == 0 && kill(fixture, 0) == 0);
        config.kind = TB_SESSION_EXTENDED_REMOTE; config.executable = NULL;
        CHECK(tb_session_start(session, &config)); state(TB_SESSION_STOPPED);
        CHECK(!*tb_session_capabilities(session)->symbol_image);
        tb_session_close(session); state(TB_SESSION_CLOSED);
        CHECK(kill(server, 0) == 0 && kill(fixture, 0) == 0);
    }
    if (argc == 4) {
        CHECK(strcmp(argv[2], "--tui") == 0);
        TestTerminal term; char *const args[] = {argv[3], NULL};
        test_terminal_start_argv(&term, args, "xterm-256color", false);
        test_terminal_click(&term, "Connect to GDB server");
        test_terminal_expect(&term, "GDB endpoint");
        test_terminal_send(&term, "\025"); test_terminal_send(&term, endpoint);
        test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[1]);
        test_terminal_send(&term, "\t\025gdb");
        test_terminal_click(&term, "Review session");
        test_terminal_expect(&term, "Review remote session");
        test_terminal_click(&term, "[ Connect ]");
        test_terminal_expect(&term, "LIVE REMOTE");
        test_terminal_expect(&term, "LIVE | Stopped");
        test_terminal_expect(&term, "Memory");
        test_terminal_resize(&term, 28, 110);
        test_terminal_expect(&term, "LIVE REMOTE");
        const char *stress = getenv("TB_RENDER_STRESS_SECONDS");
        if (stress) {
            char *end; long seconds = strtol(stress, &end, 10);
            CHECK(*stress && !*end && seconds > 0 && seconds <= 300);
            uint64_t deadline = tb_gdb_now_ms() + (uint64_t)seconds * 1000;
            unsigned rounds = 0;
            while (tb_gdb_now_ms() < deadline) {
                test_terminal_click(&term, "Continue F5"); test_terminal_expect(&term, "LIVE | Running");
                test_terminal_pump(&term, 50);
                test_terminal_click(&term, "Pause"); test_terminal_expect(&term, "LIVE | Stopped");
                test_terminal_send(&term, "2"); /* Select its pane in compact rendering too. */
                test_terminal_click(&term, rounds % 2 ? "Disassembly" : "Call Stack");
                test_terminal_resize(&term, 44, 140);
                test_terminal_expect(&term, "LIVE REMOTE"); test_terminal_expect(&term, "Registers");
                test_terminal_expect(&term, "Memory"); test_terminal_expect(&term, "Console");
                test_terminal_resize(&term, 28, 110);
                test_terminal_expect(&term, rounds % 2 ? "Disassembly" : "Call Stack");
                CHECK(kill(server, 0) == 0 && kill(fixture, 0) == 0);
                ++rounds;
            }
            printf("PASS rendering stress %ld seconds, %u rounds: real localhost remote updates, tabs, resize and external ownership\n", seconds, rounds);
        }
        test_terminal_send(&term, "\021");
        test_terminal_expect(&term, "Disconnect from the existing server");
        test_terminal_click(&term, "Close session"); test_terminal_stop(&term, 0);
        CHECK(kill(server, 0) == 0 && kill(fixture, 0) == 0);
        puts("PASS M15 actual remote launcher/review/live panes/resize/disconnect and terminal restoration");
    }
    if (argc != 3) {
        CHECK(tb_session_start(session, &config)); state(TB_SESSION_STOPPED);
        /* Simulate a genuinely lost external server, not a successful detach. */
        CHECK(kill(server, SIGKILL) == 0); CHECK(waitpid(server, NULL, 0) == server); server = -1;
        CHECK(tb_session_console(session, "x/b &remote_buffer"));
        until = tb_gdb_now_ms() + 5000;
        while (tb_session_state(session) == TB_SESSION_STOPPED && tb_gdb_now_ms() < until) { tb_session_pump(session, 1); }
        CHECK(tb_session_state(session) != TB_SESSION_STOPPED);
        CHECK(!tb_memory_address(tb_session_memory(session), 0, "0x20000000"));
        tb_session_close(session);
        until = tb_gdb_now_ms() + 5000;
        while (tb_session_active(session) && tb_gdb_now_ms() < until) { tb_session_pump(session, 1); }
        CHECK(!tb_session_active(session));
        CHECK(tb_session_start(session, &config)); state(TB_SESSION_FAILED);
        puts("PASS M15 real server loss revokes Memory reads; refused reconnect closes only the owned debugger");
    }
    puts(argc == 3 ? "PASS M15 smoke: external gdbserver, explicit remote attach, bounded Memory and external ownership survival" :
         "PASS M15 real external gdbserver, explicit remote attach, bounded Memory, reconnect and external ownership survival");
    cleanup(); return 0;
}
