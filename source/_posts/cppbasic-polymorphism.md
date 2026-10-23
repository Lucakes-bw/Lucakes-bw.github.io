---
title: 多态与虚函数
date: 2026-10-23 11:40:00
updated: 2026-10-23 11:40:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 面向对象
  - 多态
order: 9
description: 多态让同一段代码对不同类型的对象表现出不同的行为，是面向对象最核心的特性。本文讲清楚虚函数和 override、实测虚表指针的存在、纯虚函数与抽象类，并逐一复现基类析构函数不是虚函数导致的内存泄漏、对象切片、构造函数中调用虚函数三大陷阱。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解静态绑定和动态绑定；会用 `virtual` 和 `override` 实现多态；理解虚函数表的原理；会定义抽象类和接口；知道为什么基类的析构函数必须是虚函数；避开对象切片和在构造函数中调用虚函数的陷阱。
>
> **前置知识**：上一篇《继承》，C++11 章节的《智能指针》（会用 `std::unique_ptr` 即可）。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 问题：通过基类调用，执行的却是基类的版本

上一篇的最后一道练习揭示了一个问题。下面的 `Shape` 和 `Circle` 都有 `area()` 函数：

```cpp
#include <iostream>
struct Shape { double area() const { return 0; } };                    // 没有 virtual
struct Circle : Shape { double r = 1; double area() const { return 3.14159 * r * r; } };
void printArea(const Shape& s) { std::cout << s.area() << '\n'; }
int main() { Circle c; std::cout << "直接调用: " << c.area() << "，通过 Shape&: "; printArea(c); }
```

运行结果：

```
直接调用: 3.14159，通过 Shape&: 0
```

`printArea` 的参数是 `Shape&`，传进去的明明是一个圆，`s.area()` 调用的却是 `Shape::area`，返回了 0。原因是：**普通成员函数的调用，在编译时就根据变量的类型确定了**。`s` 的类型是 `Shape&`，编译器就调用 `Shape::area`，不管 `s` 实际引用的是什么对象。这叫**静态绑定**。

我们想要的是：**根据对象实际的类型**来决定调用哪个函数。这叫**动态绑定**，也就是**多态**（polymorphism）。

## 2. 虚函数

在基类的成员函数前面加上 **`virtual`**，它就成了**虚函数**，调用时会根据对象的实际类型选择版本：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class Shape {
public:
    virtual ~Shape() = default;                                // 基类的析构函数要是虚函数
    virtual double area() const { return 0; }                  // 虚函数：派生类可以重写
    virtual std::string name() const { return "形状"; }
    void describe() const                                      // 普通函数中调用虚函数
    {
        std::cout << name() << "，面积 " << area() << '\n';
    }
};

class Circle : public Shape {
public:
    explicit Circle(double r) : r_(r) {}
    double area() const override { return 3.14159 * r_ * r_; }   // override：重写基类的虚函数
    std::string name() const override { return "圆"; }
private:
    double r_;
};

class Rect : public Shape {
public:
    Rect(double w, double h) : w_(w), h_(h) {}
    double area() const override { return w_ * h_; }
    std::string name() const override { return "矩形"; }
private:
    double w_, h_;
};

void printArea(const Shape& s)                                 // 参数是基类的引用
{
    std::cout << "  " << s.name() << ": " << s.area() << '\n';
}

int main()
{
    Circle c(1);
    Rect r(2, 3);
    std::cout << "通过基类引用调用:\n";
    printArea(c);                                              // 实际调用的是 Circle::area
    printArea(r);

    std::cout << "放进同一个容器:\n";
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Circle>(2));
    shapes.push_back(std::make_unique<Rect>(4, 5));
    shapes.push_back(std::make_unique<Circle>(0.5));
    double total = 0;
    for (const auto& s : shapes) {
        s->describe();
        total += s->area();
    }
    std::cout << "总面积 " << total << '\n';
    return 0;
}
```

运行结果：

```
通过基类引用调用:
  圆: 3.14159
  矩形: 6
