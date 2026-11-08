---
title: UML 类图
date: 2026-11-08 10:00:00
updated: 2026-11-08 10:00:00
categories:
  - 设计模式
  - 基础与设计原则
tags:
  - 设计模式
  - UML
  - 面向对象
order: 2
description: 设计模式的书和文档几乎都用类图说话。本文讲清楚类图里的每一个符号：类的三栏、可见性、静态和抽象成员、接口，以及最容易混的六种关系（泛化、实现、关联、聚合、组合、依赖），每一种都对应到具体的 C++ 写法；用一个运行程序实测组合和聚合的区别：车没了，发动机跟着析构，轮子还在。最后用一段播放器代码，一步步画出它的类图。
top_group_list: true
disableNunjucks: true
viz: [dp-uml]
uml_viz: true
---

> **本文目标**：看懂类图中类的画法（三栏、可见性、静态成员、抽象类、接口）；分清六种关系的符号和箭头方向；能根据 C++ 代码判断两个类之间是什么关系，并画出类图；反过来，能根据类图写出 C++ 代码。
>
> **前置知识**：上一篇《为什么需要设计模式》；C/C++ 专栏的《继承》《多态与虚函数》《智能指针》（知道 `unique_ptr` 和 `shared_ptr` 的区别即可）。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/02-uml/` 目录。

## 1. 为什么要画类图

后面讲每一个设计模式，都会先给一张类图。原因很简单：设计模式讲的是**类和类之间怎么配合**，而这种信息分散在代码的各个角落。一个模式的示例代码往往有五六个类、一两百行，读者要来回翻好几遍，才能在脑子里拼出「谁继承谁、谁持有谁、谁调用谁」。类图把这些信息压缩到一张图里，只保留结构，去掉实现细节。

UML（Unified Modeling Language，统一建模语言）定义了十几种图，日常用得最多的只有两种：描述静态结构的**类图**，描述调用顺序的**时序图**。本文只讲类图，时序图在讲观察者、命令等行为型模式时再介绍。

> 不用追求画得「标准」。类图是用来交流的，能让别人一眼看懂结构就够了。本文只讲最常用的那部分符号，掌握它们足以读懂任何一本设计模式的书。

## 2. 类的画法

### 2.1 三栏

一个类画成一个矩形，从上到下分成三栏：**类名**、**属性**（成员变量）、**操作**（成员函数）。

```uml
label: 一个类分成三栏：类名、属性、操作
classes:
  - id: Account
    x: 250
    y: 16
    attrs: ['- balance_ : double', '$- count_ : int']
    ops: ['+ Account(owner: string)', '+ deposit(amount: double) : void', '+ balance() : double', '# log(msg: string) : void']
notes:
  - { x: 20, y: 18, text: '类名', to: Account }
  - { x: 20, y: 52, text: '属性（成员变量）', to: Account }
  - { x: 20, y: 114, text: '操作（成员函数）', to: Account }
```

它对应的 C++ 代码：

<!-- run: off -->
```cpp
class Account {
public:
    explicit Account(std::string owner);
    void deposit(double amount);
    double balance() const;
protected:
    void log(const std::string& msg);
private:
    double balance_ = 0;
    static int count_;
};
```

几条规则：

- **可见性**写在最前面：`+` public，`-` private，`#` protected。
- **类型写在后面**，用冒号隔开：`balance_ : double`，`deposit(amount: double) : void`。这和 C++ 的顺序正好相反，第一次看会有点别扭。返回 `void` 的函数可以省略 `: void`。
- **静态成员加下划线**，比如 `count_`。
- `const`、引用、指针这些 C++ 细节，需要强调时就写上（`log(msg: const string&)`），不需要时就省略。
- 两栏都可以**按需省略**。画一个模式的结构时，通常只画和模式有关的那一两个函数，属性栏经常整栏不画。

### 2.2 抽象类和接口

**抽象类**的类名用**斜体**，纯虚函数也用斜体。**接口**在类名上方加上 `«interface»`。C++ 没有 `interface` 关键字，一个只有纯虚函数（外加虚析构函数）的类就是接口。

```uml
label: 抽象类 Shape 的类名和纯虚函数是斜体；接口 Drawable 带 «interface»；Circle 继承 Shape，同时实现 Drawable
classes:
  - { id: Shape, stereo: abstract, x: 40, y: 20, w: 190, attrs: [], ops: ['*+ area() : double', '+ name() : string'] }
  - { id: Drawable, stereo: interface, x: 400, y: 20, w: 190, ops: ['*+ draw() : void'] }
  - { id: Circle, x: 220, y: 190, w: 190, attrs: ['- r_ : double'], ops: ['+ area() : double', '+ draw() : void'] }
rels:
  - { from: Circle, to: Shape, type: inherit, fdx: -40 }
  - { from: Circle, to: Drawable, type: realize, fdx: 40 }
```

