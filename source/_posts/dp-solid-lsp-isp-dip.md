---
title: SOLID 原则（下）：里氏替换、接口隔离与依赖倒置
date: 2026-11-08 12:00:00
updated: 2026-11-08 12:00:00
categories:
  - 设计模式
  - 基础与设计原则
tags:
  - 设计模式
  - SOLID
  - 里氏替换
  - 接口隔离
  - 依赖倒置
order: 4
description: 正方形「是一种」长方形，可把它交给调整横幅尺寸的函数，728 × 90 的横幅变成了 90 × 90：用契约测试找出违反里氏替换的子类。一个大而全的设备接口，给 fax() 加一个参数，9 个源文件全部重新编译；拆成三个小接口后只重编 3 个。业务逻辑直接依赖写文件的存储类，1000 个测试要几十到上百毫秒；依赖倒过来、注入内存实现后只要 0.025 毫秒。最后用一张表把 SOLID 五条原则和后面的模式对应起来。
top_group_list: true
disableNunjucks: true
viz: [dp-solid2]
---

> **本文目标**：理解里氏替换原则，知道「is-a」为什么不足以决定继承，会用契约测试检查子类；理解接口隔离原则，知道胖接口在 C++ 里带来的运行时和编译期代价；理解依赖倒置原则，分清依赖倒置、依赖注入和控制反转，会用接口或模板参数把高层逻辑和底层细节分开。
>
> **前置知识**：上一篇《SOLID 原则（上）：单一职责与开闭》；C/C++ 专栏的《多态与虚函数》《模板入门》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/04-solid2/` 目录。

## 1. 接着上一篇

上一篇讲了 SOLID 的前两条：单一职责回答「代码怎么切分」，开闭回答「新需求落在哪里」。开闭原则的实现手段几乎总是**多态**：定义一个接口，新需求就是一个新的实现。剩下三条原则，恰好都在回答「多态该怎么用才不出问题」：

<figure class="diagram">
<svg viewBox="0 0 720 170" role="img" aria-label="SOLID 五条原则：本文讲后三条，里氏替换、接口隔离、依赖倒置"><g style="font-family:var(--mono)"><rect x="10" y="10" width="132" height="130" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.2"/><text x="76.0" y="52.0" text-anchor="middle" style="fill:var(--muted);font-size:34px;font-weight:700">S</text><text x="76.0" y="78.0" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">单一职责</text><text x="76.0" y="96.0" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Single Responsibility</text><text x="76.0" y="124.0" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">一个模块只对一类角色负责</text><rect x="152" y="10" width="132" height="130" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.2"/><text x="218.0" y="52.0" text-anchor="middle" style="fill:var(--muted);font-size:34px;font-weight:700">O</text><text x="218.0" y="78.0" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">开闭</text><text x="218.0" y="96.0" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Open / Closed</text><text x="218.0" y="124.0" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">对扩展开放，对修改关闭</text><rect x="294" y="10" width="132" height="130" rx="8" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:2"/><text x="360.0" y="52.0" text-anchor="middle" style="fill:var(--accent);font-size:34px;font-weight:700">L</text><text x="360.0" y="78.0" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">里氏替换</text><text x="360.0" y="96.0" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Liskov Substitution</text><text x="360.0" y="124.0" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">子类能替换父类而不出错</text><rect x="436" y="10" width="132" height="130" rx="8" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:2"/><text x="502.0" y="52.0" text-anchor="middle" style="fill:var(--accent);font-size:34px;font-weight:700">I</text><text x="502.0" y="78.0" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">接口隔离</text><text x="502.0" y="96.0" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Interface Segregation</text><text x="502.0" y="124.0" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">不强迫依赖用不到的接口</text><rect x="578" y="10" width="132" height="130" rx="8" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:2"/><text x="644.0" y="52.0" text-anchor="middle" style="fill:var(--accent);font-size:34px;font-weight:700">D</text><text x="644.0" y="78.0" text-anchor="middle" style="fill:var(--fg);font-size:14px;font-weight:700">依赖倒置</text><text x="644.0" y="96.0" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">Dependency Inversion</text><text x="644.0" y="124.0" text-anchor="middle" style="fill:var(--fg-2);font-size:9.8px">高层和低层都依赖抽象</text><text x="147.0" y="160.0" text-anchor="middle" style="fill:var(--muted);font-size:12px">上一篇</text><text x="502.0" y="160.0" text-anchor="middle" style="fill:var(--accent);font-size:12px;font-weight:700">本文（下）</text></g></svg>
</figure>

- **里氏替换**：子类能不能放心地替换父类？
- **接口隔离**：接口该定义得多大？
- **依赖倒置**：接口该放在哪一边？

## 2. 里氏替换原则（LSP）

### 2.1 子类必须能替换父类

里氏替换原则（Liskov Substitution Principle）来自 Barbara Liskov 1987 年的一次主题演讲，后来她和 Jeannette Wing 把它写成了严格的定义。用大白话说：**凡是使用父类的地方，换成任何一个子类，程序都应该照样正确**。

这听起来是继承的天经地义，C++ 也允许把子类对象传给接收父类引用的函数。但编译器只检查**语法**上能不能替换（函数签名对得上），不检查**行为**上能不能替换。下面就是一个语法完全正确、行为却出了问题的例子。

### 2.2 正方形是不是长方形

数学上，正方形是一种长方形，于是很自然地写出 `Square : public Rectangle`。正方形的宽和高必须相等，所以 `Square` 重写了两个设置函数，设置宽时把高一起改掉，反过来也一样：

```cpp
#include <iostream>

