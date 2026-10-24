---
title: TCP Socket 入门
date: 2026-10-24 16:30:00
updated: 2026-10-24 16:30:00
categories:
  - 计算机网络
  - Socket编程
tags:
  - 计算机网络
  - Socket
  - TCP
order: 1
description: 套接字（socket）是程序使用 TCP/UDP 的接口。本文用 C 语言写一个回声服务器和客户端：socket、bind、listen、accept、connect、read、write、close，每个函数在做什么，又对应了第 2 章中的哪些报文；先弄清字节序，htons(9000) 在内存中的两个字节是 23 28 而不是 28 23；用 strace 看服务器实际执行的系统调用；最后发现这个服务器的问题：第二个客户端的连接已经建立，却要等 2.5 秒才得到回应。配有系统调用与报文对应关系的逐步演示。
top_group_list: true
disableNunjucks: true
viz: [net-socket]
---

> **本文目标**：理解套接字是什么；理解字节序，会用 `htons`、`htonl`、`inet_pton`、`inet_ntop` 转换端口和地址；会用 `socket`、`bind`、`listen`、`accept`、`connect`、`read`、`write`、`close` 写 TCP 服务器和客户端，并正确处理错误；理解监听套接字和连接套接字的区别；理解每个调用在 TCP 层面做了什么。
>
> **前置知识**：第 2 章的 TCP，特别是第 2.2 篇的三次握手、第 2.5 篇的「字节流没有边界」和 backlog；Linux 基础专栏的《文件描述符与系统调用》。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），GCC 13.3。服务器运行在实验网络的 s（10.0.2.2）上，客户端运行在 c（10.0.1.2）上。

## 1. 套接字

前面三章一直在用 nc、curl、Python 收发数据，它们背后都是同一套接口：**套接字**（socket）。在 Linux 中，套接字也是一个**文件描述符**：创建后得到一个整数，像文件一样用 `read`、`write` 读写，用 `close` 关闭（《文件描述符与系统调用》中的「一切皆文件」）。区别在于，它的另一端不是磁盘上的文件，而是网络另一头的一个程序。

第 1.1 篇说过，套接字是应用层和传输层之间的接口：程序把数据交给套接字，TCP、IP、以太网的封装都由内核完成。

## 2. 字节序

写网络程序之前，先要处理一个细节：一个多字节的整数（比如端口 9000 = 0x2328）在内存中按什么顺序存放？

- **小端序**：低位字节在前，`28 23`。x86 和 ARM（本机就是，`lscpu` 显示 `Byte Order: Little Endian`）都是小端序；
- **大端序**：高位字节在前，`23 28`。

网络协议规定，所有的头部字段都按**大端序**传送，所以大端序也叫**网络字节序**。第 1.1 篇拆开 TCP 头时，端口 80 就是 `00 50`。程序中的端口、地址，在交给套接字之前都要转换：

```c
// byteorder.c：主机字节序和网络字节序
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>

static void dump(const char *name, const void *p, int n)
{
    const unsigned char *b = p;
    printf("%s：", name);
    for (int i = 0; i < n; i++) printf(" %02x", b[i]);
    printf("\n");
}

int main(void)
{
    uint16_t port = 9000;                        // 0x2328
    uint32_t addr = 0x0a000202;                  // 10.0.2.2
    uint16_t nport = htons(port);
    uint32_t naddr = htonl(addr);
    dump("9000（主机字节序）", &port, 2);
    dump("htons(9000)", &nport, 2);
    dump("10.0.2.2（主机字节序）", &addr, 4);
    dump("htonl(10.0.2.2)", &naddr, 4);

    struct in_addr a;
    inet_pton(AF_INET, "10.0.2.2", &a);          // 字符串 → 网络字节序的整数
    dump("inet_pton(\"10.0.2.2\")", &a, 4);
    char s[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a, s, sizeof s);         // 网络字节序的整数 → 字符串
    printf("inet_ntop 转换回来：%s\n", s);
    return 0;
}
```

