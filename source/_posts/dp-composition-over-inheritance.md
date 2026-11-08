---
title: 组合优于继承与 C++ 的四种多态
date: 2026-11-08 13:00:00
updated: 2026-11-08 13:00:00
categories:
  - 设计模式
  - 基础与设计原则
tags:
  - 设计模式
  - 组合
  - 多态
  - std::variant
  - 性能
order: 5
description: 继承复用代码有两个隐患：子类依赖了基类的实现细节，统计插入次数的 CountingSet 一次插入 3 个元素却数出 6 次；功能组合靠子类实现，3 种功能就要 8 个类。改成组合以后两个问题都消失了。C++ 的多态不止虚函数一种，还有 std::function、模板和 std::variant。实测 100 万个形状求面积：写法之间最多差 1.6 倍，数据怎么排列却能差 6 倍，按类型分桶的模板版只要 0.48 纳秒一个元素。最后给出四种写法的选择指南。
top_group_list: true
disableNunjucks: true
viz: [dp-poly]
---

> **本文目标**：理解「组合优于继承」的具体理由（脆弱的基类、子类爆炸），会把实现继承改成组合加委托；掌握 C++ 实现多态的四种方式（虚函数、`std::function`、模板、`std::variant`），知道它们在扩展性、存储方式和性能上的差别，能根据场景选择。
>
> **前置知识**：本章前四篇；C/C++ 专栏的《多态与虚函数》《std::function 与 std::bind》《模板入门》《std::variant：类型安全的联合体》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/05-composition/` 目录。

## 1. 继承的两种用途

《为什么需要设计模式》里提到了 GoF 的第二条原则：**优先使用对象组合，而不是类继承**。这句话常被误解成「不要用继承」，其实它针对的是继承的一种特定用途。继承在 C++ 里同时干了两件事：

- **接口继承**：子类承诺实现父类声明的接口，使用者可以通过父类的指针或引用使用任何子类。这是多态，前几篇的 `Formatter`、`Rule`、`OrderStore` 都是这样用的。
- **实现继承**：子类为了**复用父类的代码**而继承它，再重写其中一部分。

「组合优于继承」反对的是第二种。下面用两个例子说明，为了复用代码而继承，会带来什么问题。

## 2. 问题一：脆弱的基类

### 2.1 数出了两倍

假设有一个整数集合 `IntSet`，现在想知道「一共尝试插入过多少个元素」（包括重复的）。最省事的办法是继承它，重写两个插入函数，在里面计数。这个例子改编自 Joshua Bloch 的《Effective Java》：

```cpp
#include <iostream>
#include <set>
#include <vector>

class IntSet {
public:
    virtual ~IntSet() = default;
    virtual bool insert(int x) { return s_.insert(x).second; }
    virtual void insertAll(const std::vector<int>& xs) {
        for (int x : xs) insert(x);          // 实现细节：逐个调用 insert
    }
    size_t size() const { return s_.size(); }

private:
    std::set<int> s_;
};

// 想统计「一共尝试插入过多少个元素」，于是继承 IntSet，重写两个插入函数
class CountingSet : public IntSet {
public:
    bool insert(int x) override {
        ++attempts_;
        return IntSet::insert(x);
    }
    void insertAll(const std::vector<int>& xs) override {
        attempts_ += xs.size();
        IntSet::insertAll(xs);               // 基类里又会调用 insert，而 insert 是虚函数……
    }
    size_t attempts() const { return attempts_; }

private:
    size_t attempts_ = 0;
};

int main() {
    CountingSet s;
    s.insertAll({1, 2, 3});
    std::cout << "元素个数 " << s.size() << "，尝试插入次数 " << s.attempts() << "（期望 3）\n";
}
```

```text
元素个数 3，尝试插入次数 6（期望 3）
```

插入 3 个元素，却数出了 6 次。原因在基类 `IntSet::insertAll` 的实现里：它逐个调用 `insert`，而 `insert` 是虚函数，于是调回了子类的 `CountingSet::insert`，每个元素又被数了一次：

```uml
label: 时序图：main 调用 CountingSet 的 insertAll，计数加 3 后调用 IntSet::insertAll；IntSet::insertAll 对每个元素调用虚函数 insert，又回到 CountingSet::insert，每次再加 1，共计 6
participants:
  - { id: m, name: main }
  - { id: c, name: 'CountingSet（子类部分）' }
  - { id: b, name: 'IntSet（基类部分）' }
