---
title: 解释器模式
date: 2026-11-20 10:30:00
updated: 2026-11-20 10:30:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 解释器模式
  - AST
  - 递归下降
  - std::variant
order: 11
description: 视频素材库要按条件筛选片段：起初用 FilterSpec 结构体堆 minWidth、minFps、codec、onlySdr 等字段，所有条件默认 AND 拼在一起；一遇到「(width >= 1920 and fps >= 60) or codec == "hevc"」这种任意嵌套的 and / or / not，结构体字段彻底表达不出来。改成解释器模式：用 6 行 BNF 定义过滤语言文法，每条规则对应一个 Expr 节点（终结符 CmpExpr / HdrExpr，非终结符 AndExpr / OrExpr / NotExpr），再用 30 行递归下降解析器把字符串解析成 AST，对每条素材反复 eval()。再用 std::variant + std::visit 重写 AST，同时支持 eval() 求值与 format() 规范打印。10 万条视频元数据实测（-O2，7 轮中位数）：硬编码 0.963 ms，解析一次的虚函数 AST 2.437 ms、variant AST 2.894 ms，而每次重解析字符串要 22.980 ms（慢近 10 倍）。最后讨论文法膨胀与何时不该手写解释器、FFmpeg libavutil/eval.c 等真实工程案例，并对比组合、访问者、策略与责任链。
top_group_list: true
disableNunjucks: true
viz: [dp-interpreter]
---

> **本文目标**：理解解释器模式（定义简单语言的文法，把句子解析成抽象语法树 AST 再求值）；手写一套视频素材过滤 DSL 的终结符、非终结符与极简递归下降解析器；掌握「解析一次、求值多次」的性能分水岭与现代 C++ `std::variant` AST 写法；认清文法膨胀的边界，知道何时该用成熟工具而不是手写解释器；分清解释器与组合、访问者、策略、责任链。
>
> **前置知识**：《组合模式》《访问者模式》（用过 `std::variant` + `std::visit`）。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整示例与性能压测代码见 `projects/dp-lab/28-interpreter/`。

## 1. 问题：过滤条件组合起来，结构体字段写不下

剪辑软件的素材库里存着成千上万条视频片段，每条片段有分辨率 `width`、帧率 `fps`、时长 `dur`（秒）、编码 `codec` 和是否 HDR（`hdr`）。产品经理提了第一个需求：筛选素材。最顺手的写法是定义一个 `FilterSpec` 结构体，把常见条件做成字段，全用「与（AND）」拼起来：

```cpp
// 《解释器模式》：问题版——用 FilterSpec 结构体堆布尔/阈值开关，遇到 or/not 嵌套组合就表达不出来
#include <cstdio>
#include <string>
#include <vector>

struct Clip {
  std::string name;
  int width, fps, dur;
  std::string codec;
  bool hdr;
};

struct FilterSpec {
  int minWidth = 0;
  int minFps = 0;
  int maxDur = 0;       // 0 表示不限
  std::string codec;    // 空串表示不限
  bool onlySdr = false;

  bool match(const Clip& c) const {
    if (minWidth > 0 && c.width < minWidth) return false;
    if (minFps > 0 && c.fps < minFps) return false;
    if (maxDur > 0 && c.dur >= maxDur) return false;
    if (!codec.empty() && c.codec != codec) return false;
    if (onlySdr && c.hdr) return false;
    return true;
  }
};

int main() {
  std::vector<Clip> clips = {
      {"intro_4k.mov",   3840, 60,  18, "hevc", true},
      {"interview.mp4",  1920, 30, 120, "h264", false},
      {"broll_slow.mp4", 1920, 60,  12, "h264", false},
      {"drone_hdr.mkv",  2560, 30,  25, "hevc", true},
      {"proxy_720.mp4",  1280, 24,   9, "h264", false},
  };

  // 需求 1：not hdr and dur < 30 —— 刚好全是 and，FilterSpec 勉强能写
  FilterSpec s1; s1.onlySdr = true; s1.maxDur = 30;
  std::printf("需求 1 (not hdr and dur < 30):");
  for (const auto& c : clips) if (s1.match(c)) std::printf(" %s", c.name.c_str());
  std::printf("\n");

  // 需求 2：(width >= 1920 and fps >= 60) or codec == "hevc" —— 单个 FilterSpec 表达不了，只能拆成两条手写 ||
  FilterSpec a; a.minWidth = 1920; a.minFps = 60;
  FilterSpec b; b.codec = "hevc";
  std::printf("需求 2 ((width>=1920 and fps>=60) or codec==\"hevc\"):");
  for (const auto& c : clips) if (a.match(c) || b.match(c)) std::printf(" %s", c.name.c_str());
  std::printf("\n");
}
```

