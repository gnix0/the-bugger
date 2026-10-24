#define _POSIX_C_SOURCE 200809L
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
static void back(void) { test_terminal_send(&term, "\033"); settle(); }
static void input(const char *text)
{
    test_terminal_expect(&term, "Profile field"); test_terminal_send(&term, "\025");
    test_terminal_send(&term, text); test_terminal_send(&term, "\r"); settle();
}
static void start(const char *binary)
{
    char *const arguments[] = {(char *)binary, NULL}; test_terminal_start_argv(&term, arguments, "xterm-256color", false); active = true;
    test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "[ Config ]");
}
static void stop(void) { test_terminal_stop(&term, 0); active = false; }
static void save(const char *name, bool replace)
{
    click("Save profile"); input(name);
    if (replace) { test_terminal_expect(&term, "Replace profile:"); click("[ Confirm ]"); }
    test_terminal_expect(&term, "Profile file saved atomically.");
}
int main(int argc, char **argv)
{
    CHECK(argc == 3 || (argc == 4 && strcmp(argv[3], "--native") == 0)); CHECK(atexit(cleanup) == 0);
    char root[] = "/tmp/the-bugger-m13-tui-XXXXXX"; CHECK(mkdtemp(root)); CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    start(argv[1]); test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[2]);
    test_terminal_send(&term, "\t\025\"two words\" \"\" '$HOME;$(false)'");
    save("native saved", false);
    click("Profile..."); click("Action keys"); click("Step into: F11"); input("F5");
    test_terminal_expect(&term, "already belongs to another action"); input("F7");
    click("Continue: F5"); input("Ctrl+Q"); test_terminal_expect(&term, "Unsupported/reserved binding"); input("F6");
    back(); click("Source directories"); click("Add entry"); input(root); back();
    click("Source mappings"); click("Add entry"); input("/old build"); input(root); back(); back();
    save("native saved", true);
    click("[ Delete ]"); test_terminal_expect(&term, "Delete profile:"); back();
    stop(); start(argv[1]); click("[ Config ]"); click("Profiles"); click("native saved");
    test_terminal_expect(&term, "two words"); CHECK(test_terminal_find(&term, "$HOME;$(false)", NULL, NULL));
    if (argc == 4) {
        click("Review session"); click("Start native"); test_terminal_expect(&term, "LIVE | Loaded");
        test_terminal_expect(&term, "Continue F6"); test_terminal_expect(&term, "Into F7");
        click("Actions"); click("Source lookup settings"); test_terminal_expect(&term, root);
        test_terminal_expect(&term, "/old build"); back();
        test_terminal_send(&term, "\033[17~"); test_terminal_expect(&term, "FIXTURE PID=");
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
    CHECK(native->keys[0] == 6 && native->keys[3] == 7 && native->directory_count == 1 && native->rule_count == 1);
    CHECK(strcmp(native->arguments.values[0], "two words") == 0 && !*native->arguments.values[1]);
    const TbProfile *board = tb_profiles_find(store, "board definition"); CHECK(board && board->kind == 1 && board->scripts.count == 2);
    CHECK(strcmp(board->scripts.values[0], "board/first with spaces.cfg") == 0 && strcmp(board->scripts.values[1], "custom/second.cfg") == 0);
    const TbProfile *remote = tb_profiles_find(store, "remote definition"); CHECK(remote && remote->kind == 2 && strcmp(remote->protocol, "extended-remote") == 0);
    if (argc == 4) { CHECK(strcmp(tb_profiles_recent(store, 0), "native saved") == 0); }
    start(argv[1]); click("[ Config ]"); click("Profiles"); click("native saved"); click("[ Delete ]"); click("[ Confirm ]"); stop();
    CHECK(tb_profiles_load(store) && tb_profiles_count(store) == 2 && !tb_profiles_find(store, "native saved"));
    const char *path = tb_profiles_path(store); CHECK(unlink(path) == 0);
    char directory[512], lock[512]; (void)snprintf(directory, sizeof directory, "%s/the-bugger", root); (void)snprintf(lock, sizeof lock, "%s/the-bugger/.lock", root);
    CHECK(unlink(lock) == 0); CHECK(rmdir(directory) == 0); CHECK(rmdir(root) == 0); tb_profiles_destroy(store);
    puts("PASS M13 TUI profile create/reload/edit/delete confirmation, literal argv, source options, reserved/conflicting remaps and inert ordered OpenOCD/remote definitions");
    if (argc == 4) { puts("PASS M13 actual native profile reopen, source settings, remapped execution, successful-session recents and clean session exit"); }
    return EXIT_SUCCESS;
}
