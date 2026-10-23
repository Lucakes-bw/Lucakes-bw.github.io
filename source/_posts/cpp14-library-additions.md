---
title: C++14 标准库的补充
date: 2026-10-22 15:00:00
updated: 2026-10-22 15:00:00
categories:
  - C/C++
  - C++14
tags:
  - C++14
  - 标准库
order: 4
description: C++14 给标准库补上了几块重要的拼图：make_unique、std::exchange、integer_sequence、读写锁 shared_timed_mutex、字符串和时间的字面量后缀，以及避免临时对象的异构查找。每一项都配有可运行的示例和实测数据。
disableNunjucks: true
---

> **本文目标**：用 `std::make_unique` 代替 `new`；会用 `std::exchange` 简化移动操作；会用 `std::index_sequence` 把 tuple 展开成函数参数；会用读写锁让多个读者并发访问；会用 `"..."s`、`100ms` 等标准字面量；会用 `std::less<>` 避免查找时构造临时对象。
>
> **前置知识**：C++11 章节的《智能指针》《可变参数模板》《多线程入门》。
>
> **编译环境**：`g++ -std=c++14` / `clang++ -std=c++14`。

## 1. std::make_unique

C++11 有 `std::make_shared`，却漏掉了 `std::make_unique`，C++14 补上了它：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Widget {
    Widget(std::string n, int v) : name(std::move(n)), value(v) {}
    std::string name;
    int value;
};

int main()
{
    auto w = std::make_unique<Widget>("button", 3);           // 参数完美转发给构造函数
    auto arr = std::make_unique<int[]>(5);                    // 数组版本：5 个值初始化为 0 的 int
    arr[2] = 42;
    std::vector<std::unique_ptr<Widget>> list;
    list.push_back(std::make_unique<Widget>("label", 1));
    std::cout << w->name << ' ' << w->value << ", arr[0] = " << arr[0] << ", arr[2] = " << arr[2]
              << ", list[0] = " << list[0]->name << '\n';
    return 0;
}
```

运行结果：

```
button 3, arr[0] = 0, arr[2] = 42, list[0] = label
```

使用 `make_unique` 的理由（详见 C++11 章节《智能指针》）：

1. **不用重复写类型**：`std::unique_ptr<Widget> w(new Widget(...))` 中 `Widget` 写了两遍。
2. **异常安全**：分配内存和交给智能指针在同一个函数调用中完成，不会出现中间状态。
3. **代码中不再出现 `new`**：配合 `make_shared`，现代 C++ 代码中几乎看不到 `new` 和 `delete`，一眼就能看出资源的所有权是安全的。

`make_unique<int[]>(5)` 是数组版本，元素会被**值初始化**（`int` 初始化为 0）。

**规则：除非需要自定义删除器，否则一律用 `make_unique` / `make_shared` 创建智能指针。**

## 2. std::exchange

`std::exchange(obj, newValue)` 把 `obj` 设置为 `newValue`，并**返回 `obj` 的旧值**。它很简单，却能让很多代码更简洁：

```cpp
#include <iostream>
#include <string>
#include <utility>
#include <vector>

class Buffer {
public:
    explicit Buffer(std::size_t n) : size_(n), data_(new int[n]()) {}
    ~Buffer() { delete[] data_; }
    // 用 std::exchange 写移动构造：取走对方的值，同时把对方设为空
    Buffer(Buffer&& other) noexcept
        : size_(std::exchange(other.size_, 0)), data_(std::exchange(other.data_, nullptr)) {}
    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            size_ = std::exchange(other.size_, 0);
            data_ = std::exchange(other.data_, nullptr);
        }
        return *this;
    }
    std::size_t size() const { return size_; }
private:
    std::size_t size_;
    int* data_;
};

