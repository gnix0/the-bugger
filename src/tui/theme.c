#include "theme.h"

#include <curses.h>
#include <stdlib.h>
#include <string.h>

/* Owned by this module for the lifetime of the single curses screen. */
static bool theme_color;

/* Adapted role palettes; upstream revisions and MIT notices: THIRD_PARTY_NOTICES. */
static const TbPalette palettes[] = {
    {"catppuccin", "latte", true,
     {0xdce0e8, 0xeff1f5, 0xccd0da, 0xbcc0cc, 0xacb0be, 0x4a4d67, 0x65687f, 0xd11039, 0x45734b, 0x9b573e, 0x80644c, 0x8739ec, 0x2f6f98}},
    {"catppuccin", "frappe", false,
     {0x232634, 0x303446, 0x414559, 0x51576d, 0x626880, 0xc6d0f5, 0xa5adce, 0xe0939d, 0xa6d189, 0xef9f76, 0xe5c890, 0xca9ee6, 0x99d1db}},
    {"catppuccin", "macchiato", false,
     {0x181926, 0x24273a, 0x363a4f, 0x494d64, 0x5b6078, 0xcad3f5, 0xa5adcb, 0xed8796, 0xa6da95, 0xf5a97f, 0xeed49f, 0xc6a0f6, 0x91d7e3}},
    {"catppuccin", "mocha", false,
     {0x11111b, 0x1e1e2e, 0x313244, 0x45475a, 0x585b70, 0xcdd6f4, 0xa6adc8, 0xf38ba8, 0xa6e3a1, 0xfab387, 0xf9e2af, 0xcba6f7, 0x89dceb}},
    {"everforest", "dark-medium", false,
     {0x232a2e, 0x2d353b, 0x343f44, 0x3d484d, 0x4f585e, 0xd3c6aa, 0x9da9a0, 0xe67e80, 0xa7c080, 0xe69875, 0xdbbc7f, 0xd699b6, 0x7fbbb3}},
    {"everforest", "light-medium", true,
     {0xefebd4, 0xfdf6e3, 0xf4f0d9, 0xefebd4, 0xe0dcc7, 0x5c6a72, 0x7c8a7e, 0xe45856, 0x7d8f25, 0xbc7642, 0x9c843a, 0xbe69a8, 0x3f8eb9}},
    {"everforest", "dark-hard", false,
     {0x1e2326, 0x272e33, 0x2e383c, 0x374145, 0x495156, 0xd3c6aa, 0x9da9a0, 0xe67e80, 0xa7c080, 0xe69875, 0xdbbc7f, 0xd699b6, 0x7fbbb3}},
    {"everforest", "light-hard", true,
     {0xf2efdf, 0xfffbef, 0xf8f5e4, 0xf2efdf, 0xe8e5d5, 0x5c6a72, 0x7f8d80, 0xed5654, 0x809220, 0xc4773e, 0xa18736, 0xc769ad, 0x3c91c0}},
    {"everforest", "dark-soft", false,
     {0x293136, 0x333c43, 0x3a464c, 0x434f55, 0x555f66, 0xd3c6aa, 0x9da9a0, 0xe67e80, 0xa7c080, 0xe69875, 0xdbbc7f, 0xd699b6, 0x7fbbb3}},
    {"everforest", "light-soft", true,
     {0xe5dfc5, 0xf3ead3, 0xeae4ca, 0xe5dfc5, 0xd8d3ba, 0x57656c, 0x74827a, 0xce5959, 0x758631, 0xab7247, 0x8f7d40, 0xab679c, 0x4286ab}},
    {"gruvbox", "dark-medium", false,
     {0x1b1b1b, 0x282828, 0x32302f, 0x45403d, 0x5a524c, 0xddc7a1, 0xd4be98, 0xea6962, 0xa9b665, 0xe78a4e, 0xd8a657, 0xd3869b, 0x7daea3}},
    {"gruvbox", "light-medium", true,
     {0xf2e5bc, 0xfbf1c7, 0xf4e8be, 0xeee0b7, 0xddccab, 0x4f3829, 0x654735, 0xc14a4a, 0x6c782e, 0xc35e0a, 0xb47109, 0x945e80, 0x45707a}},
    {"gruvbox", "dark-hard", false,
     {0x141617, 0x1d2021, 0x282828, 0x3c3836, 0x504945, 0xddc7a1, 0xd4be98, 0xea6962, 0xa9b665, 0xe78a4e, 0xd8a657, 0xd3869b, 0x7daea3}},
    {"gruvbox", "light-hard", true,
     {0xf3eac7, 0xf9f5d7, 0xf5edca, 0xf2e5bc, 0xebdbb2, 0x4f3829, 0x654735, 0xc14a4a, 0x6c782e, 0xc35e0a, 0xb47109, 0x945e80, 0x45707a}},
    {"gruvbox", "dark-soft", false,
     {0x252423, 0x32302f, 0x3c3836, 0x504945, 0x665c54, 0xddc7a1, 0xd4be98, 0xe8756a, 0xa9b665, 0xe78a4e, 0xd8a657, 0xd3869b, 0x7daea3}},
    {"gruvbox", "light-soft", true,
     {0xebdbb2, 0xf2e5bc, 0xeddeb5, 0xe6d5ae, 0xd5c4a1, 0x4f3829, 0x654735, 0xc14a4a, 0x6c782e, 0xc05d0b, 0xaa6b0c, 0x945e80, 0x45707a}},
    {"tokyonight", "night", false,
     {0x0c0e14, 0x1a1b26, 0x292e42, 0x292e42, 0x545c7e, 0xc0caf5, 0xa9b1d6, 0xf7768e, 0x9ece6a, 0xff9e64, 0xe0af68, 0xbb9af7, 0x7dcfff}},
    {"tokyonight", "storm", false,
     {0x1b1e2d, 0x24283b, 0x292e42, 0x292e42, 0x545c7e, 0xc0caf5, 0xa9b1d6, 0xf7768e, 0x9ece6a, 0xff9e64, 0xe0af68, 0xbb9af7, 0x7dcfff}},
    {"tokyonight", "moon", false,
     {0x191b29, 0x222436, 0x2f334d, 0x2f334d, 0x545c7e, 0xc8d3f5, 0x828bb8, 0xff757f, 0xc3e88d, 0xff966c, 0xffc777, 0xc099ff, 0x86e1fc}},
    {"tokyonight", "day", true,
     {0xc1c9df, 0xe1e2e7, 0xc4c8da, 0xc4c8da, 0x8990b3, 0x2e51a0, 0x586cad, 0xc93372, 0x587539, 0xa95b0a, 0x866a44, 0x7f53de, 0x007197}},
    {"rose-pine", "main", false,
     {0x16141f, 0x191724, 0x1f1d2e, 0x403d52, 0x524f67, 0xe0def4, 0x908caa, 0xeb6f92, 0x95b1ac, 0xebbcba, 0xf6c177, 0xc4a7e7, 0x9ccfd8}},
    {"rose-pine", "moon", false,
     {0x1f1d30, 0x232136, 0x2a273f, 0x44415a, 0x56526e, 0xe0def4, 0x908caa, 0xeb6f92, 0x95b1ac, 0xea9a97, 0xf6c177, 0xc4a7e7, 0x9ccfd8}},
    {"rose-pine", "dawn", true,
     {0xf8f0e7, 0xfaf4ed, 0xfffaf3, 0xdfdad9, 0xcecacd, 0x464261, 0x797593, 0xb4637a, 0x668182, 0xa66c74, 0x9f7349, 0x8773a0, 0x538392}},
    {"nightfox", "nightfox", false,
     {0x131a24, 0x192330, 0x212e3f, 0x2b3b51, 0x39506d, 0xcdcecf, 0xaeafb0, 0xca627c, 0x81b29a, 0xf4a261, 0xdbc074, 0x9d79d6, 0x63cdcf}},
    {"nightfox", "dayfox", true,
     {0xe4dcd4, 0xf6f2ee, 0xdbd1dd, 0xe7d2be, 0xaab0ad, 0x3d2b5a, 0x643f61, 0xa5222f, 0x396847, 0x955f61, 0xac5402, 0x6e33ce, 0x287980}},
    {"nightfox", "dawnfox", true,
     {0xebe5df, 0xfaf4ed, 0xebe0df, 0xd0d8d8, 0xbdbfc9, 0x575279, 0x625c87, 0xae627a, 0x607f75, 0x9b6b7c, 0x93715d, 0x83719e, 0x567e92}},
    {"nightfox", "duskfox", false,
     {0x191726, 0x232136, 0x2d2a45, 0x433c59, 0x4b4673, 0xe0def4, 0xcdcbe0, 0xeb6f92, 0xa3be8c, 0xea9a97, 0xf6c177, 0xc4a7e7, 0x9ccfd8}},
    {"nightfox", "nordfox", false,
     {0x232831, 0x2e3440, 0x39404f, 0x3e4a5b, 0x5a657d, 0xcdcecf, 0xabb1bb, 0xc4858b, 0xa3be8c, 0xc9846e, 0xebcb8b, 0xb48ead, 0x88c0d0}},
    {"nightfox", "terafox", false,
     {0x0f1c1e, 0x152528, 0x1d3337, 0x293e40, 0x2d4f56, 0xe6eaea, 0xcbd9d8, 0xe85c51, 0x7aa4a1, 0xff8349, 0xfda47f, 0xb5708b, 0xa1cdd8}},
    {"nightfox", "carbonfox", false,
     {0x0c0c0c, 0x161616, 0x252525, 0x2a2a2a, 0x535353, 0xf2f4f8, 0xb6b8bb, 0xee5396, 0x25be6a, 0x3ddbd9, 0x08bdba, 0xbe95ff, 0x33b1ff}},
    {"monokai-pro", "pro", false,
     {0x221f22, 0x2d2a2e, 0x353236, 0x4b3d4b, 0x49454b, 0xfcfcfa, 0x939293, 0xff6188, 0xa9dc76, 0xfc9867, 0xffd866, 0xab9df2, 0x78dce8}},
    {"monokai-pro", "classic", false,
     {0x1d1e19, 0x272822, 0x3b3c35, 0x3b3c35, 0x57584f, 0xfdfff1, 0x919288, 0xf92d76, 0xa6e22e, 0xfd971f, 0xe6db74, 0xae81ff, 0x66d9ef}},
    {"monokai-pro", "machine", false,
     {0x1d2528, 0x273136, 0x3a4449, 0x3a4449, 0x545f62, 0xf2fffc, 0x8b9798, 0xff6d7e, 0xa2e57b, 0xffb270, 0xffed72, 0xbaa0f8, 0x7cd5f1}},
    {"monokai-pro", "octagon", false,
     {0x1e1f2b, 0x282a3a, 0x3a3d4b, 0x3a3d4b, 0x535763, 0xeaf2f1, 0x888d94, 0xff657a, 0xbad761, 0xff9b5e, 0xffd76d, 0xc39ac9, 0x9cd1bb}},
    {"monokai-pro", "ristretto", false,
     {0x211c1c, 0x2c2525, 0x403838, 0x403838, 0x5b5353, 0xfff1f3, 0x948a8b, 0xfd6883, 0xadda78, 0xf38d70, 0xf9cc6c, 0xa8a9eb, 0x85dacc}},
    {"monokai-pro", "spectrum", false,
     {0x191919, 0x222222, 0x363537, 0x363537, 0x525053, 0xf7f1ff, 0x8b888f, 0xfc618d, 0x7bd88f, 0xfd9353, 0xfce566, 0x948ae3, 0x5ad4e6}},
    {"monokai-pro", "light", true,
     {0xede7e5, 0xfaf4f2, 0xd3cdcc, 0xd3cdcc, 0xbfb9ba, 0x29242a, 0x777275, 0xc7426a, 0x27825b, 0xbc5430, 0xa36412, 0x7058be, 0x1e7d96}},
};

