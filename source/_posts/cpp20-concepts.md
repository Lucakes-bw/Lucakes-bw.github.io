---
title: Concepts：给模板参数加上约束
date: 2026-10-26 13:00:00
updated: 2026-10-26 13:00:00
categories:
  - C/C++
  - C++20
tags:
  - C++20
  - 模板
  - Concepts
order: 1
description: 模板一旦用错，编译器就会吐出上百行难以理解的错误。C++20 的 Concepts 让你在模板参数上写明「需要什么样的类型」，错误信息直接告诉你哪个条件没满足。本文讲清楚四种写法、标准库概念、用 requires 表达式自定义概念，以及按约束重载。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 Concepts 解决了什么问题；会用四种写法给模板加约束；认识常用的标准库概念；会用 `requires` 表达式定义自己的概念；会根据约束重载函数；能用 Concepts 代替 `enable_if`。
>
> **前置知识**：函数模板，C++17 章节的《if/switch 初始化语句与 if constexpr》中关于 SFINAE 的部分。
>
> **编译环境**：`g++ -std=c++20` / `clang++ -std=c++20`，GCC 10+ / Clang 10+ / VS2019 16.3+，需要包含头文件 `<concepts>`。

## 1. C++20 概述

C++20 是继 C++11 之后**变化最大**的一个版本，其中有四个重量级特性：Concepts、Ranges、协程、模块。本章 7 篇文章：

| 文章 | 内容 |
| --- | --- |
| 本篇 | Concepts：给模板参数加约束 |
| Ranges | 管道式的组合算法，惰性求值的视图 |
| 三路比较运算符 | `<=>` 一次定义所有比较运算 |
| 协程入门 | 可以暂停和恢复的函数，生成器 |
| 模块 | 代替 `#include` 的新的代码组织方式 |
| consteval 与 constinit | 立即函数、编译期初始化，以及 `constexpr` 的进一步增强 |
| span、format、jthread 等 | 常用的新标准库组件 |

## 2. 问题：模板的错误信息太长了

`std::list` 的迭代器不支持随机访问，不能用 `std::sort` 排序。看看用错时会发生什么：

```cpp
#include <algorithm>
#include <list>
int main()
{
    std::list<int> l{3, 1, 2};
    std::sort(l.begin(), l.end());          // list 的迭代器不支持随机访问
}
```

Clang 输出了 **138 行**错误信息，前几条是：

```
make_heap.h:37:31: error: invalid operands to binary expression ('std::__list_iterator<int, void *>' and 'std::__list_iterator<int, void *>')
partial_sort.h:43:85: error: invalid operands to binary expression (...)
sort_heap.h:39:37: error: invalid operands to binary expression (...)
```

错误发生在标准库内部的 `make_heap.h`、`partial_sort.h` 等文件中，说的是「两个迭代器不能相减」。初学者很难从中看出真正的原因：`list` 根本不能用 `std::sort`。

问题的根源在于：C++20 之前，**模板参数没有任何约束**，`template <typename It>` 接受任何类型。编译器只能先把模板实例化，在函数体内部某处出错时才报告，错误信息自然指向模板的实现细节。

换成 C++20 中带约束的 `std::ranges::sort`：

```cpp
std::ranges::sort(l);                   // C++20：带约束的版本
```

错误信息只有 **22 行**，而且直接说明了原因：

```
error: no matching function for call to object of type 'const __sort'
note: candidate template ignored: constraints not satisfied [with _Range = std::list<int> &, ...]
note: because 'std::list<int> &' does not satisfy 'random_access_range'
note: because 'iterator_t<std::list<int> &>' does not satisfy 'random_access_iterator'
```

**「`std::list<int>&` 不满足 `random_access_range`」**，一句话就说清楚了。这就是 **Concepts**（概念）的作用：在模板的**接口**上写明对类型的要求，编译器在调用处就检查，不满足时直接报告哪个要求没满足。

## 3. 使用概念的四种写法

