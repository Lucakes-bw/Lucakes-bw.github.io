// strace 与 ltrace：被跟踪的程序会慢多少；用 strace 找出慢在哪个系统调用
//   syscost getppid 次数    连续执行 getppid 系统调用，打印每次的平均耗时
//   syscost malloc 次数     连续 malloc(32) + free，打印每次的平均耗时
//   syscost startup 目录    模拟一个启动很慢的程序：读配置、写日志并 fsync、等一个不回应的服务
//   syscost stuck 管道文件   打开一个命名管道读数据，没有人写就一直等着
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "common.h"

static void readConfig(const char* path)
{
    char buf[4096];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    while (read(fd, buf, sizeof buf) > 0) {
    }
    close(fd);
}

static void writeLog(const char* dir, size_t mb)
{
    char path[512];
    snprintf(path, sizeof path, "%s/app.log", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    static char block[1 << 20];
    memset(block, 'a', sizeof block);
    for (size_t i = 0; i < mb; i++) CHECK(write(fd, block, sizeof block) == (ssize_t)sizeof block);
    CHECK(fsync(fd) == 0);
    close(fd);
}

// 向一个不回应的「服务」发请求，最多等 1 秒
static void askService(void)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    CHECK(write(sv[0], "ping", 4) == 4);
    struct pollfd p = {.fd = sv[0], .events = POLLIN};
    poll(&p, 1, 1000);
    close(sv[0]);
    close(sv[1]);
}

int main(int argc, char** argv)
{
    CHECK(argc == 3);
    const char* mode = argv[1];
    if (strcmp(mode, "getppid") == 0) {
        long n = atol(argv[2]);
        double t0 = nowSeconds();
        for (long i = 0; i < n; i++) syscall(SYS_getppid);
        double t = nowSeconds() - t0;
        printf("getppid %ld 次：%.3f 秒，每次 %.0f ns\n", n, t, t / n * 1e9);
    } else if (strcmp(mode, "malloc") == 0) {
        long n = atol(argv[2]);
        double t0 = nowSeconds();
        for (long i = 0; i < n; i++) {
            void* volatile p = malloc(32);
            free(p);
        }
        double t = nowSeconds() - t0;
        printf("malloc+free %ld 次：%.3f 秒，每次 %.0f ns\n", n, t, t / n * 1e9);
    } else if (strcmp(mode, "startup") == 0) {
        double t0 = nowSeconds();
        for (int i = 0; i < 20; i++) readConfig("/etc/services");
        writeLog(argv[2], 128);
        askService();
        printf("启动完成，用了 %.2f 秒\n", nowSeconds() - t0);
    } else if (strcmp(mode, "stuck") == 0) {
        int fd = open(argv[2], O_RDONLY);
        CHECK(fd >= 0);
        char buf[64];
        ssize_t r = read(fd, buf, sizeof buf);
        printf("读到 %zd 字节\n", r);
    } else {
        fprintf(stderr, "未知模式 %s\n", mode);
        return 1;
    }
    return 0;
}
