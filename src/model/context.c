#include "context.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { QUERY_NONE, QUERY_THREADS, QUERY_CURRENT, QUERY_FRAMES, QUERY_ARGUMENTS,
               SELECT_THREAD, SELECT_FRAME } Query;
enum { COOKIE_BASE = 6000 };
struct TbContext {
    TbGdb *gdb;
    TbContextSnapshot snapshot, next;
    bool visible_threads, visible_frames, required, dirty, invalidate, waiting, failed;
    bool select_thread, select_frame;
    char desired_thread[32], selection_error[192];
    int desired_frame, first_frame;
    Query query;
    uint64_t revision;
};

/* Render-safe owned bytes; MI strings may include NUL/control characters. */
static void text(char *out, size_t capacity, const TbMiValue *value)
{
    size_t used = 0;
    if (value && value->type == TB_MI_STRING) {
        for (size_t i = 0; i < value->string.length && used + 1 < capacity; ++i) {
            unsigned char c = (unsigned char)value->string.data[i];
            if (c >= 32 && c < 127) { out[used++] = (char)c; }
            else if (used + 4 < capacity) {
                static const char digits[] = "0123456789abcdef";
                out[used++] = '\\'; out[used++] = 'x';
                out[used++] = digits[c / 16]; out[used++] = digits[c % 16];
            }
        }
    }
    out[used] = '\0';
}

static int number(const TbMiValue *value)
{
    if (!value || value->type != TB_MI_STRING || value->string.length == 0) { return -1; }
    unsigned result = 0;
    for (size_t i = 0; i < value->string.length; ++i) {
        unsigned char c = (unsigned char)value->string.data[i];
        if (c < '0' || c > '9' || result > ((unsigned)INT_MAX - (c - '0')) / 10) { return -1; }
        result = result * 10 + c - '0';
    }
    return (int)result;
}

static void frame(TbFrame *out, const TbMiValue *value)
{
    memset(out, 0, sizeof *out); out->level = out->line = -1;
    if (!value || value->type != TB_MI_TUPLE) { return; }
    out->level = number(tb_mi_find(value->children, "level"));
    out->line = number(tb_mi_find(value->children, "line"));
    text(out->address, sizeof out->address, tb_mi_find(value->children, "addr"));
    text(out->function, sizeof out->function, tb_mi_find(value->children, "func"));
    const TbMiValue *file = tb_mi_find(value->children, "fullname");
    if (!file) { file = tb_mi_find(value->children, "file"); }
    text(out->file, sizeof out->file, file);
    if (file && file->type == TB_MI_STRING && file->string.length < sizeof out->path &&
        memchr(file->string.data, '\0', file->string.length) == NULL) {
        memcpy(out->path, file->string.data, file->string.length);
        out->path[file->string.length] = '\0';
    }
    text(out->from, sizeof out->from, tb_mi_find(value->children, "from"));
    text(out->architecture, sizeof out->architecture, tb_mi_find(value->children, "arch"));
}

TbContext *tb_context_create(TbGdb *gdb)
{
    TbContext *context = calloc(1, sizeof *context);
    if (context) { context->gdb = gdb; tb_context_reset(context); }
    return context;
}
void tb_context_destroy(TbContext *context) { free(context); }
void tb_context_reset(TbContext *context)
{
    TbGdb *gdb = context->gdb;
    uint64_t revision = context->revision;
    memset(context, 0, sizeof *context); context->gdb = gdb;
    context->snapshot.stale = true;
    context->snapshot.selected.level = context->snapshot.stopped.level = -1;
    context->revision = revision + 1;
}
const TbContextSnapshot *tb_context_snapshot(const TbContext *context) { return &context->snapshot; }
uint64_t tb_context_revision(const TbContext *context) { return context->revision; }

static void stale(TbContext *context)
{
    context->dirty = true; context->snapshot.stale = true;
    context->snapshot.refreshing = true; ++context->revision;
}
void tb_context_refresh(TbContext *context)
{
    tb_gdb_context_changed(context->gdb); stale(context);
}
void tb_context_visible(TbContext *context, bool threads, bool frames)
{
    if ((threads && !context->visible_threads) || (frames && !context->visible_frames)) { stale(context); }
    context->visible_threads = threads; context->visible_frames = frames;
}
void tb_context_require(TbContext *context, bool required)
{
    if (required && !context->required) { stale(context); }
    context->required = required;
}

