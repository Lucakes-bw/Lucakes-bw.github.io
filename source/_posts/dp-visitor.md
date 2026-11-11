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

> **本文目标**：理解访问者模式：类层次稳定、操作经常增加时，把每种操作写成一个访问者类，元素类只提供一个 `accept()`；弄懂双分派（为什么重载做不到、`accept` + `visit` 怎么做到）；量出「加一种操作」和「加一种元素」在几种写法下各要改几处、漏改时编译器能不能发现；会用 `std::variant` + `std::visit` 写现代版本，包括同时分派两个对象；知道几种写法的开销；分清访问者和组合、迭代器、策略、解释器。
>
> **前置知识**：上一篇《责任链模式》；《组合模式》（访问者最常用在组合结构上）、《迭代器模式》、《状态模式》（第 4 节用过 `std::variant`）；音视频专栏里字幕的部分（SRT、ASS 两种格式）。
>
> **编译环境**：g++/clang++ -std=c++17，完整代码在 projects/dp-lab/25-visitor/

## 1. 问题：一棵树，很多种操作

### 1.1 字幕元素树

一条字幕不只是一串文字，还有样式：粗体、斜体、颜色、换行。不同的字幕格式写法完全不同：

| 格式 | 粗体 | 颜色 | 换行 |
|------|------|------|------|
| SRT | `<b>big</b>` | `<font color="#FF0000">…</font>` | 真的换行 |
| ASS | `{\b1}big{\b0}` | `{\c&H0000FF&}…{\c}`（蓝绿红的顺序） | `\N` |
| 纯文本 | 去掉 | 去掉 | 真的换行 |

播放器、字幕编辑器内部通常先把字幕解析成一棵**元素树**，再从这棵树导出成各种格式，或者统计字数、算显示宽度、检查有没有超长。下面这条字幕

```
Hello big world        （big 是粗体，world 是红色斜体）
再见
```

解析出来是这样一棵树：`Text("Hello ")`、`Bold[Text("big")]`、`Text(" ")`、`Italic[Color(红)[Text("world")]]`、`Br`、`Text("再见")`。元素一共 5 种：`Text`、`Bold`、`Italic`、`Color`、`Br`，其中 `Bold`、`Italic`、`Color` 带子节点，是《组合模式》里的组合节点。

最直接的写法是把每种操作都写成基类 `Node` 的虚函数，每个元素类各实现一遍：

```cpp
// 《访问者模式》：把每种操作都写成元素类的虚函数：字幕元素树会统计字数、导出纯文本、导出 ASS
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Node {                       // 每种操作一个虚函数
  virtual ~Node() = default;
  virtual int chars() const = 0;
  virtual std::string plain() const = 0;
  virtual std::string ass() const = 0;
};
using Children = std::vector<std::unique_ptr<Node>>;

int utf8Len(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

struct Text : Node {
  std::string s;
  explicit Text(std::string t) : s(std::move(t)) {}
  int chars() const override { return utf8Len(s); }
  std::string plain() const override { return s; }
  std::string ass() const override { return s; }
};
struct Span : Node {                // 带子节点的元素：字数和纯文本都是把子节点的拼起来
  Children kids;
  int chars() const override { int n = 0; for (auto& k : kids) n += k->chars(); return n; }
  std::string plain() const override { std::string s; for (auto& k : kids) s += k->plain(); return s; }
  std::string kidsAss() const { std::string s; for (auto& k : kids) s += k->ass(); return s; }
};
struct Bold : Span {
  std::string ass() const override { return "{\\b1}" + kidsAss() + "{\\b0}"; }
};
struct Italic : Span {
  std::string ass() const override { return "{\\i1}" + kidsAss() + "{\\i0}"; }
};
struct Color : Span {
  std::uint32_t rgb;
  explicit Color(std::uint32_t c) : rgb(c) {}
  std::string ass() const override {
    char tag[32];                     // ASS 的颜色是 &HBBGGRR&
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", rgb & 0xFF, (rgb >> 8) & 0xFF, rgb >> 16);
    return tag + kidsAss() + "{\\c}";
  }
};
struct Br : Node {
  int chars() const override { return 0; }
  std::string plain() const override { return "\n"; }
  std::string ass() const override { return "\\N"; }
};

// ---- 以下是测试
struct Cue {                        // 一条字幕
  Children items;
  template <class F> auto join(F f) const { decltype(f(*items[0])) r{}; for (auto& n : items) r += f(*n); return r; }
};

std::unique_ptr<Node> text(std::string s) { return std::make_unique<Text>(std::move(s)); }
template <class S, class... K>
std::unique_ptr<Node> span(S s, K... kids) {
  auto p = std::make_unique<S>(std::move(s));
  (p->kids.push_back(std::move(kids)), ...);
  return p;
}

Cue sample() {   // Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见
  Cue c;
  c.items.push_back(text("Hello "));
  c.items.push_back(span(Bold{}, text("big")));
  c.items.push_back(text(" "));
  c.items.push_back(span(Italic{}, span(Color{0xFF0000}, text("world"))));
  c.items.push_back(std::make_unique<Br>());
  c.items.push_back(text("再见"));
  return c;
}

int main() {
  Cue cue = sample();
  int n = cue.join([](const Node& x) { return x.chars(); });
  std::string plain = cue.join([](const Node& x) { return x.plain(); });
  std::string ass = cue.join([](const Node& x) { return x.ass(); });
  std::printf("字数：%d\n纯文本：%s\nASS：%s\n", n, plain.c_str(), ass.c_str());
}
```

输出：

```
字数：17
纯文本：Hello big world
再见
ASS：Hello {\b1}big{\b0} {\i1}{\c&H0000FF&}world{\c}{\i0}\N再见
```

`Span` 把「带子节点」的共同部分提了出来：字数和纯文本都是把子节点的结果拼起来，所以 `Bold`、`Italic`、`Color` 只需要各写一个 `ass()`。

### 1.2 加一种导出格式

现在要支持导出 SRT。按这种写法，要在基类加一个纯虚函数 `srt()`，再到 `Text`、`Span`、`Bold`、`Italic`、`Color`、`Br` 里各加一个实现（完整的文件是 `methods_srt.cpp`）：

<!-- run: off -->
```diff
 struct Node {
   ...
   virtual std::string ass() const = 0;
+  virtual std::string srt() const = 0;
 };
 struct Text : Node {
   ...
+  std::string srt() const override { return s; }
 };
 struct Span : Node {
   ...
+  std::string kidsSrt() const { std::string s; for (auto& k : kids) s += k->srt(); return s; }
 };
 struct Bold : Span {
   std::string ass() const override { return "{\\b1}" + kidsAss() + "{\\b0}"; }
+  std::string srt() const override { return "<b>" + kidsSrt() + "</b>"; }
 };
 ...（Italic、Color、Br 同样各加一个）
```

`measure.sh` 用 diff 统计：**7 处改动**，基类和每个元素类都动了。这还只是一种格式，真实的字幕工具要导出 SRT、ASS、WebVTT、TTML、mov_text，还要统计字数、算显示宽度、检查行长、渲染成位图。每加一种，所有元素类都要改一遍，元素类越来越胖：`Bold` 里同时有 SRT、ASS、WebVTT 的知识，还有渲染用的字体逻辑。元素类本来很稳定（字幕样式就那么几种，几年不变），却因为操作在变而不停地改。

### 1.3 写在外面：dynamic_cast

另一种思路：元素类里什么操作都不放，只放数据；操作写成类外的函数，用 `dynamic_cast` 一个个试，看拿到的是哪种元素。加一种操作就是加一个函数，元素类一行不改。

问题出在加元素的时候。后来有人加了一种元素「下划线」`Underline`，没有去改这些函数：

