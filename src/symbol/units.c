#include "symbol/units.h"

#include <elf.h>
#include <errno.h>
#include <stdlib.h>

typedef enum {
    UNITS_SECTION,
    UNITS_BEGIN,
    UNITS_NEXT,
    UNITS_DECODE,
    UNITS_ALLOCATE,
    UNITS_DONE,
    UNITS_FAILED
} UnitsPhase_e;

struct tb_symbol_units {
    TbSymbolBudget_t       *construction;
    TbSymbolFile_t         *file;
    TbSymbolSectionsView_t  directory;
    TbSymbolUnitSelection_t selection;
    TbSymbolElfSection_t    section;
    TbSymbolUnitEntry_t    *entries;
    size_t                 array_bytes;
    uint64_t               count;
    uint64_t               unsupported;
    uint64_t               written;
    uint64_t               unsupported_written;
    uint64_t               cursor;
    uint64_t               progress;
    bool                   populating;
    bool                   reading;
    UnitsPhase_e           phase;
    TbSymbolOutcome_e      outcome;
    TbSymbolUnitsFailure_t failure;
};

TbSymbolUnits_t *tb_symbol_units_create(TbSymbolSectionsView_t directory,
                                       TbSymbolUnitSelection_t selection, TbSymbolBudget_t *construction)
{
    if (!directory.header || !directory.layout || !directory.entries || selection.descriptor < 0 ||
        selection.section_index >= directory.count || directory.count != directory.layout->sections.count ||
        directory.backing.bytes != directory.header->file_bytes ||
        (selection.kind != TB_SYMBOL_DWARF_INFO && selection.kind != TB_SYMBOL_DWARF_TYPES) ||
        !construction || !tb_symbol_budget_reserve(construction, sizeof(TbSymbolUnits_t))) {
        return NULL;
    }

    TbSymbolUnits_t *units = calloc(1, sizeof(*units));
    if (!units) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolUnits_t));
        return NULL;
    }

    units->construction = construction;
    units->directory    = directory;
    units->selection    = selection;
    units->section      = directory.entries[selection.section_index].header;
    units->cursor       = UINT64_MAX;
    units->file = tb_symbol_file_create(directory.backing, selection.descriptor, construction);

    if (!units->file) {
        tb_symbol_units_destroy(units);
        return NULL;
    }

    return units;
}


static void fail(TbSymbolUnits_t *units, TbSymbolUnitsFailure_e reason, TbSymbolOutcome_e outcome)
{
    units->failure.reason      = reason;
    units->failure.unit_offset = units->cursor;
    units->outcome             = outcome;
    units->phase               = UNITS_FAILED;
}


