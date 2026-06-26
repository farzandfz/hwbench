#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef __APPLE__
#  include <sys/mount.h>
#else
#  include <sys/statfs.h>
#endif

#include "benchmark.h"
#include "modules.h"

#define SEQ_FILE_MB       512
#define SEQ_CHUNK_MB        1
#define RAND_FILE_MB       64
#define RAND_BLOCK_BYTES 4096
#define SMALL_FILE_COUNT 1000

static int detect_tmpfs(const char *path) {
#ifdef __linux__
    struct statfs st;
    if (statfs(path, &st) == 0)
        return st.f_type == 0x01021994; /* TMPFS_MAGIC */
#else
    (void)path;
#endif
    return 0;
}

static char *make_path(const char *dir, const char *name,
                        char *buf, size_t bufsz) {
    snprintf(buf, bufsz, "%s/hwbench_%s", dir, name);
    return buf;
}

StorageResult bench_storage(const BenchConfig *cfg) {
    StorageResult r;
    memset(&r, 0, sizeof(r));
    snprintf(r.storage_path, sizeof(r.storage_path), "%s", cfg->storage_path);
    r.is_tmpfs = detect_tmpfs(cfg->storage_path);

    const size_t seq_bytes   = (size_t)SEQ_FILE_MB  * 1024 * 1024;
    const size_t chunk_bytes = (size_t)SEQ_CHUNK_MB * 1024 * 1024;
    const size_t rand_bytes  = (size_t)RAND_FILE_MB  * 1024 * 1024;

    /* ---- allocate aligned write buffer ---- */
    void *wbuf = NULL;
#if defined(__linux__) || defined(__APPLE__)
    if (posix_memalign(&wbuf, 4096, chunk_bytes) != 0)
        wbuf = malloc(chunk_bytes);
#else
    wbuf = malloc(chunk_bytes);
#endif
    if (!wbuf) { r.skipped = true; return r; }
    memset(wbuf, 0xA5, chunk_bytes);

    void *rbuf = NULL;
#if defined(__linux__) || defined(__APPLE__)
    if (posix_memalign(&rbuf, 4096, chunk_bytes) != 0)
        rbuf = malloc(chunk_bytes);
#else
    rbuf = malloc(chunk_bytes);
#endif
    if (!rbuf) { free(wbuf); r.skipped = true; return r; }

    char seq_path[600], rand_path[600];
    make_path(cfg->storage_path, "seq.tmp",  seq_path,  sizeof(seq_path));
    make_path(cfg->storage_path, "rand.tmp", rand_path, sizeof(rand_path));

    struct timespec ts;

    /* ==== Sequential write ==== */
    {
        int fd = open(seq_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) goto cleanup;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        size_t written = 0;
        size_t chunks  = seq_bytes / chunk_bytes;
        for (size_t i = 0; i < chunks; i++) {
            ssize_t n = write(fd, wbuf, chunk_bytes);
            if (n < 0) break;
            written += (size_t)n;
        }
        fsync(fd);
        double t = elapsed_sec(&ts);
        close(fd);
        r.sequential_write_mbps = (double)written / t / (1024.0*1024.0);
    }

    /* ==== Sequential read ==== */
    {
        int fd = open(seq_path, O_RDONLY, 0);
        if (fd < 0) goto cleanup;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        size_t total_read = 0;
        ssize_t n;
        while ((n = read(fd, rbuf, chunk_bytes)) > 0)
            total_read += (size_t)n;
        double t = elapsed_sec(&ts);
        close(fd);
        r.sequential_read_mbps = (double)total_read / t / (1024.0*1024.0);
    }

    /* ==== Random 4K write ==== */
    {
        void *blk4k = NULL;
#if defined(__linux__) || defined(__APPLE__)
        if (posix_memalign(&blk4k, 4096, RAND_BLOCK_BYTES) != 0)
            blk4k = malloc(RAND_BLOCK_BYTES);
#else
        blk4k = malloc(RAND_BLOCK_BYTES);
#endif
        if (!blk4k) goto cleanup;
        memset(blk4k, 0xBB, RAND_BLOCK_BYTES);

        int fd = open(rand_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) { free(blk4k); goto cleanup; }

        /* pre-allocate the file */
        {
            uint8_t zero = 0;
            lseek(fd, (off_t)(rand_bytes - 1), SEEK_SET);
            if (write(fd, &zero, 1) != 1) { /* ignore */ }
            lseek(fd, 0, SEEK_SET);
        }
        fsync(fd);

        size_t max_blk = rand_bytes / RAND_BLOCK_BYTES;
        uint64_t iops  = 0;
        srand(42);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        while (elapsed_sec(&ts) < (double)cfg->duration_sec) {
            size_t blk = (size_t)rand() % max_blk;
            off_t off  = (off_t)(blk * RAND_BLOCK_BYTES);
            lseek(fd, off, SEEK_SET);
            if (write(fd, blk4k, RAND_BLOCK_BYTES) == RAND_BLOCK_BYTES)
                iops++;
        }
        double t = elapsed_sec(&ts);
        fsync(fd);
        close(fd);
        free(blk4k);
        r.random_4k_write_iops = (double)iops / t;
    }

    /* ==== Random 4K read ==== */
    {
        void *blk4k = NULL;
#if defined(__linux__) || defined(__APPLE__)
        if (posix_memalign(&blk4k, 4096, RAND_BLOCK_BYTES) != 0)
            blk4k = malloc(RAND_BLOCK_BYTES);
#else
        blk4k = malloc(RAND_BLOCK_BYTES);
#endif
        if (!blk4k) goto cleanup;

        int fd = open(rand_path, O_RDONLY, 0);
        if (fd < 0) { free(blk4k); goto cleanup; }

        size_t max_blk = rand_bytes / RAND_BLOCK_BYTES;
        uint64_t iops  = 0;
        srand(42);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        while (elapsed_sec(&ts) < (double)cfg->duration_sec) {
            size_t blk = (size_t)rand() % max_blk;
            off_t off  = (off_t)(blk * RAND_BLOCK_BYTES);
            lseek(fd, off, SEEK_SET);
            if (read(fd, blk4k, RAND_BLOCK_BYTES) == RAND_BLOCK_BYTES)
                iops++;
        }
        double t = elapsed_sec(&ts);
        close(fd);
        free(blk4k);
        r.random_4k_read_iops = (double)iops / t;
    }

    /* ==== File creation benchmark ==== */
    {
        char dir_path[600];
        make_path(cfg->storage_path, "filecreate", dir_path, sizeof(dir_path));
        mkdir(dir_path, 0700);

        char fpath[700];
        clock_gettime(CLOCK_MONOTONIC, &ts);
        for (int i = 0; i < SMALL_FILE_COUNT; i++) {
            snprintf(fpath, sizeof(fpath), "%s/%05d", dir_path, i);
            int fd = open(fpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd >= 0) close(fd);
        }
        double t = elapsed_sec(&ts);
        r.file_create_per_sec = SMALL_FILE_COUNT / t;

        /* cleanup */
        for (int i = 0; i < SMALL_FILE_COUNT; i++) {
            snprintf(fpath, sizeof(fpath), "%s/%05d", dir_path, i);
            unlink(fpath);
        }
        rmdir(dir_path);
    }

cleanup:
    unlink(seq_path);
    unlink(rand_path);
    free(wbuf);
    free(rbuf);
    return r;
}