```cpp
// 《访问者模式》：操作写成类外的函数，用 dynamic_cast 判断类型；后来加了 Underline，没人改这些函数
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Node { virtual ~Node() = default; };      // 元素类里什么操作都没有
using Children = std::vector<std::unique_ptr<Node>>;

struct Text : Node { std::string s; explicit Text(std::string t) : s(std::move(t)) {} };
struct Span : Node { Children kids; };
struct Bold : Span {};
struct Italic : Span {};
struct Color : Span { std::uint32_t rgb; explicit Color(std::uint32_t c) : rgb(c) {} };
struct Br : Node {};
struct Underline : Span {};                      // 后来加的

int utf8Len(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

int chars(const Node& n) {
  if (auto t = dynamic_cast<const Text*>(&n)) return utf8Len(t->s);
  if (auto s = dynamic_cast<const Span*>(&n)) { int k = 0; for (auto& c : s->kids) k += chars(*c); return k; }
  return 0;
}

std::string ass(const Node& n) {
  auto kids = [](const Span& s) { std::string r; for (auto& c : s.kids) r += ass(*c); return r; };
  if (auto t = dynamic_cast<const Text*>(&n)) return t->s;
  if (auto b = dynamic_cast<const Bold*>(&n)) return "{\\b1}" + kids(*b) + "{\\b0}";
  if (auto i = dynamic_cast<const Italic*>(&n)) return "{\\i1}" + kids(*i) + "{\\i0}";
  if (auto c = dynamic_cast<const Color*>(&n)) {
    char tag[32];
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", c->rgb & 0xFF, (c->rgb >> 8) & 0xFF, c->rgb >> 16);
    return tag + kids(*c) + "{\\c}";
  }
  if (dynamic_cast<const Br*>(&n)) return "\\N";
  return "";                                     // 不认识的类型：什么也不输出
}

// ---- 以下是测试
std::unique_ptr<Node> text(std::string s) { return std::make_unique<Text>(std::move(s)); }
template <class S, class... K>
std::unique_ptr<Node> span(S s, K... kids) {
  auto p = std::make_unique<S>(std::move(s));
  (p->kids.push_back(std::move(kids)), ...);
  return p;
}

int main() {
  Children cue;     // Hello <b>big</b> <i><font color=红>world</font></i> ↵ <u>再见</u>
  cue.push_back(text("Hello "));
  cue.push_back(span(Bold{}, text("big")));
  cue.push_back(text(" "));
  cue.push_back(span(Italic{}, span(Color{0xFF0000}, text("world"))));
  cue.push_back(std::make_unique<Br>());
  cue.push_back(span(Underline{}, text("再见")));
  int n = 0;
  std::string out;
  for (auto& x : cue) { n += chars(*x); out += ass(*x); }
  std::printf("字数：%d\nASS：%s\n", n, out.c_str());
}
```

输出：

```
字数：17
ASS：Hello {\b1}big{\b0} {\i1}{\c&H0000FF&}world{\c}{\i0}\N
```

字数是对的（`Underline` 也是 `Span`，碰巧被第二个分支接住了），但导出的 ASS 里，最后那句「再见」**整个不见了**：`ass()` 不认识 `Underline`，走到最后的 `return ""`。用 `-Wall -Wextra` 编译，0 个错误、0 个警告。这种 bug 要等到用户看见字幕少了一句才会发现。

`dynamic_cast` 还慢：每次都要从头一个个试，第 4 节实测比虚函数慢 4～7 倍。

### 1.4 重载不是分派

既然每种元素要一个函数，那写一组重载 `visit(const Text&)`、`visit(const Br&)` 不就行了？编译器会帮我们挑。试一下：

```cpp
// 《访问者模式》：重载不是分派：按参数的静态类型选重载，手里只有 Node& 时永远选中 visit(const Node&)
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

  AssExporter a;
  for (auto& n : cue) a.visit(*n);               // *n 的静态类型是 Node&
  std::printf("按重载：%s\n", a.out.c_str());

  AssExporter b;
  Text hello("Hello");
  Br br;
  b.visit(hello);                                // 静态类型就是 Text&、Br&，才选得对
  b.visit(br);
  std::printf("静态类型已知：%s\n", b.out.c_str());
}
```

输出：

```
按重载：[?][?][?]
静态类型已知：Hello\N
```

三个元素都走进了兜底的 `visit(const Node&)`。原因是：**重载是在编译期按参数的静态类型选的**。循环里 `*n` 的静态类型是 `Node&`，编译器只能选 `visit(const Node&)`，它不会在运行时看 `*n` 到底是 `Text` 还是 `Br`。只有在静态类型本来就是 `Text&` 的地方（第二段），才选得对。

运行时按类型选函数的机制，C++ 里只有虚函数，而虚函数只按**一个**对象（`this`）的动态类型选。我们需要的是按两个东西选：元素是什么类型 × 操作是哪一种。这叫**双分派**（double dispatch）。

## 2. 访问者模式

### 2.1 意图

> **GoF**：表示一个作用于某对象结构中的各元素的操作。它使你可以在不改变各元素的类的前提下定义作用于这些元素的新操作。

大白话：**元素类只留一个 `accept()` 入口，每种操作写成一个访问者类，访问者里给每种元素各写一个 `visit()`**。加操作就是加一个访问者类，元素类不动。

### 2.2 双分派

关键是 `accept()` 的写法，每个元素类都一样，就一行：

<!-- run: off -->
```cpp
void Bold::accept(Visitor& v) const { v.visit(*this); }
```

这一行完成了两次分派：

1. 调用 `n->accept(v)` 时，`accept` 是虚函数，按**元素**的动态类型选中 `Bold::accept`；
2. 在 `Bold::accept` 里，`*this` 的静态类型就是 `const Bold&`，所以重载在编译期就选中了 `visit(const Bold&)`；`visit` 又是虚函数，按**访问者**的动态类型选中 `AssExporter::visit(const Bold&)`。

第 1.4 节的重载之所以失败，是因为在循环里静态类型只有 `Node&`。`accept` 用一次虚调用「进到」元素类内部，在那里静态类型就知道了，重载自然选得对。所以每个元素类都**必须自己写** `accept`，哪怕代码一模一样，不能放到基类里（基类里 `*this` 的静态类型是 `Node`，又回到 1.4 的问题了）。

### 2.3 重构字幕树

```cpp
// 《访问者模式》：经典的双分派写法：元素类只有一个 accept()，每种操作写成一个访问者类
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Text; struct Bold; struct Italic; struct Color; struct Br;

struct Visitor {                    // 每种元素一个 visit()
  virtual ~Visitor() = default;
  virtual void visit(const Text&) = 0;
  virtual void visit(const Bold&) = 0;
  virtual void visit(const Italic&) = 0;
  virtual void visit(const Color&) = 0;
  virtual void visit(const Br&) = 0;
};

struct Node {
  virtual ~Node() = default;
  virtual void accept(Visitor& v) const = 0;
};
using Children = std::vector<std::unique_ptr<Node>>;

struct Text : Node {
  std::string s;
  explicit Text(std::string t) : s(std::move(t)) {}
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Span : Node {                // 带子节点的元素
  Children kids;
  void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
};
struct Bold : Span {
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Italic : Span {
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Color : Span {
  std::uint32_t rgb;
  explicit Color(std::uint32_t c) : rgb(c) {}
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Br : Node {
  void accept(Visitor& v) const override { v.visit(*this); }
};

int utf8Len(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

// ---- 操作：每种一个访问者类，元素类一行都不用改
struct CharCounter : Visitor {      // 统计字数（换行不算）
  int n = 0;
  void visit(const Text& t) override { n += utf8Len(t.s); }
  void visit(const Bold& b) override { b.visitKids(*this); }
  void visit(const Italic& i) override { i.visitKids(*this); }
  void visit(const Color& c) override { c.visitKids(*this); }
  void visit(const Br&) override {}
};

struct PlainExporter : Visitor {    // 去掉所有样式，只留文字
  std::string out;
  void visit(const Text& t) override { out += t.s; }
  void visit(const Bold& b) override { b.visitKids(*this); }
  void visit(const Italic& i) override { i.visitKids(*this); }
  void visit(const Color& c) override { c.visitKids(*this); }
  void visit(const Br&) override { out += '\n'; }
};

struct AssExporter : Visitor {      // 导出成 ASS 的覆盖标签
  std::string out;
  void visit(const Text& t) override { out += t.s; }
  void visit(const Bold& b) override { out += "{\\b1}"; b.visitKids(*this); out += "{\\b0}"; }
  void visit(const Italic& i) override { out += "{\\i1}"; i.visitKids(*this); out += "{\\i0}"; }
  void visit(const Color& c) override {
    char tag[32];                     // ASS 的颜色是 &HBBGGRR&
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", c.rgb & 0xFF, (c.rgb >> 8) & 0xFF, c.rgb >> 16);
    out += tag; c.visitKids(*this); out += "{\\c}";
  }
  void visit(const Br&) override { out += "\\N"; }
};

// ---- 以下是测试
struct Cue {                        // 一条字幕
  Children items;
  void accept(Visitor& v) const { for (auto& n : items) n->accept(v); }
};

std::unique_ptr<Node> text(std::string s) { return std::make_unique<Text>(std::move(s)); }
template <class S, class... K>
std::unique_ptr<Node> span(S s, K... kids) {
  auto p = std::make_unique<S>(std::move(s));
  (p->kids.push_back(std::move(kids)), ...);
  return p;
}

Cue sample() {   // Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见
  Cue c;
  c.items.push_back(text("Hello "));
  c.items.push_back(span(Bold{}, text("big")));
  c.items.push_back(text(" "));
  c.items.push_back(span(Italic{}, span(Color{0xFF0000}, text("world"))));
  c.items.push_back(std::make_unique<Br>());
  c.items.push_back(text("再见"));
  return c;
}

int main() {
  Cue cue = sample();
  CharCounter cc;   cue.accept(cc);
  PlainExporter pe; cue.accept(pe);
  AssExporter ae;   cue.accept(ae);
  std::printf("字数：%d\n纯文本：%s\nASS：%s\n", cc.n, pe.out.c_str(), ae.out.c_str());
}
```

输出和第 1 节一样。现在元素类只剩数据和 `accept()`，三种操作各是一个访问者类，统计字数的状态 `n`、导出的结果 `out` 都放在访问者里。

