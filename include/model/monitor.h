#ifndef THE_BUGGER_MONITOR_H
#define THE_BUGGER_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Text framing only; this helper never sends a command or reads a target.
 * Feed decoded MI @ stream bytes for one serialized request. The caller must
 * correlate the terminal MI result token before accepting finish(done=true). */
typedef struct {
    char     id[64];
    char     target[64];
    char     line[256];
    size_t   used;
    size_t   received;
    uint64_t address;
    uint64_t value;
    unsigned width;
    bool     invalid;
    bool     found;
    bool     discard;
    bool     cr;
} TbMonitorFrame_t;

bool tb_monitor_start(TbMonitorFrame_t *frame, const char *id, const char *target, uint64_t address,
                      unsigned width);
void tb_monitor_feed(TbMonitorFrame_t *frame, const char *bytes, size_t length);
bool tb_monitor_finish(const TbMonitorFrame_t *frame, bool done, uint64_t *value);

#endif // THE_BUGGER_MONITOR_H
