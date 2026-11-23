#ifndef THE_BUGGER_SYMBOL_MD5_H
#define THE_BUGGER_SYMBOL_MD5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TB_MD5_DIGEST_BYTES = 16,
    TB_MD5_BLOCK_BYTES  = 64
};

typedef enum {
    TB_MD5_UNINITIALIZED,
    TB_MD5_ACTIVE,
    TB_MD5_FAILED,
    TB_MD5_FINISHED
} TbMd5State_e;

/* Streaming RFC 1321 for recorded DWARF source digests only. MD5 is not the
 * artifact fingerprint or a security/integrity primitive. No allocation/I/O.
 * Caller owns/accounts state, retains exact source bytes and binds any resulting
 * match to that immutable byte selection and revision; a digest alone proves
 * neither source provenance nor permission. Input/output do not alias state. */
typedef struct {
    uint32_t      words[4];
    uint64_t      length;
    unsigned char block[TB_MD5_BLOCK_BYTES];
    size_t        used;
    TbMd5State_e  state;
} TbMd5_t;

bool tb_md5_init(TbMd5_t *context);
/* Each call accepts at most 65536 bytes. Invalid input/length poisons an active
 * stream; no digest of its successfully hashed prefix can be published. Outer
 * jobs own cancellation, deadlines and progress. NULL only for zero length. */
bool tb_md5_update(TbMd5_t *context, const void *bytes, size_t length);
/* One finalization only; exact complete expected_length required. Failure
 * preserves the output and poisons an active stream. Reinitialize before reuse. */
bool tb_md5_final(TbMd5_t *context, uint64_t expected_length, unsigned char digest[TB_MD5_DIGEST_BYTES]);

#endif // THE_BUGGER_SYMBOL_MD5_H
