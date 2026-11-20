#include "symbol/text.h"

#include <stdlib.h>

typedef enum {
    TEXT_FIELD,
    TEXT_CLASSIFY,
    TEXT_BASE,
    TEXT_OFFSETS,
    TEXT_STRING,
    TEXT_DONE
} TextPhase_e;

struct tb_symbol_text {
    TbSymbolBudget_t        *construction;
    TbSymbolSectionsView_t   directory;
    TbSymbolUnitsView_t      units;
    TbSymbolDiesView_t       tree;
    TbSymbolTextSelection_t  selection;
    TbSymbolAttributes_t     lookup;
    TbSymbolStringOffsets_t *offsets;
    TbSymbolString_t        *string;
    TbSymbolTextView_t       view;
    uint64_t                progress;
    TextPhase_e             phase;
    TbSymbolTextOutcome_e    outcome;
    TbSymbolTextFailure_t    failure;
};

static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.bytes == right.bytes && left.key.device == right.key.device &&
           left.key.inode == right.key.inode;
}


static bool section_available(TbSymbolSectionsView_t directory, uint64_t section)
{
    return section == UINT64_MAX || section < directory.count;
}


TbSymbolText_t *tb_symbol_text_create(TbSymbolSectionsView_t directory, TbSymbolUnitsView_t units,
                                     TbSymbolDiesView_t tree, TbSymbolTextSelection_t selection,
                                     TbSymbolBudget_t *construction)
{
    TbSymbolAttributes_t lookup;
    if (!directory.header || !directory.layout || !directory.entries || !units.entries ||
        directory.count != directory.layout->sections.count ||
        directory.backing.bytes != directory.header->file_bytes || selection.descriptor < 0 ||
        units.section_index >= directory.count || selection.unit_index >= units.count ||
        !units.entries[selection.unit_index].header_available ||
        units.section_index != tree.section_index ||
        units.entries[selection.unit_index].offset != tree.unit_offset ||
        !same_backing(directory.backing, units.backing) || !same_backing(directory.backing, tree.backing) ||
        !section_available(directory, selection.string_section) ||
        !section_available(directory, selection.line_string_section) ||
        !section_available(directory, selection.offset_section) ||
        tb_symbol_attributes_init(&lookup, tree, (TbSymbolAttributeSelection_t){
            .die_index = selection.die_index,
            .name      = selection.name
        }) != TB_SYMBOL_ATTRIBUTE_PENDING || !construction ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolText_t))) {
        return NULL;
    }
    TbSymbolText_t *text = calloc(1, sizeof(*text));
    if (!text) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolText_t));
        return NULL;
    }
    text->construction = construction;
    text->directory    = directory;
    text->units        = units;
    text->tree         = tree;
    text->selection    = selection;
    text->lookup       = lookup;
    return text;
}


static void finish(TbSymbolText_t *text, TbSymbolTextOutcome_e outcome, TbSymbolTextFailure_e reason)
{
    text->outcome        = outcome;
    text->failure.reason = reason;
    text->phase          = TEXT_DONE;
}


static TbSymbolTextOutcome_e child_outcome(TbSymbolOutcome_e outcome)
{
    switch (outcome) {
    case TB_SYMBOL_MALFORMED:
        return TB_SYMBOL_TEXT_MALFORMED;
    case TB_SYMBOL_UNSUPPORTED:
        return TB_SYMBOL_TEXT_UNSUPPORTED;
    case TB_SYMBOL_RESOURCE_FAILURE:
        return TB_SYMBOL_TEXT_RESOURCE;
    default:
        return TB_SYMBOL_TEXT_OPERATIONAL;
    }
}


