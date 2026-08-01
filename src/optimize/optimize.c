#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "optimize.h"

/* ============================== Constant folding ============================== */

static int is_const_literal(ASTNode *n) {
    return n && (n->kind == AST_INT_LIT || n->kind == AST_FLOAT_LIT || n->kind == AST_BOOL_LIT);
}

static double lit_as_double(ASTNode *n) {
    switch (n->kind) {
        case AST_INT_LIT:   return (double)n->as.int_lit.value;
        case AST_FLOAT_LIT:  return n->as.float_lit.value;
        case AST_BOOL_LIT:    return (double)n->as.bool_lit.value;
        default:                return 0.0;
    }
}

/* Turns `node` (currently a BinOp/UnaryOp) into a literal node
 * in-place, so the ASTNode* the parent already holds stays valid —
 * the parent never needs to know folding happened. */
static void replace_with_int(ASTNode *node, int value) {
    node->kind = AST_INT_LIT;
    node->as.int_lit.value = value;
    node->type = TYPE_INT;
}
static void replace_with_float(ASTNode *node, double value) {
    node->kind = AST_FLOAT_LIT;
    node->as.float_lit.value = value;
    node->type = TYPE_FLOAT;
}
static void replace_with_bool(ASTNode *node, int value) {
    node->kind = AST_BOOL_LIT;
    node->as.bool_lit.value = value ? 1 : 0;
    node->type = TYPE_BOOL;
}

/* Tries to fold a BinOp node whose left/right have ALREADY been
 * recursively folded (so if they're still not literals, they
 * genuinely depend on a variable and can't be folded further).
 * Returns 1 if it folded, 0 if it left the node alone. */
static int try_fold_binop(ASTNode *node) {
    ASTNode *l = node->as.binop.left;
    ASTNode *r = node->as.binop.right;
    if (!is_const_literal(l) || !is_const_literal(r)) return 0;
    const char *op = node->as.binop.op;
    int use_float = (l->kind == AST_FLOAT_LIT || r->kind == AST_FLOAT_LIT);

    /* Relational/equality/logical ops always fold to a bool. */
    if (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=") ||
        !strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "&&") || !strcmp(op, "||")) {
        double a = lit_as_double(l), b = lit_as_double(r);
        int result;
        if (!strcmp(op, "<"))  result = a < b;
        else if (!strcmp(op, ">"))  result = a > b;
        else if (!strcmp(op, "<=")) result = a <= b;
        else if (!strcmp(op, ">=")) result = a >= b;
        else if (!strcmp(op, "==")) result = a == b;
        else if (!strcmp(op, "!=")) result = a != b;
        else if (!strcmp(op, "&&")) result = (a != 0.0) && (b != 0.0);
        else                          result = (a != 0.0) || (b != 0.0);
        char *op_copy = node->as.binop.op;
        ast_free(l); ast_free(r); free(op_copy);
        replace_with_bool(node, result);
        return 1;
    }

    /* Arithmetic: stays int if both operands are int, else float. */
    if (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%")) {
        char *op_copy = node->as.binop.op;
        if (!strcmp(op, "%")) {
            /* '%' is int-only per this language's semantics (see semantic.c) */
            int a = l->as.int_lit.value, b = r->as.int_lit.value;
            int result = (b != 0) ? (a % b) : 0;
            ast_free(l); ast_free(r); free(op_copy);
            replace_with_int(node, result);
            return 1;
        }
        double a = lit_as_double(l), b = lit_as_double(r);
        double result;
        if (!strcmp(op, "+")) result = a + b;
        else if (!strcmp(op, "-")) result = a - b;
        else if (!strcmp(op, "*")) result = a * b;
        else result = (b != 0.0) ? a / b : 0.0;
        ast_free(l); ast_free(r); free(op_copy);
        if (use_float) replace_with_float(node, result);
        else replace_with_int(node, (int)result);
        return 1;
    }
    return 0;
}

