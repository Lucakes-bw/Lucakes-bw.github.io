---
title: 信号
date: 2026-10-24 10:55:00
updated: 2026-10-24 10:55:00
categories:
  - Linux基础
  - 进程与线程
tags:
  - Linux
  - 信号
order: 2
description: 信号是内核通知进程「发生了某件事」的方式：Ctrl+C、kill、子进程结束、向断开的管道写数据。本文用 sigaction 捕获信号，实测 SIGKILL 和 SIGSTOP 不能被捕获、信号会打断 read；复现在信号处理函数中调用 malloc 导致的死锁，并用 GDB 看清它卡在哪里；实测 10 个子进程同时结束只收到 6 个 SIGCHLD；最后修好上一篇迷你 Shell 按 Ctrl+C 就退出的问题。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：了解常用信号和它们的默认行为；会用 `sigaction` 设置信号处理函数；理解信号处理函数中能做什么、不能做什么（异步信号安全）；理解信号对系统调用的打断和 `SA_RESTART`；会正确处理 SIGCHLD；会写「收到信号后清理再退出」的程序。
>
> **前置知识**：《进程与系统监控》中的 `kill`、信号和退出码；上一篇的 `fork`、`wait` 和迷你 Shell。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），GCC 13.3，glibc 2.39。

## 1. 信号是什么

**信号**（signal）是内核发给进程的一个通知，只携带一个编号，表示「发生了某件事」。信号的来源：

| 来源 | 例子 |
|---|---|
| 终端 | Ctrl+C 发送 SIGINT，Ctrl+\ 发送 SIGQUIT，Ctrl+Z 发送 SIGTSTP |
| 其他进程 | `kill` 命令或 `kill()` 函数 |
| 内核：程序出错 | 访问非法内存 SIGSEGV，除以 0 SIGFPE，`abort()` SIGABRT |
| 内核：某个事件 | 子进程结束 SIGCHLD，向没有读端的管道写数据 SIGPIPE，定时器到期 SIGALRM |

每个信号都有一个**默认行为**：

| 默认行为 | 信号 |
|---|---|
| 终止进程 | SIGINT、SIGTERM、SIGKILL、SIGPIPE、SIGALRM、SIGHUP、SIGUSR1/2 |
| 终止并生成 core | SIGSEGV、SIGABRT、SIGFPE、SIGQUIT、SIGBUS |
| 忽略 | SIGCHLD、SIGWINCH（终端窗口大小改变） |
| 暂停 / 继续 | SIGSTOP、SIGTSTP / SIGCONT |

进程可以改变大多数信号的处理方式：**忽略**它，或者**捕获**它（收到时调用自己写的函数）。

## 2. 用 sigaction 捕获信号

```c
#include <signal.h>

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
```

下面的程序捕获 SIGINT，按 3 次 Ctrl+C 才退出：

```c
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t count = 0;

static void on_sigint(int sig)
{
    (void)sig;
    count++;                                     // 只修改一个标志，其余的事交给主循环
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    printf("pid = %d，按 3 次 Ctrl+C 退出\n", getpid());
    int seen = 0;
    while (count < 3) {
        pause();                                 // 睡眠，直到收到一个信号
        if (count != seen) {
            seen = count;
            printf("收到 SIGINT，第 %d 次\n", seen);
        }
    }
    printf("退出\n");
    return 0;
}
```

在终端中运行，按 3 次 Ctrl+C：

```
pid = 19671，按 3 次 Ctrl+C 退出
^C收到 SIGINT，第 1 次
^C收到 SIGINT，第 2 次
^C收到 SIGINT，第 3 次
退出
```

`^C` 是终端显示的，每次按下，进程没有终止，而是调用了 `on_sigint`。几个要点：

- `struct sigaction` 的 `sa_handler` 是处理函数，它接收信号的编号；也可以是 `SIG_IGN`（忽略）或 `SIG_DFL`（恢复默认）；
- `sa_mask` 指定处理函数执行期间**额外屏蔽**哪些信号（正在处理的信号本身默认就会被屏蔽，不会嵌套）；
- 处理函数和主程序共享的变量，类型用 **`volatile sig_atomic_t`**：`sig_atomic_t` 保证读写是一次完成的，`volatile` 告诉编译器这个变量可能被「意外地」修改，每次都要从内存中读；
- `pause()` 让进程睡眠，直到收到一个被捕获的信号。

还有一个更老的函数 `signal()`，在不同的系统上行为不一致（处理一次后是否恢复默认、是否重启系统调用），新代码应该使用 `sigaction`。

