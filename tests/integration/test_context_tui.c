#include "../support/terminal.h"
#include "../support/check.h"
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static char tmux_socket[80], tmux_config[] = "/tmp/the-bugger-context-tmux-XXXXXX";
static void tmux_cleanup(void)
{
    pid_t child = fork();
    if (child == 0) {
        if (freopen("/dev/null", "w", stderr) == NULL) { _exit(127); }
        execlp("tmux", "tmux", "-L", tmux_socket, "kill-server", (char *)NULL); _exit(127);
    }
    if (child > 0) { while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { } }
    (void)unlink(tmux_config);
}
static void start(TestTerminal *term, const char *binary)
{
    if (!getenv("TB_CONTEXT_TEST_TMUX")) {
        char *const args[] = {(char *)binary, NULL};
        test_terminal_start_argv(term, args, "xterm-256color", false); return;
    }
    (void)snprintf(tmux_socket, sizeof tmux_socket, "the-bugger-context-%ld", (long)getpid());
    int fd = mkstemp(tmux_config); CHECK(fd >= 0);
    const char config[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
    CHECK(write(fd, config, sizeof config - 1) == (ssize_t)(sizeof config - 1)); CHECK(close(fd) == 0);
    CHECK(atexit(tmux_cleanup) == 0 && unsetenv("TMUX") == 0);
    char *args[] = {"tmux", "-L", tmux_socket, "-f", tmux_config, "new-session", "-s", "context", (char *)binary, NULL};
    test_terminal_start_argv(term, args, "xterm-256color", false);
}

int main(int argc, char *argv[])
{
    CHECK(argc == 3);
    TestTerminal term;
    start(&term, argv[1]);
    test_terminal_expect(&term, "Linux application");
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[2]);
    test_terminal_click(&term, "Review session"); test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_click(&term, "Call Stack");
    test_terminal_expect(&term, "Stop a running program");
    test_terminal_click(&term, "Continue F5");
    test_terminal_expect(&term, "recursive | context.c:");
    int x, y;
    CHECK(test_terminal_find(&term, "recursive | context.c:", &x, &y));
    const char *level_text = strchr(term.cells[y], '#'); CHECK(level_text);
    int level; CHECK(sscanf(level_text, "#%d", &level) == 1);
    test_terminal_click(&term, "recursive | context.c:");
    char selected[64]; (void)snprintf(selected, sizeof selected, "LIVE | T1 frame #%d | inspected caller", level);
    test_terminal_expect(&term, selected);
    unsigned clears = term.full_clears, exits = term.alternate_exits;
    unsigned unsynchronized = term.unsynchronized_writes;
    char toolbar[TEST_COLS + 1]; memcpy(toolbar, term.cells[0], sizeof toolbar);
    test_terminal_click(&term, "Read Args");
    test_terminal_expect(&term, selected);
    test_terminal_click(&term, "Details");
    test_terminal_expect(&term, "Frame details | cached snapshot");
    test_terminal_expect(&term, "Arguments (explicitly read): depth=0");
    test_terminal_expect(&term, "Full source path:");
    test_terminal_expect(&term, "context.c");
    test_terminal_expect(&term, "Object: unavailable");
    test_terminal_send(&term, "\033OB\033OB\033[6~\033[5~");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, selected);
    CHECK(term.full_clears == clears && term.alternate_exits == exits);
    CHECK(memcmp(toolbar, term.cells[0], sizeof toolbar) == 0);
    if (!getenv("TB_CONTEXT_TEST_TMUX")) {
        CHECK(term.synchronized_frames > 0 && !term.synchronized_update);
        CHECK(term.unsynchronized_writes == unsynchronized);
    }
    /* Opening the highlighted next frame does not select it in GDB. */
    test_terminal_click(&term, "recursive | context.c:");
    test_terminal_send(&term, "\033OB");
    test_terminal_click(&term, "Details");
    char detail_frame[32]; (void)snprintf(detail_frame, sizeof detail_frame, "Frame: #%d", level + 1);
    test_terminal_expect(&term, detail_frame);
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, selected);
    /* Arrow navigation browses rows; Enter deliberately selects the caller. */
    test_terminal_send(&term, "\r");
    (void)snprintf(selected, sizeof selected, "LIVE | T1 frame #%d", level + 1);
    test_terminal_expect(&term, selected);
    test_terminal_click(&term, "...");
    test_terminal_expect(&term, "SHOW VIEW");
    test_terminal_click(&term, "Threads");
    test_terminal_expect(&term, "T2 stopped");
    test_terminal_click(&term, "T2 stopped");
    test_terminal_expect(&term, "T2 frame #0");
    test_terminal_expect(&term, "worker_main |");
    test_terminal_click(&term, "T1 stopped");
    test_terminal_expect(&term, "T1 frame #0");
    test_terminal_lower(&term, 2, true);
    test_terminal_click(&term, "Continue F5");
    test_terminal_expect(&term, "CONTEXT workers joined");
    test_terminal_expect(&term, "T1 stopped");
    CHECK(!test_terminal_find(&term, "T2 stopped", NULL, NULL));
    test_terminal_click(&term, "Call Stack");
    test_terminal_expect(&term, "LIVE | T1 frame #0");
    test_terminal_click(&term, "Details");
    test_terminal_expect(&term, "LIVE cached frame details");
    test_terminal_click(&term, "Continue F5");
    test_terminal_expect(&term, "STALE cached frame details");
    test_terminal_send(&term, "\033");
    for (int i = 0; i < 500 && test_terminal_find(&term, "Frame details | cached snapshot", NULL, NULL); ++i) { test_terminal_pump(&term, 5); }
    CHECK(!test_terminal_find(&term, "Frame details | cached snapshot", NULL, NULL));
    test_terminal_expect(&term, "Running: retained context is stale");
    test_terminal_click(&term, "Pause");
    test_terminal_expect(&term, "LIVE | T1 frame #0");
    test_terminal_send(&term, "\021");
    test_terminal_expect(&term, "Terminate the launched program");
    test_terminal_click(&term, "Close session");
    test_terminal_stop(&term, 0);
    puts("PASS M06 live Call Stack/Threads mouse and keyboard selection, caller label, thread exit, running stale state and cleanup");
    return 0;
}
