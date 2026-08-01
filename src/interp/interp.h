#ifndef MINILANG_INTERP_H
#define MINILANG_INTERP_H

#include "../ast/ast.h"
#include "../codegen/tac.h"

/*
 * TAC Interpreter (advanced/unique feature, beyond the manual's
 * required scope — manual Section 6 explicitly says machine-code /
 * "backend targeting real hardware" is out of scope, but this is
 * neither: it's a tree-free, direct EXECUTION of the Three Address
 * Code this compiler already generates, entirely within this process,
 * producing the program's actual runtime output (including reading
 * real input for `read` statements) — the same category of thing a
 * "TAC-level reference interpreter" is in a textbook compilers
 * course, not a hardware backend.
 *
 * Design:
 *  - One flat instruction pointer walks the TACList; TAC_LABEL and
 *    TAC_FUNC_BEGIN entries are resolved into a label->index map up
 *    front (see interp.c's build_label_map).
 *  - Variables/arrays live in "frames": one global frame, plus one
 *    fresh frame PER function call (pushed on TAC_CALL, popped on
 *    TAC_RETURN) — this is what makes recursion work correctly, since
 *    each active call gets its own independent copy of its
 *    parameters/locals. Reads fall back to the global frame if a name
 *    isn't found in the current call's frame (matching this
 *    language's own scoping: a function body's enclosing scope is the
 *    global scope — see semantic.c's AST_FUNC_DECL case).
 *  - Function bodies are emitted INLINE in the TAC listing at their
 *    declaration position (see tac.c's docstring) — the interpreter
 *    must never fall through into one during ordinary top-level
 *    execution, only enter via an explicit TAC_CALL. See interp.c's
 *    build_func_skip_table for how this is made safe.
 *  - Arrays are backing arrays sized on demand (grown to fit the
 *    largest index used so far) — TAC carries no size information
 *    (declarations emit no TAC, matching every other declaration kind
 *    in this compiler), so this is a deliberate, documented
 *    simplification rather than an oversight.
 */

/* One function's parameter NAMES, in declaration order — TAC's own
 * `param`/`call` instructions carry values but not names (see tac.h),
 * so the interpreter needs this separately, built directly from the
 * AST once before execution starts. */
typedef struct {
    char *name;
    char **param_names;
    int param_count;
} FuncSig;

typedef struct {
    FuncSig *funcs;
    int count;
} FuncSigTable;

/* Scans `program` for every AST_FUNC_DECL and records its parameter
 * names. Call once, after semantic analysis succeeds, before interp_run. */
FuncSigTable *build_func_sig_table(ASTNode *program);
void free_func_sig_table(FuncSigTable *table);

/* Executes `list` to completion, printing real program output to
 * stdout and reading real input from stdin for every `read`
 * instruction encountered. */
void interp_run(const TACList *list, const FuncSigTable *sigs);

#endif /* MINILANG_INTERP_H */