messages:
  - { from: m, to: c, text: 'insertAll({1,2,3})', n: 1, note: 'attempts_ += 3' }
  - { from: c, to: b, text: 'IntSet::insertAll(xs)', n: 2 }
  - { from: b, to: c, text: 'insert(x)  // 虚函数，调回子类', n: 3, hl: warn, note: '++attempts_' }
  - { from: c, to: b, text: 'IntSet::insert(x)', n: 4 }
  - { from: b, to: c, type: reply }
  - { from: c, to: b, type: reply }
  - { from: b, to: c, type: reply }
  - { from: c, to: m, type: reply, text: 'attempts_ = 6' }
frags:
  - { from: 2, to: 5, label: 'loop [每个元素]' }
```

### 2.2 为什么叫「脆弱」

更糟的是，`CountingSet` 的正确性取决于 `IntSet` 内部**怎么实现**，而这些实现细节没有写在接口里，随时可能变化：

- 如果 `IntSet` 的作者某天把 `insertAll` 优化成一次性插入（不再逐个调用 `insert`），`CountingSet` 的计数会突然从 6 变成 3。
- 如果你「修复」这个 bug，把 `CountingSet::insertAll` 里的 `attempts_ += xs.size()` 删掉，那么等基类优化之后，计数又会变成 0。

基类的修改不改变任何接口、所有基类自己的测试都通过，却悄悄改变了子类的行为。这就是**脆弱基类问题**（fragile base class）：继承让子类和父类的**实现**绑在了一起，父类的作者没法放心地修改自己的内部实现，因为他不知道有哪些子类依赖了这些细节。这也是上一篇里氏替换原则的另一面：子类不但要遵守父类的契约，还不能依赖父类没有承诺的东西。

### 2.3 改成组合

不继承 `IntSet`，而是**持有**一个 `IntSet`，只通过它的公开接口使用它，这种做法叫**委托**（delegation）：

```cpp
#include <iostream>
#include <set>
#include <vector>

class IntSet {
public:
    bool insert(int x) { return s_.insert(x).second; }
    void insertAll(const std::vector<int>& xs) {
        for (int x : xs) insert(x);
    }
    size_t size() const { return s_.size(); }

private:
    std::set<int> s_;
};

class CountingSet {                          // 不是一种 IntSet，而是「持有」一个 IntSet
public:
    bool insert(int x) {
        ++attempts_;
        return set_.insert(x);
    }
    void insertAll(const std::vector<int>& xs) {
        attempts_ += xs.size();
        set_.insertAll(xs);                  // IntSet 内部怎么实现，都调不回 CountingSet
    }
    size_t size() const { return set_.size(); }
    size_t attempts() const { return attempts_; }

private:
    IntSet set_;
    size_t attempts_ = 0;
};