static bool query(TbSymbolText_t *text, bool base)
{
    TbSymbolAttributeStatus_e status = tb_symbol_attributes_step(&text->lookup, 256);
    if (status == TB_SYMBOL_ATTRIBUTE_PENDING)
        return false;
    if (status != TB_SYMBOL_ATTRIBUTE_UNIQUE) {
        (void)tb_symbol_attributes_result(&text->lookup, &text->failure.attributes);
        if (status == TB_SYMBOL_ATTRIBUTE_MISSING) {
            finish(text, base ? TB_SYMBOL_TEXT_DEPENDENCY : TB_SYMBOL_TEXT_ABSENT,
                   base ? TB_SYMBOL_TEXT_BASE_ABSENT : TB_SYMBOL_TEXT_FIELD_ABSENT);
        } else if (status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS) {
            finish(text, TB_SYMBOL_TEXT_CONFLICT,
                   base ? TB_SYMBOL_TEXT_BASE_AMBIGUOUS : TB_SYMBOL_TEXT_FIELD_AMBIGUOUS);
        } else {
            finish(text, TB_SYMBOL_TEXT_OPERATIONAL, TB_SYMBOL_TEXT_REQUEST);
        }
        return false;
    }
    return true;
}


static void begin_string(TbSymbolText_t *text, TbSymbolStringSelection_t selection)
{
    text->string = tb_symbol_string_create(text->directory, selection, text->construction);
    if (!text->string) {
        finish(text, TB_SYMBOL_TEXT_RESOURCE, TB_SYMBOL_TEXT_ALLOCATION);
    } else {
        text->phase = TEXT_STRING;
    }
}


static void indirect_string(TbSymbolText_t *text, uint64_t offset, bool line)
{
    uint64_t section = line ? text->selection.line_string_section : text->selection.string_section;
    if (section == UINT64_MAX) {
        finish(text, TB_SYMBOL_TEXT_DEPENDENCY,
               line ? TB_SYMBOL_TEXT_LINE_STRING_SECTION : TB_SYMBOL_TEXT_STRING_SECTION);
        return;
    }
    begin_string(text, (TbSymbolStringSelection_t){
        .section_index = section,
        .offset        = offset,
        .end_offset    = text->directory.entries[section].header.bytes,
        .descriptor    = text->selection.descriptor
    });
}


static void classify(TbSymbolText_t *text)
{
    const TbSymbolDieAttribute_t *attribute = &text->view.attribute;
    uint64_t form = attribute->value.form;
    if (form == TB_SYMBOL_FORM_STRING) {
        uint64_t end;
        uint64_t field_end;
        const TbSymbolDieEntry_t *die = &text->tree.entries[text->selection.die_index];
        if (attribute->offset <= die->offset ||
            !tb_symbol_add(attribute->offset, attribute->value.bytes, &field_end) ||
            attribute->value.payload_offset < attribute->offset ||
            !tb_symbol_add(attribute->value.payload_offset, attribute->value.payload_bytes, &end) ||
            !tb_symbol_add(end, 1, &end) || end != field_end || end > die->end_offset) {
            finish(text, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_SPAN);
            return;
        }
        begin_string(text, (TbSymbolStringSelection_t){
            .section_index = text->tree.section_index,
            .offset        = attribute->value.payload_offset,
            .end_offset    = end,
            .descriptor    = text->selection.descriptor
        });
        return;
    }
    bool indexed = form == TB_SYMBOL_FORM_STRX ||
                   (form >= TB_SYMBOL_FORM_STRX1 && form <= TB_SYMBOL_FORM_STRX4);
    const TbSymbolDieEntry_t *die = &text->tree.entries[text->selection.die_index];
    uint64_t field_end;
    if (form != TB_SYMBOL_FORM_STRP && form != TB_SYMBOL_FORM_LINE_STRP && !indexed) {
        finish(text, TB_SYMBOL_TEXT_UNSUPPORTED, TB_SYMBOL_TEXT_FORM);
    } else if (!attribute->value.bytes || attribute->offset <= die->offset ||
               !tb_symbol_add(attribute->offset, attribute->value.bytes, &field_end) ||
               field_end > die->end_offset) {
        finish(text, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_SPAN);
    } else if (!attribute->value.numeric || attribute->value.is_signed) {
        finish(text, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_VALUE);
    } else if (!indexed) {
        indirect_string(text, attribute->value.bits, form == TB_SYMBOL_FORM_LINE_STRP);
    } else if (text->units.entries[text->selection.unit_index].header.version != 5) {
        finish(text, TB_SYMBOL_TEXT_UNSUPPORTED, TB_SYMBOL_TEXT_VERSION);
    } else if (text->selection.offset_section == UINT64_MAX) {
        finish(text, TB_SYMBOL_TEXT_DEPENDENCY, TB_SYMBOL_TEXT_OFFSET_SECTION);
    } else if (text->selection.string_section == UINT64_MAX) {
        finish(text, TB_SYMBOL_TEXT_DEPENDENCY, TB_SYMBOL_TEXT_STRING_SECTION);
    } else if (tb_symbol_attributes_init(&text->lookup, text->tree, (TbSymbolAttributeSelection_t){
        .die_index = 0,
        .name      = 0x72
    }) != TB_SYMBOL_ATTRIBUTE_PENDING) {
        finish(text, TB_SYMBOL_TEXT_OPERATIONAL, TB_SYMBOL_TEXT_REQUEST);
    } else {
        text->view.indexed = true;
        text->phase = TEXT_BASE;
    }
}