```text
需求 1 (not hdr and dur < 30): broll_slow.mp4 proxy_720.mp4
需求 2 ((width>=1920 and fps>=60) or codec=="hevc"): intro_4k.mov broll_slow.mp4 drone_hdr.mkv
```

需求 1 刚好是纯 `and`，`FilterSpec` 还能应付；一到需求 2，`(width >= 1920 and fps >= 60) or codec == "hevc"` 就没法装进一个 `FilterSpec` 里，只能在外层拆成两条再手写 `||`。要是用户在搜索框里输入：

- `not (codec == "h264" and dur > 60)`
- `(width >= 2560 or hdr) and fps >= 30`
- 甚至把自定义筛选规则存进工程文件、下次打开直接复用

往 `FilterSpec` 里继续加 `orCodec`、`excludeCodec`、`negateAll` 之类字段只会让结构体失控，而且永远覆盖不了任意深度的括号嵌套。问题根源很清楚：**过滤条件本质上是一门微型领域语言（DSL）的句子，不是一张扁平的开关表**。

## 2. 解释器模式

### 2.1 意图、BNF 文法与四个角色

> **GoF**：给定一个语言，定义它的文法的一种表示，并定义一个解释器，这个解释器使用该表示来解释语言中的句子。

大白话：**先写清楚这门小语言的文法规则，再把每条规则变成一个类，把输入字符串拼成一棵抽象语法树（AST），最后从根节点调 `eval(context)` 递归求值。**

视频素材过滤语言的文法只需 6 行 BNF（按优先级从低到高分层：`or` < `and` < `not` < 括号与原子条件）：

```text
expr  := or_e
or_e  := and_e ( "or"  and_e )*
and_e := not_e ( "and" not_e )*
not_e := "not" not_e | atom
atom  := "(" expr ")" | "hdr" | cmp
cmp   := field ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) value
```

文法层次天然决定了优先级：`not` 绑得最紧，`and` 次之，`or` 最松，括号可以任意提升优先级。解释器模式把这棵树分成 4 个角色：

| 角色 | 在素材过滤 DSL 里 | 职责 |
| --- | --- | --- |
| **AbstractExpression**（抽象表达式） | `Expr` | 声明统一的求值接口 `eval(const Clip&) const` |
| **TerminalExpression**（终结符表达式） | `HdrExpr`、`CmpExpr` | 语法树的叶子，直接从 `Context` 取字段判断真假，不再包含子表达式 |
| **NonterminalExpression**（非终结符表达式） | `NotExpr`、`AndExpr`、`OrExpr` | 语法树的内部节点，持有子 `Expr`，递归调用子节点并组合结果 |
| **Context**（上下文） | `Clip` | 求值时外部传入的数据环境；同一棵 AST 换不同的 `Clip` 就能反复求值 |

注意：**GoF 解释器模式本身只负责「AST 怎么表示和求值」，并不强行规定怎么把字符串变成 AST**——但在工程里通常配一个极简的递归下降解析器（每条 BNF 规则正好对应一个 `parseXxx()` 函数），让用户直接写字符串查询。

### 2.2 用解释器重构素材过滤 DSL

