#define _POSIX_C_SOURCE 200809L
#include "../support/check.h"
#include "../support/terminal.h"
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    char root[] = "/tmp/tb-mmio-ui-XXXXXX"; CHECK(mkdtemp(root));
    CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0 && setenv("TB_TEST_CONFIG_HOME", root, 1) == 0);
    CHECK(setenv("TB_FAKE_MODE", "remote-big", 1) == 0);
    TestTerminal t; char *const args[] = {argv[1], NULL};
    test_terminal_start_argv(&t, args, "xterm-256color", false); test_terminal_resize(&t, 50, 180);
    test_terminal_click(&t, "Connect to GDB server"); test_terminal_expect(&t, "GDB endpoint");
    test_terminal_send(&t, "\t\025\t\025"); test_terminal_send(&t, argv[2]);
    test_terminal_click(&t, "Review session"); test_terminal_click(&t, "[ Connect ]");
    test_terminal_expect(&t, "LIVE REMOTE"); test_terminal_expect(&t, "LIVE | Stopped");
    test_terminal_lower(&t, 0, true);
    test_terminal_click(&t, "Options"); test_terminal_expect(&t, "Memory inspection options");
    test_terminal_click(&t, "Temporary ranges"); test_terminal_click(&t, "[ Add ]");
    test_terminal_click(&t, "End (excluded)"); test_terminal_send(&t, "\0250x40020020\r");
    test_terminal_click(&t, "Begin:"); test_terminal_send(&t, "\0250x40020010\r");
    test_terminal_click(&t, "Policy: ordinary"); test_terminal_expect(&t, "Policy: MMIO");
    test_terminal_click(&t, "Setup: NOT CONFIRMED");
    test_terminal_expect(&t, "USER CONFIRMATION, not automatic verification");
    test_terminal_expect(&t, "V2J48M35"); test_terminal_expect(&t, "localhost:3333");
    test_terminal_resize(&t, 18, 48);
    for (unsigned i = 0; i < 16 && !test_terminal_find(&t, "I confirm every setup", NULL, NULL); ++i) {
        test_terminal_click(&t, "Next");
    }
    test_terminal_click(&t, "I confirm every setup");
    test_terminal_resize(&t, 50, 180);
    test_terminal_expect(&t, "Setup: USER-CONFIRMED");
    test_terminal_click(&t, "[ Authorize ]"); test_terminal_click(&t, "Authorize without reading");
    test_terminal_expect(&t, "AUTHORIZED | temporary");
    test_terminal_click(&t, "Browse declared MMIO units"); test_terminal_expect(&t, "not sampled");
    test_terminal_expect(&t, "Read unit");
    /* This authored mode returns ^done without a frame. The production
     * adapter must quarantine it and preserve the full diagnostic in Console. */
    test_terminal_click(&t, "Read unit");
    test_terminal_expect(&t, "Untrustworthy MMIO response");
    test_terminal_lower(&t, 2, true); test_terminal_click(&t, "Command");
    test_terminal_send(&t, "echo harmless\r");
    /* Open Memory Options by minimizing Console again, removing ambiguity. */
    test_terminal_lower(&t, 2, false); test_terminal_click(&t, "Options");
    test_terminal_click(&t, "Temporary ranges"); test_terminal_click(&t, "1: 0x40020010");
    test_terminal_expect(&t, "NOT AUTHORIZED"); test_terminal_expect(&t, "Setup: NOT CONFIRMED");
    test_terminal_send(&t, "\021"); test_terminal_click(&t, "Close session"); test_terminal_stop(&t, 0);
    CHECK(rmdir(root) == 0);
    puts("PASS authored remote TUI MMIO setup assertion, range consent, read-free unit navigation, untrustworthy-frame diagnostic and Console revocation");
    return 0;
}
