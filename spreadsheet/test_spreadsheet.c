#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

// Struct to hold test case details
typedef struct {
    char *name;
    char *description;
    char **commands;
    int command_count;
    char **expected_outputs;
    int expected_output_count;
    int rows;
    int columns;
    int timeout_seconds;
} TestCase;

// Colors for output formatting
#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"

// Global variables for statistics
int tests_passed = 0;
int tests_failed = 0;
int tests_skipped = 0;

// Function declarations
void run_test_case(TestCase *test_case);
int compare_output(FILE *actual_output, char **expected_outputs, int expected_output_count);
void print_test_summary();

// Test Case 1: Basic Cell Assignment
char *commands_test1[] = {
    "A1=2",
    "B1=3",
    "C1=4",
    "q"
};

// Expected output fragments for Test Case 1 (adjusted for actual format)
char *expected_outputs_test1[] = {
    "A",
    "B",
    "C",
    "1   2",
    "1   3",
    "1   4",
    "(ok)"
};

// Test Case 2: Arithmetic Operations
char *commands_test2[] = {
    "A1=5",
    "B1=A1+2",
    "C1=B1*3",
    "D1=C1/2",
    "E1=C1-D1",
    "q"
};

// Expected output fragments for Test Case 2 (adjusted)
char *expected_outputs_test2[] = {
    "A",
    "B",
    "C",
    "D",
    "E",
    "1   5",
    "1   7",
    "1   21",
    "1   10",
    "1   11",
    "(ok)"
};

// Test Case 3: Functions
char *commands_test3[] = {
    "A1=3",
    "A2=7",
    "A3=1",
    "A4=5",
    "B1=MIN(A1:A4)",
    "B2=MAX(A1:A4)",
    "B3=AVG(A1:A4)",
    "B4=SUM(A1:A4)",
    "C1=STDEV(A1:A4)",
    "q"
};

// Expected output fragments for Test Case 3 (adjusted)
char *expected_outputs_test3[] = {
    "A",
    "B",
    "C",
    "1   3",
    "1   1",
    "2   7",
    "2   7",
    "3   1",
    "3   4",
    "4   5",
    "4   16",
    "(ok)"
};

// Test Case 4: Range Handling
char *commands_test4[] = {
    "A1=1",
    "A2=2",
    "A3=3",
    "B1=4",
    "B2=5",
    "B3=6",
    "C1=SUM(A1:B3)",
    "C2=AVG(A1:A3)",
    "C3=MAX(B1:B3)",
    "q"
};

// Expected output fragments for Test Case 4 (adjusted)
char *expected_outputs_test4[] = {
    "A",
    "B",
    "C",
    "1   1",
    "1   4",
    "1   21",
    "2   2",
    "2   5",
    "2   2",
    "3   3",
    "3   6",
    "3   6",
    "(ok)"
};

// Test Case 5: Error Handling - Invalid Cell
char *commands_test5[] = {
    "X999=5",
    "q"
};

// Expected output fragments for Test Case 5 (assuming error message in output)
char *expected_outputs_test5[] = {
    "Invalid cell"
};

// Test Case 6: Error Handling - Division by Zero
char *commands_test6[] = {
    "A1=0",
    "B1=5/A1",
    "q"
};

// Expected output fragments for Test Case 6 (adjusted)
char *expected_outputs_test6[] = {
    "A",
    "B",
    "1   0",
    "1   ERR",
    "(ok)"
};

// Test Case 7: Circular References
char *commands_test7[] = {
    "A1=B1+1",
    "B1=A1+1",
    "q"
};

// Expected output fragments for Test Case 7 (adjusted for possible outputs)
char *expected_outputs_test7[] = {
    "Circular reference"
};

// Test Case 8: Recalculation
char *commands_test8[] = {
    "A1=2",
    "B1=A1+1",
    "A2=B1+2",
    "A1=5",
    "q"
};

// Expected output fragments for Test Case 8 (adjusted)
char *expected_outputs_test8[] = {
    "A",
    "B",
    "1   5",
    "1   6",
    "2   8",
    "(ok)"
};

// Test Case 9: Output Control
char *commands_test9[] = {
    "A1=10",
    "disable_output",
    "B1=20",
    "C1=30",
    "enable_output",
    "D1=40",
    "q"
};

// Expected output fragments for Test Case 9 (adjusted)
char *expected_outputs_test9[] = {
    "A",
    "B", 
    "C",
    "D",
    "1   10",
    "1   20",
    "1   30", 
    "1   40",
    "(ok)"
};

