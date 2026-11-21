#include "symbol/abbrev.h"
#include "symbol/leb.h"

#include <elf.h>
#include <errno.h>
#include <stdlib.h>

typedef enum {
    ABBREV_SECTION,
    ABBREV_PARSE,
    ABBREV_ALLOCATE,
    ABBREV_SORT,
    ABBREV_CHECK,
    ABBREV_DONE,
    ABBREV_FAILED
} AbbrevPhase_e;

typedef enum {
    FIELD_CODE,
    FIELD_TAG,
    FIELD_CHILDREN,
    FIELD_NAME,
    FIELD_FORM,
    FIELD_IMPLICIT
} AbbrevField_e;

struct tb_symbol_abbrev {
    TbSymbolBudget_t         *construction;
    TbSymbolFile_t           *file;
    TbSymbolSectionsView_t    directory;
    TbSymbolAbbrevSelection_t selection;
    TbSymbolElfSection_t      section;

    TbSymbolAbbrevEntry_t     *entries;
    TbSymbolAbbrevAttribute_t *attributes;
    uint64_t                 *order;
    size_t                    array_bytes;
    uint64_t                  count;
    uint64_t                  attribute_count;
    uint64_t                  written;
    uint64_t                  attributes_written;
    uint64_t                  end_offset;

    TbSymbolAbbrevEntry_t     current;
    TbSymbolAbbrevAttribute_t attribute;
    TbSymbolLeb_t            leb;
    uint64_t                 cursor;
    uint64_t                 field_start;
    const unsigned char     *window;
    uint64_t                 window_start;
    size_t                   window_bytes;
    bool                     populating;
    bool                     reading;

    uint64_t sort_build;
    uint64_t sort_remaining;
    uint64_t root;
    uint64_t heap;
    uint64_t check_index;
    bool     sifting;

    uint64_t                progress;
    AbbrevPhase_e           phase;
    AbbrevField_e           field;
    TbSymbolOutcome_e       outcome;
    TbSymbolAbbrevFailure_t failure;
};

TbSymbolAbbrev_t *tb_symbol_abbrev_create(TbSymbolSectionsView_t directory,
                                         TbSymbolAbbrevSelection_t selection,
                                         TbSymbolBudget_t *construction)
{
    if (!directory.header || !directory.layout || !directory.entries || selection.descriptor < 0 ||
        selection.section_index >= directory.count || directory.count != directory.layout->sections.count ||
        directory.backing.bytes != directory.header->file_bytes ||
        (selection.version != 4 && selection.version != 5) || !construction ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolAbbrev_t))) {
        return NULL;
    }
    TbSymbolAbbrev_t *abbrev = calloc(1, sizeof(*abbrev));
    if (!abbrev) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolAbbrev_t));
        return NULL;
    }
    abbrev->construction = construction;
    abbrev->directory    = directory;
    abbrev->selection    = selection;
    abbrev->section      = directory.entries[selection.section_index].header;
    abbrev->cursor       = selection.table_offset;
    abbrev->field_start  = selection.table_offset;
    abbrev->file = tb_symbol_file_create(directory.backing, selection.descriptor, construction);
    if (!abbrev->file) {
        tb_symbol_abbrev_destroy(abbrev);
        return NULL;
    }
    return abbrev;
}


static void fail(TbSymbolAbbrev_t *abbrev, TbSymbolAbbrevFailure_e reason, TbSymbolOutcome_e outcome)
{
    abbrev->failure.reason         = reason;
    abbrev->failure.offset         = abbrev->field_start;
    abbrev->failure.related_offset = UINT64_MAX;
    abbrev->outcome                = outcome;
    abbrev->phase                  = ABBREV_FAILED;
}


static void field(TbSymbolAbbrev_t *abbrev, AbbrevField_e next)
{
    abbrev->field = next;
    abbrev->field_start = abbrev->cursor;
    (void)tb_symbol_leb_init(&abbrev->leb, next == FIELD_IMPLICIT);
}


