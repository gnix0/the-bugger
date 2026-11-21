#include "symbol/admission.h"
#include "../support/check.h"

#include <stdlib.h>

static unsigned fail_after;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
    if (fail_after && !--fail_after)
        return NULL;
    return __real_calloc(count, size);
}


static void usage_is(TbSymbolAdmissionLedger_t *ledger, uint64_t bytes, uint64_t reserved,
                     uint64_t opened, uint64_t cached, uint64_t cleanup)
{
    TbSymbolAdmissionUsage_t usage = tb_symbol_admission_usage(ledger);
    CHECK(usage.bytes == bytes);
    CHECK(usage.reservations == reserved);
    CHECK(usage.open_contexts == opened);
    CHECK(usage.cached_backings == cached);
    CHECK(usage.cleanup_pending == cleanup);
}


static void delayed_cleanup(void)
{
    TbSymbolBudget_t root;
    CHECK(tb_symbol_budget_init(&root, 4096));
    TbSymbolAdmissionLedger_t *ledger = tb_symbol_admission_create(100, 150, &root);
    CHECK(ledger);
    size_t empty = root.used;
    TbSymbolReservation_t *first = tb_symbol_admission_reserve(ledger, 100);
    CHECK(first);
    CHECK(tb_symbol_admission_size(first) == 100);
    CHECK(tb_symbol_admission_state(first) == TB_SYMBOL_CHARGE_RESERVED);
    CHECK(!tb_symbol_admission_close(first));
    CHECK(!tb_symbol_admission_cleaned(first));
    CHECK(!tb_symbol_admission_reserve(ledger, 101));
    CHECK(!tb_symbol_admission_reserve(ledger, 51));
    TbSymbolReservation_t *second = tb_symbol_admission_reserve(ledger, 50);
    CHECK(second);
    usage_is(ledger, 150, 2, 0, 0, 0);
    CHECK(!tb_symbol_admission_destroy(ledger));
    CHECK(tb_symbol_admission_abort(first));
    CHECK(!tb_symbol_admission_abort(first));
    CHECK(!tb_symbol_admission_commit(first, NULL));
    CHECK(!tb_symbol_admission_reserve(ledger, 1));
    usage_is(ledger, 150, 1, 0, 0, 1);
    CHECK(tb_symbol_admission_cleaned(first));
    usage_is(ledger, 50, 1, 0, 0, 0);
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(second, NULL);
    CHECK(backing);
    CHECK(tb_symbol_admission_state(second) == TB_SYMBOL_CHARGE_OPEN);
    CHECK(!tb_symbol_admission_commit(second, NULL));
    CHECK(!tb_symbol_admission_abort(second));
    CHECK(!tb_symbol_admission_evict(backing));
    CHECK(!tb_symbol_admission_backing_cleaned(backing));
    CHECK(tb_symbol_admission_close(second));
    usage_is(ledger, 50, 0, 0, 1, 0);
    CHECK(tb_symbol_admission_evict(backing));
    CHECK(!tb_symbol_admission_evict(backing));
    CHECK(!tb_symbol_admission_destroy(ledger));
    usage_is(ledger, 50, 0, 0, 0, 1);
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    usage_is(ledger, 0, 0, 0, 0, 0);
    CHECK(root.used == empty);
    CHECK(tb_symbol_admission_destroy(ledger));
    CHECK(!root.used);
}


