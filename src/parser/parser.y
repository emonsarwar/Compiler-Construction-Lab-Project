/*
 * Bison grammar for the mini language (project manual Section 5).
 *
 * Design note on the expression grammar: precedence is expressed by
 * LAYERING (one nonterminal per precedence level, each left-recursive
 * over the next-tighter level) rather than via Bison's %left/%right
 * precedence declarations. Both are standard techniques; this project
 * uses layering because it is unambiguous *by construction* — there is
 * no possibility of a shift/reduce conflict from operator precedence,
 * since each level is its own grammar rule. See docs/grammar.md for
 * the full formal CFG this file implements, and docs/design.md for
 * the precedence table in one place.
 *
 * Levels, loosest to tightest (mirrors C's own precedence ordering):
 *   expr -> logical_or -> logical_and -> equality -> relational
 *        -> additive -> multiplicative -> unary -> primary
 */

%code requires {
    #include "ast.h"
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "error.h"

extern int yylineno;
extern int yylex(void);
extern FILE *yyin;
void yyerror(const char *msg);

/* The fully parsed program. NULL if parsing failed. main.c reads this
 * after a successful yyparse() call. */
ASTNode *g_program = NULL;
%}

%union {
    int ival;
    double fval;
    char *sval;
    struct ASTNode *node;
    ParamList *paramlist;   /* bonus: function parameter list — see ast.h */
    ArgList *arglist;       /* bonus: call argument list — see ast.h */
    DataType dtype;
    CaseList *caselist;     /* advanced feature: switch-case arms — see ast.h */
}

%token <ival> INT_LIT
%token <fval> FLOAT_LIT
%token <sval> IDENT
%token <sval> STRING_LIT

%token KW_INT KW_FLOAT KW_BOOL
%token KW_IF KW_ELSE KW_WHILE KW_PRINT
%token KW_TRUE KW_FALSE
%token KW_FUNCTION KW_RETURN KW_FOR KW_DO
%token KW_STRING KW_SWITCH KW_CASE KW_DEFAULT KW_BREAK KW_READ

%token PLUS MINUS STAR SLASH PERCENT
%token LT GT LE GE EQ NEQ
%token AND OR NOT
%token INC DEC
%token ASSIGN SEMI COMMA LBRACE RBRACE LPAREN RPAREN
%token LBRACKET RBRACKET COLON

%type <node> program program_items top_level_item
%type <node> stmt_list stmt declaration assignment call_stmt if_stmt while_stmt print_stmt block
%type <node> function_decl return_stmt for_stmt do_while_stmt incdec_stmt for_clause
%type <node> expr logical_or_expr logical_and_expr equality_expr relational_expr
%type <node> additive_expr multiplicative_expr unary_expr primary_expr
%type <dtype> type
%type <paramlist> param_list_opt param_list
%type <arglist> arg_list_opt arg_list
%type <node> array_decl array_assign switch_stmt read_stmt
%type <caselist> switch_case_list

%define parse.error verbose

%start program

%%

program:
      program_items {
          $1->kind = AST_PROGRAM;
          g_program = $1;
          $$ = $1;
      }
    ;

/* Bonus feature (functions): the top level additionally allows
 * function_decl, interspersed in any order with ordinary statements —
 * matching how the manual's original sample program already treats
 * the top level as one big implicit "main" sequence. Function BODIES
 * use plain `block` (via `stmt`), which does NOT include
 * function_decl — this is what structurally forbids nested function
 * definitions, with no extra semantic check needed. */
program_items:
      /* empty */ {
          $$ = ast_new_stmt_list(AST_BLOCK, yylineno);
      }
    | program_items top_level_item {
          if ($2) ast_stmt_list_add($1, $2);
          $$ = $1;
      }
    ;

top_level_item:
      stmt
    | function_decl
    ;

/* Also used for block bodies (see `block` below); a bare stmt_list is
 * built tagged AST_BLOCK, and `program` relabels the top-level one to
 * AST_PROGRAM once parsing completes. */
