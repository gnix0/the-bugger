#include "browse.h"
#include "../support/check.h"
#include <string.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void __real_free(void *pointer);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void __wrap_free(void *pointer);

static struct { void *pointer; size_t size; } allocations[128];
static size_t allocated, peak;
static bool fail;
static void *track(void *pointer, size_t size)
{
    if (!pointer) { return NULL; }
    for (size_t i = 0; i < 128; ++i) {
        if (!allocations[i].pointer) {
            allocations[i].pointer = pointer; allocations[i].size = size;
            allocated += size; if (allocated > peak) { peak = allocated; }
            CHECK(allocated <= TB_BROWSE_LIMIT); return pointer;
        }
    }
    CHECK(false); return NULL;
}
void *__wrap_malloc(size_t size) { return fail ? NULL : track(__real_malloc(size), size); }
void *__wrap_calloc(size_t count, size_t size) { return fail ? NULL : track(__real_calloc(count, size), count * size); }
void __wrap_free(void *pointer)
{
    if (!pointer) { return; }
    for (size_t i = 0; i < 128; ++i) {
        if (allocations[i].pointer == pointer) {
            allocated -= allocations[i].size; allocations[i].pointer = NULL; __real_free(pointer); return;
        }
    }
    CHECK(false);
}
int main(void)
{
    fail = true; CHECK(!tb_browse_create()); fail = false;
    TbBrowseCache *cache = tb_browse_create(); CHECK(cache);
    size_t empty = tb_browse_allocated(cache); CHECK(empty == allocated && empty < 1024);
    static unsigned char payload[65536];
    TbBrowseKey key = {.kind = TB_BROWSE_CODE, .view = 3, .generation = {1, 2, 3}, .epoch = 7};
    for (unsigned i = 0; i < 100; ++i) {
        key.begin = i; key.end = i + 1; payload[0] = (unsigned char)i;
        CHECK(tb_browse_store(cache, key, payload, sizeof payload)); CHECK(tb_browse_allocated(cache) == allocated);
    }
    CHECK(peak <= TB_BROWSE_LIMIT && allocated > TB_BROWSE_LIMIT - 2 * sizeof payload);
    TbBrowseSample sample; key.begin = 0; CHECK(!tb_browse_find(cache, key, &sample));
    key.begin = 99; CHECK(tb_browse_find(cache, key, &sample));
    CHECK(sample.size == sizeof payload && ((const unsigned char *)sample.data)[0] == 99);
    CHECK(sample.key.generation.stop == 2 && sample.key.epoch == 7);
    key.begin = 80; key.end = 81; CHECK(tb_browse_neighbor(cache, key, -1, &sample) && sample.key.begin == 79);
    CHECK(tb_browse_neighbor(cache, key, 1, &sample) && sample.key.begin == 81);
    key.view = 4; CHECK(!tb_browse_find(cache, key, &sample)); key.view = 3;
    key.kind = TB_BROWSE_MEMORY; CHECK(!tb_browse_find(cache, key, &sample));
    CHECK(tb_browse_store(cache, key, payload, 16));
    size_t before = allocated; CHECK(!tb_browse_store(cache, key, payload, TB_BROWSE_LIMIT)); CHECK(before == allocated);
    fail = true; key.begin = 1000; key.end = 1001; CHECK(!tb_browse_store(cache, key, payload, 16)); fail = false;
    CHECK(tb_browse_retain(cache) == cache); tb_browse_release(cache); CHECK(allocated == tb_browse_allocated(cache));
    tb_browse_clear(cache, TB_BROWSE_CODE, 4); CHECK(allocated == before);
    tb_browse_clear(cache, TB_BROWSE_CODE, -1); CHECK(allocated < 1024);
    key.begin = 80; CHECK(tb_browse_find(cache, key, &sample));
    tb_browse_clear(cache, TB_BROWSE_MEMORY, 3); CHECK(allocated == empty);
    tb_browse_release(cache); CHECK(!allocated);
    puts("PASS shared cache actual allocations, transient peak, eviction, provenance, isolation, failure and reference ownership");
    return 0;
}