<!-- run: off -->
```cpp
class Shape {                          // 抽象类：有纯虚函数，也有普通函数
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
    std::string name() const { return "shape"; }
};

class Drawable {                       // 接口：只有纯虚函数
public:
    virtual ~Drawable() = default;
    virtual void draw() const = 0;
};

class Circle : public Shape, public Drawable {
public:
    double area() const override;
    void draw() const override;
private:
    double r_ = 1;
};
```

手写的时候斜体不好画，也可以在类名或函数后面写 `{abstract}`。

### 2.3 C++ 成员到类图的对照

| C++ | 类图 |
| --- | --- |
| `public:` / `private:` / `protected:` | `+` / `-` / `#` |
| `int count;` | `count : int` |
| `double area(double r) const;` | `area(r: double) : double` |
| `static int count_;` | 加下划线 |
| `virtual double area() const = 0;` | 斜体，或者后面写 `{abstract}` |
| 有纯虚函数的类 | 类名斜体 |
| 只有纯虚函数的类 | 类名上方写 `«interface»` |
| 构造函数 | `+ Account(owner: string)`，不写返回类型 |
| `template <typename T> class Stack` | 类名写成 `Stack<T>` |

## 3. 六种关系

类图真正有用的地方是**类与类之间的连线**。UML 定义了六种关系，按照耦合从强到弱排列：

```uml
label: 六种关系的符号：泛化是实线加空心三角；实现是虚线加空心三角；组合是实心菱形；聚合是空心菱形；关联是实线箭头；依赖是虚线箭头
width: 740
classes:
  - { id: Circle, x: 20, y: 24, w: 100 }
  - { id: Shape, x: 230, y: 24, w: 100 }
  - { id: FileLog, x: 400, y: 24, w: 100 }
  - { id: Logger, x: 610, y: 24, w: 100 }
  - { id: Car, x: 20, y: 104, w: 100 }
  - { id: Engine, x: 230, y: 104, w: 100 }
  - { id: Team, x: 400, y: 104, w: 100 }
  - { id: Member, x: 610, y: 104, w: 100 }
  - { id: Order, x: 20, y: 184, w: 100 }
  - { id: Customer, x: 230, y: 184, w: 100 }
  - { id: Player, x: 400, y: 184, w: 100 }
  - { id: Log, name: Logger, x: 610, y: 184, w: 100 }
rels:
  - { from: Circle, to: Shape, type: inherit, label: '① 泛化' }
  - { from: FileLog, to: Logger, type: realize, label: '② 实现' }
  - { from: Car, to: Engine, type: compose, label: '③ 组合' }
  - { from: Team, to: Member, type: aggregate, label: '④ 聚合' }
  - { from: Order, to: Customer, type: assoc, label: '⑤ 关联' }
  - { from: Player, to: Log, type: depend, label: '⑥ 依赖' }
texts:
  - { x: 175, y: 72, text: 'public 继承', anchor: middle, muted: true }
  - { x: 555, y: 72, text: '继承纯虚接口', anchor: middle, muted: true }
  - { x: 175, y: 152, text: '值成员 / unique_ptr', anchor: middle, muted: true }
  - { x: 555, y: 152, text: 'shared_ptr / 外部传入', anchor: middle, muted: true }
  - { x: 175, y: 232, text: '指针或引用成员', anchor: middle, muted: true }
  - { x: 555, y: 232, text: '参数、局部变量、返回值', anchor: middle, muted: true }
```

记符号只要记住两个规律：

- **箭头（三角）指向「被依赖」的一方**：子类指向父类，实现类指向接口，使用者指向被使用者。可以读成「Circle **是一种** Shape」「Player **用到了** Logger」。
- **菱形画在「整体」一端**：Car 拥有 Engine，菱形就画在 Car 旁边。实心菱形（组合）比空心菱形（聚合）拥有得更彻底。

下面逐个看每种关系对应什么样的 C++ 代码。

### 3.1 泛化（继承）

**泛化**（generalization）就是继承，表示「是一种」（is-a）。实线加空心三角，三角指向父类。

<!-- run: off -->
```cpp
class Shape { /* ... */ };
class Circle : public Shape { /* ... */ };       // Circle 是一种 Shape
class Rect   : public Shape { /* ... */ };
```

多个子类继承同一个父类时，习惯把它们的线汇合成一棵树：

