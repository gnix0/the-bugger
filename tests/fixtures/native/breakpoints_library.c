void m08_future(void) __attribute__((constructor));
void m08_future(void)
{
    volatile int value = 7;
    (void)value;
}
