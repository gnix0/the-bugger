#include "layout.h"
#include "../support/check.h"

#include <stdint.h>
#include <string.h>

static void tree(const TbLayout *layout, int node, bool visited[TB_LAYOUT_NODES],
                 bool panes[TB_MAX_PANES])
{
    CHECK(node >= 0 && node < TB_LAYOUT_NODES && !visited[node]);
    visited[node] = true;
    const TbLayoutNode *n = &layout->nodes[node];
    CHECK(n->used);
    if (n->pane >= 0) {
        CHECK(n->pane < TB_MAX_PANES && !panes[n->pane]);
        panes[n->pane] = true;
    } else {
        CHECK(n->ratio > 0 && n->ratio < 10000);
        tree(layout, n->first, visited, panes);
        tree(layout, n->second, visited, panes);
    }
}

static void invariants(const TbLayout *layout, int focus)
{
    bool visited[TB_LAYOUT_NODES] = {false}, panes[TB_MAX_PANES] = {false};
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) { tree(layout, layout->roots[i], visited, panes); }
    for (int i = 0; i < TB_LAYOUT_NODES; ++i) { CHECK(visited[i] == layout->nodes[i].used); }
    int live = 0, visible = 0, surface = 0;
    TbRect area = layout->area;
    for (int i = 0; i < TB_MAX_PANES; ++i) {
        CHECK(panes[i] == tb_layout_has_pane(layout, i));
        if (panes[i]) { ++live; }
        TbRect r = layout->panes[i];
        if (r.w == 0 || r.h == 0) { continue; }
        CHECK(panes[i]);
        CHECK(r.x >= area.x && r.y >= area.y);
        CHECK(r.x + r.w <= area.x + area.w && r.y + r.h <= area.y + area.h);
        if (layout->tiled) { CHECK(r.w >= TB_MIN_PANE_WIDTH && r.h >= TB_MIN_PANE_HEIGHT); }
        else { CHECK(i == focus); }
        ++visible;
        surface += r.w * r.h;
        for (int j = 0; j < i; ++j) {
            TbRect b = layout->panes[j];
            if (b.w == 0 || b.h == 0) { continue; }
            CHECK(r.x >= b.x + b.w || b.x >= r.x + r.w ||
                  r.y >= b.y + b.h || b.y >= r.y + r.h);
        }
    }
    CHECK(live == layout->count && live >= 6 && live <= TB_MAX_PANES);
    bool ordered[TB_MAX_PANES] = {false};
    for (int i = 0; i < live; ++i) {
        CHECK(layout->order[i] >= 0 && layout->order[i] < TB_MAX_PANES);
        CHECK(panes[layout->order[i]] && !ordered[layout->order[i]]);
        ordered[layout->order[i]] = true;
    }
    CHECK(visible == (area.w <= 0 || area.h <= 0 ? 0 : layout->tiled ? live : 1));
    if (!layout->tiled) { CHECK(layout->divider_count == 0); return; }
    CHECK(layout->divider_count == live - 3);
    /* Only the two global column gutters intersect the global row gutter. */
    int gutters = -2;
    for (int i = 0; i < layout->divider_count; ++i) {
        TbRect r = layout->dividers[i].rect;
        CHECK(r.x >= area.x && r.y >= area.y && r.x + r.w <= area.x + area.w && r.y + r.h <= area.y + area.h);
        gutters += r.w * r.h;
        CHECK(tb_layout_divider_at(layout, r.x, r.y) >= 0);
    }
    CHECK(surface + gutters == area.w * area.h);
}

