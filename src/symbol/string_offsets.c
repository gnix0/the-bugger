#include "symbol/string_offsets.h"

#include <elf.h>
#include <stdlib.h>

typedef enum {
    OFFSETS_SECTION,
    OFFSETS_LENGTH_REQUEST,
    OFFSETS_LENGTH,
    OFFSETS_HEADER,
    OFFSETS_ENTRY,
    OFFSETS_DONE,
    OFFSETS_FAILED
} OffsetsPhase_e;

struct tb_symbol_string_offsets {
    TbSymbolBudget_t               *construction;
    TbSymbolFile_t                 *file;
    TbSymbolSectionsView_t          directory;
    TbSymbolStringOffsetSelection_t selection;
    TbSymbolElfSection_t            section;
    TbSymbolDwarfLength_t           extent;
    uint64_t                        cursor;
    uint64_t                        base;
    uint64_t                        count;
    uint64_t                        entry_offset;
    uint64_t                        string_offset;
    bool                            reading;
    uint64_t                        progress;
    OffsetsPhase_e                  phase;
    TbSymbolOutcome_e               outcome;
    TbSymbolStringOffsetFailure_t   failure;
};

TbSymbolStringOffsets_t *tb_symbol_string_offsets_create(TbSymbolSectionsView_t directory,
                                                         TbSymbolStringOffsetSelection_t selection,
                                                         TbSymbolBudget_t *construction)
{
    if (!directory.header || !directory.layout || !directory.entries || selection.descriptor < 0 ||
        directory.count != directory.layout->sections.count || selection.section_index >= directory.count ||
        directory.backing.bytes != directory.header->file_bytes ||
        (selection.offset_bytes != 4 && selection.offset_bytes != 8) || !construction ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolStringOffsets_t))) {
        return NULL;
    }
    TbSymbolStringOffsets_t *offsets = calloc(1, sizeof(*offsets));
    if (!offsets) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolStringOffsets_t));
        return NULL;
    }
    offsets->construction = construction;
    offsets->directory    = directory;
    offsets->selection    = selection;
    offsets->section      = directory.entries[selection.section_index].header;
    offsets->file = tb_symbol_file_create(directory.backing, selection.descriptor, construction);
    if (!offsets->file) {
        tb_symbol_string_offsets_destroy(offsets);
        return NULL;
    }
    return offsets;
}


static void fail(TbSymbolStringOffsets_t *offsets, TbSymbolStringOffsetFailure_e reason,
                 TbSymbolOutcome_e outcome)
{
    offsets->failure.reason              = reason;
    offsets->failure.contribution_offset = offsets->cursor;
    offsets->phase                       = OFFSETS_FAILED;
    offsets->outcome                     = outcome;
}


static void request(TbSymbolStringOffsets_t *offsets, TbSymbolFileRange_t range, OffsetsPhase_e next)
{
    uint64_t absolute;
    if (!tb_symbol_add(offsets->section.offset, range.offset, &absolute) ||
        !tb_symbol_file_request(offsets->file, (TbSymbolFileRange_t){
            .offset = absolute,
            .bytes  = range.bytes
        })) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else {
        offsets->reading = true;
        offsets->phase = next;
    }
}


static void length(TbSymbolStringOffsets_t *offsets)
{
    TbSymbolFileView_t view;
    if (!tb_symbol_file_view(offsets->file, &view)) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    TbSymbolDwarfStatus_e status = tb_symbol_dwarf_length((TbSymbolDwarfSpan_t){
        .section_bytes = offsets->section.bytes,
        .unit_offset   = offsets->cursor,
        .byte_order    = offsets->directory.header->byte_order
    }, view.data, view.bytes, &offsets->extent);
    if (status != TB_SYMBOL_DWARF_OK) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_LENGTH, TB_SYMBOL_MALFORMED);
    } else if (offsets->selection.base > offsets->extent.end_offset) {
        offsets->cursor = offsets->extent.end_offset;
        offsets->phase = OFFSETS_LENGTH_REQUEST;
    } else if (offsets->extent.total_bytes < offsets->extent.length_bytes + 4U) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_HEADER, TB_SYMBOL_MALFORMED);
    } else {
        request(offsets, (TbSymbolFileRange_t){
            .offset = offsets->cursor,
            .bytes  = offsets->extent.length_bytes + 4U
        }, OFFSETS_HEADER);
    }
}


