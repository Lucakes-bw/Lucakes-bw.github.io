---
title: 进程间通信
date: 2026-10-24 11:00:00
updated: 2026-10-24 11:00:00
categories:
  - Linux基础
  - 进程与线程
tags:
  - Linux
  - IPC
  - 共享内存
order: 3
description: 进程的内存互相隔离，要交换数据就需要进程间通信（IPC）。本文介绍命名管道 FIFO、Unix 域套接字和 POSIX 共享内存，实测父子进程不加同步地修改共享的计数器，200 万次加法只剩下不到 100 万；再让子进程向父进程传送 1 GB 数据，比较管道、套接字和共享内存的速度：块大小为 4 KB 时共享内存反而最慢，只有 317 MB/s，1 MB 时它才变成最快。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用命名管道（FIFO）在不相关的进程之间传递数据；了解 Unix 域套接字；会用 `shm_open` 和 `mmap` 创建共享内存；理解共享内存需要同步，会用信号量；通过实测理解各种 IPC 方式的速度差异从哪里来。
>
> **前置知识**：《重定向与管道的原理》中的管道；《内存映射 mmap》中的 `MAP_SHARED`；上一篇的信号也是一种进程间通信，只是只能传递一个编号。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64，2 个 CPU 核），GCC 13.3。

## 1. 为什么需要 IPC

《进程：fork、exec、wait》中看到，`fork` 之后父子进程的变量互不影响：每个进程有自己独立的地址空间。这种隔离保证了一个进程的错误不会破坏另一个进程，但进程之间要合作，就需要内核提供的**进程间通信**（Inter-Process Communication，IPC）机制：

| 方式 | 特点 | 本专栏中 |
|---|---|---|
| 管道 | 单向的字节流，只能在有亲缘关系的进程之间使用 | 《重定向与管道的原理》 |
| 命名管道（FIFO） | 有文件名的管道，任意进程都可以打开 | 本文第 2 节 |
| Unix 域套接字 | 双向的，可以传送字节流或消息 | 本文第 3 节 |
| 共享内存 | 多个进程映射同一块物理内存 | 本文第 4 节 |
| 信号 | 只能传递一个编号 | 《信号》 |
| 信号量 | 用于同步，不传递数据 | 本文第 4 节 |

## 2. 命名管道 FIFO

普通的管道没有名字，只能通过 `fork` 把描述符传给子进程。**命名管道**（FIFO）在文件系统中有一个名字，任何有权限的进程都可以用 `open` 打开它：

```bash
mkfifo myfifo
ls -l myfifo
```

```
prw-r--r-- 1 lucky lucky 0 Oct 24 03:03 myfifo
```

类型是 `p`（《文件属性与目录》中的 `S_ISFIFO`），大小是 0：数据不会写到磁盘上，仍然在内核的管道缓冲区中。在 C 程序中用 `mkfifo(path, mode)` 创建。

一个进程 1 秒后向它写入，另一个进程立即开始读：

```bash
(sleep 1; echo "来自另一个进程的消息" > myfifo; echo "[writer] 写完了") &
date +%T.%N | cut -c1-12; cat myfifo; date +%T.%N | cut -c1-12
```

```
03:03:05.107
[writer] 写完了
来自另一个进程的消息
03:03:06.112
```

`cat` 打开 FIFO 时**阻塞**了 1 秒，直到另一端有人打开、写入数据。FIFO 的 `open` 是同步的：**只读打开会等到有写端，只写打开会等到有读端**。所以只有写端时：

```bash
timeout 2 bash -c "echo hi > myfifo"; echo "exit=$?"
```

```
exit=124
```

`echo` 一直卡在 `open` 上，直到 2 秒后被 `timeout` 终止。打开之后，FIFO 的行为和普通管道完全相同：容量 64 KB、读端关闭后写会收到 SIGPIPE、所有写端关闭后读到文件结束。

FIFO 适合简单的场合，比如一个程序把日志写进 FIFO，另一个程序读取并处理。它是单向的，需要双向通信时可以用两个 FIFO，或者用套接字。

## 3. Unix 域套接字

