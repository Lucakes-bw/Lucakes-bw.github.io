---
title: 同时服务多个客户端
date: 2026-10-24 16:40:00
updated: 2026-10-24 16:40:00
categories:
  - 计算机网络
  - Socket编程
tags:
  - 计算机网络
  - Socket
  - 并发
order: 3
description: 上一篇的回声服务器一次只能服务一个客户端。本文用两种最直接的方法改造它：每个连接一个子进程，每个连接一个线程。写一个压测程序，建立 10、100、1000 个连接，每个连接往返 10 次；然后看服务器付出的代价：1000 个连接时，多进程版本有 1001 个进程，实际占用约 27 MB 内存（RSS 加起来却有 329 MB），多线程版本约 12 MB。最后尝试 10000 个连接，两种方法都撑住了，但已经能看到它们的代价和极限。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用多进程、多线程两种方式让服务器同时服务多个客户端；理解它们各自要注意的问题（僵尸进程、描述符的关闭、线程的回收）；会测量服务器的资源占用，理解 RSS、PSS、VSZ 的区别；理解 C10K 问题。
>
> **前置知识**：上一篇的 TCP Socket；Linux 基础专栏的《进程：fork、exec、wait》《信号》《POSIX 线程》。
>
> **实验环境**：Ubuntu 24.04 容器（2 个 CPU 核），GCC 13.3。服务器在 s（10.0.2.2:9000），压测程序在 c；路由器 r 上不加延迟。

## 1. 公共部分

几个服务器共用的代码放在一个头文件中：创建监听套接字，以及回声的循环：

```c
// common.h：几个服务器共用的函数
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// 创建监听 port 端口的 TCP 套接字
static inline int listen_on(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) == -1 || listen(fd, 4096) == -1) {
        perror("bind/listen");
        exit(1);
    }
    return fd;
}

// 回声：读到什么写回什么，直到对方关闭
static inline void echo(int fd)
{
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (ssize_t off = 0; off < n; ) {
            ssize_t w = write(fd, buf + off, n - off);
            if (w <= 0) return;
            off += w;
        }
    }
}
```

## 2. 多进程

每接受一个连接，就 `fork` 一个子进程专门服务它：

```c
// echo_fork.c：每个连接一个子进程
#include <signal.h>
#include <sys/wait.h>
#include "common.h"

static void reap(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {     // 回收所有已经结束的子进程，避免僵尸
    }
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = reap;
    sa.sa_flags = SA_RESTART;                    // 被 SIGCHLD 打断的 accept 自动重启
    sigaction(SIGCHLD, &sa, NULL);

    int lfd = listen_on(9000);
    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd == -1) continue;
        pid_t pid = fork();
        if (pid == 0) {                          // 子进程：只服务这一个客户端
            close(lfd);                          // 子进程不需要监听套接字
            echo(cfd);
            _exit(0);
        }
        close(cfd);                              // 父进程：连接交给了子进程，自己关掉这个描述符
    }
}
```

要注意的地方：

- **描述符的关闭**：`fork` 之后，父子进程都有 `lfd` 和 `cfd`（《进程：fork、exec、wait》）。子进程关闭 `lfd`，它不接受新连接；父进程必须关闭 `cfd`，否则连接的引用计数不会归零，子进程关闭后连接也不会真正关闭（客户端收不到 FIN），父进程的描述符还会越积越多。这和 Linux 基础专栏《重定向与管道的原理》中「忘了关闭管道的写端」是同一个道理；
- **僵尸进程**：子进程结束后要有人 `wait` 它。用 SIGCHLD 的处理函数循环调用 `waitpid(-1, NULL, WNOHANG)`（《信号》中实测过，同类信号不排队，一定要循环）；
- **`SA_RESTART`**：父进程大部分时间阻塞在 `accept` 中，SIGCHLD 会打断它，设置 `SA_RESTART` 让它自动重新开始。

## 3. 多线程

每个连接一个线程：

```c
// echo_thread.c：每个连接一个线程
#include <pthread.h>
#include "common.h"

static void *serve(void *arg)
{
    int fd = (int)(long)arg;
    echo(fd);
    close(fd);
    return NULL;
}

int main(void)
{
    int lfd = listen_on(9000);
    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd == -1) continue;
        pthread_t t;
        if (pthread_create(&t, NULL, serve, (void *)(long)cfd) != 0) {   // 把描述符的值传给线程
            close(cfd);
            continue;
        }
        pthread_detach(t);                       // 不需要 join，线程结束后自动回收
    }
}
```

