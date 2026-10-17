#ifndef THE_BUGGER_CODE_H
#define THE_BUGGER_CODE_H

#include "context.h"

enum { TB_CODE_VIEWS = 32, TB_CODE_FILES = 256, TB_CODE_INSNS = 128,
       TB_CODE_SOURCE_BYTES = 1024 * 1024, TB_CODE_LINES = 32768,
       TB_CODE_PATH = 4096, TB_CODE_RULES = 8 };
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
    int selected_line, stopped_line;
    size_t line_count, instruction_count;
    TbInstruction instructions[TB_CODE_INSNS];
    uint64_t anchor, next_address, source_revision, assembly_revision;
} TbCodeView;
typedef struct TbCode TbCode;

TbCode *tb_code_create(TbGdb *gdb, TbContext *context);
void tb_code_destroy(TbCode *code);
void tb_code_reset(TbCode *code);
bool tb_code_event(TbCode *code, const TbGdbEvent *event);
void tb_code_pump(TbCode *code);
void tb_code_visible(TbCode *code, int view, bool source, bool assembly);
const TbCodeView *tb_code_view(const TbCode *code, int view);
uint64_t tb_code_revision(const TbCode *code);
const char *tb_code_message(const TbCode *code);
/* Source navigation is read-only and restricted to GDB-known files. */
size_t tb_code_file_count(const TbCode *code);
const TbCodeFile *tb_code_file(const TbCode *code, size_t index);
bool tb_code_files_limited(const TbCode *code);
void tb_code_request_files(TbCode *code);
bool tb_code_open(TbCode *code, int view, size_t file);
bool tb_code_source_line(const TbCode *code, int view, size_t line, char *out, size_t capacity);
int tb_code_find(const TbCode *code, int view, const char *needle, int after);
bool tb_code_address(TbCode *code, int view, const char *address_or_symbol);
bool tb_code_assembly_page(TbCode *code, int view, int direction);
void tb_code_mixed(TbCode *code, int view, bool mixed);
void tb_code_return(TbCode *code, int view); /* Follow selected stopped context. */
bool tb_code_stopped(TbCode *code); /* Inspect original stopped thread/frame zero. */
void tb_code_refresh(TbCode *code);
/* Settings apply through GDB, never as shell snippets. Ordered rules are
 * retained only after successful replies. Basic profile persistence is M13. */
bool tb_code_add_directory(TbCode *code, const char *path);
bool tb_code_add_rule(TbCode *code, const char *from, const char *to);
bool tb_code_clear_settings(TbCode *code);
size_t tb_code_directory_count(const TbCode *code);
const char *tb_code_directory(const TbCode *code, size_t index);
size_t tb_code_rule_count(const TbCode *code);
const TbSourceRule *tb_code_rule(const TbCode *code, size_t index);
/* Conservative address/symbol grammar and length-aware terminal escaping. */
bool tb_code_location(const char *text, uint64_t *address, bool *symbol);
void tb_code_safe(char *out, size_t capacity, const char *bytes, size_t length);

#endif
