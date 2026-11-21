#include "symbol/inherited_text.h"

#include <stdlib.h>

typedef enum {
    INHERITED_OWNERS,
    INHERITED_FACT_STORAGE,
    INHERITED_NEXT_OWNER,
    INHERITED_VALUE_SOURCE,
    INHERITED_VALUE,
    INHERITED_ADOPT,
    INHERITED_COMPARE_START,
    INHERITED_COMPARE,
    INHERITED_DONE
} InheritedTextPhase_e;

struct tb_symbol_inherited_text {
    TbSymbolBudget_t                *construction;
    TbSymbolSectionsView_t           directory;
    TbSymbolGraphView_t              graph;
    TbSymbolInheritedTextSelection_t selection;
    TbSymbolInherit_t                owners;
    TbSymbolInheritSlot_t           *slots;
    TbSymbolInheritedTextFact_t     *facts;
    TbSymbolInheritOwner_t           expected;
    TbSymbolInheritedTextFact_t      candidate;
    TbSymbolText_t                  *value;
    TbSymbolStringOrder_t           *comparison;
    size_t                           owner_count;
    size_t                           fact_count;
    size_t                           scan;
    uint64_t                         slot_bytes;
    uint64_t                         fact_bytes;
    uint64_t                         progress;
    InheritedTextPhase_e             phase;
    TbSymbolInheritedTextStatus_e    status;
    TbSymbolInheritedTextFailure_t   failure;
};

static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.key.device == right.key.device && left.key.inode == right.key.inode &&
           left.bytes == right.bytes;
}


static bool section_available(TbSymbolSectionsView_t directory, uint64_t index)
{
    return index == UINT64_MAX || index < directory.count;
}


TbSymbolInheritedText_t *tb_symbol_inherited_text_create(TbSymbolSectionsView_t directory,
                                                         TbSymbolGraphView_t graph,
                                                         TbSymbolInheritedTextSelection_t selection,
                                                         TbSymbolBudget_t *construction)
{
    uint64_t bytes;
    if (!directory.header || !directory.layout || !directory.entries || !graph.nodes || !graph.count ||
        directory.count != directory.layout->sections.count ||
        directory.backing.bytes != directory.header->file_bytes || selection.descriptor < 0 ||
        (selection.name != TB_SYMBOL_INHERIT_NAME && selection.name != TB_SYMBOL_INHERIT_LINKAGE_NAME) ||
        !same_backing(directory.backing, graph.nodes[0].record.backing) ||
        !section_available(directory, selection.string_section) ||
        !section_available(directory, selection.line_string_section) ||
        !section_available(directory, selection.offset_section) ||
        graph.count > SIZE_MAX / sizeof(TbSymbolInheritSlot_t) ||
        !tb_symbol_multiply(graph.count, sizeof(TbSymbolInheritSlot_t), &bytes) || !construction ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolInheritedText_t))) {
        return NULL;
    }
    TbSymbolInheritedText_t *text = calloc(1, sizeof(*text));
    if (!text) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolInheritedText_t));
        return NULL;
    }
    text->construction = construction;
    text->directory    = directory;
    text->graph        = graph;
    text->selection    = selection;
    if (!tb_symbol_budget_reserve(construction, bytes)) {
        tb_symbol_inherited_text_destroy(text);
        return NULL;
    }
    text->slot_bytes = bytes;
    text->slots = malloc((size_t)bytes);
    if (!text->slots || tb_symbol_inherit_init(&text->owners, graph, selection.name, text->slots,
                                               graph.count) != TB_SYMBOL_INHERIT_PENDING) {
        tb_symbol_inherited_text_destroy(text);
        return NULL;
    }
    return text;
}


static void finish(TbSymbolInheritedText_t *text, TbSymbolInheritedTextStatus_e status,
                    TbSymbolInheritedTextFailure_e reason)
{
    text->status         = status;
    text->failure.reason = reason;
    text->phase          = INHERITED_DONE;
}


