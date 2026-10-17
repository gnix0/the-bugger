#ifndef THE_BUGGER_NATIVE_H
#define THE_BUGGER_NATIVE_H

#include "controller.h"

enum { TB_NATIVE_WORDS = 32, TB_NATIVE_WORD_BYTES = 512,
       TB_NATIVE_LOG_LINES = 512, TB_NATIVE_LOG_WIDTH = 256 };
typedef struct { char values[TB_NATIVE_WORDS][TB_NATIVE_WORD_BYTES]; size_t count; } TbNativeWords;
/* Quoted whitespace/backslash parsing only; no expansions or shell execution. */
bool tb_native_words(const char *input, TbNativeWords *words, char *error, size_t capacity);

typedef struct {
    bool attach;
    pid_t pid;
    const char *gdb, *executable, *directory;
    const TbNativeWords *arguments, *environment;
} TbNativeConfig;
typedef enum { TB_NATIVE_IDLE, TB_NATIVE_STARTING, TB_NATIVE_READY,
               TB_NATIVE_RUNNING, TB_NATIVE_STOPPED, TB_NATIVE_EXITED,
               TB_NATIVE_CLOSING, TB_NATIVE_CLOSED, TB_NATIVE_FAILED } TbNativeState;
typedef struct TbNative TbNative;

TbNative *tb_native_create(void);
bool tb_native_destroy(TbNative *native); /* Refuses a live debugger. */
bool tb_native_start(TbNative *native, const TbNativeConfig *config);
bool tb_native_continue(TbNative *native);
bool tb_native_pause(TbNative *native);
bool tb_native_console(TbNative *native, const char *command);
bool tb_native_save_log(TbNative *native, const char *path);
/* Launch: terminate the still-owned inferior; Attach: detach/resume. Caller
 * confirms this policy before normal close; signals also use this cleanup. */
void tb_native_close(TbNative *native);
void tb_native_pump(TbNative *native, int wait_ms);
size_t tb_native_pollfds(const TbNative *native, struct pollfd fds[5]);
int tb_native_timeout(const TbNative *native, int maximum_ms);
TbNativeState tb_native_state(const TbNative *native);
const char *tb_native_state_name(const TbNative *native);
const char *tb_native_message(const TbNative *native);
uint64_t tb_native_revision(const TbNative *native);
size_t tb_native_log_count(const TbNative *native);
const char *tb_native_log_line(const TbNative *native, size_t row);
pid_t tb_native_target_pid(const TbNative *native);
pid_t tb_native_debugger_pid(const TbNative *native);
bool tb_native_attached(const TbNative *native);
bool tb_native_active(const TbNative *native);

#endif
