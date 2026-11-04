#ifndef THE_BUGGER_DEMO_H
#define THE_BUGGER_DEMO_H

#include <stdbool.h>
#include "layout/layout.h"

/* Explicitly simulated presentation state. Never use this as target data. */
enum {
    TB_LOGS         = 16,
    TB_SOURCE_LINES = 24
};

typedef enum {
    TB_SOURCE,
    TB_DISASSEMBLY,
    TB_REGISTERS,
    TB_MEMORY,
    TB_LOCALS,
    TB_CONSOLE,
    TB_WATCHES,
    TB_CALL_STACK,
    TB_BREAKPOINTS,
    TB_THREADS,
    TB_SYMBOLS,
    TB_PERIPHERALS,
    TB_STACK,
    TB_VIEW_COUNT
} TbView_e;

typedef enum {
    TB_CONTINUE,
    TB_PAUSE,
    TB_NEXT,
    TB_STEP,
    TB_FINISH,
    TB_BREAKPOINT,
    TB_RESET
} TbDemoAction_e;

typedef struct {
    TbView_e tabs[TB_VIEW_COUNT];
    int      count;
    int      active;
    int      scroll[TB_VIEW_COUNT];
    int      row[TB_VIEW_COUNT];
} TbDemoPane_t;

typedef struct {
    TbDemoPane_t panes[TB_MAX_PANES];
    int          pane;
    int          step;
    bool         running;
    bool         breakpoints[TB_SOURCE_LINES];
    char         logs[TB_LOGS][160];
    int          log_count;
} TbDemo_t;

typedef struct {
    int total;
    int visible;
    int offset;
    int maximum;
    int thumb_start;
    int thumb_size;
} TbScroll_t;

extern const char *const tb_view_names[TB_VIEW_COUNT];
extern const char *const tb_demo_source[TB_SOURCE_LINES];

void tb_demo_init(TbDemo_t *demo);
void tb_demo_action(TbDemo_t *demo, TbDemoAction_e action);
void tb_demo_log(TbDemo_t *demo, const char *message);
void tb_demo_view(TbDemo_t *demo, TbView_e view);
bool tb_demo_has_view(const TbDemoPane_t *pane, TbView_e view);
bool tb_demo_toggle_view(TbDemo_t *demo, TbView_e view);
bool tb_demo_close_tab(TbDemo_t *demo);
int tb_demo_row_count(const TbDemo_t *demo, TbView_e view);
TbScroll_t tb_demo_scrollbar(const TbDemo_t *demo, const TbDemoPane_t *pane, int visible);

#endif // THE_BUGGER_DEMO_H
