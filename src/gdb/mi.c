#include "mi.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *data;
    size_t length, at, values;
    TbMiError error;
} Parse;

static void fail(Parse *parse, TbMiStatus code, size_t offset, const char *message)
{
    if (parse->error.code == TB_MI_OK) {
        parse->error = (TbMiError){code, offset, message};
    }
}

static bool digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static bool word(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           digit(c) || c == '_' || c == '-' || c == '.';
}

static int hex_digit(unsigned char c)
{
    if (digit(c)) { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

static char *copy(Parse *parse, size_t start, size_t length)
{
    char *text = malloc(length + 1);
    if (text == NULL) {
        fail(parse, TB_MI_NOMEM, start, "out of memory");
        return NULL;
    }
    memcpy(text, parse->data + start, length);
    text[length] = '\0';
    return text;
}

static char *identifier(Parse *parse)
{
    size_t start = parse->at;
    while (parse->at < parse->length && word((unsigned char)parse->data[parse->at])) {
        ++parse->at;
    }
    if (start == parse->at) {
        fail(parse, TB_MI_INVALID, start, "expected a field name or record class");
        return NULL;
    }
    return copy(parse, start, parse->at - start);
}

static bool take(Parse *parse, char character)
{
    if (parse->at < parse->length && parse->data[parse->at] == character) {
        ++parse->at;
        return true;
    }
    return false;
}

static bool quoted(Parse *parse, TbMiString *string)
{
    if (!take(parse, '"')) {
        fail(parse, TB_MI_INVALID, parse->at, "expected a quoted string");
        return false;
    }
    size_t start = parse->at, end = start;
    /* Scan only this string before allocating: allocating all remaining input
     * for every field would multiply memory use for large records. */
    while (end < parse->length && parse->data[end] != '"') {
        unsigned char c = (unsigned char)parse->data[end++];
        if (c < 32) {
            fail(parse, TB_MI_INVALID, end - 1, "unescaped control byte in string");
            return false;
        }
        if (c == '\\' && end < parse->length) { ++end; }
    }
    if (end == parse->length) {
        fail(parse, TB_MI_INVALID, end, "unterminated quoted string");
        return false;
    }
    char *decoded = malloc(end - start + 1);
    if (decoded == NULL) {
        fail(parse, TB_MI_NOMEM, start, "out of memory");
        return false;
    }
    size_t used = 0;
    for (size_t i = start; i < end; ++i) {
        unsigned char c = (unsigned char)parse->data[i];
        if (c == '\\') {
            size_t escape = i++;
            c = (unsigned char)parse->data[i];
            switch (c) {
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'e': c = 27; break; /* GDB/GNU escape. */
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'v': c = '\v'; break;
            case '\\': case '"': case '\'': case '?': break;
            default: {
                unsigned value = 0;
                if (c >= '0' && c <= '7') {
                    value = (unsigned)(c - '0');
                    for (int count = 1; count < 3 && i + 1 < end &&
                         parse->data[i + 1] >= '0' && parse->data[i + 1] <= '7'; ++count) {
                        value = value * 8 + (unsigned)(parse->data[++i] - '0');
                    }
                } else if (c == 'x') {
                    if (i + 1 == end || hex_digit((unsigned char)parse->data[i + 1]) < 0) {
                        fail(parse, TB_MI_INVALID, escape, "hex escape requires digits");
                    }
                    while (i + 1 < end && hex_digit((unsigned char)parse->data[i + 1]) >= 0) {
                        unsigned next = (unsigned)hex_digit((unsigned char)parse->data[++i]);
                        if (value > (255U - next) / 16U) {
                            fail(parse, TB_MI_INVALID, escape, "escape exceeds one byte");
                            break;
                        }
                        value = value * 16U + next;
                    }
                } else {
                    fail(parse, TB_MI_INVALID, escape, "unknown string escape");
                }
                if (value > 255U) {
                    fail(parse, TB_MI_INVALID, escape, "escape exceeds one byte");
                }
                c = (unsigned char)value;
                break;
            }
            }
        }
        if (parse->error.code != TB_MI_OK) {
            free(decoded);
            return false;
        }
        decoded[used++] = (char)c;
    }
    decoded[used] = '\0';
    *string = (TbMiString){decoded, used};
    parse->at = end + 1;
    return true;
}

static void values_free(TbMiValue *value)
{
    while (value != NULL) {
        TbMiValue *next = value->next;
        values_free(value->children); /* Construction bounds recursion to 64. */
        free(value->name);
        free(value->string.data);
        free(value);
        value = next;
    }
}

void tb_mi_record_free(TbMiRecord *record)
{
    if (record == NULL) { return; }
    free(record->token);
    free(record->klass);
    free(record->stream.data);
    values_free(record->results);
    free(record);
}

const TbMiValue *tb_mi_find(const TbMiValue *values, const char *name)
{
    if (name == NULL) { return NULL; }
    for (; values != NULL; values = values->next) {
        if (values->name != NULL && strcmp(values->name, name) == 0) { return values; }
    }
    return NULL;
}

static TbMiValue *value_parse(Parse *parse, size_t depth);

static TbMiValue *entry_parse(Parse *parse, bool named, size_t depth)
{
    char *name = NULL;
    if (named) {
        name = identifier(parse);
        if (name == NULL) { return NULL; }
        if (!take(parse, '=')) {
            fail(parse, TB_MI_INVALID, parse->at, "expected '=' after field name");
            free(name);
            return NULL;
        }
    }
    TbMiValue *value = value_parse(parse, depth);
    if (value == NULL) { free(name); }
    else { value->name = name; }
    return value;
}

static TbMiValue *value_parse(Parse *parse, size_t depth)
{
    if (parse->values == TB_MI_MAX_VALUES) {
        fail(parse, TB_MI_LIMIT, parse->at, "record exceeds value-count limit");
        return NULL;
    }
    if (parse->at == parse->length) {
        fail(parse, TB_MI_INVALID, parse->at, "expected a value");
        return NULL;
    }
    char marker = parse->data[parse->at];
    if (marker != '"' && marker != '{' && marker != '[') {
        fail(parse, TB_MI_INVALID, parse->at, "expected string, tuple or list");
        return NULL;
    }
    if (marker != '"' && depth == TB_MI_MAX_DEPTH) {
        fail(parse, TB_MI_LIMIT, parse->at, "record exceeds nesting limit");
        return NULL;
    }
    TbMiValue *value = calloc(1, sizeof *value);
    if (value == NULL) {
        fail(parse, TB_MI_NOMEM, parse->at, "out of memory");
        return NULL;
    }
    ++parse->values;
    value->type = marker == '"' ? TB_MI_STRING : marker == '{' ? TB_MI_TUPLE : TB_MI_LIST;
    if (marker == '"') {
        if (quoted(parse, &value->string)) { return value; }
    } else {
        ++parse->at;
        char close = marker == '{' ? '}' : ']';
        if (take(parse, close)) { return value; }
        /* MI lists contain either values or results, never a mixture. Tuples
         * contain results. Empty containers retain their tuple/list kind. */
        bool named = marker == '{' || (parse->at < parse->length &&
                     parse->data[parse->at] != '"' && parse->data[parse->at] != '{' &&
                     parse->data[parse->at] != '[');
        TbMiValue **tail = &value->children;
        while (true) {
            TbMiValue *child = entry_parse(parse, named, depth + 1);
            if (child == NULL) { break; }
            *tail = child;
            tail = &child->next;
            if (take(parse, close)) { return value; }
            if (!take(parse, ',')) {
                fail(parse, TB_MI_INVALID, parse->at, "expected comma or container end");
                break;
            }
        }
    }
    values_free(value);
    return NULL;
}

static void trailing_space(Parse *parse)
{
    while (parse->at < parse->length &&
           (parse->data[parse->at] == ' ' || parse->data[parse->at] == '\t')) {
        ++parse->at;
    }
}

static void record_parse(Parse *parse, TbMiRecord *record)
{
    if (parse->length >= 5 && memcmp(parse->data, "(gdb)", 5) == 0) {
        record->kind = TB_MI_PROMPT;
        parse->at = 5;
        trailing_space(parse);
        return;
    }
    while (parse->at < parse->length && digit((unsigned char)parse->data[parse->at])) {
        ++parse->at;
    }
    if (parse->at != 0) {
        record->token = copy(parse, 0, parse->at);
        if (record->token == NULL) { return; }
    }
    if (parse->at == parse->length) {
        fail(parse, TB_MI_INVALID, parse->at, "expected MI record prefix");
        return;
    }
    size_t prefix_at = parse->at;
    char prefix = parse->data[parse->at++];
    switch (prefix) {
    case '^': record->kind = TB_MI_RESULT; break;
    case '*': record->kind = TB_MI_EXEC; break;
    case '+': record->kind = TB_MI_STATUS; break;
    case '=': record->kind = TB_MI_NOTIFY; break;
    case '~': record->kind = TB_MI_CONSOLE; break;
    case '@': record->kind = TB_MI_TARGET; break;
    case '&': record->kind = TB_MI_LOG; break;
    default:
        fail(parse, TB_MI_INVALID, prefix_at, "unknown MI record prefix");
        return;
    }
    if (prefix == '~' || prefix == '@' || prefix == '&') {
        if (record->token != NULL) {
            fail(parse, TB_MI_INVALID, prefix_at, "stream record cannot have a token");
            return;
        }
        if (parse->at < parse->length && parse->data[parse->at] == '"') {
            (void)quoted(parse, &record->stream);
            trailing_space(parse);
        } else {
            /* The stream-record documentation also permits raw text, with an
             * implicit newline. Keep its trailing spaces and bytes verbatim. */
            size_t length = parse->length - parse->at;
            char *text = malloc(length + 2);
            if (text == NULL) {
                fail(parse, TB_MI_NOMEM, parse->at, "out of memory");
                return;
            }
            memcpy(text, parse->data + parse->at, length);
            text[length] = '\n';
            text[length + 1] = '\0';
            record->stream = (TbMiString){text, length + 1};
            parse->at = parse->length;
        }
        return;
    }
    record->klass = identifier(parse);
    if (record->klass == NULL) { return; }
    TbMiValue **tail = &record->results;
    while (take(parse, ',')) {
        TbMiValue *value = entry_parse(parse, true, 0);
        if (value == NULL) { return; }
        *tail = value;
        tail = &value->next;
    }
    trailing_space(parse);
}

TbMiStatus tb_mi_parse(const char *data, size_t length,
                      TbMiRecord **record, TbMiError *error)
{
    Parse parse = {.data = data, .length = length,
                   .error = {TB_MI_OK, 0, "no error"}};
    if (record != NULL) { *record = NULL; }
    if (record == NULL || data == NULL) {
        fail(&parse, TB_MI_INVALID, 0, "invalid parser arguments");
    } else if (length > TB_MI_MAX_RECORD_BYTES) {
        fail(&parse, TB_MI_LIMIT, TB_MI_MAX_RECORD_BYTES, "record exceeds byte limit");
    } else {
        const char *bad = memchr(data, '\0', length);
        if (bad == NULL) { bad = memchr(data, '\r', length); }
        if (bad == NULL) { bad = memchr(data, '\n', length); }
        if (bad != NULL) {
            fail(&parse, TB_MI_INVALID, (size_t)(bad - data), "raw NUL or record delimiter");
        } else {
            TbMiRecord *parsed = calloc(1, sizeof *parsed);
            if (parsed == NULL) {
                fail(&parse, TB_MI_NOMEM, 0, "out of memory");
            } else {
                record_parse(&parse, parsed);
                if (parse.error.code == TB_MI_OK && parse.at != length) {
                    fail(&parse, TB_MI_INVALID, parse.at, "unexpected trailing bytes");
                }
                if (parse.error.code == TB_MI_OK) { *record = parsed; }
                else { tb_mi_record_free(parsed); }
            }
        }
    }
    if (error != NULL) { *error = parse.error; }
    return parse.error.code;
}

static TbMiStatus emit_line(TbMiParser *parser, TbMiEmit emit, void *context, bool eof)
{
    TbMiError error = {TB_MI_OK, 0, "no error"};
    TbMiRecord *record = NULL;
    if (parser->discard) {
        error = (TbMiError){TB_MI_LIMIT, TB_MI_MAX_RECORD_BYTES, "record exceeds byte limit"};
    } else if (parser->length == 0) {
        return TB_MI_OK;
    } else if (eof) {
        error = (TbMiError){TB_MI_TRUNCATED, parser->length, "unterminated record at EOF"};
    } else {
        (void)tb_mi_parse(parser->buffer, parser->length, &record, &error);
    }
    parser->length = 0;
    parser->discard = false;
    parser->failed = error.code == TB_MI_NOMEM;
    emit(context, record, record == NULL ? &error : NULL);
    return parser->failed ? TB_MI_NOMEM : TB_MI_OK;
}

TbMiStatus tb_mi_feed(TbMiParser *parser, const char *data, size_t length,
                     TbMiEmit emit, void *context)
{
    if (parser == NULL || emit == NULL || (data == NULL && length != 0)) {
        return TB_MI_INVALID;
    }
    if (parser->failed) { return TB_MI_NOMEM; }
    for (size_t i = 0; i < length; ++i) {
        char c = data[i];
        if (parser->skip_lf) {
            parser->skip_lf = false;
            if (c == '\n') { continue; }
        }
        if (c == '\r' || c == '\n') {
            if (emit_line(parser, emit, context, false) == TB_MI_NOMEM) { return TB_MI_NOMEM; }
            parser->skip_lf = c == '\r';
            continue;
        }
        if (parser->discard) { continue; }
        if (parser->length == TB_MI_MAX_RECORD_BYTES) {
            parser->discard = true;
            parser->length = 0;
            continue;
        }
        if (parser->length == parser->capacity) {
            size_t capacity = parser->capacity == 0 ? 256 : parser->capacity * 2;
            if (capacity > TB_MI_MAX_RECORD_BYTES) { capacity = TB_MI_MAX_RECORD_BYTES; }
            char *buffer = realloc(parser->buffer, capacity);
            if (buffer == NULL) {
                TbMiError error = {TB_MI_NOMEM, parser->length, "out of memory buffering record"};
                parser->failed = true;
                emit(context, NULL, &error);
                return TB_MI_NOMEM;
            }
            parser->buffer = buffer;
            parser->capacity = capacity;
        }
        parser->buffer[parser->length++] = c;
    }
    return TB_MI_OK;
}

TbMiStatus tb_mi_finish(TbMiParser *parser, TbMiEmit emit, void *context)
{
    if (parser == NULL || emit == NULL) { return TB_MI_INVALID; }
    if (parser->failed) { return TB_MI_NOMEM; }
    TbMiStatus status = emit_line(parser, emit, context, true);
    parser->skip_lf = false;
    return status;
}

void tb_mi_parser_destroy(TbMiParser *parser)
{
    if (parser != NULL) {
        free(parser->buffer);
        memset(parser, 0, sizeof *parser);
    }
}
