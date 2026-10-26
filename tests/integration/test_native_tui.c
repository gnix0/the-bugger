#include "../support/terminal.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char tmux_socket[80], tmux_config[] = "/tmp/the-bugger-console-tmux-XXXXXX";

static void tmux_cleanup(void)
{
    pid_t child = fork();
    if (child == 0) {
        if (freopen("/dev/null", "w", stderr) == NULL) { _exit(127); }
        execlp("tmux", "tmux", "-L", tmux_socket, "kill-server", (char *)NULL);
        _exit(127);
    }
    if (child > 0) { while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { } }
    (void)unlink(tmux_config);
}

static void start(TestTerminal *term, const char *binary, const char *fixture, bool retry)
{
    char *const args[] = {(char *)binary, NULL};
    if (getenv("TB_CONSOLE_TEST_TMUX")) {
        (void)snprintf(tmux_socket, sizeof tmux_socket, "the-bugger-console-%ld", (long)getpid());
        int fd = mkstemp(tmux_config); CHECK(fd >= 0);
        const char config[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
        CHECK(write(fd, config, sizeof config - 1) == (ssize_t)(sizeof config - 1)); CHECK(close(fd) == 0);
        CHECK(atexit(tmux_cleanup) == 0); CHECK(unsetenv("TMUX") == 0);
        char *const tmux_args[] = {"tmux", "-L", tmux_socket, "-f", tmux_config,
                                  "new-session", "-s", "console", (char *)binary, NULL};
        test_terminal_start_argv(term, tmux_args, "xterm-256color", false);
    } else { test_terminal_start_argv(term, args, "xterm-256color", false); }
    test_terminal_expect(term, "Native Linux");
    test_terminal_send(term, "q");
    test_terminal_expect(term, "Already at the launcher");
    test_terminal_send(term, "\r");
    test_terminal_expect(term, "Executable / ELF");
    test_terminal_send(term, "\t\025");
    test_terminal_send(term, fixture);
    if (retry) { test_terminal_send(term, "\t\t\t\t\025/nonexistent/gdb"); }
    test_terminal_click(term, "Review session");
    test_terminal_expect(term, "Review native session");
    test_terminal_click(term, "Start native");
    if (retry) {
        test_terminal_expect(term, "cannot spawn debugger executable");
        test_terminal_expect(term, "/nonexistent/gdb");
        test_terminal_click(term, "[ Back ]");
        test_terminal_send(term, "\t\t\t\t\t\025gdb");
        test_terminal_click(term, "Review session");
        test_terminal_click(term, "Start native");
    }
    test_terminal_expect(term, "LIVE | Loaded");
    test_terminal_expect(term, "PID not started");
    CHECK(strstr(term->output, "Cortex-M4F sample") == NULL);
    CHECK(strstr(term->output, "0x0800042") == NULL);
}

static void console(TestTerminal *term, const char *command)
{
    test_terminal_click(term, "Command");
    test_terminal_expect(term, "LIVE | GDB / program log");
    test_terminal_send(term, command);
    test_terminal_send(term, "\r");
}

static void inline_console(TestTerminal *term)
{
    test_terminal_click(term, "Command");
    test_terminal_expect(term, "Send");
    test_terminal_expect(term, "PID not started");
    int command_x, command_y, send_x, send_y, save_x, save_y;
    CHECK(test_terminal_find(term, "Command", &command_x, &command_y));
    CHECK(test_terminal_find(term, "Send", &send_x, &send_y));
    CHECK(test_terminal_find(term, "Save Log", &save_x, &save_y));
    CHECK(send_y == command_y && save_y == command_y + 2);
    for (int x = command_x; x < send_x + 4; ++x) { CHECK(term->cells[command_y + 1][x] == ' '); }
    CHECK(!test_terminal_find(term, "GDB console command", NULL, NULL));
    test_terminal_send(term, "\r");
    test_terminal_expect(term, "Enter a GDB console command.");
    test_terminal_send(term, "set mi-async off\r");
    test_terminal_expect(term, "Interpreter/async mode is managed");
    int x, y; CHECK(test_terminal_find(term, "Command", &x, &y));
    CHECK(strstr(term->cells[y], "set mi-async off") != NULL);

    /* Printable navigation letters/digits remain text; Backspace edits locally. */
    test_terminal_send(term, "\025echo hjklq123[]X\177\\n");
    test_terminal_click(term, "Send");
    test_terminal_expect(term, "[gdb] hjklq123[]");
    CHECK(test_terminal_find(term, "Command", &x, &y));
    CHECK(strstr(term->cells[y], "hjklq123") == NULL);
    test_terminal_send(term, "echo DRAFT_ONLY");
    test_terminal_expect(term, "echo DRAFT_ONLY");
    CHECK(test_terminal_find(term, "Command", &x, &y));
    test_terminal_send(term, "\033");
    /* Wait for Escape to leave the field, including tmux's forwarding delay. */
    for (int i = 0; i < 500 && term->y == y; ++i) { test_terminal_pump(term, 10); }
    test_terminal_expect(term, "LIVE | Loaded");
    CHECK(!test_terminal_find(term, "Close native session?", NULL, NULL));
    test_terminal_click(term, "Breakpoints");
    test_terminal_expect(term, "Breakpoints");
    test_terminal_click(term, "Console");
    test_terminal_expect(term, "DRAFT_ONLY");
    test_terminal_click(term, "Save Log");
    test_terminal_expect(term, "New log destination");
    test_terminal_send(term, "\033");
    test_terminal_expect(term, "LIVE | GDB / program log");
    test_terminal_expect(term, "DRAFT_ONLY");
    /* Another Console view gets its own draft, not the original pane's text. */
    test_terminal_click(term, "...");
    test_terminal_expect(term, "SHOW VIEW");
    test_terminal_click(term, "Console");
    test_terminal_click(term, "Command");
    CHECK(test_terminal_find(term, "Command", &x, &y));
    CHECK(strstr(term->cells[y], "DRAFT_ONLY") == NULL);
    test_terminal_send(term, "OTHER_DRAFT");
    test_terminal_expect(term, "OTHER_DRAFT");
    test_terminal_click(term, "...");
    test_terminal_click(term, "Pane actions >");
    test_terminal_click(term, "Close selected tab");
    test_terminal_expect(term, "Source");
    test_terminal_expect(term, "DRAFT_ONLY");
    test_terminal_click(term, "Command");
    test_terminal_send(term, "\025echo ");
    for (int i = 0; i < 100; ++i) { test_terminal_send(term, "x"); }
    test_terminal_send(term, "DRAFT_END");
    test_terminal_expect(term, "DRAFT_END");
    test_terminal_resize(term, 24, 80);
    test_terminal_expect(term, "DRAFT_END");
    test_terminal_resize(term, 17, 48);
    test_terminal_expect(term, "Terminal too small");
    test_terminal_expect(term, "Current: 48x17");
    CHECK(!test_terminal_find(term, "DEMO:", NULL, NULL));
    test_terminal_resize(term, 24, 80);
    test_terminal_expect(term, "DRAFT_END");
    test_terminal_click(term, "Command"); /* Resume editing after the control-free warning. */
    test_terminal_send(term, "\001");
    test_terminal_expect(term, "echo xxx");
    CHECK(test_terminal_find(term, "Command", &x, &y));
    CHECK(strstr(term->cells[y], "echo xxx") != NULL);
    CHECK(term->y == y && term->x == x + 7);
    test_terminal_send(term, "\005");
    test_terminal_expect(term, "DRAFT_END");
    test_terminal_resize(term, 44, 140);
    test_terminal_expect(term, "DRAFT_END");
    test_terminal_send(term, "\025echo INLINE_ENTER\\n\r");
    test_terminal_expect(term, "[gdb] INLINE_ENTER");
    test_terminal_send(term, "\033");
    /* Tab reaches the inline field without acquiring content focus. */
    for (int i = 0; i < 150; ++i) {
        test_terminal_send(term, "\t");
        test_terminal_expect(term, "Command");
        CHECK(test_terminal_find(term, "Command", &x, &y));
        if (term->y == y && term->x == x + 7) { break; }
        CHECK(i < 149);
    }
    test_terminal_send(term, "echo KEYBOARD_ONLY\\n\r");
    test_terminal_expect(term, "[gdb] KEYBOARD_ONLY");
    test_terminal_send(term, "\033");
    puts("PASS inline Console Enter/Send, rejected and pane-local drafts, literal keys, keyboard access, caret and resize");
}

int main(int argc, char *argv[])
{
    CHECK(argc == 3);
    TestTerminal term;
    start(&term, argv[1], argv[2], true);
    inline_console(&term);
    if (getenv("TB_CONSOLE_TEST_TMUX")) {
        test_terminal_send(&term, "\021");
        test_terminal_expect(&term, "Close native session?");
        test_terminal_click(&term, "Close session");
        test_terminal_stop(&term, 0);
        puts("PASS inline Console through isolated real tmux, with session and terminal cleanup");
        return EXIT_SUCCESS;
    }
    test_terminal_click(&term, "Continue F5");
    test_terminal_expect(&term, "LIVE | Running");
    test_terminal_expect(&term, "STDERR marker");
    test_terminal_resize(&term, 18, 47);
    test_terminal_expect(&term, "Terminal too small");
    test_terminal_expect(&term, "Current: 47x18");
    CHECK(!test_terminal_find(&term, "DEMO:", NULL, NULL));
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "LIVE | Running");
    CHECK(strstr(term.output, "\033]52") == NULL);
    test_terminal_click(&term, "Pause");
    test_terminal_expect(&term, "LIVE | Stopped");
    console(&term, "this-command-does-not-exist");
    test_terminal_expect(&term, "Undefined command");
    console(&term, "echo q-TUI-CONSOLE-OK\\n");
    test_terminal_expect(&term, "[gdb] q-TUI-CONSOLE-OK");
    char directory[] = "/tmp/the bugger tui-XXXXXX"; CHECK(mkdtemp(directory));
    char path[256]; (void)snprintf(path, sizeof path, "%s/session-q.log", directory);
    test_terminal_click(&term, "Save Log");
    test_terminal_expect(&term, "New log destination");
    test_terminal_send(&term, path); test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Log saved");
    struct stat info; CHECK(stat(path, &info) == 0 && info.st_size > 0);
    test_terminal_send(&term, "q");
    test_terminal_expect(&term, "Terminate the launched program");
    int cancel_x, cancel_y, close_x, close_y;
    CHECK(test_terminal_find(&term, "[ Cancel ]", &cancel_x, &cancel_y));
    CHECK(test_terminal_find(&term, "[ Close session ]", &close_x, &close_y));
    CHECK(cancel_y == close_y && cancel_y > 12 && cancel_y < term.rows - 5);
    CHECK(cancel_x > 10 && close_x > cancel_x && close_x + 17 < term.cols - 10);
    test_terminal_send(&term, "q");
    test_terminal_expect(&term, "LIVE | Stopped");
    test_terminal_send(&term, "q");
    test_terminal_expect(&term, "Close native session?");
    test_terminal_click(&term, "Close session");
    test_terminal_expect(&term, "Start a debugging session");
    test_terminal_stop(&term, 0);
    CHECK(unlink(path) == 0 && rmdir(directory) == 0);

    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) {
        start(&term, argv[1], argv[2], false);
        test_terminal_click(&term, "Continue F5");
        test_terminal_expect(&term, "LIVE | Running");
        test_terminal_stop(&term, signals[i]);
    }
    int pipe_fds[2]; CHECK(pipe(pipe_fds) == 0);
    pid_t external = fork(); CHECK(external >= 0);
    if (external == 0) {
        (void)close(pipe_fds[0]); CHECK(dup2(pipe_fds[1], 1) >= 0 && dup2(pipe_fds[1], 2) >= 0);
        (void)close(pipe_fds[1]);
        execl(argv[2], argv[2], "--allow-attach", (char *)NULL); _exit(127);
    }
    (void)close(pipe_fds[1]);
    CHECK(fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC) == 0);
    char byte;
    do { CHECK(read(pipe_fds[0], &byte, 1) == 1); } while (byte != '\n');
    char *const bare[] = {argv[1], NULL};
    test_terminal_start_argv(&term, bare, "xterm-256color", false);
    test_terminal_expect(&term, "Native Linux");
    test_terminal_send(&term, "\r\t\t'unclosed"); /* Retain invalid Launch args. */
    test_terminal_send(&term, "\033[Z\033[Z\r\t"); /* Toggle Attach; hidden args must not block it. */
    char pid[32]; (void)snprintf(pid, sizeof pid, "%ld", (long)external);
    test_terminal_send(&term, pid);
    test_terminal_click(&term, "Review session");
    test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Stopped");
    test_terminal_send(&term, "\021");
    test_terminal_expect(&term, "Detach and resume");
    test_terminal_click(&term, "Close session");
    test_terminal_stop(&term, 0);
    CHECK(kill(external, 0) == 0 && kill(external, SIGTERM) == 0);
    CHECK(waitpid(external, NULL, 0) == external); (void)close(pipe_fds[0]);
    puts("PASS M05 actual native launcher/Continue/Pause/Console/Save Log/confirmation and signal terminal cleanup");
    return 0;
}
