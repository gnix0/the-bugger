#include "symbol/strings.h"

#include <elf.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STRING_SECTION,
    STRING_SCAN,
    STRING_DONE,
    STRING_FAILED
} StringPhase_e;

struct tb_symbol_string {
    TbSymbolBudget_t          *construction;
    TbSymbolFile_t            *file;
    TbSymbolSectionsView_t     directory;
    TbSymbolStringSelection_t  selection;
    TbSymbolElfSection_t       section;
    const unsigned char       *window;
    uint64_t                   window_start;
    size_t                     window_bytes;
    uint64_t                   cursor;
    uint64_t                   bytes;
    bool                       reading;
    uint64_t                   progress;
    StringPhase_e              phase;
    TbSymbolOutcome_e          outcome;
    TbSymbolStringFailure_t    failure;
};

TbSymbolString_t *tb_symbol_string_create(TbSymbolSectionsView_t directory,
                                         TbSymbolStringSelection_t selection,
                                         TbSymbolBudget_t *construction)
{
    if (!directory.header || !directory.layout || !directory.entries || selection.descriptor < 0 ||
        directory.count != directory.layout->sections.count || selection.section_index >= directory.count ||
        directory.backing.bytes != directory.header->file_bytes || !construction ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolString_t))) {
        return NULL;
    }

    TbSymbolString_t *string = calloc(1, sizeof(*string));
    if (!string) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolString_t));
        return NULL;
    }

    string->construction = construction;
    string->directory    = directory;
    string->selection    = selection;
    string->section      = directory.entries[selection.section_index].header;
    string->cursor       = selection.offset;
    string->file = tb_symbol_file_create(directory.backing, selection.descriptor, construction);

    if (!string->file) {
        tb_symbol_string_destroy(string);
        return NULL;
    }

    return string;
}


static void fail(TbSymbolString_t *string, TbSymbolStringFailure_e reason, TbSymbolOutcome_e outcome)
{
    string->failure.reason = reason;
    string->failure.offset = string->cursor;
    string->phase          = STRING_FAILED;
    string->outcome        = outcome;
}


static bool window(TbSymbolString_t *string, size_t max_bytes)
{
    if (string->cursor >= string->window_start &&
        string->cursor - string->window_start < string->window_bytes) {
        return true;
    }

    uint64_t remaining = string->selection.end_offset - string->cursor;
    uint64_t offset;
    if (!tb_symbol_add(string->section.offset, string->cursor, &offset) ||
        !tb_symbol_file_request(string->file, (TbSymbolFileRange_t) {
            .offset = offset,
            .bytes  = remaining < max_bytes ? (size_t)remaining : max_bytes
        })) {
        fail(string, TB_SYMBOL_STRING_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return false;
    }

    string->window_start = string->cursor;
    string->window_bytes = 0;
    string->reading = true;

    return false;
}


static void scan(TbSymbolString_t *string, size_t max_bytes)
{
    if (string->cursor == string->selection.end_offset) {
        fail(string, TB_SYMBOL_STRING_UNTERMINATED, TB_SYMBOL_MALFORMED);
        return;
    }

    if (!window(string, max_bytes))
        return;

    size_t at = (size_t)(string->cursor - string->window_start);
    size_t count = string->window_bytes - at;
    if (count > max_bytes)
        count = max_bytes;

    const unsigned char *terminator = memchr(string->window + at, 0, count);
    if (terminator) {
        size_t before = (size_t)(terminator - (string->window + at));

        string->bytes = string->cursor - string->selection.offset + before;
        string->phase = STRING_DONE;
        string->outcome = TB_SYMBOL_SUCCESS;
    } else {
        string->cursor += count;
    }
}


TbSymbolOutcome_e tb_symbol_string_step(TbSymbolString_t *string, size_t max_bytes)
{
    if (!string)
        return TB_SYMBOL_OPERATIONAL_FAILURE;

    if (string->phase == STRING_DONE || string->phase == STRING_FAILED)
        return string->outcome;

    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        fail(string, TB_SYMBOL_STRING_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return string->outcome;
    }

    bool progressed = true;

    if (string->reading) {
        uint64_t before = tb_symbol_file_progress(string->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(string->file, max_bytes);

        progressed = before != tb_symbol_file_progress(string->file);

        if (outcome == TB_SYMBOL_SUCCESS) {
            TbSymbolFileView_t view;
            if (tb_symbol_file_view(string->file, &view)) {
                string->window = view.data;
                string->window_bytes = view.bytes;
                string->reading = false;
            } else {
                fail(string, TB_SYMBOL_STRING_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
            }
        } else if (outcome != TB_SYMBOL_PENDING) {
            string->failure.read = tb_symbol_file_failure(string->file);
            fail(string, TB_SYMBOL_STRING_READ, outcome);
        }
    } else if (string->phase == STRING_SECTION) {
        if ((string->section.type != SHT_PROGBITS && string->section.type != SHT_STRTAB) ||
            (string->section.flags & SHF_COMPRESSED)) {
            fail(string, TB_SYMBOL_STRING_SECTION, TB_SYMBOL_UNSUPPORTED);
        } else if (string->selection.offset >= string->selection.end_offset ||
                   string->selection.end_offset > string->section.bytes) {
            fail(string, TB_SYMBOL_STRING_RANGE, TB_SYMBOL_MALFORMED);
        } else {
            string->phase = STRING_SCAN;
        }
    } else {
        scan(string, max_bytes);
    }

    if (progressed) {
        if (string->progress == UINT64_MAX)
            fail(string, TB_SYMBOL_STRING_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++string->progress;
    }

    return string->outcome;
}


bool tb_symbol_string_view(const TbSymbolString_t *string, TbSymbolStringView_t *view)
{
    if (!string || !view || string->phase != STRING_DONE || string->outcome != TB_SYMBOL_SUCCESS)
        return false;

    *view = (TbSymbolStringView_t) {
        .backing       = string->directory.backing,
        .section_index = string->selection.section_index,
        .offset        = string->selection.offset,
        .bytes         = string->bytes,
        .end_offset    = string->selection.end_offset
    };

    return true;
}


TbSymbolStringFailure_t tb_symbol_string_failure(const TbSymbolString_t *string)
{
    return string ? string->failure : (TbSymbolStringFailure_t) {
        .reason = TB_SYMBOL_STRING_REQUEST,
        .offset = UINT64_MAX
    };
}


uint64_t tb_symbol_string_progress(const TbSymbolString_t *string)
{
    return string ? string->progress : 0;
}


void tb_symbol_string_destroy(TbSymbolString_t *string)
{
    if (!string)
        return;

    tb_symbol_file_destroy(string->file);
    (void)tb_symbol_budget_release(string->construction, sizeof(*string));
    free(string);
}
