# Advanced / Unique Extensions

This document covers a second layer of extensions, built **on top of**
the required language (manual Section 5) and the bonus features
(Section 14, covered in `docs/bonus_features.md`). These go further
than anything the manual asks for, specifically to make this project
stand out beyond the "same fixed language, evaluated on a level
playing field" baseline the manual describes in Section 3 — Section 3
is explicit that optional/bonus work is exactly where a group is
meant to differentiate its project, and everything here is additive
only: every required-language and bonus-feature test still compiles
and behaves identically (`make test` / `make test-run` cover both,
see `tests/README.md`).

Seven things, in the order they were built (each depends on the last):

1. **Arrays** — fixed-size, e.g. `int arr[10];`
2. **`string` type** — string variables, literals, `print`, `+` concatenation
3. **`switch` / `case`** — with `default` and mandatory `break`
4. **`read`** — reads real input from stdin at runtime
5. **Constant folding** — AST-level, `-O` flag
6. **Dead code elimination** — TAC-level, `-O` flag
7. **A TAC interpreter** — actually *runs* the program, `-run` flag

## 1. Arrays

`int arr[10];` declares a fixed-size array of `int`, `float`, or
`bool`. `arr[i] = expr;` and `x = arr[i] + 1;` read/write elements.

- **Grammar** (`parser.y`): `array_decl` (`type IDENT [ INT_LIT ] ;`)
  and `array_assign` (`IDENT [ expr ] = expr ;`) are distinguished from
  a plain `declaration`/`assignment` by nothing more than the token
  right after `IDENT` — `SEMI`/`ASSIGN` vs `LBRACKET` — the same
  one-token LALR(1) lookahead trick already used for `call_stmt` vs a
  bare identifier in the original grammar.
- **Symbol table** (`symbol_table.h`): `Symbol` gained `is_array` and
  `array_size`. A new `symtab_insert_array` mirrors `symtab_insert`.
- **Semantics**: index must be `int`; the base name must actually be
  an array; assigned value must match the element type — same error
  style/format as every other semantic check (line number + message).
- **TAC**: two new instruction kinds, `TAC_ARR_STORE`
  (`arr[idx] = value`) and `TAC_ARR_LOAD` (`t = arr[idx]`).
- **Interpreter**: arrays are sized **on demand** (grown to fit the
  largest index used), not pre-allocated to the declared size — TAC
  carries no size information at all (declarations emit no TAC, same
  as every other declaration kind in this compiler), so this is a
  deliberate, documented simplification, not an oversight.

## 2. `string` type

`string s;`, `s = "hello";`, `print s;`, and `s = a + b;` (string
concatenation, only when *both* operands are already `string` — no
implicit number-to-string coercion, to keep `+`'s existing numeric
behavior completely unchanged for everything else).

String literals support the usual `\n`, `\t`, `\"`, `\\` escapes
(`lexer.l`). A literal's TAC "place" is just its text re-wrapped in
quotes (`"like this"`), the same zero-extra-instruction pattern
int/float/bool literals already use.

## 3. `switch` / `case`

```
switch (grade) {
    case 1: print "Excellent"; break;
    case 2: print "Good"; break;
    default: print "Unknown";
}
```

Case labels are restricted to `int` literals on purpose — no ranges,
no arbitrary constant expressions — and every arm except `default`
needs its own `break;`. That `break;` is consumed as a **literal
token** directly in the `switch_case` grammar rule, not turned into a
general-purpose statement — an earlier draft added a standalone
`break_stmt` alternative to `stmt` and it created a genuine
reduce/reduce ambiguity with `switch_case`'s own trailing
`KW_BREAK SEMI` (both able to consume the same tokens at the same
point); removing it from `stmt` entirely was simpler and safer than
trying to out-clever the ambiguity. Semantic analysis rejects a
`switch` subject that isn't `int` and duplicate `case` values, both
with line numbers, same as every other semantic error.

**Codegen** lowers this to the standard equality-check-and-branch
chain — evaluate the subject once, `==` against each case value,
`if_false` to a per-case `next` label, unconditional `goto end` after
each matched body, `default` (if present) emitted last as an
unconditional fallback regardless of where it appeared in the source.

## 4. `read`

`read x;` (or `read arr[i];`) reads one value from stdin into an
already-declared target at **runtime** — this only does anything under
`-run` (see §7); without it, `read` still type-checks and generates
TAC (`read x`), it just has nothing to execute it.

TAC carries no type information anywhere (by design — see the rest of
this document), so the interpreter can't know in advance whether it
should parse an int, a float, a bool, or a string for a given `read`.
Rather than thread type information through TAC just for this, `read`
**auto-detects** the type of whatever whitespace-delimited token it
reads (`true`/`false` → bool, digits → int, digits with a `.` →
float, anything else → string) — one simple rule that works uniformly
for every target type with no extra plumbing.

## 5 & 6. Optimizer (`-O`): constant folding + dead code elimination

Both manual Section 14 bonus items, in `src/optimize/`, enabled with
a single `-O` flag (prints how much each pass changed):

- **`fold_constants`** (AST-level, before codegen): recursively walks
  the validated tree; any `BinOp`/`UnaryOp` whose operand(s) are
  *already* literals gets collapsed in place into a single literal —
  `2 + 3 * 4` becomes the literal `14` in the tree itself, so the TAC
  below never even generates the original multiply/add.
