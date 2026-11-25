---
title: 设计模式实战（四）：模式的边界、反模式实测与全专栏选型总览
date: 2026-11-25 23:30:00
updated: 2026-11-25 23:30:00
categories:
  - 设计模式
  - 设计模式实战
tags:
  - 设计模式
  - 设计模式实战
  - 反模式
  - 过度设计
  - 选型指南
  - 架构复盘
order: 4
description: 学完 23 种 GoF 模式与 4 个 C++ 惯用法后，最危险的阶段不是「记不住」，而是「看见什么问题都想套三层模式」。本文结合前三篇实现的 minilog，逐一复盘故意没有使用的模式清单（全局单例、抽象工厂、多层虚函数装饰器、热路径 GoF 解释器类树），并用代码实测同台对比三版本实现相同过滤与格式化功能：大泥球单例版耗时 25.38 ms（126.9 ns/op，每次 4 次堆分配）、过度 GoF 炫技版暴涨到 22 个类、耗时 39.71 ms（198.5 ns/op，每次 17 次堆分配，20 万次触发 340 万次 new），而克制的现代 C++ 版 minilog 仅用 18.55 ms（92.8 ns/op，0 次堆分配）。最后收束一张覆盖全专栏 23 GoF + 4 C++ 惯用法的工程选型总览图。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：看清**「不套模式」与「选对模式」同等重要**：复盘 `minilog` 里故意弃用的 GoF 模式及原因；用同一份需求写出「大泥球」「过度 GoF 炫技」「克制现代 C++」三版代码，同台实测代码量、类膨胀、耗时与堆分配差距；最后把全专栏 **23 种 GoF 模式 + 4 种 C++ 惯用法** 收成一张按变化维度和性能预算检索的终极选型总览。
>
> **前置知识**：《设计模式实战（一）~（三）》及《设计模式速查》。
>
> **编译环境**：`g++ / clang++ -std=c++17`，三版本对照源码与实测见 `projects/minilog/legacy/overengineered_logger.cpp` 与 `projects/minilog/bench/bench_three_ways.cpp`。

## 1. 复盘：`minilog` 里故意「没用」的模式

前三篇里，`minilog` 只落了真正解决问题的 7 个工具：**外观 + Pimpl、建造者、依赖注入注册表、RAII / ScopeGuard、NVI 模板方法、策略 + 扁平管线、SBO 类型擦除**。同样重要的是清单另一面——我们在 `projects/minilog/` 里**故意没有引入**下面这些经典模式：

| 故意没用的模式 | 如果硬套会发生什么 | 我们实际的选择 |
| --- | --- | --- |
| **全局单例（Singleton）** | 测试之间状态串扰、静态对象析构次序踩雷 | 普通局部对象 + 可注入 `LoggerRegistry` |
| **抽象工厂（Abstract Factory）** | Sink、格式器、滚动策略彼此正交，强行凑「产品族」只会造出一堆空壳工厂类 | 按各自职责独立配置，`SinkFactory` 注册表只创建 Sink |
| **多层虚装饰器（Decorator）** | `Lock(Level(Module(Redact(Metrics(Core)))))` 套 5 层堆对象，顺序写反就出错且每层间接跳转 | NVI `BaseSink` 固定加锁与格式化 + 扁平 `FilterPipeline` |
| **GoF 解释器类树（Interpreter）** | 把 `"%T [%L] [%n] %v"` 拆成满堆 `unique_ptr<Expr>` 节点，每次打日志虚调用拼接 | 构造期一次性预编译成 `std::variant` Token 数组 |
| **状态（State）/ 备忘录 / 组合 / 访问者** | 日志器没有复杂多态生命周期、不需要撤销历史、也没有嵌套编组树 | 布尔标志 `stopping_` / `stopped_` 够用就绝不加类 |

拿 **解释器类树 vs 预编译 `std::variant`** 举个最直观的例子。下面这段纯标准库代码在浏览器里就能跑，直观对比「把每个占位符写成 `Expr` 子类」和「用紧凑 `std::variant` 数组」的代码密度与堆分配差异：

