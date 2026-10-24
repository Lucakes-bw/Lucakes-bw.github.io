---
title: UDP Socket
date: 2026-10-24 16:35:00
updated: 2026-10-24 16:35:00
categories:
  - 计算机网络
  - Socket编程
tags:
  - 计算机网络
  - Socket
  - UDP
  - DNS
order: 2
description: UDP 套接字没有连接，用 sendto 和 recvfrom 收发一个个独立的数据报，一个套接字就能同时服务所有的客户端。本文先写一个 UDP 回声服务器，两个客户端的数据报交错到达，谁也不用等谁；然后用 C 语言手工构造 DNS 查询报文，发给 1.1.1.1，解析出 example.com 的两个 IP 地址，结果和 dig 一致；查询一个不存在的 DNS 服务器时，程序自己超时重发了 3 次。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `SOCK_DGRAM`、`sendto`、`recvfrom` 编写 UDP 程序；理解 UDP 服务器和 TCP 服务器在结构上的区别；能按照协议的格式手工构造和解析二进制报文；会用 `SO_RCVTIMEO` 设置超时，在应用层实现重发。
>
> **前置知识**：上一篇的 TCP Socket；第 2.1 篇的 UDP；第 3.1 篇的 DNS。
>
> **实验环境**：Ubuntu 24.04 容器，GCC 13.3。DNS 查询发往真实的 1.1.1.1 和 8.8.8.8。

## 1. UDP 回声服务器

```c
// udp_echo.c：UDP 回声服务器，一个套接字就能服务所有的客户端
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int main(int argc, char *argv[])
{
    int port = argc > 1 ? atoi(argv[1]) : 9001;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);            // SOCK_DGRAM：UDP
    if (fd == -1) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) == -1) {
        perror("bind");
        return 1;
    }
    for (;;) {                                          // 没有 listen，也没有 accept
        char buf[2048];
        struct sockaddr_in peer;
        socklen_t len = sizeof peer;
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&peer, &len);   // 收一个数据报，同时得到发送方的地址
        if (n == -1) {
            perror("recvfrom");
            continue;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        printf("来自 %s:%d 的 %zd 字节\n", ip, ntohs(peer.sin_port), n);
        fflush(stdout);
        sendto(fd, buf, n, 0, (struct sockaddr *)&peer, len);   // 发回给这个发送方
    }
}
```

和上一篇的 TCP 服务器相比：

| | TCP | UDP |
|---|---|---|
| 创建 | `SOCK_STREAM` | `SOCK_DGRAM` |
| 服务器的步骤 | `bind` → `listen` → `accept` → `read`/`write` | `bind` → `recvfrom`/`sendto` |
| 每个客户端 | 一个连接，`accept` 返回一个新描述符 | 没有连接，所有客户端共用一个套接字 |
| 对方是谁 | 连接建立时就确定了 | 每个数据报都要问：`recvfrom` 告诉你发送方，`sendto` 要指定接收方 |
| 一次读取 | 任意多的字节，没有边界 | 正好一个完整的数据报 |

在 s 上运行服务器，c 上的两个客户端（用 `nc -u`）几乎同时发送：A 发一行，1 秒后再发一行；B 在 A 开始 0.3 秒后发一行：

```
[B 收到] from B
[A 收到] from A 1
[A 收到] from A 2
```

服务器的输出：

```
来自 10.0.1.2:42662 的 9 字节
来自 10.0.1.2:42554 的 7 字节
来自 10.0.1.2:42662 的 9 字节
```

A（端口 42662）和 B（端口 42554）的数据报交错到达，服务器逐个处理，**B 不需要等 A 结束**。上一篇的 TCP 服务器中，B 要等 A 断开连接才能得到服务；UDP 没有「连接」这个状态，每个数据报都是独立的，一个循环就能服务所有的客户端。这正是 DNS 服务器能用很少的资源回答海量查询的原因之一。

当然，第 2.1 篇讲过的问题都还在：数据报可能丢失、乱序，处理不过来时会被丢弃。可靠性要由应用自己负责。

## 2. 手工构造 DNS 查询

