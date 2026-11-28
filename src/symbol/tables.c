#include "symbol/tables.h"

#include <elf.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TABLES_SECTION,
    TABLES_BEGIN,
    TABLES_NEXT,
    TABLES_DECODE,
    TABLES_ALLOCATE,
    TABLES_DONE,
    TABLES_FAILED
} TablesPhase_e;

struct tb_symbol_tables {
    TbSymbolBudget_t            *construction;
    TbSymbolFile_t              *file;
    TbSymbolSectionsView_t       directory;
    TbSymbolTablesSelection_t    selection;
    TbSymbolElfSection_t         section;
    TbSymbolTableContribution_t *entries;
    size_t                       array_bytes;
    uint64_t                     count;
    uint64_t                     unsupported;
    uint64_t                     written;
    uint64_t                     unsupported_written;
    uint64_t                     cursor;
    uint64_t                     progress;
    bool                         populating;
    bool                         reading;
    TablesPhase_e                phase;
    TbSymbolOutcome_e            outcome;
    TbSymbolTablesFailure_t      failure;
};

struct tb_symbol_table_value {
    TbSymbolBudget_t             *construction;
    TbSymbolFile_t               *file;
    TbSymbolSectionsView_t        directory;
    TbSymbolTableValueView_t      view;
    TbSymbolTableValueSelection_t selection;
    TbSymbolTablesFailure_t       failure;
    TbSymbolOutcome_e             outcome;
    uint64_t                      progress;
    bool                          reading;
    bool                          done;
};

TbSymbolTables_t *tb_symbol_tables_create(TbSymbolSectionsView_t directory,
                                       TbSymbolTablesSelection_t selection, TbSymbolBudget_t *construction)
{
    if (!directory.header || !directory.layout || !directory.entries || selection.descriptor < 0 ||
        selection.section_index >= directory.count || directory.count != directory.layout->sections.count ||
        directory.backing.bytes != directory.header->file_bytes ||
        (selection.kind != TB_SYMBOL_TABLE_ADDRESSES && selection.kind != TB_SYMBOL_TABLE_RANGES) ||
        !construction || !tb_symbol_budget_reserve(construction, sizeof(TbSymbolTables_t))) {
        return NULL;
    }

    TbSymbolTables_t *tables = calloc(1, sizeof(*tables));
    if (!tables) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolTables_t));
        return NULL;
    }

    tables->construction = construction;
    tables->directory    = directory;
    tables->selection    = selection;
    tables->section      = directory.entries[selection.section_index].header;
    tables->cursor       = UINT64_MAX;
    tables->file         = tb_symbol_file_create(directory.backing, selection.descriptor, construction);

    if (!tables->file) {
        tb_symbol_tables_destroy(tables);
        return NULL;
    }

    return tables;
}


static void fail(TbSymbolTables_t *tables, TbSymbolTablesFailure_e reason, TbSymbolOutcome_e outcome)
{
    tables->failure.reason = reason;
    tables->failure.offset = tables->cursor;
    tables->outcome        = outcome;
    tables->phase          = TABLES_FAILED;
}


