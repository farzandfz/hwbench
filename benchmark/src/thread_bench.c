#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "benchmark.h"
#include "modules.h"

/* ---- a) Thread creation latency ---- */
static void *noop_thread(void *arg) { (void)arg; return NULL; }

static double measure_thread_create_latency(void) {
    const int N = 1000;
    pthread_t t;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (int i = 0; i < N; i++) {
        pthread_create(&t, NULL, noop_thread, NULL);
        pthread_join(t, NULL);
    }
    double elapsed = elapsed_sec(&ts);
    return elapsed / N * 1e6; /* microseconds */
}

/* ---- b) Mutex contention ---- */
#define MUTEX_THREADS 4

typedef struct {
    pthread_mutex_t *mutex;
    volatile uint64_t *counter;
    volatile int running;
} MutexArg;

static void *mutex_thread(void *arg) {
    MutexArg *ma = (MutexArg *)arg;
    while (ma->running) {
        pthread_mutex_lock(ma->mutex);
        (*ma->counter)++;
        pthread_mutex_unlock(ma->mutex);
    }
    return NULL;
}

static double measure_mutex_ops(int duration_sec) {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    volatile uint64_t counter = 0;
    pthread_t threads[MUTEX_THREADS];
    MutexArg  args[MUTEX_THREADS];

    for (int i = 0; i < MUTEX_THREADS; i++) {
        args[i].mutex   = &mutex;
        args[i].counter = &counter;
        args[i].running = 1;
        pthread_create(&threads[i], NULL, mutex_thread, &args[i]);
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /* sleep for half duration */
    struct timespec sleep_ts = {duration_sec / 2, 0};
    nanosleep(&sleep_ts, NULL);

    for (int i = 0; i < MUTEX_THREADS; i++) args[i].running = 0;
    for (int i = 0; i < MUTEX_THREADS; i++) pthread_join(threads[i], NULL);

    double t = elapsed_sec(&ts);
    pthread_mutex_destroy(&mutex);
    return (double)counter / t;
}

/* ---- c) Condition variable producer/consumer ---- */
typedef struct {
    pthread_mutex_t  mutex;
    pthread_cond_t   cond_prod;
    pthread_cond_t   cond_cons;
    volatile int     msg_ready;
    volatile uint64_t msgs_sent;
    volatile int     running;
} CondArg;

static void *producer_thread(void *arg) {
    CondArg *ca = (CondArg *)arg;
    while (ca->running) {
        pthread_mutex_lock(&ca->mutex);
        while (ca->msg_ready && ca->running)
            pthread_cond_wait(&ca->cond_prod, &ca->mutex);
        if (ca->running) {
            ca->msg_ready = 1;
            ca->msgs_sent++;
            pthread_cond_signal(&ca->cond_cons);
        }
        pthread_mutex_unlock(&ca->mutex);
    }
    return NULL;
}

static void *consumer_thread(void *arg) {
    CondArg *ca = (CondArg *)arg;
    while (ca->running) {
        pthread_mutex_lock(&ca->mutex);
        while (!ca->msg_ready && ca->running)
            pthread_cond_wait(&ca->cond_cons, &ca->mutex);
        ca->msg_ready = 0;
        pthread_cond_signal(&ca->cond_prod);
        pthread_mutex_unlock(&ca->mutex);
    }
    return NULL;
}

static double measure_condvar_msgs(int duration_sec) {
    CondArg ca;
    memset(&ca, 0, sizeof(ca));
    pthread_mutex_init(&ca.mutex, NULL);
    pthread_cond_init(&ca.cond_prod, NULL);
    pthread_cond_init(&ca.cond_cons, NULL);
    ca.running = 1;

    pthread_t prod, cons;
    pthread_create(&prod, NULL, producer_thread, &ca);
    pthread_create(&cons, NULL, consumer_thread, &ca);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    struct timespec sleep_ts = {duration_sec / 2, 0};
    nanosleep(&sleep_ts, NULL);

    pthread_mutex_lock(&ca.mutex);
    ca.running = 0;
    pthread_cond_broadcast(&ca.cond_prod);
    pthread_cond_broadcast(&ca.cond_cons);
    pthread_mutex_unlock(&ca.mutex);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    double t = elapsed_sec(&ts);
    uint64_t msgs = ca.msgs_sent;
    pthread_mutex_destroy(&ca.mutex);
    pthread_cond_destroy(&ca.cond_prod);
    pthread_cond_destroy(&ca.cond_cons);
    return (double)msgs / t;
}

ThreadBenchResult bench_threads(const BenchConfig *cfg) {
    ThreadBenchResult r;
    r.thread_create_latency_us = measure_thread_create_latency();
    r.mutex_ops_per_sec        = measure_mutex_ops(cfg->duration_sec);
    r.condvar_msgs_per_sec     = measure_condvar_msgs(cfg->duration_sec);
    return r;
}
