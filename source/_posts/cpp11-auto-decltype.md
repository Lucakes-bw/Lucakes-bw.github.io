---
title: auto 与 decltype：让编译器推导类型
date: 2026-10-22 09:00:00
updated: 2026-10-22 09:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 类型推导
order: 1
description: auto 让编译器根据初始值推导变量类型，decltype 根据表达式得到类型。本文讲清楚它们的用法、推导规则和常见坑。
disableNunjucks: true
---

> **本文目标**：会用 `auto` 简化代码；知道 `auto` 什么时候会丢掉 `const` 和引用；会用 `decltype` 获取表达式的类型；了解尾置返回类型。
>
> **编译环境**：本文代码均使用 `g++ -std=c++11` 或 `clang++ -std=c++11` 编译通过，GCC 4.8+ / Clang 3.3+ / VS2015+ 均支持。

## 1. 为什么需要 auto

先看一段 C++98 的代码，遍历一个 `map`：

```cpp
std::map<std::string, std::vector<int>> scores;
for (std::map<std::string, std::vector<int>>::iterator it = scores.begin();
     it != scores.end(); ++it) {
    // ...
}
```

迭代器的类型又长又难写，而编译器明明知道 `scores.begin()` 返回什么类型。C++11 的 `auto` 就是让编译器**根据初始值自动推导变量的类型**：

```cpp
for (auto it = scores.begin(); it != scores.end(); ++it) {
    // ...
}
```

`auto` 不是「动态类型」，变量的类型在**编译期**就确定了，之后不能改变。它只是让你少写字，和 Python、JavaScript 里的变量完全不同。

## 2. auto 的基本用法

```cpp
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main()
{
    auto a = 10;          // int
    auto b = 3.14;        // double
    auto c = 'x';         // char
    auto d = "hello";     // const char*
    auto e = std::string("world");   // std::string

    std::cout << a << ' ' << b << ' ' << c << ' ' << d << ' ' << e << '\n';

    std::map<std::string, std::vector<int>> scores = {
        {"Alice", {90, 85}},
        {"Bob",   {78, 92, 88}}
    };

    // C++98 写法
    for (std::map<std::string, std::vector<int>>::iterator it = scores.begin();
         it != scores.end(); ++it) {
        std::cout << it->first << " 有 " << it->second.size() << " 个成绩\n";
    }

    // C++11 写法
    for (auto it = scores.begin(); it != scores.end(); ++it) {
        std::cout << it->first << " 有 " << it->second.size() << " 个成绩\n";
    }
    return 0;
}
```

运行结果：

```
10 3.14 x hello world
Alice 有 2 个成绩
Bob 有 3 个成绩
Alice 有 2 个成绩
Bob 有 3 个成绩
```

使用 `auto` 有两条硬性规定：

1. **必须初始化**。`auto x;` 是错误的，编译器没有任何信息可以推导。
2. 一条声明里推导出的类型必须一致。`auto i = 1, j = 2.0;` 是错误的，`i` 推导为 `int`，`j` 推导为 `double`。

注意 `auto d = "hello";` 推导出来的是 `const char*`，不是 `std::string`。字符串字面量的类型是字符数组，赋值给 `auto` 时退化成了指针。

## 3. auto 的推导规则

这是 `auto` 最容易出错的地方。记住一句话：

> **`auto` 单独使用时，会丢掉初始值的引用和顶层 `const`；想保留，就自己加上 `&` 或 `const`。**

用 `static_assert` 加 `std::is_same` 可以在编译期验证推导结果，类型不对就编译失败。下面的代码能编译通过，说明注释里写的类型全部正确：

```cpp
#include <iostream>
#include <type_traits>

int main()
{
    const int ci = 10;
    int x = 1;
    int& rx = x;
    int arr[3] = {1, 2, 3};

    auto a1 = ci;      // 顶层 const 被丢弃      -> int
    auto a2 = rx;      // 引用被丢弃            -> int
    auto& a3 = ci;     // 加 & 后保留 const     -> const int&
    const auto a4 = x; // 自己加 const          -> const int
    auto* a5 = &x;     // 指针                  -> int*
    auto a6 = arr;     // 数组退化为指针        -> int*
    auto& a7 = arr;    // 引用不退化            -> int(&)[3]

    static_assert(std::is_same<decltype(a1), int>::value, "a1");
    static_assert(std::is_same<decltype(a2), int>::value, "a2");
    static_assert(std::is_same<decltype(a3), const int&>::value, "a3");
    static_assert(std::is_same<decltype(a4), const int>::value, "a4");
    static_assert(std::is_same<decltype(a5), int*>::value, "a5");
    static_assert(std::is_same<decltype(a6), int*>::value, "a6");
    static_assert(std::is_same<decltype(a7), int(&)[3]>::value, "a7");

    a2 = 100;                    // 修改的是副本
    std::cout << "x = " << x << '\n';   // x 仍然是 1
    std::cout << "所有类型检查通过\n";
    return 0;
}
```

