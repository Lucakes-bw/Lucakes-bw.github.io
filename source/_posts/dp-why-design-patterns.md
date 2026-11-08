---
title: 为什么需要设计模式
date: 2026-11-08 09:30:00
updated: 2026-11-08 09:30:00
categories:
  - 设计模式
  - 基础与设计原则
tags:
  - 设计模式
  - 面向对象
  - 重构
order: 1
description: 一个导出报表的小程序，支持 CSV 和 JSON；再加一种 HTML 格式，if-else 写法要改三个函数，漏改一处编译器一声不吭，HTML 就少了结尾标签；换成多态的写法，只要新增一个类，漏写一个函数直接编译不过。可是再加一个「新操作」，局面正好反过来，多态版要改四处。本文用这几次实测说明设计模式到底在解决什么问题，再介绍 GoF 的 23 种模式怎么分类、什么时候不该用模式。
top_group_list: true
disableNunjucks: true
viz: [dp-why]
---

> **本文目标**：通过一个不断加需求的小程序，体会「改一处、漏一处」是怎么发生的；理解设计模式解决的核心问题是**把会变化的部分隔离开**；知道没有一种结构对所有变化都方便；了解 GoF 23 种模式的分类，以及什么时候不该用模式。
>
> **前置知识**：C/C++ 专栏的《多态与虚函数》《智能指针》；会用 `std::map` 和 lambda 更好。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/01-why/` 目录，文中每个版本的改动量都是用 `diff` 统计出来的。

## 1. 一个不断加需求的程序

假设你在写一个销售系统，要把销售记录导出成文件。最开始只要 CSV，后来又要 JSON，于是写成了下面这样：每个导出步骤（表头、每一行、结尾）是一个函数，函数里用 `if-else` 区分格式。

```cpp
#include <iostream>
#include <string>
#include <vector>

struct Sale {
    std::string region, product;
    int qty;
    double price;
};

void printHeader(const std::string& fmt) {
    if (fmt == "csv") {
        std::cout << "region,product,qty,price\n";
    } else if (fmt == "json") {
        std::cout << "[\n";
    }
}

void printRow(const std::string& fmt, const Sale& s, bool last) {
    if (fmt == "csv") {
        std::cout << s.region << ',' << s.product << ',' << s.qty << ',' << s.price << '\n';
    } else if (fmt == "json") {
        std::cout << "  {\"region\": \"" << s.region << "\", \"product\": \"" << s.product
                  << "\", \"qty\": " << s.qty << ", \"price\": " << s.price << "}" << (last ? "" : ",") << '\n';
    }
}

void printFooter(const std::string& fmt) {
    if (fmt == "json") {
        std::cout << "]\n";
    }
}

void report(const std::string& fmt, const std::vector<Sale>& sales) {
    printHeader(fmt);
    for (size_t i = 0; i < sales.size(); ++i) printRow(fmt, sales[i], i + 1 == sales.size());
    printFooter(fmt);
}

int main() {
    std::vector<Sale> sales = {{"east", "keyboard", 12, 199}, {"south", "mouse", 30, 59.5}};
    for (const char* fmt : {"csv", "json"}) {
        std::cout << "== " << fmt << " ==\n";
        report(fmt, sales);
    }
}
```

```text
== csv ==
region,product,qty,price
east,keyboard,12,199
south,mouse,30,59.5
== json ==
[
  {"region": "east", "product": "keyboard", "qty": 12, "price": 199},
  {"region": "south", "product": "mouse", "qty": 30, "price": 59.5}
]
```

这段代码没有任何问题：结构清楚，能用，也容易读。如果需求到此为止，就该这么写。

问题出在**需求不会到此为止**。

## 2. 加一种格式：要改三个地方

新需求来了：再支持一种格式 HTML，导出成网页里的表格。要改哪里？表头、每一行、结尾，三个函数里各有一串 `if-else`，每一串都要加一个分支：

<!-- run: off -->
```cpp
void printHeader(const std::string& fmt) {
    // ...
    } else if (fmt == "html") {                                   // ① 新增
        std::cout << "<table>\n  <tr><th>region</th><th>product</th><th>qty</th><th>price</th></tr>\n";
    }
}

void printRow(const std::string& fmt, const Sale& s, bool last) {
    // ...
    } else if (fmt == "html") {                                   // ② 新增
        std::cout << "  <tr><td>" << s.region << "</td><td>" << s.product << "</td><td>" << s.qty
                  << "</td><td>" << s.price << "</td></tr>\n";
    }
}

void printFooter(const std::string& fmt) {
    // ...
    } else if (fmt == "html") {                                   // ③ 新增
        std::cout << "</table>\n";
    }
}
```

关于 HTML 的知识被拆成了三块，分别塞进了三个函数。下面的演示里，左边是这种写法，右边是第 4 节要讲的另一种写法。按 ▶ 播放，看同一个需求在两边分别要改哪些地方：

<div class="dsviz" data-viz="dp-why"></div>

这个程序只有三个函数，改三个地方还不算难。真实的项目里，「按格式分支」的代码可能散落在导出、预览、文件名、MIME 类型、权限检查等十几个地方，每加一种格式都要把它们全部找出来改一遍。这种「一个需求，要在很多地方各改一点」的情况有个名字，叫**散弹式修改**（shotgun surgery）。

## 3. 漏改一处会怎样

改的地方一多，就难免漏掉。假设加 HTML 时，`printHeader` 和 `printRow` 都改了，唯独忘了 `printFooter`：

```cpp
#include <iostream>
#include <string>
#include <vector>