static void parse_threads(TbContext *context, const TbMiRecord *record)
{
    TbContextSnapshot *next = &context->next;
    const TbMiValue *list = tb_mi_find(record->results, "threads");
    if (!list || list->type != TB_MI_LIST) {
        (void)snprintf(next->error, sizeof next->error, "GDB returned no structured thread list."); return;
    }
    text(next->selected_thread, sizeof next->selected_thread, tb_mi_find(record->results, "current-thread-id"));
    for (const TbMiValue *item = list->children; item; item = item->next) {
        if (next->thread_count == TB_CONTEXT_THREADS) { next->threads_limited = true; break; }
        if (item->type != TB_MI_TUPLE || number(tb_mi_find(item->children, "id")) <= 0) { continue; }
        TbThread *thread = &next->threads[next->thread_count++];
        text(thread->id, sizeof thread->id, tb_mi_find(item->children, "id"));
        text(thread->target, sizeof thread->target, tb_mi_find(item->children, "target-id"));
        text(thread->name, sizeof thread->name, tb_mi_find(item->children, "name"));
        text(thread->state, sizeof thread->state, tb_mi_find(item->children, "state"));
        frame(&thread->frame, tb_mi_find(item->children, "frame"));
    }
    if (!next->selected_thread[0]) {
        (void)snprintf(next->error, sizeof next->error, "GDB did not identify a selected thread.");
    }
}

static void parse_frames(TbContext *context, const TbMiRecord *record)
{
    const TbMiValue *list = tb_mi_find(record->results, "stack");
    if (!list || list->type != TB_MI_LIST) {
        (void)snprintf(context->next.stack_error, sizeof context->next.stack_error, "GDB returned no structured stack."); return;
    }
    for (const TbMiValue *item = list->children; item; item = item->next) {
        if (context->next.frame_count == TB_CONTEXT_FRAMES) { context->next.more_frames = true; break; }
        if (item->type != TB_MI_TUPLE) { continue; }
        frame(&context->next.frames[context->next.frame_count], item);
        if (context->next.frames[context->next.frame_count].level >= 0) { ++context->next.frame_count; }
    }
}

static void parse_arguments(TbContext *context, const TbMiRecord *record)
{
    const TbMiValue *list = tb_mi_find(record->results, "stack-args");
    if (!list || list->type != TB_MI_LIST) {
        (void)snprintf(context->next.arguments_error, sizeof context->next.arguments_error, "Frame arguments unavailable."); return;
    }
    for (const TbMiValue *item = list->children; item; item = item->next) {
        int level = number(tb_mi_find(item->children, "level"));
        for (size_t i = 0; i < context->next.frame_count; ++i) {
            TbFrame *out = &context->next.frames[i];
            if (out->level != level) { continue; }
            const TbMiValue *args = tb_mi_find(item->children, "args");
            if (!args || args->type != TB_MI_LIST) { continue; }
            size_t used = 0;
            for (const TbMiValue *arg = args->children; arg && used + 1 < sizeof out->arguments; arg = arg->next) {
                char name[64], value[128];
                text(name, sizeof name, tb_mi_find(arg->children, "name"));
                const TbMiValue *v = tb_mi_find(arg->children, "value");
                text(value, sizeof value, v);
                int written = snprintf(out->arguments + used, sizeof out->arguments - used,
                                       "%s%s=%s", used ? ", " : "", name, v ? value : "<aggregate/unavailable>");
                if (written < 0 || (size_t)written >= sizeof out->arguments - used) { break; }
                used += (size_t)written;
            }
        }
    }
}

