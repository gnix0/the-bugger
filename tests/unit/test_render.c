#include "render.h"
#include "../support/check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pair_at(int y, int x)
{
    return PAIR_NUMBER(mvinch(y, x));
}

int main(void)
{
    FILE *input = tmpfile(), *output = tmpfile();
    CHECK(input != NULL && output != NULL);
    CHECK(unsetenv("NO_COLOR") == 0);
    SCREEN *screen = newterm("xterm-direct", output, input);
    CHECK(screen != NULL);
    CHECK(resizeterm(44, 140) == OK);
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
    TbRect rect = {1, 4, 49, 20};
    tb_fill(rect, TB_PANEL);
    tb_render_demo(&demo, 0, rect);
    /* Initial source row is main: line number, type and function are distinct. */
    CHECK(pair_at(6, 4) == TB_MUTED);
    CHECK(pair_at(6, 8) == TB_CYAN);
    CHECK(pair_at(6, 12) == TB_GREEN);
    CHECK(pair_at(7, 3) == (int)tb_selected_style(TB_YELLOW));
    CHECK(pair_at(7, 48) == TB_SELECTED); /* selection fills the row */
    CHECK(pair_at(23, 2) == TB_FOOTER);
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
    puts("PASS render palette, semantic colors, full-row selection and monochrome tab marker");
    return EXIT_SUCCESS;
}
