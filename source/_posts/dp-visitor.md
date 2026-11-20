---
title: 访问者模式
date: 2026-11-12 10:00:00
updated: 2026-11-12 10:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 访问者模式
  - 双分派
  - std::variant
  - std::visit
order: 8
description: 一条字幕是一棵元素树（文字、粗体、斜体、颜色、换行），要统计字数、导出纯文本、导出 ASS，以后还要导出 SRT、WebVTT……把每种操作都写成元素类的虚函数，加一种导出格式要改 7 处；写成类外的函数、用 dynamic_cast 判断类型，后来加了「下划线」没人改，编译 0 个警告，导出的 ASS 里那句字幕直接没了。改成访问者模式（双分派：accept 按元素类型分派一次，visit 按操作类型再分派一次），加一种操作只要新写一个类、改 1 处；代价是加一种元素要改 6 处，好在漏改时编译器会报错。再用 std::variant + std::visit 重写：值类型、不用 accept、能直接返回结果、能同时分派两个对象。实测遍历一个元素：随机排列时单分派虚函数、访问者、std::visit 都在 4.28～5.49 纳秒（Clang），dynamic_cast 链 36.41 纳秒；按类型排好序后前三种都不到 1 纳秒。最后看 FFmpeg 的字幕编码器怎么用一组回调函数当访问者，比较访问者和组合、迭代器、策略、解释器的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-visitor]
---

> **本文目标**：理解访问者模式（元素类稳定、操作常增时，用 `accept()` + `visit()` 实现双分派）；量出加操作与加元素在几种写法下的改动成本及编译器检查能力；掌握现代 C++ `std::variant` + `std::visit`（含 `overloaded` 与双对象分派）和性能实测结论；分清访问者与组合、迭代器、策略、解释器。
>
> **前置知识**：《组合模式》《迭代器模式》《状态模式》（用过 `std::variant`）；音视频专栏里 SRT / ASS 字幕格式。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整示例与性能压测代码见 `projects/dp-lab/25-visitor/`。

## 1. 问题：一棵树，很多种操作

### 1.1 字幕元素树：操作写成虚函数，加操作难

一条字幕包含文字和样式（粗体、斜体、颜色、换行），不同格式写法各异：

| 格式 | 粗体 | 颜色 | 换行 |
| --- | --- | --- | --- |
| SRT | `<b>big</b>` | `<font color="#FF0000">…</font>` | 真实换行 |
| ASS | `{\b1}big{\b0}` | `{\c&H0000FF&}…{\c}`（BGR 顺序） | `\N` |
| 纯文本 | 去掉 | 去掉 | 真实换行 |

播放器通常先把字幕解析成一棵由 5 种元素（`Text`、`Bold`、`Italic`、`Color`、`Br`）组成的树，再做字数统计、导出纯文本、导出 ASS 等操作。最直接的写法是把每种操作都写成基类 `Node` 的虚函数：

```cpp
// 《访问者模式》：问题版——每种操作都是元素类的虚函数
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Node {
  virtual ~Node() = default;
  virtual int chars() const = 0;
  virtual std::string plain() const = 0;
  virtual std::string ass() const = 0;
};
using Children = std::vector<std::unique_ptr<Node>>;

struct Text : Node {
  std::string s;
  explicit Text(std::string t) : s(std::move(t)) {}
  int chars() const override { int n = 0; for (unsigned char c : s) n += (c & 0xC0) != 0x80; return n; }
  std::string plain() const override { return s; }
  std::string ass() const override { return s; }
};
struct Span : Node {
  Children kids;
  int chars() const override { int n = 0; for (auto& k : kids) n += k->chars(); return n; }
  std::string plain() const override { std::string s; for (auto& k : kids) s += k->plain(); return s; }
  std::string kidsAss() const { std::string s; for (auto& k : kids) s += k->ass(); return s; }
};
struct Bold : Span   { std::string ass() const override { return "{\\b1}" + kidsAss() + "{\\b0}"; } };
struct Italic : Span { std::string ass() const override { return "{\\i1}" + kidsAss() + "{\\i0}"; } };
struct Color : Span {
  std::uint32_t rgb;
  explicit Color(std::uint32_t c) : rgb(c) {}
  std::string ass() const override {
    char tag[32];
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", rgb & 0xFF, (rgb >> 8) & 0xFF, rgb >> 16);
    return tag + kidsAss() + "{\\c}";
  }
};
struct Br : Node {
  int chars() const override { return 0; }
  std::string plain() const override { return "\n"; }
  std::string ass() const override { return "\\N"; }
};

template <class S, class... K>
std::unique_ptr<Node> makeSpan(S s, K... ks) {
  auto p = std::make_unique<S>(std::move(s)); (p->kids.push_back(std::move(ks)), ...); return p;
}

int main() {
  auto txt = [](std::string s) { return std::make_unique<Text>(std::move(s)); };
  Children cue;   // Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见
  cue.push_back(txt("Hello "));
  cue.push_back(makeSpan(Bold{}, txt("big")));
  cue.push_back(txt(" "));
  cue.push_back(makeSpan(Italic{}, makeSpan(Color{0xFF0000}, txt("world"))));
  cue.push_back(std::make_unique<Br>());
  cue.push_back(txt("再见"));

  int n = 0; std::string plain, ass;
  for (auto& x : cue) { n += x->chars(); plain += x->plain(); ass += x->ass(); }
  std::printf("字数：%d\n纯文本：%s\nASS：%s\n", n, plain.c_str(), ass.c_str());
}
```

