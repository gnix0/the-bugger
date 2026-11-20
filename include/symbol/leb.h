#ifndef THE_BUGGER_SYMBOL_LEB_H
#define THE_BUGGER_SYMBOL_LEB_H

#include "symbol/reader.h"

typedef struct {
    uint64_t bytes;
    uint64_t bits;
    bool     initialized;
    bool     is_signed;
    bool     high_zero;
    bool     high_one;
    bool     done;
    bool     failed;
} TbSymbolLeb_t;

typedef struct {
    uint64_t bytes;
    uint64_t bits;
    bool     is_signed;
    bool     fits;
} TbSymbolLebView_t;

/* Structural LEB framing may exceed the checked reader's numeric width.
 * Initialize before use; do not modify fields directly. No allocation, I/O or
 * retained input pointer. Target byte order does not change LEB encoding. */
bool tb_symbol_leb_init(TbSymbolLeb_t *leb, bool is_signed);
/* Inspect at most 256 input bytes per call. OK means a terminator was consumed;
 * LIMIT means another chunk is needed. consumed identifies only this call's
 * prefix and never includes bytes following the terminator. Empty input makes
 * no progress. INVALID changes neither state nor consumed. Counter exhaustion
 * returns OVERFLOW and makes the scanner unavailable; it is not numeric overflow.
 * Arbitrarily wide values retain valid framing without a truncated numeric value. */
TbSymbolReadStatus_e tb_symbol_leb_feed(TbSymbolLeb_t *leb, const void *bytes, size_t length,
                                      size_t *consumed);
/* End-of-scope check: incomplete framing is TRUNCATED, never a complete value. */
TbSymbolReadStatus_e tb_symbol_leb_finish(const TbSymbolLeb_t *leb);
/* Complete framing only. bits is zero when fits is false. Numeric accessors
 * require the matching signedness and a representable value. Failure leaves
 * outputs unchanged; raw encoded bytes remain with the caller's retained scope. */
bool tb_symbol_leb_view(const TbSymbolLeb_t *leb, TbSymbolLebView_t *view);
bool tb_symbol_leb_uint(const TbSymbolLeb_t *leb, uint64_t *value);
bool tb_symbol_leb_sint(const TbSymbolLeb_t *leb, int64_t *value);

#endif // THE_BUGGER_SYMBOL_LEB_H