```cpp
// 《解释器模式》：经典实现——Expr 抽象表达式 + 终结符/非终结符 + 极简递归下降解析器
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct Clip {
  std::string name;
  int width, fps, dur;
  std::string codec;
  bool hdr;
};

struct Expr { virtual ~Expr() = default; virtual bool eval(const Clip&) const = 0; };
using ExprPtr = std::unique_ptr<Expr>;

struct HdrExpr : Expr { bool eval(const Clip& c) const override { return c.hdr; } };

struct CmpExpr : Expr {                                   // 终结符：字段比较
  std::string field, op, sval; int ival = 0;
  CmpExpr(std::string f, std::string o, std::string v) : field(std::move(f)), op(std::move(o)), sval(std::move(v)) {
    if (field != "codec") ival = std::stoi(sval);
  }
  bool eval(const Clip& c) const override {
    if (field == "codec") return op == "==" ? c.codec == sval : c.codec != sval;
    int lhs = (field == "width") ? c.width : (field == "fps") ? c.fps : c.dur;
    if (op == "==") return lhs == ival; if (op == "!=") return lhs != ival;
    if (op == "<")  return lhs <  ival; if (op == "<=") return lhs <= ival;
    if (op == ">")  return lhs >  ival; return lhs >= ival;
  }
};

struct NotExpr : Expr {                                   // 非终结符：逻辑非
  ExprPtr sub; explicit NotExpr(ExprPtr s) : sub(std::move(s)) {}
  bool eval(const Clip& c) const override { return !sub->eval(c); }
};
struct AndExpr : Expr {                                   // 非终结符：逻辑与（短路）
  ExprPtr l, r; AndExpr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
  bool eval(const Clip& c) const override { return l->eval(c) && r->eval(c); }
};
struct OrExpr : Expr {                                    // 非终结符：逻辑或（短路）
  ExprPtr l, r; OrExpr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
  bool eval(const Clip& c) const override { return l->eval(c) || r->eval(c); }
};

struct Parser {
  std::vector<std::string> tok; size_t pos = 0;
  explicit Parser(const std::string& src) {
    for (size_t i = 0; i < src.size(); ) {
      if (std::isspace((unsigned char)src[i])) { ++i; continue; }
      if (src[i] == '(' || src[i] == ')') { tok.emplace_back(1, src[i++]); continue; }
      if (src[i] == '"') { size_t j = src.find('"', ++i); tok.push_back(src.substr(i, j - i)); i = j + 1; continue; }
      if (std::strchr("=!<>", src[i])) {
        size_t j = i + 1; if (j < src.size() && src[j] == '=') ++j;
        tok.push_back(src.substr(i, j - i)); i = j; continue;
      }
      size_t j = i; while (j < src.size() && (std::isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
      tok.push_back(src.substr(i, j - i)); i = j;
    }
  }
  bool match(const char* s) { return pos < tok.size() && tok[pos] == s ? (++pos, true) : false; }
  ExprPtr parseOr()  { auto e = parseAnd(); while (match("or"))  e = std::make_unique<OrExpr>(std::move(e), parseAnd()); return e; }
  ExprPtr parseAnd() { auto e = parseNot(); while (match("and")) e = std::make_unique<AndExpr>(std::move(e), parseNot()); return e; }
  ExprPtr parseNot() { return match("not") ? std::make_unique<NotExpr>(parseNot()) : parseAtom(); }
  ExprPtr parseAtom() {
    if (match("(")) { auto e = parseOr(); match(")"); return e; }
    if (match("hdr")) return std::make_unique<HdrExpr>();
    std::string f = tok[pos++], op = tok[pos++], v = tok[pos++];
    return std::make_unique<CmpExpr>(std::move(f), std::move(op), std::move(v));
  }
};

int main() {
  std::vector<Clip> clips = {
      {"intro_4k.mov",   3840, 60,  18, "hevc", true},
      {"interview.mp4",  1920, 30, 120, "h264", false},
      {"broll_slow.mp4", 1920, 60,  12, "h264", false},
      {"drone_hdr.mkv",  2560, 30,  25, "hevc", true},
      {"proxy_720.mp4",  1280, 24,   9, "h264", false},
  };
  for (const char* q : {"(width >= 1920 and fps >= 60) or codec == \"hevc\"", "not hdr and dur < 30"}) {
    auto ast = Parser(q).parseOr();
    std::printf("查询 %s ->", q);
    for (const auto& c : clips) if (ast->eval(c)) std::printf(" %s", c.name.c_str());
    std::printf("\n");
  }
}
```

```text
查询 (width >= 1920 and fps >= 60) or codec == "hevc" -> intro_4k.mov broll_slow.mp4 drone_hdr.mkv
查询 not hdr and dur < 30 -> broll_slow.mp4 proxy_720.mp4
```

仔细看 `Parser` 里那 4 行递归下降：`parseOr()` 调 `parseAnd()`，`parseAnd()` 调 `parseNot()`，`parseNot()` 调 `parseAtom()`，`parseAtom()` 遇到 `(` 又折回 `parseOr()`——**BNF 怎么写，代码就怎么写**，任何深度的 `and / or / not / ()` 嵌套都能直接解析并求值。而且 `AndExpr::eval` 与 `OrExpr::eval` 直接复用 C++ 的 `&&` / `||`，天然支持**短路求值**。

> [!NOTE]
> 上面的 `Parser` 为聚焦核心省去了语法错误检查；实际落地时，解析用户输入应检查括号闭合、未知字段名、未闭合引号以及最大递归深度（防止恶意嵌套 `((((...))))` 打爆调用栈），出错时返回 `std::optional` 或错误位置，而不是直接越界访问。

### 2.3 类图、时序图与演示

