#include "variables.h"
#include "code.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VAR_NAMES = 9601, VAR_CREATE, VAR_CHILDREN, VAR_FORMAT, VAR_DELETE,
       VAR_PRUNE, OWNED_MAX = 128 };
typedef struct {
    TbVariable row;
    char object[256], expression[256], canonical[512], baseline[512], key[512];
    int parent;
    bool used, wanted, read, expand, evaluate, evaluate_explicit, failed, canonical_ok, baseline_ok;
    TbGdbGeneration sampled;
} Node;
typedef struct {
    Node nodes[TB_VARIABLE_NODES];
    int order[TB_VARIABLE_NODES]; size_t count;
    bool visible, names_ready, names_failed, limited;
    char error[192];
    struct { char key[512], type[192], value[512]; } previous[TB_VARIABLE_NODES];
    size_t previous_count;
} Tree;
typedef struct { char name[64]; bool retired, possible; } Owned;
struct TbVariables {
    TbGdb *gdb; TbContext *context;
    Tree trees[TB_VARIABLE_VIEWS];
    Owned owned[OWNED_MAX]; size_t owned_count;
    TbGdbGeneration generation, request_generation;
    char thread[32], function[160]; int frame;
    unsigned job; int view, node; size_t deletion;
    bool waiting, cleanup_failed;
    char prune[256];
    uint64_t serial, revision;
    TbVariableAuthorize authorize; void *authorize_user;
    TbVariableRegion regions[TB_MEMORY_REGIONS]; size_t region_count;
};
static bool same(TbGdbGeneration a, TbGdbGeneration b)
{ return a.session == b.session && a.stop == b.stop && a.context == b.context; }
static bool text(char *out, size_t size, const TbMiValue *value)
{
    out[0] = 0;
    if (!value || value->type != TB_MI_STRING) { return false; }
    tb_code_safe(out, size, value->string.data, value->string.length);
    return value->string.length < size && strlen(out) == value->string.length;
}
static unsigned number(const TbMiValue *v)
{
    unsigned n = 0;
    if (!v || v->type != TB_MI_STRING || !v->string.length) { return 0; }
    for (size_t i = 0; i < v->string.length; ++i) {
        unsigned char c = (unsigned char)v->string.data[i];
        if (c < '0' || c > '9' || n > (UINT_MAX - (unsigned)(c - '0')) / 10) { return UINT_MAX; }
        n = n * 10 + c - '0';
    }
    return n;
}
static void walk(Tree *t, int parent)
{
    for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
        Node *n = &t->nodes[i];
        if (!n->used || n->parent != parent) { continue; }
        t->order[t->count++] = i;
        if (n->row.expanded) { walk(t, i); }
    }
}
static void order(Tree *t) { t->count = 0; walk(t, -1); }
static Node *at(TbVariables *v, TbVariableView view, size_t row)
{
    if (view < 0 || view >= TB_VARIABLE_VIEWS || row >= v->trees[view].count) { return NULL; }
    return &v->trees[view].nodes[v->trees[view].order[row]];
}
static void children_remove(Tree *t, int parent)
{
    for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
        if (!t->nodes[i].used || t->nodes[i].parent != parent) { continue; }
        children_remove(t, i); memset(&t->nodes[i], 0, sizeof t->nodes[i]);
    }
    t->nodes[parent].row.expanded = false;
    t->nodes[parent].expand = false;
    order(t);
}
static int allocate(Tree *t, int parent)
{
    unsigned roots = 0;
    for (int i = 0; i < TB_VARIABLE_NODES; ++i) { roots += t->nodes[i].used && t->nodes[i].parent == -1; }
    if (parent < 0 && roots >= TB_VARIABLE_ROOTS) { t->limited = true; return -1; }
    for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
        if (t->nodes[i].used) { continue; }
        Node *n = &t->nodes[i]; memset(n, 0, sizeof *n);
        n->used = true; n->parent = parent;
        n->row.depth = parent < 0 ? 0 : t->nodes[parent].row.depth + 1;
        (void)snprintf(n->row.value, sizeof n->row.value, "Read required");
        return i;
    }
    t->limited = true; return -1;
}
static void retire(TbVariables *v, const char *object)
{
    for (size_t i = 0; i < v->owned_count; ++i) {
        if (strcmp(v->owned[i].name, object) == 0) { v->owned[i].retired = true; }
    }
}
static bool stopped(const TbVariables *v)
{
    const TbContextSnapshot *s = tb_context_snapshot(v->context);
    return tb_gdb_state(v->gdb) == TB_GDB_STOPPED && s->ready && !s->stale && same(v->generation, s->generation);
}
TbVariables *tb_variables_create(TbGdb *gdb, TbContext *context)
{
    TbVariables *v = calloc(1, sizeof *v);
    if (v) { v->gdb = gdb; v->context = context; v->frame = -1; }
    return v;
}
void tb_variables_destroy(TbVariables *v) { free(v); }
void tb_variables_reset(TbVariables *v)
{
    /* Session owner calls reset only after the previous GDB has exited. */
    TbGdb *gdb = v->gdb; TbContext *context = v->context;
    uint64_t revision = v->revision, serial = v->serial;
    TbVariableAuthorize authorize = v->authorize; void *user = v->authorize_user;
    memset(v, 0, sizeof *v); v->gdb = gdb; v->context = context; v->frame = -1;
    v->revision = revision + 1; v->serial = serial; v->authorize = authorize; v->authorize_user = user;
}
void tb_variables_authorizer(TbVariables *v, TbVariableAuthorize authorize, void *user)
{ v->authorize = authorize; v->authorize_user = user; }
void tb_variables_regions(TbVariables *v, const TbVariableRegion *regions, size_t count)
{
    v->region_count = 0;
    /* Truncated or malformed policy must not accidentally omit a deny overlay. */
    if (count > TB_MEMORY_REGIONS || (count && !regions)) { return; }
    for (size_t i = 0; i < count; ++i) {
        if (regions[i].end <= regions[i].begin || regions[i].kind < TB_VARIABLE_UNKNOWN ||
            regions[i].kind > TB_VARIABLE_SIDE_EFFECT) { return; }
    }
    if (count) { memcpy(v->regions, regions, count * sizeof *regions); }
    v->region_count = count;
}
void tb_variables_visible(TbVariables *v, bool locals, bool watches)
{
    v->trees[0].visible = locals; v->trees[1].visible = watches;
    tb_context_variables(v->context, locals || watches);
}
void tb_variables_begin(TbVariables *v)
{
    for (int j = 0; j < TB_VARIABLE_VIEWS; ++j) {
        for (int i = 0; i < TB_VARIABLE_NODES; ++i) { v->trees[j].nodes[i].wanted = false; }
    }
}
void tb_variables_want(TbVariables *v, TbVariableView view, size_t first, size_t count)
{
    if (view < 0 || view >= TB_VARIABLE_VIEWS) { return; }
    for (size_t i = first; count && i < v->trees[view].count; ++i, --count) {
        v->trees[view].nodes[v->trees[view].order[i]].wanted = true;
    }
}
size_t tb_variables_count(const TbVariables *v, TbVariableView view)
{ return view >= 0 && view < TB_VARIABLE_VIEWS ? v->trees[view].count : 0; }
const TbVariable *tb_variables_row(const TbVariables *v, TbVariableView view, size_t row)
{
    if (view < 0 || view >= TB_VARIABLE_VIEWS || row >= v->trees[view].count) { return NULL; }
    return &v->trees[view].nodes[v->trees[view].order[row]].row;
}
bool tb_variables_stale(const TbVariables *v, TbVariableView view)
{
    if (view < 0 || view >= TB_VARIABLE_VIEWS || !stopped(v)) { return true; }
    const Tree *t = &v->trees[view];
    for (size_t i = 0; i < t->count; ++i) {
        const Node *n = &t->nodes[t->order[i]];
        if (!n->row.fresh || n->row.format_pending || !same(n->sampled, v->generation)) { return true; }
    }
    return false;
}
const char *tb_variables_error(const TbVariables *v, TbVariableView view)
{ return view >= 0 && view < TB_VARIABLE_VIEWS ? v->trees[view].error : "Invalid variable view"; }
bool tb_variables_limited(const TbVariables *v, TbVariableView view)
{ return view >= 0 && view < TB_VARIABLE_VIEWS && v->trees[view].limited; }
const char *tb_variables_format_name(TbVariableFormat format)
{
    static const char *const names[] = {"Natural", "Hex", "Decimal", "Octal", "Binary"};
    return format >= 0 && format < TB_VARIABLE_FORMATS ? names[format] : "Unknown";
}
uint64_t tb_variables_revision(const TbVariables *v) { return v->revision; }
bool tb_variables_read(TbVariables *v, TbVariableView view, size_t row)
{
    Node *n = at(v, view, row);
    if (!n || !stopped(v) || v->waiting || *v->prune) { return false; }
    Tree *t = &v->trees[view];
    while (n->parent >= 0) { n = &t->nodes[n->parent]; }
    retire(v, n->object); n->object[0] = 0;
    children_remove(t, (int)(n - t->nodes));
    n->read = true; n->failed = false; n->row.fresh = false;
    t->error[0] = 0; v->cleanup_failed = false; ++v->revision; return true;
}
bool tb_variables_toggle(TbVariables *v, TbVariableView view, size_t row)
{
    Node *n = at(v, view, row);
    if (!n || !stopped(v) || v->waiting || *v->prune) { return false; }
    Tree *t = &v->trees[view];
    if (!n->row.fresh || !*n->object) {
        if (n->parent >= 0 || !tb_variables_read(v, view, row)) { return false; }
        n->expand = true; ++v->revision; return true;
    }
    if (!n->row.children || n->row.depth >= TB_VARIABLE_DEPTH) { return false; }
    t->error[0] = 0;
    if (n->row.expanded) {
        (void)snprintf(v->prune, sizeof v->prune, "%s", n->object);
        children_remove(t, (int)(n - t->nodes));
    } else { n->expand = true; n->failed = false; }
    ++v->revision; return true;
}
/* Row identifies the parent; direction is a signed paging action. */
bool tb_variables_page(TbVariables *v, TbVariableView view, size_t row, int direction) // NOLINT(bugprone-easily-swappable-parameters)
{
    Node *n = at(v, view, row);
    if (!n || !stopped(v) || v->waiting || *v->prune) { return false; }
    Tree *t = &v->trees[view];
    if (!n->row.expanded && n->parent >= 0) { n = &t->nodes[n->parent]; }
    if (!*n->object || !n->row.fresh || !n->row.children) { return false; }
    t->error[0] = 0;
    if (direction < 0) { if (!n->row.first) { return false; } n->row.first -= TB_VARIABLE_CHILDREN; }
    else {
        if (n->row.first > UINT_MAX - TB_VARIABLE_CHILDREN || n->row.first + TB_VARIABLE_CHILDREN >= n->row.children) { return false; }
        n->row.first += TB_VARIABLE_CHILDREN;
    }
    (void)snprintf(v->prune, sizeof v->prune, "%s", n->object);
    children_remove(t, (int)(n - t->nodes)); n->expand = true; n->failed = false;
    ++v->revision; return true;
}
bool tb_variables_format(TbVariables *v, TbVariableView view, size_t row)
{
    Node *n = at(v, view, row);
    if (!n || !stopped(v) || v->waiting || !n->row.fresh || !*n->object) { return false; }
    n->row.format = (TbVariableFormat)((n->row.format + 1) % TB_VARIABLE_FORMATS);
    v->trees[view].error[0] = 0;
    n->evaluate = n->evaluate_explicit = true; n->row.format_pending = true;
    n->failed = false; ++v->revision; return true;
}
bool tb_variables_watch(TbVariables *v, const char *expression)
{
    Tree *t = &v->trees[TB_VARIABLE_WATCHES];
    if (!tb_memory_expression(expression)) {
        (void)snprintf(t->error, sizeof t->error, "Use a name, member, constant index or simple offset; calls and mutations are rejected."); return false;
    }
    if (v->waiting) { (void)snprintf(t->error, sizeof t->error, "Wait for the current variable operation."); return false; }
    int i = allocate(t, -1); if (i < 0) { (void)snprintf(t->error, sizeof t->error, "Watch/node limit reached (64 roots, 256 rows); collapse children or remove a watch."); return false; }
    Node *n = &t->nodes[i]; (void)snprintf(n->row.name, sizeof n->row.name, "%s", expression);
    (void)snprintf(n->expression, sizeof n->expression, "%s", expression);
    (void)snprintf(n->key, sizeof n->key, "w:%llu", (unsigned long long)++v->serial);
    /* Adding a name does not grant a value read; Read is an explicit action. */
    order(t); t->error[0] = 0; ++v->revision; return true;
}
bool tb_variables_remove(TbVariables *v, size_t row)
{
    Node *n = at(v, TB_VARIABLE_WATCHES, row); Tree *t = &v->trees[TB_VARIABLE_WATCHES];
    if (!n || v->waiting) { return false; }
    while (n->parent >= 0) { n = &t->nodes[n->parent]; }
    retire(v, n->object); children_remove(t, (int)(n - t->nodes)); memset(n, 0, sizeof *n);
    order(t); ++v->revision; return true;
}
bool tb_variables_names(TbVariables *v)
{
    if (!stopped(v) || v->waiting) { return false; }
    v->trees[0].names_ready = v->trees[0].names_failed = false;
    v->trees[0].error[0] = 0; ++v->revision; return true;
}

