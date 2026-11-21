#include "symbol/name_order.h"

#include <elf.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    TbSymbolNameOrderInput_t input;
    TbSymbolFile_t          *file;
    TbSymbolFileRange_t      request;
    const unsigned char     *window;
    uint64_t                 cursor;
    uint64_t                 total;
    uint64_t                 produced;
    size_t                   component;
    size_t                   window_bytes;
    size_t                   consumed;
    bool                     separator_pending;
    bool                     pending;
} NameStream_t;

typedef struct {
    unsigned input;
    size_t   max_bytes;
} NameSlice_t;

struct tb_symbol_name_order {
    TbSymbolBudget_t          *construction;
    NameStream_t               streams[2];
    size_t                     profiling;
    unsigned                   input;
    unsigned                   phase;
    uint64_t                   progress;
    TbSymbolOutcome_e          outcome;
    TbSymbolStringOrder_e      order;
    TbSymbolNameOrderFailure_t failure;
};

enum {
    NAME_PROFILE,
    NAME_LEFT,
    NAME_RIGHT,
    NAME_COMPARE,
    NAME_DONE
};

TbSymbolNameOrder_t *tb_symbol_name_order_create(const TbSymbolNameOrderInput_t inputs[2],
                                                 TbSymbolBudget_t *construction)
{
    if (!inputs || !construction)
        return NULL;
    for (unsigned i = 0; i < 2; ++i) {
        TbSymbolSectionsView_t directory = inputs[i].directory;
        if (!directory.header || !directory.layout || !directory.entries || inputs[i].descriptor < 0 ||
            directory.count != directory.layout->sections.count ||
            directory.backing.bytes != directory.header->file_bytes ||
            (inputs[i].count && !inputs[i].components) ||
            inputs[i].count > SIZE_MAX / sizeof(*inputs[i].components)) {
            return NULL;
        }
    }
    if (!tb_symbol_budget_reserve(construction, sizeof(TbSymbolNameOrder_t)))
        return NULL;
    TbSymbolNameOrder_t *comparison = calloc(1, sizeof(*comparison));
    if (!comparison) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolNameOrder_t));
        return NULL;
    }
    comparison->construction = construction;
    for (unsigned i = 0; i < 2; ++i) {
        comparison->streams[i].input = inputs[i];
        comparison->streams[i].file = tb_symbol_file_create(inputs[i].directory.backing,
                                                            inputs[i].descriptor, construction);
        if (!comparison->streams[i].file) {
            tb_symbol_name_order_destroy(comparison);
            return NULL;
        }
    }
    return comparison;
}


static void fail(TbSymbolNameOrder_t *comparison, TbSymbolOutcome_e outcome,
                  TbSymbolStringOrderFailure_e reason, unsigned input, size_t component)
{
    comparison->outcome = outcome;
    comparison->phase = NAME_DONE;
    comparison->failure = (TbSymbolNameOrderFailure_t){
        .input     = input,
        .component = component,
        .reason    = reason
    };
}


static void profile(TbSymbolNameOrder_t *comparison)
{
    NameStream_t *stream = &comparison->streams[comparison->input];
    if (comparison->profiling == stream->input.count) {
        comparison->profiling = 0;
        if (comparison->input == 0)
            comparison->input = 1;
        else
            comparison->phase = NAME_LEFT;
        return;
    }
    size_t at = comparison->profiling;
    TbSymbolStringView_t string = stream->input.components[at];
    TbSymbolSectionsView_t directory = stream->input.directory;
    if (directory.backing.key.device != string.backing.key.device ||
        directory.backing.key.inode != string.backing.key.inode ||
        directory.backing.bytes != string.backing.bytes || string.section_index >= directory.count) {
        fail(comparison, TB_SYMBOL_MALFORMED, TB_SYMBOL_STRING_ORDER_RANGE, comparison->input, at);
        return;
    }
    TbSymbolElfSection_t section = directory.entries[string.section_index].header;
    if ((section.type != SHT_PROGBITS && section.type != SHT_STRTAB) || (section.flags & SHF_COMPRESSED)) {
        fail(comparison, TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_STRING_ORDER_SECTION, comparison->input, at);
        return;
    }
    uint64_t end;
    uint64_t section_end;
    if (!tb_symbol_add(section.offset, section.bytes, &section_end) || section_end > string.backing.bytes ||
        !tb_symbol_add(string.offset, string.bytes, &end) || end >= string.end_offset ||
        string.end_offset > section.bytes) {
        fail(comparison, TB_SYMBOL_MALFORMED, TB_SYMBOL_STRING_ORDER_RANGE, comparison->input, at);
        return;
    }
    uint64_t total;
    if (!tb_symbol_add(stream->total, string.bytes, &total) ||
        (at && !tb_symbol_add(total, 2, &total))) {
        fail(comparison, TB_SYMBOL_RESOURCE_FAILURE, TB_SYMBOL_STRING_ORDER_RANGE, comparison->input, at);
        return;
    }
    stream->total = total;
    ++comparison->profiling;
}


