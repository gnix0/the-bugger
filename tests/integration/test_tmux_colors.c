#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char socket_name[80], configuration[] = "/tmp/the-bugger-rgb-XXXXXX";
static TestTerminal term;
static bool active;
static void cleanup(void)
{
    if (active) {
        pid_t got = waitpid(term.child, NULL, WNOHANG);
        if (!got) { (void)kill(term.child, SIGTERM); while (waitpid(term.child, NULL, 0) < 0 && errno == EINTR) { } }
    }
    pid_t child = fork();
    if (!child) {
        if (!freopen("/dev/null", "w", stderr)) { _exit(127); }
        execlp("tmux", "tmux", "-L", socket_name, "kill-server", (char *)NULL); _exit(127);
    }
    if (child > 0) { while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { } }
    (void)unlink(configuration);
}
static void settle(void)
{
    struct timespec t; CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    long long until = (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000 + 120;
    do {
        test_terminal_pump(&term, 5); CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    } while ((long long)t.tv_sec * 1000 + t.tv_nsec / 1000000 < until);
}
static bool rgb(unsigned value, bool background)
{
    char semicolon[64], colon[64];
    (void)snprintf(semicolon, sizeof semicolon, "%d;2;%u;%u;%u", background ? 48 : 38,
                   value >> 16U, value >> 8U & 255U, value & 255U);
    (void)snprintf(colon, sizeof colon, "%d:2::%u:%u:%u", background ? 48 : 38,
                   value >> 16U, value >> 8U & 255U, value & 255U);
    return strstr(term.output, semicolon) || strstr(term.output, colon);
}
static void preview(void)
{
    test_terminal_click(&term, "[ Config ]"); test_terminal_click(&term, "Themes");
    test_terminal_click(&term, "[ Preview ]"); test_terminal_expect(&term, "THEME PREVIEW"); settle();
}
static void colors(void)
{
    /* Exact Macchiato workspace/panel/raised surfaces and text/accent channels. */
    CHECK(rgb(0x181926, true) && rgb(0x24273a, true) && rgb(0x363a4f, true));
    CHECK(rgb(0xcad3f5, false) && rgb(0xa6da95, false) && rgb(0xed8796, false));
}


static void black_background(void)
{
    test_terminal_click(&term, "[ Config ]");
    test_terminal_click(&term, "Themes");
    test_terminal_click(&term, "Theme       <");
    test_terminal_click(&term, "Ashen");
    test_terminal_click(&term, "Variant     <");
    test_terminal_click(&term, "high-contrast");
    test_terminal_click(&term, "[ Save ]");
    test_terminal_click(&term, "[ Back ]");
    test_terminal_expect(&term, "Start a debugging session");
    settle();
    CHECK(rgb(0, true));
    CHECK(term.backgrounds[5][5] == 0);
    CHECK(term.backgrounds[term.rows / 2][term.cols - 2] == 0);

    /* Theme switching must leave no ANSI-black gray behind in pane content. */
    preview();
    CHECK(rgb(0, true));
    int x, y;
    CHECK(test_terminal_find(&term, "Source", &x, &y));
    CHECK(term.backgrounds[y + 3][x + 8] == 0);
    test_terminal_resize(&term, 20, 80);
    test_terminal_expect(&term, "THEME PREVIEW");
    settle();
    CHECK(test_terminal_find(&term, "Source", &x, &y));
    CHECK(term.backgrounds[y + 3][x + 8] == 0);

    test_terminal_resize(&term, 44, 140);
    test_terminal_click(&term, "[ Back ]");
    test_terminal_click(&term, "Theme       <");
    test_terminal_click(&term, "Catppuccin");
    test_terminal_click(&term, "[ Save ]");
    test_terminal_click(&term, "[ Back ]");
}


int main(int argc, char **argv)
{
    CHECK(argc == 2 && atexit(cleanup) == 0);
    char root[] = "/tmp/the-bugger-black-XXXXXX";
    CHECK(mkdtemp(root) && setenv("TB_TEST_CONFIG_HOME", root, 1) == 0);
    CHECK(unsetenv("TMUX") == 0 && setenv("COLORTERM", "truecolor", 1) == 0);
    test_terminal_start(&term, argv[1], "alacritty", false); active = true;
    preview(); colors();
    test_terminal_click(&term, "[ Back ]");
    test_terminal_click(&term, "[ Back ]");
    test_terminal_click(&term, "[ Back ]");
    black_background();
    test_terminal_stop(&term, 0); active = false;
    (void)snprintf(socket_name, sizeof socket_name, "the-bugger-rgb-%ld", (long)getpid());
    int fd = mkstemp(configuration); CHECK(fd >= 0);
    /* Same RGB override as the reported setup; no changes to user configuration. */
    const char options[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\nset -as terminal-overrides ',*:Tc'\n";
    CHECK(write(fd, options, sizeof options - 1) == (ssize_t)(sizeof options - 1)); CHECK(close(fd) == 0);
    char *args[] = {"tmux", "-L", socket_name, "-f", configuration, "new-session", "-s", "rgb", argv[1], "--demo", NULL};
    test_terminal_start_argv(&term, args, "alacritty", false); active = true;
    preview(); colors();
    test_terminal_click(&term, "[ Back ]");
    /* A different light palette must update both channels through tmux too. */
    test_terminal_click(&term, "Theme       <"); test_terminal_expect(&term, "Choose theme");
    test_terminal_send(&term, "\033OF"); test_terminal_expect(&term, "Rose Pine");
    test_terminal_click(&term, "Rose Pine");
    test_terminal_click(&term, "Variant     <"); test_terminal_expect(&term, "Choose variant");
    test_terminal_click(&term, "dawn");
    test_terminal_click(&term, "[ Preview ]"); test_terminal_expect(&term, "Rose Pine / dawn"); settle();
    CHECK(rgb(0xf8f0e7, true) && rgb(0xfaf4ed, true) && rgb(0x464261, false));
    test_terminal_click(&term, "[ Back ]");
    test_terminal_click(&term, "[ Back ]");
    test_terminal_click(&term, "[ Back ]");
    black_background();
    test_terminal_stop(&term, 0); active = false;
    CHECK(unsetenv("COLORTERM") == 0);
    for (int mode = 0; mode < 2; ++mode) {
        (void)snprintf(socket_name, sizeof socket_name, "the-bugger-rgb-%ld-fallback-%d", (long)getpid(), mode);
        /* New tmux versions advertise COLORTERM themselves. Remove it only
         * from this fixture's app process to exercise the absent-signal path. */
        char *fallback[] = {"tmux", "-L", socket_name, "-f", configuration, "new-session", "-s", "fallback",
                            "env", "-u", "COLORTERM", argv[1], "--demo", NULL};
        test_terminal_start_argv(&term, mode ? args : fallback, "alacritty", mode != 0); active = true;
        test_terminal_expect(&term, "Start a debugging session");
        test_terminal_send(&term, "\r\t\t\t\t\t\r"); test_terminal_expect(&term, "Review session preview");
        test_terminal_send(&term, "\r"); test_terminal_expect(&term, mode ? "monochrome" : "256-color"); settle();
        CHECK(!rgb(0x24273a, true));
        test_terminal_stop(&term, 0); active = false;
    }
    puts("PASS real tmux RGB-disabled and NO_COLOR fallbacks retain navigation and terminal restoration");
    puts("PASS real tmux Tc forwarding: exact Macchiato foreground/background roles match standalone and theme switching updates backgrounds");
    puts("PASS Ashen high-contrast emits RGB black in launcher and preview content, "
         "directly and through tmux");
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/the-bugger/preferences.json", root);
    CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof(path), "%s/the-bugger/.preferences.lock", root);
    CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof(path), "%s/the-bugger", root);
    CHECK(rmdir(path) == 0 && rmdir(root) == 0);
    return EXIT_SUCCESS;
}
