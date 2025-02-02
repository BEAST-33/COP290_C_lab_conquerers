CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS = -lm -pthread
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
EXEC = sheet

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(EXEC)
	./test_runner  # Replace with actual test command

report:
	pdflatex report.tex

clean:
	rm -f $(OBJ) $(EXEC) report.pdf

.PHONY: all clean test report