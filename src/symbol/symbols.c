#include "symbol/symbols.h"

#include <elf.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SYMBOLS_TABLE,
    SYMBOLS_DEPENDENCIES,
    SYMBOLS_STR_FIRST,
    SYMBOLS_STR_LAST,
    SYMBOLS_ALLOCATE,
    SYMBOLS_NEXT,
    SYMBOLS_RECORD,
    SYMBOLS_INDEX,
    SYMBOLS_RESOLVE,
    SYMBOLS_NAME_SCAN,
    SYMBOLS_DONE,
    SYMBOLS_FAILED
} SymbolsPhase_e;

struct tb_symbol_symbols {
    TbSymbolBudget_t        *construction;
    TbSymbolFile_t          *file;
    TbSymbolSectionsView_t   directory;
    TbSymbolTableSelection_t selection;

    TbSymbolElfSection_t table;
    TbSymbolElfSection_t strings;
    TbSymbolElfSection_t indexes;
    TbSymbolElfEntry_t  *entries;
    size_t              array_bytes;
    uint64_t            count;
    uint64_t            cursor;
    uint64_t            dependency;

    bool     has_indexes;
    bool     reading;
    uint64_t extended_index;
    uint64_t name_cursor;
    size_t   scan_at;
    uint64_t progress;

    SymbolsPhase_e          phase;
    TbSymbolOutcome_e       outcome;
    TbSymbolSymbolsFailure_t failure;
};

TbSymbolSymbols_t *tb_symbol_symbols_create(TbSymbolSectionsView_t directory,
                                           TbSymbolTableSelection_t selection,
                                           TbSymbolBudget_t *construction)
{
    if (!directory.header || !directory.layout || !directory.entries || selection.descriptor < 0 ||
        selection.section_index >= directory.count || directory.count != directory.layout->sections.count ||
        directory.backing.bytes != directory.header->file_bytes || !construction ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolSymbols_t))) {
        return NULL;
    }

    TbSymbolSymbols_t *symbols = calloc(1, sizeof(*symbols));
    if (!symbols) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolSymbols_t));
        return NULL;
    }

    symbols->construction = construction;
    symbols->directory    = directory;
    symbols->selection    = selection;
    symbols->table        = directory.entries[selection.section_index].header;
    symbols->cursor       = UINT64_MAX;
    symbols->file = tb_symbol_file_create(directory.backing, selection.descriptor, construction);

    if (!symbols->file) {
        tb_symbol_symbols_destroy(symbols);
        return NULL;
    }

    return symbols;
}


static void fail(TbSymbolSymbols_t *symbols, TbSymbolSymbolsFailure_e reason, TbSymbolOutcome_e outcome)
{
    symbols->failure.reason       = reason;
    symbols->failure.record_index = symbols->cursor;
    symbols->outcome              = outcome;
    symbols->phase                = SYMBOLS_FAILED;
}


