/** ***********************************************************************
	@file	Program1.c
	@author	Drake Schlautman
	@date	September 29, 2025
	@brief	Approximating the natural logarithm using POSIX threads

	C program to take 3 command line arguments. The value, number of 
    threads, and number of iterations. This program aproximates the
    natural logarithm using POSIX threads.
    
    	Compile by:
    
        	gcc -Wall Program1.c -o Program1  -lpthread -lm

	Compiler:	gcc
	Company:	me

*************************************************************************** */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>

//global variable
double globalResult = 0.0;

// Lock variable (mutex)
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int threadNumber;     
    int numThreads;       
    int numIterations;    
    double x;             
} THREAD_DATA_TYPE;

// Function prototypes
void * thread_function(void *data);

/**
 * @brief pthread function to compute ln(x) terms
 * The function is written to be a pthread using the pthread function
        prototype
 * @param[in] data - void * pointer to data needed for this pthread
 * @return Returns - void *
 */
void * thread_function(void *data) {
    THREAD_DATA_TYPE * threadData = (THREAD_DATA_TYPE*)data;
    int tid = threadData->threadNumber;
    int numThreads = threadData->numThreads;
    int iterations = threadData->numIterations;
    double x = threadData->x;

    double localSum = 0.0;

    for (int i = 0; i < iterations; i++) {
        int n = tid + 1 + i * numThreads;   
        double term = pow(x - 1, n) / n;

        if (n % 2 == 0) {
            term = -term; 
        }

        localSum += term;
    }

    // Update global result 
    if (pthread_mutex_lock(&lock) != 0) {
        printf("Error requesting lock\n");
    }

    globalResult += localSum;

    pthread_mutex_unlock(&lock);

    pthread_exit(NULL);
}

/**
 * @brief C main function
 * C main function
 * @return Returns - int
 */
int main(int argc, char *argv[]) {
    
    //check number of arguments is correct
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <value (0,2)> <numThreads> <iterations>\n", argv[0]);
        return 1;
    }

    double x = atof(argv[1]);
    int numThreads = atoi(argv[2]);
    int iterations = atoi(argv[3]);

    //check 1st argument
    if (x <= 0 || x >= 2) {
        fprintf(stderr, "Error: value must be within (0,2)\n");
        return 1;
    }

    pthread_t *threadID_table = malloc(sizeof(pthread_t) * numThreads);
    THREAD_DATA_TYPE *threadData = malloc(sizeof(THREAD_DATA_TYPE) * numThreads);

    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("Error initializing lock\n");
        return 1;
    }

    // Create threads
    for (int i = 0; i < numThreads; i++) {
        threadData[i].threadNumber = i;
        threadData[i].numThreads = numThreads;
        threadData[i].numIterations = iterations;
        threadData[i].x = x;

        if (pthread_create(&threadID_table[i], NULL, thread_function, &threadData[i]) != 0) {
            printf("pthread_create error\n");
            return 1;
        }
    }

    // Join threads
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threadID_table[i], NULL);
    }

    pthread_mutex_destroy(&lock);
    
    // Print results
    printf("%.14f\n", globalResult);
    printf("%.14f\n", log(x));

    return 0;
}