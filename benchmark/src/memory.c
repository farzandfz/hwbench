#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "benchmark.h"
#include "modules.h"

#define BUF_LARGE_MB    256
#define BUF_SMALL_MB    128
#define LATENCY_BUF_MB   64
#define NODE_COUNT      (LATENCY_BUF_MB * 1024 * 1024 / sizeof(void*))

/* Fisher-Yates shuffle of index array */
static void shuffle(size_t *arr, size_t n) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)rand() % (i + 1);
        size_t tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

MemoryResult bench_memory(const BenchConfig *cfg, long ram_mb) {
    (void)cfg;
    MemoryResult r = {0};

    long buf_mb = (ram_mb < 512) ? BUF_SMALL_MB : BUF_LARGE_MB;
    size_t buf_bytes = (size_t)buf_mb * 1024 * 1024;
    r.buffer_size_mb = buf_mb;

    uint8_t *src = malloc(buf_bytes);
    uint8_t *dst = malloc(buf_bytes);
    if (!src || !dst) {
        free(src); free(dst);
        fprintf(stderr, "memory: cannot allocate %ld MB buffers\n", buf_mb);
        return r;
    }

    struct timespec ts;

    /* ---- Sequential write (memset) ---- */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    memset(dst, 0xAB, buf_bytes);
    double t = elapsed_sec(&ts);
    r.sequential_write_gbps = (double)buf_bytes / t / 1e9;

    /* ---- Sequential read (volatile sum) ---- */
    volatile uint64_t sum = 0;
    uint64_t *p64 = (uint64_t *)dst;
    size_t n64 = buf_bytes / sizeof(uint64_t);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (size_t i = 0; i < n64; i++) sum += p64[i];
    t = elapsed_sec(&ts);
    r.sequential_read_gbps = (double)buf_bytes / t / 1e9;
    /* Use sum to prevent elimination */
    COMPILER_BARRIER(sum);

    /* ---- memcpy throughput ---- */
    memset(src, 0xCD, buf_bytes);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    memcpy(dst, src, buf_bytes);
    t = elapsed_sec(&ts);
    r.memcpy_gbps = (double)buf_bytes / t / 1e9;

    /* ---- Pointer-chasing latency ---- */
    /* Build a linked list through NODE_COUNT slots in random order */
    size_t node_count = NODE_COUNT;
    void **nodes = (void **)malloc(node_count * sizeof(void *));
    size_t *idx   = (size_t *)malloc(node_count * sizeof(size_t));
    if (!nodes || !idx) {
        free(nodes); free(idx);
        goto cleanup;
    }
    srand(12345);
    for (size_t i = 0; i < node_count; i++) idx[i] = i;
    shuffle(idx, node_count);
    /* Link: nodes[idx[i]] -> nodes[idx[(i+1) % node_count]] */
    for (size_t i = 0; i < node_count; i++)
        nodes[idx[i]] = &nodes[idx[(i + 1) % node_count]];
    free(idx);

    /* Chase the list for ~2 seconds to measure latency */
    void **cur = nodes;
    uint64_t chases = 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /* run for up to 2 seconds */
    while (elapsed_sec(&ts) < 2.0) {
        for (int k = 0; k < 1024; k++) {
            cur = (void **)*cur;
            cur = (void **)*cur;
            cur = (void **)*cur;
            cur = (void **)*cur;
        }
        chases += 4096;
    }
    t = elapsed_sec(&ts);
    r.latency_ns = (t / (double)chases) * 1e9;
    COMPILER_BARRIER(cur);
    free(nodes);

cleanup:
    free(src);
    free(dst);
    return r;
}
