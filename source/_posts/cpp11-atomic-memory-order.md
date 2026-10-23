---
title: 原子操作与内存序
date: 2026-10-22 13:40:00
updated: 2026-10-22 13:40:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 多线程
  - 原子操作
  - 内存模型
order: 15
description: std::atomic 不用加锁就能安全地在多线程间共享简单数据。本文先讲原子变量和 CAS 的用法，再用一个真实的实验展示 CPU 指令重排，讲清楚 relaxed、acquire/release、seq_cst 三种内存序的含义和用法。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `std::atomic` 实现无锁计数器；会用 `fetch_add`、`exchange`、`compare_exchange` 等原子操作，会写 CAS 循环；理解为什么需要内存序；分清 `relaxed`、`acquire/release`、`seq_cst`，会用 release/acquire 在线程间安全地传递数据；会用 `atomic_flag` 实现自旋锁。
>
> **前置知识**：《多线程入门：std::thread 与互斥锁》。本文后半部分（内存序）属于进阶内容，初次学习可以先掌握前 4 节，用默认的 `seq_cst` 就不会出错。
>
> **编译环境**：`g++ -std=c++11 -pthread` / `clang++ -std=c++11 -pthread`。本文实验在 Apple M 系列芯片（ARM64）上运行。

## 1. 为什么需要原子操作

《多线程入门》中，4 个线程同时执行 `++counter` 会丢失大量的加法，原因是 `++counter` 在 CPU 上分为「读、加、写」三步，多个线程的步骤交错在一起。我们用互斥锁解决了这个问题。

但为了保护一个整数的自增，每次都要加锁、解锁，代价有点大。CPU 其实直接提供了**原子指令**：一条指令就完成「读-改-写」，执行过程中其他核心无法插手。C++11 的 `std::atomic<T>` 把这些指令封装成了跨平台的类型。

**原子操作**的意思是：这个操作**不可分割**，其他线程要么看到它完全没发生，要么看到它完全完成，不会看到中间状态。

## 2. std::atomic 基础

### 2.1 无锁计数器

同样是 4 个线程、每个线程自增 100 万次，比较三种写法：

```cpp
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

const int kThreads = 4;
const int kPerThread = 1000000;

template <typename F>
double timeIt(F f)
{
    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) ts.emplace_back(f);
    for (auto& t : ts) t.join();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main()
{
    long long plain = 0;
    std::mutex m;
    double tMutex = timeIt([&] {
        for (int i = 0; i < kPerThread; ++i) {
            std::lock_guard<std::mutex> lock(m);
            ++plain;
        }
    });

    std::atomic<long long> counter(0);
    double tAtomic = timeIt([&] {
        for (int i = 0; i < kPerThread; ++i)
            ++counter;                                   // 原子自增，等价于 fetch_add(1)
    });

    std::atomic<long long> relaxedCounter(0);
    double tRelaxed = timeIt([&] {
        for (int i = 0; i < kPerThread; ++i)
            relaxedCounter.fetch_add(1, std::memory_order_relaxed);
    });

    std::cout << "mutex           : " << plain << "  耗时 " << static_cast<int>(tMutex) << " ms\n";
    std::cout << "atomic          : " << counter << "  耗时 " << static_cast<int>(tAtomic) << " ms\n";
    std::cout << "atomic(relaxed) : " << relaxedCounter.load() << "  耗时 " << static_cast<int>(tRelaxed) << " ms\n";
    return 0;
}
```

运行结果（`-O2`，具体耗时因机器而异）：

```
mutex           : 4000000  耗时 144 ms
atomic          : 4000000  耗时 29 ms
atomic(relaxed) : 4000000  耗时 19 ms
```

三种写法的结果都正确，原子变量比互斥锁快了 4 倍左右。只需要把类型从 `long long` 改成 `std::atomic<long long>`，`++counter` 就变成了原子操作。`relaxed` 版本更快，原因在第 5 节解释。

### 2.2 常用操作

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

// 用 CAS 循环实现“原子地更新最大值”
void updateMax(std::atomic<int>& maxValue, int value)
{
    int current = maxValue.load();
    while (value > current &&
           !maxValue.compare_exchange_weak(current, value)) {
        // 失败时 current 已被更新为最新值，循环再试
    }
}

