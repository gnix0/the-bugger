#ifndef THE_BUGGER_BREAKPOINTS_H
#define THE_BUGGER_BREAKPOINTS_H
#include "controller.h"

enum { TB_BREAK_ROWS = 256 };
typedef struct {
    char id[32], parent[32], type[64], address[48], file[4096], location[512], condition[512];
    unsigned line, ignore; uint64_t hits;
    bool enabled, pending, multiple, hardware, temporary, child;
} TbBreakpoint;
typedef struct TbBreakpoints TbBreakpoints;
typedef struct { const char *text, *source; unsigned line; } TbBreakLocation;
TbBreakpoints *tb_break_create(TbGdb *gdb);
void tb_break_destroy(TbBreakpoints *breakpoints);
void tb_break_reset(TbBreakpoints *breakpoints);
bool tb_break_event(TbBreakpoints *breakpoints, const TbGdbEvent *event);
void tb_break_pump(TbBreakpoints *breakpoints);
void tb_break_refresh(TbBreakpoints *breakpoints);
bool tb_break_busy(const TbBreakpoints *breakpoints);
size_t tb_break_count(const TbBreakpoints *breakpoints);
const TbBreakpoint *tb_break_row(const TbBreakpoints *breakpoints, size_t row);
uint64_t tb_break_revision(const TbBreakpoints *breakpoints);
const char *tb_break_message(const TbBreakpoints *breakpoints);
bool tb_break_ready(const TbBreakpoints *breakpoints);
bool tb_break_add(TbBreakpoints *breakpoints, const char *location, bool hardware);
bool tb_break_add_at(TbBreakpoints *breakpoints, TbBreakLocation location, bool hardware);
bool tb_break_remove(TbBreakpoints *breakpoints, const char *id);
bool tb_break_enable(TbBreakpoints *breakpoints, const char *id, bool enabled);
bool tb_break_condition(TbBreakpoints *breakpoints, const char *id, const char *condition);
bool tb_break_ignore(TbBreakpoints *breakpoints, const char *id, const char *count);
/* Temporary run-to state is owned separately, removed on ANY intervening stop,
 * resume failure or cancellation. Never change an existing user breakpoint. */
bool tb_break_run_to(TbBreakpoints *breakpoints, const char *location, const char *thread);
bool tb_break_run_at(TbBreakpoints *breakpoints, TbBreakLocation location, const char *thread);
const TbBreakpoint *tb_break_at(const TbBreakpoints *breakpoints, const char *path, unsigned line, const char *address);
#endif
