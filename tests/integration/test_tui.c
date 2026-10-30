#include "../support/terminal.h"
#include "../support/check.h"
#include "the_bugger/version.h"

#include <signal.h>
#include <string.h>

static void preview(TestTerminal *term, int session)
{
    test_terminal_expect(term, "Start a debugging session");
    for (int i = 0; i < session; ++i) { test_terminal_send(term, "\033OB"); }
    test_terminal_send(term, "\r");
    test_terminal_expect(term, "Configure session");
    int field_count = session == 0 ? 5 : session == 1 ? 7 : 3;
    for (int i = 0; i < field_count; ++i) { test_terminal_send(term, "\t"); }
    test_terminal_send(term, "\r");
    test_terminal_expect(term, "Review session preview");
    test_terminal_send(term, "\r");
    test_terminal_expect(term, "Cortex-M4F sample");
    test_terminal_expect(term, "DEMO | Paused");
}

static void launcher_centered(TestTerminal *term)
{
    test_terminal_expect(term, "Start a debugging session");
    int x, y, vx, vy;
    CHECK(test_terminal_find(term, "THE BUGGER", &x, &y));
    CHECK(test_terminal_find(term, "v" THE_BUGGER_VERSION, &vx, &vy));
    CHECK(y == vy && !test_terminal_find(term, "-beta", NULL, NULL));
    int left = x - 2, right = vx + (int)strlen("v" THE_BUGGER_VERSION) + 2;
    CHECK(abs(left + right - term->cols) <= 1);
    CHECK(abs(2 * (y - 1) + 23 - (term->rows - 2)) <= 1);
}

static void launcher_reference(const char *binary)
{
    TestTerminal term; test_terminal_start(&term, binary, "xterm-256color", false);
    launcher_centered(&term);
    test_terminal_click(&term, "Launch or attach to a native process");
    test_terminal_expect(&term, "Configure session"); test_terminal_send(&term, "\033");
    test_terminal_expect(&term, "Start a debugging session");
    test_terminal_resize(&term, 60, 200); launcher_centered(&term);
    test_terminal_resize(&term, 18, 48);
    test_terminal_expect(&term, "Linux application");
    test_terminal_expect(&term, "Embedded target");
    test_terminal_expect(&term, "Connect to GDB server");
    test_terminal_send(&term, "\033OB\033OB\r");
    test_terminal_expect(&term, "GDB endpoint");
    test_terminal_stop(&term, 0);
    puts("PASS centered reference launcher, version, whole-row description click, large resize and compact three-flow keyboard access");
}

