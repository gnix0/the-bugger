#include "breakpoints.h"
#include "memory.h"
#include "images.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { BREAK_NONE, BREAK_LIST, BREAK_CHANGE, BREAK_RUN_INSERT, BREAK_RUN_CONTINUE, BREAK_RUN_CLEAN, BREAK_WATCH_INSERT, BREAK_WATCH_CLEAN, BREAK_RESTORE_INSERT, BREAK_RESTORE_CONDITION, BREAK_RESTORE_IGNORE, BREAK_RESTORE_VERIFY, BREAK_RESTORE_ENABLE, BREAK_RESTORE_RECONCILE, BREAK_INTENT_RECONCILE } BreakJob;
enum { BREAK_COOKIE = 8100 };
typedef enum { EDIT_NONE, EDIT_CREATE, EDIT_REMOVE, EDIT_ENABLE, EDIT_CONDITION, EDIT_IGNORE } DefinitionEdit;
typedef struct { TbBreakDefinition data; char backend[32]; bool conflict, mapping_checked; } ConfiguredBreak;
typedef enum { RESTORE_IDLE, RESTORE_RETRY, RESTORE_INSERT, RESTORE_CONDITION, RESTORE_IGNORE, RESTORE_VERIFY_DISABLED, RESTORE_ENABLE, RESTORE_VERIFY_ENABLED } RestoreStep;
typedef struct {
    TbBreakRestoration progress;
    TbSetupBreak definition;
} RestoredBreak;
struct TbBreakpoints {
    TbGdb *gdb; pid_t native_pid;
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
    ConfiguredBreak definitions[TB_BREAK_ROWS]; size_t definition_count, definition_reserved;
    TbBreakDefinition pending_definition;
    char edit_id[32]; DefinitionEdit edit;
    uint64_t definition_serial;
    bool definition_uncertain, interlock, listing_complete;
    RestoredBreak *restored; size_t restored_count, restoring;
    RestoreStep restore_step, uncertain_step;
    bool restore_cancel, restore_query, restore_uncertain, baseline_complete, creation_notified, baseline_queried;
    TbGdbGeneration restore_generation;
    DefinitionEdit uncertain_edit;
    TbBreakDefinition uncertain_definition;
    char uncertain_id[32];
};
static bool restore_pump(TbBreakpoints *b);
static bool restore_event(TbBreakpoints *b, BreakJob job, const TbGdbEvent *event, bool okay);
static void remember_baseline(TbBreakpoints *b);
static void forget_configured(TbBreakpoints *b, uint64_t identity);
static void parse_listing(TbBreakpoints *b, const TbMiRecord *record);
static bool definition_location(const TbBreakDefinition *d, const TbBreakpoint *row);
size_t tb_break_definition_count(const TbBreakpoints *b) { return b->definition_count; }
const TbBreakDefinition *tb_break_definition(const TbBreakpoints *b, size_t i)
{ return i < b->definition_count ? &b->definitions[i].data : NULL; }
bool tb_break_definitions_ready(const TbBreakpoints *b)
{
    if (b->definition_uncertain || b->edit != EDIT_NONE || tb_break_busy(b)) { return false; }
    TbGdbState state = tb_gdb_state(b->gdb);
    if ((state == TB_GDB_READY || state == TB_GDB_STOPPED) && (b->dirty || b->waiting)) { return false; }
    for (size_t i = 0; i < b->definition_count; ++i) { if (b->definitions[i].conflict) { return false; } }
    return true;
}
static size_t configured_index(const TbBreakpoints *b, const char *id)
{
    size_t i = 0; while (i < b->definition_count && strcmp(b->definitions[i].backend, id) != 0) { ++i; } return i;
}
static void edit_definition(TbBreakpoints *b, const char *id, DefinitionEdit edit)
{
    size_t i = configured_index(b, id);
    if (i == b->definition_count) { return; }
    b->pending_definition = b->definitions[i].data; b->edit = edit;
    (void)snprintf(b->edit_id, sizeof b->edit_id, "%s", id);
}
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
void tb_break_destroy(TbBreakpoints *b) { if (b) { free(b->restored); free(b); } }
void tb_break_reset(TbBreakpoints *b)
{
    TbGdb *gdb = b->gdb; uint64_t revision = b->revision;
    free(b->restored); memset(b, 0, sizeof *b); b->gdb = gdb; b->revision = revision + 1; b->dirty = true;
}
void tb_break_refresh(TbBreakpoints *b)
{
    b->dirty = true;
    if (b->watch_failed && !b->waiting) { b->watch_failed = false; }
    if (b->cleanup_failed && !b->waiting) { b->cleanup_failed = false; b->cleanup = *b->temporary != 0; }
}
bool tb_break_busy(const TbBreakpoints *b) { return b->definition_uncertain || b->restore_uncertain || (b->restore_step != RESTORE_IDLE && tb_gdb_state(b->gdb) != TB_GDB_RUNNING) || b->watch_pending || (b->waiting && b->job != BREAK_LIST) || b->cleanup || b->continue_run || b->cleanup_failed || b->recover_insert; }
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
void tb_break_interlock(TbBreakpoints *b, bool active) { b->interlock = active; }
void tb_break_definition_reserve(TbBreakpoints *b, size_t retained)
{ b->definition_reserved = retained <= TB_BREAK_ROWS ? retained : TB_BREAK_ROWS; }
static bool allowed(TbBreakpoints *b)
{
    if (b->interlock) { message(b, "Wait for setup restoration to settle."); return false; }
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
    if (b->definition_count + b->definition_reserved >= TB_BREAK_ROWS) { message(b, "At most 256 configured code breakpoints are supported."); return false; }
    remember_baseline(b);
    const char *args[6]; char line[24]; size_t n = 0; args[n++] = "-f";
    if (hardware) { args[n++] = "-h"; }
    n = location_args(location, args, n, line);
    if (!issue(b, BREAK_CHANGE, "-break-insert", args, n)) { return false; }
    b->pending_definition = (TbBreakDefinition){.enabled = true, .hardware = hardware, .line = location.source ? location.line : 0};
    if (location.source) { (void)snprintf(b->pending_definition.source, sizeof b->pending_definition.source, "%s", location.source); }
    else { (void)snprintf(b->pending_definition.location, sizeof b->pending_definition.location, "%s", location.text); }
    b->edit = EDIT_CREATE; b->edit_id[0] = 0; return true;
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
    if (!issue(b, BREAK_CHANGE, "-break-delete", args, 1)) { return false; }
    edit_definition(b, id, EDIT_REMOVE); return true;
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
    if (okay) { edit_definition(b, id, EDIT_ENABLE); b->pending_definition.enabled = enabled; }
    if (okay && enabled && row && row->watchpoint) {
        b->watch_pending = true; b->watch_recover = b->watch_reject = b->watch_failed = false;
        b->baseline_count = 0; b->watch_mode = row->watch_mode; b->watch_generation = tb_gdb_generation(b->gdb);
        (void)snprintf(b->watch_id, sizeof b->watch_id, "%s", id);
        (void)snprintf(b->watch_expression, sizeof b->watch_expression, "%.511s", row->location);
    }
    return okay;
}
bool tb_break_condition(TbBreakpoints *b, const char *id, const char *condition)
{
    if (!by_id(b, id) || !condition || strlen(condition) >= 512) { return false; }
    const char *args[] = {id, condition};
    if (!issue(b, BREAK_CHANGE, "-break-condition", args, *condition ? 2 : 1)) { return false; }
    edit_definition(b, id, EDIT_CONDITION);
    (void)snprintf(b->pending_definition.condition, sizeof b->pending_definition.condition, "%s", condition); return true;
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
    const char *args[] = {id, count};
    if (!issue(b, BREAK_CHANGE, "-break-after", args, 2)) { return false; }
    edit_definition(b, id, EDIT_IGNORE); b->pending_definition.ignore = (unsigned)value; b->pending_definition.ignore_set = true; return true;
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
static bool field_text(char *out, size_t size, const TbMiValue *value)
{
    copy(out, size, value);
    if (!value || value->type != TB_MI_STRING || value->string.length >= size || strlen(out) != value->string.length) { return false; }
    for (size_t i = 0; out[i]; ++i) { if ((unsigned char)out[i] < 32 || out[i] == 127) { return false; } }
    return true;
}
static bool field_unsigned(const TbMiValue *value, uint64_t maximum, uint64_t *out)
{
    if (!value || value->type != TB_MI_STRING || !value->string.length) { return false; }
    uint64_t n = 0;
    for (size_t i = 0; i < value->string.length; ++i) {
        unsigned char c = (unsigned char)value->string.data[i];
        if (c < '0' || c > '9' || (uint64_t)(c - '0') > maximum || n > (maximum - (c - '0')) / 10) { return false; }
        n = n * 10 + c - '0';
    }
    *out = n; return true;
}
static bool supported_fields(const TbMiValue *fields, char reason[192])
{
    static const char *const supported[] = {"number", "type", "disp", "enabled", "addr", "func", "file", "fullname", "line", "thread-groups", "times", "original-location", "cond", "ignore", "pending", "locations"};
    for (const TbMiValue *v = fields; v; v = v->next) {
        for (const TbMiValue *previous = fields; previous != v; previous = previous->next) {
            if (v->name && previous->name && !strcmp(v->name, previous->name)) { (void)snprintf(reason, 192, "Duplicate breakpoint attribute: %.120s", v->name); return false; }
        }
        bool known = false;
        for (size_t i = 0; i < sizeof supported / sizeof supported[0]; ++i) { known = known || strcmp(v->name ? v->name : "", supported[i]) == 0; }
        if (!known) { (void)snprintf(reason, 192, "Unsupported attribute/restriction: %.120s", v->name ? v->name : "unnamed"); return false; }
    }
    return true;
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
    uint64_t checked;
    row.definition_complete = !row.child && (strcmp(row.type, "breakpoint") == 0 || strcmp(row.type, "hw breakpoint") == 0) &&
        strcmp(disp, "keep") == 0 && (strcmp(enabled, "y") == 0 || strcmp(enabled, "n") == 0) &&
        field_text(row.location, sizeof row.location, tb_mi_find(fields, "original-location")) && *row.location &&
        (!tb_mi_find(fields, "cond") || field_text(row.condition, sizeof row.condition, tb_mi_find(fields, "cond"))) &&
        (!tb_mi_find(fields, "ignore") || field_unsigned(tb_mi_find(fields, "ignore"), UINT32_MAX, &checked)) &&
        supported_fields(fields, row.unsupported);
    if (row.definition_complete && row.location[0] == '-' && strncmp(row.location, "-source ", 8) != 0) {
        row.definition_complete = false; (void)snprintf(row.unsupported, sizeof row.unsupported, "Unsupported explicit original-location syntax; entire entry excluded.");
    }
    if (!row.definition_complete && !*row.unsupported) { (void)snprintf(row.unsupported, sizeof row.unsupported, "Incomplete or unsupported code-breakpoint definition."); }
    if (parent && (!supported_fields(fields, row.unsupported) || tb_mi_find(fields, "cond") || tb_mi_find(fields, "ignore") ||
        (strcmp(enabled, "y") != 0 && strcmp(enabled, "n") != 0) || (parent->enabled && strcmp(enabled, "y") != 0))) {
        if (!*row.unsupported) { (void)snprintf(row.unsupported, sizeof row.unsupported, "Unsupported or incomplete per-location attributes."); }
        for (size_t i = 0; i < b->count; ++i) {
            if (strcmp(b->rows[i].id, parent->id) == 0) { b->rows[i].definition_complete = false; memcpy(b->rows[i].unsupported, row.unsupported, sizeof row.unsupported); }
        }
    }
    b->rows[b->count++] = row;
    const TbMiValue *locations = tb_mi_find(fields, "locations");
    if (!parent && locations) {
        bool complete = locations->type == TB_MI_LIST;
        for (const TbMiValue *v = locations->children; complete && v; v = v->next) {
            size_t before = b->count; parse_row(b, v, &row); complete = b->count > before;
        }
        if (!complete) { b->listing_complete = false; }
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
static void definition_complete(TbBreakpoints *b, const TbMiRecord *record)
{
    if (b->edit == EDIT_NONE) { return; }
    size_t i = configured_index(b, b->edit_id);
    if (b->edit == EDIT_CREATE) {
        const TbMiValue *bp = tb_mi_find(record->results, "bkpt"); char id[32];
        copy(id, sizeof id, bp ? tb_mi_find(bp->children, "number") : NULL);
        if (!id_valid(id) || strchr(id, '.') || configured_index(b, id) != b->definition_count) { b->definition_uncertain = true; }
        else {
            ConfiguredBreak *d = &b->definitions[b->definition_count++];
            memset(d, 0, sizeof *d);
            d->data = b->pending_definition; d->data.identity = ++b->definition_serial;
            (void)snprintf(d->backend, sizeof d->backend, "%s", id);
        }
    } else if (i < b->definition_count) {
        if (b->edit == EDIT_REMOVE) {
            forget_configured(b, b->definitions[i].data.identity);
            memmove(&b->definitions[i], &b->definitions[i + 1], (b->definition_count - i - 1) * sizeof b->definitions[0]); --b->definition_count;
        } else { b->definitions[i].data = b->pending_definition; }
    }
    if (b->definition_uncertain) {
        b->restore_query = true; b->uncertain_edit = b->edit; b->uncertain_definition = b->pending_definition;
        (void)snprintf(b->uncertain_id, sizeof b->uncertain_id, "%s", b->edit_id);
    }
    b->edit = EDIT_NONE;
}
static void definition_observations(TbBreakpoints *b)
{
    for (size_t i = 0; i < b->definition_count; ++i) {
        ConfiguredBreak *d = &b->definitions[i]; const TbBreakpoint *row = NULL;
        for (size_t j = 0; j < b->count; ++j) { if (strcmp(d->backend, b->rows[j].id) == 0) { row = &b->rows[j]; break; } }
        d->conflict = !row || !row->definition_complete || !definition_location(&d->data, row) || row->watchpoint || row->temporary || row->enabled != d->data.enabled ||
            strcmp(row->condition, d->data.condition) != 0 || (d->data.hardware && !row->hardware) ||
            (row->ignore > (d->data.ignore_set ? d->data.ignore : 0));
        /* Declining remaining ignore counts and hits are observations, never
         * replacements for the explicitly configured count. */
    }
}
bool tb_break_event(TbBreakpoints *b, const TbGdbEvent *event)
{
    const TbMiRecord *record = event->record;
    if (event->kind == TB_GDB_RECONCILED) { b->dirty = true; b->cleanup = *b->temporary != 0; b->continue_run = false; }
    if (event->kind == TB_GDB_RECORD && record) {
        if (record->kind == TB_MI_NOTIFY && strncmp(record->klass, "breakpoint-", 11) == 0) {
            b->dirty = true;
            if (b->waiting && b->job == BREAK_RESTORE_INSERT && strcmp(record->klass, "breakpoint-created") == 0) { b->creation_notified = true; }
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
    if (event->cookie <= BREAK_COOKIE || event->cookie > BREAK_COOKIE + BREAK_INTENT_RECONCILE ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    if (!b->waiting || event->token != b->request_token) { return true; }
    b->waiting = false; BreakJob job = (BreakJob)(event->cookie - BREAK_COOKIE); b->job = BREAK_NONE;
    bool okay = event->kind == TB_GDB_REPLY && record && (strcmp(record->klass, "done") == 0 || strcmp(record->klass, "running") == 0);
    if (job >= BREAK_RESTORE_INSERT) { return restore_event(b, job, event, okay); }
    if (!okay) {
        if (b->edit != EDIT_NONE) {
            if (event->kind != TB_GDB_REPLY) {
                b->definition_uncertain = b->restore_query = true; b->uncertain_edit = b->edit;
                b->uncertain_definition = b->pending_definition;
                (void)snprintf(b->uncertain_id, sizeof b->uncertain_id, "%s", b->edit_id);
            }
            b->edit = EDIT_NONE;
        }
        char error[192]; copy(error, sizeof error, record ? tb_mi_find(record->results, "msg") : NULL);
        if (!*error) { (void)snprintf(error, sizeof error, "Breakpoint operation cancelled/timed out; refresh before retrying."); }
        message(b, error); b->dirty = job != BREAK_LIST;
        if (job == BREAK_LIST) { b->ready = b->listing_complete = false; }
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
        parse_listing(b, record);
        b->ready = true;
        definition_observations(b);
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
    else if (job == BREAK_CHANGE) { definition_complete(b, record); b->dirty = true; message(b, "Breakpoint updated; refreshing backend state."); }
    ++b->revision; return true;
}
void tb_break_native(TbBreakpoints *b, pid_t pid) { b->native_pid = pid; }
void tb_break_pump(TbBreakpoints *b)
{
    if (b->waiting) { return; }
    if (restore_pump(b)) { return; }
    TbGdbState state = tb_gdb_state(b->gdb);
    if (state != TB_GDB_READY && state != TB_GDB_STOPPED) { return; }
    if (state == TB_GDB_STOPPED && b->native_pid > 1) {
        for (size_t i = 0; i < b->definition_count; ++i) {
            TbBreakDefinition *d = &b->definitions[i].data;
            if (b->definitions[i].mapping_checked || *d->mapping.path || *d->source) { continue; }
            TbSetupBreak candidate = {0}; memcpy(candidate.location, d->location, sizeof candidate.location); uint64_t address;
            if (tb_break_literal(&candidate, &address) && address) {
                b->definitions[i].mapping_checked = true;
                (void)tb_images_native_mapping(b->native_pid, address, &d->mapping); break;
            }
        }
    }
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

static void parse_listing(TbBreakpoints *b, const TbMiRecord *record)
{
    const TbMiValue *table = tb_mi_find(record->results, "BreakpointTable");
    const TbMiValue *body = table ? tb_mi_find(table->children, "body") : NULL;
    uint64_t expected = 0; size_t roots = 0;
    b->count = 0; b->limited = false; b->listing_complete = body && body->type == TB_MI_LIST &&
        field_unsigned(table ? tb_mi_find(table->children, "nr_rows") : NULL, UINT32_MAX, &expected);
    if (!body || body->type != TB_MI_LIST) { b->listing_complete = false; b->ready = false; return; }
    for (const TbMiValue *v = body->children; v; v = v->next) {
        char id[32]; copy(id, sizeof id, v->type == TB_MI_TUPLE ? tb_mi_find(v->children, "number") : NULL);
        if (!id_valid(id)) { b->listing_complete = false; }
        if (!strchr(id, '.')) { ++roots; }
        size_t before = b->count; parse_row(b, v, NULL);
        if (b->count == before && strcmp(id, b->temporary) != 0) { b->listing_complete = false; }
    }
    b->listing_complete = b->listing_complete && !b->limited && roots == expected;
    b->ready = true; b->dirty = false;
}
static void remember_baseline(TbBreakpoints *b)
{
    b->baseline_count = 0; b->baseline_complete = b->ready && !b->dirty && b->listing_complete;
    for (size_t i = 0; i < b->count; ++i) {
        if (b->rows[i].child) { continue; }
        (void)snprintf(b->baseline[b->baseline_count++], 32, "%s", b->rows[i].id);
    }
}
bool tb_break_literal(const TbSetupBreak *d, uint64_t *address)
{
    if (*d->source) { return false; }
    const char *text = d->location; bool indirect = *text == '*'; if (indirect) { ++text; }
    while (*text == ' ') { ++text; }
    bool numeric = (*text >= '0' && *text <= '9');
    if (address) {
        char *end; errno = 0; unsigned long long n = strtoull(text, &end, 0);
        *address = !errno && *text && !*end ? (uint64_t)n : 0;
    }
    return indirect || (numeric && strncmp(text, "0x", 2) == 0);
}
const char *tb_break_restore_state(TbBreakRestoreState state)
{
    static const char *const names[] = {"pending", "awaiting stop", "address approval required", "creating disabled", "configuring disabled", "verifying attributes", "installed", "unresolved", "uncertain; execution blocked", "imported draft; not restored"};
    return state >= TB_RESTORE_PENDING && state <= TB_RESTORE_DRAFT ? names[state] : "unknown";
}
static RestoredBreak *restoration(TbBreakpoints *b, uint64_t id)
{
    for (size_t i = 0; i < b->restored_count; ++i) { if (b->restored[i].progress.id == id) { return &b->restored[i]; } }
    return NULL;
}
const TbBreakRestoration *tb_break_restoration(const TbBreakpoints *b, uint64_t id)
{
    for (size_t i = 0; i < b->restored_count; ++i) { if (b->restored[i].progress.id == id) { return &b->restored[i].progress; } }
    return NULL;
}
bool tb_break_restoration_active(const TbBreakpoints *b)
{
    return b->restore_uncertain || b->definition_uncertain ||
        (b->waiting && b->job >= BREAK_RESTORE_INSERT) ||
        (b->restore_step != RESTORE_IDLE && tb_gdb_state(b->gdb) != TB_GDB_RUNNING);
}
bool tb_break_restoration_uncertain(const TbBreakpoints *b) { return b->restore_uncertain || b->definition_uncertain; }
static void restore_finish(TbBreakpoints *b, TbBreakRestoreState state, const char *reason)
{
    RestoredBreak *r = &b->restored[b->restoring];
    r->progress.state = state; (void)snprintf(r->progress.reason, sizeof r->progress.reason, "%s", reason);
    b->restore_step = RESTORE_IDLE; b->restore_cancel = false; b->dirty = true; message(b, reason);
}
static void original_location(const TbSetupBreak *d, char out[TB_SETUP_LOCATION])
{
    if (*d->source) { (void)snprintf(out, TB_SETUP_LOCATION, "-source %s -line %u", d->source, d->line); }
    else { (void)snprintf(out, TB_SETUP_LOCATION, "%s", d->location); }
}
static bool location_matches(const TbSetupBreak *d, const TbBreakpoint *row)
{
    char original[TB_SETUP_LOCATION]; original_location(d, original); return strcmp(original, row->location) == 0;
}
static TbBreakDefinition configured(const TbSetupBreak *d)
{
    TbBreakDefinition out = {.mapping = d->mapping, .enabled = d->enabled, .ignore_set = d->ignore_set, .ignore = d->ignore, .hardware = d->hardware, .line = d->line};
    memcpy(out.location, d->location, sizeof out.location); memcpy(out.source, d->source, sizeof out.source); memcpy(out.condition, d->condition, sizeof out.condition); return out;
}
static const TbBreakpoint *row_id(const TbBreakpoints *b, const char *id)
{
    for (size_t i = 0; i < b->count; ++i) { if (!b->rows[i].child && strcmp(b->rows[i].id, id) == 0) { return &b->rows[i]; } } return NULL;
}
static bool attributes(const TbSetupBreak *d, const TbBreakpoint *row, bool enabled)
{
    return row && row->definition_complete && location_matches(d, row) && row->enabled == enabled &&
        (!d->hardware || row->hardware) && strcmp(row->condition, d->condition) == 0 && row->ignore == (d->ignore_set ? d->ignore : 0);
}
static void restore_install(TbBreakpoints *b)
{
    RestoredBreak *r = &b->restored[b->restoring];
    size_t i = configured_index(b, r->progress.backend);
    if (i == b->definition_count) {
        if (i == TB_BREAK_ROWS) { restore_finish(b, TB_RESTORE_UNRESOLVED, "Configured breakpoint capacity exhausted; saved definition retained."); return; }
        ++b->definition_count; memset(&b->definitions[i], 0, sizeof b->definitions[i]);
        b->definitions[i].data.identity = ++b->definition_serial;
    }
    uint64_t identity = b->definitions[i].data.identity;
    b->definitions[i].data = configured(&r->definition); b->definitions[i].data.identity = identity;
    (void)snprintf(b->definitions[i].backend, sizeof b->definitions[i].backend, "%s", r->progress.backend);
    b->definitions[i].conflict = false; r->progress.identity = identity;
    restore_finish(b, TB_RESTORE_INSTALLED, r->definition.hardware ?
        "Required hardware breakpoint configuration verified; physical allocation is not guaranteed." : "Saved breakpoint configuration verified.");
}
bool tb_break_restore(TbBreakpoints *b, const TbSetupBreak *d, bool approved)
{
    if (!d || !d->id || b->restore_step != RESTORE_IDLE || tb_break_busy(b) || b->waiting) { return false; }
    RestoredBreak *r = restoration(b, d->id);
    if (r && (r->progress.state == TB_RESTORE_INSTALLED || r->progress.state == TB_RESTORE_UNCERTAIN)) { return false; }
    if (!r) {
        if (b->restored_count == TB_SETUP_BREAKS) { return false; }
        if (!b->restored) { b->restored = calloc(TB_SETUP_BREAKS, sizeof *b->restored); if (!b->restored) { return false; } }
        r = &b->restored[b->restored_count++];
    }
    r->definition = *d; r->progress.id = d->id; r->progress.reason[0] = 0;
    if (tb_break_literal(d, NULL) && !approved) {
        r->progress.state = TB_RESTORE_ADDRESS;
        (void)snprintf(r->progress.reason, sizeof r->progress.reason, "Literal address requires current-session approval or matching native mapping provenance."); return true;
    }
    b->restoring = (size_t)(r - b->restored); b->restore_cancel = b->restore_uncertain = b->creation_notified = b->baseline_queried = false;
    /* Reusing an existing disabled location starts with metadata verification;
     * no insertion is repeated after a partial configuration failure. */
    b->restore_step = *r->progress.backend ? RESTORE_RETRY : RESTORE_INSERT;
    r->progress.state = tb_gdb_state(b->gdb) == TB_GDB_RUNNING ? TB_RESTORE_AWAITING_STOP : TB_RESTORE_PENDING;
    remember_baseline(b); ++b->revision; return true;
}
bool tb_break_restore_draft(TbBreakpoints *b, uint64_t id, const char *backend)
{
    if (restoration(b, id) || b->restored_count == TB_SETUP_BREAKS || !id_valid(backend)) { return false; }
    if (!b->restored) { b->restored = calloc(TB_SETUP_BREAKS, sizeof *b->restored); if (!b->restored) { return false; } }
    RestoredBreak *r = &b->restored[b->restored_count++]; r->progress.id = id; r->progress.state = TB_RESTORE_DRAFT;
    (void)snprintf(r->progress.backend, sizeof r->progress.backend, "%s", backend); return true;
}
void tb_break_restore_cancel(TbBreakpoints *b)
{
    if (b->restore_step == RESTORE_IDLE && !b->restore_uncertain) { return; }
    b->restore_cancel = true;
    if (!b->waiting && !b->restore_uncertain && b->restore_step != RESTORE_VERIFY_ENABLED) { restore_finish(b, TB_RESTORE_UNRESOLVED, "Restoration canceled; configured location remains disabled and definition retained."); }
}
bool tb_break_reconcile(TbBreakpoints *b)
{
    if (b->waiting || (!b->restore_uncertain && !b->definition_uncertain)) { return false; }
    b->restore_query = true; return true;
}
static void restore_uncertain(TbBreakpoints *b, RestoreStep step)
{
    b->restore_uncertain = b->restore_query = true; b->uncertain_step = step;
    RestoredBreak *r = &b->restored[b->restoring]; r->progress.state = TB_RESTORE_UNCERTAIN;
    (void)snprintf(r->progress.reason, sizeof r->progress.reason, "Mutation outcome uncertain. One metadata query will reconcile; no mutation is repeated.");
    ++b->revision;
}
static bool restore_issue(TbBreakpoints *b, BreakJob job, const char *operation, const char *const args[], size_t count)
{
    b->restore_generation = tb_gdb_generation(b->gdb);
    if (issue(b, job, operation, args, count)) { return true; }
    restore_finish(b, TB_RESTORE_UNRESOLVED, "Restoration request was not queued; definition retained."); return false;
}
static bool restore_pump(TbBreakpoints *b)
{
    TbGdbState state = tb_gdb_state(b->gdb);
    if (b->restore_query && (b->restore_uncertain || b->definition_uncertain)) {
        if (state != TB_GDB_READY && state != TB_GDB_STOPPED && state != TB_GDB_RUNNING) { return true; }
        b->restore_query = false;
        (void)issue(b, b->definition_uncertain ? BREAK_INTENT_RECONCILE : BREAK_RESTORE_RECONCILE, "-break-list", NULL, 0); return true;
    }
    if (b->restore_uncertain || b->definition_uncertain) { return true; }
    if (b->restore_step == RESTORE_IDLE) { return false; }
    RestoredBreak *r = &b->restored[b->restoring];
    if (state == TB_GDB_RUNNING) { r->progress.state = TB_RESTORE_AWAITING_STOP; return true; }
    if (state != TB_GDB_READY && state != TB_GDB_STOPPED) { return true; }
    if (b->restore_cancel && b->restore_step != RESTORE_VERIFY_ENABLED) { restore_finish(b, TB_RESTORE_UNRESOLVED, "Restoration canceled; partial disabled location retained."); return true; }
    const char *args[8]; char line[24], ignore[24]; size_t count = 0;
    switch (b->restore_step) {
    case RESTORE_INSERT:
        if (!b->baseline_complete) {
            if (!b->baseline_queried) { b->baseline_queried = true; (void)issue(b, BREAK_LIST, "-break-list", NULL, 0); return true; }
            if (!b->ready || b->dirty || !b->listing_complete) { restore_finish(b, TB_RESTORE_UNRESOLVED, "A complete current breakpoint listing is required before restoration."); return true; }
            remember_baseline(b);
        }
        r->progress.state = TB_RESTORE_CREATING; args[count++] = "-d"; args[count++] = "-f";
        if (r->definition.hardware) { args[count++] = "-h"; }
        count = location_args((TbBreakLocation){.text = r->definition.location, .source = *r->definition.source ? r->definition.source : NULL, .line = r->definition.line}, args, count, line);
        (void)restore_issue(b, BREAK_RESTORE_INSERT, "-break-insert", args, count); break;
    case RESTORE_CONDITION:
        r->progress.state = TB_RESTORE_CONFIGURING; args[count++] = r->progress.backend;
        if (*r->definition.condition) { args[count++] = r->definition.condition; }
        (void)restore_issue(b, BREAK_RESTORE_CONDITION, "-break-condition", args, count); break;
    case RESTORE_IGNORE:
        r->progress.state = TB_RESTORE_CONFIGURING; args[count++] = r->progress.backend;
        (void)snprintf(ignore, sizeof ignore, "%u", r->definition.ignore_set ? r->definition.ignore : 0); args[count++] = ignore;
        (void)restore_issue(b, BREAK_RESTORE_IGNORE, "-break-after", args, count); break;
    case RESTORE_RETRY: case RESTORE_VERIFY_DISABLED: case RESTORE_VERIFY_ENABLED:
        r->progress.state = TB_RESTORE_VERIFYING;
        (void)restore_issue(b, BREAK_RESTORE_VERIFY, "-break-list", NULL, 0); break;
    case RESTORE_ENABLE:
        r->progress.state = TB_RESTORE_CONFIGURING; args[count++] = r->progress.backend;
        (void)restore_issue(b, BREAK_RESTORE_ENABLE, "-break-enable", args, count); break;
    default: break;
    }
    return true;
}
static bool definition_location(const TbBreakDefinition *d, const TbBreakpoint *row)
{
    TbSetupBreak definition = {.line = d->line};
    memcpy(definition.location, d->location, sizeof definition.location); memcpy(definition.source, d->source, sizeof definition.source);
    return location_matches(&definition, row);
}
static bool supported_match(const TbBreakDefinition *d, const TbBreakpoint *row)
{
    TbSetupBreak definition = {.enabled = d->enabled, .hardware = d->hardware, .ignore_set = d->ignore_set, .ignore = d->ignore, .line = d->line};
    memcpy(definition.location, d->location, sizeof definition.location); memcpy(definition.source, d->source, sizeof definition.source); memcpy(definition.condition, d->condition, sizeof definition.condition);
    return attributes(&definition, row, d->enabled);
}
static void intent_reconciled(TbBreakpoints *b)
{
    if (!b->listing_complete) { message(b, "Incomplete reconciliation listing; mutation remains uncertain. Retry reconciliation or close."); return; }
    size_t i = configured_index(b, b->uncertain_id); const TbBreakpoint *row = row_id(b, b->uncertain_id);
    if (b->uncertain_edit == EDIT_REMOVE && !row && i < b->definition_count) {
        forget_configured(b, b->definitions[i].data.identity);
        memmove(&b->definitions[i], &b->definitions[i + 1], (b->definition_count - i - 1) * sizeof b->definitions[0]); --b->definition_count;
    } else if (b->uncertain_edit == EDIT_CREATE && b->baseline_complete) {
        const TbBreakpoint *candidate = NULL; size_t matches = 0, additions = 0;
        for (size_t j = 0; j < b->count; ++j) {
            additions += !b->rows[j].child && new_id(b, b->rows[j].id);
            if (!b->rows[j].child && new_id(b, b->rows[j].id) && supported_match(&b->uncertain_definition, &b->rows[j])) { candidate = &b->rows[j]; ++matches; }
        }
        if (matches == 0 && additions == 0) { b->definition_uncertain = false; message(b, "Complete listing confirms no matching created breakpoint; original intent retained without retry."); return; }
        if (matches != 1 || additions != 1 || b->definition_count == TB_BREAK_ROWS) { message(b, "Ambiguous breakpoint mutation; retry reconciliation or close."); return; }
        ConfiguredBreak *d = &b->definitions[b->definition_count++]; memset(d, 0, sizeof *d);
        d->data = b->uncertain_definition; d->data.identity = ++b->definition_serial;
        (void)snprintf(d->backend, sizeof d->backend, "%s", candidate->id);
    } else if (i < b->definition_count && supported_match(&b->uncertain_definition, row)) { b->definitions[i].data = b->uncertain_definition; b->definitions[i].conflict = false; }
    else { message(b, "Mutation remains uncertain; metadata does not establish the requested state. Retry reconciliation or close."); return; }
    b->definition_uncertain = false; message(b, "Breakpoint mutation reconciled from complete current metadata; no mutation repeated.");
}
static void restore_reconciled(TbBreakpoints *b)
{
    RestoredBreak *r = &b->restored[b->restoring];
    if (!b->listing_complete) { message(b, "Incomplete restoration reconciliation; execution remains blocked. Retry reconciliation or close."); return; }
    const TbBreakpoint *row = row_id(b, r->progress.backend);
    if (!*r->progress.backend && b->uncertain_step == RESTORE_INSERT && b->baseline_complete) {
        size_t matches = 0, additions = 0;
        for (size_t i = 0; i < b->count; ++i) {
            const TbBreakpoint *candidate = &b->rows[i];
            additions += !candidate->child && new_id(b, candidate->id);
            if (!candidate->child && new_id(b, candidate->id) && candidate->definition_complete && location_matches(&r->definition, candidate) &&
                !candidate->enabled && (!r->definition.hardware || candidate->hardware)) { row = candidate; ++matches; }
        }
        if (matches != 1 || additions != 1) {
            if (!matches && !additions) { b->restore_uncertain = false; restore_finish(b, TB_RESTORE_UNRESOLVED, "Complete listing confirms no matching created location; definition retained without insertion retry."); }
            return;
        }
        (void)snprintf(r->progress.backend, sizeof r->progress.backend, "%s", row->id);
    }
    if (!*r->progress.backend) { message(b, "No complete creation baseline; ownership remains uncertain. Retry reconciliation or close."); return; }
    if (!row) { r->progress.backend[0] = 0; b->restore_uncertain = false; restore_finish(b, TB_RESTORE_UNRESOLVED, "Complete listing confirms restored location absent; definition retained."); return; }
    bool match = row->definition_complete && location_matches(&r->definition, row) && (!r->definition.hardware || row->hardware);
    if (b->uncertain_step == RESTORE_INSERT) { match = match && !row->enabled && !*row->condition && !row->ignore; }
    else if (b->uncertain_step == RESTORE_CONDITION) { match = match && !row->enabled && strcmp(row->condition, r->definition.condition) == 0; }
    else if (b->uncertain_step == RESTORE_ENABLE || b->uncertain_step == RESTORE_VERIFY_ENABLED) { match = attributes(&r->definition, row, true); }
    else { match = attributes(&r->definition, row, false); }
    if (!match) { message(b, "Restoration mutation remains uncertain; requested state is not an unambiguous match. Retry reconciliation or close."); return; }
    b->restore_uncertain = false;
    if (b->uncertain_step == RESTORE_ENABLE || b->uncertain_step == RESTORE_VERIFY_ENABLED) { restore_install(b); return; }
    if (b->restore_cancel) { restore_finish(b, TB_RESTORE_UNRESOLVED, "Cancellation settled; partial disabled location retained."); return; }
    b->restore_step = b->uncertain_step == RESTORE_INSERT ? RESTORE_CONDITION : b->uncertain_step == RESTORE_CONDITION ? RESTORE_IGNORE : RESTORE_VERIFY_DISABLED;
    r->progress.state = TB_RESTORE_PENDING;
}
static bool restore_event(TbBreakpoints *b, BreakJob job, const TbGdbEvent *event, bool okay)
{
    bool query = job == BREAK_RESTORE_VERIFY || job == BREAK_RESTORE_RECONCILE || job == BREAK_INTENT_RECONCILE;
    if (job == BREAK_INTENT_RECONCILE || job == BREAK_RESTORE_RECONCILE) {
        TbGdbGeneration current = tb_gdb_generation(b->gdb);
        okay = okay && event->generation.session == current.session && event->generation.stop == current.stop && event->generation.context == current.context;
        if (!okay) { message(b, "Reconciliation query failed; uncertainty and execution interlock retained. Retry reconciliation or close."); return true; }
        parse_listing(b, event->record);
        if (job == BREAK_INTENT_RECONCILE) { intent_reconciled(b); } else { restore_reconciled(b); }
        ++b->revision; return true;
    }
    RestoredBreak *r = &b->restored[b->restoring];
    TbGdbGeneration now = tb_gdb_generation(b->gdb);
    bool changed = event->generation.session != now.session || event->generation.stop != now.stop || event->generation.context != now.context;
    if (!okay || changed) {
        if (event->kind != TB_GDB_REPLY || changed || job == BREAK_RESTORE_ENABLE || (job == BREAK_RESTORE_INSERT && b->creation_notified)) { restore_uncertain(b, b->restore_step); }
        else {
            char error[192]; copy(error, sizeof error, event->record ? tb_mi_find(event->record->results, "msg") : NULL);
            char reason[192]; (void)snprintf(reason, sizeof reason, "%s failed: %.140s", job == BREAK_RESTORE_CONDITION ? "Condition" : job == BREAK_RESTORE_IGNORE ? "Ignore count" : query ? "Attribute verification" : "Location creation", error);
            restore_finish(b, TB_RESTORE_UNRESOLVED, reason);
        }
        return true;
    }
    if (job == BREAK_RESTORE_INSERT) {
        const TbMiValue *record = tb_mi_find(event->record->results, "bkpt");
        copy(r->progress.backend, sizeof r->progress.backend, record ? tb_mi_find(record->children, "number") : NULL);
        if (!id_valid(r->progress.backend) || strchr(r->progress.backend, '.') || !new_id(b, r->progress.backend)) { r->progress.backend[0] = 0; restore_uncertain(b, RESTORE_INSERT); return true; }
        b->restore_step = RESTORE_CONDITION;
    } else if (job == BREAK_RESTORE_CONDITION) { b->restore_step = RESTORE_IGNORE; }
    else if (job == BREAK_RESTORE_IGNORE) { b->restore_step = RESTORE_VERIFY_DISABLED; }
    else if (job == BREAK_RESTORE_ENABLE) { b->restore_step = RESTORE_VERIFY_ENABLED; }
    else if (job == BREAK_RESTORE_VERIFY) {
        parse_listing(b, event->record);
        if (!b->listing_complete) { restore_uncertain(b, b->restore_step); return true; }
        const TbBreakpoint *row = row_id(b, r->progress.backend);
        bool enabled = b->restore_step == RESTORE_VERIFY_ENABLED;
        if (b->restore_step == RESTORE_RETRY && !row) {
            r->progress.backend[0] = 0; remember_baseline(b); b->restore_step = RESTORE_INSERT;
            if (b->restore_cancel) { restore_finish(b, TB_RESTORE_UNRESOLVED, "Retry canceled after confirmed location absence."); }
            return true;
        }
        if (b->restore_step == RESTORE_RETRY && row && row->definition_complete && !row->enabled && location_matches(&r->definition, row) && (!r->definition.hardware || row->hardware)) {
            b->restore_step = RESTORE_CONDITION;
            if (b->restore_cancel) { restore_finish(b, TB_RESTORE_UNRESOLVED, "Retry canceled; disabled location retained."); }
            return true;
        }
        if (!attributes(&r->definition, row, enabled)) {
            /* A known disabled partial location can be reconfigured only after
             * an explicit retry. It is never enabled by a failed verification. */
            if (row && row->definition_complete && !row->enabled && location_matches(&r->definition, row) && (!r->definition.hardware || row->hardware)) {
                restore_finish(b, TB_RESTORE_UNRESOLVED, "Supported attributes did not verify; location remains disabled. Explicit Retry reconfigures it.");
            } else { restore_uncertain(b, b->restore_step); }
            return true;
        }
        if (enabled || !r->definition.enabled) { restore_install(b); return true; }
        b->restore_step = RESTORE_ENABLE;
    }
    if (b->restore_cancel && b->restore_step != RESTORE_VERIFY_ENABLED) { restore_finish(b, TB_RESTORE_UNRESOLVED, "Cancellation settled; partial disabled location retained."); }
    ++b->revision; return true;
}
bool tb_break_importable(const TbBreakpoints *b, const char *id, TbBreakDefinition *d)
{
    const TbBreakpoint *row = row_id(b, id);
    if (!b->ready || b->dirty || b->waiting || !b->listing_complete || !row || !row->definition_complete || row->temporary || row->watchpoint) { return false; }
    *d = (TbBreakDefinition){.enabled = row->enabled};
    if (!strncmp(row->location, "-source ", 8)) {
        const char *last = NULL, *cursor = row->location + 8;
        while ((cursor = strstr(cursor, " -line "))) { last = cursor; ++cursor; }
        if (!last || last == row->location + 8 || (size_t)(last - row->location - 8) >= sizeof d->source) { return false; }
        uint64_t line = 0; const char *digits = last + 7;
        if (!*digits) { return false; }
        for (size_t i = 0; digits[i]; ++i) {
            if (digits[i] < '0' || digits[i] > '9' || line > (UINT32_MAX - (unsigned)(digits[i] - '0')) / 10) { return false; }
            line = line * 10 + (unsigned)(digits[i] - '0');
        }
        if (!line) { return false; } d->line = (unsigned)line;
        memcpy(d->source, row->location + 8, (size_t)(last - row->location - 8));
    } else {
        if (row->location[0] == '-') { return false; }
        memcpy(d->location, row->location, sizeof d->location);
    }
    memcpy(d->condition, row->condition, sizeof d->condition); return true;
}
bool tb_break_managed(const TbBreakpoints *b, const char *id) { return configured_index(b, id) < b->definition_count; }
bool tb_break_review_intent(TbBreakpoints *b, const char *id, const TbBreakDefinition *d)
{
    const TbBreakpoint *row = row_id(b, id); size_t i = configured_index(b, id);
    if (b->definition_uncertain || i == b->definition_count || !row || !row->definition_complete || !b->listing_complete || b->dirty || b->waiting ||
        row->enabled != d->enabled || strcmp(row->condition, d->condition) != 0 || (d->hardware && !row->hardware) || row->ignore != (d->ignore_set ? d->ignore : 0)) { return false; }
    uint64_t identity = b->definitions[i].data.identity;
    TbBreakDefinition reviewed = *d;
    if (definition_location(&b->definitions[i].data, row)) {
        memcpy(reviewed.source, b->definitions[i].data.source, sizeof reviewed.source);
        memcpy(reviewed.location, b->definitions[i].data.location, sizeof reviewed.location); reviewed.line = b->definitions[i].data.line;
        reviewed.mapping = b->definitions[i].data.mapping;
    }
    b->definitions[i].data = reviewed; b->definitions[i].data.identity = identity; b->definitions[i].conflict = false; ++b->revision; return true;
}

bool tb_break_owned(const TbBreakpoints *b, const char *backend)
{
    if (tb_break_managed(b, backend)) { return true; }
    for (size_t i = 0; i < b->restored_count; ++i) { if (!strcmp(b->restored[i].progress.backend, backend)) { return true; } }
    return false;
}
void tb_break_restore_forget(TbBreakpoints *b, uint64_t id)
{
    if (tb_break_restoration_active(b) || b->restore_step != RESTORE_IDLE) { return; }
    for (size_t i = 0; i < b->restored_count; ++i) {
        if (b->restored[i].progress.id != id) { continue; }
        memmove(&b->restored[i], &b->restored[i + 1], (b->restored_count - i - 1) * sizeof b->restored[0]); --b->restored_count; return;
    }
}

const char *tb_break_conflict(const TbBreakpoints *b, size_t i)
{ return i < b->definition_count && b->definitions[i].conflict ? b->definitions[i].backend : NULL; }
bool tb_break_discard_intent(TbBreakpoints *b, uint64_t identity)
{
    if (tb_break_busy(b) || !tb_break_ready(b) || !b->listing_complete) { return false; }
    for (size_t i = 0; i < b->definition_count; ++i) {
        if (b->definitions[i].data.identity != identity || !b->definitions[i].conflict) { continue; }
        forget_configured(b, identity);
        memmove(&b->definitions[i], &b->definitions[i + 1], (b->definition_count - i - 1) * sizeof b->definitions[0]);
        --b->definition_count; ++b->revision; return true;
    }
    return false;
}

static void forget_configured(TbBreakpoints *b, uint64_t identity)
{
    for (size_t i = 0; i < b->restored_count; ++i) {
        if (b->restored[i].progress.identity != identity || b->restored[i].progress.state != TB_RESTORE_INSTALLED) { continue; }
        memmove(&b->restored[i], &b->restored[i + 1], (b->restored_count - i - 1) * sizeof b->restored[0]);
        --b->restored_count; return;
    }
}
