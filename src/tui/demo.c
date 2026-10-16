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
    const TbView defaults[TB_PANES][2] = {
        {TB_SOURCE, TB_SYMBOLS}, {TB_DISASSEMBLY, TB_CALL_STACK},
        {TB_REGISTERS, TB_PERIPHERALS}, {TB_MEMORY, TB_STACK},
        {TB_LOCALS, TB_WATCHES}, {TB_CONSOLE, TB_BREAKPOINTS}
    };
    memset(demo, 0, sizeof *demo);
    for (int i = 0; i < TB_PANES; ++i) {
        demo->panes[i].tabs[0] = defaults[i][0];
        demo->panes[i].tabs[1] = defaults[i][1];
        demo->panes[i].count = 2;
    }
    demo->breakpoints[19] = true;
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

bool tb_rect_contains(TbRect rect, int x, int y)
{
    return rect.w > 0 && rect.h > 0 && x >= rect.x && y >= rect.y &&
           x - rect.x < rect.w && y - rect.y < rect.h;
}

bool tb_demo_geometry(TbRect area, int focused, TbRect panes[TB_PANES])
{
    memset(panes, 0, TB_PANES * sizeof panes[0]);
    if (area.w <= 0 || area.h <= 0 || focused < 0 || focused >= TB_PANES) {
        return false;
    }
    if (area.w < 110 || area.h < 20) {
        panes[focused] = area;
        return false;
    }
    int available = area.w - 2;
    int widths[3] = {available * 36 / 100, available * 34 / 100, 0};
    widths[2] = available - widths[0] - widths[1];
    int top = (area.h - 1) * 56 / 100;
    for (int row = 0; row < 2; ++row) {
        int x = area.x;
        for (int col = 0; col < 3; ++col) {
            panes[row * 3 + col] = (TbRect){
                x, area.y + (row == 0 ? 0 : top + 1), widths[col],
                row == 0 ? top : area.h - top - 1
            };
            x += widths[col] + 1;
        }
    }
    return true;
}
