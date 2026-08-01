#ifndef MINILANG_ERROR_H
#define MINILANG_ERROR_H

/*
 * Shared error-reporting used by every compiler phase (lexer, parser,
 * semantic analyzer). Having ONE function all phases call keeps error
 * output consistently formatted and makes it trivial for main() to
 * decide whether to proceed to the next phase: it just checks
 * error_count() after each stage, per the pipeline in the project
 * manual (Section 2) — a lexical/syntax error must stop the pipeline
 * before semantic analysis runs on a possibly-malformed AST, and a
 * semantic error must stop it before TAC generation runs on an
 * unvalidated AST.
 */

/* phase: one of "Lexical", "Syntax", "Semantic" — printed in the message. */
void report_error(const char *phase, int line, const char *fmt, ...);

/* Total errors reported so far across all phases. */
int error_count(void);

/* Reset the counter to 0. The real compiler (main.c) never needs this —
 * it's a single-shot process — but the test harnesses under
 * verify_*.c use it to isolate one test case's error count from the
 * next. */
void reset_error_count(void);

#endif /* MINILANG_ERROR_H */
