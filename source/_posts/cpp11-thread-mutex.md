---
title: 多线程入门：std::thread 与互斥锁
date: 2026-10-24 09:00:00
updated: 2026-10-24 09:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 多线程
  - 并发
order: 13
description: C++11 第一次把线程写进了标准库。本文从创建线程开始，用实验展示数据竞争的后果，讲清楚 mutex、lock_guard、unique_lock 的用法，以及死锁的成因和避免方法，并介绍检测数据竞争的 ThreadSanitizer。
disableNunjucks: true
---

> **本文目标**：会用 `std::thread` 创建线程，理解 `join` 和 `detach`；知道什么是数据竞争，以及为什么「结果正确」不代表没有数据竞争；会用 `std::mutex` 配合 `lock_guard`、`unique_lock` 保护共享数据；能识别和避免死锁；会用 ThreadSanitizer 检测数据竞争。
>
> **前置知识**：《Lambda 表达式》《std::function 与 std::bind》中的 `std::ref`。
>
> **编译环境**：`g++ -std=c++11 -pthread` / `clang++ -std=c++11 -pthread`。Linux 上使用 GCC 时需要加 `-pthread` 链接线程库。

## 1. 为什么需要多线程

现代 CPU 都有多个核心。一个普通程序只有一个线程，同一时刻只能用到一个核心。多线程可以：

- **并行计算**：把大任务拆成几份，同时在多个核心上算。
- **不阻塞**：下载文件、读写磁盘等需要等待的操作放到后台线程，界面和主逻辑不会卡住。

C++11 之前，C++ 标准完全不知道「线程」的存在，只能使用操作系统的 API：Linux 上是 `pthread`，Windows 上是 `CreateThread`，代码无法跨平台。C++11 引入了 `<thread>`、`<mutex>` 等头文件，同一份代码可以在所有平台上编译运行。

## 2. 创建线程

```cpp
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

void download(const std::string& file, int seconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(seconds * 100));
    std::cout << "下载完成: " << file << '\n';
}

int main()
{
    std::cout << "CPU 支持的并发线程数: " << std::thread::hardware_concurrency() << '\n';

    auto start = std::chrono::steady_clock::now();

    std::thread t1(download, "a.mp4", 3);               // 普通函数 + 参数
    std::thread t2([] {                                 // lambda
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "lambda 线程完成\n";
    });
    std::thread t3(download, "b.mp4", 1);

    t1.join();                                          // 等待线程结束
    t2.join();
    t3.join();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start).count();
    std::cout << "总耗时约 " << (ms + 50) / 100 * 100 << " ms（三个任务并行执行）\n";
    return 0;
}
```

运行结果（12 核的机器）：

```
CPU 支持的并发线程数: 12
下载完成: b.mp4
lambda 线程完成
下载完成: a.mp4
总耗时约 300 ms（三个任务并行执行）
```

要点：

1. **构造 `std::thread` 对象时，线程就立刻开始运行**。第一个参数是线程要执行的可调用对象（函数、lambda、函数对象），后面的参数会传给它。
2. 三个任务分别需要 300ms、200ms、100ms，总耗时却约等于最长的那个，因为它们是**同时**执行的。完成顺序取决于各自的耗时，而不是创建顺序。
3. **`join()`**：等待线程执行完毕。主线程会阻塞在这里。
4. `std::this_thread::sleep_for` 让当前线程休眠，`std::chrono` 是 C++11 的时间库。
5. `hardware_concurrency()` 返回硬件支持的并发线程数（通常是 CPU 逻辑核心数），可以作为决定开几个线程的参考。

### 2.1 join 还是 detach

每个 `std::thread` 对象在销毁之前，**必须**调用 `join()` 或 `detach()` 之一：

- **`join()`**：等待线程结束。之后这个 `thread` 对象不再关联任何线程。
- **`detach()`**：分离线程，让它在后台独立运行，`thread` 对象不再管它。

如果都没有调用：