```text
字数：17
纯文本：Hello big world
再见
ASS：Hello {\b1}big{\b0} {\i1}{\c&H0000FF&}world{\c}{\i0}\N再见
```

现在要支持导出 SRT（完整修改见 `projects/dp-lab/25-visitor/methods_srt.cpp`）：要在 `Node` 里加纯虚函数 `srt()`，再到 `Text`、`Span`、`Bold`、`Italic`、`Color`、`Br` 里各补一个实现——`measure.sh` 统计一共 **7 处改动**。元素种类本来很稳定，却因为每加一种导出格式或分析工具而被迫反复修改，越来越胖。

### 1.2 写在外面：`dynamic_cast` 漏改无警告

把操作搬到类外、用 `dynamic_cast` 逐个判断类型，加操作确实不用动元素类了。但后来新增一种元素「下划线」`Underline`，如果忘了改 `ass()`：

```cpp
// 《访问者模式》：dynamic_cast 链——加了 Underline 后漏改 ass()，编译 0 警告但字幕直接丢字
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Node { virtual ~Node() = default; };
using Children = std::vector<std::unique_ptr<Node>>;
struct Text : Node { std::string s; explicit Text(std::string t) : s(std::move(t)) {} };
struct Span : Node { Children kids; };
struct Bold : Span {};
struct Br : Node {};
struct Underline : Span {};                      // 后来新增的元素

int chars(const Node& n) {
  if (auto t = dynamic_cast<const Text*>(&n)) {
    int k = 0; for (unsigned char c : t->s) k += (c & 0xC0) != 0x80; return k;
  }
  if (auto s = dynamic_cast<const Span*>(&n)) { int k = 0; for (auto& c : s->kids) k += chars(*c); return k; }
  return 0;
}

std::string ass(const Node& n) {
  auto kids = [](const Span& s) { std::string r; for (auto& c : s.kids) r += ass(*c); return r; };
  if (auto t = dynamic_cast<const Text*>(&n)) return t->s;
  if (auto b = dynamic_cast<const Bold*>(&n)) return "{\\b1}" + kids(*b) + "{\\b0}";
  if (dynamic_cast<const Br*>(&n)) return "\\N";
  return "";                                     // 不认识 Underline：静默丢弃！
}

int main() {
  Children cue;
  cue.push_back(std::make_unique<Text>("Hello "));
  auto u = std::make_unique<Underline>();
  u->kids.push_back(std::make_unique<Text>("再见"));
  cue.push_back(std::move(u));

  int n = 0; std::string out;
  for (auto& x : cue) { n += chars(*x); out += ass(*x); }
  std::printf("字数：%d\nASS：%s\n", n, out.c_str());
}
```

```text
字数：8
ASS：Hello 
```

`Underline` 碰巧是 `Span` 子类所以字数算对了，但导出的 ASS 里「再见」**凭空消失**，用 `-Wall -Wextra` 编译依然 **0 错误、0 警告**；而且 `dynamic_cast` 链每次从头比对 RTTI，第 4 节实测比虚函数慢 4～7 倍。

### 1.3 重载不是分派

既然不想写 `dynamic_cast`，那给操作类写一组重载 `visit(const Text&)`、`visit(const Br&)` 行不行？试试看：

```cpp
// 《访问者模式》：重载按静态类型在编译期选定，手里只有 Node& 时永远落入兜底重载
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Node { virtual ~Node() = default; };
struct Text : Node { std::string s; explicit Text(std::string t) : s(std::move(t)) {} };
struct Br : Node {};

struct AssExporter {
  std::string out;
  void visit(const Text& t) { out += t.s; }
  void visit(const Br&) { out += "\\N"; }
  void visit(const Node&) { out += "[?]"; }      // 兜底
};

int main() {
  std::vector<std::unique_ptr<Node>> cue;
  cue.push_back(std::make_unique<Text>("Hello"));
  cue.push_back(std::make_unique<Br>());
  cue.push_back(std::make_unique<Text>("再见"));

  AssExporter a, b;
  for (auto& n : cue) a.visit(*n);               // *n 的静态类型是 Node&
  Text hello("Hello"); Br br;
  b.visit(hello); b.visit(br);                   // 静态类型明确时才选得对
  std::printf("按重载：%s\n静态类型已知：%s\n", a.out.c_str(), b.out.c_str());
}
```

```text
按重载：[?][?][?]
静态类型已知：Hello\N
```

**重载是在编译期按参数静态类型选的**；C++ 运行时只支持按**一个**对象（`this`）动态类型选函数的单分派虚函数。我们需要同时按「元素动态类型 × 操作动态类型」选择函数——这叫**双分派（double dispatch）**。

## 2. 访问者模式

### 2.1 意图与双分派

> **GoF**：表示一个作用于某对象结构中的各元素的操作。它使你可以在不改变各元素的类的前提下定义作用于这些元素的新操作。

大白话：**元素类只留一个 `accept()` 入口，每种操作写成一个访问者类，里面给每种元素各写一个 `visit()`。**

双分派的秘密就在每个元素类那一行一模一样的 `accept()`：

<!-- run: off -->
```cpp
void Bold::accept(Visitor& v) const { v.visit(*this); }
```

