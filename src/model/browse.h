#ifndef THE_BUGGER_BROWSE_H
#define THE_BUGGER_BROWSE_H

#include "controller.h"

enum { TB_BROWSE_LIMIT = 4 * 1024 * 1024 };
typedef enum { TB_BROWSE_CODE, TB_BROWSE_MEMORY, TB_BROWSE_SOURCE, TB_BROWSE_MMIO, TB_BROWSE_ANCHORS } TbBrowseKind;
typedef struct TbBrowseCache TbBrowseCache;
typedef struct {
    TbBrowseKind kind;
    int view;
    unsigned format;
    uint64_t begin, end, epoch;
    TbGdbGeneration generation;
} TbBrowseKey;
typedef struct {
    TbBrowseKey key;
    const void *data;
    size_t size;
} TbBrowseSample;

/* One ref-counted pool per session, shared by code and memory. Allocation
 * accounting includes the pool, every entry header and all retained payloads.
 * Eviction has no callback and can never dispatch debugger requests. */
TbBrowseCache *tb_browse_create(void);
TbBrowseCache *tb_browse_retain(TbBrowseCache *cache);
void tb_browse_release(TbBrowseCache *cache);
void tb_browse_clear(TbBrowseCache *cache, TbBrowseKind kind, int view);
size_t tb_browse_allocated(const TbBrowseCache *cache);
bool tb_browse_store(TbBrowseCache *cache, TbBrowseKey key, const void *data, size_t size);
/* Result data is borrowed until the next store/clear/release. A hit affects LRU
 * order, not its provenance. Freshness is always decided by the owning model. */
bool tb_browse_find(TbBrowseCache *cache, TbBrowseKey key, TbBrowseSample *sample);
bool tb_browse_neighbor(TbBrowseCache *cache, TbBrowseKey key, int direction, TbBrowseSample *sample);

#endif
