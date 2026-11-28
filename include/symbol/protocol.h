#ifndef THE_BUGGER_SYMBOL_PROTOCOL_H
#define THE_BUGGER_SYMBOL_PROTOCOL_H

#include "symbol/schedule.h"
#include "symbol/wire.h"

/* Internal payload schema version is covered by the enclosing wire version.
 * Reads/writes require a checked fixed-width payload span and a width of 1–8. */
enum {
    TB_SYMBOL_HELLO_BYTES      = 40,
    TB_SYMBOL_REQUEST_BYTES    = 24,
    TB_SYMBOL_PROGRESS_BYTES   = 32,
    TB_SYMBOL_RESULT_BYTES     = 16,
    TB_SYMBOL_CANCEL_BYTES     = 8,
    TB_SYMBOL_SETTLED_BYTES    = 8,
    TB_SYMBOL_DIAGNOSTIC_BYTES = 40,
    TB_SYMBOL_OFFER_BYTES      = 32,
    TB_SYMBOL_GRANT_BYTES      = 16,
    TB_SYMBOL_PROGRESS_START   = 1,
    TB_SYMBOL_PROGRESS_ADVANCE = 2
};

enum {
    TB_SYMBOL_OPERATION_ACQUIRE = 65536,
    TB_SYMBOL_OPERATION_CREATE_STORAGE,
    TB_SYMBOL_OPERATION_REMOVE_FILE,
    TB_SYMBOL_OPERATION_COMPARE,
    TB_SYMBOL_OPERATION_REMOVE_STORAGE,
    TB_SYMBOL_OPERATION_DISCARD_EMPTY,
    TB_SYMBOL_OPERATION_ELF_VALIDATE,
    TB_SYMBOL_OPERATION_COMPANION_INSPECT
};

#define TB_SYMBOL_DETAIL_CREATED       UINT32_C(65536)
#define TB_SYMBOL_DETAIL_HAS_KEY       UINT32_C(131072)
#define TB_SYMBOL_DETAIL_NAMESPACE     UINT32_C(256)
#define TB_SYMBOL_DETAIL_COMPARE       UINT32_C(512)
#define TB_SYMBOL_DETAIL_PUBLICATION   UINT32_C(768)
#define TB_SYMBOL_DETAIL_ELF_OPEN      UINT32_C(1024)
#define TB_SYMBOL_DETAIL_ELF_SECTIONS  UINT32_C(1280)
#define TB_SYMBOL_DETAIL_ELF_SYMBOLS   UINT32_C(1536)
#define TB_SYMBOL_DETAIL_ELF_DISABLED  UINT32_C(1792)
#define TB_SYMBOL_DETAIL_ELF_SELECTION UINT32_C(2048)

#define TB_SYMBOL_DETAIL_COMPANION          UINT32_C(2304)
#define TB_SYMBOL_DETAIL_COMPANION_SECTIONS UINT32_C(2560)
#define TB_SYMBOL_DETAIL_COMPANION_READ     UINT32_C(2816)

static inline bool tb_symbol_protocol_owned(uint32_t operation)
{
    return operation >= TB_SYMBOL_OPERATION_ACQUIRE && operation <= TB_SYMBOL_OPERATION_COMPANION_INSPECT;
}


static inline void tb_symbol_protocol_put(unsigned char *bytes, uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i)
        bytes[i] = (unsigned char)(value >> ((width - i - 1) * 8));
}


static inline uint64_t tb_symbol_protocol_get(const unsigned char *bytes, unsigned width)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value = (value << 8U) | bytes[i];
    return value;
}


static inline TbSymbolMessage_t tb_symbol_protocol_message(TbSymbolMessageKind_e kind,
                                                           TbSymbolWorkIdentity_t identity,
                                                           uint64_t generation,
                                                           const unsigned char *bytes, size_t length)
{
    return (TbSymbolMessage_t){
        .kind       = kind,
        .request    = identity.request,
        .consumer   = identity.consumer,
        .revision   = identity.revision,
        .generation = generation,
        .payload    = bytes,
        .length     = length,
        .descriptor = -1
    };
}

#endif // THE_BUGGER_SYMBOL_PROTOCOL_H
