#include "tui/render.h"

#include <curses.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// named display columns follow output capacity.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void tb_format_assembly(char *out, size_t capacity, TbAssemblyColumns_t columns, const char *address,
                        const char *bytes, const char *instruction)
{
    int         mnemonic = (int)strcspn(instruction, " \t");
    const char *operands = instruction + mnemonic;

    while (*operands == ' ' || *operands == '\t')
        ++operands;

    if (*operands) {
        (void)snprintf(out, capacity, "%-*s  %-*s  %-*.*s %s", columns.address, address, columns.bytes, bytes,
                       columns.mnemonic, mnemonic, instruction, operands);
    } else {
        (void)snprintf(out, capacity, "%-*s  %-*s  %.*s", columns.address, address, columns.bytes, bytes,
                       mnemonic, instruction);
    }
}


// follows curses y/x convention.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void tb_text(int y, int x, int width, TbStyle_e style, attr_t attributes, const char *text)
{
    if (y < 0 || y >= LINES || x < 0 || x >= COLS || width <= 0)
        return;

    int available = COLS - x - (y == LINES - 1 ? 1 : 0);
    if (width > available)
        width = available;

    if (width <= 0 || !text)
        return;

    tb_style(style, attributes);

    int       cells = 0;
    mbstate_t state = {0};

    while (*text && cells < width) {
        /* Keep the usual ASCII path cheap, but never let curses interpret a
         * control character or use a byte count as a display-cell bound. */
        int ascii = 0;
        while (ascii < width - cells && text[ascii] >= ' ' && text[ascii] <= '~')
            ++ascii;
        if (ascii) {
            (void)mvaddnstr(y, x + cells, text, ascii);
            cells += ascii;
            text  += ascii;
            continue;
        }
        wchar_t character;
        size_t  count   = mbrtowc(&character, text, strnlen(text, MB_CUR_MAX), &state);
        bool    invalid = count == (size_t)-1 || count == (size_t)-2;
        int     columns = invalid ? -1 : wcwidth(character);
        if (columns < 0 || (columns == 0 && cells == 0)) {
            char escaped[5];
            (void)snprintf(escaped, sizeof(escaped), "\\x%02x", (unsigned char)*text);
            int length = width - cells < 4 ? width - cells : 4;
            (void)mvaddnstr(y, x + cells, escaped, length);
            cells += length;
            ++text;
            memset(&state, 0, sizeof(state));
        } else {
            if (columns > width - cells)
                break;

            (void)mvaddnwstr(y, x + cells, &character, 1);
            cells += columns;
            text  += count;
        }
    }
}


void tb_fill(TbRect_t rect, TbStyle_e style)
{
    tb_style(style, 0);
    if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0)
        return;

    for (int y = rect.y; y < rect.y + rect.h && y < LINES; ++y) {
        if (rect.x < COLS) {
            int w = rect.w < COLS - rect.x ? rect.w : COLS - rect.x;
            (void)mvhline(y, rect.x, ' ', w);
        }
    }
}


#define BORDER_UP    1U
#define BORDER_RIGHT 2U
#define BORDER_DOWN  4U
#define BORDER_LEFT  8U


static void border_cell(int y, int x, unsigned edges, const chtype *glyphs,
                        TbStyle_e style, bool preserve_content)
{
    chtype old   = mvinch(y, x);
    chtype mask  = A_CHARTEXT | A_ALTCHARSET;
    int    pair  = PAIR_NUMBER(old);
    bool   clear = (old & A_CHARTEXT) == ' ' && !(old & (A_REVERSE | A_UNDERLINE)) &&
                 (!tb_theme_has_color() || pair == 0 || pair == TB_WORKSPACE || pair == TB_CHROME);
    bool   line  = false;

    /* Adjacent exterior frames share one-cell gutters, including split junctions. */
    if (PAIR_NUMBER(old) == (tb_theme_has_color() ? (int)style : 0)) {
        for (unsigned i = 1; i < 16; ++i) {
            if ((old & mask) == (glyphs[i] & mask)) {
                line = true;
                edges |= i == BORDER_UP || i == BORDER_DOWN ? BORDER_UP | BORDER_DOWN :
                         i == BORDER_LEFT || i == BORDER_RIGHT ? BORDER_LEFT | BORDER_RIGHT : i;
                break;
            }
        }
    }

    /* Exterior frames can share a row with status text or dock controls. */
    if (!preserve_content || clear || line)
        (void)mvaddch(y, x, glyphs[edges]);
}


