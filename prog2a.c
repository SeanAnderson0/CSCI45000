/**********************************************************************
File: prog2.c
Author: Sean Anderson
Date: October 13, 2025
Brief: Finds the best matching position of a DNA subsequence within a larger DNA sequence using multiple processes. 
Each process searches every P-th index of the sequence, based on its process ID, and reports its best local match. 
Shared results are stored in POSIX shared memory and synchronized using a named semaphore to prevent data conflicts. 
The program outputs three lines showing the total number of processes, the best match position, and the match count.
Compile by: gcc -Wall prog2.c -o prog2 -lpthread
Compiler: gcc
***********************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Max sizes for sequence.txt, subsequence.txt and number of processes
#define MAX_SEQUENCE_SIZE       1048576  // 1MB
#define MAX_SUBSEQUENCE_SIZE     10240   // 10KB
#define MAX_PROCS                  20    // 20 processes max

// Only two items live in shared memory
typedef struct {
    int best_position;
    int best_count;
} shared_results_t;

// globals for input buffers
static char   *sequence    = NULL;
static char   *target      = NULL;
static size_t  sequenceLen = 0;
static size_t  targetLen   = 0;
static int     num_procs   = 0;

// shared memory + semaphore
static char shm_name[64];
static int  shm_fd = -1;
static shared_results_t *sharedBest = NULL;

static char sem_name[64];
static sem_t *semLock = NULL;

// fwd decl
static void usage(const char *prog);

// Main
int main(int argc, char *argv[]) {
    // Check input args: program <sequence> <subsequence> <num_procs>
    if (argc != 4) {
        fprintf(stderr, "wrong number of args\n");
        usage(argv[0]);
        return 1;
    }

    // Parse number of processes and bound to MAX_PROCS
    num_procs = atoi(argv[3]);
    if (num_procs <= 0) {
        fprintf(stderr, "need positive number of processes\n");
        return 1;
    }
    if (num_procs > MAX_PROCS) {
        fprintf(stderr, "note: capping processes to %d (was %d)\n", MAX_PROCS, num_procs);
        num_procs = MAX_PROCS;
    }

    // Read sequence file into memory
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("fopen sequence"); return 1; }
    sequence = (char*)malloc(MAX_SEQUENCE_SIZE + 1);
    if (!sequence) { perror("malloc sequence"); fclose(fp); return 1; }
    sequenceLen = fread(sequence, 1, MAX_SEQUENCE_SIZE, fp);
    fclose(fp);
    sequence[sequenceLen] = '\0';  // ensure string termination

    // Read subsequence file into memory
    fp = fopen(argv[2], "rb");
    if (!fp) { perror("fopen subsequence"); free(sequence); return 1; }
    target = (char*)malloc(MAX_SUBSEQUENCE_SIZE + 1);
    if (!target) { perror("malloc subsequence"); fclose(fp); free(sequence); return 1; }
    targetLen = fread(target, 1, MAX_SUBSEQUENCE_SIZE, fp);
    fclose(fp);
    target[targetLen] = '\0';  // ensure string termination

    // Validate non-empty input files
    if (sequenceLen == 0 || targetLen == 0) {
        fprintf(stderr, "empty sequence or subsequence\n");
        free(sequence); free(target);
        return 1;
    }

    // Create unique names for kernel objects (based on PID)
    pid_t mainPid = getpid();
    snprintf(shm_name, sizeof(shm_name), "/dna_results_%ld", (long)mainPid);
    shm_unlink(shm_name);  // best-effort clear from any prior run

    // Create and size shared memory (holds only two integers)
    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) { perror("shm_open"); return 1; }
    if (ftruncate(shm_fd, (off_t)sizeof(*sharedBest)) == -1) { perror("ftruncate"); return 1; }

    // Map shared memory; mapping is inherited by children after fork()
    sharedBest = (shared_results_t*)mmap(NULL, sizeof(*sharedBest),
                                         PROT_READ | PROT_WRITE, MAP_SHARED,
                                         shm_fd, 0);
    if (sharedBest == MAP_FAILED) { perror("mmap"); return 1; }
    close(shm_fd); shm_fd = -1;  // fd not needed after mapping

    // Initialize shared result so any real match improves it
    sharedBest->best_position = -1;
    sharedBest->best_count    = -1;

    // Create named semaphore (not stored inside shared memory)
    snprintf(sem_name, sizeof(sem_name), "/dna_sem_%ld", (long)mainPid);
    sem_unlink(sem_name);
    semLock = sem_open(sem_name, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);
    if (semLock == SEM_FAILED) { perror("sem_open"); return 1; }

    // Fork workers: child i processes positions i, i+P, i+2P, ...
    int started = 0;
    for (int i = 0; i < num_procs; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // Child: compute best local match over its interleaved positions
            const int procIndex = i;
            int localBestPos = -1;
            int localBestCnt = -1;

            for (size_t pos = (size_t)procIndex; pos < sequenceLen; pos += (size_t)num_procs) {
                int matches = 0;
                for (size_t j = 0; j < targetLen; j++) {
                    size_t sidx = pos + j;
                    if (sidx >= sequenceLen) break;
                    if (sequence[sidx] == target[j]) matches++;
                }
                if (matches > localBestCnt) {
                    localBestCnt = matches;
                    localBestPos = (int)pos;
                }
            }

            if (localBestCnt >= 0) {
                if (sem_wait(semLock) == -1) { perror("sem_wait"); _exit(1); }
                if (localBestCnt > sharedBest->best_count ||
                   (localBestCnt == sharedBest->best_count && localBestPos < sharedBest->best_position)) {
                    sharedBest->best_count    = localBestCnt;
                    sharedBest->best_position = localBestPos;
                }
                if (sem_post(semLock) == -1) { perror("sem_post"); _exit(1); }
            }
            _exit(0);  // child done, exit
        } else if (pid > 0) {
            started++;
        } else {
            perror("fork");
            for (int k = 0; k < started; k++) (void)wait(NULL);
            return 1;
        }
    }

    // Wait for all children to complete
    for (int i = 0; i < started; i++) {
        if (wait(NULL) == -1) {
            perror("wait");
            return 1;
        }
    }

    // Print final results
    printf("Number of Processes: %d\n", num_procs);
    printf("Best Match Position: %d\n", sharedBest->best_position);
    printf("Best Match Count:    %d\n", sharedBest->best_count);

    return 0;
}

// Usage message
static void usage(const char *prog) {
    fprintf(stdout, "Usage: %s <Sequence File Name> <Subsequence File Name> <Num Processes>\n", prog);
    fprintf(stdout, "Example: %s sequence.txt subsequence.txt 4\n", prog);
}
