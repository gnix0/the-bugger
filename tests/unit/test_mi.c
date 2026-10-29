#include "mi.h"
#include "../support/check.h"

#include <stdint.h>
#include <string.h>

static TbMiRecord *parse_ok(const char *text)
{
    TbMiRecord *record = NULL;
    TbMiError error;
    CHECK(tb_mi_parse(text, strlen(text), &record, &error) == TB_MI_OK);
    CHECK(record != NULL && error.code == TB_MI_OK);
    return record;
}

static const TbMiValue *field(const TbMiValue *values, const char *name)
{
    const TbMiValue *value = tb_mi_find(values, name);
    CHECK(value != NULL);
    return value;
}

static void string_is(const TbMiString *string, const char *expected)
{
    CHECK(string->length == strlen(expected));
    CHECK(memcmp(string->data, expected, string->length) == 0);
    CHECK(string->data[string->length] == '\0');
}

static void invalid(const char *text, TbMiStatus status)
{
    TbMiRecord *record = NULL;
    TbMiError error;
    CHECK(tb_mi_parse(text, strlen(text), &record, &error) == status);
    CHECK(record == NULL && error.code == status);
    CHECK(error.offset <= strlen(text) && error.message != NULL);
}

static void semantics(void)
{
    const char *const classes[] = {"done", "running", "connected", "error", "exit", "future-result"};
    for (size_t i = 0; i < sizeof classes / sizeof classes[0]; ++i) {
        char input[100];
        (void)snprintf(input, sizeof input, "000123456789012345678901234567890^%s", classes[i]);
        TbMiRecord *record = parse_ok(input);
        CHECK(record->kind == TB_MI_RESULT);
        CHECK(strcmp(record->token, "000123456789012345678901234567890") == 0);
        CHECK(strcmp(record->klass, classes[i]) == 0 && record->results == NULL);
        tb_mi_record_free(record);
    }
    TbMiRecord *record = parse_ok(
        "*stopped,reason=\"breakpoint-hit\",frame={level=\"0\",args=[{name=\"a\",value=\"3\"}]},"
        "stack=[frame={func=\"f\"},frame={func=\"main\"}],a=\"first\",a=\"second\","
        "empty={},list=[],values=[\"x\",[],{}]");
    CHECK(record->kind == TB_MI_EXEC && record->token == NULL);
    string_is(&field(record->results, "reason")->string, "breakpoint-hit");
    const TbMiValue *frame = field(record->results, "frame");
    CHECK(frame->type == TB_MI_TUPLE);
    const TbMiValue *args = field(frame->children, "args");
    CHECK(args->type == TB_MI_LIST && args->children->type == TB_MI_TUPLE);
    string_is(&field(args->children->children, "value")->string, "3");
    const TbMiValue *stack = field(record->results, "stack");
    CHECK(stack->children->type == TB_MI_TUPLE);
    CHECK(strcmp(stack->children->name, "frame") == 0);
    CHECK(strcmp(stack->children->next->name, "frame") == 0);
    string_is(&field(stack->children->next->children, "func")->string, "main");
    const TbMiValue *first = field(record->results, "a");
    string_is(&first->string, "first");
    CHECK(strcmp(first->next->name, "a") == 0);
    string_is(&first->next->string, "second");
    CHECK(field(record->results, "empty")->type == TB_MI_TUPLE);
    CHECK(field(record->results, "empty")->children == NULL);
    CHECK(field(record->results, "list")->type == TB_MI_LIST);
    CHECK(field(record->results, "list")->children == NULL);
    const TbMiValue *values = field(record->results, "values");
    CHECK(values->children->name == NULL && values->children->type == TB_MI_STRING);
    CHECK(values->children->next->type == TB_MI_LIST);
    CHECK(values->children->next->next->type == TB_MI_TUPLE);
    CHECK(tb_mi_find(record->results, "missing") == NULL);
    CHECK(tb_mi_find(NULL, "anything") == NULL && tb_mi_find(record->results, NULL) == NULL);
    tb_mi_record_free(record);

    record = parse_ok("17+download,section=\".text\"");
    CHECK(record->kind == TB_MI_STATUS && strcmp(record->token, "17") == 0);
    tb_mi_record_free(record);
    record = parse_ok("=future-event,new_field=\"yes\"");
    CHECK(record->kind == TB_MI_NOTIFY && strcmp(record->klass, "future-event") == 0);
    tb_mi_record_free(record);
    record = parse_ok("(gdb) \t");
    CHECK(record->kind == TB_MI_PROMPT && record->klass == NULL && record->results == NULL);
    tb_mi_record_free(record);

    char owned[] = "^done,x=\"kept\"";
    TbMiRecord *copy = parse_ok(owned);
    memset(owned, 'X', sizeof owned - 1);
    string_is(&field(copy->results, "x")->string, "kept");
    tb_mi_record_free(copy);
    tb_mi_record_free(NULL);
}

