#include "model/browse.h"

#include <stdlib.h>
#include <string.h>

typedef struct entry Entry_t;

struct entry {
    Entry_t       *next;
    Entry_t       *previous;
    TbBrowseKey_t  key;
    size_t         size;
    unsigned char  data[];
};

struct tb_browse_cache {
    Entry_t  *first;
    Entry_t  *last;
    size_t    allocated;
    unsigned  references;
};

TbBrowseCache_t *tb_browse_create(void)
{
    TbBrowseCache_t *cache = calloc(1, sizeof(*cache));

    if (cache) {
        cache->references = 1;
        cache->allocated  = sizeof(*cache);
    }

    return cache;
}


TbBrowseCache_t *tb_browse_retain(TbBrowseCache_t *cache)
{
    if (cache)
        ++cache->references;

    return cache;
}


static void unlink_entry(TbBrowseCache_t *cache, Entry_t *entry)
{
    if (cache->first == entry)
        cache->first = entry->next;
    if (cache->last == entry)
        cache->last = entry->previous;
    if (entry->previous)
        entry->previous->next = entry->next;
    if (entry->next)
        entry->next->previous = entry->previous;
}


static void discard(TbBrowseCache_t *cache, Entry_t *entry)
{
    unlink_entry(cache, entry);
    cache->allocated -= sizeof(*entry) + entry->size;
    free(entry);
}


void tb_browse_release(TbBrowseCache_t *cache)
{
    if (!cache || --cache->references)
        return;

    while (cache->last)
        discard(cache, cache->last);

    free(cache);
}


void tb_browse_clear(TbBrowseCache_t *cache, TbBrowseKind_e kind, int view)
{
    if (!cache)
        return;

    Entry_t *entry = cache->first;
    while (entry) {
        Entry_t *next = entry->next;
        if (entry->key.kind == kind && (view < 0 || entry->key.view == view))
            discard(cache, entry);
        entry = next;
    }
}


size_t tb_browse_allocated(const TbBrowseCache_t *cache)
{
    return cache ? cache->allocated : 0;
}


static bool owner(TbBrowseKey_t a, TbBrowseKey_t b)
{
    return a.kind == b.kind && a.view == b.view && a.format == b.format;
}


bool tb_browse_store(TbBrowseCache_t *cache, TbBrowseKey_t key, const void *data, size_t size)
{
    if (!cache || !data || !size || key.end <= key.begin ||
        size > TB_BROWSE_LIMIT - sizeof(*cache) - sizeof(Entry_t)) {
        return false;
    }

    /* Allocate only after making room, so transient allocation never exceeds
     * the same budget. Failed allocation may evict history, never the viewport. */
    Entry_t *entry = cache->first;
    while (entry) {
        Entry_t *next = entry->next;
        if (owner(entry->key, key) && entry->key.begin == key.begin)
            discard(cache, entry);
        entry = next;
    }

    size_t allocation = sizeof(Entry_t) + size;
    while (cache->allocated > TB_BROWSE_LIMIT - allocation) {
        if (!cache->last)
            return false;

        discard(cache, cache->last);
    }

    entry = malloc(allocation);
    if (!entry)
        return false;

    *entry = (Entry_t) {
        .next = cache->first,
        .key  = key,
        .size = size
    };

    memcpy(entry->data, data, size);
    if (cache->first) {
        cache->first->previous = entry;
    } else {
        cache->last = entry;
    }
    cache->first      = entry;
    cache->allocated += allocation;

    return true;
}


static bool found(TbBrowseCache_t *cache, Entry_t *entry, TbBrowseSample_t *sample)
{
    if (!entry)
        return false;

    unlink_entry(cache, entry);
    entry->previous = NULL;
    entry->next     = cache->first;
    if (cache->first) {
        cache->first->previous = entry;
    } else {
        cache->last = entry;
    }
    cache->first = entry;
    *sample      = (TbBrowseSample_t) {
        .key  = entry->key,
        .data = entry->data,
        .size = entry->size
    };

    return true;
}


bool tb_browse_find(TbBrowseCache_t *cache, TbBrowseKey_t key, TbBrowseSample_t *sample)
{
    if (!cache || !sample)
        return false;

    for (Entry_t *entry = cache->first; entry; entry = entry->next) {
        if (owner(entry->key, key) && entry->key.begin == key.begin)
            return found(cache, entry, sample);
    }

    return false;
}


bool tb_browse_neighbor(TbBrowseCache_t *cache, TbBrowseKey_t key, int direction, TbBrowseSample_t *sample)
{
    if (!cache || !sample || !direction)
        return false;

    Entry_t *best = NULL;

    for (Entry_t *entry = cache->first; entry; entry = entry->next) {
        if (!owner(entry->key, key) ||
            (direction < 0 ? entry->key.begin >= key.begin : entry->key.begin <= key.begin)) {
            continue;
        }
        if (direction < 0 ? entry->key.end < key.begin : entry->key.begin > key.end)
            continue;

        if (!best ||
            (direction < 0 ? entry->key.begin > best->key.begin : entry->key.begin < best->key.begin)) {
            best = entry;
        }
    }

    return found(cache, best, sample);
}


bool tb_browse_history(TbBrowseCache_t *cache, TbBrowseKey_t key, int direction, TbBrowseSample_t *sample)
{
    if (!cache || !sample || !direction)
        return false;

    Entry_t *best = NULL;

    for (Entry_t *entry = cache->first; entry; entry = entry->next) {
        if (!owner(entry->key, key) ||
            (direction < 0 ? entry->key.begin >= key.begin : entry->key.begin <= key.begin)) {
            continue;
        }
        if (!best ||
            (direction < 0 ? entry->key.begin > best->key.begin : entry->key.begin < best->key.begin)) {
            best = entry;
        }
    }

    return found(cache, best, sample);
}
