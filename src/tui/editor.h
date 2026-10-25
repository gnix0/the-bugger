#ifndef THE_BUGGER_EDITOR_H
#define THE_BUGGER_EDITOR_H
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>
/* Bounded single-line Normal commands. True requests Insert mode. */
bool tb_editor_normal(wchar_t *text, size_t *cursor, wint_t key);
#endif