放进同一个容器:
圆，面积 12.5664
矩形，面积 20
圆，面积 0.785397
总面积 33.3518
```

现在 `printArea` 能够正确地调用 `Circle::area` 和 `Rect::area` 了。这就是多态的威力：

- **`printArea` 只认识 `Shape`**，它根本不知道 `Circle` 和 `Rect` 的存在。将来增加一个 `Triangle` 类，`printArea` 一行都不用改，就能正确处理三角形。
- **不同类型的对象可以放在同一个容器中**统一处理。容器中存放的是基类的智能指针，每个元素实际指向不同类型的对象。
- **在基类的普通成员函数 `describe` 中调用虚函数**，同样会调用派生类的版本。

多态的三个条件：

1. 基类中的函数是 **`virtual`** 的；
2. 派生类**重写**（override）了这个函数，签名完全相同；
3. 通过**基类的指针或引用**调用。

**`override`** 关键字（C++11）告诉编译器「这个函数是要重写基类的虚函数」。如果签名写错了（比如漏了 `const`），没有真正重写成功，编译器会报错。**重写虚函数时一定要写 `override`**，详见 C++11 章节的《类的增强》。派生类中的 `virtual` 可以省略，写了 `override` 就说明它是虚函数。

## 3. 原理：虚函数表

编译器是怎么在运行时知道对象的实际类型的？答案是**虚函数表**（vtable）。来做个实验：

```cpp
#include <cstdint>
#include <cstring>
#include <iostream>

struct Plain   { double x; void f() {} };
struct Virtual { double x; virtual void f() {} virtual ~Virtual() = default; };
struct A : Virtual { void f() override {} };
struct B : Virtual { void f() override {} };

std::uintptr_t vptrOf(const void* obj)            // 读出对象开头 8 字节：虚表指针（仅用于观察）
{
    std::uintptr_t p;
    std::memcpy(&p, obj, sizeof p);
    return p;
}

