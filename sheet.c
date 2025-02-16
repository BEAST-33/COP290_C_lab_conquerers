/*
  sheet.c - A spreadsheet program that supports cell formulas, dependencies, and basic commands.
  This single-file version integrates dependency tracking and a stub parser.
  See assignment specification and comments for further details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>



#define MAX_ROWS 999
#define MAX_COLS 18278
#define VIEWPORT_SIZE 10

// Enumerated command status codes.
typedef enum {
    CMD_OK,
    CMD_UNRECOGNIZED,
    CMD_INVALID_CELL,
    CMD_INVALID_RANGE,
    CMD_CIRCULAR_REF,
    CMD_DIV_BY_ZERO
} CommandStatus;

// Parser status codes (stub values, extend as needed)
typedef enum {
    PARSER_OK = 0,
    PARSER_CIRCULAR_REF,
    PARSER_SYNTAX_ERROR,
    PARSER_DIV_ZERO,
    PARSER_INVALID_RANGE
} ParserStatus;

/*====================== Data Structures ========================*/

// Each cell stores its value, a formula (if any), an error state, and dependency info.
typedef struct Cell {
    int value;
    char* formula;
    int error_state;
    struct Cell** dependencies;  // cells this cell depends on
    int dep_count;
    struct Cell** dependents;    // cells that depend on this cell
    int dependent_count;
} Cell;

// Spreadsheet structure holds a 2D grid of cells and additional UI info.
typedef struct {
    Cell*** grid;        // 2D array of pointers to cells.
    int rows, cols;
    int viewport_row;    // Starting row index for the printed viewport.
    int viewport_col;    // Starting column index for the printed viewport.
    bool output_enabled; // Controls whether the spreadsheet prints output.
} Spreadsheet;

/*====================== Utility Functions ========================*/

// Converts a one-indexed column number to a column name (e.g., 1->"A", 28->"AB").
char* get_column_name(int col) {
    char *name = malloc(4 * sizeof(char));
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

// Converts a column letter string (e.g., "A", "ZZ") to a 0-based column number.
int column_name_to_number(const char* name) {
    int result = 0;
    for (int i = 0; name[i] != '\0'; i++) {
        result *= 26;
        result += (toupper(name[i]) - 'A' + 1);
    }
    return result - 1;
}

// Parses a cell reference (e.g., "B12") into 0-indexed row and col.
void parse_cell_reference(const char* cell, int* row, int* col) {
    char col_name[4] = {0};
    int i = 0;
    while (cell[i] != '\0' && isupper(cell[i])) {
        if (i >= 3) { *row = *col = -1; return; }
        col_name[i] = cell[i];
        i++;
    }
    if (cell[i] == '\0' || !isdigit(cell[i])) { *row = *col = -1; return; }
    *col = column_name_to_number(col_name);
    *row = atoi(cell + i) - 1;
    while (cell[i] != '\0') {
        if (!isdigit(cell[i])) { *row = *col = -1; return; }
        i++;
    }
}

/*================== Spreadsheet Construction ====================*/

// Creates a new spreadsheet given rows and columns.
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
            sheet->grid[i][j]->dependencies = NULL;
            sheet->grid[i][j]->dep_count = 0;
            sheet->grid[i][j]->dependents = NULL;
            sheet->grid[i][j]->dependent_count = 0;
        }
    }
    return sheet;
}

// Prints the spreadsheet viewport.
void print_spreadsheet(Spreadsheet* sheet) {
    if (!sheet->output_enabled) return;
    int start_row = sheet->viewport_row;
    int start_col = sheet->viewport_col;
    int display_rows = (sheet->rows - start_row < VIEWPORT_SIZE) ? 
                           sheet->rows - start_row : VIEWPORT_SIZE;
    int display_cols = (sheet->cols - start_col < VIEWPORT_SIZE) ? 
                           sheet->cols - start_col : VIEWPORT_SIZE;
    
    // Print header.
    printf("    ");
    for (int j = start_col; j < start_col + display_cols; j++) {
        char* col_name = get_column_name(j + 1);
        printf("%-8s", col_name);
        free(col_name);
    }
    printf("\n");
    
    // Print cells.
    for (int i = start_row; i < start_row + display_rows; i++) {
        printf("%-4d", i + 1);
        for (int j = start_col; j < start_col + display_cols; j++) {
            if (sheet->grid[i][j]->error_state)
                printf("%-8s", "ERR");
            else
                printf("%-8d", sheet->grid[i][j]->value);
        }
        printf("\n");
    }
}

