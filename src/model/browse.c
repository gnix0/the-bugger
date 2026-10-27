#include "browse.h"

#include <stdlib.h>
#include <string.h>

typedef struct Entry Entry;
struct Entry {
    Entry *next, *previous;
    TbBrowseKey key;
    size_t size;
    unsigned char data[];
};
struct TbBrowseCache {
    Entry *first, *last;
    size_t allocated;
    unsigned references;
};

TbBrowseCache *tb_browse_create(void)
{
    TbBrowseCache *cache = calloc(1, sizeof *cache);
    if (cache) { cache->references = 1; cache->allocated = sizeof *cache; }
    return cache;
}
TbBrowseCache *tb_browse_retain(TbBrowseCache *cache)
{
    if (cache) { ++cache->references; } return cache;
}
static void unlink_entry(TbBrowseCache *cache, Entry *entry)
{
    if (cache->first == entry) { cache->first = entry->next; }
    if (cache->last == entry) { cache->last = entry->previous; }
    if (entry->previous) { entry->previous->next = entry->next; }
    if (entry->next) { entry->next->previous = entry->previous; }
}
static void discard(TbBrowseCache *cache, Entry *entry)
{
    unlink_entry(cache, entry); cache->allocated -= sizeof *entry + entry->size; free(entry);
}
void tb_browse_release(TbBrowseCache *cache)
{
    if (!cache || --cache->references) { return; }
    while (cache->last) { discard(cache, cache->last); } free(cache);
}
void tb_browse_clear(TbBrowseCache *cache, TbBrowseKind kind, int view)
{
    if (!cache) { return; }
    Entry *entry = cache->first;
    while (entry) {
        Entry *next = entry->next;
        if (entry->key.kind == kind && (view < 0 || entry->key.view == view)) { discard(cache, entry); }
        entry = next;
    }
}
size_t tb_browse_allocated(const TbBrowseCache *cache) { return cache ? cache->allocated : 0; }
static bool owner(TbBrowseKey a, TbBrowseKey b)
{
    return a.kind == b.kind && a.view == b.view && a.format == b.format;
}
bool tb_browse_store(TbBrowseCache *cache, TbBrowseKey key, const void *data, size_t size)
{
    if (!cache || !data || !size || key.end <= key.begin ||
        size > TB_BROWSE_LIMIT - sizeof *cache - sizeof(Entry)) { return false; }
    /* Allocate only after making room, so transient allocation never exceeds
     * the same budget. Failed allocation may evict history, never the viewport. */
    Entry *entry = cache->first;
    while (entry) {
        Entry *next = entry->next;
        if (owner(entry->key, key) && entry->key.begin == key.begin) { discard(cache, entry); }
        entry = next;
    }
    size_t allocation = sizeof(Entry) + size;
    while (cache->allocated > TB_BROWSE_LIMIT - allocation) {
        if (!cache->last) { return false; }
        discard(cache, cache->last);
    }
    entry = malloc(allocation); if (!entry) { return false; }
    *entry = (Entry){.next = cache->first, .key = key, .size = size};
    memcpy(entry->data, data, size);
    if (cache->first) { cache->first->previous = entry; } else { cache->last = entry; }
    cache->first = entry; cache->allocated += allocation; return true;
}
static bool found(TbBrowseCache *cache, Entry *entry, TbBrowseSample *sample)
{
    if (!entry) { return false; }
    unlink_entry(cache, entry); entry->previous = NULL; entry->next = cache->first;
    if (cache->first) { cache->first->previous = entry; } else { cache->last = entry; }
    cache->first = entry;
    *sample = (TbBrowseSample){.key = entry->key, .data = entry->data, .size = entry->size}; return true;
}
bool tb_browse_find(TbBrowseCache *cache, TbBrowseKey key, TbBrowseSample *sample)
{
    if (!cache || !sample) { return false; }
    for (Entry *entry = cache->first; entry; entry = entry->next) {
        if (owner(entry->key, key) && entry->key.begin == key.begin) { return found(cache, entry, sample); }
    }
    return false;
}
bool tb_browse_neighbor(TbBrowseCache *cache, TbBrowseKey key, int direction, TbBrowseSample *sample)
{
    if (!cache || !sample || !direction) { return false; }
    Entry *best = NULL;
    for (Entry *entry = cache->first; entry; entry = entry->next) {
        if (!owner(entry->key, key) || (direction < 0 ? entry->key.begin >= key.begin : entry->key.begin <= key.begin)) { continue; }
        if (direction < 0 ? entry->key.end < key.begin : entry->key.begin > key.end) { continue; }
        if (!best || (direction < 0 ? entry->key.begin > best->key.begin : entry->key.begin < best->key.begin)) { best = entry; }
    }
    return found(cache, best, sample);
}
