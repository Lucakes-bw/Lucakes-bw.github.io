---
title: 线程同步
date: 2026-10-24 11:10:00
updated: 2026-10-24 11:10:00
categories:
  - Linux基础
  - 进程与线程
tags:
  - Linux
  - 线程
  - 同步
order: 5
description: 互斥锁只能保证「同一时刻只有一个线程」，线程之间还需要「等待某件事发生」，这就是条件变量。本文实测忙等 2 秒用掉 2 秒的 CPU，而条件变量几乎为 0；用条件变量实现有界缓冲区的生产者消费者模型，把 while 改成 if 后，5 次运行全部出错；复现两个线程转账导致的死锁，用 GDB 看到它们各自卡在第二把锁上，用 TSan 在没有卡住的运行中提前发现死锁的隐患，再用固定加锁顺序解决；最后对照 C++11 的线程库。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用条件变量让线程等待某个条件，理解为什么必须用 `while` 检查条件；能实现生产者消费者模型；理解死锁产生的条件，会用固定加锁顺序避免死锁，会用 GDB 和 TSan 诊断死锁；能把 pthread 的写法对应到 C++11 的线程库。
>
> **前置知识**：上一篇的线程、数据竞争和互斥锁。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64，2 个 CPU 核），GCC 13.3。

## 1. 等待一个条件

一个线程要等另一个线程把数据准备好，最直接的写法是不停地检查一个标志（**忙等**）。另一种写法是用**条件变量**（condition variable）：等待的线程睡眠，条件满足时由另一个线程唤醒它。比较两者：

```c
// waitflag.c：一个线程等待另一个线程准备好数据，比较忙等和条件变量
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int ready = 0;
static int data = 0;

static void *producer(void *arg)
{
    (void)arg;
    sleep(2);                                    // 准备数据要 2 秒
    pthread_mutex_lock(&lock);
    data = 42;
    ready = 1;
    pthread_cond_signal(&cond);                  // 唤醒一个等待的线程
    pthread_mutex_unlock(&lock);
    return NULL;
}

static double cpu_seconds(void)                  // 本线程用掉的 CPU 时间
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char *argv[])
{
    int busy = argc > 1 && strcmp(argv[1], "busy") == 0;
    pthread_t t;
    pthread_create(&t, NULL, producer, NULL);

    double c0 = cpu_seconds();
    pthread_mutex_lock(&lock);
    if (busy) {
        while (!ready) {                         // 忙等：不停地检查
            pthread_mutex_unlock(&lock);
            pthread_mutex_lock(&lock);
        }
    } else {
        while (!ready) {
            pthread_cond_wait(&cond, &lock);     // 睡眠，直到被唤醒；等待期间释放锁
        }
    }
    printf("%s：data = %d，等待期间用掉 CPU %.3f 秒\n", busy ? "忙等" : "条件变量", data, cpu_seconds() - c0);
    pthread_mutex_unlock(&lock);
    pthread_join(t, NULL);
    return 0;
}
```

```bash
./waitflag busy
./waitflag
```

```
忙等：data = 42，等待期间用掉 CPU 2.003 秒
条件变量：data = 42，等待期间用掉 CPU 0.000 秒
```

两种写法都拿到了数据，但忙等在等待的 2 秒中**一直占满了一个 CPU 核**，条件变量几乎没有用掉 CPU。（`CLOCK_THREAD_CPUTIME_ID` 测量的是当前线程实际使用的 CPU 时间。）

条件变量的用法有固定的格式：

```c
// 等待的一方
pthread_mutex_lock(&lock);
while (!条件) {
    pthread_cond_wait(&cond, &lock);
}
/* 条件成立，访问共享数据 */
pthread_mutex_unlock(&lock);

// 通知的一方
pthread_mutex_lock(&lock);
/* 修改共享数据，使条件成立 */
pthread_cond_signal(&cond);      // 唤醒一个等待的线程；pthread_cond_broadcast 唤醒全部
pthread_mutex_unlock(&lock);
```

