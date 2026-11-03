#ifndef THE_BUGGER_MEMORY_H
#define THE_BUGGER_MEMORY_H
#include "model/context.h"
#include "model/browse.h"

enum {
    TB_MEMORY_VIEWS   = 32,
    TB_MEMORY_BYTES   = 1024,
    TB_MEMORY_ROW     = 16,
    TB_MEMORY_REGIONS = 128,
    TB_MEMORY_CLIENTS = 64,
    TB_MEMORY_RANGES  = 16
};

/* Established ordinary RAM/ROM only; unknown/MMIO ranges are never implicit.
 * Writable is separate evidence and is never inferred from readability. */
typedef struct {
    uint64_t begin;
    uint64_t end;
    bool     writable;
} TbMemoryRegion_t;

typedef enum {
    TB_MEMORY_UNKNOWN,
    TB_MEMORY_LITTLE,
    TB_MEMORY_BIG
} TbMemoryOrder_e;

typedef enum {
    TB_MEMORY_ORDINARY,
    TB_MEMORY_MMIO
} TbMemoryPolicy_e;

enum {
    TB_MEMORY_WIDTH8  = 1,
    TB_MEMORY_WIDTH16 = 2,
    TB_MEMORY_WIDTH32 = 4
};

/* Descriptive assertions from the reviewed setup, not automatic verification.
 * The session adapter must replace/invalidate this identity whenever any
 * relevant connection, target or server configuration changes. */
typedef struct {
    char     target[64];
    char     device[128];
    char     server[512];
    char     firmware[64];
    uint64_t configuration;
    unsigned widths;
} TbMemoryBackend_t;

typedef struct {
    uint64_t          begin;
    uint64_t          end;      /* Half-open bounds; never wrap. */
    TbMemoryPolicy_e  policy;
    unsigned          width;    /* MMIO bits; zero for ordinary byte ranges. */
    bool              used;
    bool              authorized;
    char              risks[160]; /* Empty means side effects are unknown, never safe. */
    TbGdbGeneration_t generation;
} TbMemoryRange_t;

typedef struct {
    uint64_t          address;
    uint64_t          value;
    uint64_t          sampled_ms;
    unsigned          width;
    TbGdbGeneration_t generation;
} TbMemoryRegister_t;

typedef struct {
    char              expression[256];
    char              error[192];
    unsigned          length;
    unsigned          grouping;
    int64_t           offset;
    uint64_t          address;
    unsigned char     bytes[TB_MEMORY_BYTES];
    bool              valid[TB_MEMORY_BYTES];
    size_t            data_size;
    size_t            readable;
    bool              configured;
    bool              ready;
    bool              pending;
    TbGdbGeneration_t generation;
    bool              register_pending;
    char              register_error[192];
    bool              mmio;
    uint64_t          unit_address;
    unsigned          unit_width;
} TbMemoryView_t;

typedef enum {
    TB_MEMORY_EDIT_NONE,
    TB_MEMORY_EDIT_READY,
    TB_MEMORY_EDIT_PENDING,
    TB_MEMORY_EDIT_ACKNOWLEDGED,
    TB_MEMORY_EDIT_UNCERTAIN
} TbMemoryEditState_e;

typedef struct tb_memory TbMemory_t;

TbMemory_t *tb_memory_create(TbGdb_t *gdb, TbContext_t *context);
void tb_memory_cache(TbMemory_t *memory, TbBrowseCache_t *cache);

/* Metadata-only: no debugger requests, reads or polling. NULL invalidates.
 * Confirm exactly the identity displayed in the review; stale reviews fail. */
bool tb_memory_backend_set(TbMemory_t *memory, const TbMemoryBackend_t *backend);
const TbMemoryBackend_t *tb_memory_backend(const TbMemory_t *memory);
bool tb_memory_backend_confirm(TbMemory_t *memory, const TbMemoryBackend_t *reviewed);
bool tb_memory_backend_confirmed(const TbMemory_t *memory);

/* Explicit one-register operation, authorized again immediately at dispatch.
 * Cache inspection never schedules a read. Values are samples, not a promise
 * that autonomous peripheral state is unchanged while halted. */
bool tb_memory_read_register(TbMemory_t *memory, int view, uint64_t address, unsigned width);
bool tb_memory_register_sample(const TbMemory_t *memory, int view, uint64_t address, unsigned width,
                               TbMemoryRegister_t *sample);
bool tb_memory_register_busy(const TbMemory_t *memory);

/* Full bounded transport diagnostic for the session footer/retained Console. */
const char *tb_memory_mmio_message(const TbMemory_t *memory);

