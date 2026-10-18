#include "registers.h"
#include "code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { REG_NAMES = 9201, REG_CANONICAL, REG_VALUES };
struct TbRegisters {
    TbGdb *gdb; TbContext *context;
    TbRegister rows[TB_REGISTERS_MAX]; size_t count;
    char canonical[TB_REGISTERS_MAX][TB_REGISTER_VALUE];
    char baseline[TB_REGISTERS_MAX][TB_REGISTER_VALUE];
    bool canonical_ok[TB_REGISTERS_MAX], baseline_ok[TB_REGISTERS_MAX], canonical_fresh[TB_REGISTERS_MAX];
    bool wanted[TB_REGISTERS_MAX], failed[TB_REGISTERS_MAX];
    uint64_t sampled_stop[TB_REGISTERS_MAX];
    char thread[32], architecture[64], error[192]; int frame;
    TbGdbGeneration generation;
    TbRegisterFormat format;
    bool visible, waiting, names_ready, names_failed, limited;
    uint64_t revision, version, request_version;
    size_t batch[TB_REGISTERS_PAGE], batch_count;
};

static int number(const TbMiValue *v)
{
    if (!v || v->type != TB_MI_STRING || !v->string.length) { return -1; }
    unsigned n = 0;
    for (size_t i = 0; i < v->string.length; ++i) {
        unsigned char c = (unsigned char)v->string.data[i];
        if (c < '0' || c > '9' || n > (TB_REGISTERS_MAX - 1U - (unsigned)(c - '0')) / 10U) { return -1; }
        n = n * 10U + c - '0';
    }
    return n < TB_REGISTERS_MAX ? (int)n : -1;
}
static bool copy(char *out, size_t capacity, const TbMiValue *v)
{
    out[0] = 0;
    if (!v || v->type != TB_MI_STRING) { return false; }
    tb_code_safe(out, capacity, v->string.data, v->string.length);
    /* Escaping can expand bytes: conservatively exclude escaped/truncated
     * values from canonical comparisons rather than claim unseen changes. */
    return v->string.length < capacity && strlen(out) == v->string.length;
}
static bool same(TbGdbGeneration a, TbGdbGeneration b)
{
    return a.session == b.session && a.stop == b.stop && a.context == b.context;
}
TbRegisters *tb_registers_create(TbGdb *gdb, TbContext *context)
{
    TbRegisters *r = calloc(1, sizeof *r);
    if (r) { r->gdb = gdb; r->context = context; tb_registers_reset(r); }
    return r;
}
void tb_registers_destroy(TbRegisters *r) { free(r); }
void tb_registers_reset(TbRegisters *r)
{
    TbGdb *gdb = r->gdb; TbContext *context = r->context; uint64_t revision = r->revision;
    memset(r, 0, sizeof *r); r->gdb = gdb; r->context = context;
    r->frame = -1; r->revision = revision + 1;
}
void tb_registers_visible(TbRegisters *r, bool visible)
{
    r->visible = visible; tb_context_registers(r->context, visible);
}
void tb_registers_begin(TbRegisters *r) { memset(r->wanted, 0, sizeof r->wanted); }
void tb_registers_want(TbRegisters *r, size_t first, size_t count)
{
    for (size_t i = first; count && i < r->count; ++i, --count) { r->wanted[i] = true; }
}
void tb_registers_refresh(TbRegisters *r)
{
    ++r->version; r->error[0] = 0; r->names_failed = false;
    for (size_t i = 0; i < r->count; ++i) { r->rows[i].fresh = false; r->failed[i] = r->canonical_fresh[i] = false; }
    ++r->revision;
}
void tb_registers_format(TbRegisters *r, TbRegisterFormat format)
{
    if (format < 0 || format >= TB_REG_FORMATS || format == r->format) { return; }
    r->format = format; tb_registers_refresh(r);
}
TbRegisterFormat tb_registers_get_format(const TbRegisters *r) { return r->format; }
const char *tb_registers_format_name(TbRegisterFormat format)
{
    static const char *const names[] = {"Hex", "Decimal", "Octal", "Binary", "Raw", "Natural"};
    return format >= 0 && format < TB_REG_FORMATS ? names[format] : "Unknown";
}
size_t tb_registers_count(const TbRegisters *r) { return r->count; }
const TbRegister *tb_registers_row(const TbRegisters *r, size_t row) { return row < r->count ? &r->rows[row] : NULL; }
bool tb_registers_stale(const TbRegisters *r)
{
    const TbContextSnapshot *s = tb_context_snapshot(r->context);
    return tb_gdb_state(r->gdb) != TB_GDB_STOPPED || s->stale || !s->ready || !same(r->generation, s->generation);
}
bool tb_registers_limited(const TbRegisters *r) { return r->limited; }
const char *tb_registers_error(const TbRegisters *r) { return r->error; }
uint64_t tb_registers_revision(const TbRegisters *r) { return r->revision; }

