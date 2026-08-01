// Semantic error test: scope violation.
// `temp` is declared inside the if-block, so it must not be visible
// after that block ends.

bool flag;
flag = true;

if (flag) {
    int temp;
    temp = 42;
    print temp;   // fine: still inside the block where `temp` lives
}

print temp;   // scope violation: `temp` does not exist out here
