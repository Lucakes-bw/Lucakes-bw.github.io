// glibc 的 malloc：小块从堆（brk）分配，大块直接 mmap；free 以后内存不一定还给操作系统；多线程的 arena
//   malloc where       看每次分配来自堆还是 mmap
//   malloc frag        碎片：释放了 99.9% 的小块，RSS 却降不下来；malloc_trim
//   malloc overhead    每个小块的额外开销
//   malloc threads N   N 个线程反复分配释放，看 RSS 和 arena 的数量
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>

#include "common.h"

#define MB (1024L * 1024)

static void* g_heapStart;

static const char* where(void* p)
{
    void* brk = sbrk(0);
    return p >= g_heapStart && p < brk ? "堆（brk）" : "mmap";
}

static void runWhere(void)
{
    g_heapStart = sbrk(0);
    size_t sizes[] = {100, 100 * 1024, 127 * 1024, 129 * 1024, 1 * MB};
    for (int i = 0; i < 5; ++i) {
        void* p = malloc(sizes[i]);
        printf("  malloc(%7zu)：%-10s %p\n", sizes[i], where(p), p);
    }
    // free 一个 mmap 来的块以后，阈值会调高到这个大小（动态阈值）
    void* a = malloc(1 * MB);
    printf("  malloc(1 MB)：  %-10s，free 它\n", where(a));
    free(a);
    void* b = malloc(1 * MB);
    printf("  再 malloc(1 MB)：%-10s（阈值已经调高）\n", where(b));
}

static void runFrag(void)
{
    int n = 1000000;
    char** ptrs = malloc(sizeof(char*) * (size_t)n);
    double r0 = rssMB();
    for (int i = 0; i < n; ++i) {
        ptrs[i] = malloc(100);
        memset(ptrs[i], 1, 100);
    }
    printf("  分配 100 万个 100 字节的块：RSS %+.1f MB\n", rssMB() - r0);
    for (int i = 0; i < n; ++i)
        if (i % 1000 != 999) {   // 每 1000 个只留下最后一个
            free(ptrs[i]);
            ptrs[i] = NULL;
        }
    printf("  释放 99.9%%，只留下 1000 个：    RSS %+.1f MB\n", rssMB() - r0);
    malloc_trim(0);
    printf("  malloc_trim(0) 以后：         RSS %+.1f MB\n", rssMB() - r0);
    for (int i = 0; i < n; ++i) free(ptrs[i]);
    printf("  全部释放：                    RSS %+.1f MB\n", rssMB() - r0);
    free(ptrs);

    // 对照：一个都不留，全部释放。不调用 malloc_trim 时，RSS 也不一定会降
    size_t sizes[] = {100, 200};
    for (int k = 0; k < 2; ++k) {
        r0 = rssMB();
        ptrs = malloc(sizeof(char*) * (size_t)n);
        for (int i = 0; i < n; ++i) {
            ptrs[i] = malloc(sizes[k]);
            memset(ptrs[i], 1, sizes[k]);
        }
        double peak = rssMB() - r0;
        for (int i = n - 1; i >= 0; --i) free(ptrs[i]);
        free(ptrs);
        double after = rssMB() - r0;
        malloc_trim(0);
        printf("  对照：100 万个 %zu 字节的块：分配后 RSS %+.1f MB，全部释放后 %+.1f MB，malloc_trim 后 %+.1f MB\n", sizes[k],
               peak, after, rssMB() - r0);
    }
}

static void runOverhead(void)
{
    size_t sizes[] = {1, 8, 24, 25, 100, 1000};
    for (int i = 0; i < 6; ++i) {
        char* a = malloc(sizes[i]);
        char* b = malloc(sizes[i]);
        printf("  malloc(%4zu)：可用 %4zu 字节，相邻两块的地址相差 %4ld 字节\n", sizes[i], malloc_usable_size(a),
               (long)(b - a));
    }
    int n = 1000000;
    double r0 = rssMB();
    for (int i = 0; i < n; ++i) *(char*)malloc(1) = 1;
    printf("  分配 100 万个 1 字节的块：RSS %+.1f MB\n", rssMB() - r0);
}

static int g_rounds = 20;
static void* worker(void* arg)
{
    (void)arg;
    // 每轮分配 50 万个 64 字节的块（约 40 MB），再全部释放
    int n = 500000;
    char** p = malloc(sizeof(char*) * (size_t)n);
    for (int r = 0; r < g_rounds; ++r) {
        for (int i = 0; i < n; ++i) {
            p[i] = malloc(64);
            p[i][0] = 1;
        }
        for (int i = 0; i < n; i += 2) free(p[i]);   // 先释放一半，再释放另一半：模拟交错的生命周期
        for (int i = 1; i < n; i += 2) free(p[i]);
    }
    free(p);
    return NULL;
}

static int countArenas(void)
{
    // malloc_info 的 XML 输出中，每个 arena 是一个 <heap nr=...>
    char* buf;
    size_t len;
    FILE* f = open_memstream(&buf, &len);
    malloc_info(0, f);
    fclose(f);
    int n = 0;
    for (char* p = buf; (p = strstr(p, "<heap nr=")); ++p) ++n;
    free(buf);
    return n;
}

static void runThreads(int threads)
{
    pthread_t t[64];
    double t0 = nowSeconds();
    for (int i = 0; i < threads; ++i) pthread_create(&t[i], NULL, worker, NULL);
    for (int i = 0; i < threads; ++i) pthread_join(t[i], NULL);
    double sec = nowSeconds() - t0;
    printf("  %d 个线程，每个 %d 轮×50 万次 malloc/free：%.2f 秒；arena %d 个；结束时 RSS %.1f MB（峰值 %.1f MB）\n", threads,
           g_rounds, sec, countArenas(), rssMB(), statusKB("VmHWM") / 1024.0);
}

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "where";
    if (strcmp(mode, "where") == 0) runWhere();
    else if (strcmp(mode, "frag") == 0) runFrag();
    else if (strcmp(mode, "overhead") == 0) runOverhead();
    else if (strcmp(mode, "threads") == 0) runThreads(argc > 2 ? atoi(argv[2]) : 8);
    return 0;
}
