#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

// --- The Shared Reality (The Kitchen) ---
typedef struct {
    char board_status[100];     // What is currently on the board?
    pthread_mutex_t mxBoard;    // The Key to the cutting board
} Kitchen;

// --- Helper to log with timestamps ---
void log_action(const char* name, const char* action) {
    printf("[%s] %s\n", name, action);
}

// --- CHEF A: The Chive Chopper ---
void* chef_chopper(void* arg) {
    Kitchen* k = (Kitchen*)arg;

    log_action("CHEF A", "I need to chop chives. Asking for the key...");

    // 1. LOCK (Grab the Key)
    pthread_mutex_lock(&k->mxBoard);
    log_action("CHEF A", "I have the key! The board is mine.");

    // 2. CRITICAL SECTION (The Work)
    log_action("CHEF A", "Placing fresh chives on the board...");
    strcpy(k->board_status, "Fresh Green Chives");
    
    printf("   (Board currently holds: %s)\n", k->board_status);

    log_action("CHEF A", "Chopping... (This takes 3 seconds)");
    sleep(3); // Simulate slow work

    // Check if our chives are still safe
    log_action("CHEF A", "I am done chopping. Checking board...");
    if (strcmp(k->board_status, "Fresh Green Chives") == 0) {
        log_action("CHEF A", "SUCCESS! The chives are perfect. Serving them.");
    } else {
        // This would happen if we didn't use a Mutex!
        log_action("CHEF A", "DISASTER! Someone messed up my board!");
        printf("   Board contained: %s\n", k->board_status);
    }

    // 3. UNLOCK (Return the Key)
    log_action("CHEF A", "I am finished. Returning the key.");
    pthread_mutex_unlock(&k->mxBoard);

    return NULL;
}

// --- CHEF B: The Cleaner ---
void* chef_cleaner(void* arg) {
    Kitchen* k = (Kitchen*)arg;

    // Wait a split second so Chef A usually starts first (for demonstration)
    usleep(500000); 

    log_action("CHEF B", "I need to clean. Asking for the key...");

    // 1. LOCK (Grab the Key)
    // If Chef A has the key, Chef B STOPS here and sleeps until A unlocks.
    pthread_mutex_lock(&k->mxBoard); 
    
    // As soon as this line prints, it means Chef A has finished!
    log_action("CHEF B", "Finally got the key! I am wiping the board.");

    // 2. CRITICAL SECTION
    strcpy(k->board_status, "Soapy Water");
    printf("   (Board currently holds: %s)\n", k->board_status);
    
    sleep(1); // Scrubbing

    // 3. UNLOCK
    log_action("CHEF B", "All clean. Returning key.");
    pthread_mutex_unlock(&k->mxBoard);

    return NULL;
}

int main() {
    pthread_t t1, t2;
    Kitchen k;

    // Initialize
    strcpy(k.board_status, "Empty");
    pthread_mutex_init(&k.mxBoard, NULL);

    printf("--- KITCHEN OPEN ---\n");

    // Hire the Chefs
    pthread_create(&t1, NULL, chef_chopper, &k);
    pthread_create(&t2, NULL, chef_cleaner, &k);

    // Wait for them to go home
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("--- KITCHEN CLOSED ---\n");
    pthread_mutex_destroy(&k.mxBoard);
    return 0;
}