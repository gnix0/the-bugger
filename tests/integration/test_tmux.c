#include "../support/terminal.h"
#include "../support/check.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* This test owns only the server named here, never a user's tmux session. */
static char socket_name[80];
static char configuration[] = "/tmp/the-bugger-tmux-XXXXXX";

static void cleanup(void)
{
    pid_t child = fork();
    if (child == 0) {
        FILE *sink = freopen("/dev/null", "w", stderr);
        (void)sink;
        execlp("tmux", "tmux", "-L", socket_name, "kill-server", (char *)NULL);
        _exit(127);
    }
    if (child > 0) {
        int result;
        while (waitpid(child, &result, 0) < 0 && errno == EINTR) { }
    }
    (void)unlink(configuration);
}

static void send_mouse(TestTerminal *term, int button, int x, int y, bool released)
{
    char sequence[64];
    (void)snprintf(sequence, sizeof sequence, "\033[<%d;%d;%d%c", button, x + 1, y + 1, released ? 'm' : 'M');
    test_terminal_send(term, sequence);
    test_terminal_pump(term, 80);
}

int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    (void)snprintf(socket_name, sizeof socket_name, "the-bugger-m02-%ld", (long)getpid());
    int config = mkstemp(configuration);
    CHECK(config >= 0);
    const char options[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
    CHECK(write(config, options, sizeof options - 1) == (ssize_t)(sizeof options - 1));
    CHECK(close(config) == 0);
    CHECK(atexit(cleanup) == 0);
    CHECK(unsetenv("TMUX") == 0);
    char *arguments[] = {"tmux", "-L", socket_name, "-f", configuration,
                         "new-session", "-s", "workspace", argv[1], NULL};
    TestTerminal term;
    test_terminal_start_argv(&term, arguments, "xterm-256color", false);
    test_terminal_expect(&term, "Start a debugging session");
    test_terminal_send(&term, "\r\t\t\t\t\t\r");
    test_terminal_expect(&term, "Review session preview");
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "DEMO | Paused");
    int x, y;
    for (int repeat = 0; repeat < 2; ++repeat) {
        test_terminal_click(&term, "Call Stack");
        test_terminal_expect(&term, "# Function     Location");
        CHECK(test_terminal_find(&term, "Disassembly", &x, &y));
        CHECK(!term.inverse[y][x]);
        test_terminal_send(&term, "\r");
        test_terminal_expect(&term, "# Function     Location");
        test_terminal_click(&term, "Disassembly");
        test_terminal_expect(&term, "Address    Bytes");
        CHECK(test_terminal_find(&term, "Call Stack", &x, &y));
        CHECK(!term.inverse[y][x]);
    }
    CHECK(test_terminal_find(&term, "...", &x, &y));
    int original_x = x;
    send_mouse(&term, 0, x + 5, y + 2, false);
    test_terminal_expect(&term, "Dragging divider");
    send_mouse(&term, 32, x + 9, y + 2, false);
    send_mouse(&term, 0, x + 9, y + 2, true);
    test_terminal_expect(&term, "Divider resized.");
    CHECK(test_terminal_find(&term, "...", &x, &y));
    CHECK(x == original_x + 4);
    test_terminal_click(&term, "...");
    test_terminal_expect(&term, "SHOW VIEW (DEMO)");
    test_terminal_click(&term, "Pane actions >");
    test_terminal_expect(&term, "PANE ACTIONS");
    test_terminal_click(&term, "Split below");
    test_terminal_expect(&term, "Pane split below.");
    test_terminal_send(&term, "\033[21~");
    test_terminal_expect(&term, "Step over (simulated)");
    test_terminal_resize(&term, 24, 80);
    test_terminal_expect(&term, "7 total");
    test_terminal_click(&term, "Next >");
    test_terminal_expect(&term, "Pane 2");
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "Source");
    FILE *capture = fopen("build/m02-tmux-workspace.txt", "w");
    CHECK(capture != NULL);
    for (int row = 0; row < term.rows; ++row) { CHECK(fprintf(capture, "%s\n", term.cells[row]) > 0); }
    CHECK(fclose(capture) == 0);
    test_terminal_stop(&term, 0);
    puts("PASS real tmux forwarding: tab styling/focus, click, held-button drag, split, F10, compact resize and clean exit");
    return EXIT_SUCCESS;
}
