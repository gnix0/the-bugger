#ifndef THE_BUGGER_LAYOUT_H
#define THE_BUGGER_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

enum { TB_DEFAULT_PANES = 6, TB_MAX_PANES = 32, TB_LAYOUT_NODES = 64,
       TB_MIN_PANE_WIDTH = 20, TB_MIN_PANE_HEIGHT = 7 };
typedef struct { int x, y, w, h; } TbRect;
typedef enum { TB_SPLIT_RIGHT, TB_SPLIT_BELOW } TbSplit;

typedef struct {
    bool used;
    int pane, first, second, ratio;
    TbSplit direction;
} TbLayoutNode;

typedef struct {
    TbRect rect, area;
    int node, minimum_first, minimum_second;
    TbSplit direction;
} TbDivider;

typedef struct {
    TbLayoutNode nodes[TB_LAYOUT_NODES];
    int roots[TB_DEFAULT_PANES], columns[3], row_ratio;
    TbRect panes[TB_MAX_PANES], area;
    TbDivider dividers[TB_MAX_PANES];
    int divider_count, order[TB_MAX_PANES], count;
    uint64_t serial, identities[TB_MAX_PANES];
    int widths[3], minimum_columns[3], minimum_rows[2];
    bool tiled;
    /* Lower roots retain their split trees while hidden or over the primary
     * roots. Preferred heights survive temporary terminal-size clamping. */
    bool overlays, lower_open[3];
    int lower_height[3], lower_order[3], root_focus[TB_DEFAULT_PANES], primary_focus;
    TbRect lower_bars[3], lower_areas[3];
} TbLayout;

void tb_layout_init(TbLayout *layout);
bool tb_layout_arrange(TbLayout *layout, TbRect area, int focused);
bool tb_layout_has_pane(const TbLayout *layout, int pane);
int tb_layout_root(const TbLayout *layout, int pane);
int tb_layout_pane_at(const TbLayout *layout, int x, int y);
/* Split requires enough currently visible space; failure leaves the tree intact. */
int tb_layout_split(TbLayout *layout, int pane, TbSplit direction);
/* Only a nested leaf can close. Returns a surviving sibling pane, or -1. */
int tb_layout_close(TbLayout *layout, int pane);
int tb_layout_divider_at(const TbLayout *layout, int x, int y);
bool tb_layout_move_divider(TbLayout *layout, int divider, int position);
bool tb_rect_contains(TbRect rect, int x, int y);

#endif
