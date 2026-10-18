#include <stddef.h>
#include <time.h>

struct Sample { int count; unsigned flags; char text[12]; int values[100]; };
struct Sample variables_sample = {17, 0xa5, "the-bugger", {1, 2, 3, 4}};
int variables_global = 17;
volatile unsigned variables_volatile = 0x1234;
union Choice { unsigned number; unsigned char bytes[4]; } variables_union = {0x12345678};

void variables_ready(int stage);
void variables_checkpoint(int seed);

__attribute__((noinline)) void variables_ready(int stage)
{
    /* Set a breakpoint here. Caller locals are completely initialized. */
    variables_volatile += (unsigned)stage;
}

__attribute__((noinline)) void variables_checkpoint(int seed)
{
    int scalar = seed;
    struct Sample aggregate = variables_sample;
    struct Sample *pointer = &aggregate;
    struct Sample *null_pointer = NULL;
    int array[100];
    for (int i = 0; i < 100; ++i) { array[i] = i; }
#ifdef __cplusplus
    struct ClassValue { int field; struct Sample nested; } object = {23, aggregate};
    int &reference = scalar;
#endif
    variables_ready(scalar);
    scalar += 3; pointer->count += 3; variables_global += 3;
    variables_ready(scalar);
    variables_global += array[3] + (null_pointer == NULL);
#ifdef __cplusplus
    variables_global += object.field + reference;
#endif
}
int main(void)
{
    variables_checkpoint(17);
    for (;;) {
        const struct timespec wait = {0, 100000000};
        (void)nanosleep(&wait, NULL);
    }
}
