#ifndef THE_BUGGER_THEME_H
#define THE_BUGGER_THEME_H

#include <curses.h>
#include <stddef.h>

/* Workspace, panel, raised, selection, border, text, muted and six accents. */
typedef struct {
    const char *family, *variant;
    bool light;
    unsigned colors[13];
} TbPalette;
size_t tb_theme_count(void);
size_t tb_theme_family_count(void);
const char *tb_theme_family(size_t family);
const char *tb_theme_name(size_t family);
const TbPalette *tb_theme_palette(size_t index);
size_t tb_theme_current(void);
/* Select without touching curses; tb_theme_init applies the selected palette. */
bool tb_theme_select(size_t index);
/* Unknown names return tb_theme_count(). */
size_t tb_theme_find(const char *family, const char *variant);

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
