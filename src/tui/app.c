#include "the_bugger/tui.h"
#include "the_bugger/version.h"
#include "demo.h"
#include "render.h"
#include "theme.h"
#include "native.h"

#include <curses.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

enum { MAX_CONTROLS = 1024, FIELD_SIZE = 256, FORM_FIELDS = 9 };
typedef enum { LAUNCHER, CONFIGURE, REVIEW, WORKSPACE, RESET_CONFIRM,
               COMMAND_INPUT, SAVE_INPUT, CLOSE_CONFIRM, CODE_MENU, CODE_INPUT, CODE_FILES, SOURCE_SETTINGS, BREAK_MENU, BREAK_INPUT } Page;
typedef enum { CODE_CHOOSE_FILE, CODE_FIND, CODE_LINE, CODE_ADDRESS, CODE_STOPPED,
               CODE_MIXED, CODE_SOURCE_SETTINGS, CODE_DIRECTORY_ADD, CODE_RULE_FROM,
               CODE_RULE_TO, CODE_SETTINGS_CLEAR, CODE_RAW, CODE_RUN_TO } CodeAction;
typedef enum {
    CHOOSE_SESSION, QUIT, SESSION_MENU, FIELD, SUBMIT, BACK, PREVIEW,
    TOOL, TAB, MENU, BODY, SHOW_VIEW, FOCUS_PANE, CANCEL_RESET, CONFIRM_RESET,
    MENU_SECTION, PANE_ACTION, DIVIDER, NEXT_PANE, PREVIOUS_PANE, SCROLLBAR,
    COMMAND_OPEN, SAVE_OPEN, INPUT_SUBMIT, CLOSE_ACCEPT, CONTEXT_REFRESH, CONTEXT_PAGE,
    CODE_ACTIONS, CODE_ACTION, CODE_PAGE, CODE_FILE_PICK, CODE_FILE_PAGE, HSCROLLBAR,
    BREAK_OPEN, BREAK_ACTION, REGISTER_REFRESH, REGISTER_FORMAT
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
    bool quit, previewed, menu_focus, menu_actions, resizing, keyboard_focus;
    int drag, resize_divider;
    int scroll_drag_pane, scroll_drag_grab;
    bool scroll_drag_horizontal;
    Control controls[MAX_CONTROLS];
    TbRect panes[TB_MAX_PANES];
    const char *colors;
    char status[160];
    TbNative *native;
    bool demo_mode, live, leave_session, exit_after_close;
    bool follow_log[TB_MAX_PANES];
    wchar_t input[FIELD_SIZE];
    bool instruction_mode;
    int code_pane, file_page, horizontal[TB_MAX_PANES][TB_VIEW_COUNT];
    int body_width[TB_MAX_PANES], body_height[TB_MAX_PANES];
    TbScroll horizontal_scroll[TB_MAX_PANES];
    CodeAction code_action;
    char rule_from[FIELD_SIZE * 4];
    uint64_t source_revision[TB_MAX_PANES], assembly_revision[TB_MAX_PANES];
    bool assembly_tail[TB_MAX_PANES];
    char assembly_previous[TB_MAX_PANES][48];
    int assembly_scroll_up[TB_MAX_PANES];
    int execution_pane, break_pane, break_action;
    bool break_hardware;
    char break_id[32];
} App;

static bool editing_page(const App *app)
{
    return app->page == COMMAND_INPUT || app->page == SAVE_INPUT || app->page == CODE_INPUT || app->page == CODE_FILES || app->page == BREAK_INPUT;
}

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
    app->scroll_drag_pane = -1;
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
        list[n++] = (Field){form->alternate ? (app->demo_mode ? "PID (preview only)" : "PID") : "Executable / ELF",
                           form->alternate && !app->demo_mode ? 6 : 1, false};
        if (!form->alternate) {
            list[n++] = (Field){app->demo_mode ? "Arguments (preview)" : "Arguments (quoted)", 2, false};
            list[n++] = (Field){"Working directory", 3, false};
            if (!app->demo_mode) { list[n++] = (Field){"Environment (NAME=value)", 7, false}; }
        }
        if (form->alternate && !app->demo_mode) { list[n++] = (Field){"Symbols ELF (optional)", 4, false}; }
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
            app->live ? "LIVE NATIVE | Console / Call Stack / Threads" :
            app->demo_mode || app->session != 0 ? "DEMO / VISUAL REFERENCE - no debugger connected" :
            "Native Linux | launch or attach through GDB");
    tb_text(LINES - 2, 1, COLS - 2, TB_YELLOW, 0, app->status);
    tb_text(LINES - 1, 1, COLS - 2, TB_MUTED, 0,
            "Tab / arrows / Enter | q / Esc back (outside text fields) | Ctrl+Q quit app");
}

static void draw_launcher(App *app)
{
    bool compact = LINES < 28;
    int width = COLS > 108 ? 100 : COLS - 8;
    int left = (COLS - width) / 2;
    int height = compact ? 15 : 23, top = (LINES - 2 - height) / 2;
    tb_fill((TbRect){left, top, width, height}, TB_PANEL);
    int title = top + (compact ? 0 : 1);
    tb_text(title, left + 2, width - 16, TB_PINK, A_BOLD, "THE BUGGER");
    char version[32]; (void)snprintf(version, sizeof version, "v%s", THE_BUGGER_VERSION);
    tb_text(title, left + width - 2 - (int)strlen(version), (int)strlen(version), TB_MUTED, 0, version);
    tb_style(TB_BORDER, 0); (void)mvhline(top + (compact ? 2 : 3), left + 2, ACS_HLINE, width - 4);
    tb_text(top + (compact ? 3 : 5), left + 2, width - 4, TB_PANEL, A_BOLD, "Start a debugging session");
    const char *const descriptions[] = {
        "Launch or attach to a native process", "Choose a board or configure adapter + target",
        "Use an existing remote debugging endpoint"
    };
    const char *const icons[] = {">_", "[]", "<>"};
    const TbStyle accents[] = {TB_GREEN, TB_CYAN, TB_ORANGE};
    for (int i = 0; i < 3; ++i) {
        int stride = compact ? 2 : 3, y = top + (compact ? 5 : 7) + i * stride;
        int control = add_control(app, (TbRect){left + 1, y, width - 2, stride}, CHOOSE_SESSION, i, 0);
        bool selected = control == app->focus;
        TbStyle surface = selected ? TB_SELECTED : TB_PANEL;
        tb_fill((TbRect){left + 1, y, width - 2, stride}, surface);
        tb_text(y, left + 1, 1, surface, A_BOLD, selected ? ">" : " ");
        tb_text(y, left + 3, 2, selected ? tb_selected_style(accents[i]) : accents[i], A_BOLD, icons[i]);
        tb_text(y, left + 7, width - 12, surface, selected ? A_BOLD : 0, session_names[i]);
        tb_text(y + 1, left + 7, width - 12, selected ? tb_selected_style(TB_MUTED) : TB_MUTED, 0, descriptions[i]);
        tb_text(y, left + width - 4, 1, selected ? tb_selected_style(TB_GREEN) : TB_MUTED, 0, ">");
    }
    int rule = top + (compact ? 11 : 17);
    tb_style(TB_BORDER, 0); (void)mvhline(rule, left + 2, ACS_HLINE, width - 4);
    tb_text(rule + 1, left + 2, width - 4, TB_MUTED, 0, app->demo_mode ?
            "Recent sessions: none saved in this preview." : "Recent sessions: profiles not implemented yet.");
    tb_text(rule + 2, left + 2, width - 4, TB_MUTED, 0, app->demo_mode ?
            "DEMO / VISUAL REFERENCE - no debugger connected" : "Native Linux | Server flows remain previews");
    int actions = top + height - 1;
    if (app->previewed) {
        button(app, actions, left + 2, 22, "[ Return to preview ]", BACK, 0, 0);
    }
    button(app, actions, left + width - 11, 9, "[ Quit ]", QUIT, 0, 0);
    tb_text(LINES - 2, 1, COLS - 2, TB_YELLOW, 0, app->status);
    tb_text(LINES - 1, 1, COLS - 2, TB_MUTED, 0, "Tab / arrows / Enter or click | Ctrl+Q quit app");
}

static void draw_form(App *app, bool review)
{
    bool native = !app->demo_mode && app->session == 0;
    draw_header(app, review ? (native ? "Review native session" : "Review session preview") : "Configure session");
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
                native ? (app->forms[0].alternate ? "Will attach and halt. Exit detaches/resumes; inputs remain on failure." :
                "Will load the ELF. Continue runs it; exit terminates a launched program.") :
                "Preview only. Nothing will be launched, connected or saved.");
        button(app, LINES - 4, 2, 23, native ? "[ Start native ]" : "[ Preview workspace ]", PREVIEW, 0, 0);
    } else {
        tb_text(LINES - 5, 2, COLS - 4, TB_MUTED, 0,
                native ? "Quotes group arguments/NAME=value entries. No shell expansion. Paths are literal." :
                "Sample configuration only; tool checks and execution come later.");
        button(app, LINES - 4, 2, 21, native ? "[ Review session ]" : "[ Review preview ]", SUBMIT, 0, 0);
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
            app->menu_actions ? "PANE ACTIONS" : app->live ? "SHOW VIEW" : "SHOW VIEW (DEMO)");
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
            int label_width = rect.w - (app->menu_actions ? 2 : 4);
            if ((size_t)label_width < strlen(text)) { text[label_width] = 0; }
            bool focused = app->count == app->focus;
            button(app, y, rect.x + 1, rect.w - 2, text, kind, i, pane);
            if (!app->menu_actions && tb_demo_has_view(&app->demo.panes[pane], (TbView)i)) {
                TbStyle style = focused ? TB_SELECTED : TB_SURFACE;
                tb_style(style, A_BOLD);
                if (MB_CUR_MAX > 1 && wcwidth(L'\u25cf') == 1) {
                    (void)mvaddnwstr(y, rect.x + rect.w - 3, L"\u25cf", 1);
                } else { tb_text(y, rect.x + rect.w - 3, 1, style, A_BOLD, "*"); }
            }
        } else {
            (void)add_control(app, (TbRect){0}, kind, i, pane);
        }
    }
    tb_text(rect.y + rect.h - 2, rect.x + 1, rect.w - 2, TB_MUTED, 0, "Wheel/Up/Down | Esc close");
    button(app, rect.y + rect.h - 1, rect.x + 1, rect.w - 2,
            app->menu_actions ? "< Show views" : "Pane actions >", MENU_SECTION, 0, pane);
}

static int assembly_rows(const TbCodeView *view);
static void context_row(const TbContextSnapshot *snapshot, TbView view, int item,
                        char *text, size_t capacity, bool *selected);

