#include "variables.h"
#include "code.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VAR_NAMES = 9601, VAR_CREATE, VAR_CHILDREN, VAR_FORMAT, VAR_DELETE,
       VAR_PRUNE, VAR_EDIT_ATTRIBUTES, VAR_EDIT_PATH, VAR_EDIT_ADDRESS,
       VAR_EDIT_SIZE, VAR_EDIT_ASSIGN, OWNED_MAX = 128 };
typedef struct {
    TbVariable row;
    char object[256], expression[256], canonical[512], baseline[512], key[512];
    int parent;
    bool used, wanted, read, expand, evaluate, evaluate_explicit, failed, canonical_ok, baseline_ok;
    TbGdbGeneration sampled;
    uint64_t identity;
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
    TbVariableEditState edit_state;
    int edit_view, edit_node;
    unsigned edit_phase, edit_bytes, edit_bits;
    bool edit_signed, edit_bool, edit_complete, edit_unresolved, edit_dispatched, octets;
    char edit_object[256], edit_key[512], edit_thread[32];
    int edit_frame;
    uint64_t edit_address, regions_epoch, edit_regions_epoch;
    char edit_path[512], edit_value[128], edit_old[512], edit_type[192], edit_error[192];
    TbGdbGeneration edit_generation;
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
static bool edit_type(TbVariables *v, const char *type)
{
    /* Only builtin integer types with unambiguous signedness. Width comes from
     * the target sizeof reply, never the frontend ABI or a substring match. */
    if (!type) { return false; }
    while (!strncmp(type, "volatile ", 9)) { type += 9; }
    static const char *const signed_types[] = {"signed char", "short", "short int", "signed short", "signed short int",
        "int", "signed", "signed int", "long", "long int", "signed long", "signed long int",
        "long long", "long long int", "signed long long", "signed long long int"};
    static const char *const unsigned_types[] = {"unsigned char", "unsigned short", "unsigned short int", "unsigned", "unsigned int",
        "unsigned long", "unsigned long int", "unsigned long long", "unsigned long long int"};
    v->edit_bool = !strcmp(type, "bool") || !strcmp(type, "_Bool"); v->edit_bits = 0;
    if (v->edit_bool) { v->edit_signed = false; return true; }
    for (size_t i = 0; i < sizeof signed_types / sizeof *signed_types; ++i) {
        if (!strcmp(type, signed_types[i])) { v->edit_signed = true; return true; }
    }
    for (size_t i = 0; i < sizeof unsigned_types / sizeof *unsigned_types; ++i) {
        if (!strcmp(type, unsigned_types[i])) { v->edit_signed = false; return true; }
    }
    return false;
}
static bool edit_path_safe(const char *path)
{
    /* GDB may parenthesize a fixed array/member path. Pointer dereferences,
     * operators, casts and calls do not establish a fixed destination. */
    char plain[512]; size_t used = 0; int depth = 0;
    for (const char *p = path; *p; ++p) {
        if (*p == '(') {
            if (used && (isalnum((unsigned char)plain[used - 1]) || plain[used - 1] == '_' || plain[used - 1] == ']')) { return false; }
            ++depth; continue;
        }
        if (*p == ')') { if (--depth < 0) { return false; } continue; }
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '.' || *p == '[' || *p == ']')) { return false; }
        if (used + 1 == sizeof plain) { return false; }
        plain[used++] = *p;
    }
    plain[used] = 0;
    return depth == 0 && used && plain[0] != '[' && tb_memory_expression(plain);
}
static bool decimal_value(const char *input, bool is_bool, bool is_signed, unsigned bits, char *out, size_t capacity)
{
    if (!input || !*input || !bits || bits > 64) { return false; }
    while (isspace((unsigned char)*input)) { ++input; }
    size_t length = strlen(input);
    while (length && isspace((unsigned char)input[length - 1])) { --length; }
    if (!length || length >= 128) { return false; }
    char copy[128]; memcpy(copy, input, length); copy[length] = 0;
    if (is_bool) {
        if (!strcmp(copy, "true") || !strcmp(copy, "1")) { (void)snprintf(out, capacity, "1"); return true; }
        if (!strcmp(copy, "false") || !strcmp(copy, "0")) { (void)snprintf(out, capacity, "0"); return true; }
        return false;
    }
    char *end = NULL; errno = 0;
    if (is_signed) {
        long long value = strtoll(copy, &end, 0);
        if (errno || end == copy || *end || (bits < 64 &&
            (value < -(long long)(UINT64_C(1) << (bits - 1)) || value >= (long long)(UINT64_C(1) << (bits - 1))))) { return false; }
        (void)snprintf(out, capacity, "%lld", value);
    } else {
        if (*copy == '-') { return false; }
        unsigned long long value = strtoull(copy, &end, 0);
        if (errno || end == copy || *end || (bits < 64 && value > (1ULL << bits) - 1ULL)) { return false; }
        (void)snprintf(out, capacity, "%llu", value);
    }
    return true;
}
static bool mi_number(const TbMiValue *value, uint64_t *out)
{
    if (!value || value->type != TB_MI_STRING || !value->string.length) { return false; }
    char text[96];
    if (value->string.length >= sizeof text) { return false; }
    memcpy(text, value->string.data, value->string.length); text[value->string.length] = 0;
    char *end = NULL; errno = 0; unsigned long long number = strtoull(text, &end, 0);
    if (errno || end == text || *end || number > UINT64_MAX) { return false; }
    *out = (uint64_t)number; return true;
}
TbVariables *tb_variables_create(TbGdb *gdb, TbContext *context)
{
    TbVariables *v = calloc(1, sizeof *v);
    if (v) { v->gdb = gdb; v->context = context; v->frame = -1; v->edit_view = v->edit_node = -1; }
    return v;
}
void tb_variables_destroy(TbVariables *v) { free(v); }
void tb_variables_reset(TbVariables *v)
{
    /* Session owner calls reset only after the previous GDB has exited. */
    TbGdb *gdb = v->gdb; TbContext *context = v->context;
    uint64_t revision = v->revision, serial = v->serial;
    TbVariableAuthorize authorize = v->authorize; void *user = v->authorize_user;
    memset(v, 0, sizeof *v); v->gdb = gdb; v->context = context; v->frame = -1; v->edit_view = v->edit_node = -1;
    v->revision = revision + 1; v->serial = serial; v->authorize = authorize; v->authorize_user = user;
}
void tb_variables_authorizer(TbVariables *v, TbVariableAuthorize authorize, void *user)
{ v->authorize = authorize; v->authorize_user = user; }
void tb_variables_target(TbVariables *v, bool octets) { v->octets = octets; }
void tb_variables_regions(TbVariables *v, const TbVariableRegion *regions, size_t count)
{
    ++v->regions_epoch;
    if (v->edit_state == TB_VARIABLE_EDIT_PENDING && v->edit_dispatched) { tb_gdb_context_changed(v->gdb); }
    if (v->edit_state == TB_VARIABLE_EDIT_READY || v->edit_state == TB_VARIABLE_EDIT_PROBING ||
        (v->edit_state == TB_VARIABLE_EDIT_PENDING && !v->edit_dispatched)) {
        v->edit_state = TB_VARIABLE_EDIT_NONE; v->edit_phase = 0;
        (void)snprintf(v->edit_error, sizeof v->edit_error, "RAM evidence changed; destination proof expired. Review a new edit.");
    }
    ++v->revision;
    v->region_count = 0;
    /* Truncated or malformed policy must not accidentally omit a deny overlay. */
    if (count > TB_MEMORY_REGIONS || (count && !regions)) { return; }
    for (size_t i = 0; i < count; ++i) {
        if (regions[i].end <= regions[i].begin || regions[i].kind < TB_VARIABLE_UNKNOWN ||
            regions[i].kind > TB_VARIABLE_SIDE_EFFECT) { return; }
        for (size_t j = 0; j < i; ++j) {
            if (regions[i].kind == TB_VARIABLE_ORDINARY && regions[j].kind == TB_VARIABLE_ORDINARY &&
                regions[i].begin < regions[j].end && regions[j].begin < regions[i].end &&
                regions[i].writable != regions[j].writable) { return; }
        }
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
bool tb_variables_settled(const TbVariables *v, TbVariableView view)
{
    if (view < 0 || view >= TB_VARIABLE_VIEWS || !stopped(v)) { return false; }
    const Tree *t = &v->trees[view];
    return (v->cleanup_failed || *t->error || view != TB_VARIABLE_LOCALS || t->names_ready || t->names_failed) &&
        (!v->waiting || v->view != (int)view);
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
    if (!tb_memory_expression(n->expression)) {
        (void)snprintf(t->error, sizeof t->error, "Unsupported saved expression; remove it and add a supported Watch."); return false;
    }
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
    if (!n->row.fresh || !*n->object) { return false; }
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
static bool watch_definition(TbVariables *v, const char *expression, bool restored)
{
    Tree *t = &v->trees[TB_VARIABLE_WATCHES];
    if (!expression || !*expression || strlen(expression) >= sizeof t->nodes[0].expression || (!restored && !tb_memory_expression(expression))) {
        (void)snprintf(t->error, sizeof t->error, "Use a name, member, constant index or simple offset; calls and mutations are rejected."); return false;
    }
    if (v->waiting) { (void)snprintf(t->error, sizeof t->error, "Wait for the current variable operation."); return false; }
    int i = allocate(t, -1); if (i < 0) { (void)snprintf(t->error, sizeof t->error, "Watch/node limit reached (64 roots, 256 rows); collapse children or remove a watch."); return false; }
    Node *n = &t->nodes[i]; (void)snprintf(n->row.name, sizeof n->row.name, "%s", expression);
    (void)snprintf(n->expression, sizeof n->expression, "%s", expression);
    n->identity = ++v->serial;
    (void)snprintf(n->key, sizeof n->key, "w:%llu", (unsigned long long)n->identity);
    /* Adding a name does not grant a value read; Read is an explicit action. */
    order(t); t->error[0] = 0; ++v->revision; return true;
}
bool tb_variables_watch(TbVariables *v, const char *expression) { return watch_definition(v, expression, false); }
bool tb_variables_restore_watch(TbVariables *v, const char *expression, TbVariableFormat format)
{
    if (format < 0 || format >= TB_VARIABLE_FORMATS || !watch_definition(v, expression, true)) { return false; }
    Tree *t = &v->trees[TB_VARIABLE_WATCHES];
    for (size_t i = 0; i < TB_VARIABLE_NODES; ++i) {
        Node *n = &t->nodes[i];
        if (n->used && n->parent < 0 && n->identity == v->serial) { n->row.format = format; return true; }
    }
    return false;
}
size_t tb_variables_definitions(const TbVariables *v)
{
    size_t count = 0; const Tree *t = &v->trees[TB_VARIABLE_WATCHES];
    for (size_t i = 0; i < t->count; ++i) { count += t->nodes[t->order[i]].parent < 0; }
    return count;
}
bool tb_variables_definition(const TbVariables *v, size_t index, TbWatchDefinition *out)
{
    const Tree *t = &v->trees[TB_VARIABLE_WATCHES];
    for (size_t i = 0; i < t->count; ++i) {
        const Node *n = &t->nodes[t->order[i]];
        if (n->parent >= 0) { continue; }
        if (index--) { continue; }
        out->identity = n->identity; out->format = n->row.format;
        memcpy(out->expression, n->expression, sizeof out->expression); return true;
    }
    return false;
}
bool tb_variables_remove_definition(TbVariables *v, uint64_t identity)
{
    Tree *t = &v->trees[TB_VARIABLE_WATCHES];
    for (size_t row = 0; row < t->count; ++row) {
        const Node *n = &t->nodes[t->order[row]];
        if (n->parent < 0 && n->identity == identity) { return tb_variables_remove(v, row); }
    }
    return false;
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
/* Direct lvalues only: resolving a pointer path can itself read unsafe storage. */
static bool direct_lvalue(const Tree *tree, const Node *node)
{
    for (;;) {
        if (strchr(node->row.type, '*') || strchr(node->row.type, '&')) { return false; }
        if (node->parent < 0) { break; }
        node = &tree->nodes[node->parent];
    }
    const unsigned char *p = (const unsigned char *)node->expression;
    if (!(*p == '_' || isalpha(*p))) { return false; }
    for (++p; *p; ++p) { if (*p != '_' && !isalnum(*p)) { return false; } }
    return true;
}
bool tb_variables_edit_begin(TbVariables *v, TbVariableView view, size_t row)
{
    Node *n = at(v, view, row);
    if (!n || !v->octets || !stopped(v) || v->waiting || tb_context_write_blocked(v->context) ||
        v->edit_state == TB_VARIABLE_EDIT_PROBING || v->edit_state == TB_VARIABLE_EDIT_READY ||
        v->edit_state == TB_VARIABLE_EDIT_PENDING || v->edit_state == TB_VARIABLE_EDIT_UNCERTAIN ||
        !n->row.fresh || !*n->object || n->row.children || !direct_lvalue(&v->trees[view], n) ||
        !edit_type(v, n->row.type)) {
        (void)snprintf(v->edit_error, sizeof v->edit_error,
            v->edit_state == TB_VARIABLE_EDIT_UNCERTAIN ? "Previous write is uncertain; explicitly verify and reconcile first." :
            "Selected variable has no supported complete edit proof.");
        return false;
    }
    (void)snprintf(v->edit_object, sizeof v->edit_object, "%s", n->object);
    (void)snprintf(v->edit_key, sizeof v->edit_key, "%s", n->key);
    (void)snprintf(v->edit_thread, sizeof v->edit_thread, "%s", v->thread); v->edit_frame = v->frame;
    v->edit_view = view; v->edit_node = (int)(n - v->trees[view].nodes);
    v->edit_regions_epoch = v->regions_epoch;
    v->edit_generation = v->generation; v->edit_state = TB_VARIABLE_EDIT_PROBING;
    v->edit_phase = VAR_EDIT_ATTRIBUTES; v->edit_address = 0; v->edit_bytes = 0;
    v->edit_path[0] = v->edit_value[0] = v->edit_error[0] = 0;
    (void)snprintf(v->edit_old, sizeof v->edit_old, "%s", n->row.value);
    (void)snprintf(v->edit_type, sizeof v->edit_type, "%s", n->row.type);
    ++v->revision; return true;
}
bool tb_variables_edit_apply(TbVariables *v, const char *value)
{
    if (v->edit_state != TB_VARIABLE_EDIT_READY || v->edit_regions_epoch != v->regions_epoch || !stopped(v) ||
        !same(v->edit_generation, tb_gdb_generation(v->gdb))) {
        (void)snprintf(v->edit_error, sizeof v->edit_error, "Edit proof expired; reopen the edit review.");
        return false;
    }
    char canonical[sizeof v->edit_value];
    if (!decimal_value(value, v->edit_bool, v->edit_signed, v->edit_bits, canonical, sizeof canonical)) {
        (void)snprintf(v->edit_error, sizeof v->edit_error, "Value is outside the supported integer/bool range.");
        return false;
    }
    (void)snprintf(v->edit_value, sizeof v->edit_value, "%s", canonical);
    v->edit_phase = VAR_EDIT_ASSIGN; v->edit_state = TB_VARIABLE_EDIT_PENDING; v->edit_dispatched = false;
    ++v->revision; return true;
}
void tb_variables_edit_cancel(TbVariables *v)
{
    if (v->edit_state == TB_VARIABLE_EDIT_UNCERTAIN ||
        (v->edit_state == TB_VARIABLE_EDIT_PENDING && v->edit_dispatched)) { return; }
    v->edit_state = TB_VARIABLE_EDIT_NONE; v->edit_phase = 0; v->edit_error[0] = 0;
    ++v->revision;
}
bool tb_variables_edit_reconcile(TbVariables *v)
{
    Node *n = v->edit_view >= 0 && v->edit_view < TB_VARIABLE_VIEWS &&
        v->edit_node >= 0 && v->edit_node < TB_VARIABLE_NODES ?
        &v->trees[v->edit_view].nodes[v->edit_node] : NULL;
    if (v->edit_state != TB_VARIABLE_EDIT_UNCERTAIN || !n || !n->row.fresh ||
        tb_gdb_pending(v->gdb) || !stopped(v) ||
        strcmp(n->key, v->edit_key) != 0 || strcmp(n->row.type, v->edit_type) != 0 ||
        v->edit_generation.session != v->generation.session || v->edit_generation.stop != v->generation.stop ||
        v->edit_frame != v->frame || strcmp(v->edit_thread, v->thread) != 0 ||
        !tb_context_write_reconcile(v->context, VAR_EDIT_ASSIGN)) { return false; }
    v->edit_state = TB_VARIABLE_EDIT_NONE; v->edit_phase = 0;
    (void)snprintf(v->edit_error, sizeof v->edit_error, "Reconciled after explicit verification; review a new edit.");
    ++v->revision; return true;
}
TbVariableEditState tb_variables_edit_state(const TbVariables *v) { return v->edit_state; }
const char *tb_variables_edit_error(const TbVariables *v) { return v->edit_error; }
const char *tb_variables_edit_old_value(const TbVariables *v) { return v->edit_old; }
const char *tb_variables_edit_type(const TbVariables *v) { return v->edit_type; }

static bool allowed(TbVariables *v, Node *n, TbVariableRead operation, bool explicit_action)
{
    /* Never discover a value in order to authorize discovering that value. */
    if (explicit_action) { return true; }
    if (tb_context_written(v->context)) { return false; }
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
static bool writable_destination(const TbVariables *v, uint64_t address, unsigned bytes)
{
    if (!bytes || address > UINT64_MAX - bytes) { return false; }
    uint64_t end = address + bytes; bool contained = false;
    for (size_t i = 0; i < v->region_count; ++i) {
        const TbVariableRegion *region = &v->regions[i];
        if (address >= region->end || end <= region->begin) { continue; }
        if (region->kind != TB_VARIABLE_ORDINARY || !region->writable) { return false; }
        if (address >= region->begin && end <= region->end) { contained = true; }
    }
    return contained;
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
    if (event->cookie < VAR_NAMES || event->cookie > VAR_EDIT_ASSIGN ||
        !(event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE || event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED)) { return false; }
    v->waiting = false;
    bool okay = event->record && strcmp(event->record->klass, "done") == 0 && event->kind == TB_GDB_REPLY;
    if (event->cookie >= VAR_EDIT_ATTRIBUTES && event->cookie <= VAR_EDIT_ASSIGN) {
        bool assignment = event->cookie == VAR_EDIT_ASSIGN;
        if (assignment) {
            v->edit_complete = true;
            v->edit_unresolved = !event->record ||
                (strcmp(event->record->klass, "done") != 0 && strcmp(event->record->klass, "error") != 0);
        }
        if (!event->record || v->edit_regions_epoch != v->regions_epoch || !same(event->generation, v->edit_generation) ||
            !same(event->generation, tb_gdb_generation(v->gdb))) {
            if (assignment) {
                v->edit_phase = 0;
                v->edit_state = TB_VARIABLE_EDIT_UNCERTAIN;
                (void)snprintf(v->edit_error, sizeof v->edit_error,
                    v->edit_unresolved ? "Write completion unresolved; reconnect before editing again." :
                    "Write completion is uncertain; verify explicitly before reconciliation.");
            } else {
                v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "Edit proof expired; reopen the edit review.");
            }
            ++v->revision; return true;
        }
        if (!okay) {
            char message[192] = "Edit request failed; destination or backend state is unknown.";
            if (event->record) {
                const TbMiValue *msg = tb_mi_find(event->record->results, "msg");
                if (msg && msg->type == TB_MI_STRING) { tb_code_safe(message, sizeof message, msg->string.data, msg->string.length); }
            }
            if (assignment) {
                v->edit_phase = 0;
                v->edit_state = TB_VARIABLE_EDIT_UNCERTAIN;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "UNCERTAIN: %.150s Verify before retrying.", message);
                Node *n = &v->trees[v->edit_view].nodes[v->edit_node];
                n->row.fresh = false; (void)snprintf(n->row.value, sizeof n->row.value, "Write uncertain; Verify required");
            } else {
                v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "%s", message);
            }
            ++v->revision; return true;
        }
        const TbMiValue *value = tb_mi_find(event->record->results, "value");
        if (event->cookie == VAR_EDIT_ATTRIBUTES) {
            char attr[32];
            if (!text(attr, sizeof attr, tb_mi_find(event->record->results, "attr")) || strcmp(attr, "editable") != 0) {
                v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "GDB does not report this variable as editable.");
            } else { v->edit_phase = VAR_EDIT_PATH; }
        } else if (event->cookie == VAR_EDIT_PATH) {
            if (!text(v->edit_path, sizeof v->edit_path, tb_mi_find(event->record->results, "path_expr")) || !edit_path_safe(v->edit_path)) {
                v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "Complete write path is unavailable for this variable.");
            } else { v->edit_phase = VAR_EDIT_ADDRESS; }
        } else if (event->cookie == VAR_EDIT_ADDRESS) {
            if (!mi_number(value, &v->edit_address)) {
                v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "Variable destination address is unavailable.");
            } else { v->edit_phase = VAR_EDIT_SIZE; }
        } else if (event->cookie == VAR_EDIT_SIZE) {
            uint64_t bytes;
            if (!mi_number(value, &bytes) || !v->octets || !bytes || bytes > 8 ||
                !writable_destination(v, v->edit_address, (unsigned)bytes)) {
                v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error,
                    "Destination lacks complete proof of ordinary writable storage.");
            } else {
                v->edit_bytes = (unsigned)bytes; v->edit_bits = (unsigned)bytes * 8;
                v->edit_state = TB_VARIABLE_EDIT_READY;
            }
        } else {
            Node *n = &v->trees[v->edit_view].nodes[v->edit_node];
            n->row.fresh = false; (void)snprintf(n->row.value, sizeof n->row.value, "Backend acknowledged; Verify");
            v->edit_phase = 0;
            v->edit_state = TB_VARIABLE_EDIT_ACKNOWLEDGED;
            (void)snprintf(v->edit_error, sizeof v->edit_error, "Backend acknowledged; Verify explicitly to establish the current value.");
        }
        ++v->revision; return true;
    }
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
    if (v->edit_state != TB_VARIABLE_EDIT_UNCERTAIN && v->edit_state != TB_VARIABLE_EDIT_ACKNOWLEDGED &&
        v->edit_state != TB_VARIABLE_EDIT_NONE) {
        v->edit_state = TB_VARIABLE_EDIT_NONE; v->edit_phase = 0;
        (void)snprintf(v->edit_error, sizeof v->edit_error, "Context changed; edit draft discarded.");
    }
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
    if (v->edit_complete) {
        v->edit_complete = false;
        tb_context_write_finish(v->context, VAR_EDIT_ASSIGN, v->edit_state != TB_VARIABLE_EDIT_UNCERTAIN ? TB_WRITE_ACKNOWLEDGED :
            v->edit_unresolved ? TB_WRITE_UNRESOLVED : TB_WRITE_UNCERTAIN);
    }
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
    if (v->edit_state == TB_VARIABLE_EDIT_PROBING || v->edit_phase == VAR_EDIT_ASSIGN) {
        Node *n = &v->trees[v->edit_view].nodes[v->edit_node];
        char expression[640];
        const char *args[4]; size_t count = 0;
        if (v->edit_phase == VAR_EDIT_ATTRIBUTES) {
            args[0] = n->object; count = 1;
            (void)request(v, VAR_EDIT_ATTRIBUTES, "-var-show-attributes", args, count, TB_GDB_CONTEXT);
        } else if (v->edit_phase == VAR_EDIT_PATH) {
            args[0] = n->object; count = 1;
            (void)request(v, VAR_EDIT_PATH, "-var-info-path-expression", args, count, TB_GDB_CONTEXT);
        } else if (v->edit_phase == VAR_EDIT_ADDRESS) {
            (void)snprintf(expression, sizeof expression, "(unsigned long long)&(%s)", v->edit_path);
            args[0] = "--language"; args[1] = "c"; args[2] = expression; count = 3;
            (void)request(v, VAR_EDIT_ADDRESS, "-data-evaluate-expression", args, count, TB_GDB_CONTEXT);
        } else if (v->edit_phase == VAR_EDIT_SIZE) {
            (void)snprintf(expression, sizeof expression, "sizeof(%s)", v->edit_path);
            args[0] = "--language"; args[1] = "c"; args[2] = expression; count = 3;
            (void)request(v, VAR_EDIT_SIZE, "-data-evaluate-expression", args, count, TB_GDB_CONTEXT);
        } else if (v->edit_phase == VAR_EDIT_ASSIGN) {
            if (!n->used || !n->row.fresh || strcmp(n->object, v->edit_object) != 0 || strcmp(n->key, v->edit_key) != 0 ||
                !v->octets || v->edit_regions_epoch != v->regions_epoch || !same(v->edit_generation, v->generation) ||
                !writable_destination(v, v->edit_address, v->edit_bytes)) {
                v->edit_phase = 0; v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "Write destination or context changed; review a new edit.");
                ++v->revision; return;
            }
            if (tb_context_write_blocked(v->context)) {
                v->edit_phase = 0; v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "Another write requires explicit reconciliation first.");
                ++v->revision; return;
            }
            if (!tb_context_write_begin(v->context, VAR_EDIT_ASSIGN)) { return; }
            args[0] = v->edit_object; args[1] = v->edit_value; count = 2;
            if (!request(v, VAR_EDIT_ASSIGN, "-var-assign", args, count, TB_GDB_CONTEXT)) {
                tb_context_write_finish(v->context, VAR_EDIT_ASSIGN, TB_WRITE_ACKNOWLEDGED);
                v->edit_phase = 0; v->edit_state = TB_VARIABLE_EDIT_NONE;
                (void)snprintf(v->edit_error, sizeof v->edit_error, "Write request rejected; no automatic retry.");
                ++v->revision;
            } else { v->edit_dispatched = true; }
        }
        return;
    }
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
            if (n->parent < 0 && !tb_memory_expression(n->expression)) {
                (void)snprintf(n->row.value, sizeof n->row.value, "Unsupported saved expression"); continue;
            }
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
