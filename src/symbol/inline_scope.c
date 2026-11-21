#include "symbol/inline_scope.h"

static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.key.device == right.key.device && left.key.inode == right.key.inode &&
           left.bytes == right.bytes;
}


static TbSymbolReferenceTarget_t record(const TbSymbolInlineScope_t *scope, uint64_t index)
{
    return (TbSymbolReferenceTarget_t){
        .backing       = scope->tree.backing,
        .section_index = scope->tree.section_index,
        .unit_offset   = scope->tree.unit_offset,
        .record_offset = scope->tree.entries[index].offset,
        .entry_index   = index
    };
}


static uint64_t root_tag(TbSymbolDwarfUnitKind_e kind)
{
    switch (kind) {
    case TB_SYMBOL_DWARF_COMPILE:
        return 0x11;
    case TB_SYMBOL_DWARF_PARTIAL:
        return 0x3c;
    case TB_SYMBOL_DWARF_TYPE:
        return 0x41;
    default:
        return 0;
    }
}


TbSymbolInlineScopeStatus_e tb_symbol_inline_scope_init(TbSymbolInlineScope_t *scope,
                                                        TbSymbolUnitsView_t units,
                                                        TbSymbolDiesView_t tree,
                                                        uint64_t unit_index, uint64_t die_index,
                                                        TbSymbolInlineScopeFact_t *facts,
                                                        size_t capacity)
{
    if (!scope || !units.entries || !tree.entries || !tree.count || !facts ||
        units.count > SIZE_MAX / sizeof(*units.entries) ||
        tree.count > SIZE_MAX / sizeof(*tree.entries) ||
        tree.attribute_count > SIZE_MAX / sizeof(*tree.attributes) ||
        (tree.attribute_count && !tree.attributes) ||
        unit_index >= units.count || die_index >= tree.count ||
        capacity > SIZE_MAX / sizeof(*facts) || !same_backing(units.backing, tree.backing) ||
        units.section_index != tree.section_index ||
        units.entries[unit_index].offset != tree.unit_offset ||
        !units.entries[unit_index].header_available ||
        (units.entries[unit_index].header.version != 4 && units.entries[unit_index].header.version != 5)) {
        return TB_SYMBOL_INLINE_SCOPE_INVALID;
    }
    const TbSymbolUnitEntry_t *unit = &units.entries[unit_index];
    uint64_t                  root;
    uint64_t                  tag = root_tag(tree.kind);
    bool                      compatible = tree.kind == unit->header.kind ||
        (unit->header.version == 4 && unit->header.kind == TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL &&
         (tree.kind == TB_SYMBOL_DWARF_COMPILE || tree.kind == TB_SYMBOL_DWARF_PARTIAL));
    if (!tag || !compatible || tree.abbrev_offset != unit->header.abbrev_offset ||
        !tb_symbol_add(unit->offset, unit->header.header_bytes, &root) ||
        root >= tree.backing.bytes || tree.entries[die_index].offset >= tree.backing.bytes ||
        tree.entries[0].offset != root || tree.entries[0].tag != tag ||
        tree.entries[0].parent != UINT64_MAX) {
        return TB_SYMBOL_INLINE_SCOPE_INVALID;
    }

    *scope = (TbSymbolInlineScope_t){
        .tree        = tree,
        .units       = units,
        .facts       = facts,
        .status      = TB_SYMBOL_INLINE_SCOPE_PENDING,
        .capacity    = capacity,
        .unit_index  = unit_index,
        .selected    = die_index,
        .active      = die_index,
        .initialized = true
    };
    scope->view.selected     = record(scope, die_index);
    scope->view.selected_tag = tree.entries[die_index].tag;
    scope->view.facts        = facts;

    return scope->status;
}


static TbSymbolInlineScopeStatus_e constant_failure(TbSymbolConstantStatus_e status)
{
    switch (status) {
    case TB_SYMBOL_CONSTANT_CONFLICT:
        return TB_SYMBOL_INLINE_SCOPE_CONFLICT;
    case TB_SYMBOL_CONSTANT_UNSUPPORTED:
        return TB_SYMBOL_INLINE_SCOPE_UNSUPPORTED;
    case TB_SYMBOL_CONSTANT_DEPENDENCY:
        return TB_SYMBOL_INLINE_SCOPE_DEPENDENCY;
    case TB_SYMBOL_CONSTANT_MALFORMED:
        return TB_SYMBOL_INLINE_SCOPE_MALFORMED;
    default:
        return TB_SYMBOL_INLINE_SCOPE_INVALID;
    }
}


static void next_parent(TbSymbolInlineScope_t *scope)
{
    uint64_t parent = scope->tree.entries[scope->active].parent;
    if (parent == UINT64_MAX) {
        if (scope->active != 0) {
            scope->status = TB_SYMBOL_INLINE_SCOPE_INVALID;
        } else {
            scope->view.count = scope->count;
            scope->status     = TB_SYMBOL_INLINE_SCOPE_READY;
        }
    } else if (parent >= scope->active) {
        scope->status = TB_SYMBOL_INLINE_SCOPE_INVALID;
    } else {
        scope->active = parent;
    }
}