**套接字**（socket）是网络编程的基础，计算机网络专栏会详细讲。它也可以用于同一台机器上的进程间通信，这种叫 **Unix 域套接字**（`AF_UNIX`）。和管道相比，它是**双向**的，还可以传送有边界的消息（`SOCK_DGRAM`），甚至可以在进程之间传递文件描述符。

最简单的用法是 `socketpair`，它像 `pipe` 一样一次创建两个描述符，但两端都可以读写：

```c
int sv[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, sv);   // sv[0] 和 sv[1] 都可读可写
```

不相关的进程之间，可以像 FIFO 一样用一个文件路径作为地址（`bind` 和 `connect`），Docker、MySQL、systemd 等很多程序都用这种方式和客户端通信。`ss -x` 列出系统中的 Unix 域套接字（`ss` 在 iproute2 软件包中，实验容器中没有安装）。

## 4. 共享内存

管道和套接字传送数据时，数据要经过内核：写端把数据从用户空间复制到内核的缓冲区，读端再复制出来。**共享内存**让多个进程把**同一块物理内存**映射到各自的地址空间，一个进程写入，另一个进程直接就能读到，不需要经过内核。

### 4.1 shm_open

POSIX 共享内存的用法和《内存映射 mmap》中映射文件几乎一样，只是用 `shm_open` 代替 `open`：

```c
// shm_writer.c：创建一块命名的共享内存，写入计数器，每秒加 1
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

struct board {
    int counter;
    char message[64];
};

int main(void)
{
    int fd = shm_open("/demo_board", O_CREAT | O_RDWR, 0600);   // 名字以 / 开头
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }
    if (ftruncate(fd, sizeof(struct board)) == -1) {
        perror("ftruncate");
        return 1;
    }
    struct board *b = mmap(NULL, sizeof *b, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    for (int i = 1; i <= 5; i++) {
        b->counter = i;
        snprintf(b->message, sizeof b->message, "writer 第 %d 次更新", i);
        sleep(1);
    }
    munmap(b, sizeof *b);
    shm_unlink("/demo_board");                   // 删除共享内存，否则它会一直存在
    return 0;
}
```

```c
// shm_reader.c：打开同一块共享内存，读出内容
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

struct board {
    int counter;
    char message[64];
};

int main(void)
{
    int fd = shm_open("/demo_board", O_RDONLY, 0);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }
    struct board *b = mmap(NULL, sizeof *b, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    printf("counter = %d，message = %s\n", b->counter, b->message);
    munmap(b, sizeof *b);
    return 0;
}
```

```bash
./shm_reader
./shm_writer &
ls -l /dev/shm
./shm_reader; sleep 2; ./shm_reader
wait; ls -l /dev/shm; ./shm_reader
```

```
shm_open: No such file or directory
total 4
-rw------- 1 lucky lucky 68 Oct 24 03:03 demo_board
counter = 1，message = writer 第 1 次更新
counter = 3，message = writer 第 3 次更新
total 0
shm_open: No such file or directory
```

- 共享内存就是 `/dev/shm` 中的一个文件。`/dev/shm` 是一个 **tmpfs** 文件系统，内容只在内存中（《文件与目录操作》中用它演示过跨文件系统的硬链接）；
- 大小 68 字节：`int` 4 个字节加 64 个字符；
- `reader` 是一个完全独立的程序，只要知道名字就能打开，看到的是 `writer` 此刻写入的内容；
- `writer` 调用 `shm_unlink` 后，共享内存被删除。如果忘了删除，它会一直占用内存，直到重启。

父子进程之间更简单：`fork` 之前用 `mmap` 创建 `MAP_SHARED | MAP_ANONYMOUS` 的映射，子进程继承的就是同一块内存，不需要名字。

### 4.2 共享内存需要同步

共享内存的速度来自于内核完全不参与，但这也意味着内核不会帮你协调多个进程的访问。父子进程同时给共享内存中的计数器加 1：

```c
// shmrace.c：父子进程同时给共享内存中的计数器加 1
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    volatile long *counter = mmap(NULL, sizeof(long), PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *counter = 0;
    pid_t pid = fork();                          // 子进程继承了这块共享的映射
    for (int i = 0; i < 1000000; i++) {
        (*counter)++;
    }
    if (pid == 0) _exit(0);
    wait(NULL);
    printf("counter = %ld（期望 2000000）\n", *counter);
    return 0;
}
```

