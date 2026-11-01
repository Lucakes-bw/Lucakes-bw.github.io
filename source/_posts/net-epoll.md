---
title: IO 多路复用
date: 2026-10-24 16:45:00
updated: 2026-10-24 16:45:00
categories:
  - 计算机网络
  - Socket编程
tags:
  - 计算机网络
  - Socket
  - epoll
order: 4
description: 不必为每个连接准备一个进程或线程：IO 多路复用让一个线程同时等待所有的连接，哪个有数据就处理哪个。本文先用 select 改写回声服务器，1000 个连接正常，1100 个连接时程序直接崩溃：bit out of range 0 - FD_SETSIZE；再用 epoll 实现非阻塞的回声服务器，10000 个连接只用 1 个线程、约 40 MB 内存，而每个连接一个线程的版本要 10001 个线程、120 MB。用 strace -c 统计，还发现了自己代码中一处多余的系统调用。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 IO 多路复用的思路；会用 select 编写服务器，了解它的局限；会用 epoll 编写非阻塞的服务器，正确处理部分写出和 `EAGAIN`；理解水平触发和边缘触发；理解事件驱动的服务器为什么能用很少的资源处理大量的连接。
>
> **前置知识**：上一篇的多进程、多线程服务器和 C10K 问题；《文件描述符与系统调用》。
>
> **实验环境**：Ubuntu 24.04 容器（2 个 CPU 核），GCC 13.3，Linux 内核 6.8。和上一篇相同的压测程序和网络。

## 1. 思路

上一篇的服务器用阻塞的 `read` 等待数据，一个执行流只能等一个连接，所以需要很多个执行流。**IO 多路复用**换一种方式：把所有的连接交给内核，用**一个**系统调用同时等待它们，内核告诉程序「这几个连接现在可以读了」，程序再去读这几个，此时 `read` 不会阻塞。一个线程就能轮流照顾成千上万个连接。

Linux 上有三种接口：`select`（最古老，所有系统都有）、`poll`、`epoll`（Linux 特有，效率最高）。

## 2. select

```c
// echo_select.c：用 select 在一个线程中同时等待所有的连接
#include <sys/select.h>
#include "common.h"

int main(void)
{
    int lfd = listen_on(9000);
    fd_set all;                                  // 所有需要等待的描述符
    FD_ZERO(&all);
    FD_SET(lfd, &all);
    int maxfd = lfd;
    for (;;) {
        fd_set ready = all;                      // select 会修改集合，每次都要复制一份
        if (select(maxfd + 1, &ready, NULL, NULL, NULL) == -1) {
            perror("select");
            return 1;
        }
        for (int fd = 0; fd <= maxfd; fd++) {    // 逐个检查哪些描述符就绪了
            if (!FD_ISSET(fd, &ready)) continue;
            if (fd == lfd) {                     // 监听套接字可读：有新连接
                int cfd = accept(lfd, NULL, NULL);
                if (cfd == -1) continue;
                FD_SET(cfd, &all);
                if (cfd > maxfd) maxfd = cfd;
            } else {                             // 连接可读：有数据，或者对方关闭了
                char buf[4096];
                ssize_t n = read(fd, buf, sizeof buf);
                if (n <= 0) {
                    close(fd);
                    FD_CLR(fd, &all);
                } else if (write(fd, buf, n) != n) {   // 简化：假设能一次写完
                    close(fd);
                    FD_CLR(fd, &all);
                }
            }
        }
    }
}
```

- `fd_set` 是一个位图，每个描述符对应一位，`FD_SET` 把它置 1；
- `select` 阻塞，直到集合中有描述符就绪，然后**修改**集合，只留下就绪的那些。所以每次调用前都要从 `all` 复制一份；
- 返回后，程序要**逐个检查**每个描述符是否就绪；
- 监听套接字「可读」表示有新的连接可以 accept。

### 2.1 select 的局限

用 1000 个连接和 1100 个连接分别压测（服务器的描述符上限已经用 `ulimit -n` 放宽）：

