#ifndef MINILANG_SYMBOL_TABLE_H
#define MINILANG_SYMBOL_TABLE_H

#include "../ast/ast.h"

/*
 * Symbol table with nested (block) scopes (project manual Section 4.4).
 *
 * Design: a linked list of Symbols per Scope, and each Scope points to
 * its enclosing (parent) Scope. `symtab_lookup` walks from the current
 * scope outward through parents — this single mechanism is what gives
 * us both "find a declared variable" AND, for free, correct enforcement
 * of the "scope violation" rule (Section 4.5): once a block's Scope is
 * exited (symtab_exit_scope), its symbols are no longer reachable from
 * any lookup that starts outside that block, so a later reference to a
 * name that only existed inside the closed block fails lookup exactly
 * like an undeclared variable — which is semantically correct: from
 * the enclosing code's point of view, that name was never in scope.
 *
 * Bonus feature (functions, manual Section 14): the same Symbol/Scope
 * mechanism represents BOTH variables and functions, distinguished by
 * `SymbolKind`, in ONE namespace (matching how C itself resolves
 * identifiers) — see the `func` fields, used only when
 * `kind == SYMBOL_FUNC`. Functions are only ever declared in the
 * global scope (no nested function definitions), which keeps this
 * extension additive rather than requiring any change to how variable
 * scoping already works.
 */

typedef enum { SYMBOL_VAR, SYMBOL_FUNC } SymbolKind;

typedef struct Symbol {
    char *name;
    SymbolKind kind;
    DataType type;         /* variable's type, or a function's RETURN type */
    int scope_level;       /* 0 = global; increases with each nested block, for diagnostics */
    int line_declared;
    struct Symbol *next;   /* next symbol declared in the SAME scope */
    /* --- SYMBOL_FUNC only --- */
    ParamData *params;     /* not owned — points directly at the AST's own FuncDeclData.params, which outlives the whole compilation, so no copy is needed */
    int param_count;
    /* --- Advanced/unique extension: arrays --- */
    int is_array;           /* 1 if this symbol is a fixed-size array (of `type` elements) */
    int array_size;         /* valid only if is_array */
} Symbol;

typedef struct Scope {
    Symbol *symbols;       /* head of this scope's symbol list */
    struct Scope *parent;  /* enclosing scope, or NULL for the global scope */
    int level;
} Scope;

/* Create a new scope nested inside `parent` (pass NULL for the global scope). */
Scope *symtab_enter_scope(Scope *parent);

/* Destroy `scope` (freeing its symbols) and return its parent — this is
 * literally what makes the scope's variables go out of visibility. */
Scope *symtab_exit_scope(Scope *scope);

/* Declare a variable `name` in THIS scope (not parents). Returns 1 on
 * success, 0 if `name` is already declared in this exact scope
 * (redeclaration — the caller reports that as a semantic error;
 * shadowing an outer scope's variable is legal and NOT a
 * redeclaration, which is why this only checks the current scope, not
 * the whole chain). */
int symtab_insert(Scope *scope, const char *name, DataType type, int line_declared);

/* Declare a function `name` (bonus feature) — same success/failure
 * contract as symtab_insert. `params` is NOT copied; it must outlive
 * the Symbol (in practice, it points at the AST's own
 * FuncDeclData.params, which lives for the whole compilation). */
int symtab_insert_func(Scope *scope, const char *name, DataType return_type, ParamData *params, int param_count, int line_declared);

/* Declare a fixed-size array (advanced/unique feature) — same success/failure contract as symtab_insert. */
int symtab_insert_array(Scope *scope, const char *name, DataType elem_type, int size, int line_declared);

/* Search `scope` and then each enclosing scope in turn. Returns NULL if
 * not found anywhere (undeclared / out-of-scope use). */
Symbol *symtab_lookup(Scope *scope, const char *name);

/* True if `name` is declared in THIS scope specifically (used to detect redeclaration). */
int symtab_declared_in_current_scope(Scope *scope, const char *name);

#endif /* MINILANG_SYMBOL_TABLE_H */