```cpp
// 《实战（四）》：同样解析格式化模板——GoF 解释器类树 vs 现代 C++ variant Token
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

struct Context { const char* level; const char* mod; const char* msg; };

// 方式 A：GoF 解释器类树（5 个类 + 每个节点堆分配 + 虚函数拼接）
struct Expr {
  virtual ~Expr() = default;
  virtual std::string eval(const Context&) const = 0;
};
struct LitExpr final : Expr {
  std::string s;
  explicit LitExpr(std::string v) : s(std::move(v)) {}
  std::string eval(const Context&) const override { return s; }
};
struct LvlExpr final : Expr {
  std::string eval(const Context& c) const override { return c.level; }
};
struct ModExpr final : Expr {
  std::string eval(const Context& c) const override { return c.mod; }
};
struct MsgExpr final : Expr {
  std::string eval(const Context& c) const override { return c.msg; }
};
struct SeqExpr final : Expr {
  std::vector<std::unique_ptr<Expr>> kids;
  std::string eval(const Context& c) const override {
    std::string out;
    for (const auto& k : kids) out += k->eval(c);
    return out;
  }
};

// 方式 B：现代 C++ 扁平 std::variant Token（零虚函数、直接追加进复用缓冲）
struct LitTok { std::string s; };
struct LvlTok {};
struct ModTok {};
struct MsgTok {};
using Token = std::variant<LitTok, LvlTok, ModTok, MsgTok>;

void formatTokens(const std::vector<Token>& toks, const Context& c, std::string& out) {
  for (const auto& t : toks) {
    std::visit([&](const auto& node) {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, LitTok>) out.append(node.s);
      else if constexpr (std::is_same_v<T, LvlTok>) out.append(c.level);
      else if constexpr (std::is_same_v<T, ModTok>) out.append(c.mod);
      else if constexpr (std::is_same_v<T, MsgTok>) out.append(c.msg);
    }, t);
  }
}

int main() {
  Context ctx{"INFO", "pay", "order=42 ok"};

  SeqExpr ast;
  ast.kids.push_back(std::make_unique<LitExpr>("["));
  ast.kids.push_back(std::make_unique<LvlExpr>());
  ast.kids.push_back(std::make_unique<LitExpr>("] ["));
  ast.kids.push_back(std::make_unique<ModExpr>());
  ast.kids.push_back(std::make_unique<LitExpr>("] "));
  ast.kids.push_back(std::make_unique<MsgExpr>());

  std::vector<Token> toks = {LitTok{"["}, LvlTok{}, LitTok{"] ["}, ModTok{}, LitTok{"] "}, MsgTok{}};
  std::string out;
  formatTokens(toks, ctx, out);

  std::printf("GoF AST : %s (sizeof node ptr=%zu B)\n", ast.eval(ctx).c_str(), sizeof(std::unique_ptr<Expr>));
  std::printf("variant : %s (vec contiguous, 0 heap alloc on format)\n", out.c_str());
}
```

```text
GoF AST : [INFO] [pay] order=42 ok (sizeof node ptr=4 B)
variant : [INFO] [pay] order=42 ok (vec contiguous, 0 heap alloc on format)
```

> 浏览器内跑在 32 位 `wasm32` 下指针为 4 B（64 位本机为 8 B）。

## 2. 同台实测：大泥球 vs 过度 GoF 炫技 vs `minilog`

为了量化「过度套模式」到底有多伤，我们在 `projects/minilog/legacy/overengineered_logger.cpp` 里写了一个极度“教科书”的反面教材——实现了和 `minilog` 相同的级别过滤、模块过滤、脱敏与格式化计数，但强行叠满了 **全局单例 + 抽象工厂 + 5 层堆上虚函数装饰器 + 每次日志重建的 GoF 解释器 AST**。