1. 调用 `n->accept(v)` 时，`accept` 是虚函数，按**元素**动态类型选中 `Bold::accept`（第一次分派）；
2. 进入 `Bold::accept` 后，`*this` 的静态类型已是 `const Bold&`，编译器精确选中 `Visitor::visit(const Bold&)`；`visit` 又是虚函数，按**访问者**动态类型选中 `AssExporter::visit(const Bold&)`（第二次分派）。

因此每个具体元素类都**必须自己写** `accept`，不能提进基类（否则 `*this` 的静态类型又退回 `Node`）。

### 2.2 用访问者重构字幕树

```cpp
// 《访问者模式》：经典双分派——元素只留 accept()，每种操作写成一个 Visitor 子类
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Text; struct Bold; struct Italic; struct Color; struct Br;
struct Visitor {                    // 每种元素一个纯虚 visit()
  virtual ~Visitor() = default;
  virtual void visit(const Text&) = 0;   virtual void visit(const Bold&) = 0;
  virtual void visit(const Italic&) = 0; virtual void visit(const Color&) = 0;
  virtual void visit(const Br&) = 0;
};
struct Node { virtual ~Node() = default; virtual void accept(Visitor& v) const = 0; };
using Children = std::vector<std::unique_ptr<Node>>;

struct Text : Node {
  std::string s; explicit Text(std::string t) : s(std::move(t)) {}
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Span : Node { Children kids; void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); } };
struct Bold : Span   { void accept(Visitor& v) const override { v.visit(*this); } };
struct Italic : Span { void accept(Visitor& v) const override { v.visit(*this); } };
struct Color : Span {
  std::uint32_t rgb; explicit Color(std::uint32_t c) : rgb(c) {}
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Br : Node     { void accept(Visitor& v) const override { v.visit(*this); } };

struct CharCounter : Visitor {
  int n = 0;
  void visit(const Text& t) override { for (unsigned char c : t.s) n += (c & 0xC0) != 0x80; }
  void visit(const Bold& b) override { b.visitKids(*this); }
  void visit(const Italic& i) override { i.visitKids(*this); }
  void visit(const Color& c) override { c.visitKids(*this); }
  void visit(const Br&) override {}
};

struct AssExporter : Visitor {
  std::string out;
  void visit(const Text& t) override { out += t.s; }
  void visit(const Bold& b) override { out += "{\\b1}"; b.visitKids(*this); out += "{\\b0}"; }
  void visit(const Italic& i) override { out += "{\\i1}"; i.visitKids(*this); out += "{\\i0}"; }
  void visit(const Color& c) override {
    char tag[32];
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", c.rgb & 0xFF, (c.rgb >> 8) & 0xFF, c.rgb >> 16);
    out += tag; c.visitKids(*this); out += "{\\c}";
  }
  void visit(const Br&) override { out += "\\N"; }
};

template <class S, class... K>
std::unique_ptr<Node> makeSpan(S s, K... ks) {
  auto p = std::make_unique<S>(std::move(s)); (p->kids.push_back(std::move(ks)), ...); return p;
}

int main() {
  auto txt = [](std::string s) { return std::make_unique<Text>(std::move(s)); };
  Children cue;
  cue.push_back(txt("Hello "));
  cue.push_back(makeSpan(Bold{}, txt("big")));
  cue.push_back(txt(" "));
  cue.push_back(makeSpan(Italic{}, makeSpan(Color{0xFF0000}, txt("world"))));
  cue.push_back(std::make_unique<Br>());
  cue.push_back(txt("再见"));

  CharCounter cc; AssExporter ae;
  for (auto& n : cue) { n->accept(cc); n->accept(ae); }
  std::printf("字数：%d\nASS：%s\n", cc.n, ae.out.c_str());
}
```

输出与第 1.1 节一致。现在新增导出纯文本或 SRT，只需新写一个 `Visitor` 子类（见 `projects/dp-lab/25-visitor/visitor_srt.cpp`），元素类一行不动。

### 2.3 类图、时序图与演示

```uml
label: 访问者模式的类图：Node 是接口，只有一个 accept(Visitor&)；Text、Bold、Br 等元素类实现它。Visitor 是接口，对每种元素各有一个 visit；CharCounter、AssExporter、SrtExporter 是具体访问者。元素的 accept 调用访问者的 visit，访问者通过 visitKids 再访问子节点
classes:
  - { id: Node, stereo: interface, x: 20, y: 20, w: 200, ops: ['*+ accept(v : Visitor&)'] }
  - { id: Text, x: 10, y: 170, w: 70, attrs: ['+ s'] }
  - { id: Bold, x: 100, y: 170, w: 80, attrs: ['+ kids'] }
  - { id: Br, x: 200, y: 170, w: 60, attrs: [] }
  - { id: Visitor, stereo: interface, x: 420, y: 20, w: 220, ops: ['*+ visit(const Text&)', '*+ visit(const Bold&)', '*+ visit(const Br&)', '*+ …每种元素一个'] }
  - { id: CharCounter, x: 330, y: 210, w: 110, attrs: ['+ n : int'] }
  - { id: AssExporter, x: 455, y: 210, w: 110, attrs: ['+ out'] }
  - { id: SrtExporter, x: 580, y: 210, w: 110, attrs: ['+ out'], hl: new }
rels:
  - { from: Text, to: Node, type: realize, mid: 130 }
  - { from: Bold, to: Node, type: realize, mid: 130 }
  - { from: Br, to: Node, type: realize, mid: 130 }
  - { from: CharCounter, to: Visitor, type: realize, mid: 175 }
  - { from: AssExporter, to: Visitor, type: realize, mid: 175 }
  - { from: SrtExporter, to: Visitor, type: realize, mid: 175 }
  - { from: Node, to: Visitor, type: depend, label: 'v.visit(*this)', points: [[220, 62], [420, 62]] }
notes:
  - { x: 20, y: 270, w: 270, text: ['每个元素类的 accept 都是同一行：', 'v.visit(*this)；Italic、Color 同理'] }
  - { x: 450, y: 290, w: 240, text: ['加一种操作：新写一个访问者（绿色）', '元素类一行不改'] }
```

