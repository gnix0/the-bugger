#include "layout/layout.h"

#include <string.h>

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}


static int find_pane(const TbLayout_t *layout, int pane)
{
    if (pane < 0 || pane >= TB_MAX_PANES)
        return -1;

    for (int i = 0; i < TB_LAYOUT_NODES; ++i) {
        if (layout->nodes[i].used && layout->nodes[i].pane == pane)
            return i;
    }

    return -1;
}


bool tb_layout_has_pane(const TbLayout_t *layout, int pane)
{
    return find_pane(layout, pane) >= 0;
}


static bool contains_node(const TbLayout_t *layout, int root, int node)
{
    if (root == node)
        return true;

    const TbLayoutNode_t *n = &layout->nodes[root];

    return n->pane < 0 && (contains_node(layout, n->first, node) || contains_node(layout, n->second, node));
}


int tb_layout_root(const TbLayout_t *layout, int pane)
{
    int node = find_pane(layout, pane);
    if (node < 0)
        return -1;

    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        if (contains_node(layout, layout->roots[i], node))
            return i;
    }

    return -1;
}


static int first_pane(const TbLayout_t *layout, int node)
{
    while (layout->nodes[node].pane < 0)
        node = layout->nodes[node].first;

    return layout->nodes[node].pane;
}


int tb_layout_pane_at(const TbLayout_t *layout, int x, int y)
{
    for (int i = 0; i < layout->count; ++i) {
        int pane = layout->order[i];
        if (tb_rect_contains(layout->panes[pane], x, y))
            return pane;
    }

    return -1;
}


void tb_layout_init(TbLayout_t *layout)
{
    memset(layout, 0, sizeof(*layout));
    layout->columns[0] = 3600;
    layout->columns[1] = 3400;
    layout->columns[2] = 3000;
    layout->row_ratio  = 6200;
    layout->count      = TB_DEFAULT_PANES;

    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        layout->nodes[i]      = (TbLayoutNode_t) {
            .used = true,
            .pane = i
        };

        layout->roots[i]      = i;
        layout->order[i]      = i;
        layout->identities[i] = ++layout->serial;
        layout->root_focus[i] = i;
    }

    for (int i = 0; i < 3; ++i)
        layout->lower_order[i] = i;
}


static TbRect_t minimum(const TbLayout_t *layout, int node)
{
    const TbLayoutNode_t *n = &layout->nodes[node];
    if (n->pane >= 0) {
        return (TbRect_t) {
            0,
            0,
            TB_MIN_PANE_WIDTH,
            TB_MIN_PANE_HEIGHT
        };
    }
    TbRect_t a = minimum(layout, n->first), b = minimum(layout, n->second);

    return (TbRect_t) {
        0,
        0,
        n->direction == TB_SPLIT_RIGHT ? a.w + b.w + 1 : (a.w > b.w ? a.w : b.w),
        n->direction == TB_SPLIT_BELOW ? a.h + b.h + 1 : (a.h > b.h ? a.h : b.h)
    };
}


static void collect(TbLayout_t *layout, int node)
{
    const TbLayoutNode_t *n = &layout->nodes[node];
    if (n->pane >= 0) {
        layout->order[layout->count++] = n->pane;
    } else {
        collect(layout, n->first);
        collect(layout, n->second);
    }
}


static void arrange_node(TbLayout_t *layout, int node, TbRect_t area)
{
    const TbLayoutNode_t *n = &layout->nodes[node];
    if (n->pane >= 0) {
        layout->panes[n->pane] = area;
        return;
    }
    TbRect_t a = minimum(layout, n->first), b = minimum(layout, n->second);
    bool     right = n->direction == TB_SPLIT_RIGHT;
    int      total = (right ? area.w : area.h) - 1;
    int      low   = right ? a.w : a.h, high = right ? b.w : b.h;
    int      first = clamp(total * n->ratio / 10000, low, total - high);
    TbRect_t ar    = area, br = area, divider = area;
    if (right) {
        ar.w       = first;
        br.x      += first + 1;
        br.w      -= first + 1;
        divider.x += first;
        divider.w  = 1;
    } else {
        ar.h       = first;
        br.y      += first + 1;
        br.h      -= first + 1;
        divider.y += first;
        divider.h  = 1;
    }
    layout->dividers[layout->divider_count++] = (TbDivider_t) {
        divider,
        area,
        node,
        low,
        high,
        n->direction
    };

    arrange_node(layout, n->first, ar);
    arrange_node(layout, n->second, br);
}


