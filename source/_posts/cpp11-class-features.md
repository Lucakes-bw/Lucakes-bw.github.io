---
title: 类的增强：default、delete、override、final 与委托构造
date: 2026-10-22 12:00:00
updated: 2026-10-22 12:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 面向对象
order: 10
description: C++11 给类增加了一组实用的关键字：= default 和 = delete 精确控制编译器生成的函数，override 和 final 让继承更安全，委托构造和继承构造减少重复代码，explicit 转换运算符避免意外的类型转换。
disableNunjucks: true
---

> **本文目标**：会用 `= default` 和 `= delete` 控制特殊成员函数；会用 `= delete` 禁止某些参数类型；养成给虚函数写 `override` 的习惯；会用 `final` 禁止继承和重写；会用委托构造函数和继承构造函数；会写 `explicit operator bool`。
>
> **前置知识**：类、构造函数、继承、虚函数。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. = default：让编译器生成

如果你不写，编译器会自动为类生成几个**特殊成员函数**：默认构造函数、析构函数、拷贝构造、拷贝赋值，以及 C++11 新增的移动构造、移动赋值。但有些情况下编译器会**不再生成**它们，比如：只要你写了任何一个构造函数，编译器就不再生成默认构造函数。

以前只能自己写一个空的版本，C++11 可以用 `= default` 显式要求编译器生成：

```cpp
#include <iostream>
#include <type_traits>

class A {
public:
    A(int x) : x_(x) {}     // 写了带参数的构造函数，编译器就不再生成默认构造函数
    int x() const { return x_; }
private:
    int x_;
};

class B {
public:
    B() = default;           // 显式要求编译器生成默认构造函数
    B(int x) : x_(x) {}
    int x() const { return x_; }
private:
    int x_ = 0;
};

struct Trivial1 { Trivial1() = default; int v; };   // 编译器生成：仍然是平凡类型
struct Trivial2 { Trivial2() {} int v; };          // 用户提供：不再是平凡类型

int main()
{
    // A a;                  // 错误：A 没有默认构造函数
    B b;                     // OK
    B b2(5);
    std::cout << "b.x() = " << b.x() << ", b2.x() = " << b2.x() << '\n';
    std::cout << std::boolalpha
              << "Trivial1 是平凡类型: " << std::is_trivial<Trivial1>::value << '\n'
              << "Trivial2 是平凡类型: " << std::is_trivial<Trivial2>::value << '\n';
    return 0;
}
```

运行结果：

```
b.x() = 0, b2.x() = 5
Trivial1 是平凡类型: true
Trivial2 是平凡类型: false
```

`B() = default;` 和 `B() {}` 看起来效果一样，其实有区别：`= default` 生成的函数可以是**平凡的**（trivial），而用户写的空函数体永远不是。平凡类型可以用 `memcpy` 安全地复制，编译器也能做更多优化，标准库的一些算法会据此选择更快的实现。**所以需要「默认行为」时，写 `= default` 而不是 `{}`。**

`= default` 也常用于**析构函数**和**拷贝 / 移动操作**，表示「我想要编译器默认的行为」，同时让读代码的人知道这是有意为之：

```cpp
class Shape {
public:
    virtual ~Shape() = default;   // 基类的虚析构函数，只需要默认行为
};
```

## 2. = delete：禁止使用

### 2.1 禁止拷贝

C++98 中想让一个类不能拷贝，只能把拷贝构造函数声明为 `private` 且不实现。C++11 可以直接把它**删除**：

```cpp
#include <iostream>

class NonCopyable {
public:
    NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;              // 禁止拷贝
    NonCopyable& operator=(const NonCopyable&) = delete;
};

class Mutex : NonCopyable { };                              // 继承后，Mutex 也不能拷贝

void setAge(int age) { std::cout << "年龄: " << age << '\n'; }
void setAge(double) = delete;                               // 禁止传入浮点数
void setAge(char) = delete;                                 // 禁止传入字符

int main()
{
    Mutex m1;
    Mutex m2 = m1;          // 错误
    setAge(18);
    setAge(18.5);           // 错误：调用了被删除的函数
    setAge('A');            // 错误
    return 0;
}
```

Clang 的报错：

```
error: call to implicitly-deleted copy constructor of 'Mutex'
error: call to deleted function 'setAge'
error: call to deleted function 'setAge'
```

