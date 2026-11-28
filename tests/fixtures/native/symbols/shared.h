#ifndef SYMBOL_FIXTURE_SHARED_H
#define SYMBOL_FIXTURE_SHARED_H

namespace symbol_fixture {

struct Device {
    int                 method(int value) const;
    static volatile int state;
};

inline int adjust(int value)
{
    return value + 3;
}


int left_value();
int right_value();
int overload(int value);
double overload(double value);

}

#endif // SYMBOL_FIXTURE_SHARED_H