```
9000（主机字节序）： 28 23
htons(9000)： 23 28
10.0.2.2（主机字节序）： 02 02 00 0a
htonl(10.0.2.2)： 0a 00 02 02
inet_pton("10.0.2.2")： 0a 00 02 02
inet_ntop 转换回来：10.0.2.2
```

| 函数 | 作用 |
|---|---|
| `htons` / `ntohs` | 16 位整数（端口）：主机字节序 ↔ 网络字节序（host to network short） |
| `htonl` / `ntohl` | 32 位整数（IPv4 地址） |
| `inet_pton` | 字符串 `"10.0.2.2"` → 网络字节序的地址（presentation to network） |
| `inet_ntop` | 反过来，网络字节序的地址 → 字符串 |

在大端序的机器上，`htons` 什么都不做；但写程序时要一律调用它，程序才能在任何机器上正确运行。**忘记转换端口**是初学者最常见的错误之一：`sin_port = 9000` 在小端序的机器上实际监听的是 0x2823 = 10275 端口。

## 3. 回声服务器

回声服务器把客户端发来的数据原样发回去：

```c
// echo_server.c：回声服务器，把客户端发来的数据原样发回去
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// 把 n 个字节全部写出去（write 可能只写了一部分）
static int write_all(int fd, const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, buf, n);
        if (w == -1) return -1;
        buf += w;
        n -= w;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int port = argc > 1 ? atoi(argv[1]) : 9000;

    int lfd = socket(AF_INET, SOCK_STREAM, 0);          // 1. 创建套接字：IPv4，TCP
    if (lfd == -1) {
        perror("socket");
        return 1;
    }
    int on = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);   // 允许立即重用端口（第 2.2 篇的 TIME_WAIT）

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);           // 0.0.0.0：本机的所有地址
    addr.sin_port = htons(port);                        // 端口要转换成网络字节序
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) == -1) {   // 2. 绑定地址和端口
        perror("bind");
        return 1;
    }
    if (listen(lfd, 16) == -1) {                        // 3. 开始监听
        perror("listen");
        return 1;
    }
    printf("监听端口 %d\n", port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t len = sizeof peer;
        int cfd = accept(lfd, (struct sockaddr *)&peer, &len);   // 4. 取出一个已建立的连接
        if (cfd == -1) {
            perror("accept");
            continue;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        int pport = ntohs(peer.sin_port);
        printf("客户端 %s:%d 连接，fd = %d\n", ip, pport, cfd);
        fflush(stdout);

        char buf[4096];
        ssize_t n;
        while ((n = read(cfd, buf, sizeof buf)) > 0) {   // 5. 读到什么就写回什么
            if (write_all(cfd, buf, n) == -1) {
                perror("write");
                break;
            }
        }
        if (n == -1) perror("read");
        printf("客户端 %s:%d 断开\n", ip, pport);         // read 返回 0：对方关闭了连接
        fflush(stdout);
        close(cfd);                                      // 6. 关闭这个连接
    }
}
```

服务器的六个步骤：

| 步骤 | 调用 | 作用 |
|---|---|---|
| 1 | `socket(AF_INET, SOCK_STREAM, 0)` | 创建套接字。`AF_INET` 是 IPv4，`SOCK_STREAM` 是 TCP（`SOCK_DGRAM` 是 UDP，下一篇） |
| 2 | `bind` | 绑定地址和端口。地址结构 `sockaddr_in` 中的端口和地址都是网络字节序 |
| 3 | `listen` | 变成监听套接字，内核开始接受连接；第二个参数是 backlog |
| 4 | `accept` | 从全连接队列中取出一个连接，返回一个**新的**描述符；没有连接时阻塞 |
| 5 | `read` / `write` | 通过新描述符收发数据 |
| 6 | `close` | 关闭这个连接 |