互斥锁、文件句柄、线程这类代表**唯一资源**的类，拷贝没有意义，就应该删除拷贝操作。标准库的 `std::mutex`、`std::thread`、`std::unique_ptr` 都是这样做的。

### 2.2 禁止特定的参数类型

`= delete` 可以用于**任何函数**，不只是特殊成员函数。上例中 `setAge(int)` 本来可以接受 `double` 和 `char`（会发生隐式转换），把这两个重载删除后，传入它们就会编译失败。

被删除的函数**仍然参与重载决议**：编译器先选出最匹配的版本，如果选中的是被删除的函数，就报错。`setAge(18.5)` 最匹配的是 `setAge(double)`，它被删除了，所以报错，而不会退而求其次地转换成 `int`。

## 3. override：让编译器检查重写

下面的代码有一个很难发现的 bug：

```cpp
#include <iostream>
#include <memory>

class Shape {
public:
    virtual ~Shape() = default;
    virtual void draw() const { std::cout << "Shape::draw\n"; }
};

class Circle : public Shape {
public:
    void draw() { std::cout << "Circle::draw\n"; }   // 少写了 const：这是一个新函数，不是重写！
};

class Square : public Shape {
public:
    void draw() const override { std::cout << "Square::draw\n"; }   // override：让编译器检查
};

int main()
{
    std::unique_ptr<Shape> s1(new Circle);
    std::unique_ptr<Shape> s2(new Square);
    s1->draw();
    s2->draw();
    return 0;
}
```

运行结果：

```
Shape::draw
Square::draw
```

`Circle::draw` 少写了一个 `const`，签名和基类不同，于是它**不是重写**，而是一个新的、和基类无关的函数。通过基类指针调用时，执行的仍然是 `Shape::draw`。参数类型不同、`const` 不同、基类函数忘了写 `virtual`、函数名拼写错误，都会导致同样的问题，而代码能正常编译运行。

> 这个例子中 Clang 开启 `-Wall` 时会给出警告 `'Circle::draw' hides overloaded virtual function`，但不是所有情况、所有编译器都会警告。

如果在派生类函数后面加上 **`override`**，编译器会检查它是否**确实重写了基类的某个虚函数**，没有就报错：

```cpp
class Circle : public Shape {
public:
    void draw() override;              // 错误：基类中没有可以重写的 draw()
};
```

```
error: non-virtual member function marked 'override' hides virtual member function
```

**规则：派生类中重写虚函数时，一律加上 `override`**，并且不必再写 `virtual`（`override` 已经说明了它是虚函数）。

## 4. final：禁止继承和重写

`final` 有两种用法：

```cpp
class Base final { };
class Derived : public Base { };       // 错误：Base 是 final

class Animal { public: virtual void speak() const; };
class Dog : public Animal { public: void speak() const final; };
class Puppy : public Dog { public: void speak() const override; };   // 错误：speak 已经是 final
```

```
error: base 'Base' is marked 'final'
error: declaration of 'speak' overrides a 'final' function
```

- **类后面加 `final`**：这个类不能被继承。
- **虚函数后面加 `final`**：这个函数不能再被派生类重写。

`final` 还有一个性能上的好处：编译器知道通过 `Dog*` 调用 `speak()` 时只可能是 `Dog::speak`，可以把虚函数调用优化成普通调用，这叫**去虚化**（devirtualization）。

> `override` 和 `final` 不是关键字，而是**有特殊含义的标识符**，只在特定位置才有特殊含义。所以老代码中名为 `final` 的变量不会因为升级到 C++11 而无法编译。

## 5. 委托构造函数

一个类有多个构造函数时，它们的初始化逻辑往往大量重复。C++11 允许一个构造函数**调用同一个类的另一个构造函数**，叫做**委托构造**：

```cpp
#include <iostream>
#include <string>

class Connection {
public:
    // 委托构造：其他构造函数都转交给这个“主构造函数”
    Connection(std::string host, int port, int timeout)
        : host_(std::move(host)), port_(port), timeout_(timeout)
    {
        std::cout << "  主构造函数: " << host_ << ':' << port_ << " timeout=" << timeout_ << '\n';
    }
    Connection(std::string host, int port) : Connection(std::move(host), port, 30)
    {
        std::cout << "  两参数版本（委托完成后再执行这里）\n";
    }
    Connection() : Connection("localhost", 80) {}

private:
    std::string host_;
    int port_;
    int timeout_;
};
```

`Connection c;` 的输出：

```
  主构造函数: localhost:80 timeout=30
  两参数版本（委托完成后再执行这里）
```

