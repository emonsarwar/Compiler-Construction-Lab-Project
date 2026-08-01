#define _POSIX_C_SOURCE 200809L  /* for strdup */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tac.h"

/* --- temp/label name generators ---
 * Simple monotonic counters. This tool compiles one file and exits, so
 * there's no need to reset these between compilations. */
static int g_temp_counter = 0;
static int g_label_counter = 0;

static char *new_temp(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "t%d", ++g_temp_counter);
    return strdup(buf);
}

static char *new_label(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "L%d", ++g_label_counter);
    return strdup(buf);
}

/* --- list management --- */

TACList *tac_list_new(void) {
    TACList *list = (TACList *)malloc(sizeof(TACList));
    list->count = 0;
    list->capacity = 8;
    list->instrs = (TACInstr *)malloc(sizeof(TACInstr) * (size_t)list->capacity);
    list->func_ranges = NULL;
    list->func_range_count = 0;
    list->func_range_capacity = 0;
    return list;
}

void tac_list_free(TACList *list) {
    for (int i = 0; i < list->count; i++) {
        TACInstr *ins = &list->instrs[i];
        free(ins->result);
        free(ins->arg1);
        free(ins->op);
        free(ins->arg2);
    }
    free(list->instrs);
    for (int i = 0; i < list->func_range_count; i++) free(list->func_ranges[i].name);
    free(list->func_ranges);
    free(list);
}

static void tac_emit(TACList *list, TACInstr instr) {
    if (list->count == list->capacity) {
        int new_capacity = list->capacity * 2;
        TACInstr *new_instrs = (TACInstr *)realloc(list->instrs, sizeof(TACInstr) * (size_t)new_capacity);
        if (!new_instrs) {
            fprintf(stderr, "fatal: out of memory in tac_emit\n");
            exit(1);
        }
        list->capacity = new_capacity;
        list->instrs = new_instrs;
    }
    list->instrs[list->count++] = instr;
}

/* Each emit_* function strdup()s every string it's given, so the TACList
 * owns fully independent copies — callers remain responsible for
 * freeing whatever they passed in (see gen_expr/gen_stmt below). */

static void emit_assign(TACList *list, const char *result, const char *arg1) {
    TACInstr instr = { .kind = TAC_ASSIGN, .result = strdup(result), .arg1 = strdup(arg1), .op = NULL, .arg2 = NULL };
    tac_emit(list, instr);
}
static void emit_binop(TACList *list, const char *result, const char *arg1, const char *op, const char *arg2) {
    TACInstr instr = { .kind = TAC_BINOP, .result = strdup(result), .arg1 = strdup(arg1), .op = strdup(op), .arg2 = strdup(arg2) };
    tac_emit(list, instr);
}
static void emit_unaryop(TACList *list, const char *result, const char *op, const char *arg1) {
    TACInstr instr = { .kind = TAC_UNARYOP, .result = strdup(result), .arg1 = strdup(arg1), .op = strdup(op), .arg2 = NULL };
    tac_emit(list, instr);
}
static void emit_label(TACList *list, const char *label) {
    TACInstr instr = { .kind = TAC_LABEL, .result = strdup(label), .arg1 = NULL, .op = NULL, .arg2 = NULL };
    tac_emit(list, instr);
}
static void emit_goto(TACList *list, const char *label) {
    TACInstr instr = { .kind = TAC_GOTO, .result = NULL, .arg1 = strdup(label), .op = NULL, .arg2 = NULL };
    tac_emit(list, instr);
}
static void emit_if_false(TACList *list, const char *cond, const char *label) {
    TACInstr instr = { .kind = TAC_IF_FALSE, .result = NULL, .arg1 = strdup(cond), .op = NULL, .arg2 = strdup(label) };
    tac_emit(list, instr);
}
static void emit_print(TACList *list, const char *arg1) {
    TACInstr instr = { .kind = TAC_PRINT, .result = NULL, .arg1 = strdup(arg1), .op = NULL, .arg2 = NULL };
    tac_emit(list, instr);
}

/* --- Bonus feature (functions) --- */

static void emit_func_begin(TACList *list, const char *name) {
    TACInstr instr = { .kind = TAC_FUNC_BEGIN, .result = strdup(name), .arg1 = NULL, .op = NULL, .arg2 = NULL };
    tac_emit(list, instr);
}
static void emit_param(TACList *list, const char *value) {
    TACInstr instr = { .kind = TAC_PARAM, .result = NULL, .arg1 = strdup(value), .op = NULL, .arg2 = NULL };
    tac_emit(list, instr);
}
/* result may be NULL (call used as a bare statement — its return value, if any, is discarded). */
static void emit_call(TACList *list, const char *result, const char *func_name, int arg_count) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", arg_count);
    TACInstr instr = { .kind = TAC_CALL, .result = result ? strdup(result) : NULL, .arg1 = strdup(func_name), .op = NULL, .arg2 = strdup(buf) };
    tac_emit(list, instr);
}
static void emit_return(TACList *list, const char *value) {
    TACInstr instr = { .kind = TAC_RETURN, .result = NULL, .arg1 = strdup(value), .op = NULL, .arg2 = NULL };
    tac_emit(list, instr);
}

