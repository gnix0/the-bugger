#include "stack.h"
#include "code.h"
#include "registers.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STACK_NONE, STACK_NAMES = 9701, STACK_ANCHOR, STACK_FRAME, STACK_LOCALS,
       STACK_GUARD, STACK_DISABLE, STACK_VERIFY, STACK_ADDRESS };
struct TbStack {
    TbGdb *gdb;
    TbContext *context;
    TbMemory *memory;
    TbBrowseCache *cache;
    TbStackView views[TB_STACK_VIEWS];
    TbStackDeclaration declarations[TB_STACK_VIEWS][TB_CONTEXT_THREADS];
    uint64_t lives[TB_STACK_VIEWS], versions[TB_STACK_VIEWS], revision, sequence;
    TbGdbGeneration seen, attempted, origin;
    uint64_t thread_lifetime, request_life, request_version, anchor_sp, anchor_lifetime;
    bool anchor_valid;
    char thread[32], architecture[64], variable[96];
    unsigned bits, job, request_job;
    TbMemoryOrder order;
    int owner, frame, sp_id;
    bool octets, waiting, canceled, catalogue, attempted_valid;
};
static bool valid(int view) { return view >= 0 && view < TB_STACK_VIEWS; }
static bool same(TbGdbGeneration a, TbGdbGeneration b)
{ return a.session == b.session && a.stop == b.stop && a.context == b.context; }
static bool raw(char *out, size_t capacity, const TbMiValue *value)
{
    if (!value || value->type != TB_MI_STRING || value->string.length >= capacity ||
        memchr(value->string.data, 0, value->string.length)) { return false; }
    memcpy(out, value->string.data, value->string.length); out[value->string.length] = 0; return true;
}
static bool literal(const char *text, uint64_t *value)
{
    if (!text || !*text || *text == '+' || *text == '-' || *text == ' ' || *text == '\t') { return false; }
    unsigned base = text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10;
    if (base == 16) { text += 2; } if (!*text) { return false; }
    uint64_t result = 0;
    for (; *text; ++text) {
        unsigned digit = *text >= '0' && *text <= '9' ? (unsigned)(*text - '0') :
                         *text >= 'a' && *text <= 'f' ? (unsigned)(*text - 'a' + 10) :
                         *text >= 'A' && *text <= 'F' ? (unsigned)(*text - 'A' + 10) : 99;
        if (digit >= base || result > (UINT64_MAX - digit) / base) { return false; }
        result = result * base + digit;
    }
    *value = result; return true;
}
static bool footprint(const TbStack *s, uint64_t address, unsigned size)
{
    return s->octets && s->bits && s->bits <= 64 && size && size <= TB_MEMORY_BYTES &&
           address <= UINT64_MAX - size &&
           (s->bits == 64 || (address < (UINT64_C(1) << s->bits) && size <= (UINT64_C(1) << s->bits) - address));
}
static void changed(TbStack *s, TbStackView *v) { ++s->revision; if (v) { ++v->revision; } }
static bool fresh(const TbStack *s)
{
    const TbContextSnapshot *c = tb_context_snapshot(s->context);
    return tb_gdb_state(s->gdb) == TB_GDB_STOPPED && c->ready && !c->stale && same(c->generation, tb_gdb_generation(s->gdb));
}
static bool context_valid(const TbStack *s, const TbStackView *v)
{
    const TbContextSnapshot *c = tb_context_snapshot(s->context);
    return fresh(s) && v->lifetime && tb_context_thread_lifetime(s->context, c->selected_thread) == v->lifetime &&
           strcmp(v->thread, c->selected_thread) == 0;
}
static void note(TbStack *s, TbStackView *v, const char *message)
{ (void)snprintf(v->message, sizeof v->message, "%s", message); changed(s, v); }
static bool position(TbStack *s, TbStackView *v)
{
    unsigned half = v->size / 2;
    if (!v->anchor_valid || v->sp < half || !footprint(s, v->sp - half, v->size)) {
        v->positioned = false;
        (void)snprintf(v->anchor_error, sizeof v->anchor_error, "Frame-0 SP unavailable or centered window overflows; no substitute anchor.");
        return false;
    }
    v->start = v->sp - half; v->positioned = true; v->historical = false; return true;
}
TbStack *tb_stack_create(TbGdb *gdb, TbContext *context, TbMemory *memory, TbBrowseCache *cache)
{
    TbStack *s = calloc(1, sizeof *s);
    if (s) { s->gdb = gdb; s->context = context; s->memory = memory; s->cache = tb_browse_retain(cache); tb_stack_reset(s); }
    return s;
}
void tb_stack_destroy(TbStack *s) { if (s) { tb_browse_release(s->cache); free(s); } }
void tb_stack_clear(TbStack *s, int id)
{
    if (!valid(id)) { return; }
    tb_memory_client_cancel(s->memory, tb_memory_stack_client(id));
    tb_memory_clear(s->memory, tb_memory_stack_client(id));
    tb_browse_clear(s->cache, TB_BROWSE_STACK, id);
    memset(&s->views[id], 0, sizeof s->views[id]);
    memset(s->declarations[id], 0, sizeof s->declarations[id]);
    s->views[id].size = 128; s->views[id].grouping = s->order != TB_MEMORY_UNKNOWN && (s->bits == 16 || s->bits == 32 || s->bits == 64) ? s->bits / 8 : 1; s->views[id].follow = true;
    ++s->lives[id]; ++s->versions[id]; changed(s, &s->views[id]);
    if (s->owner == id && (s->job || s->waiting)) { s->canceled = true; if (!s->waiting) { s->job = STACK_NONE; } }
}
void tb_stack_reset(TbStack *s)
{
    for (int i = 0; i < TB_STACK_VIEWS; ++i) { tb_stack_clear(s, i); }
    s->seen = (TbGdbGeneration){0}; s->attempted_valid = false;
    s->catalogue = s->waiting = s->canceled = false; s->sp_id = -1; s->job = STACK_NONE; s->owner = -1;
    s->bits = 0; s->octets = false; s->order = TB_MEMORY_UNKNOWN; s->architecture[0] = 0; ++s->revision;
}
void tb_stack_visible(TbStack *s, int id, bool visible)
{
    if (!valid(id)) { return; }
    TbStackView *v = &s->views[id];
    if (v->visible && !visible && v->reading) { tb_stack_cancel(s, id); }
    v->visible = visible; tb_memory_visible(s->memory, tb_memory_stack_client(id), visible);
    bool any = false; for (int i = 0; i < TB_STACK_VIEWS; ++i) { any = any || s->views[i].visible; }
    tb_context_stack(s->context, any);
}
void tb_stack_invalidate(TbStack *s)
{
    s->attempted_valid = false; s->anchor_valid = false; s->catalogue = false; s->sp_id = -1;
    for (int i = 0; i < TB_STACK_VIEWS; ++i) {
        TbStackView *v = &s->views[i]; v->anchor_valid = false; if (v->follow) { v->positioned = false; } v->point_count = 0; v->names_ready = false;
        v->guard_confirmation = false; ++s->versions[i];
        if (v->reading) { tb_stack_cancel(s, i); }
        changed(s, v);
    }
    if (s->waiting || s->job) { s->canceled = true; if (!s->waiting) { s->job = STACK_NONE; } }
}
void tb_stack_target(TbStack *s, bool octets, unsigned bits, TbMemoryOrder order, const char *architecture)
{
    if (!architecture) { architecture = ""; }
    if (s->octets == octets && s->bits == bits && s->order == order && strcmp(s->architecture, architecture) == 0) { return; }
    tb_stack_invalidate(s); s->octets = octets; s->bits = bits; s->order = order;
    (void)snprintf(s->architecture, sizeof s->architecture, "%s", architecture);
    unsigned group = order != TB_MEMORY_UNKNOWN && (bits == 16 || bits == 32 || bits == 64) ? bits / 8 : 1;
    for (int i = 0; i < TB_STACK_VIEWS; ++i) { s->views[i].grouping = group; }
}
const TbStackView *tb_stack_view(const TbStack *s, int id) { return valid(id) ? &s->views[id] : NULL; }
uint64_t tb_stack_revision(const TbStack *s) { return s->revision; }
void tb_stack_manual(TbStack *s, int id)
{
    if (valid(id) && s->views[id].follow) { s->views[id].follow = false; changed(s, &s->views[id]); }
}
bool tb_stack_follow(TbStack *s, int id)
{
    if (!valid(id)) { return false; } TbStackView *v = &s->views[id];
    tb_stack_cancel(s, id); v->follow = true; v->historical = false;
    if (!context_valid(s, v) || !v->anchor_valid) { v->positioned = false; s->attempted_valid = false; }
    else { (void)position(s, v); }
    if (s->sp_id < 0) { s->catalogue = false; }
    note(s, v, "Follow frame-0 SP restored; no bytes read."); return true;
}
bool tb_stack_navigate(TbStack *s, int id, const char *text)
{
    if (!valid(id) || !text) { return false; } TbStackView *v = &s->views[id]; uint64_t address, distance;
    bool offset = *text == '+' || *text == '-';
    if (!literal(text + (offset ? 1 : 0), &distance)) { note(s, v, "Use a literal address or signed SP offset; no expressions."); return false; }
    address = distance;
    if (offset) {
        if (!context_valid(s, v) || !v->anchor_valid || (*text == '-' ? v->sp < distance : v->sp > UINT64_MAX - distance)) {
            note(s, v, "Fresh frame-0 SP required; offset unavailable/overflow."); return false;
        }
        address = *text == '-' ? v->sp - distance : v->sp + distance;
    }
    if (!footprint(s, address, v->size)) { note(s, v, "Window exceeds target width; no alignment or clipping applied."); return false; }
    tb_stack_cancel(s, id); v->start = address; v->positioned = true; v->follow = v->historical = false;
    ++s->versions[id]; note(s, v, "Window moved; uncached bytes remain unread. Use Read explicitly."); return true;
}
bool tb_stack_page(TbStack *s, int id, int direction)
{
    if (!valid(id) || !direction) { return false; } TbStackView *v = &s->views[id];
    if (!v->positioned || (direction < 0 ? v->start < v->size : v->start > UINT64_MAX - v->size)) { return false; }
    uint64_t next = direction < 0 ? v->start - v->size : v->start + v->size;
    char text[32]; (void)snprintf(text, sizeof text, "0x%" PRIx64, next); return tb_stack_navigate(s, id, text);
}
bool tb_stack_size(TbStack *s, int id, unsigned bytes)
{
    if (!valid(id) || !bytes || bytes > TB_MEMORY_BYTES) { return false; } TbStackView *v = &s->views[id];
    if (v->follow && v->anchor_valid) {
        if (v->sp < bytes / 2 || !footprint(s, v->sp - bytes / 2, bytes)) { note(s, v, "Centered window overflows; size unchanged."); return false; }
    } else if (v->positioned && !footprint(s, v->start, bytes)) { note(s, v, "Window overflows; size unchanged."); return false; }
    tb_stack_cancel(s, id); v->size = bytes; if (v->follow) { (void)position(s, v); }
    ++s->versions[id]; note(s, v, "Window size changed; no bytes read."); return true;
}
bool tb_stack_group(TbStack *s, int id, unsigned bytes)
{
    if (!valid(id) || (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) ||
        (bytes > 1 && (s->order == TB_MEMORY_UNKNOWN || (s->views[id].historical && s->views[id].sample.order == TB_MEMORY_UNKNOWN)))) { return false; }
    s->views[id].grouping = bytes; note(s, &s->views[id], "Grouping changed; no bytes read."); return true;
}
bool tb_stack_read(TbStack *s, int id)
{
    if (!valid(id)) { return false; } TbStackView *v = &s->views[id];
    if (!v->visible || v->historical || !v->positioned || !context_valid(s, v) || !footprint(s, v->start, v->size) ||
        !tb_memory_client_read(s->memory, tb_memory_stack_client(id), v->start, v->size, v->thread)) {
        note(s, v, "Read requires a current stopped thread and numeric window; leave history with Follow SP/navigation."); return false;
    }
    v->reading = true; v->cancel_requested = false; note(s, v, "Explicit byte read requested; no automatic refresh."); return true;
}
void tb_stack_cancel(TbStack *s, int id)
{
    if (!valid(id)) { return; } TbStackView *v = &s->views[id];
    bool active = v->reading || (s->owner == id && (s->job || s->waiting || v->guard_confirmation));
    tb_memory_client_cancel(s->memory, tb_memory_stack_client(id)); v->reading = false;
    if (s->owner == id && (s->job || s->waiting || v->guard_confirmation)) {
        s->canceled = true; if (!s->waiting) { s->job = STACK_NONE; }
    }
    v->guard_confirmation = false;
    if (active) {
        v->cancel_requested = tb_memory_client_busy(s->memory, tb_memory_stack_client(id)) || (s->owner == id && s->waiting);
        note(s, v, v->cancel_requested ? "Cancel requested; outstanding command may still complete. Target not interrupted." : "Canceled locally; no further dispatch.");
    }
}
static bool showing(const TbStackView *v)
{
    return v->sample.sequence && v->sample.address == v->start && (v->historical || v->sample.lifetime == v->lifetime);
}
bool tb_stack_stale(const TbStack *s, int id)
{
    if (!valid(id)) { return true; } const TbStackView *v = &s->views[id];
    return !showing(v) || v->historical || !context_valid(s, v) || !same(v->sample.generation, tb_gdb_generation(s->gdb));
}
size_t tb_stack_rows(const TbStack *s, int id)
{
    if (!valid(id) || !s->views[id].positioned) { return 0; } const TbStackView *v = &s->views[id];
    return (v->size + v->grouping - 1) / v->grouping;
}
bool tb_stack_cached(TbStack *s, int id, int direction)
{
    if (!valid(id) || !direction) { return false; } TbStackView *v = &s->views[id]; TbBrowseSample sample;
    uint64_t sequence = v->historical ? v->sample.sequence : direction < 0 ? UINT64_MAX : 0;
    TbBrowseKey key = {.kind = TB_BROWSE_STACK, .view = id, .begin = sequence};
    if (!tb_browse_history(s->cache, key, direction, &sample) || sample.size != sizeof(TbStackSample)) {
        note(s, v, "No retained sample in that direction; no read queued."); return false;
    }
    tb_stack_cancel(s, id); memcpy(&v->sample, sample.data, sizeof v->sample);
    if (v->sample.order == TB_MEMORY_UNKNOWN) { v->grouping = 1; }
    v->start = v->sample.address; v->size = v->sample.size; v->positioned = true; v->historical = true; v->follow = false;
    note(s, v, "Deliberate cached inspection; original thread lifetime and stop provenance retained."); return true;
}
static bool point_here(const TbStack *s, const TbStackView *v, const TbStackPoint *p)
{
    return context_valid(s, v) && p->lifetime == v->lifetime && same(p->generation, tb_gdb_generation(s->gdb));
}
static void offset_text(const TbStack *s, const TbStackView *v, uint64_t address, char *text, size_t capacity)
{
    if (!v->anchor_valid || !context_valid(s, v) || (showing(v) && v->sample.lifetime != v->lifetime)) {
        (void)snprintf(text, capacity, "SP offset unavailable"); return;
    }
    (void)snprintf(text, capacity, "%c0x%" PRIx64, address < v->sp ? '-' : '+', address < v->sp ? v->sp - address : address - v->sp);
}
void tb_stack_line(const TbStack *s, int id, size_t row, char *text, size_t capacity)
{
    if (!capacity) { return; } *text = 0;
    if (!valid(id) || row >= tb_stack_rows(s, id)) { return; }
    const TbStackView *v = &s->views[id]; unsigned first = (unsigned)row * v->grouping;
    unsigned count = v->size - first < v->grouping ? v->size - first : v->grouping;
    char offset[64], value[96]; uint64_t address = v->start + first;
    offset_text(s, v, address, offset, sizeof offset);
    bool available = showing(v) && first + count <= v->sample.size;
    bool complete = available && count == v->grouping;
    for (unsigned i = 0; i < count && available; ++i) { complete = complete && v->sample.valid[first + i]; }
    if (!available) { (void)snprintf(value, sizeof value, "UNREAD"); }
    else if (complete) {
        uint64_t number = 0;
        for (unsigned i = 0; i < count; ++i) {
            unsigned at = v->sample.order == TB_MEMORY_LITTLE ? count - 1 - i : i;
            number = number * UINT64_C(256) + v->sample.bytes[first + at];
        }
        (void)snprintf(value, sizeof value, "0x%0*" PRIx64, (int)(count * 2), number);
    } else {
        size_t used = 0;
        for (unsigned i = 0; i < count && used + 4 < sizeof value; ++i) {
            int n = v->sample.valid[first + i] ? snprintf(value + used, sizeof value - used, "%02x ", v->sample.bytes[first + i]) :
                                               snprintf(value + used, sizeof value - used, "?? ");
            if (n > 0) { used += (size_t)n; }
        }
        (void)snprintf(value + used, sizeof value - used, "[%s]", count < v->grouping ? "incomplete group" : "missing bytes");
    }
    /* Keep columns stable for every row in the complete requested window. */
    char first_offset[64], last_offset[64];
    offset_text(s, v, v->start, first_offset, sizeof first_offset);
    offset_text(s, v, v->start + v->size - 1, last_offset, sizeof last_offset);
    int offset_width = (int)strlen(first_offset);
    if ((int)strlen(last_offset) > offset_width) { offset_width = (int)strlen(last_offset); }
    (void)snprintf(text, capacity, "0x%016" PRIx64 " %*s %-18s", address, offset_width, offset, value);
    for (size_t i = 0; i < v->point_count; ++i) {
        const TbStackPoint *p = &v->points[i];
        if (!point_here(s, v, p) || (showing(v) && v->sample.lifetime != p->lifetime) || p->address < address || p->address - address >= count) { continue; }
        size_t used = strlen(text);
        if (used < capacity) { (void)snprintf(text + used, capacity - used, " | @%zu %s", i + 1, p->label); }
    }
}
const TbStackDeclaration *tb_stack_declaration(const TbStack *s, int id)
{
    if (!valid(id)) { return NULL; } const TbStackView *v = &s->views[id];
    if (!tb_context_lifetime_live(s->context, v->lifetime)) { return NULL; }
    for (size_t i = 0; i < TB_CONTEXT_THREADS; ++i) {
        const TbStackDeclaration *d = &s->declarations[id][i];
        if (d->used && d->lifetime == v->lifetime) { return d; }
    }
    return NULL;
}
bool tb_stack_declare(TbStack *s, int id, uint64_t low, uint64_t high, bool upward)
{
    if (!valid(id) || low >= high || !s->bits || s->bits > 64 ||
        (s->bits < 64 && high > (UINT64_C(1) << s->bits)) || !context_valid(s, &s->views[id])) { return false; }
    const TbStackDeclaration *current = tb_stack_declaration(s, id);
    for (size_t i = 0; i < TB_CONTEXT_THREADS; ++i) {
        TbStackDeclaration *d = &s->declarations[id][i];
        if (d == current || (!current && !d->used)) {
            *d = (TbStackDeclaration){.used = true, .low = low, .high = high, .upward = upward, .lifetime = s->views[id].lifetime};
            note(s, &s->views[id], "Bounds declared for this view/thread lifetime; no read permission or ownership established."); return true;
        }
    }
    return false;
}
void tb_stack_undeclare(TbStack *s, int id)
{
    if (!valid(id)) { return; }
    for (size_t i = 0; i < TB_CONTEXT_THREADS; ++i) {
        if (s->declarations[id][i].lifetime == s->views[id].lifetime) { memset(&s->declarations[id][i], 0, sizeof s->declarations[id][i]); }
    }
    note(s, &s->views[id], "Declaration removed; verified bounds remain unknown.");
}
void tb_stack_bounds(const TbStack *s, int id, char *text, size_t capacity)
{
    if (!valid(id)) { if (capacity) { *text = 0; } return; }
    const TbStackView *v = &s->views[id]; const TbStackDeclaration *d = tb_stack_declaration(s, id);
    if (v->historical && v->sample.lifetime != v->lifetime) {
        (void)snprintf(text, capacity, "Verified bounds: unknown. Cached sample belongs to another thread lifetime; current SP/declaration is not applied."); return;
    }
    if (!d) { (void)snprintf(text, capacity, "Verified bounds: unknown. No declared interval for this view/thread lifetime."); return; }
    char relationship[160];
    if (!v->anchor_valid || !context_valid(s, v)) { (void)snprintf(relationship, sizeof relationship, "Fresh frame-0 SP unavailable; displacement unknown."); }
    else if (v->sp < d->low || v->sp > d->high) { (void)snprintf(relationship, sizeof relationship, "SP is %s the declared endpoint span.", v->sp < d->low ? "below" : "above"); }
    else { (void)snprintf(relationship, sizeof relationship, "SP displacement from declared start: %" PRIu64 " bytes.", d->upward ? v->sp - d->low : d->high - v->sp); }
    (void)snprintf(text, capacity, "Verified bounds: unknown. Declared [0x%" PRIx64 ", 0x%" PRIx64 "), growth %s. %s Declaration establishes neither ownership nor access permission; endpoints need not be readable bytes.",
        d->low, d->high, d->upward ? "upward" : "downward", relationship);
}
void tb_stack_details(const TbStack *s, int id, size_t row, char *text, size_t capacity)
{
    if (!capacity) { return; } *text = 0;
    if (!valid(id)) { (void)snprintf(text, capacity, "No selected Stack row."); return; }
    if (row >= tb_stack_rows(s, id)) {
        const TbStackView *v = &s->views[id];
        (void)snprintf(text, capacity, "Frame-0 anchor unavailable. No selected Stack row. Verified bounds: unknown. %s %s %s", v->anchor_error, v->message, v->annotation_error); return;
    }
    const TbStackView *v = &s->views[id]; char line[4096], bounds[768], anchor[80];
    if (v->anchor_valid && context_valid(s, v)) { (void)snprintf(anchor, sizeof anchor, "GDB-reported SP 0x%" PRIx64, v->sp); }
    else { (void)snprintf(anchor, sizeof anchor, "unavailable (no substitute)"); }
    tb_stack_line(s, id, row, line, sizeof line); tb_stack_bounds(s, id, bounds, sizeof bounds);
    (void)snprintf(text, capacity, "%s. Window [0x%" PRIx64 ", 0x%" PRIx64 ") requested %u bytes; grouping %u bytes. Frame-0 anchor: %s. Current thread T%s lifetime %" PRIu64 ". %s ",
        line, v->start, v->start + v->size, v->size, v->grouping, anchor, v->thread, v->lifetime, bounds);
    size_t used = strlen(text);
    if (used < capacity && showing(v)) {
        (void)snprintf(text + used, capacity - used, "Sample provenance: T%s lifetime %" PRIu64 ", session %" PRIu64 " stop %" PRIu64 " context %" PRIu64 ", monotonic sample time %" PRIu64 " ms, %s endian; %s. ",
            v->sample.thread, v->sample.lifetime, v->sample.generation.session, v->sample.generation.stop, v->sample.generation.context,
            v->sample.sampled_ms, v->sample.order == TB_MEMORY_LITTLE ? "little" : v->sample.order == TB_MEMORY_BIG ? "big" : "unknown",
            tb_stack_stale(s, id) ? "STALE/HISTORICAL" : "current stopped-context sample");
    } else if (used < capacity) { (void)snprintf(text + used, capacity - used, "No sample for this window; bytes UNREAD. "); }
    uint64_t address = v->start + row * v->grouping;
    for (size_t i = 0; i < v->point_count; ++i) {
        const TbStackPoint *p = &v->points[i];
        if (!point_here(s, v, p) || p->address < address || p->address - address >= v->grouping ||
            (showing(v) && v->sample.lifetime != p->lifetime)) { continue; }
        used = strlen(text); if (used >= capacity) { break; }
        (void)snprintf(text + used, capacity - used, "Point @%zu %s at 0x%" PRIx64 ": thread lifetime %" PRIu64 ", frame %d, session %" PRIu64 " stop %" PRIu64 " context %" PRIu64 ". %s ",
            i + 1, p->label, p->address, p->lifetime, p->frame, p->generation.session, p->generation.stop, p->generation.context,
            p->variable ? "Address point only; no extent, stack membership or ownership established. References may identify referents." : "GDB-reported selected-frame SP; callers may be reconstructed. Separate from the frame-0 anchor.");
    }
    used = strlen(text); if (used < capacity) { (void)snprintf(text + used, capacity - used, "Raw numeric values carry no pointer interpretation. %s %s %s", v->message, v->anchor_error, v->annotation_error); }
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): view ID and request kind are separate validated domains.
static bool begin(TbStack *s, int id, unsigned job)
{
    if (!valid(id) || s->waiting || s->job || !s->views[id].visible || !context_valid(s, &s->views[id]) || tb_context_snapshot(s->context)->selected.level < 0) { return false; }
    TbStackView *v = &s->views[id];
    if (job != STACK_LOCALS && v->point_count >= TB_STACK_POINTS) { note(s, v, "32 points retained; remove a point before querying another. No automatic replacement."); return false; }
    for (int i = 0; i < TB_STACK_VIEWS; ++i) { if (s->views[i].guard_confirmation) { note(s, v, "Complete or cancel the pending no-call confirmation first."); return false; } }
    const TbContextSnapshot *c = tb_context_snapshot(s->context);
    s->owner = id; s->job = job; s->origin = c->generation; s->thread_lifetime = v->lifetime;
    s->request_life = s->lives[id]; s->request_version = s->versions[id]; s->frame = c->selected.level;
    (void)snprintf(s->thread, sizeof s->thread, "%s", v->thread);
    s->canceled = false; v->guard_confirmation = false; v->annotation_error[0] = 0; changed(s, v); return true;
}
bool tb_stack_frame_point(TbStack *s, int id) { return begin(s, id, STACK_FRAME); }
bool tb_stack_names(TbStack *s, int id) { return begin(s, id, STACK_LOCALS); }
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): view ID and picker index select independent bounded collections.
bool tb_stack_variable_point(TbStack *s, int id, size_t name)
{
    if (!valid(id)) { return false; } TbStackView *v = &s->views[id];
    if (!v->names_ready || name >= v->name_count || !v->names[name].eligible) {
        (void)snprintf(v->annotation_error, sizeof v->annotation_error, "Name is unsupported, ambiguous or truncated; no address query sent."); changed(s, v); return false;
    }
    if (!begin(s, id, STACK_GUARD)) { return false; }
    (void)snprintf(s->variable, sizeof s->variable, "%s", v->names[name].name); return true;
}
bool tb_stack_disable_calls(TbStack *s, int id)
{
    if (!valid(id) || !s->views[id].guard_confirmation || s->owner != id || s->waiting || s->job ||
        !context_valid(s, &s->views[id]) || !same(s->origin, tb_gdb_generation(s->gdb)) || s->request_life != s->lives[id]) { return false; }
    s->views[id].guard_confirmation = false; s->views[id].annotation_error[0] = 0; s->job = STACK_DISABLE; s->canceled = false; changed(s, &s->views[id]); return true;
}
bool tb_stack_remove_point(TbStack *s, int id, size_t point)
{
    if (!valid(id) || point >= s->views[id].point_count) { return false; }
    TbStackView *v = &s->views[id];
    memmove(v->points + point, v->points + point + 1, (v->point_count - point - 1) * sizeof *v->points);
    --v->point_count; changed(s, v); return true;
}
static bool chain_valid(const TbStack *s)
{
    if (s->canceled || !fresh(s) || !same(s->origin, tb_gdb_generation(s->gdb)) ||
        tb_context_thread_lifetime(s->context, s->thread) != s->thread_lifetime) { return false; }
    if (s->owner < 0) {
        for (int i = 0; i < TB_STACK_VIEWS; ++i) { if (s->views[i].visible) { return true; } }
        return false;
    }
    return valid(s->owner) && s->lives[s->owner] == s->request_life && s->versions[s->owner] == s->request_version &&
           s->views[s->owner].visible && context_valid(s, &s->views[s->owner]);
}
static void finish_error(TbStack *s, const char *message)
{
    if (s->job == STACK_ANCHOR) {
        s->anchor_valid = false;
        for (int i = 0; i < TB_STACK_VIEWS; ++i) {
            TbStackView *v = &s->views[i];
            if (!v->visible || v->lifetime != s->thread_lifetime) { continue; }
            v->anchor_valid = false; if (v->follow) { v->positioned = false; }
            (void)snprintf(v->anchor_error, sizeof v->anchor_error, "%s", message); changed(s, v);
        }
    } else if (valid(s->owner) && s->lives[s->owner] == s->request_life) {
        TbStackView *v = &s->views[s->owner];
        (void)snprintf(v->annotation_error, sizeof v->annotation_error, "%s", message);
        v->guard_confirmation = false; changed(s, v);
    }
    s->job = STACK_NONE;
}
static void anchor_publish(TbStack *s)
{
    for (int i = 0; i < TB_STACK_VIEWS; ++i) {
        TbStackView *v = &s->views[i];
        if (!v->visible || v->lifetime != s->anchor_lifetime) { continue; }
        bool moved = !v->anchor_valid || v->sp != s->anchor_sp;
        v->sp = s->anchor_sp; v->anchor_valid = true; v->anchor_error[0] = 0;
        if (v->follow && (moved || !v->positioned)) { (void)position(s, v); }
        if (moved) { changed(s, v); }
    }
}
static void synchronize(TbStack *s)
{
    TbGdbGeneration generation = tb_gdb_generation(s->gdb);
    bool transitioned = !same(s->seen, generation);
    if (transitioned) {
        s->seen = generation; s->anchor_valid = false;
        for (int i = 0; i < TB_STACK_VIEWS; ++i) {
            TbStackView *v = &s->views[i]; v->anchor_valid = false;
            v->point_count = 0; v->names_ready = false; v->guard_confirmation = false;
            if (v->follow) { v->positioned = false; }
            if (v->reading) { tb_stack_cancel(s, i); }
            ++s->versions[i]; changed(s, v);
        }
    }
    const TbContextSnapshot *c = tb_context_snapshot(s->context);
    for (int i = 0; i < TB_STACK_VIEWS; ++i) {
        for (size_t j = 0; j < TB_CONTEXT_THREADS; ++j) {
            TbStackDeclaration *d = &s->declarations[i][j];
            if (d->used && !tb_context_lifetime_live(s->context, d->lifetime)) { memset(d, 0, sizeof *d); changed(s, &s->views[i]); }
        }
        TbStackView *v = &s->views[i];
        if (v->cancel_requested && !tb_memory_client_busy(s->memory, tb_memory_stack_client(i)) && !(s->waiting && s->owner == i)) {
            v->cancel_requested = false; note(s, v, "Canceled locally; outstanding command settled or timed out. No backend-cancellation claim.");
        }
        if (!fresh(s)) { v->anchor_valid = false; continue; }
        uint64_t lifetime = tb_context_thread_lifetime(s->context, c->selected_thread);
        if (v->lifetime != lifetime || strcmp(v->thread, c->selected_thread) != 0) {
            tb_stack_cancel(s, i); tb_memory_clear(s->memory, tb_memory_stack_client(i));
            tb_memory_visible(s->memory, tb_memory_stack_client(i), v->visible);
            v->lifetime = lifetime; (void)snprintf(v->thread, sizeof v->thread, "%s", c->selected_thread);
            v->follow = true; v->historical = false; v->positioned = v->anchor_valid = false;
            v->point_count = 0; v->names_ready = false; memset(&v->sample, 0, sizeof v->sample);
            ++s->versions[i]; note(s, v, "Thread changed: following frame-0 SP; bytes UNREAD. Old samples require cached inspection.");
        }
        if (v->reading) {
            const TbMemoryView *bytes = tb_memory_view(s->memory, tb_memory_stack_client(i));
            if (!bytes->pending && !tb_memory_client_busy(s->memory, tb_memory_stack_client(i))) {
                v->reading = false;
                if (bytes->ready && same(bytes->generation, generation) && bytes->address == v->start && bytes->data_size == v->size) {
                    TbStackSample *sample = &v->sample; memset(sample, 0, sizeof *sample);
                    sample->address = bytes->address; sample->size = (unsigned)bytes->data_size;
                    sample->generation = bytes->generation; sample->lifetime = v->lifetime; sample->sequence = ++s->sequence;
                    sample->sampled_ms = tb_gdb_now_ms(); sample->order = s->order;
                    (void)snprintf(sample->thread, sizeof sample->thread, "%s", v->thread);
                    memcpy(sample->bytes, bytes->bytes, sizeof sample->bytes); memcpy(sample->valid, bytes->valid, sizeof sample->valid);
                    TbBrowseKey key = {.kind = TB_BROWSE_STACK, .view = i, .begin = sample->sequence, .end = sample->sequence + 1,
                        .epoch = sample->lifetime, .generation = sample->generation};
                    bool retained = tb_browse_store(s->cache, key, sample, sizeof *sample);
                    note(s, v, *bytes->error ? bytes->error : retained ? "Explicit byte sample retained; metadata and numeric values establish no pointer or ownership information." : "Read completed; history allocation failed. No retry.");
                } else { note(s, v, *bytes->error ? bytes->error : "Byte read failed or became obsolete; no retry. Prior sample retained with its provenance."); }
            }
        }
    }
    if (fresh(s) && s->anchor_valid && s->attempted_valid && same(s->attempted, generation)) { anchor_publish(s); }
}
static bool identifier(const char *name)
{
    if (!name || !((*name >= 'A' && *name <= 'Z') || (*name >= 'a' && *name <= 'z') || *name == '_')) { return false; }
    for (++name; *name; ++name) {
        if (!((*name >= 'A' && *name <= 'Z') || (*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') || *name == '_')) { return false; }
    }
    return true;
}
static bool register_value(const TbStack *s, const TbMiRecord *record, uint64_t *address)
{
    const TbMiValue *list = tb_mi_find(record->results, "register-values"); bool found = false;
    if (!list || list->type != TB_MI_LIST) { return false; }
    for (const TbMiValue *entry = list->children; entry; entry = entry->next) {
        char number[32], value[128]; uint64_t id;
        if (entry->type != TB_MI_TUPLE || !raw(number, sizeof number, tb_mi_find(entry->children, "number")) ||
            !literal(number, &id) || id != (unsigned)s->sp_id || found || !raw(value, sizeof value, tb_mi_find(entry->children, "value")) ||
            !literal(value, address) || (s->bits < 64 && *address >= (UINT64_C(1) << s->bits))) { return false; }
        found = true;
    }
    return found;
}
static void parse_catalogue(TbStack *s, const TbMiRecord *record)
{
    s->catalogue = true; s->sp_id = -1;
    const TbMiValue *list = tb_mi_find(record->results, "register-names");
    if (!list || list->type != TB_MI_LIST) { return; }
    const char *canonical = strstr(s->architecture, "xtensa") ? "a1" :
                            strstr(s->architecture, "x86-64") ? "rsp" :
                            strstr(s->architecture, "i386") ? "esp" : "sp";
    int generic = -1, specific = -1; unsigned index = 0;
    for (const TbMiValue *entry = list->children; entry && index < TB_REGISTERS_MAX; entry = entry->next, ++index) {
        char name[96]; if (!raw(name, sizeof name, entry)) { s->sp_id = -1; return; }
        if (strcmp(name, "sp") == 0) { if (generic >= 0) { return; } generic = (int)index; }
        if (strcmp(name, canonical) == 0) { if (specific >= 0) { return; } specific = (int)index; }
    }
    s->sp_id = specific >= 0 ? specific : generic;
}
static void parse_locals(TbStack *s, const TbMiRecord *record)
{
    TbStackView *v = &s->views[s->owner]; v->name_count = 0; v->names_ready = false; v->names_limited = false;
    const TbMiValue *list = tb_mi_find(record->results, "variables");
    if (!list || list->type != TB_MI_LIST) { finish_error(s, "No structured names-only locals/arguments list; no address query sent."); return; }
    for (const TbMiValue *entry = list->children; entry; entry = entry->next) {
        if (v->name_count == TB_STACK_NAMES) { v->names_limited = true; break; }
        TbStackName *name = &v->names[v->name_count++]; memset(name, 0, sizeof *name);
        bool exact = entry->type == TB_MI_TUPLE && raw(name->name, sizeof name->name, tb_mi_find(entry->children, "name"));
        if (!exact) { (void)snprintf(name->name, sizeof name->name, "<unsupported/truncated name>"); }
        name->eligible = exact && identifier(name->name);
    }
    for (size_t i = 0; i < v->name_count; ++i) {
        for (size_t j = i + 1; j < v->name_count; ++j) {
            if (strcmp(v->names[i].name, v->names[j].name) == 0) { v->names[i].eligible = v->names[j].eligible = false; }
        }
        /* A truncated inventory cannot prove that an omitted local does not
         * shadow a retained name. No guessed lookup in an incomplete scope. */
        if (v->names_limited) { v->names[i].eligible = false; }
    }
    v->names_ready = true; s->job = STACK_NONE; changed(s, v);
}
static void add_point(TbStack *s, uint64_t address, bool variable)
{
    TbStackView *v = &s->views[s->owner];
    if (v->point_count >= TB_STACK_POINTS) { finish_error(s, "Point capacity reached; no automatic replacement."); return; }
    v->annotation_error[0] = 0;
    TbStackPoint *p = &v->points[v->point_count++];
    *p = (TbStackPoint){.address = address, .lifetime = v->lifetime, .generation = s->origin, .frame = s->frame, .variable = variable};
    if (variable) { (void)snprintf(p->label, sizeof p->label, "%s", s->variable); }
    else { (void)snprintf(p->label, sizeof p->label, "frame %d SP%s", s->frame, s->frame > 0 ? " (may be reconstructed)" : " (point)"); }
    s->job = STACK_NONE; note(s, v, variable ? "Variable address point retained; no extent, stack membership or ownership established. References may identify referents." : "Selected-frame SP point retained separately from frame-0 anchor; caller SP may be reconstructed.");
}
bool tb_stack_event(TbStack *s, const TbGdbEvent *event)
{
    if (event->kind == TB_GDB_STATE && tb_gdb_state(s->gdb) != TB_GDB_STOPPED) {
        s->anchor_valid = false;
        for (int i = 0; i < TB_STACK_VIEWS; ++i) {
            TbStackView *v = &s->views[i]; v->anchor_valid = false; v->point_count = 0; v->names_ready = false; v->guard_confirmation = false;
            if (v->follow) { v->positioned = false; }
            changed(s, v);
        }
    }
    if (event->kind == TB_GDB_RECORD && event->record && event->record->kind == TB_MI_NOTIFY &&
        (strcmp(event->record->klass, "library-loaded") == 0 || strcmp(event->record->klass, "library-unloaded") == 0 ||
         strcmp(event->record->klass, "memory-changed") == 0 || strcmp(event->record->klass, "thread-group-exited") == 0)) { tb_stack_invalidate(s); }
    if (event->cookie < STACK_NAMES || event->cookie > STACK_ADDRESS ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    s->waiting = false;
    if (!chain_valid(s) || event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED) {
        finish_error(s, s->canceled ? "Canceled result rejected; no further dispatch. Backend cancellation was not established." : "Context/view/thread lifetime changed; annotation/anchor result rejected."); return true;
    }
    const TbMiRecord *record = event->record;
    if (event->kind != TB_GDB_REPLY || !record || strcmp(record->klass, "done") != 0) {
        char message[192] = "Stack metadata query failed/timed out; backend outcome unknown. Re-query explicitly; no retry.";
        const TbMiValue *msg = record ? tb_mi_find(record->results, "msg") : NULL;
        if (msg && msg->type == TB_MI_STRING) { tb_code_safe(message, sizeof message, msg->string.data, msg->string.length); }
        if (s->request_job == STACK_NAMES) { s->catalogue = true; s->sp_id = -1; }
        finish_error(s, message); return true;
    }
    if (s->request_job == STACK_NAMES) {
        parse_catalogue(s, record);
        if (s->sp_id < 0) { finish_error(s, "No unambiguous discovered SP register; anchor unavailable. No PC/FP/CFA substitution."); }
    } else if (s->request_job == STACK_ANCHOR || s->request_job == STACK_FRAME) {
        uint64_t address;
        if (!register_value(s, record, &address)) { finish_error(s, "GDB did not report a complete numeric SP; no substitute anchor/address."); }
        else if (s->request_job == STACK_FRAME) { add_point(s, address, false); }
        else {
            s->anchor_sp = address; s->anchor_lifetime = s->thread_lifetime; s->anchor_valid = true; anchor_publish(s); s->job = STACK_NONE;
        }
    } else if (s->request_job == STACK_LOCALS) { parse_locals(s, record); }
    else if (s->request_job == STACK_DISABLE) { s->job = STACK_VERIFY; }
    else if (s->request_job == STACK_GUARD || s->request_job == STACK_VERIFY) {
        char setting[32] = {0};
        if (raw(setting, sizeof setting, tb_mi_find(record->results, "value")) && strcmp(setting, "off") == 0) { s->job = STACK_ADDRESS; }
        else if (s->request_job == STACK_GUARD && strcmp(setting, "on") == 0) {
            TbStackView *v = &s->views[s->owner]; v->guard_confirmation = true; s->job = STACK_NONE;
            (void)snprintf(v->annotation_error, sizeof v->annotation_error, "may-call-functions is ON. Explicitly disable it session-wide to query; it will not be restored. GDB may still read internally."); changed(s, v);
        } else { finish_error(s, "Cannot verify may-call-functions off; no address query dispatched."); }
    } else if (s->request_job == STACK_ADDRESS) {
        char text[128]; uint64_t address;
        if (!raw(text, sizeof text, tb_mi_find(record->results, "value")) || !literal(text, &address) ||
            (s->bits < 64 && address >= (UINT64_C(1) << s->bits))) {
            finish_error(s, "No supported numeric address: register-only, optimized, piecewise or unavailable locations cannot be invented.");
        } else { add_point(s, address, true); }
    }
    return true;
}
void tb_stack_pump(TbStack *s)
{
    synchronize(s);
    if (s->waiting) { return; }
    if (s->job && !chain_valid(s)) { finish_error(s, "Context changed/canceled before dispatch; no further query."); }
    bool confirmation = false;
    for (int i = 0; i < TB_STACK_VIEWS; ++i) { confirmation = confirmation || s->views[i].guard_confirmation; }
    if (!s->job && !confirmation && fresh(s)) {
        const TbContextSnapshot *c = tb_context_snapshot(s->context); bool any = false;
        for (int i = 0; i < TB_STACK_VIEWS; ++i) { any = any || s->views[i].visible; }
        uint64_t lifetime = tb_context_thread_lifetime(s->context, c->selected_thread);
        if (any && lifetime && s->octets && s->bits && s->bits <= 64 && (!s->attempted_valid || !same(s->attempted, c->generation))) {
            s->owner = -1; s->frame = 0; s->job = STACK_ANCHOR; s->origin = c->generation; s->thread_lifetime = lifetime;
            (void)snprintf(s->thread, sizeof s->thread, "%s", c->selected_thread);
            s->attempted = c->generation; s->attempted_valid = true; s->canceled = false;
        }
    }
    if (!s->job || !chain_valid(s)) { return; }
    unsigned job = s->job;
    if ((job == STACK_FRAME || job == STACK_ANCHOR) && s->sp_id < 0) {
        if (s->catalogue) { finish_error(s, "SP register unavailable; use Follow SP for an explicit metadata retry."); return; }
        job = STACK_NAMES;
    }
    const char *args[9]; size_t count = 0; char frame[24], reg[24], expression[192]; const char *operation;
    if (job == STACK_GUARD || job == STACK_VERIFY || job == STACK_DISABLE) {
        operation = job == STACK_DISABLE ? "-gdb-set" : "-gdb-show";
        args[count++] = "may-call-functions"; if (job == STACK_DISABLE) { args[count++] = "off"; }
    } else if (job == STACK_NAMES) { operation = "-data-list-register-names"; }
    else {
        args[count++] = "--thread"; args[count++] = s->thread; args[count++] = "--frame";
        (void)snprintf(frame, sizeof frame, "%d", job == STACK_ANCHOR ? 0 : s->frame); args[count++] = frame;
        if (job == STACK_LOCALS) {
            operation = "-stack-list-variables"; args[count++] = "--no-frame-filters"; args[count++] = "--no-values";
        } else if (job == STACK_ADDRESS) {
            /* Quoted symbol lookup bypasses macro expansion, and C language
             * prevents C++ operator& dispatch. No arbitrary user expression. */
            operation = "-data-evaluate-expression"; args[count++] = "--language"; args[count++] = "c";
            (void)snprintf(expression, sizeof expression, "(unsigned long long)&'%s'", s->variable); args[count++] = expression;
        } else {
            operation = "-data-list-register-values"; args[count++] = "x";
            (void)snprintf(reg, sizeof reg, "%d", s->sp_id); args[count++] = reg;
        }
    }
    s->request_job = job;
    s->waiting = tb_gdb_request(s->gdb, operation, args, count, TB_GDB_CONTEXT, job, 3000, NULL);
    if (!s->waiting) { finish_error(s, "Stack request rejected; no automatic retry."); }
}
