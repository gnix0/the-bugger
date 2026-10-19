#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <sys/prctl.h>
#include <time.h>
char remote_buffer[64] = "REMOTE bounded memory";
volatile unsigned remote_counter = 17;
int main(void)
{
    if (prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0) != 0) { return 2; }
    (void)setvbuf(stdout, NULL, _IONBF, 0); puts("REMOTE fixture ready");
    for (;;) {
        const struct timespec delay = {.tv_nsec = 10000000};
        (void)nanosleep(&delay, NULL); ++remote_counter;
    }
}
