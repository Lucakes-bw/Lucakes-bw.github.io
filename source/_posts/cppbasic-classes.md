---
title: 类与对象：封装
date: 2026-10-23 10:00:00
updated: 2026-10-23 10:00:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 面向对象
order: 4
description: 类把数据和操作数据的函数封装在一起，并用 public / private 控制访问权限，从而保证对象始终处于合法状态。本文从一个可以被随意改成负数的银行账户讲起，介绍类的定义、访问控制、this 指针、const 成员函数，以及如何把类拆成头文件和源文件。
disableNunjucks: true
---

> **本文目标**：理解封装的意义；会定义类、创建对象、调用成员函数；会用 `public` 和 `private` 控制访问；理解 `this` 指针；会写 `const` 成员函数；会把类的声明和实现分别放在 `.h` 和 `.cpp` 文件中。
>
> **前置知识**：本章前三篇，C 语言的结构体。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 问题：数据可以被随意修改

用 C 语言实现一个银行账户：

```cpp
#include <iostream>

// C 语言风格：数据和操作分开，任何人都能直接改数据
struct Account {
    char owner[32];
    double balance;
};

void withdraw(Account* a, double amount)
{
    if (amount > 0 && amount <= a->balance) a->balance -= amount;
}

int main()
{
    Account acc{"Alice", 100};
    withdraw(&acc, 30);
    acc.balance = -999999;          // 绕过了 withdraw 中的检查，直接改成负数
    std::cout << acc.owner << " 的余额: " << acc.balance << '\n';
    return 0;
}
```

运行结果：

```
Alice 的余额: -999999
```

`withdraw` 函数中写了检查：取款金额必须为正，且不能超过余额。但这个检查形同虚设，因为**任何代码都可以绕过它，直接修改 `balance`**。程序越大，越难保证每一处都「守规矩」。一旦余额变成了负数，你很难找出是哪一行代码改的。

问题的根源在于：**数据和保护数据的规则是分开的**。

## 2. 类：把数据和操作封装在一起

C++ 的**类**（class）把数据（**成员变量**）和操作这些数据的函数（**成员函数**）放在一起，并且可以规定哪些是外部能访问的：

```cpp
#include <iostream>
#include <string>

class BankAccount {
public:                                         // 公开的接口
    void open(const std::string& owner, double initial)
    {
        owner_ = owner;
        balance_ = initial > 0 ? initial : 0;
    }

    bool deposit(double amount)
    {
        if (amount <= 0) return false;
        balance_ += amount;
        return true;
    }

    bool withdraw(double amount)
    {
        if (amount <= 0 || amount > balance_) {
            std::cout << "  取款 " << amount << " 失败\n";
            return false;
        }
        balance_ -= amount;
        return true;
    }

    double balance() const { return balance_; }  // 只读访问
    const std::string& owner() const { return owner_; }

private:                                        // 私有的数据：外部无法直接访问
    std::string owner_;
    double balance_ = 0;
};

int main()
{
    BankAccount acc;                            // 创建一个对象
    acc.open("Alice", 100);
    acc.deposit(50);
    acc.withdraw(30);
    acc.withdraw(1000);                         // 余额不足，被拒绝
    std::cout << acc.owner() << " 的余额: " << acc.balance() << '\n';

    BankAccount other;                          // 同一个类可以创建多个对象，各自有独立的数据
    other.open("Bob", 20);
    std::cout << other.owner() << " 的余额: " << other.balance() << '\n';
    return 0;
}
```

运行结果：

```
  取款 1000 失败
Alice 的余额: 120
Bob 的余额: 20
```

几个概念：

- **类**是一种自定义的类型，像一张「图纸」；**对象**是用这个类型创建的变量，是根据图纸造出来的「实物」。`BankAccount` 是类，`acc` 和 `other` 是对象，它们各自有独立的 `owner_` 和 `balance_`。
- 用 `对象.成员函数()` 调用成员函数，函数中直接使用的 `balance_` 就是**这个对象的** `balance_`。
- 成员变量名后面的下划线 `_` 只是一种命名习惯，用来区分成员变量和局部变量，不是语法要求。

## 3. 访问控制：public 与 private

现在尝试像 C 语言那样直接修改余额：

```cpp
BankAccount acc;
acc.balance_ = -999999;         // 错误：balance_ 是私有成员
```

```
error: 'balance_' is a private member of 'BankAccount'
note: declared private here
```

**编译直接失败**。外部代码只能通过 `deposit`、`withdraw` 这些公开的函数来改变余额，而这些函数中的检查**无法被绕过**。只要这几个成员函数写对了，余额就永远不可能变成负数。这就是**封装**：把数据隐藏起来，只通过受控的接口访问。

| 访问说明符 | 类的成员函数中 | 外部代码中 |
| --- | --- | --- |
| `public` | 可以访问 | 可以访问 |
| `private` | 可以访问 | **不能访问** |
| `protected` | 可以访问 | 不能访问（派生类中可以，见《继承》） |

