---
title: C++20 常用标准库：span、format、jthread 等
date: 2026-10-22 20:20:00
updated: 2026-10-22 20:20:00
categories:
  - C/C++
  - C++20
tags:
  - C++20
  - 标准库
order: 7
description: C++20 新增了一批日常非常好用的标准库组件：std::span 统一表示连续内存，std::format 带来类型安全又简洁的格式化，std::jthread 自动 join 并支持协作式取消，还有 latch、semaphore、source_location、starts_with 等。本文最后回顾 C++11 到 C++20 的学习路线。
disableNunjucks: true
---

> **本文目标**：会用 `std::span` 作为数组参数；会用 `std::format` 格式化字符串，并让自定义类型支持它；会用 `std::jthread` 和 `std::stop_token` 写可以安全停止的线程；认识 `latch`、`counting_semaphore`；了解 `source_location`、`contains`、`starts_with`、`<bit>` 等小工具。
>
> **编译环境**：`g++ -std=c++20` / `clang++ -std=c++20`。`std::format` 需要 GCC 13+ / Clang 17+（libc++）/ VS2019 16.10+；`std::jthread` 需要 GCC 10+ / Clang 18+（libc++）。

## 1. std::span：连续内存的视图

### 1.1 问题：数组参数

一个函数要处理一段连续的整数，调用者手里可能是 C 数组、`std::array`、`std::vector`，C++17 之前常见的写法：

```cpp
double average(const int* data, std::size_t size);           // C 风格：指针和长度分开，容易传错
double average(const std::vector<int>& v);                   // 只能接受 vector
template <typename C> double average(const C& c);            // 模板：所有代码都得放在头文件里
```

C++20 的 **`std::span<T>`** 是一段**连续内存的视图**：一个指针加一个长度，不拥有数据。它之于数组，就像 `std::string_view` 之于字符串。

### 1.2 用法

```cpp
#include <array>
#include <iostream>
#include <span>
#include <vector>

// 一个函数同时接受 C 数组、std::array、std::vector，而且知道长度
double average(std::span<const int> data)
{
    if (data.empty()) return 0;
    long long sum = 0;
    for (int x : data) sum += x;
    return static_cast<double>(sum) / data.size();
}

void doubleAll(std::span<int> data)       // 非 const 的 span 可以修改元素
{
    for (int& x : data) x *= 2;
}

int main()
{
    int raw[] = {1, 2, 3, 4};
    std::array<int, 3> arr{10, 20, 30};
    std::vector<int> vec{5, 6, 7, 8, 9};

    std::cout << average(raw) << ' ' << average(arr) << ' ' << average(vec) << '\n';

    std::span<int> s = vec;
    std::cout << "first(2): " << s.first(2)[1] << ", last(2): " << s.last(2)[0]
              << ", subspan(1, 3).size(): " << s.subspan(1, 3).size() << '\n';

    doubleAll(std::span(vec).subspan(1, 2));   // 只修改 vec[1] 和 vec[2]
    for (int x : vec) std::cout << x << ' ';
    std::cout << '\n';

    std::cout << "sizeof(span<int>) = " << sizeof(std::span<int>)
              << ", sizeof(span<int, 3>) = " << sizeof(std::span<int, 3>) << '\n';   // 固定长度的 span 只存指针
    return 0;
}
```

运行结果：

```
2.5 20 7
first(2): 6, last(2): 8, subspan(1, 3).size(): 3
5 12 14 8 9 
sizeof(span<int>) = 16, sizeof(span<int, 3>) = 8
```

- **C 数组、`std::array`、`std::vector` 都能隐式转换成 `span`**，而且长度自动带过去，不会传错。
- **`span<const int>`** 是只读视图，**`span<int>`** 可以修改元素，修改直接作用于原容器。
- `first(n)`、`last(n)`、`subspan(偏移, 长度)` 都返回新的 `span`，O(1)，不拷贝数据。上例中 `doubleAll` 只修改了 `vec[1]` 和 `vec[2]`。
- `span<int>` 是 16 字节（指针 + 长度），**按值传递**即可。`span<int, 3>` 是**固定长度**的 span，长度是类型的一部分，只需要存一个指针。