```
1000 个连接：建立用时 0.039 秒；10000 次往返用时 0.785 秒，平均每次 78.5 微秒
  bench exit=0
  服务器：运行中
  bench exit=1
Aborted (core dumped) ip netns exec s ./echo_select
  服务器：已退出 *** bit out of range 0 - FD_SETSIZE on fd_set ***: terminated
```

1000 个连接一切正常；1100 个连接时，服务器**崩溃**了。`fd_set` 的大小是固定的 `FD_SETSIZE`，即 **1024** 位，描述符的值达到 1024 时，`FD_SET` 就越界了。glibc 的安全检查（C 语言专栏《字符串》中介绍过的 `_FORTIFY_SOURCE`，Ubuntu 默认开启）发现了这个越界，直接终止了程序；没有这个检查的话，它会悄悄地破坏内存。

select 的其他问题：

- 每次调用都要把整个集合从用户空间复制到内核，内核再逐个检查所有的描述符；
- 返回后，程序也要从 0 到 maxfd 逐个检查。连接越多，每次调用越慢，即使只有一个连接有数据。

`poll` 用一个数组代替位图，去掉了 1024 的限制，但每次调用仍然要传递并检查所有的描述符。

## 3. epoll

`epoll` 把「要等待哪些描述符」保存在内核中，只需要告诉它一次；每次等待时，内核只返回**就绪的**那些：

| 函数 | 作用 |
|---|---|
| `epoll_create1(0)` | 创建一个 epoll 实例，返回一个描述符 |
| `epoll_ctl(ep, 操作, fd, &事件)` | 添加（`ADD`）、修改（`MOD`）、删除（`DEL`）要等待的描述符和事件 |
| `epoll_wait(ep, events, 最多几个, 超时)` | 等待，返回就绪的描述符的数量，它们被填入 `events` 数组 |

`struct epoll_event` 的 `data` 字段可以存放一个任意的指针，就绪时原样返回，程序通常用它指向这个连接自己的数据结构。

<figure class="diagram">
<svg viewBox="0 0 700 246" role="img" aria-label="select 与 epoll：select 每次调用都要把 1024 位的整个描述符集合复制进内核，内核逐个检查，返回后程序还要逐个检查，描述符达到 1024 就越界；epoll 用 epoll_ctl 把要关注的描述符登记在内核的关注列表中，数据到达时内核把连接放到就绪列表，epoll_wait 只返回就绪的描述符">
  <defs><marker id="ep-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/></marker><marker id="ep-g" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20" y="22" text-anchor="start" style="fill:var(--warn);font-size:12px">select：每次调用都要整个集合走一遍</text><rect x="20" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="30.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="40" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="50.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="60" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="70.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="80" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="90.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="100" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="110.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="120" y="34" width="20" height="22" rx="0" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="130.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="140" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="150.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="160" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="170.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="180" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="190.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="200" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="210.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="220" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="230.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="240" y="34" width="20" height="22" rx="0" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="250.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="260" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="270.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="280" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="290.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="300" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="310.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><rect x="320" y="34" width="20" height="22" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="330.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:13px"></text><text x="345" y="50" text-anchor="start" style="fill:var(--muted);font-size:11px">… 共 1024 位（FD_SETSIZE）</text><text x="20" y="76" text-anchor="start" style="fill:var(--muted);font-size:11px">① 程序把整个位图复制进内核　② 内核逐个检查　③ 返回后程序再从 0 到 maxfd 逐个检查</text><text x="20" y="92" text-anchor="start" style="fill:var(--danger);font-size:11px">连接越多每次越慢；描述符达到 1024 时直接越界（实验中 1100 个连接就崩溃了）</text><line x1="20" y1="108" x2="680" y2="108" style="stroke:var(--border);stroke-dasharray:4 4"/><text x="20" y="130" text-anchor="start" style="fill:var(--accent);font-size:12px">epoll：关心哪些描述符只告诉内核一次，每次只取回就绪的</text><rect x="20" y="142" width="300" height="70" rx="8" style="fill:none;stroke:var(--link);stroke-width:1.5;stroke-dasharray:5 3"/><text x="30" y="158" text-anchor="start" style="fill:var(--link);font-size:11px">内核中的 epoll 实例</text><rect x="30" y="166" width="150" height="36" rx="0" style="fill:var(--panel-2);stroke:var(--link);stroke-width:1.5"/><text x="105.0" y="189.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">关注列表（红黑树）</text><rect x="196" y="166" width="114" height="36" rx="0" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="253.0" y="189.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">就绪列表</text><line x1="180" y1="184" x2="192" y2="184" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#ep-a)"/><rect x="400" y="142" width="130" height="30" rx="0" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="465.0" y="162.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">epoll_ctl(ADD)</text><line x1="400" y1="157" x2="184" y2="172" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ep-g)"/><text x="540" y="162" text-anchor="start" style="fill:var(--muted);font-size:11px">连接建立时一次</text><rect x="400" y="182" width="130" height="30" rx="0" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="465.0" y="202.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">epoll_wait</text><line x1="314" y1="190" x2="396" y2="196" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#ep-a)"/><text x="540" y="202" text-anchor="start" style="fill:var(--accent);font-size:11px">只返回就绪的 fd 5、11</text><text x="20" y="234" text-anchor="start" style="fill:var(--muted);font-size:11px">数据到达时，内核把那个连接挂到就绪列表上；有多少连接都不影响每次等待的代价</text>
  </g>
