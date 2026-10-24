---
title: POSIX 线程
date: 2026-10-24 11:05:00
updated: 2026-10-24 11:05:00
categories:
  - Linux基础
  - 进程与线程
tags:
  - Linux
  - 线程
  - pthread
order: 4
description: 线程是同一个进程中的多个执行流，它们共享内存，所以通信很方便，也很危险。本文用 pthread_create 和 pthread_join 创建线程，实测创建一个线程比创建一个进程快 2.6 倍；复现把循环变量的地址传给线程的经典错误；两个线程各给计数器加 100 万次，结果只有约 100 万，开了 -O2 却「正确」了，看汇编才知道是编译器把循环变成了一次加法；用 ThreadSanitizer 抓住这个数据竞争；最后比较互斥锁、原子操作和各自累加三种修复方法的速度。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解线程和进程的区别；会用 `pthread_create`、`pthread_join` 创建和等待线程，正确地传递参数和返回值；理解数据竞争，会用 ThreadSanitizer 检测；会用互斥锁和原子操作保护共享数据，并了解它们的代价。
>
> **前置知识**：《进程：fork、exec、wait》；上一篇的竞争条件；C++ 专栏的《多线程入门：std::thread 与互斥锁》讲的是同样的内容在 C++11 中的写法，可以对照阅读。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64，2 个 CPU 核），GCC 13.3，glibc 2.39。编译时要加 `-pthread`。

## 1. 线程和进程

一个进程可以有多个**线程**（thread），每个线程是一个独立的执行流，有自己的栈、寄存器和程序计数器，但它们**共享**进程的其他一切：

| | 每个线程各有一份 | 同一进程的线程共享 |
|---|---|---|
| 内容 | 栈（局部变量）、寄存器、线程 ID、`errno`、信号屏蔽字 | 代码、全局变量、堆（`malloc` 的内存）、打开的文件描述符、当前目录、信号处理方式 |

和 `fork` 出的子进程相比：进程之间要交换数据，需要上一篇的 IPC；线程之间直接读写同一个变量就行。代价是，一个线程的错误会影响整个进程。

## 2. 创建和等待线程

```c
#include <pthread.h>

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
```

`pthread_create` 创建一个新线程，从函数 `start(arg)` 开始执行；`pthread_join` 等待一个线程结束，取得它的返回值（相当于进程的 `waitpid`）。pthread 的函数出错时不设置 `errno`，而是**直接返回错误码**。

```c
#define _GNU_SOURCE                              // gettid 需要它
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

int results[3];                                  // 全局变量：所有线程共享

static void *worker(void *arg)
{
    int id = *(int *)arg;
    results[id - 1] = id * 10;                   // 每个线程只写自己的那个元素
    printf("线程 %d：pid = %d，tid = %d，&results = %p\n", id, getpid(), gettid(), (void *)results);
    return NULL;
}

int main(void)
{
    pthread_t t[3];
    int ids[3] = {1, 2, 3};
    for (int i = 0; i < 3; i++) {
        pthread_create(&t[i], NULL, worker, &ids[i]);
    }
    for (int i = 0; i < 3; i++) {
        pthread_join(t[i], NULL);                // 等待线程结束
    }
    printf("主线程：pid = %d，tid = %d，results = %d %d %d\n", getpid(), gettid(), results[0], results[1], results[2]);
    return 0;
}
```

```
线程 1：pid = 20518，tid = 20519，&results = 0xca9e32f20018
线程 2：pid = 20518，tid = 20520，&results = 0xca9e32f20018
线程 3：pid = 20518，tid = 20521，&results = 0xca9e32f20018
主线程：pid = 20518，tid = 20518，results = 10 20 30
```

- 4 个线程的 **pid 相同**，它们属于同一个进程；每个线程有自己的**线程 ID**（`gettid`），主线程的线程 ID 等于 pid；
- 各线程看到的 `results` 是**同一个地址**，主线程读到了 3 个线程写入的值。对比《进程：fork、exec、wait》中 `fork` 之后，父子进程的 `x` 地址相同、值却各不相同；
- 这里每个线程只写数组中自己的那个元素，互不干扰。多个线程写同一个变量时会出问题，第 4 节讲。

在 Linux 内核看来，线程就是一种共享了内存等资源的「轻量级进程」（LWP）：

