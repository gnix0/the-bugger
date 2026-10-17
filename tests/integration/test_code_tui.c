#include "../support/terminal.h"
#include "../support/check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long long now(void)
{
    struct timespec value; CHECK(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (long long)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static void source_pc(TestTerminal *term, const char *text)
{
    bool found = false;
    long long until = now() + 5000;
    while (!found && now() < until) {
        test_terminal_pump(term, 50);
        int x, y;
        if (test_terminal_find(term, text, &x, &y)) {
            for (int i = 0; i < x && i < 8; ++i) { if (term->cells[y][i] == '>') { found = true; } }
        }
    }
    if (!found) {
        fprintf(stderr, "Expected source PC at %s\n", text);
        for (int row = 0; row < term->rows; ++row) { fprintf(stderr, "%s\n", term->cells[row]); }
    }
    CHECK(found);
}
static unsigned long long assembly_pc(const TestTerminal *term)
{
    for (int y = 0; y < term->rows - 2; ++y) {
        const char *marker = strstr(term->cells[y], "> 0x");
        if (marker) { return strtoull(marker + 2, NULL, 16); }
    }
    return 0;
}
static int source_pc_line(const TestTerminal *term)
{
    for (int y = 0; y < term->rows - 2; ++y) {
        const char *marker = memchr(term->cells[y], '>', 7);
        int line;
        if (marker && sscanf(marker + 1, "%d", &line) == 1) { return line; }
    }
    return 0;
}
static void input(TestTerminal *term, const char *text)
{
    test_terminal_send(term, text); test_terminal_send(term, "\r");
}
static void actions(TestTerminal *term, bool source)
{
    /* Click the Actions footer belonging to the requested code column. */
    int x = -1, y = -1;
    for (int row = 0; row < term->rows - 2; ++row) {
        const char *at = term->cells[row];
        while ((at = strstr(at, "Actions")) != NULL) {
            int column = (int)(at - term->cells[row]);
            if ((source && column < 45) || (!source && column >= 45 && column < 95)) { x = column; y = row; break; }
            ++at;
        }
        if (x >= 0) { break; }
    }
    CHECK(x >= 0);
    char sequence[64]; (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dM", x + 1, y + 1);
    test_terminal_send(term, sequence);
    long long settle = now() + 60;
    while (now() < settle) { test_terminal_pump(term, 5); }
    (void)snprintf(sequence, sizeof sequence, "\033[<0;%d;%dm", x + 1, y + 1);
    test_terminal_send(term, sequence);
    settle = now() + 60;
    while (now() < settle) { test_terminal_pump(term, 5); }
    test_terminal_expect(term, "Code view actions");
}

static void mouse_at(TestTerminal *term, int button, int x, int y, bool release)
{
    char sequence[64];
    (void)snprintf(sequence, sizeof sequence, "\033[<%d;%d;%d%c", button, x + 1, y + 1, release ? 'm' : 'M');
    test_terminal_send(term, sequence);
    long long settle = now() + 60;
    while (now() < settle) { test_terminal_pump(term, 5); }
}

static void horizontal_drag(TestTerminal *term, const char *footer, int right, bool to_end)
{
    int x, y; CHECK(test_terminal_find(term, footer, &x, &y)); --y;
    int first = -1, last = -1, thumb = -1;
    for (int column = x; column < right; ++column) {
        char cell = term->cells[y][column];
        if (cell == '=' || cell == '-') {
            if (first < 0) { first = column; }
            last = column;
            if (cell == '=' && thumb < 0) { thumb = column; }
        }
    }
    CHECK(first >= 0 && last - first > 8 && thumb >= 0);
    mouse_at(term, 0, thumb, y, false);
    mouse_at(term, 32, to_end ? last : first, y, false);
    mouse_at(term, 0, to_end ? last : first, y, true);
}

static void scrolling(TestTerminal *term)
{
    unsigned long long pc = assembly_pc(term); CHECK(pc);
    int menu_x, menu_y; CHECK(test_terminal_find(term, "...", &menu_x, &menu_y));
    CHECK(term->cells[menu_y + 2][menu_x + 4] == '#' || term->cells[menu_y + 2][menu_x + 4] == '|');
    CHECK(term->cells[menu_y + 2][menu_x + 3] != term->cells[menu_y + 2][menu_x + 4]);
    horizontal_drag(term, "Actions", 46, true);
    test_terminal_expect(term, "CODE_FIND_MARKER");
    test_terminal_click(term, "CODE_FIND_MARKER");
    for (int i = 0; i < 30; ++i) { test_terminal_send(term, "\033OD"); }
    test_terminal_expect(term, "volatile int code_counter");
    horizontal_drag(term, "Actions", 46, false);
    CHECK(source_pc_line(term) > 0);
    int track_x, track_y; CHECK(test_terminal_find(term, "Actions", &track_x, &track_y));
    --track_y; track_x = 45;
    while (track_x > 10 && term->cells[track_y][track_x] != '-') { --track_x; }
    CHECK(track_x > 10);
    mouse_at(term, 0, track_x, track_y, false);
    mouse_at(term, 0, track_x, track_y, true);
    test_terminal_expect(term, "CODE_FIND_MARKER");
    horizontal_drag(term, "Actions", 46, false);
    test_terminal_click(term, "Command"); test_terminal_expect(term, "GDB console command");
    input(term, "echo HORIZONTAL_BEGIN_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_HORIZONTAL_END\\n");
    test_terminal_expect(term, "HORIZONTAL_BEGIN");
    CHECK(!test_terminal_find(term, "HORIZONTAL_END", NULL, NULL));
    horizontal_drag(term, "Command", term->cols - 1, true);
    test_terminal_expect(term, "HORIZONTAL_END");
    CHECK(!test_terminal_find(term, "HORIZONTAL_BEGIN", NULL, NULL));
    test_terminal_expect(term, "volatile int code_counter");
    horizontal_drag(term, "Command", term->cols - 1, false);
    test_terminal_expect(term, "HORIZONTAL_BEGIN");
    CHECK(assembly_pc(term) == pc);
    test_terminal_resize(term, 60, 200);
    test_terminal_expect(term, "LIVE | C/C++ | code.c");
    int x, y; CHECK(test_terminal_find(term, "...", &x, &y));
    mouse_at(term, 0, x + 5, y + 2, false);
    mouse_at(term, 32, x + 30, y + 2, false);
    mouse_at(term, 0, x + 30, y + 2, true);
    CHECK(test_terminal_find(term, "...", &x, &y)); int edge = x + 5;
    CHECK(test_terminal_find(term, "Actions", &x, &y));
    CHECK(!memchr(term->cells[y - 1], '=', (size_t)edge));
    CHECK(term->cells[y - 2][edge - 1] != '#' && term->cells[y - 2][edge - 1] != '|');
    test_terminal_resize(term, 44, 140);
    test_terminal_expect(term, "LIVE | C/C++ | code.c");
    puts("PASS M07 source/Console horizontal thumb drag, independent offsets, PC preservation and resize overflow visibility");
}

static void instruction_margin(TestTerminal *term)
{
    test_terminal_click(term, "Disassembly");
    test_terminal_expect(term, "instruction stepping");
    int tab_x, tab_y; CHECK(test_terminal_find(term, "Disassembly", &tab_x, &tab_y));
    int footer = -1;
    for (int row = tab_y + 3; row < term->rows - 2; ++row) {
        if (strstr(term->cells[row] + tab_x, "Actions")) { footer = row; break; }
    }
    CHECK(footer > tab_y + 10);
    int stable = 0;
    for (int step = 0; step < 20; ++step) {
        char top[32]; (void)snprintf(top, sizeof top, "%.24s", term->cells[tab_y + 2] + tab_x);
        unsigned long long pc = assembly_pc(term); CHECK(pc);
        test_terminal_send(term, "\033[23~");
        long long until = now() + 5000;
        while ((!assembly_pc(term) || assembly_pc(term) == pc) && now() < until) { test_terminal_pump(term, 50); }
        CHECK(assembly_pc(term) && assembly_pc(term) != pc);
        int pc_row = -1;
        for (int row = tab_y + 2; row < footer; ++row) {
            if (strstr(term->cells[row] + tab_x - 2, "> 0x")) { pc_row = row; break; }
        }
        if (!(pc_row >= tab_y + 2 && pc_row <= footer - 7)) {
            fprintf(stderr, "step=%d tab=%d,%d footer=%d pcrow=%d\n", step, tab_x, tab_y, footer, pc_row);
            for (int row = tab_y; row <= footer; ++row) { fprintf(stderr, "%s\n", term->cells[row]); }
            CHECK(false);
        }
        if (strncmp(top, term->cells[tab_y + 2] + tab_x, strlen(top)) == 0) { ++stable; }
    }
    CHECK(stable >= 3);
    /* Browse until PC is near the top, then resume following by stepping. */
    unsigned long long pc = assembly_pc(term);
    int pc_row = -1;
    for (int row = tab_y + 2; row < footer; ++row) {
        if (strstr(term->cells[row] + tab_x - 2, "> 0x")) { pc_row = row; break; }
    }
    CHECK(pc_row >= tab_y + 7);
    for (int wheel = 0; pc_row > tab_y + 5 && wheel < term->rows; ++wheel) {
        mouse_at(term, 65, tab_x + 3, tab_y + 3, false);
        CHECK(assembly_pc(term) == pc);
        pc_row = -1;
        for (int row = tab_y + 2; row < footer; ++row) {
            if (strstr(term->cells[row] + tab_x - 2, "> 0x")) { pc_row = row; break; }
        }
    }
    CHECK(pc_row >= tab_y + 2 && pc_row <= tab_y + 5);
    test_terminal_send(term, "\033[21~"); /* instruction Next, without entering a callee */
    long long until = now() + 5000;
    while ((!assembly_pc(term) || assembly_pc(term) == pc) && now() < until) { test_terminal_pump(term, 50); }
    CHECK(assembly_pc(term) && assembly_pc(term) != pc);
    pc_row = -1;
    for (int row = tab_y + 2; row < footer; ++row) {
        if (strstr(term->cells[row] + tab_x - 2, "> 0x")) { pc_row = row; break; }
    }
    CHECK(pc_row >= tab_y + 7 && pc_row <= footer - 7);
    puts("PASS M07 instruction stepping preserves the viewport with five-row top and bottom margins");
}
static void direct_start(const char *binary, const char *fixture)
{
    TestTerminal term;
    char *const args[] = {(char *)binary, NULL};
    test_terminal_start_argv(&term, args, "xterm-256color", false);
    test_terminal_expect(&term, "Native Linux"); test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF"); test_terminal_send(&term, "\t\025"); test_terminal_send(&term, fixture);
    test_terminal_click(&term, "Review session"); test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_expect(&term, "Awaiting stop | C/C++");
    test_terminal_expect(&term, "Awaiting stop | assembly");
    CHECK(!test_terminal_find(&term, "STALE", NULL, NULL));
    test_terminal_click(&term, "Command"); test_terminal_expect(&term, "GDB console command"); input(&term, "start");
    test_terminal_expect(&term, "LIVE | C/C++ | code.c");
    test_terminal_expect(&term, "LIVE | assembly | code.c");
    CHECK(source_pc_line(&term) > 0 && assembly_pc(&term) != 0);
    scrolling(&term);
    instruction_margin(&term);
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Terminate the launched program");
    test_terminal_click(&term, "Close session"); test_terminal_stop(&term, 0);
    puts("PASS M07 direct Console start without file browsing clears stale source/assembly");
}
static void wheel_up_listing(const char *binary, const char *fixture)
{
    TestTerminal term; char *const args[] = {(char *)binary, NULL};
    test_terminal_start_argv(&term, args, "xterm-256color", false);
    test_terminal_expect(&term, "Native Linux"); test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF"); test_terminal_send(&term, "\t\025"); test_terminal_send(&term, fixture);
    test_terminal_click(&term, "Review session"); test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_click(&term, "Command"); test_terminal_expect(&term, "GDB console command"); input(&term, "start");
    test_terminal_expect(&term, "LIVE | assembly | session.c");
    unsigned long long pc = assembly_pc(&term); CHECK(pc);
    test_terminal_click(&term, "Disassembly");
    int x, y; CHECK(test_terminal_find(&term, "Disassembly", &x, &y));
    for (int repeat = 0; repeat < 3; ++repeat) { mouse_at(&term, 64, x + 3, y + 3, false); }
    long long until = now() + 500;
    while (now() < until) { test_terminal_pump(&term, 10); }
    int rows = 0;
    for (int row = y + 2; row < y + 15; ++row) {
        if (strstr(term.cells[row] + x - 2, "0x")) { ++rows; }
    }
    CHECK(rows >= 8 && assembly_pc(&term) == pc);
    mouse_at(&term, 65, x + 3, y + 3, false);
    until = now() + 300; while (now() < until) { test_terminal_pump(&term, 10); }
    rows = 0;
    for (int row = y + 2; row < y + 15; ++row) { if (strstr(term.cells[row] + x - 2, "0x")) { ++rows; } }
    CHECK(rows >= 8);
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Terminate the launched program");
    test_terminal_click(&term, "Close session"); test_terminal_stop(&term, 0);
    puts("PASS M07 real session fixture wheel-up at the first window preserves forward instructions and PC");
}

int main(int argc, char *argv[])
{
    CHECK(argc == 3 || argc == 4);
    if (argc == 4) { wheel_up_listing(argv[1], argv[3]); }
    direct_start(argv[1], argv[2]); TestTerminal term;
    char *const args[] = {argv[1], NULL};
    test_terminal_start_argv(&term, args, "xterm-256color", false);
    test_terminal_expect(&term, "Native Linux"); test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF"); test_terminal_send(&term, "\t\025"); test_terminal_send(&term, argv[2]);
    test_terminal_click(&term, "Review session"); test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Loaded");
    actions(&term, true); test_terminal_click(&term, "Choose source file");
    test_terminal_expect(&term, "GDB source files"); input(&term, "code.c");
    test_terminal_click(&term, "tests/fixtures/native/code.c");
    test_terminal_expect(&term, "#define _POSIX_C_SOURCE");
    CHECK(assembly_pc(&term) == 0); /* Browsing a loaded ELF invents no PC. */
    test_terminal_click(&term, "Command"); test_terminal_expect(&term, "GDB console command"); input(&term, "start");
    long long first_stop = now() + 5000;
    while (!source_pc_line(&term) && now() < first_stop) { test_terminal_pump(&term, 50); }
    CHECK(source_pc_line(&term) == 13 || source_pc_line(&term) == 14);
    if (source_pc_line(&term) == 13) { test_terminal_send(&term, "\033[21~"); }
    source_pc(&term, "code_counter = 1");
    test_terminal_expect(&term, "source stepping");
    test_terminal_send(&term, "\033[21~"); source_pc(&term, "code_counter += 2");
    test_terminal_click(&term, "Disassembly"); test_terminal_expect(&term, "instruction stepping");
    test_terminal_click(&term, "Locals"); test_terminal_expect(&term, "instruction stepping");
    unsigned long long pc = assembly_pc(&term); CHECK(pc != 0);
    char previous_pc[32]; (void)snprintf(previous_pc, sizeof previous_pc, "%llx", pc);
    test_terminal_send(&term, "\033[21~");
    long long until = now() + 5000;
    while ((assembly_pc(&term) == 0 || assembly_pc(&term) == pc) && now() < until) { test_terminal_pump(&term, 50); }
    CHECK(assembly_pc(&term) != 0 && assembly_pc(&term) != pc);
    test_terminal_expect(&term, previous_pc);
    int previous_x, previous_y; CHECK(test_terminal_find(&term, previous_pc, &previous_x, &previous_y));
    CHECK(previous_x >= 45 && previous_x < 95); /* Prior instruction remains in the assembly pane. */
    test_terminal_expect(&term, "instruction stepping");
    test_terminal_click(&term, "Source"); test_terminal_expect(&term, "source stepping");
    actions(&term, true); test_terminal_click(&term, "Find in current file"); input(&term, "CODE_FIND_MARKER");
    test_terminal_expect(&term, "Browsing source line 14");
    actions(&term, true); test_terminal_click(&term, "Go to line"); input(&term, "1");
    test_terminal_expect(&term, "#define _POSIX_C_SOURCE");
    test_terminal_click(&term, "Stopped"); source_pc(&term, "code_counter += 2");
    actions(&term, false); test_terminal_click(&term, "Go to address / symbol"); input(&term, "code_callee");
    test_terminal_expect(&term, "Instruction window loaded");
    actions(&term, false); test_terminal_click(&term, "Toggle mixed source/assembly");
    test_terminal_expect(&term, "mixed | code.c");
    actions(&term, true); test_terminal_click(&term, "Source lookup settings");
    test_terminal_expect(&term, "Directories: newest first"); test_terminal_click(&term, "Add directory"); input(&term, "/tmp");
    test_terminal_expect(&term, "GDB source settings applied");
    test_terminal_click(&term, "Source");
    test_terminal_send(&term, "\033[21~"); source_pc(&term, "code_result = code_callee");
    test_terminal_send(&term, "\033[23~"); source_pc(&term, "code_local = value");
    test_terminal_expect(&term, "Out Shift+F11");
    test_terminal_send(&term, "\033[23;2~");
    until = now() + 5000;
    while (source_pc_line(&term) != 16 && source_pc_line(&term) != 17 && now() < until) { test_terminal_pump(&term, 50); }
    CHECK(source_pc_line(&term) == 16 || source_pc_line(&term) == 17);
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Terminate the launched program");
    test_terminal_click(&term, "Close session"); test_terminal_stop(&term, 0);
    puts("PASS M07 actual TUI loaded-file browse, live code/PC, F10 modes retained by inspectors, source find/line/return, symbol/mixed navigation and source settings");
    return 0;
}
