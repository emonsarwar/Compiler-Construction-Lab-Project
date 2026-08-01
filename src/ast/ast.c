#define _POSIX_C_SOURCE 200809L  /* for strdup — kept explicit so this compiles under -std=c99 too */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

const char *datatype_to_string(DataType t) {
    switch (t) {
        case TYPE_INT:     return "int";
        case TYPE_FLOAT:   return "float";
        case TYPE_BOOL:    return "bool";
        case TYPE_STRING:  return "string";
        case TYPE_VOID:    return "void";
        case TYPE_UNKNOWN: return "unknown";
    }
    return "unknown";
}

/* Every constructor starts from this: allocate, tag, stamp the line,
 * and leave `type` as TYPE_UNKNOWN for the semantic analyzer to fill in. */
static ASTNode *ast_alloc(ASTNodeKind kind, int line) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "fatal: out of memory while building AST\n");
        exit(1);
    }
    node->kind = kind;
    node->line = line;
    node->type = TYPE_UNKNOWN;
    return node;
}

ASTNode *ast_new_stmt_list(ASTNodeKind kind, int line) {
    ASTNode *node = ast_alloc(kind, line);
    node->as.stmt_list.count = 0;
    node->as.stmt_list.capacity = 4;
    node->as.stmt_list.stmts = (ASTNode **)malloc(sizeof(ASTNode *) * (size_t)node->as.stmt_list.capacity);
    return node;
}

void ast_stmt_list_add(ASTNode *list, ASTNode *stmt) {
    StmtList *sl = &list->as.stmt_list;
    if (sl->count == sl->capacity) {
        sl->capacity *= 2;
        sl->stmts = (ASTNode **)realloc(sl->stmts, sizeof(ASTNode *) * (size_t)sl->capacity);
    }
    sl->stmts[sl->count++] = stmt;
}