关键在于 `pthread_cond_wait` 做的三件事：**释放锁、睡眠、被唤醒后重新获取锁**。前两步是一个不可分割的操作，所以不会出现「刚释放锁、还没开始睡眠时，通知已经发出，于是错过了通知」的情况。这也是条件变量必须和一个互斥锁配合使用的原因。

## 2. 生产者消费者

**生产者消费者**模型是多线程程序中最常见的结构：生产者线程产生数据放进缓冲区，消费者线程从缓冲区取出处理。缓冲区的容量有限，所以：

- 缓冲区满了，生产者要等待（条件变量 `not_full`）；
- 缓冲区空了，消费者要等待（条件变量 `not_empty`）。

《重定向与管道的原理》中的管道就是内核实现的一个有界缓冲区：写满 64 KB 时写端阻塞，空了读端阻塞。下面用条件变量自己实现一个，2 个生产者各放入 1 到 200000，2 个消费者一共取出 400000 个，检查总和：

```c
// prodcons.c：有界缓冲区，多个生产者和消费者
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define CAP 8                                    // 缓冲区容量
#define ITEMS 200000                             // 每个生产者生产的个数
#define PRODUCERS 2
#define CONSUMERS 2

#ifdef USE_IF
#define WAIT_WHILE if                            // 错误的写法
#else
#define WAIT_WHILE while                         // 正确的写法
#endif

static int buf[CAP];
static int head = 0, tail = 0, count = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static long consumed_sum = 0, consumed_n = 0;
static long bad_put = 0, bad_get = 0;           // 醒来后条件却不满足的次数

static void put(int x)
{
    pthread_mutex_lock(&lock);
    WAIT_WHILE (count == CAP) {
        pthread_cond_wait(&not_full, &lock);     // 满了：等待消费者取走
    }
    if (count == CAP) bad_put++;                 // 醒来后缓冲区却是满的
    buf[tail] = x;
    tail = (tail + 1) % CAP;
    count++;
    pthread_cond_signal(&not_empty);             // 通知：有数据了
    pthread_mutex_unlock(&lock);
}

static int get(void)
{
    pthread_mutex_lock(&lock);
    WAIT_WHILE (count == 0) {
        pthread_cond_wait(&not_empty, &lock);    // 空了：等待生产者放入
    }
    if (count == 0) bad_get++;                   // 醒来后缓冲区却是空的
    int x = buf[head];
    head = (head + 1) % CAP;
    count--;
    pthread_cond_signal(&not_full);              // 通知：有空位了
    pthread_mutex_unlock(&lock);
    return x;
}

static void *producer(void *arg)
{
    (void)arg;
    for (int i = 1; i <= ITEMS; i++) put(i);
    return NULL;
}

static void *consumer(void *arg)
{
    long n = (long)arg;                          // 这个消费者要取的个数
    for (long i = 0; i < n; i++) {
        int x = get();
        pthread_mutex_lock(&lock);
        consumed_sum += x;
        consumed_n++;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main(void)
{
    pthread_t p[PRODUCERS], c[CONSUMERS];
    long per = (long)ITEMS * PRODUCERS / CONSUMERS;
    for (int i = 0; i < CONSUMERS; i++) pthread_create(&c[i], NULL, consumer, (void *)per);
    for (int i = 0; i < PRODUCERS; i++) pthread_create(&p[i], NULL, producer, NULL);
    for (int i = 0; i < PRODUCERS; i++) pthread_join(p[i], NULL);
    for (int i = 0; i < CONSUMERS; i++) pthread_join(c[i], NULL);
    long expect = (long)ITEMS * (ITEMS + 1) / 2 * PRODUCERS;
    printf("取出 %ld 个，总和 %ld（期望 %ld），醒来时满 %ld 次、空 %ld 次\n",
           consumed_n, consumed_sum, expect, bad_put, bad_get);
    return 0;
}
```

