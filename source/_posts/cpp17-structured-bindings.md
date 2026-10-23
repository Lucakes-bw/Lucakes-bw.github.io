---
title: 结构化绑定
date: 2026-10-22 15:20:00
updated: 2026-10-22 15:20:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 结构化绑定
order: 1
description: auto [a, b] = ... 一次把 pair、tuple、结构体、数组拆成多个变量。本文讲清楚结构化绑定的三种适用对象、按值与按引用的区别、编译器背后生成的「隐藏变量」，以及如何让自己的类支持结构化绑定。
disableNunjucks: true
---

> **本文目标**：会用结构化绑定拆解结构体、`pair`、`tuple`、数组；遍历 `map` 时直接得到键和值；分清 `auto`、`auto&`、`const auto&` 三种写法；理解结构化绑定的名字其实是「别名」；知道 C++17 中的几个限制；能让自定义类支持结构化绑定。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`，GCC 7+ / Clang 4+ / VS2017 15.3+。

## 1. C++17 概述

C++17 是继 C++11 之后又一个重要的版本，带来了大量让日常编码更方便的特性。本章 9 篇文章：

| 文章 | 内容 |
| --- | --- |
| 本篇 | 结构化绑定 |
| if/switch 初始化与 if constexpr | 带初始化语句的条件语句；编译期分支 |
| std::optional | 「可能没有值」的类型 |
| std::variant | 类型安全的联合体 |
| string_view 与 any | 字符串的只读视图；能装任何类型的容器 |
| 折叠表达式与类模板参数推导 | 简化可变参数模板；构造类模板时省略模板参数 |
| inline 变量与常用属性 | 头文件中定义变量；`[[nodiscard]]` 等属性 |
| std::filesystem | 跨平台的文件和目录操作 |
| 并行算法 | 一个参数让标准算法用上多核 |

## 2. 为什么需要结构化绑定

C++11 中，处理 `pair` 和 `tuple` 很啰嗦：

```cpp
for (const auto& kv : ages)
    std::cout << kv.first << " -> " << kv.second;    // first、second 是什么意思？

int mn, mx;
double avg;
std::tie(mn, mx, avg) = stats();                     // 要先声明变量，还不能是 const 或引用
```

`kv.first` 和 `kv.second` 完全没有表达出「名字」和「年龄」的含义；`std::tie` 要求先声明好变量，类型还得自己写对。C++17 的**结构化绑定**（structured binding）一行解决：

```cpp
for (const auto& [name, age] : ages)
    std::cout << name << " -> " << age;

auto [mn, mx, avg] = stats();
```

## 3. 基本用法

```cpp
#include <iostream>
#include <map>
#include <string>
#include <tuple>

struct Student {
    std::string name;
    int age;
    double score;
};

std::tuple<int, int, double> stats() { return {4, 42, 18.0}; }

int main()
{
    // 1. 结构体
    Student s{"Alice", 20, 92.5};
    auto [name, age, score] = s;
    std::cout << name << ' ' << age << ' ' << score << '\n';

    // 2. tuple：函数返回多个值
    auto [mn, mx, avg] = stats();
    std::cout << "min=" << mn << " max=" << mx << " avg=" << avg << '\n';

    // 3. 数组
    int point[2] = {3, 4};
    auto [x, y] = point;
    std::cout << "x=" << x << " y=" << y << '\n';

    // 4. 遍历 map：不再需要 .first / .second
    std::map<std::string, int> ages{{"Alice", 20}, {"Bob", 22}};
    for (const auto& [key, value] : ages)
        std::cout << key << " -> " << value << '\n';

    // 5. insert 返回 pair<iterator, bool>
    if (auto [it, inserted] = ages.insert({"Carol", 21}); inserted)
        std::cout << "插入了 " << it->first << '\n';
    auto [it2, inserted2] = ages.insert({"Alice", 99});
    std::cout << "再次插入 Alice: " << (inserted2 ? "成功" : "已存在，值仍为 ") << it2->second << '\n';
    return 0;
}
```

运行结果：

```
Alice 20 92.5
min=4 max=42 avg=18
x=3 y=4
Alice -> 20
Bob -> 22
插入了 Carol
再次插入 Alice: 已存在，值仍为 20
```

语法是 `auto [名字1, 名字2, ...] = 表达式;`，**名字的个数必须和元素个数完全相等**。它能拆解三类对象：

| 类型 | 条件 | 拆解方式 |
| --- | --- | --- |
| 数组 | 任何固定大小的数组 | 按下标顺序 |
| 类元组类型 | `std::pair`、`std::tuple`、`std::array`，以及实现了「元组协议」的类（见第 7 节） | 按 `get<0>`、`get<1>`…… |
| 结构体 / 类 | 所有非静态成员都是 public，且都在同一个类中声明 | 按成员声明顺序 |

第 5 个例子用到了 C++17 的 **if 初始化语句**（`if (初始化; 条件)`），下一篇会详细介绍。`map::insert` 返回 `pair<iterator, bool>`，用结构化绑定取出来后，`inserted` 比 `result.second` 清楚得多。

## 4. 按值还是按引用

和 `auto` 一样，`[...]` 前面可以加 `&`、`const`：

```cpp
#include <iostream>
#include <map>
#include <string>

