#define _POSIX_C_SOURCE 200809L
#include "session.h"
#include "../support/check.h"
#include <arpa/inet.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static unsigned lines;
static void log_line(void *context, const char *line)
{
    (void)context; ++lines;
    for (const unsigned char *p = (const unsigned char *)line; *p; ++p) { CHECK(*p >= 32 && *p < 127); }
}
static unsigned port(int *socket_fd)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0); CHECK(fd >= 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(fd, (struct sockaddr *)&addr, sizeof addr) == 0); socklen_t n = sizeof addr;
    CHECK(getsockname(fd, (struct sockaddr *)&addr, &n) == 0);
    *socket_fd = fd; return ntohs(addr.sin_port);
}
static void server_wait(TbOpenOcd *s, bool ready)
{
    uint64_t until = tb_gdb_now_ms() + 6500;
    while ((ready ? !tb_openocd_ready(s) && tb_openocd_active(s) : tb_openocd_active(s)) && tb_gdb_now_ms() < until) {
        tb_openocd_pump(s); (void)poll(NULL, 0, 1);
    }
}
static void server_case(const char *helper, const char *mode)
{
    CHECK(setenv("TB_FAKE_OPENOCD", mode, 1) == 0);
    TbOpenOcd *s = tb_openocd_create(log_line, NULL); TbOpenOcdPlan *plan = calloc(1, sizeof *plan); CHECK(s && plan);
    int fd; plan->port = port(&fd); CHECK(close(fd) == 0); plan->start_server = true; plan->count = 3;
    (void)snprintf(plan->argv[0], TB_PROFILE_PATH, "%s", helper); (void)snprintf(plan->argv[1], TB_PROFILE_PATH, "-c");
    (void)snprintf(plan->argv[2], TB_PROFILE_PATH, "gdb_port %u", plan->port);
    CHECK(tb_openocd_start(s, plan)); server_wait(s, true);
    bool failure = !strcmp(mode, "exit") || !strcmp(mode, "hang");
    if (!failure) {
        CHECK(tb_openocd_ready(s));
        for (int i = 0; i < 8; ++i) {
            uint64_t start = tb_gdb_now_ms(); tb_openocd_pump(s); CHECK(tb_gdb_now_ms() - start < 100);
        }
        tb_openocd_stop(s);
    }
    server_wait(s, false); CHECK(!tb_openocd_active(s)); CHECK(tb_openocd_failed(s) == failure);
    CHECK(tb_openocd_destroy(s)); free(plan);
}
static bool logged(TbSession *s, const char *text)
{
    for (size_t i = 0; i < tb_session_log_count(s); ++i) { if (strstr(tb_session_log_line(s, i), text)) { return true; } }
    return false;
}
static void pump_state(TbSession *s, TbSessionState expected)
{
    uint64_t until = tb_gdb_now_ms() + 6000;
    while (tb_session_state(s) != expected && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    if (tb_session_state(s) != expected) {
        fprintf(stderr, "Expected state %d; got %s: %s\n", expected, tb_session_state_name(s), tb_session_message(s));
        for (size_t i = 0; i < tb_session_log_count(s); ++i) { fprintf(stderr, "%s\n", tb_session_log_line(s, i)); }
        CHECK(false);
    }
}
static void session_case(const char *helper, const char *peer, bool managed, bool resume, int error)
{
    CHECK(setenv("TB_FAKE_OPENOCD", "ready", 1) == 0);
    CHECK(setenv("TB_FAKE_MODE", error == 1 ? "remote-openocd-error" : error == 2 ? "remote-openocd-flush-error" : error == 3 ? "remote-openocd-verify-error" : error == 4 ? "remote-openocd-resume-error" : error == 5 ? "remote-openocd-current-error" : error == 6 ? "remote-openocd-frame-error" : error == 7 ? "remote-openocd-address-error" : "remote-openocd", 1) == 0);
    TbSession *s = tb_session_create(); TbOpenOcdPlan *plan = calloc(1, sizeof *plan); CHECK(s && plan);
    int fd; unsigned number = port(&fd); CHECK(close(fd) == 0);
    plan->start_server = managed; plan->port = number; plan->count = 3;
    (void)snprintf(plan->endpoint, sizeof plan->endpoint, "127.0.0.1:%u", number);
    (void)snprintf(plan->argv[0], TB_PROFILE_PATH, "%s", helper); (void)snprintf(plan->argv[1], TB_PROFILE_PATH, "-c");
    (void)snprintf(plan->argv[2], TB_PROFILE_PATH, "gdb_port %u", number);
    TbSessionConfig cfg = {.kind = TB_SESSION_REMOTE, .endpoint = plan->endpoint, .gdb = peer, .openocd = plan};
    if (managed) {
        cfg.endpoint = "127.0.0.1:1";
        CHECK(!tb_session_start(s, &cfg) && !tb_session_active(s));
        cfg.endpoint = plan->endpoint;
    }
    CHECK(tb_session_start(s, &cfg)); pump_state(s, TB_SESSION_STOPPED);
    CHECK(tb_session_capabilities(s)->openocd && tb_session_capabilities(s)->server_owned == managed);
    CHECK(tb_session_target_pid(s) == 0 && !logged(s, "M16 reset") && !logged(s, "M16 halt") && !logged(s, "M16 resume"));
    CHECK(tb_session_reset_halt(s)); CHECK(!tb_session_continue(s) && !tb_session_step(s, TB_STEP_INTO, true));
    const char *expected = error == 1 ? "Reset unsupported" : error == 2 ? "Register cache flush unsupported" : (error == 3 || error >= 5) ? "OpenOCD action could not verify" : "OpenOCD command completed";
    uint64_t until = tb_gdb_now_ms() + 2500;
    while (!logged(s, expected) && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    CHECK(logged(s, "M16 reset requested")); CHECK(logged(s, expected));
    if (error && error != 4) {
        CHECK(tb_session_state(s) == TB_SESSION_READY);
        CHECK(!tb_memory_address(tb_session_memory(s), 0, "0x20000000"));
        tb_session_close(s); pump_state(s, TB_SESSION_FAILED); CHECK(logged(s, "disposition remains unverified")); CHECK(tb_session_destroy(s)); free(plan); return;
    }
    CHECK(tb_session_state(s) == TB_SESSION_STOPPED);
    CHECK(logged(s, "M16 flush requested"));
    CHECK(!strcmp(tb_context_snapshot(tb_session_context(s))->stopped.address, "0x8000200"));
    CHECK(tb_session_halt(s)); until = tb_gdb_now_ms() + 1000;
    while (!logged(s, "M16 halt requested") && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    CHECK(logged(s, "M16 halt requested"));
    tb_session_disconnect(s, resume); pump_state(s, error == 4 ? TB_SESSION_FAILED : TB_SESSION_CLOSED);
    CHECK(logged(s, "M16 resume requested") == resume);
    CHECK(!logged(s, "M16 detach requested"));
    if (error == 4) { CHECK(logged(s, "Resume unsupported")); }
    CHECK(!tb_session_active(s) && tb_session_destroy(s)); free(plan);
}
static void crash_case(const char *helper, const char *peer)
{
    CHECK(setenv("TB_FAKE_OPENOCD", "crash", 1) == 0 && setenv("TB_FAKE_MODE", "remote-openocd", 1) == 0);
    TbSession *s = tb_session_create(); TbOpenOcdPlan *p = calloc(1, sizeof *p); CHECK(s && p);
    int fd; p->port = port(&fd); CHECK(close(fd) == 0); p->count = 3; p->start_server = true;
    (void)snprintf(p->endpoint, sizeof p->endpoint, "127.0.0.1:%u", p->port);
    (void)snprintf(p->argv[0], TB_PROFILE_PATH, "%s", helper); (void)snprintf(p->argv[1], TB_PROFILE_PATH, "-c");
    (void)snprintf(p->argv[2], TB_PROFILE_PATH, "gdb_port %u", p->port);
    TbSessionConfig cfg = {.kind = TB_SESSION_REMOTE, .endpoint = p->endpoint, .gdb = peer, .openocd = p};
    CHECK(tb_session_start(s, &cfg)); pump_state(s, TB_SESSION_STOPPED); pump_state(s, TB_SESSION_FAILED);
    CHECK(!tb_session_active(s) && !tb_memory_address(tb_session_memory(s), 0, "0x20000000"));
    CHECK(tb_session_destroy(s)); free(p);
}
int main(int argc, char **argv)
{
    CHECK(argc == 3 || argc == 4);
    server_case(argv[1], "ready"); server_case(argv[1], "exit"); server_case(argv[1], "ignore-term"); server_case(argv[1], "flood");
    if (argc == 4) { server_case(argv[1], "hang"); }
    TbOpenOcd *s = tb_openocd_create(log_line, NULL); TbOpenOcdPlan *p = calloc(1, sizeof *p); CHECK(s && p);
    int fd; p->port = port(&fd); p->count = 1; p->start_server = true;
    (void)snprintf(p->argv[0], TB_PROFILE_PATH, "%s", argv[1]);
    CHECK(!tb_openocd_start(s, p) && !tb_openocd_active(s)); CHECK(strstr(tb_openocd_message(s), "port unavailable")); CHECK(close(fd) == 0);
    (void)snprintf(p->argv[0], TB_PROFILE_PATH, "/definitely/missing/openocd"); CHECK(!tb_openocd_start(s, p)); CHECK(strstr(tb_openocd_message(s), "Cannot start"));
    CHECK(tb_openocd_destroy(s)); free(p);
    crash_case(argv[1], argv[2]);
    session_case(argv[1], argv[2], true, false, false); session_case(argv[1], argv[2], true, true, false);
    session_case(argv[1], argv[2], true, true, 4);
    session_case(argv[1], argv[2], false, false, 5); session_case(argv[1], argv[2], false, false, 6); session_case(argv[1], argv[2], false, false, 7);
    session_case(argv[1], argv[2], false, false, 1);
    session_case(argv[1], argv[2], false, false, 2); session_case(argv[1], argv[2], false, false, 3); session_case(argv[1], argv[2], false, true, false);
    CHECK(unsetenv("TB_FAKE_OPENOCD") == 0 && unsetenv("TB_FAKE_MODE") == 0); CHECK(lines > 0);
    puts("PASS M16 authored server readiness/exit/flood/TERM-KILL/busy-port/missing-tool, shared session ownership, explicit reset/halt errors and halted/resume disconnect");
    return 0;
}
