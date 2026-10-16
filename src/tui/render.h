#ifndef THE_BUGGER_RENDER_H
#define THE_BUGGER_RENDER_H

#include "demo.h"
#include "theme.h"

void tb_text(int y, int x, int width, TbStyle style, attr_t attributes, const char *text);
void tb_fill(TbRect rect, TbStyle style);
void tb_render_demo(const TbDemo *demo, int pane, TbRect rect);

#endif
