---
title: 实战：迷你 HTTP 服务器
date: 2026-10-24 16:50:00
updated: 2026-10-24 16:50:00
categories:
  - 计算机网络
  - Socket编程
tags:
  - 计算机网络
  - Socket
  - HTTP
order: 5
description: 把这一章的套接字编程和第三章的 HTTP 合在一起，用不到 200 行 C 写一个静态文件服务器：解析请求行，按扩展名设置 Content-Type，用 sendfile 发送文件，支持持久连接和一次发来的多个请求，拒绝 /../../etc/passwd 这样的路径，浏览器可以直接访问。用 ab 压测时，开启 keep-alive 后每个请求反而从 0.7 毫秒变成了 45 毫秒；抓包发现又是 Nagle 算法和延迟确认，用 MSG_MORE 把头部和正文合成一个报文段后，吞吐量从每秒 14000 个请求提高到 36000 个。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会在 TCP 字节流上正确地读取一个 HTTP 请求（读到空行为止，处理多个请求连在一起的情况）；会构造响应头部，用 `sendfile` 发送文件；理解持久连接在服务器一侧怎样实现；会防止路径穿越；会用 ab 压测，并用抓包解释压测中的反常结果。
>
> **前置知识**：本章前几篇的 TCP Socket 和多线程服务器；第三章的《HTTP/1.1》；第二章的《TCP 的常见问题》（Nagle 算法和延迟确认）。
>
> **实验环境**：Ubuntu 24.04 容器（2 个 CPU 核），GCC 13.3，ApacheBench 2.3。服务器在 s（10.0.2.2:8000），curl 和 ab 在 c；路由器 r 上默认不加延迟。

## 1. 要做什么

《HTTP/1.1》一篇用 curl 和 nc 看过 HTTP 报文：请求是几行文本，以一个空行结束；响应也是几行文本的头部，加上空行，再加上正文。HTTP 服务器要做的就是：

1. 从连接中读出一个完整的请求头部；
2. 解析第一行，得到方法、路径、版本；
3. 把路径映射到磁盘上的一个文件；
4. 发回状态行、头部和文件内容；
5. 如果是持久连接，回到第 1 步，读同一个连接上的下一个请求。

这次只做静态文件：只支持 `GET` 和 `HEAD`，网站目录 `www` 中有一个页面、一个样式表、一张 SVG 图片和一个文本文件：

```
www/index.html  www/style.css  www/logo.svg  www/notes.txt
```

并发模型用 4.3 篇的「每个连接一个线程」，它最简单，也足以支撑后面的测试。

## 2. 代码