```uml
label: 解释器模式的类图：Expr 是抽象表达式接口，声明 eval(Clip)；终结符 HdrExpr 和 CmpExpr 实现它；非终结符 NotExpr、AndExpr、OrExpr 实现它并聚合子 Expr；Parser 根据字符串构造整棵 AST，Client 对每条 Clip 调用根节点的 eval
classes:
  - { id: Expr, stereo: interface, x: 240, y: 20, w: 200, ops: ['*+ eval(c : Clip) : bool'] }
  - { id: Clip, x: 520, y: 20, w: 150, attrs: ['+ width, fps, dur', '+ codec, hdr'] }
  - { id: HdrExpr, x: 10, y: 170, w: 110, ops: ['+ eval(c)'] }
  - { id: CmpExpr, x: 135, y: 170, w: 135, attrs: ['+ field, op, val'], ops: ['+ eval(c)'] }
  - { id: NotExpr, x: 285, y: 170, w: 120, attrs: ['- sub : Expr'], ops: ['+ eval(c)'] }
  - { id: AndExpr, x: 420, y: 170, w: 120, attrs: ['- l, r : Expr'], ops: ['+ eval(c)'] }
  - { id: OrExpr, x: 555, y: 170, w: 120, attrs: ['- l, r : Expr'], ops: ['+ eval(c)'] }
rels:
  - { from: HdrExpr, to: Expr, type: realize, mid: 130 }
  - { from: CmpExpr, to: Expr, type: realize, mid: 130 }
  - { from: NotExpr, to: Expr, type: realize, mid: 130 }
  - { from: AndExpr, to: Expr, type: realize, mid: 130 }
  - { from: OrExpr, to: Expr, type: realize, mid: 130 }
  - { from: Expr, to: Clip, type: depend, label: 'Context', points: [[440, 52], [520, 52]] }
notes:
  - { x: 15, y: 270, w: 255, text: ['终结符（叶子）：直接读 Clip 字段', '不再递归调用子表达式'] }
  - { x: 295, y: 270, w: 375, text: ['非终结符（内部节点）：持有子 Expr 指针（组合结构）', '递归求值并按逻辑运算符汇总（支持 && / || 短路）'] }
```

下面以 `drone_hdr.mkv`（`2560×30fps, hevc`）对 `(width >= 1920 and fps >= 60) or codec == "hevc"` 求值为例，看调用如何从根节点一路下探再自底向上汇总：

```uml
label: 时序图：对 drone_hdr.mkv 求值时，OrExpr 先调左子树 AndExpr；AndExpr 先调 width>=1920 得 true，再调 fps>=60 得 false，于是 AndExpr 返回 false；OrExpr 未短路，继续调右子树 codec=="hevc" 得 true，最终返回 true
participants:
  - { id: cli, name: '调用方', x: 15, w: 72 }
  - { id: or, name: 'OrExpr', x: 125, w: 86 }
  - { id: and, name: 'AndExpr', x: 250, w: 92 }
  - { id: w, name: 'width>=1920', x: 380, w: 112 }
  - { id: f, name: 'fps>=60', x: 515, w: 92 }
  - { id: c, name: 'codec=="hevc"', x: 625, w: 120 }
messages:
  - { from: cli, to: or, text: 'eval(drone)', n: 1 }
  - { from: or, to: and, text: 'l->eval(drone)', n: 2 }
  - { from: and, to: w, text: 'l->eval(drone)', n: 3 }
  - { from: w, to: and, text: 'true (2560>=1920)', type: reply }
  - { from: and, to: f, text: 'r->eval(drone)', n: 4 }
  - { from: f, to: and, text: 'false (30>=60)', type: reply }
  - { from: and, to: or, text: 'false', type: reply }
  - { from: or, to: c, text: 'r->eval(drone)', n: 5, note: '左枝为 false，继续查右枝' }
  - { from: c, to: or, text: 'true ("hevc")', type: reply }
  - { from: or, to: cli, text: 'true（命中）', type: reply }
```

点击下面演示里的不同素材，可以单步观察 `or` 短路、`and` 短路和右枝兜底三种求值路径：

<div class="dsviz" data-viz="dp-interpreter"></div>

## 3. 深入解释器：解析摊薄、文法膨胀与真实工程

### 3.1 解析一次，求值多次

很多人对解释器的第一印象是「慢」，但慢通常出在**把解析放进了循环里**。

一条过滤查询只输入一次，却要筛几万甚至几十万条素材。只要在循环外 `auto ast = Parser(q).parseOr();` **解析一次**（把词法切分、`std::stoi`、字段/运算符映射成枚举全部在建树时做完），循环内只跑纯内存指针/索引求值，解析成本就会被几十万条记录完全摊薄。第 4.2 节的实测会看到：**每条都重解析比解析一次慢近 10 倍**。

这和正则库 `std::regex` / RE2 要求「先 `compile` 成自动机，再反复 `match`」、数据库要求「先 `prepare` SQL，再反复 `execute`」是完全一样的道理。

### 3.2 文法膨胀：何时不要手写解释器

解释器模式是 23 种 GoF 模式里**适用面最窄**的一个——因为每加一条文法规则，往往就要多一个类、改一处解析函数；如果文法很快膨胀出几十条规则（算术优先级、函数调用、局部变量、循环、类型检查、报错行号列号），手写类层次很快就会变成难以维护的泥潭。

