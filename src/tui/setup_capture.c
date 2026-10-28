#include "setup_capture.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct TbSetupCapture {
    TbSetup *current, *saved, *next;
    uint64_t panes[TB_MAX_PANES], watches[TB_SETUP_WATCHES], breaks[TB_SETUP_BREAKS];
};
TbSetupCapture *tb_setup_capture_create(const TbSetup *saved)
{
    TbSetupCapture *c = calloc(1, sizeof *c); if (!c) { return NULL; }
    c->next = calloc(1, sizeof *c->next);
    if (saved) { c->saved = malloc(sizeof *c->saved); if (c->saved) { *c->saved = *saved; } }
    if (!c->next || (saved && !c->saved)) { tb_setup_capture_destroy(c); return NULL; } return c;
}
void tb_setup_capture_destroy(TbSetupCapture *c)
{
    if (!c) { return; } free(c->current); free(c->saved); free(c->next); free(c);
}
const TbSetup *tb_setup_capture_current(const TbSetupCapture *c) { return c ? c->current : NULL; }
bool tb_setup_capture_dirty(const TbSetupCapture *c) { return c && c->current && !tb_setup_equal(c->current, c->saved); }
bool tb_setup_capture_saved(TbSetupCapture *c)
{
    if (!c || !c->current) { return false; }
    if (!c->saved) { c->saved = malloc(sizeof *c->saved); if (!c->saved) { return false; } }
    *c->saved = *c->current; return true;
}
bool tb_setup_capture_baseline(TbSetupCapture *c, const TbSetup *saved)
{
    if (!c || (!saved && !c->current)) { return false; }
    if (!c->saved) { c->saved = malloc(sizeof *c->saved); if (!c->saved) { return false; } }
    *c->saved = saved ? *saved : *c->current; return true;
}
static uint64_t allocate_id(TbSetup *s)
{ return s->next_id < INT64_MAX ? s->next_id++ : 0; }
static size_t key_index(const uint64_t *keys, size_t count, uint64_t key)
{ size_t i = 0; while (i < count && keys[i] != key) { ++i; } return i; }
bool tb_setup_capture_update(TbSetupCapture *c, const TbLayout *layout, const TbDemo *workspace,
                            const TbCode *code, const TbMemory *memory, const TbVariables *variables,
                            const TbBreakpoints *breakpoints, const char *const expressions[TB_MAX_PANES])
{
    if (!c) { return false; }
    TbSetup *s = c->next; const TbSetup *old = c->current ? c->current : c->saved;
    memset(s, 0, sizeof *s); s->next_id = old ? old->next_id : 1;
    if (c->saved && c->saved->next_id > s->next_id) { s->next_id = c->saved->next_id; }
    memcpy(s->columns, layout->columns, sizeof s->columns); s->row_ratio = layout->row_ratio;
    memcpy(s->roots, layout->roots, sizeof s->roots);
    uint64_t pane_keys[TB_MAX_PANES] = {0}, watch_keys[TB_SETUP_WATCHES] = {0}, break_keys[TB_SETUP_BREAKS] = {0};
    for (size_t i = 0; i < TB_SETUP_NODES; ++i) {
        const TbLayoutNode *n = &layout->nodes[i]; if (!n->used) { continue; }
        s->nodes[i] = (TbSetupNode){true, n->pane, n->pane < 0 ? n->first : -1, n->pane < 0 ? n->second : -1,
                                  n->pane < 0 ? n->ratio : 0, n->pane < 0 ? (int)n->direction : 0};
    }
    for (int i = 0; i < TB_MAX_PANES; ++i) {
        if (!tb_layout_has_pane(layout, i)) { continue; }
        TbSetupPane *p = &s->panes[i]; const TbDemoPane *view = &workspace->panes[i];
        const TbSetupPane *previous = old && old->panes[i].used && (!c->current || c->panes[i] == layout->identities[i]) ? &old->panes[i] : NULL;
        p->used = true; p->id = previous ? previous->id : allocate_id(s); pane_keys[i] = layout->identities[i];
        p->tab_count = view->count;
        if (view->count < 1 || view->count > TB_SETUP_VIEWS || view->active < 0 || view->active >= view->count) { return false; }
        for (int j = 0; j < view->count; ++j) { p->tabs[j] = (int)view->tabs[j]; }
        p->selected = (int)view->tabs[view->active];
        p->source_count = tb_code_tab_count(code, i) - 1;
        if (p->source_count >= TB_SETUP_SOURCES) { return false; }
        for (size_t j = 0; j < p->source_count; ++j) {
            const TbCodeFile *file = tb_code_tab_file(code, i, j + 1); if (!file) { return false; }
            TbSetupSource *f = &p->sources[j]; memcpy(f->file, file->file, sizeof f->file); memcpy(f->full, file->full, sizeof f->full);
            for (size_t k = 0; previous && k < previous->source_count; ++k) {
                if (strcmp(f->file, previous->sources[k].file) == 0 && strcmp(f->full, previous->sources[k].full) == 0) { f->id = previous->sources[k].id; break; }
            }
            if (!f->id) { f->id = allocate_id(s); }
            if (tb_code_tab_active(code, i) == j + 1) { p->source_selected = f->id; }
        }
        const TbMemoryView *m = tb_memory_view(memory, i); if (!m || !expressions[i] || strlen(expressions[i]) >= sizeof p->memory_expression) { return false; }
        memcpy(p->memory_expression, expressions[i], strlen(expressions[i]) + 1); p->memory_bytes = m->length; p->memory_group = m->grouping;
    }
    s->watch_count = tb_variables_definitions(variables);
    if (s->watch_count > TB_SETUP_WATCHES) { return false; }
    for (size_t i = 0; i < s->watch_count; ++i) {
        TbWatchDefinition w; if (!tb_variables_definition(variables, i, &w)) { return false; }
        size_t prior = c->current ? key_index(c->watches, c->current->watch_count, w.identity) : 0;
        s->watches[i].id = c->current && prior < c->current->watch_count ? c->current->watches[prior].id : allocate_id(s);
        memcpy(s->watches[i].expression, w.expression, sizeof w.expression); s->watches[i].format = (int)w.format; watch_keys[i] = w.identity;
    }
    s->break_count = tb_break_definition_count(breakpoints);
    if (s->break_count > TB_SETUP_BREAKS) { return false; }
    for (size_t i = 0; i < s->break_count; ++i) {
        const TbBreakDefinition *b = tb_break_definition(breakpoints, i); TbSetupBreak *d = &s->breaks[i]; if (!b) { return false; }
        size_t prior = c->current ? key_index(c->breaks, c->current->break_count, b->identity) : 0;
        d->id = c->current && prior < c->current->break_count ? c->current->breaks[prior].id : allocate_id(s);
        memcpy(d->location, b->location, sizeof d->location); memcpy(d->source, b->source, sizeof d->source); memcpy(d->condition, b->condition, sizeof d->condition);
        d->line = b->line; d->ignore = b->ignore; d->enabled = b->enabled; d->ignore_set = b->ignore_set; d->hardware = b->hardware; break_keys[i] = b->identity;
    }
    if (!tb_setup_valid(s)) { return false; }
    /* Allocate both work buffers before publishing the first draft. */
    if (!c->current) { c->current = malloc(sizeof *c->current); if (!c->current) { return false; } }
    TbSetup *swap = c->current; c->current = c->next; c->next = swap;
    memcpy(c->panes, pane_keys, sizeof c->panes); memcpy(c->watches, watch_keys, sizeof c->watches); memcpy(c->breaks, break_keys, sizeof c->breaks);
    return c->saved || tb_setup_capture_saved(c);
}