static void mixed_summary(const App *app, int index, const TbInstruction *instruction,
                          char *summary, size_t capacity)
{
    const TbCodeView *view = tb_code_view(tb_native_code(app->native), index);
    char text[1024] = {0};
    if (strcmp(instruction->source, view->path) == 0) {
        (void)tb_code_source_line(tb_native_code(app->native), index, (size_t)instruction->line - 1, text, sizeof text);
    }
    if (*text) { (void)snprintf(summary, capacity, "  L%d %.1023s", instruction->line, text); }
    else {
        char file[512]; tb_code_safe(file, sizeof file, instruction->source, strlen(instruction->source));
        (void)snprintf(summary, capacity, "  L%d %.511s <source text unavailable>", instruction->line, file);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): content length, viewport length, then position on that axis.
static TbScroll axis_scroll(int total, int visible, int offset)
{
    TbScroll scroll = {.total = total, .visible = visible};
    if (visible <= 0) { return scroll; }
    scroll.maximum = total > visible ? total - visible : 0;
    scroll.offset = offset < 0 ? 0 : offset > scroll.maximum ? scroll.maximum : offset;
    scroll.thumb_size = scroll.maximum ? visible * visible / total : visible;
    if (scroll.thumb_size < 1) { scroll.thumb_size = 1; }
    scroll.thumb_start = scroll.maximum ? scroll.offset * (visible - scroll.thumb_size) / scroll.maximum : 0;
    return scroll;
}

static TbScroll workspace_scroll(const App *app, const TbDemoPane *pane, int visible)
{
    if (!app->live) { return tb_demo_scrollbar(&app->demo, pane, visible); }
    int index = (int)(pane - app->demo.panes);
    if (app->body_height[index] > 0) { visible = app->body_height[index]; }
    TbView view = pane->tabs[pane->active];
    const TbContextSnapshot *context = tb_context_snapshot(tb_native_context(app->native));
    const TbCodeView *code = tb_code_view(tb_native_code(app->native), (int)(pane - app->demo.panes));
    int total = view == TB_CONSOLE ? (int)tb_native_log_count(app->native) :
                       view == TB_BREAKPOINTS ? (int)tb_break_count(tb_native_breakpoints(app->native)) :
                       view == TB_REGISTERS ? (int)tb_registers_count(tb_native_registers(app->native)) :
                       view == TB_SOURCE ? (int)code->line_count :
                       view == TB_DISASSEMBLY ? assembly_rows(code) :
                       view == TB_CALL_STACK && context->ready && !context->stale ? (int)context->frame_count :
                       view == TB_THREADS && context->ready && !context->stale ? (int)context->thread_count : 0;
    return axis_scroll(total, visible, pane->scroll[view]);
}

static void break_text(const TbBreakpoint *bp, char *text, size_t capacity)
{
    char raw[1800];
    (void)snprintf(raw, sizeof raw, "%s%s %c %s%s %.500s | %s | %.500s:%u | ignore %u hit %llu | %.500s",
                   bp->child ? "  " : "", bp->id, bp->enabled ? 'o' : '-', bp->hardware ? "HW " : "",
                   bp->pending ? "PENDING" : bp->multiple ? "MULTIPLE" : "",
                   bp->location, bp->address, bp->file, bp->line, bp->ignore,
                   (unsigned long long)bp->hits, bp->condition);
    tb_code_safe(text, capacity, raw, strlen(raw));
}

static void register_text(const TbRegister *row, char *text, size_t capacity)
{
    (void)snprintf(text, capacity, "%c %-16s %s%s%s", row->changed && row->fresh ? '*' : ' ', row->name,
                   *row->value ? row->value : "Awaiting read", row->fresh ? "" : " [stale]", row->truncated ? " [bounded]" : "");
}

static TbAssemblyColumns assembly_columns(const TbCodeView *view)
{
    TbAssemblyColumns columns = {10, 11, 7};
    for (size_t i = 0; i < view->instruction_count; ++i) {
        const TbInstruction *instruction = &view->instructions[i];
        int address = (int)strlen(instruction->address), bytes = (int)strlen(instruction->opcodes);
        int mnemonic = (int)strcspn(instruction->text, " \t");
        if (address > columns.address) { columns.address = address; }
        if (bytes > columns.bytes) { columns.bytes = bytes; }
        if (mnemonic > columns.mnemonic) { columns.mnemonic = mnemonic; }
    }
    return columns;
}

static int native_content_width(const App *app, int index)
{
    const TbDemoPane *pane = &app->demo.panes[index];
    TbView view = pane->tabs[pane->active];
    const TbCodeView *code = tb_code_view(tb_native_code(app->native), index);
    TbAssemblyColumns columns = assembly_columns(code);
    const TbContextSnapshot *context = tb_context_snapshot(tb_native_context(app->native));
    size_t rows = view == TB_REGISTERS ? tb_registers_count(tb_native_registers(app->native)) : view == TB_SOURCE ? code->line_count : view == TB_DISASSEMBLY ? code->instruction_count :
                  view == TB_CONSOLE ? tb_native_log_count(app->native) : view == TB_BREAKPOINTS ? tb_break_count(tb_native_breakpoints(app->native)) : view == TB_CALL_STACK && context->ready && !context->stale ? context->frame_count :
                  view == TB_THREADS && context->ready && !context->stale ? context->thread_count : 0;
    size_t maximum = 0;
    for (size_t row = 0; row < rows; ++row) {
        char text[4096]; bool selected;
        if (view == TB_REGISTERS) { register_text(tb_registers_row(tb_native_registers(app->native), row), text, sizeof text); }
        else if (view == TB_SOURCE) { (void)tb_code_source_line(tb_native_code(app->native), index, row, text, sizeof text); }
        else if (view == TB_DISASSEMBLY) {
            const TbInstruction *instruction = &code->instructions[row];
            tb_format_assembly(text, sizeof text, columns, instruction->address, instruction->opcodes, instruction->text);
        } else if (view == TB_CONSOLE) { (void)snprintf(text, sizeof text, "%s", tb_native_log_line(app->native, row)); }
        else if (view == TB_BREAKPOINTS) { break_text(tb_break_row(tb_native_breakpoints(app->native), row), text, sizeof text); }
        else { context_row(context, view, (int)row, text, sizeof text, &selected); }
        size_t length = strlen(text); if (length > maximum) { maximum = length; }
        if (view == TB_DISASSEMBLY && code->mixed && code->mixed_available && code->instructions[row].line > 0) {
            char summary[1200]; mixed_summary(app, index, &code->instructions[row], summary, sizeof summary);
            length = strlen(summary) - 2;
            if (length > maximum) { maximum = length; }
        }
    }
    return (int)maximum;
}

static int code_gutter(TbView view) { return view == TB_SOURCE ? 8 : view == TB_DISASSEMBLY ? 3 : 0; }

static void prepare_scrollbars(App *app, int index, TbRect rect)
{
    TbDemoPane *pane = &app->demo.panes[index]; TbView view = pane->tabs[pane->active];
    int width = native_content_width(app, index), gutter = code_gutter(view);
    int visible = rect.w - 2 - gutter;
    int height = rect.h - 3 - (width > visible ? 1 : 0);
    app->body_width[index] = rect.w - 2;
    app->body_height[index] = height;
    app->horizontal_scroll[index] = axis_scroll(width, visible, app->horizontal[index][view]);
    app->horizontal[index][view] = app->horizontal_scroll[index].offset;
}

static void draw_scrollbars(App *app, int index, TbRect rect)
{
    TbDemoPane *pane = &app->demo.panes[index]; TbView view = pane->tabs[pane->active];
    TbScroll vertical = workspace_scroll(app, pane, rect.h - 3);
    if (vertical.maximum > 0) {
        for (int row = 0; row < vertical.visible; ++row) {
            bool thumb = row >= vertical.thumb_start && row < vertical.thumb_start + vertical.thumb_size;
            tb_text(rect.y + 2 + row, rect.x + rect.w - 1, 1, thumb ? TB_CYAN : TB_MUTED, 0, thumb ? "#" : "|");
        }
        (void)add_control(app, (TbRect){rect.x + rect.w - 1, rect.y + 2, 1, vertical.visible}, SCROLLBAR, 0, index);
    }
    TbScroll horizontal = app->horizontal_scroll[index];
    if (horizontal.maximum > 0) {
        int x = rect.x + 1 + code_gutter(view), y = rect.y + rect.h - 2;
        for (int col = 0; col < horizontal.visible; ++col) {
            bool thumb = col >= horizontal.thumb_start && col < horizontal.thumb_start + horizontal.thumb_size;
            tb_text(y, x + col, 1, thumb ? TB_CYAN : TB_MUTED, 0, thumb ? "=" : "-");
        }
        (void)add_control(app, (TbRect){x, y, horizontal.visible, 1}, HSCROLLBAR, 0, index);
    }
}

static bool assembly_gap(const TbCodeView *view, size_t row)
{
    if (!row) { return false; }
    const TbInstruction *previous = &view->instructions[row - 1];
    return previous->bytes && previous->numeric <= UINT64_MAX - previous->bytes &&
           previous->numeric + previous->bytes < view->instructions[row].numeric;
}

static int assembly_rows(const TbCodeView *view)
{
    int rows = (int)view->instruction_count;
    for (size_t i = 1; i < view->instruction_count; ++i) {
        if (assembly_gap(view, i)) { ++rows; }
    }
    if (view->mixed && view->mixed_available) {
        for (size_t i = 0; i < view->instruction_count; ++i) {
            if (view->instructions[i].line > 0 && (i == 0 || view->instructions[i].line != view->instructions[i - 1].line ||
                strcmp(view->instructions[i].source, view->instructions[i - 1].source) != 0)) { ++rows; }
        }
    }
    return rows;
}

static void code_focus(App *app, int pane)
{
    if (!app->live) { return; }
    TbView view = app->demo.panes[pane].tabs[app->demo.panes[pane].active];
    if (view == TB_SOURCE || view == TB_DISASSEMBLY) { app->instruction_mode = view == TB_DISASSEMBLY; app->execution_pane = pane; }
}

static bool selected_location(App *app, char *location, size_t capacity, const TbBreakpoint **existing, TbBreakLocation *where)
{
    int id = app->execution_pane; TbDemoPane *pane = &app->demo.panes[id];
    const TbCodeView *code = tb_code_view(tb_native_code(app->native), id);
    TbBreakpoints *b = tb_native_breakpoints(app->native);
    if (!app->instruction_mode) {
        unsigned line = pane->row[TB_SOURCE] >= 0 ? (unsigned)pane->row[TB_SOURCE] + 1 : 0;
        *existing = tb_break_at(b, code->path, line, NULL);
        *where = (TbBreakLocation){.source = code->path, .line = line};
        return *code->path && line && line <= code->line_count;
    }
    int row = 0;
    for (size_t i = 0; i < code->instruction_count; ++i) {
        const TbInstruction *instruction = &code->instructions[i];
        if (assembly_gap(code, i)) { ++row; }
        if (code->mixed && code->mixed_available && instruction->line > 0 &&
            (!i || instruction->line != code->instructions[i - 1].line || strcmp(instruction->source, code->instructions[i - 1].source) != 0)) { ++row; }
        if (row++ != pane->row[TB_DISASSEMBLY]) { continue; }
        *existing = tb_break_at(b, NULL, 0, instruction->address);
        (void)snprintf(location, capacity, "*%s", instruction->address); *where = (TbBreakLocation){.text = location}; return !code->stale;
    }
    return false;
}
static bool toggle_breakpoint(App *app)
{
    char location[8192]; const TbBreakpoint *existing = NULL;
    TbBreakLocation where;
    TbBreakpoints *b = tb_native_breakpoints(app->native);
    bool okay = selected_location(app, location, sizeof location, &existing, &where) &&
                (existing ? tb_break_remove(b, existing->child ? existing->parent : existing->id) : tb_break_add_at(b, where, false));
    status(app, okay ? "Breakpoint change queued." : *tb_break_message(b) ? tb_break_message(b) : "Select a source line or instruction first.");
    return okay;
}

static int assembly_pc_row(const TbCodeView *view, const char *pc)
{
    uint64_t address; bool symbol;
    if (!tb_code_location(pc, &address, &symbol) || symbol) { return -1; }
    int row = 0;
    for (size_t i = 0; i < view->instruction_count; ++i) {
        const TbInstruction *instruction = &view->instructions[i];
        if (assembly_gap(view, i)) { ++row; }
        if (view->mixed && view->mixed_available && instruction->line > 0 &&
            (i == 0 || instruction->line != view->instructions[i - 1].line ||
             strcmp(instruction->source, view->instructions[i - 1].source) != 0)) { ++row; }
        if (instruction->numeric == address) { return row; }
        ++row;
    }
    return -1;
}

static void draw_code_pane(App *app, int index, TbRect rect)
{
    TbCode *code = tb_native_code(app->native);
    const TbCodeView *view = tb_code_view(code, index);
    const TbContextSnapshot *context = tb_context_snapshot(tb_native_context(app->native));
    TbDemoPane *pane = &app->demo.panes[index];
    bool source = pane->tabs[pane->active] == TB_SOURCE;
    bool stale = view->stale || context->stale;
    bool loaded = tb_native_state(app->native) == TB_NATIVE_READY;
    bool empty = source ? !view->line_count : !view->instruction_count;
    char header[512];
    const char *path = strrchr(view->path, '/'); path = path ? path + 1 : view->path;
    char label[256]; tb_code_safe(label, sizeof label, path, strlen(path));
    (void)snprintf(header, sizeof header, "%s | %s%s | %.240s",
                   loaded ? empty ? "Awaiting stop" : "BROWSE" : stale ? "STALE" : "LIVE",
                   context->selected.level > 0 ? "caller | " : "",
                   source ? "C/C++" : view->mixed && view->mixed_available ? "mixed" : "assembly", label);
    tb_text(rect.y + 1, rect.x + 1, rect.w - 2, stale ? TB_ORANGE : TB_MUTED, 0, header);
    TbScroll scroll = workspace_scroll(app, pane, rect.h - 3);
    uint64_t *seen = source ? &app->source_revision[index] : &app->assembly_revision[index];
    uint64_t revision = source ? view->source_revision : view->assembly_revision;
    if (*seen != revision) {
        *seen = revision;
        if (source && view->source_follow && view->selected_line > 0) {
            pane->scroll[TB_SOURCE] = view->selected_line > 4 ? view->selected_line - 4 : 0;
            pane->row[TB_SOURCE] = view->selected_line - 1;
        } else if (!source && view->assembly_follow) {
            int pc_row = assembly_pc_row(view, context->selected.address);
            if (pc_row >= 0) { pane->row[TB_DISASSEMBLY] = pc_row; }
            int margin = scroll.visible > 10 ? 5 : scroll.visible / 3;
            if (pc_row >= 0 && (pc_row < scroll.offset + margin || pc_row >= scroll.offset + scroll.visible - margin)) {
                if (pc_row < scroll.offset + margin) { pane->scroll[TB_DISASSEMBLY] = pc_row - margin; }
                else { pane->scroll[TB_DISASSEMBLY] = pc_row - scroll.visible + margin + 1; }
                if (pane->scroll[TB_DISASSEMBLY] < 0) { pane->scroll[TB_DISASSEMBLY] = 0; }
            }
        } else if (!source && app->assembly_tail[index]) {
            int previous = assembly_pc_row(view, app->assembly_previous[index]);
            pane->scroll[TB_DISASSEMBLY] = previous < 0 ? scroll.maximum :
                                         previous > app->assembly_scroll_up[index] ? previous - app->assembly_scroll_up[index] : 0;
        }
        if (!source) { app->assembly_tail[index] = false; }
        scroll = workspace_scroll(app, pane, rect.h - 3);
    }
    int output_row = 0, flat_row = 0;
    uint64_t selected_pc = 0, stopped_pc = 0; bool symbol;
    bool selected_valid = tb_code_location(context->selected.address, &selected_pc, &symbol) && !symbol;
    bool stopped_valid = tb_code_location(context->stopped.address, &stopped_pc, &symbol) && !symbol;
    size_t total = source ? view->line_count : view->instruction_count;
    size_t visible_first = total, visible_end = 0;
    TbAssemblyColumns columns = assembly_columns(view);
    for (size_t i = 0; i < total && output_row < scroll.visible; ++i) {
        const TbInstruction *instruction = source ? NULL : &view->instructions[i];
        if (!source && assembly_gap(view, i)) {
            if (flat_row >= scroll.offset && output_row < scroll.visible) {
                char gap[128];
                const TbInstruction *previous = &view->instructions[i - 1];
                unsigned long long bytes = (unsigned long long)(instruction->numeric - previous->numeric - previous->bytes);
                (void)snprintf(gap, sizeof gap, "... %llu bytes not decoded", bytes);
                tb_text(rect.y + 2 + output_row++, rect.x + 4, app->body_width[index] - 3, TB_MUTED, 0, gap);
            }
            ++flat_row;
        }
        bool mixed_header = !source && view->mixed && view->mixed_available && instruction->line > 0 &&
                            (i == 0 || instruction->line != view->instructions[i - 1].line ||
                             strcmp(instruction->source, view->instructions[i - 1].source) != 0);
        if (mixed_header) {
            if (flat_row >= scroll.offset && output_row < scroll.visible) {
                char summary[1200]; mixed_summary(app, index, instruction, summary, sizeof summary);
                size_t offset = (size_t)app->horizontal[index][TB_DISASSEMBLY];
                if (offset > strlen(summary) - 2) { offset = strlen(summary) - 2; }
                tb_text(rect.y + 2 + output_row++, rect.x + 4, app->body_width[index] - 3, TB_CYAN, 0, summary + 2 + offset);
            }
            ++flat_row;
        }
        if (flat_row++ < scroll.offset) { continue; }
        if (output_row >= scroll.visible) { break; }
        char text[4096]; bool stopped, selected;
        if (source) {
            (void)tb_code_source_line(code, index, i, text, sizeof text);
            stopped = (int)i + 1 == view->stopped_line;
            selected = (int)i + 1 == view->selected_line;
        } else {
            tb_format_assembly(text, sizeof text, columns, instruction->address, instruction->opcodes, instruction->text);
            if (visible_first == total) { visible_first = i; }
            visible_end = i + 1;
            stopped = stopped_valid && instruction->numeric == stopped_pc && strcmp(context->selected_thread, context->stopped_thread) == 0;
            selected = selected_valid && instruction->numeric == selected_pc;
        }
        stopped = stopped && !stale; selected = selected && !stale;
        bool inspected = selected && (!stopped || context->selected.level > 0 || strcmp(context->selected_thread, context->stopped_thread) != 0);
        bool highlighted = stopped || inspected;
        TbStyle style = highlighted ? tb_selected_style(stopped ? TB_YELLOW : TB_CYAN) : TB_PANEL;
        if (highlighted) { tb_fill((TbRect){rect.x + 1, rect.y + 2 + output_row, rect.w - 2, 1}, style); }
        char gutter[24];
        (void)snprintf(gutter, sizeof gutter, "%c%c%5zu ", source ? ' ' : stopped ? '>' : inspected ? '~' : ' ',
                       source ? stopped ? '>' : inspected ? '~' : ' ' : ' ', source ? i + 1 : i);
        if (!source) { (void)snprintf(gutter, sizeof gutter, " %c ", stopped ? '>' : inspected ? '~' : ' '); }
        const TbBreakpoint *bp = tb_break_at(tb_native_breakpoints(app->native), source ? view->path : NULL,
                                            source ? (unsigned)i + 1 : 0, source ? NULL : instruction->address);
        if (bp) { gutter[0] = bp->enabled ? 'o' : '-'; }
        int gutter_width = source ? 8 : 3;
        tb_text(rect.y + 2 + output_row, rect.x + 1, gutter_width, highlighted ? style : TB_MUTED, 0, gutter);
        if (bp) { tb_text(rect.y + 2 + output_row, rect.x + 1, 1, TB_PINK, 0, bp->enabled ? "o" : "-"); }
        size_t offset = (size_t)app->horizontal[index][source ? TB_SOURCE : TB_DISASSEMBLY];
        if (offset > strlen(text)) { offset = strlen(text); }
        if (source) { tb_render_source_text(rect.y + 2 + output_row, rect.x + 1 + gutter_width, app->body_width[index] - gutter_width, text + offset, highlighted); }
        else {
            tb_text(rect.y + 2 + output_row, rect.x + 4, app->body_width[index] - 3, style, 0, text + offset);
            if (!offset) { tb_text(rect.y + 2 + output_row, rect.x + 4, 18, highlighted ? tb_selected_style(TB_PURPLE) : TB_PURPLE, 0, instruction->address); }
        }
        ++output_row;
    }
    if (!total) {
        const char *error = source ? view->source_error : view->assembly_error;
        tb_text(rect.y + 3, rect.x + 2, rect.w - 4, TB_ORANGE, 0,
                *error ? error : "Stop a program to inspect live code.");
    }
    const char *notice = source ? "" : tb_code_assembly_notice(code, index, visible_first, visible_end > visible_first ? visible_end - visible_first : 0);
    if (*notice) {
        tb_text(rect.y + 1, rect.x + 1, rect.w - 2, TB_ORANGE, 0, notice);
    }
    if (!source && total && *view->assembly_error) {
        tb_text(rect.y + 1, rect.x + 1, rect.w - 2, TB_ORANGE, 0, view->assembly_error);
    }
    button(app, rect.y + rect.h - 1, rect.x + 1, 9, " Actions ", CODE_ACTIONS, 0, index);
    if (source) {
        button(app, rect.y + rect.h - 1, rect.x + 11, 9, " Stopped ", CODE_ACTION, CODE_STOPPED, index);
        if (view->truncated) { tb_text(rect.y + rect.h - 1, rect.x + 21, rect.w - 22, TB_ORANGE, 0, "bounded preview"); }
        else { tb_text(rect.y + rect.h - 1, rect.x + 21, rect.w - 22, TB_MUTED, 0, "> PC | ~ frame"); }
    } else {
        button(app, rect.y + rect.h - 1, rect.x + 11, 7, view->mixed ? " Mixed+" : " Mixed ", CODE_ACTION, CODE_MIXED, index);
        button(app, rect.y + rect.h - 1, rect.x + 19, 3, " < ", CODE_PAGE, -1, index);
        button(app, rect.y + rect.h - 1, rect.x + 23, 3, " > ", CODE_PAGE, 1, index);
        if (view->mixed && !view->mixed_available && total) {
            tb_text(rect.y + 1, rect.x + 1, rect.w - 2, TB_MUTED, 0, "Assembly only | mixed data unavailable");
        }
    }
}


// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): view kind and its row index identify one bounded display row.
static void context_row(const TbContextSnapshot *snapshot, TbView view, int item,
                        char *text, size_t capacity, bool *selected)
{
    if (view == TB_CALL_STACK) {
        const TbFrame *frame = &snapshot->frames[item];
        *selected = frame->level == snapshot->selected.level;
        bool stopped = strcmp(snapshot->selected_thread, snapshot->stopped_thread) == 0 && frame->level == 0;
        char location[416];
        const char *file = *frame->file ? frame->file : *frame->from ? frame->from : "<source unavailable>";
        if (frame->line > 0) { (void)snprintf(location, sizeof location, "%.384s:%d", file, frame->line); }
        else { (void)snprintf(location, sizeof location, "%.384s", file); }
        (void)snprintf(text, capacity, "%c%c #%d %.100s(%.120s) | %s | %.47s",
                       *selected ? '>' : ' ', stopped ? '*' : ' ', frame->level,
                       *frame->function ? frame->function : "<unknown>", frame->arguments, location, frame->address);
    } else {
        const TbThread *thread = &snapshot->threads[item];
        *selected = strcmp(thread->id, snapshot->selected_thread) == 0;
        (void)snprintf(text, capacity, "%c T%.31s %.23s %.95s | %.159s | %.127s",
                       *selected ? '>' : ' ', thread->id, thread->state, thread->name,
                       *thread->frame.function ? thread->frame.function : "<frame unavailable>", thread->target);
    }
}

static void draw_context_pane(App *app, int index, TbRect rect)
{
    TbDemoPane *pane = &app->demo.panes[index];
    TbView view = pane->tabs[pane->active];
    const TbContextSnapshot *snapshot = tb_context_snapshot(tb_native_context(app->native));
    bool stack = view == TB_CALL_STACK;
    char header[256];
    (void)snprintf(header, sizeof header, "%s | T%.20s frame #%d | %s | stop T%.20s PC %.47s",
                   snapshot->stale ? "STALE" : "LIVE", snapshot->selected_thread,
                   snapshot->selected.level, snapshot->selected.level > 0 ? "inspected caller" : "selected context",
                   snapshot->stopped_thread, snapshot->stopped.address);
    if (snapshot->selected.level < 0) {
        (void)snprintf(header, sizeof header, "%s | selected context unavailable",
                       snapshot->stale ? "STALE" : "LIVE");
    }
    tb_text(rect.y + 1, rect.x + 1, rect.w - 2, snapshot->stale ? TB_ORANGE : TB_CYAN, 0, header);
    button(app, rect.y + rect.h - 1, rect.x + 1, 9, " Refresh ", CONTEXT_REFRESH, 0, index);
    if (stack && rect.w >= 36) {
        button(app, rect.y + rect.h - 1, rect.x + 11, 9, " < Frames", CONTEXT_PAGE, -1, index);
        button(app, rect.y + rect.h - 1, rect.x + 21, 9, " Frames >", CONTEXT_PAGE, 1, index);
    }
    TbScroll scroll = workspace_scroll(app, pane, rect.h - 3);
    if (!snapshot->ready || snapshot->stale) {
        const char *state = tb_native_state(app->native) == TB_NATIVE_RUNNING ? "Running: retained context is stale." :
                            tb_native_state(app->native) == TB_NATIVE_STOPPED && snapshot->refreshing ?
                            "Refreshing stopped context..." : "Stop a running program to inspect context.";
        tb_text(rect.y + 2, rect.x + 1, rect.w - 2, TB_ORANGE, 0, state);
        return;
    }
    const char *error = *snapshot->error ? snapshot->error :
                        stack && *snapshot->stack_error ? snapshot->stack_error :
                        stack && *snapshot->arguments_error ? snapshot->arguments_error : NULL;
    for (int row = 0; row < scroll.visible && row + scroll.offset < scroll.total; ++row) {
        int item = scroll.offset + row;
        char text[1024]; bool selected;
        context_row(snapshot, view, item, text, sizeof text, &selected);
        size_t offset = (size_t)app->horizontal[index][view];
        if (offset > strlen(text)) { offset = strlen(text); }
        TbStyle style = selected ? tb_selected_style(TB_CYAN) : TB_PANEL;
        if (selected) { tb_fill((TbRect){rect.x + 1, rect.y + 2 + row, rect.w - 2, 1}, style); }
        tb_text(rect.y + 2 + row, rect.x + 1, app->body_width[index], style,
                app->keyboard_focus && pane->row[view] == item ? A_UNDERLINE : 0, text + offset);
    }
    if (!scroll.total) {
        tb_text(rect.y + 2, rect.x + 1, rect.w - 2, TB_MUTED, 0, stack ? "No frames available from GDB." : "No threads available from GDB.");
    }
    if (error) {
        tb_text(rect.y + 1, rect.x + 1, rect.w - 2, TB_ORANGE, 0, error);
    } else if (stack && rect.w >= 52) {
        tb_text(rect.y + rect.h - 1, rect.x + 31, rect.w - 32, TB_MUTED, 0,
                "> selected | * stopped | click / Enter");
    } else if (!stack && rect.w >= 34) {
        tb_text(rect.y + rect.h - 1, rect.x + 11, rect.w - 12, TB_MUTED, 0,
                snapshot->threads_limited ? "First 128 threads | click / Enter" : "Click / arrows + Enter select");
    }
}

static void select_context_row(App *app, int pane_index)
{
    if (!app->live) { return; }
    TbDemoPane *pane = &app->demo.panes[pane_index];
    TbView view = pane->tabs[pane->active];
    TbContext *context = tb_native_context(app->native);
    const TbContextSnapshot *snapshot = tb_context_snapshot(context);
    int row = pane->row[view];
    bool okay = false;
    if (row >= 0 && snapshot->ready && !snapshot->stale) {
        if (view == TB_CALL_STACK && (size_t)row < snapshot->frame_count) {
            okay = tb_context_select_frame(context, snapshot->frames[row].level);
        } else if (view == TB_THREADS && (size_t)row < snapshot->thread_count) {
            okay = tb_context_select_thread(context, snapshot->threads[row].id);
        } else { return; }
    }
    if (view == TB_CALL_STACK || view == TB_THREADS) {
        status(app, okay ? "Selecting debugger context; execution is unchanged." : "Wait for a fresh stopped context before selecting.");
    }
}


static void draw_native_pane(App *app, int index, TbRect rect)
{
    TbDemoPane *pane = &app->demo.panes[index];
    TbView view = pane->tabs[pane->active];
    if (view == TB_REGISTERS) {
        TbRegisters *r = tb_native_registers(app->native); bool stale = tb_registers_stale(r);
        char header[256]; const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(app->native));
        (void)snprintf(header, sizeof header, "%s | registers | T%s frame %d | * changed%s",
                       stale ? "STALE" : "LIVE", s->selected_thread, s->selected.level,
                       tb_registers_limited(r) ? " | limited" : "");
        tb_text(rect.y + 1, rect.x + 1, rect.w - 2, stale ? TB_ORANGE : TB_MUTED, 0,
                *tb_registers_error(r) ? tb_registers_error(r) : header);
        TbScroll scroll = workspace_scroll(app, pane, rect.h - 3);
        tb_registers_want(r, (size_t)scroll.offset, (size_t)scroll.visible);
        for (int i = 0; i < scroll.visible && i + scroll.offset < scroll.total; ++i) {
            const TbRegister *row = tb_registers_row(r, (size_t)(i + scroll.offset)); char text[1400];
            register_text(row, text, sizeof text);
            size_t offset = (size_t)app->horizontal[index][TB_REGISTERS]; if (offset > strlen(text)) { offset = strlen(text); }
            TbStyle style = stale || !row->fresh || !row->available ? TB_MUTED : row->changed ? TB_YELLOW : TB_GREEN;
            tb_text(rect.y + 2 + i, rect.x + 1, app->body_width[index], style, 0, text + offset);
        }
        if (!scroll.total) { tb_text(rect.y + 3, rect.x + 2, rect.w - 4, TB_MUTED, 0, "Stop a program to discover its registers."); }
        button(app, rect.y + rect.h - 1, rect.x + 1, 9, " Refresh ", REGISTER_REFRESH, 0, index);
        char format[32]; (void)snprintf(format, sizeof format, " %s ", tb_registers_format_name(tb_registers_get_format(r)));
        button(app, rect.y + rect.h - 1, rect.x + 11, (int)strlen(format), format, REGISTER_FORMAT, 0, index);
        tb_text(rect.y + rect.h - 1, rect.x + 22, rect.w - 23, TB_MUTED, 0, "All (ungrouped)"); return;
    }
    if (view == TB_BREAKPOINTS) {
        TbBreakpoints *b = tb_native_breakpoints(app->native);
        tb_text(rect.y + 1, rect.x + 1, rect.w - 2, TB_CYAN, 0, tb_break_ready(b) ? "LIVE | code breakpoints | o enabled / - disabled" : "Refreshing breakpoint table...");
        if (tb_break_busy(b) && *tb_break_message(b)) { tb_text(rect.y + 1, rect.x + 1, rect.w - 2, TB_ORANGE, 0, tb_break_message(b)); }
        TbScroll scroll = workspace_scroll(app, pane, rect.h - 3);
        for (int row = 0; row < scroll.visible && row + scroll.offset < scroll.total; ++row) {
            int item = row + scroll.offset; const TbBreakpoint *bp = tb_break_row(b, (size_t)item);
            char safe[4096]; break_text(bp, safe, sizeof safe);
            size_t offset = (size_t)app->horizontal[index][view]; if (offset > strlen(safe)) { offset = strlen(safe); }
            tb_text(rect.y + 2 + row, rect.x + 1, rect.w - 2, TB_PANEL, pane->row[view] == item ? A_UNDERLINE : 0, safe + offset);
        }
        if (!scroll.total) { tb_text(rect.y + 3, rect.x + 2, rect.w - 4, TB_MUTED, 0, "No code breakpoints. Use F9/gutter or Manage."); }
        button(app, rect.y + rect.h - 1, rect.x + 1, 10, " Manage ", BREAK_OPEN, 0, index); return;
    }
    if (view == TB_SOURCE || view == TB_DISASSEMBLY) { draw_code_pane(app, index, rect); return; }
    if (view == TB_CALL_STACK || view == TB_THREADS) {
        draw_context_pane(app, index, rect); return;
    }
    tb_text(rect.y + 1, rect.x + 1, rect.w - 2, TB_MUTED, 0,
            view == TB_CONSOLE ? "LIVE | GDB / program log" : "LIVE | inspection unavailable");
    if (view == TB_CONSOLE) {
        TbScroll scroll = workspace_scroll(app, pane, rect.h - 3);
        if (app->follow_log[index]) { pane->scroll[view] = scroll.maximum; scroll = workspace_scroll(app, pane, rect.h - 3); }
        for (int i = 0; i < scroll.visible && scroll.offset + i < scroll.total; ++i) {
            const char *text = tb_native_log_line(app->native, (size_t)scroll.offset + (size_t)i);
            size_t offset = (size_t)app->horizontal[index][view];
            if (offset > strlen(text)) { offset = strlen(text); }
            tb_text(rect.y + 2 + i, rect.x + 1, app->body_width[index], TB_PANEL, 0, text + offset);
        }
        button(app, rect.y + rect.h - 1, rect.x + 1, 9, " Command ", COMMAND_OPEN, 0, index);
        button(app, rect.y + rect.h - 1, rect.x + 11, rect.w < 22 ? rect.w - 12 : 10, " Save Log ", SAVE_OPEN, 0, index);
    } else {
        tb_text(rect.y + 3, rect.x + 2, rect.w - 4, TB_MUTED, 0, "Live view not implemented yet.");
        tb_text(rect.y + 4, rect.x + 2, rect.w - 4, TB_MUTED, 0, "Use Console for GDB commands.");
    }
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
                (app->keyboard_focus && control == app->focus ? A_REVERSE : 0), label);
        x += width;
    }
    tb_text(rect.y, rect.x, 1, TB_ACTIVE_TAB, A_BOLD,
            pane_index == app->demo.pane ? ">" : " ");
    button(app, rect.y, rect.x + rect.w - 5, 4, "...", MENU, 0, pane_index);
    if (app->menu == pane_index) {
        draw_menu(app, pane_index, rect);
    } else {
        if (app->live) { prepare_scrollbars(app, pane_index, rect); }
        (void)add_control(app, (TbRect){rect.x + 1, rect.y + 2,
                          app->live ? app->body_width[pane_index] : rect.w - 2,
                          app->live ? app->body_height[pane_index] : rect.h - 3},
                          BODY, 0, pane_index);
        if (app->live) { draw_native_pane(app, pane_index, rect); }
        else { tb_render_demo(&app->demo, pane_index, rect); }
        if (app->live) { draw_scrollbars(app, pane_index, rect); return; }
        TbScroll scroll = workspace_scroll(app, pane, rect.h - 3);
        if (scroll.maximum > 0) {
            (void)add_control(app, (TbRect){rect.x + rect.w - 1, rect.y + 2, 1, rect.h - 3},
                              SCROLLBAR, 0, pane_index);
        }
    }
}

