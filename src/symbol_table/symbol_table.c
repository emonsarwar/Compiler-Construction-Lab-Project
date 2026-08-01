#define _POSIX_C_SOURCE 200809L  /* for strdup */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"

Scope *symtab_enter_scope(Scope *parent) {
    Scope *scope = (Scope *)malloc(sizeof(Scope));
    if (!scope) { fprintf(stderr, "fatal: out of memory creating scope\n"); exit(1); }
    scope->symbols = NULL;
    scope->parent = parent;
    scope->level = parent ? parent->level + 1 : 0;
    return scope;
}

Scope *symtab_exit_scope(Scope *scope) {
    Scope *parent = scope->parent;
    Symbol *sym = scope->symbols;
    while (sym) {
        Symbol *next = sym->next;
        free(sym->name);
        if (sym->kind == SYMBOL_FUNC && sym->params) {
            free(sym->params);
        }
        free(sym);
        sym = next;
    }
    free(scope);
    return parent;
}

int symtab_declared_in_current_scope(Scope *scope, const char *name) {
    for (Symbol *sym = scope->symbols; sym; sym = sym->next) {
        if (strcmp(sym->name, name) == 0) return 1;
    }
    return 0;
}

int symtab_insert(Scope *scope, const char *name, DataType type, int line_declared) {
    if (symtab_declared_in_current_scope(scope, name)) {
        return 0; /* redeclaration in this scope — caller reports the error */
    }
    Symbol *sym = (Symbol *)malloc(sizeof(Symbol));
    if (!sym) { fprintf(stderr, "fatal: out of memory inserting symbol\n"); exit(1); }
    sym->name = strdup(name);
    sym->kind = SYMBOL_VAR;
    sym->type = type;
    sym->scope_level = scope->level;
    sym->line_declared = line_declared;
    sym->params = NULL;
    sym->param_count = 0;
    sym->is_array = 0;
    sym->array_size = 0;
    sym->next = scope->symbols;  /* prepend: O(1) insert, most-recent-first within a scope */
    scope->symbols = sym;
    return 1;
}

int symtab_insert_array(Scope *scope, const char *name, DataType elem_type, int size, int line_declared) {
    if (symtab_declared_in_current_scope(scope, name)) {
        return 0;
    }
    Symbol *sym = (Symbol *)malloc(sizeof(Symbol));
    if (!sym) { fprintf(stderr, "fatal: out of memory inserting symbol\n"); exit(1); }
    sym->name = strdup(name);
    sym->kind = SYMBOL_VAR;
    sym->type = elem_type;
    sym->scope_level = scope->level;
    sym->line_declared = line_declared;
    sym->params = NULL;
    sym->param_count = 0;
    sym->is_array = 1;
    sym->array_size = size;
    sym->next = scope->symbols;
    scope->symbols = sym;
    return 1;
}

int symtab_insert_func(Scope *scope, const char *name, DataType return_type, ParamData *params, int param_count, int line_declared) {
    if (symtab_declared_in_current_scope(scope, name)) {
        return 0;
    }
    Symbol *sym = (Symbol *)malloc(sizeof(Symbol));
    if (!sym) { fprintf(stderr, "fatal: out of memory inserting symbol\n"); exit(1); }
    sym->name = strdup(name);
    sym->kind = SYMBOL_FUNC;
    sym->type = return_type;
    sym->scope_level = scope->level;
    sym->line_declared = line_declared;
    if (params && param_count > 0) {
        sym->params = (ParamData *)malloc(sizeof(ParamData) * (size_t)param_count);
        if (sym->params) {
            memcpy(sym->params, params, sizeof(ParamData) * (size_t)param_count);
        }
    } else {
        sym->params = NULL;
    }
    sym->param_count = param_count;
    sym->is_array = 0;
    sym->array_size = 0;
    sym->next = scope->symbols;
    scope->symbols = sym;
    return 1;
}

Symbol *symtab_lookup(Scope *scope, const char *name) {
    for (Scope *s = scope; s != NULL; s = s->parent) {
        for (Symbol *sym = s->symbols; sym; sym = sym->next) {
            if (strcmp(sym->name, name) == 0) return sym;
        }
    }
    return NULL;
}
