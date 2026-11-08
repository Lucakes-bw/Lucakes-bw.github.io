---
title: 原型模式
date: 2026-11-08 18:00:00
updated: 2026-11-08 18:00:00
categories:
  - 设计模式
  - 创建型模式
tags:
  - 设计模式
  - 原型
  - 深拷贝
  - CRTP
order: 5
description: 手里只有基类指针，怎么复制出一个同样的对象？按值拷贝会发生对象切片，逐个 dynamic_cast 又违反开闭原则。原型模式让对象自己实现 clone()。实测两个坑：孙类忘了重写 clone()，两个编译器都不报警告，蓝色填充的圆复制出来只剩一个普通的圆，用 CRTP 自动生成 clone() 可以根治；浅拷贝让新文档和模板共用图层，改了新文档，模板也变了。最后实测克隆的价值：解析一个 3D LUT 调色文件要 1～2 毫秒，克隆原型只要 3～7 微秒，快 300 多倍。
top_group_list: true
disableNunjucks: true
viz: [dp-prototype]
---

> **本文目标**：知道为什么通过基类指针复制对象需要原型模式；会写 `clone()`，并用 CRTP 避免忘记重写；分清浅拷贝和深拷贝；会用原型注册表；知道克隆在什么情况下能带来性能上的好处。
>
> **前置知识**：上一篇《建造者模式》；C/C++ 专栏的《多态与虚函数》（对象切片）、《智能指针：unique_ptr、shared_ptr 与 weak_ptr》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/10-prototype/` 目录。

## 1. 问题：只有基类指针，怎么复制

图形编辑器的画布上有各种图形：圆、文字、矩形……它们都保存在一个 `std::vector<std::unique_ptr<Shape>>` 里。用户选中几个图形按下「复制」，程序要为每个图形复制出一个一模一样的新对象。问题是，代码手里只有 `Shape*`，并不知道每个图形具体是什么类型：

```cpp
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

std::string num(double v) { std::ostringstream os; os << v; return os.str(); }

struct Shape {
    virtual ~Shape() = default;
    virtual std::string describe() const { return "一个形状"; }
};
struct Circle : Shape {
    double r = 1;
    std::string describe() const override { return "半径 " + num(r) + " 的圆"; }
};
struct Text : Shape {
    std::string content = "Hello";
    std::string describe() const override { return "文字「" + content + "」"; }
};

int main() {
    std::vector<std::unique_ptr<Shape>> canvas;
    auto c = std::make_unique<Circle>(); c->r = 2.5;
    canvas.push_back(std::move(c));
    canvas.push_back(std::make_unique<Text>());

    // 用户选中所有图形，按下「复制」
    // 尝试一：按值拷贝基类——发生对象切片，只复制了 Shape 那一部分
    for (const auto& s : canvas) {
        Shape copy = *s;
        std::cout << "按值拷贝：" << copy.describe() << '\n';
    }

    // 尝试二：逐个判断具体类型——能用，但每加一种图形都要回来改这里
    for (const auto& s : canvas) {
        std::unique_ptr<Shape> copy;
        if (auto* p = dynamic_cast<Circle*>(s.get())) copy = std::make_unique<Circle>(*p);
        else if (auto* p = dynamic_cast<Text*>(s.get())) copy = std::make_unique<Text>(*p);
        std::cout << "dynamic_cast：" << (copy ? copy->describe() : "不认识的类型") << '\n';
    }
}
```

```text
按值拷贝：一个形状
按值拷贝：一个形状
dynamic_cast：半径 2.5 的圆
dynamic_cast：文字「Hello」
```

- **按值拷贝**：`Shape copy = *s;` 只复制了 `Shape` 那一部分，圆的半径、文字的内容全没了，这就是对象切片（C/C++ 专栏《多态与虚函数》里讲过）。
- **逐个 `dynamic_cast`**：能用，但每加一种图形都要回来改这个 `if-else`，而且这样的代码往往不止一处（复制、粘贴、撤销、多选拖动……）。又是第 1 章说的散弹式修改。

C++ 的拷贝构造函数不能是虚函数：构造一个对象时必须先知道它的确切类型，而我们恰恰不知道。

## 2. 原型模式

### 2.1 意图

**原型模式**（Prototype）：用原型实例指定要创建的对象的种类，并且通过复制这个原型来创建新对象。

做法很简单：既然只有对象自己知道自己的真实类型，就让对象**自己复制自己**。在基类里声明一个虚函数 `clone()`，每个派生类用自己的拷贝构造函数实现它：

```cpp
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

