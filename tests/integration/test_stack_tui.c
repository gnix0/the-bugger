#include "../support/terminal.h"
#include "../support/check.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

static char tmux_socket[80], tmux_config[] = "/tmp/the-bugger-f02-tmux-XXXXXX";
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
static void start(TestTerminal *t, const char *binary)
{
    if (!getenv("TB_STACK_TEST_TMUX")) {
        char *const args[] = {(char *)binary, NULL}; test_terminal_start_argv(t, args, "xterm-256color", false); return;
    }
    (void)snprintf(tmux_socket, sizeof tmux_socket, "the-bugger-f02-%ld", (long)getpid());
    int fd = mkstemp(tmux_config); CHECK(fd >= 0);
    const char config[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
    CHECK(write(fd, config, sizeof config - 1) == (ssize_t)(sizeof config - 1)); CHECK(close(fd) == 0);
    CHECK(atexit(tmux_cleanup) == 0); CHECK(unsetenv("TMUX") == 0);
    char *args[] = {"tmux", "-L", tmux_socket, "-f", tmux_config, "new-session", "-s", "stack", (char *)binary, NULL};
    test_terminal_start_argv(t, args, "xterm-256color", false);
}


static void settle_ui(TestTerminal *t)
{
    struct timespec now; CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    long long until = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 80;
    do {
        test_terminal_pump(t, 5); CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    } while ((long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 < until);
}
static void click(TestTerminal *t, const char *label)
{ test_terminal_click(t, label); settle_ui(t); }
static void back(TestTerminal *t) { test_terminal_send(t, "\033"); settle_ui(t); }
static void option(TestTerminal *t, const char *label)
{
    for (int page = 0; page < 8; ++page) {
        if (test_terminal_find(t, label, NULL, NULL)) { click(t, label); return; }
        click(t, "Next");
    }
    test_terminal_expect(t, label);
}
int main(int argc, char **argv)
{
    CHECK(argc == 3); TestTerminal t; start(&t, argv[1]); test_terminal_resize(&t, 44, 160);
    test_terminal_expect(&t, "Native Linux"); test_terminal_send(&t, "\r");
    test_terminal_expect(&t, "Executable / ELF"); test_terminal_send(&t, "\t\025"); test_terminal_send(&t, argv[2]);
    click(&t, "Review session"); click(&t, "Start native"); test_terminal_expect(&t, "LIVE | Loaded");
    test_terminal_lower(&t, 2, true); click(&t, "Command"); test_terminal_send(&t, "break stack_checkpoint\r");
    test_terminal_pump(&t, 200); click(&t, "Continue");
    test_terminal_expect(&t, "LIVE | registers"); test_terminal_lower(&t, 0, true);
    int tab_x, tab_y; test_terminal_expect(&t, "Memory  Stack"); CHECK(test_terminal_find(&t, "Memory  Stack", &tab_x, &tab_y));
    char tab_click[80]; (void)snprintf(tab_click, sizeof tab_click, "\033[<0;%d;%dM\033[<0;%d;%dm", tab_x + 10, tab_y + 1, tab_x + 10, tab_y + 1);
    test_terminal_send(&t, tab_click); test_terminal_pump(&t, 100); test_terminal_expect(&t, "UNREAD | Follow SP");
    click(&t, "Options"); test_terminal_expect(&t, "Stack options");
    option(&t, "Set requested byte count"); test_terminal_send(&t, "\0251025\r");
    test_terminal_expect(&t, "Invalid literal"); test_terminal_send(&t, "\025128\r");
    test_terminal_expect(&t, "Stack options"); option(&t, "Read / Refresh"); back(&t);
    test_terminal_expect(&t, "SAMPLED | Follow SP");
    settle_ui(&t);
    char stable_source[20][55];
    for (int row = 0; row < 20; ++row) { memcpy(stable_source[row], t.cells[row], sizeof stable_source[row]); }
    unsigned clears = t.full_clears, exits = t.alternate_exits, unsynchronized = t.unsynchronized_writes;
    int stack_x, stack_y; CHECK(test_terminal_find(&t, "Stack  Memory", &stack_x, &stack_y));
    char scroll_event[96];
    (void)snprintf(scroll_event, sizeof scroll_event, "\033[<65;%d;%dM", stack_x + 4, stack_y + 4);
    test_terminal_send(&t, scroll_event); settle_ui(&t);
    const char *menu = strstr(t.cells[stack_y], "..."); CHECK(menu);
    int gutter = (int)(menu - t.cells[stack_y]) + 4, thumb = stack_y + 2;
    while (thumb < t.rows - 3 && t.cells[thumb][gutter] != '#') { ++thumb; }
    CHECK(thumb < t.rows - 3);
    (void)snprintf(scroll_event, sizeof scroll_event, "\033[<0;%d;%dM", gutter + 1, thumb + 1);
    test_terminal_send(&t, scroll_event); settle_ui(&t);
    (void)snprintf(scroll_event, sizeof scroll_event, "\033[<32;%d;%dM", gutter + 1, thumb + 2);
    test_terminal_send(&t, scroll_event); settle_ui(&t);
    (void)snprintf(scroll_event, sizeof scroll_event, "\033[<0;%d;%dm", gutter + 1, thumb + 2);
    test_terminal_send(&t, scroll_event); settle_ui(&t);
    for (int row = 0; row < 20; ++row) { CHECK(memcmp(stable_source[row], t.cells[row], sizeof stable_source[row]) == 0); }
    CHECK(t.full_clears == clears && t.alternate_exits == exits);
    if (!getenv("TB_STACK_TEST_TMUX")) {
        CHECK(t.unsynchronized_writes == unsynchronized && t.synchronized_frames > 0 && !t.synchronized_update);
    }
    click(&t, "Options"); option(&t, "Follow frame-0 SP"); back(&t);
    test_terminal_expect(&t, "SAMPLED | Follow SP");
    click(&t, "Options"); option(&t, "Query selected-frame SP");
    test_terminal_expect(&t, "Selected-frame SP point retained");
    option(&t, "Query local/argument names"); test_terminal_expect(&t, "Stack local/argument names");
    click(&t, "object (query address)"); test_terminal_expect(&t, "may-call-functions is ON");
    click(&t, "Review session-wide no-call change"); test_terminal_expect(&t, "Disable inferior calls session-wide?");
    for (int i = 0; i < 12 && !test_terminal_find(&t, "Disable calls session-wide and query", NULL, NULL); ++i) { click(&t, "Next"); }
    click(&t, "Disable calls session-wide and query"); test_terminal_expect(&t, "Variable address point retained");
    option(&t, "Selected row: complete details");
    test_terminal_expect(&t, "Stack row details");
    for (int i = 0; i < 8 && !test_terminal_find(&t, "provenance:", NULL, NULL); ++i) { click(&t, "Next"); }
    test_terminal_expect(&t, "provenance:"); back(&t); back(&t);
    click(&t, "Options"); option(&t, "Navigate literal address");
    test_terminal_send(&t, "\025+0\r"); test_terminal_expect(&t, "Stack options"); back(&t);
    test_terminal_expect(&t, "UNREAD | Manual");
    click(&t, "Options"); option(&t, "Follow frame-0 SP"); back(&t); test_terminal_expect(&t, "SAMPLED | Follow SP");
    const int widths[] = {48, 80, 123, 124, 160};
    for (size_t i = 0; i < sizeof widths / sizeof widths[0]; ++i) {
        test_terminal_resize(&t, 24, widths[i]); test_terminal_expect(&t, "Stack");
        click(&t, "Options"); option(&t, "Selected row: complete details"); test_terminal_expect(&t, "Stack row details");
        if (test_terminal_find(&t, "Next", NULL, NULL)) { click(&t, "Next"); }
        back(&t); back(&t);
    }
    test_terminal_resize(&t, 44, 160); click(&t, "Options"); option(&t, "Declare bounds");
    test_terminal_expect(&t, "Declared Stack bounds"); test_terminal_expect(&t, "Verified bounds: unknown");
    click(&t, "Low:"); test_terminal_send(&t, "\0250x1\r"); click(&t, "High:"); test_terminal_send(&t, "\0250x100\r");
    click(&t, "Declare ]"); test_terminal_expect(&t, "Stack row details"); back(&t); back(&t);
    /* A minimum-width left pane must retain Options, clipping and details. */
    int divider_x = 0, divider_y = 0;
    for (int row = 3; row < 12; ++row) {
        const char *at = strstr(t.cells[row], "...");
        if (at) { divider_x = (int)(at - t.cells[row]) + 5; divider_y = row + 2; break; }
    }
    if (!divider_x) { for (int row = 0; row < t.rows; ++row) { fprintf(stderr, "%s\n", t.cells[row]); } }
    CHECK(divider_x > 0);
    char sequence[96];
    (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dM", divider_x + 1, divider_y + 1);
    test_terminal_send(&t, sequence); settle_ui(&t);
    (void)snprintf(sequence, sizeof sequence, "\033[<32;22;%dM", divider_y + 1);
    test_terminal_send(&t, sequence); settle_ui(&t);
    (void)snprintf(sequence, sizeof sequence, "\033[<0;22;%dm", divider_y + 1);
    test_terminal_send(&t, sequence); settle_ui(&t);
    const char *minimum_menu = strstr(t.cells[divider_y - 2], "...");
    if (!minimum_menu || minimum_menu - t.cells[divider_y - 2] != 16) { fprintf(stderr, "minimum pane header: %s\n", t.cells[divider_y - 2]); }
    CHECK(minimum_menu && minimum_menu - t.cells[divider_y - 2] == 16); /* x=1, width=20. */
    int narrow_y = -1;
    for (int row = divider_y; row < t.rows; ++row) {
        const char *label = strstr(t.cells[row], "Stack");
        if (label && label - t.cells[row] < 5) { narrow_y = row; break; }
    }
    if (narrow_y < 0) { for (int row = 0; row < t.rows; ++row) { fprintf(stderr, "%s\n", t.cells[row]); } }
    CHECK(narrow_y >= 0); /* Inactive tab labels may be clipped at 20 columns. */
    const char *clip = strchr(t.cells[narrow_y + 2], '>'); CHECK(clip && clip - t.cells[narrow_y + 2] < 21);
    (void)snprintf(sequence, sizeof sequence, "\033[<0;4;%dM", narrow_y + 3);
    test_terminal_send(&t, sequence); settle_ui(&t);
    (void)snprintf(sequence, sizeof sequence, "\033[<0;4;%dm", narrow_y + 3);
    test_terminal_send(&t, sequence); settle_ui(&t);
    test_terminal_send(&t, "\033OC\033OC\033OB"); settle_ui(&t);
    CHECK(t.cells[narrow_y + 2][2] == '<');
    test_terminal_send(&t, "\r"); test_terminal_expect(&t, "Stack row details"); back(&t); back(&t);
    test_terminal_send(&t, "\021"); test_terminal_expect(&t, "Terminate the launched program"); click(&t, "Close session"); test_terminal_stop(&t, 0);
    puts("PASS F02 native PTY Stack read/navigation/follow, exact size errors, complete paginated details, declared bounds, minimum pane and widths 48/80/123/124/160 with cleanup");
    return 0;
}