调用链是：默认构造 → 两参数版本 → 三参数版本。先执行被委托的构造函数（包括它的函数体），再回来执行委托者自己的函数体。

两条规则：

1. **委托构造的初始化列表里只能有委托调用**，不能再初始化其他成员。`Connection() : Connection("localhost", 80), timeout_(10) {}` 是错误的。
2. **不能形成循环**，比如 A 委托 B、B 又委托 A。编译器不一定能检测出来，运行时会无限递归。

## 6. 继承构造函数

派生类默认不会继承基类的构造函数。如果派生类只是增加了一些成员函数，没有新的成员变量，却要把基类的每个构造函数都手动「转发」一遍，非常繁琐。C++11 可以用 `using` 一次性继承基类的所有构造函数：

```cpp
class Base {
public:
    Base(int x) { std::cout << "  Base(int) x=" << x << '\n'; }
    Base(int x, double y) { std::cout << "  Base(int, double) x=" << x << " y=" << y << '\n'; }
};

class Derived : public Base {
public:
    using Base::Base;          // 继承构造函数：Derived 自动拥有 Base 的所有构造函数
    void extra() const { std::cout << "  Derived::extra\n"; }
};

Derived d1(1);
Derived d2(2, 3.5);
d2.extra();
```

运行结果：

```
  Base(int) x=1
  Base(int, double) x=2 y=3.5
  Derived::extra
```

注意：继承来的构造函数只负责初始化基类部分，**派生类自己新增的成员变量不会被它初始化**。如果派生类有新成员，要给它们写默认值（类内初始化），否则就是未初始化的。

## 7. explicit 类型转换运算符

很多类希望能在 `if` 中直接判断「是否有效」，比如文件、智能指针、流对象。如果提供一个普通的 `operator bool()`，会带来意想不到的隐式转换：

```cpp
#include <iostream>

class SmartFile {
public:
    explicit SmartFile(bool open) : open_(open) {}
    explicit operator bool() const { return open_; }   // 显式类型转换运算符
private:
    bool open_;
};

class BadFile {
public:
    explicit BadFile(bool open) : open_(open) {}
    operator bool() const { return open_; }            // 隐式的：危险
private:
    bool open_;
};

int main()
{
    SmartFile f(true);
    if (f) std::cout << "if (f)：可以在条件中直接使用\n";
    bool ok = static_cast<bool>(f);
    std::cout << "static_cast<bool>(f) = " << ok << '\n';
    // int n = f;           // 错误：不允许隐式转换

    BadFile a(true), b(true);
    int n = a + b;           // 能编译！两个 bool 转成 int 相加
    std::cout << "BadFile: a + b = " << n << "（毫无意义，但能编译）\n";
    return 0;
}
```

运行结果：

```
if (f)：可以在条件中直接使用
static_cast<bool>(f) = 1
BadFile: a + b = 2（毫无意义，但能编译）
```

C++11 允许给类型转换运算符加 `explicit`。加了之后，**只有在明确需要 `bool` 的场合**（`if`、`while`、`for` 的条件，`!`、`&&`、`||` 的操作数，三目运算符的条件）才会自动转换，这叫**上下文转换**（contextual conversion）；其他地方必须写 `static_cast`。标准库的 `std::unique_ptr`、`std::shared_ptr`、`std::function`、`std::ifstream` 的 `operator bool` 都是 `explicit` 的。

## 8. 常见错误

1. **以为 `= delete` 的函数不参与重载**：它参与，被选中就报错，这正是它能「禁止某些参数类型」的原因。
2. **委托构造的同时初始化其他成员**：不允许，见第 5 节。
3. **基类析构函数不是虚函数**：通过基类指针 `delete` 派生类对象时，只会调用基类的析构函数，派生类的资源不会被释放。有虚函数的基类，析构函数一定要写成 `virtual ~Base() = default;`。
4. **继承构造函数后忘记初始化新成员**：见第 6 节。

## 9. 进阶

### 9.1 用 = delete 禁止模板的某些实例化

`= delete` 也能用于函数模板的特化，禁止某些类型：

```cpp
template <typename T>
void process(T* ptr);

template <>
void process<void>(void*) = delete;    // 禁止传入 void*
template <>
void process<char>(char*) = delete;    // 禁止传入 char*（可能是 C 风格字符串）
```

### 9.2 特殊成员函数的生成规则

把上一篇《右值引用与移动语义》中提到的规则补充完整：

