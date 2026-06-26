#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>

#ifdef __APPLE__
#  include <sys/sysctl.h>
#  include <sys/types.h>
#  include <sys/mount.h>
#  include <mach/mach.h>
#endif

#include "system_info.h"
#include "benchmark.h"

/* ---- helpers ---- */

#ifndef __APPLE__
static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
        s[--n] = '\0';
}

/* Read a single-line value from /proc/cpuinfo for the given key */
static bool cpuinfo_val(const char *key, char *out, size_t outsz) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return false;
    char line[512];
    bool found = false;
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                char *val = colon + 2;
                rtrim(val);
                snprintf(out, outsz, "%s", val);
                found = true;
                break;
            }
        }
    }
    fclose(f);
    return found;
}
#endif /* !__APPLE__ */

#ifdef __APPLE__
static bool sysctl_str(const char *name, char *out, size_t outsz) {
    size_t sz = outsz - 1;
    if (sysctlbyname(name, out, &sz, NULL, 0) == 0) {
        out[sz] = '\0';
        return true;
    }
    return false;
}

static uint64_t sysctl_u64(const char *name) {
    uint64_t val = 0;
    size_t sz = sizeof(val);
    sysctlbyname(name, &val, &sz, NULL, 0);
    return val;
}
#endif

static bool path_is_tmpfs(const char *path) {
#ifdef __linux__
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return false;
    char line[512];
    /* We want to find the longest-prefix mount that covers path */
    char best_fs[64] = "";
    size_t best_len = 0;
    while (fgets(line, sizeof(line), f)) {
        char dev[256], mnt[256], fs[64];
        if (sscanf(line, "%255s %255s %63s", dev, mnt, fs) == 3) {
            size_t mlen = strlen(mnt);
            if (strncmp(path, mnt, mlen) == 0 && mlen > best_len) {
                best_len = mlen;
                snprintf(best_fs, sizeof(best_fs), "%s", fs);
            }
        }
    }
    fclose(f);
    return strcmp(best_fs, "tmpfs") == 0;
#else
    (void)path;
    return false;
#endif
}

static void detect_fs_type(const char *path, char *out, size_t outsz) {
#ifdef __APPLE__
    struct statfs st;
    if (statfs(path, &st) == 0) {
        snprintf(out, outsz, "%s", st.f_fstypename);
        return;
    }
#elif defined(__linux__)
    FILE *f = fopen("/proc/mounts", "r");
    if (f) {
        char line[512];
        char best_fs[64] = "unknown";
        size_t best_len = 0;
        while (fgets(line, sizeof(line), f)) {
            char dev[256], mnt[256], fs[64];
            if (sscanf(line, "%255s %255s %63s", dev, mnt, fs) == 3) {
                size_t mlen = strlen(mnt);
                if (strncmp(path, mnt, mlen) == 0 && mlen > best_len) {
                    best_len = mlen;
                    snprintf(best_fs, sizeof(best_fs), "%s", fs);
                }
            }
        }
        fclose(f);
        snprintf(out, outsz, "%s", best_fs);
        return;
    }
#endif
    (void)path;
    snprintf(out, outsz, "unknown");
}

/* ---- main collect ---- */

