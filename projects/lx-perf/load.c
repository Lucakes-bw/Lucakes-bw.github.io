// 性能分析从哪里入手：几种「慢」的程序，用 time、top、vmstat 区分它们
//   load spin 秒数          一直计算（用户态 CPU）
//   load tiny 次数          每次 write 1 字节到 /dev/null（内核态 CPU）
//   load sleep 秒数         什么都不做，睡眠
//   load threads 线程数 秒数  多个线程同时计算
//   load dread 文件         用 O_DIRECT 不停地读文件（等磁盘，进程处于 D 状态）
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include "common.h"

static volatile unsigned long sink;

static void spinFor(double sec)
{
    double end = nowSeconds() + sec;
    unsigned long x = 1;
    while (nowSeconds() < end)
        for (int i = 0; i < 100000; i++) x = x * 6364136223846793005UL + 1;
    sink = x;
}

static void* spinThread(void* arg)
{
    spinFor(*(double*)arg);
    return NULL;
}

int main(int argc, char** argv)
{
    CHECK(argc >= 3);
    const char* mode = argv[1];
    if (strcmp(mode, "spin") == 0) {
        spinFor(atof(argv[2]));
    } else if (strcmp(mode, "tiny") == 0) {
        long n = atol(argv[2]);
        int fd = open("/dev/null", O_WRONLY);
        CHECK(fd >= 0);
        for (long i = 0; i < n; i++) CHECK(write(fd, "x", 1) == 1);
        close(fd);
    } else if (strcmp(mode, "sleep") == 0) {
        usleep((useconds_t)(atof(argv[2]) * 1e6));
    } else if (strcmp(mode, "threads") == 0) {
        CHECK(argc == 4);
        int n = atoi(argv[2]);
        double sec = atof(argv[3]);
        pthread_t t[64];
        CHECK(n > 0 && n <= 64);
        for (int i = 0; i < n; i++) pthread_create(&t[i], NULL, spinThread, &sec);
        for (int i = 0; i < n; i++) pthread_join(t[i], NULL);
    } else if (strcmp(mode, "dread") == 0) {
        int fd = open(argv[2], O_RDONLY | O_DIRECT);
        CHECK(fd >= 0);
        size_t sz = 4 << 20;
        void* buf;
        CHECK(posix_memalign(&buf, 4096, sz) == 0);
        for (;;) {
            ssize_t r = read(fd, buf, sz);
            CHECK(r >= 0);
            if (r == 0) lseek(fd, 0, SEEK_SET);
        }
    } else {
        fprintf(stderr, "未知模式 %s\n", mode);
        return 1;
    }
    return 0;
}
