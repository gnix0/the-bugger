#ifndef THE_BUGGER_LAYOUT_H
#define THE_BUGGER_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

enum {
    TB_DEFAULT_PANES   = 6,
    TB_MAX_PANES       = 32,
    TB_LAYOUT_NODES    = 64,
    TB_MIN_PANE_WIDTH  = 20,
    TB_MIN_PANE_HEIGHT = 7
};

typedef struct {
    int x;
    int y;
    int w;
    int h;
} TbRect_t;

typedef enum {
    TB_SPLIT_RIGHT,
    TB_SPLIT_BELOW
} TbSplit_e;

typedef struct {
    bool      used;
    int       pane;
    int       first;
    int       second;
    int       ratio;
    TbSplit_e direction;
} TbLayoutNode_t;

typedef struct {
    TbRect_t  rect;
    TbRect_t  area;
    int       node;
    int       minimum_first;
    int       minimum_second;
    TbSplit_e direction;
} TbDivider_t;

typedef struct {
    TbLayoutNode_t nodes[TB_LAYOUT_NODES];
    int            roots[TB_DEFAULT_PANES];
    int            columns[3];
    int            row_ratio;
    TbRect_t       panes[TB_MAX_PANES];
    TbRect_t       area;
    TbDivider_t    dividers[TB_MAX_PANES];
    int            divider_count;
    int            order[TB_MAX_PANES];
    int            count;
    uint64_t       serial;
    uint64_t       identities[TB_MAX_PANES];
    int            widths[3];
    int            minimum_columns[3];
    int            minimum_rows[2];
    bool           tiled;
    /* Lower roots retain their split trees while hidden or over the primary
     * roots. Preferred heights survive temporary terminal-size clamping. */
    bool     overlays;
    bool     lower_open[3];
    int      lower_height[3];
    int      lower_order[3];
    int      root_focus[TB_DEFAULT_PANES];
    int      primary_focus;
    TbRect_t lower_bars[3];
    TbRect_t lower_areas[3];
} TbLayout_t;

void tb_layout_init(TbLayout_t *layout);
bool tb_layout_arrange(TbLayout_t *layout, TbRect_t area, int focused);
bool tb_layout_has_pane(const TbLayout_t *layout, int pane);
int tb_layout_root(const TbLayout_t *layout, int pane);
int tb_layout_pane_at(const TbLayout_t *layout, int x, int y);

/* Split requires enough currently visible space; failure leaves the tree intact. */
int tb_layout_split(TbLayout_t *layout, int pane, TbSplit_e direction);

/* Only a nested leaf can close. Returns a surviving sibling pane, or -1. */
int tb_layout_close(TbLayout_t *layout, int pane);
int tb_layout_divider_at(const TbLayout_t *layout, int x, int y);
bool tb_layout_move_divider(TbLayout_t *layout, int divider, int position);
bool tb_rect_contains(TbRect_t rect, int x, int y);

#endif // THE_BUGGER_LAYOUT_H
