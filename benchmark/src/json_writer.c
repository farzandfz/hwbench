#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "benchmark.h"
#include "system_info.h"
#include "modules.h"

/* ---- simple string buffer ---- */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} SB;

static void sb_init(SB *sb, size_t initial) {
    sb->buf = malloc(initial);
    sb->len = 0;
    sb->cap = initial;
    if (sb->buf) sb->buf[0] = '\0';
}

static void sb_grow(SB *sb, size_t needed) {
    while (sb->len + needed + 1 > sb->cap) {
        sb->cap *= 2;
        sb->buf = realloc(sb->buf, sb->cap);
    }
}

static void sb_append(SB *sb, const char *s) {
    size_t n = strlen(s);
    sb_grow(sb, n);
    memcpy(sb->buf + sb->len, s, n + 1);
    sb->len += n;
}

static void sb_printf(SB *sb, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(tmp)) {
        /* big value: use heap */
        char *big = malloc((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        sb_append(sb, big);
        free(big);
    } else {
        sb_append(sb, tmp);
    }
}

/* ---- JSON helpers ---- */
static void json_str(SB *sb, const char *key, const char *val) {
    sb_printf(sb, "    \"%s\": \"%s\"", key, val ? val : "");
}

static void json_dbl(SB *sb, const char *key, double val) {
    sb_printf(sb, "    \"%s\": %.6g", key, val);
}

static void json_u64(SB *sb, const char *key, uint64_t val) {
    sb_printf(sb, "    \"%s\": %llu", key, (unsigned long long)val);
}

static void json_int(SB *sb, const char *key, int val) {
    sb_printf(sb, "    \"%s\": %d", key, val);
}

static void json_long(SB *sb, const char *key, long val) {
    sb_printf(sb, "    \"%s\": %ld", key, val);
}

static void json_bool(SB *sb, const char *key, bool val) {
    sb_printf(sb, "    \"%s\": %s", key, val ? "true" : "false");
}

#define COMMA(sb) sb_append(sb, ",\n")

/* ---- write JSON file ---- */
void write_json(const char *path,
                const BenchConfig *cfg,
                const SystemInfo  *sys,
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
    SB sb;
    sb_init(&sb, 8192);

    sb_append(&sb, "{\n");

    /* meta */
    sb_append(&sb, "  \"meta\": {\n");
    sb_printf (&sb, "    \"hwbench_version\": \"%s\"", HWBENCH_VERSION); COMMA(&sb);
    json_str  (&sb, "device_name",    sys->device_name); COMMA(&sb);
    json_str  (&sb, "hostname",       sys->hostname);    COMMA(&sb);
    json_str  (&sb, "timestamp",      sys->timestamp);   COMMA(&sb);
    json_int  (&sb, "duration_per_module_sec", cfg->duration_sec);
    sb_append (&sb, "\n  },\n");

    /* system */
    sb_append (&sb, "  \"system\": {\n");
    json_str  (&sb, "os_name",            sys->os_name);       COMMA(&sb);
    json_str  (&sb, "os_version",         sys->os_version);    COMMA(&sb);
    json_str  (&sb, "kernel_version",     sys->kernel_version); COMMA(&sb);
    json_str  (&sb, "architecture",       sys->architecture);  COMMA(&sb);
    json_str  (&sb, "cpu_model",          sys->cpu_model);     COMMA(&sb);
    json_dbl  (&sb, "cpu_freq_mhz",       sys->cpu_freq_mhz);  COMMA(&sb);
    json_int  (&sb, "cpu_physical_cores", sys->cpu_physical_cores); COMMA(&sb);
    json_int  (&sb, "cpu_logical_cores",  sys->cpu_logical_cores);  COMMA(&sb);
    json_long (&sb, "ram_total_mb",       sys->ram_total_mb);  COMMA(&sb);
    json_long (&sb, "ram_available_mb",   sys->ram_available_mb); COMMA(&sb);
    json_str  (&sb, "disk_model",         sys->disk_model);    COMMA(&sb);
    json_str  (&sb, "disk_type",          sys->disk_type);     COMMA(&sb);
    json_str  (&sb, "filesystem_type",    sys->filesystem_type); COMMA(&sb);
    json_str  (&sb, "storage_path_used",  sys->storage_path_used); COMMA(&sb);
    json_bool (&sb, "storage_is_tmpfs",   sys->storage_is_tmpfs);
    sb_append (&sb, "\n  },\n");

    /* results */
    sb_append (&sb, "  \"results\": {\n");

    /* cpu_single */
    sb_append (&sb, "    \"cpu_single\": {\n");
    json_u64  (&sb, "iterations",        cs->iterations); COMMA(&sb);
    json_dbl  (&sb, "duration_sec",      cs->duration_sec); COMMA(&sb);
    json_dbl  (&sb, "iterations_per_sec",cs->iterations_per_sec); COMMA(&sb);
    json_str  (&sb, "note", "Register-only workload. No memory latency.");
    sb_append (&sb, "\n    },\n");

    /* cpu_multi */
    sb_append (&sb, "    \"cpu_multi\": {\n");
    json_u64  (&sb, "total_iterations",   cm->total_iterations); COMMA(&sb);
    json_dbl  (&sb, "duration_sec",       cm->duration_sec); COMMA(&sb);
    json_dbl  (&sb, "iterations_per_sec", cm->iterations_per_sec); COMMA(&sb);
    json_dbl  (&sb, "scaling_factor",     cm->scaling_factor); COMMA(&sb);
    json_dbl  (&sb, "efficiency_percent", cm->efficiency_percent); COMMA(&sb);
    json_int  (&sb, "threads_used",       cm->threads_used);
    sb_append (&sb, "\n    },\n");

    /* memory */
    sb_append (&sb, "    \"memory\": {\n");
    json_dbl  (&sb, "sequential_write_gbps", mem->sequential_write_gbps); COMMA(&sb);
    json_dbl  (&sb, "sequential_read_gbps",  mem->sequential_read_gbps);  COMMA(&sb);
    json_dbl  (&sb, "memcpy_gbps",           mem->memcpy_gbps);           COMMA(&sb);
    json_dbl  (&sb, "latency_ns",            mem->latency_ns);            COMMA(&sb);
    json_long (&sb, "buffer_size_mb",        mem->buffer_size_mb);
    sb_append (&sb, "\n    },\n");

    /* storage */
    sb_append (&sb, "    \"storage\": {\n");
    if (stor->skipped) {
        json_bool(&sb, "skipped", true);
    } else {
        json_dbl  (&sb, "sequential_write_mbps",  stor->sequential_write_mbps); COMMA(&sb);
        json_dbl  (&sb, "sequential_read_mbps",   stor->sequential_read_mbps);  COMMA(&sb);
        json_dbl  (&sb, "random_4k_write_iops",   stor->random_4k_write_iops);  COMMA(&sb);
        json_dbl  (&sb, "random_4k_read_iops",    stor->random_4k_read_iops);   COMMA(&sb);
        json_dbl  (&sb, "file_create_per_sec",    stor->file_create_per_sec);   COMMA(&sb);
        json_str  (&sb, "storage_path",           stor->storage_path);          COMMA(&sb);
        json_bool (&sb, "is_tmpfs",               stor->is_tmpfs);
    }
    sb_append (&sb, "\n    },\n");

    /* compression */
    sb_append (&sb, "    \"compression\": {\n");
    if (comp->skipped) {
        json_bool(&sb, "skipped", true);
    } else {
        json_dbl  (&sb, "compress_mbps",    comp->compress_mbps);    COMMA(&sb);
        json_dbl  (&sb, "decompress_mbps",  comp->decompress_mbps);  COMMA(&sb);
        json_dbl  (&sb, "compression_ratio",comp->compression_ratio); COMMA(&sb);
        json_dbl  (&sb, "input_size_mb",    comp->input_size_mb);
    }
    sb_append (&sb, "\n    },\n");

    /* hashing */
    sb_append (&sb, "    \"hashing\": {\n");
    json_dbl  (&sb, "hashes_per_sec",  hash->hashes_per_sec);  COMMA(&sb);
    json_dbl  (&sb, "throughput_mbps", hash->throughput_mbps);
    sb_append (&sb, "\n    },\n");

    /* floating_point */
    sb_append (&sb, "    \"floating_point\": {\n");
    json_dbl  (&sb, "flop_estimate_per_sec", fp->flop_estimate_per_sec); COMMA(&sb);
    json_dbl  (&sb, "iterations_per_sec",    fp->iterations_per_sec);    COMMA(&sb);
    json_dbl  (&sb, "duration_sec",          fp->duration_sec);
    sb_append (&sb, "\n    },\n");

    /* thread_bench */
    sb_append (&sb, "    \"thread_bench\": {\n");
    json_dbl  (&sb, "thread_create_latency_us", thr->thread_create_latency_us); COMMA(&sb);
    json_dbl  (&sb, "mutex_ops_per_sec",         thr->mutex_ops_per_sec);         COMMA(&sb);
    json_dbl  (&sb, "condvar_msgs_per_sec",       thr->condvar_msgs_per_sec);
    sb_append (&sb, "\n    },\n");

    /* fs_metadata */
    sb_append (&sb, "    \"fs_metadata\": {\n");
    json_dbl  (&sb, "create_per_sec", fsmeta->create_per_sec); COMMA(&sb);
    json_dbl  (&sb, "stat_per_sec",   fsmeta->stat_per_sec);   COMMA(&sb);
    json_dbl  (&sb, "delete_per_sec", fsmeta->delete_per_sec);
    sb_append (&sb, "\n    },\n");

    /* python */
    sb_append (&sb, "    \"python\": {\n");
    if (py->skipped) {
        json_bool(&sb, "skipped", true);
    } else {
        json_str  (&sb, "python_version",         py->python_version);    COMMA(&sb);
        json_dbl  (&sb, "startup_time_ms",         py->startup_time_ms);   COMMA(&sb);
        json_dbl  (&sb, "single_core_iterations_per_sec", py->single_core_ips); COMMA(&sb);
        json_dbl  (&sb, "multi_core_iterations_per_sec",  py->multi_core_ips);  COMMA(&sb);
        json_dbl  (&sb, "multi_core_scaling_factor",      py->multi_core_scaling);
    }
    sb_append (&sb, "\n    }\n");

    sb_append (&sb, "  }\n}\n");

    /* write file */
    FILE *f = fopen(path, "w");
    if (f) {
        fwrite(sb.buf, 1, sb.len, f);
        fclose(f);
    } else {
        fprintf(stderr, "Cannot write JSON to %s\n", path);
    }
    free(sb.buf);
}