static void stream_window(TbSymbolNameOrder_t *comparison, NameSlice_t slice)
{
    static const unsigned char separator[] = "::";
    unsigned input = slice.input;
    size_t max_bytes = slice.max_bytes;
    NameStream_t *stream = &comparison->streams[input];
    if (stream->consumed < stream->window_bytes || stream->produced == stream->total) {
        comparison->phase = input == 0 ? NAME_RIGHT : NAME_COMPARE;
        return;
    }
    if (stream->pending) {
        TbSymbolOutcome_e outcome = tb_symbol_file_step(stream->file, max_bytes);
        if (outcome == TB_SYMBOL_PENDING)
            return;
        TbSymbolFileView_t window;
        if (outcome != TB_SYMBOL_SUCCESS || !tb_symbol_file_view(stream->file, &window) ||
            window.offset != stream->request.offset || window.bytes != stream->request.bytes) {
            fail(comparison, outcome == TB_SYMBOL_RESOURCE_FAILURE ? TB_SYMBOL_RESOURCE_FAILURE
                                                                   : TB_SYMBOL_OPERATIONAL_FAILURE,
                 TB_SYMBOL_STRING_ORDER_READ, input, stream->component);
            comparison->failure.read = tb_symbol_file_failure(stream->file);
            return;
        }
        stream->window = window.data;
        stream->window_bytes = window.bytes;
        stream->consumed = 0;
        stream->pending = false;
        comparison->phase = input == 0 ? NAME_RIGHT : NAME_COMPARE;
        return;
    }
    if (stream->separator_pending) {
        stream->window = separator;
        stream->window_bytes = 2;
        stream->consumed = 0;
        stream->separator_pending = false;
        comparison->phase = input == 0 ? NAME_RIGHT : NAME_COMPARE;
        return;
    }
    if (stream->component >= stream->input.count) {
        fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_STRING_ORDER_SPAN,
             input, stream->component);
        return;
    }
    TbSymbolStringView_t string = stream->input.components[stream->component];
    if (stream->cursor == string.bytes) {
        ++stream->component;
        stream->cursor = 0;
        stream->separator_pending = stream->component < stream->input.count;
        return;
    }
    uint64_t remaining = string.bytes - stream->cursor;
    size_t count = remaining < max_bytes ? (size_t)remaining : max_bytes;
    uint64_t offset;
    TbSymbolElfSection_t section = stream->input.directory.entries[string.section_index].header;
    if (!tb_symbol_add(section.offset, string.offset, &offset) ||
        !tb_symbol_add(offset, stream->cursor, &offset) ||
        !tb_symbol_file_request(stream->file, (TbSymbolFileRange_t){
            .offset = offset,
            .bytes  = count
        })) {
        fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_STRING_ORDER_REQUEST,
             input, stream->component);
        return;
    }
    stream->request = (TbSymbolFileRange_t){
        .offset = offset,
        .bytes  = count
    };
    stream->cursor += count;
    stream->pending = true;
}


