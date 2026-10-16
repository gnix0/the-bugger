#include "../support/check.h"
#include "../support/terminal.h"

#include <signal.h>

int main(int argc, char *argv[])
{
    CHECK(argc == 3);
    TestTerminal term;
    char *const args[] = {argv[1], argv[2], "normal", NULL};
    test_terminal_start_argv(&term, args, "xterm-256color", false);
    test_terminal_expect(&term, "ready (no target)");
    test_terminal_send(&term, "stop\n");
    test_terminal_expect(&term, "FAKE stopped");
    test_terminal_send(&term, "hold-context\ncontext\nrelease\n");
    test_terminal_expect(&term, "stale: token=");
    test_terminal_send(&term, "run\n");
    test_terminal_expect(&term, "FAKE running");
    test_terminal_send(&term, "flood\nerror\n");
    test_terminal_expect(&term, "class=error");
    test_terminal_send(&term, "hold\n");
    test_terminal_expect(&term, "timeout: token=");
    test_terminal_send(&term, "quit\n");
    test_terminal_expect(&term, "cleanup: disconnected, no owned child");
    test_terminal_stop(&term, 0);

    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) {
        char *const stubborn[] = {argv[1], argv[2], "stubborn", NULL};
        test_terminal_start_argv(&term, stubborn, "xterm-256color", false);
        test_terminal_expect(&term, "state: starting");
        test_terminal_stop(&term, signals[i]);
    }
    puts("PASS M04 interactive fake harness: commands, stale/timeout/flood, terminal preservation and signal cleanup");
    return 0;
}
