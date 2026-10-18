#include "../support/terminal.h"
#include "../support/check.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

static char tmux_socket[80], tmux_config[] = "/tmp/the-bugger-m10-tmux-XXXXXX";
static void tmux_cleanup(void)
{
    pid_t child = fork();
    if (child == 0) {
        (void)freopen("/dev/null", "w", stderr);
        execlp("tmux", "tmux", "-L", tmux_socket, "kill-server", (char *)NULL); _exit(127);
    }
    if (child > 0) { while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { } }
    (void)unlink(tmux_config);
}
static void start(TestTerminal *t, const char *binary)
{
    if (!getenv("TB_MEMORY_TEST_TMUX")) {
        char *const args[] = {(char *)binary, NULL}; test_terminal_start_argv(t, args, "xterm-256color", false); return;
    }
    (void)snprintf(tmux_socket, sizeof tmux_socket, "the-bugger-m10-%ld", (long)getpid());
    int fd = mkstemp(tmux_config); CHECK(fd >= 0);
    const char config[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
    CHECK(write(fd, config, sizeof config - 1) == (ssize_t)(sizeof config - 1)); CHECK(close(fd) == 0);
    CHECK(atexit(tmux_cleanup) == 0); CHECK(unsetenv("TMUX") == 0);
    char *args[] = {"tmux", "-L", tmux_socket, "-f", tmux_config, "new-session", "-s", "memory", (char *)binary, NULL};
    test_terminal_start_argv(t, args, "xterm-256color", false);
}

static long long milliseconds(void)
{
    struct timespec value; CHECK(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (long long)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}
static void settle(TestTerminal *t)
{
    long long until = milliseconds() + 60;
    while (milliseconds() < until) { test_terminal_pump(t, 5); }
}
static void click_at(TestTerminal *t, int x, int y)
{
    char sequence[64]; (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dM", x + 1, y + 1);
    test_terminal_send(t, sequence); settle(t);
    (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dm", x + 1, y + 1);
    test_terminal_send(t, sequence); settle(t);
}
static void pane_click(TestTerminal *t, const char *text, int column, bool last)
{
    int found_x = -1, found_y = -1;
    long long until = milliseconds() + 2000;
    do {
        int header = -1, left = 0, right = 0;
        /* The default columns have different widths. Locate the lower-row
         * pane boundaries from their rendered menu buttons instead of thirds. */
        for (int y = 0; y < t->rows - 2; ++y) {
            const char *menus[3]; menus[0] = strstr(t->cells[y], "...");
            menus[1] = menus[0] ? strstr(menus[0] + 3, "...") : NULL;
            menus[2] = menus[1] ? strstr(menus[1] + 3, "...") : NULL;
            if (!menus[2]) { continue; }
            header = y; left = column ? (int)(menus[column - 1] - t->cells[y]) + 6 : 0;
            right = (int)(menus[column] - t->cells[y]) + 5;
        }
        if (header >= 0) {
            for (int y = header; y < t->rows - 2; ++y) {
                const char *p = strstr(t->cells[y] + left, text);
                if (p && p - t->cells[y] < right) {
                    found_x = (int)(p - t->cells[y]); found_y = y;
                    if (!last) { break; }
                }
            }
        }
        if (found_x >= 0) { break; }
        test_terminal_pump(t, 20);
    } while (milliseconds() < until);
    if (found_x < 0) {
        fprintf(stderr, "Missing control '%s' in lower pane column %d\n", text, column + 1);
        for (int y = 0; y < t->rows; ++y) { fprintf(stderr, "%s\n", t->cells[y]); }
        (void)kill(t->child, SIGKILL); (void)waitpid(t->child, NULL, 0);
    }
    CHECK(found_x >= 0);
    if (strcmp(text, "Address") == 0) { found_x += 8; }
    click_at(t, found_x, found_y);
}
static void address(TestTerminal *t, int column, const char *expression)
{
    pane_click(t, "Address", column, false);
    test_terminal_send(t, "\025"); test_terminal_send(t, expression); test_terminal_send(t, "\r");
    /* Callers wait for live bytes or a retained read error. The shared
     * session footer may replace the short-lived request acknowledgement. */
}
static unsigned long long first_memory_address(TestTerminal *t)
{
    int x, y; CHECK(test_terminal_find(t, "Memory  Stack", &x, &y));
    CHECK(y + 4 < t->rows); const char *at = strstr(t->cells[y + 4], "0x"); CHECK(at);
    return strtoull(at, NULL, 16);
}
static void browse(TestTerminal *t)
{
    pane_click(t, "4854 2045", 0, false);
    unsigned long long original = first_memory_address(t);
    for (int i = 0; i < 40 && first_memory_address(t) < original + 1024; ++i) {
        test_terminal_send(t, "\033[6~"); settle(t);
    }
    CHECK(first_memory_address(t) == original + 1024);
    /* A wheel-up at the new block's top loads the previous block, with its
     * tail visible. The remaining upward gestures return to the original row. */
    int x, y; CHECK(test_terminal_find(t, "Memory  Stack", &x, &y));
    char wheel[64]; (void)snprintf(wheel, sizeof wheel, "\033[<64;%d;%dM", x + 2, y + 5);
    for (int i = 0; i < 40 && first_memory_address(t) != original; ++i) {
        test_terminal_send(t, wheel); settle(t);
    }
    CHECK(first_memory_address(t) == original);
    test_terminal_expect(t, "4854 2045"); test_terminal_expect(t, "Address");
}
int main(int argc, char **argv)
{
    CHECK(argc == 3); TestTerminal t; start(&t, argv[1]);
    test_terminal_expect(&t, "Native Linux"); test_terminal_send(&t, "\r");
    test_terminal_expect(&t, "Executable / ELF"); test_terminal_send(&t, "\t\025"); test_terminal_send(&t, argv[2]);
    test_terminal_click(&t, "Review session"); test_terminal_click(&t, "Start native");
    test_terminal_expect(&t, "LIVE | Loaded"); test_terminal_click(&t, "Command");
    test_terminal_send(&t, "start\r"); test_terminal_expect(&t, "LIVE | registers");
    test_terminal_expect(&t, "Enter an address above");
    CHECK(!test_terminal_find(&t, "Read memory", NULL, NULL));
    address(&t, 0, "&memory_buffer"); test_terminal_expect(&t, "54 48 45 20");
    test_terminal_expect(&t, "Address");
    pane_click(&t, "54 48 45 20", 0, false);
    for (int i = 0; i < 20; ++i) { test_terminal_send(&t, "\033OC"); }
    test_terminal_expect(&t, "THE BUGGER MEMOR"); test_terminal_expect(&t, "Address");
    for (int i = 0; i < 20; ++i) { test_terminal_send(&t, "\033OD"); }
    test_terminal_expect(&t, "54 48 45 20");
    pane_click(&t, "Options", 0, true); test_terminal_expect(&t, "Memory inspection options");
    test_terminal_click(&t, "Cycle grouping"); test_terminal_expect(&t, "grouping 2B");
    test_terminal_click(&t, "Byte count"); test_terminal_expect(&t, "Memory byte count");
    test_terminal_send(&t, "\0251025\r"); test_terminal_expect(&t, "Enter a byte count from 1 to 1024.");
    test_terminal_send(&t, "\0251024\r"); test_terminal_expect(&t, "requested range 1024 bytes");
    test_terminal_click(&t, "Back"); pane_click(&t, "Refresh", 0, true); test_terminal_expect(&t, "4854 2045");
    browse(&t);
    /* Add a second Memory instance in the lower-middle pane. Its menu and
     * configuration are local; the first Memory pane remains unchanged. */
    pane_click(&t, "Locals", 1, false); pane_click(&t, "...", 1, true);
    pane_click(&t, "Memory", 1, false); address(&t, 1, "&memory_other");
    test_terminal_expect(&t, "49 4e 44 45"); test_terminal_expect(&t, "4854 2045");
    pane_click(&t, "Address", 1, false); test_terminal_send(&t, "\025&memory_other");
    pane_click(&t, "Go", 1, false); test_terminal_expect(&t, "49 4e 44 45");
    pane_click(&t, "Address", 0, false); test_terminal_send(&t, "\025unsubmitted_draft"); settle(&t);
    pane_click(&t, "Stack", 0, false); pane_click(&t, "Memory", 0, false);
    test_terminal_expect(&t, "unsubmitted_draft"); test_terminal_expect(&t, "49 4e 44 45");
    address(&t, 1, "0x1"); test_terminal_expect(&t, "STALE | "); test_terminal_expect(&t, "49 4e 44 45");
    pane_click(&t, "Address", 1, false);
    test_terminal_send(&t, "\025memory_checkpoint()\r"); test_terminal_expect(&t, "Use an address, &buffer");
    CHECK(!test_terminal_find(&t, "Read memory", NULL, NULL));
    test_terminal_send(&t, "\033"); test_terminal_expect(&t, "4854 2045");
    test_terminal_resize(&t, 24, 80); test_terminal_expect(&t, "Memory");
    test_terminal_resize(&t, 44, 140); test_terminal_expect(&t, "4854 2045");
    test_terminal_send(&t, "\021"); test_terminal_expect(&t, "Terminate the launched program");
    test_terminal_click(&t, "Close session"); test_terminal_stop(&t, 0);
    puts(getenv("TB_MEMORY_TEST_TMUX") ? "PASS M10 real tmux live Memory inline entry/Go, keyboard/wheel browsing, pane independence, errors and resize/cleanup" :
         "PASS M10 persistent inline Address/Enter/Go, hex/ASCII, bounded keyboard/wheel browsing, independent drafts/settings, errors, retained snapshot and resize");
    return EXIT_SUCCESS;
}
