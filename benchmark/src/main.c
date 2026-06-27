#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "benchmark.h"
#include "system_info.h"
#include "modules.h"

/* ---- forward declarations for writers ---- */
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
                const PythonResult      *py);

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
               const PythonResult      *py);

/* ---- globals for signal handler ---- */
static volatile sig_atomic_t g_interrupted = 0;
static char g_json_path[512] = "";
static char g_csv_path[512]  = "";

/* partial results — filled in as modules complete */
static BenchConfig        g_cfg;
static SystemInfo         g_sys;
static CpuSingleResult    g_cs;
static CpuMultiResult     g_cm;
static MemoryResult       g_mem;
static StorageResult      g_stor;
static CompressionResult  g_comp;
static HashingResult      g_hash;
static FloatResult        g_fp;
static ThreadBenchResult  g_thr;
static FsMetaResult       g_fsmeta;
static PythonResult       g_py;

static int g_modules_done = 0;

static void sigint_handler(int sig) {
    (void)sig;
    g_interrupted = 1;
    if (g_modules_done > 0 && g_json_path[0]) {
        write_json(g_json_path, &g_cfg, &g_sys,
                   &g_cs, &g_cm, &g_mem, &g_stor, &g_comp,
                   &g_hash, &g_fp, &g_thr, &g_fsmeta, &g_py);
        fprintf(stderr, "\nInterrupted — partial results saved to %s\n", g_json_path);
    }
    exit(130);
}

/* ---- spinner thread ---- */
typedef struct {
    const char *label;
    volatile bool done;
    int duration_sec;
} SpinnerArg;

static void *spinner_fn(void *arg) {
    SpinnerArg *sa = (SpinnerArg *)arg;
    const char *frames = "|/-\\";
    int f = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    while (!sa->done) {
        double elapsed = elapsed_sec(&ts);
        if (sa->duration_sec > 0 && elapsed <= sa->duration_sec * 1.05)
            printf("\r  %c  %.1fs / %ds ", frames[f++ & 3], elapsed,
                   sa->duration_sec);
        else
            printf("\r  %c  %.1fs         ", frames[f++ & 3], elapsed);
        fflush(stdout);
        struct timespec sl = {0, 500000000L}; /* 500 ms */
        nanosleep(&sl, NULL);
    }
    printf("\r                    \r");
    fflush(stdout);
    return NULL;
}

/* ---- utility ---- */
void fmt_commas(char *buf, size_t bufsz, uint64_t val) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)val);
    size_t len = strlen(tmp);
    size_t out = 0;
    for (size_t i = 0; i < len && out + 1 < bufsz; i++) {
        buf[out++] = tmp[i];
        size_t rem = len - i - 1;
        if (rem > 0 && rem % 3 == 0 && out + 1 < bufsz)
            buf[out++] = ',';
    }
    buf[out] = '\0';
}

static void print_separator(bool no_color) {
    const char *cy = no_color ? "" : COL_CYAN;
    const char *rs = no_color ? "" : COL_RESET;
    printf("%s────────────────────────────────────────────────%s\n", cy, rs);
}

static void print_header(bool no_color) {
    const char *bl = no_color ? "" : COL_BOLD;
    const char *cy = no_color ? "" : COL_CYAN;
    const char *rs = no_color ? "" : COL_RESET;
    printf("\n");
    printf("%s%s┌─────────────────────────────────────────────┐%s\n", bl, cy, rs);
    printf("%s%s│  hwbench v%-5s — Hardware Benchmark Suite  │%s\n", bl, cy, HWBENCH_VERSION, rs);
    printf("%s%s└─────────────────────────────────────────────┘%s\n", bl, cy, rs);
}

static void print_module_header(int idx, int total, const char *name,
                                int dur, bool no_color) {
    const char *bl  = no_color ? "" : COL_BOLD;
    const char *gr  = no_color ? "" : COL_GREEN;
    const char *rs  = no_color ? "" : COL_RESET;
    printf("\n%s[%d/%d]%s %s%s%s  (~%ds)\n", gr, idx, total, rs, bl, name, rs, dur);
}