```c minihttpd.c
// minihttpd.c：一个迷你的静态文件 HTTP 服务器
// 用法：minihttpd 端口 网站目录
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *root;                         // 网站的根目录

static const char *mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css")) return "text/css; charset=utf-8";
    if (!strcmp(dot, ".js")) return "text/javascript; charset=utf-8";
    if (!strcmp(dot, ".txt")) return "text/plain; charset=utf-8";
    if (!strcmp(dot, ".svg")) return "image/svg+xml";
    if (!strcmp(dot, ".png")) return "image/png";
    return "application/octet-stream";
}

// more 为真表示后面紧接着还有数据，先不要发出（见正文「keep-alive 反而变慢」）
static int send_all(int fd, const char *buf, size_t n, int more)
{
    // MSG_NOSIGNAL：对方已关闭时返回错误，而不是收到 SIGPIPE
    int flags = MSG_NOSIGNAL | (more ? MSG_MORE : 0);
    while (n > 0) {
        ssize_t w = send(fd, buf, n, flags);
        if (w <= 0) return -1;
        buf += w;
        n -= w;
    }
    return 0;
}

// 发送一个只有简短正文的响应（错误页）
static int send_simple(int fd, int code, const char *reason, int keep)
{
    char body[128], head[256];
    int blen = snprintf(body, sizeof body, "%d %s\n", code, reason);
    int hlen = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: %s\r\n\r\n",
        code, reason, blen, keep ? "keep-alive" : "close");
    if (send_all(fd, head, hlen, 1) == -1) return -1;
    return send_all(fd, body, blen, 0);
}

// 处理一个请求；返回 1 表示保持连接，0 表示关闭
static int handle(int fd, char *req)
{
    char method[8], target[1024], version[16];
    if (sscanf(req, "%7s %1023s %15s", method, target, version) != 3) {
        send_simple(fd, 400, "Bad Request", 0);
        return 0;
    }
    // HTTP/1.1 默认保持连接，除非请求中有 Connection: close；
    // HTTP/1.0 默认关闭，除非请求中有 Connection: keep-alive
    int keep = strcmp(version, "HTTP/1.1") == 0 ? !strcasestr(req, "\r\nConnection: close")
                                                : strcasestr(req, "\r\nConnection: keep-alive") != NULL;
    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        return send_simple(fd, 405, "Method Not Allowed", keep) == 0 && keep;
    }
    char *q = strchr(target, '?');               // 去掉查询字符串
    if (q) *q = '\0';
    if (target[0] != '/' || strstr(target, "..")) {   // 防止 /../../etc/passwd 这样访问网站目录之外的文件
        return send_simple(fd, 403, "Forbidden", keep) == 0 && keep;
    }
    char path[2048];
    snprintf(path, sizeof path, "%s%s%s", root, target, target[strlen(target) - 1] == '/' ? "index.html" : "");

    int file = open(path, O_RDONLY);
    struct stat st;
    if (file == -1 || fstat(file, &st) == -1 || !S_ISREG(st.st_mode)) {
        if (file != -1) close(file);
        return send_simple(fd, 404, "Not Found", keep) == 0 && keep;
    }
    char head[512];
    int hlen = snprintf(head, sizeof head,
        "HTTP/1.1 200 OK\r\nServer: minihttpd\r\nContent-Type: %s\r\nContent-Length: %lld\r\nConnection: %s\r\n\r\n",
        mime_type(path), (long long)st.st_size, keep ? "keep-alive" : "close");
    int get = strcmp(method, "GET") == 0;
    int ok = send_all(fd, head, hlen, get && st.st_size > 0) == 0;   // GET 的头部后面还有正文
    if (ok && get) {
        off_t off = 0;
        while (ok && off < st.st_size) {         // sendfile：内核直接把文件内容发到套接字，不经过用户空间
            if (sendfile(fd, file, &off, st.st_size - off) <= 0) ok = 0;
        }
    }
    close(file);
    return ok && keep;
}

static void *serve(void *arg)
{
    int fd = (int)(long)arg;
    char buf[8192];
    size_t len = 0;
    for (;;) {
        char *end;
        while ((end = memmem(buf, len, "\r\n\r\n", 4)) == NULL) {   // 读到空行，请求头才完整
            if (len == sizeof buf - 1) {
                send_simple(fd, 431, "Request Header Fields Too Large", 0);
                goto done;
            }
            ssize_t n = recv(fd, buf + len, sizeof buf - 1 - len, 0);
            if (n <= 0) goto done;               // 对方关闭了持久连接，或者出错
            len += n;
        }
        size_t used = end + 4 - buf;
        buf[used - 2] = '\0';                    // 只保留到第一个 \r\n\r\n 的前半部分
        int keep = handle(fd, buf);
        memmove(buf, buf + used, len - used);    // 同一个连接上可能已经收到了下一个请求
        len -= used;
        if (!keep) break;
    }
done:
    close(fd);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "用法：%s 端口 网站目录\n", argv[0]);
        return 2;
    }
    root = argv[2];
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(atoi(argv[1]));
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) == -1 || listen(lfd, 1024) == -1) {
        perror("bind/listen");
        return 1;
    }
    printf("minihttpd 监听 %s 端口，网站目录 %s\n", argv[1], root);
    fflush(stdout);
    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd == -1) continue;
        pthread_t t;
        if (pthread_create(&t, NULL, serve, (void *)(long)cfd) != 0) {
            close(cfd);
            continue;
        }
        pthread_detach(t);
    }
}
```

```bash
gcc -Wall -Wextra -O2 -pthread minihttpd.c -o minihttpd
```

编译没有警告。下面分几点说明。

### 2.1 读出一个完整的请求

TCP 是字节流，没有消息边界（《TCP 的常见问题》中的「粘包」）。一次 `recv` 可能只读到请求的前半部分，也可能读到一个请求再加上下一个请求的开头。所以 `serve` 不能「`recv` 一次，处理一次」，而要：

