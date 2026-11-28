#ifndef THE_BUGGER_SYMBOL_SHA256_H
#define THE_BUGGER_SYMBOL_SHA256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TB_SHA256_DIGEST_BYTES = 32,
    TB_SHA256_BLOCK_BYTES  = 64
};

typedef enum {
    TB_SHA256_UNINITIALIZED,
    TB_SHA256_ACTIVE,
    TB_SHA256_FAILED,
    TB_SHA256_FINISHED
} TbSha256State_e;

/* Caller-owned implementation state; initialize before use, do not modify.
 * No allocation, file I/O or artifact/IPC dependency. Input/output must not
 * alias context storage. The digest is a content candidate, not coherence,
 * byte-equality, runtime correspondence or authorization evidence. */
typedef struct {
    uint32_t        words[8];
    uint64_t        length;
    unsigned char   block[TB_SHA256_BLOCK_BYTES];
    size_t          used;
    TbSha256State_e state;
} TbSha256_t;

bool tb_sha256_init(TbSha256_t *context);
/* NULL data is accepted only for zero length. Input/length failure poisons an
 * active stream; finalization cannot publish its successfully hashed prefix. */
bool tb_sha256_update(TbSha256_t *context, const void *data, size_t length);
/* expected_length must equal the complete intended byte count. A mismatch
 * poisons the stream. Failure leaves digest unchanged. Finalization is once
 * only; initialize again to reuse. NULL output poisons an active stream. */
bool tb_sha256_final(TbSha256_t *context, uint64_t expected_length,
                     unsigned char digest[TB_SHA256_DIGEST_BYTES]);

#endif // THE_BUGGER_SYMBOL_SHA256_H