struct Point { int x; int y; };

int main()
{
    Point p{1, 2};

    auto [a, b] = p;          // 拷贝：a、b 指向一个 p 的副本
    a = 100;
    std::cout << "auto  修改后 p.x = " << p.x << '\n';

    auto& [c, d] = p;         // 引用：c、d 就是 p.x、p.y
    c = 100;
    std::cout << "auto& 修改后 p.x = " << p.x << '\n';

    std::map<std::string, int> stock{{"apple", 5}, {"pear", 3}};
    for (auto& [fruit, count] : stock)     // 引用：可以直接修改 map 中的值
        count *= 10;
    for (const auto& [fruit, count] : stock)
        std::cout << fruit << ": " << count << '\n';
    return 0;
}
```

运行结果：

```
auto  修改后 p.x = 1
auto& 修改后 p.x = 100
apple: 50
pear: 30
```

选择的原则和范围 for 一样：**只读用 `const auto&`，要修改用 `auto&`，需要独立的副本才用 `auto`**。遍历 `map` 时用 `auto&` 可以直接修改值（但不能修改键，键是 `const` 的）。

## 5. 结构化绑定的本质：隐藏变量

结构化绑定引入的名字**并不是普通的变量**。编译器实际上做了两件事：

1. 先声明一个**匿名的隐藏变量**，比如 `auto& __e = p;`。`[...]` 前面的 `auto`、`auto&`、`const` 修饰的都是这个隐藏变量。
2. 然后让 `a`、`b` 分别成为 `__e.x`、`__e.y` 的**别名**。

```cpp
#include <iostream>
#include <type_traits>

struct Point { int x; const int y; };

int main()
{
    Point p{1, 2};
    auto& [a, b] = p;
    // 编译器实际生成：auto& __e = p;  a 是 __e.x 的别名，b 是 __e.y 的别名
    std::cout << std::boolalpha
              << "&a == &p.x: " << (&a == &p.x) << '\n'
              << "decltype(a) 是 int: " << std::is_same<decltype(a), int>::value << '\n'
              << "decltype(b) 是 const int: " << std::is_same<decltype(b), const int>::value << '\n';
    return 0;
}
```

运行结果：

```
&a == &p.x: true
decltype(a) 是 int: true
decltype(b) 是 const int: true
```

这解释了几个现象：

- **`a` 的地址就是 `p.x` 的地址**，它们是同一个东西。
- **`decltype(a)` 是 `int` 而不是 `int&`**：`a` 是成员的别名，`decltype` 得到的是成员声明时的类型。成员本身是 `const int`，别名的类型也是 `const int`。
- 用 `auto [a, b] = p;`（不加 `&`）时，隐藏变量是 `p` 的**整体拷贝**，`a`、`b` 是这个拷贝的成员的别名。所以修改 `a` 不影响 `p`。
- 对于临时对象，比如 `auto [mn, mx, avg] = stats();`，隐藏变量保存了这个临时 tuple，它的生命周期和结构化绑定一样长，所以是安全的。即使写 `const auto& [...] = stats();`，临时对象的生命周期也会被延长。

## 6. 常见错误与限制

```cpp
#include <tuple>
std::tuple<int, int, int> get3() { return {1, 2, 3}; }
int main()
{
    auto [a, b] = get3();                 // 错误：数量不匹配
    auto [x, y, z] = get3();
    auto f = [x]() { return x; };         // C++17 中错误：lambda 不能捕获结构化绑定
    static auto [s1, s2, s3] = get3();    // C++17 中错误：不能是 static
    return f();
}
```

1. **名字数量必须完全匹配**，不能只取一部分：

   ```
   error: type 'std::tuple<int, int, int>' decomposes into 3 elements, but only 2 names were provided
   ```

   不需要的元素也要起一个名字。C++26 才允许用 `_` 作为占位符。

2. **C++17 中 lambda 不能捕获结构化绑定**，C++20 起允许。Clang 在 `-std=c++17` 下把它作为扩展接受，只给出警告 `captured structured bindings are a C++20 extension`；加上 `-pedantic-errors`（严格遵守标准）就会报错。为了代码可移植，C++17 中可以用初始化捕获绕过：`[x = x]() { return x; }`。

3. **C++17 中不能声明为 `static`、`thread_local` 或 `constexpr`**。`static` 和 `thread_local` 在 C++20 中被允许，`constexpr` 要到 C++26。

4. **不能指定每个名字的类型**，所有名字共享 `[...]` 前面的那一个 `auto` 修饰。

5. **结构体必须所有成员都是 public**，并且不能有一部分成员在基类、一部分在派生类中。

## 7. 进阶：让自定义类支持结构化绑定

成员是私有的类，默认不能使用结构化绑定。可以通过实现**元组协议**（tuple-like protocol）来支持它，需要提供三样东西：

1. `std::tuple_size<T>`：元素个数；
2. `std::tuple_element<I, T>`：第 I 个元素的类型；
3. 成员函数 `get<I>()` 或非成员函数 `get<I>(obj)`：取出第 I 个元素。

```cpp
#include <iostream>
#include <string>
#include <tuple>
#include <utility>

