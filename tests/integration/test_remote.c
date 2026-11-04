#define _POSIX_C_SOURCE 200809L
#include "session.h"
#include "../support/check.h"
#include <stdlib.h>
#include <unistd.h>
#include <elf.h>
#include <string.h>

static void mmio_close(const char *peer, bool dispatched)
{
    CHECK(setenv("TB_FAKE_MODE", dispatched ? "remote-mmio-slow" : "remote-mmio", 1) == 0);
    TbSession_t *s = tb_session_create(); CHECK(s);
    TbSessionConfig_t config = {.kind = TB_SESSION_REMOTE, .gdb = peer, .endpoint = "localhost:3333"};
    CHECK(tb_session_start(s, &config));
    tb_memory_visible(tb_session_memory(s), 0, true);
    uint64_t until = tb_gdb_now_ms() + 3000;
    const TbContextSnapshot_t *context = tb_context_snapshot(tb_session_context(s));
    while ((!context->ready || context->stale || tb_session_state(s) != TB_SESSION_STOPPED) && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    if (!context->ready || context->stale) {
        fprintf(stderr, "MMIO close fixture: %s | %s\n", tb_session_state_name(s), tb_session_message(s));
        for (size_t i = 0; i < tb_session_log_count(s); ++i) { fprintf(stderr, "%s\n", tb_session_log_line(s, i)); }
    }
    CHECK(context->ready && !context->stale);
    TbMemory_t *m = tb_session_memory(s); TbMemoryBackend_t review;
    CHECK(tb_session_mmio_review(s, &review) && tb_session_mmio_confirm(s, &review));
    CHECK(tb_memory_range_set(m, 0, 0, 0x40020010, 0x40020020, TB_MEMORY_MMIO, 32, "Authored peer only"));
    CHECK(tb_memory_range_authorize(m, 0, 0) && tb_memory_read_register(m, 0, 0x40020014, 32));
    if (dispatched) { for (unsigned i = 0; i < 50; ++i) { tb_session_pump(s, 1); } }
    CHECK(!tb_session_console(s, "echo rejected-while-MMIO-pending"));
    CHECK(tb_memory_backend_confirmed(m) && tb_memory_range_authorized(m, 0, 0));
    CHECK(!tb_session_continue(s));
    tb_session_close(s);
    CHECK(!tb_memory_backend_confirmed(m) && !tb_memory_range_authorized(m, 0, 0));
    CHECK(!tb_session_mmio_confirm(s, &review));
    until = tb_gdb_now_ms() + 6500;
    while (tb_session_active(s) && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    CHECK(tb_session_state(s) == TB_SESSION_CLOSED);
    bool uncertain = false;
    for (size_t i = 0; i < tb_session_log_count(s); ++i) {
        const char *line = tb_session_log_line(s, i);
        uncertain = uncertain || strstr(line, "MMIO outcome uncertain") != NULL;
        CHECK(!strstr(line, "GDB request rejected"));
        if (!dispatched) { CHECK(!strstr(line, "TBM1|")); }
    }
    CHECK(uncertain == dispatched);
    CHECK(tb_session_destroy(s));
}

static void run(const char *peer, const char *mode)
{
    CHECK(setenv("TB_FAKE_MODE", mode, 1) == 0);
    TbSession_t *s = tb_session_create(); CHECK(s);
    TbSessionConfig_t config = {.kind = TB_SESSION_REMOTE, .gdb = peer, .endpoint = "localhost:3333"};
    const TbMemoryRegion_t regions[] = {{0x20000000, 0x20000100, true}, {0x08000000, 0x08000100, false}};
    if (strcmp(mode, "remote-regions") == 0) {
        config.ordinary_regions = regions; config.ordinary_region_count = 2;
    }
    char image[] = "/tmp/the-bugger-remote-image-XXXXXX";
    if (strcmp(mode, "remote-mismatch") == 0) {
        int fd = mkstemp(image); CHECK(fd >= 0);
        unsigned char elf[64] = {0x7f, 'E', 'L', 'F', ELFCLASS32, ELFDATA2MSB, EV_CURRENT};
        elf[19] = EM_RISCV; /* Same width/endian as peer; architecture differs. */
        CHECK(write(fd, elf, sizeof elf) == sizeof elf); CHECK(close(fd) == 0);
        config.executable = image;
    }
    CHECK(tb_session_start(s, &config));
    bool cancel = strcmp(mode, "remote-cancel") == 0;
    if (cancel) { tb_session_close(s); }
    uint64_t until = tb_gdb_now_ms() + 4000;
    while (tb_session_state(s) == TB_SESSION_STARTING && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    bool refused = strcmp(mode, "remote-refused") == 0 || strcmp(mode, "remote-timeout") == 0 ||
                   strcmp(mode, "remote-drop") == 0 || strcmp(mode, "remote-mismatch") == 0;
    if (!refused && !cancel) {
        CHECK(tb_session_state(s) == TB_SESSION_STOPPED);
        const TbSessionCapabilities_t *caps = tb_session_capabilities(s);
        CHECK(caps->connected && !caps->local_process && !caps->server_owned);
        CHECK(strcmp(caps->target_id, "987654321") == 0 && tb_session_target_pid(s) == 0);
        CHECK(caps->address_bits == 32 && strcmp(caps->architecture, "arm") == 0);
        CHECK(caps->byte_order == (strcmp(mode, "remote-unknown") == 0 ? TB_MEMORY_UNKNOWN : TB_MEMORY_BIG));
        CHECK(caps->octet_addressing == (strcmp(mode, "remote-wide") != 0));
        CHECK(!caps->image_relocation_known);
        tb_memory_visible(tb_session_memory(s), 0, true);
        for (int i = 0; i < 10; ++i) { tb_session_pump(s, 1); }
        CHECK(!tb_memory_view(tb_session_memory(s), 0)->configured);
        const TbContextSnapshot_t *snapshot = tb_context_snapshot(tb_session_context(s));
        uint64_t ready_until = tb_gdb_now_ms() + 1000;
        while ((!snapshot->ready || snapshot->stale) && tb_gdb_now_ms() < ready_until) { tb_session_pump(s, 1); }
        CHECK(snapshot->ready && !snapshot->stale);
        if (strcmp(mode, "remote-big") == 0) {
            TbMemory_t *m = tb_session_memory(s);
            TbMemoryBackend_t setup;
            CHECK(tb_session_mmio_review(s, &setup));
            CHECK(!tb_memory_backend_confirmed(m));
            CHECK(strstr(setup.server, "localhost:3333") && strstr(setup.server, "board/st_nucleo_f4.cfg"));
            CHECK(strcmp(setup.target, "stm32f4x.cpu") == 0 && strstr(setup.firmware, "V2J48M35"));
            CHECK(tb_session_mmio_confirm(s, &setup));
            CHECK(tb_memory_range_set(m, 0, 0, 0x40020010, 0x40020020, TB_MEMORY_MMIO, 32, "Unknown side effects"));
            CHECK(tb_memory_range_authorize(m, 0, 0));
            CHECK(!tb_session_console(s, "echo rejected\n"));
            CHECK(tb_memory_backend_confirmed(m) && tb_memory_range_authorized(m, 0, 0));
            CHECK(tb_session_console(s, "echo accepted"));
            /* No pump has executed: revocation precedes command transmission. */
            CHECK(!tb_memory_backend_confirmed(m) && !tb_memory_backend(m));
            CHECK(!tb_memory_range_authorized(m, 0, 0));
            CHECK(tb_memory_range(m, 0, 0)->used); /* Definition survives revocation. */
            CHECK(!tb_memory_read_register(m, 0, 0x40020014, 32));
            CHECK(!tb_session_mmio_confirm(s, &setup));
            for (unsigned i = 0; i < 30; ++i) { tb_session_pump(s, 1); }
            TbMemoryBackend_t renewed;
            CHECK(tb_session_mmio_review(s, &renewed));
            CHECK(renewed.configuration != setup.configuration);
            CHECK(!tb_session_mmio_confirm(s, &setup));
            CHECK(tb_session_mmio_confirm(s, &renewed));
            CHECK(!tb_memory_range_authorized(m, 0, 0));
        }
        if (strcmp(mode, "remote-regions") == 0) {
            TbMemory_t *m = tb_session_memory(s); CHECK(tb_memory_length(m, 0, 16));
            for (size_t i = 0; i < 2; ++i) {
                CHECK(tb_memory_address(m, 0, i ? "0x08000000" : "0x20000000"));
                until = tb_gdb_now_ms() + 1000;
                while (tb_memory_view(m, 0)->pending && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
                CHECK(tb_memory_view(m, 0)->readable == 16 && tb_memory_view(m, 0)->bytes[15] == 15);
                CHECK(tb_memory_browse(m, 0, 1));
                until = tb_gdb_now_ms() + 1000;
                while (tb_memory_view(m, 0)->pending && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
                CHECK(tb_memory_view(m, 0)->address == regions[i].begin + 16);
                CHECK(tb_memory_browse(m, 0, -1));
                until = tb_gdb_now_ms() + 1000;
                while (tb_memory_view(m, 0)->pending && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
                CHECK(tb_memory_view(m, 0)->address == regions[i].begin);
                CHECK(!tb_memory_browse(m, 0, -1));
            }
        }
        if (strcmp(mode, "remote-big") == 0) {
            CHECK(tb_memory_address(tb_session_memory(s), 0, "0x100000000"));
            for (int i = 0; i < 10; ++i) { tb_session_pump(s, 1); }
            CHECK(strstr(tb_memory_view(tb_session_memory(s), 0)->error, "overflow"));
            CHECK(!tb_memory_view(tb_session_memory(s), 0)->pending);
        }
        if (strcmp(mode, "remote-wide") == 0) {
            CHECK(!tb_memory_address(tb_session_memory(s), 0, "0x20000000"));
            CHECK(strstr(tb_memory_view(tb_session_memory(s), 0)->error, "unsupported"));
        }
        tb_session_close(s);
    }
    until = tb_gdb_now_ms() + 4000;
    while (tb_session_active(s) && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    CHECK(!tb_session_active(s));
    CHECK(tb_session_state(s) == (refused || strcmp(mode, "remote-disconnect-error") == 0 ? TB_SESSION_FAILED : TB_SESSION_CLOSED));
    CHECK(!tb_session_capabilities(s)->connected);
    CHECK(tb_session_destroy(s));
    if (strcmp(mode, "remote-mismatch") == 0) { CHECK(unlink(image) == 0); }
}
static void ram_ready(TbSession_t *s)
{
    TbSessionRamReview_t review;
    uint64_t until = tb_gdb_now_ms() + 4000;
    tb_memory_visible(tb_session_memory(s), 0, true);
    while (!tb_session_ram_review(s, &review) && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    CHECK(tb_session_ram_review(s, &review));
}
static void ram_close(TbSession_t *s)
{
    tb_session_close(s);
    uint64_t until = tb_gdb_now_ms() + 5000;
    while (tb_session_active(s) && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    CHECK(!tb_session_active(s) && !tb_session_ram_confirmed(s));
}
static void ram_evidence(const char *peer)
{
    CHECK(setenv("TB_FAKE_MODE", "remote-openocd-ram", 1) == 0);
    TbOpenOcdPlan_t plan = {0}; /* External authored peer; never launches hardware tools. */
    TbSessionConfig_t config = {.kind = TB_SESSION_REMOTE, .gdb = peer, .endpoint = "localhost:3333", .openocd = &plan};
    TbSession_t *s = tb_session_create(), *other = tb_session_create(); CHECK(s && other);
    CHECK(tb_session_start(s, &config)); ram_ready(s);
    CHECK(tb_session_start(other, &config)); ram_ready(other);
    TbSessionRamReview_t first, current, tampered;
    CHECK(tb_session_ram_review(s, &first) && !tb_session_ram_confirmed(s));
    CHECK(first.region.begin == UINT64_C(0x20000000) && first.region.end == UINT64_C(0x20020000));
    CHECK(strstr(first.device, "user-confirmed"));
    CHECK(!tb_session_ram_confirm(other, &first)); /* same endpoint/inferior, different connection */
    tb_memory_target(tb_session_memory(other), true, 16);
    CHECK(tb_session_ram_review(other, &current) && !tb_session_ram_confirm(other, &current));
    CHECK(!tb_session_ram_confirmed(other)); /* no partial publication on a consumer capability mismatch */
    tb_memory_target(tb_session_memory(other), true, 32);
    tampered = first; tampered.region.end = UINT64_C(0x40000000);
    CHECK(!tb_session_ram_confirm(s, &tampered));
    CHECK(tb_session_ram_confirm(s, &first) && tb_session_ram_confirmed(s));
    CHECK(!tb_session_ram_confirm(s, &first)); /* evidence token consumed */
    CHECK(tb_session_ram_review(s, &current));
    CHECK(tb_session_ram_confirm(s, &current)); /* same numeric bounds, new generation */
    CHECK(!tb_session_ram_confirm(s, &current));
    ram_ready(s);
    CHECK(tb_session_console(s, "echo harmless"));
    CHECK(!tb_session_ram_confirmed(s) && !tb_session_ram_confirm(s, &first));
    ram_ready(s); CHECK(tb_session_ram_review(s, &current));
    CHECK(tb_session_ram_confirm(s, &current)); ram_ready(s);
    CHECK(tb_session_reset_halt(s)); CHECK(!tb_session_ram_confirmed(s));
    ram_ready(s); CHECK(tb_session_ram_review(s, &current));
    CHECK(tb_session_ram_confirm(s, &current)); ram_ready(s);
    TbContext_t *context = tb_session_context(s);
    uint64_t until = tb_gdb_now_ms() + 3000;
    while (!tb_context_write_begin(context, 99991) && tb_gdb_now_ms() < until) { tb_session_pump(s, 1); }
    CHECK(tb_context_write_blocked(context));
    tb_context_write_finish(context, 99991, TB_WRITE_UNRESOLVED);
    tb_session_ram_revoke(s); ram_ready(s);
    CHECK(tb_session_ram_review(s, &current) && tb_session_ram_confirm(s, &current));
    CHECK(tb_context_write_blocked(context) && !tb_context_write_reconcile(context, 99991));
    ram_close(s); CHECK(tb_session_start(s, &config)); ram_ready(s);
    CHECK(!tb_session_ram_confirmed(s) && !tb_session_ram_confirm(s, &current));
    ram_close(s); ram_close(other);
    CHECK(tb_session_destroy(s) && tb_session_destroy(other));
    const TbMemoryRegion_t invalid[] = {{1, 4, true}, {2, 3, false}};
    config.ordinary_regions = invalid; config.ordinary_region_count = 2;
    s = tb_session_create(); CHECK(s && !tb_session_start(s, &config));
    config.ordinary_region_count = TB_MEMORY_REGIONS + 1; CHECK(!tb_session_start(s, &config));
    const TbMemoryRegion_t malformed[] = {{1, 4, true}, {8, 7, false}};
    config.ordinary_regions = malformed; config.ordinary_region_count = 2; CHECK(!tb_session_start(s, &config));
    CHECK(tb_session_destroy(s));
    puts("PASS F04 user-confirmed SRAM binding, cross-connection/reconnect rejection, atomic evidence, reset/Console revocation and quarantine preservation (authored peer)");
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    TbSession_t *idle = tb_session_create(); CHECK(idle);
    CHECK(tb_session_destroy(idle)); /* MMIO releases its lease before its controller dies. */
    CHECK(tb_session_endpoint("localhost:3333") && tb_session_endpoint("[::1]:65535"));
    const char *bad[] = {"|touch /tmp/no", "/dev/ttyUSB0", "localhost", "-x:1", "h:0", "h:65536", "h:1\n-gdb-exit", "h:22;true", "h:2 3"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) { CHECK(!tb_session_endpoint(bad[i])); }
    run(argv[1], "remote-big"); run(argv[1], "remote-unknown"); run(argv[1], "remote-wide"); run(argv[1], "remote-regions");
    run(argv[1], "remote-refused"); run(argv[1], "remote-disconnect-error");
    run(argv[1], "remote-timeout"); run(argv[1], "remote-cancel"); run(argv[1], "remote-drop"); run(argv[1], "remote-mismatch");
    ram_evidence(argv[1]);
    mmio_close(argv[1], false); mmio_close(argv[1], true);
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M15 TCP validation, remote IDs never local PIDs, ARM/big-endian/unknown/wide metadata, no automatic values, refusal and disconnect failure");
    return 0;
}
