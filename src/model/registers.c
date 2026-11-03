#include "model/registers.h"
#include "model/code.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    REG_NAMES = 9201,
    REG_CANONICAL,
    REG_VALUES
};

enum {
    REG_EDIT_CAPABILITY = 9250,
    REG_EDIT_NAME,
    REG_EDIT_PERMISSION,
    REG_EDIT_CREATE,
    REG_EDIT_FREEZE,
    REG_EDIT_ATTRIBUTES,
    REG_EDIT_SIZE,
    REG_EDIT_WRITE,
    REG_EDIT_DELETE
};

struct tb_registers {
    TbGdb_t           *gdb;
    TbContext_t       *context;
    TbRegister_t       rows[TB_REGISTERS_MAX];
    size_t             count;
    char               canonical[TB_REGISTERS_MAX][TB_REGISTER_VALUE];
    char               baseline[TB_REGISTERS_MAX][TB_REGISTER_VALUE];
    bool               canonical_ok[TB_REGISTERS_MAX];
    bool               baseline_ok[TB_REGISTERS_MAX];
    bool               canonical_fresh[TB_REGISTERS_MAX];
    bool               wanted[TB_REGISTERS_MAX];
    bool               failed[TB_REGISTERS_MAX];
    uint64_t           sampled_stop[TB_REGISTERS_MAX];
    char               thread[32];
    char               architecture[64];
    char               error[192];
    int                frame;
    TbGdbGeneration_t  generation;
    TbRegisterFormat_e format;
    bool               visible;
    bool               waiting;
    bool               names_ready;
    bool               names_failed;
    bool               names_complete[TB_REGISTERS_MAX];
    bool               limited;
    uint64_t           revision;
    uint64_t           version;
    uint64_t           request_version;
    size_t             batch[TB_REGISTERS_PAGE];
    size_t             batch_count;
    bool               octets;
    TbGdbGeneration_t  refresh_generation;
    TbRegisterEdit_t   edit;
    TbGdbGeneration_t  edit_generation;
    uint64_t           edit_version;
    uint64_t           edit_lifetime;
    size_t             edit_row;
    unsigned           edit_phase;
    bool               edit_waiting;
    bool               edit_object;
    bool               edit_cancelled;
    bool               edit_broken;
    bool               edit_complete;
    TbWriteOutcome_e   edit_outcome;
};

static bool edit_event(TbRegisters_t *r, const TbGdbEvent_t *event);
static void edit_pump(TbRegisters_t *r);
static void edit_observe(TbRegisters_t *r, size_t row);


static int number(const TbMiValue_t *v)
{
    if (!v || v->type != TB_MI_STRING || !v->string.length)
        return -1;

    unsigned n = 0;
    for (size_t i = 0; i < v->string.length; ++i) {
        unsigned char c = (unsigned char)v->string.data[i];
        if (c < '0' || c > '9' || n > (TB_REGISTERS_MAX - 1U - (unsigned)(c - '0')) / 10U)
            return -1;

        n = n * 10U + c - '0';
    }

    return n < TB_REGISTERS_MAX ? (int)n : -1;
}


static bool copy(char *out, size_t capacity, const TbMiValue_t *v)
{
    out[0] = 0;
    if (!v || v->type != TB_MI_STRING)
        return false;

    tb_code_safe(out, capacity, v->string.data, v->string.length);
    /* Escaping can expand bytes: conservatively exclude escaped/truncated
     * values from canonical comparisons rather than claim unseen changes. */
    return v->string.length < capacity && strlen(out) == v->string.length;
}


static bool same(TbGdbGeneration_t a, TbGdbGeneration_t b)
{
    return a.session == b.session && a.stop == b.stop && a.context == b.context;
}


TbRegisters_t *tb_registers_create(TbGdb_t *gdb, TbContext_t *context)
{
    TbRegisters_t *r = calloc(1, sizeof(*r));
    if (r) {
        r->gdb     = gdb;
        r->context = context;
        tb_registers_reset(r);
    }

    return r;
}


void tb_registers_destroy(TbRegisters_t *r)
{
    free(r);
}


void tb_registers_reset(TbRegisters_t *r)
{
    TbGdb_t     *gdb      = r->gdb;
    TbContext_t *context  = r->context;
    uint64_t     revision = r->revision;

    memset(r, 0, sizeof(*r));
    r->gdb      = gdb;
    r->context  = context;
    r->frame    = -1;
    r->revision = revision + 1;
}


void tb_registers_visible(TbRegisters_t *r, bool visible)
{
    r->visible = visible;
    tb_context_registers(r->context, visible);
}


void tb_registers_begin(TbRegisters_t *r)
{
    memset(r->wanted, 0, sizeof(r->wanted));
}


