#include "layout.h"

#include <string.h>

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static int find_pane(const TbLayout *layout, int pane)
{
    if (pane < 0 || pane >= TB_MAX_PANES) { return -1; }
    for (int i = 0; i < TB_LAYOUT_NODES; ++i) {
        if (layout->nodes[i].used && layout->nodes[i].pane == pane) { return i; }
    }
    return -1;
}

bool tb_layout_has_pane(const TbLayout *layout, int pane)
{
    return find_pane(layout, pane) >= 0;
}

void tb_layout_init(TbLayout *layout)
{
    memset(layout, 0, sizeof *layout);
    layout->columns[0] = 3600;
    layout->columns[1] = 3400;
    layout->columns[2] = 3000;
    layout->row_ratio = 6200;
    layout->count = TB_DEFAULT_PANES;
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        layout->nodes[i] = (TbLayoutNode){.used = true, .pane = i};
        layout->roots[i] = i;
        layout->order[i] = i;
    }
}

static TbRect minimum(const TbLayout *layout, int node)
{
    const TbLayoutNode *n = &layout->nodes[node];
    if (n->pane >= 0) { return (TbRect){0, 0, TB_MIN_PANE_WIDTH, TB_MIN_PANE_HEIGHT}; }
    TbRect a = minimum(layout, n->first), b = minimum(layout, n->second);
    return (TbRect){0, 0,
        n->direction == TB_SPLIT_RIGHT ? a.w + b.w + 1 : (a.w > b.w ? a.w : b.w),
        n->direction == TB_SPLIT_BELOW ? a.h + b.h + 1 : (a.h > b.h ? a.h : b.h)};
}

static void collect(TbLayout *layout, int node)
{
    const TbLayoutNode *n = &layout->nodes[node];
    if (n->pane >= 0) { layout->order[layout->count++] = n->pane; }
    else { collect(layout, n->first); collect(layout, n->second); }
}

static void arrange_node(TbLayout *layout, int node, TbRect area)
{
    const TbLayoutNode *n = &layout->nodes[node];
    if (n->pane >= 0) { layout->panes[n->pane] = area; return; }
    TbRect a = minimum(layout, n->first), b = minimum(layout, n->second);
    bool right = n->direction == TB_SPLIT_RIGHT;
    int total = (right ? area.w : area.h) - 1;
    int low = right ? a.w : a.h, high = right ? b.w : b.h;
    int first = clamp(total * n->ratio / 10000, low, total - high);
    TbRect ar = area, br = area, divider = area;
    if (right) {
        ar.w = first; br.x += first + 1; br.w -= first + 1;
        divider.x += first; divider.w = 1;
    } else {
        ar.h = first; br.y += first + 1; br.h -= first + 1;
        divider.y += first; divider.h = 1;
    }
    layout->dividers[layout->divider_count++] =
        (TbDivider){divider, area, node, low, high, n->direction};
    arrange_node(layout, n->first, ar);
    arrange_node(layout, n->second, br);
}

bool tb_layout_arrange(TbLayout *layout, TbRect area, int focused)
{
    memset(layout->panes, 0, sizeof layout->panes);
    layout->area = area;
    layout->divider_count = 0;
    layout->count = 0;
    layout->tiled = false;
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) { collect(layout, layout->roots[i]); }
    if (!tb_layout_has_pane(layout, focused) || area.w <= 0 || area.h <= 0) { return false; }
    int *columns = layout->minimum_columns, *rows = layout->minimum_rows;
    memset(columns, 0, sizeof layout->minimum_columns);
    memset(rows, 0, sizeof layout->minimum_rows);
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        TbRect m = minimum(layout, layout->roots[i]);
        if (m.w > columns[i % 3]) { columns[i % 3] = m.w; }
        if (m.h > rows[i / 3]) { rows[i / 3] = m.h; }
    }
    int available = area.w - 2;
    if (area.w < 110 || area.h < 20 ||
        available < columns[0] + columns[1] + columns[2] || area.h - 1 < rows[0] + rows[1]) {
        layout->panes[focused] = area;
        return false;
    }
    layout->tiled = true;
    int *widths = layout->widths;
    widths[0] = clamp(available * layout->columns[0] / 10000,
                      columns[0], available - columns[1] - columns[2]);
    widths[1] = clamp(available * layout->columns[1] / 10000,
                      columns[1], available - widths[0] - columns[2]);
    widths[2] = available - widths[0] - widths[1];
    int top = clamp((area.h - 1) * layout->row_ratio / 10000, rows[0], area.h - 1 - rows[1]);
    for (int i = 0; i < 2; ++i) {
        int x = area.x + widths[0] + (i == 0 ? 0 : widths[1] + 1);
        layout->dividers[layout->divider_count++] =
            (TbDivider){{x, area.y, 1, area.h}, area, -1 - i, 0, 0, TB_SPLIT_RIGHT};
    }
    layout->dividers[layout->divider_count++] =
        (TbDivider){{area.x, area.y + top, area.w, 1}, area, -3, rows[0], rows[1], TB_SPLIT_BELOW};
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        int col = i % 3, x = area.x;
        for (int j = 0; j < col; ++j) { x += widths[j] + 1; }
        arrange_node(layout, layout->roots[i],
                     (TbRect){x, area.y + (i < 3 ? 0 : top + 1), widths[col],
                              i < 3 ? top : area.h - top - 1});
    }
    return true;
}