加导出 SRT，就是新写一个 `SrtExporter`（完整的文件是 `visitor_srt.cpp`），别的地方一行不动：

<!-- run: off -->
```cpp
struct SrtExporter : Visitor {      // 新操作：导出成 SRT（HTML 风格的标签）
  std::string out;
  void visit(const Text& t) override { out += t.s; }
  void visit(const Bold& b) override { out += "<b>"; b.visitKids(*this); out += "</b>"; }
  void visit(const Italic& i) override { out += "<i>"; i.visitKids(*this); out += "</i>"; }
  void visit(const Color& c) override { /* <font color="#RRGGBB"> … </font> */ }
  void visit(const Br&) override { out += '\n'; }
};
```

### 2.4 类图和时序图

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

在导出 ASS 时，遍历到粗体节点，调用顺序是这样的：

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

每访问一个元素，都是「元素 → 访问者」两次虚调用。容器元素的子节点由访问者决定什么时候访问（第 4 步），所以 ASS 的闭合标签 `{\b0}` 能放在子节点后面。

### 2.5 演示

下面的演示把字幕树画在左边，右边是访问者和它的输出。「导出 ASS」「统计字数」一步步走完整棵树，每一步标出两次分派各选中了哪个函数；「只用重载」演示第 1.4 节的失败。

<div class="dsviz" data-viz="dp-visitor"></div>

### 2.6 加操作容易，加元素难

访问者把「加操作」变容易了，代价是「加元素」变难了。给访问者版加 `Underline`（完整的文件是 `visitor_underline.cpp`）：

<!-- run: off -->
```diff
-struct Text; struct Bold; struct Italic; struct Color; struct Br;
+struct Text; struct Bold; struct Italic; struct Color; struct Br; struct Underline;
 struct Visitor {
   ...
+  virtual void visit(const Underline&) = 0;
 };
+struct Underline : Span {
+  void accept(Visitor& v) const override { v.visit(*this); }
+};
 struct CharCounter : Visitor {
   ...
+  void visit(const Underline& u) override { u.visitKids(*this); }
 };
 （PlainExporter、AssExporter 同样各加一个）
```

把三种写法都改一遍，用 `measure.sh` 统计：

<figure class="diagram">
<svg viewBox="0 0 640 156" role="img" aria-label="三种写法加一种操作、加一种元素各要改几处：加导出 SRT，虚函数版改 7 处，访问者和 variant 都只改 1 处；加下划线元素，虚函数版改 1 处，访问者改 6 处，variant 改 5 处"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">加一种操作（导出 SRT）</text><rect x="138.0" y="30.0" width="140.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="284.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">7 处</text><rect x="138.0" y="60.0" width="20.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="164.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1 处</text><rect x="138.0" y="90.0" width="20.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="164.0" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1 处</text><text x="388.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">加一种元素（下划线）</text><rect x="388.0" y="30.0" width="20.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="414.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">1 处</text><rect x="388.0" y="60.0" width="120.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="514.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6 处</text><rect x="388.0" y="90.0" width="100.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="494.0" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">5 处</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">操作写成虚函数</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">访问者</text><text x="130.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">variant + visit</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：diff 只比较类和操作的部分（不含 main 里的测试），相邻的改动算一处；Clang 和 GCC 结果相同</text></g></svg>
</figure>

两种操作正好相反。把「元素 × 操作」画成一张表就清楚了：虚函数写法里一个元素类管**一行**，访问者写法里一个访问者类管**一列**。加一行（新元素）或加一列（新操作），总有一种写法要伸进很多个类里各改一格。

<figure class="diagram">
<svg viewBox="0 0 720 270" role="img" aria-label="元素乘操作的表格：左边虚函数写法里一个元素类管一行，加一列操作要改每个类，加一行元素只要新写一个类；右边访问者写法里一个访问者类管一列，加一列操作只要新写一个访问者，加一行元素要改每个访问者"><g style="font-family:var(--mono)"><text x="10.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">操作写成虚函数：一个类管一行</text><text x="97.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">字数</text><text x="159.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">纯文本</text><text x="221.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">ASS</text><text x="283.0" y="40.0" text-anchor="middle" style="fill:var(--link);font-size:11px;font-weight:700">SRT</text><text x="58.0" y="69.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Text</text><rect x="68" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="52" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="97.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Bold</text><rect x="68" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="80" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="125.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Italic</text><rect x="68" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="108" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="153.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Color</text><rect x="68" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="136" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="181.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Br</text><rect x="68" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="130" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="192" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="254" y="164" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="58.0" y="209.0" text-anchor="end" style="fill:var(--link);font-size:11px;font-weight:700">Underline</text><rect x="68" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="130" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="192" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="254" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="66" y="50" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="78" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="106" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="134" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="162" width="186" height="28" rx="5" style="fill:none;stroke:var(--warn);stroke-width:1.6"/><rect x="66" y="50" width="186" height="140" rx="0" style="fill:none;stroke:none"/><rect x="253" y="50" width="60" height="140" rx="5" style="fill:none;stroke:var(--danger);stroke-width:2"/><rect x="66" y="191" width="186" height="26" rx="5" style="fill:none;stroke:var(--accent);stroke-width:2"/><text x="10.0" y="240.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">加 SRT（新的一列）：要伸进 5 个类各加一格（红框）</text><text x="10.0" y="256.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">加 Underline（新的一行）：新写一个类就行（绿框）</text><text x="370.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">访问者：一个访问者类管一列</text><text x="457.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">字数</text><text x="519.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">纯文本</text><text x="581.0" y="40.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">ASS</text><text x="643.0" y="40.0" text-anchor="middle" style="fill:var(--link);font-size:11px;font-weight:700">SRT</text><text x="418.0" y="69.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Text</text><rect x="428" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="52" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="52" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="97.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Bold</text><rect x="428" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="80" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="80" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="125.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Italic</text><rect x="428" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="108" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="108" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="153.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Color</text><rect x="428" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="136" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="136" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="181.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">Br</text><rect x="428" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="490" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="552" y="164" width="58" height="24" rx="3" style="fill:var(--code-bg);stroke:var(--border);stroke-width:1"/><rect x="614" y="164" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><text x="418.0" y="209.0" text-anchor="end" style="fill:var(--link);font-size:11px;font-weight:700">Underline</text><rect x="428" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="490" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="552" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="614" y="192" width="58" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1;stroke-dasharray:3 3"/><rect x="426" y="50" width="62" height="140" rx="5" style="fill:none;stroke:var(--accent);stroke-width:1.6"/><rect x="488" y="50" width="62" height="140" rx="5" style="fill:none;stroke:var(--accent);stroke-width:1.6"/><rect x="550" y="50" width="62" height="140" rx="5" style="fill:none;stroke:var(--accent);stroke-width:1.6"/><rect x="613" y="50" width="60" height="140" rx="5" style="fill:none;stroke:var(--accent);stroke-width:2"/><rect x="426" y="191" width="186" height="26" rx="5" style="fill:none;stroke:var(--danger);stroke-width:2"/><text x="370.0" y="240.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">加 SRT（新的一列）：新写一个访问者就行（绿框）</text><text x="370.0" y="256.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">加 Underline（新的一行）：要伸进每个访问者各加一格（红框）</text></g></svg>
</figure>

这叫**表达式问题**（expression problem）：在静态类型语言里，很难做到加行、加列都只写新代码、不改旧代码。访问者没有消除这个问题，只是选了一边。所以选不选访问者，看哪个方向更常变：

- 元素种类稳定、操作经常加（字幕样式、语法树节点、文件格式的结构）：用访问者；
- 元素种类经常加、操作很少变（各种滤镜、各种解码器，对外只有「处理一帧」这一个操作）：用普通的虚函数。

访问者在「加元素」上还有一个好处：**漏改时编译器会报错**。把 `visitor_underline.cpp` 里三个访问者处理 `Underline` 的函数删掉再编译，`CharCounter`、`PlainExporter`、`AssExporter` 都变成了抽象类，创建它们的地方各报一个错；variant 版也一样（第 4 节），而 dynamic_cast 版一个警告都没有：

<figure class="diagram">
<svg viewBox="0 0 680 156" role="img" aria-label="加了下划线元素却没改任何操作时编译器报的错误数：访问者在 Clang 和 GCC 上都是 3 个；variant 在 Clang 上 12 个、GCC 上 18 个；dynamic_cast 链 0 个错误 0 个警告"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="138.0" y="30.0" width="24.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="168.0" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">3 个 error</text><rect x="138.0" y="60.0" width="96.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="240.0" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">12 个 error</text><rect x="138.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--danger)"/><text x="145.5" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">0 个 error</text><text x="408.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="408.0" y="30.0" width="24.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="438.0" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">3 个 error</text><rect x="408.0" y="60.0" width="144.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="558.0" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">18 个 error</text><rect x="408.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--danger)"/><text x="415.5" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">0 个 error</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">访问者</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">variant + visit</text><text x="130.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">dynamic_cast 链</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：从「加了下划线」的版本里删掉所有处理 Underline 的函数再编译；dynamic_cast 链加 -Wall -Wextra 也没有警告</text></g></svg>
</figure>

