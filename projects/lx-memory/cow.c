// fork 与写时复制：fork 的耗时和父进程的内存大小；子进程写内存时的复制；透明大页下的写时复制；posix_spawn
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"

#define MB (1024L * 1024)
extern char** environ;

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

// 申请 size 字节并全部写一遍；huge 为真时用透明大页
static char* allocTouched(long size, int huge)
{
    char* p = mmap(NULL, size + 2 * MB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(p != MAP_FAILED);
    p = (char*)(((unsigned long)p + 2 * MB - 1) & ~(2 * MB - 1));
    CHECK(madvise(p, size, huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE) == 0);
    for (long i = 0; i < size; i += 4096) p[i] = 1;
    return p;
}

// fork 一个立即退出的子进程，返回 fork() 本身的平均耗时
static double forkTime(void)
{
    double total = 0;
    fflush(stdout);   // 否则缓冲区中还没输出的内容会被子进程复制一份，输出两次
    for (int k = 0; k < 10; ++k) {
        double t0 = nowSeconds();
        pid_t pid = fork();
        if (pid == 0) _exit(0);
        total += nowSeconds() - t0;
        waitpid(pid, NULL, 0);
    }
    return total / 10;
}

// 子进程中每隔 step 字节写一个字节，报告缺页、耗时、复制出来的私有内存
static void childWrite(const char* what, char* p, long size, long step)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        long priv0 = smapsKB("Private_Dirty"), f0 = minorFaults();
        double t0 = nowSeconds();
        for (long i = 0; i < size; i += step) p[i] = 2;
        double t = nowSeconds() - t0;
        printf("  %-36s 缺页 %6ld 次，用时 %6.1f 毫秒，复制出 %6.1f MB\n", what, minorFaults() - f0, t * 1000,
               (smapsKB("Private_Dirty") - priv0) / 1024.0);
        fflush(stdout);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
}

int main(void)
{
    printf("== fork 的耗时和父进程的内存\n");
    printf("  父进程 RSS %6.0f MB：fork %.3f 毫秒\n", rssMB(), forkTime() * 1000);
    long sizes[] = {256, 1024};
    char* bufs[2];
    for (int i = 0; i < 2; ++i) {
        bufs[i] = allocTouched(sizes[i] * MB, 0);
        printf("  父进程 RSS %6.0f MB：fork %.3f 毫秒（4 KB 页）\n", rssMB(), forkTime() * 1000);
    }
    munmap(bufs[1], 1024 * MB);
    char* huge = allocTouched(1024 * MB, 1);
    printf("  父进程 RSS %6.0f MB：fork %.3f 毫秒（其中 1 GB 是透明大页，AnonHugePages %ld MB）\n", rssMB(),
           forkTime() * 1000, smapsKB("AnonHugePages") / 1024);
    munmap(huge, 1024 * MB);

    printf("== 子进程写父进程的内存：写时复制\n");
    char* p = bufs[0];
    childWrite("写 256 MB 中的每一页（4 KB 页）", p, 256 * MB, 4096);
    childWrite("只读不写", p, 0, 4096);
    childWrite("每 2 MB 写一个字节（4 KB 页）", p, 256 * MB, 2 * MB);

    printf("== 透明大页下的写时复制\n");
    char* h = allocTouched(256 * MB, 1);
    printf("  父进程 AnonHugePages %ld MB\n", smapsKB("AnonHugePages") / 1024);
    childWrite("每 2 MB 写一个字节（透明大页）", h, 256 * MB, 2 * MB);
    childWrite("写 256 MB 中的每一页（透明大页）", h, 256 * MB, 4096);

    printf("== 在 1 GB 的进程中启动 200 次 /bin/true\n");
    char* big = allocTouched(1024 * MB, 0);
    (void)big;
    char* args[] = {"/bin/true", NULL};
    fflush(stdout);
    double t0 = nowSeconds();
    for (int i = 0; i < 200; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            execv("/bin/true", args);
            _exit(127);
        }
        waitpid(pid, NULL, 0);
    }
    printf("  fork + exec：  %.1f 毫秒/次\n", (nowSeconds() - t0) / 200 * 1000);
    t0 = nowSeconds();
    for (int i = 0; i < 200; ++i) {
        pid_t pid;
        CHECK(posix_spawn(&pid, "/bin/true", NULL, NULL, args, environ) == 0);   // glibc 用 clone(CLONE_VM | CLONE_VFORK) 实现，不复制页表
        waitpid(pid, NULL, 0);
    }
    printf("  posix_spawn：  %.1f 毫秒/次\n", (nowSeconds() - t0) / 200 * 1000);
    return 0;
}