再用 `bench/bench_three_ways.cpp` 把它与大泥球单例 `MonoLogger`、克制的 `minilog` 放在同一台机器上跑 20 万次日志（Apple clang 21 `-O2`，5 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 700 198" role="img" aria-label="20 万次日志三版本实测：过度 GoF 炫技版单次耗时 198.5 ns 并触发 340 万次堆分配，大泥球单例版 126.9 ns 并触发 80 万次堆分配，克制的现代 C++ 版 minilog 耗时 92.8 ns 且 0 次堆分配"><g style="font-family:var(--mono)"><text x="165" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">20 万次日志调用单次耗时与堆分配（Clang -O2）</text><text x="157" y="50" text-anchor="end" style="fill:var(--fg);font-size:11px">2. 过度 GoF 炫技版</text><rect x="165" y="34" width="320" height="22" rx="2" style="fill:var(--danger)"/><text x="492" y="49" text-anchor="start" style="fill:var(--danger);font-size:11px;font-weight:700">198.5 ns/op（3,400,000 次堆分配 / 17.0/op）</text><text x="157" y="88" text-anchor="end" style="fill:var(--fg);font-size:11px">1. 大泥球单例版</text><rect x="165" y="72" width="205" height="22" rx="2" style="fill:var(--warn)"/><text x="377" y="87" text-anchor="start" style="fill:var(--warn);font-size:11px;font-weight:700">126.9 ns/op（800,001 次堆分配 / 4.0/op）</text><text x="157" y="126" text-anchor="end" style="fill:var(--fg);font-size:11px">3. minilog 现代 C++</text><rect x="165" y="110" width="150" height="22" rx="2" style="fill:var(--accent)"/><text x="322" y="125" text-anchor="start" style="fill:var(--accent);font-size:11px;font-weight:700">92.8 ns/op（0 次堆分配，比过度 GoF 快 2.1×）</text><text x="10" y="162" text-anchor="start" style="fill:var(--muted);font-size:10.5px">结构规模：大泥球 112 行 / 1 类（213.7 ms 编）；过度 GoF 243 行 / 22 类（200.1 ms）；完整 minilog 库含异步滚动 1565 行 / 29 类</text><text x="10" y="180" text-anchor="start" style="fill:var(--muted);font-size:10.5px">完整对比源码见 projects/minilog/legacy/overengineered_logger.cpp 与 bench/bench_three_ways.cpp</text></g></svg>
</figure>

| 对比维度 | 1. 大泥球单例版 | 2. 过度 GoF 炫技版 | 3. 克制的现代 C++ 版（`minilog`） |
| --- | :---: | :---: | :---: |
| **类 / 结构体数量** | 1（全耦合） | 22（单文件膨胀出 22 个类型） | 29（覆盖异步/滚动/Pimpl/SBO 完整库） |
| **非空代码行（LOC）** | 112 | 243（仅同步玩具子集） | 1565（完整多文件工业级实现） |
| **20 万次总耗时** | 25.38 ms | 39.71 ms | **18.55 ms** |
| **单次耗时** | 126.9 ns/op | 198.5 ns/op | **92.8 ns/op** |
| **20 万次堆分配数** | 800,001（4.0/op） | **3,400,000（17.0/op）** | **0（0.0/op）** |
| **测试隔离 / 扩展示性** | 不可测、改字段重编全部调用方 | 依旧全局单例、顺序脆弱 | 完全隔离、SBO 零继承扩展、ABI 恒定 8 B |

结果非常讽刺也很真实：**为了套满设计模式而生的「过度 GoF 版」，243 行代码就造出 22 个类，比大泥球慢了 56%、每打一条日志触发 17 次堆分配；而克制的 `minilog` 在提供完整可扩展架构的前提下，反而跑出最低延迟（92.8 ns/op）与 0 次堆分配。**

## 3. 全专栏收官选型地图：23 GoF + 4 C++ 惯用法

学完整套《设计模式》专栏后，平时写 C++ 遇到扩展与耦合难题，按下面这张总表从**变化维度 → 运行/编译期绑定 → 现代 C++ 替代写法**查表即可（日常速查也可以直接看[设计模式速查](/column/design-patterns/cheatsheet/)）：

### 3.1 核心多态机制四选一（先定实现载体）