</svg>
</figure>

```c
// echo_epoll.c：用 epoll 在一个线程中处理所有的连接
#define _GNU_SOURCE                              // accept4 需要它
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "common.h"

#define MAX_EVENTS 256

struct conn {                                    // 每个连接一个，记录还没写出去的数据
    int fd;
    char pending[4096];
    size_t len;
};

static void set_nonblock(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static void close_conn(int ep, struct conn *c)
{
    epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    free(c);
}

// 尽量把 pending 中的数据写出去；返回 -1 表示连接出错
static int flush(int ep, struct conn *c)
{
    while (c->len > 0) {
        ssize_t w = write(c->fd, c->pending, c->len);
        if (w == -1) {
            if (errno != EAGAIN) return -1;
            struct epoll_event ev = {.events = EPOLLIN | EPOLLOUT, .data.ptr = c};
            epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &ev);   // 发送缓冲区满了：等它可写时再继续
            return 0;
        }
        memmove(c->pending, c->pending + w, c->len - w);
        c->len -= w;
    }
    struct epoll_event ev = {.events = EPOLLIN, .data.ptr = c};
    epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &ev);           // 写完了，只关心可读
    return 0;
}

int main(void)
{
    int lfd = listen_on(9000);
    set_nonblock(lfd);
    int ep = epoll_create1(0);                          // 创建一个 epoll 实例
    struct epoll_event ev = {.events = EPOLLIN, .data.ptr = NULL};   // data 为 NULL 表示监听套接字
    epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev);

    struct epoll_event events[MAX_EVENTS];
    for (;;) {
        int n = epoll_wait(ep, events, MAX_EVENTS, -1);  // 只返回就绪的描述符
        for (int i = 0; i < n; i++) {
            struct conn *c = events[i].data.ptr;
            if (c == NULL) {                             // 新连接：一直 accept，直到没有了
                int cfd;
                while ((cfd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK)) != -1) {
                    struct conn *nc = calloc(1, sizeof *nc);
                    nc->fd = cfd;
                    struct epoll_event cev = {.events = EPOLLIN, .data.ptr = nc};
                    epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev);
                }
                continue;
            }
            if (events[i].events & EPOLLOUT) {           // 之前没写完的，现在可以继续写
                if (flush(ep, c) == -1) close_conn(ep, c);
                continue;
            }
            if (c->len > 0) continue;                    // 还有数据没写完，先不读新的
            ssize_t r = read(c->fd, c->pending, sizeof c->pending);
            if (r == 0 || (r == -1 && errno != EAGAIN)) {   // 对方关闭，或者出错
                close_conn(ep, c);
                continue;
            }
            if (r > 0) {
                c->len = r;
                if (flush(ep, c) == -1) close_conn(ep, c);
            }
        }
    }
}
```

### 3.1 非阻塞

