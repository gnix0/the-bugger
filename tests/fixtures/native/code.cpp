volatile int code_sideeffects;
struct CodeTrap {
    CodeTrap *operator&() { ++code_sideeffects; return this; }
};
CodeTrap code_object;
int code_callee(int value) { return value + 1; }
int main()
{
    volatile int code_result = code_callee(3);
    return code_result == 4 ? 0 : 1;
}