static void base(TbSymbolText_t *text)
{
    uint64_t index;
    if (!tb_symbol_attributes_unique(&text->lookup, &index)) {
        finish(text, TB_SYMBOL_TEXT_OPERATIONAL, TB_SYMBOL_TEXT_REQUEST);
        return;
    }
    text->view.base_attribute = text->tree.attributes[index];
    const TbSymbolFormView_t *value = &text->view.base_attribute.value;
    if (value->form != TB_SYMBOL_FORM_SEC_OFFSET) {
        finish(text, TB_SYMBOL_TEXT_UNSUPPORTED, TB_SYMBOL_TEXT_FORM);
    } else if (!value->numeric || value->is_signed) {
        finish(text, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_VALUE);
    } else {
        text->offsets = tb_symbol_string_offsets_create(text->directory, (TbSymbolStringOffsetSelection_t){
            .section_index = text->selection.offset_section,
            .base          = value->bits,
            .index         = text->view.attribute.value.bits,
            .offset_bytes  = text->units.entries[text->selection.unit_index].extent.offset_bytes,
            .descriptor    = text->selection.descriptor
        }, text->construction);
        if (!text->offsets)
            finish(text, TB_SYMBOL_TEXT_RESOURCE, TB_SYMBOL_TEXT_ALLOCATION);
        else
            text->phase = TEXT_OFFSETS;
    }
}


static bool offsets(TbSymbolText_t *text, size_t max_bytes)
{
    uint64_t before = tb_symbol_string_offsets_progress(text->offsets);
    TbSymbolOutcome_e outcome = tb_symbol_string_offsets_step(text->offsets, max_bytes);
    bool progressed = before != tb_symbol_string_offsets_progress(text->offsets);
    if (outcome == TB_SYMBOL_SUCCESS) {
        if (!tb_symbol_string_offsets_view(text->offsets, &text->view.offsets)) {
            finish(text, TB_SYMBOL_TEXT_OPERATIONAL, TB_SYMBOL_TEXT_REQUEST);
        } else {
            tb_symbol_string_offsets_destroy(text->offsets);
            text->offsets = NULL;
            indirect_string(text, text->view.offsets.string_offset, false);
        }
    } else if (outcome != TB_SYMBOL_PENDING) {
        text->failure.offsets = tb_symbol_string_offsets_failure(text->offsets);
        finish(text, child_outcome(outcome), TB_SYMBOL_TEXT_OFFSETS);
    }
    return progressed;
}


