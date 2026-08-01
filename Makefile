CC     := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-function -g -D_POSIX_C_SOURCE=200809L
FLEX   := flex
BISON  := bison

BUILD  := build
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
else
EXEEXT :=
endif
BIN    := minilangc$(EXEEXT)

INCLUDES := -Isrc/ast -Isrc/symbol_table -Isrc/semantic -Isrc/codegen -Isrc/common -Isrc/interp -Isrc/optimize -I$(BUILD)

.PHONY: all clean test test-run

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

# --- Generated lexer/parser ---
# A single bison invocation with -d produces both parser.tab.c and its
# header in one shot; listing both as targets of one rule is the
# standard GNU Make idiom for that (see docs/design.md).

$(BUILD)/parser.tab.c $(BUILD)/parser.tab.h: src/parser/parser.y | $(BUILD)
	$(BISON) -d -o $(BUILD)/parser.tab.c $<

$(BUILD)/lex.yy.c: src/lexer/lexer.l | $(BUILD)
	$(FLEX) -o $(BUILD)/lex.yy.c $<

# --- Object files ---
# lex.yy.c's compilation (not its generation) is what needs
# parser.tab.h to exist, since lexer.l's #include "parser.tab.h" is
# only resolved when gcc compiles the generated .c file.

$(BUILD)/lex.yy.o: $(BUILD)/lex.yy.c $(BUILD)/parser.tab.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/parser.tab.o: $(BUILD)/parser.tab.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/main.o: src/main.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/ast.o: src/ast/ast.c src/ast/ast.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/symbol_table.o: src/symbol_table/symbol_table.c src/symbol_table/symbol_table.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/semantic.o: src/semantic/semantic.c src/semantic/semantic.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/tac.o: src/codegen/tac.c src/codegen/tac.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/error.o: src/common/error.c src/common/error.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# --- Advanced/unique extensions: TAC interpreter + optimizer ---

$(BUILD)/interp.o: src/interp/interp.c src/interp/interp.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/optimize.o: src/optimize/optimize.c src/optimize/optimize.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

OBJS := $(BUILD)/main.o $(BUILD)/ast.o $(BUILD)/symbol_table.o $(BUILD)/semantic.o \
        $(BUILD)/tac.o $(BUILD)/error.o $(BUILD)/interp.o $(BUILD)/optimize.o \
        $(BUILD)/lex.yy.o $(BUILD)/parser.tab.o

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@
	chmod +x $@

clean:
	rm -rf $(BUILD) $(BIN)

# Runs the compiler on every sample/test program under tests/ and
# examples/, printing which file is being run before each (useful for
# a quick smoke-test / live demo dry run).
test: $(BIN)
	@for f in examples/*.mc tests/valid/*.mc tests/lexical_errors/*.mc tests/syntax_errors/*.mc tests/semantic_errors/*.mc; do \
		echo "=================================================="; \
		echo "--- $$f ---"; \
		echo "=================================================="; \
		./$(BIN) $$f; \
		echo; \
	done

# Advanced/unique extension: same as `test`, but also passes -O -run
# to every VALID program, so the optimizer and interpreter both get
# exercised on the whole example/test suite in one command. Programs
# that use `read` (advanced_read_and_run.mc) need stdin, so a sample
# value is piped in for this smoke test.
test-run: $(BIN)
	@for f in examples/*.mc tests/valid/*.mc; do \
		echo "=================================================="; \
		echo "--- $$f (-O -run) ---"; \
		echo "=================================================="; \
		echo 5 | ./$(BIN) $$f -O -run; \
		echo; \
	done