注意访问者版的错误报在**创建访问者对象**的地方，不是在类定义处。如果某个访问者只在别的源文件里创建，那个文件重新编译时才会报错。

## 3. 深入访问者模式

### 3.1 谁来遍历子节点

遍历可以放在三个地方：

| 放在哪 | 写法 | 特点 |
|------|------|------|
| 元素的 `accept` 里 | `Bold::accept` 先 `v.visit(*this)`，再对每个子节点 `accept` | 访问者最简单，但没法在子节点**之后**做事（ASS 的 `{\b0}` 写不出来），也没法跳过子树 |
| 访问者里（本文的写法） | `visit(const Bold&)` 里自己调用 `visitKids` | 访问者决定先后、跳不跳过；每个访问者都要记得遍历，忘了子树就丢了 |
| 单独的遍历器 | 一个遍历函数负责走树，在进入、离开节点时调用访问者的 `enter` / `leave` | 遍历逻辑只写一次；访问者变成两组函数 |

Clang 的 `RecursiveASTVisitor` 用的是第三种：它负责走遍整棵语法树，用户的访问者只写感兴趣的 `VisitCallExpr()` 之类的函数，返回 `false` 可以提前结束。

### 3.2 访问者要看元素的内部

访问者是在元素类**外面**写操作，所以元素要把操作需要的数据露出来：`Text::s`、`Color::rgb`、`Span::kids` 都是公有的。如果元素有不变式要保护，就只能提供只读的访问函数，或者把访问者声明成友元。这是访问者对封装的代价：操作搬出去了，数据也得跟着让人看。

### 3.3 返回值和状态

经典的 `visit()` 返回 `void`，因为同一个 `Visitor` 接口要给所有访问者用，返回类型没法统一。所以结果只能存在访问者的成员里（`CharCounter::n`、`AssExporter::out`），用完再取。这也意味着访问者对象**带状态**：同一个 `CharCounter` 访问两条字幕，`n` 会累加；想重用就得记得清零。

有几种改法：给 `Visitor` 做成模板 `Visitor<R>`，每个元素写一组 `accept` 的重载；或者干脆用第 4 节的 `std::visit`，`operator()` 可以直接返回任何类型。

### 3.4 FFmpeg 的字幕编码器

FFmpeg 内部的文本字幕统一用 ASS 表示：SRT、WebVTT 解码时都转成 ASS 的对话行；编码成别的格式时，再把 ASS 的覆盖标签拆开，一段一段交给编码器。拆开这一步是 `libavcodec/ass_split.c` 里的 `ff_ass_split_override_codes(callbacks, priv, buf)`，`callbacks` 是一个结构体：

<!-- run: off -->
```c
typedef struct {
    void (*text)(void *priv, const char *text, int len);
    void (*new_line)(void *priv, int forced);
    void (*style)(void *priv, char style, int close);   // b、i、u、s，close 表示是开还是关
    void (*color)(void *priv, unsigned int color, unsigned int color_id);
    ...                                                 // alpha、font_name、font_size、alignment、move……
    void (*end)(void *priv);
} ASSCodesCallbacks;
```

这就是 C 语言写的访问者：结构体里每个函数指针对应一种元素（文字、换行、样式、颜色……），`ff_ass_split_override_codes` 负责遍历、按元素类型调用对应的函数（分派），`priv` 就是访问者的状态。每种字幕编码器各填一份：`srtenc.c` 的 `srt_callbacks` 把样式写成 `<b>`、`<font color=…>`；`webvttenc.c` 的 `webvtt_callbacks`、`movtextenc.c` 的 `mov_text_callbacks` 各写各的；`ttmlenc.c` 只填了 `text` 和 `new_line`。加一种字幕格式，就是新写一组回调。

还有一个和本文不同的设计：`ass_split.c` 调用每个回调之前都先判断它是不是 `NULL`，没填的就跳过。WebVTT 不支持字体大小，`font_size` 就填 `NULL`。这相当于每个 `visit` 都有一个「什么也不做」的默认实现，好处是给 ASS 加一种新的标签时，旧的编码器不用改；坏处正是第 2.6 节说的：哪个编码器该处理却没处理，没人会提醒。

### 3.5 编译器里的访问者

访问者最经典的用武之地是编译器：语法树的节点种类基本固定，要做的事却越来越多（打印、类型检查、常量折叠、生成代码……）。但大型编译器很少用 `accept` + 虚函数 `visit` 这套写法：

- Clang 的 `StmtVisitor` 根据节点里存的类型编号 `getStmtClass()` 做一个大 `switch`，再调用 `VisitXxx()`；
- `RecursiveASTVisitor` 是 CRTP 模板，用户的访问者作为模板参数传进去，调用全在编译期确定，可以内联；
- LLVM 的 `InstVisitor` 也是按指令的操作码 `switch`。

节点里存一个类型编号、用 `switch` 分派，和第 4 节的 `std::variant` 是同一个思路：类型集合固定，就用编号代替虚函数。

## 4. 现代 C++ 写法

### 4.1 std::variant + std::visit

《状态模式》第 4 节用 `std::variant` 表示状态，这里用它表示元素：元素就是普通的结构体，没有基类、没有虚函数、没有 `accept`；一个 `Node` 里放一个 `std::variant`，存的是哪种元素由 `index()` 记着。`std::visit(op, node.v)` 按实际存的类型调用 `op` 的对应重载，这就是分派。

```cpp
// 《访问者模式》：std::variant + std::visit：元素是普通的值类型，每种操作是一个带重载 operator() 的函数对象
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

struct Node;
using Kids = std::vector<Node>;     // vector 允许元素类型此时还不完整（C++17）

struct Text { std::string s; };
struct Bold { Kids kids; };
struct Italic { Kids kids; };
struct Color { std::uint32_t rgb; Kids kids; };
struct Br {};

struct Node { std::variant<Text, Bold, Italic, Color, Br> v; };

int utf8Len(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

// ---- 操作：std::visit 按 v 里实际存的类型调用对应的 operator()，可以直接返回结果
struct CharCounter {
  int operator()(const Text& t) const { return utf8Len(t.s); }
  int operator()(const Bold& b) const { return sum(b.kids); }
  int operator()(const Italic& i) const { return sum(i.kids); }
  int operator()(const Color& c) const { return sum(c.kids); }
  int operator()(const Br&) const { return 0; }
  int sum(const Kids& ks) const { int n = 0; for (auto& k : ks) n += std::visit(*this, k.v); return n; }
};

struct PlainExporter {
  std::string operator()(const Text& t) const { return t.s; }
  std::string operator()(const Bold& b) const { return join(b.kids); }
  std::string operator()(const Italic& i) const { return join(i.kids); }
  std::string operator()(const Color& c) const { return join(c.kids); }
  std::string operator()(const Br&) const { return "\n"; }
  std::string join(const Kids& ks) const { std::string s; for (auto& k : ks) s += std::visit(*this, k.v); return s; }
};

struct AssExporter {
  std::string operator()(const Text& t) const { return t.s; }
  std::string operator()(const Bold& b) const { return "{\\b1}" + join(b.kids) + "{\\b0}"; }
  std::string operator()(const Italic& i) const { return "{\\i1}" + join(i.kids) + "{\\i0}"; }
  std::string operator()(const Color& c) const {
    char tag[32];                     // ASS 的颜色是 &HBBGGRR&
    std::snprintf(tag, sizeof tag, "{\\c&H%02X%02X%02X&}", c.rgb & 0xFF, (c.rgb >> 8) & 0xFF, c.rgb >> 16);
    return tag + join(c.kids) + "{\\c}";
  }
  std::string operator()(const Br&) const { return "\\N"; }
  std::string join(const Kids& ks) const { std::string s; for (auto& k : ks) s += std::visit(*this, k.v); return s; }
};

// ---- 以下是测试
Kids sample() {   // Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见
  return {
      {Text{"Hello "}},
      {Bold{{{Text{"big"}}}}},
      {Text{" "}},
      {Italic{{{Color{0xFF0000, {{Text{"world"}}}}}}}},
      {Br{}},
      {Text{"再见"}},
  };
}

int main() {
  Kids cue = sample();
  std::printf("字数：%d\n纯文本：%s\nASS：%s\n", CharCounter{}.sum(cue), PlainExporter{}.join(cue).c_str(),
              AssExporter{}.join(cue).c_str());
  std::printf("sizeof(Node) = %zu，存了哪种元素：v.index() = %zu\n", sizeof(Node), cue[1].v.index());
}
```

输出：

```
字数：17
纯文本：Hello big world
再见
ASS：Hello {\b1}big{\b0} {\i1}{\c&H0000FF&}world{\c}{\i0}\N再见
sizeof(Node) = 40，存了哪种元素：v.index() = 1
```

和经典写法比：