ASTNode *ast_new_var_decl(int line, DataType type, char *name) {
    ASTNode *node = ast_alloc(AST_VAR_DECL, line);
    node->as.var_decl.var_type = type;
    node->as.var_decl.name = name;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_assign(int line, char *name, ASTNode *value) {
    ASTNode *node = ast_alloc(AST_ASSIGN, line);
    node->as.assign.name = name;
    node->as.assign.value = value;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_if(int line, ASTNode *cond, ASTNode *then_branch, ASTNode *else_branch) {
    ASTNode *node = ast_alloc(AST_IF, line);
    node->as.if_stmt.cond = cond;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_while(int line, ASTNode *cond, ASTNode *body) {
    ASTNode *node = ast_alloc(AST_WHILE, line);
    node->as.while_stmt.cond = cond;
    node->as.while_stmt.body = body;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_print(int line, ASTNode *value) {
    ASTNode *node = ast_alloc(AST_PRINT, line);
    node->as.print_stmt.value = value;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_binop(int line, const char *op, ASTNode *left, ASTNode *right) {
    ASTNode *node = ast_alloc(AST_BINOP, line);
    node->as.binop.op = strdup(op);
    node->as.binop.left = left;
    node->as.binop.right = right;
    return node;
}

ASTNode *ast_new_unaryop(int line, const char *op, ASTNode *operand) {
    ASTNode *node = ast_alloc(AST_UNARYOP, line);
    node->as.unaryop.op = strdup(op);
    node->as.unaryop.operand = operand;
    return node;
}

ASTNode *ast_new_int_lit(int line, int value) {
    ASTNode *node = ast_alloc(AST_INT_LIT, line);
    node->as.int_lit.value = value;
    node->type = TYPE_INT;
    return node;
}

ASTNode *ast_new_float_lit(int line, double value) {
    ASTNode *node = ast_alloc(AST_FLOAT_LIT, line);
    node->as.float_lit.value = value;
    node->type = TYPE_FLOAT;
    return node;
}

ASTNode *ast_new_bool_lit(int line, int value) {
    ASTNode *node = ast_alloc(AST_BOOL_LIT, line);
    node->as.bool_lit.value = value;
    node->type = TYPE_BOOL;
    return node;
}

ASTNode *ast_new_ident(int line, char *name) {
    ASTNode *node = ast_alloc(AST_IDENT, line);
    node->as.ident.name = name;
    return node;
}

/* --- Bonus features --- */

ASTNode *ast_new_func_decl(int line, char *name, DataType return_type, ParamData *params, int param_count, ASTNode *body) {
    ASTNode *node = ast_alloc(AST_FUNC_DECL, line);
    node->as.func_decl.name = name;
    node->as.func_decl.return_type = return_type;
    node->as.func_decl.params = params;
    node->as.func_decl.param_count = param_count;
    node->as.func_decl.body = body;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_call(int line, char *callee, ASTNode **args, int arg_count) {
    ASTNode *node = ast_alloc(AST_CALL, line);
    node->as.call.callee = callee;
    node->as.call.args = args;
    node->as.call.arg_count = arg_count;
    return node;
}

ASTNode *ast_new_call_stmt(int line, char *callee, ASTNode **args, int arg_count) {
    ASTNode *node = ast_alloc(AST_CALL_STMT, line);
    node->as.call.callee = callee;
    node->as.call.args = args;
    node->as.call.arg_count = arg_count;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_return(int line, ASTNode *value) {
    ASTNode *node = ast_alloc(AST_RETURN, line);
    node->as.ret.value = value;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_for(int line, ASTNode *init, ASTNode *cond, ASTNode *update, ASTNode *body) {
    ASTNode *node = ast_alloc(AST_FOR, line);
    node->as.for_stmt.init = init;
    node->as.for_stmt.cond = cond;
    node->as.for_stmt.update = update;
    node->as.for_stmt.body = body;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_do_while(int line, ASTNode *body, ASTNode *cond) {
    ASTNode *node = ast_alloc(AST_DO_WHILE, line);
    node->as.do_while.body = body;
    node->as.do_while.cond = cond;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_incdec(int line, char *name, const char *op) {
    ASTNode *node = ast_alloc(AST_INCDEC, line);
    node->as.incdec.name = name;
    node->as.incdec.op = strdup(op);
    node->type = TYPE_VOID;
    return node;
}

/* --- Advanced/unique extensions (beyond the manual's bonus list) --- */

ASTNode *ast_new_string_lit(int line, char *value) {
    ASTNode *node = ast_alloc(AST_STRING_LIT, line);
    node->as.string_lit.value = value;
    node->type = TYPE_STRING;
    return node;
}

ASTNode *ast_new_array_decl(int line, DataType elem_type, char *name, int size) {
    ASTNode *node = ast_alloc(AST_ARRAY_DECL, line);
    node->as.array_decl.elem_type = elem_type;
    node->as.array_decl.name = name;
    node->as.array_decl.size = size;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_array_assign(int line, char *name, ASTNode *index, ASTNode *value) {
    ASTNode *node = ast_alloc(AST_ARRAY_ASSIGN, line);
    node->as.array_assign.name = name;
    node->as.array_assign.index = index;
    node->as.array_assign.value = value;
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_array_access(int line, char *name, ASTNode *index) {
    ASTNode *node = ast_alloc(AST_ARRAY_ACCESS, line);
    node->as.array_access.name = name;
    node->as.array_access.index = index;
    return node;
}

ASTNode *ast_new_switch(int line, ASTNode *subject) {
    ASTNode *node = ast_alloc(AST_SWITCH, line);
    node->as.switch_stmt.subject = subject;
    node->as.switch_stmt.case_count = 0;
    node->as.switch_stmt.case_capacity = 4;
    node->as.switch_stmt.cases = (SwitchCase *)malloc(sizeof(SwitchCase) * (size_t)node->as.switch_stmt.case_capacity);
    node->type = TYPE_VOID;
    return node;
}

void ast_switch_add_case(ASTNode *sw, ASTNode *value, ASTNode *body) {
    SwitchData *sd = &sw->as.switch_stmt;
    if (sd->case_count == sd->case_capacity) {
        sd->case_capacity *= 2;
        sd->cases = (SwitchCase *)realloc(sd->cases, sizeof(SwitchCase) * (size_t)sd->case_capacity);
    }
    sd->cases[sd->case_count].is_default = (value == NULL);
    sd->cases[sd->case_count].value = value;
    sd->cases[sd->case_count].body = body;
    sd->case_count++;
}

ASTNode *ast_new_break(int line) {
    ASTNode *node = ast_alloc(AST_BREAK, line);
    node->type = TYPE_VOID;
    return node;
}

ASTNode *ast_new_read(int line, ASTNode *target) {
    ASTNode *node = ast_alloc(AST_READ, line);
    node->as.read_stmt.target = target;
    node->type = TYPE_VOID;
    return node;
}

ParamList *ast_param_list_new(void) {
    ParamList *list = (ParamList *)malloc(sizeof(ParamList));
    list->count = 0;
    list->capacity = 4;
    list->params = (ParamData *)malloc(sizeof(ParamData) * (size_t)list->capacity);
    return list;
}

void ast_param_list_add(ParamList *list, char *name, DataType type) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->params = (ParamData *)realloc(list->params, sizeof(ParamData) * (size_t)list->capacity);
    }
    list->params[list->count].name = name;
    list->params[list->count].type = type;
    list->count++;
}

void ast_param_list_free(ParamList *list) {
    free(list); /* NOT list->params — ownership already transferred to the FuncDeclData that consumed it */
}

ArgList *ast_arg_list_new(void) {
    ArgList *list = (ArgList *)malloc(sizeof(ArgList));
    list->count = 0;
    list->capacity = 4;
    list->args = (ASTNode **)malloc(sizeof(ASTNode *) * (size_t)list->capacity);
    return list;
}

void ast_arg_list_add(ArgList *list, ASTNode *arg) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->args = (ASTNode **)realloc(list->args, sizeof(ASTNode *) * (size_t)list->capacity);
    }
    list->args[list->count++] = arg;
}

void ast_arg_list_free(ArgList *list) {
    free(list); /* NOT list->args — ownership already transferred to the CallData that consumed it */
}

CaseList *ast_case_list_new(void) {
    CaseList *list = (CaseList *)malloc(sizeof(CaseList));
    list->count = 0;
    list->capacity = 4;
    list->items = (CaseItem *)malloc(sizeof(CaseItem) * (size_t)list->capacity);
    return list;
}

void ast_case_list_add(CaseList *list, ASTNode *value, ASTNode *body) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->items = (CaseItem *)realloc(list->items, sizeof(CaseItem) * (size_t)list->capacity);
    }
    list->items[list->count].value = value;
    list->items[list->count].body = body;
    list->count++;
}

void ast_case_list_free(CaseList *list) {
    free(list->items);
    free(list);
}

/* --- Printing --- */

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

void ast_print(const ASTNode *node, int indent) {
    if (!node) { print_indent(indent); printf("(null)\n"); return; }

    switch (node->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            print_indent(indent);
            printf("%s\n", node->kind == AST_PROGRAM ? "Program" : "Block");
            for (int i = 0; i < node->as.stmt_list.count; i++) {
                ast_print(node->as.stmt_list.stmts[i], indent + 1);
            }
            break;

        case AST_VAR_DECL:
            print_indent(indent);
            printf("VarDecl <%s> %s  (line %d)\n",
                   datatype_to_string(node->as.var_decl.var_type),
                   node->as.var_decl.name, node->line);
            break;

        case AST_ASSIGN:
            print_indent(indent);
            printf("Assign %s =  (line %d)\n", node->as.assign.name, node->line);
            ast_print(node->as.assign.value, indent + 1);
            break;

        case AST_IF:
            print_indent(indent);
            printf("If  (line %d)\n", node->line);
            print_indent(indent + 1); printf("Cond:\n");
            ast_print(node->as.if_stmt.cond, indent + 2);
            print_indent(indent + 1); printf("Then:\n");
            ast_print(node->as.if_stmt.then_branch, indent + 2);
            if (node->as.if_stmt.else_branch) {
                print_indent(indent + 1); printf("Else:\n");
                ast_print(node->as.if_stmt.else_branch, indent + 2);
            }
            break;

        case AST_WHILE:
            print_indent(indent);
            printf("While  (line %d)\n", node->line);
            print_indent(indent + 1); printf("Cond:\n");
            ast_print(node->as.while_stmt.cond, indent + 2);
            print_indent(indent + 1); printf("Body:\n");
            ast_print(node->as.while_stmt.body, indent + 2);
            break;

        case AST_PRINT:
            print_indent(indent);
            printf("Print  (line %d)\n", node->line);
            ast_print(node->as.print_stmt.value, indent + 1);
            break;

        case AST_BINOP:
            print_indent(indent);
            printf("BinOp '%s'  : %s  (line %d)\n",
                   node->as.binop.op, datatype_to_string(node->type), node->line);
            ast_print(node->as.binop.left, indent + 1);
            ast_print(node->as.binop.right, indent + 1);
            break;

        case AST_UNARYOP:
            print_indent(indent);
            printf("UnaryOp '%s'  : %s  (line %d)\n",
                   node->as.unaryop.op, datatype_to_string(node->type), node->line);
            ast_print(node->as.unaryop.operand, indent + 1);
            break;

        case AST_INT_LIT:
            print_indent(indent);
            printf("IntLit %d\n", node->as.int_lit.value);
            break;

        case AST_FLOAT_LIT:
            print_indent(indent);
            printf("FloatLit %g\n", node->as.float_lit.value);
            break;

        case AST_BOOL_LIT:
            print_indent(indent);
            printf("BoolLit %s\n", node->as.bool_lit.value ? "true" : "false");
            break;

        case AST_IDENT:
            print_indent(indent);
            printf("Ident %s  : %s  (line %d)\n",
                   node->as.ident.name, datatype_to_string(node->type), node->line);
            break;

        case AST_FUNC_DECL: {
            print_indent(indent);
            printf("FuncDecl %s(", node->as.func_decl.name);
            for (int i = 0; i < node->as.func_decl.param_count; i++) {
                if (i > 0) printf(", ");
                printf("%s: %s", node->as.func_decl.params[i].name, datatype_to_string(node->as.func_decl.params[i].type));
            }
            printf("): %s  (line %d)\n", datatype_to_string(node->as.func_decl.return_type), node->line);
            ast_print(node->as.func_decl.body, indent + 1);
            break;
        }

        case AST_CALL:
        case AST_CALL_STMT:
            print_indent(indent);
            printf("%s %s(...)  : %s  (line %d)\n",
                   node->kind == AST_CALL ? "Call" : "CallStmt",
                   node->as.call.callee, datatype_to_string(node->type), node->line);
            for (int i = 0; i < node->as.call.arg_count; i++) {
                ast_print(node->as.call.args[i], indent + 1);
            }
            break;

        case AST_RETURN:
            print_indent(indent);
            printf("Return  (line %d)\n", node->line);
            ast_print(node->as.ret.value, indent + 1);
            break;

        case AST_FOR:
            print_indent(indent);
            printf("For  (line %d)\n", node->line);
            if (node->as.for_stmt.init) { print_indent(indent + 1); printf("Init:\n"); ast_print(node->as.for_stmt.init, indent + 2); }
            if (node->as.for_stmt.cond) { print_indent(indent + 1); printf("Cond:\n"); ast_print(node->as.for_stmt.cond, indent + 2); }
            if (node->as.for_stmt.update) { print_indent(indent + 1); printf("Update:\n"); ast_print(node->as.for_stmt.update, indent + 2); }
            print_indent(indent + 1); printf("Body:\n");
            ast_print(node->as.for_stmt.body, indent + 2);
            break;

        case AST_DO_WHILE:
            print_indent(indent);
            printf("DoWhile  (line %d)\n", node->line);
            print_indent(indent + 1); printf("Body:\n");
            ast_print(node->as.do_while.body, indent + 2);
            print_indent(indent + 1); printf("Cond:\n");
            ast_print(node->as.do_while.cond, indent + 2);
            break;

        case AST_INCDEC:
            print_indent(indent);
            printf("IncDec %s%s  (line %d)\n", node->as.incdec.name, node->as.incdec.op, node->line);
            break;

        /* --- Advanced/unique extensions --- */

        case AST_STRING_LIT:
            print_indent(indent);
            printf("StringLit \"%s\"\n", node->as.string_lit.value);
            break;

        case AST_ARRAY_DECL:
            print_indent(indent);
            printf("ArrayDecl <%s[%d]> %s  (line %d)\n",
                   datatype_to_string(node->as.array_decl.elem_type),
                   node->as.array_decl.size, node->as.array_decl.name, node->line);
            break;

        case AST_ARRAY_ASSIGN:
            print_indent(indent);
            printf("ArrayAssign %s[...] =  (line %d)\n", node->as.array_assign.name, node->line);
            print_indent(indent + 1); printf("Index:\n");
            ast_print(node->as.array_assign.index, indent + 2);
            print_indent(indent + 1); printf("Value:\n");
            ast_print(node->as.array_assign.value, indent + 2);
            break;

        case AST_ARRAY_ACCESS:
            print_indent(indent);
            printf("ArrayAccess %s[...]  : %s  (line %d)\n",
                   node->as.array_access.name, datatype_to_string(node->type), node->line);
            ast_print(node->as.array_access.index, indent + 1);
            break;

        case AST_SWITCH:
            print_indent(indent);
            printf("Switch  (line %d)\n", node->line);
            print_indent(indent + 1); printf("Subject:\n");
            ast_print(node->as.switch_stmt.subject, indent + 2);
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                SwitchCase *c = &node->as.switch_stmt.cases[i];
                print_indent(indent + 1);
                if (c->is_default) printf("Default:\n");
                else printf("Case:\n");
                if (!c->is_default) ast_print(c->value, indent + 2);
                ast_print(c->body, indent + 2);
            }
            break;

        case AST_BREAK:
            print_indent(indent);
            printf("Break  (line %d)\n", node->line);
            break;

        case AST_READ:
            print_indent(indent);
            printf("Read  (line %d)\n", node->line);
            ast_print(node->as.read_stmt.target, indent + 1);
            break;
    }
}

/* --- Graphviz export (bonus feature) ---
 * Each AST node becomes one dot node, labeled with a short description;
 * each parent/child relationship becomes one edge. Render with:
 *   dot -Tpng ast.dot -o ast.png
 */

static void dot_escape(const char *s, char *out, size_t outsize) {
    size_t j = 0;
    for (size_t i = 0; s[i] != '\0' && j + 2 < outsize; i++) {
        if (s[i] == '"' || s[i] == '\\') out[j++] = '\\';
        out[j++] = s[i];
    }
    out[j] = '\0';
}

/* Writes this node (and its subtree) as dot nodes+edges, returns the dot-id assigned to `node`. */
static int dot_visit(const ASTNode *node, FILE *out, int *counter) {
    int id = (*counter)++;
    char label[256];
    char esc[192];

    if (!node) {
        fprintf(out, "  n%d [label=\"(null)\", shape=box, style=dashed];\n", id);
        return id;
    }

    switch (node->kind) {
        case AST_PROGRAM: snprintf(label, sizeof(label), "Program"); break;
        case AST_BLOCK:    snprintf(label, sizeof(label), "Block"); break;
        case AST_VAR_DECL: snprintf(label, sizeof(label), "VarDecl\\n%s %s", datatype_to_string(node->as.var_decl.var_type), node->as.var_decl.name); break;
        case AST_ASSIGN:   snprintf(label, sizeof(label), "Assign\\n%s =", node->as.assign.name); break;
        case AST_IF:       snprintf(label, sizeof(label), "If"); break;
        case AST_WHILE:    snprintf(label, sizeof(label), "While"); break;
        case AST_FOR:      snprintf(label, sizeof(label), "For"); break;
        case AST_DO_WHILE: snprintf(label, sizeof(label), "DoWhile"); break;
        case AST_PRINT:    snprintf(label, sizeof(label), "Print"); break;
        case AST_RETURN:   snprintf(label, sizeof(label), "Return"); break;
        case AST_BINOP:    dot_escape(node->as.binop.op, esc, sizeof(esc)); snprintf(label, sizeof(label), "BinOp\\n%s : %s", esc, datatype_to_string(node->type)); break;
        case AST_UNARYOP:  dot_escape(node->as.unaryop.op, esc, sizeof(esc)); snprintf(label, sizeof(label), "UnaryOp\\n%s : %s", esc, datatype_to_string(node->type)); break;
        case AST_INT_LIT:   snprintf(label, sizeof(label), "IntLit\\n%d", node->as.int_lit.value); break;
        case AST_FLOAT_LIT: snprintf(label, sizeof(label), "FloatLit\\n%g", node->as.float_lit.value); break;
        case AST_BOOL_LIT:  snprintf(label, sizeof(label), "BoolLit\\n%s", node->as.bool_lit.value ? "true" : "false"); break;
        case AST_IDENT:     snprintf(label, sizeof(label), "Ident\\n%s : %s", node->as.ident.name, datatype_to_string(node->type)); break;
        case AST_FUNC_DECL: snprintf(label, sizeof(label), "FuncDecl\\n%s(): %s", node->as.func_decl.name, datatype_to_string(node->as.func_decl.return_type)); break;
        case AST_CALL:      snprintf(label, sizeof(label), "Call\\n%s(): %s", node->as.call.callee, datatype_to_string(node->type)); break;
        case AST_CALL_STMT: snprintf(label, sizeof(label), "CallStmt\\n%s()", node->as.call.callee); break;
        case AST_INCDEC:    snprintf(label, sizeof(label), "IncDec\\n%s%s", node->as.incdec.name, node->as.incdec.op); break;
        case AST_STRING_LIT: dot_escape(node->as.string_lit.value, esc, sizeof(esc)); snprintf(label, sizeof(label), "StringLit\\n\\\"%s\\\"", esc); break;
        case AST_ARRAY_DECL: snprintf(label, sizeof(label), "ArrayDecl\\n%s %s[%d]", datatype_to_string(node->as.array_decl.elem_type), node->as.array_decl.name, node->as.array_decl.size); break;
        case AST_ARRAY_ASSIGN: snprintf(label, sizeof(label), "ArrayAssign\\n%s[...] =", node->as.array_assign.name); break;
        case AST_ARRAY_ACCESS: snprintf(label, sizeof(label), "ArrayAccess\\n%s[...] : %s", node->as.array_access.name, datatype_to_string(node->type)); break;
        case AST_SWITCH: snprintf(label, sizeof(label), "Switch"); break;
        case AST_BREAK: snprintf(label, sizeof(label), "Break"); break;
        case AST_READ: snprintf(label, sizeof(label), "Read"); break;
        default: snprintf(label, sizeof(label), "?"); break;
    }
    fprintf(out, "  n%d [label=\"%s\", shape=box];\n", id, label);

    /* Recurse into children, drawing an edge from this node to each. */
    switch (node->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->as.stmt_list.count; i++) {
                int c = dot_visit(node->as.stmt_list.stmts[i], out, counter);
                fprintf(out, "  n%d -> n%d;\n", id, c);
            }
            break;
        case AST_ASSIGN: { int c = dot_visit(node->as.assign.value, out, counter); fprintf(out, "  n%d -> n%d;\n", id, c); break; }
        case AST_IF: {
            int cc = dot_visit(node->as.if_stmt.cond, out, counter); fprintf(out, "  n%d -> n%d [label=\"cond\"];\n", id, cc);
            int tc = dot_visit(node->as.if_stmt.then_branch, out, counter); fprintf(out, "  n%d -> n%d [label=\"then\"];\n", id, tc);
            if (node->as.if_stmt.else_branch) { int ec = dot_visit(node->as.if_stmt.else_branch, out, counter); fprintf(out, "  n%d -> n%d [label=\"else\"];\n", id, ec); }
            break;
        }
        case AST_WHILE: {
            int cc = dot_visit(node->as.while_stmt.cond, out, counter); fprintf(out, "  n%d -> n%d [label=\"cond\"];\n", id, cc);
            int bc = dot_visit(node->as.while_stmt.body, out, counter); fprintf(out, "  n%d -> n%d [label=\"body\"];\n", id, bc);
            break;
        }
        case AST_FOR: {
            if (node->as.for_stmt.init) { int c = dot_visit(node->as.for_stmt.init, out, counter); fprintf(out, "  n%d -> n%d [label=\"init\"];\n", id, c); }
            if (node->as.for_stmt.cond) { int c = dot_visit(node->as.for_stmt.cond, out, counter); fprintf(out, "  n%d -> n%d [label=\"cond\"];\n", id, c); }
            if (node->as.for_stmt.update) { int c = dot_visit(node->as.for_stmt.update, out, counter); fprintf(out, "  n%d -> n%d [label=\"update\"];\n", id, c); }
            int bc = dot_visit(node->as.for_stmt.body, out, counter); fprintf(out, "  n%d -> n%d [label=\"body\"];\n", id, bc);
            break;
        }
        case AST_DO_WHILE: {
            int bc = dot_visit(node->as.do_while.body, out, counter); fprintf(out, "  n%d -> n%d [label=\"body\"];\n", id, bc);
            int cc = dot_visit(node->as.do_while.cond, out, counter); fprintf(out, "  n%d -> n%d [label=\"cond\"];\n", id, cc);
            break;
        }
        case AST_PRINT: { int c = dot_visit(node->as.print_stmt.value, out, counter); fprintf(out, "  n%d -> n%d;\n", id, c); break; }
        case AST_RETURN: { int c = dot_visit(node->as.ret.value, out, counter); fprintf(out, "  n%d -> n%d;\n", id, c); break; }
        case AST_BINOP: {
            int lc = dot_visit(node->as.binop.left, out, counter); fprintf(out, "  n%d -> n%d;\n", id, lc);
            int rc = dot_visit(node->as.binop.right, out, counter); fprintf(out, "  n%d -> n%d;\n", id, rc);
            break;
        }
        case AST_UNARYOP: { int c = dot_visit(node->as.unaryop.operand, out, counter); fprintf(out, "  n%d -> n%d;\n", id, c); break; }
        case AST_FUNC_DECL: { int c = dot_visit(node->as.func_decl.body, out, counter); fprintf(out, "  n%d -> n%d [label=\"body\"];\n", id, c); break; }
        case AST_CALL:
        case AST_CALL_STMT:
            for (int i = 0; i < node->as.call.arg_count; i++) {
                int c = dot_visit(node->as.call.args[i], out, counter);
                fprintf(out, "  n%d -> n%d [label=\"arg%d\"];\n", id, c, i);
            }
            break;
        case AST_ARRAY_ASSIGN: {
            int ic = dot_visit(node->as.array_assign.index, out, counter); fprintf(out, "  n%d -> n%d [label=\"index\"];\n", id, ic);
            int vc = dot_visit(node->as.array_assign.value, out, counter); fprintf(out, "  n%d -> n%d [label=\"value\"];\n", id, vc);
            break;
        }
        case AST_ARRAY_ACCESS: { int c = dot_visit(node->as.array_access.index, out, counter); fprintf(out, "  n%d -> n%d [label=\"index\"];\n", id, c); break; }
        case AST_SWITCH: {
            int sc = dot_visit(node->as.switch_stmt.subject, out, counter); fprintf(out, "  n%d -> n%d [label=\"subject\"];\n", id, sc);
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                int bc = dot_visit(node->as.switch_stmt.cases[i].body, out, counter);
                fprintf(out, "  n%d -> n%d [label=\"case%d\"];\n", id, bc, i);
            }
            break;
        }
        case AST_READ: { int c = dot_visit(node->as.read_stmt.target, out, counter); fprintf(out, "  n%d -> n%d;\n", id, c); break; }
        default:
            break; /* literals, identifiers, decls, incdec: no AST children to recurse into */
    }
    return id;
}

void ast_write_dot(const ASTNode *program, FILE *out) {
    int counter = 0;
    fprintf(out, "digraph AST {\n  node [fontname=\"monospace\"];\n");
    dot_visit(program, out, &counter);
    fprintf(out, "}\n");
}

/* --- Cleanup ---
 * Not load-bearing for a compiler that runs once and exits (the OS
 * reclaims everything), but included as good practice and so
 * valgrind/ASan runs during development are clean. */

void ast_free(ASTNode *node) {
    if (!node) return;
    switch (node->kind) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->as.stmt_list.count; i++) ast_free(node->as.stmt_list.stmts[i]);
            free(node->as.stmt_list.stmts);
            break;
        case AST_VAR_DECL:
            free(node->as.var_decl.name);
            break;
        case AST_ASSIGN:
            free(node->as.assign.name);
            ast_free(node->as.assign.value);
            break;
        case AST_IF:
            ast_free(node->as.if_stmt.cond);
            ast_free(node->as.if_stmt.then_branch);
            ast_free(node->as.if_stmt.else_branch);
            break;
        case AST_WHILE:
            ast_free(node->as.while_stmt.cond);
            ast_free(node->as.while_stmt.body);
            break;
        case AST_PRINT:
            ast_free(node->as.print_stmt.value);
            break;
        case AST_BINOP:
            free(node->as.binop.op);
            ast_free(node->as.binop.left);
            ast_free(node->as.binop.right);
            break;
        case AST_UNARYOP:
            free(node->as.unaryop.op);
            ast_free(node->as.unaryop.operand);
            break;
        case AST_IDENT:
            free(node->as.ident.name);
            break;
        case AST_INT_LIT:
        case AST_FLOAT_LIT:
        case AST_BOOL_LIT:
            break;
        case AST_FUNC_DECL:
            free(node->as.func_decl.name);
            for (int i = 0; i < node->as.func_decl.param_count; i++) free(node->as.func_decl.params[i].name);
            free(node->as.func_decl.params);
            ast_free(node->as.func_decl.body);
            break;
        case AST_CALL:
        case AST_CALL_STMT:
            free(node->as.call.callee);
            for (int i = 0; i < node->as.call.arg_count; i++) ast_free(node->as.call.args[i]);
            free(node->as.call.args);
            break;
        case AST_RETURN:
            ast_free(node->as.ret.value);
            break;
        case AST_FOR:
            ast_free(node->as.for_stmt.init);
            ast_free(node->as.for_stmt.cond);
            ast_free(node->as.for_stmt.update);
            ast_free(node->as.for_stmt.body);
            break;
        case AST_DO_WHILE:
            ast_free(node->as.do_while.body);
            ast_free(node->as.do_while.cond);
            break;
        case AST_INCDEC:
            free(node->as.incdec.name);
            free(node->as.incdec.op);
            break;

        /* --- Advanced/unique extensions --- */
        case AST_STRING_LIT:
            free(node->as.string_lit.value);
            break;
        case AST_ARRAY_DECL:
            free(node->as.array_decl.name);
            break;
        case AST_ARRAY_ASSIGN:
            free(node->as.array_assign.name);
            ast_free(node->as.array_assign.index);
            ast_free(node->as.array_assign.value);
            break;
        case AST_ARRAY_ACCESS:
            free(node->as.array_access.name);
            ast_free(node->as.array_access.index);
            break;
        case AST_SWITCH:
            ast_free(node->as.switch_stmt.subject);
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                ast_free(node->as.switch_stmt.cases[i].value);
                ast_free(node->as.switch_stmt.cases[i].body);
            }
            free(node->as.switch_stmt.cases);
            break;
        case AST_BREAK:
            break;
        case AST_READ:
            ast_free(node->as.read_stmt.target);
            break;
    }
    free(node);
}