int tb_layout_split(TbLayout *layout, int pane, TbSplit direction)
{
    int node = find_pane(layout, pane);
    if (node < 0 || !layout->tiled || layout->count == TB_MAX_PANES ||
        (direction != TB_SPLIT_RIGHT && direction != TB_SPLIT_BELOW)) { return -1; }
    TbRect r = layout->panes[pane];
    if ((direction == TB_SPLIT_RIGHT && r.w < 2 * TB_MIN_PANE_WIDTH + 1) ||
        (direction == TB_SPLIT_BELOW && r.h < 2 * TB_MIN_PANE_HEIGHT + 1)) { return -1; }
    int slots[2], found = 0, new_pane = 0;
    for (int i = 0; i < TB_LAYOUT_NODES && found < 2; ++i) {
        if (!layout->nodes[i].used) { slots[found++] = i; }
    }
    if (found != 2) { return -1; }
    while (tb_layout_has_pane(layout, new_pane)) { ++new_pane; }
    layout->nodes[slots[0]] = layout->nodes[node];
    layout->nodes[slots[1]] = (TbLayoutNode){.used = true, .pane = new_pane};
    layout->nodes[node] = (TbLayoutNode){true, -1, slots[0], slots[1], 5000, direction};
    (void)tb_layout_arrange(layout, layout->area, new_pane);
    return new_pane;
}

int tb_layout_close(TbLayout *layout, int pane)
{
    int node = find_pane(layout, pane);
    if (node < 0) { return -1; }
    for (int i = 0; i < TB_LAYOUT_NODES; ++i) {
        TbLayoutNode *n = &layout->nodes[i];
        if (!n->used || n->pane >= 0 || (n->first != node && n->second != node)) { continue; }
        int sibling = n->first == node ? n->second : n->first;
        int survivor = sibling;
        while (layout->nodes[survivor].pane < 0) { survivor = layout->nodes[survivor].first; }
        int focus = layout->nodes[survivor].pane;
        *n = layout->nodes[sibling];
        layout->nodes[node].used = false;
        layout->nodes[sibling].used = false;
        (void)tb_layout_arrange(layout, layout->area, focus);
        return focus;
    }
    return -1;
}

bool tb_rect_contains(TbRect rect, int x, int y)
{
    return rect.w > 0 && rect.h > 0 && x >= rect.x && y >= rect.y &&
           x - rect.x < rect.w && y - rect.y < rect.h;
}

int tb_layout_divider_at(const TbLayout *layout, int x, int y)
{
    for (int i = 0; i < layout->divider_count; ++i) {
        if (tb_rect_contains(layout->dividers[i].rect, x, y)) { return i; }
    }
    return -1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): divider ID and terminal coordinate are distinct API inputs.
bool tb_layout_move_divider(TbLayout *layout, int divider, int position)
{
    if (divider < 0 || divider >= layout->divider_count || !layout->tiled) { return false; }
    TbDivider d = layout->dividers[divider];
    bool right = d.direction == TB_SPLIT_RIGHT;
    int total = (right ? d.area.w : d.area.h) - 1;
    int first = position - (right ? d.area.x : d.area.y);
    if (d.node == -1 || d.node == -2) {
        int col = -d.node - 1;
        int available = layout->area.w - 2;
        int start = layout->area.x + (col == 0 ? 0 : layout->widths[0] + 1);
        int combined = layout->widths[col] + layout->widths[col + 1];
        first = clamp(position - start, layout->minimum_columns[col],
                      combined - layout->minimum_columns[col + 1]);
        int widths[3];
        memcpy(widths, layout->widths, sizeof widths);
        widths[col] = first;
        widths[col + 1] = combined - first;
        /* Ceiling prevents a one-cell jump from fixed-point truncation. */
        layout->columns[0] = (widths[0] * 10000 + available - 1) / available;
        layout->columns[1] = (widths[1] * 10000 + available - 1) / available;
        layout->columns[2] = 10000 - layout->columns[0] - layout->columns[1];
    } else {
        first = clamp(first, d.minimum_first, total - d.minimum_second);
        int ratio = (first * 10000 + total - 1) / total;
        if (d.node == -3) { layout->row_ratio = ratio; }
        else { layout->nodes[d.node].ratio = ratio; }
    }
    return true;
}