```uml
label: 时序图：遍历代码调用 Bold 的 accept，传入 AssExporter；Bold 的 accept 调用 AssExporter 的 visit(const Bold&)；AssExporter 先输出 {\b1}，再调用 Bold 的 visitKids；visitKids 调用子节点 Text 的 accept；Text 的 accept 调用 AssExporter 的 visit(const Text&) 输出 big；返回后 AssExporter 输出 {\b0}
participants:
  - { id: loop, name: '遍历', x: 20, w: 70 }
  - { id: b, name: 'Bold', x: 160, w: 80 }
  - { id: v, name: 'AssExporter', x: 320, w: 120 }
  - { id: t, name: 'Text("big")', x: 520, w: 120 }
messages:
  - { from: loop, to: b, text: 'accept(v)', n: 1, note: '第一次分派：按元素类型' }
  - { from: b, to: v, text: 'visit(*this)', n: 2, note: '第二次分派：按访问者类型' }
  - { from: v, to: v, text: 'out += "{\b1}"', n: 3 }
  - { from: v, to: b, text: 'visitKids(*this)', n: 4 }
  - { from: b, to: t, text: 'accept(v)', n: 5 }
  - { from: t, to: v, text: 'visit(*this)', n: 6, note: 'out += "big"' }
  - { from: v, to: t, text: '', type: reply }
  - { from: t, to: b, text: '', type: reply }
  - { from: b, to: v, text: '', type: reply }
  - { from: v, to: v, text: 'out += "{\b0}"', n: 7 }
  - { from: v, to: b, text: '', type: reply }
  - { from: b, to: loop, text: '', type: reply }
```

下面的演示一步步展示两次分派如何命中目标函数，以及第 1.3 节「只用重载」的失败：

<div class="dsviz" data-viz="dp-visitor"></div>

### 2.4 加操作容易，加元素难（表达式问题）

访问者的代价是**加元素变难**：新增 `Underline` 时，要改 `Visitor` 接口并给每个具体访问者各补一个 `visit(const Underline&)`（见 `visitor_underline.cpp`）。`measure.sh` 统计三种写法加操作与加元素的改动处数：

<figure class="diagram">
<svg viewBox="0 0 640 156" role="img" aria-label="三种写法加一种操作、加一种元素各要改几处：加导出 SRT，虚函数版改 7 处，访问者和 variant 都只改 1 处；加下划线元素，虚函数版改 1 处，访问者改 6 处，variant 改 5 处"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">加一种操作（导出 SRT）</text><rect x="138.0" y="30.0" width="140.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="284.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">7 处</text><rect x="138.0" y="60.0" width="20.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="164.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1 处</text><rect x="138.0" y="90.0" width="20.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="164.0" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1 处</text><text x="388.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">加一种元素（下划线）</text><rect x="388.0" y="30.0" width="20.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="414.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">1 处</text><rect x="388.0" y="60.0" width="120.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="514.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6 处</text><rect x="388.0" y="90.0" width="100.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="494.0" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">5 处</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">操作写成虚函数</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">访问者</text><text x="130.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">variant + visit</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：diff 只比较类和操作的部分（不含 main 里的测试），相邻的改动算一处；Clang 和 GCC 结果相同</text></g></svg>
</figure>

把「元素 × 操作」画成矩阵，这就是著名的**表达式问题（expression problem）**——虚函数版一个类管**一行**，访问者版一个类管**一列**：