void tb_border(TbRect_t rect, TbStyle_e style, bool preserve_content)
{
    if (rect.x < 0 || rect.y < 0 || rect.w < 2 || rect.h < 2 ||
        rect.x > COLS - rect.w || rect.y > LINES - rect.h)
        return;

    const chtype glyphs[] = {
        ' ', ACS_VLINE, ACS_HLINE, ACS_LLCORNER,
        ACS_VLINE, ACS_VLINE, ACS_ULCORNER, ACS_LTEE,
        ACS_HLINE, ACS_LRCORNER, ACS_HLINE, ACS_BTEE,
        ACS_URCORNER, ACS_RTEE, ACS_TTEE, ACS_PLUS
    };

    tb_style(style, 0);
    for (int col = 0; col < rect.w; ++col) {
        unsigned edges = (col > 0 ? BORDER_LEFT : 0U) | (col + 1 < rect.w ? BORDER_RIGHT : 0U);
        bool     corner = col == 0 || col + 1 == rect.w;
        border_cell(rect.y, rect.x + col, edges | (corner ? BORDER_DOWN : 0U),
                    glyphs, style, preserve_content);
        border_cell(rect.y + rect.h - 1, rect.x + col, edges | (corner ? BORDER_UP : 0U),
                    glyphs, style, preserve_content);
    }
    for (int row = 1; row + 1 < rect.h; ++row) {
        border_cell(rect.y + row, rect.x, BORDER_UP | BORDER_DOWN, glyphs, style, preserve_content);
        border_cell(rect.y + row, rect.x + rect.w - 1, BORDER_UP | BORDER_DOWN,
                    glyphs, style, preserve_content);
    }
}


