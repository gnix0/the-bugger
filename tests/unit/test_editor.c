#include "editor.h"
#include "../support/check.h"
#include <string.h>

static void terminal_editing(void)
{
    wchar_t text[64] = L"one_two:: three.four";
    size_t  cursor   = 999;

    tb_editor_edit(text, &cursor, TB_EDIT_WORD_BACK);
    CHECK(cursor == 16);
    tb_editor_edit(text, &cursor, TB_EDIT_WORD_BACK);
    CHECK(cursor == 10);
    tb_editor_edit(text, &cursor, TB_EDIT_WORD_BACK);
    CHECK(cursor == 0);
    tb_editor_edit(text, &cursor, TB_EDIT_WORD_BACK);
    CHECK(cursor == 0);
    tb_editor_edit(text, &cursor, TB_EDIT_WORD_FORWARD);
    CHECK(cursor == 7);
    tb_editor_edit(text, &cursor, TB_EDIT_WORD_FORWARD);
    CHECK(cursor == 15);
    tb_editor_edit(text, &cursor, TB_EDIT_WORD_FORWARD);
    CHECK(cursor == wcslen(text));
    tb_editor_edit(text, &cursor, TB_EDIT_WORD_FORWARD);
    CHECK(cursor == wcslen(text));
    tb_editor_edit(text, &cursor, TB_EDIT_BEGIN);
    tb_editor_edit(text, &cursor, TB_EDIT_LEFT);
    CHECK(cursor == 0);
    tb_editor_edit(text, &cursor, TB_EDIT_RIGHT);
    CHECK(cursor == 1);
    tb_editor_edit(text, &cursor, TB_EDIT_BACKSPACE);
    CHECK(cursor == 0 && wcscmp(text, L"ne_two:: three.four") == 0);
    tb_editor_edit(text, &cursor, TB_EDIT_DELETE);
    CHECK(cursor == 0 && wcscmp(text, L"e_two:: three.four") == 0);
    tb_editor_edit(text, &cursor, TB_EDIT_END);
    tb_editor_edit(text, &cursor, TB_EDIT_RIGHT);
    tb_editor_edit(text, &cursor, TB_EDIT_DELETE_END);
    CHECK(wcscmp(text, L"e_two:: three.four") == 0);
    tb_editor_edit(text, &cursor, TB_EDIT_WORD_BACK);
    tb_editor_edit(text, &cursor, TB_EDIT_DELETE_END);
    CHECK(wcscmp(text, L"e_two:: three.") == 0 && cursor == wcslen(text));
    CHECK(!tb_editor_normal(text, &cursor, 'D'));
    CHECK(wcscmp(text, L"e_two:: three") == 0 && cursor == 12);
    tb_editor_edit(text, &cursor, TB_EDIT_CLEAR);
    CHECK(!*text && cursor == 0);
    tb_editor_edit(text, &cursor, TB_EDIT_DELETE_END);
    CHECK(!tb_editor_normal(text, &cursor, 'D') && !*text && cursor == 0);

    /* Editing indices are wide characters, never byte offsets. */
    wcscpy(text, L"\u00e9\u754c!");
    cursor = 2;
    tb_editor_edit(text, &cursor, TB_EDIT_BACKSPACE);
    CHECK(wcscmp(text, L"\u00e9!") == 0 && cursor == 1);
    CHECK(!tb_editor_normal(text, &cursor, 'D'));
    CHECK(wcscmp(text, L"\u00e9") == 0 && cursor == 0);
}


int main(void)
{
    terminal_editing();
    wchar_t text[64] = L"one two:: three"; size_t cursor = 0;
    CHECK(!tb_editor_normal(text, &cursor, 'w') && cursor == 4);
    CHECK(!tb_editor_normal(text, &cursor, 'e') && cursor == 6);
    CHECK(!tb_editor_normal(text, &cursor, 'w') && cursor == 7);
    CHECK(!tb_editor_normal(text, &cursor, 'w') && cursor == 10);
    CHECK(!tb_editor_normal(text, &cursor, 'b') && cursor == 7);
    CHECK(!tb_editor_normal(text, &cursor, 'b') && cursor == 4);
    CHECK(!tb_editor_normal(text, &cursor, '0') && cursor == 0);
    CHECK(!tb_editor_normal(text, &cursor, 'h') && cursor == 0);
    CHECK(!tb_editor_normal(text, &cursor, '$') && cursor == 14);
    CHECK(!tb_editor_normal(text, &cursor, 'l') && cursor == 14);
    CHECK(tb_editor_normal(text, &cursor, 'a') && cursor == 15);
    CHECK(tb_editor_normal(text, &cursor, 'I') && cursor == 0);
    CHECK(tb_editor_normal(text, &cursor, 'A') && cursor == 15);
    CHECK(!tb_editor_normal(text, &cursor, 'x') && wcscmp(text, L"one two:: thre") == 0 && cursor == 13);
    CHECK(!tb_editor_normal(text, &cursor, 'q') && cursor == 13);
    text[0] = 0; cursor = 999;
    CHECK(!tb_editor_normal(text, &cursor, 'x') && cursor == 0 && !*text);
    CHECK(tb_editor_normal(text, &cursor, 'i') && cursor == 0);
    CHECK(tb_editor_normal(text, &cursor, 'a') && cursor == 0);
    wcscpy(text, L"x"); CHECK(!tb_editor_normal(text, &cursor, 'x') && !*text && cursor == 0);
    puts("PASS single-line Normal editing: words, punctuation, edges, insertion, deletion and empty input");
    return EXIT_SUCCESS;
}
