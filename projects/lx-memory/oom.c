// 内存不足：overcommit 让 malloc 很少失败；真正用完内存时，OOM killer 选一个进程杀掉
//   oom commit        各种大小的 malloc（不写入）能不能成功
//   oom eat 步长MB     每次申请并写满一块内存，直到被杀
//   oom hold MB       申请并写满一块内存，然后一直等着
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"

#define MB (1024L * 1024)
#define GB (1024L * MB)

static void runCommit(void)
{
    printf("  CommitLimit %ld MB，Committed_AS %ld MB，MemTotal %ld MB\n", meminfoKB("CommitLimit") / 1024,
           meminfoKB("Committed_AS") / 1024, meminfoKB("MemTotal") / 1024);
    long sizes[] = {1, 2, 4, 64};
    for (int i = 0; i < 4; ++i) {
        void* p = malloc(sizes[i] * GB);
        printf("  malloc(%2ld GB)：%s\n", sizes[i], p ? "成功" : "失败");
        free(p);
    }
    // 每次 1 GB、不释放、不写入，最多能申请多少次
    int n = 0;
    while (n < 1000 && malloc(1 * GB)) ++n;
    printf("  每次 1 GB、不写入、不释放：成功了 %d 次%s，VSZ %.0f GB\n", n, n == 1000 ? "（到上限 1000 次停止）" : "",
           vszMB() / 1024);
}

static void runEat(long step)
{
    long total = 0;
    for (;;) {
        char* p = malloc(step * MB);
        if (!p) {
            printf("malloc 返回 NULL，一共 %ld MB\n", total);
            return;
        }
        for (long i = 0; i < step * MB; i += 4096) ((volatile char*)p)[i] = 1;   // 写入，真正占用物理内存
        total += step;
        printf("已占用 %ld MB\n", total);
        fflush(stdout);
    }
}

static void runHold(long mb)
{
    volatile char* p = malloc(mb * MB);   // volatile：后面不再读这块内存，编译器可能把普通的 memset 优化掉
    CHECK(p);
    for (long i = 0; i < mb * MB; i += 4096) p[i] = 1;
    printf("pid %d 占用了 %ld MB\n", getpid(), mb);
    fflush(stdout);
    pause();
}

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "commit";
    if (strcmp(mode, "commit") == 0) runCommit();
    else if (strcmp(mode, "eat") == 0) runEat(argc > 2 ? atol(argv[2]) : 10);
    else if (strcmp(mode, "hold") == 0) runHold(argc > 2 ? atol(argv[2]) : 100);
    return 0;
}
