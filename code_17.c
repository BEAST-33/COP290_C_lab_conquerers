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
typedef struct __attribute__((packed)) Cell {
    AVLTree children;   // 8 bytes (AVL tree for children)
    int cell1;           // Stores parent cell key or start of range or custom value
    int cell2;           // Stores parent cell key or end of range or custom value
    int value;          // stores the value of the cell
    short formula;      // stores the formula code
    bool error_state;  // 1 byte
} Cell;
// Formula codes
// -1: No formula
// 82: Simple cell reference
// 10: Add with both cells 
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
// 5: SUM in Range cell1 to cell2
// 6: AVG
// 7: MIN
// 8: MAX
// 9: STDEV
// 102: SLEEP with cell reference

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

Cell* get_cell_check(Spreadsheet* sheet, int index){
    return &(sheet->grid[index]);
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
    if(child->formula==-1){
        return;
    }
    short rem = child->formula%10;
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
    else if(rem==0){
        Cell* ref_cell1 = get_cell(sheet, child->cell1/sheet->cols, child->cell1%sheet->cols);
        Cell* ref_cell2 = get_cell(sheet, child->cell2/sheet->cols, child->cell2%sheet->cols);
        remove_child(ref_cell1, key);
        remove_child(ref_cell2, key);
    }
    else if(rem==2){
        Cell* ref_cell1 = get_cell(sheet, child->cell1/sheet->cols, child->cell1%sheet->cols);
        remove_child(ref_cell1, key);
    }
    else if(rem==3){
        Cell* ref_cell2 = get_cell(sheet, child->cell2/sheet->cols, child->cell2%sheet->cols);
        remove_child(ref_cell2, key);
    }
}

// Recursively collects all keys from an AVL tree in-order into a dynamic array.
// keys: pointer to an int* (array of keys)
// count: current count of keys
// capacity: current capacity of the array
// Helper: recursively collects keys from an AVL tree.
static void avl_collect_keys(AVLTree root, int **keys, int *count, int *capacity) {
    if (!root)
        return;
    avl_collect_keys(root->left, keys, count, capacity);
    if (*count >= *capacity) {
        *capacity *= 2;
        *keys = realloc(*keys, (*capacity) * sizeof(int));
    }
    (*keys)[(*count)++] = root->key;
    avl_collect_keys(root->right, keys, count, capacity);
}

