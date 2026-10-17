#include <cstdio>
#include <string>

int main(int argc, char *argv[])
{
    std::string value = argc > 1 ? argv[1] : "C++ fixture";
    std::printf("CPP=<%s> sum=42\n", value.c_str());
    return 0;
}
