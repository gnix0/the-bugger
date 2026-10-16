#include "theme.h"

#include <curses.h>
#include <stdlib.h>

/* Owned by this module for the lifetime of the single curses screen. */
static bool theme_color;

static int color_index(unsigned rgb)
{
    if (COLORS >= 16777216) {
        return (int)rgb;
    }
    if (COLORS >= 256) {
        /* Choose the closest xterm cube or grey entry to the reference RGB. */
        static const int levels[] = {0, 95, 135, 175, 215, 255};
        int best = 0, distance = 200000;
        for (int index = 16; index < 256; ++index) {
            int n = index - 16;
            int r = index < 232 ? levels[n / 36] : 8 + (index - 232) * 10;
            int g = index < 232 ? levels[n / 6 % 6] : r;
            int b = index < 232 ? levels[n % 6] : r;
            int dr = r - (int)(rgb >> 16U), dg = g - (int)(rgb >> 8U & 255U);
            int db = b - (int)(rgb & 255U);
            int d = dr * dr + dg * dg + db * db;
            if (d < distance) {
                distance = d;
                best = index;
            }
        }
        return best;
    }
    return COLOR_WHITE;
}

const char *tb_theme_init(void)
{
    theme_color = false;
    const int foreground[TB_COLOR_COUNT] = {
        0, 0xfcfcfa, 0xfcfcfa, 0x939293, 0xff6188, 0xa9dc76,
        0xfc9867, 0xffd866, 0xab9df2, 0x78dce8, 0xfcfcfa, 0x49454b,
        0xfcfcfa, 0x939293, 0x939293, 0xff6188, 0x939293, 0xfc9867,
        0xff6188, 0x78dce8, 0xa9dc76, 0xffd866
    };
    const int fallback[TB_COLOR_COUNT] = {
        0, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_MAGENTA, COLOR_GREEN,
        COLOR_YELLOW, COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN,
        COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE,
        COLOR_MAGENTA, COLOR_WHITE, COLOR_YELLOW, COLOR_MAGENTA,
        COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW
    };
    if (getenv("NO_COLOR") != NULL || !has_colors() || start_color() == ERR ||
        COLORS < 8 || COLOR_PAIRS < 2 * TB_COLOR_COUNT) {
        return "monochrome";
    }
    for (int pair = 1; pair < 2 * TB_COLOR_COUNT; ++pair) {
        int i = pair % TB_COLOR_COUNT;
        int background = i == TB_SURFACE || i == TB_TAB || i == TB_KEY ? 0x353236 :
                         i == TB_WORKSPACE ? 0x221f22 :
                         i == TB_CHROME || i == TB_BRAND || i == TB_TARGET || i == TB_STATE ? 0x29262a :
                         i == TB_FOOTER ? 0x302d32 : i == TB_NOTICE ? 0x322b34 :
                         i == TB_SELECTED || i == TB_ACTIVE_TAB ? 0x4b3d4b : 0x2d2a2e;
        if (pair >= TB_COLOR_COUNT) { background = 0x4b3d4b; }
        int fg = COLORS >= 256 ? color_index(foreground[i]) : fallback[i];
        int bg = COLORS >= 256 ? color_index(background) : COLOR_BLACK;
        if (init_extended_pair(pair, fg, bg) == ERR) {
            return "monochrome";
        }
    }
    theme_color = true;
    return COLORS >= 16777216 ? "truecolor" : COLORS >= 256 ? "256-color" : "8-color";
}

void tb_style(TbStyle style, attr_t attributes)
{
    if (!theme_color && (style == TB_ACTIVE_TAB || style == TB_SELECTED ||
                        style >= TB_COLOR_COUNT)) { attributes |= A_REVERSE; }
    /* Pair zero is always valid, including when NO_COLOR is set. */
    (void)attrset(attributes | (theme_color ? COLOR_PAIR(style) : 0U));
}

TbStyle tb_selected_style(TbStyle style)
{
    return (TbStyle)(style + TB_COLOR_COUNT);
}
