#ifndef THE_BUGGER_SYMBOL_COMPANION_METADATA_H
#define THE_BUGGER_SYMBOL_COMPANION_METADATA_H

#include "symbol/storage.h"
#include "symbol/reader.h"

typedef enum {
    TB_SYMBOL_COMPANION_METADATA_PENDING,
    TB_SYMBOL_COMPANION_METADATA_FOUND,
    TB_SYMBOL_COMPANION_METADATA_ABSENT,
    TB_SYMBOL_COMPANION_METADATA_CONFLICT,
    TB_SYMBOL_COMPANION_METADATA_MALFORMED,
    TB_SYMBOL_COMPANION_METADATA_UNSUPPORTED,
    TB_SYMBOL_COMPANION_METADATA_RESOURCE,
    TB_SYMBOL_COMPANION_METADATA_INVALID
} TbSymbolCompanionMetadataStatus_e;

typedef struct {
    TbSymbolStorageInfo_t backing;
    uint64_t             section_index;
    uint64_t             bytes;
    TbSymbolByteOrder_e  byte_order;
} TbSymbolCompanionScope_t;

typedef struct {
    TbSymbolCompanionScope_t scope;
    const unsigned char    *name;
    size_t                  name_bytes;
    uint64_t                crc_offset;
    uint32_t                crc;
} TbSymbolDebuglinkView_t;

/* Pure stationary caller-owned/accounted state and complete basename buffer.
 * Scope is exactly a validated same-content uncompressed .gnu_debuglink section;
 * caller proves section role, extent and retained content lease. No allocation,
 * I/O, discovery, CRC computation or attachment. Buffers/state/input disjoint. */
typedef struct {
    TbSymbolDebuglinkView_t            view;
    unsigned char                    *name;
    size_t                            capacity;
    uint64_t                          cursor;
    uint64_t                          accumulator;
    unsigned                          phase;
    unsigned                          received;
    TbSymbolCompanionMetadataStatus_e status;
    bool                              unsupported_name;
    bool                              initialized;
} TbSymbolDebuglink_t;

/* capacity includes NUL, no production default. Invalid init preserves state. */
TbSymbolCompanionMetadataStatus_e tb_symbol_debuglink_init(TbSymbolDebuglink_t *link,
    TbSymbolCompanionScope_t scope, unsigned char *name, size_t capacity);
/* At most 256 bytes; consumed may be zero on terminal status. A POSIX basename
 * is required: nonempty, not . or .., no slash/NUL/CR/LF within its bytes.
 * Well-framed unsafe names are UNSUPPORTED after complete framing, not guessed
 * or reported as absent. Padding is opaque; CRC uses executable byte order.
 * Complete section extent required; trailing/short data cannot yield a view. */
TbSymbolCompanionMetadataStatus_e tb_symbol_debuglink_feed(TbSymbolDebuglink_t *link,
    const void *bytes, size_t length, size_t *consumed);
TbSymbolCompanionMetadataStatus_e tb_symbol_debuglink_finish(TbSymbolDebuglink_t *link);
bool tb_symbol_debuglink_view(const TbSymbolDebuglink_t *link, TbSymbolDebuglinkView_t *view);
void tb_symbol_debuglink_discard(TbSymbolDebuglink_t *link);

typedef struct {
    TbSymbolCompanionScope_t scope;
    const unsigned char    *bytes;
    size_t                  length;
    uint64_t                note_offset;
    uint64_t                descriptor_offset;
    unsigned                alignment;
} TbSymbolBuildIdView_t;

/* Selected whole SHT_NOTE section. All notes are framed before publication;
 * exact GNU\0/type 3 is recognized regardless of section name. Other vendors
 * and note types are structurally skipped with checked boundaries. Duplicate
 * GNU IDs conflict even when equal. No hash/algorithm interpretation, content
 * equivalence, program-header discovery, admission or runtime claim follows.
 * Caller retains the validated section, its alignment profile and leases.
 * State and full ID capacity are caller-owned/accounted, with no default. */
typedef struct {
    TbSymbolBuildIdView_t              view;
    unsigned char                    *identifier;
    size_t                            capacity;
    uint64_t                          cursor;
    uint64_t                          note_offset;
    uint64_t                          name_end;
    uint64_t                          descriptor_offset;
    uint64_t                          descriptor_end;
    uint64_t                          note_end;
    unsigned char                     header[12];
    unsigned char                     owner[4];
    unsigned                          received;
    unsigned                          alignment;
    unsigned                          phase;
    uint32_t                          name_bytes;
    uint32_t                          descriptor_bytes;
    uint32_t                          type;
    TbSymbolCompanionMetadataStatus_e status;
    bool                              selected;
    bool                              found;
    bool                              initialized;
} TbSymbolBuildId_t;

/* Explicit 4/8-byte note alignment only; other profiles are UNSUPPORTED without
 * guessing. Names start immediately after the 12-byte header; descriptor/next
 * entry start at the next selected alignment boundary. Invalid init unchanged. */
TbSymbolCompanionMetadataStatus_e tb_symbol_build_id_init(TbSymbolBuildId_t *id,
    TbSymbolCompanionScope_t scope, unsigned alignment, unsigned char *identifier, size_t capacity);
/* At most 256 bytes, including opaque note bytes and padding. No usable prefix
 * before the entire section finishes. Caller owns cancellation/deadlines. */
TbSymbolCompanionMetadataStatus_e tb_symbol_build_id_feed(TbSymbolBuildId_t *id,
    const void *bytes, size_t length, size_t *consumed);
TbSymbolCompanionMetadataStatus_e tb_symbol_build_id_finish(TbSymbolBuildId_t *id);
bool tb_symbol_build_id_view(const TbSymbolBuildId_t *id, TbSymbolBuildIdView_t *view);
void tb_symbol_build_id_discard(TbSymbolBuildId_t *id);

#endif // THE_BUGGER_SYMBOL_COMPANION_METADATA_H
