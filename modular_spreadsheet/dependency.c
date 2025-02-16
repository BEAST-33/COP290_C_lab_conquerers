#include <stdlib.h>
#include "dependency.h"

// Helper function for topological sorting.
void topological_sort(Cell* cell, bool* visited, Cell** sorted_cells, int* index) {
    if (visited[cell->id]) return;
    
    visited[cell->id] = true;
    
    for (int i = 0; i < cell->dependent_count; i++) {
        topological_sort(cell->dependents[i], visited, sorted_cells, index);
    }
    
    sorted_cells[(*index)--] = cell; // Add to sorted list
}

// Recalculate dependencies using topological sorting.
void recalculate_dependencies(Spreadsheet* sheet) {
    bool* visited = calloc(sheet->rows * sheet->cols, sizeof(bool));
    Cell** sorted_cells = malloc(sheet->rows * sheet->cols * sizeof(Cell*));
    
    int index = sheet->rows * sheet->cols - 1;
    
    for (int i = 0; i < sheet->rows; i++) {
        for (int j = 0; j < sheet->cols; j++) {
            topological_sort(sheet->grid[i][j], visited, sorted_cells, &index);
        }
    }
    
    // Recalculate cells in topological order.
    for (int i = 0; i < sheet->rows * sheet->cols; i++) {
        if (sorted_cells[i] != NULL && sorted_cells[i]->formula != NULL) {
            set_cell_value(sheet, sorted_cells[i]->row, sorted_cells[i]->col,
                           sorted_cells[i]->formula);
        }
    }
    
    free(visited);
    free(sorted_cells);
}