```uml
label: Circle、Rect、Triangle 三个子类的继承线汇合到一起，指向父类 Shape
classes:
  - { id: Shape, stereo: abstract, x: 230, y: 16, w: 160, ops: ['*+ area() : double'] }
  - { id: Circle, x: 40, y: 150, w: 140, ops: ['+ area() : double'] }
  - { id: Rect, x: 240, y: 150, w: 140, ops: ['+ area() : double'] }
  - { id: Triangle, x: 440, y: 150, w: 140, ops: ['+ area() : double'] }
rels:
  - { from: Circle, to: Shape, type: inherit, mid: 115 }
  - { from: Rect, to: Shape, type: inherit, mid: 115 }
  - { from: Triangle, to: Shape, type: inherit, mid: 115 }
```

注意只有 **public 继承**才是「是一种」。`private` 继承在 C++ 里的含义是「用它来实现」，从设计上看更接近组合，画类图时一般不画成泛化。

### 3.2 实现

**实现**（realization）表示一个类实现了接口规定的操作。虚线加空心三角，三角指向接口。

<!-- run: off -->
```cpp
class Logger {                                   // 接口
public:
    virtual ~Logger() = default;
    virtual void write(const std::string& msg) = 0;
};
class FileLogger : public Logger {               // FileLogger 实现了 Logger
public:
    void write(const std::string& msg) override;
};
```

在 C++ 里，泛化和实现写出来都是 `: public`，区别只在于父类是不是接口。父类有数据成员、有已经实现的函数，子类继承的是「实现」，画实线；父类只有纯虚函数，子类继承的只是「承诺」，画虚线。

### 3.3 组合

**组合**（composition）表示「整体拥有部分」，而且**部分不能脱离整体单独存在**：整体创建时部分跟着创建，整体销毁时部分跟着销毁。实线，实心菱形画在整体一端。

C++ 中，组合对应**值成员**或**由整体自己创建的 `unique_ptr`**：

<!-- run: off -->
```cpp
class Car {
private:
    Engine engine_;                              // 值成员：Car 析构时 engine_ 一定析构
    std::unique_ptr<Gearbox> gearbox_;           // 独占：Car 析构时 unique_ptr 释放 Gearbox
    std::vector<Seat> seats_;                    // 一组值成员，也是组合
};
```

```uml
label: Car 组合 Engine（1 个）、Gearbox（1 个）和 Seat（多个），实心菱形都在 Car 一端
classes:
  - { id: Car, x: 20, y: 50, attrs: ['- engine_ : Engine', '- gearbox_ : unique_ptr<Gearbox>', '- seats_ : vector<Seat>'] }
  - { id: Engine, x: 430, y: 10, w: 110 }
  - { id: Gearbox, x: 430, y: 80, w: 110 }
  - { id: Seat, x: 430, y: 150, w: 110 }
rels:
  - { from: Car, to: Engine, type: compose, fdx: -30, tm: '1', mid: 350 }
  - { from: Car, to: Gearbox, type: compose, tm: '1' }
  - { from: Car, to: Seat, type: compose, fdx: 30, tm: '*', mid: 350 }
```

图中 `Seat` 一端的 `*` 是**多重性**，第 4 节再讲。

### 3.4 聚合

**聚合**（aggregation）也表示「整体包含部分」，但**部分可以独立于整体存在**，甚至可以同时属于多个整体。实线，空心菱形画在整体一端。

C++ 中，聚合通常是**从外面传进来的 `shared_ptr`**，或者整体只保存指针、但不负责释放：

<!-- run: off -->
```cpp
class Team {
public:
    void add(std::shared_ptr<Member> m) { members_.push_back(std::move(m)); }
private:
    std::vector<std::shared_ptr<Member>> members_;   // 团队解散了，成员还在；一个人也可以同时在两个团队里
};
```

```uml
label: Team 和 Project 都聚合 Member，空心菱形在整体一端；同一个 Member 可以同时属于两个整体
classes:
  - { id: Team, x: 20, y: 20, w: 120 }
  - { id: Project, x: 20, y: 110, w: 120 }
  - { id: Member, x: 380, y: 64, w: 120, attrs: ['- name_ : string'] }
rels:
  - { from: Team, to: Member, type: aggregate, tm: '*', tdx: -12, mid: 260 }
  - { from: Project, to: Member, type: aggregate, tm: '*', tdx: 12, mid: 260 }
```

聚合和组合的区别是本文的重点，第 3.7 节用一个程序实测。

### 3.5 关联

