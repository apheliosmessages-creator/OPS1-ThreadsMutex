#define _XOPEN_SOURCE 700
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

// --- SHARED CONTEXT ---
typedef struct {
    int n;
    int p;
    int *array;
    pthread_mutex_t *mxArray; // Fine-grained locks
    
    // Thread Management
    int active_count;
    pthread_mutex_t mxParams; // Protects active_count and stop_flag
    bool stop_flag;
    
    // Signal Flags
    volatile sig_atomic_t req_inversion;
    volatile sig_atomic_t req_print;
    volatile sig_atomic_t req_exit;

    // To track threads for joining on exit
    // Since we need to join them, we need to store TIDs.
    // However, detached threads are easier for "fire and forget".
    // Requirement says: "cleanly terminates". 
    // We will use a Linked List to store running threads so we can join them at the end.
    struct ThreadList *threads; 
} SharedContext;

typedef struct ThreadList {
    pthread_t tid;
    struct ThreadList *next;
} ThreadList;

// --- GLOBAL POINTER FOR SIGNAL HANDLER ---
static SharedContext *g_ctx = NULL;

// --- HELPER: Lock entire array safely ---
void lock_all(SharedContext *ctx) {
    for(int i=0; i<ctx->n; i++) pthread_mutex_lock(&ctx->mxArray[i]);
}

void unlock_all(SharedContext *ctx) {
    // Unlocking order doesn't impact deadlock, but reverse is good practice
    for(int i=ctx->n-1; i>=0; i--) pthread_mutex_unlock(&ctx->mxArray[i]);
}

// --- THREAD 1: Inversion Worker ---
void *thread_inversion(void *arg) {
    SharedContext *ctx = (SharedContext *)arg;
    
    int a = rand() % ctx->n;
    int b = rand() % ctx->n;
    if (a > b) { int t = a; a = b; b = t; }
    
    // Don't invert single element or invalid ranges
    if (a == b) {
        pthread_mutex_lock(&ctx->mxParams);
        ctx->active_count--;
        pthread_mutex_unlock(&ctx->mxParams);
        return NULL;
    }

    printf("[Worker] Inverting range [%d, %d]\n", a, b);

    int left = a;
    int right = b;
    
    while (left < right) {
        // Check stop flag (Stage 4 requirement for fast exit)
        pthread_mutex_lock(&ctx->mxParams);
        if (ctx->stop_flag) {
            pthread_mutex_unlock(&ctx->mxParams);
            break;
        }
        pthread_mutex_unlock(&ctx->mxParams);

        // LOCK ORDER: Lower index first
        pthread_mutex_lock(&ctx->mxArray[left]);
        pthread_mutex_lock(&ctx->mxArray[right]);

        // Swap
        int tmp = ctx->array[left];
        ctx->array[left] = ctx->array[right];
        ctx->array[right] = tmp;

        pthread_mutex_unlock(&ctx->mxArray[right]);
        pthread_mutex_unlock(&ctx->mxArray[left]);

        left++;
        right--;
        usleep(5000); // 5ms wait
    }

    // Decrement active count
    pthread_mutex_lock(&ctx->mxParams);
    ctx->active_count--;
    pthread_mutex_unlock(&ctx->mxParams);
    return NULL;
}

// --- THREAD 2: Printer Worker ---
void *thread_printer(void *arg) {
    SharedContext *ctx = (SharedContext *)arg;
    
    // To print consistent state, lock EVERYTHING
    lock_all(ctx);
    
    printf("[Printer] Array: ");
    for(int i=0; i<ctx->n; i++) printf("%d ", ctx->array[i]);
    printf("\n");
    
    unlock_all(ctx);
    
    pthread_mutex_lock(&ctx->mxParams);
    ctx->active_count--;
    pthread_mutex_unlock(&ctx->mxParams);
    return NULL;
}

// --- SIGNAL HANDLER ---
void handler(int sig) {
    if (!g_ctx) return;
    if (sig == SIGUSR1) g_ctx->req_inversion = 1;
    if (sig == SIGUSR2) g_ctx->req_print = 1;
    if (sig == SIGINT)  g_ctx->req_exit = 1;
}

// --- MAIN LIST MANAGEMENT ---
void add_thread(SharedContext *ctx, pthread_t tid) {
    ThreadList *node = malloc(sizeof(ThreadList));
    node->tid = tid;
    node->next = ctx->threads;
    ctx->threads = node;
}

// --- MAIN ---
int main(int argc, char **argv) {
    if (argc != 3) ERR("Usage: n p");
    
    SharedContext ctx;
    ctx.n = atoi(argv[1]);
    ctx.p = atoi(argv[2]);
    if (ctx.n < 8 || ctx.n > 256) ERR("Invalid n (8-256)");
    if (ctx.p < 1 || ctx.p > 16) ERR("Invalid p (1-16)");

    ctx.array = malloc(sizeof(int) * ctx.n);
    ctx.mxArray = malloc(sizeof(pthread_mutex_t) * ctx.n);
    ctx.active_count = 0;
    ctx.stop_flag = false;
    ctx.req_inversion = 0; 
    ctx.req_print = 0; 
    ctx.req_exit = 0;
    ctx.threads = NULL;
    pthread_mutex_init(&ctx.mxParams, NULL);

    for(int i=0; i<ctx.n; i++) {
        ctx.array[i] = i;
        pthread_mutex_init(&ctx.mxArray[i], NULL);
    }

    g_ctx = &ctx;
    srand(time(NULL));

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    printf("PID: %d (Ready)\n", getpid());

    while (1) {
        // Use sigwait or simple sleep loop to verify flags
        // We use sleep/pause loop as "Main Loop"
        usleep(100000); // 100ms check interval

        if (ctx.req_exit) break;

        // --- HANDLE SIGUSR1 (Invert) ---
        if (ctx.req_inversion) {
            ctx.req_inversion = 0; // Clear flag
            
            pthread_mutex_lock(&ctx.mxParams);
            if (ctx.active_count >= ctx.p) {
                printf("All threads busy, aborting request\n");
                pthread_mutex_unlock(&ctx.mxParams);
            } else {
                ctx.active_count++;
                pthread_mutex_unlock(&ctx.mxParams);
                
                pthread_t tid;
                if (pthread_create(&tid, NULL, thread_inversion, &ctx) == 0) {
                    add_thread(&ctx, tid);
                }
            }
        }

        // --- HANDLE SIGUSR2 (Print) ---
        if (ctx.req_print) {
            ctx.req_print = 0;
            
            pthread_mutex_lock(&ctx.mxParams);
            if (ctx.active_count >= ctx.p) {
                printf("All threads busy, aborting request\n");
                pthread_mutex_unlock(&ctx.mxParams);
            } else {
                ctx.active_count++;
                pthread_mutex_unlock(&ctx.mxParams);
                
                pthread_t tid;
                if (pthread_create(&tid, NULL, thread_printer, &ctx) == 0) {
                    add_thread(&ctx, tid);
                }
            }
        }
    }

    // --- CLEANUP ON EXIT (SIGINT) ---
    printf("\nExiting... Waiting for threads.\n");
    
    pthread_mutex_lock(&ctx.mxParams);
    ctx.stop_flag = true; // Tell workers to stop early if possible
    pthread_mutex_unlock(&ctx.mxParams);

    ThreadList *curr = ctx.threads;
    while (curr) {
        pthread_join(curr->tid, NULL);
        ThreadList *tmp = curr;
        curr = curr->next;
        free(tmp);
    }

    free(ctx.array);
    free(ctx.mxArray);
    return 0;
}