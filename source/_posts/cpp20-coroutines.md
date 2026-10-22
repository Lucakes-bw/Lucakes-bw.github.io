---
title: 协程入门：可以暂停的函数
date: 2026-10-26 16:00:00
updated: 2026-10-26 16:00:00
categories:
  - C/C++
  - C++20
tags:
  - C++20
  - 协程
order: 4
description: 协程是可以在中途暂停、之后再从暂停处继续执行的函数。C++20 只提供了协程的底层机制，本文从零实现一个生成器，用打印跟踪协程和调用者交替执行的过程，再用 co_await 写一个单线程的「事件循环」，最后讲清楚协程帧的内存分配和引用参数悬空的陷阱。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解协程和普通函数的区别；认识 `co_yield`、`co_await`、`co_return`；能看懂 `promise_type`、`coroutine_handle` 在协程中的作用；会实现一个可以用于范围 for 的生成器；理解 `co_await` 如何让出执行权；知道协程帧的分配和引用参数悬空的问题。
>
> **前置知识**：类、模板、迭代器，C++11 章节的《Lambda 表达式》。本文是 C++20 中最难的一篇，第一次阅读时可以先跳过 `promise_type` 的细节，重点理解协程的执行过程。
>
> **编译环境**：`g++ -std=c++20` / `clang++ -std=c++20`，GCC 10+（GCC 10 需加 `-fcoroutines`）/ Clang 14+ / VS2019 16.8+，需要包含头文件 `<coroutine>`。

## 1. 什么是协程

**普通函数**一旦被调用，就会一直执行到 `return`，期间不能暂停。函数的局部变量保存在栈上，返回后就全部消失了。

**协程**（coroutine）是可以**在中途暂停，把控制权交还给调用者，之后再从暂停的地方继续执行**的函数。暂停期间，它的局部变量都被保存下来，恢复时一切照旧。

这种能力在两类场景中非常有用：

1. **生成器**：按需逐个产生值。比如一个无限的斐波那契数列，调用者要一个，它就算一个，算完就暂停，等待下一次请求。
2. **异步编程**：一个网络请求发出后，不必阻塞线程等待结果，而是暂停当前协程，让线程去处理别的事情；结果到达后，再恢复协程继续执行。代码可以像同步代码一样顺序书写，而不是层层嵌套的回调函数。

在 C++20 中，**函数体中只要出现了 `co_yield`、`co_await` 或 `co_return` 之一，它就是一个协程**：

| 关键字 | 作用 |
| --- | --- |
| `co_yield 值` | 产生一个值，并暂停 |
| `co_await 表达式` | 等待某件事，通常会暂停，事情完成后恢复 |
| `co_return 值` | 结束协程（相当于协程中的 `return`） |

## 2. C++20 只提供了底层机制

和其他语言不同，C++20 **只提供了协程的语言机制，几乎没有提供现成的工具**。想写一个生成器，必须自己定义它的返回类型。C++23 才提供了标准的 `std::generator`，而本文使用的 libc++ 还没有实现它（`#include <generator>` 报错 `file not found`）。

这反倒是理解协程原理的好机会。下面我们自己实现一个最小的生成器。

## 3. 实现一个生成器

```cpp
// generator.h：一个最小的生成器实现
#pragma once
#include <coroutine>
#include <exception>
#include <iterator>
#include <utility>

template <typename T>
class Generator {
public:
    struct promise_type {
        T current;                                              // 最近一次 co_yield 的值

        Generator get_return_object()
        {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }   // 创建后先暂停，等调用者来取
        std::suspend_always final_suspend() noexcept { return {}; }     // 结束后暂停，由 Generator 负责销毁
        std::suspend_always yield_value(T value)                        // co_yield value 时调用
        {
            current = std::move(value);
            return {};                                                  // 暂停，把控制权交还调用者
        }
        void return_void() {}
        void unhandled_exception() { throw; }
    };

    // 让 Generator 可以用于范围 for
    struct iterator {
        std::coroutine_handle<promise_type> h;
        iterator& operator++() { h.resume(); return *this; }            // 恢复协程，运行到下一个 co_yield
        const T& operator*() const { return h.promise().current; }
        bool operator==(std::default_sentinel_t) const { return h.done(); }
    };
    iterator begin() { h_.resume(); return iterator{h_}; }              // 第一次恢复，得到第一个值
    std::default_sentinel_t end() { return {}; }

    explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}
    Generator(Generator&& other) noexcept : h_(std::exchange(other.h_, {})) {}
    Generator(const Generator&) = delete;
    ~Generator() { if (h_) h_.destroy(); }                              // 销毁协程帧，释放内存

private:
    std::coroutine_handle<promise_type> h_;
};
```

这段代码有三个关键角色：

