#define _POSIX_C_SOURCE 200809L
#include "lwp.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static size_t iterations = 100000;
static void *ping(void *arg) {
    (void)arg;
    for (size_t i = 0; i < iterations; ++i)
        if (lwp_yield()) abort();
    return NULL;
}
static double seconds(struct timespec t) { return (double)t.tv_sec + t.tv_nsec / 1e9; }
int main(void) {
    lwp_tid a, b;
    struct timespec start, finish;
    if (lwp_create(&a, ping, NULL, 0) || lwp_create(&b, ping, NULL, 0)) return 1;
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (lwp_run()) return 1;
    clock_gettime(CLOCK_MONOTONIC, &finish);
    double elapsed = seconds(finish) - seconds(start);
    printf("threads=2 yields=%zu elapsed_seconds=%.6f ns_per_yield=%.1f\n",
           2 * iterations, elapsed, elapsed * 1e9 / (2 * iterations));
    puts("Includes scheduler bookkeeping and two context transfers per yield; not a CPU-independent benchmark.");
    return lwp_destroy() != 0;
}