<figure class="diagram">
<svg viewBox="0 0 720 270" role="img" aria-label="元素乘操作的表格：左边虚函数写法里一个元素类管一行，加一列操作要改每个类，加一行元素只要新写一个类；右边访问者写法里一个访问者类管一列，加一列操作只要新写一个访问者，加一行元素要改每个访问者"><g style="font-family:var(--mono)"><text x="10.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">操作写成虚函数：一个类管一行</text><text x="97.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">字数</text><text x="159.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">纯文本</text><text x="221.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">ASS</text><text x="283.0" y="40.0" text-anchor="middle" style="fill:var(--link);font-size:11px;font-weight:700">SRT</text><text x="58.0" y="69.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Text</text><rect x="68" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="52" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="97.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Bold</text><rect x="68" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="80" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="125.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Italic</text><rect x="68" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="108" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="153.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Color</text><rect x="68" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="136" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="181.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Br</text><rect x="68" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="164" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="209.0" text-anchor="end" style="fill:var(--link);font-size:11px;font-weight:700">Underline</text><rect x="68" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="130" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="192" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="254" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="66" y="50" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="78" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="106" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="134" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="162" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="50" width="186" height="140" rx="0" style="fill:none;stroke:none"/><rect x="253" y="50" width="60" height="140" rx="5" style="fill:none;stroke:var(--danger);stroke-width:2"/><rect x="66" y="191" width="186" height="26" rx="5" style="fill:none;stroke:var(--accent);stroke-width:2"/><text x="10.0" y="240.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">加 SRT（新的一列）：要伸进 5 个类各加一格（红框）</text><text x="10.0" y="256.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">加 Underline（新的一行）：新写一个类就行（绿框）</text><text x="370.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">访问者：一个访问者类管一列</text><text x="457.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">字数</text><text x="519.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">纯文本</text><text x="581.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">ASS</text><text x="643.0" y="40.0" text-anchor="middle" style="fill:var(--link);font-size:11px;font-weight:700">SRT</text><text x="418.0" y="69.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Text</text><rect x="428" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="52" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="97.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Bold</text><rect x="428" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="80" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="125.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Italic</text><rect x="428" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="108" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="153.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Color</text><rect x="428" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="136" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="181.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Br</text><rect x="428" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="164" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="209.0" text-anchor="end" style="fill:var(--link);font-size:11px;font-weight:700">Underline</text><rect x="428" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="490" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="552" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="614" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="426" y="50" width="62" height="140" rx="5" style="fill:none;stroke:var(--accent);stroke-width:1.6"/><rect x="488" y="50" width="62" height="140" rx="5" style="fill:none;stroke:var(--accent);stroke-width:1.6"/><rect x="550" y="50" width="62" height="140" rx="5" style="fill:none;stroke:var(--accent);stroke-width:1.6"/><rect x="613" y="50" width="60" height="140" rx="5" style="fill:none;stroke:var(--accent);stroke-width:2"/><rect x="426" y="191" width="186" height="26" rx="5" style="fill:none;stroke:var(--danger);stroke-width:2"/><text x="370.0" y="240.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">加 SRT（新的一列）：新写一个访问者就行（绿框）</text><text x="370.0" y="256.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">加 Underline（新的一行）：要伸进每个访问者各加一格（红框）</text></g></svg>
</figure>

选哪边看变化方向：元素稳定、操作常加（字幕样式、AST 节点）用访问者；元素常加、操作固定（滤镜、解码器）用普通虚函数。而且访问者与 `variant` 加元素漏改时**编译器会报错**，不像 `dynamic_cast` 链静默出错：

<figure class="diagram">
<svg viewBox="0 0 680 156" role="img" aria-label="加了下划线元素却没改任何操作时编译器报的错误数：访问者在 Clang 和 GCC 上都是 3 个；variant 在 Clang 上 12 个、GCC 上 18 个；dynamic_cast 链 0 个错误 0 个警告"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="138.0" y="30.0" width="24.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="168.0" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">3 个 error</text><rect x="138.0" y="60.0" width="96.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="240.0" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">12 个 error</text><rect x="138.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--danger)"/><text x="145.5" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">0 个 error</text><text x="408.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="408.0" y="30.0" width="24.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="438.0" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">3 个 error</text><rect x="408.0" y="60.0" width="144.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="558.0" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">18 个 error</text><rect x="408.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--danger)"/><text x="415.5" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">0 个 error</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">访问者</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">variant + visit</text><text x="130.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">dynamic_cast 链</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：从「加了下划线」的版本里删掉所有处理 Underline 的函数再编译；dynamic_cast 链加 -Wall -Wextra 也没有警告</text></g></svg>
</figure>

## 3. 深入访问者模式

### 3.1 谁来遍历子节点

| 放在哪 | 写法 | 特点 |
| --- | --- | --- |
| 元素的 `accept` 里 | `Bold::accept` 先 `v.visit(*this)`，再遍历子节点 | 访问者简单，但没法在子节点**之后**收尾（ASS 的 `{\b0}` 写不出来），也无法跳过子树 |
| 访问者里（本文写法） | `visit(const Bold&)` 自己调 `visitKids` | 访问者控制前后顺序和剪枝；每个访问者都要记得遍历 |
| 单独的遍历器 | 遍历器走树，进出节点时调 `enter / leave` | 遍历只写一次（如 Clang `RecursiveASTVisitor`） |

### 3.2 封装代价与返回值状态

- **封装代价**：操作搬到元素类外部，元素就得把内部数据（`Text::s`、`Color::rgb`、`Span::kids`）以公有字段、只读 getter 或 `friend` 暴露给访问者。
- **返回值与状态**：经典 `Visitor::visit()` 只能统一返回 `void`，结果必须存在访问者成员（`CharCounter::n`、`AssExporter::out`）里，重用前要清零。想直接返回值，用第 4 节的 `std::visit` 最省事。

### 3.3 真实工程：FFmpeg 字幕回调访问者

FFmpeg 内部文本字幕统一用 ASS 表示，编码成 SRT / WebVTT / TTML 时，由 `libavcodec/ass_split.c` 里的 `ff_ass_split_override_codes()` 拆开标签，再按元素类型回调一组函数指针：

<!-- run: off -->
```c
typedef struct {
    void (*text)(void *priv, const char *text, int len);
    void (*new_line)(void *priv, int forced);
    void (*style)(void *priv, char style, int close);   // b/i/u/s
    void (*color)(void *priv, unsigned int color, unsigned int color_id);
    void (*end)(void *priv);
} ASSCodesCallbacks;
```

这就是 C 语言的访问者：`priv` 存状态，`srtenc.c`、`webvttenc.c`、`ttmlenc.c` 各自填一份回调表；加一种新格式只需新填一组回调。未填的指针为 `NULL` 时自动跳过（相当于默认空实现），新增标签不会破坏旧编码器，但漏处理也不会报错。此外，Clang `StmtVisitor` 和 LLVM `InstVisitor` 在节点里存类型编号用 `switch` 分派，思路与下一节的 `std::variant` 如出一辙。

