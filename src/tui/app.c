#include "the_bugger/tui.h"
#include "the_bugger/version.h"
#include "demo.h"
#include "render.h"
#include "theme.h"

#include <curses.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

enum { MAX_CONTROLS = 1024, FIELD_SIZE = 256, FORM_FIELDS = 9 };
typedef enum { LAUNCHER, CONFIGURE, REVIEW, WORKSPACE, RESET_CONFIRM } Page;
typedef enum {
    CHOOSE_SESSION, QUIT, SESSION_MENU, FIELD, SUBMIT, BACK, PREVIEW,
    TOOL, TAB, MENU, BODY, SHOW_VIEW, FOCUS_PANE, CANCEL_RESET, CONFIRM_RESET,
    MENU_SECTION, PANE_ACTION, DIVIDER, NEXT_PANE, PREVIOUS_PANE
} ControlKind;

typedef struct {
    TbRect rect;
    ControlKind kind;
    int value, pane;
} Control;

typedef struct {
    wchar_t values[FORM_FIELDS][FIELD_SIZE];
    bool alternate, start_server;
} Form;

typedef struct {
    const char *label;
    int index;
    bool toggle;
} Field;

typedef struct {
    Page page;
    TbDemo demo;
    TbLayout layout;
    Form forms[3];
    int session, focus, count, menu, form_scroll;
    size_t cursor;
    bool quit, previewed, menu_focus, menu_actions, resizing;
    int drag, resize_divider;
    Control controls[MAX_CONTROLS];
    TbRect panes[TB_MAX_PANES];
    const char *colors;
    char status[160];
} App;

/* Signal handler communicates only through this flag; cleanup runs normally. */
static volatile sig_atomic_t interrupted;

static const char *const session_names[] = {
    "Linux application", "Embedded target - OpenOCD", "Connect to GDB server"
};

static void handle_signal(int number)
{
    interrupted = number;
}

static void status(App *app, const char *message)
{
    (void)snprintf(app->status, sizeof app->status, "%s", message);
}

static void page(App *app, Page next)
{
    app->page = next;
    app->focus = 0;
    app->cursor = 0;
    app->form_scroll = 0;
    app->menu = -1;
    app->drag = -1;
    app->resizing = false;
}

static int add_control(App *app, TbRect rect, ControlKind kind, int value, int pane)
{
    if (app->count >= MAX_CONTROLS) {
        return -1;
    }
    int index = app->count++;
    app->controls[index] = (Control){rect, kind, value, pane};
    return index;
}

static void button(App *app, int y, int x, int width, const char *label,
                   ControlKind kind, int value, int pane)
{
    int index = add_control(app, (TbRect){x, y, width, 1}, kind, value, pane);
    if (index < 0) {
        return;
    }
    tb_fill((TbRect){x, y, width, 1}, index == app->focus ? TB_SELECTED : TB_SURFACE);
    tb_text(y, x, width, index == app->focus ? TB_SELECTED : TB_SURFACE,
            index == app->focus ? A_UNDERLINE | A_BOLD : 0, label);
}

static int fields(const App *app, Field list[FORM_FIELDS])
{
    int n = 0;
    const Form *form = &app->forms[app->session];
    if (app->session == 0) {
        list[n++] = (Field){"Mode", 0, true};
        list[n++] = (Field){form->alternate ? "PID (preview only)" : "Executable / ELF", 1, false};
        if (!form->alternate) {
            list[n++] = (Field){"Arguments (preview)", 2, false};
            list[n++] = (Field){"Working directory", 3, false};
        }
        list[n++] = (Field){"GDB binary", 5, false};
    } else if (app->session == 1) {
        list[n++] = (Field){"OpenOCD setup", 0, true};
        if (form->alternate) {
            list[n++] = (Field){"Adapter script", 2, false};
            list[n++] = (Field){"Target script", 3, false};
        } else {
            list[n++] = (Field){"Board script (sample)", 1, false};
        }
        list[n++] = (Field){"Symbols (ELF)", 4, false};
        list[n++] = (Field){"GDB binary", 5, false};
        list[n++] = (Field){"GDB endpoint", 6, false};
        list[n++] = (Field){"OpenOCD binary", 7, false};
        list[n++] = (Field){"Start OpenOCD", 8, true};
    } else {
        list[n++] = (Field){"GDB endpoint", 0, false};
        list[n++] = (Field){"Symbols (ELF)", 1, false};
        list[n++] = (Field){"GDB binary", 2, false};
    }
    return n;
}

static const wchar_t *field_value(const App *app, Field field)
{
    const Form *form = &app->forms[app->session];
    if (!field.toggle) {
        return form->values[field.index];
    }
    if (app->session == 0) {
        return form->alternate ? L"< Attach to PID >" : L"< Launch executable >";
    }
    if (field.index == 8) {
        return form->start_server ? L"< Yes - preview only >" : L"< No - existing server >";
    }
    return form->alternate ? L"< Adapter + target (advanced) >" : L"< Board script >";
}

