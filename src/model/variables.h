#ifndef THE_BUGGER_VARIABLES_H
#define THE_BUGGER_VARIABLES_H
#include "context.h"
#include "memory.h"

enum { TB_VARIABLE_NODES = 256, TB_VARIABLE_ROOTS = 64,
       TB_VARIABLE_CHILDREN = 32, TB_VARIABLE_DEPTH = 12 };
typedef enum { TB_VARIABLE_LOCALS, TB_VARIABLE_WATCHES, TB_VARIABLE_VIEWS } TbVariableView;
typedef enum { TB_VARIABLE_NATURAL, TB_VARIABLE_HEX, TB_VARIABLE_DECIMAL,
               TB_VARIABLE_OCTAL, TB_VARIABLE_BINARY, TB_VARIABLE_FORMATS } TbVariableFormat;
typedef struct {
    char name[256], type[192], value[512];
    unsigned depth, children, first;
    bool expanded, fresh, changed, available, limited, format_pending;
    TbVariableFormat format;
} TbVariable;
typedef enum { TB_VARIABLE_CREATE, TB_VARIABLE_EVALUATE, TB_VARIABLE_EXPAND } TbVariableRead;
typedef enum { TB_VARIABLE_UNKNOWN, TB_VARIABLE_ORDINARY, TB_VARIABLE_SIDE_EFFECT } TbVariableRegionKind;
typedef struct { uint64_t begin, end; TbVariableRegionKind kind; } TbVariableRegion;
enum { TB_VARIABLE_READ_SPANS = 8 };
typedef struct {
    struct { uint64_t address, bytes; } spans[TB_VARIABLE_READ_SPANS];
    size_t count;
} TbVariableAccess;
/* Optional adapter authorization, BEFORE any value-producing MI request.
 * True must attest the complete operation's reads, not just the expression's
 * apparent base address. Missing/incomplete evidence means false (unknown).
 * A readable target range or absence of 'volatile' is NOT enough. The default
 * adapter supplies no such proof: values require deliberate Read/expansion.
 * No callback may read target memory in order to establish this proof. */
typedef bool (*TbVariableAuthorize)(void *user, TbGdbGeneration generation,
                                   const char *expression, TbVariableRead operation,
                                   unsigned first, unsigned count, TbVariableAccess *access);
typedef struct TbVariables TbVariables;
typedef struct { uint64_t identity; char expression[256]; TbVariableFormat format; } TbWatchDefinition;
/* Root definitions only; no values, permissions or backend objects. */
size_t tb_variables_definitions(const TbVariables *variables);
bool tb_variables_definition(const TbVariables *variables, size_t index, TbWatchDefinition *definition);
TbVariables *tb_variables_create(TbGdb *gdb, TbContext *context);
/* Destroy/reset after session shutdown; live root cleanup runs in pump. */
void tb_variables_destroy(TbVariables *variables);
void tb_variables_reset(TbVariables *variables);
void tb_variables_authorizer(TbVariables *variables, TbVariableAuthorize authorize, void *user);
/* Ordinary is an affirmative no-side-effects classification, not just readable.
 * Unknown/side-effect overlays take precedence. Metadata is adapter-supplied. */
void tb_variables_regions(TbVariables *variables, const TbVariableRegion *regions, size_t count);
void tb_variables_visible(TbVariables *variables, bool locals, bool watches);
void tb_variables_begin(TbVariables *variables);
void tb_variables_want(TbVariables *variables, TbVariableView view, size_t first, size_t count);
size_t tb_variables_count(const TbVariables *variables, TbVariableView view);
const TbVariable *tb_variables_row(const TbVariables *variables, TbVariableView view, size_t row);
/* Explicit actions grant this operation in the current stopped context only. */
bool tb_variables_read(TbVariables *variables, TbVariableView view, size_t row);
bool tb_variables_toggle(TbVariables *variables, TbVariableView view, size_t row);
bool tb_variables_page(TbVariables *variables, TbVariableView view, size_t row, int direction);
bool tb_variables_format(TbVariables *variables, TbVariableView view, size_t row);
bool tb_variables_watch(TbVariables *variables, const char *expression);
/* Fresh root definition only, preserving format without granting a read. */
bool tb_variables_restore_watch(TbVariables *variables, const char *expression, TbVariableFormat format);
bool tb_variables_remove(TbVariables *variables, size_t row);
bool tb_variables_remove_definition(TbVariables *variables, uint64_t identity);
bool tb_variables_names(TbVariables *variables);
bool tb_variables_stale(const TbVariables *variables, TbVariableView view);
bool tb_variables_limited(const TbVariables *variables, TbVariableView view);
const char *tb_variables_error(const TbVariables *variables, TbVariableView view);
const char *tb_variables_format_name(TbVariableFormat format);
bool tb_variables_event(TbVariables *variables, const TbGdbEvent *event);
void tb_variables_pump(TbVariables *variables);
uint64_t tb_variables_revision(const TbVariables *variables);
#endif