| 你声明了 | 默认构造函数 | 拷贝构造 / 拷贝赋值 | 移动构造 / 移动赋值 |
| --- | --- | --- | --- |
| 什么都没有 | 生成 | 生成 | 生成 |
| 析构函数 | 生成 | 生成（已弃用） | **不生成** |
| 拷贝赋值 | 生成 | 生成拷贝构造（已弃用） | **不生成** |
| 拷贝构造 | **不生成** | 生成拷贝赋值（已弃用） | **不生成** |
| 移动赋值 | 生成 | **删除** | 不生成移动构造 |
| 移动构造 | **不生成** | **删除** | 不生成移动赋值 |
| 其他任意构造函数 | **不生成** | 生成 | 生成 |

记忆要点：

1. **声明了任何构造函数（包括拷贝构造、移动构造），就不再生成默认构造函数。**
2. **声明了析构函数或任一拷贝操作，就不再生成移动操作**，「移动」会退化成拷贝。
3. **声明了任一移动操作，拷贝操作就被删除**，类变成只能移动。

表中的每一条都用 `std::is_default_constructible`、`std::is_copy_constructible` 等类型特征在 Clang 上验证过。「已弃用」指的是：标准认为这种情况下自动生成拷贝操作很可能是错误的（三五法则），只是为了兼容老代码还保留着。最简单的做法：**要么一个都不写（零法则），要么五个都显式写出来**，不需要自定义的就写 `= default` 或 `= delete`。

### 9.3 = default 放在类外

`= default` 也可以写在类外，这时生成的函数**不再是平凡的**，但可以把函数定义放到 .cpp 文件中，减少头文件的依赖。这在 Pimpl 惯用法中很常用：

```cpp
// widget.h
class Widget {
public:
    Widget();
    ~Widget();                         // 只声明
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;       // Impl 在这里是不完整类型
};

// widget.cpp
struct Widget::Impl { /* ... */ };
Widget::Widget() : impl_(new Impl) {}
Widget::~Widget() = default;           // 在这里 Impl 已经完整，unique_ptr 才能正确删除它
```

如果在头文件里写 `~Widget() = default;`，编译 `unique_ptr<Impl>` 的析构时 `Impl` 还是不完整类型，会编译失败。

## 10. 小结

| 特性 | 写法 | 作用 |
| --- | --- | --- |
| 默认实现 | `X() = default;` | 让编译器生成特殊成员函数，可以保持平凡性 |
| 删除函数 | `X(const X&) = delete;` | 禁止拷贝、禁止某些参数类型 |
| 重写检查 | `void f() override;` | 编译器检查是否真的重写了基类虚函数 |
| 禁止继承 / 重写 | `class X final`、`void f() final;` | 不能再被继承或重写，还能帮助去虚化 |
| 委托构造 | `X() : X(0, 0) {}` | 构造函数之间复用初始化逻辑 |
| 继承构造 | `using Base::Base;` | 派生类直接使用基类的构造函数 |
| 显式转换 | `explicit operator bool() const;` | 只在条件判断等场合自动转换 |

## 11. 练习

1. 写一个类 `Id`，只能用 `int` 构造，传入 `double`、`bool`、`char` 时编译失败。
2. 下面的代码中 `Derived::f` 能否重写 `Base::f`？加上 `override` 会怎样？

   ```cpp
   class Base { public: virtual void f(int) const; };
   class Derived : public Base { public: void f(long) const; };
   ```

3. 给下面的 `Rect` 用委托构造补上两个构造函数：`Rect()` 构造 0×0 的矩形，`Rect(int side)` 构造正方形。

   ```cpp
   class Rect {
   public:
       Rect(int w, int h) : w_(w), h_(h) {}
   private:
       int w_, h_;
   };
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   class Id {
   public:
       explicit Id(int v) : v_(v) {}
       Id(double) = delete;
       Id(bool) = delete;
       Id(char) = delete;
   private:
       int v_;
   };
   ```

   也可以用一个删除的模板一次禁止所有非 `int` 类型：`template <typename T> Id(T) = delete;`。

2. 不能。参数类型 `long` 和 `int` 不同，`Derived::f` 是一个新函数，它会**隐藏**基类的 `f`。加上 `override` 后编译失败，正好把这个错误暴露出来。
3. 参考代码：

   ```cpp
   Rect() : Rect(0, 0) {}
   explicit Rect(int side) : Rect(side, side) {}
   ```

</details>
