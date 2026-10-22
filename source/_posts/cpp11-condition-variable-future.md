---
title: 条件变量与 async / future
date: 2026-10-24 10:00:00
updated: 2026-10-24 10:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 多线程
  - 并发
order: 14
description: 线程之间如何等待和通知？本文用条件变量实现一个线程安全的阻塞队列（生产者-消费者模型），再介绍更高层的 std::async、std::future、std::promise 和 std::packaged_task，以及 std::async 最容易踩的坑。
disableNunjucks: true
---

> **本文目标**：理解为什么需要条件变量；会用 `std::condition_variable` 实现生产者-消费者队列，知道为什么 `wait` 一定要带条件；会用 `std::async` 和 `std::future` 在后台计算并取回结果、传递异常；会用 `std::promise` 和 `std::packaged_task`；避开 `std::async` 临时对象导致串行执行的坑。
>
> **前置知识**：上一篇《多线程入门：std::thread 与互斥锁》。
>
> **编译环境**：`g++ -std=c++11 -pthread` / `clang++ -std=c++11 -pthread`。

## 1. 问题：线程之间怎么「等」

上一篇的互斥锁解决了「不能同时访问」的问题。但多线程程序还需要另一种协作：**一个线程等待某个条件成立，另一个线程让条件成立后通知它**。

典型场景是**生产者-消费者**：生产者线程不断产生任务放进队列，消费者线程从队列中取任务处理。队列为空时，消费者该怎么办？

最直接的想法是不停地检查：

```cpp
while (true) {
    std::lock_guard<std::mutex> lock(m);
    if (!queue.empty()) { /* 取出任务 */ break; }
}   // 队列一直为空时，这个循环会把一个 CPU 核心占满
```

这叫**忙等待**（busy waiting），在等待期间白白消耗 100% 的 CPU。加上 `sleep_for` 可以降低消耗，但又会带来延迟：任务到达后，最多要等一个休眠周期才能被处理。

**条件变量**（condition variable）解决这个问题：消费者在条件不满足时**进入休眠，不占用 CPU**；生产者放入任务后**唤醒**它。

## 2. std::condition_variable

条件变量必须和一个互斥锁配合使用。核心操作只有三个：

| 操作 | 作用 |
| --- | --- |
| `cv.wait(lock, 条件)` | 条件不满足时：**释放锁并休眠**；被唤醒后重新加锁并再次检查条件，满足才返回 |
| `cv.notify_one()` | 唤醒一个正在等待的线程 |
| `cv.notify_all()` | 唤醒所有正在等待的线程 |

### 2.1 实现一个阻塞队列

```cpp
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

// 线程安全的阻塞队列
template <typename T>
class BlockingQueue {
public:
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.push(std::move(value));
        }                                  // 先解锁，再通知
        cv_.notify_one();                  // 唤醒一个等待的消费者
    }

    // 队列为空时阻塞等待；队列已关闭且为空时返回 false
    bool pop(T& out)
    {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [this] { return !q_.empty() || closed_; });   // 带条件的等待
        if (q_.empty()) return false;      // 被唤醒是因为关闭了
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            closed_ = true;
        }
        cv_.notify_all();                  // 唤醒所有等待的消费者，让它们退出
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<T> q_;
    bool closed_ = false;
};

int main()
{
    BlockingQueue<int> q;
    std::mutex printMutex;

    auto consumer = [&](int id) {
        int task;
        int handled = 0;
        while (q.pop(task)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));   // 模拟处理
            ++handled;
        }
        std::lock_guard<std::mutex> lock(printMutex);
        std::cout << "消费者 " << id << " 处理了 " << handled << " 个任务\n";
    };

    std::thread c1(consumer, 1), c2(consumer, 2);
    for (int i = 0; i < 20; ++i)
        q.push(i);                         // 生产者：主线程
    q.close();
    c1.join();
    c2.join();
    return 0;
}
```

运行结果（两个消费者的分工每次可能不同）：

```
消费者 2 处理了 10 个任务
消费者 1 处理了 10 个任务
```