| 机制 | 类型集合 | 热路径性能 / 堆分配 | 典型用途 |
| --- | :---: | :---: | --- |
| **模板 / CRTP** | 开放，但**编译期固定** | 零虚调用，可内联 / SIMD | 像素核、音频采样滤镜、`std::sort` 比较器 |
| **`std::variant` + `std::visit`** | **编译期封闭** | 0 堆分配，支持异构容器 | 格式串 Token、AST 节点、有限状态机状态 |
| **类型擦除（含 SBO）** | 开放，**运行时混装** | SBO 下 0 堆分配，非侵入 | `std::function`、`AnySink`、回调/插件包装 |
| **虚基类 + 智能指针** | 开放，运行时混装 | 1 次堆分配 + 虚表跳转 | 大型长生命周期框架组件（`BaseSink`、`RotationPolicy`） |

下面这段短程序可在浏览器里直接运行，用同一条日志数据同台展示「静态模板」「`std::variant` 封闭集」「虚基类」三种多态写法的尺寸与代码风格差异：

```cpp
// 《实战（四）》：三种多态载体同台对比（编译期模板 vs variant 封闭集 vs 虚基类）
#include <cstdio>
#include <memory>
#include <string>
#include <variant>
#include <vector>

// 1. 编译期静态多态：零 vptr，必须已知具体类型
struct StaticSink {
  int factor = 2;
  int consume(const std::string& s) const { return static_cast<int>(s.size()) * factor; }
};
template <typename Sink>
int emitStatic(const Sink& s, const std::string& msg) { return s.consume(msg); }

// 2. 封闭类型集：std::variant，零堆分配可混装同一容器
struct NullTarget { int tag; };
struct StatTarget { int mul; };
using VariantTarget = std::variant<NullTarget, StatTarget>;

// 3. 运行时开放继承树：带 8B vptr，需指针混装
struct IVirtTarget {
  virtual ~IVirtTarget() = default;
  virtual int consume(const std::string& s) const = 0;
};
struct VirtStat final : IVirtTarget {
  int mul;
  explicit VirtStat(int m) : mul(m) {}
  int consume(const std::string& s) const override { return static_cast<int>(s.size()) * mul; }
};

int main() {
  std::string msg = "payment ok";
  StaticSink st{3};
  std::vector<VariantTarget> closed = {NullTarget{1}, StatTarget{3}};
  std::vector<std::unique_ptr<IVirtTarget>> open;
  open.push_back(std::make_unique<VirtStat>(3));

  int sumVariant = 0;
  for (const auto& t : closed) {
    sumVariant += std::visit([&](const auto& x) -> int {
      using T = std::decay_t<decltype(x)>;
      if constexpr (std::is_same_v<T, NullTarget>) return x.tag;
      else return static_cast<int>(msg.size()) * x.mul;
    }, t);
  }
  std::printf("static=%d variant=%d virtual=%d\n",
              emitStatic(st, msg), sumVariant, open[0]->consume(msg));
  std::printf("sizeof: StaticSink=%zu B  VariantTarget=%zu B  VirtStat=%zu B\n",
              sizeof(StaticSink), sizeof(VariantTarget), sizeof(VirtStat));
}
```

```text
static=30 variant=31 virtual=30
sizeof: StaticSink=4 B  VariantTarget=8 B  VirtStat=8 B
```

> 注：浏览器沙箱为 `wasm32`（虚表指针 `vptr` 4 B，故 `VirtStat` 为 `8 B`）；64 位本机下 `VirtStat` 会膨胀至 `16 B`（`8 B vptr + 4 B int + 4 B` 填充）。

### 3.2 23 种 GoF + 4 种 C++ 惯用法工程检索表

