/* Disposable F02 address-point fixture; compiled as a debug target only. */
#include <csignal>
#include <cstdint>
volatile unsigned stack_calls;
struct StackObject {
    int value;
    StackObject *operator&() { ++stack_calls; return nullptr; }
};
__attribute__((noinline)) void stack_checkpoint(volatile int *value, StackObject *object, int *reference)
{
    asm volatile("" : : "r"(value), "r"(object), "r"(reference) : "memory");
}
__attribute__((noinline)) static int recurse(int depth)
{
    volatile int local = 100 + depth;
    StackObject object{200 + depth};
    int storage = 300 + depth;
    int &reference = storage;
    StackObject *object_address = __builtin_addressof(object);
#define object stack_macro_must_not_expand()
    if (depth) { local += recurse(depth - 1); }
    else { stack_checkpoint(&local, object_address, &reference); }
#undef object
    return local + object.value + reference;
}
int main()
{
    int result = recurse(3);
    return result == 0;
}
