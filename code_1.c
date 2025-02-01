// Implemented a simple spreadsheet with sheet creation. 
// 10x10 viewport, q to quit, disable_output, enable_output
// Implemented functions name -> create_spreadsheet, get_column_name, print_spreadsheet, free_spreadsheet.
// Created Structure -> Cell, Spreadsheet.
// Error checking added ->
    // checks dimensions of the column and row whether given inputs are inbound or out of bound
    // checks when calling ./sheet user is giving correct input of column and row in int or not 
    // checks whether the inputs are of format ./sheet row column
    // if user press enter without giving any command previous table will be reprinted

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_ROWS 999
#define MAX_COLS 18278
#define VIEWPORT_SIZE 10

typedef struct Cell {
    int value;
    char* formula;
    int error_state;
    struct Cell** dependents;
    int dep_count;
} Cell;

typedef struct {
    Cell*** grid;
    int rows;
    int cols;
    int viewport_row;
    int viewport_col;
    bool output_enabled;
} Spreadsheet;

char* get_column_name(int col) {
    char* name = malloc(4 * sizeof(char));
    int i = 0;
    while (col > 0) {
        name[i++] = 'A' + (col - 1) % 26;
        col = (col - 1) / 26;
    }
    name[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char temp = name[j];
        name[j] = name[i - 1 - j];
        name[i - 1 - j] = temp;
    }
    return name;
}

Spreadsheet* create_spreadsheet(int rows, int cols) {
    if (rows < 1 || rows > MAX_ROWS || cols < 1 || cols > MAX_COLS) {
        fprintf(stderr, "Invalid spreadsheet dimensions\n");
        return NULL;
    }

    Spreadsheet* sheet = malloc(sizeof(Spreadsheet));
    sheet->rows = rows;
    sheet->cols = cols;
    sheet->viewport_row = 0;
    sheet->viewport_col = 0;
    sheet->output_enabled = true;

    sheet->grid = malloc(rows * sizeof(Cell**));
    for (int i = 0; i < rows; i++) {
        sheet->grid[i] = malloc(cols * sizeof(Cell*));
        for (int j = 0; j < cols; j++) {
            sheet->grid[i][j] = calloc(1, sizeof(Cell));
            sheet->grid[i][j]->value = 0;
            sheet->grid[i][j]->formula = NULL;
            sheet->grid[i][j]->error_state = 0;
            sheet->grid[i][j]->dependents = NULL;
            sheet->grid[i][j]->dep_count = 0;
        }
    }

    return sheet;
}

void print_spreadsheet(Spreadsheet* sheet) {
    if (!sheet->output_enabled) return;

    int end_row = sheet->viewport_row + VIEWPORT_SIZE;
    int end_col = sheet->viewport_col + VIEWPORT_SIZE;

    if (end_row > sheet->rows) end_row = sheet->rows;
    if (end_col > sheet->cols) end_col = sheet->cols;

    printf("    ");
    for (int j = sheet->viewport_col; j < end_col; j++) {
        char* col_name = get_column_name(j + 1);
        printf("%-8s", col_name);
        free(col_name);
    }
    printf("\n");

    for (int i = sheet->viewport_row; i < end_row; i++) {
        printf("%-4d", i + 1);
        for (int j = sheet->viewport_col; j < end_col; j++) {
            if (sheet->grid[i][j]->error_state) {
                printf("%-8s", "ERR");
            } else {
                printf("%-8d", sheet->grid[i][j]->value);
            }
        }
        printf("\n");
    }
}

void handle_command(Spreadsheet* sheet, const char* cmd) {
    if (strcmp(cmd, "disable_output") == 0) {
        sheet->output_enabled = false;
    } else if (strcmp(cmd, "enable_output") == 0) {
        sheet->output_enabled = true;
    }
    // Add other command handlers
}

int eval_expression(Spreadsheet* sheet, int row, int col, const char* expr) {
    // Implement expression evaluation
    return 0; // Placeholder
}

void update_dependencies(Spreadsheet* sheet, int row, int col) {
    // Implement dependency graph updates
}

void free_spreadsheet(Spreadsheet* sheet) {
    for (int i = 0; i < sheet->rows; i++) {
        for (int j = 0; j < sheet->cols; j++) {
            free(sheet->grid[i][j]->formula);
            free(sheet->grid[i][j]->dependents);
            free(sheet->grid[i][j]);
        }
        free(sheet->grid[i]);
    }
    free(sheet->grid);
    free(sheet);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <rows> <columns>\n", argv[0]);
        return 1;
    }

    int rows = atoi(argv[1]);
    int cols = atoi(argv[2]);

    Spreadsheet* sheet = create_spreadsheet(rows, cols);
    if (!sheet) return 1;

    char input[256];
    while (1) {
        print_spreadsheet(sheet);
        printf("[0.0] (ok) > ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        input[strcspn(input, "\n")] = 0; // Remove newline
        if (strcmp(input, "q") == 0) break;
        handle_command(sheet, input);
    }

    free_spreadsheet(sheet);
    return 0;
}