**关联**（association）表示一个类**长期知道**另一个类的存在：把对方的指针或引用存成成员，以后随时可以调用它；但两者是平等的，谁也不拥有谁。实线，箭头指向被引用的一方。

<!-- run: off -->
```cpp
class Order {
private:
    Customer* customer_;                         // 订单记着下单的客户，但不负责客户的生死
};
```

如果两个类互相保存对方的指针，就是**双向关联**，两端都不画箭头。

严格来说，组合和聚合都是「关联」的特殊情况：关联只说明两个类之间有长期的联系，聚合进一步说明这种联系是「整体和部分」，组合再进一步说明部分的生命周期由整体负责。

### 3.6 依赖

**依赖**（dependency）是最弱的关系：一个类在**某个函数里临时用到了**另一个类，用完就不再管它。虚线加箭头，指向被依赖的一方。

以下几种情况都是依赖：

<!-- run: off -->
```cpp
class Player {
public:
    void play(Logger& log);                      // ① 作为参数
    Frame current() const;                       // ② 作为返回值
    void reset() {
        Clock c;                                 // ③ 作为局部变量
        Config::instance().load();               // ④ 调用别的类的静态函数
    }
};
```

判断是依赖还是关联，看**有没有存成成员变量**：存了就是关联（或聚合、组合），只在函数里出现就是依赖。

### 3.7 实测：组合和聚合的区别

组合和聚合在类图上只差一个菱形是实心还是空心，到了代码里，区别是**部分对象的生命周期由谁负责**。下面的程序中，`Car` 组合了 `Engine`，聚合了一组 `Wheel`，关联了一个 `Driver`：

```uml
label: Car 组合 Engine，聚合多个 Wheel，关联 0 或 1 个 Driver
classes:
  - { id: Car, x: 20, y: 60, w: 290, attrs: ['- engine_ : Engine', '- wheels_ : vector<shared_ptr<Wheel>>', '- driver_ : Driver*'], ops: ['+ setDriver(d: Driver*)'] }
  - { id: Engine, x: 470, y: 10, w: 110 }
  - { id: Wheel, x: 470, y: 90, w: 110 }
  - { id: Driver, x: 470, y: 170, w: 110 }
rels:
  - { from: Car, to: Engine, type: compose, fdx: -40, tm: '1', mid: 400 }
  - { from: Car, to: Wheel, type: aggregate, fdx: -8, tm: '*', mid: 400 }
  - { from: Car, to: Driver, type: assoc, fdx: 30, tm: '0..1', mid: 400 }
```

每个类在构造和析构时打印一行，看看它们分别在什么时候析构：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Engine {                       // 组合的部分
    Engine()  { std::cout << "  Engine 构造\n"; }
    ~Engine() { std::cout << "  Engine 析构\n"; }
};

struct Wheel {                        // 聚合的部分
    explicit Wheel(int id) : id(id) { std::cout << "  Wheel" << id << " 构造\n"; }
    ~Wheel() { std::cout << "  Wheel" << id << " 析构\n"; }
    int id;
};

struct Driver {                       // 关联的对象
    std::string name;
};

class Car {
public:
    explicit Car(std::vector<std::shared_ptr<Wheel>> wheels) : wheels_(std::move(wheels)) {
        std::cout << "  Car 构造\n";
    }
    ~Car() { std::cout << "  Car 析构\n"; }
    void setDriver(Driver* d) { driver_ = d; }

private:
    Engine engine_;                                // 组合：值成员，和 Car 同生共死
    std::vector<std::shared_ptr<Wheel>> wheels_;   // 聚合：和外面共享，Car 没了轮子还在
    Driver* driver_ = nullptr;                     // 关联：只是认识，不管它的死活
};

int main() {
    auto w1 = std::make_shared<Wheel>(1);
    auto w2 = std::make_shared<Wheel>(2);
    Driver tom{"Tom"};

    std::cout << "进入作用域\n";
    {
        Car car({w1, w2});
        car.setDriver(&tom);
        std::cout << "  车里的 Wheel1 有 " << w1.use_count() << " 个主人\n";
    }
    std::cout << "离开作用域\n";
    std::cout << "  Wheel1 还剩 " << w1.use_count() << " 个主人\n";
    std::cout << "  司机 " << tom.name << " 还在\n";
    std::cout << "main 结束\n";
}
```

运行结果：

```text
  Wheel1 构造
  Wheel2 构造
进入作用域
  Engine 构造
  Car 构造
  车里的 Wheel1 有 2 个主人
  Car 析构
  Engine 析构
离开作用域
  Wheel1 还剩 1 个主人
  司机 Tom 还在