std::string num(double v) { std::ostringstream os; os << v; return os.str(); }

struct Shape {
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> clone() const = 0;    // 「复制我自己」
    virtual std::string describe() const = 0;
};

struct Circle : Shape {
    double r = 1;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<Circle>(*this); }   // 调用自己的拷贝构造函数
    std::string describe() const override { return "半径 " + num(r) + " 的圆"; }
};

struct Text : Shape {
    std::string content = "Hello";
    std::unique_ptr<Shape> clone() const override { return std::make_unique<Text>(*this); }
    std::string describe() const override { return "文字「" + content + "」"; }
};

int main() {
    std::vector<std::unique_ptr<Shape>> canvas;
    auto c = std::make_unique<Circle>(); c->r = 2.5;
    canvas.push_back(std::move(c));
    canvas.push_back(std::make_unique<Text>());

    std::vector<std::unique_ptr<Shape>> clipboard;
    for (const auto& s : canvas) clipboard.push_back(s->clone());   // 不用知道具体类型
    for (const auto& s : clipboard) std::cout << "复制出：" << s->describe() << '\n';
}
```

```text
复制出：半径 2.5 的圆
复制出：文字「Hello」
```

```uml
label: 原型模式：Shape 声明纯虚函数 clone；Circle 和 Text 各自用拷贝构造函数实现 clone；调用者只通过 Shape 指针调用 clone
classes:
  - { id: Client, name: 复制粘贴的代码, x: 20, y: 44, w: 150 }
  - { id: S, name: Shape, stereo: interface, x: 270, y: 16, w: 240, ops: ['*+ clone() : unique_ptr<Shape>', '*+ describe() : string'] }
  - { id: C, name: Circle, x: 50, y: 190, w: 220, ops: ['+ clone()  // make_unique<Circle>(*this)'] }
  - { id: T, name: Text, x: 400, y: 190, w: 220, ops: ['+ clone()  // make_unique<Text>(*this)'] }
rels:
  - { from: Client, to: S, type: depend, label: 'clone()' }
  - { from: C, to: S, type: realize, mid: 150 }
  - { from: T, to: S, type: realize, mid: 150 }
```

复制的代码只调用 `s->clone()`，以后加多少种图形都不用改它。`clone()` 返回 `std::unique_ptr<Shape>`，明确表示调用者拥有这个新对象。

> C++ 支持**协变返回类型**：派生类重写虚函数时，可以把返回的 `Shape*` 改成 `Circle*`。但这只对裸指针和引用有效，`std::unique_ptr<Circle>` 和 `std::unique_ptr<Shape>` 是两个不相关的类型，不能协变。所以用智能指针时，派生类的 `clone()` 也只能返回 `std::unique_ptr<Shape>`。

## 3. 第一个坑：忘了重写 clone()

`clone()` 要求**每一个**派生类都重写它，包括派生类的派生类。后来有人加了一个带填充色的圆：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

struct Shape {
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> clone() const = 0;
    virtual std::string describe() const = 0;
};

struct Circle : Shape {
    double r = 1;
    std::unique_ptr<Shape> clone() const override { return std::make_unique<Circle>(*this); }
    std::string describe() const override { return "圆"; }
};

// 后来有人加了带填充色的圆，继承 Circle，重写了 describe，却忘了重写 clone
struct FilledCircle : Circle {
    std::string color = "红色";
    std::string describe() const override { return color + "填充的圆"; }
};

int main() {
    FilledCircle original;
    original.color = "蓝色";
    std::unique_ptr<Shape> copy = original.clone();   // 调用的是 Circle::clone
    std::cout << "原件：" << original.describe() << '\n';
    std::cout << "副本：" << copy->describe() << '\n';
    const Shape& c = *copy;
    std::cout << "类型相同吗？" << (typeid(c) == typeid(original) ? "是" : "不是") << '\n';
}
```

```text
原件：蓝色填充的圆
副本：圆
类型相同吗？不是
```

