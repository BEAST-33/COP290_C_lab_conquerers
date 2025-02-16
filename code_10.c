/*


  This program implements a spreadsheet with dependency tracking, formula parsing,
  and functions including arithmetic, MIN, MAX, SUM, AVG, and SLEEP. SLEEP(n)
  sleeps the program for n seconds and returns n. The program also measures and prints
  real (wall-clock) elapsed time for each command.

 
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>         // For clock_gettime
#include <limits.h>       // For INT_MAX and INT_MIN
#include <pthread.h>      // For threading
#include <unistd.h>       // For sleep
#include <stdbool.h>

#define MAX_ROWS 999
#define MAX_COLS 18278
#define VIEWPORT_SIZE 10

// Command status codes.
typedef enum {
    CMD_OK,
    CMD_UNRECOGNIZED,
    CMD_INVALID_CELL,
    CMD_INVALID_RANGE,
    CMD_CIRCULAR_REF,
    CMD_DIV_BY_ZERO,
    CMD_RANGE_ERROR
} CommandStatus;

// Forward declarations
struct Cell;
typedef struct Cell Cell;
typedef struct Spreadsheet Spreadsheet;
typedef struct Range Range;

void add_dependency(Cell* dependent, Cell* dependency);
void remove_dependencies(Cell* cell);
int detect_cycle(Cell* current, Cell* target);
void recalculate_dependencies_topological(Spreadsheet* sheet);
CommandStatus evaluate_arithmetic(Spreadsheet* sheet, Cell* cell, const char* expr);
CommandStatus evaluate_function(Spreadsheet* sheet, Cell* cell, const char* expr);
CommandStatus evaluate_sleep(Spreadsheet* sheet, Cell* cell, const char* expr);
int evaluate_cell_reference(Spreadsheet* sheet, const char* token, Cell* current_cell);
CommandStatus set_cell_value(Spreadsheet* sheet, int row, int col, const char* expr) ;

// Cell structure with dependency tracking.
struct Cell {
    int value;
    char* formula;
    int error_state;
    Cell** dependencies;
    int dep_count;
    Cell** dependents;
    int dependent_count;
    int visited;
};

struct Spreadsheet {
    Cell*** grid;
    int rows;
    int cols;
    int viewport_row;
    int viewport_col;
    bool output_enabled;
};

// Range structure (for functions operating on ranges).
struct Range {
    int start_row;
    int start_col;
    int end_row;
    int end_col;
};

/*------------------- Utility Functions -------------------*/

// Convert 1-indexed column number to a column name (e.g., 1 -> "A", 28 -> "AB").
char* get_column_name(int col) {
    char* name = malloc(4 * sizeof(char));  // Up to 3 letters + null.
    int i = 0;
    while (col > 0) {
        name[i++] = 'A' + (col - 1) % 26;
        col = (col - 1) / 26;
    }
    name[i] = '\0';
    // Reverse the string.
    for (int j = 0; j < i / 2; j++) {
        char temp = name[j];
        name[j] = name[i - 1 - j];
        name[i - 1 - j] = temp;
    }
    return name;
}

// Convert column name (e.g., "A", "ZZ") to 0-based column number.
int column_name_to_number(const char* name) {
    int result = 0;
    for (int i = 0; name[i] != '\0'; i++) {
        result *= 26;
        result += (toupper(name[i]) - 'A' + 1);
    }
    return result - 1;
}

// Parse a cell reference (e.g., "B12") into 0-indexed row and col.
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

/*------------------- Spreadsheet Construction -------------------*/

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

void print_spreadsheet(Spreadsheet* sheet) {
    if (!sheet->output_enabled) return;
    int start_row = sheet->viewport_row;
    int start_col = sheet->viewport_col;
    int display_rows = (sheet->rows - start_row < VIEWPORT_SIZE) ? 
                       sheet->rows - start_row : VIEWPORT_SIZE;
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
            if (sheet->grid[i][j]->error_state)
                printf("%-8s", "ERR");
            else
                printf("%-8d", sheet->grid[i][j]->value);
        }
        printf("\n");
    }
}

/*------------------- Scrolling -------------------*/

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

CommandStatus scroll_to_cell(Spreadsheet* sheet, const char* cell) {
    int row, col;
    parse_cell_reference(cell, &row, &col);
    if (row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols)
        return CMD_INVALID_CELL;
    sheet->viewport_row = row;
    sheet->viewport_col = col;
    return CMD_OK;
}

