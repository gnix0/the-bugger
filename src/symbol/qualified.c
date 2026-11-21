#include "symbol/qualified.h"

#include <elf.h>
#include <stdlib.h>

/* Profiles describe metadata construction, not canonical C++ source syntax. */
typedef enum {
    QUAL_INIT,
    QUAL_SCOPE,
    QUAL_SCOPE_EDGE,
    QUAL_NAME_REQUEST,
    QUAL_NAME_VALIDATE,
    QUAL_LANGUAGE_REQUEST,
    QUAL_FIND,
    QUAL_PARENTS,
    QUAL_CHOOSE,
    QUAL_FLATTEN,
    QUAL_COMPARE,
    QUAL_DONE
} QualifiedPhase_e;

struct tb_symbol_qualified {
    TbSymbolBudget_t          *construction;
    TbSymbolSectionsView_t     directory;
    TbSymbolScopeView_t        scope;
    TbSymbolQualifiedNode_t   *nodes;
    TbSymbolConstantView_t    *languages;
    TbSymbolStringView_t      *parts[2];
    TbSymbolNameOrder_t       *comparison;
    TbSymbolQualifiedFailure_t failure;
    TbSymbolQualifiedStatus_e  status;
    QualifiedPhase_e           phase;
    size_t                     owned;
    size_t                     node;
    size_t                     anchor;
    size_t                     scan;
    size_t                     complete;
    size_t                     sweep_complete;
    size_t                     representative;
    size_t                     flatten_node;
    size_t                     flatten_cursor;
    unsigned                   flatten_input;
    uint64_t                   progress;
    int                        descriptor;
};

static bool same_record(TbSymbolReferenceTarget_t left, TbSymbolReferenceTarget_t right)
{
    return left.backing.key.device == right.backing.key.device &&
           left.backing.key.inode == right.backing.key.inode && left.backing.bytes == right.backing.bytes &&
           left.section_index == right.section_index && left.unit_offset == right.unit_offset &&
           left.record_offset == right.record_offset && left.entry_index == right.entry_index;
}


static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.key.device == right.key.device && left.key.inode == right.key.inode &&
           left.bytes == right.bytes;
}


TbSymbolQualified_t *tb_symbol_qualified_create(TbSymbolSectionsView_t directory, TbSymbolScopeView_t scope,
                                                int descriptor, TbSymbolBudget_t *construction)
{
    if (!directory.header || !directory.layout || !directory.entries || !scope.nodes || !scope.node_count ||
        !scope.edges || !scope.edge_count || descriptor < 0 || !construction ||
        directory.count != directory.layout->sections.count ||
        directory.backing.bytes != directory.header->file_bytes ||
        scope.node_count > SIZE_MAX / sizeof(TbSymbolQualifiedNode_t) ||
        scope.edge_count > SIZE_MAX / sizeof(TbSymbolConstantView_t) ||
        scope.node_count > SIZE_MAX / (2 * sizeof(TbSymbolStringView_t))) {
        return NULL;
    }
    size_t nodes     = scope.node_count * sizeof(TbSymbolQualifiedNode_t);
    size_t languages = scope.edge_count * sizeof(TbSymbolConstantView_t);
    size_t parts     = scope.node_count * sizeof(TbSymbolStringView_t);
    if (nodes > SIZE_MAX - sizeof(TbSymbolQualified_t) ||
        languages > SIZE_MAX - sizeof(TbSymbolQualified_t) - nodes ||
        2 * parts > SIZE_MAX - sizeof(TbSymbolQualified_t) - nodes - languages) {
        return NULL;
    }
    size_t owned = sizeof(TbSymbolQualified_t) + nodes + languages + 2 * parts;
    if (!tb_symbol_budget_reserve(construction, owned))
        return NULL;
    TbSymbolQualified_t *qualified = calloc(1, sizeof(*qualified));
    if (!qualified) {
        (void)tb_symbol_budget_release(construction, owned);
        return NULL;
    }
    qualified->construction = construction;
    qualified->directory    = directory;
    qualified->scope        = scope;
    qualified->owned        = owned;
    qualified->descriptor   = descriptor;
    qualified->nodes        = malloc(nodes);
    qualified->languages    = malloc(languages);
    qualified->parts[0]    = malloc(parts);
    qualified->parts[1]    = malloc(parts);
    if (!qualified->nodes || !qualified->languages || !qualified->parts[0] || !qualified->parts[1]) {
        tb_symbol_qualified_destroy(qualified);
        return NULL;
    }
    return qualified;
}