main 结束
  Wheel2 析构
  Wheel1 析构
```

把每个对象从构造到析构的时间画成一条线：

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="对象生命周期：Engine 在 Car 构造之前构造、在 Car 析构之后析构，完全落在作用域之内；Wheel1、Wheel2 和 Driver 从作用域之前一直活到 main 结束">
  <g style="font-family:var(--mono);font-size:12px">
    <rect x="186" y="14" width="310" height="176" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-dasharray:4 3"/>
    <text x="341" y="30" text-anchor="middle" style="fill:var(--muted);font-size:11px">{ Car car(...); }  的作用域</text>
    <text x="20" y="57" style="fill:var(--fg)">Car</text>
    <rect x="310" y="45" width="62" height="16" rx="3" style="fill:var(--fg-2)"/>
    <text x="20" y="87" style="fill:var(--fg)">Engine</text>
    <rect x="248" y="75" width="186" height="16" rx="3" style="fill:var(--accent)"/>
    <text x="446" y="87" style="fill:var(--accent);font-size:11px">组合</text>
    <text x="20" y="117" style="fill:var(--fg)">Wheel1</text>
    <rect x="124" y="105" width="502" height="16" rx="3" style="fill:var(--link)"/>
    <text x="20" y="147" style="fill:var(--fg)">Wheel2</text>
    <rect x="124" y="135" width="496" height="16" rx="3" style="fill:var(--link)"/>
    <text x="634" y="132" style="fill:var(--link);font-size:11px">聚合</text>
    <text x="20" y="177" style="fill:var(--fg)">Driver</text>
    <rect x="140" y="165" width="476" height="16" rx="3" style="fill:none;stroke:var(--muted);stroke-width:1.5"/>
    <text x="634" y="177" style="fill:var(--muted);font-size:11px">关联</text>
    <line x1="110" y1="200" x2="660" y2="200" style="stroke:var(--border-2);stroke-width:1.2"/>
    <g style="fill:var(--muted);font-size:11px" text-anchor="middle">
      <text x="124" y="218">Wheel 构造</text>
      <text x="186" y="236">进入</text>
      <text x="248" y="218">Engine 构造</text>
      <text x="310" y="236">Car 构造</text>
      <text x="372" y="218">Car 析构</text>
      <text x="434" y="236">Engine 析构</text>
      <text x="496" y="218">离开</text>
      <text x="560" y="236">main 结束</text>
      <text x="626" y="218">Wheel 析构</text>
    </g>
  </g>
</svg>
</figure>

从图上能看出三件事：

1. **组合的部分完全落在整体的生命周期里**。`Engine` 在 `Car` 的构造函数体执行**之前**构造，在 `Car` 的析构函数体执行**之后**析构：C++ 先构造成员、再执行构造函数体；析构时顺序相反。不管 `Car` 怎么被销毁（离开作用域、`delete`、所在的容器被清空），`Engine` 一定跟着销毁。
2. **聚合的部分比整体活得久**。`Car` 析构时，`wheels_` 里的 `shared_ptr` 只是把引用计数从 2 减到 1，轮子本身还在，直到 `main` 结束、最后一个主人 `w1` / `w2` 离开作用域，才真正析构。
3. **关联的对象和整体互不影响**。`Car` 只保存了 `tom` 的地址，从头到尾没有管过它的构造和析构。反过来，如果 `tom` 先于 `Car` 销毁，`driver_` 就成了悬空指针，这正是关联关系需要小心的地方。

## 4. 多重性

关系线的两端可以标上**多重性**（multiplicity），表示一个对象对应几个另一端的对象：

| 写法 | 含义 | 常见的 C++ 写法 |
| --- | --- | --- |
| `1` | 恰好一个 | 值成员 `T`、引用 `T&`、不为空的 `unique_ptr<T>` |
| `0..1` | 零个或一个 | 可以为空的指针 `T*`、`std::optional<T>` |
| `*` 或 `0..*` | 零个或多个 | `std::vector<T>`、`std::list<T>` 等容器 |
| `1..*` | 至少一个 | 容器，并且由构造函数保证非空 |
| `2..4` | 两到四个 | 很少用 |

```uml
label: 一个 Order 组合 1 到多个 OrderItem；一个 OrderItem 关联 1 个 Product；一个 Order 关联 0 或 1 个 Coupon
classes:
  - { id: Order, x: 20, y: 30, w: 220, attrs: ['- items_ : vector<OrderItem>', '- coupon_ : Coupon*'] }
  - { id: OrderItem, x: 400, y: 20, w: 150, attrs: ['- count_ : int'] }
  - { id: Product, x: 400, y: 140, w: 150 }
  - { id: Coupon, x: 60, y: 160, w: 140 }
