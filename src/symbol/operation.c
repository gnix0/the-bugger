#include "symbol/operation.h"
#include "symbol/equal.h"
#include "symbol/symbols.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    OPERATION_HEADER,
    OPERATION_LAYOUT,
    OPERATION_STRINGS,
    OPERATION_NAMESPACE_INIT,
    OPERATION_NAMESPACE,
    OPERATION_ACQUIRE_INIT,
    OPERATION_ACQUIRE,
    OPERATION_LEFT,
    OPERATION_RIGHT,
    OPERATION_COMPARE_INIT,
    OPERATION_COMPARE,
    OPERATION_REMOVE_FILE,
    OPERATION_REMOVE_NAMESPACE,
    OPERATION_DISCARD_EMPTY,
    OPERATION_ELF_OPEN,
    OPERATION_ELF_INIT,
    OPERATION_ELF_SECTIONS,
    OPERATION_ELF_SYMBOLS,
    OPERATION_DONE
} OperationPhase_e;

struct tb_symbol_operation {
    TbSymbolBudget_t       *storage;
    TbSymbolBudget_t       *decoder;
    const unsigned char    *arguments;
    size_t                  length;
    uint64_t                words[9];
    uint32_t                string_lengths[2];
    const char             *strings[2];
    size_t                  cursor;
    size_t                  string_offset;
    uint64_t                accumulator;
    unsigned                field_bytes;
    unsigned                field;
    unsigned                word_count;
    unsigned                string_count;
    unsigned                string_index;
    uint32_t                code;
    OperationPhase_e        phase;
    TbSymbolOutcome_e       outcome;
    uint64_t                progress;
    uint64_t                backend_progress;
    TbSymbolStorageKey_t    namespace_key;
    TbSymbolStorageInfo_t   files[2];
    uint64_t                slots[2];
    uint64_t                bytes;
    TbSymbolStorage_t      *namespace;
    TbSymbolAcquire_t      *acquisition;
    TbSymbolEquality_t     *comparison;
    TbSymbolSections_t     *sections;
    TbSymbolSymbols_t      *symbols;
    unsigned char           result[56];
    size_t                  result_length;
    int                     descriptors[2];
    int                     system_error;
    uint32_t                detail;
    uint64_t                record_index;
};

TbSymbolOperation_t *tb_symbol_operation_create(uint32_t operation, const unsigned char *arguments,
                                               size_t length, TbSymbolOperationBudgets_t budgets)
{
    TbSymbolBudget_t *storage = budgets.control;
    if (!tb_symbol_protocol_owned(operation) || (length && !arguments) ||
        (storage && !tb_symbol_budget_reserve(storage, sizeof(TbSymbolOperation_t)))) {
        return NULL;
    }
    TbSymbolOperation_t *state = calloc(1, sizeof(*state));
    if (!state) {
        if (storage)
            (void)tb_symbol_budget_release(storage, sizeof(TbSymbolOperation_t));
        return NULL;
    }
    state->storage        = storage;
    state->decoder        = budgets.decoder;
    state->arguments      = arguments;
    state->length         = length;
    state->code           = operation;
    state->descriptors[0] = -1;
    state->descriptors[1] = -1;
    state->string_count   = operation == TB_SYMBOL_OPERATION_ACQUIRE ? 2 : 1;
    switch (operation) {
    case TB_SYMBOL_OPERATION_ACQUIRE:
        state->word_count = 4;
        break;
    case TB_SYMBOL_OPERATION_REMOVE_FILE:
        state->word_count = 5;
        break;
    case TB_SYMBOL_OPERATION_COMPARE:
        state->word_count = 9;
        break;
    case TB_SYMBOL_OPERATION_REMOVE_STORAGE:
        state->word_count = 2;
        break;
    case TB_SYMBOL_OPERATION_ELF_VALIDATE:
        state->word_count = 7;
        break;
    case TB_SYMBOL_OPERATION_DISCARD_EMPTY:
        state->word_count = 3;
        break;
    default:
        break;
    }
    return state;
}


static void fail(TbSymbolOperation_t *state, TbSymbolOutcome_e outcome)
{
    state->outcome = outcome;
    state->phase = OPERATION_DONE;
}


static void io_failure(TbSymbolOperation_t *state)
{
    state->system_error = errno;
    bool resource = errno == ENOMEM || errno == ENOSPC || errno == EDQUOT || errno == EMFILE ||
                    errno == ENFILE || errno == EFBIG;
    fail(state, resource ? TB_SYMBOL_RESOURCE_FAILURE : TB_SYMBOL_OPERATIONAL_FAILURE);
}