- **元素是值**：`Kids` 是 `std::vector<Node>`，元素连续存放，不用 `unique_ptr`，可以直接拷贝（`merge.cpp` 就靠这个）。`bench` 里建 104 万个元素，基类指针的写法分配了 104 万次堆内存，variant 只有 vector 自己的 1 次；
- **不用 `accept`**：分派由 `std::visit` 完成，也就不会出现「忘了写 `accept`」（练习 4）；
- **能直接返回结果**：`CharCounter` 的 `operator()` 返回 `int`，`AssExporter` 的返回 `std::string`，访问者不用带状态；
- **漏写会编译失败**：`std::visit` 要求每种可能的类型都能调用。删掉处理 `Underline` 的重载，Clang 报 12 个错误，第一条就是 ``static assertion failed … `std::visit` requires the visitor to be exhaustive``；GCC 报 18 个，都在标准库的头文件里，要往下翻才找得到是哪个类型没处理。

代价也一样在：加一种元素要改 variant 的类型列表和每一个操作，统计是 5 处。而且类型列表是**封闭**的：别的模块想加一种元素，只能改这个头文件，没法像继承那样在外面扩展。`sizeof(Node)` 是最大那种元素的大小加上类型编号，元素大小差得多时会浪费空间。

### 4.2 overloaded 和兜底

不想为每个操作写一个结构体时，可以用 C++17 常见的 `overloaded` 小工具把几个 lambda 拼成一个重载集合：

<!-- run: off -->
```cpp
template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;   // C++20 起可以不写

std::visit(overloaded{
    [](const Text& t) { ... },
    [](const Br&) { ... },
    [](const auto& span) { ... },   // 其余类型：Bold、Italic、Color
}, node.v);
```

最后那个 `auto` 的 lambda 很方便，三种容器元素一行搞定。但要小心：它是个**兜底**，以后加了 `Underline`，`std::visit` 会选中它，编译器不再报错，第 2.6 节说的那个好处就没了。兜底分支只用在「其余类型确实都该这样处理」的地方，比如 `merge.cpp` 里「其余组合都不能合并」。

### 4.3 同时分派两个对象

经典的访问者只能双分派：元素一次、访问者一次。如果要按**两个元素**的类型选函数，比如编辑器里逐字修改后，字幕被切成很多碎片，要把相邻的、能合并的元素合起来（两段文字拼成一段、两个粗体合成一个、颜色相同的两段合成一段），经典写法需要再套一层访问者，5 种 × 5 种 = 25 种组合要写一大堆类。

`std::visit` 可以一次传多个 variant，按它们的类型组合选重载：

```cpp
// 《访问者模式》：std::visit 同时分派两个 variant：相邻的两个元素能不能合并，取决于两边各是什么类型
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
struct Italic { Kids kids; };
struct Color { std::uint32_t rgb; Kids kids; };
struct Br {};
struct Node { std::variant<Text, Bold, Italic, Color, Br> v; };

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;   // C++17 要写推导指引，C++20 可以省掉

Kids concat(Kids a, const Kids& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

// 5 种 × 5 种 = 25 种组合，这里只写了 5 个 lambda：4 种能合并的，再加一个兜底
std::optional<Node> merge(const Node& a, const Node& b) {
  return std::visit(overloaded{
      [](const Text& x, const Text& y) -> std::optional<Node> { return Node{Text{x.s + y.s}}; },
      [](const Bold& x, const Bold& y) -> std::optional<Node> { return Node{Bold{concat(x.kids, y.kids)}}; },
      [](const Italic& x, const Italic& y) -> std::optional<Node> { return Node{Italic{concat(x.kids, y.kids)}}; },
      [](const Color& x, const Color& y) -> std::optional<Node> {
        if (x.rgb != y.rgb) return std::nullopt;                  // 颜色不同不能合并
        return Node{Color{x.rgb, concat(x.kids, y.kids)}};
      },
      [](const auto&, const auto&) -> std::optional<Node> { return std::nullopt; },
  }, a.v, b.v);
}

Kids normalize(const Kids& in);
void normalizeKids(Node& n) {        // 带子节点的元素：子节点也要整理
  std::visit(overloaded{[](Text&) {}, [](Br&) {}, [](auto& s) { s.kids = normalize(s.kids); }}, n.v);
}

Kids normalize(const Kids& in) {     // 从左到右，能和前一个合并就合并
  Kids out;
  for (const Node& n : in) {
    if (!out.empty())
      if (auto r = merge(out.back(), n)) {
        out.back() = std::move(*r);
        normalizeKids(out.back());     // 两个 Bold 合并后，里面的两段 Text 也能合并
        continue;
      }
    out.push_back(n);
    normalizeKids(out.back());
  }
  return out;
}

std::string show(const Kids& ks) {   // 用 [b ...] 这样的记号把树打印出来
  std::string s;
  for (auto& k : ks)
    s += std::visit(overloaded{
        [](const Text& t) { return "\"" + t.s + "\""; },
        [](const Bold& b) { return "[b " + show(b.kids) + "]"; },
        [](const Italic& i) { return "[i " + show(i.kids) + "]"; },
        [](const Color& c) { char h[16]; std::snprintf(h, sizeof h, "#%06X", c.rgb); return "[" + std::string(h) + " " + show(c.kids) + "]"; },
        [](const Br&) { return std::string("↵"); },
    }, k.v);
  return s;
}

int count(const Kids& ks) {
  int n = 0;
  for (auto& k : ks) {
    ++n;
    std::visit(overloaded{[](const Text&) {}, [](const Br&) {}, [&](const auto& s) { n += count(s.kids); }}, k.v);
  }
  return n;
}

int main() {
  // 编辑器里逐字修改后，字幕被切成了很多碎片
  Kids cue = {
      {Text{"Hel"}}, {Text{"lo "}},
      {Bold{{{Text{"bi"}}}}}, {Bold{{{Text{"g"}}}}},
      {Text{" "}},
      {Color{0xFF0000, {{Text{"wor"}}}}}, {Color{0xFF0000, {{Text{"ld"}}}}}, {Color{0x0000FF, {{Text{"!"}}}}},
  };
  Kids n = normalize(cue);
  std::printf("整理前 %2d 个节点：%s\n整理后 %2d 个节点：%s\n", count(cue), show(cue).c_str(), count(n), show(n).c_str());
}
```

输出：

```
整理前 13 个节点："Hel""lo "[b "bi"][b "g"]" "[#FF0000 "wor"][#FF0000 "ld"][#0000FF "!"]
整理后  8 个节点："Hello "[b "big"]" "[#FF0000 "world"][#0000FF "!"]
```

25 种组合只写了 5 个 lambda：4 种能合并的，加一个兜底的 `(const auto&, const auto&)`。重载决议会优先选更具体的那个，所以 `Text` + `Text` 不会落进兜底。

### 4.4 四种写法的开销

`bench.cpp` 用四种写法遍历同一批 104 万个元素（5 种元素随机排列），对每个元素做一次「统计」：

1. 单分派虚函数 `x->chars()`（第 1.1 节的写法）；
2. 双分派访问者 `x->accept(v)`；
3. `dynamic_cast` 链（第 1.3 节的写法）；
4. `std::visit`，元素按值连续存放在 `std::vector<variant>` 里。

<figure class="diagram">
<svg viewBox="0 0 660 186" role="img" aria-label="四种写法遍历元素的平均时间，5 种元素随机排列：Clang 上单分派虚函数 5.36、访问者 5.49、dynamic_cast 链 36.41、std::visit 4.28 纳秒；GCC 上分别是 4.68、5.37、18.53、3.32 纳秒"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="138.0" y="30.0" width="22.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="166.8" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5.36 ns</text><rect x="138.0" y="60.0" width="23.3" height="20.0" rx="2" style="fill:var(--accent)"/><text x="167.3" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.49 ns</text><rect x="138.0" y="90.0" width="154.7" height="20.0" rx="2" style="fill:var(--danger)"/><text x="298.7" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">36.41 ns</text><rect x="138.0" y="120.0" width="18.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="162.2" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">4.28 ns</text><text x="398.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="398.0" y="30.0" width="19.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="423.9" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">4.68 ns</text><rect x="398.0" y="60.0" width="22.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="426.8" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.37 ns</text><rect x="398.0" y="90.0" width="78.8" height="20.0" rx="2" style="fill:var(--danger)"/><text x="482.8" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">18.53 ns</text><rect x="398.0" y="120.0" width="14.1" height="20.0" rx="2" style="fill:var(--link)"/><text x="418.1" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">3.32 ns</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">单分派虚函数</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">双分派访问者</text><text x="130.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">dynamic_cast 链</text><text x="130.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::visit</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp，-O2：104 万个元素（5 种随机排列）× 10 遍，7 次取中位数；GCC 跑在 colima 虚拟机里</text></g></svg>
</figure>

随机排列时，前两种和 `std::visit` 差不多，都是 3～6 纳秒：这时的开销主要是**预测失败**，下一个元素是什么类型 CPU 猜不到，间接跳转（虚函数）和 `switch` 跳转都一样要付出代价。访问者比单分派多一次虚调用，只慢了 0.1～0.7 纳秒。`dynamic_cast` 慢得多（Clang 36.41、GCC 18.53 纳秒），它每次都要拿类型信息比较，最坏要比较 5 次。

