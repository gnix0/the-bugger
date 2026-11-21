#include "symbol/string_order.h"

#include <elf.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* A comparison inspects both operands within one 64 KiB checkpoint. */
enum {
    ORDER_INSPECTION_BYTES = 65536,
    ORDER_OPERAND_BYTES   = ORDER_INSPECTION_BYTES / 2
};

typedef enum {
    ORDER_PROFILE_LEFT,
    ORDER_PROFILE_RIGHT,
    ORDER_REQUEST_LEFT,
    ORDER_READ_LEFT,
    ORDER_REQUEST_RIGHT,
    ORDER_READ_RIGHT,
    ORDER_COMPARE,
    ORDER_DONE
} OrderPhase_e;

typedef struct {
    unsigned input;
    size_t   max_bytes;
} OrderSlice_t;

struct tb_symbol_string_order {
    TbSymbolBudget_t            *construction;
    TbSymbolStringOrderInput_t   inputs[2];
    TbSymbolFile_t              *files[2];
    const unsigned char         *windows[2];
    uint64_t                     starts[2];
    uint64_t                     total;
    uint64_t                     cursor;
    uint64_t                     progress;
    size_t                       chunk;
    size_t                       compared;
    OrderPhase_e                 phase;
    TbSymbolOutcome_e            outcome;
    TbSymbolStringOrder_e        order;
    TbSymbolStringOrderFailure_t failure;
};

TbSymbolStringOrder_t *tb_symbol_string_order_create(const TbSymbolStringOrderInput_t inputs[2],
                                                     TbSymbolBudget_t *construction)
{
    if (!inputs || !construction)
        return NULL;
    for (unsigned i = 0; i < 2; ++i) {
        TbSymbolSectionsView_t directory = inputs[i].directory;
        if (!directory.header || !directory.layout || !directory.entries || inputs[i].descriptor < 0 ||
            directory.count != directory.layout->sections.count ||
            directory.backing.bytes != directory.header->file_bytes) {
            return NULL;
        }
    }
    if (!tb_symbol_budget_reserve(construction, sizeof(TbSymbolStringOrder_t)))
        return NULL;
    TbSymbolStringOrder_t *comparison = calloc(1, sizeof(*comparison));
    if (!comparison) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolStringOrder_t));
        return NULL;
    }
    comparison->construction = construction;
    for (unsigned i = 0; i < 2; ++i) {
        comparison->inputs[i] = inputs[i];
        comparison->files[i] = tb_symbol_file_create(inputs[i].directory.backing, inputs[i].descriptor,
                                                      construction);
        if (!comparison->files[i]) {
            tb_symbol_string_order_destroy(comparison);
            return NULL;
        }
    }
    return comparison;
}


static void fail(TbSymbolStringOrder_t *comparison, TbSymbolOutcome_e outcome,
                  TbSymbolStringOrderFailure_t failure)
{
    comparison->failure = failure;
    comparison->outcome = outcome;
    comparison->phase   = ORDER_DONE;
}


static void profile(TbSymbolStringOrder_t *comparison, unsigned index)
{
    TbSymbolSectionsView_t directory = comparison->inputs[index].directory;
    TbSymbolStringView_t string = comparison->inputs[index].string;
    if (directory.backing.key.device != string.backing.key.device ||
        directory.backing.key.inode != string.backing.key.inode ||
        directory.backing.bytes != string.backing.bytes || string.section_index >= directory.count) {
        fail(comparison, TB_SYMBOL_MALFORMED, (TbSymbolStringOrderFailure_t){
            .reason = TB_SYMBOL_STRING_ORDER_RANGE,
            .input  = index
        });
        return;
    }
    TbSymbolElfSection_t section = directory.entries[string.section_index].header;
    if ((section.type != SHT_PROGBITS && section.type != SHT_STRTAB) || (section.flags & SHF_COMPRESSED)) {
        fail(comparison, TB_SYMBOL_UNSUPPORTED, (TbSymbolStringOrderFailure_t){
            .reason = TB_SYMBOL_STRING_ORDER_SECTION,
            .input  = index
        });
        return;
    }
    uint64_t end;
    uint64_t section_end;
    if (!tb_symbol_add(section.offset, section.bytes, &section_end) || section_end > string.backing.bytes ||
        !tb_symbol_add(string.offset, string.bytes, &end) || end >= string.end_offset ||
        string.end_offset > section.bytes ||
        !tb_symbol_add(section.offset, string.offset, &comparison->starts[index])) {
        fail(comparison, TB_SYMBOL_MALFORMED, (TbSymbolStringOrderFailure_t){
            .reason = TB_SYMBOL_STRING_ORDER_RANGE,
            .input  = index
        });
        return;
    }
    if (index == 0) {
        comparison->phase = ORDER_PROFILE_RIGHT;
    } else {
        uint64_t minimum = comparison->inputs[0].string.bytes < string.bytes
                               ? comparison->inputs[0].string.bytes : string.bytes;
        if (!tb_symbol_add(minimum, 1, &comparison->total)) {
            fail(comparison, TB_SYMBOL_MALFORMED, (TbSymbolStringOrderFailure_t){
                .reason = TB_SYMBOL_STRING_ORDER_RANGE,
                .input  = index
            });
            return;
        }
        comparison->phase = ORDER_REQUEST_LEFT;
    }
}


