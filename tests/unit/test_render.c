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
    cchar_t cell;
    wchar_t glyph[CCHARW_MAX]; attr_t attrs; short pair;
    CHECK(mvin_wch(y, x, &cell) == OK);
    CHECK(getcchar(&cell, glyph, &attrs, &pair, NULL) == OK);
    return pair;
}

static void pane_frames(void)
{
    TbLayout_t layout;
    tb_layout_init(&layout); layout.overlays = true;
    layout.lower_height[0] = 12; layout.lower_height[1] = 9; layout.lower_height[2] = 7;
    for (int mask = 0; mask < 8; ++mask) {
        for (int i = 0; i < 3; ++i) { layout.lower_open[i] = (mask & (1 << i)) != 0; }
        CHECK(tb_layout_arrange(&layout, (TbRect_t){1, 4, 138, 38}, 0));
        TbRect_t frames[TB_MAX_PANES + 3];
        memcpy(frames, layout.panes, sizeof layout.panes);
        size_t count = TB_MAX_PANES;
        for (int i = 0; i < 3; ++i) {
            if (!layout.lower_open[i]) { frames[count++] = layout.lower_bars[i]; }
        }
        tb_fill((TbRect_t){0, 0, 140, 44}, TB_WORKSPACE);
        for (size_t i = 0; i < count; ++i) {
            if (frames[i].w) { tb_text(frames[i].y, frames[i].x, 1, TB_YELLOW, 0, "X"); }
        }
        tb_pane_frames(frames, count, (TbRect_t){0});
        chtype before[44][140];
        for (int y = 0; y < 44; ++y) {
            for (int x = 0; x < 140; ++x) { before[y][x] = mvinch(y, x); }
        }
        for (size_t i = 0; i < count; ++i) {
            TbRect_t r = frames[i];
            if (!r.w) { continue; }
            CHECK((mvinch(r.y, r.x) & A_CHARTEXT) == 'X');
            for (int x = r.x; x < r.x + r.w; ++x) {
                CHECK((mvinch(r.y - 1, x) & A_CHARTEXT) == (ACS_HLINE & A_CHARTEXT));
                CHECK((mvinch(r.y + r.h, x) & A_CHARTEXT) == (ACS_HLINE & A_CHARTEXT));
            }
        }
        for (int i = 0; i < 2; ++i) {
            int x = layout.panes[i].x + layout.panes[i].w;
            for (int y = 4; y < 42; ++y) {
                chtype cell = mvinch(y, x) & A_CHARTEXT;
                CHECK(cell == (ACS_VLINE & A_CHARTEXT) || cell == (ACS_LTEE & A_CHARTEXT) ||
                      cell == (ACS_RTEE & A_CHARTEXT) || cell == (ACS_PLUS & A_CHARTEXT));
            }
        }
        for (size_t i = 0; i < count / 2; ++i) {
            TbRect_t r = frames[i]; frames[i] = frames[count - i - 1]; frames[count - i - 1] = r;
        }
        tb_pane_frames(frames, count, (TbRect_t){0});
        for (int y = 0; y < 44; ++y) {
            for (int x = 0; x < 140; ++x) { CHECK(before[y][x] == mvinch(y, x)); }
        }
    }
}

static void scrollbar_tracks(void)
{
    for (int width = 3; width < 100; ++width) {
        TbRect_t frame = {3, 4, width, 17};
        TbRect_t h = tb_scrollbar_track(frame, true), v = tb_scrollbar_track(frame, false);
        CHECK(h.x - frame.x == frame.x + frame.w - h.x - h.w);
        CHECK(v.y - frame.y == frame.y + frame.h - v.y - v.h);
        CHECK(h.x == frame.x + 1 && h.w == width - 2 && h.y == frame.y + frame.h - 2);
        CHECK(v.x == frame.x + frame.w - 2 && v.y == frame.y + 1 && v.h == 15);
        TbScroll_t scroll = {.total = 200, .visible = 23, .offset = 177, .maximum = 177};
        scroll = tb_scrollbar_layout(scroll, h.w);
        CHECK(scroll.visible == 23 && scroll.offset == 177 && scroll.maximum == 177);
        CHECK(scroll.thumb_size >= 1 && scroll.thumb_start + scroll.thumb_size == h.w);
        scroll.offset = 0; scroll = tb_scrollbar_layout(scroll, h.w);
        CHECK(scroll.thumb_start == 0);
    }
    CHECK(tb_scrollbar_track((TbRect_t){0, 0, 2, 10}, false).h == 0);
}


