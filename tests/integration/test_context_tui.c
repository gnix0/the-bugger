#include "../support/terminal.h"
#include "../support/check.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    CHECK(argc == 3);
    TestTerminal term;
    char *const args[] = {argv[1], NULL};
    test_terminal_start_argv(&term, args, "xterm-256color", false);
    test_terminal_expect(&term, "Linux application");
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[2]);
    test_terminal_click(&term, "Review session"); test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_click(&term, "Call Stack");
    test_terminal_expect(&term, "Stop a running program");
    test_terminal_click(&term, "Continue F5");
    test_terminal_expect(&term, "recursive(depth=<read required>");
    int x, y;
    CHECK(test_terminal_find(&term, "recursive(depth=<read required>", &x, &y));
    const char *level_text = strchr(term.cells[y], '#'); CHECK(level_text);
    int level; CHECK(sscanf(level_text, "#%d", &level) == 1);
    test_terminal_click(&term, "recursive(depth=<read required>");
    char selected[64]; (void)snprintf(selected, sizeof selected, "LIVE | T1 frame #%d | inspected caller", level);
    test_terminal_expect(&term, selected);
    test_terminal_click(&term, "Read Args");
    test_terminal_expect(&term, "recursive(depth=0");
    /* Restore row focus after clicking the footer control. */
    test_terminal_click(&term, "recursive(depth=0");
    test_terminal_expect(&term, "recursive(depth=<read required>");
    test_terminal_expect(&term, selected);
    /* Arrow navigation browses rows; Enter deliberately selects the caller. */
    test_terminal_send(&term, "\033OB\r");
    (void)snprintf(selected, sizeof selected, "LIVE | T1 frame #%d", level + 1);
    test_terminal_expect(&term, selected);
    test_terminal_click(&term, "...");
    test_terminal_expect(&term, "SHOW VIEW");
    test_terminal_click(&term, "Threads");
    test_terminal_expect(&term, "T2 stopped");
    test_terminal_click(&term, "T2 stopped");
    test_terminal_expect(&term, "T2 frame #0");
    test_terminal_expect(&term, "worker_main(");
    test_terminal_click(&term, "T1 stopped");
    test_terminal_expect(&term, "T1 frame #0");
    test_terminal_lower(&term, 2, true);
    test_terminal_click(&term, "Continue F5");
    test_terminal_expect(&term, "CONTEXT workers joined");
    test_terminal_expect(&term, "T1 stopped");
    CHECK(!test_terminal_find(&term, "T2 stopped", NULL, NULL));
    test_terminal_click(&term, "Continue F5");
    test_terminal_expect(&term, "Running: retained context is stale");
    test_terminal_click(&term, "Pause");
    test_terminal_expect(&term, "T1 stopped");
    test_terminal_send(&term, "\021");
    test_terminal_expect(&term, "Terminate the launched program");
    test_terminal_click(&term, "Close session");
    test_terminal_stop(&term, 0);
    puts("PASS M06 live Call Stack/Threads mouse and keyboard selection, caller label, thread exit, running stale state and cleanup");
    return 0;
}