- 描述符的值直接通过 `void *` 参数传给线程（转换成 `long` 再转换），每个线程得到的是自己的副本。不能传 `&cfd`，因为主线程马上就会用 `cfd` 存放下一个连接（《POSIX 线程》中「传参的陷阱」）；
- `pthread_detach` 让线程结束后自动回收资源，主线程不需要 `pthread_join`；
- 线程共享所有的描述符，不需要像多进程那样关闭对方的描述符。

## 4. 压测

写一个压测程序：建立 N 个连接并一直保持，然后进行若干轮，每一轮在每个连接上发送 64 字节、读回 64 字节：

```c
// bench.c 服务器IP 连接数 轮数：建立 N 个连接并保持住，每一轮在每个连接上发送 64 字节、读回 64 字节
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "用法：%s 服务器IP 连接数 轮数\n", argv[0]);
        return 2;
    }
    int n = atoi(argv[2]), rounds = atoi(argv[3]);
    int *fds = malloc(n * sizeof *fds);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(9000);
    inet_pton(AF_INET, argv[1], &a.sin_addr);

    double t0 = now();
    for (int i = 0; i < n; i++) {
        fds[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(fds[i], (struct sockaddr *)&a, sizeof a) == -1) {
            perror("connect");
            return 1;
        }
    }
    double t1 = now();
    char msg[64], buf[64];
    memset(msg, 'x', sizeof msg);
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < n; i++) {
            if (write(fds[i], msg, sizeof msg) != sizeof msg) return 1;
            for (size_t got = 0; got < sizeof buf; ) {
                ssize_t k = read(fds[i], buf + got, sizeof buf - got);
                if (k <= 0) return 1;
                got += k;
            }
        }
    }
    double t2 = now();
    printf("%d 个连接：建立用时 %.3f 秒；%d 次往返用时 %.3f 秒，平均每次 %.1f 微秒\n",
           n, t1 - t0, n * rounds, t2 - t1, (t2 - t1) / (n * rounds) * 1e6);
    if (getenv("HOLD")) {                        // 设置了 HOLD 时保持连接，便于观察服务器
        printf("保持连接 %s 秒\n", getenv("HOLD"));
        fflush(stdout);
        sleep(atoi(getenv("HOLD")));
    }
    return 0;
}
```

先拿上一篇的单客户端服务器试一下，10 个连接、1 轮：

```
=== 一次服务一个客户端（第 4.1 篇）
  bench exit=124（124 表示 5 秒后被 timeout 终止）
```

卡住了：服务器在第一个连接的 `read` 中等待，而 bench 在等第一个连接的回声之后，还要依次处理后面 9 个连接，服务器永远不会 `accept` 它们。

多进程和多线程的服务器（每种情况在保持连接时测量服务器的进程数和内存）：

```
10 个连接：建立用时 0.001 秒；100 次往返用时 0.010 秒，平均每次 97.4 微秒
  echo_fork：进程 11 个，内存（RSS 之和）4 MB
100 个连接：建立用时 0.009 秒；1000 次往返用时 0.063 秒，平均每次 63.3 微秒
  echo_fork：进程 101 个，内存（RSS 之和）34 MB
1000 个连接：建立用时 0.056 秒；10000 次往返用时 0.473 秒，平均每次 47.3 微秒
  echo_fork：进程 1001 个，内存（RSS 之和）329 MB
10 个连接：建立用时 0.001 秒；100 次往返用时 0.008 秒，平均每次 83.0 微秒
  echo_thread：线程 11 个，内存（RSS）1 MB
100 个连接：建立用时 0.007 秒；1000 次往返用时 0.065 秒，平均每次 65.4 微秒
  echo_thread：线程 101 个，内存（RSS）2 MB
1000 个连接：建立用时 0.045 秒；10000 次往返用时 0.455 秒，平均每次 45.5 微秒
  echo_thread：线程 1001 个，内存（RSS）13 MB
```