用 `while` 检查条件（正确的写法），和用 `if`（错误的写法）各运行几次：

```
while:
取出 400000 个，总和 40000200000（期望 40000200000），醒来时满 0 次、空 0 次
取出 400000 个，总和 40000200000（期望 40000200000），醒来时满 0 次、空 0 次
取出 400000 个，总和 40000200000（期望 40000200000），醒来时满 0 次、空 0 次
if:
取出 400000 个，总和 76100190715（期望 40000200000），醒来时满 2 次、空 2 次
取出 400000 个，总和 71461487033（期望 40000200000），醒来时满 1 次、空 2 次
取出 400000 个，总和 75505321113（期望 40000200000），醒来时满 3 次、空 1 次
取出 400000 个，总和 76252671263（期望 40000200000），醒来时满 1 次、空 1 次
取出 400000 个，总和 75294514495（期望 40000200000），醒来时满 1 次、空 1 次
```

`while` 的版本每次都正确。`if` 的版本 **5 次全错**：每次都有线程醒来后发现条件并不成立（生产者醒来时缓冲区仍是满的，消费者醒来时仍是空的），它没有再检查一遍就继续执行，覆盖了还没被取走的数据，或者取出了不存在的数据，`count` 超出了 0 到 8 的范围，之后整个缓冲区的状态都乱了。

为什么被唤醒了，条件却不成立？

1. **被别人抢先了**：生产者放入一个数据，唤醒了消费者 X；但在 X 重新拿到锁之前，消费者 Y 先拿到锁，把数据取走了。X 拿到锁时缓冲区又空了。
2. **虚假唤醒**（spurious wakeup）：POSIX 允许 `pthread_cond_wait` 在没有任何通知的情况下返回。

所以**被唤醒只说明「条件可能成立了」**，必须用 `while` 再检查一遍。这和《信号》中「收到一个 SIGCHLD 只说明至少有一个子进程结束」是同样的思路。

## 3. 死锁

### 3.1 复现

两个账户，各有自己的锁；两个线程同时在它们之间转账，转账时先锁转出的账户，再锁转入的账户：

```c
// deadlock.c：两个线程在两个账户之间转账
#include <pthread.h>
#include <stdio.h>

struct account {
    int id;
    long balance;
    pthread_mutex_t lock;
};

static struct account A = {1, 1000, PTHREAD_MUTEX_INITIALIZER};
static struct account B = {2, 1000, PTHREAD_MUTEX_INITIALIZER};

static void transfer(struct account *from, struct account *to, long amount)
{
#ifdef ORDERED
    // 总是先锁 id 小的账户，所有线程加锁的顺序就一致了
    struct account *first = from->id < to->id ? from : to;
    struct account *second = from->id < to->id ? to : from;
#else
    struct account *first = from, *second = to; // 先锁转出的账户，再锁转入的账户
#endif
    pthread_mutex_lock(&first->lock);
    pthread_mutex_lock(&second->lock);
    from->balance -= amount;
    to->balance += amount;
    pthread_mutex_unlock(&second->lock);
    pthread_mutex_unlock(&first->lock);
}

static void *a_to_b(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000000; i++) transfer(&A, &B, 1);
    return NULL;
}

static void *b_to_a(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000000; i++) transfer(&B, &A, 1);
    return NULL;
}

int main(void)
{
    pthread_t t1, t2;
    pthread_create(&t1, NULL, a_to_b, NULL);
#ifdef SEQUENTIAL
    pthread_join(t1, NULL);                      // 等第一个线程做完，再启动第二个：不会真的死锁
#endif
    pthread_create(&t2, NULL, b_to_a, NULL);
#ifndef SEQUENTIAL
    pthread_join(t1, NULL);
#endif
    pthread_join(t2, NULL);
    printf("A = %ld，B = %ld，合计 %ld\n", A.balance, B.balance, A.balance + B.balance);
    return 0;
}
```

