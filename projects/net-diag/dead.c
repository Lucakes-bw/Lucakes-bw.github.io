// 对端死了，多久才能发现：keepalive、重传超时与 TCP_USER_TIMEOUT
//   dead server 端口
//       接受连接，读到的数据全部丢掉，从不主动发送
//   dead client 地址 端口 idle|send [选项...]
//       idle：连上以后什么也不发，阻塞在 recv 上
//       send：每秒发送 100 字节
//       选项：ka=空闲秒数,间隔秒数,次数   打开 keepalive
//             ut=毫秒                    设置 TCP_USER_TIMEOUT
//       连接出错时打印从连上开始经过的时间和错误原因
#include <netinet/tcp.h>
#include <pthread.h>

#include "common.h"

static void* drain(void* arg)
{
    int fd = (int)(long)arg;
    char buf[4096];
    while (read(fd, buf, sizeof buf) > 0) {
    }
    close(fd);
    return NULL;
}

static void server(int port)
{
    int lfd = listenOn(port, 64);
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) continue;
        pthread_t t;
        pthread_create(&t, NULL, drain, (void*)(long)fd);
        pthread_detach(t);
    }
}

static void client(const char* ip, int port, const char* mode, int nopt, char** opts)
{
    int fd = connectTo(ip, port);
    CHECK(fd >= 0);
    for (int i = 0; i < nopt; i++) {
        int idle, intvl, cnt, ms;
        if (sscanf(opts[i], "ka=%d,%d,%d", &idle, &intvl, &cnt) == 3) {
            int one = 1;
            CHECK(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one) == 0);
            CHECK(setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle) == 0);
            CHECK(setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl) == 0);
            CHECK(setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt) == 0);
        } else if (sscanf(opts[i], "ut=%d", &ms) == 1) {
            CHECK(setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &ms, sizeof ms) == 0);
        } else {
            fprintf(stderr, "不认识的选项 %s\n", opts[i]);
            exit(1);
        }
    }
    double t0 = nowSeconds();
    printf("已连接，模式 %s\n", mode);
    fflush(stdout);
    if (strcmp(mode, "idle") == 0) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        printf("%.1f 秒后 recv 返回 %zd：%s\n", nowSeconds() - t0, r, r < 0 ? strerror(errno) : "对方关闭了连接");
    } else {
        char buf[100];
        memset(buf, 's', sizeof buf);
        for (long i = 0;; i++) {
            if (send(fd, buf, sizeof buf, MSG_NOSIGNAL) < 0) {
                printf("%.1f 秒后 send 失败：%s（已经成功调用 send %ld 次）\n", nowSeconds() - t0, strerror(errno), i);
                break;
            }
            sleep(1);
        }
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && strcmp(argv[1], "server") == 0) {
        server(atoi(argv[2]));
    } else if (argc >= 5 && strcmp(argv[1], "client") == 0) {
        client(argv[2], atoi(argv[3]), argv[4], argc - 5, argv + 5);
    } else {
        fprintf(stderr, "用法见文件开头的注释\n");
        return 1;
    }
    return 0;
}