rels:
  - { from: Order, to: OrderItem, type: compose, fm: '1', tm: '1..*', fdx: -8, tdx: -15 }
  - { from: OrderItem, to: Product, type: assoc, tm: '1' }
  - { from: Order, to: Coupon, type: assoc, tm: '0..1' }
```

读法是**从一端出发，看另一端的数字**：「一个 Order 有 1..* 个 OrderItem」「一个 OrderItem 对应 1 个 Product」。多重性只在它能说明问题时才标，比如区分「一个」和「一组」；每条线都标反而让图变乱。

## 5. 从代码画出类图

把前面的内容串起来。下面这段代码是一个极简的播放器（和音视频专栏的播放器结构相同，只保留了骨架）。按 ▶ 播放，看看每一行代码对应类图上的什么：

<div class="dsviz" data-viz="dp-uml"></div>

画类图的步骤可以总结成三步：

1. **先画类**。每个类一个框，只写出和要说明的问题相关的成员。
2. **再看成员变量**，判断「拥有」关系：值成员和自己创建的 `unique_ptr` 是组合，外面传进来的 `shared_ptr` 是聚合，不负责释放的指针或引用是关联。
3. **最后看函数**：继承和实现画三角；函数参数、返回值、局部变量里出现的类画依赖。依赖线很多时，只画重要的几条。

完整的程序（`projects/dp-lab/02-uml/player.cpp`）可以直接运行：

```cpp
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Frame {
    int64_t pts;
};

class Logger {
public:
    void info(const std::string& msg) { std::cout << "[info] " << msg << '\n'; }
};

class Renderer {                                   // 接口：只有纯虚函数
public:
    virtual ~Renderer() = default;
    virtual void draw(const Frame& f) = 0;
};

class SdlRenderer : public Renderer {              // 实现接口
public:
    void draw(const Frame& f) override { std::cout << "draw frame pts=" << f.pts << '\n'; }
};

class Decoder {
public:
    Frame decode(int i) { return Frame{i * 40}; }  // 每帧 40ms（25fps）
};

class Player {
public:
    explicit Player(std::shared_ptr<Renderer> r)
        : decoder_(std::make_unique<Decoder>()), renderer_(std::move(r)) {}

    void play(const std::string& url, Logger& log) {   // Logger 只在这个函数里用一下：依赖
        log.info("open " + url);
        for (int i = 0; i < 3; ++i) queue_.push_back(decoder_->decode(i));
        for (const Frame& f : queue_) renderer_->draw(f);
        log.info("done, " + std::to_string(queue_.size()) + " frames");
    }

private:
    std::unique_ptr<Decoder> decoder_;      // 组合：Player 自己创建、独占
    std::shared_ptr<Renderer> renderer_;    // 聚合：从外面传进来，可能和别人共享
    std::vector<Frame> queue_;              // 组合：0 到多个 Frame
};