void tb_registers_want(TbRegisters_t *r, size_t first, size_t count)
{
    for (size_t i = first; count && i < r->count; ++i, --count)
        r->wanted[i] = true;
}


void tb_registers_refresh(TbRegisters_t *r)
{
    r->refresh_generation = tb_gdb_generation(r->gdb);
    ++r->version;
    r->error[0]     = 0;
    r->names_failed = false;

    for (size_t i = 0; i < r->count; ++i) {
        r->rows[i].fresh = false;
        r->failed[i]     = r->canonical_fresh[i] = false;
    }
    ++r->revision;
}


void tb_registers_format(TbRegisters_t *r, TbRegisterFormat_e format)
{
    if (format < 0 || format >= TB_REG_FORMATS || format == r->format)
        return;

    r->format = format;
    tb_registers_refresh(r);
}


TbRegisterFormat_e tb_registers_get_format(const TbRegisters_t *r)
{
    return r->format;
}


const char *tb_registers_format_name(TbRegisterFormat_e format)
{
    static const char *const names[] = {"Hex", "Decimal", "Octal", "Binary", "Raw", "Natural"};

    return format >= 0 && format < TB_REG_FORMATS ? names[format] : "Unknown";
}


size_t tb_registers_count(const TbRegisters_t *r)
{
    return r->count;
}


const TbRegister_t *tb_registers_row(const TbRegisters_t *r, size_t row)
{
    return row < r->count ? &r->rows[row] : NULL;
}


bool tb_registers_stale(const TbRegisters_t *r)
{
    const TbContextSnapshot_t *s = tb_context_snapshot(r->context);

    return tb_gdb_state(r->gdb) != TB_GDB_STOPPED || s->stale || !s->ready ||
           !same(r->generation, s->generation);
}


bool tb_registers_settled(const TbRegisters_t *r)
{
    if (tb_registers_stale(r) || r->waiting)
        return false;

    if (r->names_failed)
        return true;

    if (!r->names_ready)
        return false;

    for (size_t i = 0; i < r->count; ++i) {
        if (r->wanted[i] && !r->rows[i].fresh && !r->failed[i])
            return false;
    }

    return true;
}


bool tb_registers_limited(const TbRegisters_t *r)
{
    return r->limited;
}


const char *tb_registers_error(const TbRegisters_t *r)
{
    return r->error;
}


uint64_t tb_registers_revision(const TbRegisters_t *r)
{
    return r->revision;
}


static bool issue(TbRegisters_t *r, unsigned job)
{
    const char *args[TB_REGISTERS_PAGE + 5];
    char        ids[TB_REGISTERS_PAGE][16], frame[16];
    size_t      count = 0;
    if (job != REG_NAMES) {
        args[count++] = "--thread";
        args[count++] = r->thread;
        args[count++] = "--frame";
        (void)snprintf(frame, sizeof(frame), "%d", r->frame);
        args[count++] = frame;
        static const char *const formats[] = {"x", "d", "o", "t", "r", "N"};
        args[count++] = job == REG_CANONICAL ? "x" : formats[r->format];
        for (size_t i = 0; i < r->batch_count; ++i) {
            (void)snprintf(ids[i], sizeof(ids[i]), "%u", r->rows[r->batch[i]].number);
            args[count++] = ids[i];
        }
    }

    r->request_version = r->version;
    r->waiting =
        tb_gdb_request(r->gdb, job == REG_NAMES ? "-data-list-register-names" : "-data-list-register-values",
                       args, count, TB_GDB_CONTEXT, job, 3000, NULL);

    return r->waiting;
}


