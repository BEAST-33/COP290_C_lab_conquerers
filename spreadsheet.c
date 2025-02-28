#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>
#include "avl.h"

#define MAX_ROWS 999
#define MAX_COLS 18278
#define VIEWPORT_SIZE 10

// Status enumeration.
typedef enum {
    CMD_OK,
    CMD_UNRECOGNIZED,
    CMD_INVALID_CELL,
    CMD_INVALID_RANGE,
    CMD_CIRCULAR_REF,
    CMD_DIV_BY_ZERO,
    CMD_RANGE_ERROR
} CommandStatus;

// Range structure: 4 shorts (8 bytes total)
typedef struct {
    short start_row;
    short start_col;
    short end_row;
    short end_col;
} Range;

// Optimized Cell structure.
// __attribute__((packed)) minimizes padding.
typedef struct Cell {
    AVLTree *children;   // 8 bytes (AVL tree for children)
    int cell1;
    int cell2;
    int value;          // 4 bytes
    short formula;
    bool error_state;  // 1 byte
} Cell;
// Formula codes
// -1: No formula
// 80: Simple cell reference
// 10: Add both cells 
// 20: Sub ''
// 30: Div ''
// 40: Mult ''
// 12: Add with single cell and custom value
// 22: Sub ''
// 32: Div ''
// 42: Mult ''
// 13: Add with custom value and single cell
// 23: Sub ''
// 33: Div '' 
// 43: Mult ''
// 5: SUM
// 6: AVG
// 7: MIN
// 8: MAX
// 9: STDEV
// 101: SLEEP

// Spreadsheet structure now uses a contiguous array for grid.
typedef struct {
    Cell* grid;    // Pointer to a contiguous block of Cells.
    short rows;
    short cols;
    short viewport_row;
    short viewport_col;
    bool output_enabled;
} Spreadsheet;

// Function prototypes.
char* get_column_name(int col);
void parse_cell_reference(const char* cell, short* row, short* col);
int column_name_to_number(const char* name);
CommandStatus parse_range(const char* range_str, Range* range);
CommandStatus set_cell_value(Spreadsheet* sheet, short row, short col, const char* expr, double* sleep_time);
CommandStatus evaluate_formula(Spreadsheet* sheet, Cell* cell, short row, short col, const char* expr, double* sleep_time);
CommandStatus reevaluate_formula(Spreadsheet* sheet, Cell* cell, double* sleep_time);

/* ---------- Contiguous Grid Access ---------- */
// Returns pointer to cell at (row, col).
static inline Cell* get_cell(Spreadsheet* sheet, short row, short col) {
    return &(sheet->grid[(int)(row * sheet->cols + col)]);
}

// Helper: encode (row, col) as an integer key.
static inline int encode_cell_key(short row, short col, short total_cols) {
    return (int)(row * total_cols + col);
}

void get_row_col(int index, short* row, short* col, short total_cols){
    *col = index%total_cols;
    *row = index/total_cols;
}

// For children dependency, we use an AVL tree.
// Adds a child key to the parent's children AVL tree.
void add_child(Cell* parent, short row, short col, short total_cols) {
    int key = encode_cell_key(row, col, total_cols);
    parent->children = avl_insert(parent->children, key);
}

// Removes a child key from the parent's children AVL tree.
void remove_child(Cell* parent, int key) {
    parent->children = avl_delete(parent->children, key);
}

// Removes all parents from a cell and the cell from every parent's children.
void remove_all_parents(Spreadsheet *sheet, short row, short col){
    int key = encode_cell_key(row, col, sheet->cols);
    Cell* child = get_cell(sheet, row, col);
    if(child->formula<=9 && child->formula>=5){
        Range range;
        range.start_row = child->cell1/sheet->cols;
        range.start_col = child->cell1%sheet->cols;
        range.end_row = child->cell2/sheet->cols;
        range.end_col = child->cell2%sheet->cols;
        for(int r=range.start_row; r<=range.end_row; r++){
            for(int c=range.start_col; c<=range.end_col; c++){
                Cell* ref_cell = get_cell(sheet, r, c);
                remove_child(ref_cell, key);
            }
        }
    }
    else if(child->formula==101){
        Cell* ref_cell = get_cell(sheet, child->cell1/sheet->cols, child->cell1%sheet->cols);
        remove_child(ref_cell, key);
    }
    // else if()
}

