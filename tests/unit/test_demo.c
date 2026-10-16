#include "demo.h"
#include "../support/check.h"

int main(void)
{
    TbDemo demo;
    tb_demo_init(&demo);
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

    /* Half-open hit bounds, zero-size hidden controls, and all screen sizes. */
    CHECK(tb_rect_contains((TbRect){2, 3, 4, 5}, 2, 3));
    CHECK(!tb_rect_contains((TbRect){2, 3, 4, 5}, 6, 3));
    CHECK(!tb_rect_contains((TbRect){2, 3, 4, 5}, 2, 8));
    CHECK(!tb_rect_contains((TbRect){0}, 0, 0));
    for (int height = 0; height < 64; ++height) {
        for (int width = 0; width < 200; ++width) {
            for (int focused = 0; focused < TB_PANES; ++focused) {
                TbRect panes[TB_PANES];
                bool tiled = tb_demo_geometry((TbRect){0, 0, width, height}, focused, panes);
                int visible = 0;
                for (int i = 0; i < TB_PANES; ++i) {
                    TbRect r = panes[i];
                    if (r.w == 0 || r.h == 0) { continue; }
                    ++visible;
                    CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= width && r.y + r.h <= height);
                    for (int j = 0; j < i; ++j) {
                        TbRect other = panes[j];
                        if (other.w == 0 || other.h == 0) { continue; }
                        CHECK(r.x >= other.x + other.w || other.x >= r.x + r.w ||
                              r.y >= other.y + other.h || other.y >= r.y + r.h);
                    }
                }
                CHECK(visible == (width == 0 || height == 0 ? 0 : tiled ? 6 : 1));
            }
        }
    }
    puts("PASS M01 demo actions, tab preservation, hit bounds and geometry invariants");
    return EXIT_SUCCESS;
}