几个要点：

- **`accept` 返回的是一个新的套接字**。监听套接字 `lfd` 只负责接受连接，每个客户端的通信都通过 `accept` 返回的 `cfd` 进行；
- 套接字的函数接收的是通用的地址结构 `struct sockaddr *`，IPv4 的 `sockaddr_in` 要强制转换；
- `INADDR_ANY`（0.0.0.0）表示监听本机的**所有**地址（第 1.3 篇）；只写 `127.0.0.1` 的话，其他机器就连不上（第 3.5 篇的练习 1）；
- `SO_REUSEADDR` 让服务器重启后能立即绑定同一个端口，而不会因为 TIME_WAIT 报 `Address already in use`（第 2.2 篇）；
- `write` 可能只写出一部分，所以用 `write_all` 循环写完（《文件描述符与系统调用》）。

## 4. 客户端

```c
// echo_client.c：从标准输入读取每一行，发给服务器，打印服务器发回的内容
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "用法：%s 服务器IP 端口\n", argv[0]);
        return 2;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
        fprintf(stderr, "地址格式错误：%s\n", argv[1]);
        return 2;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == -1) {   // 三次握手在这里完成
        perror("connect");
        return 1;
    }

    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        if (write(fd, line, len) != (ssize_t)len) {
            perror("write");
            return 1;
        }
        char reply[sizeof line];
        size_t got = 0;                          // TCP 没有消息边界，要读够发出的字节数
        while (got < len) {
            ssize_t n = read(fd, reply + got, sizeof reply - got);
            if (n <= 0) {
                fprintf(stderr, "服务器关闭了连接\n");
                return 1;
            }
            got += n;
        }
        printf("回声：%.*s", (int)got, reply);
    }
    close(fd);
    return 0;
}
```

客户端不需要 `bind`：`connect` 时内核会自动选一个临时端口（下面的输出中是 36316）。`connect` 会完成三次握手，成功返回后就可以收发数据了。

读取回声时，客户端要**一直读到收齐发出的字节数**：第 2.5 篇讲过，TCP 没有消息边界，一次 `read` 可能只读到一部分。

## 5. 运行

在 s 上运行服务器（同时用 strace 记录它的系统调用），在 c 上运行客户端，发送两行：

```bash
ip netns exec s strace -f -e trace=socket,setsockopt,bind,listen,accept,read,write,close -o strace.txt ./echo_server 9000 &
printf "hello\nsocket programming\n" | ip netns exec c ./echo_client 10.0.2.2 9000
```

客户端：

```
回声：hello
回声：socket programming
```

服务器：

```
监听端口 9000
客户端 10.0.1.2:36316 连接，fd = 4
客户端 10.0.1.2:36316 断开
```

服务器执行的系统调用（省略了程序启动时加载 libc 的部分；输出到终端的 `write(1, ...)` 是中文的 printf，显示成了转义的 UTF-8 字节）：

```
socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3
setsockopt(3, SOL_SOCKET, SO_REUSEADDR, [1], 4) = 0
bind(3, {sa_family=AF_INET, sin_port=htons(9000), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
listen(3, 16)                     = 0
write(1, "\347\233\221\345\220\254\347\253\257\345\217\243 9000\n", 18) = 18
accept(3, {sa_family=AF_INET, sin_port=htons(36316), sin_addr=inet_addr("10.0.1.2")}, [16]) = 4
write(1, "\345\256\242\346\210\267\347\253\257 10.0.1.2:36316 \350\277\236\346\216\245\357"..., 41) = 41
read(4, "hello\n", 4096)          = 6
write(4, "hello\n", 6)            = 6
read(4, "socket programming\n", 4096) = 19
write(4, "socket programming\n", 19) = 19
read(4, "", 4096)                 = 0
write(1, "\345\256\242\346\210\267\347\253\257 10.0.1.2:36316 \346\226\255\345\274\200\n", 32) = 32
close(4)                          = 0
```

