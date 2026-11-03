#ifndef THE_BUGGER_SETUP_CAPTURE_H
#define THE_BUGGER_SETUP_CAPTURE_H

#include "session/setup.h"
#include "tui/demo.h"
#include "model/code.h"
#include "model/memory.h"
#include "model/variables.h"
#include "model/breakpoints.h"

typedef struct tb_setup_capture TbSetupCapture_t;

TbSetupCapture_t *tb_setup_capture_create(const TbSetup_t *saved);
void tb_setup_capture_destroy(TbSetupCapture_t *capture);
/* Metadata only. No requests or reads. Expressions are the visible Memory drafts. */
bool tb_setup_capture_update(TbSetupCapture_t *capture, const TbLayout_t *layout, const TbDemo_t *workspace,
                             const TbCode_t *code, const TbMemory_t *memory, const TbVariables_t *variables,
                             const TbBreakpoints_t *breakpoints, const char *const expressions[TB_MAX_PANES]);
const TbSetup_t *tb_setup_capture_current(const TbSetupCapture_t *capture);
bool tb_setup_capture_dirty(const TbSetupCapture_t *capture);
bool tb_setup_capture_saved(TbSetupCapture_t *capture);
bool tb_setup_capture_baseline(TbSetupCapture_t *capture, const TbSetup_t *saved);
/* Fresh-session local definitions. Breakpoints remain retained/uninstalled. */
bool tb_setup_capture_restore(TbSetupCapture_t *capture, const TbSetup_t *saved, TbLayout_t *layout,
                              TbDemo_t *workspace, TbCode_t *code, TbMemory_t *memory,
                              TbVariables_t *variables);
size_t tb_setup_capture_retained(const TbSetupCapture_t *capture);
const TbSetupBreak_t *tb_setup_capture_retained_break(const TbSetupCapture_t *capture, size_t index);
bool tb_setup_capture_adopt_break(TbSetupCapture_t *capture, uint64_t identity, uint64_t model_identity);
uint64_t tb_setup_capture_import_break(TbSetupCapture_t *capture, const TbBreakDefinition_t *definition);
bool tb_setup_capture_remove_break(TbSetupCapture_t *capture, uint64_t identity);

#endif // THE_BUGGER_SETUP_CAPTURE_H