class Rectangle {
public:
    virtual ~Rectangle() = default;
    virtual void setWidth(double w) { w_ = w; }
    virtual void setHeight(double h) { h_ = h; }
    double width() const { return w_; }
    double height() const { return h_; }
    double area() const { return w_ * h_; }

protected:
    double w_ = 0, h_ = 0;
};

class Square : public Rectangle {          // 正方形：宽和高必须相等
public:
    void setWidth(double w) override { w_ = h_ = w; }
    void setHeight(double h) override { w_ = h_ = h; }
};

// 使用长方形的代码：把一块区域调整成横幅广告的尺寸 728 × 90
void fitToBanner(Rectangle& r) {
    r.setWidth(728);
    r.setHeight(90);
}

// 契约测试：对每一个「长方形」都应该成立的约定
bool checkRectangleContract(Rectangle& r, const char* name) {
    r.setWidth(5);
    r.setHeight(4);
    bool ok = r.width() == 5 && r.height() == 4 && r.area() == 20;
    std::cout << name << "：设置宽 5、高 4 后，宽 " << r.width() << "，高 " << r.height()
              << "，面积 " << r.area() << (ok ? "  ✓" : "  ✗ 违反了长方形的约定") << '\n';
    return ok;
}

int main() {
    Rectangle rect;
    Square square;
    checkRectangleContract(rect, "Rectangle");
    checkRectangleContract(square, "Square");

    fitToBanner(rect);
    fitToBanner(square);
    std::cout << "横幅：Rectangle " << rect.width() << " × " << rect.height()
              << "，Square " << square.width() << " × " << square.height() << '\n';
}
```

```text
Rectangle：设置宽 5、高 4 后，宽 5，高 4，面积 20  ✓
Square：设置宽 5、高 4 后，宽 4，高 4，面积 16  ✗ 违反了长方形的约定
横幅：Rectangle 728 × 90，Square 90 × 90
```

`fitToBanner` 的作者完全有理由相信：对一个长方形先设宽 728、再设高 90，得到的就是 728 × 90。这是长方形这个类型对使用者做出的承诺。`Square` 在语法上是 `Rectangle`，却悄悄违背了这个承诺，于是 `fitToBanner` 得到了一个 90 × 90 的「横幅」。`fitToBanner` 本身没有任何错误，错在 `Square` 不能替换 `Rectangle`。

```uml
label: 违反里氏替换的继承：Square 继承 Rectangle，并重写了 setWidth 和 setHeight，破坏了「设置高不改变宽」的约定
classes:
  - { id: Rect, name: Rectangle, x: 220, y: 16, w: 220, attrs: ['# w_ : double', '# h_ : double'], ops: ['+ setWidth(w)', '+ setHeight(h)', '+ area() : double'] }
  - { id: Sq, name: Square, x: 220, y: 230, w: 220, ops: ['+ setWidth(w)  // 宽高一起改', '+ setHeight(h)  // 宽高一起改'], hl: warn }
  - { id: Banner, name: 'fitToBanner(Rectangle&)', x: 520, y: 70, w: 190 }
rels:
  - { from: Sq, to: Rect, type: inherit, hl: warn }
  - { from: Banner, to: Rect, type: depend, fdx: -2 }
notes:
  - { x: 10, y: 120, text: ['约定：setHeight', '不改变宽度'], to: Rect }