// 一个成员是私有的类，默认不能使用结构化绑定
class Employee {
public:
    Employee(std::string n, int id) : name_(std::move(n)), id_(id) {}

    // 按照“类元组协议”提供 get<I>()
    template <std::size_t I>
    decltype(auto) get() const
    {
        if constexpr (I == 0) return (name_);   // 返回 const std::string&
        else return id_;
    }
private:
    std::string name_;
    int id_;
};

// 告诉编译器：Employee 有 2 个元素，以及每个元素的类型
namespace std {
template <> struct tuple_size<Employee> : integral_constant<size_t, 2> {};
template <> struct tuple_element<0, Employee> { using type = const std::string&; };
template <> struct tuple_element<1, Employee> { using type = int; };
}

int main()
{
    Employee e("Alice", 1001);
    auto [name, id] = e;
    std::cout << name << " #" << id << '\n';
    return 0;
}
```

运行结果：

```
Alice #1001
```

要点：

- `std::tuple_size` 和 `std::tuple_element` 需要在 `std` 命名空间中**特化**，这是标准允许的少数几种向 `std` 中添加内容的方式之一。
- `get<I>()` 中用到了 **`if constexpr`**（下一篇介绍）：根据编译期常量 `I` 选择返回哪个成员，不同分支可以返回不同的类型。
- `return (name_);` 加了括号，配合 `decltype(auto)` 返回引用，避免拷贝字符串。这正是 C++14 章节《返回类型推导与 decltype(auto)》中讲的规则，这里的 `name_` 是成员变量，返回它的引用是安全的。

实际项目中很少需要这样做。如果一个类的主要用途就是打包几个值，把成员设为 public 的简单结构体就足够了。

## 8. 小结

- `auto [a, b, c] = 表达式;` 把数组、`pair`/`tuple`/`array`、所有成员为 public 的结构体拆成多个名字
- 名字数量必须和元素个数相等
- `[...]` 前的 `auto`、`auto&`、`const auto&` 修饰的是一个**隐藏变量**，绑定的名字是这个隐藏变量的成员的**别名**
- 遍历 `map` 用 `for (const auto& [key, value] : m)`，要修改值用 `auto&`
- C++17 中不能被 lambda 捕获、不能是 `static`（C++20 起允许）
- 实现 `tuple_size`、`tuple_element`、`get` 三件套，可以让自定义类支持结构化绑定

## 9. 练习

1. 用结构化绑定改写下面的代码：

   ```cpp
   std::map<std::string, std::vector<int>> scores;
   for (std::map<std::string, std::vector<int>>::iterator it = scores.begin(); it != scores.end(); ++it) {
       int total = 0;
       for (int s : it->second) total += s;
       std::cout << it->first << ": " << total << '\n';
   }
   ```

2. 写一个函数 `divmod(a, b)`，同时返回商和余数，调用者用结构化绑定接收。

3. 下面的代码输出什么？

   ```cpp
   std::pair<int, int> p{1, 2};
   auto& [a, b] = p;
   auto [c, d] = p;
   a = 10;
   c = 30;
   std::cout << p.first << ' ' << b << ' ' << d << '\n';
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   for (const auto& [name, list] : scores) {
       int total = 0;
       for (int s : list) total += s;
       std::cout << name << ": " << total << '\n';
   }
   ```

2. 参考代码：

   ```cpp
   std::pair<int, int> divmod(int a, int b) { return {a / b, a % b}; }

   auto [q, r] = divmod(17, 5);   // q = 3, r = 2
   ```

3. 输出 `10 2 2`。`a` 是 `p.first` 的别名，修改 `a` 就是修改 `p.first`；`c`、`d` 是 `p` 的一个**拷贝**的成员，修改 `c` 不影响 `p`。`b` 和 `d` 都没被修改，仍然是 2。

</details>
