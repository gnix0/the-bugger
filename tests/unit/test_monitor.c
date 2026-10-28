#include "monitor.h"
#include "mi.h"
#include "../support/check.h"
#include <string.h>

static const char good[] = "TBM1|probe-1|stm32f4x.cpu|20000000|32|1|12345678|END\n";
static void begin(TbMonitorFrame *f)
{
    CHECK(tb_monitor_start(f, "probe-1", "stm32f4x.cpu", 0x20000000, 32));
}
static void malformed(const char *text)
{
    TbMonitorFrame frame; begin(&frame); uint64_t value = 77;
    tb_monitor_feed(&frame, text, strlen(text)); CHECK(!tb_monitor_finish(&frame, true, &value)); CHECK(value == 77);
}
typedef struct { TbMonitorFrame frame; unsigned active, completed; } Replay;
static void next(Replay *r, unsigned token)
{
    char id[64]; (void)snprintf(id, sizeof id, "d2304c391cb1698bb6c4cc6293930c53-%u", token);
    CHECK(tb_monitor_start(&r->frame, id, "stm32f4x.cpu", 0x20000000, 32)); r->active = token;
}
static void record(void *context, TbMiRecord *mi, const TbMiError *error)
{
    Replay *r = context; CHECK(!error && mi);
    if (mi->kind == TB_MI_TARGET && r->active) { tb_monitor_feed(&r->frame, mi->stream.data, mi->stream.length); }
    if (mi->kind == TB_MI_RESULT && mi->token) {
        unsigned token = (unsigned)strtoul(mi->token, NULL, 10);
        if (token == 11) { CHECK(strcmp(mi->klass, "connected") == 0); next(r, 12); }
        else if (token >= 12 && token <= 16) {
            CHECK(r->active == token); uint64_t value = 77;
            bool success = token != 14 && token != 15;
            CHECK(tb_monitor_finish(&r->frame, strcmp(mi->klass, "done") == 0, &value) == success);
            CHECK(value == (success ? 0x12345678 : 77)); ++r->completed;
            if (token < 16) { next(r, token + 1); } else { r->active = 0; }
        }
    }
    tb_mi_record_free(mi);
}
static void replay(const char *path, size_t chunk)
{
    FILE *file = fopen(path, "rb"); CHECK(file); char bytes[4096]; TbMiParser parser = {0}; Replay r = {0};
    size_t n;
    while ((n = fread(bytes, 1, chunk, file)) != 0) { CHECK(tb_mi_feed(&parser, bytes, n, record, &r) == TB_MI_OK); }
    CHECK(!ferror(file) && fclose(file) == 0); CHECK(tb_mi_finish(&parser, record, &r) == TB_MI_OK);
    CHECK(r.completed == 5 && !r.active); tb_mi_parser_destroy(&parser);
}
static void width_record(void *context, TbMiRecord *mi, const TbMiError *error)
{
    Replay *r = context; CHECK(!error && mi);
    if (mi->kind == TB_MI_TARGET && r->active) { tb_monitor_feed(&r->frame, mi->stream.data, mi->stream.length); }
    if (mi->kind == TB_MI_RESULT && mi->token) {
        unsigned token = (unsigned)strtoul(mi->token, NULL, 10);
        if (token >= 22 && token <= 24) {
            uint64_t value = 77;
            CHECK(token == r->active && tb_monitor_finish(&r->frame, strcmp(mi->klass, "done") == 0, &value) && value == 0);
            ++r->completed; r->active = token == 24 ? 0 : token + 1;
        } else if (token == 21) { CHECK(strcmp(mi->klass, "connected") == 0); r->active = 22; }
        if (r->active) {
            char id[64]; (void)snprintf(id, sizeof id, "081748b7949a5283f5f7197f4d0843c7-%u", r->active);
            CHECK(tb_monitor_start(&r->frame, id, "stm32f4x.cpu", 0x40020014, 8U << (r->active - 22)));
        }
    }
    tb_mi_record_free(mi);
}
static void width_replay(const char *path)
{
    FILE *file = fopen(path, "rb"); CHECK(file); TbMiParser parser = {0}; Replay r = {0}; int c;
    while ((c = fgetc(file)) != EOF) { char byte = (char)c; CHECK(tb_mi_feed(&parser, &byte, 1, width_record, &r) == TB_MI_OK); }
    CHECK(!ferror(file) && fclose(file) == 0 && tb_mi_finish(&parser, width_record, &r) == TB_MI_OK);
    CHECK(r.completed == 3 && !r.active); tb_mi_parser_destroy(&parser);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 || argc == 3); TbMonitorFrame f; uint64_t value = 0;
    for (size_t chunk = 1; chunk < sizeof good; ++chunk) {
        begin(&f);
        for (size_t at = 0; at < sizeof good - 1; at += chunk) {
            size_t n = sizeof good - 1 - at; if (n > chunk) { n = chunk; } tb_monitor_feed(&f, good + at, n);
        }
        CHECK(tb_monitor_finish(&f, true, &value) && value == 0x12345678);
        CHECK(!tb_monitor_finish(&f, false, &value));
    }
    begin(&f); tb_monitor_feed(&f, good, sizeof good - 1); tb_monitor_feed(&f, good, sizeof good - 1);
    CHECK(!tb_monitor_finish(&f, true, &value));
    malformed("TBM1|probe-1|other.cpu|20000000|32|1|12345678|END\n");
    malformed("TBM1|probe-1|stm32f4x.cpu|20000004|32|1|12345678|END\n");
    malformed("TBM1|probe-1|stm32f4x.cpu|20000000|16|1|1234|END\n");
    malformed("TBM1|probe-1|stm32f4x.cpu|20000000|32|2|12345678|END\n");
    malformed("TBM1|probe-1|stm32f4x.cpu|20000000|32|1|100000000|END\n");
    malformed("TBM1|probe-1|stm32f4x.cpu|20000000|32|1|12345678|END|extra\n");
    malformed("TBM1|probe-1|stm32f4x.cpu|20000000|32|1|12345678|END");
    malformed("TBM1|old-request|stm32f4x.cpu|20000000|32|1|12345678|END\n");
    begin(&f); static char noise[512]; memset(noise, 'x', sizeof noise);
    tb_monitor_feed(&f, noise, sizeof noise); tb_monitor_feed(&f, "\n", 1); tb_monitor_feed(&f, good, sizeof good - 1);
    CHECK(tb_monitor_finish(&f, true, &value));
    begin(&f); for (int i = 0; i < 33; ++i) { tb_monitor_feed(&f, noise, sizeof noise); }
    CHECK(!tb_monitor_finish(&f, true, &value));
    CHECK(!tb_monitor_start(&f, "id|injection", "stm32f4x.cpu", 0x20000000, 32));
    CHECK(!tb_monitor_start(&f, "id", "target;reset", 0x20000000, 32));
    CHECK(!tb_monitor_start(&f, "id", "stm32f4x.cpu", 0x20000001, 32));
    replay(argv[1], 1); replay(argv[1], 13); replay(argv[1], 4096);
    if (argc == 3) { width_replay(argv[2]); }
    puts("PASS bounded monitor framing, actual STM32/OpenOCD capture, fragments/coalescing, unrelated/duplicate/malformed/late frames and MI errors");
    return 0;
}
