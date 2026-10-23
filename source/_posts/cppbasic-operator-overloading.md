---
title: 运算符重载
date: 2026-10-23 11:00:00
updated: 2026-10-23 11:00:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 运算符重载
order: 7
description: 运算符重载让自定义类型像内置类型一样使用 +、==、<<、[]、() 等运算符。本文从零实现一个完整的分数类，讲清楚成员函数和非成员函数的选择、复合赋值与二元运算符的关系、前置和后置 ++ 的区别，以及 [] 和 () 的重载与函数对象。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解运算符重载的本质是函数调用；会重载算术、比较、输入输出、自增、下标、函数调用运算符；知道哪些运算符该写成成员函数、哪些该写成非成员函数；会基于 `+=` 实现 `+`；理解函数对象。
>
> **前置知识**：本章《类与对象》《构造与析构》《static 成员、友元与内联》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 运算符重载是什么

对于内置类型，`a + b`、`a == b`、`std::cout << a` 都可以直接用。对于自己定义的类，比如一个分数类 `Fraction`，C++ 允许你**定义这些运算符的含义**，让代码写成：

```cpp
Fraction a(1, 2), b(1, 3);
Fraction c = a + b;           // 而不是 Fraction c = add(a, b);
std::cout << c;               // 而不是 print(c);
```

**运算符重载的本质就是一个名字特殊的函数**：`a + b` 会被编译器转换为调用 `operator+(a, b)`（非成员函数）或 `a.operator+(b)`（成员函数）。除了名字，它和普通函数没有任何区别。

前面的文章已经多次用到了运算符重载：`std::string` 的 `+` 和 `==`，`std::cout` 的 `<<`，`std::vector` 的 `[]`，以及《构造与析构》中的拷贝赋值 `operator=`。

## 2. 实现一个完整的分数类

```cpp
// fraction.h：分数类
#pragma once
#include <iostream>
#include <numeric>
#include <stdexcept>

class Fraction {
public:
    Fraction(long long num = 0, long long den = 1) : num_(num), den_(den)   // 允许 int 隐式转换成分数
    {
        if (den_ == 0) throw std::invalid_argument("分母不能为 0");
        normalize();
    }

    long long num() const { return num_; }
    long long den() const { return den_; }

    // 复合赋值运算符：修改自身，写成成员函数，返回自身的引用
    Fraction& operator+=(const Fraction& o) { num_ = num_ * o.den_ + o.num_ * den_; den_ *= o.den_; normalize(); return *this; }
    Fraction& operator-=(const Fraction& o) { num_ = num_ * o.den_ - o.num_ * den_; den_ *= o.den_; normalize(); return *this; }
    Fraction& operator*=(const Fraction& o) { num_ *= o.num_; den_ *= o.den_; normalize(); return *this; }
    Fraction& operator/=(const Fraction& o)
    {
        if (o.num_ == 0) throw std::domain_error("除数不能为 0");
        num_ *= o.den_; den_ *= o.num_; normalize(); return *this;
    }

    Fraction operator-() const { return Fraction(-num_, den_); }        // 一元负号

    Fraction& operator++() { num_ += den_; return *this; }              // 前置 ++：先加，返回自身
    Fraction operator++(int) { Fraction old = *this; ++*this; return old; }   // 后置 ++：返回旧值

    explicit operator double() const { return static_cast<double>(num_) / den_; }   // 类型转换运算符

private:
    void normalize()
    {
        if (den_ < 0) { num_ = -num_; den_ = -den_; }
        long long g = std::gcd(num_, den_);
        if (g > 1) { num_ /= g; den_ /= g; }
    }
    long long num_, den_;
};

// 二元算术运算符：写成非成员函数，基于复合赋值实现
inline Fraction operator+(Fraction a, const Fraction& b) { return a += b; }
inline Fraction operator-(Fraction a, const Fraction& b) { return a -= b; }
inline Fraction operator*(Fraction a, const Fraction& b) { return a *= b; }
inline Fraction operator/(Fraction a, const Fraction& b) { return a /= b; }

// 比较运算符
inline bool operator==(const Fraction& a, const Fraction& b) { return a.num() == b.num() && a.den() == b.den(); }
inline bool operator!=(const Fraction& a, const Fraction& b) { return !(a == b); }
inline bool operator<(const Fraction& a, const Fraction& b)  { return a.num() * b.den() < b.num() * a.den(); }
inline bool operator>(const Fraction& a, const Fraction& b)  { return b < a; }
inline bool operator<=(const Fraction& a, const Fraction& b) { return !(b < a); }
inline bool operator>=(const Fraction& a, const Fraction& b) { return !(a < b); }

// 输入输出运算符：左边是流，只能写成非成员函数
inline std::ostream& operator<<(std::ostream& os, const Fraction& f)
{
    os << f.num();
    if (f.den() != 1) os << '/' << f.den();
    return os;
}
inline std::istream& operator>>(std::istream& is, Fraction& f)
{
    long long n, d;
    char slash;
    if (is >> n >> slash >> d && slash == '/') f = Fraction(n, d);
    else is.setstate(std::ios::failbit);
    return is;
}
```

