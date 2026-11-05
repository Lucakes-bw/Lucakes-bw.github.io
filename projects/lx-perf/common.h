// 各个实验共用的小工具：计时、检查、读取进程用掉的 CPU 时间
#ifndef LX_PERF_COMMON_H
#define LX_PERF_COMMON_H

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

// 本进程（所有线程）到目前为止用掉的用户态、内核态 CPU 时间，单位秒
static inline void cpuSeconds(double* user, double* sys)
{
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    *user = (double)u.ru_utime.tv_sec + (double)u.ru_utime.tv_usec / 1e6;
    *sys = (double)u.ru_stime.tv_sec + (double)u.ru_stime.tv_usec / 1e6;
}

// 主动让出 CPU（等锁、等 IO、睡眠）和被抢占的次数
static inline void ctxSwitches(long* voluntary, long* involuntary)
{
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    *voluntary = u.ru_nvcsw;
    *involuntary = u.ru_nivcsw;
}

#endif
