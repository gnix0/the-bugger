#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "keybinds.h"
#include "preferences.h"
#include "theme.h"
#include "profiles.h"
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static TestTerminal term;
static bool active;
static const char *vim_stage = "not started";
static int selected;
static char tmux_socket[80], tmux_config[] = "/tmp/the-bugger-keybinds-tmux-XXXXXX";
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
static void cleanup(void)
{
    if (!active) { return; }
    fprintf(stderr, "Keybind test cleanup at Vim stage: %s\n", vim_stage);
    pid_t result = waitpid(term.child, NULL, WNOHANG);
    if (result == 0) { (void)kill(term.child, SIGTERM); while (waitpid(term.child, NULL, 0) < 0 && errno == EINTR) { } }
}
static void start(const char *binary, const char *type)
{
    char *const args[] = {(char *)binary, NULL};
    bool mono = strcmp(type, "mono") == 0;
    if (getenv("TB_KEYBINDS_TEST_TMUX")) {
        char *const tmux_args[] = {"tmux", "-L", tmux_socket, "-f", tmux_config,
                                  "new-session", "-s", "keybinds", (char *)binary, NULL};
        test_terminal_start_argv(&term, tmux_args, "xterm-256color", mono);
    } else { test_terminal_start_argv(&term, args, mono ? "xterm-256color" : type, mono); }
    active = true;
    test_terminal_expect(&term, "[ Config ]");
}
static void click(const char *label) { test_terminal_click(&term, label); }
static int headers(void)
{
    int count = 0;
    for (int y = 0; y < term.rows; ++y) {
        const char *at = term.cells[y];
        while ((at = strstr(at, "Action"))) { ++count; at += 6; }
    }
    return count;
}
static void resize_keys(int rows, int cols)
{
    test_terminal_resize(&term, rows, cols);
    for (int i = 0; i < 400 && (headers() != (cols >= 112 ? 2 : 1) ||
         !test_terminal_find(&term, "Personal keybinds", NULL, NULL)); ++i) { test_terminal_pump(&term, 5); }
    CHECK(headers() == (cols >= 112 ? 2 : 1));
    test_terminal_expect(&term, "Personal keybinds");
}
static void cell(int action, int *x, int *y)
{
    char label[25]; (void)snprintf(label, sizeof label, "%.24s", tb_keybind_action((size_t)action));
    int column; CHECK(test_terminal_find(&term, label, &column, y));
    for (int row = 0; row < *y; ++row) {
        if (strncmp(term.cells[row] + column, "Action", 6) != 0) { continue; }
        const char *key = strstr(term.cells[row] + column + 6, "Key");
        CHECK(key); *x = (int)(key - term.cells[row]); return;
    }
    CHECK(false);
}
static void settle(void)
{
    struct timespec now; CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    long long until = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 100;
    do {
        test_terminal_pump(&term, 5); CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    } while ((long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 < until);
}
static void mouse_at(int x, int y)
{
    char mouse[80];
    (void)snprintf(mouse, sizeof mouse, "\033[<0;%d;%dM", x + 1, y + 1); test_terminal_send(&term, mouse);
    settle();
    (void)snprintf(mouse, sizeof mouse, "\033[<0;%d;%dm", x + 1, y + 1); test_terminal_send(&term, mouse);
    settle();
}
static void capture(int action)
{
    int x, y; cell(action, &x, &y); mouse_at(x + 1, y);
    test_terminal_expect(&term, "Press a key | Esc cancels");
    for (int i = 0; i < 400 && (!term.inverse[y - 1][x] || term.cells[y][x] != ' '); ++i) { test_terminal_pump(&term, 5); }
    if (!term.inverse[y - 1][x] || term.cells[y][x] != ' ') {
        fprintf(stderr, "Capture border failure: action=%d x=%d y=%d\n", action, x, y);
        for (int row = 0; row < term.rows; ++row) { fprintf(stderr, "%s\n", term.cells[row]); }
        fprintf(stderr, "Raw terminal bytes:\n");
        CHECK(fwrite(term.output, 1, term.output_length, stderr) == term.output_length);
    }
    CHECK(term.cells[y][x] == ' ' && !term.inverse[y][x]);
    CHECK(term.inverse[y - 1][x]); /* Capture emphasis affects the border only. */
}
static void clean_key_borders(void)
{
    settle();
    for (int action = 0; action < TB_BIND_COUNT; ++action) {
        char label[25]; (void)snprintf(label, sizeof label, "%.24s", tb_keybind_action((size_t)action));
        if (!test_terminal_find(&term, label, NULL, NULL)) { continue; }
        int x, y; cell(action, &x, &y);
        /* Underlining box glyphs adds stray strokes on real terminals. */
        CHECK(!term.underline[y - 1][x] && !term.underline[y + 1][x]);
    }
}
static void scrollbar_layout(void)
{
    resize_keys(20, 160); clean_key_borders();
    int x, y; CHECK(test_terminal_find(&term, "#", &x, &y));
    int top = y, bottom = y;
    while (top > 0 && strchr("|#", term.cells[top - 1][x])) { --top; }
    while (bottom + 1 < term.rows && strchr("|#", term.cells[bottom + 1][x])) { ++bottom; }
    CHECK(bottom > top && bottom < term.rows - 3);
    mouse_at(x, bottom); test_terminal_expect(&term, "Rows 3-4/15");
    test_terminal_send(&term, "\033OH"); test_terminal_expect(&term, "Rows 1-2/15"); settle();
    CHECK(test_terminal_find(&term, "#", &x, &y));
    char event[80];
    (void)snprintf(event, sizeof event, "\033[<0;%d;%dM", x + 1, y + 1); test_terminal_send(&term, event); settle();
    (void)snprintf(event, sizeof event, "\033[<32;%d;%dM", x + 1, bottom + 1); test_terminal_send(&term, event); settle();
    (void)snprintf(event, sizeof event, "\033[<0;%d;%dm", x + 1, bottom + 1); test_terminal_send(&term, event);
    test_terminal_expect(&term, "Rows 14-15/15"); clean_key_borders();
    test_terminal_send(&term, "\033[<64;50;10M"); settle(); clean_key_borders();
    test_terminal_send(&term, "\033[<65;50;10M"); settle(); clean_key_borders();
    resize_keys(64, 160); CHECK(!test_terminal_find(&term, "#", NULL, NULL));
    test_terminal_send(&term, "\033OH"); settle();
    resize_keys(60, 140); selected = 0;
}

static void open_keys(void)
{
    click("[ Config ]"); click("Keybinds"); selected = 0;
    test_terminal_expect(&term, "Restore Defaults"); CHECK(headers() == (term.cols >= 112 ? 2 : 1));
}
static void choose(int action)
{
    /* Recover a row focus after toolbar actions without assuming control indices. */
    bool found = false;
    for (int i = 0; i < TB_BIND_COUNT; ++i) {
        char label[25]; (void)snprintf(label, sizeof label, "%.24s", tb_keybind_action((size_t)i));
        if (test_terminal_find(&term, label, NULL, NULL)) { capture(i); test_terminal_send(&term, "\033"); test_terminal_expect(&term, "Capture cancelled."); settle(); found = true; break; }
    }
    CHECK(found);
    test_terminal_send(&term, "\033OH");
    for (int i = 0; i < action; ++i) { test_terminal_send(&term, "\t"); }
    settle(); selected = action;
    int x, y; cell(action, &x, &y);
}
static void edit(const char *binding)
{
    capture(selected);
    int key; CHECK(tb_keybind_parse(binding, &key));
    if (!key) { test_terminal_send(&term, "\033[3~"); }
    else if (key < 0) { char text[2] = {(char)-key, 0}; test_terminal_send(&term, text); }
    else if (key >= 289) { char text[2] = {(char)(key - 256), 0}; test_terminal_send(&term, text); }
    else {
        static const int codes[] = {0, 0, 0, 0, 0, 15, 17, 18, 19, 20, 21, 23, 24};
        char text[32]; int function = key > 12 ? key - 12 : key;
        if (function <= 4) { (void)snprintf(text, sizeof text, key > 12 ? "\033[1;2%c" : "\033O%c", 'P' + function - 1); }
        else { (void)snprintf(text, sizeof text, key > 12 ? "\033[%d;2~" : "\033[%d~", codes[function]); }
        test_terminal_send(&term, text);
    }
}
static void bind_action(int action, const char *binding)
{
    choose(action); edit(binding); test_terminal_expect(&term, "Binding captured.");
}
static void inactive(const char *label)
{
    char before[sizeof term.cells]; memcpy(before, term.cells, sizeof before);
    click(label); CHECK(memcmp(before, term.cells, sizeof before) == 0);
}
static void stop(void) { test_terminal_stop(&term, 0); active = false; }

static void native_bindings(const char *binary, const char *fixture)
{
    TbProfiles *store = tb_profiles_create(); TbProfile *profile = malloc(sizeof *profile); CHECK(store && profile);
    CHECK(tb_profiles_load(store)); tb_profile_defaults(profile, 0);
    (void)snprintf(profile->name, sizeof profile->name, "Shared connection");
    (void)snprintf(profile->executable, sizeof profile->executable, "%s", fixture);
    CHECK(tb_profiles_put(store, profile, false)); free(profile); tb_profiles_destroy(store);
    start(binary, "xterm-256color");
    click("[ Config ]"); click("Profiles"); click("Shared connection");
    click("Review session"); click("Start native");
    test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_expect(&term, "Continue F6"); test_terminal_expect(&term, "Pause p");
    test_terminal_lower(&term, 2, true);
    click("Command"); test_terminal_send(&term, "echo wasdbnp\\n\r");
    test_terminal_expect(&term, "[gdb] wasdbnp");
    test_terminal_send(&term, "start\r"); test_terminal_expect(&term, "LIVE | Stopped");
    click("Source");
    test_terminal_send(&term, "\033[17~"); test_terminal_expect(&term, "LIVE | Running");
    test_terminal_expect(&term, "FIXTURE PID=");
    test_terminal_send(&term, "p"); test_terminal_expect(&term, "LIVE | Stopped");
    int x, y; CHECK(test_terminal_find(&term, "Source", &x, &y));
    char mouse[64]; (void)snprintf(mouse, sizeof mouse, "\033[<0;%d;%dM", x + 12, y + 4);
    test_terminal_send(&term, mouse); test_terminal_expect(&term, "CONTENT FOCUS");
    (void)snprintf(mouse, sizeof mouse, "\033[<0;%d;%dm", x + 12, y + 4); test_terminal_send(&term, mouse);
    test_terminal_send(&term, "\006"); test_terminal_expect(&term, "instruction stepping");
    test_terminal_send(&term, "\010"); test_terminal_expect(&term, "source stepping");
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Close native session?");
    click("Close session"); stop();
    puts("PASS global shortcuts after shared profile load, real Continue/Pause, content-pane transfer and protected Console input");
}

static void leave_vim_field(void)
{
    test_terminal_send(&term, "\033");
    for (int i = 0; i < 800 && (test_terminal_find(&term, "VIM NORMAL", NULL, NULL) ||
         test_terminal_find(&term, "VIM INSERT", NULL, NULL)); ++i) { test_terminal_pump(&term, 5); }
    CHECK(!test_terminal_find(&term, "VIM NORMAL", NULL, NULL));
    CHECK(!test_terminal_find(&term, "VIM INSERT", NULL, NULL));
    test_terminal_expect(&term, "LIVE | Stopped");
}

static void vim_input(const char *binary, const char *fixture)
{
    start(binary, "xterm-256color"); open_keys();
    test_terminal_expect(&term, "Vim-like input   [ Off ]");
    click("Vim-like input"); test_terminal_expect(&term, "Vim-like input   [ On ]");
    click("[ Undo ]"); test_terminal_expect(&term, "Vim-like input   [ Off ]");
    inactive("[ Save ]");
    click("Vim-like input"); click("[ Back ]"); test_terminal_expect(&term, "Unsaved keybind changes");
    click("[ Cancel ]"); click("[ Save ]"); test_terminal_expect(&term, "Keybinds saved.");
    click("[ Back ]"); test_terminal_send(&term, "\r\t");
    test_terminal_expect(&term, "VIM NORMAL");
    test_terminal_send(&term, "i\025alpha beta"); test_terminal_expect(&term, "VIM INSERT");
    test_terminal_expect(&term, "alpha beta");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "VIM NORMAL");
    test_terminal_send(&term, "0wxiX"); test_terminal_expect(&term, "alpha Xeta");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "VIM NORMAL");
    test_terminal_send(&term, "Aqhjkl"); test_terminal_expect(&term, "alpha Xetaqhjkl");
    test_terminal_send(&term, "\t"); test_terminal_expect(&term, "VIM NORMAL");
    test_terminal_send(&term, "i\025literal"); test_terminal_expect(&term, "literal");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "VIM NORMAL");
    test_terminal_resize(&term, 18, 48); test_terminal_expect(&term, "VIM NORMAL");
    test_terminal_resize(&term, 54, 166); test_terminal_expect(&term, "VIM NORMAL");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "[ Config ]");
    stop();
    if (fixture) {
        start(binary, "mono"); click("[ Config ]"); click("Profiles"); click("Shared connection");
        click("Review session"); click("Start native"); test_terminal_expect(&term, "LIVE | Loaded");
        test_terminal_lower(&term, 2, true);
        vim_stage = "native Command focus";
        click("Command"); test_terminal_expect(&term, "VIM NORMAL");
        test_terminal_send(&term, "iecho modal_hjklq[]123\\n"); test_terminal_expect(&term, "VIM INSERT");
        test_terminal_send(&term, "\033"); test_terminal_expect(&term, "VIM NORMAL");
        test_terminal_send(&term, "\r"); test_terminal_expect(&term, "[gdb] modal_hjklq[]123");
        test_terminal_expect(&term, "VIM NORMAL");
        vim_stage = "native start command";
        test_terminal_send(&term, "istart\r"); test_terminal_expect(&term, "VIM INSERT");
        test_terminal_expect(&term, "LIVE | Stopped");
        test_terminal_send(&term, "\033"); test_terminal_expect(&term, "VIM NORMAL");
        leave_vim_field();
        test_terminal_lower(&term, 0, true);
        int x, y; CHECK(test_terminal_find(&term, "Address", &x, &y));
        vim_stage = "Memory focus";
        mouse_at(x + 10, y); test_terminal_expect(&term, "VIM NORMAL");
        vim_stage = "Memory Insert";
        test_terminal_send(&term, "i\025&fixture_counter"); test_terminal_expect(&term, "VIM INSERT");
        vim_stage = "Memory Escape to Normal";
        /* A mode change itself never dispatches the draft memory read. */
        test_terminal_send(&term, "\033"); test_terminal_expect(&term, "VIM NORMAL");
        leave_vim_field();
        vim_stage = "Save Log focus";
        click("Save Log"); test_terminal_expect(&term, "VIM NORMAL");
        test_terminal_send(&term, "i\025/tmp/not-written-vim-test"); test_terminal_expect(&term, "VIM INSERT");
        test_terminal_send(&term, "\033"); test_terminal_expect(&term, "VIM NORMAL");
        test_terminal_send(&term, "\033"); test_terminal_expect(&term, "LIVE | Stopped");
        test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Close native session?");
        click("Close session"); stop();
    }
    puts("PASS opt-in Vim toggle/undo/save/reload, Normal entry, literal Insert, editing, focus/resize, inline fields and Escape/submit cleanup");
}