和 `string_view` 一样，`span` 不拥有数据，**它指向的容器必须比它活得久**；容器扩容（`push_back`）后，原来的 span 就失效了。

## 2. std::format：类型安全的格式化

### 2.1 printf 和 iostream 的问题

- **`printf`**：格式简洁，但**不是类型安全的**：`printf("%d", 3.14)` 能编译，输出是错的；也不支持 `std::string` 和自定义类型。
- **`iostream`**：类型安全，但格式化非常啰嗦：输出保留两位小数、宽度为 10 的右对齐数字，要写 `std::cout << std::setw(10) << std::fixed << std::setprecision(2) << x;`，而且 `fixed` 等设置会一直保留，影响后面的输出。

C++20 的 **`std::format`** 兼具两者的优点：语法类似 Python 的 `str.format`，又是类型安全的。

### 2.2 用法

```cpp
#include <format>
#include <iostream>
#include <string>
#include <vector>

struct Point { int x, y; };

// 让自定义类型支持 std::format
template <>
struct std::formatter<Point> : std::formatter<std::string> {
    auto format(const Point& p, std::format_context& ctx) const
    {
        return std::formatter<std::string>::format(std::format("({}, {})", p.x, p.y), ctx);
    }
};

int main()
{
    std::string name = "Alice";
    int age = 20;
    double score = 92.456;

    std::cout << std::format("{} 今年 {} 岁，成绩 {:.1f}\n", name, age, score);
    std::cout << std::format("{1} 和 {0}，{1} 出现两次\n", "甲", "乙");          // 按位置引用参数
    std::cout << std::format("[{:>8}] [{:<8}] [{:^8}] [{:*^8}]\n", "右", "左", "中", "填");
    std::cout << std::format("{:08.3f} | {:+d} | {:#x} | {:#b} | {:e}\n", 3.14159, 42, 255, 5, 12345.678);
    std::cout << std::format("{:>10} | {:>10}\n", "商品", "价格");
    for (auto [item, price] : std::vector<std::pair<std::string, double>>{{"苹果", 5.5}, {"西瓜", 25}})
        std::cout << std::format("{:>10} | {:>10.2f}\n", item, price);

    std::cout << std::format("点的坐标是 {}\n", Point{3, 4});
    std::string s = std::format("{}-{}", 2026, "10");    // 结果是 std::string，不只用于输出
    std::cout << s << ", 长度 " << s.size() << '\n';
    return 0;
}
```

运行结果：

```
Alice 今年 20 岁，成绩 92.5
乙 和 甲，乙 出现两次
[      右] [左      ] [   中   ] [***填***]
0003.142 | +42 | 0xff | 0b101 | 1.234568e+04
      商品 |       价格
      苹果 |       5.50
      西瓜 |      25.00
点的坐标是 (3, 4)
2026-10, 长度 7
```

格式说明的语法是 `{参数序号:填充 对齐 符号 # 0 宽度 .精度 类型}`，各部分都可以省略：

| 写法 | 含义 | 例子 |
| --- | --- | --- |
| `{}` | 按顺序取下一个参数，默认格式 | `format("{} {}", 1, 2)` → `1 2` |
| `{1}` | 第 1 个参数（从 0 开始） | |
| `{:>8}` `{:<8}` `{:^8}` | 宽度 8，右 / 左 / 居中对齐 | |
| `{:*^8}` | 用 `*` 填充 | `***填***` |
| `{:.2f}` | 定点小数，保留 2 位 | |
| `{:08.3f}` | 宽度 8，不足补 0 | `0003.142` |
| `{:+d}` | 总是显示正负号 | `+42` |
| `{:#x}` `{:#b}` | 十六进制 / 二进制，带前缀 | `0xff`、`0b101` |
| `{:e}` | 科学计数法 | |

两个值得注意的细节：

- **中文对齐**：`{:>10}` 计算宽度时，`std::format` 会把中文等东亚宽字符算作**两列**，所以上面的表格中，中文和数字能正确对齐。用 `printf` 的 `%10s` 时，宽度按字节计算，一个汉字在 UTF-8 中占 3 个字节，表格就对不齐了。实际对比：

  ```
  printf("[%10s]", ...)       std::format("[{:>10}]", ...)
  [    商品]                  [      商品]
  [        ab]                [        ab]
  ```

  `printf` 认为「商品」占 6 个字节，只补了 4 个空格，显示出来比 `ab` 那一行短了一截。
