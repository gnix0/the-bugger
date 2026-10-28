#ifndef THE_BUGGER_SETUP_RESTORE_H
#define THE_BUGGER_SETUP_RESTORE_H
#include "setup_capture.h"
#include "session.h"
typedef struct TbSetupRestore TbSetupRestore;
/* Owns progress only. Capture retains the definitions and unresolved entries. */
TbSetupRestore *tb_setup_restore_create(void);
void tb_setup_restore_destroy(TbSetupRestore *restore);
void tb_setup_restore_begin(TbSetupRestore *restore);
void tb_setup_restore_pump(TbSetupRestore *restore, TbSetupCapture *capture, TbSession *session);
void tb_setup_restore_cancel(TbSetupRestore *restore, TbSession *session);
bool tb_setup_restore_active(const TbSetupRestore *restore);
bool tb_setup_restore_attempted(const TbSetupRestore *restore);
const char *tb_setup_restore_status(const TbSetupRestore *restore);
void tb_setup_restore_acknowledge(TbSetupRestore *restore);
bool tb_setup_restore_acknowledgment(const TbSetupRestore *restore);
#endif
