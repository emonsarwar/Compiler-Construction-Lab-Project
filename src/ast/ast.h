#ifndef MINILANG_AST_H
#define MINILANG_AST_H

#include <stdio.h>

/*
 * Abstract Syntax Tree for the mini language (project manual Section 5).
 *
 * Design: one ASTNode struct, tagged by `kind`, with a C union holding
 * only the fields relevant to that kind. This is the standard shape
 * for a hand-written-in-C AST (the same pattern used in most
 * textbook/course compilers): a single node type keeps constructor
 * functions and the recursive walkers in semantic.c / tac.c simple
 * (`switch (node->kind)`), while the union avoids wasting memory on
 * fields a given node kind never uses.
 *
 * Every node carries `line` (for error messages) and `type` (filled
 * in by the semantic analyzer — see semantic.h; it is TYPE_UNKNOWN
 * until then, and codegen/tac.c never runs on a tree that still has
 * TYPE_UNKNOWN anywhere, since main.c stops after semantic analysis
 * if any error was reported).
 */

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_STRING,   /* advanced feature: string type — see docs/bonus_features.md */
    TYPE_VOID,     /* statements that don't produce a value (declarations, print, ...) */
    TYPE_UNKNOWN   /* not yet determined — only valid before semantic analysis runs */
} DataType;

const char *datatype_to_string(DataType t);

typedef enum {
    AST_PROGRAM,
    AST_BLOCK,
    AST_VAR_DECL,
    AST_ASSIGN,
    AST_IF,
    AST_WHILE,
    AST_PRINT,
    AST_BINOP,
    AST_UNARYOP,
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_BOOL_LIT,
    AST_IDENT,
    /* --- Bonus features (manual Section 14) — see docs/bonus_features.md --- */
    AST_FUNC_DECL,
    AST_CALL,       /* function call used as a value-producing expression */
    AST_CALL_STMT,  /* function call used as a bare statement (return value discarded) */
    AST_RETURN,
    AST_FOR,
    AST_DO_WHILE,
    AST_INCDEC,     /* x++ or x-- , always used as a statement */
    /* --- Advanced/unique extensions (beyond the manual's bonus list) --- */
    AST_STRING_LIT,
    AST_ARRAY_DECL,   /* e.g. int arr[10]; */
    AST_ARRAY_ASSIGN, /* e.g. arr[i] = expr; */
    AST_ARRAY_ACCESS, /* e.g. arr[i] used as a value */
    AST_SWITCH,
    AST_BREAK,
    AST_READ          /* e.g. read x;  — reads user input at runtime */
} ASTNodeKind;

typedef struct ASTNode ASTNode;

/* AST_PROGRAM and AST_BLOCK: an ordered, growable list of statements. */
typedef struct {
    ASTNode **stmts;
    int count;
    int capacity;
} StmtList;

typedef struct {
    DataType var_type;   /* declared type, e.g. TYPE_INT for `int x;` */
    char *name;
} VarDeclData;

typedef struct {
    char *name;
    ASTNode *value;
} AssignData;

typedef struct {
    ASTNode *cond;
    ASTNode *then_branch;  /* always an AST_BLOCK */
    ASTNode *else_branch;  /* an AST_BLOCK, or NULL if there is no else */
} IfData;

typedef struct {
    ASTNode *cond;
    ASTNode *body;         /* always an AST_BLOCK */
} WhileData;

typedef struct {
    ASTNode *value;
} PrintData;

typedef struct {
    char *op;               /* "+" "-" "*" "/" "%" "<" ">" "<=" ">=" "==" "!=" "&&" "||" */
    ASTNode *left;
    ASTNode *right;
} BinOpData;

typedef struct {
    char *op;               /* "-" (negation) or "!" (logical not) */
    ASTNode *operand;
} UnaryOpData;

typedef struct { int value; } IntLitData;
typedef struct { double value; } FloatLitData;
typedef struct { int value; } BoolLitData;   /* 0 or 1 */
typedef struct { char *name; } IdentData;

/* --- Bonus features (manual Section 14) — see docs/bonus_features.md --- */