static void request(TbSymbolUnits_t *units, TbSymbolFileRange_t range, UnitsPhase_e next)
{
    if (!tb_symbol_file_request(units->file, range)) {
        fail(units, TB_SYMBOL_UNITS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else {
        units->reading = true;
        units->phase = next;
    }
}


static void section(TbSymbolUnits_t *units)
{
    if (units->section.type != SHT_PROGBITS || (units->section.flags & SHF_COMPRESSED)) {
        fail(units, TB_SYMBOL_UNITS_SECTION, TB_SYMBOL_UNSUPPORTED);
        return;
    }

    /* Even an empty selected section must not accept a replacement descriptor. */
    request(units, (TbSymbolFileRange_t) {
        .offset = 0,
        .bytes  = 1
    }, UNITS_BEGIN);
}


static void next(TbSymbolUnits_t *units)
{
    if (units->cursor == units->section.bytes) {
        if (!units->populating) {
            units->phase = UNITS_ALLOCATE;
        } else if (units->written != units->count || units->unsupported_written != units->unsupported) {
            fail(units, TB_SYMBOL_UNITS_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
        } else {
            units->phase = UNITS_DONE;
            units->outcome = TB_SYMBOL_SUCCESS;
        }
        return;
    }

    uint64_t offset;
    if (units->cursor > units->section.bytes ||
        !tb_symbol_add(units->section.offset, units->cursor, &offset)) {
        fail(units, TB_SYMBOL_UNITS_LENGTH, TB_SYMBOL_MALFORMED);
        return;
    }

    uint64_t remaining = units->section.bytes - units->cursor;

    request(units, (TbSymbolFileRange_t) {
        .offset = offset,
        .bytes  = remaining < 40 ? (size_t)remaining : 40
    }, UNITS_DECODE);
}


static void decode(TbSymbolUnits_t *units)
{
    TbSymbolFileView_t view;
    if (!tb_symbol_file_view(units->file, &view)) {
        fail(units, TB_SYMBOL_UNITS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }

    TbSymbolDwarfSpan_t span = {
        .section_bytes = units->section.bytes,
        .unit_offset   = units->cursor,
        .byte_order    = units->directory.header->byte_order
    };

    TbSymbolUnitEntry_t entry = {
        .offset = units->cursor
    };

    TbSymbolDwarfStatus_e status = tb_symbol_dwarf_length(span, view.data, view.bytes, &entry.extent);
    if (status != TB_SYMBOL_DWARF_OK) {
        fail(units, TB_SYMBOL_UNITS_LENGTH, status == TB_SYMBOL_DWARF_INVALID
                                               ? TB_SYMBOL_OPERATIONAL_FAILURE : TB_SYMBOL_MALFORMED);
        return;
    }

    status = tb_symbol_dwarf_unit(units->selection.kind, span, view.data, view.bytes, &entry.header);

    if (status != TB_SYMBOL_DWARF_OK && status != TB_SYMBOL_DWARF_UNSUPPORTED) {
        fail(units, TB_SYMBOL_UNITS_HEADER, status == TB_SYMBOL_DWARF_INVALID
                                               ? TB_SYMBOL_OPERATIONAL_FAILURE : TB_SYMBOL_MALFORMED);
        return;
    }

    entry.header_available = status == TB_SYMBOL_DWARF_OK;

    if (units->populating) {
        if (units->written >= units->count) {
            fail(units, TB_SYMBOL_UNITS_RECONSTRUCTION, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }

        units->entries[units->written++] = entry;

        if (!entry.header_available)
            ++units->unsupported_written;
    } else {
        if (units->count == UINT64_MAX) {
            fail(units, TB_SYMBOL_UNITS_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
            return;
        }

        ++units->count;

        if (!entry.header_available)
            ++units->unsupported;
    }
    units->cursor = entry.extent.end_offset;
    units->phase = UNITS_NEXT;
}


static void allocate(TbSymbolUnits_t *units)
{
    uint64_t required;
    if (!tb_symbol_multiply(units->count, sizeof(*units->entries), &required) || required > SIZE_MAX ||
        !tb_symbol_budget_reserve(units->construction, (size_t)required)) {
        fail(units, TB_SYMBOL_UNITS_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }

    units->array_bytes = (size_t)required;

    if (required) {
        units->entries = malloc(units->array_bytes);

        if (!units->entries) {
            units->failure.system_error = ENOMEM;
            fail(units, TB_SYMBOL_UNITS_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
            return;
        }
    }
    units->cursor = 0;
    units->populating = true;
    units->phase = UNITS_NEXT;
}


TbSymbolOutcome_e tb_symbol_units_step(TbSymbolUnits_t *units, size_t max_bytes)
{
    if (!units)
        return TB_SYMBOL_OPERATIONAL_FAILURE;

    if (units->phase == UNITS_DONE || units->phase == UNITS_FAILED)
        return units->outcome;

    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        fail(units, TB_SYMBOL_UNITS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return units->outcome;
    }

    bool progressed = true;

    if (units->reading) {
        uint64_t before = tb_symbol_file_progress(units->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(units->file, max_bytes);

        progressed = before != tb_symbol_file_progress(units->file);

        if (outcome == TB_SYMBOL_SUCCESS) {
            units->reading = false;
        } else if (outcome != TB_SYMBOL_PENDING) {
            units->failure.read = tb_symbol_file_failure(units->file);
            fail(units, TB_SYMBOL_UNITS_READ, outcome);
        }
    } else {
        switch (units->phase) {
        case UNITS_SECTION:
            section(units);
            break;
        case UNITS_BEGIN:
            units->cursor = 0;
            units->phase = UNITS_NEXT;
            break;
        case UNITS_NEXT:
            next(units);
            break;
        case UNITS_DECODE:
            decode(units);
            break;
        case UNITS_ALLOCATE:
            allocate(units);
            break;
        case UNITS_DONE:
        case UNITS_FAILED:
            break;
        }
    }

    if (progressed) {
        if (units->progress == UINT64_MAX)
            fail(units, TB_SYMBOL_UNITS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++units->progress;
    }

    return units->outcome;
}


bool tb_symbol_units_view(const TbSymbolUnits_t *units, TbSymbolUnitsView_t *view)
{
    if (!units || !view || units->phase != UNITS_DONE || units->outcome != TB_SYMBOL_SUCCESS)
        return false;

    *view = (TbSymbolUnitsView_t) {
        .entries       = units->entries,
        .count         = units->count,
        .unsupported   = units->unsupported,
        .section_index = units->selection.section_index,
        .kind          = units->selection.kind,
        .backing       = units->directory.backing
    };

    return true;
}


TbSymbolUnitsFailure_t tb_symbol_units_failure(const TbSymbolUnits_t *units)
{
    return units ? units->failure : (TbSymbolUnitsFailure_t) {
        .reason      = TB_SYMBOL_UNITS_REQUEST,
        .unit_offset = UINT64_MAX
    };
}


uint64_t tb_symbol_units_progress(const TbSymbolUnits_t *units)
{
    return units ? units->progress : 0;
}


void tb_symbol_units_destroy(TbSymbolUnits_t *units)
{
    if (!units)
        return;

    tb_symbol_file_destroy(units->file);
    free(units->entries);

    if (units->array_bytes)
        (void)tb_symbol_budget_release(units->construction, units->array_bytes);
    (void)tb_symbol_budget_release(units->construction, sizeof(*units));
    free(units);
}