static bool allowed(TbVariables *v, Node *n, TbVariableRead operation, bool explicit_action)
{
    /* Never discover a value in order to authorize discovering that value. */
    if (explicit_action) { return true; }
    TbVariableAccess access = {0};
    if (!v->authorize || !v->authorize(v->authorize_user, v->generation, n->expression,
        operation, n->row.first, operation == TB_VARIABLE_EXPAND ? TB_VARIABLE_CHILDREN : 1, &access) ||
        access.count > TB_VARIABLE_READ_SPANS) { return false; }
    uint64_t total = 0;
    for (size_t i = 0; i < access.count; ++i) {
        uint64_t begin = access.spans[i].address, bytes = access.spans[i].bytes;
        if (!bytes || bytes > TB_MEMORY_BYTES || begin > UINT64_MAX - bytes) { return false; }
        if (bytes > TB_MEMORY_BYTES - total) { return false; } total += bytes;
        uint64_t end = begin + bytes; bool ordinary = false;
        for (size_t j = 0; j < v->region_count; ++j) {
            const TbVariableRegion *region = &v->regions[j];
            if (begin >= region->end || end <= region->begin) { continue; }
            if (region->kind != TB_VARIABLE_ORDINARY) { return false; }
            if (begin >= region->begin && end <= region->end) { ordinary = true; }
        }
        if (!ordinary) { return false; }
    }
    return true;
}
static bool request(TbVariables *v, unsigned job, const char *op, const char *const *args, size_t count, TbGdbScope scope)
{
    v->job = job; v->request_generation = v->generation;
    v->waiting = tb_gdb_request(v->gdb, op, args, count, scope, job, 3000, NULL);
    return v->waiting;
}
static bool unavailable(const char *value)
{
    return *value == '<' && (strstr(value, "optimized out") || strstr(value, "unavailable") ||
                            strstr(value, "not available") || strstr(value, "error"));
}
static void publish(Node *n, const TbMiValue *values, TbGdbGeneration generation, bool canonical)
{
    const TbMiValue *value = tb_mi_find(values, "value");
    char buffer[512]; bool complete = text(buffer, sizeof buffer, value);
    if (canonical) {
        n->canonical_ok = complete && *buffer && !unavailable(buffer);
        n->row.changed = n->canonical_ok && n->baseline_ok && !n->row.children && strcmp(buffer, n->baseline) != 0;
        (void)snprintf(n->canonical, sizeof n->canonical, "%s", buffer);
    }
    n->row.available = (value || n->row.children) && !unavailable(buffer);
    (void)snprintf(n->row.value, sizeof n->row.value, "%s", value ? buffer : n->row.children ? "{...}" : "Unavailable");
    n->row.fresh = true; n->sampled = generation; n->row.limited = value && !complete;
}
static void catalog(TbVariables *v, const TbMiValue *list)
{
    Tree *t = &v->trees[0]; bool seen[TB_VARIABLE_NODES] = {false};
    t->limited = false;
    /* Reclaim vanished scopes before allocating new names, even at the cap. */
    for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
        Node *n = &t->nodes[i]; if (!n->used || n->parent >= 0) { continue; }
        bool found = false;
        for (const TbMiValue *item = list->children; item; item = item->next) {
            char name[256];
            if (item->type == TB_MI_TUPLE && text(name, sizeof name, tb_mi_find(item->children, "name")) &&
                strcmp(name, n->row.name) == 0) { found = true; break; }
        }
        if (!found) { retire(v, n->object); children_remove(t, i); memset(n, 0, sizeof *n); }
    }
    for (const TbMiValue *item = list->children; item; item = item->next) {
        if (item->type != TB_MI_TUPLE) { continue; }
        char name[256];
        if (!text(name, sizeof name, tb_mi_find(item->children, "name")) || !*name) { continue; }
        int index = -1;
        for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
            if (t->nodes[i].used && t->nodes[i].parent == -1 && strcmp(t->nodes[i].row.name, name) == 0) { index = i; break; }
        }
        if (index < 0) { index = allocate(t, -1); }
        if (index < 0) { break; }
        seen[index] = true; Node *n = &t->nodes[index];
        (void)snprintf(n->row.name, sizeof n->row.name, "%s", name);
        (void)snprintf(n->expression, sizeof n->expression, "%s", name);
        (void)snprintf(n->key, sizeof n->key, "l:%s", name);
        if (!tb_memory_expression(name)) { n->failed = true; (void)snprintf(n->row.value, sizeof n->row.value, "Unsupported compiler-generated name"); }
    }
    for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
        Node *n = &t->nodes[i];
        if (n->used && n->parent < 0 && !seen[i]) { children_remove(t, i); memset(n, 0, sizeof *n); }
    }
    order(t); t->names_ready = true;
}
bool tb_variables_event(TbVariables *v, const TbGdbEvent *event)
{
    if (event->cookie < VAR_NAMES || event->cookie > VAR_PRUNE ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    v->waiting = false;
    bool okay = event->record && strcmp(event->record->klass, "done") == 0 && event->kind == TB_GDB_REPLY;
    if (event->cookie == VAR_CREATE && event->record) {
        Node *created = &v->trees[v->view].nodes[v->node];
        for (size_t i = 0; i < v->owned_count; ++i) {
            if (strcmp(v->owned[i].name, created->object) == 0 && strcmp(event->record->klass, "done") == 0) { v->owned[i].possible = true; }
        }
    }
    if (event->cookie == VAR_CREATE && event->kind == TB_GDB_STALE && !event->record) {
        Node *created = &v->trees[v->view].nodes[v->node];
        for (size_t i = 0; i < v->owned_count; ++i) {
            if (strcmp(v->owned[i].name, created->object) == 0) { v->owned[i].possible = false; }
        }
    }
    if (event->cookie == VAR_DELETE) {
        /* An obsolete queued create may never have reached GDB. A structured
         * error deleting that unconfirmed name is not a leaked live object. */
        if (okay || (event->kind == TB_GDB_REPLY && !v->owned[v->deletion].possible)) {
            v->owned[v->deletion] = v->owned[--v->owned_count];
        }
        else { v->cleanup_failed = true; }
        ++v->revision; return true;
    }
    if (event->cookie == VAR_PRUNE) {
        v->prune[0] = 0;
        if (!okay) { v->cleanup_failed = true; }
        ++v->revision; return true;
    }
    if (!same(event->generation, v->generation) || !same(event->generation, tb_gdb_generation(v->gdb)) ||
        event->kind == TB_GDB_STALE || event->kind == TB_GDB_CANCELLED) { ++v->revision; return true; }
    Tree *t = &v->trees[v->view]; Node *n = &t->nodes[v->node];
    if (!okay) {
        (void)text(t->error, sizeof t->error, event->record ? tb_mi_find(event->record->results, "msg") : NULL);
        if (!*t->error) { (void)snprintf(t->error, sizeof t->error, "Variable request failed/timed out; Read retries."); }
        if (event->cookie == VAR_NAMES) { t->names_failed = true; }
        else {
            n->failed = true; n->read = n->expand = n->evaluate = false;
            char message[192]; memcpy(message, t->error, sizeof message);
            (void)snprintf(n->row.value, sizeof n->row.value, "Unavailable: %.190s", message);
            n->row.available = false;
            n->row.fresh = true; n->sampled = v->generation; n->row.changed = false;
            if (event->cookie == VAR_CREATE && event->kind == TB_GDB_REPLY) {
                for (size_t i = 0; i < v->owned_count; ++i) {
                    if (strcmp(v->owned[i].name, n->object) == 0) { v->owned[i] = v->owned[--v->owned_count]; break; }
                }
                n->object[0] = 0;
            }
        }
        ++v->revision; return true;
    }
    const TbMiValue *results = event->record->results;
    if (event->cookie == VAR_NAMES) {
        const TbMiValue *list = tb_mi_find(results, "variables");
        if (!list || list->type != TB_MI_LIST) { t->names_failed = true; (void)snprintf(t->error, sizeof t->error, "Malformed local-name list."); }
        else { catalog(v, list); }
    } else if (event->cookie == VAR_CREATE) {
        char name[256]; bool valid = text(name, sizeof name, tb_mi_find(results, "name")) && strcmp(name, n->object) == 0;
        if (!valid || tb_mi_find(results, "dynamic")) {
            n->failed = true; n->expand = false; retire(v, n->object); n->object[0] = 0;
            (void)snprintf(t->error, sizeof t->error, "Unexpected variable object; pretty-printer objects are unsupported.");
        } else {
            char type[192] = {0}; (void)text(type, sizeof type, tb_mi_find(results, "type"));
            if (strcmp(type, n->row.type) != 0) { n->baseline_ok = false; }
            memcpy(n->row.type, type, sizeof type);
            n->row.children = number(tb_mi_find(results, "numchild"));
            publish(n, results, v->generation, true);
            /* A format conversion is a separate value-producing operation. */
            n->row.format_pending = n->row.format != TB_VARIABLE_NATURAL;
            if (n->row.format_pending) { n->evaluate = true; n->evaluate_explicit = n->read; }
        }
        n->read = false;
    } else if (event->cookie == VAR_FORMAT) { publish(n, results, v->generation, false); n->evaluate = false; n->row.format_pending = false; }
    else {
        const TbMiValue *list = tb_mi_find(results, "children");
        n->expand = false;
        if (!list || list->type != TB_MI_LIST) {
            n->failed = true; (void)snprintf(t->error, sizeof t->error, "Malformed child list; collapse/read retries.");
        } else {
            unsigned count = 0;
            for (const TbMiValue *item = list->children; item; item = item->next) {
                if (count++ == TB_VARIABLE_CHILDREN) { t->limited = true; break; }
                if (item->type != TB_MI_TUPLE) { continue; }
                char id[256]; size_t prefix = strlen(n->object);
                if (!text(id, sizeof id, tb_mi_find(item->children, "name")) ||
                    strncmp(id, n->object, prefix) != 0 || id[prefix] != '.') { t->limited = true; continue; }
                int index = allocate(t, v->node); if (index < 0) { break; }
                Node *child = &t->nodes[index];
                (void)snprintf(child->object, sizeof child->object, "%s", id);
                (void)text(child->row.name, sizeof child->row.name, tb_mi_find(item->children, "exp"));
                (void)text(child->row.type, sizeof child->row.type, tb_mi_find(item->children, "type"));
                child->row.children = number(tb_mi_find(item->children, "numchild"));
                char parent_key[512], child_name[256]; memcpy(parent_key, n->key, sizeof parent_key);
                memcpy(child_name, child->row.name, sizeof child_name);
                int length = snprintf(child->key, sizeof child->key, "%s/%s", parent_key, child_name);
                if (length < 0 || (size_t)length >= sizeof child->key || !*parent_key) { child->key[0] = 0; child->row.limited = true; }
                for (size_t previous = 0; *child->key && previous < t->previous_count; ++previous) {
                    if (strcmp(child->key, t->previous[previous].key) == 0 && strcmp(child->row.type, t->previous[previous].type) == 0) {
                        memcpy(child->baseline, t->previous[previous].value, sizeof child->baseline); child->baseline_ok = true; break;
                    }
                }
                /* Child names are display data, never reconstructed C expressions. */
                publish(child, item->children, v->generation, true);
            }
            n->row.expanded = true; order(t);
        }
    }
    ++v->revision; return true;
}
static void invalidate(TbVariables *v, const TbContextSnapshot *s)
{
    bool identity = v->generation.session == s->generation.session && v->frame == s->selected.level &&
        strcmp(v->thread, s->selected_thread) == 0 && strcmp(v->function, s->selected.function) == 0;
    /* Generations reject obsolete requests; their numeric deltas do not
     * identify a scope. Native Continue/Step also invalidate the context. */
    bool new_stop = v->generation.stop != s->generation.stop;
    for (size_t i = 0; i < v->owned_count; ++i) { v->owned[i].retired = true; }
    v->prune[0] = 0;
    for (int j = 0; j < TB_VARIABLE_VIEWS; ++j) {
        Tree *t = &v->trees[j];
        if (!identity || new_stop) { t->previous_count = 0; }
        for (int i = 0; identity && new_stop && i < TB_VARIABLE_NODES; ++i) {
            Node *n = &t->nodes[i];
            if (!n->used || !n->canonical_ok || !*n->key || n->sampled.stop != v->generation.stop) { continue; }
            size_t previous = t->previous_count++;
            memcpy(t->previous[previous].key, n->key, sizeof n->key);
            memcpy(t->previous[previous].type, n->row.type, sizeof n->row.type);
            memcpy(t->previous[previous].value, n->canonical, sizeof n->canonical);
        }
        for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
            Node *n = &t->nodes[i]; if (!n->used) { continue; }
            if (n->parent < 0) { children_remove(t, i); }
            n->object[0] = 0; n->read = n->expand = n->evaluate = n->evaluate_explicit = n->failed = false;
            if (!identity) { n->baseline_ok = false; }
            else if (new_stop) {
                n->baseline_ok = n->canonical_ok && n->sampled.stop == v->generation.stop;
                if (n->baseline_ok) { memcpy(n->baseline, n->canonical, sizeof n->baseline); }
            }
            n->row.fresh = n->row.changed = n->row.format_pending = false;
            if (!identity) { n->canonical_ok = false; n->row.type[0] = 0; }
        }
        t->error[0] = 0; t->names_ready = j == TB_VARIABLE_WATCHES; t->names_failed = false; order(t);
    }
    v->generation = s->generation; v->frame = s->selected.level;
    (void)snprintf(v->thread, sizeof v->thread, "%s", s->selected_thread);
    (void)snprintf(v->function, sizeof v->function, "%s", s->selected.function);
    ++v->revision;
}
void tb_variables_pump(TbVariables *v)
{
    TbGdbState state = tb_gdb_state(v->gdb);
    /* Pending replies must not keep an old sample marked fresh after resume
     * or a context change. Keep object identities until completion so stale
     * create replies can still be retired correctly. */
    if (state == TB_GDB_RUNNING || !same(v->generation, tb_gdb_generation(v->gdb))) {
        bool changed = false;
        for (int j = 0; j < TB_VARIABLE_VIEWS; ++j) {
            for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
                TbVariable *row = &v->trees[j].nodes[i].row;
                if (row->fresh) { row->fresh = false; changed = true; }
            }
        }
        if (changed) { ++v->revision; }
    }
    if (v->waiting) { return; }
    if (state == TB_GDB_RUNNING || !same(v->generation, tb_gdb_generation(v->gdb))) {
        for (size_t i = 0; i < v->owned_count; ++i) { v->owned[i].retired = true; }
        for (int j = 0; j < TB_VARIABLE_VIEWS; ++j) {
            for (int i = 0; i < TB_VARIABLE_NODES; ++i) {
                Node *n = &v->trees[j].nodes[i]; n->object[0] = 0; n->read = n->expand = n->evaluate = false;
            }
        }
    }
    const TbContextSnapshot *s = tb_context_snapshot(v->context);
    if (state == TB_GDB_STOPPED && s->ready && !s->stale && !same(v->generation, s->generation)) { invalidate(v, s); }
    if (state != TB_GDB_READY && state != TB_GDB_RUNNING && state != TB_GDB_STOPPED) { return; }
    if (v->cleanup_failed) {
        for (int j = 0; j < TB_VARIABLE_VIEWS; ++j) {
            (void)snprintf(v->trees[j].error, sizeof v->trees[j].error, "Variable cleanup failed; Read retries, session exit releases all objects.");
        }
        return;
    }
    for (size_t i = 0; i < v->owned_count; ++i) {
        if (!v->owned[i].retired) { continue; }
        const char *args[] = {v->owned[i].name}; v->deletion = i;
        (void)request(v, VAR_DELETE, "-var-delete", args, 1, TB_GDB_SESSION); return;
    }
    if (*v->prune) {
        const char *args[] = {"-c", v->prune};
        (void)request(v, VAR_PRUNE, "-var-delete", args, 2, TB_GDB_SESSION); return;
    }
    if (!stopped(v)) { return; }
    char frame[16]; (void)snprintf(frame, sizeof frame, "%d", v->frame);
    if (v->trees[0].visible && !v->trees[0].names_ready && !v->trees[0].names_failed) {
        const char *args[] = {"--thread", v->thread, "--frame", frame, "--no-frame-filters", "--no-values"};
        v->view = 0; v->node = 0;
        (void)request(v, VAR_NAMES, "-stack-list-variables", args, 6, TB_GDB_CONTEXT); return;
    }
    for (int j = 0; j < TB_VARIABLE_VIEWS; ++j) {
        Tree *t = &v->trees[j]; if (!t->visible) { continue; }
        for (size_t row = 0; row < t->count; ++row) {
            int i = t->order[row]; Node *n = &t->nodes[i];
            if ((!n->wanted && !n->read && !n->expand && !n->evaluate) || n->failed) { continue; }
            v->view = j; v->node = i;
            if (!*n->object && n->parent < 0 && !n->row.fresh && allowed(v, n, TB_VARIABLE_CREATE, n->read)) {
                if (v->owned_count == OWNED_MAX) { (void)snprintf(t->error, sizeof t->error, "Variable-object ownership limit reached."); return; }
                char id[64]; (void)snprintf(id, sizeof id, "tbv%llu", (unsigned long long)++v->serial);
                const char *args[] = {"--thread", v->thread, "--frame", frame, "--language", "c", id, "*", n->expression};
                if (request(v, VAR_CREATE, "-var-create", args, 9, TB_GDB_CONTEXT)) {
                    (void)snprintf(n->object, sizeof n->object, "%s", id);
                    memset(&v->owned[v->owned_count], 0, sizeof v->owned[v->owned_count]);
                    v->owned[v->owned_count].possible = true;
                    (void)snprintf(v->owned[v->owned_count++].name, sizeof v->owned[0].name, "%s", id);
                }
                return;
            }
            if (!*n->object || !n->row.fresh) { continue; }
            if (n->evaluate && allowed(v, n, TB_VARIABLE_EVALUATE, n->evaluate_explicit)) {
                static const char *const formats[] = {"natural", "hexadecimal", "decimal", "octal", "binary"};
                const char *args[] = {"-f", formats[n->row.format], n->object};
                (void)request(v, VAR_FORMAT, "-var-evaluate-expression", args, 3, TB_GDB_CONTEXT); return;
            }
            if (n->expand && n->row.children && n->row.depth < TB_VARIABLE_DEPTH && allowed(v, n, TB_VARIABLE_EXPAND, true)) {
                char first[16], end[16]; unsigned upper = n->row.first > UINT_MAX - TB_VARIABLE_CHILDREN ? UINT_MAX : n->row.first + TB_VARIABLE_CHILDREN;
                if (upper > n->row.children) { upper = n->row.children; }
                (void)snprintf(first, sizeof first, "%u", n->row.first); (void)snprintf(end, sizeof end, "%u", upper);
                const char *args[] = {"--all-values", n->object, first, end};
                (void)request(v, VAR_CHILDREN, "-var-list-children", args, 4, TB_GDB_CONTEXT); return;
            }
            if (n->expand && (!n->row.children || n->row.depth >= TB_VARIABLE_DEPTH)) { n->expand = false; ++v->revision; }
        }
    }
}