- 把收到的数据**追加**到缓冲区，直到其中出现 `\r\n\r\n`（头部结束的空行）；
- 处理这个请求后，把**空行之后**剩余的数据用 `memmove` 移到缓冲区开头，它们属于下一个请求；
- 缓冲区满了还没有看到空行，说明头部太大，回复 `431` 并关闭连接，不让客户端无限制地占用内存。

`buf[used - 2] = '\0'` 把第二个 `\r\n` 截掉，只保留到最后一个头部行的 `\r\n`，这样 `handle` 中用 `\r\nConnection: close` 这样的字符串查找头部时，最后一行也带着前面的 `\r\n`。

这里没有处理请求正文：只支持 `GET` 和 `HEAD`，它们没有正文。如果要支持 `POST`，还要按 `Content-Length` 再读那么多字节（或者按分块编码读）。

### 2.2 解析请求行，找到文件

`sscanf` 的 `%7s %1023s %15s` 限定了每个字段的最大长度，不会溢出 `method`、`target`、`version`（C 语言专栏《字符串》中的缓冲区溢出）。然后：

- 去掉 `?` 之后的查询字符串，`/index.html?v=2` 和 `/index.html` 是同一个文件；
- 拒绝不以 `/` 开头、或者包含 `..` 的路径。否则 `GET /../../../etc/passwd` 会被拼成 `www/../../../etc/passwd`，读到网站目录之外的文件，这就是**路径穿越**（path traversal）；
- 路径以 `/` 结尾时补上 `index.html`；
- 打开文件，`fstat` 得到大小，`S_ISREG` 排除目录等非普通文件。

### 2.3 发送响应

响应头部中最重要的是：

- `Content-Type`：按扩展名决定。浏览器靠它判断怎样处理正文，CSS 文件如果以 `text/plain` 发送，浏览器会拒绝把它当作样式表；
- `Content-Length`：正文的字节数。在持久连接上，客户端靠它知道这个响应在哪里结束、下一个响应从哪里开始；
- `Connection`：告诉客户端这个连接是保持还是关闭。

正文用 `sendfile` 发送。用 `read` 和 `write` 也可以，但数据要从内核（页缓存）复制到用户空间的缓冲区，再复制回内核（套接字缓冲区）；`sendfile` 让内核直接把文件内容送到套接字，省掉了两次复制和来回的系统调用。它和 `write` 一样可能只发出一部分，所以放在循环中，`off` 会被自动推进。

`HEAD` 和 `GET` 一样处理，只是不发正文，头部中的 `Content-Length` 仍然是文件的大小。

`send` 加了 `MSG_NOSIGNAL`。客户端可能在服务器发送时已经关闭了连接（浏览器停止加载、用户关掉页面），这时向套接字写入会使进程收到 `SIGPIPE`，默认动作是**终止整个进程**。加上这个标志后，`send` 只是返回 -1，这个线程关闭连接就行了，不影响其他连接。`sendfile` 没有这个标志参数；不过它总是在 `send` 发出头部之后才调用，对方已经关闭时，通常 `send` 就先失败了。更稳妥的做法是在 `main` 开头用 `signal(SIGPIPE, SIG_IGN)` 忽略这个信号（《信号》）。

`MSG_MORE` 的作用见第 4 节。

### 2.4 持久连接

`handle` 返回是否保持连接：

- HTTP/1.1 默认保持连接，除非请求中有 `Connection: close`；
- HTTP/1.0 默认关闭，除非请求中有 `Connection: keep-alive`（后面的 ab 就是这样的客户端）。

保持连接时，`serve` 回到循环开头，在同一个连接上读下一个请求，直到客户端关闭连接（`recv` 返回 0）。

## 3. 测试

在 s 中启动服务器：

```bash
ip netns exec s ./minihttpd 8000 www
```

```
minihttpd 监听 8000 端口，网站目录 www
```

在 c 中用 curl 逐个请求，打印状态码、类型和正文的大小：

```
GET /            → 200  text/html; charset=utf-8  399 字节
GET /style.css   → 200  text/css; charset=utf-8  116 字节
GET /logo.svg    → 200  image/svg+xml  226 字节
GET /notes.txt   → 200  text/plain; charset=utf-8  29 字节
GET /nope.html   → 404  text/plain; charset=utf-8  14 字节
```