int main() {
    auto screen = std::make_shared<SdlRenderer>();
    Logger log;
    Player player(screen);
    player.play("bbb-10s.mp4", log);
}
```

```text
[info] open bbb-10s.mp4
draw frame pts=0
draw frame pts=40
draw frame pts=80
[info] done, 3 frames
```

注意 `Player` 只认识 `Renderer` 接口，完全不知道 `SdlRenderer` 的存在：类图上 `Player` 和 `SdlRenderer` 之间没有任何连线。想换成别的渲染方式，只要再写一个实现 `Renderer` 的类，在 `main` 里传进去，`Player` 一行都不用改。这就是后面《SOLID 原则》要讲的**依赖倒置**，也是几乎所有设计模式的出发点。

## 6. 常见错误

1. **箭头方向画反**。继承的三角要指向**父类**，不是从父类「派生」出子类的方向。记住「箭头指向被依赖的一方」：子类离不开父类，父类不知道有哪些子类。

   ```uml
   label: 左边是错误的画法，三角指向了子类 Circle；右边是正确的画法，三角指向父类 Shape
   classes:
     - { id: S1, name: Shape, x: 40, y: 30, w: 110, hl: warn }
     - { id: C1, name: Circle, x: 40, y: 140, w: 110, hl: warn }
     - { id: S2, name: Shape, x: 330, y: 30, w: 110, hl: ok }
     - { id: C2, name: Circle, x: 330, y: 140, w: 110, hl: ok }
   rels:
     - { from: S1, to: C1, type: inherit, hl: warn }
     - { from: C2, to: S2, type: inherit, hl: ok }
   texts:
     - { x: 170, y: 115, text: '✗ 三角指向了子类' }
     - { x: 460, y: 115, text: '✓ 三角指向父类' }
   ```

2. **菱形画错了端**。菱形在**整体**一端，不在部分一端。
3. **所有「有一个」都画成组合**。成员是指针的时候，要看指针指向的对象由谁负责释放：自己 `new` 自己 `delete`（或者 `unique_ptr`）是组合，别人负责释放就是聚合或关联。
4. **把依赖画成关联**。只在函数参数里出现的类是依赖（虚线），只有存成成员变量才是关联（实线）。
5. **想把所有东西都画上**。一张图画二十个类、每个类十几个成员，等于没画。类图要回答一个具体的问题（比如「策略模式里谁持有谁」），和这个问题无关的类、成员、关系都省掉。

## 7. 用工具画类图

手画适合打草稿，正式文档推荐用**文本描述生成类图**的工具，类图和代码一起放进 Git，改起来也方便：

- **PlantUML**：最老牌，语法简单，大多数 IDE 和文档系统都有插件。
- **Mermaid**：GitHub、GitLab 的 Markdown 直接支持 `mermaid` 代码块，语法和 PlantUML 很像。
- **draw.io**（diagrams.net）：拖拽式，适合不想写代码的场合。

PlantUML 和 Mermaid 用一串字符表示关系，形状和类图上的符号很像，很好记。第 5 节的类图用 Mermaid 写是这样的：

<!-- run: off -->
```text
classDiagram
    class Renderer {
        <<interface>>
        +draw(f: Frame)*
    }
    SdlRenderer ..|> Renderer      %% 实现
    Player *-- Decoder             %% 组合
    Player *-- "*" Frame           %% 组合，多重性 *
    Player o-- Renderer            %% 聚合
    Player ..> Logger              %% 依赖
    Decoder ..> Frame              %% 依赖
```

| 关系 | 写法 |
| --- | --- |
| 泛化 | `<|--` 或 `--|>` |
| 实现 | `<|..` 或 `..|>` |
| 组合 | `*--` |
| 聚合 | `o--` |
| 关联 | `-->` |
| 依赖 | `..>` |

本专栏的类图也是用一段文本描述生成的（字段说明在博客仓库的 `themes/lucky-term/source/js/uml.js`）。

## 8. 小结

- 类画成三栏：类名、属性、操作。`+` `-` `#` 表示可见性，类型写在冒号后面，静态成员加下划线
- 抽象类和纯虚函数用**斜体**；只有纯虚函数的类是接口，标 `«interface»`
- 六种关系，从强到弱：**泛化、实现**（三角）> **组合**（实心菱形）> **聚合**（空心菱形）> **关联**（实线箭头）> **依赖**（虚线箭头）
- 箭头和三角指向**被依赖**的一方，菱形画在**整体**一端
- 对应 C++：public 继承是泛化，继承纯虚接口是实现，值成员和 `unique_ptr` 是组合，外部传入的 `shared_ptr` 是聚合，不负责释放的指针 / 引用成员是关联，只在函数里出现是依赖
- 组合和聚合的区别是**部分的生命周期由谁负责**：实测中 `Car` 析构时 `Engine` 跟着析构，`Wheel` 一直活到 `main` 结束
- 类图要回答具体的问题，只画和问题有关的部分

## 9. 练习

1. 下面每段代码中，`A` 和 `B` 是什么关系？

   ```cpp
   // ①
   class A { B b_; };
   // ②
   class A { public: void f(const B& b); };
   // ③
   class A : public B {};              // B 有数据成员和普通函数
   // ④
   class A { std::vector<std::shared_ptr<B>> bs_; public: void add(std::shared_ptr<B> b); };
   // ⑤
   class A { B* b_; public: explicit A(B* b) : b_(b) {} };   // A 不负责 delete b_
   // ⑥
   class A { std::unique_ptr<B> b_ = std::make_unique<B>(); };
   ```

2. 画出下面代码的类图（只画类名和关系，标出多重性）：

   <!-- run: off -->
   ```cpp
   class Shape { public: virtual ~Shape() = default; virtual void draw() const = 0; };
   class Circle : public Shape { public: void draw() const override; };
   class Group : public Shape {
   public:
       void add(std::unique_ptr<Shape> s);
       void draw() const override;
   private:
       std::vector<std::unique_ptr<Shape>> children_;
   };
   ```