static void owners(TbSymbolInheritedText_t *text)
{
    TbSymbolInheritStatus_e status = tb_symbol_inherit_step(&text->owners, 256);
    if (status == TB_SYMBOL_INHERIT_SOURCE) {
        text->status = TB_SYMBOL_INHERITED_TEXT_SOURCE;
    } else if (status == TB_SYMBOL_INHERIT_ABSENT) {
        finish(text, TB_SYMBOL_INHERITED_TEXT_ABSENT, TB_SYMBOL_INHERITED_TEXT_NONE);
    } else if (status == TB_SYMBOL_INHERIT_UNIQUE || status == TB_SYMBOL_INHERIT_MULTIPLE) {
        TbSymbolInheritResult_t result;
        if (!tb_symbol_inherit_result(&text->owners, &result) || !result.owner_count) {
            finish(text, TB_SYMBOL_INHERITED_TEXT_OPERATIONAL, TB_SYMBOL_INHERITED_TEXT_OWNERS);
        } else {
            text->owner_count = result.owner_count;
            text->phase       = INHERITED_FACT_STORAGE;
        }
    } else if (status != TB_SYMBOL_INHERIT_PENDING) {
        (void)tb_symbol_inherit_failure(&text->owners, &text->failure.owner);
        finish(text, status == TB_SYMBOL_INHERIT_CONFLICT ? TB_SYMBOL_INHERITED_TEXT_CONFLICT
                        : status == TB_SYMBOL_INHERIT_DEPENDENCY ? TB_SYMBOL_INHERITED_TEXT_DEPENDENCY
                                                                : TB_SYMBOL_INHERITED_TEXT_OPERATIONAL,
               TB_SYMBOL_INHERITED_TEXT_OWNERS);
    }
}


static void fact_storage(TbSymbolInheritedText_t *text)
{
    uint64_t bytes;
    if (text->owner_count > SIZE_MAX / sizeof(*text->facts) ||
        !tb_symbol_multiply(text->owner_count, sizeof(*text->facts), &bytes) ||
        !tb_symbol_budget_reserve(text->construction, bytes)) {
        finish(text, TB_SYMBOL_INHERITED_TEXT_RESOURCE, TB_SYMBOL_INHERITED_TEXT_ALLOCATION);
        return;
    }
    text->fact_bytes = bytes;
    text->facts = malloc((size_t)bytes);
    if (!text->facts) {
        finish(text, TB_SYMBOL_INHERITED_TEXT_RESOURCE, TB_SYMBOL_INHERITED_TEXT_ALLOCATION);
        return;
    }
    text->phase = INHERITED_NEXT_OWNER;
}


static void next_owner(TbSymbolInheritedText_t *text)
{
    if (text->scan == text->graph.count) {
        finish(text, text->fact_count == text->owner_count ? TB_SYMBOL_INHERITED_TEXT_FOUND
                                                         : TB_SYMBOL_INHERITED_TEXT_OPERATIONAL,
               text->fact_count == text->owner_count ? TB_SYMBOL_INHERITED_TEXT_NONE
                                                     : TB_SYMBOL_INHERITED_TEXT_OWNERS);
        return;
    }
    if (tb_symbol_inherit_owner(&text->owners, text->scan++, &text->expected)) {
        text->failure.node_index = text->expected.node_index;
        text->phase              = INHERITED_VALUE_SOURCE;
        text->status             = TB_SYMBOL_INHERITED_TEXT_SOURCE;
    }
}


bool tb_symbol_inherited_text_need(const TbSymbolInheritedText_t *text, TbSymbolInheritedTextNeed_t *need)
{
    if (!text || !need || text->status != TB_SYMBOL_INHERITED_TEXT_SOURCE)
        return false;
    TbSymbolInheritedTextNeed_t candidate = {0};
    if (text->phase == INHERITED_OWNERS) {
        candidate.kind = TB_SYMBOL_INHERITED_TEXT_OWNER_SOURCE;
        if (!tb_symbol_inherit_need(&text->owners, &candidate.source))
            return false;
    } else if (text->phase == INHERITED_VALUE_SOURCE) {
        const TbSymbolGraphNode_t *node = &text->graph.nodes[text->expected.node_index];
        candidate.kind = TB_SYMBOL_INHERITED_TEXT_VALUE_SOURCE;
        candidate.source = (TbSymbolInheritNeed_t){
            .record     = node->record,
            .context    = node->context,
            .name       = text->selection.name,
            .node_index = text->expected.node_index
        };
    } else {
        return false;
    }
    *need = candidate;
    return true;
}


static bool string_form(uint64_t form)
{
    return form == TB_SYMBOL_FORM_STRING || form == TB_SYMBOL_FORM_STRP || form == TB_SYMBOL_FORM_LINE_STRP ||
           form == TB_SYMBOL_FORM_STRX || (form >= TB_SYMBOL_FORM_STRX1 && form <= TB_SYMBOL_FORM_STRX4);
}