`FilledCircle` 重写了 `describe()`，却忘了重写 `clone()`，于是调用的是父类 `Circle::clone()`，它用 `Circle` 的拷贝构造函数只复制了 `Circle` 那一部分：**又是对象切片**，只不过这次藏在 `clone()` 里面。蓝色填充的圆，复制出来变成了一个普通的圆。

更麻烦的是，Clang 21 和 GCC 13 开着 `-Wall -Wextra` 都**没有任何警告**：`Circle::clone()` 已经实现了，从编译器的角度看，`FilledCircle` 继承它完全合法。

### 3.1 用 CRTP 自动生成 clone()

每个类的 `clone()` 写法都一样，只是类名不同。这正是模板擅长的事。用一个中间类模板 `Cloneable<Derived, Base>`，让它继承 `Base`，并替 `Derived` 实现 `clone()`：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

struct Shape {
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> clone() const = 0;
    virtual std::string describe() const = 0;
};

// Cloneable<Derived, Base>：继承 Base，并替 Derived 实现 clone()
template <typename Derived, typename Base = Shape>
struct Cloneable : Base {
    using Base::Base;
    std::unique_ptr<Shape> clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));   // 按真实类型拷贝
    }
};

struct Circle : Cloneable<Circle> {
    double r = 1;
    std::string describe() const override { return "圆"; }
};

struct FilledCircle : Cloneable<FilledCircle, Circle> {     // 只要按这个格式写继承，clone 就自动是对的
    std::string color = "红色";
    std::string describe() const override { return color + "填充的圆"; }
};

int main() {
    FilledCircle original;
    original.color = "蓝色";
    std::unique_ptr<Shape> copy = original.clone();
    std::cout << "原件：" << original.describe() << '\n';
    std::cout << "副本：" << copy->describe() << '\n';
    const Shape& c = *copy;
    std::cout << "类型相同吗？" << (typeid(c) == typeid(original) ? "是" : "不是") << '\n';
}
```

```text
原件：蓝色填充的圆
副本：蓝色填充的圆
类型相同吗？是
```

```uml
label: CRTP 自动生成 clone：Cloneable<Circle, Shape> 继承 Shape 并实现 clone；Circle 继承它；Cloneable<FilledCircle, Circle> 继承 Circle 并实现 clone；FilledCircle 继承它
classes:
  - { id: S, name: Shape, stereo: interface, x: 20, y: 16, w: 220, ops: ['*+ clone() : unique_ptr<Shape>'] }
  - { id: C1, name: 'Cloneable<Circle, Shape>', x: 20, y: 130, w: 220, ops: ['+ clone()  // 复制成 Circle'] }
  - { id: C, name: Circle, x: 20, y: 240, w: 220 }
  - { id: C2, name: 'Cloneable<FilledCircle, Circle>', x: 320, y: 240, w: 260, ops: ['+ clone()  // 复制成 FilledCircle'] }
  - { id: F, name: FilledCircle, x: 320, y: 350, w: 260 }
rels:
  - { from: C1, to: S, type: realize }
  - { from: C, to: C1, type: inherit }
  - { from: C2, to: C, type: inherit }
  - { from: F, to: C2, type: inherit }
```

这种「派生类把自己作为模板参数传给基类」的写法叫 **CRTP**（Curiously Recurring Template Pattern，奇异递归模板模式），「C++ 惯用法」那一章会专门讲。在这里，只要每个类都按 `struct X : Cloneable<X, 父类>` 的格式继承，`clone()` 就一定按真实类型复制，再也不会忘。

如果不想用 CRTP，至少可以做两件事：

- 把不打算再被继承的类标记成 `final`（`struct FilledCircle final : Circle`），这样就不会出现「孙类」；
- 写一个测试，对每种图形检查 `typeid(*s->clone()) == typeid(*s)`（练习 2）。

## 4. 第二个坑：浅拷贝

`clone()` 用拷贝构造函数复制对象，而默认的拷贝构造函数只是**逐个复制成员**。成员如果是指针（包括 `shared_ptr`），复制的只是指针本身，指向的对象还是同一个。这叫**浅拷贝**。

下面的文档用 `shared_ptr` 保存图层。从「海报模板」新建一份文档，改了新文档的背景：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Layer {
    std::string name;
    std::string color;
};

struct Document {
    std::string title;
    std::vector<std::shared_ptr<Layer>> layers;    // 图层用 shared_ptr 保存

    Document shallowCopy() const { return *this; }  // 默认的拷贝：只复制了指针，图层还是同一份

    Document deepCopy() const {                      // 深拷贝：每个图层都复制一份新的
        Document d{title, {}};
        for (const auto& l : layers) d.layers.push_back(std::make_shared<Layer>(*l));
        return d;
    }
};

void show(const char* who, const Document& d) {
    std::cout << "  " << who << "《" << d.title << "》的背景：" << d.layers[0]->color
              << "（这个图层被 " << d.layers[0].use_count() << " 个文档共用）\n";
}

int main() {
    Document poster{"海报模板", {std::make_shared<Layer>(Layer{"背景", "白色"})}};

    std::cout << "浅拷贝：\n";
    {
        Document a = poster.shallowCopy();
        a.title = "活动海报";
        a.layers[0]->color = "红色";               // 只想改新文档的背景
        show("新文档", a);
        show("模板", poster);                      // 模板也变成红色了
    }

    poster.layers[0]->color = "白色";              // 把模板改回来
    std::cout << "深拷贝：\n";
    Document b = poster.deepCopy();
    b.title = "活动海报";
    b.layers[0]->color = "红色";
    show("新文档", b);
    show("模板", poster);
}
```

