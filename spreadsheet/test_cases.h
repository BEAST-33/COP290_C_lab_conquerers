#ifndef TEST_CASES_H
#define TEST_CASES_H

#include <stdlib.h>
#include <string.h>
// Add this structure definition at the beginning of your file, after the includes

// Helper function to create a new test case
TestCase create_test_case(const char *name, const char *description, int rows, int cols, 
                         char **commands, int num_commands, const char *expected_output) {
    TestCase test;
    test.name = strdup(name);
    test.description = strdup(description);
    test.rows = rows;
    test.cols = cols;
    
    // Copy commands
    test.commands = (char**)malloc(num_commands * sizeof(char*));
    test.num_commands = num_commands;
    for (int i = 0; i < num_commands; i++) {
        test.commands[i] = strdup(commands[i]);
    }
    
    test.expected_output = strdup(expected_output);
    return test;
}

// Function to get all test cases
TestCase* get_test_cases(int *count) {
    // Allocate memory for test cases
    // Increase this number when you add more test cases
    const int MAX_TEST_CASES = 10;
    TestCase *tests = (TestCase*)malloc(MAX_TEST_CASES * sizeof(TestCase));
    *count = 0;
    
    // Test Case 1: Basic operations
    {
        char *commands[] = {
            "A1=2",
            "B1=A1+1",
            "A2=MAX(A1:B1)"
        };
        
        const char *expected_output = 
            "    A       B       \n"
            "1   0       0       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       0       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   3       0       \n"
            "[0.0] (ok) > ";
        
        tests[(*count)++] = create_test_case("basic_operations", "Test basic cell operations and MAX function", 
                                            2, 2, commands, 3, expected_output);
    }
    
    // Test Case 2: SLEEP function and recalculation
    {
        char *commands[] = {
            "A1=2",
            "B1=A1+1",
            "A2=MAX(A1:B1)",
            "B2=SLEEP(2)",
            "A1=5"
        };
        
        const char *expected_output = 
            "    A       B       \n"
            "1   0       0       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       0       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   3       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   3       2       \n"
            "[2.0] (ok) >     A       B       \n"
            "1   5       6       \n"
            "2   6       2       \n"
            "[0.0] (ok) > ";
        
        tests[(*count)++] = create_test_case("recalculation", "Test SLEEP function and recalculation", 
                                          2, 2, commands, 5, expected_output);
    }
    
    // Test Case 3: Error handling
    {
        char *commands[] = {
            "A1=2",
            "B1=A1+1",
            "A2=MAX(B1:A1)",
            "A2=MAX(A1:B1)"
        };
        
        const char *expected_output = 
            "    A       B       \n"
            "1   0       0       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       0       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   0       0       \n"
            "[0.0] (invalid range) >     A       B       \n"
            "1   2       3       \n"
            "2   3       0       \n"
            "[0.0] (ok) > ";
        
        tests[(*count)++] = create_test_case("error_handling", "Test invalid range error handling", 
                                           2, 2, commands, 4, expected_output);
    }
    
    // Test Case 4: Output suppression
    {
        char *commands[] = {
            "disable_output",
            "A1=2",
            "B1=A1+1",
            "enable_output",
            "A2=MAX(A1:B1)"
        };
        
        const char *expected_output = 
            "    A       B       \n"
            "1   0       0       \n"
            "2   0       0       \n"
            "[0.0] (ok) > [0.0] (ok) > [0.0] (ok) > [0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   0       0       \n"
            "[0.0] (ok) >     A       B       \n"
            "1   2       3       \n"
            "2   3       0       \n"
            "[0.0] (ok) > ";
        
        tests[(*count)++] = create_test_case("output_suppression", "Test disable_output and enable_output commands", 
                                          2, 2, commands, 5, expected_output);
    }
    
    // Test Case 5: Scrolling
    {
        char *commands[] = {
            "A1=1",
            "B2=2",
            "C3=3",
            "scroll_to B2",
            "scroll_to A1"
        };
        
        const char *expected_output = 
            "    A       B       C       \n"
            "1   0       0       0       \n"
            "2   0       0       0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   1       0       0       \n"
            "2   0       0       0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   1       0       0       \n"
            "2   0       2       0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   1       0       0       \n"
            "2   0       2       0       \n"
            "3   0       0       3       \n"
            "[0.0] (ok) >     B       C       \n"
            "2   2       0       \n"
            "3   0       3       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   1       0       0       \n"
            "2   0       2       0       \n"
            "3   0       0       3       \n"
            "[0.0] (ok) > ";
        
        tests[(*count)++] = create_test_case("scrolling", "Test scroll_to command", 
                                          3, 3, commands, 5, expected_output);
    }
    
    // Test Case 6: Division by zero error handling
    {
        char *commands[] = {
            "A1=1",
            "B1=A1-100",
            "B2=1/A1",
            "C1=MAX(B1:B2)",
            "A1=0",
            "A2=SLEEP(C1)"
        };
        
        const char *expected_output = 
            "    A       B       C       \n"
            "1   0       0       0       \n"
            "2   0       0       0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   1       0       0       \n"
            "2   0       0       0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   1       -99     0       \n"
            "2   0       0       0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   1       -99     0       \n"
            "2   0       1       0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   1       -99     1       \n"
            "2   0       1       0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   0       -100    ERR     \n"
            "2   0       ERR     0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) >     A       B       C       \n"
            "1   0       -100    ERR     \n"
            "2   ERR     ERR     0       \n"
            "3   0       0       0       \n"
            "[0.0] (ok) > ";
        
        tests[(*count)++] = create_test_case("div_by_zero", "Test division by zero error handling", 
                                          3, 3, commands, 6, expected_output);
    }
    
    // Add more test cases as needed...
    
    return tests;
}

#endif // TEST_CASES_H