（`ORDERED` 和 `SEQUENTIAL` 两个宏用于后面的实验，默认不定义。）运行 5 次，每次最多等 10 秒：

```bash
/usr/bin/time -f "%e 秒" timeout 10 ./deadlock; echo "exit=$?"
```

```
Command exited with non-zero status 124
10.00 秒
exit=124
```

5 次全部卡住，被 `timeout` 终止（退出码 124）。卡住时查看：

```
    LWP STAT %CPU WCHAN
  61023 Sl    0.0 futex_wait_queue
  61025 Sl    0.0 futex_wait_queue
  61026 Sl    0.0 futex_wait_queue
```

3 个线程（主线程和两个转账线程）都在睡眠，CPU 占用 0，等待在 futex 上（pthread 的锁在内核中就是用 futex 实现的）。用 GDB 连接上去，看每个线程的调用栈（只保留关键的几行）：

```
Thread 3 (Thread 0xf96a5618f1a0 (LWP 61025) "deadlock"):
#1  __GI___lll_lock_wait at ./nptl/lowlevellock.c:49
#4  0x0000b8362d9c0aa0 in transfer at deadlock.c:24
#5  a_to_b at deadlock.c:34
Thread 2 (Thread 0xf96a5597f1a0 (LWP 61026) "deadlock"):
#1  __GI___lll_lock_wait at ./nptl/lowlevellock.c:49
#4  0x0000b8362d9c0b30 in transfer at deadlock.c:24
#5  b_to_a at deadlock.c:41
Thread 1 (Thread 0xf96a56394ee0 (LWP 61023) "deadlock"):
#5  0x0000b8362d9c08d8 in main at deadlock.c:50
```

两个转账线程都卡在第 24 行，**第二个** `pthread_mutex_lock`：

- `a_to_b` 已经拿到了 A 的锁，正在等 B 的锁；
- `b_to_a` 已经拿到了 B 的锁，正在等 A 的锁。

两个线程各自拿着对方需要的锁，同时等待对方释放，谁也不会让步，这就是**死锁**。主线程则在第 50 行的 `pthread_join` 中等它们结束。《信号》中「处理函数等待被它打断的主程序释放 malloc 的锁」，是死锁在单个线程中的形式。

### 3.2 避免死锁

死锁需要同时满足四个条件：资源互斥使用；拿着一个资源的同时等待另一个；资源不能被抢走；存在循环等待。只要破坏其中一个，就不会死锁。最实用的方法是破坏**循环等待**：**所有线程都按照同一个顺序加锁**。转账时总是先锁 id 小的账户：

```c
struct account *first = from->id < to->id ? from : to;
struct account *second = from->id < to->id ? to : from;
```

```bash
gcc -O2 -pthread -DORDERED deadlock.c -o ordered
/usr/bin/time -f "%e 秒" ./ordered
```

```
A = 1000，B = 1000，合计 2000
0.29 秒
```

3 次运行都在约 0.29 秒内完成，两个线程各转了 100 万次，余额和合计都正确。两个线程现在都先锁 A，拿不到 A 的线程不会拿着 B 去等待，循环等待就不存在了。

其他方法：用 `pthread_mutex_trylock` 尝试加锁，失败时释放已经拿到的锁，过一会儿重试；或者干脆把需要一起使用的数据放在同一把锁下（但锁的粒度越大，线程之间能并行的就越少）。

### 3.3 用 TSan 提前发现死锁的隐患

死锁和数据竞争一样，依赖于时机，测试时不一定会发生。把 `deadlock.c` 改成第一个线程做完再启动第二个线程（定义 `SEQUENTIAL`），两个线程不会同时运行，程序一定能正常结束：

```
A = 1000，B = 1000，合计 2000
```

但是用 TSan 运行（和上一篇一样要用 `setarch -R`），它仍然发现了问题：