int main()
{
    int state = 1;
    int old = std::exchange(state, 2);        // state 变为 2，返回旧值 1
    std::cout << "old = " << old << ", state = " << state << '\n';

    Buffer a(100);
    Buffer b(std::move(a));
    std::cout << "a.size() = " << a.size() << ", b.size() = " << b.size() << '\n';

    // 斐波那契：一行完成“记住旧值并更新”
    long long x = 0, y = 1;
    for (int i = 0; i < 10; ++i) x = std::exchange(y, x + y);
    std::cout << "fib(10) = " << x << '\n';

    // 取走容器中的内容，并把它清空
    std::vector<std::string> pending{"任务1", "任务2"};
    auto batch = std::exchange(pending, {});
    std::cout << "batch " << batch.size() << " 个, pending " << pending.size() << " 个\n";
    return 0;
}
```

运行结果：

```
old = 1, state = 2
a.size() = 0, b.size() = 100
fib(10) = 55
batch 2 个, pending 0 个
```

- **移动操作**：C++11 章节《右值引用与移动语义》中，移动构造函数要先复制对方的指针，再把对方的指针置空，分两步写。`std::exchange` 一步完成，而且可以直接写在初始化列表里。
- **斐波那契**：`x = std::exchange(y, x + y);` 把 `y` 更新为 `x + y`，同时把 `y` 的旧值赋给 `x`，不需要临时变量。
- **取走并清空**：`std::exchange(pending, {})` 把容器的内容整体取走（移动），并把原容器换成一个空容器。在多线程中，持有锁时这样「取走一批任务」，然后在锁外处理，可以让临界区非常短。

## 3. std::integer_sequence

C++11 的 `std::tuple` 有一个不方便的地方：`std::get<I>` 的 `I` 必须是编译期常量，所以不能用普通的循环遍历 tuple 的元素。C++14 提供了 `std::integer_sequence`，它是一个「编译期整数序列」类型：

```cpp
std::index_sequence<0, 1, 2, 3>           // 一个类型，携带了整数 0, 1, 2, 3
std::make_index_sequence<4>               // 就是 std::index_sequence<0, 1, 2, 3>
```

把它作为函数参数传进去，配合参数包展开，就能对 `0` 到 `N-1` 的每个下标做同样的操作：

```cpp
#include <iostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

// 把 tuple 的各个元素展开成函数参数（C++17 的 std::apply 就是这样实现的）
template <typename F, typename Tuple, std::size_t... I>
decltype(auto) applyImpl(F&& f, Tuple&& t, std::index_sequence<I...>)
{
    return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}

template <typename F, typename Tuple>
decltype(auto) apply(F&& f, Tuple&& t)
{
    constexpr std::size_t N = std::tuple_size<std::decay_t<Tuple>>::value;
    return applyImpl(std::forward<F>(f), std::forward<Tuple>(t), std::make_index_sequence<N>{});
}

// 打印 tuple 的每个元素
template <typename Tuple, std::size_t... I>
void printTuple(const Tuple& t, std::index_sequence<I...>)
{
    int dummy[] = {0, (std::cout << (I == 0 ? "" : ", ") << std::get<I>(t), 0)...};
    (void)dummy;
    std::cout << '\n';
}

int describe(const std::string& name, int age, double score)
{
    std::cout << name << "，" << age << " 岁，成绩 " << score << '\n';
    return age;
}

int main()
{
    auto student = std::make_tuple(std::string("Alice"), 20, 92.5);
    int age = apply(describe, student);
    std::cout << "返回值 " << age << '\n';
    printTuple(student, std::make_index_sequence<3>{});

    // make_index_sequence<4> 的类型就是 index_sequence<0, 1, 2, 3>
    static_assert(std::is_same<std::make_index_sequence<4>, std::index_sequence<0, 1, 2, 3>>::value, "");
    return 0;
}
```

运行结果：

```
Alice，20 岁，成绩 92.5
返回值 20
Alice, 20, 92.5
```

理解 `apply` 的关键：

1. `apply` 用 `std::make_index_sequence<3>{}` 创建了一个 `index_sequence<0, 1, 2>` 类型的空对象，传给 `applyImpl`。
2. `applyImpl` 的最后一个参数类型是 `std::index_sequence<I...>`，编译器据此推导出参数包 `I` 是 `0, 1, 2`。这个参数本身没有名字，也不会被使用，**它的唯一作用就是让编译器推导出 `I...`**。
3. `std::get<I>(t)...` 展开成 `std::get<0>(t), std::get<1>(t), std::get<2>(t)`，正好作为 `describe` 的三个参数。

这个技巧是很多泛型库的基础。C++17 的 `std::apply` 就是这样实现的，C++17 的折叠表达式还能让 `printTuple` 写得更简洁。

另外，`std::decay_t<Tuple>` 是 C++14 新增的**类型特征别名**：C++11 中要写 `typename std::decay<Tuple>::type`，C++14 为所有返回类型的特征都提供了 `_t` 版本，比如 `std::remove_reference_t`、`std::enable_if_t`、`std::conditional_t`。

## 4. 读写锁：shared_timed_mutex

很多数据**读得多、写得少**，比如配置、缓存、路由表。用 `std::mutex` 保护它们时，即使多个线程都只是读，也只能一个一个排队。C++14 的**读写锁** `std::shared_timed_mutex` 区分两种锁：

- **共享锁**（读锁）：多个线程可以**同时**持有；
- **独占锁**（写锁）：只能有一个线程持有，并且持有期间不能有任何读锁。

```cpp
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

class Config {
public:
    std::string get(const std::string& key) const
    {
        std::shared_lock<std::shared_timed_mutex> lock(m_);   // 读锁：多个读者可以同时持有
        std::this_thread::sleep_for(std::chrono::milliseconds(50));   // 模拟较慢的读取
        auto it = data_.find(key);
        return it == data_.end() ? "" : it->second;
    }
    void set(const std::string& key, const std::string& value)
    {
        std::unique_lock<std::shared_timed_mutex> lock(m_);   // 写锁：独占
        data_[key] = value;
    }
private:
    mutable std::shared_timed_mutex m_;
    std::map<std::string, std::string> data_;
};