```c
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void *worker(void *arg)
{
    (void)arg;
    sleep(2);
    return NULL;
}

int main(void)
{
    pthread_t t[3];
    for (int i = 0; i < 3; i++) pthread_create(&t[i], NULL, worker, NULL);
    sleep(1);
    char cmd[128];
    snprintf(cmd, sizeof cmd, "ps -L -o pid,lwp,nlwp,stat,cmd -p %d; ls /proc/%d/task", getpid(), getpid());
    if (system(cmd) == -1) perror("system");
    for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);
    return 0;
}
```

```
    PID     LWP NLWP STAT CMD
  20446   20446    4 Ssl  ./threadlist
  20446   20495    4 Ssl  ./threadlist
  20446   20496    4 Ssl  ./threadlist
  20446   20497    4 Ssl  ./threadlist
20446
20495
20496
20497
```

`ps -L` 每个线程显示一行：`LWP` 是线程 ID，`NLWP` 是线程数，`STAT` 中的 `l` 表示多线程。`/proc/PID/task/` 中每个线程有一个目录。`top` 中按 `H` 可以按线程显示。

### 2.1 创建线程比创建进程快

```c
// createcost.c 的核心：各创建并等待 10000 次
pthread_create(&t, NULL, nothing, NULL);
pthread_join(t, NULL);
...
pid_t pid = fork();
if (pid == 0) _exit(0);
waitpid(pid, NULL, 0);
```

```
创建并等待一个线程：18.3 微秒
创建并等待一个进程：46.8 微秒
```

创建线程不需要复制页表，比 `fork` 快约 2.6 倍。这里的进程几乎没有占用内存；《进程：fork、exec、wait》中测过，父进程占用 500 MB 时 `fork` 要 1.6 毫秒，差距会大得多。

### 2.2 传参的陷阱

```c
#include <pthread.h>
#include <stdio.h>

static void *worker(void *arg)
{
    printf("我是线程 %d\n", *(int *)arg);
    return NULL;
}

int main(void)
{
    pthread_t t[5];
    for (int i = 0; i < 5; i++) {
        pthread_create(&t[i], NULL, worker, &i); // 错误：所有线程拿到的是同一个变量 i 的地址
    }
    for (int i = 0; i < 5; i++) {
        pthread_join(t[i], NULL);
    }
    return 0;
}
```

运行 3 次：

```
我是线程 5 我是线程 5 我是线程 5 我是线程 5 我是线程 5 
我是线程 2 我是线程 3 我是线程 5 我是线程 3 我是线程 4 
我是线程 3 我是线程 5 我是线程 5 我是线程 5 我是线程 5 
```

期望的输出是 0 到 4，实际上出现了好几个 5，0 一次都没有出现，每次的结果还不一样。5 个线程收到的都是**同一个变量 `i` 的地址**，线程真正去读 `*arg` 时，主线程的循环可能已经把 `i` 改掉了，甚至已经结束（`i` 为 5）。更糟的是，循环结束后 `i` 已经超出了作用域，读它是未定义行为。

正确的做法是给每个线程一个**独立的**、在线程运行期间一直有效的变量，就像 `hello_thread.c` 中的 `ids` 数组。

线程的返回值通过 `pthread_join` 的第二个参数取得。注意不要返回指向线程局部变量的指针，线程结束后它的栈就不存在了；可以返回 `malloc` 的内存，或者像本文这样写进一个共享的数组。

### 2.3 一个线程崩溃，整个进程结束

```c
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static void *bad(void *arg)
{
    (void)arg;
    sleep(1);
    int *p = NULL;
    *p = 1;                                      // 一个线程访问空指针
    return NULL;
}

static void *good(void *arg)
{
    (void)arg;
    for (int i = 1; ; i++) {
        printf("正常的线程在工作：%d\n", i);
        fflush(stdout);
        usleep(400000);
    }
    return NULL;
}

int main(void)
{
    pthread_t a, b;
    pthread_create(&a, NULL, good, NULL);
    pthread_create(&b, NULL, bad, NULL);
    pthread_join(a, NULL);
    printf("不会执行到这里\n");
    return 0;
}
```

```
正常的线程在工作：1
正常的线程在工作：2
正常的线程在工作：3
Segmentation fault      ./crash_thread
exit=139
```

