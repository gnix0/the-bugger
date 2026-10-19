#ifndef THE_BUGGER_GDB_CONTROLLER_H
#define THE_BUGGER_GDB_CONTROLLER_H

#include "mi.h"
#include <poll.h>
#include <stdint.h>
#include <sys/types.h>

enum { TB_GDB_MAX_PENDING = 64, TB_GDB_MAX_COMMAND = 16384,
       TB_GDB_IO_BUDGET = 16384 };

typedef struct TbGdb TbGdb;
typedef enum { TB_GDB_DISCONNECTED, TB_GDB_STARTING, TB_GDB_READY,
               TB_GDB_RUNNING, TB_GDB_STOPPED, TB_GDB_EXITED,
               TB_GDB_CLOSING } TbGdbState;
typedef enum { TB_GDB_SESSION, TB_GDB_STOP, TB_GDB_CONTEXT } TbGdbScope;
typedef struct { uint64_t session, stop, context; } TbGdbGeneration;
typedef enum { TB_GDB_STATE, TB_GDB_RECORD, TB_GDB_REPLY, TB_GDB_STALE,
               TB_GDB_TIMEOUT, TB_GDB_CANCELLED, TB_GDB_DIAGNOSTIC,
               TB_GDB_STDERR, TB_GDB_CHILD_EXIT, TB_GDB_RECONCILED } TbGdbEventKind;
typedef struct {
    TbGdbEventKind kind;
    uint64_t token, cookie;
    TbGdbGeneration generation; /* Request origin for completion events. */
    const TbMiRecord *record;
    const char *text;
    size_t length;
    int status;                /* errno for diagnostic; wait status for exit. */
} TbGdbEvent;

/* Events and all their pointers are borrowed for the callback only. Do not
 * mutate/reenter/destroy the controller from its callback. No global handlers. */
typedef void (*TbGdbEmit)(void *context, const TbGdbEvent *event);
TbGdb *tb_gdb_create(TbGdbEmit emit, void *context);
/* Destroy only after disconnected: returns false while an owned child exists.
 * First close and pump until disconnected; never abandon a live child. */
bool tb_gdb_destroy(TbGdb *gdb);

/* Spawn executable literally via argv, with no shell or target arguments.
 * Adds --quiet --nx --interpreter=mi3. Startup-only MI2 fallback requires an
 * explicit unrecognized-mi3 diagnostic before any prompt/command. */
bool tb_gdb_start(TbGdb *gdb, const char *executable, unsigned startup_ms);
/* Internal command API: operation is -lowercase-with-hyphens. Arguments are
 * C-string quoted when not plain ASCII tokens. GDB's CLI-derived -gdb-set/-gdb-show
 * commands use a restricted setting name and literal tails without CR/LF.
 * No raw command/record injection; environment assignments remain literal.
 * Deadline starts at enqueue. STOP/CONTEXT requests require a stopped target.
 * A true return owns the copied arguments and yields one completion event
 * (REPLY/STALE/TIMEOUT/CANCELLED). cookie is opaque caller identity. */
bool tb_gdb_request(TbGdb *gdb, const char *operation, const char *const args[],
                    size_t count, TbGdbScope scope, uint64_t cookie,
                    unsigned timeout_ms, uint64_t *token);
void tb_gdb_context_changed(TbGdb *gdb);
/* Explicit monitor actions may change registers without an MI stop event.
 * Verify through structured thread state; emits RECONCILED only on proof. */
bool tb_gdb_verify_stop(TbGdb *gdb);
/* Nonblocking close: command-input EOF, then TERM/KILL deadlines and reap.
 * Only the still-owned child PID is ever signalled. No process group signals. */
void tb_gdb_close(TbGdb *gdb);
/* Stop only the owned local debugger immediately, without allowing GDB quit
 * handlers to kill a remote inferior after an unconfirmed disconnect. */
void tb_gdb_abort(TbGdb *gdb);
/* At most budget bytes per pipe per pump; waits at most 25 ms, or an earlier
 * deadline. Expose the same fds/timeout to a caller's larger poll loop; after
 * poll call pump(gdb, 0). EINTR returns promptly for caller signal handling. */
void tb_gdb_pump(TbGdb *gdb, int wait_ms);
size_t tb_gdb_pollfds(const TbGdb *gdb, struct pollfd fds[3]);
int tb_gdb_timeout(const TbGdb *gdb, int maximum_ms);
TbGdbState tb_gdb_state(const TbGdb *gdb);
TbGdbGeneration tb_gdb_generation(const TbGdb *gdb);
pid_t tb_gdb_pid(const TbGdb *gdb);
int tb_gdb_mi_version(const TbGdb *gdb);
size_t tb_gdb_pending(const TbGdb *gdb);
uint64_t tb_gdb_now_ms(void);

#endif
