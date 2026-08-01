#include <string.h>
#include <stdlib.h>
#include "semantic.h"
#include "../common/error.h"

/* Forward declaration: check_call() (added below, for the bonus
 * functions feature) needs to type-check call arguments, which
 * requires analyze_expr — and analyze_expr's own AST_CALL case needs
 * check_call. Breaking that cycle the standard C way. */
static DataType analyze_expr(ASTNode *node, Scope *scope);

/* NULL while checking top-level statements; set to the enclosing
 * AST_FUNC_DECL node while checking a function body — used to
 * validate `return`'s type against the function's declared return
 * type, and to reject `return` used outside any function. Functions
 * cannot nest (see parser.y), so this never needs to be a stack. */
static ASTNode *current_function = NULL;

/* Advanced/unique extension: how many `switch` statements we're
 * currently nested inside — used only to validate `break;` isn't used
 * outside one. (This language has no loop-`break`, only switch-`break`,
 * so there's no ambiguity to resolve between the two.) */
static int switch_depth = 0;

static int is_numeric(DataType t) { return t == TYPE_INT || t == TYPE_FLOAT; }

/* int -> float is a lossless implicit widening (same rule as C/Java/C#);
 * nothing else converts implicitly — see semantic.h's docstring for the
 * full rationale. `value == TYPE_UNKNOWN` means an error was already
 * reported for that sub-expression, so we accept silently rather than
 * cascading a second, redundant error on top of it. */
static int types_assignable(DataType target, DataType value) {
    if (value == TYPE_UNKNOWN) return 1;
    if (target == value) return 1;
    if (target == TYPE_FLOAT && value == TYPE_INT) return 1;
    return 0;
}

static int check_numeric_operands(ASTNode *node, const char *op, DataType lt, DataType rt) {
    if (!is_numeric(lt) || !is_numeric(rt)) {
        report_error("Semantic", node->line,
            "invalid expression: operator '%s' requires numeric operands, found '%s' and '%s'",
            op, datatype_to_string(lt), datatype_to_string(rt));
        return 0;
    }
    return 1;
}

