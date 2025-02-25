// Version 12
// Improvements over code_7.c:


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>  // Standard C timing
#include <limits.h> // For INT_MAX and INT_MIN
#include <unistd.h> // For sleep function

#define MAX_ROWS 999
#define MAX_COLS 18278
#define VIEWPORT_SIZE 10

// Add status enumeration
// Added these new error statuses to the enum
// version 2
typedef enum {
    CMD_OK,
    CMD_UNRECOGNIZED,
    CMD_INVALID_CELL,
    CMD_INVALID_RANGE,
    CMD_CIRCULAR_REF,
    CMD_DIV_BY_ZERO,
    CMD_RANGE_ERROR
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

// Add range parsing and validation functions
typedef struct {
    int start_row;
    int start_col;
    int end_row;
    int end_col;
} Range;

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
            *row = *col = -1;
            return; // Invalid cell reference
        }
        col_name[i] = cell[i];
        i++;
    }

    // Validate row number (must be digits)
    if (cell[i] == '\0' || !isdigit(cell[i])) {
        *row = *col = -1;
        return; // Invalid cell reference
    }

    *col = column_name_to_number(col_name);
    *row = atoi(cell + i) - 1;

    // Check for invalid characters after row number
    while (cell[i] != '\0') {
        if (!isdigit(cell[i])) {
            *row = *col = -1;
            return; // Invalid characters after row number
        }
        i++;
    }
}

// Function to parse an operator
// version 1
int parse_operator(const char* expr, int* a, int* b, char* op) {
    char* end;
    *a = strtol(expr, &end, 10);
    
    if(end == expr) return 0; // Not a number
    if(*end == '\0') return 1; // Single number
    
    *op = *end;
    *b = strtol(end+1, &end, 10);
    return (*end == '\0') ? 2 : 0;
}

// Function to scroll to a cell
// version 5
CommandStatus scroll_to_cell(Spreadsheet* sheet, const char* cell) {
    int row, col;
    parse_cell_reference(cell, &row, &col);
    if (row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols) {
        return CMD_INVALID_CELL;
    }
    sheet->viewport_row = row;
    sheet->viewport_col = col;
    return CMD_OK;
}

// Function to scroll the viewport
void scroll_viewport(Spreadsheet* sheet, char direction) {
    switch (direction) {
        case 'w': // up
            sheet->viewport_row = (sheet->viewport_row > 10) ? sheet->viewport_row - 10 : 0;
            break;
        case 's': // down
            sheet->viewport_row = (sheet->viewport_row + 10 < sheet->rows - VIEWPORT_SIZE) ? 
                                 sheet->viewport_row + 10 : sheet->rows - VIEWPORT_SIZE;
            break;
        case 'a': // left 
            sheet->viewport_col = (sheet->viewport_col > 10) ? sheet->viewport_col - 10 : 0;
            break;
        case 'd': // right
            sheet->viewport_col = (sheet->viewport_col + 10 < sheet->cols - VIEWPORT_SIZE) ? 
                                 sheet->viewport_col + 10 : sheet->cols - VIEWPORT_SIZE;
            break;
    }
}

// // Function to evaluate an expression
// int eval_expression(Spreadsheet* sheet, int row, int col, const char* expr) {
//     // Implement expression evaluation
//     return 0; // Placeholder
// }

// // Function to update dependencies
// void update_dependencies(Spreadsheet* sheet, int row, int col) {
//     // Implement dependency graph updates
// }

// Function to parse a range
CommandStatus parse_range(const char* range_str, Range* range) {
    char* colon = strchr(range_str, ':');
    if (!colon || colon == range_str || colon[1] == '\0') {
        return CMD_INVALID_RANGE;
    }

    char start_cell[20];
    char end_cell[20];
    strncpy(start_cell, range_str, colon - range_str);
    start_cell[colon - range_str] = '\0';
    strcpy(end_cell, colon + 1);

    // Parse start cell
    parse_cell_reference(start_cell, &range->start_row, &range->start_col);
    if (range->start_row < 0 || range->start_col < 0) {
        return CMD_INVALID_CELL;
    }

    // Parse end cell
    parse_cell_reference(end_cell, &range->end_row, &range->end_col);
    if (range->end_row < 0 || range->end_col < 0) {
        return CMD_INVALID_CELL;
    }

    // Validate range order
    if (range->start_row > range->end_row || range->start_col > range->end_col) {
        return CMD_INVALID_RANGE;
    }

    return CMD_OK;
}

