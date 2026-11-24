#ifndef THE_BUGGER_SYMBOL_COMPANION_RECEIPT_H
#define THE_BUGGER_SYMBOL_COMPANION_RECEIPT_H

#include "symbol/companion_scan.h"

enum {
    TB_SYMBOL_COMPANION_RECEIPT_BYTES = 96
};

/* Private versioned helper payload, explicit big-endian fields; no C ABI.
 * Fixed prefix: extent/key (three u64), debuglink section/ID section/note
 * (three u64), then twelve u32: object type, machine, address width, byte order
 * (1 little/2 big), ELF flags, OS ABI, ABI version, debuglink coverage (0/1),
 * debuglink CRC, complete-file CRC, name length, ID length. Raw name and opaque ID
 * follow without terminators. Both <= schema maxima, name strictly less than
 * PATH_BYTES. Scope/protection/retirement and request generations remain external.
 * Returned inventory text borrows the complete immutable payload. Decoder checks
 * framing/profile consistency, not underlying ELF or the truth of a helper claim.
 * Encode/decode inspect at most 8 KiB; allocation/ownership belong to the caller. */
size_t tb_symbol_companion_receipt_size(const TbSymbolCompanionInventory_t *view);
bool tb_symbol_companion_receipt_encode(const TbSymbolCompanionInventory_t *view, void *bytes, size_t length);
bool tb_symbol_companion_receipt_decode(const void *bytes, size_t length, TbSymbolCompanionInventory_t *view);

#endif // THE_BUGGER_SYMBOL_COMPANION_RECEIPT_H
