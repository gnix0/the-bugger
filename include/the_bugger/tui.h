#ifndef THE_BUGGER_TUI_H
#define THE_BUGGER_TUI_H

/* Owns terminal initialization and restoration; returns a process exit status. */
#include <stdbool.h>
int tb_tui_run(bool demo);

#endif