struct Sale {
    std::string region, product;
    int qty;
    double price;
};

void printHeader(const std::string& fmt) {
    if (fmt == "csv") {
        std::cout << "region,product,qty,price\n";
    } else if (fmt == "json") {
        std::cout << "[\n";
    } else if (fmt == "html") {
        std::cout << "<table>\n  <tr><th>region</th><th>product</th><th>qty</th><th>price</th></tr>\n";
    }
}

void printRow(const std::string& fmt, const Sale& s, bool last) {
    if (fmt == "csv") {
        std::cout << s.region << ',' << s.product << ',' << s.qty << ',' << s.price << '\n';
    } else if (fmt == "json") {
        std::cout << "  {\"region\": \"" << s.region << "\", \"product\": \"" << s.product
                  << "\", \"qty\": " << s.qty << ", \"price\": " << s.price << "}" << (last ? "" : ",") << '\n';
    } else if (fmt == "html") {
        std::cout << "  <tr><td>" << s.region << "</td><td>" << s.product << "</td><td>" << s.qty
                  << "</td><td>" << s.price << "</td></tr>\n";
    }
}

void printFooter(const std::string& fmt) {
    if (fmt == "json") {
        std::cout << "]\n";
    }
}

void report(const std::string& fmt, const std::vector<Sale>& sales) {
    printHeader(fmt);
    for (size_t i = 0; i < sales.size(); ++i) printRow(fmt, sales[i], i + 1 == sales.size());
    printFooter(fmt);
}

int main() {
    std::vector<Sale> sales = {{"east", "keyboard", 12, 199}, {"south", "mouse", 30, 59.5}};
    for (const char* fmt : {"csv", "json", "html"}) {
        std::cout << "== " << fmt << " ==\n";
        report(fmt, sales);
    }
}
```

运行结果的最后几行：

```text
== html ==
<table>
  <tr><th>region</th><th>product</th><th>qty</th><th>price</th></tr>
  <tr><td>east</td><td>keyboard</td><td>12</td><td>199</td></tr>
  <tr><td>south</td><td>mouse</td><td>30</td><td>59.5</td></tr>
```

少了结尾的 `</table>`。更糟糕的是：

- **编译器什么也没说**。我在 Clang 21 和 GCC 13 上都用 `-Wall -Wextra` 编译了这段代码，一条警告都没有。对编译器来说，`fmt == "html"` 只是一个字符串比较，它不知道「格式」这个概念，更不知道你漏了哪个。
- **程序照常运行**，不崩溃，不报错。浏览器对 HTML 很宽容，少一个结尾标签多半也能显示，这个 bug 可能要等到有人把输出拼进更大的页面、表格样式乱掉时才会被发现。

问题不在于程序员粗心，而在于代码的组织方式：**同一个概念（HTML 格式）的代码被拆散在多处，修改时必须靠人记住所有地方**。

## 4. 换一种切分方式

把思路反过来：不按「操作」切分，而是按「格式」切分。每种格式写成一个类，回答同样的三个问题（开头写什么、每行写什么、结尾写什么）：

```uml
label: 多态版的结构：Formatter 接口有 header、row、footer 三个纯虚函数；CsvFormatter、JsonFormatter、HtmlFormatter 各自实现；report 只依赖 Formatter 接口
classes:
  - { id: report, name: 'report()', x: 20, y: 30, w: 150 }
  - { id: Formatter, stereo: interface, x: 290, y: 16, w: 250, ops: ['*+ header() : void', '*+ row(s: const Sale&, last: bool) : void', '*+ footer() : void'] }
  - { id: Csv, name: CsvFormatter, x: 180, y: 200, w: 130 }
  - { id: Json, name: JsonFormatter, x: 350, y: 200, w: 130 }
  - { id: Html, name: HtmlFormatter, x: 520, y: 200, w: 130, hl: new }
rels:
  - { from: report, to: Formatter, type: depend, fdx: 3 }
  - { from: Csv, to: Formatter, type: realize, mid: 170 }
  - { from: Json, to: Formatter, type: realize, mid: 170 }
  - { from: Html, to: Formatter, type: realize, mid: 170 }
```

完整代码（已经加上了 HTML）：

```cpp
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Sale {
    std::string region, product;
    int qty;
    double price;
};

// 每种格式要回答的三个问题：开头写什么、每行写什么、结尾写什么
class Formatter {
public:
    virtual ~Formatter() = default;
    virtual void header() const = 0;
    virtual void row(const Sale& s, bool last) const = 0;
    virtual void footer() const = 0;
};

class CsvFormatter : public Formatter {
public:
    void header() const override { std::cout << "region,product,qty,price\n"; }
    void row(const Sale& s, bool) const override {
        std::cout << s.region << ',' << s.product << ',' << s.qty << ',' << s.price << '\n';
    }
    void footer() const override {}
};