同样的元素按类型排好序，同类的挨在一起，跳转好预测了：

<figure class="diagram">
<svg viewBox="0 0 660 186" role="img" aria-label="同样的元素按类型排好序后，四种写法的平均时间：Clang 上 0.67、0.74、30.58、0.64 纳秒；GCC 上 0.64、0.65、13.97、0.47 纳秒"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="138.0" y="30.0" width="2.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="146.8" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.67 ns</text><rect x="138.0" y="60.0" width="3.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="147.1" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.74 ns</text><rect x="138.0" y="90.0" width="130.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="274.0" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">30.58 ns</text><rect x="138.0" y="120.0" width="2.7" height="20.0" rx="2" style="fill:var(--link)"/><text x="146.7" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.64 ns</text><text x="398.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="398.0" y="30.0" width="2.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="406.7" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.64 ns</text><rect x="398.0" y="60.0" width="2.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="406.8" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.65 ns</text><rect x="398.0" y="90.0" width="59.4" height="20.0" rx="2" style="fill:var(--danger)"/><text x="463.4" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">13.97 ns</text><rect x="398.0" y="120.0" width="2.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="406.0" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.47 ns</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">单分派虚函数</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">双分派访问者</text><text x="130.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">dynamic_cast 链</text><text x="130.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::visit</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp，-O2：同一批元素按类型排序（同类挨在一起，分支和间接跳转都好预测），其余同上</text></g></svg>
</figure>

前三种都降到 1 纳秒以下，`std::visit` 最快（GCC 0.47 纳秒）；`dynamic_cast` 还是 14～31 纳秒，因为它慢在比较类型本身，不在预测。

<details>
<summary>bench.cpp（统计了堆分配次数，请用 -O2 编译）</summary>

<!-- run: off -->
```cpp
// 《访问者模式》：单分派虚函数、双分派访问者、dynamic_cast 链、std::visit 四种写法遍历同一串元素的开销（请用 -O2 编译）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <variant>
#include <vector>

static long g_allocs = 0;
void* operator new(std::size_t n) { ++g_allocs; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// ---- 写法 1、2、3：元素在堆上，用基类指针保存
struct Visitor;
struct Node {
  virtual ~Node() = default;
  virtual long chars() const = 0;               // 写法 1：操作是元素的虚函数（单分派）
  virtual void accept(Visitor& v) const = 0;    // 写法 2：访问者（双分派）
};
struct Text; struct Bold; struct Italic; struct Color; struct Br;
struct Visitor {
  virtual ~Visitor() = default;
  virtual void visit(const Text&) = 0;
  virtual void visit(const Bold&) = 0;
  virtual void visit(const Italic&) = 0;
  virtual void visit(const Color&) = 0;
  virtual void visit(const Br&) = 0;
};
struct Text : Node {
  std::string s;
  explicit Text(std::string t) : s(std::move(t)) {}
  long chars() const override { return (long)s.size(); }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Bold : Node {
  long chars() const override { return 1; }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Italic : Node {
  long chars() const override { return 2; }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Color : Node {
  unsigned rgb = 0xFF0000;
  long chars() const override { return 3; }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Br : Node {
  long chars() const override { return 4; }
  void accept(Visitor& v) const override { v.visit(*this); }
};
struct Counter : Visitor {
  long n = 0;
  void visit(const Text& t) override { n += (long)t.s.size(); }
  void visit(const Bold&) override { n += 1; }
  void visit(const Italic&) override { n += 2; }
  void visit(const Color&) override { n += 3; }
  void visit(const Br&) override { n += 4; }
};
long castChars(const Node& x) {                 // 写法 3
  if (auto t = dynamic_cast<const Text*>(&x)) return (long)t->s.size();
  if (dynamic_cast<const Bold*>(&x)) return 1;
  if (dynamic_cast<const Italic*>(&x)) return 2;
  if (dynamic_cast<const Color*>(&x)) return 3;
  if (dynamic_cast<const Br*>(&x)) return 4;
  return 0;
}

// ---- 写法 4：元素是 variant，按值连续存放
struct VText { std::string s; };
struct VBold {}; struct VItalic {}; struct VColor { unsigned rgb = 0xFF0000; }; struct VBr {};
using VNode = std::variant<VText, VBold, VItalic, VColor, VBr>;
struct VCounter {
  long operator()(const VText& t) const { return (long)t.s.size(); }
  long operator()(const VBold&) const { return 1; }
  long operator()(const VItalic&) const { return 2; }
  long operator()(const VColor&) const { return 3; }
  long operator()(const VBr&) const { return 4; }
};

using Clock = std::chrono::steady_clock;
template <class F> double median7(F f, int n, long& sink) {
  std::vector<double> t;
  for (int r = 0; r < 7; ++r) {
    auto t0 = Clock::now();
    sink += f();
    t.push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n);
  }
  std::sort(t.begin(), t.end());
  return t[3];
}

struct Result { double t[4]; long heapAllocs, valAllocs; };

Result run(const std::vector<int>& kinds, long& sink) {
  const int N = (int)kinds.size(), PASSES = 10;
  long a0 = g_allocs;
  std::vector<std::unique_ptr<Node>> heap;
  heap.reserve(N);
  for (int k : kinds) {
    switch (k) {
      case 0: heap.push_back(std::make_unique<Text>("word")); break;
      case 1: heap.push_back(std::make_unique<Bold>()); break;
      case 2: heap.push_back(std::make_unique<Italic>()); break;
      case 3: heap.push_back(std::make_unique<Color>()); break;
      default: heap.push_back(std::make_unique<Br>()); break;
    }
  }
  Result r;
  r.heapAllocs = g_allocs - a0;
  a0 = g_allocs;
  std::vector<VNode> vals;
  vals.reserve(N);
  for (int k : kinds) {
    switch (k) {
      case 0: vals.push_back(VText{"word"}); break;
      case 1: vals.push_back(VBold{}); break;
      case 2: vals.push_back(VItalic{}); break;
      case 3: vals.push_back(VColor{}); break;
      default: vals.push_back(VBr{}); break;
    }
  }
  r.valAllocs = g_allocs - a0;
  r.t[0] = median7([&] { long s = 0; for (int p = 0; p < PASSES; ++p) for (auto& x : heap) s += x->chars(); return s; }, N * PASSES, sink);
  r.t[1] = median7([&] { Counter c; for (int p = 0; p < PASSES; ++p) for (auto& x : heap) x->accept(c); return c.n; }, N * PASSES, sink);
  r.t[2] = median7([&] { long s = 0; for (int p = 0; p < PASSES; ++p) for (auto& x : heap) s += castChars(*x); return s; }, N * PASSES, sink);
  r.t[3] = median7([&] { long s = 0; for (int p = 0; p < PASSES; ++p) for (auto& x : vals) s += std::visit(VCounter{}, x); return s; }, N * PASSES, sink);
  return r;
}

int main() {
  const int N = 1 << 20;
  std::mt19937 rng(42);
  std::vector<int> kinds(N);
  for (int& k : kinds) k = (int)(rng() % 5);    // 5 种元素随机排列
  long sink = 0;
  Result rnd = run(kinds, sink);
  std::sort(kinds.begin(), kinds.end());         // 同样的元素，按类型排好序：分支好预测
  Result srt = run(kinds, sink);

  const char* names[4] = {"单分派虚函数 x->chars()", "双分派访问者 x->accept(v)", "dynamic_cast 链", "std::visit（值连续存放）"};
  std::printf("%d 个元素 × 10 遍，7 次取中位数，每个元素平均（随机排列 / 按类型排序）：\n", N);
  for (int i = 0; i < 4; ++i) std::printf("  %5.2f ns / %5.2f ns  %s\n", rnd.t[i], srt.t[i], names[i]);
  std::printf("建树时的堆分配：基类指针 %ld 次，variant %ld 次\n", rnd.heapAllocs, rnd.valAllocs);
  std::printf("sizeof：Text %zu、Br %zu（再加 unique_ptr %zu），variant %zu\n", sizeof(Text), sizeof(Br),
              sizeof(std::unique_ptr<Node>), sizeof(VNode));
  std::printf("（校验和 %ld）\n", sink);
}
```

</details>

结论：**双分派本身的开销可以忽略**，选访问者还是 variant 不用看速度，看第 2.6 节的扩展方向和下一节的取舍；只是别用 `dynamic_cast` 链。

### 4.5 怎么选

| 写法 | 加操作 | 加元素 | 漏改时 | 适合 |
|------|--------|--------|--------|------|
| 操作写成虚函数 | 改每个类 | 新写一个类 | 编译报错（纯虚函数） | 操作少而固定，元素常加（滤镜、解码器） |
| `dynamic_cast` 链 | 新写一个函数 | 新写一个类 | **没有任何提示** | 不推荐 |
| 经典访问者 | 新写一个类 | 改接口和每个访问者 | 编译报错 | 元素固定、操作常加；元素类在库里，用户要在库外加操作 |
| `variant` + `visit` | 新写一个函数对象 | 改类型列表和每个操作 | 编译报错（有兜底分支时没有） | 元素固定、在一处定义；想要值语义、返回值、多分派 |