int main()
{
    std::cout << "sizeof(Plain) = " << sizeof(Plain) << ", sizeof(Virtual) = " << sizeof(Virtual)
              << "（多了一个 8 字节的虚表指针）\n";
    A a1, a2;
    B b1;
    std::cout << std::hex
              << "a1 的虚表指针: 0x" << vptrOf(&a1) << '\n'
              << "a2 的虚表指针: 0x" << vptrOf(&a2) << "（同一个类的对象共用一张虚表）\n"
              << "b1 的虚表指针: 0x" << vptrOf(&b1) << "（不同的类有不同的虚表）\n";
    return 0;
}
```

运行结果（64 位系统，地址每次运行可能不同）：

```
sizeof(Plain) = 8, sizeof(Virtual) = 16（多了一个 8 字节的虚表指针）
a1 的虚表指针: 0x104aa4010
a2 的虚表指针: 0x104aa4010（同一个类的对象共用一张虚表）
b1 的虚表指针: 0x104aa4088（不同的类有不同的虚表）
```

> 直接读取对象的内存来获取虚表指针，依赖于具体编译器的实现，只用于观察原理，不要在实际代码中这样做。

实验说明了三件事：

1. 两个类都只有一个 `double` 成员，但**有虚函数的类多了 8 个字节**。
2. 多出来的是对象开头的一个指针，叫**虚表指针**（vptr）。`a1` 和 `a2` 的虚表指针**完全相同**。
3. `b1` 的虚表指针**不同**。

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="虚函数表示意图：每个对象开头有一个虚表指针，同一个类的对象指向同一张虚表，虚表中保存该类各个虚函数的地址">
  <defs>
    <marker id="vt-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/>
    </marker>
  </defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20" y="20" style="fill:var(--muted)">对象（每个对象一份）</text>
    <text x="400" y="20" style="fill:var(--muted)">虚函数表（每个类一张）</text>
    <rect x="20" y="35" width="150" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="30" y="55" style="fill:var(--accent)">A 对象 a1</text>
    <text x="30" y="80" style="fill:var(--fg)">vptr │ x</text>
    <rect x="20" y="105" width="150" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="30" y="125" style="fill:var(--accent)">A 对象 a2</text>
    <text x="30" y="150" style="fill:var(--fg)">vptr │ x</text>
    <rect x="20" y="175" width="150" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="30" y="195" style="fill:var(--warn)">B 对象 b1</text>
    <text x="30" y="220" style="fill:var(--fg)">vptr │ x</text>
    <rect x="400" y="50" width="270" height="80" rx="6" style="fill:var(--code-bg);stroke:var(--accent)"/>
    <text x="415" y="72" style="fill:var(--accent)">A 的虚表</text>
    <text x="415" y="96" style="fill:var(--fg)">[0] → A::f</text>
    <text x="415" y="118" style="fill:var(--fg)">[1] → A::~A</text>
    <rect x="400" y="155" width="270" height="80" rx="6" style="fill:var(--code-bg);stroke:var(--warn)"/>
    <text x="415" y="177" style="fill:var(--warn)">B 的虚表</text>
    <text x="415" y="201" style="fill:var(--fg)">[0] → B::f</text>
    <text x="415" y="223" style="fill:var(--fg)">[1] → B::~B</text>
    <line x1="170" y1="75" x2="398" y2="80" style="stroke:var(--fg-2)" marker-end="url(#vt-arrow)"/>
    <line x1="170" y1="145" x2="398" y2="95" style="stroke:var(--fg-2)" marker-end="url(#vt-arrow)"/>
    <line x1="170" y1="215" x2="398" y2="200" style="stroke:var(--fg-2)" marker-end="url(#vt-arrow)"/>
  </g>
</svg>
</figure>

- 编译器为**每个有虚函数的类**生成一张**虚函数表**，表中按顺序存放这个类的各个虚函数的地址。`A` 重写了 `f`，所以 `A` 的虚表中 `f` 的位置存放的是 `A::f` 的地址。
- 每个对象的开头有一个**虚表指针**，在构造对象时被设置为指向它所属类的虚表。同一个类的所有对象共用一张虚表。
- 调用 `p->f()` 时，编译器生成的代码是：**通过对象的虚表指针找到虚表，再从虚表的固定位置取出函数地址，然后调用**。不管 `p` 的类型是什么，找到的都是对象实际所属的类的函数。

所以虚函数的代价是：**每个对象多一个指针的空间**，**每次调用多两次内存读取**（读虚表指针、读函数地址），而且编译器通常无法把虚函数调用内联。对于绝大多数程序，这点开销完全可以忽略；但在每秒调用上亿次的热点代码中，可能需要考虑。

## 4. 纯虚函数与抽象类

`Shape::area()` 返回 0 其实没有意义：「形状」本身没有面积，只有具体的圆、矩形才有。可以把它声明为**纯虚函数**：

```cpp
class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;          // 纯虚函数
};
class Circle : public Shape {};               // 没有实现 area，仍然是抽象类
int main()
{
    Shape s;                                  // 错误：不能创建抽象类的对象
    Circle c;                                 // 错误：Circle 也是抽象类
}
```

```
error: variable type 'Shape' is an abstract class
note: unimplemented pure virtual method 'area' in 'Shape'
error: variable type 'Circle' is an abstract class
```

- `= 0` 表示**纯虚函数**：基类只声明「派生类必须提供这个函数」，自己不提供实现。
- 含有纯虚函数的类叫**抽象类**，**不能创建它的对象**，只能用它的指针或引用。
- 派生类**必须实现所有的纯虚函数**，否则它也是抽象类。编译器会准确地告诉你还有哪个函数没有实现。

**只包含纯虚函数（和虚析构函数）的抽象类，就是「接口」**。它只规定「能做什么」，不关心「怎么做」。比如一个日志系统可以定义 `Logger` 接口，有 `ConsoleLogger`、`FileLogger`、`NetworkLogger` 等不同的实现，使用日志的代码只依赖 `Logger&`，可以随时替换成任何一种实现。

## 5. 三大陷阱

### 5.1 基类的析构函数不是虚函数

```cpp
#include <iostream>

struct Base {
    ~Base() { std::cout << "  ~Base\n"; }                 // 不是虚函数！
};
struct Derived : Base {
    int* buffer = new int[1000];
    ~Derived() { delete[] buffer; std::cout << "  ~Derived（释放 buffer）\n"; }
};

