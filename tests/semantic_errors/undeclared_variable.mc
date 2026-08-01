// Semantic error test: undeclared variable use.
// `total` is never declared anywhere.

int x;
x = 5;

total = x;   // 'total' was never declared

print x;
