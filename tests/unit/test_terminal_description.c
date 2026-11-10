#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "terminal.h"
#include "../support/check.h"
#include <string.h>
#include <term.h>
#include <stdint.h>

static uint64_t capability_hash(void)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int kind = 0; kind < 3; ++kind) {
        const char *const *names = kind == 0 ? boolnames : kind == 1 ? numnames : strnames;
        size_t count = kind == 0 ? BOOLCOUNT : kind == 1 ? NUMCOUNT : STRCOUNT;
        size_t extra = 0;
        size_t offset = 0;
#if NCURSES_XNAMES
        extra = kind == 0 ? cur_term->type.ext_Booleans
            : kind == 1 ? cur_term->type.ext_Numbers : cur_term->type.ext_Strings;
        offset = kind == 0 ? 0 : kind == 1 ? cur_term->type.ext_Booleans
            : cur_term->type.ext_Booleans + cur_term->type.ext_Numbers;
#endif // NCURSES_XNAMES
        for (size_t i = 0; i < count + extra; ++i) {
            const char *name = names[i < count ? i : 0];
#if NCURSES_XNAMES
            if (i >= count)
                name = cur_term->type.ext_Names[offset + i - count];
#else
            (void)offset;
#endif // NCURSES_XNAMES
            if (strcmp(name, "setaf") == 0 || strcmp(name, "setab") == 0)
                continue;
            char number[32];
            const char *value;
            if (kind < 2) {
                (void)snprintf(number, sizeof(number), "%d",
                               kind == 0 ? tigetflag(name) : tigetnum(name));
                value = number;
            } else {
                value = tigetstr(name);
                if (!value || value == (char *)-1)
                    value = "<absent>";
            }
            for (const unsigned char *at = (const unsigned char *)value; *at; ++at)
                hash = (hash ^ *at) * UINT64_C(1099511628211);
            hash = (hash ^ 0xffU) * UINT64_C(1099511628211);
        }
    }
    return hash;
}


static void direct_colors(void)
{
    const char *descriptions[] = {"alacritty-direct", "tmux-direct", "xterm-direct", "xterm-256color"};
    CHECK(unsetenv("NO_COLOR") == 0);
    CHECK(setenv("TERMINFO", "/nonexistent/the-bugger-terminfo-test", 1) == 0);
    for (size_t i = 0; i < sizeof(descriptions) / sizeof(*descriptions); ++i) {
        const char *expected = i == 1 ? NULL : i == 2 ? "" : "/nonexistent/the-bugger-terminfo-test";
        if (expected)
            CHECK(setenv("TERMINFO", expected, 1) == 0);
        else
            CHECK(unsetenv("TERMINFO") == 0);
        FILE *input  = tmpfile();
        FILE *output = tmpfile();
        int   error  = 0;
        CHECK(input && output && setupterm(descriptions[i], fileno(output), &error) == OK);
        uint64_t before = capability_hash();
        char *key = tigetstr("kLFT5");
        char *extended_key = key && key != (char *)-1 ? strdup(key) : NULL;
        (void)del_curterm(cur_term);

        SCREEN *screen = tb_terminal_screen(descriptions[i], output, input);
        CHECK(screen);
        CHECK(expected ? getenv("TERMINFO") && strcmp(getenv("TERMINFO"), expected) == 0
                       : !getenv("TERMINFO"));
        CHECK(capability_hash() == before);
        if (extended_key) {
            key = tigetstr("kLFT5");
            CHECK(key && key != (char *)-1 && strcmp(key, extended_key) == 0);
        }
        free(extended_key);
        if (i < 3) {
            CHECK(tigetnum("colors") == 16777216 && tigetflag("RGB") == 1);
            CHECK(strcmp(tparm(tigetstr("setab"), 0L), "\033[48;2;0;0;0m") == 0);
            CHECK(strcmp(tparm(tigetstr("setaf"), 0L), "\033[38;2;0;0;0m") == 0);
            CHECK(strcmp(tparm(tigetstr("setab"), 7L), "\033[48;2;0;0;7m") == 0);
            CHECK(strcmp(tparm(tigetstr("setab"), 0x123456L), "\033[48;2;18;52;86m") == 0);
        } else {
            CHECK(tigetnum("colors") == 256);
            CHECK(strcmp(tparm(tigetstr("setab"), 0L), "\033[40m") == 0);
        }
        (void)endwin();
        delscreen(screen);
        CHECK(fclose(input) == 0 && fclose(output) == 0);
    }
    CHECK(unsetenv("TERMINFO") == 0);
    puts("PASS exact direct RGB black, unchanged installed capabilities, extended keys "
         "and TERMINFO restoration");
}


int main(void)
{
    direct_colors();
    CHECK(unsetenv("NO_COLOR") == 0 && unsetenv("COLORTERM") == 0 && unsetenv("TMUX") == 0);
    CHECK(setenv("TERM", "tmux-256color", 1) == 0);
    CHECK(tb_terminal_description() == NULL);
    CHECK(setenv("TMUX", "/synthetic/capability-fixture,1,0", 1) == 0);
    CHECK(tb_terminal_description() == NULL);
    CHECK(setenv("COLORTERM", "truecolor", 1) == 0);
    CHECK(strcmp(tb_terminal_description(), "tmux-direct") == 0);
    CHECK(strcmp(getenv("TERM"), "tmux-256color") == 0); /* No environment rewrite. */
    CHECK(setenv("COLORTERM", "24bit", 1) == 0);
    CHECK(strcmp(tb_terminal_description(), "tmux-direct") == 0);
    CHECK(setenv("TERM", "screen-256color", 1) == 0);
    CHECK(strcmp(tb_terminal_description(), "tmux-direct") == 0);
    CHECK(setenv("TERM", "vt100", 1) == 0 && tb_terminal_description() == NULL);
    CHECK(setenv("TERM", "alacritty", 1) == 0 && tb_terminal_description() == NULL);
    CHECK(unsetenv("TMUX") == 0 && strcmp(tb_terminal_description(), "alacritty-direct") == 0);
    CHECK(setenv("NO_COLOR", "1", 1) == 0 && tb_terminal_description() == NULL);
    CHECK(unsetenv("NO_COLOR") == 0 && unsetenv("COLORTERM") == 0);
    CHECK(setenv("TERM", "does-not-exist-the-bugger", 1) == 0 && tb_terminal_description() == NULL);
    puts("PASS matching terminal capability selection, RGB environment signals, NO_COLOR and unchanged TERM");
    return EXIT_SUCCESS;
}
