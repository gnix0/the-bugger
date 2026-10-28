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
static long long milliseconds(void)
{
    struct timespec now; CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static void settle(void)
{
    long long deadline = milliseconds() + 60;
    while (milliseconds() < deadline) { test_terminal_pump(&term, 5); }
}
static void cleanup(void)
{
    if (active && term.child > 1) {
        pid_t state;
        do { state = waitpid(term.child, NULL, WNOHANG); } while (state < 0 && errno == EINTR);
        if (state == 0) { (void)kill(term.child, SIGTERM); while (waitpid(term.child, NULL, 0) < 0 && errno == EINTR) { } }
    }
}
static void click(int x, int y)
{
    char sequence[64]; (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dM", x + 1, y + 1);
    test_terminal_send(&term, sequence); settle();
    (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dm", x + 1, y + 1);
    test_terminal_send(&term, sequence); settle();
}
static void watch_click(const char *label)
{
    int x = -1, y = -1; long long deadline = milliseconds() + 5000;
    do {
        int tab_x, tab_y;
        if (test_terminal_find(&term, "Watches", &tab_x, &tab_y)) {
            int left = tab_x > 2 ? tab_x - 2 : 0;
            const char *menu = strstr(term.cells[tab_y] + tab_x, "...");
            int right = menu ? (int)(menu - term.cells[tab_y]) + 3 : left;
            for (int row = tab_y + 1; row < term.rows - 2; ++row) {
                const char *found = strstr(term.cells[row] + left, label);
                if (found && found - term.cells[row] < right) { x = (int)(found - term.cells[row]); y = row; }
            }
        }
        if (x < 0) { test_terminal_pump(&term, 20); }
    } while (x < 0 && milliseconds() < deadline);
    if (x < 0) {
        fprintf(stderr, "Missing Watches control: %s\n", label);
        for (int row = 0; row < term.rows; ++row) { fprintf(stderr, "%s\n", term.cells[row]); }
    }
    CHECK(x >= 0); click(x + 1, y);
}
static void watch_wait(const char *expanded_name)
{
    long long deadline = milliseconds() + 5000;
    while (milliseconds() < deadline) {
        int x, y;
        if (test_terminal_find(&term, "Watches", &x, &y)) {
            const char *menu = strstr(term.cells[y] + x, "...");
            int left = x > 2 ? x - 2 : 0;
            int right = menu ? (int)(menu - term.cells[y]) + 3 : left;
            if (!expanded_name && y + 1 < term.rows) {
                const char *live = strstr(term.cells[y + 1] + left, "LIVE |");
                if (live && live - term.cells[y + 1] < right) { return; }
            } else if (expanded_name) {
                for (int row = y + 3; row < term.rows - 2; ++row) {
                    const char *name = strstr(term.cells[row] + left, expanded_name);
                    if (name && name - term.cells[row] < right && name - term.cells[row] >= left + 2 && name[-2] == '-') { return; }
                }
            }
        }
        test_terminal_pump(&term, 20);
    }
    fprintf(stderr, "Watches did not finish %s\n", expanded_name ? expanded_name : "the explicit read/format");
    CHECK(false);
}
static void add_watch(const char *expression)
{
    fprintf(stderr, "M11 TUI add watch: %s\n", expression);
    watch_click("+ Watch"); test_terminal_expect(&term, "Add watch expression");
    test_terminal_send(&term, expression); test_terminal_send(&term, "\r");
    test_terminal_expect(&term, expression);
}
static void selected_value(const char *expected)
{
    int x, y;
    test_terminal_expect(&term, "children 0");
    CHECK(test_terminal_find(&term, "variables_global", &x, &y));
    CHECK(strstr(term.cells[y], "children 0") && y + 2 < term.rows - 2);
    const char *value = term.cells[y + 2] + x;
    CHECK(strncmp(value, expected, strlen(expected)) == 0 && value[strlen(expected)] == ' ');
    fprintf(stderr, "Selected snapshot value %s verified at centered dialog row %d\n", expected, y + 2);
}
int main(int argc, char **argv)
{
    CHECK(argc == 3 || (argc == 4 && strcmp(argv[3], "--startup-only") == 0)); CHECK(atexit(cleanup) == 0);
    /* test_terminal_start is the demo helper; live tests need a bare argv. */
    char *const arguments[] = {argv[1], NULL};
    test_terminal_start_argv(&term, arguments, "xterm-256color", false); active = true;
    test_terminal_resize(&term, 54, 200);
    test_terminal_expect(&term, "Native Linux");
    CHECK(!test_terminal_find(&term, "DEMO / VISUAL REFERENCE", NULL, NULL));
    if (argc == 4) {
        test_terminal_stop(&term, 0); active = false;
        puts("PASS M11 TUI startup uses the native launcher without --demo and restores the terminal");
        return EXIT_SUCCESS;
    }
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF"); test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[2]);
    test_terminal_click(&term, "Review session"); test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Loaded"); test_terminal_lower(&term, 2, true); test_terminal_click(&term, "Command");
    test_terminal_send(&term, "start\r"); test_terminal_expect(&term, "LIVE | registers");
    test_terminal_lower(&term, 1, true);
    test_terminal_click(&term, "Watches"); test_terminal_expect(&term, "+ Watch");
    add_watch("variables_global"); test_terminal_expect(&term, "Read required");
    watch_click("Read"); watch_wait(NULL);
    /* Do not assert a transient session-footer acknowledgment. Inspect the
     * selected snapshot through the pane's stable Actions screen instead. */
    watch_click("Actions");
    selected_value("17");
    test_terminal_click(&term, "Cycle selected value format");
    watch_wait(NULL); watch_click("Actions");
    test_terminal_expect(&term, "Hex"); selected_value("0x11");
    test_terminal_send(&term, "\033"); add_watch("variables_sample");
    watch_click("variables_sample"); watch_click("Read"); watch_wait(NULL);
    watch_click("Actions"); test_terminal_click(&term, "Expand / collapse");
    watch_wait("variables_sample");
    test_terminal_expect(&term, "count"); watch_click("values");
    watch_click("Actions"); test_terminal_click(&term, "Expand / collapse");
    watch_wait("values"); test_terminal_expect(&term, "int [100]");
    /* Page while the parent is selected; rows are bounded and navigable. */
    watch_click("Actions"); test_terminal_click(&term, "Next child page");
    watch_wait("values"); watch_click("Actions");
    test_terminal_expect(&term, "page 2"); test_terminal_send(&term, "\033");
    add_watch("variables_ready(0)"); test_terminal_expect(&term, "calls and mutations are rejected");
    test_terminal_send(&term, "\033");
    test_terminal_resize(&term, 24, 80); test_terminal_expect(&term, "Watches");
    test_terminal_resize(&term, 54, 200); test_terminal_expect(&term, "variables_sample");
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Terminate the launched program");
    test_terminal_click(&term, "Close session"); test_terminal_stop(&term, 0); active = false;
    puts("PASS M11 live TUI Watches add/read/format, selected-row actions, lazy aggregate/array expansion and paging, rejected calls, resize and terminal cleanup");
    return EXIT_SUCCESS;
}