/* A root's retained tree may temporarily exceed the available column. */
static void arrange_root(TbLayout_t *layout, int root, TbRect_t area)
{
    TbRect_t m = minimum(layout, layout->roots[root]);
    if (area.w >= m.w && area.h >= m.h) {
        arrange_node(layout, layout->roots[root], area);
    } else if (area.w >= TB_MIN_PANE_WIDTH && area.h >= TB_MIN_PANE_HEIGHT) {
        layout->panes[layout->root_focus[root]] = area;
    }
}


static void arrange_column(TbLayout_t *layout, int column, TbRect_t area, int focused_root)
{
    TbRect_t upper = minimum(layout, layout->roots[column]);
    TbRect_t lower = minimum(layout, layout->roots[column + 3]);
    if (lower.w > area.w)
        lower.h = TB_MIN_PANE_HEIGHT;
    bool open = layout->lower_open[column];

    if (open && area.h >= upper.h + lower.h + 1) {
        int maximum = area.h - upper.h - 1;
        if (!layout->lower_height[column])
            layout->lower_height[column] = clamp(area.h * 3800 / 10000, lower.h, maximum);

        int height = clamp(layout->lower_height[column], lower.h, maximum);
        TbRect_t top = area;
        top.h -= height + 1;
        TbRect_t bottom = area;
        bottom.y += top.h + 1;
        bottom.h = height;
        TbRect_t grip = area;
        grip.y += top.h;
        grip.h = 1;
        layout->lower_areas[column] = bottom;
        layout->dividers[layout->divider_count++] = (TbDivider_t) {
            .rect           = grip,
            .area           = area,
            .node           = -4 - column,
            .minimum_first  = upper.h,
            .minimum_second = lower.h,
            .direction      = TB_SPLIT_BELOW
        };
        arrange_root(layout, column, top);
        arrange_root(layout, column + 3, bottom);
    } else {
        /* Too few rows for two usable panes: show the selected root without
         * changing expansion preferences or losing access to the other root. */
        int root = open && focused_root == column + 3 ? column + 3 : column;
        if (layout->tiled && root == column)
            area.h -= 2; /* Dock and its separating gutter. */
        arrange_root(layout, root, area);
        if (root >= 3)
            layout->lower_areas[column] = area;
    }
}


static bool arrange_columns(TbLayout_t *layout, TbRect_t area, int focused)
{
    int root = tb_layout_root(layout, focused);
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        if (tb_layout_root(layout, layout->root_focus[i]) != i)
            layout->root_focus[i] = first_pane(layout, layout->roots[i]);
    }
    layout->root_focus[root] = focused;
    if (root < 3)
        layout->primary_focus = focused;
    if (tb_layout_root(layout, layout->primary_focus) < 0 ||
        tb_layout_root(layout, layout->primary_focus) >= 3) {
        layout->primary_focus = layout->root_focus[0];
    }

    memset(layout->minimum_columns, 0, sizeof(layout->minimum_columns));
    memset(layout->minimum_rows, 0, sizeof(layout->minimum_rows));
    for (int i = 0; i < 3; ++i) {
        TbRect_t upper = minimum(layout, layout->roots[i]);
        layout->minimum_columns[i] = upper.w;
        if (upper.h > layout->minimum_rows[0])
            layout->minimum_rows[0] = upper.h;
    }

    int available = area.w - 2;
    int *widths = layout->widths;
    int *columns = layout->minimum_columns;
    layout->tiled = area.w >= 110 && area.h - 2 >= layout->minimum_rows[0] &&
                    available >= columns[0] + columns[1] + columns[2];
    if (area.w < 8 || area.h < TB_MIN_PANE_HEIGHT)
        return false;

    widths[0] = available * layout->columns[0] / 10000;
    widths[1] = available * layout->columns[1] / 10000;
    if (layout->tiled) {
        widths[0] = clamp(widths[0], columns[0], available - columns[1] - columns[2]);
        widths[1] = clamp(widths[1], columns[1], available - widths[0] - columns[2]);
    }
    widths[2] = available - widths[0] - widths[1];

    int x = area.x;
    for (int i = 0; i < 3; ++i) {
        layout->lower_bars[i] = (TbRect_t) {
            .x = x,
            .y = area.y + area.h - 1,
            .w = widths[i],
            .h = 1
        };
        if (layout->tiled) {
            TbRect_t column = area;
            column.x = x;
            column.w = widths[i];
            arrange_column(layout, i, column, root);
        }
        x += widths[i] + 1;
    }

    if (!layout->tiled) {
        /* Compact terminals retain one column's upper/lower pair when it fits.
         * All other groups remain reachable through the dock and pane selector. */
        TbRect_t column = area;
        column.h -= 2;
        int selected = root % 3;
        if (column.h >= TB_MIN_PANE_HEIGHT)
            arrange_column(layout, selected, column, root);
        else
            arrange_root(layout, root, area);
    } else {
        x = area.x + widths[0];
        for (int i = 0; i < 2; ++i) {
            layout->dividers[layout->divider_count++] = (TbDivider_t) {
                .rect = {
                    .x = x,
                    .y = area.y,
                    .w = 1,
                    .h = area.h
                },
                .area      = area,
                .node      = -1 - i,
                .direction = TB_SPLIT_RIGHT
            };
            x += widths[1] + 1;
        }
    }

    return layout->tiled;
}