一个线程访问空指针，正常工作的线程也随之结束，`main` 中的 `printf` 没有执行。SIGSEGV 终止的是**整个进程**。这是线程和进程的重要区别：Chrome 等浏览器把每个网页放在单独的进程中，一个网页崩溃不会影响其他网页。

## 3. 数据竞争

上一篇中，两个进程通过共享内存同时修改一个计数器，结果不对。线程天然共享内存，同样的问题更容易发生：

```c
// race.c：两个线程同时给一个计数器加 1
#include <pthread.h>
#include <stdio.h>

#define N 1000000
long counter = 0;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < N; i++) {
        counter++;
    }
    return NULL;
}

int main(void)
{
    pthread_t a, b;
    pthread_create(&a, NULL, worker, NULL);
    pthread_create(&b, NULL, worker, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("counter = %ld（期望 %d）\n", counter, 2 * N);
    return 0;
}
```

```bash
gcc -O0 -pthread race.c -o race0
gcc -O2 -pthread race.c -o race2
```

```
-O0:
counter = 1026800（期望 2000000）
counter = 1013245（期望 2000000）
counter = 1012537（期望 2000000）
counter = 1088650（期望 2000000）
counter = 1023197（期望 2000000）
-O2:
counter = 2000000（期望 2000000）
counter = 2000000（期望 2000000）
counter = 2000000（期望 2000000）
counter = 2000000（期望 2000000）
counter = 2000000（期望 2000000）
```

不开优化时，结果只有约 100 万，丢失了将近一半；开了 `-O2`，5 次全都正确。难道优化修好了 bug？看看 `-O2` 生成的 `worker`：

```bash
objdump -d race2 | grep -A7 "<worker>:"
```

```
00000000000009a0 <worker>:
 9a0:	90000102 	adrp	x2, 20000 <__data_start>
 9a4:	d2800000 	mov	x0, #0x0                   	// #0
 9a8:	f9400c41 	ldr	x1, [x2, #24]
 9ac:	9143d021 	add	x1, x1, #0xf4, lsl #12
 9b0:	91090021 	add	x1, x1, #0x240
 9b4:	f9000c41 	str	x1, [x2, #24]
 9b8:	d65f03c0 	ret
```

循环不见了。编译器把 100 万次加 1 合并成了一次加法：读出 `counter`（`ldr`），加上 0xf4000 + 0x240 = 0xf4240 = 1000000，写回（`str`）。整个函数只执行几条指令，第一个线程很可能在第二个线程启动之前就结束了，所以碰巧没有出错。但**竞争仍然存在**：两个线程如果恰好同时执行这几条指令，仍然会丢失 100 万。

这就是数据竞争最麻烦的地方：**结果依赖于时机，换个编译选项、换台机器、换个负载就可能不同**，测试通过并不说明没有问题。

### 3.1 为什么会丢失

`-O0` 时，`counter++` 编译成三条指令：从内存读到寄存器、寄存器加 1、写回内存。两个线程在两个 CPU 核上同时执行：

| 时刻 | 线程 A | 线程 B | 内存中的 counter |
|---|---|---|---|
| 1 | 读出 100 | | 100 |
| 2 | | 读出 100 | 100 |
| 3 | 加 1，得到 101 | 加 1，得到 101 | 100 |
| 4 | 写回 101 | | 101 |
| 5 | | 写回 101 | 101 |

两次加法，`counter` 只增加了 1。C 标准规定：两个线程访问同一个内存位置，其中至少一个是写，并且没有同步，就是**数据竞争**（data race），程序的行为是**未定义**的。

### 3.2 ThreadSanitizer

《开发环境与编程工具》中介绍过 **ThreadSanitizer**（TSan），它能在运行时检测数据竞争：

```bash
gcc -g -O1 -fsanitize=thread -pthread race.c -o race_tsan
./race_tsan
```

```
FATAL: ThreadSanitizer: unexpected memory mapping 0xc14010a30000-0xc14010a31000
```

在这台机器上，TSan 没能启动。原因是内核的地址空间布局随机化（ASLR）使用了比 TSan 预期更多的随机位，程序被加载到了 TSan 不认识的地址上。这是较新的内核上 TSan 的一个已知问题。用 `setarch -R` 在关闭随机化的情况下运行（只影响这一个程序，不需要 root）：

```bash
setarch $(uname -m) -R ./race_tsan
```