/* Muted/accent colors are blended toward text where needed for legibility. */
static size_t active_palette = 2; /* Catppuccin Macchiato. */
static const char *const families[] = {
    "catppuccin", "everforest", "tokyonight", "rose-pine", "nightfox", "gruvbox", "monokai-pro"
};
static const char *const names[] = {
    "Catppuccin", "Everforest", "Tokyo Night", "Rose Pine", "Nightfox", "Gruvbox Material", "Monokai Pro"
};
size_t tb_theme_count(void) { return sizeof palettes / sizeof palettes[0]; }
size_t tb_theme_family_count(void) { return sizeof families / sizeof families[0]; }
const char *tb_theme_family(size_t family) { return family < tb_theme_family_count() ? families[family] : NULL; }
const char *tb_theme_name(size_t family) { return family < tb_theme_family_count() ? names[family] : NULL; }
const TbPalette *tb_theme_palette(size_t index) { return index < tb_theme_count() ? &palettes[index] : NULL; }
size_t tb_theme_current(void) { return active_palette; }
bool tb_theme_select(size_t index)
{
    if (index >= tb_theme_count()) { return false; }
    active_palette = index; return true;
}
size_t tb_theme_find(const char *family, const char *variant)
{
    for (size_t i = 0; family && variant && i < tb_theme_count(); ++i) {
        if (strcmp(family, palettes[i].family) == 0 && strcmp(variant, palettes[i].variant) == 0) { return i; }
    }
    return tb_theme_count();
}

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
    const TbPalette *palette = &palettes[active_palette];
    const unsigned *c = palette->colors;
    const unsigned foreground[TB_COLOR_COUNT] = {
        0, c[5], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12],
        c[5], c[4], c[5], c[6], c[6], c[7], c[6], c[9], c[7], c[12], c[8], c[10],
        c[10], c[6], c[12], c[8]
    };
    const int fallback[TB_COLOR_COUNT] = {
        0, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_MAGENTA, COLOR_GREEN,
        COLOR_YELLOW, COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN,
        COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE,
        COLOR_MAGENTA, COLOR_WHITE, COLOR_YELLOW, COLOR_MAGENTA,
        COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW,
        COLOR_YELLOW, COLOR_WHITE, COLOR_CYAN, COLOR_GREEN
    };
    if (getenv("NO_COLOR") != NULL || !has_colors() || start_color() == ERR ||
        COLORS < 8 || COLOR_PAIRS < 2 * TB_COLOR_COUNT) {
        return "monochrome";
    }
    for (int pair = 1; pair < 2 * TB_COLOR_COUNT; ++pair) {
        int i = pair % TB_COLOR_COUNT;
        unsigned background = i == TB_SURFACE || i == TB_TAB || i == TB_KEY ? c[2] :
                         i == TB_WORKSPACE || i >= TB_STATUS_TEXT ? c[0] :
                         i == TB_CHROME || i == TB_BRAND || i == TB_TARGET || i == TB_STATE ? c[0] :
                         i == TB_FOOTER || i == TB_NOTICE ? c[2] :
                         i == TB_SELECTED || i == TB_ACTIVE_TAB ? c[3] : c[1];
        if (strcmp(palette->family, "monokai-pro") == 0 && strcmp(palette->variant, "pro") == 0) {
            if (i == TB_CHROME || i == TB_BRAND || i == TB_TARGET || i == TB_STATE) { background = 0x29262a; }
            if (i == TB_FOOTER) { background = 0x302d32; }
            if (i == TB_NOTICE) { background = 0x322b34; }
        }
        if (pair >= TB_COLOR_COUNT) { background = c[3]; }
        int fg = COLORS >= 256 ? color_index(foreground[i]) : fallback[i];
        int bg = COLORS >= 256 ? color_index(background) : COLOR_BLACK;
        /* Quantization can merge subtle borders with a selected surface. */
        if (i != 0 && fg == bg) { fg = COLORS >= 256 ? color_index(c[5]) : COLOR_WHITE; }
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