每次往返的时间，两种服务器几乎一样，都在 50～100 微秒。这个压测中客户端是一个一个连接依次请求的，服务器同一时刻只有一个连接在忙，体现不出两者的差别。差别在**资源**上：1000 个连接时，多进程版本有 1001 个进程，多线程版本有 1001 个线程。

### 4.1 RSS、PSS 和 VSZ

多进程版本的 RSS 之和是 329 MB，多线程版本只有 13 MB，相差 25 倍？这个比较并不公平。**RSS**（常驻内存）统计的是一个进程用到的所有物理页，**包括和其他进程共享的页**：1001 个进程都映射了同一份 libc、同一份程序代码，以及 fork 之后写时复制、还没有被修改的页（《进程：fork、exec、wait》），这些页在 RSS 中被计算了 1001 次。

**PSS**（按比例分摊的内存）把共享的页按共享的进程数平分，加起来才是真实的占用。`/proc/PID/smaps_rollup` 中有它。1000 个连接时再测一次：

```
echo_fork：PSS 之和 27.4 MB，RSS 之和 329 MB，VSZ 之和 2072 MB
echo_thread：PSS 之和 12.2 MB，RSS 之和 13 MB，VSZ 之和 8065 MB
```

- 多进程：真实的占用约 27 MB，每个连接约 **28 KB**（主要是每个进程的页表、内核中的进程结构、各自修改过的页）；
- 多线程：约 12 MB，每个连接约 **12 KB**；
- **VSZ**（虚拟内存）：多线程版本有 8 GB！每个线程默认有 8 MB 的栈（`ulimit -s`），1000 个线程就是 8 GB 的虚拟地址空间。但栈只在真正被使用时才分配物理页（《内存映射 mmap》中的「按需加载」），每个线程只用到了栈顶的几 KB。

用 `ps` 看内存时，要清楚看的是哪一种。

## 5. C10K

1999 年，Dan Kegel 提出了 **C10K 问题**：一台服务器怎样同时处理 1 万个连接？用 `ulimit -n 30000` 放宽描述符的限制，试一下 10000 个连接、每个往返 2 次：

```
10000 个连接：建立用时 0.409 秒；20000 次往返用时 1.001 秒，平均每次 50.0 微秒
  echo_thread：1 个进程，PSS 之和 120 MB
10000 个连接：建立用时 2.104 秒；20000 次往返用时 0.986 秒，平均每次 49.3 微秒
  echo_fork：10001 个进程，PSS 之和 274 MB
```

两种方法都撑住了，现在的机器比 1999 年强大得多。但可以看到它们的代价：

- **建立连接的时间不可比**：这一次多进程用了 2.1 秒、多线程用了 0.4 秒，但重复实验时，同一个服务器建立 10000 个连接的时间在 0.4～4.9 秒之间大幅波动。主要原因在客户端：之前的实验留下了大量 TIME_WAIT 的连接（第 2.2 篇），客户端要在剩下的本地端口中寻找可用的，所以这个时间主要反映的是客户端的状况，不能用来比较服务器；
- **内存**：多进程 274 MB，多线程 120 MB，都随连接数线性增长；
- **上限**：这台机器 `/proc/sys/kernel/threads-max` 只有 15325，多线程版本再多几千个连接就会创建线程失败；进程数同样受这个限制。如果连接数是 10 万、100 万，这两种方法都无法胜任；
- **调度**：真实的服务器中，大多数连接在大多数时间是空闲的（比如等待用户的下一个请求），却各自占着一个进程或线程。当很多连接同时活跃时，几千个线程之间的上下文切换会消耗大量的 CPU 时间。

问题的根源在于：「每个连接一个执行流」意味着用**阻塞**的 `read` 等待数据，一个执行流只能等一个连接。下一篇的 **IO 多路复用**换了一种思路：用**一个**线程同时等待所有的连接，哪个连接有数据就处理哪个。

## 6. 常见错误

1. **多进程服务器的父进程不关闭连接的描述符**：描述符泄漏，客户端收不到 FIN。
2. **不回收子进程**：大量僵尸进程，最终无法 fork。
3. **把 `&cfd` 传给线程**：多个线程拿到同一个地址，下一个连接到来时值就被改掉了。
4. **线程既不 join 也不 detach**：线程结束后资源无法回收。
5. **用 RSS 之和衡量多进程程序的内存**：共享的页被重复计算，用 PSS。
6. **忘了 `ulimit -n`**：默认 1024 个描述符，连接数一多就 `accept` 失败（`Too many open files`）。

