#include "symbol/companion_scan.h"
#include "symbol/crc32.h"

#include <elf.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SCAN_DIRECTORY,
    SCAN_SELECT,
    SCAN_NAME,
    SCAN_DATA_BEGIN,
    SCAN_DATA,
    SCAN_CRC_BEGIN,
    SCAN_CRC,
    SCAN_DONE
} ScanPhase_e;

struct tb_symbol_companion_scan {
    TbSymbolBudget_t              *budget;
    TbSymbolSections_t            *sections;
    TbSymbolFile_t                *file;
    TbSymbolSectionsView_t         directory;
    TbSymbolCompanionInventory_t   view;
    TbSymbolCompanionScanFailure_t failure;
    TbSymbolDebuglink_t            link;
    TbSymbolBuildId_t              id;
    TbCrc32_t                      crc;
    unsigned char                *buffers;
    size_t                        buffer_bytes;
    size_t                        name_capacity;
    size_t                        id_capacity;
    uint64_t                      section;
    uint64_t                      cursor;
    uint64_t                      progress;
    uint64_t                      backend_progress;
    size_t                        consumed;
    ScanPhase_e                   phase;
    TbSymbolOutcome_e             outcome;
    bool                          reading;
    bool                          note;
};

TbSymbolCompanionScan_t *tb_symbol_companion_scan_create(TbSymbolStorageInfo_t backing, int descriptor,
    size_t name_capacity, size_t id_capacity, TbSymbolBudget_t *construction)
{
    if (!construction || descriptor < 0 || backing.bytes > INT64_MAX || !name_capacity || !id_capacity ||
        name_capacity > TB_SYMBOL_COMPANION_PATH_BYTES || id_capacity > TB_SYMBOL_COMPANION_ID_BYTES ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolCompanionScan_t))) {
        return NULL;
    }

    TbSymbolCompanionScan_t *scan = calloc(1, sizeof(*scan));
    if (!scan) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolCompanionScan_t));
        return NULL;
    }
    scan->budget        = construction;
    scan->name_capacity = name_capacity;
    scan->id_capacity   = id_capacity;
    scan->view = (TbSymbolCompanionInventory_t) {
        .backing           = backing,
        .debuglink_section = UINT64_MAX,
        .build_id_section  = UINT64_MAX,
        .build_id_note     = UINT64_MAX
    };

    size_t bytes = name_capacity + 2 * id_capacity;
    if (tb_symbol_budget_reserve(construction, bytes)) {
        scan->buffers = malloc(bytes);
        if (scan->buffers)
            scan->buffer_bytes = bytes;
        else
            (void)tb_symbol_budget_release(construction, bytes);
    }
    scan->sections = tb_symbol_sections_create(backing, descriptor, construction);
    scan->file = tb_symbol_file_create(backing, descriptor, construction);
    if (!scan->buffers || !scan->sections || !scan->file) {
        tb_symbol_companion_scan_destroy(scan);
        return NULL;
    }
    scan->view.name     = scan->buffers;
    scan->view.build_id = scan->buffers + name_capacity;

    return scan;
}


static void fail(TbSymbolCompanionScan_t *scan, TbSymbolOutcome_e outcome)
{
    scan->failure.section = scan->section;
    scan->outcome = outcome;
    scan->phase = SCAN_DONE;
}


static void metadata_failure(TbSymbolCompanionScan_t *scan, TbSymbolCompanionMetadataStatus_e status)
{
    scan->failure.metadata = status;
    fail(scan, status == TB_SYMBOL_COMPANION_METADATA_RESOURCE ? TB_SYMBOL_RESOURCE_FAILURE :
               status == TB_SYMBOL_COMPANION_METADATA_UNSUPPORTED ? TB_SYMBOL_UNSUPPORTED :
               status == TB_SYMBOL_COMPANION_METADATA_INVALID ? TB_SYMBOL_OPERATIONAL_FAILURE :
                                                               TB_SYMBOL_MALFORMED);
}


static void request(TbSymbolCompanionScan_t *scan, uint64_t offset, size_t bytes)
{
    if (!tb_symbol_file_request(scan->file, (TbSymbolFileRange_t) {
        .offset = offset,
        .bytes  = bytes
    })) {
        fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }

    scan->reading = true;
    scan->consumed = 0;
}


