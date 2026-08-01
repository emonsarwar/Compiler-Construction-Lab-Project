// Semantic error test: invalid expression.
// The manual's own example (Section 4.5's table): "applying logical
// operators to numeric operands where not permitted."

int a;
int b;
bool result;
a = 5;
b = 3;

result = a && b;   // '&&' requires bool operands; a and b are int

print result;