```
==================
WARNING: ThreadSanitizer: data race (pid=20579)
  Read of size 8 at 0xaaaaaaac0018 by thread T2:
    #0 worker /home/lucky/ch4/thr/race.c:11 (race_tsan+0xaf8)

  Previous write of size 8 at 0xaaaaaaac0018 by thread T1:
    #0 worker /home/lucky/ch4/thr/race.c:11 (race_tsan+0xb24)

  Location is global 'counter' of size 8 at 0xaaaaaaac0018 (race_tsan+0x20018)

  Thread T2 (tid=20584, running) created by main thread at:
    #0 pthread_create ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1022 (libtsan.so.2+0x5dd98)
    #1 main /home/lucky/ch4/thr/race.c:21 (race_tsan+0xba8)

  Thread T1 (tid=20583, finished) created by main thread at:
    #0 pthread_create ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1022 (libtsan.so.2+0x5dd98)
    #1 main /home/lucky/ch4/thr/race.c:20 (race_tsan+0xb90)

SUMMARY: ThreadSanitizer: data race /home/lucky/ch4/thr/race.c:11 in worker
==================
ThreadSanitizer: reported 1 warnings
counter = 2000000（期望 2000000）
```

（为了便于阅读，删去了每行末尾的 BuildId。）报告说得很清楚：线程 T2 在 `race.c` 第 11 行**读** `counter`，而线程 T1 之前在同一行**写**过它，两次访问之间没有同步；还列出了两个线程分别是在 `main` 的第 21 行和第 20 行创建的。注意这次运行的结果碰巧是正确的 2000000，**TSan 不需要等到结果出错，只要发生了没有同步的并发访问就会报告**，这正是它的价值。退出码是 66，表示 TSan 发现了问题。

## 4. 修复：互斥锁、原子操作和各自累加

### 4.1 互斥锁

**互斥锁**（mutex）保证同一时刻只有一个线程执行被保护的代码（**临界区**）：

```c
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_lock(&lock);       // 拿到锁才能继续；锁被别的线程拿着就等待
counter++;
pthread_mutex_unlock(&lock);     // 释放锁
```

### 4.2 原子操作

对于「加 1」这样简单的操作，C11 的 `<stdatomic.h>` 提供了**原子操作**：CPU 用一条特殊的指令完成「读、加、写」，中间不会被其他线程插入：

```c
static atomic_long acounter = 0;

atomic_fetch_add(&acounter, 1);
```

### 4.3 各自累加，最后合并

最好的方法是**不共享**：每个线程用自己的局部变量计数，最后只把结果写一次。

### 4.4 速度对比

两个线程各加 1000 万次，三种方法：

```c
// fixes.c：三种正确的计数方法，比较速度
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define N 10000000
#define THREADS 2

static long counter = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_long acounter = 0;
static long partial[THREADS];

static void *with_mutex(void *arg)
{
    (void)arg;
    for (int i = 0; i < N; i++) {
        pthread_mutex_lock(&lock);
        counter++;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static void *with_atomic(void *arg)
{
    (void)arg;
    for (int i = 0; i < N; i++) {
        atomic_fetch_add(&acounter, 1);          // 一条不可分割的「读、加、写」
    }
    return NULL;
}

static void *with_local(void *arg)
{
    int id = *(int *)arg;
    volatile long sum = 0;                       // 局部变量：每个线程各有一份（volatile 防止编译器把循环优化掉）
    for (int i = 0; i < N; i++) {
        sum++;
    }
    partial[id] = sum;                           // 最后只写一次
    return NULL;
}

static double run(void *(*fn)(void *))
{
    pthread_t t[THREADS];
    int ids[THREADS];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < THREADS; i++) {
        ids[i] = i;
        pthread_create(&t[i], NULL, fn, &ids[i]);
    }
    for (int i = 0; i < THREADS; i++) pthread_join(t[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
}

int main(void)
{
    double t = run(with_mutex);
    printf("互斥锁：counter = %ld，%.3f 秒\n", counter, t);
    t = run(with_atomic);
    printf("原子操作：counter = %ld，%.3f 秒\n", (long)acounter, t);
    t = run(with_local);
    printf("各自累加再合并：counter = %ld，%.3f 秒\n", partial[0] + partial[1], t);
    return 0;
}
```