```cpp
#include <concepts>
#include <iostream>
#include <string>

// 写法 1：requires 子句
template <typename T>
    requires std::integral<T>
T gcd1(T a, T b) { return b == 0 ? a : gcd1(b, a % b); }

// 写法 2：用概念代替 typename
template <std::integral T>
T gcd2(T a, T b) { return b == 0 ? a : gcd2(b, a % b); }

// 写法 3：简写函数模板（参数直接写 概念 auto）
auto gcd3(std::integral auto a, std::integral auto b)
{
    while (b != 0) { auto t = a % b; a = b; b = t; }
    return a;
}

// 写法 4：尾置 requires
template <typename T>
T gcd4(T a, T b) requires std::integral<T> { return b == 0 ? a : gcd4(b, a % b); }

int main()
{
    std::cout << gcd1(48, 18) << ' ' << gcd2(48L, 18L) << ' ' << gcd3(48, 18) << ' ' << gcd4(48u, 18u) << '\n';
    return 0;
}
```

运行结果：

```
6 6 6 6
```

四种写法的效果相同，`std::integral<T>` 是标准库提供的概念，要求 `T` 是整数类型。最常用的是写法 2 和写法 3：

- **写法 2** `template <std::integral T>`：把 `typename` 换成概念名，最直观。
- **写法 3** `std::integral auto a`：C++20 允许普通函数的参数写 `auto`（之前只有 lambda 可以），这样的函数自动成为函数模板，叫做**简写函数模板**。前面加上概念名就是带约束的版本。注意：两个 `auto` 参数是**两个独立的模板参数**，`gcd3(48, 18L)` 也能调用。
- **写法 1 和写法 4** 的 `requires` 子句后面可以写任意的编译期布尔表达式，适合组合多个条件，如 `requires std::integral<T> && (sizeof(T) >= 4)`。

传入不满足约束的类型：

```cpp
gcd2(4.5, 1.5);
```

```
error: no matching function for call to 'gcd2'
note: candidate template ignored: constraints not satisfied [with T = double]
note: because 'double' does not satisfy 'integral'
note: because 'is_integral_v<double>' evaluated to false
```

## 4. 常用的标准库概念

`<concepts>` 和其他头文件中定义了大量现成的概念：

| 类别 | 概念 | 含义 |
| --- | --- | --- |
| 类型关系 | `std::same_as<T, U>` | `T` 和 `U` 是同一类型 |
| | `std::derived_from<D, B>` | `D` 公有继承自 `B` |
| | `std::convertible_to<From, To>` | `From` 能隐式转换为 `To` |
| 数值 | `std::integral<T>` | 整数类型 |
| | `std::signed_integral<T>` / `std::unsigned_integral<T>` | 有符号 / 无符号整数 |
| | `std::floating_point<T>` | 浮点类型 |
| 比较 | `std::equality_comparable<T>` | 支持 `==` 和 `!=` |
| | `std::totally_ordered<T>` | 支持 `<`、`>`、`<=`、`>=` |
| 对象 | `std::copyable<T>` / `std::movable<T>` | 可拷贝 / 可移动 |
| | `std::default_initializable<T>` | 可以默认构造 |
| 可调用 | `std::invocable<F, Args...>` | `F` 可以用 `Args...` 调用 |
| | `std::predicate<F, Args...>` | 可调用，并返回能当作 `bool` 的值 |
| 迭代器（`<iterator>`） | `std::input_iterator`、`std::forward_iterator`、`std::random_access_iterator` 等 | 迭代器的各个类别 |
| 范围（`<ranges>`） | `std::ranges::range`、`std::ranges::random_access_range` 等 | 可以遍历的范围 |

## 5. 自定义概念

用 `concept` 关键字定义自己的概念，最常见的写法是配合 **`requires` 表达式**，列出类型必须支持的操作：

```cpp
#include <concepts>
#include <iostream>
#include <string>
#include <vector>

// 自定义概念：能计算面积、能取名字
template <typename T>
concept Shape = requires(const T& s) {
    { s.area() } -> std::convertible_to<double>;    // 有 area()，返回值能转成 double
    { s.name() } -> std::convertible_to<std::string>;
};

// 自定义概念：能用 << 输出
template <typename T>
concept Printable = requires(std::ostream& os, const T& v) {
    os << v;
};

struct Circle {
    double r;
    double area() const { return 3.14159 * r * r; }
    std::string name() const { return "圆"; }
};
struct Square {
    double a;
    double area() const { return a * a; }
    std::string name() const { return "正方形"; }
};
struct Point { int x, y; };          // 不满足 Shape

void describe(const Shape auto& s)
{
    std::cout << s.name() << " 的面积是 " << s.area() << '\n';
}

template <Printable... Ts>
void printAll(const Ts&... args) { ((std::cout << args << ' '), ...); std::cout << '\n'; }

int main()
{
    describe(Circle{1});
    describe(Square{2});
    static_assert(Shape<Circle> && Shape<Square> && !Shape<Point>);
    static_assert(Printable<int> && Printable<std::string> && !Printable<Point>);
    printAll(1, "two", 3.0);
    return 0;
}
```

