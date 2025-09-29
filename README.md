/** ***********************************************************************
 * File: prog1.c
 * Author: Sean Anderson
 * Date: September 29, 2025
 * Brief: Approximates ln(x) using POSIX threads, with each thread computing
 *        terms of the series and updating a shared global sum using mutex locking.
 * Compile by: gcc -Wall prog1.c -o prog1 -lpthread -lm
 * Compiler: gcc
 *************************************************************************** */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>

typedef struct {
    int threadNumber;
    int numIterations;
} THREAD_DATA_TYPE;

// Global variables
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
double sum = 0.0;
double x;
int num_threads;
int iterations;

void *thread_function(void *data) {
    THREAD_DATA_TYPE *threadData = (THREAD_DATA_TYPE *)data;
    double base = x - 1.0; // Compute x - 1 once per thread
    for (int i = 0; i < threadData->numIterations; i++) {
        int n = threadData->threadNumber + i * num_threads; // Term index
        double power = 1.0;
        // Compute (x-1)^n iteratively
        for (int j = 1; j <= n; j++) {
            power *= base;
        }
        double term = power / n; // Base term
        // Apply appropriate sign based on n
        if (n % 2 == 0) {
            term = -term; // Negative for even n
        }
        // Lock only the update to the global sum
        pthread_mutex_lock(&lock);
        sum += term;
        pthread_mutex_unlock(&lock);
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <value (0,2)> <Number of Threads> <Num of iterations>\n", argv[0]);
        return 1;
    }

    x = atof(argv[1]); // First argument: x (0 < x < 2)
    num_threads = atoi(argv[2]); // Second argument: number of threads
    iterations = atoi(argv[3]); // Third argument: iterations per thread

    pthread_t threads[num_threads];
    THREAD_DATA_TYPE threadData[num_threads];

    // Initialize mutex
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("Error initializing mutex\n");
        return 1;
    }

    // Start all threads first
    for (int i = 0; i < num_threads; i++) {
        threadData[i].threadNumber = i + 1; // Thread numbers start at 1
        threadData[i].numIterations = iterations;
        if (pthread_create(&threads[i], NULL, thread_function, &threadData[i]) != 0) {
            printf("pthread_create error\n");
            return 1;
        }
    }

    // Connect to each thread with pthread_join
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            printf("pthread_join error\n");
            return 1;
        }
    }

    // Destroy mutex
    pthread_mutex_destroy(&lock);

    // Print results with 14 digits of precision
    printf("%.14f\n", sum); // Calculated answer
    printf("%.14f\n", log(x)); // Math function answer

    return 0;
}