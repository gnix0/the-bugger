#include <stdint.h>
#include <dlfcn.h>
volatile unsigned m08_counter;
volatile unsigned m08_result;
const unsigned char m08_zeros[128] = {0};

int m08_tick(unsigned value)
{
    m08_result = value + 1;
    return (int)m08_result;
}
void m08_long(void)
{
    __asm__ volatile(".rept 4096\n nop\n .endr");
}
int main(int argc, char **argv)
{
    if (argc > 1 && !dlopen(argv[1], RTLD_NOW)) { return 2; }
    m08_long();
    for (m08_counter = 0; m08_counter < 100; ++m08_counter) {
        m08_result = (unsigned)m08_tick(m08_counter);
    }
    for (;;) { m08_result ^= 1; }
}
