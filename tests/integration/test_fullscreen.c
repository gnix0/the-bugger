#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
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
static void cleanup(void)
{
    if (!active || term.child <= 1) { return; }
    pid_t result;
    do { result = waitpid(term.child, NULL, WNOHANG); } while (result < 0 && errno == EINTR);
    if (!result) { (void)kill(term.child, SIGTERM); while (waitpid(term.child, NULL, 0) < 0 && errno == EINTR) { } }
}
static void settle(void)
{
    struct timespec t; CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    long long end = (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000 + 100;
    do { test_terminal_pump(&term, 5); CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0); }
    while ((long long)t.tv_sec * 1000 + t.tv_nsec / 1000000 < end);
}
static void click(const char *label) { test_terminal_click(&term, label); settle(); }
static void keys(const char *input) { test_terminal_send(&term, input); settle(); }
static void mouse(int button, int x, int y, bool release)
{
    char input[64]; (void)snprintf(input, sizeof input, "\033[<%d;%d;%d%c", button, x + 1, y + 1, release ? 'm' : 'M'); keys(input);
}
static int menus(void)
{
    int count = 0;
    for (int row = 0; row < term.rows - 2; ++row) {
        const char *p = term.cells[row];
        while ((p = strstr(p, "...")) != NULL) { ++count; p += 3; }
    }
    return count;
}
static void fullscreen(void)
{
    click("..."); click("Pane actions"); click("Fullscreen Source");
    test_terminal_expect(&term, "Source | fullscreen");
}
static void only_source(void)
{
    test_terminal_expect(&term, "[ Restore ]"); test_terminal_expect(&term, "Continue F5");
    CHECK(test_terminal_find(&term, "Ctrl+Q quit app", NULL, NULL));
    CHECK(!test_terminal_find(&term, "Disassembly", NULL, NULL));
    CHECK(!test_terminal_find(&term, "Registers", NULL, NULL));
    CHECK(!test_terminal_find(&term, "Memory", NULL, NULL));
    CHECK(!test_terminal_find(&term, "Symbols", NULL, NULL));
    CHECK(menus() == 0);
}
static void demo(const char *binary)
{
    test_terminal_start(&term, binary, "xterm-256color", true); active = true;
    test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "Start a debugging session");
    keys("\r\t\t\t\t\t\r\r"); test_terminal_expect(&term, "DEMO | Paused");
    int x, y; CHECK(test_terminal_find(&term, "...", &x, &y));
    mouse(0, x + 5, y + 2, false); mouse(32, x + 10, y + 2, false); mouse(0, x + 10, y + 2, true);
    test_terminal_expect(&term, "Divider resized.");
    click("..."); click("Pane actions"); click("Split right"); test_terminal_expect(&term, "Pane split right.");
    CHECK(menus() == 7);
    int original_x, original_y; CHECK(test_terminal_find(&term, "...", &original_x, &original_y));
    fullscreen(); only_source();
    /* Hidden divider hit regions and pane shortcuts must not steal Source focus. */
    mouse(0, original_x + 5, original_y + 2, false); mouse(0, original_x + 5, original_y + 2, true);
    CHECK(!test_terminal_find(&term, "Dragging divider", NULL, NULL));
    keys("23456789[]hljk"); only_source();
    test_terminal_resize(&term, 18, 48); settle(); test_terminal_expect(&term, "[ Restore ]");
    CHECK(!test_terminal_find(&term, "2 Asm", NULL, NULL));
    test_terminal_resize(&term, 44, 160); settle(); only_source();
    click("[ Restore ]"); test_terminal_expect(&term, "Disassembly"); CHECK(menus() == 7);
    CHECK(test_terminal_find(&term, "...", &x, &y) && x == original_x && y == original_y);
    fullscreen(); keys("\033"); test_terminal_expect(&term, "Disassembly"); CHECK(menus() == 7);
    CHECK(test_terminal_find(&term, "...", &x, &y) && x == original_x && y == original_y);
    /* The restoration action remains reachable without mouse input. */
    fullscreen(); keys("\t\t\t\t\t\t\t\t\t\r");
    test_terminal_expect(&term, "Disassembly"); CHECK(menus() == 7);
    test_terminal_stop(&term, 0); active = false;
    puts("PASS Source fullscreen: header/footer, Source-only rendering, mouse/keyboard/Escape restore, retained resize/splits, compact mode and hidden hit regions");
}
static int source_line(void)
{
    for (int row = 0; row < term.rows - 2; ++row) {
        const char *p = memchr(term.cells[row], '>', 7); int line;
        if (p && sscanf(p + 1, "%d", &line) == 1) { return line; }
    }
    return 0;
}
static void native(const char *binary, const char *fixture)
{
    char *const args[] = {(char *)binary, NULL};
    test_terminal_start_argv(&term, args, "xterm-256color", false); active = true;
    test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "Linux application"); keys("\r\t\025");
    test_terminal_send(&term, fixture); click("Review session"); click("Start native");
    test_terminal_expect(&term, "LIVE | Loaded"); click("Command"); test_terminal_expect(&term, "GDB console command"); keys("start\r");
    test_terminal_expect(&term, "code_counter = 1");
    for (int i = 0; i < 40 && !source_line(); ++i) { settle(); }
    int before = source_line(); CHECK(before > 0);
    click("Actions"); test_terminal_expect(&term, "Code view actions"); click("Fullscreen Source");
    only_source(); CHECK(source_line() == before);
    keys("\033[21~");
    for (int i = 0; i < 40 && (!source_line() || source_line() == before); ++i) { settle(); }
    CHECK(source_line() > 0 && source_line() != before); int after = source_line();
    only_source(); click("[ Restore ]"); test_terminal_expect(&term, "Disassembly"); CHECK(source_line() == after);
    keys("q"); test_terminal_expect(&term, "Terminate the launched program"); click("Close session");
    test_terminal_expect(&term, "Start a debugging session"); test_terminal_stop(&term, 0); active = false;
    puts("PASS live Source Actions fullscreen, unchanged entry PC, source stepping, restoration and owned-session cleanup");
}
int main(int argc, char **argv)
{
    CHECK((argc == 2 || argc == 3) && atexit(cleanup) == 0);
    char root[] = "/tmp/the-bugger-fullscreen-XXXXXX"; CHECK(mkdtemp(root)); CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    if (argc == 3) { native(argv[1], argv[2]); } else { demo(argv[1]); }
    CHECK(rmdir(root) == 0); return EXIT_SUCCESS;
}