/*------------------- Dependency Management -------------------*/
int detect_cycle(Cell* current, Cell* target) {
    if (current == target)
        return 1;
    for (int i = 0; i < current->dep_count; i++) {
        if (detect_cycle(current->dependencies[i], target))
            return 1;
    }
    return 0;
}
void add_dependency(Cell* dependent, Cell* dependency) {
    dependency->dependents = realloc(dependency->dependents, (dependency->dependent_count + 1) * sizeof(Cell*));
    dependency->dependents[dependency->dependent_count++] = dependent;
}

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

/*------------------- Topological Sorting for Dependency Evaluation -------------------*/
void dfs(Cell* cell, Cell** sorted, int* index) {
    if (cell->visited) return;
    cell->visited = 1;
    
    for (int i = 0; i < cell->dep_count; i++) {
        dfs(cell->dependencies[i], sorted, index);
    }
    
    sorted[(*index)++] = cell;
}

void recalculate_dependencies_topological(Spreadsheet* sheet) {
    Cell** sorted = malloc(sheet->rows * sheet->cols * sizeof(Cell*));
    int index = 0;

    for (int i = 0; i < sheet->rows; i++) {
        for (int j = 0; j < sheet->cols; j++) {
            sheet->grid[i][j]->visited = 0;
        }
    }

    for (int i = 0; i < sheet->rows; i++) {
        for (int j = 0; j < sheet->cols; j++) {
            if (!sheet->grid[i][j]->visited) {
                dfs(sheet->grid[i][j], sorted, &index);
            }
        }
    }

    for (int i = index - 1; i >= 0; i--) {
        Cell* cell = sorted[i];
        if (cell->formula != NULL) {  // Prevent NULL dereference
            set_cell_value(sheet, i / sheet->cols, i % sheet->cols, cell->formula);
        }
    }

    free(sorted);
}
/*------------------- Cell Evaluation -------------------*/


/*------------------- Range Parsing and Evaluation -------------------*/

CommandStatus parse_range(const char* range_str, Range* range) {
    char* colon = strchr(range_str, ':');
    if (!colon || colon == range_str || colon[1] == '\0')
        return CMD_INVALID_RANGE;
    char start_cell[20], end_cell[20];
    int len = colon - range_str;
    strncpy(start_cell, range_str, len);
    start_cell[len] = '\0';
    strcpy(end_cell, colon + 1);
    parse_cell_reference(start_cell, &range->start_row, &range->start_col);
    if (range->start_row < 0 || range->start_col < 0)
        return CMD_INVALID_CELL;
    parse_cell_reference(end_cell, &range->end_row, &range->end_col);
    if (range->end_row < 0 || range->end_col < 0)
        return CMD_INVALID_CELL;
    if (range->start_row > range->end_row || range->start_col > range->end_col)
        return CMD_INVALID_RANGE;
    return CMD_OK;
}

CommandStatus evaluate_range(Spreadsheet* sheet, Range range, int* result, bool is_min) {
    int cur = is_min ? INT_MAX : INT_MIN;
    bool error_found = false;
    for (int i = range.start_row; i <= range.end_row; i++) {
        for (int j = range.start_col; j <= range.end_col; j++) {
            if (i >= sheet->rows || j >= sheet->cols)
                return CMD_INVALID_RANGE;
            if (sheet->grid[i][j]->error_state) {
                error_found = true;
                continue;
            }
            int val = sheet->grid[i][j]->value;
            if (is_min)
                cur = (val < cur) ? val : cur;
            else
                cur = (val > cur) ? val : cur;
        }
    }
    if (error_found)
        return CMD_RANGE_ERROR;
    *result = cur;
    return CMD_OK;
}

CommandStatus evaluate_sum_and_count(Spreadsheet* sheet, Range range, int* sum, int* count) {
    *sum = 0;
    *count = 0;
    bool error_found = false;
    for (int i = range.start_row; i <= range.end_row; i++) {
        for (int j = range.start_col; j <= range.end_col; j++) {
            if (i >= sheet->rows || j >= sheet->cols)
                return CMD_INVALID_RANGE;
            if (sheet->grid[i][j]->error_state) {
                error_found = true;
                continue;
            }
            *sum += sheet->grid[i][j]->value;
            (*count)++;
        }
    }
    if (error_found)
        return CMD_RANGE_ERROR;
    return CMD_OK;
}

/*------------------- Arithmetic and Cell Reference Evaluation -------------------*/

// Evaluates a token that can be either an integer or a cell reference.
int evaluate_cell_reference(Spreadsheet* sheet, const char* token, Cell* current_cell) {
    int ref_row, ref_col;
    sscanf(token, "%d:%d", &ref_row, &ref_col);

    if (ref_row < 0 || ref_col < 0 || ref_row >= sheet->rows || ref_col >= sheet->cols) {
        return -1;
    }

    Cell* ref_cell = sheet->grid[ref_row][ref_col];
    add_dependency(current_cell, ref_cell);
    return ref_cell->value;
}