所有的描述符都设成了**非阻塞**的（`O_NONBLOCK`、`accept4` 的 `SOCK_NONBLOCK`）：没有数据可读、发送缓冲区已满时，`read`/`write` 不等待，而是返回 -1，`errno` 为 `EAGAIN`。这是事件驱动的程序必须遵守的规则：**主循环中的任何一个调用都不能阻塞**，否则一个慢的连接会让所有的连接都停下来，又回到了上一篇的单客户端服务器。

### 3.2 部分写出

阻塞的 `write` 会一直等到全部写完；非阻塞的 `write` 可能只写出一部分（发送缓冲区快满了，对方读得慢，第 2.4 篇的流量控制）。程序不能等，只能把没写完的数据保存在这个连接的 `pending` 中，用 `EPOLL_CTL_MOD` 增加关注 **EPOLLOUT**（可写）事件，等内核通知「现在可以写了」再继续。写完后再改回只关注可读。对于回声服务器，这时还要暂停读取这个连接（`if (c->len > 0) continue;`），否则客户端一直发、却不读回声，服务器的内存就会无限增长，这也是一种流量控制。

select 版本省略了这些处理（假设一次能写完），在真实的服务器中这是不可以的。

### 3.3 水平触发和边缘触发

epoll 有两种通知方式：

- **水平触发**（默认）：只要描述符处于可读的状态（接收缓冲区中有数据），每次 `epoll_wait` 都会报告它。这个服务器用的就是这种方式，每次只读一部分也没关系，下次还会通知；
- **边缘触发**（`EPOLLET`）：只在状态**变化**时（新数据到达）通知一次。程序必须一直读到 `EAGAIN`，把数据全部读完，否则剩下的数据不会再有通知，连接就「卡住」了。边缘触发减少了重复的通知，但更容易写错。

## 4. 对比

### 4.1 资源

1000 个和 10000 个连接，保持连接时测量服务器（PSS 的含义见上一篇）：

```
1000 个连接：建立用时 0.041 秒；10000 次往返用时 0.435 秒，平均每次 43.5 微秒
  echo_epoll：线程 1 个，描述符 1005 个，PSS 4.2 MB
  echo_epoll：线程 1 个，描述符 10005 个，PSS 39.6 MB
10000 个连接：建立用时 0.341 秒；100000 次往返用时 4.301 秒，平均每次 43.0 微秒
```

和上一篇的结果放在一起：

| 服务器 | 1000 个连接 | 10000 个连接 |
|---|---|---|
| 每个连接一个进程 | 1001 个进程，27 MB | 10001 个进程，274 MB |
| 每个连接一个线程 | 1001 个线程，12 MB | 10001 个线程，120 MB |
| epoll | **1 个线程**，4 MB | **1 个线程**，40 MB |

<figure class="diagram">
<svg viewBox="0 0 700 196" role="img" aria-label="服务器保持连接时的内存（PSS）：每个连接一个进程，1000 个连接 27 MB、10000 个连接 274 MB；每个连接一个线程，12 MB 和 120 MB；epoll 只用一个线程，4 MB 和 40 MB">
  <defs></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="158" y="54" text-anchor="end" style="fill:var(--fg);font-size:12px">每个连接一个进程</text><rect x="170" y="30" width="29.7" height="16" style="fill:var(--danger);opacity:.45"/><text x="205.7" y="43" text-anchor="start" style="fill:var(--muted);font-size:11px">1000 个连接 27 MB</text><rect x="170" y="50" width="301.4" height="16" style="fill:var(--danger);opacity:.8"/><text x="477.40000000000003" y="63" text-anchor="start" style="fill:var(--danger);font-size:11px">10000 个连接 274 MB（10001 个进程）</text><text x="158" y="106" text-anchor="end" style="fill:var(--fg);font-size:12px">每个连接一个线程</text><rect x="170" y="82" width="13.2" height="16" style="fill:var(--warn);opacity:.45"/><text x="189.2" y="95" text-anchor="start" style="fill:var(--muted);font-size:11px">1000 个连接 12 MB</text><rect x="170" y="102" width="132.0" height="16" style="fill:var(--warn);opacity:.8"/><text x="308.0" y="115" text-anchor="start" style="fill:var(--warn);font-size:11px">10000 个连接 120 MB（10001 个线程）</text><text x="158" y="158" text-anchor="end" style="fill:var(--fg);font-size:12px">epoll</text><rect x="170" y="134" width="4.4" height="16" style="fill:var(--accent);opacity:.45"/><text x="180.4" y="147" text-anchor="start" style="fill:var(--muted);font-size:11px">1000 个连接 4 MB</text><rect x="170" y="154" width="44.0" height="16" style="fill:var(--accent);opacity:.8"/><text x="220.0" y="167" text-anchor="start" style="fill:var(--accent);font-size:11px">10000 个连接 40 MB（1 个线程）</text>
  </g>
