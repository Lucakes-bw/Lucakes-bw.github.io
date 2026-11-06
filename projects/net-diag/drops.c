// 内核在哪里丢包：制造两种最常见的丢包
//   drops udprecv 端口 接收缓冲区字节数 每次处理的微秒数 秒数   接收 UDP，处理得比发送慢
//   drops udpsend 地址 端口 个数                               尽快发送指定个数的 UDP 数据报
//   drops listen 端口 backlog 秒数                             监听，但从不 accept
//   drops connect 地址 端口 个数                               同时发起多个连接，3 秒后统计结果
#include <fcntl.h>
#include <poll.h>

#include "common.h"

static void udpRecv(int port, int rcvbuf, int delayUs, double sec)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(fd >= 0);
    if (rcvbuf > 0) CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf) == 0);
    int real;
    socklen_t len = sizeof real;
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &real, &len);
    struct sockaddr_in a = makeAddr(NULL, port);
    CHECK(bind(fd, (struct sockaddr*)&a, sizeof a) == 0);
    printf("接收缓冲区 %d 字节，每个数据报处理 %d 微秒\n", real, delayUs);
    fflush(stdout);

    struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[2048];
    long got = 0;
    double end = nowSeconds() + sec;
    while (nowSeconds() < end) {
        if (recv(fd, buf, sizeof buf, 0) > 0) {
            got++;
            if (delayUs > 0) usleep((useconds_t)delayUs);
        }
    }
    printf("收到 %ld 个数据报\n", got);
}

static void udpSend(const char* ip, int port, long n)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(fd >= 0);
    struct sockaddr_in a = makeAddr(ip, port);
    char buf[100];
    memset(buf, 'u', sizeof buf);
    long sent = 0;
    double t0 = nowSeconds();
    for (long i = 0; i < n; i++)
        if (sendto(fd, buf, sizeof buf, 0, (struct sockaddr*)&a, sizeof a) == (ssize_t)sizeof buf) sent++;
    printf("发送了 %ld 个数据报，用了 %.3f 秒\n", sent, nowSeconds() - t0);
}

static void connectMany(const char* ip, int port, int n)
{
    int* fds = malloc(sizeof(int) * (size_t)n);
    CHECK(fds);
    struct sockaddr_in a = makeAddr(ip, port);
    for (int i = 0; i < n; i++) {
        fds[i] = socket(AF_INET, SOCK_STREAM, 0);
        CHECK(fds[i] >= 0);
        fcntl(fds[i], F_SETFL, O_NONBLOCK);
        int r = connect(fds[i], (struct sockaddr*)&a, sizeof a);
        CHECK(r == 0 || errno == EINPROGRESS);
    }
    usleep(3000000);
    int ok = 0, pending = 0, failed = 0;
    for (int i = 0; i < n; i++) {
        struct pollfd p = {.fd = fds[i], .events = POLLOUT};
        if (poll(&p, 1, 0) == 0) {
            pending++;
            continue;
        }
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &len);
        if (err == 0)
            ok++;
        else
            failed++;
    }
    printf("%d 个连接：3 秒后 已建立 %d，还在握手 %d，失败 %d\n", n, ok, pending, failed);
    fflush(stdout);
    usleep(30000000);  // 保持 30 秒，方便用 ss 观察
}

int main(int argc, char** argv)
{
    CHECK(argc >= 2);
    const char* m = argv[1];
    if (strcmp(m, "udprecv") == 0 && argc == 6) {
        udpRecv(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atof(argv[5]));
    } else if (strcmp(m, "udpsend") == 0 && argc == 5) {
        udpSend(argv[2], atoi(argv[3]), atol(argv[4]));
    } else if (strcmp(m, "listen") == 0 && argc == 5) {
        int fd = listenOn(atoi(argv[2]), atoi(argv[3]));
        printf("监听 %s 端口，backlog %s，从不 accept\n", argv[2], argv[3]);
        fflush(stdout);
        usleep((useconds_t)(atof(argv[4]) * 1e6));
        close(fd);
    } else if (strcmp(m, "connect") == 0 && argc == 5) {
        connectMany(argv[2], atoi(argv[3]), atoi(argv[4]));
    } else {
        fprintf(stderr, "用法见文件开头的注释\n");
        return 1;
    }
    return 0;
}