```
counter = 1070432（期望 2000000）
counter = 1024445（期望 2000000）
counter = 1040203（期望 2000000）
counter = 1000000（期望 2000000）
counter = 927611（期望 2000000）
```

两个进程各加 100 万次，结果却只有 100 万左右，最后一次甚至比一个进程单独加的还少。`(*counter)++` 看起来是一步，实际上是三步：从内存读出、加 1、写回内存。两个进程在两个 CPU 核上同时执行时，两个进程可能读到同一个旧值，各自加 1 后写回，两次加法只生效了一次。这叫**竞争条件**（race condition），下一篇讲线程时会详细分析它的原因和几种解决方法。

在进程之间同步，可以用 **POSIX 信号量**：一个计数器，`sem_wait` 把它减 1（为 0 时等待），`sem_post` 把它加 1（唤醒等待的进程）。把信号量放在共享内存中，用 `sem_init(&sem, 1, 初值)` 初始化，第二个参数 1 表示在进程之间共享。下一节的测速程序中，用两个信号量协调生产者和消费者对一块缓冲区的使用。

## 5. 速度对比

让子进程向父进程传送 1 GB 的数据，比较管道、Unix 域套接字和共享内存：

```c
// ipcbench.c：子进程向父进程传送 1 GB 数据，比较管道、套接字和共享内存
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TOTAL (1L << 30)                         // 1 GB

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

// 管道和套接字：子进程写，父进程读
static void by_fd(int rfd, int wfd, size_t chunk)
{
    char *buf = malloc(chunk);
    memset(buf, 'x', chunk);
    if (fork() == 0) {
        close(rfd);
        for (long sent = 0; sent < TOTAL; sent += chunk) {
            if (write(wfd, buf, chunk) != (ssize_t)chunk) _exit(1);
        }
        _exit(0);
    }
    close(wfd);
    long got = 0;
    ssize_t n;
    while ((n = read(rfd, buf, chunk)) > 0) got += n;
    wait(NULL);
    if (got != TOTAL) fprintf(stderr, "只收到 %ld 字节\n", got);
    free(buf);
}

// 共享内存：一块缓冲区加两个信号量，子进程放入数据，父进程取出（复制到自己的缓冲区）
struct shared {
    sem_t empty, full;                           // 缓冲区空了 / 满了
    size_t len;
    char data[];
};

static void by_shm(size_t chunk)
{
    struct shared *sh = mmap(NULL, sizeof *sh + chunk, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(&sh->empty, 1, 1);                  // 第二个参数为 1：在进程之间共享
    sem_init(&sh->full, 1, 0);
    char *buf = malloc(chunk);
    memset(buf, 'x', chunk);
    if (fork() == 0) {
        for (long sent = 0; sent < TOTAL; sent += chunk) {
            sem_wait(&sh->empty);
            memcpy(sh->data, buf, chunk);
            sh->len = chunk;
            sem_post(&sh->full);
        }
        _exit(0);
    }
    long got = 0;
    while (got < TOTAL) {
        sem_wait(&sh->full);
        memcpy(buf, sh->data, sh->len);
        got += sh->len;
        sem_post(&sh->empty);
    }
    wait(NULL);
    free(buf);
    munmap(sh, sizeof *sh + chunk);
}

int main(int argc, char *argv[])
{
    size_t chunk = argc > 1 ? strtoul(argv[1], NULL, 10) : 65536;
    double t;
    int p[2];

    if (pipe(p) == -1) return 1;
    t = now(); by_fd(p[0], p[1], chunk); t = now() - t;
    printf("块大小 %7zu：管道     %5.2f 秒，%6.0f MB/s\n", chunk, t, 1024 / t);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) == -1) return 1;
    t = now(); by_fd(p[0], p[1], chunk); t = now() - t;
    printf("块大小 %7zu：套接字   %5.2f 秒，%6.0f MB/s\n", chunk, t, 1024 / t);

    t = now(); by_shm(chunk); t = now() - t;
    printf("块大小 %7zu：共享内存 %5.2f 秒，%6.0f MB/s\n", chunk, t, 1024 / t);
    return 0;
}
```

