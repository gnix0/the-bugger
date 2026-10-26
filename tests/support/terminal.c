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

static char isolated_config[] = "/tmp/the-bugger-terminal-config-XXXXXX";
static bool config_ready;
static void config_cleanup(void) { (void)rmdir(isolated_config); }

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
        memset(term->inverse[y], term->reverse, sizeof term->inverse[y]);
        memset(term->underline[y], term->underlined, sizeof term->underline[y]);
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
        memmove(term->inverse[top], term->inverse[top + count],
                (size_t)(bottom - top + 1 - count) * sizeof term->inverse[0]);
        memmove(term->underline[top], term->underline[top + count],
                (size_t)(bottom - top + 1 - count) * sizeof term->underline[0]);
        for (int y = bottom - count + 1; y <= bottom; ++y) {
            memset(term->cells[y], ' ', (size_t)term->cols);
            memset(term->inverse[y], term->reverse, (size_t)term->cols * sizeof(bool));
            memset(term->underline[y], term->underlined, (size_t)term->cols * sizeof(bool));
        }
    } else {
        if (size > 0) { memmove(term->cells[top + count], term->cells[top], size); }
        memmove(term->inverse[top + count], term->inverse[top],
                (size_t)(bottom - top + 1 - count) * sizeof term->inverse[0]);
        memmove(term->underline[top + count], term->underline[top],
                (size_t)(bottom - top + 1 - count) * sizeof term->underline[0]);
        for (int y = top; y < top + count; ++y) {
            memset(term->cells[y], ' ', (size_t)term->cols);
            memset(term->inverse[y], term->reverse, (size_t)term->cols * sizeof(bool));
            memset(term->underline[y], term->underlined, (size_t)term->cols * sizeof(bool));
        }
    }
}

static void sgr(TestTerminal *term)
{
    const char *next = term->sequence;
    if (*next == '\0') { term->reverse = false; term->underlined = false; }
    while (*next != '\0') {
        char *end;
        long code = strtol(next, &end, 10);
        if (end == next) { return; }
        next = *end == ';' ? end + 1 : end;
        if (code == 0 || code == 24) { term->underlined = false; }
        else if (code == 4) { term->underlined = true; }
        if (code == 0 || code == 27) { term->reverse = false; }
        else if (code == 7) { term->reverse = true; }
        else if (code == 38 || code == 48 || code == 58) {
            /* A palette index/RGB component of 7 is not an inverse attribute. */
            long mode = strtol(next, &end, 10);
            if (end == next) { return; }
            next = *end == ';' ? end + 1 : end;
            int components = mode == 2 ? 3 : mode == 5 ? 1 : 0;
            for (int i = 0; i < components && *next != '\0'; ++i) {
                (void)strtol(next, &end, 10);
                if (end == next) { return; }
                next = *end == ';' ? end + 1 : end;
            }
        }
    }
}

