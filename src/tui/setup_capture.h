#ifndef THE_BUGGER_SETUP_CAPTURE_H
#define THE_BUGGER_SETUP_CAPTURE_H
#include "setup.h"
#include "demo.h"
#include "code.h"
#include "memory.h"
#include "variables.h"
#include "breakpoints.h"

typedef struct TbSetupCapture TbSetupCapture;
TbSetupCapture *tb_setup_capture_create(const TbSetup *saved);
void tb_setup_capture_destroy(TbSetupCapture *capture);
/* Metadata only. No requests or reads. Expressions are the visible Memory drafts. */
bool tb_setup_capture_update(TbSetupCapture *capture, const TbLayout *layout, const TbDemo *workspace,
                            const TbCode *code, const TbMemory *memory, const TbVariables *variables,
                            const TbBreakpoints *breakpoints, const char *const expressions[TB_MAX_PANES]);
const TbSetup *tb_setup_capture_current(const TbSetupCapture *capture);
bool tb_setup_capture_dirty(const TbSetupCapture *capture);
bool tb_setup_capture_saved(TbSetupCapture *capture);
bool tb_setup_capture_baseline(TbSetupCapture *capture, const TbSetup *saved);
#endif