运行结果：

```
x = 1
所有类型检查通过
```

`a2` 是 `rx` 的副本，而不是引用。修改 `a2` 不会影响 `x`。这正是初学者最常踩的坑：**以为 `auto` 推导出了引用，其实拷贝了一份**。

把规则整理成表格：

| 写法 | 初始值 `const int ci` | 初始值 `int& rx` | 说明 |
| --- | --- | --- | --- |
| `auto` | `int` | `int` | 拷贝一份，丢掉 const 和引用 |
| `auto&` | `const int&` | `int&` | 引用，保留 const |
| `const auto&` | `const int&` | `const int&` | 只读引用，最常用于遍历大对象 |
| `auto*` | — | — | 初始值必须是指针 |

> **什么是「顶层 const」？** 修饰变量本身的 const 叫顶层 const，例如 `const int ci`；修饰指针所指对象的 const 叫底层 const，例如 `const int* p` 中的 const。`auto` 只丢掉顶层 const，`auto p = &ci;` 推导出的是 `const int*`，底层 const 会保留，否则就能通过 `p` 修改常量了。

## 4. decltype：获取表达式的类型

`auto` 根据**初始值**推导类型，而 `decltype(表达式)` 直接得到**表达式的类型**，并且**不会真正计算这个表达式**。

```cpp
#include <iostream>
#include <type_traits>
#include <vector>

int func();               // 只需要声明，decltype 不会真的调用它

template <typename T, typename U>
auto add(T t, U u) -> decltype(t + u)   // 尾置返回类型
{
    return t + u;
}

int main()
{
    int x = 0;
    const int cx = 0;
    int& rx = x;
    std::vector<int> v{1, 2, 3};

    static_assert(std::is_same<decltype(x), int>::value, "");
    static_assert(std::is_same<decltype(cx), const int>::value, "");   // 保留 const
    static_assert(std::is_same<decltype(rx), int&>::value, "");        // 保留引用
    static_assert(std::is_same<decltype(func()), int>::value, "");
    static_assert(std::is_same<decltype(v[0]), int&>::value, "");      // operator[] 返回 int&
    static_assert(std::is_same<decltype((x)), int&>::value, "");       // 加括号变成左值表达式
    static_assert(std::is_same<decltype(x + 1), int>::value, "");      // 纯右值 -> int

    auto r = add(1, 2.5);
    static_assert(std::is_same<decltype(r), double>::value, "");
    std::cout << "add(1, 2.5) = " << r << '\n';
    return 0;
}
```

运行结果：

```
add(1, 2.5) = 3.5
```

和 `auto` 不同，**`decltype` 会原样保留 `const` 和引用**。

### 4.1 decltype 的推导规则

1. `decltype(变量名)`：得到变量**声明时的类型**，`const`、引用全部保留。
2. `decltype(表达式)`：
   - 表达式是**左值**，得到 `T&`。例如 `v[0]`、`*p`、`(x)`。
   - 表达式是**纯右值**，得到 `T`。例如 `x + 1`、`func()`。

第 2 条解释了一个奇怪的现象：`decltype(x)` 是 `int`，`decltype((x))` 却是 `int&`。多了一层括号，`(x)` 就不再是「变量名」，而是一个左值表达式。

> 左值和右值会在《右值引用与移动语义》中详细讲解。现在可以先简单理解为：**能取地址、有名字的是左值**，临时的、马上就消失的是右值。

### 4.2 尾置返回类型

函数模板 `add` 的返回类型取决于 `t + u` 的类型：`int + double` 是 `double`，`int + int` 是 `int`。可是下面这样写不行：

```cpp
template <typename T, typename U>
decltype(t + u) add(T t, U u);   // 错误：此时 t 和 u 还没有声明
```

C++11 的解决办法是**尾置返回类型**：前面写 `auto` 占位，真正的返回类型写在参数列表后面的 `->` 之后，这时参数已经可见了。

```cpp
template <typename T, typename U>
auto add(T t, U u) -> decltype(t + u);
```