DNS 查询是一个典型的 UDP 应用：一问一答，各一个数据报。按照 DNS 报文的格式，自己构造一个查询，发给 DNS 服务器，再解析回答：

```c
// minidig.c：手工构造 DNS 查询报文，用 UDP 发给 DNS 服务器，解析回答中的 A 记录
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// 把 "example.com" 编码成 DNS 的格式：\x07example\x03com\x00
static int encode_name(const char *name, unsigned char *out)
{
    int n = 0;
    while (*name) {
        const char *dot = strchr(name, '.');
        int len = dot ? dot - name : (int)strlen(name);
        if (len == 0 || len > 63) return -1;
        out[n++] = len;
        memcpy(out + n, name, len);
        n += len;
        name += len + (dot ? 1 : 0);
    }
    out[n++] = 0;
    return n;
}

// 跳过报文中位置 p 处的一个名字（可能是压缩指针），返回名字之后的位置
static int skip_name(const unsigned char *msg, int len, int p)
{
    while (p < len) {
        if (msg[p] == 0) return p + 1;
        if ((msg[p] & 0xC0) == 0xC0) return p + 2;      // 压缩：两个字节的指针
        p += msg[p] + 1;
    }
    return -1;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "用法：%s 域名 [DNS服务器]\n", argv[0]);
        return 2;
    }
    const char *server = argc > 2 ? argv[2] : "1.1.1.1";

    // 1. 构造查询：12 字节的头部 + 问题
    unsigned char q[512];
    srand(time(NULL) ^ getpid());
    uint16_t id = rand() & 0xFFFF;
    memset(q, 0, 12);
    q[0] = id >> 8;  q[1] = id & 0xFF;                  // 事务 ID
    q[2] = 0x01;                                        // 标志：RD = 1（请求递归）
    q[5] = 1;                                           // 问题数 = 1
    int n = encode_name(argv[1], q + 12);
    if (n < 0) {
        fprintf(stderr, "域名格式错误\n");
        return 2;
    }
    n += 12;
    q[n++] = 0; q[n++] = 1;                             // 类型 A
    q[n++] = 0; q[n++] = 1;                             // 类 IN

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);   // recvfrom 最多等 2 秒
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    inet_pton(AF_INET, server, &addr.sin_addr);

    // 2. 发送并等待回答；UDP 可能丢包，超时就重发，最多 3 次
    unsigned char r[1500];
    ssize_t rn = -1;
    for (int try = 1; try <= 3 && rn < 0; try++) {
        sendto(fd, q, n, 0, (struct sockaddr *)&addr, sizeof addr);
        printf("第 %d 次发送查询（%d 字节）到 %s\n", try, n, server);
        rn = recvfrom(fd, r, sizeof r, 0, NULL, NULL);
        if (rn < 0) printf("  2 秒内没有回答\n");
    }
    if (rn < 0) return 1;

    // 3. 解析回答
    if (rn < 12 || r[0] != q[0] || r[1] != q[1]) {
        fprintf(stderr, "回答的事务 ID 不匹配\n");
        return 1;
    }
    int rcode = r[3] & 0x0F, ancount = r[6] << 8 | r[7];
    printf("收到 %zd 字节，ID 0x%04x，状态码 %d，%d 条回答\n", rn, id, rcode, ancount);
    int p = skip_name(r, rn, 12) + 4;                   // 跳过问题部分的名字、类型、类
    for (int i = 0; i < ancount && p > 0 && p + 10 <= rn; i++) {
        p = skip_name(r, rn, p);
        int type = r[p] << 8 | r[p + 1];
        uint32_t ttl = (uint32_t)r[p + 4] << 24 | r[p + 5] << 16 | r[p + 6] << 8 | r[p + 7];
        int rdlen = r[p + 8] << 8 | r[p + 9];
        p += 10;
        if (type == 1 && rdlen == 4) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, r + p, ip, sizeof ip);
            printf("  A %s  TTL %u\n", ip, ttl);
        } else {
            printf("  类型 %d 的记录，%d 字节\n", type, rdlen);
        }
        p += rdlen;
    }
    close(fd);
    return 0;
}
```

### 2.1 报文的格式

DNS 报文由 12 字节的**头部**和若干部分组成：

