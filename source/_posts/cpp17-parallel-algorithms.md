---
title: 并行算法：一个参数用上多核
date: 2026-10-26 10:00:00
updated: 2026-10-26 10:00:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 多线程
  - 标准库
order: 9
description: C++17 给大部分标准算法增加了执行策略参数，加上 std::execution::par 就能并行执行。本文用 1000 万个元素实测排序、变换、归约的加速比，讲清楚什么时候并行值得、浮点结果为什么会变、为什么并行算法中不能有数据竞争，以及异常为什么会直接终止程序。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `std::execution::seq`、`par`、`par_unseq` 调用并行版本的标准算法；会用 `std::reduce`、`std::transform_reduce` 做并行归约；通过实测数据理解什么样的任务值得并行；避开数据竞争、浮点结果变化、异常终止等陷阱；知道各编译器的支持情况。
>
> **前置知识**：C++11 章节的《多线程入门》《原子操作与内存序》。
>
> **编译环境**：需要包含头文件 `<execution>`。各编译器的支持情况见第 6 节。本文的实验在 Apple M 系列芯片（12 线程）上，用 Clang 21 + libc++ 编译：`clang++ -std=c++17 -O2 -fexperimental-library`。

## 1. 一个参数实现并行

C++11 章节中，想让计算用上多个 CPU 核心，需要自己创建线程、划分数据、汇总结果。C++17 给标准库中的大部分算法（约 70 个）增加了一个重载：**第一个参数是执行策略**。

```cpp
#include <algorithm>
#include <execution>

std::sort(v.begin(), v.end());                          // 原来的串行版本
std::sort(std::execution::par, v.begin(), v.end());     // 并行版本
```

只多了一个参数，标准库就会自动把工作分配到多个线程上执行。

### 1.1 执行策略

| 策略 | 含义 |
| --- | --- |
| `std::execution::seq` | 顺序执行，和不传策略基本相同 |
| `std::execution::par` | **并行**：可以在多个线程上执行 |
| `std::execution::par_unseq` | **并行 + 向量化**：可以在多个线程上执行，同一个线程中还可以用 SIMD 指令交错处理多个元素 |
| `std::execution::unseq` | 只向量化，不使用多线程（C++20 新增） |

执行策略只是**允许**标准库并行执行，并不保证一定并行。具体用几个线程、如何分块，由标准库的实现决定。

## 2. 实测：哪些任务能加速

用 1000 万个随机 `double` 做四组实验：

```cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <execution>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

template <typename F>
double timeMs(F f)
{
    auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main()
{
    const std::size_t N = 10'000'000;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::vector<double> data(N);
    for (auto& x : data) x = dist(rng);
    std::cout << "元素个数: " << N << ", CPU 线程数: " << std::thread::hardware_concurrency() << "\n\n";

    // 1. 排序
    auto a = data, b = data;
    double tSeq = timeMs([&] { std::sort(a.begin(), a.end()); });
    double tPar = timeMs([&] { std::sort(std::execution::par, b.begin(), b.end()); });
    std::cout << "sort            串行 " << static_cast<int>(tSeq) << " ms, 并行 " << static_cast<int>(tPar)
              << " ms, 加速 " << tSeq / tPar << " 倍, 结果相同: " << (a == b) << '\n';

    // 2. 对每个元素做较重的计算
    std::vector<double> out1(N), out2(N);
    auto heavy = [](double x) { return std::sin(x) * std::cos(x) + std::sqrt(x) * std::log1p(x); };
    tSeq = timeMs([&] { std::transform(data.begin(), data.end(), out1.begin(), heavy); });
    tPar = timeMs([&] { std::transform(std::execution::par_unseq, data.begin(), data.end(), out2.begin(), heavy); });
    std::cout << "transform       串行 " << static_cast<int>(tSeq) << " ms, 并行 " << static_cast<int>(tPar)
              << " ms, 加速 " << tSeq / tPar << " 倍, 结果相同: " << (out1 == out2) << '\n';

    // 3. 求平方和
    double s1 = 0, s2 = 0;
    tSeq = timeMs([&] { s1 = std::transform_reduce(std::execution::seq, data.begin(), data.end(), 0.0, std::plus<>(), [](double x) { return x * x; }); });
    tPar = timeMs([&] { s2 = std::transform_reduce(std::execution::par, data.begin(), data.end(), 0.0, std::plus<>(), [](double x) { return x * x; }); });
    std::cout << "transform_reduce串行 " << tSeq << " ms, 并行 " << tPar << " ms, 结果 " << s1 << " / " << s2 << '\n';

    // 4. 数据量很小时
    std::vector<int> small(1000, 1);
    double tSmallSeq = timeMs([&] { for (int i = 0; i < 1000; ++i) std::sort(small.begin(), small.end()); });
    double tSmallPar = timeMs([&] { for (int i = 0; i < 1000; ++i) std::sort(std::execution::par, small.begin(), small.end()); });
    std::cout << "1000 个元素排序 1000 次: 串行 " << tSmallSeq << " ms, 并行 " << tSmallPar << " ms\n";
    return 0;
}
```

