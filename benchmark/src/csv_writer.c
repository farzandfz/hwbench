#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "benchmark.h"
#include "system_info.h"
#include "modules.h"

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

void write_csv(const char *path,
               const SystemInfo        *sys,
               const CpuSingleResult   *cs,
               const CpuMultiResult    *cm,
               const MemoryResult      *mem,
               const StorageResult     *stor,
               const CompressionResult *comp,
               const HashingResult     *hash,
               const FloatResult       *fp,
               const ThreadBenchResult *thr,
               const FsMetaResult      *fsmeta,
               const PythonResult      *py) {
    bool need_header = !file_exists(path);
    FILE *f = fopen(path, "a");
    if (!f) { fprintf(stderr, "Cannot open CSV %s\n", path); return; }

    if (need_header) {
        fprintf(f,
            "timestamp,device_name,hostname,os,arch,cpu_model,"
            "cpu_physical_cores,cpu_logical_cores,ram_total_mb,"
            /* cpu single */
            "cpu_dep_ips,cpu_thr_ips,cpu_mem_ips,"
            /* cpu multi */
            "cpu_dep_ips_multi,cpu_thr_ips_multi,cpu_mem_ips_multi,"
            "cpu_thr_scaling,cpu_thr_efficiency,"
            /* memory */
            "mem_write_gbps,mem_read_gbps,mem_memcpy_gbps,mem_latency_ns,"
            /* storage */
            "stor_seq_write_mbps,stor_seq_read_mbps,"
            "stor_rand4k_write_iops,stor_rand4k_read_iops,stor_file_create_ps,"
            /* compression */
            "comp_compress_mbps,comp_decompress_mbps,comp_ratio,"
            /* hashing */
            "hash_hashes_ps,hash_mbps,"
            /* float */
            "fp_gflops,fp_ips,"
            /* thread */
            "thr_create_us,thr_mutex_ops_ps,thr_condvar_ps,"
            /* fs_meta */
            "fsmeta_create_ps,fsmeta_stat_ps,fsmeta_delete_ps,"
            /* python */
            "py_version,py_startup_ms,py_sc_ips,py_mc_ips,py_mc_scaling\n");
    }

    /* system + CPU single + CPU multi + memory */
    fprintf(f,
        "%s,%s,%s,%s %s,%s,%s,"
        "%d,%d,%ld,"
        "%.3f,%.3f,%.3f,"
        "%.3f,%.3f,%.3f,%.3f,%.2f,"
        "%.4f,%.4f,%.4f,%.2f,",
        sys->timestamp, sys->device_name, sys->hostname,
        sys->os_name, sys->os_version, sys->architecture, sys->cpu_model,
        sys->cpu_physical_cores, sys->cpu_logical_cores, sys->ram_total_mb,
        /* cpu single: dep, thr, mem (millions/sec) */
        cs->dep_chain_ips / 1e6, cs->indep_throughput_ips / 1e6,
        cs->memory_bound_ips / 1e6,
        /* cpu multi: dep, thr, mem, thr_scaling, thr_efficiency */
        cm->dep_chain_ips / 1e6, cm->indep_throughput_ips / 1e6,
        cm->memory_bound_ips / 1e6, cm->thr_scaling, cm->thr_efficiency_percent,
        mem->sequential_write_gbps, mem->sequential_read_gbps,
        mem->memcpy_gbps, mem->latency_ns);

    /* storage — sequential fields always written; random 4K empty when tmpfs */
    if (stor->skipped) {
        fprintf(f, ",,,,,"  /* seq_write, seq_read, rand4k_write, rand4k_read, file_create */);
    } else {
        fprintf(f, "%.2f,%.2f,", stor->sequential_write_mbps, stor->sequential_read_mbps);
        if (stor->is_tmpfs)
            fprintf(f, ",,"); /* null — tmpfs random 4K reflects RAM, not disk */
        else
            fprintf(f, "%.1f,%.1f,", stor->random_4k_write_iops, stor->random_4k_read_iops);
        fprintf(f, "%.1f,", stor->file_create_per_sec);
    }

    /* compression + hashing + fp + threads + fsmeta + python */
    fprintf(f,
        "%.2f,%.2f,%.4f,"
        "%.3f,%.3f,"
        "%.3f,%.3f,"
        "%.3f,%.0f,%.0f,"
        "%.0f,%.0f,%.0f,"
        "%s,%.2f,%.0f,%.0f,%.3f\n",
        comp->skipped ? 0.0 : comp->compress_mbps,
        comp->skipped ? 0.0 : comp->decompress_mbps,
        comp->skipped ? 0.0 : comp->compression_ratio,
        hash->hashes_per_sec, hash->throughput_mbps,
        fp->flop_estimate_per_sec / 1e9, fp->iterations_per_sec / 1e6,
        thr->thread_create_latency_us,
        thr->mutex_ops_per_sec,
        thr->condvar_msgs_per_sec,
        fsmeta->create_per_sec, fsmeta->stat_per_sec, fsmeta->delete_per_sec,
        py->skipped ? "n/a" : py->python_version,
        py->skipped ? 0.0 : py->startup_time_ms,
        py->skipped ? 0.0 : py->single_core_ips,
        py->skipped ? 0.0 : py->multi_core_ips,
        py->skipped ? 0.0 : py->multi_core_scaling);

    fclose(f);
}
