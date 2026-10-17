#include "../support/terminal.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void start(TestTerminal *term, const char *binary, const char *fixture, bool retry)
{
    char *const args[] = {(char *)binary, NULL};
    test_terminal_start_argv(term, args, "xterm-256color", false);
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
    test_terminal_expect(term, "GDB console command");
    test_terminal_send(term, command);
    test_terminal_send(term, "\r");
}

int main(int argc, char *argv[])
{
    CHECK(argc == 3);
    TestTerminal term;
    start(&term, argv[1], argv[2], true);
    test_terminal_click(&term, "Continue F5");
    test_terminal_expect(&term, "LIVE | Running");
    test_terminal_expect(&term, "STDERR marker");
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