/* Coordinates follow curses' row/column convention. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void pane_frame_cell(const TbRect_t *panes, size_t count, int y, int x, TbRect_t active)
{
    if (y < 0 || x < 0 || y >= LINES || x >= COLS)
        return;

    unsigned edges = 0;
    for (size_t i = 0; i < count; ++i) {
        TbRect_t rect = panes[i];
        if (rect.w <= 0 || rect.h <= 0)
            continue;
        if (tb_rect_contains(rect, x, y))
            return;

        int left   = rect.x - 1;
        int right  = rect.x + rect.w;
        int top    = rect.y - 1;
        int bottom = rect.y + rect.h;
        if ((y == top || y == bottom) && x >= left && x <= right) {
            edges |= (x > left ? BORDER_LEFT : 0U) | (x < right ? BORDER_RIGHT : 0U);
            if (x == left || x == right)
                edges |= y == top ? BORDER_DOWN : BORDER_UP;
        }
        if ((x == left || x == right) && y > top && y < bottom)
            edges |= BORDER_UP | BORDER_DOWN;
    }
    if (!edges)
        return;

    const chtype glyphs[] = {
        ' ', ACS_VLINE, ACS_HLINE, ACS_LLCORNER,
        ACS_VLINE, ACS_VLINE, ACS_ULCORNER, ACS_LTEE,
        ACS_HLINE, ACS_LRCORNER, ACS_HLINE, ACS_BTEE,
        ACS_URCORNER, ACS_RTEE, ACS_TTEE, ACS_PLUS
    };
    bool selected = tb_rect_contains(active, x, y);
    tb_style(selected ? TB_ACTIVE_TAB : TB_BORDER, selected ? A_BOLD : 0);
    (void)mvaddch(y, x, glyphs[edges]);
}


void tb_pane_frames(const TbRect_t *panes, size_t count, TbRect_t active)
{
    for (size_t i = 0; i < count; ++i) {
        TbRect_t rect = panes[i];
        if (rect.w <= 0 || rect.h <= 0)
            continue;
        for (int x = rect.x - 1; x <= rect.x + rect.w; ++x) {
            pane_frame_cell(panes, count, rect.y - 1, x, active);
            pane_frame_cell(panes, count, rect.y + rect.h, x, active);
        }
        for (int y = rect.y; y < rect.y + rect.h; ++y) {
            pane_frame_cell(panes, count, y, rect.x - 1, active);
            pane_frame_cell(panes, count, y, rect.x + rect.w, active);
        }
    }
}


/* Desired cell width and item count are separate dimensions of the popup. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
TbRect_t tb_dropdown_rect(TbRect_t trigger, TbRect_t bounds, int width, int count)
{
    if (bounds.w < 4 || bounds.h < 3 || count <= 0)
        return (TbRect_t) {0};

    int below = bounds.y + bounds.h - trigger.y - trigger.h;
    int above = trigger.y - bounds.y;
    int rows  = count < 8 ? count : 8;
    bool up   = below < rows + 2 && above >= below;
    int space = up ? above : below;

    if (rows + 2 > space)
        rows = space - 2;
    if (rows < 1)
        rows = 1;
    if (width > bounds.w)
        width = bounds.w;
    if (width < 4)
        width = 4;

    int x = trigger.x;
    int y = up ? trigger.y - rows - 2 : trigger.y + trigger.h;
    if (x > bounds.x + bounds.w - width)
        x = bounds.x + bounds.w - width;
    if (x < bounds.x)
        x = bounds.x;
    if (y > bounds.y + bounds.h - rows - 2)
        y = bounds.y + bounds.h - rows - 2;
    if (y < bounds.y)
        y = bounds.y;

    return (TbRect_t) {
        .x = x,
        .y = y,
        .w = width,
        .h = rows + 2
    };
}


TbRect_t tb_scrollbar_track(TbRect_t frame, bool horizontal)
{
    if (frame.w < 3 || frame.h < 3)
        return (TbRect_t) {0};

    return (TbRect_t) {
        .x = horizontal ? frame.x + 1 : frame.x + frame.w - 2,
        .y = horizontal ? frame.y + frame.h - 2 : frame.y + 1,
        .w = horizontal ? frame.w - 2 : 1,
        .h = horizontal ? 1 : frame.h - 2
    };
}


TbScroll_t tb_scrollbar_layout(TbScroll_t scroll, int length)
{
    if (length <= 0 || scroll.visible <= 0 || scroll.total <= 0) {
        scroll.thumb_size = scroll.thumb_start = 0;
        return scroll;
    }
    scroll.thumb_size = (int)((int64_t)length * scroll.visible / scroll.total);
    if (scroll.thumb_size < 1)
        scroll.thumb_size = 1;
    if (scroll.thumb_size > length)
        scroll.thumb_size = length;
    scroll.thumb_start = scroll.maximum > 0
        ? (int)((int64_t)scroll.offset * (length - scroll.thumb_size) / scroll.maximum) : 0;
    return scroll;
}


void tb_render_scrollbar(TbRect_t rect, TbScroll_t scroll, bool horizontal)
{
    int length = horizontal ? rect.w : rect.h;
    if (scroll.maximum <= 0 || length <= 0 || scroll.visible <= 0 ||
        rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 ||
        rect.x > COLS - rect.w || rect.y > LINES - rect.h)
        return;

    if (length != scroll.visible)
        scroll = tb_scrollbar_layout(scroll, length);
    int start = scroll.thumb_start;
    int size  = scroll.thumb_size;
    if (start < 0)
        start = 0;
    if (start >= length)
        start = length - 1;
    if (size < 1)
        size = 1;
    if (size > length - start)
        size = length - start;

    if (horizontal && tb_theme_has_color() && MB_CUR_MAX > 1 && wcwidth(L'\u2584') == 1) {
        for (int col = 0; col < length; ++col) {
            tb_style(col >= start && col < start + size ? TB_SCROLL_H_THUMB : TB_SCROLL_H_TRACK, 0);
            (void)mvaddnwstr(rect.y, rect.x + col, L"\u2584", 1);
        }
        return;
    }

    if (tb_theme_has_color()) {
        tb_fill(rect, TB_SCROLL_TRACK);
    } else {
        /* Line drawing retains a visible track without color; reverse cells mark the thumb. */
        tb_style(TB_SCROLL_TRACK, A_DIM);
        if (horizontal)
            (void)mvhline(rect.y, rect.x, ACS_HLINE, rect.w);
        else
            (void)mvvline(rect.y, rect.x, ACS_VLINE, rect.h);
    }

    if (horizontal) {
        rect.x += start;
        rect.w  = size;
    } else {
        rect.y += start;
        rect.h  = size;
    }
    tb_fill(rect, TB_SCROLL_THUMB);
}


