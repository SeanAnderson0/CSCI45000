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
#include <pthread.h>
#include <math.h>


// Structure used to pass arguments to each thread
typedef struct {
	int threadIndex; // Thread index
	int numThreads; // number of threads running
	int numIterations; // Number of iterations per thread
	double x; // x used in ln(x) approximation
} THREAD_DATA_TYPE;

// Global variable
static double globalVariable = 0.0;

// lock variable (mutex)
static pthread_mutex_t lock;


// each thread computes its subset of series terms
static void *thread_function(void *data)
{
	THREAD_DATA_TYPE *threadData = (THREAD_DATA_TYPE *)data; // cast pointer
	
	// Loop through
	int i;
	for (i = 0; i < threadData->numIterations; i++) {
		int n = (threadData->threadIndex + 1) + i * threadData->numThreads; // N starts at i + 1

		// Compute magnitude of the nth term
		double xMinusOne = threadData->x - 1.0;
		double termMagnitude = pow(xMinusOne, (double)n) / (double)n;

		//  odd n is positive and even n is negative
		double term = (n % 2 == 1) ? termMagnitude : -termMagnitude;

		// Lock the mutex before updating the global sum
		pthread_mutex_lock(&lock);
		globalVariable += term;
		pthread_mutex_unlock(&lock);
	}
	return NULL;
}


// Main
int main(int argc, char *argv[])
{
	// Check input args
	if (argc != 4) {
		fprintf(stderr, "Usage: %s <x in (0,2)> <numThreads> <iterationsPerThread>\n", argv[0]);
		return 1;
	}

	// Ensures input is within (0,2)
	char *endptr = NULL;
	double x = strtod(argv[1], &endptr);
	if (endptr == argv[1] || x <= 0.0 || x >= 2.0) {
		fprintf(stderr, "Value must be in (0,2).\n");
		return 1;
	}

	// Ensures inputs are positive to avoid error
	int numThreads = (int)strtol(argv[2], &endptr, 10);
    int iterationsPerThread = (int)strtol(argv[3], &endptr, 10);
    if (endptr == argv[2] || numThreads <= 0 || endptr == argv[3] || iterationsPerThread <= 0) {
        fprintf(stderr, "threads and iterations must be positive integers.\n");
        return 1;
    }

	// Initialize the mutex
	if (pthread_mutex_init(&lock, NULL) != 0) {
		fprintf(stderr, "Failed to initialize mutex.\n");
		return 1;
	}

	// Table of thread IDs
	pthread_t *threadID_table = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)numThreads);
	// Table of values passed to each thread
	THREAD_DATA_TYPE *threadData = (THREAD_DATA_TYPE *)malloc(sizeof(THREAD_DATA_TYPE) * (size_t)numThreads);
	if (threadID_table == NULL || threadData == NULL) {
		fprintf(stderr, "Error: memory allocation failed.\n");
		pthread_mutex_destroy(&lock);
		free(threadID_table);
		free(threadData);
		return 1;
	}

	// Create and start up each thread
	int i;
	for (i = 0; i < numThreads; i++) {
		threadData[i].threadIndex = i;
		threadData[i].numThreads = numThreads;
		threadData[i].numIterations = iterationsPerThread;
		threadData[i].x = x;

		// Create the thread and pass in the data
		int thread_create_status = pthread_create(&threadID_table[i], NULL, thread_function, (void *)&threadData[i]);
		if (thread_create_status != 0) {
			fprintf(stderr, "Error: pthread_create failed for thread %d.\n", i);
			//  If thread creation fails join previously created threads
			int j;
			for (j = 0; j < i; j++) {
				pthread_join(threadID_table[j], NULL);
			}
			pthread_mutex_destroy(&lock);
			free(threadID_table);
			free(threadData);
			return 1;
		}
	}

	// Wait for all threads to complete
	for (i = 0; i < numThreads; i++) {
		pthread_join(threadID_table[i], NULL);
	}

	// Free allocated memory
	free(threadID_table);
	free(threadData);

	// Print results
	printf("%.14f\n", globalVariable);
	printf("%.14f\n", log(x));

	// Clean up the mutex
	pthread_mutex_destroy(&lock);
	return 0;
}