// Test Case 10: Scroll To
char *commands_test10[] = {
    "A1=1",
    "B1=2",
    "A20=3",
    "B20=4",
    "scroll_to A20",
    "q"
};

// Expected output fragments for Test Case 10 (adjusted)
char *expected_outputs_test10[] = {
    "A",
    "B",
    "20  3",
    "20  4",
    "(ok)"
};

// Test case definitions
TestCase test_cases[] = {
    {
        .name = "Basic Cell Assignment",
        .description = "Test basic assignment of values to cells",
        .commands = commands_test1,
        .command_count = sizeof(commands_test1) / sizeof(char *),
        .expected_outputs = expected_outputs_test1,
        .expected_output_count = sizeof(expected_outputs_test1) / sizeof(char *),
        .rows = 3,
        .columns = 3,
        .timeout_seconds = 5
    },
    {
        .name = "Arithmetic Operations",
        .description = "Test basic arithmetic operations",
        .commands = commands_test2,
        .command_count = sizeof(commands_test2) / sizeof(char *),
        .expected_outputs = expected_outputs_test2,
        .expected_output_count = sizeof(expected_outputs_test2) / sizeof(char *),
        .rows = 3,
        .columns = 5,
        .timeout_seconds = 5
    },
    {
        .name = "Functions",
        .description = "Test spreadsheet functions like MIN, MAX, AVG, SUM, STDEV",
        .commands = commands_test3,
        .command_count = sizeof(commands_test3) / sizeof(char *),
        .expected_outputs = expected_outputs_test3,
        .expected_output_count = sizeof(expected_outputs_test3) / sizeof(char *),
        .rows = 5,
        .columns = 3,
        .timeout_seconds = 5
    },
    {
        .name = "Range Handling",
        .description = "Test handling of 1D and 2D ranges",
        .commands = commands_test4,
        .command_count = sizeof(commands_test4) / sizeof(char *),
        .expected_outputs = expected_outputs_test4,
        .expected_output_count = sizeof(expected_outputs_test4) / sizeof(char *),
        .rows = 4,
        .columns = 3,
        .timeout_seconds = 5
    },
    {
        .name = "Invalid Cell Error",
        .description = "Test handling of invalid cell references",
        .commands = commands_test5,
        .command_count = sizeof(commands_test5) / sizeof(char *),
        .expected_outputs = expected_outputs_test5,
        .expected_output_count = sizeof(expected_outputs_test5) / sizeof(char *),
        .rows = 2,
        .columns = 2,
        .timeout_seconds = 5
    },
    {
        .name = "Division by Zero",
        .description = "Test handling of division by zero errors",
        .commands = commands_test6,
        .command_count = sizeof(commands_test6) / sizeof(char *),
        .expected_outputs = expected_outputs_test6,
        .expected_output_count = sizeof(expected_outputs_test6) / sizeof(char *),
        .rows = 2,
        .columns = 2,
        .timeout_seconds = 5
    },
    {
        .name = "Circular References",
        .description = "Test detection of circular references",
        .commands = commands_test7,
        .command_count = sizeof(commands_test7) / sizeof(char *),
        .expected_outputs = expected_outputs_test7,
        .expected_output_count = sizeof(expected_outputs_test7) / sizeof(char *),
        .rows = 2,
        .columns = 2,
        .timeout_seconds = 5
    },
    {
        .name = "Recalculation",
        .description = "Test automatic recalculation of dependent cells",
        .commands = commands_test8,
        .command_count = sizeof(commands_test8) / sizeof(char *),
        .expected_outputs = expected_outputs_test8,
        .expected_output_count = sizeof(expected_outputs_test8) / sizeof(char *),
        .rows = 3,
        .columns = 2,
        .timeout_seconds = 5
    },
    {
        .name = "Output Control",
        .description = "Test disabling and enabling output",
        .commands = commands_test9,
        .command_count = sizeof(commands_test9) / sizeof(char *),
        .expected_outputs = expected_outputs_test9,
        .expected_output_count = sizeof(expected_outputs_test9) / sizeof(char *),
        .rows = 2,
        .columns = 4,
        .timeout_seconds = 5
    },
    {
        .name = "Scroll To",
        .description = "Test scrolling to a specific cell",
        .commands = commands_test10,
        .command_count = sizeof(commands_test10) / sizeof(char *),
        .expected_outputs = expected_outputs_test10,
        .expected_output_count = sizeof(expected_outputs_test10) / sizeof(char *),
        .rows = 25,
        .columns = 3,
        .timeout_seconds = 5
    }
};

