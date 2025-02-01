#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/time.h> // For time measurement

#define MAX_ROWS 999
#define MAX_COLS 18278
#define VIEWPORT_SIZE 10

// Add status enumeration
typedef enum {
    CMD_OK,
    CMD_UNRECOGNIZED,
    CMD_INVALID_CELL,
    CMD_INVALID_RANGE,
    CMD_CIRCULAR_REF,
    CMD_DIV_BY_ZERO
} CommandStatus;

// Define the cell structure
typedef struct Cell {
    int value;
    char* formula;
    int error_state;
    struct Cell** dependents;
    int dep_count;
} Cell;

// Define the spreadsheet structure
typedef struct {
    Cell*** grid;
    int rows;
    int cols;
    int viewport_row;
    int viewport_col;
    bool output_enabled;
} Spreadsheet;

// Function to get column name

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

// Function to create a new spreadsheet

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

// Function to print the spreadsheet grid
// version 2

void print_spreadsheet(Spreadsheet* sheet) {
    if (!sheet->output_enabled) return;

    // Calculate display bounds dynamically
    int start_row = sheet->viewport_row;
    int start_col = sheet->viewport_col;
    
    // Handle edge cases for rows
    int display_rows = (sheet->rows - start_row < VIEWPORT_SIZE) ? 
                       sheet->rows - start_row : VIEWPORT_SIZE;
    
    // Handle edge cases for columns
    int display_cols = (sheet->cols - start_col < VIEWPORT_SIZE) ? 
                        sheet->cols - start_col : VIEWPORT_SIZE;

    printf("    ");
    for (int j = start_col; j < start_col + display_cols; j++) {
        char* col_name = get_column_name(j + 1);
        printf("%-8s", col_name);
        free(col_name);
    }
    printf("\n");

    for (int i = start_row; i < start_row + display_rows; i++) {
        printf("%-4d", i + 1);
        for (int j = start_col; j < start_col + display_cols; j++) {
            if (sheet->grid[i][j]->error_state) {
                printf("%-8s", "ERR");
            } else {
                printf("%-8d", sheet->grid[i][j]->value);
            }
        }
        printf("\n");
    }
}

// Function to convert column name to number

int column_name_to_number(const char* name) {
    int result = 0;
    for (int i = 0; name[i] != '\0'; i++) {
        result *= 26;
        result += (toupper(name[i]) - 'A' + 1);
    }
    return result - 1;
}


// Function to parse cell reference
// version 2

void parse_cell_reference(const char* cell, int* row, int* col) {
    char col_name[4] = {0};
    int i = 0;
    
    // Validate and extract column name (must be uppercase)
    while (cell[i] != '\0' && isupper(cell[i])) {
        if (i >= 3) {
            *row = -1;
            *col = -1;
            return; // Invalid column name (too long)
        }
        col_name[i] = cell[i];
        i++;
    }
    
    // Validate row number (must be digits)
    if (cell[i] == '\0' || !isdigit(cell[i])) {
        *row = -1;
        *col = -1;
        return; // Invalid cell reference
    }
    
    *col = column_name_to_number(col_name);
    *row = atoi(cell + i) - 1;
    
    // Check for invalid characters after row number
    while (cell[i] != '\0') {
        if (!isdigit(cell[i])) {
            *row = -1;
            *col = -1;
            return; // Invalid characters after row number
        }
        i++;
    }
}

// Function to scroll to a cell

// version 4

// Modify scroll_to_cell to return success status
bool scroll_to_cell(Spreadsheet* sheet, const char* cell) {
    int row, col;
    parse_cell_reference(cell, &row, &col);

    if (row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols) {
        return false;
    }

    sheet->viewport_row = row;
    sheet->viewport_col = col;

    // Boundary checks
    if (sheet->viewport_row > sheet->rows - 1) 
        sheet->viewport_row = sheet->rows - 1;
    if (sheet->viewport_col > sheet->cols - 1) 
        sheet->viewport_col = sheet->cols - 1;
    
    return true;
}

// version 3

// void scroll_to_cell(Spreadsheet* sheet, const char* cell) {
//     int row, col;
//     parse_cell_reference(cell, &row, &col);

//     if (row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols) {
//         printf("Invalid cell reference\n");
//         return;
//     }

//     // Set viewport to target cell without forcing 10x10 grid
//     sheet->viewport_row = row;
//     sheet->viewport_col = col;