- **自定义类型**：特化 `std::formatter<T>`，提供 `format` 成员函数。最简单的做法是继承 `std::formatter<std::string>`，先把对象转换成字符串，再交给它处理，这样宽度、对齐等格式说明也能直接用于自定义类型。

### 2.3 编译期检查格式字符串

```cpp
auto s1 = std::format("{} {}", 1);         // 参数不够
auto s2 = std::format("{:d}", "text");     // 字符串不能用整数格式
```

两者都**编译失败**：

```
error: call to consteval function 'std::basic_format_string<char, int>::basic_format_string<char[6]>' is not a constant expression
note: ... "Argument index outside the valid range"
```

`std::format` 的第一个参数类型是 `std::format_string<Args...>`，它的构造函数是上一篇介绍的 **`consteval`** 函数，在编译期解析格式字符串，并检查每个占位符和参数的类型是否匹配。`printf` 的类型错误要到运行时才暴露（或者根本不暴露），`std::format` 在编译时就能发现。

需要在运行时才确定格式字符串时（比如从配置文件读取），使用 `std::vformat`，此时检查推迟到运行时，出错会抛出 `std::format_error` 异常。

> C++23 增加了 `std::print` 和 `std::println`，直接输出格式化的结果：`std::println("{} 今年 {} 岁", name, age);`。

## 3. std::jthread：会自动 join 的线程

C++11 章节《多线程入门》中讲过，`std::thread` 销毁前必须手动 `join()` 或 `detach()`，否则程序直接终止；而且标准库没有提供「通知线程停止」的机制，只能自己用一个原子变量做标志。C++20 的 **`std::jthread`** 解决了这两个问题：

```cpp
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main()
{
    {
        std::jthread worker([](std::stop_token st) {     // 第一个参数可以是 stop_token
            int n = 0;
            while (!st.stop_requested()) {                 // 定期检查是否被要求停止
                std::this_thread::sleep_for(50ms);
                ++n;
            }
            std::cout << "worker 收到停止请求，退出，共工作了 " << n << " 轮\n";
        });
        std::this_thread::sleep_for(220ms);
        std::cout << "main 离开作用域\n";
    }   // jthread 析构：自动 request_stop()，再自动 join()
    std::cout << "worker 已经结束，main 继续\n";
    return 0;
}
```

运行结果：

```
main 离开作用域
worker 收到停止请求，退出，共工作了 4 轮
worker 已经结束，main 继续
```

- **自动 join**：`jthread` 析构时会自动 `join()`，不会因为忘记而导致程序终止（`j` 就是 joining 的意思）。
- **协作式取消**：如果线程函数的第一个参数是 `std::stop_token`，`jthread` 会自动传入。析构时（或手动调用 `request_stop()` 时），`stop_requested()` 变为 `true`，线程函数检查到后自行退出。之所以叫「协作式」，是因为线程不会被强行终止，而是由线程自己决定在安全的时机退出，不会出现资源泄漏或数据损坏。

**新代码中，优先使用 `std::jthread` 代替 `std::thread`。**

## 4. latch 与 semaphore

C++20 还增加了几个线程同步工具，在一些场景中比互斥锁加条件变量简单得多：

```cpp
#include <atomic>
#include <chrono>
#include <iostream>
#include <latch>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main()
{
    // 1. latch：一次性的倒计数，等待 N 个线程都准备好
    std::latch ready(3);
    std::mutex printMutex;
    std::vector<std::jthread> ts;
    for (int i = 1; i <= 3; ++i)
        ts.emplace_back([&, i] {
            std::this_thread::sleep_for(i * 30ms);
            { std::lock_guard lock(printMutex); std::cout << "线程 " << i << " 初始化完成\n"; }
            ready.count_down();
        });
    ready.wait();                                  // 等到计数减为 0
    std::cout << "所有线程都已就绪\n";
    ts.clear();

    // 2. counting_semaphore：最多允许 2 个线程同时访问
    std::counting_semaphore<2> slots(2);
    std::atomic<int> inside{0}, maxInside{0};
    for (int i = 0; i < 6; ++i)
        ts.emplace_back([&] {
            slots.acquire();                       // 拿一个名额，没有名额就等待
            int now = ++inside;
            int prev = maxInside.load();
            while (now > prev && !maxInside.compare_exchange_weak(prev, now)) {}
            std::this_thread::sleep_for(20ms);
            --inside;
            slots.release();                       // 归还名额
        });
    ts.clear();                                    // jthread 析构时自动 join
    std::cout << "同时在临界区内的线程数最多为 " << maxInside << '\n';
    return 0;
}
```

