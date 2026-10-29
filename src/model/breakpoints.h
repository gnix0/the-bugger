#ifndef THE_BUGGER_BREAKPOINTS_H
#define THE_BUGGER_BREAKPOINTS_H
#include "controller.h"
#include "setup.h"
#include <sys/types.h>

enum { TB_BREAK_ROWS = 256 };
typedef enum { TB_WATCH_CHANGE, TB_WATCH_READ, TB_WATCH_ACCESS, TB_WATCH_MODES } TbWatchMode;
typedef struct {
    char id[32], parent[32], type[64], address[48], file[4096], location[8193], condition[512], unsupported[192];
    unsigned line, ignore; uint64_t hits;
    bool enabled, pending, multiple, hardware, temporary, child;
    bool watchpoint, definition_complete;
    TbWatchMode watch_mode;
} TbBreakpoint;
typedef struct TbBreakpoints TbBreakpoints;
typedef struct {
    uint64_t identity;
    TbSetupMapping mapping;
    char location[8193], source[4096], condition[512];
    unsigned line, ignore;
    bool enabled, ignore_set, hardware;
} TbBreakDefinition;
/* Configured frontend intent, independent of hits/remaining backend counts.
 * Backend-only entries are excluded until explicitly imported. */
size_t tb_break_definition_count(const TbBreakpoints *breakpoints);
const TbBreakDefinition *tb_break_definition(const TbBreakpoints *breakpoints, size_t index);
bool tb_break_definitions_ready(const TbBreakpoints *breakpoints);
void tb_break_interlock(TbBreakpoints *breakpoints, bool active);
void tb_break_definition_reserve(TbBreakpoints *breakpoints, size_t retained);
typedef enum { TB_RESTORE_PENDING, TB_RESTORE_AWAITING_STOP, TB_RESTORE_ADDRESS,
               TB_RESTORE_CREATING, TB_RESTORE_CONFIGURING, TB_RESTORE_VERIFYING,
               TB_RESTORE_INSTALLED, TB_RESTORE_UNRESOLVED, TB_RESTORE_UNCERTAIN,
               TB_RESTORE_DRAFT } TbBreakRestoreState;
typedef struct {
    uint64_t id, identity;
    TbBreakRestoreState state;
    char backend[32], reason[192];
} TbBreakRestoration;
/* Runtime progress and backend IDs are never serialized. One bounded operation
 * at a time, disabled insertion and attribute verification before enable. */
bool tb_break_restore(TbBreakpoints *breakpoints, const TbSetupBreak *definition, bool address_approved);
const TbBreakRestoration *tb_break_restoration(const TbBreakpoints *breakpoints, uint64_t identity);
bool tb_break_restoration_active(const TbBreakpoints *breakpoints);
bool tb_break_restoration_uncertain(const TbBreakpoints *breakpoints);
void tb_break_restore_cancel(TbBreakpoints *breakpoints);
bool tb_break_reconcile(TbBreakpoints *breakpoints);
bool tb_break_literal(const TbSetupBreak *definition, uint64_t *address);
bool tb_break_importable(const TbBreakpoints *breakpoints, const char *id, TbBreakDefinition *definition);
const char *tb_break_conflict(const TbBreakpoints *breakpoints, size_t definition);
bool tb_break_discard_intent(TbBreakpoints *breakpoints, uint64_t identity);
bool tb_break_managed(const TbBreakpoints *breakpoints, const char *id);
bool tb_break_review_intent(TbBreakpoints *breakpoints, const char *id, const TbBreakDefinition *definition);
bool tb_break_restore_draft(TbBreakpoints *breakpoints, uint64_t identity, const char *backend);
bool tb_break_owned(const TbBreakpoints *breakpoints, const char *backend);
void tb_break_restore_forget(TbBreakpoints *breakpoints, uint64_t identity);
const char *tb_break_restore_state(TbBreakRestoreState state);
typedef struct { const char *text, *source; unsigned line; } TbBreakLocation;
TbBreakpoints *tb_break_create(TbGdb *gdb);
void tb_break_destroy(TbBreakpoints *breakpoints);
void tb_break_reset(TbBreakpoints *breakpoints);
bool tb_break_event(TbBreakpoints *breakpoints, const TbGdbEvent *event);
void tb_break_native(TbBreakpoints *breakpoints, pid_t pid);
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
/* Deliberate creation can read target storage. Never called automatically.
 * Execution stays blocked until hardware type is verified or fallback removed. */
bool tb_break_watch(TbBreakpoints *breakpoints, const char *expression, TbWatchMode mode);
bool tb_break_watch_address(TbBreakpoints *breakpoints, uint64_t address, unsigned bytes, TbWatchMode mode);
const char *tb_break_watch_mode(TbWatchMode mode);
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