- 监听套接字是 3，`accept` 返回的连接套接字是 4；
- strace 很贴心地把地址结构解码成了 `htons(9000)`、`inet_addr("0.0.0.0")`；
- 客户端发了两行，服务器正好读了两次，每次一行。这只是因为客户端每发一行都等待回声，两行没有机会合在一起。如果客户端连续发送，服务器一次 `read` 读到的可能是任意的长度；
- 最后的 `read` 返回 0：客户端关闭了连接。

下面的演示把客户端和服务器的调用对照起来，并标出每个调用引起的报文：

<div class="dsviz" data-viz="socket-calls"></div>

注意握手完全由内核完成：客户端的 `connect` 发起握手并等待它完成，服务器的 `accept` 只是从队列中取出一个已经建立好的连接。

## 6. 一次只能服务一个客户端

这个服务器有一个严重的问题。让客户端 A 连接后，发送一行，3 秒后再发送一行；A 连接后 0.5 秒，客户端 B 也连接，发送一行：

```
--- 此时 s 上的连接：
ESTAB 0      0           10.0.2.2:9000     10.0.1.2:36332
ESTAB 7      0           10.0.2.2:9000     10.0.1.2:36340
[A] 回声：first
[A] 回声：first-again
[B] 回声：second
[B] 用时 2.5 秒
```

- B 的连接**已经建立**了（`ESTAB`），它发送的 7 字节（`second\n`）也已经到了服务器的接收缓冲区（`Recv-Q` 是 7），但没有人读取；
- 服务器正在 A 的 `read` 中等待，处理完 A，回到 `accept`，才轮到 B。B 等了 **2.5 秒**才得到回声。

如果 A 一直不断开，B 就要一直等下去。真实的服务器要同时服务成千上万个客户端，第 4.3 篇和第 4.4 篇会解决这个问题。

## 7. 常见错误

1. **端口没有用 `htons` 转换**：服务器监听在了错误的端口上，客户端连不上。
2. **`bind` 报 `Address already in use`**：端口被占用，或者有 TIME_WAIT 的连接，设置 `SO_REUSEADDR`；用 `ss -ltnp` 看看是谁占用了它。
3. **用监听套接字收发数据**：要用 `accept` 返回的新描述符。
4. **假设一次 `read` 读到一条完整的消息**：TCP 是字节流（第 2.5 篇）。
5. **不检查 `read` 返回 0**：对方关闭了连接，程序却在死循环中不断读到 0。
6. **忘记关闭 `accept` 返回的描述符**：描述符泄漏，连接停在 CLOSE_WAIT（第 2.2 篇）。
7. **向已经关闭的连接写数据**：进程收到 SIGPIPE 被终止（Linux 基础专栏的《信号》）。服务器通常要忽略 SIGPIPE，或者使用 `send(fd, buf, n, MSG_NOSIGNAL)`。

## 8. 小结

- 套接字是一个文件描述符，是程序使用 TCP/UDP 的接口
- 网络字节序是大端序；端口用 `htons`/`ntohs`，地址用 `inet_pton`/`inet_ntop`；实测 `htons(9000)` 在内存中是 `23 28`
- 服务器：`socket` → `bind` → `listen` → `accept` → `read`/`write` → `close`；客户端：`socket` → `connect` → `write`/`read` → `close`
- `accept` 返回一个新的连接套接字，监听套接字继续接受其他连接；握手由内核完成
- 这个最简单的服务器一次只能服务一个客户端：实测第二个客户端的连接已经建立，却等了 2.5 秒

## 9. 练习

1. 把 `addr.sin_port = htons(port)` 改成 `addr.sin_port = port`，用 9000 端口运行，服务器实际监听在哪个端口？用 `ss -ltn` 验证。
2. 修改客户端，让它接受一个域名（如 `localhost`）而不只是 IP 地址（提示：`getaddrinfo`）。
3. 服务器的 `listen` 第二个参数改成 1，并且在 `accept` 之后 `sleep(30)` 再处理，连续启动 5 个客户端，会怎样？（回顾第 2.5 篇）