static void inspect_constant(TbSymbolInlineScope_t *scope, size_t max_steps)
{
    TbSymbolConstantStatus_e status = tb_symbol_constant_step(&scope->constant, max_steps);
    if (status == TB_SYMBOL_CONSTANT_PENDING)
        return;
    if (status == TB_SYMBOL_CONSTANT_SCHEMA) {
        scope->status = TB_SYMBOL_INLINE_SCOPE_SCHEMA;
        return;
    }
    if (status != TB_SYMBOL_CONSTANT_FOUND && status != TB_SYMBOL_CONSTANT_ABSENT) {
        scope->status = constant_failure(status);
        return;
    }
    TbSymbolInlineScopeFact_t fact = {
        .record = record(scope, scope->active),
        .status = status
    };
    if (!tb_symbol_constant_selection(&scope->constant, &fact.selection)) {
        scope->status = TB_SYMBOL_INLINE_SCOPE_INVALID;
        return;
    }
    if (status == TB_SYMBOL_CONSTANT_FOUND) {
        TbSymbolInlineKind_e kind;
        if (!tb_symbol_constant_view(&scope->constant, &fact.constant)) {
            scope->status = TB_SYMBOL_INLINE_SCOPE_INVALID;
            return;
        }
        if (!tb_symbol_inline_kind(fact.constant.value, &kind)) {
            scope->status = TB_SYMBOL_INLINE_SCOPE_UNSUPPORTED;
            return;
        }
        if (kind != TB_SYMBOL_INLINE_NOT_INLINED && !scope->view.abstract_available) {
            scope->view.abstract_root      = fact.record;
            scope->view.abstract_available = true;
            scope->view.selected_abstract  = scope->active == scope->selected;
        }
    }
    scope->facts[scope->count++] = fact;
    scope->inspecting           = false;
    next_parent(scope);
}


TbSymbolInlineScopeStatus_e tb_symbol_inline_scope_step(TbSymbolInlineScope_t *scope, size_t max_steps)
{
    if (!scope || !scope->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_INLINE_SCOPE_INVALID;
    if (scope->status != TB_SYMBOL_INLINE_SCOPE_PENDING)
        return scope->status;
    if (scope->progress == UINT64_MAX || scope->active >= scope->tree.count) {
        scope->status = TB_SYMBOL_INLINE_SCOPE_INVALID;
        return scope->status;
    }
    if (scope->inspecting) {
        inspect_constant(scope, max_steps);
    } else {
        uint64_t tag = scope->tree.entries[scope->active].tag;
        if (tag == 0x2e) {
            if (scope->count == scope->capacity) {
                scope->status = TB_SYMBOL_INLINE_SCOPE_RESOURCE;
            } else if (tb_symbol_constant_init(&scope->constant, scope->tree, scope->active,
                                               TB_SYMBOL_CONSTANT_INLINE) != TB_SYMBOL_CONSTANT_PENDING) {
                scope->status = TB_SYMBOL_INLINE_SCOPE_INVALID;
            } else {
                scope->inspecting = true;
            }
        } else {
            if (tag == 0x1d && !scope->view.concrete_available) {
                scope->view.concrete_root      = record(scope, scope->active);
                scope->view.concrete_available = true;
                scope->view.selected_concrete  = scope->active == scope->selected;
            }
            next_parent(scope);
        }
    }
    ++scope->progress;
    return scope->status;
}


TbSymbolInlineScopeStatus_e tb_symbol_inline_scope_schema(TbSymbolInlineScope_t *scope,
                                                          const TbSymbolAbbrevView_t *schema)
{
    if (!scope || !scope->initialized || scope->status != TB_SYMBOL_INLINE_SCOPE_SCHEMA)
        return TB_SYMBOL_INLINE_SCOPE_INVALID;
    TbSymbolConstantStatus_e status = tb_symbol_constant_schema(&scope->constant, schema);
    if (status == TB_SYMBOL_CONSTANT_INVALID)
        return TB_SYMBOL_INLINE_SCOPE_INVALID;
    if (status == TB_SYMBOL_CONSTANT_PENDING) {
        scope->status = TB_SYMBOL_INLINE_SCOPE_PENDING;
    } else {
        scope->status = constant_failure(status);
    }
    return scope->status;
}


bool tb_symbol_inline_scope_view(const TbSymbolInlineScope_t *scope, TbSymbolInlineScopeView_t *view)
{
    if (!scope || !scope->initialized || !view || scope->status != TB_SYMBOL_INLINE_SCOPE_READY)
        return false;
    *view = scope->view;
    return true;
}


bool tb_symbol_inline_scope_failure(const TbSymbolInlineScope_t *scope,
                                    TbSymbolInlineScopeFailure_t *failure)
{
    if (!scope || !scope->initialized || !failure || scope->status == TB_SYMBOL_INLINE_SCOPE_READY ||
        scope->status == TB_SYMBOL_INLINE_SCOPE_PENDING || scope->status == TB_SYMBOL_INLINE_SCOPE_SCHEMA) {
        return false;
    }
    *failure = (TbSymbolInlineScopeFailure_t){
        .status             = scope->status,
        .entry_index        = scope->active,
        .record_available   = scope->active < scope->tree.count,
        .constant_available = scope->inspecting
    };
    if (failure->record_available)
        failure->record = record(scope, scope->active);
    if (failure->constant_available) {
        failure->constant_status     = scope->constant.status;
        failure->selection_available = tb_symbol_constant_selection(&scope->constant, &failure->selection);
        failure->value_available     = tb_symbol_constant_view(&scope->constant, &failure->constant);
    }
    return true;
}


uint64_t tb_symbol_inline_scope_progress(const TbSymbolInlineScope_t *scope)
{
    return scope && scope->initialized ? scope->progress : 0;
}
