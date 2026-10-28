#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "setup.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool text_valid(const char *text, size_t size, bool empty)
{
    size_t n = strnlen(text, size);
    if (n == size || (!n && !empty)) { return false; }
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32 || c == 127) { return false; }
        if (c < 128) { continue; }
        unsigned count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!count || n - i - 1 < count) { return false; }
        unsigned value = c & (0x7fU >> (count + 1));
        for (unsigned j = 0; j < count; ++j) {
            c = (unsigned char)text[++i]; if ((c & 0xc0U) != 0x80) { return false; }
            value = value * 64 + (c & 0x3fU);
        }
        if (value < (count == 1 ? 0xa0U : count == 2 ? 0x800U : 0x10000U) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) { return false; }
    }
    return true;
}
static bool tree(const TbSetup *s, int node, bool visited[TB_SETUP_NODES], bool panes[TB_SETUP_PANES])
{
    if (node < 0 || node >= TB_SETUP_NODES || visited[node] || !s->nodes[node].used) { return false; }
    visited[node] = true; const TbSetupNode *n = &s->nodes[node];
    if (n->pane >= 0) {
        if (n->pane >= TB_SETUP_PANES || panes[n->pane] || !s->panes[n->pane].used) { return false; }
        panes[n->pane] = true; return n->first == -1 && n->second == -1;
    }
    return n->pane == -1 && n->ratio > 0 && n->ratio < 10000 && n->direction >= 0 && n->direction <= 1 &&
           tree(s, n->first, visited, panes) && tree(s, n->second, visited, panes);
}
static bool identity(uint64_t id, uint64_t next, uint64_t ids[], size_t *count)
{
    if (!id || id >= next || id > INT64_MAX) { return false; }
    for (size_t i = 0; i < *count; ++i) { if (ids[i] == id) { return false; } }
    ids[(*count)++] = id; return true;
}
bool tb_setup_valid(const TbSetup *s)
{
    if (!s || !s->next_id || s->next_id > INT64_MAX || s->watch_count > TB_SETUP_WATCHES || s->break_count > TB_SETUP_BREAKS ||
        s->row_ratio <= 0 || s->row_ratio >= 10000) { return false; }
    int total = 0;
    for (size_t i = 0; i < 3; ++i) { if (s->columns[i] <= 0 || s->columns[i] >= 10000) { return false; } total += s->columns[i]; }
    if (total != 10000) { return false; }
    bool visited[TB_SETUP_NODES] = {0}, panes[TB_SETUP_PANES] = {0};
    for (size_t i = 0; i < 6; ++i) { if (!tree(s, s->roots[i], visited, panes)) { return false; } }
    for (size_t i = 0; i < TB_SETUP_NODES; ++i) { if (s->nodes[i].used != visited[i]) { return false; } }
    uint64_t ids[TB_SETUP_PANES * TB_SETUP_SOURCES + TB_SETUP_WATCHES + TB_SETUP_BREAKS]; size_t count = 0;
    for (size_t i = 0; i < TB_SETUP_PANES; ++i) {
        const TbSetupPane *p = &s->panes[i];
        if (p->used != panes[i]) { return false; }
        if (!p->used) { continue; }
        if (!identity(p->id, s->next_id, ids, &count) || p->tab_count < 1 || p->tab_count > TB_SETUP_VIEWS ||
            p->selected < 0 || p->selected >= TB_SETUP_VIEWS || p->source_count >= TB_SETUP_SOURCES ||
            !text_valid(p->memory_expression, sizeof p->memory_expression, true) ||
            !p->memory_bytes || p->memory_bytes > 1024 ||
            (p->memory_group != 1 && p->memory_group != 2 && p->memory_group != 4 && p->memory_group != 8)) { return false; }
        bool tabs[TB_SETUP_VIEWS] = {0};
        for (int j = 0; j < p->tab_count; ++j) {
            int tab = p->tabs[j]; if (tab < 0 || tab >= TB_SETUP_VIEWS || tabs[tab]) { return false; } tabs[tab] = true;
        }
        if (!tabs[p->selected]) { return false; }
        bool selected = !p->source_selected;
        for (size_t j = 0; j < p->source_count; ++j) {
            const TbSetupSource *f = &p->sources[j];
            if (!identity(f->id, s->next_id, ids, &count) || !text_valid(f->file, sizeof f->file, false) || !text_valid(f->full, sizeof f->full, true)) { return false; }
            selected = selected || p->source_selected == f->id;
            for (size_t k = 0; k < j; ++k) {
                if (strcmp(f->file, p->sources[k].file) == 0 && strcmp(f->full, p->sources[k].full) == 0) { return false; }
            }
        }
        if (!selected) { return false; }
    }
    for (size_t i = 0; i < s->watch_count; ++i) {
        const TbSetupWatch *w = &s->watches[i];
        if (!identity(w->id, s->next_id, ids, &count) || !text_valid(w->expression, sizeof w->expression, false) || w->format < 0 || w->format >= 5) { return false; }
    }
    for (size_t i = 0; i < s->break_count; ++i) {
        const TbSetupBreak *b = &s->breaks[i];
        if (!identity(b->id, s->next_id, ids, &count) || !text_valid(b->location, sizeof b->location, *b->source != 0) ||
            !text_valid(b->source, sizeof b->source, true) || !text_valid(b->condition, sizeof b->condition, true) ||
            ((*b->source != 0) != (b->line != 0)) || (!b->ignore_set && b->ignore)) { return false; }
    }
    return true;
}

