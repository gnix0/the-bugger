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
enum { CODE_COOKIE = 7000, CODE_ANCHORS = 512 };
typedef struct { uint64_t address, epoch; TbGdbGeneration generation; } CodeAnchor;
typedef struct { size_t count, next; CodeAnchor entries[CODE_ANCHORS]; } AnchorHistory;
typedef struct {
    TbGdbGeneration generation;
    uint64_t epoch, next_address, gap_begin, gap_end, function_start, range_end, follow_pc;
    bool more, mixed_available, truncated, function_known, indexed;
} ListingOrigin;
typedef struct {
    TbCodeView data;
    bool visible_source, visible_assembly, source_dirty, assembly_dirty, assembly_refresh;
    char *source;
    size_t source_length, *offsets;
    TbGdbGeneration generation;
    uint64_t version;
    bool have_anchor, fallback, origin_tried, precontext, function_known;
    uint64_t follow_pc, function_start;
    size_t prefix_limit;
    TbInstruction *forward;
    size_t forward_count;
    ListingOrigin forward_origin;
    bool indexed, raw;
    bool merge_tail;
    uint64_t range_end, scan_target;
    TbInstruction *scan_prefix;
    bool mixed_checked[TB_CODE_INSNS];
    size_t scan_count;
    size_t scan_rows;
    char file_key[TB_CODE_PATH];
    TbCodeFile tabs[TB_CODE_TABS - 1];
    size_t tab_count, tab_active;
    bool restored[TB_CODE_TABS - 1], unavailable[TB_CODE_TABS - 1];
    char resolved[TB_CODE_TABS - 1][TB_CODE_PATH];
} View;
struct TbCode {
    TbGdb *gdb; TbContext *context;
    TbBrowseCache *cache;
    TbImages *images;
    pid_t pid;
    char executable[TB_CODE_PATH];
    View views[TB_CODE_VIEWS];
    TbCodeFile files[TB_CODE_FILES]; size_t file_count;
    bool files_dirty, files_limited, waiting, flavor_done, images_dirty, files_requested;
    CodeJob job, settings_job;
    int active;
    uint64_t revision;
    uint64_t request_version, epoch, request_epoch, image_revision;
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
bool tb_code_settings_busy(const TbCode *code)
{
    return code->restoring || code->settings_job != CODE_NONE ||
        (code->waiting && code->job >= CODE_DIRECTORY && code->job <= CODE_CLEAR_DIRS);
}
static void listing_store(TbCode *code, int id);

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
        code->gdb = gdb; code->context = context; code->images = tb_images_create(); code->cache = tb_browse_create();
        if (!code->images || !code->cache) { tb_images_destroy(code->images); tb_browse_release(code->cache); free(code); return NULL; }
        tb_code_reset(code);
    }
    return code;
}
void tb_code_reset(TbCode *code)
{
    TbGdb *gdb = code->gdb; TbContext *context = code->context; uint64_t revision = code->revision;
    TbImages *images = code->images; tb_images_reset(images);
    TbBrowseCache *cache = code->cache; tb_browse_clear(cache, TB_BROWSE_CODE, -1);
    tb_browse_clear(cache, TB_BROWSE_ANCHORS, -1);
    tb_browse_clear(cache, TB_BROWSE_SOURCE, -1);
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { free(code->views[i].source); free(code->views[i].offsets); free(code->views[i].forward); free(code->views[i].scan_prefix); }
    memset(code, 0, sizeof *code); code->gdb = gdb; code->context = context;
    code->images = images; code->cache = cache; code->revision = revision + 1; code->files_dirty = true; code->selected_level = -1;
    for (int i = 0; i < TB_CODE_VIEWS; ++i) {
        code->views[i].data.source_follow = code->views[i].data.assembly_follow = true;
        code->views[i].data.stale = true;
        code->views[i].tab_count = 1;
        code->views[i].source_dirty = code->views[i].assembly_dirty = true;
    }
}
void tb_code_destroy(TbCode *code)
{
    if (!code) { return; }
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { free(code->views[i].source); free(code->views[i].offsets); free(code->views[i].forward); free(code->views[i].scan_prefix); }
    tb_images_destroy(code->images); tb_browse_release(code->cache); free(code);
}
TbBrowseCache *tb_code_cache(TbCode *code) { return code->cache; }
void tb_code_native(TbCode *code, pid_t pid, const char *executable)
{
    if (pid != code->pid || strcmp(code->executable, executable ? executable : "") != 0) {
        tb_images_reset(code->images); code->image_revision = tb_images_revision(code->images); ++code->epoch;
    }
    code->pid = pid;
    (void)snprintf(code->executable, sizeof code->executable, "%s", executable ? executable : "");
}
static bool image_lookup(const TbCode *code, uint64_t address, TbImageRange *range)
{
    return tb_images_trusted_lookup(code->images, address, range);
}
static void image_changes(TbCode *code)
{
    uint64_t revision = tb_images_revision(code->images);
    if (code->image_revision != revision) { code->image_revision = revision; ++code->epoch; ++code->revision; }
}
void tb_code_image_invalidate(TbCode *code)
{
    ++code->epoch;
    if (tb_images_unverify(code->images)) { message(code, "Image mapping UNVERIFIED; historical listings retained. Review before trusting ELF metadata."); }
    code->image_revision = tb_images_revision(code->images);
    ++code->revision;
}
bool tb_code_image_review(TbCode *code, uint64_t bias, TbCodeImageReview *review)
{
    if (!review || code->pid > 1 || !*code->executable || tb_gdb_state(code->gdb) != TB_GDB_STOPPED) { return false; }
    TbImageReview image;
    bool loaded = tb_images_review(code->images, code->executable, bias, &image); image_changes(code);
    if (!loaded) { return false; }
    *review = (TbCodeImageReview){.image = image, .generation = tb_gdb_generation(code->gdb), .epoch = code->epoch};
    return true;
}
bool tb_code_image_range(const TbCode *code, const TbCodeImageReview *review, size_t index, TbImageRange *range)
{
    return review && review->epoch == code->epoch && review->generation.session == tb_gdb_generation(code->gdb).session &&
           tb_images_review_range(code->images, &review->image, index, range);
}
bool tb_code_image_confirm(TbCode *code, const TbCodeImageReview *review)
{
    if (!review || code->pid > 1 || tb_gdb_state(code->gdb) != TB_GDB_STOPPED || review->epoch != code->epoch ||
        review->generation.session != tb_gdb_generation(code->gdb).session ||
        !memchr(review->image.path, 0, sizeof review->image.path) || strcmp(review->image.path, code->executable) != 0) { return false; }
    bool confirmed = tb_images_confirm(code->images, &review->image, TB_IMAGE_USER_CONFIRMED); image_changes(code);
    if (!confirmed) { return false; }
    ++code->revision; message(code, "ELF/load addresses USER-CONFIRMED; not byte verification or memory-read permission."); return true;
}
const TbCodeView *tb_code_view(const TbCode *code, int view) { return valid_view(view) ? &code->views[view].data : NULL; }
size_t tb_code_rows(const TbCodeView *view, TbCodeRow *rows)
{
    if (!view) { return 0; } size_t count = 0;
    if (view->gap_end > view->gap_begin && (!view->instruction_count || view->gap_end <= view->instructions[0].numeric)) {
        if (rows) { rows[count] = (TbCodeRow){TB_CODE_GAP, 0, view->gap_begin, view->gap_end}; } ++count;
    }
    for (size_t i = 0; i < view->instruction_count; ++i) {
        const TbInstruction *instruction = &view->instructions[i];
        if (i) {
            const TbInstruction *previous = &view->instructions[i - 1];
            if (previous->bytes && previous->numeric <= UINT64_MAX - previous->bytes && previous->numeric + previous->bytes < instruction->numeric) {
                if (rows) { rows[count] = (TbCodeRow){TB_CODE_GAP, i, previous->numeric + previous->bytes, instruction->numeric}; } ++count;
            }
        }
        if (view->mixed && view->mixed_available && instruction->line > 0 &&
            (!i || instruction->line != view->instructions[i - 1].line || strcmp(instruction->source, view->instructions[i - 1].source) != 0)) {
            if (rows) { rows[count] = (TbCodeRow){TB_CODE_SOURCE, i, instruction->numeric, instruction->numeric}; } ++count;
        }
        if (rows) { rows[count] = (TbCodeRow){TB_CODE_INSTRUCTION, i, instruction->numeric, instruction->numeric}; } ++count;
    }
    return count;
}
const char *tb_code_gap_label(const TbCode *code, int id, uint64_t begin, uint64_t end)
{
    if (!valid_view(id) || code->views[id].data.assembly_historical) { return "historical gap"; }
    TbImageRange range;
    return image_lookup(code, begin, &range) && range.kind == TB_IMAGE_DATA && range.end >= end ? "data gap" : "not decoded";
}
TbImageTrust tb_code_assembly_mapping(const TbCode *code, int id, size_t first, size_t count)
{
    if (!valid_view(id)) { return TB_IMAGE_UNVERIFIED; }
    const TbCodeView *view = &code->views[id].data;
    if (!count || first >= view->instruction_count || view->assembly_historical || tb_images_limited(code->images)) { return TB_IMAGE_UNVERIFIED; }
    TbImageTrust trust = TB_IMAGE_BACKEND_VERIFIED;
    for (size_t i = first; count && i < view->instruction_count; ++i, --count) {
        TbImageRange range;
        if (!image_lookup(code, view->instructions[i].numeric, &range) || range.kind != TB_IMAGE_CODE) { return TB_IMAGE_UNVERIFIED; }
        if (range.trust == TB_IMAGE_USER_CONFIRMED) { trust = TB_IMAGE_USER_CONFIRMED; }
    }
    return trust;
}
const char *tb_code_assembly_notice(const TbCode *code, int id, size_t first, size_t count)
{
    if (!valid_view(id)) { return ""; }
    const TbCodeView *view = &code->views[id].data;
    if (view->assembly_historical) { return "Historical cached instructions; not fresh decoding evidence"; }
    bool unknown = false, data = false, user = false, backend = false;
    for (size_t i = first; count && i < view->instruction_count; ++i, --count) {
        TbImageRange range;
        if (!image_lookup(code, view->instructions[i].numeric, &range)) { unknown = true; }
        else {
            data = data || range.kind == TB_IMAGE_DATA;
            user = user || range.trust == TB_IMAGE_USER_CONFIRMED;
            backend = backend || range.trust == TB_IMAGE_BACKEND_VERIFIED;
        }
    }
    if (data) { return "Raw decoding of non-code data"; }
    if (unknown) { return "Code range unknown for visible instructions"; }
    if (tb_images_limited(code->images)) { return "Code catalog limited; explicit Go to remains available"; }
    if (user) { return "USER-CONFIRMED ELF mapping; live bytes not verified"; }
    return backend ? "Native mapping BACKEND-VERIFIED; live bytes not verified" : "";
}
uint64_t tb_code_revision(const TbCode *code) { return code->revision; }
const char *tb_code_message(const TbCode *code) { return code->message; }
size_t tb_code_file_count(const TbCode *code) { return code->file_count; }
const TbCodeFile *tb_code_file(const TbCode *code, size_t index) { return index < code->file_count ? &code->files[index] : NULL; }
bool tb_code_files_limited(const TbCode *code) { return code->files_limited; }
bool tb_code_catalog_pending(const TbCode *code) { return code->waiting && code->job == CODE_FILES; }
bool tb_code_files_ready(const TbCode *code)
{ return !code->files_dirty && !(code->waiting && code->job == CODE_FILES); }
void tb_code_request_files(TbCode *code) { code->files_dirty = code->files_requested = true; ++code->revision; }
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
    if (view->tab_active && view->restored[view->tab_active - 1] && view->unavailable[view->tab_active - 1]) {
        (void)snprintf(view->data.source_error, sizeof view->data.source_error, "Saved Source unavailable. Setup > Unresolved setup entries offers Retry or Remove."); return;
    }
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
typedef struct {
    char path[512], text[1024];
    int line;
    bool available;
} SourceAnnotation;
static TbBrowseKey annotation_key(int id, const TbInstruction *instruction)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)instruction->source; *p; ++p) {
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    }
    hash = (hash ^ (unsigned)instruction->line) * UINT64_C(1099511628211);
    if (hash == UINT64_MAX) { --hash; }
    return (TbBrowseKey){.kind = TB_BROWSE_SOURCE, .view = id, .begin = hash, .end = hash + 1};
}
bool tb_code_mixed_line(const TbCode *code, int id, const TbInstruction *instruction, char *out, size_t capacity)
{
    if (!valid_view(id) || !instruction || !capacity) { return false; } out[0] = 0;
    if (instruction->line <= 0) { return false; }
    if (strcmp(instruction->source, code->views[id].data.path) == 0) {
        return tb_code_source_line(code, id, (size_t)instruction->line - 1, out, capacity);
    }
    TbBrowseSample sample;
    if (!tb_browse_find(code->cache, annotation_key(id, instruction), &sample) || sample.size != sizeof(SourceAnnotation)) { return false; }
    SourceAnnotation annotation; memcpy(&annotation, sample.data, sizeof annotation);
    if (!annotation.available || annotation.line != instruction->line || strcmp(annotation.path, instruction->source) != 0) { return false; }
    (void)snprintf(out, capacity, "%s", annotation.text); return true;
}
/* At most one bounded local source file per pump. Keep renderer lookup free of
 * I/O and keep all retained annotations inside the shared allocation budget. */
