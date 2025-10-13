/*************************************************************************
File: prog2.c
Author: Sean Anderson
Date: October 13, 2025
Brief: DNA subsequence search using multiple processes with POSIX
       shared memory and a POSIX named semaphore to protect shared results.
       Each child examines interleaved starting positions (i, i+P, …).
       Output format matches the assignment exactly (3 lines).
Compile by: gcc -Wall prog2.c -o prog1 -lpthread
Compiler: gcc
**************************************************************************/

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Max input sizes
#define MAX_SEQUENCE_SIZE      1048576   // 1MB
#define MAX_SUBSEQUENCE_SIZE     10240   // 10KB

// Structure stored in shared memory for best result
typedef struct {
    int best_position;
    int best_count;
} shared_results_t;

// Global variables for sequence data
static char *seq        = NULL;
static char *subseq     = NULL;
static size_t seq_len   = 0;
static size_t subseq_len = 0;
static int num_procs    = 0;

// Shared memory and semaphore names
static char shm_name[64];
static char sem_name[64];

// Handles for shared objects
static shared_results_t *g_results = NULL;
static int shm_fd = -1;
static sem_t *g_sem = NULL;

// Function prototypes
static ssize_t read_and_filter_acgt(const char *fname, char **out, size_t max_keep);
static void cleanup_parent(void);
static void cleanup_child(void);
static void usage(const char *prog);

// Main
int main(int argc, char *argv[]) {
    // Check input arguments
    if (argc != 4) {
        fprintf(stderr, "wrong number of args\n");
        usage(argv[0]);
        return 1;
    }

    // Parse number of processes
    num_procs = atoi(argv[3]);
    if (num_procs <= 0) {
        fprintf(stderr, "need positive number of processes\n");
        return 1;
    }

    // Read sequence file (keep only A/C/G/T)
    ssize_t n_seq = read_and_filter_acgt(argv[1], &seq, MAX_SEQUENCE_SIZE);
    if (n_seq < 0) return 1;
    seq_len = (size_t)n_seq;

    // Read subsequence file (keep only A/C/G/T)
    ssize_t n_sub = read_and_filter_acgt(argv[2], &subseq, MAX_SUBSEQUENCE_SIZE);
    if (n_sub < 0) { free(seq); return 1; }
    subseq_len = (size_t)n_sub;

    // Check for empty files
    if (subseq_len == 0 || seq_len == 0) {
        fprintf(stderr, "empty sequence or subsequence\n");
        free(seq);
        free(subseq);
        return 1;
    }

    // Create unique names for this run
    pid_t self = getpid();
    snprintf(shm_name, sizeof(shm_name), "/dna_results_%ld", (long)self);
    snprintf(sem_name, sizeof(sem_name), "/dna_lock_%ld", (long)self);

    // Create shared memory for results
    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        free(seq);
        free(subseq);
        return 1;
    }

    // Resize shared memory to struct size
    if (ftruncate(shm_fd, (off_t)sizeof(shared_results_t)) == -1) {
        perror("ftruncate failed");
        cleanup_parent();
        return 1;
    }

    // Map shared memory
    g_results = (shared_results_t *)mmap(NULL, sizeof(shared_results_t),
                                         PROT_READ | PROT_WRITE, MAP_SHARED,
                                         shm_fd, 0);
    if (g_results == MAP_FAILED) {
        perror("mmap failed");
        cleanup_parent();
        return 1;
    }

    // Initialize shared results
    g_results->best_position = -1;
    g_results->best_count    = -1;

    // Create semaphore for synchronization
    g_sem = sem_open(sem_name, O_CREAT | O_EXCL, 0666, 1);
    if (g_sem == SEM_FAILED) {
        perror("sem_open failed");
        cleanup_parent();
        return 1;
    }

    // Track number of successful forks
    int started = 0;

    // Create child processes
    for (int i = 0; i < num_procs; i++) {
        pid_t pid = fork();

        // Child process
        if (pid == 0) {
            cleanup_child();

            // Open shared memory in child
            int local_fd = shm_open(shm_name, O_RDWR, 0);
            if (local_fd == -1) {
                perror("child shm_open failed");
                _exit(1);
            }

            // Map shared memory
            shared_results_t *res = (shared_results_t *)mmap(NULL, sizeof(shared_results_t),
                                                             PROT_READ | PROT_WRITE, MAP_SHARED,
                                                             local_fd, 0);
            if (res == MAP_FAILED) {
                perror("child mmap failed");
                close(local_fd);
                _exit(1);
            }

            // Open semaphore
            sem_t *lock = sem_open(sem_name, 0);
            if (lock == SEM_FAILED) {
                perror("child sem_open failed");
                munmap(res, sizeof(*res));
                close(local_fd);
                _exit(1);
            }

            // Each child searches interleaved starting positions
            int worker_id = i;
            int best_pos = -1;
            int best_cnt = -1;

            // Iterate through positions assigned to this process
            for (size_t pos = (size_t)worker_id; pos < seq_len; pos += (size_t)num_procs) {
                int matches = 0;

                // Count matching characters between seq and subseq
                for (size_t j = 0; j < subseq_len; j++) {
                    size_t sidx = pos + j;
                    if (sidx < seq_len && seq[sidx] == subseq[j]) {
                        matches++;
                    } else if (sidx >= seq_len) {
                        break;
                    }
                }

                // Track best local match
                if (matches > best_cnt) {
                    best_cnt = matches;
                    best_pos = (int)pos;
                }
            }

            // Lock semaphore before updating shared memory
            int rc = 0;
            if (best_cnt >= 0) {
                if (sem_wait(lock) == -1) {
                    perror("sem_wait failed");
                    rc = 1;
                } else {
                    // Update global best result if this one is better
                    if (best_cnt > res->best_count ||
                        (best_cnt == res->best_count && best_pos < res->best_position)) {
                        res->best_count    = best_cnt;
                        res->best_position = best_pos;
                    }
                    if (sem_post(lock) == -1) {
                        perror("sem_post failed");
                        rc = 1;
                    }
                }
            }

            // Clean up child handles
            sem_close(lock);
            munmap(res, sizeof(*res));
            close(local_fd);
            _exit(rc);

        // Parent process
        } else if (pid > 0) {
            started++;
        } else {
            // Fork failed
            perror("fork failed");
            for (int k = 0; k < started; k++) {
                (void)wait(NULL);
            }
            cleanup_parent();
            return 1;
        }
    }

    // Wait for all child processes to finish
    for (int i = 0; i < started; i++) {
        int wstatus = 0;
        if (wait(&wstatus) == -1) {
            perror("wait failed");
            cleanup_parent();
            return 1;
        }
        if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
            fprintf(stderr, "child process error\n");
            cleanup_parent();
            return 1;
        }
    }

    // Required output format
    printf("Number of Processes: %d\n", num_procs);
    printf("Best Match Position: %d\n", g_results->best_position);
    printf("Best Match Count:    %d\n", g_results->best_count);

    // Cleanup
    cleanup_parent();
    return 0;
}

