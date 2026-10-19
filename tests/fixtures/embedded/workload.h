#ifndef TB_FIXTURE_WORKLOAD_H
#define TB_FIXTURE_WORKLOAD_H
#include <stdint.h>
typedef struct { uint32_t count; uint32_t samples[8]; const char *label; } FixtureSample;
extern const char fixture_flash[];
extern char fixture_buffer[64];
extern volatile uint32_t fixture_counter, fixture_sink;
extern FixtureSample fixture_sample;
extern uint32_t *fixture_pointer;
void fixture_checkpoint(uint32_t stage);
uint32_t fixture_callee(uint32_t input);
void fixture_cycle(void);
#endif