```

### 2.3 继承的契约

「is-a」只能说明两个概念在分类上的关系，不足以决定继承。决定能不能继承的，是**行为上的契约**。一个子类要能替换父类，必须遵守三条规则：

| 规则 | 含义 | 违反的例子 |
| --- | --- | --- |
| 前置条件不能加强 | 父类接受的输入，子类也必须接受 | 父类的 `withdraw(amount)` 接受任何正数，子类却要求 `amount ≥ 100`，否则返回错误 |
| 后置条件不能削弱 | 父类保证的结果，子类也必须保证 | 父类的 `find()` 找不到时返回空的 `optional`，子类却偶尔返回一个默认值 |
| 不变式必须保持 | 父类始终成立的性质，子类也必须成立 | `Rectangle` 的「宽和高可以独立设置」，`Square` 做不到 |

`Square` 违反的就是第三条（也可以说削弱了 `setHeight` 的后置条件「宽度不变」）。

一个常见的危险信号是：**子类重写一个函数，让它什么也不做，或者直接报错**。比如 `ReadOnlyFile` 继承 `File`，把 `write()` 重写成空函数。所有调用 `write()` 的代码都以为写成功了。这样的继承几乎一定违反里氏替换。

### 2.4 契约测试

怎么发现违反里氏替换的子类？上面的 `checkRectangleContract` 就是一种做法，叫**契约测试**：针对**父类的接口**写一组测试，描述所有子类都必须遵守的约定，然后把**每一个子类**都拿来跑一遍。新加一个子类时，只要它通过这组测试，就可以放心地替换父类。

C++ 里可以把契约测试写成模板，用同一组测试检查不同的类型：

<!-- run: off -->
```cpp
template <typename Rect>
void rectangleContract() {
    Rect r;
    r.setWidth(5);
    r.setHeight(4);
    assert(r.width() == 5 && r.area() == 20);   // 设置高不应该改变宽
}

rectangleContract<Rectangle>();
rectangleContract<Square>();                     // 失败：这个子类不能替换 Rectangle
```

### 2.5 修正：不要用继承表达不了的关系

既然 `Square` 做不到 `Rectangle` 承诺的事情，就不要让它继承 `Rectangle`。让两者都实现一个只包含**真正共同行为**的接口 `Shape`（求面积）；需要改尺寸时，返回一个新对象，而不是修改原来的对象：

```cpp
#include <iostream>
#include <memory>
#include <vector>

class Shape {                              // 所有形状都能做的事：求面积
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
};

class Rectangle : public Shape {
public:
    Rectangle(double w, double h) : w_(w), h_(h) {}
    double area() const override { return w_ * h_; }
    Rectangle withSize(double w, double h) const { return {w, h}; }   // 改尺寸得到一个新对象
private:
    double w_, h_;
};

class Square : public Shape {
public:
    explicit Square(double side) : side_(side) {}
    double area() const override { return side_ * side_; }
    Square withSide(double s) const { return Square(s); }
private:
    double side_;
};

int main() {
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Rectangle>(5, 4));
    shapes.push_back(std::make_unique<Square>(4));
    double total = 0;
    for (const auto& s : shapes) total += s->area();      // 任何 Shape 都可以放心地替换进来
    std::cout << "总面积 " << total << '\n';
    // fitToBanner 只接受 Rectangle：编译器不会让你把 Square 传进去
    Rectangle banner = Rectangle(1, 1).withSize(728, 90);
    std::cout << "横幅面积 " << banner.area() << '\n';
}
```

```text
总面积 36
横幅面积 65520
```

```uml
label: 修正后：Rectangle 和 Square 都只实现 Shape 接口的 area；两者之间没有继承关系
classes:
  - { id: Shape, stereo: interface, x: 230, y: 16, w: 170, ops: ['*+ area() : double'] }
  - { id: Rect, name: Rectangle, x: 90, y: 170, w: 210, ops: ['+ area() : double', '+ withSize(w, h) : Rectangle'] }
  - { id: Sq, name: Square, x: 340, y: 170, w: 200, ops: ['+ area() : double', '+ withSide(s) : Square'] }
rels:
  - { from: Rect, to: Shape, type: realize, mid: 125 }
  - { from: Sq, to: Shape, type: realize, mid: 125 }
```

现在任何接收 `Shape` 的代码，都可以放心地使用任何形状；`fitToBanner` 如果需要「宽高独立」的能力，就只接收 `Rectangle`，编译器不会让你把 `Square` 传进去。

## 3. 接口隔离原则（ISP）

### 3.1 不要强迫使用者依赖它用不到的东西

接口隔离原则（Interface Segregation Principle）是 Robert C. Martin 为施乐（Xerox）的打印机软件做咨询时总结的：**不应该强迫客户端依赖它不使用的方法**。接口应该按**使用者的需要**划分，而不是按**实现者能做什么**划分。

### 3.2 一个大而全的设备接口

办公室里有两种设备：多功能一体机（能打印、扫描、传真）和普通打印机（只能打印）。如果只定义一个 `Device` 接口，普通打印机就被迫实现它根本做不到的事：

<!-- run: off -->
```cpp
class Device {
public:
    virtual ~Device() = default;
    virtual void print(const Doc& d) = 0;
    virtual Doc scan() = 0;
    virtual void fax(const Doc& d, const std::string& number) = 0;
};