static void escapes(void)
{
    TbMiRecord *record = parse_ok("~\"\\a\\b\\f\\n\\r\\t\\v\\e\\\\\\\"\\'\\?\\000\\101\\x42\"");
    const unsigned char expected[] = {7, 8, 12, 10, 13, 9, 11, 27, '\\', '"', '\'', '?', 0, 'A', 'B'};
    CHECK(record->kind == TB_MI_CONSOLE);
    CHECK(record->stream.length == sizeof expected);
    CHECK(memcmp(record->stream.data, expected, sizeof expected) == 0);
    CHECK(record->stream.data[sizeof expected] == '\0');
    tb_mi_record_free(record);
    record = parse_ok("@\"\\377\\xFF\\12A\\1234\\303\\251\"");
    const unsigned char bytes[] = {255, 255, 10, 'A', 'S', '4', 195, 169};
    CHECK(record->kind == TB_MI_TARGET && record->stream.length == sizeof bytes);
    CHECK(memcmp(record->stream.data, bytes, sizeof bytes) == 0);
    tb_mi_record_free(record);
    record = parse_ok("&\"\"");
    CHECK(record->kind == TB_MI_LOG);
    string_is(&record->stream, "");
    tb_mi_record_free(record);
    record = parse_ok("~raw \"text\" \\ stays raw  ");
    string_is(&record->stream, "raw \"text\" \\ stays raw  \n");
    tb_mi_record_free(record);
    record = parse_ok("@");
    string_is(&record->stream, "\n");
    tb_mi_record_free(record);
    record = parse_ok("^done,value=\"before\\000after\"");
    const TbMiString *string = &field(record->results, "value")->string;
    CHECK(string->length == 12 && memcmp(string->data, "before\0after", 12) == 0);
    tb_mi_record_free(record);
}

static void malformed(void)
{
    TbMiRecord *script = parse_ok("^done,bkpt={number=\"1\",script={\"silent\",\"printf 1\"},original-location=\"main\"}");
    const TbMiValue *commands = field(field(script->results, "bkpt")->children, "script");
    CHECK(commands->type == TB_MI_TUPLE && commands->children && !commands->children->name);
    string_is(&commands->children->string, "silent"); string_is(&commands->children->next->string, "printf 1");
    tb_mi_record_free(script);
    invalid("^done,script={\"silent\",{}}", TB_MI_INVALID);
    invalid("^done,script={\"silent\",command=\"other\"}", TB_MI_INVALID);
    const char *const cases[] = {
        "", " ", "^", "123", "123?", "done", ".",
        "^done,", "^done,x", "^done,x=", "^done,x=12", "^done,=tx",
        "^done,x=\"abc", "^done,x=\"abc\\", "^done,x=\"bad\\q\"",
        "^done,x=\"\\x\"", "^done,x=\"\\x100\"", "^done,x=\"\\400\"",
        "^done,x=\"raw\tcontrol\"", "^done,x=\"ok\"garbage",
        "^done,x={", "^done,x={\"not-result\"}", "^done,x={a=\"1\",}",
        "^done,x=[", "^done,x=[\"x\",]", "^done,x=[\"x\",a=\"y\"]",
        "^done,x=[a=\"y\",\"x\"]", "^done,x=[]]",
        "3~\"not tokenized\"", "3@\"x\"", "3&raw", "~\"x\"junk",
        "(gdb)junk", " (gdb)", "^done\n^done", "^done\r", "~raw\n"
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) { invalid(cases[i], TB_MI_INVALID); }
    TbMiRecord *record = NULL;
    TbMiError error;
    const char nul[] = {'~', '"', 'a', '\0', '"'};
    CHECK(tb_mi_parse(nul, sizeof nul, &record, &error) == TB_MI_INVALID);
    CHECK(record == NULL && error.offset == 3);
    CHECK(tb_mi_parse(NULL, 1, &record, &error) == TB_MI_INVALID);
    CHECK(tb_mi_parse("^done", 5, NULL, &error) == TB_MI_INVALID);
    CHECK(tb_mi_parse("^done,", 6, &record, NULL) == TB_MI_INVALID);
}

typedef struct {
    uint64_t hash;
    size_t records, errors;
    TbMiStatus last_error;
    size_t offset;
} Events;