运行 6 次：

```
互斥锁：counter = 20000000，2.224 秒 原子操作：counter = 20000000，0.167 秒 各自累加再合并：counter = 20000000，0.011 秒
互斥锁：counter = 20000000，2.125 秒 原子操作：counter = 20000000，0.166 秒 各自累加再合并：counter = 20000000，0.002 秒
互斥锁：counter = 20000000，2.408 秒 原子操作：counter = 20000000，0.167 秒 各自累加再合并：counter = 20000000，0.009 秒
互斥锁：counter = 20000000，2.268 秒 原子操作：counter = 20000000，0.167 秒 各自累加再合并：counter = 20000000，0.008 秒
互斥锁：counter = 20000000，2.175 秒 原子操作：counter = 20000000，0.165 秒 各自累加再合并：counter = 20000000，0.007 秒
互斥锁：counter = 20000000，2.374 秒 原子操作：counter = 20000000，0.167 秒 各自累加再合并：counter = 20000000，0.011 秒
```

三种方法的结果都正确，速度却天差地别：

| 方法 | 时间 | 相对于各自累加 |
|---|---|---|
| 互斥锁 | 约 2.2 秒 | 慢 200 多倍 |
| 原子操作 | 约 0.17 秒 | 慢约 20 倍 |
| 各自累加再合并 | 0.002 到 0.011 秒 | — |

- 互斥锁最慢：两个线程不停地争抢同一把锁，抢不到的线程要进入内核睡眠、再被唤醒。（在最初调试时的 9 次运行中，有 1 次互斥锁只用了 0.27 秒，可能是那次两个线程很少真正同时争抢。争抢的程度不同，锁的开销可以相差近 10 倍。）
- 原子操作快得多，但它仍然要让两个 CPU 核争夺同一块缓存，每次加法都要在两个核之间传递那一块数据；
- 各自累加几乎没有开销：两个线程完全不共享数据，可以真正并行。`sum` 用 `volatile` 修饰，是为了防止编译器像第 3 节那样把循环优化成一次加法，否则时间是 0。

所以**多线程程序的第一原则是尽量不共享**；必须共享时，简单的计数用原子操作；复杂的数据结构（比如链表、哈希表）用互斥锁，并且让临界区尽量短。

## 5. 常见错误

1. **编译时忘了 `-pthread`**：可能链接失败，或者没有启用线程安全的设置。
2. **把循环变量的地址传给线程**：所有线程共用一个变量。
3. **返回指向线程局部变量的指针**：线程结束后栈就不存在了。
4. **用 `errno` 判断 pthread 函数是否出错**：它们直接返回错误码。
5. **以为测试通过就没有数据竞争**：`-O2` 下结果碰巧正确。用 TSan 检查。
6. **线程结束后不 `pthread_join`**：线程的资源不会被回收，类似僵尸进程。不需要等待的线程用 `pthread_detach`。
7. **在多线程程序中调用 `fork`**：子进程中只有调用 `fork` 的那一个线程，如果其他线程正拿着某把锁（比如 `malloc` 的锁），子进程中这把锁永远不会被释放。多线程程序中 `fork` 后应该立即 `exec`。

## 6. 小结

- 线程共享进程的内存和文件描述符，各自有栈和寄存器；在内核中线程是轻量级进程，`ps -L` 查看
- `pthread_create` 创建，`pthread_join` 等待并取得返回值；创建线程比 fork 快约 2.6 倍
- 传给线程的参数要在线程运行期间有效，并且每个线程各有一份
- 一个线程崩溃，整个进程结束
- 多个线程不加同步地访问同一个变量（至少一个是写）就是数据竞争，行为未定义；`-O2` 时结果碰巧正确，不代表没有问题
- ThreadSanitizer（`-fsanitize=thread`）检测数据竞争；在这台机器上要用 `setarch -R` 运行
- 修复方法：互斥锁、原子操作、不共享；实测速度分别约为 2.2 秒、0.17 秒、0.01 秒

## 7. 练习

1. 修正 `argbug.c`，让 5 个线程分别输出 0 到 4。
2. 写一个程序，用 4 个线程并行地计算 1 到 10 亿的和：每个线程计算其中的四分之一，通过 `pthread_join` 返回自己的部分和。和单线程比较速度（本机有 2 个核）。
3. `hello_thread.c` 中，各个线程写的是 `results` 数组的不同元素，用 TSan 检查会报告数据竞争吗？

