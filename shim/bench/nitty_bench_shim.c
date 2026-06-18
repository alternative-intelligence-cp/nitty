#define _GNU_SOURCE
#include "nitty_bench_shim.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int64_t nitty_bench_time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((int64_t)ts.tv_sec * 1000000000LL) + (int64_t)ts.tv_nsec;
}

int64_t nitty_bench_read_rss(void) {
    FILE* fp = fopen("/proc/self/statm", "r");
    if (!fp) return 0;
    long size = 0, resident = 0;
    if (fscanf(fp, "%ld %ld", &size, &resident) != 2) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    /* Resident is in pages. Usually 4096 bytes per page on x86_64 linux */
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 0) page_size = 4096;
    return (int64_t)resident * page_size;
}

int64_t nitty_bench_read_fds(void) {
    DIR* dir = opendir("/proc/self/fd");
    if (!dir) return 0;
    int64_t count = 0;
    struct dirent* dp;
    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) continue;
        count++;
    }
    closedir(dir);
    return count;
}