```text
浅拷贝：
  新文档《活动海报》的背景：红色（这个图层被 2 个文档共用）
  模板《海报模板》的背景：红色（这个图层被 2 个文档共用）
深拷贝：
  新文档《活动海报》的背景：红色（这个图层被 1 个文档共用）
  模板《海报模板》的背景：白色（这个图层被 1 个文档共用）
```

浅拷贝以后，两个文档共用同一个图层，改了新文档，模板也被改成了红色，以后再用这个模板新建的文档背景全是红的。**深拷贝**把每个图层也复制一份，新文档改的是自己的图层，模板不受影响。下面的演示展示了两种拷贝在内存里的区别：

<div class="dsviz" data-viz="dp-prototype"></div>

不同类型的成员，默认拷贝的行为不同：

| 成员类型 | 默认拷贝构造函数的行为 | 写 `clone()` 时要注意 |
| --- | --- | --- |
| 值（`int`、`std::string`、`std::vector<int>`） | 深拷贝 | 不用管 |
| `std::unique_ptr<T>` | 拷贝构造函数被删除，编译不过 | 必须自己写深拷贝（编译器会逼你） |
| `std::shared_ptr<T>` | 浅拷贝，共用对象 | 要想清楚：是该共用，还是该各有一份 |
| 裸指针 `T*` | 浅拷贝，共用对象 | 最危险：两个对象析构时可能重复释放 |

原型模式里，`clone()` 通常应该是深拷贝：克隆出来的对象应该能独立修改。只有被指向的对象**永远不会被修改**时，共用才是安全的，而且能省下复制的开销（练习 4）。

## 5. 原型注册表

把一组预先配置好的对象保存起来，需要新对象时就克隆其中一个，再修改少量属性，这就是**原型注册表**（prototype manager）。图形编辑器的工具栏正是这样：每个按钮背后是一个配置好颜色、大小、字体的原型，拖到画布上，就是克隆一份再设置位置：

```cpp
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

std::string num(double v) { std::ostringstream os; os << v; return os.str(); }

struct Shape {
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> clone() const = 0;
    virtual std::string describe() const = 0;
    double x = 0, y = 0;
};

template <typename Derived>
struct Cloneable : Shape {
    std::unique_ptr<Shape> clone() const override { return std::make_unique<Derived>(static_cast<const Derived&>(*this)); }
};

struct Circle : Cloneable<Circle> {
    double r = 10;
    std::string color = "黑色";
    std::string describe() const override { return color + "的圆，半径 " + num(r) + "，位于 (" + num(x) + ", " + num(y) + ")"; }
};

struct Label : Cloneable<Label> {
    std::string text, font = "宋体";
    int size = 12;
    std::string describe() const override { return font + " " + std::to_string(size) + " 号字「" + text + "」，位于 (" + num(x) + ", " + num(y) + ")"; }
};

class Palette {                                     // 原型注册表
public:
    void add(const std::string& name, std::unique_ptr<Shape> proto) { protos_[name] = std::move(proto); }
    std::unique_ptr<Shape> create(const std::string& name, double x, double y) const {
        auto it = protos_.find(name);
        if (it == protos_.end()) return nullptr;
        auto s = it->second->clone();               // 克隆原型
        s->x = x; s->y = y;                         // 只改需要改的
        return s;
    }
private:
    std::map<std::string, std::unique_ptr<Shape>> protos_;
};

int main() {
    Palette palette;
    auto red = std::make_unique<Circle>(); red->color = "红色"; red->r = 20;
    palette.add("红色大圆", std::move(red));
    auto title = std::make_unique<Label>(); title->text = "标题"; title->font = "黑体"; title->size = 32;
    palette.add("标题文字", std::move(title));

    std::vector<std::unique_ptr<Shape>> canvas;
    canvas.push_back(palette.create("红色大圆", 100, 50));
    canvas.push_back(palette.create("红色大圆", 300, 50));
    canvas.push_back(palette.create("标题文字", 120, 10));
    for (const auto& s : canvas) std::cout << s->describe() << '\n';
}
```

