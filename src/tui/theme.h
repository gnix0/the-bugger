#ifndef THE_BUGGER_THEME_H
#define THE_BUGGER_THEME_H

#include <curses.h>

typedef enum {
    TB_PANEL = 1, TB_SURFACE, TB_MUTED, TB_PINK, TB_GREEN, TB_ORANGE,
    TB_YELLOW, TB_PURPLE, TB_CYAN, TB_SELECTED, TB_BORDER,
    TB_WORKSPACE, TB_CHROME, TB_TAB, TB_ACTIVE_TAB, TB_FOOTER, TB_NOTICE,
    TB_BRAND, TB_TARGET, TB_STATE, TB_KEY, TB_COLOR_COUNT
} TbStyle;

/* Uses curses capabilities, without redefining the terminal's palette. */
const char *tb_theme_init(void);
void tb_style(TbStyle style, attr_t attributes);
TbStyle tb_selected_style(TbStyle style);

#endif
