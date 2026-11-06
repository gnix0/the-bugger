#ifndef THE_BUGGER_RENDER_H
#define THE_BUGGER_RENDER_H

#include "tui/demo.h"
#include "tui/theme.h"

/* Draw within one row and width display cells. Controls/invalid bytes are inert
 * escaped text; a wide character is omitted if it cannot fit completely. */
void tb_text(int y, int x, int width, TbStyle_e style, attr_t attributes, const char *text);
void tb_fill(TbRect_t rect, TbStyle_e style);
/* Join shared edges; preserving content keeps exterior frames clear of existing chrome. */
void tb_border(TbRect_t rect, TbStyle_e style, bool preserve_content);
/* Derive shared exterior edges from geometry, independent of paint order. */
void tb_pane_frames(const TbRect_t *panes, size_t count, TbRect_t active);
/* Position a bounded list against its actual trigger, inside usable screen bounds. */
TbRect_t tb_dropdown_rect(TbRect_t trigger, TbRect_t bounds, int width, int count);
void tb_render_scrollbar(TbRect_t rect, TbScroll_t scroll, bool horizontal);
void tb_render_demo(const TbDemo_t *demo, int pane, TbRect_t rect);
void tb_render_source_text(int y, int x, int width, const char *code, bool selected);
void tb_render_source_slice(int y, int x, int width, const char *code, int scroll, bool selected);

typedef struct {
    int address;
    int bytes;
    int mnemonic;
} TbAssemblyColumns_t;

void tb_format_assembly(char *out, size_t capacity, TbAssemblyColumns_t columns, const char *address,
                        const char *bytes, const char *instruction);

#endif // THE_BUGGER_RENDER_H
