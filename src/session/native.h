#ifndef THE_BUGGER_NATIVE_H
#define THE_BUGGER_NATIVE_H
#include "session.h"
/* Source compatibility for accepted native integrations. The implementation
 * and target-neutral operations are owned by the shared session module. */
typedef TbSessionWords TbNativeWords;
typedef TbSessionConfig TbNativeConfig;
typedef TbSessionState TbNativeState;
typedef TbSession TbNative;
#define TB_NATIVE_CLOSED TB_SESSION_CLOSED
#define TB_NATIVE_CLOSING TB_SESSION_CLOSING
#define TB_NATIVE_EXITED TB_SESSION_EXITED
#define TB_NATIVE_FAILED TB_SESSION_FAILED
#define TB_NATIVE_IDLE TB_SESSION_IDLE
#define TB_NATIVE_LOG_LINES TB_SESSION_LOG_LINES
#define TB_NATIVE_LOG_WIDTH TB_SESSION_LOG_WIDTH
#define TB_NATIVE_READY TB_SESSION_READY
#define TB_NATIVE_RUNNING TB_SESSION_RUNNING
#define TB_NATIVE_STARTING TB_SESSION_STARTING
#define TB_NATIVE_STOPPED TB_SESSION_STOPPED
#define TB_NATIVE_WORDS TB_SESSION_WORDS
#define TB_NATIVE_WORD_BYTES TB_SESSION_WORD_BYTES
#define tb_native_words tb_session_words
#define tb_native_create tb_session_create
#define tb_native_destroy tb_session_destroy
#define tb_native_start tb_session_start
#define tb_native_continue tb_session_continue
#define tb_native_pause tb_session_pause
#define tb_native_step tb_session_step
#define tb_native_console tb_session_console
#define tb_native_save_log tb_session_save_log
#define tb_native_close tb_session_close
#define tb_native_pump tb_session_pump
#define tb_native_pollfds tb_session_pollfds
#define tb_native_timeout tb_session_timeout
#define tb_native_state tb_session_state
#define tb_native_state_name tb_session_state_name
#define tb_native_message tb_session_message
#define tb_native_revision tb_session_revision
#define tb_native_log_count tb_session_log_count
#define tb_native_log_line tb_session_log_line
#define tb_native_target_pid tb_session_target_pid
#define tb_native_debugger_pid tb_session_debugger_pid
#define tb_native_attached tb_session_attached
#define tb_native_active tb_session_active
#define tb_native_context tb_session_context
#define tb_native_code tb_session_code
#define tb_native_breakpoints tb_session_breakpoints
#define tb_native_registers tb_session_registers
#define tb_native_memory tb_session_memory
#define tb_native_variables tb_session_variables
#define tb_native_run_to tb_session_run_to
#define tb_native_run_at tb_session_run_at
/* Linux metadata parser; never used for remote target identifiers. */
bool tb_native_memory_region(const char *row, TbMemoryRegion *region);
#endif
