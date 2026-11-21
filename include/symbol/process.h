#ifndef THE_BUGGER_SYMBOL_PROCESS_H
#define THE_BUGGER_SYMBOL_PROCESS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* Internal Linux process ownership; the caller owns the channel and storage.
 * Initialize once before use. Never reset an unreaped owner or copy this value. */
typedef enum {
    TB_SYMBOL_PROCESS_IDLE,
    TB_SYMBOL_PROCESS_RUNNING,
    TB_SYMBOL_PROCESS_STOPPING,
    TB_SYMBOL_PROCESS_TERM,
    TB_SYMBOL_PROCESS_KILL,
    TB_SYMBOL_PROCESS_REAPED,
    TB_SYMBOL_PROCESS_UNCERTAIN
} TbSymbolProcessState_e;

typedef struct {
    pid_t                  pid;
    int                    pidfd;
    TbSymbolProcessState_e state;
    uint64_t               deadline_ns;
    uint64_t               cancel_grace_ns;
    uint64_t               term_grace_ns;
    int                    wait_status;
    int                    error;
    bool                   wait_owned;
} TbSymbolProcess_t;

bool tb_symbol_process_init(TbSymbolProcess_t *process, uint64_t cancel_grace_ns,
                            uint64_t term_grace_ns);
/* Explicit start only. path is literal, never PATH searched or shell expanded.
 * Duplicates the child socket to FD 3 and isolates all other inherited FDs.
 * Caller retains socket ownership on every outcome and must close its child end.
 * A false result can still retain an unreaped child: inspect state and pump it.
 * Requires Linux pidfd/close_range support, default SIGCHLD and exclusive reap.
 * Exec/setup failure is asynchronous (exit 127), reported through owned reap. */
bool tb_symbol_process_start(TbSymbolProcess_t *process, const char *path, int socket_fd);
/* Begin cooperative shutdown grace. Caller separately sends bounded cancellation
 * or shutdown and closes an unusable channel. No implicit interrupt/resume. */
void tb_symbol_process_stop(TbSymbolProcess_t *process, uint64_t now_ns);
/* Nonblocking wait/escalation. Signals only through the verified owned pidfd.
 * Uncertain reap permanently prevents another start on this owner. */
void tb_symbol_process_pump(TbSymbolProcess_t *process, uint64_t now_ns);
/* Safe to relinquish storage only when no owned child remains. */
bool tb_symbol_process_released(const TbSymbolProcess_t *process);

#endif // THE_BUGGER_SYMBOL_PROCESS_H
