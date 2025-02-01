// This is version 2 of the spreadsheet program.
// Improvements over code_1.c:
// New Implementated function names -> scroll_viewport().
// Updated handle_command() function to handle the commands 'w', 'a', 's', and 'd'.
// scroll_viewport() now scrolls the viewport by 10 cells at a time using commands 'w', 'a', 's', and 'd'.
// even if we are at corner edges of the spreadsheet and we try to scroll in the direction of the edge, the viewport will not go out of bounds.
// scroll_viewport works even if the disable_output is their at the back the command is being executed.
// The handle_command() function is now implemented and it can handle the commands 'disable_output', 'enable_output', 'w', 'a', 's', and 'd'.
// Error checking new measures are added to the code.
    // The scrolling works even when at the edges of the sheet. For example, if you're at the leftmost edge and press 'a', it won't change the viewport, which is the desired behavior.
// spreadsheet working fine

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

void scroll_viewport(Spreadsheet* sheet, char direction) {
    switch (direction) {
        case 'w': // up
            if (sheet->viewport_row > 0) {
                sheet->viewport_row = (sheet->viewport_row > 10) ? sheet->viewport_row - 10 : 0;
            }
            break;
        case 's': // down
            if (sheet->viewport_row + VIEWPORT_SIZE < sheet->rows) {
                sheet->viewport_row = (sheet->viewport_row + 10 + VIEWPORT_SIZE <= sheet->rows) ? sheet->viewport_row + 10 : sheet->rows - VIEWPORT_SIZE;
            }
            break;
        case 'a': // left
            if (sheet->viewport_col > 0) {
                sheet->viewport_col = (sheet->viewport_col > 10) ? sheet->viewport_col - 10 : 0;
            }
            break;
        case 'd': // right
            if (sheet->viewport_col + VIEWPORT_SIZE < sheet->cols) {
                sheet->viewport_col = (sheet->viewport_col + 10 + VIEWPORT_SIZE <= sheet->cols) ? sheet->viewport_col + 10 : sheet->cols - VIEWPORT_SIZE;
            }
            break;
    }
}

void handle_command(Spreadsheet* sheet, const char* cmd) {
    if (strcmp(cmd, "disable_output") == 0) {
        sheet->output_enabled = false;
    } else if (strcmp(cmd, "enable_output") == 0) {
        sheet->output_enabled = true;
    } else if (strlen(cmd) == 1 && strchr("wasd", cmd[0])) {
        scroll_viewport(sheet, cmd[0]);
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