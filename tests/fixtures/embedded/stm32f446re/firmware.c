#include "../workload.h"

/* This opt-in affects only the fixture's PA5 LED, never frontend policy. */
#ifndef TB_FIXTURE_LED
#define TB_FIXTURE_LED 0
#endif

int main(void);

int main(void)
{
#if TB_FIXTURE_LED
    /* F446 RCC_AHB1ENR / GPIOA MODER / BSRR; PA5 is Nucleo LD2.
     * Build this variant only with PA5 disconnected from external circuitry. */
    volatile uint32_t *const clock = (volatile uint32_t *)UINT32_C(0x40023830);
    volatile uint32_t *const mode = (volatile uint32_t *)UINT32_C(0x40020000);
    volatile uint32_t *const output = (volatile uint32_t *)UINT32_C(0x40020018);
    *clock |= 1U;
    (void)*clock;
    *mode = (*mode & ~(3U << 10)) | (1U << 10);
#endif
    fixture_checkpoint(0);
    for (;;) {
        fixture_cycle();
#if TB_FIXTURE_LED
        *output = (fixture_counter & 1U) ? (1U << 5) : (1U << 21);
#endif
        for (volatile uint32_t wait = 0; wait < 100000U; ++wait) { __asm__ volatile ("nop"); }
    }
}