/* The descriptor codec keeps integer bounds and mandatory field checking in one
 * place. Arrays and ownership are handled explicitly below. */
typedef enum { NUMBER, FLAG, STRING } Kind;
typedef struct { const char *name; size_t offset, size; Kind kind; uint64_t max; } Field;
#define NUM(T, n, m) {#n, offsetof(T, n), sizeof(((T *)0)->n), NUMBER, m}
#define STR(T, n) {#n, offsetof(T, n), sizeof(((T *)0)->n), STRING, 0}
#define BOOL(T, n) {#n, offsetof(T, n), sizeof(((T *)0)->n), FLAG, 0}
static const Field source_fields[] = {NUM(TbSetupSource, id, INT64_MAX), STR(TbSetupSource, file), STR(TbSetupSource, full)};
static const Field watch_fields[] = {NUM(TbSetupWatch, id, INT64_MAX), STR(TbSetupWatch, expression), NUM(TbSetupWatch, format, 4)};
static const Field break_fields[] = {NUM(TbSetupBreak, id, INT64_MAX), STR(TbSetupBreak, location), STR(TbSetupBreak, source), STR(TbSetupBreak, condition),
    NUM(TbSetupBreak, line, UINT_MAX), NUM(TbSetupBreak, ignore, UINT_MAX), BOOL(TbSetupBreak, enabled), BOOL(TbSetupBreak, ignore_set), BOOL(TbSetupBreak, hardware)};
static const Field pane_fields[] = {NUM(TbSetupPane, id, INT64_MAX), NUM(TbSetupPane, selected, TB_SETUP_VIEWS - 1), NUM(TbSetupPane, source_selected, INT64_MAX),
    STR(TbSetupPane, memory_expression), NUM(TbSetupPane, memory_bytes, 1024), NUM(TbSetupPane, memory_group, 8)};