/*================== Scrolling Functions ====================*/

// Scrolls the viewport in the given direction.
void scroll_viewport(Spreadsheet* sheet, char direction) {
    switch (direction) {
        case 'w': // up
            sheet->viewport_row = (sheet->viewport_row >= 10) ? sheet->viewport_row - 10 : 0;
            break;
        case 's': // down
            sheet->viewport_row = (sheet->viewport_row + 10 < sheet->rows - VIEWPORT_SIZE) ?
                sheet->viewport_row + 10 : sheet->rows - VIEWPORT_SIZE;
            break;
        case 'a': // left
            sheet->viewport_col = (sheet->viewport_col >= 10) ? sheet->viewport_col - 10 : 0;
            break;
        case 'd': // right
            sheet->viewport_col = (sheet->viewport_col + 10 < sheet->cols - VIEWPORT_SIZE) ?
                sheet->viewport_col + 10 : sheet->cols - VIEWPORT_SIZE;
            break;
    }
}

// Scroll to a specific cell (e.g., "B3").
CommandStatus scroll_to_cell(Spreadsheet* sheet, const char* cell) {
    int row, col;
    parse_cell_reference(cell, &row, &col);
    if (row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols)
        return CMD_INVALID_CELL;
    sheet->viewport_row = row;
    sheet->viewport_col = col;
    return CMD_OK;
}

/*================== Dependency Management ====================*/

// Adds a dependency: the current cell now depends on the dependency cell.
void add_dependency(Cell* dependent, Cell* dependency) {
    dependency->dependents = realloc(dependency->dependents, 
        (dependency->dependent_count + 1) * sizeof(Cell*));
    dependency->dependents[dependency->dependent_count++] = dependent;
    
    dependent->dependencies = realloc(dependent->dependencies,
        (dependent->dep_count + 1) * sizeof(Cell*));
    dependent->dependencies[dependent->dep_count++] = dependency;
}

// Removes all dependencies from a cell.
void remove_dependencies(Cell* cell) {
    for (int i = 0; i < cell->dep_count; i++) {
        Cell* dep = cell->dependencies[i];
        for (int j = 0; j < dep->dependent_count; j++) {
            if (dep->dependents[j] == cell) {
                dep->dependents[j] = dep->dependents[dep->dependent_count - 1];
                dep->dependent_count--;
                break;
            }
        }
    }
    free(cell->dependencies);
    cell->dependencies = NULL;
    cell->dep_count = 0;
}

// Detects circular dependencies via recursion.
int detect_cycle(Cell* current, Cell* target) {
    if (current == target)
        return 1;
    for (int i = 0; i < current->dep_count; i++) {
        if (detect_cycle(current->dependencies[i], target))
            return 1;
    }
    return 0;
}

/*================== Parser Infrastructure ====================*/

// The parser state for a recursive descent parser.
typedef struct {
    const char* input;     // Input formula string.
    int pos;               // Current position in the string.
    Spreadsheet* sheet;    // Reference to the spreadsheet.
    Cell* current_cell;    // The cell being updated.
    ParserStatus error;    // Parser error status.
} ParserState;

// A stub parser function. Extend this with your parsing logic.
int parse_expression(ParserState* state) {
    // For now, assume the expression parses correctly.
    // In a complete implementation, you would analyze the formula,
    // add dependencies when encountering cell references, etc.
    return PARSER_OK;
}

// Stub function that checks if the formula causes division by zero.
// Replace with your own implementation.
bool contains_division_by_zero(const char* formula) {
    return false;
}

/*================== Calculation and Update ====================*/

// Dummy calculation function: if dependencies exist, returns their sum,
// otherwise converts the formula to an integer.
int calculate_cell(Cell* cell) {
    if (cell->error_state)
        return -1;
    int result = 0;
    if (cell->dep_count > 0) {
        for (int i = 0; i < cell->dep_count; i++) {
            result += cell->dependencies[i]->value;
        }
    } else {
        if (cell->formula != NULL)
            result = atoi(cell->formula);
    }
    if (cell->value != result) {
        cell->value = result;
        // Propagate recalculations to dependents.
        for (int i = 0; i < cell->dependent_count; i++) {
            calculate_cell(cell->dependents[i]);
        }
    }
    return result;
}

