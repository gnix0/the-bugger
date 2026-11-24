#include "symbol/crc32.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static void vector(const unsigned char *bytes, size_t length, uint32_t expected)
{
    for (size_t chunk = 1; chunk < 100; ++chunk) {
        TbCrc32_t crc;
        CHECK(tb_crc32_init(&crc));
        for (size_t offset = 0; offset < length;) {
            size_t count = length - offset < chunk ? length - offset : chunk;
            CHECK(tb_crc32_update(&crc, bytes + offset, count));
            offset += count;
        }
        uint32_t value;
        CHECK(tb_crc32_update(&crc, NULL, 0) && tb_crc32_final(&crc, length, &value));
        CHECK(value == expected && crc.state == TB_CRC32_FINISHED);
        CHECK(!tb_crc32_update(&crc, NULL, 0) && !tb_crc32_final(&crc, length, &value));
    }
}


int main(void)
{
    vector(NULL, 0, 0);
    vector((const unsigned char *)"123456789", 9, UINT32_C(0xcbf43926));
    vector((const unsigned char *)"abc", 3, UINT32_C(0x352441c2));
    unsigned char bytes[65536];
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (unsigned char)i;
    vector(bytes, 256, UINT32_C(0x29058c73));
    TbCrc32_t crc;
    CHECK(tb_crc32_init(&crc) && tb_crc32_update(&crc, bytes, sizeof(bytes)));
    uint32_t value;
    CHECK(tb_crc32_final(&crc, sizeof(bytes), &value) && value == UINT32_C(0xb11de6a1));

    memset(bytes, 'a', sizeof(bytes));
    CHECK(tb_crc32_init(&crc));
    size_t total = 0;
    while (total < 1000000) {
        size_t count = 1000000 - total < sizeof(bytes) ? 1000000 - total : sizeof(bytes);
        CHECK(tb_crc32_update(&crc, bytes, count));
        total += count;
    }
    CHECK(tb_crc32_final(&crc, total, &value) && value == UINT32_C(0xdc25bfbc));
    CHECK(tb_crc32_init(&crc) && tb_crc32_update(&crc, "abc", 3));
    value = UINT32_C(0xdeadbeef);
    CHECK(!tb_crc32_final(&crc, 2, &value) && value == UINT32_C(0xdeadbeef));
    CHECK(crc.state == TB_CRC32_FAILED && !tb_crc32_final(&crc, 3, &value));
    CHECK(tb_crc32_init(&crc) && !tb_crc32_update(&crc, NULL, 1));
    CHECK(tb_crc32_init(&crc) && !tb_crc32_update(&crc, bytes, 65537));
    CHECK(tb_crc32_init(&crc));
    crc.bytes = UINT64_MAX;
    CHECK(!tb_crc32_update(&crc, bytes, 1) && crc.state == TB_CRC32_FAILED);
    CHECK(tb_crc32_init(&crc) && !tb_crc32_final(&crc, 0, NULL));
    CHECK(!tb_crc32_init(NULL) && !tb_crc32_update(NULL, NULL, 0) && !tb_crc32_final(NULL, 0, &value));
    puts("PASS GNU CRC32 vectors, fragmented streams, complete length and poisoned failures");
    return 0;
}
