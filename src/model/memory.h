#ifndef THE_BUGGER_MEMORY_H
#define THE_BUGGER_MEMORY_H
#include "context.h"
#include "browse.h"

enum { TB_MEMORY_VIEWS = 32, TB_MEMORY_BYTES = 1024, TB_MEMORY_ROW = 16, TB_MEMORY_REGIONS = 128,
       TB_MEMORY_RANGES = 16 };
/* Established ordinary RAM/ROM only; unknown/MMIO ranges are never implicit. */
typedef struct { uint64_t begin, end; } TbMemoryRegion;
typedef enum { TB_MEMORY_UNKNOWN, TB_MEMORY_LITTLE, TB_MEMORY_BIG } TbMemoryOrder;
typedef enum { TB_MEMORY_ORDINARY, TB_MEMORY_MMIO } TbMemoryPolicy;
enum { TB_MEMORY_WIDTH8 = 1, TB_MEMORY_WIDTH16 = 2, TB_MEMORY_WIDTH32 = 4 };
/* Descriptive assertions from the reviewed setup, not automatic verification.
 * The session adapter must replace/invalidate this identity whenever any
 * relevant connection, target or server configuration changes. */
typedef struct {
    char target[64], device[128], server[512], firmware[64];
    uint64_t configuration;
    unsigned widths;
} TbMemoryBackend;
typedef struct {
    uint64_t begin, end; /* Half-open bounds; never wrap. */
    TbMemoryPolicy policy;
    unsigned width; /* MMIO bits; zero for ordinary byte ranges. */
    bool used, authorized;
    char risks[160]; /* Empty means side effects are unknown, never safe. */
    TbGdbGeneration generation;
} TbMemoryRange;
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
void tb_memory_cache(TbMemory *memory, TbBrowseCache *cache);
/* Metadata-only: no debugger requests, reads or polling. NULL invalidates.
 * Confirm exactly the identity displayed in the review; stale reviews fail. */
bool tb_memory_backend_set(TbMemory *memory, const TbMemoryBackend *backend);
const TbMemoryBackend *tb_memory_backend(const TbMemory *memory);
bool tb_memory_backend_confirm(TbMemory *memory, const TbMemoryBackend *reviewed);
bool tb_memory_backend_confirmed(const TbMemory *memory);
/* Consent management only: none of these functions schedules a target read. */
const TbMemoryRange *tb_memory_range(const TbMemory *memory, int view, unsigned range);
bool tb_memory_range_set(TbMemory *memory, int view, unsigned range, uint64_t begin, uint64_t end,
                         TbMemoryPolicy policy, unsigned width, const char *risks);
bool tb_memory_range_authorize(TbMemory *memory, int view, unsigned range);
bool tb_memory_range_revoke(TbMemory *memory, int view, unsigned range);
bool tb_memory_range_authorized(const TbMemory *memory, int view, unsigned range);
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
/* Capability metadata only; unknown/non-octet/wider targets reject reads before dispatch. */
void tb_memory_target(TbMemory *memory, bool octets, unsigned address_bits);
void tb_memory_order(TbMemory *memory, TbMemoryOrder order);
TbMemoryOrder tb_memory_get_order(const TbMemory *memory);
const TbMemoryView *tb_memory_view(const TbMemory *memory, int view);
bool tb_memory_stale(const TbMemory *memory, int view);
void tb_memory_line(const TbMemory *memory, int view, size_t row, char *out, size_t capacity);
bool tb_memory_event(TbMemory *memory, const TbGdbEvent *event);
void tb_memory_pump(TbMemory *memory);
uint64_t tb_memory_revision(const TbMemory *memory);
#endif
