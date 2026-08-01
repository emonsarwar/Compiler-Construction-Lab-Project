// Syntax error test: a single, obvious grammar violation.
// The `if` condition is missing its closing parenthesis.

int x;
x = 5;

if (x > 0 {
    print x;
}
