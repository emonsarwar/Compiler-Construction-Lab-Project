// Semantic error test: type mismatch.
// This is the project manual's own example (Section 4.5's table):
// `bool b = 5 + 3.2;` — 5 + 3.2 has type float, which cannot be
// assigned to a bool. This language declares and assigns separately
// (Section 5.2), so the same mismatch is expressed as two statements.

bool b;
b = 5 + 3.2;   // 5 + 3.2 is float; assigning it to bool b is a type mismatch

print b;