<details>
<summary>点击查看答案</summary>

1. 9000 = 0x2328，在小端序的机器上，内存中是 `28 23`；`sin_port` 被当作网络字节序（大端序）解读，就成了 0x2823 = **10275**：

   ```
   LISTEN 0      16           0.0.0.0:10275      0.0.0.0:*
   ```

   程序打印「监听端口 9000」，实际却监听在 10275。客户端连接 9000 会得到 Connection refused。

2. `getaddrinfo` 把主机名和端口解析成一个地址列表（它会读 `/etc/hosts`、查询 DNS，和第 3.1 篇的 `getent` 相同），同时适用于 IPv4 和 IPv6：

   ```c
   // echo_client2.c：用 getaddrinfo 解析主机名，支持 IPv4 和 IPv6
   #include <netdb.h>
   #include <stdio.h>
   #include <string.h>
   #include <sys/socket.h>
   #include <unistd.h>

   static int connect_to(const char *host, const char *port)
   {
       struct addrinfo hints, *res, *p;
       memset(&hints, 0, sizeof hints);
       hints.ai_family = AF_UNSPEC;                 // IPv4 和 IPv6 都可以
       hints.ai_socktype = SOCK_STREAM;
       int err = getaddrinfo(host, port, &hints, &res);
       if (err != 0) {
           fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
           return -1;
       }
       int fd = -1;
       for (p = res; p != NULL; p = p->ai_next) {   // 一个名字可能有多个地址，依次尝试
           fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
           if (fd == -1) continue;
           if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
           close(fd);
           fd = -1;
       }
       freeaddrinfo(res);
       return fd;
   }

   int main(int argc, char *argv[])
   {
       if (argc != 3) {
           fprintf(stderr, "用法：%s 主机 端口\n", argv[0]);
           return 2;
       }
       int fd = connect_to(argv[1], argv[2]);
       if (fd == -1) {
           fprintf(stderr, "无法连接到 %s:%s\n", argv[1], argv[2]);
           return 1;
       }
       char line[1024];
       while (fgets(line, sizeof line, stdin)) {
           size_t len = strlen(line);
           if (write(fd, line, len) != (ssize_t)len) return 1;
           char reply[sizeof line];
           size_t got = 0;
           while (got < len) {
               ssize_t n = read(fd, reply + got, sizeof reply - got);
               if (n <= 0) return 1;
               got += n;
           }
           printf("回声：%.*s", (int)got, reply);
       }
       close(fd);
       return 0;
   }
   ```

   ```
   $ echo hello | ./echo_client2 localhost 9000
   回声：hello
   $ echo hello | ./echo_client2 nosuchhost.invalid 9000
   getaddrinfo: Name or service not known
   无法连接到 nosuchhost.invalid:9000
   $ echo hello | ./echo_client2 localhost 9999
   无法连接到 localhost:9999
   ```

   `/etc/hosts` 中 localhost 有两个地址：`127.0.0.1` 和 `::1`。服务器只监听了 IPv4 的 0.0.0.0，连接 `::1` 会被拒绝，程序接着尝试下一个地址 127.0.0.1，成功了。这就是要遍历 `getaddrinfo` 返回的整个列表的原因。现代的网络程序应该用 `getaddrinfo`，而不是只能处理 IPv4 的 `inet_pton`。
3. 第 2.5 篇的实验：全连接队列中最多排 2 个（backlog + 1），再加上已经被 accept 取走的 1 个，前 3 个客户端的 connect 会立即成功（但要排队等服务器处理），之后的客户端的 SYN 被丢弃，connect 会在重发 SYN 的过程中等待，直到队列中有了空位，或者客户端自己超时。

</details>
