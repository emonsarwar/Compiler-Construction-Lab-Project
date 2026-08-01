// Semantic error test: invalid assignment.
// The manual's own example (Section 4.5's table): "Assigning a bool
// expression to an int variable, or similar."

int n;
bool isReady;
isReady = true;

n = isReady;   // isReady is bool; assigning it to int n is invalid

print n;
