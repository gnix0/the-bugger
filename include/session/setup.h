#ifndef THE_BUGGER_SETUP_H
#define THE_BUGGER_SETUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <json-c/json.h>

/* Definitions only. No debugger IDs, sampled data or access permissions. */
enum { TB_SETUP_PANES = 32, TB_SETUP_NODES = 64, TB_SETUP_VIEWS = 13,
       TB_SETUP_SOURCES = 8, TB_SETUP_WATCHES = 64, TB_SETUP_BREAKS = 256,
       TB_SETUP_PATH = 4096, TB_SETUP_LOCATION = 8193 };
typedef struct { uint64_t id; char file[TB_SETUP_PATH], full[TB_SETUP_PATH]; } TbSetupSource;
typedef struct {
    bool used;
    uint64_t id;
    int tabs[TB_SETUP_VIEWS], tab_count, selected;
    TbSetupSource sources[TB_SETUP_SOURCES - 1]; size_t source_count;
    uint64_t source_selected; /* Zero is the stopped-context role. */
    char memory_expression[256]; unsigned memory_bytes, memory_group;
} TbSetupPane;
typedef struct { bool used; int pane, first, second, ratio, direction; } TbSetupNode;
typedef struct { uint64_t id; char expression[256]; int format; } TbSetupWatch;
typedef struct {
    char path[TB_SETUP_PATH], identity[256];
    uint64_t bias, begin, end;
} TbSetupMapping;
typedef struct {
    uint64_t id;
    TbSetupMapping mapping;
    char location[TB_SETUP_LOCATION], source[TB_SETUP_PATH], condition[512];
    unsigned line, ignore;
    bool enabled, ignore_set, hardware;
} TbSetupBreak;
typedef struct {
    uint64_t next_id;
    int columns[3], row_ratio, roots[6];
    TbSetupNode nodes[TB_SETUP_NODES];
    TbSetupPane panes[TB_SETUP_PANES];
    TbSetupWatch watches[TB_SETUP_WATCHES]; size_t watch_count;
    TbSetupBreak breaks[TB_SETUP_BREAKS]; size_t break_count;
} TbSetup;

bool tb_setup_valid(const TbSetup *setup);
/* Strict, bounded schema. The caller owns the returned object/model. */
json_object *tb_setup_encode(const TbSetup *setup);
TbSetup *tb_setup_decode(json_object *object);
bool tb_setup_equal(const TbSetup *left, const TbSetup *right);

#endif