bool tb_registers_event(TbRegisters_t *r, const TbGdbEvent_t *event)
{
    if (edit_event(r, event))
        return true;

    if (!r->visible && event->kind == TB_GDB_RECORD && event->record && event->record->kind == TB_MI_EXEC &&
        strcmp(event->record->klass, "stopped") == 0) {
        memset(r->canonical_ok, 0, sizeof(r->canonical_ok));
    }

    if (event->cookie < REG_NAMES || event->cookie > REG_VALUES ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT ||
          event->kind == TB_GDB_CANCELLED)) {
        return false;
    }
    r->waiting = false;
    if (event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED || r->request_version != r->version ||
        !same(event->generation, r->generation)) {
        ++r->revision;
        return true;
    }

    const TbMiRecord_t *record = event->record;
    bool                okay   = event->kind == TB_GDB_REPLY && record && strcmp(record->klass, "done") == 0;
    if (!okay) {
        (void)copy(r->error, sizeof(r->error), record ? tb_mi_find(record->results, "msg") : NULL);
        if (!*r->error)
            (void)snprintf(r->error, sizeof(r->error), "Register read failed/timed out; Refresh retries.");

        if (event->cookie == REG_NAMES) {
            r->names_failed = true;
        } else {
            for (size_t i = 0; i < r->batch_count; ++i)
                r->failed[r->batch[i]] = true;
        }
        ++r->revision;
        return true;
    }

    const TbMiValue_t *list =
        tb_mi_find(record->results, event->cookie == REG_NAMES ? "register-names" : "register-values");
    if (!list || list->type != TB_MI_LIST) {
        (void)snprintf(r->error, sizeof(r->error), "Malformed register reply; Refresh retries.");

        if (event->cookie == REG_NAMES) {
            r->names_failed = true;
        } else {
            for (size_t i = 0; i < r->batch_count; ++i)
                r->failed[r->batch[i]] = true;
        }
        ++r->revision;
        return true;
    }

    if (event->cookie == REG_NAMES) {
        r->count   = 0;
        r->limited = false;
        unsigned id = 0;
        for (const TbMiValue_t *v = list->children; v; v = v->next, ++id) {
            if (id == TB_REGISTERS_MAX) {
                r->limited = true;
                break;
            }
            if (v->type != TB_MI_STRING || !v->string.length)
                continue;

            TbRegister_t *row = &r->rows[r->count];
            memset(row, 0, sizeof(*row));
            row->number = id;
            r->names_complete[r->count] = copy(row->name, sizeof(row->name), v);
            ++r->count;
        }
        r->names_ready = true;
        if (!r->count)
            (void)snprintf(r->error, sizeof(r->error), "GDB reports no named registers.");
    } else {
        for (size_t i = 0; i < r->batch_count; ++i) {
            size_t             index = r->batch[i];
            TbRegister_t      *row   = &r->rows[index];
            const TbMiValue_t *value = NULL;
            for (const TbMiValue_t *v = list->children; v; v = v->next) {
                if (v->type == TB_MI_TUPLE && number(tb_mi_find(v->children, "number")) == (int)row->number) {
                    value = tb_mi_find(v->children, "value");
                    break;
                }
            }

            char text[TB_REGISTER_VALUE];
            bool complete  = copy(text, sizeof(text), value);
            bool available = value && *text && strcmp(text, "<unavailable>") != 0;
            if (event->cookie == REG_CANONICAL) {
                row->changed =
                    complete && available && r->baseline_ok[index] && strcmp(text, r->baseline[index]) != 0;
                memcpy(r->canonical[index], text, strlen(text) + 1);
                r->canonical_ok[index]    = complete && available;
                r->sampled_stop[index]    = r->generation.stop;
                r->canonical_fresh[index] = true;
                edit_observe(r, index);
            }

            if (event->cookie == REG_VALUES || r->format == TB_REG_HEX) {
                (void)snprintf(row->value, sizeof(row->value), "%s", available ? text : "Unavailable");
                row->available = available;
                row->truncated = value && !complete;
                row->fresh     = true;
            }
        }
    }
    ++r->revision;

    return true;
}


