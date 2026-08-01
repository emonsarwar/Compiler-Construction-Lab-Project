#define _POSIX_C_SOURCE 200809L  /* for strdup */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "interp.h"

/* ============================== Values ============================== */

typedef struct {
    DataType type;   /* TYPE_INT, TYPE_FLOAT, TYPE_BOOL, or TYPE_STRING */
    long long i;      /* used for TYPE_INT and TYPE_BOOL (0/1) */
    double f;          /* used for TYPE_FLOAT */
    char *s;            /* used for TYPE_STRING — owned copy */
} Value;

static Value value_int(long long i)   { Value v; v.type = TYPE_INT;   v.i = i; v.f = 0; v.s = NULL; return v; }
static Value value_float(double f)     { Value v; v.type = TYPE_FLOAT; v.i = 0; v.f = f; v.s = NULL; return v; }
static Value value_bool(int b)          { Value v; v.type = TYPE_BOOL;  v.i = b ? 1 : 0; v.f = 0; v.s = NULL; return v; }
static Value value_string(const char *s) { Value v; v.type = TYPE_STRING; v.i = 0; v.f = 0; v.s = strdup(s ? s : ""); return v; }

/* Every Value-returning lookup below must hand back an INDEPENDENTLY
 * owned copy (fresh strdup for TYPE_STRING) — never an aliased
 * pointer into a frame's own storage. Without this, a caller that
 * later frees a "borrowed" string (e.g. after printing it) would
 * leave the frame holding a dangling pointer, causing a double-free
 * the next time that frame is freed or the variable reassigned. */
static Value value_copy(Value v) {
    if (v.type == TYPE_STRING) return value_string(v.s);
    return v;
}

static double value_as_double(Value v) {
    switch (v.type) {
        case TYPE_INT:   return (double)v.i;
        case TYPE_FLOAT:  return v.f;
        case TYPE_BOOL:    return (double)v.i;
        default:            return 0.0;
    }
}
static long long value_as_int(Value v) {
    switch (v.type) {
        case TYPE_INT:   return v.i;
        case TYPE_FLOAT:  return (long long)v.f;
        case TYPE_BOOL:    return v.i;
        default:            return 0;
    }
}
static int value_as_bool(Value v) {
    switch (v.type) {
        case TYPE_BOOL: return v.i != 0;
        case TYPE_INT:   return v.i != 0;
        case TYPE_FLOAT:  return v.f != 0.0;
        default:            return v.s && v.s[0] != '\0';
    }
}

static void value_print(Value v, FILE *out) {
    switch (v.type) {
        case TYPE_INT:    fprintf(out, "%lld\n", v.i); break;
        case TYPE_FLOAT:   fprintf(out, "%g\n", v.f); break;
        case TYPE_BOOL:     fprintf(out, "%s\n", v.i ? "true" : "false"); break;
        case TYPE_STRING:   fprintf(out, "%s\n", v.s ? v.s : ""); break;
        default:              fprintf(out, "?\n"); break;
    }
}

/* ============================== Frames ============================== */
/* One frame = one activation record: the global frame (index 0, never
 * popped) plus one fresh frame per active function call — see
 * interp.h's docstring for why this is what makes recursion correct. */

typedef struct { char *name; Value value; } VarEntry;
typedef struct { char *name; Value *elems; int size; } ArrEntry;

typedef struct {
    VarEntry *vars; int var_count, var_cap;
    ArrEntry *arrs; int arr_count, arr_cap;
} Frame;

static Frame *frame_new(void) {
    Frame *f = (Frame *)malloc(sizeof(Frame));
    f->var_count = 0; f->var_cap = 8;
    f->vars = (VarEntry *)malloc(sizeof(VarEntry) * (size_t)f->var_cap);
    f->arr_count = 0; f->arr_cap = 4;
    f->arrs = (ArrEntry *)malloc(sizeof(ArrEntry) * (size_t)f->arr_cap);
    return f;
}

static void frame_free(Frame *f) {
    for (int i = 0; i < f->var_count; i++) { free(f->vars[i].name); if (f->vars[i].value.s) free(f->vars[i].value.s); }
    for (int i = 0; i < f->arr_count; i++) {
        free(f->arrs[i].name);
        for (int j = 0; j < f->arrs[i].size; j++) if (f->arrs[i].elems[j].s) free(f->arrs[i].elems[j].s);
        free(f->arrs[i].elems);
    }
    free(f->vars);
    free(f->arrs);
    free(f);
}

static VarEntry *frame_find_var(Frame *f, const char *name) {
    for (int i = 0; i < f->var_count; i++) if (strcmp(f->vars[i].name, name) == 0) return &f->vars[i];
    return NULL;
}