static void mixed_sources(TbCode *code, int id)
{
    View *view = &code->views[id];
    if (!view->visible_assembly || !view->data.mixed || !view->data.mixed_available) { return; }
    for (size_t i = 0; i < view->data.instruction_count; ++i) {
        const TbInstruction *instruction = &view->data.instructions[i];
        if (view->mixed_checked[i]) { continue; } view->mixed_checked[i] = true;
        if (instruction->line <= 0 || !*instruction->source || strcmp(instruction->source, view->data.path) == 0) { continue; }
        char existing[1024];
        if (tb_code_mixed_line(code, id, instruction, existing, sizeof existing)) { continue; }
        View *local = calloc(1, sizeof *local); if (!local) { return; }
        (void)snprintf(local->data.path, sizeof local->data.path, "%s", instruction->source); source_load(local);
        for (size_t j = i; j < view->data.instruction_count; ++j) {
            const TbInstruction *other = &view->data.instructions[j];
            if (strcmp(other->source, instruction->source) != 0) { continue; }
            view->mixed_checked[j] = true;
            SourceAnnotation annotation = {.line = other->line};
            (void)snprintf(annotation.path, sizeof annotation.path, "%s", other->source);
            if (other->line > 0 && (size_t)other->line <= local->data.line_count) {
                size_t start = local->offsets[other->line - 1], end = local->offsets[other->line];
                while (end > start && (local->source[end - 1] == '\n' || local->source[end - 1] == '\r')) { --end; }
                tb_code_safe(annotation.text, sizeof annotation.text, local->source + start, end - start); annotation.available = true;
            }
            (void)tb_browse_store(code->cache, annotation_key(id, other), &annotation, sizeof annotation);
        }
        free(local->source); free(local->offsets); free(local); ++code->revision; return;
    }
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
    const TbCodeFile *chosen = &code->files[file];
    const char *path = *chosen->full ? chosen->full : chosen->file;
    size_t tab = 1;
    for (; tab < view->tab_count; ++tab) {
        const TbCodeFile *opened = &view->tabs[tab - 1];
        if (strcmp(opened->file, chosen->file) != 0) { continue; }
        bool same = strcmp(opened->full, chosen->full) == 0;
        if (!same && view->restored[tab - 1]) {
            size_t matches = 0;
            for (size_t i = 0; i < code->file_count; ++i) { matches += strcmp(opened->file, code->files[i].file) == 0; }
            same = (matches == 1 && !code->files_limited) || (*view->resolved[tab - 1] && strcmp(view->resolved[tab - 1], chosen->full) == 0);
        }
        if (same) { break; }
    }
    if (tab == view->tab_count) {
        if (view->tab_count == TB_CODE_TABS) { message(code, "Source tab limit reached; close a file tab first."); return false; }
        view->tabs[tab - 1] = *chosen; view->restored[tab - 1] = view->unavailable[tab - 1] = false;
        view->resolved[tab - 1][0] = 0; ++view->tab_count;
    }
    if (view->restored[tab - 1]) { (void)snprintf(view->resolved[tab - 1], TB_CODE_PATH, "%s", path); }
    view->unavailable[tab - 1] = false;
    view->tab_active = tab;
    (void)snprintf(view->data.path, sizeof view->data.path, "%s", path);
    memmove(view->file_key, chosen->file, sizeof view->file_key);
    view->data.source_follow = false; view->source_dirty = true;
    source_load(view); ++code->revision;
    message(code, *view->data.source_error ? view->data.source_error : "Source opened; execution unchanged."); return true;
}
bool tb_code_tab_restore(TbCode *code, int id, const TbCodeFile *file)
{
    if (!valid_view(id) || !file || !*file->file || strnlen(file->file, TB_CODE_PATH) == TB_CODE_PATH ||
        strnlen(file->full, TB_CODE_PATH) == TB_CODE_PATH) { return false; }
    View *view = &code->views[id]; if (view->tab_count == TB_CODE_TABS) { return false; }
    size_t slot = view->tab_count++ - 1; view->tabs[slot] = *file;
    view->restored[slot] = view->unavailable[slot] = true; view->resolved[slot][0] = 0;
    ++code->revision; return true;
}
bool tb_code_tab_unavailable(const TbCode *code, int id, size_t tab)
{ return valid_view(id) && tab && tab < code->views[id].tab_count && code->views[id].unavailable[tab - 1]; }
bool tb_code_tab_retry(TbCode *code, int id, size_t tab)
{
    if (!valid_view(id) || !tab || tab >= code->views[id].tab_count || !tb_code_files_ready(code)) { return false; }
    View *view = &code->views[id]; size_t slot = tab - 1;
    if (!view->restored[slot]) { return !view->unavailable[slot]; }
    const TbCodeFile *saved = &view->tabs[slot], *chosen = NULL; size_t matches = 0; bool exact = false;
    for (size_t i = 0; i < code->file_count; ++i) {
        if (strcmp(saved->file, code->files[i].file) != 0) { continue; }
        ++matches; chosen = &code->files[i];
        if (*saved->full && strcmp(saved->full, chosen->full) == 0) { matches = 1; exact = true; break; }
    }
    struct stat info;
    bool available = matches == 1 && (exact || !code->files_limited) && chosen && chosen->full[0] == '/' &&
        stat(chosen->full, &info) == 0 && S_ISREG(info.st_mode);
    view->unavailable[slot] = !available;
    if (available) { (void)snprintf(view->resolved[slot], TB_CODE_PATH, "%s", chosen->full); }
    else { view->resolved[slot][0] = 0; }
    if (view->tab_active == tab) {
        (void)snprintf(view->data.path, sizeof view->data.path, "%s", available ? view->resolved[slot] : (*saved->full ? saved->full : saved->file));
        view->source_dirty = true; source_load(view);
        if (*view->data.source_error) { view->unavailable[slot] = true; available = false; }
    }
    ++code->revision; return available;
}
size_t tb_code_tab_count(const TbCode *code, int id) { return valid_view(id) ? code->views[id].tab_count : 0; }
size_t tb_code_tab_active(const TbCode *code, int id) { return valid_view(id) ? code->views[id].tab_active : 0; }
const TbCodeFile *tb_code_tab_file(const TbCode *code, int id, size_t tab)
{
    return valid_view(id) && tab > 0 && tab < code->views[id].tab_count ? &code->views[id].tabs[tab - 1] : NULL;
}
bool tb_code_tab_select(TbCode *code, int id, size_t tab)
{
    if (!valid_view(id) || tab >= code->views[id].tab_count) { return false; }
    View *view = &code->views[id];
    if (view->tab_active == tab) { return true; }
    view->tab_active = tab;
    view->data.source_follow = tab == 0;
    if (!tab) {
        const TbContextSnapshot *context = tb_context_snapshot(code->context);
        (void)snprintf(view->data.path, sizeof view->data.path, "%s", context->ready ? context->selected.path : "");
        view->file_key[0] = '\0';
    } else {
        const TbCodeFile *file = &view->tabs[tab - 1];
        (void)snprintf(view->data.path, sizeof view->data.path, "%s",
                       view->restored[tab - 1] && !view->unavailable[tab - 1] ? view->resolved[tab - 1] : *file->full ? file->full : file->file);
        memmove(view->file_key, file->file, sizeof view->file_key);
    }
    view->source_dirty = true; source_load(view);
    if (tab && view->restored[tab - 1] && *view->data.source_error) { view->unavailable[tab - 1] = true; }
    ++code->revision; return true;
}
bool tb_code_tab_close(TbCode *code, int id, size_t tab)
{
    if (!valid_view(id) || !tab || tab >= code->views[id].tab_count) { return false; }
    View *view = &code->views[id];
    if (tab == view->tab_active && !tb_code_tab_select(code, id, tab == 1 ? 0 : tab - 1)) { return false; }
    if (tab + 1 < view->tab_count) {
        memmove(&view->tabs[tab - 1], &view->tabs[tab], (view->tab_count - tab - 1) * sizeof view->tabs[0]);
        memmove(&view->restored[tab - 1], &view->restored[tab], (view->tab_count - tab - 1) * sizeof view->restored[0]);
        memmove(&view->unavailable[tab - 1], &view->unavailable[tab], (view->tab_count - tab - 1) * sizeof view->unavailable[0]);
        memmove(view->resolved[tab - 1], view->resolved[tab], (view->tab_count - tab - 1) * sizeof view->resolved[0]);
    }
    --view->tab_count;
    if (view->tab_active > tab) { --view->tab_active; }
    ++code->revision; return true;
}
void tb_code_tab_reset(TbCode *code, int id)
{
    if (!valid_view(id)) { return; }
    View *view = &code->views[id]; view->tab_count = 1; view->tab_active = 0;
    view->data.source_follow = true; view->file_key[0] = '\0';
    view->source_dirty = true; ++code->revision;
}
void tb_code_clear(TbCode *code, int id)
{
    if (!valid_view(id)) { return; }
    View *view = &code->views[id]; uint64_t version = view->version + 1;
    free(view->source); free(view->offsets); free(view->forward); free(view->scan_prefix);
    memset(view, 0, sizeof *view); view->version = version;
    view->data.source_follow = view->data.assembly_follow = true;
    view->data.stale = true; view->tab_count = 1;
    view->source_dirty = view->assembly_dirty = true;
    tb_browse_clear(code->cache, TB_BROWSE_CODE, id);
    tb_browse_clear(code->cache, TB_BROWSE_ANCHORS, id);
    tb_browse_clear(code->cache, TB_BROWSE_SOURCE, id);
    ++code->revision;
}

