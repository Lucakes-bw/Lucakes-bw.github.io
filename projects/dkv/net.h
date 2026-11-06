/* dkv 的网络小工具：TCP 连接、按行读写、计时 */
#ifndef DKV_NET_H
#define DKV_NET_H

#include <stddef.h>
#include <stdint.h>

int64_t nowUs(void);                                   /* CLOCK_MONOTONIC，微秒 */
int64_t wallUs(void);                                  /* CLOCK_REALTIME，微秒 */
void sleepMs(int ms);

int parseAddr(const char *s, char *host, int *port);   /* "host:port" */
int tcpListen(int port);
int tcpConnect(const char *addr, int timeoutMs);       /* 失败返回 -1 */
int tcpConnectFrom(const char *addr, int timeoutMs, const char *srcIp);   /* 指定本端 IP */
int udpSocket(int port);                               /* port 为 0 时不绑定 */

int writeAll(int fd, const char *buf, size_t n);

/* 带缓冲的按行读取。每条连接一个 */
typedef struct {
    int fd;
    char buf[65536];
    size_t start, end;
} LineReader;

void lrInit(LineReader *r, int fd);
/* 读一行（去掉换行符）到 out。返回长度；超时返回 -2；连接关闭或出错返回 -1。
 * timeoutMs < 0 表示一直等 */
int lrRead(LineReader *r, char *out, size_t cap, int timeoutMs);
/* 缓冲区里是否已经有完整的一行 */
int lrHasLine(LineReader *r);

#endif
