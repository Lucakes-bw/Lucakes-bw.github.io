---
title: 折叠表达式与类模板参数推导
date: 2026-10-22 17:00:00
updated: 2026-10-22 17:00:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 模板
order: 6
description: 折叠表达式让参数包的处理不再需要递归和数组技巧，一行 (... + args) 就能求和；类模板参数推导（CTAD）让 std::pair p(1, 2.5)、std::vector v{1, 2, 3} 这样的写法成为可能。本文讲清楚四种折叠形式、空包的规则、推导指引，以及 CTAD 的几个意外结果。
disableNunjucks: true
---

> **本文目标**：会写四种折叠表达式；知道左折叠和右折叠的区别、空参数包时的行为；会用逗号折叠代替 C++11 的数组展开技巧；会用类模板参数推导省略模板参数；会写推导指引；知道 CTAD 的几个意外结果。
>
> **前置知识**：C++11 章节的《可变参数模板》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 折叠表达式

### 1.1 回顾：C++11 处理参数包的两种方法

C++11 章节《可变参数模板》中，处理参数包只有两种方法：

1. **递归**：每次剥离一个参数，需要单独的终点函数；
2. **数组初始化技巧**：`int dummy[] = {0, (f(args), 0)...};`，看起来很别扭。

C++17 的**折叠表达式**（fold expression）用一个二元运算符把参数包中的所有元素「折叠」成一个结果，一行代码就能完成。

### 1.2 四种形式

假设参数包 `args` 有三个元素 `a1, a2, a3`，`op` 是一个二元运算符：

| 名称 | 写法 | 展开结果 |
| --- | --- | --- |
| 一元左折叠 | `(... op args)` | `((a1 op a2) op a3)` |
| 一元右折叠 | `(args op ...)` | `(a1 op (a2 op a3))` |
| 二元左折叠 | `(init op ... op args)` | `(((init op a1) op a2) op a3)` |
| 二元右折叠 | `(args op ... op init)` | `(a1 op (a2 op (a3 op init)))` |

记忆方法：**`...` 在左边就从左边开始结合，在右边就从右边开始结合**。**外层的圆括号是语法的一部分，不能省略。**

支持几乎所有的二元运算符：`+ - * / % ^ & | = < > << >> += -= && || , .* ->*` 等。

```cpp
#include <iostream>
#include <string>
#include <vector>

template <typename... Args>
auto sum(Args... args) { return (... + args); }              // 一元左折叠：((a1 + a2) + a3) + ...

template <typename... Args>
auto sumOrZero(Args... args) { return (0 + ... + args); }     // 二元折叠：给定初始值，空包时返回 0

template <typename... Args>
void print(const Args&... args)
{
    ((std::cout << args << ' '), ...);                        // 逗号折叠：按顺序对每个参数执行
    std::cout << '\n';
}

template <typename... Args>
bool allPositive(Args... args) { return (... && (args > 0)); }   // 逻辑与折叠

template <typename T, typename... Args>
void pushAll(std::vector<T>& v, Args&&... args)
{
    (v.push_back(std::forward<Args>(args)), ...);
}

template <typename... Args> int leftSub(Args... args)  { return (... - args); }   // ((1 - 2) - 3)
template <typename... Args> int rightSub(Args... args) { return (args - ...); }   // (1 - (2 - 3))

int main()
{
    std::cout << "sum(1, 2, 3, 4) = " << sum(1, 2, 3, 4) << '\n';
    std::cout << "sum(1, 2.5) = " << sum(1, 2.5) << '\n';
    std::cout << "sum(string) = " << sum(std::string("a"), "b", "c") << '\n';
    std::cout << "sumOrZero() = " << sumOrZero() << '\n';
    print(1, "two", 3.0, 'x');
    std::cout << std::boolalpha << "allPositive(1, 2, 3) = " << allPositive(1, 2, 3)
              << ", allPositive(1, -2) = " << allPositive(1, -2)
              << ", allPositive() = " << allPositive() << '\n';
    std::vector<int> v;
    pushAll(v, 1, 2, 3);
    std::cout << "v.size() = " << v.size() << '\n';
    std::cout << "leftSub(1, 2, 3) = " << leftSub(1, 2, 3) << ", rightSub(1, 2, 3) = " << rightSub(1, 2, 3) << '\n';
    return 0;
}
```

运行结果：

