#include "workload.h"

const char fixture_flash[] = "THE BUGGER M17 embedded fixture";
char fixture_buffer[64] = "M17 SRAM: hex and ASCII must agree";
volatile uint32_t fixture_counter;
volatile uint32_t fixture_sink;
FixtureSample fixture_sample = {.count = 17, .samples = {1, 2, 3, 4, 5, 6, 7, 8}, .label = fixture_flash};
uint32_t *fixture_pointer = fixture_sample.samples;

__attribute__((noinline)) void fixture_checkpoint(uint32_t stage)
{
    fixture_sink = stage;
    __asm__ volatile ("nop" ::: "memory");
}

__attribute__((noinline)) uint32_t fixture_callee(uint32_t input)
{
    uint32_t local = input + 3;
    fixture_checkpoint(local);
    fixture_sample.samples[input & 7U] = local;
    return local;
}

__attribute__((noinline)) void fixture_cycle(void)
{
    uint32_t current = fixture_counter;
    fixture_sample.count = fixture_callee(current);
    fixture_sink = fixture_pointer[current & 7U];
    fixture_counter = current + 1U;
    fixture_buffer[0] = (current & 1U) ? 'M' : 'm';
    fixture_checkpoint(1);
}