用 ThreadSanitizer 运行，没有任何数据竞争报告。逐个分析关键点：

**`pop` 中的 `cv_.wait(lock, 条件)`** 等价于：

```cpp
while (!条件()) {
    cv_.wait(lock);   // 原子地「释放锁 + 休眠」；被唤醒后重新获得锁
}
```

1. 先检查条件。如果队列不空，直接返回，根本不会休眠。
2. 条件不满足时，**释放锁并进入休眠**。释放锁很关键，否则生产者永远拿不到锁，也就无法放入任务。
3. 被唤醒后，**重新获得锁**，再次检查条件，满足才返回。

所以 `wait` 必须用 **`std::unique_lock`**：它在等待期间需要解锁、再加锁，`lock_guard` 做不到。

**`close()` 和 `notify_all`**：消费者用 `while (q.pop(task))` 循环取任务，需要一个办法告诉它们「不会再有任务了」。`closed_` 标志加入了等待条件，`notify_all` 唤醒所有消费者，它们发现队列已空且已关闭，`pop` 返回 `false`，循环结束。

**先解锁再通知**：`push` 中把 `notify_one()` 放在锁的作用域之外。如果在持有锁时通知，被唤醒的线程会立刻尝试加锁，却发现锁还被占着，只能再次等待。先解锁可以避免这次无谓的唤醒。在锁内通知也是正确的，只是效率稍低。

### 2.2 为什么 wait 一定要带条件

条件变量有两个「坑」，带条件的 `wait` 同时解决了它们：

1. **虚假唤醒**（spurious wakeup）：标准允许 `wait` 在没有任何人通知的情况下返回。这是操作系统实现上的原因，在 Linux、Windows 上都可能发生。如果醒来后不检查条件，就会从空队列里取元素。
2. **丢失的唤醒**（lost wakeup）：如果生产者在消费者调用 `wait` **之前**就已经 `notify` 了，这次通知就丢了，而不带条件的 `wait` 会一直等下去。带条件的版本会**先检查条件**，队列里已经有数据就根本不会休眠。

**规则：永远使用带谓词（条件）的 `wait`，并且条件中用到的变量必须在同一个互斥锁的保护下修改。** 即使条件只是一个 `bool` 标志，修改它时也要加锁，否则仍可能出现丢失唤醒。

## 3. std::future 与 std::async

条件变量功能强大，但写起来容易出错。很多时候我们只是想：**在后台算一个结果，算完后拿回来**。C++11 为此提供了更高层的工具。

**`std::future<T>`** 代表「一个将来才会有的 T 类型的值」。它的 `get()` 成员函数会**等待**结果就绪，然后取出结果。**`std::async`** 启动一个异步任务，返回与之关联的 `future`。

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

long long sumRange(const std::vector<int>& v, std::size_t begin, std::size_t end)
{
    return std::accumulate(v.begin() + begin, v.begin() + end, 0LL);
}

int parse(const std::string& s)
{
    if (s.empty()) throw std::invalid_argument("空字符串");
    return std::stoi(s);
}