/* --- Advanced/unique extensions --- */

static void emit_arr_store(TACList *list, const char *arr_name, const char *index_place, const char *value_place) {
    TACInstr instr = { .kind = TAC_ARR_STORE, .result = strdup(arr_name), .arg1 = strdup(value_place), .op = NULL, .arg2 = strdup(index_place) };
    tac_emit(list, instr);
}
static char *emit_arr_load(TACList *list, const char *arr_name, const char *index_place) {
    char *t = new_temp();
    TACInstr instr = { .kind = TAC_ARR_LOAD, .result = strdup(t), .arg1 = strdup(arr_name), .op = NULL, .arg2 = strdup(index_place) };
    tac_emit(list, instr);
    return t;
}
static void emit_read(TACList *list, const char *target) {
    TACInstr instr = { .kind = TAC_READ, .result = NULL, .arg1 = strdup(target), .op = NULL, .arg2 = NULL };
    tac_emit(list, instr);
}

/* --- expression codegen: returns a freshly-owned "place" string --- */

static char *gen_expr(ASTNode *node, TACList *list) {
    switch (node->kind) {
        case AST_INT_LIT: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", node->as.int_lit.value);
            return strdup(buf);
        }
        case AST_FLOAT_LIT: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", node->as.float_lit.value);
            return strdup(buf);
        }
        case AST_BOOL_LIT:
            return strdup(node->as.bool_lit.value ? "true" : "false");

        case AST_IDENT:
            return strdup(node->as.ident.name);

        /* Advanced/unique extension: a string literal's TAC "place" is
         * its text wrapped in quotes, matching how int/float/bool
         * literals already work — printed/assigned directly with zero
         * extra instructions. */
        case AST_STRING_LIT: {
            size_t len = strlen(node->as.string_lit.value);
            char *buf = (char *)malloc(len + 3);
            buf[0] = '"';
            memcpy(buf + 1, node->as.string_lit.value, len);
            buf[len + 1] = '"';
            buf[len + 2] = '\0';
            return buf;
        }

        case AST_ARRAY_ACCESS: {
            char *idx = gen_expr(node->as.array_access.index, list);
            char *t = emit_arr_load(list, node->as.array_access.name, idx);
            free(idx);
            return t;
        }

        case AST_BINOP: {
            char *l = gen_expr(node->as.binop.left, list);
            char *r = gen_expr(node->as.binop.right, list);
            char *t = new_temp();
            emit_binop(list, t, l, node->as.binop.op, r);
            free(l);
            free(r);
            return t;
        }

        case AST_UNARYOP: {
            char *v = gen_expr(node->as.unaryop.operand, list);
            char *t = new_temp();
            emit_unaryop(list, t, node->as.unaryop.op, v);
            free(v);
            return t;
        }

        /* Bonus feature (functions): evaluate each argument left-to-right,
         * declaring it with `param` immediately (matching the standard
         * TAC scheme — see tac.h), then call and capture the result in
         * a fresh temp. */
        case AST_CALL: {
            for (int i = 0; i < node->as.call.arg_count; i++) {
                char *arg_place = gen_expr(node->as.call.args[i], list);
                emit_param(list, arg_place);
                free(arg_place);
            }
            char *t = new_temp();
            emit_call(list, t, node->as.call.callee, node->as.call.arg_count);
            return t;
        }

        default:
            /* Not reachable for a program that passed semantic analysis. */
            return strdup("<?>");
    }
}

static void gen_block(ASTNode *block, TACList *list);