工程上的分界线非常清晰：

| 复杂度 | 典型需求 | 建议方案 |
| --- | --- | --- |
| **微型 DSL**（5～15 条规则） | 筛选条件、权限规则、路由匹配、简单公式 | 手写递归下降 + 解释器（几十到两百行，零外部依赖） |
| **中等文法**（几十条规则，需完整报错） | 配置语言、查询语言、模板引擎 | 用 **ANTLR / Bison+Flex / Lemon / PEGTL** 生成解析器 |
| **图灵完备脚本**（变量、函数、循环、闭包） | 游戏逻辑、插件脚本、复杂业务规则 | 直接嵌入 **Lua / QuickJS / Python**，或把筛选交给 **SQLite** |

出现下面任一信号，就说明该停手换工具了：
- 需要给终端用户返回精确到**行号、列号与修复建议**的语法报错；
- 运算符优先级超过 7～8 层，或需要引入局部变量、函数定义、循环控制流；
- 表达式来自不可信的外部输入，需要沙箱隔离、内存上限与执行超时控制。

### 3.3 真实工程里的解释器

- **正则表达式引擎**：把 `^clip_[0-9]+\.(mp4|mov)$` 编译成 NFA/DFA 或字节码，反复匹配字符串。
- **SQL `WHERE` 子句**：数据库把 `WHERE width >= 1920 AND fps >= 60` 解析成表达式树，在扫描每一行时求值（配合索引下推和常量折叠）。
- **FFmpeg `libavutil/eval.c`**：FFmpeg 滤镜里常见的 `scale=iw/2:ih/2`、`fade=alpha=if(lt(t,1),t,1)`，底层正是「解析一次、逐帧解释」的标准三步：

<!-- run: off -->
```c
AVExpr *expr = NULL;
const char *vars[] = { "t", "n", NULL };
av_expr_parse(&expr, "if(lt(t,1), t, 1)", vars, NULL, NULL, NULL, NULL, 0, NULL); // 初始化时解析一次
for (int n = 0; n < total_frames; ++n) {
  double vals[] = { n / 60.0, (double)n };
  double alpha = av_expr_eval(expr, vals, NULL);                                  // 每帧传入 Context 求值
}
av_expr_free(expr);
```

FFmpeg 没有在每帧重新解析字符串，而是把表达式编译成 `AVExpr` 树，在 `filter_frame` 热路径里反复传 `vals`（就是 `Context`）求值，既给了用户极高的表达灵活性，又把开销压在纳秒级。

## 4. 现代 C++ 写法与性能实测

### 4.1 `std::variant` AST + `std::visit`：解释器与访问者配合

经典解释器把 `eval()` 写进每个节点类里，如果以后既要 `eval()` 求值，又要 `format()` 把 AST 还原成规范表达式（用来做查询缓存 key、日志审计或前端回显），还要 `validate()` 做字段校验，往每个节点类塞满虚函数就会重演《访问者模式》里的膨胀。

现代 C++ 可以用 `std::variant` 承载所有 AST 节点类型，把「数据结构」和「对树的各种解释/遍历操作」彻底分开：