// Revised reevaluate_topologically using in-degree (Kahn’s algorithm) and encoded keys.
// Prototype: void reevaluate_topologically(Spreadsheet* sheet, short modRow, short modCol)
void reevaluate_topologically(Spreadsheet* sheet, short modRow, short modCol) {
    int total_cells = sheet->rows * sheet->cols;
    // Allocate a visited array (one bool per cell) indexed by key.
    bool *visited = calloc(total_cells, sizeof(bool));
    if (!visited) {
        fprintf(stderr, "Failed to allocate visited array\n");
        return;
    }
    
    // Use a DFS stack (storing int keys) to collect affected cells.
    int *stack = malloc(total_cells * sizeof(int));
    if (!stack) { free(visited); return; }
    int stackTop = 0;
    int modKey = modRow * sheet->cols + modCol;
    stack[stackTop++] = modKey;
    
    // Array to hold affected cell keys.
    int *affected = malloc(total_cells * sizeof(int));
    if (!affected) { free(stack); free(visited); return; }
    int affectedCount = 0;
    
    while (stackTop > 0) {
        int curKey = stack[--stackTop];
        if (!visited[curKey]) {
            visited[curKey] = true;
            affected[affectedCount++] = curKey;
            short curRow = curKey / sheet->cols;
            short curCol = curKey % sheet->cols;
            Cell* curCell = get_cell(sheet, curRow, curCol);
            // Traverse children stored in AVL tree.
            int *childKeys = malloc(8 * sizeof(int));
            if (!childKeys) continue;
            int childCount = 0, childCapacity = 8;
            avl_collect_keys(curCell->children, &childKeys, &childCount, &childCapacity);
            for (int i = 0; i < childCount; i++) {
                int childKey = childKeys[i];
                if(!visited[childKey])
                    stack[stackTop++] = childKey;
            }
            free(childKeys);
        }
    }
    free(stack);
    
    // Build lookup: map each key to its index in the affected array.
    int *lookup = malloc(total_cells * sizeof(int));
    for (int i = 0; i < total_cells; i++)
        lookup[i] = -1;
    for (int i = 0; i < affectedCount; i++) {
        lookup[affected[i]] = i;
    }
    
    // Compute in-degree for affected cells.
    int *inDegree = calloc(affectedCount, sizeof(int));
    // For each affected cell, we scan its dependency info stored in, say, cell->cell1 and cell->cell2.
    // (This part must match your dependency storage. Here we assume that if cell->formula is an arithmetic
    // operation (codes 1-4), then cell->cell1 and cell->cell2 hold the encoded keys of its dependencies.)
    for (int i = 0; i < affectedCount; i++) {
        int key = affected[i];
        short r = key / sheet->cols;
        short c = key % sheet->cols;
        Cell* cell = get_cell(sheet, r, c);
        // For demonstration, assume arithmetic formulas use cell->cell1 and cell->cell2:
        short rem = cell->formula%10;
        if (rem==0) {
            if(lookup[cell->cell1] != -1)
                inDegree[i]++;
            if(lookup[cell->cell2] != -1)
                inDegree[i]++;
        }
        else if(rem==2){
            if(lookup[cell->cell1] != -1)
                inDegree[i]++;
        }
        else if(rem==3){
            if(lookup[cell->cell2] != -1)
                inDegree[i]++;
        }
        // For range formulas (codes 5-9), iterate over the range:
        else if (cell->formula >= 5 && cell->formula <= 9) {
            short startRow = cell->cell1 / sheet->cols;
            short startCol = cell->cell1 % sheet->cols;
            short endRow   = cell->cell2 / sheet->cols;
            short endCol   = cell->cell2 % sheet->cols;
            for (short rr = startRow; rr <= endRow; rr++) {
                for (short cc = startCol; cc <= endCol; cc++) {
                    int parentKey = (int)(rr * sheet->cols + cc);
                    if (lookup[parentKey] != -1)
                        inDegree[i]++;
                }
            }
        }
        // Else, inDegree stays 0.
    }
    
    // Prepare queue for Kahn's algorithm.
    int *queue = malloc(affectedCount * sizeof(int));
    int qFront = 0, qRear = 0;
    for (int i = 0; i < affectedCount; i++) {
        if (inDegree[i] == 0)
            queue[qRear++] = affected[i];
    }
    
    // Process cells in topological order.
    while (qFront < qRear) {
        int curKey = queue[qFront++];
        short r = curKey / sheet->cols;
        short c = curKey % sheet->cols;
        Cell* curCell = get_cell(sheet, r, c);
        double dummySleep = 0.0;
        reevaluate_formula(sheet, curCell, &dummySleep);
        // For each affected cell, if it depends on curKey, decrement its in-degree.
        for (int i = 0; i < affectedCount; i++) {
            int key = affected[i];
            Cell* cell = get_cell(sheet, key / sheet->cols, key % sheet->cols);
            bool depends = false;
            short rem = cell->formula%10;
            if(rem==0){
                if (cell->cell1 == curKey || cell->cell2 == curKey)
                    depends = true;
            }
            else if(rem==2){
                if (cell->cell1 == curKey)
                    depends = true;
            }
            else if(rem==3){
                if (cell->cell2 == curKey)
                    depends = true;
            }
            else if (cell->formula >= 5 && cell->formula <= 9) {
                short startRow = cell->cell1 / sheet->cols;
                short startCol = cell->cell1 % sheet->cols;
                short endRow   = cell->cell2 / sheet->cols;
                short endCol   = cell->cell2 % sheet->cols;
                if (r >= startRow && r <= endRow &&
                    c >= startCol && c <= endCol)
                {
                    depends = true;
                }
            }
            if (depends) {
                inDegree[i]--;
                if (inDegree[i] == 0)
                    queue[qRear++] = affected[i];
            }
        }
    }
    
    free(queue);
    free(inDegree);
    free(lookup);
    free(affected);
    free(visited);
}

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

CommandStatus sum_value(Spreadsheet* sheet, Cell *cell){
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
        cell->value = sum;
        return CMD_OK;
}

CommandStatus variance(Spreadsheet* sheet, Cell *cell){
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
    return CMD_OK;
}

