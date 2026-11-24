#ifndef THE_BUGGER_SYMBOL_COMPANION_SCAN_H
#define THE_BUGGER_SYMBOL_COMPANION_SCAN_H

#include "symbol/companion_metadata.h"
#include "symbol/companion_paths.h"
#include "symbol/sections.h"

typedef struct tb_symbol_companion_scan TbSymbolCompanionScan_t;

typedef struct {
    TbSymbolStorageInfo_t backing;
    TbSymbolElfHeader_t   header;
    const unsigned char  *name;
    const unsigned char  *build_id;
    size_t                name_bytes;
    size_t                id_bytes;
    uint64_t              debuglink_section;
    uint64_t              build_id_section;
    uint64_t              build_id_note;
    uint32_t              debuglink_crc;
    uint32_t              file_crc;
    bool                  debuglink_known;
} TbSymbolCompanionInventory_t;

typedef struct {
    uint64_t                         section;
    TbSymbolFileFailure_t             read;
    TbSymbolSectionsFailure_t         directory;
    TbSymbolCompanionMetadataStatus_e metadata;
} TbSymbolCompanionScanFailure_t;

/* Helper-only selected section inventory and complete-file CRC, independently
 * testable without IPC. Caller pins frozen backing/descriptor through destroy.
 * No source-path access or discovery. Requires an explicit construction budget,
 * full basename capacity (including NUL) and ID capacity within schema maxima.
 * All allocations/retained capacities are charged; no production defaults.
 * Complete directory, all SHT_NOTE sections and full CRC precede publication.
 * Program-header-only notes remain unexamined; absent IDs are section-scoped.
 * Missing/compressed section names make debuglink presence unknown, not absent. */
TbSymbolCompanionScan_t *tb_symbol_companion_scan_create(TbSymbolStorageInfo_t backing, int descriptor,
    size_t name_capacity, size_t id_capacity, TbSymbolBudget_t *construction);
/* One bounded action/read (64..65536 byte allowance); metadata feeds <=256 bytes.
 * Outer helper owns context revisions, cancellation, deadlines and retirement. */
TbSymbolOutcome_e tb_symbol_companion_scan_step(TbSymbolCompanionScan_t *scan, size_t max_bytes);
bool tb_symbol_companion_scan_view(const TbSymbolCompanionScan_t *scan, TbSymbolCompanionInventory_t *view);
TbSymbolCompanionScanFailure_t tb_symbol_companion_scan_failure(const TbSymbolCompanionScan_t *scan);
uint64_t tb_symbol_companion_scan_progress(const TbSymbolCompanionScan_t *scan);
void tb_symbol_companion_scan_destroy(TbSymbolCompanionScan_t *scan);

typedef enum {
    TB_SYMBOL_COMPANION_BY_DEBUGLINK = 1,
    TB_SYMBOL_COMPANION_BY_BUILD_ID = 2
} TbSymbolCompanionMatch_e;

typedef enum {
    TB_SYMBOL_COMPANION_RELATION_MATCH,
    TB_SYMBOL_COMPANION_RELATION_INVALID,
    TB_SYMBOL_COMPANION_RELATION_UNSUPPORTED,
    TB_SYMBOL_COMPANION_RELATION_ELF_MISMATCH,
    TB_SYMBOL_COMPANION_RELATION_MISSING,
    TB_SYMBOL_COMPANION_RELATION_CRC_MISMATCH,
    TB_SYMBOL_COMPANION_RELATION_ID_MISMATCH
} TbSymbolCompanionRelation_e;

/* Pure comparison of complete validated inventories, including optional build-ID
 * agreement even on the CRC route. Inputs retain exact independent backings;
 * equal keys do not merge contexts. No attachment, byte identity, DWARF validity,
 * coherent acquisition, runtime/source correspondence or access authority.
 * Header profile conservatively requires linked ELF with equal class, endian,
 * machine, type, flags, OS ABI and ABI version. ID compares at most schema max.
 * Caller validates retained receipts and their captured context/evidence revisions. */
TbSymbolCompanionRelation_e tb_symbol_companion_relation(const TbSymbolCompanionInventory_t *primary,
    const TbSymbolCompanionInventory_t *candidate, TbSymbolCompanionMatch_e method);

#endif // THE_BUGGER_SYMBOL_COMPANION_SCAN_H