static void wide_text(int y, int x, int width, const wchar_t *text, size_t start,
                      bool selected)
{
    if (y < 0 || y >= LINES || x < 0 || x >= COLS || width < 1) {
        return;
    }
    int available = COLS - x - 1;
    if (width > available) {
        width = available;
    }
    tb_style(selected ? TB_SELECTED : TB_SURFACE, selected ? A_UNDERLINE : 0);
    (void)move(y, x);
    int used = 0;
    for (size_t i = start; text[i] != L'\0'; ++i) {
        int cells = wcwidth(text[i]);
        if (cells < 1) {
            cells = 1;
        }
        if (used + cells > width) {
            break;
        }
        wchar_t character[2] = {text[i], L'\0'};
        (void)addnwstr(character, 1);
        used += cells;
    }
}

static void draw_header(const App *app, const char *subtitle)
{
    char text[160];
    tb_fill((TbRect){0, 0, COLS, 1}, TB_CHROME);
    tb_text(0, 1, 12, TB_BRAND, A_BOLD, "THE BUGGER");
    (void)snprintf(text, sizeof text, "v%s  |  %s", THE_BUGGER_VERSION, subtitle);
    tb_text(0, 14, COLS - 14, TB_CHROME, 0, text);
    tb_fill((TbRect){0, 2, COLS, 1}, TB_NOTICE);
    tb_text(2, 2, COLS - 4, TB_NOTICE, A_BOLD,
            "DEMO / VISUAL REFERENCE - no debugger connected");
    tb_text(LINES - 2, 1, COLS - 2, TB_YELLOW, 0, app->status);
    tb_text(LINES - 1, 1, COLS - 2, TB_MUTED, 0,
            "Tab / arrows / Enter / Esc | click controls | Ctrl+Q quit");
}

static void draw_launcher(App *app)
{
    draw_header(app, "Start a debugging session");
    int width = COLS > 82 ? 76 : COLS - 6;
    int left = (COLS - width) / 2;
    tb_fill((TbRect){left - 1, 4, width + 2, 11}, TB_PANEL);
    tb_text(4, left, width, TB_PANEL, A_BOLD, "Start a debugging session");
    const char *const descriptions[] = {
        "Launch or attach locally", "Board script or advanced adapter + target",
        "Existing remote endpoint; server stays external"
    };
    for (int i = 0; i < 3; ++i) {
        char text[96];
        (void)snprintf(text, sizeof text, "  %s", session_names[i]);
        button(app, 6 + i * 2, left, width, text, CHOOSE_SESSION, i, 0);
        tb_fill((TbRect){left, 7 + i * 2, width, 1}, TB_SURFACE);
        tb_text(7 + i * 2, left + 2, width - 4, TB_TAB, 0, descriptions[i]);
    }
    tb_text(13, left, width, TB_MUTED, 0, "Recent sessions: none saved in this preview.");
    if (app->previewed) {
        button(app, LINES - 4, 3, 22, "[ Return to preview ]", BACK, 0, 0);
    }
    button(app, LINES - 4, COLS - 12, 9, "[ Quit ]", QUIT, 0, 0);
}

static void draw_form(App *app, bool review)
{
    draw_header(app, review ? "Review session preview" : "Configure session");
    int width = COLS > 84 ? 80 : COLS - 4;
    int left = (COLS - width) / 2;
    tb_fill((TbRect){left - 1, 4, width + 2, LINES - 9}, TB_PANEL);
    tb_text(4, left, width, TB_CYAN, A_BOLD, session_names[app->session]);
    Field list[FORM_FIELDS];
    int n = fields(app, list);
    int cursor_y = -1, cursor_x = 0;
    int visible = LINES - 11;
    if (visible < 1) {
        visible = 1;
    }
    if (!review && app->focus < n) {
        if (app->focus < app->form_scroll) {
            app->form_scroll = app->focus;
        } else if (app->focus >= app->form_scroll + visible) {
            app->form_scroll = app->focus - visible + 1;
        }
    }
    for (int i = 0; i < n; ++i) {
        int y = 6 + i - app->form_scroll;
        bool shown = y >= 6 && y < 6 + visible;
        int input_x = left + (COLS >= 64 ? 24 : 21);
        TbRect rect = shown ? (TbRect){input_x, y, left + width - input_x, 1} : (TbRect){0};
        int index = review ? -1 : add_control(app, rect, FIELD, list[i].index, 0);
        if (!shown) {
            continue;
        }
        tb_text(y, left, input_x - left - 1, TB_MUTED, 0, list[i].label);
        const wchar_t *value = field_value(app, list[i]);
        size_t length = wcslen(value), start = 0;
        bool selected = index == app->focus && !review;
        if (selected && !list[i].toggle) {
            if (app->cursor > length) {
                app->cursor = length;
            }
            /* Keep the editing point visible, including wide characters. */
            int cells = 0;
            for (size_t p = app->cursor; p > 0; --p) {
                int w = wcwidth(value[p - 1]);
                cells += w > 0 ? w : 1;
                if (cells >= rect.w - 1) {
                    start = p;
                    break;
                }
            }
        }
        tb_fill(rect, selected ? TB_SELECTED : TB_SURFACE);
        wide_text(y, input_x, rect.w, value, start, selected);
        if (selected && !list[i].toggle) {
            cursor_x = input_x;
            for (size_t p = start; p < app->cursor; ++p) {
                int w = wcwidth(value[p]);
                cursor_x += w > 0 ? w : 1;
            }
            cursor_y = y;
        }
    }
    if (review) {
        tb_text(LINES - 5, 2, COLS - 4, TB_ORANGE, 0,
                "Preview only. Nothing will be launched, connected or saved.");
        button(app, LINES - 4, 2, 23, "[ Preview workspace ]", PREVIEW, 0, 0);
    } else {
        tb_text(LINES - 5, 2, COLS - 4, TB_MUTED, 0,
                "Sample configuration only; tool checks and execution come later.");
        button(app, LINES - 4, 2, 21, "[ Review preview ]", SUBMIT, 0, 0);
    }
    button(app, LINES - 4, COLS - 12, 10, "[ Back ]", BACK, 0, 0);
    if (cursor_y >= 0) {
        (void)curs_set(1);
        (void)move(cursor_y, cursor_x);
        (void)leaveok(stdscr, FALSE);
    }
}

