// Version 12
// Improvements over code_7.c:

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
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

// Updated Cell structure: added row and col fields.
typedef struct Cell {
    int value;
    char* formula;
    bool error_state;
    short row;    // cell’s row index
    short col;    // cell’s column index
    struct Cell** parents;
    int parents_count;
    int parents_capacity;
    struct Cell** children;
    int child_count;
    int child_capacity;
} Cell;

// Define the spreadsheet structure
typedef struct {
    Cell*** grid;
    short rows;
    short cols;
    int viewport_row;
    int viewport_col;
    bool output_enabled;
} Spreadsheet;

// Add range parsing and validation functions
typedef struct {
    short start_row;
    short start_col;
    short end_row;
    short end_col;
} Range;

char* get_column_name(int col);
void parse_cell_reference(const char* cell, int* row, int* col);
void parse_cell_reference(const char* cell, int* row, int* col);
int parse_operator(const char* expr, int* a, int* b, char* op);
CommandStatus parse_range(const char* range_str, Range* range);
CommandStatus set_cell_value(Spreadsheet* sheet, short row, short col, const char* expr, double* sleep_time);
CommandStatus evaluate_formula(Spreadsheet* sheet, Cell* cell, const char* expr, double* sleep_time);
/* ----- Memory helper functions ----- */
// safe_alloc: exits if malloc fails.
void* safe_alloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

// --- Memory Efficiency in Dependency Arrays ---
// When adding a parent or child, we allocate (or grow) the dependency array 
// only if needed. For example:
void add_parent(Cell* child, Cell* parent) {
    // Allocate child->parents only when first parent is being added.
    if (child->parents_capacity == 0) {
        child->parents_capacity = 1; // initial capacity set to a small number
        child->parents = malloc(child->parents_capacity * sizeof(Cell*));
    } else if (child->parents_count >= child->parents_capacity) {
        child->parents_capacity += 1;
        child->parents = realloc(child->parents, child->parents_capacity * sizeof(Cell*));
    }
    child->parents[child->parents_count++] = parent;

    // Also add the child to parent's children array similarly:
    if (parent->child_capacity == 0) {
        parent->child_capacity = 1;
        parent->children = malloc(parent->child_capacity * sizeof(Cell*));
    } else if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity += 1;
        parent->children = realloc(parent->children, parent->child_capacity * sizeof(Cell*));
    }
    parent->children[parent->child_count++] = child;
}

// When a cell’s formula is re-evaluated or replaced, remove dependency links as needed.
void remove_all_parents(Cell* cell) {
    // For each parent currently in the cell’s parents array, remove the corresponding child link.
    for (int i = 0; i < cell->parents_count; i++) {
        Cell* parent = cell->parents[i];
        // Remove "cell" from parent's children array.
        for (int j = 0; j < parent->child_count; j++) {
            if (parent->children[j] == cell) {
                parent->children[j] = parent->children[--parent->child_count];
                break;
            }
        }
    }
    free(cell->parents);
    cell->parents = NULL;
    cell->parents_count = 0;
    cell->parents_capacity = 0;
}

// bool detect_cycle(Cell* start, Cell* target) {
//     Cell** stack = safe_alloc(1024 * sizeof(Cell*));
//     bool* visited = calloc(MAX_ROWS * MAX_COLS, sizeof(bool));
//     int top = 0;

//     stack[top++] = start;
//     visited[start->row * MAX_COLS + start->col] = true;

//     while (top > 0) {
//         Cell* current = stack[--top];
//         if (current == target) {
//             free(stack);
//             free(visited);
//             return true;
//         }

//         for (int i = 0; i < current->parents_count; i++) {
//             Cell* parent = current->parents[i];
//             int idx = parent->row * MAX_COLS + parent->col;
//             if (!visited[idx]) {
//                 if (top % 1024 == 0) {
//                     stack = realloc(stack, (top + 1024) * sizeof(Cell*));
//                 }
//                 stack[top++] = parent;
//                 visited[idx] = true;
//             }
//         }
//     }

//     free(stack);
//     free(visited);
//     return false;
// }

// --- Helper: Linear index for a cell (assuming cell->row and cell->col are stored) ---
static inline int cell_index(Spreadsheet* sheet, int row, int col) {
    return row * sheet->cols + col;
}