int main()
{
    std::atomic<int> a(10);
    int old = a.fetch_add(5);              // 返回旧值，a 变为 15
    std::cout << "fetch_add: 旧值 " << old << ", 新值 " << a.load() << '\n';
    old = a.exchange(100);                 // 设置新值，返回旧值
    std::cout << "exchange:  旧值 " << old << ", 新值 " << a.load() << '\n';

    int expected = 100;
    bool ok = a.compare_exchange_strong(expected, 200);   // a == expected 时才改成 200
    std::cout << "CAS(100 -> 200): " << (ok ? "成功" : "失败") << ", a = " << a << '\n';
    expected = 100;
    ok = a.compare_exchange_strong(expected, 300);
    std::cout << "CAS(100 -> 300): " << (ok ? "成功" : "失败") << ", a = " << a
              << ", expected 被改成了 " << expected << '\n';

    std::atomic<int> maxValue(0);
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&maxValue, t] {
            for (int i = 0; i < 10000; ++i) updateMax(maxValue, i * 4 + t);
        });
    for (auto& th : ts) th.join();
    std::cout << "并发求最大值: " << maxValue << "（期望 39999）\n";

    std::cout << std::boolalpha << "atomic<int> 无锁: " << a.is_lock_free() << '\n';
    struct Big { char data[64]; };
    std::atomic<Big> big;
    std::cout << "atomic<64 字节结构体> 无锁: " << big.is_lock_free() << '\n';
    return 0;
}
```

运行结果：

```
fetch_add: 旧值 10, 新值 15
exchange:  旧值 15, 新值 100
CAS(100 -> 200): 成功, a = 200
CAS(100 -> 300): 失败, a = 200, expected 被改成了 200
并发求最大值: 39999（期望 39999）
atomic<int> 无锁: true
atomic<64 字节结构体> 无锁: false
```

| 操作 | 作用 |
| --- | --- |
| `load()` / `store(v)` | 原子地读 / 写 |
| `exchange(v)` | 写入新值，返回旧值 |
| `fetch_add(n)` / `fetch_sub(n)` | 加 / 减，返回**旧值**；`++`、`--`、`+=`、`-=` 是它们的简写 |
| `fetch_and` / `fetch_or` / `fetch_xor` | 位运算，返回旧值（仅限整数类型） |
| `compare_exchange_weak/strong(expected, desired)` | 比较并交换，见下一节 |

## 3. 比较并交换（CAS）

`fetch_add` 只能做加减，如果要原子地做更复杂的更新，比如「如果新值更大，就更新最大值」，就需要 **CAS**（compare-and-swap，比较并交换）：

```cpp
bool compare_exchange_strong(T& expected, T desired);
```

它原子地执行：

```
if (当前值 == expected) {
    当前值 = desired;  return true;
} else {
    expected = 当前值; return false;    // 注意：会修改 expected
}
```

**CAS 循环**是无锁编程的基本套路：

1. 读出当前值；
2. 根据当前值算出新值；
3. 用 CAS 尝试写入：如果这期间没有别的线程改过它，就成功；否则 CAS 失败，并且 `expected` 已经被更新为最新的值，回到第 2 步重试。

上面的 `updateMax` 就是这样实现的：4 个线程同时更新最大值，最终结果正确。

**`weak` 和 `strong` 的区别**：`compare_exchange_weak` 在某些平台（如 ARM）上即使当前值等于 `expected` 也可能失败（伪失败），但它在循环中性能更好。**在循环中用 `weak`，单次调用用 `strong`。**

## 4. 无锁与有锁

`is_lock_free()` 告诉你某个原子类型是否真正由 CPU 的原子指令实现。

- `int`、`long`、指针等**基本类型通常是无锁的**。
- 较大的类型（上例中 64 字节的结构体），CPU 没有对应的原子指令，标准库会在内部**用锁来模拟**。它仍然是线程安全的，只是失去了性能优势。在 GCC 上使用这样的类型可能还需要链接 `-latomic`。

`std::atomic<T>` 要求 `T` 是**可平凡复制**的类型（可以用 `memcpy` 复制），所以 `std::atomic<std::string>` 是无法编译的。

C++ 标准只保证一个类型一定是无锁的：`std::atomic_flag`，后面实现自旋锁时会用到它。

## 5. 内存序：CPU 会重排你的代码

从这一节开始是进阶内容。

### 5.1 一个「不可能」的结果

看下面这个经典实验：`x`、`y` 初始都是 0，两个线程同时运行：

```
线程 1：          线程 2：
x = 1;            y = 1;
r1 = y;           r2 = x;
```

两个线程都是「先写自己的变量，再读对方的变量」。直觉上，无论怎么交错，至少有一个线程的写操作会先发生，所以 **`r1` 和 `r2` 不可能同时为 0**。

我们用原子变量把这个实验重复 20 万次（完整代码较长，核心部分如下，每一轮用一个原子变量 `round_` 让两个线程同时开始）：

```cpp
std::atomic<int> x(0), y(0);
int r1, r2;