static bool string(TbSymbolText_t *text, size_t max_bytes)
{
    uint64_t before = tb_symbol_string_progress(text->string);
    TbSymbolOutcome_e outcome = tb_symbol_string_step(text->string, max_bytes);
    bool progressed = before != tb_symbol_string_progress(text->string);
    if (outcome == TB_SYMBOL_SUCCESS) {
        if (!tb_symbol_string_view(text->string, &text->view.text)) {
            finish(text, TB_SYMBOL_TEXT_OPERATIONAL, TB_SYMBOL_TEXT_REQUEST);
        } else if (text->view.attribute.value.form == TB_SYMBOL_FORM_STRING &&
                   text->view.text.bytes != text->view.attribute.value.payload_bytes) {
            finish(text, TB_SYMBOL_TEXT_MALFORMED, TB_SYMBOL_TEXT_SPAN);
        } else {
            finish(text, TB_SYMBOL_TEXT_FOUND, TB_SYMBOL_TEXT_NONE);
        }
    } else if (outcome != TB_SYMBOL_PENDING) {
        text->failure.string = tb_symbol_string_failure(text->string);
        finish(text, child_outcome(outcome), TB_SYMBOL_TEXT_STRING);
    }
    return progressed;
}


TbSymbolTextOutcome_e tb_symbol_text_step(TbSymbolText_t *text, size_t max_bytes)
{
    if (!text)
        return TB_SYMBOL_TEXT_OPERATIONAL;
    if (text->phase == TEXT_DONE)
        return text->outcome;
    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        finish(text, TB_SYMBOL_TEXT_OPERATIONAL, TB_SYMBOL_TEXT_REQUEST);
        return text->outcome;
    }
    bool progressed = true;
    switch (text->phase) {
    case TEXT_FIELD: {
        uint64_t before = text->lookup.cursor;
        TbSymbolAttributeStatus_e status = text->lookup.status;
        if (query(text, false)) {
            uint64_t index;
            if (!tb_symbol_attributes_unique(&text->lookup, &index)) {
                finish(text, TB_SYMBOL_TEXT_OPERATIONAL, TB_SYMBOL_TEXT_REQUEST);
            } else {
                text->view.backing        = text->tree.backing;
                text->view.source_section = text->tree.section_index;
                text->view.source_unit    = text->tree.unit_offset;
                text->view.source_record  = text->tree.entries[text->selection.die_index].offset;
                text->view.abbrev_section = text->tree.abbrev_section;
                text->view.abbrev_offset  = text->tree.abbrev_offset;
                text->view.attribute      = text->tree.attributes[index];
                text->phase              = TEXT_CLASSIFY;
            }
        }
        progressed = before != text->lookup.cursor || status != text->lookup.status;
        break;
    }
    case TEXT_CLASSIFY:
        classify(text);
        break;
    case TEXT_BASE: {
        uint64_t before = text->lookup.cursor;
        TbSymbolAttributeStatus_e status = text->lookup.status;
        if (query(text, true))
            base(text);
        progressed = before != text->lookup.cursor || status != text->lookup.status;
        break;
    }
    case TEXT_OFFSETS:
        progressed = offsets(text, max_bytes);
        break;
    case TEXT_STRING:
        progressed = string(text, max_bytes);
        break;
    case TEXT_DONE:
        break;
    }
    if (progressed) {
        if (text->progress == UINT64_MAX)
            finish(text, TB_SYMBOL_TEXT_OPERATIONAL, TB_SYMBOL_TEXT_REQUEST);
        else
            ++text->progress;
    }
    return text->outcome;
}


bool tb_symbol_text_view(const TbSymbolText_t *text, TbSymbolTextView_t *view)
{
    if (!text || !view || text->phase != TEXT_DONE || text->outcome != TB_SYMBOL_TEXT_FOUND)
        return false;
    *view = text->view;
    return true;
}


TbSymbolTextFailure_t tb_symbol_text_failure(const TbSymbolText_t *text)
{
    return text ? text->failure : (TbSymbolTextFailure_t){
        .reason = TB_SYMBOL_TEXT_REQUEST
    };
}


uint64_t tb_symbol_text_progress(const TbSymbolText_t *text)
{
    return text ? text->progress : 0;
}


void tb_symbol_text_destroy(TbSymbolText_t *text)
{
    if (!text)
        return;
    tb_symbol_string_offsets_destroy(text->offsets);
    tb_symbol_string_destroy(text->string);
    (void)tb_symbol_budget_release(text->construction, sizeof(*text));
    free(text);
}
