#include "../support/terminal.h"
#include "../support/check.h"
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <sys/prctl.h>
#include <time.h>

/* Test-owned relay: the real GDB still executes every command. Only selected
 * metadata/register replies are delayed, to expose intermediate UI states. */
static void forward_bytes(int fd, const char *data, size_t size)
{
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) { continue; }
        CHECK(n > 0); data += n; size -= (size_t)n;
    }
}
static int delayed_gdb(char **argv)
{
    int input[2], output[2]; CHECK(pipe(input) == 0 && pipe(output) == 0);
    pid_t owner = getpid(), child = fork(); CHECK(child >= 0);
    if (child == 0) {
        CHECK(prctl(PR_SET_PDEATHSIG, SIGTERM) == 0 && getppid() == owner);
        CHECK(dup2(input[0], STDIN_FILENO) >= 0 && dup2(output[1], STDOUT_FILENO) >= 0);
        (void)close(input[0]); (void)close(input[1]); (void)close(output[0]); (void)close(output[1]);
        char *args[] = {"gdb", argv[1], argv[2], argv[3], NULL}; execvp(args[0], args); _exit(127);
    }
    (void)close(input[0]); (void)close(output[1]);
    char line[65536], bytes[4096]; size_t used = 0; bool feeding = true, reading = true;
    while (reading) {
        struct pollfd fds[2] = {{feeding ? STDIN_FILENO : -1, POLLIN, 0}, {output[0], POLLIN, 0}};
        int n = poll(fds, 2, 5000); if (n < 0 && errno == EINTR) { continue; } CHECK(n >= 0);
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            ssize_t count = read(STDIN_FILENO, bytes, sizeof bytes); CHECK(count >= 0);
            if (count) { forward_bytes(input[1], bytes, (size_t)count); }
            else { (void)close(input[1]); feeding = false; }
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t count = read(output[0], bytes, sizeof bytes); CHECK(count >= 0);
            if (!count) { reading = false; }
            for (ssize_t i = 0; i < count; ++i) {
                CHECK(used + 1 < sizeof line); line[used++] = bytes[i];
                if (bytes[i] == '\n') {
                    line[used] = 0;
                    if (strstr(line, "^done,frame=") || strstr(line, "^done,register-values=")) {
                        struct timespec delay = {.tv_nsec = 250000000};
                        if (getenv("TB_CONTEXT_TIMEOUT") && strstr(line, "^done,frame=") && strstr(line, "func=\"recursive\"")) {
                            delay.tv_sec = 3; delay.tv_nsec = 500000000;
                        }
                        (void)nanosleep(&delay, NULL);
                    }
                    forward_bytes(STDOUT_FILENO, line, used); used = 0;
                }
            }
        }
    }
    if (used) { forward_bytes(STDOUT_FILENO, line, used); }
    if (feeding) { (void)close(input[1]); } (void)close(output[0]);
    int status; CHECK(waitpid(child, &status, 0) == child); return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

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
    if (argc == 4 && strcmp(argv[1], "--quiet") == 0) { return delayed_gdb(argv); }
    CHECK(argc == 3);
    TestTerminal term;
    start(&term, argv[1]);
    test_terminal_expect(&term, "Linux application");
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[2]);
    if (getenv("TB_CONTEXT_DELAY")) {
        char executable[4096]; CHECK(realpath(argv[0], executable));
        test_terminal_send(&term, "\t\t\t\t\025"); test_terminal_send(&term, executable);
    }
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
    if (getenv("TB_CONTEXT_TIMEOUT")) {
        test_terminal_expect(&term, "Context request timed out or failed");
        const struct timespec late = {.tv_sec = 1}; (void)nanosleep(&late, NULL);
        for (int i = 0; i < 20; ++i) { test_terminal_pump(&term, 10); }
        CHECK(!test_terminal_find(&term, selected, NULL, NULL));
        CHECK(!test_terminal_find(&term, "Switching to frame #", NULL, NULL));
        test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Terminate the launched program");
        test_terminal_click(&term, "Close session"); test_terminal_stop(&term, 0);
        puts("PASS real GDB delayed frame metadata timeout, truthful error and rejected late result with cleanup");
        return 0;
    }
    test_terminal_expect(&term, selected);
    test_terminal_expect(&term, "LIVE | registers");
    for (int i = 0; i < 30; ++i) { test_terminal_pump(&term, 10); }
    size_t before_reselect = term.output_length;
    test_terminal_click(&term, "recursive | context.c:");
    for (int i = 0; i < 30; ++i) { test_terminal_pump(&term, 10); }
    test_terminal_expect(&term, selected);
    CHECK(strstr(term.output + before_reselect, "[stale]") == NULL);
    CHECK(strstr(term.output + before_reselect, "Refreshing") == NULL);
    for (int round = 0; round < 4; ++round) {
        int target = round % 2 ? level : level + 1;
        char row_label[64], target_header[96];
        (void)snprintf(row_label, sizeof row_label, "#%d recursive | context.c:", target);
        (void)snprintf(target_header, sizeof target_header, "LIVE | T1 frame #%d | inspected caller", target);
        int source_x, source_y;
        CHECK(test_terminal_find(&term, "Source  Symbols", &source_x, &source_y));
        const char *source_menu = strstr(term.cells[source_y] + source_x, "..."); CHECK(source_menu);
        size_t source_width = (size_t)(source_menu - (term.cells[source_y] + source_x));
        char source_before[14][TEST_COLS + 1];
        for (int r = 0; r < 14; ++r) { memcpy(source_before[r], term.cells[source_y + 1 + r] + source_x, source_width); }
        size_t before = term.output_length;
        test_terminal_click(&term, row_label);
        if (getenv("TB_CONTEXT_DELAY")) {
            test_terminal_expect(&term, "Switching to frame #");
            CHECK(test_terminal_find(&term, "recursive | context.c:", NULL, NULL));
            int previous = target == level ? level + 1 : level;
            char previous_header[80]; (void)snprintf(previous_header, sizeof previous_header, "registers | T1 frame %d |", previous);
            CHECK(test_terminal_find(&term, previous_header, NULL, NULL));
            CHECK(!test_terminal_find(&term, target_header, NULL, NULL));
            for (int r = 0; r < 14; ++r) {
                CHECK(memcmp(source_before[r], term.cells[source_y + 1 + r] + source_x, source_width) == 0);
            }
            int feedback_y; CHECK(test_terminal_find(&term, "Switching to frame #", NULL, &feedback_y));
            CHECK(feedback_y == term.rows - 2);
        }
        test_terminal_expect(&term, target_header);
        CHECK(strstr(term.output + before, "[stale]") == NULL);
        CHECK(strstr(term.output + before, "Refreshing stopped context") == NULL);
        char reg_header[80]; (void)snprintf(reg_header, sizeof reg_header, "registers | T1 frame %d |", target);
        test_terminal_expect(&term, reg_header);
        CHECK(!test_terminal_find(&term, "Switching to frame #", NULL, NULL));
    }
    unsigned clears = term.full_clears, exits = term.alternate_exits;
    unsigned unsynchronized = term.unsynchronized_writes;
    char toolbar[TEST_COLS + 1]; memcpy(toolbar, term.cells[1], sizeof toolbar);
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
    CHECK(memcmp(toolbar, term.cells[1], sizeof toolbar) == 0);
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
    printf("PASS M06 real GDB repeated coherent frame switching, cached Details, threads, resume invalidation and cleanup%s%s\n", getenv("TB_CONTEXT_DELAY") ? " with delayed replies" : "", getenv("TB_CONTEXT_TEST_TMUX") ? " through tmux" : " directly");
    return 0;
}
