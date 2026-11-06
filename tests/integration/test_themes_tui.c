#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "../support/terminal.h"
#include "../support/check.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
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
static void mouse_at(int button, int x, int y, bool release)
{
    char event[80]; (void)snprintf(event, sizeof event, "\033[<%d;%d;%d%c", button, x + 1, y + 1, release ? 'm' : 'M');
    test_terminal_send(&term, event);
    for (int i = 0; i < 20; ++i) { test_terminal_pump(&term, 5); }
}
static void cycle(const char *text, bool forward)
{
    click(text); test_terminal_expect(&term, "Choose ");
    back(); test_terminal_send(&term, forward ? "\033OC" : "\033OD");
    test_terminal_pump(&term, 60);
}
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

/* The harness normalizes UTF-8 box drawing but retains DEC ACS bytes. */
static bool border_glyph(int y, int x, char normalized, char acs)
{
    char value = term.cells[y][x];
    return value == normalized || value == acs;
}

typedef struct {
    int      x;
    int      y;
    int      width;
    int      height;
    uint64_t text;
} PreviewSource_t;

static PreviewSource_t preview_source(void)
{
    PreviewSource_t source = {0};
    int footer_y;
    CHECK(test_terminal_find(&term, "Source", &source.x, &source.y));
    CHECK(test_terminal_find(&term, "main.c | UTF-8 | C", NULL, &footer_y));
    const char *demo = strstr(term.cells[footer_y] + source.x, "DEMO");
    CHECK(demo);
    source.width = (int)(demo - term.cells[footer_y]) + 5 - source.x;
    source.height = footer_y + 1 - source.y;
    source.text = UINT64_C(14695981039346656037);
    for (int y = source.y; y < source.y + source.height; ++y) {
        for (int x = source.x; x < source.x + source.width; ++x) {
            source.text ^= (unsigned char)term.cells[y][x];
            source.text *= UINT64_C(1099511628211);
        }
    }
    return source;
}