class SimplePrinter : public Device {          // 只会打印，另外两个只能写成「不支持」
public:
    void print(const Doc& d) override;
    Doc scan() override { std::cout << "不支持扫描\n"; return {}; }
    void fax(const Doc&, const std::string&) override { std::cout << "不支持传真\n"; }
};

void fax_order(Device& d) { d.fax({"order"}, "010-12345678"); }

fax_order(printer);                            // 编译通过，运行时才输出「不支持传真」
```

这里有两个问题：

- **运行时才暴露错误**。`fax_order(printer)` 编译得过，运行时才发现打印机不能传真。这些空实现也违反了上一节的里氏替换：`SimplePrinter` 不能替换 `Device`。
- **不相关的代码被绑在一起**。只用到打印的代码，也依赖着 `scan` 和 `fax` 的声明。下面会实测这个依赖的代价。

### 3.3 拆成小接口

按**使用者需要什么**拆成三个小接口。一体机实现全部三个，打印机只实现 `Printer`；每个使用者只依赖它用得到的那一个：

```uml
label: 拆分后：Printer、Scanner、Fax 三个小接口；OfficeMachine 实现全部三个，SimplePrinter 只实现 Printer
classes:
  - { id: P, name: Printer, stereo: interface, x: 20, y: 16, w: 170, ops: ['*+ print(d)'] }
  - { id: S, name: Scanner, stereo: interface, x: 250, y: 16, w: 170, ops: ['*+ scan() : Doc'] }
  - { id: F, name: Fax, stereo: interface, x: 480, y: 16, w: 200, ops: ['*+ fax(d, number)'] }
  - { id: OM, name: OfficeMachine, x: 250, y: 200, w: 170 }
  - { id: SP, name: SimplePrinter, x: 20, y: 200, w: 170 }
rels:
  - { from: OM, to: P, type: realize, fdx: -50, mid: 150 }
  - { from: OM, to: S, type: realize }
  - { from: OM, to: F, type: realize, fdx: 50, mid: 150 }
  - { from: SP, to: P, type: realize }
```

<!-- run: off -->
```cpp
class Printer { public: virtual ~Printer() = default; virtual void print(const Doc& d) = 0; };
class Scanner { public: virtual ~Scanner() = default; virtual Doc scan() = 0; };
class Fax     { public: virtual ~Fax() = default; virtual void fax(const Doc& d, const std::string& number) = 0; };

class OfficeMachine : public Printer, public Scanner, public Fax { /* ... */ };
class SimplePrinter : public Printer { /* 只实现 print */ };

void fax_order(Fax& f) { f.fax({"order"}, "010-12345678"); }

fax_order(printer);                            // 编译错误：SimplePrinter 不是 Fax
```

「不支持传真」从运行时的一行输出，变成了编译错误。C++ 的多重继承在这里用得恰到好处：一个类实现多个**只有纯虚函数的接口**，不会带来菱形继承那些麻烦。

### 3.4 实测：改一个函数，要重新编译几个文件

我把两种写法各做成一个 CMake 小项目（`isp/fat` 和 `isp/split`），功能完全相同：9 个源文件，其中 4 个只用到打印、1 个只用到扫描、1 个只用到传真，加上两个设备的实现和 `main`。然后模拟一个需求变化：**传真要支持重试次数**，`fax()` 多一个参数 `int retries`。`isp/measure.sh` 先把两个项目各完整编译一次，再按这个需求修改代码，让 `make` 增量编译，统计重新编译了哪些源文件：

<div class="dsviz" data-viz="dp-isp"></div>

| | 胖接口 `Device` | 小接口 |
| --- | --- | --- |
| 需要修改的文件 | 6 个：`device.h`、两个设备的 `.h` 和 `.cpp`、`fax_order.cpp` | 4 个：`fax.h`、`office_machine.h/.cpp`、`fax_order.cpp` |
| 重新编译的源文件 | **9 / 9** | **3 / 9**：`main`、`office_machine`、`fax_order` |

胖接口的项目里，一个和打印毫无关系的改动，让 4 个打印客户端、扫描客户端、甚至根本不支持传真的 `SimplePrinter` 都重新编译了一遍，`SimplePrinter` 还得跟着修改那个「不支持传真」的空函数。拆分以后，改传真只影响用到传真的代码。项目越大，这个差别越明显。

### 3.5 拆到什么程度

和单一职责一样，接口隔离也不是越细越好。每个函数一个接口，会让实现类继承一长串接口，使用者也不知道该依赖哪个。按**角色**拆：一组总是被同一类使用者一起调用的函数，放在一个接口里。上面的「打印」「扫描」「传真」就是三个角色。

## 4. 依赖倒置原则（DIP）

### 4.1 两句话

依赖倒置原则（Dependency Inversion Principle）也是 Martin 提出的，有两句：

1. **高层模块不应该依赖低层模块，两者都应该依赖抽象**。
2. **抽象不应该依赖细节，细节应该依赖抽象**。

「高层」是业务规则（下单要满足什么条件），「低层」是实现细节（订单存在文件里还是数据库里）。业务规则是系统里最有价值、最稳定的部分，不应该因为换了一个数据库就要修改。

### 4.2 改造前：业务规则被存储细节绑住

`OrderService` 负责下单的业务规则，订单直接用 `FileOrderStore` 存进文件（完整代码 `dip_before.cpp`）：

<!-- run: off -->
```cpp
class FileOrderStore {                         // 低层：每个订单追加一行，并确保写到磁盘上
public:
    void save(const Order& o) {
        FILE* f = std::fopen(path_.c_str(), "a");
        std::fprintf(f, "%s,%d,%.2f\n", o.customer.c_str(), o.qty, o.price);
        std::fflush(f);
        fsync(fileno(f));                       // 数据库也会这样做：断电也不能丢订单
        std::fclose(f);
    }
    // ...
};

