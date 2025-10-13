/** ***********************************************************************
    @file	prog2.c
    @author	Drake Schlautman
    @date	October 13, 2025
    @brief	DNA Sequence search using Processes, Shared Memory, and Semaphores.

    This program searches for the best match between a DNA subsequence
    and a larger DNA sequence using multiple processes. It uses
    process creation with fork(), shared memory with POSIX shm_open(),
    and synchronization with POSIX semaphores.
    
        Compile by:
    
            gcc -Wall prog2.c -o prog2 -lpthread -lrt

    Compiler:	gcc
    Company:	me
*************************************************************************** */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <semaphore.h>

// Max processes
#define MAX_PROCESSES 20

// Structure stored in shared memory
typedef struct {
    int best_match_position;
    int best_match_count;
    sem_t semaphore;
} shared_data_t;

/***************************************************************************
    @brief  read_file function to read a DNA sequence file into memory.
***************************************************************************/
char *read_file(const char *filename, long *length) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("Error Opening file: %s\n", filename);
        exit(0);
    }

    fseek(fp, 0, SEEK_END);
    *length = ftell(fp);
    rewind(fp);

    char *buffer = (char *)malloc(*length + 1);
    if (buffer == NULL) {
        printf("Error allocating memory\n");
        fclose(fp);
        exit(0);
    }

    fread(buffer, 1, *length, fp);
    buffer[*length] = '\0';
    fclose(fp);
    return buffer;
}

/***************************************************************************
    @brief  search_segemnt function so each child process searches part of the DNA sequence.
***************************************************************************/
void search_segment(char *sequence, char *subseq, long seq_len, long sub_len,
                    int start, int step, shared_data_t *shared) {
    for (long pos = start; pos < seq_len; pos += step) {
        int count = 0;
        for (long j = 0; j < sub_len && (pos + j) < seq_len; j++) {
            if (sequence[pos + j] == subseq[j])
                count++;
        }

        // Protect shared memory update with semaphore
        sem_wait(&(shared->semaphore));
        if (count > shared->best_match_count) {
            shared->best_match_count = count;
            shared->best_match_position = pos;
        }
        sem_post(&(shared->semaphore));
    }
}

/***************************************************************************
    @brief  Main function to run the program
***************************************************************************/
int main(int argc, char *argv[]) {
    int shm_fd = -1;
    shared_data_t *shared = NULL;
    pid_t pidValue = 0;
    int i, status;

    if (argc != 4) {
        printf("Usage: %s <Sequence File> <Subsequence File> <Num Processes>\n", argv[0]);
        exit(0);
    }

    const char *seq_file = argv[1];
    const char *subseq_file = argv[2];
    int num_processes = atoi(argv[3]);

    if (num_processes <= 0) {
        printf("Invalid number of processes.\n");
        exit(0);
    }

    if (num_processes > MAX_PROCESSES) {
        printf("Invalid number of processes. Maximum allowed is %d.\n", MAX_PROCESSES);
        exit(0);
    }

    // Read both sequence files into memory
    long seq_len, sub_len;
    char *sequence = read_file(seq_file, &seq_len);
    char *subsequence = read_file(subseq_file, &sub_len);

    // Create and set up shared memory
    const char *SHM_NAME = "/dna_shm";
    shm_unlink(SHM_NAME); 

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, S_IRWXU);
    if (shm_fd < 0) {
        printf("Shared Memory Error - Exit\n");
        exit(0);
    }

    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        printf("Error sizing shared memory\n");
        exit(0);
    }

    shared = (shared_data_t *)mmap(0, sizeof(shared_data_t),
            PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == NULL) {
        printf("mmap error\n");
        exit(0);
    }

    // Initialize shared memory data
    shared->best_match_position = -1;
    shared->best_match_count = 0;
    sem_init(&(shared->semaphore), 1, 1); 

    // Fork the specified number of processes
    for (i = 0; i < num_processes; i++) {
        pidValue = fork();
        if (pidValue == 0) {
            search_segment(sequence, subsequence, seq_len, sub_len, i, num_processes, shared);
            exit(0);
        } else if (pidValue < 0) {
            printf("Error with fork() call\n");
            exit(0);
        }
    }

    // Parent waits for all child processes to finish
    for (i = 0; i < num_processes; i++) {
        wait(&status);
    }

    // Display results
    printf("Number of Processes: %d\n", num_processes);
    printf("Best Match Position: %d\n", shared->best_match_position);
    printf("Best Match Count:    %d\n", shared->best_match_count);

    // Clean up
    sem_destroy(&(shared->semaphore));
    munmap(shared, sizeof(shared_data_t));
    shm_unlink(SHM_NAME);
    free(sequence);
    free(subsequence);

    return 0;
}