static void hash_bytes(Events *events, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; ++i) {
        events->hash = (events->hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    events->hash = (events->hash ^ 255U) * UINT64_C(1099511628211);
}

static void hash_string(Events *events, const char *text)
{
    if (text != NULL) { hash_bytes(events, text, strlen(text)); }
}

static void hash_values(Events *events, const TbMiValue *value)
{
    for (; value != NULL; value = value->next) {
        hash_bytes(events, &value->type, sizeof value->type);
        hash_string(events, value->name);
        if (value->type == TB_MI_STRING) {
            hash_bytes(events, value->string.data, value->string.length);
        } else { hash_values(events, value->children); }
    }
}

static void collect(void *context, TbMiRecord *record, const TbMiError *error)
{
    Events *events = context;
    CHECK((record == NULL) != (error == NULL));
    if (record != NULL) {
        ++events->records;
        hash_bytes(events, &record->kind, sizeof record->kind);
        hash_string(events, record->token);
        hash_string(events, record->klass);
        hash_bytes(events, record->stream.data, record->stream.length);
        hash_values(events, record->results);
        tb_mi_record_free(record);
    } else {
        ++events->errors;
        CHECK(error->code != TB_MI_OK && error->message != NULL);
        events->last_error = error->code;
        events->offset = error->offset;
    }
}

static Events feed(const char *text, size_t length, size_t split)
{
    TbMiParser parser = {0};
    Events events = {0};
    CHECK(tb_mi_feed(&parser, text, split, collect, &events) == TB_MI_OK);
    CHECK(tb_mi_feed(&parser, text + split, length - split, collect, &events) == TB_MI_OK);
    CHECK(tb_mi_finish(&parser, collect, &events) == TB_MI_OK);
    tb_mi_parser_destroy(&parser);
    return events;
}

static void framing(void)
{
    const char text[] = "\r\n(gdb) \r\n1^done\r*running,thread-id=\"all\"\n\n~\"line\\n\"\r\n";
    Events expected = feed(text, sizeof text - 1, 0);
    CHECK(expected.records == 4 && expected.errors == 0);
    for (size_t split = 0; split <= sizeof text - 1; ++split) {
        Events actual = feed(text, sizeof text - 1, split);
        CHECK(actual.records == expected.records && actual.errors == 0 && actual.hash == expected.hash);
    }
    TbMiParser parser = {0};
    Events events = {0};
    const char tail[] = "^done,x=\"unfinished";
    CHECK(tb_mi_feed(&parser, tail, sizeof tail - 1, collect, &events) == TB_MI_OK);
    CHECK(events.records == 0 && events.errors == 0);
    CHECK(tb_mi_finish(&parser, collect, &events) == TB_MI_OK);
    CHECK(events.errors == 1 && events.last_error == TB_MI_TRUNCATED);
    CHECK(events.offset == sizeof tail - 1);
    CHECK(tb_mi_finish(&parser, collect, &events) == TB_MI_OK && events.errors == 1);
    CHECK(tb_mi_feed(&parser, "^done\n", 6, collect, &events) == TB_MI_OK);
    CHECK(events.records == 1);
    CHECK(tb_mi_feed(&parser, NULL, 0, collect, &events) == TB_MI_OK);
    CHECK(tb_mi_feed(&parser, NULL, 1, collect, &events) == TB_MI_INVALID);
    CHECK(tb_mi_feed(NULL, "", 0, collect, &events) == TB_MI_INVALID);
    CHECK(tb_mi_feed(&parser, "", 0, NULL, NULL) == TB_MI_INVALID);
    CHECK(tb_mi_finish(NULL, collect, &events) == TB_MI_INVALID);
    tb_mi_parser_destroy(&parser);
    tb_mi_parser_destroy(&parser);
    tb_mi_parser_destroy(NULL);
}

static void limits(void)
{
    char nested[TB_MI_MAX_DEPTH * 2 + 32];
    for (int depth = TB_MI_MAX_DEPTH; depth <= TB_MI_MAX_DEPTH + 1; ++depth) {
        size_t at = 0;
        memcpy(nested, "^done,v=", 8);
        at = 8;
        for (int i = 0; i < depth; ++i) { nested[at++] = '['; }
        memcpy(nested + at, "\"x\"", 3);
        at += 3;
        for (int i = 0; i < depth; ++i) { nested[at++] = ']'; }
        nested[at] = '\0';
        if (depth == TB_MI_MAX_DEPTH) { tb_mi_record_free(parse_ok(nested)); }
        else { invalid(nested, TB_MI_LIMIT); }
    }

    size_t capacity = (size_t)TB_MI_MAX_VALUES * 4 + 20;
    char *many = malloc(capacity);
    CHECK(many != NULL);
    for (int count = TB_MI_MAX_VALUES - 1; count <= TB_MI_MAX_VALUES; ++count) {
        size_t at = 9;
        memcpy(many, "^done,v=[", 9);
        for (int i = 0; i < count; ++i) {
            if (i != 0) { many[at++] = ','; }
            memcpy(many + at, "\"x\"", 3);
            at += 3;
        }
        many[at++] = ']';
        many[at] = '\0';
        CHECK(at < capacity);
        if (count == TB_MI_MAX_VALUES - 1) { tb_mi_record_free(parse_ok(many)); }
        else { invalid(many, TB_MI_LIMIT); }
    }
    free(many);

    size_t maximum = TB_MI_MAX_RECORD_BYTES;
    char *large = malloc(maximum + 2);
    CHECK(large != NULL);
    memset(large, 'x', maximum + 1);
    memcpy(large, "^done,v=\"", 9);
    large[maximum - 1] = '"';
    TbMiRecord *record = NULL;
    TbMiError error;
    CHECK(tb_mi_parse(large, maximum, &record, &error) == TB_MI_OK);
    CHECK(record->results->string.length == maximum - 10);
    tb_mi_record_free(record);
    CHECK(tb_mi_parse(large, maximum + 1, &record, &error) == TB_MI_LIMIT);
    CHECK(record == NULL && error.offset == maximum);

    TbMiParser parser = {0};
    Events events = {0};
    CHECK(tb_mi_feed(&parser, large, maximum + 1, collect, &events) == TB_MI_OK);
    CHECK(events.errors == 0 && parser.capacity <= maximum);
    CHECK(tb_mi_feed(&parser, "\r\n77^done\n", 10, collect, &events) == TB_MI_OK);
    CHECK(events.errors == 1 && events.last_error == TB_MI_LIMIT && events.records == 1);
    CHECK(tb_mi_finish(&parser, collect, &events) == TB_MI_OK);
    CHECK(events.errors == 1);
    tb_mi_parser_destroy(&parser);
    parser = (TbMiParser){0};
    events = (Events){0};
    CHECK(tb_mi_feed(&parser, large, maximum + 1, collect, &events) == TB_MI_OK);
    CHECK(tb_mi_finish(&parser, collect, &events) == TB_MI_OK);
    CHECK(events.errors == 1 && events.last_error == TB_MI_LIMIT);
    tb_mi_parser_destroy(&parser);
    free(large);
}

static char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    CHECK(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    *length = (size_t)size;
    char *text = malloc(*length);
    CHECK(text != NULL && fread(text, 1, *length, file) == *length);
    CHECK(fclose(file) == 0);
    return text;
}

static void corpus(const char *valid, const char *bad)
{
    size_t length;
    char *text = read_file(valid, &length);
    Events expected = feed(text, length, 0);
    CHECK(expected.records == 22 && expected.errors == 0);
    for (size_t split = 0; split <= length; ++split) {
        Events actual = feed(text, length, split);
        CHECK(actual.records == expected.records && actual.errors == 0 && actual.hash == expected.hash);
    }
    for (size_t chunk = 1; chunk <= 71; ++chunk) {
        TbMiParser parser = {0};
        Events events = {0};
        for (size_t at = 0; at < length; at += chunk) {
            size_t size = length - at < chunk ? length - at : chunk;
            CHECK(tb_mi_feed(&parser, text + at, size, collect, &events) == TB_MI_OK);
        }
        CHECK(tb_mi_finish(&parser, collect, &events) == TB_MI_OK);
        CHECK(events.records == expected.records && events.errors == 0 && events.hash == expected.hash);
        tb_mi_parser_destroy(&parser);
    }
    free(text);
    text = read_file(bad, &length);
    for (size_t split = 0; split <= length; ++split) {
        Events events = feed(text, length, split);
        CHECK(events.records == 2 && events.errors == 6);
    }
    free(text);
}

static void mutations(void)
{
    const char seed[] = "12^done,v=[{name=\"x\",value=\"quote\\\" and slash\\\\\"},\"\",[\"a\"]]";
    char input[sizeof seed];
    for (size_t at = 0; at < sizeof seed - 1; ++at) {
        for (unsigned byte = 0; byte < 256; ++byte) {
            memcpy(input, seed, sizeof seed);
            input[at] = (char)byte;
            TbMiRecord *record = NULL;
            TbMiError error;
            TbMiStatus status = tb_mi_parse(input, sizeof seed - 1, &record, &error);
            CHECK(status == TB_MI_OK || status == TB_MI_INVALID);
            CHECK((status == TB_MI_OK) == (record != NULL));
            tb_mi_record_free(record);
        }
    }
}

int main(int argc, char *argv[])
{
    CHECK(argc == 3);
    semantics();
    escapes();
    malformed();
    framing();
    limits();
    corpus(argv[1], argv[2]);
    mutations();
    puts("PASS NFR-04 MI trees, classes/tokens, byte strings, corpus splits/chunks, recovery, limits and mutations");
    return EXIT_SUCCESS;
}
