#include "editor.h"
#include <wctype.h>

static int word_kind(wchar_t c)
{
    return iswspace((wint_t)c) ? 0 : iswalnum((wint_t)c) || c == L'_' ? 1 : 2;
}

bool tb_editor_normal(wchar_t *text, size_t *cursor, wint_t key)
{
    size_t length = wcslen(text), at = *cursor;
    if (at >= length) { at = length ? length - 1 : 0; }
    switch (key) {
    case 'i': *cursor = at; return true;
    case 'a': *cursor = length ? at + 1 : 0; return true;
    case 'I': *cursor = 0; return true;
    case 'A': *cursor = length; return true;
    case 'h': if (at) { --at; } break;
    case 'l': if (at + 1 < length) { ++at; } break;
    case '0': at = 0; break;
    case '$': at = length ? length - 1 : 0; break;
    case 'x':
        if (at < length) { wmemmove(text + at, text + at + 1, length - at); --length; }
        break;
    case 'w':
        if (at < length) {
            int kind = word_kind(text[at]);
            while (at < length && word_kind(text[at]) == kind) { ++at; }
            while (at < length && word_kind(text[at]) == 0) { ++at; }
        }
        break;
    case 'b':
        if (at) {
            --at;
            while (at && word_kind(text[at]) == 0) { --at; }
            int kind = word_kind(text[at]);
            while (at && word_kind(text[at - 1]) == kind) { --at; }
        }
        break;
    case 'e':
        if (at + 1 < length) {
            ++at;
            while (at + 1 < length && word_kind(text[at]) == 0) { ++at; }
            int kind = word_kind(text[at]);
            while (at + 1 < length && word_kind(text[at + 1]) == kind) { ++at; }
        }
        break;
    default: break;
    }
    *cursor = at < length ? at : length ? length - 1 : 0;
    return false;
}
