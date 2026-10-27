#ifndef THE_BUGGER_MMIO_H
#define THE_BUGGER_MMIO_H
#include "controller.h"

typedef enum { TB_MMIO_IDLE, TB_MMIO_PENDING, TB_MMIO_COMPLETE, TB_MMIO_FAILED, TB_MMIO_UNCERTAIN } TbMmioStatus;
typedef struct {
    TbMmioStatus status;
    uint64_t address, value;
    unsigned width;
    TbGdbGeneration generation;
    char error[192];
} TbMmioResult;
typedef struct TbMmio TbMmio;
TbMmio *tb_mmio_create(TbGdb *gdb);
/* Refuses to abandon an outstanding request on a live debugger. */
bool tb_mmio_destroy(TbMmio *mmio);
/* Transport only. Caller must establish target/width capability and explicit
 * per-view range consent immediately before calling. Never retries or polls. */
bool tb_mmio_read(TbMmio *mmio, const char *target, uint64_t address, unsigned width);
/* Feed from the controller callback; never mutates the controller. Stream
 * records remain available to the normal console logger. */
bool tb_mmio_event(TbMmio *mmio, const TbGdbEvent *event);
void tb_mmio_pump(TbMmio *mmio);
const TbMmioResult *tb_mmio_result(const TbMmio *mmio);
bool tb_mmio_busy(const TbMmio *mmio);
#endif
