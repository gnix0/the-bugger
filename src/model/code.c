#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "code.h"
#include "images.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum { CODE_NONE, CODE_FILES, CODE_ASSEMBLY, CODE_SYMBOL, CODE_FLAVOR,
               CODE_DIRECTORY, CODE_RULE, CODE_CLEAR_RULES, CODE_CLEAR_DIRS } CodeJob;
enum { CODE_COOKIE = 7000 };
typedef struct {
    TbCodeView data;
    bool visible_source, visible_assembly, source_dirty, assembly_dirty;
    char *source;
    size_t source_length, *offsets;
    uint64_t history[16], history_end[16]; size_t history_count;
    TbGdbGeneration generation;
    uint64_t version;
    bool have_anchor, fallback, origin_tried, precontext, function_known;
    uint64_t follow_pc, function_start;
    size_t prefix_limit;
    TbInstruction *forward;
    size_t forward_count;
    bool indexed, raw;
    bool merge_tail;
    uint64_t range_end, scan_target;
    TbInstruction *scan_prefix;
    size_t scan_count;
    unsigned scan_steps;
    char file_key[TB_CODE_PATH];
} View;
struct TbCode {
    TbGdb *gdb; TbContext *context;
    TbImages *images;
    pid_t pid;
    char executable[TB_CODE_PATH];
    View views[TB_CODE_VIEWS];
    TbCodeFile files[TB_CODE_FILES]; size_t file_count;
    bool files_dirty, files_limited, waiting, flavor_done, images_dirty;
    CodeJob job, settings_job;
    int active;
    uint64_t revision;
    uint64_t request_version;
    TbGdbGeneration flavor_generation;
    char selected_thread[32], selected_address[48];
    int selected_level;
    uint64_t stop;
    char message[192];
    char directories[TB_CODE_RULES][TB_CODE_PATH]; size_t directory_count;
    TbSourceRule rules[TB_CODE_RULES], pending_rule; size_t rule_count;
    char pending_directory[TB_CODE_PATH];
    char restore_directories[TB_CODE_RULES][TB_CODE_PATH]; size_t restore_dirs, restore_dir;
    TbSourceRule restore_rules[TB_CODE_RULES]; size_t restore_count, restore_rule;
    bool restoring;
};