共享内存的版本用一块缓冲区和两个信号量：`empty` 表示缓冲区可以写入，`full` 表示缓冲区中有数据。子进程等到 `empty`，写入一块数据，通知 `full`；父进程等到 `full`，取出数据，通知 `empty`。编译时要加 `-pthread`（信号量的函数在 pthread 库中）。

每种块大小运行 3 次，结果很稳定，取第一次：

```
块大小    4096：管道      0.17 秒，  6129 MB/s
块大小    4096：套接字    0.15 秒，  6631 MB/s
块大小    4096：共享内存  3.22 秒，   318 MB/s
块大小   65536：管道      0.28 秒，  3596 MB/s
块大小   65536：套接字    0.08 秒， 13209 MB/s
块大小   65536：共享内存  0.25 秒，  4067 MB/s
块大小 1048576：管道      0.27 秒，  3585 MB/s
块大小 1048576：套接字    0.06 秒， 18330 MB/s
块大小 1048576：共享内存  0.05 秒， 21870 MB/s
```

结果和「共享内存最快」的直觉并不一致：

- **块大小为 4 KB 时，共享内存最慢**，只有 318 MB/s，比管道慢了近 20 倍。1 GB 分成 262144 块，每一块都要两个进程通过信号量交接一次：一方等待、被唤醒、处理，再唤醒另一方。平均每块花了约 12 微秒，几乎全部是交接的开销，复制 4 KB 本身用不了这么久。管道和套接字的缓冲区能容纳多块数据，写端不必等读端取走每一块，两个进程可以同时工作；
- 块越大，交接的次数越少，共享内存越快；到 1 MB 时它变成了最快的，每秒 21.9 GB；
- 管道在块大小为 64 KB 时比 4 KB 时更慢，可能是因为 64 KB 正好等于管道的容量，每次写入都把管道写满，写端必须等读端把整块读走才能继续，两个进程不能同时工作；
- 这个测试中，共享内存的版本也复制了两次（写入时一次，取出时一次），和管道一样。共享内存真正的优势是**不需要复制**：生产者直接在共享内存中构造数据，消费者直接在原处使用，比如视频处理程序在共享内存中交换一帧帧的图像。

结论：**IPC 的性能主要取决于同步的次数，而不是数据经过了几次复制**。小消息、频繁的通信，用管道或套接字更简单也更快；大块数据、能在原处处理时，共享内存的优势才能发挥出来。

## 6. 常见错误

1. **FIFO 只打开一端**：`open` 阻塞，程序看起来卡住了。
2. **忘记 `shm_unlink`**：共享内存留在 `/dev/shm` 中，占用内存直到重启。
3. **共享内存不加同步**：竞争条件，结果错误，而且每次运行都不同。
4. **在共享内存中存放指针**：两个进程映射的地址可能不同，一个进程中的指针在另一个进程中没有意义。应该存放相对于共享内存起点的偏移。
5. **`sem_init` 的第二个参数写成 0**：信号量只在线程之间共享，放在共享内存中用于进程之间时，行为未定义。
6. **以为共享内存一定最快**：小块数据、频繁交接时，同步的开销远大于复制的开销。

## 7. 小结

- 进程的地址空间是隔离的，交换数据需要 IPC：管道、FIFO、Unix 域套接字、共享内存，同步用信号量，通知用信号
- FIFO 是有名字的管道（类型 `p`），任意进程都能打开；`open` 会等到另一端也被打开
- Unix 域套接字是双向的，`socketpair` 创建一对相连的套接字
- `shm_open` + `ftruncate` + `mmap` 创建命名的共享内存，它位于 `/dev/shm`；用完要 `shm_unlink`
- 共享内存不加同步会产生竞争条件：200 万次加法只得到约 100 万
- 实测传送 1 GB：4 KB 一块时共享内存只有 318 MB/s，最慢；1 MB 一块时 21.9 GB/s，最快。同步的次数决定了性能

## 8. 练习