1. **协程句柄** `std::coroutine_handle<promise_type>`：指向一个协程实例的「遥控器」。`resume()` 让协程从暂停处继续执行，`done()` 查询协程是否已经结束，`destroy()` 销毁协程。
2. **promise 对象** `promise_type`：编译器要求协程的返回类型中必须有一个名为 `promise_type` 的嵌套类型，它定义了协程的行为：
   - `get_return_object()`：协程被调用时，创建返回给调用者的对象（这里是 `Generator`）；
   - `initial_suspend()`：协程开始执行之前是否先暂停；
   - `yield_value(v)`：遇到 `co_yield v` 时调用，这里把值保存在 `current` 中，然后暂停；
   - `final_suspend()`：协程执行完毕后是否暂停；
   - `return_void()` 和 `unhandled_exception()`：处理 `co_return` 和异常。
3. **返回对象** `Generator`：调用者拿到的东西。它持有协程句柄，提供 `begin()` / `end()` 让它可以用于范围 for，并在析构时销毁协程。

`std::suspend_always` 和 `std::suspend_never` 是标准库提供的两个现成的「可等待对象」，分别表示「暂停」和「不暂停」。

### 3.1 使用生成器

有了 `Generator`，写生成器就非常自然了：

```cpp
#include <iostream>
#include "generator.h"

// 协程：函数体中出现了 co_yield，它就是一个协程
Generator<int> range(int first, int last)
{
    for (int i = first; i < last; ++i)
        co_yield i;
}

// 无限序列：只要调用者还在要，就一直产生下去
Generator<long long> fibonacci()
{
    long long a = 0, b = 1;
    while (true) {
        co_yield a;
        auto next = a + b;
        a = b;
        b = next;
    }
}

int main()
{
    for (int x : range(1, 6)) std::cout << x << ' ';
    std::cout << '\n';

    int count = 0;
    for (long long f : fibonacci()) {
        if (++count > 15) break;          // 调用者决定什么时候停止
        std::cout << f << ' ';
    }
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
1 2 3 4 5 
0 1 1 2 3 5 8 13 21 34 55 89 144 233 377 
```

`fibonacci()` 里是一个 `while (true)` 死循环，却不会让程序卡住：每次 `co_yield` 都会暂停，只有调用者要下一个值时才继续。调用者 `break` 之后，`Generator` 析构，销毁协程，那个「死循环」就永远停在了暂停的地方。用普通函数实现同样的功能，要么一次算出固定数量的结果存进 `vector`，要么写一个手动保存状态（`a` 和 `b`）的类，都没有这么直观。

## 4. 跟踪协程的执行过程

在协程和调用者中都加上打印，观察它们是怎样交替执行的：

```cpp
#include <iostream>
#include "generator.h"

Generator<int> counter()
{
    std::cout << "    [协程] 开始执行\n";
    for (int i = 1; i <= 3; ++i) {
        std::cout << "    [协程] 产生 " << i << "，暂停\n";
        co_yield i;
        std::cout << "    [协程] 从暂停处恢复，i = " << i << "\n";
    }
    std::cout << "    [协程] 执行完毕\n";
}

int main()
{
    std::cout << "[main] 调用 counter()\n";
    auto gen = counter();                 // 只创建协程，还没有开始执行
    std::cout << "[main] 开始遍历\n";
    for (int x : gen)
        std::cout << "[main] 拿到 " << x << "\n";
    std::cout << "[main] 遍历结束\n";
    return 0;
}
```

运行结果：

```
[main] 调用 counter()
[main] 开始遍历
    [协程] 开始执行
    [协程] 产生 1，暂停
[main] 拿到 1
    [协程] 从暂停处恢复，i = 1
    [协程] 产生 2，暂停
[main] 拿到 2
    [协程] 从暂停处恢复，i = 2
    [协程] 产生 3，暂停
[main] 拿到 3
    [协程] 从暂停处恢复，i = 3
    [协程] 执行完毕
[main] 遍历结束
```

对照 `Generator` 的实现，逐步理解：

1. **调用 `counter()` 时，函数体一行都没有执行**。编译器在堆上创建了一个**协程帧**（保存局部变量、参数、promise 对象和暂停位置），调用 `get_return_object()` 得到 `Generator`，然后调用 `initial_suspend()`，它返回 `suspend_always`，协程立刻暂停，控制权回到 `main`。
2. 范围 for 调用 `begin()`，其中的 `h_.resume()` 让协程**第一次**开始执行，一直运行到 `co_yield 1`。
3. `co_yield 1` 调用 `yield_value(1)`，把 1 保存到 `current`，然后暂停，控制权回到 `begin()`，`main` 拿到 1。
4. 范围 for 调用 `++`，其中的 `h.resume()` 让协程**从 `co_yield` 的下一行**继续，局部变量 `i` 仍然是 1，循环继续，直到 `co_yield 2` 再次暂停……
5. 第 3 次恢复后，循环结束，协程执行到函数末尾，调用 `final_suspend()` 暂停。此时 `h.done()` 为 `true`，范围 for 结束。
6. `gen` 离开作用域，析构函数调用 `destroy()`，释放协程帧。