class OrderService {                           // 高层：下单的业务规则
public:
    bool place(const Order& o) {
        if (o.qty <= 0 || o.qty > 100 || o.price <= 0) return false;
        store_.save(o);
        return true;
    }
private:
    FileOrderStore store_{"orders.csv"};       // 直接创建具体的存储类：换不掉
};
```

这样写的后果：

- **测试业务规则必须真的写文件**。每个测试都会往 `orders.csv` 里追加内容，测试之间互相影响，测试也慢。
- **换存储方式必须改业务代码**。要改成存数据库，就得改 `OrderService`，连带着重新测试下单规则。
- **编译依赖**：`OrderService` 的头文件要包含 `FileOrderStore` 的头文件，存储层的任何改动都会让业务层重新编译。

### 4.3 改造：接口由高层定义

在**业务这一侧**定义一个接口 `OrderStore`，写明业务需要存储层提供什么；`OrderService` 只依赖这个接口，具体用哪个实现由外面传进来；`FileOrderStore` 去实现这个接口。下面的演示展示了改造前后依赖的方向，注意看数据包（运行时的调用）和箭头（源码依赖）的方向：

<div class="dsviz" data-viz="dp-dip"></div>

改造后，测试只需要一个把订单存在内存里的实现：

```cpp
#include <iostream>
#include <string>
#include <vector>

struct Order {
    std::string customer;
    int qty;
    double price;
};

// 接口由高层（业务）定义：业务需要什么，就规定什么
class OrderStore {
public:
    virtual ~OrderStore() = default;
    virtual void save(const Order& o) = 0;
};

class OrderService {
public:
    explicit OrderService(OrderStore& store) : store_(store) {}   // 依赖从外面注入
    bool place(const Order& o) {
        if (o.qty <= 0 || o.qty > 100 || o.price <= 0) return false;
        store_.save(o);
        return true;
    }
private:
    OrderStore& store_;
};

// 测试用的实现：订单存在内存里，测试结束就没了，测试之间互不影响
class InMemoryOrderStore : public OrderStore {
public:
    void save(const Order& o) override { orders.push_back(o); }
    std::vector<Order> orders;
};