static bool add(json_object *o, const char *name, json_object *v)
{
    if (!v) { return false; } if (json_object_object_add(o, name, v) == 0) { return true; } json_object_put(v); return false;
}
static bool append(json_object *a, json_object *v)
{
    if (!v) { return false; } if (json_object_array_add(a, v) == 0) { return true; } json_object_put(v); return false;
}
static json_object *encode_fields(const void *data, const Field *fields, size_t count)
{
    json_object *o = json_object_new_object(); if (!o) { return NULL; }
    for (size_t i = 0; i < count; ++i) {
        const Field *f = &fields[i]; const char *p = (const char *)data + f->offset; json_object *v;
        if (f->kind == STRING) { v = json_object_new_string(p); }
        else if (f->kind == FLAG) { v = json_object_new_boolean(*(const bool *)p); }
        else {
            uint64_t n = 0;
            if (f->size == sizeof(uint64_t)) { memcpy(&n, p, sizeof n); }
            else { unsigned value = 0; memcpy(&value, p, f->size); n = value; }
            v = json_object_new_int64((int64_t)n);
        }
        if (!add(o, f->name, v)) { json_object_put(o); return NULL; }
    }
    return o;
}
static bool decode_fields(json_object *o, void *data, const Field *fields, size_t count, size_t extra)
{
    if (!o || !json_object_is_type(o, json_type_object) || json_object_object_length(o) != (int)(count + extra)) { return false; }
    for (size_t i = 0; i < count; ++i) {
        const Field *f = &fields[i]; char *p = (char *)data + f->offset; json_object *v = NULL;
        if (!json_object_object_get_ex(o, f->name, &v)) { return false; }
        if (f->kind == STRING) {
            if (!json_object_is_type(v, json_type_string)) { return false; }
            const char *s = json_object_get_string(v); size_t n = (size_t)json_object_get_string_len(v);
            if (n >= f->size || strlen(s) != n || !text_valid(s, f->size, true)) { return false; } memcpy(p, s, n + 1);
        } else if (f->kind == FLAG) {
            if (!json_object_is_type(v, json_type_boolean)) { return false; } *(bool *)p = json_object_get_boolean(v) != 0;
        } else {
            if (!json_object_is_type(v, json_type_int) || json_object_get_int64(v) < 0 || json_object_get_uint64(v) > f->max) { return false; }
            uint64_t n = json_object_get_uint64(v);
            if (f->size == sizeof n) { memcpy(p, &n, sizeof n); }
            else { unsigned value = (unsigned)n; memcpy(p, &value, f->size); }
        }
    }
    return true;
}
static json_object *integers(const int *values, size_t count)
{
    json_object *a = json_object_new_array(); if (!a) { return NULL; }
    for (size_t i = 0; i < count; ++i) { if (!append(a, json_object_new_int(values[i]))) { json_object_put(a); return NULL; } }
    return a;
}
static json_object *array(json_object *o, const char *name, size_t maximum)
{
    json_object *a = NULL;
    return json_object_object_get_ex(o, name, &a) && json_object_is_type(a, json_type_array) && json_object_array_length(a) <= maximum ? a : NULL;
}
static bool get_integers(json_object *a, int *out, size_t count)
{
    if (!a || json_object_array_length(a) != count) { return false; }
    for (size_t i = 0; i < count; ++i) {
        json_object *v = json_object_array_get_idx(a, i);
        if (!json_object_is_type(v, json_type_int) || json_object_get_int64(v) < -1 || json_object_get_int64(v) > INT_MAX) { return false; }
        out[i] = json_object_get_int(v);
    }
    return true;
}
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
json_object *tb_setup_encode(const TbSetup *s)
{
    if (!tb_setup_valid(s)) { return NULL; }
    json_object *o = json_object_new_object(), *nodes = json_object_new_array(), *panes = json_object_new_array(),
                *watches = json_object_new_array(), *breaks = json_object_new_array();
    bool ok = o && nodes && panes && watches && breaks;
    for (size_t i = 0; ok && i < TB_SETUP_NODES; ++i) {
        const TbSetupNode *n = &s->nodes[i]; int values[6] = {0};
        if (n->used) {
            values[0] = 1; values[1] = n->pane; values[2] = n->first; values[3] = n->second;
            if (n->pane < 0) { values[4] = n->ratio; values[5] = n->direction; }
        }
        ok = append(nodes, integers(values, COUNT(values)));
    }
    for (size_t i = 0; ok && i < TB_SETUP_PANES; ++i) {
        const TbSetupPane *p = &s->panes[i];
        if (!p->used) { ok = json_object_array_add(panes, NULL) == 0; continue; }
        json_object *v = encode_fields(p, pane_fields, COUNT(pane_fields)), *sources = json_object_new_array();
        ok = v && sources;
        for (size_t j = 0; ok && j < p->source_count; ++j) { ok = append(sources, encode_fields(&p->sources[j], source_fields, COUNT(source_fields))); }
        if (ok) { ok = add(v, "sources", sources); sources = NULL; }
        if (ok) { ok = add(v, "tabs", integers(p->tabs, (size_t)p->tab_count)); }
        json_object_put(sources);
        if (ok) { ok = append(panes, v); } else { json_object_put(v); }
    }
    for (size_t i = 0; ok && i < s->watch_count; ++i) { ok = append(watches, encode_fields(&s->watches[i], watch_fields, COUNT(watch_fields))); }
    for (size_t i = 0; ok && i < s->break_count; ++i) { ok = append(breaks, encode_fields(&s->breaks[i], break_fields, COUNT(break_fields))); }
    ok = ok && add(o, "next_id", json_object_new_int64((int64_t)s->next_id)) && add(o, "row_ratio", json_object_new_int(s->row_ratio)) &&
         add(o, "columns", integers(s->columns, 3)) && add(o, "roots", integers(s->roots, 6));
    if (ok) { ok = add(o, "nodes", nodes); nodes = NULL; }
    if (ok) { ok = add(o, "panes", panes); panes = NULL; }
    if (ok) { ok = add(o, "watches", watches); watches = NULL; }
    if (ok) { ok = add(o, "breaks", breaks); breaks = NULL; }
    json_object_put(nodes); json_object_put(panes); json_object_put(watches); json_object_put(breaks);
    if (!ok) { json_object_put(o); return NULL; } return o;
}
TbSetup *tb_setup_decode(json_object *o)
{
    static const Field fields[] = {NUM(TbSetup, next_id, INT64_MAX), NUM(TbSetup, row_ratio, 9999)};
    /* Reject shallow malformed records before allocating a full bounded model. */
    if (!o || !json_object_is_type(o, json_type_object) || json_object_object_length(o) != 8 ||
        !array(o, "columns", 3) || !array(o, "roots", 6) || !array(o, "nodes", TB_SETUP_NODES) ||
        !array(o, "panes", TB_SETUP_PANES) || !array(o, "watches", TB_SETUP_WATCHES) || !array(o, "breaks", TB_SETUP_BREAKS)) { return NULL; }
    if (json_object_array_length(array(o, "columns", 3)) != 3 || json_object_array_length(array(o, "roots", 6)) != 6 ||
        json_object_array_length(array(o, "nodes", TB_SETUP_NODES)) != TB_SETUP_NODES ||
        json_object_array_length(array(o, "panes", TB_SETUP_PANES)) != TB_SETUP_PANES) { return NULL; }
    TbSetup *s = calloc(1, sizeof *s); if (!s) { return NULL; }
    bool ok = decode_fields(o, s, fields, COUNT(fields), 6) && get_integers(array(o, "columns", 3), s->columns, 3) &&
              get_integers(array(o, "roots", 6), s->roots, 6);
    json_object *nodes = ok ? array(o, "nodes", TB_SETUP_NODES) : NULL, *panes = ok ? array(o, "panes", TB_SETUP_PANES) : NULL;
    json_object *watches = ok ? array(o, "watches", TB_SETUP_WATCHES) : NULL, *breaks = ok ? array(o, "breaks", TB_SETUP_BREAKS) : NULL;
    ok = ok && nodes && panes && watches && breaks && json_object_array_length(nodes) == TB_SETUP_NODES && json_object_array_length(panes) == TB_SETUP_PANES;
    for (size_t i = 0; ok && i < TB_SETUP_NODES; ++i) {
        int v[6]; json_object *a = json_object_array_get_idx(nodes, i);
        ok = a && json_object_is_type(a, json_type_array) && get_integers(a, v, 6) && v[0] >= 0 && v[0] <= 1;
        if (ok) { s->nodes[i] = (TbSetupNode){v[0] != 0, v[1], v[2], v[3], v[4], v[5]}; }
    }
    for (size_t i = 0; ok && i < TB_SETUP_PANES; ++i) {
        TbSetupPane *p = &s->panes[i]; json_object *v = json_object_array_get_idx(panes, i); if (!v) { continue; }
        p->used = true; ok = decode_fields(v, p, pane_fields, COUNT(pane_fields), 2);
        json_object *sources = ok ? array(v, "sources", TB_SETUP_SOURCES - 1) : NULL, *tabs = ok ? array(v, "tabs", TB_SETUP_VIEWS) : NULL;
        ok = ok && sources && tabs;
        if (!ok) { break; }
        p->source_count = json_object_array_length(sources); p->tab_count = (int)json_object_array_length(tabs);
        ok = get_integers(tabs, p->tabs, (size_t)p->tab_count);
        for (size_t j = 0; ok && j < p->source_count; ++j) { ok = decode_fields(json_object_array_get_idx(sources, j), &p->sources[j], source_fields, COUNT(source_fields), 0); }
    }
    if (ok) {
        s->watch_count = json_object_array_length(watches); s->break_count = json_object_array_length(breaks);
        for (size_t i = 0; ok && i < s->watch_count; ++i) { ok = decode_fields(json_object_array_get_idx(watches, i), &s->watches[i], watch_fields, COUNT(watch_fields), 0); }
        for (size_t i = 0; ok && i < s->break_count; ++i) { ok = decode_fields(json_object_array_get_idx(breaks, i), &s->breaks[i], break_fields, COUNT(break_fields), 0); }
    }
    if (!ok || !tb_setup_valid(s)) { free(s); return NULL; } return s;
}
bool tb_setup_equal(const TbSetup *left, const TbSetup *right)
{
    json_object *a = tb_setup_encode(left), *b = tb_setup_encode(right);
    /* Allocator progress alone is not a changed workspace definition. */
    if (a) { json_object_object_del(a, "next_id"); }
    if (b) { json_object_object_del(b, "next_id"); }
    bool equal = a && b && json_object_equal(a, b); json_object_put(a); json_object_put(b); return equal;
}
