#include "terminal.h"
#include "check.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long long milliseconds(void)
{
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void clear_screen(TestTerminal *term)
{
    for (int y = 0; y < TEST_ROWS; ++y) {
        memset(term->cells[y], ' ', TEST_COLS);
        term->cells[y][term->cols] = '\0';
    }
}

static void scroll_lines(TestTerminal *term, int top, int bottom, int distance)
{
    int count = distance < 0 ? -distance : distance;
    if (count > bottom - top + 1) { count = bottom - top + 1; }
    size_t size = (size_t)(bottom - top + 1 - count) * sizeof term->cells[0];
    if (distance > 0) {
        if (size > 0) { memmove(term->cells[top], term->cells[top + count], size); }
        for (int y = bottom - count + 1; y <= bottom; ++y) {
            memset(term->cells[y], ' ', (size_t)term->cols);
        }
    } else {
        if (size > 0) { memmove(term->cells[top + count], term->cells[top], size); }
        for (int y = top; y < top + count; ++y) {
            memset(term->cells[y], ' ', (size_t)term->cols);
        }
    }
}

static void csi(TestTerminal *term, char command)
{
    int first = 0, second = 0;
    (void)sscanf(term->sequence, "%d;%d", &first, &second);
    int distance = first == 0 ? 1 : first;
    switch (command) {
    case 'H': case 'f':
        term->y = first > 0 ? first - 1 : 0;
        term->x = second > 0 ? second - 1 : 0;
        break;
    case 'A': term->y -= distance; break;
    case 'B': term->y += distance; break;
    case 'C': term->x += distance; break;
    case 'D': term->x -= distance; break;
    case 'G': term->x = distance - 1; break;
    case 'd': term->y = distance - 1; break;
    case 'r':
        term->scroll_top = first > 0 ? first - 1 : 0;
        term->scroll_bottom = second > 0 ? second - 1 : term->rows - 1;
        if (term->scroll_top < 0 || term->scroll_bottom >= term->rows ||
            term->scroll_top >= term->scroll_bottom) {
            term->scroll_top = 0; term->scroll_bottom = term->rows - 1;
        }
        term->x = 0; term->y = 0;
        break;
    case 'L': case 'M':
        if (term->y >= term->scroll_top && term->y <= term->scroll_bottom) {
            scroll_lines(term, term->y, term->scroll_bottom, command == 'M' ? distance : -distance);
        }
        break;
    case 'S': case 'T':
        scroll_lines(term, term->scroll_top, term->scroll_bottom, command == 'S' ? distance : -distance);
        break;
    case 'J':
        if (first == 2 || first == 3) { clear_screen(term); }
        else {
            memset(term->cells[term->y] + term->x, ' ', (size_t)(term->cols - term->x));
            for (int y = term->y + 1; y < term->rows; ++y) {
                memset(term->cells[y], ' ', (size_t)term->cols);
            }
        }
        break;
    case 'K':
        if (first == 2) { memset(term->cells[term->y], ' ', (size_t)term->cols); }
        else if (first == 1) { memset(term->cells[term->y], ' ', (size_t)term->x + 1); }
        else { memset(term->cells[term->y] + term->x, ' ', (size_t)(term->cols - term->x)); }
        break;
    case 'P': case '@': {
        int count = distance < term->cols - term->x ? distance : term->cols - term->x;
        char *line = term->cells[term->y] + term->x;
        size_t tail = (size_t)(term->cols - term->x - count);
        if (command == 'P') {
            memmove(line, line + count, tail);
            memset(line + tail, ' ', (size_t)count);
        } else {
            memmove(line + count, line, tail);
            memset(line, ' ', (size_t)count);
        }
        break;
    }
    case 'X': {
        int count = distance < term->cols - term->x ? distance : term->cols - term->x;
        memset(term->cells[term->y] + term->x, ' ', (size_t)count);
        break;
    }
    default: break; /* Color, modes and cursor visibility. */
    }
    if (term->x < 0) { term->x = 0; }
    if (term->y < 0) { term->y = 0; }
    if (term->x >= term->cols) { term->x = term->cols - 1; }
    if (term->y >= term->rows) { term->y = term->rows - 1; }
}

static void consume(TestTerminal *term, unsigned char character)
{
    if (term->escape == 1) {
        if (character == '[') {
            term->escape = 2;
            term->sequence_length = 0;
            term->sequence[0] = '\0';
        } else if (character == '(' || character == ')') {
            term->escape = 3;
        } else {
            if (character == 'M') {
                if (term->y == term->scroll_top) { scroll_lines(term, term->scroll_top, term->scroll_bottom, -1); }
                else if (term->y > 0) { --term->y; }
            }
            term->escape = 0;
        }
        return;
    }
    if (term->escape == 3) { term->escape = 0; return; }
    if (term->escape == 2) {
        if (character >= '@' && character <= '~') {
            csi(term, (char)character);
            term->escape = 0;
        } else if (term->sequence_length < sizeof term->sequence - 1) {
            term->sequence[term->sequence_length++] = (char)character;
            term->sequence[term->sequence_length] = '\0';
        }
        return;
    }
    if (character == 27) { term->escape = 1; return; }
    if (character == '\r') { term->x = 0; return; }
    if (character == '\n') {
        if (term->y == term->scroll_bottom) { scroll_lines(term, term->scroll_top, term->scroll_bottom, 1); }
        else if (term->y < term->rows - 1) { ++term->y; }
        return;
    }
    if (character == '\b') { if (term->x > 0) { --term->x; } return; }
    if (character < 32 || character >= 127) { return; }
    if (term->x >= term->cols) {
        term->x = 0;
        if (term->y < term->rows - 1) { ++term->y; }
    }
    term->cells[term->y][term->x++] = (char)character;
}

void test_terminal_start_argv(TestTerminal *term, char *const arguments[], const char *type, bool no_color)
{
    memset(term, 0, sizeof *term);
    term->rows = 44;
    term->cols = 140;
    term->scroll_bottom = term->rows - 1;
    term->master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(term->master >= 0);
    CHECK(grantpt(term->master) == 0 && unlockpt(term->master) == 0);
    char *slave_path = ptsname(term->master);
    CHECK(slave_path != NULL);
    term->slave = open(slave_path, O_RDWR | O_NOCTTY);
    CHECK(term->slave >= 0 && tcgetattr(term->slave, &term->original) == 0);
    struct winsize size = {.ws_row = (unsigned short)term->rows, .ws_col = (unsigned short)term->cols};
    CHECK(ioctl(term->slave, TIOCSWINSZ, &size) == 0);
    clear_screen(term);
    term->child = fork();
    CHECK(term->child >= 0);
    if (term->child == 0) {
        (void)close(term->master);
        CHECK(setsid() >= 0);
        CHECK(ioctl(term->slave, TIOCSCTTY, 0) == 0);
        for (int fd = 0; fd < 3; ++fd) { CHECK(dup2(term->slave, fd) >= 0); }
        (void)close(term->slave);
        CHECK(setenv("TERM", type, 1) == 0);
        CHECK(setenv("LC_ALL", "C.UTF-8", 1) == 0);
        if (no_color) { CHECK(setenv("NO_COLOR", "1", 1) == 0); }
        else { CHECK(unsetenv("NO_COLOR") == 0); }
        execvp(arguments[0], arguments);
        _exit(127);
    }
}

void test_terminal_start(TestTerminal *term, const char *binary, const char *type, bool no_color)
{
    char *arguments[] = {(char *)binary, NULL};
    test_terminal_start_argv(term, arguments, type, no_color);
}

void test_terminal_send(TestTerminal *term, const char *input)
{
    size_t length = strlen(input), sent = 0;
    while (sent < length) {
        ssize_t count = write(term->master, input + sent, length - sent);
        CHECK(count > 0);
        sent += (size_t)count;
    }
}

void test_terminal_pump(TestTerminal *term, int milliseconds)
{
    struct pollfd descriptor = {.fd = term->master, .events = POLLIN};
    int ready = poll(&descriptor, 1, milliseconds);
    CHECK(ready >= 0 || errno == EINTR);
    if (ready <= 0 || !(descriptor.revents & POLLIN)) { return; }
    char buffer[8192];
    ssize_t length = read(term->master, buffer, sizeof buffer);
    CHECK(length > 0);
    CHECK(term->output_length + (size_t)length < sizeof term->output);
    memcpy(term->output + term->output_length, buffer, (size_t)length);
    term->output_length += (size_t)length;
    term->output[term->output_length] = '\0';
    for (ssize_t i = 0; i < length; ++i) { consume(term, (unsigned char)buffer[i]); }
}

bool test_terminal_find(const TestTerminal *term, const char *text, int *x, int *y)
{
    for (int row = 0; row < term->rows; ++row) {
        const char *found = strstr(term->cells[row], text);
        if (found != NULL) {
            if (x != NULL) { *x = (int)(found - term->cells[row]); }
            if (y != NULL) { *y = row; }
            return true;
        }
    }
    return false;
}

void test_terminal_expect(TestTerminal *term, const char *text)
{
    /* Bounded wait even when no output is produced. */
    long long deadline = milliseconds() + 5000;
    while (milliseconds() < deadline) {
        if (test_terminal_find(term, text, NULL, NULL)) {
            /* Let a fragmented curses refresh finish before locating controls. */
            long long settle = milliseconds() + 40;
            while (milliseconds() < settle) { test_terminal_pump(term, 5); }
            if (test_terminal_find(term, text, NULL, NULL)) { return; }
        }
        test_terminal_pump(term, 50);
    }
    fprintf(stderr, "Expected on screen: %s\n", text);
    for (int row = 0; row < term->rows; ++row) { fprintf(stderr, "%s\n", term->cells[row]); }
    fprintf(stderr, "Raw terminal output:\n%s\n", term->output);
    (void)kill(term->child, SIGKILL);
    (void)waitpid(term->child, NULL, 0);
    CHECK(false);
}

void test_terminal_click(TestTerminal *term, const char *text)
{
    test_terminal_expect(term, text);
    int x, y;
    CHECK(test_terminal_find(term, text, &x, &y));
    char input[64];
    (void)snprintf(input, sizeof input, "\033[<0;%d;%dM", x + 1, y + 1);
    test_terminal_send(term, input);
    long long settle = milliseconds() + 60;
    while (milliseconds() < settle) { test_terminal_pump(term, 5); }
    (void)snprintf(input, sizeof input, "\033[<0;%d;%dm", x + 1, y + 1);
    test_terminal_send(term, input);
    settle = milliseconds() + 60;
    while (milliseconds() < settle) { test_terminal_pump(term, 5); }
}

void test_terminal_resize(TestTerminal *term, int rows, int cols)
{
    CHECK(rows > 0 && rows <= TEST_ROWS && cols > 0 && cols <= TEST_COLS);
    term->rows = rows; term->cols = cols;
    term->scroll_top = 0; term->scroll_bottom = rows - 1;
    clear_screen(term);
    struct winsize size = {.ws_row = (unsigned short)rows, .ws_col = (unsigned short)cols};
    CHECK(ioctl(term->slave, TIOCSWINSZ, &size) == 0);
    CHECK(kill(term->child, SIGWINCH) == 0);
    test_terminal_pump(term, 100);
}

void test_terminal_stop(TestTerminal *term, int signal_number)
{
    if (signal_number != 0) { CHECK(kill(term->child, signal_number) == 0); }
    else { test_terminal_send(term, "\021"); }
    int status = 0;
    bool exited = false;
    long long deadline = milliseconds() + 5000;
    while (milliseconds() < deadline) {
        if (waitpid(term->child, &status, WNOHANG) == term->child) { exited = true; break; }
        test_terminal_pump(term, 50);
    }
    if (!exited) {
        (void)kill(term->child, SIGKILL);
        (void)waitpid(term->child, NULL, 0);
        CHECK(false);
    }
    struct termios after;
    CHECK(tcgetattr(term->slave, &after) == 0);
    CHECK(after.c_iflag == term->original.c_iflag && after.c_oflag == term->original.c_oflag);
    CHECK(after.c_cflag == term->original.c_cflag && after.c_lflag == term->original.c_lflag);
    CHECK(memcmp(after.c_cc, term->original.c_cc, sizeof after.c_cc) == 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == (signal_number == 0 ? 0 : 128 + signal_number));
    CHECK(strstr(term->output, "ERROR: AddressSanitizer") == NULL);
    CHECK(strstr(term->output, "runtime error:") == NULL);
    CHECK(strstr(term->output, "LeakSanitizer") == NULL);
    (void)close(term->master); (void)close(term->slave);
}
