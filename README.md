# Mini Language Compiler — Compiler Construction Lab Project

A compiler front-end (lexer → parser → AST → semantic analysis →
Three Address Code) for the fixed mini language specified in the
project manual, Section 5. Built with Flex, Bison, and C, per Section
4's required module list.

> **Rename this repository** to `CC-Lab-Project-<YourGroupName>`
> before submission, per the course template's Step 3, and add every
> team member as a collaborator (Step 4) — see the original template
> repo's instructions for both.

## Quick start

```sh
make            # builds ./minilangc
./minilangc examples/counting_sum.mc
./minilangc examples/bonus_functions.mc          # bonus: recursion, calls
./minilangc examples/bonus_loops.mc              # bonus: for, do-while, ++/--
./minilangc examples/bonus_functions.mc -dot ast.dot   # bonus: Graphviz export
dot -Tpng ast.dot -o ast.png                     # (needs graphviz installed)

# --- advanced/unique extensions beyond the manual — see docs/advanced_features.md ---
./minilangc examples/advanced_arrays_strings_switch.mc -run   # arrays, string, switch — actually run it
./minilangc examples/bonus_functions.mc -O -run                # -O: constant folding + dead code elimination
echo 5 | ./minilangc examples/advanced_read_and_run.mc -run    # read: real stdin input at runtime

make test       # runs the compiler on every file under examples/ and tests/
make test-run   # same, but with -O -run on every valid program (needs stdin for the read example)
make clean      # removes generated/build files
```

Requires `flex`, `bison`, `gcc`, and `make` (all listed in the
manual's Section 7 recommended stack; on Ubuntu/Debian:
`sudo apt install flex bison gcc make`).

## A note on how this was verified

This project has been **built and run for real**, with actual Flex,
Bison, and GCC — `make` (clean, zero warnings), `make test`, and
`make test-run` (every valid program under `-O -run`, actually
executing and printing correct output) were all run directly, and the
whole suite was additionally checked under **AddressSanitizer** with
no leaks or crashes, across every combination of `-O`/`-run`. This
includes the required language (Section 5), the bonus features
(Section 14), and the advanced extensions in
`docs/advanced_features.md` — arrays, `switch`, `read`, constant
folding, dead code elimination, and the `-run` interpreter, including
recursion (`factorial`) and interactive input.

The Bison grammar itself was also checked directly with
`bison -d -Wall` — **zero shift/reduce or reduce/reduce conflicts** —
so the dangling-else and other ambiguity concerns documented in
`docs/grammar.md` are confirmed resolved, not just reasoned about by
hand.

**Please still run `make test` / `make test-run` yourself** before
submission and treat any mismatch on your machine (different Bison
version, different platform) as a real bug to investigate — but this
is no longer "written carefully but never compiled": it's been
compiled, run, and exercised end-to-end.

## Directory structure

```
docs/                  grammar.md (formal CFG), design.md (architecture
                        + every design decision explained),
                        bonus_features.md (functions/for/do-while/++/--),
                        advanced_features.md (arrays/string/switch/read/
                        -O optimizer/-run interpreter), report_template.md
src/
  lexer/lexer.l         Flex lexical analyzer
  parser/parser.y       Bison grammar + AST construction
  ast/                  AST node types, constructors, printer, Graphviz export
  semantic/             semantic analyzer (symbol table + type checking)
  symbol_table/         nested-scope symbol table (variables + functions + arrays)
  codegen/tac.{h,c}     Three Address Code generator
  optimize/             -O: constant folding (AST) + dead code elimination (TAC)
  interp/               -run: TAC interpreter — actually executes the program
  common/error.{h,c}    shared error reporting
  main.c                driver: wires the whole pipeline together
tests/                  valid/, lexical_errors/, syntax_errors/,
                        semantic_errors/ — see tests/README.md
examples/               small demo-friendly sample programs, including
                        bonus_functions.mc, bonus_loops.mc, and the
                        advanced_*.mc examples (arrays/string/switch/read)
verification/           harnesses that test semantic.c/tac.c directly,
                        bypassing the parser — see its own README
web/                    optional browser playground (frontend + backend)
                        for minilangc — see web/README.md
Makefile
```

## What's implemented

Every required module (manual Section 4): lexical analysis (with
lexical-error recovery — bad characters are reported and skipped, not
fatal), syntax analysis with basic error recovery (`error SEMI`,
resynchronizing at the next statement boundary so one syntax error
doesn't hide a second, independent one), a printable AST, a
nested-scope symbol table, semantic analysis covering all six required
error categories, and Three Address Code generation for arithmetic
(precedence-correct), relational, and logical expressions, `if`,
`if-else`, `while` (via labels + conditional/unconditional jumps), and
`print`.

Explicitly out of scope, matching manual Section 6: no machine code,
assembly, register allocation, linking, or executable generation.

**Bonus features implemented** (manual Section 14 — see
**[docs/bonus_features.md](./docs/bonus_features.md)** for the full
design, including three real grammar-safety decisions worth reading
if you're touching the parser): functions (declarations, calls,
parameters, `return`, recursion, a structural "returns on every path"
check), `for` loops, `do-while` loops, `++`/`--`, and Graphviz AST
visualization (`./minilangc file.mc -dot out.dot`, then
`dot -Tpng out.dot -o out.png`). These are strictly additive — every
required-language test still passes unchanged.

**Advanced/unique extensions, beyond both the required language and
the bonus list** — see
**[docs/advanced_features.md](./docs/advanced_features.md)** for the
full design and the interpreter's internals: fixed-size **arrays**,
a **`string`** type (with `+` concatenation), **`switch`/`case`**, a
**`read`** statement, an **optimizer** (`-O`: constant folding +
dead code elimination, both listed as Section 14 bonus items), and a
**TAC interpreter** (`-run`: actually executes the generated TAC and
prints the program's real output, including reading real stdin input
for `read` — this is a tree-free execution of the compiler's own
intermediate representation, not a hardware backend, so it stays
within Section 6's scope). All of this is strictly additive too — the
whole point of `make test` / `make test-run` both existing is to keep
proving that at every step.

## Team

**Team Name:** ByteForge

| Role | Name | ID |
|---|---|---|
| Team Leader | Emon Sarwar | 231-115-256 |
| Member 2 | Mahmuda Islam Naima | 231-115-267 |
| Member 3 | Mst. Priya Akter Ferdousi | 231-115-085 |

## AI usage disclosure

Built with AI assistance (per the manual's Section 10, which
explicitly permits this). **Every team member is still expected to be
able to explain any part of this implementation in the individual
viva regardless of who — or what — originally wrote a given line** —
that's the manual's condition on using AI tools at all, not optional
fine print. `docs/design.md` exists specifically to make that easier:
it explains the *reasoning* behind every non-obvious decision in this
codebase, not just what the code does.