## 7. 小结

- 多进程：每个连接 fork 一个子进程；父进程关闭连接描述符，子进程关闭监听描述符，用 SIGCHLD 循环回收子进程
- 多线程：每个连接一个线程，通过参数传递描述符的值，detach 线程
- 实测 1000 个连接：两者的往返时间相同（约 50 微秒）；真实内存（PSS）多进程约 27 MB，多线程约 12 MB；RSS 之和会严重高估多进程的内存
- 实测 10000 个连接都能支撑，但内存随连接数线性增长（多进程 274 MB，多线程 120 MB），线程数受 `threads-max`（15325）限制
- 「每个连接一个执行流」难以扩展到更多的连接，这是 C10K 问题的核心

## 8. 练习

1. 去掉多进程服务器中父进程的 `close(cfd)`，运行 bench 后，服务器的父进程有多少个打开的描述符？客户端断开后，连接处于什么状态？
2. 多线程服务器中，用 `pthread_attr_setstacksize` 把线程的栈改小，VSZ 会怎样变化？会带来什么风险？
3. 有一种折中的做法叫**线程池**：预先创建固定数量（比如 8 个）的线程，主线程把接受的连接放进一个队列，空闲的线程从队列中取出来处理。它解决了哪些问题，又有什么局限？

<details>
<summary>点击查看答案</summary>

1. 父进程不关闭 `cfd`，100 个连接之后：

   ```
   父进程 59765 的描述符个数：104
   s 上 9000 端口的连接状态：
       100 CLOSE-WAIT
   c 上的连接状态：
       100 FIN-WAIT-2
   ```

   （省略了之前实验留下的 TIME-WAIT。）父进程多了 100 个描述符（0、1、2、监听套接字，加上 100 个连接）。客户端关闭后，子进程读到 0、关闭自己的描述符并退出，但父进程还拿着同一个连接的描述符，引用计数没有归零，内核不会发出 FIN：服务器一端停在 **CLOSE-WAIT**，客户端一端停在 **FIN-WAIT-2**，和第 2.2 篇讲的「大量 CLOSE_WAIT 一定是程序的 bug」完全一致。父进程的描述符会一直增长，直到 1024 个用完。
2. 我最初试的是 64 KB，结果 VSZ 仍然是 8 GB，什么都没变。原因是 `pthread_attr_setstacksize` 返回了 `EINVAL`：这台 aarch64 机器上 `PTHREAD_STACK_MIN` 是 131072（128 KB），64 KB 小于下限，设置失败，而我没有检查返回值，线程用的还是默认的 8 MB。改成 256 KB 并检查返回值：

   ```c
   pthread_attr_t attr;
   pthread_attr_init(&attr);
   int err = pthread_attr_setstacksize(&attr, 256 * 1024);   // 每个线程的栈只有 256 KB
   if (err != 0) {
       fprintf(stderr, "pthread_attr_setstacksize: %s\n", strerror(err));
       return 1;
   }
   ...
   pthread_create(&t, &attr, serve, (void *)(long)cfd)
   ```

   ```
   256 KB 栈：线程 1001 个，VSZ 314 MB，PSS 12.3 MB
   ```

   VSZ 从 8 GB 降到了 314 MB，PSS（真实的占用）没有变化，仍然是 12.3 MB：栈本来就只用到了很少的一部分。VSZ 小了，在 32 位系统或者限制了虚拟内存的环境中能创建更多的线程。风险是：如果线程中有深的递归或者大的局部数组，栈不够用就会溢出，进程收到 SIGSEGV 崩溃。
3. 线程池避免了为每个连接创建、销毁线程的开销，也限制了线程的总数，不会因为连接太多而耗尽资源（队列就是 Linux 基础专栏《线程同步》中的生产者消费者模型）。局限是：如果每个线程用阻塞的方式处理一个连接直到它断开，8 个线程就只能同时服务 8 个连接，第 9 个连接要等前面的断开。所以线程池适合处理「短任务」（一个请求处理完就把线程还回去），通常和下一篇的 IO 多路复用结合使用：一个线程负责等待哪些连接有数据，再把具体的请求交给线程池处理。

</details>