int main(int argc, char **argv)
{
    CHECK((argc == 2 || argc == 3) && atexit(cleanup) == 0);
    if (getenv("TB_KEYBINDS_TEST_TMUX")) {
        (void)snprintf(tmux_socket, sizeof tmux_socket, "the-bugger-keybinds-%ld", (long)getpid());
        int fd = mkstemp(tmux_config); CHECK(fd >= 0);
        const char config[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
        CHECK(write(fd, config, sizeof config - 1) == (ssize_t)(sizeof config - 1)); CHECK(close(fd) == 0);
        CHECK(unsetenv("TMUX") == 0 && atexit(tmux_cleanup) == 0);
    }
    char root[] = "/tmp/the-bugger-keybinds-tui-XXXXXX"; CHECK(mkdtemp(root));
    CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    CHECK(setenv("TB_TEST_CONFIG_HOME", root, 1) == 0);
    start(argv[1], "xterm-256color"); test_terminal_resize(&term, 60, 140); open_keys();
    scrollbar_layout();
    int x, y; CHECK(test_terminal_find(&term, "Keybinds", &x, &y) && x > 0 && y > 0);
    inactive("[ Undo ]"); inactive("[ Save ]"); inactive("[ Restore Defaults ]");
    TbPreferences prefs; CHECK(tb_preferences_load(&prefs) && !prefs.existed);
    edit("F11"); test_terminal_expect(&term, "Already assigned to Step into");
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Key unavailable or reserved");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "Capture cancelled.");
    inactive("[ Undo ]"); inactive("[ Save ]");
    capture(TB_BIND_CONTINUE);
    resize_keys(60, 111); CHECK(headers() == 1);
    cell(TB_BIND_CONTINUE, &x, &y); CHECK(term.cells[y][x] == ' ');
    resize_keys(60, 112); CHECK(headers() == 2);
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "Capture cancelled.");
    test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Press a key");
    test_terminal_send(&term, "\033[17~"); test_terminal_expect(&term, "Binding captured.");
    CHECK(tb_preferences_load(&prefs) && !prefs.existed);
    click("[ Undo ]"); test_terminal_expect(&term, "Last change undone");
    inactive("[ Undo ]"); inactive("[ Save ]"); inactive("[ Restore Defaults ]");
    bind_action(TB_BIND_CONTINUE, "F6");
    click("[ Back ]"); test_terminal_expect(&term, "Unsaved keybind changes");
    click("[ Cancel ]"); test_terminal_expect(&term, "F6");
    test_terminal_send(&term, "q"); test_terminal_expect(&term, "Unsaved keybind changes");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "F6");
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Discard and quit");
    click("[ Cancel ]"); click("[ Back ]"); click("[ Discard changes ]");
    test_terminal_expect(&term, "Configuration");
    CHECK(tb_preferences_load(&prefs) && !prefs.existed && prefs.keys[TB_BIND_CONTINUE] == 5);
    click("[ Back ]"); open_keys();
    bind_action(TB_BIND_CONTINUE, "F6"); bind_action(TB_BIND_PAUSE, "p");
    edit("None"); test_terminal_expect(&term, "Binding captured.");
    click("[ Undo ]"); test_terminal_expect(&term, "Last change undone");
    bind_action(TB_BIND_LEFT, "a"); bind_action(TB_BIND_DOWN, "s");
    bind_action(TB_BIND_UP, "w"); bind_action(TB_BIND_RIGHT, "d");
    bind_action(TB_BIND_CONTENT_RIGHT, "Ctrl+F");
    bind_action(TB_BIND_CONTENT_LEFT, "Ctrl+H");
    bind_action(TB_BIND_NEXT_PANE, "n"); bind_action(TB_BIND_PANE_1 + 2, "r");
    bind_action(TB_BIND_BACK, "b"); bind_action(TB_BIND_NEXT_CONTROL, "Ctrl+N");
    click("[ Save ]"); test_terminal_expect(&term, "Keybinds saved."); stop();
    CHECK(tb_preferences_load(&prefs) && prefs.keys[TB_BIND_CONTINUE] == 6 && prefs.keys[TB_BIND_UP] == 256 + 'w');
    CHECK(prefs.keys[TB_BIND_PAUSE] == 256 + 'p' && prefs.keys[TB_BIND_LEFT] == 256 + 'a');
    CHECK(prefs.keys[TB_BIND_DOWN] == 256 + 's' && prefs.keys[TB_BIND_RIGHT] == 256 + 'd');
    CHECK(prefs.keys[TB_BIND_CONTENT_RIGHT] == -6 && prefs.keys[TB_BIND_NEXT_PANE] == 256 + 'n');
    CHECK(prefs.keys[TB_BIND_PANE_1 + 2] == 256 + 'r' && prefs.keys[TB_BIND_BACK] == 256 + 'b');
    CHECK(prefs.keys[TB_BIND_NEXT_CONTROL] == -14);
    if (argc == 3) { native_bindings(argv[1], argv[2]); }
    /* Saving a theme cannot reset personal bindings. */
    CHECK(tb_preferences_save(&prefs, tb_theme_find("rose-pine", "dawn")));
    start(argv[1], "mono"); test_terminal_resize(&term, 60, 140);
    for (int i = 0; i < (argc == 3 ? 4 : 3); ++i) { test_terminal_send(&term, "\016"); }
    test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Configuration");
    click("Keybinds"); selected = 0; test_terminal_expect(&term, "F6");
    inactive("[ Undo ]"); inactive("[ Save ]");
    click("[ Restore Defaults ]"); test_terminal_expect(&term, "Restore default keybinds?");
    click("[ Cancel ]"); test_terminal_expect(&term, "F6");
    click("[ Restore Defaults ]"); click("[ Restore Defaults ]");
    test_terminal_expect(&term, "Defaults restored in draft");
    inactive("[ Restore Defaults ]");
    click("[ Undo ]"); test_terminal_expect(&term, "F6"); inactive("[ Save ]");
    resize_keys(60, 111); CHECK(headers() == 1);
    test_terminal_send(&term, "\033[<65;10;10M"); settle();
    test_terminal_expect(&term, "Restore Defaults");
    choose(TB_BIND_COUNT - 1); test_terminal_send(&term, "\033[6~");
    bind_action(TB_BIND_COUNT - 1, "F8");
    resize_keys(18, 48); CHECK(headers() == 1);
    test_terminal_expect(&term, "Restore Defaults");
    choose(TB_BIND_CONTINUE); edit("F7"); test_terminal_expect(&term, "Binding captured.");
    resize_keys(60, 112); CHECK(headers() == 2);
    click("[ Back ]"); test_terminal_expect(&term, "Unsaved keybind changes");
    click("[ Discard changes ]"); click("[ Back ]");
    /* Remapped aliases navigate menus, but remain literal in text fields. */
    test_terminal_send(&term, "s\r"); test_terminal_expect(&term, "Embedded");
    test_terminal_send(&term, "\t\025wasdbn"); test_terminal_expect(&term, "wasdbn");
    test_terminal_send(&term, "\033"); test_terminal_expect(&term, "[ Config ]");
    test_terminal_resize(&term, 60, 140); open_keys();
    click("[ Restore Defaults ]"); click("[ Restore Defaults ]");
    click("[ Save ]"); test_terminal_expect(&term, "Keybinds saved."); stop();
    CHECK(tb_preferences_load(&prefs) && prefs.keys[TB_BIND_CONTINUE] == 5 && prefs.keys[TB_BIND_UP] == 0);
    CHECK(prefs.palette == tb_theme_find("rose-pine", "dawn"));
    vim_input(argv[1], argc == 3 ? argv[2] : NULL);
    CHECK(tb_preferences_load(&prefs) && prefs.vim);
    char path[4096]; CHECK(unlink(prefs.path) == 0);
    if (argc == 3) {
        (void)snprintf(path, sizeof path, "%s/profiles.json", prefs.directory); CHECK(unlink(path) == 0);
        (void)snprintf(path, sizeof path, "%s/.lock", prefs.directory); CHECK(unlink(path) == 0);
    }
    (void)snprintf(path, sizeof path, "%s/.preferences.lock", prefs.directory); CHECK(unlink(path) == 0);
    CHECK(rmdir(prefs.directory) == 0 && rmdir(root) == 0);
    puts("PASS responsive Keybinds tables, direct capture, border-only emphasis, conflicts, inactive no-ops, undo, confirmations, save/reload, navigation and literal fields");
    if (getenv("TB_KEYBINDS_TEST_TMUX")) { puts("PASS global keybind editing and dispatch through an isolated real tmux server"); }
    return EXIT_SUCCESS;
}
