// CPU 缓存与分支预测：同样的计算量，访问内存和分支的方式不同，速度差多少
//   cache rowcol N        N×N 的 int 矩阵，按行求和与按列求和
//   cache latency 最大MB   在不同大小的内存里随机跳转，测每次访问内存的延迟
//   cache branch N        N 个随机字节，统计 >= 128 的和：数组乱序与排好序
//   cache share 间隔字节   两个线程各自累加自己的计数器，两个计数器相隔指定的字节数
#include <pthread.h>
#include <stdint.h>

#include "common.h"

__attribute__((noipa)) static long sumRows(const int* m, int n)
{
    long s = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) s += m[(size_t)i * n + j];
    return s;
}

__attribute__((noipa)) static long sumCols(const int* m, int n)
{
    long s = 0;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) s += m[(size_t)i * n + j];
    return s;
}

static void rowcol(int n)
{
    int* m = malloc(sizeof(int) * (size_t)n * n);
    CHECK(m);
    for (size_t i = 0; i < (size_t)n * n; i++) m[i] = (int)(i & 7);
    double t0 = nowSeconds();
    long s1 = sumRows(m, n);
    double t1 = nowSeconds();
    long s2 = sumCols(m, n);
    double t2 = nowSeconds();
    CHECK(s1 == s2);
    printf("%d×%d：按行 %.1f ms，按列 %.1f ms，慢 %.1f 倍\n", n, n, (t1 - t0) * 1e3, (t2 - t1) * 1e3,
           (t2 - t1) / (t1 - t0));
    free(m);
}

// 每个节点占一个缓存行（64 字节），next 指向随机的另一个节点，所有节点连成一个环
struct Node {
    struct Node* next;
    char pad[56];
};

static double chase(size_t bytes)
{
    size_t n = bytes / sizeof(struct Node);
    struct Node* a = malloc(n * sizeof(struct Node));
    size_t* order = malloc(n * sizeof(size_t));
    CHECK(a && order);
    for (size_t i = 0; i < n; i++) order[i] = i;
    for (size_t i = n - 1; i > 0; i--) {  // 打乱顺序
        size_t j = ((size_t)rand() << 16 ^ (size_t)rand()) % (i + 1);
        size_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (size_t i = 0; i < n; i++) a[order[i]].next = &a[order[(i + 1) % n]];
    long steps = 20 * 1000 * 1000;
    struct Node* p = &a[order[0]];
    for (size_t i = 0; i < n; i++) p = p->next;  // 先走一圈，让数据进入缓存
    double t0 = nowSeconds();
    for (long i = 0; i < steps; i++) p = p->next;
    double t = nowSeconds() - t0;
    CHECK(p != NULL);
    free(a);
    free(order);
    return t / steps * 1e9;
}

__attribute__((noipa)) static long sumBig(const uint8_t* d, size_t n)
{
    long s = 0;
    for (size_t i = 0; i < n; i++)
        if (d[i] >= 128) s += d[i];
    return s;
}

static int cmpByte(const void* a, const void* b) { return *(const uint8_t*)a - *(const uint8_t*)b; }

static void branch(size_t n)
{
    uint8_t* d = malloc(n);
    CHECK(d);
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)rand();
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) qsort(d, n, 1, cmpByte);
        long s = 0;
        double t0 = nowSeconds();
        for (int r = 0; r < 10; r++) s += sumBig(d, n);
        printf("%s：%.1f ms（和 %ld）\n", pass ? "排好序" : "乱序  ", (nowSeconds() - t0) * 1e3, s);
    }
    free(d);
}

static char counters[256] __attribute__((aligned(256)));
static size_t gap;

static void* bump(void* arg)
{
    volatile long* c = (volatile long*)(counters + (arg ? gap : 0));
    for (long i = 0; i < 200 * 1000 * 1000; i++) (*c)++;
    return NULL;
}

int main(int argc, char** argv)
{
    CHECK(argc == 3);
    const char* mode = argv[1];
    if (strcmp(mode, "rowcol") == 0) {
        rowcol(atoi(argv[2]));
    } else if (strcmp(mode, "latency") == 0) {
        long maxMB = atol(argv[2]);
        for (size_t kb = 4; kb <= (size_t)maxMB * 1024; kb *= 2) {
            double ns = chase(kb * 1024);
            if (kb < 1024)
                printf("%6zu KB  %6.1f ns\n", kb, ns);
            else
                printf("%6zu MB  %6.1f ns\n", kb / 1024, ns);
            fflush(stdout);
        }
    } else if (strcmp(mode, "branch") == 0) {
        branch((size_t)atol(argv[2]));
    } else if (strcmp(mode, "share") == 0) {
        gap = (size_t)atol(argv[2]);
        CHECK(gap >= 8 && gap <= 248);
        pthread_t a, b;
        double t0 = nowSeconds();
        pthread_create(&a, NULL, bump, NULL);
        pthread_create(&b, NULL, bump, (void*)1);
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        printf("间隔 %3zu 字节：%.2f 秒\n", gap, nowSeconds() - t0);
    } else {
        fprintf(stderr, "未知模式 %s\n", mode);
        return 1;
    }
    return 0;
}