- **`eliminate_dead_code`** (TAC-level, after codegen): two things in
  one combined pass —
  1. A constant `if_false` condition (`true`/`false` — typically
     produced by folding a `while (false)` or an `if (1 == 2)`) is
     resolved at compile time: `true` → the instruction is a pure
     no-op and is dropped; `false` → it's rewritten in place as an
     unconditional `goto`.
  2. Standard straight-line reachability: anything between an
     unconditional `goto`/`return` and the next label is unreachable
     and gets dropped. Labels are always treated as possible jump
     targets — kept deliberately conservative rather than doing full
     control-flow reachability analysis to find genuinely-unused
     labels.

  These two are done in **one** combined pass, not two separate ones,
  for a subtle but important reason: `TACList` also tracks exactly
  where each function's body begins/ends (`func_ranges` — see §7),
  and every removed instruction shifts every later index. Two
  separate compactions would shift those ranges *twice* and silently
  corrupt them; one pass means one index remap (a simple prefix-sum
  over "how many instructions survived"), applied once, correctly, to
  `func_ranges` as well as the instruction list itself. This is also
  exactly why `eliminate_dead_code` needed to learn that a function's
  `end` boundary — not just an explicit `TAC_LABEL` — restores
  reachability: a `return` never "falls through" to whatever's
  textually next, so naive straight-line analysis would (and, in an
  earlier version of this pass, briefly did) wrongly mark *everything*
  after the **last** declared function's body as dead, including real
  top-level code after it.

## 7. TAC Interpreter (`-run`)

Manual Section 6 explicitly scopes out machine code, assembly, and
"any compiler backend targeting real hardware" — this is none of
those. It's a direct, in-process **execution of the TAC this compiler
already generates** (a "TAC-level reference interpreter", the same
category of thing a compilers course covers as a way to check codegen
correctness), producing the program's actual runtime output, including
real input for `read`. `src/interp/interp.c` is documented in detail
inline; the short version:

- One flat instruction pointer walks the `TACList`. `TAC_LABEL` /
  `TAC_FUNC_BEGIN` are resolved to instruction indices up front.
- **Frames**: one global frame, plus one fresh frame **per active
  function call**, pushed on `TAC_CALL` and popped on `TAC_RETURN` —
  this is what makes recursion correct, since every active call gets
  its own independent copy of its parameters/locals (verified with
  `factorial`, `examples/bonus_functions.mc`). A variable lookup checks
  the current frame first, falling back to the global frame — matching
  this language's own scoping rule that a function body's enclosing
  scope is the global scope (`semantic.c`'s `AST_FUNC_DECL` case).
- **Never falling into a function body**: function bodies are emitted
  *inline* in the flat TAC listing, at their declaration position
  (see `tac.c`'s `tac_generate`) — ordinary top-level execution must
  skip a function's body entirely, entering only via an explicit
  `TAC_CALL`. `TACList.func_ranges` (populated during generation, not
  guessed afterward by scanning for the next `TAC_FUNC_BEGIN`, which
  is wrong for whichever function is declared *last*) gives the
  interpreter the exact `[begin, end)` of every function body for
  this.
- **Nested calls as arguments** (`f(g(x))`) work correctly because
  argument values are pushed onto a small stack by `TAC_PARAM` and
  each `TAC_CALL` pops exactly its own `argc` most-recently-pushed
  values — since a nested call's own params+call always fully
  completes (push, consume, and shrink the stack back down) *before*
  the outer call's next argument is pushed, values never get
  cross-contaminated between calls at any nesting depth.
- **String ownership**: every `Value`-returning lookup (`get_var`,
  `get_arr`) hands back an **independently owned copy** (a fresh
  `strdup` for `TYPE_STRING`), never a pointer aliased into a frame's
  own storage — this was in fact a real bug caught while building
  this (a variable read used to alias the frame's stored string, so
  printing it and freeing the "borrowed" pointer left the frame
  holding a dangling one, causing a double-free the next time that
  frame was touched). Verified leak/crash-free with AddressSanitizer
  across the whole `examples/` + `tests/valid/` suite, in every
  combination of `-O`/`-run`.

## Grammar additions (EBNF)

Layered on top of `docs/grammar.md`'s formal CFG — the required
language's grammar (manual Section 5, `docs/grammar.md`) is completely
unchanged by any of this; these are pure additions to `statement` and
`primary_expr`:

```
statement       := ... (as in docs/grammar.md) ...
                  | array_decl
                  | array_assign
                  | switch_stmt
                  | read_stmt

array_decl      := ('int' | 'float' | 'bool') IDENT '[' INT_LIT ']' ';'
array_assign    := IDENT '[' expr ']' '=' expr ';'
read_stmt       := 'read' IDENT ';'
                  | 'read' IDENT '[' expr ']' ';'

switch_stmt     := 'switch' '(' expr ')' '{' switch_case* '}'
switch_case     := 'case' INT_LIT ':' statement* 'break' ';'
                  | 'default' ':' statement*

primary_expr    := ... (as in docs/grammar.md) ...
                  | STRING_LIT
                  | IDENT '[' expr ']'

type            := 'int' | 'float' | 'bool' | 'string'
```

Try it:
```bash
./minilangc examples/advanced_arrays_strings_switch.mc -run
./minilangc examples/bonus_functions.mc -O -run
echo 5 | ./minilangc examples/advanced_read_and_run.mc -run
```
