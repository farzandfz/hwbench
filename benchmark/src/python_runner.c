#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "benchmark.h"
#include "modules.h"

/* Python script written to /tmp/hwbench_pyworker.py */
static const char *PY_SCRIPT =
"import time, sys, os, multiprocessing\n"
"\n"
"DURATION = %d\n"
"\n"
"def cpu_work(duration):\n"
"    a = 0xdeadbeefcafe\n"
"    b = 0x123456789abc\n"
"    c = 0xfedcba98765432\n"
"    d = 0x0102030405060708\n"
"    iters = 0\n"
"    end = time.monotonic() + duration\n"
"    while time.monotonic() < end:\n"
"        for _ in range(100000):\n"
"            a ^= (b << 13) & 0xFFFFFFFFFFFFFFFF\n"
"            b ^= (c >> 7) & 0xFFFFFFFFFFFFFFFF\n"
"            c ^= (d << 17) & 0xFFFFFFFFFFFFFFFF\n"
"            d ^= a & 0xFFFFFFFFFFFFFFFF\n"
"            a = (a * 6364136223846793005) & 0xFFFFFFFFFFFFFFFF\n"
"            b = (b + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF\n"
"            c ^= (c >> 33) & 0xFFFFFFFFFFFFFFFF\n"
"            d = (d * 2685821657736338717) & 0xFFFFFFFFFFFFFFFF\n"
"        iters += 100000\n"
"    return iters\n"
"\n"
"def worker(_):\n"
"    return cpu_work(DURATION)\n"
"\n"
"if __name__ == '__main__':\n"
"    import sys\n"
"    version = f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}'\n"
"    print(f'python_version={version}')\n"
"\n"
"    # single-core\n"
"    t0 = time.monotonic()\n"
"    sc_iters = cpu_work(DURATION)\n"
"    sc_time = time.monotonic() - t0\n"
"    print(f'single_core_iters={sc_iters}')\n"
"    print(f'single_core_time={sc_time:.4f}')\n"
"\n"
"    # multi-core via multiprocessing\n"
"    ncpu = %d\n"
"    t1 = time.monotonic()\n"
"    with multiprocessing.Pool(ncpu) as pool:\n"
"        results = pool.map(worker, range(ncpu))\n"
"    mc_time = time.monotonic() - t1\n"
"    mc_iters = sum(results)\n"
"    print(f'multi_core_iters={mc_iters}')\n"
"    print(f'multi_core_time={mc_time:.4f}')\n"
"    print(f'num_cores={ncpu}')\n";

PythonResult bench_python(const BenchConfig *cfg, int num_cores) {
    PythonResult r;
    memset(&r, 0, sizeof(r));

    /* Check if python3 exists */
    FILE *fp = popen("python3 --version 2>&1", "r");
    if (!fp) { r.skipped = true; return r; }
    char vbuf[128] = "";
    if (!fgets(vbuf, sizeof(vbuf), fp)) {
        pclose(fp);
        r.skipped = true;
        return r;
    }
    pclose(fp);

    /* Must start with "Python" */
    if (strncmp(vbuf, "Python", 6) != 0) {
        r.skipped = true;
        return r;
    }

    /* Write the Python worker script */
    const char *script_path = "/tmp/hwbench_pyworker.py";
    FILE *sf = fopen(script_path, "w");
    if (!sf) { r.skipped = true; return r; }
    fprintf(sf, PY_SCRIPT, cfg->duration_sec, num_cores);
    fclose(sf);

    /* Measure startup time */
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    FILE *sp = popen("python3 -c 'pass' 2>/dev/null", "r");
    if (sp) { pclose(sp); }
    r.startup_time_ms = elapsed_sec(&ts_start) * 1000.0;

    /* Run the actual benchmark */
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "python3 %s 2>/dev/null", script_path);
    FILE *op = popen(cmd, "r");
    if (!op) { r.skipped = true; return r; }

    char line[256];
    double sc_iters = 0, sc_time = 0, mc_iters = 0, mc_time = 0;
    int nc = num_cores;
    while (fgets(line, sizeof(line), op)) {
        char key[64]; double val = 0; char sval[64];
        if (sscanf(line, "%63[^=]=%63s", key, sval) == 2) {
            val = atof(sval);
            if (strcmp(key, "python_version") == 0)
                snprintf(r.python_version, sizeof(r.python_version), "%s", sval);
            else if (strcmp(key, "single_core_iters") == 0) sc_iters = val;
            else if (strcmp(key, "single_core_time")  == 0) sc_time  = val;
            else if (strcmp(key, "multi_core_iters")  == 0) mc_iters = val;
            else if (strcmp(key, "multi_core_time")   == 0) mc_time  = val;
            else if (strcmp(key, "num_cores")         == 0) nc       = (int)val;
        }
    }
    pclose(op);
    unlink(script_path);

    r.single_core_ips    = (sc_time > 0) ? sc_iters / sc_time : 0;
    r.multi_core_ips     = (mc_time > 0) ? mc_iters / mc_time : 0;
    r.multi_core_scaling = (r.single_core_ips > 0)
                           ? r.multi_core_ips / r.single_core_ips : 0;
    (void)nc;
    return r;
}
