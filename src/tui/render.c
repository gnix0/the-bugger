#include "render.h"

#include <curses.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named display columns follow output capacity.
void tb_format_assembly(char *out, size_t capacity, TbAssemblyColumns columns,
                        const char *address, const char *bytes, const char *instruction)
{
    int mnemonic = (int)strcspn(instruction, " \t");
    const char *operands = instruction + mnemonic;
    while (*operands == ' ' || *operands == '\t') { ++operands; }
    if (*operands) {
        (void)snprintf(out, capacity, "%-*s  %-*s  %-*.*s %s", columns.address, address,
                       columns.bytes, bytes, columns.mnemonic, mnemonic, instruction, operands);
    } else {
        (void)snprintf(out, capacity, "%-*s  %-*s  %.*s", columns.address, address,
                       columns.bytes, bytes, mnemonic, instruction);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): follows curses y/x convention.
void tb_text(int y, int x, int width, TbStyle style, attr_t attributes, const char *text)
{
    if (y < 0 || y >= LINES || x < 0 || x >= COLS || width <= 0) {
        return;
    }
    int available = COLS - x - (y == LINES - 1 ? 1 : 0);
    if (width > available) {
        width = available;
    }
    if (width > 0) {
        tb_style(style, attributes);
        (void)mvaddnstr(y, x, text, width);
    }
}

void tb_fill(TbRect rect, TbStyle style)
{
    tb_style(style, 0);
    if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    for (int y = rect.y; y < rect.y + rect.h && y < LINES; ++y) {
        if (rect.x < COLS) {
            int w = rect.w < COLS - rect.x ? rect.w : COLS - rect.x;
            (void)mvhline(y, rect.x, ' ', w);
        }
    }
}

static const char *heading(TbView view)
{
    switch (view) {
    case TB_SOURCE: return "main.c | Cortex-M4F sample";
    case TB_DISASSEMBLY: return "  Address    Bytes  Instruction";
    case TB_REGISTERS: return "Name       Value       Change";
    case TB_MEMORY: return "Address   HEX bytes / ASCII";
    case TB_LOCALS: return "Name        Type       Value";
    case TB_CONSOLE: return "Sample log / no commands executed";
    case TB_WATCHES: return "Expression             Value";
    case TB_CALL_STACK: return "# Function     Location";
    case TB_BREAKPOINTS: return "State   Location";
    case TB_THREADS: return "ID State       Location";
    default: return "NOT IMPLEMENTED";
    }
}

static void source_line(int index, char *text, size_t capacity,
                        TbStyle *style, const TbDemo *demo)
{
    char pc = index == 17 + demo->step ? '>' : ' ';
    (void)snprintf(text, capacity, "%c%c %2d %s",
                   demo->breakpoints[index] ? '*' : ' ', pc,
                   index + 13, tb_demo_source[index]);
    const char *code = tb_demo_source[index];
    *style = code[0] == '#' ? TB_PINK : strstr(code, "uint32_t") != NULL ? TB_CYAN :
             strstr(code, "main(") != NULL || strstr(code, "delay(") != NULL ? TB_GREEN :
             strstr(code, "for") != NULL || strstr(code, "while") != NULL ? TB_PINK : TB_PANEL;
    if (pc == '>') {
        *style = TB_YELLOW;
    }
}

static bool view_line(const TbDemo *demo, TbView view, int index,
                      char *text, size_t capacity, TbStyle *style)
{
    if (index < 0 || index >= tb_demo_row_count(demo, view)) { return false; }
    *style = TB_PANEL;
    if (view == TB_SOURCE) {
        source_line(index, text, capacity, style, demo);
    } else if (view == TB_DISASSEMBLY) {
        static const char *const instructions[] = {
            "push {r4, lr}", "ldr r3, [pc, #20]", "ldr r2, [r3]",
            "eor r2, r2, #32", "str r2, [r3]", "ldr r0, [pc, #12]",
            "bl delay", "adds r4, #1", "b.n 0x0800042A"
        };
        static const char *const bytes[] = {
            "B5 10", "4B 05", "68 1A", "F0 82", "60 1A", "48 03", "F0 00", "34 01", "E7 F4"
        };
        (void)snprintf(text, capacity, "%c %08X  %s  %s",
                       index == demo->step ? '>' : ' ',
                       0x0800042aU + (unsigned)index * 2U, bytes[index], instructions[index]);
        *style = index == demo->step ? TB_YELLOW : TB_GREEN;
    } else if (view == TB_REGISTERS) {
        char name[16];
        if (index < 13) {
            (void)snprintf(name, sizeof name, "r%d", index);
        } else {
            const char *const names[] = {"sp", "lr", "pc", "xPSR"};
            (void)snprintf(name, sizeof name, "%s", names[index - 13]);
        }
        unsigned value = index == 15 ? 0x0800042aU + (unsigned)demo->step * 2U :
                         index == 13 ? 0x2001fff0U : index == 16 ? 0x61000000U :
                         (unsigned)index * 32U + (unsigned)demo->step;
        (void)snprintf(text, capacity, "%-7s  0x%08X  %s", name, value,
                       demo->step != 0 ? "+ changed" : "");
        *style = demo->step != 0 ? TB_YELLOW : TB_CYAN;
    } else if (view == TB_MEMORY) {
        (void)snprintf(text, capacity, "%08X  %02X 42 55 47 47 45 52 21  .BUGGER!",
                       0x20000000U + (unsigned)index * 8U,
                       (unsigned)demo->step + 0x20U);
        *style = TB_PURPLE;
    } else if (view == TB_LOCALS || view == TB_WATCHES) {
        const char *const names[] = {"counter", "gpio_state", "LED_PIN", "i"};
        (void)snprintf(text, capacity, "%-12s %-9s %d%s", names[index],
                       view == TB_LOCALS ? "uint32_t" : "", demo->step + index,
                       demo->step != 0 ? " +" : "");
        *style = demo->step != 0 ? TB_YELLOW : TB_GREEN;
    } else if (view == TB_CONSOLE) {
        (void)snprintf(text, capacity, "%s", demo->logs[index]);
        *style = TB_GREEN;
    } else if (view == TB_CALL_STACK) {
        (void)snprintf(text, capacity, "%d %-14s %s", index,
                       index == 0 ? "main" : "Reset_Handler",
                       index == 0 ? "main.c:30" : "startup.s:63");
        *style = TB_CYAN;
    } else if (view == TB_BREAKPOINTS) {
        (void)snprintf(text, capacity, "%c %-8s main.c:%d",
                       demo->breakpoints[index] ? '*' : '-',
                       demo->breakpoints[index] ? "enabled" : "none", index + 13);
        *style = demo->breakpoints[index] ? TB_PINK : TB_MUTED;
    } else if (view == TB_THREADS) {
        (void)snprintf(text, capacity, "1  %-10s main.c:30", demo->running ? "running" : "stopped");
        *style = TB_GREEN;
    } else if (view >= TB_SYMBOLS) {
        (void)snprintf(text, capacity, "%s", index == 0 ?
                       "Future view; not implemented." : "No target data is available.");
        *style = TB_ORANGE;
    } else {
        return false;
    }
    return true;
}

/* Read-only lexical coloring; this does not parse or evaluate source code. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): follows curses y/x convention.
void tb_render_source_text(int y, int x, int width, const char *code, bool selected)
{
    int offset = 0;
    while (code[offset] != '\0' && offset < width) {
        int start = offset++;
        TbStyle style = TB_PANEL;
        unsigned char c = (unsigned char)code[start];
        if (c == '/' && code[offset] == '/') {
            offset = (int)strlen(code);
            style = TB_MUTED;
        } else if (c == '#' || c == '"') {
            offset = (int)strlen(code);
            style = c == '#' ? TB_PINK : TB_YELLOW;
        } else if (isalnum(c) || c == '_') {
            while (isalnum((unsigned char)code[offset]) || code[offset] == '_') { ++offset; }
            int length = offset - start;
            if (isdigit(c)) { style = TB_PURPLE; }
            else if (code[offset] == '(') { style = TB_GREEN; }
            if ((length == 3 && strncmp(code + start, "int", 3) == 0) ||
                (length == 8 && strncmp(code + start, "uint32_t", 8) == 0)) { style = TB_CYAN; }
            if ((length == 5 && strncmp(code + start, "while", 5) == 0) ||
                (length == 3 && strncmp(code + start, "for", 3) == 0) ||
                (length == 6 && strncmp(code + start, "return", 6) == 0) ||
                (length == 8 && strncmp(code + start, "volatile", 8) == 0)) { style = TB_PINK; }
        }
        int length = offset - start;
        if (length > width - start) { length = width - start; }
        tb_text(y, x + start, length, selected ? tb_selected_style(style) : style, 0, code + start);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): follows curses y/x convention.
static void styled_line(TbView view, int y, int x, int width, const char *text,
                        TbStyle style, bool selected)
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
    int starts[4] = {0}, lengths[4] = {0};
    TbStyle styles[4] = {TB_PANEL, TB_PANEL, TB_PANEL, TB_PANEL};
    int count = 0;
    if (view == TB_DISASSEMBLY) {
        starts[0] = 2; lengths[0] = 8; styles[0] = TB_PURPLE;
        starts[1] = 12; lengths[1] = 5; styles[1] = TB_MUTED;
        starts[2] = 19; lengths[2] = (int)strcspn(text + 19, " "); styles[2] = TB_GREEN;
        count = 3;
    } else if (view == TB_REGISTERS) {
        lengths[0] = 7; styles[0] = TB_CYAN;
        starts[1] = 9; lengths[1] = width - 9; styles[1] = style == TB_YELLOW ? TB_YELLOW : TB_GREEN;
        count = 2;
    } else if (view == TB_MEMORY) {
        lengths[0] = 8; styles[0] = TB_PURPLE;
        starts[1] = 10; lengths[1] = 23; styles[1] = TB_PANEL;
        starts[2] = 35; lengths[2] = 8; styles[2] = TB_MUTED;
        count = 3;
    } else if (view == TB_LOCALS || view == TB_WATCHES) {
        lengths[0] = 12; styles[0] = TB_CYAN;
        starts[1] = 13; lengths[1] = 9; styles[1] = TB_MUTED;
        starts[2] = 23; lengths[2] = width - 23; styles[2] = style;
        count = 3;
    }
    int size = (int)strlen(text);
    for (int i = 0; i < count; ++i) {
        int length = lengths[i] < width - starts[i] ? lengths[i] : width - starts[i];
        if (starts[i] < size && length > 0) {
            tb_text(y, x + starts[i], length,
                    selected ? tb_selected_style(styles[i]) : styles[i], 0, text + starts[i]);
        }
    }
}

void tb_render_demo(const TbDemo *demo, int pane_index, TbRect rect)
{
    if (rect.w < 10 || rect.h < 7) {
        return;
    }
    const TbDemoPane *pane = &demo->panes[pane_index];
    TbView view = pane->tabs[pane->active];
    TbScroll scroll = tb_demo_scrollbar(demo, pane, rect.h - 3);
    int x = rect.x + 1, width = rect.w - 2;
    tb_fill((TbRect){rect.x, rect.y + 1, rect.w, 1}, TB_FOOTER);
    tb_text(rect.y + 1, x, width, TB_FOOTER, 0, heading(view));
    for (int row = 0; row < rect.h - 3; ++row) {
        char text[256];
        TbStyle style;
        int index = row + scroll.offset;
        if (!view_line(demo, view, index, text, sizeof text, &style)) {
            break;
        }
        bool selected = (view == TB_SOURCE && index == 17 + demo->step) ||
                        (view == TB_DISASSEMBLY && index == demo->step);
        if (selected) { tb_fill((TbRect){rect.x, rect.y + 2 + row, rect.w, 1}, TB_SELECTED); }
        styled_line(view, rect.y + 2 + row, x, width, text, style, selected);
    }
    if (scroll.maximum > 0) {
        for (int row = 0; row < scroll.visible; ++row) {
            bool thumb = row >= scroll.thumb_start && row < scroll.thumb_start + scroll.thumb_size;
            tb_text(rect.y + 2 + row, rect.x + rect.w - 1, 1,
                    thumb ? TB_CYAN : TB_MUTED, 0, thumb ? "#" : "|");
        }
    }
    static const char *const footers[] = {
        "main.c | UTF-8 | C", "Thumb-2 | simulated", "Cortex-M4F | core",
        "20000000 | HEX/ASCII", "Frame 0 | main", "GDB/MI | sample log"
    };
    int footer = rect.y + rect.h - 1;
    tb_fill((TbRect){rect.x, footer, rect.w, 1}, TB_FOOTER);
    tb_text(footer, x, width - 6, TB_FOOTER, 0,
            demo->running ? "Running | stale sample" : view <= TB_CONSOLE ? footers[view] : "Simulated preview");
    tb_text(footer, rect.x + rect.w - 5, 4, TB_FOOTER, A_BOLD, "DEMO");
}