// Function to evaluate a range
CommandStatus evaluate_range(Spreadsheet* sheet, Range range, int* result, bool is_min) {
    int min_max = is_min ? INT_MAX : INT_MIN;
    bool error_found = false;

    for (int row = range.start_row; row <= range.end_row; row++) {
        for (int col = range.start_col; col <= range.end_col; col++) {
            if (row >= sheet->rows || col >= sheet->cols) {
                return CMD_INVALID_RANGE;
            }

            if (sheet->grid[row][col]->error_state) {
                error_found = true;
                break;
            }

            int val = sheet->grid[row][col]->value;
            if (is_min) {
                min_max = (val < min_max) ? val : min_max;
            } else {
                min_max = (val > min_max) ? val : min_max;
            }
        }
        if (error_found) break;
    }

    if (error_found) {
        return CMD_RANGE_ERROR;
    }

    *result = min_max;
    return CMD_OK;
}

// Function to evaluate sum and count of a range
CommandStatus evaluate_sum_and_count(Spreadsheet* sheet, Range range, int* sum, int* count) {
    *sum = 0;
    *count = 0;
    bool error_found = false;

    for (int row = range.start_row; row <= range.end_row; row++) {
        for (int col = range.start_col; col <= range.end_col; col++) {
            if (row >= sheet->rows || col >= sheet->cols) {
                return CMD_INVALID_RANGE;
            }

            if (sheet->grid[row][col]->error_state) {
                error_found = true;
                break;
            }

            *sum += sheet->grid[row][col]->value;
            (*count)++;
        }
        if (error_found) break;
    }

    if (error_found) {
        return CMD_RANGE_ERROR;
    }

    return CMD_OK;
}

CommandStatus handle_sleep(Spreadsheet* sheet, int row, int col, const char* expr) {
    // Remove "SLEEP(" from the beginning and ")" from the end
    char* sleep_arg = malloc(strlen(expr) - 6);
    strncpy(sleep_arg, expr + 6, strlen(expr) - 7);
    sleep_arg[strlen(expr) - 7] = '\0';

    int sleep_time;
    
    // Check if the argument is a cell reference or a number
    if (isalpha(sleep_arg[0])) {
        int sleep_row, sleep_col;
        parse_cell_reference(sleep_arg, &sleep_row, &sleep_col);
        
        if (sleep_row < 0 || sleep_row >= sheet->rows || sleep_col < 0 || sleep_col >= sheet->cols) {
            free(sleep_arg);
            return CMD_INVALID_CELL;
        }

        if (sheet->grid[sleep_row][sleep_col]->error_state) {
            sheet->grid[row][col]->error_state = 1;
            free(sleep_arg);
            return CMD_OK; // We return OK because we've handled the ERR case
        }

        sleep_time = sheet->grid[sleep_row][sleep_col]->value;
    } else {
        sleep_time = atoi(sleep_arg);
    }

    free(sleep_arg);

    if (sleep_time < 0) {
        sheet->grid[row][col]->value = sleep_time;
        return CMD_OK;
    }

    sleep(sleep_time);
    sheet->grid[row][col]->value = sleep_time;
    return CMD_OK;
}


// Function to set cell value
// version 1
CommandStatus set_cell_value(Spreadsheet* sheet, int row, int col, const char* expr) {
    // Handle constant value
    char* end;
    long value = strtol(expr, &end, 10);
    
    if(*end == '\0') { // Simple constant
        sheet->grid[row][col]->value = value;
        sheet->grid[row][col]->error_state = 0;
        return CMD_OK;
    }
    
    // Handle SUM and AVG functions
    else if (strncmp(expr, "SUM(", 4) == 0 || strncmp(expr, "AVG(", 4) == 0) {
        bool is_sum = (expr[0] == 'S');
        char range_str[100];
        strncpy(range_str, expr + 4, strlen(expr) - 5);
        range_str[strlen(expr) - 5] = '\0';

        Range range;
        CommandStatus status = parse_range(range_str, &range);
        if (status != CMD_OK) {
            return status;
        }

        int total, count;
        status = evaluate_sum_and_count(sheet, range, &total, &count);
        if (status == CMD_OK) {
            if (is_sum) {
                sheet->grid[row][col]->value = total;
            } else {
                sheet->grid[row][col]->value = total / count;
            }
            sheet->grid[row][col]->error_state = 0;
        } else {
            sheet->grid[row][col]->error_state = 1;
            return status;
        }
        return CMD_OK;
    }

    // Handle MIN and MAX functions
    else if (strncmp(expr, "MIN(", 4) == 0 || strncmp(expr, "MAX(", 4) == 0) {
        bool is_min = (expr[0] == 'M' && expr[1] == 'I');
        char range_str[100];
        strncpy(range_str, expr + 4, strlen(expr) - 5); // Remove MIN( or MAX( and )
        range_str[strlen(expr) - 5] = '\0';

        Range range;
        CommandStatus status = parse_range(range_str, &range);
        if (status != CMD_OK) {
            return status;
        }

        int result;
        status = evaluate_range(sheet, range, &result, is_min);
        if (status == CMD_OK) {
            sheet->grid[row][col]->value = result;
            sheet->grid[row][col]->error_state = 0;
        } else if (status == CMD_RANGE_ERROR) {
            sheet->grid[row][col]->error_state = 1;
            return CMD_RANGE_ERROR;
        }
            return status;
    }
        // Handle SLEEP function
    else if (strncmp(expr, "SLEEP(", 6) == 0) {
        return handle_sleep(sheet, row, col, expr);
    }

    // Handle arithmetic operations
    int a, b;
    char op;
    int components = parse_operator(expr, &a, &b, &op);
    
    if(components == 2) { // Valid binary operation
        switch(op) {
            case '+': 
                sheet->grid[row][col]->value = a + b;
                break;
            case '-':
                sheet->grid[row][col]->value = a - b;
                break;
            case '*':
                sheet->grid[row][col]->value = a * b;
                break;
            case '/':
                if(b == 0) {
                    sheet->grid[row][col]->error_state = 1;
                    return CMD_DIV_BY_ZERO;
                }
                sheet->grid[row][col]->value = a / b;
                break;
            default:
                return CMD_UNRECOGNIZED;
        }
        sheet->grid[row][col]->error_state = 0;
        return CMD_OK;
    }
    
    return CMD_UNRECOGNIZED;
}

