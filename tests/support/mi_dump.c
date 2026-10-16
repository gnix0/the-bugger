#include "mi.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t records, errors; } Summary;

static void string_print(const char *data, size_t length)
{
    (void)putchar('"');
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\\' || c == '"') { (void)putchar('\\'); }
        if (c >= 32 && c < 127) { (void)putchar(c); }
        else { (void)printf("\\%03o", (unsigned)c); }
    }
    (void)putchar('"');
}

static void values_print(const TbMiValue *value, int depth)
{
    for (; value != NULL; value = value->next) {
        (void)printf("%*s", depth * 2, "");
        if (value->name != NULL) {
            string_print(value->name, strlen(value->name));
            fputs(" = ", stdout);
        }
        if (value->type == TB_MI_STRING) {
            string_print(value->string.data, value->string.length);
            (void)printf(" (%zu bytes)\n", value->string.length);
        } else {
            puts(value->type == TB_MI_TUPLE ? "tuple" : "list");
            values_print(value->children, depth + 1);
        }
    }
}

static void emit(void *context, TbMiRecord *record, const TbMiError *error)
{
    Summary *summary = context;
    ++summary->records;
    if (error != NULL) {
        ++summary->errors;
        fprintf(stderr, "record %zu: %s at byte %zu (status %d)\n",
                summary->records, error->message, error->offset, (int)error->code);
        return;
    }
    (void)printf("record %zu: %c", summary->records, (char)record->kind);
    if (record->token != NULL) {
        fputs(" token=", stdout);
        string_print(record->token, strlen(record->token));
    }
    if (record->klass != NULL) {
        fputs(" class=", stdout);
        string_print(record->klass, strlen(record->klass));
    }
    (void)putchar('\n');
    if (record->kind == TB_MI_CONSOLE || record->kind == TB_MI_TARGET || record->kind == TB_MI_LOG) {
        fputs("  stream=", stdout);
        string_print(record->stream.data, record->stream.length);
        (void)printf(" (%zu bytes)\n", record->stream.length);
    }
    values_print(record->results, 1);
    tb_mi_record_free(record);
}

static int usage(void)
{
    fputs("Usage: mi-dump [--chunk N] [FILE|-]\n"
          "Decode MI records only; no debugger or target is launched.\n"
          "N is 1..65536. Exit: 0 valid, 1 MI errors, 2 usage/I/O failure.\n", stderr);
    return 2;
}

int main(int argc, char *argv[])
{
    size_t chunk = 4096;
    int at = 1;
    if (at < argc && strcmp(argv[at], "--chunk") == 0) {
        if (++at == argc) { return usage(); }
        char *end;
        errno = 0;
        unsigned long count = strtoul(argv[at++], &end, 10);
        if (errno != 0 || *end != '\0' || count == 0 || count > 65536) { return usage(); }
        chunk = (size_t)count;
    }
    if (argc - at > 1) { return usage(); }
    FILE *input = stdin;
    if (at < argc && strcmp(argv[at], "-") != 0) {
        input = fopen(argv[at], "rb");
        if (input == NULL) { perror(argv[at]); return 2; }
    }
    TbMiParser parser = {0};
    Summary summary = {0};
    char buffer[65536];
    TbMiStatus status = TB_MI_OK;
    while (status == TB_MI_OK) {
        size_t length = fread(buffer, 1, chunk, input);
        if (length == 0) { break; }
        status = tb_mi_feed(&parser, buffer, length, emit, &summary);
    }
    bool io_error = ferror(input) != 0;
    if (io_error) { fputs("mi-dump: input read failed\n", stderr); }
    if (input != stdin && fclose(input) != 0) { io_error = true; }
    if (status == TB_MI_OK) { status = tb_mi_finish(&parser, emit, &summary); }
    tb_mi_parser_destroy(&parser);
    if (fflush(stdout) == EOF) {
        fputs("mi-dump: output write failed\n", stderr);
        io_error = true;
    }
    return io_error ? 2 : summary.errors != 0 || status != TB_MI_OK ? 1 : 0;
}
