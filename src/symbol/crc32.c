#include "symbol/crc32.h"

#include <limits.h>

_Static_assert(CHAR_BIT == 8, "CRC32 requires eight-bit bytes");

bool tb_crc32_init(TbCrc32_t *crc)
{
    if (!crc)
        return false;

    *crc = (TbCrc32_t) {
        .accumulator = UINT32_MAX,
        .state       = TB_CRC32_ACTIVE
    };

    return true;
}


bool tb_crc32_update(TbCrc32_t *crc, const void *bytes, size_t length)
{
    if (!crc || crc->state != TB_CRC32_ACTIVE)
        return false;
    if ((!bytes && length) || length > 65536 || length > UINT64_MAX - crc->bytes) {
        crc->state = TB_CRC32_FAILED;
        return false;
    }

    const unsigned char *input = bytes;
    for (size_t i = 0; i < length; ++i) {
        crc->accumulator ^= input[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t polynomial = (crc->accumulator & 1U) ? UINT32_C(0xedb88320) : 0;
            crc->accumulator = (crc->accumulator >> 1U) ^ polynomial;
        }
    }
    crc->bytes += length;

    return true;
}


bool tb_crc32_final(TbCrc32_t *crc, uint64_t expected_bytes, uint32_t *value)
{
    if (!crc || crc->state != TB_CRC32_ACTIVE)
        return false;
    if (!value || expected_bytes != crc->bytes) {
        crc->state = TB_CRC32_FAILED;
        return false;
    }

    *value = crc->accumulator ^ UINT32_MAX;
    crc->state = TB_CRC32_FINISHED;

    return true;
}
