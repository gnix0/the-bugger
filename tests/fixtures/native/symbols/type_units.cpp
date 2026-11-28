namespace type_unit_fixture {

struct Device {
    int method(int value) const &;
    int method(int value) &&;
    static int static_method(int value);
};

int Device::method(int value) const &
{
    return value + 1;
}


int Device::method(int value) &&
{
    return value + 2;
}


int Device::static_method(int value)
{
    return value + 3;
}

}

int main()
{
    type_unit_fixture::Device device;
    int result = device.method(1) + static_cast<type_unit_fixture::Device &&>(device).method(2) +
                 type_unit_fixture::Device::static_method(3);

    return result == 12 ? 0 : 1;
}
