#ifndef THE_BUGGER_REGISTERS_H
#define THE_BUGGER_REGISTERS_H

#include "model/context.h"

enum { TB_REGISTERS_MAX = 512, TB_REGISTERS_PAGE = 32, TB_REGISTER_VALUE = 1024 };
typedef enum {
    TB_REG_HEX,
    TB_REG_DECIMAL,
    TB_REG_OCTAL,
    TB_REG_BINARY,
    TB_REG_RAW,
    TB_REG_NATURAL,
    TB_REG_FORMATS
} TbRegisterFormat_e;

typedef struct {
    unsigned number;
    char     name[96], value[TB_REGISTER_VALUE];
    bool     available, changed, truncated, fresh;
} TbRegister_t;

typedef struct tb_registers TbRegisters_t;

TbRegisters_t *tb_registers_create(TbGdb_t *gdb, TbContext_t *context);
void tb_registers_destroy(TbRegisters_t *registers);
void tb_registers_reset(TbRegisters_t *registers);
bool tb_registers_event(TbRegisters_t *registers, const TbGdbEvent_t *event);
void tb_registers_pump(TbRegisters_t *registers);
/* Begin a render pass, then request each visible pane's row range. */
void tb_registers_visible(TbRegisters_t *registers, bool visible);
void tb_registers_begin(TbRegisters_t *registers);
void tb_registers_want(TbRegisters_t *registers, size_t first, size_t count);
void tb_registers_refresh(TbRegisters_t *registers);
void tb_registers_format(TbRegisters_t *registers, TbRegisterFormat_e format);
TbRegisterFormat_e tb_registers_get_format(const TbRegisters_t *registers);
const char *tb_registers_format_name(TbRegisterFormat_e format);
size_t tb_registers_count(const TbRegisters_t *registers);
const TbRegister_t *tb_registers_row(const TbRegisters_t *registers, size_t row);
bool tb_registers_settled(const TbRegisters_t *registers);
bool tb_registers_stale(const TbRegisters_t *registers);
bool tb_registers_limited(const TbRegisters_t *registers);
const char *tb_registers_error(const TbRegisters_t *registers);
uint64_t tb_registers_revision(const TbRegisters_t *registers);

#endif // THE_BUGGER_REGISTERS_H
