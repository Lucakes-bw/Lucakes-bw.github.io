// 虚拟内存：进程的地址空间里有什么；申请内存时只分配了地址，第一次访问才分配物理页（缺页）
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"

int g_inited = 42;          // 已初始化的全局变量：.data
int g_zero;                 // 未初始化的全局变量：.bss

// 输出 /proc/self/maps 中包含某个地址的那一行
static void whereIs(const char* what, const void* p)
{
    FILE* f = fopen("/proc/self/maps", "r");
    char line[512];
    unsigned long a = (unsigned long)p;
    while (fgets(line, sizeof line, f)) {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && a >= lo && a < hi) {
            line[strcspn(line, "\n")] = '\0';
            printf("  %-16s %p  在  %s\n", what, p, line);
            break;
        }
    }
    fclose(f);
}

#define MB (1024L * 1024)

// 从 /proc/self/smaps_rollup 读一项（kB）
static long smapsKB(const char* name)
{
    FILE* f = fopen("/proc/self/smaps_rollup", "r");
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

// 逐页写一个字节，返回耗时；同时报告缺页次数和 RSS 的变化
static void touch(const char* what, char* p, long bytes, long step)
{
    long f0 = minorFaults();
    double r0 = rssMB(), t0 = nowSeconds();
    for (long i = 0; i < bytes; i += step) p[i] = 1;
    double t = nowSeconds() - t0;
    long faults = minorFaults() - f0;
    printf("  %-34s 缺页 %6ld 次，RSS %+7.1f MB，用时 %6.1f 毫秒（每次缺页 %.2f 微秒）\n", what, faults, rssMB() - r0,
           t * 1000, faults ? t / faults * 1e6 : 0.0);
}

int main(void)
{
    int local = 0;
    static const char rodata[] = "hello";
    char* small = malloc(100);
    char* big = malloc(64 * MB);
    printf("== 地址空间中的各个区域\n");
    whereIs("main（代码）", (void*)main);
    whereIs("字符串常量", rodata);
    whereIs("g_inited", &g_inited);
    whereIs("g_zero", &g_zero);
    whereIs("malloc(100)", small);
    whereIs("malloc(64 MB)", big);
    whereIs("printf（libc）", (void*)printf);
    whereIs("局部变量", &local);
    free(small);
    free(big);

    printf("== 申请 1 GB：只分配了地址\n");
    printf("  申请前：VSZ %7.1f MB，RSS %6.1f MB\n", vszMB(), rssMB());
    long size = 1024 * MB;
    char* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(p != MAP_FAILED);
    printf("  申请后：VSZ %7.1f MB，RSS %6.1f MB\n", vszMB(), rssMB());

    printf("== 第一次访问才分配物理页\n");
    {
        long f0 = minorFaults();
        double r0 = rssMB(), t0 = nowSeconds();
        volatile char sum = 0;
        for (long i = 0; i < 100 * MB; i += 4096) sum += p[i];
        printf("  %-34s 缺页 %6ld 次，RSS %+7.1f MB，用时 %6.1f 毫秒\n", "读前 100 MB", minorFaults() - f0,
               rssMB() - r0, (nowSeconds() - t0) * 1000);
    }
    touch("写前 100 MB", p, 100 * MB, 4096);
    touch("再写一遍这 100 MB", p, 100 * MB, 4096);
    touch("写第 100～200 MB，每 1 KB 写一次", p + 100 * MB, 100 * MB, 1024);
    printf("  现在：VSZ %7.1f MB，RSS %6.1f MB\n", vszMB(), rssMB());

    printf("== 透明大页：一次缺页映射 2 MB\n");
    char* q = mmap(NULL, 512 * MB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(q != MAP_FAILED);
    char* aligned = (char*)(((unsigned long)q + 2 * MB - 1) & ~(2 * MB - 1));   // 大页要 2 MB 对齐
    CHECK(madvise(aligned, 256 * MB, MADV_HUGEPAGE) == 0);
    touch("写 256 MB，MADV_HUGEPAGE", aligned, 256 * MB, 4096);
    touch("写 256 MB，普通的 4 KB 页", p + 200 * MB, 256 * MB, 4096);
    printf("  /proc/self/smaps_rollup 中的 AnonHugePages：%ld MB\n", smapsKB("AnonHugePages") / 1024);

    printf("== 归还：munmap 以后 RSS 下降\n");
    munmap(p, size);
    munmap(q, 512 * MB);
    printf("  VSZ %7.1f MB，RSS %6.1f MB\n", vszMB(), rssMB());
    return 0;
}
