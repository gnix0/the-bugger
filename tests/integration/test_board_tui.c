#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier): POSIX feature selection. */
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char socket_name[80], configuration[] = "/tmp/tb-m17-tmux-XXXXXX";
static void tmux_cleanup(void)
{
    pid_t pid = fork();
    if (!pid) { execlp("tmux", "tmux", "-L", socket_name, "kill-server", (char *)NULL); _exit(127); }
    if (pid > 0) { while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) { } }
    (void)unlink(configuration);
}
static void start(TestTerminal *t, const char *binary)
{
    char *const args[] = {(char *)binary, NULL};
    if (!getenv("TB_BOARD_TMUX")) { test_terminal_start_argv(t, args, "xterm-256color", false); return; }
    (void)snprintf(socket_name, sizeof socket_name, "tb-m17-%ld", (long)getpid());
    int fd = mkstemp(configuration); CHECK(fd >= 0);
    const char content[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
    CHECK(write(fd, content, sizeof content - 1) == (ssize_t)(sizeof content - 1) && close(fd) == 0);
    CHECK(atexit(tmux_cleanup) == 0 && unsetenv("TMUX") == 0);
    char *const command[] = {"tmux", "-L", socket_name, "-f", configuration, "new-session", "-s", "board", (char *)binary, NULL};
    test_terminal_start_argv(t, command, "xterm-256color", false);
}
static void console(TestTerminal *t, const char *command)
{
    test_terminal_lower(t, 2, true);
    test_terminal_click(t, "Command"); test_terminal_expect(t, "LIVE | GDB / program log");
    test_terminal_send(t, "\025"); test_terminal_send(t, command); test_terminal_send(t, "\r");
}
static unsigned long long pc(const TestTerminal *t)
{
    for (int y = 0; y < t->rows; ++y) {
        const char *p = strstr(t->cells[y], "> 0x"); if (p) { return strtoull(p + 2, NULL, 16); }
    }
    return 0;
}
int main(int argc, char **argv)
{
    CHECK(argc == 5);
    char settings[] = "/tmp/tb-m17-settings-XXXXXX"; CHECK(mkdtemp(settings));
    CHECK(setenv("XDG_CONFIG_HOME", settings, 1) == 0);
    CHECK(setenv("TB_TEST_CONFIG_HOME", settings, 1) == 0);
    TestTerminal t; start(&t, argv[1]); test_terminal_resize(&t, 55, 200);
    test_terminal_click(&t, "Embedded target"); test_terminal_expect(&t, "Board script");
    test_terminal_send(&t, "\t\t\025"); test_terminal_send(&t, argv[4]);
    test_terminal_send(&t, "\t\025"); test_terminal_send(&t, argv[2]);
    test_terminal_send(&t, "\t\025"); test_terminal_send(&t, argv[3]);
    test_terminal_click(&t, "< Yes - managed process >"); test_terminal_expect(&t, "No - existing server");
    test_terminal_click(&t, "Review session"); test_terminal_expect(&t, "Connect to existing server");
    test_terminal_click(&t, "[ Connect ]"); test_terminal_expect(&t, "LIVE OPENOCD"); test_terminal_expect(&t, "LIVE | Stopped");
    console(&t, "hbreak fixture_checkpoint"); test_terminal_expect(&t, "Hardware assisted breakpoint");
    test_terminal_click(&t, "Continue"); test_terminal_expect(&t, "breakpoint-hit"); test_terminal_expect(&t, "workload.c");
    test_terminal_click(&t, "Disassembly");
    for (int i = 0; !pc(&t) && i < 100; ++i) { test_terminal_pump(&t, 10); }
    unsigned long long before = pc(&t); CHECK(before);
    test_terminal_click(&t, "Into");
    for (int i = 0; (!pc(&t) || pc(&t) == before) && i < 200; ++i) { test_terminal_pump(&t, 10); }
    CHECK(pc(&t) && pc(&t) != before);
    test_terminal_lower(&t, 0, true);
    int x = -1, y = -1;
    for (int row = t.rows / 2; row < t.rows; ++row) {
        const char *at = strstr(t.cells[row], "Address");
        if (at) { x = (int)(at - t.cells[row]) + 10; y = row; break; }
    }
    CHECK(x >= 0 && y >= 0);
    char click[64]; (void)snprintf(click, sizeof click, "\033[<0;%d;%dM\033[<0;%d;%dm", x + 1, y + 1, x + 1, y + 1);
    test_terminal_send(&t, click); test_terminal_send(&t, "\025&fixture_buffer\r");
    test_terminal_expect(&t, "31 37 20 53");
    console(&t, "delete $bpnum"); test_terminal_expect(&t, "LIVE OPENOCD");
    test_terminal_click(&t, "Reset"); test_terminal_expect(&t, "Reset and halt target?");
    test_terminal_click(&t, "Cancel"); test_terminal_expect(&t, "LIVE OPENOCD");
    test_terminal_resize(&t, 28, 110); test_terminal_expect(&t, "LIVE OPENOCD");
    test_terminal_resize(&t, 55, 200); test_terminal_expect(&t, "LIVE OPENOCD");
    test_terminal_send(&t, "\021"); test_terminal_expect(&t, "Target on disconnect: Leave halted");
    test_terminal_click(&t, "Close session"); test_terminal_stop(&t, 0);
    CHECK(rmdir(settings) == 0);
    puts("PASS M17 actual board TUI review/connect, real code/PC/instruction step, inline SRAM, reset cancellation, resize and terminal cleanup");
    return 0;
}