1. 用两个 FIFO 实现一个「回声服务器」：服务器从 `req.fifo` 读一行，转换成大写后写入 `resp.fifo`；客户端发送一行并读取结果。
2. 修改 `shmrace.c`，在共享内存中放一个初值为 1 的信号量，每次加 1 前 `sem_wait`，加 1 后 `sem_post`。结果正确了吗？速度变化了多少？
3. 为什么 `shm_reader` 用 `O_RDONLY` 打开、`PROT_READ` 映射，而 `shm_writer` 要用 `O_RDWR` 和 `PROT_READ | PROT_WRITE`？

<details>
<summary>点击查看答案</summary>

1. 服务器：

   ```c
   #include <ctype.h>
   #include <stdio.h>
   #include <sys/stat.h>

   int main(void)
   {
       mkfifo("req.fifo", 0600);                    // 已经存在时会失败，忽略即可
       mkfifo("resp.fifo", 0600);
       for (;;) {
           FILE *req = fopen("req.fifo", "r");      // 等待客户端打开写端
           FILE *resp = fopen("resp.fifo", "w");    // 等待客户端打开读端
           if (req == NULL || resp == NULL) {
               perror("fopen");
               return 1;
           }
           char line[256];
           if (fgets(line, sizeof line, req)) {
               for (char *p = line; *p; p++) *p = toupper((unsigned char)*p);
               fputs(line, resp);
           }
           fclose(req);
           fclose(resp);
       }
   }
   ```

   客户端：

   ```c
   #include <stdio.h>

   int main(int argc, char *argv[])
   {
       if (argc != 2) {
           fprintf(stderr, "用法：%s 消息\n", argv[0]);
           return 2;
       }
       FILE *req = fopen("req.fifo", "w");          // 顺序要和服务器一致，否则双方互相等待
       FILE *resp = fopen("resp.fifo", "r");
       if (req == NULL || resp == NULL) {
           perror("fopen");
           return 1;
       }
       fprintf(req, "%s\n", argv[1]);
       fclose(req);                                 // 关闭写端，服务器才能读到完整的一行并继续
       char line[256];
       if (fgets(line, sizeof line, resp)) {
           printf("服务器返回：%s", line);
       }
       fclose(resp);
       return 0;
   }
   ```

   ```bash
   ./echo_server &
   ./echo_client "hello fifo"
   ./echo_client "second message"
   ```

   ```
   服务器返回：HELLO FIFO
   服务器返回：SECOND MESSAGE
   ```

   要注意两个 FIFO 的打开顺序：双方都必须先打开 `req.fifo`，再打开 `resp.fifo`。如果服务器先打开 `req.fifo` 等待写端，而客户端先打开 `resp.fifo` 等待读端，双方会永远互相等待。这个简单的服务器一次只能服务一个客户端，多个客户端同时使用时请求会混在一起，真实的程序会改用 Unix 域套接字。

2. 把计数器和信号量放进同一个结构体：

   ```c
   struct { sem_t lock; long counter; } *sh = mmap(NULL, sizeof *sh, PROT_READ | PROT_WRITE,
                                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
   sem_init(&sh->lock, 1, 1);                   // 初值 1：同一时刻只允许一个进程进入
   ...
   sem_wait(&sh->lock);
   sh->counter++;
   sem_post(&sh->lock);
   ```

   加上计时后各运行 3 次：

   ```
   counter = 1014497（期望 2000000），0.004 秒
   counter = 1026773（期望 2000000），0.003 秒
   counter = 1020385（期望 2000000），0.003 秒
   counter = 2000000（期望 2000000），0.190 秒
   counter = 2000000（期望 2000000），0.163 秒
   counter = 2000000（期望 2000000），0.168 秒
   ```

   前 3 行不加同步，后 3 行用信号量保护。结果正确了，但时间从约 0.003 秒变成了约 0.17 秒，慢了 50 多倍：200 万次加法，每次都要获取和释放信号量，两个进程还在争抢同一个信号量。正确是第一位的，但同步的代价要心中有数，下一篇会比较几种同步方法的开销，以及怎样减少它。
3. `mmap` 的 `prot` 必须和 `open` 时的访问方式一致（《内存映射 mmap》的常见错误第 3 条）：要映射成可写的 `MAP_SHARED`，文件就必须以可读可写的方式打开；`ftruncate` 也要求可写。只读的一方用 `O_RDONLY` 和 `PROT_READ`，即使程序有 bug，也不会意外地改坏共享的数据。

</details>
