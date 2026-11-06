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

static void paint_background(TestTerminal *term, int y, int x, int count)
{
    for (int i = 0; i < count; ++i) {
        term->backgrounds[y][x + i] = term->background;
        term->foregrounds[y][x + i] = term->foreground;
    }
}

static void clear_screen(TestTerminal *term)
{
    for (int y = 0; y < TEST_ROWS; ++y) {
        memset(term->cells[y], ' ', TEST_COLS);
        paint_background(term, y, 0, TEST_COLS);
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
        memmove(term->backgrounds[top], term->backgrounds[top + count],
                (size_t)(bottom - top + 1 - count) * sizeof term->backgrounds[0]);
        memmove(term->foregrounds[top], term->foregrounds[top + count],
                (size_t)(bottom - top + 1 - count) * sizeof term->foregrounds[0]);
        memmove(term->underline[top], term->underline[top + count],
                (size_t)(bottom - top + 1 - count) * sizeof term->underline[0]);
        for (int y = bottom - count + 1; y <= bottom; ++y) {
            memset(term->cells[y], ' ', (size_t)term->cols);
            paint_background(term, y, 0, term->cols);
            memset(term->inverse[y], term->reverse, (size_t)term->cols * sizeof(bool));
            memset(term->underline[y], term->underlined, (size_t)term->cols * sizeof(bool));
        }
    } else {
        if (size > 0) { memmove(term->cells[top + count], term->cells[top], size); }
        memmove(term->inverse[top + count], term->inverse[top],
                (size_t)(bottom - top + 1 - count) * sizeof term->inverse[0]);
        memmove(term->backgrounds[top + count], term->backgrounds[top],
                (size_t)(bottom - top + 1 - count) * sizeof term->backgrounds[0]);
        memmove(term->foregrounds[top + count], term->foregrounds[top],
                (size_t)(bottom - top + 1 - count) * sizeof term->foregrounds[0]);
        memmove(term->underline[top + count], term->underline[top],
                (size_t)(bottom - top + 1 - count) * sizeof term->underline[0]);
        for (int y = top; y < top + count; ++y) {
            memset(term->cells[y], ' ', (size_t)term->cols);
            paint_background(term, y, 0, term->cols);
            memset(term->inverse[y], term->reverse, (size_t)term->cols * sizeof(bool));
            memset(term->underline[y], term->underlined, (size_t)term->cols * sizeof(bool));
        }
    }
}

static void sgr(TestTerminal *term)
{
    int values[64], count = 0;
    const char *next = term->sequence;
    if (!*next) { term->reverse = false; term->underlined = false; term->background = term->foreground = -1; }
    while (*next && count < 64) {
        char *end;
        long value = strtol(next, &end, 10);
        values[count++] = end == next ? -1 : (int)value;
        next = (*end == ';' || *end == ':') ? end + 1 : end;
        if (end == next) { break; }
    }
    for (int i = 0; i < count; ++i) {
        int code = values[i];
        if (code == 0 || code == 24) { term->underlined = false; }
        else if (code == 4) { term->underlined = true; }
        if (code == 0 || code == 27) { term->reverse = false; }
        else if (code == 7) { term->reverse = true; }
        if (code == 0 || code == 39) { term->foreground = -1; }
        else if (code >= 30 && code <= 37) { term->foreground = 0x1000000 | (code - 30); }
        else if (code >= 90 && code <= 97) { term->foreground = 0x1000000 | (code - 90 + 8); }
        if (code == 0 || code == 49) { term->background = -1; }
        else if (code >= 40 && code <= 47) { term->background = 0x1000000 | (code - 40); }
        else if (code >= 100 && code <= 107) { term->background = 0x1000000 | (code - 100 + 8); }
        else if ((code == 38 || code == 48 || code == 58) && i + 1 < count) {
            int mode = values[++i];
            if (mode == 5 && i + 1 < count) {
                int color = values[++i];
                if (code == 48) { term->background = 0x1000000 | color; }
                if (code == 38) { term->foreground = 0x1000000 | color; }
            } else if (mode == 2) {
                if (i + 1 < count && values[i + 1] < 0) { ++i; }
                if (i + 3 >= count) { return; }
                int red = values[i + 1], green = values[i + 2], blue = values[i + 3];
                if (code == 48) { term->background = (red << 16) | (green << 8) | blue; }
                if (code == 38) { term->foreground = (red << 16) | (green << 8) | blue; }
                i += 3;
            }
        }
    }
}