经典访问者有一个 variant 做不到的地方：元素类可以在库里**编译好**，用户在库外面写新的访问者，只要拿到 `Visitor` 接口就行；variant 的每个操作都要看到全部元素类型的定义。

## 5. 访问者和相似的模式

| 模式 | 关心什么 | 和访问者的区别 |
|------|----------|----------------|
| 组合 | 树形结构，让叶子和容器一样对待 | 组合是**结构**，访问者是在这个结构上加**操作**的办法；两个经常一起用（本文的字幕树就是组合） |
| 迭代器 | 按顺序取出每个元素 | 迭代器只管「下一个」，拿到的都是同一个类型；访问者按元素的具体类型分派到不同的函数 |
| 策略 | 一个算法有几种实现，运行时换 | 策略是单分派：换的只是算法；访问者是双分派：既看算法（访问者），也看元素类型 |
| 命令 | 把一次请求包成对象，可以排队、撤销 | 命令包的是「做什么」，执行时不关心对象结构 |
| 解释器 | 给一种语言定义语法树，并对它求值 | 解释器把求值写在每个节点类里（第 1.1 节的写法）；节点上的操作多了，常常改成访问者 |

## 6. 小结

1. 访问者模式：元素类只提供 `accept(Visitor&)`，每种操作写成一个访问者类，里面给每种元素各写一个 `visit`；加操作不改元素类。
2. 重载按静态类型选，虚函数只按一个对象分派。`accept` 里的 `v.visit(*this)` 先按元素类型、再按访问者类型分派两次，这就是双分派；每个元素类都必须自己写 `accept`。
3. 实测：加一种导出格式，虚函数写法改 7 处，访问者改 1 处；加一种元素，访问者反过来要改 6 处。访问者只是在表达式问题里选了「加操作容易」那一边，元素种类稳定时才划算。
4. 漏改时访问者和 variant 都会编译失败；`dynamic_cast` 链 0 个警告，导出的字幕直接少了一句，而且慢 4～7 倍。
5. `std::variant` + `std::visit`：元素是值、没有 `accept`、能返回结果、能同时分派两个对象；类型列表是封闭的，兜底的 `auto` 分支会让编译器不再提醒漏改。
6. 随机排列时虚函数、访问者、`std::visit` 都在 3～6 纳秒，开销主要是预测失败；双分派多出来的那次虚调用可以忽略。

## 7. 练习

1. **辨析**：下面几个需求，哪些适合用访问者？不适合的用什么？
   - (a) 播放器的滤镜链里有缩放、裁剪、调色、去隔行等几十种滤镜，还会不断加新的；对每个滤镜只做「处理一帧」这一件事；
   - (b) 一个 MP4 解析库把文件解析成 box 树（`ftyp`、`moov`、`trak`、`mdat`……，种类由标准定好），使用者想打印结构、统计每个 track 的大小、检查 box 是否合法，这些需求库的作者事先不知道；
   - (c) 播放列表要按顺序一首首取出来播放；
   - (d) 字幕元素树要导出成第 6 种格式 TTML。
2. **动手**：给 `visitor.cpp` 加一个访问者 `WidthMeasurer`，算出字幕最宽那一行的显示宽度（ASCII 占 1 格，中文占 2 格，`Br` 换行），用来判断字幕会不会超出屏幕。要改元素类吗？「当前这一行已经多宽」存在哪里？
3. **设计**：用 `std::variant` 表示一条视频滤镜链：缩放 `Scale{w, h}`、裁剪 `Crop{w, h, x, y}`、改帧率 `Fps{fps}`、水平翻转 `Hflip{}`。写两个操作：一个拼出 ffmpeg 的 `-vf` 参数（如 `scale=640:360,hflip,fps=12`），一个从 `bbb-10s.mp4` 的 1280×720@24 出发，推算每一步之后的尺寸和帧率，以及每秒一共要处理多少像素。`fps=12` 放在链的开头和末尾，处理的像素差多少？
4. **找问题**：下面的代码给字幕加了「下划线」，`Visitor` 接口加了 `visit(const Underline&)`，`AssExporter` 也实现了，编译 0 个警告。可是导出的下划线变成了粗体。问题在哪？怎么改，才能让以后再也犯不了这个错？

   ```cpp
   // 《访问者模式》练习 4：加了 Underline，访问者接口和每个访问者都改了，编译也通过了，可下划线导出成了粗体
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   struct Text; struct Bold; struct Underline; struct Br;
   struct Visitor {
     virtual ~Visitor() = default;
     virtual void visit(const Text&) = 0;
     virtual void visit(const Bold&) = 0;
     virtual void visit(const Underline&) = 0;
     virtual void visit(const Br&) = 0;
   };
   struct Node {
     virtual ~Node() = default;
     virtual void accept(Visitor& v) const = 0;
   };
   using Children = std::vector<std::unique_ptr<Node>>;
   struct Text : Node {
     std::string s;
     explicit Text(std::string t) : s(std::move(t)) {}
     void accept(Visitor& v) const override { v.visit(*this); }
   };
   struct Bold : Node {
     Children kids;
     void accept(Visitor& v) const override { v.visit(*this); }
     void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
   };
   struct Underline : Bold {};                       // 和 Bold 一样有子节点，直接继承过来
   struct Br : Node { void accept(Visitor& v) const override { v.visit(*this); } };

   struct AssExporter : Visitor {
     std::string out;
     void visit(const Text& t) override { out += t.s; }
     void visit(const Bold& b) override { out += "{\\b1}"; b.visitKids(*this); out += "{\\b0}"; }
     void visit(const Underline& u) override { out += "{\\u1}"; u.visitKids(*this); out += "{\\u0}"; }
     void visit(const Br&) override { out += "\\N"; }
   };

   int main() {
     Children cue;
     auto b = std::make_unique<Bold>();
     b->kids.push_back(std::make_unique<Text>("Hello"));
     auto u = std::make_unique<Underline>();
     u->kids.push_back(std::make_unique<Text>("再见"));
     cue.push_back(std::move(b));
     cue.push_back(std::make_unique<Br>());
     cue.push_back(std::move(u));
     AssExporter a;
     for (auto& n : cue) n->accept(a);
     std::printf("ASS：%s\n", a.out.c_str());
   }
   ```

   输出：

   ```
   ASS：{\b1}Hello{\b0}\N{\b1}再见{\b0}
   ```

<details>
<summary>点击查看答案</summary>

1. (a) **不适合**。滤镜种类常加、操作只有一个，正好是访问者不擅长的方向（加一种滤镜要改每个访问者）。用普通的虚函数（或《策略模式》）：`Filter::process(Frame&)`，新滤镜就是新写一个类。
   (b) **适合**，而且是经典访问者比 variant 更合适的场景：box 的种类由标准固定，库的作者把元素类编译进库里，使用者在库外面写自己的访问者（打印、统计、检查），不用改库。
   (c) **不适合**，用《迭代器模式》。只是按顺序取出同一种东西，不需要按类型分派。
   (d) **适合**，这正是访问者擅长的方向：新写一个 `TtmlExporter`，元素类不动。FFmpeg 的 `ttmlenc.c` 就是新填一组 `ASSCodesCallbacks`。

