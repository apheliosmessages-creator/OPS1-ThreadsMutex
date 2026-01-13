#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <unistd.h> // For usleep

// --- Shared Context ---
typedef struct {
    int k;                   // Size of array
    double* tasks;           // Inputs
    double* results;         // Outputs
    bool* isDone;            // Flags to mark completed cells
    
    pthread_mutex_t* mxCells; // Array of mutexes (one per cell)
    
    int remaining;           // How many tasks are left?
    pthread_mutex_t mxCount; // Protects 'remaining' variable
} SharedData;

// --- Worker Thread ---
void* worker(void* arg) {
    SharedData* data = (SharedData*)arg;
    
    while (true) {
        // 1. Check if there is work left (Quick check)
        pthread_mutex_lock(&data->mxCount);
        if (data->remaining == 0) {
            pthread_mutex_unlock(&data->mxCount);
            break; // No work left, terminate
        }
        pthread_mutex_unlock(&data->mxCount);

        // 2. Pick a random cell
        int index = rand() % data->k;

        // 3. Try to claim this specific cell
        // We lock the mutex for THIS specific index
        pthread_mutex_lock(&data->mxCells[index]);
        
        if (!data->isDone[index]) {
            // It hasn't been done yet! We do the work.
            
            // Calculation
            double input = data->tasks[index];
            double res = sqrt(input);
            data->results[index] = res;
            
            // Mark as done
            data->isDone[index] = true;
            
            // Decrement global counter safely
            pthread_mutex_lock(&data->mxCount);
            data->remaining--;
            int left = data->remaining;
            pthread_mutex_unlock(&data->mxCount);

            // Print
            printf("Thread %lu: sqrt(%.2f) = %.2f (Index %d, Remaining: %d)\n", 
                   (unsigned long)pthread_self(), input, res, index, left);
            
            // Unlock cell
            pthread_mutex_unlock(&data->mxCells[index]);
            
            // Sleep 100ms (Requirement)
            usleep(100000); 
        } else {
            // It was already done. Unlock and try again.
            pthread_mutex_unlock(&data->mxCells[index]);
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    // 0. Parse Args
    if (argc != 3) {
        printf("Usage: %s <n> <k>\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    int k = atoi(argv[2]);
    srand(time(NULL));

    // 1. Allocations
    double* tasks = malloc(sizeof(double) * k);
    double* results = malloc(sizeof(double) * k);
    bool* isDone = malloc(sizeof(bool) * k);
    pthread_mutex_t* mxCells = malloc(sizeof(pthread_mutex_t) * k);
    pthread_mutex_t mxCount;

    // 2. Initialization
    pthread_mutex_init(&mxCount, NULL);
    printf("Input Array: [ ");
    for (int i = 0; i < k; i++) {
        tasks[i] = 1.0 + ((double)rand() / RAND_MAX) * 59.0;
        isDone[i] = false;
        pthread_mutex_init(&mxCells[i], NULL); // Init mutex for EACH cell
        printf("%.2f ", tasks[i]);
    }
    printf("]\n");

    // 3. Prepare Shared Data
    SharedData shared;
    shared.k = k;
    shared.tasks = tasks;
    shared.results = results;
    shared.isDone = isDone;
    shared.mxCells = mxCells;
    shared.remaining = k;       // Start with k tasks remaining
    shared.mxCount = mxCount;

    // 4. Create Threads
    pthread_t* threads = malloc(sizeof(pthread_t) * n);
    for (int i = 0; i < n; i++) {
        pthread_create(&threads[i], NULL, worker, &shared);
    }

    // 5. Wait for completion
    for (int i = 0; i < n; i++) {
        pthread_join(threads[i], NULL);
    }

    // 6. Print Final Results
    printf("\n--- Final Results ---\n");
    printf("Input:  ");
    for(int i=0; i<k; i++) printf("%6.2f ", tasks[i]);
    printf("\nResult: ");
    for(int i=0; i<k; i++) printf("%6.2f ", results[i]);
    printf("\n");

    // 7. Cleanup
    free(tasks); free(results); free(isDone); free(threads);
    for(int i=0; i<k; i++) pthread_mutex_destroy(&mxCells[i]);
    free(mxCells);
    pthread_mutex_destroy(&mxCount);

    return 0;
}