#ifndef THE_BUGGER_CODE_H
#define THE_BUGGER_CODE_H

#include "model/context.h"
#include "model/browse.h"
#include "model/images.h"

enum {
    TB_CODE_VIEWS        = 32,
    TB_CODE_FILES        = 256,
    TB_CODE_INSNS        = 128,
    TB_CODE_SOURCE_BYTES = 1024 * 1024,
    TB_CODE_LINES        = 32768,
    TB_CODE_PATH         = 4096,
    TB_CODE_RULES        = 8,
    TB_CODE_TABS         = 8,
    TB_CODE_ROWS         = 3 * TB_CODE_INSNS + 1
};

typedef struct {
    char file[TB_CODE_PATH], full[TB_CODE_PATH];
} TbCodeFile_t;

typedef struct {
    char from[TB_CODE_PATH], to[TB_CODE_PATH];
} TbSourceRule_t;

typedef struct {
    char     address[48], opcodes[96], text[384], source[512];
    int      line;
    uint64_t numeric;
    unsigned bytes;
} TbInstruction_t;

typedef struct {
    char              path[TB_CODE_PATH], source_error[192], assembly_error[192];
    bool              stale, source_follow, assembly_follow, mixed, mixed_available, more, truncated;
    bool              assembly_stopped, assembly_historical;
    TbGdbGeneration_t assembly_generation;
    uint64_t          assembly_epoch;
    uint64_t          gap_begin, gap_end;
    int               selected_line, stopped_line;
    size_t            line_count, instruction_count;
    TbInstruction_t   instructions[TB_CODE_INSNS];
    uint64_t          anchor, next_address, source_revision, assembly_revision;
} TbCodeView_t;

typedef enum { TB_CODE_INSTRUCTION, TB_CODE_SOURCE, TB_CODE_GAP } TbCodeRowKind_e;

typedef struct {
    TbCodeRowKind_e kind;
    size_t          instruction;
    uint64_t        begin, end;
} TbCodeRow_t;

/* The same flattened rows drive rendering, scrolling, selection and markers.
 * Pass NULL for a count only; otherwise provide TB_CODE_ROWS slots. */
size_t tb_code_rows(const TbCodeView_t *view, TbCodeRow_t *rows);

typedef struct tb_code TbCode_t;

typedef struct {
    TbImageReview_t   image;
    TbGdbGeneration_t generation;
    uint64_t          epoch;
} TbCodeImageReview_t;

/* Remote configured ELF review: metadata-only, no target access or permission.
 * Bias is added to linked virtual addresses; nonuniform relocation is not
 * inferred. User confirmation is distinct from native mapping verification. */
bool tb_code_image_review(TbCode_t *code, uint64_t bias, TbCodeImageReview_t *review);
bool tb_code_image_range(const TbCode_t *code, const TbCodeImageReview_t *review, size_t index,
                         TbImageRange_t *range);
bool tb_code_image_confirm(TbCode_t *code, const TbCodeImageReview_t *review);
void tb_code_image_invalidate(TbCode_t *code);
TbImageTrust_e tb_code_assembly_mapping(const TbCode_t *code, int view, size_t first, size_t count);
const char *tb_code_gap_label(const TbCode_t *code, int view, uint64_t begin, uint64_t end);