static void request(TbSymbolStringOrder_t *comparison, OrderSlice_t slice)
{
    unsigned index = slice.input;
    size_t max_bytes = slice.max_bytes;
    if (index == 0) {
        if (comparison->cursor == comparison->total) {
            if (comparison->inputs[0].string.bytes != comparison->inputs[1].string.bytes) {
                fail(comparison, TB_SYMBOL_MALFORMED, (TbSymbolStringOrderFailure_t){
                    .reason = TB_SYMBOL_STRING_ORDER_SPAN,
                    .input  = 0
                });
            } else {
                comparison->outcome = TB_SYMBOL_SUCCESS;
                comparison->phase   = ORDER_DONE;
            }
            return;
        }
        uint64_t remaining = comparison->total - comparison->cursor;
        comparison->chunk = remaining < max_bytes ? (size_t)remaining : max_bytes;
        comparison->compared = 0;
    }
    uint64_t offset;
    if (!tb_symbol_add(comparison->starts[index], comparison->cursor, &offset) ||
        !tb_symbol_file_request(comparison->files[index], (TbSymbolFileRange_t){
            .offset = offset,
            .bytes  = comparison->chunk
        })) {
        fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, (TbSymbolStringOrderFailure_t){
            .reason = TB_SYMBOL_STRING_ORDER_REQUEST,
            .input  = index
        });
        return;
    }
    comparison->phase = index == 0 ? ORDER_READ_LEFT : ORDER_READ_RIGHT;
}


static void read_window(TbSymbolStringOrder_t *comparison, OrderSlice_t slice)
{
    unsigned index = slice.input;
    size_t max_bytes = slice.max_bytes;
    TbSymbolOutcome_e outcome = tb_symbol_file_step(comparison->files[index], max_bytes);
    if (outcome == TB_SYMBOL_SUCCESS) {
        TbSymbolFileView_t window;
        uint64_t offset;
        if (!tb_symbol_add(comparison->starts[index], comparison->cursor, &offset) ||
            !tb_symbol_file_view(comparison->files[index], &window) || window.offset != offset ||
            window.bytes != comparison->chunk) {
            fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, (TbSymbolStringOrderFailure_t){
                .reason = TB_SYMBOL_STRING_ORDER_REQUEST,
                .input  = index
            });
            return;
        }
        comparison->windows[index] = window.data;
        comparison->phase = index == 0 ? ORDER_REQUEST_RIGHT : ORDER_COMPARE;
    } else if (outcome != TB_SYMBOL_PENDING) {
        fail(comparison, outcome, (TbSymbolStringOrderFailure_t){
            .reason = TB_SYMBOL_STRING_ORDER_READ,
            .input  = index,
            .read   = tb_symbol_file_failure(comparison->files[index])
        });
    }
}