int main() {
    InMemoryOrderStore store;
    OrderService svc(store);
    int failed = 0;
    auto expect = [&](bool cond, const char* what) { if (!cond) { ++failed; std::cout << "✗ " << what << '\n'; } };

    expect(svc.place({"alice", 2, 99.5}), "正常订单应该成功");
    expect(!svc.place({"bob", 0, 10}), "数量为 0 应该失败");
    expect(!svc.place({"carol", 101, 10}), "数量超过 100 应该失败");
    expect(!svc.place({"dave", 1, -5}), "价格为负应该失败");
    expect(store.orders.size() == 1, "只有一个订单被保存");
    std::cout << (failed ? "有测试失败" : "5 个测试全部通过") << "，保存了 " << store.orders.size() << " 个订单\n";
}
```

```text
5 个测试全部通过，保存了 1 个订单
```

### 4.4 实测：同一组测试，两种存储

`dip_measure.cpp` 用同一组 1000 个下单测试（其中约 840 个是合法订单，会被保存），分别注入 `FileOrderStore` 和 `InMemoryOrderStore`，测量总耗时（`-O2`，各运行 3 次 / 2 次）：

| 存储实现 | macOS（Apple M6，APFS） | Linux（Ubuntu 24.04 容器） |
| --- | --- | --- |
| `FileOrderStore`（每次 `fsync`） | 27–56 ms | 186–189 ms |
| `InMemoryOrderStore` | 0.024–0.053 ms | 0.025 ms |

<figure class="diagram">
<svg viewBox="0 0 720 200" role="img" aria-label="1000 个下单测试的耗时（对数刻度）：内存实现约 0.025 毫秒；写文件并 fsync 在 macOS 上 27 到 56 毫秒，在 Linux 容器里约 186 毫秒"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--muted);font-size:11px">1000 个下单测试的总耗时（对数刻度，每格 10 倍）</text><line x1="230.0" y1="36" x2="230.0" y2="160" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="230.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0.01 ms</text><line x1="322.0" y1="36" x2="322.0" y2="160" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="322.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0.1 ms</text><line x1="414.0" y1="36" x2="414.0" y2="160" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="414.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 ms</text><line x1="506.0" y1="36" x2="506.0" y2="160" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="506.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 ms</text><line x1="598.0" y1="36" x2="598.0" y2="160" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="598.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">100 ms</text><line x1="690.0" y1="36" x2="690.0" y2="160" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="690.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1000 ms</text><text x="220.0" y="63.0" text-anchor="end" style="fill:var(--fg);font-size:12px">内存实现（macOS / Linux）</text><rect x="230" y="48" width="36.6" height="22" rx="3" style="fill:var(--accent)"/><text x="274.6" y="63.0" text-anchor="start" style="fill:var(--accent);font-size:12px;font-weight:700">0.025 ms</text><text x="220.0" y="101.0" text-anchor="end" style="fill:var(--fg);font-size:12px">写文件 + fsync（macOS）</text><rect x="230" y="86" width="315.7" height="22" rx="3" style="fill:var(--warn)"/><rect x="545.7" y="86" width="29.1" height="22" rx="3" style="fill:var(--warn);opacity:.45"/><text x="582.8" y="101.0" text-anchor="start" style="fill:var(--warn);font-size:12px;font-weight:700">27–56 ms</text><text x="220.0" y="139.0" text-anchor="end" style="fill:var(--fg);font-size:12px">写文件 + fsync（Linux 容器）</text><rect x="230" y="124" width="392.8" height="22" rx="3" style="fill:var(--danger)"/><rect x="622.8" y="124" width="0.6" height="22" rx="3" style="fill:var(--danger);opacity:.45"/><text x="631.4" y="139.0" text-anchor="start" style="fill:var(--danger);font-size:12px;font-weight:700">186–189 ms</text></g></svg>
</figure>

相差三到四个数量级。macOS 的 `fsync` 只把数据交给磁盘控制器，不保证真正写到存储介质上（要用 `fcntl(fd, F_FULLFSYNC)` 才行），所以 macOS 上的数字偏小；换成真正的数据库、加上网络往返，差距只会更大。更重要的是，内存实现让测试**彼此独立、可以重复运行**：不会因为上一次测试留下的文件而失败，也可以并行跑。

### 4.5 「倒置」倒的是什么

改造前后，**运行时的调用方向没有变**，始终是 `OrderService` 调用存储层保存订单。变的是**源码依赖的方向**：改造前业务层 `#include` 存储层；改造后存储层 `#include` 业务层定义的接口。依赖的箭头从「高层 → 低层」变成了「低层 → 高层的接口」，这就是「倒置」。

关键在于**接口归谁所有**。接口要按高层的需要来设计、和高层放在一起（同一个目录、同一个库）。如果反过来，让存储层自己定义一个 `IFileStore` 接口、业务层去依赖它，形式上也有接口，但依赖方向没有倒过来：存储层的接口一变，业务层还是得跟着改。

几个容易混淆的概念：

| 概念 | 是什么 | 本文的例子 |
| --- | --- | --- |
| 依赖倒置（DIP） | 设计原则：高层和低层都依赖抽象，抽象归高层所有 | `OrderStore` 接口由业务层定义 |
| 依赖注入（DI） | 实现手段：依赖从外面传进来，而不是自己创建 | `OrderService(OrderStore& store)` |
| 控制反转（IoC） | 更宽泛的思想：流程由框架调用你的代码，而不是你调用框架 | 插件系统、回调、事件循环 |

依赖注入是实现依赖倒置最常用的方式。具体对象在哪里创建、组装起来？通常集中在 `main`（或者测试代码）里，这个地方叫**组合根**（composition root）：只有它知道所有具体类，其余代码都只认识接口。

### 4.6 C++ 的另一种写法：模板参数

