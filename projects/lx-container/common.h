// 各个实验共用的小工具：计时、检查
#ifndef LX_CONTAINER_COMMON_H
#define LX_CONTAINER_COMMON_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline double nowSeconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            fprintf(stderr, "%s:%d: 检查失败：%s（%s）\n", __FILE__, __LINE__, #cond, strerror(errno)); \
            exit(1);                                                                                 \
        }                                                                                            \
    } while (0)

// 把一个字符串写进文件（cgroup、/proc 中的控制文件），失败时返回 -1
static inline int writeFile(const char* path, const char* s)
{
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    int ok = fputs(s, f) >= 0;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : -1;
}

#endif