static const char *heading(TbView_e view)
{
    switch (view) {
    case TB_SOURCE:
        return "main.c | Cortex-M4F sample";
    case TB_DISASSEMBLY:
        return "  Address    Bytes  Instruction";
    case TB_REGISTERS:
        return "Name       Value       Change";
    case TB_MEMORY:
        return "Address   HEX bytes / ASCII";
    case TB_LOCALS:
        return "Name        Type       Value";
    case TB_CONSOLE:
        return "Sample log / no commands executed";
    case TB_WATCHES:
        return "Expression             Value";
    case TB_CALL_STACK:
        return "# Function     Location";
    case TB_BREAKPOINTS:
        return "State   Location";
    case TB_THREADS:
        return "ID State       Location";
    default:
        return "NOT IMPLEMENTED";
    }
}


static void source_line(int index, char *text, size_t capacity, TbStyle_e *style, const TbDemo_t *demo)
{
    char pc = index == 17 + demo->step ? '>' : ' ';
    (void)snprintf(text, capacity, "%c%c %2d %s", demo->breakpoints[index] ? '*' : ' ', pc, index + 13,
                   tb_demo_source[index]);
    const char *code = tb_demo_source[index];
    *style = code[0] == '#' ? TB_PINK :
             strstr(code, "uint32_t") != NULL ? TB_CYAN :
             strstr(code, "main(") != NULL || strstr(code, "delay(") != NULL ? TB_GREEN :
             strstr(code, "for") != NULL || strstr(code, "while") != NULL ? TB_PINK : TB_PANEL;
    if (pc == '>')
        *style = TB_YELLOW;
}


static bool view_line(const TbDemo_t *demo, TbView_e view, int index, char *text, size_t capacity,
                      TbStyle_e *style)
{
    if (index < 0 || index >= tb_demo_row_count(demo, view))
        return false;

    *style = TB_PANEL;
    if (view == TB_SOURCE) {
        source_line(index, text, capacity, style, demo);
    } else if (view == TB_DISASSEMBLY) {
        static const char *const instructions[] = {
            "push {r4, lr}",
            "ldr r3, [pc, #20]",
            "ldr r2, [r3]",
            "eor r2, r2, #32",
            "str r2, [r3]",
            "ldr r0, [pc, #12]",
            "bl delay",
            "adds r4, #1",
            "b.n 0x0800042A"
        };

        static const char *const bytes[] = {
            "B5 10",
            "4B 05",
            "68 1A",
            "F0 82",
            "60 1A",
            "48 03",
            "F0 00",
            "34 01",
            "E7 F4"
        };

        (void)snprintf(text, capacity, "%c %08X  %s  %s", index == demo->step ? '>' : ' ',
                       0x0800042aU + (unsigned)index * 2U, bytes[index], instructions[index]);
        *style = index == demo->step ? TB_YELLOW : TB_GREEN;
    } else if (view == TB_REGISTERS) {
        char name[16];
        if (index < 13) {
            (void)snprintf(name, sizeof(name), "r%d", index);
        } else {
            const char *const names[] = {
                "sp",
                "lr",
                "pc",
                "xPSR"
            };

            (void)snprintf(name, sizeof(name), "%s", names[index - 13]);
        }
        unsigned value = index == 15   ? 0x0800042aU + (unsigned)demo->step * 2U
                         : index == 13 ? 0x2001fff0U
                         : index == 16 ? 0x61000000U
                                       : (unsigned)index * 32U + (unsigned)demo->step;
        (void)snprintf(text, capacity, "%-7s  0x%08X  %s", name, value, demo->step != 0 ? "+ changed" : "");
        *style = demo->step != 0 ? TB_YELLOW : TB_CYAN;
    } else if (view == TB_MEMORY) {
        (void)snprintf(text, capacity, "%08X  %02X 42 55 47 47 45 52 21  .BUGGER!",
                       0x20000000U + (unsigned)index * 8U, (unsigned)demo->step + 0x20U);
        *style = TB_PURPLE;
    } else if (view == TB_LOCALS || view == TB_WATCHES) {
        const char *const names[] = {
            "counter",
            "gpio_state",
            "LED_PIN",
            "i"
        };

        (void)snprintf(text, capacity, "%-12s %-9s %d%s", names[index], view == TB_LOCALS ? "uint32_t" : "",
                       demo->step + index, demo->step != 0 ? " +" : "");
        *style = demo->step != 0 ? TB_YELLOW : TB_GREEN;
    } else if (view == TB_CONSOLE) {
        (void)snprintf(text, capacity, "%s", demo->logs[index]);
        *style = TB_GREEN;
    } else if (view == TB_CALL_STACK) {
        (void)snprintf(text, capacity, "%d %-14s %s", index, index == 0 ? "main" : "Reset_Handler",
                       index == 0 ? "main.c:30" : "startup.s:63");
        *style = TB_CYAN;
    } else if (view == TB_BREAKPOINTS) {
        (void)snprintf(text, capacity, "%c %-8s main.c:%d", demo->breakpoints[index] ? '*' : '-',
                       demo->breakpoints[index] ? "enabled" : "none", index + 13);
        *style = demo->breakpoints[index] ? TB_PINK : TB_MUTED;
    } else if (view == TB_THREADS) {
        (void)snprintf(text, capacity, "1  %-10s main.c:30", demo->running ? "running" : "stopped");
        *style = TB_GREEN;
    } else if (view >= TB_SYMBOLS) {
        (void)snprintf(text, capacity, "%s",
                       index == 0 ? "Future view; not implemented." : "No target data is available.");
        *style = TB_ORANGE;
    } else {
        return false;
    }

    return true;
}


