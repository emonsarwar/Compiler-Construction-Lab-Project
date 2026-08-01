#ifndef MINILANG_OPTIMIZE_H
#define MINILANG_OPTIMIZE_H

#include "../ast/ast.h"
#include "../codegen/tac.h"

/*
 * Optimizer (advanced/unique feature — manual Section 14 lists
 * "constant folding" and "dead code elimination" as optional bonus
 * items; both are implemented here, enabled with the `-O` CLI flag).
 *
 * fold_constants   — AST-level pass, runs BEFORE code generation.
 *                     Collapses any BinOp/UnaryOp whose operand(s) are
 *                     already literals into a single literal node, e.g.
 *                     `2 + 3 * 4` becomes the literal `14` in the tree
 *                     itself, so no TAC is ever generated for the
 *                     original multiply/add at all.
 *
 * eliminate_dead_code — TAC-level pass, runs AFTER code generation.
 *                     Drops instructions that can provably never
 *                     execute: anything between an unconditional
 *                     `goto`/`return` and the next label. Labels are
 *                     always treated as possible jump targets (kept
 *                     conservative on purpose — no full control-flow
 *                     reachability analysis, just the standard
 *                     textbook "straight-line dead code after an
 *                     unconditional jump" pattern).
 *
 * Both return how many things they changed, purely so main.c can
 * print a one-line summary; a 0 return means "nothing to optimize".
 */

int fold_constants(ASTNode *node);
int eliminate_dead_code(TACList *list);

#endif /* MINILANG_OPTIMIZE_H */