struct VBase {
    virtual ~VBase() { std::cout << "  ~VBase\n"; }       // 虚析构函数
};
struct VDerived : VBase {
    int* buffer = new int[1000];
    ~VDerived() override { delete[] buffer; std::cout << "  ~VDerived（释放 buffer）\n"; }
};

int main()
{
    std::cout << "delete 一个指向 Derived 的 Base*:\n";
    Base* p = new Derived;
    delete p;                                             // 只调用了 ~Base

    std::cout << "delete 一个指向 VDerived 的 VBase*:\n";
    VBase* q = new VDerived;
    delete q;                                             // 先 ~VDerived，再 ~VBase
    return 0;
}
```

运行结果：

```
delete 一个指向 Derived 的 Base*:
  ~Base
delete 一个指向 VDerived 的 VBase*:
  ~VDerived（释放 buffer）
  ~VBase
```

用 `leaks` 工具检查：

```
Process 38314: 1 leak for 4096 total leaked bytes.
```

通过 `Base*` 删除一个 `Derived` 对象时，因为析构函数**不是虚函数**，`delete p` 静态绑定到了 `~Base`，**`~Derived` 根本没有被调用**，`buffer` 的 4000 字节就泄漏了（报告中的 4096 是分配器对齐后的大小）。严格来说，这种情况在 C++ 标准中是**未定义行为**。更危险的是，这段代码用 `-Wall -Wextra` 编译时**没有任何警告**。

**规则：只要一个类打算被继承、并且可能通过基类指针删除派生类对象，它的析构函数就必须是虚函数。** 简单的判断方法：类中有任何虚函数，析构函数就应该是虚函数。本文所有的基类都写了 `virtual ~Shape() = default;`。

### 5.2 对象切片

```cpp
#include <iostream>
#include <string>
#include <vector>

struct Animal {
    virtual ~Animal() = default;
    virtual std::string sound() const { return "……"; }
};
struct Dog : Animal {
    std::string name = "旺财";
    std::string sound() const override { return "汪汪（" + name + "）"; }
};

void byValue(Animal a)        { std::cout << "  按值:   " << a.sound() << '\n'; }
void byRef(const Animal& a)   { std::cout << "  按引用: " << a.sound() << '\n'; }

int main()
{
    Dog d;
    byValue(d);                        // 只拷贝了 Animal 部分：切片
    byRef(d);

    std::vector<Animal> zoo;           // 存放 Animal 对象的容器
    zoo.push_back(d);                  // 同样被切片
    std::cout << "  vector<Animal>: " << zoo[0].sound() << '\n';
    return 0;
}
```

运行结果：

```
  按值:   ……
  按引用: 汪汪（旺财）
  vector<Animal>: ……
```

把 `Dog` 对象**按值**传给 `Animal` 参数时，只有 `Animal` 部分被拷贝了，`Dog` 特有的部分（`name` 成员）被「切掉」了，拷贝出来的是一个真正的 `Animal` 对象，它的虚表指针指向 `Animal` 的虚表，所以调用的是 `Animal::sound`。这叫**对象切片**（object slicing）。

`std::vector<Animal>` 中存放的也是 `Animal` 对象，`push_back(d)` 同样发生了切片。

**规则：多态只能通过指针或引用实现。** 函数参数用 `const Base&`；容器中存放 `std::unique_ptr<Base>`（像第 2 节那样），而不是 `Base` 对象。为了防止意外切片，抽象基类（有纯虚函数）本身就不能按值传递，编译器会报错，这也是把基类设计为抽象类的好处之一。

### 5.3 在构造函数中调用虚函数

```cpp
#include <iostream>
#include <string>

struct Base {
    Base() { std::cout << "Base 构造函数中调用 name(): " << name() << '\n'; }
    virtual ~Base() = default;
    virtual std::string name() const { return "Base"; }
};
struct Derived : Base {
    std::string label = "Derived";
    std::string name() const override { return label; }
};

