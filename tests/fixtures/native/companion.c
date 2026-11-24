/* Linked metadata fixture; companion tests inspect files without executing it. */
static volatile int counter = 7;

int main(void)
{
    return counter;
}