</svg>
</figure>

epoll 版本的内存主要是每个连接的 `struct conn`（4 KB 多的缓冲区）和内核中的套接字，没有任何进程或线程的开销，也不受 `threads-max` 的限制。每次往返的时间三者相当（约 45 微秒），这个压测中同一时刻只有一个连接在活动，体现不出调度上的差别；在大量连接同时活跃的场景中，几千个线程的上下文切换会成为负担，而 epoll 只有一个线程，没有这个问题。

（建立 10000 个连接的时间在重复实验中波动很大，epoll 和多线程版本都在 0.3～5 秒之间，主要取决于客户端寻找可用本地端口的情况，上一篇已经说明，这里不做比较。）

### 4.2 系统调用

用 `strace -c` 统计 epoll 服务器处理 1000 个连接、10000 次往返时的系统调用：

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 30.64    0.054859           5     10000           write
 23.37    0.041841           3     12001           epoll_ctl
 17.84    0.031947           2     11001           read
 16.31    0.029208           2     10051         1 epoll_pwait
  5.56    0.009948           9      1045        45 accept4
  3.29    0.005900           5      1002           close
```

- `write` 10000 次，正好是 10000 次回声；`read` 11001 次：10000 次读到数据，1000 次读到 0（连接关闭），还有 1 次；
- `epoll_pwait`（glibc 的 `epoll_wait` 用它实现）约 10000 次，每次往返等待一次；
- `accept4` 有 45 次返回错误：一次就绪后循环 accept，直到返回 `EAGAIN`（「没有更多的连接了」），这正是非阻塞的正确用法；
- **`epoll_ctl` 有 12001 次**：1000 次添加、1000 次删除，剩下的 10000 次是 `flush` 每次写完都调用 `EPOLL_CTL_MOD` 把事件改回 `EPOLLIN`，而它本来就是 `EPOLLIN`，这些调用完全是多余的。这是我写的代码中的一处浪费，练习 1 修正它。

`strace -c` 是发现这类问题的好办法：系统调用的次数和业务的次数（这里是 10000 次往返）对不上时，往往就有值得优化的地方。

## 5. 事件驱动

epoll 服务器的结构叫作**事件循环**：一个循环不断地等待事件、分派给对应的处理代码。每个连接的状态（还有什么没写完）不再保存在线程的栈上，而是保存在 `struct conn` 这样的数据结构中。Nginx、Redis、Node.js 都是这种结构；Nginx 用少数几个工作进程，每个进程一个事件循环，就能处理几万个并发连接。

代价是编程变复杂了：任何操作都不能阻塞，一个请求的处理被拆散在多次事件中。为了利用多个 CPU 核，常见的做法是每个核运行一个事件循环（多个进程或线程，各自一个 epoll），或者事件循环负责网络 IO，把耗时的计算交给线程池（上一篇的练习 3）。

## 6. 常见错误

1. **select 中使用值大于等于 1024 的描述符**：越界，实测程序被终止。
2. **事件循环中调用阻塞的函数**：阻塞的 `read`、`sleep`、同步的 DNS 解析（`getaddrinfo`）……都会让所有的连接停下来。
3. **非阻塞的 `write` 只写了一部分，剩下的丢掉了**：要保存起来，等 EPOLLOUT 再写。
4. **边缘触发时没有读到 `EAGAIN`**：剩下的数据不会再有通知。
5. **把 `EAGAIN` 当作错误关闭连接**：它只表示「现在没有」。
6. **关闭描述符前不从 epoll 中删除**：通常 close 会自动删除，但如果描述符被 dup 过，epoll 中的记录会残留。

## 7. 小结

- IO 多路复用让一个线程同时等待多个描述符，只处理就绪的那些
- select 用固定大小的位图，最多 1024 个描述符，实测超过时程序被终止；每次调用要传递并检查所有的描述符
- epoll 在内核中保存关注的描述符，`epoll_wait` 只返回就绪的；配合非阻塞 IO，正确处理 `EAGAIN` 和部分写出
- 水平触发：只要可读就一直通知；边缘触发：只在变化时通知一次，必须读到 `EAGAIN`
- 实测 10000 个连接：epoll 1 个线程、40 MB，多线程 10001 个线程、120 MB，多进程 10001 个进程、274 MB
- `strace -c` 发现了 10000 次多余的 `epoll_ctl`

## 8. 练习

1. 修改 `flush`，只在关注的事件真正需要改变时才调用 `epoll_ctl`，再用 `strace -c` 统计。
2. 把 `echo_epoll` 改成边缘触发，如果 `read` 只读一次就返回事件循环，会发生什么？
3. 一个事件驱动的服务器中，处理某个请求需要计算 2 秒，会怎样？应该怎样处理？

<details>
<summary>点击查看答案</summary>

1. 在 `struct conn` 中记下当前关注的事件，增加一个函数，只有事件真正改变时才调用 `epoll_ctl`：

   ```c
   struct conn {
       int fd;
       char pending[4096];
       size_t len;
       uint32_t events;                             // 当前在 epoll 中关注的事件
   };

   // 只在关注的事件需要改变时才调用 epoll_ctl
   static void want(int ep, struct conn *c, uint32_t events)
   {
       if (c->events == events) return;
       struct epoll_event ev = {.events = events, .data.ptr = c};
       epoll_ctl(ep, EPOLL_CTL_MOD, c->fd, &ev);
       c->events = events;
   }
   ```

   `flush` 中的两处 `epoll_ctl` 改成 `want(ep, c, EPOLLIN | EPOLLOUT)` 和 `want(ep, c, EPOLLIN)`，新连接的 `events` 初始化为 `EPOLLIN`。同样的压测：

   ```
    36.15    0.051760           5     10000           write
    24.60    0.035224           3     10008         1 epoll_pwait
     5.61    0.008038           4      2001           epoll_ctl
   ```

   `epoll_ctl` 从 12001 次降到 2001 次：1000 次添加、1000 次删除，1 次是监听套接字的添加。
2. 在练习 1 的版本上改成边缘触发（所有的事件都加上 `EPOLLET`），并把缓冲区故意改成 16 字节，让一次 `read` 读不完。客户端发送 37 字节（36 个字符加换行）：

   ```
   1 秒后：服务器 运行中
   ESTAB 21     0           10.0.2.2:9000     10.0.1.2:39622
   0000000   0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f
   ```

   客户端只收到了前 16 个字节，之后就一直等着；服务器的接收队列中还有 21 个字节（`Recv-Q` 是 21），却再也没有被读取。边缘触发只在新数据到达时通知一次，服务器读了 16 字节就回到了 `epoll_wait`，剩下的 21 字节不会再触发任何通知。除非客户端再发送新的数据，这个连接就永远卡住了。正确的做法是在边缘触发时循环 `read`，直到返回 `EAGAIN`。

   有意思的是，在原来的 `echo_epoll` 上做同样的修改，并不会卡住：它的 `flush` 每次都调用 `EPOLL_CTL_MOD`，而重新设置事件时，epoll 会重新检查描述符的状态，发现还有数据可读就再次通知。练习 1 中「多余」的调用，在这里恰好掩盖了 bug。
3. 这 2 秒中事件循环无法处理任何其他连接的事件，所有的客户端都要等待，就像上一篇的单客户端服务器一样。应该把耗时的计算交给线程池，事件循环继续处理其他的事件；计算完成后，通过一个管道或者 `eventfd` 通知事件循环（它也能被 epoll 等待），再把结果写回客户端。

</details>