/* Read-only lexical coloring; this does not parse or evaluate source code. */
// follows curses y/x convention.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void tb_render_source_slice(int y, int x, int width, const char *code, int scroll, bool selected)
{
    if (scroll < 0 || width <= 0)
        return;

    int offset = 0;
    while (code[offset] != '\0' && offset - scroll < width) {
        int           start = offset++;
        TbStyle_e     style = TB_PANEL;
        unsigned char c     = (unsigned char)code[start];
        if (c == '/' && code[offset] == '/') {
            offset = (int)strlen(code);
            style  = TB_MUTED;
        } else if (c == '#' || c == '"') {
            offset = (int)strlen(code);
            style  = c == '#' ? TB_PINK : TB_YELLOW;
        } else if (isalnum(c) || c == '_') {
            while (isalnum((unsigned char)code[offset]) || code[offset] == '_')
                ++offset;
            int length = offset - start;
            if (isdigit(c)) {
                style = TB_PURPLE;
            } else if (code[offset] == '(') {
                style = TB_GREEN;
            }
            if ((length == 3 && strncmp(code + start, "int", 3) == 0) ||
                (length == 8 && strncmp(code + start, "uint32_t", 8) == 0)) {
                style = TB_CYAN;
            }
            if ((length == 5 && strncmp(code + start, "while", 5) == 0) ||
                (length == 3 && strncmp(code + start, "for", 3) == 0) ||
                (length == 6 && strncmp(code + start, "return", 6) == 0) ||
                (length == 8 && strncmp(code + start, "volatile", 8) == 0)) {
                style = TB_PINK;
            }
        }
        /* Classify the complete token before clipping its visible fragment. */
        int left = start < scroll ? scroll : start;
        int right = offset - scroll > width ? scroll + width : offset;
        if (right > left) {
            tb_text(y, x + left - scroll, right - left,
                    selected ? tb_selected_style(style) : style, 0, code + left);
        }
    }
}


// follows curses y/x convention.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void tb_render_source_text(int y, int x, int width, const char *code, bool selected)
{
    tb_render_source_slice(y, x, width, code, 0, selected);
}


