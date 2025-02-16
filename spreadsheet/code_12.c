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
CommandStatus evaluate_arithmetic(Spreadsheet* sheet, Cell* cell, const char* expr);
CommandStatus evaluate_function(Spreadsheet* sheet, Cell* cell, const char* expr);
CommandStatus evaluate_sleep(Spreadsheet* sheet, Cell* cell, const char* expr);
int evaluate_cell_reference(Spreadsheet* sheet, const char* token, Cell* current_cell);

// Cell structure with dependency tracking.
struct Cell {
    int row;
    int col;
    int value;
    char* formula;
    int error_state;       // 0 = OK, 1 = error.
    Cell** dependencies;   // Cells this cell depends on.
    int dep_count;
    Cell** dependents;     // Cells that depend on this cell.
    int dependent_count;
};

// Spreadsheet structure.
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
            sheet->grid[i][j]->row = i;
            sheet->grid[i][j]->col = j;
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

void add_dependency(Cell* dependent, Cell* dependency) {
    dependency->dependents = realloc(dependency->dependents,
        (dependency->dependent_count + 1) * sizeof(Cell*));
    dependency->dependents[dependency->dependent_count++] = dependent;
    dependent->dependencies = realloc(dependent->dependencies,
        (dependent->dep_count + 1) * sizeof(Cell*));
    dependent->dependencies[dependent->dep_count++] = dependency;
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

int detect_cycle(Cell* current, Cell* target) {
    if (current == target)
        return 1;
    for (int i = 0; i < current->dep_count; i++) {
        if (detect_cycle(current->dependencies[i], target))
            return 1;
    }
    return 0;
}

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
    char* endptr;
    int value = strtol(token, &endptr, 10);
    if (*endptr == '\0') return value;

    int ref_row, ref_col;
    parse_cell_reference(token, &ref_row, &ref_col);
    if (ref_row < 0 || ref_col < 0 || ref_row >= sheet->rows || ref_col >= sheet->cols) {
        current_cell->error_state = 1; // Mark error for invalid reference
        return 0;
    }

    Cell* ref_cell = sheet->grid[ref_row][ref_col];
    if (detect_cycle(ref_cell, current_cell)) {
        current_cell->error_state = 1;
        return 0;
    }

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
    int val1 = evaluate_cell_reference(sheet, first, cell);
    if (cell->error_state) return CMD_CIRCULAR_REF;
    int val2 = evaluate_cell_reference(sheet, second, cell);
    if (cell->error_state) return CMD_CIRCULAR_REF;
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

// Sleep wrapper for pthread.
void* sleep_wrapper(void* arg) {
    int duration = *((int*)arg);
    sleep(duration);
    return NULL;
}

// Evaluates SLEEP(n): sleeps for n seconds (using a separate thread) and returns n.
CommandStatus evaluate_sleep(Spreadsheet* sheet, Cell* cell, const char* expr) {
    int len = strlen(expr);
    if (len < 7 || expr[len-1] != ')') {
        cell->error_state = 1;
        return CMD_UNRECOGNIZED;
    }

    char arg_str[256];
    strncpy(arg_str, expr + 6, len - 7);
    arg_str[len - 7] = '\0';

    int duration = evaluate_cell_reference(sheet, arg_str, cell);
    if (cell->error_state) {
        return CMD_CIRCULAR_REF;
    }

    pthread_t thread;
    int* arg = malloc(sizeof(int));
    *arg = duration;
    if (pthread_create(&thread, NULL, sleep_wrapper, arg) != 0) {
        free(arg);
        cell->error_state = 1;
        return CMD_UNRECOGNIZED;
    }
    pthread_join(thread, NULL);
    free(arg);

    cell->value = duration;
    cell->error_state = 0;
    return CMD_OK;
}

/*------------------- set_cell_value -------------------*/

CommandStatus set_cell_value(Spreadsheet* sheet, int row, int col, const char* expr) {
    Cell* cell = sheet->grid[row][col];
    remove_dependencies(cell);
    cell->error_state = 0;
    
    // Constant value.
    char* end;
    long value = strtol(expr, &end, 10);
    if (*end == '\0') {
        free(cell->formula);
        cell->formula = NULL;
        cell->value = value;
        return CMD_OK;
    }
    
    // Store the formula for non-constant values
    free(cell->formula);
    cell->formula = strdup(expr);
    
    // Handle SUM/AVG, MIN/MAX, SLEEP via evaluate_function.
    if (strncmp(expr, "SUM(", 4) == 0 || strncmp(expr, "AVG(", 4) == 0 ||
        strncmp(expr, "MIN(", 4) == 0 || strncmp(expr, "MAX(", 4) == 0 ||
        strncmp(expr, "SLEEP(", 6) == 0) {
        return evaluate_function(sheet, cell, expr);
    }
    
    // Direct cell reference.
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
    
    // Handle arithmetic expressions.
    if (strchr(expr, '+') || strchr(expr, '-') ||
        strchr(expr, '*') || strchr(expr, '/')) {
        return evaluate_arithmetic(sheet, cell, expr);
    }
    
    return CMD_UNRECOGNIZED;
}

/*------------------- Command Handling -------------------*/

void get_all_dependents(Spreadsheet* sheet, Cell* start, Cell*** dependents_list, int* count) {
    *dependents_list = NULL;
    *count = 0;

    if (start->dependent_count == 0) return; // No dependents to process

    // Use a hash table to track visited cells more efficiently
    bool** visited = (bool**)calloc(sheet->rows, sizeof(bool*));
    for (int i = 0; i < sheet->rows; i++) {
        visited[i] = (bool*)calloc(sheet->cols, sizeof(bool));
    }

    // Initialize dynamic array for dependents
    int capacity = 10;
    Cell** deps = malloc(capacity * sizeof(Cell*));
    
    // Use a queue for BFS (start with direct dependents of 'start')
    Cell** queue = malloc(sheet->rows * sheet->cols * sizeof(Cell*));
    int front = 0, rear = 0;
    
    // Initialize queue with direct dependents of the modified cell
    for (int i = 0; i < start->dependent_count; i++) {
        Cell* dependent = start->dependents[i];
        if (!visited[dependent->row][dependent->col]) {
            visited[dependent->row][dependent->col] = true;
            queue[rear++] = dependent;
            
            // Add to dependents list
            if (*count >= capacity) {
                capacity *= 2;
                deps = realloc(deps, capacity * sizeof(Cell*));
            }
            deps[*count] = dependent;
            (*count)++;
        }
    }
    
    // Perform BFS to find all indirect dependents
    while (front < rear) {
        Cell* current = queue[front++];
        
        // Add all direct dependents of 'current' to queue
        for (int i = 0; i < current->dependent_count; i++) {
            Cell* dependent = current->dependents[i];
            if (!visited[dependent->row][dependent->col]) {
                visited[dependent->row][dependent->col] = true;
                queue[rear++] = dependent;
                
                // Add to dependents list
                if (*count >= capacity) {
                    capacity *= 2;
                    deps = realloc(deps, capacity * sizeof(Cell*));
                }
                deps[*count] = dependent;
                (*count)++;
            }
        }
    }
    
    // Allocate and return final list
    *dependents_list = malloc(*count * sizeof(Cell*));
    memcpy(*dependents_list, deps, *count * sizeof(Cell*));
    
    // Cleanup
    for (int i = 0; i < sheet->rows; i++) {
        free(visited[i]);
    }
    free(visited);
    free(queue);
    free(deps);
}

Cell** topological_sort(Cell** dependents, int count, int* sorted_count) {
    *sorted_count = 0;
    if (count == 0) return NULL;

    // Track in-degree for each cell (number of unresolved dependencies)
    int* in_degree = calloc(count, sizeof(int));
    Cell** sorted = malloc(count * sizeof(Cell*));

    // Calculate initial in-degree
    for (int i = 0; i < count; i++) {
        Cell* cell = dependents[i];
        in_degree[i] = 0;
        for (int j = 0; j < cell->dep_count; j++) {
            Cell* dep = cell->dependencies[j];
            // Check if the dependency is in the dependents list
            for (int k = 0; k < count; k++) {
                if (dependents[k] == dep) {
                    in_degree[i]++;
                    break;
                }
            }
        }
    }

    // Initialize queue with cells having in_degree 0
    int* queue = malloc(count * sizeof(int));
    int front = 0, rear = 0;
    for (int i = 0; i < count; i++) {
        if (in_degree[i] == 0) {
            queue[rear++] = i;
        }
    }

    // Process queue
    while (front < rear) {
        int idx = queue[front++];
        Cell* cell = dependents[idx];
        sorted[(*sorted_count)++] = cell;

        // Reduce in_degree for dependents
        for (int i = 0; i < cell->dependent_count; i++) {
            Cell* dependent = cell->dependents[i];
            for (int k = 0; k < count; k++) {
                if (dependents[k] == dependent) {
                    in_degree[k]--;
                    if (in_degree[k] == 0) {
                        queue[rear++] = k;
                    }
                    break;
                }
            }
        }
    }

    free(in_degree);
    free(queue);
    return sorted;
}

void propagate_changes(Spreadsheet* sheet, Cell* modified_cell) {
    Cell** dependents;
    int count;
    get_all_dependents(sheet, modified_cell, &dependents, &count);

    int sorted_count;
    Cell** sorted = topological_sort(dependents, count, &sorted_count);

    for (int i = 0; i < sorted_count; i++) {
        Cell* cell = sorted[i];
        if (cell->formula) {
            // Clear dependencies before re-evaluating to avoid stale references
            remove_dependencies(cell);
            cell->error_state = 0;
            set_cell_value(sheet, cell->row, cell->col, cell->formula);
        }
    }

    free(dependents);
    free(sorted);
}
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
        CommandStatus status = set_cell_value(sheet, row, col, expr);
        if (status == CMD_OK) {
            propagate_changes(sheet, sheet->grid[row][col]);
        }
        return status;
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
    Spreadsheet* sheet = create_spreadsheet(atoi(argv[1]), atoi(argv[2]));
    if (!sheet)
        return 1;
    
    char input[256];
    double last_time = 0.0;
    const char* last_status = "ok";
    struct timespec start, finish;
    
    while (1) {
        print_spreadsheet(sheet);
        printf("[%.1f] (%s) > ", last_time, last_status);
        
        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "q") == 0)
            break;
        
        clock_gettime(CLOCK_REALTIME, &start);
        CommandStatus status = handle_command(sheet, input);
        clock_gettime(CLOCK_REALTIME, &finish);
        
        last_time = (finish.tv_sec - start.tv_sec) +
                    (finish.tv_nsec - start.tv_nsec) / 1e9;
        
        switch (status) {
            case CMD_OK: last_status = "ok"; break;
            case CMD_UNRECOGNIZED: last_status = "unrecognized cmd"; break;
            case CMD_INVALID_CELL: last_status = "invalid cell"; break;
            case CMD_INVALID_RANGE: last_status = "invalid range"; break;
            case CMD_CIRCULAR_REF: last_status = "circular ref"; break;
            case CMD_DIV_BY_ZERO: last_status = "div by zero"; break;
            case CMD_RANGE_ERROR: last_status = "range error"; break;
            default: last_status = "error"; break;
        }
    }
    
    free_spreadsheet(sheet);
    return 0;
}