---
title: 继承
date: 2026-10-23 11:20:00
updated: 2026-10-23 11:20:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 面向对象
  - 继承
order: 8
description: 继承让一个类在另一个类的基础上扩展，表达「是一种」的关系。本文讲清楚派生类的定义、protected 访问控制、构造和析构的顺序、名字隐藏的陷阱，以及多重继承中的菱形问题和虚继承。
disableNunjucks: true
---

> **本文目标**：会定义派生类，理解「是一种」的关系；会用 `protected`；会在派生类构造函数中初始化基类；知道构造和析构的顺序；理解名字隐藏，会用 `using` 解决；了解多重继承和菱形继承问题。
>
> **前置知识**：本章《类与对象》《构造与析构》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 为什么需要继承

公司里有普通员工和经理。经理也是员工：有名字、有工资、能自我介绍，此外还有团队规模、能给自己加薪……如果写两个完全独立的类，名字、工资、自我介绍这些代码就要写两遍，修改时也要改两处。

**继承**（inheritance）让一个类在另一个类的基础上扩展：

- 被继承的类叫**基类**（base class），也叫父类；
- 新的类叫**派生类**（derived class），也叫子类。派生类自动拥有基类的成员，并可以增加自己的成员。

继承表达的是「**是一种**」（is-a）的关系：经理**是一种**员工，所以 `Manager` 可以继承 `Employee`。

## 2. 定义派生类

```cpp
#include <iostream>
#include <string>

class Employee {                                     // 基类
public:
    Employee(std::string name, double salary) : salary_(salary), name_(std::move(name)) {}
    const std::string& name() const { return name_; }
    double salary() const { return salary_; }
    void introduce() const { std::cout << "我是 " << name_ << "，月薪 " << salary_ << '\n'; }
protected:
    double salary_;                                  // protected：派生类可以访问，外部不行
private:
    std::string name_;                               // private：只有 Employee 自己能访问
};

class Manager : public Employee {                    // 派生类：Manager 是一种 Employee
public:
    Manager(std::string name, double salary, int teamSize)
        : Employee(std::move(name), salary), teamSize_(teamSize) {}   // 先构造基类部分
    void giveBonus(double pct) { salary_ *= 1 + pct; }               // 可以访问基类的 protected 成员
    int teamSize() const { return teamSize_; }
private:
    int teamSize_;
};

int main()
{
    Employee e("Alice", 8000);
    Manager m("Bob", 15000, 6);

    e.introduce();
    m.introduce();                                   // 继承来的成员函数
    m.giveBonus(0.1);
    std::cout << m.name() << " 管理 " << m.teamSize() << " 人，加薪后月薪 " << m.salary() << '\n';

    Employee& ref = m;                               // 派生类对象可以当作基类对象使用
    std::cout << "通过 Employee& 访问: " << ref.name() << '\n';
    std::cout << "sizeof(Employee) = " << sizeof(Employee) << ", sizeof(Manager) = " << sizeof(Manager) << '\n';
    return 0;
}
```

运行结果：

```
我是 Alice，月薪 8000
我是 Bob，月薪 15000
Bob 管理 6 人，加薪后月薪 16500
通过 Employee& 访问: Bob
sizeof(Employee) = 32, sizeof(Manager) = 40
```

要点：

1. **`class Manager : public Employee`**：`Manager` 公有继承 `Employee`。`m.introduce()`、`m.name()` 这些成员函数都是从 `Employee` 继承来的，`Manager` 中一行都没写。
2. **构造派生类时，必须先构造基类部分**。在派生类构造函数的初始化列表中，用 `Employee(name, salary)` 调用基类的构造函数。如果不写，编译器会尝试调用基类的默认构造函数，`Employee` 没有默认构造函数，就会编译失败。
3. **派生类对象可以当作基类对象使用**：`Employee& ref = m;` 是合法的，因为经理就是一种员工。这是下一篇「多态」的基础。
4. **派生类对象包含一个完整的基类对象**：`Manager` 的大小是 `Employee`（32 字节）加上自己的 `teamSize_`（4 字节），再对齐到 40 字节。

