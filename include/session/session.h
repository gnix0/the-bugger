#ifndef THE_BUGGER_SESSION_H
#define THE_BUGGER_SESSION_H

#include "gdb/controller.h"
#include "model/context.h"
#include "model/code.h"
#include "model/breakpoints.h"
#include "model/registers.h"
#include "model/memory.h"
#include "model/stack.h"
#include "model/variables.h"
#include "session/openocd.h"

enum {
    TB_SESSION_WORDS      = 32,
    TB_SESSION_WORD_BYTES = 512,
    TB_SESSION_LOG_LINES  = 512,
    TB_SESSION_LOG_WIDTH  = 256
};

typedef struct {
    char   values[TB_SESSION_WORDS][TB_SESSION_WORD_BYTES];
    size_t count;
} TbSessionWords_t;

/* Quoted whitespace/backslash parsing only; no expansions or shell execution. */
bool tb_session_words(const char *input, TbSessionWords_t *words, char *error, size_t capacity);

typedef enum {
    TB_SESSION_LOCAL,
    TB_SESSION_REMOTE,
    TB_SESSION_EXTENDED_REMOTE
} TbSessionKind_e;

typedef struct {
    bool            connected;
    bool            octet_addressing;
    bool            local_process;
    bool            server_owned;
    bool            openocd;
    unsigned        address_bits;
    TbMemoryOrder_e byte_order;
    char            architecture[64];
    char            target_id[32];
    char            symbol_image[4096];
    bool            image_relocation_known;
} TbSessionCapabilities_t;

/* TCP only: never accepts a GDB pipe, serial path or command. */
bool tb_session_endpoint(const char *endpoint);

typedef struct {
    TbSessionKind_e        kind;
    const char            *endpoint;
    const TbOpenOcdPlan_t *openocd;
    /* Affirmative adapter/user evidence, not inferred from target readability.
     * Empty for unknown servers. Never automatically derived from an ELF. */
    const TbMemoryRegion_t *ordinary_regions;
    size_t                  ordinary_region_count;
    bool                    attach;
    pid_t                   pid;
    const char             *gdb;
    const char             *executable;
    const char             *directory;
    const TbSessionWords_t *arguments;
    const TbSessionWords_t *environment;
} TbSessionConfig_t;

typedef struct tb_session TbSession_t;

/* Fixed device description only. Identity/source are USER-CONFIRMED assertions,
 * never silicon detection. Tokens bind the exact live connection and policy. */
typedef struct {
    uint64_t         connection;
    uint64_t         configuration;
    uint64_t         evidence;
    TbMemoryRegion_t region;
    char             endpoint[256];
    char             inferior[32];
    char             device[32];
    char             source[128];
} TbSessionRamReview_t;

bool tb_session_ram_review(TbSession_t *session, TbSessionRamReview_t *review);
bool tb_session_ram_confirm(TbSession_t *session, const TbSessionRamReview_t *review);
void tb_session_ram_revoke(TbSession_t *session);
bool tb_session_ram_confirmed(const TbSession_t *session);

typedef enum {
    TB_SESSION_IDLE,
    TB_SESSION_STARTING,
    TB_SESSION_READY,
    TB_SESSION_RUNNING,
    TB_SESSION_STOPPED,
    TB_SESSION_EXITED,
    TB_SESSION_CLOSING,
    TB_SESSION_CLOSED,
    TB_SESSION_FAILED
} TbSessionState_e;

TbSession_t *tb_session_create(void);
const TbSessionCapabilities_t *tb_session_capabilities(const TbSession_t *session);
TbSessionKind_e tb_session_kind(const TbSession_t *session);
bool tb_session_destroy(TbSession_t *native); /* Refuses a live debugger. */
bool tb_session_start(TbSession_t *native, const TbSessionConfig_t *config);

/* Local coordination only. Pause/close remain available. Failure acknowledgment
 * gates execution but does not imply that an unresolved breakpoint is installed. */
void tb_session_setup_interlock(TbSession_t *session, bool active, bool unacknowledged);
bool tb_session_continue(TbSession_t *native);
bool tb_session_pause(TbSession_t *native);

typedef enum {
    TB_STEP_OVER,
    TB_STEP_INTO,
    TB_STEP_OUT
} TbStep_e;

bool tb_session_step(TbSession_t *native, TbStep_e step, bool instruction);
bool tb_session_console(TbSession_t *native, const char *command);

/* Metadata-only review of the documented physically validated MMIO setup.
 * These functions perform no hardware requests. Confirmation is the user's
 * assertion that every displayed binding matches, never automatic detection. */
bool tb_session_mmio_review(TbSession_t *session, TbMemoryBackend_t *review);
bool tb_session_mmio_confirm(TbSession_t *session, const TbMemoryBackend_t *review);
bool tb_session_save_log(TbSession_t *native, const char *path);

/* Local launch: terminate the still-owned inferior; local attach: detach/resume.
 * Remote: disconnect only, with target disposition controlled by the server.
 * Caller confirms this policy before normal close; signals use the same cleanup. */
void tb_session_close(TbSession_t *native);

/* Explicit OpenOCD-only operations; monitor errors remain visible. */
bool tb_session_reset_halt(TbSession_t *session);
bool tb_session_halt(TbSession_t *session);
void tb_session_disconnect(TbSession_t *session, bool resume);
void tb_session_pump(TbSession_t *native, int wait_ms);
size_t tb_session_pollfds(const TbSession_t *native, struct pollfd fds[5]);
int tb_session_timeout(const TbSession_t *native, int maximum_ms);
TbSessionState_e tb_session_state(const TbSession_t *native);
const char *tb_session_state_name(const TbSession_t *native);
const char *tb_session_message(const TbSession_t *native);
uint64_t tb_session_revision(const TbSession_t *native);
size_t tb_session_log_count(const TbSession_t *native);
const char *tb_session_log_line(const TbSession_t *native, size_t row);
pid_t tb_session_target_pid(const TbSession_t *native);
pid_t tb_session_debugger_pid(const TbSession_t *native);
bool tb_session_attached(const TbSession_t *native);
bool tb_session_active(const TbSession_t *native);
TbContext_t *tb_session_context(TbSession_t *native);
TbCode_t *tb_session_code(TbSession_t *native);
TbBreakpoints_t *tb_session_breakpoints(TbSession_t *native);
TbRegisters_t *tb_session_registers(TbSession_t *native);
TbMemory_t *tb_session_memory(TbSession_t *native);
TbStack_t *tb_session_stack(TbSession_t *session);
TbVariables_t *tb_session_variables(TbSession_t *native);
bool tb_session_run_to(TbSession_t *native, const char *location);
bool tb_session_run_at(TbSession_t *native, TbBreakLocation_t location);

#endif // THE_BUGGER_SESSION_H