static void csi(TestTerminal *term, char command)
{
    int first = 0, second = 0;
    (void)sscanf(term->sequence, "%d;%d", &first, &second);
    int distance = first == 0 ? 1 : first;
    switch (command) {
    case 'm': sgr(term); break;
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
            memset(term->inverse[term->y] + term->x, term->reverse,
                    (size_t)(term->cols - term->x) * sizeof(bool));
            memset(term->underline[term->y] + term->x, term->underlined,
                    (size_t)(term->cols - term->x) * sizeof(bool));
            for (int y = term->y + 1; y < term->rows; ++y) {
                memset(term->cells[y], ' ', (size_t)term->cols);
                memset(term->inverse[y], term->reverse, (size_t)term->cols * sizeof(bool));
                memset(term->underline[y], term->underlined, (size_t)term->cols * sizeof(bool));
            }
        }
        break;
    case 'K':
        if (first == 2) { memset(term->cells[term->y], ' ', (size_t)term->cols); }
        else if (first == 1) { memset(term->cells[term->y], ' ', (size_t)term->x + 1); }
        else { memset(term->cells[term->y] + term->x, ' ', (size_t)(term->cols - term->x)); }
        {
            int start = first == 1 || first == 2 ? 0 : term->x;
            int count = first == 1 ? term->x + 1 : term->cols - start;
            memset(term->inverse[term->y] + start, term->reverse, (size_t)count * sizeof(bool));
            memset(term->underline[term->y] + start, term->underlined, (size_t)count * sizeof(bool));
        }
        break;
    case 'P': case '@': {
        int count = distance < term->cols - term->x ? distance : term->cols - term->x;
        char *line = term->cells[term->y] + term->x;
        bool *inverse = term->inverse[term->y] + term->x;
        bool *underline = term->underline[term->y] + term->x;
        size_t tail = (size_t)(term->cols - term->x - count);
        if (command == 'P') {
            memmove(line, line + count, tail);
            memset(line + tail, ' ', (size_t)count);
            memmove(inverse, inverse + count, tail * sizeof(bool));
            memmove(underline, underline + count, tail * sizeof(bool));
            memset(inverse + tail, term->reverse, (size_t)count * sizeof(bool));
            memset(underline + tail, term->underlined, (size_t)count * sizeof(bool));
        } else {
            memmove(line + count, line, tail);
            memset(line, ' ', (size_t)count);
            memmove(inverse + count, inverse, tail * sizeof(bool));
            memmove(underline + count, underline, tail * sizeof(bool));
            memset(inverse, term->reverse, (size_t)count * sizeof(bool));
            memset(underline, term->underlined, (size_t)count * sizeof(bool));
        }
        break;
    }
    case 'X': {
        int count = distance < term->cols - term->x ? distance : term->cols - term->x;
        memset(term->cells[term->y] + term->x, ' ', (size_t)count);
        memset(term->inverse[term->y] + term->x, term->reverse, (size_t)count * sizeof(bool));
        memset(term->underline[term->y] + term->x, term->underlined, (size_t)count * sizeof(bool));
        break;
    }
    default: break; /* Other modes and cursor visibility. */
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
            if (character == '7') { term->saved_x = term->x; term->saved_y = term->y; }
            if (character == '8') { term->x = term->saved_x; term->y = term->saved_y; }
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
    /* tmux converts ACS borders into UTF-8. Preserve their cell positions and
     * attributes rather than silently dropping the bytes from the test screen. */
    if (term->utf8_remaining) {
        if ((character & 0xc0U) == 0x80U) {
            term->utf8_value = (term->utf8_value << 6) | (character & 0x3fU);
            if (--term->utf8_remaining) { return; }
            unsigned value = term->utf8_value;
            if (value < 0x2500U || value > 0x257fU) { return; }
            character = value == 0x2500U ? '-' : value == 0x2502U ? '|' : '+';
        } else { term->utf8_remaining = 0; }
    } else if (character >= 0xc2U && character <= 0xf4U) {
        term->utf8_remaining = character < 0xe0U ? 1U : character < 0xf0U ? 2U : 3U;
        term->utf8_value = character & (term->utf8_remaining == 1 ? 0x1fU : term->utf8_remaining == 2 ? 0x0fU : 0x07U);
        return;
    }
    if (character < 32 || character >= 127) { return; }
    if (term->x >= term->cols) {
        term->x = 0;
        if (term->y < term->rows - 1) { ++term->y; }
    }
    term->inverse[term->y][term->x] = term->reverse;
    term->underline[term->y][term->x] = term->underlined;
    term->cells[term->y][term->x++] = (char)character;
}

void test_terminal_start_argv(TestTerminal *term, char *const arguments[], const char *type, bool no_color)
{
    const char *config = getenv("TB_TEST_CONFIG_HOME");
    if (!config) {
        if (!config_ready) {
            CHECK(mkdtemp(isolated_config) != NULL && atexit(config_cleanup) == 0);
            config_ready = true;
        }
        config = isolated_config;
    }
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
        CHECK(setenv("XDG_CONFIG_HOME", config, 1) == 0);
        if (no_color) { CHECK(setenv("NO_COLOR", "1", 1) == 0); }
        else { CHECK(unsetenv("NO_COLOR") == 0); }
        execvp(arguments[0], arguments);
        _exit(127);
    }
}

void test_terminal_start(TestTerminal *term, const char *binary, const char *type, bool no_color)
{
    char *arguments[] = {(char *)binary, "--demo", NULL};
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
    if (term->output_length + (size_t)length >= sizeof term->output) {
        /* Keep a bounded diagnostic tail independently of the parsed screen.
         * Check sanitizer evidence before discarding any captured bytes. */
        CHECK(strstr(term->output, "ERROR: AddressSanitizer") == NULL);
        CHECK(strstr(term->output, "runtime error:") == NULL);
        CHECK(strstr(term->output, "LeakSanitizer") == NULL);
        size_t retain = sizeof term->output / 2;
        memmove(term->output, term->output + term->output_length - retain, retain);
        term->output_length = retain;
    }
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
    fputs("Raw terminal tail (escaped, at most 1024 bytes):\n", stderr);
    size_t start = term->output_length > 1024 ? term->output_length - 1024 : 0;
    for (size_t i = start; i < term->output_length; ++i) {
        unsigned char c = (unsigned char)term->output[i];
        if (c >= 32 && c < 127) { (void)fputc(c, stderr); }
        else { (void)fprintf(stderr, "\\x%02x", (unsigned)c); }
    }
    (void)fputc('\n', stderr);
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