// Evaluates a binary arithmetic expression (supports a single operation).
CommandStatus evaluate_arithmetic(Spreadsheet* sheet, Cell* cell, const char* expr) {
    char first[256], second[256];
    char op;
    int pos = 0;
    while (expr[pos] && !strchr("+-*/", expr[pos])) {
        first[pos] = expr[pos];
        pos++;
    }
    first[pos] = '\0';
    if (!expr[pos])
        return CMD_UNRECOGNIZED;
    op = expr[pos++];
    strcpy(second, expr + pos);
    int val1 = isalpha(first[0]) ? evaluate_cell_reference(sheet, first, cell) : atoi(first);
    int val2 = isalpha(second[0]) ? evaluate_cell_reference(sheet, second, cell) : atoi(second);
    switch(op) {
        case '+': cell->value = val1 + val2; break;
        case '-': cell->value = val1 - val2; break;
        case '*': cell->value = val1 * val2; break;
        case '/':
            if (val2 == 0) {
                cell->error_state = 1;
                return CMD_DIV_BY_ZERO;
            }
            cell->value = val1 / val2;
            break;
        default: return CMD_UNRECOGNIZED;
    }
    cell->error_state = 0;
    return CMD_OK;
}

/*------------------- Function Evaluation -------------------*/

// Evaluates functions: MIN, MAX, SUM, AVG, SLEEP.
CommandStatus evaluate_function(Spreadsheet* sheet, Cell* cell, const char* expr) {
    int len = strlen(expr);
    if (len < 6)
        return CMD_UNRECOGNIZED;
    // Handle MIN/MAX functions.
    if (strncmp(expr, "MIN(", 4) == 0 || strncmp(expr, "MAX(", 4) == 0) {
        bool is_min = (expr[0]=='M' && expr[1]=='I');
        char range_str[100];
        strncpy(range_str, expr + 4, len - 5);
        range_str[len - 5] = '\0';
        Range range;
        CommandStatus status = parse_range(range_str, &range);
        if (status != CMD_OK)
            return status;
        for (int i = range.start_row; i <= range.end_row; i++) {
            for (int j = range.start_col; j <= range.end_col; j++) {
                Cell* dep = sheet->grid[i][j];
                if (detect_cycle(dep, cell)) {
                    cell->error_state = 1;
                    return CMD_CIRCULAR_REF;
                }
                add_dependency(cell, dep);
            }
        }
        int result;
        status = evaluate_range(sheet, range, &result, is_min);
        if (status == CMD_OK) {
            cell->value = result;
            cell->error_state = 0;
        } else {
            cell->error_state = 1;
        }
        return status;
    }
    // Handle SUM/AVG functions.
    if (strncmp(expr, "SUM(", 4) == 0 || strncmp(expr, "AVG(", 4) == 0) {
        bool is_sum = (expr[0]=='S');
        char range_str[100];
        strncpy(range_str, expr + 4, len - 5);
        range_str[len - 5] = '\0';
        Range range;
        CommandStatus status = parse_range(range_str, &range);
        if (status != CMD_OK)
            return status;
        for (int i = range.start_row; i <= range.end_row; i++) {
            for (int j = range.start_col; j <= range.end_col; j++) {
                Cell* dep = sheet->grid[i][j];
                if (detect_cycle(dep, cell)) {
                    cell->error_state = 1;
                    return CMD_CIRCULAR_REF;
                }
                add_dependency(cell, dep);
            }
        }
        int total, count;
        status = evaluate_sum_and_count(sheet, range, &total, &count);
        if (status == CMD_OK) {
            cell->value = is_sum ? total : (count ? total / count : 0);
            cell->error_state = 0;
        } else {
            cell->error_state = 1;
        }
        return status;
    }
    // Handle SLEEP function.
    if (strncmp(expr, "SLEEP(", 6) == 0) {
        return evaluate_sleep(sheet, cell, expr);
    }
    return CMD_UNRECOGNIZED;
}

/*------------------- SLEEP Function -------------------*/

void* sleep_wrapper(void* arg) {
    int duration = *((int*)arg);
    sleep(duration);
    return NULL;
}