static void parse_header(TbSymbolOperation_t *state, size_t max_bytes)
{
    unsigned width = state->field < state->word_count ? 8 : 4;
    size_t count = width - state->field_bytes;
    if (count > max_bytes)
        count = max_bytes;
    if (count > state->length - state->cursor) {
        fail(state, TB_SYMBOL_MALFORMED);
        return;
    }
    for (size_t i = 0; i < count; ++i)
        state->accumulator = (state->accumulator << 8U) | state->arguments[state->cursor++];
    state->field_bytes += (unsigned)count;
    if (state->field_bytes != width)
        return;
    if (state->field < state->word_count)
        state->words[state->field] = state->accumulator;
    else
        state->string_lengths[state->field - state->word_count] = (uint32_t)state->accumulator;
    ++state->field;
    state->field_bytes = 0;
    state->accumulator = 0;
    if (state->field == state->word_count + state->string_count)
        state->phase = OPERATION_LAYOUT;
}


static void layout(TbSymbolOperation_t *state)
{
    size_t remaining = state->length - state->cursor;
    for (unsigned i = 0; i < state->string_count; ++i) {
        size_t length = state->string_lengths[i];
        if (length < 2 || length > remaining) {
            fail(state, TB_SYMBOL_MALFORMED);
            return;
        }
        state->strings[i] = (const char *)state->arguments + state->cursor;
        state->cursor += length;
        remaining -= length;
    }
    if (remaining) {
        fail(state, TB_SYMBOL_MALFORMED);
        return;
    }
    switch (state->code) {
    case TB_SYMBOL_OPERATION_ACQUIRE:
        state->bytes = state->words[0];
        state->slots[0] = state->words[1];
        state->namespace_key = (TbSymbolStorageKey_t){
            .device = state->words[2],
            .inode  = state->words[3]
        };
        if (!state->bytes || !state->slots[0])
            fail(state, TB_SYMBOL_MALFORMED);
        break;
    case TB_SYMBOL_OPERATION_REMOVE_FILE:
    case TB_SYMBOL_OPERATION_DISCARD_EMPTY:
        state->slots[0] = state->words[0];
        state->namespace_key = (TbSymbolStorageKey_t){
            .device = state->words[1],
            .inode  = state->words[2]
        };
        if (state->code == TB_SYMBOL_OPERATION_REMOVE_FILE) {
            state->files[0].key = (TbSymbolStorageKey_t){
                .device = state->words[3],
                .inode  = state->words[4]
            };
        }
        if (!state->slots[0])
            fail(state, TB_SYMBOL_MALFORMED);
        break;
    case TB_SYMBOL_OPERATION_COMPARE:
        state->bytes = state->words[0];
        state->namespace_key = (TbSymbolStorageKey_t){
            .device = state->words[1],
            .inode  = state->words[2]
        };
        for (unsigned i = 0; i < 2; ++i) {
            unsigned base = 3 + i * 3;
            state->slots[i] = state->words[base];
            state->files[i] = (TbSymbolStorageInfo_t){
                .key = {
                    .device = state->words[base + 1],
                    .inode  = state->words[base + 2]
                },
                .bytes = state->bytes
            };
        }
        if (!state->slots[0] || !state->slots[1] || state->bytes > INT64_MAX)
            fail(state, TB_SYMBOL_MALFORMED);
        break;
    case TB_SYMBOL_OPERATION_ELF_VALIDATE:
        state->bytes = state->words[0];
        state->slots[0] = state->words[1];
        state->namespace_key = (TbSymbolStorageKey_t){
            .device = state->words[2],
            .inode  = state->words[3]
        };
        state->files[0] = (TbSymbolStorageInfo_t){
            .key = {
                .device = state->words[4],
                .inode  = state->words[5]
            },
            .bytes = state->bytes
        };
        if (!state->slots[0] || state->bytes > INT64_MAX)
            fail(state, TB_SYMBOL_MALFORMED);
        break;
    case TB_SYMBOL_OPERATION_REMOVE_STORAGE:
        state->namespace_key = (TbSymbolStorageKey_t){
            .device = state->words[0],
            .inode  = state->words[1]
        };
        break;
    default:
        break;
    }
    if (state->outcome == TB_SYMBOL_PENDING)
        state->phase = OPERATION_STRINGS;
}


