#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define MAX_CMD_LEN 1024
#define MAX_OUTPUT_LEN 10240
#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define RESET "\033[0m"

typedef struct {
    char *name;
    char *description;
    int rows;
    int cols;
    char **commands;
    int num_commands;
    char *expected_output;
} TestCase;

// Function to execute a command and capture its output
char* execute_sheet_command(int rows, int cols, char **commands, int num_commands) {
    int pipe_in[2], pipe_out[2];
    pid_t pid;
    char command[MAX_CMD_LEN];
    
    if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
    
    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    
    if (pid == 0) { // Child process
        // Redirect stdin to pipe_in
        close(pipe_in[1]);
        dup2(pipe_in[0], STDIN_FILENO);
        close(pipe_in[0]);
        
        // Redirect stdout to pipe_out
        close(pipe_out[0]);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_out[1]);
        
        // Execute the sheet program
        sprintf(command, "./sheet %d %d", rows, cols);
        execlp("sh", "sh", "-c", command, NULL);
        
        perror("execlp");
        exit(EXIT_FAILURE);
    } else { // Parent process
        close(pipe_in[0]);
        close(pipe_out[1]);
        
        // Write commands to the child process
        for (int i = 0; i < num_commands; i++) {
            write(pipe_in[1], commands[i], strlen(commands[i]));
            write(pipe_in[1], "\n", 1);
        }
        
        // Add quit command
        write(pipe_in[1], "q\n", 2);
        close(pipe_in[1]);
        
        // Read output from the child process
        char *output = malloc(MAX_OUTPUT_LEN);
        if (!output) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        
        ssize_t bytes_read = read(pipe_out[0], output, MAX_OUTPUT_LEN - 1);
        if (bytes_read == -1) {
            perror("read");
            free(output);
            exit(EXIT_FAILURE);
        }
        
        output[bytes_read] = '\0';
        close(pipe_out[0]);
        
        // Wait for the child process to finish
        waitpid(pid, NULL, 0);
        
        return output;
    }
}

// Function to compare expected and actual output
int compare_output(const char *expected, const char *actual) {
    // Create temporary files for expected and actual output
    FILE *expected_file = fopen("expected.tmp", "w");
    FILE *actual_file = fopen("actual.tmp", "w");
    
    if (!expected_file || !actual_file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    
    fprintf(expected_file, "%s", expected);
    fprintf(actual_file, "%s", actual);
    
    fclose(expected_file);
    fclose(actual_file);
    
    // Compare using diff
    int result = system("diff -u expected.tmp actual.tmp > diff.tmp");
    
    // Cleanup
    unlink("expected.tmp");
    unlink("actual.tmp");
    
    return result == 0;
}

// Function to run a test case
int run_test(TestCase *test) {
    printf("Running test: %s - %s\n", test->name, test->description);
    
    // Execute the test
    char *output = execute_sheet_command(test->rows, test->cols, test->commands, test->num_commands);
    
    // Compare output
    int result = compare_output(test->expected_output, output);
    
    // Print result
    if (result) {
        printf("%sPASS%s\n\n", GREEN, RESET);
    } else {
        printf("%sFAIL%s\n", RED, RESET);
        printf("Diff:\n");
        system("cat diff.tmp");
        printf("\n");
    }
    
    free(output);
    return result;
}

// Include test cases
#include "test_cases.h"

int main() {
    int total_tests = 0;
    int passed_tests = 0;
    time_t start_time, end_time;
    
    start_time = time(NULL);
    
    // Register and run all test cases
    TestCase *test_cases = get_test_cases(&total_tests);
    
    for (int i = 0; i < total_tests; i++) {
        passed_tests += run_test(&test_cases[i]);
        
        // Free resources
        for (int j = 0; j < test_cases[i].num_commands; j++) {
            free(test_cases[i].commands[j]);
        }
        free(test_cases[i].commands);
        free(test_cases[i].expected_output);
    }
    
    free(test_cases);
    unlink("diff.tmp");
    
    end_time = time(NULL);
    
    // Print summary
    printf("Tests completed in %ld seconds\n", end_time - start_time);
    printf("Results: %d/%d tests passed\n", passed_tests, total_tests);
    
    return passed_tests == total_tests ? EXIT_SUCCESS : EXIT_FAILURE;
}