static void shared_contexts(void)
{
    TbSymbolAdmissionLedger_t *ledger = tb_symbol_admission_create(100, 300, NULL);
    TbSymbolAdmissionLedger_t *other = tb_symbol_admission_create(100, 300, NULL);
    CHECK(ledger && other);
    TbSymbolReservation_t *primary = tb_symbol_admission_reserve(ledger, 100);
    TbSymbolReservation_t *companion = tb_symbol_admission_reserve(ledger, 100);
    TbSymbolReservation_t *foreign = tb_symbol_admission_reserve(other, 100);
    TbSymbolReservation_t *different = tb_symbol_admission_reserve(ledger, 99);
    CHECK(primary && companion && foreign && different);
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(primary, NULL);
    CHECK(backing);
    CHECK(!tb_symbol_admission_commit(foreign, backing));
    CHECK(!tb_symbol_admission_commit(different, backing));
    CHECK(tb_symbol_admission_abort(different));
    CHECK(tb_symbol_admission_cleaned(different));
    CHECK(tb_symbol_admission_commit(companion, backing) == backing);
    usage_is(ledger, 200, 0, 2, 0, 0);
    CHECK(!tb_symbol_admission_reserve(ledger, 101));
    TbSymbolReservation_t *third = tb_symbol_admission_reserve(ledger, 100);
    CHECK(third);
    CHECK(tb_symbol_admission_commit(third, backing) == backing);
    CHECK(!tb_symbol_admission_reserve(ledger, 1));
    CHECK(!tb_symbol_admission_evict(backing));
    CHECK(tb_symbol_admission_close(companion));
    usage_is(ledger, 200, 0, 2, 0, 0);
    CHECK(tb_symbol_admission_close(primary));
    CHECK(tb_symbol_admission_close(third));
    usage_is(ledger, 100, 0, 0, 1, 0);

    /* Cache reactivation still needs its independent full reservation. */
    third = tb_symbol_admission_reserve(ledger, 100);
    CHECK(third);
    usage_is(ledger, 200, 1, 0, 1, 0);
    CHECK(tb_symbol_admission_commit(third, backing) == backing);
    usage_is(ledger, 100, 0, 1, 0, 0);
    CHECK(tb_symbol_admission_close(third));
    CHECK(tb_symbol_admission_evict(backing));
    third = tb_symbol_admission_reserve(ledger, 100);
    CHECK(third && !tb_symbol_admission_commit(third, backing));
    CHECK(tb_symbol_admission_abort(third));
    CHECK(tb_symbol_admission_cleaned(third));
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    CHECK(tb_symbol_admission_destroy(ledger));
    CHECK(tb_symbol_admission_abort(foreign));
    CHECK(tb_symbol_admission_cleaned(foreign));
    CHECK(tb_symbol_admission_destroy(other));
}


static void allocation_rollback(void)
{
    TbSymbolBudget_t root;
    TbSymbolBudget_t child;
    CHECK(tb_symbol_budget_init(&root, 4096));
    CHECK(tb_symbol_budget_init_child(&child, 4096, &root));
    fail_after = 1;
    CHECK(!tb_symbol_admission_create(100, 100, &child));
    CHECK(!root.used && !child.used);
    TbSymbolAdmissionLedger_t *ledger = tb_symbol_admission_create(100, 100, &child);
    CHECK(ledger);
    size_t empty = root.used;
    for (unsigned failure = 1; failure <= 2; ++failure) {
        fail_after = failure;
        CHECK(!tb_symbol_admission_reserve(ledger, 100));
        CHECK(root.used == empty && child.used == empty);
        usage_is(ledger, 0, 0, 0, 0, 0);
    }
    root.limit = empty;
    CHECK(!tb_symbol_admission_reserve(ledger, 0));
    CHECK(child.used == empty);
    root.limit = 4096;
    TbSymbolReservation_t *reservation = tb_symbol_admission_reserve(ledger, 100);
    CHECK(reservation);
    /* Publication, cancellation and cleanup require no further allocations. */
    fail_after = 1;
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(reservation, NULL);
    CHECK(backing && fail_after == 1);
    CHECK(tb_symbol_admission_close(reservation));
    CHECK(tb_symbol_admission_evict(backing));
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    CHECK(fail_after == 1);
    fail_after = 0;
    CHECK(tb_symbol_admission_destroy(ledger));
    CHECK(!root.used && !child.used);
}


static void boundaries(void)
{
    CHECK(!tb_symbol_admission_create(0, 1, NULL));
    CHECK(!tb_symbol_admission_create(1, 0, NULL));
    CHECK(!tb_symbol_admission_create(2, 1, NULL));
    CHECK(tb_symbol_admission_destroy(NULL));
    CHECK(!tb_symbol_admission_reserve(NULL, 0));
    CHECK(!tb_symbol_admission_commit(NULL, NULL));
    CHECK(!tb_symbol_admission_abort(NULL));
    CHECK(!tb_symbol_admission_cleaned(NULL));
    CHECK(!tb_symbol_admission_close(NULL));
    CHECK(!tb_symbol_admission_evict(NULL));
    CHECK(!tb_symbol_admission_backing_cleaned(NULL));
    TbSymbolAdmissionLedger_t *ledger = tb_symbol_admission_create(UINT64_MAX, UINT64_MAX, NULL);
    CHECK(ledger);
    TbSymbolReservation_t *full = tb_symbol_admission_reserve(ledger, UINT64_MAX);
    TbSymbolReservation_t *zero = tb_symbol_admission_reserve(ledger, 0);
    CHECK(full && zero);
    CHECK(!tb_symbol_admission_reserve(ledger, 1));
    usage_is(ledger, UINT64_MAX, 2, 0, 0, 0);
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(full, NULL);
    CHECK(backing);
    CHECK(tb_symbol_admission_close(full));
    CHECK(tb_symbol_admission_evict(backing));
    CHECK(!tb_symbol_admission_reserve(ledger, 1));
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    CHECK(tb_symbol_admission_abort(zero));
    CHECK(tb_symbol_admission_cleaned(zero));
    CHECK(tb_symbol_admission_destroy(ledger));

    ledger = tb_symbol_admission_create(TB_SYMBOL_FILE_BYTES_DEFAULT,
                                         TB_SYMBOL_ADMISSION_BYTES_DEFAULT, NULL);
    CHECK(ledger);
    TbSymbolReservation_t *files[4];
    for (size_t i = 0; i < 4; ++i) {
        files[i] = tb_symbol_admission_reserve(ledger, TB_SYMBOL_FILE_BYTES_DEFAULT);
        CHECK(files[i]);
    }
    CHECK(!tb_symbol_admission_reserve(ledger, 1));
    for (size_t i = 0; i < 4; ++i) {
        CHECK(tb_symbol_admission_abort(files[i]));
        CHECK(tb_symbol_admission_cleaned(files[i]));
    }
    CHECK(tb_symbol_admission_destroy(ledger));
}