void tb_registers_pump(TbRegisters_t *r)
{
    edit_pump(r);

    if (!r->visible || tb_gdb_state(r->gdb) != TB_GDB_STOPPED)
        return;

    const TbContextSnapshot_t *s = tb_context_snapshot(r->context);
    if (s->stale || !s->ready)
        return;

    if (!same(r->generation, s->generation)) {
        bool identity     = r->generation.session == s->generation.session && r->frame == s->selected.level &&
                            strcmp(r->thread, s->selected_thread) == 0 &&
                            strcmp(r->architecture, s->selected.architecture) == 0;
        bool stop_changed = r->generation.stop != s->generation.stop;

        for (size_t i = 0; i < r->count; ++i) {
            if (!identity || stop_changed) {
                r->baseline_ok[i] =
                    identity && r->canonical_ok[i] && r->sampled_stop[i] == r->generation.stop;
                if (r->baseline_ok[i])
                    memcpy(r->baseline[i], r->canonical[i], sizeof(r->baseline[i]));
            }
            r->rows[i].fresh      = r->rows[i].changed = false;
            r->failed[i]          = false;
            r->canonical_fresh[i] = false;
            if (!identity) {
                r->canonical_ok[i]  = false;
                r->rows[i].value[0] = 0;
            }
        }
        if (r->generation.session != s->generation.session ||
            strcmp(r->architecture, s->selected.architecture) != 0) {
            r->names_ready = false;
            r->count       = 0;
            memset(r->canonical_ok, 0, sizeof(r->canonical_ok));
            memset(r->baseline_ok, 0, sizeof(r->baseline_ok));
        }
        r->generation = s->generation;
        r->frame      = s->selected.level;
        (void)snprintf(r->thread, sizeof(r->thread), "%s", s->selected_thread);
        (void)snprintf(r->architecture, sizeof(r->architecture), "%s", s->selected.architecture);
        ++r->version;
        r->error[0]     = 0;
        r->names_failed = false;
        ++r->revision;
    }

    if (r->waiting)
        return;

    if (r->edit_waiting || r->edit_object || r->edit.state == TB_REGISTER_EDIT_PROBING ||
        r->edit.state == TB_REGISTER_EDIT_READY || r->edit.state == TB_REGISTER_EDIT_CHECKING ||
        r->edit.state == TB_REGISTER_EDIT_PENDING ||
        (tb_context_written(r->context) &&
         !same(r->refresh_generation, tb_gdb_generation(r->gdb)))) {
        return;
    }

    if (!r->names_ready) {
        if (!r->names_failed)
            (void)issue(r, REG_NAMES);
        return;
    }

    r->batch_count = 0;
    for (size_t i = 0; i < r->count && r->batch_count < TB_REGISTERS_PAGE; ++i) {
        if (r->wanted[i] && !r->rows[i].fresh && !r->failed[i])
            r->batch[r->batch_count++] = i;
    }
    if (!r->batch_count)
        return;

    bool canonical = false;
    for (size_t i = 0; i < r->batch_count; ++i) {
        if (!r->canonical_fresh[r->batch[i]])
            canonical = true;
    }
    if (r->format == TB_REG_HEX || canonical) {
        (void)issue(r, REG_CANONICAL);
    } else {
        (void)issue(r, REG_VALUES);
    }
}


static bool edit_string(const TbMiValue_t *value, const char *expected)
{
    return value && value->type == TB_MI_STRING && value->string.length == strlen(expected) &&
           memcmp(value->string.data, expected, value->string.length) == 0;
}


static bool edit_hex(const char *text, unsigned bits, char out[19])
{
    if (!text || !bits || bits > 64 || bits % 8)
        return false;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;

    size_t length = strlen(text);
    if (!length || length > bits / 4)
        return false;

    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c     = (unsigned char)text[i];
        unsigned      digit = c >= '0' && c <= '9' ? (unsigned)(c - '0')
            : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a') + 10
            : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A') + 10
            : 16;

        if (digit > 15)
            return false;

        value = value * 16 + digit;
    }

    (void)snprintf(out, 19, "0x%0*" PRIx64, (int)(bits / 4), value);

    return true;
}


static bool edit_identity(const TbRegisters_t *r)
{
    const TbContextSnapshot_t *s = tb_context_snapshot(r->context);

    return r->octets && r->edit_lifetime && !r->edit_cancelled && !tb_registers_stale(r) &&
           s->selected.level == 0 && !tb_context_interlocked(r->context) &&
           !tb_context_write_blocked(r->context) && same(r->edit_generation, tb_gdb_generation(r->gdb)) &&
           r->version == r->edit_version && strcmp(r->edit.thread, s->selected_thread) == 0 &&
           tb_context_thread_lifetime(r->context, s->selected_thread) == r->edit_lifetime &&
           r->edit_row < r->count && r->rows[r->edit_row].number == r->edit.number &&
           strcmp(r->rows[r->edit_row].name, r->edit.name) == 0 && r->rows[r->edit_row].fresh &&
           r->rows[r->edit_row].available && !r->rows[r->edit_row].truncated &&
           r->canonical_ok[r->edit_row] && r->canonical_fresh[r->edit_row];
}


static bool edit_destination(const TbRegisters_t *r)
{
    const TbContextSnapshot_t *s = tb_context_snapshot(r->context);

    return r->edit_lifetime && !tb_registers_stale(r) && s->selected.level == 0 &&
           r->edit_generation.session == s->generation.session &&
           r->edit_generation.stop == s->generation.stop && strcmp(r->edit.thread, s->selected_thread) == 0 &&
           tb_context_thread_lifetime(r->context, s->selected_thread) == r->edit_lifetime &&
           r->edit_row < r->count && r->rows[r->edit_row].number == r->edit.number &&
           strcmp(r->rows[r->edit_row].name, r->edit.name) == 0;
}