static DataType check_binop(ASTNode *node, DataType lt, DataType rt) {
    const char *op = node->as.binop.op;
    if (lt == TYPE_UNKNOWN || rt == TYPE_UNKNOWN) return TYPE_UNKNOWN; /* already reported below */

    /* Advanced/unique extension: '+' also means string concatenation
     * when BOTH operands are type 'string' (kept strict — no implicit
     * number-to-string coercion — so this stays additive rather than
     * changing '+''s existing numeric behavior at all). */
    if (!strcmp(op, "+") && lt == TYPE_STRING && rt == TYPE_STRING) {
        return TYPE_STRING;
    }

    if (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") || !strcmp(op, "/")) {
        if (!check_numeric_operands(node, op, lt, rt)) {
            return TYPE_UNKNOWN;
        }
        return (lt == TYPE_FLOAT || rt == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
    }

    if (!strcmp(op, "%")) {
        if (lt != TYPE_INT || rt != TYPE_INT) {
            report_error("Semantic", node->line,
                "invalid expression: operator '%%' requires int operands, found '%s' and '%s'",
                datatype_to_string(lt), datatype_to_string(rt));
            return TYPE_UNKNOWN;
        }
        return TYPE_INT;
    }

    if (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) {
        if (!check_numeric_operands(node, op, lt, rt)) {
            return TYPE_UNKNOWN;
        }
        return TYPE_BOOL;
    }

    if (!strcmp(op, "==") || !strcmp(op, "!=")) {
        int both_numeric = is_numeric(lt) && is_numeric(rt);
        int both_bool = (lt == TYPE_BOOL && rt == TYPE_BOOL);
        if (!both_numeric && !both_bool) {
            report_error("Semantic", node->line,
                "type mismatch: cannot compare '%s' with '%s' using '%s'",
                datatype_to_string(lt), datatype_to_string(rt), op);
            return TYPE_UNKNOWN;
        }
        return TYPE_BOOL;
    }

    if (!strcmp(op, "&&") || !strcmp(op, "||")) {
        if (lt != TYPE_BOOL || rt != TYPE_BOOL) {
            report_error("Semantic", node->line,
                "invalid expression: operator '%s' requires bool operands, found '%s' and '%s'",
                op, datatype_to_string(lt), datatype_to_string(rt));
            return TYPE_UNKNOWN;
        }
        return TYPE_BOOL;
    }

    report_error("Semantic", node->line, "internal error: unknown binary operator '%s'", op);
    return TYPE_UNKNOWN;
}

static DataType check_unaryop(ASTNode *node, DataType t) {
    const char *op = node->as.unaryop.op;
    if (t == TYPE_UNKNOWN) return TYPE_UNKNOWN;

    if (!strcmp(op, "-")) {
        if (!is_numeric(t)) {
            report_error("Semantic", node->line,
                "invalid expression: unary '-' requires a numeric operand, found '%s'", datatype_to_string(t));
            return TYPE_UNKNOWN;
        }
        return t;
    }
    if (!strcmp(op, "!")) {
        if (t != TYPE_BOOL) {
            report_error("Semantic", node->line,
                "invalid expression: unary '!' requires a bool operand, found '%s'", datatype_to_string(t));
            return TYPE_UNKNOWN;
        }
        return TYPE_BOOL;
    }
    report_error("Semantic", node->line, "internal error: unknown unary operator '%s'", op);
    return TYPE_UNKNOWN;
}

/* Bonus feature (functions): checks a call's callee resolves to a
 * function (not a variable), its argument count matches, and each
 * argument's type is assignable to the corresponding parameter
 * (reusing types_assignable — so int->float widening applies to
 * arguments exactly like it does to plain assignment, for
 * consistency). Shared by AST_CALL (expression) and AST_CALL_STMT
 * (statement) — see ast.h's CallData comment. */
static DataType check_call(ASTNode *node, Scope *scope) {
    const char *callee = node->as.call.callee;
    Symbol *sym = symtab_lookup(scope, callee);
    if (!sym) {
        report_error("Semantic", node->line, "call to undeclared function '%s'", callee);
        for (int i = 0; i < node->as.call.arg_count; i++) analyze_expr(node->as.call.args[i], scope);
        return TYPE_UNKNOWN;
    }
    if (sym->kind != SYMBOL_FUNC) {
        report_error("Semantic", node->line, "'%s' is a variable, not a function, and cannot be called", callee);
        for (int i = 0; i < node->as.call.arg_count; i++) analyze_expr(node->as.call.args[i], scope);
        return TYPE_UNKNOWN;
    }
    if (node->as.call.arg_count != sym->param_count) {
        report_error("Semantic", node->line,
            "function '%s' expects %d argument(s), found %d", callee, sym->param_count, node->as.call.arg_count);
        for (int i = 0; i < node->as.call.arg_count; i++) analyze_expr(node->as.call.args[i], scope);
        return sym->type;
    }
    for (int i = 0; i < node->as.call.arg_count; i++) {
        DataType arg_type = analyze_expr(node->as.call.args[i], scope);
        if (!types_assignable(sym->params[i].type, arg_type)) {
            report_error("Semantic", node->line,
                "argument %d to '%s': expected '%s', found '%s'",
                i + 1, callee, datatype_to_string(sym->params[i].type), datatype_to_string(arg_type));
        }
    }
    return sym->type;
}

static DataType analyze_expr(ASTNode *node, Scope *scope) {
    switch (node->kind) {
        case AST_INT_LIT:   node->type = TYPE_INT;   return TYPE_INT;
        case AST_FLOAT_LIT: node->type = TYPE_FLOAT; return TYPE_FLOAT;
        case AST_BOOL_LIT:  node->type = TYPE_BOOL;  return TYPE_BOOL;
        case AST_STRING_LIT: node->type = TYPE_STRING; return TYPE_STRING;

        /* Advanced/unique extension: array element read. */
        case AST_ARRAY_ACCESS: {
            const char *name = node->as.array_access.name;
            Symbol *sym = symtab_lookup(scope, name);
            DataType idx_type = analyze_expr(node->as.array_access.index, scope);
            if (!sym) {
                report_error("Semantic", node->line, "undeclared variable '%s' used", name);
                node->type = TYPE_UNKNOWN;
                return TYPE_UNKNOWN;
            }
            if (!sym->is_array) {
                report_error("Semantic", node->line, "'%s' is not an array and cannot be indexed", name);
                node->type = TYPE_UNKNOWN;
                return TYPE_UNKNOWN;
            }
            if (idx_type != TYPE_INT && idx_type != TYPE_UNKNOWN) {
                report_error("Semantic", node->line,
                    "invalid expression: array index must be type 'int', found '%s'", datatype_to_string(idx_type));
            }
            node->type = sym->type;
            return sym->type;
        }

        case AST_IDENT: {
            Symbol *sym = symtab_lookup(scope, node->as.ident.name);
            if (!sym) {
                report_error("Semantic", node->line, "undeclared variable '%s' used", node->as.ident.name);
                node->type = TYPE_UNKNOWN;
                return TYPE_UNKNOWN;
            }
            if (sym->kind == SYMBOL_FUNC) {
                report_error("Semantic", node->line,
                    "'%s' is a function; did you mean to call it with '%s(...)'?", node->as.ident.name, node->as.ident.name);
                node->type = TYPE_UNKNOWN;
                return TYPE_UNKNOWN;
            }
            node->type = sym->type;
            return sym->type;
        }

        case AST_CALL: {
            node->type = check_call(node, scope);
            return node->type;
        }

        case AST_BINOP: {
            DataType lt = analyze_expr(node->as.binop.left, scope);
            DataType rt = analyze_expr(node->as.binop.right, scope);
            node->type = check_binop(node, lt, rt);
            return node->type;
        }

        case AST_UNARYOP: {
            DataType t = analyze_expr(node->as.unaryop.operand, scope);
            node->type = check_unaryop(node, t);
            return node->type;
        }

        default:
            report_error("Semantic", node->line, "internal error: node kind %d is not a valid expression", (int)node->kind);
            node->type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
    }
}

static void analyze_stmt(ASTNode *node, Scope *scope);

/* Bonus feature (functions): does every control-flow path through
 * this block end in `return`? Structural, not just "a return exists
 * somewhere" — an if/else only counts if BOTH branches definitely
 * return. do-while's body is included too, since (unlike while/for)
 * it's guaranteed to execute at least once. while/for are
 * deliberately NOT included even if their body always returns: this
 * language doesn't attempt to prove a condition is always true (e.g.
 * `while (true)`), so treating any loop as a guaranteed-executed path
 * would be unsound in general — a conservative "reject some valid
 * programs" trade-off, documented in docs/bonus_features.md. */
static int definitely_returns(ASTNode *block) {
    for (int i = 0; i < block->as.stmt_list.count; i++) {
        ASTNode *stmt = block->as.stmt_list.stmts[i];
        if (stmt->kind == AST_RETURN) return 1;
        if (stmt->kind == AST_IF && stmt->as.if_stmt.else_branch) {
            if (definitely_returns(stmt->as.if_stmt.then_branch) && definitely_returns(stmt->as.if_stmt.else_branch)) return 1;
        }
        if (stmt->kind == AST_DO_WHILE && definitely_returns(stmt->as.do_while.body)) return 1;
        if (stmt->kind == AST_BLOCK && definitely_returns(stmt)) return 1;
    }
    return 0;
}

/* A block always introduces a fresh nested scope — this single call is
 * what implements block scoping for if/else/while bodies (Section 5.2:
 * "Nested blocks ({ ... }), with proper scoping"). */
static void analyze_block(ASTNode *block, Scope *parent) {
    Scope *inner = symtab_enter_scope(parent);
    for (int i = 0; i < block->as.stmt_list.count; i++) {
        analyze_stmt(block->as.stmt_list.stmts[i], inner);
    }
    symtab_exit_scope(inner);
}

static void analyze_stmt(ASTNode *node, Scope *scope) {
    switch (node->kind) {
        case AST_VAR_DECL: {
            const char *name = node->as.var_decl.name;
            if (!symtab_insert(scope, name, node->as.var_decl.var_type, node->line)) {
                report_error("Semantic", node->line, "redeclaration of variable '%s'", name);
            }
            break;
        }

        case AST_ASSIGN: {
            const char *name = node->as.assign.name;
            Symbol *sym = symtab_lookup(scope, name);
            DataType value_type = analyze_expr(node->as.assign.value, scope);
            if (!sym) {
                report_error("Semantic", node->line, "undeclared variable '%s' used in assignment", name);
                break;
            }
            if (!types_assignable(sym->type, value_type)) {
                report_error("Semantic", node->line,
                    "invalid assignment: cannot assign a value of type '%s' to variable '%s' of type '%s'",
                    datatype_to_string(value_type), name, datatype_to_string(sym->type));
            }
            break;
        }

        case AST_IF: {
            DataType cond_type = analyze_expr(node->as.if_stmt.cond, scope);
            if (cond_type != TYPE_BOOL && cond_type != TYPE_UNKNOWN) {
                report_error("Semantic", node->line,
                    "if-condition must be type 'bool', found '%s'", datatype_to_string(cond_type));
            }
            analyze_block(node->as.if_stmt.then_branch, scope);
            if (node->as.if_stmt.else_branch) analyze_block(node->as.if_stmt.else_branch, scope);
            break;
        }

        case AST_WHILE: {
            DataType cond_type = analyze_expr(node->as.while_stmt.cond, scope);
            if (cond_type != TYPE_BOOL && cond_type != TYPE_UNKNOWN) {
                report_error("Semantic", node->line,
                    "while-condition must be type 'bool', found '%s'", datatype_to_string(cond_type));
            }
            analyze_block(node->as.while_stmt.body, scope);
            break;
        }

        case AST_PRINT:
            analyze_expr(node->as.print_stmt.value, scope);
            break;

        case AST_BLOCK:
            analyze_block(node, scope);
            break;

        /* --- Bonus features --- */

        case AST_CALL_STMT:
            check_call(node, scope); /* return value, if any, is discarded */
            break;

        case AST_RETURN: {
            if (!current_function) {
                report_error("Semantic", node->line, "'return' used outside of a function");
                analyze_expr(node->as.ret.value, scope); /* still check the expression itself */
                break;
            }
            DataType value_type = analyze_expr(node->as.ret.value, scope);
            DataType expected = current_function->as.func_decl.return_type;
            if (value_type != TYPE_UNKNOWN && !types_assignable(expected, value_type)) {
                report_error("Semantic", node->line,
                    "function '%s' is declared to return '%s', but this returns '%s'",
                    current_function->as.func_decl.name, datatype_to_string(expected), datatype_to_string(value_type));
            }
            break;
        }

        case AST_FOR: {
            /* init/update run against the ENCLOSING scope, not a new
             * one — this language's for-loop doesn't declare a new
             * loop variable (see parser.y's for_stmt comment), so
             * there's nothing that needs its own scope beyond the
             * body (handled identically to while's body below). */
            analyze_stmt(node->as.for_stmt.init, scope);
            DataType cond_type = analyze_expr(node->as.for_stmt.cond, scope);
            if (cond_type != TYPE_BOOL && cond_type != TYPE_UNKNOWN) {
                report_error("Semantic", node->line,
                    "for-condition must be type 'bool', found '%s'", datatype_to_string(cond_type));
            }
            analyze_stmt(node->as.for_stmt.update, scope);
            analyze_block(node->as.for_stmt.body, scope);
            break;
        }

        case AST_DO_WHILE: {
            analyze_block(node->as.do_while.body, scope);
            DataType cond_type = analyze_expr(node->as.do_while.cond, scope);
            if (cond_type != TYPE_BOOL && cond_type != TYPE_UNKNOWN) {
                report_error("Semantic", node->line,
                    "do-while condition must be type 'bool', found '%s'", datatype_to_string(cond_type));
            }
            break;
        }

        case AST_INCDEC: {
            Symbol *sym = symtab_lookup(scope, node->as.incdec.name);
            if (!sym) {
                report_error("Semantic", node->line, "undeclared variable '%s' used", node->as.incdec.name);
                break;
            }
            if (sym->kind == SYMBOL_FUNC) {
                report_error("Semantic", node->line, "'%s' is a function, not a variable", node->as.incdec.name);
                break;
            }
            if (!is_numeric(sym->type)) {
                report_error("Semantic", node->line,
                    "invalid expression: '%s' requires a numeric operand, found '%s'",
                    node->as.incdec.op, datatype_to_string(sym->type));
            }
            break;
        }

        /* --- Advanced/unique extensions --- */

        case AST_ARRAY_DECL: {
            const char *name = node->as.array_decl.name;
            if (node->as.array_decl.size <= 0) {
                report_error("Semantic", node->line, "array '%s' must have a positive size", name);
            }
            if (!symtab_insert_array(scope, name, node->as.array_decl.elem_type, node->as.array_decl.size, node->line)) {
                report_error("Semantic", node->line, "redeclaration of variable '%s'", name);
            }
            break;
        }

        case AST_ARRAY_ASSIGN: {
            const char *name = node->as.array_assign.name;
            Symbol *sym = symtab_lookup(scope, name);
            DataType idx_type = analyze_expr(node->as.array_assign.index, scope);
            DataType value_type = analyze_expr(node->as.array_assign.value, scope);
            if (!sym) {
                report_error("Semantic", node->line, "undeclared variable '%s' used in assignment", name);
                break;
            }
            if (!sym->is_array) {
                report_error("Semantic", node->line, "'%s' is not an array and cannot be indexed", name);
                break;
            }
            if (idx_type != TYPE_INT && idx_type != TYPE_UNKNOWN) {
                report_error("Semantic", node->line,
                    "invalid expression: array index must be type 'int', found '%s'", datatype_to_string(idx_type));
            }
            if (!types_assignable(sym->type, value_type)) {
                report_error("Semantic", node->line,
                    "invalid assignment: cannot assign a value of type '%s' to array '%s' of element type '%s'",
                    datatype_to_string(value_type), name, datatype_to_string(sym->type));
            }
            break;
        }

        /* switch's subject and every `case` label must be type int
         * (case labels are always int literals — see parser.y); each
         * case body gets its own nested scope, same as if/while. */
        case AST_SWITCH: {
            DataType subj_type = analyze_expr(node->as.switch_stmt.subject, scope);
            if (subj_type != TYPE_INT && subj_type != TYPE_UNKNOWN) {
                report_error("Semantic", node->line,
                    "switch subject must be type 'int', found '%s'", datatype_to_string(subj_type));
            }
            switch_depth++;
            int seen_default = 0;
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                SwitchCase *c = &node->as.switch_stmt.cases[i];
                if (c->is_default) {
                    if (seen_default) {
                        report_error("Semantic", node->line, "switch statement has more than one 'default' arm");
                    }
                    seen_default = 1;
                } else {
                    for (int j = 0; j < i; j++) {
                        SwitchCase *prev = &node->as.switch_stmt.cases[j];
                        if (!prev->is_default && prev->value->as.int_lit.value == c->value->as.int_lit.value) {
                            report_error("Semantic", node->line,
                                "duplicate 'case %d' in switch statement", c->value->as.int_lit.value);
                        }
                    }
                }
                analyze_block(c->body, scope);
            }
            switch_depth--;
            break;
        }

        case AST_BREAK:
            if (switch_depth == 0) {
                report_error("Semantic", node->line, "'break' used outside of a switch statement");
            }
            break;

        /* `read` (advanced feature): the target must already be a
         * declared variable (or array element) of a readable type —
         * reuses AST_IDENT / AST_ARRAY_ACCESS analysis as-is, so all the
         * usual undeclared/scope-violation checks apply for free. */
        case AST_READ: {
            DataType t = analyze_expr(node->as.read_stmt.target, scope);
            if (t == TYPE_VOID) {
                report_error("Semantic", node->line, "cannot read a value into this target");
            }
            break;
        }

        case AST_FUNC_DECL: {
            ASTNode *prev_function = current_function;
            current_function = node;

            /* Parameters and the body's top-level share ONE scope
             * (matching C: a local variable with the same name as a
             * parameter is a redeclaration, not shadowing) — so the
             * body's statements are walked directly in func_scope
             * rather than via analyze_block, which would introduce an
             * extra nested scope. */
            Scope *func_scope = symtab_enter_scope(scope);
            for (int i = 0; i < node->as.func_decl.param_count; i++) {
                if (!symtab_insert(func_scope, node->as.func_decl.params[i].name, node->as.func_decl.params[i].type, node->line)) {
                    report_error("Semantic", node->line,
                        "duplicate parameter name '%s' in function '%s'",
                        node->as.func_decl.params[i].name, node->as.func_decl.name);
                }
            }
            ASTNode *body = node->as.func_decl.body;
            for (int i = 0; i < body->as.stmt_list.count; i++) {
                analyze_stmt(body->as.stmt_list.stmts[i], func_scope);
            }
            symtab_exit_scope(func_scope);

            if (!definitely_returns(body)) {
                report_error("Semantic", node->line,
                    "function '%s' does not return a value on all code paths", node->as.func_decl.name);
            }

            current_function = prev_function;
            break;
        }

        default:
            report_error("Semantic", node->line, "internal error: node kind %d is not a valid statement", (int)node->kind);
            break;
    }
}

void analyze_program(ASTNode *program) {
    Scope *global = symtab_enter_scope(NULL);

    /* Pass 1 (bonus: functions): register every function's SIGNATURE
     * before checking ANY body — see semantic.h for why (forward
     * references and mutual recursion). */
    for (int i = 0; i < program->as.stmt_list.count; i++) {
        ASTNode *item = program->as.stmt_list.stmts[i];
        if (item->kind == AST_FUNC_DECL) {
            if (!symtab_insert_func(global, item->as.func_decl.name, item->as.func_decl.return_type,
                                     item->as.func_decl.params, item->as.func_decl.param_count, item->line)) {
                report_error("Semantic", item->line, "redeclaration of function '%s'", item->as.func_decl.name);
            }
        }
    }

    /* Pass 2: analyze every top-level item — plain statements execute
     * as the implicit "main" sequence in order; function bodies are
     * checked in their own parameter scope (see AST_FUNC_DECL above). */
    for (int i = 0; i < program->as.stmt_list.count; i++) {
        analyze_stmt(program->as.stmt_list.stmts[i], global);
    }

    symtab_exit_scope(global);
}
