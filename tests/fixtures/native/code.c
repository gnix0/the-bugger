#define _POSIX_C_SOURCE 200809L
#include <time.h>
volatile int code_result;

int code_callee(int value)
{
    volatile int code_local = value + 3;
    code_result = code_local;
    return code_local * 2;
}

int main(void)
{
    volatile int code_counter = 1; /* CODE_FIND_MARKER */
    code_counter += 2;
    code_result = code_callee(code_counter);
    code_result += 1;
    for (;;) {
        const struct timespec delay = {.tv_nsec = 100000000};
        (void)nanosleep(&delay, NULL);
    }
}