static void edit_observe(TbRegisters_t *r, size_t row)
{
    if ((r->edit.state != TB_REGISTER_EDIT_ACKNOWLEDGED && r->edit.state != TB_REGISTER_EDIT_UNCERTAIN) ||
        row != r->edit_row || !edit_destination(r) || !same(r->refresh_generation, r->generation) ||
        !r->canonical_ok[row] || !edit_hex(r->canonical[row], r->edit.bits, r->edit.observed_value)) {
        return;
    }

    r->edit.verified = true;
    r->edit.matches  = strcmp(r->edit.new_value, r->edit.observed_value) == 0;
    (void)snprintf(r->edit.message, sizeof(r->edit.message), "%s",
                   r->edit.matches
                       ? "Explicit Refresh observed the requested bits."
                       : "Explicit Refresh observed DIFFERENT bits; the requested value is not verified.");
    ++r->revision;
}


bool tb_registers_edit_verify(TbRegisters_t *r)
{
    if ((r->edit.state != TB_REGISTER_EDIT_ACKNOWLEDGED && r->edit.state != TB_REGISTER_EDIT_UNCERTAIN) ||
        !edit_destination(r) || r->edit_waiting || r->edit_object || tb_gdb_pending(r->gdb)) {
        return false;
    }

    r->visible = true;
    tb_registers_refresh(r);
    memset(r->wanted, 0, sizeof(r->wanted));
    tb_registers_want(r, r->edit_row, 1);
    r->edit.verified = false;

    return true;
}


static void edit_fail(TbRegisters_t *r, const char *message)
{
    r->edit.state = TB_REGISTER_EDIT_FAILED;
    r->edit_phase = r->edit_object ? REG_EDIT_DELETE : 0;
    (void)snprintf(r->edit.message, sizeof(r->edit.message), "%s", message);
    ++r->revision;
}


void tb_registers_target(TbRegisters_t *r, bool octets)
{
    if (r->octets == octets)
        return;

    r->octets = octets;
    if (r->edit.state == TB_REGISTER_EDIT_READY || r->edit.state == TB_REGISTER_EDIT_PROBING ||
        r->edit.state == TB_REGISTER_EDIT_CHECKING) {
        r->edit_cancelled = true;
        edit_fail(r, "Register size evidence changed; cancel and review again.");
    }
}


const TbRegisterEdit_t *tb_registers_edit(const TbRegisters_t *r)
{
    return &r->edit;
}


bool tb_registers_edit_begin(TbRegisters_t *r, size_t row)
{
    /* Caller-frame writes may alter unwound stack storage rather than a live CPU register. */
    const TbContextSnapshot_t *s = tb_context_snapshot(r->context);
    const TbRegister_t        *v = tb_registers_row(r, row);

    if (r->edit_waiting || r->edit_object || r->edit_broken || r->waiting ||
        tb_context_write_blocked(r->context) || tb_context_interlocked(r->context) ||
        tb_gdb_pending(r->gdb) || !r->octets || tb_registers_stale(r) || s->selected.level != 0 || !v ||
        !v->fresh || !v->available || v->truncated || !r->names_complete[row] || !r->canonical_ok[row] ||
        !r->canonical_fresh[row]) {
        (void)snprintf(r->edit.message, sizeof(r->edit.message),
                       "Use a fresh frame-0 CPU register on an idle stopped target; Refresh or reconnect.");
        ++r->revision;
        return false;
    }

    size_t length = strlen(v->name);
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)v->name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (i && c >= '0' && c <= '9'))) {
            edit_fail(r, "This register name cannot be bound to an unambiguous GDB register expression.");
            return false;
        }
    }

    for (size_t i = 0; i < r->count; ++i) {
        if (i != row && strcmp(r->rows[i].name, v->name) == 0) {
            edit_fail(r, "Duplicate register names cannot establish an unambiguous editable destination.");
            return false;
        }
    }

    char old[19];
    if (!length || !edit_hex(r->canonical[row], 64, old)) {
        edit_fail(r, "A complete scalar hexadecimal sample is required; wide/compound values are excluded.");
        return false;
    }

    r->edit = (TbRegisterEdit_t) {
        .state  = TB_REGISTER_EDIT_PROBING,
        .number = v->number
    };

    (void)snprintf(r->edit.name, sizeof(r->edit.name), "%s", v->name);
    (void)snprintf(r->edit.thread, sizeof(r->edit.thread), "%s", s->selected_thread);
    r->edit_generation = s->generation;
    r->edit_version    = r->version;
    r->edit_lifetime   = tb_context_thread_lifetime(r->context, s->selected_thread);
    r->edit_row        = row;
    r->edit_phase      = REG_EDIT_CAPABILITY;
    r->edit_cancelled  = false;
    (void)snprintf(r->edit.message, sizeof(r->edit.message),
                   "Checking GDB editability and target size; no write.");
    ++r->revision;

    return true;
}