int main()
{
    Derived d;
    std::cout << "构造完成后调用 name(): " << d.name() << '\n';
    return 0;
}
```

运行结果：

```
Base 构造函数中调用 name(): Base
构造完成后调用 name(): Derived
```

在 `Base` 的构造函数中调用虚函数 `name()`，执行的是 `Base::name`，而不是 `Derived::name`。上一篇讲过，构造派生类对象时**先构造基类部分**，`Base` 的构造函数执行时，`Derived` 的成员（比如 `label`）还没有被构造。如果这时调用了 `Derived::name`，它就会访问一个还不存在的 `label`。所以 C++ 规定：**在构造函数（和析构函数）中，虚函数不会表现出多态**，对象此时的类型就是正在构造的那个类。在实现上，这是因为构造 `Base` 部分时，虚表指针指向的是 `Base` 的虚表，直到 `Derived` 的构造函数开始执行，它才被改为指向 `Derived` 的虚表。

**规则：不要在构造函数和析构函数中调用虚函数。**

## 6. 常见错误

1. **基类析构函数不是虚函数**：通过基类指针删除派生类对象时泄漏资源，见第 5.1 节。
2. **重写时签名不一致**：少写了 `const`、参数类型不同，结果没有重写成功。用 `override` 让编译器检查。
3. **按值传递多态对象**：对象切片，见第 5.2 节。
4. **容器中直接存放基类对象**：同样是切片，应该存放智能指针。
5. **在构造函数或析构函数中调用虚函数**：见第 5.3 节。

## 7. 小结

- 普通成员函数是**静态绑定**的，根据变量的类型调用；**虚函数**是**动态绑定**的，根据对象的实际类型调用
- 多态的条件：基类函数是 `virtual`，派生类用 `override` 重写，通过基类的指针或引用调用
- 原理：每个有虚函数的类有一张**虚函数表**，每个对象开头有一个**虚表指针**；代价是一个指针的空间和一次间接调用
- 纯虚函数 `= 0` 使类成为**抽象类**，不能创建对象；只有纯虚函数的抽象类就是接口
- **基类的析构函数必须是虚函数**，否则通过基类指针删除派生类对象会泄漏资源（实测泄漏 4096 字节，而且没有编译警告）
- 按值传递会导致**对象切片**；多态只能通过指针或引用实现
- 不要在构造函数和析构函数中调用虚函数

## 8. 练习

1. 定义一个抽象类 `Logger`，有纯虚函数 `log(const std::string& msg)`，实现 `ConsoleLogger`（输出到屏幕）和 `PrefixLogger`（在每条消息前加上一个前缀，前缀在构造时指定）。写一个函数 `void work(Logger& logger)`，分别传入两种日志器。
2. 下面的代码有几处问题？

   ```cpp
   class Base {
   public:
       virtual void run(int x) const;
       ~Base();
   };
   class Derived : public Base {
   public:
       void run(int x);
   };
   void call(Base b) { b.run(1); }
   ```

3. 一个有 1000 万个对象的数组，每个对象只有一个 `int` 成员。如果给这个类加上一个虚函数，数组大约要多占用多少内存？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   class Logger {
   public:
       virtual ~Logger() = default;
       virtual void log(const std::string& msg) = 0;
   };
   class ConsoleLogger : public Logger {
   public:
       void log(const std::string& msg) override { std::cout << msg << '\n'; }
   };
   class PrefixLogger : public Logger {
   public:
       explicit PrefixLogger(std::string prefix) : prefix_(std::move(prefix)) {}
       void log(const std::string& msg) override { std::cout << prefix_ << msg << '\n'; }
   private:
       std::string prefix_;
   };
   void work(Logger& logger) { logger.log("开始工作"); logger.log("工作完成"); }
   ```

2. 三处：① `Base` 的析构函数不是虚函数；② `Derived::run` 少了 `const`，没有重写 `Base::run`，而是隐藏了它，加上 `override` 编译器就会报错；③ `call` 按值传递 `Base`，会发生对象切片，应该改为 `const Base&`。

3. 原来每个对象 4 字节；加上虚函数后多了一个 8 字节的虚表指针，再按 8 字节对齐，每个对象变成 16 字节。1000 万个对象从约 40MB 增加到约 160MB，多了约 120MB。所以，大量的小对象不适合使用虚函数。

</details>