```text
红色的圆，半径 20，位于 (100, 50)
红色的圆，半径 20，位于 (300, 50)
黑体 32 号字「标题」，位于 (120, 10)
```

和前几篇的工厂相比，原型注册表不需要为每种「配置」写一个工厂函数或工厂子类：用户在程序运行时自己调好一个图形，把它加进工具栏，它就成了一个新的原型。新的「种类」可以在运行时产生，这是工厂做不到的。

## 6. 实测：克隆比重新构造快多少

如果对象的构造代价很高，克隆一个现成的原型还能节省大量时间。一个和音视频相关的例子：**3D LUT 调色滤镜**。FFmpeg 的 `lut3d` 滤镜读取 `.cube` 文件，文件里是一个 33×33×33 的颜色查找表，一共 35937 行文本。每次创建滤镜都重新解析文件，还是先解析一次作为原型、之后都克隆它？`bench_clone.cpp` 测量了两种做法（核心部分）：

<!-- run: off -->
```cpp
class Lut3D : public Filter {
public:
    explicit Lut3D(const std::string& cube);         // 构造：逐行解析 .cube 文本
    std::unique_ptr<Filter> clone() const override { return std::make_unique<Lut3D>(*this); }   // 克隆：复制数组
private:
    int size_ = 0;
    std::vector<float> table_;                       // 33 × 33 × 33 × 3 个 float，约 421 KB
};
```

```text
.cube 文本 947 KB，LUT 表 421 KB
每次重新解析：  1008.0 µs
克隆原型：         3.1 µs（快 327 倍）
```

| 创建一个 LUT 滤镜 | Clang 21（macOS） | GCC 13（Linux） |
| --- | --- | --- |
| 正常解析 `.cube` 文本 | 约 1.0 ms（刚编译完的第一次运行约 1.6 ms） | 约 2.35 ms |
| 克隆原型 | 约 3.1 µs | 约 7.3 µs |
| 克隆快多少 | 约 320 倍 | 约 320 倍 |