typedef struct {
    char *name;
    DataType type;
} ParamData;

typedef struct {
    char *name;
    DataType return_type;
    ParamData *params;
    int param_count;
    ASTNode *body;   /* always AST_BLOCK */
} FuncDeclData;

/* Shared by AST_CALL (expression position) and AST_CALL_STMT (statement
 * position, return value discarded) — same data, different AST kind. */
typedef struct {
    char *callee;
    ASTNode **args;
    int arg_count;
} CallData;

typedef struct {
    ASTNode *value;   /* every function in this language must return a value on every path — see semantic.h */
} ReturnData;

typedef struct {
    ASTNode *init;    /* AST_VAR_DECL or AST_ASSIGN, or NULL */
    ASTNode *cond;    /* or NULL (treated as always-true, like C) */
    ASTNode *update;  /* AST_ASSIGN or AST_INCDEC, or NULL */
    ASTNode *body;    /* always AST_BLOCK */
} ForData;

typedef struct {
    ASTNode *body;    /* always AST_BLOCK */
    ASTNode *cond;
} DoWhileData;

typedef struct {
    char *name;
    char *op;         /* "++" or "--" */
} IncDecData;

/* --- Advanced/unique extensions (beyond the manual's bonus list) --- */

typedef struct { char *value; } StringLitData;

typedef struct {
    DataType elem_type;   /* element type, e.g. TYPE_INT for `int arr[10];` */
    char *name;
    int size;              /* fixed size, must be a positive int literal at declaration */
} ArrayDeclData;

typedef struct {
    char *name;
    ASTNode *index;
    ASTNode *value;
} ArrayAssignData;

typedef struct {
    char *name;
    ASTNode *index;
} ArrayAccessData;

/* One `case CONST: stmts` arm of a switch. `is_default` marks the
 * (at most one) `default:` arm, in which case `value` is unused. */
typedef struct {
    int is_default;
    ASTNode *value;   /* AST_INT_LIT — case labels are restricted to int constants, kept simple on purpose */
    ASTNode *body;     /* AST_BLOCK — statements up to (and not including) `break;` */
} SwitchCase;

typedef struct {
    ASTNode *subject;
    SwitchCase *cases;
    int case_count;
    int case_capacity;
} SwitchData;

typedef struct { ASTNode *target; } ReadData;   /* target is an AST_IDENT (or AST_ARRAY_ACCESS) naming where input is stored */

/* Transient parsing-helper types (NOT AST nodes) — grammar actions in
 * parser.y accumulate parameters/arguments into one of these while
 * reducing a comma-separated list, then their contents are copied into
 * the final AST_FUNC_DECL / AST_CALL node and the helper is freed.
 * Kept separate from ASTNode because a parameter list isn't a
 * meaningful standalone language construct the way e.g. a block is. */

typedef struct {
    ParamData *params;
    int count;
    int capacity;
} ParamList;

ParamList *ast_param_list_new(void);
void ast_param_list_add(ParamList *list, char *name, DataType type);
void ast_param_list_free(ParamList *list);   /* frees the wrapper only — ownership of `params` transfers to the FuncDeclData that consumes it */

typedef struct {
    ASTNode **args;
    int count;
    int capacity;
} ArgList;

ArgList *ast_arg_list_new(void);
void ast_arg_list_add(ArgList *list, ASTNode *arg);
void ast_arg_list_free(ArgList *list);       /* frees the wrapper only — ownership of `args` transfers to the CallData that consumes it */

/* Transient parsing-helper (advanced feature: switch-case) — accumulates
 * `case`/`default` arms while reducing switch_case_list, mirroring
 * ParamList/ArgList above. `value == NULL` marks the `default:` arm. */
typedef struct {
    ASTNode *value;
    ASTNode *body;
} CaseItem;

typedef struct {
    CaseItem *items;
    int count;
    int capacity;
} CaseList;

CaseList *ast_case_list_new(void);
void ast_case_list_add(CaseList *list, ASTNode *value, ASTNode *body);
void ast_case_list_free(CaseList *list);

