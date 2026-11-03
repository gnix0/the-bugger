#ifndef THE_BUGGER_SETUP_RESTORE_H
#define THE_BUGGER_SETUP_RESTORE_H

#include "tui/setup_capture.h"
#include "session/session.h"

typedef struct tb_setup_restore TbSetupRestore_t;

/* Owns progress only. Capture retains the definitions and unresolved entries. */
TbSetupRestore_t *tb_setup_restore_create(void);
void tb_setup_restore_destroy(TbSetupRestore_t *restore);
void tb_setup_restore_begin(TbSetupRestore_t *restore);
void tb_setup_restore_pump(TbSetupRestore_t *restore, TbSetupCapture_t *capture, TbSession_t *session);
void tb_setup_restore_cancel(TbSetupRestore_t *restore, TbSession_t *session);
bool tb_setup_restore_active(const TbSetupRestore_t *restore);
bool tb_setup_restore_attempted(const TbSetupRestore_t *restore);
const char *tb_setup_restore_status(const TbSetupRestore_t *restore);
void tb_setup_restore_acknowledge(TbSetupRestore_t *restore);
bool tb_setup_restore_acknowledgment(const TbSetupRestore_t *restore);

#endif // THE_BUGGER_SETUP_RESTORE_H