其他方法和路径穿越（`--path-as-is` 让 curl 原样发送路径，不自己先把 `..` 化简掉）：

```bash
curl -d x=1 http://10.0.2.2:8000/
curl --path-as-is http://10.0.2.2:8000/../../../etc/passwd
```

```
POST /           → 405
403 Forbidden
```

`HEAD` 请求只返回头部：

```bash
curl -I http://10.0.2.2:8000/notes.txt
```

```
HTTP/1.1 200 OK
Server: minihttpd
Content-Type: text/plain; charset=utf-8
Content-Length: 29
Connection: keep-alive
```

一条 curl 命令请求两个 URL，`-v` 可以看到第二个请求复用了第一个连接：

```
* Connected to 10.0.2.2 (10.0.2.2) port 8000
* Re-using existing connection with host 10.0.2.2
```

再用 nc 一次写入两个请求，测试 2.1 节中「一次收到多个请求」的处理，第二个请求带 `Connection: close`：

```bash
printf "GET /notes.txt HTTP/1.1\r\nHost: x\r\n\r\nGET /nope HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n" | nc 10.0.2.2 8000
```

```
HTTP/1.1 200 OK
Server: minihttpd
Content-Type: text/plain; charset=utf-8
Content-Length: 29
Connection: keep-alive

minihttpd 的文本文件。
HTTP/1.1 404 Not Found
Content-Type: text/plain; charset=utf-8
Content-Length: 14
Connection: close

404 Not Found
```

两个请求很可能在同一个报文段中到达，服务器依次回答，回答完第二个后关闭连接，nc 随之退出。

最后用浏览器访问。把服务器放在一个映射了端口的容器中，在宿主机的浏览器中打开 `http://localhost:8088/`：

![浏览器中打开 minihttpd 提供的页面](/img/net/minihttpd-browser.png)

浏览器先请求 `/`，解析页面后再请求 `/style.css` 和 `/logo.svg`，三个请求都是 200，样式和图片都正确显示了。点「一个不存在的页面」会看到 `404 Not Found`。

## 4. 压测：keep-alive 反而变慢

ab（ApacheBench）是一个简单的 HTTP 压测工具：`-n` 是请求总数，`-c` 是并发数（同时有多少个请求在进行），`-k` 开启持久连接。先不开 `-k`，每个请求都新建一个连接：

```bash
ab -q -n 2000 -c 10 http://10.0.2.2:8000/
```

```
并发 10 ：14452.02 请求/秒，平均 0.692 毫秒，失败 0
```

每个请求都要握手、挥手，持久连接应该更快才对。加上 `-k`：

```
并发 10 （keep-alive）：221.69 请求/秒，平均 45.108 毫秒，失败 0，Keep-Alive requests:    2000
```

2000 个请求确实都用了持久连接，每个请求却从 0.7 毫秒变成了 45 毫秒，慢了 60 多倍。45 毫秒这个数字，在《TCP 的常见问题》中见过。

> 这里的结果对应的是第 2 节代码中**没有** `MSG_MORE` 的版本（`send_all` 不带 `more` 参数，头部和正文分别发送）。最初的版本还只在 HTTP/1.1 时保持连接，而 ab 发送的是 `HTTP/1.0` 加上 `Connection: Keep-Alive`，于是 `-k` 根本没有生效，这是 2.4 节中 HTTP/1.0 那条规则的来历。

用 `ab -k -n 6 -c 1` 只发 6 个请求，在 s 上抓包（`-ttt` 显示和上一个包的时间间隔）：

```
 00:00:00.000299 IP 10.0.1.2.50706 > 10.0.2.2.8000: Flags [P.], seq 1:106, ack 1, ... length 105
 00:00:00.000024 IP 10.0.2.2.8000 > 10.0.1.2.50706: Flags [.], ack 106, ... length 0
 00:00:00.000085 IP 10.0.2.2.8000 > 10.0.1.2.50706: Flags [P.], seq 1:124, ack 106, ... length 123
 00:00:00.000031 IP 10.0.1.2.50706 > 10.0.2.2.8000: Flags [.], ack 124, ... length 0
 00:00:00.000013 IP 10.0.2.2.8000 > 10.0.1.2.50706: Flags [P.], seq 124:523, ack 106, ... length 399
 00:00:00.000020 IP 10.0.1.2.50706 > 10.0.2.2.8000: Flags [.], ack 523, ... length 0
 00:00:00.000088 IP 10.0.1.2.50706 > 10.0.2.2.8000: Flags [P.], seq 106:211, ack 523, ... length 105
 00:00:00.000029 IP 10.0.2.2.8000 > 10.0.1.2.50706: Flags [P.], seq 523:646, ack 211, ... length 123
 00:00:00.043929 IP 10.0.1.2.50706 > 10.0.2.2.8000: Flags [.], ack 646, ... length 0
 00:00:00.000031 IP 10.0.2.2.8000 > 10.0.1.2.50706: Flags [P.], seq 646:1045, ack 211, ... length 399
```

