#include "../support/terminal.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void mouse(TestTerminal *term, int button, int x, int y, bool release)
{
    char sequence[64];
    (void)snprintf(sequence, sizeof sequence, "\033[<%d;%d;%d%c", button, x + 1, y + 1, release ? 'm' : 'M');
    test_terminal_send(term, sequence);
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    long long until = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 60;
    do {
        test_terminal_pump(term, 5);
        CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    } while ((long long)now.tv_sec * 1000 + now.tv_nsec / 1000000 < until);
}

static void click_at(TestTerminal *term, int x, int y)
{
    mouse(term, 0, x, y, false);
    mouse(term, 0, x, y, true);
}

static void menu_at(TestTerminal *term, int x, int y)
{
    click_at(term, x, y);
    test_terminal_expect(term, "SHOW VIEW (DEMO)");
    test_terminal_click(term, "Pane actions >");
    test_terminal_expect(term, "PANE ACTIONS");
}

static void toggle_view_at(TestTerminal *term, int menu_x, int menu_y, const char *name)
{
    click_at(term, menu_x, menu_y);
    test_terminal_expect(term, "SHOW VIEW (DEMO)");
    /* The same label can be a header tab. Select only this pane's menu body. */
    for (int row = menu_y + 2; row < term->rows - 2; ++row) {
        const char *at = strstr(term->cells[row], name);
        if (at && at - term->cells[row] < menu_x) {
            click_at(term, (int)(at - term->cells[row]), row);
            return;
        }
    }
    CHECK(false);
}

static void start(TestTerminal *term, const char *binary, const char *type, bool monochrome)
{
    test_terminal_start(term, binary, type, monochrome);
    test_terminal_expect(term, "Start a debugging session");
    test_terminal_send(term, "\r\t\t\t\t\t\r");
    test_terminal_expect(term, "Review session preview");
    test_terminal_send(term, "\r");
    test_terminal_expect(term, "DEMO | Paused");
}

static int count_text(const TestTerminal *term, const char *text)
{
    int count = 0;
    for (int y = 0; y < term->rows - 3; ++y) {
        const char *p = term->cells[y];
        while ((p = strstr(p, text)) != NULL) { ++count; p += strlen(text); }
    }
    return count;
}