<details>
<summary>点击查看答案</summary>

1. 用一个数组，给每个线程一个独立的变量：

   ```c
   #include <pthread.h>
   #include <stdio.h>

   static void *worker(void *arg)
   {
       printf("我是线程 %d\n", *(int *)arg);
       return NULL;
   }

   int main(void)
   {
       pthread_t t[5];
       int ids[5];                                  // 每个线程一个独立的变量
       for (int i = 0; i < 5; i++) {
           ids[i] = i;
           pthread_create(&t[i], NULL, worker, &ids[i]);
       }
       for (int i = 0; i < 5; i++) {
           pthread_join(t[i], NULL);
       }
       return 0;
   }
   ```

   运行 3 次：

   ```
   我是线程 2 我是线程 0 我是线程 1 我是线程 3 我是线程 4 
   我是线程 2 我是线程 0 我是线程 1 我是线程 3 我是线程 4 
   我是线程 0 我是线程 2 我是线程 3 我是线程 4 我是线程 1 
   ```

   0 到 4 各出现一次，顺序由调度决定。用 TSan 检查原来的 `argbug.c`，它报告了 `argbug.c:13 in main` 的数据竞争：主线程在第 13 行修改 `i`，同时线程在读它。

2. ```c
   #include <pthread.h>
   #include <stdio.h>
   #include <stdlib.h>
   #include <time.h>

   #define N 1000000000L

   struct range {
       long from, to;                               // [from, to]
   };

   static void *partial_sum(void *arg)
   {
       struct range *r = arg;
       long *sum = malloc(sizeof *sum);             // 返回值放在堆上，线程结束后仍然有效
       *sum = 0;
       for (long i = r->from; i <= r->to; i++) {
           *sum += i;
       }
       return sum;
   }

   static double now(void)
   {
       struct timespec t;
       clock_gettime(CLOCK_MONOTONIC, &t);
       return t.tv_sec + t.tv_nsec / 1e9;
   }

   int main(int argc, char *argv[])
   {
       int k = argc > 1 ? atoi(argv[1]) : 4;        // 线程数
       pthread_t t[16];
       struct range r[16];
       double t0 = now();
       for (int i = 0; i < k; i++) {
           r[i].from = N / k * i + 1;
           r[i].to = (i == k - 1) ? N : N / k * (i + 1);
           pthread_create(&t[i], NULL, partial_sum, &r[i]);
       }
       long total = 0;
       for (int i = 0; i < k; i++) {
           void *ret;
           pthread_join(t[i], &ret);
           total += *(long *)ret;
           free(ret);
       }
       printf("%d 个线程：和 = %ld，%.3f 秒\n", k, total, now() - t0);
       return 0;
   }
   ```

   ```
   1 个线程：和 = 500000000500000000，0.215 秒
   2 个线程：和 = 500000000500000000，0.110 秒
   4 个线程：和 = 500000000500000000，0.111 秒
   ```

   2 个线程比 1 个线程快了将近一倍。4 个线程并没有更快：本机只有 2 个 CPU 核，同一时刻最多只有 2 个线程在运行，多出的线程只是轮流使用这两个核。**计算密集的任务，线程数超过 CPU 核数没有好处**，`nproc` 可以查看核数。部分和放在 `malloc` 的内存中返回，`main` 用完后释放。

3. 不会。实测：

   ```bash
   gcc -g -O1 -fsanitize=thread -pthread hello_thread.c -o hello_tsan
   setarch $(uname -m) -R ./hello_tsan; echo "exit=$?"
   ```

   ```
   线程 1：pid = 60771，tid = 60773，&results = 0xaaaaaaac0018
   线程 2：pid = 60771，tid = 60774，&results = 0xaaaaaaac0018
   线程 3：pid = 60771，tid = 60775，&results = 0xaaaaaaac0018
   主线程：pid = 60771，tid = 60771，results = 10 20 30
   exit=0
   ```

   数据竞争是指访问**同一个内存位置**，数组的不同元素是不同的位置，互不干扰。主线程在 `pthread_join` 之后才读这些元素，`pthread_join` 本身就是一种同步：被等待的线程中的所有写入，在 `pthread_join` 返回后都对主线程可见。

</details>