stmt_list:
      /* empty */ {
          $$ = ast_new_stmt_list(AST_BLOCK, yylineno);
      }
    | stmt_list stmt {
          if ($2) ast_stmt_list_add($1, $2);  /* $2 is NULL only from error-recovery — see `stmt` below */
          $$ = $1;
      }
    ;

stmt:
      declaration
    | assignment
    | call_stmt
    | if_stmt
    | while_stmt
    | for_stmt
    | do_while_stmt
    | print_stmt
    | return_stmt
    | incdec_stmt
    | block
    /* --- Advanced/unique extensions --- */
    | array_decl
    | array_assign
    | switch_stmt
    | read_stmt
    | error SEMI {
          /* Basic error recovery (manual Section 4.2): resynchronize at
           * the next ';' so one bad statement doesn't abort the whole
           * parse. yyerrok tells Bison to resume normal error
           * reporting for subsequent, independent errors. */
          yyerrok;
          $$ = NULL;
      }
    ;

type:
      KW_INT   { $$ = TYPE_INT; }
    | KW_FLOAT { $$ = TYPE_FLOAT; }
    | KW_BOOL  { $$ = TYPE_BOOL; }
    | KW_STRING { $$ = TYPE_STRING; }
    ;

declaration:
      type IDENT SEMI { $$ = ast_new_var_decl(yylineno, $1, $2); }
    ;

assignment:
      IDENT ASSIGN expr SEMI { $$ = ast_new_assign(yylineno, $1, $3); }
    ;

/* --- Advanced/unique extensions (beyond the manual's bonus list) --- */

/* Fixed-size array declaration, e.g. `int arr[10];`. Distinguished from
 * a plain `declaration` by the token right after IDENT (SEMI vs
 * LBRACKET) — one-token LALR(1) lookahead, same technique used
 * throughout this grammar (see call_stmt's comment). Size must be a
 * literal int (not an arbitrary expression) — deliberately simple,
 * since this language has no heap/dynamic allocation concept at all. */
array_decl:
      type IDENT LBRACKET INT_LIT RBRACKET SEMI {
          $$ = ast_new_array_decl(yylineno, $1, $2, $4);
      }
    ;

array_assign:
      IDENT LBRACKET expr RBRACKET ASSIGN expr SEMI {
          $$ = ast_new_array_assign(yylineno, $1, $3, $6);
      }
    ;

/* `read x;` (advanced feature): reads one value from stdin into an
 * already-declared variable at runtime — see interp/ for the actual
 * execution and tac.c for the `read` TAC instruction this lowers to. */
read_stmt:
      KW_READ IDENT SEMI {
          $$ = ast_new_read(yylineno, ast_new_ident(yylineno, $2));
      }
    | KW_READ IDENT LBRACKET expr RBRACKET SEMI {
          $$ = ast_new_read(yylineno, ast_new_array_access(yylineno, $2, $4));
      }
    ;

/* switch/case (advanced feature): case labels are restricted to int
 * literals (kept simple — no ranges, no arbitrary constant
 * expressions), each arm ends with a mandatory `break;` except
 * `default:`, which — being last by convention here — doesn't need
 * one. Semantic analysis (not the grammar) rejects duplicate case
 * values and requires the switch subject to be type int. */
switch_stmt:
      KW_SWITCH LPAREN expr RPAREN LBRACE switch_case_list RBRACE {
          ASTNode *sw = ast_new_switch(yylineno, $3);
          for (int i = 0; i < $6->count; i++) {
              ast_switch_add_case(sw, $6->items[i].value, $6->items[i].body);
          }
          ast_case_list_free($6);
          $$ = sw;
      }
    ;

switch_case_list:
      /* empty */ { $$ = ast_case_list_new(); }
    | switch_case_list KW_CASE INT_LIT COLON stmt_list KW_BREAK SEMI {
          ast_case_list_add($1, ast_new_int_lit(yylineno, $3), $5);
          $$ = $1;
      }
    | switch_case_list KW_DEFAULT COLON stmt_list {
          ast_case_list_add($1, NULL, $4);
          $$ = $1;
      }
    ;

/* Bonus feature (functions): a call used purely for its side effect,
 * with any return value discarded. Distinguished from `assignment`
 * by the token immediately after IDENT (ASSIGN vs LPAREN) — a plain
 * one-token-lookahead LALR(1) decision with no ambiguity, since
 * neither continuation is a prefix of the other. */
