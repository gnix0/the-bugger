#define _POSIX_C_SOURCE 200809L
#include <time.h>

volatile unsigned watched_value = 17;
volatile unsigned watched_sink;
unsigned char watched_wide[256];
void watch_checkpoint(unsigned stage);
void watch_scope(void);
__attribute__((noinline)) void watch_checkpoint(unsigned stage)
{
    watched_sink = stage;
}
__attribute__((noinline)) void watch_scope(void)
{
    volatile unsigned local = 10;
    watch_checkpoint(1);
    local = 11;
    watched_sink = local;
}
int main(void)
{
    watch_checkpoint(0);
    watched_value = 20;
    watched_sink = watched_value;
    watched_value = 21;
    watch_scope();
    watch_checkpoint(2);
    for (;;) {
        const struct timespec wait = {0, 100000000};
        (void)nanosleep(&wait, NULL);
    }
}