static void source_slices(void)
{
    const char *const samples[] = {
        "volatile int value = 123; // return 456", "#define VALUE 123", "return function(123);",
        "const char *value = \"string with return 123\";"
    };
    for (int selected = 0; selected < 2; ++selected) {
        for (size_t i = 0; i < sizeof samples / sizeof samples[0]; ++i) {
            size_t length = strlen(samples[i]);
            tb_fill((TbRect_t){0, 0, 140, 2}, TB_PANEL);
            tb_render_source_text(0, 0, 100, samples[i], selected);
            chtype reference[100];
            for (size_t j = 0; j < length; ++j) { reference[j] = mvinch(0, (int)j); }
            for (size_t offset = 0; offset < length; ++offset) {
                tb_fill((TbRect_t){0, 1, 100, 1}, TB_PANEL);
                tb_render_source_slice(1, 0, 17, samples[i], (int)offset, selected);
                for (size_t j = 0; j < 17 && j + offset < length; ++j) {
                    CHECK(mvinch(1, (int)j) == reference[offset + j]);
                }
            }
        }
    }
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
    TbAssemblyColumns_t columns = {18, 44, 7};
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
    TbRect_t bounds = {1, 1, 138, 41};
    TbRect_t down = tb_dropdown_rect((TbRect_t){10, 3, 12, 1}, bounds, 30, 17);
    CHECK(down.x == 10 && down.y == 4 && down.w == 30 && down.h == 10);
    TbRect_t up = tb_dropdown_rect((TbRect_t){130, 39, 8, 1}, bounds, 30, 17);
    CHECK(up.x == 109 && up.y == 29 && up.h == 10);
    TbRect_t short_list = tb_dropdown_rect((TbRect_t){2, 8, 8, 1}, (TbRect_t){1, 1, 78, 14}, 100, 17);
    CHECK(short_list.x == 1 && short_list.w == 78 && short_list.y >= 1 && short_list.y + short_list.h <= 15);
    CHECK(short_list.h < 10 && short_list.h >= 3);
    CHECK(tb_dropdown_rect((TbRect_t){0}, (TbRect_t){0}, 30, 5).h == 0);
    text_bounds();
    source_slices();
    scrollbar_tracks();
    pane_frames();
    int default_fg, default_bg;
    CHECK(extended_pair_content(TB_PANEL, &default_fg, &default_bg) == OK);
    CHECK(default_fg == 0xcad3f5 && default_bg == 0x24273a);
    CHECK(tb_theme_count() == 66 && tb_theme_family_count() == 17);
    const int scrollbar_colors[] = {7, 11, 11, 8, 8, 9, 11, 6, 7, 11, 11, 12, 12, 9, 6, 11, 8};
    for (size_t family = 0; family < tb_theme_family_count(); ++family) {
        CHECK(tb_theme_family(family) && tb_theme_name(family));
        if (family) { CHECK(strcmp(tb_theme_name(family - 1), tb_theme_name(family)) < 0); }
        size_t variants = 0;
        for (size_t i = 0; i < tb_theme_count(); ++i) {
            variants += strcmp(tb_theme_palette(i)->family, tb_theme_family(family)) == 0;
        }
        CHECK(variants > 0);
    }
    CHECK(tb_theme_palette(tb_theme_find("ashen", "high-contrast"))->colors[0] == 0);
    CHECK(tb_theme_palette(tb_theme_find("ashen", "high-contrast"))->colors[1] == 0);
    CHECK(tb_theme_palette(tb_theme_find("ashen", "high-contrast"))->colors[4] == 0xdf6464);
    for (size_t i = 0; i < tb_theme_count(); ++i) {
        const TbPalette_t *palette = tb_theme_palette(i);
        CHECK(tb_theme_find(palette->family, palette->variant) == i);
        for (int surface = 0; surface < 4; ++surface) {
            CHECK(contrast(palette->colors[5], palette->colors[surface]) >= 4.5);
            for (int role = 6; role < 13; ++role) { CHECK(contrast(palette->colors[role], palette->colors[surface]) >= 3.0); }
        }
        CHECK(tb_theme_select(i) && strcmp(tb_theme_init(), "truecolor") == 0);
        CHECK(extended_pair_content(TB_SCROLL_TRACK, &default_fg, &default_bg) == OK);
        CHECK((unsigned)default_bg == palette->colors[2]);
        CHECK(extended_pair_content(TB_SCROLL_THUMB, &default_fg, &default_bg) == OK);
        size_t family = 0;
        while (family < tb_theme_family_count() && strcmp(palette->family, tb_theme_family(family)) != 0) { ++family; }
        CHECK(family < sizeof scrollbar_colors / sizeof scrollbar_colors[0]);
        CHECK((unsigned)default_bg == palette->colors[scrollbar_colors[family]]);
        CHECK(contrast(palette->colors[scrollbar_colors[family]], palette->colors[2]) >= 3.0);
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
    /* Color-deficiency variants retain their upstream semantic remapping. */
    for (size_t light = 0; light < 2; ++light) {
        const char *deut = light ? "operandi-deuteranopia" : "vivendi-deuteranopia";
        const char *trit = light ? "operandi-tritanopia" : "vivendi-tritanopia";
        const TbPalette_t *d = tb_theme_palette(tb_theme_find("modus", deut));
        const TbPalette_t *t = tb_theme_palette(tb_theme_find("modus", trit));
        CHECK(d && t && d->colors[7] != t->colors[7] && d->colors[8] != t->colors[8]);
        CHECK(d->colors[8] == (light ? 0x0031a9U : 0x2fafffU));
        CHECK(t->colors[8] == (light ? 0x005e8bU : 0x00d3d0U));
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
    TbDemo_t demo;
    tb_demo_init(&demo);
    demo.panes[0].scroll[TB_SOURCE] = 16;
    TbRect_t rect = {1, 4, 49, 10};
    tb_fill(rect, TB_PANEL);
    tb_render_demo(&demo, 0, rect);
    /* Initial source row is main: line number, type and function are distinct. */
    CHECK(pair_at(6, 4) == TB_MUTED);
    CHECK(pair_at(6, 8) == TB_CYAN);
    CHECK(pair_at(6, 12) == TB_GREEN);
    CHECK(pair_at(7, 3) == (int)tb_selected_style(TB_YELLOW));
    CHECK(pair_at(7, 47) == TB_SELECTED); /* selection stops before the narrow bar */
    CHECK(pair_at(13, 2) == TB_FOOTER);
    CHECK(pair_at(6, 48) == TB_SCROLL_TRACK); /* one-cell track matches horizontal styling */
    CHECK(pair_at(11, 48) == TB_SCROLL_THUMB); /* thumb near the end of the source */
    tb_demo_action(&demo, TB_NEXT);
    tb_fill(rect, TB_PANEL);
    tb_render_demo(&demo, 0, rect);
    CHECK(pair_at(7, 47) == TB_PANEL); /* previous PC is not a navigation highlight */
    CHECK(pair_at(8, 47) == TB_SELECTED);
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
    tb_border((TbRect_t){60, 20, 10, 4}, TB_BORDER, false);
    CHECK((mvinch(20, 60) & A_CHARTEXT) == (ACS_ULCORNER & A_CHARTEXT));
    CHECK((mvinch(20, 69) & A_CHARTEXT) == (ACS_URCORNER & A_CHARTEXT));
    CHECK((mvinch(23, 60) & A_CHARTEXT) == (ACS_LLCORNER & A_CHARTEXT));
    CHECK((mvinch(23, 69) & A_CHARTEXT) == (ACS_LRCORNER & A_CHARTEXT));
    for (int x = 61; x < 69; ++x) { CHECK((mvinch(20, x) & A_CHARTEXT) == (ACS_HLINE & A_CHARTEXT)); }
    for (int y = 21; y < 23; ++y) { CHECK((mvinch(y, 60) & A_CHARTEXT) == (ACS_VLINE & A_CHARTEXT)); }
    tb_border((TbRect_t){69, 20, 10, 4}, TB_BORDER, false);
    CHECK((mvinch(20, 69) & A_CHARTEXT) == (ACS_TTEE & A_CHARTEXT));
    CHECK((mvinch(23, 69) & A_CHARTEXT) == (ACS_BTEE & A_CHARTEXT));
    tb_border((TbRect_t){60, 23, 10, 4}, TB_BORDER, false);
    CHECK((mvinch(23, 69) & A_CHARTEXT) == (ACS_PLUS & A_CHARTEXT));
    tb_text(20, 60, 1, TB_YELLOW, 0, "X");
    tb_border((TbRect_t){60, 20, 10, 4}, TB_BORDER, true);
    CHECK((mvinch(20, 60) & A_CHARTEXT) == 'X');
    CHECK(pair_at(20, 60) == TB_YELLOW);
    tb_fill((TbRect_t){61, 20, 1, 1}, TB_SURFACE);
    tb_border((TbRect_t){60, 20, 10, 4}, TB_BORDER, true);
    CHECK(pair_at(20, 61) == TB_SURFACE); /* Preserve blank control padding too. */
    TbScroll_t bar = {.total = 100, .visible = 10, .offset = 45, .maximum = 90, .thumb_start = 4, .thumb_size = 2};
    TbRect_t horizontal_bar = {60, 30, 10, 1};
    tb_fill(horizontal_bar, TB_PANEL);
    tb_render_scrollbar(horizontal_bar, bar, true);
    for (int col = 0; col < 10; ++col) {
        CHECK(pair_at(30, 60 + col) == (col >= 4 && col < 6 ? TB_SCROLL_H_THUMB : TB_SCROLL_H_TRACK));
        cchar_t block; wchar_t glyph[CCHARW_MAX]; attr_t block_attrs; short block_pair;
        CHECK(mvin_wch(30, 60 + col, &block) == OK);
        CHECK(getcchar(&block, glyph, &block_attrs, &block_pair, NULL) == OK);
        CHECK(glyph[0] == L'\u2584');
    }
    TbRect_t vertical_bar = {72, 25, 1, 10};
    tb_render_scrollbar(vertical_bar, bar, false);
    for (int row = 0; row < 10; ++row) {
        CHECK(pair_at(25 + row, 72) == (row >= 4 && row < 6 ? TB_SCROLL_THUMB : TB_SCROLL_TRACK));
    }
    TbScroll_t scaled = {.total = 100, .visible = 10, .offset = 90, .maximum = 90};
    tb_render_scrollbar((TbRect_t){60, 32, 20, 1}, scaled, true);
    CHECK(pair_at(32, 77) == TB_SCROLL_H_TRACK && pair_at(32, 79) == TB_SCROLL_H_THUMB);
    tb_render_scrollbar((TbRect_t){82, 20, 1, 12}, scaled, false);
    CHECK(pair_at(30, 82) == TB_SCROLL_TRACK && pair_at(31, 82) == TB_SCROLL_THUMB);
    bar.maximum = 0;
    tb_fill(horizontal_bar, TB_PANEL);
    tb_render_scrollbar(horizontal_bar, bar, true);
    CHECK(pair_at(30, 64) == TB_PANEL);
    bar.maximum = 1; bar.visible = 1; bar.thumb_start = 0; bar.thumb_size = 1;
    tb_render_scrollbar((TbRect_t){75, 30, 1, 1}, bar, true);
    CHECK(pair_at(30, 75) == TB_SCROLL_H_THUMB);
    tb_render_scrollbar((TbRect_t){-1, 30, 1, 1}, bar, true);
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
    tb_render_scrollbar((TbRect_t){75, 30, 1, 1}, bar, true);
    CHECK(pair_at(30, 75) == TB_SCROLL_THUMB);
    CHECK((mvinch(30, 75) & A_CHARTEXT) == ' ');
    CHECK(setlocale(LC_CTYPE, "C.UTF-8") != NULL);
    CHECK(setenv("NO_COLOR", "1", 1) == 0);
    CHECK(strcmp(tb_theme_init(), "monochrome") == 0);
    CHECK(!tb_theme_has_color());
    bar = (TbScroll_t){.total = 100, .visible = 10, .maximum = 90, .thumb_start = 4, .thumb_size = 2};
    tb_render_scrollbar(horizontal_bar, bar, true);
    CHECK((mvinch(30, 64) & A_REVERSE) != 0);
    CHECK((mvinch(30, 61) & A_REVERSE) == 0);

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
                int track_fg, track_bg, thumb_fg, thumb_bg;
                CHECK(extended_pair_content(TB_SCROLL_TRACK, &track_fg, &track_bg) == OK);
                CHECK(extended_pair_content(TB_SCROLL_THUMB, &thumb_fg, &thumb_bg) == OK);
                CHECK(track_bg != thumb_bg);
                CHECK(extended_pair_content(TB_SCROLL_H_TRACK, &track_fg, &track_bg) == OK);
                CHECK(extended_pair_content(TB_SCROLL_H_THUMB, &thumb_fg, &thumb_bg) == OK);
                CHECK(track_fg != thumb_fg && track_bg == thumb_bg);
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
    puts("PASS cell/control/UTF-8 text bounds, 66 palettes, contrast, direct/256/8-color/monochrome, execution highlight and scrollbar");
    return EXIT_SUCCESS;
}