static void draw_menu(App *app, int pane, TbRect rect)
{
    tb_text(rect.y + 1, rect.x + 1, rect.w - 2, TB_YELLOW, A_BOLD,
            app->menu_actions ? "PANE ACTIONS" : "SHOW VIEW (DEMO)");
    int start_control = app->count;
    if (app->menu_focus) {
        app->focus = start_control;
        app->menu_focus = false;
    }
    int height = rect.h - 4;
    if (height < 1) {
        return;
    }
    const char *const actions[] = {
        "Split right", "Split below", "Close selected tab", "Close split pane", "Resize dividers"
    };
    int count = app->menu_actions ? 5 : TB_VIEW_COUNT;
    int selected = app->focus - start_control;
    int scroll = selected >= 0 && selected < count ? selected / height * height : 0;
    for (int i = 0; i < count; ++i) {
        int y = rect.y + 2 + i - scroll;
        bool visible = i >= scroll && i < scroll + height;
        char text[96];
        (void)snprintf(text, sizeof text, "%s%s",
                       app->menu_actions ? actions[i] : tb_view_names[i],
                       !app->menu_actions && i >= TB_SYMBOLS ? " [not implemented]" : "");
        ControlKind kind = app->menu_actions ? PANE_ACTION : SHOW_VIEW;
        if (visible) {
            button(app, y, rect.x + 1, rect.w - 2, text, kind, i, pane);
        } else {
            (void)add_control(app, (TbRect){0}, kind, i, pane);
        }
    }
    tb_text(rect.y + rect.h - 2, rect.x + 1, rect.w - 2, TB_MUTED, 0, "Wheel/Up/Down | Esc close");
    button(app, rect.y + rect.h - 1, rect.x + 1, rect.w - 2,
            app->menu_actions ? "< Show views" : "Pane actions >", MENU_SECTION, 0, pane);
}

static void draw_pane(App *app, int pane_index, TbRect rect)
{
    if (rect.w < 10 || rect.h < 7) {
        return;
    }
    TbDemoPane *pane = &app->demo.panes[pane_index];
    tb_fill(rect, TB_PANEL);
    tb_fill((TbRect){rect.x, rect.y, rect.w, 1}, TB_SURFACE);
    int x = rect.x + 1;
    /* Active tab is always visible; additional tabs use the remaining width. */
    for (int pass = 0; pass < pane->count; ++pass) {
        int tab = (pane->active + pass) % pane->count;
        char label[64];
        (void)snprintf(label, sizeof label, " %s ",
                       tb_view_names[pane->tabs[tab]]);
        int width = (int)strlen(label);
        int available = rect.x + rect.w - 6 - x;
        if (available <= 0 || (pass > 0 && width > available)) {
            break;
        }
        if (width > available) {
            width = available;
        }
        int control = add_control(app, (TbRect){x, rect.y, width, 1}, TAB, tab, pane_index);
        TbStyle style = tab == pane->active ? TB_ACTIVE_TAB : TB_TAB;
        tb_fill((TbRect){x, rect.y, width, 1}, style);
        tb_text(rect.y, x, width, style,
                (tab == pane->active ? A_UNDERLINE | A_BOLD : 0) |
                (control == app->focus ? A_REVERSE : 0), label);
        x += width;
    }
    tb_text(rect.y, rect.x, 1, TB_ACTIVE_TAB, A_BOLD,
            pane_index == app->demo.pane ? ">" : " ");
    button(app, rect.y, rect.x + rect.w - 5, 4, "...", MENU, 0, pane_index);
    if (app->menu == pane_index) {
        draw_menu(app, pane_index, rect);
    } else {
        (void)add_control(app, (TbRect){rect.x + 1, rect.y + 2, rect.w - 2, rect.h - 3},
                          BODY, 0, pane_index);
        tb_render_demo(&app->demo, pane_index, rect);
    }
}

