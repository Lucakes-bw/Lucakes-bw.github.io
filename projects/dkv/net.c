#define _GNU_SOURCE
#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int64_t nowUs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int64_t wallUs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void sleepMs(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000 };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

int parseAddr(const char *s, char *host, int *port)
{
    const char *c = strrchr(s, ':');
    if (!c || c == s || c - s > 63) return -1;
    memcpy(host, s, (size_t)(c - s));
    host[c - s] = 0;
    *port = atoi(c + 1);
    return *port > 0 ? 0 : -1;
}

static int resolve(const char *host, int port, struct sockaddr_in *sa)
{
    memset(sa, 0, sizeof *sa);
    sa->sin_family = AF_INET;
    sa->sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &sa->sin_addr) == 1) return 0;
    struct addrinfo hints = { .ai_family = AF_INET }, *res;
    if (getaddrinfo(host, NULL, &hints, &res) != 0) return -1;
    sa->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return 0;
}

int tcpListen(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(fd, 128) < 0) {
        perror("listen");
        exit(1);
    }
    return fd;
}

int tcpConnect(const char *addr, int timeoutMs)
{
    return tcpConnectFrom(addr, timeoutMs, NULL);
}

int tcpConnectFrom(const char *addr, int timeoutMs, const char *srcIp)
{
    char host[64];
    int port;
    struct sockaddr_in sa;
    if (parseAddr(addr, host, &port) < 0 || resolve(host, port, &sa) < 0) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srcIp && *srcIp) {
        struct sockaddr_in me = { .sin_family = AF_INET };
        inet_pton(AF_INET, srcIp, &me.sin_addr);
        if (bind(fd, (struct sockaddr *)&me, sizeof me) < 0) { close(fd); return -1; }
    }
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);              /* 非阻塞 connect，才能设超时 */
    int r = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (r < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    if (r < 0) {
        struct pollfd p = { fd, POLLOUT, 0 };
        int err = 0;
        socklen_t len = sizeof err;
        if (poll(&p, 1, timeoutMs) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err) {
            close(fd);
            return -1;
        }
    }
    fcntl(fd, F_SETFL, fl);
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);   /* 小请求不要等 Nagle */
    return fd;
}

int udpSocket(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (port > 0) {
        struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
        if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("udp bind"); exit(1); }
    }
    return fd;
}

int writeAll(int fd, const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, buf, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += w;
        n -= (size_t)w;
    }
    return 0;
}

void lrInit(LineReader *r, int fd)
{
    r->fd = fd;
    r->start = r->end = 0;
}

int lrHasLine(LineReader *r)
{
    return memchr(r->buf + r->start, '\n', r->end - r->start) != NULL;
}

int lrRead(LineReader *r, char *out, size_t cap, int timeoutMs)
{
    int64_t deadline = timeoutMs >= 0 ? nowUs() + (int64_t)timeoutMs * 1000 : 0;
    for (;;) {
        char *nl = memchr(r->buf + r->start, '\n', r->end - r->start);
        if (nl) {
            size_t len = (size_t)(nl - (r->buf + r->start));
            if (len >= cap) len = cap - 1;
            memcpy(out, r->buf + r->start, len);
            out[len] = 0;
            r->start = (size_t)(nl - r->buf) + 1;
            return (int)len;
        }
        if (r->start > 0) {                            /* 把剩下的半行挪到开头 */
            memmove(r->buf, r->buf + r->start, r->end - r->start);
            r->end -= r->start;
            r->start = 0;
        }
        if (r->end == sizeof r->buf) return -1;        /* 一行太长 */
        if (timeoutMs >= 0) {
            int64_t left = deadline - nowUs();
            if (left < 0) left = 0;                    /* 超时为 0：只看一眼有没有数据 */
            struct pollfd p = { r->fd, POLLIN, 0 };
            int pr = poll(&p, 1, (int)((left + 999) / 1000));
            if (pr == 0) return -2;
            if (pr < 0 && errno != EINTR) return -1;
            if (pr < 0) continue;
        }
        ssize_t n = recv(r->fd, r->buf + r->end, sizeof r->buf - r->end, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        r->end += (size_t)n;
    }
}
