#include "shared.h"

volatile int symbol_result;

int main()
{
    symbol_result = symbol_fixture::left_value() + symbol_fixture::right_value() +
                    symbol_fixture::overload(1) + static_cast<int>(symbol_fixture::overload(2.0));
    return symbol_result == 26 ? 0 : 1;
}