static void draw_workspace(App *app)
{
    tb_fill((TbRect){0, 0, COLS, 1}, TB_CHROME);
    tb_text(0, 1, 11, TB_BRAND, A_BOLD, "THE BUGGER");
    const char *const actions[] = {
        " Continue F5 ", " Pause ", " Over F10 ", " Into F11 ",
        " Out S-F11 ", " BP F9 ", " Reset "
    };
    int y = COLS >= 124 ? 0 : 1, x = COLS >= 124 ? 14 : 1;
    for (int i = 0; i < 7; ++i) {
        int width = (int)strlen(actions[i]);
        if (x + width > COLS - 1) {
            ++y;
            x = 1;
        }
        button(app, y, x, width, actions[i], TOOL, i, 0);
        const char *key = strchr(actions[i] + 1, ' ');
        if (key != NULL && key[1] != '\0') {
            tb_text(y, x + (int)(key - actions[i]) + 1, (int)strlen(key) - 2,
                    app->focus == i ? tb_selected_style(TB_YELLOW) : TB_KEY, 0, key + 1);
        }
        x += width + 1;
    }
    button(app, 0, COLS - 17, 9, " Session ", SESSION_MENU, 0, 0);
    button(app, 0, COLS - 7, 6, " Quit ", QUIT, 0, 0);
    ++y;
    tb_fill((TbRect){0, y, COLS, 1}, TB_NOTICE);
    tb_text(y, 1, COLS - 2, TB_NOTICE, 0,
            "DEMO / VISUAL REFERENCE  |  All values simulated. No debugger connected.");
    ++y;
    tb_fill((TbRect){0, y, COLS, 1}, TB_CHROME);
    tb_text(y, 1, COLS - 20, TB_TARGET, 0, session_names[app->session]);
    tb_text(y, COLS - 18, 17, TB_STATE, A_BOLD,
            app->demo.running ? "Running / stale" : "Paused (demo)");
    ++y;
    TbRect area = {1, y + 1, COLS - 2, LINES - y - 3};
    bool tiled = tb_layout_arrange(&app->layout, area, app->demo.pane);
    memcpy(app->panes, app->layout.panes, sizeof app->panes);
    if (!tiled) {
        const char *const labels[] = {"1 Source", "2 Asm", "3 Regs", "4 Mem", "5 Locals", "6 Log"};
        if (app->layout.count == TB_DEFAULT_PANES) {
            for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
                int pane = app->layout.order[i];
                if (pane == i) {
                    button(app, y, i * 8, 8, labels[i], FOCUS_PANE, pane, 0);
                } else {
                    char label[32];
                    (void)snprintf(label, sizeof label, "%d %.5s", pane + 1,
                                   tb_view_names[app->demo.panes[pane].tabs[app->demo.panes[pane].active]]);
                    button(app, y, i * 8, 8, label, FOCUS_PANE, pane, 0);
                }
            }
        } else {
            button(app, y, 1, 8, "< Prev", PREVIOUS_PANE, 0, 0);
            char label[96];
            (void)snprintf(label, sizeof label, "Pane %d | %.30s | %d total",
                           app->demo.pane + 1,
                           tb_view_names[app->demo.panes[app->demo.pane].tabs[app->demo.panes[app->demo.pane].active]],
                           app->layout.count);
            tb_text(y, 10, COLS - 20, TB_MUTED, 0, label);
            button(app, y, COLS - 9, 8, "Next >", NEXT_PANE, 0, 0);
        }
    }
    for (int i = 0; i < app->layout.count; ++i) {
        int pane = app->layout.order[i];
        draw_pane(app, pane, app->panes[pane]);
    }
    for (int i = 0; i < app->layout.divider_count; ++i) {
        TbDivider divider = app->layout.dividers[i];
        int control = add_control(app, divider.rect, DIVIDER, i, 0);
        if (app->resizing && app->resize_divider == i) { app->focus = control; }
        bool active = app->drag == i || (app->resizing && app->focus == control);
        if (active) {
            tb_fill(divider.rect, TB_SELECTED);
            tb_style(TB_ACTIVE_TAB, A_BOLD);
            if (divider.direction == TB_SPLIT_RIGHT) {
                (void)mvvline(divider.rect.y, divider.rect.x, '|', divider.rect.h);
            } else {
                (void)mvhline(divider.rect.y, divider.rect.x, '-', divider.rect.w);
            }
        }
    }
    char footer[256];
    (void)snprintf(footer, sizeof footer, "DEMO | %s | PC 0x%08X | %.16s | %.159s",
                   app->demo.running ? "Running / stale" : "Paused",
                   0x0800042aU + (unsigned)app->demo.step * 2U,
                   app->colors, app->status);
    tb_fill((TbRect){0, LINES - 2, COLS, 2}, TB_CHROME);
    tb_text(LINES - 2, 1, COLS - 2, TB_CHROME, 0, footer);
    tb_text(LINES - 1, 1, COLS - 2, TB_CHROME, 0,
            app->resizing ? "Resize: Tab selects divider | arrows move | Enter / Esc finish" :
            "Not connected | Cortex-M4F sample | ... pane actions | drag dividers | Ctrl+Q quit");
}

static void draw(App *app)
{
    app->count = 0;
    (void)curs_set(0);
    (void)leaveok(stdscr, TRUE);
    tb_fill((TbRect){0, 0, COLS, LINES}, TB_WORKSPACE);
    if (COLS < 48 || LINES < 18) {
        tb_text(0, 0, COLS, TB_ORANGE, A_BOLD, "DEMO: terminal too small");
        tb_text(2, 0, COLS, TB_PANEL, 0, "Resize to at least 48x18.");
        tb_text(4, 0, COLS, TB_PANEL, 0, "State retained. Ctrl+Q quits.");
    } else if (app->page == LAUNCHER) {
        draw_launcher(app);
    } else if (app->page == CONFIGURE || app->page == REVIEW) {
        draw_form(app, app->page == REVIEW);
    } else if (app->page == RESET_CONFIRM) {
        draw_header(app, "Reset DEMO state?");
        tb_text(6, 3, COLS - 6, TB_ORANGE, A_BOLD, "Reset simulated execution?");
        tb_text(8, 3, COLS - 6, TB_PANEL, 0, "No hardware or process will be reset.");
        button(app, 11, 3, 14, "[ Cancel ]", CANCEL_RESET, 0, 0);
        button(app, 11, 20, 22, "[ Reset DEMO state ]", CONFIRM_RESET, 0, 0);
    } else {
        draw_workspace(app);
    }
    if (app->focus >= app->count) {
        app->focus = app->count > 0 ? app->count - 1 : 0;
    }
    (void)refresh();
}

