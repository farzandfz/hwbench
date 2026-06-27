#pragma once
#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define HWBENCH_VERSION "1.0.0"
#define DEFAULT_DURATION_SEC 10

/* Get elapsed seconds since ts_start using CLOCK_MONOTONIC */
static inline double elapsed_sec(struct timespec *ts_start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - ts_start->tv_sec) +
           (double)(now.tv_nsec - ts_start->tv_nsec) * 1e-9;
}

/* Compiler barrier to prevent dead-code elimination */
#define COMPILER_BARRIER(val) __asm__ volatile("" : "+r"(val) :: "memory")

/* Format a large number with commas into buf */
void fmt_commas(char *buf, size_t bufsz, uint64_t val);

/* Benchmark configuration passed to all modules */
typedef struct {
    int duration_sec;
    int cpu_duration_sec;   /* separate duration for CPU modules; defaults to 30 */
    const char *storage_path;
    bool csv_output;
    bool no_color;
    bool skip_python;
    bool skip_storage;
    bool skip_compression;
    bool upload;
    const char *device_name;
} BenchConfig;

/* ANSI color helpers — only emit codes when no_color == false */
#define COL_RESET   "\033[0m"
#define COL_BOLD    "\033[1m"
#define COL_CYAN    "\033[36m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_RED     "\033[31m"
#define COL_BLUE    "\033[34m"

#endif /* BENCHMARK_H */