// Updates a cell with a new expression (formula) and handles dependency tracking.
CommandStatus update_cell(Spreadsheet* sheet, int row, int col, const char* expr) {
    Cell* cell = sheet->grid[row][col];
    // Clear previous dependencies and clear error.
    remove_dependencies(cell);
    cell->error_state = 0;
    if (cell->formula)
        free(cell->formula);
    cell->formula = strdup(expr);

    // Set up parser state and parse the formula.
    ParserState state = {
        .input = expr,
        .pos = 0,
        .sheet = sheet,
        .current_cell = cell,
        .error = PARSER_OK
    };
    int parse_result = parse_expression(&state);
    if (parse_result != PARSER_OK) {
        cell->error_state = 1;
        return (parse_result == PARSER_CIRCULAR_REF) ? CMD_CIRCULAR_REF : CMD_UNRECOGNIZED;
    }
    if (contains_division_by_zero(expr)) {
        cell->error_state = 1;
        return CMD_DIV_BY_ZERO;
    }
    // In a full version, during parsing, add any cell dependencies.
    calculate_cell(cell);
    return CMD_OK;
}

// set_cell_value now delegates to update_cell.
CommandStatus set_cell_value(Spreadsheet* sheet, int row, int col, const char* expr) {
    CommandStatus status = update_cell(sheet, row, col, expr);
    if (status != CMD_OK)
        sheet->grid[row][col]->error_state = 1;
    return status;
}

/*================== Command Handling ====================*/

// Processes a command entered by the user.
CommandStatus handle_command(Spreadsheet* sheet, const char* cmd) {
    if (strcmp(cmd, "disable_output") == 0) {
        sheet->output_enabled = false;
        return CMD_OK;
    } else if (strcmp(cmd, "enable_output") == 0) {
        sheet->output_enabled = true;
        return CMD_OK;
    } else if (strncmp(cmd, "scroll_to ", 10) == 0) {
        return scroll_to_cell(sheet, cmd + 10);
    } else if (strlen(cmd) == 1 && strchr("wasd", cmd[0])) {
        scroll_viewport(sheet, cmd[0]);
        return CMD_OK;
    }
    // Process cell assignments (e.g., A1=3 or B2=A1+1).
    char* eq = strchr(cmd, '=');
    if (eq != NULL) {
        char cell_ref[10] = {0};
        strncpy(cell_ref, cmd, eq - cmd);
        cell_ref[eq - cmd] = '\0';
        int row, col;
        parse_cell_reference(cell_ref, &row, &col);
        if (row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols)
            return CMD_INVALID_CELL;
        const char* expr = eq + 1;
        return set_cell_value(sheet, row, col, expr);
    }
    return CMD_UNRECOGNIZED;
}

/*================== Freeing Resources ====================*/

// Frees memory allocated for the spreadsheet.
void free_spreadsheet(Spreadsheet* sheet) {
    for (int i = 0; i < sheet->rows; i++) {
        for (int j = 0; j < sheet->cols; j++) {
            free(sheet->grid[i][j]->formula);
            free(sheet->grid[i][j]->dependencies);
            free(sheet->grid[i][j]->dependents);
            free(sheet->grid[i][j]);
        }
        free(sheet->grid[i]);
    }
    free(sheet->grid);
    free(sheet);
}

/*================== Main Function ====================*/

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <rows> <columns>\n", argv[0]);
        return 1;
    }
    Spreadsheet* sheet = create_spreadsheet(atoi(argv[1]), atoi(argv[2]));
    if (!sheet)
        return 1;

    char input[256];
    double last_time = 0.0;
    const char* last_status = "ok";
    clock_t start, end;

    while (1) {
        print_spreadsheet(sheet);
        printf("[%.1f] (%s) > ", last_time, last_status);
        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\n")] = 0; // Remove trailing newline.
        if (strcmp(input, "q") == 0)
            break;
        start = clock();
        CommandStatus status = handle_command(sheet, input);
        end = clock();
        last_time = (double)(end - start) / CLOCKS_PER_SEC;
        switch (status) {
            case CMD_OK: last_status = "ok"; break;
            case CMD_UNRECOGNIZED: last_status = "unrecognized cmd"; break;
            case CMD_INVALID_CELL: last_status = "invalid cell"; break;
            case CMD_INVALID_RANGE: last_status = "invalid range"; break;
            case CMD_CIRCULAR_REF: last_status = "circular ref"; break;
            case CMD_DIV_BY_ZERO: last_status = "div by zero"; break;
            default: last_status = "error"; break;
        }
    }
    free_spreadsheet(sheet);
    return 0;
}
