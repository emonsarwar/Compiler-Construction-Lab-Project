# Bonus Features

This document covers the optional bonus features (project manual
Section 14) implemented on top of the required language (Section 5):
**functions** (declarations, calls, parameters, return), **for-loops**,
**do-while loops**, **increment/decrement**, and **Graphviz AST
visualization**. Per Section 3 ("you are not permitted to... extend
the grammar arbitrarily... optional bonus features are where you may
differentiate your project"), these are strictly *additive* — every
required-language test and example still compiles and behaves
identically to before any of this was added (see `tests/README.md`
and `verification/README.md`, which confirm zero regressions).

This document is also written to double as an **implementation-order
walkthrough** — the sections below are in the order the work actually
happened, each building on the last, so it can serve as a viva-prep
narrative of "how do you add a language feature to a real Bison
grammar" as much as a reference for what these specific features do.

## 1. Lexer: new keywords and operators

`src/lexer/lexer.l` gained four keywords (`function`, `return`, `for`,
`do`) and two operators (`++`, `--`), plus `,` for parameter/argument
lists. Nothing subtle here — same longest-match, keywords-before-
identifiers pattern as the rest of the lexer (see `docs/design.md`).

## 2. AST: new node kinds

`src/ast/ast.h`/`ast.c` gained: `AST_FUNC_DECL`, `AST_CALL` (call used
as a value), `AST_CALL_STMT` (call used as a bare statement, return
value discarded), `AST_RETURN`, `AST_FOR`, `AST_DO_WHILE`, `AST_INCDEC`.
Plus two **transient, non-AST** helper types — `ParamList` and
`ArgList` — used only during parsing to accumulate a comma-separated
list before it's copied into the final node (see their comment in
`ast.h`: a parameter list isn't a meaningful standalone AST construct
the way a block is, so it doesn't get its own node kind).

`AST_CALL` and `AST_CALL_STMT` share one `CallData` struct — same
data (`callee`, `args`, `arg_count`), different AST *kind* signals
whether the result is used as a value or discarded.

## 3. Symbol table: functions share one namespace with variables

`Symbol` gained a `SymbolKind` (`SYMBOL_VAR` / `SYMBOL_FUNC`) and,
for functions, `params`/`param_count`. Deliberately **one shared
namespace** (matching C): a function and a variable can't have the
same name in the same scope, and `symtab_lookup` doesn't care which
kind it finds — callers check `sym->kind` themselves (see
`check_call()` in `semantic.c`, which rejects calling a variable, and
the `AST_IDENT` case, which rejects using a function name as a bare
value).

`Symbol.params` points **directly at the AST's own
`FuncDeclData.params` array** rather than being copied — it's not
owned by the Symbol, and it doesn't need to be, since the AST outlives
the whole compilation. (An earlier draft used a separate `DataType[]`
copy here; that was reworked once it became clear it needed its own,
more complicated ownership story for zero benefit — reusing the AST's
own array directly is both simpler and avoids an allocation.)

## 4. Parser: the highest-risk part — three grammar decisions worth explaining

**4a. Functions are prefixed with the `function` keyword.**
`function_decl: KW_FUNCTION type IDENT LPAREN param_list_opt RPAREN block`.
An earlier draft omitted the keyword (`type IDENT LPAREN ...`,
directly analogous to C), which is *also* grammatically valid — but
requires disambiguating from `declaration` (`type IDENT SEMI`) via
one token of lookahead after `IDENT`. Prefixing with `function`
removes the need for that disambiguation entirely: `function_decl` now
starts with a token (`KW_FUNCTION`) that nothing else in the grammar
can start with. Strictly a safety margin, not a necessity — but a free
one, and worth taking when a grammar can't be test-compiled directly
(see the root README's verification section).

**4b. Three different statement forms all start with `IDENT`**, and
none of them conflict:

```
assignment:  IDENT ASSIGN expr SEMI
call_stmt:   IDENT LPAREN arg_list_opt RPAREN SEMI
incdec_stmt: IDENT INC SEMI | IDENT DEC SEMI
```

After shifting `IDENT` at the start of a statement, the parser has
four live "continue parsing as..." possibilities, each requiring a
*different* specific next token (`ASSIGN`, `LPAREN`, `INC`, `DEC`).
Since these four tokens are mutually exclusive, one token of lookahead
resolves it every time with zero ambiguity — this is standard LALR(1)
structure, not a conflict (a genuine conflict needs the *same*
lookahead token to be valid for two different actions; here every
lookahead token picks out exactly one path).

**4c. Function calls as a value vs. as a statement don't overlap.**
`primary_expr: IDENT LPAREN arg_list_opt RPAREN` (a call producing a
value) and `call_stmt: IDENT LPAREN arg_list_opt RPAREN SEMI` (a call
whose value is discarded) look similar but are reached from entirely
different grammar positions — `call_stmt` only from `stmt`, the
`primary_expr` form only from within an existing `expr` context
(inside `assignment`'s right-hand side, a `print` argument, etc.).
They're never both "live options" in the same parser state, so there's
no shared-prefix ambiguity between them either.

**4d. `for`-loops require all three clauses, and the init/update
clauses are deliberately NOT `declaration`/`assignment` reused
directly.** `for (for_clause SEMI expr SEMI for_clause) block`, where
`for_clause` is `IDENT = expr` or `IDENT++`/`IDENT--` — **not**
optional, and **not** a new variable declaration (`for (int i = 0; ...)`
is not supported; you write `int i; for (i = 0; ...) {...}`, matching
this language's existing declare-then-use style everywhere else).
Both restrictions are deliberate scope-reduction for a bonus feature:
optional clauses would need a "no condition = always true" special
case threaded through both semantic analysis and TAC generation for a
rarely-needed case (`for(;;)`); reusing `declaration`/`assignment`
directly doesn't work syntactically anyway, since both already consume
their own trailing `;`, which would collide with the for-loop's own
explicit `;` separators.

**4e. `%code requires` — a real mistake caught during development,
worth explaining because it generalizes.** The `%union` needed
`ParamList *`/`ArgList *`/`DataType` members. A first attempt used
`struct ParamList *` in the union (mirroring the existing, working
`struct ASTNode *node` — pointers to an incomplete type don't need the
full definition visible, thanks to C's implicit forward-declaration
rule for tagged structs). That doesn't work here: `ast.h` defines
`ParamList` as `typedef struct { ... } ParamList;` — an **anonymous**
struct. There is no tag named `ParamList` to forward-declare; `struct
ParamList` would refer to a tag that doesn't exist. The fix was
`%code requires { #include "ast.h" }` before `%union`, which Bison
places into *both* the generated `.c` and `.h` files — so wherever
`parser.tab.h` ends up included (including `lex.yy.c`), `ParamList`,
`ArgList`, and `DataType` are already fully defined, and the union can
just use the plain typedef names (`ParamList *paramlist;`) with no
struct-tag trick needed at all.

## 5. Semantic analysis: two-pass registration, call checking, return-path analysis

**Two-pass, same reason as the original language's forward
references:** every function's *signature* is registered into the
global scope in one pass before any body (function or top-level) is
checked — see `analyze_program()`. Without this, `factorial` calling
itself, or two functions calling each other regardless of which is
declared first, wouldn't resolve.

**`check_call()`** (shared by `AST_CALL` and `AST_CALL_STMT`) checks,
in order: does the name resolve at all; is it actually a function (not
a variable); does the argument count match; does each argument's type
match its parameter (via the same `types_assignable()` used for plain
assignment, so int→float widening applies to arguments too, for
consistency).

**`definitely_returns()`** is a structural "does every path end in
`return`" analysis — not just "is there a return statement somewhere
in the function." An `if` only counts if it has an `else` AND both
branches definitely return; a `do-while` counts if its body definitely
returns (its body is guaranteed to run at least once, unlike
`while`/`for`); a plain `return` anywhere in the sequence counts
regardless of what (unreachable) code follows it. `while`/`for` are
deliberately **never** treated as guaranteed-to-execute, even when
their body always returns and even for something like `while (true)`
— this analysis doesn't attempt to prove a condition is always true,
so it conservatively rejects a few valid-in-principle programs (a
function whose only return path is inside `while (true) { return x; }`)
in exchange for staying simple and definitely sound. This is the exact
same conservative trade-off (and, not coincidentally, almost the exact
same function) used for the return-value-on-every-path check in an
entirely separate compiler project — the pattern generalizes.

**`current_function`** (a single pointer, not a stack, since functions
can't nest — see the parser section) is what lets `return` check its
value's type against the enclosing function's declared return type,
and lets a `return` outside any function be reported as an error
rather than silently mishandled.

## 6. TAC generation: the standard textbook function scheme

```
param <value>          ; once per argument, immediately before the call
result = call name, N  ; N = argument count; 'result =' omitted for a
                        ; call_stmt, whose return value is discarded
name:                   ; marks a function's entry point
return <value>
```

Verified against an actual recursive factorial
(`verification/verify_bonus_features.c`) — the generated TAC is
genuinely correct textbook output; see that file's captured run for
the full listing.

`for`/`do-while` reuse the exact same label + `if_false` +
`goto` scheme as `while`/`if-else` (see `docs/design.md`) — `do-while`
specifically needs no new instruction kind: putting the condition
check *after* the body and after the unconditional `goto` back to the
top is enough to express "check after, not before" using only
`if_false`, the same single conditional-jump primitive everything else
already uses.

`x++`/`x--` compile to exactly **one** instruction — `x = x + 1`
written directly to `x`, not routed through a temporary — since there
was no reason to spend an extra instruction on a value nobody reads
back.

## 7. Verification

Given the same no-Flex/Bison sandbox constraint as the rest of this
project (see the root README), the bonus features got the exact same
treatment: `verification/verify_bonus_features.c` builds a recursive
factorial, all eight function-related semantic error categories (type
mismatch, missing return, return outside a function, wrong arg count,
wrong arg type, calling a non-function, referencing a function as a
value, and the "both branches return is fine" sanity check), a
for-loop, a for-condition-type-check, a do-while, and
increment/decrement — by hand-constructing ASTs and running them
through the real `semantic.c`/`tac.c`, exactly like the original
harnesses. All pass. See that file and `verification/README.md` for
how to re-run it yourself (no Flex/Bison needed, seconds to run).

## 8. What's still NOT covered

- **Arrays** (Section 14 lists this too) — not implemented. Would need
  a new type-system concept (element type + size) and TAC forms for
  indexed load/store; a reasonable next step, not attempted here to
  keep this round of additions reviewable and fully verifiable.
- **`switch`-case** — not implemented.
- **Dead code elimination / constant folding** (the two Section 14
  *optimization* bonus items) — not implemented as a separate pass;
  `definitely_returns()`'s "code after an unconditional return is
  unreachable" observation is used only for the *return-coverage
  check*, not to actually strip that code from the generated TAC.
- **A GUI** — out of scope for a CLI compiler front-end.