bool tb_registers_edit_value(TbRegisters_t *r, const char *hex)
{
    if (r->edit.state != TB_REGISTER_EDIT_READY)
        return false;

    if (!edit_identity(r)) {
        edit_fail(r, "Register edit proof expired; cancel, Refresh and review again.");
        return false;
    }

    char value[19];
    if (!edit_hex(hex, r->edit.bits, value)) {
        (void)snprintf(r->edit.message, sizeof(r->edit.message),
                       "Enter 1 to %u hexadecimal digits, optionally prefixed by 0x; no expressions.",
                       r->edit.bits / 4);
        ++r->revision;
        return false;
    }

    memcpy(r->edit.new_value, value, strlen(value) + 1);
    ++r->revision;

    return true;
}


bool tb_registers_edit_apply(TbRegisters_t *r, const char *hex)
{
    if (!tb_registers_edit_value(r, hex))
        return false;

    r->edit.state = TB_REGISTER_EDIT_CHECKING;
    r->edit_phase = REG_EDIT_NAME;
    (void)snprintf(r->edit.message, sizeof(r->edit.message),
                   "Rechecking write permission and editability before single dispatch.");
    ++r->revision;

    return true;
}


void tb_registers_edit_cancel(TbRegisters_t *r)
{
    if (r->edit.state == TB_REGISTER_EDIT_PENDING || r->edit.state == TB_REGISTER_EDIT_UNCERTAIN)
        return;

    r->edit_cancelled = true;
    r->edit.state     = TB_REGISTER_EDIT_NONE;
    if (!r->edit_waiting)
        r->edit_phase = r->edit_object ? REG_EDIT_DELETE : 0;

    ++r->revision;
}


bool tb_registers_edit_reconcile(TbRegisters_t *r)
{
    const TbContextSnapshot_t *s = tb_context_snapshot(r->context);

    if (r->edit.state != TB_REGISTER_EDIT_UNCERTAIN || r->edit_object || r->edit_waiting ||
        tb_registers_stale(r) || s->selected.level != 0 ||
        r->edit_generation.session != s->generation.session ||
        r->edit_generation.stop != s->generation.stop || strcmp(r->edit.thread, s->selected_thread) != 0 ||
        tb_context_thread_lifetime(r->context, s->selected_thread) != r->edit_lifetime ||
        !same(r->refresh_generation, s->generation) || r->edit_row >= r->count ||
        r->rows[r->edit_row].number != r->edit.number ||
        strcmp(r->rows[r->edit_row].name, r->edit.name) != 0 || !r->rows[r->edit_row].fresh ||
        !r->canonical_fresh[r->edit_row] || !r->canonical_ok[r->edit_row]) {
        return false;
    }

    char observed[19];
    if (!edit_hex(r->canonical[r->edit_row], r->edit.bits, observed) ||
        !tb_context_write_reconcile(r->context, REG_EDIT_WRITE)) {
        return false;
    }

    r->edit.state = TB_REGISTER_EDIT_ACKNOWLEDGED;
    (void)snprintf(r->edit.message, sizeof(r->edit.message),
                   "Reconciled original register after explicit Refresh.");
    ++r->revision;

    return true;
}