/* Completion/reservation bookkeeping only; never dispatches a request. */
void tb_memory_transport(TbMemory_t *memory);

/* MMIO presentation/navigation only: width-sized address units, never inferred
 * hardware-register identities. None of these calls reads the target. */
bool tb_memory_units(TbMemory_t *memory, int view, uint64_t address, unsigned width);
void tb_memory_bytes(TbMemory_t *memory, int view);
size_t tb_memory_rows(const TbMemory_t *memory, int view);
bool tb_memory_unit_address(const TbMemory_t *memory, int view, size_t row, uint64_t *address);

/* Consent management only: none of these functions schedules a target read. */
const TbMemoryRange_t *tb_memory_range(const TbMemory_t *memory, int view, unsigned range);
bool tb_memory_range_set(TbMemory_t *memory, int view, unsigned range, uint64_t begin, uint64_t end,
                         TbMemoryPolicy_e policy, unsigned width, const char *risks);
bool tb_memory_range_authorize(TbMemory_t *memory, int view, unsigned range);
bool tb_memory_range_revoke(TbMemory_t *memory, int view, unsigned range);
bool tb_memory_range_authorized(const TbMemory_t *memory, int view, unsigned range);
void tb_memory_destroy(TbMemory_t *memory);
void tb_memory_reset(TbMemory_t *memory);
void tb_memory_clear(TbMemory_t *memory, int view);
void tb_memory_visible(TbMemory_t *memory, int view, bool visible);
bool tb_memory_expression(const char *expression);
bool tb_memory_address(TbMemory_t *memory, int view, const char *expression);
bool tb_memory_refresh(TbMemory_t *memory, int view);
bool tb_memory_page(TbMemory_t *memory, int view, int direction);
bool tb_memory_browse(TbMemory_t *memory, int view, int direction);

/* Atomic replacement; invalid input revokes all ordinary evidence. Every call,
 * including identical bounds, revokes dependent grants and edit proofs. */
bool tb_memory_regions(TbMemory_t *memory, const TbMemoryRegion_t *regions, size_t count);
bool tb_memory_regions_valid(const TbMemoryRegion_t *regions, size_t count, unsigned address_bits);

/* Fresh-view metadata only: no address, sample or permission is restored. */
bool tb_memory_definition(TbMemory_t *memory, int view, unsigned bytes, unsigned grouping);
bool tb_memory_length(TbMemory_t *memory, int view, unsigned bytes);
bool tb_memory_group(TbMemory_t *memory, int view, unsigned bytes);

/* Metadata is supplied by a session adapter, never inferred from the host. */
/* Capability metadata only; unknown/non-octet/wider targets reject reads before dispatch. */
void tb_memory_target(TbMemory_t *memory, bool octets, unsigned address_bits);
void tb_memory_order(TbMemory_t *memory, TbMemoryOrder_e order);
TbMemoryOrder_e tb_memory_get_order(const TbMemory_t *memory);
const TbMemoryView_t *tb_memory_view(const TbMemory_t *memory, int view);
bool tb_memory_stale(const TbMemory_t *memory, int view);
void tb_memory_line(const TbMemory_t *memory, int view, size_t row, char *out, size_t capacity);
bool tb_memory_event(TbMemory_t *memory, const TbGdbEvent_t *event);
void tb_memory_pump(TbMemory_t *memory);
uint64_t tb_memory_revision(const TbMemory_t *memory);

/* Reserved independent clients for Stack; the same scheduler serializes all
 * byte reads. Client indices never alias ordinary pane Memory state. */
int tb_memory_stack_client(int pane);
bool tb_memory_client_read(TbMemory_t *memory, int client, uint64_t address, unsigned bytes,
                           const char *thread);
void tb_memory_client_cancel(TbMemory_t *memory, int client);
bool tb_memory_client_busy(const TbMemory_t *memory, int client);
bool tb_memory_edit_begin(TbMemory_t *memory, int view, uint64_t address, unsigned bytes);
bool tb_memory_edit_apply(TbMemory_t *memory, const char *hex);
void tb_memory_edit_cancel(TbMemory_t *memory);
bool tb_memory_edit_reconcile(TbMemory_t *memory);
TbMemoryEditState_e tb_memory_edit_state(const TbMemory_t *memory);
const char *tb_memory_edit_error(const TbMemory_t *memory);
const char *tb_memory_edit_old(const TbMemory_t *memory);
bool tb_memory_edit_destination(const TbMemory_t *memory, uint64_t *address, unsigned *bytes);
#endif // THE_BUGGER_MEMORY_H