## 4. 现代 C++ 写法与性能

### 4.1 `std::variant` + `std::visit`

当元素种类固定且在同一处定义时，直接用 `std::variant` 代替继承体系：元素是普通值类型、无虚函数、无 `accept`，`std::visit` 按实际类型分派并**直接返回结果**：

```cpp
// 《访问者模式》：现代 C++——std::variant 值类型 + std::visit 分派，直接返回结果
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

struct Node;
using Kids = std::vector<Node>;     // C++17 允许 vector 元素为不完整类型

struct Text { std::string s; };
struct Bold { Kids kids; };
struct Italic { Kids kids; };
struct Color { std::uint32_t rgb; Kids kids; };
struct Br {};
struct Node { std::variant<Text, Bold, Italic, Color, Br> v; };

struct CharCounter {
  int operator()(const Text& t) const {
    int n = 0; for (unsigned char c : t.s) n += (c & 0xC0) != 0x80; return n;
  }
  int operator()(const Bold& b) const { return sum(b.kids); }
  int operator()(const Italic& i) const { return sum(i.kids); }
  int operator()(const Color& c) const { return sum(c.kids); }
  int operator()(const Br&) const { return 0; }
  int sum(const Kids& ks) const { int n = 0; for (auto& k : ks) n += std::visit(*this, k.v); return n; }
};

struct AssExporter {
  std::string operator()(const Text& t) const { return t.s; }
  std::string operator()(const Bold& b) const { return "{\\b1}" + join(b.kids) + "{\\b0}"; }
  std::string operator()(const Italic& i) const { return "{\\i1}" + join(i.kids) + "{\\i0}"; }
  std::string operator()(const Color& c) const {
    char tag[32];
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", c.rgb & 0xFF, (c.rgb >> 8) & 0xFF, c.rgb >> 16);
    return tag + join(c.kids) + "{\\c}";
  }
  std::string operator()(const Br&) const { return "\\N"; }
  std::string join(const Kids& ks) const { std::string s; for (auto& k : ks) s += std::visit(*this, k.v); return s; }
};

int main() {
  Kids cue = {
      {Text{"Hello "}}, {Bold{{{Text{"big"}}}}}, {Text{" "}},
      {Italic{{{Color{0xFF0000, {{Text{"world"}}}}}}}}, {Br{}}, {Text{"再见"}},
  };
  std::printf("字数：%d\nASS：%s\n", CharCounter{}.sum(cue), AssExporter{}.join(cue).c_str());
  std::printf("sizeof(Node) = %zu，cue[1].v.index() = %zu\n", sizeof(Node), cue[1].v.index());
}
```

```text
字数：17
ASS：Hello {\b1}big{\b0} {\i1}{\c&H0000FF&}world{\c}{\i0}\N再见
sizeof(Node) = 40，cue[1].v.index() = 1
```

优势很明显：元素连续存放在 `vector<Node>` 里（建 104 万节点时基类指针分配 104 万次堆内存，`variant` 只有 `vector` 的 1 次）；不用手写 `accept`；可直接返回值；漏处理某种类型直接编译报错。代价是类型列表封闭（外部模块无法继承扩展），且 `sizeof(Node)` 等于最大元素加标签。

不想写结构体时，可用 C++17 的 `overloaded` 拼 lambda：

<!-- run: off -->
```cpp
template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;   // C++20 可省
```

注意：给 `overloaded` 写兜底的 `[](const auto&)` 会让编译器不再检查是否漏了新增类型，只在「其余类型确实同一种处理」时再用。

### 4.2 双对象分派：合并相邻字幕碎片

经典访问者只能按「一个元素 × 一个访问者」双分派；如果要按**两个元素**的类型组合决定行为（比如把编辑器切碎的相邻同样式节点合并），`std::visit` 可以直接传两个 `variant`，25 种组合只需写 5 个 lambda：

```cpp
// 《访问者模式》：std::visit 同时分派两个 variant，合并相邻的同类型字幕碎片
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct Node;
using Kids = std::vector<Node>;
struct Text { std::string s; };
struct Bold { Kids kids; };
struct Color { std::uint32_t rgb; Kids kids; };
struct Node { std::variant<Text, Bold, Color> v; };

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

Kids concat(Kids a, const Kids& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

std::optional<Node> merge(const Node& a, const Node& b) {
  return std::visit(overloaded{
      [](const Text& x, const Text& y) -> std::optional<Node> { return Node{Text{x.s + y.s}}; },
      [](const Bold& x, const Bold& y) -> std::optional<Node> { return Node{Bold{concat(x.kids, y.kids)}}; },
      [](const Color& x, const Color& y) -> std::optional<Node> {
        return x.rgb == y.rgb ? std::optional<Node>{Node{Color{x.rgb, concat(x.kids, y.kids)}}} : std::nullopt;
      },
      [](const auto&, const auto&) -> std::optional<Node> { return std::nullopt; },
  }, a.v, b.v);
}

Kids normalize(const Kids& in) {
  Kids out;
  auto normKids = [](Node& n) { std::visit(overloaded{[](Text&) {}, [](auto& s) { s.kids = normalize(s.kids); }}, n.v); };
  for (const Node& n : in) {
    if (!out.empty())
      if (auto r = merge(out.back(), n)) { out.back() = std::move(*r); normKids(out.back()); continue; }
    out.push_back(n); normKids(out.back());
  }
  return out;
}

std::string show(const Kids& ks) {
  std::string s;
  for (auto& k : ks)
    s += std::visit(overloaded{
        [](const Text& t) { return "\"" + t.s + "\""; },
        [](const Bold& b) { return "[b " + show(b.kids) + "]"; },
        [](const Color& c) { char h[16]; std::snprintf(h, sizeof h, "#%06X", c.rgb); return "[" + std::string(h) + " " + show(c.kids) + "]"; },
    }, k.v);
  return s;
}

int main() {
  Kids cue = {{Text{"Hel"}}, {Text{"lo "}}, {Bold{{{Text{"bi"}}}}}, {Bold{{{Text{"g"}}}}}, {Text{" "}},
              {Color{0xFF0000, {{Text{"wor"}}}}}, {Color{0xFF0000, {{Text{"ld"}}}}}, {Color{0x0000FF, {{Text{"!"}}}}}};
  std::printf("整理前：%s\n整理后：%s\n", show(cue).c_str(), show(normalize(cue)).c_str());
}
```

