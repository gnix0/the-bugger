#include "symbol/md5.h"

#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "MD5 requires eight-bit bytes");

/* RFC 1321 section 3.4: floor(2^32 * abs(sin(i + 1))). */
static const uint32_t constants[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};
static const unsigned shifts[4][4] = {
    {7, 12, 17, 22},
    {5, 9, 14, 20},
    {4, 11, 16, 23},
    {6, 10, 15, 21}
};

static void block(TbMd5_t *context, const unsigned char *bytes)
{
    uint32_t words[16];
    for (size_t i = 0; i < 16; ++i) {
        words[i] = (uint32_t)bytes[4 * i] | (uint32_t)bytes[4 * i + 1] << 8U |
                   (uint32_t)bytes[4 * i + 2] << 16U | (uint32_t)bytes[4 * i + 3] << 24U;
    }

    uint32_t a = context->words[0];
    uint32_t b = context->words[1];
    uint32_t c = context->words[2];
    uint32_t d = context->words[3];
    for (unsigned i = 0; i < 64; ++i) {
        unsigned round = i / 16;
        uint32_t f;
        unsigned index;
        switch (round) {
        case 0:
            f = (b & c) | (~b & d);
            index = i;
            break;
        case 1:
            f = (d & b) | (~d & c);
            index = (5 * i + 1) % 16;
            break;
        case 2:
            f = b ^ c ^ d;
            index = (3 * i + 5) % 16;
            break;
        default:
            f = c ^ (b | ~d);
            index = (7 * i) % 16;
            break;
        }
        uint32_t sum = a + f + constants[i] + words[index];
        unsigned shift = shifts[round][i % 4];
        uint32_t next = b + ((sum << shift) | (sum >> (32U - shift)));
        a = d;
        d = c;
        c = b;
        b = next;
    }
    context->words[0] += a;
    context->words[1] += b;
    context->words[2] += c;
    context->words[3] += d;
}


bool tb_md5_init(TbMd5_t *context)
{
    if (!context)
        return false;

    *context = (TbMd5_t) {
        .words = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476},
        .state = TB_MD5_ACTIVE
    };

    return true;
}


bool tb_md5_update(TbMd5_t *context, const void *bytes, size_t length)
{
    if (!context || context->state != TB_MD5_ACTIVE)
        return false;
    if ((!bytes && length) || length > 65536 || length > UINT64_MAX / 8 - context->length) {
        context->state = TB_MD5_FAILED;
        return false;
    }

    const unsigned char *input = bytes;
    context->length += length;
    while (length) {
        size_t available = TB_MD5_BLOCK_BYTES - context->used;
        size_t count = length < available ? length : available;
        memcpy(context->block + context->used, input, count);
        context->used += count;
        input += count;
        length -= count;
        if (context->used == TB_MD5_BLOCK_BYTES) {
            block(context, context->block);
            context->used = 0;
        }
    }

    return true;
}


bool tb_md5_final(TbMd5_t *context, uint64_t expected_length, unsigned char digest[TB_MD5_DIGEST_BYTES])
{
    if (!context || context->state != TB_MD5_ACTIVE)
        return false;
    if (!digest || expected_length != context->length) {
        context->state = TB_MD5_FAILED;
        return false;
    }

    uint64_t bits = context->length * 8;
    context->block[context->used++] = 0x80;
    if (context->used > 56) {
        memset(context->block + context->used, 0, TB_MD5_BLOCK_BYTES - context->used);
        block(context, context->block);
        context->used = 0;
    }
    memset(context->block + context->used, 0, 56 - context->used);
    for (unsigned i = 0; i < 8; ++i)
        context->block[56 + i] = (unsigned char)(bits >> (8U * i));
    block(context, context->block);
    for (unsigned i = 0; i < TB_MD5_DIGEST_BYTES; ++i)
        digest[i] = (unsigned char)(context->words[i / 4] >> (8U * (i % 4)));
    context->state = TB_MD5_FINISHED;

    return true;
}