static void same_source(PreviewSource_t expected)
{
    PreviewSource_t actual = preview_source();
    CHECK(actual.x == expected.x && actual.y == expected.y);
    CHECK(actual.width == expected.width && actual.height == expected.height);
    CHECK(actual.text == expected.text);
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
        test_terminal_resize(&term, 20, 80); test_terminal_expect(&term, "Reload file");
        int new_x, new_y, reload_x, reload_y, back_x, back_y;
        CHECK(test_terminal_find(&term, "New native", &new_x, &new_y));
        CHECK(test_terminal_find(&term, "Reload file", &reload_x, &reload_y));
        CHECK(test_terminal_find(&term, "[ Back ]", &back_x, &back_y));
        CHECK(new_y == reload_y && reload_y == back_y);
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
            const char *const endings[] = {"expansion.", "Profile options.", "profile options."};
            int note_x, note_y, review_x, review_y, save_x, save_y;
            test_terminal_expect(&term, endings[i]);
            CHECK(test_terminal_find(&term, endings[i], &note_x, &note_y));
            CHECK(test_terminal_find(&term, "[ Review session ]", &review_x, &review_y));
            CHECK(test_terminal_find(&term, "[ Save profile ]", &save_x, &save_y));
            CHECK(review_y == note_y + 2 && save_y == review_y + 2);
        }
        if (i == 0) {
            if (!demo) {
                test_terminal_expect(&term, "Environment (NAME=value)");
                test_terminal_resize(&term, 20, 80); test_terminal_send(&term, "\t\t\t\t");
                test_terminal_expect(&term, "Environment (NAME=value)");
                test_terminal_resize(&term, 44, 160);
            }
            int title_x, title_y, action_x, action_y;
            test_terminal_expect(&term, "Linux application");
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
    test_terminal_resize(&term, 20, 80);
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
    test_terminal_resize(&term, 20, 80);
    int field_x, field_y, popup_x, popup_y;
    test_terminal_expect(&term, "Theme       <");
    CHECK(test_terminal_find(&term, "Theme       <", &field_x, &field_y));
    click("Theme       <"); test_terminal_expect(&term, "Choose theme");
    CHECK(test_terminal_find(&term, "Choose theme", &popup_x, &popup_y));
    CHECK(popup_y == field_y + 1 && popup_x == field_x + 14);
    CHECK(strncmp(term.cells[popup_y + 2] + popup_x - 1, "Catppuccin", 10) == 0);
    CHECK(term.inverse[popup_y + 2][popup_x - 1] || term.underline[popup_y + 2][popup_x - 1]);
    CHECK(!test_terminal_find(&term, "Zenburn", NULL, NULL)); /* Bounded dropdown, not a full catalog card. */
    click("[ Save ]"); CHECK(!test_terminal_find(&term, "Choose theme", NULL, NULL));
    test_terminal_expect(&term, "Themes"); /* Outside click dismisses without activating Save. */
    click("Theme       <"); test_terminal_expect(&term, "Choose theme");
    CHECK(test_terminal_find(&term, "Choose theme", &popup_x, &popup_y));
    int bar_x, thumb_y;
    test_terminal_expect_scrollbar(&term, true);
    CHECK(test_terminal_find_scrollbar(&term, true, &bar_x, &thumb_y));
    mouse_at(0, bar_x, popup_y + 8, false); mouse_at(0, bar_x, popup_y + 8, true);
    test_terminal_expect(&term, "Solarized");
    CHECK(test_terminal_find_scrollbar(&term, true, &bar_x, &thumb_y));
    mouse_at(0, bar_x, thumb_y, false);
    mouse_at(32, bar_x + 2, popup_y + 8, false); mouse_at(0, bar_x + 2, popup_y + 8, true);
    test_terminal_expect(&term, "Zenburn");
    CHECK(test_terminal_find_scrollbar(&term, true, &bar_x, &thumb_y));
    mouse_at(0, bar_x, thumb_y, false);
    mouse_at(32, bar_x, popup_y + 1, false); mouse_at(0, bar_x, popup_y + 1, true);
    test_terminal_expect(&term, "Ashen");
    back(); test_terminal_expect(&term, "< Catppuccin >"); /* Dragging never changes the draft. */
    click("Theme       <"); test_terminal_expect(&term, "Choose theme");
    test_terminal_send(&term, "\033OH"); click("Modus");
    CHECK(test_terminal_find(&term, "Variant", &field_x, &field_y));
    click("Variant"); test_terminal_expect(&term, "Choose variant");
    CHECK(test_terminal_find(&term, "Choose variant", &popup_x, &popup_y));
    CHECK(popup_y < field_y); /* Long Variant list opens upward at the minimum size. */
    test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "Choose variant");
    test_terminal_send(&term, "\033OB\r"); test_terminal_expect(&term, "< vivendi >");
    click("Theme       <"); test_terminal_send(&term, "\033OH"); click("Catppuccin");
    test_terminal_resize(&term, 20, 80);
    click("Theme       <"); test_terminal_expect(&term, "Choose theme"); CHECK(test_terminal_find_scrollbar(&term, true, NULL, NULL));
    test_terminal_send(&term, "\033[<65;25;11M\r"); test_terminal_expect(&term, "< Catppuccin >");
    test_terminal_send(&term, " "); test_terminal_expect(&term, "Choose theme");
    test_terminal_send(&term, "\033[6~"); click("Solarized"); test_terminal_expect(&term, "< Solarized >");
    click("Theme       <"); test_terminal_expect(&term, "Choose theme");
    test_terminal_send(&term, "\033OH"); click("Catppuccin"); test_terminal_expect(&term, "< macchiato >");
    click("Variant"); test_terminal_expect(&term, "Choose variant");
    test_terminal_send(&term, "\033[<0;1;1M\033[<0;1;1m"); test_terminal_expect(&term, "< macchiato >");
    CHECK(!test_terminal_find(&term, "Choose variant", NULL, NULL));
    test_terminal_resize(&term, 44, 160);
    click("Variant"); test_terminal_expect(&term, "Choose variant");
    test_terminal_send(&term, "\033OB\r"); test_terminal_expect(&term, "< mocha >");
    click("Theme       <"); test_terminal_expect(&term, "Choose theme");
    test_terminal_send(&term, "\r"); test_terminal_expect(&term, "< mocha >");
    click("Theme       <"); test_terminal_expect(&term, "Choose theme");
    test_terminal_send(&term, "\033OB\033"); test_terminal_expect(&term, "< Catppuccin >");
    cycle("Variant", false); test_terminal_expect(&term, "< macchiato >");
    cycle("Variant", true); test_terminal_expect(&term, "< mocha >");
    cycle("< mocha >", false); test_terminal_expect(&term, "< macchiato >");
    cycle("Variant", true); test_terminal_expect(&term, "< mocha >");
    click("[ Preview ]"); test_terminal_expect(&term, "THEME PREVIEW | Catppuccin / mocha");
    test_terminal_expect(&term, "simulated data"); test_terminal_expect(&term, "Source");
    back(); test_terminal_expect(&term, "< mocha >"); back(); back();
    open_themes(); test_terminal_expect(&term, "< macchiato >"); /* Back discarded preview. */
    const char *names[] = {"Ashen", "Catppuccin", "Dracula", "Everforest", "Gruber Darker", "Gruvbox Material", "Kanagawa", "Modus", "Monokai Pro", "Moonfly", "Nightfox", "Nord", "One Dark", "Rose Pine", "Solarized", "Tokyo Night", "Zenburn"};
    const int counts[] = {2, 4, 2, 6, 1, 6, 3, 8, 7, 1, 7, 2, 7, 3, 2, 4, 1};
    static const int geometry_sizes[][2] = {{55, 256}, {54, 160}, {44, 160}, {32, 160}, {20, 80}};
    PreviewSource_t source_geometry[5] = {0};
    cycle("Theme       <", false); /* Start the full A–Z catalog at Ashen. */
    for (int family = 0; family < 17; ++family) {
        char label[80]; (void)snprintf(label, sizeof label, "< %s >", names[family]); test_terminal_expect(&term, label);
        for (int variant = 0; variant < counts[family]; ++variant) {
            click("[ Preview ]"); test_terminal_expect(&term, "THEME PREVIEW");
            if (family == 0) {
                for (size_t size = 0; size < sizeof geometry_sizes / sizeof geometry_sizes[0]; ++size) {
                    test_terminal_resize(&term, geometry_sizes[size][0], geometry_sizes[size][1]);
                    test_terminal_expect(&term, "main.c | UTF-8 | C");
                    if (variant == 0) { source_geometry[size] = preview_source(); }
                    else { same_source(source_geometry[size]); }
                }
                test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "main.c | UTF-8 | C");
            }
            same_source(source_geometry[2]);
            if (family == 0 && variant == 1) {
                int source_x, source_y;
                CHECK(test_terminal_find(&term, "Source", &source_x, &source_y));
                CHECK(border_glyph(source_y - 1, source_x - 1, '+', 'l'));
                CHECK(border_glyph(source_y - 1, source_x, '-', 'q'));
                CHECK(border_glyph(source_y + 2, source_x - 1, '|', 'x'));
                test_terminal_resize(&term, 32, 160);
                test_terminal_expect(&term, "HEX bytes / ASCII");
            }
            test_terminal_resize(&term, 20, 80); test_terminal_expect(&term, "THEME PREVIEW");
            back(); test_terminal_expect(&term, "Variant");
            test_terminal_resize(&term, 44, 160); cycle("Variant", true);
        }
        cycle("Theme       <", true);
    }
    test_terminal_expect(&term, "< Ashen >"); cycle("Theme       <", true);
    char path[512]; (void)snprintf(path, sizeof path, "%s/the-bugger/preferences.json", root);
    CHECK(access(path, F_OK) != 0); /* Every preview so far remained unsaved. */
    test_terminal_expect(&term, "< macchiato >"); cycle("Variant", true); click("[ Save ]");
    test_terminal_expect(&term, "Configuration"); footer("Theme saved.");
    click("Themes"); footer("Preview a theme and variant.");
    CHECK(!test_terminal_find(&term, "Theme saved.", NULL, NULL));
    back(); footer("Choose personal");
    test_terminal_stop(&term, 0); active = false;
    start(argv[1], "vt100", true);
    test_terminal_send(&term, "\t\t\t\r"); test_terminal_expect(&term, "Configuration");
    test_terminal_send(&term, "\t\r"); test_terminal_expect(&term, "< mocha >");
    /* Keyboard changes theme and the dependent variant, including backward wrap. */
    test_terminal_send(&term, "\033OD"); test_terminal_expect(&term, "< Ashen >");
    test_terminal_send(&term, "\033OD"); test_terminal_expect(&term, "< Zenburn >");
    test_terminal_send(&term, "\033OD\033OD"); test_terminal_expect(&term, "< Solarized >");
    test_terminal_send(&term, "\t\033OD"); test_terminal_expect(&term, "< light >");
    test_terminal_resize(&term, 20, 80); test_terminal_expect(&term, "Variant");
    /* Resize retains Variant focus; Back then Preview are the next two controls. */
    int variant_x, variant_y; CHECK(test_terminal_find(&term, "Variant", &variant_x, &variant_y));
    CHECK(term.inverse[variant_y][variant_x]);
    test_terminal_send(&term, "\t\t\r"); test_terminal_expect(&term, "THEME PREVIEW"); back();
    test_terminal_stop(&term, 0); active = false;
    start(argv[1], "xterm-256color", false); open_themes();
    cycle("Theme       <", false); cycle("Variant", true);
    test_terminal_expect(&term, "< high-contrast >"); click("[ Save ]");
    test_terminal_stop(&term, 0); active = false;
    char *const demo_args[] = {argv[1], "--demo", NULL};
    test_terminal_start_argv(&term, demo_args, "xterm-256color", false); active = true;
    test_terminal_send(&term, "\r\t\t\t\t\t\r"); test_terminal_expect(&term, "Review session preview");
    test_terminal_send(&term, "\r"); test_terminal_expect(&term, "Source");
    int source_x, source_y;
    CHECK(test_terminal_find(&term, "Source", &source_x, &source_y));
    CHECK(border_glyph(source_y - 1, source_x - 3, '+', 'l'));
    CHECK(strncmp(term.cells[source_y - 1] + 1, "Linux application", 17) == 0);
    CHECK(border_glyph(source_y - 1, source_x + 20, '-', 'q'));
    CHECK(border_glyph(source_y + 2, source_x - 3, '|', 'x'));
    click("Symbols"); click("Source");
    test_terminal_resize(&term, 20, 80); test_terminal_expect(&term, "Source");
    test_terminal_resize(&term, 44, 160); test_terminal_expect(&term, "Source");
    CHECK(test_terminal_find(&term, "Source", &source_x, &source_y));
    CHECK(border_glyph(source_y - 1, source_x - 3, '+', 'l'));
    test_terminal_stop(&term, 0); active = false;
    CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof path, "%s/the-bugger/.preferences.lock", root); CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof path, "%s/the-bugger", root); CHECK(rmdir(path) == 0 && rmdir(root) == 0);
    puts("PASS Config/Themes: centered menus, 66 variant previews, cancel/save/reload, dependent variant selection, keyboard/mouse, compact and monochrome");
    return EXIT_SUCCESS;
}