static void select_section(TbSymbolCompanionScan_t *scan)
{
    if (scan->section == scan->directory.count) {
        scan->phase = SCAN_CRC_BEGIN;
        scan->cursor = 0;
        (void)tb_crc32_init(&scan->crc);
        return;
    }

    const TbSymbolSectionEntry_t *entry = &scan->directory.entries[scan->section];
    if (entry->has_name && entry->name_bytes == sizeof(".gnu_debuglink") - 1) {
        request(scan, entry->name_offset, sizeof(".gnu_debuglink") - 1);
        scan->phase = SCAN_NAME;
    } else if (entry->header.type == SHT_NOTE) {
        scan->note = true;
        scan->phase = SCAN_DATA_BEGIN;
    } else {
        ++scan->section;
    }
}


static void begin_data(TbSymbolCompanionScan_t *scan)
{
    const TbSymbolElfSection_t *header = &scan->directory.entries[scan->section].header;
    if ((header->flags & SHF_COMPRESSED) ||
        (scan->note && header->alignment != 4 && header->alignment != 8)) {
        metadata_failure(scan, TB_SYMBOL_COMPANION_METADATA_UNSUPPORTED);
        return;
    }
    if (!scan->note && header->type != SHT_PROGBITS) {
        metadata_failure(scan, TB_SYMBOL_COMPANION_METADATA_MALFORMED);
        return;
    }
    if (!scan->note && scan->view.debuglink_section != UINT64_MAX) {
        metadata_failure(scan, TB_SYMBOL_COMPANION_METADATA_CONFLICT);
        return;
    }

    TbSymbolCompanionScope_t scope = {
        .backing       = scan->view.backing,
        .section_index = scan->section,
        .bytes         = header->bytes,
        .byte_order    = scan->view.header.byte_order
    };
    TbSymbolCompanionMetadataStatus_e status = scan->note ?
        tb_symbol_build_id_init(&scan->id, scope, (unsigned)header->alignment,
            scan->buffers + scan->name_capacity + scan->id_capacity, scan->id_capacity) :
        tb_symbol_debuglink_init(&scan->link, scope, scan->buffers, scan->name_capacity);
    if (status != TB_SYMBOL_COMPANION_METADATA_PENDING) {
        metadata_failure(scan, status);
        return;
    }

    scan->cursor = 0;
    scan->phase = SCAN_DATA;
}