static int try_fold_unaryop(ASTNode *node) {
    ASTNode *v = node->as.unaryop.operand;
    if (!is_const_literal(v)) return 0;
    const char *op = node->as.unaryop.op;
    char *op_copy = node->as.unaryop.op;
    if (!strcmp(op, "-")) {
        if (v->kind == AST_FLOAT_LIT) {
            double result = -v->as.float_lit.value;
            ast_free(v); free(op_copy);
            replace_with_float(node, result);
        } else {
            int result = -v->as.int_lit.value;
            ast_free(v); free(op_copy);
            replace_with_int(node, result);
        }
        return 1;
    }
    if (!strcmp(op, "!")) {
        int result = !(v->kind == AST_BOOL_LIT ? v->as.bool_lit.value : (int)lit_as_double(v));
        ast_free(v); free(op_copy);
        replace_with_bool(node, result);
        return 1;
    }
    return 0;
}

int fold_constants(ASTNode *node) {
    if (!node) return 0;
    int count = 0;

    switch (node->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->as.stmt_list.count; i++) count += fold_constants(node->as.stmt_list.stmts[i]);
            break;

        case AST_ASSIGN:      count += fold_constants(node->as.assign.value); break;
        case AST_ARRAY_ASSIGN: count += fold_constants(node->as.array_assign.index); count += fold_constants(node->as.array_assign.value); break;
        case AST_ARRAY_ACCESS: count += fold_constants(node->as.array_access.index); break;
        case AST_PRINT:        count += fold_constants(node->as.print_stmt.value); break;
        case AST_RETURN:       count += fold_constants(node->as.ret.value); break;
        case AST_READ:         break; /* target is an lvalue, nothing to fold */

        case AST_IF:
            count += fold_constants(node->as.if_stmt.cond);
            count += fold_constants(node->as.if_stmt.then_branch);
            if (node->as.if_stmt.else_branch) count += fold_constants(node->as.if_stmt.else_branch);
            break;

        case AST_WHILE:
            count += fold_constants(node->as.while_stmt.cond);
            count += fold_constants(node->as.while_stmt.body);
            break;

        case AST_DO_WHILE:
            count += fold_constants(node->as.do_while.body);
            count += fold_constants(node->as.do_while.cond);
            break;

        case AST_FOR:
            if (node->as.for_stmt.init) count += fold_constants(node->as.for_stmt.init);
            if (node->as.for_stmt.cond) count += fold_constants(node->as.for_stmt.cond);
            if (node->as.for_stmt.update) count += fold_constants(node->as.for_stmt.update);
            count += fold_constants(node->as.for_stmt.body);
            break;

        case AST_SWITCH:
            count += fold_constants(node->as.switch_stmt.subject);
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) count += fold_constants(node->as.switch_stmt.cases[i].body);
            break;

        case AST_FUNC_DECL:
            count += fold_constants(node->as.func_decl.body);
            break;

        case AST_CALL:
            for (int i = 0; i < node->as.call.arg_count; i++) count += fold_constants(node->as.call.args[i]);
            break;

        case AST_BINOP:
            count += fold_constants(node->as.binop.left);
            count += fold_constants(node->as.binop.right);
            if (try_fold_binop(node)) count++;
            break;

        case AST_UNARYOP:
            count += fold_constants(node->as.unaryop.operand);
            if (try_fold_unaryop(node)) count++;
            break;

        default:
            break; /* literals, identifiers, declarations, incdec, break: nothing to fold */
    }
    return count;
}

/* ============================== Dead code elimination ============================== */

/* A TAC_IF_FALSE whose condition is the literal text "true" or
 * "false" (typically produced by constant folding upstream, e.g. a
 * `while (false)` or an `if (1 == 2)`) can be resolved at compile
 * time instead of runtime:
 *   - condition "true"  -> the jump never happens -> the instruction
 *     is a no-op and can be dropped.
 *   - condition "false" -> the jump always happens -> it's really an
 *     unconditional `goto` (rewritten in place, same instruction
 *     count), which the reachability sweep below then uses to
 *     correctly drop everything dead after it.
 * Folded into ONE combined pass with the reachability sweep (rather
 * than two separate compactions) specifically so there's only ONE
 * index remapping to apply to TACList's func_ranges (see tac.h) —
 * doing it in two passes would shift indices twice and silently
 * corrupt them, which would in turn make `-O -run` together jump into
 * the wrong place at runtime. */
