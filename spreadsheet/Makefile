CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS = -lm -pthread
SRC = $(wildcard *.c)
OBJ = $(filter-out test_%.o, $(SRC:.c=.o))  # Exclude test files from main build
EXEC = sheet
TEST_SRC = test_spreadsheet.c
TEST_RUNNER = test_runner.c
TEST_RUNNER_EXEC = test_runner
TEST_EXEC = test_spreadsheet

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_RUNNER_EXEC): $(TEST_RUNNER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(EXEC) $(TEST_RUNNER_EXEC)
	./$(TEST_RUNNER_EXEC)

test_build: $(TEST_SRC)
	$(CC) $(CFLAGS) -o $(TEST_EXEC) $^ $(LDFLAGS)

test_run: test_build
	./$(TEST_EXEC)

report:
	pdflatex report.tex

clean:
	rm -f $(OBJ) $(EXEC) report.pdf $(TEST_EXEC) $(TEST_RUNNER_EXEC) *.tmp

.PHONY: all clean test test_build test_run report