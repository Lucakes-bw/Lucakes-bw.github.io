---
title: std::variant：类型安全的联合体
date: 2026-10-25 13:00:00
updated: 2026-10-25 13:00:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 标准库
order: 4
description: std::variant 在同一时刻保存若干种类型中的某一种，并且始终知道当前是哪一种。本文讲清楚它的基本操作、用 std::visit 处理每种情况、overloaded 技巧、用 variant 返回「结果或错误」，以及它和继承多态的取舍。
disableNunjucks: true
---

> **本文目标**：会创建、赋值和访问 `std::variant`；会用 `std::visit` 配合泛型 lambda 或 `overloaded` 处理所有可能的类型；会用 `variant` 返回「结果或错误」；了解 `variant` 的内存布局；能在 `variant` 和虚函数之间做出选择。
>
> **前置知识**：本章《if/switch 初始化语句与 if constexpr》，C++14 章节《泛型 lambda》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`，需要包含头文件 `<variant>`。

## 1. 问题：C 语言的 union 不安全

有时一个变量需要在不同时刻保存不同类型的值，比如 JSON 中的一个值可能是数字、字符串或布尔值。C 语言的 `union` 可以做到，但它有两个严重的问题：

1. **不知道当前存的是哪种类型**。存入 `double`、按 `int` 读出，编译器不会阻止，结果是未定义行为。只能自己额外用一个变量记录类型，并时刻保持两者同步。
2. **不能存放有构造函数、析构函数的类型**。`union { int i; std::string s; };` 中，`string` 什么时候构造、什么时候析构，`union` 并不知道，需要手动用定位 `new` 和显式析构来管理，非常容易出错。

C++17 的 **`std::variant<T1, T2, ...>`** 是**类型安全的联合体**：它在任一时刻保存其中一种类型的值，**自己记录当前是哪一种**，并且会正确地构造和析构对象。

## 2. 基本用法

```cpp
#include <iostream>
#include <string>
#include <variant>

int main()
{
    std::variant<int, double, std::string> v;     // 默认保存第一个类型 int，值为 0
    std::cout << "index = " << v.index() << ", 值 = " << std::get<int>(v) << '\n';

    v = 3.14;                                       // 现在保存 double
    std::cout << "index = " << v.index() << ", 值 = " << std::get<double>(v) << '\n';

    v = std::string("hello");                       // 现在保存 string，之前的 double 被销毁
    std::cout << "index = " << v.index() << ", 值 = " << std::get<2>(v) << '\n';   // 也可以按下标取

    std::cout << std::boolalpha
              << "holds string: " << std::holds_alternative<std::string>(v)
              << ", holds int: " << std::holds_alternative<int>(v) << '\n';

    // get_if：类型不对时返回 nullptr，不抛异常
    if (auto p = std::get_if<std::string>(&v))
        std::cout << "get_if 成功, 长度 " << p->size() << '\n';
    if (std::get_if<int>(&v) == nullptr)
        std::cout << "get_if<int> 返回 nullptr\n";

    // get：类型不对时抛出异常
    try {
        std::cout << std::get<int>(v) << '\n';
    } catch (const std::bad_variant_access& e) {
        std::cout << "异常: " << e.what() << '\n';
    }
    return 0;
}
```

运行结果：

```
index = 0, 值 = 0
index = 1, 值 = 3.14
index = 2, 值 = hello
holds string: true, holds int: false
get_if 成功, 长度 5
get_if<int> 返回 nullptr
异常: bad_variant_access
```

| 操作 | 作用 |
| --- | --- |
| `v = 值;` | 存入新值，类型由值决定；原来的值被正确地析构 |
| `v.index()` | 当前保存的是第几个类型（从 0 开始） |
| `std::holds_alternative<T>(v)` | 当前是否保存着 `T` |
| `std::get<T>(v)` / `std::get<I>(v)` | 按类型或下标取值，**类型不对时抛出** `std::bad_variant_access` |
| `std::get_if<T>(&v)` | 类型对时返回指向值的指针，否则返回 `nullptr`（注意参数是**指针**） |
| `v.emplace<T>(参数...)` | 原地构造一个 `T` |

默认构造的 variant 保存的是**第一个类型**的默认值，所以第一个类型必须能默认构造（见第 6.2 节）。

## 3. std::visit：处理每一种情况

用 `index()` 加 `switch` 或者一连串 `get_if` 来判断类型，写起来很繁琐，而且新增一种类型时容易漏掉处理。**`std::visit`** 接收一个「访问者」（可调用对象）和一个 variant，根据 variant 当前的类型，**用对应类型的值调用访问者**。

```cpp
#include <cmath>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