<figure class="diagram">
<svg viewBox="0 0 740 230" role="img" aria-label="创建一个 3D LUT 滤镜的耗时（对数刻度）：每行都 sscanf 的解析 145 到 231 毫秒；正常解析 1 到 2.4 毫秒；克隆原型 3 到 7 微秒"><g style="font-family:var(--mono)"><text x="10.0" y="18.0" text-anchor="start" style="fill:var(--muted);font-size:11px">创建一个 33³ 的 3D LUT 滤镜（微秒，对数刻度，每格 10 倍）</text><line x1="190.0" y1="30" x2="190.0" y2="180" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="190.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 µs</text><line x1="273.3" y1="30" x2="273.3" y2="180" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="273.3" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 µs</text><line x1="356.7" y1="30" x2="356.7" y2="180" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="356.7" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">100 µs</text><line x1="440.0" y1="30" x2="440.0" y2="180" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="440.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 ms</text><line x1="523.3" y1="30" x2="523.3" y2="180" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="523.3" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 ms</text><line x1="606.7" y1="30" x2="606.7" y2="180" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="606.7" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">100 ms</text><line x1="690.0" y1="30" x2="690.0" y2="180" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="690.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 s</text><text x="182.0" y="56.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">每行都 sscanf 的解析</text><rect x="190.0" y="36.0" width="447.0" height="14.0" rx="3" style="fill:var(--danger);opacity:1"/><text x="643.0" y="47.0" text-anchor="start" style="fill:var(--danger);font-size:10.5px">Clang 231.5 ms</text><rect x="190.0" y="54.0" width="430.2" height="14.0" rx="3" style="fill:var(--danger);opacity:0.6"/><text x="626.2" y="65.0" text-anchor="start" style="fill:var(--danger);font-size:10.5px">GCC 145.2 ms</text><text x="182.0" y="104.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">正常解析</text><rect x="190.0" y="84.0" width="250.4" height="14.0" rx="3" style="fill:var(--warn);opacity:1"/><text x="446.4" y="95.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">Clang 1.0 ms</text><rect x="190.0" y="102.0" width="280.9" height="14.0" rx="3" style="fill:var(--warn);opacity:0.6"/><text x="476.9" y="113.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">GCC 2.4 ms</text><text x="182.0" y="152.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">克隆原型</text><rect x="190.0" y="132.0" width="40.9" height="14.0" rx="3" style="fill:var(--accent);opacity:1"/><text x="236.9" y="143.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">Clang 3.1 µs</text><rect x="190.0" y="150.0" width="72.4" height="14.0" rx="3" style="fill:var(--accent);opacity:0.6"/><text x="268.4" y="161.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">GCC 7.4 µs</text><text x="10.0" y="222.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">每种各运行多次取中位数；Apple M6，GCC 运行在 Ubuntu 24.04 容器里</text></g></svg>
</figure>

解析要逐个字符地把文本转换成浮点数，克隆只需要一次 421 KB 的内存复制。一个视频剪辑软件里，同一个调色预设可能被几十个片段使用，每个片段都要一个独立的滤镜对象（各自还有强度等参数可以调），克隆原型就省下了几十次解析。

> **顺带踩到的一个坑**。第一版解析代码在每一行都调用 `sscanf(p, "LUT_3D_SIZE %d", &size)` 来判断是不是尺寸那一行，结果解析一次要 **231 ms**（macOS）/ **145 ms**（Linux），比现在慢了 100 多倍（运行 `bench_clone --sscanf` 可以复现）。原因是 `sscanf` 在开始解析之前会先对整个输入字符串求长度，而 `p` 指向的是剩下的全部文本，于是 35937 行每行都要扫一遍后面近 1 MB 的内容。几年前 GTA Online 的加载时间长达好几分钟，原因之一就是这个。改成先用 `strncmp` 比较行首就好了。

## 7. 原型和其他创建型模式

- **值类型不需要原型模式**。C++ 的值类型（`std::string`、本专栏上一篇的 `EncodeJob`）本来就有拷贝构造函数，复制一个值就是在用原型。原型模式只在**通过基类指针操作多态对象**时才需要。
- **原型注册表可以代替一大堆工厂子类**：工厂方法要为每种产品写一个创建者子类，原型注册表只要往表里放一个配置好的对象。
- **和建造者的关系**：上一篇提到，建造者通常只用一次；如果要反复创建相似的对象，可以先配置好一个建造者（或者一个产品）作为模板，每次复制一份再修改，这就是原型的思路。
- 到这里，**创建型模式**就讲完了：单例控制实例的个数，工厂方法和抽象工厂决定创建哪个类，建造者分步创建复杂对象，原型通过复制来创建。下一章进入结构型模式。

## 8. 小结

- 只有基类指针时，按值拷贝会切片，逐个 `dynamic_cast` 违反开闭原则；拷贝构造函数不能是虚函数
- **原型模式**：每个类实现虚函数 `clone()`，用自己的拷贝构造函数复制自己，调用者只通过基类调用
- 坑一：孙类忘了重写 `clone()`，复制时发生切片。实测：两个编译器开 `-Wall -Wextra` 都不警告；用 CRTP 的 `Cloneable<Derived, Base>` 自动生成 `clone()` 可以根治
- 坑二：默认拷贝是浅拷贝，`shared_ptr` 和裸指针成员会被共用。实测：从模板新建的文档改了背景，模板也变了。`clone()` 通常应该是深拷贝
- 原型注册表：保存配置好的对象，克隆后再修改；新的「种类」可以在运行时产生
- 实测：创建 3D LUT 滤镜，正常解析 1～2.4 ms，克隆原型 3～7 µs，快约 320 倍；每行都 `sscanf` 会让解析慢到 145～231 ms

