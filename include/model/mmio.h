#ifndef THE_BUGGER_MMIO_H
#define THE_BUGGER_MMIO_H

#include "gdb/controller.h"

typedef enum {
    TB_MMIO_IDLE,
    TB_MMIO_PENDING,
    TB_MMIO_COMPLETE,
    TB_MMIO_FAILED,
    TB_MMIO_UNCERTAIN
} TbMmioStatus_e;

typedef struct {
    TbMmioStatus_e    status;
    uint64_t          address;
    uint64_t          value;
    unsigned          width;
    TbGdbGeneration_t generation;
    char              error[192];
} TbMmioResult_t;

typedef struct tb_mmio TbMmio_t;

TbMmio_t *tb_mmio_create(TbGdb_t *gdb);
/* Refuses to abandon an outstanding request on a live debugger. */
bool tb_mmio_destroy(TbMmio_t *mmio);
/* Transport only. Caller must establish target/width capability and explicit
 * per-view range consent immediately before calling. Never retries or polls. */
bool tb_mmio_read(TbMmio_t *mmio, const char *target, uint64_t address, unsigned width);
/* Feed from the controller callback; never mutates the controller. Stream
 * records remain available to the normal console logger. */
bool tb_mmio_event(TbMmio_t *mmio, const TbGdbEvent_t *event);
void tb_mmio_pump(TbMmio_t *mmio);
const TbMmioResult_t *tb_mmio_result(const TbMmio_t *mmio);
bool tb_mmio_busy(const TbMmio_t *mmio);

#endif // THE_BUGGER_MMIO_H
