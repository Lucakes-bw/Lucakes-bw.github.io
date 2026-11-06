// 容器网络的开销：TCP 上一来一回 1 字节要多久
//   pingpong server 端口           收到什么就回复什么
//   pingpong client 地址 端口 次数  发 1 字节、收 1 字节，重复指定的次数，打印平均每次往返的时间
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "common.h"

int main(int argc, char** argv)
{
    CHECK(argc >= 3);
    int one = 1;
    if (strcmp(argv[1], "server") == 0) {
        int lfd = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((unsigned short)atoi(argv[2]))};
        CHECK(bind(lfd, (struct sockaddr*)&a, sizeof a) == 0);
        CHECK(listen(lfd, 16) == 0);
        for (;;) {
            int fd = accept(lfd, NULL, NULL);
            if (fd < 0) continue;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            char c;
            while (read(fd, &c, 1) == 1 && write(fd, &c, 1) == 1) {
            }
            close(fd);
        }
    }
    CHECK(argc == 5);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((unsigned short)atoi(argv[3]))};
    a.sin_addr.s_addr = inet_addr(argv[2]);
    CHECK(connect(fd, (struct sockaddr*)&a, sizeof a) == 0);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    long n = atol(argv[4]);
    char c = 'p';
    double t0 = nowSeconds();
    for (long i = 0; i < n; i++) CHECK(write(fd, &c, 1) == 1 && read(fd, &c, 1) == 1);
    printf("%ld 次往返，平均每次 %.1f 微秒\n", n, (nowSeconds() - t0) / n * 1e6);
    return 0;
}