static bool same_attribute(TbSymbolDieAttribute_t left, TbSymbolDieAttribute_t right)
{
    return left.schema_attribute == right.schema_attribute && left.name == right.name &&
           left.declared_form == right.declared_form && left.offset == right.offset &&
           left.value.form == right.value.form && left.value.bytes == right.value.bytes &&
           left.value.payload_offset == right.value.payload_offset &&
           left.value.payload_bytes == right.value.payload_bytes && left.value.bits == right.value.bits &&
           left.value.numeric == right.value.numeric && left.value.is_signed == right.value.is_signed &&
           left.value.indirect == right.value.indirect;
}


TbSymbolInheritedTextStatus_e tb_symbol_inherited_text_source(TbSymbolInheritedText_t *text,
                                                             const TbSymbolUnitsView_t *units,
                                                             const TbSymbolDiesView_t *tree,
                                                             uint64_t unit_index)
{
    if (!text || text->status != TB_SYMBOL_INHERITED_TEXT_SOURCE)
        return TB_SYMBOL_INHERITED_TEXT_INVALID;
    if (text->phase == INHERITED_OWNERS) {
        TbSymbolInheritStatus_e status = tb_symbol_inherit_source(&text->owners, units, tree, unit_index);
        if (status == TB_SYMBOL_INHERIT_INVALID)
            return TB_SYMBOL_INHERITED_TEXT_INVALID;
        if (status != TB_SYMBOL_INHERIT_PENDING) {
            (void)tb_symbol_inherit_failure(&text->owners, &text->failure.owner);
            finish(text, TB_SYMBOL_INHERITED_TEXT_DEPENDENCY, TB_SYMBOL_INHERITED_TEXT_OWNERS);
        } else {
            text->status = TB_SYMBOL_INHERITED_TEXT_PENDING;
        }
        return text->status;
    }
    if (text->phase != INHERITED_VALUE_SOURCE)
        return TB_SYMBOL_INHERITED_TEXT_INVALID;
    if (!units || !tree) {
        finish(text, TB_SYMBOL_INHERITED_TEXT_DEPENDENCY, TB_SYMBOL_INHERITED_TEXT_VALUE);
        return text->status;
    }
    const TbSymbolGraphNode_t *node = &text->graph.nodes[text->expected.node_index];
    if (!tb_symbol_graph_node_matches(node, *units, *tree, unit_index))
        return TB_SYMBOL_INHERITED_TEXT_INVALID;
    const TbSymbolDieEntry_t *entry = &tree->entries[node->record.entry_index];
    uint64_t index = text->expected.selection.first_index;
    if (!tree->attributes || index >= tree->attribute_count || index < entry->first_attribute ||
        index - entry->first_attribute >= entry->attribute_count ||
        !same_attribute(tree->attributes[index], text->expected.attribute)) {
        return TB_SYMBOL_INHERITED_TEXT_INVALID;
    }
    uint64_t form = text->expected.attribute.value.form;
    if (!string_form(form)) {
        finish(text, form == TB_SYMBOL_FORM_STRP_SUP ? TB_SYMBOL_INHERITED_TEXT_UNSUPPORTED
                                                    : TB_SYMBOL_INHERITED_TEXT_MALFORMED,
               TB_SYMBOL_INHERITED_TEXT_FORM);
        return text->status;
    }
    text->candidate = (TbSymbolInheritedTextFact_t){
        .node_index          = text->expected.node_index,
        .selection           = text->expected.selection,
        .unit                = units->entries[unit_index],
        .unit_root           = {
            .backing       = tree->backing,
            .section_index = tree->section_index,
            .unit_offset   = tree->unit_offset,
            .record_offset = tree->entries[0].offset
        },
        .root_first_attribute = tree->entries[0].first_attribute,
        .root_attribute_count = tree->entries[0].attribute_count
    };
    text->value = tb_symbol_text_create(text->directory, *units, *tree, (TbSymbolTextSelection_t){
        .unit_index          = unit_index,
        .die_index           = node->record.entry_index,
        .name                = text->selection.name,
        .string_section      = text->selection.string_section,
        .line_string_section = text->selection.line_string_section,
        .offset_section      = text->selection.offset_section,
        .descriptor          = text->selection.descriptor
    }, text->construction);
    if (!text->value) {
        finish(text, TB_SYMBOL_INHERITED_TEXT_RESOURCE, TB_SYMBOL_INHERITED_TEXT_ALLOCATION);
    } else {
        text->phase  = INHERITED_VALUE;
        text->status = TB_SYMBOL_INHERITED_TEXT_PENDING;
    }
    return text->status;
}