static void lower_overlays(const char *binary)
{
    TestTerminal term;
    start(&term, binary, "xterm-256color", false);
    CHECK(!test_terminal_find(&term, "HEX bytes / ASCII", NULL, NULL));
    CHECK(!test_terminal_find(&term, "Sample log", NULL, NULL));
    CHECK(count_text(&term, "...") == 3);
    for (int i = 0; i < 3; ++i) { test_terminal_lower(&term, i, false); }
    int source_x, source_y, memory_x, memory_y, locals_x, locals_y;
    CHECK(test_terminal_find(&term, "main.c | Cortex-M4F sample", &source_x, &source_y));
    test_terminal_lower(&term, 0, true);
    test_terminal_expect(&term, "HEX bytes / ASCII");
    CHECK(test_terminal_find(&term, "Memory", &memory_x, &memory_y));
    test_terminal_lower(&term, 1, true);
    test_terminal_expect(&term, "Name        Type");
    CHECK(test_terminal_find(&term, "Locals", &locals_x, &locals_y));
    mouse(&term, 0, memory_x + 2, memory_y - 1, false);
    test_terminal_expect(&term, "Dragging divider");
    mouse(&term, 32, memory_x + 2, memory_y - 4, false);
    mouse(&term, 0, memory_x + 2, memory_y - 4, true);
    test_terminal_expect(&term, "Divider resized.");
    int x, y;
    CHECK(test_terminal_find(&term, "Memory", &x, &y) && y == memory_y - 3);
    CHECK(test_terminal_find(&term, "Locals", &x, &y) && y == locals_y);
    CHECK(test_terminal_find(&term, "main.c | Cortex-M4F sample", &x, &y) && x == source_x && y == source_y);
    test_terminal_lower(&term, 0, false);
    test_terminal_lower(&term, 0, true);
    CHECK(test_terminal_find(&term, "Memory", &x, &y) && y == memory_y - 3);
    test_terminal_lower(&term, 2, true);
    test_terminal_resize(&term, 24, 80);
    test_terminal_expect(&term, "Sample log / no commands executed");
    CHECK(!test_terminal_find(&term, "HEX bytes / ASCII", NULL, NULL));
    /* Pane selection raises an open group without requiring a duplicate dock. */
    test_terminal_send(&term, "4");
    test_terminal_expect(&term, "HEX bytes / ASCII");
    test_terminal_send(&term, "6");
    test_terminal_expect(&term, "Sample log / no commands executed");
    /* Open groups use their header and pane selection, without duplicate docks. */
    test_terminal_send(&term, "4");
    test_terminal_expect(&term, "HEX bytes / ASCII");
    CHECK(!test_terminal_find(&term, "Sample log / no commands executed", NULL, NULL));
    test_terminal_lower(&term, 0, false);
    test_terminal_expect(&term, "Sample log / no commands executed");
    test_terminal_lower(&term, 1, false);
    test_terminal_lower(&term, 2, false);
    test_terminal_send(&term, "4"); /* Keyboard pane selection also restores. */
    test_terminal_expect(&term, "HEX bytes / ASCII");
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "main.c | Cortex-M4F sample");
    CHECK(test_terminal_find(&term, "Memory", &x, &y) && y == memory_y - 3);
    test_terminal_click(&term, "Session");
    test_terminal_expect(&term, "Start a debugging session");
    test_terminal_send(&term, "\r\t\t\t\t\t\r");
    test_terminal_expect(&term, "Review session preview");
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "DEMO | Paused");
    CHECK(count_text(&term, "...") == 3);
    CHECK(!test_terminal_find(&term, "HEX bytes / ASCII", NULL, NULL));
    test_terminal_lower(&term, 0, true);
    CHECK(test_terminal_find(&term, "Memory", &x, &y) && y == memory_y);
    test_terminal_lower(&term, 0, false);
    test_terminal_resize(&term, 24, 80);
    test_terminal_send(&term, "2");
    test_terminal_expect(&term, "Disassembly");
    test_terminal_lower(&term, 0, true);
    test_terminal_lower(&term, 0, false);
    test_terminal_expect(&term, "Disassembly"); /* Return to the actual underlay. */
    test_terminal_stop(&term, 0);
    puts("PASS minimized lower groups, independent height, retained geometry, compact stacking and keyboard restore");
}

static void tab_focus(const char *binary, const char *type, bool monochrome)
{
    TestTerminal term;
    start(&term, binary, type, monochrome);
    for (int repeat = 0; repeat < 3; ++repeat) {
        test_terminal_click(&term, "Call Stack");
        test_terminal_expect(&term, "# Function     Location");
        int stack_x, stack_y, asm_x, asm_y;
        CHECK(test_terminal_find(&term, "Call Stack", &stack_x, &stack_y));
        CHECK(test_terminal_find(&term, "Disassembly", &asm_x, &asm_y));
        CHECK(stack_y == asm_y && stack_x < asm_x);
        CHECK(!term.inverse[asm_y][asm_x]);
        CHECK(term.inverse[stack_y][stack_x] == monochrome);
        /* Enter must keep the clicked tab active, not activate its old index. */
        test_terminal_send(&term, "\r");
        test_terminal_expect(&term, "# Function     Location");
        CHECK(test_terminal_find(&term, "Call Stack", &stack_x, &stack_y));
        CHECK(test_terminal_find(&term, "Disassembly", &asm_x, &asm_y));
        CHECK(stack_x < asm_x && term.inverse[stack_y][stack_x]);
        test_terminal_send(&term, "\t");
        test_terminal_expect(&term, "# Function     Location");
        CHECK(term.inverse[asm_y][asm_x]); /* visible keyboard focus */
        test_terminal_send(&term, "\r");
        test_terminal_expect(&term, "Address    Bytes");
        CHECK(test_terminal_find(&term, "Disassembly", &asm_x, &asm_y));
        CHECK(test_terminal_find(&term, "Call Stack", &stack_x, &stack_y));
        CHECK(asm_x < stack_x && !term.inverse[stack_y][stack_x]);
    }
    test_terminal_stop(&term, 0);
}

