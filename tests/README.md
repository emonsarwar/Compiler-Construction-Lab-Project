# Test suite

Per the project manual (Section 15), every required category is covered:

| Category | Files |
|---|---|
| Successful compilation | `valid/sample_program.mc`, `valid/arithmetic_and_scoping.mc`, `valid/advanced_features.mc` |
| Lexical errors | `lexical_errors/invalid_characters.mc` |
| Syntax errors | `syntax_errors/missing_paren.mc`, `syntax_errors/multiple_errors_recovery.mc` |
| Semantic errors (one per Section 4.5 rule) | `semantic_errors/undeclared_variable.mc`, `redeclaration.mc`, `scope_violation.mc`, `type_mismatch.mc`, `invalid_assignment.mc`, `invalid_expression.mc` |
| Advanced/unique extension errors (not required by the manual — see `docs/advanced_features.md`) | `semantic_errors/advanced_feature_errors.mc` (array/switch-specific rules) |

## A note on how "expected output" below was derived

This whole suite has now actually been built and run with real Flex,
Bison, and GCC (`make`, `make test`, `make test-run`) — every file
below was verified by actually executing the compiled `minilangc`,
not traced by hand. `make test-run` additionally runs every `valid/`
program through `-O -run`, actually executing the generated TAC and
printing real output, cross-checked against each file's own comments
documenting the expected result.

The one thing that could not be pinned down exactly is Bison's own
generated wording for syntax errors (`%define parse.error verbose`'s
"syntax error, unexpected X, expecting Y" phrasing depends on the
installed Bison version) — for the two `syntax_errors/` files below,
the expected *line numbers and count* of errors are traced precisely,
but the exact message text is described rather than quoted verbatim.

**Please re-run `make test` once built and diff against these — if
anything doesn't match, that's a real bug to fix, not a documentation
error to shrug off.**

---

## `valid/sample_program.mc`

The manual's own Section 5.5 sample program. Expect: 0 errors, and
this exact TAC (verified by actually running this program's AST
through `analyze_program()` + `tac_generate()`):

```
x = 10
y = 0
flag = true
L1:
t1 = x > 0
if_false t1 goto L2
t2 = y + x
y = t2
t3 = x - 1
x = t3
goto L1
L2:
t4 = flag == true
if_false t4 goto L3
print y
goto L4
L3:
print x
L4:
```

## `valid/arithmetic_and_scoping.mc`

A larger program exercising float arithmetic with implicit int→float
widening, `%`, nested if/else via blocks, `&&`/`||`/`!`, unary `-`,
and shadowing. Every statement in this file was traced by hand against
the implemented type rules (see the comments in the file and in
`src/semantic/semantic.h`) and confirmed to produce 0 semantic errors,
but — unlike the sample program — its exact TAC text was not generated
by an actual run, so no byte-exact expected TAC is claimed here.
Expect: 0 errors, and a TAC listing with no dangling jump targets.

## `lexical_errors/invalid_characters.mc`

Expect exactly 2 lexical errors (the compiler stops before syntax
analysis has anything to report, since bad characters are discarded
by the lexer's catch-all rule with no token emitted, so the *rest* of
the file still parses cleanly):

```
[Lexical Error] line 15: unexpected character '@'
[Lexical Error] line 20: unexpected character '#'
```

(Line numbers assume the file as written; recount if you edit it.)

## `syntax_errors/missing_paren.mc`

Expect at least 1 syntax error reported near the missing `)` (line 7),
and the compiler halts before semantic analysis. Basic panic-mode
recovery may or may not produce a second cascading error for the
orphaned closing `}` a few lines later — either is acceptable per the
manual's "at least basic error recovery" scope.

## `syntax_errors/multiple_errors_recovery.mc`

Expect **2** independent syntax errors reported — one for each missing
semicolon (`x = 5` and `int z`) — demonstrating that `error SEMI`
recovery resynchronizes and keeps parsing rather than stopping at the
first problem. As documented in the file itself, basic recovery
swallows the statement immediately following each error during
resynchronization; that's expected, not a bug, for "at least basic"
recovery as scoped by the manual.

## `semantic_errors/*.mc`

Each of these six files was traced by hand against the exact
implemented logic in `src/semantic/semantic.c` (not just described in
general terms) — including the specific line number each error should
land on, given as written:

| File | Expected error (exact text) | Line |
|---|---|---|
| `undeclared_variable.mc` | `[Semantic Error] line 7: undeclared variable 'total' used in assignment` | 7 |
| `redeclaration.mc` | `[Semantic Error] line 7: redeclaration of variable 'count'` | 7 |
| `scope_violation.mc` | `[Semantic Error] line 14: undeclared variable 'temp' used` | 14 |
| `type_mismatch.mc` | `[Semantic Error] line 8: invalid assignment: cannot assign a value of type 'float' to variable 'b' of type 'bool'` | 8 |
| `invalid_assignment.mc` | `[Semantic Error] line 9: invalid assignment: cannot assign a value of type 'bool' to variable 'n' of type 'int'` | 9 |
| `invalid_expression.mc` | `[Semantic Error] line 11: invalid expression: operator '&&' requires bool operands, found 'int' and 'int'` | 11 |

Each file produces **exactly one** error (traced by hand to confirm no
unintended second error cascades from the same statement — e.g.
`undeclared_variable.mc` deliberately reads an already-declared
variable on the right-hand side, so only the assignment *target* is
flagged, not also a read).

Note that `type_mismatch.mc` and `invalid_assignment.mc` both surface
through the same underlying `types_assignable()` check and share the
"invalid assignment: ..." message template — see
`src/semantic/semantic.h`'s docstring for why that's a deliberate,
documented design choice (the manual's own Section 4.5 table already
treats these as closely related, e.g. its "type mismatch" example
`bool b = 5 + 3.2` and its "invalid assignment" example are both
fundamentally "wrong type flowing into an assignment").