int main()
{
    Config cfg;
    cfg.set("host", "localhost");
    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i)
        readers.emplace_back([&cfg] { cfg.get("host"); });
    for (auto& t : readers) t.join();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "4 个读者各读 50ms，总耗时约 " << (ms + 25) / 50 * 50 << " ms（读锁可以共享）\n";
    return 0;
}
```

运行结果：

```
4 个读者各读 50ms，总耗时约 50 ms（读锁可以共享）
```

4 个读者同时持有读锁，总耗时和一个读者一样。如果换成 `std::mutex`，4 次读取必须排队，总耗时约 200ms。

- 读操作用 **`std::shared_lock`**（C++14 新增），写操作用 `std::unique_lock` 或 `std::lock_guard`。
- `mutable` 让 `const` 成员函数 `get` 也能加锁。

> 名字里的 `timed` 表示它支持带超时的加锁（`try_lock_for` 等）。C++17 增加了不带超时、更轻量的 `std::shared_mutex`，新代码优先用它。另外，读写锁本身比普通互斥锁复杂，开销也更大，**只有在读远多于写、且读操作耗时较长时，才值得使用**。

## 5. 标准库字面量

C++11 允许用户自定义字面量后缀（`operator""`），C++14 在标准库中提供了一批常用的后缀：

```cpp
#include <chrono>
#include <complex>
#include <iostream>
#include <string>
#include <thread>

int main()
{
    using namespace std::literals;          // 引入标准库的字面量后缀

    auto s = "hello"s;                      // std::string，而不是 const char*
    std::cout << s.size() << ' ' << ("abc"s + "def") << '\n';

    auto timeout = 1500ms;                  // std::chrono::milliseconds
    auto interval = 2s;                     // std::chrono::seconds
    auto total = timeout + interval;        // 自动换算成 3500ms
    std::cout << "total = " << total.count() << " ms\n";
    std::this_thread::sleep_for(10ms);

    auto z = 3.0 + 4.0i;                    // std::complex<double>
    std::cout << "|3 + 4i| = " << std::abs(z) << '\n';

    std::string withNull = "a\0b"s;         // 用 s 后缀时，中间的 \0 也会被保留
    std::cout << "withNull.size() = " << withNull.size() << ", std::string(\"a\\0b\").size() = "
              << std::string("a\0b").size() << '\n';
    return 0;
}
```

运行结果：

```
5 abcdef
total = 3500 ms
|3 + 4i| = 5
withNull.size() = 3, std::string("a\0b").size() = 1
```

| 后缀 | 类型 | 例子 |
| --- | --- | --- |
| `s`（字符串） | `std::string` | `"hello"s` |
| `h`、`min`、`s`、`ms`、`us`、`ns` | `std::chrono::duration` | `1500ms`、`2s` |
| `i`、`if`、`il` | `std::complex` | `4.0i` |

- **`"abc"s + "def"`**：两个字符串字面量（`const char*`）不能直接相加，`"abc" + "def"` 是编译错误；加上 `s` 后缀，左边就是 `std::string`，可以相加。
- **时间字面量**让代码一目了然：`sleep_for(10ms)` 比 `sleep_for(std::chrono::milliseconds(10))` 简洁得多；不同单位的时间可以直接相加，自动换算。
- **`"a\0b"s` 保留了中间的 `\0`**：用 `const char*` 构造 `std::string` 时，遇到 `\0` 就认为字符串结束了，所以 `std::string("a\0b")` 的长度是 1；`s` 后缀知道字面量的真实长度，得到长度为 3 的字符串。
- 字符串的 `s` 和秒的 `s` 不会冲突：一个作用于字符串字面量，一个作用于数字字面量。

`using namespace std::literals;` 只引入字面量后缀，不会引入整个 `std` 命名空间，可以放心使用（但仍然不要写在头文件的全局作用域中）。

## 6. 异构查找：std::less<>

`std::set<std::string>` 的 `find` 参数类型是 `const std::string&`。如果你手里只有一个 `const char*`，调用 `find` 时会先**构造一个临时的 `std::string`**，字符串较长时就要分配堆内存。用自定义的 `operator new` 统计堆分配次数：

```cpp
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>
#include <string>

static int allocations = 0;
void* operator new(std::size_t n)
{
    ++allocations;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }

