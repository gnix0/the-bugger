#include "symbol/dies.h"

#include <stdlib.h>

typedef enum {
    DIES_CODE,
    DIES_ATTRIBUTE,
    DIES_VALUE,
    DIES_ENTRY,
    DIES_ALLOCATE,
    DIES_DONE,
    DIES_FAILED
} DiesPhase_e;

struct tb_symbol_dies {
    TbSymbolBudget_t        *construction;
    TbSymbolFile_t          *file;
    TbSymbolSectionsView_t   directory;
    uint64_t                section_index;
    TbSymbolUnitEntry_t      unit;
    TbSymbolDieSelection_t   selection;
    const TbSymbolAbbrev_t  *abbrev;
    TbSymbolAbbrevView_t     schema;
    TbSymbolElfSection_t     section;

    TbSymbolDieEntry_t      *entries;
    TbSymbolDieAttribute_t  *attributes;
    uint64_t               *parents;
    size_t                  array_bytes;
    uint64_t                count;
    uint64_t                attribute_count;
    uint64_t                max_depth;
    uint64_t                written;
    uint64_t                attributes_written;
    uint64_t                depth;
    bool                    populating;
    bool                    root_seen;
    bool                    root_closed;
    bool                    type_found;
    TbSymbolDwarfUnitKind_e  kind;

    const TbSymbolAbbrevEntry_t *declaration;
    TbSymbolDieEntry_t           current;
    TbSymbolDieAttribute_t       attribute;
    uint64_t                    attribute_index;
    TbSymbolLeb_t               code;
    TbSymbolForm_t              form;
    uint64_t                    cursor;
    uint64_t                    field_start;
    const unsigned char        *window;
    uint64_t                    window_start;
    size_t                      window_bytes;
    bool                        reading;

    uint64_t              progress;
    DiesPhase_e           phase;
    TbSymbolOutcome_e     outcome;
    TbSymbolDiesFailure_t failure;
};

static bool same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.bytes == right.bytes && left.key.device == right.key.device &&
           left.key.inode == right.key.inode;
}


TbSymbolDies_t *tb_symbol_dies_create(TbSymbolSectionsView_t directory, TbSymbolUnitsView_t units,
                                     TbSymbolDieSelection_t selection, const TbSymbolAbbrev_t *abbrev,
                                     TbSymbolBudget_t *construction)
{
    TbSymbolAbbrevView_t schema;
    if (!directory.header || !directory.layout || !directory.entries || !units.entries ||
        directory.count != directory.layout->sections.count ||
        directory.backing.bytes != directory.header->file_bytes ||
        units.section_index >= directory.count || selection.unit_index >= units.count ||
        selection.descriptor < 0 || !units.entries[selection.unit_index].header_available ||
        !same_backing(directory.backing, units.backing) || !tb_symbol_abbrev_view(abbrev, &schema) ||
        !same_backing(directory.backing, schema.backing) || !construction) {
        return NULL;
    }
    TbSymbolUnitEntry_t unit = units.entries[selection.unit_index];
    uint64_t body;
    if (!tb_symbol_add(unit.offset, unit.header.header_bytes, &body) ||
        unit.extent.end_offset > directory.entries[units.section_index].header.bytes ||
        body >= unit.extent.end_offset || schema.version != unit.header.version ||
        schema.table_offset != unit.header.abbrev_offset ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolDies_t))) {
        return NULL;
    }
    TbSymbolDies_t *dies = calloc(1, sizeof(*dies));
    if (!dies) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolDies_t));
        return NULL;
    }
    dies->construction  = construction;
    dies->directory     = directory;
    dies->section_index = units.section_index;
    dies->selection     = selection;
    dies->unit          = unit;
    dies->abbrev        = abbrev;
    dies->schema        = schema;
    dies->section       = directory.entries[units.section_index].header;
    dies->cursor        = body;
    dies->field_start   = dies->cursor;
    dies->kind          = unit.header.kind;
    (void)tb_symbol_leb_init(&dies->code, false);
    dies->file = tb_symbol_file_create(directory.backing, selection.descriptor, construction);
    if (!dies->file) {
        tb_symbol_dies_destroy(dies);
        return NULL;
    }
    return dies;
}