int main()
{
    std::vector<int> v(1000000, 1);

    // 1. std::async：在后台计算，future 用来取结果
    std::future<long long> first = std::async(std::launch::async, sumRange, std::cref(v), 0, v.size() / 2);
    long long second = sumRange(v, v.size() / 2, v.size());      // 主线程同时算另一半
    std::cout << "总和 = " << first.get() + second << '\n';      // get() 等待并取出结果

    // 2. 异常会通过 future 传回调用者
    std::future<int> bad = std::async(std::launch::async, parse, "");
    try {
        int value = bad.get();
        std::cout << value << '\n';
    } catch (const std::exception& e) {
        std::cout << "在主线程捕获到子线程的异常: " << e.what() << '\n';
    }

    // 3. promise：手动设置 future 的值
    std::promise<std::string> p;
    std::future<std::string> f = p.get_future();
    std::thread worker([&p] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        p.set_value("来自 worker 的消息");
    });
    std::cout << "等待消息……\n";
    std::cout << f.get() << '\n';
    worker.join();

    // 4. wait_for：带超时的等待
    auto slow = std::async(std::launch::async, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return 42;
    });
    if (slow.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout)
        std::cout << "100ms 内没有完成，先做点别的\n";
    std::cout << "最终结果 " << slow.get() << '\n';
    return 0;
}
```

运行结果：

```
总和 = 1000000
在主线程捕获到子线程的异常: 空字符串
等待消息……
来自 worker 的消息
100ms 内没有完成，先做点别的
最终结果 42
```

和直接使用 `std::thread` 相比，`std::async` + `future` 有三大优势：

1. **能拿到返回值**。`std::thread` 执行的函数的返回值会被丢弃，想拿结果只能自己用共享变量加锁传回来。
2. **异常会被传递**。异步任务中抛出的异常被保存在 `future` 里，调用 `get()` 时在调用者的线程中**重新抛出**。而 `std::thread` 中未捕获的异常会直接终止整个程序。
3. **不用手动 `join`**。

注意：**`get()` 只能调用一次**，调用后 `future` 就失效了（`valid()` 返回 `false`）。需要多个线程都能读取结果时，用 `std::shared_future`（通过 `f.share()` 获得）。

### 3.1 启动策略

`std::async` 的第一个参数是**启动策略**：

```cpp
#include <future>
#include <iostream>
#include <sstream>
#include <thread>

std::string tid()
{
    std::ostringstream os;
    os << std::this_thread::get_id();
    return os.str();
}

int main()
{
    std::cout << "主线程      id = " << tid() << '\n';
    auto a = std::async(std::launch::async, [] { return "async    在线程 " + tid() + " 中执行"; });
    auto d = std::async(std::launch::deferred, [] { return "deferred 在线程 " + tid() + " 中执行"; });
    std::cout << a.get() << '\n';
    std::cout << d.get() << "（get() 时才在调用者线程中执行）\n";
    return 0;
}
```

运行结果：

```
主线程      id = 0x1f0067ec0
async    在线程 0x16be23000 中执行
deferred 在线程 0x1f0067ec0 中执行（get() 时才在调用者线程中执行）
```

| 策略 | 行为 |
| --- | --- |
| `std::launch::async` | **立即在新线程中**执行 |
| `std::launch::deferred` | **延迟执行**：调用 `get()` 或 `wait()` 时，才在**调用者的线程**中同步执行；如果从未调用，就永远不执行 |
| 不指定（默认） | 两者之一，由实现决定 |

默认策略的不确定性会带来问题：如果实现选择了 `deferred`，你以为在后台并行执行的任务，实际上直到 `get()` 时才在当前线程执行；`wait_for` 也会一直返回 `deferred` 而不是 `ready`，基于超时的循环可能永远不会结束。**所以需要真正的并行时，明确写出 `std::launch::async`。**

### 3.2 std::promise

`std::async` 中，任务函数的返回值自动成为 `future` 的结果。`std::promise` 则让你**手动**设置结果：一个线程持有 `promise`，在合适的时机调用 `set_value()`（或 `set_exception()`）；另一个线程持有对应的 `future`，调用 `get()` 等待。它就像一个只能使用一次的「单向通道」。

上面例子中的第 3 部分，`worker` 线程通过 `promise` 把字符串交给了主线程。`promise` 适合结果不是某个函数的返回值，而是在执行过程中某个时刻产生的情况，比如网络回调、事件处理。

### 3.3 std::packaged_task

`std::packaged_task` 把一个可调用对象**包装**成一个任务，任务执行后，返回值自动存入关联的 `future`：

```cpp
#include <future>
#include <iostream>
#include <thread>
#include <utility>

int multiply(int a, int b) { return a * b; }

