#include <stdio.h>
#include <stdarg.h>
#include "error.h"

static int g_error_count = 0;

void report_error(const char *phase, int line, const char *fmt, ...) {
    fprintf(stderr, "[%s Error] line %d: ", phase, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
    g_error_count++;
}

int error_count(void) {
    return g_error_count;
}

void reset_error_count(void) {
    g_error_count = 0;
}