### 2.1 SIGKILL 和 SIGSTOP 不能被捕获

```c
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static void handler(int sig) { (void)sig; }

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    int sigs[] = {SIGKILL, SIGSTOP, SIGTERM};
    const char *names[] = {"SIGKILL", "SIGSTOP", "SIGTERM"};
    for (int i = 0; i < 3; i++) {
        if (sigaction(sigs[i], &sa, NULL) == -1) {
            printf("%s：%s\n", names[i], strerror(errno));
        } else {
            printf("%s：设置成功\n", names[i]);
        }
    }
    return 0;
}
```

```
SIGKILL：Invalid argument
SIGSTOP：Invalid argument
SIGTERM：设置成功
```

SIGKILL 和 SIGSTOP 既不能被捕获，也不能被忽略。这保证了管理员总有办法终止或暂停任何一个进程。也正因为不能被捕获，进程收到 SIGKILL 时没有任何机会做清理，《进程与系统监控》中实测过这一点。

## 3. 信号处理函数中能做什么

信号可能在程序执行到**任何一条指令**时到来，处理函数会**打断**主程序，在中间插进来执行。如果主程序正在 `malloc` 中修改堆的数据结构，处理函数也调用 `malloc`，会发生什么？

```c
// unsafe.c：在信号处理函数中调用 malloc
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t handled = 0;

static void on_alarm(int sig)
{
    (void)sig;
    free(malloc(5000));                          // 错误：malloc 不是异步信号安全的
    handled++;
}

static void *idle(void *arg)
{
    (void)arg;
    pause();
    return NULL;
}

int main(void)
{
    pthread_t t;
    pthread_create(&t, NULL, idle, NULL);        // 再启动一个线程，原因见正文

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval it = {{0, 100}, {0, 100}};  // 每 100 微秒一个 SIGALRM
    setitimer(ITIMER_REAL, &it, NULL);

    for (long i = 0; ; i++) {
        free(malloc(2000 + i % 20000));          // 主程序也在不停地 malloc
        if (i % 1000000 == 0) {
            fprintf(stderr, "主循环 %ld 次，处理了 %d 个信号\n", i, (int)handled);
        }
    }
}
```

主程序不停地分配和释放内存，定时器每 100 微秒发送一个 SIGALRM，处理函数中也分配和释放内存。运行 5 次，每次 3 秒后查看：

```
第 1 次：主循环 0 次，处理了 0 个信号，状态 S     0.0
第 2 次：主循环 0 次，处理了 0 个信号，状态 S     0.0
第 3 次：主循环 0 次，处理了 0 个信号，状态 S     0.0
第 4 次：主循环 0 次，处理了 0 个信号，状态 S     0.0
第 5 次：主循环 0 次，处理了 0 个信号，状态 S     0.0
```

每次都只输出了第一行，状态是 `S`（睡眠），CPU 占用 0：程序**卡死**了，一个信号都没有处理完。用《GDB 与 core dump》中的方法连接上去：

```
#0  futex_wait at ../sysdeps/nptl/futex-internal.h:146
#1  __GI___lll_lock_wait_private at ./nptl/lowlevellock.c:34
#2  0x0000fcb872da685c in __GI___libc_malloc at ./malloc/malloc.c:3334
#3  0x0000bd0b37400b30 in on_alarm at unsafe.c:15
#4  <signal handler called>
#5  0x0000fcb872da5968 in _int_malloc at ./malloc/malloc.c:4456
#6  0x0000fcb872da6540 in __GI___libc_malloc at ./malloc/malloc.c:3336
#7  0x0000bd0b374009bc in main at unsafe.c:40
```

从下往上读调用栈：`main` 调用 `malloc`（#6、#7），`malloc` 内部已经**拿到了锁**，正在 `_int_malloc` 中工作（#5）；这时信号来了（#4），处理函数 `on_alarm` 也调用 `malloc`（#2、#3），要拿同一把锁，于是等待（#0、#1）。而锁的持有者正是被打断的主程序，它要等处理函数返回才能继续。**自己等自己，永远等不到**，这就是死锁。

为什么程序中要多启动一个什么都不做的线程？我最初写的版本没有它，运行 10 秒，处理了 10 万个信号也没有卡住。原因是 glibc 做了一个优化：程序中只有一个线程时，`malloc` 根本不加锁。一旦程序创建过线程，`malloc` 就开始加锁，问题立刻暴露。没有锁并不意味着安全：处理函数打断 `malloc` 时，堆的数据结构可能正处于修改到一半的状态，再调用 `malloc` 可能把它彻底弄乱，只是这种错误更难复现。