```
sum(1, 2, 3, 4) = 10
sum(1, 2.5) = 3.5
sum(string) = abc
sumOrZero() = 0
1 two 3 x 
allPositive(1, 2, 3) = true, allPositive(1, -2) = false, allPositive() = true
v.size() = 3
leftSub(1, 2, 3) = -4, rightSub(1, 2, 3) = 2
```

逐个说明：

- **`sum`**：C++11 章节中，用递归实现的 `sum(1, 2.5)` 结果是 3，因为返回类型取决于第一个参数。折叠表达式直接计算 `1 + 2.5`，返回类型由表达式自然推导，结果是正确的 3.5。
- **`sum(std::string("a"), "b", "c")`**：左折叠展开为 `(string("a") + "b") + "c"`，每一步都是 `string + const char*`，没有问题。如果改成右折叠 `(args + ...)`，展开为 `string("a") + ("b" + "c")`，两个 `const char*` 相加无法编译。**所以左折叠和右折叠的选择，不只影响结果，还影响能否编译。**
- **`print`**：逗号运算符折叠 `(表达式, ...)` 会**从左到右依次**对每个参数执行表达式，完全取代了 C++11 的数组初始化技巧。模式是 `(std::cout << args << ' ')`，外面还要再套一层括号。
- **`allPositive`**：`&&` 折叠，模式可以是任意包含参数包的表达式，比如 `(args > 0)`。`&&` 和 `||` 折叠保留**短路求值**：遇到第一个 `false`，后面的就不再计算。
- **`pushAll`**：逗号折叠配合完美转发。
- **减法**：`leftSub` 是 `(1 - 2) - 3 = -4`，`rightSub` 是 `1 - (2 - 3) = 2`。对于不满足结合律的运算符，左右折叠的结果不同。

### 1.3 空参数包

参数包为空时，**一元折叠**只有三个运算符有定义：

| 运算符 | 空包时的值 |
| --- | --- |
| `&&` | `true` |
| `\|\|` | `false` |
| `,` | `void()` |

其他运算符的一元折叠遇到空包会编译失败：

```cpp
template <typename... Args>
auto sum(Args... args) { return (... + args); }
int main() { return sum(); }
```

```
error: unary fold expression has empty expansion for operator '+' with no fallback value
```

需要支持空包时，用**二元折叠**提供一个初始值，比如 `(0 + ... + args)`。上面的 `sumOrZero()` 返回 0，`allPositive()` 返回 `true`，就是这个原因。

### 1.4 和 C++11 写法的对比

```cpp
// C++11：借助数组初始化展开
template <typename... Args>
void print11(const Args&... args)
{
    int dummy[] = {0, ((std::cout << args << ' '), 0)...};
    (void)dummy;
    std::cout << '\n';
}
// C++17：折叠表达式
template <typename... Args>
void print17(const Args&... args)
{
    ((std::cout << args << ' '), ...);
    std::cout << '\n';
}
```

两者输出完全一样，C++17 的写法既简洁又直接表达了意图。配合上一篇的 `if constexpr`，C++17 中处理参数包几乎不再需要递归。

## 2. 类模板参数推导（CTAD）

### 2.1 问题：函数模板能推导，类模板却不能

C++11 中，调用函数模板时编译器能推导出模板参数，`std::max(1, 2)` 不需要写成 `std::max<int>(1, 2)`。但**构造类模板的对象时必须写出模板参数**：

```cpp
std::pair<int, double> p(1, 2.5);
std::lock_guard<std::mutex> lock(mtx);
```

为了偷懒，标准库提供了一堆 `make_xxx` 函数（`make_pair`、`make_tuple`），利用函数模板推导来间接地创建对象。

C++17 的**类模板参数推导**（Class Template Argument Deduction，CTAD）让编译器根据**构造函数的参数**推导类模板的模板参数：