```text
整理前："Hel""lo "[b "bi"][b "g"]" "[#FF0000 "wor"][#FF0000 "ld"][#0000FF "!"]
整理后："Hello "[b "big"]" "[#FF0000 "world"][#0000FF "!"]
```

### 4.3 四种写法的开销实测与选型

用 `projects/dp-lab/25-visitor/bench.cpp` 遍历 104 万个元素 × 10 遍（`-O2`，7 次取中位数），对比单分派虚函数、双分派访问者、`dynamic_cast` 链与 `std::visit`：

<figure class="diagram">
<svg viewBox="0 0 660 186" role="img" aria-label="四种写法遍历元素的平均时间，5 种元素随机排列：Clang 上单分派虚函数 5.36、访问者 5.49、dynamic_cast 链 36.41、std::visit 4.28 纳秒；GCC 上分别是 4.68、5.37、18.53、3.32 纳秒"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="138.0" y="30.0" width="22.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="166.8" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5.36 ns</text><rect x="138.0" y="60.0" width="23.3" height="20.0" rx="2" style="fill:var(--accent)"/><text x="167.3" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.49 ns</text><rect x="138.0" y="90.0" width="154.7" height="20.0" rx="2" style="fill:var(--danger)"/><text x="298.7" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">36.41 ns</text><rect x="138.0" y="120.0" width="18.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="162.2" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">4.28 ns</text><text x="398.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="398.0" y="30.0" width="19.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="423.9" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">4.68 ns</text><rect x="398.0" y="60.0" width="22.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="426.8" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.37 ns</text><rect x="398.0" y="90.0" width="78.8" height="20.0" rx="2" style="fill:var(--danger)"/><text x="482.8" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">18.53 ns</text><rect x="398.0" y="120.0" width="14.1" height="20.0" rx="2" style="fill:var(--link)"/><text x="418.1" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">3.32 ns</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">单分派虚函数</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">双分派访问者</text><text x="130.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">dynamic_cast 链</text><text x="130.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::visit</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp，-O2：104 万个元素（5 种随机排列）× 10 遍，7 次取中位数；GCC 跑在 colima 虚拟机里</text></g></svg>
</figure>

| 写法 | 随机排列（Clang / GCC） | 按类型排序（Clang / GCC） | 建树堆分配 | 漏改元素时 | 适合场景 |
| --- | :---: | :---: | :---: | :---: | --- |
| 操作写成虚函数 | 5.36 / 4.68 ns | 0.67 / 0.64 ns | 104 万次 | 编译报错 | 操作固定、元素常加（滤镜、解码器） |
| 经典双分派访问者 | 5.49 / 5.37 ns | 0.74 / 0.65 ns | 104 万次 | 编译报错 | 元素固定、操作常加；元素编译在库内、用户在库外扩展操作 |
| `dynamic_cast` 链 | 36.41 / 18.53 ns | 30.58 / 13.97 ns | 104 万次 | **0 警告** | 不推荐（慢 4～7 倍且静默丢数据） |
| `variant` + `visit` | 4.28 / 3.32 ns | 0.64 / 0.47 ns | **1 次** | 编译报错* | 元素固定且在一处定义；要值语义、返回值、多分派 |

\*有 `auto` 兜底分支时编译器不再报错。结论：**双分派本身只多 0.1～0.7 ns，选型看扩展方向，但别用 `dynamic_cast` 链**。

## 5. 访问者和相似的模式

| 模式 | 关心什么 | 和访问者的区别 |
| --- | --- | --- |
| **组合** | 树形结构，统一对待叶子和容器 | 组合管**结构**，访问者管结构上的**操作**，常搭配使用 |
| **迭代器** | 按顺序取出每个元素 | 迭代器只管「下一个」，元素同类型；访问者按具体类型分派 |
| **策略** | 同一算法的多种可互换实现 | 策略是单分派（只看算法）；访问者是双分派（元素 × 操作） |
| **命令** | 把一次请求封装成对象以排队、撤销 | 命令包的是「做什么」，不关心对象树结构 |
| **解释器** | 定义语法树并求值 | 解释器把求值写在各节点里；操作变多时常重构成访问者 |

## 6. 小结

