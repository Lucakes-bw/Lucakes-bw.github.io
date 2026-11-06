// cgroup 的 CPU 配额：总的 CPU 够用，为什么请求的延迟变长了
//   cgcpu 线程数 每个请求的CPU毫秒数 秒数
//   每个线程不停地处理请求：每个请求要消耗指定毫秒数的 CPU。统计每个请求从开始到完成的时间
#include <pthread.h>

#include "common.h"

static double reqMs, seconds;
static int nThreads;
static double* lat[64];
static int cnt[64];

static double threadCpu(void)
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void* worker(void* arg)
{
    int id = (int)(long)arg;
    lat[id] = malloc(sizeof(double) * 200000);
    double end = nowSeconds() + seconds;
    volatile unsigned long x = 1;
    while (nowSeconds() < end && cnt[id] < 200000) {
        double t0 = nowSeconds(), c0 = threadCpu();
        while (threadCpu() - c0 < reqMs / 1e3)
            for (int i = 0; i < 1000; i++) x = x * 6364136223846793005UL + 1;
        lat[id][cnt[id]++] = (nowSeconds() - t0) * 1e3;
    }
    return NULL;
}

static int cmp(const void* a, const void* b)
{
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

int main(int argc, char** argv)
{
    CHECK(argc == 4);
    nThreads = atoi(argv[1]);
    reqMs = atof(argv[2]);
    seconds = atof(argv[3]);
    CHECK(nThreads > 0 && nThreads <= 64);
    pthread_t t[64];
    for (int i = 0; i < nThreads; i++) pthread_create(&t[i], NULL, worker, (void*)(long)i);
    for (int i = 0; i < nThreads; i++) pthread_join(t[i], NULL);
    int n = 0;
    for (int i = 0; i < nThreads; i++) n += cnt[i];
    double* all = malloc(sizeof(double) * (size_t)n);
    for (int i = 0, k = 0; i < nThreads; i++)
        for (int j = 0; j < cnt[i]; j++) all[k++] = lat[i][j];
    qsort(all, (size_t)n, sizeof(double), cmp);
    printf("%d 个线程，%d 个请求（每秒 %.0f 个）；延迟 中位数 %.1f ms，p99 %.1f ms，最大 %.1f ms\n", nThreads, n,
           n / seconds, all[n / 2], all[(int)(n * 0.99)], all[n - 1]);
    return 0;
}
