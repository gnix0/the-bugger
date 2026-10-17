#include "native.h"
#include "../support/check.h"
#include <string.h>

int main(void)
{
    TbNativeWords words; char error[128];
    CHECK(tb_native_words("one 'two three' \"\" path\\ with\\ spaces '$HOME;$(false)'", &words, error, sizeof error));
    CHECK(words.count == 5 && strcmp(words.values[0], "one") == 0);
    CHECK(strcmp(words.values[1], "two three") == 0 && words.values[2][0] == '\0');
    CHECK(strcmp(words.values[3], "path with spaces") == 0);
    CHECK(strcmp(words.values[4], "$HOME;$(false)") == 0);
    CHECK(!tb_native_words("'unclosed", &words, error, sizeof error) && strstr(error, "quote"));
    CHECK(!tb_native_words("trailing\\", &words, error, sizeof error));
    char large[TB_NATIVE_WORD_BYTES + 1]; memset(large, 'x', sizeof large - 1); large[sizeof large - 1] = '\0';
    CHECK(!tb_native_words(large, &words, error, sizeof error));
    char many[128] = {0};
    for (int i = 0; i < TB_NATIVE_WORDS + 1; ++i) { strcat(many, "x "); }
    CHECK(!tb_native_words(many, &words, error, sizeof error));
    CHECK(tb_native_words(" \t ", &words, error, sizeof error) && words.count == 0);
    puts("PASS native structured words: quotes, empty tokens, literal metacharacters and bounds");
    return 0;
}
