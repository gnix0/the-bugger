#ifndef THE_BUGGER_MEMORY_H
#define THE_BUGGER_MEMORY_H
#include "context.h"

enum { TB_MEMORY_VIEWS = 32, TB_MEMORY_BYTES = 1024, TB_MEMORY_ROW = 16, TB_MEMORY_REGIONS = 128 };
/* Established ordinary RAM/ROM only; unknown/MMIO ranges are never implicit. */
typedef struct { uint64_t begin, end; } TbMemoryRegion;
typedef enum { TB_MEMORY_UNKNOWN, TB_MEMORY_LITTLE, TB_MEMORY_BIG } TbMemoryOrder;
typedef struct {
    char expression[256], error[192];
    unsigned length, grouping;
    int64_t offset;
    uint64_t address;
    unsigned char bytes[TB_MEMORY_BYTES];
    bool valid[TB_MEMORY_BYTES];
    size_t data_size, readable;
    bool configured, ready, pending;
    TbGdbGeneration generation;
} TbMemoryView;
typedef struct TbMemory TbMemory;
TbMemory *tb_memory_create(TbGdb *gdb, TbContext *context);
void tb_memory_destroy(TbMemory *memory);
void tb_memory_reset(TbMemory *memory);
void tb_memory_clear(TbMemory *memory, int view);
void tb_memory_visible(TbMemory *memory, int view, bool visible);
bool tb_memory_expression(const char *expression);
bool tb_memory_address(TbMemory *memory, int view, const char *expression);
bool tb_memory_refresh(TbMemory *memory, int view);
bool tb_memory_page(TbMemory *memory, int view, int direction);
bool tb_memory_browse(TbMemory *memory, int view, int direction);
void tb_memory_regions(TbMemory *memory, const TbMemoryRegion *regions, size_t count);
bool tb_memory_length(TbMemory *memory, int view, unsigned bytes);
bool tb_memory_group(TbMemory *memory, int view, unsigned bytes);
/* Metadata is supplied by a session adapter, never inferred from the host. */
void tb_memory_order(TbMemory *memory, TbMemoryOrder order);
TbMemoryOrder tb_memory_get_order(const TbMemory *memory);
const TbMemoryView *tb_memory_view(const TbMemory *memory, int view);
bool tb_memory_stale(const TbMemory *memory, int view);
void tb_memory_line(const TbMemory *memory, int view, size_t row, char *out, size_t capacity);
bool tb_memory_event(TbMemory *memory, const TbGdbEvent *event);
void tb_memory_pump(TbMemory *memory);
uint64_t tb_memory_revision(const TbMemory *memory);
#endif