**设计原则：成员变量一律 `private`，只把必要的操作设为 `public`。** 这样做还有一个好处：将来修改内部的实现（比如把余额从 `double` 改成以「分」为单位的整数，避免浮点误差），只要公开的接口不变，使用这个类的代码一行都不用改。

### 3.1 class 和 struct

C++ 中 `struct` 也可以有成员函数和访问控制，和 `class` 唯一的区别是**默认的访问权限**：`class` 的成员默认是 `private`，`struct` 默认是 `public`。习惯上，只是把几个数据打包在一起、没有什么约束的简单类型用 `struct`；需要封装、维护内部规则的用 `class`。

## 4. this 指针

成员函数是怎么知道该操作哪个对象的数据的？答案是**每个成员函数都有一个隐藏的参数 `this`**，它指向调用这个函数的对象。

```cpp
#include <iostream>
#include <string>

class Builder {
public:
    Builder& setName(const std::string& name) { name_ = name; return *this; }   // 返回自身的引用
    Builder& setAge(int age)                  { this->age_ = age; return *this; }
    void print() const
    {
        std::cout << "this = " << this << ", " << name_ << ", " << age_ << " 岁\n";
    }
private:
    std::string name_;
    int age_ = 0;
};

int main()
{
    Builder b;
    std::cout << "&b   = " << &b << '\n';
    b.setName("Alice").setAge(20).print();      // 链式调用
    return 0;
}
```

运行结果：

```
&b   = 0x16b3ba528
this = 0x16b3ba528, Alice, 20 岁
```

- `this` 的值和 `&b` 完全相同。调用 `b.setAge(20)` 时，编译器实际上把它当作 `setAge(&b, 20)` 来处理。
- 成员函数中直接写 `age_`，等价于 `this->age_`。通常可以省略 `this->`，只在参数和成员同名时才需要写出来区分。
- **`return *this;`** 返回对象自身的引用，于是可以在返回值上继续调用成员函数，形成 `b.setName(...).setAge(...).print()` 这样的**链式调用**。`std::cout << a << b` 能连续输出，也是同样的原理（见《运算符重载》）。

## 5. const 成员函数

成员函数后面的 `const`（如 `double balance() const`）表示**这个函数不会修改对象**：

```cpp
#include <string>
class Counter {
public:
    int get() const { return value_; }      // const 成员函数：承诺不修改对象
    void inc() { ++value_; }
    void bad() const { ++value_; }          // 错误：const 函数中不能修改成员
private:
    int value_ = 0;
};
void show(const Counter& c)
{
    c.get();                                // OK
    c.inc();                                // 错误：const 对象只能调用 const 成员函数
}
```

```
error: cannot assign to non-static data member within const member function 'bad'
error: 'this' argument to member function 'inc' has type 'const Counter', but function is not marked const
```

- 在 `const` 成员函数中修改成员变量，编译失败。
- **`const` 对象（或 `const` 引用）只能调用 `const` 成员函数**。上一篇讲过，只读参数应该写成 `const T&`；如果类的只读函数忘了加 `const`，那么拿到 `const` 引用的代码就连 `get()` 都调用不了。

**规则：所有不修改对象的成员函数，都要加上 `const`。**

> 极少数情况下，某个成员变量不影响对象的「逻辑状态」（比如缓存、互斥锁），需要在 `const` 函数中修改，可以把它声明为 `mutable`。

## 6. 对象的内存布局

```cpp
#include <iostream>

class Point {
public:
    void move(int dx, int dy) { x_ += dx; y_ += dy; }
    int x() const { return x_; }
    int y() const { return y_; }
private:
    int x_ = 0, y_ = 0;
};

class Empty {};

int main()
{
    Point a, b;
    a.move(1, 2);
    b.move(10, 20);
    std::cout << "sizeof(Point) = " << sizeof(Point) << "（只有两个 int，成员函数不占对象的空间）\n";
    std::cout << "sizeof(Empty) = " << sizeof(Empty) << "（空类也至少占 1 字节，保证不同对象地址不同）\n";
    std::cout << "a = (" << a.x() << ", " << a.y() << "), b = (" << b.x() << ", " << b.y() << ")\n";
    return 0;
}
```

运行结果：

```
sizeof(Point) = 8（只有两个 int，成员函数不占对象的空间）
sizeof(Empty) = 1（空类也至少占 1 字节，保证不同对象地址不同）
a = (1, 2), b = (10, 20)
```

**每个对象只保存自己的成员变量**，所以 `Point` 和包含两个 `int` 的 C 结构体一样大。成员函数的代码只有一份，所有对象共用，通过 `this` 区分操作的是哪个对象。所以封装**没有任何额外的内存开销**，成员函数调用也和普通函数调用一样快。（有虚函数的类除外，见《多态与虚函数》。）