call_stmt:
      IDENT LPAREN arg_list_opt RPAREN SEMI {
          $$ = ast_new_call_stmt(yylineno, $1, $3->args, $3->count);
          ast_arg_list_free($3);
      }
    ;

if_stmt:
      KW_IF LPAREN expr RPAREN block {
          $$ = ast_new_if(yylineno, $3, $5, NULL);
      }
    | KW_IF LPAREN expr RPAREN block KW_ELSE block {
          $$ = ast_new_if(yylineno, $3, $5, $7);
      }
    ;

while_stmt:
      KW_WHILE LPAREN expr RPAREN block { $$ = ast_new_while(yylineno, $3, $5); }
    ;

print_stmt:
      KW_PRINT expr SEMI { $$ = ast_new_print(yylineno, $2); }
    ;

block:
      LBRACE stmt_list RBRACE { $$ = $2; }
    ;

/* --- Bonus features (manual Section 14) — see docs/bonus_features.md --- */

/* Prefixed with the `function` keyword — this makes function_decl
 * start with a token (KW_FUNCTION) that no other top-level rule can
 * start with, so there is no lookahead-based disambiguation needed
 * against `declaration` at all (unlike, say, call_stmt vs assignment,
 * which genuinely do share a starting token and rely on 1-token
 * lookahead — see call_stmt's comment). Return type still comes right
 * after, matching this language's existing type-first declaration
 * style (`int x;`) rather than a colon-based syntax. No nested
 * function definitions: `block` (used for the body) only ever expands
 * through `stmt`, which does not include function_decl — see
 * `top_level_item` above. */
function_decl:
      KW_FUNCTION type IDENT LPAREN param_list_opt RPAREN block {
          $$ = ast_new_func_decl(yylineno, $3, $2, $5->params, $5->count, $7);
          ast_param_list_free($5);
      }
    ;

param_list_opt:
      /* empty */ { $$ = ast_param_list_new(); }
    | param_list
    ;

param_list:
      type IDENT {
          $$ = ast_param_list_new();
          ast_param_list_add($$, $2, $1);
      }
    | param_list COMMA type IDENT {
          ast_param_list_add($1, $4, $3);
          $$ = $1;
      }
    ;

/* Every function in this language returns a value on every path (see
 * semantic.h) — there is deliberately no bare `return;` form, which
 * avoids needing a `void` function concept at all. */
return_stmt:
      KW_RETURN expr SEMI { $$ = ast_new_return(yylineno, $2); }
    ;

/* All three clauses are mandatory (unlike C, where every clause is
 * optional) — a deliberate simplification for this bonus feature: it
 * avoids needing a NULL-condition-means-true special case anywhere
 * downstream, in exchange for disallowing `for (;;)`. init/update
 * reuse the SAME `for_clause` nonterminal (assignment or
 * increment/decrement), matching what's actually useful in a loop
 * header. init/update deliberately do NOT reuse `declaration` or
 * `assignment` directly — those already include their own trailing
 * SEMI, which would conflict with the for-loop's own explicit
 * SEMI separators. */
for_stmt:
      KW_FOR LPAREN for_clause SEMI expr SEMI for_clause RPAREN block {
          $$ = ast_new_for(yylineno, $3, $5, $7, $9);
      }
    ;

for_clause:
      IDENT ASSIGN expr { $$ = ast_new_assign(yylineno, $1, $3); }
    | IDENT INC          { $$ = ast_new_incdec(yylineno, $1, "++"); }
    | IDENT DEC          { $$ = ast_new_incdec(yylineno, $1, "--"); }
    ;

do_while_stmt:
      KW_DO block KW_WHILE LPAREN expr RPAREN SEMI {
          $$ = ast_new_do_while(yylineno, $2, $5);
      }
    ;

incdec_stmt:
      IDENT INC SEMI { $$ = ast_new_incdec(yylineno, $1, "++"); }
    | IDENT DEC SEMI { $$ = ast_new_incdec(yylineno, $1, "--"); }
    ;

