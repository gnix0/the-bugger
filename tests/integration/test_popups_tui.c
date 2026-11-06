#include "../support/terminal.h"
#include "../support/check.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static TestTerminal term;
static char socket_name[80];
static char tmux_config[] = "/tmp/the-bugger-popups-XXXXXX";


static void cleanup(void)
{
    if (*socket_name) {
        pid_t child = fork();
        if (child == 0) {
            execlp("tmux", "tmux", "-L", socket_name, "kill-server", (char *)NULL);
            _exit(127);
        }
        if (child > 0) {
            while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { }
        }
        (void)unlink(tmux_config);
    }
}


static void settle(void)
{
    for (int i = 0; i < 20; ++i)
        test_terminal_pump(&term, 10);
}


static void absent(const char *label)
{
    for (int i = 0; i < 100 && test_terminal_find(&term, label, NULL, NULL); ++i)
        test_terminal_pump(&term, 20);
    CHECK(!test_terminal_find(&term, label, NULL, NULL));
}


static void mouse(int button, int x, int y)
{
    char input[80];
    (void)snprintf(input, sizeof(input), "\033[<%d;%d;%dM", button, x + 1, y + 1);
    test_terminal_send(&term, input);
    settle();
}


static void click_at(int x, int y)
{
    mouse(0, x, y);
    char input[80];
    (void)snprintf(input, sizeof(input), "\033[<0;%d;%dm", x + 1, y + 1);
    test_terminal_send(&term, input);
    settle();
}


static void action_menu(void)
{
    test_terminal_expect(&term, "Actions");
    int pane_x, pane_y;
    CHECK(test_terminal_find(&term, "Disassembly", &pane_x, &pane_y));
    for (int row = pane_y + 1; row < term.rows - 2; ++row) {
        const char *label = strstr(term.cells[row] + pane_x, "Actions");
        if (!label)
            continue;
        int x = (int)(label - term.cells[row]);
        click_at(x, row);
        test_terminal_expect(&term, "Code view actions");
        int menu_x, menu_y;
        CHECK(test_terminal_find(&term, "Code view actions", &menu_x, &menu_y));
        CHECK(menu_y == row + 1 || menu_y < row); /* Fit below the trigger or open upward. */
        CHECK(menu_x - 2 <= x && menu_x > 0);
        CHECK(test_terminal_find(&term, "Continue", NULL, NULL));
        return;
    }
    for (int row = 0; row < term.rows; ++row)
        fprintf(stderr, "%s\n", term.cells[row]);
    CHECK(false);
}


static void column_pairs(void)
{
    for (int i = 0; i < 3; ++i) { test_terminal_lower(&term, i, true); }
    const char *const upper[] = {"Source", "Disassembly", "Registers"};
    const char *const lower[] = {"Memory", "Locals", "Console"};
    int lower_x[3], lower_y[3];
    for (int i = 0; i < 3; ++i) {
        CHECK(test_terminal_find(&term, lower[i], &lower_x[i], &lower_y[i]));
    }
    mouse(0, lower_x[0] + 2, lower_y[0] - 1);
    mouse(32, lower_x[0] + 2, lower_y[0] - 4);
    char release[80];
    (void)snprintf(release, sizeof(release), "\033[<0;%d;%dm", lower_x[0] + 3, lower_y[0] - 3);
    test_terminal_send(&term, release); settle();
    int x, y;
    CHECK(test_terminal_find(&term, "Memory", &x, &y) && y == lower_y[0] - 3);
    CHECK(test_terminal_find(&term, "Locals", &x, &y) && y == lower_y[1]);
    CHECK(test_terminal_find(&term, "Console", &x, &y) && y == lower_y[2]);
    mouse(0, lower_x[1] + 2, lower_y[1] - 1);
    mouse(32, lower_x[1] + 2, lower_y[1] + 1);
    (void)snprintf(release, sizeof(release), "\033[<0;%d;%dm", lower_x[1] + 3, lower_y[1] + 2);
    test_terminal_send(&term, release); settle();
    for (int i = 0; i < 3; ++i) {
        int ux, uy, lx, ly;
        CHECK(test_terminal_find(&term, upper[i], &ux, &uy));
        CHECK(test_terminal_find(&term, lower[i], &lx, &ly));
        click_at(ux + 2, uy + 4);
        test_terminal_expect(&term, "CONTENT FOCUS");
        test_terminal_send(&term, "\033[1;5B"); settle();
        CHECK(term.cells[ly][lx - 2] == '*');
        test_terminal_send(&term, "\033[1;5A"); settle();
        CHECK(term.cells[uy][ux - 2] == '*');
        /* Mouse focus can return to either visible body without closing its partner. */
        click_at(lx + 2, ly + (i == 0 ? 4 : 3));
        CHECK(term.cells[ly][lx - 2] == '*');
        click_at(ux + 2, uy + 4);
        CHECK(term.cells[uy][ux - 2] == '*');
    }
}


static void horizontal_colors(void)
{
    int x, y, right;
    CHECK(test_terminal_find(&term, "Disassembly", &x, &y));
    CHECK(test_terminal_find(&term, "Registers", &right, NULL));
    int row = y + 2;
    const char *address = strstr(term.cells[row] + x, "0x");
    CHECK(address && address - term.cells[row] < right);
    int column = (int)(address - term.cells[row]);
    char original[19]; memcpy(original, address, 18); original[18] = 0;
    click_at(column + 1, row);
    int foreground = term.foregrounds[row][column + 4];
    test_terminal_send(&term, "\033OC"); settle();
    CHECK(strncmp(term.cells[row] + column, original + 4, 14) == 0);
    CHECK(term.foregrounds[row][column] == foreground);
    test_terminal_send(&term, "\033OD"); settle();
    CHECK(strncmp(term.cells[row] + column, original, 18) == 0);
}