虚函数接口的依赖倒置在运行时选择实现，有一次间接调用的开销，对下单这种操作完全可以忽略。如果实现在编译期就能确定，或者调用非常频繁，C++ 还可以用模板参数把依赖倒过来：

<!-- run: off -->
```cpp
template <typename Store>                      // Store 只要有 save(const Order&) 就行
class OrderService {
public:
    explicit OrderService(Store& store) : store_(store) {}
    bool place(const Order& o) {
        if (o.qty <= 0 || o.qty > 100 || o.price <= 0) return false;
        store_.save(o);                        // 编译期确定调用哪个函数，可以内联
        return true;
    }
private:
    Store& store_;
};

OrderService<FileOrderStore> real(file);
OrderService<InMemoryOrderStore> test(mem);
```

这里没有基类，`OrderService` 依赖的「抽象」是对 `Store` 的隐式要求（有一个 `save` 函数），C++20 可以用 concept 把这个要求写明。代价是 `OrderService` 的实现要放在头文件里，每换一种 `Store` 就生成一份代码，而且不能在运行时切换。两种写法的详细对比放在本章最后一篇《组合优于继承》里。

### 4.7 不是每个依赖都要倒置

依赖倒置针对的是**会变化的、或者妨碍测试的**依赖：数据库、文件、网络、时钟、随机数、第三方服务。对 `std::string`、`std::vector` 这些稳定的标准库类型，或者项目里稳定的值类型，直接依赖就好，给它们套上接口只会增加复杂度。

## 5. SOLID 一览

| 原则 | 一句话 | 违反时的症状 | 后面会用到它的模式 |
| --- | --- | --- | --- |
| 单一职责 | 一个模块只对一类角色负责 | 改一个部门的需求，另一个部门的功能坏了 | 外观、命令 |
| 开闭 | 新需求通过新增代码实现 | 每加一种类型，都要改一串 `if-else` | 策略、装饰器、观察者、工厂方法 |
| 里氏替换 | 子类能替换父类而不出错 | 子类里出现空实现、`dynamic_cast` 判断具体类型 | 所有用到继承的模式 |
| 接口隔离 | 不强迫依赖用不到的接口 | 实现类里一堆「不支持」，改一处牵连一大片编译 | 适配器、组合 |
| 依赖倒置 | 高层和低层都依赖高层定义的抽象 | 业务逻辑没法脱离数据库测试 | 工厂、桥接、策略、模板方法 |

这五条原则都不是必须严格遵守的规则，而是判断「代码哪里会疼」的工具。下一篇《组合优于继承》会把它们放到一起，对比 C++ 里实现多态的四种方式。

## 6. 小结

- **里氏替换**：凡是使用父类的地方，换成任何子类都应该照样正确。「is-a」不足以决定继承，决定继承的是行为契约：前置条件不能加强，后置条件不能削弱，不变式必须保持
- 实测：`Square` 继承 `Rectangle`，`fitToBanner` 想要 728 × 90 的横幅，得到的却是 90 × 90；用契约测试可以在每个子类上检查父类的约定
- **接口隔离**：按使用者的需要划分接口。胖接口让实现类写出「不支持」的空函数，错误要到运行时才暴露；拆分后变成编译错误
- 实测：给 `fax()` 加一个参数，胖接口的项目 9 个源文件全部重新编译，拆分后只重编 3 个
- **依赖倒置**：高层定义接口，低层实现它，依赖从外面注入。运行时的调用方向不变，源码依赖的方向倒了过来
- 实测：1000 个下单测试，写文件（`fsync`）要 27–189 ms，注入内存实现后只要约 0.025 ms
- 依赖倒置、依赖注入、控制反转是三个不同的概念；C++ 里除了虚函数接口，还可以用模板参数实现依赖倒置

## 7. 练习

1. 下面的函数模板对 `std::vector<int>` 没有问题。如果传入 `std::vector<bool>`，会怎样？这和里氏替换原则有什么关系？

   <!-- run: off -->
   ```cpp
   template <typename T>
   void resetFirst(std::vector<T>& v) {
       T& first = v[0];
       first = T();
   }
   ```

2. 下面三个子类，哪些违反了里氏替换原则？
   ① `ReadOnlyFile : public File`，把 `write()` 重写成什么也不做；
   ② `CachedRepository : public Repository`，查询结果和父类完全相同，只是会把结果缓存起来，第二次查询更快；
   ③ `SortedVector : public Vector`，把 `push_back(x)` 重写成「插入到合适的位置，保持有序」。

