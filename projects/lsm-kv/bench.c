// lsm-kv 的压测和检查
//   bench fill  目录 条数 值的字节数 seq|rand 同步间隔 布隆位数   写入，打印吞吐量和写放大
//   bench read  目录 条数 次数 yes|no 布隆位数                  随机读存在（yes）或不存在（no）的键
//   bench check 目录 次数                                        随机的写、删、读、重新打开，和一个简单的数组对照
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "lsm.h"

static double nowSeconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static uint64_t mix(uint64_t x)  // splitmix64：把 0、1、2…… 打散成看起来随机的 64 位数
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// 第 i 个键：16 个十六进制字符。seq 时按 i 递增，rand 时打散
static void makeKey(char* k, uint64_t i, int seq) { snprintf(k, 17, "%016llx", (unsigned long long)(seq ? i : mix(i))); }

// 进程写到存储设备的字节数（/proc/self/io 的 write_bytes，包括后来被内核写回的页缓存）
static uint64_t ioWriteBytes(void)
{
    FILE* f = fopen("/proc/self/io", "r");
    char line[128];
    unsigned long long v = 0;
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, "write_bytes: %llu", &v) == 1) break;
    if (f) fclose(f);
    return v;
}

static void printStats(Lsm* db, double sec, uint64_t ops)
{
    const LsmStats* s = lsmStats(db);
    double u = (double)s->userBytes;
    printf("%llu 次，%.2f 秒，每秒 %.0f 次\n", (unsigned long long)ops, sec, ops / sec);
    printf("写入的数据 %.1f MB；WAL %.1f MB，落盘 %.1f MB，合并写出 %.1f MB（读入 %.1f MB）\n", u / 1048576,
           s->walBytes / 1048576.0, s->flushBytes / 1048576.0, s->compactWrite / 1048576.0, s->compactRead / 1048576.0);
    printf("写放大 %.2f（WAL + 落盘 + 合并写出，除以写入的数据）；落盘 %llu 次，合并 %llu 次，平移 %llu 次，fsync %llu 次\n",
           (s->walBytes + s->flushBytes + s->compactWrite) / u, (unsigned long long)s->flushes,
           (unsigned long long)s->compactions, (unsigned long long)s->trivialMoves, (unsigned long long)s->fsyncs);
}

static void fill(const char* dir, uint64_t n, int vsize, int seq, int syncEvery, int bloom)
{
    LsmOptions o;
    lsmDefaultOptions(&o);
    o.syncEvery = syncEvery;
    o.bloomBitsPerKey = bloom;
    Lsm* db = lsmOpen(dir, &o);
    char k[17], *v = malloc((size_t)vsize);
    memset(v, 'v', (size_t)vsize);
    uint64_t io0 = ioWriteBytes();
    uint64_t slow = 0;  // 超过 10 毫秒的写：碰上了落盘或合并
    double worst = 0, slowSum = 0;
    double t0 = nowSeconds();
    for (uint64_t i = 0; i < n; i++) {
        makeKey(k, i, seq);
        memcpy(v, k, vsize < 16 ? (size_t)vsize : 16);
        double p0 = nowSeconds();
        lsmPut(db, k, 16, v, (uint32_t)vsize);
        double d = nowSeconds() - p0;
        if (d > worst) worst = d;
        if (d > 0.01) slow++, slowSum += d;
    }
    double t = nowSeconds() - t0;
    printStats(db, t, n);
    printf("超过 10 ms 的写 %llu 次，一共 %.2f 秒；最慢的一次 %.0f ms\n", (unsigned long long)slow, slowSum, worst * 1e3);
    printf("各层：\n");
    lsmPrintLevels(db);
    printf("SSTable 一共 %.1f MB；/proc/self/io 统计的写盘 %.1f MB\n", lsmDiskBytes(db) / 1048576.0,
           (ioWriteBytes() - io0) / 1048576.0);
    lsmClose(db);
    free(v);
}

