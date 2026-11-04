// 各个实验共用的小工具：计时、检查、读取进程的内存占用和缺页次数
#ifndef LX_MEMORY_COMMON_H
#define LX_MEMORY_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static inline double nowSeconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: 检查失败：%s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                             \
        }                                                                        \
    } while (0)

// 从 /proc/self/status 读一项（单位 kB），比如 "VmRSS"、"VmSize"
static inline long statusKB(const char* name)
{
    FILE* f = fopen("/proc/self/status", "r");
    char line[256];
    long v = -1;
    size_t n = strlen(name);
    while (f && fgets(line, sizeof line, f))
        if (strncmp(line, name, n) == 0 && line[n] == ':') {
            v = atol(line + n + 1);
            break;
        }
    if (f) fclose(f);
    return v;
}

static inline double rssMB(void) { return statusKB("VmRSS") / 1024.0; }
static inline double vszMB(void) { return statusKB("VmSize") / 1024.0; }

// 次缺页（不需要读磁盘）和主缺页（需要读磁盘）的累计次数
static inline long minorFaults(void)
{
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    return u.ru_minflt;
}
static inline long majorFaults(void)
{
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    return u.ru_majflt;
}

// 从 /proc/meminfo 读一项（kB）
static inline long meminfoKB(const char* name)
{
    FILE* f = fopen("/proc/meminfo", "r");
    char line[256];
    long v = -1;
    size_t n = strlen(name);
    while (f && fgets(line, sizeof line, f))
        if (strncmp(line, name, n) == 0 && line[n] == ':') {
            v = atol(line + n + 1);
            break;
        }
    if (f) fclose(f);
    return v;
}

#endif