```cpp
#include <array>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

template <typename T>
class Box {
public:
    explicit Box(T value) : value_(std::move(value)) {}
    const T& get() const { return value_; }
private:
    T value_;
};

// 推导指引：用 const char* 构造 Box 时，推导为 Box<std::string>
Box(const char*) -> Box<std::string>;

int main()
{
    std::pair p(1, 2.5);                         // pair<int, double>
    std::tuple t(1, 'a', std::string("s"));      // tuple<int, char, string>
    std::vector v{1, 2, 3};                      // vector<int>
    std::array a{1.0, 2.0, 3.0};                 // array<double, 3>
    std::map m{std::pair{1, std::string("one")}};   // map<int, string>
    std::mutex mtx;
    std::lock_guard lock(mtx);                   // lock_guard<std::mutex>

    Box b1(42);                                  // Box<int>
    Box b2("hello");                             // 推导指引生效：Box<std::string>

    static_assert(std::is_same_v<decltype(p), std::pair<int, double>>);
    static_assert(std::is_same_v<decltype(t), std::tuple<int, char, std::string>>);
    static_assert(std::is_same_v<decltype(v), std::vector<int>>);
    static_assert(std::is_same_v<decltype(a), std::array<double, 3>>);
    static_assert(std::is_same_v<decltype(m), std::map<int, std::string>>);
    static_assert(std::is_same_v<decltype(lock), std::lock_guard<std::mutex>>);
    static_assert(std::is_same_v<decltype(b1), Box<int>>);
    static_assert(std::is_same_v<decltype(b2), Box<std::string>>);
    std::cout << "所有推导结果检查通过, b2.get().size() = " << b2.get().size() << '\n';
    return 0;
}
```

运行结果：

```
所有推导结果检查通过, b2.get().size() = 5
```

所有 `static_assert` 都通过了，说明注释中的推导结果全部正确。另外注意：C++17 中 `static_assert` 的第二个参数（错误信息）可以省略。

- 标准库的 `pair`、`tuple`、`vector`、`array`、`map`、`lock_guard`、`unique_lock` 等都支持 CTAD。`std::array a{1.0, 2.0, 3.0}` 连元素个数 3 都推导出来了。
- 自己写的类模板**自动支持 CTAD**：编译器根据构造函数 `Box(T value)` 推导出 `Box b1(42)` 是 `Box<int>`。

### 2.2 推导指引

如果没有那一行推导指引，`Box b2("hello")` 会根据构造函数推导出 `Box<const char*>`，保存的是一个指针，而不是字符串。**推导指引**（deduction guide）用来告诉编译器「遇到这种参数时，应该推导成什么类型」：

```cpp
Box(const char*) -> Box<std::string>;
```

写法像一个带尾置返回类型的函数声明，但没有返回类型以外的函数体。推导指引也可以是模板，比如标准库为 `vector` 提供了这样一个推导指引，让「用一对迭代器构造」能推导出元素类型：

```cpp
template <typename InputIt>
vector(InputIt, InputIt) -> vector<typename std::iterator_traits<InputIt>::value_type>;
```

上一篇《std::variant》中的 `overloaded` 也用到了推导指引：`overloaded(Ts...) -> overloaded<Ts...>;`。

## 3. CTAD 的意外结果

CTAD 很方便，但有几种情况的推导结果可能出人意料：

```cpp
#include <iostream>
#include <type_traits>
#include <vector>
#include <utility>
int main()
{
    std::vector v1{1, 2, 3};
    std::vector v2{v1};                 // 是 vector<int> 的拷贝，还是 vector<vector<int>>？
    std::vector v3{v1, v1};             // 两个元素呢？
    std::pair p("text", 1);             // 第一个元素的类型是？
    static_assert(std::is_same_v<decltype(v2), std::vector<int>>);
    static_assert(std::is_same_v<decltype(v3), std::vector<std::vector<int>>>);
    static_assert(std::is_same_v<decltype(p), std::pair<const char*, int>>);
    std::cout << "v2.size() = " << v2.size() << ", v3.size() = " << v3.size() << '\n';
    return 0;
}
```

运行结果：

```
v2.size() = 3, v3.size() = 2
```

1. **`std::vector v2{v1}` 是拷贝**：花括号里只有一个同类型的 vector 时，编译器优先认为你是在拷贝它，结果是 `vector<int>`。而 `std::vector v3{v1, v1}` 有两个元素，推导为 `vector<vector<int>>`。同样是花括号，元素个数不同，结果的类型完全不同。
2. **`std::pair p("text", 1)` 的第一个元素是 `const char*`**，不是 `std::string`。如果需要 `string`，写 `std::pair p(std::string("text"), 1)`，或者用 C++14 的 `"text"s`。

还有两种情况 CTAD 不起作用：

```cpp
std::pair<int> p(1, 2);                     // 错误：不能只写一部分模板参数
std::unique_ptr up(new int(1));             // 错误：unique_ptr 故意不支持从指针推导
```