| 偏移 | 长度 | 字段 | 查询中的值 |
|---|---|---|---|
| 0 | 2 | 事务 ID | 随机数 |
| 2 | 2 | 标志 | 0x0100：RD = 1，请求递归 |
| 4 | 2 | 问题数 | 1 |
| 6 | 2 | 回答数 | 0 |
| 8 | 2 | 授权记录数 | 0 |
| 10 | 2 | 附加记录数 | 0 |
| 12 | 不定 | 问题：名字、类型、类 | example.com，A，IN |

所有多字节的数字都是**网络字节序**（大端序），所以程序中逐个字节地写入高位和低位。域名的编码方式是：每一段前面加上它的长度，最后以 0 结束。在 c 上抓到 minidig 发出的查询（只显示了 UDP 头之后的部分）：

```
	0x0010:  0101 0101 db6d 0035 0025 ae4c d427 0100  .....m.5.%.L.'..
	0x0020:  0001 0000 0000 0000 0765 7861 6d70 6c65  .........example
	0x0030:  0363 6f6d 0000 0100 01                   .com.....
```

从 `d427` 开始：事务 ID 0xd427（十进制 54311，tcpdump 显示为 `54311+`），标志 `0100`，问题数 `0001`，三个 0；然后是 `07 example 03 com 00`，最后是类型 `0001`（A）和类 `0001`（IN）。整个查询 29 字节，第 3.1 篇中 dig 的查询是 52 字节，多出的部分是 dig 附加的 EDNS 选项。

### 2.2 解析回答

回答的开头是同样的头部和问题，后面是回答记录，每条记录是：名字、类型（2 字节）、类（2 字节）、TTL（4 字节）、数据长度（2 字节）、数据。回答中的名字通常是**压缩**的：两个最高位都为 1 的字节（`0xC0`）表示「这个名字和报文中某个位置的名字相同」，后面跟着位置，只占 2 个字节。程序用 `skip_name` 处理这两种情况。

```bash
./minidig example.com
./minidig www.github.com 8.8.8.8
dig +noall +answer www.github.com @8.8.8.8
```

```
第 1 次发送查询（29 字节）到 1.1.1.1
收到 61 字节，ID 0x9532，状态码 0，2 条回答
  A 104.20.23.154  TTL 148
  A 172.66.147.243  TTL 148
第 1 次发送查询（32 字节）到 8.8.8.8
收到 62 字节，ID 0x532c，状态码 0，2 条回答
  类型 5 的记录，2 字节
  A 20.205.243.166  TTL 60
www.github.com.		3371	IN	CNAME	github.com.
github.com.		60	IN	A	20.205.243.166
```

example.com 的两个地址都正确解析出来了。www.github.com 的第一条回答是类型 5，即 **CNAME**（第 3.1 篇），它的数据只有 2 字节，因为 `github.com` 也被压缩成了一个指针；第二条才是 A 记录，和 dig 的结果一致。

程序检查了回答的事务 ID 是否和查询相同：UDP 没有连接，任何人都可以向这个端口发送数据报，事务 ID（加上随机的源端口）是防止接收到伪造回答的一道简单的防线。

### 2.3 超时和重发

UDP 不会重发，丢了就是丢了。程序用 `SO_RCVTIMEO` 让 `recvfrom` 最多等待 2 秒，超时就重新发送，最多 3 次。向一个不存在的地址查询：

```bash
./minidig example.com 10.255.255.1; echo "exit=$?"
```

```
第 1 次发送查询（29 字节）到 10.255.255.1
  2 秒内没有回答
第 2 次发送查询（29 字节）到 10.255.255.1
  2 秒内没有回答
第 3 次发送查询（29 字节）到 10.255.255.1
  2 秒内没有回答
exit=1
```

这就是第 3.5 篇中 curl 等待 10 秒才报「无法解析主机名」的原因：glibc 的解析器做的正是同样的事，默认每次等待 5 秒、尝试 2 次。

## 3. connect 用于 UDP

