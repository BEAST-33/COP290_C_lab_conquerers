CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS = -lm -pthread

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
TEST_SRC = test_runner.c
TEST_OBJ = test_runner.o
EXEC = sheet

# Filter out test files from main build
MAIN_SRC = $(filter-out $(TEST_SRC), $(SRC))
MAIN_OBJ = $(MAIN_SRC:.c=.o)

all: $(EXEC)

$(EXEC): $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build and run test suite
test: test_runner
	./test_runner

test_runner: $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Generate PDF report
report:
	pdflatex report.tex

clean:
	rm -f $(OBJ) $(EXEC) test_runner *.o *.tmp report.pdf

.PHONY: all clean test report