#define _POSIX_C_SOURCE 200809L
#include "setup_capture.h"
#include "session.h"
#include "profiles.h"
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Explicit physical runner. Seeds only definitions in a private profile store;
 * actual review, restore, save, acknowledgment and reopen use the terminal UI. */
static TestTerminal terminal;
static bool active;
static char socket_name[80], tmux_file[] = "/tmp/tb-setup-board-tmux-XXXXXX";
static void cleanup(void)
{
    if (active && terminal.child > 1) {
        pid_t state = waitpid(terminal.child, NULL, WNOHANG);
        if (!state) { (void)kill(terminal.child, SIGTERM); while (waitpid(terminal.child, NULL, 0) < 0 && errno == EINTR) { } }
    }
    if (*socket_name) {
        pid_t child = fork();
        if (!child) { if (!freopen("/dev/null", "w", stderr)) { _exit(127); } execlp("tmux", "tmux", "-L", socket_name, "kill-server", (char *)NULL); _exit(127); }
        if (child > 0) { while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { } }
        (void)unlink(tmux_file);
    }
}
static void settle(void) { for (int i = 0; i < 12; ++i) { test_terminal_pump(&terminal, 5); } }
static void click(const char *label) { test_terminal_click(&terminal, label); settle(); }
static void launch(const char *binary)
{
    if (getenv("TB_BOARD_TMUX")) {
        (void)snprintf(socket_name, sizeof socket_name, "tb-setup-physical-%ld", (long)getpid());
        int fd = mkstemp(tmux_file); CHECK(fd >= 0); const char text[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
        CHECK(write(fd, text, sizeof text - 1) == (ssize_t)(sizeof text - 1) && close(fd) == 0 && unsetenv("TMUX") == 0);
        char *const args[] = {"tmux", "-L", socket_name, "-f", tmux_file, "new-session", "-s", "setup", (char *)binary, NULL};
        test_terminal_start_argv(&terminal, args, "xterm-256color", false);
    } else { char *const args[] = {(char *)binary, NULL}; test_terminal_start_argv(&terminal, args, "xterm-256color", false); }
    active = true; test_terminal_resize(&terminal, 55, 200); test_terminal_expect(&terminal, "[ Config ]");
}
static void review_resize(int rows, int cols)
{
    test_terminal_resize(&terminal, rows, cols); bool ready = false;
    for (int i = 0; i < 300; ++i) {
        int x = 0, y = 0;
        if (test_terminal_find(&terminal, "[ Start session ]", &x, &y) && (cols >= 100 ? x > cols / 5 && y > rows / 3 : x < 8 && y < rows)) { ready = true; break; }
        test_terminal_pump(&terminal, 10);
    }
    CHECK(ready); settle();
}
static void reopen(void)
{
    click("[ Config ]"); click("Profiles"); click("physical-f01-ui"); click("Review session"); click("[ Connect ]");
    test_terminal_expect(&terminal, "[x] Restore saved setup");
    review_resize(18, 48); test_terminal_expect(&terminal, "Restore saved setup"); test_terminal_expect(&terminal, "[ Start session ]");
    review_resize(55, 200); click("[ Start session ]"); test_terminal_expect(&terminal, "LIVE | Stopped");
    click("Setup"); click("Unresolved setup entries"); test_terminal_expect(&terminal, "Acknowledge failures");
    test_terminal_expect(&terminal, "missing-physical-source.c"); test_terminal_expect(&terminal, "enabled intent");
}
int main(int argc, char **argv)
{
    CHECK(argc == 8 && atexit(cleanup) == 0);
    char settings[] = "/tmp/tb-setup-board-ui-XXXXXX"; CHECK(mkdtemp(settings));
    CHECK(setenv("XDG_CONFIG_HOME", settings, 1) == 0 && setenv("TB_TEST_CONFIG_HOME", settings, 1) == 0);
    TbSession *s = tb_session_create(); CHECK(s); TbLayout layout; tb_layout_init(&layout); TbDemo workspace; tb_demo_init(&workspace);
    const char *expressions[TB_MAX_PANES]; for (int i = 0; i < TB_MAX_PANES; ++i) { expressions[i] = ""; }
    expressions[3] = argv[7]; TbSetupCapture *capture = tb_setup_capture_create(NULL); CHECK(capture);
    CHECK(tb_setup_capture_update(capture, &layout, &workspace, tb_session_code(s), tb_session_memory(s), tb_session_variables(s), tb_session_breakpoints(s), expressions));
    TbSetup *saved = malloc(sizeof *saved); CHECK(saved); *saved = *tb_setup_capture_current(capture);
    saved->watch_count = 1; saved->watches[0].id = saved->next_id++; saved->watches[0].format = TB_VARIABLE_HEX;
    (void)snprintf(saved->watches[0].expression, 256, "%s", argv[6]);
    saved->panes[0].source_count = 2; saved->panes[0].sources[0].id = saved->next_id++;
    /* A stable missing pin is sufficient to inspect identity retention in UI;
     * actual Source/PC decoding is checked by the physical backend runner. */
    saved->panes[0].sources[0] = (TbSetupSource){.id = saved->panes[0].sources[0].id, .file = "missing-physical-source.c", .full = "/missing-physical-source.c"};
    saved->panes[0].sources[1] = (TbSetupSource){.id = saved->next_id++, .file = "second-missing-physical.c", .full = "/second-missing-physical.c"};
    saved->panes[0].source_selected = saved->panes[0].sources[0].id;
    saved->break_count = 2; saved->breaks[0] = (TbSetupBreak){.id = saved->next_id++, .enabled = true, .hardware = true, .condition = "1", .ignore_set = true, .ignore = 2};
    (void)snprintf(saved->breaks[0].location, TB_SETUP_LOCATION, "%s", argv[5]);
    saved->breaks[1] = saved->breaks[0]; saved->breaks[1].id = saved->next_id++; (void)snprintf(saved->breaks[1].condition, 512, "(");
    CHECK(tb_setup_valid(saved)); tb_setup_capture_destroy(capture); CHECK(tb_session_destroy(s));
    TbProfiles *store = tb_profiles_create(); CHECK(store && tb_profiles_load(store)); TbProfile profile; tb_profile_defaults(&profile, 1); profile.start_server = false;
    (void)snprintf(profile.name, TB_PROFILE_NAME, "physical-f01-ui"); (void)snprintf(profile.gdb, TB_PROFILE_PATH, "%s", argv[2]);
    (void)snprintf(profile.endpoint, sizeof profile.endpoint, "%s", argv[3]); (void)snprintf(profile.executable, TB_PROFILE_PATH, "%s", argv[4]);
    char directory[TB_SETUP_PATH]; CHECK(getcwd(directory, sizeof directory));
    CHECK(tb_profiles_put(store, &profile, false) && tb_profiles_save_setup(store, profile.name, saved, &profile, directory));
    launch(argv[1]); reopen(); click("Acknowledge failures");
    CHECK(!test_terminal_find(&terminal, "Acknowledge failures", NULL, NULL)); click("[ Back ]");
    CHECK(!test_terminal_find(&terminal, "Save setup*", NULL, NULL)); click("Save setup"); test_terminal_expect(&terminal, "2 code breakpoints"); click("[ Save setup ]");
    test_terminal_expect(&terminal, "Setup saved. Connection profile unchanged.");
    CHECK(tb_profiles_load(store)); TbSetup *roundtrip = tb_profiles_setup(store, profile.name); CHECK(roundtrip && tb_setup_equal(roundtrip, saved)); free(roundtrip);
    click("[ Back ]"); test_terminal_lower(&terminal, 2, true); click("Continue"); test_terminal_expect(&terminal, "breakpoint-hit");
    click("Setup"); click("Unresolved setup entries"); CHECK(!test_terminal_find(&terminal, "Acknowledge failures", NULL, NULL));
    click("[ Back ]"); CHECK(!test_terminal_find(&terminal, "Save setup*", NULL, NULL)); click("[ Back ]");
    const int widths[] = {48, 80, 123, 124, 160};
    for (size_t i = 0; i < sizeof widths / sizeof widths[0]; ++i) {
        test_terminal_resize(&terminal, 44, widths[i]); test_terminal_expect(&terminal, "Setup"); test_terminal_expect(&terminal, "Session"); test_terminal_expect(&terminal, "Quit");
    }
    test_terminal_resize(&terminal, 55, 200);
    bool wide = false; for (int i = 0; i < 300; ++i) { int x, y; if (test_terminal_find(&terminal, "Setup", &x, &y) && y == 0 && x >= 176) { wide = true; break; } test_terminal_pump(&terminal, 10); }
    CHECK(wide); click("Session"); click("Close session"); test_terminal_expect(&terminal, "[ Config ]");
    reopen(); click("[ Back ]"); click("[ Back ]"); click("Session"); click("Close session"); test_terminal_expect(&terminal, "[ Config ]");
    test_terminal_send(&terminal, "\021"); test_terminal_stop(&terminal, 0); active = false;
    CHECK(tb_profiles_load(store) && tb_profiles_remove(store, profile.name)); char path[2048]; (void)snprintf(path, sizeof path, "%s", tb_profiles_path(store)); tb_profiles_destroy(store); free(saved);
    CHECK(unlink(path) == 0); (void)snprintf(path, sizeof path, "%s/the-bugger/.lock", settings); CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof path, "%s/the-bugger", settings); CHECK(rmdir(path) == 0 && rmdir(settings) == 0);
    puts("PASS F01 physical terminal profile reopen/default restoration review, failed-enabled acknowledgment, partial save fidelity, real hardware breakpoint hit, stable dirty state, narrow controls and repeated reopen/cleanup");
    return 0;
}