所以，信号处理函数中只能调用**异步信号安全**（async-signal-safe）的函数。`man 7 signal-safety` 列出了它们，主要是 `write`、`read`、`_exit`、`kill`、`waitpid` 等系统调用的包装函数；**`printf`、`malloc`、`free`、`exit` 都不在其中**（`printf` 内部也有锁和缓冲区）。

最安全、最常用的写法就是本文所有例子的写法：**处理函数只设置一个 `volatile sig_atomic_t` 标志，真正的工作交给主程序去做**。

## 4. 信号打断系统调用

进程阻塞在一个慢速的系统调用中（比如等待终端输入的 `read`）时收到信号，会怎样？

```c
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void on_alarm(int sig) { (void)sig; }

int main(int argc, char *argv[])
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    if (argc > 1) sa.sa_flags = SA_RESTART;      // 带参数运行时，自动重启被中断的系统调用
    sigaction(SIGALRM, &sa, NULL);

    alarm(1);                                    // 1 秒后给自己发送 SIGALRM
    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf);   // 等待输入
    if (n == -1) {
        printf("read 返回 -1：%s\n", strerror(errno));
    } else {
        printf("read 读到 %zd 字节\n", n);
    }
    return 0;
}
```

```bash
sleep 3 | ./eintr                    # 3 秒内没有输入
(sleep 2; echo hi) | ./eintr restart # 加上 SA_RESTART
```

```
read 返回 -1：Interrupted system call
read 读到 3 字节
```

- 不加 `SA_RESTART`：处理函数返回后，`read` 不再继续等待，而是返回 -1，`errno` 为 **`EINTR`**；
- 加上 `SA_RESTART`：处理函数返回后，内核自动重新执行 `read`，2 秒后读到了 `hi\n`。

所以设置了信号处理函数的程序，要么使用 `SA_RESTART`，要么在调用慢速系统调用时检查 `EINTR` 并重试。注意 `SA_RESTART` 并不对所有的调用都有效，`sleep`、`nanosleep`、`pause` 等被打断后总是提前返回，后面的例子会遇到这一点。

## 5. SIGCHLD：子进程结束的通知

子进程结束时，父进程会收到 SIGCHLD（默认被忽略）。上一篇的练习中，迷你 Shell 只在用户按回车时才回收后台进程；如果在 SIGCHLD 的处理函数中调用 `waitpid`，子进程一结束就能被回收。

但信号有一个特点：**同一种信号不会排队**。一个信号在等待处理时又来了一个同样的信号，两个只算一个。让 10 个子进程几乎同时结束：

```c
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t signals = 0, reaped = 0;
static int reap_all;

static void on_sigchld(int sig)
{
    (void)sig;
    int saved = errno;                           // 处理函数可能改变 errno，要保存和恢复
    signals++;
    if (reap_all) {
        while (waitpid(-1, NULL, WNOHANG) > 0) { // 正确：一次回收所有已经结束的子进程
            reaped++;
        }
    } else if (waitpid(-1, NULL, WNOHANG) > 0) { // 错误：每个信号只回收一个
        reaped++;
    }
    errno = saved;
}

int main(int argc, char *argv[])
{
    reap_all = argc > 1;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigchld;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    for (int i = 0; i < 10; i++) {
        if (fork() == 0) {
            _exit(0);                            // 10 个子进程几乎同时结束
        }
    }
    struct timespec ts = {1, 0};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {   // 被信号打断时，继续睡完剩下的时间
    }
    printf("%s：收到 %d 个 SIGCHLD，回收了 %d 个子进程\n", reap_all ? "循环回收" : "每次回收一个", (int)signals, (int)reaped);
    fflush(stdout);
    if (system("ps -o stat= --ppid $PPID | grep -c Z") == -1) perror("system");
    return 0;
}
```

（最后一行用 `ps` 统计还剩多少个僵尸子进程。处理函数中调用的 `waitpid` 是异步信号安全的；`errno` 要保存和恢复，否则可能改掉主程序刚得到的错误码。）

