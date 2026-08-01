// Syntax error test: two INDEPENDENT missing-semicolon violations,
// far apart in the file. This demonstrates that error recovery
// (Section 4.2: "a single syntax error does not necessarily halt the
// entire compilation immediately without any diagnostic output")
// actually resynchronizes and keeps looking, rather than reporting
// only the first problem and giving up.
//
// Basic panic-mode recovery (resync at the next ';') isn't perfect —
// the statement immediately following each error gets swallowed
// during resynchronization, which is expected for "at least basic"
// recovery. What matters is that BOTH independent errors are still
// detected and reported (with their own line numbers), not just the
// first one.

int x;
x = 5

int y;
y = 10;

int z
z = 20;

print x;
print y;
print z;