// 线程 1                                  // 线程 2
x.store(1, Store);                         y.store(1, Store);
r1 = y.load(Load);                         r2 = x.load(Load);
```

分别用三种内存序（`Store` 和 `Load` 是模板参数）运行，统计 `r1 == 0 && r2 == 0` 的次数：

```
relaxed: r1 == r2 == 0 出现 199903 次 / 200000
release/acquire: 出现 0 次 / 200000
seq_cst: 出现 0 次 / 200000
```

使用 `relaxed` 时，「不可能」的结果出现了 **99.95%** 的次数！

### 5.2 发生了什么

查看编译器生成的 ARM64 汇编：

```
relaxed:          seq_cst:
  str  w8, [x]      stlr  w9, [x]
  ldr  w0, [y]      ldar  w0, [y]
```

编译器老老实实地按照源代码的顺序，先写 `x`（`str`）再读 `y`（`ldr`），所以**重排是 CPU 做的**。

现代 CPU 为了速度，每个核心都有一个**存储缓冲区**（store buffer）：执行写操作时，数据先放进缓冲区，CPU 不等它真正写入缓存就继续执行后面的指令。于是线程 1 的 `x = 1` 还在自己核心的缓冲区里、对其他核心不可见时，它已经读到了 `y` 的旧值 0；线程 2 同理。从其他核心的角度看，就好像「读」跑到了「写」的前面。

除了 CPU，**编译器也会重排**：只要不改变单线程程序的结果，编译器可以随意调整读写的顺序。

**单线程程序永远察觉不到这些重排**，因为它们都保证「单线程看起来和按顺序执行一样」。但多线程程序中，一个线程看到的另一个线程的内存操作顺序，可能和代码顺序完全不同。**内存序**（memory order）就是用来告诉编译器和 CPU：哪些重排是不允许的。

### 5.3 三种常用的内存序

所有原子操作都有一个可选的内存序参数，默认是 `std::memory_order_seq_cst`。

| 内存序 | 保证 | 典型用途 |
| --- | --- | --- |
| `memory_order_relaxed` | **只保证操作本身是原子的**，不约束它和其他读写之间的顺序 | 只关心最终数值的计数器、统计量 |
| `memory_order_release`（写） / `memory_order_acquire`（读） | **成对使用**：release 之前的所有写入，对 acquire 读到这个值的线程**全部可见** | 一个线程准备好数据，通知另一个线程使用 |
| `memory_order_seq_cst`（默认） | 在 release/acquire 的基础上，所有线程看到的**所有 seq_cst 操作的顺序是一致的** | 拿不准时就用它 |

另外还有 `memory_order_acq_rel`（用于读-改-写操作，同时具有 acquire 和 release 语义）和 `memory_order_consume`（实际上所有编译器都把它当作 acquire 处理，不建议使用）。

**这就是第 2.1 节中 `relaxed` 计数器更快的原因**：计数器只关心最终数值，不需要和其他内存操作排序，`relaxed` 让 CPU 少做了很多工作。

> **关于 5.1 节实验中 release/acquire 的结果**：C++ 标准其实**允许** release/acquire 在这个实验中出现 `r1 == r2 == 0`，因为 release/acquire 不禁止「先写后读」被重排。实验中出现 0 次，是因为 ARM64 的 `stlr`/`ldapr` 指令在这款芯片上恰好表现得更严格，**换一个平台或编译器结果可能不同**。如果代码的正确性依赖于「`r1` 和 `r2` 不能同时为 0」，必须使用 `seq_cst`。

## 6. 用 release / acquire 传递数据

这是内存序最重要的应用场景：一个线程准备数据，然后通过一个原子标志通知另一个线程。

```cpp
#include <atomic>
#include <iostream>
#include <thread>