```
每次回收一个：收到 6 个 SIGCHLD，回收了 6 个子进程
4
每次回收一个：收到 7 个 SIGCHLD，回收了 7 个子进程
3
每次回收一个：收到 6 个 SIGCHLD，回收了 6 个子进程
4
每次回收一个：收到 7 个 SIGCHLD，回收了 7 个子进程
3
循环回收：收到 7 个 SIGCHLD，回收了 10 个子进程
0
循环回收：收到 6 个 SIGCHLD，回收了 10 个子进程
0
循环回收：收到 10 个 SIGCHLD，回收了 10 个子进程
0
循环回收：收到 7 个 SIGCHLD，回收了 10 个子进程
0
```

10 个子进程结束，通常只收到 6 或 7 个 SIGCHLD。每次只回收一个的版本，总有 3 到 4 个子进程成了僵尸；循环调用 `waitpid(-1, NULL, WNOHANG)` 直到返回 0 的版本，每次都回收了全部 10 个。**收到一个 SIGCHLD，只能说明「至少有一个」子进程结束了**，要用循环把所有已经结束的都回收掉。

我第一次写这个程序时，主程序用的是 `sleep(1)`，结果两个版本都会剩下僵尸。原因正是第 4 节说的：`sleep` 被 SIGCHLD 打断后提前返回，程序在子进程还没全部结束时就开始统计了。改成被打断后继续睡完剩下时间的 `nanosleep` 循环才正确。

## 6. 收到信号后清理再退出

《进程与系统监控》中用 Shell 的 `trap` 实现过「收到 SIGTERM 时删除临时文件再退出」。C 程序中的写法：

```c
// server.c：收到 SIGTERM 或 SIGINT 后，处理完当前的任务再退出
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop = 0;

static void on_stop(int sig)
{
    stop = sig;                                  // 只记下收到了哪个信号
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    FILE *tmp = fopen("/tmp/server.tmp", "w");
    int job = 0;
    while (!stop) {
        job++;
        printf("处理任务 %d……\n", job);
        fflush(stdout);
        sleep(1);                                // 假装在工作；收到信号时 sleep 会提前返回
        printf("任务 %d 完成\n", job);
    }
    printf("收到信号 %d，清理后退出\n", (int)stop);
    fclose(tmp);
    remove("/tmp/server.tmp");
    return 0;
}
```

```bash
./server & sleep 2.5; kill $!; wait $!; echo "exit=$?"
ls /tmp/server.tmp
```

```
处理任务 1……
任务 1 完成
处理任务 2……
任务 2 完成
处理任务 3……
任务 3 完成
收到信号 15，清理后退出
exit=0
ls: cannot access '/tmp/server.tmp': No such file or directory
```

在第 3 个任务进行到一半时收到了 SIGTERM（15）。处理函数只设置了 `stop`，`sleep` 被打断后提前返回，程序把当前任务的收尾做完，检查到 `stop` 后退出循环，关闭并删除临时文件，以退出码 0 正常结束。服务器程序、`systemd` 管理的服务都应该这样处理 SIGTERM：`systemctl stop` 先发送 SIGTERM，等待一段时间（默认 90 秒）后进程还没退出，才发送 SIGKILL。

## 7. 常见错误

1. **在信号处理函数中调用 `printf`、`malloc`**：可能死锁或破坏数据，而且很难复现。只设置标志。
2. **共享的标志没有用 `volatile sig_atomic_t`**：编译器可能把循环中的读取优化掉，主程序永远看不到标志的变化。
3. **以为每个信号都会被单独处理**：同类信号不排队。处理 SIGCHLD 要循环 `waitpid`。
4. **不处理 `EINTR`**：慢速系统调用被信号打断后返回 -1，程序把它当作真正的错误。
5. **试图捕获 SIGKILL**：`sigaction` 返回 `EINVAL`。
6. **处理函数中修改了 `errno` 不恢复**：主程序看到的错误码被改掉了。
7. **以为被忽略的信号在 exec 后会恢复**：exec 会把**被捕获**的信号恢复为默认处理（新程序中没有那个处理函数了），但**被忽略**的信号仍然被忽略。下面的练习 1 就会遇到。

## 8. 小结

- 信号是内核发给进程的通知，只有一个编号；来源有终端、其他进程和内核
- 默认行为有终止、终止并生成 core、忽略、暂停；SIGKILL 和 SIGSTOP 不能被捕获或忽略
- 用 `sigaction` 设置处理函数；处理函数中只能调用异步信号安全的函数，最好只设置一个 `volatile sig_atomic_t` 标志
- 在处理函数中调用 `malloc` 实测导致死锁：主程序在 `malloc` 中持有锁时被打断，处理函数再请求同一把锁
- 信号会打断慢速系统调用，返回 `EINTR`；`SA_RESTART` 让大部分调用自动重启，但 `sleep` 等仍会提前返回
- 同类信号不排队：10 个子进程同时结束只收到 6 或 7 个 SIGCHLD，要循环 `waitpid(-1, ..., WNOHANG)`
- 收到 SIGTERM 时设置标志，主程序做完清理再退出