static void frame_put_var(Frame *f, const char *name, Value v) {
    VarEntry *e = frame_find_var(f, name);
    if (e) {
        if (e->value.s) free(e->value.s);
        e->value = v;
        return;
    }
    if (f->var_count == f->var_cap) {
        f->var_cap *= 2;
        f->vars = (VarEntry *)realloc(f->vars, sizeof(VarEntry) * (size_t)f->var_cap);
    }
    f->vars[f->var_count].name = strdup(name);
    f->vars[f->var_count].value = v;
    f->var_count++;
}

static ArrEntry *frame_find_arr(Frame *f, const char *name) {
    for (int i = 0; i < f->arr_count; i++) if (strcmp(f->arrs[i].name, name) == 0) return &f->arrs[i];
    return NULL;
}

/* Grows (or creates) `name`'s backing array in-place so index `idx` is
 * valid; new slots default to int 0 — see interp.h's docstring on why
 * arrays are sized on demand rather than up front. */
static ArrEntry *frame_ensure_arr(Frame *f, const char *name, int idx) {
    ArrEntry *e = frame_find_arr(f, name);
    if (!e) {
        if (f->arr_count == f->arr_cap) {
            f->arr_cap *= 2;
            f->arrs = (ArrEntry *)realloc(f->arrs, sizeof(ArrEntry) * (size_t)f->arr_cap);
        }
        e = &f->arrs[f->arr_count++];
        e->name = strdup(name);
        e->size = 0;
        e->elems = NULL;
    }
    if (idx >= e->size) {
        int new_size = idx + 1;
        e->elems = (Value *)realloc(e->elems, sizeof(Value) * (size_t)new_size);
        for (int i = e->size; i < new_size; i++) e->elems[i] = value_int(0);
        e->size = new_size;
    }
    return e;
}

/* ====================== Call-stack-aware var/array access ======================
 * Policy (documented in interp.h): look in the current (innermost)
 * frame first; if not found and we're inside a function call, fall
 * back to the global frame — matching this language's own scoping
 * rule that a function body's enclosing scope is the global scope.
 * On write: update wherever the name already lives; if it doesn't
 * exist anywhere yet, create it in the CURRENT frame. */

static Value get_var(Frame **stack, int depth, const char *name) {
    VarEntry *e = frame_find_var(stack[depth], name);
    if (!e && depth > 0) e = frame_find_var(stack[0], name);
    if (e) return value_copy(e->value);
    return value_int(0); /* defensive default — unreachable for a program that passed semantic analysis */
}

static void set_var(Frame **stack, int depth, const char *name, Value v) {
    if (depth > 0 && frame_find_var(stack[depth], name)) { frame_put_var(stack[depth], name, v); return; }
    if (depth > 0 && frame_find_var(stack[0], name)) { frame_put_var(stack[0], name, v); return; }
    frame_put_var(stack[depth], name, v);
}

static Value get_arr(Frame **stack, int depth, const char *name, int idx) {
    Frame *owner = stack[depth];
    if (!frame_find_arr(owner, name) && depth > 0 && frame_find_arr(stack[0], name)) owner = stack[0];
    ArrEntry *e = frame_ensure_arr(owner, name, idx < 0 ? 0 : idx);
    if (idx < 0 || idx >= e->size) return value_int(0);
    return value_copy(e->elems[idx]);
}

static void set_arr(Frame **stack, int depth, const char *name, int idx, Value v) {
    Frame *owner = stack[depth];
    if (!frame_find_arr(owner, name) && depth > 0 && frame_find_arr(stack[0], name)) owner = stack[0];
    ArrEntry *e = frame_ensure_arr(owner, name, idx < 0 ? 0 : idx);
    if (idx >= 0 && idx < e->size) {
        if (e->elems[idx].s) free(e->elems[idx].s);
        e->elems[idx] = v;
    }
}

/* ============================== Places ============================== */
/* A TAC "place" (see tac.h) is either a literal's own text, a quoted
 * string, or a variable name — resolve it to a runtime Value. */

