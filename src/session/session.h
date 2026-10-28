#ifndef THE_BUGGER_SESSION_H
#define THE_BUGGER_SESSION_H

#include "controller.h"
#include "context.h"
#include "code.h"
#include "breakpoints.h"
#include "registers.h"
#include "memory.h"
#include "variables.h"
#include "openocd.h"

enum { TB_SESSION_WORDS = 32, TB_SESSION_WORD_BYTES = 512,
       TB_SESSION_LOG_LINES = 512, TB_SESSION_LOG_WIDTH = 256 };
typedef struct { char values[TB_SESSION_WORDS][TB_SESSION_WORD_BYTES]; size_t count; } TbSessionWords;
/* Quoted whitespace/backslash parsing only; no expansions or shell execution. */
bool tb_session_words(const char *input, TbSessionWords *words, char *error, size_t capacity);

typedef enum { TB_SESSION_LOCAL, TB_SESSION_REMOTE, TB_SESSION_EXTENDED_REMOTE } TbSessionKind;
typedef struct {
    bool connected, octet_addressing, local_process, server_owned, openocd;
    unsigned address_bits;
    TbMemoryOrder byte_order;
    char architecture[64], target_id[32], symbol_image[4096];
    bool image_relocation_known;
} TbSessionCapabilities;
/* TCP only: never accepts a GDB pipe, serial path or command. */
bool tb_session_endpoint(const char *endpoint);

typedef struct {
    TbSessionKind kind;
    const char *endpoint;
    const TbOpenOcdPlan *openocd;
    /* Affirmative adapter/user evidence, not inferred from target readability.
     * Empty for unknown servers. Never automatically derived from an ELF. */
    const TbMemoryRegion *ordinary_regions;
    size_t ordinary_region_count;
    bool attach;
    pid_t pid;
    const char *gdb, *executable, *directory;
    const TbSessionWords *arguments, *environment;
} TbSessionConfig;
typedef enum { TB_SESSION_IDLE, TB_SESSION_STARTING, TB_SESSION_READY,
               TB_SESSION_RUNNING, TB_SESSION_STOPPED, TB_SESSION_EXITED,
               TB_SESSION_CLOSING, TB_SESSION_CLOSED, TB_SESSION_FAILED } TbSessionState;
typedef struct TbSession TbSession;

TbSession *tb_session_create(void);
const TbSessionCapabilities *tb_session_capabilities(const TbSession *session);
TbSessionKind tb_session_kind(const TbSession *session);
bool tb_session_destroy(TbSession *native); /* Refuses a live debugger. */
bool tb_session_start(TbSession *native, const TbSessionConfig *config);
/* Local coordination only. Pause/close remain available. Failure acknowledgment
 * gates execution but does not imply that an unresolved breakpoint is installed. */
void tb_session_setup_interlock(TbSession *session, bool active, bool unacknowledged);
bool tb_session_continue(TbSession *native);
bool tb_session_pause(TbSession *native);
typedef enum { TB_STEP_OVER, TB_STEP_INTO, TB_STEP_OUT } TbStep;
bool tb_session_step(TbSession *native, TbStep step, bool instruction);
bool tb_session_console(TbSession *native, const char *command);
/* Metadata-only review of the documented physically validated MMIO setup.
 * These functions perform no hardware requests. Confirmation is the user's
 * assertion that every displayed binding matches, never automatic detection. */
bool tb_session_mmio_review(TbSession *session, TbMemoryBackend *review);
bool tb_session_mmio_confirm(TbSession *session, const TbMemoryBackend *review);
bool tb_session_save_log(TbSession *native, const char *path);
/* Local launch: terminate the still-owned inferior; local attach: detach/resume.
 * Remote: disconnect only, with target disposition controlled by the server.
 * Caller confirms this policy before normal close; signals use the same cleanup. */
void tb_session_close(TbSession *native);
/* Explicit OpenOCD-only operations; monitor errors remain visible. */
bool tb_session_reset_halt(TbSession *session);
bool tb_session_halt(TbSession *session);
void tb_session_disconnect(TbSession *session, bool resume);
void tb_session_pump(TbSession *native, int wait_ms);
size_t tb_session_pollfds(const TbSession *native, struct pollfd fds[5]);
int tb_session_timeout(const TbSession *native, int maximum_ms);
TbSessionState tb_session_state(const TbSession *native);
const char *tb_session_state_name(const TbSession *native);
const char *tb_session_message(const TbSession *native);
uint64_t tb_session_revision(const TbSession *native);
size_t tb_session_log_count(const TbSession *native);
const char *tb_session_log_line(const TbSession *native, size_t row);
pid_t tb_session_target_pid(const TbSession *native);
pid_t tb_session_debugger_pid(const TbSession *native);
bool tb_session_attached(const TbSession *native);
bool tb_session_active(const TbSession *native);
TbContext *tb_session_context(TbSession *native);
TbCode *tb_session_code(TbSession *native);
TbBreakpoints *tb_session_breakpoints(TbSession *native);
TbRegisters *tb_session_registers(TbSession *native);
TbMemory *tb_session_memory(TbSession *native);
TbVariables *tb_session_variables(TbSession *native);
bool tb_session_run_to(TbSession *native, const char *location);
bool tb_session_run_at(TbSession *native, TbBreakLocation location);

#endif