```cpp
#include <iostream>
#include <thread>
int main()
{
    std::thread t([] { std::cout << "子线程运行\n"; });
    // 忘了 join() 或 detach()
    return 0;
}
```

运行结果：

```
libc++abi: terminating
子线程运行
```

程序直接被终止（退出码 134，即 `SIGABRT`）。`std::thread` 的析构函数发现线程还处于「可 join」状态，就会调用 `std::terminate()`。标准这样设计是因为两种默认行为都有问题：析构时自动 `join` 可能让程序意外卡住，自动 `detach` 则容易导致线程访问已经销毁的局部变量。

**建议：几乎总是用 `join()`。** `detach()` 后的线程可能在 `main` 结束、全局对象被销毁之后还在运行，非常难以管理。

### 2.2 参数是按值传递的

```cpp
#include <functional>
#include <iostream>
#include <string>
#include <thread>

void fill(std::string& out) { out = "子线程写入的数据"; }

int main()
{
    std::string result = "初始值";
    // std::thread t(fill, result);       // 编译错误：thread 会复制参数，复制品无法绑定到 std::string&
    std::thread t(fill, std::ref(result)); // 用 std::ref 传引用
    t.join();
    std::cout << result << '\n';
    return 0;
}
```

运行结果：

```
子线程写入的数据
```

和 `std::bind` 一样，**`std::thread` 会把所有参数复制一份**保存到新线程中，即使函数的参数是引用类型。想传引用必须用 `std::ref`。直接写 `std::thread t(fill, result);` 会编译失败，libc++ 的报错是：

```
error: static assertion failed due to requirement '__is_invocable_v<void (*)(std::string &), std::string>'
```

意思是：无法用一个 `std::string` 的副本（右值）去调用参数为 `std::string&` 的函数。

> 用 `std::ref` 传引用时，**你要保证被引用的对象比线程活得久**。上例中 `result` 在 `t.join()` 之后才离开作用域，所以是安全的。

## 3. 数据竞争

多个线程访问同一个变量，并且**至少有一个在写**，又没有任何同步措施，就是**数据竞争**（data race）。C++ 标准规定：**有数据竞争的程序，行为是未定义的。**

来做个实验：4 个线程，每个线程把全局变量 `counter` 加 10 万次，结果应该是 40 万。

```cpp
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

int counter = 0;
std::mutex mtx;

void unsafeIncrement(int n)
{
    for (int i = 0; i < n; ++i)
        ++counter;                           // 数据竞争！
}

void safeIncrement(int n)
{
    for (int i = 0; i < n; ++i) {
        std::lock_guard<std::mutex> lock(mtx);   // 构造时加锁，析构时解锁
        ++counter;
    }
}

template <typename F>
int run(F func)
{
    counter = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(func, 100000);
    for (auto& t : threads) t.join();
    return counter;
}

int main()
{
    for (int round = 1; round <= 3; ++round)
        std::cout << "第 " << round << " 次  不加锁: " << run(unsafeIncrement)
                  << "    加锁: " << run(safeIncrement) << "    （期望 400000）\n";
    return 0;
}
```

不开优化（`-O0`）编译，运行结果：

```
第 1 次  不加锁: 111332    加锁: 400000    （期望 400000）
第 2 次  不加锁: 116330    加锁: 400000    （期望 400000）
第 3 次  不加锁: 125709    加锁: 400000    （期望 400000）
```

不加锁时，40 万次加法丢了将近 30 万次，而且每次运行的结果都不一样。

### 3.1 为什么会丢

`++counter` 看起来是一条语句，在 CPU 上其实是三步：

```
1. 从内存读取 counter 到寄存器
2. 寄存器加 1
3. 把寄存器写回内存
```

两个线程同时执行时，可能发生这样的交错：

```
线程 A：读取 counter = 5
线程 B：读取 counter = 5
线程 A：计算 6，写回 counter = 6
线程 B：计算 6，写回 counter = 6     ← A 的那次加法丢失了
```

### 3.2 结果正确不代表没有问题

