#include "mi.h"

#include "../support/check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Link-time allocation faults affect only this test executable. */
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);

static size_t allocation, fail_at;

static bool reject(void)
{
    return fail_at != 0 && ++allocation == fail_at;
}

void *__wrap_malloc(size_t size)
{
    return reject() ? NULL : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    return reject() ? NULL : __real_calloc(count, size);
}

void *__wrap_realloc(void *pointer, size_t size)
{
    return reject() ? NULL : __real_realloc(pointer, size);
}

typedef struct { size_t records, errors; } Events;

static void emit(void *context, TbMiRecord *record, const TbMiError *error)
{
    Events *events = context;
    if (error != NULL) {
        CHECK(record == NULL && error->code == TB_MI_NOMEM);
        ++events->errors;
    } else {
        CHECK(record != NULL);
        ++events->records;
        tb_mi_record_free(record);
    }
}

static void check(const char *line)
{
    size_t length = strlen(line);
    bool completed = false;
    for (size_t failure = 1; failure < 200; ++failure) {
        TbMiRecord *record = NULL;
        TbMiError error;
        allocation = 0;
        fail_at = failure;
        TbMiStatus status = tb_mi_parse(line, length, &record, &error);
        fail_at = 0;
        if (status == TB_MI_OK) {
            CHECK(record != NULL);
            tb_mi_record_free(record);
            completed = true;
            break;
        }
        CHECK(status == TB_MI_NOMEM && error.code == TB_MI_NOMEM && record == NULL);
    }
    CHECK(completed);

    completed = false;
    for (size_t failure = 1; failure < 200; ++failure) {
        TbMiParser parser = {0};
        Events events = {0};
        allocation = 0;
        fail_at = failure;
        TbMiStatus status = tb_mi_feed(&parser, line, length, emit, &events);
        if (status == TB_MI_OK) { status = tb_mi_feed(&parser, "\n", 1, emit, &events); }
        fail_at = 0;
        if (status == TB_MI_OK) {
            CHECK(events.records == 1 && events.errors == 0);
            completed = true;
        } else {
            CHECK(status == TB_MI_NOMEM && events.errors == 1 && events.records == 0);
            CHECK(tb_mi_feed(&parser, "^done\n", 6, emit, &events) == TB_MI_NOMEM);
            CHECK(tb_mi_finish(&parser, emit, &events) == TB_MI_NOMEM);
            CHECK(events.errors == 1);
        }
        tb_mi_parser_destroy(&parser);
        CHECK(tb_mi_feed(&parser, "^done\n", 6, emit, &events) == TB_MI_OK);
        tb_mi_parser_destroy(&parser);
        if (completed) { break; }
    }
    CHECK(completed);
}

int main(void)
{
    check("0009^done,stack=[frame={args=[{name=\"x\",value=\"a\\000b\"}]},frame={}],empty=[]");
    check("~\"quoted\\nstream\"");
    check("@raw target stream");
    char large[800];
    memset(large, 'x', sizeof large);
    large[0] = '~';
    large[sizeof large - 1] = '\0';
    check(large); /* Multiple reallocations, including failure with owned input. */
    puts("PASS NFR-03/04 allocation failures, partial-tree cleanup and parser reset");
    return 0;
}
