#ifndef THE_BUGGER_CONTEXT_H
#define THE_BUGGER_CONTEXT_H

#include "controller.h"

enum { TB_CONTEXT_THREADS = 128, TB_CONTEXT_FRAMES = 32, TB_CONTEXT_MAX_LEVEL = 4095 };
typedef struct {
    int level, line;
    char address[48], function[160], file[384], from[160], architecture[64];
    char arguments[256];
} TbFrame;
typedef struct {
    char id[32], name[96], target[128], state[24];
    TbFrame frame;
} TbThread;
typedef struct {
    TbGdbGeneration generation;
    bool ready, stale, refreshing, threads_limited, more_frames;
    char selected_thread[32], stopped_thread[32];
    TbFrame selected, stopped;
    TbThread threads[TB_CONTEXT_THREADS];
    TbFrame frames[TB_CONTEXT_FRAMES];
    size_t thread_count, frame_count;
    int first_frame;
    char error[192], stack_error[192], arguments_error[192];
} TbContextSnapshot;
typedef struct TbContext TbContext;

TbContext *tb_context_create(TbGdb *gdb);
void tb_context_destroy(TbContext *context);
void tb_context_reset(TbContext *context);
/* Called from the controller callback: copies borrowed data only. Returns true
 * for completions owned by this module, so other request owners ignore them. */
bool tb_context_event(TbContext *context, const TbGdbEvent *event);
/* No controller mutation from callbacks. Pump only when session setup/manual
 * operations are idle. No polling reads while running or with no visible view. */
void tb_context_pump(TbContext *context);
void tb_context_visible(TbContext *context, bool threads, bool frames);
void tb_context_refresh(TbContext *context); /* Outside controller callbacks. */
bool tb_context_select_thread(TbContext *context, const char *id);
/* Frame rows must belong to a fresh snapshot, never the previous thread. */
bool tb_context_select_frame(TbContext *context, int level);
bool tb_context_page(TbContext *context, int direction);
const TbContextSnapshot *tb_context_snapshot(const TbContext *context);
uint64_t tb_context_revision(const TbContext *context);

#endif