void collect_system_info(SystemInfo *info, const char *device_name,
                         const char *storage_path) {
    memset(info, 0, sizeof(*info));

    /* hostname */
    gethostname(info->hostname, sizeof(info->hostname));

    /* device name */
    snprintf(info->device_name, sizeof(info->device_name), "%s", device_name);

    /* timestamp ISO8601 */
    time_t now = time(NULL);
    struct tm *tm_utc = gmtime(&now);
    strftime(info->timestamp, sizeof(info->timestamp),
             "%Y-%m-%dT%H:%M:%SZ", tm_utc);

    /* uname */
    struct utsname un;
    uname(&un);
    snprintf(info->os_name,       sizeof(info->os_name),       "%s", un.sysname);
    snprintf(info->os_version,    sizeof(info->os_version),    "%s", un.release);
    snprintf(info->kernel_version,sizeof(info->kernel_version),"%s", un.version);
    snprintf(info->architecture,  sizeof(info->architecture),  "%s", un.machine);

    /* CPU model */
#ifdef __APPLE__
    if (!sysctl_str("machdep.cpu.brand_string", info->cpu_model,
                    sizeof(info->cpu_model)))
        sysctl_str("hw.model", info->cpu_model, sizeof(info->cpu_model));
    uint64_t freq = sysctl_u64("hw.cpufrequency");
    if (freq == 0) freq = sysctl_u64("hw.cpufrequency_max");
    info->cpu_freq_mhz = freq ? (double)freq / 1e6 : 0.0;
    int ncpu = 0; size_t sz = sizeof(ncpu);
    sysctlbyname("hw.physicalcpu", &ncpu, &sz, NULL, 0);
    info->cpu_physical_cores = ncpu;
#else
    if (!cpuinfo_val("model name", info->cpu_model, sizeof(info->cpu_model)))
        if (!cpuinfo_val("Processor", info->cpu_model, sizeof(info->cpu_model)))
            cpuinfo_val("Model name", info->cpu_model, sizeof(info->cpu_model));

    char freq_str[64] = "";
    if (cpuinfo_val("cpu MHz", freq_str, sizeof(freq_str)))
        info->cpu_freq_mhz = atof(freq_str);

    /* physical cores: count unique "core id" + "physical id" pairs */
    {
        char cores_str[32] = "";
        if (cpuinfo_val("cpu cores", cores_str, sizeof(cores_str)))
            info->cpu_physical_cores = atoi(cores_str);
    }
#endif

    info->cpu_logical_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (info->cpu_physical_cores == 0)
        info->cpu_physical_cores = info->cpu_logical_cores;

    /* RAM */
#ifdef __APPLE__
    uint64_t memsize = sysctl_u64("hw.memsize");
    info->ram_total_mb = (long)(memsize / (1024*1024));

    /* available RAM via mach */
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    host_page_size(host, &page_size);
    vm_statistics64_data_t vmstat;
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64,
                          (host_info64_t)&vmstat, &cnt) == KERN_SUCCESS) {
        info->ram_available_mb = (long)(
            ((uint64_t)vmstat.free_count + vmstat.inactive_count)
            * page_size / (1024*1024));
    }
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_sz = sysconf(_SC_PAGE_SIZE);
    info->ram_total_mb = (long)((uint64_t)pages * (uint64_t)page_sz / (1024*1024));

    /* read MemAvailable from /proc/meminfo */
    FILE *mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[256];
        while (fgets(line, sizeof(line), mf)) {
            unsigned long kbytes = 0;
            if (sscanf(line, "MemAvailable: %lu kB", &kbytes) == 1) {
                info->ram_available_mb = (long)(kbytes / 1024);
                break;
            }
        }
        fclose(mf);
    }
#endif

    /* Disk model / type */
