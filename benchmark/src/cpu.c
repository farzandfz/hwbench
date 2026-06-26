#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "benchmark.h"
#include "modules.h"

/* ---- inner loop: register-only integer workload ---- */
/* Returns iterations completed within duration_sec. */
static uint64_t cpu_workload(int duration_sec) {
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    uint64_t a = 0xdeadbeefcafeULL;
    uint64_t b = 0x123456789abcULL;
    uint64_t c = 0xfedcba9876543210ULL;
    uint64_t d = 0x0102030405060708ULL;
    uint64_t iters = 0;

    double target = (double)duration_sec;
    double elapsed = 0.0;

    while (elapsed < target) {
        /* 8 iterations unrolled per "outer" iteration for timing amortization */
        for (int i = 0; i < 100000; i++) {
            a ^= b << 13;
            b ^= c >> 7;
            c ^= d << 17;
            d ^= a;
            a *= 6364136223846793005ULL;
            b += 1442695040888963407ULL;
            c ^= (c >> 33);
            d *= 2685821657736338717ULL;
        }
        iters += 100000;
        COMPILER_BARRIER(a);
        COMPILER_BARRIER(b);
        elapsed = elapsed_sec(&ts_start);
    }
    return iters;
}

/* ---- single-core ---- */

CpuSingleResult bench_cpu_single(const BenchConfig *cfg) {
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    uint64_t iters = cpu_workload(cfg->duration_sec);
    double dur = elapsed_sec(&ts_start);

    CpuSingleResult r;
    r.iterations      = iters;
    r.duration_sec    = dur;
    r.iterations_per_sec = (double)iters / dur;
    return r;
}

/* ---- multi-core ---- */

typedef struct {
    int      duration_sec;
    uint64_t result_iters;
} ThreadArg;

static void *thread_fn(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    ta->result_iters = cpu_workload(ta->duration_sec);
    return NULL;
}

CpuMultiResult bench_cpu_multi(const BenchConfig *cfg, int num_cores,
                               double single_ips) {
    pthread_t *threads = malloc((size_t)num_cores * sizeof(pthread_t));
    ThreadArg *args    = malloc((size_t)num_cores * sizeof(ThreadArg));

    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    for (int i = 0; i < num_cores; i++) {
        args[i].duration_sec  = cfg->duration_sec;
        args[i].result_iters  = 0;
        pthread_create(&threads[i], NULL, thread_fn, &args[i]);
    }

    uint64_t total = 0;
    for (int i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
        total += args[i].result_iters;
    }
    double dur = elapsed_sec(&ts_start);

    free(threads);
    free(args);

    CpuMultiResult r;
    r.total_iterations   = total;
    r.duration_sec       = dur;
    r.iterations_per_sec = (double)total / dur;
    r.threads_used       = num_cores;
    r.scaling_factor     = single_ips > 0.0
                           ? r.iterations_per_sec / single_ips : 0.0;
    r.efficiency_percent = num_cores > 0
                           ? r.scaling_factor / num_cores * 100.0 : 0.0;
    return r;
}
