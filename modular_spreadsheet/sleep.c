#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include "sleep.h"

// Sleep wrapper for pthread.
void* sleep_wrapper(void* arg) {
    int duration = *((int*)arg);
    sleep(duration);
    return NULL;
}

// Evaluates SLEEP(n): sleeps for n seconds and returns n.
CommandStatus evaluate_sleep(Spreadsheet* sheet, Cell* cell, const char* expr) {
    char token[256];
    sscanf(expr, "SLEEP(%[^)])", token); // Extract argument inside SLEEP()
    
    int duration = evaluate_cell_reference(sheet, token, cell); // Evaluate input
    if (duration < 0 || cell->error_state) {
        cell->error_state = 1;
        return CMD_UNRECOGNIZED;
    }
    
    pthread_t thread;
    int* arg = malloc(sizeof(int));
    *arg = duration;
    if (pthread_create(&thread, NULL, sleep_wrapper, arg) != 0) {
        free(arg);
        cell->error_state = 1;
        return CMD_UNRECOGNIZED;
    }
    pthread_join(thread, NULL); // Wait for sleep to complete.
    free(arg);
    
    cell->value = duration;
    cell->error_state = 0;
    return CMD_OK;
}
