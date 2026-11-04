#ifndef THE_BUGGER_VARIABLES_H
#define THE_BUGGER_VARIABLES_H
#include "model/context.h"
#include "model/memory.h"

enum {
    TB_VARIABLE_NODES    = 256,
    TB_VARIABLE_ROOTS    = 64,
    TB_VARIABLE_CHILDREN = 32,
    TB_VARIABLE_DEPTH    = 12
};

typedef enum {
    TB_VARIABLE_LOCALS,
    TB_VARIABLE_WATCHES,
    TB_VARIABLE_VIEWS
} TbVariableView_e;

typedef enum {
    TB_VARIABLE_NATURAL,
    TB_VARIABLE_HEX,
    TB_VARIABLE_DECIMAL,
    TB_VARIABLE_OCTAL,
    TB_VARIABLE_BINARY,
    TB_VARIABLE_FORMATS
} TbVariableFormat_e;

typedef struct {
    char               name[256];
    char               type[192];
    char               value[512];
    unsigned           depth;
    unsigned           children;
    unsigned           first;
    bool               expanded;
    bool               fresh;
    bool               changed;
    bool               available;
    bool               limited;
    bool               format_pending;
    TbVariableFormat_e format;
} TbVariable_t;

typedef enum {
    TB_VARIABLE_CREATE,
    TB_VARIABLE_EVALUATE,
    TB_VARIABLE_EXPAND
} TbVariableRead_e;

typedef enum {
    TB_VARIABLE_UNKNOWN,
    TB_VARIABLE_ORDINARY,
    TB_VARIABLE_SIDE_EFFECT
} TbVariableRegionKind_e;

typedef struct {
    uint64_t               begin;
    uint64_t               end;
    TbVariableRegionKind_e kind;
    bool                   writable;
} TbVariableRegion_t;

enum {
    TB_VARIABLE_READ_SPANS = 8
};

typedef struct {
    struct {
        uint64_t address;
        uint64_t bytes;
    } spans[TB_VARIABLE_READ_SPANS];
    size_t count;
} TbVariableAccess_t;

/* Optional adapter authorization, BEFORE any value-producing MI request.
 * True must attest the complete operation's reads, not just the expression's
 * apparent base address. Missing/incomplete evidence means false (unknown).
 * A readable target range or absence of 'volatile' is NOT enough. The default
 * adapter supplies no such proof: values require deliberate Read/expansion.
 * No callback may read target memory in order to establish this proof. */
typedef bool (*tb_variable_authorize_fn)(void *user, TbGdbGeneration_t generation, const char *expression,
                                        TbVariableRead_e operation, unsigned first, unsigned count,
                                        TbVariableAccess_t *access);
typedef struct tb_variables TbVariables_t;

typedef struct {
    uint64_t           identity;
    char               expression[256];
    TbVariableFormat_e format;
} TbWatchDefinition_t;

typedef enum {
    TB_VARIABLE_EDIT_NONE,
    TB_VARIABLE_EDIT_PROBING,
    TB_VARIABLE_EDIT_READY,
    TB_VARIABLE_EDIT_PENDING,
    TB_VARIABLE_EDIT_ACKNOWLEDGED,
    TB_VARIABLE_EDIT_UNCERTAIN
} TbVariableEditState_e;

/* Root definitions only; no values, permissions or backend objects. */
size_t tb_variables_definitions(const TbVariables_t *variables);
bool tb_variables_definition(const TbVariables_t *variables, size_t index, TbWatchDefinition_t *definition);
TbVariables_t *tb_variables_create(TbGdb_t *gdb, TbContext_t *context);

/* Destroy/reset after session shutdown; live root cleanup runs in pump. */
void tb_variables_destroy(TbVariables_t *variables);
void tb_variables_reset(TbVariables_t *variables);
void tb_variables_authorizer(TbVariables_t *variables, tb_variable_authorize_fn authorize, void *user);

/* Ordinary is an affirmative no-side-effects classification, not just readable.
 * Unknown/side-effect overlays take precedence. Metadata is adapter-supplied. */
void tb_variables_regions(TbVariables_t *variables, const TbVariableRegion_t *regions, size_t count);
void tb_variables_target(TbVariables_t *variables, bool octets);
void tb_variables_visible(TbVariables_t *variables, bool locals, bool watches);
void tb_variables_begin(TbVariables_t *variables);
void tb_variables_want(TbVariables_t *variables, TbVariableView_e view, size_t first, size_t count);
size_t tb_variables_count(const TbVariables_t *variables, TbVariableView_e view);
const TbVariable_t *tb_variables_row(const TbVariables_t *variables, TbVariableView_e view, size_t row);

/* Explicit actions grant this operation in the current stopped context only. */
bool tb_variables_read(TbVariables_t *variables, TbVariableView_e view, size_t row);
bool tb_variables_toggle(TbVariables_t *variables, TbVariableView_e view, size_t row);
bool tb_variables_page(TbVariables_t *variables, TbVariableView_e view, size_t row, int direction);
bool tb_variables_format(TbVariables_t *variables, TbVariableView_e view, size_t row);
bool tb_variables_watch(TbVariables_t *variables, const char *expression);

/* Fresh root definition only, preserving format without granting a read. */
bool tb_variables_restore_watch(TbVariables_t *variables, const char *expression, TbVariableFormat_e format);
bool tb_variables_remove(TbVariables_t *variables, size_t row);
bool tb_variables_remove_definition(TbVariables_t *variables, uint64_t identity);
bool tb_variables_names(TbVariables_t *variables);
bool tb_variables_edit_begin(TbVariables_t *variables, TbVariableView_e view, size_t row);
bool tb_variables_edit_apply(TbVariables_t *variables, const char *value);
void tb_variables_edit_cancel(TbVariables_t *variables);
bool tb_variables_edit_reconcile(TbVariables_t *variables);
TbVariableEditState_e tb_variables_edit_state(const TbVariables_t *variables);
const char *tb_variables_edit_error(const TbVariables_t *variables);
const char *tb_variables_edit_old_value(const TbVariables_t *variables);
const char *tb_variables_edit_type(const TbVariables_t *variables);
bool tb_variables_settled(const TbVariables_t *variables, TbVariableView_e view);
bool tb_variables_stale(const TbVariables_t *variables, TbVariableView_e view);
bool tb_variables_limited(const TbVariables_t *variables, TbVariableView_e view);
const char *tb_variables_error(const TbVariables_t *variables, TbVariableView_e view);
const char *tb_variables_format_name(TbVariableFormat_e format);
bool tb_variables_event(TbVariables_t *variables, const TbGdbEvent_t *event);
void tb_variables_pump(TbVariables_t *variables);
uint64_t tb_variables_revision(const TbVariables_t *variables);
#endif // THE_BUGGER_VARIABLES_H
