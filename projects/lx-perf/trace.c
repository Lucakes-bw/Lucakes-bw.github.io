// bpftrace：不改代码、不重启，看清程序在做什么
//   trace serve 秒数   模拟一个服务：不停地处理三种请求，偶尔有一个很慢
//   trace empty 次数   调用一个空函数指定的次数，测 uprobe 给每次调用增加的开销
#include <stdint.h>
#include <unistd.h>

#include "common.h"

static volatile uint64_t sink;

__attribute__((noinline)) uint64_t burn(uint64_t x, long iters)
{
    for (long i = 0; i < iters; i++) x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    return x;
}

// type：0 查询，1 写入，2 统计；size：请求的大小（字节）
__attribute__((noinline)) int handleRequest(int type, int size)
{
    long iters = 2000L + size * (type == 2 ? 40L : 4L);
    if (rand() % 1000 == 0) usleep(20000);  // 千分之一的请求要等 20 ms
    sink = burn((uint64_t)size, iters);
    return type == 1 && size >= 1024 ? -1 : 0;  // 大的写入请求会失败
}

__attribute__((noinline)) void empty(int i) { __asm__ volatile("" ::"r"(i)); }

int main(int argc, char** argv)
{
    CHECK(argc == 3);
    if (strcmp(argv[1], "serve") == 0) {
        double end = nowSeconds() + atof(argv[2]);
        long n = 0;
        printf("pid %d\n", getpid());
        fflush(stdout);
        while (nowSeconds() < end) {
            int r = rand() % 10;
            int type = r < 7 ? 0 : (r < 9 ? 1 : 2);
            int size = 64 << (rand() % 6);  // 64 到 2048 字节
            handleRequest(type, size);
            n++;
        }
        printf("处理了 %ld 个请求\n", n);
    } else if (strcmp(argv[1], "empty") == 0) {
        long n = atol(argv[2]);
        double t0 = nowSeconds();
        for (long i = 0; i < n; i++) empty((int)i);
        double t = nowSeconds() - t0;
        printf("调用 %ld 次：%.3f 秒，每次 %.1f ns\n", n, t, t / n * 1e9);
    } else {
        fprintf(stderr, "未知模式 %s\n", argv[1]);
        return 1;
    }
    return 0;
}
