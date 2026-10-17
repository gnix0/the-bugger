volatile int m08_cpp_result;
int m08_overload(int value) { return value + 1; }
double m08_overload(double value) { return value + 2; }
int main()
{
    m08_cpp_result = m08_overload(1);
    m08_cpp_result += (int)m08_overload(2.0);
    return 0;
}