CommandStatus evaluate_sleep(Spreadsheet* sheet, Cell* cell, const char* expr) {
    char token[256];
    sscanf(expr, "SLEEP(%255[^)])", token);
    
    int duration = evaluate_cell_reference(sheet, token, cell);
    if (duration < 0) return CMD_INVALID_CELL;

    pthread_t thread;
    int* arg = malloc(sizeof(int));
    *arg = duration;
    pthread_create(&thread, NULL, sleep_wrapper, arg);
    pthread_join(thread, NULL);
    free(arg);

    cell->value = duration;
    return CMD_OK;
}

/*------------------- set_cell_value -------------------*/
CommandStatus set_cell_value(Spreadsheet* sheet, int row, int col, const char* expr) {
    Cell* cell = sheet->grid[row][col];
    remove_dependencies(cell);  // Clear old dependencies
    cell->error_state = 0;
    
    // Free old formula memory before assigning a new one
    free(cell->formula);
    cell->formula = strdup(expr);

    // Handle constant values (e.g., "42")
    char* end;
    long value = strtol(expr, &end, 10);
    if (*end == '\0') {
        cell->value = value;
        return CMD_OK;
    }

    // Handle functions (SUM, AVG, MIN, MAX, SLEEP)
    if (strncmp(expr, "SUM(", 4) == 0 || strncmp(expr, "AVG(", 4) == 0 ||
        strncmp(expr, "MIN(", 4) == 0 || strncmp(expr, "MAX(", 4) == 0 ||
        strncmp(expr, "SLEEP(", 6) == 0) {
        return evaluate_function(sheet, cell, expr);
    }

    // Handle direct cell references (e.g., "B1")
    if (isalpha(expr[0])) {
        int ref_row, ref_col;
        parse_cell_reference(expr, &ref_row, &ref_col);
        if (ref_row >= 0 && ref_col >= 0 && ref_row < sheet->rows && ref_col < sheet->cols) {
            Cell* dep = sheet->grid[ref_row][ref_col];
            if (detect_cycle(dep, cell)) {
                cell->error_state = 1;
                return CMD_CIRCULAR_REF;
            }
            add_dependency(cell, dep);
            cell->value = dep->value;
            return CMD_OK;
        }
    }

    // Trim spaces before evaluating arithmetic
    while (isspace(*expr)) expr++;

    // Handle arithmetic expressions (e.g., "A1+2", "B2*C3")
    if (strchr(expr, '+') || strchr(expr, '-') || strchr(expr, '*') || strchr(expr, '/')) {
        return evaluate_arithmetic(sheet, cell, expr);
    }

    return CMD_UNRECOGNIZED;
}
/*------------------- Command Handling -------------------*/

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
    // Process cell assignments.
    char* eq = strchr(cmd, '=');
    if (eq != NULL) {
        char cell_ref[10];
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

/*------------------- Cleanup -------------------*/

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

/*------------------- Main -------------------*/


int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <rows> <cols>\n", argv[0]);
        return 1;
    }
    Spreadsheet* sheet = malloc(sizeof(Spreadsheet));
    sheet->rows = atoi(argv[1]);
    sheet->cols = atoi(argv[2]);
    sheet->grid = malloc(sheet->rows * sizeof(Cell**));

    for (int i = 0; i < sheet->rows; i++) {
        sheet->grid[i] = malloc(sheet->cols * sizeof(Cell*));
        for (int j = 0; j < sheet->cols; j++) {
            sheet->grid[i][j] = calloc(1, sizeof(Cell));
        }
    }

    char input[256];
    struct timespec start, finish;
    while (1) {
        printf("> ");
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "q") == 0) break;

        clock_gettime(CLOCK_REALTIME, &start);
        CommandStatus status = CMD_OK;

        char cell_ref[10], expr[256];
        if (sscanf(input, "%9[^=]=%255s", cell_ref, expr) == 2) {
            int row, col;
            sscanf(cell_ref, "%d:%d", &row, &col);
            if (row < 0 || col < 0 || row >= sheet->rows || col >= sheet->cols) {
                status = CMD_INVALID_CELL;
            } else {
                status = set_cell_value(sheet, row, col, expr);
            }
        } else {
            status = CMD_UNRECOGNIZED;
        }

        clock_gettime(CLOCK_REALTIME, &finish);
        double elapsed_time = (finish.tv_sec - start.tv_sec) + (finish.tv_nsec - start.tv_nsec) / 1e9;
        printf("[%.1f] (%s)\n", elapsed_time, status == CMD_OK ? "ok" : "error");

        recalculate_dependencies_topological(sheet);
    }

    for (int i = 0; i < sheet->rows; i++) {
        for (int j = 0; j < sheet->cols; j++) {
            free(sheet->grid[i][j]);
        }
        free(sheet->grid[i]);
    }
    free(sheet->grid);
    free(sheet);

    return 0;
}