每个响应被分成了两个报文段：123 字节的头部（`send`）和 399 字节的正文（`sendfile`）。

- 第一个请求（前 6 行）：头部发出后，客户端立即确认，正文紧接着发出，一切正常；
- 第二个请求（后 4 行）：头部发出后，客户端过了 **43.9 毫秒**才确认，正文直到收到这个确认才发出。之后的每个请求都一样，间隔在 40～50 毫秒。

这正是 Nagle 算法和延迟确认在互相等待：

- 服务器一侧，头部还没有被确认，Nagle 算法扣住了 399 字节的正文（它不够一个满的报文段）；
- 客户端一侧，收到头部后响应还不完整，它没有数据要发，于是推迟了确认，等到延迟确认的计时器到期（约 40 毫秒）才发出。

第一个请求不受影响，是因为连接刚建立时接收方处于「快速确认」模式。不开 `-k` 时每个请求都是新连接，永远在快速确认模式中，所以反而没有问题。

《TCP 的常见问题》中是**客户端**把请求分两次发送；这里是**服务器**把响应分两次发送，原理完全一样。

### 4.1 用 MSG_MORE 合并

解决办法是让头部和正文一起发出。可以把文件读进内存，和头部拼在一个缓冲区中再 `send`，但那样就失去了 `sendfile` 的好处。Linux 提供了 `MSG_MORE` 标志：它告诉内核「后面紧接着还有数据」，这次的数据先留在发送缓冲区中，不要单独发出，和后面的数据一起组成报文段。这就是第 2 节代码中 `send_all` 的 `more` 参数：

- 发送头部时，如果后面还有正文（`GET` 并且文件不为空），带上 `MSG_MORE`；
- `sendfile` 没有带这个标志，它写入正文后，内核把头部和正文一起发出。

`HEAD` 请求和空文件的头部后面没有数据了，不能带 `MSG_MORE`，否则头部会被扣住，要等内核的计时器到期才发出：实测给 `HEAD` 也带上 `MSG_MORE`，`curl -I` 从 0.0008 秒变成了 0.21 秒。

再抓包：

```
IP 10.0.1.2.49542 > 10.0.2.2.8000: Flags [P.], seq 106:211, ack 523, ... length 105
IP 10.0.2.2.8000 > 10.0.1.2.49542: Flags [P.], seq 523:1045, ack 211, ... length 522
IP 10.0.1.2.49542 > 10.0.2.2.8000: Flags [P.], seq 211:316, ack 1045, ... length 105
IP 10.0.2.2.8000 > 10.0.1.2.49542: Flags [P.], seq 1045:1567, ack 316, ... length 522
```

每个响应只有一个 522 字节（123 + 399）的报文段，客户端收到后马上发出下一个请求，确认捎带在请求中。`ab -c 1` 的平均时间从 38.059 毫秒降到了 0.353 毫秒。

同样的压测，加上 `-k` 前后：

```
并发 10 ：14488.76 请求/秒，平均 0.690 毫秒，失败 0
并发 10 （keep-alive）：36254.87 请求/秒，平均 0.276 毫秒，失败 0，Keep-Alive requests:    2000
```

持久连接终于快了，吞吐量是不开时的 2.5 倍。

### 4.2 加上网络延迟

上面的服务器和客户端之间几乎没有延迟，节省的主要是建立、关闭连接的系统调用和内核开销。在路由器 r 上加 5 毫秒的延迟（`ping` 测得往返约 6 毫秒），再测：

```
并发 10 ：902.13 请求/秒，平均 11.085 毫秒，失败 0
并发 10 （keep-alive）：1412.04 请求/秒，平均 7.082 毫秒，失败 0，Keep-Alive requests:    2000
```