使用它：

```cpp
#include <algorithm>
#include <iostream>
#include <vector>
#include "fraction.h"

int main()
{
    Fraction a(1, 2), b(1, 3);
    std::cout << a << " + " << b << " = " << a + b << '\n';
    std::cout << a << " - " << b << " = " << a - b << '\n';
    std::cout << a << " * " << b << " = " << a * b << '\n';
    std::cout << a << " / " << b << " = " << a / b << '\n';
    std::cout << "6/8 化简为 " << Fraction(6, 8) << ", -a = " << -a << '\n';

    std::cout << "a + 1 = " << a + 1 << ", 2 * b = " << 2 * b << '\n';    // int 自动转换为 Fraction
    std::cout << std::boolalpha << "a > b: " << (a > b) << ", a == 2/4: " << (a == Fraction(2, 4)) << '\n';

    Fraction c = a;
    Fraction post = c++;                    // 后置：返回旧值
    std::cout << "c++ 的值: " << post << ", 之后 c = " << c << '\n';
    Fraction pre = ++c;                     // 前置：返回新值
    std::cout << "++c 的值: " << pre << ", 之后 c = " << c << '\n';
    std::cout << "double(a) = " << static_cast<double>(a) << '\n';

    std::vector<Fraction> fs{{3, 4}, {1, 6}, {2, 3}, {1, 2}};
    std::sort(fs.begin(), fs.end());                                     // sort 使用 operator<
    for (const auto& f : fs) std::cout << f << ' ';
    std::cout << '\n';

    Fraction sum;
    for (int i = 1; i <= 10; ++i) sum += Fraction(1, i * (i + 1));       // 1/(1×2) + 1/(2×3) + ... = 10/11
    std::cout << "裂项求和 = " << sum << '\n';

    Fraction input;
    std::cout << "输入一个分数: ";
    if (std::cin >> input) std::cout << "你输入的是 " << input << "，平方是 " << input * input << '\n';
    return 0;
}
```

输入 `3/5`，运行结果：

```
1/2 + 1/3 = 5/6
1/2 - 1/3 = 1/6
1/2 * 1/3 = 1/6
1/2 / 1/3 = 3/2
6/8 化简为 3/4, -a = -1/2
a + 1 = 3/2, 2 * b = 2/3
a > b: true, a == 2/4: true
c++ 的值: 1/2, 之后 c = 3/2
++c 的值: 5/2, 之后 c = 5/2
double(a) = 0.5
1/6 1/2 2/3 3/4 
裂项求和 = 10/11
输入一个分数: 你输入的是 3/5，平方是 9/25
```

有了运算符重载，`Fraction` 用起来和 `int`、`double` 几乎一样：能做四则运算、能比较、能用 `cout` 输出、能用 `cin` 读入，还能直接交给 `std::sort` 排序（`sort` 默认用 `<` 比较元素）。下面逐一讲解其中的要点。

## 3. 成员函数还是非成员函数

### 3.1 一个对称性问题

先看把 `+` 写成成员函数会怎样：

```cpp
class Money {
public:
    Money(long long cents) : cents_(cents) {}
    Money operator+(const Money& o) const { return Money(cents_ + o.cents_); }   // 写成成员函数
private:
    long long cents_;
};
int main()
{
    Money a(100);
    Money b = a + 50;       // OK：a.operator+(Money(50))
    Money c = 50 + a;       // 错误：50 不是 Money，没有 50.operator+(...)
}
```