static void fail(TbSymbolQualified_t *qualified, TbSymbolQualifiedStatus_e status,
                 TbSymbolQualifiedFailure_e reason)
{
    qualified->status  = status;
    qualified->phase   = QUAL_DONE;
    qualified->failure = (TbSymbolQualifiedFailure_t){
        .reason       = reason,
        .node_index   = qualified->node,
        .anchor_index = qualified->anchor,
        .other_anchor = SIZE_MAX
    };
}


static void scope_node(TbSymbolQualified_t *qualified)
{
    if (qualified->node == qualified->scope.node_count) {
        qualified->node = 0;
        qualified->phase = QUAL_NAME_REQUEST;
        return;
    }
    const TbSymbolScopeNode_t *node = &qualified->scope.nodes[qualified->node];
    if (!node->complete || !node->relationships.nodes || !node->relationships.count ||
        !node->contexts.anchors || !node->contexts.count ||
        node->first_edge >= qualified->scope.edge_count ||
        node->contexts.count > qualified->scope.edge_count - node->first_edge ||
        !same_record(node->record, node->relationships.nodes[0].record) ||
        !same_record(node->record, node->contexts.selected) ||
        !same_backing(node->record.backing, qualified->directory.backing)) {
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_SCOPE);
        return;
    }
    if (node->context.tag == 0x11 || node->context.tag == 0x3c || node->context.tag == 0x41) {
        fail(qualified, TB_SYMBOL_QUALIFIED_UNSUPPORTED, TB_SYMBOL_QUALIFIED_PARENT_PROFILE);
        return;
    }
    qualified->anchor = 0;
    qualified->phase  = QUAL_SCOPE_EDGE;
}


static void scope_edge(TbSymbolQualified_t *qualified)
{
    const TbSymbolScopeNode_t *node = &qualified->scope.nodes[qualified->node];
    if (qualified->anchor == node->contexts.count) {
        ++qualified->node;
        qualified->phase = QUAL_SCOPE;
        return;
    }
    const TbSymbolScopeEdge_t     *edge    = &qualified->scope.edges[node->first_edge + qualified->anchor];
    const TbSymbolContextAnchor_t *anchor  = &node->contexts.anchors[qualified->anchor];
    bool                           lexical = anchor->kind == TB_SYMBOL_CONTEXT_LEXICAL_PARENT;
    if ((anchor->context.unit_kind != TB_SYMBOL_DWARF_COMPILE &&
         anchor->context.unit_kind != TB_SYMBOL_DWARF_PARTIAL &&
         anchor->context.unit_kind != TB_SYMBOL_DWARF_TYPE) ||
        edge->anchor_index != qualified->anchor ||
        (lexical && (edge->target_node >= qualified->scope.node_count ||
                     !same_record(anchor->parent, qualified->scope.nodes[edge->target_node].record))) ||
        (!lexical && (edge->target_node != SIZE_MAX ||
                      (anchor->kind != TB_SYMBOL_CONTEXT_UNIT_PARENT &&
                       anchor->kind != TB_SYMBOL_CONTEXT_UNIT_ROOT)))) {
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_SCOPE);
        return;
    }
    ++qualified->anchor;
}


bool tb_symbol_qualified_need(const TbSymbolQualified_t *qualified, TbSymbolQualifiedNeed_t *need)
{
    if (!qualified || !need || qualified->status != TB_SYMBOL_QUALIFIED_SOURCE ||
        qualified->node >= qualified->scope.node_count) {
        return false;
    }
    const TbSymbolScopeNode_t *node = &qualified->scope.nodes[qualified->node];
    if (qualified->phase == QUAL_NAME_REQUEST) {
        *need = (TbSymbolQualifiedNeed_t){
            .kind         = TB_SYMBOL_QUALIFIED_NAME,
            .node_index   = qualified->node,
            .anchor_index = SIZE_MAX,
            .record       = node->record,
            .context      = node->context
        };
    } else if (qualified->phase == QUAL_LANGUAGE_REQUEST && qualified->anchor < node->contexts.count) {
        const TbSymbolContextAnchor_t *anchor  = &node->contexts.anchors[qualified->anchor];
        TbSymbolGraphContext_t         context = anchor->context;
        context.tag                            = context.unit_kind == TB_SYMBOL_DWARF_COMPILE   ? 0x11
                        : context.unit_kind == TB_SYMBOL_DWARF_PARTIAL ? 0x3c : 0x41;
        context.parent_record                  = UINT64_MAX;
        context.parent_tag                     = 0;
        *need = (TbSymbolQualifiedNeed_t){
            .kind         = TB_SYMBOL_QUALIFIED_LANGUAGE,
            .node_index   = qualified->node,
            .anchor_index = qualified->anchor,
            .record       = anchor->unit_root,
            .context      = context
        };
    } else {
        return false;
    }
    return true;
}


