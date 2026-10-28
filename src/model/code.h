#ifndef THE_BUGGER_CODE_H
#define THE_BUGGER_CODE_H

#include "context.h"
#include "browse.h"
#include "images.h"

enum { TB_CODE_VIEWS = 32, TB_CODE_FILES = 256, TB_CODE_INSNS = 128,
       TB_CODE_SOURCE_BYTES = 1024 * 1024, TB_CODE_LINES = 32768,
       TB_CODE_PATH = 4096, TB_CODE_RULES = 8, TB_CODE_TABS = 8,
       TB_CODE_ROWS = 3 * TB_CODE_INSNS + 1 };
typedef struct { char file[TB_CODE_PATH], full[TB_CODE_PATH]; } TbCodeFile;
typedef struct { char from[TB_CODE_PATH], to[TB_CODE_PATH]; } TbSourceRule;
typedef struct {
    char address[48], opcodes[96], text[384], source[512];
    int line;
    uint64_t numeric;
    unsigned bytes;
} TbInstruction;
typedef struct {
    char path[TB_CODE_PATH], source_error[192], assembly_error[192];
    bool stale, source_follow, assembly_follow, mixed, mixed_available, more, truncated;
    bool assembly_stopped, assembly_historical;
    TbGdbGeneration assembly_generation;
    uint64_t assembly_epoch;
    uint64_t gap_begin, gap_end;
    int selected_line, stopped_line;
    size_t line_count, instruction_count;
    TbInstruction instructions[TB_CODE_INSNS];
    uint64_t anchor, next_address, source_revision, assembly_revision;
} TbCodeView;
typedef enum { TB_CODE_INSTRUCTION, TB_CODE_SOURCE, TB_CODE_GAP } TbCodeRowKind;
typedef struct {
    TbCodeRowKind kind;
    size_t instruction;
    uint64_t begin, end;
} TbCodeRow;
/* The same flattened rows drive rendering, scrolling, selection and markers.
 * Pass NULL for a count only; otherwise provide TB_CODE_ROWS slots. */
size_t tb_code_rows(const TbCodeView *view, TbCodeRow *rows);
typedef struct TbCode TbCode;
typedef struct { TbImageReview image; TbGdbGeneration generation; uint64_t epoch; } TbCodeImageReview;
/* Remote configured ELF review: metadata-only, no target access or permission.
 * Bias is added to linked virtual addresses; nonuniform relocation is not
 * inferred. User confirmation is distinct from native mapping verification. */
bool tb_code_image_review(TbCode *code, uint64_t bias, TbCodeImageReview *review);
bool tb_code_image_range(const TbCode *code, const TbCodeImageReview *review, size_t index, TbImageRange *range);
bool tb_code_image_confirm(TbCode *code, const TbCodeImageReview *review);
void tb_code_image_invalidate(TbCode *code);
TbImageTrust tb_code_assembly_mapping(const TbCode *code, int view, size_t first, size_t count);
const char *tb_code_gap_label(const TbCode *code, int view, uint64_t begin, uint64_t end);

TbCode *tb_code_create(TbGdb *gdb, TbContext *context);
TbBrowseCache *tb_code_cache(TbCode *code); /* Borrowed session cache. */
void tb_code_native(TbCode *code, pid_t pid, const char *executable);
void tb_code_raw(TbCode *code, int view);
void tb_code_destroy(TbCode *code);
void tb_code_reset(TbCode *code);
bool tb_code_event(TbCode *code, const TbGdbEvent *event);
void tb_code_pump(TbCode *code);
void tb_code_visible(TbCode *code, int view, bool source, bool assembly);
const TbCodeView *tb_code_view(const TbCode *code, int view);
/* Classify only the displayed instruction slice, never the last fetch request. */
const char *tb_code_assembly_notice(const TbCode *code, int view, size_t first, size_t count);
uint64_t tb_code_revision(const TbCode *code);
const char *tb_code_message(const TbCode *code);
/* Source navigation is read-only and restricted to GDB-known files. */
size_t tb_code_file_count(const TbCode *code);
const TbCodeFile *tb_code_file(const TbCode *code, size_t index);
bool tb_code_files_limited(const TbCode *code);
void tb_code_request_files(TbCode *code);
bool tb_code_open(TbCode *code, int view, size_t file);
/* Tab zero follows the selected frame; other tabs retain GDB-known files. */
size_t tb_code_tab_count(const TbCode *code, int view);
size_t tb_code_tab_active(const TbCode *code, int view);
const TbCodeFile *tb_code_tab_file(const TbCode *code, int view, size_t tab);
bool tb_code_tab_select(TbCode *code, int view, size_t tab);
bool tb_code_tab_close(TbCode *code, int view, size_t tab);
/* Clear a closed/reused pane, including its browsing history and request identity. */
void tb_code_clear(TbCode *code, int view);
void tb_code_tab_reset(TbCode *code, int view);
bool tb_code_source_line(const TbCode *code, int view, size_t line, char *out, size_t capacity);
/* Local source annotations only; lookup never performs filesystem/debugger I/O.
 * A true result may be an empty source line. Missing text remains explicit. */
bool tb_code_mixed_line(const TbCode *code, int view, const TbInstruction *instruction, char *out, size_t capacity);
int tb_code_find(const TbCode *code, int view, const char *needle, int after);
bool tb_code_address(TbCode *code, int view, const char *address_or_symbol);
bool tb_code_assembly_page(TbCode *code, int view, int direction);
void tb_code_mixed(TbCode *code, int view, bool mixed);
void tb_code_return(TbCode *code, int view); /* Follow selected stopped context. */
bool tb_code_stopped(TbCode *code); /* Inspect original stopped thread/frame zero. */
/* Disassembly-only navigation; neither action selects a thread/frame. */
bool tb_code_return_stop(TbCode *code, int view);
bool tb_code_follow_frame(TbCode *code, int view);
void tb_code_refresh(TbCode *code);
/* Settings apply through GDB, never as shell snippets. Ordered rules are
 * retained only after successful replies. Basic profile persistence is M13. */
bool tb_code_add_directory(TbCode *code, const char *path);
bool tb_code_add_rule(TbCode *code, const char *from, const char *to);
bool tb_code_clear_settings(TbCode *code);
/* Queue only for a fresh session. Directories are highest-priority first;
 * apply them in reverse because GDB prepends each directory. */
bool tb_code_restore_settings(TbCode *code, const char *const directories[], size_t count,
                              const TbSourceRule *rules, size_t rule_count);
bool tb_code_restoring(const TbCode *code);
bool tb_code_settings_busy(const TbCode *code);
size_t tb_code_directory_count(const TbCode *code);
const char *tb_code_directory(const TbCode *code, size_t index);
size_t tb_code_rule_count(const TbCode *code);
const TbSourceRule *tb_code_rule(const TbCode *code, size_t index);
/* Conservative address/symbol grammar and length-aware terminal escaping. */
bool tb_code_location(const char *text, uint64_t *address, bool *symbol);
void tb_code_safe(char *out, size_t capacity, const char *bytes, size_t length);

#endif
