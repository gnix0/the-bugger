#include "demo.h"
#include "../support/check.h"

int main(void)
{
    TbDemo demo;
    tb_demo_init(&demo);
    for (int i = 0; i < TB_SOURCE_LINES; ++i) { CHECK(!demo.breakpoints[i]); }
    for (int height = 1; height <= 30; ++height) {
        TbScroll scroll = tb_demo_scrollbar(&demo, &demo.panes[0], height);
        CHECK(scroll.offset >= 0 && scroll.offset <= scroll.maximum);
        CHECK(scroll.thumb_size >= 1 && scroll.thumb_size <= height);
        CHECK(scroll.thumb_start >= 0 && scroll.thumb_start + scroll.thumb_size <= height);
    }
    CHECK(demo.panes[0].tabs[0] == TB_SOURCE);
    CHECK(demo.panes[5].tabs[0] == TB_CONSOLE);
    tb_demo_action(&demo, TB_CONTINUE);
    CHECK(demo.running);
    tb_demo_action(&demo, TB_PAUSE);
    CHECK(!demo.running);
    const TbDemoAction steps[] = {TB_NEXT, TB_STEP, TB_FINISH};
    for (int i = 0; i < 3; ++i) {
        tb_demo_action(&demo, steps[i]);
        CHECK(demo.step == i + 1);
        CHECK(!demo.running);
    }
    bool before = demo.breakpoints[17];
    tb_demo_action(&demo, TB_BREAKPOINT);
    CHECK(demo.breakpoints[17] != before);
    tb_demo_action(&demo, TB_BREAKPOINT);
    CHECK(demo.breakpoints[17] == before);
    tb_demo_action(&demo, TB_RESET);
    CHECK(demo.step == 0 && !demo.running);
    for (int i = 0; i < 100; ++i) {
        tb_demo_action(&demo, TB_NEXT);
        CHECK(demo.step >= 0 && demo.step < 6);
    }
    CHECK(demo.log_count == TB_LOGS);
    for (int i = 0; i < TB_VIEW_COUNT; ++i) { tb_demo_view(&demo, (TbView)i); }
    CHECK(demo.panes[0].count == TB_VIEW_COUNT);
    tb_demo_view(&demo, TB_SOURCE);
    CHECK(demo.panes[0].count == TB_VIEW_COUNT);
    CHECK(demo.panes[0].tabs[demo.panes[0].active] == TB_SOURCE);

    demo.panes[0].scroll[TB_SOURCE] = 4;
    CHECK(tb_demo_close_tab(&demo));
    tb_demo_view(&demo, TB_SOURCE);
    CHECK(demo.panes[0].scroll[TB_SOURCE] == 4);
    while (demo.panes[0].count > 1) { CHECK(tb_demo_close_tab(&demo)); }
    CHECK(!tb_demo_close_tab(&demo));
    tb_demo_init(&demo);
    demo.pane = 2;
    TbDemoPane *pane = &demo.panes[2];
    CHECK(tb_demo_has_view(pane, TB_REGISTERS) && tb_demo_has_view(pane, TB_PERIPHERALS));
    CHECK(!tb_demo_has_view(pane, TB_THREADS));
    CHECK(tb_demo_toggle_view(&demo, TB_THREADS) && pane->count == 3);
    CHECK(pane->tabs[pane->active] == TB_THREADS);
    CHECK(!tb_demo_has_view(&demo.panes[0], TB_THREADS));
    CHECK(tb_demo_toggle_view(&demo, TB_PERIPHERALS) && pane->count == 2);
    CHECK(pane->tabs[pane->active] == TB_THREADS); /* removing an inactive tab preserves selection */
    pane->scroll[TB_THREADS] = 7;
    CHECK(tb_demo_toggle_view(&demo, TB_THREADS) && pane->count == 1);
    CHECK(pane->tabs[pane->active] == TB_REGISTERS);
    CHECK(!tb_demo_toggle_view(&demo, TB_REGISTERS) && pane->count == 1);
    CHECK(!tb_demo_toggle_view(&demo, TB_VIEW_COUNT));
    CHECK(tb_demo_toggle_view(&demo, TB_THREADS) && pane->scroll[TB_THREADS] == 7);
    demo.pane = 0;
    CHECK(tb_demo_toggle_view(&demo, TB_THREADS));
    CHECK(tb_demo_toggle_view(&demo, TB_THREADS));
    CHECK(tb_demo_has_view(&demo.panes[2], TB_THREADS));
    puts("PASS demo actions and tab/view state preservation");
    return EXIT_SUCCESS;
}
