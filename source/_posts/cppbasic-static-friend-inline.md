---
title: static 成员、友元与内联函数
date: 2026-10-23 10:40:00
updated: 2026-10-23 10:40:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 面向对象
order: 6
description: 静态成员属于类而不是某个对象，适合实现计数器、编号生成；友元让指定的函数或类访问私有成员，是重载 << 运算符的基础；内联函数可以定义在头文件中而不会重复定义。本文用多文件工程实测 inline 解决的链接错误。
disableNunjucks: true
---

> **本文目标**：会用静态成员变量和静态成员函数；理解静态局部变量；会用友元函数和友元类，知道什么时候该用；理解 `inline` 的真正含义，知道它解决的是「头文件中定义函数」的链接问题。
>
> **前置知识**：本章《类与对象》《构造与析构》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. static 成员

### 1.1 静态成员变量：所有对象共享一份

一个在线游戏需要记录「当前有多少个玩家在线」，并给每个玩家分配一个唯一的编号。这些数据不属于某个玩家，而是属于「玩家」这个整体。用全局变量可以实现，但全局变量谁都能修改，也和 `Player` 类没有任何关联。

**静态成员变量**属于类本身，**所有对象共享同一份**：

```cpp
#include <iostream>
#include <string>

class Player {
public:
    explicit Player(std::string name) : name_(std::move(name)), id_(++nextId_)
    {
        ++alive_;
    }
    ~Player() { --alive_; }

    static int alive() { return alive_; }          // 静态成员函数：不属于某个对象，没有 this
    int id() const { return id_; }
    const std::string& name() const { return name_; }

private:
    std::string name_;
    int id_;
    static inline int nextId_ = 0;                  // 静态成员变量：所有对象共享一份
    static inline int alive_ = 0;
};

int nextTicket()
{
    static int ticket = 100;                        // 静态局部变量：只初始化一次，函数返回后仍然存在
    return ++ticket;
}

int main()
{
    std::cout << "开始时在线: " << Player::alive() << '\n';    // 不需要对象，用 类名:: 调用
    Player a("Alice"), b("Bob");
    {
        Player c("Carol");
        std::cout << c.name() << " 的编号: " << c.id() << "，在线: " << Player::alive() << '\n';
    }
    std::cout << "Carol 离开后在线: " << Player::alive() << '\n';
    std::cout << "sizeof(Player) = " << sizeof(Player) << "（静态成员不占对象的空间）\n";

    for (int i = 0; i < 3; ++i) std::cout << "取号: " << nextTicket() << '\n';
    return 0;
}
```

运行结果：

```
开始时在线: 0
Carol 的编号: 3，在线: 3
Carol 离开后在线: 2
sizeof(Player) = 32（静态成员不占对象的空间）
取号: 101
取号: 102
取号: 103
```

- 每创建一个 `Player`，构造函数把 `alive_` 加 1，并用 `++nextId_` 分配编号；析构函数把 `alive_` 减 1。所有对象操作的是**同一个** `alive_`，所以它准确地记录了在线人数。
- 静态成员变量**不存储在对象中**：`sizeof(Player)` 是 32 字节，正好是一个 `std::string`（24 字节）加一个 `int`（4 字节），再补齐 4 字节对齐，`nextId_` 和 `alive_` 没有占用对象的空间。
- `private` 的静态成员只能在类的成员函数中访问，比全局变量安全。

**定义方式**：C++17 起可以写成 `static inline int alive_ = 0;`，直接在类中定义和初始化。C++17 之前，类中只能**声明** `static int alive_;`，还必须在某一个 `.cpp` 文件中**定义**一次：`int Player::alive_ = 0;`，否则链接时会报 `undefined symbol`。在老代码中会经常看到这种写法。

### 1.2 静态成员函数

`static int alive()` 是**静态成员函数**：它属于类，不需要通过对象调用，直接写 `Player::alive()`。因为不通过对象调用，它**没有 `this` 指针**，所以只能访问静态成员，不能访问普通成员：

```cpp
class Player {
public:
    static int alive() { return id_; }      // 错误：静态成员函数中没有 this，不能访问普通成员
private:
    int id_ = 0;
};
```

```
error: invalid use of member 'id_' in static member function
```

静态成员函数常用于：访问静态成员变量、工厂函数（如 `static Player create(...)`）、和类相关但不需要对象的工具函数。

### 1.3 静态局部变量