```
==================
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock) (pid=61073)
  Cycle in lock order graph: M0 (0xaaaaaaac0060) => M1 (0xaaaaaaac0020) => M0

  Mutex M1 acquired here while holding mutex M0 in thread T1:
    #0 pthread_mutex_lock ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1341 (libtsan.so.2+0x5cc14)
    #1 transfer /home/lucky/ch4/sync/deadlock.c:24 (seq_tsan+0xc6c)
    #2 a_to_b /home/lucky/ch4/sync/deadlock.c:34 (seq_tsan+0xc6c)

    Hint: use TSAN_OPTIONS=second_deadlock_stack=1 to get more informative warning message

  Mutex M0 acquired here while holding mutex M1 in thread T2:
    #0 pthread_mutex_lock ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1341 (libtsan.so.2+0x5cc14)
    #1 transfer /home/lucky/ch4/sync/deadlock.c:24 (seq_tsan+0xba8)
    #2 b_to_a /home/lucky/ch4/sync/deadlock.c:41 (seq_tsan+0xba8)
...
SUMMARY: ThreadSanitizer: lock-order-inversion (potential deadlock) /home/lucky/ch4/sync/deadlock.c:24 in transfer
==================
```

TSan 记录了每个线程「拿着哪把锁时又去拿哪把锁」，构成一张图：T1 拿着 M0 去拿 M1，T2 拿着 M1 去拿 M0，图中有一个环 `M0 => M1 => M0`，这就是**锁顺序颠倒**（lock-order-inversion）。只要存在这样的环，在某个时机下就可能死锁，即使这次没有发生。同时定义 `ORDERED` 后再用 TSan 运行，没有任何报告，退出码 0。

## 4. 对照 C++11

C++ 专栏的《多线程入门：std::thread 与互斥锁》和《条件变量与 async / future》讲了 C++11 的线程库。在 Linux 上，它就是在 pthread 的基础上实现的（所以编译 C++ 的多线程程序同样要 `-pthread`）：

| pthread | C++11 |
|---|---|
| `pthread_create` / `pthread_join` | `std::thread t(f, args...)` / `t.join()` |
| `pthread_detach` | `t.detach()` |
| `pthread_mutex_t`、`lock` / `unlock` | `std::mutex`，配合 `std::lock_guard`、`std::unique_lock` 自动解锁 |
| `pthread_cond_t`、`wait` / `signal` / `broadcast` | `std::condition_variable`、`wait(lock, 谓词)` / `notify_one` / `notify_all` |
| C11 `atomic_long`、`atomic_fetch_add` | `std::atomic<long>`、`fetch_add` 或 `++` |
| 固定加锁顺序 | `std::scoped_lock lk(m1, m2)`（C++17）一次锁住多把锁，内部用避免死锁的算法 |

C++ 的写法有两个明显的优点：`lock_guard` 在离开作用域时自动解锁，不会因为提前 `return` 或抛出异常而忘记解锁；`cv.wait(lock, [] { return 条件; })` 把 `while` 循环写进了函数内部，不会犯第 2 节的错误。理解了 pthread 的原理，再用 C++ 的封装，就知道它们替你做了什么。

## 5. 常见错误

1. **用 `if` 而不是 `while` 检查条件**：被抢先或虚假唤醒后，在条件不成立时继续执行。
2. **不持有锁就调用 `pthread_cond_wait`**：行为未定义。
3. **修改条件时不加锁**：等待的线程可能在检查条件和开始睡眠之间错过通知，永远睡下去。
4. **该用 `broadcast` 时用了 `signal`**：多个线程在等不同的条件时，只唤醒一个可能唤醒了「错误」的那个。
5. **加锁顺序不一致**：死锁。规定一个全局的顺序，或者一次锁住所有需要的锁。
6. **拿着锁去做耗时的事**（读文件、等网络）：其他线程都被阻塞。临界区要尽量短。
7. **在拿着锁的时候调用外部的回调函数**：回调函数可能再去拿同一把锁或其他锁，造成意想不到的死锁。

## 6. 小结

