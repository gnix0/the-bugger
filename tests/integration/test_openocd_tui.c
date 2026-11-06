#define _POSIX_C_SOURCE 200809L
#include "../support/check.h"
#include "../support/terminal.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
static char tmux_socket[80], tmux_config[] = "/tmp/the-bugger-script-tmux-XXXXXX";
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
    if (!getenv("TB_OPENOCD_TEST_TMUX")) {
        char *const args[] = {(char *)binary, NULL}; test_terminal_start_argv(t, args, "xterm-256color", false); return;
    }
    if (!*tmux_socket) {
        (void)snprintf(tmux_socket, sizeof tmux_socket, "the-bugger-script-%ld", (long)getpid());
        int fd = mkstemp(tmux_config); CHECK(fd >= 0);
        const char config[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
        CHECK(write(fd, config, sizeof config - 1) == (ssize_t)(sizeof config - 1)); CHECK(close(fd) == 0);
        CHECK(atexit(tmux_cleanup) == 0); CHECK(unsetenv("TMUX") == 0);
    }
    char *args[] = {"tmux", "-L", tmux_socket, "-f", tmux_config, "new-session", "-s", "scripts", (char *)binary, NULL};
    test_terminal_start_argv(t, args, "xterm-256color", false);
}

static TestTerminal term;
static void mouse_event_at(int button, int x, int y, bool release)
{
    char event[80]; (void)snprintf(event, sizeof event, "\033[<%d;%d;%d%c", button, x + 1, y + 1, release ? 'm' : 'M');
    test_terminal_send(&term, event);
    for (int i = 0; i < 20; ++i) { test_terminal_pump(&term, 5); }
}
static void scrollbar(int *x, int *top, int *bottom)
{
    test_terminal_expect_scrollbar(&term, true);
    CHECK(test_terminal_find_scrollbar(&term, true, x, top)); *bottom = *top;
    while (*top > 0 && (test_terminal_scroll_cell(&term, *top - 1, *x, false) || test_terminal_scroll_cell(&term, *top - 1, *x, true))) { --*top; }
    while (*bottom + 1 < term.rows && (test_terminal_scroll_cell(&term, *bottom + 1, *x, false) || test_terminal_scroll_cell(&term, *bottom + 1, *x, true))) { ++*bottom; }
}
static int panel_geometry(bool keys)
{
    int x = 0, y = 0, bx = 0, by = 0, sx = 0, sy = 0, ry = 0;
    int top = 0, height = 0;
    bool centered = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (test_terminal_find(&term, keys ? "Keybinds" : "Filter:", &x, &y) &&
            test_terminal_find(&term, "[ Back ]", &bx, &by) &&
            (!keys || (test_terminal_find(&term, "[ Save ]", &sx, &sy) && test_terminal_find(&term, "Restore Defaults", NULL, &ry)))) {
            top = keys && term.rows >= 26 ? y - 1 : y;
            int last = keys && term.cols < 64 ? ry : by;
            height = last - top + (keys ? (term.cols < 64 || term.rows < 26 ? 2 : 3) : 2);
            centered = top >= 4 && top + height < term.rows - 2 && abs(2 * top + height - (term.rows + 1)) <= 1;
            if (keys) { centered = centered && abs((x - 2) + sx + 10 - term.cols) <= 1; }
            else { centered = centered && abs(2 * (x - 2) + (term.cols < 100 ? term.cols - 4 : 96) - term.cols) <= 1; }
            if (term.rows >= 40) { centered = centered && top > 4; }
            if (centered) { return height; }
        }
        test_terminal_pump(&term, 10);
    }
    fprintf(stderr, "Menu geometry: keys=%d size=%dx%d top=%d height=%d\n", keys, term.cols, term.rows, top, height);
    CHECK(centered); return height;
}
static void centered_menus(const char *binary)
{
    static const int sizes[][2] = {{60, 256}, {44, 160}, {90, 160}, {28, 110}, {20, 80}, {60, 256}};
    int heights[sizeof sizes / sizeof *sizes];
    start(&term, binary); test_terminal_click(&term, "[ Config ]"); test_terminal_click(&term, "Keybinds");
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        test_terminal_resize(&term, sizes[i][0], sizes[i][1]); heights[i] = panel_geometry(true);
    }
    test_terminal_click(&term, "[ Back ]"); test_terminal_click(&term, "[ Back ]");
    test_terminal_click(&term, "Embedded target"); test_terminal_click(&term, "Browse boards");
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        test_terminal_resize(&term, sizes[i][0], sizes[i][1]); CHECK(panel_geometry(false) == heights[i]);
    }
    test_terminal_send(&term, "\021"); test_terminal_stop(&term, 0);
}
static void script_navigation(const char *binary, const char *root)
{
    start(&term, binary);
    test_terminal_resize(&term, 28, 110);
    test_terminal_click(&term, "Embedded target");
    test_terminal_send(&term, "\t\t\t\t\t\t"); test_terminal_expect(&term, "Start OpenOCD");
    test_terminal_send(&term, "\033OB\r"); test_terminal_expect(&term, "Installed OpenOCD");
    CHECK(!test_terminal_find(&term, "Previous", NULL, NULL)); CHECK(!test_terminal_find(&term, "Next", NULL, NULL));
    test_terminal_click(&term, "[ Filter ]"); test_terminal_send(&term, root); test_terminal_send(&term, "\r");
    int x, top, bottom; scrollbar(&x, &top, &bottom);
    mouse_event_at(0, x, bottom, false); mouse_event_at(0, x, bottom, true);
    CHECK(!test_terminal_find(&term, "scroll-000.cfg", NULL, NULL));
    int thumb; CHECK(test_terminal_find_scrollbar(&term, true, &x, &thumb));
    mouse_event_at(0, x, thumb, false); mouse_event_at(32, x + 3, bottom, false); mouse_event_at(0, x + 3, bottom, true);
    test_terminal_expect(&term, "selected-m16.cfg");
    CHECK(test_terminal_find_scrollbar(&term, true, &x, &thumb));
    mouse_event_at(0, x, thumb, false); mouse_event_at(32, x, top, false); mouse_event_at(0, x, top, true);
    test_terminal_expect(&term, "scroll-000.cfg");
    mouse_event_at(65, x - 2, top + 1, false); mouse_event_at(64, x - 2, top + 1, false);
    /* The first row stays selected through redraw; page movement reveals later scripts. */
    test_terminal_send(&term, "\033[6~\033[6~\033[6~\033[6~");
    test_terminal_expect(&term, "scroll-059.cfg");
    /* Enter activates the original first script, not the newly visible page. */
    test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Configure session");
    test_terminal_expect(&term, "scroll-000.cfg");
    test_terminal_click(&term, "Browse boards"); test_terminal_expect(&term, "Installed OpenOCD");
    test_terminal_send(&term, "\033[5~"); test_terminal_resize(&term, 20, 80);
    test_terminal_expect(&term, "Installed OpenOCD");
    /* Reverse Tab from a list row eventually reaches Back. Enter must not be stolen by redraw. */
    test_terminal_click(&term, "[ Filter ]");
    test_terminal_send(&term, "\t\r"); test_terminal_expect(&term, "Installed OpenOCD");
    test_terminal_click(&term, "[ Filter ]"); test_terminal_expect(&term, "Filter installed");
    test_terminal_send(&term, "\025selected-m16.cfg\r");
    CHECK(!test_terminal_find_scrollbar(&term, true, NULL, NULL)); test_terminal_expect(&term, "/board/selected-m16.cfg");
    test_terminal_send(&term, "\t\r"); test_terminal_expect(&term, "Filter installed");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "Installed OpenOCD");
    test_terminal_click(&term, "[ Back ]"); test_terminal_expect(&term, "Configure session");
    test_terminal_send(&term, "\021"); test_terminal_stop(&term, 0);
}
static void configure(const char *binary, const char *helper, const char *peer)
{
    start(&term, binary);
    test_terminal_resize(&term, 44, 160);
    test_terminal_click(&term, "Embedded target"); test_terminal_expect(&term, "Board script");
    test_terminal_click(&term, "Browse boards"); test_terminal_expect(&term, "Installed OpenOCD scripts");
    test_terminal_click(&term, "[ Filter ]"); test_terminal_expect(&term, "Filter installed scripts");
    test_terminal_send(&term, "\025selected-m16.cfg\r");
    test_terminal_expect(&term, "/board/selected-m16.cfg"); test_terminal_click(&term, "/board/selected-m16.cfg");
    test_terminal_expect(&term, "Configure session");
    /* Board mode: mode, board, symbols, GDB, endpoint, OpenOCD, ownership. */
    test_terminal_send(&term, "\t\t\025\t\025"); test_terminal_send(&term, peer);
    int fd = socket(AF_INET, SOCK_STREAM, 0); CHECK(fd >= 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(fd, (struct sockaddr *)&addr, sizeof addr) == 0); socklen_t n = sizeof addr;
    CHECK(getsockname(fd, (struct sockaddr *)&addr, &n) == 0); CHECK(close(fd) == 0);
    char endpoint[64]; (void)snprintf(endpoint, sizeof endpoint, "127.0.0.1:%u", ntohs(addr.sin_port));
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, endpoint);
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, helper);
    test_terminal_click(&term, "Review session"); test_terminal_expect(&term, "Review OpenOCD session");
    test_terminal_expect(&term, "Start owned OpenOCD"); test_terminal_expect(&term, "argv[0]");
    test_terminal_expect(&term, "selected-m16.cfg");
}
int main(int argc, char **argv)
{
    CHECK(argc == 4);
    char root[] = "/tmp/tb-m16ui-XXXXXX"; CHECK(mkdtemp(root));
    char dir[256], script[256]; (void)snprintf(dir, sizeof dir, "%s/board", root); CHECK(mkdir(dir, 0700) == 0);
    (void)snprintf(script, sizeof script, "%s/board/selected-m16.cfg", root);
    int fd = open(script, O_CREAT | O_WRONLY | O_EXCL, 0600); CHECK(fd >= 0 && close(fd) == 0);
    CHECK(setenv("OPENOCD_SCRIPTS", root, 1) == 0 && setenv("XDG_CONFIG_HOME", root, 1) == 0);
    CHECK(setenv("TB_TEST_CONFIG_HOME", root, 1) == 0);
    CHECK(setenv("TB_FAKE_MODE", "remote-openocd", 1) == 0 && setenv("TB_FAKE_OPENOCD", "ready", 1) == 0);
    for (unsigned i = 0; i < 60; ++i) {
        char path[256]; (void)snprintf(path, sizeof path, "%s/board/scroll-%03u.cfg", root, i);
        int created = open(path, O_CREAT | O_WRONLY | O_EXCL, 0600); CHECK(created >= 0 && close(created) == 0);
    }
    centered_menus(argv[1]);
    script_navigation(argv[1], root);
    for (unsigned i = 0; i < 60; ++i) {
        char path[256]; (void)snprintf(path, sizeof path, "%s/board/scroll-%03u.cfg", root, i); CHECK(unlink(path) == 0);
    }
    if (getenv("TB_OPENOCD_TEST_TMUX")) {
        CHECK(unlink(script) == 0 && rmdir(dir) == 0 && rmdir(root) == 0);
        puts("PASS centered Keybinds/scripts resize, equal heights, overflow scrollbar paging/dragging and Filter/Back through tmux");
        return 0;
    }
    configure(argv[1], argv[2], argv[3]);
    test_terminal_resize(&term, 20, 80); test_terminal_expect(&term, "Review OpenOCD session");
    test_terminal_click(&term, "Next argv"); test_terminal_click(&term, "Previous argv");
    test_terminal_resize(&term, 44, 160);
    test_terminal_click(&term, "Start session"); test_terminal_expect(&term, "LIVE OPENOCD");
    test_terminal_expect(&term, "LIVE | Stopped"); test_terminal_expect(&term, "Halt");
    test_terminal_lower(&term, 2, true); /* Retained command outcomes are in Console. */
    test_terminal_click(&term, "Reset"); test_terminal_expect(&term, "Reset and halt target?");
    test_terminal_click(&term, "Cancel"); test_terminal_expect(&term, "LIVE OPENOCD");
    test_terminal_click(&term, "Reset"); test_terminal_click(&term, "Reset and halt ]");
    test_terminal_expect(&term, "OpenOCD command completed");
    test_terminal_click(&term, "Halt"); test_terminal_expect(&term, "M16 halt requested");
    test_terminal_resize(&term, 28, 110); test_terminal_expect(&term, "LIVE OPENOCD");
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Target on disconnect: Leave halted");
    test_terminal_click(&term, "Target on disconnect"); test_terminal_expect(&term, "Target on disconnect: Resume");
    test_terminal_click(&term, "Close session"); test_terminal_stop(&term, 0);
    /* Signal cleanup during a server that never announces a listener. */
    CHECK(setenv("TB_FAKE_OPENOCD", "hang", 1) == 0);
    configure(argv[1], argv[2], argv[3]); test_terminal_click(&term, "Start session");
    test_terminal_expect(&term, "LIVE OPENOCD"); test_terminal_stop(&term, SIGTERM);
    CHECK(unlink(script) == 0 && rmdir(dir) == 0 && rmdir(root) == 0);
    puts("PASS M16 real TUI installed-script/filter/review, authored OpenOCD startup, reset confirmation, Halt, disconnect policy, resize and signal cleanup");
    return 0;
}