int main()
{
    std::packaged_task<int(int, int)> task(multiply);   // 把函数包装成任务
    std::future<int> result = task.get_future();        // 先拿到 future
    std::thread t(std::move(task), 6, 7);               // 在另一个线程中执行任务
    std::cout << "6 * 7 = " << result.get() << '\n';
    t.join();
    return 0;
}
```

运行结果：

```
6 * 7 = 42
```

它和 `std::async` 的区别在于：**任务何时、在哪个线程执行，由你决定**。这正是线程池需要的：把用户提交的函数包装成 `packaged_task`，把 `future` 返回给用户，把任务放进队列，由线程池中的工作线程取出执行。

三者的关系：

| 工具 | 谁产生结果 | 适用场景 |
| --- | --- | --- |
| `std::async` | 自动：新线程执行函数，返回值即结果 | 简单的后台计算 |
| `std::packaged_task` | 自动：任务被执行时，返回值即结果 | 自己控制任务在哪里执行，如线程池 |
| `std::promise` | 手动调用 `set_value` | 结果在某个时刻产生，而不是某个函数的返回值 |

## 4. 常见错误

### 4.1 std::async 的临时 future 会阻塞

这是 `std::async` 最隐蔽的坑：

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

void task() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }

long long elapsedMs(std::chrono::steady_clock::time_point start)
{
    auto d = std::chrono::steady_clock::now() - start;
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

int main()
{
    auto start = std::chrono::steady_clock::now();
    std::async(std::launch::async, task);     // 返回的临时 future 在这一行结束时析构，并等待任务完成
    std::async(std::launch::async, task);
    std::cout << "不保存 future: " << (elapsedMs(start) + 50) / 100 * 100 << " ms（串行了！）\n";

    start = std::chrono::steady_clock::now();
    auto f1 = std::async(std::launch::async, task);
    auto f2 = std::async(std::launch::async, task);
    f1.get();
    f2.get();
    std::cout << "保存 future:   " << (elapsedMs(start) + 50) / 100 * 100 << " ms（并行）\n";
    return 0;
}
```

运行结果：

```
不保存 future: 400 ms（串行了！）
保存 future:   200 ms（并行）
```

**由 `std::async` 返回的 `future`，析构时会等待任务完成。** 不保存返回值时，临时的 `future` 在语句结束时就被析构，于是每一行都要等任务执行完才能继续，两个任务变成了串行执行。

好消息是，libc++ 和 libstdc++ 都给 `std::async` 加了 `[[nodiscard]]` 属性，编译时会警告：

```
warning: ignoring return value of function declared with 'nodiscard' attribute [-Wunused-result]
```

**规则：一定要保存 `std::async` 的返回值。**

### 4.2 条件变量相关

1. **用不带条件的 `wait`**：会受到虚假唤醒和丢失唤醒的影响，见第 2.2 节。
2. **修改条件时不加锁**：哪怕条件变量本身是线程安全的，条件中用到的变量也必须在同一把锁下修改。
3. **用 `notify_one` 通知「关闭」**：只会唤醒一个消费者，其他消费者永远在等。需要所有等待者都知道的事件，用 `notify_all`。

### 4.3 future 相关

1. **多次调用 `get()`**：第二次调用是未定义行为（实际通常抛出 `std::future_error`）。
2. **`promise` 销毁前没有设置值**：等待中的 `get()` 会抛出 `std::future_error`，错误码是 `broken_promise`。

## 5. 进阶

### 5.1 条件变量的等待超时

`wait_for` 和 `wait_until` 可以设置超时，同样有带条件的版本：

```cpp
std::unique_lock<std::mutex> lock(m);
if (cv.wait_for(lock, std::chrono::seconds(1), [&] { return ready; })) {
    // 条件满足
} else {
    // 超时，条件仍不满足
}
```

带条件的 `wait_for` 返回 `bool`，表示返回时条件是否满足，不会受到虚假唤醒的干扰。

### 5.2 一个最简单的线程池

把本文的内容组合起来，就是线程池的骨架：