| 你当前面对的变化 / 痛点 | 首选模式或惯用法 | 何时用现代 C++ 简化 | 什么时候绝对别用 |
| --- | --- | --- | --- |
| 需要全局唯一查找表 / 资源 | **注册表 + 依赖注入**（[2.1 单例](/posts/dp-singleton/)） | `LoggerRegistry` 局部传递 | 能传参就绝不做真全局单例 |
| 创建单个多态产品 | **工厂方法**（[2.2 工厂](/posts/dp-factory-method/)） | 字符串 → 创建函数注册表 | 只有 2 个固定类型且永不扩充 |
| 成套创建不可混用的同族产品 | **抽象工厂**（[2.3 抽象工厂](/posts/dp-abstract-factory/)） | 模板产品族编译期卡死 | 各个组件彼此正交独立 |
| 多参数装配 + 集中校验 | **建造者**（[2.4 建造者](/posts/dp-builder/)） | 简单结构体用 C++20 指定初始化 | 只有 2～3 个必填参数 |
| 拿基类指针深拷贝对象 | **原型**（[2.5 原型](/posts/dp-prototype/)） | CRTP 自动生成 `clone()` | 对象不可拷贝或只有唯一所有权 |
| 对接第三方 / C 库接口 | **适配器**（[3.1 适配器](/posts/dp-adapter/)） | 自由函数 / ADL + 类型擦除 | 源码归自己管，直接改接口更好 |
| 两个正交维度独立扩展 | **桥接 / Pimpl**（[3.2 桥接](/posts/dp-bridge/)、[5.2 Pimpl](/posts/dp-pimpl/)） | 单实现直接 Pimpl；编译期双维度用模板 | 只有一个维度会变 |
| 整体和部分同构的树形结构 | **组合**（[3.3 组合](/posts/dp-composite/)） | `vector<unique_ptr<Node>>` | 只有扁平列表，强行套树 |
| 运行时自由叠加同接口职责 | **装饰器**（[3.4 装饰器](/posts/dp-decorator/)） | 静态组合用模板 Mixin；过滤器用 `vector` | 层数固定且顺序要求极死 |
| 屏蔽子系统多步繁琐调用 | **外观**（[3.5 外观](/posts/dp-facade/)） | 几组自由函数或门面类（`Logger`） | 本身只有一个简单类 |
| 百万级细粒度对象省内存 | **享元**（[3.6 享元](/posts/dp-flyweight/)） | `string_view` 驻留池 / `weak_ptr` 缓存 | 对象总数不过千，省的内存不够查表开销 |
| 延迟加载 / 鉴权 / 缓存控制 | **代理**（[3.7 代理](/posts/dp-proxy/)） | `unique_ptr` / `shared_ptr` 自带代理语义 | 没有额外控制需求，纯透明转发 |
| 运行时切换可互换算法 | **策略**（[4.1 策略](/posts/dp-strategy/)） | 单方法直接用 `std::function` 或模板参数 | 算法永远只有一种（`switch` 只有 1 个分支） |
| 一对多自动通知、解耦订阅 | **观察者**（[4.2 观察者](/posts/dp-observer/)） | `Signal + RAII Connection` / `weak_ptr` | 固定一对一直接调用更清楚 |
| 请求封装成值对象（队列/撤销） | **命令**（[4.3 命令](/posts/dp-command/)） | 不撤销时用 `std::function` / 紧凑 SBO 消息 | 同步直接调用即可，无需排队或回滚 |
| 固定算法骨架、定制局部步骤 | **模板方法**（[4.4 模板方法](/posts/dp-template-method/)） | **NVI（公有非虚 + 私有虚函数）**；热路径用 CRTP | 步骤之间没有任何公共流程可复用 |
| 隐藏容器内部实现并统一遍历 | **迭代器**（[4.5 迭代器](/posts/dp-iterator/)） | 直接对齐 `begin()/end()` 与 C++20 Ranges | `std::vector` 已经够用时别手搓容器 |
| 行为随内部状态大量分支迁移 | **状态**（[4.6 状态](/posts/dp-state/)） | 状态有限用 `std::variant` + `std::visit` | 只有 `open`/`closed` 两三个布尔标志 |
| 请求沿多级处理者过滤或放行 | **责任链**（[4.7 责任链](/posts/dp-chain/)） | 扁平 `vector<std::function<bool(Ctx&)>>` | 固定顺序不可配置，直接顺序调函数更快 |
| 对稳定类型集不断加新操作 | **访问者**（[4.8 访问者](/posts/dp-visitor/)） | 类型集合封闭直接用 `std::variant` + `std::visit` | 经常加新元素类型而非新操作 |
| 多控件网状联动解耦 | **中介者**（[4.9 中介者](/posts/dp-mediator/)） | 局部控制器回调接线 | 只有两个对象通信 |
| 不破坏封装保存并恢复快照 | **备忘录**（[4.10 备忘录](/posts/dp-memento/)） | `shared_ptr<const Data>` 写时复制 | 操作极小且可逆，用命令逆操作更省内存 |
| 简单规则/表达式求值 | **解释器**（[4.11 解释器](/posts/dp-interpreter/)） | `std::variant` AST 或预编译 Token | 复杂文法直接上现成解析器，热路径别建树 |
| 资源释放 / 编译防火墙 / 零开销 | **RAII / Pimpl / CRTP / 类型擦除** | 贯穿全部实战的 C++ 底座 | 永远优先用 RAII；后三者按边界与热路径需求启用 |