```cpp
// 《解释器模式》：现代 C++——std::variant 表示 AST + std::visit 同时支持求值与规范打印
#include <cstdio>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct Clip { std::string name; int width, fps, dur; std::string codec; bool hdr; };

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Hdr {};
struct IntCmp   { std::string field, op; int val; };
struct CodecCmp { std::string op, val; };
struct Not      { ExprPtr sub; };
struct And      { ExprPtr l, r; };
struct Or       { ExprPtr l, r; };
struct Expr { std::variant<Hdr, IntCmp, CodecCmp, Not, And, Or> v; };

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

bool eval(const Expr& e, const Clip& c) {
  return std::visit(overloaded{
      [&](const Hdr&) { return c.hdr; },
      [&](const IntCmp& x) {
        int lhs = (x.field == "width") ? c.width : (x.field == "fps") ? c.fps : c.dur;
        if (x.op == "==") return lhs == x.val; if (x.op == "!=") return lhs != x.val;
        if (x.op == "<")  return lhs <  x.val; if (x.op == "<=") return lhs <= x.val;
        if (x.op == ">")  return lhs >  x.val; return lhs >= x.val;
      },
      [&](const CodecCmp& x) { return x.op == "==" ? c.codec == x.val : c.codec != x.val; },
      [&](const Not& x) { return !eval(*x.sub, c); },
      [&](const And& x) { return eval(*x.l, c) && eval(*x.r, c); },
      [&](const Or&  x) { return eval(*x.l, c) || eval(*x.r, c); },
  }, e.v);
}

std::string format(const Expr& e) {
  return std::visit(overloaded{
      [](const Hdr&) { return std::string("hdr"); },
      [](const IntCmp& x) { return x.field + " " + x.op + " " + std::to_string(x.val); },
      [](const CodecCmp& x) { return "codec " + x.op + " \"" + x.val + "\""; },
      [](const Not& x) { return "(not " + format(*x.sub) + ")"; },
      [](const And& x) { return "(" + format(*x.l) + " and " + format(*x.r) + ")"; },
      [](const Or&  x) { return "(" + format(*x.l) + " or "  + format(*x.r) + ")"; },
  }, e.v);
}

template <class T> ExprPtr mk(T x) { return std::make_unique<Expr>(Expr{std::move(x)}); }

int main() {
  std::vector<Clip> clips = {
      {"intro_4k.mov",   3840, 60,  18, "hevc", true},
      {"interview.mp4",  1920, 30, 120, "h264", false},
      {"broll_slow.mp4", 1920, 60,  12, "h264", false},
      {"drone_hdr.mkv",  2560, 30,  25, "hevc", true},
      {"proxy_720.mp4",  1280, 24,   9, "h264", false},
  };
  auto ast = mk(Or{
      mk(And{mk(IntCmp{"width", ">=", 1920}), mk(IntCmp{"fps", ">=", 60})}),
      mk(CodecCmp{"==", "hevc"}),
  });
  std::printf("规范表达式：%s\n命中素材：", format(*ast).c_str());
  for (const auto& c : clips) if (eval(*ast, c)) std::printf(" %s", c.name.c_str());
  std::printf("\n");
}
```

```text
规范表达式：((width >= 1920 and fps >= 60) or codec == "hevc")
命中素材： intro_4k.mov broll_slow.mp4 drone_hdr.mkv
```

这里正好体现了**解释器 + 访问者的经典搭档**：AST 节点集合相对稳定（6 种），但围绕 AST 的操作（求值、规范打印、常量折叠、编译成 SQL）会不断增加，用 `std::variant` + `std::visit` 加一个新操作只需新写一个函数，而且漏处理任何节点类型都会直接编译报错。

如果希望连逐节点 `new` / `unique_ptr` 也省掉，可以把所有节点平铺进一个 `std::vector<Node>`，子节点只存 `int` 下标（类似 Arena 分配，完整实现见 `projects/dp-lab/28-interpreter/bench.cpp`）：

<!-- run: off -->
```cpp
struct Not { int sub; };
struct And { int l, r; };
struct Or  { int l, r; };
using Node = std::variant<Hdr, IntCmp, CodecCmp, Not, And, Or>;

struct Program {
  std::vector<Node> nodes;   // 整棵 AST 连续存放在一块内存里，零逐节点堆分配
  int root = 0;
};
```

这样建树时只需要 `vector` 的一次连续分配，拷贝、释放、序列化 AST 都非常简单；但当 AST 很小时（比如本例只有 5 个节点），它与经典虚函数 AST 的求值速度几乎在同一量级，下面用实测数据说话。

### 4.2 四种写法求值耗时实测

用 `projects/dp-lab/28-interpreter/bench.cpp` 对 **10 万条**随机生成的视频元数据执行 `(width >= 1920 and fps >= 60) or codec == "hevc"`（命中 53,105 条，Apple Clang 21，`-O2`，7 轮取中位数）：

<figure class="diagram">
<svg viewBox="0 0 660 176" role="img" aria-label="10 万条视频元数据求值耗时对比（-O2，7 轮中位数）：手写 C++ 硬编码 0.963 ms（9.6 ns/条），经典虚函数 AST 2.437 ms（24.4 ns/条），std::variant AST 2.894 ms（28.9 ns/条），每次重解析字符串 22.980 ms（229.8 ns/条）"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">10 万条素材求值耗时（ms，越小越快）</text><rect x="158.0" y="30.0" width="15.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="180.1" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.963 ms (9.6 ns/条)</text><rect x="158.0" y="60.0" width="38.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="203.2" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2.437 ms (24.4 ns/条)</text><rect x="158.0" y="90.0" width="45.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="210.3" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2.894 ms (28.9 ns/条)</text><rect x="158.0" y="120.0" width="360.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="525.0" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">22.980 ms (229.8 ns/条)</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">手写 C++ 硬编码</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">经典虚函数 AST</text><text x="150.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::variant AST</text><text x="150.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">每次重解析字符串</text><text x="10.0" y="166.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp，Apple Clang 21 -O2（arm64）：10 万条随机 Clip，7 轮中位数，四种写法命中数均为 53105</text></g></svg>
</figure>