```cpp
class ThreadPool {
public:
    explicit ThreadPool(std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
            workers_.emplace_back([this] {
                std::function<void()> job;
                while (tasks_.pop(job)) job();          // 阻塞队列中取任务执行
            });
    }
    ~ThreadPool()
    {
        tasks_.close();                                 // 通知所有工作线程退出
        for (auto& t : workers_) t.join();
    }
    template <typename F>
    std::future<typename std::result_of<F()>::type> submit(F f)
    {
        using R = typename std::result_of<F()>::type;
        auto task = std::make_shared<std::packaged_task<R()>>(std::move(f));
        std::future<R> result = task->get_future();
        tasks_.push([task] { (*task)(); });
        return result;
    }
private:
    BlockingQueue<std::function<void()>> tasks_;        // 第 2.1 节的阻塞队列
    std::vector<std::thread> workers_;
};
```

- 工作线程从阻塞队列中取任务，没有任务时在条件变量上休眠。
- `submit` 把用户的函数包装成 `packaged_task`，把 `future` 返回给用户。
- `packaged_task` 只能移动，而 `std::function` 要求可拷贝，所以用 `shared_ptr` 包一层再放进 lambda。

用法：

```cpp
ThreadPool pool(4);
auto f = pool.submit([] { return 6 * 7; });
std::cout << f.get() << '\n';   // 42
```

> `std::result_of` 在 C++17 中被弃用，改用 `std::invoke_result`。生产环境的线程池还需要考虑任务带参数、异常、优雅关闭等问题，这里只展示核心思路。

## 6. 小结

- **条件变量**让线程在条件不满足时休眠、条件满足时被唤醒，避免忙等待；必须配合 `std::unique_lock` 使用
- **永远使用带条件的 `wait`**，以应对虚假唤醒和丢失唤醒；条件中的变量必须在同一把锁下修改
- **`std::async` + `std::future`**：后台计算并取回结果，异常会传回调用者；需要并行时明确指定 `std::launch::async`
- **一定要保存 `std::async` 返回的 `future`**，否则临时对象析构时会阻塞，导致串行执行
- **`std::promise`** 手动设置结果，**`std::packaged_task`** 把函数包装成可以交给任意线程执行的任务
- 阻塞队列 + `packaged_task` 就是线程池的核心

## 7. 练习

1. 给 `BlockingQueue` 加一个容量上限：队列满时，`push` 阻塞等待，直到有消费者取走元素。（提示：再加一个条件变量。）
2. 用 `std::async` 把一个大数组分成 4 段并行求和，与上一篇用 `std::thread` 的写法比较，哪个更简洁？
3. 下面的代码预期并行执行 3 个下载任务，实际却是一个接一个执行的，为什么？怎么改？

   ```cpp
   for (const auto& url : urls)
       std::async(std::launch::async, download, url);
   ```

<details>
<summary>点击查看答案</summary>

1. 增加 `std::condition_variable notFull_;` 和容量 `capacity_`：

   ```cpp
   void push(T value)
   {
       {
           std::unique_lock<std::mutex> lock(m_);
           notFull_.wait(lock, [this] { return q_.size() < capacity_ || closed_; });
           q_.push(std::move(value));
       }
       cv_.notify_one();
   }
   // pop 中取出元素后，增加一行 notFull_.notify_one();
   // close 中增加 notFull_.notify_all();
   ```

2. 参考代码，不需要手动 `join`，也不需要准备存放结果的数组：

   ```cpp
   std::vector<std::future<long long>> parts;
   std::size_t chunk = v.size() / 4;
   for (int i = 0; i < 4; ++i) {
       std::size_t b = i * chunk, e = (i == 3) ? v.size() : b + chunk;
       parts.push_back(std::async(std::launch::async, sumRange, std::cref(v), b, e));
   }
   long long total = 0;
   for (auto& f : parts) total += f.get();
   ```

3. `std::async` 返回的临时 `future` 在每次循环结束时被析构，析构会等待任务完成，所以三个下载是串行的。把 `future` 保存到容器中：

   ```cpp
   std::vector<std::future<void>> jobs;
   for (const auto& url : urls)
       jobs.push_back(std::async(std::launch::async, download, url));
   for (auto& j : jobs) j.get();
   ```

</details>