int main()
{
    std::set<std::string> plain{"a-very-long-key-that-will-not-fit-in-sso"};
    std::set<std::string, std::less<>> transparent{"a-very-long-key-that-will-not-fit-in-sso"};
    const char* key = "a-very-long-key-that-will-not-fit-in-sso";

    int found = 0;
    allocations = 0;
    for (int i = 0; i < 1000; ++i) found += plain.find(key) != plain.end();          // 每次都要先构造一个临时 std::string
    std::printf("std::less<std::string>: 查找 1000 次，堆分配 %d 次\n", allocations);

    allocations = 0;
    for (int i = 0; i < 1000; ++i) found += transparent.find(key) != transparent.end();    // 直接用 const char* 比较
    std::printf("std::less<>           : 查找 1000 次，堆分配 %d 次（共找到 %d 次）\n", allocations, found);
    return 0;
}
```

运行结果：

```
std::less<std::string>: 查找 1000 次，堆分配 1000 次
std::less<>           : 查找 1000 次，堆分配 0 次（共找到 2000 次）
```

C++14 的 **`std::less<>`**（即 `std::less<void>`）是一个「透明」的比较器：它的 `operator()` 是模板，可以直接比较 `std::string` 和 `const char*`。当 `set` 或 `map` 的比较器是透明的，`find`、`count`、`lower_bound`、`upper_bound`、`equal_range` 就会接受**任何能与键比较的类型**，不再构造临时对象。这叫做**异构查找**（heterogeneous lookup）。

> 键较短时，`std::string` 的小字符串优化（SSO）会避免堆分配，但构造临时对象、复制字符的开销仍然存在。在高频查找的场合（比如用字符串做键的路由表），换成 `std::less<>` 是一个几乎零成本的优化。C++20 起，`unordered_map` 和 `unordered_set` 也支持异构查找。

## 7. 其他小改进

- **`std::cbegin` / `std::cend` / `std::rbegin` / `std::rend`**：C++11 只有非成员的 `std::begin` / `std::end`，C++14 补全了 const 和反向版本，对数组也适用。
- **`std::quoted`**：输出时给字符串加上引号并转义其中的引号，读入时去掉引号，方便处理带空格的字符串：`std::cout << std::quoted("He said \"hi\"");` 输出 `"He said \"hi\""`。
- **`std::equal`、`std::mismatch`、`std::is_permutation` 的四迭代器版本**：同时传入两个范围的开始和结束，长度不同时也能安全比较，不会越界读取第二个范围。
- **`std::result_of_t`、`std::enable_if_t` 等 `_t` 别名**：见第 3 节。

## 8. 小结

| 新增内容 | 一句话 |
| --- | --- |
| `std::make_unique` | 创建 `unique_ptr` 的首选方式，代码中不再出现 `new` |
| `std::exchange` | 设置新值并返回旧值，简化移动操作和「取走并清空」 |
| `std::index_sequence` | 编译期整数序列，用来展开 tuple 等需要编译期下标的场合 |
| `std::shared_timed_mutex` + `std::shared_lock` | 读写锁，读多写少时让读者并发 |
| `"..."s`、`10ms`、`4.0i` | 标准字面量，需要 `using namespace std::literals;` |
| `std::less<>` | 透明比较器，`set`/`map` 查找时不构造临时对象 |
| `std::xxx_t<T>` | 类型特征的简写，代替 `typename std::xxx<T>::type` |

## 9. 练习

1. 用 `std::exchange` 实现一个线程安全的任务队列的 `takeAll()` 方法：持有锁时取走所有任务并清空队列，在锁外返回这些任务。
2. 用 `std::index_sequence` 写一个函数 `reverseTuple(t)`，返回元素顺序反转后的新 tuple，例如 `(1, 'a', 2.5)` 变成 `(2.5, 'a', 1)`。
3. 下面两行代码分别输出什么？

   ```cpp
   using namespace std::literals;
   std::cout << ("ab"s + "cd").size() << ' ' << (500ms + 1s).count() << '\n';
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::vector<Task> takeAll()
   {
       std::lock_guard<std::mutex> lock(m_);
       return std::exchange(tasks_, {});
   }
   ```

   锁内只做了一次移动，非常快；调用者拿到任务后在锁外慢慢处理。

2. 参考代码：

   ```cpp
   template <typename Tuple, std::size_t... I>
   auto reverseImpl(const Tuple& t, std::index_sequence<I...>)
   {
       constexpr std::size_t N = sizeof...(I);
       return std::make_tuple(std::get<N - 1 - I>(t)...);
   }

   template <typename... Ts>
   auto reverseTuple(const std::tuple<Ts...>& t)
   {
       return reverseImpl(t, std::index_sequence_for<Ts...>{});
   }
   ```

   `std::index_sequence_for<Ts...>` 等价于 `std::make_index_sequence<sizeof...(Ts)>`。

3. 输出 `4 1500`。`"ab"s + "cd"` 得到 `"abcd"`，长度 4；`500ms + 1s` 的结果类型是两者中更精细的单位，即毫秒，值为 1500。

</details>