// --- Topological Reevaluation using Kahn's algorithm ---
// This function gathers all cells transitively affected by modified_cell (using its children links),
// computes in-degrees on the restricted subgraph, and then re-evaluates formulas in topological order.
void reevaluate_topologically(Spreadsheet* sheet, Cell* modified_cell) {
    int max_cells = sheet->rows * sheet->cols;
    // allocate a boolean array (indexed by linear index) to mark affected cells.
    bool* affectedMark = calloc(max_cells, sizeof(bool));
    // Temporary array to hold pointers to all affected cells.
    Cell** affected = malloc(max_cells * sizeof(Cell*));
    int affectedCount = 0;

    // Use a DFS (stack) to collect all cells in the affected subgraph.
    Cell** stack = malloc(max_cells * sizeof(Cell*));
    int stackTop = 0;
    stack[stackTop++] = modified_cell;
    while (stackTop > 0) {
        Cell* current = stack[--stackTop];
        int idx = cell_index(sheet, current->row, current->col);
        if (!affectedMark[idx]) {
            affectedMark[idx] = true;
            affected[affectedCount++] = current;
            // For each child of this cell, add to DFS stack.
            for (int i = 0; i < current->child_count; i++) {
                Cell* child = current->children[i];
                stack[stackTop++] = child;
            }
        }
    }
    free(stack);

    // Create a lookup table: use an array of indices (size max_cells, default -1)
    int* lookup = malloc(max_cells * sizeof(int));
    for (int i = 0; i < max_cells; i++) {
        lookup[i] = -1;
    }
    for (int i = 0; i < affectedCount; i++) {
        int idx = cell_index(sheet, affected[i]->row, affected[i]->col);
        lookup[idx] = i;
    }

    // Compute in-degree (within the affected subgraph) for each affected cell.
    // (We count only those parent links that lie in the affected set.)
    int* inDegree = calloc(affectedCount, sizeof(int));
    for (int i = 0; i < affectedCount; i++) {
        Cell* cell = affected[i];
        int deg = 0;
        for (int j = 0; j < cell->parents_count; j++) {
            Cell* parent = cell->parents[j];
            int pidx = cell_index(sheet, parent->row, parent->col);
            if (affectedMark[pidx]) { // parent is in affected subgraph
                deg++;
            }
        }
        inDegree[i] = deg;
    }

    // Prepare a queue (using an array of affected cell indices) for those with inDegree == 0.
    int* queue = malloc(affectedCount * sizeof(int));
    int qFront = 0, qRear = 0;
    for (int i = 0; i < affectedCount; i++) {
        if (inDegree[i] == 0)
            queue[qRear++] = i;
    }

    // Process the queue in topological order.
    // For each cell, re-calculate its formula (if it has one) and then update
    // the in-degree of its children.
    double dummySleep = 0.0;
    while (qFront < qRear) {
        int curIndex = queue[qFront++];
        Cell* curCell = affected[curIndex];
        // If the cell has a formula, re-evaluate it.
        if (curCell->formula) {
            // Call your evaluate_formula() function or equivalent evaluation routine.
            // It should update curCell->value and set error if needed.
            CommandStatus stat = evaluate_formula(sheet, curCell, curCell->formula, &dummySleep);
            // (You may decide to propagate errors here if you want.)
            (void)stat;   // Ignored in this snippet.
        }
        // For every child in curCell->children that is within the affected subgraph,
        // decrement its in-degree.
        for (int i = 0; i < curCell->child_count; i++) {
            Cell* child = curCell->children[i];
            int childIdx = cell_index(sheet, child->row, child->col);
            if (affectedMark[childIdx]) {
                int lookupIndex = lookup[childIdx];
                if (lookupIndex != -1) {
                    inDegree[lookupIndex]--;
                    if (inDegree[lookupIndex] == 0) {
                        queue[qRear++] = lookupIndex;
                    }
                }
            }
        }
    }
    free(queue);
    free(inDegree);
    free(lookup);
    free(affected);
    free(affectedMark);
}

