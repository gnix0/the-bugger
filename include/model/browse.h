#ifndef THE_BUGGER_BROWSE_H
#define THE_BUGGER_BROWSE_H

#include "gdb/controller.h"

enum { TB_BROWSE_LIMIT = 4 * 1024 * 1024 };
typedef enum {
    TB_BROWSE_CODE,
    TB_BROWSE_MEMORY,
    TB_BROWSE_SOURCE,
    TB_BROWSE_MMIO,
    TB_BROWSE_ANCHORS,
    TB_BROWSE_STACK,
    TB_BROWSE_STACK_BYTES
} TbBrowseKind_e;

typedef struct tb_browse_cache TbBrowseCache_t;

typedef struct {
    TbBrowseKind_e    kind;
    int               view;
    unsigned          format;
    uint64_t          begin, end, epoch;
    TbGdbGeneration_t generation;
} TbBrowseKey_t;

typedef struct {
    TbBrowseKey_t  key;
    const void    *data;
    size_t         size;
} TbBrowseSample_t;

/* One ref-counted pool per session, shared by code and memory. Allocation
 * accounting includes the pool, every entry header and all retained payloads.
 * Eviction has no callback and can never dispatch debugger requests. */
TbBrowseCache_t *tb_browse_create(void);
TbBrowseCache_t *tb_browse_retain(TbBrowseCache_t *cache);
void tb_browse_release(TbBrowseCache_t *cache);
void tb_browse_clear(TbBrowseCache_t *cache, TbBrowseKind_e kind, int view);
size_t tb_browse_allocated(const TbBrowseCache_t *cache);
bool tb_browse_store(TbBrowseCache_t *cache, TbBrowseKey_t key, const void *data, size_t size);
/* Result data is borrowed until the next store/clear/release. A hit affects LRU
 * order, not its provenance. Freshness is always decided by the owning model. */
bool tb_browse_find(TbBrowseCache_t *cache, TbBrowseKey_t key, TbBrowseSample_t *sample);
bool tb_browse_neighbor(TbBrowseCache_t *cache, TbBrowseKey_t key, int direction, TbBrowseSample_t *sample);

/* Deliberate history inspection by sequence, independent of address adjacency. */
bool tb_browse_history(TbBrowseCache_t *cache, TbBrowseKey_t key, int direction, TbBrowseSample_t *sample);

#endif // THE_BUGGER_BROWSE_H
