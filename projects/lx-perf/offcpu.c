// off-CPU 分析：一个模拟的服务，每个请求要做三件事
//   1. parse：解析请求，纯计算，约 0.1 ms
//   2. lookup：持有一把全局的互斥锁，在共享的表里查数据，约 0.1 ms
//   3. load：从磁盘读 256 KB（O_DIRECT，不经过页缓存）
//   offcpu 文件 线程数 请求数 [选项]
// 选项是几个字母的组合：f 让 parse 快一倍；n 不读磁盘；l 不加锁
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#include "common.h"

#define READ_SIZE (256 << 10)

static int fd;
static off_t fileSize;
static long perThread;
static long parseIters = 60000;
static int doIO = 1, doLock = 1;
static pthread_mutex_t tableLock = PTHREAD_MUTEX_INITIALIZER;
static volatile uint64_t sink;

__attribute__((noinline)) uint64_t burn(uint64_t x, long iters)
{
    for (long i = 0; i < iters; i++) x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    return x;
}

__attribute__((noinline)) void parse(uint64_t id) { sink = burn(id, parseIters); }

__attribute__((noinline)) void lookup(uint64_t id)
{
    if (doLock) pthread_mutex_lock(&tableLock);
    sink = burn(id, 60000);
    if (doLock) pthread_mutex_unlock(&tableLock);
}

__attribute__((noinline)) void load(uint64_t id, void* buf)
{
    if (!doIO) return;
    off_t blocks = fileSize / READ_SIZE;
    off_t off = (off_t)(id * 2654435761ULL % (uint64_t)blocks) * READ_SIZE;
    CHECK(pread(fd, buf, READ_SIZE, off) == READ_SIZE);
}

static void* worker(void* arg)
{
    uint64_t base = (uint64_t)(uintptr_t)arg * 1000003;
    void* buf;
    CHECK(posix_memalign(&buf, 4096, READ_SIZE) == 0);
    for (long i = 0; i < perThread; i++) {
        uint64_t id = base + (uint64_t)i;
        parse(id);
        lookup(id);
        load(id, buf);
    }
    free(buf);
    return NULL;
}

int main(int argc, char** argv)
{
    CHECK(argc == 4 || argc == 5);
    fd = open(argv[1], O_RDONLY | O_DIRECT);
    CHECK(fd >= 0);
    fileSize = lseek(fd, 0, SEEK_END);
    int nThreads = atoi(argv[2]);
    long nReq = atol(argv[3]);
    const char* opt = argc == 5 ? argv[4] : "";
    if (strchr(opt, 'f')) parseIters /= 2;
    if (strchr(opt, 'n')) doIO = 0;
    if (strchr(opt, 'l')) doLock = 0;
    CHECK(nThreads > 0 && nThreads <= 64);
    perThread = nReq / nThreads;

    pthread_t t[64];
    double t0 = nowSeconds();
    for (int i = 0; i < nThreads; i++) pthread_create(&t[i], NULL, worker, (void*)(uintptr_t)i);
    for (int i = 0; i < nThreads; i++) pthread_join(t[i], NULL);
    double wall = nowSeconds() - t0;
    double user, sys;
    long vol, invol;
    cpuSeconds(&user, &sys);
    ctxSwitches(&vol, &invol);
    printf("%d 个线程处理 %ld 个请求：%.2f 秒，每秒 %.0f 个；CPU 用户态 %.2f 秒、内核态 %.2f 秒；"
           "主动切换 %ld 次，被抢占 %ld 次\n",
           nThreads, perThread * nThreads, wall, perThread * nThreads / wall, user, sys, vol, invol);
    return 0;
}
