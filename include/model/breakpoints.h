#ifndef THE_BUGGER_BREAKPOINTS_H
#define THE_BUGGER_BREAKPOINTS_H

#include "gdb/controller.h"
#include "session/setup.h"
#include <sys/types.h>

enum { TB_BREAK_ROWS = 256 };

typedef enum { TB_WATCH_CHANGE, TB_WATCH_READ, TB_WATCH_ACCESS, TB_WATCH_MODES } TbWatchMode_e;

typedef struct {
    char          id[32], parent[32], type[64], address[48], file[4096], location[8193], condition[512],
                  unsupported[192];
    unsigned      line, ignore;
    uint64_t      hits;
    bool          enabled, pending, multiple, hardware, temporary, child;
    bool          watchpoint, definition_complete;
    TbWatchMode_e watch_mode;
} TbBreakpoint_t;

typedef struct tb_breakpoints TbBreakpoints_t;

typedef struct {
    uint64_t       identity;
    TbSetupMapping mapping;
    char           location[8193], source[4096], condition[512];
    unsigned       line, ignore;
    bool           enabled, ignore_set, hardware;
} TbBreakDefinition_t;

/* Configured frontend intent, independent of hits/remaining backend counts.
 * Backend-only entries are excluded until explicitly imported. */
size_t tb_break_definition_count(const TbBreakpoints_t *breakpoints);
const TbBreakDefinition_t *tb_break_definition(const TbBreakpoints_t *breakpoints, size_t index);
bool tb_break_definitions_ready(const TbBreakpoints_t *breakpoints);
void tb_break_interlock(TbBreakpoints_t *breakpoints, bool active);
void tb_break_definition_reserve(TbBreakpoints_t *breakpoints, size_t retained);

typedef enum {
    TB_RESTORE_PENDING,
    TB_RESTORE_AWAITING_STOP,
    TB_RESTORE_ADDRESS,
    TB_RESTORE_CREATING,
    TB_RESTORE_CONFIGURING,
    TB_RESTORE_VERIFYING,
    TB_RESTORE_INSTALLED,
    TB_RESTORE_UNRESOLVED,
    TB_RESTORE_UNCERTAIN,
    TB_RESTORE_DRAFT
} TbBreakRestoreState_e;

typedef struct {
    uint64_t              id, identity;
    TbBreakRestoreState_e state;
    char                  backend[32], reason[192];
} TbBreakRestoration_t;

/* Runtime progress and backend IDs are never serialized. One bounded operation
 * at a time, disabled insertion and attribute verification before enable. */
bool tb_break_restore(TbBreakpoints_t *breakpoints, const TbSetupBreak *definition, bool address_approved);
const TbBreakRestoration_t *tb_break_restoration(const TbBreakpoints_t *breakpoints, uint64_t identity);
bool tb_break_restoration_active(const TbBreakpoints_t *breakpoints);
bool tb_break_restoration_uncertain(const TbBreakpoints_t *breakpoints);
void tb_break_restore_cancel(TbBreakpoints_t *breakpoints);
bool tb_break_reconcile(TbBreakpoints_t *breakpoints);
bool tb_break_literal(const TbSetupBreak *definition, uint64_t *address);
bool tb_break_importable(const TbBreakpoints_t *breakpoints, const char *id, TbBreakDefinition_t *definition);
const char *tb_break_conflict(const TbBreakpoints_t *breakpoints, size_t definition);
bool tb_break_discard_intent(TbBreakpoints_t *breakpoints, uint64_t identity);
bool tb_break_managed(const TbBreakpoints_t *breakpoints, const char *id);
bool tb_break_review_intent(TbBreakpoints_t *breakpoints, const char *id,
                            const TbBreakDefinition_t *definition);
bool tb_break_restore_draft(TbBreakpoints_t *breakpoints, uint64_t identity, const char *backend);
bool tb_break_owned(const TbBreakpoints_t *breakpoints, const char *backend);
void tb_break_restore_forget(TbBreakpoints_t *breakpoints, uint64_t identity);
const char *tb_break_restore_state(TbBreakRestoreState_e state);
typedef struct {
    const char *text, *source;
    unsigned    line;
} TbBreakLocation_t;

TbBreakpoints_t *tb_break_create(TbGdb_t *gdb);
void tb_break_destroy(TbBreakpoints_t *breakpoints);
void tb_break_reset(TbBreakpoints_t *breakpoints);
bool tb_break_event(TbBreakpoints_t *breakpoints, const TbGdbEvent_t *event);
void tb_break_native(TbBreakpoints_t *breakpoints, pid_t pid);
void tb_break_pump(TbBreakpoints_t *breakpoints);
void tb_break_refresh(TbBreakpoints_t *breakpoints);
bool tb_break_busy(const TbBreakpoints_t *breakpoints);
size_t tb_break_count(const TbBreakpoints_t *breakpoints);
const TbBreakpoint_t *tb_break_row(const TbBreakpoints_t *breakpoints, size_t row);
uint64_t tb_break_revision(const TbBreakpoints_t *breakpoints);
const char *tb_break_message(const TbBreakpoints_t *breakpoints);
bool tb_break_ready(const TbBreakpoints_t *breakpoints);
bool tb_break_add(TbBreakpoints_t *breakpoints, const char *location, bool hardware);
bool tb_break_add_at(TbBreakpoints_t *breakpoints, TbBreakLocation_t location, bool hardware);
/* Deliberate creation can read target storage. Never called automatically.
 * Execution stays blocked until hardware type is verified or fallback removed. */
bool tb_break_watch(TbBreakpoints_t *breakpoints, const char *expression, TbWatchMode_e mode);
bool tb_break_watch_address(TbBreakpoints_t *breakpoints, uint64_t address, unsigned bytes,
                            TbWatchMode_e mode);
const char *tb_break_watch_mode(TbWatchMode_e mode);
bool tb_break_remove(TbBreakpoints_t *breakpoints, const char *id);
bool tb_break_enable(TbBreakpoints_t *breakpoints, const char *id, bool enabled);
bool tb_break_condition(TbBreakpoints_t *breakpoints, const char *id, const char *condition);
bool tb_break_ignore(TbBreakpoints_t *breakpoints, const char *id, const char *count);
/* Temporary run-to state is owned separately, removed on ANY intervening stop,
 * resume failure or cancellation. Never change an existing user breakpoint. */
bool tb_break_run_to(TbBreakpoints_t *breakpoints, const char *location, const char *thread);
bool tb_break_run_at(TbBreakpoints_t *breakpoints, TbBreakLocation_t location, const char *thread);
const TbBreakpoint_t *tb_break_at(const TbBreakpoints_t *breakpoints, const char *path, unsigned line,
                                  const char *address);

#endif // THE_BUGGER_BREAKPOINTS_H