static void complete_data(TbSymbolCompanionScan_t *scan, TbSymbolCompanionMetadataStatus_e status)
{
    if (status == TB_SYMBOL_COMPANION_METADATA_FOUND && scan->note) {
        TbSymbolBuildIdView_t id;
        if (!tb_symbol_build_id_view(&scan->id, &id)) {
            fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        if (scan->view.id_bytes) {
            metadata_failure(scan, TB_SYMBOL_COMPANION_METADATA_CONFLICT);
            return;
        }
        memcpy(scan->buffers + scan->name_capacity, id.bytes, id.length);
        scan->view.id_bytes         = id.length;
        scan->view.build_id_section = scan->section;
        scan->view.build_id_note    = id.note_offset;
    } else if (status == TB_SYMBOL_COMPANION_METADATA_FOUND) {
        TbSymbolDebuglinkView_t link;
        if (!tb_symbol_debuglink_view(&scan->link, &link)) {
            fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        scan->view.debuglink_section = scan->section;
        scan->view.name_bytes        = link.name_bytes;
        scan->view.debuglink_crc     = link.crc;
    } else if (status != TB_SYMBOL_COMPANION_METADATA_ABSENT || !scan->note) {
        metadata_failure(scan, status);
        return;
    }

    ++scan->section;
    scan->phase = SCAN_SELECT;
}


static void data_step(TbSymbolCompanionScan_t *scan, size_t max_bytes)
{
    const TbSymbolElfSection_t *header = &scan->directory.entries[scan->section].header;
    if (scan->cursor == header->bytes) {
        TbSymbolCompanionMetadataStatus_e status = scan->note ? tb_symbol_build_id_finish(&scan->id) :
                                                               tb_symbol_debuglink_finish(&scan->link);
        complete_data(scan, status);
        return;
    }
    if (scan->consumed == 0) {
        uint64_t left = header->bytes - scan->cursor;
        size_t count = left < max_bytes ? (size_t)left : max_bytes;
        if (count > 256)
            count = 256;
        request(scan, header->offset + scan->cursor, count);
        /* A distinct marker separates a ready reader from a new request. */
        scan->consumed = SIZE_MAX;
        return;
    }

    TbSymbolFileView_t bytes;
    if (!tb_symbol_file_view(scan->file, &bytes)) {
        fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    size_t consumed;
    TbSymbolCompanionMetadataStatus_e status = scan->note ?
        tb_symbol_build_id_feed(&scan->id, bytes.data, bytes.bytes, &consumed) :
        tb_symbol_debuglink_feed(&scan->link, bytes.data, bytes.bytes, &consumed);
    if (status != TB_SYMBOL_COMPANION_METADATA_PENDING &&
        status != TB_SYMBOL_COMPANION_METADATA_FOUND && status != TB_SYMBOL_COMPANION_METADATA_ABSENT) {
        metadata_failure(scan, status);
        return;
    }
    if (consumed != bytes.bytes) {
        fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    scan->cursor += consumed;
    scan->consumed = 0;
}


static void crc_step(TbSymbolCompanionScan_t *scan, size_t max_bytes)
{
    if (scan->phase == SCAN_CRC) {
        TbSymbolFileView_t bytes;
        if (!tb_symbol_file_view(scan->file, &bytes) ||
            !tb_crc32_update(&scan->crc, bytes.data, bytes.bytes)) {
            fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        scan->cursor += bytes.bytes;
        scan->phase = SCAN_CRC_BEGIN;
        return;
    }
    if (scan->cursor == scan->view.backing.bytes) {
        if (!tb_crc32_final(&scan->crc, scan->view.backing.bytes, &scan->view.file_crc)) {
            fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        scan->outcome = TB_SYMBOL_SUCCESS;
        scan->phase = SCAN_DONE;
        return;
    }

    uint64_t left = scan->view.backing.bytes - scan->cursor;
    size_t count = left < max_bytes ? (size_t)left : max_bytes;
    request(scan, scan->cursor, count);
    scan->phase = SCAN_CRC;
}


TbSymbolOutcome_e tb_symbol_companion_scan_step(TbSymbolCompanionScan_t *scan, size_t max_bytes)
{
    if (!scan)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (!max_bytes || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES || max_bytes < 64)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (scan->outcome != TB_SYMBOL_PENDING)
        return scan->outcome;

    ScanPhase_e before = scan->phase;
    uint64_t section = scan->section;
    uint64_t cursor = scan->cursor;
    bool reading = scan->reading;
    size_t consumed = scan->consumed;
    uint64_t backend;
    if (scan->reading) {
        TbSymbolOutcome_e status = tb_symbol_file_step(scan->file, max_bytes);
        if (status == TB_SYMBOL_SUCCESS) {
            scan->reading = false;
        } else if (status != TB_SYMBOL_PENDING) {
            scan->failure.read = tb_symbol_file_failure(scan->file);
            fail(scan, status);
        }
    } else {
        switch (scan->phase) {
        case SCAN_DIRECTORY: {
            TbSymbolOutcome_e status = tb_symbol_sections_step(scan->sections, max_bytes);
            if (status == TB_SYMBOL_SUCCESS) {
                if (!tb_symbol_sections_view(scan->sections, &scan->directory)) {
                    fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
                    break;
                }
                scan->view.header = *scan->directory.header;
                scan->view.debuglink_known = scan->directory.names == TB_SYMBOL_SECTION_NAMES_AVAILABLE;
                scan->phase = SCAN_SELECT;
            } else if (status != TB_SYMBOL_PENDING) {
                scan->failure.directory = tb_symbol_sections_failure(scan->sections);
                fail(scan, status);
            }
            break;
        }
        case SCAN_SELECT:
            select_section(scan);
            break;
        case SCAN_NAME: {
            TbSymbolFileView_t bytes;
            if (!tb_symbol_file_view(scan->file, &bytes)) {
                fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
                break;
            }
            bool named = memcmp(bytes.data, ".gnu_debuglink", sizeof(".gnu_debuglink") - 1) == 0;
            scan->note = !named;
            if (named || scan->directory.entries[scan->section].header.type == SHT_NOTE)
                scan->phase = SCAN_DATA_BEGIN;
            else {
                ++scan->section;
                scan->phase = SCAN_SELECT;
            }
            break;
        }
        case SCAN_DATA_BEGIN:
            scan->consumed = 0;
            begin_data(scan);
            break;
        case SCAN_DATA:
            data_step(scan, max_bytes);
            break;
        case SCAN_CRC_BEGIN:
        case SCAN_CRC:
            crc_step(scan, max_bytes);
            break;
        case SCAN_DONE:
            break;
        }
    }
    backend = scan->phase == SCAN_DIRECTORY ? tb_symbol_sections_progress(scan->sections) :
                                             tb_symbol_file_progress(scan->file);
    if (before != scan->phase || section != scan->section || cursor != scan->cursor ||
        reading != scan->reading || consumed != scan->consumed || backend != scan->backend_progress) {
        if (scan->progress == UINT64_MAX)
            fail(scan, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++scan->progress;
    }
    scan->backend_progress = backend;

    return scan->outcome;
}


bool tb_symbol_companion_scan_view(const TbSymbolCompanionScan_t *scan, TbSymbolCompanionInventory_t *view)
{
    if (!scan || !view || scan->outcome != TB_SYMBOL_SUCCESS)
        return false;

    *view = scan->view;

    return true;
}


TbSymbolCompanionScanFailure_t tb_symbol_companion_scan_failure(const TbSymbolCompanionScan_t *scan)
{
    return scan ? scan->failure : (TbSymbolCompanionScanFailure_t) {0};
}


uint64_t tb_symbol_companion_scan_progress(const TbSymbolCompanionScan_t *scan)
{
    return scan ? scan->progress : 0;
}


void tb_symbol_companion_scan_destroy(TbSymbolCompanionScan_t *scan)
{
    if (!scan)
        return;

    tb_symbol_sections_destroy(scan->sections);
    tb_symbol_file_destroy(scan->file);
    free(scan->buffers);
    (void)tb_symbol_budget_release(scan->budget, scan->buffer_bytes);
    (void)tb_symbol_budget_release(scan->budget, sizeof(*scan));
    free(scan);
}


TbSymbolCompanionRelation_e tb_symbol_companion_relation(const TbSymbolCompanionInventory_t *primary,
    const TbSymbolCompanionInventory_t *candidate, TbSymbolCompanionMatch_e method)
{
    if (!primary || !candidate ||
        (method != TB_SYMBOL_COMPANION_BY_DEBUGLINK && method != TB_SYMBOL_COMPANION_BY_BUILD_ID) ||
        primary->id_bytes > TB_SYMBOL_COMPANION_ID_BYTES ||
        candidate->id_bytes > TB_SYMBOL_COMPANION_ID_BYTES ||
        (primary->id_bytes && !primary->build_id) || (candidate->id_bytes && !candidate->build_id)) {
        return TB_SYMBOL_COMPANION_RELATION_INVALID;
    }

    const TbSymbolElfHeader_t *left = &primary->header;
    const TbSymbolElfHeader_t *right = &candidate->header;
    if ((left->object_type != ET_EXEC && left->object_type != ET_DYN) ||
        (right->object_type != ET_EXEC && right->object_type != ET_DYN)) {
        return TB_SYMBOL_COMPANION_RELATION_UNSUPPORTED;
    }
    if (left->object_type != right->object_type || left->machine != right->machine ||
        left->byte_order != right->byte_order || left->address_bytes != right->address_bytes ||
        left->flags != right->flags || left->os_abi != right->os_abi ||
        left->abi_version != right->abi_version) {
        return TB_SYMBOL_COMPANION_RELATION_ELF_MISMATCH;
    }
    if (primary->id_bytes && candidate->id_bytes &&
        (primary->id_bytes != candidate->id_bytes ||
         memcmp(primary->build_id, candidate->build_id, primary->id_bytes) != 0)) {
        return TB_SYMBOL_COMPANION_RELATION_ID_MISMATCH;
    }
    if (method == TB_SYMBOL_COMPANION_BY_DEBUGLINK) {
        if (!primary->debuglink_known || !primary->name_bytes || primary->debuglink_section == UINT64_MAX)
            return TB_SYMBOL_COMPANION_RELATION_MISSING;
        return primary->debuglink_crc == candidate->file_crc ? TB_SYMBOL_COMPANION_RELATION_MATCH :
                                                             TB_SYMBOL_COMPANION_RELATION_CRC_MISMATCH;
    }

    return primary->id_bytes && candidate->id_bytes ? TB_SYMBOL_COMPANION_RELATION_MATCH :
                                                    TB_SYMBOL_COMPANION_RELATION_MISSING;
}