static void header(TbSymbolStringOffsets_t *offsets)
{
    TbSymbolFileView_t view;
    TbSymbolReader_t reader;
    const unsigned char *prefix;
    uint64_t version;
    uint64_t padding;
    if (!tb_symbol_file_view(offsets->file, &view) ||
        tb_symbol_reader_init(&reader, view.data, view.bytes, offsets->directory.header->byte_order) !=
            TB_SYMBOL_READ_OK ||
        tb_symbol_reader_bytes(&reader, offsets->extent.length_bytes, &prefix) != TB_SYMBOL_READ_OK ||
        tb_symbol_reader_uint(&reader, 2, &version) != TB_SYMBOL_READ_OK ||
        tb_symbol_reader_uint(&reader, 2, &padding) != TB_SYMBOL_READ_OK) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    if (version != 5) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_VERSION, TB_SYMBOL_UNSUPPORTED);
    } else if (padding) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_PADDING, TB_SYMBOL_MALFORMED);
    } else if (offsets->extent.offset_bytes != offsets->selection.offset_bytes) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_FORMAT, TB_SYMBOL_MALFORMED);
    } else {
        uint64_t header_bytes = offsets->extent.length_bytes + 4U;
        uint64_t payload = offsets->extent.total_bytes - header_bytes;
        if (payload % offsets->extent.offset_bytes ||
            !tb_symbol_add(offsets->cursor, header_bytes, &offsets->base)) {
            fail(offsets, TB_SYMBOL_STRING_OFFSETS_HEADER, TB_SYMBOL_MALFORMED);
            return;
        }
        offsets->count = payload / offsets->extent.offset_bytes;
        if (offsets->selection.base != offsets->base) {
            fail(offsets, TB_SYMBOL_STRING_OFFSETS_BASE, TB_SYMBOL_MALFORMED);
            return;
        }
        uint64_t delta;
        if (offsets->selection.index >= offsets->count ||
            !tb_symbol_multiply(offsets->selection.index, offsets->extent.offset_bytes, &delta) ||
            !tb_symbol_add(offsets->base, delta, &offsets->entry_offset)) {
            fail(offsets, TB_SYMBOL_STRING_OFFSETS_INDEX, TB_SYMBOL_MALFORMED);
            return;
        }
        request(offsets, (TbSymbolFileRange_t){
            .offset = offsets->entry_offset,
            .bytes  = offsets->extent.offset_bytes
        }, OFFSETS_ENTRY);
    }
}


static void entry(TbSymbolStringOffsets_t *offsets)
{
    TbSymbolFileView_t view;
    TbSymbolReader_t reader;
    if (!tb_symbol_file_view(offsets->file, &view) ||
        tb_symbol_reader_init(&reader, view.data, view.bytes, offsets->directory.header->byte_order) !=
            TB_SYMBOL_READ_OK ||
        tb_symbol_reader_uint(&reader, offsets->extent.offset_bytes, &offsets->string_offset) !=
            TB_SYMBOL_READ_OK) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    offsets->phase = OFFSETS_DONE;
    offsets->outcome = TB_SYMBOL_SUCCESS;
}