```
error: invalid operands to binary expression ('int' and 'Money')
```

`a + 50` 能编译：它被转换为 `a.operator+(50)`，`50` 通过构造函数隐式转换为 `Money`。但 `50 + a` 不行：它需要调用 `50.operator+(a)`，而 `int` 没有这样的成员函数。**成员函数的左操作数不会进行隐式转换**，加法本应满足的对称性就被破坏了。

`Fraction` 把 `+` 写成**非成员函数** `operator+(Fraction a, const Fraction& b)`，两个参数地位相同，`a + 1` 和 `2 * b` 都能正常工作（见上面的运行结果）。

### 3.2 选择规则

| 运算符 | 写成 | 原因 |
| --- | --- | --- |
| `=`、`[]`、`()`、`->` | **必须**是成员函数 | 语言规定 |
| `+=`、`-=`、`++`、`--`、一元 `-` | 成员函数 | 它们修改（或只涉及）对象本身 |
| `+`、`-`、`*`、`/`、`==`、`<` 等对称的二元运算符 | 非成员函数 | 两边都允许隐式转换，保持对称 |
| `<<`、`>>`（输入输出） | **只能**是非成员函数 | 左操作数是 `std::ostream` / `std::istream`，不是我们的类 |

非成员函数如果需要访问私有成员，可以声明为友元（上一篇介绍过）；`Fraction` 提供了公开的 `num()` 和 `den()`，所以不需要友元。

### 3.3 基于 += 实现 +

```cpp
inline Fraction operator+(Fraction a, const Fraction& b) { return a += b; }
```

这是实现算术运算符的惯用法：

1. 先把**复合赋值** `+=` 实现为成员函数，它修改自身并返回 `*this` 的引用。
2. 再用 `+=` 实现 `+`：第一个参数**按值传递**，得到一个副本，在副本上执行 `+=`，返回这个副本。

这样，加法的逻辑只写一遍（在 `+=` 中），`+` 只有一行，不会出现两者不一致的情况。

## 4. 各类运算符的要点

### 4.1 比较运算符

只需要真正实现 `==` 和 `<` 两个，其他四个都可以用它们表示：`a != b` 就是 `!(a == b)`，`a > b` 就是 `b < a`，`a <= b` 就是 `!(b < a)`。C++20 的 `<=>` 让这件事变得更简单，见 C++20 章节的《三路比较运算符》。

`<` 的实现用交叉相乘 `a.num * b.den < b.num * a.den` 来比较两个分数，避免了浮点数除法的精度问题（分母已经被规范为正数，所以不等号方向不会变）。

### 4.2 输入输出运算符

```cpp
std::ostream& operator<<(std::ostream& os, const Fraction& f);
std::istream& operator>>(std::istream& is, Fraction& f);
```

- **返回流的引用**，所以才能连续输出 `std::cout << a << " + " << b`：`std::cout << a` 返回 `std::cout`，再继续 `<< " + "`。
- `<<` 的第二个参数是 `const` 引用（输出不修改对象）；`>>` 的第二个参数是**非 const 引用**（要把读到的值存进去）。
- `>>` 中读取失败时，用 `is.setstate(std::ios::failbit)` 把流设为失败状态，这样调用者的 `if (std::cin >> input)` 才能正确判断是否成功。

### 4.3 前置 ++ 与后置 ++

```cpp
Fraction& operator++() { num_ += den_; return *this; }              // 前置 ++：先加，返回自身
Fraction operator++(int) { Fraction old = *this; ++*this; return old; }   // 后置 ++：返回旧值
```

- **前置 `++c`**：先加 1，返回加完之后的对象本身（引用）。
- **后置 `c++`**：返回加 1 **之前**的值。为了区分两者，后置版本有一个不使用的 `int` 参数，这只是一个语法标记。
- 后置版本必须先拷贝一份旧值，再修改自身，最后返回旧值的副本，**比前置版本多了一次拷贝**。这就是为什么在 `for` 循环中操作迭代器时，推荐写 `++it` 而不是 `it++`。

运行结果中 `c` 从 1/2 开始：`c++` 返回旧值 1/2，`c` 变成 3/2；`++c` 返回的就是新值 5/2。

### 4.4 类型转换运算符

