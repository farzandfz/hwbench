#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <time.h>

#include "benchmark.h"
#include "modules.h"

/*
 * Register-only FP workload.
 * 5 FP ops per iteration: mul, add, mul, sub, sqrt.
 * We use a volatile barrier after the inner batch to prevent DCE.
 */

#define FLOPS_PER_ITER 5

FloatResult bench_floating_point(const BenchConfig *cfg) {
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    double a = 1.1, b = 2.2, c = 3.3, d = 4.4;
    uint64_t iters = 0;
    double target = (double)cfg->duration_sec;
    double elapsed = 0.0;

    while (elapsed < target) {
        for (int i = 0; i < 100000; i++) {
            a = a * b + c;        /* FMA-like: mul + add */
            b = b * c - d;        /* mul + sub */
            c = c / (a + 1.0);   /* div */
            d = sqrt(d + a);      /* sqrt */
        }
        iters += 100000;
        COMPILER_BARRIER(a);
        COMPILER_BARRIER(d);
        elapsed = elapsed_sec(&ts_start);
    }
    double dur = elapsed_sec(&ts_start);

    FloatResult r;
    r.iterations_per_sec    = (double)iters / dur;
    r.flop_estimate_per_sec = r.iterations_per_sec * FLOPS_PER_ITER;
    r.duration_sec          = dur;
    return r;
}
