// Valid program #1 — the manual's own sample program (Section 5.5).
// Exercises: int/bool declarations, assignment, while, relational
// expressions, if-else, equality comparison, print, nested blocks.

int x;
int y;
bool flag;

x = 10;
y = 0;
flag = true;

while (x > 0) {
    y = y + x;
    x = x - 1;
}

if (flag == true) {
    print y;
} else {
    print x;
}