## 7. 把类拆成头文件和源文件

实际项目中，类的**声明**放在头文件（`.h`）中，供其他文件包含；成员函数的**定义**放在源文件（`.cpp`）中：

```cpp
// point.h：类的声明
#pragma once

class Point {
public:
    void move(int dx, int dy);
    double distanceToOrigin() const;
    int x() const { return x_; }          // 很短的函数可以直接写在类中
    int y() const { return y_; }
private:
    int x_ = 0;
    int y_ = 0;
};
```

```cpp
// point.cpp：成员函数的定义
#include "point.h"
#include <cmath>

void Point::move(int dx, int dy)          // 类外定义要加上 Point::
{
    x_ += dx;
    y_ += dy;
}

double Point::distanceToOrigin() const    // const 也要写
{
    return std::sqrt(x_ * x_ + y_ * y_);
}
```

```cpp
#include <iostream>
#include "point.h"

int main()
{
    Point p;
    p.move(3, 4);
    std::cout << "(" << p.x() << ", " << p.y() << ") 到原点的距离: " << p.distanceToOrigin() << '\n';
    return 0;
}
```

一起编译：

```bash
g++ -std=c++17 point.cpp main.cpp -o app
./app
```

```
(3, 4) 到原点的距离: 5
```

- 在类外定义成员函数时，函数名前要加上 **`类名::`**，表示它属于哪个类。
- `const` 是函数签名的一部分，声明和定义中都要写。
- 直接写在类定义中的成员函数自动是 `inline` 的，适合很短的函数（比如 `x()`、`y()`），放在头文件中不会导致重复定义。
- `#pragma once` 防止头文件被重复包含，也可以用传统的 `#ifndef POINT_H / #define POINT_H / #endif`。

## 8. 常见错误

1. **成员变量设为 `public`**：失去了封装的意义。
2. **只读的成员函数忘记加 `const`**：`const` 对象无法调用它。
3. **类定义的右花括号后面忘记写分号**：`class A { ... };` 结尾的分号不能省略，否则会出现莫名其妙的编译错误。
4. **类外定义成员函数时忘记写 `类名::`**：这会定义一个同名的普通函数，在里面访问成员变量时报错。
5. **对象创建后忘记调用 `open`**：本文的 `BankAccount` 必须先调用 `open` 才能使用，忘记调用时 `owner_` 是空的。这个问题由下一篇的**构造函数**来解决：让对象在创建的那一刻就完成初始化。

## 9. 小结

- 类把数据（成员变量）和操作（成员函数）封装在一起；类是类型，对象是这个类型的变量
- `private` 成员只能在类的成员函数中访问，外部只能通过 `public` 接口操作数据，从而保证对象始终合法
- `class` 默认 `private`，`struct` 默认 `public`，其他完全相同
- 每个成员函数都有隐藏的 `this` 指针，指向调用它的对象；`return *this;` 可以实现链式调用
- 不修改对象的成员函数要加 `const`；`const` 对象只能调用 `const` 成员函数
- 对象只包含成员变量，成员函数不占对象的空间
- 类的声明放在 `.h`，成员函数的定义放在 `.cpp`，类外定义要写 `类名::`

## 10. 练习

1. 设计一个 `Rectangle` 类，私有成员为宽和高，提供 `setSize`（宽高必须为正数，否则拒绝修改）、`area`、`perimeter`、`isSquare` 等成员函数，哪些应该是 `const` 的？
2. 给 `BankAccount` 增加一个 `transferTo(BankAccount& other, double amount)` 成员函数，把钱转给另一个账户。在这个函数中，能否直接访问 `other.balance_`？
3. 下面的代码能编译吗？为什么？

   ```cpp
   class Temperature {
   public:
       double celsius() { return c_; }
   private:
       double c_ = 20;
   };
   void report(const Temperature& t) { std::cout << t.celsius(); }
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码，`area`、`perimeter`、`isSquare` 都不修改对象，应该是 `const` 的：

   ```cpp
   class Rectangle {
   public:
       bool setSize(double w, double h)
       {
           if (w <= 0 || h <= 0) return false;
           w_ = w; h_ = h;
           return true;
       }
       double area() const { return w_ * h_; }
       double perimeter() const { return 2 * (w_ + h_); }
       bool isSquare() const { return w_ == h_; }
   private:
       double w_ = 1, h_ = 1;
   };
   ```

2. 可以。访问控制是**以类为单位**的，而不是以对象为单位：`BankAccount` 的成员函数可以访问任何一个 `BankAccount` 对象的私有成员。

   ```cpp
   bool transferTo(BankAccount& other, double amount)
   {
       if (!withdraw(amount)) return false;
       other.balance_ += amount;
       return true;
   }
   ```

3. 不能。`t` 是 `const` 引用，只能调用 `const` 成员函数，而 `celsius()` 没有声明为 `const`。改成 `double celsius() const { return c_; }`。

</details>