static void cycle_pane(App *app, int direction)
{
    for (int i = 0; i < app->layout.count; ++i) {
        if (app->layout.order[i] == app->demo.pane) {
            int next = (i + direction + app->layout.count) % app->layout.count;
            app->demo.pane = app->layout.order[next];
            app->menu = -1;
            app->focus = 0;
            return;
        }
    }
}

static void pane_action(App *app, int action, int pane)
{
    app->menu = -1;
    app->focus = 0;
    if (action < 2) {
        int added = tb_layout_split(&app->layout, pane,
                                    action == 0 ? TB_SPLIT_RIGHT : TB_SPLIT_BELOW);
        if (added < 0) {
            status(app, app->layout.count == TB_MAX_PANES ?
                    "Pane limit reached (32). Close a split before adding another." :
                    "Not enough space to split. Enlarge the terminal or resize dividers.");
            return;
        }
        memset(&app->demo.panes[added], 0, sizeof app->demo.panes[added]);
        app->demo.pane = added;
        tb_demo_view(&app->demo, TB_DISASSEMBLY);
        status(app, action == 0 ? "Pane split right." : "Pane split below.");
    } else if (action == 2) {
        status(app, tb_demo_close_tab(&app->demo) ? "Selected tab closed." :
                "Keep at least one tab per pane. Choose a different view in ...");
    } else if (action == 3) {
        int survivor = tb_layout_close(&app->layout, pane);
        if (survivor < 0) {
            status(app, "Only nested split panes can close. Keep the six workspace tiles.");
        } else {
            memset(&app->demo.panes[pane], 0, sizeof app->demo.panes[pane]);
            app->demo.pane = survivor;
            status(app, "Split pane closed; sibling expanded.");
        }
    } else if (app->layout.divider_count == 0) {
        status(app, "Enlarge the terminal to resize tiled dividers.");
    } else {
        app->resizing = true;
        app->resize_divider = 0;
        for (int i = 0; i < app->count; ++i) {
            if (app->controls[i].kind == DIVIDER) { app->focus = i; break; }
        }
        status(app, "Resize dividers with arrows; Tab selects another; Enter finishes.");
    }
}

static void activate(App *app, Control control)
{
    switch (control.kind) {
    case CHOOSE_SESSION:
        app->session = control.value;
        page(app, CONFIGURE);
        break;
    case QUIT: app->quit = true; break;
    case SESSION_MENU: page(app, LAUNCHER); break;
    case SUBMIT: page(app, REVIEW); break;
    case PREVIEW:
        app->previewed = true;
        status(app, "Preview selected. All values are simulated.");
        page(app, WORKSPACE);
        break;
    case BACK:
        page(app, app->page == REVIEW ? CONFIGURE :
             app->page == LAUNCHER ? WORKSPACE : LAUNCHER);
        break;
    case FIELD: {
        Form *form = &app->forms[app->session];
        bool toggle = (app->session < 2 && control.value == 0) ||
                      (app->session == 1 && control.value == 8);
        if (toggle) {
            if (control.value == 8) {
                form->start_server = !form->start_server;
            } else {
                form->alternate = !form->alternate;
            }
        } else {
            app->focus = (app->focus + 1) % app->count;
            app->cursor = 0;
        }
        break;
    }
    case TOOL:
        if (control.value == TB_RESET) {
            page(app, RESET_CONFIRM);
        } else {
            tb_demo_action(&app->demo, (TbDemoAction)control.value);
            status(app, app->demo.logs[app->demo.log_count - 1]);
        }
        break;
    case TAB:
        app->demo.pane = control.pane;
        app->demo.panes[control.pane].active = control.value;
        app->menu = -1;
        break;
    case MENU:
        app->demo.pane = control.pane;
        app->menu = app->menu == control.pane ? -1 : control.pane;
        app->menu_focus = app->menu >= 0;
        app->menu_actions = false;
        break;
    case BODY: app->demo.pane = control.pane; break;
    case SHOW_VIEW:
        app->demo.pane = control.pane;
        tb_demo_view(&app->demo, (TbView)control.value);
        app->menu = -1;
        app->focus = 0;
        break;
    case FOCUS_PANE:
        app->demo.pane = control.value;
        app->menu = -1;
        break;
    case NEXT_PANE: cycle_pane(app, 1); break;
    case PREVIOUS_PANE: cycle_pane(app, -1); break;
    case MENU_SECTION:
        app->menu_actions = !app->menu_actions;
        app->menu_focus = true;
        break;
    case PANE_ACTION: pane_action(app, control.value, control.pane); break;
    case DIVIDER:
        app->resizing = true;
        app->resize_divider = control.value;
        break;
    case CANCEL_RESET: page(app, WORKSPACE); break;
    case CONFIRM_RESET:
        tb_demo_action(&app->demo, TB_RESET);
        status(app, "DEMO reset confirmed.");
        page(app, WORKSPACE);
        break;
    }
}

