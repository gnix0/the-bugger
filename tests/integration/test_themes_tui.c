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
static void footer(const char *text)
{
    test_terminal_expect(&term, text);
    CHECK(strstr(term.cells[term.rows - 2], text) || strstr(term.cells[term.rows - 1], text));
}

static void contextual_footers(const char *binary, bool demo, bool no_color)
{
    char *const args[] = {(char *)binary, demo ? "--demo" : NULL, NULL};
    test_terminal_start_argv(&term, args, "xterm-256color", no_color);
    active = true;
    test_terminal_resize(&term, 44, 160);
    /* --demo starts in the launcher too; nothing is connected during this test. */
    footer(demo ? "DEMO only." : "Choose native launch/attach");
    int config_x, config_y, quit_x, quit_y;
    CHECK(test_terminal_find(&term, "[ Config ]", &config_x, &config_y));
    CHECK(test_terminal_find(&term, "[ Quit ]", &quit_x, &quit_y));
    CHECK(config_x == 33 && quit_x == 119 && config_y == quit_y);
    click("[ Config ]"); footer("Choose personal");
    int hint_x, hint_y;
    const char *hints = "Tab / arrows / Enter or click | Esc back | Ctrl+Q quit app";
    CHECK(test_terminal_find(&term, hints, &hint_x, &hint_y));
    CHECK(hint_x + (int)strlen(hints) == term.cols - 1 && hint_y == term.rows - 1);
    CHECK(!test_terminal_find(&term, "Choose native launch/attach", NULL, NULL));
    click("Keybinds"); footer("Rows ");
    test_terminal_send(&term, "\r"); footer("Press a key.");
    back(); footer("Capture cancelled.");
    back(); footer("Choose personal");
    CHECK(!test_terminal_find(&term, "Capture cancelled.", NULL, NULL));
    click("Themes"); footer("Preview a theme and variant.");
    click("[ Preview ]"); test_terminal_expect(&term, "THEME PREVIEW");
    back(); footer("Preview a theme and variant.");
    back(); footer("Choose personal");
    if (!demo) {
        click("Profiles"); footer("Open a saved connection profile");
        const char *const actions[] = {"New native", "New OpenOCD", "New remote", "Reload file", "[ Back ]"};
        int previous_x = -1, previous_y = -1;
        for (int action = 0; action < 5; ++action) {
            int x, y; CHECK(test_terminal_find(&term, actions[action], &x, &y));
            if (action) {
                CHECK(y == previous_y && x >= previous_x + (int)strlen(actions[action - 1]) + 2);
            }
            previous_x = x; previous_y = y;
        }
        test_terminal_resize(&term, 18, 48); test_terminal_expect(&term, "Reload file");
        int new_x, new_y, reload_x, reload_y, back_x, back_y;
        CHECK(test_terminal_find(&term, "New native", &new_x, &new_y));
        CHECK(test_terminal_find(&term, "Reload file", &reload_x, &reload_y));
        CHECK(test_terminal_find(&term, "[ Back ]", &back_x, &back_y));
        CHECK(new_y + 1 == reload_y && reload_y == back_y);
        CHECK(back_x > reload_x + (int)strlen("Reload file") && new_x < reload_x);
        click("Reload file"); footer("Profile file reloaded.");
        test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "New OpenOCD");
        back(); footer("Choose personal");
    }
    back(); footer(demo ? "DEMO only." : "Choose native launch/attach");
    const char *const sessions[] = {"Linux application", "Embedded target", "Connect to GDB server"};
    for (int i = 0; i < 3; ++i) {
        click(sessions[i]); footer("Edit this session's settings");
        if (i < 2) {
            int first_x, first_y, second_y = -1;
            CHECK(test_terminal_find(&term, i ? "OpenOCD setup" : "Mode", &first_x, &first_y));
            const char *second = i ? "Board script" : "Executable / ELF";
            for (int row = first_y + 1; row < term.rows; ++row) {
                if (strncmp(term.cells[row] + first_x, second, strlen(second)) == 0) { second_y = row; break; }
            }
            CHECK(second_y == first_y + 2);
        }
        if (i == 1 && !demo) {
            int binary_x, binary_y, last_x, last_y, browse_x, browse_y, note_x, note_y;
            CHECK(test_terminal_find(&term, "OpenOCD binary", &binary_x, &binary_y));
            CHECK(test_terminal_find(&term, "Start OpenOCD", &last_x, &last_y));
            CHECK(test_terminal_find(&term, "[ Browse boards ]", &browse_x, &browse_y));
            CHECK(test_terminal_find(&term, "Choose installed scripts", &note_x, &note_y));
            CHECK(last_y == binary_y + 2);
            CHECK(browse_y == last_y + 2 && note_y == browse_y + 2);
        }
        if (!demo) {
            const char *const endings[] = {"literal.", "Profile options.", "profile options."};
            int note_x, note_y, review_x, review_y, save_x, save_y;
            test_terminal_expect(&term, endings[i]);
            CHECK(test_terminal_find(&term, endings[i], &note_x, &note_y));
            CHECK(test_terminal_find(&term, "[ Review session ]", &review_x, &review_y));
            CHECK(test_terminal_find(&term, "[ Save profile ]", &save_x, &save_y));
            CHECK(review_y == note_y + 2 && save_y == review_y + 2);
        }
        if (i == 0) {
            int title_x, title_y, action_x, action_y;
            CHECK(test_terminal_find(&term, "Linux application", &title_x, &title_y));
            CHECK(test_terminal_find(&term, demo ? "[ Review preview ]" : "[ Review session ]", &action_x, &action_y));
            CHECK(action_x >= title_x && action_x <= title_x + 2 && title_x > 20 && title_y > 8);
            CHECK(action_y - title_y < 16 && action_y < term.rows - 6);
            if (!demo) {
                int save_x, save_y, profile_x, profile_y;
                CHECK(test_terminal_find(&term, "[ Save profile ]", &save_x, &save_y));
                CHECK(test_terminal_find(&term, "[ Profile... ]", &profile_x, &profile_y));
                CHECK(save_y == profile_y && save_y == action_y + 2);
                CHECK(profile_x > save_x + (int)strlen("[ Save profile ]"));
            }
            click(demo ? "[ Review preview ]" : "[ Review session ]");
            footer(demo ? "Review the simulated session settings." : "Review the session settings before starting;");
            back(); footer("Edit this session's settings");
            if (!demo) {
                click("[ Profile... ]"); footer("Edit connection profile settings.");
                click("Source directories"); footer("Edit connection profile settings.");
                click("Add entry"); footer("Edit this profile field;");
                back(); footer("Edit connection profile settings.");
                back(); back(); footer("Edit this session's settings");
                click("[ Delete ]"); footer("Load a saved profile before deleting it.");
            }
        }
        if (i == 1 && !demo) {
            click("[ Browse boards ]"); footer("Choose an installed OpenOCD script;");
            click("[ Filter ]"); footer("Filter installed OpenOCD scripts");
            back(); footer("Choose an installed OpenOCD script;");
            back(); footer("Edit this session's settings");
        }
        back(); footer(demo ? "DEMO only." : "Choose native launch/attach");
    }
    test_terminal_resize(&term, 18, 48);
    click("[ Config ]"); footer("Choose personal");
    click("Themes"); footer("Preview a theme and variant.");
    test_terminal_stop(&term, 0); active = false;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 && atexit(cleanup) == 0);
    char root[] = "/tmp/the-bugger-themes-tui-XXXXXX"; CHECK(mkdtemp(root));
    CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    CHECK(setenv("TB_TEST_CONFIG_HOME", root, 1) == 0);
    contextual_footers(argv[1], false, false);
    contextual_footers(argv[1], false, true);
    contextual_footers(argv[1], true, false);
    puts("PASS contextual menu footers, navigation clears stale notices, errors remain visible, compact and monochrome");
    start(argv[1], "xterm-256color", false); open_themes();
    test_terminal_expect(&term, "< Catppuccin >"); test_terminal_expect(&term, "< macchiato >");
    click("Variant"); test_terminal_expect(&term, "< mocha >");
    click("< mocha >"); test_terminal_expect(&term, "< macchiato >");
    click("Variant"); test_terminal_expect(&term, "< mocha >");
    click("[ Preview ]"); test_terminal_expect(&term, "THEME PREVIEW | Catppuccin / mocha");
    test_terminal_expect(&term, "simulated data"); test_terminal_expect(&term, "Source");
    back(); test_terminal_expect(&term, "< mocha >"); back(); back();
    open_themes(); test_terminal_expect(&term, "< macchiato >"); /* Back discarded preview. */
    const char *names[] = {"Catppuccin", "Everforest", "Tokyo Night", "Rose Pine", "Nightfox", "Gruvbox Material", "Monokai Pro", "Gruber Darker", "Zenburn", "Modus", "Solarized"};
    const int counts[] = {4, 6, 4, 3, 7, 6, 7, 1, 1, 8, 2};
    for (int family = 0; family < 11; ++family) {
        char label[80]; (void)snprintf(label, sizeof label, "< %s >", names[family]); test_terminal_expect(&term, label);
        for (int variant = 0; variant < counts[family]; ++variant) {
            click("[ Preview ]"); test_terminal_expect(&term, "THEME PREVIEW");
            test_terminal_resize(&term, 18, 48); test_terminal_expect(&term, "THEME PREVIEW");
            back(); test_terminal_expect(&term, "Variant");
            test_terminal_resize(&term, 44, 160); click("Variant");
        }
        click("Theme       <");
    }
    char path[512]; (void)snprintf(path, sizeof path, "%s/the-bugger/preferences.json", root);
    CHECK(access(path, F_OK) != 0); /* Every preview so far remained unsaved. */
    test_terminal_expect(&term, "< macchiato >"); click("Variant"); click("[ Save ]");
    test_terminal_expect(&term, "Configuration"); footer("Theme saved.");
    click("Themes"); footer("Preview a theme and variant.");
    CHECK(!test_terminal_find(&term, "Theme saved.", NULL, NULL));
    back(); footer("Choose personal");
    test_terminal_stop(&term, 0); active = false;
    start(argv[1], "vt100", true);
    test_terminal_send(&term, "\t\t\t\r"); test_terminal_expect(&term, "Configuration");
    test_terminal_send(&term, "\t\r"); test_terminal_expect(&term, "< mocha >");
    /* Keyboard changes theme and the dependent variant, including backward wrap. */
    test_terminal_send(&term, "\033OD"); test_terminal_expect(&term, "< Solarized >");
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
    puts("PASS Config/Themes: centered menus, 49 variant previews, cancel/save/reload, dependent variant selection, keyboard/mouse, compact and monochrome");
    return EXIT_SUCCESS;
}
