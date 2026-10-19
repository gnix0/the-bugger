#define _POSIX_C_SOURCE 200809L
#include "session.h"
#include "../support/check.h"
#include <stdlib.h>
#include <unistd.h>
#include <elf.h>
#include <string.h>

static void run(const char *peer, const char *mode)
{
    CHECK(setenv("TB_FAKE_MODE", mode, 1) == 0);
    TbSession *s = tb_session_create(); CHECK(s);
    TbSessionConfig config = {.kind = TB_SESSION_REMOTE, .gdb = peer, .endpoint = "localhost:3333"};
    const TbMemoryRegion regions[] = {{0x20000000, 0x20000100}, {0x08000000, 0x08000100}};
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
        const TbSessionCapabilities *caps = tb_session_capabilities(s);
        CHECK(caps->connected && !caps->local_process && !caps->server_owned);
        CHECK(strcmp(caps->target_id, "987654321") == 0 && tb_session_target_pid(s) == 0);
        CHECK(caps->address_bits == 32 && strcmp(caps->architecture, "arm") == 0);
        CHECK(caps->byte_order == (strcmp(mode, "remote-unknown") == 0 ? TB_MEMORY_UNKNOWN : TB_MEMORY_BIG));
        CHECK(caps->octet_addressing == (strcmp(mode, "remote-wide") != 0));
        CHECK(!caps->image_relocation_known);
        tb_memory_visible(tb_session_memory(s), 0, true);
        for (int i = 0; i < 10; ++i) { tb_session_pump(s, 1); }
        CHECK(!tb_memory_view(tb_session_memory(s), 0)->configured);
        const TbContextSnapshot *snapshot = tb_context_snapshot(tb_session_context(s));
        uint64_t ready_until = tb_gdb_now_ms() + 1000;
        while ((!snapshot->ready || snapshot->stale) && tb_gdb_now_ms() < ready_until) { tb_session_pump(s, 1); }
        CHECK(snapshot->ready && !snapshot->stale);
        if (strcmp(mode, "remote-regions") == 0) {
            TbMemory *m = tb_session_memory(s); CHECK(tb_memory_length(m, 0, 16));
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
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(tb_session_endpoint("localhost:3333") && tb_session_endpoint("[::1]:65535"));
    const char *bad[] = {"|touch /tmp/no", "/dev/ttyUSB0", "localhost", "-x:1", "h:0", "h:65536", "h:1\n-gdb-exit", "h:22;true", "h:2 3"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) { CHECK(!tb_session_endpoint(bad[i])); }
    run(argv[1], "remote-big"); run(argv[1], "remote-unknown"); run(argv[1], "remote-wide"); run(argv[1], "remote-regions");
    run(argv[1], "remote-refused"); run(argv[1], "remote-disconnect-error");
    run(argv[1], "remote-timeout"); run(argv[1], "remote-cancel"); run(argv[1], "remote-drop"); run(argv[1], "remote-mismatch");
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M15 TCP validation, remote IDs never local PIDs, ARM/big-endian/unknown/wide metadata, no automatic values, refusal and disconnect failure");
    return 0;
}