static void value(TbSymbolInheritedText_t *text, size_t max_bytes)
{
    TbSymbolTextOutcome_e status = tb_symbol_text_step(text->value, max_bytes);
    if (status == TB_SYMBOL_TEXT_FOUND) {
        text->phase = INHERITED_ADOPT;
    } else if (status != TB_SYMBOL_TEXT_PENDING) {
        text->failure.text = tb_symbol_text_failure(text->value);
        TbSymbolInheritedTextStatus_e outcome = TB_SYMBOL_INHERITED_TEXT_OPERATIONAL;
        if (status == TB_SYMBOL_TEXT_CONFLICT) {
            outcome = TB_SYMBOL_INHERITED_TEXT_CONFLICT;
        } else if (status == TB_SYMBOL_TEXT_DEPENDENCY) {
            outcome = TB_SYMBOL_INHERITED_TEXT_DEPENDENCY;
        } else if (status == TB_SYMBOL_TEXT_UNSUPPORTED) {
            bool read_failure = (text->failure.text.reason == TB_SYMBOL_TEXT_STRING &&
                                 text->failure.text.string.reason == TB_SYMBOL_STRING_READ) ||
                                (text->failure.text.reason == TB_SYMBOL_TEXT_OFFSETS &&
                                 text->failure.text.offsets.reason == TB_SYMBOL_STRING_OFFSETS_READ);
            if (!read_failure)
                outcome = TB_SYMBOL_INHERITED_TEXT_UNSUPPORTED;
        } else if (status == TB_SYMBOL_TEXT_MALFORMED) {
            outcome = TB_SYMBOL_INHERITED_TEXT_MALFORMED;
        } else if (status == TB_SYMBOL_TEXT_RESOURCE) {
            outcome = TB_SYMBOL_INHERITED_TEXT_RESOURCE;
        }
        finish(text, outcome, TB_SYMBOL_INHERITED_TEXT_VALUE);
    }
}


static void adopt(TbSymbolInheritedText_t *text)
{
    const TbSymbolGraphNode_t *node = &text->graph.nodes[text->expected.node_index];
    if (text->fact_count >= text->owner_count || !tb_symbol_text_view(text->value, &text->candidate.text) ||
        !same_backing(node->record.backing, text->candidate.text.backing) ||
        text->candidate.text.source_section != node->record.section_index ||
        text->candidate.text.source_unit != node->record.unit_offset ||
        text->candidate.text.source_record != node->record.record_offset ||
        text->candidate.text.abbrev_section != node->context.abbrev_section ||
        text->candidate.text.abbrev_offset != node->context.abbrev_offset ||
        !same_attribute(text->candidate.text.attribute, text->expected.attribute)) {
        finish(text, TB_SYMBOL_INHERITED_TEXT_OPERATIONAL, TB_SYMBOL_INHERITED_TEXT_ADOPTION);
        return;
    }
    /* Adopt the complete scoped claim while all original inputs remain pinned.
     * Reader/decoder scratch is reconstructible; facts own no child pointers. */
    text->facts[text->fact_count++] = text->candidate;
    tb_symbol_text_destroy(text->value);
    text->value = NULL;
    text->phase = text->fact_count == 1 ? INHERITED_NEXT_OWNER : INHERITED_COMPARE_START;
}


static void compare_start(TbSymbolInheritedText_t *text)
{
    TbSymbolStringOrderInput_t inputs[2] = {
        {
            .directory  = text->directory,
            .string     = text->facts[0].text.text,
            .descriptor = text->selection.descriptor
        },
        {
            .directory  = text->directory,
            .string     = text->facts[text->fact_count - 1].text.text,
            .descriptor = text->selection.descriptor
        }
    };
    text->comparison = tb_symbol_string_order_create(inputs, text->construction);
    if (!text->comparison)
        finish(text, TB_SYMBOL_INHERITED_TEXT_RESOURCE, TB_SYMBOL_INHERITED_TEXT_ALLOCATION);
    else
        text->phase = INHERITED_COMPARE;
}