static bool issue(TbRegisters *r, unsigned job)
{
    const char *args[TB_REGISTERS_PAGE + 5]; char ids[TB_REGISTERS_PAGE][16], frame[16];
    size_t count = 0;
    if (job != REG_NAMES) {
        args[count++] = "--thread"; args[count++] = r->thread;
        args[count++] = "--frame"; (void)snprintf(frame, sizeof frame, "%d", r->frame); args[count++] = frame;
        static const char *const formats[] = {"x", "d", "o", "t", "r", "N"};
        args[count++] = job == REG_CANONICAL ? "x" : formats[r->format];
        for (size_t i = 0; i < r->batch_count; ++i) {
            (void)snprintf(ids[i], sizeof ids[i], "%u", r->rows[r->batch[i]].number); args[count++] = ids[i];
        }
    }
    r->request_version = r->version;
    r->waiting = tb_gdb_request(r->gdb, job == REG_NAMES ? "-data-list-register-names" : "-data-list-register-values",
                               args, count, TB_GDB_CONTEXT, job, 3000, NULL);
    return r->waiting;
}
bool tb_registers_event(TbRegisters *r, const TbGdbEvent *event)
{
    if (!r->visible && event->kind == TB_GDB_RECORD && event->record &&
        event->record->kind == TB_MI_EXEC && strcmp(event->record->klass, "stopped") == 0) {
        memset(r->canonical_ok, 0, sizeof r->canonical_ok);
    }
    if (event->cookie < REG_NAMES || event->cookie > REG_VALUES ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    r->waiting = false;
    if (event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED || r->request_version != r->version ||
        !same(event->generation, r->generation)) { ++r->revision; return true; }
    const TbMiRecord *record = event->record;
    bool okay = event->kind == TB_GDB_REPLY && record && strcmp(record->klass, "done") == 0;
    if (!okay) {
        (void)copy(r->error, sizeof r->error, record ? tb_mi_find(record->results, "msg") : NULL);
        if (!*r->error) { (void)snprintf(r->error, sizeof r->error, "Register read failed/timed out; Refresh retries."); }
        if (event->cookie == REG_NAMES) { r->names_failed = true; }
        else { for (size_t i = 0; i < r->batch_count; ++i) { r->failed[r->batch[i]] = true; } }
        ++r->revision; return true;
    }
    const TbMiValue *list = tb_mi_find(record->results, event->cookie == REG_NAMES ? "register-names" : "register-values");
    if (!list || list->type != TB_MI_LIST) {
        (void)snprintf(r->error, sizeof r->error, "Malformed register reply; Refresh retries.");
        if (event->cookie == REG_NAMES) { r->names_failed = true; }
        else { for (size_t i = 0; i < r->batch_count; ++i) { r->failed[r->batch[i]] = true; } }
        ++r->revision; return true;
    }
    if (event->cookie == REG_NAMES) {
        r->count = 0; r->limited = false; unsigned id = 0;
        for (const TbMiValue *v = list->children; v; v = v->next, ++id) {
            if (id == TB_REGISTERS_MAX) { r->limited = true; break; }
            if (v->type != TB_MI_STRING || !v->string.length) { continue; }
            TbRegister *row = &r->rows[r->count]; memset(row, 0, sizeof *row); row->number = id;
            (void)copy(row->name, sizeof row->name, v); ++r->count;
        }
        r->names_ready = true;
        if (!r->count) { (void)snprintf(r->error, sizeof r->error, "GDB reports no named registers."); }
    } else {
        for (size_t i = 0; i < r->batch_count; ++i) {
            size_t index = r->batch[i]; TbRegister *row = &r->rows[index]; const TbMiValue *value = NULL;
            for (const TbMiValue *v = list->children; v; v = v->next) {
                if (v->type == TB_MI_TUPLE && number(tb_mi_find(v->children, "number")) == (int)row->number) {
                    value = tb_mi_find(v->children, "value"); break;
                }
            }
            char text[TB_REGISTER_VALUE]; bool complete = copy(text, sizeof text, value);
            bool available = value && *text && strcmp(text, "<unavailable>") != 0;
            if (event->cookie == REG_CANONICAL) {
                row->changed = complete && available && r->baseline_ok[index] && strcmp(text, r->baseline[index]) != 0;
                memcpy(r->canonical[index], text, strlen(text) + 1);
                r->canonical_ok[index] = complete && available; r->sampled_stop[index] = r->generation.stop;
                r->canonical_fresh[index] = true;
            }
            if (event->cookie == REG_VALUES || r->format == TB_REG_HEX) {
                (void)snprintf(row->value, sizeof row->value, "%s", available ? text : "Unavailable");
                row->available = available; row->truncated = value && !complete; row->fresh = true;
            }
        }
    }
    ++r->revision; return true;
}
void tb_registers_pump(TbRegisters *r)
{
    if (!r->visible || tb_gdb_state(r->gdb) != TB_GDB_STOPPED) { return; }
    const TbContextSnapshot *s = tb_context_snapshot(r->context);
    if (s->stale || !s->ready) { return; }
    if (!same(r->generation, s->generation)) {
        bool identity = r->generation.session == s->generation.session && r->frame == s->selected.level &&
                        strcmp(r->thread, s->selected_thread) == 0 && strcmp(r->architecture, s->selected.architecture) == 0;
        bool stop_changed = r->generation.stop != s->generation.stop;
        for (size_t i = 0; i < r->count; ++i) {
            if (!identity || stop_changed) {
                r->baseline_ok[i] = identity && r->canonical_ok[i] && r->sampled_stop[i] == r->generation.stop;
                if (r->baseline_ok[i]) { memcpy(r->baseline[i], r->canonical[i], sizeof r->baseline[i]); }
            }
            r->rows[i].fresh = r->rows[i].changed = false; r->failed[i] = false;
            r->canonical_fresh[i] = false;
            if (!identity) { r->canonical_ok[i] = false; }
        }
        if (r->generation.session != s->generation.session || strcmp(r->architecture, s->selected.architecture) != 0) {
            r->names_ready = false; r->count = 0;
            memset(r->canonical_ok, 0, sizeof r->canonical_ok); memset(r->baseline_ok, 0, sizeof r->baseline_ok);
        }
        r->generation = s->generation; r->frame = s->selected.level;
        (void)snprintf(r->thread, sizeof r->thread, "%s", s->selected_thread);
        (void)snprintf(r->architecture, sizeof r->architecture, "%s", s->selected.architecture);
        ++r->version; r->error[0] = 0; r->names_failed = false; ++r->revision;
    }
    if (r->waiting) { return; }
    if (!r->names_ready) { if (!r->names_failed) { (void)issue(r, REG_NAMES); } return; }
    r->batch_count = 0;
    for (size_t i = 0; i < r->count && r->batch_count < TB_REGISTERS_PAGE; ++i) {
        if (r->wanted[i] && !r->rows[i].fresh && !r->failed[i]) { r->batch[r->batch_count++] = i; }
    }
    if (!r->batch_count) { return; }
    bool canonical = false;
    for (size_t i = 0; i < r->batch_count; ++i) {
        if (!r->canonical_fresh[r->batch[i]]) { canonical = true; }
    }
    if (r->format == TB_REG_HEX || canonical) { (void)issue(r, REG_CANONICAL); }
    else { (void)issue(r, REG_VALUES); }
}