int payload = 0;                     // 普通变量，不是原子的
std::atomic<bool> ready(false);

void producer()
{
    payload = 42;                                      // ① 写数据
    ready.store(true, std::memory_order_release);     // ② 发布：①一定在②之前对其他线程可见
}

void consumer()
{
    while (!ready.load(std::memory_order_acquire)) {} // ③ 获取：看到 true 后……
    std::cout << "读到: " << payload << '\n';          // ④ ……一定能看到①写入的数据
}

int main()
{
    std::thread c(consumer), p(producer);
    p.join();
    c.join();
    return 0;
}
```

运行结果：

```
读到: 42
```

关键在于 release 和 acquire 的**配对**：

- **release 写**（②）：它之前的所有读写（①），**不能被重排到它后面**。
- **acquire 读**（③）：它之后的所有读写（④），**不能被重排到它前面**。
- 当 ③ 读到了 ② 写入的值，就说 ② **同步于**（synchronizes-with）③。此时 ② 之前的所有写入，对 ③ 之后的代码**全部可见**。

`payload` 是一个普通的 `int`，没有加锁，也不是原子变量，但它的访问是安全的，因为 release/acquire 在两个线程之间建立了「先行发生」（happens-before）关系：① 先于 ②，② 同步于 ③，③ 先于 ④，所以 ① 先于 ④。

把两处内存序都改成 `relaxed`，用 ThreadSanitizer 运行：

```
WARNING: ThreadSanitizer: data race
SUMMARY: ThreadSanitizer: data race message_relaxed.cpp:17 in consumer()
```

第 17 行正是 `consumer` 中读取 `payload` 的那一行。`relaxed` 不建立同步关系，对 `payload` 的读写就构成了数据竞争。在 ARM 这类「弱内存模型」的 CPU 上，消费者确实可能看到 `ready == true`，`payload` 却还是 0。用 release/acquire 版本运行 ThreadSanitizer，则没有任何警告。

> **互斥锁就是这样实现的**：`lock()` 是一次 acquire 操作，`unlock()` 是一次 release 操作。所以上一个线程在临界区内的所有写入，对下一个获得锁的线程都可见。

## 7. 用 atomic_flag 实现自旋锁

`std::atomic_flag` 是最简单的原子类型，只有两个操作：`test_and_set()`（设为 true 并返回旧值）和 `clear()`（设为 false）。用它可以实现一个**自旋锁**：

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

class SpinLock {
public:
    void lock()
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // 已被占用，原地自旋等待
        }
    }
    void unlock() { flag_.clear(std::memory_order_release); }
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;   // 唯一保证无锁的原子类型
};

int main()
{
    SpinLock spin;
    int counter = 0;
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i)
        ts.emplace_back([&] {
            for (int j = 0; j < 100000; ++j) {
                spin.lock();
                ++counter;
                spin.unlock();
            }
        });
    for (auto& t : ts) t.join();
    std::cout << "counter = " << counter << '\n';
    return 0;
}
```

运行结果：

```
counter = 400000
```

ThreadSanitizer 也没有报告任何问题。`lock()` 用 acquire、`unlock()` 用 release，正好对应第 6 节的配对规则。由于 `SpinLock` 提供了 `lock()` 和 `unlock()`，它还可以直接配合 `std::lock_guard<SpinLock>` 使用。

**自旋锁和互斥锁的区别**：拿不到锁时，自旋锁在原地循环（一直占用 CPU），互斥锁则让线程休眠。临界区非常短（几十纳秒）时，自旋锁避免了休眠和唤醒的开销；临界区较长时，自旋会白白浪费 CPU。**一般情况下用 `std::mutex`**，现代的 mutex 实现在拿不到锁时也会先自旋一小会儿再休眠。

## 8. 常见错误

### 8.1 多个原子操作组合起来不是原子的

```cpp
std::atomic<int> count(0);

if (count.load() < 10)   // ① 检查
    ++count;             // ② 修改
```

① 和 ② 各自是原子的，但**组合起来不是**：两个线程可能同时通过检查（都读到 9），然后都执行 `++`，结果变成 11。需要「检查后修改」时，用 CAS 循环把它变成一个原子操作：

```cpp
int cur = count.load();
while (cur < 10 && !count.compare_exchange_weak(cur, cur + 1)) {}
```

