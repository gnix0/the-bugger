#define _DEFAULT_SOURCE
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

unsigned char memory_buffer[2048] = "THE BUGGER MEMORY: first buffer";
unsigned char memory_other[2048] = "INDEPENDENT MEMORY: second buffer";
unsigned char *memory_edge;
volatile unsigned memory_checkpoint_count;

void memory_checkpoint(void)
{
    ++memory_checkpoint_count;
}

int main(void)
{
    long size = sysconf(_SC_PAGESIZE);
    if (size <= 0) { return 2; }
    unsigned char *mapping = mmap(NULL, (size_t)size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) { return 3; }
    if (munmap(mapping + size, (size_t)size) != 0) { return 4; }
    memory_edge = mapping + size - 32;
    for (unsigned i = 0; i < 32; ++i) { memory_edge[i] = (unsigned char)i; }
    memory_checkpoint();
    memory_buffer[0] = 'Z';
    memory_checkpoint();
    for (;;) {
        const struct timespec wait = {.tv_nsec = 100000000};
        (void)nanosleep(&wait, NULL);
    }
}
