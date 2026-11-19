#ifndef THE_BUGGER_SYMBOL_READER_H
#define THE_BUGGER_SYMBOL_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TB_SYMBOL_LITTLE_ENDIAN,
    TB_SYMBOL_BIG_ENDIAN
} TbSymbolByteOrder_e;

typedef enum {
    TB_SYMBOL_READ_OK,
    TB_SYMBOL_READ_INVALID,
    TB_SYMBOL_READ_TRUNCATED,
    TB_SYMBOL_READ_OVERFLOW,
    TB_SYMBOL_READ_LIMIT
} TbSymbolReadStatus_e;

/* Borrows immutable bytes. Initialize before use; do not modify fields directly.
 * Failure leaves the cursor and output arguments unchanged. Outputs must not
 * alias reader state or input bytes. No allocation, I/O or metadata validation. */
typedef struct {
    const unsigned char *data;
    size_t               length;
    size_t               offset;
    TbSymbolByteOrder_e  byte_order;
} TbSymbolReader_t;

bool tb_symbol_add(uint64_t left, uint64_t right, uint64_t *result);
bool tb_symbol_multiply(uint64_t left, uint64_t right, uint64_t *result);
bool tb_symbol_range(uint64_t offset, uint64_t length, uint64_t extent);
TbSymbolReadStatus_e tb_symbol_reader_init(TbSymbolReader_t *reader, const void *data, size_t length,
                                          TbSymbolByteOrder_e byte_order);
TbSymbolReadStatus_e tb_symbol_reader_bytes(TbSymbolReader_t *reader, size_t length,
                                           const unsigned char **bytes);
/* Only ELF/DWARF fixed integer widths 1, 2, 4 and 8 are accepted. */
TbSymbolReadStatus_e tb_symbol_reader_uint(TbSymbolReader_t *reader, unsigned width, uint64_t *value);
TbSymbolReadStatus_e tb_symbol_reader_sint(TbSymbolReader_t *reader, unsigned width, int64_t *value);
/* LEB128 is limited to a representable 64-bit value and at most ten bytes.
 * Nonminimal encodings are accepted when the value remains representable. */
TbSymbolReadStatus_e tb_symbol_reader_uleb(TbSymbolReader_t *reader, uint64_t *value);
TbSymbolReadStatus_e tb_symbol_reader_sleb(TbSymbolReader_t *reader, int64_t *value);
/* max_bytes includes the terminator. LIMIT means the scan allowance ended;
 * TRUNCATED means input ended first. The text pointer borrows the original bytes. */
TbSymbolReadStatus_e tb_symbol_reader_string(TbSymbolReader_t *reader, size_t max_bytes,
                                            const char **text, size_t *length);
TbSymbolReadStatus_e tb_symbol_reader_slice(TbSymbolReader_t *reader, size_t length,
                                           TbSymbolReader_t *slice);

#endif // THE_BUGGER_SYMBOL_READER_H
