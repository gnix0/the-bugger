#ifndef THE_BUGGER_SETUP_H
#define THE_BUGGER_SETUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <json-c/json.h>

/* Definitions only. No debugger IDs, sampled data or access permissions. */
enum {
    TB_SETUP_PANES    = 32,
    TB_SETUP_NODES    = 64,
    TB_SETUP_VIEWS    = 13,
    TB_SETUP_SOURCES  = 8,
    TB_SETUP_WATCHES  = 64,
    TB_SETUP_BREAKS   = 256,
    TB_SETUP_PATH     = 4096,
    TB_SETUP_LOCATION = 8193
};

typedef struct {
    uint64_t id;
    char     file[TB_SETUP_PATH];
    char     full[TB_SETUP_PATH];
} TbSetupSource_t;

typedef struct {
    bool            used;
    uint64_t        id;
    int             tabs[TB_SETUP_VIEWS];
    int             tab_count;
    int             selected;
    TbSetupSource_t sources[TB_SETUP_SOURCES - 1];
    size_t          source_count;
    uint64_t        source_selected; /* Zero is the stopped-context role. */
    char            memory_expression[256];
    unsigned        memory_bytes;
    unsigned        memory_group;
} TbSetupPane_t;

typedef struct {
    bool used;
    int  pane;
    int  first;
    int  second;
    int  ratio;
    int  direction;
} TbSetupNode_t;

typedef struct {
    uint64_t id;
    char     expression[256];
    int      format;
} TbSetupWatch_t;

typedef struct {
    char     path[TB_SETUP_PATH];
    char     identity[256];
    uint64_t bias;
    uint64_t begin;
    uint64_t end;
} TbSetupMapping_t;

typedef struct {
    uint64_t         id;
    TbSetupMapping_t mapping;
    char             location[TB_SETUP_LOCATION];
    char             source[TB_SETUP_PATH];
    char             condition[512];
    unsigned         line;
    unsigned         ignore;
    bool             enabled;
    bool             ignore_set;
    bool             hardware;
} TbSetupBreak_t;

typedef struct {
    uint64_t       next_id;
    int            columns[3];
    int            row_ratio;
    int            roots[6];
    TbSetupNode_t  nodes[TB_SETUP_NODES];
    TbSetupPane_t  panes[TB_SETUP_PANES];
    TbSetupWatch_t watches[TB_SETUP_WATCHES];
    size_t         watch_count;
    TbSetupBreak_t breaks[TB_SETUP_BREAKS];
    size_t         break_count;
} TbSetup_t;

bool tb_setup_valid(const TbSetup_t *setup);

/* Strict, bounded schema. The caller owns the returned object/model. */
json_object *tb_setup_encode(const TbSetup_t *setup);
TbSetup_t *tb_setup_decode(json_object *object);
bool tb_setup_equal(const TbSetup_t *left, const TbSetup_t *right);

#endif // THE_BUGGER_SETUP_H