- 访问者让元素只保留 `accept(Visitor&)`，把每种操作收进独立的 `Visitor` 子类；加操作改 1 处，代价是加元素改 6 处（表达式问题）。
- `v.visit(*this)` 用一次虚调用恢复 `*this` 的静态类型，再靠重载 + 虚函数完成双分派；**每个具体元素类都必须自己写 `accept`**。
- 漏改元素时，经典访问者和 `std::visit` 都会编译失败；`dynamic_cast` 链 0 警告且慢 4～7 倍。
- 现代 C++ 优先考虑 `std::variant` + `std::visit`：值语义零逐节点堆分配、无 `accept`、直接返回结果、支持多对象分派；需要库外扩展操作时选经典访问者。

## 7. 练习

1. **辨析**：下面四个需求，哪些适合用访问者？不适合的用什么？
   - (a) 播放器滤镜链有缩放、裁剪、调色等几十种滤镜且不断新增，对外只有「处理一帧」；
   - (b) MP4 解析库把文件解析成固定标准的 box 树（`ftyp`、`moov`、`trak`……），使用者要在库外自行实现打印、统计、校验；
   - (c) 播放列表按顺序一首首取出播放；
   - (d) 字幕元素树要新增导出第 6 种格式 TTML。
2. **设计**：用 `std::variant` 表示一条视频滤镜链：`Scale{w, h}`、`Crop{w, h, x, y}`、`Fps{fps}`、`Hflip{}`。写两个操作：一个拼出 ffmpeg 的 `-vf` 参数，一个从 `1280×720@24` 出发推算每秒处理的像素数。思考：`fps=12` 放开头还是末尾，处理像素差多少？
3. **找坑**：下面给字幕加了 `Underline`，`Visitor` 和 `AssExporter` 都补齐了，编译 0 警告，但下划线却导出成了粗体 `{\b1}再见{\b0}`。问题在哪？怎么改才能杜绝这类错误？

```cpp
// 《访问者模式》练习 3：加了 Underline，编译 0 警告，但导出成了粗体
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Text; struct Bold; struct Underline;
struct Visitor {
  virtual ~Visitor() = default;
  virtual void visit(const Text&) = 0;
  virtual void visit(const Bold&) = 0;
  virtual void visit(const Underline&) = 0;
};
struct Node { virtual ~Node() = default; virtual void accept(Visitor& v) const = 0; };
using Children = std::vector<std::unique_ptr<Node>>;
struct Text : Node {
  std::string s; explicit Text(std::string t) : s(std::move(t)) {}
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Bold : Node {
  Children kids;
  void accept(Visitor& v) const override { v.visit(*this); }
  void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
};
struct Underline : Bold {};                       // 直接继承 Bold 复用 kids

struct AssExporter : Visitor {
  std::string out;
  void visit(const Text& t) override { out += t.s; }
  void visit(const Bold& b) override { out += "{\\b1}"; b.visitKids(*this); out += "{\\b0}"; }
  void visit(const Underline& u) override { out += "{\\u1}"; u.visitKids(*this); out += "{\\u0}"; }
};

int main() {
  auto b = std::make_unique<Bold>();      b->kids.push_back(std::make_unique<Text>("Hello"));
  auto u = std::make_unique<Underline>(); u->kids.push_back(std::make_unique<Text>("再见"));
  Children cue; cue.push_back(std::move(b)); cue.push_back(std::move(u));
  AssExporter a;
  for (auto& n : cue) n->accept(a);
  std::printf("ASS：%s\n", a.out.c_str());
}
```

```text
ASS：{\b1}Hello{\b0}{\b1}再见{\b0}
```

<details>
<summary>点击查看答案</summary>

1. (a) **不适合**，元素常加而操作唯一，用普通虚函数 / 策略模式；
   (b) **适合**，且适合**经典访问者**（box 种类固定在库内，用户在库外加操作）；
   (c) **不适合**，用迭代器模式；
   (d) **适合**，新写一个 `TtmlExporter` 即可，元素类不动。

2. 定义 `using Filter = std::variant<Scale, Crop, Fps, Hflip>;`：拼 `-vf` 无状态，用 `std::visit(overloaded{...}, f)` 返回字符串；推算像素需携带当前 `(w, h, fps)` 和累计 `pixels`，写成带成员的函数对象按引用传给 `std::visit(estimator, f)`。`scale=640:360,hflip,fps=12` 每秒处理 **27.6 百万像素**，把 `fps=12` 挪到开头则只需 **13.8 百万像素**（少一半）。完整可运行版见 `projects/dp-lab/25-visitor/ex3_filters.cpp`（另附带状态的 `WidthMeasurer` 示例见 `ex2_width.cpp`）。

3. `Underline` 继承 `Bold` 却没重写 `accept`，于是复用了 `Bold::accept`——里面的 `*this` 静态类型是 `const Bold&`，直接分派给了 `visit(const Bold&)`。
   **根治办法**：把共用子节点放到抽象基类 `Span`，用 CRTP 模板自动生成 `accept`，并将所有具体元素标成 `final`（再写 `: Bold` 直接编译报错）：

   <!-- run: off -->
   ```cpp
   template <class Derived, class Base = Node>
   struct Visitable : Base {
     void accept(Visitor& v) const override { v.visit(static_cast<const Derived&>(*this)); }
   };
   struct Bold final : Visitable<Bold, Span> {};
   struct Underline final : Visitable<Underline, Span> {};
   ```

   完整修复版见 `projects/dp-lab/25-visitor/ex4_fixed.cpp`；改用 `std::variant` 则从根源上不会出现此问题。

</details>