static void compare_window(TbSymbolStringOrder_t *comparison, size_t max_bytes)
{
    size_t count = comparison->chunk - comparison->compared;
    if (count > max_bytes)
        count = max_bytes;
    if (count > ORDER_OPERAND_BYTES)
        count = ORDER_OPERAND_BYTES;
    int order = memcmp(comparison->windows[0] + comparison->compared,
                        comparison->windows[1] + comparison->compared, count);
    if (order) {
        comparison->order   = order < 0 ? TB_SYMBOL_STRING_BEFORE : TB_SYMBOL_STRING_AFTER;
        comparison->outcome = TB_SYMBOL_SUCCESS;
        comparison->phase   = ORDER_DONE;
        return;
    }
    comparison->compared += count;
    if (comparison->compared == comparison->chunk) {
        comparison->cursor += comparison->chunk;
        comparison->phase = ORDER_REQUEST_LEFT;
    }
}


TbSymbolOutcome_e tb_symbol_string_order_step(TbSymbolStringOrder_t *comparison, size_t max_bytes)
{
    if (!comparison || !max_bytes || max_bytes > ORDER_INSPECTION_BYTES)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (comparison->outcome != TB_SYMBOL_PENDING)
        return comparison->outcome;
    OrderPhase_e phase = comparison->phase;
    uint64_t cursor = comparison->cursor;
    size_t compared = comparison->compared;
    uint64_t left_progress = tb_symbol_file_progress(comparison->files[0]);
    uint64_t right_progress = tb_symbol_file_progress(comparison->files[1]);
    switch (comparison->phase) {
    case ORDER_PROFILE_LEFT:
        profile(comparison, 0);
        break;
    case ORDER_PROFILE_RIGHT:
        profile(comparison, 1);
        break;
    case ORDER_REQUEST_LEFT:
        request(comparison, (OrderSlice_t){
            .input     = 0,
            .max_bytes = max_bytes
        });
        break;
    case ORDER_REQUEST_RIGHT:
        request(comparison, (OrderSlice_t){
            .input     = 1,
            .max_bytes = max_bytes
        });
        break;
    case ORDER_READ_LEFT:
        read_window(comparison, (OrderSlice_t){
            .input     = 0,
            .max_bytes = max_bytes
        });
        break;
    case ORDER_READ_RIGHT:
        read_window(comparison, (OrderSlice_t){
            .input     = 1,
            .max_bytes = max_bytes
        });
        break;
    case ORDER_COMPARE:
        compare_window(comparison, max_bytes);
        break;
    default:
        fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, (TbSymbolStringOrderFailure_t){
            .reason = TB_SYMBOL_STRING_ORDER_REQUEST,
            .input  = 0
        });
        break;
    }
    if (comparison->phase != phase || comparison->cursor != cursor || comparison->compared != compared ||
        tb_symbol_file_progress(comparison->files[0]) != left_progress ||
        tb_symbol_file_progress(comparison->files[1]) != right_progress) {
        if (comparison->progress == UINT64_MAX)
            fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, (TbSymbolStringOrderFailure_t){
                .reason = TB_SYMBOL_STRING_ORDER_REQUEST,
                .input  = 0
            });
        else
            ++comparison->progress;
    }
    return comparison->outcome;
}


bool tb_symbol_string_order_result(const TbSymbolStringOrder_t *comparison, TbSymbolStringOrder_e *order)
{
    if (!comparison || !order || comparison->outcome != TB_SYMBOL_SUCCESS)
        return false;
    *order = comparison->order;
    return true;
}


TbSymbolStringOrderFailure_t tb_symbol_string_order_failure(const TbSymbolStringOrder_t *comparison)
{
    return comparison ? comparison->failure : (TbSymbolStringOrderFailure_t){
        .reason = TB_SYMBOL_STRING_ORDER_REQUEST,
        .input  = UINT_MAX
    };
}


uint64_t tb_symbol_string_order_progress(const TbSymbolStringOrder_t *comparison)
{
    return comparison ? comparison->progress : 0;
}


void tb_symbol_string_order_destroy(TbSymbolStringOrder_t *comparison)
{
    if (!comparison)
        return;
    tb_symbol_file_destroy(comparison->files[0]);
    tb_symbol_file_destroy(comparison->files[1]);
    (void)tb_symbol_budget_release(comparison->construction, sizeof(*comparison));
    free(comparison);
}