函数中用 `static` 修饰的局部变量（上面的 `ticket`）**只在第一次执行到它的定义时初始化一次**，之后一直存在，函数返回后也不会销毁，下次调用时保留着上次的值。所以 `nextTicket()` 每次返回的号码都比上次大 1。

C++11 起，静态局部变量的初始化是**线程安全的**：多个线程同时第一次调用这个函数，也只会初始化一次。这让它成为实现「单例」最简单的方式：

```cpp
Config& globalConfig()
{
    static Config instance;     // 第一次调用时构造，之后一直返回同一个对象
    return instance;
}
```

> `static` 这个关键字在 C++ 中有三种含义，容易混淆：① 类中的 `static`：属于类而不是对象；② 函数中的 `static` 局部变量：只初始化一次，生命周期持续到程序结束；③ 全局作用域中的 `static` 函数或变量：只在当前 `.cpp` 文件内可见（现代 C++ 中更推荐用匿名命名空间 `namespace { ... }` 实现同样的效果）。

## 2. 友元

### 2.1 友元函数

有时一个**非成员函数**需要访问类的私有成员。最典型的例子是重载 `<<` 运算符，让 `std::cout << v` 能输出自定义类型：`<<` 的左边是 `std::cout`，所以它不能是 `Vector2` 的成员函数（运算符重载的细节见下一篇）。

在类中用 **`friend`** 声明一个函数，这个函数就可以访问类的私有成员：

```cpp
#include <iostream>

class Vector2 {
public:
    Vector2(double x, double y) : x_(x), y_(y) {}

    friend double dot(const Vector2& a, const Vector2& b);                 // 友元函数
    friend std::ostream& operator<<(std::ostream& os, const Vector2& v);   // 友元：让 cout << v 能访问私有成员
    friend class Matrix;                                                   // 友元类

private:
    double x_, y_;
};

double dot(const Vector2& a, const Vector2& b)          // 不是成员函数，但可以访问私有成员
{
    return a.x_ * b.x_ + a.y_ * b.y_;
}

std::ostream& operator<<(std::ostream& os, const Vector2& v)
{
    return os << '(' << v.x_ << ", " << v.y_ << ')';
}

class Matrix {
public:
    static Vector2 scale(const Vector2& v, double k) { return {v.x_ * k, v.y_ * k}; }   // 友元类可以访问私有成员
};

int main()
{
    Vector2 a(1, 2), b(3, 4);
    std::cout << "a = " << a << ", b = " << b << '\n';
    std::cout << "dot(a, b) = " << dot(a, b) << '\n';
    std::cout << "scale(a, 10) = " << Matrix::scale(a, 10) << '\n';
    return 0;
}
```

运行结果：

```
a = (1, 2), b = (3, 4)
dot(a, b) = 11
scale(a, 10) = (10, 20)
```

- 友元函数**不是成员函数**：没有 `this`，调用时也不用写 `对象.`。
- **友元类**：`friend class Matrix;` 让 `Matrix` 的所有成员函数都能访问 `Vector2` 的私有成员。
- 友元关系是**单向的、不传递的、不继承的**：`Matrix` 是 `Vector2` 的友元，不代表 `Vector2` 能访问 `Matrix`；`Matrix` 的友元也不能访问 `Vector2`。

### 2.2 友元破坏封装吗

友元确实开了一个「后门」，但它是**由类自己授权的**，只有类中明确列出的函数和类才能访问，这和把成员设为 `public` 完全不同。合理的使用场景：

- 重载 `<<`、`>>`、`==`、`+` 等需要访问私有成员的非成员运算符；
- 两个关系紧密、需要互相访问内部细节的类，比如容器和它的迭代器。

如果发现需要大量的友元，通常说明类的设计有问题，应该重新考虑接口。另外，能用公开接口实现的就不需要友元，比如 `dot` 也可以通过 `x()`、`y()` 两个公开的访问函数来实现。

## 3. 内联函数

### 3.1 头文件中定义函数的问题

把一个工具函数直接定义在头文件中：

```cpp
// util_bad.h
#pragma once
int square(int x) { return x * x; }          // 头文件中定义普通函数
```

两个 `.cpp` 文件都包含它，编译没问题，链接时报错：

```
duplicate symbol 'square(int)' in:
ld: 1 duplicate symbols
```

原因是**单一定义规则**（ODR）：`#include` 是文本复制，两个 `.cpp` 都得到了一份 `square` 的**定义**，链接器发现同一个函数被定义了两次。`#pragma once` 只能防止**同一个 `.cpp` 中**重复包含，管不了多个 `.cpp` 文件。