// propagate_errors: Recursively sets error_state for all dependent children.
void propagate_errors(Cell* cell) {
    for (int i = 0; i < cell->child_count; i++) {
        Cell* child = cell->children[i];
        if (!child->error_state) {
            child->error_state = true;
            propagate_errors(child);
        }
    }
}

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

CommandStatus handle_sleep(Spreadsheet* sheet, int row, int col, const char* expr, double* sleep_time) {
    // Remove "SLEEP(" from the beginning and ")" from the end
    char* sleep_arg = malloc(strlen(expr) - 6);
    strncpy(sleep_arg, expr + 6, strlen(expr) - 7);
    sleep_arg[strlen(expr) - 7] = '\0';

    int sleep_duration;
    
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

        sleep_duration = sheet->grid[sleep_row][sleep_col]->value;
    } else {
        sleep_duration = atoi(sleep_arg);
    }

    free(sleep_arg);

    if (sleep_duration < 0) {
        sheet->grid[row][col]->value = sleep_duration;
        *sleep_time = 0;
        return CMD_OK;
    }

    *sleep_time = sleep_duration;
    sheet->grid[row][col]->value = sleep_duration;
    return CMD_OK;
}

CommandStatus evaluate_formula(Spreadsheet* sheet, Cell* cell, const char* expr, double* sleep_time) {

    int expr_len = strlen(expr);
    if(expr_len == 0) {
        return CMD_UNRECOGNIZED;
    }
    // Handle constant value
    char* end;
    long value = strtol(expr, &end, 10);
    if (*end == '\0') {
        cell->value = value;
        return CMD_OK;
    }

    // Handle functions
    if (strncmp(expr, "SUM(", 4) == 0 || strncmp(expr, "AVG(", 4) == 0 ||
        strncmp(expr, "MIN(", 4) == 0 || strncmp(expr, "MAX(", 4) == 0) {
        
        bool is_sum = (expr[0] == 'S');
        bool is_avg = (expr[0] == 'A');
        bool is_min = (expr[0] == 'M' && expr[1] == 'I');
        
        char range_str[100];
        strncpy(range_str, expr + 4, strlen(expr) - 5);
        range_str[strlen(expr) - 5] = '\0';

        Range range;
        CommandStatus status = parse_range(range_str, &range);
        if (status != CMD_OK) return status;

        int total = 0, count = 0, min_val = INT_MAX, max_val = INT_MIN;
        bool error_found = false;   

        // Process range and add dependencies
        for (int r = range.start_row; r <= range.end_row; r++) {
            for (int c = range.start_col; c <= range.end_col; c++) {
                if (r >= sheet->rows || c >= sheet->cols) {
                    return CMD_INVALID_RANGE;
                }
                
                Cell* ref_cell = sheet->grid[r][c];
                add_parent(cell, ref_cell);
                
                if (ref_cell->error_state) {
                    error_found = true;
                    continue;
                }
                
                int val = ref_cell->value;
                total += val;
                count++;
                if (val < min_val) min_val = val;
                if (val > max_val) max_val = val;
            }
        }

        if (error_found) {
            cell->error_state = true;
            return CMD_RANGE_ERROR;
        }

        if (is_sum) cell->value = total;
        else if (is_avg) cell->value = (count > 0) ? total / count : 0;
        else if (is_min) cell->value = (min_val != INT_MAX) ? min_val : 0;
        else cell->value = (max_val != INT_MIN) ? max_val : 0;
        
        return CMD_OK;
    }
    // Handle SLEEP function
    else if (strncmp(expr, "SLEEP(", 6) == 0) {
        return handle_sleep(sheet, cell->row, cell->col, expr, sleep_time);
    }

    // 2. Try to interpret the entire expression as an integer literal.
    //    (e.g., "23" or "-17")
    char* endptr;
    long number = strtol(expr, &endptr, 10);
    if(*endptr == '\0') { 
        // The entire expression was a number.
        cell->value = (int)number;
        return CMD_OK;
    }

    // 3. Check whether the expression is a simple cell reference.
    // If all characters are alphanumeric (letters and digits) then assume it’s a reference.
    bool all_alnum = true;
    for (int i = 0; i < expr_len; i++) {
        if (!isalnum((unsigned char)expr[i])) {
            all_alnum = false;
            break;
        }
    }
    if(all_alnum) {
        int ref_row, ref_col;
        parse_cell_reference(expr, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
            return CMD_INVALID_CELL;
        }
        Cell* ref_cell = sheet->grid[ref_row][ref_col];
        if(ref_cell->error_state) {
            // cell->error_state = 1;
            return CMD_RANGE_ERROR;
        }
        // Add a dependency: cell now depends on ref_cell.
        add_parent(cell, ref_cell);
        cell->value = ref_cell->value;
        return CMD_OK;
    }

    // 4. At this point, the expression should be a binary arithmetic expression.
    // We now search for a binary operator. Note that we start scanning at index 1
    // so as not to confuse a leading minus sign with a subtraction operator.
    int op_index = -1;
    char op_char = '\0';
    for (int i = 1; i < expr_len; i++) {
        char c = expr[i];
        if(c == '+' || c == '-' || c == '*' || c == '/') {
            op_char = c;
            op_index = i;
            break;
        }
    }
    if(op_index == -1) {
        // No operator found but earlier tests failed – unrecognized expression.
        return CMD_UNRECOGNIZED;
    }

    // 5. Split the expression into left and right operand strings.
    //    For example, "B1+3" is split into "B1" and "3".
    char* left_str = strndup(expr, op_index);              // left substring: first op_index characters.
    char* right_str = strdup(expr + op_index + 1);           // right substring: after the operator.
    if (!left_str || !right_str) {
        free(left_str);
        free(right_str);
        return CMD_UNRECOGNIZED;
    }

    int left_val = 0, right_val = 0;

    // 6. Evaluate the left operand.
    // First, try converting to an integer using strtol. Note that if conversion
    // stops early, we assume the operand is a cell reference.
    char* left_endptr;
    long left_num = strtol(left_str, &left_endptr, 10);
    if(*left_endptr == '\0') {
        left_val = (int)left_num;
    } else {
        // Not a pure number; assume it is a cell reference.
        int ref_row, ref_col;
        parse_cell_reference(left_str, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
            free(left_str); free(right_str);
            return CMD_INVALID_CELL;
        }
        Cell* ref_cell = sheet->grid[ref_row][ref_col];
        if(ref_cell->error_state) {
            free(left_str); free(right_str);
            return CMD_RANGE_ERROR;
        }
        add_parent(cell, ref_cell);   // Record dependency.
        left_val = ref_cell->value;
    }

    // 7. Evaluate the right operand.
    char* right_endptr;
    long right_num = strtol(right_str, &right_endptr, 10);
    if(*right_endptr == '\0') {
        right_val = (int)right_num;
    } else {
        // Assume it is a cell reference.
        int ref_row, ref_col;
        parse_cell_reference(right_str, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
            free(left_str); free(right_str);
            return CMD_INVALID_CELL;
        }
        Cell* ref_cell = sheet->grid[ref_row][ref_col];
        if(ref_cell->error_state) {
            free(left_str); free(right_str);
            return CMD_RANGE_ERROR;
        }
        add_parent(cell, ref_cell);   // Record dependency.
        right_val = ref_cell->value;
    }

    // Clean up temporary strings.
    free(left_str);
    free(right_str);

    // 8. Perform the binary operation indicated by op_char.
    switch(op_char) {
        case '+': 
            cell->value = left_val + right_val;
            break;
        case '-': 
            cell->value = left_val - right_val;
            break;
        case '*': 
            cell->value = left_val * right_val;
            break;
        case '/':
            if(right_val == 0) {
                cell->error_state = true;
                return CMD_DIV_BY_ZERO;
            }
            cell->value = left_val / right_val;
            break;
        default:
            return CMD_UNRECOGNIZED;
    }

    return CMD_OK;
}