| 写法 | 10 万条耗时 | 单条耗时 | 相对硬编码 | 关键特点 |
| --- | :---: | :---: | :---: | --- |
| 手写 C++ 硬编码 | **0.963 ms** | 9.6 ns | 1.0× | 编译器完全内联，但规则写死在代码里 |
| 经典虚函数 AST（解析一次） | **2.437 ms** | 24.4 ns | 2.5× | 5 个节点常驻 L1、分支预测极准，最易扩展新节点 |
| `std::variant` AST（解析一次） | **2.894 ms** | 28.9 ns | 3.0× | 零逐节点堆分配，极易扩展 `format` / `validate` 等多操作 |
| 每次重解析字符串再求值 | **22.980 ms** | 229.8 ns | 23.9× | 每次都切词 + 堆分配建树，慢近 10 倍，必须避免 |

两个值得记住的实测结论：
1. **最大的瓶颈不是虚函数分派，而是重复解析**。只要做到「解析一次、求值多次」，虚函数 AST 和 `variant` AST 在 10 万条数据上都只要 **2～3 毫秒**，换来的却是运行时动态组合任意条件的能力。
2. 小 AST 反复求值时，虚函数 AST 甚至略快于扁平 `variant` AST（2.44 vs 2.89 ms），因为固定 5 个节点的虚表跳转目标 100% 可预测，而 `std::visit` 递归查表多了下标寻址；选型应优先看**扩展方向**（只求值且常加语法节点选虚函数，节点稳定但要加多种遍历操作选 `variant`）。

## 5. 解释器和相似的模式

| 模式 | 关心什么 | 和解释器的区别 |
| --- | --- | --- |
| **组合** | 用树形结构统一对待叶子和容器 | 解释器的 AST 本身就是一棵组合树；组合只管**结构**，解释器额外赋予它**文法含义与求值语义** |
| **访问者** | 在不改元素类的前提下新增操作 | 解释器常把求值写在节点里；一旦要对 AST 做求值、打印、优化、代码生成等多种操作，就会配合访问者（或 `std::visit`） |
| **策略** | 在多种可互换的算法之间选一个 | 策略通常是一层平铺的算法替换；解释器是通过文法把原子规则**递归组合**成任意深度的树 |
| **责任链** | 把请求沿着一条链依次传递直到被处理 | 责任链是线性的单向传递；解释器是树形的递归求值，并由非终结符组合子结果 |

## 6. 小结

- 当业务规则需要支持**任意深度的嵌套组合**（如 `and / or / not / ()`），扁平的配置结构体很快就会失效；此时应把规则视为一门微型 DSL，用解释器模式表示文法并求值。
- 解释器的核心是四个角色：`Expr`（抽象表达式）、终结符（叶子）、非终结符（内部节点）与 `Context`（求值上下文）；配合按优先级分层的递归下降解析器，几十行就能解析任意括号嵌套。
- **解析一次、求值多次**是性能底线（10 万条实测 2.4 ms vs 23.0 ms）；操作不止 `eval` 一种时，用 `std::variant` + `std::visit` 把 AST 与操作解耦。
- 文法简单（5～15 条规则）适合手写解释器；一旦文法膨胀或需要脚本能力，果断换 ANTLR/Bison 或嵌入 Lua/SQLite。
- **至此，23 种 GoF 设计模式已全部讲完**（创建型 5 种、结构型 7 种、行为型 11 种）——回头看，很多模式并非孤立存在，就像本篇里解释器的 AST 借了组合的结构、多操作借了访问者、`variant` 写法又和状态/访问者一脉相承。

## 7. 练习

1. **辨析**：下面四个场景，哪些适合手写解释器模式？不适合的该用什么？
   - (a) 剪辑软件的智能素材箱过滤框，支持 `width / fps / codec / hdr` 的 `and / or / not` 组合；
   - (b) 允许用户写包含自定义函数、循环和异步网络请求的完整插件脚本；
   - (c) 规则引擎要解析并执行完整的 SQL `SELECT ... JOIN ... GROUP BY` 查询；
   - (d) 媒体转码任务里的简单路由规则：`ext == "mov" and bitrate > 50`。
2. **扩展**：在 `projects/dp-lab/28-interpreter/interpreter_basic.cpp` 的基础上新增 `in` 运算符，支持 `codec in ("hevc", "av1") and width >= 1920`。需要新增什么终结符类？`Parser` 哪两处要改？
3. **找坑**：下面是一段简化后的素材过滤解释器，用来求值 `codec == "hevc" or width >= 1920 and fps >= 60`。输入 `drone_hdr.mkv`（`2560×30fps, hevc`）明明满足 `codec == "hevc"`，却输出 `命中=0，叶子求值次数=3`。找出代码里的**两个**坑，并说明怎么修。

