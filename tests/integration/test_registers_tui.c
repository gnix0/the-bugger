#include "../support/terminal.h"
#include "../support/check.h"

int main(int argc, char **argv)
{
    CHECK(argc == 3); TestTerminal t; char *const args[] = {argv[1], NULL};
    test_terminal_start_argv(&t, args, "xterm-256color", false);
    test_terminal_expect(&t, "Native Linux"); test_terminal_send(&t, "\r");
    test_terminal_expect(&t, "Executable / ELF"); test_terminal_send(&t, "\t\025"); test_terminal_send(&t, argv[2]);
    test_terminal_click(&t, "Review session"); test_terminal_click(&t, "Start native");
    test_terminal_expect(&t, "LIVE | Loaded"); test_terminal_lower(&t, 2, true); test_terminal_click(&t, "Command");
    test_terminal_send(&t, "start\r"); test_terminal_lower(&t, 2, false); test_terminal_expect(&t, "LIVE | registers");
    test_terminal_expect(&t, "rax"); test_terminal_expect(&t, "rip"); test_terminal_expect(&t, "All (ungrouped)");
    test_terminal_click(&t, "Disassembly"); test_terminal_send(&t, "\033[23~");
    test_terminal_expect(&t, "* rip");
    test_terminal_click(&t, "Hex"); test_terminal_expect(&t, "Decimal"); test_terminal_expect(&t, "* rip");
    test_terminal_click(&t, "Registers"); test_terminal_resize(&t, 24, 80);
    test_terminal_expect(&t, "registers"); test_terminal_expect(&t, "Decimal");
    test_terminal_resize(&t, 44, 140); test_terminal_expect(&t, "LIVE | registers");
    test_terminal_send(&t, "\021"); test_terminal_expect(&t, "Terminate the launched program");
    test_terminal_click(&t, "Close session"); test_terminal_stop(&t, 0);
    puts("PASS M09 live Registers pane, target names, changed PC, numeric format control and compact resize");
    return EXIT_SUCCESS;
}
