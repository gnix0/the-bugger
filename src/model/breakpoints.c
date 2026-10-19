#include "breakpoints.h"
#include "memory.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { BREAK_NONE, BREAK_LIST, BREAK_CHANGE, BREAK_RUN_INSERT, BREAK_RUN_CONTINUE, BREAK_RUN_CLEAN, BREAK_WATCH_INSERT, BREAK_WATCH_CLEAN } BreakJob;
enum { BREAK_COOKIE = 8100 };
struct TbBreakpoints {
    TbGdb *gdb;
    TbBreakpoint rows[TB_BREAK_ROWS]; size_t count;
    bool dirty, waiting, ready, cleanup, continue_run, cleanup_failed, recover_insert, limited;
    BreakJob job;
    uint64_t revision, request_token;
    char temporary[32], thread[32], message[192];
    TbGdbGeneration run_generation;
    char run_location[8193];
    char baseline[TB_BREAK_ROWS][32]; size_t baseline_count;
    bool watch_pending, watch_recover, watch_reject, watch_failed;
    char watch_id[32], watch_expression[512];
    TbWatchMode watch_mode;
    TbGdbGeneration watch_generation;
    char managed[TB_BREAK_ROWS][32]; size_t managed_count;
};
static void message(TbBreakpoints *b, const char *text)
{
    size_t out = 0;
    for (size_t i = 0; text[i] && out + 1 < sizeof b->message; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 32 && c < 127) { b->message[out++] = (char)c; }
        else if (out + 4 < sizeof b->message) {
            static const char hex[] = "0123456789abcdef";
            b->message[out++] = '\\'; b->message[out++] = 'x'; b->message[out++] = hex[c / 16]; b->message[out++] = hex[c % 16];
        }
    }
    b->message[out] = 0; ++b->revision;
}
static void copy(char *out, size_t capacity, const TbMiValue *v)
{
    out[0] = 0;
    if (!v || v->type != TB_MI_STRING || v->string.length >= capacity || memchr(v->string.data, 0, v->string.length)) { return; }
    memcpy(out, v->string.data, v->string.length); out[v->string.length] = 0;
}
static uint64_t number(const TbMiValue *v)
{
    char text[32]; copy(text, sizeof text, v); uint64_t n = 0;
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '9' || n > (UINT64_MAX - (unsigned)(text[i] - '0')) / 10) { return 0; }
        n = n * 10 + (unsigned)(text[i] - '0');
    }
    return n;
}
static bool id_valid(const char *id)
{
    if (!id || !*id || strlen(id) >= 32) { return false; }
    bool dot = false;
    for (size_t i = 0; id[i]; ++i) {
        if (id[i] == '.' && i && id[i + 1] && !dot) { dot = true; }
        else if (id[i] < '0' || id[i] > '9') { return false; }
    }
    return true;
}
static bool same_address(const char *left, const char *right)
{
    if (!left || !right || strncmp(left, "0x", 2) != 0 || strncmp(right, "0x", 2) != 0 || !left[2] || !right[2]) { return false; }
    char *end; errno = 0; unsigned long long a = strtoull(left + 2, &end, 16);
    if (errno || *end) { return false; }
    errno = 0; unsigned long long b = strtoull(right + 2, &end, 16);
    return !errno && !*end && a == b;
}
TbBreakpoints *tb_break_create(TbGdb *gdb)
{
    TbBreakpoints *b = calloc(1, sizeof *b); if (b) { b->gdb = gdb; b->dirty = true; } return b;
}
void tb_break_destroy(TbBreakpoints *b) { free(b); }
void tb_break_reset(TbBreakpoints *b)
{
    TbGdb *gdb = b->gdb; uint64_t revision = b->revision;
    memset(b, 0, sizeof *b); b->gdb = gdb; b->revision = revision + 1; b->dirty = true;
}
void tb_break_refresh(TbBreakpoints *b)
{
    b->dirty = true;
    if (b->watch_failed && !b->waiting) { b->watch_failed = false; }
    if (b->cleanup_failed && !b->waiting) { b->cleanup_failed = false; b->cleanup = *b->temporary != 0; }
}
bool tb_break_busy(const TbBreakpoints *b) { return b->watch_pending || (b->waiting && b->job != BREAK_LIST) || b->cleanup || b->continue_run || b->cleanup_failed || b->recover_insert; }
size_t tb_break_count(const TbBreakpoints *b) { return b->count; }
const TbBreakpoint *tb_break_row(const TbBreakpoints *b, size_t row) { return row < b->count ? &b->rows[row] : NULL; }
uint64_t tb_break_revision(const TbBreakpoints *b) { return b->revision; }
const char *tb_break_message(const TbBreakpoints *b) { return b->message; }
bool tb_break_ready(const TbBreakpoints *b) { return b->ready && !b->dirty && !b->waiting; }
static bool issue(TbBreakpoints *b, BreakJob job, const char *operation, const char *const args[], size_t count)
{
    b->job = job;
    if (job != BREAK_LIST) { b->message[0] = 0; ++b->revision; }
    b->waiting = tb_gdb_request(b->gdb, operation, args, count, TB_GDB_SESSION, BREAK_COOKIE + (uint64_t)job, 3000, &b->request_token);
    if (!b->waiting) { b->job = BREAK_NONE; message(b, "Breakpoint request unavailable; retry while stopped."); }
    return b->waiting;
}
static bool allowed(TbBreakpoints *b)
{
    TbGdbState state = tb_gdb_state(b->gdb);
    if (b->waiting || tb_break_busy(b) || (state != TB_GDB_READY && state != TB_GDB_STOPPED)) {
        message(b, "Breakpoint operation needs an idle, loaded/stopped debugger."); return false;
    }
    return true;
}
static bool location_valid(TbBreakLocation location)
{
    return location.source ? *location.source && strlen(location.source) < 4096 && location.line &&
                             !strchr(location.source, '\r') && !strchr(location.source, '\n') :
                             location.text && *location.text && strlen(location.text) <= 8192;
}
static size_t location_args(TbBreakLocation location, const char *args[], size_t n, char line[24])
{
    if (location.source) {
        (void)snprintf(line, 24, "%u", location.line);
        args[n++] = "--source"; args[n++] = location.source; args[n++] = "--line"; args[n++] = line;
    } else { args[n++] = location.text; }
    return n;
}
bool tb_break_add_at(TbBreakpoints *b, TbBreakLocation location, bool hardware)
{
    if (!allowed(b) || !location_valid(location)) { return false; }
    const char *args[6]; char line[24]; size_t n = 0; args[n++] = "-f";
    if (hardware) { args[n++] = "-h"; }
    n = location_args(location, args, n, line); return issue(b, BREAK_CHANGE, "-break-insert", args, n);
}
bool tb_break_add(TbBreakpoints *b, const char *location, bool hardware)
{
    return tb_break_add_at(b, (TbBreakLocation){.text = location}, hardware);
}
const char *tb_break_watch_mode(TbWatchMode mode)
{
    return mode == TB_WATCH_CHANGE ? "Value change" : mode == TB_WATCH_READ ? "Read" : mode == TB_WATCH_ACCESS ? "Read/write access" : "Unknown";
}
static bool watch_insert(TbBreakpoints *b, const char *expression, TbWatchMode mode)
{
    if (!allowed(b)) { return false; }
    if (mode < 0 || mode >= TB_WATCH_MODES || tb_gdb_state(b->gdb) != TB_GDB_STOPPED ||
        !tb_break_ready(b) || b->limited) {
        message(b, "Data watchpoints need a stopped target and complete fresh breakpoint table."); return false;
    }
    b->baseline_count = b->count;
    for (size_t i = 0; i < b->count; ++i) { (void)snprintf(b->baseline[i], sizeof b->baseline[i], "%s", b->rows[i].id); }
    b->watch_generation = tb_gdb_generation(b->gdb); b->watch_mode = mode;
    (void)snprintf(b->watch_expression, sizeof b->watch_expression, "%s", expression);
    b->watch_id[0] = 0; b->watch_recover = b->watch_reject = b->watch_failed = false;
    const char *args[4] = {"--language", "c"}; size_t count = 2;
    if (mode != TB_WATCH_CHANGE) { args[count++] = mode == TB_WATCH_READ ? "-r" : "-a"; }
    args[count++] = b->watch_expression;
    b->watch_pending = issue(b, BREAK_WATCH_INSERT, "-break-watch", args, count);
    return b->watch_pending;
}
bool tb_break_watch(TbBreakpoints *b, const char *expression, TbWatchMode mode)
{
    if (!tb_memory_expression(expression)) {
        message(b, "Use a name, member, constant index or simple offset; calls and mutations are rejected."); return false;
    }
    return watch_insert(b, expression, mode);
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Existing address/validated byte-width/watch-mode API; width and mode are checked independently.
bool tb_break_watch_address(TbBreakpoints *b, uint64_t address, unsigned bytes, TbWatchMode mode)
{
    const char *type = bytes == 1 ? "unsigned char" : bytes == 2 ? "unsigned short" : bytes == 4 ? "unsigned int" : bytes == 8 ? "unsigned long long" : NULL;
    if (!type || address > UINT64_MAX - (bytes - 1)) {
        message(b, "Watch width must be 1, 2, 4 or 8 bytes without address overflow."); return false;
    }
    char expression[128]; (void)snprintf(expression, sizeof expression, "*(%s *)0x%llx", type, (unsigned long long)address);
    return watch_insert(b, expression, mode);
}
static bool by_id(TbBreakpoints *b, const char *id)
{
    if (!allowed(b) || !id_valid(id) || (*b->temporary && strcmp(id, b->temporary) == 0)) { return false; }
    for (size_t i = 0; i < b->count; ++i) { if (strcmp(id, b->rows[i].id) == 0) { return true; } }
    message(b, "Breakpoint no longer exists; refresh and select again."); return false;
}
bool tb_break_remove(TbBreakpoints *b, const char *id)
{
    if (!by_id(b, id)) { return false; } const char *args[] = {id};
    return issue(b, BREAK_CHANGE, "-break-delete", args, 1);
}
bool tb_break_enable(TbBreakpoints *b, const char *id, bool enabled)
{
    if (!by_id(b, id)) { return false; } const char *args[] = {id};
    const TbBreakpoint *row = NULL;
    for (size_t i = 0; i < b->count; ++i) { if (strcmp(b->rows[i].id, id) == 0) { row = &b->rows[i]; break; } }
    if (enabled && row && row->watchpoint && !row->hardware) {
        message(b, "Software data watchpoints cannot be enabled here; hardware support is required."); return false;
    }
    bool okay = issue(b, BREAK_CHANGE, enabled ? "-break-enable" : "-break-disable", args, 1);
    if (okay && enabled && row && row->watchpoint) {
        b->watch_pending = true; b->watch_recover = b->watch_reject = b->watch_failed = false;
        b->baseline_count = 0; b->watch_mode = row->watch_mode; b->watch_generation = tb_gdb_generation(b->gdb);
        (void)snprintf(b->watch_id, sizeof b->watch_id, "%s", id);
        (void)snprintf(b->watch_expression, sizeof b->watch_expression, "%s", row->location);
    }
    return okay;
}
bool tb_break_condition(TbBreakpoints *b, const char *id, const char *condition)
{
    if (!by_id(b, id) || !condition || strlen(condition) >= 512) { return false; }
    const char *args[] = {id, condition}; return issue(b, BREAK_CHANGE, "-break-condition", args, *condition ? 2 : 1);
}
bool tb_break_ignore(TbBreakpoints *b, const char *id, const char *count)
{
    if (!by_id(b, id) || !count || !*count) { return false; }
    uint64_t value = 0;
    for (size_t i = 0; count[i]; ++i) {
        if (count[i] < '0' || count[i] > '9' || value > (UINT32_MAX - (unsigned)(count[i] - '0')) / 10) {
            message(b, "Ignore count must be an unsigned 32-bit decimal number."); return false;
        }
        value = value * 10 + (unsigned)(count[i] - '0');
    }
    const char *args[] = {id, count}; return issue(b, BREAK_CHANGE, "-break-after", args, 2);
}
bool tb_break_run_at(TbBreakpoints *b, TbBreakLocation location, const char *thread)
{
    if (!allowed(b) || tb_gdb_state(b->gdb) != TB_GDB_STOPPED || *b->temporary ||
        !location_valid(location) || !id_valid(thread) || strchr(thread, '.')) { return false; }
    if (!tb_break_ready(b) || b->limited) { message(b, "Run to Selected needs a complete fresh breakpoint table."); return false; }
    b->baseline_count = b->count;
    for (size_t i = 0; i < b->count; ++i) { (void)snprintf(b->baseline[i], sizeof b->baseline[i], "%s", b->rows[i].id); }
    (void)snprintf(b->thread, sizeof b->thread, "%s", thread);
    b->run_generation = tb_gdb_generation(b->gdb);
    if (location.source) { (void)snprintf(b->run_location, sizeof b->run_location, "-source %s -line %u", location.source, location.line); }
    else { (void)snprintf(b->run_location, sizeof b->run_location, "%s", location.text); }
    const char *args[7] = {"-t", "-p", b->thread}; char line[24];
    size_t count = location_args(location, args, 3, line);
    return issue(b, BREAK_RUN_INSERT, "-break-insert", args, count);
}
bool tb_break_run_to(TbBreakpoints *b, const char *location, const char *thread)
{
    return tb_break_run_at(b, (TbBreakLocation){.text = location}, thread);
}
static void parse_row(TbBreakpoints *b, const TbMiValue *tuple, const TbBreakpoint *parent)
{
    if (!tuple || tuple->type != TB_MI_TUPLE) { return; }
    TbBreakpoint row = {0}; const TbMiValue *fields = tuple->children;
    copy(row.id, sizeof row.id, tb_mi_find(fields, "number")); if (!id_valid(row.id)) { return; }
    for (size_t i = 0; i < b->count; ++i) { if (strcmp(b->rows[i].id, row.id) == 0) { return; } }
    const char *dot = strchr(row.id, '.');
    if (!parent && dot) {
        for (size_t i = 0; i < b->count; ++i) {
            if (strlen(b->rows[i].id) == (size_t)(dot - row.id) && strncmp(b->rows[i].id, row.id, (size_t)(dot - row.id)) == 0) { parent = &b->rows[i]; break; }
        }
    }
    copy(row.type, sizeof row.type, tb_mi_find(fields, "type"));
    copy(row.address, sizeof row.address, tb_mi_find(fields, "addr"));
    copy(row.file, sizeof row.file, tb_mi_find(fields, "fullname"));
    if (!*row.file) { copy(row.file, sizeof row.file, tb_mi_find(fields, "file")); }
    copy(row.location, sizeof row.location, tb_mi_find(fields, "original-location"));
    if (!*row.location) { copy(row.location, sizeof row.location, tb_mi_find(fields, "exp")); }
    if (!*row.location) { copy(row.location, sizeof row.location, tb_mi_find(fields, "what")); }
    copy(row.condition, sizeof row.condition, tb_mi_find(fields, "cond"));
    char enabled[8], disp[16]; copy(enabled, sizeof enabled, tb_mi_find(fields, "enabled")); copy(disp, sizeof disp, tb_mi_find(fields, "disp"));
    row.enabled = strcmp(enabled, "y") == 0;
    row.line = (unsigned)number(tb_mi_find(fields, "line")); row.hits = number(tb_mi_find(fields, "times"));
    row.ignore = (unsigned)number(tb_mi_find(fields, "ignore"));
    row.pending = strcmp(row.address, "<PENDING>") == 0 || tb_mi_find(fields, "pending") != NULL;
    row.multiple = strcmp(row.address, "<MULTIPLE>") == 0;
    row.watchpoint = strstr(row.type, "watchpoint") != NULL;
    row.watch_mode = strcmp(row.type, "read watchpoint") == 0 ? TB_WATCH_READ : strcmp(row.type, "acc watchpoint") == 0 ? TB_WATCH_ACCESS : TB_WATCH_CHANGE;
    row.hardware = strstr(row.type, "hw breakpoint") != NULL || strcmp(row.type, "hw watchpoint") == 0 ||
                   strcmp(row.type, "read watchpoint") == 0 || strcmp(row.type, "acc watchpoint") == 0;
    row.temporary = strcmp(disp, "del") == 0;
    if (parent) {
        row.child = true; (void)snprintf(row.parent, sizeof row.parent, "%s", parent->id);
        row.hardware = parent->hardware; row.temporary = parent->temporary;
        row.enabled = row.enabled && parent->enabled;
    }
    if (strcmp(row.id, b->temporary) == 0) { return; }
    if (b->count == TB_BREAK_ROWS) { b->limited = true; message(b, "Breakpoint display limited to 256 entries; backend breakpoints preserved."); return; }
    b->rows[b->count++] = row;
    const TbMiValue *locations = tb_mi_find(fields, "locations");
    if (!parent && locations && locations->type == TB_MI_LIST) {
        for (const TbMiValue *v = locations->children; v; v = v->next) { parse_row(b, v, &row); }
    }
}
static bool new_id(const TbBreakpoints *b, const char *id)
{
    if (!id_valid(id) || strchr(id, '.')) { return false; }
    for (size_t i = 0; i < b->baseline_count; ++i) { if (strcmp(id, b->baseline[i]) == 0) { return false; } }
    return true;
}
static void watch_verify(TbBreakpoints *b)
{
    const TbBreakpoint *found = NULL; unsigned candidates = 0;
    for (size_t i = 0; i < b->count; ++i) {
        const TbBreakpoint *row = &b->rows[i];
        if (*b->watch_id) {
            if (strcmp(row->id, b->watch_id) == 0 && new_id(b, row->id)) { found = row; ++candidates; }
        } else if (row->watchpoint && new_id(b, row->id)) {
            ++candidates;
            if (strcmp(row->location, b->watch_expression) == 0) { found = row; }
        }
    }
    if (b->limited || candidates > 1 || (candidates && !found)) {
        b->watch_failed = true;
        message(b, "Ambiguous data-watchpoint ownership; execution blocked. Close the session safely."); return;
    }
    if (!found) {
        b->watch_pending = b->watch_recover = b->watch_reject = false; b->watch_id[0] = 0;
        if (!*b->message) { message(b, "Data watchpoint absent/expired; no watchpoint installed."); }
        return;
    }
    (void)snprintf(b->watch_id, sizeof b->watch_id, "%s", found->id);
    TbGdbGeneration now = tb_gdb_generation(b->gdb);
    bool context_changed = now.session != b->watch_generation.session || now.stop != b->watch_generation.stop || now.context != b->watch_generation.context;
    if (b->watch_recover || context_changed || !found->watchpoint || !found->hardware || found->watch_mode != b->watch_mode) {
        b->watch_reject = true;
        message(b, "Data watchpoint unconfirmed or software fallback; removing owned entry before execution.");
    } else {
        bool tracked = false;
        for (size_t i = 0; i < b->managed_count; ++i) { tracked = tracked || strcmp(b->managed[i], found->id) == 0; }
        if (!tracked && b->managed_count < TB_BREAK_ROWS) {
            (void)snprintf(b->managed[b->managed_count++], sizeof b->managed[0], "%s", found->id);
        }
        b->watch_pending = false; b->watch_id[0] = 0;
        message(b, "Hardware data watchpoint verified. Width/slot insertion can still fail on resume; inspect GDB errors.");
    }
}
bool tb_break_event(TbBreakpoints *b, const TbGdbEvent *event)
{
    const TbMiRecord *record = event->record;
    if (event->kind == TB_GDB_RECONCILED) { b->dirty = true; b->cleanup = *b->temporary != 0; b->continue_run = false; }
    if (event->kind == TB_GDB_RECORD && record) {
        if (record->kind == TB_MI_NOTIFY && strncmp(record->klass, "breakpoint-", 11) == 0) {
            b->dirty = true;
            if (b->watch_pending && b->job == BREAK_WATCH_INSERT && strcmp(record->klass, "breakpoint-created") == 0) {
                const TbMiValue *bp = tb_mi_find(record->results, "bkpt");
                char expression[512], id[32];
                copy(expression, sizeof expression, bp ? tb_mi_find(bp->children, "exp") : NULL);
                if (!*expression) { copy(expression, sizeof expression, bp ? tb_mi_find(bp->children, "original-location") : NULL); }
                copy(id, sizeof id, bp ? tb_mi_find(bp->children, "number") : NULL);
                if (strcmp(expression, b->watch_expression) == 0 && new_id(b, id)) { (void)snprintf(b->watch_id, sizeof b->watch_id, "%s", id); }
            }
            if (b->waiting && b->job == BREAK_RUN_INSERT && strcmp(record->klass, "breakpoint-created") == 0) {
                const TbMiValue *bp = tb_mi_find(record->results, "bkpt");
                char location[8193], disp[16];
                copy(location, sizeof location, bp ? tb_mi_find(bp->children, "original-location") : NULL);
                copy(disp, sizeof disp, bp ? tb_mi_find(bp->children, "disp") : NULL);
                if (bp && strcmp(location, b->run_location) == 0 && strcmp(disp, "del") == 0) {
                    copy(b->temporary, sizeof b->temporary, tb_mi_find(bp->children, "number"));
                    if (!id_valid(b->temporary)) { b->temporary[0] = 0; }
                }
            }
            if (strcmp(record->klass, "breakpoint-deleted") == 0) {
                char id[32]; copy(id, sizeof id, tb_mi_find(record->results, "id"));
                if (*b->temporary && strcmp(id, b->temporary) == 0) { b->temporary[0] = 0; b->cleanup = b->cleanup_failed = false; }
            }
            ++b->revision;
        }
        if (record->kind == TB_MI_EXEC && strcmp(record->klass, "stopped") == 0) {
            b->dirty = true; b->cleanup = *b->temporary != 0; b->continue_run = false;
            char reason[64]; copy(reason, sizeof reason, tb_mi_find(record->results, "reason"));
            if (strstr(reason, "watchpoint")) {
                char id[32], old[64], value[64], summary[192];
                const TbMiValue *wp = tb_mi_find(record->results, "wpt");
                if (!wp) { wp = tb_mi_find(record->results, "hw-rwpt"); }
                if (!wp) { wp = tb_mi_find(record->results, "hw-awpt"); }
                copy(id, sizeof id, wp ? tb_mi_find(wp->children, "number") : tb_mi_find(record->results, "wpnum"));
                const TbMiValue *values = tb_mi_find(record->results, "value");
                copy(old, sizeof old, values ? tb_mi_find(values->children, "old") : NULL);
                copy(value, sizeof value, values ? tb_mi_find(values->children, "new") : NULL);
                if (!*value) { copy(value, sizeof value, values ? tb_mi_find(values->children, "value") : NULL); }
                (void)snprintf(summary, sizeof summary, "%.40s #%s | %.48s%s%.48s", reason, id, old, *old ? " -> " : "", value);
                message(b, summary);
            }
        }
    }
    if (event->cookie <= BREAK_COOKIE || event->cookie > BREAK_COOKIE + BREAK_WATCH_CLEAN ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    if (!b->waiting || event->token != b->request_token) { return true; }
    b->waiting = false; BreakJob job = (BreakJob)(event->cookie - BREAK_COOKIE); b->job = BREAK_NONE;
    bool okay = event->kind == TB_GDB_REPLY && record && (strcmp(record->klass, "done") == 0 || strcmp(record->klass, "running") == 0);
    if (!okay) {
        char error[192]; copy(error, sizeof error, record ? tb_mi_find(record->results, "msg") : NULL);
        if (!*error) { (void)snprintf(error, sizeof error, "Breakpoint operation cancelled/timed out; refresh before retrying."); }
        message(b, error); b->dirty = true;
        if (job == BREAK_WATCH_INSERT) { b->watch_recover = true; }
        if (job == BREAK_WATCH_CLEAN) {
            b->watch_failed = true;
            message(b, "Data-watchpoint cleanup failed; execution blocked. Manage > Refresh retries; close also releases the session.");
        }
        if (job == BREAK_RUN_CONTINUE) { b->cleanup = *b->temporary != 0; }
        if (job == BREAK_RUN_INSERT) { b->cleanup = *b->temporary != 0; b->recover_insert = !*b->temporary; b->continue_run = false; }
        if (job == BREAK_RUN_CLEAN) {
            b->cleanup = false; b->cleanup_failed = *b->temporary != 0;
            message(b, "Run-to cleanup failed; execution blocked. Manage > Refresh retries cleanup; closing also releases the session.");
        }
        return true;
    }
    if (job == BREAK_LIST) {
        const TbMiValue *table = tb_mi_find(record->results, "BreakpointTable");
        const TbMiValue *body = table ? tb_mi_find(table->children, "body") : NULL;
        if (!body || body->type != TB_MI_LIST) { b->ready = false; message(b, "Malformed breakpoint table; retained listing is unavailable."); return true; }
        if (b->recover_insert) {
            unsigned found = 0; bool uncertain = false; char recovered[32] = {0};
            for (const TbMiValue *v = body->children; v; v = v->next) {
                if (v->type != TB_MI_TUPLE) { continue; }
                char id[32], disp[16], location[8193];
                copy(id, sizeof id, tb_mi_find(v->children, "number")); copy(disp, sizeof disp, tb_mi_find(v->children, "disp"));
                copy(location, sizeof location, tb_mi_find(v->children, "original-location"));
                bool existing = false;
                for (size_t i = 0; i < b->baseline_count; ++i) { if (strcmp(b->baseline[i], id) == 0) { existing = true; break; } }
                if (!existing && strcmp(disp, "del") == 0) {
                    if (id_valid(id) && strcmp(location, b->run_location) == 0) {
                        ++found; (void)snprintf(recovered, sizeof recovered, "%s", id);
                    } else { uncertain = true; }
                }
            }
            b->recover_insert = false;
            if (found == 1 && !uncertain) { (void)snprintf(b->temporary, sizeof b->temporary, "%s", recovered); b->cleanup = true; }
            else if (found > 1 || uncertain) { b->recover_insert = b->cleanup_failed = true; message(b, "Ambiguous temporary breakpoint ownership; execution blocked. Close the session to release safely."); }
        }
        b->count = 0; b->limited = false;
        for (const TbMiValue *v = body->children; v; v = v->next) { parse_row(b, v, NULL); }
        b->ready = true;
        if (!b->watch_pending && !b->limited) {
            size_t keep = 0;
            for (size_t i = 0; i < b->managed_count; ++i) {
                for (size_t j = 0; j < b->count; ++j) {
                    const TbBreakpoint *row = &b->rows[j];
                    if (strcmp(row->id, b->managed[i]) != 0) { continue; }
                    if (keep != i) { memcpy(b->managed[keep], b->managed[i], sizeof b->managed[0]); }
                    ++keep;
                    if (row->watchpoint && !row->hardware && !b->watch_pending) {
                        (void)snprintf(b->watch_id, sizeof b->watch_id, "%s", row->id);
                        b->watch_pending = b->watch_reject = true;
                        message(b, "Managed watchpoint became software; removing fallback before execution.");
                    }
                    break;
                }
            }
            b->managed_count = keep;
        }
        if (b->watch_pending && !b->watch_reject && !b->watch_failed) { watch_verify(b); }
    } else if (job == BREAK_WATCH_INSERT) {
        const TbMiValue *wp = tb_mi_find(record->results, "wpt");
        if (!wp) { wp = tb_mi_find(record->results, "hw-rwpt"); }
        if (!wp) { wp = tb_mi_find(record->results, "hw-awpt"); }
        char id[32]; copy(id, sizeof id, wp ? tb_mi_find(wp->children, "number") : NULL);
        if (new_id(b, id)) { (void)snprintf(b->watch_id, sizeof b->watch_id, "%s", id); }
        else { b->watch_recover = true; }
        b->dirty = true;
    } else if (job == BREAK_WATCH_CLEAN) {
        b->watch_pending = b->watch_reject = b->watch_failed = b->watch_recover = false;
        b->watch_id[0] = 0; b->dirty = true;
        message(b, "Data watchpoint rejected and removed; no software fallback retained.");
    } else if (job == BREAK_RUN_INSERT) {
        const TbMiValue *bp = tb_mi_find(record->results, "bkpt");
        copy(b->temporary, sizeof b->temporary, bp ? tb_mi_find(bp->children, "number") : NULL);
        if (!id_valid(b->temporary)) { b->temporary[0] = 0; b->recover_insert = true; message(b, "Run-to breakpoint ID unavailable; reconciling before any resume."); }
        else if (b->run_generation.stop != tb_gdb_generation(b->gdb).stop || tb_gdb_state(b->gdb) != TB_GDB_STOPPED) { b->cleanup = true; }
        else { b->continue_run = true; }
        b->dirty = true;
    } else if (job == BREAK_RUN_CLEAN) { b->cleanup = b->cleanup_failed = false; b->temporary[0] = 0; b->dirty = true; }
    else if (job == BREAK_CHANGE) { b->dirty = true; message(b, "Breakpoint updated; refreshing backend state."); }
    ++b->revision; return true;
}
void tb_break_pump(TbBreakpoints *b)
{
    if (b->waiting) { return; }
    TbGdbState state = tb_gdb_state(b->gdb);
    if (state != TB_GDB_READY && state != TB_GDB_STOPPED) { return; }
    if (b->watch_pending && b->watch_reject && !b->watch_failed && *b->watch_id) {
        const char *args[] = {b->watch_id}; (void)issue(b, BREAK_WATCH_CLEAN, "-break-delete", args, 1); return;
    }
    if (b->cleanup && *b->temporary) {
        const char *args[] = {b->temporary}; (void)issue(b, BREAK_RUN_CLEAN, "-break-delete", args, 1); return;
    }
    if (b->continue_run) {
        b->continue_run = false;
        if (state != TB_GDB_STOPPED) { b->cleanup = true; return; }
        (void)issue(b, BREAK_RUN_CONTINUE, "-exec-continue", NULL, 0); return;
    }
    if (b->dirty) { b->dirty = false; (void)issue(b, BREAK_LIST, "-break-list", NULL, 0); }
}
const TbBreakpoint *tb_break_at(const TbBreakpoints *b, const char *path, unsigned line, const char *address)
{
    if (!b->ready) { return NULL; }
    for (size_t i = 0; i < b->count; ++i) {
        const TbBreakpoint *row = &b->rows[i];
        if (!strstr(row->type, "breakpoint") && !row->child) { continue; }
        if ((address && *address && same_address(address, row->address)) ||
            (path && *path && line && row->line == line && strcmp(path, row->file) == 0)) { return row; }
    }
    return NULL;
}
