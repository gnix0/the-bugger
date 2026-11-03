#ifndef THE_BUGGER_CONTEXT_H
#define THE_BUGGER_CONTEXT_H

#include "gdb/controller.h"

enum {
    TB_CONTEXT_THREADS   = 128,
    TB_CONTEXT_FRAMES    = 32,
    TB_CONTEXT_MAX_LEVEL = 4095
};

typedef struct {
    int  level;
    int  line;
    char path[4096];            /* Exact filesystem bytes; file below is a render-safe label. */
    char address[48];
    char function[160];
    char file[384];
    char from[160];
    char architecture[64];
    char arguments[256];
    char argument_names[256];
    bool arguments_read;
} TbFrame_t;

typedef struct {
    char      id[32];
    char      name[96];
    char      target[128];
    char      state[24];
    TbFrame_t frame;
    uint64_t  lifetime;         /* Session-local identity; numeric ID reuse never inherits it. */
} TbThread_t;

typedef struct {
    TbGdbGeneration_t generation;
    bool              ready;
    bool              stale;
    bool              refreshing;
    bool              threads_limited;
    bool              more_frames;
    char              selected_thread[32];
    char              stopped_thread[32];
    TbFrame_t         selected;
    TbFrame_t         stopped;
    TbThread_t        threads[TB_CONTEXT_THREADS];
    TbFrame_t         frames[TB_CONTEXT_FRAMES];
    size_t            thread_count;
    size_t            frame_count;
    int               first_frame;
    char              error[192];
    char              stack_error[192];
    char              arguments_error[192];
} TbContextSnapshot_t;

typedef struct tb_context TbContext_t;

TbContext_t *tb_context_create(TbGdb_t *gdb);
void tb_context_destroy(TbContext_t *context);
void tb_context_reset(TbContext_t *context);
/* Called from the controller callback: copies borrowed data only. Returns true
 * for completions owned by this module, so other request owners ignore them. */
bool tb_context_event(TbContext_t *context, const TbGdbEvent_t *event);
/* No controller mutation from callbacks. Pump only when session setup/manual
 * operations are idle. No polling reads while running or with no visible view. */
/* One stopped mutation at a time across inspectors. Acquire only immediately
 * before dispatch on an idle controller; finish outside event callbacks.
 * Uncertain mutations quarantine all inspector writes until explicit recovery. */
typedef enum {
    TB_WRITE_ACKNOWLEDGED,
    TB_WRITE_UNCERTAIN,
    TB_WRITE_UNRESOLVED
} TbWriteOutcome_e;

bool tb_context_write_begin(TbContext_t *context, uint64_t cookie);
void tb_context_write_finish(TbContext_t *context, uint64_t cookie, TbWriteOutcome_e outcome);
bool tb_context_write_blocked(const TbContext_t *context);
/* Mutation in this stop requires deliberate value reads, including aliases. */
bool tb_context_written(const TbContext_t *context);
bool tb_context_write_reconcile(TbContext_t *context, uint64_t cookie);
void tb_context_pump(TbContext_t *context);
void tb_context_visible(TbContext_t *context, bool threads, bool frames);
void tb_context_require(TbContext_t *context, bool required);
void tb_context_registers(TbContext_t *context, bool required);
void tb_context_memory(TbContext_t *context, bool required);
void tb_context_variables(TbContext_t *context, bool required);
void tb_context_stack(TbContext_t *context, bool required);
uint64_t tb_context_thread_lifetime(const TbContext_t *context, const char *id);
bool tb_context_lifetime_live(const TbContext_t *context, uint64_t lifetime);
void tb_context_interlock(TbContext_t *context, bool active);
bool tb_context_interlocked(const TbContext_t *context);
bool tb_context_frame_pending(const TbContext_t *context);
void tb_context_refresh(TbContext_t *context); /* Outside controller callbacks. */
bool tb_context_select_thread(TbContext_t *context, const char *id);
bool tb_context_return_stopped(TbContext_t *context);
/* Frame rows must belong to a fresh snapshot, never the previous thread. */
bool tb_context_select_frame(TbContext_t *context, int level);
/* Deliberate value read of the selected frame only. Never repeated automatically.
 * Unknown storage is not authorized for automatic argument reads. */
bool tb_context_read_arguments(TbContext_t *context);
bool tb_context_page(TbContext_t *context, int direction);
const TbContextSnapshot_t *tb_context_snapshot(const TbContext_t *context);
uint64_t tb_context_revision(const TbContext_t *context);

#endif // THE_BUGGER_CONTEXT_H