运行结果：

```
圆 的面积是 3.14159
正方形 的面积是 4
1 two 3 
```

### 5.1 requires 表达式

`requires (参数列表) { 要求... }` 是一个编译期的布尔表达式：花括号中的每一条要求都满足时为 `true`。参数列表中的变量只是用来描述要求的「假想对象」，不会真的被创建。要求有四种：

| 种类 | 写法 | 含义 |
| --- | --- | --- |
| 简单要求 | `os << v;` | 这个表达式能通过编译 |
| 类型要求 | `typename T::value_type;` | 这个类型存在 |
| 复合要求 | `{ s.area() } -> std::convertible_to<double>;` | 表达式能编译，并且结果的类型满足后面的概念 |
| 嵌套要求 | `requires sizeof(T) <= 8;` | 一个编译期布尔条件为真 |

### 5.2 不满足时的错误信息

```cpp
describe(Point{1, 2});
```

```
error: no matching function for call to 'describe'
note: candidate template ignored: constraints not satisfied [with s:auto = Point]
note: because 'Point' does not satisfy 'Shape'
note: because 's.area()' would be invalid: no member named 'area' in 'Point'
```

错误信息一路追溯到具体原因：`Point` 没有 `area` 成员。

### 5.3 「鸭子类型」，不需要继承

`Circle` 和 `Square` 没有继承任何基类，只要它们**有** `area()` 和 `name()`，就满足 `Shape` 概念。这和虚函数接口不同：虚函数要求类型显式继承接口类，概念只看类型「能做什么」。而且一切都在编译期完成，没有虚函数调用的开销。

## 6. 按约束重载

多个重载都满足时，编译器会选择**约束更具体**的那一个：

```cpp
#include <concepts>
#include <iostream>
#include <list>
#include <vector>
#include <iterator>

// 按约束重载：越具体的约束越优先
void info(std::integral auto)        { std::cout << "整数\n"; }
void info(std::signed_integral auto) { std::cout << "有符号整数（更具体）\n"; }
void info(std::floating_point auto)  { std::cout << "浮点数\n"; }

// 根据迭代器能力选择不同的实现
template <std::forward_iterator It>
void advanceBy(It& it, int n) { std::cout << "  逐个前进 " << n << " 步\n"; while (n--) ++it; }

template <std::random_access_iterator It>
void advanceBy(It& it, int n) { std::cout << "  直接跳 " << n << " 步\n"; it += n; }

int main()
{
    info(42);        // int 同时满足 integral 和 signed_integral，选更具体的
    info(42u);       // unsigned 只满足 integral
    info(3.14);

    std::vector<int> v{1, 2, 3, 4, 5};
    std::list<int> l{1, 2, 3, 4, 5};
    auto vi = v.begin();
    auto li = l.begin();
    std::cout << "vector:\n"; advanceBy(vi, 3);
    std::cout << "list:\n";   advanceBy(li, 3);
    std::cout << *vi << ' ' << *li << '\n';
    return 0;
}
```

运行结果：

```
有符号整数（更具体）
整数
浮点数
vector:
  直接跳 3 步
list:
  逐个前进 3 步
4 4
```

- `int` 同时满足 `std::integral` 和 `std::signed_integral`。标准库中 `signed_integral` 的定义是 `integral<T> && is_signed_v<T>`，它**包含**了 `integral` 的全部条件，所以更具体，被优先选中。这种关系叫做**约束的包含**（subsumption）。
- `vector` 的迭代器同时满足 `forward_iterator` 和 `random_access_iterator`，后者更具体，于是选择了 O(1) 的实现；`list` 的迭代器只满足 `forward_iterator`。

这正是标准库 `std::advance` 根据迭代器类别选择实现的原理。C++20 之前要用「标签分发」技巧实现，现在只需要两个重载。