int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    lower_overlays(argv[1]);
    tab_focus(argv[1], "xterm-direct", false);
    tab_focus(argv[1], "xterm-256color", false);
    tab_focus(argv[1], "xterm-256color", true);
    puts("PASS tab promotion, immediate inactive styling and stable mouse/keyboard focus");
    TestTerminal term;
    start(&term, argv[1], "xterm-256color", false);
    test_terminal_lower(&term, 0, true);
    CHECK(strstr(term.output, "\033[?1002h") != NULL); /* held-button motion requested */
    int x, y;
    CHECK(test_terminal_find(&term, "...", &x, &y));
    int old_menu = x;
    int divider = x + 5; /* gutter immediately after first pane */
    mouse(&term, 0, divider, y + 2, false);
    test_terminal_expect(&term, "Dragging divider");
    mouse(&term, 32, divider + 5, y + 2, false);
    mouse(&term, 0, divider + 5, y + 2, true);
    test_terminal_expect(&term, "Divider resized.");
    CHECK(test_terminal_find(&term, "...", &x, &y));
    CHECK(x == old_menu + 5);

    int memory_x, memory_y;
    CHECK(test_terminal_find(&term, "Memory", &memory_x, &memory_y));
    int old_memory_y = memory_y;
    mouse(&term, 0, memory_x + 3, memory_y - 1, false);
    test_terminal_expect(&term, "Dragging divider");
    mouse(&term, 32, memory_x + 3, memory_y + 1, false);
    mouse(&term, 0, memory_x + 3, memory_y + 1, true);
    test_terminal_expect(&term, "Divider resized.");
    CHECK(test_terminal_find(&term, "Memory", &memory_x, &memory_y));
    if (memory_y != old_memory_y + 2) {
        fprintf(stderr, "Row divider: expected memory y=%d, got %d\n", old_memory_y + 2, memory_y);
        for (int row = 0; row < term.rows; ++row) { fprintf(stderr, "%s\n", term.cells[row]); }
    }
    CHECK(memory_y == old_memory_y + 2);

    test_terminal_lower(&term, 0, false);
    menu_at(&term, x, y);
    test_terminal_click(&term, "Split right");
    test_terminal_expect(&term, "Pane split right.");
    /* All seven pane headers remain available above/within the overlays. */
    CHECK(count_text(&term, "...") == 4);
    CHECK(test_terminal_find(&term, "...", &x, &y));
    int original_menu = x;
    /* Newly split pane occupies the remaining half, ending at old outer gutter. */
    int new_menu = old_menu + 5;
    menu_at(&term, new_menu, y);
    test_terminal_click(&term, "Split below");
    test_terminal_expect(&term, "Pane split below.");
    CHECK(count_text(&term, "...") == 5);

    int internal_y = -1;
    for (int row = y + 1; row < term.rows - 2; ++row) {
        if (strncmp(term.cells[row] + new_menu, "...", 3) == 0) { internal_y = row; break; }
    }
    CHECK(internal_y > y);
    mouse(&term, 0, new_menu - 3, internal_y - 1, false);
    test_terminal_expect(&term, "Dragging divider");
    mouse(&term, 32, new_menu - 3, internal_y, false);
    mouse(&term, 0, new_menu - 3, internal_y, true);
    test_terminal_expect(&term, "Divider resized.");
    CHECK(strncmp(term.cells[internal_y + 1] + new_menu, "...", 3) == 0);

    /* Compact navigation reaches every pane, including both new ones. */
    test_terminal_resize(&term, 24, 80);
    test_terminal_expect(&term, "8 total");
    test_terminal_send(&term, "7");
    test_terminal_expect(&term, "Pane 7");
    test_terminal_click(&term, "Next >");
    test_terminal_expect(&term, "Pane 8");
    test_terminal_send(&term, "[");
    test_terminal_expect(&term, "Pane 7");
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "Disassembly");
    CHECK(count_text(&term, "...") == 5);

    /* Find the internal lower pane's menu by its shared x and later y. */
    int lower_y = -1;
    for (int row = y + 1; row < term.rows - 2; ++row) {
        if (strncmp(term.cells[row] + new_menu, "...", 3) == 0) { lower_y = row; break; }
    }
    CHECK(lower_y > y);
    menu_at(&term, new_menu, lower_y);
    test_terminal_click(&term, "Close split pane");
    test_terminal_expect(&term, "Split pane closed");
    CHECK(count_text(&term, "...") == 4);
    menu_at(&term, new_menu, y);
    test_terminal_click(&term, "Close split pane");
    test_terminal_expect(&term, "Split pane closed");
    CHECK(count_text(&term, "...") == 3);
    CHECK(test_terminal_find(&term, "...", &x, &y));
    CHECK(x == old_menu + 5 && x > original_menu);

    /* Add/switch/close a tab while retaining the Source tab and its state. */
    test_terminal_click(&term, "...");
    test_terminal_expect(&term, "SHOW VIEW (DEMO)");
    test_terminal_click(&term, "Watches");
    test_terminal_expect(&term, "Expression");
    menu_at(&term, x, y);
    test_terminal_click(&term, "Close selected tab");
    test_terminal_expect(&term, "Selected tab closed.");
    test_terminal_click(&term, "Source");
    test_terminal_expect(&term, "main.c | Cortex-M4F sample");

    /* Keyboard-only mode selects a real divider and moves it one cell. */
    menu_at(&term, x, y);
    test_terminal_click(&term, "Resize dividers");
    test_terminal_expect(&term, "Resize: Tab selects divider");
    test_terminal_send(&term, "\033OC\r");
    test_terminal_expect(&term, "Divider resizing finished.");
    CHECK(test_terminal_find(&term, "...", &x, &y));
    CHECK(x == old_menu + 6);
    mouse(&term, 0, x + 5, y + 2, false);
    test_terminal_expect(&term, "Dragging divider");
    test_terminal_resize(&term, 24, 80);
    test_terminal_expect(&term, "1 Source");
    mouse(&term, 0, 20, 10, true); /* release after resize must not continue old drag */
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "Source");
    CHECK(test_terminal_find(&term, "...", &x, &y));
    CHECK(x == old_menu + 6);
    menu_at(&term, x, y);
    test_terminal_click(&term, "Close split pane");
    test_terminal_expect(&term, "Only nested split panes can close");

    test_terminal_resize(&term, 5, 15);
    test_terminal_expect(&term, "Terminal too");
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "Source");
    CHECK(count_text(&term, "...") == 3);
    test_terminal_stop(&term, 0);
    CHECK(strstr(term.output, "\033[?1002l") != NULL);
    puts("PASS M02 mouse drag, nested splits, seventh/eighth view, compact access, close and keyboard resize");

    /* A ninth-row click must survive resize and drive scrolling past row four. */
    start(&term, argv[1], "xterm-256color", false);
    test_terminal_click(&term, "0800043A");
    test_terminal_resize(&term, 18, 80);
    test_terminal_expect(&term, "2 Asm");
    test_terminal_click(&term, "Disassembly");
    test_terminal_click(&term, "08000436"); /* Last visible row above the dock. */
    test_terminal_send(&term, "\033OB\033OB");
    test_terminal_expect(&term, "0800043A");
    CHECK(!test_terminal_find(&term, "0800042A  B5 10", NULL, NULL));
    test_terminal_stop(&term, 0);

    start(&term, argv[1], "xterm-256color", false);
    test_terminal_resize(&term, 18, 80);
    test_terminal_send(&term, "2");
    test_terminal_expect(&term, "0800042A  B5 10");
    test_terminal_click(&term, "0800042A  B5 10");
    for (int i = 0; i < 12; ++i) { test_terminal_send(&term, "\033OB"); }
    test_terminal_expect(&term, "0800043A");
    CHECK(!test_terminal_find(&term, "0800042A  B5 10", NULL, NULL));
    test_terminal_send(&term, "\033[5~\033[5~"); /* PageUp back to first row */
    test_terminal_expect(&term, "0800042A  B5 10");
    CHECK(test_terminal_find(&term, "0800042A  B5 10", &x, &y));
    for (int i = 0; i < 3; ++i) { mouse(&term, 65, x, y, false); }
    test_terminal_expect(&term, "0800043A");
    CHECK(!test_terminal_find(&term, "0800042A  B5 10", NULL, NULL));
    test_terminal_stop(&term, 0);
    puts("PASS disassembly selection: ninth-row click, keyboard and wheel reach all instructions");

    start(&term, argv[1], "xterm-256color", false);
    test_terminal_resize(&term, 18, 80);
    test_terminal_expect(&term, "main.c | Cortex-M4F sample");
    CHECK(test_terminal_find(&term, "...", &x, &y));
    int scrollbar_x = x + 4;
    for (int i = 0; i < 4 && !test_terminal_find(&term, "#include <stdint.h>", NULL, NULL); ++i) {
        click_at(&term, scrollbar_x, y + 2); /* Page above the thumb. */
    }
    test_terminal_expect(&term, "#include <stdint.h>");
    mouse(&term, 0, scrollbar_x, y + 2, false);
    mouse(&term, 32, scrollbar_x, y + 8, false);
    mouse(&term, 0, scrollbar_x, y + 8, true);
    test_terminal_expect(&term, "counter++;");
    CHECK(!test_terminal_find(&term, "#include <stdint.h>", NULL, NULL));
    test_terminal_stop(&term, 0);
    puts("PASS pane scrollbar track paging and thumb drag reach source endpoints");

    start(&term, argv[1], "vt100", true);
    /* Tab into Source menu, choose actions, then keyboard split below. */
    for (int i = 0; i < 11; ++i) { test_terminal_send(&term, "\t"); }
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "SHOW VIEW (DEMO)");
    for (int i = 0; i < 13; ++i) { test_terminal_send(&term, "\t"); }
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "PANE ACTIONS");
    test_terminal_send(&term, "\033OB\r");
    test_terminal_expect(&term, "Pane split below.");
    CHECK(count_text(&term, "...") == 4);
    test_terminal_stop(&term, 0);
    puts("PASS M02 monochrome keyboard-only split flow");
    start(&term, argv[1], "xterm-256color", false);
    CHECK(test_terminal_find(&term, "...", &x, &y));
    toggle_view_at(&term, x, y, "Threads"); test_terminal_expect(&term, "View added to this pane.");
    CHECK(strstr(term.cells[y], "Threads"));
    toggle_view_at(&term, x, y, "Symbols"); test_terminal_expect(&term, "View removed from this pane.");
    CHECK(strstr(term.cells[y], "Threads") && !strstr(term.cells[y], "Symbols"));
    toggle_view_at(&term, x, y, "Threads"); test_terminal_expect(&term, "View removed from this pane.");
    CHECK(!strstr(term.cells[y], "Threads") && strstr(term.cells[y], "Source"));
    toggle_view_at(&term, x, y, "Source"); test_terminal_expect(&term, "Each pane needs at least one tab.");
    CHECK(strstr(term.cells[y], "Source"));
    test_terminal_send(&term, "\033"); test_terminal_stop(&term, 0);
    puts("PASS pane view menu adds/removes active and inactive tabs and protects the final tab");
    return EXIT_SUCCESS;
}