不开 `-k` 时每个请求约 11 毫秒，是**两个往返**：一个用于三次握手，一个用于请求和响应。开了 `-k` 后约 7 毫秒，只剩**一个往返**。这正是《HTTP/1.1》中说的持久连接的意义：网络延迟越大，省下的握手越值钱（如果再加上 TLS 握手，省得更多）。

### 4.3 提高并发数

最后用 `-k`，每次 50000 个请求，把并发数从 1 提高到 1000：

| 并发数 | 请求/秒 | 平均时间 | 99% 的请求在 | 失败 |
|---|---|---|---|---|
| 1 | 15034 | 0.067 毫秒 | 0 毫秒内 | 0 |
| 10 | 36539 | 0.274 毫秒 | 0 毫秒内 | 0 |
| 100 | 36460 | 2.743 毫秒 | 3 毫秒内 | 0 |
| 1000 | 31330 | 31.919 毫秒 | 235 毫秒内 | 0 |

（ab 的百分位以毫秒为单位取整，0 表示不到 1 毫秒。）

- 并发数为 1 时，同一时刻只有一个请求，服务器和客户端轮流等待，2 个 CPU 核没有用满；
- 并发数到 10 时，吞吐量达到约 36000 请求/秒，再增加并发数也不会提高：容器只有 2 个 CPU 核，而且 ab 本身也是单线程的，要和服务器争抢 CPU；
- 并发数继续增加，每个请求只能排队等待，平均时间随之线性增长（100 个请求同时排队，每个就要等约 100 倍的时间）；
- 1000 个并发时服务器有 1001 个线程（压测进行中查看 `/proc/PID/task`），吞吐量下降了约 14%，最慢的 1% 的请求等了超过 235 毫秒。线程之间的切换和调度开始变得明显，这就是上一篇用 epoll 的理由。

1000 个并发、50000 个请求，没有一个失败。对一个不到 200 行的程序来说，表现还不错。

## 5. 和真正的服务器相比

minihttpd 能为浏览器提供一个静态网站，但离 nginx 这样的服务器还差得很远：

- **并发模型**：nginx 用几个工作进程（通常和 CPU 核数相同），每个进程中一个 epoll 事件循环，结合了 4.3 篇的多进程和 4.4 篇的 IO 多路复用；
- **超时**：客户端连上后什么都不发，这个线程就一直等着（见练习 3）。nginx 对读取请求头部、读取正文、空闲的持久连接都有超时；
- **协议**：没有处理请求正文、分块编码、`Range`（断点续传）、`If-Modified-Since`（缓存验证）、压缩，也没有 HTTPS 和 HTTP/2；
- **路径**：只是简单地拒绝包含 `..` 的路径。正确的做法是对 URL 解码（`%2e%2e` 也是 `..`）后规范化路径，再检查它是否仍在网站目录之内（见练习 2）；
- **其他**：访问日志、配置文件、反向代理、虚拟主机……

但它的骨架和真正的服务器是一样的：在字节流上划分请求，解析，查找资源，发回带 `Content-Length` 的响应，在持久连接上循环。

## 6. 常见错误

- **以为一次 `recv` 就是一个请求**：请求可能分几次到达，也可能和下一个请求连在一起。必须找到 `\r\n\r\n` 才算头部完整，处理后保留多余的数据；
- **忘记 `Content-Length`**：持久连接上，客户端无法知道响应在哪里结束，只能一直等待；
- **直接拼接路径**：`GET /../../etc/passwd` 会读到网站目录之外的文件；
- **没有处理 `SIGPIPE`**：一个客户端提前关闭连接，整个服务器进程就被终止了；
- **把一个响应分成几次小的写入**：在持久连接上遇到 Nagle 算法和延迟确认，每个请求多出 40 毫秒。用 `MSG_MORE`、`TCP_CORK`，或者一次写出（`writev`）；
- **压测结果不合常理时不深究**：「keep-alive 更慢」显然不对，抓包才找到原因。

## 7. 小结

