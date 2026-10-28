#include "setup_restore.h"
#include <stdlib.h>

typedef enum { RESTORE_NONE, RESTORE_CONNECTION, RESTORE_SETTINGS, RESTORE_CATALOG,
               RESTORE_SOURCES, RESTORE_COMPLETE, RESTORE_CANCEL_REQUESTED, RESTORE_CANCELED } Phase;
struct TbSetupRestore {
    Phase phase;
    bool active, awaiting_stop;
    int pane;
    size_t tab;
    uint64_t deadline, failures, acknowledged;
};
TbSetupRestore *tb_setup_restore_create(void) { return calloc(1, sizeof(TbSetupRestore)); }
void tb_setup_restore_destroy(TbSetupRestore *r) { free(r); }
void tb_setup_restore_begin(TbSetupRestore *r) { *r = (TbSetupRestore){.phase = RESTORE_CONNECTION, .active = true, .tab = 1}; }
bool tb_setup_restore_active(const TbSetupRestore *r) { return r && r->active; }
bool tb_setup_restore_attempted(const TbSetupRestore *r) { return r && r->phase != RESTORE_NONE; }
bool tb_setup_restore_acknowledgment(const TbSetupRestore *r) { return r && r->failures && r->failures != r->acknowledged; }
void tb_setup_restore_acknowledge(TbSetupRestore *r) { if (r) { r->acknowledged = r->failures; } }
const char *tb_setup_restore_status(const TbSetupRestore *r)
{
    if (!r || r->phase == RESTORE_NONE) { return "No saved setup restoration attempted."; }
    if (r->phase == RESTORE_CANCEL_REQUESTED) { return "Cancel requested; waiting for the in-flight operation to settle."; }
    if (r->phase == RESTORE_CANCELED) { return "Restoration canceled. Applied definitions remain; unresolved definitions are retained."; }
    if (r->awaiting_stop) { return "Awaiting stop; local definitions restored. No halt or resume requested."; }
    switch (r->phase) {
    case RESTORE_CONNECTION: return "Local definitions restored; waiting for connection/symbol initialization.";
    case RESTORE_SETTINGS: return "Waiting for existing source-setting restoration.";
    case RESTORE_CATALOG: return "Resolving saved Source identities through the current GDB catalog.";
    case RESTORE_SOURCES: return "Reopening saved Source entries, one per pump.";
    case RESTORE_COMPLETE: return "Local/Source restoration settled. Review unresolved entries; code-breakpoint installation remains pending.";
    default: return "Restoration state unavailable.";
    }
}
void tb_setup_restore_cancel(TbSetupRestore *r, TbSession *s)
{
    if (!r || r->phase == RESTORE_NONE || r->phase == RESTORE_COMPLETE || r->phase == RESTORE_CANCELED) { return; }
    r->phase = RESTORE_CANCEL_REQUESTED; r->active = true;
    tb_code_cancel_settings(tb_session_code(s)); tb_session_setup_interlock(s, true, false);
}
static uint64_t failed_enabled(const TbSetupCapture *c)
{
    uint64_t hash = 1469598103934665603ULL; bool any = false;
    for (size_t i = 0; i < tb_setup_capture_retained(c); ++i) {
        const TbSetupBreak *b = tb_setup_capture_retained_break(c, i); if (!b->enabled) { continue; }
        any = true;
        for (unsigned byte = 0; byte < 8; ++byte) { hash ^= (b->id >> (byte * 8)) & 255U; hash *= 1099511628211ULL; }
    }
    return any ? hash ? hash : 1 : 0;
}
void tb_setup_restore_pump(TbSetupRestore *r, TbSetupCapture *c, TbSession *s)
{
    if (!r || r->phase == RESTORE_NONE) { return; }
    tb_break_definition_reserve(tb_session_breakpoints(s), tb_setup_capture_retained(c));
    TbCode *code = tb_session_code(s); TbSessionState state = tb_session_state(s);
    bool running = state == TB_SESSION_RUNNING;
    if (r->phase == RESTORE_CANCEL_REQUESTED) {
        if (!tb_code_catalog_pending(code) && !tb_code_settings_busy(code)) { r->phase = RESTORE_CANCELED; r->active = false; }
    } else if (r->phase != RESTORE_CANCELED && r->phase != RESTORE_COMPLETE) {
        r->awaiting_stop = running; r->active = !running;
        if (state == TB_SESSION_CLOSING || state == TB_SESSION_CLOSED || state == TB_SESSION_FAILED || state == TB_SESSION_EXITED) {
            r->phase = RESTORE_CANCELED; r->active = false;
        } else if (!running && state != TB_SESSION_STARTING && state != TB_SESSION_IDLE) {
            if (r->phase == RESTORE_CONNECTION) { r->phase = RESTORE_SETTINGS; }
            if (r->phase == RESTORE_SETTINGS && !tb_code_settings_busy(code)) {
                tb_code_request_files(code); r->deadline = tb_gdb_now_ms() + 4000; r->phase = RESTORE_CATALOG;
            } else if (r->phase == RESTORE_CATALOG && (tb_code_files_ready(code) || tb_gdb_now_ms() >= r->deadline)) {
                /* A timed-out catalog still settles its in-flight request before
                 * the snapshot or execution interlock can be released. */
                if (!tb_code_catalog_pending(code)) { r->phase = RESTORE_SOURCES; }
            } else if (r->phase == RESTORE_SOURCES) {
                while (r->pane < TB_MAX_PANES && r->tab >= tb_code_tab_count(code, r->pane)) { ++r->pane; r->tab = 1; }
                if (r->pane == TB_MAX_PANES) { r->phase = RESTORE_COMPLETE; r->active = false; }
                else { (void)tb_code_tab_retry(code, r->pane, r->tab++); }
            }
        }
    }
    if (!running && !r->active) { r->failures = failed_enabled(c); }
    tb_session_setup_interlock(s, r->active, tb_setup_restore_acknowledgment(r));
}