TbSymbolQualifiedStatus_e tb_symbol_qualified_name(TbSymbolQualified_t               *qualified,
                                                   TbSymbolInheritedTextStatus_e      status,
                                                   const TbSymbolInheritedTextView_t *name)
{
    TbSymbolQualifiedNeed_t need;
    if (!tb_symbol_qualified_need(qualified, &need) || need.kind != TB_SYMBOL_QUALIFIED_NAME)
        return TB_SYMBOL_QUALIFIED_INVALID;
    if (status == TB_SYMBOL_INHERITED_TEXT_FOUND) {
        if (!name || !name->facts || !name->count ||
            name->count > qualified->scope.nodes[qualified->node].relationships.count ||
            !same_record(name->selected, need.record)) {
            return TB_SYMBOL_QUALIFIED_INVALID;
        }
        qualified->nodes[qualified->node].name = *name;
        qualified->scan                        = 0;
        qualified->phase                       = QUAL_NAME_VALIDATE;
        qualified->status = TB_SYMBOL_QUALIFIED_PENDING;
        return qualified->status;
    }
    TbSymbolQualifiedStatus_e result;
    switch (status) {
    case TB_SYMBOL_INHERITED_TEXT_ABSENT:
        result = qualified->node == 0 ? TB_SYMBOL_QUALIFIED_UNNAMED : TB_SYMBOL_QUALIFIED_UNSUPPORTED;
        break;
    case TB_SYMBOL_INHERITED_TEXT_CONFLICT:
        result = TB_SYMBOL_QUALIFIED_CONFLICT;
        break;
    case TB_SYMBOL_INHERITED_TEXT_DEPENDENCY:
        result = TB_SYMBOL_QUALIFIED_DEPENDENCY;
        break;
    case TB_SYMBOL_INHERITED_TEXT_UNSUPPORTED:
        result = TB_SYMBOL_QUALIFIED_UNSUPPORTED;
        break;
    case TB_SYMBOL_INHERITED_TEXT_MALFORMED:
        result = TB_SYMBOL_QUALIFIED_MALFORMED;
        break;
    case TB_SYMBOL_INHERITED_TEXT_RESOURCE:
        result = TB_SYMBOL_QUALIFIED_RESOURCE;
        break;
    case TB_SYMBOL_INHERITED_TEXT_OPERATIONAL:
        result = TB_SYMBOL_QUALIFIED_OPERATIONAL;
        break;
    default:
        return TB_SYMBOL_QUALIFIED_INVALID;
    }
    fail(qualified, result, TB_SYMBOL_QUALIFIED_NAME_VALUE);
    return qualified->status;
}


