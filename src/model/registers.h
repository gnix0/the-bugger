#ifndef THE_BUGGER_REGISTERS_H
#define THE_BUGGER_REGISTERS_H

#include "context.h"

enum { TB_REGISTERS_MAX = 512, TB_REGISTERS_PAGE = 32, TB_REGISTER_VALUE = 1024 };
typedef enum { TB_REG_HEX, TB_REG_DECIMAL, TB_REG_OCTAL, TB_REG_BINARY,
               TB_REG_RAW, TB_REG_NATURAL, TB_REG_FORMATS } TbRegisterFormat;
typedef struct {
    unsigned number;
    char name[96], value[TB_REGISTER_VALUE];
    bool available, changed, truncated, fresh;
} TbRegister;
typedef struct TbRegisters TbRegisters;
TbRegisters *tb_registers_create(TbGdb *gdb, TbContext *context);
void tb_registers_destroy(TbRegisters *registers);
void tb_registers_reset(TbRegisters *registers);
bool tb_registers_event(TbRegisters *registers, const TbGdbEvent *event);
void tb_registers_pump(TbRegisters *registers);
/* Begin a render pass, then request each visible pane's row range. */
void tb_registers_visible(TbRegisters *registers, bool visible);
void tb_registers_begin(TbRegisters *registers);
void tb_registers_want(TbRegisters *registers, size_t first, size_t count);
void tb_registers_refresh(TbRegisters *registers);
void tb_registers_format(TbRegisters *registers, TbRegisterFormat format);
TbRegisterFormat tb_registers_get_format(const TbRegisters *registers);
const char *tb_registers_format_name(TbRegisterFormat format);
size_t tb_registers_count(const TbRegisters *registers);
const TbRegister *tb_registers_row(const TbRegisters *registers, size_t row);
bool tb_registers_settled(const TbRegisters *registers);
bool tb_registers_stale(const TbRegisters *registers);
bool tb_registers_limited(const TbRegisters *registers);
const char *tb_registers_error(const TbRegisters *registers);
uint64_t tb_registers_revision(const TbRegisters *registers);

#endif
