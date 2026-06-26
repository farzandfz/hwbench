#pragma once
#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char hostname[256];
    char device_name[256];
    char timestamp[64];
    char os_name[64];
    char os_version[128];
    char kernel_version[256];
    char architecture[64];
    char cpu_model[256];
    double cpu_freq_mhz;
    int cpu_physical_cores;
    int cpu_logical_cores;
    long ram_total_mb;
    long ram_available_mb;
    char disk_model[256];
    char disk_type[32];   /* "SSD", "HDD", "unknown" */
    char filesystem_type[64];
    char storage_path_used[512];
    bool storage_is_tmpfs;
} SystemInfo;

void collect_system_info(SystemInfo *info, const char *device_name,
                         const char *storage_path);
void print_system_info(const SystemInfo *info, bool no_color);

#endif /* SYSTEM_INFO_H */