```cpp
// 《解释器模式》练习 3：运算符优先级写扁 + 提前算完左右分支破坏短路求值
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip { std::string name; int width, fps; std::string codec; };

struct Expr { virtual ~Expr() = default; virtual bool eval(const Clip&, int& probes) const = 0; };
using ExprPtr = std::unique_ptr<Expr>;

struct CodecEq : Expr {
  std::string val; explicit CodecEq(std::string v) : val(std::move(v)) {}
  bool eval(const Clip& c, int& probes) const override { ++probes; return c.codec == val; }
};
struct GeExpr : Expr {
  bool isWidth; int val; GeExpr(bool w, int v) : isWidth(w), val(v) {}
  bool eval(const Clip& c, int& probes) const override { ++probes; return (isWidth ? c.width : c.fps) >= val; }
};

struct BadAnd : Expr {
  ExprPtr l, r; BadAnd(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
  bool eval(const Clip& c, int& probes) const override {
    bool a = l->eval(c, probes), b = r->eval(c, probes);  // 坑 1：先算两边，短路失效
    return a && b;
  }
};
struct BadOr : Expr {
  ExprPtr l, r; BadOr(ExprPtr a, ExprPtr b) : l(std::move(a)), r(std::move(b)) {}
  bool eval(const Clip& c, int& probes) const override {
    bool a = l->eval(c, probes), b = r->eval(c, probes);  // 同样破坏短路
    return a || b;
  }
};

// 坑 2：把 and / or 放在同一个 while 循环里从左到右结合，无视 and 优先级高于 or
ExprPtr parseFlat(const std::vector<std::string>& ops, std::vector<ExprPtr> atoms) {
  ExprPtr e = std::move(atoms[0]);
  for (size_t i = 0; i < ops.size(); ++i)
    e = (ops[i] == "and") ? ExprPtr(std::make_unique<BadAnd>(std::move(e), std::move(atoms[i + 1])))
                          : ExprPtr(std::make_unique<BadOr>(std::move(e), std::move(atoms[i + 1])));
  return e;
}

int main() {
  // 表达式：codec == "hevc" or width >= 1920 and fps >= 60
  std::vector<ExprPtr> atoms;
  atoms.push_back(std::make_unique<CodecEq>("hevc"));
  atoms.push_back(std::make_unique<GeExpr>(true, 1920));
  atoms.push_back(std::make_unique<GeExpr>(false, 60));
  auto ast = parseFlat({"or", "and"}, std::move(atoms));

  Clip drone{"drone_hdr.mkv", 2560, 30, "hevc"};
  int probes = 0;
  bool ok = ast->eval(drone, probes);
  std::printf("%s 命中=%d，叶子求值次数=%d\n", drone.name.c_str(), ok ? 1 : 0, probes);
}
```

```text
drone_hdr.mkv 命中=0，叶子求值次数=3
```

<details>
<summary>点击查看答案</summary>

1. (a) **适合**：文法小、固定、零依赖，几十行递归下降即可；
   (b) **不适合**：图灵完备脚本直接嵌入 Lua / QuickJS / Python，不要手写解释器；
   (c) **不适合**：完整 SQL 文法极庞大，直接用 SQLite 或成熟 SQL 解析器；
   (d) **适合**：典型的微型布尔条件 DSL。

2. 新增终结符 `InExpr`（持有 `std::vector<std::string> items`，`eval` 判断 `c.codec` 是否在列表中）。`Parser` 改两处：① 词法切分里把 `,` 当独立单字符 token；② `parseAtom()` 读完字段名后若 `match("in")`，依次解析 `(`、逗号分隔的字符串列表、`)`，构造 `InExpr`。完整参考实现见 `projects/dp-lab/28-interpreter/ex2_in_expr.cpp`。

3. **坑 1（破坏短路）**：`BadAnd` / `BadOr` 把左右两边先存进局部变量 `a`、`b` 再 `&&` / `||`，导致即便左边已经决定结果，右边依然会被求值（如果右枝是读磁盘/查数据库等重操作，开销白白浪费）。修复：直接 `return l->eval(c, probes) && r->eval(c, probes);`（`||` 同理）。
   **坑 2（优先级写扁）**：`parseFlat` 把 `or` 和 `and` 放进同一层循环从左到右结合，把原式错误解析成了 `(codec == "hevc" or width >= 1920) and fps >= 60`，结果 `drone_hdr.mkv`（30 fps）被错误过滤掉。修复：按优先级拆成两层函数——`parseOr()` 调用 `parseAnd()`，让 `and` 先结合成右子树；两处全修后输出应为 `命中=1，叶子求值次数=1`（`codec == "hevc"` 为真后直接短路）。

</details>