void tb_code_safe(char *out, size_t capacity, const char *bytes, size_t length)
{
    if (!capacity) { return; }
    size_t used = 0;
    for (size_t i = 0; i < length && used + 1 < capacity; ++i) {
        unsigned char c = (unsigned char)bytes[i];
        if (c >= 32 && c < 127) { out[used++] = (char)c; }
        else if (c == '\t') {
            size_t spaces = 4 - used % 4;
            while (spaces-- && used + 1 < capacity) { out[used++] = ' '; }
        } else if (used + 4 < capacity) {
            static const char digits[] = "0123456789abcdef";
            out[used++] = '\\'; out[used++] = 'x'; out[used++] = digits[c / 16]; out[used++] = digits[c % 16];
        }
    }
    out[used] = '\0';
}
static bool raw(char *out, size_t capacity, const TbMiValue *value)
{
    out[0] = '\0';
    if (!value || value->type != TB_MI_STRING || value->string.length >= capacity ||
        memchr(value->string.data, '\0', value->string.length)) { return false; }
    memcpy(out, value->string.data, value->string.length); out[value->string.length] = '\0'; return true;
}
static void safe(char *out, size_t capacity, const TbMiValue *value)
{
    if (!value || value->type != TB_MI_STRING) { out[0] = '\0'; return; }
    tb_code_safe(out, capacity, value->string.data, value->string.length);
}
static int integer(const TbMiValue *value)
{
    char text[24]; if (!raw(text, sizeof text, value)) { return 0; }
    uint64_t n = 0;
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '9' || n > (INT32_MAX - (uint64_t)(text[i] - '0')) / 10) { return 0; }
        n = n * 10 + (unsigned)(text[i] - '0');
    }
    return (int)n;
}
static void message(TbCode *code, const char *text)
{
    (void)snprintf(code->message, sizeof code->message, "%s", text); ++code->revision;
}
bool tb_code_location(const char *text, uint64_t *address, bool *symbol)
{
    if (!text || !*text || strlen(text) > 128) { return false; }
    *symbol = false; *address = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        if (!text[2]) { return false; }
        for (size_t i = 2; text[i]; ++i) {
            unsigned char c = (unsigned char)text[i];
            unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
                             c >= 'a' && c <= 'f' ? (unsigned)(c - 'a') + 10 :
                             c >= 'A' && c <= 'F' ? (unsigned)(c - 'A') + 10 : 16;
            if (digit == 16 || *address > (UINT64_MAX - digit) / 16) { return false; }
            *address = *address * 16 + digit;
        }
        return true;
    }
    if (!((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z') || text[0] == '_')) { return false; }
    for (size_t i = 1; text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!(isalnum(c) || c == '_' || (c == ':' && text[i + 1] == ':') || (c == ':' && text[i - 1] == ':'))) { return false; }
    }
    *symbol = true; return true;
}
static bool address_value(const TbMiValue *value, uint64_t *address)
{
    char text[192]; bool symbol;
    if (!raw(text, sizeof text, value)) { return false; }
    char *space = strchr(text, ' '); if (space) { *space = '\0'; }
    return tb_code_location(text, address, &symbol) && !symbol;
}
static bool valid_view(int view) { return view >= 0 && view < TB_CODE_VIEWS; }
TbCode *tb_code_create(TbGdb *gdb, TbContext *context)
{
    TbCode *code = calloc(1, sizeof *code);
    if (code) {
        code->gdb = gdb; code->context = context; code->images = tb_images_create();
        if (!code->images) { free(code); return NULL; }
        tb_code_reset(code);
    }
    return code;
}
void tb_code_reset(TbCode *code)
{
    TbGdb *gdb = code->gdb; TbContext *context = code->context; uint64_t revision = code->revision;
    TbImages *images = code->images; tb_images_reset(images);
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { free(code->views[i].source); free(code->views[i].offsets); free(code->views[i].forward); free(code->views[i].scan_prefix); }
    memset(code, 0, sizeof *code); code->gdb = gdb; code->context = context;
    code->images = images; code->revision = revision + 1; code->files_dirty = true; code->selected_level = -1;
    for (int i = 0; i < TB_CODE_VIEWS; ++i) {
        code->views[i].data.source_follow = code->views[i].data.assembly_follow = true;
        code->views[i].data.stale = true;
        code->views[i].source_dirty = code->views[i].assembly_dirty = true;
    }
}
void tb_code_destroy(TbCode *code)
{
    if (!code) { return; }
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { free(code->views[i].source); free(code->views[i].offsets); free(code->views[i].forward); free(code->views[i].scan_prefix); }
    tb_images_destroy(code->images); free(code);
}
void tb_code_native(TbCode *code, pid_t pid, const char *executable)
{
    if (code->pid > 1 && pid != code->pid) { tb_images_reset(code->images); }
    code->pid = pid;
    (void)snprintf(code->executable, sizeof code->executable, "%s", executable ? executable : "");
}
const TbCodeView *tb_code_view(const TbCode *code, int view) { return valid_view(view) ? &code->views[view].data : NULL; }
const char *tb_code_assembly_notice(const TbCode *code, int id, size_t first, size_t count)
{
    if (!valid_view(id)) { return ""; }
    const TbCodeView *view = &code->views[id].data;
    bool unknown = false, data = false;
    for (size_t i = first; count && i < view->instruction_count; ++i, --count) {
        TbImageRange range;
        if (!tb_images_lookup(code->images, view->instructions[i].numeric, &range)) { unknown = true; }
        else if (range.kind == TB_IMAGE_DATA) { data = true; }
    }
    if (data) { return "Raw decoding of non-code data"; }
    if (unknown) { return "Code range unknown for visible instructions"; }
    return tb_images_limited(code->images) ? "Code catalog limited; explicit Go to remains available" : "";
}
uint64_t tb_code_revision(const TbCode *code) { return code->revision; }
const char *tb_code_message(const TbCode *code) { return code->message; }
size_t tb_code_file_count(const TbCode *code) { return code->file_count; }
const TbCodeFile *tb_code_file(const TbCode *code, size_t index) { return index < code->file_count ? &code->files[index] : NULL; }
bool tb_code_files_limited(const TbCode *code) { return code->files_limited; }
void tb_code_request_files(TbCode *code) { code->files_dirty = true; ++code->revision; }
void tb_code_visible(TbCode *code, int id, bool source, bool assembly)
{
    if (!valid_view(id)) { return; }
    View *view = &code->views[id];
    /* Visibility is not invalidation. Stops/settings already dirty hidden views;
     * reopening a menu/tab must preserve the cached instruction window. */
    view->visible_source = source; view->visible_assembly = assembly;
    bool required = false;
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { required = required || code->views[i].visible_source || code->views[i].visible_assembly; }
    tb_context_require(code->context, required);
}
static void source_load(View *view)
{
    free(view->source); free(view->offsets); view->source = NULL; view->offsets = NULL;
    view->source_length = view->data.line_count = 0; view->data.truncated = false;
    view->data.source_error[0] = '\0'; view->source_dirty = false; ++view->data.source_revision;
    if (!*view->data.path) {
        (void)snprintf(view->data.source_error, sizeof view->data.source_error, "No source mapping from GDB; use Files or assembly."); return;
    }
    if (view->data.path[0] != '/') {
        (void)snprintf(view->data.source_error, sizeof view->data.source_error, "Source path not resolved by GDB; use source lookup settings."); return;
    }
    int fd = open(view->data.path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat info;
    if (fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        if (fd >= 0) { (void)close(fd); }
        (void)snprintf(view->data.source_error, sizeof view->data.source_error, "Source unavailable: use Sources to add a directory or path mapping."); return;
    }
    view->source = malloc(TB_CODE_SOURCE_BYTES + 1);
    view->offsets = malloc((TB_CODE_LINES + 1) * sizeof *view->offsets);
    if (!view->source || !view->offsets) {
        (void)close(fd); free(view->source); free(view->offsets); view->source = NULL; view->offsets = NULL;
        (void)snprintf(view->data.source_error, sizeof view->data.source_error, "Cannot allocate bounded source buffer."); return;
    }
    bool okay = true;
    while (view->source_length < TB_CODE_SOURCE_BYTES) {
        ssize_t count = read(fd, view->source + view->source_length, TB_CODE_SOURCE_BYTES - view->source_length);
        if (count < 0 && errno == EINTR) { continue; }
        if (count < 0) { okay = false; break; }
        if (!count) { break; }
        view->source_length += (size_t)count;
    }
    (void)close(fd); view->source[view->source_length] = '\0';
    if (!okay) { (void)snprintf(view->data.source_error, sizeof view->data.source_error, "Source read failed; retained text may be partial."); }
    view->data.truncated = info.st_size > TB_CODE_SOURCE_BYTES;
    if (view->source_length) { view->offsets[view->data.line_count++] = 0; }
    for (size_t i = 0; i < view->source_length; ++i) {
        if (view->source[i] == '\n' && i + 1 < view->source_length) {
            if (view->data.line_count == TB_CODE_LINES) { view->data.truncated = true; view->source_length = i + 1; break; }
            view->offsets[view->data.line_count++] = i + 1;
        }
    }
    view->offsets[view->data.line_count] = view->source_length;
    if (!view->source_length && !*view->data.source_error) {
        (void)snprintf(view->data.source_error, sizeof view->data.source_error, "Source file is empty.");
    }
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): view index precedes the line within that view.
bool tb_code_source_line(const TbCode *code, int id, size_t line, char *out, size_t capacity)
{
    if (!valid_view(id) || !capacity) { return false; }
    const View *view = &code->views[id];
    out[0] = '\0';
    if (line >= view->data.line_count) { return false; }
    size_t start = view->offsets[line], end = view->offsets[line + 1];
    while (end > start && (view->source[end - 1] == '\n' || view->source[end - 1] == '\r')) { --end; }
    tb_code_safe(out, capacity, view->source + start, end - start); return true;
}
int tb_code_find(const TbCode *code, int id, const char *needle, int after)
{
    if (!valid_view(id) || !needle || !*needle) { return -1; }
    const View *view = &code->views[id]; size_t length = strlen(needle);
    for (size_t step = 0; step < view->data.line_count; ++step) {
        size_t row = ((size_t)(after < 0 ? 0 : after) + step) % view->data.line_count;
        size_t start = view->offsets[row], end = view->offsets[row + 1];
        for (size_t at = start; at + length <= end; ++at) {
            if (memcmp(view->source + at, needle, length) == 0) { return (int)row; }
        }
    }
    return -1;
}
bool tb_code_open(TbCode *code, int id, size_t file)
{
    if (!valid_view(id) || file >= code->file_count) { return false; }
    View *view = &code->views[id];
    const char *path = *code->files[file].full ? code->files[file].full : code->files[file].file;
    (void)snprintf(view->data.path, sizeof view->data.path, "%s", path);
    memmove(view->file_key, code->files[file].file, sizeof view->file_key);
    view->data.source_follow = false; view->source_dirty = true;
    source_load(view); ++code->revision;
    message(code, *view->data.source_error ? view->data.source_error : "Source opened; execution unchanged."); return true;
}
static void assembly_parse(View *view, const TbMiValue *list, const char *path, int line)
{
    if (!list || list->type != TB_MI_LIST) { return; }
    for (const TbMiValue *item = list->children; item; item = item->next) {
        if (view->data.instruction_count == TB_CODE_INSNS && !view->precontext) { view->data.more = true; return; }
        const TbMiValue *nested = tb_mi_find(item->children, "line_asm_insn");
        if (nested) {
            char source[512];
            const TbMiValue *file = tb_mi_find(item->children, "fullname");
            if (!file) { file = tb_mi_find(item->children, "file"); }
            (void)raw(source, sizeof source, file);
            assembly_parse(view, nested, source, integer(tb_mi_find(item->children, "line"))); continue;
        }
        uint64_t address;
        if (!address_value(tb_mi_find(item->children, "address"), &address)) { continue; }
        if (view->data.instruction_count && address <= view->data.instructions[view->data.instruction_count - 1].numeric) { continue; }
        if (view->precontext && address < view->follow_pc && view->data.instruction_count == view->prefix_limit) {
            memmove(view->data.instructions, view->data.instructions + 1, (view->prefix_limit - 1) * sizeof view->data.instructions[0]);
            --view->data.instruction_count;
        }
        if (view->data.instruction_count == TB_CODE_INSNS) { view->data.more = true; return; }
        if (!view->data.instruction_count) {
            int offset = integer(tb_mi_find(item->children, "offset"));
            if (tb_mi_find(item->children, "offset") && (uint64_t)offset <= address) {
                view->function_known = true; view->function_start = address - (uint64_t)offset;
            }
        }
        TbInstruction *instruction = &view->data.instructions[view->data.instruction_count++];
        memset(instruction, 0, sizeof *instruction); instruction->numeric = address;
        safe(instruction->address, sizeof instruction->address, tb_mi_find(item->children, "address"));
        safe(instruction->opcodes, sizeof instruction->opcodes, tb_mi_find(item->children, "opcodes"));
        safe(instruction->text, sizeof instruction->text, tb_mi_find(item->children, "inst"));
        (void)snprintf(instruction->source, sizeof instruction->source, "%s", path ? path : "");
        instruction->line = line;
        size_t hex = 0;
        for (size_t i = 0; instruction->opcodes[i]; ++i) { if (isxdigit((unsigned char)instruction->opcodes[i])) { ++hex; } }
        instruction->bytes = (unsigned)(hex / 2);
        view->data.next_address = address <= UINT64_MAX - instruction->bytes ? address + instruction->bytes : 0;
        if (line > 0 && strcmp(instruction->source, view->data.path) == 0 && (size_t)line <= view->data.line_count) { view->data.mixed_available = true; }
    }
}
static bool request(TbCode *code, CodeJob job, const char *operation, const char *const args[], size_t count, TbGdbScope scope)
{
    code->job = job;
    code->request_version = code->views[code->active].version;
    code->waiting = tb_gdb_request(code->gdb, operation, args, count, scope, CODE_COOKIE + (uint64_t)job, 3000, NULL);
    if (!code->waiting) { code->job = CODE_NONE; message(code, "Code request unavailable; retry while stopped."); }
    return code->waiting;
}
static void files_parse(TbCode *code, const TbMiRecord *record)
{
    code->file_count = 0; code->files_limited = false;
    const TbMiValue *list = tb_mi_find(record->results, "files");
    if (!list || list->type != TB_MI_LIST) { message(code, "GDB returned no source-file catalog."); return; }
    for (const TbMiValue *item = list->children; item; item = item->next) {
        if (code->file_count == TB_CODE_FILES) { code->files_limited = true; break; }
        TbCodeFile *file = &code->files[code->file_count];
        if (!raw(file->file, sizeof file->file, tb_mi_find(item->children, "file"))) { continue; }
        (void)raw(file->full, sizeof file->full, tb_mi_find(item->children, "fullname"));
        ++code->file_count;
    }
    for (int id = 0; id < TB_CODE_VIEWS; ++id) {
        View *view = &code->views[id];
        if (view->data.source_follow || !*view->file_key) { continue; }
        size_t found = 0, index = 0;
        for (size_t i = 0; i < code->file_count; ++i) {
            if (strcmp(code->files[i].file, view->file_key) == 0) { ++found; index = i; }
        }
        if (found == 1 && *code->files[index].full) {
            memmove(view->data.path, code->files[index].full, sizeof view->data.path);
            view->source_dirty = true;
        }
    }
    message(code, code->files_limited ? "Source catalog limited to first 256 GDB files." : "GDB source-file catalog refreshed.");
}
static void invalidate_sources(TbCode *code)
{
    code->files_dirty = true; tb_context_refresh(code->context);
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { code->views[i].source_dirty = code->views[i].assembly_dirty = true; }
}
static void cancel_precontext(View *view)
{
    if (view->precontext && view->data.instruction_count) { view->data.anchor = view->data.instructions[0].numeric; }
    view->precontext = false;
    free(view->forward); view->forward = NULL; view->forward_count = 0;
    free(view->scan_prefix); view->scan_prefix = NULL; view->scan_count = 0; view->scan_target = 0;
}

static void append_forward(View *view)
{
    if (!view->forward) { return; }
    if (view->indexed) {
        /* Merge monotonically addressed chunks, preserving overlap identity. */
        TbInstruction *merged = malloc((size_t)2 * TB_CODE_INSNS * sizeof *merged);
        if (!merged) {
            memcpy(view->data.instructions, view->forward, view->forward_count * sizeof *view->forward);
            view->data.instruction_count = view->forward_count;
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "Cannot extend code cache; previous listing retained.");
        } else {
            size_t a = 0, b = 0, count = 0;
            while (a < view->forward_count || b < view->data.instruction_count) {
                if (b == view->data.instruction_count || (a < view->forward_count && view->forward[a].numeric < view->data.instructions[b].numeric)) {
                    merged[count++] = view->forward[a++];
                } else {
                    if (a < view->forward_count && view->forward[a].numeric == view->data.instructions[b].numeric) { ++a; }
                    merged[count++] = view->data.instructions[b++];
                }
            }
            size_t keep = count < TB_CODE_INSNS ? count : TB_CODE_INSNS;
            size_t start = view->merge_tail && count > keep ? count - keep : 0;
            memcpy(view->data.instructions, merged + start, keep * sizeof *merged); view->data.instruction_count = keep;
            free(merged);
        }
        view->data.anchor = view->data.instructions[0].numeric;
        const TbInstruction *last = &view->data.instructions[view->data.instruction_count - 1];
        view->data.next_address = last->numeric <= UINT64_MAX - last->bytes ? last->numeric + last->bytes : 0;
        free(view->forward); view->forward = NULL; view->forward_count = 0; return;
    }
    size_t room = TB_CODE_INSNS - view->data.instruction_count;
    size_t count = view->forward_count < room ? view->forward_count : room;
    memcpy(view->data.instructions + view->data.instruction_count, view->forward, count * sizeof view->forward[0]);
    view->data.instruction_count += count;
    view->data.more = view->data.more || count < view->forward_count;
    if (view->data.instruction_count) {
        const TbInstruction *last = &view->data.instructions[view->data.instruction_count - 1];
        view->data.next_address = last->numeric <= UINT64_MAX - last->bytes ? last->numeric + last->bytes : 0;
    }
    for (size_t i = 0; i < view->data.instruction_count; ++i) {
        const TbInstruction *instruction = &view->data.instructions[i];
        if (instruction->line > 0 && strcmp(instruction->source, view->data.path) == 0 &&
            (size_t)instruction->line <= view->data.line_count) { view->data.mixed_available = true; }
    }
    free(view->forward); view->forward = NULL; view->forward_count = 0;
}
bool tb_code_event(TbCode *code, const TbGdbEvent *event)
{
    if (event->kind == TB_GDB_RECORD && event->record && event->record->kind == TB_MI_NOTIFY &&
        (strcmp(event->record->klass, "library-loaded") == 0 || strcmp(event->record->klass, "library-unloaded") == 0 ||
         strcmp(event->record->klass, "thread-group-started") == 0)) { code->images_dirty = true; }
    if (event->cookie <= CODE_COOKIE || event->cookie > CODE_COOKIE + CODE_CLEAR_DIRS ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    code->waiting = false;
    CodeJob job = (CodeJob)(event->cookie - CODE_COOKIE); code->job = CODE_NONE;
    View *view = &code->views[code->active];
    if ((job == CODE_ASSEMBLY || job == CODE_SYMBOL) && code->request_version != view->version) {
        cancel_precontext(view);
        view->assembly_dirty = true; view->data.stale = true; ++code->revision; return true;
    }
    if (event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED) {
        if (job >= CODE_DIRECTORY) { code->restoring = false; message(code, "Source profile application cancelled; review source settings."); }
        cancel_precontext(view);
        if (job == CODE_FILES) { code->files_dirty = true; }
        if (job == CODE_FLAVOR) { code->flavor_done = false; }
        view->assembly_dirty = true; view->data.stale = true; ++code->revision; return true;
    }
    bool okay = event->kind == TB_GDB_REPLY && strcmp(event->record->klass, "done") == 0;
    if (!okay) {
        if (job >= CODE_DIRECTORY) { code->restoring = false; }
        char error[192]; safe(error, sizeof error, event->record ? tb_mi_find(event->record->results, "msg") : NULL);
        if (!*error) { (void)snprintf(error, sizeof error, "GDB code request timed out or failed."); }
        if (job == CODE_ASSEMBLY && view->data.mixed && !view->fallback) {
            view->fallback = true; view->assembly_dirty = true;
        } else if (job == CODE_ASSEMBLY || job == CODE_SYMBOL) {
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "%s", error);
            view->data.instruction_count = 0; ++view->data.assembly_revision;
            if (job == CODE_ASSEMBLY && view->forward) {
                append_forward(view);
                if (view->data.instruction_count) { view->data.anchor = view->data.instructions[0].numeric; }
                view->precontext = false;
            }
            if (job == CODE_ASSEMBLY && view->data.instruction_count) { view->data.anchor = view->data.instructions[0].numeric; }
        }
        message(code, error); return true;
    }
    if (job == CODE_FILES) { files_parse(code, event->record); }
    else if (job == CODE_ASSEMBLY) {
        view->data.instruction_count = 0; view->data.mixed_available = view->data.more = false;
        view->function_known = false;
        view->data.assembly_error[0] = '\0';
        const TbMiValue *list = tb_mi_find(event->record->results, "asm_insns");
        assembly_parse(view, list, NULL, 0);
        if (view->scan_target && !view->data.instruction_count) {
            memcpy(view->data.instructions, view->forward, view->forward_count * sizeof *view->forward);
            view->data.instruction_count = view->forward_count; cancel_precontext(view);
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "No earlier instructions available; listing retained.");
            ++view->data.assembly_revision; ++code->revision; return true;
        }
        if (view->scan_target && view->data.instruction_count) {
            uint64_t next = view->data.next_address;
            if (view->indexed && !view->raw && next < view->scan_target) {
                TbImageRange range;
                if (!tb_images_lookup(code->images, next, &range) || range.kind != TB_IMAGE_CODE) {
                    if (tb_images_neighbor(code->images, next, 1, &range) && range.anchor <= view->scan_target) {
                        next = range.anchor;
                    } else { next = 0; }
                }
            }
            if (next < view->scan_target && next > view->data.anchor && ++view->scan_steps < 4096) {
                size_t keep = view->data.instruction_count < 64 ? view->data.instruction_count : 64;
                memcpy(view->scan_prefix, view->data.instructions + view->data.instruction_count - keep, keep * sizeof *view->scan_prefix);
                view->scan_count = keep;
                memcpy(view->data.instructions, view->forward, view->forward_count * sizeof *view->forward);
                view->data.instruction_count = view->forward_count;
                view->data.anchor = next; view->assembly_dirty = true; view->data.stale = true;
                ++code->revision; return true;
            }
            if (next < view->scan_target) {
                memcpy(view->data.instructions, view->forward, view->forward_count * sizeof *view->forward);
                view->data.instruction_count = view->forward_count;
                cancel_precontext(view);
                (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "Earlier boundary scan reached its work limit; retained listing unchanged.");
                ++view->data.assembly_revision; ++code->revision; return true;
            }
            size_t keep = view->data.instruction_count < 64 ? 64 - view->data.instruction_count : 0;
            if (keep > view->scan_count) { keep = view->scan_count; }
            memmove(view->data.instructions + keep, view->data.instructions, view->data.instruction_count * sizeof view->data.instructions[0]);
            memcpy(view->data.instructions, view->scan_prefix + view->scan_count - keep, keep * sizeof view->data.instructions[0]);
            view->data.instruction_count += keep;
            free(view->scan_prefix); view->scan_prefix = NULL; view->scan_target = 0; view->scan_count = 0;
        }
        append_forward(view);
        if (view->data.assembly_follow && !view->origin_tried && code->pid <= 1) {
            view->origin_tried = true;
            if (view->function_known && view->function_start < view->data.anchor &&
                view->function_start <= view->follow_pc && view->follow_pc - view->function_start <= 1024) {
                view->data.anchor = view->function_start; view->precontext = true; view->prefix_limit = view->indexed ? 64 : 4;
                view->assembly_dirty = true; view->data.stale = true; ++code->revision; return true;
            }
        }
        if (view->precontext && view->data.instruction_count) { view->data.anchor = view->data.instructions[0].numeric; }
        view->precontext = false;
        if (!view->data.instruction_count) { (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "No instructions returned by GDB."); }
        ++view->data.assembly_revision;
        if (!view->data.assembly_follow && view->data.instruction_count) {
            message(code, "Instruction window loaded; execution unchanged.");
        }
    } else if (job == CODE_SYMBOL) {
        uint64_t address;
        if (address_value(tb_mi_find(event->record->results, "value"), &address)) {
            view->data.anchor = address; view->have_anchor = true; view->assembly_dirty = true;
        } else { (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "GDB did not resolve a numeric symbol address."); }
    } else if (job == CODE_FLAVOR) { code->flavor_done = true; }
    else if (job >= CODE_DIRECTORY) {
        /* Defer context-generation changes and further requests to the pump. */
        code->settings_job = job;
    }
    ++code->revision; return true;
}
static bool generation_equal(TbGdbGeneration a, TbGdbGeneration b)
{
    return a.session == b.session && a.stop == b.stop && a.context == b.context;
}