// Function to handle commands
// version 4
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
        return scroll_to_cell(sheet, cmd + 10);
    }
    
    // Handle cell assignments
    char* eq = strchr(cmd, '=');
    if(eq != NULL) {
        // Split into cell and expression
        char cell_ref[10];
        strncpy(cell_ref, cmd, eq - cmd);
        cell_ref[eq - cmd] = '\0';
        
        int row, col;
        parse_cell_reference(cell_ref, &row, &col);
        
        if(row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols) {
            return CMD_INVALID_CELL;
        }
        
        const char* expr = eq + 1;
        return set_cell_value(sheet, row, col, expr);
    }

    return CMD_UNRECOGNIZED;
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
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <rows> <columns>\n", argv[0]);
        return 1;
    }

    Spreadsheet* sheet = create_spreadsheet(atoi(argv[1]), atoi(argv[2]));
    if (!sheet) return 1;

    char input[256];
    double last_time = 0.0;
    const char* last_status = "ok";
    clock_t start, end;

// Version 2
    while (1) {
        print_spreadsheet(sheet);
        printf("[%.1f] (%s) > ", last_time, last_status);
        
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        if (strcmp(input, "q") == 0) break;

        // Measure elapsed time
        start = clock();
        CommandStatus status = handle_command(sheet, input);
        end = clock();

        // Calculate elapsed time
        last_time = (double)(end - start) / CLOCKS_PER_SEC;
        
        // Determine status message
        switch (status) {
            case CMD_OK: last_status = "ok"; break;
            case CMD_UNRECOGNIZED: last_status = "unrecognized cmd"; break;
            case CMD_INVALID_CELL: last_status = "invalid cell"; break;
            case CMD_INVALID_RANGE: last_status = "invalid range"; break;
            case CMD_CIRCULAR_REF: last_status = "circular ref"; break;
            case CMD_DIV_BY_ZERO: last_status = "div by zero"; break;
            case CMD_RANGE_ERROR: last_status = "range error"; break;
            default: last_status = "error";
        }
    }
    // while (1) {
    //     print_spreadsheet(sheet);
    //     printf("[%.1f] (%s) > ", last_time, last_status);
        
    //     if (!fgets(input, sizeof(input), stdin)) break;
    //     input[strcspn(input, "\n")] = 0;
    //     if (strcmp(input, "q") == 0) break;

    //     // Measure elapsed time
    //     start = clock();
    //     CommandStatus status = handle_command(sheet, input);
    //     end = clock();

    //     // Calculate elapsed time
    //     last_time = (double)(end - start) / CLOCKS_PER_SEC;
        
    //     // Determine status message
    //     switch (status) {
    //         case CMD_OK: last_status = "ok"; break;
    //         case CMD_UNRECOGNIZED: last_status = "unrecognized cmd"; break;
    //         case CMD_INVALID_CELL: last_status = "invalid cell"; break;
    //         case CMD_INVALID_RANGE: last_status = "invalid range"; break;
    //         case CMD_CIRCULAR_REF: last_status = "circular ref"; break;
    //         case CMD_DIV_BY_ZERO: last_status = "div by zero"; break;
    //         case CMD_RANGE_ERROR: last_status = "range error"; break;
    //         default: last_status = "error";
    //     }
    // }

    // Free the spreadsheet
    free_spreadsheet(sheet);
    return 0;
}