int main() {
    CountingSet s;
    s.insertAll({1, 2, 3});
    s.insert(2);
    std::cout << "元素个数 " << s.size() << "，尝试插入次数 " << s.attempts() << "（期望 4）\n";
}
```

```text
元素个数 3，尝试插入次数 4（期望 4）
```

`IntSet::insertAll` 内部调用的 `insert` 是 `IntSet` 自己的，不可能绕回 `CountingSet`。不管 `IntSet` 以后怎么改内部实现，只要公开接口的行为不变，`CountingSet` 就是正确的。代价是要手写几个转发函数（`size()`）；如果 `CountingSet` 还需要被当作「一种集合」传给别的函数，就让 `IntSet` 和 `CountingSet` 都实现同一个接口，这正是后面要讲的**装饰器模式**。

## 3. 问题二：子类爆炸

第二个问题出现在「功能组合」上。假设文件流有三种可选的功能：缓冲、压缩、加密，它们可以任意组合。用继承实现，每一种组合都要一个子类：

<figure class="diagram">
<svg viewBox="0 0 740 300" role="img" aria-label="用继承组合缓冲、压缩、加密三种功能，需要 8 个类；用组合只要 3 个装饰器加 1 个数据源，运行时一层套一层"><g style="font-family:var(--mono)"><text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">继承：每种组合一个子类</text><rect x="150" y="36" width="120" height="26" rx="4" style="fill:var(--panel-2);stroke:var(--fg-2);stroke-width:1.2"/><text x="210.0" y="54.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">FileStream</text><path d="M210,62 V76 H30 V265" style="fill:none;stroke:var(--fg-2);stroke-width:1.2"/><path d="M30,97 H40" style="fill:none;stroke:var(--fg-2);stroke-width:1.2"/><rect x="40" y="86" width="220" height="22" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.2"/><text x="48.0" y="101.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Buffered</text><path d="M30,125 H40" style="fill:none;stroke:var(--fg-2);stroke-width:1.2"/><rect x="40" y="114" width="220" height="22" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.2"/><text x="48.0" y="129.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Compressed</text><path d="M30,153 H40" style="fill:none;stroke:var(--fg-2);stroke-width:1.2"/><rect x="40" y="142" width="220" height="22" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.2"/><text x="48.0" y="157.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Encrypted</text><path d="M30,181 H40" style="fill:none;stroke:var(--fg-2);stroke-width:1.2"/><rect x="40" y="170" width="220" height="22" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.2"/><text x="48.0" y="185.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Buffered + Compressed</text><path d="M30,209 H40" style="fill:none;stroke:var(--fg-2);stroke-width:1.2"/><rect x="40" y="198" width="220" height="22" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.2"/><text x="48.0" y="213.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Buffered + Encrypted</text><path d="M30,237 H40" style="fill:none;stroke:var(--fg-2);stroke-width:1.2"/><rect x="40" y="226" width="220" height="22" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.2"/><text x="48.0" y="241.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Compressed + Encrypted</text><path d="M30,265 H40" style="fill:none;stroke:var(--fg-2);stroke-width:1.2"/><rect x="40" y="254" width="220" height="22" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.2"/><text x="48.0" y="269.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Buffered + Compressed + Encrypted</text><text x="270.0" y="160.0" text-anchor="start" style="fill:var(--warn);font-size:16px;font-weight:700">8 个类</text><text x="270.0" y="182.0" text-anchor="start" style="fill:var(--muted);font-size:11px">再加一个 SocketStream：16 个</text><text x="270.0" y="200.0" text-anchor="start" style="fill:var(--muted);font-size:11px">再加一种功能（日志）：32 个</text><text x="450.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">组合：功能是可以套起来的对象</text><rect x="450" y="40" width="270" height="170" rx="6" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.6"/><text x="460.0" y="58.0" text-anchor="start" style="fill:var(--accent);font-size:12px;font-weight:700">Encrypted</text><rect x="472" y="70" width="226" height="110" rx="6" style="fill:var(--panel);stroke:var(--link);stroke-width:1.6"/><text x="482.0" y="88.0" text-anchor="start" style="fill:var(--link);font-size:12px;font-weight:700">Compressed</text><rect x="494" y="100" width="182" height="50" rx="6" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.6"/><text x="504.0" y="118.0" text-anchor="start" style="fill:var(--warn);font-size:12px;font-weight:700">Buffered</text><rect x="516" y="130" width="138" height="-10" rx="6" style="fill:var(--panel);stroke:var(--fg-2);stroke-width:1.6"/><text x="526.0" y="148.0" text-anchor="start" style="fill:var(--fg-2);font-size:12px;font-weight:700">FileStream</text><text x="450.0" y="238.0" text-anchor="start" style="fill:var(--accent);font-size:13px;font-weight:700">3 个装饰器 + 1 个数据源 = 4 个类</text><text x="450.0" y="258.0" text-anchor="start" style="fill:var(--muted);font-size:11px">任意组合在运行时拼出来：</text><text x="450.0" y="276.0" text-anchor="start" style="fill:var(--muted);font-size:11px">Encrypted(Compressed(Buffered(File)))</text></g></svg>
</figure>

| 数据源 × 可选功能 | 继承需要的类 | 组合需要的类 |
| --- | --- | --- |
| 1 种数据源 × 3 种功能 | 2³ = 8 | 1 + 3 = 4 |
| 2 种数据源（文件、网络）× 3 种功能 | 2 × 2³ = 16 | 2 + 3 = 5 |
| 2 种数据源 × 5 种功能 | 2 × 2⁵ = 64 | 2 + 5 = 7 |

继承把「组合」固定在了编译期的类型里，每种组合都要一个名字、一个类。组合把每种功能做成一个独立的对象，运行时想要哪几种就套哪几种：`Encrypted(Compressed(Buffered(File)))`。类的数量从乘法变成了加法，而且套的顺序也可以调整（先压缩再加密，还是先加密再压缩）。这也是**装饰器模式**，结构型模式那一章会详细讲。

## 4. C++ 的四种多态

「组合优于继承」说的是怎么组织代码，落到 C++ 上，还有一个更具体的问题：**多态不一定要用虚函数**。C++ 至少有四种方式让同一段代码处理不同类型的对象：

```cpp
#include <functional>
#include <iostream>
#include <memory>
#include <variant>
#include <vector>

// ---------- ① 虚函数：继承一个接口 ----------
struct Shape {
    virtual ~Shape() = default;
    virtual double area() const = 0;
};
struct VCircle : Shape { double r; explicit VCircle(double r) : r(r) {} double area() const override { return 3.14159 * r * r; } };
struct VRect : Shape { double w, h; VRect(double w, double h) : w(w), h(h) {} double area() const override { return w * h; } };

double totalVirtual(const std::vector<std::unique_ptr<Shape>>& v) {
    double sum = 0;
    for (const auto& s : v) sum += s->area();
    return sum;
}

