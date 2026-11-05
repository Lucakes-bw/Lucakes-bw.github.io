// 短连接与端口：客户端不停地建立、关闭连接，本地端口会不会用完
//   churn server 端口               每收到 1 字节就回复 1 字节；读到对方关闭后再关闭
//                                   （客户端先关，TIME_WAIT 留在客户端）
//   churn client 地址 端口 次数      短连接：每次请求都建立连接、发 1 字节、收 1 字节、关闭，遇到第一个错误就停下
//   churn reuse 地址 端口 次数       长连接：一个连接上发完所有请求
#include <netinet/tcp.h>

#include "common.h"

static void server(int port)
{
    int lfd = listenOn(port, 4096);
    char buf[256];
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        ssize_t r;
        while ((r = read(fd, buf, sizeof buf)) > 0)
            if (write(fd, buf, (size_t)r) != r) break;
        close(fd);
    }
}

// 发 1 字节、收 1 字节
static int request(int fd)
{
    char c = 'q';
    return write(fd, &c, 1) == 1 && read(fd, &c, 1) == 1 ? 0 : -1;
}

static void client(const char* ip, int port, long n)
{
    double t0 = nowSeconds(), last = t0;
    long i;
    for (i = 0; i < n; i++) {
        int fd = connectTo(ip, port);
        if (fd < 0) {
            printf("第 %ld 次连接失败（%.2f 秒）：%s\n", i + 1, nowSeconds() - t0, strerror(errno));
            break;
        }
        CHECK(request(fd) == 0);
        close(fd);
        if ((i + 1) % 5000 == 0) {
            double now = nowSeconds();
            printf("%6ld 次，%.2f 秒（这 5000 次每秒 %.0f 次）\n", i + 1, now - t0, 5000 / (now - last));
            fflush(stdout);
            last = now;
        }
    }
    double t = nowSeconds() - t0;
    printf("短连接：成功 %ld 次，%.2f 秒，每秒 %.0f 次\n", i, t, i / t);
}

static void reuse(const char* ip, int port, long n)
{
    int fd = connectTo(ip, port);
    CHECK(fd >= 0);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    double t0 = nowSeconds();
    for (long i = 0; i < n; i++) CHECK(request(fd) == 0);
    double t = nowSeconds() - t0;
    printf("长连接：%ld 次请求，%.2f 秒，每秒 %.0f 次\n", n, t, n / t);
    close(fd);
}

int main(int argc, char** argv)
{
    if (argc == 3 && strcmp(argv[1], "server") == 0) {
        server(atoi(argv[2]));
    } else if (argc == 5 && strcmp(argv[1], "client") == 0) {
        client(argv[2], atoi(argv[3]), atol(argv[4]));
    } else if (argc == 5 && strcmp(argv[1], "reuse") == 0) {
        reuse(argv[2], atoi(argv[3]), atol(argv[4]));
    } else {
        fprintf(stderr, "用法见文件开头的注释\n");
        return 1;
    }
    return 0;
}
