#ifndef THE_BUGGER_TERMINAL_H
#define THE_BUGGER_TERMINAL_H

#include <curses.h>

/* Matching RGB terminfo name when advertised, or NULL for the original TERM.
 * Call before creating the curses screen. Never alters the environment. */
const char *tb_terminal_description(void);

/* Create the initial screen with a private description when ANSI slots alias RGB 0..7.
 * Retains the installed input capabilities and restores TERMINFO before return. */
SCREEN *tb_terminal_screen(const char *description, FILE *output, FILE *input);

#endif // THE_BUGGER_TERMINAL_H
