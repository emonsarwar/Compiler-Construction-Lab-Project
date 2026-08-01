# Design

This document explains the architecture and the specific design
decisions behind each module — the "why", to complement the grammar
in `grammar.md` and the inline comments in the source itself. It's
meant to be a starting point for the project report's Compiler
Architecture / Lexer Design / Parser Design / AST / Semantic Analysis
/ Symbol Table / Intermediate Code chapters (see `report_template.md`).

## Pipeline

```
source.mc
   |
   v
Lexer (src/lexer/lexer.l, Flex)        source text -> token stream
   |
   v
Parser (src/parser/parser.y, Bison)    tokens -> AST, building it via
   |                                    constructor calls in grammar
   |                                    actions (src/ast/ast.h)
   v
AST (src/ast/ast.h, ast.c)
   |
   v
Semantic Analyzer (src/semantic/)      walks the AST, populates the
   |                                    Symbol Table, annotates every
   |                                    expression node's `type`
   v
Symbol Table (src/symbol_table/)       nested scopes, used BY the
                                        semantic analyzer (not a
                                        separate pass)
   |
   v
Annotated / Validated AST
   |
   v
TAC Generator (src/codegen/tac.c)      walks the (now-typed) AST,
   |                                    emits Three Address Code
   v
TAC output
```

`src/main.c` drives all of this: parse, stop if lex/syntax errors,
print the AST, run semantic analysis, stop if semantic errors, print
the annotated AST again (types now filled in), generate + print TAC.
See `src/common/error.h`/`error.c` for the one shared error-reporting
function every phase calls, which is what lets `main.c` make that
"stop or continue" decision with a single `error_count()` check after
each phase.

## Lexer design

