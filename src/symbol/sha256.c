#include "symbol/sha256.h"

#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "SHA-256 requires eight-bit bytes");

/* FIPS 180-4, sections 4.2.2 and 6.2. */
static const uint32_t round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};


static uint32_t rotate_right(uint32_t value, unsigned count)
{
    return (value >> count) | (value << (32U - count));
}


static void compress(TbSha256_t *context, const unsigned char *block)
{
    uint32_t schedule[64];
    for (unsigned i = 0; i < 16; ++i) {
        size_t offset = (size_t)i * 4;

        schedule[i] = ((uint32_t)block[offset] << 24U) | ((uint32_t)block[offset + 1] << 16U) |
                      ((uint32_t)block[offset + 2] << 8U) | block[offset + 3];
    }

    for (unsigned i = 16; i < 64; ++i) {
        uint32_t x      = schedule[i - 15];
        uint32_t y      = schedule[i - 2];
        uint32_t small0 = rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3U);
        uint32_t small1 = rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10U);

        schedule[i] = schedule[i - 16] + small0 + schedule[i - 7] + small1;
    }

    uint32_t a = context->words[0];
    uint32_t b = context->words[1];
    uint32_t c = context->words[2];
    uint32_t d = context->words[3];
    uint32_t e = context->words[4];
    uint32_t f = context->words[5];
    uint32_t g = context->words[6];
    uint32_t h = context->words[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t big1   = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first  = h + big1 + choose + round_constants[i] + schedule[i];
        uint32_t big0   = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t second = big0 + ((a & b) ^ (a & c) ^ (b & c));

        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }

    context->words[0] += a;
    context->words[1] += b;
    context->words[2] += c;
    context->words[3] += d;
    context->words[4] += e;
    context->words[5] += f;
    context->words[6] += g;
    context->words[7] += h;
}


bool tb_sha256_init(TbSha256_t *context)
{
    if (!context)
        return false;

    *context = (TbSha256_t) {
        .words = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        },
        .state = TB_SHA256_ACTIVE
    };

    return true;
}


bool tb_sha256_update(TbSha256_t *context, const void *data, size_t length)
{
    if (!context || context->state != TB_SHA256_ACTIVE)
        return false;

    if ((!data && length) || context->used >= TB_SHA256_BLOCK_BYTES ||
        context->length > UINT64_MAX / 8 || length > UINT64_MAX / 8 - context->length) {
        context->state = TB_SHA256_FAILED;
        return false;
    }

    const unsigned char *bytes = data;

    context->length += length;

    while (length) {
        size_t count = TB_SHA256_BLOCK_BYTES - context->used;
        if (count > length)
            count = length;
        memcpy(context->block + context->used, bytes, count);
        context->used += count;
        bytes        += count;
        length       -= count;

        if (context->used == TB_SHA256_BLOCK_BYTES) {
            compress(context, context->block);
            context->used = 0;
        }
    }

    return true;
}


bool tb_sha256_final(TbSha256_t *context, uint64_t expected_length,
                     unsigned char digest[TB_SHA256_DIGEST_BYTES])
{
    if (!context || context->state != TB_SHA256_ACTIVE)
        return false;

    if (!digest || context->length != expected_length || context->length > UINT64_MAX / 8 ||
        context->used >= TB_SHA256_BLOCK_BYTES) {
        context->state = TB_SHA256_FAILED;
        return false;
    }

    uint64_t bits = context->length * 8;

    context->block[context->used++] = 0x80;

    if (context->used > 56) {
        memset(context->block + context->used, 0, TB_SHA256_BLOCK_BYTES - context->used);
        compress(context, context->block);
        context->used = 0;
    }
    memset(context->block + context->used, 0, 56 - context->used);

    for (unsigned i = 0; i < 8; ++i)
        context->block[56 + i] = (unsigned char)(bits >> ((7 - i) * 8));

    compress(context, context->block);

    for (unsigned i = 0; i < 8; ++i) {
        for (unsigned j = 0; j < 4; ++j)
            digest[i * 4 + j] = (unsigned char)(context->words[i] >> ((3 - j) * 8));
    }

    context->state = TB_SHA256_FINISHED;

    return true;
}
