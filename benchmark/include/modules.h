#pragma once
#ifndef MODULES_H
#define MODULES_H

#include <stdint.h>
#include <stdbool.h>
#include "benchmark.h"

/* ---- CPU ---- */
typedef struct {
    uint64_t iterations;
    double   duration_sec;
    double   iterations_per_sec;
} CpuSingleResult;

typedef struct {
    uint64_t total_iterations;
    double   duration_sec;
    double   iterations_per_sec;
    double   scaling_factor;
    double   efficiency_percent;
    int      threads_used;
} CpuMultiResult;

CpuSingleResult bench_cpu_single(const BenchConfig *cfg);
CpuMultiResult  bench_cpu_multi(const BenchConfig *cfg, int num_cores,
                                double single_ips);

/* ---- Memory ---- */
typedef struct {
    double sequential_write_gbps;
    double sequential_read_gbps;
    double memcpy_gbps;
    double latency_ns;
    long   buffer_size_mb;
} MemoryResult;

MemoryResult bench_memory(const BenchConfig *cfg, long ram_mb);

/* ---- Storage ---- */
typedef struct {
    double sequential_write_mbps;
    double sequential_read_mbps;
    double random_4k_write_iops;
    double random_4k_read_iops;
    double file_create_per_sec;
    char   storage_path[512];
    bool   is_tmpfs;
    bool   skipped;
} StorageResult;

StorageResult bench_storage(const BenchConfig *cfg);

/* ---- Compression ---- */
typedef struct {
    double compress_mbps;
    double decompress_mbps;
    double compression_ratio;
    double input_size_mb;
    bool   skipped;
} CompressionResult;

CompressionResult bench_compression(const BenchConfig *cfg);

/* ---- Hashing ---- */
typedef struct {
    double hashes_per_sec;
    double throughput_mbps;
} HashingResult;

HashingResult bench_hashing(const BenchConfig *cfg);

/* ---- Floating point ---- */
typedef struct {
    double flop_estimate_per_sec;
    double iterations_per_sec;
    double duration_sec;
} FloatResult;

FloatResult bench_floating_point(const BenchConfig *cfg);

/* ---- Thread bench ---- */
typedef struct {
    double thread_create_latency_us;
    double mutex_ops_per_sec;
    double condvar_msgs_per_sec;
} ThreadBenchResult;

ThreadBenchResult bench_threads(const BenchConfig *cfg);

/* ---- FS metadata ---- */
typedef struct {
    double create_per_sec;
    double stat_per_sec;
    double delete_per_sec;
} FsMetaResult;

FsMetaResult bench_fs_metadata(const BenchConfig *cfg);

/* ---- Python ---- */
typedef struct {
    char   python_version[64];
    double startup_time_ms;
    double single_core_ips;
    double multi_core_ips;
    double multi_core_scaling;
    bool   skipped;
} PythonResult;

PythonResult bench_python(const BenchConfig *cfg, int num_cores);

#endif /* MODULES_H */