// ---------- 下面三种用的是普通的类，没有基类，也没有虚函数 ----------
struct Circle { double r; double area() const { return 3.14159 * r * r; } };
struct Rect { double w, h; double area() const { return w * h; } };

// ---------- ② std::function：只要「能调用、返回 double」就行 ----------
double totalFunction(const std::vector<std::function<double()>>& v) {
    double sum = 0;
    for (const auto& f : v) sum += f();
    return sum;
}

// ---------- ③ 模板：编译期确定类型，每种类型生成一份代码 ----------
template <typename T>
double totalOf(const std::vector<T>& v) {
    double sum = 0;
    for (const auto& s : v) sum += s.area();
    return sum;
}

// ---------- ④ std::variant：类型的集合是固定的 ----------
using AnyShape = std::variant<Circle, Rect>;

double totalVariant(const std::vector<AnyShape>& v) {
    double sum = 0;
    for (const auto& s : v) sum += std::visit([](const auto& x) { return x.area(); }, s);
    return sum;
}

int main() {
    std::vector<std::unique_ptr<Shape>> v1;
    v1.push_back(std::make_unique<VCircle>(1));
    v1.push_back(std::make_unique<VRect>(2, 3));

    Circle c{1};
    Rect r{2, 3};
    std::vector<std::function<double()>> v2 = {[c] { return c.area(); }, [r] { return r.area(); }};

    std::vector<Circle> circles = {c};
    std::vector<Rect> rects = {r};

    std::vector<AnyShape> v4 = {c, r};

    std::cout << "① 虚函数       " << totalVirtual(v1) << '\n';
    std::cout << "② std::function " << totalFunction(v2) << '\n';
    std::cout << "③ 模板         " << totalOf(circles) + totalOf(rects) << '\n';
    std::cout << "④ std::variant  " << totalVariant(v4) << '\n';
}
```

```text
① 虚函数       9.14159
② std::function 9.14159
③ 模板         9.14159
④ std::variant  9.14159
```

四种写法结果相同，背后的机制却很不一样：

- **① 虚函数**：所有形状继承同一个基类，通过基类指针调用。类型在运行时确定，任何人都可以再写一个子类。对象通常分散在堆上，容器里存的是指针。
- **② `std::function`**：不要求继承任何基类，只要「能调用、签名对得上」就能装进去。它内部用**类型擦除**把不同的可调用对象包装成同一个类型；小的可调用对象直接存在 `std::function` 内部（实测 `sizeof` 是 32 字节），不用分配堆内存。
- **③ 模板**：编译期确定类型，每种类型生成一份代码，调用可以被内联。代价是不同类型不能放进同一个容器（`vector<Circle>` 和 `vector<Rect>` 是两个类型），本例只能按类型分开存放，分别求和。
- **④ `std::variant`**：一个值是「几种类型之一」，类型的集合在定义时就固定了。对象按值连续存放，`std::visit` 根据当前存的是哪种类型跳到对应的代码。

### 4.1 实测：每个元素多少纳秒

`bench.cpp` 生成 100 万个形状（圆、矩形、三角形随机出现），用四种写法分别求总面积，再加上传统的 `enum` + `switch` 作为基准。每种写法运行 31 次取中位数，分三种数据排列：

- **类型随机**：三种形状随机交错，对象按创建顺序存放；
- **按类型排序**：先是所有的圆，再是矩形，最后是三角形；
- **对象打乱**：只对虚函数版有意义，类型仍然随机，但堆上的对象在内存里的顺序和遍历顺序无关（模拟真实程序里对象在不同时间分配的情况）。

<figure class="diagram">
<svg viewBox="0 0 740 340" role="img" aria-label="每个元素的耗时：类型随机时，switch、虚函数、std::function、variant 都在 3 到 4.6 纳秒；按类型排序后都降到 0.6 到 0.8 纳秒；按类型分桶的模板版 0.5 到 0.7 纳秒；虚函数的对象在内存中打乱后升到 6.6 到 7.4 纳秒"><g style="font-family:var(--mono)"><text x="10.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">Clang 21（libc++）</text><text x="120.0" y="54.0" text-anchor="end" style="fill:var(--fg);font-size:11px">enum + switch</text><rect x="128" y="36" width="75.66" height="14" rx="2" style="fill:var(--warn)"/><text x="208.7" y="47.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">2.91</text><rect x="128" y="53" width="15.079999999999998" height="14" rx="2" style="fill:var(--accent)"/><text x="148.1" y="64.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.58</text><text x="120.0" y="98.0" text-anchor="end" style="fill:var(--fg);font-size:11px">① 虚函数</text><rect x="128" y="80" width="108.94000000000001" height="14" rx="2" style="fill:var(--warn)"/><text x="241.9" y="91.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">4.19</text><rect x="128" y="97" width="19.76" height="14" rx="2" style="fill:var(--accent)"/><text x="152.8" y="108.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.76</text><rect x="128" y="114" width="170.29999999999998" height="14" rx="2" style="fill:var(--danger)"/><text x="303.3" y="125.0" text-anchor="start" style="fill:var(--danger);font-size:10.5px">6.55</text><text x="120.0" y="159.0" text-anchor="end" style="fill:var(--fg);font-size:11px">② std::function</text><rect x="128" y="141" width="119.08" height="14" rx="2" style="fill:var(--warn)"/><text x="252.1" y="152.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">4.58</text><rect x="128" y="158" width="16.38" height="14" rx="2" style="fill:var(--accent)"/><text x="149.4" y="169.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.63</text><text x="120.0" y="203.0" text-anchor="end" style="fill:var(--fg);font-size:11px">④ std::variant</text><rect x="128" y="185" width="98.02" height="14" rx="2" style="fill:var(--warn)"/><text x="231.0" y="196.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">3.77</text><rect x="128" y="202" width="18.2" height="14" rx="2" style="fill:var(--accent)"/><text x="151.2" y="213.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.70</text><text x="120.0" y="247.0" text-anchor="end" style="fill:var(--fg);font-size:11px">③ 模板（分桶）</text><rect x="128" y="229" width="12.48" height="14" rx="2" style="fill:var(--warn)"/><text x="145.5" y="240.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">0.48</text><rect x="128" y="246" width="12.48" height="14" rx="2" style="fill:var(--accent)"/><text x="145.5" y="257.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.48</text><text x="380.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">GCC 13（libstdc++）</text><text x="490.0" y="54.0" text-anchor="end" style="fill:var(--fg);font-size:11px">enum + switch</text><rect x="498" y="36" width="73.84" height="14" rx="2" style="fill:var(--warn)"/><text x="576.8" y="47.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">2.84</text><rect x="498" y="53" width="14.819999999999999" height="14" rx="2" style="fill:var(--accent)"/><text x="517.8" y="64.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.57</text><text x="490.0" y="98.0" text-anchor="end" style="fill:var(--fg);font-size:11px">① 虚函数</text><rect x="498" y="80" width="105.55999999999999" height="14" rx="2" style="fill:var(--warn)"/><text x="608.6" y="91.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">4.06</text><rect x="498" y="97" width="16.900000000000002" height="14" rx="2" style="fill:var(--accent)"/><text x="519.9" y="108.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.65</text><rect x="498" y="114" width="193.18" height="14" rx="2" style="fill:var(--danger)"/><text x="696.2" y="125.0" text-anchor="start" style="fill:var(--danger);font-size:10.5px">7.43</text><text x="490.0" y="159.0" text-anchor="end" style="fill:var(--fg);font-size:11px">② std::function</text><rect x="498" y="141" width="83.2" height="14" rx="2" style="fill:var(--warn)"/><text x="586.2" y="152.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">3.20</text><rect x="498" y="158" width="16.64" height="14" rx="2" style="fill:var(--accent)"/><text x="519.6" y="169.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.64</text><text x="490.0" y="203.0" text-anchor="end" style="fill:var(--fg);font-size:11px">④ std::variant</text><rect x="498" y="185" width="73.32" height="14" rx="2" style="fill:var(--warn)"/><text x="576.3" y="196.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">2.82</text><rect x="498" y="202" width="15.079999999999998" height="14" rx="2" style="fill:var(--accent)"/><text x="518.1" y="213.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.58</text><text x="490.0" y="247.0" text-anchor="end" style="fill:var(--fg);font-size:11px">③ 模板（分桶）</text><rect x="498" y="229" width="18.72" height="14" rx="2" style="fill:var(--warn)"/><text x="521.7" y="240.0" text-anchor="start" style="fill:var(--warn);font-size:10.5px">0.72</text><rect x="498" y="246" width="18.72" height="14" rx="2" style="fill:var(--accent)"/><text x="521.7" y="257.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">0.72</text><rect x="14" y="290" width="12" height="12" rx="2" style="fill:var(--warn)"/><text x="32.0" y="300.0" text-anchor="start" style="fill:var(--fg);font-size:11px">类型随机</text><rect x="110" y="290" width="12" height="12" rx="2" style="fill:var(--accent)"/><text x="128.0" y="300.0" text-anchor="start" style="fill:var(--fg);font-size:11px">按类型排序</text><rect x="220" y="290" width="12" height="12" rx="2" style="fill:var(--danger)"/><text x="238.0" y="300.0" text-anchor="start" style="fill:var(--fg);font-size:11px">类型随机，并且对象在内存中打乱（只有虚函数版受影响）</text><text x="14.0" y="324.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">每个元素的耗时（纳秒，越短越好）：100 万个形状，-O2，31 次取中位数；Apple M6，GCC 运行在 Ubuntu 24.04 容器里</text></g></svg>
</figure>

| 每个元素的耗时（ns） | Clang 21：随机 | Clang 21：排序 | GCC 13：随机 | GCC 13：排序 |
| --- | --- | --- | --- | --- |
| `enum` + `switch` | 2.91 | 0.58 | 2.84 | 0.57 |
| ① 虚函数 | 4.19 | 0.76 | 4.06 | 0.65 |
| ① 虚函数，对象打乱 | 6.55 | - | 7.43 | - |
| ② `std::function` | 4.58 | 0.63 | 3.20 | 0.64 |
| ④ `std::variant` | 3.77 | 0.70 | 2.82 | 0.58 |
| ③ 模板（按类型分桶） | 0.48 | 0.48 | 0.72 | 0.72 |

（Apple M6，`-O2`；GCC 运行在同一台机器上的 Ubuntu 24.04 容器里。重复运行的结果相差不到 0.1 ns。）

几个结论：

1. **数据怎么排列，比用哪种写法重要得多**。类型随机时，除了分桶的模板版，所有写法都在 2.8–4.6 ns；按类型排好序以后，全部降到 0.6–0.8 ns，快了 5–6 倍。写法之间的差距最多 1.6 倍。
2. **原因是分支预测**。不管是 `switch`、虚函数调用还是 `std::visit`，本质上都是「根据类型跳到不同的代码」。CPU 为了不停下来等，会先猜一个目标接着往下执行，猜错了就要把做了一半的工作丢掉。类型随机时几乎猜不准，排好序以后只有类型切换的地方会猜错。下面的演示用一个简化的预测模型说明这一点。
3. **虚函数多了一次内存访问**。调用虚函数要先从对象里读出虚表指针，而对象在堆上；对象在内存里打乱以后，每次都可能是一次缓存未命中，耗时又多了 2.4–3.4 ns。`variant` 和 `switch` 的数据连续存放，没有这个问题。
4. **最快的是根本不分支**。按类型分桶以后，每个循环里只有一种类型，`area()` 被内联进循环，没有任何跳转。这就是「面向数据设计」的思路：与其让每个元素自己决定做什么，不如把同类的元素放在一起批量处理。代价是元素原来的顺序没了，而且类型必须在编译期全部已知。

<div class="dsviz" data-viz="dp-poly"></div>

**这些差距什么时候才重要？** 100 万个元素，4 ns 和 0.5 ns 的差别是每遍 4 ms 和 0.5 ms。对于一个每秒执行一次的报表任务，这完全可以忽略，应该按设计上的需要选择写法；对于每帧都要处理几十万个对象的游戏引擎、或者高频交易的热点循环，它就很关键了。先按设计选，再用性能分析工具找到真正的热点，只在热点上考虑数据布局。

### 4.2 四种写法怎么选

还记得《为什么需要设计模式》里的「格式 × 操作」表格吗？四种写法在「加新类型」和「加新操作」两个方向上各有侧重：

| 写法 | 加一种新类型 | 加一种新操作 | 运行时替换 | 对象怎么存 | 适合的场景 |
| --- | --- | --- | --- | --- | --- |
| ① 虚函数 | 新写一个子类，已有代码不用改 | 要改接口和所有子类 | 可以 | 指针 + 堆上的对象 | 类型由别人扩展（插件、框架）、对象有复杂的生命周期 |
| ② `std::function` | 任何签名匹配的可调用对象 | 只能有一个操作 | 可以 | 按值，小对象不分配堆内存 | 回调、只有一个函数的策略 |
| ③ 模板 | 任何满足要求的类型 | 新写一个函数模板 | 不行，编译期确定 | 按值，不同类型分开存 | 算法的参数（`std::sort` 的比较函数）、性能关键的代码 |
| ④ `std::variant` | 要改 `variant` 的定义和所有访问它的地方（编译器会检查） | 新写一个访问函数，已有代码不用改 | 可以（换成另一种备选类型） | 按值，连续存放 | 类型集合固定：语法树节点、网络消息、状态机的状态 |

一个简单的判断顺序：

1. 只有一个操作、只是想传一段「行为」进去？用 `std::function`（或者直接用模板参数接收 lambda）。
2. 类型的集合是固定的、由你自己掌控？用 `std::variant`，以后加新操作很方便，加新类型时编译器会提醒你哪里没处理（练习 3）。
3. 类型要由别人扩展，或者要在运行时加载？用虚函数。
4. 已经确认是性能热点，而且编译期就知道所有类型？考虑模板和按类型分桶。

C++20 的 **concept** 可以给模板参数写上明确的要求（比如「必须有一个返回 `double` 的 `area()`」），让模板版的错误信息像虚函数接口一样清楚；**CRTP** 则是用模板实现「编译期的继承」，这两个会在「C++ 惯用法」那一章展开。

## 5. 什么时候仍然该用继承

组合优于继承，不等于不用继承。下面这些情况，继承就是正确的选择：

- **实现接口**：子类实现一个只有纯虚函数的接口（前几篇的 `Formatter`、`Rule`、`OrderStore`）。这是接口继承，不存在脆弱基类的问题，因为基类没有任何实现可以被依赖。
- **框架明确设计好的扩展点**：基类的作者专门留出了供子类重写的虚函数，并在文档里写清楚了调用时机（比如 Qt 的 `paintEvent`）。这就是后面要讲的**模板方法模式**，它和脆弱基类的区别在于：哪些函数可以重写、什么时候被调用，是基类公开承诺的，而不是碰巧的实现细节。
- **真正的「是一种」关系，并且满足里氏替换**：子类在任何场合都能替换父类。

一句话总结：**继承接口，组合实现**。

## 6. 第 1 章小结

到这里，「基础与设计原则」这一章就结束了。回顾一下五篇文章的主线：

1. 《为什么需要设计模式》：代码的组织方式决定了哪种修改容易，设计就是让常见的变化只需要新增代码；
2. 《UML 类图》：读懂类和类之间的关系，后面每个模式都用它描述结构；
3. 《SOLID 原则（上）》：按「谁会要求修改」切分代码，在会变化的方向上留出扩展点；
4. 《SOLID 原则（下）》：子类能替换父类，接口按使用者拆小，依赖指向高层定义的抽象；
5. 本文：用组合代替实现继承；C++ 里的多态有四种写法，按扩展方向和性能需要选择。

下一章开始逐个讲 GoF 的 23 种模式，先从创建型模式开始。你会发现，每个模式都是这几条原则在某个具体问题上的应用。

## 7. 小结

- 继承有两种用途：接口继承（多态）和实现继承（复用代码）。「组合优于继承」反对的是后者
- **脆弱基类**：子类依赖了基类没有承诺的实现细节。实测：`CountingSet` 插入 3 个元素数出 6 次，因为基类的 `insertAll` 内部调用了虚函数 `insert`；改成组合（持有一个 `IntSet` 并委托给它）后结果正确
- **子类爆炸**：用继承表达功能组合，类的数量按 2ⁿ 增长；改成组合后变成 n 个可以任意套用的装饰器
- C++ 多态的四种写法：虚函数、`std::function`、模板、`std::variant`
- 实测 100 万个形状：类型随机时 2.8–4.6 ns/个，按类型排序后 0.6–0.8 ns/个，按类型分桶的模板版 0.48–0.72 ns/个；数据排列（分支预测、缓存）的影响远大于写法本身
- 选择：只有一个操作用 `std::function`，类型集合固定用 `variant`，类型由别人扩展用虚函数，确认的性能热点再考虑模板和分桶
- 继承接口，组合实现

## 8. 练习

1. `IntSet` 的作者把 `insertAll` 优化成了一次性插入：

   <!-- run: off -->
   ```cpp
   virtual void insertAll(const std::vector<int>& xs) { s_.insert(xs.begin(), xs.end()); }
   ```

   `fragile_base.cpp` 里的 `CountingSet` 不做任何修改，`insertAll({1, 2, 3})` 之后 `attempts()` 是多少？`fragile_fixed.cpp` 里组合版的 `CountingSet` 呢？

2. 下面四个场景，分别用哪种多态写法最合适？
   ① 图片编辑器支持第三方滤镜，滤镜以动态库的形式在运行时加载；
   ② 一个表达式计算器的语法树，节点只有数字、加、减、乘、除五种，但要不断加新的功能：求值、打印、化简、求导；
   ③ 一个排序函数，允许调用者自定义比较规则，并且要和 `std::sort` 一样快；
   ④ 按钮被点击时执行一段由使用者传进来的代码。

3. 用 `std::variant<Circle, Rect, Tri>` 和下面的 `overloaded` 写法访问形状时，漏写了 `Tri` 的分支，编译时会发生什么？如果用的是虚函数，新加了一个 `Tri` 子类却忘了重写 `area()`，又会怎样？

   <!-- run: off -->
   ```cpp
   template <typename... Fs> struct overloaded : Fs... { using Fs::operator()...; };
   template <typename... Fs> overloaded(Fs...) -> overloaded<Fs...>;

   double area(const AnyShape& s) {
       return std::visit(overloaded{
           [](const Circle& c) { return 3.14159 * c.r * c.r; },
           [](const Rect& r) { return r.w * r.h; },
           // 忘了写 Tri
       }, s);
   }
   ```

4. 有人用继承实现了带各种附加功能的日志类：`Logger`、`TimestampLogger`、`ColorLogger`、`TimestampColorLogger`、`FileLogger`、`TimestampFileLogger`……现在又要加「按级别过滤」的功能。用组合重新设计它，画出（或写出）类的结构。

<details>
<summary>点击查看答案</summary>

1. 继承版：`attempts()` 是 **3**。`CountingSet::insertAll` 先加了 3，再调用基类的 `insertAll`，而基类现在不再调用 `insert`，所以不会再加。`CountingSet` 的代码一行没改，结果却从 6 变成了 3：在这个版本里它碰巧对了，可如果之前有人为了「修 bug」删掉了 `attempts_ += xs.size()`，现在就会变成 0。子类的正确性取决于基类的实现细节，这就是脆弱基类。
   组合版：仍然是 **3**（插入 3 个元素）。组合版的计数完全在 `CountingSet` 自己的函数里完成，`IntSet` 内部怎么实现都不影响它。

2. ① 虚函数：滤镜由第三方在运行时提供，类型的集合是开放的，只能用接口加动态库（《SOLID 原则（上）》里的插件）。
   ② `std::variant`：节点类型固定只有五种，要不断加的是「操作」，用 `variant` 加新操作只需新写一个访问函数。用虚函数的话，每加一个功能都要改基类和五个子类。（经典的面向对象做法是**访问者模式**，行为型模式那一章会讲，`variant` 是它在现代 C++ 里的简化版。）
   ③ 模板：比较规则作为模板参数传进去，编译期确定，比较函数可以被内联。`std::sort` 正是这样做的。
   ④ `std::function<void()>`：只有一个操作（被点击时执行），使用者想传 lambda、普通函数还是成员函数都可以，不需要继承任何基类。

3. **`variant` 版编译失败**（完整代码在 `variant_missing_ce.cpp`）。Clang 21（libc++）的报错很直白：

   ```text
   error: static assertion failed due to requirement 'is_invocable_v<overloaded<...>, const Tri &>': `std::visit` requires the visitor to be exhaustive.
   ```

   GCC 13（libstdc++）的报错不太好读，但也指出了是 `const Tri&` 处理不了：

   ```text
   error: no type named 'type' in 'struct std::invoke_result<overloaded<...>, const Tri&>'
   ```

   也就是说，`variant` 加了新类型以后，所有漏掉它的 `visit` 都会变成编译错误，编译器帮你找到每一处要改的地方。这和《为什么需要设计模式》练习 2 里「`enum class` + 不写 `default` 的 `switch`」是同一个思路。
   **虚函数版**：如果 `Shape::area()` 是纯虚函数，忘了重写的 `Tri` 仍然是抽象类，创建它的那一行编译失败；如果 `Shape::area()` 有一个默认实现（比如返回 0），编译器什么也不会说，三角形的面积默默地变成了 0。所以接口里的函数应该是纯虚函数，不要提供「方便」的默认实现。

4. 把每种附加功能做成一个装饰器，和最终的输出目标实现同一个接口，运行时按需套起来：

   <!-- run: off -->
   ```cpp
   class LogSink {                                   // 接口：写一条日志
   public:
       virtual ~LogSink() = default;
       virtual void write(int level, const std::string& msg) = 0;
   };

   class ConsoleSink : public LogSink { /* 输出到终端 */ };
   class FileSink : public LogSink { /* 输出到文件 */ };

   class Decorator : public LogSink {                // 装饰器：持有下一层
   public:
       explicit Decorator(std::unique_ptr<LogSink> next) : next_(std::move(next)) {}
   protected:
       std::unique_ptr<LogSink> next_;
   };

   class Timestamp : public Decorator { /* 在 msg 前加上时间，再交给 next_ */ };
   class Color : public Decorator { /* 按级别加上颜色 */ };
   class LevelFilter : public Decorator { /* 级别不够就直接返回，不交给 next_ */ };

   // 运行时组装：过滤 → 时间戳 → 颜色 → 终端
   auto log = std::make_unique<LevelFilter>(
                  std::make_unique<Timestamp>(
                      std::make_unique<Color>(std::make_unique<ConsoleSink>())));
   ```

   加「按级别过滤」只需新写一个 `LevelFilter` 类。原来的方案要再加一倍的子类：每个已有的类都要多一个「带过滤」的版本。

</details>
