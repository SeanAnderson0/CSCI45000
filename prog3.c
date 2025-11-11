/**********************************************************************
File: prog3.c
Author: Sean Anderson
Date: November 10, 2025
Brief: Simulates virtual memory behavior using a local LRU policy
       Each process has a fixed number of frames and its own LRU list
       Input lines are "<proc> <page>" where proc is 1-4
       The program prints hit rates for P1–P4 and the overall average

Compile by: gcc -Wall prog3.c -o vvm_sim
Compiler: gcc
***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 128
#define PROCS     4


// each process uses a simple LRU cache
typedef struct {
    int *q;
    int size;
    int cap;
} lru_t;

// print error message for correct number of args
static void usage(const char *prog) {
    fprintf(stderr, "Error: %s <datafile> <p1> <p2> <p3> <p4>\n", prog);
}

// Move a found page to the MRU position
static void lru_touch(lru_t *l, int page, int idx) {
    for (int i = idx; i < l->size - 1; ++i)
        l->q[i] = l->q[i + 1];
    l->q[l->size - 1] = page;
}

// insert a new page
static void lru_insert(lru_t *l, int page) {
    if (l->cap <= 0) return; 
    if (l->size < l->cap) {
        l->q[l->size++] = page;
    } else {
        for (int i = 0; i < l->cap - 1; ++i)
            l->q[i] = l->q[i + 1];
        l->q[l->cap - 1] = page;
    }
}

// access a page, return 1 if hit, 0 if miss
static int lru_access(lru_t *l, int page) {
    for (int i = 0; i < l->size; ++i) {
        if (l->q[i] == page) {
            lru_touch(l, page, i);
            return 1;
        }
    }
    lru_insert(l, page);
    return 0;
}


int main(int argc, char *argv[]) {
    if (argc != 6) {
        usage(argv[0]);
        return 1;
    }

    const char *datafile = argv[1];

    // Get frame counts for each process
    int caps[PROCS];
    for (int i = 0; i < PROCS; ++i) {
        caps[i] = atoi(argv[2 + i]);
        if (caps[i] < 0) {
            fprintf(stderr, "Frame counts must be non-negative.\n");
            return 1;
        }
    }

    // open input data file
    FILE *fp = fopen(datafile, "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    // initialize LRU caches for each process
    lru_t lru[PROCS];
    int *bufs[PROCS] = {0};
    for (int p = 0; p < PROCS; ++p) {
        if (caps[p] > 0) {
            bufs[p] = (int *)malloc((size_t)caps[p] * sizeof(int));
            if (!bufs[p]) {
                perror("malloc");
                fclose(fp);
                for (int k = 0; k < p; ++k) free(bufs[k]);
                return 1;
            }
        }
        lru[p].q = bufs[p];
        lru[p].size = 0;
        lru[p].cap  = caps[p];
    }

    // initialize hit and reference counters
    long hits[PROCS] = {0,0,0,0};
    long refs[PROCS] = {0,0,0,0};

    // process each line of input
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '#') continue;
        // Read proc and page from data file
        int proc = 0, page = 0;
        if (sscanf(line, "%d %d", &proc, &page) != 2) continue;
        if (proc < 1 || proc > PROCS) continue;
        // update reference and hit counters
        int idx = proc - 1;
        refs[idx]++;
        if (lru_access(&lru[idx], page))
            hits[idx]++;
    }
    fclose(fp);

    // compute percentages and averages
    double pct[PROCS];
    double avg = 0.0;
    for (int i = 0; i < PROCS; ++i) {
        pct[i] = (refs[i] > 0) ? (100.0 * (double)hits[i] / (double)refs[i]) : 0.0;
        avg += pct[i];
    }
    avg /= PROCS;

    // Print results
    printf("P1=%d%%  P2=%d%%  P3=%d%%  P4=%d%%  AVG=%d%%\n",
           (int)(pct[0] + 0.5), (int)(pct[1] + 0.5),
           (int)(pct[2] + 0.5), (int)(pct[3] + 0.5),
           (int)(avg + 0.5));

    // clean up
    for (int p = 0; p < PROCS; ++p)
        free(bufs[p]);

    return 0;
}