static bool window(TbSymbolAbbrev_t *abbrev)
{
    if (abbrev->cursor >= abbrev->section.bytes) {
        fail(abbrev, TB_SYMBOL_ABBREV_TRUNCATED, TB_SYMBOL_MALFORMED);
        return false;
    }
    if (abbrev->cursor >= abbrev->window_start &&
        abbrev->cursor - abbrev->window_start < abbrev->window_bytes) {
        return true;
    }
    uint64_t offset;
    uint64_t remaining = abbrev->section.bytes - abbrev->cursor;
    if (!tb_symbol_add(abbrev->section.offset, abbrev->cursor, &offset)) {
        fail(abbrev, TB_SYMBOL_ABBREV_RANGE, TB_SYMBOL_MALFORMED);
        return false;
    }
    if (!tb_symbol_file_request(abbrev->file, (TbSymbolFileRange_t){
        .offset = offset,
        .bytes  = remaining < TB_SYMBOL_FILE_WINDOW_BYTES ? (size_t)remaining : TB_SYMBOL_FILE_WINDOW_BYTES
    })) {
        fail(abbrev, TB_SYMBOL_ABBREV_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else {
        abbrev->window_start = abbrev->cursor;
        abbrev->window_bytes = 0;
        abbrev->reading = true;
    }
    return false;
}


static bool known_form(const TbSymbolAbbrev_t *abbrev, uint64_t form)
{
    return form == TB_SYMBOL_FORM_ADDR ||
           (form >= TB_SYMBOL_FORM_BLOCK2 && form <= TB_SYMBOL_FORM_FLAG_PRESENT) ||
           form == TB_SYMBOL_FORM_REF_SIG8 ||
           (abbrev->selection.version == 5 && form >= TB_SYMBOL_FORM_STRX && form <= TB_SYMBOL_FORM_ADDRX4);
}


static void add_attribute(TbSymbolAbbrev_t *abbrev)
{
    if (abbrev->populating) {
        if (abbrev->attributes_written >= abbrev->attribute_count) {
            fail(abbrev, TB_SYMBOL_ABBREV_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        abbrev->attributes[abbrev->attributes_written++] = abbrev->attribute;
    } else if (abbrev->attribute_count == UINT64_MAX) {
        fail(abbrev, TB_SYMBOL_ABBREV_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    } else {
        ++abbrev->attribute_count;
    }
    field(abbrev, FIELD_NAME);
}


static void add_entry(TbSymbolAbbrev_t *abbrev)
{
    abbrev->current.end_offset = abbrev->cursor;
    uint64_t attributes = abbrev->populating ? abbrev->attributes_written : abbrev->attribute_count;
    abbrev->current.attribute_count = attributes - abbrev->current.first_attribute;
    if (abbrev->populating) {
        if (abbrev->written >= abbrev->count) {
            fail(abbrev, TB_SYMBOL_ABBREV_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        abbrev->order[abbrev->written] = abbrev->written;
        abbrev->entries[abbrev->written++] = abbrev->current;
    } else if (abbrev->count == UINT64_MAX) {
        fail(abbrev, TB_SYMBOL_ABBREV_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    } else {
        ++abbrev->count;
    }
    field(abbrev, FIELD_CODE);
}


static void finish_pass(TbSymbolAbbrev_t *abbrev)
{
    if (!abbrev->populating) {
        abbrev->end_offset = abbrev->cursor;
        abbrev->phase = ABBREV_ALLOCATE;
    } else if (abbrev->written != abbrev->count || abbrev->attributes_written != abbrev->attribute_count ||
               abbrev->cursor != abbrev->end_offset) {
        fail(abbrev, TB_SYMBOL_ABBREV_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else {
        abbrev->sort_build = abbrev->count / 2;
        abbrev->sort_remaining = abbrev->count;
        abbrev->phase = ABBREV_SORT;
    }
}


static void value(TbSymbolAbbrev_t *abbrev)
{
    TbSymbolLebView_t view;
    if (!tb_symbol_leb_view(&abbrev->leb, &view)) {
        fail(abbrev, TB_SYMBOL_ABBREV_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    if (abbrev->field == FIELD_IMPLICIT) {
        abbrev->attribute.implicit_offset = abbrev->field_start;
        abbrev->attribute.implicit_bytes = view.bytes;
        abbrev->attribute.implicit_fits = view.fits;
        if (view.fits && !tb_symbol_leb_sint(&abbrev->leb, &abbrev->attribute.implicit_value)) {
            fail(abbrev, TB_SYMBOL_ABBREV_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        add_attribute(abbrev);
        return;
    }
    if (!view.fits) {
        fail(abbrev, TB_SYMBOL_ABBREV_IDENTIFIER, TB_SYMBOL_UNSUPPORTED);
        return;
    }
    switch (abbrev->field) {
    case FIELD_CODE:
        if (!view.bits) {
            finish_pass(abbrev);
        } else {
            abbrev->current = (TbSymbolAbbrevEntry_t){
                .code            = view.bits,
                .offset          = abbrev->field_start,
                .first_attribute = abbrev->populating ? abbrev->attributes_written : abbrev->attribute_count
            };
            field(abbrev, FIELD_TAG);
        }
        break;
    case FIELD_TAG:
        abbrev->current.tag = view.bits;
        field(abbrev, FIELD_CHILDREN);
        break;
    case FIELD_NAME:
        abbrev->attribute = (TbSymbolAbbrevAttribute_t){
            .name   = view.bits,
            .offset = abbrev->field_start
        };
        field(abbrev, FIELD_FORM);
        break;
    case FIELD_FORM:
        if (!abbrev->attribute.name || !view.bits) {
            if (!abbrev->attribute.name && !view.bits)
                add_entry(abbrev);
            else
                fail(abbrev, TB_SYMBOL_ABBREV_TERMINATOR, TB_SYMBOL_MALFORMED);
        } else if (!known_form(abbrev, view.bits)) {
            fail(abbrev, TB_SYMBOL_ABBREV_FORM, TB_SYMBOL_UNSUPPORTED);
        } else {
            abbrev->attribute.form = view.bits;
            if (view.bits == TB_SYMBOL_FORM_IMPLICIT_CONST)
                field(abbrev, FIELD_IMPLICIT);
            else
                add_attribute(abbrev);
        }
        break;
    case FIELD_CHILDREN:
    case FIELD_IMPLICIT:
        fail(abbrev, TB_SYMBOL_ABBREV_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        break;
    }
}


static void parse(TbSymbolAbbrev_t *abbrev, size_t max_bytes)
{
    if (!window(abbrev))
        return;
    size_t at = (size_t)(abbrev->cursor - abbrev->window_start);
    if (abbrev->field == FIELD_CHILDREN) {
        unsigned children = abbrev->window[at];
        ++abbrev->cursor;
        if (children > 1) {
            fail(abbrev, TB_SYMBOL_ABBREV_CHILDREN, TB_SYMBOL_MALFORMED);
        } else {
            abbrev->current.children = children != 0;
            field(abbrev, FIELD_NAME);
        }
        return;
    }
    size_t available = abbrev->window_bytes - at;
    if (available > max_bytes)
        available = max_bytes;
    size_t consumed;
    TbSymbolReadStatus_e status = tb_symbol_leb_feed(&abbrev->leb, abbrev->window + at, available, &consumed);
    if (status == TB_SYMBOL_READ_OK || status == TB_SYMBOL_READ_LIMIT) {
        abbrev->cursor += consumed;
        if (status == TB_SYMBOL_READ_OK)
            value(abbrev);
    } else {
        TbSymbolOutcome_e outcome = status == TB_SYMBOL_READ_OVERFLOW
                                        ? TB_SYMBOL_RESOURCE_FAILURE : TB_SYMBOL_OPERATIONAL_FAILURE;
        fail(abbrev, TB_SYMBOL_ABBREV_RESOURCE, outcome);
    }
}


static void allocate(TbSymbolAbbrev_t *abbrev)
{
    uint64_t entries;
    uint64_t attributes;
    uint64_t order;
    uint64_t total;
    if (!tb_symbol_multiply(abbrev->count, sizeof(*abbrev->entries), &entries) ||
        !tb_symbol_multiply(abbrev->attribute_count, sizeof(*abbrev->attributes), &attributes) ||
        !tb_symbol_multiply(abbrev->count, sizeof(*abbrev->order), &order) ||
        !tb_symbol_add(entries, attributes, &total) || !tb_symbol_add(total, order, &total) ||
        total > SIZE_MAX || !tb_symbol_budget_reserve(abbrev->construction, (size_t)total)) {
        fail(abbrev, TB_SYMBOL_ABBREV_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }
    abbrev->array_bytes = (size_t)total;
    if (entries)
        abbrev->entries = malloc((size_t)entries);
    if (attributes)
        abbrev->attributes = malloc((size_t)attributes);
    if (order)
        abbrev->order = malloc((size_t)order);
    if ((entries && !abbrev->entries) || (attributes && !abbrev->attributes) || (order && !abbrev->order)) {
        abbrev->failure.system_error = ENOMEM;
        fail(abbrev, TB_SYMBOL_ABBREV_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }
    abbrev->populating = true;
    abbrev->cursor = abbrev->selection.table_offset;
    abbrev->window_bytes = 0;
    field(abbrev, FIELD_CODE);
    abbrev->phase = ABBREV_PARSE;
}


static bool less(const TbSymbolAbbrev_t *abbrev, uint64_t left, uint64_t right)
{
    uint64_t a = abbrev->entries[left].code;
    uint64_t b = abbrev->entries[right].code;
    return a < b || (a == b && left < right);
}


static void exchange(TbSymbolAbbrev_t *abbrev, uint64_t left, uint64_t right)
{
    uint64_t temporary = abbrev->order[left];
    abbrev->order[left] = abbrev->order[right];
    abbrev->order[right] = temporary;
}


static void sort(TbSymbolAbbrev_t *abbrev)
{
    if (abbrev->sifting) {
        if (abbrev->heap < 2 || abbrev->root > (abbrev->heap - 2) / 2) {
            abbrev->sifting = false;
            return;
        }
        uint64_t child = abbrev->root * 2 + 1;
        if (child + 1 < abbrev->heap && less(abbrev, abbrev->order[child], abbrev->order[child + 1]))
            ++child;
        if (less(abbrev, abbrev->order[abbrev->root], abbrev->order[child])) {
            exchange(abbrev, abbrev->root, child);
            abbrev->root = child;
        } else {
            abbrev->sifting = false;
        }
    } else if (abbrev->sort_build) {
        abbrev->root = --abbrev->sort_build;
        abbrev->heap = abbrev->count;
        abbrev->sifting = true;
    } else if (abbrev->sort_remaining > 1) {
        --abbrev->sort_remaining;
        exchange(abbrev, 0, abbrev->sort_remaining);
        abbrev->root = 0;
        abbrev->heap = abbrev->sort_remaining;
        abbrev->sifting = true;
    } else {
        abbrev->check_index = 1;
        abbrev->phase = ABBREV_CHECK;
    }
}


static void check(TbSymbolAbbrev_t *abbrev)
{
    if (abbrev->check_index >= abbrev->count) {
        abbrev->phase = ABBREV_DONE;
        abbrev->outcome = TB_SYMBOL_SUCCESS;
        return;
    }
    const TbSymbolAbbrevEntry_t *left = &abbrev->entries[abbrev->order[abbrev->check_index - 1]];
    const TbSymbolAbbrevEntry_t *right = &abbrev->entries[abbrev->order[abbrev->check_index++]];
    if (left->code == right->code) {
        fail(abbrev, TB_SYMBOL_ABBREV_CODE_CONFLICT, TB_SYMBOL_MALFORMED);
        abbrev->failure.offset = right->offset;
        abbrev->failure.related_offset = left->offset;
    }
}


TbSymbolOutcome_e tb_symbol_abbrev_step(TbSymbolAbbrev_t *abbrev, size_t max_bytes)
{
    if (!abbrev)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (abbrev->phase == ABBREV_DONE || abbrev->phase == ABBREV_FAILED)
        return abbrev->outcome;
    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        fail(abbrev, TB_SYMBOL_ABBREV_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return abbrev->outcome;
    }
    bool progressed = true;
    if (abbrev->reading) {
        uint64_t before = tb_symbol_file_progress(abbrev->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(abbrev->file, max_bytes);
        progressed = before != tb_symbol_file_progress(abbrev->file);
        if (outcome == TB_SYMBOL_SUCCESS) {
            TbSymbolFileView_t view;
            if (!tb_symbol_file_view(abbrev->file, &view)) {
                fail(abbrev, TB_SYMBOL_ABBREV_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
            } else {
                abbrev->window = view.data;
                abbrev->window_bytes = view.bytes;
                abbrev->reading = false;
            }
        } else if (outcome != TB_SYMBOL_PENDING) {
            abbrev->failure.read = tb_symbol_file_failure(abbrev->file);
            fail(abbrev, TB_SYMBOL_ABBREV_READ, outcome);
        }
    } else {
        switch (abbrev->phase) {
        case ABBREV_SECTION:
            if (abbrev->section.type != SHT_PROGBITS || (abbrev->section.flags & SHF_COMPRESSED)) {
                fail(abbrev, TB_SYMBOL_ABBREV_SECTION, TB_SYMBOL_UNSUPPORTED);
            } else if (abbrev->cursor >= abbrev->section.bytes) {
                fail(abbrev, TB_SYMBOL_ABBREV_RANGE, TB_SYMBOL_MALFORMED);
            } else {
                field(abbrev, FIELD_CODE);
                abbrev->phase = ABBREV_PARSE;
            }
            break;
        case ABBREV_PARSE:
            parse(abbrev, max_bytes);
            break;
        case ABBREV_ALLOCATE:
            allocate(abbrev);
            break;
        case ABBREV_SORT:
            sort(abbrev);
            break;
        case ABBREV_CHECK:
            check(abbrev);
            break;
        case ABBREV_DONE:
        case ABBREV_FAILED:
            break;
        }
    }
    if (progressed) {
        if (abbrev->progress == UINT64_MAX)
            fail(abbrev, TB_SYMBOL_ABBREV_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++abbrev->progress;
    }
    return abbrev->outcome;
}


bool tb_symbol_abbrev_view(const TbSymbolAbbrev_t *abbrev, TbSymbolAbbrevView_t *view)
{
    if (!abbrev || !view || abbrev->phase != ABBREV_DONE || abbrev->outcome != TB_SYMBOL_SUCCESS)
        return false;
    *view = (TbSymbolAbbrevView_t){
        .entries         = abbrev->entries,
        .attributes      = abbrev->attributes,
        .count           = abbrev->count,
        .attribute_count = abbrev->attribute_count,
        .section_index   = abbrev->selection.section_index,
        .table_offset    = abbrev->selection.table_offset,
        .end_offset      = abbrev->end_offset,
        .version         = abbrev->selection.version,
        .backing         = abbrev->directory.backing
    };
    return true;
}


TbSymbolAbbrevLookup_e tb_symbol_abbrev_find(const TbSymbolAbbrev_t *abbrev, uint64_t code,
                                           const TbSymbolAbbrevEntry_t **entry)
{
    if (!abbrev || !entry || !code)
        return TB_SYMBOL_ABBREV_INVALID;
    if (abbrev->phase != ABBREV_DONE || abbrev->outcome != TB_SYMBOL_SUCCESS)
        return TB_SYMBOL_ABBREV_UNAVAILABLE;
    uint64_t begin = 0;
    uint64_t end = abbrev->count;
    while (begin < end) {
        uint64_t middle = begin + (end - begin) / 2;
        const TbSymbolAbbrevEntry_t *candidate = &abbrev->entries[abbrev->order[middle]];
        if (candidate->code < code)
            begin = middle + 1;
        else
            end = middle;
    }
    if (begin == abbrev->count || abbrev->entries[abbrev->order[begin]].code != code)
        return TB_SYMBOL_ABBREV_MISSING;
    *entry = &abbrev->entries[abbrev->order[begin]];
    return TB_SYMBOL_ABBREV_FOUND;
}


TbSymbolAbbrevFailure_t tb_symbol_abbrev_failure(const TbSymbolAbbrev_t *abbrev)
{
    return abbrev ? abbrev->failure : (TbSymbolAbbrevFailure_t){
        .reason         = TB_SYMBOL_ABBREV_REQUEST,
        .offset         = UINT64_MAX,
        .related_offset = UINT64_MAX
    };
}


uint64_t tb_symbol_abbrev_progress(const TbSymbolAbbrev_t *abbrev)
{
    return abbrev ? abbrev->progress : 0;
}


void tb_symbol_abbrev_destroy(TbSymbolAbbrev_t *abbrev)
{
    if (!abbrev)
        return;
    tb_symbol_file_destroy(abbrev->file);
    free(abbrev->entries);
    free(abbrev->attributes);
    free(abbrev->order);
    if (abbrev->array_bytes)
        (void)tb_symbol_budget_release(abbrev->construction, abbrev->array_bytes);
    (void)tb_symbol_budget_release(abbrev->construction, sizeof(*abbrev));
    free(abbrev);
}
