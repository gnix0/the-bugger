#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- POSIX interfaces. */
#include "mmio.h"
#include "monitor.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

enum { MMIO_COOKIE = 9403 };
struct TbMmio {
    TbGdb *gdb;
    TbMonitorFrame frame;
    TbMmioResult result;
    uint64_t session, sequence, token, late_token;
    char nonce[33];
    bool active, release;
};
TbMmio *tb_mmio_create(TbGdb *gdb)
{
    if (!gdb) { return NULL; }
    TbMmio *m = calloc(1, sizeof *m); if (m) { m->gdb = gdb; } return m;
}
bool tb_mmio_destroy(TbMmio *m)
{
    if (!m) { return true; }
    if ((m->active || m->late_token) && tb_gdb_state(m->gdb) != TB_GDB_DISCONNECTED) { return false; }
    tb_gdb_release(m->gdb, MMIO_COOKIE); free(m); return true;
}
const TbMmioResult *tb_mmio_result(const TbMmio *m) { return &m->result; }
bool tb_mmio_busy(const TbMmio *m) { return m->active; }
static void outcome(TbMmio *m, TbMmioStatus status, const char *error)
{
    m->result.status = status;
    (void)snprintf(m->result.error, sizeof m->result.error, "%s", error);
    m->active = false; m->release = true;
}
void tb_mmio_pump(TbMmio *m)
{
    if (m->release) { tb_gdb_release(m->gdb, MMIO_COOKIE); m->release = false; }
    uint64_t session = tb_gdb_generation(m->gdb).session;
    if (m->session != session || tb_gdb_state(m->gdb) == TB_GDB_DISCONNECTED) {
        tb_gdb_release(m->gdb, MMIO_COOKIE);
        m->session = session; m->active = m->release = false; m->late_token = m->token = m->sequence = 0;
        memset(m->nonce, 0, sizeof m->nonce); memset(&m->result, 0, sizeof m->result);
    }
}
static bool nonce(TbMmio *m)
{
    if (*m->nonce) { return true; }
    unsigned char bytes[16]; size_t used = 0;
    for (unsigned attempt = 0; used < sizeof bytes && attempt < 32; ++attempt) {
        ssize_t n = getrandom(bytes + used, sizeof bytes - used, GRND_NONBLOCK);
        if (n < 0 && errno == EINTR) { continue; }
        if (n <= 0) { return false; } used += (size_t)n;
    }
    if (used != sizeof bytes) { return false; }
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof bytes; ++i) { m->nonce[2 * i] = hex[bytes[i] / 16]; m->nonce[2 * i + 1] = hex[bytes[i] % 16]; }
    m->nonce[32] = 0; return true;
}
bool tb_mmio_read(TbMmio *m, const char *target, uint64_t address, unsigned width)
{
    tb_mmio_pump(m);
    if (m->active || m->result.status == TB_MMIO_UNCERTAIN || m->late_token ||
        tb_gdb_state(m->gdb) != TB_GDB_STOPPED || m->sequence == UINT64_MAX ||
        (width != 8 && width != 16 && width != 32) || !nonce(m)) { return false; }
    char id[64]; (void)snprintf(id, sizeof id, "%s-%" PRIu64, m->nonce, ++m->sequence);
    if (!tb_monitor_start(&m->frame, id, target, address, width) || !tb_gdb_reserve(m->gdb, MMIO_COOKIE)) { return false; }
    char command[1024];
    /* Validated identifiers and integer literals only. Target instance binding
     * is checked both before and after the single explicit-width operation.
     * No persistent Tcl helper, human memory table or byte-read fallback. */
    int n = snprintf(command, sizeof command,
        "monitor apply {{id t a w} {if {[target current] ne $t} {error TB_TARGET_CHANGED}; "
        "set v [$t read_memory $a $w 1]; if {[llength $v] != 1} {error TB_BAD_COUNT}; "
        "if {[target current] ne $t} {error TB_TARGET_CHANGED}; "
        "format {TBM1|%%s|%%s|%%x|%%d|1|%%x|END} $id $t $a $w [lindex $v 0]}} %s %s 0x%" PRIx64 " %u",
        id, target, address, width);
    const char *args[] = {"console", command};
    if (n < 0 || (size_t)n >= sizeof command ||
        !tb_gdb_request(m->gdb, "-interpreter-exec", args, 2, TB_GDB_CONTEXT, MMIO_COOKIE, 3000, &m->token)) {
        tb_gdb_release(m->gdb, MMIO_COOKIE); return false;
    }
    m->result = (TbMmioResult){.status = TB_MMIO_PENDING, .address = address, .width = width,
        .generation = tb_gdb_generation(m->gdb)};
    m->active = true; return true;
}
static bool late_result(const TbMmio *m, const TbMiRecord *record)
{
    if (!m->late_token || !record || record->kind != TB_MI_RESULT || !record->token) { return false; }
    char token[32]; (void)snprintf(token, sizeof token, "%" PRIu64, m->late_token);
    return strcmp(token, record->token) == 0;
}
bool tb_mmio_event(TbMmio *m, const TbGdbEvent *event)
{
    if (m->active && event->kind == TB_GDB_RECORD && event->record && event->record->kind == TB_MI_TARGET) {
        tb_monitor_feed(&m->frame, event->record->stream.data, event->record->stream.length);
        return false;
    }
    if (event->kind == TB_GDB_STALE && late_result(m, event->record)) {
        m->late_token = 0;
        outcome(m, TB_MMIO_FAILED, "Late operation completed; no sample accepted and no retry sent. Read again explicitly."); return true;
    }
    if (!m->active || event->cookie != MMIO_COOKIE || event->token != m->token ||
        (event->kind != TB_GDB_REPLY && event->kind != TB_GDB_STALE &&
         event->kind != TB_GDB_TIMEOUT && event->kind != TB_GDB_CANCELLED)) { return false; }
    if (event->kind == TB_GDB_TIMEOUT || !event->record) {
        m->late_token = m->token;
        outcome(m, TB_MMIO_UNCERTAIN, "MMIO outcome uncertain; hardware may have been read. No retry. Wait for reconciliation or reconnect."); return true;
    }
    if (event->kind != TB_GDB_REPLY) {
        outcome(m, TB_MMIO_FAILED, "Context changed; MMIO result discarded. Hardware may have been read; no retry sent."); return true;
    }
    bool done = strcmp(event->record->klass, "done") == 0;
    uint64_t value;
    if (tb_monitor_finish(&m->frame, done, &value)) {
        m->result.value = value; outcome(m, TB_MMIO_COMPLETE, "");
    } else if (done) {
        outcome(m, TB_MMIO_UNCERTAIN, "Untrustworthy MMIO response; hardware may have been read. No retry; reconnect before another read.");
    } else {
        outcome(m, TB_MMIO_FAILED, "OpenOCD read failed; hardware may have been accessed. No retry sent.");
    }
    return true;
}
