#ifndef THE_BUGGER_SETUP_CAPTURE_H
#define THE_BUGGER_SETUP_CAPTURE_H
#include "session/setup.h"
#include "tui/demo.h"
#include "model/code.h"
#include "model/memory.h"
#include "model/variables.h"
#include "model/breakpoints.h"

typedef struct TbSetupCapture TbSetupCapture;
TbSetupCapture *tb_setup_capture_create(const TbSetup *saved);
void tb_setup_capture_destroy(TbSetupCapture *capture);
/* Metadata only. No requests or reads. Expressions are the visible Memory drafts. */
bool tb_setup_capture_update(TbSetupCapture *capture, const TbLayout_t *layout, const TbDemo *workspace,
                            const TbCode_t *code, const TbMemory_t *memory, const TbVariables_t *variables,
                            const TbBreakpoints_t *breakpoints, const char *const expressions[TB_MAX_PANES]);
const TbSetup *tb_setup_capture_current(const TbSetupCapture *capture);
bool tb_setup_capture_dirty(const TbSetupCapture *capture);
bool tb_setup_capture_saved(TbSetupCapture *capture);
bool tb_setup_capture_baseline(TbSetupCapture *capture, const TbSetup *saved);
/* Fresh-session local definitions. Breakpoints remain retained/uninstalled. */
bool tb_setup_capture_restore(TbSetupCapture *capture, const TbSetup *saved, TbLayout_t *layout,
                              TbDemo *workspace, TbCode_t *code, TbMemory_t *memory, TbVariables_t *variables);
size_t tb_setup_capture_retained(const TbSetupCapture *capture);
const TbSetupBreak *tb_setup_capture_retained_break(const TbSetupCapture *capture, size_t index);
bool tb_setup_capture_adopt_break(TbSetupCapture *capture, uint64_t identity, uint64_t model_identity);
uint64_t tb_setup_capture_import_break(TbSetupCapture *capture, const TbBreakDefinition_t *definition);
bool tb_setup_capture_remove_break(TbSetupCapture *capture, uint64_t identity);
#endif