static bool edit_event(TbRegisters_t *r, const TbGdbEvent_t *event)
{
    if (event->cookie < REG_EDIT_CAPABILITY || event->cookie > REG_EDIT_DELETE ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT ||
          event->kind == TB_GDB_CANCELLED)) {
        return false;
    }

    r->edit_waiting = false;

    const TbMiRecord_t *record = event->record;
    bool                okay   = event->kind == TB_GDB_REPLY && record && strcmp(record->klass, "done") == 0;

    if (event->cookie == REG_EDIT_WRITE) {
        bool current = same(event->generation, r->edit_generation) &&
                       same(event->generation, tb_gdb_generation(r->gdb)) &&
                       tb_gdb_state(r->gdb) == TB_GDB_STOPPED;

        r->edit.state    = okay && current ? TB_REGISTER_EDIT_ACKNOWLEDGED : TB_REGISTER_EDIT_UNCERTAIN;
        r->edit_outcome  = okay && current ? TB_WRITE_ACKNOWLEDGED
            : event->kind == TB_GDB_REPLY ? TB_WRITE_UNCERTAIN
            : TB_WRITE_UNRESOLVED;
        r->edit_complete = true;
        r->edit_phase    = 0;
        (void)snprintf(r->edit.message, sizeof(r->edit.message), "%s",
                       okay && current
                           ? "Write acknowledged; contents are not verified. Refresh explicitly."
                           : "UNCERTAIN register write; no retry/rollback. Explicitly Refresh or reconnect.");
        if (!okay && record) {
            char error[96];
            (void)copy(error, sizeof(error), tb_mi_find(record->results, "msg"));
            if (*error)
                (void)snprintf(r->edit.message, sizeof(r->edit.message),
                               "UNCERTAIN: %.95s Refresh or reconnect.", error);
        }
        ++r->revision;
        return true;
    }

    if (event->cookie == REG_EDIT_DELETE) {
        r->edit_object = false;
        r->edit_phase  = 0;
        if (!okay) {
            r->edit_broken = true;
            (void)snprintf(r->edit.message, sizeof(r->edit.message),
                           "Register probe cleanup failed; reconnect before another edit.");
        }
        ++r->revision;
        return true;
    }

    /* A rejected/corrupt creation reply cannot establish ownership of that name.
     * Leave any pre-existing or uncertain object alone until the session ends. */
    if (event->cookie == REG_EDIT_CREATE &&
        (!okay || !edit_string(tb_mi_find(record->results, "name"), "tb_register_edit"))) {
        r->edit_broken = true;
        r->edit_object = false;
    }

    if (!okay || !same(event->generation, r->edit_generation) || !edit_identity(r)) {
        if (r->edit_cancelled) {
            r->edit_phase = r->edit_object ? REG_EDIT_DELETE : 0;
        } else {
            edit_fail(r, "Register probe failed, is unsupported or became stale; no write was sent.");
        }
        return true;
    }

    const TbMiValue_t *results = record->results;
    switch (event->cookie) {
    case REG_EDIT_CAPABILITY: {
        const TbMiValue_t *command = tb_mi_find(results, "command");
        if (!command || command->type != TB_MI_TUPLE ||
            !edit_string(tb_mi_find(command->children, "exists"), "true")) {
            edit_fail(r,
                      "GDB does not report the structured register-write command; editing is unavailable.");
        } else {
            r->edit_phase = REG_EDIT_NAME;
        }
        break;
    }
    case REG_EDIT_NAME: {
        const TbMiValue_t *names = tb_mi_find(results, "register-names");
        if (!names || names->type != TB_MI_LIST || !names->children || names->children->next ||
            !edit_string(names->children, r->edit.name)) {
            edit_fail(r, "GDB register ID/name binding changed or is unavailable; no write was sent.");
        } else {
            r->edit_phase = REG_EDIT_PERMISSION;
        }
        break;
    }
    case REG_EDIT_PERMISSION:
        if (!edit_string(tb_mi_find(results, "value"), "on")) {
            edit_fail(r, "GDB register writes are disabled; the frontend does not enable them implicitly.");
        } else {
            r->edit_phase =
                r->edit.state == TB_REGISTER_EDIT_CHECKING ? REG_EDIT_ATTRIBUTES : REG_EDIT_CREATE;
        }
        break;
    case REG_EDIT_CREATE:
        if (!edit_string(tb_mi_find(results, "name"), "tb_register_edit") ||
            !edit_string(tb_mi_find(results, "numchild"), "0")) {
            edit_fail(
                r, "GDB did not establish a scalar register object; wide/compound registers are excluded.");
        } else {
            r->edit_phase = REG_EDIT_FREEZE;
        }
        break;
    case REG_EDIT_FREEZE:
        r->edit_phase = REG_EDIT_ATTRIBUTES;
        break;
    case REG_EDIT_ATTRIBUTES:
        if (!edit_string(tb_mi_find(results, "attr"), "editable")) {
            edit_fail(r, "GDB reports this register object as noneditable; no write was sent.");
        } else {
            r->edit_phase = r->edit.state == TB_REGISTER_EDIT_CHECKING ? REG_EDIT_WRITE : REG_EDIT_SIZE;
        }
        break;
    case REG_EDIT_SIZE: {
        char     value[32];
        bool     complete = copy(value, sizeof(value), tb_mi_find(results, "value"));
        unsigned bytes    = complete && value[0] >= '1' && value[0] <= '8' && !value[1]
            ? (unsigned)(value[0] - '0') : 0;

        if (!bytes ||
            !edit_hex(r->canonical[r->edit_row], (unsigned)bytes * 8, r->edit.old_value)) {
            edit_fail(r, "Only complete 8–64-bit scalar register samples on octet targets can be edited.");
        } else {
            r->edit.bits  = (unsigned)bytes * 8;
            r->edit.state = TB_REGISTER_EDIT_READY;
            r->edit_phase = 0;
            (void)snprintf(
                r->edit.message, sizeof(r->edit.message),
                "GDB reports an editable register object; physical writability requires verification.");
        }
        break;
    }
    default:
        break;
    }
    ++r->revision;

    return true;
}


