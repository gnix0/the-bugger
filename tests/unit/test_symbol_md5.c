#include "symbol/md5.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static void check_digest(const unsigned char *bytes, size_t length, const char *expected)
{
    for (size_t chunk = 1; chunk <= 73; ++chunk) {
        TbMd5_t hash;
        CHECK(tb_md5_init(&hash));
        for (size_t cursor = 0; cursor < length;) {
            size_t count = length - cursor < chunk ? length - cursor : chunk;
            CHECK(tb_md5_update(&hash, bytes + cursor, count));
            cursor += count;
        }
        unsigned char digest[16];
        CHECK(tb_md5_final(&hash, length, digest));
        char text[33];
        for (unsigned i = 0; i < 16; ++i)
            CHECK(snprintf(text + 2 * i, 3, "%02x", digest[i]) == 2);
        CHECK(!strcmp(text, expected));
        CHECK(!tb_md5_final(&hash, length, digest));
        CHECK(!tb_md5_update(&hash, bytes, length));
    }
}


int main(void)
{
    const char *texts[] = {
        "", "a", "abc", "message digest", "abcdefghijklmnopqrstuvwxyz",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890"
    };
    const char *digests[] = {
        "d41d8cd98f00b204e9800998ecf8427e", "0cc175b9c0f1b6a831c399e269772661",
        "900150983cd24fb0d6963f7d28e17f72", "f96b697d7cb7938d525a2f31aaf161d0",
        "c3fcd3d76192e4007dfb496cca67e13b", "d174ab98d277d9f5a5611c2c9f419d9f",
        "57edf4a22be3c955ac49da2e2107b67a"
    };
    for (unsigned i = 0; i < 7; ++i)
        check_digest((const unsigned char *)texts[i], strlen(texts[i]), digests[i]);

    const size_t boundaries[] = {55, 56, 63, 64, 65, 119, 120, 127, 128, 129};
    const char *boundary_digests[] = {
        "6912ee65fff2d9f9ce2508cddf8bcda0",
        "51fdd1acda72405dfdfa03fcb85896d7",
        "48a6295221902e8e0938f773a7185e72",
        "b2d3f56bc197fd985d5965079b5e7148",
        "8bd7053801c768420faf816fadba971c",
        "1c772251899a7ff007400b888d6b2042",
        "b7ba1efc6022e9ed272f00b8831e26e6",
        "8402b21e7bc7906493bae0dac017f1f9",
        "37eff01866ba3f538421b30b7cbefcac",
        "46f986692847558fc38b0cece591c20f"
    };
    unsigned char sequence[129];
    for (unsigned i = 0; i < sizeof(sequence); ++i)
        sequence[i] = (unsigned char)i;
    for (unsigned i = 0; i < 10; ++i)
        check_digest(sequence, boundaries[i], boundary_digests[i]);

    unsigned char bytes[65536];
    memset(bytes, 'a', sizeof(bytes));
    TbMd5_t hash;
    CHECK(tb_md5_init(&hash));
    size_t total = 0;
    while (total < 1000000) {
        size_t count = 1000000 - total < sizeof(bytes) ? 1000000 - total : sizeof(bytes);
        CHECK(tb_md5_update(&hash, bytes, count));
        total += count;
    }
    unsigned char digest[16];
    CHECK(tb_md5_final(&hash, total, digest));
    const unsigned char million[] = {
        0x77, 0x07, 0xd6, 0xae, 0x4e, 0x02, 0x7c, 0x70,
        0xee, 0xa2, 0xa9, 0x35, 0xc2, 0x29, 0x6f, 0x21
    };
    CHECK(!memcmp(digest, million, 16));

    CHECK(tb_md5_init(&hash) && tb_md5_update(&hash, "abc", 3));
    memset(digest, 0x55, sizeof(digest));
    CHECK(!tb_md5_final(&hash, 2, digest) && hash.state == TB_MD5_FAILED);
    for (unsigned i = 0; i < 16; ++i)
        CHECK(digest[i] == 0x55);
    CHECK(!tb_md5_final(&hash, 3, digest));
    CHECK(tb_md5_init(&hash) && !tb_md5_update(&hash, NULL, 1));
    CHECK(!tb_md5_final(&hash, 0, digest));
    CHECK(tb_md5_init(&hash) && !tb_md5_update(&hash, bytes, 65537));
    CHECK(hash.state == TB_MD5_FAILED);
    CHECK(tb_md5_init(&hash) && tb_md5_update(&hash, NULL, 0));
    CHECK(!tb_md5_final(&hash, 0, NULL));
    CHECK(tb_md5_init(&hash));
    hash.length = UINT64_MAX / 8;
    CHECK(!tb_md5_update(&hash, bytes, 1));
    CHECK(!tb_md5_init(NULL) && !tb_md5_update(NULL, NULL, 0) && !tb_md5_final(NULL, 0, digest));
    puts("PASS MD5 published vectors, fragmented blocks, complete-length proof and poisoned failures");
    return 0;
}
