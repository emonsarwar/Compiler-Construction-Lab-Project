#ifndef MINILANG_TAC_H
#define MINILANG_TAC_H

#include <stdio.h>
#include "../ast/ast.h"

/*
 * Three Address Code generation (project manual Section 4.6).
 *
 * Design: gen_expr() walks an expression subtree and returns its
 * "place" — a string naming where its value lives after the emitted
 * instructions run: either a variable name, a literal's own text
 * (e.g. "5"), or a freshly allocated temporary ("t1", "t2", ...). This
 * is what lets literals and plain variable reads emit ZERO
 * instructions (matching the manual's own example exactly: `a = 5`
 * compiles to one instruction, not a redundant `t1 = 5; a = t1`).
 *
 * Declarations emit nothing — TAC is about computation, not the
 * symbol table (which the semantic analyzer already built); the
 * manual's own worked example confirms this: `int a; int b; int c;`
 * produce no TAC lines at all.
 *
 * Control flow (if / if-else / while) is generated with the standard
 * label+conditional-jump scheme taught for this exact assignment
 * shape — see the .c file for the instruction sequence used for each.
 *
 * Scope: only what Section 4.6 asks for. In particular, '&&' and '||'
 * are generated as plain eager binary TAC operations (`t1 = a && b`),
 * NOT short-circuited into extra jumps — the manual doesn't require
 * short-circuit evaluation here, and keeping every binary operator
 * generated the same uniform way keeps this code simpler. (Note this
 * means side-effecting operands would evaluate eagerly, but this
 * language's expressions have no side effects to short-circuit
 * around, so it's a distinction without a behavioral difference here.)
 *
 * --- Bonus feature: functions (manual Section 14) ---
 * Standard textbook TAC scheme: `param` declares each argument value
 * immediately before a `call`; `call` invokes a function by name with
 * an explicit argument count (assigning the result, or not, depending
 * on whether the call is used as a value or as a bare statement); a
 * function's own TAC is introduced by a label matching its name
 * (TAC_FUNC_BEGIN) and ends wherever its `return` statements are.
 * Functions are emitted inline in declaration order alongside the
 * top-level "main" statements — see tac_generate()'s AST_FUNC_DECL
 * case — since they only ever run when called, their position in the
 * linear listing doesn't affect program behavior.
 */

typedef enum {
    TAC_ASSIGN,     /* result = arg1                         */
    TAC_BINOP,      /* result = arg1 op arg2                 */
    TAC_UNARYOP,    /* result = op arg1                      */
    TAC_LABEL,      /* result:                                */
    TAC_GOTO,       /* goto arg1                              */
    TAC_IF_FALSE,   /* if_false arg1 goto arg2                */
    TAC_PRINT,      /* print arg1                             */
    /* --- Bonus feature (functions, manual Section 14) --- */
    TAC_FUNC_BEGIN, /* result:               (a label marking a function's entry point) */
    TAC_PARAM,      /* param arg1             (declares one argument value ahead of a call) */
    TAC_CALL,       /* result = call arg1, arg2   OR   call arg1, arg2  (result NULL = discarded, e.g. a call_stmt) — arg2 is the argument count */
    TAC_RETURN,     /* return arg1 */
    /* --- Advanced/unique extensions --- */
    TAC_ARR_STORE,  /* result[arg2] = arg1   (result = array name, arg2 = index place, arg1 = value place) */
    TAC_ARR_LOAD,   /* result = arg1[arg2]   (arg1 = array name, arg2 = index place) */
    TAC_READ        /* read arg1             (arg1 = variable name, or "arrname[idx]" text for an array element) */
} TACOpKind;

typedef struct {
    TACOpKind kind;
    char *result;  /* destination var/temp, or label name for TAC_LABEL — NULL if unused */
    char *arg1;    /* first operand / condition / goto target — NULL if unused */
    char *op;      /* operator text for TAC_BINOP / TAC_UNARYOP — NULL otherwise */
    char *arg2;    /* second operand (TAC_BINOP) or jump target (TAC_IF_FALSE) — NULL otherwise */
} TACInstr;

typedef struct {
    TACInstr *instrs;
    int count;
    int capacity;
    /* Advanced/unique extension (needed by the TAC interpreter, see
     * src/interp/): exactly where each function's body starts and
     * ends in the flat instruction list. Function bodies are emitted
     * INLINE at their declaration position (see tac_generate's
     * docstring below) — an interpreter walking top-level code
     * linearly must skip a function's body entirely rather than
     * falling into it, and "the next TAC_FUNC_BEGIN, or end of list"
     * is WRONG for whichever function happens to be declared last (it
     * would skip everything after it too) — hence tracking this
     * precisely, right here, during generation itself. */
    struct { char *name; int begin; int end; } *func_ranges;
    int func_range_count;
    int func_range_capacity;
} TACList;

TACList *tac_list_new(void);
void tac_list_free(TACList *list);

/* Generate TAC for an entire AST_PROGRAM into `list`. */
void tac_generate(ASTNode *program, TACList *list);

/* Print the generated TAC in the manual's illustrative style, e.g.:
 *   a = 5
 *   t1 = b * 2
 *   L1:
 *   if_false t3 goto L2
 *   goto L1
 */
void tac_print(const TACList *list, FILE *out);

#endif /* MINILANG_TAC_H */
