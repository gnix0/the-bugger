#ifndef THE_BUGGER_SYMBOL_CRC32_H
#define THE_BUGGER_SYMBOL_CRC32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TB_CRC32_UNINITIALIZED,
    TB_CRC32_ACTIVE,
    TB_CRC32_FAILED,
    TB_CRC32_FINISHED
} TbCrc32State_e;

/* Reflected IEEE CRC-32 for complete GNU debuglink file comparisons. This is
 * neither the RSP qCRC profile nor an artifact identity/security primitive.
 * Caller owns/accounts state and supplies immutable nonaliasing input/output.
 * No allocation, I/O, IPC, file discovery or relationship publication. */
typedef struct {
    uint64_t       bytes;
    uint32_t       accumulator;
    TbCrc32State_e state;
} TbCrc32_t;

bool tb_crc32_init(TbCrc32_t *crc);
/* At most 65536 bytes; NULL only for zero length. Invalid input or total-length
 * overflow poisons an active stream. Outer job owns cancellation/deadlines. */
bool tb_crc32_update(TbCrc32_t *crc, const void *bytes, size_t length);
/* Exactly one finalization, requiring the complete expected file extent.
 * Failure preserves output and poisons an active stream; no prefix CRC. */
bool tb_crc32_final(TbCrc32_t *crc, uint64_t expected_bytes, uint32_t *value);

#endif // THE_BUGGER_SYMBOL_CRC32_H
