#ifndef THE_BUGGER_SYMBOL_ADMISSION_H
#define THE_BUGGER_SYMBOL_ADMISSION_H

#include "symbol/budget.h"

#include <stdint.h>

#define TB_SYMBOL_FILE_BYTES_DEFAULT (UINT64_C(256) * 1024 * 1024)
#define TB_SYMBOL_ADMISSION_BYTES_DEFAULT (UINT64_C(1024) * 1024 * 1024)

typedef struct tb_symbol_admission TbSymbolAdmissionLedger_t;
typedef struct tb_symbol_reservation TbSymbolReservation_t;
typedef struct tb_symbol_backing_charge TbSymbolBackingCharge_t;

typedef enum {
    TB_SYMBOL_CHARGE_RESERVED,
    TB_SYMBOL_CHARGE_OPEN,
    TB_SYMBOL_CHARGE_CLEANUP
} TbSymbolChargeState_e;

typedef struct {
    uint64_t bytes;
    uint64_t reservations;
    uint64_t open_contexts;
    uint64_t cached_backings;
    uint64_t cleanup_pending;
} TbSymbolAdmissionUsage_t;

/* Logical storage accounting, independent of filesystem capacity and allocation
 * accounting. This ledger performs no I/O, validation, deduplication or eviction.
 * Both limits are startup ceilings; allocation ownership uses the optional
 * frontend account, which must outlive the ledger and all of its tickets.
 * Use only live handles returned by this ledger; consumed handles are invalid. */
TbSymbolAdmissionLedger_t *tb_symbol_admission_create(uint64_t file_bytes, uint64_t aggregate_bytes,
                                                     TbSymbolBudget_t *frontend);
/* Refuses destruction while any reservation, context or backing is owned. */
bool tb_symbol_admission_destroy(TbSymbolAdmissionLedger_t *ledger);
TbSymbolAdmissionUsage_t tb_symbol_admission_usage(const TbSymbolAdmissionLedger_t *ledger);

/* Reserve before storage growth. Every independent open/companion needs its own
 * ticket, even if the eventual bytes are shared. A failed call changes nothing.
 * The returned opaque ticket is an internal owner handle, not artifact identity. */
TbSymbolReservation_t *tb_symbol_admission_reserve(TbSymbolAdmissionLedger_t *ledger, uint64_t bytes);
TbSymbolChargeState_e tb_symbol_admission_state(const TbSymbolReservation_t *reservation);
uint64_t tb_symbol_admission_size(const TbSymbolReservation_t *reservation);

/* Publication requires independently validated acquisition and closed writers.
 * Fresh backing uses ownership preallocated by reserve; commit cannot allocate.
 * For shared backing, the caller must first prove complete byte equality. The
 * ledger checks ownership, length and lifecycle, never infers equality itself.
 * Successful shared commit consumes the reservation's temporary backing owner:
 * staging cleanup must already be confirmed before calling it. */
TbSymbolBackingCharge_t *tb_symbol_admission_commit(TbSymbolReservation_t *reservation,
                                                   TbSymbolBackingCharge_t *shared);

/* Abort a reservation only after fencing storage growth. Close only after
 * detaching affected work. Neither operation confirms physical cleanup.
 * A final close transfers its full charge to cached backing; earlier closes
 * release their independent context charge, since the backing remains pinned.
 * Each successful close/cleaned call consumes its ticket. */
bool tb_symbol_admission_abort(TbSymbolReservation_t *reservation);
bool tb_symbol_admission_close(TbSymbolReservation_t *reservation);
bool tb_symbol_admission_cleaned(TbSymbolReservation_t *reservation);

/* Only closed cached backing is eligible. Eviction is irreversible; its charge
 * remains until physical cleanup has been confirmed. A backing in cleanup cannot
 * be shared or reopened. Successful backing_cleaned consumes the owner handle. */
bool tb_symbol_admission_evict(TbSymbolBackingCharge_t *backing);
bool tb_symbol_admission_backing_cleaned(TbSymbolBackingCharge_t *backing);

#endif // THE_BUGGER_SYMBOL_ADMISSION_H