//     // Ensure valid viewport start positions
//     if (sheet->viewport_row > sheet->rows - 1) sheet->viewport_row = sheet->rows - 1;
//     if (sheet->viewport_col > sheet->cols - 1) sheet->viewport_col = sheet->cols - 1;
//     if (sheet->viewport_row < 0) sheet->viewport_row = 0;
//     if (sheet->viewport_col < 0) sheet->viewport_col = 0;
// }

// Function to scroll the viewport

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

// Function to handle commands

// version 4

// Update handle_command implementation
CommandStatus handle_command(Spreadsheet* sheet, const char* cmd) {
    if (strcmp(cmd, "disable_output") == 0) {
        sheet->output_enabled = false;
        return CMD_OK;
    } else if (strcmp(cmd, "enable_output") == 0) {
        sheet->output_enabled = true;
        return CMD_OK;
    } else if (strlen(cmd) == 1 && strchr("wasd", cmd[0])) {
        scroll_viewport(sheet, cmd[0]);
        return CMD_OK;
    } else if (strncmp(cmd, "scroll_to ", 10) == 0) {
        if (!scroll_to_cell(sheet, cmd + 10)) {
            return CMD_INVALID_CELL;
        }
        return CMD_OK;
    }
    // Add formula handling later
    return CMD_UNRECOGNIZED;
}

// void handle_command(Spreadsheet* sheet, const char* cmd) {
//     if (strcmp(cmd, "disable_output") == 0) {
//         sheet->output_enabled = false;
//     } else if (strcmp(cmd, "enable_output") == 0) {
//         sheet->output_enabled = true;
//     } else if (strlen(cmd) == 1 && strchr("wasd", cmd[0])) {
//         scroll_viewport(sheet, cmd[0]);
//     } else if (strncmp(cmd, "scroll_to ", 10) == 0) {
//         if (sheet->output_enabled) {
//             scroll_to_cell(sheet, cmd + 10);
//         }
//     }
//     // Add other command handlers
// }

// Function to evaluate expressions

int eval_expression(Spreadsheet* sheet, int row, int col, const char* expr) {
    // Implement expression evaluation
    return 0; // Placeholder
}

// Function to update dependencies

void update_dependencies(Spreadsheet* sheet, int row, int col) {
    // Implement dependency graph updates
}

// Function to free the spreadsheet

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

// Main function

// version 2

// Update main function
int main(int argc, char* argv[]) {
    // ... existing initialization code ...
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <rows> <columns>\n", argv[0]);
        return 1;
    }

    int rows = atoi(argv[1]);
    int cols = atoi(argv[2]);

    Spreadsheet* sheet = create_spreadsheet(rows, cols);
    if (!sheet) return 1;

    double last_time = 0.0;
    const char* last_status = "ok";
    struct timeval start, end;

    char input[256];
    while (1) {
        print_spreadsheet(sheet);
        printf("[%.1f] (%s) > ", last_time, last_status);
        
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "q") == 0) break;

        gettimeofday(&start, NULL);
        CommandStatus status = handle_command(sheet, input);
        gettimeofday(&end, NULL);

        // Calculate elapsed time
        last_time = (end.tv_sec - start.tv_sec) + 
                   (end.tv_usec - start.tv_usec) / 1000000.0;

        // Determine status message
        switch (status) {
            case CMD_OK: 
                last_status = "ok";
                break;
            case CMD_UNRECOGNIZED: 
                last_status = "unrecognized cmd";
                break;
            case CMD_INVALID_CELL: 
                last_status = "invalid cell";
                break;
            case CMD_INVALID_RANGE: 
                last_status = "invalid range";
                break;
            case CMD_CIRCULAR_REF: 
                last_status = "circular ref";
                break;
            case CMD_DIV_BY_ZERO: 
                last_status = "div by zero";
                break;
            default: 
                last_status = "error";
        }
    }

    free_spreadsheet(sheet);
    return 0;
}

// int main(int argc, char* argv[]) {
//     if (argc != 3) {
//         fprintf(stderr, "Usage: %s <rows> <columns>\n", argv[0]);
//         return 1;
//     }

//     int rows = atoi(argv[1]);
//     int cols = atoi(argv[2]);

//     Spreadsheet* sheet = create_spreadsheet(rows, cols);
//     if (!sheet) return 1;

//     char input[256];
//     while (1) {
//         print_spreadsheet(sheet);
//         printf("[0.0] (ok) > ");
//         if (fgets(input, sizeof(input), stdin) == NULL) break;
//         input[strcspn(input, "\n")] = 0; // Remove newline
//         if (strcmp(input, "q") == 0) break;
//         handle_command(sheet, input);
//     }

//     free_spreadsheet(sheet);
//     return 0;
// }