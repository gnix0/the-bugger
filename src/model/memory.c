#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- POSIX interfaces. */
#include "memory.h"
#include "code.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MEMORY_RESOLVE = 9401, MEMORY_READ };
typedef struct {
    TbMemoryView data;
    bool visible;
    unsigned phase;
    uint64_t version, next_address;
    TbGdbGeneration origin;
} View;
struct TbMemory {
    TbGdb *gdb; TbContext *context;
    View views[TB_MEMORY_VIEWS];
    bool waiting; int active, cursor;
    uint64_t revision, request_version;
    TbGdbGeneration request_generation;
    TbMemoryOrder order;
    TbMemoryRegion regions[TB_MEMORY_REGIONS];
    size_t region_count;
};
static bool valid_view(int id) { return id >= 0 && id < TB_MEMORY_VIEWS; }
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
TbMemory *tb_memory_create(TbGdb *gdb, TbContext *context)
{
    TbMemory *m = calloc(1, sizeof *m); if (m) { m->gdb = gdb; m->context = context; tb_memory_reset(m); } return m;
}
void tb_memory_destroy(TbMemory *m) { free(m); }
void tb_memory_clear(TbMemory *m, int id)
{
    if (!valid_view(id)) { return; }
    View *v = &m->views[id]; uint64_t version = v->version + 1;
    memset(v, 0, sizeof *v); v->version = version; v->data.length = 256; v->data.grouping = 1; ++m->revision;
}
void tb_memory_reset(TbMemory *m)
{
    TbGdb *gdb = m->gdb; TbContext *context = m->context; uint64_t revision = m->revision;
    memset(m, 0, sizeof *m); m->gdb = gdb; m->context = context; m->revision = revision + 1;
    for (int i = 0; i < TB_MEMORY_VIEWS; ++i) { tb_memory_clear(m, i); }
}
void tb_memory_visible(TbMemory *m, int id, bool visible)
{
    if (!valid_view(id)) { return; } m->views[id].visible = visible;
    bool required = false; for (int i = 0; i < TB_MEMORY_VIEWS; ++i) { required = required || m->views[i].visible; }
    tb_context_memory(m->context, required);
}
const TbMemoryView *tb_memory_view(const TbMemory *m, int id) { return valid_view(id) ? &m->views[id].data : NULL; }
uint64_t tb_memory_revision(const TbMemory *m) { return m->revision; }
static bool same(TbGdbGeneration a, TbGdbGeneration b)
{
    return a.session == b.session && a.stop == b.stop && a.context == b.context;
}
bool tb_memory_stale(const TbMemory *m, int id)
{
    if (!valid_view(id)) { return true; }
    const TbContextSnapshot *s = tb_context_snapshot(m->context); const TbMemoryView *v = &m->views[id].data;
    return !v->ready || v->pending || tb_gdb_state(m->gdb) != TB_GDB_STOPPED || s->stale || !s->ready || !same(v->generation, s->generation);
}
bool tb_memory_refresh(TbMemory *m, int id)
{
    if (!valid_view(id)) { return false; } View *v = &m->views[id];
    if (!v->data.configured || tb_gdb_state(m->gdb) != TB_GDB_STOPPED) {
        error(m, v, "Choose an address and stop the target before reading."); return false;
    }
    ++v->version; v->origin = tb_gdb_generation(m->gdb); v->phase = MEMORY_RESOLVE; v->data.pending = true; v->data.ready = false; v->data.error[0] = 0; ++m->revision; return true;
}
bool tb_memory_address(TbMemory *m, int id, const char *expression)
{
    if (!valid_view(id)) { return false; } View *v = &m->views[id];
    if (!tb_memory_expression(expression)) {
        error(m, v, "Use an address, &buffer, pointer/field, constant index or offset; no calls/writes/casts."); return false;
    }
    if (tb_gdb_state(m->gdb) != TB_GDB_STOPPED) { error(m, v, "Stop the target before inspecting memory."); return false; }
    (void)snprintf(v->data.expression, sizeof v->data.expression, "%s", expression);
    v->data.configured = true; v->data.offset = 0; return tb_memory_refresh(m, id);
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
        for (int i = 0; i < TB_MEMORY_VIEWS; ++i) { m->views[i].data.grouping = 1; }
    }
    m->order = order; ++m->revision;
}
TbMemoryOrder tb_memory_get_order(const TbMemory *m) { return m->order; }
void tb_memory_regions(TbMemory *m, const TbMemoryRegion *regions, size_t count)
{
    m->region_count = 0;
    for (size_t i = 0; regions && i < count && m->region_count < TB_MEMORY_REGIONS; ++i) {
        if (regions[i].end > regions[i].begin) { m->regions[m->region_count++] = regions[i]; }
    }
}
bool tb_memory_browse(TbMemory *m, int id, int direction)
{
    if (!valid_view(id) || !direction) { return false; }
    View *v = &m->views[id];
    if (v->data.pending || tb_memory_stale(m, id)) { return false; }
    uint64_t start = v->data.address; unsigned length = v->data.length;
    bool safe = direction < 0 ? start >= length : start <= UINT64_MAX - length;
    if (safe) { start = direction < 0 ? start - length : start + length; }
    safe = safe && start <= UINT64_MAX - length && v->data.readable == v->data.data_size;
    bool inside = false;
    for (size_t i = 0; safe && i < m->region_count; ++i) {
        inside = inside || (start >= m->regions[i].begin && start + length <= m->regions[i].end);
    }
    int64_t offset = v->data.offset;
    if (!inside || (direction > 0 && offset > INT64_MAX - length) || (direction < 0 && offset < INT64_MIN + length)) {
        (void)snprintf(v->data.error, sizeof v->data.error, "Region boundary/unknown: use inline Go for an explicit range read."); ++m->revision; return false;
    }
    v->data.offset += direction < 0 ? -(int64_t)length : (int64_t)length;
    ++v->version; v->origin = tb_gdb_generation(m->gdb); v->next_address = start; v->phase = MEMORY_READ;
    v->data.pending = true; v->data.ready = false; v->data.error[0] = 0; ++m->revision; return true;
}
static bool raw(char *out, size_t capacity, const TbMiValue *v)
{
    if (!v || v->type != TB_MI_STRING || v->string.length >= capacity || memchr(v->string.data, 0, v->string.length)) { return false; }
    memcpy(out, v->string.data, v->string.length); out[v->string.length] = 0; return true;
}
static bool address_value(const TbMiValue *v, uint64_t *address)
{
    char text[256]; if (!raw(text, sizeof text, v)) { return false; }
    const char *p = text; return numeric(&p, address) && (*p == 0 || *p == ' ');
}
static bool locate(View *v, uint64_t base)
{
    uint64_t address;
    if (v->data.offset >= 0) {
        if (base > UINT64_MAX - (uint64_t)v->data.offset) { return false; } address = base + (uint64_t)v->data.offset;
    } else {
        uint64_t distance = (uint64_t)(-(v->data.offset + 1)) + 1;
        if (base < distance) { return false; } address = base - distance;
    }
    if (address > UINT64_MAX - v->data.length) { return false; }
    v->next_address = address; v->phase = MEMORY_READ; return true;
}
bool tb_memory_event(TbMemory *m, const TbGdbEvent *event)
{
    if ((event->cookie != MEMORY_RESOLVE && event->cookie != MEMORY_READ) ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    m->waiting = false; View *v = &m->views[m->active];
    if (v->version != m->request_version) { return true; }
    const TbContextSnapshot *s = tb_context_snapshot(m->context);
    if (event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED || s->stale || !same(s->generation, m->request_generation)) {
        error(m, v, "Context changed; Refresh explicitly to read this range."); return true;
    }
    const TbMiRecord *record = event->record;
    if (event->kind != TB_GDB_REPLY || !record || strcmp(record->klass, "done") != 0) {
        const TbMiValue *msg = record ? tb_mi_find(record->results, "msg") : NULL;
        char safe[192] = "Memory request failed/timed out; Refresh retries.";
        if (msg && msg->type == TB_MI_STRING) { tb_code_safe(safe, sizeof safe, msg->string.data, msg->string.length); }
        error(m, v, safe); return true;
    }
    if (event->cookie == MEMORY_RESOLVE) {
        uint64_t base;
        if (!address_value(tb_mi_find(record->results, "value"), &base) || !locate(v, base)) {
            error(m, v, "Address unavailable/overflow; use &buffer for arrays and objects.");
        }
        return true;
    }
    const TbMiValue *list = tb_mi_find(record->results, "memory");
    unsigned char bytes[TB_MEMORY_BYTES] = {0}; bool valid[TB_MEMORY_BYTES] = {0}; size_t readable = 0;
    if (!list || list->type != TB_MI_LIST) { error(m, v, "Malformed memory reply; retained data is stale."); return true; }
    for (const TbMiValue *block = list->children; block; block = block->next) {
        uint64_t begin, end; const TbMiValue *contents = tb_mi_find(block->children, "contents");
        if (block->type != TB_MI_TUPLE || !address_value(tb_mi_find(block->children, "begin"), &begin) ||
            !address_value(tb_mi_find(block->children, "end"), &end) || end < begin || begin < v->next_address ||
            end - v->next_address > v->data.length || !contents || contents->type != TB_MI_STRING ||
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
    v->data.address = v->next_address; v->data.data_size = v->data.length; v->data.readable = readable;
    v->data.generation = m->request_generation; v->data.pending = false; v->data.ready = true;
    v->data.error[0] = 0;
    if (readable < v->data.length) { (void)snprintf(v->data.error, sizeof v->data.error, "Partial read: %zu/%u bytes; ?? marks unavailable bytes.", readable, v->data.length); }
    ++m->revision; return true;
}
void tb_memory_pump(TbMemory *m)
{
    TbGdbGeneration current = tb_gdb_generation(m->gdb);
    for (int i = 0; i < TB_MEMORY_VIEWS; ++i) {
        View *v = &m->views[i];
        if (v->data.pending && !same(v->origin, current)) { error(m, v, "Context changed; Refresh explicitly to read this range."); }
    }
    if (m->waiting || tb_gdb_state(m->gdb) != TB_GDB_STOPPED) { return; }
    const TbContextSnapshot *s = tb_context_snapshot(m->context); if (!s->ready || s->stale) { return; }
    for (int i = 0; i < TB_MEMORY_VIEWS; ++i) {
        int id = (m->cursor + i) % TB_MEMORY_VIEWS; View *v = &m->views[id];
        if (!v->visible || !v->data.pending) { continue; }
        if (v->phase == MEMORY_RESOLVE) {
            const char *p = spaces(v->data.expression); uint64_t base;
            if (numeric(&p, &base) && *spaces(p) == 0) {
                if (!locate(v, base)) { error(m, v, "Memory range or page offset overflow."); continue; }
            }
        }
        char address[48], length[24], scalar[320]; const char *args[3]; size_t count;
        if (v->phase == MEMORY_RESOLVE) {
            /* Validated user grammar contains no calls/writes/casts. This owned
             * scalar conversion also prevents GDB printing char-pointer strings
             * by reading beyond the explicitly requested byte range. */
            (void)snprintf(scalar, sizeof scalar, "(unsigned long long)(%s)", v->data.expression);
            args[0] = "--language"; args[1] = "c"; args[2] = scalar; count = 3;
        } else {
            (void)snprintf(address, sizeof address, "0x%" PRIx64, v->next_address);
            (void)snprintf(length, sizeof length, "%u", v->data.length);
            args[0] = address; args[1] = length; count = 2;
        }
        m->active = id; m->request_version = v->version; m->request_generation = s->generation;
        m->waiting = tb_gdb_request(m->gdb, v->phase == MEMORY_RESOLVE ? "-data-evaluate-expression" : "-data-read-memory-bytes",
                                   args, count, TB_GDB_CONTEXT, v->phase, 3000, NULL);
        m->cursor = (id + 1) % TB_MEMORY_VIEWS; return;
    }
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Existing pane ID/row API.
void tb_memory_line(const TbMemory *m, int id, size_t row, char *out, size_t capacity)
{
    if (!capacity) { return; } out[0] = 0;
    if (!valid_view(id)) { return; } const TbMemoryView *v = &m->views[id].data;
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