static void name_fact(TbSymbolQualified_t *qualified)
{
    const TbSymbolInheritedTextView_t *name = &qualified->nodes[qualified->node].name;
    if (qualified->scan == name->count) {
        if (!name->facts[0].text.text.bytes) {
            TbSymbolQualifiedStatus_e status = qualified->node == 0 ? TB_SYMBOL_QUALIFIED_UNNAMED
                                                                      : TB_SYMBOL_QUALIFIED_UNSUPPORTED;
            fail(qualified, status, TB_SYMBOL_QUALIFIED_NAME_VALUE);
            return;
        }
        ++qualified->node;
        qualified->phase = QUAL_NAME_REQUEST;
        return;
    }
    const TbSymbolInheritedTextFact_t *fact  = &name->facts[qualified->scan];
    const TbSymbolGraphView_t         *graph = &qualified->scope.nodes[qualified->node].relationships;
    if (fact->node_index >= graph->count ||
        (qualified->scan && fact->node_index <= name->facts[qualified->scan - 1].node_index)) {
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_NAME_VALUE);
        return;
    }
    TbSymbolReferenceTarget_t owner = graph->nodes[fact->node_index].record;
    if (!same_backing(fact->selection.backing, owner.backing) || fact->selection.name != 3 ||
        fact->selection.matches != 1 || fact->selection.section_index != owner.section_index ||
        fact->selection.unit_offset != owner.unit_offset ||
        fact->selection.record_offset != owner.record_offset ||
        fact->selection.first_offset != fact->text.attribute.offset || fact->text.attribute.name != 3 ||
        !same_backing(fact->text.backing, owner.backing) ||
        fact->text.source_section != owner.section_index || fact->text.source_unit != owner.unit_offset ||
        fact->text.source_record != owner.record_offset ||
        fact->text.abbrev_section != graph->nodes[fact->node_index].context.abbrev_section ||
        fact->text.abbrev_offset != graph->nodes[fact->node_index].context.abbrev_offset) {
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_NAME_VALUE);
        return;
    }
    TbSymbolStringView_t string = fact->text.text;
    if (!same_backing(string.backing, qualified->directory.backing) ||
        string.section_index >= qualified->directory.count) {
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_NAME_VALUE);
        return;
    }
    TbSymbolElfSection_t section = qualified->directory.entries[string.section_index].header;
    uint64_t             end;
    uint64_t             section_end;
    if ((section.type != SHT_PROGBITS && section.type != SHT_STRTAB) || (section.flags & SHF_COMPRESSED) ||
        !tb_symbol_add(section.offset, section.bytes, &section_end) || section_end > string.backing.bytes ||
        !tb_symbol_add(string.offset, string.bytes, &end) || end >= string.end_offset ||
        string.end_offset > section.bytes) {
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_NAME_VALUE);
        return;
    }
    ++qualified->scan;
}


TbSymbolQualifiedStatus_e tb_symbol_qualified_language(TbSymbolQualified_t          *qualified,
                                                       TbSymbolConstantStatus_e      status,
                                                       const TbSymbolConstantView_t *language)
{
    TbSymbolQualifiedNeed_t need;
    if (!tb_symbol_qualified_need(qualified, &need) || need.kind != TB_SYMBOL_QUALIFIED_LANGUAGE)
        return TB_SYMBOL_QUALIFIED_INVALID;
    if (status == TB_SYMBOL_CONSTANT_FOUND) {
        if (!language || !same_backing(language->selection.backing, need.record.backing) ||
            language->selection.name != TB_SYMBOL_CONSTANT_LANGUAGE || language->selection.matches != 1 ||
            language->selection.section_index != need.record.section_index ||
            language->selection.unit_offset != need.record.unit_offset ||
            language->selection.record_offset != need.record.record_offset ||
            language->attribute.name != TB_SYMBOL_CONSTANT_LANGUAGE ||
            language->selection.first_offset != language->attribute.offset ||
            language->abbrev_section != need.context.abbrev_section ||
            language->abbrev_offset != need.context.abbrev_offset) {
            return TB_SYMBOL_QUALIFIED_INVALID;
        }
        const TbSymbolScopeNode_t *node = &qualified->scope.nodes[qualified->node];
        const TbSymbolScopeEdge_t *edge   = &qualified->scope.edges[node->first_edge + qualified->anchor];
        TbSymbolLanguageFamily_e   family = tb_symbol_language_family(language->value);
        if (family == TB_SYMBOL_LANGUAGE_OTHER ||
            (edge->target_node != SIZE_MAX && family != TB_SYMBOL_LANGUAGE_CPP)) {
            fail(qualified, TB_SYMBOL_QUALIFIED_UNSUPPORTED, TB_SYMBOL_QUALIFIED_LANGUAGE_VALUE);
            return qualified->status;
        }
        if (edge->target_node != SIZE_MAX) {
            uint64_t tag = qualified->scope.nodes[edge->target_node].context.tag;
            if (tag != 0x39 && tag != 0x02 && tag != 0x13 && tag != 0x17) {
                fail(qualified, TB_SYMBOL_QUALIFIED_UNSUPPORTED, TB_SYMBOL_QUALIFIED_PARENT_PROFILE);
                return qualified->status;
            }
        }
        qualified->languages[node->first_edge + qualified->anchor] = *language;
        ++qualified->anchor;
        qualified->status = TB_SYMBOL_QUALIFIED_PENDING;
        return qualified->status;
    }
    TbSymbolQualifiedStatus_e result;
    switch (status) {
    case TB_SYMBOL_CONSTANT_ABSENT:
    case TB_SYMBOL_CONSTANT_UNSUPPORTED:
        result = TB_SYMBOL_QUALIFIED_UNSUPPORTED;
        break;
    case TB_SYMBOL_CONSTANT_DEPENDENCY:
        result = TB_SYMBOL_QUALIFIED_DEPENDENCY;
        break;
    case TB_SYMBOL_CONSTANT_CONFLICT:
        result = TB_SYMBOL_QUALIFIED_CONFLICT;
        break;
    case TB_SYMBOL_CONSTANT_MALFORMED:
        result = TB_SYMBOL_QUALIFIED_MALFORMED;
        break;
    default:
        return TB_SYMBOL_QUALIFIED_INVALID;
    }
    fail(qualified, result, TB_SYMBOL_QUALIFIED_LANGUAGE_VALUE);
    return qualified->status;
}