static uint32_t random_value(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

int main(void)
{
    TbLayout layout;
    tb_layout_init(&layout);
    CHECK(tb_layout_arrange(&layout, (TbRect){1, 4, 138, 38}, 0));
    invariants(&layout, 0);
    CHECK(layout.panes[0].w == 48 && layout.panes[1].w == 46);
    TbRect original = layout.panes[0];
    int added = tb_layout_split(&layout, 0, TB_SPLIT_RIGHT);
    CHECK(added == 6 && layout.count == 7);
    invariants(&layout, added);
    CHECK(tb_layout_close(&layout, added) == 0);
    CHECK(memcmp(&original, &layout.panes[0], sizeof original) == 0);
    CHECK(tb_layout_close(&layout, 0) == -1);
    CHECK(tb_layout_split(&layout, 0, TB_SPLIT_BELOW) == 6);
    CHECK(tb_layout_close(&layout, 0) == 6); /* original leaf may close inside a split */
    CHECK(!tb_layout_has_pane(&layout, 0) && tb_layout_has_pane(&layout, 6));
    invariants(&layout, 6);

    /* Shrink/restore preserves tree structure, ratios, order and all pane IDs. */
    TbLayout before = layout;
    for (int height = 0; height < 64; ++height) {
        for (int width = 0; width < 200; ++width) {
            for (int i = 0; i < layout.count; ++i) {
                int focus = layout.order[i];
                (void)tb_layout_arrange(&layout, (TbRect){2, 3, width, height}, focus);
                invariants(&layout, focus);
                CHECK(memcmp(before.nodes, layout.nodes, sizeof before.nodes) == 0);
            }
        }
    }
    tb_layout_init(&layout);
    CHECK(!tb_layout_has_pane(&layout, -1) && !tb_layout_has_pane(&layout, TB_MAX_PANES));
    CHECK(!tb_rect_contains((TbRect){0}, 0, 0));
    CHECK(tb_rect_contains((TbRect){2, 3, 4, 5}, 2, 3));
    CHECK(!tb_rect_contains((TbRect){2, 3, 4, 5}, 6, 3));
    CHECK(!tb_rect_contains((TbRect){2, 3, 4, 5}, 2, 8));
    uint32_t state = UINT32_C(0xB066E2);
    int focus = 0;
    for (int iteration = 0; iteration < 12000; ++iteration) {
        unsigned action = random_value(&state) % 5;
        int width = 100 + (int)(random_value(&state) % 1900);
        int height = 20 + (int)(random_value(&state) % 180);
        (void)tb_layout_arrange(&layout, (TbRect){1, 2, width, height}, focus);
        int pane = layout.order[random_value(&state) % (unsigned)layout.count];
        if (action < 2) {
            before = layout;
            int next = tb_layout_split(&layout, pane, action == 0 ? TB_SPLIT_RIGHT : TB_SPLIT_BELOW);
            if (next < 0) { CHECK(memcmp(&before, &layout, sizeof layout) == 0); }
            else { focus = next; }
        } else if (action == 2) {
            int next = tb_layout_close(&layout, pane);
            if (next >= 0) { focus = next; }
        } else if (action == 3 && layout.divider_count > 0) {
            int divider = (int)(random_value(&state) % (unsigned)layout.divider_count);
            int position = (int)(random_value(&state) % 3000) - 500;
            CHECK(tb_layout_move_divider(&layout, divider, position));
        } else { focus = pane; }
        (void)tb_layout_arrange(&layout, layout.area, focus);
        invariants(&layout, focus);
    }
    /* Fill the pool, reject overflow, then reuse freed slots repeatedly. */
    tb_layout_init(&layout);
    for (int cycle = 0; cycle < 3; ++cycle) {
        while (layout.count < TB_MAX_PANES) {
            CHECK(tb_layout_arrange(&layout, (TbRect){0, 0, 6000, 1000}, 0));
            int pane = layout.order[0];
            for (int i = 1; i < layout.count; ++i) {
                int candidate = layout.order[i];
                if (layout.panes[candidate].h > layout.panes[pane].h) { pane = candidate; }
            }
            CHECK(tb_layout_split(&layout, pane, TB_SPLIT_BELOW) >= 0);
        }
        before = layout;
        CHECK(tb_layout_split(&layout, 0, TB_SPLIT_RIGHT) == -1);
        CHECK(memcmp(&before, &layout, sizeof layout) == 0);
        while (layout.count > 6) {
            int pane = TB_MAX_PANES - 1;
            while (!tb_layout_has_pane(&layout, pane)) { --pane; }
            CHECK(tb_layout_close(&layout, pane) >= 0);
        }
    }
    puts("PASS layout tree, split/collapse, resize bounds, hit tests, pool reuse and 12000 deterministic operations");
    return EXIT_SUCCESS;
}