```
error: too few template arguments for class template 'pair'
error: no viable constructor or deduction guide for deduction of template arguments of 'std::unique_ptr'
```

- **要么全部推导，要么全部写出**，不能只指定一部分模板参数。
- **`std::unique_ptr` 和 `std::shared_ptr` 故意不支持从裸指针推导**，因为编译器无法区分 `new int` 和 `new int[10]`，推导成 `unique_ptr<int>` 去管理数组会导致错误的释放。继续用 `std::make_unique`。

## 4. 常见错误

1. **折叠表达式漏掉外层括号**：`return ... + args;` 是语法错误，必须写成 `return (... + args);`。
2. **对可能为空的包使用一元折叠**：除了 `&&`、`||`、`,` 之外都会编译失败，用二元折叠提供初始值。
3. **以为 CTAD 会把字符串字面量推导成 `std::string`**：它推导出的是 `const char*`。
4. **在需要明确类型的地方滥用 CTAD**：`std::vector v{someValue}` 的类型取决于 `someValue` 是不是 vector，代码的含义会随着上下文变化。类型不明显时，写出模板参数更清晰。

## 5. 进阶：用折叠表达式做编译期计算

折叠表达式同样可以用于类型特征，C++11 章节中需要递归特化的 `AllIntegral`，现在一行就能写完：

```cpp
template <typename... Ts>
constexpr bool allIntegral = (std::is_integral_v<Ts> && ...);

template <typename T, typename... Ts>
constexpr bool contains = (std::is_same_v<T, Ts> || ...);

static_assert(allIntegral<int, long, char>);
static_assert(!allIntegral<int, double>);
static_assert(contains<int, char, int>);
```

结合 C++14 的变量模板、C++17 的 `_v` 简写和折叠表达式，编译期的类型计算变得和写普通的布尔表达式一样简单。

## 6. 小结

- 折叠表达式：`(... op args)`、`(args op ...)`、`(init op ... op args)`、`(args op ... op init)`，外层括号不能省略
- `...` 在左边就从左开始结合；对减法、字符串拼接等，左右折叠的结果甚至能否编译都不同
- 空包时只有 `&&`（true）、`||`（false）、`,`（void）的一元折叠合法，其他情况用二元折叠提供初始值
- 逗号折叠 `(f(args), ...)` 按顺序处理每个参数，取代了 C++11 的数组技巧
- CTAD：`std::pair p(1, 2.5)`、`std::vector v{1, 2, 3}`、`std::lock_guard lock(m)`，编译器根据构造参数推导模板参数
- 推导指引 `Box(const char*) -> Box<std::string>;` 自定义推导规则
- 注意：`vector v{v1}` 是拷贝；字符串字面量推导为 `const char*`；不能只写一部分模板参数；智能指针不支持从裸指针推导

## 7. 练习

1. 用折叠表达式写一个函数 `average(args...)`，返回所有参数的平均值（`double`）。
2. 用折叠表达式写一个函数 `printWithSep(sep, args...)`，参数之间用 `sep` 分隔，例如 `printWithSep(", ", 1, 2, 3)` 输出 `1, 2, 3`（最后没有多余的分隔符）。
3. 给下面的类模板写一个推导指引，使 `Range r(v.begin(), v.end());` 推导出 `Range<int>`（假设 `v` 是 `vector<int>`）。

   ```cpp
   template <typename T>
   struct Range {
       template <typename It>
       Range(It first, It last) : data(first, last) {}
       std::vector<T> data;
   };
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   template <typename... Args>
   double average(Args... args)
   {
       static_assert(sizeof...(args) > 0, "至少需要一个参数");
       return (0.0 + ... + args) / sizeof...(args);
   }
   ```

   初始值写成 `0.0`，保证整个求和以 `double` 进行。

2. 参考代码，用一个计数器判断是否是第一个参数：

   ```cpp
   template <typename... Args>
   void printWithSep(const char* sep, const Args&... args)
   {
       int i = 0;
       ((std::cout << (i++ ? sep : "") << args), ...);
       std::cout << '\n';
   }
   ```

   逗号折叠保证从左到右执行，所以 `i++` 的顺序是确定的。

3. 构造函数的参数是迭代器 `It`，编译器无法从中推导出 `T`，需要推导指引：

   ```cpp
   template <typename It>
   Range(It, It) -> Range<typename std::iterator_traits<It>::value_type>;
   ```

</details>