static void remember_window(View *view)
{
    if (!view->data.instruction_count || view->data.next_address <= view->data.anchor) { return; }
    if (view->history_count && view->history[view->history_count - 1] == view->data.anchor) { return; }
    if (view->history_count == 16) {
        memmove(view->history, view->history + 1, 15 * sizeof view->history[0]);
        memmove(view->history_end, view->history_end + 1, 15 * sizeof view->history_end[0]);
        --view->history_count;
    }
    view->history[view->history_count] = view->data.anchor;
    view->history_end[view->history_count++] = view->data.next_address;
}

static void follow_address(View *view, uint64_t address)
{
    view->follow_pc = address;
    if (view->precontext) { return; }
    for (size_t i = 0; i < view->data.instruction_count; ++i) {
        if (view->data.instructions[i].numeric == address) { return; }
    }
    uint64_t anchor = address;
    for (size_t i = view->history_count; i > 0; --i) {
        if (address >= view->history[i - 1] && address < view->history_end[i - 1]) {
            anchor = view->history[i - 1]; break;
        }
    }
    if (view->have_anchor && anchor != view->data.anchor) { remember_window(view); }
    view->data.anchor = anchor; view->origin_tried = anchor != address;
}
void tb_code_pump(TbCode *code)
{
    if (code->settings_job) {
        CodeJob job = code->settings_job; code->settings_job = CODE_NONE;
        if (job == CODE_DIRECTORY) {
            for (size_t i = 0; i < code->directory_count; ++i) {
                if (strcmp(code->directories[i], code->pending_directory) == 0) {
                    memmove(code->directories[i], code->directories[i + 1], (code->directory_count - i - 1) * sizeof code->directories[0]); --code->directory_count; break;
                }
            }
            if (code->directory_count < TB_CODE_RULES) {
                memmove(code->directories[1], code->directories[0], code->directory_count * sizeof code->directories[0]);
                (void)snprintf(code->directories[0], sizeof code->directories[0], "%s", code->pending_directory); ++code->directory_count;
            }
        } else if (job == CODE_RULE) {
            for (size_t i = 0; i < code->rule_count; ++i) {
                if (strcmp(code->rules[i].from, code->pending_rule.from) == 0) {
                    memmove(&code->rules[i], &code->rules[i + 1], (code->rule_count - i - 1) * sizeof code->rules[0]); --code->rule_count; break;
                }
            }
            if (code->rule_count < TB_CODE_RULES) { code->rules[code->rule_count++] = code->pending_rule; }
        } else if (job == CODE_CLEAR_RULES) {
            code->rule_count = 0;
            const char *const args[] = {"-r"};
            (void)request(code, CODE_CLEAR_DIRS, "-environment-directory", args, 1, TB_GDB_SESSION); return;
        } else if (job == CODE_CLEAR_DIRS) { code->directory_count = 0; }
        invalidate_sources(code); message(code, "GDB source settings applied; refreshing context.");
    }
    const TbContextSnapshot *context = tb_context_snapshot(code->context);
    if (code->restoring && !code->waiting) {
        TbGdbState state = tb_gdb_state(code->gdb);
        if (state != TB_GDB_READY && state != TB_GDB_STOPPED) { return; }
        if (code->restore_dir < code->restore_dirs) {
            size_t index = code->restore_dirs - 1 - code->restore_dir++;
            if (!tb_code_add_directory(code, code->restore_directories[index])) { code->restoring = false; }
            return;
        }
        if (code->restore_rule < code->restore_count) {
            const TbSourceRule *rule = &code->restore_rules[code->restore_rule++];
            if (!tb_code_add_rule(code, rule->from, rule->to)) { code->restoring = false; }
            return;
        }
        code->restoring = false;
        message(code, "Profile source settings applied.");
    }
    bool stopped = tb_gdb_state(code->gdb) == TB_GDB_STOPPED;
    bool required = false;
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { required = required || code->views[i].visible_source || code->views[i].visible_assembly; }
    tb_context_require(code->context, required);
    for (int i = 0; i < TB_CODE_VIEWS; ++i) {
        View *view = &code->views[i];
        bool stale = !stopped || context->stale || !context->ready ||
                     !generation_equal(view->generation, context->generation) || view->source_dirty ||
                     (view->visible_assembly && view->assembly_dirty) ||
                     (code->waiting && code->active == i && (code->job == CODE_ASSEMBLY || code->job == CODE_SYMBOL));
        if (view->data.stale != stale) { view->data.stale = stale; ++code->revision; }
    }
    if (code->waiting || !required) { return; }
    if (!stopped) {
        if (tb_gdb_state(code->gdb) == TB_GDB_READY && code->files_dirty) {
            code->files_dirty = false;
            (void)request(code, CODE_FILES, "-file-list-exec-source-files", NULL, 0, TB_GDB_SESSION);
        }
        return;
    }
    if (context->stale || !context->ready) { return; }
    if (code->images_dirty) { tb_images_reset(code->images); code->images_dirty = false; }
    bool changed = code->stop != context->generation.stop || code->selected_level != context->selected.level ||
                   strcmp(code->selected_thread, context->selected_thread) != 0 || strcmp(code->selected_address, context->selected.address) != 0;
    code->stop = context->generation.stop; code->selected_level = context->selected.level;
    if (changed && code->pid > 1) {
        bool symbol; uint64_t pc;
        if (tb_code_location(context->selected.address, &pc, &symbol) && !symbol) { tb_images_native(code->images, code->pid, code->executable, pc); }
    }
    if (!generation_equal(code->flavor_generation, context->generation)) {
        code->flavor_done = false; code->flavor_generation = context->generation;
    }
    (void)snprintf(code->selected_thread, sizeof code->selected_thread, "%s", context->selected_thread);
    (void)snprintf(code->selected_address, sizeof code->selected_address, "%s", context->selected.address);
    for (int i = 0; i < TB_CODE_VIEWS; ++i) {
        View *view = &code->views[i];
        if (changed) { cancel_precontext(view); view->raw = false; view->data.source_follow = view->data.assembly_follow = true; }
        if (changed || !generation_equal(view->generation, context->generation)) {
            view->generation = context->generation; view->source_dirty = view->assembly_dirty = true;
        }
        if (view->data.source_follow && strcmp(view->data.path, context->selected.path) != 0) {
            (void)snprintf(view->data.path, sizeof view->data.path, "%s", context->selected.path); view->source_dirty = true;
        }
        view->data.selected_line = strcmp(view->data.path, context->selected.path) == 0 ? context->selected.line : 0;
        view->data.stopped_line = strcmp(view->data.path, context->stopped.path) == 0 ? context->stopped.line : 0;
        if (view->data.assembly_follow) {
            bool symbol; uint64_t address;
            view->have_anchor = tb_code_location(context->selected.address, &address, &symbol) && !symbol;
            if (view->have_anchor) { follow_address(view, address); }
        }
    }
    if (!code->flavor_done && strstr(context->selected.architecture, "i386")) {
        code->flavor_done = true; /* Do not retry endlessly on unsupported setting. */
        const char *const args[] = {"disassembly-flavor", "att"};
        (void)request(code, CODE_FLAVOR, "-gdb-set", args, 2, TB_GDB_CONTEXT); return;
    }
    if (code->files_dirty) {
        code->files_dirty = false;
        (void)request(code, CODE_FILES, "-file-list-exec-source-files", NULL, 0, TB_GDB_CONTEXT); return;
    }
    for (int i = 0; i < TB_CODE_VIEWS; ++i) {
        View *view = &code->views[i];
        if (!view->visible_source && !view->visible_assembly) { continue; }
        if (view->source_dirty) { source_load(view); ++code->revision; }
        if (!view->visible_assembly || !view->assembly_dirty) { continue; }
        view->assembly_dirty = false; code->active = i;
        if (!view->have_anchor || view->data.anchor > UINT64_MAX - 256) {
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "No valid bounded instruction address; use Go to.");
            view->data.instruction_count = 0; ++view->data.assembly_revision;
            message(code, view->data.assembly_error); continue;
        }
        TbImageRange range;
        if (!tb_images_lookup(code->images, view->data.anchor, &range) && code->pid > 1) {
            tb_images_native(code->images, code->pid, code->executable, view->data.anchor);
        }
        if (tb_images_lookup(code->images, view->data.anchor, &range)) {
            view->indexed = true; view->range_end = range.end;
            if (range.kind == TB_IMAGE_DATA && view->scan_target) {
                TbImageRange next;
                if (tb_images_neighbor(code->images, range.end, 1, &next) && next.anchor < view->scan_target) {
                    view->data.anchor = next.anchor; view->assembly_dirty = true; continue;
                }
                cancel_precontext(view);
                (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "Earlier code boundary unavailable; listing retained.");
                ++view->data.assembly_revision; continue;
            }
            if (range.kind == TB_IMAGE_DATA && !view->raw && !view->data.assembly_follow && !view->scan_target) {
                view->data.instruction_count = 0;
                (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error,
                               "Non-code data at 0x%" PRIx64 "; Actions > Decode bytes to inspect explicitly.", view->data.anchor);
                ++view->data.assembly_revision; continue;
            }
            if (range.kind == TB_IMAGE_CODE && view->data.assembly_follow && !view->precontext &&
                view->data.anchor == view->follow_pc && range.anchor < view->data.anchor && view->data.anchor - range.anchor <= 1024) {
                view->precontext = true; view->prefix_limit = 64; view->data.anchor = range.anchor; view->origin_tried = true;
            }
        } else {
            view->range_end = UINT64_MAX;
        }
        char low[48], high[48];
        (void)snprintf(low, sizeof low, "0x%" PRIx64, view->data.anchor);
        uint64_t padding = view->data.assembly_follow ? 256 : 0;
        uint64_t end = view->precontext ? view->follow_pc > UINT64_MAX - padding ? UINT64_MAX : view->follow_pc + padding : view->data.anchor + 256;
        if (view->scan_target && end > view->data.anchor + 256) { end = view->data.anchor + 256; }
        if (end > view->range_end) { end = view->range_end; }
        (void)snprintf(high, sizeof high, "0x%" PRIx64, end);
        const char *const args[] = {"-s", low, "-e", high, "--", view->data.mixed && !view->fallback ? "5" : "2"};
        (void)request(code, CODE_ASSEMBLY, "-data-disassemble", args, sizeof args / sizeof args[0], TB_GDB_CONTEXT); return;
    }
}
void tb_code_refresh(TbCode *code)
{
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { code->views[i].fallback = false; }
    code->flavor_done = false; invalidate_sources(code);
}
void tb_code_return(TbCode *code, int id)
{
    if (!valid_view(id)) { return; }
    View *view = &code->views[id]; cancel_precontext(view);
    view->raw = false;
    view->data.source_follow = view->data.assembly_follow = true;
    view->source_dirty = view->assembly_dirty = true; view->data.stale = true;
    ++view->version; ++code->revision;
}
bool tb_code_stopped(TbCode *code)
{
    const TbContextSnapshot *context = tb_context_snapshot(code->context);
    if (!*context->stopped_thread || !tb_context_select_thread(code->context, context->stopped_thread)) {
        message(code, "Original stopped thread is unavailable; pause and refresh context."); return false;
    }
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { tb_code_return(code, i); }
    message(code, "Returning to stopped context; execution unchanged."); return true;
}
void tb_code_mixed(TbCode *code, int id, bool mixed)
{
    if (!valid_view(id)) { return; }
    cancel_precontext(&code->views[id]);
    code->views[id].data.mixed = mixed; code->views[id].assembly_dirty = true;
    code->views[id].data.stale = true; ++code->views[id].version;
    code->views[id].fallback = false; ++code->revision;
}
bool tb_code_address(TbCode *code, int id, const char *location)
{
    uint64_t address; bool symbol;
    if (!valid_view(id) || code->waiting || code->settings_job != CODE_NONE || tb_gdb_state(code->gdb) != TB_GDB_STOPPED ||
        !tb_code_location(location, &address, &symbol)) {
        message(code, "Use a hexadecimal address or simple symbol (including ::); no expressions/calls."); return false;
    }
    if (!symbol && address > UINT64_MAX - 256) { message(code, "Instruction window would overflow the target address range."); return false; }
    View *view = &code->views[id]; cancel_precontext(view); view->data.assembly_follow = false;
    ++view->version; view->data.stale = true;
    view->history_count = 0; view->data.assembly_error[0] = '\0';
    if (symbol) {
        char expression[160]; (void)snprintf(expression, sizeof expression, "&'%s'", location);
        const char *const args[] = {"--language", "c", expression}; code->active = id;
        /* C address-of semantics avoid C++ overloaded operator&/inferior calls. */
        return request(code, CODE_SYMBOL, "-data-evaluate-expression", args, 3, TB_GDB_CONTEXT);
    }
    view->data.anchor = address; view->have_anchor = true; view->assembly_dirty = true; ++code->revision; return true;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): view index precedes signed paging direction.
