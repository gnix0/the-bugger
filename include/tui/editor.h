#ifndef THE_BUGGER_EDITOR_H
#define THE_BUGGER_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef enum {
    TB_EDIT_BEGIN,
    TB_EDIT_END,
    TB_EDIT_LEFT,
    TB_EDIT_RIGHT,
    TB_EDIT_WORD_BACK,
    TB_EDIT_WORD_FORWARD,
    TB_EDIT_BACKSPACE,
    TB_EDIT_DELETE,
    TB_EDIT_CLEAR,
    TB_EDIT_DELETE_END
} TbEditAction_e;

/* Character-indexed cursor; text remains a terminated single-line buffer. */
void tb_editor_edit(wchar_t *text, size_t *cursor, TbEditAction_e action);

/* Bounded single-line Normal commands. True requests Insert mode. */
bool tb_editor_normal(wchar_t *text, size_t *cursor, wint_t key);

#endif // THE_BUGGER_EDITOR_H