Token categories map directly to Section 5.4 of the manual. The one
design choice worth calling out: **keyword patterns are listed before
the generic identifier pattern** in `lexer.l` (the manual's own tip).
Flex's matching rule is "longest match wins; ties broken by whichever
rule appears first in the file" — `"int"` (a literal-string pattern)
and the identifier pattern `[a-zA-Z_][a-zA-Z0-9_]*` both match the
text `int` with the same length (3 characters), so listing `"int"`
first is what makes it win the tie over being tokenized as a plain
identifier. An alternative, also-standard approach is to tokenize
everything alphabetic as `IDENT` and have the *parser* (or a
symbol-table lookup in the lexer) distinguish keywords — this project
uses the simpler ordering-based approach since it needs no extra
lookup structure and is exactly what the manual's own tip suggests.

Block comments use the standard Flex idiom
`"/*"([^*]|\*+[^*/])*\*+"/"` rather than a naive `.*` pattern, which
would incorrectly consume through an embedded `*` (e.g. `/* a * b */`)
or greedily span multiple unrelated comments. Invalid characters fall
through to a catch-all `.` rule that reports a Lexical Error and
**discards** the character rather than aborting — the lexer keeps
scanning afterward, which is what lets a single bad character not
take down the whole compilation (see `tests/lexical_errors/`).

## Parser design

See `grammar.md` for the full formal CFG and the reasoning for why it
has no shift/reduce conflicts (operator-precedence layering + mandatory
braces eliminating dangling-else). Error recovery uses Bison's
standard `error` token at the statement level:

```
stmt: ... | error SEMI { yyerrok; $$ = NULL; }
```

When a syntax error occurs mid-statement, Bison unwinds the parse
stack looking for a state where `error` can be shifted, then discards
tokens until it finds the `;` this rule expects, and resumes normal
parsing from there. `yyerrok` resets Bison's internal error-suppression
state so a *second*, independent error later in the file is also
reported (without it, Bison suppresses further error messages until
it has successfully shifted a few tokens, to avoid cascades of noise
from a single root problem) — see `tests/syntax_errors/
multiple_errors_recovery.mc`, which is specifically designed to prove
this works (two independent missing-semicolons, both detected).

This is genuinely "basic" recovery, not sophisticated: the statement
immediately following an error gets swallowed while the parser
resyncs at the next `;`. That's an accepted, documented limitation —
the manual asks for "at least basic error recovery," and the important
property (one error doesn't hide a second, independent one) holds.

## AST design

One `ASTNode` struct (`src/ast/ast.h`), tagged by a `kind` enum, with
a C `union` holding only the fields relevant to that kind. This is the
standard shape for a hand-written-in-C AST: constructors and the
recursive walkers in `semantic.c`/`tac.c` are all a single
`switch (node->kind)`, and the union avoids wasting memory on fields a
given node never uses (a literal doesn't need `left`/`right` pointers;
a control-flow node doesn't need a numeric value).

Every node carries:
- `line` — the source line, captured from Bison's `yylineno` at the
  point the grammar rule that builds the node reduces. Used by every
  diagnostic.
- `type` — `TYPE_UNKNOWN` until the semantic analyzer fills it in.
  Nothing downstream of semantic analysis (i.e. `tac.c`) ever runs on
  a program that still has semantic errors, so TAC generation never
  needs to handle `TYPE_UNKNOWN`.

AST_PROGRAM and AST_BLOCK share one underlying shape (a growable list
of statement pointers) — a top-level program and a `{ }` block are
structurally the same thing (an ordered sequence of
declarations/statements); `AST_PROGRAM` is just how the top-level one
gets tagged once parsing completes (see `parser.y`'s `program` rule).

## Symbol table design

`src/symbol_table/`: a linked list of `Symbol` entries per `Scope`,
and each `Scope` points to its enclosing (parent) `Scope`.
`symtab_lookup` walks from the current scope outward through parents.

This single mechanism gives two of the six required semantic checks
for free:

- **Redeclaration** is checked against *only the current scope*
  (`symtab_declared_in_current_scope`) — so declaring a new variable
  that happens to share a name with an *outer* scope's variable is
  legal (shadowing), while declaring the same name twice in the *same*
  scope is not.
- **Scope violation** doesn't need a separate check at all. When a
  block ends, `symtab_exit_scope` frees that scope's symbols entirely.
  A later reference to a name that only ever existed in that
  now-closed scope simply fails `symtab_lookup` — indistinguishable,
  by design, from referencing a name that was never declared at all,
  which is the semantically correct behavior (from the enclosing
  code's point of view, that name genuinely isn't in scope). See
  `tests/semantic_errors/scope_violation.mc`.

Fields per `Symbol`: `name`, `type`, `scope_level` (for
diagnostics/debugging), `line_declared` — matching the manual's
Section 4.4 table exactly.

## Semantic analysis: type rules

The manual specifies the required *error categories* (Section 4.5) but
leaves the exact type-compatibility rules to each project. This
project's rules (all in `src/semantic/semantic.c`, and restated in
that file's header comment):

- **`int` widens implicitly to `float`**, in both arithmetic and
  assignment (`n = 5; f = n;` where `f` is `float` is legal; the
  result type of `int + float` is `float`). This is lossless,
  standard widening — the same rule C, Java, and C# use.
- **`float` does NOT narrow implicitly to `int`.** Assigning a
  `float`-typed expression to an `int` variable is a type-mismatch
  error (`tests/semantic_errors/type_mismatch.mc` uses exactly this,
  via the manual's own `bool b = 5 + 3.2` example — `5 + 3.2` is
  `float`, so assigning it to `bool` fails the same way).
- **`bool` never implicitly converts to/from `int` or `float`.**
  There's no "0 is falsy" C-style coercion in this language.
- **`%` requires both operands to be `int`.** There's no obvious
  meaning for float modulo here, and the manual's operator table lists
  `%` as an arithmetic operator without specifying float support, so
  restricting it to int keeps the rule unambiguous.
- **`&&` `||` `!` require `bool` operand(s).** This is what makes the
  manual's own "invalid expression" example — applying a logical
  operator to numeric operands — a detectable error (see
  `invalid_expression.mc`).
- **`<` `>` `<=` `>=` require numeric (`int`/`float`) operands**;
  result is always `bool`.
- **`==` `!=` require both operands numeric, OR both operands
  `bool`** (comparing a number to a bool is a type mismatch); result
  is always `bool`.
- **`if`/`while` conditions must be exactly `bool`.** No C-style
  "any nonzero int is truthy" — this keeps the condition-checking rule
  simple and matches how the manual's own sample program always
  produces genuine bool expressions for conditions (`x > 0`,
  `flag == true`).

**Cascading-error suppression:** every type-checking function treats
`TYPE_UNKNOWN` operands as "already reported, don't check further" —
e.g. `total = total + x;` where `total` is undeclared reports exactly
one error for the undeclared use, not a second, confusing "type
mismatch" error for the resulting `+` expression whose operand type
couldn't be determined. See `types_assignable()`, `check_binop()`,
`check_unaryop()` in `semantic.c`.

## TAC generation design

See `src/codegen/tac.h`'s header comment for the full design
rationale. In brief: `gen_expr()` returns a "place" string (a
variable name, a literal's own text, or a fresh temporary `t1`, `t2`,
...) rather than always emitting an instruction — this is what makes
`a = 5;` compile to exactly one instruction (`a = 5`) instead of a
redundant `t1 = 5; a = t1`, matching the manual's own worked example
exactly (verified byte-for-byte — see `verification/`). Declarations
emit nothing (TAC is about computation, and the manual's own example
confirms `int a; int b; int c;` produce no output lines).

Control flow uses the standard label + conditional-jump scheme:

```
if (cond) { THEN }                    if (cond) { THEN } else { ELSE }
  ---------------------                 ---------------------
  <code for cond>                       <code for cond>
  if_false <cond> goto Lend             if_false <cond> goto Lelse
  <code for THEN>                       <code for THEN>
  Lend:                                 goto Lend
                                         Lelse:
                                         <code for ELSE>
                                         Lend:

while (cond) { BODY }
  ---------------------
  Lstart:
  <code for cond>
  if_false <cond> goto Lend
  <code for BODY>
  goto Lstart
  Lend:
```

**Scope note:** `&&`/`||` are generated as plain eager binary TAC
operations (`t1 = a && b`), not short-circuited into extra jumps. The
manual doesn't require short-circuit evaluation here, and this
language's expressions have no side effects to short-circuit around
(no function calls, no assignment-as-expression), so eager evaluation
is behaviorally identical to short-circuit evaluation for every
program this language can express — keeping every binary operator
generated the same uniform way was judged simpler than adding
control-flow-heavy special-casing for no observable behavior
difference.
