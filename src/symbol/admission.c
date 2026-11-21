#include "symbol/admission.h"

#include <stdlib.h>

struct tb_symbol_admission {
    TbSymbolBudget_t        *frontend;
    uint64_t                 file_bytes;
    uint64_t                 aggregate_bytes;
    TbSymbolAdmissionUsage_t usage;
};

struct tb_symbol_backing_charge {
    TbSymbolAdmissionLedger_t *owner;
    uint64_t                   bytes;
    uint64_t                   contexts;
    bool                       retiring;
};

struct tb_symbol_reservation {
    TbSymbolAdmissionLedger_t *owner;
    TbSymbolBackingCharge_t   *backing;
    uint64_t                   bytes;
    TbSymbolChargeState_e      state;
    bool                       needs_size;
};

static bool reserve_allocation(TbSymbolBudget_t *frontend, size_t bytes)
{
    return !frontend || tb_symbol_budget_reserve(frontend, bytes);
}


static void release_allocation(TbSymbolBudget_t *frontend, size_t bytes)
{
    if (frontend)
        (void)tb_symbol_budget_release(frontend, bytes);
}


TbSymbolAdmissionLedger_t *tb_symbol_admission_create(uint64_t file_bytes, uint64_t aggregate_bytes,
                                                     TbSymbolBudget_t *frontend)
{
    if (!file_bytes || !aggregate_bytes || file_bytes > aggregate_bytes ||
        !reserve_allocation(frontend, sizeof(TbSymbolAdmissionLedger_t))) {
        return NULL;
    }
    TbSymbolAdmissionLedger_t *ledger = calloc(1, sizeof(*ledger));
    if (!ledger) {
        release_allocation(frontend, sizeof(*ledger));
        return NULL;
    }
    ledger->frontend        = frontend;
    ledger->file_bytes      = file_bytes;
    ledger->aggregate_bytes = aggregate_bytes;
    return ledger;
}


bool tb_symbol_admission_destroy(TbSymbolAdmissionLedger_t *ledger)
{
    if (!ledger)
        return true;
    if (ledger->usage.reservations || ledger->usage.open_contexts ||
        ledger->usage.cached_backings || ledger->usage.cleanup_pending) {
        return false;
    }
    TbSymbolBudget_t *frontend = ledger->frontend;
    free(ledger);
    release_allocation(frontend, sizeof(*ledger));
    return true;
}


TbSymbolAdmissionUsage_t tb_symbol_admission_usage(const TbSymbolAdmissionLedger_t *ledger)
{
    return ledger ? ledger->usage : (TbSymbolAdmissionUsage_t){0};
}


TbSymbolReservation_t *tb_symbol_admission_reserve(TbSymbolAdmissionLedger_t *ledger, uint64_t bytes)
{
    const size_t allocation = sizeof(TbSymbolReservation_t) + sizeof(TbSymbolBackingCharge_t);
    if (!ledger || bytes > ledger->file_bytes || bytes > ledger->aggregate_bytes - ledger->usage.bytes ||
        ledger->usage.reservations == UINT64_MAX || !reserve_allocation(ledger->frontend, allocation)) {
        return NULL;
    }
    TbSymbolReservation_t *reservation = calloc(1, sizeof(*reservation));
    TbSymbolBackingCharge_t *backing = reservation ? calloc(1, sizeof(*backing)) : NULL;
    if (!backing) {
        free(reservation);
        release_allocation(ledger->frontend, allocation);
        return NULL;
    }
    *backing = (TbSymbolBackingCharge_t){
        .owner = ledger,
        .bytes = bytes
    };
    *reservation = (TbSymbolReservation_t){
        .owner   = ledger,
        .backing = backing,
        .bytes   = bytes,
        .state   = TB_SYMBOL_CHARGE_RESERVED
    };
    ledger->usage.bytes += bytes;
    ++ledger->usage.reservations;
    return reservation;
}


TbSymbolReservation_t *tb_symbol_admission_prepare(TbSymbolAdmissionLedger_t *ledger)
{
    TbSymbolReservation_t *reservation = tb_symbol_admission_reserve(ledger, 0);
    if (reservation)
        reservation->needs_size = true;
    return reservation;
}


