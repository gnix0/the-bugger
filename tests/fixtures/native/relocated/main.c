int left_value(void);
int right_value(void);
volatile int relocation_result;
int main(void)
{
    relocation_result = left_value();
    relocation_result += right_value();
    return relocation_result == 33 ? 0 : 1;
}
