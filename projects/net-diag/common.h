// 各个实验共用的小工具：计时、检查、建立连接
#ifndef NET_DIAG_COMMON_H
#define NET_DIAG_COMMON_H

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static inline double nowSeconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            fprintf(stderr, "%s:%d: 检查失败：%s（%s）\n", __FILE__, __LINE__, #cond, strerror(errno)); \
            exit(1);                                                                                 \
        }                                                                                            \
    } while (0)

static inline struct sockaddr_in makeAddr(const char* ip, int port)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = ip ? inet_addr(ip) : htonl(INADDR_ANY);
    return a;
}

// 监听指定的端口，返回监听套接字
static inline int listenOn(int port, int backlog)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = makeAddr(NULL, port);
    CHECK(bind(fd, (struct sockaddr*)&a, sizeof a) == 0);
    CHECK(listen(fd, backlog) == 0);
    return fd;
}

// 连接到 ip:port，失败返回 -1（errno 说明原因）
static inline int connectTo(const char* ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    struct sockaddr_in a = makeAddr(ip, port);
    if (connect(fd, (struct sockaddr*)&a, sizeof a) != 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

#endif