TbSymbolOutcome_e tb_symbol_string_offsets_step(TbSymbolStringOffsets_t *offsets, size_t max_bytes)
{
    if (!offsets)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (offsets->phase == OFFSETS_DONE || offsets->phase == OFFSETS_FAILED)
        return offsets->outcome;
    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        fail(offsets, TB_SYMBOL_STRING_OFFSETS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return offsets->outcome;
    }
    bool progressed = true;
    if (offsets->reading) {
        uint64_t before = tb_symbol_file_progress(offsets->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(offsets->file, max_bytes);
        progressed = before != tb_symbol_file_progress(offsets->file);
        if (outcome == TB_SYMBOL_SUCCESS) {
            offsets->reading = false;
        } else if (outcome != TB_SYMBOL_PENDING) {
            offsets->failure.read = tb_symbol_file_failure(offsets->file);
            fail(offsets, TB_SYMBOL_STRING_OFFSETS_READ, outcome);
        }
    } else {
        switch (offsets->phase) {
        case OFFSETS_SECTION:
            if (offsets->section.type != SHT_PROGBITS || (offsets->section.flags & SHF_COMPRESSED)) {
                fail(offsets, TB_SYMBOL_STRING_OFFSETS_SECTION, TB_SYMBOL_UNSUPPORTED);
            } else if (offsets->selection.base > offsets->section.bytes) {
                fail(offsets, TB_SYMBOL_STRING_OFFSETS_BASE, TB_SYMBOL_MALFORMED);
            } else {
                offsets->phase = OFFSETS_LENGTH_REQUEST;
            }
            break;
        case OFFSETS_LENGTH_REQUEST:
            if (offsets->cursor >= offsets->section.bytes) {
                fail(offsets, TB_SYMBOL_STRING_OFFSETS_BASE, TB_SYMBOL_MALFORMED);
            } else {
                uint64_t remaining = offsets->section.bytes - offsets->cursor;
                request(offsets, (TbSymbolFileRange_t){
                    .offset = offsets->cursor,
                    .bytes  = remaining < 12 ? (size_t)remaining : 12
                }, OFFSETS_LENGTH);
            }
            break;
        case OFFSETS_LENGTH:
            length(offsets);
            break;
        case OFFSETS_HEADER:
            header(offsets);
            break;
        case OFFSETS_ENTRY:
            entry(offsets);
            break;
        case OFFSETS_DONE:
        case OFFSETS_FAILED:
            break;
        }
    }
    if (progressed) {
        if (offsets->progress == UINT64_MAX)
            fail(offsets, TB_SYMBOL_STRING_OFFSETS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++offsets->progress;
    }
    return offsets->outcome;
}


bool tb_symbol_string_offsets_view(const TbSymbolStringOffsets_t *offsets, TbSymbolStringOffsetView_t *view)
{
    if (!offsets || !view || offsets->phase != OFFSETS_DONE || offsets->outcome != TB_SYMBOL_SUCCESS)
        return false;
    *view = (TbSymbolStringOffsetView_t){
        .backing             = offsets->directory.backing,
        .section_index       = offsets->selection.section_index,
        .contribution_offset = offsets->cursor,
        .end_offset          = offsets->extent.end_offset,
        .base                = offsets->base,
        .count               = offsets->count,
        .index               = offsets->selection.index,
        .entry_offset        = offsets->entry_offset,
        .string_offset       = offsets->string_offset,
        .offset_bytes        = offsets->extent.offset_bytes
    };
    return true;
}


TbSymbolStringOffsetFailure_t tb_symbol_string_offsets_failure(const TbSymbolStringOffsets_t *offsets)
{
    return offsets ? offsets->failure : (TbSymbolStringOffsetFailure_t){
        .reason              = TB_SYMBOL_STRING_OFFSETS_REQUEST,
        .contribution_offset = UINT64_MAX
    };
}


uint64_t tb_symbol_string_offsets_progress(const TbSymbolStringOffsets_t *offsets)
{
    return offsets ? offsets->progress : 0;
}


void tb_symbol_string_offsets_destroy(TbSymbolStringOffsets_t *offsets)
{
    if (!offsets)
        return;
    tb_symbol_file_destroy(offsets->file);
    (void)tb_symbol_budget_release(offsets->construction, sizeof(*offsets));
    free(offsets);
}
