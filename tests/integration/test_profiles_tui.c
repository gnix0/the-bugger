#define _POSIX_C_SOURCE 200809L
#include "profiles.h"
#include "keybinds.h"
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static TestTerminal term;
static bool active;
static char tmux_socket[80], tmux_config[] = "/tmp/the-bugger-setup-tmux-XXXXXX";
static void tmux_cleanup(void)
{
    pid_t child = fork();
    if (child == 0) {
        if (!freopen("/dev/null", "w", stderr)) { _exit(127); }
        execlp("tmux", "tmux", "-L", tmux_socket, "kill-server", (char *)NULL); _exit(127);
    }
    if (child > 0) { while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { } }
    (void)unlink(tmux_config);
}
static void cleanup(void)
{
    if (!active || term.child <= 1) { return; }
    pid_t state;
    do { state = waitpid(term.child, NULL, WNOHANG); } while (state < 0 && errno == EINTR);
    if (state == 0) { (void)kill(term.child, SIGTERM); while (waitpid(term.child, NULL, 0) < 0 && errno == EINTR) { } }
}
static void settle(void)
{
    struct timespec ts; CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    long long end = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + 60;
    do { test_terminal_pump(&term, 5); CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0); }
    while ((long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 < end);
}
static void click(const char *text) { test_terminal_click(&term, text); settle(); }
static void back(void)
{
    char header[TEST_COLS + 1]; memcpy(header, term.cells[0], sizeof header);
    test_terminal_send(&term, "\033");
    /* Wait for a deferred multiplexer Escape before sending another
     * escape-prefixed mouse sequence. */
    for (int i = 0; i < 200 && memcmp(header, term.cells[0], sizeof header) == 0; ++i) { test_terminal_pump(&term, 10); }
    CHECK(memcmp(header, term.cells[0], sizeof header) != 0); settle();
}
static void input(const char *text)
{
    test_terminal_expect(&term, "Profile field"); test_terminal_send(&term, "\025");
    test_terminal_send(&term, text); test_terminal_send(&term, "\r"); settle();
}
static void start(const char *binary)
{
    char *const arguments[] = {(char *)binary, NULL};
    if (getenv("TB_PROFILE_SETUP_TMUX")) {
        char *const wrapped[] = {"tmux", "-L", tmux_socket, "-f", tmux_config, "new-session", "-s", "profiles", (char *)binary, NULL};
        test_terminal_start_argv(&term, wrapped, "xterm-256color", false);
    } else { test_terminal_start_argv(&term, arguments, "xterm-256color", false); }
    active = true;
    test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "[ Config ]");
    settle();
}
static void stop(void) { test_terminal_stop(&term, 0); active = false; }
static void save(const char *name, bool replace)
{
    click("Save profile"); input(name);
    if (replace) { test_terminal_expect(&term, "Replace profile:"); click("[ Confirm ]"); }
    test_terminal_expect(&term, "Profile file saved atomically.");
}
static void setup_review(void)
{
    click("Setup");
    for (int i = 0; i < 50; ++i) {
        click("Save setup"); if (test_terminal_find(&term, "Save setup review", NULL, NULL)) { return; }
    }
    CHECK(false);
}
static void restored_setup(const char *binary, TbProfiles *store)
{
    TbSetup *saved = tb_profiles_setup(store, "saved copy"); CHECK(saved && saved->panes[0].source_count == 1);
    saved->panes[0].source_count = 2;
    TbSetupSource *missing = &saved->panes[0].sources[1]; missing->id = saved->next_id++;
    (void)snprintf(missing->file, sizeof missing->file, "missing-saved-source.c");
    (void)snprintf(missing->full, sizeof missing->full, "/missing-saved-source.c");
    saved->panes[0].source_selected = missing->id;
    saved->panes[3].memory_bytes = 96; saved->panes[3].memory_group = 4;
    (void)snprintf(saved->panes[3].memory_expression, 256, "&saved_memory");
    saved->watch_count = 2;
    saved->watches[0] = (TbSetupWatch){.id = saved->next_id++, .expression = "saved_watch", .format = 1};
    saved->watches[1] = (TbSetupWatch){.id = saved->next_id++, .expression = "unsafe_call()", .format = 2};
    saved->break_count = 1;
    saved->breaks[0] = (TbSetupBreak){.id = saved->next_id++, .location = "main", .condition = "1", .enabled = true, .ignore_set = true, .ignore = 2, .hardware = true};
    char directory[TB_SETUP_PATH]; CHECK(getcwd(directory, sizeof directory));
    CHECK(tb_profiles_save_setup(store, "saved copy", saved, tb_profiles_find(store, "saved copy"), directory));
    json_object *root = json_object_from_file(tb_profiles_path(store)); CHECK(root);
    json_object *setups = NULL; CHECK(json_object_object_get_ex(root, "setups", &setups));
    for (size_t i = 0; i < json_object_array_length(setups); ++i) {
        json_object *record = json_object_array_get_idx(setups, i), *name = NULL; CHECK(json_object_object_get_ex(record, "profile", &name));
        if (strcmp(json_object_get_string(name), "saved copy") == 0) {
            CHECK(json_object_object_add(record, "elf_metadata", json_object_new_string("previous-local-file")) == 0);
        }
    }
    CHECK(json_object_to_file_ext(tb_profiles_path(store), root, JSON_C_TO_STRING_PRETTY) == 0); json_object_put(root);
    CHECK(tb_profiles_load(store));
    start(binary); click("[ Config ]"); click("Profiles"); click("saved copy"); click("Review session"); click("Start native");
    test_terminal_expect(&term, "[x] Restore saved setup"); test_terminal_expect(&term, "ELF metadata changed");
    test_terminal_expect(&term, "code-breakpoint intent");
    test_terminal_resize(&term, 18, 48); test_terminal_expect(&term, "Restore saved setup");
    test_terminal_expect(&term, "[ Start session ]"); test_terminal_expect(&term, "[ Back ]");
    test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "ELF metadata changed");
    click("[ Start session ]"); test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_expect(&term, "Saved Source unavailable");
    setup_review(); test_terminal_expect(&term, "2 Watches | 1 code breakpoints"); click("[ Save setup ]");
    test_terminal_expect(&term, "Setup saved. Connection profile unchanged."); CHECK(!test_terminal_find(&term, "Save setup*", NULL, NULL));
    CHECK(tb_profiles_load(store)); TbSetup *partial = tb_profiles_setup(store, "saved copy"); CHECK(partial && tb_setup_equal(partial, saved)); free(partial);
    click("Unresolved setup entries"); test_terminal_expect(&term, "missing-saved-source.c"); test_terminal_expect(&term, "unsupported saved expression");
    test_terminal_expect(&term, "enabled intent"); test_terminal_expect(&term, "unresolved");
    test_terminal_expect(&term, "Acknowledge failures"); click("Acknowledge failures");
    CHECK(!test_terminal_find(&term, "Acknowledge failures", NULL, NULL));
    click("[ Back ]"); click("[ Back ]"); click("Continue"); test_terminal_expect(&term, "LIVE | Running");
    click("Pause"); test_terminal_expect(&term, "LIVE | Stopped"); click("Setup"); click("Unresolved setup entries");
    CHECK(!test_terminal_find(&term, "Acknowledge failures", NULL, NULL));
    /* Remove the Source entry by its row-specific control, then verify retained
     * breakpoint/Watch definitions remain and removal makes the draft dirty. */
    int x, y; CHECK(test_terminal_find(&term, "missing-saved-source.c", &x, &y));
    const char *remove = strstr(term.cells[y + 1], "[ Remove ]"); CHECK(remove); x = (int)(remove - term.cells[y + 1]) + 2;
    char mouse[80]; (void)snprintf(mouse, sizeof mouse, "\033[<0;%d;%dM\033[<0;%d;%dm", x, y + 2, x, y + 2);
    test_terminal_send(&term, mouse); settle(); CHECK(!test_terminal_find(&term, "missing-saved-source.c", NULL, NULL));
    click("[ Back ]"); test_terminal_expect(&term, "Save setup*"); click("Save setup"); click("[ Save setup ]");
    CHECK(tb_profiles_load(store)); partial = tb_profiles_setup(store, "saved copy");
    CHECK(partial && partial->panes[0].source_count == 1 && partial->break_count == 1 && partial->breaks[0].id == saved->breaks[0].id && partial->watch_count == 2); free(partial);
    click("[ Back ]"); click("Session"); click("Close session"); test_terminal_expect(&term, "[ Config ]"); stop(); free(saved);
    puts("PASS F01 restore review/default consent/ELF renewal, missing Source selection, partial saves, retained breakpoint/Watch definitions, failure acknowledgment and explicit removal");
}
static void live_setup(const char *binary, const char *fixture, TbProfiles *store)
{
    const char *root = getenv("XDG_CONFIG_HOME"); char preference[1024], source_directory[1024];
    (void)snprintf(source_directory, sizeof source_directory, "%s/source lookup", root); CHECK(mkdir(source_directory, 0700) == 0);
    (void)snprintf(preference, sizeof preference, "%s/the-bugger/preferences.json", root);
    int keys[TB_BIND_COUNT]; tb_keybind_defaults(keys); for (int i = 0; i < 7; ++i) { keys[i] = 18 + i; }
    CHECK(tb_keybinds_valid(keys));
    json_object *prefs = json_object_new_object(), *bindings = json_object_new_array(); CHECK(prefs && bindings);
    for (int i = 0; i < TB_BIND_COUNT; ++i) { CHECK(json_object_array_add(bindings, json_object_new_int(keys[i])) == 0); }
    CHECK(json_object_object_add(prefs, "version", json_object_new_int(1)) == 0);
    CHECK(json_object_object_add(prefs, "theme", json_object_new_string("catppuccin")) == 0);
    CHECK(json_object_object_add(prefs, "variant", json_object_new_string("macchiato")) == 0);
    CHECK(json_object_object_add(prefs, "keys", bindings) == 0);
    CHECK(json_object_to_file_ext(preference, prefs, JSON_C_TO_STRING_PLAIN) == 0); json_object_put(prefs);
    start(binary); test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, fixture);
    click("Review session"); click("Start native"); test_terminal_expect(&term, "LIVE | Loaded");
    const int widths[] = {48, 80, 123, 124, 152, 160};
    const char *names[] = {"Continue", "Pause", "Over", "Into", "Out", "BP", "Reset"};
    for (size_t w = 0; w < sizeof widths / sizeof widths[0]; ++w) {
        int cols = widths[w]; test_terminal_resize(&term, 44, cols); settle();
        int y = cols >= 124 ? 0 : 1, x = cols >= 124 ? 14 : 1;
        for (int i = 0; i < 7; ++i) {
            char key[24]; tb_keybind_name(keys[i], key, sizeof key); int length = (int)strlen(names[i]) + (int)strlen(key) + 3;
            if (x + length > cols - (y == 0 ? 18 : 1)) { ++y; x = 1; } x += length + 1;
            CHECK(test_terminal_find(&term, names[i], NULL, NULL));
        }
        int actual = -1;
        int notice_row = y + 2 + (cols >= 124 ? 1 : 0);
        /* tmux resizes its pane after updating the outer terminal. Wait for
         * the application geometry, not the first partial refresh. */
        for (int wait = 0; wait < 500; ++wait) {
            if (test_terminal_find(&term, "LIVE NATIVE", NULL, &actual) && actual == notice_row) { break; }
            test_terminal_pump(&term, 10);
        }
        if (actual != notice_row) {
            fprintf(stderr, "Toolbar width %d: notice row %d, expected %d\n", cols, actual, notice_row);
            for (int row = 0; row < 8; ++row) { fprintf(stderr, "%s\n", term.cells[row]); }
        }
        CHECK(actual == notice_row);
        CHECK(strspn(term.cells[0], " ") == (size_t)cols);
        /* One top margin row moves the toolbar and pane headers down once. */
        CHECK(test_terminal_find(&term, "Source  Symbols", NULL, &actual) && actual == y + 5);
        if (cols >= 124) {
            CHECK(test_terminal_find(&term, "Disassembly  Call Stack", NULL, &actual) && actual == y + 5);
            CHECK(test_terminal_find(&term, "Registers  Peripherals", NULL, &actual) && actual == y + 5);
        }
        CHECK(test_terminal_find(&term, "Setup", NULL, &actual) && actual == 1);
        CHECK(test_terminal_find(&term, "Session", NULL, &actual) && actual == 1);
        CHECK(test_terminal_find(&term, "Quit", NULL, &actual) && actual == 1);
    }
    click("Actions"); click("Choose source file"); test_terminal_expect(&term, "GDB source files");
    test_terminal_send(&term, "session.c\r"); settle(); click("tests/fixtures/native/session.c");
    setup_review(); test_terminal_expect(&term, "requires an associated saved");
    save("on demand", false); test_terminal_expect(&term, "Save setup*");
    click("Save setup"); test_terminal_expect(&term, "Save setup review"); click("[ Save setup ]");
    test_terminal_expect(&term, "Setup saved. Connection profile unchanged.");
    CHECK(!test_terminal_find(&term, "Save setup*", NULL, NULL)); CHECK(tb_profiles_load(store));
    TbSetup *saved = tb_profiles_setup(store, "on demand"); CHECK(saved && saved->panes[0].source_count == 1 && saved->panes[0].source_selected);
    TbProfile *profile = malloc(sizeof *profile); CHECK(profile); *profile = *tb_profiles_find(store, "on demand");
    click("[ Back ]"); click("Session"); click("Close session"); test_terminal_expect(&term, "[ Config ]"); stop();
    start(binary); click("[ Config ]"); click("Profiles"); click("on demand");
    click("Profile..."); click("Source directories"); click("Add entry"); input(source_directory); back();
    click("[ Duplicate ]"); input("saved copy"); test_terminal_expect(&term, "Unsaved draft unchanged.");
    CHECK(tb_profiles_load(store) && tb_profiles_find(store, "saved copy")->directory_count == 0);
    TbSetup *copy = tb_profiles_setup(store, "saved copy"); CHECK(copy && tb_setup_equal(copy, saved)); free(copy);
    back(); click("Review session"); click("Start native");
    test_terminal_expect(&term, "[x] Restore saved setup"); click("Restore saved setup"); click("[ Start session ]");
    test_terminal_expect(&term, "LIVE | Loaded");
    setup_review(); test_terminal_expect(&term, "Replace discards"); test_terminal_expect(&term, "Source directory"); back(); test_terminal_expect(&term, "Restoration information");
    CHECK(tb_profiles_load(store)); TbSetup *preserved = tb_profiles_setup(store, "on demand"); CHECK(preserved && tb_setup_equal(saved, preserved)); free(preserved);
    click("Save setup"); test_terminal_expect(&term, "Save setup review"); click("[ Replace setup ]"); test_terminal_expect(&term, "Setup saved. Connection profile unchanged.");
    CHECK(tb_profiles_load(store)); preserved = tb_profiles_setup(store, "on demand"); CHECK(preserved && !preserved->panes[0].source_count); free(preserved);
    CHECK(memcmp(profile, tb_profiles_find(store, "on demand"), sizeof *profile) == 0);
    copy = tb_profiles_setup(store, "saved copy"); CHECK(copy && tb_setup_equal(copy, saved)); free(copy);
    click("[ Back ]"); click("Session"); click("Close session"); test_terminal_expect(&term, "[ Config ]"); stop();
    restored_setup(binary, store);
    CHECK(tb_profiles_load(store) && tb_profiles_remove(store, "on demand") && tb_profiles_remove(store, "saved copy")); free(saved); free(profile); CHECK(unlink(preference) == 0 && rmdir(source_directory) == 0);
    puts("PASS F01 live saving/profile association, pinned identity, dirty state, skipped replacement, profile preservation and toolbar widths with long shortcuts");
}
static void recovery(const char *binary, const char *path)
{
    json_object *root = json_object_from_file(path); CHECK(root);
    CHECK(json_object_object_add(root, "version", json_object_new_int(3)) == 0);
    json_object *setups = json_object_new_array(); CHECK(setups);
    CHECK(json_object_array_add(setups, json_object_new_object()) == 0);
    CHECK(json_object_object_add(root, "setups", setups) == 0);
    CHECK(json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY) == 0);
    start(binary); click("[ Config ]"); click("Profiles"); test_terminal_expect(&term, "READ ONLY");
    test_terminal_resize(&term, 18, 48); test_terminal_expect(&term, "READ ONLY");
    test_terminal_resize(&term, 44, 160); settle();
    click("remote definition"); click("Save profile"); input("remote definition"); click("[ Confirm ]");
    test_terminal_expect(&term, "Profile writes blocked");
    json_object *unchanged = json_object_from_file(path); CHECK(unchanged && json_object_equal(root, unchanged)); json_object_put(unchanged);
    back(); back(); click("[ Config ]"); click("Profiles"); test_terminal_expect(&term, "READ ONLY");
    CHECK(json_object_object_add(root, "setups", json_object_new_array()) == 0);
    CHECK(json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY) == 0); json_object_put(root);
    click("Reload file"); test_terminal_expect(&term, "Profile file reloaded.");
    CHECK(!test_terminal_find(&term, "READ ONLY", NULL, NULL));
    click("remote definition"); save("remote definition", true); stop();
    puts("PASS F01 schema-3 read-only recovery, valid connection review, blocked save, 48-column indication and repaired reload");
}
static void import_setup(const char *binary, const char *fixture, TbProfiles *store)
{
    start(binary); test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, fixture);
    click("Review session"); click("Start native"); test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_lower(&term, 2, true); click("Command"); test_terminal_send(&term, "break main\r");
    test_terminal_expect(&term, "Breakpoint 1"); click("Setup"); click("Review GDB breakpoints");
    test_terminal_expect(&term, "Review for import"); click("Review for import");
    click("Include reviewed definition"); test_terminal_expect(&term, "Choose hardware and ignore-count intent");
    click("Hardware policy: choose"); test_terminal_expect(&term, "Hardware policy: Default");
    click("Ignore count: choose"); test_terminal_expect(&term, "Ignore count: none");
    click("Include reviewed definition"); test_terminal_expect(&term, "included in the unsaved setup draft");
    CHECK(!test_terminal_find(&term, "Review for import", NULL, NULL));
    click("[ Back ]"); test_terminal_expect(&term, "Save setup*");
    save("imported definition", false); click("Save setup"); click("[ Save setup ]");
    test_terminal_expect(&term, "Setup saved. Connection profile unchanged.");
    CHECK(tb_profiles_load(store)); TbSetup *saved = tb_profiles_setup(store, "imported definition");
    CHECK(saved && saved->break_count == 1 && !strcmp(saved->breaks[0].location, "main") && !saved->breaks[0].hardware && !saved->breaks[0].ignore_set); free(saved);
    click("Unresolved setup entries"); test_terminal_expect(&term, "imported draft; not restored");
    CHECK(!test_terminal_find(&term, "Acknowledge failures", NULL, NULL)); click("[ Remove ]");
    click("[ Back ]"); test_terminal_expect(&term, "Save setup*"); click("[ Back ]");
    click("Session"); click("Close session"); test_terminal_expect(&term, "[ Config ]"); stop();
    puts("PASS F01 actual GDB backend-only breakpoint review, explicit hardware/count decisions, unsaved import without duplicate, save and draft removal (direct/tmux)");
}
int main(int argc, char **argv)
{
    CHECK(argc == 3 || (argc == 4 && strcmp(argv[3], "--native") == 0)); CHECK(atexit(cleanup) == 0);
    char root[] = "/tmp/the-bugger-m13-tui-XXXXXX"; CHECK(mkdtemp(root)); CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    CHECK(setenv("TB_TEST_CONFIG_HOME", root, 1) == 0);
    if (getenv("TB_PROFILE_SETUP_TMUX")) {
        (void)snprintf(tmux_socket, sizeof tmux_socket, "the-bugger-setup-%ld", (long)getpid());
        int fd = mkstemp(tmux_config); CHECK(fd >= 0);
        const char settings[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
        CHECK(write(fd, settings, sizeof settings - 1) == (ssize_t)(sizeof settings - 1) && close(fd) == 0);
        CHECK(unsetenv("TMUX") == 0 && atexit(tmux_cleanup) == 0);
    }
    start(argv[1]); test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[2]);
    test_terminal_send(&term, "\t\025\"two words\" \"\" '$HOME;$(false)'");
    save("native saved", false);
    click("Profile..."); CHECK(!test_terminal_find(&term, "Action keys", NULL, NULL));
    click("Source directories"); click("Add entry"); input(root); back();
    click("Source mappings"); click("Add entry"); input("/old build"); input(root); back(); back();
    save("native saved", true);
    click("[ Delete ]"); test_terminal_expect(&term, "Delete profile:"); back();
    stop(); start(argv[1]); click("[ Config ]"); click("Profiles"); click("native saved");
    test_terminal_expect(&term, "two words"); CHECK(test_terminal_find(&term, "$HOME;$(false)", NULL, NULL));
    if (argc == 4) {
        click("Review session"); click("Start native"); test_terminal_expect(&term, "LIVE | Loaded");
        test_terminal_expect(&term, "Continue F5"); test_terminal_expect(&term, "Into F11");
        click("Actions"); click("Source lookup settings"); test_terminal_expect(&term, root);
        test_terminal_expect(&term, "/old build"); back();
        test_terminal_lower(&term, 2, true);
        test_terminal_send(&term, "\033[15~"); test_terminal_expect(&term, "FIXTURE PID=");
        click("Pause"); test_terminal_expect(&term, "LIVE | Stopped");
        test_terminal_send(&term, "q"); test_terminal_expect(&term, "Terminate the launched program");
        click("Close session"); test_terminal_expect(&term, "Recent: native saved");
        stop(); start(argv[1]); click("Recent: native saved"); test_terminal_expect(&term, "Executable / ELF");
    }
    back(); click("[ Config ]"); click("Profiles"); click("New OpenOCD"); click("Profile...");
    click("Ordered OpenOCD scripts"); click("Add entry"); input("board/first with spaces.cfg");
    click("Add entry"); input("custom/second.cfg"); back(); back(); save("board definition", false);
    back(); click("[ Config ]"); click("Profiles"); click("New remote"); click("Profile...");
    click("Remote protocol: remote"); back(); save("remote definition", false);
    stop();
    TbProfiles *store = tb_profiles_create(); CHECK(store && tb_profiles_load(store));
    CHECK(tb_profiles_count(store) == 3);
    const TbProfile *native = tb_profiles_find(store, "native saved"); CHECK(native && native->arguments.count == 3);
    CHECK(native->directory_count == 1 && native->rule_count == 1);
    CHECK(strcmp(native->arguments.values[0], "two words") == 0 && !*native->arguments.values[1]);
    const TbProfile *board = tb_profiles_find(store, "board definition"); CHECK(board && board->kind == 1 && board->scripts.count == 2);
    CHECK(strcmp(board->scripts.values[0], "board/first with spaces.cfg") == 0 && strcmp(board->scripts.values[1], "custom/second.cfg") == 0);
    const TbProfile *remote = tb_profiles_find(store, "remote definition"); CHECK(remote && remote->kind == 2 && strcmp(remote->protocol, "extended-remote") == 0);
    if (argc == 4) { CHECK(strcmp(tb_profiles_recent(store, 0), "native saved") == 0); }
    start(argv[1]); click("[ Config ]"); click("Profiles"); click("native saved"); click("[ Delete ]"); click("[ Confirm ]"); stop();
    CHECK(tb_profiles_load(store) && tb_profiles_count(store) == 2 && !tb_profiles_find(store, "native saved"));
    if (argc == 4) { live_setup(argv[1], argv[2], store); import_setup(argv[1], argv[2], store); }
    const char *path = tb_profiles_path(store); recovery(argv[1], path); CHECK(unlink(path) == 0);
    char directory[512], lock[512]; (void)snprintf(directory, sizeof directory, "%s/the-bugger", root); (void)snprintf(lock, sizeof lock, "%s/the-bugger/.lock", root);
    CHECK(unlink(lock) == 0); CHECK(rmdir(directory) == 0); CHECK(rmdir(root) == 0); tb_profiles_destroy(store);
    puts("PASS M13 TUI profile create/reload/edit/delete confirmation, literal argv, source options, inert ordered OpenOCD/remote definitions");
    if (argc == 4) { puts("PASS M13 actual native profile reopen, source settings, execution with global shortcuts, successful-session recents and clean session exit"); }
    return EXIT_SUCCESS;
}