```
Manager 对象的内存：
┌──────────────────────────┐
│ Employee 部分             │  salary_、name_（32 字节）
├──────────────────────────┤
│ Manager 自己的部分        │  teamSize_（4 字节 + 4 字节对齐填充）
└──────────────────────────┘
```

> 编写这个示例时，我最初把 `Employee` 构造函数的初始化列表写成了 `name_(...), salary_(...)`，而类中先声明的是 `salary_`，编译器立刻给出了 `-Wreorder-ctor` 警告。这正是《构造与析构》中讲过的「成员按声明顺序初始化」，所以示例中调整了初始化列表的顺序。

## 3. 访问控制

### 3.1 protected

```cpp
class Manager : public Employee {
public:
    void rename() { name_ = "new"; }        // 错误：基类的 private 成员在派生类中也不能访问
};
int main()
{
    Manager m;
    m.salary_ = 0;                          // 错误：protected 成员在外部不能访问
}
```

```
error: 'name_' is a private member of 'Employee'
error: 'salary_' is a protected member of 'Employee'
```

| 基类成员 | 基类自己 | 派生类 | 外部代码 |
| --- | --- | --- | --- |
| `public` | 可以 | 可以 | 可以 |
| `protected` | 可以 | **可以** | 不能 |
| `private` | 可以 | **不能** | 不能 |

**派生类也不能访问基类的 `private` 成员**。派生类对象中确实包含 `name_`（它占着空间），但只能通过基类提供的 `name()` 访问。`protected` 是专门为派生类开的「后门」。

`protected` 成员变量要谨慎使用：它意味着所有的派生类都可以随意修改它，基类就无法保证它的合法性了。更好的做法通常是：成员变量仍然 `private`，给派生类提供 `protected` 的成员**函数**。

### 3.2 三种继承方式

`class Manager : public Employee` 中的 `public` 是**继承方式**。还有 `protected` 继承和 `private` 继承，它们会把基类的 `public` 成员在派生类中变成 `protected` 或 `private`。**绝大多数情况下使用 `public` 继承**；`private` 继承表示「用……实现」而不是「是一种」，通常可以用组合（把对象作为成员）代替。

注意：`class` 默认是 `private` 继承，`struct` 默认是 `public` 继承。写 `class Manager : Employee`（漏掉了 `public`）时，就成了 `private` 继承，外部无法通过 `Manager` 调用 `name()`，也不能把 `Manager&` 转换成 `Employee&`。

## 4. 构造与析构的顺序

```cpp
#include <iostream>

struct Engine {
    Engine()  { std::cout << "  构造 Engine（成员）\n"; }
    ~Engine() { std::cout << "  析构 Engine（成员）\n"; }
};

struct Vehicle {
    Vehicle()  { std::cout << "  构造 Vehicle（基类）\n"; }
    ~Vehicle() { std::cout << "  析构 Vehicle（基类）\n"; }
};

struct Car : Vehicle {
    Engine engine;
    Car()  { std::cout << "  构造 Car（派生类自己）\n"; }
    ~Car() { std::cout << "  析构 Car（派生类自己）\n"; }
};

int main()
{
    std::cout << "创建 Car:\n";
    {
        Car c;
        std::cout << "销毁 Car:\n";
    }
    return 0;
}
```

运行结果：

```
创建 Car:
  构造 Vehicle（基类）
  构造 Engine（成员）
  构造 Car（派生类自己）
销毁 Car:
  析构 Car（派生类自己）
  析构 Engine（成员）
  析构 Vehicle（基类）
```

- **构造顺序**：先基类，再成员，最后派生类自己的构造函数体。就像盖房子，先打地基，再盖上层。派生类的构造函数体执行时，基类部分和所有成员都已经构造好了，可以放心使用。
- **析构顺序与构造完全相反**：先析构派生类自己，再析构成员，最后析构基类。派生类的析构函数执行时，基类部分仍然完好。

