#include "tui/editor.h"
#include <wctype.h>

static int word_kind(wchar_t c)
{
    return iswspace((wint_t)c) ? 0 : iswalnum((wint_t)c) || c == L'_' ? 1 : 2;
}


void tb_editor_edit(wchar_t *text, size_t *cursor, TbEditAction_e action)
{
    size_t length = wcslen(text);
    size_t at     = *cursor > length ? length : *cursor;

    switch (action) {
    case TB_EDIT_BEGIN:
        at = 0;
        break;
    case TB_EDIT_END:
        at = length;
        break;
    case TB_EDIT_LEFT:
        if (at)
            --at;
        break;
    case TB_EDIT_RIGHT:
        if (at < length)
            ++at;
        break;
    case TB_EDIT_WORD_BACK:
        while (at && word_kind(text[at - 1]) != 1)
            --at;
        while (at && word_kind(text[at - 1]) == 1)
            --at;
        break;
    case TB_EDIT_WORD_FORWARD:
        while (at < length && word_kind(text[at]) != 1)
            ++at;
        while (at < length && word_kind(text[at]) == 1)
            ++at;
        break;
    case TB_EDIT_BACKSPACE:
        if (at) {
            --at;
            wmemmove(text + at, text + at + 1, length - at);
        }
        break;
    case TB_EDIT_DELETE:
        if (at < length)
            wmemmove(text + at, text + at + 1, length - at);
        break;
    case TB_EDIT_CLEAR:
        at = 0;
        text[0] = L'\0';
        break;
    case TB_EDIT_DELETE_END:
        text[at] = L'\0';
        break;
    }

    *cursor = at;
}


bool tb_editor_normal(wchar_t *text, size_t *cursor, wint_t key)
{
    size_t length = wcslen(text);
    size_t at     = *cursor;

    if (at >= length)
        at = length ? length - 1 : 0;

    switch (key) {
    case 'i':
        *cursor = at;
        return true;
    case 'a':
        *cursor = length ? at + 1 : 0;
        return true;
    case 'I':
        *cursor = 0;
        return true;
    case 'A':
        *cursor = length;
        return true;
    case 'h':
        if (at)
            --at;
        break;
    case 'l':
        if (at + 1 < length)
            ++at;
        break;
    case '0':
        at = 0;
        break;
    case '$':
        at = length ? length - 1 : 0;
        break;
    case 'x':
        if (at < length) {
            wmemmove(text + at, text + at + 1, length - at);
            --length;
        }
        break;
    case 'D':
        text[at] = L'\0';
        length = at;
        break;
    case 'w':
        if (at < length) {
            int kind = word_kind(text[at]);
            while (at < length && word_kind(text[at]) == kind)
                ++at;
            while (at < length && word_kind(text[at]) == 0)
                ++at;
        }
        break;
    case 'b':
        if (at) {
            --at;
            while (at && word_kind(text[at]) == 0)
                --at;

            int kind = word_kind(text[at]);
            while (at && word_kind(text[at - 1]) == kind)
                --at;
        }
        break;
    case 'e':
        if (at + 1 < length) {
            ++at;
            while (at + 1 < length && word_kind(text[at]) == 0)
                ++at;

            int kind = word_kind(text[at]);
            while (at + 1 < length && word_kind(text[at + 1]) == kind)
                ++at;
        }
        break;
    default:
        break;
    }

    *cursor = at < length ? at : length ? length - 1 : 0;

    return false;
}
