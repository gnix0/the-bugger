#ifndef THE_BUGGER_SYMBOL_LINE_FILES_H
#define THE_BUGGER_SYMBOL_LINE_FILES_H

#include "symbol/form.h"
#include "symbol/line.h"

typedef enum {
    TB_SYMBOL_LINE_FILES_PENDING,
    TB_SYMBOL_LINE_FILES_COMPLETE,
    TB_SYMBOL_LINE_FILES_PARTIAL,
    TB_SYMBOL_LINE_FILES_INVALID,
    TB_SYMBOL_LINE_FILES_MALFORMED,
    TB_SYMBOL_LINE_FILES_CONFLICT,
    TB_SYMBOL_LINE_FILES_UNSUPPORTED,
    TB_SYMBOL_LINE_FILES_RESOURCE
} TbSymbolLineFilesStatus_e;

typedef enum {
    TB_SYMBOL_LINE_FIELD_VALUE,
    TB_SYMBOL_LINE_FIELD_REFERENCE,
    TB_SYMBOL_LINE_FIELD_UNSUPPORTED
} TbSymbolLineFieldProfile_e;

typedef struct {
    uint64_t offset;
    uint64_t content;
    uint64_t form;
} TbSymbolLineFormat_t;

typedef struct {
    uint64_t                   offset;
    uint64_t                   content;
    TbSymbolFormView_t         value;
    TbSymbolLineFieldProfile_e profile;
} TbSymbolLineField_t;

typedef struct {
    uint64_t index;
    uint64_t offset;
    size_t   first_field;
    unsigned field_count;
    bool     directory;
} TbSymbolLineEntry_t;

typedef struct {
    TbSymbolLineHeader_t         header;
    const TbSymbolLineEntry_t   *entries;
    const TbSymbolLineField_t   *fields;
    const TbSymbolLineFormat_t  *directory_format;
    const TbSymbolLineFormat_t  *file_format;
    size_t                      directory_count;
    size_t                      file_count;
    size_t                      field_count;
    unsigned                    directory_formats;
    unsigned                    file_formats;
    bool                        partial;
} TbSymbolLineFilesView_t;

typedef struct {
    TbSymbolLineFilesStatus_e status;
    uint64_t                 offset;
    uint64_t                 content;
    uint64_t                 form;
    uint64_t                 entry_index;
    uint64_t                 field_offset;
    bool                     directory;
    bool                     field_available;
} TbSymbolLineFilesFailure_t;

/* Stationary state, full entry/field arenas and frozen source scope are caller-owned and
 * accounted. Schema arrays cover the on-wire ubyte count, not a production
 * capacity default. Getters borrow state/arenas until discard. No allocation,
 * I/O, recursion, implicit string resolution or local source-file access.
 * State, arenas and frozen input are disjoint; feed arguments do not alias
 * state or output arenas. */
typedef struct {
    TbSymbolLineFilesView_t    view;
    TbSymbolLineEntry_t       *entries;
    TbSymbolLineField_t       *fields;
    TbSymbolLineFormat_t       formats[2][255];
    unsigned                  format_count[2];
    TbSymbolForm_t             form;
    TbSymbolLeb_t              leb;
    TbSymbolLineFilesFailure_t failure;
    TbSymbolLineFilesStatus_e  status;
    uint64_t                  cursor;
    uint64_t                  progress;
    uint64_t                  remaining;
    uint64_t                  ordinal;
    uint64_t                  field_offset;
    size_t                    entry_capacity;
    size_t                    field_capacity;
    unsigned                  table;
    unsigned                  phase;
    unsigned                  format_index;
    unsigned                  scan;
    unsigned                  field_index;
    bool                      has_path;
    bool                      initialized;
} TbSymbolLineFiles_t;

/* Header from line_header. Legacy formats are generated descriptors with
 * offset UINT64_MAX (no encoded schema). Directory/file indexes start at 1 for
 * v2-4, 0 for v5; no implicit legacy compilation-directory entry is invented.
 * Failed initialization preserves state and both arenas. */
TbSymbolLineFilesStatus_e tb_symbol_line_files_init(TbSymbolLineFiles_t *files,
    TbSymbolLineHeader_t header, TbSymbolLineEntry_t *entries, size_t entry_capacity,
    TbSymbolLineField_t *fields, size_t field_capacity);
/* Up to 256 byte/field/schema actions. A call can consume zero bytes while
 * progressing internal work; compare progress before requesting more input.
 * Never consumes the program. Exact variable-prologue extent is required.
 * Outer jobs own revisions, interests, cancellation and absolute deadlines. */
TbSymbolLineFilesStatus_e tb_symbol_line_files_feed(TbSymbolLineFiles_t *files,
    const void *bytes, size_t length, size_t *consumed);
TbSymbolLineFilesStatus_e tb_symbol_line_files_finish(TbSymbolLineFiles_t *files);
/* COMPLETE/PARTIAL only. Every record/field remains physically distinct.
 * Field offset is section-relative; value.payload_offset is relative to that
 * field, unlike the adjusted payload coordinates in DIE attribute views.
 * VALUE is interpreted framing, not source correspondence. REFERENCE requires
 * explicit string dependencies; unsupported framed fields retain raw spans.
 * MD5 is a 16-byte metadata span, never proof that displayed source matches. */
bool tb_symbol_line_files_view(const TbSymbolLineFiles_t *files, TbSymbolLineFilesView_t *view);
bool tb_symbol_line_files_failure(const TbSymbolLineFiles_t *files, TbSymbolLineFilesFailure_t *failure);
uint64_t tb_symbol_line_files_progress(const TbSymbolLineFiles_t *files);
void tb_symbol_line_files_discard(TbSymbolLineFiles_t *files);

#endif // THE_BUGGER_SYMBOL_LINE_FILES_H