static void request_name(TbSymbolQualified_t *qualified)
{
    if (qualified->node == qualified->scope.node_count) {
        qualified->node = 0;
        qualified->anchor = 0;
        qualified->phase  = QUAL_LANGUAGE_REQUEST;
    } else {
        qualified->status = TB_SYMBOL_QUALIFIED_SOURCE;
    }
}


static void request_language(TbSymbolQualified_t *qualified)
{
    if (qualified->node == qualified->scope.node_count) {
        qualified->node = 0;
        qualified->phase = QUAL_FIND;
        return;
    }
    const TbSymbolScopeNode_t *node = &qualified->scope.nodes[qualified->node];
    if (qualified->anchor == node->contexts.count) {
        ++qualified->node;
        qualified->anchor = 0;
    } else {
        qualified->status = TB_SYMBOL_QUALIFIED_SOURCE;
    }
}


static void find_ready(TbSymbolQualified_t *qualified)
{
    if (qualified->complete == qualified->scope.node_count) {
        qualified->status = TB_SYMBOL_QUALIFIED_READY;
        qualified->phase  = QUAL_DONE;
    } else if (qualified->node == qualified->scope.node_count) {
        if (qualified->complete == qualified->sweep_complete) {
            fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_SCOPE);
        } else {
            qualified->node = 0;
            qualified->sweep_complete = qualified->complete;
        }
    } else if (qualified->nodes[qualified->node].complete) {
        ++qualified->node;
    } else {
        qualified->anchor = 0;
        qualified->phase  = QUAL_PARENTS;
    }
}


static void check_parents(TbSymbolQualified_t *qualified)
{
    const TbSymbolScopeNode_t *node = &qualified->scope.nodes[qualified->node];
    if (qualified->anchor == node->contexts.count) {
        qualified->anchor = 0;
        qualified->representative = qualified->scope.edges[node->first_edge].target_node;
        qualified->phase          = QUAL_CHOOSE;
        return;
    }
    size_t parent = qualified->scope.edges[node->first_edge + qualified->anchor].target_node;
    if (parent != SIZE_MAX && !qualified->nodes[parent].complete) {
        ++qualified->node;
        qualified->phase = QUAL_FIND;
    } else {
        ++qualified->anchor;
    }
}


static void finish_node(TbSymbolQualified_t *qualified)
{
    TbSymbolQualifiedNode_t *node   = &qualified->nodes[qualified->node];
    size_t                   parent = qualified->representative;
    uint64_t                 prefix = parent == SIZE_MAX ? 0 : qualified->nodes[parent].bytes;
    size_t                   parts  = parent == SIZE_MAX ? 0 : qualified->nodes[parent].part_count;
    uint64_t                 bytes;
    if (parts >= qualified->scope.node_count ||
        (parent != SIZE_MAX && !tb_symbol_add(prefix, 2, &prefix)) ||
        !tb_symbol_add(prefix, node->name.facts[0].text.text.bytes, &bytes)) {
        fail(qualified, TB_SYMBOL_QUALIFIED_RESOURCE, TB_SYMBOL_QUALIFIED_LENGTH);
        return;
    }
    node->parent_node = parent;
    node->part_count  = parts + 1;
    node->bytes       = bytes;
    node->complete    = true;
    ++qualified->complete;
    ++qualified->node;
    qualified->phase = QUAL_FIND;
}


static void conflict(TbSymbolQualified_t *qualified)
{
    fail(qualified, TB_SYMBOL_QUALIFIED_CONFLICT, TB_SYMBOL_QUALIFIED_AGREEMENT);
    qualified->failure.other_anchor = 0;
}


