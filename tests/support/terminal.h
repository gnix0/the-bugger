#ifndef THE_BUGGER_TEST_TERMINAL_H
#define THE_BUGGER_TEST_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

enum { TEST_ROWS = 64, TEST_COLS = 200, TEST_OUTPUT = 262144 };

/* Test-only VT reader for ASCII labels and single-cell UTF-8 box drawing. */
typedef struct {
    pid_t child;
    int master, slave, rows, cols, x, y, escape;
    int scroll_top, scroll_bottom;
    int saved_x, saved_y;
    struct termios original;
    char cells[TEST_ROWS][TEST_COLS + 1];
    bool reverse, inverse[TEST_ROWS][TEST_COLS];
    bool underlined, underline[TEST_ROWS][TEST_COLS];
    unsigned utf8_value, utf8_remaining;
    char sequence[128];
    size_t sequence_length;
    char output[TEST_OUTPUT];
    size_t output_length;
} TestTerminal;

void test_terminal_start(TestTerminal *term, const char *binary, const char *type, bool no_color);
void test_terminal_start_argv(TestTerminal *term, char *const arguments[], const char *type, bool no_color);
void test_terminal_send(TestTerminal *term, const char *input);
void test_terminal_pump(TestTerminal *term, int milliseconds);
bool test_terminal_find(const TestTerminal *term, const char *text, int *x, int *y);
void test_terminal_expect(TestTerminal *term, const char *text);
void test_terminal_click(TestTerminal *term, const char *text);
void test_terminal_resize(TestTerminal *term, int rows, int cols);
void test_terminal_stop(TestTerminal *term, int signal_number);

#endif
