#include "render.h"
#include "../support/check.h"

#include <math.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int pair_at(int y, int x)
{
    return PAIR_NUMBER(mvinch(y, x));
}

static void text_bounds(void)
{
    /* A terminal row's cell budget must also constrain curses' expansion of
     * control characters: a byte limit alone lets tabs/newlines escape it. */
    const char *const samples[] = {"A\tB", "A\nB", "A\rB", "A\033B", "A\177B"};
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
        CHECK(mvhline(0, 0, '.', COLS) == OK);
        CHECK(mvhline(1, 0, '.', COLS) == OK);
        tb_text(0, 2, 4, TB_PANEL, 0, samples[i]);
        for (int x = 0; x < COLS; ++x) {
            if (x < 2 || x >= 6) { CHECK((mvinch(0, x) & A_CHARTEXT) == '.'); }
            CHECK((mvinch(1, x) & A_CHARTEXT) == '.');
        }
    }
    CHECK(mvhline(0, 0, '.', COLS) == OK);
    tb_text(0, 2, 1, TB_CYAN, 0, "\xc3\xa9Z");
    cchar_t cell;
    wchar_t characters[CCHARW_MAX];
    attr_t attributes;
    short pair;
    CHECK(mvin_wch(0, 2, &cell) == OK);
    CHECK(getcchar(&cell, characters, &attributes, &pair, NULL) == OK);
    CHECK(characters[0] == L'\u00e9' && pair == TB_CYAN);
    CHECK((mvinch(0, 3) & A_CHARTEXT) == '.');
    tb_text(0, 5, 1, TB_PANEL, 0, "\xe7\x95\x8cZ");
    CHECK((mvinch(0, 5) & A_CHARTEXT) == '.');
    tb_text(0, 5, 2, TB_PANEL, 0, "\xe7\x95\x8cZ");
    CHECK(mvin_wch(0, 5, &cell) == OK);
    CHECK(getcchar(&cell, characters, &attributes, &pair, NULL) == OK);
    CHECK(characters[0] == L'\u754c' && (mvinch(0, 7) & A_CHARTEXT) == '.');
    tb_text(0, 9, 2, TB_PANEL, 0, "e\xcc\x81Z");
    CHECK(mvin_wch(0, 9, &cell) == OK);
    CHECK(getcchar(&cell, characters, &attributes, &pair, NULL) == OK);
    CHECK(characters[0] == L'e' && characters[1] == L'\u0301');
    CHECK((mvinch(0, 10) & A_CHARTEXT) == 'Z');
    tb_text(0, 12, 5, TB_PANEL, 0, "\xffZ");
    char escaped[6]; CHECK(mvinnstr(0, 12, escaped, 5) == 5);
    CHECK(strcmp(escaped, "\\xffZ") == 0);
    CHECK((mvinch(0, 17) & A_CHARTEXT) == '.');
    tb_text(0, 19, 2, TB_PANEL, 0, "\xcc\x81Z");
    CHECK((mvinch(0, 18) & A_CHARTEXT) == '.');
    CHECK((mvinch(0, 21) & A_CHARTEXT) == '.');
    CHECK(mvhline(LINES - 1, 0, '.', COLS) == OK);
    tb_text(LINES - 1, COLS - 2, 8, TB_PANEL, 0, "ABC");
    CHECK((mvinch(LINES - 1, COLS - 2) & A_CHARTEXT) == 'A');
    CHECK((mvinch(LINES - 1, COLS - 1) & A_CHARTEXT) == '.');
}

static double luminance(unsigned rgb)
{
    double result = 0;
    const double weights[] = {0.2126, 0.7152, 0.0722};
    for (int i = 0; i < 3; ++i) {
        double value = (double)((rgb >> (unsigned)(16 - i * 8)) & 255U) / 255.0;
        result += weights[i] * (value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4));
    }
    return result;
}
static double contrast(unsigned foreground, unsigned background)
{
    double fg = luminance(foreground), bg = luminance(background);
    return fg > bg ? (fg + 0.05) / (bg + 0.05) : (bg + 0.05) / (fg + 0.05);
}