#ifdef __linux__
    /* Find the backing device for storage_path via /proc/mounts then
       look up /sys/block */
    {
        char best_fs[64] = "";
        char best_dev[256] = "";
        size_t best_len = 0;
        FILE *f = fopen("/proc/mounts", "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                char dev[256], mnt[256], fs[64];
                if (sscanf(line, "%255s %255s %63s", dev, mnt, fs) == 3) {
                    size_t mlen = strlen(mnt);
                    if (strncmp(storage_path, mnt, mlen) == 0 && mlen > best_len) {
                        best_len = mlen;
                        snprintf(best_dev, sizeof(best_dev), "%s", dev);
                        snprintf(best_fs, sizeof(best_fs), "%s", fs);
                    }
                }
            }
            fclose(f);
        }
        (void)best_fs;

        /* strip partition number: /dev/sda1 -> sda */
        if (strncmp(best_dev, "/dev/", 5) == 0) {
            char *devname = best_dev + 5;
            /* remove trailing digit(s) for partition */
            size_t dlen = strlen(devname);
            while (dlen > 0 && devname[dlen-1] >= '0' && devname[dlen-1] <= '9')
                devname[--dlen] = '\0';

            char model_path[512];
            snprintf(model_path, sizeof(model_path),
                     "/sys/block/%s/device/model", devname);
            FILE *mf2 = fopen(model_path, "r");
            if (mf2) {
                if (fgets(info->disk_model, sizeof(info->disk_model), mf2))
                    rtrim(info->disk_model);
                fclose(mf2);
            }

            char rot_path[512];
            snprintf(rot_path, sizeof(rot_path),
                     "/sys/block/%s/queue/rotational", devname);
            FILE *rf = fopen(rot_path, "r");
            if (rf) {
                int rot = 1;
                if (fscanf(rf, "%d", &rot) == 1)
                    snprintf(info->disk_type, sizeof(info->disk_type),
                             rot ? "HDD" : "SSD");
                fclose(rf);
            }
        }
        if (info->disk_model[0] == '\0')
            snprintf(info->disk_model, sizeof(info->disk_model), "unknown");
        if (info->disk_type[0] == '\0')
            snprintf(info->disk_type, sizeof(info->disk_type), "unknown");
    }
#elif defined(__APPLE__)
    snprintf(info->disk_model, sizeof(info->disk_model), "unknown");
    snprintf(info->disk_type,  sizeof(info->disk_type),  "unknown");
    /* Could popen diskutil but keep it simple */
#else
    snprintf(info->disk_model, sizeof(info->disk_model), "unknown");
    snprintf(info->disk_type,  sizeof(info->disk_type),  "unknown");
#endif

    /* Filesystem type */
    detect_fs_type(storage_path, info->filesystem_type, sizeof(info->filesystem_type));

    /* Storage path */
    snprintf(info->storage_path_used, sizeof(info->storage_path_used),
             "%s", storage_path);

    /* tmpfs check */
    info->storage_is_tmpfs = path_is_tmpfs(storage_path);
}

void print_system_info(const SystemInfo *info, bool no_color) {
    const char *cy = no_color ? "" : COL_CYAN;
    const char *bl = no_color ? "" : COL_BOLD;
    const char *rs = no_color ? "" : COL_RESET;

    printf("\n");
    printf("%sDevice%s : %s%s%s\n", bl, rs, cy, info->device_name, rs);
    printf("%sHost  %s : %s\n", bl, rs, info->hostname);
    printf("%sOS    %s : %s %s\n", bl, rs, info->os_name, info->os_version);
    printf("%sArch  %s : %s\n", bl, rs, info->architecture);
    printf("%sCPU   %s : %s", bl, rs, info->cpu_model);
    if (info->cpu_freq_mhz > 0.0)
        printf(" @ %.0f MHz", info->cpu_freq_mhz);
    printf("\n");
    printf("%sCores %s : %d physical / %d logical\n", bl, rs,
           info->cpu_physical_cores, info->cpu_logical_cores);
    printf("%sRAM   %s : %ld MB total / %ld MB available\n", bl, rs,
           info->ram_total_mb, info->ram_available_mb);
    printf("%sDisk  %s : %s (%s)\n", bl, rs, info->disk_model, info->disk_type);
    printf("%sFS    %s : %s\n", bl, rs, info->filesystem_type);

    if (info->storage_is_tmpfs) {
        const char *yr = no_color ? "" : COL_YELLOW;
        printf("\n%sWARNING: %s is tmpfs (RAM-backed). Storage benchmark may reflect RAM speed, not disk speed.%s\n",
               yr, info->storage_path_used, rs);
        printf("%s         Use --storage-path /path/to/real/disk to override.%s\n", yr, rs);
    }
    printf("\n");
}
