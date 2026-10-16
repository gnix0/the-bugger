#include "../support/terminal.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static void mouse(TestTerminal *term, int button, int x, int y, bool release)
{
    char sequence[64];
    (void)snprintf(sequence, sizeof sequence, "\033[<%d;%d;%d%c", button, x + 1, y + 1, release ? 'm' : 'M');
    test_terminal_send(term, sequence);
    test_terminal_pump(term, 60);
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
    for (int y = 0; y < term->rows - 2; ++y) {
        const char *p = term->cells[y];
        while ((p = strstr(p, text)) != NULL) { ++count; p += strlen(text); }
    }
    return count;
}

int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    TestTerminal term;
    start(&term, argv[1], "xterm-256color", false);
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

    menu_at(&term, x, y);
    test_terminal_click(&term, "Split right");
    test_terminal_expect(&term, "Pane split right.");
    CHECK(count_text(&term, "DEMO") >= 8); /* ribbon + seven pane footers */
    CHECK(count_text(&term, "...") == 7);
    CHECK(test_terminal_find(&term, "...", &x, &y));
    int original_menu = x;
    /* Newly split pane occupies the remaining half, ending at old outer gutter. */
    int new_menu = old_menu + 5;
    menu_at(&term, new_menu, y);
    test_terminal_click(&term, "Split below");
    test_terminal_expect(&term, "Pane split below.");
    CHECK(count_text(&term, "...") == 8);

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
    CHECK(count_text(&term, "...") == 8);

    /* Find the internal lower pane's menu by its shared x and later y. */
    int lower_y = -1;
    for (int row = y + 1; row < term.rows - 2; ++row) {
        if (strncmp(term.cells[row] + new_menu, "...", 3) == 0) { lower_y = row; break; }
    }
    CHECK(lower_y > y);
    menu_at(&term, new_menu, lower_y);
    test_terminal_click(&term, "Close split pane");
    test_terminal_expect(&term, "Split pane closed");
    CHECK(count_text(&term, "...") == 7);
    menu_at(&term, new_menu, y);
    test_terminal_click(&term, "Close split pane");
    test_terminal_expect(&term, "Split pane closed");
    CHECK(count_text(&term, "...") == 6);
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
    test_terminal_expect(&term, "DEMO:");
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "Source");
    CHECK(count_text(&term, "...") == 6);
    test_terminal_stop(&term, 0);
    CHECK(strstr(term.output, "\033[?1002l") != NULL);
    puts("PASS M02 mouse drag, nested splits, seventh/eighth view, compact access, close and keyboard resize");

    /* A ninth-row click must survive resize and drive scrolling past row four. */
    start(&term, argv[1], "xterm-256color", false);
    test_terminal_click(&term, "0800043A");
    test_terminal_resize(&term, 18, 80);
    test_terminal_expect(&term, "2 Asm");
    test_terminal_click(&term, "Disassembly");
    test_terminal_send(&term, "\t\t\t\033OB"); /* focus body, move down */
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
    click_at(&term, scrollbar_x, y + 2); /* page above the thumb */
    test_terminal_expect(&term, "#include <stdint.h>");
    mouse(&term, 0, scrollbar_x, y + 2, false);
    mouse(&term, 32, scrollbar_x, y + 9, false);
    mouse(&term, 0, scrollbar_x, y + 9, true);
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
    CHECK(count_text(&term, "...") == 7);
    test_terminal_stop(&term, 0);
    puts("PASS M02 monochrome keyboard-only split flow");
    return EXIT_SUCCESS;
}