bool tb_code_assembly_page(TbCode *code, int id, int direction)
{
    if (!valid_view(id) || code->waiting || code->settings_job != CODE_NONE || code->views[id].data.stale || !code->views[id].data.instruction_count) { return false; }
    View *view = &code->views[id];
    if (view->indexed) {
        TbImageRange range; uint64_t anchor;
        if (direction < 0) {
            if (!tb_images_neighbor(code->images, view->data.anchor, -1, &range)) { message(code, "Beginning of known code; use Go to for another address."); return false; }
            anchor = range.anchor;
        } else {
            anchor = view->data.next_address;
            if (!anchor || !tb_images_lookup(code->images, anchor, &range) || range.kind != TB_IMAGE_CODE) {
                if (!tb_images_neighbor(code->images, anchor, 1, &range)) { message(code, "End of known code; use Go to or Decode bytes for another range."); return false; }
                anchor = range.anchor;
                message(code, "Skipped non-code/unavailable address gap; loading next code region.");
            }
        }
        TbInstruction *saved = malloc(view->data.instruction_count * sizeof *saved);
        if (!saved) { message(code, "Cannot extend instruction cache; listing unchanged."); return false; }
        memcpy(saved, view->data.instructions, view->data.instruction_count * sizeof *saved);
        view->forward = saved; view->forward_count = view->data.instruction_count; view->merge_tail = direction > 0;
        view->follow_pc = view->data.anchor; view->data.anchor = anchor;
        if (direction < 0) {
            view->scan_prefix = malloc(64 * sizeof *view->scan_prefix);
            if (!view->scan_prefix) { cancel_precontext(view); return false; }
            view->scan_target = view->follow_pc; view->scan_count = 0; view->scan_steps = 0;
        }
        view->precontext = direction < 0; view->prefix_limit = 64;
        view->data.assembly_follow = false; view->assembly_dirty = true; view->data.stale = true; ++code->revision; return true;
    }
    if (direction < 0) {
        if (view->history_count) { view->data.anchor = view->history[--view->history_count]; }
        else if (view->function_known && view->function_start < view->data.anchor && view->data.anchor - view->function_start <= 1024) {
            TbInstruction *forward = malloc(view->data.instruction_count * sizeof *forward);
            if (!forward) { message(code, "Cannot preserve the instruction window; browsing unchanged."); return false; }
            memcpy(forward, view->data.instructions, view->data.instruction_count * sizeof *forward);
            view->forward = forward; view->forward_count = view->data.instruction_count;
            view->follow_pc = view->data.anchor; view->data.anchor = view->function_start;
            view->precontext = true; view->prefix_limit = TB_CODE_INSNS;
        } else { return false; }
    } else {
        if (!view->data.next_address || view->data.next_address <= view->data.anchor) { return false; }
        remember_window(view); view->data.anchor = view->data.next_address;
    }
    view->data.assembly_follow = false; view->assembly_dirty = true; view->data.stale = true; ++code->revision; return true;
}
void tb_code_raw(TbCode *code, int id)
{
    if (!valid_view(id) || code->waiting || tb_gdb_state(code->gdb) != TB_GDB_STOPPED) { return; }
    code->views[id].raw = !code->views[id].raw;
    code->views[id].assembly_dirty = true; code->views[id].data.stale = true; ++code->revision;
}
static bool path_valid(const char *path)
{
    return path && *path && strlen(path) < TB_CODE_PATH && !strchr(path, '\n') && !strchr(path, '\r');
}
bool tb_code_add_directory(TbCode *code, const char *path)
{
    bool existing = false;
    if (path) { for (size_t i = 0; i < code->directory_count; ++i) { existing = existing || strcmp(path, code->directories[i]) == 0; } }
    if (code->waiting || code->settings_job != CODE_NONE || !path_valid(path) || (code->directory_count == TB_CODE_RULES && !existing)) { message(code, "Wait for GDB; use a directory path (at most eight overrides)."); return false; }
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) { message(code, "Source directory does not exist or is inaccessible."); return false; }
    (void)snprintf(code->pending_directory, sizeof code->pending_directory, "%s", path);
    const char *const args[] = {path}; return request(code, CODE_DIRECTORY, "-environment-directory", args, 1, TB_GDB_SESSION);
}
bool tb_code_restoring(const TbCode *code) { return code->restoring; }
bool tb_code_restore_settings(TbCode *code, const char *const dirs[], size_t count,
                              const TbSourceRule *rules, size_t rule_count)
{
    if (code->waiting || code->restoring || code->directory_count || code->rule_count ||
        count > TB_CODE_RULES || rule_count > TB_CODE_RULES) { return false; }
    for (size_t i = 0; i < count; ++i) { if (!path_valid(dirs[i])) { return false; } }
    for (size_t i = 0; i < rule_count; ++i) { if (!path_valid(rules[i].from) || !path_valid(rules[i].to)) { return false; } }
    for (size_t i = 0; i < count; ++i) { (void)snprintf(code->restore_directories[i], TB_CODE_PATH, "%s", dirs[i]); }
    if (rule_count) { memcpy(code->restore_rules, rules, rule_count * sizeof *rules); }
    code->restore_dirs = count; code->restore_count = rule_count;
    code->restore_dir = code->restore_rule = 0; code->restoring = count || rule_count; return true;
}
/* Quote for GDB CLI's argument parser, independently of MI C-string encoding. */
static bool cli_quote(char *out, size_t capacity, const char *text)
{
    size_t at = 0; out[at++] = '"';
    for (size_t i = 0; text[i]; ++i) {
        if (at + 3 >= capacity) { return false; }
        if (text[i] == '"' || text[i] == '\\') { out[at++] = '\\'; }
        out[at++] = text[i];
    }
    out[at++] = '"'; out[at] = '\0'; return true;
}
bool tb_code_add_rule(TbCode *code, const char *from, const char *to)
{
    bool existing = false;
    if (from) { for (size_t i = 0; i < code->rule_count; ++i) { existing = existing || strcmp(from, code->rules[i].from) == 0; } }
    if (code->waiting || code->settings_job != CODE_NONE || !path_valid(from) || !path_valid(to) || (code->rule_count == TB_CODE_RULES && !existing)) { message(code, "Wait for GDB; provide both prefixes (at most eight mappings)."); return false; }
    char quoted_from[TB_CODE_PATH * 2 + 3], quoted_to[TB_CODE_PATH * 2 + 3], command[TB_CODE_PATH * 4 + 32];
    if (!cli_quote(quoted_from, sizeof quoted_from, from) || !cli_quote(quoted_to, sizeof quoted_to, to)) { return false; }
    (void)snprintf(command, sizeof command, "set substitute-path %s %s", quoted_from, quoted_to);
    (void)snprintf(code->pending_rule.from, sizeof code->pending_rule.from, "%s", from);
    (void)snprintf(code->pending_rule.to, sizeof code->pending_rule.to, "%s", to);
    const char *const args[] = {"console", command}; return request(code, CODE_RULE, "-interpreter-exec", args, 2, TB_GDB_SESSION);
}
bool tb_code_clear_settings(TbCode *code)
{
    if (code->waiting || code->settings_job != CODE_NONE) { return false; }
    const char *const args[] = {"console", "unset substitute-path"};
    return request(code, CODE_CLEAR_RULES, "-interpreter-exec", args, 2, TB_GDB_SESSION);
}
size_t tb_code_directory_count(const TbCode *code) { return code->directory_count; }
const char *tb_code_directory(const TbCode *code, size_t index) { return index < code->directory_count ? code->directories[index] : ""; }
size_t tb_code_rule_count(const TbCode *code) { return code->rule_count; }
const TbSourceRule *tb_code_rule(const TbCode *code, size_t index) { return index < code->rule_count ? &code->rules[index] : NULL; }
