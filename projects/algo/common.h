// 各个示例共用的小工具：计时、检查
#ifndef ALGO_COMMON_H
#define ALGO_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static inline double nowSeconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

// 检查失败时输出位置并退出，用来验证几种写法的结果一致
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: 检查失败：%s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                             \
        }                                                                        \
    } while (0)

// 固定种子的随机数（xorshift），每次运行结果相同
static unsigned long long g_rand = 88172645463325252ULL;
static inline unsigned long long nextRand(void)
{
    g_rand ^= g_rand << 13;
    g_rand ^= g_rand >> 7;
    g_rand ^= g_rand << 17;
    return g_rand;
}
static inline int randInt(int lo, int hi) { return lo + (int)(nextRand() % (unsigned long long)(hi - lo + 1)); }

#endif