2. 元素类一行不改，只新写一个访问者。「当前行宽」和「最宽的行」都是这次遍历的状态，放在访问者的成员里；遇到 `Br` 把当前行宽清零。第二行「再见，大千世界！」只有 8 个字，却比第一行 15 个字母还宽：

   ```cpp
   // 《访问者模式》练习 2：加一个访问者，算出字幕最宽那一行的显示宽度（ASCII 占 1 格，中文等宽字符占 2 格）
   #include <algorithm>
   #include <cstdint>
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   struct Text; struct Bold; struct Italic; struct Color; struct Br;
   struct Visitor {
     virtual ~Visitor() = default;
     virtual void visit(const Text&) = 0;
     virtual void visit(const Bold&) = 0;
     virtual void visit(const Italic&) = 0;
     virtual void visit(const Color&) = 0;
     virtual void visit(const Br&) = 0;
   };
   struct Node {
     virtual ~Node() = default;
     virtual void accept(Visitor& v) const = 0;
   };
   using Children = std::vector<std::unique_ptr<Node>>;
   struct Text : Node {
     std::string s;
     explicit Text(std::string t) : s(std::move(t)) {}
     void accept(Visitor& v) const override { v.visit(*this); }
   };
   struct Span : Node {
     Children kids;
     void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
   };
   struct Bold : Span { void accept(Visitor& v) const override { v.visit(*this); } };
   struct Italic : Span { void accept(Visitor& v) const override { v.visit(*this); } };
   struct Color : Span {
     std::uint32_t rgb;
     explicit Color(std::uint32_t c) : rgb(c) {}
     void accept(Visitor& v) const override { v.visit(*this); }
   };
   struct Br : Node { void accept(Visitor& v) const override { v.visit(*this); } };

   // 新的访问者：不用改任何元素类。访问者可以带状态：当前行宽、最宽的行
   struct WidthMeasurer : Visitor {
     int line = 0, widest = 0;
     void visit(const Text& t) override {
       for (unsigned char c : t.s) {
         if ((c & 0xC0) == 0x80) continue;          // UTF-8 的后续字节
         line += c < 0x80 ? 1 : 2;                  // 简化：非 ASCII 都按全角算
       }
       widest = std::max(widest, line);
     }
     void visit(const Bold& b) override { b.visitKids(*this); }
     void visit(const Italic& i) override { i.visitKids(*this); }
     void visit(const Color& c) override { c.visitKids(*this); }
     void visit(const Br&) override { line = 0; }   // 换行：从 0 重新数
   };

   std::unique_ptr<Node> text(std::string s) { return std::make_unique<Text>(std::move(s)); }
   template <class S, class... K>
   std::unique_ptr<Node> span(S s, K... kids) {
     auto p = std::make_unique<S>(std::move(s));
     (p->kids.push_back(std::move(kids)), ...);
     return p;
   }

   int main() {
     Children cue;     // Hello <b>big</b> <i><font color=红>world</font></i> ↵ 再见，<b>大</b>千世界！
     cue.push_back(text("Hello "));
     cue.push_back(span(Bold{}, text("big")));
     cue.push_back(text(" "));
     cue.push_back(span(Italic{}, span(Color{0xFF0000}, text("world"))));
     cue.push_back(std::make_unique<Br>());
     cue.push_back(text("再见，"));
     cue.push_back(span(Bold{}, text("大")));
     cue.push_back(text("千世界！"));
     WidthMeasurer w;
     for (auto& n : cue) n->accept(w);
     std::printf("最宽一行占 %d 格\n", w.widest);
   }
   ```

   输出：

   ```
   最宽一行占 16 格
   ```

3. 滤镜是普通结构体，放进 `std::variant`；拼字符串的操作没有状态，用 `overloaded` 加几个 lambda；推算尺寸要一路带着当前的尺寸和帧率，写成带成员的函数对象，`std::visit(e, f)` 按引用传进去，状态就留在 `e` 里：

   ```cpp
   // 《访问者模式》练习 3：用 std::variant 表示一条滤镜链，两种操作：拼出 ffmpeg 的 -vf 参数、算出每一步之后的画面尺寸和每秒要处理的像素
   #include <cstdio>
   #include <string>
   #include <variant>
   #include <vector>

   struct Scale { int w, h; };
   struct Crop { int w, h, x, y; };
   struct Fps { int fps; };
   struct Hflip {};
   using Filter = std::variant<Scale, Crop, Fps, Hflip>;

   template <class... F> struct overloaded : F... { using F::operator()...; };
   template <class... F> overloaded(F...) -> overloaded<F...>;

   // 操作 1：拼成 ffmpeg 的滤镜描述，返回字符串
   std::string describe(const std::vector<Filter>& chain) {
     std::string s;
     for (auto& f : chain) {
       if (!s.empty()) s += ',';
       s += std::visit(overloaded{
           [](const Scale& x) { return "scale=" + std::to_string(x.w) + ":" + std::to_string(x.h); },
           [](const Crop& x) { return "crop=" + std::to_string(x.w) + ":" + std::to_string(x.h) + ":" +
                                      std::to_string(x.x) + ":" + std::to_string(x.y); },
           [](const Fps& x) { return "fps=" + std::to_string(x.fps); },
           [](const Hflip&) { return std::string("hflip"); },
       }, f);
     }
     return s;
   }

   // 操作 2：沿着链往下推算画面格式；带状态，所以写成一个函数对象
   struct Shape { int w, h, fps; };
   struct Estimator {
     Shape cur;
     double pixels = 0;                      // 每秒一共要处理（读入）多少像素
     void operator()(const Scale& x) { pixels += 1.0 * cur.w * cur.h * cur.fps; cur.w = x.w; cur.h = x.h; }
     void operator()(const Crop& x) { pixels += 1.0 * cur.w * cur.h * cur.fps; cur.w = x.w; cur.h = x.h; }
     void operator()(const Fps& x) { cur.fps = x.fps; }      // 丢帧几乎不花时间
     void operator()(const Hflip&) { pixels += 1.0 * cur.w * cur.h * cur.fps; }
   };

   int main() {
     std::vector<Filter> a = {Scale{640, 360}, Hflip{}, Fps{12}};
     std::vector<Filter> b = {Fps{12}, Scale{640, 360}, Hflip{}};   // 同样的效果，先丢帧
     for (auto* chain : {&a, &b}) {
       Estimator e{{1280, 720, 24}};         // bbb-10s.mp4：1280×720，24 fps
       for (auto& f : *chain) std::visit(e, f);
       std::printf("-vf %-32s → %d×%d@%d，每秒处理 %.1f 百万像素\n", describe(*chain).c_str(), e.cur.w, e.cur.h,
                   e.cur.fps, e.pixels / 1e6);
     }
   }
   ```

   输出：

   ```
   -vf scale=640:360,hflip,fps=12       → 640×360@12，每秒处理 27.6 百万像素
   -vf fps=12,scale=640:360,hflip       → 640×360@12，每秒处理 13.8 百万像素
   ```

   结果一样，先丢帧的那条处理的像素少一半：后面的缩放、翻转每秒只处理 12 帧。这类「分析一条链」的操作正适合用访问者，加一种分析只要多写一个函数对象。

4. `Underline` 继承了 `Bold`，没有自己写 `accept`，于是继承了 `Bold::accept`，里面的 `v.visit(*this)` 中 `*this` 的静态类型是 `const Bold&`，选中的是 `visit(const Bold&)`。这正是第 2.2 节说的：`accept` 必须在**每个**具体元素类里写一遍，写在基类里就失去了第二次分派的意义。编译器不会报错，因为 `Underline` 从 `Bold` 继承来的 `accept` 是完整的。

   改法：用 CRTP 基类 `Visitable<Derived, Base>` 自动生成 `accept`，`static_cast<const Derived&>(*this)` 让静态类型永远是具体的元素类；共用的部分（子节点）放进一个不能被访问的基类 `Span`；具体元素类标成 `final`，谁再写 `struct Underline : Bold` 就直接编译失败：

   ```cpp
   // 《访问者模式》练习 4 的答案：accept() 由 CRTP 基类自动生成，具体元素类标成 final
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   struct Text; struct Bold; struct Underline; struct Br;
   struct Visitor {
     virtual ~Visitor() = default;
     virtual void visit(const Text&) = 0;
     virtual void visit(const Bold&) = 0;
     virtual void visit(const Underline&) = 0;
     virtual void visit(const Br&) = 0;
   };
   struct Node {
     virtual ~Node() = default;
     virtual void accept(Visitor& v) const = 0;
   };
   using Children = std::vector<std::unique_ptr<Node>>;

   // 每个具体元素都从 Visitable<自己> 派生，accept() 不用手写，也就不会忘
   template <class Derived, class Base = Node>
   struct Visitable : Base {
     void accept(Visitor& v) const override { v.visit(static_cast<const Derived&>(*this)); }
   };
   struct Span : Node {                              // 共用的部分放进一个不能直接访问的基类
     Children kids;
     void visitKids(Visitor& v) const { for (auto& k : kids) k->accept(v); }
   };

   struct Text final : Visitable<Text> {
     std::string s;
     explicit Text(std::string t) : s(std::move(t)) {}
   };
   struct Bold final : Visitable<Bold, Span> {};
   struct Underline final : Visitable<Underline, Span> {};   // 写成 : Bold 会编译失败（Bold 是 final）
   struct Br final : Visitable<Br> {};

   struct AssExporter : Visitor {
     std::string out;
     void visit(const Text& t) override { out += t.s; }
     void visit(const Bold& b) override { out += "{\\b1}"; b.visitKids(*this); out += "{\\b0}"; }
     void visit(const Underline& u) override { out += "{\\u1}"; u.visitKids(*this); out += "{\\u0}"; }
     void visit(const Br&) override { out += "\\N"; }
   };

   int main() {
     Children cue;
     auto b = std::make_unique<Bold>();
     b->kids.push_back(std::make_unique<Text>("Hello"));
     auto u = std::make_unique<Underline>();
     u->kids.push_back(std::make_unique<Text>("再见"));
     cue.push_back(std::move(b));
     cue.push_back(std::make_unique<Br>());
     cue.push_back(std::move(u));
     AssExporter a;
     for (auto& n : cue) n->accept(a);
     std::printf("ASS：%s\n", a.out.c_str());
   }
   ```

   输出：

   ```
   ASS：{\b1}Hello{\b0}\N{\u1}再见{\u0}
   ```

   用 `std::variant` 的写法根本不会有这个问题：没有 `accept`，`Underline` 和 `Bold` 是两个不相关的类型。

</details>