```cpp
explicit operator double() const { return static_cast<double>(num_) / den_; }
```

它定义了 `Fraction` 到 `double` 的转换，没有返回类型（返回类型就是 `double`）。加上 `explicit` 后，必须写 `static_cast<double>(a)` 才能转换，避免 `Fraction` 在意想不到的地方被悄悄转换成 `double`，丢失精度。（`Fraction` 的构造函数故意没有加 `explicit`，因为「整数可以看作分母为 1 的分数」是一个自然的隐式转换，正是它让 `a + 1` 能够编译。）

## 5. 下标运算符与函数调用运算符

```cpp
#include <iostream>
#include <stdexcept>
#include <vector>

class Matrix {
public:
    Matrix(int rows, int cols) : rows_(rows), cols_(cols), data_(rows * cols, 0) {}

    // 函数调用运算符：m(i, j) 访问元素，提供 const 和非 const 两个版本
    double& operator()(int i, int j)             { return data_.at(i * cols_ + j); }
    double  operator()(int i, int j) const       { return data_.at(i * cols_ + j); }

    // 下标运算符：m[i] 返回第 i 行的起始地址，于是 m[i][j] 也能用
    double* operator[](int i)                    { return &data_[i * cols_]; }

    int rows() const { return rows_; }
    int cols() const { return cols_; }
private:
    int rows_, cols_;
    std::vector<double> data_;
};

// 函数对象：重载了 () 的类，可以像函数一样调用，还能带状态
class Counter {
public:
    int operator()() { return ++count_; }
private:
    int count_ = 0;
};

class GreaterThan {
public:
    explicit GreaterThan(int limit) : limit_(limit) {}
    bool operator()(int x) const { return x > limit_; }
private:
    int limit_;
};

int main()
{
    Matrix m(2, 3);
    m(0, 1) = 5;
    m[1][2] = 7;
    for (int i = 0; i < m.rows(); ++i) {
        for (int j = 0; j < m.cols(); ++j) std::cout << m(i, j) << ' ';
        std::cout << '\n';
    }
    try {
        m(5, 5) = 1;                              // operator() 用 at()，越界时抛异常
    } catch (const std::out_of_range& e) {
        std::cout << "越界: " << e.what() << '\n';
    }

    Counter next;
    int n1 = next();
    int n2 = next();
    int n3 = next();
    std::cout << n1 << ' ' << n2 << ' ' << n3 << '\n';

    std::vector<int> v{3, 8, 1, 9, 5};
    GreaterThan gt4(4);
    std::cout << "大于 4 的有: ";
    for (int x : v) if (gt4(x)) std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
0 5 0 
0 0 7 
越界: vector
1 2 3
大于 4 的有: 8 9 5
```

### 5.1 下标运算符 []

- 返回**引用**（`double&`），所以 `m(0, 1) = 5` 能修改元素，这一点在《引用》中讲过。
- 通常要提供 **const 和非 const 两个版本**：非 const 版本返回引用，供修改；const 版本返回值或 `const` 引用，让 `const Matrix&` 也能读取元素。
- `[]` 在 C++23 之前只能有一个参数，所以 `Matrix` 中的 `m[i]` 返回第 i 行的起始指针，再用内置的 `[]` 取第 j 个元素。用 `()` 可以直接接受两个参数 `m(i, j)`。

### 5.2 函数调用运算符 () 与函数对象

重载了 `operator()` 的类的对象，可以像函数一样调用，叫做**函数对象**（functor）。和普通函数相比，它的优势是**可以携带状态**：`Counter` 记住了自己被调用了几次，`GreaterThan` 记住了比较的界限。

函数对象在 STL 中用得非常多，比如 `std::sort(v.begin(), v.end(), std::greater<int>())` 中的 `std::greater<int>` 就是一个函数对象。C++11 的 lambda 表达式本质上就是编译器自动生成的函数对象，见 C++11 章节的《Lambda 表达式》。

## 6. 运算符重载的规则

1. **不能创造新的运算符**，比如不能定义 `**` 表示乘方。
2. **不能改变运算符的优先级、结合性和操作数个数**：重载的 `+` 仍然比 `*` 优先级低。
3. **至少有一个操作数是自定义类型**：不能重新定义两个 `int` 相加。
4. **有些运算符不能重载**：`.`（成员访问）、`::`（作用域解析）、`?:`（三目）、`sizeof`、`.*`。
5. **不要重载 `&&`、`||` 和逗号运算符**：重载后它们会失去短路求值的特性，行为会让人困惑。