3. 根据下面的类图写出 C++ 类的声明（不用写函数体）：

   ```uml
   label: 练习 3 的类图：Editor 组合一个 Document，聚合多个 Plugin；Plugin 是接口，SpellCheck 实现它；Plugin 的 run 依赖 Document
   classes:
     - { id: Editor, x: 20, y: 20, w: 230, attrs: ['- doc_ : Document', '- plugins_ : vector<shared_ptr<Plugin>>'], ops: ['+ install(p: shared_ptr<Plugin>)'] }
     - { id: Document, x: 430, y: 20, w: 170, attrs: ['- text_ : string'] }
     - { id: Plugin, stereo: interface, x: 55, y: 200, w: 230, ops: ['*+ run(doc: Document&) : void'] }
     - { id: SpellCheck, x: 85, y: 330, w: 170 }
   rels:
     - { from: Editor, to: Document, type: compose, tm: '1', fdx: -21 }
     - { from: Editor, to: Plugin, type: aggregate, tm: '*' }
     - { from: SpellCheck, to: Plugin, type: realize }
     - { from: Plugin, to: Document, type: depend, points: [[285, 234], [515, 234], [515, 75]] }
   ```

4. 把第 3.7 节程序中的 `wheels_` 改成 `std::vector<std::unique_ptr<Wheel>>`，轮子改为在 `Car` 的构造函数里自己创建（`main` 里不再创建轮子）。类图要怎么改？程序的输出会变成什么样？先预测，再改代码运行验证。

<details>
<summary>点击查看答案</summary>

1. ① 组合（值成员）；② 依赖（只出现在参数里）；③ 泛化（`B` 有实现，不是接口）；④ 聚合（外面传进来的 `shared_ptr`，可以和别人共享）；⑤ 关联（保存了指针，但不负责它的生死）；⑥ 组合（自己创建、独占）。

2. `Group` 既**是一种** `Shape`（泛化），又**拥有**一组 `Shape`（组合，`unique_ptr` 表示独占）。这就是后面要讲的**组合模式**，用它可以表示「图形里套图形」的树形结构：

   ```uml
   label: 练习 2 答案：Circle 和 Group 都继承 Shape；Group 组合了多个 Shape
   classes:
     - { id: Shape, stereo: abstract, x: 150, y: 20, w: 150, ops: ['*+ draw() : void'] }
     - { id: Circle, x: 30, y: 170, w: 130 }
     - { id: Group, x: 290, y: 170, w: 130 }
   rels:
     - { from: Circle, to: Shape, type: inherit, mid: 135, tdx: -20 }
     - { from: Group, to: Shape, type: inherit, mid: 135, tdx: -20 }
     - { from: Group, to: Shape, key: children, type: compose, tm: '*', points: [[395, 170], [395, 47], [300, 47]] }
   ```

   注意 `Group` 和 `Shape` 之间有**两条**线：一条泛化，一条组合。画两条线不是重复，它们表达的是两件不同的事。

3. 参考代码：

   <!-- run: off -->
   ```cpp
   class Document {
   private:
       std::string text_;
   };

   class Plugin {
   public:
       virtual ~Plugin() = default;
       virtual void run(Document& doc) = 0;     // 依赖：Document 只出现在参数里
   };

   class SpellCheck : public Plugin {           // 实现
   public:
       void run(Document& doc) override;
   };

   class Editor {
   public:
       void install(std::shared_ptr<Plugin> p);
   private:
       Document doc_;                                    // 组合
       std::vector<std::shared_ptr<Plugin>> plugins_;    // 聚合
   };
   ```

4. `Car` 和 `Wheel` 之间的空心菱形改成实心菱形（组合）。轮子变成和发动机一样，跟着 `Car` 一起析构，而且都在「离开作用域」之前。成员按声明顺序构造、按相反顺序析构，`engine_` 声明在 `wheels_` 前面，所以轮子先析构、发动机最后析构。完整代码在 `projects/dp-lab/02-uml/lifetime_owned.cpp`，用 Clang（macOS 默认的 libc++）运行的输出：

   ```text
   进入作用域
     Engine 构造
     Wheel1 构造
     Wheel2 构造
     Car 构造
     Car 析构
     Wheel2 析构
     Wheel1 析构
     Engine 析构
   离开作用域
   main 结束
   ```

   有一个意外：同样的代码在 Ubuntu 24.04 上用 GCC 13（libstdc++）编译，输出的是 `Wheel1 析构` 在前、`Wheel2 析构` 在后。C++ 标准规定了**成员**的析构顺序（和声明顺序相反），但没有规定 `vector` 里**元素**的析构顺序：libc++ 从后往前销毁，libstdc++ 从前往后销毁。所以，不要写依赖容器元素析构顺序的代码。

</details>