/* Validate the complete reply before replacing a useful retained listing.
 * Mixed mode adds one source wrapper; its instruction list is otherwise flat. */
static bool assembly_valid(const TbMiValue *list, bool nested, size_t *instruction_count)
{
    if (!list || list->type != TB_MI_LIST) { return false; }
    for (const TbMiValue *item = list->children; item; item = item->next) {
        if (item->type != TB_MI_TUPLE) { return false; }
        const TbMiValue *children = tb_mi_find(item->children, "line_asm_insn");
        if (children) {
            if (nested || !assembly_valid(children, true, instruction_count)) { return false; }
        } else {
            uint64_t address;
            const TbMiValue *text = tb_mi_find(item->children, "inst");
            if (!address_value(tb_mi_find(item->children, "address"), &address) ||
                !text || text->type != TB_MI_STRING || !text->string.length) { return false; }
            ++*instruction_count;
        }
    }
    return true;
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
        if (line > 0 && *instruction->source) { view->data.mixed_available = true; }
    }
}
static bool request(TbCode *code, CodeJob job, const char *operation, const char *const args[], size_t count, TbGdbScope scope)
{
    code->job = job;
    code->request_version = code->views[code->active].version;
    code->request_epoch = code->epoch;
    code->waiting = tb_gdb_request(code->gdb, operation, args, count, scope, CODE_COOKIE + (uint64_t)job, 3000, NULL);
    if (!code->waiting) { code->job = CODE_NONE; message(code, "Code request unavailable; retry while stopped."); }
    return code->waiting;
}
static void files_parse(TbCode *code, const TbMiRecord *record)
{
    code->file_count = 0; code->files_limited = false;
    const TbMiValue *list = tb_mi_find(record->results, "files");
    if (!list || list->type != TB_MI_LIST) { code->files_limited = true; message(code, "GDB returned no source-file catalog."); return; }
    for (const TbMiValue *item = list->children; item; item = item->next) {
        if (code->file_count == TB_CODE_FILES) { code->files_limited = true; break; }
        TbCodeFile *file = &code->files[code->file_count];
        if (!raw(file->file, sizeof file->file, tb_mi_find(item->children, "file"))) { code->files_limited = true; continue; }
        (void)raw(file->full, sizeof file->full, tb_mi_find(item->children, "fullname"));
        ++code->file_count;
    }
    for (int id = 0; id < TB_CODE_VIEWS; ++id) {
        View *view = &code->views[id];
        if (view->data.source_follow || !*view->file_key || (view->tab_active && view->restored[view->tab_active - 1])) { continue; }
        size_t found = 0, index = 0;
        for (size_t i = 0; i < code->file_count; ++i) {
            if (strcmp(code->files[i].file, view->file_key) == 0) { ++found; index = i; }
        }
        if (found == 1 && *code->files[index].full) {
            memmove(view->data.path, code->files[index].full, sizeof view->data.path);
            memmove(view->tabs[view->tab_active - 1].full, code->files[index].full, TB_CODE_PATH);
            view->source_dirty = true;
        }
    }
    message(code, code->files_limited ? "Source catalog incomplete or limited to 256 files; use exact identities." : "GDB source-file catalog refreshed.");
}
static void invalidate_sources(TbCode *code)
{
    tb_browse_clear(code->cache, TB_BROWSE_SOURCE, -1);
    code->files_dirty = true; tb_context_refresh(code->context);
    for (int i = 0; i < TB_CODE_VIEWS; ++i) {
        code->views[i].source_dirty = code->views[i].assembly_dirty = code->views[i].assembly_refresh = true;
        memset(code->views[i].mixed_checked, 0, sizeof code->views[i].mixed_checked);
    }
}
static bool preserve_forward(View *view)
{
    TbInstruction *saved = malloc(view->data.instruction_count * sizeof *saved);
    if (!saved) { return false; }
    memcpy(saved, view->data.instructions, view->data.instruction_count * sizeof *saved);
    view->forward = saved; view->forward_count = view->data.instruction_count;
    view->forward_origin = (ListingOrigin){.generation = view->data.assembly_generation, .epoch = view->data.assembly_epoch,
        .next_address = view->data.next_address, .gap_begin = view->data.gap_begin, .gap_end = view->data.gap_end,
        .more = view->data.more, .mixed_available = view->data.mixed_available, .truncated = view->data.truncated,
        .function_start = view->function_start, .range_end = view->range_end, .follow_pc = view->follow_pc,
        .function_known = view->function_known, .indexed = view->indexed};
    return true;
}
static void restore_forward(View *view)
{
    if (!view->forward || !view->forward_count) { return; }
    memcpy(view->data.instructions, view->forward, view->forward_count * sizeof *view->forward);
    view->data.instruction_count = view->forward_count;
    view->data.anchor = view->data.instructions[0].numeric;
    view->data.assembly_generation = view->forward_origin.generation; view->data.assembly_epoch = view->forward_origin.epoch;
    view->data.next_address = view->forward_origin.next_address;
    view->data.gap_begin = view->forward_origin.gap_begin; view->data.gap_end = view->forward_origin.gap_end;
    view->data.more = view->forward_origin.more; view->data.mixed_available = view->forward_origin.mixed_available;
    view->data.truncated = view->forward_origin.truncated;
    view->function_start = view->forward_origin.function_start; view->function_known = view->forward_origin.function_known;
    view->range_end = view->forward_origin.range_end; view->indexed = view->forward_origin.indexed;
    view->follow_pc = view->forward_origin.follow_pc;
}
static void cancel_precontext(View *view)
{
    restore_forward(view);
    if (view->data.instruction_count) { view->data.anchor = view->data.instructions[0].numeric; }
    view->precontext = false;
    free(view->forward); view->forward = NULL; view->forward_count = 0;
    free(view->scan_prefix); view->scan_prefix = NULL; view->scan_count = 0; view->scan_target = 0; view->scan_rows = 0;
}

