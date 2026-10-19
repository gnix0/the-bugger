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
    TbMemoryRegion region = {0};
    CHECK(tb_native_memory_region("1000-2000 rw-p 00000000 00:00 0\n", &region));
    CHECK(region.begin == 0x1000 && region.end == 0x2000);
    CHECK(tb_native_memory_region("1000-2000 rw-p 00000000 00:00 0    [heap]\n", &region));
    const char *invalid[] = {
        "1000-2000 rw-p 0 00:00 0", /* Truncated. */
        "1000-2000 rw-p 0 00:00 0 [vvar]\n",
        "1000-2000 rw-p 0 00:00 0 /dev/mem\n",
        "-1000-2000 rw-p 0 00:00 0\n",
        "10000000000000000-2000 rw-p 0 00:00 0\n",
        "2000-1000 rw-p 0 00:00 0\n",
        "1000-1000 rw-p 0 00:00 0\n",
        "1000-2000 -w-p 0 00:00 0\n",
        "1000-2000 rw-z 0 00:00 0\n",
        "1000-2000 rw-p 0 00:00 -1\n",
        "1000-2000 rw-p 0 00:00 18446744073709551616\n",
        "1000-2000 rw-p 0 00:00 0\ntrailing",
        "1000-2000 rw-p 0 00:00 0 [unknown]\n"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        CHECK(!tb_native_memory_region(invalid[i], &region));
    }
    FILE *maps = fopen("/proc/self/maps", "r"); CHECK(maps);
    char mapping[8192]; bool stack_found = false;
    uint64_t stack_address = (uint64_t)(uintptr_t)&region;
    while (fgets(mapping, sizeof mapping, maps)) {
        if (tb_native_memory_region(mapping, &region) && region.begin <= stack_address && stack_address < region.end) { stack_found = true; }
    }
    CHECK(fclose(maps) == 0 && stack_found);
    puts("PASS native actual/maps permission/overflow/truncation/device classification");
    puts("PASS native structured words: quotes, empty tokens, literal metacharacters and bounds");
    return 0;
}