struct Circle { double r; };
struct Rect { double w, h; };
struct Triangle { double a, b, c; };

using Shape = std::variant<Circle, Rect, Triangle>;

// 小技巧：把多个 lambda 合并成一个重载集合
template <typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template <typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

int main()
{
    std::vector<Shape> shapes{Circle{1.0}, Rect{2, 3}, Triangle{3, 4, 5}};

    // 方式 1：一个泛型 lambda + if constexpr
    for (const auto& s : shapes) {
        double area = std::visit([](const auto& shape) -> double {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, Circle>) return 3.14159 * shape.r * shape.r;
            else if constexpr (std::is_same_v<T, Rect>) return shape.w * shape.h;
            else {
                double p = (shape.a + shape.b + shape.c) / 2;
                return std::sqrt(p * (p - shape.a) * (p - shape.b) * (p - shape.c));
            }
        }, s);
        std::cout << "面积 " << area << '\n';
    }

    // 方式 2：overloaded，每种类型一个 lambda
    for (const auto& s : shapes) {
        std::string name = std::visit(overloaded{
            [](const Circle& c)   { return "圆，半径 " + std::to_string(c.r); },
            [](const Rect& r)     { return "矩形 " + std::to_string(r.w) + " x " + std::to_string(r.h); },
            [](const Triangle&)   { return std::string("三角形"); },
        }, s);
        std::cout << name << '\n';
    }
    return 0;
}
```

运行结果：

```
面积 3.14159
面积 6
面积 6
圆，半径 1.000000
矩形 2.000000 x 3.000000
三角形
```

### 3.1 方式 1：泛型 lambda + if constexpr

访问者是一个泛型 lambda，`std::visit` 会用三种类型分别实例化它。在 lambda 内部用 `std::decay_t<decltype(shape)>` 得到去掉引用和 const 的真实类型，再用 `if constexpr` 分别处理。适合多种类型有共同处理逻辑、只有少数需要特殊处理的情况。

### 3.2 方式 2：overloaded

`overloaded` 是一个广为流传的小技巧，几行代码把多个 lambda 合并成一个拥有多个 `operator()` 重载的对象：

```cpp
template <typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };   // 继承所有 lambda，并引入它们的 operator()
template <typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;                   // 推导指引：从构造参数推导模板参数
```

- `struct overloaded : Ts...`：**同时继承**所有传进来的 lambda（lambda 是类，可以被继承）。
- `using Ts::operator()...;`：把每个基类的 `operator()` 都引入当前作用域，让它们形成一组重载。这种「`using` 声明的包展开」是 C++17 的新语法。
- 第二行是**推导指引**，让 `overloaded{lambda1, lambda2}` 能自动推导出模板参数。它属于「类模板参数推导」，在本章后面的文章中介绍。C++20 中这一行可以省略。

`std::visit` 调用 `overloaded` 对象时，编译器按照普通的重载决议规则，为每种类型挑选最匹配的那个 lambda。每种类型一个 lambda，清晰直观。

### 3.3 编译器帮你检查是否处理了所有类型

如果漏掉了某种类型：

```cpp
return std::visit(overloaded{
    [](const Circle&) { return 1; },
    [](const Rect&)   { return 2; },
}, s);                         // 漏掉了 Triangle
```

libc++ 的报错非常直白：

```
error: static assertion failed due to requirement 'is_invocable_v<overloaded<...>, Triangle &>': `std::visit` requires the visitor to be exhaustive.
```

这正是 variant 的一大优势：以后给 `Shape` 增加一种新类型，**所有没有处理它的 `visit` 都会编译失败**，不会有遗漏。

## 4. 用 variant 返回「结果或错误」

上一篇提到，`std::optional` 只能表示「没有值」，不能说明原因。用 `variant<结果, 错误>` 可以同时携带两者：

```cpp
#include <iostream>
#include <string>
#include <variant>

struct ParseError {
    std::size_t position;
    std::string message;
};

