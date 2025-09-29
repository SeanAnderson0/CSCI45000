/** ***********************************************************************
File: prog1.c
Author: Sean Anderson
Date: September 29, 2025
Brief: Approximates ln(x) using POSIX threads, with each thread computing terms
of the series and updating a shared global sum using mutex locking.
Compile by: gcc -Wall prog1.c -o prog1 -lpthread -lm
Compiler: gcc
*************************************************************************** */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define USE_LOCKS 1
#define GBUFFER_SIZE 48

typedef struct {
    int numIterations;
    int threadNumber;
} THREAD_DATA_TYPE;

// lock variable (mutex)
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Global variable
int globalVariable = 0;

// Function Prototypes
void *thread_function(void *data);

// Global Buffer shared between threads
char *sharedBuffer = NULL;

void *thread_function(void *data)
{
    THREAD_DATA_TYPE *threadData = (THREAD_DATA_TYPE *)data; // cast pointer
    int i;
    int j;
    // Loop through
    for (i = 0; i < threadData->numIterations; i++) {
#if USE_LOCKS
        if (pthread_mutex_lock(&lock) != 0) {
            printf("Error requesting lock\n");
        }
#endif
        globalVariable++;
        for (j = 0; j < GBUFFER_SIZE - 1; j++) {
            sharedBuffer[j] = '0' + threadData->threadNumber;
        }
        sharedBuffer[j] = 0x00;
        printf("Thread Num: %2d count: %d sharedBuffer: %s\n", threadData->threadNumber, i, sharedBuffer);
#if USE_LOCKS
        pthread_mutex_unlock(&lock);
#endif
    }
    printf("Thread %d exiting...\n", threadData->threadNumber);
    pthread_exit(data); // return pointer to the number of seconds pass to us
}

int main()
{
    const int MAX_THREADS = 8; // Number of threads to create
    const int NUM_ITERATIONS = 10;
    int i;
    // Table of thread IDs
    pthread_t threadID_table[MAX_THREADS];
    // Table of seconds values passed to each thread
    THREAD_DATA_TYPE threadData[MAX_THREADS];
    printf("Thread Test Threads: %d Itnerations: %d Locking: %s\n", MAX_THREADS, NUM_ITERATIONS, (USE_LOCKS == 0) ? "LOCKING DISABLED" : "LOCKING ENABLED");
    sharedBuffer = malloc(GBUFFER_SIZE);
    memset(sharedBuffer, 0x00, GBUFFER_SIZE);
    // Initialize the lock, pass NULL to indicate to use default lock parameters
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("Error initializing lock\n");
    }
    // Create and start up each thread
    for (i = 0; i < MAX_THREADS; i++) {
        int thread_create_status = 0;
        threadData[i].threadNumber = i;
        threadData[i].numIterations = NUM_ITERATIONS;
        // Create the thread and pass in the number of seconds
        // and also save this threads thread ID for user later
        thread_create_status = pthread_create(&threadID_table[i], NULL, thread_function, ((void *)&threadData[i]));
        if (thread_create_status != 0) {
            printf("pthread_create error\n");
        }
    }
#if 1
    // We need to join (Connect) to each thread and wait for its completion
    for (i = 0; i < MAX_THREADS; i++) {
        int *thread_retval;
        int join_retval;
        // Join to the thread identified by its thread ID
        join_retval = pthread_join(threadID_table[i], (void **)(&thread_retval));
        if (join_retval == 0) {
            printf("join> thread Number: %d join retval: %d\n", ((THREAD_DATA_TYPE *)thread_retval)->threadNumber, join_retval);
        } else {
            printf("pthread join error: %d\n", join_retval);
        }
    }
#endif
    // Destroy the lock - bascially return the lock resources
    pthread_mutex_destroy(&lock);
    return 0;
}