static void print_kv(const char *key, const char *val, bool no_color) {
    const char *cy = no_color ? "" : COL_CYAN;
    const char *rs = no_color ? "" : COL_RESET;
    printf("  %-30s %s%s%s\n", key, cy, val, rs);
}

static void print_kv_dbl(const char *key, double val, const char *unit,
                         bool no_color) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.3f %s", val, unit);
    print_kv(key, buf, no_color);
}

/* ---- module-specific print helpers ---- */
static void print_cpu_ips_row(const char *label, double ips, const char *note,
                              bool nc) {
    char tmp[64];
    const char *cy = nc ? "" : COL_CYAN;
    const char *rs = nc ? "" : COL_RESET;
    fmt_commas(tmp, sizeof(tmp), (uint64_t)ips);
    printf("  %-20s: %s%18s%s iter/s  (%s)\n", label, cy, tmp, rs, note);
}

static void print_cpu_single(const CpuSingleResult *r, bool nc) {
    print_cpu_ips_row("Dependent chain",   r->dep_chain_ips,
                      "latency-bound, critical path", nc);
    print_cpu_ips_row("Indep. throughput", r->indep_throughput_ips,
                      "throughput-bound, OOO width", nc);
    print_cpu_ips_row("Memory bound",      r->memory_bound_ips,
                      "L2-pressure, realistic", nc);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%.3f s", r->duration_sec);
    print_kv("Duration (per sub-test)", tmp, nc);
}

static void print_cpu_multi(const CpuMultiResult *r, bool nc) {
    print_cpu_ips_row("Dependent chain",   r->dep_chain_ips,
                      "latency-bound, critical path", nc);
    print_cpu_ips_row("Indep. throughput", r->indep_throughput_ips,
                      "throughput-bound, OOO width", nc);
    print_cpu_ips_row("Memory bound",      r->memory_bound_ips,
                      "L2-pressure, realistic", nc);
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%d", r->threads_used);
    print_kv("Threads",                tmp, nc);
    snprintf(tmp, sizeof(tmp), "%.2fx / %.2fx / %.2fx  (dep/thr/mem)",
             r->dep_scaling, r->thr_scaling, r->mem_scaling);
    print_kv("Scaling",                tmp, nc);
    snprintf(tmp, sizeof(tmp), "%.1f%%", r->thr_efficiency_percent);
    print_kv("Throughput efficiency",  tmp, nc);
}

static void print_memory(const MemoryResult *r, bool nc) {
    print_kv_dbl("Sequential write", r->sequential_write_gbps, "GB/s", nc);
    print_kv_dbl("Sequential read",  r->sequential_read_gbps,  "GB/s", nc);
    print_kv_dbl("memcpy",           r->memcpy_gbps,           "GB/s", nc);
    print_kv_dbl("Pointer-chase latency", r->latency_ns,       "ns",   nc);
}

static void print_storage(const StorageResult *r, bool nc) {
    if (r->skipped) { print_kv("Status", "skipped", nc); return; }
    print_kv_dbl("Sequential write",  r->sequential_write_mbps,  "MB/s", nc);
    print_kv_dbl("Sequential read",   r->sequential_read_mbps,   "MB/s", nc);
    print_kv_dbl("Random 4K write",   r->random_4k_write_iops,   "IOPS", nc);
    print_kv_dbl("Random 4K read",    r->random_4k_read_iops,    "IOPS", nc);
    print_kv_dbl("File creation",     r->file_create_per_sec,    "/s",   nc);
    if (r->is_tmpfs) print_kv("NOTE", "tmpfs detected (RAM-backed)", nc);
}

static void print_compression(const CompressionResult *r, bool nc) {
    if (r->skipped) { print_kv("Status", "skipped", nc); return; }
    print_kv_dbl("Compress",   r->compress_mbps,    "MB/s", nc);
    print_kv_dbl("Decompress", r->decompress_mbps,  "MB/s", nc);
    char tmp[32]; snprintf(tmp, sizeof(tmp), "%.3fx", r->compression_ratio);
    print_kv("Compression ratio",   tmp, nc);
}