## 9. 练习

1. 上一篇的迷你 Shell 中，运行 `sleep 3` 时按 Ctrl+C，整个 Shell 都退出了。怎样让 Ctrl+C 只终止正在运行的命令？
2. 下面的写法有什么问题？
   ```c
   static void handler(int sig)
   {
       printf("收到信号 %d\n", sig);
   }
   ```
   如果确实想在处理函数中输出一条信息，应该怎样写？
3. 用 `sigprocmask` 在一段关键代码执行期间屏蔽 SIGINT，执行完再解除屏蔽。屏蔽期间按下的 Ctrl+C 会丢失吗？

<details>
<summary>点击查看答案</summary>

1. 按下 Ctrl+C 时，终端把 SIGINT 发给**前台的所有进程**，包括 Shell 和它正在运行的命令。所以 Shell 自己要忽略 SIGINT：

   ```c
   signal(SIGINT, SIG_IGN);                     // 在 main 的开头：Shell 自己忽略 Ctrl+C
   ```

   但只这样改还不够。实测三个版本，都是运行 `sleep 3` 后按 Ctrl+C，再输入 `echo 还活着`：

   ```
   == 原来的版本
   mysh> sleep 3
   ^C
   == 只在 Shell 中忽略 SIGINT
   mysh> sleep 3
   ^Cmysh> echo 还活着
   还活着
   == 子进程中再恢复默认处理
   mysh> sleep 3
   ^C[退出码 130]
   mysh> echo 还活着
   还活着
   ```

   原来的版本，Shell 和 `sleep` 一起被终止了。第二个版本，Shell 活了下来，但 `sleep` 也没有被终止，它睡满了 3 秒才结束：子进程继承了「忽略 SIGINT」，并且**被忽略的信号在 exec 之后仍然被忽略**（第 7 节第 7 条）。所以子进程在 exec 之前还要恢复默认处理：

   ```c
   static void run_child(struct cmd *c)
   {
       signal(SIGINT, SIG_DFL);                 // 恢复默认处理：被忽略的信号在 exec 之后仍然被忽略
       ...
   ```

   第三个版本中，`sleep` 被 Ctrl+C 终止，退出码 130 = 128 + 2，Shell 继续运行。（这里用 `signal` 设置 `SIG_IGN` 和 `SIG_DFL` 是安全的，不安全的是用它设置处理函数。）bash 实际的做法更复杂：它把每个命令放进单独的进程组，让终端只把信号发给前台的进程组。

2. `printf` 不是异步信号安全的，第 3 节的 `malloc` 是同样的问题。可以用异步信号安全的 `write`：

   ```c
   static void handler(int sig)
   {
       (void)sig;
       static const char msg[] = "收到信号\n";
       ssize_t r = write(STDERR_FILENO, msg, sizeof msg - 1);
       (void)r;                                 // 处理函数中无法处理写入失败，忽略返回值
   }
   ```

   `write` 不能格式化数字。需要输出信号的编号时，自己把数字转换成字符，或者更简单：只设置标志，由主程序用 `printf` 输出。

3. 不会丢失。被屏蔽的信号处于**未决**（pending）状态，解除屏蔽后立即被处理。实测：屏蔽 SIGINT 后睡眠 3 秒，期间按 3 次 Ctrl+C，再用 `sigpending` 检查、解除屏蔽：

   ```c
   sigset_t set, old;
   sigemptyset(&set);
   sigaddset(&set, SIGINT);
   sigprocmask(SIG_BLOCK, &set, &old);         // 屏蔽 SIGINT
   sleep(3);                                   // 关键代码
   sigprocmask(SIG_SETMASK, &old, NULL);       // 解除屏蔽
   ```

   ```
   屏蔽期间，3 秒内按 Ctrl+C……
   ^C^C^C解除屏蔽前：SIGINT 未决，处理了 0 次
   收到信号
   解除屏蔽后：处理了 1 次
   ```

   信号没有丢失，解除屏蔽后立即调用了处理函数；但按了 3 次，只处理了 1 次，因为同类信号不排队（第 5 节）。

</details>