运行结果：

```
线程 1 初始化完成
线程 2 初始化完成
线程 3 初始化完成
所有线程都已就绪
同时在临界区内的线程数最多为 2
```

| 工具 | 作用 | 典型场景 |
| --- | --- | --- |
| `std::latch` | 一次性倒计数器：`count_down()` 减 1，`wait()` 等到 0 | 主线程等待所有工作线程初始化完成 |
| `std::barrier` | 可重复使用的「集合点」：所有线程到达后一起进入下一阶段 | 多线程分阶段计算 |
| `std::counting_semaphore<N>` | 最多 N 个名额：`acquire()` 拿名额，`release()` 归还 | 限制同时访问数据库连接、文件的线程数 |
| `std::binary_semaphore` | 只有 1 个名额的信号量 | 线程之间的简单通知 |

6 个线程竞争 2 个名额，最多同时只有 2 个线程在临界区中。`ts.clear()` 销毁所有 `jthread`，自动等待它们结束，不需要再写 `join` 循环。

上面的代码中还有一处 C++17 的用法：`std::lock_guard lock(printMutex);` 省略了模板参数，这是 C++17 章节介绍的类模板参数推导。

## 5. 其他实用的小工具

```cpp
#include <bit>
#include <cstdint>
#include <iostream>
#include <map>
#include <numbers>
#include <source_location>
#include <string>

void log(const std::string& msg, std::source_location loc = std::source_location::current())
{
    std::cout << "[" << loc.function_name() << ":" << loc.line() << "] " << msg << '\n';
}

int main()
{
    log("程序启动");                                                  // 自动获得调用处的函数名和行号

    std::cout << "pi = " << std::numbers::pi << ", e = " << std::numbers::e << '\n';

    std::map<std::string, int> m{{"a", 1}};
    std::cout << std::boolalpha << "contains(\"a\"): " << m.contains("a") << '\n';   // 代替 count() / find()

    std::string url = "https://example.com/index.html";
    std::cout << "starts_with https: " << url.starts_with("https://")
              << ", ends_with .html: " << url.ends_with(".html") << '\n';

    std::uint32_t x = 0b0010'1100;
    std::cout << "popcount = " << std::popcount(x) << ", countr_zero = " << std::countr_zero(x)
              << ", bit_width = " << std::bit_width(x) << ", has_single_bit(64) = " << std::has_single_bit(64u) << '\n';
    return 0;
}
```

运行结果：

```
[int main():16] 程序启动
pi = 3.14159, e = 2.71828
contains("a"): true
starts_with https: true, ends_with .html: true
popcount = 3, countr_zero = 2, bit_width = 6, has_single_bit(64) = true
```

| 工具 | 说明 |
| --- | --- |
| `std::source_location` | 作为默认参数时，自动获得**调用处**的文件名、行号、函数名，代替 `__FILE__`、`__LINE__` 宏，适合写日志和断言 |
| `std::numbers::pi`、`e`、`sqrt2` 等 | 标准的数学常量，用变量模板实现，`std::numbers::pi_v<float>` 是 float 版本 |
| `contains(key)` | `map`、`set` 等关联容器判断是否包含某个键，代替 `m.count(k) > 0` 或 `m.find(k) != m.end()` |
| `starts_with` / `ends_with` | `string` 和 `string_view` 判断前缀、后缀 |
| `<bit>` | `popcount`（1 的个数）、`countr_zero`（末尾 0 的个数）、`bit_width`、`has_single_bit`（是否为 2 的幂）、`std::bit_cast`（安全的按位类型转换） |
| `std::ssize(c)` | 返回有符号的大小，避免 `for (int i = 0; i < v.size(); ++i)` 中有符号和无符号比较的警告 |
| `std::erase` / `std::erase_if` | `std::erase_if(v, pred)` 一步删除满足条件的元素，代替 `v.erase(std::remove_if(...), v.end())` |

