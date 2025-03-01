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
void setup_test_case(TestCase *test_case);
void cleanup_test_case(TestCase *test_case);
int compare_output(FILE *actual_output, char **expected_outputs, int expected_output_count);
void print_test_summary();

// Commands for Test Case 1: Basic Cell Assignment
char *commands_test1[] = {
    "A1=2",
    "B1=3",
    "C1=4",
    "q"
};

// Expected output fragments for Test Case 1
char *expected_outputs_test1[] = {
    "A B C",
    "1 2 3 4",
    "(ok)"
};

// Commands for Test Case 2: Arithmetic Operations
char *commands_test2[] = {
    "A1=5",
    "B1=A1+2",
    "C1=B1*3",
    "D1=C1/2",
    "E1=C1-D1",
    "q"
};

// Expected output fragments for Test Case 2
char *expected_outputs_test2[] = {
    "A B C D E",
    "1 5 7 21 10 11",
    "(ok)"
};

// Commands for Test Case 3: Functions
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

// Expected output fragments for Test Case 3
char *expected_outputs_test3[] = {
    "A B C",
    "1 3 1",
    "2 7 7",
    "3 1",
    "4 5 16",
    "(ok)"
};

// Commands for Test Case 4: Range Handling
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

// Expected output fragments for Test Case 4
char *expected_outputs_test4[] = {
    "A B C",
    "1 1 4 21",
    "2 2 5 2",
    "3 3 6 6",
    "(ok)"
};

// Commands for Test Case 5: Error Handling - Invalid Cell
char *commands_test5[] = {
    "X999=5",
    "q"
};

// Expected output fragments for Test Case 5 (assuming a smaller sheet)
char *expected_outputs_test5[] = {
    "(Invalid cell)"
};

// Commands for Test Case 6: Error Handling - Division by Zero
char *commands_test6[] = {
    "A1=0",
    "B1=5/A1",
    "q"
};

// Expected output fragments for Test Case 6
char *expected_outputs_test6[] = {
    "A B",
    "1 0 ERR",
    "(ok)"
};

// Commands for Test Case 7: Circular References
char *commands_test7[] = {
    "A1=B1+1",
    "B1=A1+1",
    "q"
};

// Expected output fragments for Test Case 7
char *expected_outputs_test7[] = {
    "(Circular reference detected)"
};

// Commands for Test Case 8: Recalculation
char *commands_test8[] = {
    "A1=2",
    "B1=A1+1",
    "A2=B1+2",
    "A1=5",
    "q"
};

// Expected output fragments for Test Case 8
char *expected_outputs_test8[] = {
    "A B",
    "1 5 6",
    "2 8",
    "(ok)"
};

// Commands for Test Case 9: Output Control
char *commands_test9[] = {
    "A1=10",
    "disable_output",
    "B1=20",
    "C1=30",
    "enable_output",
    "D1=40",
    "q"
};

// Expected output fragments for Test Case 9
char *expected_outputs_test9[] = {
    "A B C D",
    "1 10 20 30 40",
    "(ok)"
};

// Commands for Test Case 10: Scroll To
char *commands_test10[] = {
    "A1=1",
    "B1=2",
    "A20=3",
    "B20=4",
    "scroll_to A20",
    "q"
};

// Expected output fragments for Test Case 10
char *expected_outputs_test10[] = {
    "A B",
    "20 3 4",
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
    int pipefd[2];
    pid_t pid;
    FILE *output;
    char command[256];
    
    // Create pipe for communication with child process
    if (pipe(pipefd) == -1) {
        perror("pipe");
        tests_skipped++;
        printf(RED "Error: Failed to create pipe. Skipping test.\n" RESET);
        return;
    }
    
    // Create command to run the spreadsheet program
    snprintf(command, sizeof(command), "./sheet %d %d", test_case->rows, test_case->columns);
    
    // Fork a child process
    pid = fork();
    
    if (pid == -1) {
        perror("fork");
        tests_skipped++;
        printf(RED "Error: Failed to fork process. Skipping test.\n" RESET);
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    
    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end
        
        // Redirect stdout to pipe
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        
        // Redirect stdin to /dev/null to prevent hanging on input
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull == -1 || dup2(devnull, STDIN_FILENO) == -1) {
            perror("Failed to redirect stdin");
            exit(EXIT_FAILURE);
        }
        
        close(pipefd[1]); // Close original pipe fd
        
        // Execute the spreadsheet program
        char *args[] = {"./sheet", malloc(10), malloc(10), NULL};
        snprintf(args[1], 10, "%d", test_case->rows);
        snprintf(args[2], 10, "%d", test_case->columns);
        
        execvp("./sheet", args);
        
        // If execvp returns, there was an error
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        close(pipefd[1]); // Close write end
        
        // Open pipe for reading
        output = fdopen(pipefd[0], "r");
        if (!output) {
            perror("fdopen");
            tests_skipped++;
            printf(RED "Error: Failed to open pipe for reading. Skipping test.\n" RESET);
            close(pipefd[0]);
            return;
        }
        
        // Wait for initial spreadsheet display
        char buffer[4096];
        if (fgets(buffer, sizeof(buffer), output) == NULL) {
            tests_skipped++;
            printf(RED "Error: Failed to read initial output. Skipping test.\n" RESET);
            fclose(output);
            return;
        }
        
        // Send commands to the spreadsheet program
        FILE *input_file = fopen("test_input.tmp", "w");
        if (!input_file) {
            perror("fopen");
            tests_skipped++;
            printf(RED "Error: Failed to create temporary input file. Skipping test.\n" RESET);
            fclose(output);
            return;
        }
        
        for (int i = 0; i < test_case->command_count; i++) {
            fprintf(input_file, "%s\n", test_case->commands[i]);
        }
        
        fclose(input_file);
        
        // Use system to send commands to the sheet process
        char send_command[300];
        snprintf(send_command, sizeof(send_command), "cat test_input.tmp | ./sheet %d %d > test_output.tmp", 
                 test_case->rows, test_case->columns);
        
        // Kill the initial process since we're starting a new one
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        
        // Run the command
        system(send_command);
        
        // Open the output file
        FILE *output_file = fopen("test_output.tmp", "r");
        if (!output_file) {
            perror("fopen");
            tests_skipped++;
            printf(RED "Error: Failed to open output file. Skipping test.\n" RESET);
            return;
        }
        
        // Check the output
        int result = compare_output(output_file, test_case->expected_outputs, test_case->expected_output_count);
        
        if (result) {
            tests_passed++;
            printf(GREEN "PASSED: All expected outputs found.\n" RESET);
        } else {
            tests_failed++;
            printf(RED "FAILED: Not all expected outputs were found.\n" RESET);
            
            // Print the actual output for debugging
            printf(YELLOW "Actual output:\n" RESET);
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
}

// Function to compare actual output with expected outputs
int compare_output(FILE *actual_output, char **expected_outputs, int expected_output_count) {
    char buffer[4096];
    int found_count = 0;
    
    // Read the entire output file into a string
    fseek(actual_output, 0, SEEK_SET);
    char output_text[10000] = "";
    
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