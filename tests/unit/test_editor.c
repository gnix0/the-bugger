#include "editor.h"
#include "../support/check.h"
#include <string.h>
int main(void)
{
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