/* 
   (Additional functions such as reevaluate_topologically, evaluate_formula, 
     and others remain unchanged except for parameter type adjustments if needed.)
*/
// --- Topological Reevaluation using Kahn's algorithm ---
// This function gathers all cells transitively affected by modified_cell (using its children links),
// computes in-degrees on the restricted subgraph, and then re-evaluates formulas in topological order.
// void reevaluate_topologically(Spreadsheet* sheet, Cell* modified_cell) {
//     int max_cells = sheet->rows * sheet->cols;
//     // allocate a boolean array (indexed by linear index) to mark affected cells.
//     bool* affectedMark = calloc(max_cells, sizeof(bool));
//     // Temporary array to hold pointers to all affected cells.
//     Cell** affected = malloc(max_cells * sizeof(Cell*));
//     int affectedCount = 0;

//     // Use a DFS (stack) to collect all cells in the affected subgraph.
//     Cell** stack = malloc(max_cells * sizeof(Cell*));
//     int stackTop = 0;
//     stack[stackTop++] = modified_cell;
//     while (stackTop > 0) {
//         Cell* current = stack[--stackTop];
//         int idx = cell_index(sheet, current->row, current->col);
//         if (!affectedMark[idx]) {
//             affectedMark[idx] = true;
//             affected[affectedCount++] = current;
//             // For each child of this cell, add to DFS stack.
//             for (int i = 0; i < current->child_count; i++) {
//                 Cell* child = current->children[i];
//                 stack[stackTop++] = child;
//             }
//         }
//     }
//     free(stack);

//     // Create a lookup table: use an array of indices (size max_cells, default -1)
//     int* lookup = malloc(max_cells * sizeof(int));
//     for (int i = 0; i < max_cells; i++) {
//         lookup[i] = -1;
//     }
//     for (int i = 0; i < affectedCount; i++) {
//         int idx = cell_index(sheet, affected[i]->row, affected[i]->col);
//         lookup[idx] = i;
//     }

//     // Compute in-degree (within the affected subgraph) for each affected cell.
//     // (We count only those parent links that lie in the affected set.)
//     int* inDegree = calloc(affectedCount, sizeof(int));
//     for (int i = 0; i < affectedCount; i++) {
//         Cell* cell = affected[i];
//         int deg = 0;
//         for (int j = 0; j < cell->parents_count; j++) {
//             Cell* parent = cell->parents[j];
//             int pidx = cell_index(sheet, parent->row, parent->col);
//             if (affectedMark[pidx]) { // parent is in affected subgraph
//                 deg++;
//             }
//         }
//         inDegree[i] = deg;
//     }

//     // Prepare a queue (using an array of affected cell indices) for those with inDegree == 0.
//     int* queue = malloc(affectedCount * sizeof(int));
//     int qFront = 0, qRear = 0;
//     for (int i = 0; i < affectedCount; i++) {
//         if (inDegree[i] == 0)
//             queue[qRear++] = i;
//     }

//     // Process the queue in topological order.
//     // For each cell, re-calculate its formula (if it has one) and then update
//     // the in-degree of its children.
//     double dummySleep = 0.0;
//     while (qFront < qRear) {
//         int curIndex = queue[qFront++];
//         Cell* curCell = affected[curIndex];
//         // If the cell has a formula, re-evaluate it.
//         if (curCell->formula) {
//             // Call your evaluate_formula() function or equivalent evaluation routine.
//             // It should update curCell->value and set error if needed.
//             CommandStatus stat = evaluate_formula(sheet, curCell, curCell->formula, &dummySleep);
//             // (You may decide to propagate errors here if you want.)
//             (void)stat;   // Ignored in this snippet.
//         }
//         // For every child in curCell->children that is within the affected subgraph,
//         // decrement its in-degree.
//         for (int i = 0; i < curCell->child_count; i++) {
//             Cell* child = curCell->children[i];
//             int childIdx = cell_index(sheet, child->row, child->col);
//             if (affectedMark[childIdx]) {
//                 int lookupIndex = lookup[childIdx];
//                 if (lookupIndex != -1) {
//                     inDegree[lookupIndex]--;
//                     if (inDegree[lookupIndex] == 0) {
//                         queue[qRear++] = lookupIndex;
//                     }
//                 }
//             }
//         }
//     }
//     free(queue);
//     free(inDegree);
//     free(lookup);
//     free(affected);
//     free(affectedMark);
// }