static void validate_string(TbSymbolOperation_t *state, size_t max_bytes)
{
    unsigned index = state->string_index;
    size_t left = state->string_lengths[index] - state->string_offset;
    size_t count = left < max_bytes ? left : max_bytes;
    const char *start = state->strings[index] + state->string_offset;
    const char *zero = memchr(start, 0, count);
    if ((zero && (zero != start + count - 1 || count != left)) || (!zero && count == left)) {
        fail(state, TB_SYMBOL_MALFORMED);
        return;
    }
    state->string_offset += count;
    if (state->string_offset == state->string_lengths[index]) {
        state->string_offset = 0;
        if (++state->string_index == state->string_count)
            state->phase = OPERATION_NAMESPACE_INIT;
    }
}


static void namespace_step(TbSymbolOperation_t *state)
{
    TbSymbolOutcome_e result = tb_symbol_storage_step(state->namespace);
    if (result == TB_SYMBOL_PENDING)
        return;
    if (result != TB_SYMBOL_SUCCESS) {
        state->system_error = tb_symbol_storage_error(state->namespace);
        state->detail = TB_SYMBOL_DETAIL_NAMESPACE;
        TbSymbolStorageKey_t key;
        if (state->code == TB_SYMBOL_OPERATION_REMOVE_STORAGE && result == TB_SYMBOL_UNSUPPORTED &&
            tb_symbol_storage_key(state->namespace, &key)) {
            /* A rejected data-storage profile does not prevent removal of an
             * identified empty owned directory. All identity/protection checks
             * must already have passed, and rmdir still refuses any entries. */
            state->system_error = 0;
            state->detail = 0;
            state->phase = OPERATION_REMOVE_NAMESPACE;
        } else if (state->code == TB_SYMBOL_OPERATION_REMOVE_STORAGE && state->system_error == ENOENT) {
            state->system_error = 0;
            fail(state, TB_SYMBOL_SUCCESS);
        } else {
            fail(state, result);
        }
        return;
    }
    switch (state->code) {
    case TB_SYMBOL_OPERATION_CREATE_STORAGE: {
        TbSymbolStorageKey_t key;
        if (!tb_symbol_storage_key(state->namespace, &key)) {
            fail(state, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        tb_symbol_protocol_put(state->result, key.device, 8);
        tb_symbol_protocol_put(state->result + 8, key.inode, 8);
        state->result_length = 16;
        fail(state, TB_SYMBOL_SUCCESS);
        break;
    }
    case TB_SYMBOL_OPERATION_ACQUIRE:
        state->phase = OPERATION_ACQUIRE_INIT;
        break;
    case TB_SYMBOL_OPERATION_ELF_VALIDATE:
        state->phase = OPERATION_ELF_OPEN;
        break;
    case TB_SYMBOL_OPERATION_COMPARE:
        state->phase = OPERATION_LEFT;
        break;
    case TB_SYMBOL_OPERATION_REMOVE_FILE:
        state->phase = OPERATION_REMOVE_FILE;
        break;
    case TB_SYMBOL_OPERATION_REMOVE_STORAGE:
        state->phase = OPERATION_REMOVE_NAMESPACE;
        break;
    default:
        state->phase = OPERATION_DISCARD_EMPTY;
        break;
    }
}


static void elf_complete(TbSymbolOperation_t *state)
{
    TbSymbolSectionsView_t directory;
    TbSymbolSymbolsView_t symbols = {0};
    if (!tb_symbol_sections_view(state->sections, &directory) ||
        (state->symbols && !tb_symbol_symbols_view(state->symbols, &symbols))) {
        fail(state, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    tb_symbol_protocol_put(state->result, directory.count, 8);
    tb_symbol_protocol_put(state->result + 8, state->words[6], 8);
    tb_symbol_protocol_put(state->result + 16, symbols.count, 8);
    tb_symbol_protocol_put(state->result + 24, directory.header->object_type, 4);
    tb_symbol_protocol_put(state->result + 28, directory.header->machine, 4);
    tb_symbol_protocol_put(state->result + 32, directory.header->address_bytes, 4);
    tb_symbol_protocol_put(state->result + 36,
                           directory.header->byte_order == TB_SYMBOL_LITTLE_ENDIAN ? 1 : 2, 4);
    tb_symbol_protocol_put(state->result + 40,
                           directory.names == TB_SYMBOL_SECTION_NAMES_AVAILABLE ? 1 :
                           directory.names == TB_SYMBOL_SECTION_NAMES_COMPRESSED ? 2 : 0, 4);
    tb_symbol_protocol_put(state->result + 44,
                           state->symbols ? directory.entries[state->words[6]].header.type : 0, 4);
    tb_symbol_protocol_put(state->result + 48, state->bytes, 8);
    state->result_length = sizeof(state->result);
    fail(state, TB_SYMBOL_SUCCESS);
}


static void elf_sections(TbSymbolOperation_t *state, size_t max_bytes)
{
    TbSymbolOutcome_e outcome = tb_symbol_sections_step(state->sections, max_bytes);
    if (outcome == TB_SYMBOL_PENDING)
        return;
    if (outcome != TB_SYMBOL_SUCCESS) {
        TbSymbolSectionsFailure_t failure = tb_symbol_sections_failure(state->sections);
        state->detail = TB_SYMBOL_DETAIL_ELF_SECTIONS | (uint32_t)failure.reason;
        state->record_index = failure.section_index;
        state->system_error = failure.system_error ? failure.system_error : failure.read.system_error;
        fail(state, outcome);
        return;
    }
    if (state->words[6] == UINT64_MAX) {
        elf_complete(state);
        return;
    }
    TbSymbolSectionsView_t directory;
    if (!tb_symbol_sections_view(state->sections, &directory)) {
        fail(state, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else if (state->words[6] >= directory.count) {
        state->detail = TB_SYMBOL_DETAIL_ELF_SELECTION;
        state->record_index = state->words[6];
        fail(state, TB_SYMBOL_MALFORMED);
    } else {
        state->symbols = tb_symbol_symbols_create(directory, (TbSymbolTableSelection_t){
            .section_index = state->words[6],
            .descriptor    = state->descriptors[0]
        }, state->decoder);
        if (!state->symbols)
            fail(state, TB_SYMBOL_RESOURCE_FAILURE);
        else
            state->phase = OPERATION_ELF_SYMBOLS;
    }
}


static void elf_symbols(TbSymbolOperation_t *state, size_t max_bytes)
{
    TbSymbolOutcome_e outcome = tb_symbol_symbols_step(state->symbols, max_bytes);
    if (outcome == TB_SYMBOL_SUCCESS) {
        elf_complete(state);
    } else if (outcome != TB_SYMBOL_PENDING) {
        TbSymbolSymbolsFailure_t failure = tb_symbol_symbols_failure(state->symbols);
        state->detail = TB_SYMBOL_DETAIL_ELF_SYMBOLS | (uint32_t)failure.reason;
        state->record_index = failure.record_index;
        state->system_error = failure.system_error ? failure.system_error : failure.read.system_error;
        fail(state, outcome);
    }
}


static void acquire_step(TbSymbolOperation_t *state, size_t max_bytes)
{
    TbSymbolOutcome_e result = tb_symbol_acquire_step(state->acquisition, max_bytes);
    if (result == TB_SYMBOL_PENDING)
        return;
    if (result != TB_SYMBOL_SUCCESS) {
        state->detail = (uint32_t)tb_symbol_acquire_failure(state->acquisition, &state->system_error);
        fail(state, result);
        return;
    }
    TbSymbolAcquiredBytes_t bytes;
    if (!tb_symbol_acquire_take(state->acquisition, &bytes)) {
        fail(state, TB_SYMBOL_OPERATIONAL_FAILURE);
        return;
    }
    state->descriptors[0] = bytes.descriptor;
    tb_symbol_protocol_put(state->result, bytes.bytes, 8);
    tb_symbol_protocol_put(state->result + 8, bytes.key.device, 8);
    tb_symbol_protocol_put(state->result + 16, bytes.key.inode, 8);
    memcpy(state->result + 24, bytes.digest, sizeof(bytes.digest));
    state->result_length = sizeof(state->result);
    fail(state, TB_SYMBOL_SUCCESS);
}


TbSymbolWorkResult_t tb_symbol_operation_step(TbSymbolOperation_t *state, size_t max_bytes)
{
    if (!state) {
        return (TbSymbolWorkResult_t){
            .outcome = TB_SYMBOL_OPERATIONAL_FAILURE
        };
    }
    if (state->outcome != TB_SYMBOL_PENDING) {
        return (TbSymbolWorkResult_t){
            .outcome  = state->outcome,
            .progress = state->progress
        };
    }
    if (!max_bytes || max_bytes > TB_SYMBOL_WIRE_IO_BYTES) {
        state->system_error = EINVAL;
        fail(state, TB_SYMBOL_OPERATIONAL_FAILURE);
    }
    OperationPhase_e before = state->phase;
    size_t cursor = state->cursor;
    size_t offset = state->string_offset;
    unsigned field = state->field;
    unsigned string_index = state->string_index;
    switch (state->phase) {
    case OPERATION_HEADER:
        parse_header(state, max_bytes);
        break;
    case OPERATION_LAYOUT:
        layout(state);
        break;
    case OPERATION_STRINGS:
        validate_string(state, max_bytes);
        break;
    case OPERATION_NAMESPACE_INIT: {
        if (state->code == TB_SYMBOL_OPERATION_ELF_VALIDATE && !state->decoder) {
            state->detail = TB_SYMBOL_DETAIL_ELF_DISABLED;
            fail(state, TB_SYMBOL_UNSUPPORTED);
            break;
        }
        bool creating = state->code == TB_SYMBOL_OPERATION_CREATE_STORAGE;
        unsigned index = state->code == TB_SYMBOL_OPERATION_ACQUIRE ? 1 : 0;
        state->namespace = tb_symbol_storage_create(state->strings[index],
                                                    creating ? NULL : &state->namespace_key, state->storage);
        if (!state->namespace)
            fail(state, TB_SYMBOL_RESOURCE_FAILURE);
        else
            state->phase = OPERATION_NAMESPACE;
        break;
    }
    case OPERATION_NAMESPACE:
        namespace_step(state);
        break;
    case OPERATION_ACQUIRE_INIT:
        state->acquisition = tb_symbol_acquire_create(state->strings[0], state->namespace,
                                                      state->slots[0], state->bytes, state->storage);
        if (!state->acquisition)
            fail(state, TB_SYMBOL_RESOURCE_FAILURE);
        else {
            state->backend_progress = 0;
            state->phase = OPERATION_ACQUIRE;
        }
        break;
    case OPERATION_ACQUIRE:
        acquire_step(state, max_bytes);
        break;
    case OPERATION_LEFT:
    case OPERATION_RIGHT: {
        unsigned input = state->phase == OPERATION_LEFT ? 0 : 1;
        state->descriptors[input] = tb_symbol_storage_reader(state->namespace, state->slots[input],
                                                              &state->files[input]);
        if (state->descriptors[input] < 0) {
            state->detail = TB_SYMBOL_DETAIL_COMPARE | (UINT32_C(1) << input);
            io_failure(state);
        } else
            state->phase = input ? OPERATION_COMPARE_INIT : OPERATION_RIGHT;
        break;
    }
    case OPERATION_COMPARE_INIT:
        state->comparison = tb_symbol_equal_create(state->descriptors[0], state->descriptors[1],
                                                    state->bytes, state->storage);
        if (!state->comparison)
            fail(state, TB_SYMBOL_RESOURCE_FAILURE);
        else {
            state->backend_progress = 0;
            state->phase = OPERATION_COMPARE;
        }
        break;
    case OPERATION_COMPARE: {
        TbSymbolOutcome_e result = tb_symbol_equal_step(state->comparison, max_bytes);
        if (result == TB_SYMBOL_SUCCESS) {
            bool equal;
            if (!tb_symbol_equal_result(state->comparison, &equal)) {
                fail(state, TB_SYMBOL_OPERATIONAL_FAILURE);
                break;
            }
            state->result[0] = equal ? 1 : 0;
            state->result_length = 1;
            fail(state, TB_SYMBOL_SUCCESS);
        } else if (result != TB_SYMBOL_PENDING) {
            TbSymbolEqualityFailure_t failure = tb_symbol_equal_failure(state->comparison);
            state->detail = TB_SYMBOL_DETAIL_COMPARE | failure.input_mask;
            state->system_error = failure.system_error;
            fail(state, result);
        }
        break;
    }
    case OPERATION_ELF_OPEN:
        state->descriptors[0] = tb_symbol_storage_reader(state->namespace, state->slots[0], &state->files[0]);
        if (state->descriptors[0] < 0) {
            state->detail = TB_SYMBOL_DETAIL_ELF_OPEN;
            io_failure(state);
        } else {
            state->phase = OPERATION_ELF_INIT;
        }
        break;
    case OPERATION_ELF_INIT:
        state->sections = tb_symbol_sections_create(state->files[0], state->descriptors[0], state->decoder);
        if (!state->sections)
            fail(state, TB_SYMBOL_RESOURCE_FAILURE);
        else
            state->phase = OPERATION_ELF_SECTIONS;
        break;
    case OPERATION_ELF_SECTIONS:
        elf_sections(state, max_bytes);
        break;
    case OPERATION_ELF_SYMBOLS:
        elf_symbols(state, max_bytes);
        break;
    case OPERATION_REMOVE_FILE:
        if (!tb_symbol_storage_unlink(state->namespace, state->slots[0], &state->files[0].key))
            io_failure(state);
        else
            fail(state, TB_SYMBOL_SUCCESS);
        break;
    case OPERATION_REMOVE_NAMESPACE:
        if (!tb_symbol_storage_remove(state->namespace))
            io_failure(state);
        else
            fail(state, TB_SYMBOL_SUCCESS);
        break;
    case OPERATION_DISCARD_EMPTY:
        if (!tb_symbol_storage_discard_empty(state->namespace, state->slots[0]))
            io_failure(state);
        else
            fail(state, TB_SYMBOL_SUCCESS);
        break;
    case OPERATION_DONE:
        break;
    }
    uint64_t backend = state->symbols ? tb_symbol_symbols_progress(state->symbols) :
                       state->sections ? tb_symbol_sections_progress(state->sections) :
                       state->acquisition ? tb_symbol_acquire_progress(state->acquisition) :
                       state->comparison ? tb_symbol_equal_progress(state->comparison) :
                       tb_symbol_storage_progress(state->namespace);
    if (before != state->phase || cursor != state->cursor || offset != state->string_offset ||
        field != state->field || string_index != state->string_index || backend != state->backend_progress) {
        if (state->progress == UINT64_MAX) {
            state->system_error = EOVERFLOW;
            fail(state, TB_SYMBOL_OPERATIONAL_FAILURE);
        } else {
            ++state->progress;
        }
    }
    state->backend_progress = backend;
    return (TbSymbolWorkResult_t){
        .outcome  = state->outcome,
        .progress = state->progress
    };
}


bool tb_symbol_operation_offer(const TbSymbolOperation_t *state, TbSymbolAcquireOffer_t *offer)
{
    if (!state || !offer || !state->acquisition)
        return false;
    TbSymbolAcquireOffer_t candidate;
    if (!tb_symbol_acquire_offer(state->acquisition, &candidate.bytes) ||
        !tb_symbol_acquire_storage_key(state->acquisition, &candidate.key)) {
        return false;
    }
    *offer = candidate;
    return true;
}


bool tb_symbol_operation_grant(TbSymbolOperation_t *state, uint64_t bytes)
{
    return state && state->phase == OPERATION_ACQUIRE &&
           tb_symbol_acquire_grant(state->acquisition, bytes);
}


TbSymbolOperationResult_t tb_symbol_operation_result(const TbSymbolOperation_t *state)
{
    TbSymbolOperationResult_t result = {
        .descriptor = -1
    };
    if (!state)
        return result;
    result.system_error = state->system_error;
    result.detail       = state->detail;
    result.record_index = state->record_index;
    if (state->code == TB_SYMBOL_OPERATION_CREATE_STORAGE && state->namespace) {
        if (tb_symbol_storage_created(state->namespace))
            result.detail |= TB_SYMBOL_DETAIL_CREATED;
        if (tb_symbol_storage_key(state->namespace, &result.key))
            result.detail |= TB_SYMBOL_DETAIL_HAS_KEY;
    } else if (state->acquisition && tb_symbol_acquire_storage_key(state->acquisition, &result.key)) {
        result.detail |= TB_SYMBOL_DETAIL_HAS_KEY | TB_SYMBOL_DETAIL_CREATED;
    }
    if (state->outcome == TB_SYMBOL_SUCCESS) {
        result.payload = state->result;
        result.length = state->result_length;
        if (state->code == TB_SYMBOL_OPERATION_ACQUIRE)
            result.descriptor = state->descriptors[0];
    }
    return result;
}


void tb_symbol_operation_destroy(TbSymbolOperation_t *state)
{
    if (!state)
        return;
    tb_symbol_symbols_destroy(state->symbols);
    tb_symbol_sections_destroy(state->sections);
    tb_symbol_equal_destroy(state->comparison);
    tb_symbol_acquire_destroy(state->acquisition);
    for (unsigned i = 0; i < 2; ++i) {
        if (state->descriptors[i] >= 0)
            (void)close(state->descriptors[i]);
    }
    tb_symbol_storage_destroy(state->namespace);
    TbSymbolBudget_t *storage = state->storage;
    free(state);
    if (storage)
        (void)tb_symbol_budget_release(storage, sizeof(TbSymbolOperation_t));
}
