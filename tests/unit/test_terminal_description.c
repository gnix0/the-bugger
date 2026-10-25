#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "terminal.h"
#include "../support/check.h"
#include <string.h>

int main(void)
{
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
