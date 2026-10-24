#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static TestTerminal arrows, motions;
static bool active[2];
static unsigned action;
static void cleanup(void)
{
    TestTerminal *terms[] = {&arrows, &motions};
    for (int i = 0; i < 2; ++i) {
        if (!active[i] || terms[i]->child <= 1) { continue; }
        pid_t result;
        do { result = waitpid(terms[i]->child, NULL, WNOHANG); } while (result < 0 && errno == EINTR);
        if (!result) {
            (void)kill(terms[i]->child, SIGTERM);
            while (waitpid(terms[i]->child, NULL, 0) < 0 && errno == EINTR) { }
        }
    }
}
static void same(void)
{
    struct timespec now; CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    long long until = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 100;
    do {
        test_terminal_pump(&arrows, 5); test_terminal_pump(&motions, 5);
        CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    } while ((long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 < until);
    for (int row = 0; row < arrows.rows; ++row) {
        if (memcmp(arrows.cells[row], motions.cells[row], (size_t)arrows.cols) != 0 ||
            memcmp(arrows.inverse[row], motions.inverse[row], (size_t)arrows.cols * sizeof(bool)) != 0) {
            fprintf(stderr, "Navigation differs after action %u on row %d:\n%s\n%s\n", action, row, arrows.cells[row], motions.cells[row]);
            CHECK(false);
        }
    }
}
static void keys(const char *ordinary, const char *alias)
{
    ++action;
    test_terminal_send(&arrows, ordinary); test_terminal_send(&motions, alias); same();
}
static void both(const char *input) { keys(input, input); }
static void click(const char *label)
{
    test_terminal_click(&arrows, label); test_terminal_click(&motions, label); same();
}
static void selected(const char *label)
{
    int x, y; CHECK(test_terminal_find(&motions, label, &x, &y));
    if (!motions.inverse[y][x]) { fprintf(stderr, "Expected focus %s after action %u\n", label, action); }
    CHECK(motions.inverse[y][x]);
}
static void mouse_at(int x, int y)
{
    char input[64];
    (void)snprintf(input, sizeof input, "\033[<0;%d;%dM", x + 1, y + 1);
    both(input);
    (void)snprintf(input, sizeof input, "\033[<0;%d;%dm", x + 1, y + 1);
    both(input);
}
static void spatial(void)
{
    selected("Continue");
    keys("\033OD", "h"); selected("Continue"); /* No edge wrap. */
    keys("\033OC", "l"); selected("Pause");
    keys("\033OD", "h"); selected("Continue");
    keys("\033OB", "j");
    int x, y; CHECK(test_terminal_find(&motions, "Pause", &x, &y));
    CHECK(!motions.inverse[y][x]); /* Down leaves the toolbar row. */
    keys("\033OA", "k"); selected("Continue");
    for (int i = 0; i < 8; ++i) { keys("\033OC", "l"); }
    selected("Quit"); keys("\033OC", "l"); selected("Quit");
    for (int i = 0; i < 8; ++i) { keys("\033OD", "h"); }
    selected("Continue");
    /* Tab never acquires a content body or scrollbar, even through a full cycle. */
    for (int i = 0; i < 80; ++i) {
        both("\t"); CHECK(!test_terminal_find(&motions, "CONTENT FOCUS", NULL, NULL));
    }
    /* A deliberate content click gives arrows to that pane, with an explicit indicator. */
    CHECK(test_terminal_find(&motions, "Source", &x, &y));
    mouse_at(x + 12, y + 3); test_terminal_expect(&motions, "CONTENT FOCUS");
    keys("\033OB", "j"); keys("\033OA", "k");
    both("\033"); CHECK(!test_terminal_find(&motions, "CONTENT FOCUS", NULL, NULL));
    mouse_at(x + 12, y + 3); test_terminal_expect(&motions, "CONTENT FOCUS");
    mouse_at(0, motions.rows - 1); CHECK(!test_terminal_find(&motions, "CONTENT FOCUS", NULL, NULL));
    mouse_at(x + 12, y + 3); test_terminal_expect(&motions, "CONTENT FOCUS");
    click("Pause"); CHECK(!test_terminal_find(&motions, "CONTENT FOCUS", NULL, NULL));
}
static void content_selected(const char *label)
{
    test_terminal_expect(&motions, "CONTENT FOCUS");
    for (int y = 0; y < motions.rows - 2; ++y) {
        const char *at = motions.cells[y];
        while ((at = strstr(at, label)) != NULL) {
            int x = (int)(at - motions.cells[y]);
            if ((x >= 1 && motions.cells[y][x - 1] == '*') ||
                (x >= 2 && motions.cells[y][x - 2] == '*')) { return; }
            ++at;
        }
    }
    fprintf(stderr, "Expected content focus in %s after action %u\n", label, action);
    CHECK(false);
}
static void content_hopping(void)
{
    int x, y; CHECK(test_terminal_find(&motions, "Source", &x, &y));
    mouse_at(x + 12, y + 3); content_selected("Source");
    keys("\033OB", "j");
    char row[TEST_COLS + 1]; (void)snprintf(row, sizeof row, "%s", motions.cells[y + 3]);
    keys("\033[1;5D", "\010"); content_selected("Source");
    keys("\033[1;5A", "\013"); content_selected("Source");
    keys("\033[1;5C", "\014"); content_selected("Disassembly");
    keys("\033[1;5D", "\177"); content_selected("Source"); /* Decoded Backspace alias. */
    keys("\033[1;5C", "\014"); content_selected("Disassembly");
    keys("\033[1;5C", "\014"); content_selected("Registers");
    keys("\033[1;5C", "\014"); content_selected("Registers");
    both("\r"); content_selected("Registers"); /* Enter must not become Ctrl+J. */
    keys("\033[1;5B", "\012"); content_selected("Console");
    keys("\033[1;5B", "\012"); content_selected("Console");
    keys("\033[1;5D", "\010"); content_selected("Locals");
    keys("\033[1;5D", "\010"); content_selected("Memory");
    keys("\033[1;5D", "\010"); content_selected("Memory");
    keys("\033[1;5A", "\013"); content_selected("Source");
    CHECK(strcmp(row, motions.cells[y + 3]) == 0); /* Hopping preserves viewports. */
    keys("\033OA", "k"); content_selected("Source");
    both("\033"); CHECK(!test_terminal_find(&motions, "CONTENT FOCUS", NULL, NULL));
    selected("Pause");

    /* A nested pane is the spatial neighbor before the next root column. */
    click("..."); click("Pane actions"); click("Split right");
    CHECK(test_terminal_find(&motions, "Source", &x, &y));
    mouse_at(x + 10, y + 3); content_selected("Source");
    int asm_x, asm_y; CHECK(test_terminal_find(&motions, "Disassembly", &asm_x, &asm_y));
    keys("\033[1;5C", "\014"); content_selected("Disassembly");
    CHECK(asm_x >= 2 && motions.cells[asm_y][asm_x - 2] == '*');
    keys("\033[1;5D", "\010"); content_selected("Source");
    keys("\033[1;5B", "\012"); content_selected("Memory");
    both("\033");
    const char *menu = strstr(motions.cells[asm_y] + asm_x, "..."); CHECK(menu);
    mouse_at((int)(menu - motions.cells[asm_y]), asm_y);
    click("Pane actions"); click("Close split pane");
    test_terminal_expect(&motions, "Split pane closed");
}
static void fullscreen_focus(void)
{
    click("..."); click("Pane actions"); click("Fullscreen Source");
    test_terminal_expect(&motions, "Source | fullscreen");
    for (int i = 0; i < 8; ++i) { keys("\033OC", "l"); }
    selected("Quit"); keys("\033OC", "l"); selected("Quit");
    keys("\033OD", "h"); selected("Session");
    both("\t"); selected("Quit"); both("\t"); selected("[ Restore ]");
    both("\t"); selected("Continue"); /* No hidden panes/body/dividers. */
    for (int i = 0; i < 8; ++i) { keys("\033OC", "l"); }
    int x, y; CHECK(test_terminal_find(&motions, "Source | fullscreen", &x, &y));
    mouse_at(x + 15, y + 3); test_terminal_expect(&motions, "CONTENT FOCUS");
    keys("\033[1;5D", "\010"); content_selected("Source | fullscreen");
    keys("\033[1;5C", "\014"); content_selected("Source | fullscreen");
    keys("\033[1;5A", "\013"); content_selected("Source | fullscreen");
    keys("\033[1;5B", "\012"); content_selected("Source | fullscreen");
    keys("\033OB", "j"); both("\033");
    test_terminal_expect(&motions, "Source | fullscreen"); selected("Quit");
    test_terminal_resize(&arrows, 18, 48); test_terminal_resize(&motions, 18, 48); same();
    selected("Quit");
    test_terminal_resize(&arrows, 54, 166); test_terminal_resize(&motions, 54, 166); same();
    selected("Quit");
    both("\033"); test_terminal_expect(&motions, "Disassembly");
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 && atexit(cleanup) == 0);
    char root[] = "/tmp/the-bugger-navigation-XXXXXX"; CHECK(mkdtemp(root));
    CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    test_terminal_start(&arrows, argv[1], "xterm-256color", true); active[0] = true;
    test_terminal_start(&motions, argv[1], "xterm-256color", true); active[1] = true;
    test_terminal_expect(&arrows, "Start a debugging session");
    test_terminal_expect(&motions, "Start a debugging session"); same();
    keys("\033OB", "j"); keys("\033OA", "k");
    keys("\033OD", "h"); keys("\033OC", "l"); both("\r");
    test_terminal_expect(&motions, "Configure session");
    /* Boolean controls follow arrows too; actual text fields keep all letters. */
    keys("\033OD", "h"); keys("\033OC", "l");
    both("\t\025hjkl"); test_terminal_expect(&motions, "hjkl");
    both("\010X"); test_terminal_expect(&motions, "hjkX"); /* Ctrl+H still edits fields. */
    both("\010l"); test_terminal_expect(&motions, "hjkl");
    both("\033ODh\033OCjkl"); test_terminal_expect(&motions, "hjkhljkl");
    both("\033");
    click("[ Config ]"); click("Themes");
    keys("\033OC", "l"); keys("\033OD", "h");
    both("\t"); keys("\033OC", "l"); keys("\033OD", "h");
    keys("\033OB", "j"); keys("\033OA", "k"); both("\033"); both("\033");
    /* Preview the simulated session, then exercise tabs and focused bodies. */
    both("\r\t\t\t\t\t\r"); test_terminal_expect(&motions, "Review session preview");
    keys("\033OB", "j"); keys("\033OA", "k"); both("\r");
    test_terminal_expect(&motions, "DEMO | Paused");
    spatial();
    content_hopping();
    for (int i = 0; i < 9; ++i) { both("\t"); }
    keys("\033OC", "l"); keys("\033OD", "h");
    both("\t\t\t"); keys("\033OB", "j"); keys("\033OA", "k");
    fullscreen_focus();
    /* Resize controls and confirmations share the same navigation dispatch. */
    click("..."); click("Pane actions"); click("Resize dividers");
    keys("\033OC", "l"); keys("\033OD", "h"); both("\t");
    keys("\033OB", "j"); keys("\033OA", "k"); both("\r");
    click("Reset"); test_terminal_expect(&motions, "Reset simulated execution?");
    keys("\033OC", "l"); keys("\033OD", "h"); both("\033");
    test_terminal_resize(&arrows, 18, 48); test_terminal_resize(&motions, 18, 48); same();
    keys("\033OB", "j"); keys("\033OA", "k");
    click("1 Source");
    int compact_x, compact_y; CHECK(test_terminal_find(&motions, "Symbols", &compact_x, &compact_y));
    mouse_at(5, compact_y + 3); content_selected("Source");
    keys("\033[1;5D", "\010"); content_selected("Source");
    keys("\033[1;5C", "\014"); content_selected("Source");
    keys("\033[1;5A", "\013"); content_selected("Source");
    keys("\033[1;5B", "\012"); content_selected("Source");
    both("\033");
    test_terminal_stop(&arrows, 0); active[0] = false;
    test_terminal_stop(&motions, 0); active[1] = false;
    CHECK(rmdir(root) == 0);
    puts("PASS spatial controls and Ctrl+arrow/hjkl content transfers, nested splits, edges, retained viewports, fullscreen/compact isolation, focus recovery and literal fields");
    return EXIT_SUCCESS;
}
