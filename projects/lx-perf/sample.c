// perf 采样的原理与陷阱：一个调用关系清楚的小程序
// 热点函数 checksum 被两条路径调用：handleUpload 处理的数据是 handleDownload 的 3 倍。
// 不同的编译选项下，看 perf 能不能分清这两条路径。
//   sample 轮数     运行指定的轮数后退出
//   sample 0        一直运行，用来演示 perf record -p
#include <stdint.h>

#include "common.h"

#define BLOCK (1 << 20)
#define NREC 20000

static uint8_t data[4 * BLOCK];

struct Record {
    int key;
    char name[28];
};
static struct Record recs[NREC];

// 很短的小函数，开了优化会被内联进调用它的函数
static inline uint64_t mix(uint64_t h, uint8_t b)
{
    h ^= b;
    h *= 0x100000001b3ULL;
    return h ^ (h >> 29);
}

__attribute__((noinline)) uint64_t checksum(const uint8_t* p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) h = mix(h, p[i]);
    return h;
}

__attribute__((noinline)) uint64_t handleUpload(void) { return checksum(data, 3 * BLOCK); }

__attribute__((noinline)) uint64_t handleDownload(void) { return checksum(data, BLOCK); }

static int cmpRecord(const void* a, const void* b)
{
    const struct Record* x = a;
    const struct Record* y = b;
    return (x->key > y->key) - (x->key < y->key);
}

__attribute__((noinline)) void sortRecords(void)
{
    for (int i = 0; i < NREC; i++) recs[i].key = rand();
    qsort(recs, NREC, sizeof recs[0], cmpRecord);
}

// 处理一轮请求：两次 checksum、一次排序
__attribute__((noinline)) uint64_t serveRound(void)
{
    uint64_t sum = handleUpload();
    sum += handleDownload();
    sortRecords();
    return sum;
}

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    long rounds = atol(argv[1]);
    for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 131);
    uint64_t sum = 0;
    double t0 = nowSeconds();
    for (long r = 0; rounds == 0 || r < rounds; r++) sum += serveRound();
    printf("%ld 轮，%.2f 秒（校验和 %llx）\n", rounds, nowSeconds() - t0, (unsigned long long)sum);
    return 0;
}