## 5. 名字隐藏

```cpp
struct Printer {
    void print(int) {}
    void print(double) {}
};
struct ColorPrinter : Printer {
    void print(const char*) {}
};
int main()
{
    ColorPrinter c;
    c.print(42);           // 错误：派生类的 print 隐藏了基类所有的 print
}
```

```
error: cannot initialize a parameter of type 'const char *' with an rvalue of type 'int'
note: passing argument to parameter here
```

`ColorPrinter` 只是**新增**了一个参数为 `const char*` 的 `print`，按理说继承来的 `print(int)` 应该还在。但编译器报错说「不能把 `int` 转换成 `const char*`」，说明它**只看到了派生类中的 `print`**。

这是 C++ 的**名字隐藏**规则：编译器查找名字时，先在派生类中找，**只要找到了这个名字，就不再到基类中查找**，然后只在找到的这些函数之间做重载决议。派生类中的 `print` 把基类中**所有**同名的 `print` 都隐藏了，不管参数是否相同。

解决办法是用 `using` 把基类的同名函数引入派生类：

```cpp
#include <iostream>

struct Printer {
    void print(int x)    { std::cout << "Printer::print(int) " << x << '\n'; }
    void print(double x) { std::cout << "Printer::print(double) " << x << '\n'; }
};

struct FixedPrinter : Printer {
    using Printer::print;                                  // 把基类的同名函数引入进来
    void print(const char* s) { std::cout << "FixedPrinter::print(const char*) " << s << '\n'; }
};

int main()
{
    FixedPrinter f;
    f.print(42);
    f.print(3.14);
    f.print("hello");
    return 0;
}
```

运行结果：

```
Printer::print(int) 42
Printer::print(double) 3.14
FixedPrinter::print(const char*) hello
```

## 6. 多重继承与菱形问题

C++ 允许一个类同时继承多个基类：`class C : public A, public B`。但这可能导致**菱形继承**问题：

```cpp
#include <iostream>
struct Device { int id = 0; };
struct Scanner : Device {};
struct Copier  : Device {};
struct Multifunction : Scanner, Copier {};          // 菱形继承：Device 出现了两份

struct VScanner : virtual Device {};
struct VCopier  : virtual Device {};
struct VMultifunction : VScanner, VCopier {};       // 虚继承：Device 只有一份

int main()
{
    Multifunction m;
    m.Scanner::id = 1;                               // 必须指明是哪一份
    m.Copier::id = 2;
    std::cout << "两份 id: " << m.Scanner::id << ", " << m.Copier::id << '\n';
    VMultifunction v;
    v.id = 3;                                        // 只有一份，可以直接访问
    std::cout << "虚继承后 id = " << v.id << '\n';
    std::cout << "sizeof(Multifunction) = " << sizeof(Multifunction)
              << ", sizeof(VMultifunction) = " << sizeof(VMultifunction) << '\n';
    return 0;
}
```

运行结果：

```
两份 id: 1, 2
虚继承后 id = 3
sizeof(Multifunction) = 8, sizeof(VMultifunction) = 24
```

```
        Device                 
        /    \                  Multifunction 中有两个 Device 子对象：
   Scanner  Copier              一个来自 Scanner，一个来自 Copier
        \    /
    Multifunction
```

一体机既是扫描仪又是复印机，它们都继承自 `Device`，于是 `Multifunction` 中有**两份** `Device`，有两个 `id`。直接写 `m.id` 会报错：

```
error: non-static member 'id' found in multiple base-class subobjects of type 'Device'
```

**虚继承**（`virtual Device`）让共同的基类只保留一份，`v.id` 就没有歧义了。代价是对象中需要额外保存定位虚基类的信息，`VMultifunction` 从 8 字节变成了 24 字节，访问虚基类的成员也多了一次间接寻址。