// follows curses y/x convention.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void styled_line(TbView_e view, int y, int x, int width, const char *text, TbStyle_e style,
                        bool selected)
{
    tb_text(y, x, width, selected ? tb_selected_style(style) : style, 0, text);
    if (view == TB_SOURCE) {
        tb_text(y, x, 1, selected ? tb_selected_style(TB_PINK) : TB_PINK, A_BOLD, text);
        tb_text(y, x + 1, 1, selected ? tb_selected_style(TB_YELLOW) : TB_YELLOW, A_BOLD, text + 1);
        tb_text(y, x + 2, 4, selected ? tb_selected_style(TB_MUTED) : TB_MUTED, 0, text + 2);
        tb_render_source_text(y, x + 6, width - 6, text + 6, selected);
        return;
    }

    /* Column boundaries match the sample table format above. */
    int       starts[4]  = {0};
    int       lengths[4] = {0};
    TbStyle_e styles[4]  = {TB_PANEL, TB_PANEL, TB_PANEL, TB_PANEL};
    int       count      = 0;
    if (view == TB_DISASSEMBLY) {
        starts[0]  = 2;
        lengths[0] = 8;
        styles[0]  = TB_PURPLE;

        starts[1]  = 12;
        lengths[1] = 5;
        styles[1]  = TB_MUTED;

        starts[2]  = 19;
        lengths[2] = (int)strcspn(text + 19, " ");
        styles[2]  = TB_GREEN;

        count = 3;
    } else if (view == TB_REGISTERS) {
        lengths[0] = 7;
        styles[0]  = TB_CYAN;

        starts[1]  = 9;
        lengths[1] = width - 9;
        styles[1]  = style == TB_YELLOW ? TB_YELLOW : TB_GREEN;

        count = 2;
    } else if (view == TB_MEMORY) {
        lengths[0] = 8;
        styles[0]  = TB_PURPLE;

        starts[1]  = 10;
        lengths[1] = 23;
        styles[1]  = TB_PANEL;

        starts[2]  = 35;
        lengths[2] = 8;
        styles[2]  = TB_MUTED;

        count = 3;
    } else if (view == TB_LOCALS || view == TB_WATCHES) {
        lengths[0] = 12;
        styles[0]  = TB_CYAN;

        starts[1]  = 13;
        lengths[1] = 9;
        styles[1]  = TB_MUTED;

        starts[2]  = 23;
        lengths[2] = width - 23;
        styles[2]  = style;

        count = 3;
    }

    int size = (int)strlen(text);
    for (int i = 0; i < count; ++i) {
        int length = lengths[i] < width - starts[i] ? lengths[i] : width - starts[i];
        if (starts[i] < size && length > 0) {
            tb_text(y, x + starts[i], length, selected ? tb_selected_style(styles[i]) : styles[i], 0,
                    text + starts[i]);
        }
    }
}


void tb_render_demo(const TbDemo_t *demo, int pane_index, TbRect_t rect)
{
    if (rect.w < 10 || rect.h < 7)
        return;

    const TbDemoPane_t *pane   = &demo->panes[pane_index];
    TbView_e            view   = pane->tabs[pane->active];
    TbScroll_t          scroll = tb_demo_scrollbar(demo, pane, rect.h - 3);
    int                 x      = rect.x + 1;
    int                 width  = rect.w - 3;

    tb_fill((TbRect_t) {
        rect.x,
        rect.y + 1,
        rect.w,
        1
    }, TB_FOOTER);
    tb_text(rect.y + 1, x, width, TB_FOOTER, 0, heading(view));

    for (int row = 0; row < rect.h - 3; ++row) {
        char      text[256];
        TbStyle_e style;
        int       index = row + scroll.offset;
        if (!view_line(demo, view, index, text, sizeof(text), &style))
            break;

        bool selected = (view == TB_SOURCE && index == 17 + demo->step) ||
                        (view == TB_DISASSEMBLY && index == demo->step);
        if (selected) {
            tb_fill((TbRect_t) {
                rect.x,
                rect.y + 2 + row,
                rect.w,
                1
            }, TB_SELECTED);
        }
        styled_line(view, rect.y + 2 + row, x, width, text, style, selected);
    }

    if (scroll.maximum > 0) {
        TbRect_t frame = rect;
        ++frame.y;
        --frame.h;
        tb_render_scrollbar(tb_scrollbar_track(frame, false), scroll, false);
    }

    static const char *const footers[] = {
        "main.c | UTF-8 | C",
        "Thumb-2 | simulated",
        "Cortex-M4F | core",
        "20000000 | HEX/ASCII",
        "Frame 0 | main",
        "GDB/MI | sample log"
    };

    int footer    = rect.y + rect.h - 1;

    tb_fill((TbRect_t) {
        rect.x,
        footer,
        rect.w,
        1
    }, TB_FOOTER);
    tb_text(footer, x, width - 6, TB_FOOTER, 0,
            demo->running        ? "Running | stale sample"
            : view <= TB_CONSOLE ? footers[view]
                                 : "Simulated preview");
    tb_text(footer, rect.x + rect.w - 5, 4, TB_FOOTER, A_BOLD, "DEMO");
}
