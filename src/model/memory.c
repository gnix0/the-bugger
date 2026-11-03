#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- POSIX interfaces. */
#include "model/memory.h"
#include "model/code.h"
#include "model/mmio.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MEMORY_RESOLVE = 9401, MEMORY_READ, MEMORY_WRITE };
typedef struct {
    TbMemoryView data;
    bool visible, browse_request;
    unsigned phase, read_length;
    uint64_t version, next_address, lifetime;
    TbGdbGeneration_t origin;
    char thread[32];
    TbMemoryRange ranges[TB_MEMORY_RANGES];
} View;
struct TbMemory {
    TbGdb_t *gdb; TbContext_t *context;
    TbBrowseCache_t *cache; TbMmio_t *mmio;
    uint64_t revision, request_version;
    size_t region_count;
    uint64_t backend_session, backend_epoch, register_epoch, register_address, register_lifetime;
    uint64_t edit_lifetime, edit_backend_epoch, edit_version, edit_address;
    TbGdbGeneration_t request_generation, register_generation, edit_generation;
    uint64_t regions_epoch, edit_regions_epoch;
    TbMemoryBackend backend;
    TbMemoryRegion regions[TB_MEMORY_REGIONS];
    View views[TB_MEMORY_CLIENTS];
    unsigned address_bits;
    int active, cursor;
    TbMemoryOrder order;
    int register_view;
    unsigned register_width;
    TbMemoryEditState edit_state;
    int edit_view;
    unsigned edit_bytes;
    bool waiting, unsupported, backend_present, backend_confirmed, register_queued;
    bool edit_complete, edit_unresolved;
    unsigned char edit_old[TB_MEMORY_ROW], edit_new[TB_MEMORY_ROW];
    char edit_old_text[TB_MEMORY_ROW * 2 + 1], edit_error[192];
};
static TbBrowseKind_e byte_kind(int id) { return id < TB_MEMORY_VIEWS ? TB_BROWSE_MEMORY : TB_BROWSE_STACK_BYTES; }
static bool valid_view(int id) { return id >= 0 && id < TB_MEMORY_CLIENTS; }
static const char *spaces(const char *p) { while (*p == ' ' || *p == '\t') { ++p; } return p; }
static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}
static bool numeric(const char **cursor, uint64_t *value)
{
    const char *p = *cursor; unsigned base = 10, digits = 0; uint64_t n = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    int digit;
    while ((digit = hex((unsigned char)*p)) >= 0 && (unsigned)digit < base) {
        if (n > (UINT64_MAX - (unsigned)digit) / base) { return false; }
        n = n * base + (unsigned)digit; ++digits; ++p;
    }
    if (!digits) { return false; }
    *cursor = p; *value = n; return true;
}
static bool initial(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static const char *identifier(const char *p)
{
    if (!initial((unsigned char)*p)) { return NULL; }
    do { ++p; } while (initial((unsigned char)*p) || (*p >= '0' && *p <= '9'));
    return p;
}
bool tb_memory_expression(const char *expression)
{
    if (!expression || !*expression || strlen(expression) >= 256) { return false; }
    const char *p = spaces(expression); uint64_t ignored;
    if (*p >= '0' && *p <= '9') {
        if (!numeric(&p, &ignored)) { return false; }
    } else {
        if (*p == '&') { p = spaces(p + 1); }
        if (*p == '$') { ++p; }
        p = identifier(p); if (!p) { return false; }
        for (;;) {
            p = spaces(p);
            if (*p == '.' || (p[0] == '-' && p[1] == '>') || (p[0] == ':' && p[1] == ':')) {
                p += *p == '.' ? 1 : 2; p = identifier(spaces(p)); if (!p) { return false; }
            } else if (*p == '[') {
                p = spaces(p + 1); if (!numeric(&p, &ignored)) { return false; }
                p = spaces(p); if (*p++ != ']') { return false; }
            } else { break; }
        }
    }
    p = spaces(p);
    if (*p == '+' || *p == '-') { p = spaces(p + 1); if (!numeric(&p, &ignored)) { return false; } p = spaces(p); }
    return *p == 0;
}
static void error(TbMemory *m, View *v, const char *message)
{
    (void)snprintf(v->data.error, sizeof v->data.error, "%s", message);
    ++v->version; v->data.pending = false; v->data.ready = false; ++m->revision;
}
TbMemory *tb_memory_create(TbGdb_t *gdb, TbContext_t *context)
{
    TbMemory *m = calloc(1, sizeof *m);
    if (m) {
        m->gdb = gdb; m->context = context; m->cache = tb_browse_create(); m->mmio = tb_mmio_create(gdb);
        if (!m->cache || !m->mmio) { tb_browse_release(m->cache); (void)tb_mmio_destroy(m->mmio); free(m); return NULL; } tb_memory_reset(m);
    }
    return m;
}
void tb_memory_destroy(TbMemory *m)
{
    if (m && tb_mmio_destroy(m->mmio)) { tb_browse_release(m->cache); free(m); }
}
void tb_memory_cache(TbMemory *m, TbBrowseCache_t *cache)
{
    if (!cache || m->cache == cache) { return; }
    tb_browse_release(m->cache); m->cache = tb_browse_retain(cache);
}
void tb_memory_clear(TbMemory *m, int id)
{
    if (!valid_view(id)) { return; }
    View *v = &m->views[id]; uint64_t version = v->version + 1, lifetime = v->lifetime + 1;
    tb_browse_clear(m->cache, byte_kind(id), id);
    tb_browse_clear(m->cache, TB_BROWSE_MMIO, id);
    if (m->register_queued && m->register_view == id) { m->register_queued = false; }
    memset(v, 0, sizeof *v); v->version = version; v->lifetime = lifetime; v->data.length = 256; v->data.grouping = 1; ++m->revision;
}
void tb_memory_reset(TbMemory *m)
{
    TbGdb_t *gdb = m->gdb; TbContext_t *context = m->context; uint64_t revision = m->revision;
    TbBrowseCache_t *cache = m->cache; TbMmio_t *mmio = m->mmio; uint64_t epoch = m->backend_epoch + 1;
    memset(m, 0, sizeof *m); m->gdb = gdb; m->context = context; m->cache = cache; m->revision = revision + 1;
    m->mmio = mmio; m->backend_epoch = epoch; m->register_view = -1;
    for (int i = 0; i < TB_MEMORY_CLIENTS; ++i) { tb_memory_clear(m, i); }
}
void tb_memory_visible(TbMemory *m, int id, bool visible)
{
    if (!valid_view(id)) { return; } m->views[id].visible = visible;
    bool required = false; for (int i = 0; i < TB_MEMORY_CLIENTS; ++i) { required = required || m->views[i].visible; }
    tb_context_memory(m->context, required);
}
const TbMemoryView *tb_memory_view(const TbMemory *m, int id) { return valid_view(id) ? &m->views[id].data : NULL; }
uint64_t tb_memory_revision(const TbMemory *m) { return m->revision; }
static bool same(TbGdbGeneration_t a, TbGdbGeneration_t b)
{
    return a.session == b.session && a.stop == b.stop && a.context == b.context;
}
static bool writable_destination(const TbMemory *m, int id, uint64_t address, unsigned bytes)
{
    if (id < 0 || id >= TB_MEMORY_VIEWS || !bytes || bytes > TB_MEMORY_ROW || address > UINT64_MAX - bytes) { return false; }
    uint64_t end = address + bytes; bool region = false, consent = false;
    for (size_t i = 0; i < m->region_count; ++i) {
        const TbMemoryRegion *r = &m->regions[i];
        if (address >= r->end || end <= r->begin) { continue; }
        if (!r->writable) { return false; }
        if (address >= r->begin && end <= r->end) { region = true; }
    }
    for (unsigned i = 0; i < TB_MEMORY_RANGES; ++i) {
        const TbMemoryRange *r = &m->views[id].ranges[i];
        if (r->used && r->policy == TB_MEMORY_ORDINARY && tb_memory_range_authorized(m, id, i) &&
            address >= r->begin && end <= r->end) { consent = true; }
    }
    return !m->unsupported && !m->views[id].data.mmio && region && consent;
}
static bool backend_equal(const TbMemoryBackend *a, const TbMemoryBackend *b)
{
    return a->configuration == b->configuration && a->widths == b->widths &&
           strcmp(a->target, b->target) == 0 && strcmp(a->device, b->device) == 0 &&
           strcmp(a->server, b->server) == 0 && strcmp(a->firmware, b->firmware) == 0;
}
static void backend_invalidate(TbMemory *m)
{
    m->backend_confirmed = false;
    ++m->backend_epoch; tb_browse_clear(m->cache, TB_BROWSE_MMIO, -1);
    if (m->register_queued) {
        View *v = &m->views[m->register_view];
        m->register_queued = false; v->data.register_pending = false;
        (void)snprintf(v->data.register_error, sizeof v->data.register_error,
            "MMIO setup invalidated before dispatch; no read sent.");
    }
    for (int i = 0; i < TB_MEMORY_CLIENTS; ++i) {
        for (unsigned j = 0; j < TB_MEMORY_RANGES; ++j) {
            if (m->views[i].ranges[j].policy == TB_MEMORY_MMIO) { m->views[i].ranges[j].authorized = false; }
        }
    }
    ++m->revision;
}
static bool backend_valid(const TbMemoryBackend *b)
{
    const unsigned allowed = (unsigned)TB_MEMORY_WIDTH8 | (unsigned)TB_MEMORY_WIDTH16 | (unsigned)TB_MEMORY_WIDTH32;
    return b && *b->target && memchr(b->target, 0, sizeof b->target) &&
           *b->device && memchr(b->device, 0, sizeof b->device) &&
           *b->server && memchr(b->server, 0, sizeof b->server) &&
           *b->firmware && memchr(b->firmware, 0, sizeof b->firmware) &&
           b->configuration && b->widths && !(b->widths & ~allowed);
}
bool tb_memory_backend_set(TbMemory *m, const TbMemoryBackend *backend)
{
    if (backend && !backend_valid(backend)) {
        backend_invalidate(m); m->backend_present = false; return false;
    }
    uint64_t session = tb_gdb_generation(m->gdb).session;
    if (backend && m->backend_present && session == m->backend_session && backend_equal(backend, &m->backend)) { return true; }
    /* Copy before invalidation so passing the model's own descriptor is safe. */
    TbMemoryBackend next = {0}; if (backend) { next = *backend; }
    backend_invalidate(m); m->backend = next; m->backend_present = backend != NULL; m->backend_session = session;
    return true;
}
const TbMemoryBackend *tb_memory_backend(const TbMemory *m)
{
    return m->backend_present && m->backend_session == tb_gdb_generation(m->gdb).session ? &m->backend : NULL;
}
bool tb_memory_backend_confirmed(const TbMemory *m)
{
    TbGdbState_e state = tb_gdb_state(m->gdb);
    return m->backend_confirmed && tb_memory_backend(m) &&
           (state == TB_GDB_STOPPED || state == TB_GDB_RUNNING || state == TB_GDB_READY);
}
bool tb_memory_backend_confirm(TbMemory *m, const TbMemoryBackend *reviewed)
{
    const TbMemoryBackend *current = tb_memory_backend(m);
    if (!backend_valid(reviewed) || !current || !backend_equal(reviewed, current) ||
        tb_gdb_state(m->gdb) != TB_GDB_STOPPED) { return false; }
    m->backend_confirmed = true; ++m->revision; return true;
}
const TbMemoryRange *tb_memory_range(const TbMemory *m, int id, unsigned range)
{
    return valid_view(id) && range < TB_MEMORY_RANGES ? &m->views[id].ranges[range] : NULL;
}
bool tb_memory_range_authorized(const TbMemory *m, int id, unsigned range)
{
    const TbMemoryRange *r = tb_memory_range(m, id, range);
    return r && r->used && r->authorized && tb_gdb_state(m->gdb) == TB_GDB_STOPPED &&
           same(r->generation, tb_gdb_generation(m->gdb));
}
bool tb_memory_range_set(TbMemory *m, int id, unsigned range, uint64_t begin, uint64_t end,
                         TbMemoryPolicy policy, unsigned width, const char *risks)
{
    if (!valid_view(id) || range >= TB_MEMORY_RANGES || begin >= end ||
        (policy != TB_MEMORY_ORDINARY && policy != TB_MEMORY_MMIO) ||
        (policy == TB_MEMORY_ORDINARY ? width != 0 : width != 8 && width != 16 && width != 32 && width != 64) ||
        !risks || strlen(risks) >= sizeof m->views[id].ranges[range].risks ||
        (m->address_bits && m->address_bits < 64 && end > (UINT64_C(1) << m->address_bits))) { return false; }
    TbMemoryRange *r = &m->views[id].ranges[range];
    char saved_risks[sizeof r->risks] = {0}; (void)snprintf(saved_risks, sizeof saved_risks, "%s", risks);
    *r = (TbMemoryRange){.begin = begin, .end = end, .policy = policy, .width = width, .used = true};
    memcpy(r->risks, saved_risks, sizeof r->risks);
    ++m->revision; return true;
}
bool tb_memory_range_authorize(TbMemory *m, int id, unsigned range)
{
    if (!valid_view(id) || range >= TB_MEMORY_RANGES || tb_gdb_state(m->gdb) != TB_GDB_STOPPED) { return false; }
    const TbContextSnapshot_t *s = tb_context_snapshot(m->context);
    TbMemoryRange *r = &m->views[id].ranges[range];
    if (!r->used || !s->ready || s->stale || m->unsupported ||
        (m->address_bits && m->address_bits < 64 && r->end > (UINT64_C(1) << m->address_bits))) { return false; }
    for (unsigned i = 0; i < TB_MEMORY_RANGES; ++i) {
        const TbMemoryRange *other = &m->views[id].ranges[i];
        if (i != range && tb_memory_range_authorized(m, id, i) && r->begin < other->end && other->begin < r->end &&
            (r->policy != other->policy || r->width != other->width)) { return false; }
    }
    r->generation = tb_gdb_generation(m->gdb); r->authorized = true; ++m->revision; return true;
}
bool tb_memory_range_revoke(TbMemory *m, int id, unsigned range)
{
    if (!valid_view(id) || range >= TB_MEMORY_RANGES || !m->views[id].ranges[range].used) { return false; }
    m->views[id].ranges[range].authorized = false; ++m->revision; return true;
}
static bool register_allowed(const TbMemory *m, int id, uint64_t address, unsigned width)
{
    unsigned capability = width == 8 ? TB_MEMORY_WIDTH8 : width == 16 ? TB_MEMORY_WIDTH16 : width == 32 ? TB_MEMORY_WIDTH32 : 0;
    if (!valid_view(id) || !capability || m->unsupported || !m->address_bits || !tb_memory_backend_confirmed(m) ||
        !(m->backend.widths & capability) || address % (width / 8) || address > UINT64_MAX - width / 8 ||
        (m->address_bits < 64 && address + width / 8 > (UINT64_C(1) << m->address_bits))) { return false; }
    const TbContextSnapshot_t *s = tb_context_snapshot(m->context);
    if (tb_gdb_state(m->gdb) != TB_GDB_STOPPED || !s->ready || s->stale) { return false; }
    for (unsigned i = 0; i < TB_MEMORY_RANGES; ++i) {
        const TbMemoryRange *r = &m->views[id].ranges[i];
        if (r->policy == TB_MEMORY_MMIO && r->width == width && tb_memory_range_authorized(m, id, i) &&
            r->begin <= address && r->end >= address + width / 8) { return true; }
    }
    return false;
}
bool tb_memory_register_busy(const TbMemory *m) { return m->register_queued || tb_mmio_busy(m->mmio); }
const char *tb_memory_mmio_message(const TbMemory *m) { return tb_mmio_result(m->mmio)->error; }
void tb_memory_transport(TbMemory *m) { tb_mmio_pump(m->mmio); }
bool tb_memory_read_register(TbMemory *m, int id, uint64_t address, unsigned width)
{
    if (!valid_view(id)) { return false; } View *v = &m->views[id];
    if (tb_memory_register_busy(m) || tb_mmio_result(m->mmio)->status == TB_MMIO_UNCERTAIN || !register_allowed(m, id, address, width)) {
        (void)snprintf(v->data.register_error, sizeof v->data.register_error,
            "%s", tb_mmio_result(m->mmio)->status == TB_MMIO_UNCERTAIN ? tb_mmio_result(m->mmio)->error :
            "Read requires stopped context, user-confirmed setup/width and this view's matching range consent; wait for any pending read.");
        ++m->revision; return false;
    }
    m->register_view = id; m->register_address = address; m->register_width = width;
    m->register_lifetime = v->lifetime;
    m->register_generation = tb_gdb_generation(m->gdb); m->register_epoch = m->backend_epoch; m->register_queued = true;
    v->data.register_pending = true; v->data.register_error[0] = 0; ++m->revision; return true;
}
bool tb_memory_register_sample(const TbMemory *m, int id, uint64_t address, unsigned width, TbMemoryRegister *sample)
{
    if (!valid_view(id) || !sample) { return false; } TbBrowseSample_t entry;
    TbBrowseKey_t key = {.kind = TB_BROWSE_MMIO, .view = id, .format = width, .begin = address};
    if (!tb_browse_find(m->cache, key, &entry) || entry.size != sizeof *sample) { return false; }
    memcpy(sample, entry.data, sizeof *sample); return true;
}
bool tb_memory_units(TbMemory *m, int id, uint64_t address, unsigned width)
{
    if (!valid_view(id) || (width != 8 && width != 16 && width != 32) || address % (width / 8) ||
        address > UINT64_MAX - width / 8 ||
        (m->address_bits && m->address_bits < 64 && address + width / 8 > (UINT64_C(1) << m->address_bits))) { return false; }
    View *v = &m->views[id]; v->data.mmio = true; v->data.unit_address = address; v->data.unit_width = width;
    /* Retire an earlier byte-window intent rather than dispatch it after the
     * user has explicitly switched to navigation-only MMIO presentation. */
    if (v->data.pending) { ++v->version; v->data.pending = false; }
    ++m->revision; return true;
}
void tb_memory_bytes(TbMemory *m, int id)
{
    if (!valid_view(id)) { return; } m->views[id].data.mmio = false; ++m->revision;
}
size_t tb_memory_rows(const TbMemory *m, int id)
{
    if (!valid_view(id)) { return 0; } const TbMemoryView *v = &m->views[id].data;
    if (!v->mmio) { return (v->data_size + TB_MEMORY_ROW - 1) / TB_MEMORY_ROW; }
    uint64_t end = m->address_bits && m->address_bits < 64 ? UINT64_C(1) << m->address_bits : UINT64_MAX;
    if (!v->unit_width || v->unit_address >= end) { return 0; }
    uint64_t units = (end - v->unit_address) / (v->unit_width / 8);
    return units < 32 ? (size_t)units : 32;
}
bool tb_memory_unit_address(const TbMemory *m, int id, size_t row, uint64_t *address)
{
    if (!valid_view(id) || !address || !m->views[id].data.mmio || row >= tb_memory_rows(m, id)) { return false; }
    const TbMemoryView *v = &m->views[id].data;
    *address = v->unit_address + row * (v->unit_width / 8); return true;
}
/* Extend only across a proven union of ordinary ranges. Neither an ELF section
 * nor an earlier successful read is included as permission evidence. */
static uint64_t ordinary_end(const TbMemory *m, int id, uint64_t begin, unsigned length)
{
    uint64_t limit = begin <= UINT64_MAX - length ? begin + length : UINT64_MAX, end = begin;
    for (unsigned pass = 0; pass < TB_MEMORY_REGIONS + TB_MEMORY_RANGES; ++pass) {
        uint64_t before = end;
        for (size_t i = 0; i < m->region_count; ++i) {
            if (m->regions[i].begin <= end && m->regions[i].end > end) { end = m->regions[i].end; }
        }
        for (unsigned i = 0; i < TB_MEMORY_RANGES; ++i) {
            const TbMemoryRange *r = &m->views[id].ranges[i];
            if (r->policy == TB_MEMORY_ORDINARY && tb_memory_range_authorized(m, id, i) && r->begin <= end && r->end > end) { end = r->end; }
        }
        if (end >= limit || end == before) { break; }
    }
    if (end > limit) { end = limit; }
    for (unsigned i = 0; i < TB_MEMORY_RANGES; ++i) {
        const TbMemoryRange *r = &m->views[id].ranges[i];
        if (r->used && r->policy == TB_MEMORY_MMIO && r->begin < end && r->end > begin) {
            if (r->begin <= begin) { return begin; } end = r->begin;
        }
    }
    return end;
}
bool tb_memory_stale(const TbMemory *m, int id)
{
    if (!valid_view(id)) { return true; }
    const TbContextSnapshot_t *s = tb_context_snapshot(m->context); const TbMemoryView *v = &m->views[id].data;
    return !v->ready || v->pending || tb_gdb_state(m->gdb) != TB_GDB_STOPPED || s->stale || !s->ready || !same(v->generation, s->generation);
}
bool tb_memory_refresh(TbMemory *m, int id)
{
    if (!valid_view(id)) { return false; } View *v = &m->views[id];
    if (v->data.mmio) {
        (void)snprintf(v->data.register_error, sizeof v->data.register_error,
            "MMIO has no whole-window refresh. Explicitly Read the selected width-sized unit."); ++m->revision; return false;
    }
    if (m->unsupported) { error(m, v, "Target address units/width unknown or unsupported; no memory request sent."); return false; }
    if (!v->data.configured || tb_gdb_state(m->gdb) != TB_GDB_STOPPED) {
        error(m, v, "Choose an address and stop the target before reading."); return false;
    }
    v->browse_request = false;
    ++v->version; v->origin = tb_gdb_generation(m->gdb); v->phase = MEMORY_RESOLVE; v->data.pending = true; v->data.ready = false; v->data.error[0] = 0; ++m->revision; return true;
}
bool tb_memory_address(TbMemory *m, int id, const char *expression)
{
    if (!valid_view(id)) { return false; } View *v = &m->views[id];
    if (v->data.mmio) {
        const char *p = spaces(expression ? expression : ""); uint64_t address;
        if (numeric(&p, &address) && !*spaces(p) && tb_memory_units(m, id, address, v->data.unit_width)) { return true; }
        (void)snprintf(v->data.register_error, sizeof v->data.register_error,
            "MMIO navigation needs an aligned numeric address within the target width. No read sent."); ++m->revision; return false;
    }
    if (!tb_memory_expression(expression)) {
        error(m, v, "Use an address, &buffer, pointer/field, constant index or offset; no calls/writes/casts."); return false;
    }
    if (tb_gdb_state(m->gdb) != TB_GDB_STOPPED) { error(m, v, "Stop the target before inspecting memory."); return false; }
    (void)snprintf(v->data.expression, sizeof v->data.expression, "%s", expression);
    v->data.configured = true; v->data.offset = 0; return tb_memory_refresh(m, id);
}
bool tb_memory_definition(TbMemory *m, int id, unsigned bytes, unsigned grouping)
{
    if (!valid_view(id) || !bytes || bytes > TB_MEMORY_BYTES ||
        (grouping != 1 && grouping != 2 && grouping != 4 && grouping != 8)) { return false; }
    tb_memory_clear(m, id); m->views[id].data.length = bytes; m->views[id].data.grouping = grouping;
    ++m->revision; return true;
}
bool tb_memory_length(TbMemory *m, int id, unsigned bytes)
{
    if (!valid_view(id) || !bytes || bytes > TB_MEMORY_BYTES) { return false; }
    View *v = &m->views[id]; v->data.length = bytes; ++v->version;
    v->data.pending = v->data.ready = false; v->data.error[0] = 0; ++m->revision; return true;
}
bool tb_memory_group(TbMemory *m, int id, unsigned bytes)
{
    if (!valid_view(id) || (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8)) { return false; }
    View *v = &m->views[id];
    if (bytes > 1 && m->order == TB_MEMORY_UNKNOWN) { error(m, v, "Target byte order unknown; use byte grouping."); return false; }
    v->data.grouping = bytes; ++m->revision; return true;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Existing pane ID/direction API.
bool tb_memory_page(TbMemory *m, int id, int direction)
{
    if (valid_view(id) && m->views[id].data.mmio) { return tb_memory_browse(m, id, direction); }
    if (!valid_view(id) || !m->views[id].data.configured || tb_gdb_state(m->gdb) != TB_GDB_STOPPED) { return false; }
    View *v = &m->views[id]; int64_t size = v->data.length;
    if ((direction > 0 && v->data.offset > INT64_MAX - size) || (direction < 0 && v->data.offset < INT64_MIN + size) || !direction) {
        error(m, v, "Memory page offset overflow; choose another address."); return false;
    }
    v->data.offset += direction > 0 ? size : -size; return tb_memory_refresh(m, id);
}
void tb_memory_order(TbMemory *m, TbMemoryOrder order)
{
    if (order < TB_MEMORY_UNKNOWN || order > TB_MEMORY_BIG || order == m->order) { return; }
    if (order == TB_MEMORY_UNKNOWN) {
        for (int i = 0; i < TB_MEMORY_CLIENTS; ++i) { m->views[i].data.grouping = 1; }
    }
    m->order = order; ++m->revision;
}
TbMemoryOrder tb_memory_get_order(const TbMemory *m) { return m->order; }
bool tb_memory_regions_valid(const TbMemoryRegion *regions, size_t count, unsigned address_bits)
{
    if (count > TB_MEMORY_REGIONS || (count && !regions) || address_bits > 64) { return false; }
    for (size_t i = 0; i < count; ++i) {
        if (regions[i].begin >= regions[i].end ||
            (address_bits && address_bits < 64 && regions[i].end > (UINT64_C(1) << address_bits))) { return false; }
        for (size_t j = 0; j < i; ++j) {
            if (regions[i].begin < regions[j].end && regions[j].begin < regions[i].end &&
                regions[i].writable != regions[j].writable) { return false; }
        }
    }
    return true;
}
bool tb_memory_regions(TbMemory *m, const TbMemoryRegion *regions, size_t count)
{
    bool valid = tb_memory_regions_valid(regions, count, m->address_bits);
    ++m->regions_epoch;
    if (m->edit_state == TB_MEMORY_EDIT_PENDING) {
        /* The controller drops obsolete, wholly unsent requests before writing
         * any bytes. Started mutations retain conservative stale outcomes. */
        tb_gdb_context_changed(m->gdb);
    }
    m->region_count = valid ? count : 0;
    if (valid && count) { memmove(m->regions, regions, count * sizeof *regions); }
    for (int i = 0; i < TB_MEMORY_CLIENTS; ++i) {
        ++m->views[i].version;
        for (unsigned j = 0; j < TB_MEMORY_RANGES; ++j) {
            if (m->views[i].ranges[j].policy == TB_MEMORY_ORDINARY) { m->views[i].ranges[j].authorized = false; }
        }
    }
    if (m->edit_state == TB_MEMORY_EDIT_READY) {
        m->edit_state = TB_MEMORY_EDIT_NONE;
        (void)snprintf(m->edit_error, sizeof m->edit_error, "RAM evidence changed; review a new edit.");
    }
    ++m->revision;
    return valid;
}
bool tb_memory_browse(TbMemory *m, int id, int direction)
{
    if (!valid_view(id) || !direction) { return false; }
    View *v = &m->views[id];
    if (v->data.mmio) {
        uint64_t step = UINT64_C(32) * (v->data.unit_width / 8), address = v->data.unit_address;
        if (direction < 0 ? address < step : address > UINT64_MAX - step) { return false; }
        return tb_memory_units(m, id, direction < 0 ? address - step : address + step, v->data.unit_width);
    }
    if (v->data.pending || !v->data.configured || !v->data.data_size) { return false; }
    uint64_t start = v->data.address; unsigned length = v->data.length;
    unsigned step = direction < 0 ? length : (unsigned)v->data.data_size;
    bool safe = direction < 0 ? start >= step : start <= UINT64_MAX - step;
    if (safe) { start = direction < 0 ? start - step : start + step; }
    int64_t offset = v->data.offset;
    if (!safe || (direction > 0 && offset > INT64_MAX - step) || (direction < 0 && offset < INT64_MIN + step)) { return false; }
    TbBrowseSample_t sample;
    TbBrowseKey_t key = {.kind = byte_kind(id), .view = id, .format = length, .begin = start};
    if (tb_browse_find(m->cache, key, &sample) && sample.size == sizeof(TbMemoryView)) {
        TbMemoryView saved; memcpy(&saved, sample.data, sizeof saved);
        memcpy(v->data.bytes, saved.bytes, sizeof v->data.bytes); memcpy(v->data.valid, saved.valid, sizeof v->data.valid);
        v->data.address = saved.address; v->data.data_size = saved.data_size; v->data.readable = saved.readable;
        v->data.generation = sample.key.generation; v->data.ready = true;
        v->data.offset += direction < 0 ? -(int64_t)step : (int64_t)step;
        (void)snprintf(v->data.error, sizeof v->data.error, "Cached sample; no target read.");
        ++v->version; ++m->revision; return true;
    }
    if (tb_memory_stale(m, id)) {
        (void)snprintf(v->data.error, sizeof v->data.error, "No cached sample here; stop and Read explicitly. No read queued.");
        ++m->revision; return false;
    }
    uint64_t end = ordinary_end(m, id, start, length);
    if (end <= start) {
        (void)snprintf(v->data.error, sizeof v->data.error, "Region boundary/unknown: use inline Go for an explicit range read."); ++m->revision; return false;
    }
    v->data.offset += direction < 0 ? -(int64_t)step : (int64_t)step;
    v->read_length = (unsigned)(end - start); v->browse_request = true;
    ++v->version; v->origin = tb_gdb_generation(m->gdb); v->next_address = start; v->phase = MEMORY_READ;
    v->data.pending = true; v->data.ready = false; v->data.error[0] = 0; ++m->revision; return true;
}
static bool raw(char *out, size_t capacity, const TbMiValue_t *v)
{
    if (!v || v->type != TB_MI_STRING || v->string.length >= capacity || memchr(v->string.data, 0, v->string.length)) { return false; }
    memcpy(out, v->string.data, v->string.length); out[v->string.length] = 0; return true;
}
static bool address_value(const TbMiValue_t *v, uint64_t *address)
{
    char text[256]; if (!raw(text, sizeof text, v)) { return false; }
    const char *p = text; return numeric(&p, address) && (*p == 0 || *p == ' ');
}
static bool locate(const TbMemory *m, View *v, uint64_t base)
{
    uint64_t address;
    if (v->data.offset >= 0) {
        if (base > UINT64_MAX - (uint64_t)v->data.offset) { return false; } address = base + (uint64_t)v->data.offset;
    } else {
        uint64_t distance = (uint64_t)(-(v->data.offset + 1)) + 1;
        if (base < distance) { return false; } address = base - distance;
    }
    if (address > UINT64_MAX - v->data.length ||
        (m->address_bits && m->address_bits < 64 &&
         (address >= (UINT64_C(1) << m->address_bits) || v->data.length > (UINT64_C(1) << m->address_bits) - address))) { return false; }
    v->next_address = address; v->read_length = v->data.length; v->phase = MEMORY_READ; return true;
}
bool tb_memory_event(TbMemory *m, const TbGdbEvent_t *event)
{
    if (tb_mmio_event(m->mmio, event)) {
        if (valid_view(m->register_view) && m->register_lifetime == m->views[m->register_view].lifetime) {
            View *v = &m->views[m->register_view]; const TbMmioResult_t *result = tb_mmio_result(m->mmio);
            v->data.register_pending = false;
            if (result->status == TB_MMIO_COMPLETE && m->register_epoch == m->backend_epoch) {
                TbMemoryRegister sample = {.address = result->address, .value = result->value, .width = result->width,
                    .generation = result->generation, .sampled_ms = tb_gdb_now_ms()};
                TbBrowseKey_t key = {.kind = TB_BROWSE_MMIO, .view = m->register_view, .format = result->width,
                    .begin = result->address, .end = result->address + result->width / 8,
                    .generation = result->generation, .epoch = m->backend_epoch};
                bool saved = tb_browse_store(m->cache, key, &sample, sizeof sample);
                (void)snprintf(v->data.register_error, sizeof v->data.register_error,
                    "%s", saved ? "Unit sampled by one frontend request; cached value is not proof of current peripheral state." :
                    "Unit read completed, but cache allocation failed. No retry sent.");
            } else {
                (void)snprintf(v->data.register_error, sizeof v->data.register_error, "%s",
                    m->register_epoch != m->backend_epoch ? "Setup changed; unit sample discarded. No retry sent." : result->error);
            }
            ++m->revision;
        }
        return true;
    }
    if (event->cookie == MEMORY_WRITE) {
        if (!(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE ||
              event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
        m->edit_unresolved = !event->record ||
            (strcmp(event->record->klass, "done") != 0 && strcmp(event->record->klass, "error") != 0);
        bool current = event->kind == TB_GDB_REPLY && event->record &&
            strcmp(event->record->klass, "done") == 0 &&
            same(event->generation, m->edit_generation) &&
            m->edit_lifetime == m->views[m->edit_view].lifetime && m->edit_backend_epoch == m->backend_epoch &&
            same(event->generation, tb_gdb_generation(m->gdb));
        if (current) {
            View *v = &m->views[m->edit_view];
            ++v->version; v->data.ready = false; v->data.pending = false;
            (void)snprintf(v->data.error, sizeof v->data.error,
                "Memory write acknowledged; Refresh explicitly to verify the exact bytes.");
            m->edit_state = TB_MEMORY_EDIT_ACKNOWLEDGED;
            (void)snprintf(m->edit_error, sizeof m->edit_error,
                "Backend acknowledged; Refresh explicitly to establish the current bytes.");
        } else {
            if (valid_view(m->edit_view)) {
                m->views[m->edit_view].data.ready = false;
                m->views[m->edit_view].data.pending = false;
            }
            m->edit_state = TB_MEMORY_EDIT_UNCERTAIN;
            (void)snprintf(m->edit_error, sizeof m->edit_error,
                m->edit_unresolved ? "UNCERTAIN: backend completion is unresolved; reconnect before editing again." :
                "UNCERTAIN: memory write failed; Refresh explicitly and reconcile before retrying.");
        }
        m->edit_complete = true;
        ++m->revision; return true;
    }
    if ((event->cookie != MEMORY_RESOLVE && event->cookie != MEMORY_READ) ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    m->waiting = false; View *v = &m->views[m->active];
    if (v->version != m->request_version) { return true; }
    const TbContextSnapshot_t *s = tb_context_snapshot(m->context);
    if (event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED || s->stale || !same(s->generation, m->request_generation)) {
        error(m, v, "Context changed; Refresh explicitly to read this range."); return true;
    }
    const TbMiRecord_t *record = event->record;
    if (event->kind != TB_GDB_REPLY || !record || strcmp(record->klass, "done") != 0) {
        const TbMiValue_t *msg = record ? tb_mi_find(record->results, "msg") : NULL;
        char safe[192] = "Memory request failed/timed out; Refresh retries.";
        if (msg && msg->type == TB_MI_STRING) { tb_code_safe(safe, sizeof safe, msg->string.data, msg->string.length); }
        error(m, v, safe); return true;
    }
    if (event->cookie == MEMORY_RESOLVE) {
        uint64_t base;
        if (!address_value(tb_mi_find(record->results, "value"), &base) || !locate(m, v, base)) {
            error(m, v, "Address unavailable/overflow; use &buffer for arrays and objects.");
        }
        return true;
    }
    const TbMiValue_t *list = tb_mi_find(record->results, "memory");
    unsigned char bytes[TB_MEMORY_BYTES] = {0}; bool valid[TB_MEMORY_BYTES] = {0}; size_t readable = 0;
    if (!list || list->type != TB_MI_LIST) { error(m, v, "Malformed memory reply; retained data is stale."); return true; }
    for (const TbMiValue_t *block = list->children; block; block = block->next) {
        uint64_t begin, end; const TbMiValue_t *contents = tb_mi_find(block->children, "contents");
        if (block->type != TB_MI_TUPLE || !address_value(tb_mi_find(block->children, "begin"), &begin) ||
            !address_value(tb_mi_find(block->children, "end"), &end) || end < begin || begin < v->next_address ||
            end - v->next_address > v->read_length || !contents || contents->type != TB_MI_STRING ||
            contents->string.length != 2 * (end - begin)) {
            error(m, v, "Malformed memory range; retained data is stale."); return true;
        }
        for (uint64_t i = 0; i < end - begin; ++i) {
            size_t at = (size_t)(begin - v->next_address + i); int high = hex((unsigned char)contents->string.data[2 * i]);
            int low = hex((unsigned char)contents->string.data[2 * i + 1]);
            if (valid[at] || high < 0 || low < 0) { error(m, v, "Malformed/overlapping memory bytes; retained data is stale."); return true; }
            bytes[at] = (unsigned char)(high * 16 + low); valid[at] = true; ++readable;
        }
    }
    memcpy(v->data.bytes, bytes, sizeof bytes); memcpy(v->data.valid, valid, sizeof valid);
    v->data.address = v->next_address; v->data.data_size = v->read_length; v->data.readable = readable;
    v->data.generation = m->request_generation; v->data.pending = false; v->data.ready = true;
    v->data.error[0] = 0;
    if (readable < v->read_length) { (void)snprintf(v->data.error, sizeof v->data.error, "Partial read: %zu/%u bytes; ?? marks unavailable bytes.", readable, v->read_length); }
    else if (v->read_length < v->data.length) { (void)snprintf(v->data.error, sizeof v->data.error, "Read clipped to authorized boundary: %u/%u bytes.", v->read_length, v->data.length); }
    TbBrowseKey_t key = {.kind = byte_kind(m->active), .view = m->active, .format = v->data.length,
        .begin = v->data.address, .end = v->data.address + v->data.data_size, .generation = v->data.generation};
    (void)tb_browse_store(m->cache, key, &v->data, sizeof v->data);
    ++m->revision; return true;
}
void tb_memory_pump(TbMemory *m)
{
    if (m->edit_complete) {
        m->edit_complete = false;
        tb_context_write_finish(m->context, MEMORY_WRITE, m->edit_state != TB_MEMORY_EDIT_UNCERTAIN ? TB_WRITE_ACKNOWLEDGED :
            m->edit_unresolved ? TB_WRITE_UNRESOLVED : TB_WRITE_UNCERTAIN);
    }
    tb_mmio_pump(m->mmio);
    TbGdbGeneration_t current = tb_gdb_generation(m->gdb);
    if (m->edit_state == TB_MEMORY_EDIT_PENDING &&
        (tb_gdb_state(m->gdb) != TB_GDB_STOPPED || !same(m->edit_generation, current))) {
        m->edit_state = TB_MEMORY_EDIT_UNCERTAIN;
        (void)snprintf(m->edit_error, sizeof m->edit_error,
            "UNCERTAIN: target context changed while the memory write was pending; Refresh before retrying.");
        ++m->revision;
    }
    if (m->backend_present && (m->backend_session != current.session ||
        tb_gdb_state(m->gdb) == TB_GDB_DISCONNECTED || tb_gdb_state(m->gdb) == TB_GDB_CLOSING ||
        tb_gdb_state(m->gdb) == TB_GDB_EXITED)) {
        (void)tb_memory_backend_set(m, NULL);
    }
    for (int i = 0; i < TB_MEMORY_CLIENTS; ++i) {
        View *v = &m->views[i];
        for (unsigned j = 0; j < TB_MEMORY_RANGES; ++j) {
            if (v->ranges[j].authorized && !tb_memory_range_authorized(m, i, j)) {
                v->ranges[j].authorized = false; ++m->revision;
            }
        }
        if (v->data.pending && !same(v->origin, current)) { error(m, v, "Context changed; Refresh explicitly to read this range."); }
    }
    if (tb_mmio_busy(m->mmio)) { return; }
    if (m->register_queued) {
        View *v = &m->views[m->register_view];
        if (!same(m->register_generation, current) || m->register_epoch != m->backend_epoch ||
            !register_allowed(m, m->register_view, m->register_address, m->register_width)) {
            m->register_queued = false; v->data.register_pending = false;
            (void)snprintf(v->data.register_error, sizeof v->data.register_error, "Register permission/context changed before dispatch; no read sent.");
            ++m->revision;
        } else if (!m->waiting && !tb_gdb_pending(m->gdb)) {
            m->register_queued = false;
            if (!tb_mmio_read(m->mmio, m->backend.target, m->register_address, m->register_width)) {
                v->data.register_pending = false;
                (void)snprintf(v->data.register_error, sizeof v->data.register_error, "%s",
                    tb_mmio_result(m->mmio)->status == TB_MMIO_UNCERTAIN ? tb_mmio_result(m->mmio)->error :
                    "Register transport unavailable; no read sent. Check target identity and pending operations.");
            }
            ++m->revision; return;
        } else { return; }
    }
    if (m->waiting || tb_gdb_state(m->gdb) != TB_GDB_STOPPED) { return; }
    const TbContextSnapshot_t *s = tb_context_snapshot(m->context); if (!s->ready || s->stale) { return; }
    for (int i = 0; i < TB_MEMORY_CLIENTS; ++i) {
        int id = (m->cursor + i) % TB_MEMORY_CLIENTS; View *v = &m->views[id];
        if (!v->visible || !v->data.pending) { continue; }
        if (v->phase == MEMORY_RESOLVE) {
            const char *p = spaces(v->data.expression); uint64_t base;
            if (numeric(&p, &base) && *spaces(p) == 0) {
                if (!locate(m, v, base)) { error(m, v, "Memory range or page offset overflow."); continue; }
            }
        }
        if (v->phase == MEMORY_READ && v->browse_request &&
            ordinary_end(m, id, v->next_address, v->read_length) != v->next_address + v->read_length) {
            error(m, v, "Ordinary range permission changed before dispatch; no read sent."); continue;
        }
        bool mmio = false;
        for (unsigned j = 0; v->phase == MEMORY_READ && j < TB_MEMORY_RANGES; ++j) {
            const TbMemoryRange *r = &v->ranges[j];
            mmio = mmio || (r->used && r->policy == TB_MEMORY_MMIO && r->begin < v->next_address + v->read_length && r->end > v->next_address);
        }
        if (mmio) {
            error(m, v, "Declared MMIO requires a verified explicit-width backend; generic byte reads are blocked."); continue;
        }
        char address[48], length[24], scalar[320]; const char *args[7]; size_t count;
        if (v->phase == MEMORY_RESOLVE) {
            /* Validated user grammar contains no calls/writes/casts. This owned
             * scalar conversion also prevents GDB printing char-pointer strings
             * by reading beyond the explicitly requested byte range. */
            (void)snprintf(scalar, sizeof scalar, "(unsigned long long)(%s)", v->data.expression);
            args[0] = "--language"; args[1] = "c"; args[2] = scalar; count = 3;
        } else {
            (void)snprintf(address, sizeof address, "0x%" PRIx64, v->next_address);
            (void)snprintf(length, sizeof length, "%u", v->read_length);
            count = 0;
            if (*v->thread) { args[count++] = "--thread"; args[count++] = v->thread; args[count++] = "--frame"; args[count++] = "0"; }
            args[count++] = address; args[count++] = length;
        }
        m->active = id; m->request_version = v->version; m->request_generation = s->generation;
        m->waiting = tb_gdb_request(m->gdb, v->phase == MEMORY_RESOLVE ? "-data-evaluate-expression" : "-data-read-memory-bytes",
                                   args, count, TB_GDB_CONTEXT, v->phase, 3000, NULL);
        m->cursor = (id + 1) % TB_MEMORY_CLIENTS; return;
    }
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Existing pane ID/row API.
void tb_memory_line(const TbMemory *m, int id, size_t row, char *out, size_t capacity)
{
    if (!capacity) { return; } out[0] = 0;
    if (!valid_view(id)) { return; } const TbMemoryView *v = &m->views[id].data;
    if (v->mmio) {
        uint64_t address; if (!tb_memory_unit_address(m, id, row, &address)) { return; }
        TbMemoryRegister sample;
        if (tb_memory_register_sample(m, id, address, v->unit_width, &sample)) {
            (void)snprintf(out, capacity, "0x%016" PRIx64 " %2ub 0x%0*" PRIx64 " [sample stop %" PRIu64 "]",
                address, v->unit_width, (int)(v->unit_width / 4), sample.value, sample.generation.stop);
        } else { (void)snprintf(out, capacity, "0x%016" PRIx64 " %2ub -- [not sampled]", address, v->unit_width); }
        return;
    }
    if (row >= (v->data_size + TB_MEMORY_ROW - 1) / TB_MEMORY_ROW) { return; }
    char digits[49], ascii[17]; memset(digits, ' ', 48); digits[48] = 0;
    static const char hexes[] = "0123456789abcdef"; size_t first = row * 16, col = 0;
    for (unsigned group = 0; group < 16; group += v->grouping) {
        for (unsigned j = 0; j < v->grouping; ++j) {
            unsigned offset = m->order == TB_MEMORY_LITTLE ? v->grouping - j - 1 : j;
            size_t at = first + group + offset;
            if (at >= v->data_size) { digits[col++] = ' '; digits[col++] = ' '; }
            else if (!v->valid[at]) { digits[col++] = '?'; digits[col++] = '?'; }
            else { unsigned byte = v->bytes[at]; digits[col++] = hexes[byte >> 4U]; digits[col++] = hexes[byte & 15U]; }
        }
        ++col;
    }
    for (unsigned i = 0; i < 16; ++i) {
        size_t at = first + i; unsigned char c = at < v->data_size ? v->bytes[at] : 0;
        if (at >= v->data_size) { ascii[i] = ' '; }
        else if (!v->valid[at]) { ascii[i] = '?'; }
        else if (c >= 32 && c < 127) { ascii[i] = (char)c; }
        else { ascii[i] = '.'; }
    }
    ascii[16] = 0;
    (void)snprintf(out, capacity, "0x%016" PRIx64 "  %s |%s|", v->address + first, digits, ascii);
}

void tb_memory_target(TbMemory *m, bool octets, unsigned address_bits)
{
    bool unsupported = !octets || !address_bits || address_bits > 64;
    if (m->unsupported == unsupported && m->address_bits == address_bits) { return; }
    (void)tb_memory_backend_set(m, NULL);
    m->unsupported = unsupported; m->address_bits = address_bits;
    for (int i = 0; i < TB_MEMORY_CLIENTS; ++i) {
        View *v = &m->views[i]; ++v->version; v->phase = 0;
        v->data.pending = v->data.ready = false;
    }
    ++m->revision;
}

int tb_memory_stack_client(int pane) { return pane >= 0 && pane < TB_MEMORY_VIEWS ? pane + TB_MEMORY_VIEWS : -1; }
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): client ID and target address are distinct validated domains.
bool tb_memory_client_read(TbMemory *m, int id, uint64_t address, unsigned bytes, const char *thread)
{
    if (id < TB_MEMORY_VIEWS || !valid_view(id) || !thread || !*thread || strlen(thread) >= 32 ||
        !bytes || bytes > TB_MEMORY_BYTES || m->unsupported || !m->address_bits || tb_memory_client_busy(m, id)) { return false; }
    const TbContextSnapshot_t *s = tb_context_snapshot(m->context);
    if (tb_gdb_state(m->gdb) != TB_GDB_STOPPED || !s->ready || s->stale || strcmp(s->selected_thread, thread) != 0) { return false; }
    View *v = &m->views[id];
    v->data.length = bytes; v->data.offset = 0;
    if (!locate(m, v, address)) { return false; }
    (void)snprintf(v->data.expression, sizeof v->data.expression, "0x%" PRIx64, address);
    (void)snprintf(v->thread, sizeof v->thread, "%s", thread);
    v->data.configured = true; v->data.pending = true; v->data.ready = false; v->data.error[0] = 0;
    v->browse_request = false; v->visible = true; v->origin = s->generation; ++v->version; ++m->revision; return true;
}
void tb_memory_client_cancel(TbMemory *m, int id)
{
    if (id < TB_MEMORY_VIEWS || !valid_view(id)) { return; }
    View *v = &m->views[id]; ++v->version; v->data.pending = false; ++m->revision;
}
bool tb_memory_client_busy(const TbMemory *m, int id)
{
    return valid_view(id) && (m->views[id].data.pending || (m->waiting && m->active == id));
}

bool tb_memory_edit_begin(TbMemory *m, int id, uint64_t address, unsigned bytes)
{
    if (!valid_view(id) || tb_context_write_blocked(m->context) || m->edit_state == TB_MEMORY_EDIT_PENDING ||
        m->edit_state == TB_MEMORY_EDIT_UNCERTAIN || !bytes || bytes > TB_MEMORY_ROW ||
        tb_memory_stale(m, id) || !writable_destination(m, id, address, bytes)) {
        (void)snprintf(m->edit_error, sizeof m->edit_error,
            m->edit_state == TB_MEMORY_EDIT_UNCERTAIN ?
                "Previous write is uncertain; Refresh and reconcile before retrying." :
                "Edit requires a fresh stopped sample, writable ordinary RAM and this view's authorized range.");
        return false;
    }
    const TbMemoryView *v = &m->views[id].data;
    if (address < v->address || address > UINT64_MAX - bytes ||
        address - v->address > v->data_size || bytes > v->data_size - (address - v->address)) {
        (void)snprintf(m->edit_error, sizeof m->edit_error,
            "Selected bytes are outside the current Memory sample.");
        return false;
    }
    size_t offset = (size_t)(address - v->address);
    for (unsigned i = 0; i < bytes; ++i) {
        if (!v->valid[offset + i]) {
            (void)snprintf(m->edit_error, sizeof m->edit_error,
                "Selected bytes include unavailable data; Refresh a complete sample first.");
            return false;
        }
        m->edit_old[i] = v->bytes[offset + i];
    }
    m->edit_view = id; m->edit_address = address; m->edit_bytes = bytes;
    m->edit_regions_epoch = m->regions_epoch;
    m->edit_generation = v->generation; m->edit_state = TB_MEMORY_EDIT_READY;
    m->edit_version = m->views[id].version; m->edit_lifetime = m->views[id].lifetime; m->edit_backend_epoch = m->backend_epoch;
    for (unsigned i = 0; i < bytes; ++i) {
        (void)snprintf(m->edit_old_text + (size_t)i * 2, 3, "%02x", m->edit_old[i]);
    }
    m->edit_old_text[(size_t)bytes * 2] = 0; m->edit_error[0] = 0; ++m->revision;
    return true;
}

bool tb_memory_edit_apply(TbMemory *m, const char *encoded)
{
    if (m->edit_state != TB_MEMORY_EDIT_READY || !encoded ||
        m->edit_regions_epoch != m->regions_epoch ||
        !same(m->edit_generation, tb_gdb_generation(m->gdb)) ||
        tb_gdb_state(m->gdb) != TB_GDB_STOPPED ||
        m->edit_lifetime != m->views[m->edit_view].lifetime || m->edit_backend_epoch != m->backend_epoch ||
        m->edit_version != m->views[m->edit_view].version || tb_memory_stale(m, m->edit_view) ||
        !writable_destination(m, m->edit_view, m->edit_address, m->edit_bytes) ||
        strlen(encoded) != (size_t)m->edit_bytes * 2) {
        (void)snprintf(m->edit_error, sizeof m->edit_error,
            "Memory edit proof expired or the exact writable extent is no longer authorized.");
        return false;
    }
    for (unsigned i = 0; i < m->edit_bytes; ++i) {
        int high = hex((unsigned char)encoded[(size_t)i * 2]);
        int low = hex((unsigned char)encoded[(size_t)i * 2 + 1]);
        if (high < 0 || low < 0) {
            (void)snprintf(m->edit_error, sizeof m->edit_error,
                "Enter exactly %u hexadecimal bytes with no separators.", m->edit_bytes);
            return false;
        }
        m->edit_new[i] = (unsigned char)(high * 16 + low);
    }
    char address[32], contents[TB_MEMORY_ROW * 2 + 1];
    (void)snprintf(address, sizeof address, "0x%" PRIx64, m->edit_address);
    memcpy(contents, encoded, (size_t)m->edit_bytes * 2 + 1);
    const char *args[] = {address, contents};
    if (m->waiting || tb_mmio_busy(m->mmio) || m->register_queued ||
        !tb_context_write_begin(m->context, MEMORY_WRITE)) {
        (void)snprintf(m->edit_error, sizeof m->edit_error, "Wait for pending operations or reconcile an uncertain write, then review again.");
        return false;
    }
    m->edit_generation = tb_gdb_generation(m->gdb);
    if (!tb_gdb_request(m->gdb, "-data-write-memory-bytes", args, 2, TB_GDB_CONTEXT,
                        MEMORY_WRITE, 3000, NULL)) {
        tb_context_write_finish(m->context, MEMORY_WRITE, TB_WRITE_ACKNOWLEDGED);
        (void)snprintf(m->edit_error, sizeof m->edit_error,
            "Memory write could not be queued; no retry was sent.");
        return false;
    }
    m->edit_state = TB_MEMORY_EDIT_PENDING; ++m->revision; return true;
}

void tb_memory_edit_cancel(TbMemory *m)
{
    if (m->edit_state == TB_MEMORY_EDIT_PENDING || m->edit_state == TB_MEMORY_EDIT_UNCERTAIN) { return; }
    m->edit_state = TB_MEMORY_EDIT_NONE; m->edit_error[0] = 0; ++m->revision;
}

bool tb_memory_edit_reconcile(TbMemory *m)
{
    if (m->edit_state != TB_MEMORY_EDIT_UNCERTAIN || !valid_view(m->edit_view) ||
        tb_memory_stale(m, m->edit_view) || tb_gdb_pending(m->gdb)) { return false; }
    const TbMemoryView *sample = &m->views[m->edit_view].data;
    if (sample->address > m->edit_address || sample->data_size < m->edit_bytes ||
        m->edit_address - sample->address > sample->data_size - m->edit_bytes) { return false; }
    for (unsigned i = 0; i < m->edit_bytes; ++i) {
        if (!sample->valid[m->edit_address - sample->address + i]) { return false; }
    }
    if (!tb_context_write_reconcile(m->context, MEMORY_WRITE)) { return false; }
    m->edit_state = TB_MEMORY_EDIT_NONE;
    (void)snprintf(m->edit_error, sizeof m->edit_error,
        "Reconciled after explicit Refresh; review a new Memory edit.");
    ++m->revision; return true;
}

TbMemoryEditState tb_memory_edit_state(const TbMemory *m) { return m->edit_state; }
const char *tb_memory_edit_error(const TbMemory *m) { return m->edit_error; }
const char *tb_memory_edit_old(const TbMemory *m) { return m->edit_old_text; }

bool tb_memory_edit_destination(const TbMemory *m, uint64_t *address, unsigned *bytes)
{
    if (m->edit_state == TB_MEMORY_EDIT_NONE || !address || !bytes) { return false; }
    *address = m->edit_address; *bytes = m->edit_bytes; return true;
}