arg_list_opt:
      /* empty */ { $$ = ast_arg_list_new(); }
    | arg_list
    ;

arg_list:
      expr {
          $$ = ast_arg_list_new();
          ast_arg_list_add($$, $1);
      }
    | arg_list COMMA expr {
          ast_arg_list_add($1, $3);
          $$ = $1;
      }
    ;

expr:
      logical_or_expr { $$ = $1; }
    ;

logical_or_expr:
      logical_and_expr
    | logical_or_expr OR logical_and_expr { $$ = ast_new_binop(yylineno, "||", $1, $3); }
    ;

logical_and_expr:
      equality_expr
    | logical_and_expr AND equality_expr { $$ = ast_new_binop(yylineno, "&&", $1, $3); }
    ;

equality_expr:
      relational_expr
    | equality_expr EQ relational_expr  { $$ = ast_new_binop(yylineno, "==", $1, $3); }
    | equality_expr NEQ relational_expr { $$ = ast_new_binop(yylineno, "!=", $1, $3); }
    ;

relational_expr:
      additive_expr
    | relational_expr LT additive_expr  { $$ = ast_new_binop(yylineno, "<", $1, $3); }
    | relational_expr GT additive_expr  { $$ = ast_new_binop(yylineno, ">", $1, $3); }
    | relational_expr LE additive_expr  { $$ = ast_new_binop(yylineno, "<=", $1, $3); }
    | relational_expr GE additive_expr  { $$ = ast_new_binop(yylineno, ">=", $1, $3); }
    ;

additive_expr:
      multiplicative_expr
    | additive_expr PLUS multiplicative_expr  { $$ = ast_new_binop(yylineno, "+", $1, $3); }
    | additive_expr MINUS multiplicative_expr { $$ = ast_new_binop(yylineno, "-", $1, $3); }
    ;

multiplicative_expr:
      unary_expr
    | multiplicative_expr STAR unary_expr    { $$ = ast_new_binop(yylineno, "*", $1, $3); }
    | multiplicative_expr SLASH unary_expr   { $$ = ast_new_binop(yylineno, "/", $1, $3); }
    | multiplicative_expr PERCENT unary_expr { $$ = ast_new_binop(yylineno, "%", $1, $3); }
    ;

unary_expr:
      primary_expr
    | MINUS unary_expr { $$ = ast_new_unaryop(yylineno, "-", $2); }
    | NOT unary_expr   { $$ = ast_new_unaryop(yylineno, "!", $2); }
    ;

primary_expr:
      INT_LIT           { $$ = ast_new_int_lit(yylineno, $1); }
    | FLOAT_LIT          { $$ = ast_new_float_lit(yylineno, $1); }
    | KW_TRUE             { $$ = ast_new_bool_lit(yylineno, 1); }
    | KW_FALSE             { $$ = ast_new_bool_lit(yylineno, 0); }
    | STRING_LIT             { $$ = ast_new_string_lit(yylineno, $1); }
    | IDENT                 { $$ = ast_new_ident(yylineno, $1); }
    | IDENT LBRACKET expr RBRACKET {
          /* Array element read (advanced feature). Distinguished from
           * the bare IDENT alternative above by the token after IDENT
           * (LBRACKET vs anything else) — same one-token LALR(1)
           * lookahead technique as the IDENT LPAREN (call) case below. */
          $$ = ast_new_array_access(yylineno, $1, $3);
      }
    | IDENT LPAREN arg_list_opt RPAREN {
          /* Function call used as a value. Distinguished from the bare
           * IDENT alternative above by the token after IDENT (LPAREN
           * vs anything else) — standard, conflict-free LALR(1)
           * shift/reduce resolution: LPAREN never follows a bare
           * primary_expr anywhere else in this grammar, so there's no
           * genuine ambiguity to resolve, just a lookahead-driven
           * choice between two non-overlapping continuations. */
          $$ = ast_new_call(yylineno, $1, $3->args, $3->count);
          ast_arg_list_free($3);
      }
    | LPAREN expr RPAREN     { $$ = $2; }
    ;

%%

void yyerror(const char *msg) {
    report_error("Syntax", yylineno, "%s", msg);
}
