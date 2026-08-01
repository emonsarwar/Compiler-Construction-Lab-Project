#include <stdio.h>
#include <string.h>
#include "ast/ast.h"
#include "symbol_table/symbol_table.h"
#include "semantic/semantic.h"
#include "codegen/tac.h"
#include "common/error.h"
#include "interp/interp.h"
#include "optimize/optimize.h"

/* Provided by the generated parser (src/parser/parser.y). */
extern FILE *yyin;
extern int yyparse(void);
extern ASTNode *g_program;

static void print_section(const char *title) {
    printf("\n=== %s ===\n", title);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source-file.mc> [-dot <output.dot>] [-O] [-run]\n", argv[0]);
        fprintf(stderr, "  -dot <file>  write a Graphviz AST to <file> (bonus feature, Section 14)\n");
        fprintf(stderr, "  -O           enable optimization: constant folding + dead code elimination\n");
        fprintf(stderr, "  -run         actually execute the generated TAC and show real program output\n");
        return 1;
    }

    const char *dot_path = NULL;
    int opt_optimize = 0;
    int opt_run = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-dot") == 0 && i + 1 < argc) {
            dot_path = argv[++i];
        } else if (strcmp(argv[i], "-O") == 0) {
            opt_optimize = 1;
        } else if (strcmp(argv[i], "-run") == 0) {
            opt_run = 1;
        }
    }

    yyin = fopen(argv[1], "r");
    if (!yyin) {
        perror(argv[1]);
        return 1;
    }

    /* --- Lexing + Parsing --- */
    yyparse();
    fclose(yyin);

    if (error_count() > 0) {
        fprintf(stderr, "\nCompilation failed: %d lexical/syntax error(s). Stopping before semantic analysis.\n", error_count());
        return 1;
    }

    if (!g_program) {
        fprintf(stderr, "\nCompilation failed: AST is null after parsing.\n");
        return 1;
    }

    print_section("Abstract Syntax Tree");
    ast_print(g_program, 0);

    /* --- Semantic Analysis (symbol table + type checking) --- */
    analyze_program(g_program);

    if (error_count() > 0) {
        fprintf(stderr, "\nCompilation failed: %d semantic error(s). Stopping before code generation.\n", error_count());
        return 1;
    }

    print_section("Annotated / Validated AST (types resolved)");
    ast_print(g_program, 0);
    printf("\nSemantic analysis passed: no errors.\n");

    /* Advanced/unique extension: constant folding (manual Section 14
     * bonus item), applied to the validated AST, strictly before code
     * generation, so the TAC below already reflects any folded values. */
    if (opt_optimize) {
        int folded = fold_constants(g_program);
        print_section("Optimization: Constant Folding");
        printf("Folded %d constant sub-expression(s).\n", folded);
    }

    /* Bonus feature: Graphviz visualization (manual Section 14),
     * opt-in via -dot so default output is unchanged for everyone not
     * using it. Render with: dot -Tpng out.dot -o out.png */
    if (dot_path) {
        FILE *dot_out = fopen(dot_path, "w");
        if (!dot_out) {
            perror(dot_path);
        } else {
            ast_write_dot(g_program, dot_out);
            fclose(dot_out);
            printf("\nWrote Graphviz AST to %s (render with: dot -Tpng %s -o ast.png)\n", dot_path, dot_path);
        }
    }

    /* --- Intermediate Code Generation --- */
    TACList *tac = tac_list_new();
    tac_generate(g_program, tac);

    /* Advanced/unique extension: dead code elimination (manual
     * Section 14 bonus item), applied to the generated TAC. */
    if (opt_optimize) {
        int removed = eliminate_dead_code(tac);
        print_section("Optimization: Dead Code Elimination");
        printf("Removed %d unreachable instruction(s).\n", removed);
    }

    print_section("Three Address Code");
    tac_print(tac, stdout);

    /* Advanced/unique extension: actually RUN the generated TAC and
     * show the program's real output (manual Section 6 explicitly
     * scopes out a hardware backend — this is a TAC-level interpreter,
     * not one, see interp.h). */
    if (opt_run) {
        print_section("Program Output (-run)");
        FuncSigTable *sigs = build_func_sig_table(g_program);
        interp_run(tac, sigs);
        free_func_sig_table(sigs);
    }

    tac_list_free(tac);
    ast_free(g_program);
    return 0;
}