static void readTest(const char* dir, uint64_t n, uint64_t times, int exist, int bloom)
{
    LsmOptions o;
    lsmDefaultOptions(&o);
    o.bloomBitsPerKey = bloom;  // 只影响新写的表；读的时候用文件里存的过滤器
    Lsm* db = lsmOpen(dir, &o);
    printf("各层：\n");
    lsmPrintLevels(db);
    char k[17];
    uint64_t found = 0;
    double t0 = nowSeconds();
    for (uint64_t i = 0; i < times; i++) {
        uint64_t idx = mix(i ^ 0x5555) % n;
        makeKey(k, exist ? idx : idx + n, 0);
        char* v;
        uint32_t vl;
        if (lsmGet(db, k, 16, &v, &vl)) {
            found++;
            free(v);
        }
    }
    double t = nowSeconds() - t0;
    const LsmStats* s = lsmStats(db);
    printf("%llu 次查找，找到 %llu 个，%.2f 秒，每秒 %.0f 次\n", (unsigned long long)times,
           (unsigned long long)found, t, times / t);
    printf("每次查找平均读 %.2f 个数据块；布隆过滤器检查 %llu 次，排除 %llu 次\n", (double)s->blockReads / times,
           (unsigned long long)s->bloomChecks, (unsigned long long)s->bloomNegatives);
    lsmClose(db);
}

// 用一个数组记录每个键的期望值（-1 表示不存在），随机地写、删、读，隔一段时间关闭再打开
static void check(const char* dir, long times)
{
    enum { KEYS = 20000 };
    static int expect[KEYS];
    for (int i = 0; i < KEYS; i++) expect[i] = -1;
    LsmOptions o;
    lsmDefaultOptions(&o);
    o.memtableBytes = 64 << 10;  // 很小的内存表和文件，让落盘和合并频繁发生
    o.tableBytes = 64 << 10;
    o.level1Bytes = 256 << 10;
    o.syncEvery = 0;
    Lsm* db = lsmOpen(dir, &o);
    srand(42);
    char k[17], v[64];
    long bad = 0, reopen = 0;
    for (long i = 0; i < times; i++) {
        int key = rand() % KEYS, op = rand() % 10;
        snprintf(k, sizeof k, "%016d", key);
        if (op < 5) {
            int val = rand();
            int vl = snprintf(v, sizeof v, "value-%d-%d", key, val);
            lsmPut(db, k, 16, v, (uint32_t)vl);
            expect[key] = val;
        } else if (op < 7) {
            lsmDelete(db, k, 16);
            expect[key] = -1;
        } else {
            char* got;
            uint32_t gl;
            int r = lsmGet(db, k, 16, &got, &gl);
            if (expect[key] < 0) {
                if (r) bad++, free(got);
            } else {
                int vl = snprintf(v, sizeof v, "value-%d-%d", key, expect[key]);
                if (!r || gl != (uint32_t)vl || memcmp(got, v, (size_t)vl)) bad++;
                if (r) free(got);
            }
        }
        if (i % 50000 == 49999) {
            lsmClose(db);
            db = lsmOpen(dir, &o);
            reopen++;
        }
    }
    const LsmStats* s = lsmStats(db);
    printf("%ld 次操作，重新打开 %ld 次，最后一轮落盘 %llu 次、合并 %llu 次；结果不一致 %ld 次\n", times, reopen,
           (unsigned long long)s->flushes, (unsigned long long)s->compactions, bad);
    lsmPrintLevels(db);
    lsmClose(db);
}

int main(int argc, char** argv)
{
    if (argc == 8 && strcmp(argv[1], "fill") == 0) {
        fill(argv[2], strtoull(argv[3], NULL, 10), atoi(argv[4]), strcmp(argv[5], "seq") == 0, atoi(argv[6]),
             atoi(argv[7]));
    } else if (argc == 7 && strcmp(argv[1], "read") == 0) {
        readTest(argv[2], strtoull(argv[3], NULL, 10), strtoull(argv[4], NULL, 10), strcmp(argv[5], "yes") == 0,
                 atoi(argv[6]));
    } else if (argc == 4 && strcmp(argv[1], "check") == 0) {
        check(argv[2], atol(argv[3]));
    } else {
        fprintf(stderr, "用法见文件开头的注释\n");
        return 1;
    }
    return 0;
}