运行结果（具体数值因机器而异，两次运行结果相近）：

```
元素个数: 10000000, CPU 线程数: 12

sort            串行 180 ms, 并行 116 ms, 加速 1.55538 倍, 结果相同: 1
transform       串行 69 ms, 并行 9 ms, 加速 7.16816 倍, 结果相同: 1
transform_reduce串行 4.57225 ms, 并行 1.62183 ms, 结果 3.33434e+06 / 3.33434e+06
1000 个元素排序 1000 次: 串行 0.465041 ms, 并行 2.50379 ms
```

把结果整理一下：

| 任务 | 加速比 | 原因 |
| --- | --- | --- |
| `transform`，每个元素做三角函数、开方、对数 | **约 7 倍** | 计算量大，元素之间互不依赖，最适合并行 |
| `transform_reduce`，求平方和 | 约 3 倍 | 每个元素只做一次乘法，瓶颈在**内存带宽**，核心再多也读不了那么快 |
| `sort` | 约 1.5 倍 | 排序的各个部分需要合并，并行化本身有额外开销；不同标准库的实现差异很大 |
| 1000 个元素排序 | **变慢了 3～5 倍** | 数据太少，创建任务、分配线程的开销远大于计算本身 |

结论：**每个元素的计算量越大、数据量越大，并行的收益越高**；简单操作受内存带宽限制，小数据量反而会变慢。是否使用并行版本，要**实际测量**之后再决定。

## 3. 并行归约：reduce 与 transform_reduce

求和用的 `std::accumulate` **没有**并行版本，因为它规定了严格的从左到右的求和顺序：`((init + a1) + a2) + a3 ...`，后一步依赖前一步的结果，无法并行。

C++17 新增了 **`std::reduce`**，它**不保证计算顺序**，可以把数据分成几段分别求和，再把各段的结果相加，因此可以并行：

```cpp
double sum = std::reduce(std::execution::par, v.begin(), v.end(), 0.0);
```

**`std::transform_reduce`** 把「对每个元素做变换」和「归约」合并成一步，避免了中间结果的数组：

```cpp
// 平方和：先对每个元素求平方（变换），再把结果相加（归约）
double sumSq = std::transform_reduce(std::execution::par, v.begin(), v.end(),
                                     0.0, std::plus<>(), [](double x) { return x * x; });

// 两个数组的点积：对应元素相乘，再相加
double dot = std::transform_reduce(std::execution::par, a.begin(), a.end(), b.begin(), 0.0);
```

由于顺序不确定，**归约操作必须满足结合律和交换律**，比如加法、乘法、取最大值。减法不满足，用 `reduce` 做减法会得到不确定的结果。

## 4. 三个陷阱

```cpp
#include <algorithm>
#include <atomic>
#include <execution>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

int main(int argc, char**)
{
    std::vector<double> data(10'000'000);
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (auto& x : data) x = dist(rng);

    // 1. 浮点数求和：并行的加法顺序不同，结果可能有微小差异
    double s1 = std::reduce(std::execution::seq, data.begin(), data.end(), 0.0);
    double s2 = std::reduce(std::execution::par, data.begin(), data.end(), 0.0);
    std::cout << std::setprecision(17) << "串行 " << s1 << "\n并行 " << s2 << "\n差值 " << s1 - s2 << '\n';

    // 2. 数据竞争：并行的 for_each 中修改共享变量
    long long unsafeCount = 0;
    std::atomic<long long> safeCount{0};
    std::for_each(std::execution::par, data.begin(), data.end(), [&](double x) {
        if (x > 0.5) {
            ++unsafeCount;                                   // 数据竞争！
            safeCount.fetch_add(1, std::memory_order_relaxed);
        }
    });
    long long expected = std::count_if(data.begin(), data.end(), [](double x) { return x > 0.5; });
    std::cout << "期望 " << expected << ", 原子计数 " << safeCount << ", 普通变量 " << unsafeCount << '\n';

    // 3. 并行算法中抛出异常
    if (argc > 1) {
        try {
            std::for_each(std::execution::par, data.begin(), data.end(), [](double x) {
                if (x > 0.9999999) throw std::runtime_error("出错了");
            });
        } catch (const std::exception& e) {
            std::cout << "捕获到异常: " << e.what() << '\n';
        }
    }
    return 0;
}
```

