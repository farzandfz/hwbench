#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "benchmark.h"
#include "modules.h"

/* Each sub-benchmark receives 1/3 of cpu_duration_sec (min 3s) so the
   whole CPU single/multi module finishes in ~cpu_duration_sec wall-clock. */
static int sub_dur(const BenchConfig *cfg) {
    int d = cfg->cpu_duration_sec / 3;
    return d < 3 ? 3 : d;
}

/* ---- Sub-benchmark 1: latency-bound dependent chain ---- */
static uint64_t cpu_latency(int duration_sec) {
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    uint64_t a = 0xdeadbeefcafeULL, b = 0x123456789abcULL,
             c = 0xfedcba9876543210ULL, d = 0x0102030405060708ULL;
    uint64_t iters = 0;
    double elapsed = 0.0;

    while (elapsed < (double)duration_sec) {
        for (int i = 0; i < 100000; i++) {
            a ^= b << 13;
            b ^= c >> 7;
            c ^= d << 17;
            d ^= a;
            a *= 6364136223846793005ULL;
            b += 1442695040888963407ULL;
            c ^= (c >> 33);
            d *= 2685821657736338717ULL;
            __asm__ volatile("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d) :: "memory");
        }
        iters += 100000;
        elapsed = elapsed_sec(&ts_start);
    }
    return iters;
}

/* ---- Sub-benchmark 2: throughput-bound (4 independent chains) ---- */
static uint64_t cpu_throughput(int duration_sec) {
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    uint64_t seed = 0xdeadbeefcafeULL;
    uint64_t a0 = seed ^ 0x111, a1 = seed ^ 0x222,
             a2 = seed ^ 0x333, a3 = seed ^ 0x444;
    uint64_t iters = 0;
    double elapsed = 0.0;

    while (elapsed < (double)duration_sec) {
        for (int i = 0; i < 100000; i++) {
            a0 = a0 * 6364136223846793005ULL + 1442695040888963407ULL;
            a1 = a1 * 6364136223846793005ULL + 1442695040888963407ULL;
            a2 = a2 * 6364136223846793005ULL + 1442695040888963407ULL;
            a3 = a3 * 6364136223846793005ULL + 1442695040888963407ULL;
            __asm__ volatile("" : "+r"(a0), "+r"(a1), "+r"(a2), "+r"(a3) :: "memory");
        }
        iters += 100000;
        elapsed = elapsed_sec(&ts_start);
    }
    volatile uint64_t sink = a0 ^ a1 ^ a2 ^ a3;
    (void)sink;
    return iters;
}

/* ---- Sub-benchmark 3: memory-bound (4 MB array, L2 pressure) ---- */
static uint64_t cpu_memory_bound(int duration_sec) {
    /* N is a power-of-2 so we can use & instead of % for the index wrap */
    size_t N = (4 * 1024 * 1024) / sizeof(uint64_t); /* 524288, i.e. 2^19 */
    uint64_t *arr = malloc(N * sizeof(uint64_t));
    if (!arr) return 0;
    for (size_t i = 0; i < N; i++)
        arr[i] = i * 6364136223846793005ULL + 1;

    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    uint64_t acc = 0;
    size_t   idx = 0;
    uint64_t iters = 0;
    double elapsed = 0.0;
    size_t mask = N - 1;

    while (elapsed < (double)duration_sec) {
        for (int i = 0; i < 10000; i++) {
            acc += arr[idx];
            acc *= 6364136223846793005ULL;
            idx = (idx + 1) & mask;
        }
        iters += 10000;
        __asm__ volatile("" : "+r"(acc), "+r"(idx) :: "memory");
        elapsed = elapsed_sec(&ts_start);
    }
    free(arr);
    volatile uint64_t sink = acc;
    (void)sink;
    return iters;
}

/* ---- single-core ---- */

CpuSingleResult bench_cpu_single(const BenchConfig *cfg) {
    int d = sub_dur(cfg);
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t dep = cpu_latency(d);
    double dep_t = elapsed_sec(&ts);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t thr = cpu_throughput(d);
    double thr_t = elapsed_sec(&ts);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t mem = cpu_memory_bound(d);
    double mem_t = elapsed_sec(&ts);

    CpuSingleResult r;
    r.dep_chain_ips        = (double)dep / dep_t;
    r.indep_throughput_ips = (double)thr / thr_t;
    r.memory_bound_ips     = (double)mem / mem_t;
    r.duration_sec         = dep_t; /* representative; all ~same */
    return r;
}

/* ---- multi-core ---- */

typedef struct {
    int      duration_sec; /* per-sub-benchmark */
    uint64_t dep_iters;
    uint64_t thr_iters;
    uint64_t mem_iters;
} ThreadArg;

static void *thread_fn(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    ta->dep_iters = cpu_latency(ta->duration_sec);
    ta->thr_iters = cpu_throughput(ta->duration_sec);
    ta->mem_iters = cpu_memory_bound(ta->duration_sec);
    return NULL;
}

CpuMultiResult bench_cpu_multi(const BenchConfig *cfg, int num_cores,
                               const CpuSingleResult *single) {
    int d = sub_dur(cfg);

    pthread_t *threads = malloc((size_t)num_cores * sizeof(pthread_t));
    ThreadArg *args    = malloc((size_t)num_cores * sizeof(ThreadArg));

    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    for (int i = 0; i < num_cores; i++) {
        args[i].duration_sec = d;
        args[i].dep_iters = args[i].thr_iters = args[i].mem_iters = 0;
        pthread_create(&threads[i], NULL, thread_fn, &args[i]);
    }

    uint64_t dep_total = 0, thr_total = 0, mem_total = 0;
    for (int i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
        dep_total += args[i].dep_iters;
        thr_total += args[i].thr_iters;
        mem_total += args[i].mem_iters;
    }
    double dur = elapsed_sec(&ts_start);
    free(threads);
    free(args);

    /* Each sub-benchmark runs serially inside each thread, so wall-clock
       dur ≈ 3 * sub_dur(). Compute per-sub IPS using dur/3. */
    double sub_wall = dur / 3.0;

    CpuMultiResult r;
    r.dep_chain_ips        = (double)dep_total / sub_wall;
    r.indep_throughput_ips = (double)thr_total / sub_wall;
    r.memory_bound_ips     = (double)mem_total / sub_wall;
    r.duration_sec         = sub_wall;
    r.threads_used         = num_cores;

    r.dep_scaling = (single->dep_chain_ips > 0)
                    ? r.dep_chain_ips / single->dep_chain_ips : 0.0;
    r.thr_scaling = (single->indep_throughput_ips > 0)
                    ? r.indep_throughput_ips / single->indep_throughput_ips : 0.0;
    r.mem_scaling = (single->memory_bound_ips > 0)
                    ? r.memory_bound_ips / single->memory_bound_ips : 0.0;
    r.thr_efficiency_percent = (num_cores > 0)
                               ? r.thr_scaling / num_cores * 100.0 : 0.0;
    return r;
}