static void print_hashing(const HashingResult *r, bool nc) {
    print_kv_dbl("SHA-256 hashes/sec",  r->hashes_per_sec, "",     nc);
    print_kv_dbl("Throughput",          r->throughput_mbps,"MB/s", nc);
}

static void print_float(const FloatResult *r, bool nc) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%.3f GFLOPs", r->flop_estimate_per_sec / 1e9);
    print_kv("FLOP estimate",        tmp, nc);
    fmt_commas(tmp, sizeof(tmp), (uint64_t)r->iterations_per_sec);
    print_kv("Iterations/sec",       tmp, nc);
}

static void print_threads(const ThreadBenchResult *r, bool nc) {
    print_kv_dbl("Thread create latency", r->thread_create_latency_us, "µs",  nc);
    print_kv_dbl("Mutex ops/sec",         r->mutex_ops_per_sec,         "",    nc);
    print_kv_dbl("Condvar msgs/sec",      r->condvar_msgs_per_sec,      "",    nc);
}

static void print_fsmeta(const FsMetaResult *r, bool nc) {
    print_kv_dbl("create/sec", r->create_per_sec, "",  nc);
    print_kv_dbl("stat/sec",   r->stat_per_sec,   "",  nc);
    print_kv_dbl("delete/sec", r->delete_per_sec, "",  nc);
}

static void print_python(const PythonResult *r, bool nc) {
    if (r->skipped) { print_kv("Status", "skipped (python3 not found)", nc); return; }
    print_kv("Python version",      r->python_version, nc);
    print_kv_dbl("Startup time",    r->startup_time_ms, "ms", nc);
    char tmp[64];
    fmt_commas(tmp, sizeof(tmp), (uint64_t)r->single_core_ips);
    print_kv("Single-core iters/s", tmp, nc);
    fmt_commas(tmp, sizeof(tmp), (uint64_t)r->multi_core_ips);
    print_kv("Multi-core iters/s",  tmp, nc);
    snprintf(tmp, sizeof(tmp), "%.2fx", r->multi_core_scaling);
    print_kv("Scaling factor",       tmp, nc);
}

/* ---- build output filename ---- */
static void make_json_path(char *buf, size_t bufsz) {
    time_t now = time(NULL);
    struct tm *tm_l = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H-%M-%S", tm_l);

    /* try to write into results/ relative to binary location */
    const char *results_dir = "results";
    struct stat st;
    if (stat(results_dir, &st) != 0) mkdir(results_dir, 0755);
    snprintf(buf, bufsz, "%s/benchmark_results_%s.json", results_dir, ts);
}

/* ---- run with spinner ---- */
#define RUN_MODULE(idx, total, label, dur, nc, body, print_fn, result_ptr) \
    do { \
        print_module_header(idx, total, label, dur, nc); \
        SpinnerArg sa = {label, false, dur}; \
        pthread_t spin_tid; \
        pthread_create(&spin_tid, NULL, spinner_fn, &sa); \
        *(result_ptr) = body; \
        sa.done = true; \
        pthread_join(spin_tid, NULL); \
        print_fn(result_ptr, nc); \
        g_modules_done++; \
    } while (0)

/* ---- usage ---- */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <device_name> [options]\n"
        "Options:\n"
        "  --duration N        Seconds per module (default %d)\n"
        "  --cpu-duration N    Seconds for CPU single/multi modules (default 30)\n"
        "  --storage-path PATH Override storage benchmark path (default /tmp)\n"
        "  --csv               Also write CSV row\n"
        "  --skip MODULE       Skip module (cpu_single,cpu_multi,memory,storage,\n"
        "                        compression,hashing,float,threads,fsmeta,python)\n"
        "  --no-color          Disable ANSI colors\n"
        "  --help              Show this message\n",
        prog, DEFAULT_DURATION_SEC);
}

