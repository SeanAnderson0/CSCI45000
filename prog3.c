/**********************************************************************
File: prog3.c
Author: Sean Anderson
Date: November 10, 2025
Brief: Simulates "virtual virtual memory" with a local LRU page policy.
       Input lines are "<proc> <logical_page>" where proc is 1..4.
       We allocate a fixed number of frames to each process and track
       LRU hits/misses per process. Output is the four hit rates and
       their overall average, all in percentages.

Compile by: gcc -Wall prog3.c -o vvm_sim
Compiler: gcc
***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 128
#define PROCS     4

// Small LRU cache for a single process.
// We keep it simple on purpose: an array used as a queue where
// index 0 is LRU and the end is MRU. For this assignment size,
// O(cap) ops are fine and easy to read/maintain.
typedef struct {
    int *q;        // pages
    int size;      // current count
    int cap;       // max frames allocated
} lru_t;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <datafile> <p1_frames> <p2_frames> <p3_frames> <p4_frames>\n",
        prog);
}

// Move the page already in the cache to MRU position.
static void lru_touch(lru_t *l, int page, int idx) {
    // shift [idx+1..size-1] left and put page at end
    for (int i = idx; i < l->size - 1; ++i) l->q[i] = l->q[i + 1];
    l->q[l->size - 1] = page;
}

// Insert a page on miss (evict LRU if full), page becomes MRU.
static void lru_insert(lru_t *l, int page) {
    if (l->cap <= 0) return; // allocation 0 -> always miss, nothing to store
    if (l->size < l->cap) {
        l->q[l->size++] = page;
    } else {
        // evict LRU at index 0; shift left
        for (int i = 0; i < l->cap - 1; ++i) l->q[i] = l->q[i + 1];
        l->q[l->cap - 1] = page;
        // size stays at cap
    }
}

// Return 1 if hit and move to MRU; 0 if miss.
static int lru_access(lru_t *l, int page) {
    // linear search, move-to-MRU on hit
    for (int i = 0; i < l->size; ++i) {
        if (l->q[i] == page) {
            lru_touch(l, page, i);
            return 1; // hit
        }
    }
    lru_insert(l, page); // miss path
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        usage(argv[0]);
        return 1;
    }

    const char *datafile = argv[1];

    // Parse fixed allocations for the 4 processes (local policy).
    int caps[PROCS];
    for (int i = 0; i < PROCS; ++i) {
        caps[i] = atoi(argv[2 + i]);
        if (caps[i] < 0) {
            fprintf(stderr, "Frame counts must be non-negative.\n");
            return 1;
        }
    }

    FILE *fp = fopen(datafile, "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    // Build four LRU caches and counters.
    lru_t lru[PROCS];
    int *bufs[PROCS] = {0};
    for (int p = 0; p < PROCS; ++p) {
        if (caps[p] > 0) {
            bufs[p] = (int *)malloc(sizeof(int) * caps[p]);
            if (!bufs[p]) {
                perror("malloc");
                fclose(fp);
                // free any previous
                for (int k = 0; k < p; ++k) free(bufs[k]);
                return 1;
            }
        }
        lru[p].q = bufs[p];
        lru[p].size = 0;
        lru[p].cap = caps[p];
    }

    long hits[PROCS]  = {0,0,0,0};
    long refs[PROCS]  = {0,0,0,0};

    // Read "<proc> <page>" per line; proc is 1..4
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        // skip blank/comment-ish lines
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '#') continue;

        int proc = 0, page = 0;
        if (sscanf(line, "%d %d", &proc, &page) != 2) continue;
        if (proc < 1 || proc > PROCS) continue; // ignore junk

        int idx = proc - 1;
        refs[idx]++;

        // Local policy: only that process' cache is considered.
        if (lru_access(&lru[idx], page)) {
            hits[idx]++;
        }
    }
    fclose(fp);

    // Compute percentages per process; cap=0 -> always 0% (and refs may be >0).
    double pct[PROCS];
    double avg = 0.0;
    for (int i = 0; i < PROCS; ++i) {
        pct[i] = (refs[i] > 0) ? (100.0 * (double)hits[i] / (double)refs[i]) : 0.0;
        avg += pct[i];
    }
    avg /= PROCS;

    // Print in a simple, spreadsheet-friendly way (two decimals).
    // Example: P1=37.00% P2=39.20% P3=21.60% P4=21.60% AVG=29.85%
    printf("P1=%.2f%%  P2=%.2f%%  P3=%.2f%%  P4=%.2f%%  AVG=%.2f%%\n",
           pct[0], pct[1], pct[2], pct[3], avg);

    for (int p = 0; p < PROCS; ++p) free(bufs[p]);
    return 0;
}