static void gen_stmt(ASTNode *node, TACList *list) {
    switch (node->kind) {
        case AST_VAR_DECL:
            /* Declarations produce no TAC — see tac.h's docstring; the
             * manual's own worked example confirms `int a;` etc. don't
             * appear in the generated TAC at all. */
            break;

        case AST_ASSIGN: {
            char *v = gen_expr(node->as.assign.value, list);
            emit_assign(list, node->as.assign.name, v);
            free(v);
            break;
        }

        case AST_PRINT: {
            char *v = gen_expr(node->as.print_stmt.value, list);
            emit_print(list, v);
            free(v);
            break;
        }

        case AST_IF: {
            char *cond = gen_expr(node->as.if_stmt.cond, list);
            if (node->as.if_stmt.else_branch) {
                char *else_label = new_label();
                char *end_label = new_label();
                emit_if_false(list, cond, else_label);
                free(cond);
                gen_block(node->as.if_stmt.then_branch, list);
                emit_goto(list, end_label);
                emit_label(list, else_label);
                gen_block(node->as.if_stmt.else_branch, list);
                emit_label(list, end_label);
                free(else_label);
                free(end_label);
            } else {
                char *end_label = new_label();
                emit_if_false(list, cond, end_label);
                free(cond);
                gen_block(node->as.if_stmt.then_branch, list);
                emit_label(list, end_label);
                free(end_label);
            }
            break;
        }

        case AST_WHILE: {
            char *start_label = new_label();
            char *end_label = new_label();
            emit_label(list, start_label);
            char *cond = gen_expr(node->as.while_stmt.cond, list);
            emit_if_false(list, cond, end_label);
            free(cond);
            gen_block(node->as.while_stmt.body, list);
            emit_goto(list, start_label);
            emit_label(list, end_label);
            free(start_label);
            free(end_label);
            break;
        }

        case AST_BLOCK:
            gen_block(node, list);
            break;

        /* --- Bonus features --- */

        case AST_CALL_STMT: {
            for (int i = 0; i < node->as.call.arg_count; i++) {
                char *arg_place = gen_expr(node->as.call.args[i], list);
                emit_param(list, arg_place);
                free(arg_place);
            }
            emit_call(list, NULL, node->as.call.callee, node->as.call.arg_count); /* NULL result: discarded */
            break;
        }

        case AST_RETURN: {
            char *v = gen_expr(node->as.ret.value, list);
            emit_return(list, v);
            free(v);
            break;
        }

        case AST_FOR: {
            gen_stmt(node->as.for_stmt.init, list); /* an Assign or IncDec — see parser.y */
            char *start_label = new_label();
            char *end_label = new_label();
            emit_label(list, start_label);
            char *cond = gen_expr(node->as.for_stmt.cond, list);
            emit_if_false(list, cond, end_label);
            free(cond);
            gen_block(node->as.for_stmt.body, list);
            gen_stmt(node->as.for_stmt.update, list);
            emit_goto(list, start_label);
            emit_label(list, end_label);
            free(start_label);
            free(end_label);
            break;
        }

        /* do-while's body always runs at least once, so — unlike
         * while/for — the condition check comes AFTER the body. Using
         * only if_false (no separate if_true instruction needed):
         * if the condition is false, skip the goto-back and fall out;
         * if true, fall through to the unconditional goto and loop. */
        case AST_DO_WHILE: {
            char *start_label = new_label();
            char *end_label = new_label();
            emit_label(list, start_label);
            gen_block(node->as.do_while.body, list);
            char *cond = gen_expr(node->as.do_while.cond, list);
            emit_if_false(list, cond, end_label);
            free(cond);
            emit_goto(list, start_label);
            emit_label(list, end_label);
            free(start_label);
            free(end_label);
            break;
        }

        /* x++ / x-- compiles directly to one instruction, `x = x + 1`
         * (result written straight to the variable, not a temp) —
         * simpler than routing through a temp for no reason. */
        case AST_INCDEC: {
            const char *op_symbol = (strcmp(node->as.incdec.op, "++") == 0) ? "+" : "-";
            emit_binop(list, node->as.incdec.name, node->as.incdec.name, op_symbol, "1");
            break;
        }

        case AST_FUNC_DECL: {
            emit_func_begin(list, node->as.func_decl.name);
            gen_block(node->as.func_decl.body, list);
            break;
        }

        /* --- Advanced/unique extensions --- */

        case AST_ARRAY_DECL:
            /* Same rationale as AST_VAR_DECL: declarations produce no TAC. */
            break;

        case AST_ARRAY_ASSIGN: {
            char *idx = gen_expr(node->as.array_assign.index, list);
            char *v = gen_expr(node->as.array_assign.value, list);
            emit_arr_store(list, node->as.array_assign.name, idx, v);
            free(idx);
            free(v);
            break;
        }

        /* switch/case lowers to a standard equality-check-and-branch
         * chain: evaluate the subject once, then for each `case` in
         * turn, compare and conditionally run its body before jumping
         * to the end; a `default:` arm (if present) is emitted last as
         * an unconditional fallback, matching how "no case matched"
         * should behave regardless of where `default:` appeared in the
         * source. */
        case AST_SWITCH: {
            char *subj = gen_expr(node->as.switch_stmt.subject, list);
            char *end_label = new_label();
            ASTNode *default_body = NULL;
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                SwitchCase *c = &node->as.switch_stmt.cases[i];
                if (c->is_default) {
                    default_body = c->body;
                    continue;
                }
                char *case_val = gen_expr(c->value, list);
                char *eq_temp = new_temp();
                emit_binop(list, eq_temp, subj, "==", case_val);
                char *next_label = new_label();
                emit_if_false(list, eq_temp, next_label);
                free(eq_temp);
                free(case_val);
                gen_block(c->body, list);
                emit_goto(list, end_label);
                emit_label(list, next_label);
                free(next_label);
            }
            if (default_body) {
                gen_block(default_body, list);
            }
            emit_label(list, end_label);
            free(subj);
            free(end_label);
            break;
        }

        case AST_BREAK:
            /* Unreachable post-parse (see parser.y: `break;` is only
             * ever consumed as switch_case's own literal terminator,
             * never turned into a standalone AST_BREAK node), kept only
             * so this switch stays exhaustive. */
            break;

        /* `read` (advanced feature) lowers to one TAC_READ instruction
         * naming its target; a plain variable's target is just its
         * name, while an array element's target is printed as
         * "name[index-place]" text so the interpreter (src/interp/)
         * can parse it back into a store — see interp.c's exec_read(). */
        case AST_READ: {
            ASTNode *target = node->as.read_stmt.target;
            if (target->kind == AST_ARRAY_ACCESS) {
                char *idx = gen_expr(target->as.array_access.index, list);
                char buf[256];
                snprintf(buf, sizeof(buf), "%s[%s]", target->as.array_access.name, idx);
                emit_read(list, buf);
                free(idx);
            } else {
                emit_read(list, target->as.ident.name);
            }
            break;
        }

        default:
            break; /* not a valid statement kind — unreachable post-semantic-analysis */
    }
}