bool tb_layout_arrange(TbLayout_t *layout, TbRect_t area, int focused)
{
    memset(layout->panes, 0, sizeof(layout->panes));
    layout->area          = area;
    layout->divider_count = 0;
    layout->count         = 0;
    layout->tiled         = false;
    memset(layout->lower_bars, 0, sizeof(layout->lower_bars));
    memset(layout->lower_areas, 0, sizeof(layout->lower_areas));
    for (int i = 0; i < TB_DEFAULT_PANES; ++i)
        collect(layout, layout->roots[i]);
    if (!tb_layout_has_pane(layout, focused) || area.w <= 0 || area.h <= 0)
        return false;

    if (layout->overlays)
        return arrange_columns(layout, area, focused);

    int *columns = layout->minimum_columns, *rows = layout->minimum_rows;
    memset(columns, 0, sizeof(layout->minimum_columns));
    memset(rows, 0, sizeof(layout->minimum_rows));
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        TbRect_t m = minimum(layout, layout->roots[i]);
        if (m.w > columns[i % 3])
            columns[i % 3] = m.w;
        if (m.h > rows[i / 3])
            rows[i / 3] = m.h;
    }

    int available = area.w - 2;
    if (area.w < 110 || area.h < 20 || available < columns[0] + columns[1] + columns[2] ||
        area.h - 1 < rows[0] + rows[1]) {
        layout->panes[focused] = area;
        return false;
    }

    layout->tiled = true;
    int *widths   = layout->widths;
    widths[0] =
        clamp(available * layout->columns[0] / 10000, columns[0], available - columns[1] - columns[2]);
    widths[1] = clamp(available * layout->columns[1] / 10000, columns[1], available - widths[0] - columns[2]);
    widths[2] = available - widths[0] - widths[1];
    int top = clamp((area.h - 1) * layout->row_ratio / 10000, rows[0], area.h - 1 - rows[1]);

    for (int i = 0; i < 2; ++i) {
        int x = area.x + widths[0] + (i == 0 ? 0 : widths[1] + 1);
        layout->dividers[layout->divider_count++] =
            (TbDivider_t) {
                {
                    x,
                    area.y,
                    1,
                    area.h
                },
                area,
                -1 - i,
                0,
                0,
                TB_SPLIT_RIGHT
            };
    }
    layout->dividers[layout->divider_count++] =
        (TbDivider_t) {
            {
                area.x,
                area.y + top,
                area.w,
                1
            },
            area,
            -3,
            rows[0],
            rows[1],
            TB_SPLIT_BELOW
        };
    for (int i = 0; i < TB_DEFAULT_PANES; ++i) {
        int col = i % 3, x = area.x;
        for (int j = 0; j < col; ++j)
            x += widths[j] + 1;
        arrange_node(
            layout, layout->roots[i],
            (TbRect_t) {
                x,
                area.y + (i < 3 ? 0 : top + 1),
                widths[col],
                i < 3 ? top : area.h - top - 1
            });
    }

    return true;
}


