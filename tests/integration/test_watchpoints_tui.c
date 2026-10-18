#define _POSIX_C_SOURCE 200809L
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static TestTerminal term;
static bool active;
static long long now(void)
{
    struct timespec value; CHECK(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (long long)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}
static void settle(void)
{
    long long until = now() + 60;
    while (now() < until) { test_terminal_pump(&term, 5); }
}
static void cleanup(void)
{
    if (!active || term.child <= 1) { return; }
    pid_t state;
    do { state = waitpid(term.child, NULL, WNOHANG); } while (state < 0 && errno == EINTR);
    if (state == 0) { (void)kill(term.child, SIGTERM); while (waitpid(term.child, NULL, 0) < 0 && errno == EINTR) { } }
}
static void click(int x, int y)
{
    char sequence[64]; (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dM", x + 1, y + 1);
    test_terminal_send(&term, sequence); settle();
    (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dm", x + 1, y + 1);
    test_terminal_send(&term, sequence); settle();
}
static void pane_click(const char *label, int column)
{
    long long until = now() + 5000;
    do {
        int header = -1, left = 0, right = 0, x = -1, y = -1;
        for (int row = 0; row < term.rows - 2; ++row) {
            const char *menus[3]; menus[0] = strstr(term.cells[row], "...");
            menus[1] = menus[0] ? strstr(menus[0] + 3, "...") : NULL;
            menus[2] = menus[1] ? strstr(menus[1] + 3, "...") : NULL;
            if (!menus[2]) { continue; }
            header = row; left = column ? (int)(menus[column - 1] - term.cells[row]) + 6 : 0;
            right = (int)(menus[column] - term.cells[row]) + 3;
        }
        for (int row = header; header >= 0 && row < term.rows - 2; ++row) {
            const char *found = strstr(term.cells[row] + left, label);
            if (found && found - term.cells[row] < right) { x = (int)(found - term.cells[row]); y = row; }
        }
        if (x >= 0) { click(x + (strcmp(label, "Address") == 0 ? 8 : 1), y); return; }
        test_terminal_pump(&term, 20);
    } while (now() < until);
    fprintf(stderr, "Missing lower pane %d control: %s\n", column, label); CHECK(false);
}
static void menu_click(const char *label)
{
    test_terminal_click(&term, label); settle();
}
int main(int argc, char **argv)
{
    CHECK(argc == 3); CHECK(atexit(cleanup) == 0);
    char *const arguments[] = {argv[1], NULL};
    test_terminal_start_argv(&term, arguments, "xterm-256color", false); active = true;
    test_terminal_resize(&term, 54, 200); test_terminal_expect(&term, "Native Linux");
    test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[2]);
    menu_click("Review session"); menu_click("Start native");
    test_terminal_expect(&term, "LIVE | Loaded"); menu_click("Command"); test_terminal_send(&term, "start\r");
    test_terminal_expect(&term, "LIVE | registers");
    pane_click("Watches", 1); pane_click("+ Watch", 1);
    test_terminal_expect(&term, "Add watch expression"); test_terminal_send(&term, "watched_value\r");
    pane_click("Actions", 1); menu_click("Hardware data watchpoint...");
    test_terminal_expect(&term, "Add hardware data watchpoint");
    CHECK(test_terminal_find(&term, "watched_value", NULL, NULL));
    menu_click("[ Create ]"); test_terminal_expect(&term, "Hardware data watchpoint verified.");
    pane_click("Breakpoints", 2); pane_click("Manage", 2); menu_click("Disable selected");
    pane_click("Manage", 2); menu_click("Enable selected");
    test_terminal_expect(&term, "Hardware data watchpoint verified.");
    test_terminal_send(&term, "\033[15~"); /* F5 */
    test_terminal_expect(&term, "watchpoint-trigger");
    pane_click("Manage", 2); menu_click("Remove selected");
    pane_click("Address", 0); test_terminal_send(&term, "&watched_value\r");
    test_terminal_expect(&term, "14 00 00 00");
    pane_click("Options", 0); menu_click("Hardware watchpoint at selected row");
    test_terminal_expect(&term, "Exact numeric address");
    menu_click("Mode: Value change"); menu_click("Mode: Read");
    menu_click("Width: 1 bytes"); menu_click("Width: 2 bytes");
    menu_click("[ Create ]"); test_terminal_expect(&term, "Hardware data watchpoint verified.");
    pane_click("Manage", 2); menu_click("Remove selected");
    pane_click("Manage", 2); menu_click("Add hardware data watchpoint...");
    test_terminal_send(&term, "watch_checkpoint(0)\r");
    test_terminal_expect(&term, "calls and mutations are rejected");
    test_terminal_send(&term, "\033"); settle();
    test_terminal_resize(&term, 24, 80); settle(); test_terminal_resize(&term, 54, 200);
    test_terminal_expect(&term, "Watches");
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Terminate the launched program");
    menu_click("Close session"); test_terminal_stop(&term, 0); active = false;
    puts("PASS M12 live TUI variable/Memory watchpoint creation, modes/width, hardware verification, hit, enable/disable/remove, rejected calls, resize and terminal cleanup");
    return EXIT_SUCCESS;
}
