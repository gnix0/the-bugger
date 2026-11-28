#include "../shared.h"

namespace symbol_fixture {

static volatile int duplicate_counter = 1;
volatile int Device::state = 5;

int Device::method(int value) const
{
    int adjusted = adjust(value);
    return adjusted + state + duplicate_counter;
}


int left_value()
{
    Device device;
    return device.method(duplicate_counter);
}

}