static void choose(TbSymbolQualified_t *qualified)
{
    const TbSymbolScopeNode_t *node = &qualified->scope.nodes[qualified->node];
    if (qualified->anchor == node->contexts.count) {
        finish_node(qualified);
        return;
    }
    size_t candidate = qualified->scope.edges[node->first_edge + qualified->anchor].target_node;
    if (candidate == qualified->representative) {
        ++qualified->anchor;
    } else if (candidate == SIZE_MAX || qualified->representative == SIZE_MAX ||
               qualified->nodes[candidate].bytes != qualified->nodes[qualified->representative].bytes) {
        conflict(qualified);
    } else {
        qualified->flatten_input  = 0;
        qualified->flatten_node   = qualified->representative;
        qualified->flatten_cursor = qualified->nodes[qualified->representative].part_count;
        qualified->phase          = QUAL_FLATTEN;
    }
}


static void flatten(TbSymbolQualified_t *qualified)
{
    if (qualified->flatten_node == SIZE_MAX) {
        if (qualified->flatten_cursor) {
            fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_SCOPE);
            return;
        }
        const TbSymbolScopeNode_t *node = &qualified->scope.nodes[qualified->node];
        size_t candidate = qualified->scope.edges[node->first_edge + qualified->anchor].target_node;
        if (qualified->flatten_input == 0) {
            qualified->flatten_input  = 1;
            qualified->flatten_node   = candidate;
            qualified->flatten_cursor = qualified->nodes[candidate].part_count;
            return;
        }
        TbSymbolNameOrderInput_t inputs[2] = {
            {
                .directory  = qualified->directory,
                .components = qualified->parts[0],
                .count      = qualified->nodes[qualified->representative].part_count,
                .descriptor = qualified->descriptor
            },
            {
                .directory  = qualified->directory,
                .components = qualified->parts[1],
                .count      = qualified->nodes[candidate].part_count,
                .descriptor = qualified->descriptor
            }
        };
        qualified->comparison = tb_symbol_name_order_create(inputs, qualified->construction);
        if (!qualified->comparison) {
            fail(qualified, TB_SYMBOL_QUALIFIED_RESOURCE, TB_SYMBOL_QUALIFIED_ALLOCATION);
            return;
        }
        qualified->phase = QUAL_COMPARE;
        return;
    }
    if (qualified->flatten_node >= qualified->scope.node_count || !qualified->flatten_cursor ||
        qualified->flatten_cursor > qualified->scope.node_count || qualified->flatten_input >= 2) {
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_SCOPE);
        return;
    }
    const TbSymbolQualifiedNode_t *node = &qualified->nodes[qualified->flatten_node];
    if (!node->complete || node->part_count != qualified->flatten_cursor) {
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_SCOPE);
        return;
    }
    qualified->parts[qualified->flatten_input][--qualified->flatten_cursor] = node->name.facts[0].text.text;
    qualified->flatten_node                                                 = node->parent_node;
}


static void compare(TbSymbolQualified_t *qualified, size_t max_bytes)
{
    TbSymbolOutcome_e outcome = tb_symbol_name_order_step(qualified->comparison, max_bytes);
    if (outcome == TB_SYMBOL_PENDING)
        return;
    if (outcome != TB_SYMBOL_SUCCESS) {
        TbSymbolNameOrderFailure_t failure = tb_symbol_name_order_failure(qualified->comparison);
        TbSymbolQualifiedStatus_e status;
        switch (outcome) {
        case TB_SYMBOL_RESOURCE_FAILURE:
            status = TB_SYMBOL_QUALIFIED_RESOURCE;
            break;
        case TB_SYMBOL_UNSUPPORTED:
            status = TB_SYMBOL_QUALIFIED_UNSUPPORTED;
            break;
        case TB_SYMBOL_MALFORMED:
            status = TB_SYMBOL_QUALIFIED_MALFORMED;
            break;
        default:
            status = TB_SYMBOL_QUALIFIED_OPERATIONAL;
            break;
        }
        fail(qualified, status, TB_SYMBOL_QUALIFIED_COMPARISON);
        qualified->failure.comparison = failure;
    } else {
        TbSymbolStringOrder_e order;
        if (!tb_symbol_name_order_result(qualified->comparison, &order)) {
            fail(qualified, TB_SYMBOL_QUALIFIED_OPERATIONAL, TB_SYMBOL_QUALIFIED_COMPARISON);
        } else if (order != TB_SYMBOL_STRING_EQUAL) {
            conflict(qualified);
        } else {
            ++qualified->anchor;
            qualified->phase = QUAL_CHOOSE;
        }
    }
    tb_symbol_name_order_destroy(qualified->comparison);
    qualified->comparison = NULL;
}