static void edit_pump(TbRegisters_t *r)
{
    if (r->edit_complete) {
        r->edit_complete = false;
        tb_context_write_finish(r->context, REG_EDIT_WRITE, r->edit_outcome);
        ++r->version;
        for (size_t i = 0; i < r->count; ++i) {
            r->rows[i].fresh      = false;
            r->canonical_fresh[i] = false;
        }
        r->edit_phase = r->edit_object ? REG_EDIT_DELETE : 0;
    }

    if (!r->edit_waiting &&
        (r->edit.state == TB_REGISTER_EDIT_PROBING || r->edit.state == TB_REGISTER_EDIT_READY ||
         r->edit.state == TB_REGISTER_EDIT_CHECKING) &&
        !edit_identity(r)) {
        edit_fail(r, "Register identity/context changed; cancel and review again. No write was sent.");
    }

    if (!r->edit_phase || r->edit_waiting || r->waiting || tb_gdb_pending(r->gdb))
        return;

    const char *args[7];
    size_t      count     = 0;
    const char *operation = NULL;
    char        expression[160];
    char        id[16];
    unsigned    job = r->edit_phase;

    switch (job) {
    case REG_EDIT_CAPABILITY:
        operation     = "-info-gdb-mi-command";
        args[count++] = "data-write-register-values";
        break;
    case REG_EDIT_NAME:
        operation = "-data-list-register-names";
        (void)snprintf(id, sizeof(id), "%u", r->edit.number);
        args[count++] = id;
        break;
    case REG_EDIT_PERMISSION:
        operation     = "-gdb-show";
        args[count++] = "may-write-registers";
        break;
    case REG_EDIT_CREATE:
        operation     = "-var-create";
        args[count++] = "--thread";
        args[count++] = r->edit.thread;
        args[count++] = "--frame";
        args[count++] = "0";
        args[count++] = "tb_register_edit";
        args[count++] = "*";
        (void)snprintf(expression, sizeof(expression), "$%s", r->edit.name);
        args[count++] = expression;
        break;
    case REG_EDIT_FREEZE:
        operation     = "-var-set-frozen";
        args[count++] = "tb_register_edit";
        args[count++] = "1";
        break;
    case REG_EDIT_ATTRIBUTES:
        operation     = "-var-show-attributes";
        args[count++] = "tb_register_edit";
        break;
    case REG_EDIT_SIZE:
        operation     = "-data-evaluate-expression";
        args[count++] = "--thread";
        args[count++] = r->edit.thread;
        args[count++] = "--frame";
        args[count++] = "0";
        args[count++] = "--language";
        args[count++] = "c";
        (void)snprintf(expression, sizeof(expression), "(unsigned int)sizeof($%s)", r->edit.name);
        args[count++] = expression;
        break;
    case REG_EDIT_DELETE:
        operation     = "-var-delete";
        args[count++] = "tb_register_edit";
        break;
    case REG_EDIT_WRITE:
        if (!edit_identity(r) || !tb_context_write_begin(r->context, REG_EDIT_WRITE)) {
            edit_fail(r, "Register dispatch interlocked or proof expired; no write was sent.");
            return;
        }
        operation     = "-data-write-register-values";
        args[count++] = "--thread";
        args[count++] = r->edit.thread;
        args[count++] = "--frame";
        args[count++] = "0";
        args[count++] = "x";
        (void)snprintf(id, sizeof(id), "%u", r->edit.number);
        args[count++] = id;
        args[count++] = r->edit.new_value;

        r->baseline_ok[r->edit_row] = true;
        memcpy(r->baseline[r->edit_row], r->canonical[r->edit_row], sizeof(r->baseline[r->edit_row]));
        break;
    default:
        return;
    }

    r->edit_waiting = tb_gdb_request(r->gdb, operation, args, count,
                                    job == REG_EDIT_DELETE ? TB_GDB_SESSION : TB_GDB_CONTEXT,
                                    job, 3000, NULL);
    if (r->edit_waiting) {
        if (job == REG_EDIT_CREATE)
            r->edit_object = true;
        if (job == REG_EDIT_WRITE) {
            r->edit.state = TB_REGISTER_EDIT_PENDING;
            (void)snprintf(r->edit.message, sizeof(r->edit.message),
                           "Register write pending; no retry or implicit read-back.");
        }
    } else {
        if (job == REG_EDIT_WRITE)
            tb_context_write_finish(r->context, REG_EDIT_WRITE, TB_WRITE_ACKNOWLEDGED);
        edit_fail(r, "Register operation could not be queued; no retry was sent.");
        if (job == REG_EDIT_DELETE) {
            r->edit_object = false;
            r->edit_phase  = 0;
            r->edit_broken = true;
        }
    }
    ++r->revision;
}