**要点：协程和调用者在同一个线程上交替执行，任何时刻只有一方在运行。** 这和多线程完全不同，不需要任何锁。

## 5. co_await：等待并让出执行权

`co_yield` 用于产生值，`co_await` 用于**等待某件事**。被 `co_await` 的对象叫做**可等待对象**（awaitable），它需要提供三个函数：

| 函数 | 调用时机 | 作用 |
| --- | --- | --- |
| `await_ready()` | 首先调用 | 返回 `true` 表示结果已经就绪，不需要暂停 |
| `await_suspend(handle)` | 决定暂停后调用 | 拿到当前协程的句柄，安排**以后由谁、在什么时候恢复它** |
| `await_resume()` | 协程被恢复时调用 | 它的返回值就是整个 `co_await` 表达式的值 |

下面用 `co_await` 实现一个单线程的「事件循环」：三个任务轮流执行，每执行一步就主动让出：

```cpp
#include <coroutine>
#include <iostream>
#include <queue>
#include <string>

// 一个极简的“事件循环”：保存所有等待中的协程，按顺序恢复它们
std::queue<std::coroutine_handle<>> readyQueue;

// 可等待对象：co_await yieldNow() 会让出执行权，排到队尾
struct YieldNow {
    bool await_ready() const noexcept { return false; }                 // 总是需要暂停
    void await_suspend(std::coroutine_handle<> h) { readyQueue.push(h); }   // 暂停时把自己放进队列
    void await_resume() const noexcept {}                               // 恢复时的返回值（这里没有）
};
YieldNow yieldNow() { return {}; }

// 最简单的协程返回类型：创建后立即运行，结束时自动销毁
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
};

Task worker(std::string name, int steps)
{
    for (int i = 1; i <= steps; ++i) {
        std::cout << name << " 第 " << i << " 步\n";
        co_await yieldNow();              // 暂停自己，让其他协程运行
    }
    std::cout << name << " 完成\n";
}

int main()
{
    worker("下载", 3);
    worker("解析", 2);
    worker("渲染", 1);
    while (!readyQueue.empty()) {         // 事件循环：轮流恢复每个协程
        auto h = readyQueue.front();
        readyQueue.pop();
        h.resume();
    }
    return 0;
}
```

运行结果：

```
下载 第 1 步
解析 第 1 步
渲染 第 1 步
下载 第 2 步
解析 第 2 步
渲染 完成
下载 第 3 步
解析 完成
下载 完成
```

三个任务的输出**交错**在一起，看起来像是在并发执行，但整个程序只有**一个线程**：

1. `worker("下载", 3)` 被调用，`initial_suspend` 返回 `suspend_never`，立即开始执行，打印「下载 第 1 步」后遇到 `co_await yieldNow()`。
2. `await_ready()` 返回 `false`，于是协程暂停，`await_suspend` 把这个协程的句柄放进 `readyQueue`，控制权回到 `main`。
3. `main` 接着调用 `worker("解析", 2)` 和 `worker("渲染", 1)`，过程相同。
4. 事件循环从队列中依次取出句柄并 `resume()`，每个协程执行一步后又把自己放回队尾……

这就是 Python 的 asyncio、JavaScript 的 async/await 背后的原理。在真实的异步网络库中，`await_suspend` 不是把协程放进队列，而是向操作系统注册「数据到达时通知我」，事件循环收到通知后再恢复对应的协程。C++ 的异步库（如 Asio）就是基于这个机制提供 `co_await socket.async_read(...)` 这样的接口的。

## 6. 常见错误

### 6.1 引用参数悬空

```cpp
#include <iostream>
#include <string>
#include "generator.h"

Generator<char> chars(const std::string& s)     // 参数是引用！
{
    for (char c : s) co_yield c;
}

int main()
{
    auto g = chars(std::string("a-long-enough-string-to-live-on-the-heap"));   // 临时 string
    for (char c : g) std::cout << c;             // 协程恢复时，临时 string 早已销毁
    std::cout << '\n';
    return 0;
}
```

用 AddressSanitizer 运行：

```
ERROR: AddressSanitizer: stack-use-after-scope
```

协程帧会保存参数，但对于**引用参数，保存的只是引用本身**。`chars(...)` 返回时（在 `initial_suspend` 处暂停），临时 `string` 在这一行结束时就被销毁了；之后遍历时协程才开始执行，访问的是悬空的引用。

**规则：协程的参数尽量按值传递。** 改成 `Generator<char> chars(std::string s)`，字符串就会被拷贝（或移动）进协程帧，与调用者的变量无关。同样，指针参数、`string_view` 参数、lambda 协程按引用捕获的变量，也都有这个问题。