static void gen_block(ASTNode *block, TACList *list) {
    for (int i = 0; i < block->as.stmt_list.count; i++) {
        gen_stmt(block->as.stmt_list.stmts[i], list);
    }
}

void tac_generate(ASTNode *program, TACList *list) {
    for (int i = 0; i < program->as.stmt_list.count; i++) {
        ASTNode *item = program->as.stmt_list.stmts[i];
        int begin = list->count;
        gen_stmt(item, list);
        if (item->kind == AST_FUNC_DECL) {
            if (list->func_range_count == list->func_range_capacity) {
                list->func_range_capacity = list->func_range_capacity ? list->func_range_capacity * 2 : 4;
                list->func_ranges = realloc(list->func_ranges, sizeof(*list->func_ranges) * (size_t)list->func_range_capacity);
            }
            list->func_ranges[list->func_range_count].name = strdup(item->as.func_decl.name);
            list->func_ranges[list->func_range_count].begin = begin;
            list->func_ranges[list->func_range_count].end = list->count;
            list->func_range_count++;
        }
    }
}

/* --- printing --- */

void tac_print(const TACList *list, FILE *out) {
    for (int i = 0; i < list->count; i++) {
        const TACInstr *ins = &list->instrs[i];
        switch (ins->kind) {
            case TAC_ASSIGN:
                fprintf(out, "%s = %s\n", ins->result, ins->arg1);
                break;
            case TAC_BINOP:
                fprintf(out, "%s = %s %s %s\n", ins->result, ins->arg1, ins->op, ins->arg2);
                break;
            case TAC_UNARYOP:
                fprintf(out, "%s = %s%s\n", ins->result, ins->op, ins->arg1);
                break;
            case TAC_LABEL:
                fprintf(out, "%s:\n", ins->result);
                break;
            case TAC_GOTO:
                fprintf(out, "goto %s\n", ins->arg1);
                break;
            case TAC_IF_FALSE:
                fprintf(out, "if_false %s goto %s\n", ins->arg1, ins->arg2);
                break;
            case TAC_PRINT:
                fprintf(out, "print %s\n", ins->arg1);
                break;
            case TAC_FUNC_BEGIN:
                fprintf(out, "%s:\n", ins->result);
                break;
            case TAC_PARAM:
                fprintf(out, "param %s\n", ins->arg1);
                break;
            case TAC_CALL:
                if (ins->result) fprintf(out, "%s = call %s, %s\n", ins->result, ins->arg1, ins->arg2);
                else fprintf(out, "call %s, %s\n", ins->arg1, ins->arg2);
                break;
            case TAC_RETURN:
                fprintf(out, "return %s\n", ins->arg1);
                break;
            case TAC_ARR_STORE:
                fprintf(out, "%s[%s] = %s\n", ins->result, ins->arg2, ins->arg1);
                break;
            case TAC_ARR_LOAD:
                fprintf(out, "%s = %s[%s]\n", ins->result, ins->arg1, ins->arg2);
                break;
            case TAC_READ:
                fprintf(out, "read %s\n", ins->arg1);
                break;
        }
    }
}