### 3.2 inline

加上 `inline`：

```cpp
// util.h
#pragma once
inline int square(int x) { return x * x; }   // 加上 inline
```

链接成功，运行结果：

```
9 16
```

**`inline` 的含义是：允许这个函数在多个翻译单元（`.cpp` 文件）中被定义，只要每份定义完全相同，链接器会把它们合并成一个。** 这才是现代 C++ 中 `inline` 的主要作用。

- **类定义中直接写出函数体的成员函数，自动是 `inline` 的**。所以上一篇中 `int x() const { return x_; }` 这样的短函数可以放在头文件的类定义中。
- **函数模板**也可以直接放在头文件中，不需要 `inline`（见《函数模板与类模板》）。
- C++17 的 `inline` 变量是同样的道理，见 C++17 章节的《inline 变量》。

### 3.3 inline 和「内联展开」

`inline` 这个名字来自它最初的用途：建议编译器把函数调用**内联展开**，也就是把函数体直接复制到调用处，省去函数调用的开销。但在现代编译器中：

- 编译器会**自己决定**是否内联：没有 `inline` 的函数，开启优化后也可能被内联；写了 `inline` 的大函数，编译器也可能不内联。
- 所以 `inline` 作为「性能提示」的作用已经很弱了，**它主要的意义是上面说的「允许多处定义」**。

**规则：头文件中定义的非模板函数，要么写在类定义中，要么加上 `inline`。**

## 4. 常见错误

1. **C++17 之前，静态成员变量只在类中声明，忘了在 `.cpp` 中定义**：链接时报 `undefined symbol`。
2. **在静态成员函数中访问普通成员**：没有 `this`，无法访问。
3. **用友元代替好的接口设计**：能用公开成员函数实现的，就不要声明友元。
4. **头文件中定义了非 inline 的普通函数或全局变量**：被多个文件包含时链接失败。
5. **以为写了 `inline` 函数就一定会被内联展开**：是否展开由编译器决定。

## 5. 小结

- **静态成员变量**属于类，所有对象共享一份，不占对象的空间；C++17 起用 `static inline` 在类中直接定义
- **静态成员函数**没有 `this`，通过 `类名::函数()` 调用，只能访问静态成员
- **静态局部变量**只初始化一次，生命周期持续到程序结束；C++11 起初始化是线程安全的
- **友元函数 / 友元类**可以访问类的私有成员，由类自己授权；关系单向、不传递、不继承；典型用途是重载 `<<`
- **`inline`** 允许函数在多个 `.cpp` 中定义而不冲突，解决头文件中定义函数的链接问题；是否内联展开由编译器决定

## 6. 练习

1. 给 `Player` 类增加一个静态成员函数 `static int total()`，返回历史上一共创建过多少个玩家（包括已经离开的）。
2. 实现一个 `IdGenerator` 类，所有对象共享同一个计数器，`next()` 返回递增的编号；再思考：如果多个线程同时调用 `next()`，会有什么问题？如何解决？
3. 下面的代码放在头文件 `math_util.h` 中，被多个 `.cpp` 包含，哪些会导致链接错误？

   ```cpp
   int add(int a, int b) { return a + b; }
   inline int sub(int a, int b) { return a - b; }
   template <typename T> T mul(T a, T b) { return a * b; }
   struct Calc { int div(int a, int b) { return a / b; } };
   int counter = 0;
   ```

<details>
<summary>点击查看答案</summary>

1. `nextId_` 每创建一个对象就加 1，永远不减，它的值就是创建过的总数：

   ```cpp
   static int total() { return nextId_; }
   ```

2. 参考代码：

   ```cpp
   class IdGenerator {
   public:
       int next() { return ++counter_; }
   private:
       static inline int counter_ = 0;
   };
   ```

   多个线程同时执行 `++counter_` 是数据竞争，可能产生重复的编号。把计数器改为 `static inline std::atomic<int> counter_{0};`，`++counter_` 就是原子操作了（见 C++11 章节的《原子操作与内存序》）。

3. `add`（普通函数）和 `counter`（全局变量）会导致 `duplicate symbol` 链接错误。`sub` 是 `inline` 的；`mul` 是函数模板；`Calc::div` 定义在类中，自动是 `inline` 的，这三个都没有问题。`counter` 可以改为 C++17 的 `inline int counter = 0;`。

</details>