static void request(TbSymbolSymbols_t *symbols, TbSymbolFileRange_t range, SymbolsPhase_e next)
{
    if (!range.bytes || range.bytes > TB_SYMBOL_FILE_WINDOW_BYTES ||
        !tb_symbol_range(range.offset, range.bytes, symbols->directory.backing.bytes)) {
        fail(symbols, TB_SYMBOL_SYMBOLS_READ, TB_SYMBOL_MALFORMED);
    } else if (!tb_symbol_file_request(symbols->file, range)) {
        fail(symbols, TB_SYMBOL_SYMBOLS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else {
        symbols->reading = true;
        symbols->phase = next;
    }
}


static TbSymbolFileView_t bytes(TbSymbolSymbols_t *symbols)
{
    TbSymbolFileView_t view = {0};
    if (!tb_symbol_file_view(symbols->file, &view))
        fail(symbols, TB_SYMBOL_SYMBOLS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);

    return view;
}


static void table(TbSymbolSymbols_t *symbols)
{
    const TbSymbolElfSection_t *selected = &symbols->table;
    uint64_t stride = symbols->directory.header->address_bytes == 4 ? 16 : 24;

    if ((selected->type != SHT_SYMTAB && selected->type != SHT_DYNSYM) ||
        (selected->flags & SHF_COMPRESSED)) {
        fail(symbols, TB_SYMBOL_SYMBOLS_TABLE, TB_SYMBOL_UNSUPPORTED);
    } else if (selected->entry_bytes != stride) {
        fail(symbols, TB_SYMBOL_SYMBOLS_TABLE, selected->entry_bytes < stride
                                               ? TB_SYMBOL_MALFORMED : TB_SYMBOL_UNSUPPORTED);
    } else if (selected->bytes % stride || !selected->bytes) {
        fail(symbols, TB_SYMBOL_SYMBOLS_TABLE, TB_SYMBOL_MALFORMED);
    } else if (!selected->link || selected->link >= symbols->directory.count) {
        fail(symbols, TB_SYMBOL_SYMBOLS_STRINGS, TB_SYMBOL_MALFORMED);
    } else {
        symbols->count = selected->bytes / stride;
        symbols->strings = symbols->directory.entries[selected->link].header;

        if (!selected->info || selected->info > symbols->count) {
            fail(symbols, TB_SYMBOL_SYMBOLS_LOCAL_ORDER, TB_SYMBOL_MALFORMED);
        } else if (symbols->strings.type != SHT_STRTAB) {
            fail(symbols, TB_SYMBOL_SYMBOLS_STRINGS, TB_SYMBOL_MALFORMED);
        } else if (symbols->strings.flags & SHF_COMPRESSED) {
            fail(symbols, TB_SYMBOL_SYMBOLS_STRINGS, TB_SYMBOL_UNSUPPORTED);
        } else {
            symbols->phase = SYMBOLS_DEPENDENCIES;
        }
    }
}


static void dependencies(TbSymbolSymbols_t *symbols)
{
    if (symbols->dependency == symbols->directory.count) {
        if (!symbols->strings.bytes) {
            symbols->phase = SYMBOLS_ALLOCATE;
        } else {
            request(symbols, (TbSymbolFileRange_t) {
                .offset = symbols->strings.offset,
                .bytes  = 1
            }, SYMBOLS_STR_FIRST);
        }
        return;
    }

    const TbSymbolElfSection_t *candidate = &symbols->directory.entries[symbols->dependency++].header;
    if (candidate->type != SHT_SYMTAB_SHNDX || candidate->link != symbols->selection.section_index)
        return;

    if (symbols->has_indexes) {
        fail(symbols, TB_SYMBOL_SYMBOLS_INDEX_CONFLICT, TB_SYMBOL_MALFORMED);
        return;
    }

    uint64_t expected;

    if ((candidate->flags & SHF_COMPRESSED) || candidate->entry_bytes > 4) {
        fail(symbols, TB_SYMBOL_SYMBOLS_INDEX_TABLE, TB_SYMBOL_UNSUPPORTED);
    } else if (!tb_symbol_multiply(symbols->count, 4, &expected) || candidate->entry_bytes != 4 ||
               candidate->bytes != expected || candidate->info) {
        fail(symbols, TB_SYMBOL_SYMBOLS_INDEX_TABLE, TB_SYMBOL_MALFORMED);
    } else {
        symbols->indexes = *candidate;
        symbols->has_indexes = true;
    }
}


static void allocate_entries(TbSymbolSymbols_t *symbols)
{
    uint64_t required;
    if (!tb_symbol_multiply(symbols->count, sizeof(*symbols->entries), &required) || required > SIZE_MAX ||
        !tb_symbol_budget_reserve(symbols->construction, (size_t)required)) {
        fail(symbols, TB_SYMBOL_SYMBOLS_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }

    symbols->array_bytes = (size_t)required;
    symbols->entries = malloc(symbols->array_bytes);

    if (!symbols->entries) {
        symbols->failure.system_error = ENOMEM;
        fail(symbols, TB_SYMBOL_SYMBOLS_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }

    symbols->cursor = 0;
    symbols->phase = SYMBOLS_NEXT;
}


static void record(TbSymbolSymbols_t *symbols)
{
    TbSymbolFileView_t view = bytes(symbols);

    if (symbols->phase == SYMBOLS_FAILED)
        return;

    TbSymbolElfSymbol_t raw;
    TbSymbolElfStatus_e status = tb_symbol_elf_symbol(symbols->directory.header, view.data, view.bytes, &raw);
    if (status != TB_SYMBOL_ELF_OK) {
        fail(symbols, TB_SYMBOL_SYMBOLS_TABLE, TB_SYMBOL_MALFORMED);
        return;
    }

    if (!symbols->cursor && (raw.name_offset || raw.info || raw.other || raw.section_index || raw.value ||
                             raw.bytes)) {
        fail(symbols, TB_SYMBOL_SYMBOLS_NULL_ENTRY, TB_SYMBOL_MALFORMED);
        return;
    }

    bool local = ELF64_ST_BIND(raw.info) == STB_LOCAL;
    if (local != (symbols->cursor < symbols->table.info)) {
        fail(symbols, TB_SYMBOL_SYMBOLS_LOCAL_ORDER, TB_SYMBOL_MALFORMED);
        return;
    }

    symbols->entries[symbols->cursor] = (TbSymbolElfEntry_t) {
        .raw           = raw,
        .record_offset = symbols->cursor * symbols->table.entry_bytes
    };

    symbols->extended_index = 0;

    if (symbols->has_indexes) {
        request(symbols, (TbSymbolFileRange_t) {
            .offset = symbols->indexes.offset + symbols->cursor * 4,
            .bytes  = 4
        }, SYMBOLS_INDEX);
    } else {
        symbols->phase = SYMBOLS_RESOLVE;
    }
}


static bool resolve(TbSymbolSymbols_t *symbols, TbSymbolElfEntry_t *entry)
{
    uint64_t index = entry->raw.section_index;
    if (index == SHN_XINDEX) {
        if (!symbols->has_indexes) {
            fail(symbols, TB_SYMBOL_SYMBOLS_INDEX_MISSING, TB_SYMBOL_MALFORMED);
            return false;
        }

        index = symbols->extended_index;

        if (index < SHN_LORESERVE || index >= symbols->directory.count) {
            fail(symbols, TB_SYMBOL_SYMBOLS_INDEX_VALUE, TB_SYMBOL_MALFORMED);
            return false;
        }

        entry->reference = TB_SYMBOL_ELF_REF_ORDINARY;
        entry->section_index = index;
    } else {
        if (symbols->extended_index) {
            fail(symbols, TB_SYMBOL_SYMBOLS_INDEX_VALUE, TB_SYMBOL_MALFORMED);
            return false;
        }

        if (!index) {
            entry->reference = TB_SYMBOL_ELF_REF_UNDEFINED;
        } else if (index == SHN_ABS) {
            entry->reference = TB_SYMBOL_ELF_REF_ABSOLUTE;
        } else if (index == SHN_COMMON) {
            entry->reference = TB_SYMBOL_ELF_REF_COMMON;
        } else if (index >= SHN_LORESERVE) {
            entry->reference = TB_SYMBOL_ELF_REF_RESERVED;
        } else if (index >= symbols->directory.count) {
            fail(symbols, TB_SYMBOL_SYMBOLS_INDEX_VALUE, TB_SYMBOL_MALFORMED);
            return false;
        } else {
            entry->reference = TB_SYMBOL_ELF_REF_ORDINARY;
            entry->section_index = index;
        }
    }

    return true;
}


static void resolve_record(TbSymbolSymbols_t *symbols)
{
    TbSymbolElfEntry_t *entry = &symbols->entries[symbols->cursor];
    if (!resolve(symbols, entry))
        return;

    if (!entry->raw.name_offset) {
        ++symbols->cursor;
        symbols->phase = SYMBOLS_NEXT;
        return;
    }

    if (entry->raw.name_offset >= symbols->strings.bytes) {
        fail(symbols, TB_SYMBOL_SYMBOLS_NAME, TB_SYMBOL_MALFORMED);
        return;
    }

    entry->name_offset = symbols->strings.offset + entry->raw.name_offset;
    symbols->name_cursor = entry->raw.name_offset;

    uint64_t remaining = symbols->strings.bytes - symbols->name_cursor;

    symbols->scan_at = 0;
    request(symbols, (TbSymbolFileRange_t) {
        .offset = entry->name_offset,
        .bytes  = remaining < 64 ? (size_t)remaining : 64
    }, SYMBOLS_NAME_SCAN);
}


static void name_scan(TbSymbolSymbols_t *symbols, size_t max_bytes)
{
    TbSymbolFileView_t view = bytes(symbols);

    if (symbols->phase == SYMBOLS_FAILED)
        return;

    size_t length = view.bytes - symbols->scan_at;
    if (length > max_bytes)
        length = max_bytes;

    const unsigned char *start = view.data + symbols->scan_at;
    const unsigned char *end = memchr(start, 0, length);
    TbSymbolElfEntry_t *entry = &symbols->entries[symbols->cursor];

    if (end) {
        entry->name_bytes += (uint64_t)(end - start);
        entry->has_name = true;
        ++symbols->cursor;
        symbols->phase = SYMBOLS_NEXT;

        return;
    }

    entry->name_bytes += length;
    symbols->scan_at += length;

    if (symbols->scan_at != view.bytes)
        return;

    symbols->name_cursor += view.bytes;

    if (symbols->name_cursor == symbols->strings.bytes) {
        fail(symbols, TB_SYMBOL_SYMBOLS_NAME, TB_SYMBOL_MALFORMED);
        return;
    }

    uint64_t remaining = symbols->strings.bytes - symbols->name_cursor;

    symbols->scan_at = 0;
    request(symbols, (TbSymbolFileRange_t) {
        .offset = symbols->strings.offset + symbols->name_cursor,
        .bytes  = remaining < max_bytes ? (size_t)remaining : max_bytes
    }, SYMBOLS_NAME_SCAN);
}


static void advance(TbSymbolSymbols_t *symbols, size_t max_bytes)
{
    switch (symbols->phase) {
    case SYMBOLS_TABLE:
        table(symbols);
        break;
    case SYMBOLS_DEPENDENCIES:
        dependencies(symbols);
        break;
    case SYMBOLS_STR_FIRST: {
        TbSymbolFileView_t view = bytes(symbols);

        if (symbols->phase == SYMBOLS_FAILED)
            break;

        if (view.data[0]) {
            fail(symbols, TB_SYMBOL_SYMBOLS_STRINGS, TB_SYMBOL_MALFORMED);
            break;
        }

        request(symbols, (TbSymbolFileRange_t) {
            .offset = symbols->strings.offset + symbols->strings.bytes - 1,
            .bytes  = 1
        }, SYMBOLS_STR_LAST);
        break;
    }
    case SYMBOLS_STR_LAST: {
        TbSymbolFileView_t view = bytes(symbols);

        if (symbols->phase == SYMBOLS_FAILED)
            break;

        if (view.data[0])
            fail(symbols, TB_SYMBOL_SYMBOLS_STRINGS, TB_SYMBOL_MALFORMED);
        else
            symbols->phase = SYMBOLS_ALLOCATE;
        break;
    }
    case SYMBOLS_ALLOCATE:
        allocate_entries(symbols);
        break;
    case SYMBOLS_NEXT:
        if (symbols->cursor == symbols->count) {
            symbols->phase = SYMBOLS_DONE;
            symbols->outcome = TB_SYMBOL_SUCCESS;
        } else {
            request(symbols, (TbSymbolFileRange_t) {
                .offset = symbols->table.offset + symbols->cursor * symbols->table.entry_bytes,
                .bytes  = (size_t)symbols->table.entry_bytes
            }, SYMBOLS_RECORD);
        }
        break;
    case SYMBOLS_RECORD:
        record(symbols);
        break;
    case SYMBOLS_INDEX: {
        TbSymbolFileView_t view = bytes(symbols);

        if (symbols->phase == SYMBOLS_FAILED)
            break;

        TbSymbolReader_t reader;
        if (tb_symbol_reader_init(&reader, view.data, view.bytes, symbols->directory.header->byte_order) !=
                TB_SYMBOL_READ_OK ||
            tb_symbol_reader_uint(&reader, 4, &symbols->extended_index) != TB_SYMBOL_READ_OK) {
            fail(symbols, TB_SYMBOL_SYMBOLS_INDEX_VALUE, TB_SYMBOL_MALFORMED);
        } else {
            symbols->phase = SYMBOLS_RESOLVE;
        }
        break;
    }
    case SYMBOLS_RESOLVE:
        resolve_record(symbols);
        break;
    case SYMBOLS_NAME_SCAN:
        name_scan(symbols, max_bytes);
        break;
    case SYMBOLS_DONE:
    case SYMBOLS_FAILED:
        break;
    }
}


TbSymbolOutcome_e tb_symbol_symbols_step(TbSymbolSymbols_t *symbols, size_t max_bytes)
{
    if (!symbols)
        return TB_SYMBOL_OPERATIONAL_FAILURE;

    if (symbols->phase == SYMBOLS_DONE || symbols->phase == SYMBOLS_FAILED)
        return symbols->outcome;

    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        fail(symbols, TB_SYMBOL_SYMBOLS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return symbols->outcome;
    }

    bool progressed = true;

    if (symbols->reading) {
        uint64_t before = tb_symbol_file_progress(symbols->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(symbols->file, max_bytes);

        progressed = before != tb_symbol_file_progress(symbols->file);

        if (outcome == TB_SYMBOL_SUCCESS) {
            symbols->reading = false;
        } else if (outcome != TB_SYMBOL_PENDING) {
            symbols->failure.read = tb_symbol_file_failure(symbols->file);
            fail(symbols, TB_SYMBOL_SYMBOLS_READ, outcome);
        }
    } else {
        advance(symbols, max_bytes);
    }

    if (progressed) {
        if (symbols->progress == UINT64_MAX)
            fail(symbols, TB_SYMBOL_SYMBOLS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++symbols->progress;
    }

    return symbols->outcome;
}


bool tb_symbol_symbols_view(const TbSymbolSymbols_t *symbols, TbSymbolSymbolsView_t *view)
{
    if (!symbols || !view || symbols->phase != SYMBOLS_DONE || symbols->outcome != TB_SYMBOL_SUCCESS)
        return false;

    *view = (TbSymbolSymbolsView_t) {
        .entries       = symbols->entries,
        .count         = symbols->count,
        .table_section = symbols->selection.section_index,
        .kind          = symbols->table.type == SHT_SYMTAB ? TB_SYMBOL_RECORD_SYMTAB : TB_SYMBOL_RECORD_DYNSYM
    };

    return true;
}


TbSymbolSymbolsFailure_t tb_symbol_symbols_failure(const TbSymbolSymbols_t *symbols)
{
    return symbols ? symbols->failure : (TbSymbolSymbolsFailure_t) {
        .reason       = TB_SYMBOL_SYMBOLS_REQUEST,
        .record_index = UINT64_MAX
    };
}


uint64_t tb_symbol_symbols_progress(const TbSymbolSymbols_t *symbols)
{
    return symbols ? symbols->progress : 0;
}


void tb_symbol_symbols_destroy(TbSymbolSymbols_t *symbols)
{
    if (!symbols)
        return;

    tb_symbol_file_destroy(symbols->file);
    free(symbols->entries);

    if (symbols->array_bytes)
        (void)tb_symbol_budget_release(symbols->construction, symbols->array_bytes);
    (void)tb_symbol_budget_release(symbols->construction, sizeof(*symbols));
    free(symbols);
}