3. 下面的接口有三类使用者：备份工具只用 `read` 和 `listFiles`；清理工具只用 `listFiles` 和 `remove`；管理后台只用 `getQuota` 和 `setQuota`。按接口隔离原则，你会怎么拆？

   <!-- run: off -->
   ```cpp
   class Storage {
   public:
       virtual std::string read(const std::string& path) = 0;
       virtual void write(const std::string& path, const std::string& data) = 0;
       virtual void remove(const std::string& path) = 0;
       virtual std::vector<std::string> listFiles() = 0;
       virtual long getQuota() = 0;
       virtual void setQuota(long bytes) = 0;
   };
   ```

4. 下面的类每次运行都会真的发一封邮件，没法测试。用依赖倒置改造它，并写一个测试，检查 `run()` 确实给老板发了一封标题为「日报」的邮件。

   <!-- run: off -->
   ```cpp
   class ReportJob {
   public:
       void run() {
           SmtpMailer mailer("smtp.example.com");
           mailer.send("boss@example.com", "日报", buildReport());
       }
   private:
       std::string buildReport();
   };
   ```

<details>
<summary>点击查看答案</summary>

1. 编译失败（完整代码在 `lsp_vector_bool_ce.cpp`）。Clang 21 的报错是 `non-const lvalue reference to type 'bool' cannot bind to a temporary of type 'reference'`，GCC 13 的报错是 `cannot bind non-const lvalue reference of type 'bool&' to an rvalue of type 'bool'`。
   为了节省空间，`std::vector<bool>` 是一个特化版本，每个元素只占一个比特。比特无法取地址，所以 `v[0]` 返回的不是 `bool&`，而是一个代理对象。对所有其他的 `T`，`vector<T>` 都承诺「`operator[]` 返回 `T&`」，唯独 `vector<bool>` 不遵守。严格说模板特化不是继承，但道理相同：写给「所有 `vector<T>`」的泛型代码，换成 `vector<bool>` 就出错了。这是 C++ 标准库里公认的设计失误，需要真正的布尔数组时，可以用 `std::vector<char>` 或者 `std::bitset`。

2. ① 违反：调用者以为写成功了，数据却丢了，削弱了 `write()` 的后置条件。只读文件不应该有 `write()`，应该按接口隔离拆成 `Readable` 和 `Writable`。
   ② 不违反：行为和父类完全一致，只是更快，任何使用 `Repository` 的代码都可以放心替换。（前提是缓存不会返回过期数据，否则就削弱了「返回最新数据」的后置条件。）
   ③ 违反：使用 `Vector` 的代码会认为 `push_back(x)` 之后 `back() == x`，`SortedVector` 不保证这一点。`SortedVector` 应该组合一个 `Vector`，提供 `insert(x)` 而不是 `push_back`。

3. 按角色拆成三个接口：

   <!-- run: off -->
   ```cpp
   class FileReader  { public: virtual std::string read(const std::string& path) = 0; virtual std::vector<std::string> listFiles() = 0; /* 虚析构省略 */ };
   class FileRemover { public: virtual void remove(const std::string& path) = 0; virtual std::vector<std::string> listFiles() = 0; };
   class QuotaAdmin  { public: virtual long getQuota() = 0; virtual void setQuota(long bytes) = 0; };
   ```

   `listFiles` 出现在两个接口里也没关系，也可以单独拆出一个 `FileLister` 让前两个接口继承它。`write` 没有出现在这三个使用者里，它属于别的角色（比如上传功能），可以单独成为 `FileWriter`。具体的存储类可以同时实现所有这些接口。

4. 在业务这一侧定义发邮件的接口，`ReportJob` 通过构造函数接收它；测试时传入一个只把邮件记下来的实现：

   <!-- run: off -->
   ```cpp
   class Mailer {                                     // 接口归 ReportJob 这一侧所有
   public:
       virtual ~Mailer() = default;
       virtual void send(const std::string& to, const std::string& subject, const std::string& body) = 0;
   };

   class ReportJob {
   public:
       explicit ReportJob(Mailer& mailer) : mailer_(mailer) {}
       void run() { mailer_.send("boss@example.com", "日报", buildReport()); }
   private:
       std::string buildReport() { return "今日订单 42 笔"; }
       Mailer& mailer_;
   };

   class SmtpMailer : public Mailer { /* 真正连接 SMTP 服务器发信 */ };

   // 测试
   struct FakeMailer : Mailer {
       std::vector<std::string> sent;
       void send(const std::string& to, const std::string& subject, const std::string&) override { sent.push_back(to + "|" + subject); }
   };

   FakeMailer fake;
   ReportJob(fake).run();
   assert(fake.sent.size() == 1 && fake.sent[0] == "boss@example.com|日报");
   ```

   `main` 里组装真正的对象：`SmtpMailer smtp("smtp.example.com"); ReportJob(smtp).run();`。SMTP 服务器地址这种配置也随之从业务代码里移了出去。

</details>