static bool edit_field(App *app, Control control, wint_t key, bool special)
{
    if (control.kind != FIELD || app->page != CONFIGURE) {
        return false;
    }
    if ((app->session < 2 && control.value == 0) ||
        (app->session == 1 && control.value == 8)) {
        if (key == KEY_LEFT || key == KEY_RIGHT || key == ' ') {
            activate(app, control);
            return true;
        }
        return false;
    }
    wchar_t *value = app->forms[app->session].values[control.value];
    size_t length = wcslen(value);
    if (app->cursor > length) {
        app->cursor = length;
    }
    if (special && key == KEY_LEFT) {
        if (app->cursor > 0) { --app->cursor; }
    } else if (special && key == KEY_RIGHT) {
        if (app->cursor < length) { ++app->cursor; }
    } else if ((special && key == KEY_HOME) || (!special && key == 1)) {
        app->cursor = 0;
    } else if ((special && key == KEY_END) || (!special && key == 5)) {
        app->cursor = length;
    } else if ((special && key == KEY_BACKSPACE) || (!special && (key == 127 || key == 8))) {
        if (app->cursor > 0) {
            --app->cursor;
            wmemmove(value + app->cursor, value + app->cursor + 1, length - app->cursor);
        }
    } else if (special && key == KEY_DC) {
        if (app->cursor < length) {
            wmemmove(value + app->cursor, value + app->cursor + 1, length - app->cursor);
        }
    } else if (!special && key == 21) {
        value[0] = L'\0';
        app->cursor = 0;
    } else if (!special && iswprint(key) && wcwidth((wchar_t)key) > 0) {
        if (length < FIELD_SIZE - 1U) {
            wmemmove(value + app->cursor + 1, value + app->cursor, length - app->cursor + 1);
            value[app->cursor++] = (wchar_t)key;
        } else {
            status(app, "Preview field is full (255 characters).");
        }
    } else {
        return false;
    }
    return true;
}

static void scroll_body(App *app, Control control, int amount)
{
    app->demo.pane = control.pane;
    TbDemoPane *pane = &app->demo.panes[control.pane];
    TbView view = pane->tabs[pane->active];
    int limit = view == TB_SOURCE || view == TB_BREAKPOINTS ? TB_SOURCE_LINES :
                view == TB_MEMORY ? 32 : view == TB_REGISTERS ? 17 :
                view == TB_CONSOLE ? app->demo.log_count : 4;
    int row = pane->row[view] + amount;
    if (row < 0) { row = 0; }
    if (row >= limit) { row = limit - 1; }
    pane->row[view] = row;
    int height = app->panes[control.pane].h - 3;
    if (row < pane->scroll[view]) {
        pane->scroll[view] = row;
    } else if (height > 0 && row >= pane->scroll[view] + height) {
        pane->scroll[view] = row - height + 1;
    }
}

static void mouse_event(App *app)
{
    MEVENT event;
    if (getmouse(&event) != OK) {
        return;
    }
    if (app->page == WORKSPACE && app->drag >= 0) {
        if (event.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | REPORT_MOUSE_POSITION)) {
            if (app->drag < app->layout.divider_count) {
                TbDivider divider = app->layout.dividers[app->drag];
                (void)tb_layout_move_divider(&app->layout, app->drag,
                        divider.direction == TB_SPLIT_RIGHT ? event.x : event.y);
            }
            if (event.bstate & BUTTON1_RELEASED) {
                app->drag = -1;
                app->resizing = false;
                status(app, "Divider resized.");
            }
        }
        return;
    }
    if (app->page == WORKSPACE && (event.bstate & BUTTON1_PRESSED)) {
        int divider = tb_layout_divider_at(&app->layout, event.x, event.y);
        if (divider >= 0) {
            app->drag = divider;
            app->menu = -1;
            status(app, "Dragging divider; release to finish.");
            return;
        }
        app->resizing = false;
    }
    if (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        int direction = event.bstate & BUTTON4_PRESSED ? -1 : 1;
        if (app->page == REVIEW) {
            Field list[FORM_FIELDS];
            int count = fields(app, list);
            int next = app->form_scroll + direction;
            if (next >= 0 && next < count) { app->form_scroll = next; }
            return;
        }
        if (app->menu >= 0) {
            for (int i = 0; i < app->count; ++i) {
                if (app->controls[i].kind == SHOW_VIEW || app->controls[i].kind == PANE_ACTION) {
                    int selected = app->focus + direction;
                    int count = app->menu_actions ? 5 : TB_VIEW_COUNT;
                    if (selected >= i && selected < i + count) { app->focus = selected; }
                    return;
                }
            }
        }
    }
    for (int i = 0; i < app->count; ++i) {
        Control control = app->controls[i];
        if (!tb_rect_contains(control.rect, event.x, event.y)) {
            continue;
        }
        if (control.kind == BODY && (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))) {
            scroll_body(app, control, event.bstate & BUTTON4_PRESSED ? -3 : 3);
            return;
        }
        if (!(event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) {
            return;
        }
        app->focus = i;
        app->cursor = 0;
        bool text_field = control.kind == FIELD &&
                          !((app->session < 2 && control.value == 0) ||
                            (app->session == 1 && control.value == 8));
        if (text_field) {
            app->cursor = wcslen(app->forms[app->session].values[control.value]);
        } else {
            activate(app, control);
        }
        if (control.kind == BODY) {
            TbDemoPane *pane = &app->demo.panes[control.pane];
            TbView view = pane->tabs[pane->active];
            int row = event.y - app->panes[control.pane].y - 2;
            if (row >= 0) {
                int target = pane->scroll[view] + row;
                scroll_body(app, control, target - pane->row[view]);
                if (view == TB_SOURCE && event.x < control.rect.x + 2) {
                    tb_demo_action(&app->demo, TB_BREAKPOINT);
                    status(app, "DEMO gutter breakpoint toggled.");
                }
            }
        }
        return;
    }
}