static void draw_workspace(App *app)
{
    tb_fill((TbRect){0, 0, COLS, 1}, TB_CHROME);
    tb_text(0, 1, 11, TB_BRAND, A_BOLD, "THE BUGGER");
    const char *const actions[] = {
        " Continue F5 ", " Pause ", " Over F10 ", " Into F11 ",
        " Out Shift+F11 ", " BP F9 ", " Reset "
    };
    int y = COLS >= 124 ? 0 : 1, x = COLS >= 124 ? 14 : 1;
    for (int i = 0; i < 7; ++i) {
        int width = (int)strlen(actions[i]);
        if (x + width > COLS - (y == 0 ? 18 : 1)) {
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
            app->live ? (app->instruction_mode ? "LIVE NATIVE | instruction stepping | live code, context and console" :
                         "LIVE NATIVE | source stepping | live code, context and console") :
            "DEMO / VISUAL REFERENCE  |  All values simulated. No debugger connected.");
    ++y;
    tb_fill((TbRect){0, y, COLS, 1}, TB_CHROME);
    tb_text(y, 1, COLS - 20, TB_TARGET, 0, session_names[app->session]);
    tb_text(y, COLS - 18, 17, TB_STATE, A_BOLD,
            app->live ? tb_native_state_name(app->native) : app->demo.running ? "Running / stale" : "Paused (demo)");
    ++y;
    TbRect area = {1, y + 1, COLS - 2, LINES - y - 3};
    bool tiled = tb_layout_arrange(&app->layout, area, app->demo.pane);
    memcpy(app->panes, app->layout.panes, sizeof app->panes);
    if (app->live) {
        bool threads = false, frames = false, registers = false;
        for (int i = 0; i < app->layout.count; ++i) {
            int pane = app->layout.order[i];
            TbView view = app->demo.panes[pane].tabs[app->demo.panes[pane].active];
            if (app->panes[pane].w < 10 || app->panes[pane].h < 7 || app->menu == pane) { continue; }
            threads = threads || view == TB_THREADS;
            frames = frames || view == TB_CALL_STACK;
            registers = registers || view == TB_REGISTERS;
        }
        tb_registers_visible(tb_native_registers(app->native), registers);
        tb_registers_begin(tb_native_registers(app->native));
        tb_context_visible(tb_native_context(app->native), threads, frames);
        for (int pane = 0; pane < TB_MAX_PANES; ++pane) {
            bool visible = app->panes[pane].w >= 10 && app->panes[pane].h >= 7 && app->menu != pane;
            TbView view = app->demo.panes[pane].tabs[app->demo.panes[pane].active];
            tb_code_visible(tb_native_code(app->native), pane, visible && view == TB_SOURCE, visible && view == TB_DISASSEMBLY);
        }
    }
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
    if (app->live) {
        char target[32] = "not started";
        if (tb_native_target_pid(app->native) > 0) {
            (void)snprintf(target, sizeof target, "%ld", (long)tb_native_target_pid(app->native));
        }
        (void)snprintf(footer, sizeof footer, "LIVE | %s | PID %s | %.159s",
                       tb_native_state_name(app->native), target, app->status);
    } else { (void)snprintf(footer, sizeof footer, "DEMO | %s | PC 0x%08X | %.16s | %.159s",
                   app->demo.running ? "Running / stale" : "Paused",
                   0x0800042aU + (unsigned)app->demo.step * 2U,
                   app->colors, app->status); }
    tb_fill((TbRect){0, LINES - 2, COLS, 2}, TB_CHROME);
    tb_text(LINES - 2, 1, COLS - 2, TB_CHROME, 0, footer);
    tb_text(LINES - 1, 1, COLS - 2, TB_CHROME, 0,
            app->resizing ? "Resize: Tab selects divider | arrows move | Enter / Esc finish" :
            app->live ? "Console: Command / Save Log | q close session | Ctrl+Q quit app" :
            "DEMO | ... pane actions | drag dividers | q launcher | Ctrl+Q quit app");
}

static bool field_utf8(const wchar_t *value, char *target, size_t capacity);

static const char *const code_labels[] = {
    "Choose source file", "Find in current file", "Go to line", "Go to address / symbol",
    "Return to stopped context", "Toggle mixed source/assembly", "Source lookup settings",
    "Add source directory", "Mapping: build prefix", "Mapping: local prefix", "Clear source overrides", "Decode bytes (raw inspection)", "Run to Selected"
};

static void draw_code_menu(App *app)
{
    draw_header(app, "Code view actions");
    bool source = app->demo.panes[app->code_pane].tabs[app->demo.panes[app->code_pane].active] == TB_SOURCE;
    const CodeAction source_actions[] = {CODE_CHOOSE_FILE, CODE_FIND, CODE_LINE, CODE_STOPPED, CODE_SOURCE_SETTINGS, CODE_RUN_TO};
    const CodeAction asm_actions[] = {CODE_ADDRESS, CODE_STOPPED, CODE_MIXED, CODE_SOURCE_SETTINGS, CODE_RAW, CODE_RUN_TO};
    int count = 6;
    for (int i = 0; i < count; ++i) {
        CodeAction action = source ? source_actions[i] : asm_actions[i];
        button(app, 5 + i, 3, COLS - 6, code_labels[action], CODE_ACTION, action, app->code_pane);
    }
    tb_text(LINES - 5, 3, COLS - 6, TB_MUTED, 0, "Browsing never moves execution. Left/Right scroll long code lines.");
    button(app, LINES - 4, 3, 12, "[ Back ]", BACK, 0, 0);
}

static void draw_source_settings(App *app)
{
    draw_header(app, "Source lookup settings");
    TbCode *code = tb_native_code(app->native);
    tb_text(4, 3, COLS - 6, TB_MUTED, 0, "Directories: newest first. Prefix rules: first match wins, in listed order.");
    int height = LINES - 12; if (height < 1) { height = 1; }
    size_t dirs = tb_code_directory_count(code), rules = tb_code_rule_count(code);
    for (int row = 0; row < height; ++row) {
        size_t item = (size_t)app->file_page * (size_t)height + (size_t)row;
        char text[1024], safe_from[400], safe_to[400];
        if (item < dirs) {
            const char *path = tb_code_directory(code, item);
            tb_code_safe(safe_from, sizeof safe_from, path, strlen(path));
            (void)snprintf(text, sizeof text, "Directory %zu: %s", item + 1, safe_from);
        } else if (item < dirs + rules) {
            const TbSourceRule *rule = tb_code_rule(code, item - dirs);
            tb_code_safe(safe_from, sizeof safe_from, rule->from, strlen(rule->from));
            tb_code_safe(safe_to, sizeof safe_to, rule->to, strlen(rule->to));
            (void)snprintf(text, sizeof text, "Rule %zu: %s -> %s", item - dirs + 1, safe_from, safe_to);
        } else { break; }
        tb_text(6 + row, 3, COLS - 6, TB_PANEL, 0, text);
    }
    button(app, LINES - 6, 3, 15, " Add directory ", CODE_ACTION, CODE_DIRECTORY_ADD, app->code_pane);
    button(app, LINES - 6, 19, 15, " Add mapping ", CODE_ACTION, CODE_RULE_FROM, app->code_pane);
    button(app, LINES - 6, 35, COLS - 38, " Clear overrides ", CODE_ACTION, CODE_SETTINGS_CLEAR, app->code_pane);
    button(app, LINES - 4, 3, 12, "[ Back ]", BACK, 0, 0);
    button(app, LINES - 4, 17, 10, "Previous", CODE_FILE_PAGE, -1, 0);
    button(app, LINES - 4, 29, 8, "Next", CODE_FILE_PAGE, 1, 0);
}

static void draw_code_files(App *app)
{
    draw_header(app, "GDB source files");
    TbCode *code = tb_native_code(app->native);
    tb_text(4, 3, 8, TB_CYAN, 0, "Filter:");
    int field = add_control(app, (TbRect){11, 4, COLS - 14, 1}, FIELD, 0, 0);
    wide_text(4, 11, COLS - 14, app->input, 0, app->focus == field);
    char filter[FIELD_SIZE * 4] = {0}; (void)field_utf8(app->input, filter, sizeof filter);
    int height = LINES - 12; if (height < 1) { height = 1; }
    int match = 0, shown = 0;
    for (size_t i = 0; i < tb_code_file_count(code); ++i) {
        const TbCodeFile *file = tb_code_file(code, i);
        if (*filter && !strstr(file->file, filter) && !strstr(file->full, filter)) { continue; }
        if (match++ < app->file_page * height || shown == height) { continue; }
        char label[1024], path[960];
        const char *name = *file->full ? file->full : file->file;
        tb_code_safe(path, sizeof path, name, strlen(name));
        (void)snprintf(label, sizeof label, "%zu  %s", i + 1, path);
        button(app, 6 + shown++, 3, COLS - 6, label, CODE_FILE_PICK, (int)i, app->code_pane);
    }
    if (!shown) { tb_text(6, 3, COLS - 6, TB_MUTED, 0, "No matching GDB source files. Symbols may be absent; use Sources for recovery."); }
    tb_text(LINES - 6, 3, COLS - 6, TB_MUTED, 0,
            tb_code_files_limited(code) ? "Catalog limited to 256 files. Full paths distinguish duplicate basenames." :
            "Full paths distinguish duplicate basenames. Filter, then click or Tab/Enter.");
    button(app, LINES - 4, 3, 12, "[ Back ]", BACK, 0, 0);
    button(app, LINES - 4, 17, 10, "Previous", CODE_FILE_PAGE, -1, 0);
    button(app, LINES - 4, 29, 8, "Next", CODE_FILE_PAGE, 1, 0);
    if (app->focus == field) { (void)curs_set(1); (void)leaveok(stdscr, FALSE); (void)move(4, 11 + (int)wcslen(app->input) < COLS - 2 ? 11 + (int)wcslen(app->input) : COLS - 2); }
}


static void draw_input(App *app)
{
    bool command = app->page == COMMAND_INPUT;
    bool code = app->page == CODE_INPUT;
    bool bp = app->page == BREAK_INPUT;
    const char *bp_label = app->break_action == 0 ? "Add code breakpoint" : app->break_action == 3 ? "Breakpoint condition (empty clears)" : "Breakpoint ignore count";
    draw_header(app, bp ? bp_label : code ? code_labels[app->code_action] : command ? "GDB console command" : "Save retained log");
    tb_text(5, 3, COLS - 6, TB_CYAN, A_BOLD,
            bp ? bp_label : code ? code_labels[app->code_action] : command ? "Explicit GDB command (through MI)" : "New log destination (existing files are preserved)");
    tb_text(7, 3, COLS - 6, TB_MUTED, 0,
            bp ? "Location: function, file:line or *0xADDRESS. Changes are validated by GDB." : code ? "Enter literal text/path. Navigation never changes execution. Esc cancels." :
            command ? "Commands are deliberate operations through GDB/MI." :
            "Saves bounded retained history with control bytes escaped; mode 0600.");
    TbRect field = {3, 9, COLS - 6, 1};
    int index = add_control(app, field, FIELD, 0, 0);
    tb_fill(field, index == app->focus ? TB_SELECTED : TB_SURFACE);
    size_t start = 0;
    int cells = 0;
    for (size_t p = app->cursor; p > 0; --p) {
        int width = wcwidth(app->input[p - 1]); cells += width > 0 ? width : 1;
        if (cells >= COLS - 8) { start = p; break; }
    }
    wide_text(9, 3, COLS - 6, app->input, start, index == app->focus);
    button(app, 12, 3, 14, bp || code ? "[ Apply ]" : command ? "[ Send ]" : "[ Save Log ]", INPUT_SUBMIT, 0, 0);
    button(app, 12, 19, 12, "[ Cancel ]", BACK, 0, 0);
    if (index == app->focus) {
        (void)curs_set(1); (void)leaveok(stdscr, FALSE);
        int cursor_x = 3;
        for (size_t p = start; p < app->cursor; ++p) {
            int width = wcwidth(app->input[p]); cursor_x += width > 0 ? width : 1;
        }
        (void)move(9, cursor_x);
    }
}

static void draw_break_menu(App *app)
{
    draw_header(app, "Code breakpoint management");
    TbDemoPane *pane = &app->demo.panes[app->break_pane];
    const TbBreakpoint *bp = tb_break_row(tb_native_breakpoints(app->native), (size_t)pane->row[TB_BREAKPOINTS]);
    char text[1024], safe[1200];
    if (bp) { (void)snprintf(text, sizeof text, "Selected %s | %.400s | %.400s", bp->id, bp->location, bp->condition); }
    else { (void)snprintf(text, sizeof text, "Select an existing row in the Breakpoints pane to edit it."); }
    tb_code_safe(safe, sizeof safe, text, strlen(text)); tb_text(4, 3, COLS - 6, TB_CYAN, 0, safe);
    const char *labels[] = {"Add code breakpoint", bp && bp->enabled ? "Disable selected" : "Enable selected", "Remove selected",
                           "Edit condition", "Set ignore count", app->break_hardware ? "New breakpoint: Require hardware" : "New breakpoint: Default", "Refresh"};
    for (int i = 0; i < 7; ++i) { button(app, 6 + i, 3, COLS - 6, labels[i], BREAK_ACTION, i, app->break_pane); }
    tb_text(LINES - 5, 3, COLS - 6, TB_MUTED, 0, "Pending and multiple locations follow GDB. Hardware errors never fall back silently.");
    button(app, LINES - 4, 3, 12, "[ Back ]", BACK, 0, 0);
}

static void draw_page(App *app)
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
    } else if (app->page == CODE_MENU) {
        draw_code_menu(app);
    } else if (app->page == BREAK_MENU) {
        draw_break_menu(app);
    } else if (app->page == CODE_FILES) {
        draw_code_files(app);
    } else if (app->page == SOURCE_SETTINGS) {
        draw_source_settings(app);
    } else if (editing_page(app)) {
        draw_input(app);
    } else if (app->page == CLOSE_CONFIRM) {
        draw_header(app, "Close native session?");
        tb_text(6, 3, COLS - 6, TB_ORANGE, A_BOLD, tb_native_attached(app->native) ?
                "Detach and resume the attached process?" : "Terminate the launched program and close GDB?");
        tb_text(8, 3, COLS - 6, TB_MUTED, 0, "Only the managed session is closed. Cancel keeps debugging.");
        button(app, 11, 3, 14, "[ Cancel ]", CANCEL_RESET, 0, 0);
        button(app, 11, 20, 24, "[ Close session ]", CLOSE_ACCEPT, 0, 0);
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
}

