/*************************************************************************
File: prog2.c
Author: Sean Anderson
Date: October 13, 2025
Brief: DNA subsequence search using multiple processes with POSIX
       shared memory and a POSIX semaphore (unnamed, pshared) to
       guard updates to the shared "best" result. Each child processes
       interleaved positions (i, i+P, ...). Output matches the spec.
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

/* Limits from assignment */
#define MAX_SEQUENCE_SIZE       1048576  /* 1MB */
#define MAX_SUBSEQUENCE_SIZE      10240  /* 10KB */

/* Shared results (now includes an unnamed, process-shared semaphore) */
typedef struct {
    int   best_position;
    int   best_count;
    sem_t lock;          /* pshared semaphore protecting updates */
} shared_results_t;

/* Globals for input data */
static char   *seq         = NULL;
static char   *subseq      = NULL;
static size_t  seq_len     = 0;
static size_t  subseq_len  = 0;
static int     num_procs   = 0;

/* POSIX shared memory */
static char shm_name[64];
static int  shm_fd = -1;
static shared_results_t *g_results = NULL;

/* Small error helper (keeps behavior identical, reduces boilerplate) */
#define DIE(MSG) do { perror(MSG); cleanup_parent(); return 1; } while (0)

/* Prototypes */
static ssize_t read_and_filter_acgt(const char *fname, char **out, size_t max_keep);
static void    cleanup_parent(void);
static void    usage(const char *prog);

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

    /* Read inputs (filter to A/C/G/T and uppercase) */
    ssize_t n_seq = read_and_filter_acgt(argv[1], &seq, MAX_SEQUENCE_SIZE);
    if (n_seq < 0) return 1;
    seq_len = (size_t)n_seq;

    ssize_t n_sub = read_and_filter_acgt(argv[2], &subseq, MAX_SUBSEQUENCE_SIZE);
    if (n_sub < 0) { free(seq); return 1; }
    subseq_len = (size_t)n_sub;

    if (seq_len == 0 || subseq_len == 0) {
        fprintf(stderr, "empty sequence or subsequence\n");
        free(seq); free(subseq);
        return 1;
    }
    /* Optional guard: pointless to spawn more workers than start positions */
    if ((size_t)num_procs > seq_len) {
        fprintf(stderr, "num_procs must be <= sequence length (%zu)\n", seq_len);
        free(seq); free(subseq);
        return 1;
    }

    /* Unique shm name per run; best-effort pre-unlink avoids stale leftovers */
    pid_t self = getpid();
    snprintf(shm_name, sizeof(shm_name), "/dna_results_%ld", (long)self);
    shm_unlink(shm_name);

    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) { free(seq); free(subseq); DIE("shm_open failed"); }
    if (ftruncate(shm_fd, (off_t)sizeof(*g_results)) == -1) DIE("ftruncate failed");

    g_results = (shared_results_t *)mmap(NULL, sizeof(*g_results),
                                         PROT_READ | PROT_WRITE, MAP_SHARED,
                                         shm_fd, 0);
    if (g_results == MAP_FAILED) DIE("mmap failed");

    /* Parent no longer needs the FD after mapping */
    close(shm_fd); shm_fd = -1;

    /* Initialize shared state + unnamed pshared semaphore */
    g_results->best_position = -1;
    g_results->best_count    = -1;
    if (sem_init(&g_results->lock, /*pshared=*/1, /*value=*/1) == -1) DIE("sem_init failed");

    /* Fork workers; children reuse inherited mapping + semaphore */
    int started = 0;
    for (int i = 0; i < num_procs; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            /* --- Child: compute local best, then single guarded update --- */
            const int worker_id = i;
            int best_pos = -1;
            int best_cnt = -1;

            for (size_t pos = (size_t)worker_id; pos < seq_len; pos += (size_t)num_procs) {
                int matches = 0;
                for (size_t j = 0; j < subseq_len; j++) {
                    size_t sidx = pos + j;
                    if (sidx >= seq_len) break;
                    if (seq[sidx] == subseq[j]) matches++;
                }
                if (matches > best_cnt) { best_cnt = matches; best_pos = (int)pos; }
            }

            int rc = 0;
            if (best_cnt >= 0) {
                if (sem_wait(&g_results->lock) == -1) { perror("sem_wait failed"); _exit(1); }
                if (best_cnt > g_results->best_count ||
                   (best_cnt == g_results->best_count && best_pos < g_results->best_position)) {
                    g_results->best_count    = best_cnt;
                    g_results->best_position = best_pos;
                }
                if (sem_post(&g_results->lock) == -1) { perror("sem_post failed"); _exit(1); }
            }
            _exit(rc);
        } else if (pid > 0) {
            started++;
        } else {
            perror("fork failed");
            for (int k = 0; k < started; k++) (void)wait(NULL);
            cleanup_parent();
            return 1;
        }
    }

    /* Wait for all children */
    for (int i = 0; i < started; i++) {
        int wstatus = 0;
        if (wait(&wstatus) == -1) DIE("wait failed");
        if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
            fprintf(stderr, "child process error\n");
            cleanup_parent();
            return 1;
        }
    }

    /* Exact required output (3 lines) */
    printf("Number of Processes: %d\n", num_procs);
    printf("Best Match Position: %d\n", g_results->best_position);
    printf("Best Match Count:    %d\n", g_results->best_count);

    cleanup_parent();
    return 0;
}

/* Reads file, keeps only A/C/G/T (case-insensitive), stores uppercase */
static ssize_t read_and_filter_acgt(const char *fname, char **out, size_t max_keep) {
    FILE *fp = fopen(fname, "rb");
    if (!fp) { perror("fopen failed"); return -1; }

    char *dst = (char *)malloc(max_keep + 1);
    if (!dst) { perror("malloc failed"); fclose(fp); return -1; }

    size_t keep = 0;
    unsigned char buf[65536];
    while (!feof(fp)) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (ferror(fp)) { perror("fread failed"); free(dst); fclose(fp); return -1; }
        for (size_t i = 0; i < n; i++) {
            unsigned char c = buf[i];
            if (c=='A'||c=='C'||c=='G'||c=='T'||c=='a'||c=='c'||c=='g'||c=='t') {
                if (keep >= max_keep) {
                    fprintf(stderr, "file '%s' too big after filtering (max %zu)\n", fname, max_keep);
                    free(dst); fclose(fp); return -1;
                }
                dst[keep++] = (char)((c>='a'&&c<='z') ? ('A' + (c - 'a')) : c);
            }
        }
    }
    dst[keep] = '\0';
    fclose(fp);
    *out = dst;
    return (ssize_t)keep;
}

static void cleanup_parent(void) {
    if (seq)    { free(seq);    seq = NULL; }
    if (subseq) { free(subseq); subseq = NULL; }
    if (g_results && g_results != MAP_FAILED) {
        /* Destroy semaphore before unmapping shared region */
        sem_destroy(&g_results->lock);
        munmap(g_results, sizeof(*g_results));
        g_results = NULL;
    }
    if (shm_name[0]) { shm_unlink(shm_name); shm_name[0] = '\0'; }
    if (shm_fd != -1) { close(shm_fd); shm_fd = -1; }
}

static void usage(const char *prog) {
    fprintf(stdout, "Usage: %s <Sequence File Name> <Subsequence File Name> <Num Processes>\n", prog);
    fprintf(stdout, "Example: %s sequence.txt subsequence.txt 4\n", prog);
}