struct ASTNode {
    ASTNodeKind kind;
    int line;
    DataType type;   /* set by the semantic analyzer; TYPE_UNKNOWN until then */
    union {
        StmtList    stmt_list;   /* AST_PROGRAM, AST_BLOCK */
        VarDeclData var_decl;
        AssignData  assign;
        IfData      if_stmt;
        WhileData   while_stmt;
        PrintData   print_stmt;
        BinOpData   binop;
        UnaryOpData unaryop;
        IntLitData   int_lit;
        FloatLitData float_lit;
        BoolLitData  bool_lit;
        IdentData    ident;
        FuncDeclData func_decl;
        CallData     call;
        ReturnData   ret;
        ForData      for_stmt;
        DoWhileData  do_while;
        IncDecData   incdec;
        /* --- Advanced/unique extensions --- */
        StringLitData    string_lit;
        ArrayDeclData     array_decl;
        ArrayAssignData   array_assign;
        ArrayAccessData   array_access;
        SwitchData        switch_stmt;
        ReadData          read_stmt;
    } as;
};

/* --- Constructors. Every one takes the source line for error reporting. --- */

ASTNode *ast_new_stmt_list(ASTNodeKind kind /* AST_PROGRAM or AST_BLOCK */, int line);
void     ast_stmt_list_add(ASTNode *list, ASTNode *stmt);

ASTNode *ast_new_var_decl(int line, DataType type, char *name);
ASTNode *ast_new_assign(int line, char *name, ASTNode *value);
ASTNode *ast_new_if(int line, ASTNode *cond, ASTNode *then_branch, ASTNode *else_branch);
ASTNode *ast_new_while(int line, ASTNode *cond, ASTNode *body);
ASTNode *ast_new_print(int line, ASTNode *value);
ASTNode *ast_new_binop(int line, const char *op, ASTNode *left, ASTNode *right);
ASTNode *ast_new_unaryop(int line, const char *op, ASTNode *operand);
ASTNode *ast_new_int_lit(int line, int value);
ASTNode *ast_new_float_lit(int line, double value);
ASTNode *ast_new_bool_lit(int line, int value);
ASTNode *ast_new_ident(int line, char *name);

/* --- Bonus features --- */
ASTNode *ast_new_func_decl(int line, char *name, DataType return_type, ParamData *params, int param_count, ASTNode *body);
ASTNode *ast_new_call(int line, char *callee, ASTNode **args, int arg_count);
ASTNode *ast_new_call_stmt(int line, char *callee, ASTNode **args, int arg_count);
ASTNode *ast_new_return(int line, ASTNode *value);
ASTNode *ast_new_for(int line, ASTNode *init, ASTNode *cond, ASTNode *update, ASTNode *body);
ASTNode *ast_new_do_while(int line, ASTNode *body, ASTNode *cond);
ASTNode *ast_new_incdec(int line, char *name, const char *op);

/* --- Advanced/unique extensions (beyond the manual's bonus list) --- */
ASTNode *ast_new_string_lit(int line, char *value);
ASTNode *ast_new_array_decl(int line, DataType elem_type, char *name, int size);
ASTNode *ast_new_array_assign(int line, char *name, ASTNode *index, ASTNode *value);
ASTNode *ast_new_array_access(int line, char *name, ASTNode *index);
ASTNode *ast_new_switch(int line, ASTNode *subject);
void     ast_switch_add_case(ASTNode *sw, ASTNode *value /* NULL for default */, ASTNode *body);
ASTNode *ast_new_break(int line);
ASTNode *ast_new_read(int line, ASTNode *target);

/* Text-based indented AST dump (project manual Section 4.3: "You must be
 * able to print or visualize the AST in some readable form"). */
void ast_print(const ASTNode *node, int indent);

/* Graphviz .dot export (bonus feature, manual Section 14) — see
 * docs/bonus_features.md. Writes a renderable `dot -Tpng ast.dot -o ast.png`
 * source file. */
void ast_write_dot(const ASTNode *program, FILE *out);

void ast_free(ASTNode *node);

#endif /* MINILANG_AST_H */