static void request(TbSymbolTables_t *tables, TbSymbolFileRange_t range, TablesPhase_e next)
{
    if (!tb_symbol_file_request(tables->file, range)) {
        fail(tables, TB_SYMBOL_TABLES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else {
        tables->reading = true;
        tables->phase   = next;
    }
}


static void section(TbSymbolTables_t *tables)
{
    if (tables->section.type != SHT_PROGBITS || (tables->section.flags & SHF_COMPRESSED)) {
        fail(tables, TB_SYMBOL_TABLES_SECTION, TB_SYMBOL_UNSUPPORTED);
        return;
    }

    /* Even an empty selected section must not accept a replacement descriptor. */
    request(tables, (TbSymbolFileRange_t) {
        .offset = 0,
        .bytes  = 1
    }, TABLES_BEGIN);
}


static void next(TbSymbolTables_t *tables)
{
    if (tables->cursor == tables->section.bytes) {
        if (!tables->populating) {
            tables->phase = TABLES_ALLOCATE;
        } else if (tables->written != tables->count || tables->unsupported_written != tables->unsupported) {
            fail(tables, TB_SYMBOL_TABLES_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
        } else {
            tables->phase   = TABLES_DONE;
            tables->outcome = TB_SYMBOL_SUCCESS;
        }
        return;
    }

    uint64_t offset;
    if (tables->cursor > tables->section.bytes ||
        !tb_symbol_add(tables->section.offset, tables->cursor, &offset)) {
        fail(tables, TB_SYMBOL_TABLES_HEADER, TB_SYMBOL_MALFORMED);
        return;
    }

    uint64_t remaining = tables->section.bytes - tables->cursor;

    request(tables, (TbSymbolFileRange_t) {
        .offset = offset,
        .bytes  = remaining < 20 ? (size_t)remaining : 20
    }, TABLES_DECODE);
}


static void decode(TbSymbolTables_t *tables)
{
    TbSymbolFileView_t view;
    if (!tb_symbol_file_view(tables->file, &view)) {
        fail(tables, TB_SYMBOL_TABLES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }

    TbSymbolDwarfSpan_t span = {
        .section_bytes = tables->section.bytes,
        .unit_offset   = tables->cursor,
        .byte_order    = tables->directory.header->byte_order
    };

    TbSymbolTableContribution_t entry = {
        .offset = tables->cursor
    };

    TbSymbolDwarfStatus_e status = tb_symbol_dwarf_length(span, view.data, view.bytes, &entry.extent);
    if (status != TB_SYMBOL_DWARF_OK) {
        fail(tables, TB_SYMBOL_TABLES_HEADER, status == TB_SYMBOL_DWARF_INVALID
                                               ? TB_SYMBOL_OPERATIONAL_FAILURE : TB_SYMBOL_MALFORMED);
        return;
    }

    status = tb_symbol_indexed_table_header(tables->selection.kind, span, view.data, view.bytes,
                                           &entry.table);

    if (status != TB_SYMBOL_DWARF_OK && status != TB_SYMBOL_DWARF_UNSUPPORTED) {
        fail(tables, TB_SYMBOL_TABLES_HEADER, status == TB_SYMBOL_DWARF_INVALID
                                               ? TB_SYMBOL_OPERATIONAL_FAILURE : TB_SYMBOL_MALFORMED);
        return;
    }

    entry.header_available = status == TB_SYMBOL_DWARF_OK;

    if (tables->populating) {
        if (tables->written >= tables->count) {
            fail(tables, TB_SYMBOL_TABLES_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }

        tables->entries[tables->written++] = entry;

        if (!entry.header_available)
            ++tables->unsupported_written;
    } else {
        if (tables->count == UINT64_MAX) {
            fail(tables, TB_SYMBOL_TABLES_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
            return;
        }

        ++tables->count;

        if (!entry.header_available)
            ++tables->unsupported;
    }

    tables->cursor = entry.extent.end_offset;
    tables->phase  = TABLES_NEXT;
}


static void allocate(TbSymbolTables_t *tables)
{
    uint64_t required;
    if (!tb_symbol_multiply(tables->count, sizeof(*tables->entries), &required) || required > SIZE_MAX ||
        !tb_symbol_budget_reserve(tables->construction, (size_t)required)) {
        fail(tables, TB_SYMBOL_TABLES_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }

    tables->array_bytes = (size_t)required;

    if (required) {
        tables->entries = malloc(tables->array_bytes);

        if (!tables->entries) {
            tables->failure.system_error = ENOMEM;
            fail(tables, TB_SYMBOL_TABLES_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
            return;
        }
    }

    tables->cursor     = 0;
    tables->populating = true;
    tables->phase      = TABLES_NEXT;
}


TbSymbolOutcome_e tb_symbol_tables_step(TbSymbolTables_t *tables, size_t max_bytes)
{
    if (!tables)
        return TB_SYMBOL_OPERATIONAL_FAILURE;

    if (tables->phase == TABLES_DONE || tables->phase == TABLES_FAILED)
        return tables->outcome;

    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        fail(tables, TB_SYMBOL_TABLES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return tables->outcome;
    }

    bool progressed = true;

    if (tables->reading) {
        uint64_t before = tb_symbol_file_progress(tables->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(tables->file, max_bytes);

        progressed = before != tb_symbol_file_progress(tables->file);

        if (outcome == TB_SYMBOL_SUCCESS) {
            tables->reading = false;
        } else if (outcome != TB_SYMBOL_PENDING) {
            tables->failure.read = tb_symbol_file_failure(tables->file);
            fail(tables, TB_SYMBOL_TABLES_READ, outcome);
        }
    } else {
        switch (tables->phase) {
        case TABLES_SECTION:
            section(tables);
            break;
        case TABLES_BEGIN:
            tables->cursor = 0;
            tables->phase  = TABLES_NEXT;
            break;
        case TABLES_NEXT:
            next(tables);
            break;
        case TABLES_DECODE:
            decode(tables);
            break;
        case TABLES_ALLOCATE:
            allocate(tables);
            break;
        case TABLES_DONE:
        case TABLES_FAILED:
            break;
        }
    }

    if (progressed) {
        if (tables->progress == UINT64_MAX)
            fail(tables, TB_SYMBOL_TABLES_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++tables->progress;
    }

    return tables->outcome;
}


bool tb_symbol_tables_view(const TbSymbolTables_t *tables, TbSymbolTablesView_t *view)
{
    if (!tables || !view || tables->phase != TABLES_DONE || tables->outcome != TB_SYMBOL_SUCCESS)
        return false;

    *view = (TbSymbolTablesView_t) {
        .entries       = tables->entries,
        .count         = tables->count,
        .unsupported   = tables->unsupported,
        .section_index = tables->selection.section_index,
        .kind          = tables->selection.kind,
        .backing       = tables->directory.backing
    };

    return true;
}


TbSymbolTablesFailure_t tb_symbol_tables_failure(const TbSymbolTables_t *tables)
{
    return tables ? tables->failure : (TbSymbolTablesFailure_t) {
        .reason = TB_SYMBOL_TABLES_REQUEST,
        .offset = UINT64_MAX
    };
}


uint64_t tb_symbol_tables_progress(const TbSymbolTables_t *tables)
{
    return tables ? tables->progress : 0;
}


void tb_symbol_tables_destroy(TbSymbolTables_t *tables)
{
    if (!tables)
        return;

    tb_symbol_file_destroy(tables->file);
    free(tables->entries);

    if (tables->array_bytes)
        (void)tb_symbol_budget_release(tables->construction, tables->array_bytes);
    (void)tb_symbol_budget_release(tables->construction, sizeof(*tables));
    free(tables);
}


TbSymbolTableBindStatus_e tb_symbol_table_binding_init(TbSymbolTableBinding_t *binding,
    TbSymbolTablesView_t tables, TbSymbolCodeView_t root)
{
    if (!binding || (tables.count && !tables.entries) || root.die_index || root.unit.version != 5 ||
        (tables.kind != TB_SYMBOL_TABLE_ADDRESSES && tables.kind != TB_SYMBOL_TABLE_RANGES)) {
        return TB_SYMBOL_TABLE_BIND_INVALID;
    }

    TbSymbolCodeField_e field = tables.kind == TB_SYMBOL_TABLE_ADDRESSES ? TB_SYMBOL_CODE_ADDR_BASE :
                                                                        TB_SYMBOL_CODE_RNGLISTS_BASE;
    const TbSymbolCodeFact_t *base = &root.fields[field];
    uint64_t root_offset;
    if ((base->kind != TB_SYMBOL_CODE_MISSING && base->kind != TB_SYMBOL_CODE_TABLE_BASE) ||
        tables.backing.key.device != base->selection.backing.key.device ||
        tables.backing.key.inode != base->selection.backing.key.inode ||
        tables.backing.bytes != base->selection.backing.bytes ||
        !tb_symbol_add(base->selection.unit_offset, root.unit.header_bytes, &root_offset) ||
        base->selection.record_offset != root_offset) {
        return TB_SYMBOL_TABLE_BIND_INVALID;
    }

    *binding = (TbSymbolTableBinding_t) {
        .tables = tables,
        .view = {
            .base          = *base,
            .unit          = root.unit,
            .section_index = tables.section_index
        },
        .status = base->kind == TB_SYMBOL_CODE_MISSING ? TB_SYMBOL_TABLE_BIND_DEPENDENCY :
                                                       TB_SYMBOL_TABLE_BIND_PENDING,
        .initialized = true
    };

    return binding->status;
}


TbSymbolTableBindStatus_e tb_symbol_table_binding_step(TbSymbolTableBinding_t *binding, size_t max_steps)
{
    if (!binding || !binding->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_TABLE_BIND_INVALID;
    if (binding->status != TB_SYMBOL_TABLE_BIND_PENDING)
        return binding->status;

    for (size_t step = 0; step < max_steps && binding->cursor < binding->tables.count; ++step) {
        uint64_t index = binding->cursor++;
        const TbSymbolTableContribution_t *entry = &binding->tables.entries[index];
        if (!entry->header_available) {
            if (binding->view.base.value >= entry->offset &&
                binding->view.base.value <= entry->extent.end_offset) {
                binding->unknown = true;
            }
            continue;
        }
        if (entry->table.entries_offset != binding->view.base.value)
            continue;

        if (binding->found || entry->table.address_bytes != binding->view.unit.address_bytes ||
            entry->table.kind != binding->tables.kind) {
            binding->status = TB_SYMBOL_TABLE_BIND_MALFORMED;
            return binding->status;
        }

        binding->view.table              = entry->table;
        binding->view.contribution_index = index;
        binding->found                   = true;
    }

    if (binding->cursor == binding->tables.count) {
        binding->status = binding->found ? TB_SYMBOL_TABLE_BIND_FOUND :
                           binding->unknown ? TB_SYMBOL_TABLE_BIND_UNSUPPORTED :
                                              TB_SYMBOL_TABLE_BIND_MALFORMED;
    }

    return binding->status;
}


bool tb_symbol_table_binding_view(const TbSymbolTableBinding_t *binding, TbSymbolTableBindingView_t *view)
{
    if (!binding || !binding->initialized || !view || binding->status != TB_SYMBOL_TABLE_BIND_FOUND)
        return false;

    *view = binding->view;

    return true;
}


void tb_symbol_table_binding_discard(TbSymbolTableBinding_t *binding)
{
    if (binding)
        memset(binding, 0, sizeof(*binding));
}


TbSymbolTableValue_t *tb_symbol_table_value_create(TbSymbolSectionsView_t directory,
    TbSymbolTableBindingView_t binding, TbSymbolTableValueSelection_t selection,
    TbSymbolBudget_t *construction)
{
    uint64_t offset;
    if (!directory.header || !directory.layout || !directory.entries ||
        directory.count != directory.layout->sections.count || binding.section_index >= directory.count ||
        selection.descriptor < 0 || !construction ||
        binding.base.kind != TB_SYMBOL_CODE_TABLE_BASE ||
        binding.base.value != binding.table.entries_offset ||
        binding.table.byte_order != directory.header->byte_order ||
        binding.table.extent.end_offset > directory.entries[binding.section_index].header.bytes ||
        directory.entries[binding.section_index].header.type != SHT_PROGBITS ||
        (directory.entries[binding.section_index].header.flags & SHF_COMPRESSED) ||
        directory.backing.key.device != binding.base.selection.backing.key.device ||
        directory.backing.key.inode != binding.base.selection.backing.key.inode ||
        directory.backing.bytes != binding.base.selection.backing.bytes ||
        !tb_symbol_indexed_table_offset(&binding.table, selection.index, &offset) ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolTableValue_t))) {
        return NULL;
    }

    TbSymbolTableValue_t *value = calloc(1, sizeof(*value));
    if (!value) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolTableValue_t));
        return NULL;
    }

    value->construction      = construction;
    value->directory         = directory;
    value->selection         = selection;
    value->view.binding      = binding;
    value->view.entry.index  = selection.index;
    value->view.entry.offset = offset;
    value->failure.offset    = offset;

    value->file = tb_symbol_file_create(directory.backing, selection.descriptor, construction);
    if (!value->file) {
        tb_symbol_table_value_destroy(value);
        return NULL;
    }

    return value;
}


TbSymbolOutcome_e tb_symbol_table_value_step(TbSymbolTableValue_t *value, size_t max_bytes)
{
    if (!value)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (value->done)
        return value->outcome;

    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        value->failure.reason = TB_SYMBOL_TABLES_REQUEST;
        value->outcome        = TB_SYMBOL_OPERATIONAL_FAILURE;
        value->done           = true;
        return value->outcome;
    }

    if (value->progress == UINT64_MAX) {
        value->failure.reason = TB_SYMBOL_TABLES_REQUEST;
        value->outcome        = TB_SYMBOL_OPERATIONAL_FAILURE;
        value->done           = true;
        return value->outcome;
    }

    const TbSymbolIndexedTable_t *table = &value->view.binding.table;
    unsigned width = table->kind == TB_SYMBOL_TABLE_ADDRESSES ? table->address_bytes :
                                                             table->extent.offset_bytes;
    if (!value->reading) {
        uint64_t offset;
        const TbSymbolElfSection_t *section =
            &value->directory.entries[value->view.binding.section_index].header;
        if (table->segment_bytes || (width != 1 && width != 2 && width != 4 && width != 8)) {
            value->failure.reason = TB_SYMBOL_TABLES_ENTRY;
            value->outcome        = TB_SYMBOL_UNSUPPORTED;
            value->done           = true;
        } else if (!tb_symbol_add(section->offset, value->view.entry.offset, &offset) ||
                   !tb_symbol_range(value->view.entry.offset, width, section->bytes) ||
                   !tb_symbol_file_request(value->file, (TbSymbolFileRange_t) {
                       .offset = offset,
                       .bytes  = width
                   })) {
            value->failure.reason = TB_SYMBOL_TABLES_REQUEST;
            value->outcome        = TB_SYMBOL_OPERATIONAL_FAILURE;
            value->done           = true;
        } else {
            value->reading = true;
        }
    } else {
        uint64_t before = tb_symbol_file_progress(value->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(value->file, max_bytes);
        if (outcome == TB_SYMBOL_SUCCESS) {
            TbSymbolFileView_t bytes;
            if (!tb_symbol_file_view(value->file, &bytes)) {
                value->failure.reason = TB_SYMBOL_TABLES_REQUEST;
                value->outcome        = TB_SYMBOL_OPERATIONAL_FAILURE;
            } else {
                TbSymbolDwarfStatus_e status = tb_symbol_indexed_table_entry(*table, value->selection.index,
                    bytes.data, bytes.bytes, &value->view.entry);
                value->outcome = status == TB_SYMBOL_DWARF_OK ? TB_SYMBOL_SUCCESS :
                                 status == TB_SYMBOL_DWARF_UNSUPPORTED ? TB_SYMBOL_UNSUPPORTED :
                                 status == TB_SYMBOL_DWARF_MALFORMED ? TB_SYMBOL_MALFORMED :
                                                                     TB_SYMBOL_OPERATIONAL_FAILURE;
                if (status != TB_SYMBOL_DWARF_OK)
                    value->failure.reason = TB_SYMBOL_TABLES_ENTRY;
            }
            value->done = true;
        } else if (outcome != TB_SYMBOL_PENDING) {
            value->failure.reason = TB_SYMBOL_TABLES_READ;
            value->failure.read   = tb_symbol_file_failure(value->file);
            value->outcome        = outcome;
            value->done           = true;
        } else if (before == tb_symbol_file_progress(value->file)) {
            return value->outcome;
        }
    }

    ++value->progress;

    return value->outcome;
}


bool tb_symbol_table_value_view(const TbSymbolTableValue_t *value, TbSymbolTableValueView_t *view)
{
    if (!value || !view || !value->done || value->outcome != TB_SYMBOL_SUCCESS)
        return false;

    *view = value->view;

    return true;
}


TbSymbolTablesFailure_t tb_symbol_table_value_failure(const TbSymbolTableValue_t *value)
{
    return value ? value->failure : (TbSymbolTablesFailure_t) {
        .reason = TB_SYMBOL_TABLES_REQUEST,
        .offset = UINT64_MAX
    };
}


uint64_t tb_symbol_table_value_progress(const TbSymbolTableValue_t *value)
{
    return value ? value->progress : 0;
}


void tb_symbol_table_value_destroy(TbSymbolTableValue_t *value)
{
    if (!value)
        return;

    tb_symbol_file_destroy(value->file);
    (void)tb_symbol_budget_release(value->construction, sizeof(*value));
    free(value);
}