static void compare(TbSymbolNameOrder_t *comparison, size_t max_bytes)
{
    NameStream_t *left = &comparison->streams[0];
    NameStream_t *right = &comparison->streams[1];
    if (left->produced == left->total || right->produced == right->total) {
        comparison->order = left->total == right->total ? TB_SYMBOL_STRING_EQUAL
                              : left->produced == left->total ? TB_SYMBOL_STRING_BEFORE
                                                              : TB_SYMBOL_STRING_AFTER;
        comparison->outcome = TB_SYMBOL_SUCCESS;
        comparison->phase = NAME_DONE;
        return;
    }
    size_t count = left->window_bytes - left->consumed;
    size_t right_count = right->window_bytes - right->consumed;
    if (count > right_count)
        count = right_count;
    if (count > max_bytes)
        count = max_bytes;
    if (count > 32768)
        count = 32768;
    if (!count) {
        fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_STRING_ORDER_SPAN, 0, left->component);
        return;
    }
    int order = memcmp(left->window + left->consumed, right->window + right->consumed, count);
    if (order) {
        comparison->order = order < 0 ? TB_SYMBOL_STRING_BEFORE : TB_SYMBOL_STRING_AFTER;
        comparison->outcome = TB_SYMBOL_SUCCESS;
        comparison->phase = NAME_DONE;
        return;
    }
    left->consumed += count;
    right->consumed += count;
    left->produced += count;
    right->produced += count;
    comparison->phase = NAME_LEFT;
}


TbSymbolOutcome_e tb_symbol_name_order_step(TbSymbolNameOrder_t *comparison, size_t max_bytes)
{
    if (!comparison || !max_bytes || max_bytes > 65536)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (comparison->outcome != TB_SYMBOL_PENDING)
        return comparison->outcome;
    if (comparison->progress == UINT64_MAX) {
        fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_STRING_ORDER_SPAN, 0, SIZE_MAX);
        return comparison->outcome;
    }
    uint64_t file_progress[2];
    size_t components[2];
    uint64_t cursors[2];
    for (unsigned i = 0; i < 2; ++i) {
        file_progress[i] = tb_symbol_file_progress(comparison->streams[i].file);
        components[i] = comparison->streams[i].component;
        cursors[i] = comparison->streams[i].cursor;
    }
    unsigned phase = comparison->phase;
    switch (comparison->phase) {
    case NAME_PROFILE:
        profile(comparison);
        break;
    case NAME_LEFT:
        stream_window(comparison, (NameSlice_t){
            .input     = 0,
            .max_bytes = max_bytes
        });
        break;
    case NAME_RIGHT:
        stream_window(comparison, (NameSlice_t){
            .input     = 1,
            .max_bytes = max_bytes
        });
        break;
    case NAME_COMPARE:
        compare(comparison, max_bytes);
        break;
    default:
        fail(comparison, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_STRING_ORDER_SPAN, 0, SIZE_MAX);
        break;
    }
    bool advanced = phase != comparison->phase || phase == NAME_PROFILE || phase == NAME_COMPARE;
    for (unsigned i = 0; i < 2; ++i) {
        advanced = advanced || file_progress[i] != tb_symbol_file_progress(comparison->streams[i].file) ||
                   components[i] != comparison->streams[i].component ||
                   cursors[i] != comparison->streams[i].cursor;
    }
    if (advanced)
        ++comparison->progress;
    return comparison->outcome;
}


bool tb_symbol_name_order_result(const TbSymbolNameOrder_t *comparison, TbSymbolStringOrder_e *order)
{
    if (!comparison || !order || comparison->outcome != TB_SYMBOL_SUCCESS)
        return false;
    *order = comparison->order;
    return true;
}


TbSymbolNameOrderFailure_t tb_symbol_name_order_failure(const TbSymbolNameOrder_t *comparison)
{
    return comparison ? comparison->failure : (TbSymbolNameOrderFailure_t){
        .component = SIZE_MAX,
        .reason    = TB_SYMBOL_STRING_ORDER_REQUEST
    };
}


uint64_t tb_symbol_name_order_progress(const TbSymbolNameOrder_t *comparison)
{
    return comparison ? comparison->progress : 0;
}


void tb_symbol_name_order_destroy(TbSymbolNameOrder_t *comparison)
{
    if (!comparison)
        return;
    for (unsigned i = 0; i < 2; ++i)
        tb_symbol_file_destroy(comparison->streams[i].file);
    TbSymbolBudget_t *construction = comparison->construction;
    free(comparison);
    (void)tb_symbol_budget_release(construction, sizeof(TbSymbolNameOrder_t));
}
