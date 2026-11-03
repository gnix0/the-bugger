#ifndef THE_BUGGER_MONITOR_H
#define THE_BUGGER_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Text framing only; this helper never sends a command or reads a target.
 * Feed decoded MI @ stream bytes for one serialized request. The caller must
 * correlate the terminal MI result token before accepting finish(done=true). */
typedef struct {
    char     id[64], target[64], line[256];
    size_t   used, received;
    uint64_t address, value;
    unsigned width;
    bool     invalid, found, discard, cr;
} TbMonitorFrame_t;

bool tb_monitor_start(TbMonitorFrame_t *frame, const char *id, const char *target, uint64_t address,
                      unsigned width);
void tb_monitor_feed(TbMonitorFrame_t *frame, const char *bytes, size_t length);
bool tb_monitor_finish(const TbMonitorFrame_t *frame, bool done, uint64_t *value);

#endif // THE_BUGGER_MONITOR_H
