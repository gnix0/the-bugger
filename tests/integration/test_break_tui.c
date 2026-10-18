#define _POSIX_C_SOURCE 200809L
#include "../support/terminal.h"
#include "../support/check.h"
#include <string.h>
#include <time.h>

static long long now(void)
{
    struct timespec ts; CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void input(TestTerminal *t, const char *text) { test_terminal_send(t, text); test_terminal_send(t, "\r"); }
static void mouse(TestTerminal *t, int x, int y)
{
    char seq[64]; (void)snprintf(seq, sizeof seq, "\033[<0;%d;%dM", x + 1, y + 1);
    test_terminal_send(t, seq); test_terminal_pump(t, 100);
    (void)snprintf(seq, sizeof seq, "\033[<0;%d;%dm", x + 1, y + 1);
    test_terminal_send(t, seq); test_terminal_pump(t, 100);
}
static void source_mark(TestTerminal *t, bool present)
{
    long long until = now() + 5000; bool found = false; int last_x = -1, last_y = -1;
    do {
        test_terminal_pump(t, 50); int x, y;
        if (test_terminal_find(t, "m08_long();", &x, &y)) {
            last_x = x; last_y = y;
            found = memchr(t->cells[y], 'o', 4) != NULL;
            if (found == present) { return; }
        }
    } while (now() < until);
    fprintf(stderr, "Source marker expected=%d found=%d at %d,%d\n", present, found, last_x, last_y);
    for (int row = 0; row < t->rows; ++row) { fprintf(stderr, "%s\n", t->cells[row]); }
    CHECK(false);
}
static void manage(TestTerminal *t, const char *action)
{
    test_terminal_click(t, "Manage"); test_terminal_expect(t, "Breakpoint / data-watchpoint management"); test_terminal_click(t, action);
}
int main(int argc, char *argv[])
{
    CHECK(argc == 3); TestTerminal t; char *const args[] = {argv[1], NULL};
    test_terminal_start_argv(&t, args, "xterm-256color", false);
    test_terminal_expect(&t, "Native Linux"); test_terminal_send(&t, "\r");
    test_terminal_expect(&t, "Executable / ELF"); test_terminal_send(&t, "\t\025"); test_terminal_send(&t, argv[2]);
    test_terminal_click(&t, "Review session"); test_terminal_click(&t, "Start native");
    test_terminal_expect(&t, "LIVE | Loaded"); test_terminal_click(&t, "Command"); input(&t, "start");
    test_terminal_expect(&t, "LIVE | C/C++ | breakpoints.c"); test_terminal_expect(&t, "m08_long();");
    int x, y; CHECK(test_terminal_find(&t, "m08_long();", &x, &y)); mouse(&t, x + 4, y);
    test_terminal_send(&t, "\033[20~"); source_mark(&t, true);
    test_terminal_send(&t, "\033[20~"); source_mark(&t, false);
    CHECK(test_terminal_find(&t, "m08_long();", &x, &y)); mouse(&t, 2, y); source_mark(&t, true);
    test_terminal_send(&t, "\033[20~"); source_mark(&t, false);
    CHECK(test_terminal_find(&t, "m08_result = (unsigned)", &x, &y)); mouse(&t, x + 3, y);
    test_terminal_click(&t, "Actions"); test_terminal_expect(&t, "Code view actions"); test_terminal_click(&t, "Run to Selected");
    long long run_until = now() + 5000; bool reached = false;
    while (!reached && now() < run_until) {
        test_terminal_pump(&t, 50);
        if (test_terminal_find(&t, "m08_result = (unsigned)", &x, &y)) { reached = memchr(t.cells[y], '>', 8) != NULL; }
    }
    CHECK(reached);
    test_terminal_click(&t, "Breakpoints"); test_terminal_expect(&t, "No breakpoints or data watches");
    manage(&t, "Add code breakpoint"); input(&t, "m08_tick"); test_terminal_expect(&t, "m08_tick");
    manage(&t, "Edit condition"); input(&t, "value == 3"); test_terminal_expect(&t, "m08_tick");
    test_terminal_click(&t, "Manage"); test_terminal_expect(&t, "value == 3"); test_terminal_click(&t, "Back");
    manage(&t, "Disable selected"); test_terminal_expect(&t, "m08_tick");
    manage(&t, "Enable selected"); test_terminal_expect(&t, "m08_tick");
    manage(&t, "Set ignore count"); input(&t, "0");
    test_terminal_click(&t, "Continue"); test_terminal_expect(&t, "LIVE | C/C++ | breakpoints.c");
    long long until = now() + 5000;
    while (now() < until) {
        test_terminal_pump(&t, 50);
        if (test_terminal_find(&t, "m08_result = value + 1", NULL, NULL)) { break; }
    }
    test_terminal_expect(&t, "m08_result = value + 1");
    manage(&t, "Remove selected"); test_terminal_expect(&t, "No breakpoints or data watches");
    test_terminal_resize(&t, 24, 80); test_terminal_expect(&t, "Manage");
    test_terminal_resize(&t, 44, 140); test_terminal_expect(&t, "Breakpoints");
    test_terminal_send(&t, "\021"); test_terminal_expect(&t, "Terminate the launched program"); test_terminal_click(&t, "Close session"); test_terminal_stop(&t, 0);
    puts("PASS M08 actual TUI F9 and gutter, live breakpoint list/management, condition, enable/disable, count, hit, removal and resize");
    return EXIT_SUCCESS;
}