TbCode_t *tb_code_create(TbGdb_t *gdb, TbContext_t *context);
TbBrowseCache_t *tb_code_cache(TbCode_t *code); /* Borrowed session cache. */
void tb_code_native(TbCode_t *code, pid_t pid, const char *executable);
void tb_code_raw(TbCode_t *code, int view);
void tb_code_destroy(TbCode_t *code);
void tb_code_reset(TbCode_t *code);
bool tb_code_event(TbCode_t *code, const TbGdbEvent_t *event);
void tb_code_pump(TbCode_t *code);
void tb_code_visible(TbCode_t *code, int view, bool source, bool assembly);
bool tb_code_settled(const TbCode_t *code, int view);
const TbCodeView_t *tb_code_view(const TbCode_t *code, int view);
/* Classify only the displayed instruction slice, never the last fetch request. */
const char *tb_code_assembly_notice(const TbCode_t *code, int view, size_t first, size_t count);
uint64_t tb_code_revision(const TbCode_t *code);
const char *tb_code_message(const TbCode_t *code);
/* Source navigation is read-only and restricted to GDB-known files. */
size_t tb_code_file_count(const TbCode_t *code);
const TbCodeFile_t *tb_code_file(const TbCode_t *code, size_t index);
bool tb_code_files_limited(const TbCode_t *code);
void tb_code_request_files(TbCode_t *code);
bool tb_code_open(TbCode_t *code, int view, size_t file);
/* Tab zero follows the selected frame; other tabs retain GDB-known files. */
size_t tb_code_tab_count(const TbCode_t *code, int view);
size_t tb_code_tab_active(const TbCode_t *code, int view);
const TbCodeFile_t *tb_code_tab_file(const TbCode_t *code, int view, size_t tab);
bool tb_code_tab_select(TbCode_t *code, int view, size_t tab);
bool tb_code_tab_close(TbCode_t *code, int view, size_t tab);
/* Definition-only restore: pins start unavailable and retain their original
 * identity. Retry resolves through the current GDB catalog before local reads. */
bool tb_code_tab_restore(TbCode_t *code, int view, const TbCodeFile_t *file);
bool tb_code_tab_retry(TbCode_t *code, int view, size_t tab);
bool tb_code_tab_unavailable(const TbCode_t *code, int view, size_t tab);
bool tb_code_files_ready(const TbCode_t *code);
bool tb_code_catalog_pending(const TbCode_t *code);
/* Clear a closed/reused pane, including its browsing history and request identity. */
void tb_code_clear(TbCode_t *code, int view);
void tb_code_tab_reset(TbCode_t *code, int view);
bool tb_code_source_line(const TbCode_t *code, int view, size_t line, char *out, size_t capacity);
/* Local source annotations only; lookup never performs filesystem/debugger I/O.
 * A true result may be an empty source line. Missing text remains explicit. */
bool tb_code_mixed_line(const TbCode_t *code, int view, const TbInstruction_t *instruction, char *out,
                        size_t capacity);
int tb_code_find(const TbCode_t *code, int view, const char *needle, int after);
bool tb_code_address(TbCode_t *code, int view, const char *address_or_symbol);
bool tb_code_assembly_page(TbCode_t *code, int view, int direction);
void tb_code_mixed(TbCode_t *code, int view, bool mixed);
void tb_code_return(TbCode_t *code, int view); /* Follow selected stopped context. */
bool tb_code_stopped(TbCode_t *code); /* Inspect original stopped thread/frame zero. */
/* Disassembly-only navigation; neither action selects a thread/frame. */
bool tb_code_return_stop(TbCode_t *code, int view);
bool tb_code_follow_frame(TbCode_t *code, int view);
void tb_code_refresh(TbCode_t *code);
/* Settings apply through GDB, never as shell snippets. Ordered rules are
 * retained only after successful replies. Basic profile persistence is M13. */
bool tb_code_add_directory(TbCode_t *code, const char *path);
bool tb_code_add_rule(TbCode_t *code, const char *from, const char *to);
bool tb_code_clear_settings(TbCode_t *code);
/* Queue only for a fresh session. Directories are highest-priority first;
 * apply them in reverse because GDB prepends each directory. */
bool tb_code_restore_settings(TbCode_t *code, const char *const directories[], size_t count,
                              const TbSourceRule_t *rules, size_t rule_count);
bool tb_code_restoring(const TbCode_t *code);
bool tb_code_settings_busy(const TbCode_t *code);
void tb_code_cancel_settings(TbCode_t *code);
size_t tb_code_directory_count(const TbCode_t *code);
const char *tb_code_directory(const TbCode_t *code, size_t index);
size_t tb_code_rule_count(const TbCode_t *code);
const TbSourceRule_t *tb_code_rule(const TbCode_t *code, size_t index);
/* Conservative address/symbol grammar and length-aware terminal escaping. */
bool tb_code_location(const char *text, uint64_t *address, bool *symbol);
void tb_code_safe(char *out, size_t capacity, const char *bytes, size_t length);

#endif // THE_BUGGER_CODE_H
