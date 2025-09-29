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
	// Thread index (0-based, determines which terms of the series to compute)
	int threadIndex; 
	// Total number of threads running
	int numThreads;
	// Number of iterations (terms) each thread should compute
	int numIterations;
	// Value of x used in ln(x) approximation
	double x;
} THREAD_DATA_TYPE;

// Global accumulator for the series sum (shared by all threads)
static double g_seriesSum = 0.0;

// Mutex to protect updates to the shared global sum
static pthread_mutex_t g_sumMutex;


// Thread function: each thread computes its subset of series terms
static void *thread_function(void *data)
{
	THREAD_DATA_TYPE *threadData = (THREAD_DATA_TYPE *)data;
	const double xMinusOne = threadData->x - 1.0;

	// Each thread loops through its assigned terms
	int k;
	for (k = 0; k < threadData->numIterations; k++) {
		// Formula: n = (threadIndex+1) + k*numThreads
		int n = (threadData->threadIndex + 1) + k * threadData->numThreads; // N starts at thread index + 1

		// Compute magnitude of the nth term: (x-1)^n / n
		double termMagnitude = pow(xMinusOne, (double)n) / (double)n;

		// Alternate sign: odd n => positive, even n => negative
		double term = (n % 2 == 1) ? termMagnitude : -termMagnitude;

		// Lock the mutex before updating the global sum
		pthread_mutex_lock(&g_sumMutex);
		g_seriesSum += term;
		pthread_mutex_unlock(&g_sumMutex);
	}
	return NULL;
}


// Main function: parses arguments, creates threads, and prints results
int main(int argc, char *argv[])
{
	// Check for proper command-line usage
	if (argc != 4) {
		fprintf(stderr, "Usage: %s <x in (0,2)> <numThreads> <iterationsPerThread>\n", argv[0]);
		return 1;
	}

	// Convert first argument to double (x value)
	char *endptr = NULL;
	double x = strtod(argv[1], &endptr);
	if (endptr == argv[1] || x <= 0.0 || x >= 2.0) {
		fprintf(stderr, "Error: x must be a floating-point value in (0,2).\n");
		return 1;
	}

	// Convert second argument to integer (number of threads)
	int numThreads = (int)strtol(argv[2], &endptr, 10);
	if (endptr == argv[2] || numThreads <= 0) {
		fprintf(stderr, "Error: numThreads must be a positive integer.\n");
		return 1;
	}

	// Convert third argument to integer (iterations per thread)
	int iterationsPerThread = (int)strtol(argv[3], &endptr, 10);
	if (endptr == argv[3] || iterationsPerThread <= 0) {
		fprintf(stderr, "Error: iterations must be a positive integer.\n");
		return 1;
	}

	// Initialize mutex for synchronizing access to the global sum
	if (pthread_mutex_init(&g_sumMutex, NULL) != 0) {
		fprintf(stderr, "Error initializing mutex.\n");
		return 1;
	}

	// Allocate arrays for thread handles and thread data
	pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)numThreads);
	THREAD_DATA_TYPE *threadData = (THREAD_DATA_TYPE *)malloc(sizeof(THREAD_DATA_TYPE) * (size_t)numThreads);
	if (threads == NULL || threadData == NULL) {
		fprintf(stderr, "Error: memory allocation failed.\n");
		pthread_mutex_destroy(&g_sumMutex);
		free(threads);
		free(threadData);
		return 1;
	}

	// Create threads and assign work
	int i;
	for (i = 0; i < numThreads; i++) {
		threadData[i].threadIndex = i;
		threadData[i].numThreads = numThreads;
		threadData[i].numIterations = iterationsPerThread;
		threadData[i].x = x;

		// Start thread with its corresponding data
		int rc = pthread_create(&threads[i], NULL, thread_function, (void *)&threadData[i]);
		if (rc != 0) {
			fprintf(stderr, "Error: pthread_create failed for thread %d.\n", i);
			// If thread creation fails, attempt to join previously created threads
			int j;
			for (j = 0; j < i; j++) {
				pthread_join(threads[j], NULL);
			}
			pthread_mutex_destroy(&g_sumMutex);
			free(threads);
			free(threadData);
			return 1;
		}
	}

	// Wait for all threads to complete
	for (i = 0; i < numThreads; i++) {
		pthread_join(threads[i], NULL);
	}

	// Free dynamically allocated memory
	free(threads);
	free(threadData);

	// Print the results: approximation and actual ln(x) with 14 digits precision
	printf("%.14f\n", g_seriesSum);
	printf("%.14f\n", log(x));

	// Clean up the mutex
	pthread_mutex_destroy(&g_sumMutex);
	return 0;
}