### 6.2 忘记销毁协程

协程帧是在堆上分配的，暂停在 `final_suspend` 的协程必须调用 `destroy()` 释放。本文的 `Generator` 在析构函数中处理了这件事；如果自己设计返回类型时漏掉了，就会内存泄漏。反过来，如果 `final_suspend` 返回 `suspend_never`（像 `Task` 那样），协程结束时会自动销毁，这时**再调用 `destroy()` 就是重复释放**。

### 6.3 在多线程中恢复协程

`resume()` 在哪个线程调用，协程就在哪个线程继续执行。如果 `await_suspend` 把句柄交给了另一个线程，协程恢复后的代码就运行在那个线程上，访问共享数据时需要同步。

## 7. 进阶

### 7.1 协程帧的内存分配

用自定义的 `operator new` 统计创建一个 `range(0, 1000)` 协程时的堆分配：

```
不开优化（-O0）：创建一个协程: 堆分配 1 次, 共 40 字节, sum = 499500
开启优化（-O2）：创建一个协程: 堆分配 0 次, 共 0 字节, sum = 499500
```

不开优化时，协程帧需要一次 40 字节的堆分配。开启优化后，编译器发现协程的生命周期完全在 `main` 的这个循环之内，就把协程帧直接放在了调用者的栈上，**完全消除了堆分配**。这种优化叫做 **HALO**（Heap Allocation eLision Optimization）。但它不是标准保证的，协程被存储起来、跨函数传递时，通常就无法优化掉了。

如果需要精确控制分配，`promise_type` 可以重载 `operator new`，让协程帧从内存池中分配。

### 7.2 无栈协程

C++20 的协程是**无栈协程**（stackless）：协程帧只保存这一个函数的局部变量，`co_yield` / `co_await` 只能出现在协程函数本身中，不能在它调用的普通函数里暂停。好处是每个协程只占用几十到几百字节，可以同时存在成千上万个。

与之相对的是**有栈协程**（stackful，如 Go 语言的 goroutine、Boost.Context），每个协程有自己独立的栈，可以在任意深度的调用中暂停，但每个协程要占用 KB 级的内存。

### 7.3 实际项目中怎么用

C++20 协程的底层机制非常灵活，但直接使用很繁琐。实际项目中通常使用现成的库：

- **生成器**：C++23 的 `std::generator`（GCC 14 起提供），或者像本文一样自己写一个。
- **异步 I/O**：Asio（`asio::awaitable`）、cppcoro、folly::coro 等。

## 8. 小结

- 协程是可以暂停并在之后恢复的函数；函数体中出现 `co_yield`、`co_await`、`co_return` 之一就是协程
- C++20 只提供了底层机制：协程的返回类型必须包含 `promise_type`，用 `coroutine_handle` 恢复、查询和销毁协程
- 调用协程时只创建协程帧，`initial_suspend` 决定是否立即执行；`co_yield` 调用 `yield_value` 并暂停；`resume()` 从暂停处继续
- 协程和调用者在同一个线程上交替执行，不需要锁
- `co_await` 调用可等待对象的 `await_ready`、`await_suspend`、`await_resume`，可以实现事件循环和异步 I/O
- 协程的参数尽量按值传递，引用参数容易悬空；协程帧在堆上分配，优化后可能被消除

## 9. 练习

1. 用本文的 `Generator` 写一个协程 `squares(int n)`，依次产生 1² 到 n²。
2. 写一个协程 `primes()`，产生无限的质数序列，在 `main` 中取出前 10 个。
3. 写一个协程 `take(Generator<int> g, int n)`，从另一个生成器中只取前 n 个值。（提示：参数按值传递，在协程体内用范围 for 遍历 `g`。）

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   Generator<int> squares(int n)
   {
       for (int i = 1; i <= n; ++i) co_yield i * i;
   }
   ```

2. 参考代码：

   ```cpp
   Generator<int> primes()
   {
       for (int n = 2; ; ++n) {
           bool isPrime = true;
           for (int d = 2; d * d <= n; ++d)
               if (n % d == 0) { isPrime = false; break; }
           if (isPrime) co_yield n;
       }
   }

   int count = 0;
   for (int p : primes()) {
       if (++count > 10) break;
       std::cout << p << ' ';     // 2 3 5 7 11 13 17 19 23 29
   }
   ```

3. 参考代码，`g` 按值传入，被移动进 `take` 的协程帧，生命周期由 `take` 负责：

   ```cpp
   Generator<int> take(Generator<int> g, int n)
   {
       for (int x : g) {
           if (n-- <= 0) co_return;
           co_yield x;
       }
   }

   for (int x : take(primes(), 5)) std::cout << x << ' ';   // 2 3 5 7 11
   ```

</details>