int main(int argc, char **argv) {
    /* ---- defaults ---- */
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.duration_sec     = DEFAULT_DURATION_SEC;
    g_cfg.cpu_duration_sec = 30;
    g_cfg.storage_path   = "/tmp";
    g_cfg.csv_output     = false;
    g_cfg.no_color       = false;

    bool skip[10] = {false}; /* indexed by module number 0-9 */
    /* 0=cpu_single 1=cpu_multi 2=memory 3=storage 4=compression
       5=hashing 6=float 7=threads 8=fsmeta 9=python */

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *device_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]); return 0;
        } else if (strcmp(argv[i], "--duration") == 0 && i+1 < argc) {
            g_cfg.duration_sec = atoi(argv[++i]);
            if (g_cfg.duration_sec < 1) g_cfg.duration_sec = 1;
        } else if (strcmp(argv[i], "--cpu-duration") == 0 && i+1 < argc) {
            g_cfg.cpu_duration_sec = atoi(argv[++i]);
            if (g_cfg.cpu_duration_sec < 1) g_cfg.cpu_duration_sec = 1;
        } else if (strcmp(argv[i], "--storage-path") == 0 && i+1 < argc) {
            g_cfg.storage_path = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0) {
            g_cfg.csv_output = true;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            g_cfg.no_color = true;
        } else if (strcmp(argv[i], "--skip") == 0 && i+1 < argc) {
            const char *mod = argv[++i];
            if (strcmp(mod, "cpu_single")   == 0) skip[0] = true;
            else if (strcmp(mod, "cpu_multi")    == 0) skip[1] = true;
            else if (strcmp(mod, "memory")       == 0) skip[2] = true;
            else if (strcmp(mod, "storage")      == 0) skip[3] = true;
            else if (strcmp(mod, "compression")  == 0) skip[4] = true;
            else if (strcmp(mod, "hashing")      == 0) skip[5] = true;
            else if (strcmp(mod, "float")        == 0) skip[6] = true;
            else if (strcmp(mod, "threads")      == 0) skip[7] = true;
            else if (strcmp(mod, "fsmeta")       == 0) skip[8] = true;
            else if (strcmp(mod, "python")       == 0) skip[9] = true;
            else fprintf(stderr, "Unknown module: %s\n", mod);
        } else if (argv[i][0] != '-') {
            device_name = argv[i];
        }
    }

    if (!device_name) {
        fprintf(stderr, "Error: device_name required as first argument\n");
        usage(argv[0]);
        return 1;
    }
    g_cfg.device_name = device_name;

    /* ---- signal handler ---- */
    struct sigaction sa_int = {0};
    sa_int.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa_int, NULL);

    /* ---- collect system info ---- */
    collect_system_info(&g_sys, device_name, g_cfg.storage_path);

    /* ---- build output path ---- */
    make_json_path(g_json_path, sizeof(g_json_path));
    snprintf(g_csv_path, sizeof(g_csv_path), "results/benchmark_results.csv");

    /* ensure results dir */
    {
        struct stat st;
        if (stat("results", &st) != 0) mkdir("results", 0755);
    }

    /* ---- header ---- */
    bool nc = g_cfg.no_color;
    print_header(nc);
    print_system_info(&g_sys, nc);
    print_separator(nc);

    int total = 10;
    int idx   = 1;

    /* ---- module 1: CPU single ---- */
    if (!skip[0]) {
        print_module_header(idx, total, "CPU Single-Core", g_cfg.cpu_duration_sec, nc);
        SpinnerArg sarg = {"CPU Single", false, g_cfg.cpu_duration_sec};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_cs = bench_cpu_single(&g_cfg);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_cpu_single(&g_cs, nc);
        g_modules_done++;
    }
    idx++;

    /* ---- module 2: CPU multi ---- */
    if (!skip[1]) {
        print_module_header(idx, total, "CPU Multi-Core", g_cfg.cpu_duration_sec, nc);
        SpinnerArg sarg = {"CPU Multi", false, g_cfg.cpu_duration_sec};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_cm = bench_cpu_multi(&g_cfg, g_sys.cpu_logical_cores, &g_cs);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_cpu_multi(&g_cm, nc);
        g_modules_done++;
    }
    idx++;

    /* ---- module 3: Memory ---- */
    if (!skip[2]) {
        print_module_header(idx, total, "Memory", 0, nc);
        SpinnerArg sarg = {"Memory", false, 5};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_mem = bench_memory(&g_cfg, g_sys.ram_total_mb);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_memory(&g_mem, nc);
        g_modules_done++;
    }
    idx++;

    /* ---- module 4: Storage ---- */
    if (!skip[3]) {
        int stor_dur = g_cfg.duration_sec * 4; /* storage takes longer */
        print_module_header(idx, total, "Storage", stor_dur, nc);
        SpinnerArg sarg = {"Storage", false, stor_dur};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_stor = bench_storage(&g_cfg);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_storage(&g_stor, nc);
        g_modules_done++;
    } else {
        g_stor.skipped = true;
    }
    idx++;

    /* ---- module 5: Compression ---- */
    if (!skip[4]) {
        print_module_header(idx, total, "Compression (LZ77)", g_cfg.duration_sec * 2, nc);
        SpinnerArg sarg = {"Compression", false, g_cfg.duration_sec * 2};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_comp = bench_compression(&g_cfg);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_compression(&g_comp, nc);
        g_modules_done++;
    } else {
        g_comp.skipped = true;
    }
    idx++;

    /* ---- module 6: Hashing ---- */
    if (!skip[5]) {
        print_module_header(idx, total, "SHA-256 Hashing", g_cfg.duration_sec, nc);
        SpinnerArg sarg = {"Hashing", false, g_cfg.duration_sec};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_hash = bench_hashing(&g_cfg);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_hashing(&g_hash, nc);
        g_modules_done++;
    }
    idx++;

    /* ---- module 7: Floating point ---- */
    if (!skip[6]) {
        print_module_header(idx, total, "Floating Point", g_cfg.duration_sec, nc);
        SpinnerArg sarg = {"Float", false, g_cfg.duration_sec};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_fp = bench_floating_point(&g_cfg);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_float(&g_fp, nc);
        g_modules_done++;
    }
    idx++;

    /* ---- module 8: Thread benchmark ---- */
    if (!skip[7]) {
        int thr_dur = g_cfg.duration_sec + 2;
        print_module_header(idx, total, "Thread / Sync Primitives", thr_dur, nc);
        SpinnerArg sarg = {"Threads", false, thr_dur};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_thr = bench_threads(&g_cfg);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_threads(&g_thr, nc);
        g_modules_done++;
    }
    idx++;

    /* ---- module 9: FS metadata ---- */
    if (!skip[8]) {
        print_module_header(idx, total, "Filesystem Metadata", 0, nc);
        SpinnerArg sarg = {"FS Meta", false, 5};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_fsmeta = bench_fs_metadata(&g_cfg);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_fsmeta(&g_fsmeta, nc);
        g_modules_done++;
    }
    idx++;

    /* ---- module 10: Python ---- */
    if (!skip[9]) {
        int py_dur = g_cfg.duration_sec * 2 + 5;
        print_module_header(idx, total, "Python Benchmark", py_dur, nc);
        SpinnerArg sarg = {"Python", false, py_dur};
        pthread_t stid;
        pthread_create(&stid, NULL, spinner_fn, &sarg);
        g_py = bench_python(&g_cfg, g_sys.cpu_logical_cores);
        sarg.done = true;
        pthread_join(stid, NULL);
        print_python(&g_py, nc);
        g_modules_done++;
    } else {
        g_py.skipped = true;
    }
    idx++;
    (void)idx;

    /* ---- write outputs ---- */
    print_separator(nc);

    write_json(g_json_path, &g_cfg, &g_sys,
               &g_cs, &g_cm, &g_mem, &g_stor, &g_comp,
               &g_hash, &g_fp, &g_thr, &g_fsmeta, &g_py);

    const char *gr = nc ? "" : COL_GREEN;
    const char *rs = nc ? "" : COL_RESET;
    printf("\n%sResults saved:%s %s\n", gr, rs, g_json_path);

    if (g_cfg.csv_output) {
        write_csv(g_csv_path, &g_sys,
                  &g_cs, &g_cm, &g_mem, &g_stor, &g_comp,
                  &g_hash, &g_fp, &g_thr, &g_fsmeta, &g_py);
        printf("%sCSV row appended:%s %s\n", gr, rs, g_csv_path);
    }

    printf("\n");
    return 0;
}