## 7. 常见错误

1. **违反直觉的重载**：`+` 应该表示「相加」，不要用它表示别的意思（比如把元素加入容器）。如果一个运算符的含义不是一眼就能看明白，就用普通的命名函数。
2. **对称的二元运算符写成成员函数**：`1 + a` 无法编译。
3. **`<<` 和 `>>` 不返回流的引用**：无法连续输出。
4. **`+=` 返回值而不是引用**，或者 `+` 返回引用：前者多了一次拷贝，后者会返回局部变量的引用。
5. **`[]` 只提供非 const 版本**：`const` 对象无法使用下标。
6. **在同一条语句中多次修改同一个变量**：比如 `std::cout << c++ << ++c;`。C++17 起，`<<` 的操作数保证从左到右求值，但在 C++17 之前这是未定义行为，而且读起来非常费解。本文的示例中特意把它们拆成了单独的语句。

## 8. 小结

- 运算符重载就是名字为 `operator符号` 的函数，`a + b` 会被转换成对它的调用
- `=`、`[]`、`()` 必须是成员函数；`+=`、`++` 等修改自身的写成成员函数；`+`、`==`、`<` 等对称的二元运算符写成非成员函数；`<<`、`>>` 只能是非成员函数
- 先实现 `+=`（返回引用），再基于它实现 `+`（返回值）
- 比较运算符只需实现 `==` 和 `<`，其他用它们表示
- `<<`、`>>` 返回流的引用以支持连续使用
- 前置 `++` 返回引用，后置 `++(int)` 返回旧值的副本，前置更高效
- `[]` 和 `()` 返回引用，并提供 const 版本；重载了 `()` 的类就是函数对象，可以携带状态

## 9. 练习

1. 给 `Fraction` 增加一个 `abs` 友元函数或成员函数，以及 `operator%`（取余对分数没有意义，想一想该不该重载它）。
2. 实现一个二维向量类 `Vec2`，支持 `+`、`-`、数乘（`v * 2` 和 `2 * v` 都要支持）、点积（用命名函数 `dot` 而不是运算符）、`==` 和 `<<`。
3. 写一个函数对象 `Accumulator`，每次调用 `acc(x)` 都把 `x` 累加起来并返回当前的总和，再用 `std::for_each` 计算一个 `vector` 的和。（提示：`std::for_each` 会返回函数对象的副本。）

<details>
<summary>点击查看答案</summary>

1. `abs` 适合作为非成员函数：

   ```cpp
   inline Fraction abs(const Fraction& f) { return f.num() < 0 ? -f : f; }
   ```

   取余对分数没有自然的数学含义，不应该重载 `%`。**运算符重载应该只用于含义一目了然的场合**。

2. 参考代码：

   ```cpp
   struct Vec2 {
       double x = 0, y = 0;
       Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
       Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
       Vec2& operator*=(double k) { x *= k; y *= k; return *this; }
   };
   inline Vec2 operator+(Vec2 a, const Vec2& b) { return a += b; }
   inline Vec2 operator-(Vec2 a, const Vec2& b) { return a -= b; }
   inline Vec2 operator*(Vec2 v, double k) { return v *= k; }
   inline Vec2 operator*(double k, Vec2 v) { return v *= k; }     // 2 * v
   inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
   inline bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }
   inline std::ostream& operator<<(std::ostream& os, const Vec2& v) { return os << '(' << v.x << ", " << v.y << ')'; }
   ```

   点积用 `*` 表示会和数乘混淆，所以用命名函数 `dot`。

3. 参考代码：

   ```cpp
   class Accumulator {
   public:
       int operator()(int x) { total_ += x; return total_; }
       int total() const { return total_; }
   private:
       int total_ = 0;
   };

   std::vector<int> v{1, 2, 3, 4, 5};
   Accumulator result = std::for_each(v.begin(), v.end(), Accumulator());
   std::cout << result.total() << '\n';   // 15
   ```

   `std::for_each` 按值接收函数对象，在内部的副本上累加，最后把这个副本返回。如果直接查看传进去的那个对象，它的总和仍然是 0。

</details>
