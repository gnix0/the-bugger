#ifndef THE_BUGGER_SYMBOL_SOURCE_FILE_H
#define THE_BUGGER_SYMBOL_SOURCE_FILE_H

#include "symbol/line_files.h"
#include "symbol/line_reference.h"
#include "symbol/strings.h"

typedef enum {
    TB_SYMBOL_SOURCE_FILE_ROW,
    TB_SYMBOL_SOURCE_FILE_DECLARATION,
    TB_SYMBOL_SOURCE_FILE_CALL,
    TB_SYMBOL_SOURCE_FILE_INDEX
} TbSymbolSourceFileKind_e;

typedef struct {
    TbSymbolSourceFileKind_e kind;
    uint64_t                 index;
} TbSymbolSourceFileSelection_t;

typedef struct {
    uint64_t line;
    uint64_t strings;
    uint64_t line_strings;
} TbSymbolSourceSections_t;

typedef enum {
    TB_SYMBOL_SOURCE_TEXT_READY,
    TB_SYMBOL_SOURCE_TEXT_ABSENT,
    TB_SYMBOL_SOURCE_TEXT_DEPENDENCY,
    TB_SYMBOL_SOURCE_TEXT_UNSUPPORTED
} TbSymbolSourceTextStatus_e;

typedef struct {
    TbSymbolStringSelection_t  selection;
    TbSymbolLineField_t        field;
    TbSymbolSourceTextStatus_e status;
} TbSymbolSourceTextRef_t;

typedef struct {
    TbSymbolLineReferenceView_t   reference;
    TbSymbolSourceFileSelection_t selection;
    TbSymbolSourceTextRef_t       path;
    TbSymbolSourceTextRef_t       directory;
    TbSymbolSourceTextRef_t       compilation;
    TbSymbolLineFile_t            event;
    uint64_t                      line_section;
    uint64_t                      file_index;
    uint64_t                      file_offset;
    uint64_t                      directory_index;
    uint64_t                      timestamp;
    uint64_t                      bytes;
    uint64_t                      md5_offset;
    bool                          dynamic;
    bool                          directory_available;
    bool                          directory_current;
    bool                          timestamp_available;
    bool                          bytes_available;
    bool                          md5_available;
    bool                          partial;
} TbSymbolSourceFileView_t;

typedef enum {
    TB_SYMBOL_SOURCE_FILE_PENDING,
    TB_SYMBOL_SOURCE_FILE_FOUND,
    TB_SYMBOL_SOURCE_FILE_ABSENT,
    TB_SYMBOL_SOURCE_FILE_INVALID,
    TB_SYMBOL_SOURCE_FILE_MALFORMED,
    TB_SYMBOL_SOURCE_FILE_UNSUPPORTED
} TbSymbolSourceFileStatus_e;

/* Caller-owned/accounted state, borrowing complete same-content unit reference,
 * prologue/program and directory through discard. Section roles are explicit.
 * ROW index selects a row ordinal; other kinds select an encoded file number.
 * Declaration/call zero is unspecified even in v5; row/explicit zero is a real
 * v5 entry. End-sequence rows have no usable source file. Dynamic legacy entries
 * are appended, and a row cannot select a later define_file event. No I/O,
 * allocation, basename lookup, path construction or local source access. */
typedef struct {
    TbSymbolSourceFileView_t   view;
    TbSymbolSectionsView_t     directory;
    TbSymbolLineFilesView_t    files;
    TbSymbolSourceSections_t   sections;
    size_t                     cursor;
    size_t                     end;
    unsigned                   phase;
    TbSymbolSourceFileStatus_e status;
    bool                       initialized;
} TbSymbolSourceFile_t;

TbSymbolSourceFileStatus_e tb_symbol_source_file_init(TbSymbolSourceFile_t *file,
    TbSymbolSectionsView_t directory, TbSymbolLineReferenceView_t reference,
    TbSymbolLineFilesView_t files, TbSymbolLineView_t program, TbSymbolSourceSections_t sections,
    TbSymbolSourceFileSelection_t selection);
/* 1..256 selected fields per call. Exact initial entry/directory lookup is O(1).
 * Missing string sections remain dependencies; framed unsupported path forms and
 * unavailable optional metadata retain their own status without guessed bytes.
 * Outer owners retain revisions, cancellation, interests and deadlines. */
TbSymbolSourceFileStatus_e tb_symbol_source_file_step(TbSymbolSourceFile_t *file, size_t max_steps);
/* FOUND only. READY string requests have no descriptor: explicitly assign the
 * retained descriptor before using the existing bounded string reader. Inline
 * spans include their terminator bound; table references use the selected section
 * bound. Keep the containing field and unit/line provenance, not just the text.
 * MD5 is a raw 16-byte metadata span, not source-correspondence proof. Numeric
 * timestamp/size availability records field presence; zero remains unknown. */
bool tb_symbol_source_file_view(const TbSymbolSourceFile_t *file, TbSymbolSourceFileView_t *view);
void tb_symbol_source_file_discard(TbSymbolSourceFile_t *file);

#endif // THE_BUGGER_SYMBOL_SOURCE_FILE_H
