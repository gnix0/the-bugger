#ifndef THE_BUGGER_SYMBOL_LINE_H
#define THE_BUGGER_SYMBOL_LINE_H

#include "symbol/dwarf.h"
#include "symbol/leb.h"

enum {
    TB_SYMBOL_LINE_PREFIX_BYTES = 284
};

/* Fixed prologue and standard-opcode table only. entries_offset begins the
 * directory/file descriptions, whose framing and dependencies remain unexamined.
 * Offsets are section-relative. Legacy address_bytes is zero: the caller must
 * supply the exact associated CU width separately, never infer it from the host. */
typedef struct {
    TbSymbolDwarfLength_t extent;
    uint64_t              table_offset;
    uint64_t              entries_offset;
    uint64_t              program_offset;
    TbSymbolByteOrder_e   byte_order;
    unsigned              version;
    unsigned              address_bytes;
    unsigned              segment_bytes;
    unsigned              minimum_instruction;
    unsigned              maximum_operations;
    unsigned              line_range;
    unsigned              opcode_base;
    int                   line_base;
    bool                  default_is_stmt;
    unsigned char         operand_counts[256];
} TbSymbolLineHeader_t;

/* At most 284 prefix bytes; no I/O/allocation, full prologue or CU validation.
 * Failed outputs are unchanged. Framing is independent of DWARF offset format,
 * line-table version and target address width. */
TbSymbolDwarfStatus_e tb_symbol_line_header(TbSymbolDwarfSpan_t span, const void *bytes, size_t length,
                                           TbSymbolLineHeader_t *header);

typedef struct {
    uint64_t address;
    uint64_t operation;
    uint64_t file;
    uint64_t line;
    uint64_t column;
    uint64_t isa;
    uint64_t discriminator;
    uint64_t instruction_offset;
    uint64_t sequence;
    bool     is_stmt;
    bool     basic_block;
    bool     end_sequence;
    bool     prologue_end;
    bool     epilogue_begin;
} TbSymbolLineRow_t;

/* Legacy define_file event: raw spans refer to retained .debug_line bytes.
 * name_bytes excludes the NUL. Ordinal here is event order, not a resolved file
 * index; it must be appended to the independently validated initial file table. */
typedef struct {
    uint64_t instruction_offset;
    uint64_t name_offset;
    uint64_t name_bytes;
    uint64_t directory;
    uint64_t timestamp;
    uint64_t bytes;
} TbSymbolLineFile_t;

typedef enum {
    TB_SYMBOL_LINE_PENDING,
    TB_SYMBOL_LINE_COMPLETE,
    TB_SYMBOL_LINE_INVALID,
    TB_SYMBOL_LINE_MALFORMED,
    TB_SYMBOL_LINE_UNSUPPORTED,
    TB_SYMBOL_LINE_RESOURCE
} TbSymbolLineStatus_e;

typedef enum {
    TB_SYMBOL_LINE_NONE,
    TB_SYMBOL_LINE_FRAMING,
    TB_SYMBOL_LINE_OPERAND,
    TB_SYMBOL_LINE_ADDRESS,
    TB_SYMBOL_LINE_NUMBER,
    TB_SYMBOL_LINE_ORDER,
    TB_SYMBOL_LINE_ROWS,
    TB_SYMBOL_LINE_FILES,
    TB_SYMBOL_LINE_EXTENSION,
    TB_SYMBOL_LINE_SEQUENCE
} TbSymbolLineFailure_e;

typedef struct {
    TbSymbolLineFailure_e reason;
    uint64_t              instruction_offset;
    uint64_t              byte_offset;
    unsigned              opcode;
    unsigned              extended_opcode;
} TbSymbolLineFailure_t;

typedef struct {
    TbSymbolLineHeader_t      header;
    const TbSymbolLineRow_t  *rows;
    const TbSymbolLineFile_t *files;
    size_t                    row_count;
    size_t                    file_count;
    unsigned                  address_bytes;
} TbSymbolLineView_t;

/* Caller owns/accounts state and full array capacities, and retains exact
 * frozen program bytes/prologue/CU evidence through discard. No input pointers
 * are retained, no I/O/allocation/callbacks, and no published prefix. Fields are
 * private working state. Rows are numeric metadata, not source paths, executable
 * range evidence, runtime locations, recommended actions or read permission. */
typedef struct {
    TbSymbolLineView_t    view;
    TbSymbolLineRow_t     registers;
    TbSymbolLineRow_t    *rows;
    TbSymbolLineFile_t   *files;
    TbSymbolLineFile_t    file;
    TbSymbolLeb_t         leb;
    TbSymbolLineFailure_t failure;
    TbSymbolLineStatus_e  status;
    uint64_t              cursor;
    uint64_t              extended_end;
    uint64_t              accumulator;
    uint64_t              remaining;
    size_t                row_capacity;
    size_t                file_capacity;
    unsigned              phase;
    unsigned              opcode;
    unsigned              extended_opcode;
    unsigned              width;
    unsigned              received;
    unsigned              argument;
    bool                  sequence_open;
    bool                  initialized;
} TbSymbolLine_t;

/* Header must come from line_header and remain unchanged. Numeric widths
 * 1/2/4/8 are supported; segmented/wider addressing is explicitly unsupported.
 * For version 5 the supplied width must agree with its own header.
 * Rejected initialization preserves preceding state and arrays. Nonnull arenas
 * are required even at zero capacity; no default capacity or preallocation. */
TbSymbolLineStatus_e tb_symbol_line_init(TbSymbolLine_t *line, TbSymbolLineHeader_t header,
    unsigned address_bytes, TbSymbolLineRow_t *rows, size_t row_capacity,
    TbSymbolLineFile_t *files, size_t file_capacity);
/* At most 256 input bytes; never consumes the next contribution. Completion
 * requires exact program extent, complete operands and terminated sequences.
 * Unknown framed opcodes report UNSUPPORTED, never a silently skipped matrix.
 * Outer jobs own revisions, cancellation, interests and absolute deadlines. */
TbSymbolLineStatus_e tb_symbol_line_feed(TbSymbolLine_t *line, const void *bytes, size_t length,
                                        size_t *consumed);
/* An incomplete input/program/sequence is malformed; no view is available. */
TbSymbolLineStatus_e tb_symbol_line_finish(TbSymbolLine_t *line);
bool tb_symbol_line_view(const TbSymbolLine_t *line, TbSymbolLineView_t *view);
bool tb_symbol_line_failure(const TbSymbolLine_t *line, TbSymbolLineFailure_t *failure);
uint64_t tb_symbol_line_progress(const TbSymbolLine_t *line);
void tb_symbol_line_discard(TbSymbolLine_t *line);

#endif // THE_BUGGER_SYMBOL_LINE_H