class JsonFormatter : public Formatter {
public:
    void header() const override { std::cout << "[\n"; }
    void row(const Sale& s, bool last) const override {
        std::cout << "  {\"region\": \"" << s.region << "\", \"product\": \"" << s.product
                  << "\", \"qty\": " << s.qty << ", \"price\": " << s.price << "}" << (last ? "" : ",") << '\n';
    }
    void footer() const override { std::cout << "]\n"; }
};

class HtmlFormatter : public Formatter {
public:
    void header() const override {
        std::cout << "<table>\n  <tr><th>region</th><th>product</th><th>qty</th><th>price</th></tr>\n";
    }
    void row(const Sale& s, bool) const override {
        std::cout << "  <tr><td>" << s.region << "</td><td>" << s.product << "</td><td>" << s.qty
                  << "</td><td>" << s.price << "</td></tr>\n";
    }
    void footer() const override { std::cout << "</table>\n"; }
};

// 格式名 → 创建函数
std::unique_ptr<Formatter> makeFormatter(const std::string& fmt) {
    static const std::map<std::string, std::function<std::unique_ptr<Formatter>()>> table = {
        {"csv", [] { return std::make_unique<CsvFormatter>(); }},
        {"json", [] { return std::make_unique<JsonFormatter>(); }},
        {"html", [] { return std::make_unique<HtmlFormatter>(); }},
    };
    auto it = table.find(fmt);
    return it == table.end() ? nullptr : it->second();
}

void report(const Formatter& f, const std::vector<Sale>& sales) {
    f.header();
    for (size_t i = 0; i < sales.size(); ++i) f.row(sales[i], i + 1 == sales.size());
    f.footer();
}

