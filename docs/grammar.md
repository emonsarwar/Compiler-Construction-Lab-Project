# Formal Grammar

This is the complete, formal Context-Free Grammar for the mini
language (project manual Section 5), in EBNF-ish notation (`|`
alternatives, `*` zero-or-more). It is **unambiguous by construction**
— every choice point resolves without needing precedence declarations
or arbitrary tie-breaking, for two structural reasons explained below.
`src/parser/parser.y` implements exactly this grammar (see that file's
own header comment for the mapping from these levels to Bison rules).

```
program        := (declaration | statement)*

declaration    := ('int' | 'float' | 'bool') IDENT ';'

statement      := declaration
                 | assignment
                 | if_stmt
                 | while_stmt
                 | print_stmt
                 | block

assignment     := IDENT '=' expr ';'

if_stmt        := 'if' '(' expr ')' block
                 | 'if' '(' expr ')' block 'else' block

while_stmt     := 'while' '(' expr ')' block

print_stmt     := 'print' expr ';'

block          := '{' (declaration | statement)* '}'

expr           := logical_or_expr

logical_or_expr      := logical_and_expr ('||' logical_and_expr)*
logical_and_expr     := equality_expr ('&&' equality_expr)*
equality_expr        := relational_expr (('==' | '!=') relational_expr)*
relational_expr       := additive_expr (('<' | '>' | '<=' | '>=') additive_expr)*
additive_expr         := multiplicative_expr (('+' | '-') multiplicative_expr)*
multiplicative_expr   := unary_expr (('*' | '/' | '%') unary_expr)*
unary_expr            := ('-' | '!') unary_expr
                        | primary_expr
primary_expr          := INT_LIT
                        | FLOAT_LIT
                        | 'true'
                        | 'false'
                        | IDENT
                        | '(' expr ')'
```

Terminals `IDENT`, `INT_LIT`, `FLOAT_LIT` are defined lexically (see
`src/lexer/lexer.l`): `IDENT` = `[a-zA-Z_][a-zA-Z0-9_]*` (minus the
reserved keywords `int float bool if else while print true false`,
which the lexer matches first — see the "Lexer Design" note below);
`INT_LIT` = `[0-9]+`; `FLOAT_LIT` = `[0-9]+.[0-9]+`.

## Why this grammar has no shift/reduce conflicts

**1. Operator precedence is expressed by layering, not by
declaration.** Each precedence level (`logical_or_expr`,
`logical_and_expr`, ..., down to `unary_expr`) is its own grammar
rule, left-recursive over the next tighter level. This is the
standard technique for encoding precedence directly into a CFG's
*structure* rather than relying on Bison's `%left`/`%right`
declarations to resolve an otherwise-ambiguous flat grammar — there is
no point in parsing `a + b * c` where the parser has a genuine choice
between two different valid parse trees, because the grammar itself
only admits one: `+` can never appear as a direct child of `*`'s
operands without going through the full chain of levels, so `b * c`
is forced to reduce to a single `multiplicative_expr` before `+` ever
becomes eligible to combine with it.

**2. `if`/`while` bodies are always a `block` (mandatory `{ }`), never
a bare statement.** This eliminates the classic "dangling else"
ambiguity structurally. That ambiguity exists in C-like grammars
specifically because a bare, unbraced single statement can be an
`if`-body, so `if (a) if (b) stmt1; else stmt2;` is genuinely
ambiguous — the parser cannot tell from the grammar alone whether
`else` belongs to the inner or outer `if`. Because this language
requires `{ }` around every `if`/`while` body (matching the manual's
own Section 5.5 sample program), every `if`'s extent is lexically
closed by its own `}` before any subsequent token — including a
following `else` — is even seen. Concretely: in
`if (a) { if (b) { x = 1; } } else { x = 2; }`, the inner
`if (b) { x = 1; }` is fully reduced to a complete `if_stmt`,
nested inside the outer block's statement list, well before the
parser ever reaches the outer block's closing `}` — so when `else`
appears, there is only one incomplete `if_stmt` it could possibly
attach to.

## Precedence table (informative — the grammar above is authoritative)

From loosest to tightest binding, matching C's own ordering:

| Level | Operators | Associativity |
|---|---|---|
| 1 (loosest) | `\|\|` | left |
| 2 | `&&` | left |
| 3 | `==` `!=` | left |
| 4 | `<` `>` `<=` `>=` | left |
| 5 | `+` `-` (binary) | left |
| 6 | `*` `/` `%` | left |
| 7 (tightest) | unary `-`, `!` | right (prefix) |

## Example parse

`a + b * 2 < c && !d`

Working inward from the loosest-binding operator (`&&`) to the
tightest (`*`, then unary `!`):

```
                  &&
                /    \
               <      !
              / \      \
             +   c      d
            / \
           a   *
              / \
             b   2
```

That is: `!d` on the right of `&&`; `(a + b * 2) < c` on the left of
`&&`; `b * 2` grouped tighter than the `+` that combines it with `a`,
because `*` is a full level tighter than `+` in the grammar above.