CommandStatus set_cell_value(Spreadsheet* sheet, short row, short col, const char* expr, double* sleep_time) {
    Cell* cell = sheet->grid[row][col];

    // Clear old dependencies.
    remove_all_parents(cell);

    // Replace old formula.
    free(cell->formula);
    cell->formula = strdup(expr);
    cell->error_state = false;
    cell->row = row;
    cell->col = col;

    // Evaluate as a formula.
    CommandStatus status = evaluate_formula(sheet, cell, expr, sleep_time);
    if (status == CMD_OK) {
        reevaluate_topologically(sheet, cell);
    } else {
        cell->error_state = true;
        propagate_errors(cell);
    }
    return status;
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

// Function to scroll to a cell
// version 5
CommandStatus scroll_to_cell(Spreadsheet* sheet, const char* cell) {
    short row, col;
    parse_cell_reference(cell, &row, &col);
    if (row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols) {
        return CMD_INVALID_CELL;
    }
    sheet->viewport_row = row;
    sheet->viewport_col = col;
    return CMD_OK;
}

// Function to handle commands
// version 4
CommandStatus handle_command(Spreadsheet* sheet, const char* cmd, double* sleep_time) {
// CommandStatus handle_command(Spreadsheet* sheet, const char* cmd) {
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
        
        short row, col;
        parse_cell_reference(cell_ref, &row, &col);
        
        if(row < 0 || row >= sheet->rows || col < 0 || col >= sheet->cols) {
            return CMD_INVALID_CELL;
        }
        
        const char* expr = eq + 1;
        return set_cell_value(sheet, row, col, expr, sleep_time);
    }

    return CMD_UNRECOGNIZED;
}

