#ifndef THE_BUGGER_RENDER_H
#define THE_BUGGER_RENDER_H

#include "demo.h"
#include "theme.h"

/* Draw within one row and width display cells. Controls/invalid bytes are inert
 * escaped text; a wide character is omitted if it cannot fit completely. */
void tb_text(int y, int x, int width, TbStyle style, attr_t attributes, const char *text);
void tb_fill(TbRect rect, TbStyle style);
void tb_render_demo(const TbDemo *demo, int pane, TbRect rect);
void tb_render_source_text(int y, int x, int width, const char *code, bool selected);
typedef struct { int address, bytes, mnemonic; } TbAssemblyColumns;
void tb_format_assembly(char *out, size_t capacity, TbAssemblyColumns columns,
                        const char *address, const char *bytes, const char *instruction);

#endif
