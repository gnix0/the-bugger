#ifndef THE_BUGGER_TERMINAL_H
#define THE_BUGGER_TERMINAL_H
/* Matching RGB terminfo name when advertised, or NULL for the original TERM.
 * Call before creating the curses screen. Never alters the environment. */
const char *tb_terminal_description(void);
#endif