**建议：尽量避免多重继承，尤其是菱形继承。** 常见的、安全的多重继承用法是：一个「真正的」基类，加上若干个只包含纯虚函数的「接口类」（见下一篇）。标准库中的 `std::iostream` 就使用了虚继承，它同时继承自 `std::istream` 和 `std::ostream`。

## 7. 继承还是组合

继承不是代码复用的唯一手段，很多时候**组合**更合适：

- **继承**（is-a）：经理**是一种**员工，`class Manager : public Employee`。
- **组合**（has-a）：汽车**有一个**引擎，`class Car { Engine engine_; };`。

判断方法：派生类对象能否在**所有**场合下代替基类对象使用？如果能，用继承；否则用组合。一个经典的反例：正方形是一种长方形吗？数学上是，但如果长方形有 `setWidth` 和 `setHeight` 两个独立的操作，正方形就无法满足「修改宽度不影响高度」这个约定，所以 `Square` 不应该继承 `Rectangle`。

**优先使用组合，只在真正的「是一种」关系时使用继承。**

## 8. 常见错误

1. **把 `public` 继承写漏了**：`class D : B` 是 `private` 继承。
2. **派生类构造函数中没有初始化基类**，而基类又没有默认构造函数。
3. **以为派生类能访问基类的 `private` 成员**：只能访问 `public` 和 `protected` 成员。
4. **在派生类中定义了同名函数，导致基类的重载版本被隐藏**：用 `using 基类::函数名;` 引入。
5. **为了复用代码而滥用继承**：不满足「是一种」的关系时，用组合。

## 9. 小结

- `class D : public B` 表示 D **是一种** B；派生类自动拥有基类的成员，并可以扩展
- 派生类可以访问基类的 `public` 和 `protected` 成员，不能访问 `private` 成员
- 派生类构造函数在初始化列表中调用基类构造函数；派生类对象可以当作基类对象使用
- 构造顺序：基类 → 成员 → 派生类；析构顺序与之相反
- 派生类中的同名函数会**隐藏**基类所有的同名函数，用 `using` 引入
- 菱形继承导致基类出现多份，虚继承可以解决，但应尽量避免
- 优先使用组合，继承只用于真正的「是一种」关系

## 10. 练习

1. 设计 `Shape` 基类（有 `name` 成员）和 `Circle`、`Rectangle` 两个派生类，各自保存半径或宽高，并提供 `area()` 成员函数。用 `Shape&` 引用一个 `Circle` 对象，能调用 `area()` 吗？为什么？
2. 给本文的 `Car` 增加一个 `Wheel wheels[4];` 成员，`Wheel` 的构造和析构都打印信息。预测构造和析构的完整输出顺序，再运行验证。
3. 下面的代码输出什么？

   ```cpp
   struct Base { void show() { std::cout << "Base\n"; } };
   struct Derived : Base { void show() { std::cout << "Derived\n"; } };
   Derived d;
   Base& b = d;
   d.show();
   b.show();
   ```

<details>
<summary>点击查看答案</summary>

1. 不能。`area()` 是 `Circle` 中新增的成员函数，`Shape` 中没有，通过 `Shape&` 只能调用 `Shape` 中声明的函数。要通过基类引用调用派生类的实现，需要下一篇介绍的虚函数：在 `Shape` 中声明 `virtual double area() const`。

2. 构造：`Vehicle` → `Engine` → 4 个 `Wheel`（数组元素按下标顺序构造）→ `Car`；析构顺序完全相反：`Car` → 4 个 `Wheel`（从最后一个开始）→ `Engine` → `Vehicle`。成员按照在类中**声明的顺序**构造，`engine` 声明在 `wheels` 前面，所以先构造 `engine`。

3. 输出 `Derived` 和 `Base`。`Derived::show` 隐藏了 `Base::show`，通过 `Derived` 对象调用的是 `Derived::show`；而 `b` 的类型是 `Base&`，普通成员函数根据**变量的类型**（而不是实际对象的类型）决定调用哪个版本，所以调用了 `Base::show`。这正是下一篇要解决的问题。

</details>
