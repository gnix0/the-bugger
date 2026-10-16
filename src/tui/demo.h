#ifndef THE_BUGGER_DEMO_H
#define THE_BUGGER_DEMO_H

#include <stdbool.h>
#include "layout.h"

/* Explicitly simulated presentation state. Never use this as target data. */
enum { TB_LOGS = 16, TB_SOURCE_LINES = 24 };

typedef enum {
    TB_SOURCE, TB_DISASSEMBLY, TB_REGISTERS, TB_MEMORY, TB_LOCALS, TB_CONSOLE,
    TB_WATCHES, TB_CALL_STACK, TB_BREAKPOINTS, TB_THREADS,
    TB_SYMBOLS, TB_PERIPHERALS, TB_STACK, TB_VIEW_COUNT
} TbView;

typedef enum {
    TB_CONTINUE, TB_PAUSE, TB_NEXT, TB_STEP, TB_FINISH, TB_BREAKPOINT, TB_RESET
} TbDemoAction;

typedef struct {
    TbView tabs[TB_VIEW_COUNT];
    int count, active;
    int scroll[TB_VIEW_COUNT];
    int row[TB_VIEW_COUNT];
} TbDemoPane;

typedef struct {
    TbDemoPane panes[TB_MAX_PANES];
    int pane, step;
    bool running;
    bool breakpoints[TB_SOURCE_LINES];
    char logs[TB_LOGS][160];
    int log_count;
} TbDemo;

extern const char *const tb_view_names[TB_VIEW_COUNT];
extern const char *const tb_demo_source[TB_SOURCE_LINES];

void tb_demo_init(TbDemo *demo);
void tb_demo_action(TbDemo *demo, TbDemoAction action);
void tb_demo_log(TbDemo *demo, const char *message);
void tb_demo_view(TbDemo *demo, TbView view);
bool tb_demo_close_tab(TbDemo *demo);

#endif