- HTTP 服务器的核心循环：读到空行为止得到请求头部，解析请求行，映射到文件，发回状态行、头部和正文，持久连接时继续读下一个请求
- `Content-Type` 决定浏览器怎样处理正文，`Content-Length` 让客户端在持久连接上划分响应
- `sendfile` 让内核直接把文件发到套接字；`MSG_NOSIGNAL` 避免对方关闭时进程被 `SIGPIPE` 终止
- 必须拒绝路径穿越
- 实测：头部和正文分两次发送时，keep-alive 让每个请求多出约 45 毫秒；用 `MSG_MORE` 合成一个报文段后，吞吐量从 14000 提高到 36000 请求/秒；有 5 毫秒延迟时，keep-alive 把每个请求从两个往返减少到一个

到这里，计算机网络专栏就结束了。从第一章的分层模型开始，经过链路层、网络层、传输层、应用层，最后自己用套接字写出了一个能让浏览器访问的 HTTP 服务器。几乎每一个概念都在实验网络中抓过包、测过数字。以后再遇到网络问题，可以用第三章的工具箱一层一层地排查。

## 8. 练习

1. 不用 `MSG_MORE`，而是在 `accept` 之后对每个连接设置 `TCP_NODELAY`（关闭 Nagle 算法），能解决 45 毫秒的问题吗？和 `MSG_MORE` 相比有什么不同？
2. 网站目录中有一个文件叫 `a..b.txt`，能访问到吗？怎样改进路径检查？
3. 一个客户端连上后只发送 `GET / HTTP/1.1\r\n`，之后什么都不发，会怎样？如果有 500 个这样的客户端呢？怎样防御？

<details>
<summary>点击查看答案</summary>

1. 能解决。在没有 `MSG_MORE` 的版本上，`accept` 之后加上：

   ```c
   setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
   ```

   `ab -k -n 6 -c 1` 的平均时间是 0.153 毫秒，不再有 40 毫秒的等待。但抓包统计服务器发出的数据报文段的长度：

   ```
         6 123
         6 399
   ```

   每个响应仍然是两个报文段，只是正文不再等待确认，立即发出了。压测（`-k -c 10`，50000 个请求）的吞吐量是 26347 请求/秒，低于 `MSG_MORE` 版本的约 36000：报文段的数量多了一倍，两端的协议栈都要多处理一倍的包。`TCP_NODELAY` 解决的是「等待」，`MSG_MORE` 解决的是「分成了两个包」，后者更彻底。
2. 访问不到，`curl http://10.0.2.2:8000/a..b.txt` 返回 `403`。`strstr(target, "..")` 把所有包含两个点的路径都拒绝了，包括合法的文件名。更好的做法是只拒绝**路径段**恰好是 `..` 的情况：按 `/` 切分路径，检查每一段是否等于 `..`；或者用 `realpath` 得到文件的真实路径（它会解析 `..` 和符号链接），再检查它是否以网站目录的真实路径开头。如果支持 URL 解码，必须在**解码之后**检查，否则 `%2e%2e` 就能绕过。
3. 服务器的线程一直阻塞在 `recv` 中，等待永远不会到来的空行。用一个 Python 程序建立 500 个这样的连接：

   ```
   2 秒：线程 501，ESTAB 500
   13 秒：线程 501，ESTAB 500
   此时正常请求：200 0.000403 秒
   ```

   500 个线程被一直占用（加上主线程共 501 个），连接也不会释放。这时正常的请求还能得到响应，但如果攻击者建立更多的连接，服务器最终会耗尽线程、内存或者描述符。这种攻击叫作 **Slowloris**：用很少的带宽，把服务器的连接全部占住。

   防御的办法是设置超时。在 `accept` 之后：

   ```c
   struct timeval tv = {.tv_sec = 10};          // 10 秒内收不到数据，recv 返回 -1
   setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
   ```

   同样的测试：

   ```
   2 秒：线程 501，ESTAB 500
   13 秒：线程 1，ESTAB 0
   此时正常请求：200 0.000369 秒
   ```

   10 秒后，`recv` 超时返回 -1，所有的线程都关闭连接退出了。不过 `SO_RCVTIMEO` 是**每次** `recv` 的超时：攻击者每 9 秒发送一个字节，仍然可以一直占住连接。真正的服务器会限制读取**整个**请求头部的总时间（nginx 的 `client_header_timeout`），并限制每个 IP 的连接数。这个超时同时也关闭了空闲超过 10 秒的持久连接，相当于 nginx 的 `keepalive_timeout`。

</details>