/* Pane identity and the validated split direction are distinct API inputs. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int tb_layout_split(TbLayout_t *layout, int pane, TbSplit_e direction)
{
    int node = find_pane(layout, pane);
    if (node < 0 || !layout->tiled || layout->count == TB_MAX_PANES ||
        (direction != TB_SPLIT_RIGHT && direction != TB_SPLIT_BELOW)) {
        return -1;
    }
    TbRect_t r = layout->panes[pane];
    if ((direction == TB_SPLIT_RIGHT && r.w < 2 * TB_MIN_PANE_WIDTH + 1) ||
        (direction == TB_SPLIT_BELOW && r.h < 2 * TB_MIN_PANE_HEIGHT + 1)) {
        return -1;
    }

    int slots[2], found = 0, new_pane = 0;
    for (int i = 0; i < TB_LAYOUT_NODES && found < 2; ++i) {
        if (!layout->nodes[i].used)
            slots[found++] = i;
    }
    if (found != 2)
        return -1;

    while (tb_layout_has_pane(layout, new_pane))
        ++new_pane;

    layout->nodes[slots[0]] = layout->nodes[node];
    layout->nodes[slots[1]]      = (TbLayoutNode_t) {
        .used = true,
        .pane = new_pane
    };

    layout->identities[new_pane] = ++layout->serial;
    layout->nodes[node]          = (TbLayoutNode_t) {
        true,
        -1,
        slots[0],
        slots[1],
        5000,
        direction
    };

    (void)tb_layout_arrange(layout, layout->area, new_pane);

    return new_pane;
}


int tb_layout_close(TbLayout_t *layout, int pane)
{
    int node = find_pane(layout, pane);
    if (node < 0)
        return -1;

    for (int i = 0; i < TB_LAYOUT_NODES; ++i) {
        TbLayoutNode_t *n = &layout->nodes[i];
        if (!n->used || n->pane >= 0 || (n->first != node && n->second != node))
            continue;

        int sibling  = n->first == node ? n->second : n->first;
        int survivor = sibling;
        while (layout->nodes[survivor].pane < 0)
            survivor = layout->nodes[survivor].first;
        int focus = layout->nodes[survivor].pane;

        *n                          = layout->nodes[sibling];
        layout->nodes[node].used    = false;
        layout->nodes[sibling].used = false;
        layout->identities[pane]    = 0;

        (void)tb_layout_arrange(layout, layout->area, focus);
        return focus;
    }

    return -1;
}


bool tb_rect_contains(TbRect_t rect, int x, int y)
{
    return rect.w > 0 && rect.h > 0 && x >= rect.x && y >= rect.y && x - rect.x < rect.w &&
           y - rect.y < rect.h;
}


int tb_layout_divider_at(const TbLayout_t *layout, int x, int y)
{
    for (int i = layout->divider_count - 1; i >= 0; --i) {
        if (tb_rect_contains(layout->dividers[i].rect, x, y))
            return i;
    }

    return -1;
}


/* Divider ID and terminal coordinate are distinct API inputs. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool tb_layout_move_divider(TbLayout_t *layout, int divider, int position)
{
    if (divider < 0 || divider >= layout->divider_count)
        return false;

    TbDivider_t d = layout->dividers[divider];
    if (layout->overlays && d.node <= -4 && d.node >= -6) {
        int total = d.area.h - 1;
        int first = clamp(position - d.area.y, d.minimum_first, total - d.minimum_second);
        layout->lower_height[-d.node - 4] = total - first;
        return true;
    }
    if (!layout->tiled || d.node < -3 || d.node >= TB_LAYOUT_NODES)
        return false;

    bool right = d.direction == TB_SPLIT_RIGHT;
    int total = (right ? d.area.w : d.area.h) - 1;
    int first = position - (right ? d.area.x : d.area.y);

    if (d.node == -1 || d.node == -2) {
        int col       = -d.node - 1;
        int available = layout->area.w - 2;
        int start     = layout->area.x + (col == 0 ? 0 : layout->widths[0] + 1);
        int combined  = layout->widths[col] + layout->widths[col + 1];
        first = clamp(position - start, layout->minimum_columns[col],
                              combined - layout->minimum_columns[col + 1]);
        int widths[3];
        memcpy(widths, layout->widths, sizeof(widths));
        widths[col] = first;
        widths[col + 1] = combined - first;

        /* Ceiling prevents a one-cell jump from fixed-point truncation. */
        layout->columns[0] = (widths[0] * 10000 + available - 1) / available;
        layout->columns[1] = (widths[1] * 10000 + available - 1) / available;
        layout->columns[2] = 10000 - layout->columns[0] - layout->columns[1];
    } else {
        first = clamp(first, d.minimum_first, total - d.minimum_second);
        int ratio = (first * 10000 + total - 1) / total;
        if (d.node == -3) {
            layout->row_ratio = ratio;
        } else {
            layout->nodes[d.node].ratio = ratio;
        }
    }

    return true;
}
