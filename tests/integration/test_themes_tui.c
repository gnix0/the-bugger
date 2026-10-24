#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static TestTerminal term;
static bool active;
static void cleanup(void)
{
    if (!active || term.child <= 1) { return; }
    pid_t result;
    do { result = waitpid(term.child, NULL, WNOHANG); } while (result < 0 && errno == EINTR);
    if (!result) { (void)kill(term.child, SIGTERM); while (waitpid(term.child, NULL, 0) < 0 && errno == EINTR) { } }
}
static void click(const char *text) { test_terminal_click(&term, text); test_terminal_pump(&term, 60); }
static void back(void) { test_terminal_send(&term, "\033"); test_terminal_pump(&term, 60); }
static void start(const char *binary, const char *type, bool no_color)
{
    char *const args[] = {(char *)binary, NULL};
    test_terminal_start_argv(&term, args, type, no_color); active = true;
    test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "[ Config ]");
}
static void open_themes(void)
{
    click("[ Config ]"); test_terminal_expect(&term, "Configuration");
    int x, y; CHECK(test_terminal_find(&term, "Configuration", &x, &y));
    CHECK(x > 30 && y > 8 && y < 24); /* Centered card on both axes. */
    test_terminal_expect(&term, "Profiles"); click("Themes"); test_terminal_expect(&term, "Variant");
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 && atexit(cleanup) == 0);
    char root[] = "/tmp/the-bugger-themes-tui-XXXXXX"; CHECK(mkdtemp(root));
    CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    start(argv[1], "xterm-256color", false); open_themes();
    test_terminal_expect(&term, "< Catppuccin >"); test_terminal_expect(&term, "< macchiato >");
    click("Variant"); test_terminal_expect(&term, "< mocha >");
    click("< mocha >"); test_terminal_expect(&term, "< macchiato >");
    click("Variant"); test_terminal_expect(&term, "< mocha >");
    click("[ Preview ]"); test_terminal_expect(&term, "THEME PREVIEW | Catppuccin / mocha");
    test_terminal_expect(&term, "simulated data"); test_terminal_expect(&term, "Source");
    back(); test_terminal_expect(&term, "< mocha >"); back(); back();
    open_themes(); test_terminal_expect(&term, "< macchiato >"); /* Back discarded preview. */
    const char *names[] = {"Catppuccin", "Everforest", "Tokyo Night", "Rose Pine", "Nightfox", "Gruvbox Material", "Monokai Pro"};
    const int counts[] = {4, 6, 4, 3, 7, 6, 7};
    for (int family = 0; family < 7; ++family) {
        char label[80]; (void)snprintf(label, sizeof label, "< %s >", names[family]); test_terminal_expect(&term, label);
        for (int variant = 0; variant < counts[family]; ++variant) {
            click("[ Preview ]"); test_terminal_expect(&term, "THEME PREVIEW"); back(); click("Variant");
        }
        click("Theme       <");
    }
    char path[512]; (void)snprintf(path, sizeof path, "%s/the-bugger/preferences.json", root);
    CHECK(access(path, F_OK) != 0); /* Every preview so far remained unsaved. */
    test_terminal_expect(&term, "< macchiato >"); click("Variant"); click("[ Save ]");
    test_terminal_expect(&term, "Configuration"); test_terminal_expect(&term, "Theme saved.");
    test_terminal_stop(&term, 0); active = false;
    start(argv[1], "vt100", true);
    test_terminal_send(&term, "\t\t\t\r"); test_terminal_expect(&term, "Configuration");
    test_terminal_send(&term, "\t\r"); test_terminal_expect(&term, "< mocha >");
    /* Keyboard changes theme and the dependent variant, including backward wrap. */
    test_terminal_send(&term, "\033OD"); test_terminal_expect(&term, "< Monokai Pro >");
    test_terminal_send(&term, "\t\033OD"); test_terminal_expect(&term, "< light >");
    test_terminal_resize(&term, 18, 48); test_terminal_expect(&term, "Variant");
    /* Resize retains Variant focus; Back then Preview are the next two controls. */
    int variant_x, variant_y; CHECK(test_terminal_find(&term, "Variant", &variant_x, &variant_y));
    CHECK(term.inverse[variant_y][variant_x]);
    test_terminal_send(&term, "\t\t\r"); test_terminal_expect(&term, "THEME PREVIEW"); back();
    test_terminal_stop(&term, 0); active = false;
    CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof path, "%s/the-bugger/.preferences.lock", root); CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof path, "%s/the-bugger", root); CHECK(rmdir(path) == 0 && rmdir(root) == 0);
    puts("PASS Config/Themes: centered menus, 37 variant previews, cancel/save/reload, dependent variant selection, keyboard/mouse, compact and monochrome");
    return EXIT_SUCCESS;
}