> C++14 起可以省略 `-> decltype(...)`，直接写 `auto add(T t, U u)`，编译器会根据 `return` 语句推导返回类型。我们在 C++14 章节详细介绍。

## 5. 常见错误

### 5.1 auto 和花括号

```cpp
auto a = 1;       // int
auto b = {1};     // std::initializer_list<int>，不是 int！
auto c{1};        // int
```

`auto b = {1};` 推导出来的是 `std::initializer_list<int>`，这几乎从来不是你想要的。另外 `auto c{1};` 在最初的 C++11 标准里也是 `initializer_list`，后来标准修订（提案 N3922）改成了 `int`，GCC 5、Clang 3.8 之后的编译器都按新规则处理。**结论：用 `auto` 时就写 `=`，别用花括号。**

### 5.2 vector&lt;bool&gt; 的陷阱

```cpp
std::vector<bool> flags{true, false};
auto f = flags[0];   // f 的类型不是 bool！
f = false;           // 修改的是 flags[0]
std::cout << std::boolalpha << "flags[0] = " << flags[0] << '\n';
```

运行结果：

```
flags[0] = false
```

`std::vector<bool>` 为了省空间，每个元素只占 1 个比特，`operator[]` 返回的是一个代理对象 `std::vector<bool>::reference`，而不是 `bool&`。`auto` 推导出的就是这个代理类型，修改它会影响原容器。需要 `bool` 时，明确写出类型：`bool f = flags[0];`。

### 5.3 不要滥用 auto

`auto` 能少写字，但会让读代码的人看不出类型：

```cpp
auto result = process(data);   // result 是什么？需要去看 process 的声明
```

推荐的使用场景：

- 类型很长、很明显或不重要：迭代器、`std::make_shared<T>()` 的返回值、lambda
- 类型写错会出问题的地方：`auto n = v.size();` 一定是 `size_t`，不会写成 `int` 导致截断

不推荐：基本类型（`auto i = 0;` 不如 `int i = 0;` 清楚）、需要特定类型的地方（比如要 `long long` 却写了 `auto x = 1;`）。

## 6. 进阶：auto 推导的本质

`auto` 的推导规则和**函数模板参数推导**完全一样。编译器遇到

```cpp
auto x = expr;
const auto& y = expr;
```

时，相当于在推导这两个模板函数调用中的 `T`：

```cpp
template <typename T> void f(T param);          f(expr);   // auto x = expr
template <typename T> void g(const T& param);   g(expr);   // const auto& y = expr
```

按值传参时模板会丢掉引用和顶层 const，所以 `auto` 也会丢掉。理解了这一点，以后学习模板推导和完美转发会轻松很多。唯一的例外就是第 5.1 节的花括号：模板推导不认 `{1}`，`auto` 却会推导出 `initializer_list`。

另外 `decltype` 常常和 `std::declval` 配合，在不创建对象的情况下获得成员函数的返回类型，这是模板元编程的常用技巧：

```cpp
#include <utility>
// 不需要 Container 能默认构造，也能得到 begin() 的返回类型
template <typename Container>
using IteratorOf = decltype(std::declval<Container&>().begin());
```

## 7. 小结

| 关键字 | 根据什么推导 | const / 引用 | 典型用途 |
| --- | --- | --- | --- |
| `auto` | 初始值 | 丢掉，需要时自己加 `&`、`const` | 迭代器、复杂类型的变量 |
| `decltype(变量)` | 声明类型 | 原样保留 | 获取已有变量的类型 |
| `decltype(表达式)` | 值类别 | 左值得到 `T&` | 泛型代码、返回类型 |

## 8. 练习

1. 写出下面每个变量的类型：

   ```cpp
   const std::vector<int> v{1, 2, 3};
   auto a = v[0];
   auto& b = v[0];
   auto c = v.begin();
   decltype(v[0]) d = v[1];
   ```

2. 下面的代码想把 `names` 里的每个名字改成大写，为什么没有效果？怎么修改？

   ```cpp
   for (auto name : names) toUpper(name);
   ```

<details>
<summary>点击查看答案</summary>

1. `a` 是 `int`（拷贝，丢掉 const）；`b` 是 `const int&`；`c` 是 `std::vector<int>::const_iterator`（`v` 是 const，`begin()` 返回 const 迭代器）；`d` 是 `const int&`（const vector 的 `operator[]` 返回 `const int&`）。
2. `auto name` 拷贝了每个元素，修改的是副本。改成 `for (auto& name : names)`。

</details>
