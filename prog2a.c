/** ***********************************************************************
File: prog2.c
Author: Sean Anderson
Date: October 13, 2025
Brief: Searches for the best match of a DNA subsequence within a larger
sequence using multiple processes. Each child examines every P-th index
(starting from its ID), with results stored in POSIX shared memory and
protected by a named semaphore. The final output prints the number of
processes, the best match position, and the match count—each on its own line.

Compile by: gcc -Wall prog2.c -o prog2 -lpthread
Compiler: gcc
***************************************************************************/

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

/* Max sizes for sequence.txt, subsequence.txt and number of processes  */
#define MAX_SEQUENCE_SIZE       1048576  /* 1MB  */
#define MAX_SUBSEQUENCE_SIZE     10240   /* 10KB */
#define MAX_PROCS                  20    /* per spec */

/* Only two items live in shared memory */
typedef struct {
    int best_position;  /* lowest index on ties */
    int best_count;
} shared_results_t;

/* globals for input buffers */
static char   *sequence    = NULL;
static char   *target      = NULL;
static size_t  sequenceLen = 0;
static size_t  targetLen   = 0;
static int     num_procs   = 0;

/* shared memory + semaphore */
static char shm_name[64];
static int  shm_fd = -1;
static shared_results_t *sharedBest = NULL;

static char sem_name[64];
static sem_t *semLock = NULL;

/* simple error helper */
#define DIE(MSG) do { perror(MSG); cleanup_parent(); return 1; } while (0)

/* fwd decls */
static void cleanup_parent(void);
static void usage(const char *prog);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "wrong number of args\n");
        usage(argv[0]);
        return 1;
    }

    num_procs = atoi(argv[3]);
    if (num_procs <= 0) {
        fprintf(stderr, "need positive number of processes\n");
        return 1;
    }
    if (num_procs > MAX_PROCS) {
        fprintf(stderr, "note: capping processes to %d (was %d)\n", MAX_PROCS, num_procs);
        num_procs = MAX_PROCS;
    }

    /* read sequence file */
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("fopen sequence"); return 1; }
    sequence = (char*)malloc(MAX_SEQUENCE_SIZE + 1);
    if (!sequence) { perror("malloc sequence"); fclose(fp); return 1; }
    sequenceLen = fread(sequence, 1, MAX_SEQUENCE_SIZE, fp);
    fclose(fp);
    sequence[sequenceLen] = '\0';

    /* read subsequence file */
    fp = fopen(argv[2], "rb");
    if (!fp) { perror("fopen subsequence"); free(sequence); return 1; }
    target = (char*)malloc(MAX_SUBSEQUENCE_SIZE + 1);
    if (!target) { perror("malloc subsequence"); fclose(fp); free(sequence); return 1; }
    targetLen = fread(target, 1, MAX_SUBSEQUENCE_SIZE, fp);
    fclose(fp);
    target[targetLen] = '\0';

    if (sequenceLen == 0 || targetLen == 0) {
        fprintf(stderr, "empty sequence or subsequence\n");
        free(sequence); free(target);
        return 1;
    }

    /* name resources by PID; unlink first to avoid leftovers */
    pid_t mainPid = getpid();
    snprintf(shm_name, sizeof(shm_name), "/dna_results_%ld", (long)mainPid);
    shm_unlink(shm_name);

    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) { free(sequence); free(target); DIE("shm_open"); }
    if (ftruncate(shm_fd, (off_t)sizeof(*sharedBest)) == -1) DIE("ftruncate");

    sharedBest = (shared_results_t*)mmap(NULL, sizeof(*sharedBest),
                                         PROT_READ | PROT_WRITE, MAP_SHARED,
                                         shm_fd, 0);
    if (sharedBest == MAP_FAILED) DIE("mmap");
    close(shm_fd); shm_fd = -1;

    /* init shared best; -1 so any real match is better */
    sharedBest->best_position = -1;
    sharedBest->best_count    = -1;

    snprintf(sem_name, sizeof(sem_name), "/dna_sem_%ld", (long)mainPid);
    sem_unlink(sem_name);
    semLock = sem_open(sem_name, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);
    if (semLock == SEM_FAILED) DIE("sem_open");

    /* spawn children: process i checks indices i, i+P, i+2P, ... */
    int started = 0;
    for (int i = 0; i < num_procs; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            const int procIndex = i;
            int localBestPos = -1;
            int localBestCnt = -1;

            for (size_t pos = (size_t)procIndex; pos < sequenceLen; pos += (size_t)num_procs) {
                int matches = 0;

                /* compare window; break if we run off the end (extreme end cases allowed) */
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

            /* publish local best with minimal critical section */
            if (localBestCnt >= 0) {
                if (sem_wait(semLock) == -1) { perror("sem_wait"); _exit(1); }
                if (localBestCnt > sharedBest->best_count ||
                   (localBestCnt == sharedBest->best_count && localBestPos < sharedBest->best_position)) {
                    sharedBest->best_count    = localBestCnt;
                    sharedBest->best_position = localBestPos;
                }
                if (sem_post(semLock) == -1) { perror("sem_post"); _exit(1); }
            }
            _exit(0);
        } else if (pid > 0) {
            started++;
        } else {
            perror("fork");
            for (int k = 0; k < started; k++) (void)wait(NULL);
            cleanup_parent();
            return 1;
        }
    }

    /* wait for all children */
    for (int i = 0; i < started; i++) {
        int st = 0;
        if (wait(&st) == -1) DIE("wait");
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            fprintf(stderr, "child process error\n");
            cleanup_parent();
            return 1;
        }
    }

    /* required 3-line output */
    printf("Number of Processes: %d\n", num_procs);
    printf("Best Match Position: %d\n", sharedBest->best_position);
    printf("Best Match Count:    %d\n", sharedBest->best_count);

    cleanup_parent();
    return 0;
}

static void cleanup_parent(void) {
    if (sequence)  { free(sequence);  sequence = NULL; }
    if (target)    { free(target);    target = NULL; }

    if (semLock && semLock != SEM_FAILED) { sem_close(semLock); semLock = NULL; }
    if (sem_name[0]) { sem_unlink(sem_name); sem_name[0] = '\0'; }

    if (sharedBest && sharedBest != MAP_FAILED) { munmap(sharedBest, sizeof(*sharedBest)); sharedBest = NULL; }
    if (shm_name[0]) { shm_unlink(shm_name); shm_name[0] = '\0'; }

    if (shm_fd != -1) { close(shm_fd); shm_fd = -1; }
}

static void usage(const char *prog) {
    fprintf(stdout, "Usage: %s <Sequence File Name> <Subsequence File Name> <Num Processes>\n", prog);
    fprintf(stdout, "Example: %s sequence.txt subsequence.txt 4\n", prog);
}