static void key_event(App *app, wint_t key, bool special)
{
    if (!special && key == 3) {
        interrupted = SIGINT;
        return;
    }
    if (!special && key == 17) {
        app->quit = true;
        return;
    }
    if (special && key == KEY_RESIZE) {
        app->drag = -1;
        app->resizing = false;
        app->focus = 0;
        (void)clearok(stdscr, TRUE);
        return;
    }
    if (app->count == 0) { return; }
    if (special && key == KEY_MOUSE) {
        mouse_event(app);
        return;
    }
    Control control = app->controls[app->focus];
    if (app->resizing) {
        if ((!special && (key == 27 || key == '\n' || key == '\r')) ||
            (special && key == KEY_ENTER)) {
            app->resizing = false;
            app->drag = -1;
            app->focus = 0;
            status(app, "Divider resizing finished.");
        } else if ((!special && key == '\t') || (special && key == KEY_BTAB)) {
            int direction = key == KEY_BTAB ? -1 : 1;
            app->resize_divider = (app->resize_divider + direction + app->layout.divider_count) %
                                 app->layout.divider_count;
        } else if (special && control.kind == DIVIDER &&
                   (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN)) {
            TbDivider divider = app->layout.dividers[control.value];
            bool right = divider.direction == TB_SPLIT_RIGHT;
            if ((right && (key == KEY_LEFT || key == KEY_RIGHT)) ||
                (!right && (key == KEY_UP || key == KEY_DOWN))) {
                int position = right ? divider.rect.x : divider.rect.y;
                (void)tb_layout_move_divider(&app->layout, control.value,
                        position + (key == KEY_RIGHT || key == KEY_DOWN ? 1 : -1));
            }
        }
        return;
    }
    if (special && app->page == REVIEW && (key == KEY_UP || key == KEY_DOWN)) {
        Field list[FORM_FIELDS];
        int count = fields(app, list);
        int next = app->form_scroll + (key == KEY_DOWN ? 1 : -1);
        if (next >= 0 && next < count) { app->form_scroll = next; }
        return;
    }
    if (!special && key == 27) {
        if (app->menu >= 0) {
            app->menu = -1;
            app->focus = 0;
        } else if (app->page == LAUNCHER) {
            if (app->previewed) { page(app, WORKSPACE); } else { app->quit = true; }
        } else if (app->page == RESET_CONFIRM) {
            page(app, WORKSPACE);
        } else {
            page(app, app->page == REVIEW ? CONFIGURE : LAUNCHER);
        }
        return;
    }
    if ((!special && key == '\t') || (special && key == KEY_BTAB)) {
        app->focus = (app->focus + (key == KEY_BTAB ? app->count - 1 : 1)) % app->count;
        app->cursor = 0;
        return;
    }
    if (edit_field(app, control, key, special)) { return; }
    if (!special && app->page == WORKSPACE && key >= '1' && key <= '9') {
        int pane = (int)(key - '1');
        if (tb_layout_has_pane(&app->layout, pane)) { app->demo.pane = pane; }
        app->menu = -1;
        return;
    }
    if (!special && app->page == WORKSPACE && (key == '[' || key == ']')) {
        cycle_pane(app, key == ']' ? 1 : -1);
        return;
    }
    if ((!special && (key == '\n' || key == '\r' || key == ' ')) ||
        (special && key == KEY_ENTER)) {
        activate(app, control);
        return;
    }
    if (special && app->page == WORKSPACE && app->menu < 0) {
        const int function_keys[] = {KEY_F(5), 0, KEY_F(10), KEY_F(11), KEY_F(23), KEY_F(9)};
        for (int i = 0; i < 6; ++i) {
            if ((int)key == function_keys[i] && function_keys[i] != 0) {
                activate(app, (Control){.kind = TOOL, .value = i});
                return;
            }
        }
        if (control.kind == BODY &&
            (key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE)) {
            scroll_body(app, control, key == KEY_UP ? -1 : key == KEY_DOWN ? 1 :
                        key == KEY_PPAGE ? -5 : 5);
            return;
        }
        if (control.kind == TAB && (key == KEY_LEFT || key == KEY_RIGHT)) {
            TbDemoPane *pane = &app->demo.panes[control.pane];
            int next = (pane->active + (key == KEY_RIGHT ? 1 : pane->count - 1)) % pane->count;
            activate(app, (Control){.kind = TAB, .value = next, .pane = control.pane});
            return;
        }
    }
    if (special && (key == KEY_UP || key == KEY_LEFT || key == KEY_DOWN || key == KEY_RIGHT)) {
        bool previous = key == KEY_UP || key == KEY_LEFT;
        app->focus = (app->focus + (previous ? app->count - 1 : 1)) % app->count;
        app->cursor = 0;
    } else if (!special && key == 'q' && (app->page == WORKSPACE || app->page == LAUNCHER)) {
        app->quit = true;
    }
}