TbSymbolQualifiedStatus_e tb_symbol_qualified_step(TbSymbolQualified_t *qualified, size_t max_bytes)
{
    if (!qualified || !max_bytes || max_bytes > 65536)
        return TB_SYMBOL_QUALIFIED_INVALID;
    if (qualified->status != TB_SYMBOL_QUALIFIED_PENDING)
        return qualified->status;
    if (qualified->progress == UINT64_MAX) {
        fail(qualified, TB_SYMBOL_QUALIFIED_OPERATIONAL, TB_SYMBOL_QUALIFIED_LENGTH);
        return qualified->status;
    }
    uint64_t child_before = tb_symbol_name_order_progress(qualified->comparison);
    bool     comparing    = qualified->phase == QUAL_COMPARE;
    switch (qualified->phase) {
    case QUAL_INIT:
        if (qualified->scan == qualified->scope.node_count) {
            qualified->node = 0;
            qualified->phase = QUAL_SCOPE;
        } else {
            qualified->nodes[qualified->scan++] = (TbSymbolQualifiedNode_t){
                .parent_node = SIZE_MAX
            };
        }
        break;
    case QUAL_SCOPE:
        scope_node(qualified);
        break;
    case QUAL_SCOPE_EDGE:
        scope_edge(qualified);
        break;
    case QUAL_NAME_REQUEST:
        request_name(qualified);
        break;
    case QUAL_NAME_VALIDATE:
        name_fact(qualified);
        break;
    case QUAL_LANGUAGE_REQUEST:
        request_language(qualified);
        break;
    case QUAL_FIND:
        find_ready(qualified);
        break;
    case QUAL_PARENTS:
        check_parents(qualified);
        break;
    case QUAL_CHOOSE:
        choose(qualified);
        break;
    case QUAL_FLATTEN:
        flatten(qualified);
        break;
    case QUAL_COMPARE:
        compare(qualified, max_bytes);
        break;
    default:
        fail(qualified, TB_SYMBOL_QUALIFIED_INVALID, TB_SYMBOL_QUALIFIED_REQUEST);
        break;
    }
    if (!comparing || qualified->phase != QUAL_COMPARE ||
        child_before != tb_symbol_name_order_progress(qualified->comparison)) {
        ++qualified->progress;
    }
    return qualified->status;
}


bool tb_symbol_qualified_view(const TbSymbolQualified_t *qualified, TbSymbolQualifiedView_t *view)
{
    if (!qualified || !view || qualified->status != TB_SYMBOL_QUALIFIED_READY)
        return false;
    *view = (TbSymbolQualifiedView_t){
        .selected       = qualified->scope.nodes[0].record,
        .scope          = qualified->scope,
        .nodes          = qualified->nodes,
        .count          = qualified->scope.node_count,
        .languages      = qualified->languages,
        .language_count = qualified->scope.edge_count
    };
    return true;
}


TbSymbolQualifiedFailure_t tb_symbol_qualified_failure(const TbSymbolQualified_t *qualified)
{
    return qualified ? qualified->failure : (TbSymbolQualifiedFailure_t){
        .reason       = TB_SYMBOL_QUALIFIED_REQUEST,
        .node_index   = SIZE_MAX,
        .anchor_index = SIZE_MAX,
        .other_anchor = SIZE_MAX
    };
}


uint64_t tb_symbol_qualified_progress(const TbSymbolQualified_t *qualified)
{
    return qualified ? qualified->progress : 0;
}


void tb_symbol_qualified_destroy(TbSymbolQualified_t *qualified)
{
    if (!qualified)
        return;
    tb_symbol_name_order_destroy(qualified->comparison);
    free(qualified->parts[1]);
    free(qualified->parts[0]);
    free(qualified->languages);
    free(qualified->nodes);
    TbSymbolBudget_t *construction = qualified->construction;
    size_t            owned        = qualified->owned;
    free(qualified);
    (void)tb_symbol_budget_release(construction, owned);
}