// get_column_name 
char* get_column_name(int col) {
    char* name = malloc(4);
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

int column_name_to_number(const char* name) {
    int result = 0;
    for (int i = 0; name[i] != '\0'; i++) {
        result *= 26;
        result += (toupper(name[i]) - 'A' + 1);
    }
    return result - 1;
}

void parse_cell_reference(const char* cell, short* row, short* col) {
    char col_name[4] = {0};
    int i = 0;
    while (cell[i] != '\0' && isupper(cell[i])) {
        if (i >= 3) { *row = *col = -1; return; }
        col_name[i] = cell[i];
        i++;
    }
    if (cell[i] == '\0' || !isdigit(cell[i])) { *row = *col = -1; return; }
    *col = (short)column_name_to_number(col_name);
    *row = (short)(atoi(cell + i) - 1);
    while (cell[i] != '\0') {
        if (!isdigit(cell[i])) { *row = *col = -1; return; }
        i++;
    }
}

// parse_range remains unchanged.
CommandStatus parse_range(const char* range_str, Range* range) {
    char* colon = strchr(range_str, ':');
    if (!colon || colon == range_str || colon[1] == '\0')
        return CMD_INVALID_RANGE;
    char start_cell[20];
    char end_cell[20];
    strncpy(start_cell, range_str, colon - range_str);
    start_cell[colon - range_str] = '\0';
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

Cell* get_cell_check(Spreadsheet* sheet, int index){
    return &(sheet->grid[index]);
}

void sum_value(Spreadsheet* sheet, Cell *cell){
        int sum = 0;
        short row1, col1, row2, col2;
        get_row_col(cell->cell1, &row1, &col1, sheet->cols);
        get_row_col(cell->cell2, &row2, &col2, sheet->cols);
        for(int i=row1; i<=row2; i++){
            for(int j=col1; j<=col2; j++){
                Cell* ref_cell = get_cell_check(sheet, i*sheet->cols+j);
                if(ref_cell->error_state){
                    cell->error_state = 1;
                    return CMD_OK;
                }
                sum += ref_cell->value;
            }
        }
}

void variance(Spreadsheet* sheet, Cell *cell){
    double variance = 0.0;
    int count = 0;
    short row1, col1, row2, col2;
    get_row_col(cell->cell1, &row1, &col1, sheet->cols);
    get_row_col(cell->cell2, &row2, &col2, sheet->cols);
    count = (row2-row1+1)*(col2-col1+1);
    sum_value(sheet, cell);
    cell->value = cell->value / count;
    for(int i=row1; i<=row2; i++){
        for(int j=col1; j<=col2; j++){
            Cell* ref_cell = get_cell_check(sheet, i*sheet->cols+j);
            if(ref_cell->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            variance += (ref_cell->value - cell->value) * (ref_cell->value - cell->value);
        }
    }
    variance /= count;
    cell->value = (int)round(sqrt(variance));
}

void min_max(Spreadsheet* sheet, Cell* cell, bool is_min){
    int max = INT_MIN;
    int min = INT_MAX; 
    short row1, col1, row2, col2;
    get_row_col(cell->cell1, &row1, &col1, sheet->cols);
    get_row_col(cell->cell2, &row2, &col2, sheet->cols);
    for(int i=row1; i<=row2; i++){
        for(int j=col1; j<=col2; j++){
            Cell* ref_cell = get_cell_check(sheet, i*sheet->cols+j);
            if(ref_cell->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            if(ref_cell->value>max){
                max = ref_cell->value;
            }
            if(ref_cell->value<min){
                min = ref_cell->value;
            }
        }
    }
    cell->value = is_min ? min : max;
}

/* ---------- (Stub) Reevaluate Formula ---------- */
CommandStatus reevaluate_formula(Spreadsheet* sheet, Cell* cell, double* sleep_time) {
    short msb = cell->formula/10;
    if (cell->formula==-1) return CMD_OK;
    // ADD
    else if(msb==1){
        msb = cell->formula%10;
        if(msb==0){
            Cell* ref_cell1 = get_cell_check(sheet, cell->cell1);
            Cell* ref_cell2 = get_cell_check(sheet, cell->cell2);
            if(ref_cell1->error_state || ref_cell2->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = ref_cell1->value + ref_cell2->value;
        }
        else if(msb==2){
            Cell* ref_cell1 = get_cell_check(sheet, cell->cell1);
            if(ref_cell1->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = ref_cell1->value + cell->cell2;
        }
        else {
            Cell *ref_cell2 = get_cell_check(sheet, cell->cell2);
            if(ref_cell2->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = cell->cell1 + ref_cell2->value;
        }
    }
    // SUB
    else if(msb==2){
        msb = cell->formula%10;
        if(msb==0){
            Cell* ref_cell1 = get_cell_check(sheet, cell->cell1);
            Cell* ref_cell2 = get_cell_check(sheet, cell->cell2);
            if(ref_cell1->error_state || ref_cell2->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = ref_cell1->value - ref_cell2->value;
        }
        else if(msb==2){
            Cell* ref_cell1 = get_cell_check(sheet, cell->cell1);
            if(ref_cell1->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = ref_cell1->value - cell->cell2;
        }
        else {
            Cell *ref_cell2 = get_cell_check(sheet, cell->cell2);
            if(ref_cell2->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = cell->cell1 - ref_cell2->value;
        }
    }
    // Div
    else if(msb==3){
        msb = cell->formula%10;
        if(msb==0){
            Cell* ref_cell1 = get_cell_check(sheet, cell->cell1);
            Cell* ref_cell2 = get_cell_check(sheet, cell->cell2);
            if(ref_cell1->error_state || ref_cell2->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            if(ref_cell2->value==0){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = ref_cell1->value / ref_cell2->value;
        }
        else if(msb==2){
            Cell* ref_cell1 = get_cell_check(sheet, cell->cell1);
            if(ref_cell1->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            if(cell->cell2==0){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = ref_cell1->value / cell->cell2;
        }
        else {
            Cell *ref_cell2 = get_cell_check(sheet, cell->cell2);
            if(ref_cell2->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            if(ref_cell2->value==0){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = cell->cell1 / ref_cell2->value;
        }
    }
    // MULt
    else if(msb==4){
        msb = cell->formula%10;
        if(msb==0){
            Cell* ref_cell1 = get_cell_check(sheet, cell->cell1);
            Cell* ref_cell2 = get_cell_check(sheet, cell->cell2);
            if(ref_cell1->error_state || ref_cell2->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = ref_cell1->value * ref_cell2->value;
        }
        else if(msb==2){
            Cell* ref_cell1 = get_cell_check(sheet, cell->cell1);
            if(ref_cell1->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = ref_cell1->value * cell->cell2;
        }
        else {
            Cell *ref_cell2 = get_cell_check(sheet, cell->cell2);
            if(ref_cell2->error_state){
                cell->error_state = 1;
                return CMD_OK;
            }
            cell->value = cell->cell1 * ref_cell2->value;
        }
    }
    // SUM
    else if(msb==5){
        sum_value(sheet, cell);
    }
    // AVG
    else if(msb==6){
        sum_value(sheet, cell);
        int count = 0;
        short row1, col1, row2, col2;
        get_row_col(cell->cell1, &row1, &col1, sheet->cols);
        get_row_col(cell->cell2, &row2, &col2, sheet->cols);
        count = (row2-row1+1)*(col2-col1+1);
        cell->value = cell->value /count;
    }
    // MIN
    else if(msb==7){
        min_max(sheet, cell, true);
    }
    // MAX
    else if(msb==8){
        min_max(sheet, cell, false);
    }
    // STDEV
    else if(msb==9){
        variance(sheet, cell);
    }

    // Sleep yet to be implemented
    return CMD_OK;
}

/* ---------- (Stub) Evaluate Formula ---------- */
CommandStatus evaluate_formula(Spreadsheet* sheet, Cell* cell, short row, short col, const char* expr, double* sleep_time) {

    int expr_len = strlen(expr);
    if(expr_len == 0) {
        return CMD_UNRECOGNIZED;
    }

    /* --- Check for range-based functions: SUM, AVG, MIN, MAX, STDEV --- */
    if (strncmp(expr, "SUM(", 4) == 0 || strncmp(expr, "AVG(", 4) == 0 ||
        strncmp(expr, "MIN(", 4) == 0 || strncmp(expr, "MAX(", 4) == 0 ||
        strncmp(expr, "STDEV(", 6) == 0) {
        
        bool is_sum = (strncmp(expr, "SUM(", 4) == 0);
        bool is_avg = (strncmp(expr, "AVG(", 4) == 0);
        bool is_min = (strncmp(expr, "MIN(", 4) == 0);
        bool is_max = (strncmp(expr, "MAX(", 4) == 0);
        bool is_stdev = (strncmp(expr, "STDEV(", 6) == 0);
        int prefix_len = is_stdev ? 6 : 4;

        // Check trailing parenthesis.
        if (expr[expr_len - 1] != ')') {
            return CMD_UNRECOGNIZED;
        }
        // Extract the range string (inside the function call).
        char* range_str = strndup(expr + prefix_len, expr_len - prefix_len - 1);
        if (!range_str) {
            return CMD_UNRECOGNIZED;
        }
        Range range;
        CommandStatus rstatus = parse_range(range_str, &range);
        free(range_str);
        if (rstatus != CMD_OK) {
            cell->error_state = 1;
            return rstatus;
        }
        // Range must be valid (start must be less than or equal to end).
        if (range.start_row > range.end_row || range.start_col > range.end_col) {
            return CMD_INVALID_RANGE;
        }

        // Clear old dependencies.
        remove_all_parents(sheet, row, col);
        // Add in parent the children only 
        for(int r=range.start_row; r<=range.end_row; r++){
            for(int c=range.start_col; c<=range.end_col; c++){
                Cell* ref_cell = get_cell_check(sheet, r*sheet->cols+c);
                ref_cell->children = avl_insert(ref_cell->children, encode_cell_key(row, col, sheet->cols));
            }
        }

        // Store the range as cell1 and cell2
        cell->cell1=encode_cell_key(range.start_row, range.start_col, sheet->cols);
        cell->cell2=encode_cell_key(range.end_row, range.end_col, sheet->cols);
        // Set the formula code
        if(is_stdev){
            cell->formula = 9;
            variance(sheet, cell);
        }
        else if(is_max){
            cell->formula = 8;
            min_max(sheet, cell, false);
        }
        else if(is_min){
            cell->formula = 7;
            min_max(sheet, cell, false);
        }
        else if(is_avg){
            cell->formula = 6;
            sum_value(sheet, cell);
            cell->value = cell->value / ((range.end_row-range.start_row+1)*(range.end_col-range.start_col+1));
        }
        else{
            cell->formula = 5;
            sum_value(sheet, cell);
        }

        return CMD_OK;
    }
   
    // Handle SLEEP function
    else if (strncmp(expr, "SLEEP(", 6) == 0) {
        // // Ensure the last character is ')'
        // if (expr[expr_len - 1] != ')') {
        //     return CMD_UNRECOGNIZED;
        // }
        // // Extract the inner argument between the parentheses.
        // char* inner = strndup(expr + 6, expr_len - 7);
        // if (!inner) {
        //     cell->error_state = 1;
        //     return CMD_UNRECOGNIZED;
        // }
        // // Check if the inner argument is a number.
        // char* inner_endptr;
        // long sleepArg = strtol(inner, &inner_endptr, 10);
        // if (*inner_endptr == '\0') {
        //     // The argument is a pure number.
        //     if (sleepArg < 0) {
        //         cell->value = (int)sleepArg; // update immediately without sleep
        //     } else {
        //         cell->value = (int)sleepArg;
        //         *sleep_time = (double)sleepArg;
        //     }
        //     // Instead of storing "SLEEP(5)", store just "5".
        //     free(cell->formula);
        //     cell->formula = strdup(inner);
        //     free(inner);
        //     return CMD_OK;
        // }
        // // Otherwise, assume inner is a cell reference.
        // int ref_row = 0, ref_col = 0;
        // parse_cell_reference(inner, &ref_row, &ref_col);
        // free(inner);
        // if (ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
        //     cell->error_state = 1;
        //     return CMD_INVALID_CELL;
        // }
        // Cell* ref_cell = sheet->grid[ref_row][ref_col];
        // if (ref_cell->error_state) {
        //     return CMD_RANGE_ERROR;
        // }
        // // Add dependency: register that the current cell depends on ref_cell.
        // add_parent(cell, ref_cell);
        // cell->value = ref_cell->value;
        // // Instead of storing "SLEEP(B2)", store only "B2" as this cell's formula.
        // free(cell->formula);
        // // Here we re-create the cell reference string. We assume get_column_name converts a column number
        // // to its letter representation (e.g. 0 -> "A", 1 -> "B", etc.). Adjust as needed.
        // char col_name;
        // get_column_name(ref_col, col_name, sizeof(col_name));
        // char formula_str;
        // snprintf(formula_str, sizeof(formula_str), "%s%d", col_name, ref_row + 1);
        // cell->formula = strdup(formula_str);
        // return CMD_OK;
        // return handle_sleep(sheet, row, col, expr, sleep_time);
    }

    // 2. Try to interpret the entire expression as an integer literal.
    //    (e.g., "23" or "-17")
    char* endptr;
    long number = strtol(expr, &endptr, 10);
    if(*endptr == '\0') { 
        // The entire expression was a number.
        cell->value = (int)number;
        cell->formula = -1;
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
    // If the expression is a simple cell reference.
    if(all_alnum) {
        short ref_row, ref_col;
        parse_cell_reference(expr, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
            return CMD_INVALID_CELL;
        }
        Cell* ref_cell = get_cell(sheet, ref_row, ref_col);
        cell->cell1=encode_cell_key(ref_row, ref_col, sheet->cols);
        if(ref_cell->error_state) {
            cell->error_state = 1;
        }
        cell->formula = 80;
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
    bool error_found = false;
    char* left_endptr;
    long left_num = strtol(left_str, &left_endptr, 10);
    if(*left_endptr == '\0') {
        left_val = (int)left_num;
    } else {
        // Not a pure number; assume it is a cell reference.
        short ref_row, ref_col;
        parse_cell_reference(left_str, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
            free(left_str); free(right_str);
            return CMD_INVALID_CELL;
        }
        Cell* ref_cell = get_cell(sheet, ref_row, ref_col);
        if(ref_cell->error_state) {
            free(left_str); free(right_str);
            error_found = true;
            cell->error_state = true;
            // return CMD_RANGE_ERROR;
        }
        // Add a dependency: cell now depends on ref_cell.
        cell->cell1 = encode_cell_key(ref_row, ref_col, sheet->cols);
        left_val = ref_cell->value;
    }

    // 7. Evaluate the right operand.
    char* right_endptr;
    long right_num = strtol(right_str, &right_endptr, 10);
    if(*right_endptr == '\0') {
        right_val = (int)right_num;
    } else {
        // Assume it is a cell reference.
        short ref_row, ref_col;
        parse_cell_reference(right_str, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
            free(left_str); free(right_str);
            return CMD_INVALID_CELL;
        }
        Cell* ref_cell = get_cell(sheet, ref_row, ref_col);
        if(ref_cell->error_state) {
            free(left_str); free(right_str);
            error_found = true;
            cell->error_state = true;
        }
        cell->cell2 = encode_cell_key(ref_row, ref_col, sheet->cols);
        right_val = ref_cell->value;
    }

    // Clean up temporary strings.
    free(left_str);
    free(right_str);
    // If an error was found, in any cell reference then cell is in error state.
    if(error_found) {
        return CMD_OK;
    }
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

/* ---------- Command Handling (Simplified) ---------- */
CommandStatus set_cell_value(Spreadsheet* sheet, short row, short col, const char* expr, double* sleep_time) {
    Cell* cell = get_cell(sheet, row, col);

    // Clear error state. Think about this if the command parsed by you is not valid and earlier if the cell was in error state.
    cell->error_state = false;

    // Evaluate as a formula.
    CommandStatus status = evaluate_formula(sheet, cell, row, col, expr, sleep_time);
    if (status == CMD_OK) {
        // reevaluate_topologically(sheet, cell);
    } else {
        cell->error_state = true;
        // propagate_errors(cell);
    }
    return status;
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

void print_spreadsheet(Spreadsheet* sheet) {
    if (!sheet->output_enabled) return;
    short start_row = sheet->viewport_row;
    short start_col = sheet->viewport_col;
    int display_rows = (sheet->rows - start_row < VIEWPORT_SIZE) ? sheet->rows - start_row : VIEWPORT_SIZE;
    int display_cols = (sheet->cols - start_col < VIEWPORT_SIZE) ? sheet->cols - start_col : VIEWPORT_SIZE;
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
            Cell* cell = get_cell(sheet, i, j);
            if (cell->error_state)
                printf("%-8s", "ERR");
            else
                printf("%-8d", cell->value);
        }
        printf("\n");
    }
}

/* ---------- Spreadsheet Creation ---------- */
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
    // Allocate one contiguous block for all cells.
    sheet->grid = (Cell*)malloc(rows * cols * sizeof(Cell));
    if (!sheet->grid) {
        fprintf(stderr, "Failed to allocate grid\n");
        free(sheet);
        return NULL;
    }
    // Initialize each cell.
    int total = rows * cols;
    for (int i = 0; i < total; i++) {
            Cell* cell = sheet->grid+i;
            cell->formula = -1;
            // cell->children = NULL;
            cell->value = 0;
            cell->error_state = false;
            cell->cell1 = 0;
            cell->cell2 = 0;
    }
    return sheet;
}

void free_spreadsheet(Spreadsheet* sheet) {
    if (!sheet) return;
    int total = sheet->rows * sheet->cols;
    for (int i = 0; i < total; i++) {
        Cell* cell = &sheet->grid[i];
        // avl_free(cell->children);
    }
    free(sheet->grid);
    free(sheet);
}

/* ----- main: Convert command line arguments to short ----- */
int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <rows> <columns>\n", argv[0]);
        return 1;
    }
    short rows = (short)atoi(argv[1]);
    short cols = (short)atoi(argv[2]);
    Spreadsheet* sheet = create_spreadsheet(rows, cols);
    if (!sheet) return 1;
    
    char input[128];
    double last_time = 0.0;
    const char* last_status = "ok";
    clock_t start, end;
    double sleep_time = 0.0;
    
    while (1) {
        print_spreadsheet(sheet);
        printf("[%.1f] (%s) > ", last_time, last_status);
        
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        if (strcmp(input, "q") == 0) break;
        
        start = clock();
        CommandStatus status=CMD_OK;
        // CommandStatus status = handle_command(sheet, input, &sleep_time);
        end = clock();
        
        double command_time = (double)(end - start) / CLOCKS_PER_SEC;
        last_time = command_time + sleep_time;
        if (sleep_time > 0) {
            sleep(sleep_time);
            sleep_time = 0.0;
        }
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
    free_spreadsheet(sheet);
    return 0;
}