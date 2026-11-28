#include "../shared.h"

namespace symbol_fixture {

static volatile int duplicate_counter = 2;

int right_value()
{
    return adjust(duplicate_counter) + Device::state;
}

}

namespace symbol_fixture {

int overload(int value)
{
    return adjust(value);
}


double overload(double value)
{
    return value + 0.5;
}

}