static void append_forward(View *view)
{
    if (!view->forward) { return; }
    /* Retain half of the old window at the expansion edge. This leaves room
     * for new rows while preserving addresses still visible at that edge. */
    TbInstruction *merged = malloc((size_t)2 * TB_CODE_INSNS * sizeof *merged);
    if (!merged) {
        restore_forward(view);
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
        size_t keep = count < TB_CODE_INSNS ? count : TB_CODE_INSNS, boundary = 0;
        uint64_t address = view->forward[view->merge_tail ? view->forward_count - 1 : 0].numeric;
        while (boundary < count && merged[boundary].numeric < address) { ++boundary; }
        size_t context = view->merge_tail ? TB_CODE_INSNS / 2 - 1 : TB_CODE_INSNS / 2;
        size_t start = boundary > context ? boundary - context : 0;
        if (start > count - keep) { start = count - keep; }
        memcpy(view->data.instructions, merged + start, keep * sizeof *merged); view->data.instruction_count = keep;
        free(merged);
    }
    if (view->data.instruction_count) {
        view->data.anchor = view->data.instructions[0].numeric;
        const TbInstruction *last = &view->data.instructions[view->data.instruction_count - 1];
        view->data.next_address = last->numeric <= UINT64_MAX - last->bytes ? last->numeric + last->bytes : 0;
    }
    free(view->forward); view->forward = NULL; view->forward_count = 0;
}
bool tb_code_event(TbCode *code, const TbGdbEvent *event)
{
    if (event->kind == TB_GDB_RECORD && event->record && event->record->kind == TB_MI_NOTIFY &&
        strcmp(event->record->klass, "memory-changed") == 0) { tb_code_image_invalidate(code); }
    if (event->kind == TB_GDB_RECORD && event->record && event->record->kind == TB_MI_NOTIFY &&
        (strcmp(event->record->klass, "library-loaded") == 0 || strcmp(event->record->klass, "library-unloaded") == 0 ||
         strcmp(event->record->klass, "thread-group-started") == 0)) { code->images_dirty = true; ++code->epoch; }
    if (event->cookie <= CODE_COOKIE || event->cookie > CODE_COOKIE + CODE_CLEAR_DIRS ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    code->waiting = false;
    CodeJob job = (CodeJob)(event->cookie - CODE_COOKIE); code->job = CODE_NONE;
    View *view = &code->views[code->active];
    if ((job == CODE_ASSEMBLY || job == CODE_SYMBOL) && code->request_epoch != code->epoch) {
        cancel_precontext(view); view->assembly_dirty = view->data.assembly_follow; view->data.stale = true;
        message(code, "Image/code evidence changed during the request; prior listing retained.");
        ++code->revision; return true;
    }
    if ((job == CODE_ASSEMBLY || job == CODE_SYMBOL) && code->request_version != view->version) {
        cancel_precontext(view);
        view->assembly_dirty = true; view->data.stale = true; ++code->revision; return true;
    }
    if (event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED) {
        if (job >= CODE_DIRECTORY) { code->restoring = false; message(code, "Source profile application cancelled; review source settings."); }
        cancel_precontext(view);
        if (job == CODE_FILES) { code->files_dirty = true; }
        if (job == CODE_FLAVOR) { code->flavor_done = false; }
        view->assembly_dirty = view->data.assembly_follow; view->data.stale = true; ++code->revision; return true;
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
            ++view->data.assembly_revision;
            if (job == CODE_ASSEMBLY && view->forward) {
                cancel_precontext(view);
            }
            if (view->data.instruction_count) {
                /* A failed neighboring read is not evidence that the retained
                 * listing disappeared. Keep a usable browsing origin instead
                 * of leaving an empty pane that refuses all paging. */
                view->data.anchor = view->data.instructions[0].numeric;
                const TbInstruction *last = &view->data.instructions[view->data.instruction_count - 1];
                view->data.next_address = last->numeric <= UINT64_MAX - last->bytes ? last->numeric + last->bytes : 0;
            }
        }
        message(code, error); return true;
    }
    if (job == CODE_FILES) { files_parse(code, event->record); }
    else if (job == CODE_ASSEMBLY) {
        const TbMiValue *list = tb_mi_find(event->record->results, "asm_insns");
        size_t decoded = 0;
        if (!assembly_valid(list, false, &decoded) || !decoded) {
            cancel_precontext(view);
            if (view->data.instruction_count) {
                view->data.anchor = view->data.instructions[0].numeric;
            }
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error,
                           "Empty or malformed disassembly reply; prior listing retained. No retry sent.");
            ++view->data.assembly_revision; ++code->revision; return true;
        }
        if (view->scan_target && decoded > TB_CODE_INSNS - view->scan_rows) {
            cancel_precontext(view);
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error,
                "Reconstruction exceeds 128 decoded instruction rows; prior listing/provenance retained. No retry sent.");
            ++view->data.assembly_revision; ++code->revision; return true;
        }
        view->data.assembly_generation = event->generation;
        view->data.assembly_epoch = code->request_epoch;
        view->data.instruction_count = 0; view->data.mixed_available = view->data.more = false;
        view->function_known = false;
        view->data.assembly_error[0] = '\0';
        assembly_parse(view, list, NULL, 0);
        if (view->scan_target && !view->data.instruction_count) {
            memcpy(view->data.instructions, view->forward, view->forward_count * sizeof *view->forward);
            view->data.instruction_count = view->forward_count; cancel_precontext(view);
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "No earlier instructions available; listing retained.");
            ++view->data.assembly_revision; ++code->revision; return true;
        }
        if (view->scan_target && view->data.instruction_count) {
            view->scan_rows += decoded;
            uint64_t next = view->data.next_address;
            if (view->indexed && !view->raw && next < view->scan_target) {
                TbImageRange range;
                if (!image_lookup(code, next, &range) || range.kind != TB_IMAGE_CODE) {
                    if (tb_images_trusted_neighbor(code->images, next, 1, &range) && range.anchor <= view->scan_target) {
                        next = range.anchor;
                    } else { next = 0; }
                }
            }
            if (next < view->scan_target && next > view->data.anchor && view->scan_rows < TB_CODE_INSNS) {
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
        memset(view->mixed_checked, 0, sizeof view->mixed_checked);
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

static void follow_address(const TbCode *code, View *view, uint64_t address)
{
    view->follow_pc = address;
    if (view->precontext) { return; }
    /* Old rows are history, not evidence of boundaries after execution. */
    if (view->data.assembly_generation.session != view->generation.session ||
        view->data.assembly_generation.stop != view->generation.stop || view->data.assembly_epoch != code->epoch) {
        view->data.anchor = address; view->origin_tried = false; return;
    }
    for (size_t i = 0; i < view->data.instruction_count; ++i) {
        if (view->data.instructions[i].numeric == address) { return; }
    }
    uint64_t anchor = address;
    view->data.anchor = anchor; view->origin_tried = anchor != address;
}
bool tb_code_settled(const TbCode *code, int id)
{
    if (id < 0 || id >= TB_CODE_VIEWS) { return false; }
    const View *v = &code->views[id];
    const TbContextSnapshot *c = tb_context_snapshot(code->context);
    return c->ready && !c->stale && !v->data.stale && generation_equal(v->generation, c->generation) &&
        !v->source_dirty && (!v->visible_assembly || (!v->assembly_dirty && !v->precontext)) &&
        !(code->waiting && code->active == id);
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
        bool historical = view->data.instruction_count &&
                          (!stopped || view->data.assembly_generation.session != context->generation.session ||
                           view->data.assembly_generation.stop != context->generation.stop ||
                           view->data.assembly_epoch != code->epoch);
        if (view->data.assembly_historical != historical) { view->data.assembly_historical = historical; ++code->revision; }
        bool stale = !stopped || context->stale || !context->ready ||
                     !generation_equal(view->generation, context->generation) || view->source_dirty ||
                     (view->visible_assembly && historical) ||
                     (view->visible_assembly && view->assembly_dirty) ||
                     (code->waiting && code->active == i && (code->job == CODE_ASSEMBLY || code->job == CODE_SYMBOL));
        if (view->data.stale != stale) { view->data.stale = stale; ++code->revision; }
        mixed_sources(code, i);
    }
    if (code->waiting || (!required && !code->files_requested)) { return; }
    if (code->files_requested && (tb_gdb_state(code->gdb) == TB_GDB_READY || stopped)) {
        code->files_dirty = code->files_requested = false;
        (void)request(code, CODE_FILES, "-file-list-exec-source-files", NULL, 0, TB_GDB_SESSION); return;
    }
    if (!stopped) {
        if (tb_gdb_state(code->gdb) == TB_GDB_READY && code->files_dirty) {
            code->files_dirty = code->files_requested = false;
            (void)request(code, CODE_FILES, "-file-list-exec-source-files", NULL, 0, TB_GDB_SESSION);
        }
        return;
    }
    if (context->stale || !context->ready) { return; }
    if (code->images_dirty) { tb_images_reset(code->images); code->image_revision = tb_images_revision(code->images); code->images_dirty = false; }
    bool changed = code->stop != context->generation.stop || code->selected_level != context->selected.level ||
                   strcmp(code->selected_thread, context->selected_thread) != 0 || strcmp(code->selected_address, context->selected.address) != 0;
    bool decoding = false;
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { decoding = decoding || code->views[i].assembly_dirty; }
    if ((changed || decoding) && tb_images_validate(code->images)) {
        message(code, "ELF identity changed; mapping unverified. Historical listings retained.");
    }
    image_changes(code);
    code->stop = context->generation.stop; code->selected_level = context->selected.level;
    if (changed && code->pid > 1) {
        bool symbol; uint64_t pc;
        if (tb_code_location(context->selected.address, &pc, &symbol) && !symbol) { tb_images_native(code->images, code->pid, code->executable, pc); }
        image_changes(code);
    }
    if (!generation_equal(code->flavor_generation, context->generation)) {
        code->flavor_done = false; code->flavor_generation = context->generation;
    }
    (void)snprintf(code->selected_thread, sizeof code->selected_thread, "%s", context->selected_thread);
    (void)snprintf(code->selected_address, sizeof code->selected_address, "%s", context->selected.address);
    for (int i = 0; i < TB_CODE_VIEWS; ++i) {
        View *view = &code->views[i];
        if (changed) { cancel_precontext(view); view->data.source_follow = view->tab_active == 0; }
        if (changed || !generation_equal(view->generation, context->generation)) {
            bool refresh = view->assembly_refresh && view->generation.session == context->generation.session &&
                           view->generation.stop == context->generation.stop;
            view->generation = context->generation; view->source_dirty = true;
            /* A context transition does not authorize a read at a manually
             * browsed address. Retain that sample and its original generation. */
            view->assembly_dirty = view->data.assembly_follow || refresh;
        }
        if (view->data.source_follow && strcmp(view->data.path, context->selected.path) != 0) {
            (void)snprintf(view->data.path, sizeof view->data.path, "%s", context->selected.path); view->source_dirty = true;
        }
        view->data.selected_line = strcmp(view->data.path, context->selected.path) == 0 ? context->selected.line : 0;
        view->data.stopped_line = strcmp(view->data.path, context->stopped.path) == 0 ? context->stopped.line : 0;
        if (view->data.assembly_follow) {
            bool symbol; uint64_t address;
            const char *pc = view->data.assembly_stopped ? context->stopped.address : context->selected.address;
            view->have_anchor = tb_code_location(pc, &address, &symbol) && !symbol;
            if (view->have_anchor) { follow_address(code, view, address); }
        }
    }
    if (!code->flavor_done && strstr(context->selected.architecture, "i386")) {
        code->flavor_done = true; /* Do not retry endlessly on unsupported setting. */
        const char *const args[] = {"disassembly-flavor", "att"};
        (void)request(code, CODE_FLAVOR, "-gdb-set", args, 2, TB_GDB_CONTEXT); return;
    }
    if (code->files_dirty) {
        code->files_dirty = code->files_requested = false;
        (void)request(code, CODE_FILES, "-file-list-exec-source-files", NULL, 0, TB_GDB_CONTEXT); return;
    }
    for (int i = 0; i < TB_CODE_VIEWS; ++i) {
        View *view = &code->views[i];
        if (!view->visible_source && !view->visible_assembly) { continue; }
        if (view->source_dirty) { source_load(view); ++code->revision; }
        if (!view->visible_assembly || !view->assembly_dirty) { continue; }
        view->assembly_dirty = view->assembly_refresh = false; code->active = i;
        if (!view->have_anchor || view->data.anchor > UINT64_MAX - 256) {
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "No valid bounded instruction address; use Go to.");
            cancel_precontext(view); ++view->data.assembly_revision;
            message(code, view->data.assembly_error); continue;
        }
        TbImageRange range;
        if (!image_lookup(code, view->data.anchor, &range) && code->pid > 1) {
            tb_images_native(code->images, code->pid, code->executable, view->data.anchor);
            image_changes(code);
        }
        if (view->forward && view->forward_origin.epoch != code->epoch) {
            cancel_precontext(view); view->data.stale = view->data.assembly_historical = true;
            (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error,
                "Image/code evidence changed before expansion; prior listing retained. No read sent.");
            ++view->data.assembly_revision; ++code->revision; continue;
        }
        if (image_lookup(code, view->data.anchor, &range)) {
            view->indexed = true; view->range_end = range.end;
            if (range.kind == TB_IMAGE_DATA && view->scan_target) {
                TbImageRange next;
                if (tb_images_trusted_neighbor(code->images, range.end, 1, &next) && next.anchor < view->scan_target) {
                    view->data.anchor = next.anchor; view->assembly_dirty = true; continue;
                }
                cancel_precontext(view);
                (void)snprintf(view->data.assembly_error, sizeof view->data.assembly_error, "Earlier code boundary unavailable; listing retained.");
                ++view->data.assembly_revision; continue;
            }
            if (range.kind == TB_IMAGE_DATA && !view->raw && !view->data.assembly_follow && !view->scan_target) {
                view->data.gap_begin = view->data.anchor; view->data.gap_end = range.end;
                TbImageRange next;
                if (tb_images_trusted_neighbor(code->images, range.end, 1, &next)) {
                    view->data.anchor = next.anchor; view->assembly_dirty = true; continue;
                }
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
            view->indexed = false; view->range_end = UINT64_MAX;
        }
        char low[48], high[48];
        (void)snprintf(low, sizeof low, "0x%" PRIx64, view->data.anchor);
        uint64_t padding = view->data.assembly_follow ? 256 : 0;
        uint64_t end = view->precontext ? view->follow_pc > UINT64_MAX - padding ? UINT64_MAX : view->follow_pc + padding : view->data.anchor + 256;
        if (view->scan_target && end > view->data.anchor + 256) { end = view->data.anchor + 256; }
        if (view->scan_target && end - view->data.anchor > TB_CODE_INSNS - view->scan_rows) {
            end = view->data.anchor + TB_CODE_INSNS - view->scan_rows;
        }
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
    listing_store(code, id);
    View *view = &code->views[id]; cancel_precontext(view);
    view->raw = false;
    view->data.source_follow = view->tab_active == 0; view->data.assembly_follow = true; view->data.assembly_stopped = false;
    view->source_dirty = view->assembly_dirty = true; view->data.stale = true;
    ++view->version; ++code->revision;
}
static bool assembly_follow(TbCode *code, int id, bool stopped)
{
    if (!valid_view(id) || tb_gdb_state(code->gdb) != TB_GDB_STOPPED) { return false; }
    const TbContextSnapshot *context = tb_context_snapshot(code->context);
    bool symbol; uint64_t address;
    if (!context->ready || context->stale ||
        !tb_code_location(stopped ? context->stopped.address : context->selected.address, &address, &symbol) || symbol) {
        message(code, "Stopped instruction address is unavailable; viewport retained."); return false;
    }
    listing_store(code, id);
    View *view = &code->views[id]; cancel_precontext(view);
    view->raw = false; view->data.assembly_follow = true; view->data.assembly_stopped = stopped;
    view->have_anchor = true; view->data.anchor = address; view->origin_tried = false;
    view->assembly_dirty = true; view->data.stale = true;
    ++view->version; ++code->revision;
    message(code, stopped ? "Returning this Disassembly view to the stopped PC; selection unchanged." :
                           "This Disassembly view follows the selected frame; execution unchanged.");
    return true;
}
bool tb_code_return_stop(TbCode *code, int id) { return assembly_follow(code, id, true); }
bool tb_code_follow_frame(TbCode *code, int id) { return assembly_follow(code, id, false); }
bool tb_code_stopped(TbCode *code)
{
    const TbContextSnapshot *context = tb_context_snapshot(code->context);
    if (!*context->stopped_thread || !tb_context_return_stopped(code->context)) {
        message(code, "Original stopped thread is unavailable; pause and refresh context."); return false;
    }
    for (int i = 0; i < TB_CODE_VIEWS; ++i) { tb_code_return(code, i); }
    message(code, "Returning to stopped context; execution unchanged."); return true;
}
void tb_code_mixed(TbCode *code, int id, bool mixed)
{
    if (!valid_view(id)) { return; }
    listing_store(code, id);
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
    listing_store(code, id);
    View *view = &code->views[id]; cancel_precontext(view); view->data.assembly_follow = false;
    view->data.gap_begin = view->data.gap_end = 0;
    ++view->version; view->data.stale = true;
    view->data.assembly_error[0] = '\0';
    if (symbol) {
        char expression[160]; (void)snprintf(expression, sizeof expression, "&'%s'", location);
        const char *const args[] = {"--language", "c", expression}; code->active = id;
        /* C address-of semantics avoid C++ overloaded operator&/inferior calls. */
        return request(code, CODE_SYMBOL, "-data-evaluate-expression", args, 3, TB_GDB_CONTEXT);
    }
    view->data.anchor = address; view->have_anchor = true; view->assembly_dirty = true; ++code->revision; return true;
}
static TbBrowseKey listing_key(const View *view, int id)
{
    return (TbBrowseKey){.kind = TB_BROWSE_CODE, .view = id,
        .format = (view->data.mixed ? 1U : 0U) | (view->raw ? 2U : 0U),
        .begin = view->data.instruction_count ? view->data.instructions[0].numeric : view->data.anchor,
        .end = view->data.next_address, .generation = view->data.assembly_generation,
        .epoch = view->data.assembly_epoch};
}
static TbBrowseKey anchor_key(const View *view, int id)
{
    TbBrowseKey key = listing_key(view, id);
    key.kind = TB_BROWSE_ANCHORS; key.begin = 0; key.end = 1; return key;
}
static void anchors_store(TbCode *code, int id)
{
    const View *view = &code->views[id]; AnchorHistory history = {0}; TbBrowseSample sample;
    TbBrowseKey key = anchor_key(view, id);
    if (tb_browse_find(code->cache, key, &sample) && sample.size == sizeof history) { memcpy(&history, sample.data, sizeof history); }
    if (history.count > CODE_ANCHORS || history.next >= CODE_ANCHORS) { memset(&history, 0, sizeof history); }
    const size_t indices[] = {0, view->data.instruction_count / 2, view->data.instruction_count - 1};
    for (size_t i = 0; i < sizeof indices / sizeof indices[0]; ++i) {
        CodeAnchor anchor = {.address = view->data.instructions[indices[i]].numeric,
            .generation = view->data.assembly_generation, .epoch = view->data.assembly_epoch};
        bool duplicate = false;
        for (size_t j = 0; j < history.count; ++j) {
            const CodeAnchor *old = &history.entries[j];
            duplicate = duplicate || (old->address == anchor.address && old->epoch == anchor.epoch &&
                old->generation.session == anchor.generation.session && old->generation.stop == anchor.generation.stop);
        }
        if (duplicate) { continue; }
        history.entries[history.next] = anchor; history.next = (history.next + 1) % CODE_ANCHORS;
        if (history.count < CODE_ANCHORS) { ++history.count; }
    }
    (void)tb_browse_store(code->cache, key, &history, sizeof history);
}
static bool anchor_before(TbCode *code, int id, uint64_t *address)
{
    View *view = &code->views[id]; TbBrowseSample sample;
    uint64_t target = view->data.instructions[0].numeric;
    if (!tb_browse_find(code->cache, anchor_key(view, id), &sample) || sample.size != sizeof(AnchorHistory)) { return false; }
    AnchorHistory history; memcpy(&history, sample.data, sizeof history);
    if (history.count > CODE_ANCHORS) { return false; }
    TbGdbGeneration current = tb_gdb_generation(code->gdb); bool found = false;
    for (size_t i = 0; i < history.count; ++i) {
        const CodeAnchor *anchor = &history.entries[i]; TbImageRange range;
        if (anchor->epoch != code->epoch || anchor->generation.session != current.session || anchor->generation.stop != current.stop ||
            anchor->address >= target || target - anchor->address > 1024 ||
            (!view->raw && image_lookup(code, anchor->address, &range) && range.kind == TB_IMAGE_DATA)) { continue; }
        if (!found || anchor->address > *address) { *address = anchor->address; found = true; }
    }
    return found;
}
static void listing_store(TbCode *code, int id)
{
    const View *view = &code->views[id];
    if (view->data.instruction_count) {
        (void)tb_browse_store(code->cache, listing_key(view, id), &view->data, sizeof view->data);
        anchors_store(code, id);
    }
}
static bool listing_cached(TbCode *code, int id, int direction)
{
    View *view = &code->views[id]; TbBrowseSample sample;
    if (!tb_browse_neighbor(code->cache, listing_key(view, id), direction, &sample) || sample.size != sizeof(TbCodeView)) { return false; }
    TbCodeView saved; memcpy(&saved, sample.data, sizeof saved);
    memcpy(view->data.instructions, saved.instructions, sizeof view->data.instructions);
    view->data.instruction_count = saved.instruction_count;
    view->data.next_address = saved.next_address; view->data.anchor = sample.key.begin;
    view->data.more = saved.more; view->data.mixed_available = saved.mixed_available;
    memset(view->mixed_checked, 0, sizeof view->mixed_checked);
    view->data.gap_begin = saved.gap_begin; view->data.gap_end = saved.gap_end;
    view->data.assembly_generation = sample.key.generation; view->data.assembly_epoch = sample.key.epoch;
    view->data.assembly_follow = false; view->assembly_dirty = false;
    view->data.assembly_historical = tb_gdb_state(code->gdb) != TB_GDB_STOPPED ||
        sample.key.generation.session != tb_gdb_generation(code->gdb).session ||
        sample.key.generation.stop != tb_gdb_generation(code->gdb).stop || sample.key.epoch != code->epoch;
    view->data.stale = view->data.assembly_historical;
    view->data.assembly_error[0] = '\0'; view->have_anchor = true; view->function_known = false;
    ++view->version; ++view->data.assembly_revision; ++code->revision;
    message(code, view->data.assembly_historical ? "Cached historical instructions; no target read." : "Cached instructions; no target read.");
    return true;
}
static bool reconstruct(TbCode *code, View *view, uint64_t anchor)
{
    uint64_t target = view->data.instructions[0].numeric;
    if (anchor >= target || target - anchor > 1024) {
        message(code, "No reliable anchor within the bounded reconstruction window; prior listing retained."); return false;
    }
    if (!preserve_forward(view)) { message(code, "Cannot preserve the instruction window; browsing unchanged."); return false; }
    view->scan_prefix = malloc(64 * sizeof *view->scan_prefix);
    if (!view->scan_prefix) { cancel_precontext(view); message(code, "Cannot allocate reconstruction context; listing unchanged."); return false; }
    view->scan_target = target; view->scan_count = view->scan_rows = 0;
    view->follow_pc = target; view->data.anchor = anchor; view->precontext = true; view->prefix_limit = 64;
    view->merge_tail = false; view->data.assembly_follow = false; view->assembly_dirty = true; view->data.stale = true;
    ++code->revision; return true;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): view index precedes signed paging direction.
bool tb_code_assembly_page(TbCode *code, int id, int direction)
{
    if (!valid_view(id) || !direction || code->waiting || code->settings_job != CODE_NONE) { return false; }
    View *view = &code->views[id];
    listing_store(code, id);
    if (listing_cached(code, id, direction)) { return true; }
    if (!view->data.instruction_count) { return false; }
    TbGdbGeneration current = tb_gdb_generation(code->gdb);
    if (tb_gdb_state(code->gdb) != TB_GDB_STOPPED || view->data.stale || view->data.assembly_historical ||
        view->data.assembly_generation.session != current.session || view->data.assembly_generation.stop != current.stop ||
        view->data.assembly_epoch != code->epoch) {
        message(code, "No cached window in that direction; stop and choose a fresh reliable instruction address. No read queued."); return false;
    }
    uint64_t retained_anchor = 0;
    bool retained = direction < 0 && anchor_before(code, id, &retained_anchor);
    if (view->indexed) {
        TbImageRange range; uint64_t anchor;
        if (direction < 0) {
            bool mapped = tb_images_trusted_neighbor(code->images, view->data.anchor, -1, &range);
            if (!mapped && !retained) { message(code, "No reliable earlier boundary; historical rows remain available."); return false; }
            anchor = retained && (!mapped || retained_anchor > range.anchor) ? retained_anchor : range.anchor;
            return reconstruct(code, view, anchor);
        } else {
            anchor = view->data.next_address;
            if (!anchor) { return false; }
            if (!image_lookup(code, anchor, &range)) {
                if (tb_images_trusted_neighbor(code->images, anchor, 1, &range)) {
                    anchor = range.anchor;
                    message(code, "Address gap to the next known instruction boundary; skipped bytes stay labeled.");
                } else { message(code, "Continuing from a decoded boundary into an unclassified range; bounded inspection only."); }
            } else if (range.kind != TB_IMAGE_CODE && !view->raw) {
                if (!tb_images_trusted_neighbor(code->images, range.end, 1, &range)) { message(code, "Data gap has no known next instruction boundary; use Decode bytes for raw inspection."); return false; }
                anchor = range.anchor;
                message(code, "Skipped non-code/unavailable address gap; loading next code region.");
            }
        }
        if (!preserve_forward(view)) { message(code, "Cannot extend instruction cache; listing unchanged."); return false; }
        view->merge_tail = true;
        view->follow_pc = view->data.anchor; view->data.anchor = anchor;
        view->precontext = false; view->prefix_limit = 64;
        view->data.assembly_follow = false; view->assembly_dirty = true; view->data.stale = true; ++code->revision; return true;
    }
    if (direction < 0) {
        if (retained) { return reconstruct(code, view, retained_anchor); }
        if (view->function_known && view->function_start < view->data.anchor && view->data.anchor - view->function_start <= 1024) {
            return reconstruct(code, view, view->function_start);
        }
        message(code, "No fresh reliable earlier anchor; historical rows cannot establish new boundaries."); return false;
    } else {
        if (!view->data.next_address || view->data.next_address <= view->data.anchor) { return false; }
        if (!preserve_forward(view)) { message(code, "Cannot preserve the instruction window; browsing unchanged."); return false; }
        view->merge_tail = true;
        view->data.anchor = view->data.next_address;
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
void tb_code_cancel_settings(TbCode *code) { code->restoring = false; ++code->revision; }
bool tb_code_add_directory(TbCode *code, const char *path)
{
    if (tb_context_interlocked(code->context) && !code->restoring) { return false; }
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
    if (tb_context_interlocked(code->context) && !code->restoring) { return false; }
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
    if (tb_context_interlocked(code->context)) { return false; }
    if (code->waiting || code->settings_job != CODE_NONE) { return false; }
    const char *const args[] = {"console", "unset substitute-path"};
    return request(code, CODE_CLEAR_RULES, "-interpreter-exec", args, 2, TB_GDB_SESSION);
}
size_t tb_code_directory_count(const TbCode *code) { return code->directory_count; }
const char *tb_code_directory(const TbCode *code, size_t index) { return index < code->directory_count ? code->directories[index] : ""; }
size_t tb_code_rule_count(const TbCode *code) { return code->rule_count; }
const TbSourceRule *tb_code_rule(const TbCode *code, size_t index) { return index < code->rule_count ? &code->rules[index] : NULL; }
