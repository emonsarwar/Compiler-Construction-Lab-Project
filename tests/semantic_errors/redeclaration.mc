// Semantic error test: redeclaration.
// `count` is declared twice in the same (global) scope.

int count;
count = 0;

int count;   // redeclaration of 'count' in the same scope

print count;
