#ifndef THE_BUGGER_SYMBOL_FORM_H
#define THE_BUGGER_SYMBOL_FORM_H

#include "symbol/forms.h"
#include "symbol/leb.h"

typedef enum {
    TB_SYMBOL_FORM_PENDING,
    TB_SYMBOL_FORM_COMPLETE,
    TB_SYMBOL_FORM_INVALID,
    TB_SYMBOL_FORM_TRUNCATED,
    TB_SYMBOL_FORM_UNSUPPORTED
} TbSymbolFormStatus_e;

typedef struct {
    uint64_t            form;
    uint64_t            available;
    unsigned            version;
    unsigned            offset_bytes;
    unsigned            address_bytes;
    TbSymbolByteOrder_e byte_order;
} TbSymbolFormRequest_t;

/* bytes includes selectors, length prefixes and any string terminator. Payload
 * excludes those framing bytes; LEB payloads retain their complete encoding.
 * numeric means raw bits fit in 64 bits, not a semantic signedness/type claim. */
typedef struct {
    uint64_t form;
    uint64_t bytes;
    uint64_t payload_offset;
    uint64_t payload_bytes;
    uint64_t bits;
    bool     numeric;
    bool     is_signed;
    bool     indirect;
} TbSymbolFormView_t;

/* Allocation-free streaming framing. Initialize before use; do not modify state
 * directly. available is the exact remaining unit-body extent at this attribute.
 * Input/output arguments must not alias state. No input pointers are retained.
 * No reference, string-table, location or type
 * validation follows. Direct implicit constants live in the abbreviation table;
 * their DIE footprint is zero and their numeric value is not supplied here. */
typedef struct {
    TbSymbolFormRequest_t request;
    TbSymbolFormView_t    view;
    TbSymbolLeb_t         leb;
    uint64_t             remaining;
    uint64_t             fixed;
    uint64_t             accumulator;
    unsigned             phase;
    TbSymbolFormStatus_e status;
    bool                 initialized;
} TbSymbolForm_t;

TbSymbolFormStatus_e tb_symbol_form_init(TbSymbolForm_t *form, TbSymbolFormRequest_t request);
/* At most 256 bytes/call; never consumes the next attribute. INVALID arguments
 * leave state and consumed unchanged. Completion exposes only a full view.
 * Caller-owned scheduling, cancellation and absolute deadlines remain required. */
TbSymbolFormStatus_e tb_symbol_form_feed(TbSymbolForm_t *form, const void *bytes, size_t length,
                                        size_t *consumed);
TbSymbolFormStatus_e tb_symbol_form_finish(const TbSymbolForm_t *form);
bool tb_symbol_form_view(const TbSymbolForm_t *form, TbSymbolFormView_t *view);

#endif // THE_BUGGER_SYMBOL_FORM_H
