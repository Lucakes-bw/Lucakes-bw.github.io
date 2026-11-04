// 页缓存与写回：write 只写进页缓存；脏页多了以后写入被限速；冷读和热读；O_DIRECT；把文件从页缓存中赶出去
//   pagecache write 文件 MB        写一个文件，观察 Dirty，再 fsync
//   pagecache stream 文件 MB       连续写一个大文件，每 128 MB 报告一次速度和 Dirty
//   pagecache read 文件            读一遍，报告速度
//   pagecache direct 文件          用 O_DIRECT 读一遍
//   pagecache cached 文件          文件有多少页在页缓存中（mincore）
//   pagecache evict 文件           posix_fadvise(DONTNEED) 把文件赶出页缓存
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"

#define MB (1024L * 1024)
#define CHUNK (4 * MB)

static char* alignedBuffer(void)
{
    void* p;
    CHECK(posix_memalign(&p, 4096, CHUNK) == 0);   // O_DIRECT 要求缓冲区按块大小对齐
    memset(p, 'x', CHUNK);
    return p;
}

static double dirtyMB(void) { return meminfoKB("Dirty") / 1024.0; }
static double writebackMB(void) { return meminfoKB("Writeback") / 1024.0; }

static void runWrite(const char* path, long mb)
{
    char* buf = alignedBuffer();
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    printf("写之前：Dirty %.1f MB\n", dirtyMB());
    double t0 = nowSeconds();
    for (long done = 0; done < mb * MB; done += CHUNK) CHECK(write(fd, buf, CHUNK) == CHUNK);
    double tw = nowSeconds() - t0;
    printf("write %ld MB：%.3f 秒（%.0f MB/s），之后 Dirty %.1f MB\n", mb, tw, mb / tw, dirtyMB());
    t0 = nowSeconds();
    CHECK(fsync(fd) == 0);
    double tf = nowSeconds() - t0;
    printf("fsync：%.3f 秒，之后 Dirty %.1f MB\n", tf, dirtyMB());
    close(fd);
    free(buf);
}

static void runStream(const char* path, long mb)
{
    char* buf = alignedBuffer();
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    double t0 = nowSeconds(), last = t0;
    for (long done = 0; done < mb * MB;) {
        CHECK(write(fd, buf, CHUNK) == CHUNK);
        done += CHUNK;
        if (done % (128 * MB) == 0) {
            double now = nowSeconds();
            printf("  已写 %5ld MB：这 128 MB 用了 %6.3f 秒（%5.0f MB/s），Dirty %6.1f MB，Writeback %5.1f MB\n", done / MB,
                   now - last, 128 / (now - last), dirtyMB(), writebackMB());
            last = now;
        }
    }
    printf("  一共 %.2f 秒\n", nowSeconds() - t0);
    close(fd);
    free(buf);
}

static void runRead(const char* path, int direct)
{
    char* buf = alignedBuffer();
    int fd = open(path, O_RDONLY | (direct ? O_DIRECT : 0));
    CHECK(fd >= 0);
    long total = 0;
    ssize_t n;
    double t0 = nowSeconds();
    while ((n = read(fd, buf, CHUNK)) > 0) total += n;
    CHECK(n == 0);
    double t = nowSeconds() - t0;
    printf("%s读 %ld MB：%.3f 秒（%.0f MB/s）\n", direct ? "O_DIRECT " : "", total / MB, t, total / MB / t);
    close(fd);
    free(buf);
}

static void runCached(const char* path)
{
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    struct stat st;
    fstat(fd, &st);
    void* p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    CHECK(p != MAP_FAILED);
    long pages = (st.st_size + 4095) / 4096, in = 0;
    unsigned char* vec = malloc((size_t)pages);
    CHECK(mincore(p, (size_t)st.st_size, vec) == 0);   // 每一页是否在内存中
    for (long i = 0; i < pages; ++i) in += vec[i] & 1;
    printf("页缓存中有 %ld / %ld 页（%.0f%%）\n", in, pages, in * 100.0 / pages);
    munmap(p, (size_t)st.st_size);
    close(fd);
    free(vec);
}

static void runEvict(const char* path)
{
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    CHECK(posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0);   // 告诉内核：这个文件的缓存不再需要
    close(fd);
    printf("posix_fadvise(POSIX_FADV_DONTNEED) 完成\n");
}

int main(int argc, char** argv)
{
    CHECK(argc >= 3);
    const char* mode = argv[1];
    const char* path = argv[2];
    if (strcmp(mode, "write") == 0) runWrite(path, argc > 3 ? atol(argv[3]) : 256);
    else if (strcmp(mode, "stream") == 0) runStream(path, argc > 3 ? atol(argv[3]) : 1536);
    else if (strcmp(mode, "read") == 0) runRead(path, 0);
    else if (strcmp(mode, "direct") == 0) runRead(path, 1);
    else if (strcmp(mode, "cached") == 0) runCached(path);
    else if (strcmp(mode, "evict") == 0) runEvict(path);
    return 0;
}