int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    const char *binary = argv[1];
    launcher_reference(binary);
    TestTerminal term;
    for (int session = 0; session < 3; ++session) {
        test_terminal_start(&term, binary, "xterm-256color", false);
        preview(&term, session);
        const char *const panes[] = {"Source", "Disassembly", "Registers", "Memory", "Locals", "Console"};
        for (int pane = 0; pane < 6; ++pane) { test_terminal_expect(&term, panes[pane]); }
        int toolbar_row;
        CHECK(test_terminal_find(&term, "Continue F5", NULL, &toolbar_row));
        CHECK(toolbar_row == 1);
        CHECK(strspn(term.cells[0], " ") == (size_t)term.cols);
        CHECK(!test_terminal_find(&term, "*Source", NULL, NULL));
        CHECK(!test_terminal_find(&term, "+---", NULL, NULL));
        CHECK(!test_terminal_find(&term, "Source focus", NULL, NULL));
        CHECK(!test_terminal_find(&term, "IAR Inspired", NULL, NULL));
        test_terminal_stop(&term, 0);
    }
    puts("PASS M01 launcher: all three keyboard session flows and six DEMO panes");

    test_terminal_start(&term, binary, "xterm-256color", false);
    test_terminal_expect(&term, "Start a debugging session");
    test_terminal_click(&term, "Embedded target");
    test_terminal_expect(&term, "Board script");
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Adapter script");
    test_terminal_expect(&term, "Target script");
    CHECK(!test_terminal_find(&term, "Board script (sample)", NULL, NULL));
    test_terminal_send(&term, "\033");
    test_terminal_expect(&term, "Start a debugging session");
    test_terminal_click(&term, "Linux application");
    test_terminal_expect(&term, "Configure session");
    test_terminal_send(&term, "\t\025path with spaces/firmware.elf");
    test_terminal_expect(&term, "path with spaces/firmware.elf");
    test_terminal_click(&term, "Review preview");
    test_terminal_expect(&term, "Review session preview");
    test_terminal_expect(&term, "path with spaces/firmware.elf");
    test_terminal_click(&term, "Back");
    test_terminal_expect(&term, "Configure session");
    test_terminal_expect(&term, "path with spaces/firmware.elf");
    test_terminal_stop(&term, 0);
    puts("PASS M01 mouse startup, advanced preview and retained text editing");

    test_terminal_start(&term, binary, "xterm-256color", false);
    preview(&term, 0);
    /* Seven tools + Session/Quit, then the first pane's two tabs and menu. */
    for (int i = 0; i < 10; ++i) { test_terminal_send(&term, "\t"); }
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "NOT IMPLEMENTED");
    test_terminal_send(&term, "\033OC\r");
    test_terminal_expect(&term, "main.c | Cortex-M4F sample");
    /* Focus follows the promoted Source tab; skip Symbols to reach its menu. */
    test_terminal_send(&term, "\t\t\r");
    test_terminal_expect(&term, "SHOW VIEW (DEMO)");
    for (int i = 0; i < 6; ++i) { test_terminal_send(&term, "\033OB"); }
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Expression");
    test_terminal_stop(&term, 0);
    puts("PASS M01 keyboard-only tab and view-selector navigation");

    test_terminal_start(&term, binary, "xterm-256color", false);
    preview(&term, 0);
    test_terminal_send(&term, "\033[15~");
    test_terminal_expect(&term, "Running / stale");
    test_terminal_send(&term, "\033[21~");
    test_terminal_expect(&term, "Step over (simulated)");
    test_terminal_expect(&term, "PC 0x0800042C");
    test_terminal_click(&term, "Into F11");
    test_terminal_expect(&term, "Step into (simulated)");
    test_terminal_click(&term, "Out Shift+F11");
    test_terminal_expect(&term, "Step out (simulated)");
    test_terminal_click(&term, "BP F9");
    test_terminal_expect(&term, "Breakpoint toggled");
    test_terminal_click(&term, "Reset");
    test_terminal_expect(&term, "Reset simulated execution?");
    test_terminal_click(&term, "Cancel");
    test_terminal_expect(&term, "PC 0x08000430");
    test_terminal_click(&term, "Reset");
    test_terminal_expect(&term, "Reset simulated execution?");
    test_terminal_click(&term, "[ Reset DEMO state ]");
    test_terminal_expect(&term, "PC 0x0800042A");
    test_terminal_click(&term, "...");
    test_terminal_expect(&term, "SHOW VIEW (DEMO)");
    test_terminal_click(&term, "Watches");
    test_terminal_expect(&term, "Expression");
    test_terminal_click(&term, "Peripherals");
    test_terminal_expect(&term, "NOT IMPLEMENTED");
    test_terminal_resize(&term, 24, 80);
    test_terminal_expect(&term, "1 Source");
    test_terminal_click(&term, "4 Mem");
    test_terminal_expect(&term, "HEX bytes / ASCII");
    test_terminal_resize(&term, 5, 15);
    test_terminal_expect(&term, "Terminal too");
    test_terminal_expect(&term, "Current: 15x5");
    CHECK(!test_terminal_find(&term, "DEMO:", NULL, NULL));
    test_terminal_resize(&term, 18, 47);
    test_terminal_expect(&term, "Current: 47x18");
    test_terminal_expect(&term, "Resize to at least 48x18.");
    test_terminal_expect(&term, "State retained. Ctrl+Q quits.");
    test_terminal_resize(&term, 17, 48);
    test_terminal_expect(&term, "Current: 48x17");
    test_terminal_resize(&term, 18, 48);
    test_terminal_expect(&term, "1 Source");
    CHECK(!test_terminal_find(&term, "Terminal too small", NULL, NULL));
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "Watches");
    test_terminal_expect(&term, "NOT IMPLEMENTED");
    test_terminal_stop(&term, 0);
    puts("PASS M01 toolbar/keys, reset confirmation, views, compact and tiny resize");

    const char *const types[] = {"xterm-direct", "xterm-256color", "vt100", "xterm-256color"};
    const char *const modes[] = {"truecolor", "256-color", "monochrome", "monochrome"};
    for (int i = 0; i < 4; ++i) {
        test_terminal_start(&term, binary, types[i], i == 3);
        preview(&term, 0);
        test_terminal_expect(&term, modes[i]);
        test_terminal_stop(&term, 0);
    }
    puts("PASS M01 direct color, 256-color, vt100 and NO_COLOR rendering");

    CHECK(setenv("COLORTERM", "truecolor", 1) == 0);
    CHECK(unsetenv("TMUX") == 0);
    test_terminal_start(&term, binary, "alacritty", false);
    preview(&term, 0);
    test_terminal_expect(&term, "truecolor");
    test_terminal_stop(&term, 0);
    puts("PASS matching Alacritty direct-color capability selection");

    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    for (int i = 0; i < 3; ++i) {
        test_terminal_start(&term, binary, "xterm-256color", false);
        test_terminal_expect(&term, "Start a debugging session");
        test_terminal_stop(&term, signals[i]);
        CHECK(strstr(term.output, "\033[?1002l") != NULL);
    }
    puts("PASS M01 normal/SIGINT/SIGTERM/SIGHUP terminal restoration");
    return EXIT_SUCCESS;
}
