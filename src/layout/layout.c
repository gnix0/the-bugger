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

static bool contains_node(const TbLayout *layout, int root, int node)
{
    if (root == node) { return true; }
    const TbLayoutNode *n = &layout->nodes[root];
    return n->pane < 0 && (contains_node(layout, n->first, node) || contains_node(layout, n->second, node));
}

int tb_layout_root(const TbLayout *layout, int pane)
{
    int node = find_pane(layout, pane);
    if (node < 0) { return -1; }
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        if (contains_node(layout, layout->roots[i], node)) { return i; }
    }
    return -1;
}

static int first_pane(const TbLayout *layout, int node)
{
    while (layout->nodes[node].pane < 0) { node = layout->nodes[node].first; }
    return layout->nodes[node].pane;
}

static int lower_at(const TbLayout *layout, int x, int y)
{
    if (!layout->overlays) { return -1; }
    for (int i = 2; i >= 0; --i) {
        int lower = layout->lower_order[i];
        if (tb_rect_contains(layout->lower_areas[lower], x, y)) { return lower; }
    }
    return -1;
}

int tb_layout_pane_at(const TbLayout *layout, int x, int y)
{
    int lower = lower_at(layout, x, y);
    for (int i = layout->count - 1; i >= 0; --i) {
        int pane = layout->order[i];
        if (tb_rect_contains(layout->panes[pane], x, y) &&
            (lower < 0 || tb_layout_root(layout, pane) == lower + 3)) { return pane; }
    }
    return -1;
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
        layout->identities[i] = ++layout->serial;
        layout->root_focus[i] = i;
    }
    for (int i = 0; i < 3; ++i) { layout->lower_order[i] = i; }
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

static bool arrange_overlays(TbLayout *layout, TbRect area, int focused)
{
    TbRect primary = area; --primary.h; /* Persistent one-row access bars. */
    if (primary.h <= 0) { return false; }
    int root = tb_layout_root(layout, focused);
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        if (tb_layout_root(layout, layout->root_focus[i]) != i) {
            layout->root_focus[i] = first_pane(layout, layout->roots[i]);
        }
    }
    layout->root_focus[root] = focused;
    if (root < 3) { layout->primary_focus = focused; }
    if (tb_layout_root(layout, layout->primary_focus) < 0 || tb_layout_root(layout, layout->primary_focus) >= 3) {
        layout->primary_focus = layout->root_focus[0];
    }
    if (area.w < 8) { layout->panes[layout->primary_focus] = primary; return false; }
    if (root >= 3 && layout->lower_open[root - 3]) {
        int position = 0;
        while (layout->lower_order[position] != root - 3) { ++position; }
        for (int i = position; i < 2; ++i) { layout->lower_order[i] = layout->lower_order[i + 1]; }
        layout->lower_order[2] = root - 3;
    }
    memset(layout->minimum_columns, 0, sizeof layout->minimum_columns);
    memset(layout->minimum_rows, 0, sizeof layout->minimum_rows);
    for (int i = 0; i < 3; ++i) {
        TbRect m = minimum(layout, layout->roots[i]);
        layout->minimum_columns[i] = m.w;
        if (m.h > layout->minimum_rows[0]) { layout->minimum_rows[0] = m.h; }
    }
    int available = area.w - 2;
    int *widths = layout->widths, *columns = layout->minimum_columns;
    layout->tiled = area.w >= 110 && primary.h >= layout->minimum_rows[0] &&
                    available >= columns[0] + columns[1] + columns[2];
    widths[0] = available * layout->columns[0] / 10000;
    widths[1] = available * layout->columns[1] / 10000;
    if (layout->tiled) {
        widths[0] = clamp(widths[0], columns[0], available - columns[1] - columns[2]);
        widths[1] = clamp(widths[1], columns[1], available - widths[0] - columns[2]);
    }
    widths[2] = available - widths[0] - widths[1];
    int x = area.x;
    for (int i = 0; i < 3; ++i) {
        layout->lower_bars[i] = (TbRect){x, primary.y + primary.h, widths[i], 1};
        if (layout->tiled) { arrange_node(layout, layout->roots[i], (TbRect){x, primary.y, widths[i], primary.h}); }
        x += widths[i] + 1;
    }
    if (!layout->tiled) { layout->panes[layout->primary_focus] = primary; }
    else {
        x = area.x + widths[0];
        for (int i = 0; i < 2; ++i) {
            layout->dividers[layout->divider_count++] =
                (TbDivider){{x, primary.y, 1, primary.h}, primary, -1 - i, 0, 0, TB_SPLIT_RIGHT};
            x += widths[1] + 1;
        }
    }
    for (int i = 0; i < 3; ++i) {
        int lower = layout->lower_order[i];
        if (!layout->lower_open[lower] || primary.h <= TB_MIN_PANE_HEIGHT) { continue; }
        int maximum = primary.h - 1;
        if (!layout->lower_height[lower]) { layout->lower_height[lower] = clamp(primary.h * 3800 / 10000, TB_MIN_PANE_HEIGHT, maximum); }
        int height = clamp(layout->lower_height[lower], TB_MIN_PANE_HEIGHT, maximum);
        TbRect m = minimum(layout, layout->roots[lower + 3]);
        /* An expanded pane owns its former dock row; its header already
         * provides minimize/menu controls. Keep its top and primary geometry. */
        TbRect r = {layout->lower_bars[lower].x, primary.y + primary.h - height, widths[lower], height + 1};
        if (!layout->tiled || m.w > r.w) { r.x = primary.x; r.w = primary.w; }
        layout->lower_areas[lower] = (TbRect){r.x, r.y - 1, r.w, r.h + 1};
        layout->dividers[layout->divider_count++] =
            (TbDivider){{r.x, r.y - 1, r.w, 1}, primary, -4 - lower, TB_MIN_PANE_HEIGHT, 0, TB_SPLIT_BELOW};
        if (r.w >= m.w && r.h >= m.h) { arrange_node(layout, layout->roots[lower + 3], r); }
        else { layout->panes[layout->root_focus[lower + 3]] = r; }
    }
    return layout->tiled;
}