static void draw(App *app)
{
    Control previous = app->focus >= 0 && app->focus < app->count ? app->controls[app->focus] : (Control){0};
    bool retain_body = app->page == WORKSPACE && (previous.kind == BODY || previous.kind == SCROLLBAR || previous.kind == HSCROLLBAR);
    draw_page(app);
    /* Live source availability can add/remove an earlier pane's scrollbar.
     * Preserve the focused body by identity rather than its old array index. */
    if (retain_body) {
        for (int i = 0; i < app->count; ++i) {
            if (app->controls[i].kind == previous.kind && app->controls[i].pane == previous.pane &&
                app->controls[i].value == previous.value) {
                if (app->focus != i) { app->focus = i; draw_page(app); }
                break;
            }
        }
    }
    (void)refresh();
}

static void cycle_pane(App *app, int direction)
{
    for (int i = 0; i < app->layout.count; ++i) {
        if (app->layout.order[i] == app->demo.pane) {
            int next = (i + direction + app->layout.count) % app->layout.count;
            app->demo.pane = app->layout.order[next];
            code_focus(app, app->demo.pane);
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

static bool field_utf8(const wchar_t *value, char *target, size_t capacity)
{
    size_t length = wcstombs(target, value, capacity);
    if (length == (size_t)-1 || length >= capacity) { return false; }
    target[length] = '\0'; return true;
}

static bool start_native(App *app)
{
    char values[FORM_FIELDS][FIELD_SIZE * 4];
    for (int i = 0; i < FORM_FIELDS; ++i) {
        if (!field_utf8(app->forms[0].values[i], values[i], sizeof values[i])) {
            status(app, "Field encoding/length is invalid."); return false;
        }
    }
    TbNativeWords arguments = {0}, environment = {0}; char error[160];
    if (!app->forms[0].alternate &&
        (!tb_native_words(values[2], &arguments, error, sizeof error) ||
         !tb_native_words(values[7], &environment, error, sizeof error))) { status(app, error); return false; }
    pid_t pid = 0;
    if (app->forms[0].alternate) {
        unsigned long number = 0;
        for (size_t i = 0; values[6][i]; ++i) {
            unsigned char c = (unsigned char)values[6][i];
            if (c < '0' || c > '9' || number > ((unsigned long)INT_MAX - (c - '0')) / 10) {
                status(app, "PID must contain decimal digits."); return false;
            }
            number = number * 10 + c - '0';
        }
        pid = (pid_t)number;
    }
    TbNativeConfig config = {.attach = app->forms[0].alternate, .pid = pid, .gdb = values[5],
        .executable = app->forms[0].alternate ? values[4] : values[1],
        .directory = app->forms[0].alternate ? "" : values[3],
        .arguments = &arguments, .environment = &environment};
    if (!tb_native_start(app->native, &config)) { status(app, tb_native_message(app->native)); return false; }
    app->live = true; app->previewed = true;
    for (int i = 0; i < TB_MAX_PANES; ++i) { app->follow_log[i] = true; }
    status(app, tb_native_message(app->native));
    return true;
}

static void request_leave(App *app, bool quit)
{
    if (app->live && tb_native_active(app->native)) {
        app->exit_after_close = quit;
        page(app, CLOSE_CONFIRM);
    } else if (quit) { app->quit = true; }
    else {
        if (app->live) { app->previewed = false; }
        app->live = false; page(app, LAUNCHER);
    }
}

static void activate(App *app, Control control)
{
    switch (control.kind) {
    case CHOOSE_SESSION:
        app->session = control.value;
        page(app, CONFIGURE);
        break;
    case QUIT: request_leave(app, true); break;
    case SESSION_MENU: request_leave(app, false); break;
    case SUBMIT: page(app, REVIEW); break;
    case PREVIEW:
        if (!app->demo_mode && app->session == 0) {
            if (start_native(app)) { page(app, WORKSPACE); }
            break;
        }
        app->live = false;
        app->previewed = true;
        status(app, "Preview selected. All values are simulated.");
        page(app, WORKSPACE);
        break;
    case BACK:
        if (app->page == CODE_MENU || app->page == SOURCE_SETTINGS || app->page == BREAK_MENU) { page(app, WORKSPACE); break; }
        if (editing_page(app)) { page(app, WORKSPACE); break; }
        page(app, app->page == REVIEW ? CONFIGURE :
             app->page == LAUNCHER ? WORKSPACE : LAUNCHER);
        break;
    case FIELD: {
        if (editing_page(app)) { break; }
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
        if (app->live) {
            if (control.value == TB_BREAKPOINT) { (void)toggle_breakpoint(app); break; }
            bool okay = control.value == TB_CONTINUE ? tb_native_continue(app->native) :
                        control.value == TB_PAUSE ? tb_native_pause(app->native) :
                        control.value >= TB_NEXT && control.value <= TB_FINISH ?
                        tb_native_step(app->native, control.value == TB_NEXT ? TB_STEP_OVER :
                                       control.value == TB_STEP ? TB_STEP_INTO : TB_STEP_OUT, app->instruction_mode) : false;
            status(app, okay ? "GDB operation queued." : control.value >= TB_NEXT ?
                   control.value <= TB_FINISH ? tb_native_message(app->native) :
                   "Reset remains unavailable in this milestone." :
                   "GDB operation pending or unavailable in the current state.");
            break;
        }
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
        code_focus(app, control.pane);
        app->menu = -1;
        /* The activated tab moves first. Its previous control index may now
         * belong to an inactive tab, so keep focus at this pane's first tab. */
        for (int i = 0; i < app->count; ++i) {
            if (app->controls[i].kind == TAB && app->controls[i].pane == control.pane) {
                app->focus = i;
                break;
            }
        }
        break;
    case MENU:
        app->demo.pane = control.pane;
        app->menu = app->menu == control.pane ? -1 : control.pane;
        app->menu_focus = app->menu >= 0;
        app->menu_actions = false;
        break;
    case BODY:
        app->demo.pane = control.pane; code_focus(app, control.pane); select_context_row(app, control.pane); break;
    case SCROLLBAR: case HSCROLLBAR: app->demo.pane = control.pane; break;
    case SHOW_VIEW:
        app->demo.pane = control.pane;
        if (!tb_demo_toggle_view(&app->demo, (TbView)control.value)) {
            status(app, "Each pane needs at least one tab.");
            break;
        }
        status(app, tb_demo_has_view(&app->demo.panes[control.pane], (TbView)control.value) ?
               "View added to this pane." : "View removed from this pane.");
        code_focus(app, control.pane);
        app->menu = -1;
        app->focus = 0;
        break;
    case FOCUS_PANE:
        app->demo.pane = control.value;
        code_focus(app, control.value);
        app->menu = -1;
        break;
    case NEXT_PANE: cycle_pane(app, 1); break;
    case PREVIOUS_PANE: cycle_pane(app, -1); break;
    case MENU_SECTION:
        app->menu_actions = !app->menu_actions;
        app->menu_focus = true;
        break;
    case PANE_ACTION: pane_action(app, control.value, control.pane); break;
    case REGISTER_REFRESH:
        tb_registers_refresh(tb_native_registers(app->native));
        status(app, "Register refresh requested; reads wait until stopped."); break;
    case REGISTER_FORMAT: {
        TbRegisters *r = tb_native_registers(app->native);
        tb_registers_format(r, (TbRegisterFormat)((tb_registers_get_format(r) + 1) % TB_REG_FORMATS));
        status(app, "Register display format changed; target values are not written."); break;
    }
    case CONTEXT_REFRESH:
        tb_context_refresh(tb_native_context(app->native));
        status(app, "Context refresh requested; reads wait until stopped."); break;
    case CONTEXT_PAGE:
        status(app, tb_context_page(tb_native_context(app->native), control.value) ?
               "Loading stack page." : "No further stack page available; wait for stopped context.");
        if (control.value > 0 && tb_context_snapshot(tb_native_context(app->native))->first_frame >
            TB_CONTEXT_MAX_LEVEL - TB_CONTEXT_FRAMES) {
            status(app, "Stack display is limited to 4096 frames; use Console for deeper inspection.");
        }
        app->demo.panes[control.pane].scroll[TB_CALL_STACK] = 0;
        app->demo.panes[control.pane].row[TB_CALL_STACK] = 0;
        break;
    case DIVIDER:
        app->resizing = true;
        app->resize_divider = control.value;
        break;
    case CANCEL_RESET: page(app, WORKSPACE); break;
    case COMMAND_OPEN: case SAVE_OPEN:
        app->input[0] = L'\0';
        page(app, control.kind == COMMAND_OPEN ? COMMAND_INPUT : SAVE_INPUT);
        break;
    case BREAK_OPEN:
        app->break_pane = control.pane; page(app, BREAK_MENU); break;
    case BREAK_ACTION: {
        TbBreakpoints *b = tb_native_breakpoints(app->native);
        TbDemoPane *pane = &app->demo.panes[app->break_pane];
        const TbBreakpoint *bp = tb_break_row(b, (size_t)pane->row[TB_BREAKPOINTS]);
        app->break_action = control.value;
        if (control.value == 5) { app->break_hardware = !app->break_hardware; break; }
        if (control.value == 6) { tb_break_refresh(b); page(app, WORKSPACE); break; }
        if (control.value != 0 && !bp) { status(app, "Select a breakpoint row first."); break; }
        if (bp) { (void)snprintf(app->break_id, sizeof app->break_id, "%s", bp->child ? bp->parent : bp->id); }
        if (control.value == 1 || control.value == 2) {
            bool okay = control.value == 1 ? tb_break_enable(b, bp->id, !bp->enabled) : tb_break_remove(b, app->break_id);
            status(app, okay ? "Breakpoint change queued." : tb_break_message(b)); if (okay) { page(app, WORKSPACE); }
        } else { app->input[0] = L'\0'; page(app, BREAK_INPUT); }
        break;
    }
    case CODE_ACTIONS:
        app->code_pane = control.pane; code_focus(app, control.pane); page(app, CODE_MENU); break;
    case CODE_ACTION: {
        TbCode *code = tb_native_code(app->native);
        app->code_pane = control.pane; app->code_action = (CodeAction)control.value;
        if (app->code_action == CODE_CHOOSE_FILE) {
            app->input[0] = L'\0'; app->file_page = 0; tb_code_request_files(code); page(app, CODE_FILES);
        } else if (app->code_action == CODE_SOURCE_SETTINGS) {
            app->file_page = 0; page(app, SOURCE_SETTINGS);
        } else if (app->code_action == CODE_STOPPED) {
            status(app, tb_code_stopped(code) ? "Returning to stopped context; execution unchanged." : tb_code_message(code)); page(app, WORKSPACE);
        } else if (app->code_action == CODE_RUN_TO) {
            char location[8192]; const TbBreakpoint *existing = NULL;
            TbBreakLocation where;
            bool okay = selected_location(app, location, sizeof location, &existing, &where) && tb_native_run_at(app->native, where);
            status(app, okay ? "Run to Selected queued; an intervening stop cancels it." : "Run to Selected needs a selected code row and fresh stopped context."); page(app, WORKSPACE);
        } else if (app->code_action == CODE_RAW) {
            tb_code_raw(code, app->code_pane); page(app, WORKSPACE);
        } else if (app->code_action == CODE_MIXED) {
            tb_code_mixed(code, control.pane, !tb_code_view(code, control.pane)->mixed); page(app, WORKSPACE);
        } else if (app->code_action == CODE_SETTINGS_CLEAR) {
            status(app, tb_code_clear_settings(code) ? "Clearing current GDB source overrides." : "Wait for the current source operation.");
        } else { app->input[0] = L'\0'; page(app, CODE_INPUT); }
        break;
    }
    case CODE_PAGE:
        status(app, tb_code_assembly_page(tb_native_code(app->native), control.pane, control.value) ?
               "Loading instruction window; execution unchanged." : "No previous window, or wait for stopped code data.");
        app->demo.panes[control.pane].scroll[TB_DISASSEMBLY] = 0; break;
    case CODE_FILE_PAGE: {
        size_t total = tb_code_directory_count(tb_native_code(app->native)) + tb_code_rule_count(tb_native_code(app->native));
        if (app->page == CODE_FILES) {
            char filter[FIELD_SIZE * 4] = {0}; (void)field_utf8(app->input, filter, sizeof filter); total = 0;
            for (size_t i = 0; i < tb_code_file_count(tb_native_code(app->native)); ++i) {
                const TbCodeFile *file = tb_code_file(tb_native_code(app->native), i);
                if (!*filter || strstr(file->file, filter) || strstr(file->full, filter)) { ++total; }
            }
        }
        if (control.value < 0) { if (app->file_page > 0) { --app->file_page; } }
        else if (((size_t)app->file_page + 1) * (size_t)(LINES - 12) < total) { ++app->file_page; }
        app->focus = 0; break;
    }
    case CODE_FILE_PICK:
        if (tb_code_open(tb_native_code(app->native), control.pane, (size_t)control.value)) {
            app->demo.panes[control.pane].scroll[TB_SOURCE] = app->demo.panes[control.pane].row[TB_SOURCE] = 0;
            status(app, tb_code_message(tb_native_code(app->native))); page(app, WORKSPACE);
        }
        break;
    case INPUT_SUBMIT: {
        char input[FIELD_SIZE * 4];
        bool command = app->page == COMMAND_INPUT;
        if (!field_utf8(app->input, input, sizeof input)) { status(app, "Invalid text encoding."); break; }
        if (app->page == BREAK_INPUT) {
            TbBreakpoints *b = tb_native_breakpoints(app->native);
            bool okay = app->break_action == 0 ? tb_break_add(b, input, app->break_hardware) :
                        app->break_action == 3 ? tb_break_condition(b, app->break_id, input) : tb_break_ignore(b, app->break_id, input);
            status(app, okay ? "Breakpoint change queued; GDB validates the request." : tb_break_message(b));
            if (okay) { page(app, WORKSPACE); } break;
        }
        if (app->page == CODE_FILES) { app->file_page = 0; break; }
        if (app->page == CODE_INPUT) {
            TbCode *code = tb_native_code(app->native);
            TbDemoPane *pane = &app->demo.panes[app->code_pane];
            bool okay = false;
            if (app->code_action == CODE_FIND || app->code_action == CODE_LINE) {
                int row = app->code_action == CODE_FIND ? tb_code_find(code, app->code_pane, input, pane->row[TB_SOURCE] + 1) : -1;
                if (app->code_action == CODE_LINE && *input && strspn(input, "0123456789") == strlen(input)) {
                    errno = 0; unsigned long line = strtoul(input, NULL, 10);
                    if (!errno && line > 0 && line <= tb_code_view(code, app->code_pane)->line_count) { row = (int)line - 1; }
                }
                okay = row >= 0;
                if (okay) {
                    pane->scroll[TB_SOURCE] = row > 2 ? row - 2 : 0; pane->row[TB_SOURCE] = row;
                    char found[128]; (void)snprintf(found, sizeof found, "Browsing source line %d; execution unchanged.", row + 1); status(app, found);
                } else { status(app, "Text/line not found in the loaded source. Use source recovery if unavailable."); }
            } else if (app->code_action == CODE_ADDRESS) { okay = tb_code_address(code, app->code_pane, input); status(app, okay ? "Resolving code location; execution unchanged." : tb_code_message(code)); }
            else if (app->code_action == CODE_DIRECTORY_ADD) { okay = tb_code_add_directory(code, input); status(app, okay ? "Source directory queued." : tb_code_message(code)); }
            else if (app->code_action == CODE_RULE_FROM) {
                if (*input) {
                    (void)snprintf(app->rule_from, sizeof app->rule_from, "%s", input);
                    app->code_action = CODE_RULE_TO; app->input[0] = L'\0'; page(app, CODE_INPUT);
                } else { status(app, "Enter the build prefix first."); }
                break;
            } else if (app->code_action == CODE_RULE_TO) { okay = tb_code_add_rule(code, app->rule_from, input); status(app, okay ? "Source mapping queued." : tb_code_message(code)); }
            if (okay) { page(app, WORKSPACE); }
            break;
        }
        bool okay = command ? tb_native_console(app->native, input) : tb_native_save_log(app->native, input);
        status(app, okay ? (command ? "GDB command queued." : tb_native_message(app->native)) : tb_native_message(app->native));
        if (okay) { page(app, WORKSPACE); }
        break;
    }
    case CLOSE_ACCEPT:
        tb_native_close(app->native); app->leave_session = true;
        page(app, WORKSPACE); break;
    case CONFIRM_RESET:
        tb_demo_action(&app->demo, TB_RESET);
        status(app, "DEMO reset confirmed.");
        page(app, WORKSPACE);
        break;
    }
}

static bool edit_field(App *app, Control control, wint_t key, bool special)
{
    bool input_page = editing_page(app);
    if (control.kind != FIELD || (app->page != CONFIGURE && !input_page)) {
        return false;
    }
    if (!input_page && ((app->session < 2 && control.value == 0) ||
        (app->session == 1 && control.value == 8))) {
        if (key == KEY_LEFT || key == KEY_RIGHT || key == ' ') {
            activate(app, control);
            return true;
        }
        return false;
    }
    wchar_t *value = input_page ? app->input : app->forms[app->session].values[control.value];
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
            status(app, "Field is full (255 characters).");
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
    TbScroll scroll = workspace_scroll(app, pane, app->panes[control.pane].h - 3);
    if (app->live && view == TB_DISASSEMBLY &&
        ((amount < 0 && scroll.offset == 0) || (amount > 0 && scroll.offset == scroll.maximum))) {
        if (tb_code_assembly_page(tb_native_code(app->native), control.pane, amount < 0 ? -1 : 1)) {
            app->assembly_tail[control.pane] = true;
            {
                const TbCodeView *code = tb_code_view(tb_native_code(app->native), control.pane);
                int flat = 0; size_t visible = 0;
                for (size_t row = 0; row < code->instruction_count; ++row) {
                    const TbInstruction *instruction = &code->instructions[row];
                    if (assembly_gap(code, row)) { ++flat; }
                    if (code->mixed && code->mixed_available && instruction->line > 0 &&
                        (!row || instruction->line != code->instructions[row - 1].line || strcmp(instruction->source, code->instructions[row - 1].source) != 0)) { ++flat; }
                    visible = row;
                    if (flat++ >= scroll.offset) { break; }
                }
                (void)snprintf(app->assembly_previous[control.pane], sizeof app->assembly_previous[control.pane], "%s",
                               code->instruction_count ? code->instructions[visible].address : "");
                app->assembly_scroll_up[control.pane] = -amount;
            }
            pane->scroll[view] = 0;
            return;
        }
    }
    int offset = scroll.offset + amount;
    pane->scroll[view] = offset < 0 ? 0 : offset > scroll.maximum ? scroll.maximum : offset;
    if (app->live) { app->follow_log[control.pane] = pane->scroll[view] == scroll.maximum; }
}

static void mouse_event(App *app)
{
    MEVENT event;
    if (getmouse(&event) != OK) {
        return;
    }
    app->keyboard_focus = false;
    if (app->page == WORKSPACE && app->scroll_drag_pane >= 0) {
        int pane_index = app->scroll_drag_pane;
        TbDemoPane *pane = &app->demo.panes[pane_index];
        TbScroll scroll = app->scroll_drag_horizontal ? app->horizontal_scroll[pane_index] :
                          workspace_scroll(app, pane, app->panes[pane_index].h - 3);
        int travel = scroll.visible - scroll.thumb_size;
        int position = app->scroll_drag_horizontal ? event.x - app->panes[pane_index].x - 1 -
                       code_gutter(pane->tabs[pane->active]) - app->scroll_drag_grab :
                       event.y - app->panes[pane_index].y - 2 - app->scroll_drag_grab;
        if (position < 0) { position = 0; }
        if (position > travel) { position = travel; }
        if (travel > 0) {
            int *offset = app->scroll_drag_horizontal ? &app->horizontal[pane_index][pane->tabs[pane->active]] : &pane->scroll[pane->tabs[pane->active]];
            *offset = position * scroll.maximum / travel;
        }
        if (app->live && !app->scroll_drag_horizontal) { app->follow_log[pane_index] = pane->scroll[pane->tabs[pane->active]] == scroll.maximum; }
        if (event.bstate & BUTTON1_RELEASED) { app->scroll_drag_pane = -1; }
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
        if ((control.kind == BODY || control.kind == SCROLLBAR || control.kind == HSCROLLBAR) &&
            (event.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))) {
            if (control.kind == HSCROLLBAR) {
                TbDemoPane *pane = &app->demo.panes[control.pane];
                int *offset = &app->horizontal[control.pane][pane->tabs[pane->active]];
                int next = *offset + (event.bstate & BUTTON4_PRESSED ? -4 : 4);
                *offset = next < 0 ? 0 : next > app->horizontal_scroll[control.pane].maximum ? app->horizontal_scroll[control.pane].maximum : next;
                return;
            }
            scroll_body(app, control, event.bstate & BUTTON4_PRESSED ? -3 : 3);
            return;
        }
        if (!(event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) {
            return;
        }
        app->focus = i;
        app->cursor = 0;
        bool input_page = editing_page(app);
        bool text_field = control.kind == FIELD && (input_page ||
                          !((app->session < 2 && control.value == 0) ||
                            (app->session == 1 && control.value == 8)));
        if (text_field) {
            app->cursor = wcslen(input_page ? app->input : app->forms[app->session].values[control.value]);
        } else if (control.kind != BODY) {
            activate(app, control);
        }
        if (control.kind == SCROLLBAR || control.kind == HSCROLLBAR) {
            TbDemoPane *pane = &app->demo.panes[control.pane];
            bool horizontal = control.kind == HSCROLLBAR;
            TbScroll scroll = horizontal ? app->horizontal_scroll[control.pane] : workspace_scroll(app, pane, control.rect.h);
            int row = horizontal ? event.x - control.rect.x : event.y - control.rect.y;
            if (row >= scroll.thumb_start && row < scroll.thumb_start + scroll.thumb_size) {
                app->scroll_drag_pane = control.pane;
                app->scroll_drag_grab = row - scroll.thumb_start;
                app->scroll_drag_horizontal = horizontal;
            } else if (horizontal) {
                int next = scroll.offset + (row < scroll.thumb_start ? -scroll.visible : scroll.visible);
                app->horizontal[control.pane][pane->tabs[pane->active]] = next < 0 ? 0 : next > scroll.maximum ? scroll.maximum : next;
            } else {
                scroll_body(app, control, row < scroll.thumb_start ? -scroll.visible : scroll.visible);
            }
        } else if (control.kind == BODY) {
            app->demo.pane = control.pane;
            code_focus(app, control.pane);
            TbDemoPane *pane = &app->demo.panes[control.pane];
            TbView view = pane->tabs[pane->active];
            int row = event.y - app->panes[control.pane].y - 2;
            if (row >= 0) {
                TbScroll scroll = workspace_scroll(app, pane, app->panes[control.pane].h - 3);
                int target = scroll.offset + row;
                if (target < scroll.total) { pane->row[view] = target; }
                if (app->live && target < scroll.total) { select_context_row(app, control.pane); }
                if (app->live && target < scroll.total && view == TB_SOURCE && event.x < control.rect.x + 2) { (void)toggle_breakpoint(app); }
                if (!app->live && target < scroll.total && view == TB_SOURCE && event.x < control.rect.x + 2) {
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
        request_leave(app, true);
        return;
    }
    if (special && key == KEY_RESIZE) {
        app->drag = -1;
        app->scroll_drag_pane = -1;
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
    app->keyboard_focus = true;
    Control control = app->controls[app->focus];
    if (!special && key == 'q') {
        if (edit_field(app, control, key, false)) { return; }
        key = 27; /* Close/back; Ctrl+Q is the application-wide quit action. */
    }
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
            if (app->previewed) { page(app, WORKSPACE); }
            else { status(app, "Already at the launcher. Ctrl+Q quits the application."); }
        } else if (app->page == RESET_CONFIRM || app->page == CLOSE_CONFIRM || app->page == CODE_MENU || app->page == SOURCE_SETTINGS ||
                   editing_page(app)) {
            page(app, WORKSPACE);
        } else if (app->page == WORKSPACE && app->live) {
            request_leave(app, false);
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
    if (edit_field(app, control, key, special)) {
        if (app->page == CODE_FILES) { app->file_page = 0; }
        return;
    }
    if ((editing_page(app)) && control.kind == FIELD &&
        ((!special && (key == '\n' || key == '\r')) || (special && key == KEY_ENTER))) {
        activate(app, (Control){.kind = INPUT_SUBMIT}); return;
    }
    if (!special && app->page == WORKSPACE && key >= '1' && key <= '9') {
        int pane = (int)(key - '1');
        if (tb_layout_has_pane(&app->layout, pane)) { app->demo.pane = pane; }
        code_focus(app, app->demo.pane);
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
        if (app->live && (control.kind == BODY || control.kind == HSCROLLBAR || control.kind == SCROLLBAR) && (key == KEY_LEFT || key == KEY_RIGHT)) {
            TbDemoPane *pane = &app->demo.panes[control.pane];
            TbView view = pane->tabs[pane->active];
            int *offset = &app->horizontal[control.pane][view];
            int next = *offset + (key == KEY_LEFT ? -4 : 4);
            int maximum = app->horizontal_scroll[control.pane].maximum;
            *offset = next < 0 ? 0 : next > maximum ? maximum : next;
            return;
        }
        if ((control.kind == BODY || control.kind == SCROLLBAR) &&
            (key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE)) {
            TbDemoPane *pane = &app->demo.panes[control.pane];
            TbView view = pane->tabs[pane->active];
            if (app->live && (view == TB_CALL_STACK || view == TB_THREADS)) {
                TbScroll scroll = workspace_scroll(app, pane, app->panes[control.pane].h - 3);
                int row = pane->row[view] + (key == KEY_UP ? -1 : key == KEY_DOWN ? 1 :
                                           key == KEY_PPAGE ? -scroll.visible : scroll.visible);
                if (row < 0) { row = 0; }
                if (row >= scroll.total) { row = scroll.total ? scroll.total - 1 : 0; }
                pane->row[view] = row;
                if (row < scroll.offset) { pane->scroll[view] = row; }
                else if (row >= scroll.offset + scroll.visible) { pane->scroll[view] = row - scroll.visible + 1; }
                return;
            }
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
    }
}

static void initialize(App *app, bool demo)
{
    memset(app, 0, sizeof *app);
    app->demo_mode = demo;
    app->native = tb_native_create();
    if (app->native == NULL) { app->quit = true; }
    app->keyboard_focus = true;
    app->menu = -1;
    app->drag = -1;
    app->scroll_drag_pane = -1;
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
    status(app, demo ? "DEMO only. Nothing is launched, connected or saved." :
                      "Choose native launch/attach. Embedded and remote remain previews.");
}

int tb_tui_run(bool demo)
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
        initialize(&app, demo);
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
        (void)wtimeout(stdscr, 0);
        draw(&app);
        uint64_t revision = app.native ? tb_native_revision(app.native) : 0;
        while (!app.quit) {
            if (interrupted != 0) {
                if (app.native) { tb_native_close(app.native); }
                if (!app.native || !tb_native_active(app.native)) { break; }
            }
            if (app.native) {
                tb_native_pump(app.native, 0);
                uint64_t next = tb_native_revision(app.native);
                if (next != revision) {
                    revision = next;
                    if (app.live) { status(&app, tb_native_message(app.native)); draw(&app); }
                }
                if (app.leave_session && !tb_native_active(app.native)) {
                    app.leave_session = false;
                    if (app.exit_after_close) { app.quit = true; continue; }
                    app.live = false; app.previewed = false; page(&app, LAUNCHER); draw(&app);
                }
            }
            wint_t key;
            int type = get_wch(&key);
            if (type != ERR && interrupted == 0) {
                key_event(&app, key, type == KEY_CODE_YES);
                if (!app.quit && interrupted == 0) { draw(&app); }
                continue;
            }
            struct pollfd fds[6] = {{STDIN_FILENO, POLLIN, 0}};
            int timeout = 100;
            nfds_t count = 1;
            if (app.native) {
                count += (nfds_t)tb_native_pollfds(app.native, fds + 1);
                timeout = tb_native_timeout(app.native, 100);
            }
            if (poll(fds, count, timeout) < 0 && errno != EINTR) { interrupted = SIGTERM; }
        }
        if (app.native) {
            tb_native_close(app.native);
            while (tb_native_active(app.native)) { tb_native_pump(app.native, 20); }
            (void)tb_native_destroy(app.native);
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