运行结果：

```
串行 5000894.564541989
并行 5000894.56454184
差值 1.4901161193847656e-07
期望 5001408, 原子计数 5001408, 普通变量 534671
```

带参数运行（触发第 3 部分）：

```
libc++abi: terminating due to uncaught exception of type std::runtime_error: 出错了
```

### 4.1 浮点数的结果会变

串行和并行的求和结果相差了约 1.5 × 10⁻⁷。原因是**浮点数加法不满足结合律**：`(a + b) + c` 和 `a + (b + c)` 的舍入误差不同，并行求和改变了相加的顺序。这不是 bug，但如果程序依赖「每次运行结果完全一致」（比如单元测试中比较浮点数是否相等、需要可复现的科学计算），就要注意。整数运算没有这个问题。

### 4.2 不能有数据竞争

`std::for_each` 的并行版本中，lambda 会被**多个线程同时调用**。用普通变量计数，期望 500 万，实际只有 53 万多，**丢失了约 90%**，而且每次运行的结果都不一样。这就是 C++11 章节《多线程入门》中讲过的数据竞争，只是这次线程是标准库创建的，更容易被忽略。

解决办法：

- 用 `std::atomic` 计数（如上），但大量线程争抢同一个原子变量会很慢；
- **更好的办法是根本不共享状态**：计数本来就是归约，直接用 `std::count_if(std::execution::par, ...)`，或者 `transform_reduce`。

**规则：传给并行算法的函数，不能修改共享的数据，除非做了同步。** 对于 `par_unseq`，要求更严格：函数中**不能加锁、不能分配内存**，因为同一个线程可能交错执行多个元素的处理，在一个元素中加锁、在另一个元素中再加同一把锁，就会死锁。

### 4.3 异常会直接终止程序

串行算法中抛出的异常会正常传播给调用者。但标准规定：**带执行策略的算法中，如果元素处理函数抛出异常，程序直接调用 `std::terminate()` 终止**，外面的 `try` 无法捕获。上面的程序虽然把并行的 `for_each` 包在了 `try` 中，仍然被终止了。

这是因为多个线程可能同时抛出多个异常，没有一种合理的方式把它们都传给调用者。所以传给并行算法的函数**不应该抛出异常**；可能出错时，把错误记录下来（比如写进结果中），算法结束后再处理。

## 5. 常见错误

1. **对小数据使用并行**：实测 1000 个元素时并行版本慢了数倍。数据量小、每个元素计算量小时，用串行版本。
2. **用 `std::accumulate` 期望并行**：它没有执行策略的重载，用 `std::reduce`。
3. **`reduce` 中使用不满足结合律的操作**：如减法、字符串拼接的顺序敏感操作，结果不确定。
4. **以为加了 `par` 就一定更快**：执行策略只是许可，实际效果取决于标准库实现、数据量和机器，必须测量。
5. **在 `par_unseq` 中使用锁、`new`、`push_back` 等**：可能死锁或产生未定义行为。

## 6. 进阶：编译器支持情况

并行算法是 C++17 中各编译器支持得最晚、差异最大的特性：

| 编译器 / 标准库 | 支持情况 |
| --- | --- |
| **MSVC** | VS2017 15.7 起直接支持，使用 Windows 线程池 |
| **GCC（libstdc++）** | GCC 9 起支持，但实现依赖 **Intel TBB**：需要安装 TBB 并链接 `-ltbb`，否则所有策略都退化为串行 |
| **Clang（libc++）** | 本文使用的 Apple Clang 中，需要加 `-fexperimental-library` 才能使用 `std::execution::par`，否则编译报错 `no member named 'par' in namespace 'std::execution'`；macOS 上的后端是 libdispatch（GCD）。编译时还会给出一个「循环未能向量化」的优化警告，不影响结果。实现也还不完整：本文实测 `sort`、`stable_sort`、`for_each`、`transform`、`reduce`、`transform_reduce`、`count_if`、`find_if`、`any_of`、`fill` 都有并行版本，但 `copy_if` 没有 |