## 6. 小结

- **`std::span<T>`**：连续内存的视图，统一接受 C 数组、`array`、`vector`，自带长度；不拥有数据，按值传递
- **`std::format`**：类型安全、语法简洁，支持对齐、精度、进制、中文宽度；格式字符串在编译期检查；特化 `std::formatter` 支持自定义类型
- **`std::jthread`**：析构时自动 `request_stop()` 并 `join()`；配合 `std::stop_token` 实现协作式取消；优先代替 `std::thread`
- **`latch`、`barrier`、`counting_semaphore`**：等待一组线程、分阶段同步、限制并发数
- `source_location`、`numbers`、`contains`、`starts_with`、`<bit>`、`erase_if` 等小工具让日常代码更简洁

## 7. 现代 C++ 学习路线回顾

至此，本专栏的现代 C++ 部分全部完成。回顾一下这一路学过的内容：

| 标准 | 核心内容 | 最重要的几篇 |
| --- | --- | --- |
| **C++11** | 现代 C++ 的起点：类型推导、移动语义、智能指针、lambda、多线程 | 《右值引用与移动语义》《智能指针》《原子操作与内存序》 |
| **C++14** | 对 C++11 的完善：泛型 lambda、返回类型推导、放宽的 constexpr | 《泛型 lambda 与初始化捕获》 |
| **C++17** | 实用特性大量增加：结构化绑定、`if constexpr`、`optional`、`variant`、`string_view`、`filesystem`、并行算法 | 《if constexpr》《std::variant》《并行算法》 |
| **C++20** | 又一次大变革：Concepts、Ranges、协程、模块 | 《Concepts》《协程入门》 |

学习建议：

1. **先把 C++11 学扎实**。移动语义、智能指针、lambda 是后面所有内容的基础，也是面试中最常被问到的部分。
2. **每个特性都动手写一遍**。本专栏中所有的示例都可以直接编译运行，修改它们，观察结果的变化，比读十遍更有效。
3. **养成使用工具的习惯**：编译时开启 `-Wall -Wextra`，调试内存问题用 AddressSanitizer（`-fsanitize=address`），调试多线程问题用 ThreadSanitizer（`-fsanitize=thread`）。本专栏中很多「看起来没问题」的代码，都是靠它们揪出来的。
4. **不必追求用上所有新特性**。新特性的价值在于让代码更安全、更清晰，而不是更炫技。

## 8. 练习

1. 写一个函数 `std::span<const int> findRun(std::span<const int> data, int value)`，返回 `data` 中第一段连续等于 `value` 的子区间。
2. 用 `std::format` 输出一个九九乘法表，每一项宽度对齐。
3. 用 `std::jthread` 写一个后台线程，每 100 毫秒打印一次进度百分比，主线程 1 秒后让它停止。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::span<const int> findRun(std::span<const int> data, int value)
   {
       std::size_t i = 0;
       while (i < data.size() && data[i] != value) ++i;
       std::size_t j = i;
       while (j < data.size() && data[j] == value) ++j;
       return data.subspan(i, j - i);
   }
   ```

   找不到时 `i == j == data.size()`，返回一个空的 span。

2. 参考代码：

   ```cpp
   for (int i = 1; i <= 9; ++i) {
       for (int j = 1; j <= i; ++j)
           std::cout << std::format("{}×{}={:<3}", j, i, i * j);
       std::cout << '\n';
   }
   ```

3. 参考代码：

   ```cpp
   std::jthread progress([](std::stop_token st) {
       int percent = 0;
       while (!st.stop_requested()) {
           std::cout << std::format("进度 {}%\n", percent);
           percent = std::min(percent + 10, 100);
           std::this_thread::sleep_for(std::chrono::milliseconds(100));
       }
   });
   std::this_thread::sleep_for(std::chrono::seconds(1));
   progress.request_stop();     // 或者什么都不写，等 progress 析构时自动停止
   ```

</details>
