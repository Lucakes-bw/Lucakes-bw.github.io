// 大块数据的传输：缓冲区大小怎样影响吞吐量；MTU 黑洞
//   bulk sink 端口 [接收缓冲区字节数]       接受连接，读完所有数据，打印吞吐量
//   bulk send 地址 端口 MB [发送缓冲区字节数] 尽快发送指定的数据量
//   bulk blob 端口                          每个连接读一行请求「字节数\n」，回复这么多字节后关闭
//   bulk fetch 地址 端口 字节数 超时秒数     请求指定字节数的回复，打印收到多少、用了多久
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>

#include "common.h"

static int bufInfo(int fd, int opt)
{
    int v;
    socklen_t len = sizeof v;
    getsockopt(fd, SOL_SOCKET, opt, &v, &len);
    return v;
}

static void sink(int port, int rcvbuf)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(lfd >= 0);
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    // 在监听套接字上设置，accept 得到的连接会继承
    if (rcvbuf > 0) CHECK(setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf) == 0);
    struct sockaddr_in a = makeAddr(NULL, port);
    CHECK(bind(lfd, (struct sockaddr*)&a, sizeof a) == 0);
    CHECK(listen(lfd, 16) == 0);
    static char buf[1 << 20];
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) continue;
        int before = bufInfo(fd, SO_RCVBUF);
        long total = 0;
        ssize_t r;
        double t0 = nowSeconds();
        while ((r = read(fd, buf, sizeof buf)) > 0) total += r;
        double t = nowSeconds() - t0;
        printf("收到 %.1f MB，%.2f 秒，%.1f Mbit/s；SO_RCVBUF 开始 %d，结束 %d\n", total / 1048576.0, t,
               total * 8 / t / 1e6, before, bufInfo(fd, SO_RCVBUF));
        fflush(stdout);
        close(fd);
    }
}

static void sendBulk(const char* ip, int port, long mb, int sndbuf)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    if (sndbuf > 0) CHECK(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf) == 0);
    struct sockaddr_in a = makeAddr(ip, port);
    CHECK(connect(fd, (struct sockaddr*)&a, sizeof a) == 0);
    static char buf[1 << 16];
    memset(buf, 'b', sizeof buf);
    long total = mb << 20, sent = 0;
    double t0 = nowSeconds();
    while (sent < total) {
        ssize_t w = write(fd, buf, sizeof buf);
        CHECK(w > 0);
        sent += w;
    }
    shutdown(fd, SHUT_WR);
    char c;
    while (read(fd, &c, 1) > 0) {
    }  // 等对方读完并关闭
    double t = nowSeconds() - t0;
    printf("发送 %ld MB，%.2f 秒，%.1f Mbit/s；SO_SNDBUF %d\n", mb, t, sent * 8 / t / 1e6, bufInfo(fd, SO_SNDBUF));
    close(fd);
}

static void* serveBlob(void* arg)
{
    int fd = (int)(long)arg;
    static char buf[1 << 16];
    memset(buf, 'x', sizeof buf);
    char req[64] = {0};
    ssize_t n = read(fd, req, sizeof req - 1);
    long want = n > 0 ? atol(req) : 0, sent = 0;
    while (sent < want) {
        long chunk = want - sent < (long)sizeof buf ? want - sent : (long)sizeof buf;
        ssize_t w = send(fd, buf, (size_t)chunk, MSG_NOSIGNAL);  // 对方断开时不要被 SIGPIPE 杀死
        if (w <= 0) break;
        sent += w;
    }
    close(fd);
    return NULL;
}

// 每个连接一个线程：一个卡住的连接不会影响别的连接
static void blob(int port)
{
    int lfd = listenOn(port, 16);
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) continue;
        pthread_t t;
        pthread_create(&t, NULL, serveBlob, (void*)(long)fd);
        pthread_detach(t);
    }
}

static void fetch(const char* ip, int port, long want, double timeout)
{
    int fd = connectTo(ip, port);
    CHECK(fd >= 0);
    double t0 = nowSeconds();
    dprintf(fd, "%ld\n", want);
    static char buf[1 << 16];
    long got = 0;
    for (;;) {
        double left = timeout - (nowSeconds() - t0);
        if (left <= 0) break;
        struct pollfd p = {.fd = fd, .events = POLLIN};
        if (poll(&p, 1, (int)(left * 1000)) <= 0) break;
        ssize_t r = read(fd, buf, sizeof buf);
        if (r <= 0) break;
        got += r;
    }
    struct tcp_info ti;
    socklen_t len = sizeof ti;
    getsockopt(fd, IPPROTO_TCP, TCP_INFO, &ti, &len);
    printf("请求 %ld 字节：收到 %ld 字节，%.2f 秒%s；本端 MSS %u\n", want, got, nowSeconds() - t0,
           got < want ? "（超时）" : "", ti.tcpi_rcv_mss);
    close(fd);
}

int main(int argc, char** argv)
{
    CHECK(argc >= 2);
    const char* m = argv[1];
    if (strcmp(m, "sink") == 0 && (argc == 3 || argc == 4)) {
        sink(atoi(argv[2]), argc == 4 ? atoi(argv[3]) : 0);
    } else if (strcmp(m, "send") == 0 && (argc == 5 || argc == 6)) {
        sendBulk(argv[2], atoi(argv[3]), atol(argv[4]), argc == 6 ? atoi(argv[5]) : 0);
    } else if (strcmp(m, "blob") == 0 && argc == 3) {
        blob(atoi(argv[2]));
    } else if (strcmp(m, "fetch") == 0 && argc == 6) {
        fetch(argv[2], atoi(argv[3]), atol(argv[4]), atof(argv[5]));
    } else {
        fprintf(stderr, "用法见文件开头的注释\n");
        return 1;
    }
    return 0;
}
