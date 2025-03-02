CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS = -lm -pthread

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
TEST_SRC = test_runner.c
TEST_OBJ = test_runner.o
EXEC = spreadsheet
TARGET_DIR = target/release

# LaTeX files
LATEX_MAIN = report.tex
LATEX_CLASS = styles.cls
LATEX_OUTPUT = report.pdf

# Filter out test files from main build
MAIN_SRC = $(filter-out $(TEST_SRC), $(SRC))
MAIN_OBJ = $(MAIN_SRC:.c=.o)

all: $(TARGET_DIR)/$(EXEC)

$(TARGET_DIR)/$(EXEC): $(MAIN_OBJ) | $(TARGET_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

# Build and run test suite
test: test_runner
	./test_runner

test_runner: $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Generate PDF report
report: $(LATEX_MAIN) $(LATEX_CLASS)
	pdflatex $(LATEX_MAIN)
	# Run twice to resolve references if needed
	pdflatex $(LATEX_MAIN)

clean:
	rm -f $(OBJ) $(EXEC) test_runner *.o *.tmp $(LATEX_OUTPUT) *.aux *.log *.toc *.out

.PHONY: all clean test report