static int looks_like_number(const char *s) {
    if (!s || !*s) return 0;
    int i = 0;
    if (s[0] == '-') i = 1;
    if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static Value resolve_place(Frame **stack, int depth, const char *place) {
    if (!place) return value_int(0);
    if (place[0] == '"') {
        size_t len = strlen(place);
        char *buf = strdup(place + 1);
        if (len >= 2 && buf[len - 2] == '"') buf[len - 2] = '\0';
        Value v = value_string(buf);
        free(buf);
        return v;
    }
    if (strcmp(place, "true") == 0) return value_bool(1);
    if (strcmp(place, "false") == 0) return value_bool(0);
    if (looks_like_number(place)) {
        if (strchr(place, '.')) return value_float(atof(place));
        return value_int(atoll(place));
    }
    return get_var(stack, depth, place);
}

/* ============================== Operators ============================== */

static Value eval_binop(const char *op, Value l, Value r) {
    if (!strcmp(op, "+") && l.type == TYPE_STRING && r.type == TYPE_STRING) {
        size_t len = strlen(l.s) + strlen(r.s) + 1;
        char *buf = (char *)malloc(len);
        snprintf(buf, len, "%s%s", l.s, r.s);
        Value v = value_string(buf);
        free(buf);
        return v;
    }
    int use_float = (l.type == TYPE_FLOAT || r.type == TYPE_FLOAT);
    if (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") || !strcmp(op, "/")) {
        if (use_float) {
            double a = value_as_double(l), b = value_as_double(r);
            if (!strcmp(op, "+")) return value_float(a + b);
            if (!strcmp(op, "-")) return value_float(a - b);
            if (!strcmp(op, "*")) return value_float(a * b);
            return value_float(b != 0.0 ? a / b : 0.0);
        } else {
            long long a = value_as_int(l), b = value_as_int(r);
            if (!strcmp(op, "+")) return value_int(a + b);
            if (!strcmp(op, "-")) return value_int(a - b);
            if (!strcmp(op, "*")) return value_int(a * b);
            return value_int(b != 0 ? a / b : 0);
        }
    }
    if (!strcmp(op, "%")) {
        long long a = value_as_int(l), b = value_as_int(r);
        return value_int(b != 0 ? a % b : 0);
    }
    if (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) {
        double a = value_as_double(l), b = value_as_double(r);
        if (!strcmp(op, "<"))  return value_bool(a < b);
        if (!strcmp(op, ">"))  return value_bool(a > b);
        if (!strcmp(op, "<=")) return value_bool(a <= b);
        return value_bool(a >= b);
    }
    if (!strcmp(op, "==") || !strcmp(op, "!=")) {
        int eq;
        if (l.type == TYPE_STRING || r.type == TYPE_STRING) eq = (l.s && r.s) ? (strcmp(l.s, r.s) == 0) : 0;
        else eq = (value_as_double(l) == value_as_double(r));
        return value_bool(!strcmp(op, "==") ? eq : !eq);
    }
    if (!strcmp(op, "&&")) return value_bool(value_as_bool(l) && value_as_bool(r));
    if (!strcmp(op, "||")) return value_bool(value_as_bool(l) || value_as_bool(r));
    return value_int(0);
}

static Value eval_unary(const char *op, Value v) {
    if (!strcmp(op, "-")) {
        if (v.type == TYPE_FLOAT) return value_float(-v.f);
        return value_int(-value_as_int(v));
    }
    if (!strcmp(op, "!")) return value_bool(!value_as_bool(v));
    return v;
}

/* ============================== Reading input ============================== */
/* `read` (advanced feature) auto-detects the type of whatever
 * whitespace-delimited token it reads — TAC carries no type
 * information at all (see interp.h), so this is a deliberate, simple
 * design choice rather than an oversight: it means `read` works
 * uniformly for int/float/bool/string targets with no extra plumbing. */
static Value read_input_value(void) {
    char buf[256];
    if (scanf("%255s", buf) != 1) return value_int(0);
    if (!strcmp(buf, "true")) return value_bool(1);
    if (!strcmp(buf, "false")) return value_bool(0);
    if (looks_like_number(buf)) {
        if (strchr(buf, '.')) return value_float(atof(buf));
        return value_int(atoll(buf));
    }
    return value_string(buf);
}

/* ============================== Function signature table ============================== */

FuncSigTable *build_func_sig_table(ASTNode *program) {
    FuncSigTable *t = (FuncSigTable *)malloc(sizeof(FuncSigTable));
    t->count = 0;
    int cap = 4;
    t->funcs = (FuncSig *)malloc(sizeof(FuncSig) * (size_t)cap);
    for (int i = 0; i < program->as.stmt_list.count; i++) {
        ASTNode *item = program->as.stmt_list.stmts[i];
        if (item->kind != AST_FUNC_DECL) continue;
        if (t->count == cap) { cap *= 2; t->funcs = (FuncSig *)realloc(t->funcs, sizeof(FuncSig) * (size_t)cap); }
        FuncSig *sig = &t->funcs[t->count++];
        sig->name = strdup(item->as.func_decl.name);
        sig->param_count = item->as.func_decl.param_count;
        sig->param_names = (char **)malloc(sizeof(char *) * (size_t)(sig->param_count > 0 ? sig->param_count : 1));
        for (int p = 0; p < sig->param_count; p++) sig->param_names[p] = strdup(item->as.func_decl.params[p].name);
    }
    return t;
}

void free_func_sig_table(FuncSigTable *t) {
    if (!t) return;
    for (int i = 0; i < t->count; i++) {
        free(t->funcs[i].name);
        for (int p = 0; p < t->funcs[i].param_count; p++) free(t->funcs[i].param_names[p]);
        free(t->funcs[i].param_names);
    }
    free(t->funcs);
    free(t);
}

static const FuncSig *find_sig(const FuncSigTable *t, const char *name) {
    if (!t) return NULL;
    for (int i = 0; i < t->count; i++) if (strcmp(t->funcs[i].name, name) == 0) return &t->funcs[i];
    return NULL;
}

/* ============================== Main interpreter loop ============================== */

#define MAX_CALL_DEPTH 20000
#define MAX_PENDING_ARGS 4096

void interp_run(const TACList *list, const FuncSigTable *sigs) {
    /* Resolve every label (TAC_LABEL and TAC_FUNC_BEGIN both store
     * their name in `result`) to its instruction index, up front. */
    char **label_names = (char **)malloc(sizeof(char *) * (size_t)(list->count > 0 ? list->count : 1));
    int *label_indices = (int *)malloc(sizeof(int) * (size_t)(list->count > 0 ? list->count : 1));
    int label_count = 0;
    for (int i = 0; i < list->count; i++) {
        if (list->instrs[i].kind == TAC_LABEL || list->instrs[i].kind == TAC_FUNC_BEGIN) {
            label_names[label_count] = list->instrs[i].result;
            label_indices[label_count] = i;
            label_count++;
        }
    }

    /* For every TAC_FUNC_BEGIN, precompute where its body ENDS, using
     * the precise ranges TACList itself recorded during generation
     * (see tac.c's tac_generate) — NOT "the next TAC_FUNC_BEGIN, or
     * end of list", which is wrong for whichever function happens to
     * be declared last (it would skip every top-level statement after
     * it too). Ordinary top-level execution must always skip over
     * this range, never fall into it, since a function only ever runs
     * via TAC_CALL. */
    int *func_skip_end = (int *)malloc(sizeof(int) * (size_t)(list->count > 0 ? list->count : 1));
    for (int i = 0; i < list->count; i++) func_skip_end[i] = i + 1; /* default: not a func begin, unused */
    for (int r = 0; r < list->func_range_count; r++) {
        func_skip_end[list->func_ranges[r].begin] = list->func_ranges[r].end;
    }



    Frame **stack = (Frame **)malloc(sizeof(Frame *) * MAX_CALL_DEPTH);
    int *return_ip = (int *)malloc(sizeof(int) * MAX_CALL_DEPTH);
    char **result_var = (char **)malloc(sizeof(char *) * MAX_CALL_DEPTH);
    int depth = 0;
    stack[0] = frame_new();

    Value *pending_args = (Value *)malloc(sizeof(Value) * MAX_PENDING_ARGS);
    int pending_count = 0;

    int ip = 0;
    while (ip < list->count) {
        const TACInstr *ins = &list->instrs[ip];
        switch (ins->kind) {
            case TAC_LABEL:
                ip++;
                break;

            case TAC_FUNC_BEGIN:
                /* Always reached by linear fall-through, never by a
                 * call (a call jumps straight past this line — see
                 * TAC_CALL below), so it always means "skip this
                 * function's body entirely". */
                ip = func_skip_end[ip];
                break;

            case TAC_ASSIGN:
                set_var(stack, depth, ins->result, resolve_place(stack, depth, ins->arg1));
                ip++;
                break;

            case TAC_BINOP: {
                Value l = resolve_place(stack, depth, ins->arg1);
                Value r = resolve_place(stack, depth, ins->arg2);
                Value result = eval_binop(ins->op, l, r);
                if (l.s) free(l.s);
                if (r.s) free(r.s);
                set_var(stack, depth, ins->result, result);
                ip++;
                break;
            }

            case TAC_UNARYOP: {
                Value v = resolve_place(stack, depth, ins->arg1);
                Value result = eval_unary(ins->op, v);
                if (v.s) free(v.s);
                set_var(stack, depth, ins->result, result);
                ip++;
                break;
            }

            case TAC_GOTO: {
                int target = -1;
                for (int i = 0; i < label_count; i++) if (!strcmp(label_names[i], ins->arg1)) { target = label_indices[i]; break; }
                ip = (target >= 0) ? target : ip + 1;
                break;
            }

            case TAC_IF_FALSE: {
                Value cond = resolve_place(stack, depth, ins->arg1);
                int falsy = !value_as_bool(cond);
                if (cond.s) free(cond.s);
                if (falsy) {
                    int target = -1;
                    for (int i = 0; i < label_count; i++) if (!strcmp(label_names[i], ins->arg2)) { target = label_indices[i]; break; }
                    ip = (target >= 0) ? target : ip + 1;
                } else {
                    ip++;
                }
                break;
            }

            case TAC_PRINT: {
                Value v = resolve_place(stack, depth, ins->arg1);
                value_print(v, stdout);
                if (v.s) free(v.s);
                ip++;
                break;
            }

            case TAC_PARAM: {
                if (pending_count < MAX_PENDING_ARGS) pending_args[pending_count++] = resolve_place(stack, depth, ins->arg1);
                ip++;
                break;
            }

            case TAC_CALL: {
                const char *func_name = ins->arg1;
                int argc = atoi(ins->arg2);
                const FuncSig *sig = find_sig(sigs, func_name);
                int target = -1;
                for (int i = 0; i < label_count; i++) if (!strcmp(label_names[i], func_name)) { target = label_indices[i]; break; }

                if (target < 0 || depth + 1 >= MAX_CALL_DEPTH) {
                    fprintf(stderr, "runtime error: cannot call '%s' (undefined or recursion too deep)\n", func_name);
                    ip = list->count; /* halt */
                    break;
                }

                Frame *callee = frame_new();
                int base = pending_count - argc;
                if (base < 0) base = 0;
                if (sig) {
                    for (int p = 0; p < argc && p < sig->param_count; p++) {
                        frame_put_var(callee, sig->param_names[p], pending_args[base + p]);
                    }
                }
                pending_count = base; /* pop exactly this call's args — see interp.h on nested-call argument ordering */

                depth++;
                stack[depth] = callee;
                return_ip[depth] = ip + 1;
                result_var[depth] = ins->result; /* NULL if the call's result is discarded */
                ip = target + 1; /* jump straight into the body, past the TAC_FUNC_BEGIN line itself */
                break;
            }

            case TAC_RETURN: {
                Value v = resolve_place(stack, depth, ins->arg1);
                int ret_ip = return_ip[depth];
                char *rv = result_var[depth];
                Frame *finished = stack[depth];
                if (depth > 0) depth--;
                if (rv) set_var(stack, depth, rv, v);
                else if (v.s) free(v.s);
                frame_free(finished);
                ip = ret_ip;
                break;
            }

            case TAC_ARR_STORE: {
                Value idxv = resolve_place(stack, depth, ins->arg2);
                int idx = (int)value_as_int(idxv);
                if (idxv.s) free(idxv.s);
                Value v = resolve_place(stack, depth, ins->arg1);
                set_arr(stack, depth, ins->result, idx, v);
                ip++;
                break;
            }

            case TAC_ARR_LOAD: {
                Value idxv = resolve_place(stack, depth, ins->arg2);
                int idx = (int)value_as_int(idxv);
                if (idxv.s) free(idxv.s);
                Value v = get_arr(stack, depth, ins->arg1, idx);
                set_var(stack, depth, ins->result, v);
                ip++;
                break;
            }

            case TAC_READ: {
                const char *target_text = ins->arg1;
                const char *bracket = strchr(target_text, '[');
                if (bracket) {
                    char name[128];
                    size_t namelen = (size_t)(bracket - target_text);
                    if (namelen >= sizeof(name)) namelen = sizeof(name) - 1;
                    memcpy(name, target_text, namelen);
                    name[namelen] = '\0';
                    char idx_text[128];
                    const char *end = strrchr(target_text, ']');
                    size_t idxlen = end ? (size_t)(end - bracket - 1) : 0;
                    if (idxlen >= sizeof(idx_text)) idxlen = sizeof(idx_text) - 1;
                    memcpy(idx_text, bracket + 1, idxlen);
                    idx_text[idxlen] = '\0';
                    Value idxv = resolve_place(stack, depth, idx_text);
                    int idx = (int)value_as_int(idxv);
                    if (idxv.s) free(idxv.s);
                    set_arr(stack, depth, name, idx, read_input_value());
                } else {
                    set_var(stack, depth, target_text, read_input_value());
                }
                ip++;
                break;
            }
        }
    }

    for (int d = 0; d <= depth; d++) frame_free(stack[d]);
    free(stack);
    free(return_ip);
    free(result_var);
    free(pending_args);
    free(label_names);
    free(label_indices);
    free(func_skip_end);
}