static void csi(TestTerminal *term, char command)
{
    if (strcmp(term->sequence, "?2026") == 0) {
        if (command == 'h') { term->synchronized_update = true; }
        if (command == 'l') { term->synchronized_update = false; ++term->synchronized_frames; }
    }
    if (command == 'l' && strcmp(term->sequence, "?1049") == 0) { ++term->alternate_exits; }
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
        if (first == 2 || first == 3) { ++term->full_clears; clear_screen(term); }
        else {
            memset(term->cells[term->y] + term->x, ' ', (size_t)(term->cols - term->x));
            paint_background(term, term->y, term->x, term->cols - term->x);
            memset(term->inverse[term->y] + term->x, term->reverse,
                    (size_t)(term->cols - term->x) * sizeof(bool));
            memset(term->underline[term->y] + term->x, term->underlined,
                    (size_t)(term->cols - term->x) * sizeof(bool));
            for (int y = term->y + 1; y < term->rows; ++y) {
                memset(term->cells[y], ' ', (size_t)term->cols);
                paint_background(term, y, 0, term->cols);
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
            paint_background(term, term->y, start, count);
            memset(term->inverse[term->y] + start, term->reverse, (size_t)count * sizeof(bool));
            memset(term->underline[term->y] + start, term->underlined, (size_t)count * sizeof(bool));
        }
        break;
    case 'P': case '@': {
        int count = distance < term->cols - term->x ? distance : term->cols - term->x;
        char *line = term->cells[term->y] + term->x;
        bool *inverse = term->inverse[term->y] + term->x;
        bool *underline = term->underline[term->y] + term->x;
        int *background = term->backgrounds[term->y] + term->x;
        int *foreground = term->foregrounds[term->y] + term->x;
        size_t tail = (size_t)(term->cols - term->x - count);
        if (command == 'P') {
            memmove(line, line + count, tail);
            memmove(background, background + count, tail * sizeof(int));
            memmove(foreground, foreground + count, tail * sizeof(int));
            paint_background(term, term->y, term->x + (int)tail, count);
            memset(line + tail, ' ', (size_t)count);
            memmove(inverse, inverse + count, tail * sizeof(bool));
            memmove(underline, underline + count, tail * sizeof(bool));
            memset(inverse + tail, term->reverse, (size_t)count * sizeof(bool));
            memset(underline + tail, term->underlined, (size_t)count * sizeof(bool));
        } else {
            memmove(line + count, line, tail);
            memmove(background + count, background, tail * sizeof(int));
            memmove(foreground + count, foreground, tail * sizeof(int));
            paint_background(term, term->y, term->x, count);
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
        paint_background(term, term->y, term->x, count);
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
            if (value == 0x2584U) { character = '_'; }
            else if (value == 0x25cfU) { character = '*'; }
            else {
                if (value < 0x2500U || value > 0x257fU) { return; }
                character = value == 0x2500U ? '-' : value == 0x2502U ? '|' : '+';
            }
        } else { term->utf8_remaining = 0; }
    } else if (character >= 0xc2U && character <= 0xf4U) {
        term->utf8_remaining = character < 0xe0U ? 1U : character < 0xf0U ? 2U : 3U;
        term->utf8_value = character & (term->utf8_remaining == 1 ? 0x1fU : term->utf8_remaining == 2 ? 0x0fU : 0x07U);
        return;
    }
    if (character < 32 || character >= 127) { return; }
    if (!term->synchronized_update) { ++term->unsynchronized_writes; }
    if (term->x >= term->cols) {
        term->x = 0;
        if (term->y < term->rows - 1) { ++term->y; }
    }
    term->foregrounds[term->y][term->x] = term->foreground;
    term->backgrounds[term->y][term->x] = term->background;
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
    term->background = term->foreground = -1;
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

bool test_terminal_scroll_cell(const TestTerminal *term, int y, int x, bool thumb)
{
    if (x < 0 || y < 0 || x >= term->cols || y >= term->rows) { return false; }
    int color = term->cells[y][x] == '_' ? term->foregrounds[y][x] : term->backgrounds[y][x];
    /* Exact reference palette and its nearest xterm/8-color fallbacks. */
    return (term->cells[y][x] == ' ' || term->cells[y][x] == '_') &&
           (thumb ? color == 0xc6a0f6 || color == (0x1000000 | 183) || color == (0x1000000 | 5)
                  : color == 0x363a4f || color == (0x1000000 | 238) || color == (0x1000000 | 7));
}

bool test_terminal_find_scrollbar(const TestTerminal *term, bool thumb, int *x, int *y)
{
    for (int row = 0; row < term->rows; ++row) {
        for (int col = 0; col < term->cols; ++col) {
            if (test_terminal_scroll_cell(term, row, col, thumb)) {
                if (x) { *x = col; }
                if (y) { *y = row; }
                return true;
            }
        }
    }
    return false;
}

void test_terminal_expect_scrollbar(TestTerminal *term, bool thumb)
{
    long long deadline = milliseconds() + 5000;
    while (milliseconds() < deadline) {
        if (test_terminal_find_scrollbar(term, thumb, NULL, NULL)) {
            long long settle = milliseconds() + 40;
            while (milliseconds() < settle) { test_terminal_pump(term, 5); }
            if (test_terminal_find_scrollbar(term, thumb, NULL, NULL)) { return; }
        }
        test_terminal_pump(term, 20);
    }
    fprintf(stderr, "Expected scrollbar %s fill\n", thumb ? "thumb" : "track");
    CHECK(false);
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

bool test_terminal_dialog_find(const TestTerminal *term, const char *title, const char *label, int *x, int *y)
{
    int title_x, title_y;
    if (!test_terminal_find(term, title, &title_x, &title_y)) { return false; }
    int left = title_x - 2, top = title_y - 1, right = left + 1;
    if (left < 0 || top < 0) { return false; }
    while (right < term->cols && (term->cells[top][right] == 'q' || term->cells[top][right] == '-')) { ++right; }
    if (right >= term->cols || right <= left + 4) { return false; }
    for (int row = title_y + 1; row < term->rows - 2; ++row) {
        if (term->cells[row][left] != 'x' && term->cells[row][left] != '|') { break; }
        const char *at = strstr(term->cells[row] + left + 1, label);
        if (at && at + strlen(label) <= term->cells[row] + right) {
            if (x) { *x = (int)(at - term->cells[row]); }
            if (y) { *y = row; }
            return true;
        }
    }
    return false;
}

void test_terminal_dialog_click(TestTerminal *term, const char *title, const char *label)
{
    test_terminal_expect(term, title);
    int x, y;
    CHECK(test_terminal_dialog_find(term, title, label, &x, &y));
    char input[80];
    (void)snprintf(input, sizeof input, "\033[<0;%d;%dM\033[<0;%d;%dm", x + 1, y + 1, x + 1, y + 1);
    test_terminal_send(term, input);
    long long until = milliseconds() + 120;
    while (milliseconds() < until) { test_terminal_pump(term, 5); }
}

void test_terminal_menu_click(TestTerminal *term, const char *title, const char *label)
{
    test_terminal_expect(term, title);
    for (int attempt = 0; attempt < 48; ++attempt) {
        int x, y;
        CHECK(test_terminal_find(term, title, &x, &y));
        int left = x - 2;
        for (int row = y + 1; row < term->rows - 2; ++row) {
            char edge = term->cells[row][left];
            if (edge != 'x' && edge != '|') { break; }
            const char *at = strstr(term->cells[row] + left + 1, label);
            if (at) {
                int col = (int)(at - term->cells[row]);
                char input[80];
                (void)snprintf(input, sizeof input, "\033[<0;%d;%dM\033[<0;%d;%dm",
                               col + 1, row + 1, col + 1, row + 1);
                test_terminal_send(term, input);
                long long settle = milliseconds() + 120;
                while (milliseconds() < settle) { test_terminal_pump(term, 5); }
                return;
            }
        }
        char wheel[64];
        (void)snprintf(wheel, sizeof wheel, "\033[<65;%d;%dM", left + 3, y + 2);
        test_terminal_send(term, wheel);
        long long settle = milliseconds() + 60;
        while (milliseconds() < settle) { test_terminal_pump(term, 5); }
    }
    fprintf(stderr, "Missing popup item %s in %s\n", label, title);
    CHECK(false);
}

void test_terminal_lower(TestTerminal *term, int lower, bool open)
{
    CHECK(lower >= 0 && lower < 3);
    long long ready = milliseconds() + 100;
    while (milliseconds() < ready) { test_terminal_pump(term, 5); }
    int dock = term->rows - 4;
    if (term->lower_column_width != term->cols) {
        int available = term->cols - 4;
        int first = available * 36 / 100, second = available * 34 / 100;
        term->lower_columns[0] = first - 6;
        term->lower_columns[1] = first + second - 5;
        term->lower_columns[2] = term->cols - 8;
        term->lower_column_width = term->cols;
    }
    /* Capture actual divider positions whenever all three docks are visible. */
    int columns[3], count = 0;
    for (int y = 3; y < dock; ++y) {
        const char *at = term->cells[y]; count = 0;
        while ((at = strstr(at, "...")) != NULL) {
            if (count < 3) { columns[count] = (int)(at - term->cells[y]) - 2; }
            ++count; at += 3;
        }
        if (count == 3) { memcpy(term->lower_columns, columns, sizeof columns); break; }
    }
    count = 0;
    for (int x = 0; x + 4 < term->cols; ++x) {
        if (term->cells[dock][x] == '^' && strncmp(term->cells[dock] + x + 2, "...", 3) == 0 && count < 3) { columns[count++] = x; }
    }
    if (count == 3) { memcpy(term->lower_columns, columns, sizeof columns); }
    int column = term->lower_columns[lower], row = dock;
    bool closed = term->cells[dock][column] == '^';
    if (closed == !open) { return; }
    if (!open) {
        row = -1;
        for (int y = 3; y < dock; ++y) {
            if (term->cells[y][column] == '_' && strncmp(term->cells[y] + column + 2, "...", 3) == 0) { row = y; break; }
        }
        if (row < 0) {
            const char *const selectors[] = {"4 Mem", "5 Locals", "6 Log"};
            if (test_terminal_find(term, selectors[lower], NULL, NULL)) { test_terminal_click(term, selectors[lower]); }
            column = term->cols - 8;
            for (int y = 3; y < dock; ++y) {
                if (term->cells[y][column] == '_' && strncmp(term->cells[y] + column + 2, "...", 3) == 0) { row = y; break; }
            }
        }
    }
    if (row < 0) {
        fprintf(stderr, "Cannot locate expanded lower %d at %dx%d, column %d\n", lower, term->cols, term->rows, column);
        for (int y = 0; y < term->rows; ++y) { fprintf(stderr, "%s\n", term->cells[y]); }
    }
    CHECK(row >= 0);
    char expanded_title[TEST_COLS + 1] = {0};
    if (!open && column > 4) {
        size_t length = (size_t)(column - 4);
        memcpy(expanded_title, term->cells[row] + 4, length);
        while (length && expanded_title[length - 1] == ' ') { --length; }
        expanded_title[length] = 0;
    }
    char input[64];
    (void)snprintf(input, sizeof input, "\033[<0;%d;%dM", column + 1, row + 1);
    test_terminal_send(term, input);
    long long settle = milliseconds() + 80;
    while (milliseconds() < settle) { test_terminal_pump(term, 5); }
    (void)snprintf(input, sizeof input, "\033[<0;%d;%dm", column + 1, row + 1);
    test_terminal_send(term, input);
    long long deadline = milliseconds() + 5000;
    column = term->lower_columns[lower];
    while (milliseconds() < deadline) {
        test_terminal_pump(term, 20);
        /* Another full-width overlay can hide the newly minimized dock.
         * Its expanded title must disappear even when '^' is not visible. */
        bool covered_closed = !open && *expanded_title &&
            test_terminal_find(term, "1 Source", NULL, NULL) &&
            !test_terminal_find(term, expanded_title, NULL, NULL);
        if ((term->cells[dock][column] == '^') == !open || covered_closed) {
            settle = milliseconds() + 80;
            while (milliseconds() < settle) { test_terminal_pump(term, 5); }
            return;
        }
    }
    fprintf(stderr, "Lower group %d wanted %s\n", lower, open ? "open" : "minimized");
    test_terminal_expect(term, "lower pane toggle did not complete");
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
        for (int row = 0; row < term->rows; ++row) { fprintf(stderr, "%s\n", term->cells[row]); }
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