// Updated spreadsheet freeing function
void free_spreadsheet(Spreadsheet* sheet) {
    if (!sheet) return;

    for (int i = 0; i < sheet->rows; i++) {
        for (int j = 0; j < sheet->cols; j++) {
                free(sheet->grid[i][j]->formula);
                // Free dependency tracking arrays
                free(sheet->grid[i][j]->parents);
                free(sheet->grid[i][j]->children);
                free(sheet->grid[i][j]);
        }
        free(sheet->grid[i]);
    }
    free(sheet->grid);
    free(sheet);
}

// Updated spreadsheet creation function
Spreadsheet* create_spreadsheet(short rows, short cols) {
    if (rows < 1 || rows > MAX_ROWS || cols < 1 || cols > MAX_COLS) {
        fprintf(stderr, "Invalid spreadsheet dimensions\n");
        return NULL;
    }

    Spreadsheet* sheet = (Spreadsheet*)malloc(sizeof(Spreadsheet));
    sheet->rows = rows;
    sheet->cols = cols;
    sheet->viewport_row = 0;
    sheet->viewport_col = 0;
    sheet->output_enabled = true;

    // Allocate grid with proper initialization
    sheet->grid = (Cell***)malloc(rows * sizeof(Cell**));
    for (int i = 0; i < rows; i++) {
        sheet->grid[i] = (Cell**)malloc(cols * sizeof(Cell*));
        for (int j = 0; j < cols; j++) {
            Cell* cell = (Cell*)malloc(sizeof(Cell));
            cell->value = 0;
            cell->formula = NULL;
            cell->error_state = 0;
    
            // Initialize dependency tracking arrays
            cell->parents = NULL;
            cell->parents_count = 0;
            cell->parents_capacity = 0;
            
            cell->children = NULL;
            cell->child_count = 0;
            cell->child_capacity = 0;
            
            sheet->grid[i][j] = cell;
        }
    }
    return sheet;
}

// Function to print the spreadsheet grid
// version 2
void print_spreadsheet(Spreadsheet* sheet) {
    if (!sheet->output_enabled) return;

    // Calculate display bounds dynamically
    short start_row = sheet->viewport_row;
    short start_col = sheet->viewport_col;

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

    char input[128];
    double last_time = 0.0;
    const char* last_status = "ok";
    clock_t start, end;
    double sleep_time = 0.0;

// Version 2
    while (1) {
        print_spreadsheet(sheet);
        printf("[%.1f] (%s) > ", last_time, last_status);
        
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        if (strcmp(input, "q") == 0) break;

        // Measure elapsed time
        start = clock();
        CommandStatus status = handle_command(sheet, input, &sleep_time);
        end = clock();

        // Calculate elapsed time
        double command_time = (double)(end - start) / CLOCKS_PER_SEC;
        last_time = command_time + sleep_time;
        
        // If there's a sleep command, actually perform the sleep
        if (sleep_time > 0) {
            sleep(sleep_time);
            sleep_time = 0.0;  // Reset sleep_time for the next command
        }
                
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
    // Free the spreadsheet
    free_spreadsheet(sheet);
    return 0;
}