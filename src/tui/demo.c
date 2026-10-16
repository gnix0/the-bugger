#include "demo.h"

#include <stdio.h>
#include <string.h>

const char *const tb_view_names[TB_VIEW_COUNT] = {
    "Source", "Disassembly", "Registers", "Memory", "Locals", "Console",
    "Watches", "Call Stack", "Breakpoints", "Threads",
    "Symbols", "Peripherals", "Stack"
};

const char *const tb_demo_source[TB_SOURCE_LINES] = {
    "#include <stdint.h>",
    "#include \"stm32f4xx.h\"",
    "", "#define LED_PIN 5U", "",
    "static volatile uint32_t counter = 0;", "",
    "static void gpio_init(void) {",
    "    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;",
    "    GPIOA->MODER |= (1U << (LED_PIN * 2));",
    "}", "",
    "static void delay(volatile uint32_t n) {",
    "    while (n--) { __NOP(); }", "}", "",
    "int main(void) {", "    gpio_init();",
    "    for (;;) {", "        GPIOA->ODR ^= (1U << LED_PIN);",
    "        counter++;", "        delay(200000);", "    }", "}"
};

void tb_demo_log(TbDemo *demo, const char *message)
{
    if (demo->log_count == TB_LOGS) {
        memmove(demo->logs, demo->logs + 1,
                (TB_LOGS - 1U) * sizeof demo->logs[0]);
        --demo->log_count;
    }
    (void)snprintf(demo->logs[demo->log_count], sizeof demo->logs[0],
                   "DEMO: %s", message);
    ++demo->log_count;
}

void tb_demo_init(TbDemo *demo)
{
    const TbView defaults[TB_DEFAULT_PANES][2] = {
        {TB_SOURCE, TB_SYMBOLS}, {TB_DISASSEMBLY, TB_CALL_STACK},
        {TB_REGISTERS, TB_PERIPHERALS}, {TB_MEMORY, TB_STACK},
        {TB_LOCALS, TB_WATCHES}, {TB_CONSOLE, TB_BREAKPOINTS}
    };
    memset(demo, 0, sizeof *demo);
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        demo->panes[i].tabs[0] = defaults[i][0];
        demo->panes[i].tabs[1] = defaults[i][1];
        demo->panes[i].count = 2;
    }
    demo->panes[0].scroll[TB_SOURCE] = 8;
    demo->panes[0].row[TB_SOURCE] = 17;
    tb_demo_log(demo, "Visual reference only; no GDB or OpenOCD connected.");
    tb_demo_log(demo, "Cortex-M4F sample values; configuration is a preview.");
    tb_demo_log(demo, "Step controls animate the sample, not a target.");
}

void tb_demo_action(TbDemo *demo, TbDemoAction action)
{
    switch (action) {
    case TB_CONTINUE:
        demo->running = true;
        tb_demo_log(demo, "Continue / Running (simulated).");
        break;
    case TB_PAUSE:
        demo->running = false;
        tb_demo_log(demo, "Pause / Paused (simulated).");
        break;
    case TB_NEXT: case TB_STEP: case TB_FINISH:
        demo->running = false;
        demo->step = (demo->step + 1) % 6;
        tb_demo_log(demo, action == TB_NEXT ? "Step over (simulated)." :
                    action == TB_STEP ? "Step into (simulated)." :
                                        "Step out (simulated).");
        break;
    case TB_BREAKPOINT: {
        TbDemoPane *pane = &demo->panes[demo->pane];
        int row = pane->tabs[pane->active] == TB_SOURCE ?
                  pane->row[TB_SOURCE] : 17 + demo->step;
        demo->breakpoints[row] = !demo->breakpoints[row];
        tb_demo_log(demo, "Breakpoint toggled (simulated).");
        break;
    }
    case TB_RESET:
        demo->step = 0;
        demo->running = false;
        tb_demo_log(demo, "Reset sample execution (simulated).");
        break;
    }
}

void tb_demo_view(TbDemo *demo, TbView view)
{
    TbDemoPane *pane = &demo->panes[demo->pane];
    if (view < 0 || view >= TB_VIEW_COUNT) {
        return;
    }
    for (int i = 0; i < pane->count; ++i) {
        if (pane->tabs[i] == view) {
            pane->active = i;
            return;
        }
    }
    pane->tabs[pane->count] = view;
    pane->active = pane->count++;
}

bool tb_demo_close_tab(TbDemo *demo)
{
    TbDemoPane *pane = &demo->panes[demo->pane];
    if (pane->count <= 1) { return false; }
    for (int i = pane->active; i + 1 < pane->count; ++i) {
        pane->tabs[i] = pane->tabs[i + 1];
    }
    --pane->count;
    if (pane->active >= pane->count) { pane->active = pane->count - 1; }
    return true;
}

int tb_demo_row_count(const TbDemo *demo, TbView view)
{
    switch (view) {
    case TB_SOURCE: case TB_BREAKPOINTS: return TB_SOURCE_LINES;
    case TB_DISASSEMBLY: return 9;
    case TB_REGISTERS: return 17;
    case TB_MEMORY: return 32;
    case TB_LOCALS: case TB_WATCHES: return 4;
    case TB_CONSOLE: return demo->log_count;
    case TB_CALL_STACK: return 2;
    case TB_THREADS: return 1;
    case TB_SYMBOLS: case TB_PERIPHERALS: case TB_STACK: return 2;
    default: return 0;
    }
}

TbScroll tb_demo_scrollbar(const TbDemo *demo, const TbDemoPane *pane, int visible)
{
    TbScroll scroll = {.total = tb_demo_row_count(demo, pane->tabs[pane->active]),
                       .visible = visible};
    if (visible <= 0) { return scroll; }
    scroll.maximum = scroll.total > visible ? scroll.total - visible : 0;
    scroll.offset = pane->scroll[pane->tabs[pane->active]];
    if (scroll.offset < 0) { scroll.offset = 0; }
    if (scroll.offset > scroll.maximum) { scroll.offset = scroll.maximum; }
    scroll.thumb_size = scroll.maximum == 0 ? visible : visible * visible / scroll.total;
    if (scroll.thumb_size < 1) { scroll.thumb_size = 1; }
    scroll.thumb_start = scroll.maximum == 0 ? 0 :
                        scroll.offset * (visible - scroll.thumb_size) / scroll.maximum;
    return scroll;
}