- 条件变量让线程睡眠等待某个条件，实测等待 2 秒，忙等用掉 2 秒 CPU，条件变量几乎为 0
- `pthread_cond_wait` 原子地释放锁并睡眠，醒来后重新拿锁；**必须用 `while` 检查条件**，改成 `if` 后生产者消费者 5 次运行全部出错
- 生产者消费者：有界缓冲区 + 一把锁 + `not_full`、`not_empty` 两个条件变量；管道就是内核实现的有界缓冲区
- 死锁：两个线程各拿一把锁、等待对方的锁；GDB 中可以看到它们都卡在第二个 `lock` 上
- 避免死锁：所有线程按同一顺序加锁；TSan 能在没有真的死锁时发现锁顺序颠倒
- C++11 的 `std::thread`、`std::mutex`、`std::condition_variable` 是 pthread 的封装，用 RAII 和谓词避免了常见错误

## 7. 练习

1. 修改 `prodcons.c`，让生产者全部结束后，消费者能自己知道「不会再有数据了」并退出，而不是事先知道要取多少个（提示：增加一个 `done` 标志，用 `pthread_cond_broadcast` 唤醒所有消费者）。
2. 下面的代码有什么问题？

   ```c
   // 线程 A
   pthread_mutex_lock(&lock);
   if (!ready) {
       pthread_mutex_unlock(&lock);
       pthread_cond_wait(&cond, &lock);
   }

   // 线程 B
   ready = 1;
   pthread_cond_signal(&cond);
   ```

3. 一个线程连续两次锁同一把普通的互斥锁，会怎样？

<details>
<summary>点击查看答案</summary>

1. `get` 在缓冲区空了**并且** `done` 为真时返回 0；主线程等所有生产者结束后，设置 `done` 并唤醒**所有**消费者：

   ```c
   // prodcons_done.c：生产者结束后，消费者自己退出
   #include <pthread.h>
   #include <stdio.h>

   #define CAP 8                                    // 缓冲区容量
   #define ITEMS 200000                             // 每个生产者生产的个数
   #define PRODUCERS 2
   #define CONSUMERS 2


   static int buf[CAP];
   static int head = 0, tail = 0, count = 0;
   static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
   static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
   static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
   static long consumed_sum = 0, consumed_n = 0;
   static int done = 0;                             // 所有生产者都结束了

   static void put(int x)
   {
       pthread_mutex_lock(&lock);
       while (count == CAP) {
           pthread_cond_wait(&not_full, &lock);     // 满了：等待消费者取走
       }
       buf[tail] = x;
       tail = (tail + 1) % CAP;
       count++;
       pthread_cond_signal(&not_empty);             // 通知：有数据了
       pthread_mutex_unlock(&lock);
   }

   static int get(int *x_out)                       // 取到数据返回 1；不会再有数据时返回 0
   {
       pthread_mutex_lock(&lock);
       while (count == 0 && !done) {
           pthread_cond_wait(&not_empty, &lock);    // 空了：等待生产者放入
       }
       if (count == 0) {                            // 空了，并且不会再有数据
           pthread_mutex_unlock(&lock);
           return 0;
       }
       int x = buf[head];
       head = (head + 1) % CAP;
       count--;
       pthread_cond_signal(&not_full);              // 通知：有空位了
       pthread_mutex_unlock(&lock);
       *x_out = x;
       return 1;
   }

   static void *producer(void *arg)
   {
       (void)arg;
       for (int i = 1; i <= ITEMS; i++) put(i);
       return NULL;
   }

   static void *consumer(void *arg)
   {
       long mine = 0;
       int x;
       while (get(&x)) {
           mine++;
           pthread_mutex_lock(&lock);
           consumed_sum += x;
           consumed_n++;
           pthread_mutex_unlock(&lock);
       }
       printf("消费者 %ld 取了 %ld 个后退出\n", (long)arg, mine);
       return NULL;
   }

   int main(void)
   {
       pthread_t p[PRODUCERS], c[CONSUMERS];
       for (long i = 0; i < CONSUMERS; i++) pthread_create(&c[i], NULL, consumer, (void *)(i + 1));
       for (int i = 0; i < PRODUCERS; i++) pthread_create(&p[i], NULL, producer, NULL);
       for (int i = 0; i < PRODUCERS; i++) pthread_join(p[i], NULL);
       pthread_mutex_lock(&lock);
       done = 1;
       pthread_cond_broadcast(&not_empty);          // 唤醒所有等待的消费者，让它们检查 done
       pthread_mutex_unlock(&lock);
       for (int i = 0; i < CONSUMERS; i++) pthread_join(c[i], NULL);
       long expect = (long)ITEMS * (ITEMS + 1) / 2 * PRODUCERS;
       printf("取出 %ld 个，总和 %ld（期望 %ld）\n", consumed_n, consumed_sum, expect);
       return 0;
   }
   ```

   ```
   消费者 1 取了 212496 个后退出
   消费者 2 取了 187504 个后退出
   取出 400000 个，总和 40000200000（期望 40000200000）
   ```

   3 次运行结果都正确，两个消费者各取了多少，每次都不一样。这里必须用 `pthread_cond_broadcast`：如果用 `signal`，只有一个消费者被唤醒并退出，另一个可能永远睡下去。