bool tb_context_event(TbContext *context, const TbGdbEvent *event)
{
    if (event->cookie > COOKIE_BASE && event->cookie <= COOKIE_BASE + SELECT_FRAME &&
        (event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE ||
         event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) {
        context->waiting = false;
        if (event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED) {
            stale(context); context->query = QUERY_NONE; return true;
        }
        Query query = (Query)(event->cookie - COOKIE_BASE);
        context->failed = event->kind != TB_GDB_REPLY || strcmp(event->record->klass, "done") != 0;
        if (context->failed) {
            char *error = query >= SELECT_THREAD ? context->selection_error :
                          query == QUERY_THREADS ? context->next.error :
                          query == QUERY_ARGUMENTS ? context->next.arguments_error : context->next.stack_error;
            const TbMiValue *msg = event->record ? tb_mi_find(event->record->results, "msg") : NULL;
            text(error, 192, msg);
            if (!*error) { (void)snprintf(error, 192, "Context request timed out or failed; refresh to retry."); }
        } else if (query == QUERY_THREADS) { parse_threads(context, event->record); }
        else if (query == QUERY_CURRENT) {
            frame(&context->next.selected, tb_mi_find(event->record->results, "frame"));
            if (context->next.selected.level < 0) {
                (void)snprintf(context->next.stack_error, sizeof context->next.stack_error, "GDB did not supply the selected frame.");
            }
        }
        else if (query == QUERY_FRAMES) { parse_frames(context, event->record); }
        else if (query == QUERY_ARGUMENTS) { parse_arguments(context, event->record); }
        context->query = query; ++context->revision;
        return true;
    }
    const TbMiRecord *record = event->record;
    if (event->kind == TB_GDB_STATE) {
        TbGdbState state = tb_gdb_state(context->gdb);
        if (state == TB_GDB_RUNNING || state == TB_GDB_EXITED || state == TB_GDB_DISCONNECTED) {
            stale(context); context->snapshot.refreshing = false;
            context->select_thread = context->select_frame = false;
        }
    }
    if (event->kind == TB_GDB_RECORD && record) {
        if (record->kind == TB_MI_EXEC && strcmp(record->klass, "stopped") == 0) {
            text(context->snapshot.stopped_thread, sizeof context->snapshot.stopped_thread,
                 tb_mi_find(record->results, "thread-id"));
            frame(&context->snapshot.stopped, tb_mi_find(record->results, "frame"));
            /* GDB's stop frame is the innermost frame and commonly omits level. */
            if (context->snapshot.stopped.level < 0 && *context->snapshot.stopped.address) {
                context->snapshot.stopped.level = 0;
            }
            context->first_frame = 0; context->select_thread = context->select_frame = false;
            stale(context);
        } else if (record->kind == TB_MI_NOTIFY &&
                   (strcmp(record->klass, "thread-created") == 0 || strcmp(record->klass, "thread-exited") == 0 ||
                    strcmp(record->klass, "thread-selected") == 0 || strcmp(record->klass, "thread-group-exited") == 0)) {
            context->invalidate = true; stale(context);
        }
    }
    return false;
}

static bool issue(TbContext *context, Query query, const char *operation, const char *const args[], size_t count)
{
    context->query = query;
    context->waiting = tb_gdb_request(context->gdb, operation, args, count, TB_GDB_CONTEXT,
                                      COOKIE_BASE + (uint64_t)query, 3000, NULL);
    if (!context->waiting) {
        (void)snprintf(context->next.error, sizeof context->next.error, "Context request rejected; refresh after the target stops.");
        context->failed = true;
    }
    return context->waiting;
}

static void publish(TbContext *context)
{
    context->next.ready = true; context->next.stale = false; context->next.refreshing = false;
    context->next.generation = tb_gdb_generation(context->gdb);
    if (*context->selection_error) {
        (void)snprintf(context->next.error, sizeof context->next.error, "%s", context->selection_error);
    }
    context->snapshot = context->next;
    context->query = QUERY_NONE; ++context->revision;
}

void tb_context_pump(TbContext *context)
{
    if (context->invalidate) {
        context->invalidate = false; tb_gdb_context_changed(context->gdb);
    }
    if (tb_gdb_state(context->gdb) != TB_GDB_STOPPED || context->waiting ||
        (!context->visible_threads && !context->visible_frames && !context->required)) { return; }
    if (context->select_thread) {
        context->select_thread = false;
        const char *const args[] = {context->desired_thread};
        (void)issue(context, SELECT_THREAD, "-thread-select", args, 1); return;
    }
    if (context->select_frame) {
        context->select_frame = false;
        char level[24]; (void)snprintf(level, sizeof level, "%d", context->desired_frame);
        const char *const args[] = {level};
        (void)issue(context, SELECT_FRAME, "-stack-select-frame", args, 1); return;
    }
    if (context->dirty || context->query >= SELECT_THREAD) {
        context->dirty = false;
        memset(&context->next, 0, sizeof context->next);
        context->next.selected.level = -1;
        context->next.stopped = context->snapshot.stopped;
        (void)snprintf(context->next.stopped_thread, sizeof context->next.stopped_thread, "%s", context->snapshot.stopped_thread);
        context->next.first_frame = context->first_frame;
        (void)issue(context, QUERY_THREADS, "-thread-info", NULL, 0); return;
    }
    if (context->query == QUERY_THREADS) {
        if (!*context->next.selected_thread || context->failed) { publish(context); return; }
        (void)issue(context, QUERY_CURRENT, "-stack-info-frame", NULL, 0);
    } else if (context->query == QUERY_CURRENT && context->visible_frames) {
        char low[24], high[24];
        (void)snprintf(low, sizeof low, "%d", context->first_frame);
        (void)snprintf(high, sizeof high, "%d", context->first_frame + TB_CONTEXT_FRAMES);
        const char *const args[] = {"--no-frame-filters", low, high};
        (void)issue(context, QUERY_FRAMES, "-stack-list-frames", args, 3);
    } else if (context->query == QUERY_FRAMES && context->next.frame_count) {
        char low[24], high[24];
        (void)snprintf(low, sizeof low, "%d", context->first_frame);
        (void)snprintf(high, sizeof high, "%d", context->next.frames[context->next.frame_count - 1].level);
        const char *const args[] = {"--no-frame-filters", "--simple-values", low, high};
        (void)issue(context, QUERY_ARGUMENTS, "-stack-list-arguments", args, 4);
    } else if (context->query) { publish(context); }
}

bool tb_context_select_thread(TbContext *context, const char *id)
{
    if (!id || tb_gdb_state(context->gdb) != TB_GDB_STOPPED) { return false; }
    bool found = false;
    for (size_t i = 0; i < context->snapshot.thread_count; ++i) {
        if (strcmp(context->snapshot.threads[i].id, id) == 0) { found = true; break; }
    }
    if (!found) { return false; }
    (void)snprintf(context->desired_thread, sizeof context->desired_thread, "%s", id);
    context->select_thread = true; context->select_frame = false;
    context->first_frame = 0; context->selection_error[0] = '\0';
    tb_context_refresh(context); return true;
}
bool tb_context_select_frame(TbContext *context, int level)
{
    if (context->snapshot.stale || context->select_thread ||
        (context->waiting && context->query == SELECT_THREAD) ||
        tb_gdb_state(context->gdb) != TB_GDB_STOPPED) { return false; }
    bool found = false;
    for (size_t i = 0; i < context->snapshot.frame_count; ++i) {
        if (context->snapshot.frames[i].level == level) { found = true; break; }
    }
    if (!found) { return false; }
    context->desired_frame = level; context->select_frame = true;
    context->selection_error[0] = '\0'; tb_context_refresh(context); return true;
}
bool tb_context_page(TbContext *context, int direction)
{
    if (context->snapshot.stale || !context->snapshot.ready || tb_gdb_state(context->gdb) != TB_GDB_STOPPED) { return false; }
    if (direction > 0 && (!context->snapshot.more_frames || context->first_frame > TB_CONTEXT_MAX_LEVEL - TB_CONTEXT_FRAMES)) { return false; }
    if (direction < 0 && context->first_frame == 0) { return false; }
    context->first_frame += direction > 0 ? TB_CONTEXT_FRAMES : -TB_CONTEXT_FRAMES;
    tb_context_refresh(context); return true;
}
