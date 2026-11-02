#ifndef THE_BUGGER_MI_H
#define THE_BUGGER_MI_H

#include <stdbool.h>
#include <stddef.h>

enum {
    TB_MI_MAX_RECORD_BYTES = 1024 * 1024,
    TB_MI_MAX_VALUES = 16384,
    TB_MI_MAX_DEPTH = 64
};

typedef enum {
    TB_MI_OK, TB_MI_INVALID, TB_MI_LIMIT, TB_MI_NOMEM, TB_MI_TRUNCATED
} TbMiStatus;

typedef struct {
    TbMiStatus code;
    size_t offset;               /* Zero-based byte offset within the record. */
    const char *message;        /* Static diagnostic; never owned by caller. */
} TbMiError;

typedef struct {
    char *data;                 /* NUL-terminated, but may contain embedded NUL. */
    size_t length;              /* Authoritative decoded byte length. */
} TbMiString;

typedef enum { TB_MI_STRING, TB_MI_TUPLE, TB_MI_LIST } TbMiType;

typedef struct TbMiValue {
    TbMiType type;
    char *name;                 /* NULL for unnamed list/script elements. */
    TbMiString string;          /* Only for TB_MI_STRING. */
    struct TbMiValue *children; /* Ordered tuple/list entries. */
    struct TbMiValue *next;     /* Duplicate names are preserved. */
} TbMiValue;

typedef enum {
    TB_MI_RESULT = '^', TB_MI_EXEC = '*', TB_MI_STATUS = '+',
    TB_MI_NOTIFY = '=', TB_MI_CONSOLE = '~', TB_MI_TARGET = '@',
    TB_MI_LOG = '&', TB_MI_PROMPT = '('
} TbMiKind;

typedef struct {
    TbMiKind kind;
    char *token;               /* NULL if absent; digits retained without conversion. */
    char *klass;               /* NULL for streams/prompts; unknown classes retained. */
    TbMiValue *results;
    TbMiString stream;
} TbMiRecord;

/* Parse exactly one record WITHOUT CR/LF. On failure, *record is NULL.
 * The returned tree is independent of input and owned by the caller.
 * error may be NULL. No callbacks, subprocesses, curses, or global state. */
TbMiStatus tb_mi_parse(const char *data, size_t length,
                      TbMiRecord **record, TbMiError *error);
void tb_mi_record_free(TbMiRecord *record);
/* Return the first matching sibling; walk next to retain duplicate results. */
const TbMiValue *tb_mi_find(const TbMiValue *values, const char *name);

/* Zero-initialize before use. Fields are implementation state; do not modify. */
typedef struct {
    char *buffer;
    size_t length, capacity;
    bool discard, skip_lf, failed;
} TbMiParser;

/* Exactly one of record/error is non-NULL. Callback owns a successful record;
 * error is valid only during the call. Do not reenter or destroy this parser
 * from its callback. Buffer input must not alias parser-owned storage. */
typedef void (*TbMiEmit)(void *context, TbMiRecord *record, const TbMiError *error);

/* CR, LF and CRLF delimit records. Empty lines are ignored. All complete
 * records/errors are delivered in order; malformed/over-limit lines recover.
 * OK means all input was consumed, NOT that every emitted record was valid.
 * NOMEM is fatal until destroy/reinitialization; the remainder is not consumed.
 * NULL data is allowed only when length is zero. emit must be non-NULL. */
TbMiStatus tb_mi_feed(TbMiParser *parser, const char *data, size_t length,
                     TbMiEmit emit, void *context);
/* At EOF, report an unterminated tail as TRUNCATED (or LIMIT if discarded).
 * This clears framing state and keeps the buffer for reuse. */
TbMiStatus tb_mi_finish(TbMiParser *parser, TbMiEmit emit, void *context);
/* Free buffering and reset to zero; safe on a zero-initialized parser. */
void tb_mi_parser_destroy(TbMiParser *parser);

#endif
