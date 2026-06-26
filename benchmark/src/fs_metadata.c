#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "benchmark.h"
#include "modules.h"

#define FILE_COUNT 10000

FsMetaResult bench_fs_metadata(const BenchConfig *cfg) {
    FsMetaResult r = {0};

    char tmpdir[600];
    snprintf(tmpdir, sizeof(tmpdir), "%s/hwbench_fsmeta_%d",
             cfg->storage_path, (int)getpid());

    if (mkdir(tmpdir, 0700) != 0) {
        fprintf(stderr, "fs_metadata: cannot create %s: %s\n",
                tmpdir, strerror(errno));
        return r;
    }

    char *paths = malloc((size_t)FILE_COUNT * 512);
    if (!paths) { rmdir(tmpdir); return r; }

    /* Pre-build paths */
    for (int i = 0; i < FILE_COUNT; i++)
        snprintf(paths + (size_t)i * 512, 512, "%s/%05d", tmpdir, i);

    struct timespec ts;
    struct stat st;

    /* ---- Create ---- */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (int i = 0; i < FILE_COUNT; i++) {
        int fd = open(paths + (size_t)i * 512, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) close(fd);
    }
    double t_create = elapsed_sec(&ts);
    r.create_per_sec = FILE_COUNT / t_create;

    /* ---- Stat ---- */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (int i = 0; i < FILE_COUNT; i++)
        stat(paths + (size_t)i * 512, &st);
    double t_stat = elapsed_sec(&ts);
    r.stat_per_sec = FILE_COUNT / t_stat;
    COMPILER_BARRIER(st.st_size);

    /* ---- Delete ---- */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (int i = 0; i < FILE_COUNT; i++)
        unlink(paths + (size_t)i * 512);
    double t_del = elapsed_sec(&ts);
    r.delete_per_sec = FILE_COUNT / t_del;

    rmdir(tmpdir);
    free(paths);
    return r;
}