不同实现的性能差异也很大，比如本文实测的排序加速比只有 1.5 倍，换成 GCC + TBB 结果可能完全不同。这也是为什么第 2 节一再强调**要实际测量**。

对于更复杂的并行需求（任务之间有依赖、需要细粒度控制），标准并行算法就不够用了，可以使用 OpenMP、Intel TBB 等专门的并行库，或者 C++11 章节中介绍的线程池。

## 7. 小结

- 给标准算法加上第一个参数 `std::execution::par`（或 `par_unseq`），就能并行执行
- 实测：计算密集的 `transform` 加速约 7 倍，受内存带宽限制的归约约 3 倍，排序约 1.5 倍，小数据量反而变慢；**一定要测量**
- `std::accumulate` 不能并行，用 `std::reduce` / `std::transform_reduce`；归约操作必须满足结合律和交换律
- 并行求和会改变浮点数的计算顺序，结果可能有微小差异
- 传给并行算法的函数**不能有数据竞争**，`par_unseq` 中不能加锁、不能分配内存
- 并行算法中抛出的异常会直接 `std::terminate`
- GCC 需要 TBB，Apple Clang 需要 `-fexperimental-library`

## 8. 练习

1. 用 `std::transform_reduce` 和执行策略计算两个 `vector<double>` 的欧几里得距离 `sqrt(Σ(aᵢ - bᵢ)²)`。
2. 下面的代码想并行地把满足条件的元素收集到 `result` 中，有什么问题？应该怎么写？

   ```cpp
   std::vector<int> result;
   std::for_each(std::execution::par, v.begin(), v.end(), [&](int x) {
       if (x % 3 == 0) result.push_back(x);
   });
   ```

3. 一个程序要对 100 万个字符串分别计算哈希值（每个哈希大约需要 1 微秒），用并行版本的 `std::transform` 预计能加速吗？对 100 个整数求和呢？

<details>
<summary>点击查看答案</summary>

1. 用二元版本的 `transform_reduce`：第一个函数是归约（加法），第二个函数是对两个数组对应元素的变换：

   ```cpp
   double distance(const std::vector<double>& a, const std::vector<double>& b)
   {
       double sumSq = std::transform_reduce(std::execution::par, a.begin(), a.end(), b.begin(), 0.0,
                                            std::plus<>(),
                                            [](double x, double y) { return (x - y) * (x - y); });
       return std::sqrt(sumSq);
   }
   ```

2. 多个线程同时 `push_back` 同一个 `vector` 是数据竞争，可能导致崩溃或数据丢失。

   标准为 `std::copy_if` 提供了执行策略的重载，在 MSVC、GCC + TBB 上可以直接写：

   ```cpp
   std::vector<int> result(v.size());
   auto end = std::copy_if(std::execution::par, v.begin(), v.end(), result.begin(),
                           [](int x) { return x % 3 == 0; });
   result.erase(end, result.end());
   ```

   但本文使用的 libc++ 还没有实现并行版本的 `copy_if`，这段代码会编译失败（`no matching function for call to 'copy_if'`）。一个可移植的做法是分两步：先**并行**地判断每个元素（判断条件计算量大时，这一步才是耗时的部分），再**串行**地按顺序收集：

   ```cpp
   std::vector<char> keep(v.size());
   std::transform(std::execution::par, v.begin(), v.end(), keep.begin(),
                  [](int x) { return x % 3 == 0; });
   std::vector<int> result;
   for (std::size_t i = 0; i < v.size(); ++i)
       if (keep[i]) result.push_back(v[i]);
   ```

   两种写法都保持元素原来的相对顺序。像本题这样判断条件非常简单时，直接用串行的 `std::copy_if` 就足够了。

3. 哈希计算：每个元素的计算量较大（1 微秒），元素之间互不依赖，数据量大，非常适合并行，预计有明显加速。100 个整数求和：总共只需要几十纳秒，并行的调度开销远大于计算本身，会变慢，应该用串行版本。

</details>