### 8.2 volatile 不是原子的

在 Java 和 C# 中，`volatile` 有线程同步的语义；但在 C/C++ 中，**`volatile` 只是告诉编译器不要优化掉这个变量的读写**（用于硬件寄存器、信号处理），它不保证原子性，也不提供任何内存序保证。多线程中请使用 `std::atomic`。

### 8.3 随意使用 relaxed

`relaxed` 只适合「只关心这个变量本身的值」的场合，比如统计计数器。如果其他线程要根据这个原子变量的值去读取别的数据，就必须使用 release/acquire 或 seq_cst。**拿不准的时候，用默认的 `seq_cst`**：它稍慢一点，但永远是对的。

## 9. 进阶

### 9.1 伪共享

两个原子变量即使互不相关，如果它们落在同一个**缓存行**（通常是 64 字节）中，不同核心频繁修改它们时，缓存行会在核心之间来回传递，性能会严重下降，这叫**伪共享**（false sharing）。解决办法是用 `alignas` 让它们分别占据不同的缓存行：

```cpp
struct Counters {
    alignas(64) std::atomic<long long> a;
    alignas(64) std::atomic<long long> b;   // 和 a 不在同一个缓存行
};
```

C++17 提供了 `std::hardware_destructive_interference_size` 常量来代替硬编码的 64。

### 9.2 无锁数据结构很难

用 CAS 可以实现无锁的栈、队列，但正确地实现它们非常困难，需要处理 **ABA 问题**（一个值从 A 变成 B 又变回 A，CAS 无法察觉中间的变化）、内存回收（何时才能安全地 `delete` 一个可能正被其他线程读取的节点）等一系列问题。**除非性能分析证明锁是瓶颈，否则优先使用互斥锁**；确实需要时，使用经过充分验证的库（如 Boost.Lockfree、folly）。

## 10. 小结

- `std::atomic<T>` 让单个变量的操作不可分割，不需要加锁；简单的计数、标志位用它比互斥锁快得多
- 常用操作：`load`、`store`、`exchange`、`fetch_add`，以及**比较并交换** `compare_exchange_weak/strong`
- **CAS 循环**可以把「读取 → 计算 → 写回」变成原子操作；循环中用 `weak`
- 编译器和 CPU 都会重排内存操作，单线程察觉不到，多线程中会导致意外的结果；**内存序**约束这些重排
- **`relaxed`**：只保证原子性，适合计数器；**release/acquire**：配对使用，在线程间安全地发布数据；**`seq_cst`**（默认）：最严格，拿不准时就用它
- 多个原子操作组合起来不是原子的；`volatile` 不能用于线程同步

## 11. 练习

1. 用 `std::atomic<bool>` 和 `compare_exchange_strong` 实现一个函数 `bool tryInit()`：多个线程同时调用时，只有第一个调用者返回 `true`。
2. 下面三种场景，分别应该使用哪种内存序？
   - 统计服务器处理的请求总数，只在程序结束时打印
   - 后台线程加载完配置后，设置 `loaded = true`，工作线程看到 `loaded` 后读取配置
   - 不确定的场景
3. 下面的代码有什么问题？

   ```cpp
   std::atomic<int> balance(100);
   void withdraw(int amount)
   {
       if (balance >= amount)
           balance -= amount;
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::atomic<bool> initialized(false);
   bool tryInit()
   {
       bool expected = false;
       return initialized.compare_exchange_strong(expected, true);
   }
   ```

   只有第一个线程能把 `false` 改成 `true`，后来的线程 CAS 都会失败。（C++11 的 `std::call_once` 提供了更完整的「只执行一次」功能，也会处理执行中抛异常等情况。）

2. 第一个用 `relaxed`：只关心最终数值，而且所有线程 `join` 之后再读取，`join` 本身就建立了同步关系；第二个：设置时用 `release`，读取时用 `acquire`；第三个：用默认的 `seq_cst`。
3. 「检查余额」和「扣款」是两个独立的原子操作，两个线程可能同时通过检查，导致余额变成负数。用 CAS 循环修正：

   ```cpp
   bool withdraw(int amount)
   {
       int cur = balance.load();
       while (cur >= amount) {
           if (balance.compare_exchange_weak(cur, cur - amount))
               return true;
       }
       return false;
   }
   ```

</details>