static void zero_length_ownership(void)
{
    TbSymbolBudget_t root;
    CHECK(tb_symbol_budget_init(&root, 1024));
    TbSymbolAdmissionLedger_t *ledger = tb_symbol_admission_create(1, 1, &root);
    CHECK(ledger);
    TbSymbolReservation_t *tickets[64];
    size_t count = 0;
    while (count < sizeof(tickets) / sizeof(tickets[0])) {
        TbSymbolReservation_t *ticket = tb_symbol_admission_reserve(ledger, 0);
        if (!ticket)
            break;
        tickets[count++] = ticket;
    }
    CHECK(count && count < sizeof(tickets) / sizeof(tickets[0]));
    usage_is(ledger, 0, count, 0, 0, 0);
    CHECK(!tb_symbol_admission_destroy(ledger));
    for (size_t i = 0; i < count; ++i) {
        CHECK(tb_symbol_admission_abort(tickets[i]));
        CHECK(tb_symbol_admission_cleaned(tickets[i]));
    }
    CHECK(tb_symbol_admission_destroy(ledger));
    CHECK(!root.used);
}


static void discovered_size(void)
{
    TbSymbolBudget_t root;
    CHECK(tb_symbol_budget_init(&root, 4096));
    TbSymbolAdmissionLedger_t *ledger = tb_symbol_admission_create(100, 150, &root);
    CHECK(ledger);
    TbSymbolReservation_t *known = tb_symbol_admission_reserve(ledger, 100);
    TbSymbolReservation_t *pending = tb_symbol_admission_prepare(ledger);
    CHECK(known && pending && !tb_symbol_admission_size(pending));
    usage_is(ledger, 100, 2, 0, 0, 0);
    CHECK(!tb_symbol_admission_commit(pending, NULL));
    CHECK(!tb_symbol_admission_assign(pending, 101));
    CHECK(!tb_symbol_admission_assign(pending, 51));
    usage_is(ledger, 100, 2, 0, 0, 0);
    CHECK(!tb_symbol_admission_assign(known, 100));
    fail_after = 1;
    CHECK(tb_symbol_admission_assign(pending, 50) && fail_after == 1);
    CHECK(!tb_symbol_admission_assign(pending, 0));
    usage_is(ledger, 150, 2, 0, 0, 0);
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(pending, NULL);
    CHECK(backing && fail_after == 1);
    CHECK(!tb_symbol_admission_assign(pending, 50));
    CHECK(tb_symbol_admission_close(pending));
    CHECK(tb_symbol_admission_evict(backing));
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    CHECK(tb_symbol_admission_abort(known));
    CHECK(tb_symbol_admission_cleaned(known));
    fail_after = 0;

    pending = tb_symbol_admission_prepare(ledger);
    CHECK(pending && !tb_symbol_admission_commit(pending, NULL));
    CHECK(tb_symbol_admission_assign(pending, 0));
    backing = tb_symbol_admission_commit(pending, NULL);
    CHECK(backing);
    CHECK(tb_symbol_admission_close(pending));
    CHECK(tb_symbol_admission_evict(backing));
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    pending = tb_symbol_admission_prepare(ledger);
    CHECK(pending && tb_symbol_admission_abort(pending));
    CHECK(!tb_symbol_admission_assign(pending, 1));
    CHECK(tb_symbol_admission_cleaned(pending));
    CHECK(tb_symbol_admission_destroy(ledger) && !root.used);
}


int main(void)
{
    delayed_cleanup();
    shared_contexts();
    allocation_rollback();
    boundaries();
    zero_length_ownership();
    discovered_size();
    return 0;
}
