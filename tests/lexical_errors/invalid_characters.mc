// Lexical error test: contains characters that aren't part of any
// valid token in the language. Each is reported as a Lexical Error
// with its line number, and the lexer keeps scanning afterward rather
// than stopping at the first bad character (Section 4.1: "must be
// reported as a lexical error with line number").
//
// The invalid characters are placed BETWEEN complete statements (not
// inside one) so that after they're discarded, the rest of the
// program still parses cleanly -- this isolates the demonstration to
// lexical errors only, with no syntax errors as a side effect.

int x;
x = 5;

@

int y;
y = 10;

#

print x;
print y;