int eliminate_dead_code(TACList *list) {
    if (list->count == 0) return 0;

    int changed = 0;
    for (int i = 0; i < list->count; i++) {
        TACInstr *ins = &list->instrs[i];
        if (ins->kind != TAC_IF_FALSE) continue;
        if (!strcmp(ins->arg1, "false")) {
            free(ins->arg1);
            ins->kind = TAC_GOTO;
            ins->arg1 = ins->arg2;   /* TAC_GOTO's target lives in arg1 (see tac.h) */
            ins->arg2 = NULL;
            changed++;
        }
    }

    int *keep = (int *)malloc(sizeof(int) * (size_t)list->count);
    int removed = 0;
    int reachable = 1;

    /* Every function's `end` boundary (see tac.h's func_ranges) is
     * where top-level control flow RESUMES after skipping that
     * function's body — the interpreter jumps straight there, it
     * never falls through the function's own final `return`. Plain
     * straight-line reachability can't see that (a `return` never
     * "falls through" to whatever's textually next), so without this,
     * DCE would wrongly treat everything after the LAST function's
     * body as dead — including real top-level code. */
    char *is_func_end = (char *)calloc((size_t)(list->count + 1), 1);
    for (int r = 0; r < list->func_range_count; r++) is_func_end[list->func_ranges[r].end] = 1;

    for (int i = 0; i < list->count; i++) {
        TACInstr *ins = &list->instrs[i];
        TACOpKind k = ins->kind;

        /* A constant-true condition's if_false never jumps — it's a
         * pure no-op regardless of reachability, so drop it without
         * touching `reachable` at all (as if it was never there). */
        if (k == TAC_IF_FALSE && !strcmp(ins->arg1, "true")) {
            keep[i] = 0;
            removed++;
            continue;
        }

        /* A label (or a function's entry point) is always a possible
         * jump target, so it always restores reachability — kept
         * deliberately conservative rather than doing full
         * control-flow analysis to find genuinely-unused labels. */
        if (k == TAC_LABEL || k == TAC_FUNC_BEGIN || is_func_end[i]) reachable = 1;

        keep[i] = reachable;
        if (!reachable) removed++;

        if (k == TAC_GOTO || k == TAC_RETURN) reachable = 0; /* everything until the next label is dead */
    }
    free(is_func_end);
    changed += removed;

    if (removed > 0) {
        /* prefix[i] = how many kept instructions exist in [0, i) —
         * gives the correct NEW index for any OLD index in one lookup,
         * whether or not that exact old index was itself kept (which
         * is exactly what an exclusive range boundary like
         * FuncRange.end needs). */
        int *prefix = (int *)malloc(sizeof(int) * (size_t)(list->count + 1));
        prefix[0] = 0;
        for (int i = 0; i < list->count; i++) prefix[i + 1] = prefix[i] + (keep[i] ? 1 : 0);

        for (int r = 0; r < list->func_range_count; r++) {
            list->func_ranges[r].begin = prefix[list->func_ranges[r].begin];
            list->func_ranges[r].end   = prefix[list->func_ranges[r].end];
        }
        free(prefix);

        TACInstr *compacted = (TACInstr *)malloc(sizeof(TACInstr) * (size_t)(list->count - removed));
        int j = 0;
        for (int i = 0; i < list->count; i++) {
            if (keep[i]) compacted[j++] = list->instrs[i];
            else {
                /* Free the strings owned by the instruction we're dropping —
                 * mirrors tac_free's own per-field frees (see tac.c). */
                free(list->instrs[i].result);
                free(list->instrs[i].arg1);
                free(list->instrs[i].op);
                free(list->instrs[i].arg2);
            }
        }
        free(list->instrs);
        list->instrs = compacted;
        list->count = j;
        list->capacity = j;
    }

    free(keep);
    return changed;
}