同样的代码，开启优化（`-O2`）编译：

```
第 1 次  不加锁: 400000    加锁: 400000    （期望 400000）
```

不加锁的版本居然每次都对了！原因是编译器发现循环里只有 `++counter`，于是把整个循环优化成了一句 `counter += n`，竞争的窗口变得极小。但这**纯属巧合**：代码稍微改动，或者换一个编译器、换一台机器，问题就可能重新出现。

所以**不能靠运行结果判断有没有数据竞争**，需要用工具来检测。GCC 和 Clang 都内置了 **ThreadSanitizer**，编译时加上 `-fsanitize=thread -g`：

```cpp
#include <thread>
int counter = 0;
void work() { for (int i = 0; i < 1000; ++i) ++counter; }
int main()
{
    std::thread a(work), b(work);
    a.join(); b.join();
    return counter == 0;
}
```

运行结果：

```
WARNING: ThreadSanitizer: data race (pid=24988)
  Write of size 4 at 0x0001000a8000 by thread T2:
  Previous write of size 4 at 0x0001000a8000 by thread T1:
SUMMARY: ThreadSanitizer: data race c3_tsan.cpp:3 in work()
```

它准确指出了哪两个线程、在哪一行代码、对同一个地址发生了竞争。**写多线程代码时，务必经常用 ThreadSanitizer 跑一遍测试。**

## 4. 互斥锁

### 4.1 std::mutex 与 lock_guard

**互斥锁**（mutex）保证同一时刻只有一个线程能进入被它保护的代码段（**临界区**）。其他线程试图加锁时会被阻塞，直到持有锁的线程解锁。

`std::mutex` 有 `lock()` 和 `unlock()` 两个成员函数，但**不要直接调用它们**：如果临界区里抛出异常或者提前 `return`，`unlock()` 就不会被执行，锁永远不会被释放，其他线程全部卡死。

正确的做法是 RAII：**`std::lock_guard`** 在构造时加锁，析构时解锁，就像智能指针管理内存一样管理锁：

```cpp
{
    std::lock_guard<std::mutex> lock(mtx);   // 加锁
    ++counter;
}                                            // 离开作用域，自动解锁
```

上面实验中的 `safeIncrement` 就是这样写的，三次都得到了正确的 400000。

**用好锁的原则：**

1. **所有访问共享数据的地方都要加同一把锁**，包括只读的地方。只给写操作加锁、读操作不加锁，仍然是数据竞争。
2. **临界区尽量小**：只把真正访问共享数据的代码放在锁里，耗时的计算、I/O 放到锁外面。
3. **锁和它保护的数据放在一起**，比如作为同一个类的成员，并写清楚注释。

### 4.2 unique_lock

`std::unique_lock` 比 `lock_guard` 更灵活：可以中途解锁再加锁、可以尝试加锁、可以延迟加锁，还可以移动（转移锁的所有权）。

```cpp
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

std::mutex mtx;
std::vector<int> data;

void produce(int id)
{
    std::unique_lock<std::mutex> lock(mtx);     // 和 lock_guard 一样，构造时加锁
    data.push_back(id);
    lock.unlock();                              // 可以提前手动解锁
    // ……这里做不需要锁的耗时工作……
    lock.lock();                                // 也可以再次加锁
    data.push_back(id * 10);
}                                               // 析构时如果持有锁，自动解锁

int main()
{
    std::thread t1(produce, 1), t2(produce, 2);
    t1.join();
    t2.join();
    std::cout << "data.size() = " << data.size() << '\n';

    std::unique_lock<std::mutex> l(mtx, std::try_to_lock);   // 尝试加锁，失败也不阻塞
    std::cout << "try_to_lock " << (l.owns_lock() ? "成功" : "失败") << '\n';
    return 0;
}
```

运行结果：

```
data.size() = 4
try_to_lock 成功
```