int main() {
    std::vector<Sale> sales = {{"east", "keyboard", 12, 199}, {"south", "mouse", 30, 59.5}};
    for (const char* fmt : {"csv", "json", "html"}) {
        std::cout << "== " << fmt << " ==\n";
        report(*makeFormatter(fmt), sales);
    }
}
```

输出和 if-else 版完全相同（`html` 部分带着结尾的 `</table>`）。和第 1 节的代码相比，加 HTML 这个需求变成了：

1. **新写**一个 `HtmlFormatter` 类，HTML 的三段代码都在这里，读一个类就能看到 HTML 格式的全部；
2. 在 `makeFormatter` 的表里**加一行** `{"html", ...}`。

已有的 `CsvFormatter`、`JsonFormatter` 和 `report` 一个字都没改。不改已有代码，也就不可能把已有的功能改坏。

再试试漏写 `footer`。因为 `footer` 是纯虚函数，`HtmlFormatter` 没有实现它，就仍然是抽象类，不能创建对象，编译直接失败（完整代码在 `report_oop_ce.cpp`）：

```text
error: allocating an object of abstract class type 'HtmlFormatter'
note: unimplemented pure virtual method 'footer' in 'HtmlFormatter'
```

**编译器替你记住了「每种格式必须回答哪几个问题」**。这是多态版最大的好处：第 3 节里靠人记的事情，变成了由类型系统检查的事情。

## 5. 实测：两次需求，各改了多少

我把两种写法的每个版本都放在 `projects/dp-lab/01-why/` 里，用 `diff` 统计了每次加需求时的改动。除了第 2、4 节的「加一种格式 HTML」，我又加了一个方向不同的需求：**每种格式都要提供自己的文件扩展名**（`.csv`、`.json`、`.html`），也就是一个新的「操作」 `extension()`。

| 需求 | 写法 | 改动的位置 | 其中修改已有的函数 / 类 |
| --- | --- | --- | --- |
| 加一种格式 HTML | if-else | `printHeader`、`printRow`、`printFooter` 各一处 | **3 个函数** |
| 加一种格式 HTML | 多态 | 新增 `HtmlFormatter`（12 行），表里加 1 行 | **0 个** |
| 加一个操作 `extension()` | if-else | 新增 `extension` 函数（6 行） | **0 个** |
| 加一个操作 `extension()` | 多态 | 接口加 1 个纯虚函数，3 个类各加 1 个实现 | **接口 + 3 个类** |

（表中没有算 `main` 里调用处的改动，两种写法都一样。）

<figure class="diagram">
<svg viewBox="0 0 680 250" role="img" aria-label="实测：加一种格式时，if-else 版改了 3 个已有函数，多态版改了 0 个已有类；加一个操作时，if-else 版改了 0 个已有函数，多态版改了接口和 3 个已有类共 4 处"><g style="font-family:var(--mono)"><line x1="60" y1="190" x2="660" y2="190" style="stroke:var(--border-2);stroke-width:1.2"/><text x="50" y="194" text-anchor="end" style="fill:var(--muted);font-size:11px">0</text><text x="50" y="160" text-anchor="end" style="fill:var(--muted);font-size:11px">1</text><line x1="60" y1="156" x2="660" y2="156" style="stroke:var(--border-2);stroke-width:.6;stroke-dasharray:2 4"/><text x="50" y="126" text-anchor="end" style="fill:var(--muted);font-size:11px">2</text><line x1="60" y1="122" x2="660" y2="122" style="stroke:var(--border-2);stroke-width:.6;stroke-dasharray:2 4"/><text x="50" y="92" text-anchor="end" style="fill:var(--muted);font-size:11px">3</text><line x1="60" y1="88" x2="660" y2="88" style="stroke:var(--border-2);stroke-width:.6;stroke-dasharray:2 4"/><text x="50" y="58" text-anchor="end" style="fill:var(--muted);font-size:11px">4</text><line x1="60" y1="54" x2="660" y2="54" style="stroke:var(--border-2);stroke-width:.6;stroke-dasharray:2 4"/><rect x="130" y="88" width="64" height="102" rx="3" style="fill:var(--link)"/><text x="162" y="80" text-anchor="middle" style="fill:var(--link);font-size:14px;font-weight:700">3</text><text x="162" y="208" text-anchor="middle" style="fill:var(--fg);font-size:12px">if-else</text><rect x="220" y="188" width="64" height="2" style="fill:var(--accent)"/><text x="252" y="182" text-anchor="middle" style="fill:var(--accent);font-size:14px;font-weight:700">0</text><text x="252" y="208" text-anchor="middle" style="fill:var(--fg);font-size:12px">多态</text><text x="207" y="232" text-anchor="middle" style="fill:var(--fg);font-size:13px;font-weight:700">加一种格式 HTML</text><rect x="420" y="188" width="64" height="2" style="fill:var(--link)"/><text x="452" y="182" text-anchor="middle" style="fill:var(--link);font-size:14px;font-weight:700">0</text><text x="452" y="208" text-anchor="middle" style="fill:var(--fg);font-size:12px">if-else</text><rect x="510" y="54" width="64" height="136" rx="3" style="fill:var(--accent)"/><text x="542" y="46" text-anchor="middle" style="fill:var(--accent);font-size:14px;font-weight:700">4</text><text x="542" y="208" text-anchor="middle" style="fill:var(--fg);font-size:12px">多态</text><text x="497" y="232" text-anchor="middle" style="fill:var(--fg);font-size:13px;font-weight:700">加一个操作 extension()</text><text x="60" y="20" text-anchor="start" style="fill:var(--muted);font-size:11px">修改已有函数 / 类的处数（越少越好，新增的代码不算）</text></g></svg>
</figure>

第二个需求的结果正好反过来：if-else 版只要新写一个函数，多态版却要改接口和每一个类。

## 6. 没有对所有变化都方便的结构

为什么会这样？把「格式」和「操作」画成一张表就清楚了：每个格子是「某种格式的某个操作」的一段代码。if-else 版把**每一列**写成一个函数，多态版把**每一行**写成一个类：

<figure class="diagram">
<svg viewBox="0 0 720 300" role="img" aria-label="格式乘以操作的表格：if-else 版把每一列（一个操作）写成一个函数，多态版把每一行（一种格式）写成一个类。加一行新格式 XML，if-else 版要改 3 个函数，多态版新增 1 个类；加一列新操作 ext，if-else 版新增 1 个函数，多态版要改 3 个类"><g style="font-family:var(--mono)"><text x="70" y="20" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">if-else 版：一列 = 一个函数</text><text x="430" y="20" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">多态版：一行 = 一个类</text><text x="116.0" y="48" text-anchor="middle" style="fill:var(--muted);font-size:11px">header</text><text x="172.0" y="48" text-anchor="middle" style="fill:var(--muted);font-size:11px">row</text><text x="228.0" y="48" text-anchor="middle" style="fill:var(--muted);font-size:11px">footer</text><text x="284.0" y="48" text-anchor="middle" style="fill:var(--warn);font-size:11px">ext</text><text x="82" y="72" text-anchor="end" style="fill:var(--fg);font-size:12px">CSV</text><text x="82" y="100" text-anchor="end" style="fill:var(--fg);font-size:12px">JSON</text><text x="82" y="128" text-anchor="end" style="fill:var(--fg);font-size:12px">HTML</text><text x="82" y="156" text-anchor="end" style="fill:var(--link);font-size:12px">XML</text><rect x="88" y="54" width="56" height="84" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.2"/><rect x="144" y="54" width="56" height="84" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.2"/><rect x="200" y="54" width="56" height="84" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.2"/><rect x="90" y="56" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="146" y="56" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="202" y="56" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="258" y="56" width="52" height="24" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="90" y="84" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="146" y="84" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="202" y="84" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="258" y="84" width="52" height="24" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="90" y="112" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="146" y="112" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="202" y="112" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="258" y="112" width="52" height="24" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="90" y="140" width="52" height="24" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="146" y="140" width="52" height="24" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="202" y="140" width="52" height="24" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.5;stroke-dasharray:4 3"/><text x="476.0" y="48" text-anchor="middle" style="fill:var(--muted);font-size:11px">header</text><text x="532.0" y="48" text-anchor="middle" style="fill:var(--muted);font-size:11px">row</text><text x="588.0" y="48" text-anchor="middle" style="fill:var(--muted);font-size:11px">footer</text><text x="644.0" y="48" text-anchor="middle" style="fill:var(--warn);font-size:11px">ext</text><text x="442" y="72" text-anchor="end" style="fill:var(--fg);font-size:12px">CSV</text><text x="442" y="100" text-anchor="end" style="fill:var(--fg);font-size:12px">JSON</text><text x="442" y="128" text-anchor="end" style="fill:var(--fg);font-size:12px">HTML</text><text x="442" y="156" text-anchor="end" style="fill:var(--link);font-size:12px">XML</text><rect x="448" y="54" width="168" height="28" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.2"/><rect x="448" y="82" width="168" height="28" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.2"/><rect x="448" y="110" width="168" height="28" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.2"/><rect x="450" y="56" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="506" y="56" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="562" y="56" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="618" y="56" width="52" height="24" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="450" y="84" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="506" y="84" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="562" y="84" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="618" y="84" width="52" height="24" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="450" y="112" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="506" y="112" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="562" y="112" width="52" height="24" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1"/><rect x="618" y="112" width="52" height="24" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="450" y="140" width="52" height="24" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="506" y="140" width="52" height="24" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.5;stroke-dasharray:4 3"/><rect x="562" y="140" width="52" height="24" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.5;stroke-dasharray:4 3"/><text x="70" y="194" text-anchor="start" style="fill:var(--link);font-size:12px">加一行 XML（新格式）：改 3 个已有函数</text><text x="70" y="214" text-anchor="start" style="fill:var(--warn);font-size:12px">加一列 ext（新操作）：新增 1 个函数</text><text x="430" y="194" text-anchor="start" style="fill:var(--link);font-size:12px">加一行 XML（新格式）：新增 1 个类</text><text x="430" y="214" text-anchor="start" style="fill:var(--warn);font-size:12px">加一列 ext（新操作）：改 3 个已有类</text><text x="360" y="248" text-anchor="middle" style="fill:var(--muted);font-size:12px">把经常变化的方向放在「新增」上：哪种变化更常见，就按那个方向切分代码</text></g></svg>
</figure>

- 加一种**格式**是加一行：多态版新增一个类就行，if-else 版要把每一列（每个函数）都改一遍。
- 加一个**操作**是加一列：if-else 版新增一个函数就行，多态版要把每一行（每个类）都改一遍。

这在编程语言理论里叫做**表达式问题**（expression problem）：同时让「加类型」和「加操作」都只需要新增代码，是很难的。后面要讲的**访问者模式**，就是在面向对象语言里把多态版翻转成「方便加操作」的一种做法。

所以，设计的第一步不是选模式，而是**判断哪个方向的变化更常见**：

- 导出格式会越来越多，每种格式要做的事情相对固定：按格式切分，用多态版。
- 格式基本不变，但经常要加新的处理（统计、校验、转换……）：按操作切分，if-else（或者 `switch`）反而更合适。

然后把变化的方向安排在「新增代码」上，而不是「修改已有代码」上。这句话几乎是所有设计模式的共同出发点。

## 7. 什么是设计模式

上面多态版的写法其实就是一个现成的模式：**策略模式**（把一族可以互相替换的做法封装成类，让使用者在运行时选择）。`makeFormatter` 里的那张表，则是**工厂**的一种简化写法。

**设计模式**（design pattern）就是这样一些**被反复验证过的、针对常见设计问题的解决方案**。「模式」这个词来自建筑师 Christopher Alexander：他发现好的建筑里反复出现某些解决方案，于是把它们整理成一套可以复用的「模式语言」。1994 年，Erich Gamma、Richard Helm、Ralph Johnson、John Vlissides 四位作者把这个思路用到了面向对象软件上，出版了《设计模式：可复用面向对象软件的基础》，收录了 23 种模式。四位作者常被称为 **GoF**（Gang of Four，四人组），这 23 种模式也就叫 GoF 模式。

### 7.1 一个模式的四个要素

GoF 用四个要素描述一个模式。拿本文的例子来说：

| 要素 | 含义 | 本文的例子（策略模式） |
| --- | --- | --- |
| 名称 | 一个词就能说清楚的名字，方便交流 | 策略（Strategy） |
| 问题 | 什么情况下用它 | 同一件事有多种做法，代码里出现了按类型选择做法的分支 |
| 解决方案 | 由哪些类组成、各自负责什么、怎么协作 | 一个接口 `Formatter`，每种做法一个实现类，使用者只依赖接口 |
| 效果 | 带来什么好处，付出什么代价 | 加新做法只需新增类；但类的数量变多了，加新操作要改每个类 |

**名称**这一项经常被低估。团队里说一句「这里用策略模式」，大家就知道代码长什么样、哪里会变化、新增功能该放在哪；不用模式名，就得画图、讲半天。设计模式首先是一套**共同的词汇**。

### 7.2 23 种模式的分类

GoF 按两个维度给模式分类。**目的**：创建型关心对象怎么创建，结构型关心类和对象怎么组合，行为型关心对象之间怎么分工和通信。**作用范围**：作用于类的模式用继承在编译期确定关系，作用于对象的模式用组合在运行时确定关系。大部分模式都作用于对象，这也呼应了下面要讲的「优先使用组合」。

<figure class="diagram">
<svg viewBox="0 0 724 423" role="img" aria-label="GoF 的 23 种设计模式按目的分为创建型、结构型、行为型，按作用范围分为作用于类和作用于对象；点击模式名跳到速查页"><g style="font-family:var(--mono)"><text x="194.0" y="26" text-anchor="middle" style="fill:var(--link);font-size:14px;font-weight:700">创建型</text><text x="400.0" y="26" text-anchor="middle" style="fill:var(--warn);font-size:14px;font-weight:700">结构型</text><text x="606.0" y="26" text-anchor="middle" style="fill:var(--accent);font-size:14px;font-weight:700">行为型</text><rect x="10" y="44" width="704" height="74" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1"/><text x="24" y="77.0" text-anchor="start" style="fill:var(--muted);font-size:11px">作用于</text><text x="24" y="93.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">类</text><a href="/column/design-patterns/cheatsheet/#p-factory-method"><rect x="96" y="52" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--link);stroke-width:1.3"/><text x="194.0" y="68" text-anchor="middle" style="fill:var(--fg);font-size:12px">工厂方法</text></a><a href="/column/design-patterns/cheatsheet/#p-adapter"><rect x="302" y="52" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.3"/><text x="400.0" y="68" text-anchor="middle" style="fill:var(--fg);font-size:12px">适配器（类）</text></a><a href="/column/design-patterns/cheatsheet/#p-interpreter"><rect x="508" y="52" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="68" text-anchor="middle" style="fill:var(--fg);font-size:12px">解释器</text></a><a href="/column/design-patterns/cheatsheet/#p-template-method"><rect x="508" y="81" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="97" text-anchor="middle" style="fill:var(--fg);font-size:12px">模板方法</text></a><rect x="10" y="128" width="704" height="277" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1"/><text x="24" y="262.5" text-anchor="start" style="fill:var(--muted);font-size:11px">作用于</text><text x="24" y="278.5" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">对象</text><a href="/column/design-patterns/cheatsheet/#p-abstract-factory"><rect x="96" y="136" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--link);stroke-width:1.3"/><text x="194.0" y="152" text-anchor="middle" style="fill:var(--fg);font-size:12px">抽象工厂</text></a><a href="/column/design-patterns/cheatsheet/#p-builder"><rect x="96" y="165" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--link);stroke-width:1.3"/><text x="194.0" y="181" text-anchor="middle" style="fill:var(--fg);font-size:12px">建造者</text></a><a href="/column/design-patterns/cheatsheet/#p-prototype"><rect x="96" y="194" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--link);stroke-width:1.3"/><text x="194.0" y="210" text-anchor="middle" style="fill:var(--fg);font-size:12px">原型</text></a><a href="/column/design-patterns/cheatsheet/#p-singleton"><rect x="96" y="223" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--link);stroke-width:1.3"/><text x="194.0" y="239" text-anchor="middle" style="fill:var(--fg);font-size:12px">单例</text></a><a href="/column/design-patterns/cheatsheet/#p-adapter"><rect x="302" y="136" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.3"/><text x="400.0" y="152" text-anchor="middle" style="fill:var(--fg);font-size:12px">适配器（对象）</text></a><a href="/column/design-patterns/cheatsheet/#p-bridge"><rect x="302" y="165" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.3"/><text x="400.0" y="181" text-anchor="middle" style="fill:var(--fg);font-size:12px">桥接</text></a><a href="/column/design-patterns/cheatsheet/#p-composite"><rect x="302" y="194" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.3"/><text x="400.0" y="210" text-anchor="middle" style="fill:var(--fg);font-size:12px">组合</text></a><a href="/column/design-patterns/cheatsheet/#p-decorator"><rect x="302" y="223" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.3"/><text x="400.0" y="239" text-anchor="middle" style="fill:var(--fg);font-size:12px">装饰器</text></a><a href="/column/design-patterns/cheatsheet/#p-facade"><rect x="302" y="252" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.3"/><text x="400.0" y="268" text-anchor="middle" style="fill:var(--fg);font-size:12px">外观</text></a><a href="/column/design-patterns/cheatsheet/#p-flyweight"><rect x="302" y="281" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.3"/><text x="400.0" y="297" text-anchor="middle" style="fill:var(--fg);font-size:12px">享元</text></a><a href="/column/design-patterns/cheatsheet/#p-proxy"><rect x="302" y="310" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.3"/><text x="400.0" y="326" text-anchor="middle" style="fill:var(--fg);font-size:12px">代理</text></a><a href="/column/design-patterns/cheatsheet/#p-chain"><rect x="508" y="136" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="152" text-anchor="middle" style="fill:var(--fg);font-size:12px">责任链</text></a><a href="/column/design-patterns/cheatsheet/#p-command"><rect x="508" y="165" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="181" text-anchor="middle" style="fill:var(--fg);font-size:12px">命令</text></a><a href="/column/design-patterns/cheatsheet/#p-iterator"><rect x="508" y="194" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="210" text-anchor="middle" style="fill:var(--fg);font-size:12px">迭代器</text></a><a href="/column/design-patterns/cheatsheet/#p-mediator"><rect x="508" y="223" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="239" text-anchor="middle" style="fill:var(--fg);font-size:12px">中介者</text></a><a href="/column/design-patterns/cheatsheet/#p-memento"><rect x="508" y="252" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="268" text-anchor="middle" style="fill:var(--fg);font-size:12px">备忘录</text></a><a href="/column/design-patterns/cheatsheet/#p-observer"><rect x="508" y="281" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="297" text-anchor="middle" style="fill:var(--fg);font-size:12px">观察者</text></a><a href="/column/design-patterns/cheatsheet/#p-state"><rect x="508" y="310" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="326" text-anchor="middle" style="fill:var(--fg);font-size:12px">状态</text></a><a href="/column/design-patterns/cheatsheet/#p-strategy"><rect x="508" y="339" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="355" text-anchor="middle" style="fill:var(--fg);font-size:12px">策略</text></a><a href="/column/design-patterns/cheatsheet/#p-visitor"><rect x="508" y="368" width="196" height="24" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.3"/><text x="606.0" y="384" text-anchor="middle" style="fill:var(--fg);font-size:12px">访问者</text></a></g></svg>
</figure>

点击图中的模式名可以跳到《[设计模式速查](/column/design-patterns/cheatsheet/)》，那里每个模式有一句话的意图和一张类图。专栏会按创建型、结构型、行为型的顺序逐个展开，再加上一章 C++ 特有的惯用法（RAII、Pimpl、CRTP、类型擦除）。

### 7.3 贯穿所有模式的两条原则

GoF 在书的开头提出了两条原则，后面的 23 种模式几乎都是它们的具体应用：

1. **针对接口编程，而不是针对实现编程**。`report` 只认识 `Formatter` 接口，不知道有哪些具体格式；所以加格式时它一行都不用改。如果 `report` 的参数写成 `const HtmlFormatter&`，这个好处就没了。
2. **优先使用对象组合，而不是类继承**。继承在编译期就把关系定死了，组合可以在运行时替换。多态版的 `report` 接收一个 `Formatter` 对象，而不是让 `Report` 类派生出 `CsvReport`、`JsonReport`，所以同一份报表可以随时换一种格式输出。

再加上第 6 节得出的那条：**找出会变化的部分，把它和不变的部分隔离开**。这三条会在下一章《SOLID 原则》里展开。

## 8. 什么时候不该用设计模式

学了模式以后，最常见的问题不是不会用，而是**用得太多**。下面这种代码在真实项目里并不少见：只为了打印一行 CSV，写了接口、工厂、工厂的工厂：

<!-- run: off -->
```cpp
class IFormatterFactoryProvider {                 // 工厂的提供者
public:
    virtual std::unique_ptr<IFormatterFactory> getFactory() = 0;
};
class DefaultFormatterFactoryProvider : public IFormatterFactoryProvider { /* ... */ };
class CsvFormatterFactory : public IFormatterFactory { /* ... */ };
// ……再加上配置文件、注册宏，最后只是为了输出 "east,keyboard,12,199"
```

每一层抽象都有代价：更多的类、更多的跳转、更难读懂的调用路径。只有当它换来的灵活性**真的用得上**时，这个代价才值得。几条实用的判断标准：

- **先写最简单的版本**。第 1 节的 if-else 代码，在只有两种格式的时候就是最好的写法。
- **三次法则**（rule of three）：第一次直接写；第二次遇到类似的需求，忍一忍，复制一份；第三次再出现，说明这个方向确实会变化，这时再重构成模式。
- **YAGNI**（You Aren't Gonna Need It，你不会需要它）：不为「以后可能会有」的需求提前设计。猜对了省一点事，猜错了留下一堆没人敢删的抽象。
- **模式是重构的目标，不是起点**。先有能用的代码和让人难受的地方（第 3 节那种漏改），再用模式消除它。本专栏的每一篇都按这个顺序写：先写一版越改越难改的代码，再重构。

## 9. 本专栏怎么读

每个模式一篇，结构基本相同：

1. **问题**：一个具体的需求，先用最直接的方式写出来，再加需求，直到它变得难改；
2. **重构**：用模式重构，画出类图，说明每个类的职责；
3. **现代 C++ 写法**：同一个模式用智能指针、`std::function`、`std::variant`、模板等写出更简洁的版本，并实测性能差别；
4. **什么时候不用**：这个模式的代价和常见的误用；
5. **练习**：文末的练习带答案；部分模式在刷题里还有「重构挑战」题，比如 [运费计算](/problems/refactor-shipping-strategy/)：给你一份能运行但结构很差的代码，加一个新需求并重构，提交时除了跑用例，还会检查代码结构。

读模式之前，先看下一篇《UML 类图》，学会读类图，后面每一篇都会用到。

## 10. 小结

- 代码的组织方式决定了**哪种修改容易**。按操作切分（if-else），加格式要改多处；按格式切分（多态），加格式只需新增一个类
- 同一个概念的代码散落在多处时，修改要靠人记住所有地方，漏改一处编译器也发现不了（实测：Clang 和 GCC 开 `-Wall -Wextra` 都没有警告）；用纯虚函数，漏写的函数会变成编译错误
- 没有对所有变化都方便的结构：多态版加格式改 0 个已有类，加操作却要改接口和 3 个类，这就是表达式问题
- 设计的关键是判断**哪个方向的变化更常见**，让那个方向的修改只需要新增代码
- 设计模式是针对常见设计问题的、被反复验证过的解决方案，也是团队之间的共同词汇；GoF 的 23 种模式分为创建型、结构型、行为型
- 两条核心原则：针对接口编程；优先使用组合而不是继承
- 不要过度设计：先写最简单的版本，同一个方向的变化出现第三次时再重构成模式

## 11. 练习

1. 对下面三个需求，分别说出 if-else 版和多态版要修改几个**已有的**函数或类：
   ① 再加一种格式 XML；② CSV 的分隔符从逗号改成分号；③ 每种格式都要提供自己的 MIME 类型（`text/csv`、`application/json`、`text/html`）。

2. 第 3 节漏改 `printFooter` 时编译器没有任何提示，因为格式只是一个字符串。如果不改成多态，有没有办法让编译器帮忙发现漏掉的分支？提示：把格式改成 `enum class`，并把 `if-else` 换成 `switch`。动手试一试，看编译器输出了什么，再想想为什么不能写 `default:`。

3. 在第 4 节的多态版里加一种 Markdown 表格格式，格式名是 `"md"`，输出：

   ```text
   | region | product | qty | price |
   |---|---|---|---|
   | east | keyboard | 12 | 199 |
   | south | mouse | 30 | 59.5 |
   ```

   写完数一数：你修改了几个已有的类？

4. 下面几种情况，你会怎么写？
   ① 一个只跑一次的数据迁移脚本，导出一份 CSV；
   ② 产品经理说「以后可能要支持很多格式」，但现在只有 CSV，也没有具体计划；
   ③ 这是你第三次往导出代码的 `if-else` 里加格式了，而且上次加 JSON 时漏改过一处。

<details>
<summary>点击查看答案</summary>

1. ① XML 是新格式（加一行）：if-else 版改 3 个函数（`printHeader`、`printRow`、`printFooter`），多态版改 0 个（新增 `XmlFormatter`，表里加一行）。
   ② 只改 CSV 这一种格式的细节：if-else 版改 2 个函数（`printHeader` 和 `printRow` 的 csv 分支），多态版改 1 个类（`CsvFormatter`），而且改动集中在一处。
   ③ MIME 类型是新操作（加一列）：if-else 版改 0 个（新增一个 `mimeType` 函数），多态版要改接口和 3 个类。

2. 代码在 `projects/dp-lab/01-why/report_enum_bug.cpp`：

   <!-- run: off -->
   ```cpp
   enum class Format { Csv, Json, Html };

   void printFooter(Format fmt) {
       switch (fmt) {           // 漏掉了 Format::Html，也不写 default
       case Format::Csv:
           break;
       case Format::Json:
           std::cout << "]\n";
           break;
       }
   }
   ```

   Clang 21 和 GCC 13 开 `-Wall` 都会给出同样的警告：

   ```text
   warning: enumeration value 'Html' not handled in switch [-Wswitch]
   ```

   编译器知道 `Format` 一共有哪几个值，所以能检查 `switch` 有没有覆盖全部。一旦写了 `default:`，所有没列出的值都算「处理了」，这个检查就失效了。所以对枚举做 `switch` 时，**不要写 `default`**，并且把 `-Werror=switch` 加进编译选项，让漏掉的分支变成编译错误。
   这个办法能发现漏改，但修改的位置还是分散在各个函数里：它让散弹式修改更安全，却没有让它变少。

3. 新增一个类，并在表里加一行（完整代码在 `report_oop_md.cpp`）：

   <!-- run: off -->
   ```cpp
   class MarkdownFormatter : public Formatter {
   public:
       void header() const override {
           std::cout << "| region | product | qty | price |\n|---|---|---|---|\n";
       }
       void row(const Sale& s, bool) const override {
           std::cout << "| " << s.region << " | " << s.product << " | " << s.qty << " | " << s.price << " |\n";
       }
       void footer() const override {}
   };

   // makeFormatter 的表里加一行：
   //     {"md", [] { return std::make_unique<MarkdownFormatter>(); }},
   ```

   修改了 0 个已有的类：`CsvFormatter`、`JsonFormatter`、`HtmlFormatter`、`report` 都没动，唯一改动的已有代码是表里的一行。注意 Markdown 没有结尾，`footer` 什么也不做，但仍然要写出来，表示「我考虑过了，这里确实不需要」。

4. ① 直接写，一个函数搞定，不需要任何抽象，脚本跑完就删了。
   ② 先只写 CSV，代码保持简单（YAGNI）。「以后可能」不是具体的需求，现在设计的接口多半和真正的需求对不上。
   ③ 该重构了：同一个方向的变化已经出现第三次，而且已经因为散弹式修改出过 bug（三次法则）。按格式切分，重构成第 4 节的多态版。

</details>