bool tb_layout_arrange(TbLayout *layout, TbRect area, int focused)
{
    memset(layout->panes, 0, sizeof layout->panes);
    layout->area = area;
    layout->divider_count = 0;
    layout->count = 0;
    layout->tiled = false;
    memset(layout->lower_bars, 0, sizeof layout->lower_bars);
    memset(layout->lower_areas, 0, sizeof layout->lower_areas);
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) { collect(layout, layout->roots[i]); }
    if (!tb_layout_has_pane(layout, focused) || area.w <= 0 || area.h <= 0) { return false; }
    if (layout->overlays) { return arrange_overlays(layout, area, focused); }
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
    layout->identities[new_pane] = ++layout->serial;
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
        layout->identities[pane] = 0;
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
    int lower = lower_at(layout, x, y);
    for (int i = layout->divider_count - 1; i >= 0; --i) {
        const TbDivider *d = &layout->dividers[i];
        if (!tb_rect_contains(d->rect, x, y)) { continue; }
        if (lower >= 0) {
            int root = d->node <= -4 ? -d->node - 1 : -1;
            if (d->node >= 0) {
                for (int r = 0; r < TB_DEFAULT_PANES; ++r) {
                    if (contains_node(layout, layout->roots[r], d->node)) { root = r; break; }
                }
            }
            if (root != lower + 3) { continue; }
        }
        return i;
    }
    return -1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): divider ID and terminal coordinate are distinct API inputs.
bool tb_layout_move_divider(TbLayout *layout, int divider, int position)
{
    if (divider < 0 || divider >= layout->divider_count) { return false; }
    TbDivider d = layout->dividers[divider];
    if (layout->overlays && d.node <= -4 && d.node >= -6) {
        int bottom = layout->area.y + layout->area.h - 1;
        position = clamp(position, layout->area.y, bottom - TB_MIN_PANE_HEIGHT - 1);
        layout->lower_height[-d.node - 4] = bottom - position - 1;
        return true;
    }
    if (!layout->tiled || d.node < -3 || d.node >= TB_LAYOUT_NODES) { return false; }
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