CommandStatus min_max(Spreadsheet* sheet, Cell* cell, bool is_min){
    int max = INT_MIN;
    int min = INT_MAX; 
    short row1, col1, row2, col2;
    get_row_col(cell->cell1, &row1, &col1, sheet->cols);
    get_row_col(cell->cell2, &row2, &col2, sheet->cols);
    for(int i=row1; i<=row2; i++){
        for(int j=col1; j<=col2; j++){
            Cell* ref_cell = get_cell(sheet, i, j);
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
    return CMD_OK;
}

CommandStatus sleep_prog(Spreadsheet* sheet, Cell *current,double* sleep_time){
    Cell* ref_cell = get_cell_check(sheet, current->cell1);
    if (ref_cell->error_state){
        current->error_state = true;
        return CMD_OK;
    }
    if(ref_cell->value < 0){
        current->value = ref_cell->value;
        return CMD_OK;
    }

    current->value = ref_cell->value;
    *sleep_time = current->value;
    return CMD_OK;
}

CommandStatus handle_sleep(Spreadsheet* sheet, short row, short col, const char* expr, double* sleep_time) {
    size_t len = strlen(expr);
    if (len < 7)
        return CMD_UNRECOGNIZED;  // Must be at least "SLEEP(x)" (7 characters)

    // Allocate buffer for inner argument (between "SLEEP(" and the final ')')
    char* sleep_arg = malloc(len - 6);  // (len-6) bytes, including space for null terminator
    if (!sleep_arg)
        return CMD_UNRECOGNIZED;
    strncpy(sleep_arg, expr + 6, len - 7);
    sleep_arg[len - 7] = '\0';

    int value = 0;
    Cell* current = get_cell(sheet, row, col);

    // If the argument begins with an alphabetic character, treat it as a cell reference.
    if (isalpha((unsigned char)sleep_arg[0])) {
        short ref_row, ref_col;
        parse_cell_reference(sleep_arg, &ref_row, &ref_col);
        free(sleep_arg);
        if (ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols)
            return CMD_INVALID_CELL;
        // Remove any existing dependency links.
        remove_all_parents(sheet, row, col);
        Cell* ref_cell = get_cell(sheet, ref_row, ref_col);
        // Establish dependency: add current cell as a child of the referenced cell.
        add_child(ref_cell, row, col, sheet->cols);
        // Update current cell's cell1 field to store the reference.
        current->cell1 = encode_cell_key(ref_row, ref_col, sheet->cols);
        value = ref_cell->value;
        // Use opcode 102 for SLEEP with cell reference.
        current->formula = 102;
    } else {
        // Otherwise, try to parse a numeric argument.
        char* endptr;
        long num = strtol(sleep_arg, &endptr, 10);
        // Strict validation: if any non-numeric character remains, error out.
        if (*endptr != '\0') {
            free(sleep_arg);
            return CMD_UNRECOGNIZED;
        }
        // Remove any existing dependency links.
        remove_all_parents(sheet, row, col);
        value = (int)num;
        free(sleep_arg);
        current->formula = -1;
    }
    current->value = value;
    sleep_prog(sheet, current, sleep_time);
    return CMD_OK;
}

/* ---------- (Stub) Reevaluate Formula ---------- */
CommandStatus reevaluate_formula(Spreadsheet* sheet, Cell* cell, double* sleep_time) {
    short msb = cell->formula/10;
    short rem = cell->formula%10;
    CommandStatus status = CMD_OK;
    if (cell->formula==-1) return status;
    // ADD
    else if (cell->formula==82){
        Cell* ref_cell = get_cell_check(sheet, cell->cell1);
        if(ref_cell->error_state){
            cell->error_state = 1;
            return CMD_OK;
        }
        cell->value = ref_cell->value;
    }
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
    // MULT
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
    else if(rem==5){
        status = sum_value(sheet, cell);
    }
    // AVG
    else if(rem==6){
        status = sum_value(sheet, cell);
        int count = 0;
        short row1, col1, row2, col2;
        get_row_col(cell->cell1, &row1, &col1, sheet->cols);
        get_row_col(cell->cell2, &row2, &col2, sheet->cols);
        count = (row2-row1+1)*(col2-col1+1);
        cell->value = cell->value /count;
    }
    // MIN
    else if(rem==7){
        status = min_max(sheet, cell, true);
    }
    // MAX
    else if(rem==8){
        status = min_max(sheet, cell, false);
    }
    // STDEV
    else if(rem==9){
        status = variance(sheet, cell);
    }
    else if(rem==2){
        status = sleep_prog(sheet, cell, sleep_time);
    }
    return status;
}

// Updated evaluate_formula:
// The parameters: sheet, pointer to the current cell, its position (row, col), 
// the expression string, and a sleep_time pointer.
// We assume that if a cell has no formula, cell->formula is -1.
CommandStatus evaluate_formula(Spreadsheet* sheet, Cell* cell, short row, short col, const char* expr, double* sleep_time) {
    int expr_len = strlen(expr);
    if(expr_len == 0) {
        return CMD_UNRECOGNIZED;
    }

    // === Range-based functions: SUM, AVG, MIN, MAX, STDEV ===
    if (strncmp(expr, "SUM(", 4) == 0 || strncmp(expr, "AVG(", 4) == 0 ||
        strncmp(expr, "MIN(", 4) == 0 || strncmp(expr, "MAX(", 4) == 0 ||
        strncmp(expr, "STDEV(", 6) == 0) {
        
        bool is_avg = (strncmp(expr, "AVG(", 4) == 0);
        bool is_min = (strncmp(expr, "MIN(", 4) == 0);
        bool is_max = (strncmp(expr, "MAX(", 4) == 0);
        bool is_stdev = (strncmp(expr, "STDEV(", 6) == 0);
        int prefix_len = is_stdev ? 6 : 4;

        if(expr[expr_len - 1] != ')')
            return CMD_UNRECOGNIZED;

        char* range_str = strndup(expr + prefix_len, expr_len - prefix_len - 1);
        if (!range_str)
            return CMD_UNRECOGNIZED;
        Range range;
        CommandStatus rstatus = parse_range(range_str, &range);
        free(range_str);
        if(rstatus != CMD_OK)
            return rstatus;

        // Remove old dependencies (if any)
        remove_all_parents(sheet, row, col);

        // For each cell in the range, add the current cell as a dependent.
        int cell_key = encode_cell_key(row, col, sheet->cols);
        for (short r = range.start_row; r <= range.end_row; r++) {
            for (short c = range.start_col; c <= range.end_col; c++) {
                Cell* ref_cell = get_cell(sheet, r, c);
                ref_cell->children = avl_insert(ref_cell->children, cell_key);
            }
        }
        // Save the range info in the cell dependency fields.
        cell->cell1 = encode_cell_key(range.start_row, range.start_col, sheet->cols);
        cell->cell2 = encode_cell_key(range.end_row, range.end_col, sheet->cols);

        // Set the formula code and perform evaluation.
        if (is_stdev) {
            cell->formula = 9;
            variance(sheet, cell);
        } else if (is_max) {
            cell->formula = 8;
            min_max(sheet, cell, false);
        } else if (is_min) {
            cell->formula = 7;
            min_max(sheet, cell, true);
        } else if (is_avg) {
            cell->formula = 6;
            sum_value(sheet, cell);
            int count = (range.end_row - range.start_row + 1) * (range.end_col - range.start_col + 1);
            cell->value = cell->value / count;
        } else { // SUM
            cell->formula = 5;
            sum_value(sheet, cell);
        }
        return CMD_OK;
    }
   
    // === SLEEP function (if implemented) ===
    else if (strncmp(expr, "SLEEP(", 6) == 0) {
        return handle_sleep(sheet, row, col, expr, sleep_time);
    }

    // === Pure integer literal ===
    char* endptr;
    long number = strtol(expr, &endptr, 10);
    if(*endptr == '\0') {
        remove_all_parents(sheet, row, col);
        cell->value = (int)number;
        cell->formula = -1;
        cell->error_state = false;
        return CMD_OK;
    }

    // === Simple cell reference (all alphanumeric) ===
    bool all_alnum = true;
    for (int i = 0; i < expr_len; i++) {
        if (!isalnum((unsigned char)expr[i])) {
            all_alnum = false;
            break;
        }
    }
    if (all_alnum) {
        short ref_row, ref_col;
        parse_cell_reference(expr, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols)
            return CMD_INVALID_CELL;
        remove_all_parents(sheet, row, col);
        cell->formula = 82;  // Code for simple cell reference.
        cell->error_state = false;
        Cell* ref_cell = get_cell(sheet, ref_row, ref_col);
        // Add current cell as dependent of ref_cell.
        add_child(ref_cell, row, col, sheet->cols);
        cell->cell1 = encode_cell_key(ref_row, ref_col, sheet->cols);
        if(ref_cell->error_state)
            cell->error_state = true;
        else
            cell->value = ref_cell->value;
        return CMD_OK;
    }


    // === Binary arithmetic expression ===
    // Find operator starting at index 1 to avoid confusion with a leading minus sign.
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
    if(op_index == -1)
        return CMD_UNRECOGNIZED;

    // Split into left and right operands.
    char* left_str = strndup(expr, op_index);
    char* right_str = strdup(expr + op_index + 1);
    if (!left_str || !right_str) {
        free(left_str);
        free(right_str);
        return CMD_UNRECOGNIZED;
    }
    int left_val = 0, right_val = 0;
    bool left_is_cell = false, right_is_cell = false;
    bool error_found = false;

    // Evaluate left operand.
    char* left_endptr;
    long left_num = strtol(left_str, &left_endptr, 10);
    int ref_cell_left = -1;
    if (*left_endptr == '\0') {
        left_val = (int)left_num;
    } else {
        short ref_row, ref_col;
        parse_cell_reference(left_str, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
            free(left_str); free(right_str);
            return CMD_INVALID_CELL;
        }
        left_is_cell = true;
        Cell* ref_cell = get_cell(sheet, ref_row, ref_col);
        if(ref_cell->error_state)
            error_found = true;
        ref_cell_left = encode_cell_key(ref_row, ref_col, sheet->cols);
        left_val = ref_cell->value;
    }

    // Evaluate right operand.
    int ref_cell_right = -1;
    char* right_endptr;
    long right_num = strtol(right_str, &right_endptr, 10);
    if (*right_endptr == '\0') {
        right_val = (int)right_num;
    } else {
        short ref_row, ref_col;
        parse_cell_reference(right_str, &ref_row, &ref_col);
        if(ref_row < 0 || ref_row >= sheet->rows || ref_col < 0 || ref_col >= sheet->cols) {
            free(left_str); free(right_str);
            return CMD_INVALID_CELL;
        }
        right_is_cell = true;
        Cell* ref_cell = get_cell(sheet, ref_row, ref_col);
        if(ref_cell->error_state)
            error_found = true;
        ref_cell_right = encode_cell_key(ref_row, ref_col, sheet->cols);
        right_val = ref_cell->value;
    }

    free(left_str);
    free(right_str);

    // Remove old dependencies (only now; we did not update parent's for arithmetic operands before).
    remove_all_parents(sheet, row, col);
    // (For each operand that is a cell, add current cell as dependent of that operand.)
    if (left_is_cell) {
        cell->cell1 = ref_cell_left;                  // Save the left ref cell key as left operand.
        Cell* ref_cell = get_cell_check(sheet, ref_cell_left); 
        add_child(ref_cell, row, col, sheet->cols);  // Add current cell as child of ref_cell.
    }
    else
        cell->cell1 = left_val;                      // Save the value as the left operand.

    if (right_is_cell) {
        cell->cell2 = ref_cell_right;                // Save the right ref cell key as right operand.
        Cell* ref_cell = get_cell_check(sheet, ref_cell_right);
        add_child(ref_cell, row, col, sheet->cols);  // Add current cell as child of ref_cell.
    }
    else
        cell->cell2 = right_val;                     // Save the value as the right operand.

    // Set error state if any operand is an error.
    if (error_found) {
        cell->error_state = true;
    } else {
        cell->error_state = false;
    }

    // Set formula code based on operator and whether operands were cells.
    switch(op_char) {
        case '+': 
            cell->value = left_val + right_val;
            if(left_is_cell && right_is_cell)
                cell->formula = 10;
            else if(left_is_cell)
                cell->formula = 12;
            else if(right_is_cell)
                cell->formula = 13;
            break;
        case '-': 
            cell->value = left_val - right_val;
            if(left_is_cell && right_is_cell)
                cell->formula = 20;
            else if(left_is_cell)
                cell->formula = 22;
            else if(right_is_cell)
                cell->formula = 23;
            break;
        case '*': 
            cell->value = left_val * right_val;
            if(left_is_cell && right_is_cell)
                cell->formula = 40;
            else if(left_is_cell)
                cell->formula = 42;
            else if(right_is_cell)
                cell->formula = 43;
            break;
        case '/':
            if(right_val == 0) {
                cell->error_state = true;
                return CMD_DIV_BY_ZERO;
            }
            if(!error_found)
                cell->value = left_val / right_val;
            if(left_is_cell && right_is_cell)
                cell->formula = 30;
            else if(left_is_cell)
                cell->formula = 32;
            else if(right_is_cell)
                cell->formula = 33;
            break;
        default:
            return CMD_UNRECOGNIZED;
    }

    return CMD_OK;
}

/* ---------- Command Handling (Simplified) ---------- */
CommandStatus set_cell_value(Spreadsheet* sheet, short row, short col, const char* expr, double* sleep_time) {
    Cell* cell = get_cell(sheet, row, col);

    // Evaluate as a formula.
    CommandStatus status = evaluate_formula(sheet, cell, row, col, expr, sleep_time);
    reevaluate_topologically(sheet, row, col);
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
        avl_free(cell->children);
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
    CommandStatus status;
    while (1) {
        print_spreadsheet(sheet);
        printf("[%.1f] (%s) > ", last_time, last_status);
        
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        if (strcmp(input, "q") == 0) break;
        
        start = clock();
        status = handle_command(sheet, input, &sleep_time);
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