#ifndef MINILANG_SEMANTIC_H
#define MINILANG_SEMANTIC_H

#include "../ast/ast.h"
#include "../symbol_table/symbol_table.h"

/*
 * Semantic analyzer (project manual Section 4.5). Walks the AST,
 * populating the symbol table and annotating every expression node's
 * `type` field, reporting (via src/common/error.h's report_error) each
 * of the six required error categories:
 *
 *   - Undeclared variable use     (symtab_lookup fails)
 *   - Redeclaration               (symtab_insert fails: already in this scope)
 *   - Scope violation             (subsumed by the lookup-fails-after-
 *                                   scope-exit mechanism — see symbol_table.h)
 *   - Type mismatch               (assigning/comparing incompatible types)
 *   - Invalid assignment          (e.g. assigning a float-typed
 *                                   expression to a bool variable)
 *   - Invalid expressions         (e.g. && / || on numeric operands,
 *                                   % on a float operand)
 *
 * Type rules this implementation applies (see docs/design.md for the
 * full rationale — these are deliberate, documented design choices,
 * not arbitrary):
 *   - int widens implicitly to float (in arithmetic AND assignment) —
 *     standard, lossless widening, same as C/Java/C#.
 *   - float does NOT narrow implicitly to int — assigning a float
 *     expression to an int variable is a type-mismatch error.
 *   - bool is never implicitly convertible to/from int or float.
 *   - '%' requires both operands to be int (matches typical C-family
 *     semantics — there's no obvious meaning for float modulo here).
 *   - '&&' '||' '!' require bool operand(s).
 *   - '<' '>' '<=' '>=' require numeric (int/float) operands; result bool.
 *   - '==' '!=' require both operands numeric, or both operands bool
 *     (comparing a number to a bool is a type mismatch); result bool.
 *   - if/while/for/do-while conditions must be exactly type bool.
 *
 * --- Bonus features (manual Section 14) — see docs/bonus_features.md ---
 *
 * Functions: every function's SIGNATURE is registered into the global
 * scope in a dedicated pass BEFORE any function body — or the
 * top-level statements — is checked. This is what makes recursion and
 * mutual recursion work: by the time any body is checked, every
 * function name in the program already resolves, regardless of
 * textual declaration order.
 *
 * Every function must return a value on EVERY control-flow path,
 * checked structurally via `definitely_returns()` (both branches of
 * an if/else must return, not just "a return exists somewhere") —
 * not just presence-of-a-return-statement. A `return` outside any
 * function body is a semantic error, tracked via a `current_function`
 * pointer threaded through statement-checking.
 *
 * for-loops get their own nested scope for the BODY only (matching
 * if/while); the header's assignment/increment clauses are checked
 * against the enclosing scope, since this language's for-loop syntax
 * doesn't declare a new loop variable (see parser.y's for_stmt
 * comment for why).
 *
 * `++`/`--` require a numeric (int or float) operand.
 *
 * analyze_program() returns nothing; the caller (main.c) checks
 * error_count() afterward, exactly like every other phase.
 */

void analyze_program(ASTNode *program);

#endif /* MINILANG_SEMANTIC_H */