2. 有三个问题：
   - 线程 A 在调用 `pthread_cond_wait` 之前**自己释放了锁**，`pthread_cond_wait` 要求调用时持有锁，这是未定义行为；
   - 更要紧的是，在 A 释放锁和开始等待之间有一个空隙，如果 B 恰好在这个空隙中设置 `ready` 并发出通知，这个通知就**丢失**了，A 随后开始等待，再也没有人唤醒它；
   - 线程 B 修改 `ready` 时没有加锁，和 A 读 `ready` 构成数据竞争；A 用的是 `if` 而不是 `while`。

   把这段代码写成一个程序，A、B 两个线程同时启动，运行 300 次，每次最多等 0.5 秒：

   ```
   300 次中卡住 3 次
   ```

   卡住的概率只有 1%，测试时很容易漏过，上线后却会偶尔「莫名其妙地卡死」。正确的写法就是第 1 节的格式：A 持有锁调用 `pthread_cond_wait`（它会原子地释放锁并睡眠），用 `while` 检查；B 在锁内修改 `ready` 并通知。

3. 普通的互斥锁不记录是谁拿着它，同一个线程再锁一次，就要等「拿着锁的线程」释放，而那正是它自己，于是**永远卡住**。可以把锁的类型设为检查错误的 `PTHREAD_MUTEX_ERRORCHECK`，第二次加锁会直接返回错误：

   ```c
   #include <errno.h>
   #include <pthread.h>
   #include <stdio.h>
   #include <string.h>

   int main(int argc, char *argv[])
   {
       pthread_mutex_t m;
       pthread_mutexattr_t attr;
       pthread_mutexattr_init(&attr);
       if (argc > 1) {
           pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);   // 检查错误的锁
       }
       pthread_mutex_init(&m, &attr);
       pthread_mutex_lock(&m);
       printf("第一次加锁成功\n");
       fflush(stdout);
       int err = pthread_mutex_lock(&m);            // 同一个线程再锁一次
       printf("第二次加锁返回：%s\n", err ? strerror(err) : "成功");
       return 0;
   }
   ```

   ```bash
   timeout 3 ./selflock; echo "exit=$?"
   ./selflock check
   ```

   ```
   第一次加锁成功
   exit=124
   第一次加锁成功
   第二次加锁返回：Resource deadlock avoided
   ```

   普通的锁卡住了，3 秒后被 `timeout` 终止；检查错误的锁返回 `EDEADLK`（Resource deadlock avoided）。如果确实需要在已经持有锁的函数中再次加锁（比如递归函数），可以用 `PTHREAD_MUTEX_RECURSIVE` 类型的锁，但通常说明代码的结构需要调整。

</details>
