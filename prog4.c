/**********************************************************************
File:   prog4.c
Author: Sean Anderson
Date:   November 29, 2025
Brief:  Simulates disk arm movement using FIFO, SSTF, or C-SCAN.
        Reads cylinder requests from a file, keeps a fixed-size queue,
        and calculates the average time each request waits. Uses the
        seek + latency costs given in the assignment.

Compile by: gcc -Wall prog4.c -o prog4
***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_CYLS   1024    // cylinders 0..1023
#define START_STOP 2.0     // 1 ms start + 1 ms stop
#define DIST_COST  0.15    // ms per cylinder
#define LATENCY    4.2     // rotational latency

// request node for the queue
typedef struct {
    int cyl;        // cylinder
    double wait;    // total wait time
} req_t;

// supported algorithms
typedef enum {
    ALG_FIFO = 0,
    ALG_SSTF,
    ALG_CSCAN
} alg_t;

// usage info
static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s <algorithm> <queue_size> <input_file>\n"
        "  FIFO | SSTF | CSCAN\n", p);
}

// parse algorithm string
static int parse_algorithm(const char *s) {
    char b[16];
    size_t i;

    // uppercase the input
    for (i = 0; i < sizeof(b) - 1 && s[i]; ++i)
        b[i] = (char)toupper((unsigned char)s[i]);
    b[i] = '\0';

    if (!strcmp(b, "FIFO"))  return ALG_FIFO;
    if (!strcmp(b, "SSTF"))  return ALG_SSTF;
    if (!strcmp(b, "CSCAN")) return ALG_CSCAN;
    return -1;
}

// compute movement time
static double seek_time_ms(int from, int to) {
    if (from == to)
        return LATENCY; // no movement, just latency

    int d = abs(to - from);
    return START_STOP + (double)d * DIST_COST + LATENCY;
}

// FIFO: first request in queue
static int pick_fifo(req_t *q, int count, int cur) {
    (void)q; (void)count; (void)cur;
    return 0;
}

// SSTF: shortest seek
static int pick_sstf(req_t *q, int count, int cur) {
    int idx = 0;
    int best = abs(q[0].cyl - cur);

    for (int i = 1; i < count; ++i) {
        int d = abs(q[i].cyl - cur);
        if (d < best) {
            best = d;
            idx = i;
        }
    }
    return idx;
}

// C-SCAN: pick smallest cylinder >= current, else wrap
static int pick_cscan(req_t *q, int count, int cur) {
    int up_idx = -1;
    int up_delta = 0;
    int min_idx = 0;
    int min_c = q[0].cyl;

    for (int i = 0; i < count; ++i) {
        int c = q[i].cyl;

        // track global min
        if (c < min_c) {
            min_c = c;
            min_idx = i;
        }

        // upward direction choice
        if (c >= cur) {
            int d = c - cur;
            if (up_idx == -1 || d < up_delta) {
                up_idx = i;
                up_delta = d;
            }
        }
    }

    return (up_idx != -1) ? up_idx : min_idx;
}

// choose request index
static int pick_index(alg_t a, req_t *q, int n, int cur) {
    switch (a) {
        case ALG_FIFO:  return pick_fifo(q, n, cur);
        case ALG_SSTF:  return pick_sstf(q, n, cur);
        case ALG_CSCAN: return pick_cscan(q, n, cur);
        default:        return 0;
    }
}

int main(int argc, char *argv[]) {

    if (argc != 4) {
        usage(argv[0]);
        return 1;
    }

    // parse algorithm
    int alg_val = parse_algorithm(argv[1]);
    if (alg_val < 0) {
        fprintf(stderr, "Error: bad algorithm '%s'.\n", argv[1]);
        return 1;
    }
    alg_t alg = (alg_t)alg_val;

    // parse queue size
    int qsize = atoi(argv[2]);
    if (qsize <= 0) {
        fprintf(stderr, "Error: queue_size must be positive.\n");
        return 1;
    }

    // open file
    FILE *fp = fopen(argv[3], "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    // queue allocation
    req_t *queue = malloc((size_t)qsize * sizeof(req_t));
    if (!queue) {
        perror("malloc");
        fclose(fp);
        return 1;
    }

    int current = 0;    // disk arm starts at cyl 0
    int qcount = 0;
    long processed = 0;

    // initial fill
    int cyl;
    while (qcount < qsize && fscanf(fp, "%d", &cyl) == 1) {
        queue[qcount].cyl = cyl;
        queue[qcount].wait = 0.0;
        ++qcount;
    }

    double total = 0.0; // sum of all finished wait times

    // run until queue is empty
    while (qcount > 0) {

        // choose next request
        int idx = pick_index(alg, queue, qcount, current);
        int target = queue[idx].cyl;

        // compute movement time
        double step = seek_time_ms(current, target);

        // all requests wait this long
        for (int i = 0; i < qcount; ++i)
            queue[i].wait += step;

        // complete request
        double done = queue[idx].wait;

        // shift queue
        for (int i = idx + 1; i < qcount; ++i)
            queue[i - 1] = queue[i];

        --qcount;
        total += done;
        ++processed;
        current = target;

        // add next request from file
        if (fscanf(fp, "%d", &cyl) == 1) {
            queue[qcount].cyl = cyl;
            queue[qcount].wait = 0.0;
            ++qcount;
        }
    }

    fclose(fp);

    double avg = (processed > 0) ? total / processed : 0.0;

    const char *name =
        (alg == ALG_FIFO)  ? "FIFO" :
        (alg == ALG_SSTF)  ? "SSTF" :
                             "CSCAN";

    printf("Algorithm: %s  Queue: %d  File: %s\n", name, qsize, argv[3]);
    printf("Processed: %ld\n", processed);
    printf("Average delay: %.2f ms\n", avg); // updated to 2 decimals

    free(queue);
    return 0;
}
