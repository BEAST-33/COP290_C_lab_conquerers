#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
    // Check if the sheet executable exists
    if (access("./sheet", F_OK) == -1) {
        printf("Error: 'sheet' executable not found. Did you run 'make' first?\n");
        return 1;
    }
    
    // Run the test suite
    printf("Running spreadsheet tests...\n");
    
    // Compile the test suite with appropriate flags
    int compile_result = system("gcc -o test_spreadsheet test_spreadsheet.c -Wall -D_GNU_SOURCE");
    if (compile_result != 0) {
        printf("Error: Failed to compile test suite.\n");
        return 1;
    }
    
    // Run the test suite
    int run_result = system("./test_spreadsheet");
    
    // Clean up
    system("rm -f test_spreadsheet test_input.tmp test_output.tmp");
    
    return run_result;
}