UDP 套接字也可以调用 `connect`，但它不发送任何报文，只是在内核中记下「默认的对方」：之后可以用 `send`/`recv`（或 `write`/`read`）代替 `sendto`/`recvfrom`，内核还会丢弃来自其他地址的数据报。更重要的是，`connect` 之后，如果对方回复了 ICMP 端口不可达（第 2.1 篇），下一次 `recv` 会返回错误 `ECONNREFUSED`，程序就能知道对方的端口没有在监听。没有 `connect` 的 UDP 套接字收不到这个错误。

## 4. 常见错误

1. **以为一次 `recvfrom` 可能读到半个数据报**：UDP 每次正好读一个完整的数据报；缓冲区太小时，多出的部分被**丢弃**，不会留到下一次。
2. **不设置超时**：对方不回答时，`recvfrom` 会永远阻塞。
3. **不检查回答的来源和 ID**：可能接收到无关的或伪造的数据报。
4. **构造二进制报文时忘记字节序**：多字节的字段都是大端序。
5. **用结构体直接映射报文**：编译器可能在字段之间插入填充字节（C 语言专栏的《结构体、联合与枚举》），而且有字节序的问题，逐字节读写更稳妥。

## 5. 小结

- UDP 套接字用 `SOCK_DGRAM` 创建，服务器只需要 `bind`，用 `recvfrom` 收数据报并得到发送方地址，用 `sendto` 回复
- 没有连接，一个套接字服务所有客户端；实测两个客户端的数据报交错到达，互不等待
- DNS 报文：12 字节的头部（ID、标志、各部分的计数）+ 问题 + 回答；域名按「长度 + 内容」编码，回答中的名字常用压缩指针
- 实测手写的 DNS 客户端：29 字节的查询，解析出的 A 记录和 dig 一致，并正确跳过了 CNAME
- 可靠性要由应用负责：用 `SO_RCVTIMEO` 设置超时，超时重发，检查事务 ID

## 6. 练习

1. 修改 minidig，让它查询 AAAA 记录（类型 28），并用 `inet_ntop(AF_INET6, ...)` 打印 IPv6 地址。
2. 为什么 DNS 的查询报文中，名字不使用 C 语言字符串那样以 `\0` 结尾、中间用点分隔的格式，而要在每一段前面加上长度？
3. UDP 回声服务器收到一个 2000 字节的数据报，`recvfrom` 的缓冲区只有 1000 字节，会怎样？

<details>
<summary>点击查看答案</summary>

1. 只需要改两处：问题中的类型改成 28，解析时处理类型 28、长度 16 的记录：

   ```c
   q[n++] = 0; q[n++] = 28;                            // 类型 AAAA
   ...
   if (type == 28 && rdlen == 16) {
       char ip[INET6_ADDRSTRLEN];
       inet_ntop(AF_INET6, r + p, ip, sizeof ip);
       printf("  AAAA %s  TTL %u\n", ip, ttl);
   ```

   ```
   第 1 次发送查询（29 字节）到 1.1.1.1
   收到 85 字节，ID 0xa1e3，状态码 0，2 条回答
     AAAA 2606:4700:10::ac42:93f3  TTL 233
     AAAA 2606:4700:10::6814:179a  TTL 233
   ```

   和第 3.1 篇 dig 查到的 AAAA 记录相同。
2. 有了长度，解析时不需要逐个字节地查找分隔符，可以直接跳到下一段；而且每段的内容中可以出现任意的字节（包括点）。更重要的是，「长度」的最高两位用来区分普通的段和压缩指针（`0xC0`），所以每段的长度最多是 63。
3. 数据报的后 1000 字节被丢弃。实测：向本机发送一个 2000 字节和一个 4 字节的数据报，用 1000 字节的缓冲区接收两次（第一次加上 `MSG_TRUNC` 标志，让 `recv` 返回数据报的真实长度）：

   ```
   第一次 recv 返回 2000（缓冲区 1000 字节）
   第二次 recv 返回 4：next
   ```

   第一次只拿到了前 1000 字节，返回值 2000 说明数据报其实更长；第二次读到的是下一个数据报 `next`，第一个数据报剩下的 1000 字节已经没有了。不加 `MSG_TRUNC` 时返回值是 1000，程序根本不知道数据被截断了。所以 UDP 的接收缓冲区要足够大，能装下最大的数据报。

</details>