// Reads input file and filters out invalid characters (only A/C/G/T allowed)
static ssize_t read_and_filter_acgt(const char *fname, char **out, size_t max_keep) {
    FILE *fp = fopen(fname, "rb");
    if (!fp) {
        perror("fopen failed");
        return -1;
    }

    // Temporary buffer for reading file
    size_t cap = (MAX_SEQUENCE_SIZE > MAX_SUBSEQUENCE_SIZE ?
                  MAX_SEQUENCE_SIZE : MAX_SUBSEQUENCE_SIZE) + 1;
    char *tmp = (char *)malloc(cap);
    if (!tmp) {
        perror("malloc failed");
        fclose(fp);
        return -1;
    }

    // Read file into buffer
    size_t total = 0;
    for (;;) {
        size_t space = cap - total;
        if (space == 0) break;
        size_t n = fread(tmp + total, 1, space, fp);
        total += n;
        if (n < space) {
            if (feof(fp)) break;
            if (ferror(fp)) {
                perror("fread failed");
                free(tmp);
                fclose(fp);
                return -1;
            }
        }
    }
    fclose(fp);

    // Allocate filtered output buffer
    char *dst = (char *)malloc(max_keep + 1);
    if (!dst) {
        perror("malloc failed");
        free(tmp);
        return -1;
    }

    // Copy only valid DNA bases
    size_t keep = 0;
    for (size_t i = 0; i < total; i++) {
        char c = tmp[i];
        if (c == 'A' || c == 'C' || c == 'G' || c == 'T') {
            if (keep >= max_keep) {
                fprintf(stderr, "file '%s' too big after filtering (max %zu)\n",
                        fname, max_keep);
                free(tmp);
                free(dst);
                return -1;
            }
            dst[keep++] = c;
        } else if (c == 'a' || c == 'c' || c == 'g' || c == 't') {
            if (keep >= max_keep) {
                fprintf(stderr, "file '%s' too big after filtering (max %zu)\n",
                        fname, max_keep);
                free(tmp);
                free(dst);
                return -1;
            }
            dst[keep++] = (char)('A' + (c - 'a'));
        }
    }
    dst[keep] = '\0';
    free(tmp);

    *out = dst;
    return (ssize_t)keep;
}

// Cleans up shared memory, semaphores, and allocated memory
static void cleanup_parent(void) {
    if (seq)      { free(seq);    seq = NULL; }
    if (subseq)   { free(subseq); subseq = NULL; }
    if (g_results && g_results != MAP_FAILED) {
        munmap(g_results, sizeof(*g_results));
        g_results = NULL;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
    if (shm_name[0]) {
        shm_unlink(shm_name);
        shm_name[0] = '\0';
    }
    if (g_sem && g_sem != SEM_FAILED) {
        sem_close(g_sem);
        g_sem = NULL;
    }
    if (sem_name[0]) {
        sem_unlink(sem_name);
        sem_name[0] = '\0';
    }
}

// No cleanup needed in child (resources are parent-owned)
static void cleanup_child(void) {
    (void)shm_fd;
    (void)g_results;
    (void)g_sem;
}

// Prints usage instructions
static void usage(const char *prog) {
    fprintf(stdout, "Usage: %s <seq_file> <subseq_file> <num_procs>\n", prog);
    fprintf(stdout, "seq_file: main DNA sequence (max 1MB)\n");
    fprintf(stdout, "subseq_file: DNA to search for (max 10KB)\n");
    fprintf(stdout, "num_procs: number of processes\n");
    fprintf(stdout, "Example: %s sequence.txt subsequence.txt 4\n", prog);
}