static void fail(TbSymbolDies_t *dies, TbSymbolDiesFailure_e reason, TbSymbolOutcome_e outcome)
{
    dies->failure.reason = reason;
    dies->failure.offset = dies->field_start;
    dies->failure.code   = dies->current.code;
    dies->phase          = DIES_FAILED;
    dies->outcome        = outcome;
}


static bool window(TbSymbolDies_t *dies)
{
    if (dies->cursor >= dies->window_start && dies->cursor - dies->window_start < dies->window_bytes)
        return true;
    uint64_t offset;
    uint64_t remaining = dies->unit.extent.end_offset - dies->cursor;
    if (!remaining || !tb_symbol_add(dies->section.offset, dies->cursor, &offset) ||
        !tb_symbol_file_request(dies->file, (TbSymbolFileRange_t){
            .offset = offset,
            .bytes  = remaining < TB_SYMBOL_FILE_WINDOW_BYTES
                          ? (size_t)remaining : TB_SYMBOL_FILE_WINDOW_BYTES
        })) {
        fail(dies, TB_SYMBOL_DIES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return false;
    }
    dies->window_start = dies->cursor;
    dies->window_bytes = 0;
    dies->reading = true;
    return false;
}


static void next_code(TbSymbolDies_t *dies)
{
    dies->field_start = dies->cursor;
    (void)tb_symbol_leb_init(&dies->code, false);
    dies->phase = DIES_CODE;
}


static bool root(TbSymbolDies_t *dies, uint64_t tag)
{
    TbSymbolDwarfUnitKind_e kind;
    if (tag == 0x11) {
        kind = TB_SYMBOL_DWARF_COMPILE;
    } else if (tag == 0x3c) {
        kind = TB_SYMBOL_DWARF_PARTIAL;
    } else if (tag == 0x41) {
        kind = TB_SYMBOL_DWARF_TYPE;
    } else {
        fail(dies, TB_SYMBOL_DIES_ROOT, TB_SYMBOL_UNSUPPORTED);
        return false;
    }
    if (dies->unit.header.kind != kind &&
        !(dies->unit.header.kind == TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL && kind != TB_SYMBOL_DWARF_TYPE)) {
        fail(dies, TB_SYMBOL_DIES_ROOT, TB_SYMBOL_MALFORMED);
        return false;
    }
    dies->kind = kind;
    return true;
}


static void begin_entry(TbSymbolDies_t *dies, uint64_t code)
{
    if (!code) {
        if (dies->depth) {
            --dies->depth;
            if (!dies->depth)
                dies->root_closed = true;
        }
        next_code(dies);
        return;
    }
    dies->current.code = code;
    if (dies->root_closed) {
        fail(dies, TB_SYMBOL_DIES_TREE, TB_SYMBOL_MALFORMED);
        return;
    }
    if (tb_symbol_abbrev_find(dies->abbrev, code, &dies->declaration) != TB_SYMBOL_ABBREV_FOUND) {
        dies->current.code = code;
        fail(dies, TB_SYMBOL_DIES_CODE, TB_SYMBOL_MALFORMED);
        return;
    }
    if (!dies->root_seen && !root(dies, dies->declaration->tag))
        return;
    dies->root_seen = true;
    dies->current = (TbSymbolDieEntry_t){
        .offset          = dies->field_start,
        .parent          = dies->populating && dies->depth ? dies->parents[dies->depth - 1] : UINT64_MAX,
        .code            = code,
        .tag             = dies->declaration->tag,
        .first_attribute = dies->populating ? dies->attributes_written : dies->attribute_count,
        .attribute_count = dies->declaration->attribute_count,
        .children        = dies->declaration->children
    };
    if (dies->unit.header.has_signature &&
        dies->current.offset - dies->unit.offset == dies->unit.header.type_offset) {
        dies->type_found = true;
    }
    dies->attribute_index = 0;
    dies->phase = DIES_ATTRIBUTE;
}


static void code(TbSymbolDies_t *dies, size_t max_bytes)
{
    if (!window(dies))
        return;
    size_t at = (size_t)(dies->cursor - dies->window_start);
    size_t available = dies->window_bytes - at;
    if (available > max_bytes)
        available = max_bytes;
    size_t consumed;
    TbSymbolReadStatus_e status = tb_symbol_leb_feed(&dies->code, dies->window + at, available, &consumed);
    if (status == TB_SYMBOL_READ_OK || status == TB_SYMBOL_READ_LIMIT) {
        dies->cursor += consumed;
        if (status == TB_SYMBOL_READ_OK) {
            uint64_t value;
            if (tb_symbol_leb_uint(&dies->code, &value))
                begin_entry(dies, value);
            else
                fail(dies, TB_SYMBOL_DIES_CODE, TB_SYMBOL_UNSUPPORTED);
        }
    } else {
        fail(dies, TB_SYMBOL_DIES_CODE, TB_SYMBOL_OPERATIONAL_FAILURE);
    }
}


static void add_attribute(TbSymbolDies_t *dies)
{
    if (!tb_symbol_form_view(&dies->form, &dies->attribute.value) ||
        !tb_symbol_add(dies->attribute.offset, dies->attribute.value.payload_offset,
                       &dies->attribute.value.payload_offset)) {
        fail(dies, TB_SYMBOL_DIES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    if (dies->populating) {
        if (dies->attributes_written >= dies->attribute_count) {
            fail(dies, TB_SYMBOL_DIES_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        dies->attributes[dies->attributes_written++] = dies->attribute;
    } else if (dies->attribute_count == UINT64_MAX) {
        fail(dies, TB_SYMBOL_DIES_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    } else {
        ++dies->attribute_count;
    }
    ++dies->attribute_index;
    dies->phase = DIES_ATTRIBUTE;
}


static void form_status(TbSymbolDies_t *dies, TbSymbolFormStatus_e status)
{
    if (status == TB_SYMBOL_FORM_COMPLETE) {
        add_attribute(dies);
    } else if (status != TB_SYMBOL_FORM_PENDING) {
        dies->failure.form = status;
        TbSymbolOutcome_e outcome = status == TB_SYMBOL_FORM_UNSUPPORTED ? TB_SYMBOL_UNSUPPORTED
                                       : status == TB_SYMBOL_FORM_TRUNCATED ? TB_SYMBOL_MALFORMED
                                                                          : TB_SYMBOL_OPERATIONAL_FAILURE;
        fail(dies, TB_SYMBOL_DIES_FORM, outcome);
    }
}


static void attribute(TbSymbolDies_t *dies)
{
    if (dies->attribute_index == dies->declaration->attribute_count) {
        dies->phase = DIES_ENTRY;
        return;
    }
    uint64_t index = dies->declaration->first_attribute + dies->attribute_index;
    const TbSymbolAbbrevAttribute_t *schema = &dies->schema.attributes[index];
    dies->field_start = dies->cursor;
    dies->attribute = (TbSymbolDieAttribute_t){
        .schema_attribute = index,
        .name             = schema->name,
        .declared_form    = schema->form,
        .offset           = dies->cursor
    };
    TbSymbolFormStatus_e status = tb_symbol_form_init(&dies->form, (TbSymbolFormRequest_t){
        .form          = schema->form,
        .available     = dies->unit.extent.end_offset - dies->cursor,
        .version       = dies->unit.header.version,
        .offset_bytes  = dies->unit.extent.offset_bytes,
        .address_bytes = dies->unit.header.address_bytes,
        .byte_order    = dies->directory.header->byte_order
    });
    dies->phase = DIES_VALUE;
    form_status(dies, status);
}


static void value(TbSymbolDies_t *dies, size_t max_bytes)
{
    if (dies->cursor == dies->unit.extent.end_offset) {
        form_status(dies, tb_symbol_form_finish(&dies->form));
        return;
    }
    if (!window(dies))
        return;
    size_t at = (size_t)(dies->cursor - dies->window_start);
    size_t available = dies->window_bytes - at;
    if (available > max_bytes)
        available = max_bytes;
    size_t consumed;
    TbSymbolFormStatus_e status = tb_symbol_form_feed(&dies->form, dies->window + at, available, &consumed);
    dies->cursor += consumed;
    form_status(dies, status);
}


static void entry(TbSymbolDies_t *dies)
{
    dies->current.end_offset = dies->cursor;
    if (dies->populating) {
        if (dies->written >= dies->count) {
            fail(dies, TB_SYMBOL_DIES_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        dies->entries[dies->written] = dies->current;
    } else if (dies->count == UINT64_MAX) {
        fail(dies, TB_SYMBOL_DIES_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }
    if (dies->current.children) {
        if (dies->depth == UINT64_MAX) {
            fail(dies, TB_SYMBOL_DIES_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
            return;
        }
        if (dies->populating) {
            if (dies->depth >= dies->max_depth) {
                fail(dies, TB_SYMBOL_DIES_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
                return;
            }
            dies->parents[dies->depth] = dies->written;
        }
        ++dies->depth;
        if (!dies->populating && dies->depth > dies->max_depth)
            dies->max_depth = dies->depth;
    } else if (!dies->depth) {
        dies->root_closed = true;
    }
    if (dies->populating)
        ++dies->written;
    else
        ++dies->count;
    next_code(dies);
}


static void finish_pass(TbSymbolDies_t *dies)
{
    if (!dies->root_seen || !dies->root_closed || dies->depth ||
        tb_symbol_leb_finish(&dies->code) != TB_SYMBOL_READ_TRUNCATED || dies->code.bytes) {
        fail(dies, TB_SYMBOL_DIES_TREE, TB_SYMBOL_MALFORMED);
    } else if (dies->unit.header.has_signature && !dies->type_found) {
        fail(dies, TB_SYMBOL_DIES_TYPE_OFFSET, TB_SYMBOL_MALFORMED);
    } else if (!dies->populating) {
        dies->phase = DIES_ALLOCATE;
    } else if (dies->written != dies->count || dies->attributes_written != dies->attribute_count) {
        fail(dies, TB_SYMBOL_DIES_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else {
        dies->phase = DIES_DONE;
        dies->outcome = TB_SYMBOL_SUCCESS;
    }
}


static void allocate(TbSymbolDies_t *dies)
{
    uint64_t entries;
    uint64_t attributes;
    uint64_t parents;
    uint64_t total;
    if (!tb_symbol_multiply(dies->count, sizeof(*dies->entries), &entries) ||
        !tb_symbol_multiply(dies->attribute_count, sizeof(*dies->attributes), &attributes) ||
        !tb_symbol_multiply(dies->max_depth, sizeof(*dies->parents), &parents) ||
        !tb_symbol_add(entries, attributes, &total) || !tb_symbol_add(total, parents, &total) ||
        total > SIZE_MAX || !tb_symbol_budget_reserve(dies->construction, (size_t)total)) {
        fail(dies, TB_SYMBOL_DIES_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }
    dies->array_bytes = (size_t)total;
    dies->entries = malloc((size_t)entries);
    if (attributes)
        dies->attributes = malloc((size_t)attributes);
    if (parents)
        dies->parents = malloc((size_t)parents);
    if (!dies->entries || (attributes && !dies->attributes) || (parents && !dies->parents)) {
        fail(dies, TB_SYMBOL_DIES_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }
    dies->populating = true;
    dies->cursor = dies->unit.offset + dies->unit.header.header_bytes;
    dies->window_bytes = 0;
    dies->root_seen = false;
    dies->root_closed = false;
    dies->type_found = false;
    next_code(dies);
}


TbSymbolOutcome_e tb_symbol_dies_step(TbSymbolDies_t *dies, size_t max_bytes)
{
    if (!dies)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (dies->phase == DIES_DONE || dies->phase == DIES_FAILED)
        return dies->outcome;
    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        fail(dies, TB_SYMBOL_DIES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return dies->outcome;
    }
    bool progressed = true;
    if (dies->reading) {
        uint64_t before = tb_symbol_file_progress(dies->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(dies->file, max_bytes);
        progressed = before != tb_symbol_file_progress(dies->file);
        if (outcome == TB_SYMBOL_SUCCESS) {
            TbSymbolFileView_t view;
            if (tb_symbol_file_view(dies->file, &view)) {
                dies->window = view.data;
                dies->window_bytes = view.bytes;
                dies->reading = false;
            } else {
                fail(dies, TB_SYMBOL_DIES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
            }
        } else if (outcome != TB_SYMBOL_PENDING) {
            dies->failure.read = tb_symbol_file_failure(dies->file);
            fail(dies, TB_SYMBOL_DIES_READ, outcome);
        }
    } else {
        switch (dies->phase) {
        case DIES_CODE:
            if (dies->cursor == dies->unit.extent.end_offset)
                finish_pass(dies);
            else
                code(dies, max_bytes);
            break;
        case DIES_ATTRIBUTE:
            attribute(dies);
            break;
        case DIES_VALUE:
            value(dies, max_bytes);
            break;
        case DIES_ENTRY:
            entry(dies);
            break;
        case DIES_ALLOCATE:
            allocate(dies);
            break;
        case DIES_DONE:
        case DIES_FAILED:
            break;
        }
    }
    if (progressed) {
        if (dies->progress == UINT64_MAX)
            fail(dies, TB_SYMBOL_DIES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++dies->progress;
    }
    return dies->outcome;
}


bool tb_symbol_dies_view(const TbSymbolDies_t *dies, TbSymbolDiesView_t *view)
{
    if (!dies || !view || dies->phase != DIES_DONE || dies->outcome != TB_SYMBOL_SUCCESS)
        return false;
    *view = (TbSymbolDiesView_t){
        .entries         = dies->entries,
        .attributes      = dies->attributes,
        .count           = dies->count,
        .attribute_count = dies->attribute_count,
        .max_depth       = dies->max_depth,
        .section_index   = dies->section_index,
        .unit_offset     = dies->unit.offset,
        .abbrev_section  = dies->schema.section_index,
        .abbrev_offset   = dies->schema.table_offset,
        .kind            = dies->kind,
        .backing         = dies->directory.backing
    };
    return true;
}


TbSymbolDiesFailure_t tb_symbol_dies_failure(const TbSymbolDies_t *dies)
{
    return dies ? dies->failure : (TbSymbolDiesFailure_t){
        .reason = TB_SYMBOL_DIES_REQUEST,
        .offset = UINT64_MAX
    };
}


uint64_t tb_symbol_dies_progress(const TbSymbolDies_t *dies)
{
    return dies ? dies->progress : 0;
}


void tb_symbol_dies_destroy(TbSymbolDies_t *dies)
{
    if (!dies)
        return;
    tb_symbol_file_destroy(dies->file);
    free(dies->entries);
    free(dies->attributes);
    free(dies->parents);
    if (dies->array_bytes)
        (void)tb_symbol_budget_release(dies->construction, dies->array_bytes);
    (void)tb_symbol_budget_release(dies->construction, sizeof(*dies));
    free(dies);
}