static void initialize(App *app)
{
    memset(app, 0, sizeof *app);
    app->menu = -1;
    app->drag = -1;
    tb_demo_init(&app->demo);
    tb_layout_init(&app->layout);
    const wchar_t *const samples[3][FORM_FIELDS] = {
        {L"", L"./build/debug-fixture", L"", L".", L"", L"gdb", L"", L"", L""},
        {L"", L"board/st_nucleo_f4.cfg", L"interface/stlink.cfg", L"target/stm32f4x.cfg",
         L"build/blinky.elf", L"arm-none-eabi-gdb", L"localhost:3333", L"openocd", L""},
        {L"localhost:3333", L"build/application.elf", L"gdb", L"", L"", L"", L"", L"", L""}
    };
    for (int form = 0; form < 3; ++form) {
        for (int field = 0; field < FORM_FIELDS; ++field) {
            (void)wcscpy(app->forms[form].values[field], samples[form][field]);
        }
        app->forms[form].start_server = true;
    }
    status(app, "DEMO only. Nothing is launched, connected or saved.");
}

int tb_tui_run(void)
{
    const char *term = getenv("TERM");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || term == NULL ||
        term[0] == '\0' || strcmp(term, "dumb") == 0) {
        fputs("the-bugger: an interactive terminal with a usable TERM is required.\n"
              "Run the-bugger --help or --version without a terminal.\n", stderr);
        return EXIT_FAILURE;
    }
    (void)setlocale(LC_ALL, "");
    interrupted = 0;
    const int signals[] = {SIGINT, SIGTERM, SIGHUP};
    struct sigaction saved[3];
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = handle_signal;
    (void)sigemptyset(&action.sa_mask);
    for (int i = 0; i < 3; ++i) {
        if (sigaction(signals[i], &action, &saved[i]) == -1) {
            for (int j = 0; j < i; ++j) { (void)sigaction(signals[j], &saved[j], NULL); }
            fputs("the-bugger: could not install terminal cleanup handlers.\n", stderr);
            return EXIT_FAILURE;
        }
    }
    /* Use the matching terminal description, never substitute xterm key/mouse
     * capabilities for a different terminal or assume tmux forwards RGB. */
    const char *color_term = getenv("COLORTERM");
    bool direct_alacritty = strcmp(term, "alacritty") == 0 && getenv("TMUX") == NULL &&
                            color_term != NULL &&
                            (strcmp(color_term, "truecolor") == 0 || strcmp(color_term, "24bit") == 0);
    SCREEN *screen = newterm(direct_alacritty ? "alacritty-direct" : NULL, stdout, stdin);
    if (screen == NULL && direct_alacritty) { screen = newterm(NULL, stdout, stdin); }
    int result = EXIT_FAILURE;
    if (screen == NULL) {
        fputs("the-bugger: terminal initialization failed; check TERM/terminfo.\n", stderr);
    } else {
        App app;
        initialize(&app);
        app.colors = tb_theme_init();
        /* Raw input also disables XON/XOFF, so Ctrl+Q reaches the application. */
        (void)raw();
        (void)noecho();
        (void)keypad(stdscr, TRUE);
        (void)set_escdelay(25);
        (void)mouseinterval(0);
        mmask_t supported = mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED |
                                      BUTTON4_PRESSED | BUTTON5_PRESSED |
                                      REPORT_MOUSE_POSITION, NULL);
        /* ncurses' XM commonly enables click mode 1000 only, even when its
         * event mask accepts REPORT_MOUSE_POSITION. Ask xterm-compatible
         * terminals/tmux for held-button motion, and undo it on every exit. */
        const char *mouse_sequence = tigetstr("kmous");
        bool drag_reporting = (supported & REPORT_MOUSE_POSITION) != 0 &&
                              mouse_sequence != NULL && mouse_sequence != (char *)-1 &&
                              (strcmp(mouse_sequence, "\033[<") == 0 ||
                               strcmp(mouse_sequence, "\033[M") == 0);
        if (drag_reporting) {
            (void)fputs("\033[?1002h", stdout);
            (void)fflush(stdout);
        }
        if (supported == 0) {
            status(&app, "Mouse unavailable: use Tab, arrows and Enter. DEMO only.");
        }
        (void)wtimeout(stdscr, 100);
        draw(&app);
        while (!app.quit && interrupted == 0) {
            wint_t key;
            int type = get_wch(&key);
            if (type != ERR) {
                key_event(&app, key, type == KEY_CODE_YES);
                if (!app.quit && interrupted == 0) { draw(&app); }
            }
        }
        if (drag_reporting) { (void)fputs("\033[?1002l", stdout); }
        (void)mousemask(0, NULL);
        (void)curs_set(1);
        (void)endwin();
        delscreen(screen);
        result = interrupted == 0 ? EXIT_SUCCESS : 128 + interrupted;
    }
    for (int i = 0; i < 3; ++i) { (void)sigaction(signals[i], &saved[i], NULL); }
    return result;
}