| | `lock_guard` | `unique_lock` |
| --- | --- | --- |
| 中途 `unlock()` / `lock()` | ✗ | ✓ |
| `std::try_to_lock` 尝试加锁 | ✗ | ✓ |
| `std::defer_lock` 延迟加锁 | ✗ | ✓ |
| 可移动 | ✗ | ✓ |
| 配合条件变量使用 | ✗ | ✓（必须用它） |
| 开销 | 最小 | 多一个 bool 标记 |

**默认用 `lock_guard`**，需要上面这些功能时再用 `unique_lock`。下一篇的条件变量就必须使用 `unique_lock`。

## 5. 死锁

### 5.1 死锁是怎么发生的

两个线程各自持有一把锁，又都在等对方手里的那把锁，谁也无法继续，这就是**死锁**（deadlock）。最典型的场景是转账：

```cpp
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

struct Account {
    explicit Account(int b) : balance(b) {}
    int balance;
    std::mutex m;
};

// 有死锁风险：两个线程以相反的顺序加锁
void transferBad(Account& from, Account& to, int amount)
{
    std::lock_guard<std::mutex> l1(from.m);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));   // 放大问题
    std::lock_guard<std::mutex> l2(to.m);
    from.balance -= amount;
    to.balance += amount;
}

// 正确：std::lock 同时锁住多个互斥量，内部算法避免死锁
void transferGood(Account& from, Account& to, int amount)
{
    std::lock(from.m, to.m);
    std::lock_guard<std::mutex> l1(from.m, std::adopt_lock);   // adopt_lock：接管已经加好的锁
    std::lock_guard<std::mutex> l2(to.m, std::adopt_lock);
    from.balance -= amount;
    to.balance += amount;
}

int main(int argc, char**)
{
    Account a(100), b(100);
    auto transfer = argc > 1 ? transferBad : transferGood;
    std::thread t1(transfer, std::ref(a), std::ref(b), 10);
    std::thread t2(transfer, std::ref(b), std::ref(a), 20);
    t1.join();
    t2.join();
    std::cout << "a = " << a.balance << ", b = " << b.balance << '\n';
    return 0;
}
```

使用 `transferBad`（带任意参数运行）：**程序永远卡住，3 秒后被手动终止**。使用 `transferGood`（不带参数运行）：

```
a = 110, b = 90
```

`transferBad` 中，线程 1 执行 `a → b`，先锁 `a` 再锁 `b`；线程 2 执行 `b → a`，先锁 `b` 再锁 `a`。线程 1 锁住 `a`、线程 2 锁住 `b` 之后，两者都在等对方，形成死锁。

### 5.2 如何避免死锁

1. **需要同时持有多把锁时，用 `std::lock` 一次性锁住它们**。它使用避免死锁的算法，不会出现「各拿一把」的局面。加锁后用 `lock_guard` 配合 `std::adopt_lock` 接管，保证自动解锁。C++17 的 `std::scoped_lock l(from.m, to.m);` 一行就能完成这两步。
2. **固定加锁顺序**：所有代码都按同样的顺序加锁（比如按账户编号从小到大），就不会形成循环等待。
3. **持有锁时不要调用外部代码**：比如在锁内调用用户传进来的回调函数，回调里可能又去加别的锁。
4. **能只用一把锁，就不用两把**。

## 6. 常见错误

1. **忘记 `join`**：`std::thread` 析构时程序被 `std::terminate` 终止。可以写一个 RAII 包装类在析构时自动 `join`（C++20 的 `std::jthread` 就是这样做的）。
2. **线程访问已经销毁的局部变量**：用 `std::ref` 传引用、用 lambda 按引用捕获，或者 `detach` 之后，都要确保变量的生命周期覆盖线程的整个运行期。
3. **只给写操作加锁**：读操作同样需要加锁。
4. **用同一个线程对同一个 `std::mutex` 加锁两次**：会死锁（标准规定为未定义行为）。确实需要递归加锁时用 `std::recursive_mutex`，但这通常说明设计可以改进。
5. **把锁的范围扩大到整个函数**：锁住的时间越长，其他线程等待越久，多线程就退化成了单线程。