> 包含关系只能通过**概念**来判断。如果直接写 `requires std::is_integral_v<T>` 和 `requires std::is_integral_v<T> && std::is_signed_v<T>`，编译器无法看出后者包含前者，会报告调用有二义性。所以需要组合条件时，优先用概念组合，而不是直接写类型特征。

## 7. 常见错误

1. **约束写得太宽或太窄**：比如求和函数要求 `std::integral`，就无法用于 `double`。约束应该描述函数**真正需要的操作**，比如「支持 `+`」，而不是具体的类型分类。
2. **以为约束会检查函数体**：约束只检查接口。`requires` 中没有写到、但函数体中用到的操作，仍然会在实例化时以传统的方式报错。
3. **简写函数模板的多个 `auto` 是不同类型**：`void f(std::integral auto a, std::integral auto b)` 允许 `a` 是 `int`、`b` 是 `long`。需要同一类型时，用 `template <std::integral T> void f(T a, T b)`。
4. **用类型特征代替概念做重载**：见第 6 节的提示，会导致二义性。

## 8. 进阶：Concepts 与 enable_if

C++17 章节中提到，`if constexpr` 只能在函数内部选择实现，要让某些类型「根本不能调用这个函数」，C++17 只能用 SFINAE：

```cpp
// C++17：返回类型被 enable_if 弄得难以阅读
template <typename T>
std::enable_if_t<std::is_integral_v<T>, T> half(T x) { return x / 2; }

// C++20：意图一目了然
template <std::integral T>
T half(T x) { return x / 2; }
```

两者的效果相同：不满足条件时，这个重载不参与重载决议。但 Concepts 的写法可读性好得多，错误信息也清楚得多，还支持按约束的包含关系选择更具体的重载。**在 C++20 中，Concepts 可以完全取代 `enable_if`。**

概念还可以用在更多地方：

```cpp
std::integral auto n = computeSize();       // 约束 auto 变量：推导出的类型必须满足概念
static_assert(std::copyable<Widget>);       // 概念本身就是 bool 常量，可以直接用在 static_assert 中
if constexpr (Shape<T>) { /* ... */ }       // 也可以用在 if constexpr 中
```

## 9. 小结

- 没有约束的模板出错时，错误信息指向模板内部，又长又难懂；Concepts 在调用处检查，直接指出哪个要求没满足
- 四种写法：`requires` 子句、`template <概念 T>`、`概念 auto` 参数（简写函数模板）、尾置 `requires`
- 标准库提供了大量概念：`integral`、`floating_point`、`same_as`、`convertible_to`、`invocable`、各类迭代器和范围概念
- 用 `concept 名字 = requires(...) { ... };` 自定义概念；要求可以是表达式、类型、带返回类型约束的表达式、嵌套条件
- 多个重载都满足时，选择约束更具体（包含关系）的那一个
- Concepts 可以完全取代 `enable_if`

## 10. 练习

1. 定义一个概念 `Container`，要求类型有 `begin()`、`end()`、`size()`，并且有嵌套类型 `value_type`。用它写一个函数 `printContainer`。
2. 写一个函数 `sum(const C& c)`，要求 `C` 是一个范围，并且元素类型支持 `+`。（提示：`std::ranges::range_value_t<C>` 得到元素类型。）
3. 下面两个重载，`f(1)` 会调用哪一个？`f(1.0)` 呢？

   ```cpp
   void f(std::integral auto)  { std::cout << "A"; }
   void f(auto)                { std::cout << "B"; }
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   template <typename T>
   concept Container = requires(const T& c) {
       typename T::value_type;
       c.begin();
       c.end();
       { c.size() } -> std::convertible_to<std::size_t>;
   };

   void printContainer(const Container auto& c)
   {
       for (const auto& x : c) std::cout << x << ' ';
       std::cout << '\n';
   }
   ```

2. 参考代码：

   ```cpp
   template <std::ranges::range C>
       requires requires(std::ranges::range_value_t<C> a) { a + a; }
   auto sum(const C& c)
   {
       std::ranges::range_value_t<C> total{};
       for (const auto& x : c) total = total + x;
       return total;
   }
   ```

   注意 `requires requires`：第一个 `requires` 开始一个 requires 子句，第二个 `requires` 开始一个 requires 表达式。

3. `f(1)` 调用 A：两个都可行，A 有约束而 B 没有，有约束的更具体。`f(1.0)` 调用 B：`double` 不满足 `integral`，A 不可行。

</details>