static void compare(TbSymbolInheritedText_t *text, size_t max_bytes)
{
    TbSymbolOutcome_e status = tb_symbol_string_order_step(text->comparison, max_bytes);
    if (status == TB_SYMBOL_SUCCESS) {
        TbSymbolStringOrder_e order;
        if (!tb_symbol_string_order_result(text->comparison, &order)) {
            finish(text, TB_SYMBOL_INHERITED_TEXT_OPERATIONAL, TB_SYMBOL_INHERITED_TEXT_ORDER);
        } else if (order != TB_SYMBOL_STRING_EQUAL) {
            text->failure.other_node_index = text->facts[0].node_index;
            finish(text, TB_SYMBOL_INHERITED_TEXT_CONFLICT, TB_SYMBOL_INHERITED_TEXT_DISAGREEMENT);
        } else {
            tb_symbol_string_order_destroy(text->comparison);
            text->comparison = NULL;
            text->phase      = INHERITED_NEXT_OWNER;
        }
    } else if (status != TB_SYMBOL_PENDING) {
        text->failure.order = tb_symbol_string_order_failure(text->comparison);
        finish(text, status == TB_SYMBOL_RESOURCE_FAILURE ? TB_SYMBOL_INHERITED_TEXT_RESOURCE
                                                         : TB_SYMBOL_INHERITED_TEXT_OPERATIONAL,
               TB_SYMBOL_INHERITED_TEXT_ORDER);
    }
}


TbSymbolInheritedTextStatus_e tb_symbol_inherited_text_step(TbSymbolInheritedText_t *text, size_t max_bytes)
{
    if (!text || max_bytes < 64 || max_bytes > 65536)
        return TB_SYMBOL_INHERITED_TEXT_INVALID;
    if (text->status != TB_SYMBOL_INHERITED_TEXT_PENDING)
        return text->status;
    InheritedTextPhase_e phase = text->phase;
    size_t scan = text->scan;
    uint64_t owner_progress = tb_symbol_inherit_progress(&text->owners);
    uint64_t value_progress = tb_symbol_text_progress(text->value);
    uint64_t order_progress = tb_symbol_string_order_progress(text->comparison);
    switch (text->phase) {
    case INHERITED_OWNERS:
        owners(text);
        break;
    case INHERITED_FACT_STORAGE:
        fact_storage(text);
        break;
    case INHERITED_NEXT_OWNER:
        next_owner(text);
        break;
    case INHERITED_VALUE:
        value(text, max_bytes);
        break;
    case INHERITED_ADOPT:
        adopt(text);
        break;
    case INHERITED_COMPARE_START:
        compare_start(text);
        break;
    case INHERITED_COMPARE:
        compare(text, max_bytes);
        break;
    default:
        finish(text, TB_SYMBOL_INHERITED_TEXT_OPERATIONAL, TB_SYMBOL_INHERITED_TEXT_REQUEST);
        break;
    }
    if (text->phase != phase || text->scan != scan ||
        tb_symbol_inherit_progress(&text->owners) != owner_progress ||
        tb_symbol_text_progress(text->value) != value_progress ||
        tb_symbol_string_order_progress(text->comparison) != order_progress) {
        if (text->progress == UINT64_MAX)
            finish(text, TB_SYMBOL_INHERITED_TEXT_OPERATIONAL, TB_SYMBOL_INHERITED_TEXT_REQUEST);
        else
            ++text->progress;
    }
    return text->status;
}


bool tb_symbol_inherited_text_view(const TbSymbolInheritedText_t *text, TbSymbolInheritedTextView_t *view)
{
    if (!text || !view || text->status != TB_SYMBOL_INHERITED_TEXT_FOUND)
        return false;
    *view = (TbSymbolInheritedTextView_t){
        .selected = text->graph.nodes[0].record,
        .facts    = text->facts,
        .count    = text->fact_count
    };
    return true;
}


TbSymbolInheritedTextFailure_t tb_symbol_inherited_text_failure(const TbSymbolInheritedText_t *text)
{
    return text ? text->failure : (TbSymbolInheritedTextFailure_t){
        .reason = TB_SYMBOL_INHERITED_TEXT_REQUEST
    };
}


uint64_t tb_symbol_inherited_text_progress(const TbSymbolInheritedText_t *text)
{
    return text ? text->progress : 0;
}


void tb_symbol_inherited_text_destroy(TbSymbolInheritedText_t *text)
{
    if (!text)
        return;
    tb_symbol_string_order_destroy(text->comparison);
    tb_symbol_text_destroy(text->value);
    free(text->facts);
    free(text->slots);
    (void)tb_symbol_budget_release(text->construction, text->fact_bytes);
    (void)tb_symbol_budget_release(text->construction, text->slot_bytes);
    (void)tb_symbol_budget_release(text->construction, sizeof(*text));
    free(text);
}