bool tb_symbol_admission_assign(TbSymbolReservation_t *reservation, uint64_t bytes)
{
    if (!reservation || reservation->state != TB_SYMBOL_CHARGE_RESERVED || !reservation->needs_size)
        return false;
    TbSymbolAdmissionLedger_t *ledger = reservation->owner;
    if (bytes > ledger->file_bytes || bytes > ledger->aggregate_bytes - ledger->usage.bytes)
        return false;
    ledger->usage.bytes += bytes;
    reservation->bytes = bytes;
    reservation->backing->bytes = bytes;
    reservation->needs_size = false;
    return true;
}


TbSymbolChargeState_e tb_symbol_admission_state(const TbSymbolReservation_t *reservation)
{
    return reservation->state;
}


uint64_t tb_symbol_admission_size(const TbSymbolReservation_t *reservation)
{
    return reservation->bytes;
}


TbSymbolBackingCharge_t *tb_symbol_admission_commit(TbSymbolReservation_t *reservation,
                                                   TbSymbolBackingCharge_t *shared)
{
    if (!reservation || reservation->state != TB_SYMBOL_CHARGE_RESERVED || reservation->needs_size)
        return NULL;
    TbSymbolAdmissionLedger_t *ledger = reservation->owner;
    if (ledger->usage.open_contexts == UINT64_MAX ||
        (shared && (shared->owner != ledger || shared->bytes != reservation->bytes ||
                    shared->retiring || shared->contexts == UINT64_MAX))) {
        return NULL;
    }
    if (shared) {
        free(reservation->backing);
        release_allocation(ledger->frontend, sizeof(TbSymbolBackingCharge_t));
        reservation->backing = shared;
        if (!shared->contexts) {
            /* The incoming reservation already carries a complete context
             * charge, so reactivation consumes the old full-size cache charge. */
            ledger->usage.bytes -= shared->bytes;
            --ledger->usage.cached_backings;
        }
    }
    ++reservation->backing->contexts;
    --ledger->usage.reservations;
    ++ledger->usage.open_contexts;
    reservation->state = TB_SYMBOL_CHARGE_OPEN;
    return reservation->backing;
}


bool tb_symbol_admission_abort(TbSymbolReservation_t *reservation)
{
    if (!reservation || reservation->state != TB_SYMBOL_CHARGE_RESERVED)
        return false;
    reservation->state = TB_SYMBOL_CHARGE_CLEANUP;
    --reservation->owner->usage.reservations;
    ++reservation->owner->usage.cleanup_pending;
    return true;
}


bool tb_symbol_admission_cleaned(TbSymbolReservation_t *reservation)
{
    if (!reservation || reservation->state != TB_SYMBOL_CHARGE_CLEANUP)
        return false;
    TbSymbolAdmissionLedger_t *ledger = reservation->owner;
    ledger->usage.bytes -= reservation->bytes;
    --ledger->usage.cleanup_pending;
    free(reservation->backing);
    free(reservation);
    release_allocation(ledger->frontend, sizeof(TbSymbolReservation_t) + sizeof(TbSymbolBackingCharge_t));
    return true;
}


bool tb_symbol_admission_close(TbSymbolReservation_t *reservation)
{
    if (!reservation || reservation->state != TB_SYMBOL_CHARGE_OPEN)
        return false;
    TbSymbolAdmissionLedger_t *ledger = reservation->owner;
    TbSymbolBackingCharge_t *backing = reservation->backing;
    --backing->contexts;
    --ledger->usage.open_contexts;
    if (backing->contexts)
        ledger->usage.bytes -= reservation->bytes;
    else
        ++ledger->usage.cached_backings;
    free(reservation);
    release_allocation(ledger->frontend, sizeof(TbSymbolReservation_t));
    return true;
}


bool tb_symbol_admission_evict(TbSymbolBackingCharge_t *backing)
{
    if (!backing || backing->contexts || backing->retiring)
        return false;
    backing->retiring = true;
    --backing->owner->usage.cached_backings;
    ++backing->owner->usage.cleanup_pending;
    return true;
}


bool tb_symbol_admission_backing_cleaned(TbSymbolBackingCharge_t *backing)
{
    if (!backing || !backing->retiring)
        return false;
    TbSymbolAdmissionLedger_t *ledger = backing->owner;
    ledger->usage.bytes -= backing->bytes;
    --ledger->usage.cleanup_pending;
    free(backing);
    release_allocation(ledger->frontend, sizeof(TbSymbolBackingCharge_t));
    return true;
}
