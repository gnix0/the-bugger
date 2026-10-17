#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static atomic_int ready_workers;
static atomic_bool release_workers;
static volatile int result;

static inline void context_checkpoint(void)
{
    puts("CONTEXT checkpoint: main recursion and two worker threads");
    (void)raise(SIGTRAP);
}

static int recursive(int depth, int worker)
{
    if (depth) { int nested = recursive(depth - 1, worker); return nested + depth; }
    if (worker) {
        atomic_fetch_add(&ready_workers, 1);
        while (!atomic_load(&release_workers)) {
            const struct timespec delay = {.tv_nsec = 10000000};
            (void)nanosleep(&delay, NULL);
        }
    } else {
        context_checkpoint();
    }
    return worker;
}
static int tail_bridge(int depth)
{
    return recursive(depth, 0);
}
static void *worker_main(void *unused)
{
    (void)unused;
    (void)recursive(4, 1);
    return NULL;
}
int main(int argc, char *argv[])
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    pthread_t workers[2];
    for (int i = 0; i < 2; ++i) { if (pthread_create(&workers[i], NULL, worker_main, NULL) != 0) { return 2; } }
    while (atomic_load(&ready_workers) < 2) {
        const struct timespec delay = {.tv_nsec = 1000000}; (void)nanosleep(&delay, NULL);
    }
    result = tail_bridge(argc > 1 && strcmp(argv[1], "--deep") == 0 ? 70 : 6);
    atomic_store(&release_workers, 1);
    for (int i = 0; i < 2; ++i) { if (pthread_join(workers[i], NULL) != 0) { return 3; } }
    printf("CONTEXT workers joined; result=%d\n", result);
    (void)raise(SIGTRAP);
    for (;;) {
        const struct timespec delay = {.tv_sec = 1}; (void)nanosleep(&delay, NULL);
    }
}