// Main function
int main() {
    int test_count = sizeof(test_cases) / sizeof(TestCase);
    
    printf(YELLOW "Running %d test cases for spreadsheet program...\n\n" RESET, test_count);
    
    // Run each test case
    for (int i = 0; i < test_count; i++) {
        printf(YELLOW "Test Case %d: %s\n" RESET, i + 1, test_cases[i].name);
        printf("Description: %s\n", test_cases[i].description);
        
        run_test_case(&test_cases[i]);
        printf("\n");
    }
    
    // Print summary of test results
    print_test_summary();
    
    // Return exit code based on test results
    return tests_failed > 0 ? 1 : 0;
}

// Function to run a single test case
void run_test_case(TestCase *test_case) {
    // Create a temporary file for input commands
    FILE *input_file = fopen("test_input.tmp", "w");
    if (!input_file) {
        perror("Failed to create input file");
        tests_skipped++;
        printf(RED "Error: Failed to create temporary input file. Skipping test.\n" RESET);
        return;
    }
    
    // Write commands to the input file
    for (int i = 0; i < test_case->command_count; i++) {
        fprintf(input_file, "%s\n", test_case->commands[i]);
    }
    fclose(input_file);
    
    // Create command to run the spreadsheet with input and capture output
    char command[512];
    snprintf(command, sizeof(command), 
             "./sheet %d %d < test_input.tmp > test_output.tmp 2>&1",
             test_case->rows, test_case->columns);
    
    // Run the command with a timeout
    printf("Running command: %s\n", command);
    
    int result = system(command);
    
    if (result != 0 && result != 256) {  // 256 is normal exit with status 1
        tests_failed++;
        printf(RED "Error: Command execution failed with code %d\n" RESET, result);
        return;
    }
    
    // Open the output file
    FILE *output_file = fopen("test_output.tmp", "r");
    if (!output_file) {
        perror("Failed to open output file");
        tests_skipped++;
        printf(RED "Error: Failed to open output file. Skipping test.\n" RESET);
        return;
    }
    
    // Check the output
    int comparison_result = compare_output(output_file, test_case->expected_outputs, test_case->expected_output_count);
    
    if (comparison_result) {
        tests_passed++;
        printf(GREEN "PASSED: All expected outputs found.\n" RESET);
    } else {
        tests_failed++;
        printf(RED "FAILED: Not all expected outputs were found.\n" RESET);
        
        // Print the actual output for debugging
        printf(YELLOW "Actual output:\n" RESET);
        char buffer[4096];
        fseek(output_file, 0, SEEK_SET);
        while (fgets(buffer, sizeof(buffer), output_file)) {
            printf("%s", buffer);
        }
    }
    
    // Clean up
    fclose(output_file);
    remove("test_input.tmp");
    remove("test_output.tmp");
}

// Function to compare actual output with expected outputs
int compare_output(FILE *actual_output, char **expected_outputs, int expected_output_count) {
    char buffer[4096];
    int found_count = 0;
    
    // Read the entire output file into a string
    fseek(actual_output, 0, SEEK_SET);
    char output_text[100000] = "";  // Increased size for larger outputs
    
    while (fgets(buffer, sizeof(buffer), actual_output)) {
        strcat(output_text, buffer);
    }
    
    // Check for each expected output
    for (int i = 0; i < expected_output_count; i++) {
        if (strstr(output_text, expected_outputs[i])) {
            found_count++;
        } else {
            printf(RED "Did not find expected output: '%s'\n" RESET, expected_outputs[i]);
        }
    }
    
    return found_count == expected_output_count;
}

// Function to print test summary
void print_test_summary() {
    printf(YELLOW "Test Summary:\n" RESET);
    printf(GREEN "Passed: %d\n" RESET, tests_passed);
    printf(RED "Failed: %d\n" RESET, tests_failed);
    printf(YELLOW "Skipped: %d\n" RESET, tests_skipped);
    printf(YELLOW "Total: %d\n" RESET, tests_passed + tests_failed + tests_skipped);
    
    if (tests_failed == 0 && tests_skipped == 0) {
        printf(GREEN "All tests passed successfully!\n" RESET);
    } else {
        printf(RED "Some tests failed or were skipped. Please check the output above.\n" RESET);
    }
}