## 7. 进阶

### 7.1 线程的开销

创建一个线程需要操作系统分配栈空间（通常 512KB 到 8MB）、建立内核数据结构，开销在**几十微秒**量级。如果每个小任务都新建一个线程，创建和销毁线程的时间可能比任务本身还长。实际项目中通常使用**线程池**：预先创建固定数量的线程，把任务放进队列，由这些线程反复取任务执行。线程池的实现需要用到下一篇的条件变量。

### 7.2 线程数不是越多越好

计算密集型任务，线程数超过 CPU 核心数后，多出来的线程只会增加上下文切换的开销，不会更快。一般取 `std::thread::hardware_concurrency()`。等待 I/O 为主的任务，线程大部分时间在休眠，可以开得更多一些。

### 7.3 加锁的开销

没有竞争时，`std::mutex` 的加锁和解锁只需要几十纳秒（在 Linux 上它基于 futex，无竞争时完全在用户态完成）。但锁竞争激烈时，线程需要进入内核休眠和唤醒，开销会上升到微秒级。本文的实验中，加锁版本每次加法都要加锁一次，比不加锁慢得多。对于简单的计数器，下一篇之后的《原子操作与内存序》会介绍更轻量的 `std::atomic`。

## 8. 小结

- `std::thread t(函数, 参数...)` 创建线程并立即运行；参数会被**复制**，传引用要用 `std::ref`
- `thread` 对象销毁前必须 `join()` 或 `detach()`，否则程序终止；**优先 `join()`**
- 多个线程访问同一数据且有写操作，必须同步，否则是**未定义行为**；结果正确不代表没有数据竞争，用 **ThreadSanitizer** 检测
- 用 `std::mutex` + **`std::lock_guard`**（RAII）保护共享数据，需要灵活控制时用 `std::unique_lock`
- 同时获取多把锁时用 `std::lock`（C++17 用 `std::scoped_lock`），或固定加锁顺序，避免死锁

## 9. 练习

1. 把一个包含 1000 万个整数的 `vector` 分成 4 段，用 4 个线程分别求和，最后汇总。每个线程应该把结果写到哪里，才不需要加锁？
2. 写一个线程安全的计数器类 `SafeCounter`，提供 `increment()` 和 `get()` 两个成员函数。
3. 下面的代码有什么问题？

   ```cpp
   void start()
   {
       int value = 42;
       std::thread t([&value] { std::cout << value << '\n'; });
       t.detach();
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 准备一个 `std::vector<long long> partial(4)`，第 i 个线程只写 `partial[i]`。不同线程写的是**不同的内存位置**，不构成数据竞争，不需要加锁。所有线程 `join` 之后，主线程再把 4 个部分和相加。

   ```cpp
   std::vector<long long> partial(4);
   std::vector<std::thread> ts;
   std::size_t chunk = v.size() / 4;
   for (int i = 0; i < 4; ++i) {
       auto begin = v.begin() + i * chunk;
       auto end = (i == 3) ? v.end() : begin + chunk;
       ts.emplace_back([begin, end, &partial, i] {
           partial[i] = std::accumulate(begin, end, 0LL);
       });
   }
   for (auto& t : ts) t.join();
   long long total = std::accumulate(partial.begin(), partial.end(), 0LL);
   ```

2. 参考代码：

   ```cpp
   class SafeCounter {
   public:
       void increment()
       {
           std::lock_guard<std::mutex> lock(m_);
           ++value_;
       }
       int get() const
       {
           std::lock_guard<std::mutex> lock(m_);   // 读也要加锁
           return value_;
       }
   private:
       mutable std::mutex m_;   // mutable：允许在 const 成员函数中加锁
       int value_ = 0;
   };
   ```

3. 线程被 `detach` 后，`start()` 很快就返回了，局部变量 `value` 随之销毁，而线程可能还没来得及读取它，访问的是已经销毁的变量。应该按值捕获 `[value]`，并且最好用 `join` 代替 `detach`。

</details>