## 9. 练习

1. 为什么 C++ 没有「虚拷贝构造函数」？`clone()` 是怎么绕开这个限制的？

2. 3.1 节提到可以写一个测试来发现「忘了重写 `clone()`」。对第 5 节工具栏里的所有原型，写出这个检查。

3. 图形编辑器支持「组合」：一个 `Group` 里可以包含任意多个图形（包括别的 `Group`）。`Group` 用 `std::vector<std::unique_ptr<Shape>>` 保存子图形。为它实现 `clone()`。

4. 第 6 节的 LUT 滤镜，克隆时要复制 421 KB 的表格。如果调色表加载之后**永远不会被修改**（每个滤镜对象只是各自有一个可调的「强度」参数），怎么让克隆几乎不花时间？如果以后需要支持「在某个片段上微调这张表」，又该怎么办？

<details>
<summary>点击查看答案</summary>

1. 构造一个对象，编译器必须在编译期就知道它的确切类型，才能知道分配多大的空间、调用哪个构造函数。而虚函数的意义恰恰是「运行时才知道调用哪个」，两者矛盾。`clone()` 把问题换了一个方向：它不是构造函数，而是一个普通的虚函数，在**已经存在的对象**上调用。运行时根据对象的真实类型找到对应的 `clone()`，而在每个类自己的 `clone()` 里，类型是确定的（`*this` 就是那个类），可以直接调用自己的拷贝构造函数。

2. 克隆每个原型，比较克隆结果和原型的真实类型：

   <!-- run: off -->
   ```cpp
   #include <typeinfo>

   bool checkClones(const std::map<std::string, std::unique_ptr<Shape>>& protos) {
       bool ok = true;
       for (const auto& [name, proto] : protos) {
           auto copy = proto->clone();
           const Shape& c = *copy;
           const Shape& p = *proto;
           if (typeid(c) != typeid(p)) {
               std::cout << name << "：clone() 复制成了 " << typeid(c).name() << '\n';
               ok = false;
           }
       }
       return ok;
   }
   ```

   `typeid` 作用于多态类型的对象时，返回的是对象的**真实**类型。这个测试不能证明 `clone()` 复制了所有成员（那需要逐个比较字段），但能抓住最常见的「忘了重写」。

3. `Group` 不能用默认的拷贝构造函数（`unique_ptr` 不能拷贝），要逐个克隆子图形：

   <!-- run: off -->
   ```cpp
   struct Group : Shape {
       std::vector<std::unique_ptr<Shape>> children;

       std::unique_ptr<Shape> clone() const override {
           auto g = std::make_unique<Group>();
           g->x = x; g->y = y;                       // 复制自己的成员
           for (const auto& c : children)
               g->children.push_back(c->clone());     // 子图形各自克隆自己，Group 里套 Group 时自然递归
           return g;
       }
       std::string describe() const override { return "包含 " + std::to_string(children.size()) + " 个图形的组"; }
   };
   ```

   这里不能用 CRTP 的 `Cloneable<Group>`，因为它依赖拷贝构造函数。另一种做法是给 `Group` 写一个深拷贝的拷贝构造函数，再让 `Cloneable` 调用它。这种「整体和部分用同一个接口」的树形结构，就是结构型模式那一章的**组合模式**。

4. 不可变的数据可以安全地共用：把表格放进 `std::shared_ptr<const std::vector<float>>`，克隆时只复制这个指针（以及「强度」等少量参数），所有克隆出来的滤镜共用同一张表：

   <!-- run: off -->
   ```cpp
   class Lut3D : public Filter {
       std::shared_ptr<const std::vector<float>> table_;   // const：谁也不能修改它，所以共用是安全的
       float strength_ = 1.0f;                             // 每个滤镜自己的参数
   public:
       std::unique_ptr<Filter> clone() const override { return std::make_unique<Lut3D>(*this); }   // 只复制一个指针
   };
   ```

   这样克隆几乎不花时间，内存里也只有一份表。这就是结构型模式那一章的**享元模式**的思路。
   如果以后要支持微调，可以用**写时复制**（copy-on-write）：平时共用；某个滤镜要修改表格时，先检查是否只有自己在用（`table_.use_count() == 1`），不是就先复制一份新表、改成指向它，再修改。没被修改过的滤镜仍然共用原来那张表。

</details>