int main(int argc, char **argv)
{
    CHECK(argc == 3);
    CHECK(atexit(cleanup) == 0);
    if (getenv("TB_POPUPS_TMUX")) {
        (void)snprintf(socket_name, sizeof(socket_name), "the-bugger-popups-%ld", (long)getpid());
        int fd = mkstemp(tmux_config);
        CHECK(fd >= 0);
        const char config[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
        CHECK(write(fd, config, sizeof(config) - 1) == (ssize_t)(sizeof(config) - 1));
        CHECK(close(fd) == 0 && unsetenv("TMUX") == 0);
        char *const args[] = {"tmux", "-L", socket_name, "-f", tmux_config,
                              "new-session", "-s", "popups", argv[1], NULL};
        test_terminal_start_argv(&term, args, "xterm-256color", false);
    } else {
        char *const args[] = {argv[1], NULL};
        test_terminal_start_argv(&term, args, "xterm-256color", false);
    }
    test_terminal_resize(&term, 44, 160);
    test_terminal_expect(&term, "Native Linux");
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025");
    test_terminal_send(&term, argv[2]);
    test_terminal_click(&term, "Review session");
    test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_lower(&term, 2, true);
    test_terminal_click(&term, "Command");
    test_terminal_send(&term, "start\r");
    test_terminal_expect(&term, "LIVE | assembly: selected frame");
    column_pairs();
    horizontal_colors();
    settle();
    int source_x, source_y;
    CHECK(test_terminal_find(&term, "Source", &source_x, &source_y));
    unsigned clears = term.full_clears;
    action_menu();
    /* Mouse dismissal consumes the click; it must not start Continue underneath. */
    test_terminal_click(&term, "Continue");
    absent("Code view actions");
    CHECK(!test_terminal_find(&term, "LIVE | Running", NULL, NULL));
    test_terminal_send(&term, "\r"); /* Focus returns to the original Actions trigger. */
    test_terminal_expect(&term, "Code view actions");
    test_terminal_menu_click(&term, "Code view actions", "Go to address / symbol");
    test_terminal_expect(&term, "Go to address / symbol");
    absent("Code view actions");
    CHECK(test_terminal_find(&term, "Continue", NULL, NULL));
    CHECK(test_terminal_find(&term, "Source", NULL, NULL));
    test_terminal_send(&term, "main");
    settle();
    CHECK(term.full_clears == clears); /* Popup transitions never clear the physical screen. */
    test_terminal_resize(&term, 32, 120);
    settle();
    test_terminal_expect(&term, "main");
    test_terminal_send(&term, "\033");
    settle();
    absent("Go to address / symbol");
    test_terminal_resize(&term, 44, 160);
    settle();
    int restored_x, restored_y;
    CHECK(test_terminal_find(&term, "Source", &restored_x, &restored_y));
    CHECK(source_x == restored_x && source_y == restored_y);
    clears = term.full_clears;
    test_terminal_click(&term, "Mixed");
    test_terminal_expect(&term, "Mixed+");
    action_menu();
    CHECK(!test_terminal_find(&term, "Toggle mixed", NULL, NULL));
    test_terminal_send(&term, "\033");
    absent("Code view actions");
    CHECK(term.full_clears == clears);
    action_menu();
    test_terminal_send(&term, "\033[15~"); /* Continue remains available inside a contextual menu. */
    test_terminal_expect(&term, "Running");
    test_terminal_expect(&term, "Code view actions");
    test_terminal_send(&term, "\033[17~"); /* Pause must also keep processing GDB events. */
    test_terminal_expect(&term, "Stopped");
    test_terminal_expect(&term, "Code view actions");
    test_terminal_send(&term, "\033");
    absent("Code view actions");
    for (int i = 0; i < 3; ++i) { test_terminal_lower(&term, i, false); }
    /* A lower dock's selector anchors to its own bottom-row control and scrolls. */
    int dock_x = -1, dock_y = term.rows - 4;
    for (int x = term.cols - 6; x >= 0; --x) {
        if (strncmp(term.cells[dock_y] + x, "...", 3) == 0) {
            dock_x = x;
            break;
        }
    }
    CHECK(dock_x >= 0);
    click_at(dock_x, dock_y);
    test_terminal_expect(&term, "SHOW VIEW");
    int list_x, list_y;
    CHECK(test_terminal_find(&term, "SHOW VIEW", &list_x, &list_y));
    CHECK(list_y < dock_y);
    const char *included = strstr(term.cells[list_y + 6] + list_x - 1, "Console");
    CHECK(included && strchr(included, '*'));
    CHECK(strstr(term.cells[list_y + 1] + list_x - 1, "Source"));
    CHECK(!strchr(term.cells[list_y + 1] + list_x - 1, '*'));
    test_terminal_send(&term, "\033OF");
    settle();
    test_terminal_expect(&term, "Pane actions >");
    CHECK(!test_terminal_find(&term, "Stack [not implemented]", NULL, NULL));
    test_terminal_resize(&term, 20, 80);
    settle();
    test_terminal_expect(&term, "SHOW VIEW");
    test_terminal_send(&term, "\033");
    settle();
    absent("SHOW VIEW");
    test_terminal_send(&term, "\021");
    test_terminal_expect(&term, "Terminate the launched program");
    CHECK(test_terminal_find(&term, "Continue", NULL, NULL));
    test_terminal_click(&term, "Close session");
    test_terminal_stop(&term, 0);
    puts("PASS anchored Actions/dock selectors, scrolling, workspace dialogs, resize, toggles, dismissal and focus");
    return 0;
}