// 成功时返回 int，失败时返回带有原因的 ParseError
std::variant<int, ParseError> parseInt(const std::string& s)
{
    if (s.empty()) return ParseError{0, "空字符串"};
    int result = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return ParseError{i, std::string("非法字符 '") + s[i] + "'"};
        result = result * 10 + (s[i] - '0');
    }
    return result;
}

int main()
{
    for (const char* s : {"2024", "12x4", ""}) {
        auto r = parseInt(s);
        if (auto value = std::get_if<int>(&r))
            std::cout << '"' << s << "\" -> " << *value << '\n';
        else {
            const auto& err = std::get<ParseError>(r);
            std::cout << '"' << s << "\" -> 第 " << err.position << " 个字符出错: " << err.message << '\n';
        }
    }
    return 0;
}
```

运行结果：

```
"2024" -> 2024
"12x4" -> 第 2 个字符出错: 非法字符 'x'
"" -> 第 0 个字符出错: 空字符串
```

C++23 提供了专门为此设计的 `std::expected<T, E>`，接口更方便（比如 `if (r)`、`*r`、`r.error()`）。在 C++17 中，`variant<T, Error>` 是一个不错的替代。

## 5. 内存布局

variant **不会动态分配内存**，它的大小约等于**最大的那个类型的大小，加上记录当前类型的索引**，再按对齐要求补齐：

```cpp
#include <cstdio>
#include <string>
#include <variant>
int main()
{
    std::printf("variant<int, double>         : %zu\n", sizeof(std::variant<int, double>));
    std::printf("variant<int, std::string>    : %zu\n", sizeof(std::variant<int, std::string>));
    std::printf("variant<char, int, double, std::string> : %zu\n", sizeof(std::variant<char, int, double, std::string>));
    return 0;
}
```

运行结果（64 位，libc++）：

```
variant<int, double>         : 16
variant<int, std::string>    : 32
variant<char, int, double, std::string> : 32
```

`std::string` 在 libc++ 中是 24 字节，加上索引并对齐到 8 字节，就是 32 字节。增加 `char`、`int`、`double` 这些更小的类型不会让 variant 变大。

## 6. 常见错误

### 6.1 用 get 而不检查类型

`std::get<T>(v)` 在类型不对时抛出异常。不确定类型时，用 `get_if` 或 `visit`。

### 6.2 第一个类型不能默认构造

```cpp
#include <iostream>
#include <variant>
struct NoDefault { explicit NoDefault(int v) : v(v) {} int v; };
int main()
{
    // std::variant<NoDefault, int> a;                 // 错误：第一个类型不能默认构造
    std::variant<std::monostate, NoDefault, int> b;    // monostate 作为“空”状态
    std::cout << "b 为空: " << std::holds_alternative<std::monostate>(b) << '\n';
    b = NoDefault(5);
    std::cout << "b.index() = " << b.index() << '\n';
    return 0;
}
```

运行结果：

```
b 为空: 1
b.index() = 1
```

默认构造的 variant 保存第一个类型的默认值。第一个类型没有默认构造函数时，可以把 **`std::monostate`** 放在第一位，它是一个专门表示「空」的类型。这样 variant 也就有了一个「还没有值」的状态。

### 6.3 字符串字面量与 bool

```cpp
std::variant<std::string, bool> v = "hello";      // 想存字符串
```

字符串字面量是 `const char*`，而**指针到 `bool` 的转换是标准转换，比到 `std::string` 的用户定义转换优先级更高**。在 C++17 最初的规则下，`v` 保存的竟然是 `bool`（值为 `true`）！

这个问题被认为是标准的缺陷，C++20 的提案 P0608 修改了 variant 的转换规则（禁止窄化转换和到 `bool` 的转换参与选择），并作为缺陷报告追溯应用到 C++17。本文使用的 Clang 21 + libc++ 已经采用了新规则，上面的代码保存的是 `string`（`index()` 为 0）。但在较旧的标准库中（如 GCC 9 及更早的 libstdc++），结果仍然是 `bool`。为了可移植，**往这类 variant 中存字符串时，明确写出 `std::string("hello")`**。

## 7. 进阶：variant 还是虚函数？

本文的 `Shape` 例子也可以用传统的继承 + 虚函数来实现：

```cpp
struct Shape { virtual ~Shape() = default; virtual double area() const = 0; };
struct Circle : Shape { double r; double area() const override { return 3.14159 * r * r; } };
// ...
std::vector<std::unique_ptr<Shape>> shapes;
```

两种方式各有所长，适合不同的场景：

| | `std::variant` | 继承 + 虚函数 |
| --- | --- | --- |
| 类型集合 | **封闭**：所有类型在定义 variant 时就确定 | **开放**：随时可以增加新的派生类 |
| 操作集合 | **开放**：随时可以写一个新的 `visit` | 封闭：增加操作要修改基类，所有派生类都要实现 |
| 内存 | 值语义，可以直接放进 `vector`，**不需要堆分配** | 通常需要 `unique_ptr`，每个对象一次堆分配 |
| 遗漏检查 | `visit` 漏掉类型会编译失败 | 纯虚函数保证每个派生类都实现 |
| 适合 | 类型种类固定、操作经常变化：语法树节点、消息类型、JSON 值、状态机的状态 | 类型种类经常扩展：插件、GUI 控件、第三方可以扩展的接口 |

简单地说：**类型固定、操作多变，用 variant；类型多变、操作固定，用虚函数。**

另外，`std::visit` 也可以同时访问**多个** variant：`std::visit(f, v1, v2)` 会用 `v1` 和 `v2` 当前的类型组合调用 `f`，这在处理「两种对象碰撞」这类需要根据两个对象的类型共同决定行为的场合（双重分派）非常方便。

## 8. 小结

- `std::variant<T1, T2, ...>` 在任一时刻保存其中一种类型的值，自己记录当前类型，并正确地构造和析构对象
- `index()`、`holds_alternative<T>` 查询类型；`get<T>` 取值（类型不对抛异常）；`get_if<T>(&v)` 取值（类型不对返回 `nullptr`）
- `std::visit(访问者, v)` 根据当前类型调用访问者；用**泛型 lambda + if constexpr** 或 **overloaded** 编写访问者；漏掉类型会编译失败
- `variant<结果, 错误>` 可以返回带原因的错误（C++23 有专门的 `std::expected`）
- 大小约为最大的类型加一个索引，不分配堆内存
- 第一个类型不能默认构造时，用 `std::monostate` 占位
- 类型固定、操作多变时用 variant，类型经常扩展时用虚函数

## 9. 练习

1. 定义 `using Json = std::variant<std::nullptr_t, bool, double, std::string>;`，写一个函数 `std::string toString(const Json& j)`，用 `std::visit` + `overloaded` 把它转成 JSON 文本（字符串要加引号，`nullptr` 输出 `null`）。
2. 用 variant 实现一个简单的交通灯状态机：状态有 `Red{剩余秒数}`、`Green{剩余秒数}`、`Yellow{剩余秒数}`，写一个 `next(state)` 函数返回下一个状态（红 → 绿 → 黄 → 红）。
3. 下面的代码输出什么？

   ```cpp
   std::variant<int, std::string> v = 42;
   v = "abc";
   std::cout << v.index() << ' ' << std::holds_alternative<int>(v) << '\n';
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::string toString(const Json& j)
   {
       return std::visit(overloaded{
           [](std::nullptr_t)          { return std::string("null"); },
           [](bool b)                  { return std::string(b ? "true" : "false"); },
           [](double d)                { return std::to_string(d); },
           [](const std::string& s)    { return "\"" + s + "\""; },
       }, j);
   }
   ```

2. 参考代码：

   ```cpp
   struct Red { int seconds; };
   struct Green { int seconds; };
   struct Yellow { int seconds; };
   using Light = std::variant<Red, Green, Yellow>;

   Light next(const Light& l)
   {
       return std::visit(overloaded{
           [](const Red&)    -> Light { return Green{30}; },
           [](const Green&)  -> Light { return Yellow{3}; },
           [](const Yellow&) -> Light { return Red{20}; },
       }, l);
   }
   ```

   每个 lambda 返回的类型不同，所以要用 `-> Light` 统一返回类型，否则 `std::visit` 会因为各个分支的返回类型不一致而编译失败。

3. 输出 `1 0`。`"abc"` 不能转换成 `int`，只能转换成 `std::string`，所以 `v` 现在保存 `string`，`index()` 为 1。

</details>