想动手检验自己能不能把乱成一团的 `if-else` 正确重构，可以做专栏配套的两道**重构挑战**题（浏览器里自带编译与结构检查）：
- [重构挑战：运费计算](/problems/refactor-shipping-strategy/)（策略模式）
- [重构挑战：日志过滤与脱敏管线](/problems/refactor-logger-pipeline/)（责任链 / 过滤管线）

## 4. 小结

- **设计模式不是目标，控制变化代价才是**：写代码前先问一句「未来半年真正会变的是什么」。没有变化维度的地方写最直白的面向过程或值结构体，反而最快、最容易读懂。
- **反模式往往长着「很懂架构」的脸**：把正交选项硬绑成抽象工厂、给只有一条过滤链的场景套 5 层虚函数装饰器、在百万次调用的热路径里现搭 GoF 解释器对象树，实测比大泥球慢 56% 且多出 4 倍堆分配。
- **现代 C++ 让经典模式更轻、更快**：`std::variant` 接管封闭集多态，**48 B / 128 B SBO + 预编译 Token** 消灭零散堆分配，**Pimpl + RAII + NVI** 守住模块边界与异常安全。到这里，《设计模式》全专栏就全部收官了。

## 5. 练习

1. **架构审查**：接手一个新项目时，你看到一段代码为了支持 2 种配置格式（JSON 和 TOML）写了 `IConfigFactory` + `JsonFactory` + `TomlFactory` + `JsonParser` + `TomlParser` + `IConfigNodeVisitor` 共 14 个类，但实际业务只需要读出 5 个键值字段。你会怎么精简它？
2. **选型判断**：假如要为 `minilog` 新增一个「支持多种输出目标的日志格式校验工具」（在 CLI 里检查 JSON 配置文件，节点类型只有 `String / Int / Array / Object` 四种，且需要新增「校验 / 格式化 / 统计键数量」多种操作），你会用 GoF 访问者模式还是 `std::variant` + `std::visit`？原因是什么？
3. **热路径优化识别**：回头看第 2 节三版本对比表：为什么 `minilog` 同步版连只有 1 个类的 `MonoLogger` 都跑赢了（92.8 ns/op vs 126.9 ns/op）？瓶颈到底出在哪里？

<details>
<summary>点击查看答案</summary>

1. 先问清楚「产品族里是不是每种格式都对应一整套必须联动且不能混用的对象」——显然这里只是把一段文本解析成平铺的键值表。直接删掉整套抽象工厂和访问者类树，简化成两个纯函数 `parseJson(string_view) -> ConfigMap` 和 `parseToml(string_view) -> ConfigMap`（或者一个按扩展名分派的简单工厂映射表），2 个函数替代 14 个类。
2. 选 **`std::variant` + `std::visit`**。JSON 值类型集合是**严格封闭**的（永远只有那几种基础类型），而后续要加的分析操作会不断增多。用 `std::variant` 既能把节点紧凑塞进容器、省掉 `accept()` 双分派样板代码，又能在新增数据类型时靠编译器穷尽检查自动找出所有漏改的 `visit` 分支。
3. 瓶颈在 **`std::ostringstream` 与临时 `std::string` 的堆分配**。`MonoLogger` 看似只有 1 个类，但每打一条日志都要构造一个 `std::ostringstream` 并返回 `oss.str()` 再拷贝给 `lastLine_`，一次调用触发约 4 次堆分配/释放（20 万次共 800,001 次）；而 `minilog` 靠 `LogRecord` 128 B SBO 与 `PatternFormatter` 复用 `BaseSink::scratch_`，在整轮 20 万次调用里做到了 **0 次堆分配**。

</details>
