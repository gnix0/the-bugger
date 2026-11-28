#include "symbol/sha256.h"
#include "../support/check.h"

#include <string.h>

static void check_digest(const unsigned char *digest, const char *expected)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < TB_SHA256_DIGEST_BYTES; ++i) {
        CHECK(hex[digest[i] >> 4] == expected[i * 2]);
        CHECK(hex[digest[i] & 15] == expected[i * 2 + 1]);
    }
    CHECK(strlen(expected) == TB_SHA256_DIGEST_BYTES * 2);
}


static void vector(const void *data, size_t length, const char *expected)
{
    for (size_t chunk = 1; chunk <= 129; ++chunk) {
        TbSha256_t context;
        CHECK(tb_sha256_init(&context));
        const unsigned char *bytes = data;
        size_t offset = 0;
        while (offset < length) {
            size_t count = length - offset < chunk ? length - offset : chunk;
            CHECK(tb_sha256_update(&context, bytes + offset, count));
            CHECK(tb_sha256_update(&context, NULL, 0));
            offset += count;
        }
        unsigned char digest[TB_SHA256_DIGEST_BYTES];
        CHECK(tb_sha256_final(&context, length, digest));
        check_digest(digest, expected);
        CHECK(context.state == TB_SHA256_FINISHED);
        CHECK(!tb_sha256_update(&context, "x", 1));
        CHECK(!tb_sha256_final(&context, length, digest));
        check_digest(digest, expected);
    }
}


static void vectors(void)
{
    /* NIST SHA-256 examples; links and the binary-boundary oracle are in testing.md. */
    vector(NULL, 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    vector("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char long_message[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    vector(long_message, sizeof(long_message) - 1,
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    static const struct {
        size_t      length;
        const char *digest;
    } boundaries[] = {
        {
            .length = 1,
            .digest = "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d"
        },
        {
            .length = 55,
            .digest = "463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"
        },
        {
            .length = 56,
            .digest = "da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"
        },
        {
            .length = 63,
            .digest = "29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"
        },
        {
            .length = 64,
            .digest = "fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"
        },
        {
            .length = 65,
            .digest = "4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"
        },
        {
            .length = 119,
            .digest = "da18797ed7c3a777f0847f429724a2d8cd5138e6ed2895c3fa1a6d39d18f7ec6"
        },
        {
            .length = 120,
            .digest = "f52b23db1fbb6ded89ef42a23ce0c8922c45f25c50b568a93bf1c075420bbb7c"
        },
        {
            .length = 127,
            .digest = "92ca0fa6651ee2f97b884b7246a562fa71250fedefe5ebf270d31c546bfea976"
        },
        {
            .length = 128,
            .digest = "471fb943aa23c511f6f72f8d1652d9c880cfa392ad80503120547703e56a2be5"
        },
        {
            .length = 129,
            .digest = "5099c6a56203f9687f7d33f4bfdf576d31dc91f6b695ecea38b2770c87631135"
        },
        {
            .length = 256,
            .digest = "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880"
        },
    };
    unsigned char bytes[256];
    for (unsigned i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (unsigned char)i;
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i)
        vector(bytes, boundaries[i].length, boundaries[i].digest);

    TbSha256_t context;
    CHECK(tb_sha256_init(&context));
    unsigned char block[1000];
    memset(block, 'a', sizeof(block));
    for (unsigned i = 0; i < 1000; ++i)
        CHECK(tb_sha256_update(&context, block, sizeof(block)));
    unsigned char digest[TB_SHA256_DIGEST_BYTES];
    CHECK(tb_sha256_final(&context, 1000000, digest));
    check_digest(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}


static void failures(void)
{
    unsigned char digest[TB_SHA256_DIGEST_BYTES];
    memset(digest, 0x5a, sizeof(digest));
    TbSha256_t context = {0};
    CHECK(!tb_sha256_init(NULL));
    CHECK(!tb_sha256_update(&context, "abc", 3));
    CHECK(!tb_sha256_final(&context, 0, digest));
    CHECK(!tb_sha256_update(NULL, NULL, 0));
    CHECK(!tb_sha256_final(NULL, 0, digest));

    CHECK(tb_sha256_init(&context));
    CHECK(tb_sha256_update(&context, "abc", 3));
    CHECK(!tb_sha256_update(&context, NULL, 1));
    CHECK(context.state == TB_SHA256_FAILED);
    CHECK(!tb_sha256_final(&context, 3, digest));
    CHECK(!tb_sha256_update(&context, "def", 3));

    CHECK(tb_sha256_init(&context));
    CHECK(tb_sha256_update(&context, "abc", 3));
    CHECK(!tb_sha256_final(&context, 4, digest));
    CHECK(!tb_sha256_final(&context, 3, digest));

    CHECK(tb_sha256_init(&context));
    CHECK(!tb_sha256_final(&context, 0, NULL));
    CHECK(!tb_sha256_final(&context, 0, digest));

    /* Inject an unreachable-size boundary without allocating or reading that input. */
    CHECK(tb_sha256_init(&context));
    context.length = UINT64_MAX / 8;
    CHECK(tb_sha256_update(&context, NULL, 0));
    CHECK(!tb_sha256_update(&context, "x", 1));
    CHECK(!tb_sha256_final(&context, UINT64_MAX / 8, digest));
    CHECK(tb_sha256_init(&context));
    if (SIZE_MAX > UINT64_MAX / 8) {
        CHECK(!tb_sha256_update(&context, "x", SIZE_MAX));
        CHECK(!tb_sha256_final(&context, 0, digest));
    }
    for (size_t i = 0; i < sizeof(digest); ++i)
        CHECK(digest[i] == 0x5a);

    CHECK(tb_sha256_init(&context));
    CHECK(tb_sha256_update(&context, "abc", 3));
    CHECK(tb_sha256_final(&context, 3, digest));
    check_digest(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}


int main(void)
{
    vectors();
    failures();
    puts("PASS SHA-256 vectors, binary padding boundaries, fragmented input and poisoned-stream failures");
    return 0;
}