int main(void)
{
    CHECK(setlocale(LC_ALL, "C.UTF-8") != NULL);
    TbAssemblyColumns columns = {18, 44, 7};
    char short_line[256], long_line[256], tiny[8];
    tb_format_assembly(short_line, sizeof short_line, columns, "0x0000000000001000", "55", "push   %rbp");
    tb_format_assembly(long_line, sizeof long_line, columns, "0x0000000000001001", "48 89 e5", "mov\t%rsp,%rbp");
    CHECK(strstr(short_line, "push") - short_line == strstr(long_line, "mov") - long_line);
    CHECK(strstr(short_line, "%rbp") - short_line == strstr(long_line, "%rsp") - long_line);
    tb_format_assembly(tiny, sizeof tiny, columns, "0x0000000000001000", "55", "push %rbp");
    CHECK(tiny[sizeof tiny - 1] == 0);
    FILE *input = tmpfile(), *output = tmpfile();
    CHECK(input != NULL && output != NULL);
    CHECK(unsetenv("NO_COLOR") == 0);
    SCREEN *screen = newterm("xterm-direct", output, input);
    CHECK(screen != NULL);
    CHECK(resizeterm(44, 140) == OK);
    CHECK(strcmp(tb_theme_palette(tb_theme_current())->variant, "macchiato") == 0);
    CHECK(strcmp(tb_theme_init(), "truecolor") == 0);
    text_bounds();
    int default_fg, default_bg;
    CHECK(extended_pair_content(TB_PANEL, &default_fg, &default_bg) == OK);
    CHECK(default_fg == 0xcad3f5 && default_bg == 0x24273a);
    CHECK(tb_theme_count() == 37 && tb_theme_family_count() == 7);
    for (size_t i = 0; i < tb_theme_count(); ++i) {
        const TbPalette *palette = tb_theme_palette(i);
        CHECK(tb_theme_find(palette->family, palette->variant) == i);
        for (int surface = 0; surface < 4; ++surface) {
            CHECK(contrast(palette->colors[5], palette->colors[surface]) >= 4.5);
            for (int role = 6; role < 13; ++role) { CHECK(contrast(palette->colors[role], palette->colors[surface]) >= 3.0); }
        }
        CHECK(tb_theme_select(i) && strcmp(tb_theme_init(), "truecolor") == 0);
        for (int role = TB_STATUS_TEXT; role <= TB_INSERT_TEXT; ++role) {
            CHECK(extended_pair_content(role, &default_fg, &default_bg) == OK);
            CHECK((unsigned)default_bg == palette->colors[0]);
        }
        for (int pair_id = 1; pair_id < 2 * TB_COLOR_COUNT; ++pair_id) {
            if (pair_id == TB_COLOR_COUNT) { continue; } /* No style uses selected pair zero. */
            CHECK(extended_pair_content(pair_id, &default_fg, &default_bg) == OK);
            CHECK(default_fg >= 0 && default_fg <= 0xffffff && default_bg >= 0 && default_bg <= 0xffffff);
            CHECK(default_fg != default_bg);
        }
    }
    CHECK(!tb_theme_select(tb_theme_count()));
    CHECK(tb_theme_find("unknown", "unknown") == tb_theme_count());
    CHECK(tb_theme_select(tb_theme_find("monokai-pro", "pro")));
    CHECK(strcmp(tb_theme_init(), "truecolor") == 0);
    int fg, bg;
    CHECK(extended_pair_content(TB_WORKSPACE, &fg, &bg) == OK && bg == 0x221f22);
    CHECK(extended_pair_content(TB_PANEL, &fg, &bg) == OK && bg == 0x2d2a2e);
    CHECK(extended_pair_content(TB_SURFACE, &fg, &bg) == OK && bg == 0x353236);
    CHECK(extended_pair_content(TB_ACTIVE_TAB, &fg, &bg) == OK && fg == 0xff6188);
    CHECK(extended_pair_content(tb_selected_style(TB_CYAN), &fg, &bg) == OK);
    CHECK(fg == 0x78dce8 && bg == 0x4b3d4b);
    TbDemo demo;
    tb_demo_init(&demo);
    demo.panes[0].scroll[TB_SOURCE] = 16;
    TbRect rect = {1, 4, 49, 10};
    tb_fill(rect, TB_PANEL);
    tb_render_demo(&demo, 0, rect);
    /* Initial source row is main: line number, type and function are distinct. */
    CHECK(pair_at(6, 4) == TB_MUTED);
    CHECK(pair_at(6, 8) == TB_CYAN);
    CHECK(pair_at(6, 12) == TB_GREEN);
    CHECK(pair_at(7, 3) == (int)tb_selected_style(TB_YELLOW));
    CHECK(pair_at(7, 48) == TB_SELECTED); /* selection stops before the narrow bar */
    CHECK(pair_at(13, 2) == TB_FOOTER);
    CHECK(pair_at(6, 49) == TB_MUTED); /* one-cell track matches horizontal styling */
    CHECK(pair_at(11, 49) == TB_CYAN); /* thumb near the end of the source */
    tb_demo_action(&demo, TB_NEXT);
    tb_fill(rect, TB_PANEL);
    tb_render_demo(&demo, 0, rect);
    CHECK(pair_at(7, 48) == TB_PANEL); /* previous PC is not a navigation highlight */
    CHECK(pair_at(8, 48) == TB_SELECTED);
    tb_demo_action(&demo, TB_RESET);
    demo.pane = 2;
    demo.panes[2].row[TB_REGISTERS] = 0;
    tb_fill(rect, TB_PANEL);
    tb_render_demo(&demo, 2, rect);
    CHECK(pair_at(6, 2) == TB_CYAN);
    CHECK(pair_at(6, 11) == TB_GREEN);
    tb_demo_action(&demo, TB_NEXT);
    tb_render_demo(&demo, 2, rect);
    CHECK(pair_at(6, 11) == TB_YELLOW);
    CHECK(setenv("NO_COLOR", "1", 1) == 0);
    CHECK(strcmp(tb_theme_init(), "monochrome") == 0);
    tb_style(TB_ACTIVE_TAB, A_UNDERLINE);
    attr_t attributes;
    short pair;
    CHECK(attr_get(&attributes, &pair, NULL) == OK);
    CHECK((attributes & A_UNDERLINE) != 0 && (attributes & A_REVERSE) != 0);
    (void)endwin();
    delscreen(screen);
    CHECK(fclose(input) == 0 && fclose(output) == 0);
    const char *const terminals[] = {"xterm-256color", "xterm", "vt100"};
    CHECK(unsetenv("NO_COLOR") == 0);
    for (size_t terminal = 0; terminal < sizeof terminals / sizeof terminals[0]; ++terminal) {
        input = tmpfile(); output = tmpfile(); CHECK(input && output);
        screen = newterm(terminals[terminal], output, input); CHECK(screen);
        for (size_t i = 0; i < tb_theme_count(); ++i) {
            CHECK(tb_theme_select(i));
            CHECK(strcmp(tb_theme_init(), terminal == 0 ? "256-color" : terminal == 1 ? "8-color" : "monochrome") == 0);
            if (terminal < 2) {
                for (int pair_id = 1; pair_id < 2 * TB_COLOR_COUNT; ++pair_id) {
                    if (pair_id == TB_COLOR_COUNT) { continue; }
                    CHECK(extended_pair_content(pair_id, &fg, &bg) == OK);
                    CHECK(fg >= 0 && fg < COLORS && bg >= 0 && bg < COLORS);
                    if (fg == bg) { fprintf(stderr, "%s %s/%s pair %d fg %d bg %d\n", terminals[terminal], tb_theme_palette(i)->family, tb_theme_palette(i)->variant, pair_id, fg, bg); }
                    CHECK(fg != bg);
                }
            }
        }
        (void)endwin(); delscreen(screen); CHECK(fclose(input) == 0 && fclose(output) == 0);
    }
    puts("PASS cell/control/UTF-8 text bounds, 37 palettes, contrast, direct/256/8-color/monochrome, execution highlight and scrollbar");
    return EXIT_SUCCESS;
}
