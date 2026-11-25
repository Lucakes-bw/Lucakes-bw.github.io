---
title: 设计模式实战（二）：过滤管线、滚动策略与非侵入式 Sink
date: 2026-11-25 22:30:00
updated: 2026-11-25 22:30:00
categories:
  - 设计模式
  - 设计模式实战
tags:
  - 设计模式
  - 设计模式实战
  - 模板方法
  - 策略模式
  - 责任链
  - 工厂方法
  - 类型擦除
order: 2
description: 第二轮需求把日志系统推向真实生产环境：按模块过滤、突发限流、手机号与 token 自动脱敏，按大小/行数/时间滚动切分文件，还要让第三方告警结构体不修改源码、不继承任何基类就能直接接入。本文展示 minilog 如何用四件克制的工具拆解这些问题：扁平 FilterPipeline 替代指针链表型责任链，NVI 模板方法 BaseSink 统一加锁与格式化骨架，Strategy 模式 RotationPolicy 与 SinkFactory 注册表解耦文件切分，48 字节 SBO 类型擦除 AnySink 兼顾值语义与零继承接入。Apple clang 21 -O2 实测 100 万次异构 Sink 创建调用：传统 unique_ptr<ISink> 触发 100 万次堆分配、耗时 7.02 ns/次；AnySink 堆分配直降为 0、耗时降至 1.40 ns/次（快 5.0 倍）。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：在 `minilog` 已立好库边界的基础上落地第二轮生产需求：用扁平 **`FilterPipeline`（责任链变形）**实现过滤/限流/脱敏；用 **NVI 模板方法 `BaseSink`** 抽干公共骨架；用 **策略模式 `RotationPolicy` + 注册表 `SinkFactory`** 支持文件滚动与插件化扩展；用 **48 B SBO 类型擦除 `AnySink`** 让第三方结构体零侵入挂入引擎，并用实测验证其堆分配与分派收益。
>
> **前置知识**：《模板方法模式》《策略模式》《责任链模式》《简单工厂与工厂方法》《类型擦除（Type Erasure）》。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整代码与实测见 `projects/minilog/`（`tests/test_pipeline.cpp`、`bench/bench_sink_dispatch.cpp`）。

## 1. 第二轮需求：光能写盘远远不够

边界立好之后，业务线很快提了三类新需求：

1. **前置治理管线**：线上只需要 `payment`、`auth` 等白名单模块的 `Info` 以上日志；同模块突发异常刷屏时要限流（比如每 1000 ms 最多放行 2 条）；日志里的 11 位手机号（`13812345678`）和 `token=Abc123` 必须自动脱敏后再落盘。
2. **文件滚动策略**：单文件一直追加迟早撑爆磁盘，需要同时支持按文件字节数、按行数、按时间窗口切分归档（`app.log` → `app.log.1` → `app.log.2`）。
3. **非侵入式扩展**：监控团队已经有一个现成的第三方结构体 `third_party::AlertWebhook`（不能修改它的类定义，也不想让它继承 `minilog::BaseSink`），希望能直接挂进 `Logger`。

如果按 `MonoLogger` 老路子硬塞，所有逻辑全挤进 `MonoLogger::log()`，每加一条过滤或一种滚动条件就要改公开头文件并触发 8/8 模块重编。在 `minilog` 里，我们按变化维度拆成四层：

```uml
label: minilog 第二阶段架构：FilterPipeline（过滤管线）→ AnySink（SBO 类型擦除）→ BaseSink（NVI 模板方法）→ RotationPolicy（滚动策略）
classes:
  - { id: LoggerImpl, name: 'Logger::Impl', x: 20, y: 26, w: 180, attrs: ['- filters_ : FilterPipeline', '- sinks_ : vector<AnySink>'] }
  - { id: FilterPipeline, x: 20, y: 185, w: 185, attrs: ['- filters_ : vector<FilterFn>'], ops: ['+ add(fn)', '+ apply(rec) : bool'] }
  - { id: AnySink, x: 260, y: 20, w: 200, attrs: ['- storage_[48] : byte', '- vptr_ : const VTable*'], ops: ['+ AnySink(T sink)', '+ log(rec)', '+ isInline() : bool'] }
  - { id: BaseSink, stereo: abstract, x: 525, y: 20, w: 205, ops: ['+ log(rec)', '+ flush()', '*# writeImpl(fmt, rec)', '*# flushImpl()'] }
  - { id: RotatingFileSink, x: 525, y: 185, w: 205, attrs: ['- policy_ : unique_ptr<...>'], ops: ['# writeImpl(fmt, rec)'] }
  - { id: RotationPolicy, stereo: interface, x: 265, y: 195, w: 205, ops: ['*+ shouldRotate(...) : bool'] }
rels:
  - { from: LoggerImpl, to: FilterPipeline, type: compose, tm: '1' }
  - { from: LoggerImpl, to: AnySink, type: compose, tm: '*' }
  - { from: AnySink, to: BaseSink, type: depend, label: 'shared_ptr / ADL' }
  - { from: RotatingFileSink, to: BaseSink, type: inherit }
  - { from: RotatingFileSink, to: RotationPolicy, type: compose, tm: '1' }
```

## 2. 前置过滤管线：扁平 `vector` 代替链表责任链

经典 GoF 责任链要求每个节点继承 `Handler` 基类、维护 `next_` 指针，容易写出指向悬空节点或拼成环的 bug。日志过滤天然是一条「顺序执行、返回 `false` 即短路丢弃、返回 `true` 允许就地改写」的不纯责任链，用 `std::vector<std::function<bool(LogRecord&)>>` 扁平存储比指针链表更紧凑、也不会成环：

- **`makeLevelFilter`**：级别门槛；
- **`makeModuleFilter`**：模块名白名单 / 黑名单；
- **`makeRateLimitFilter`**：按时间窗计数（内部状态通过 `std::shared_ptr` 持有，保证拷贝 `std::function` 时编译合法且多线程共享同一个计数窗口）；
- **`makeRedactFilter`**：单趟扫描就地替换 11 位手机号和 `token=...`；**如果没有命中敏感词，零修改、零堆分配**。

下面这段完整代码可以在浏览器里直接运行，验证四级过滤管线对消息的拦截与就地脱敏效果：

```cpp
// 《实战（二）》：扁平 FilterPipeline（级别 + 白名单 + 时间窗限流 + 脱敏）
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class Level { Trace, Debug, Info, Warn, Error };

struct LogRecord {
  uint64_t timestampMs = 0;
  Level level = Level::Info;
  std::string logger;
  std::string msg;
};

using FilterFn = std::function<bool(LogRecord&)>;

class FilterPipeline {
 public:
  void add(FilterFn fn) { filters_.push_back(std::move(fn)); }
  bool apply(LogRecord& rec) const {
    for (const auto& fn : filters_) {
      if (!fn(rec)) return false;
    }
    return true;
  }
 private:
  std::vector<FilterFn> filters_;
};

FilterFn makeModuleFilter(std::vector<std::string> allow) {
  auto list = std::make_shared<std::vector<std::string>>(std::move(allow));
  return [list](LogRecord& rec) {
    return std::find(list->begin(), list->end(), rec.logger) != list->end();
  };
}

FilterFn makeRateLimitFilter(uint32_t maxPerWin, uint64_t winMs) {
  struct State { uint64_t start = 0; uint32_t count = 0; bool init = false; };
  auto st = std::make_shared<State>();
  return [st, maxPerWin, winMs](LogRecord& rec) {
    if (!st->init || rec.timestampMs - st->start >= winMs) {
      st->init = true; st->start = rec.timestampMs; st->count = 1;
      return true;
    }
    if (st->count < maxPerWin) { ++st->count; return true; }
    return false;
  };
}

FilterFn makeRedactFilter() {
  auto isDig = [](char c) { return c >= '0' && c <= '9'; };
  return [isDig](LogRecord& rec) {
    std::string& s = rec.msg;
    for (std::size_t i = 0; i + 11 <= s.size(); ++i) {
      if (s[i] == '1' && s[i + 1] >= '3' && s[i + 1] <= '9') {
        bool ok = true;
        for (std::size_t k = 2; k < 11; ++k) ok = ok && isDig(s[i + k]);
        if (ok) { s.replace(i + 3, 4, "****"); i += 10; }
      }
    }
    std::size_t pos = s.find("token=");
    if (pos != std::string::npos) {
      std::size_t end = pos + 6;
      while (end < s.size() && (std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_')) {
        ++end;
      }
      if (end > pos + 6) s.replace(pos + 6, end - (pos + 6), "***");
    }
    return true;
  };
}

int main() {
  FilterPipeline pipe;
  pipe.add([](LogRecord& r) { return static_cast<int>(r.level) >= static_cast<int>(Level::Info); });
  pipe.add(makeModuleFilter({"auth", "pay"}));
  pipe.add(makeRateLimitFilter(2, 1000));
  pipe.add(makeRedactFilter());

  std::vector<LogRecord> inputs = {
      {100, Level::Debug, "auth", "debug ignored"},
      {100, Level::Info,  "cron", "module ignored"},
      {100, Level::Info,  "auth", "login 13812345678 token=Secret_99 ok"},
      {200, Level::Warn,  "pay",  "retry order=42"},
      {300, Level::Error, "pay",  "rate limited in same window"},
      {1200, Level::Info, "pay",  "new window passed"},
  };

  for (auto& rec : inputs) {
    bool pass = pipe.apply(rec);
    std::printf("[%s] (%llu ms) [%s] %s\n",
                pass ? "PASS" : "DROP",
                static_cast<unsigned long long>(rec.timestampMs),
                rec.logger.c_str(), rec.msg.c_str());
  }
}
```

```text
[DROP] (100 ms) [auth] debug ignored
[DROP] (100 ms) [cron] module ignored
[PASS] (100 ms) [auth] login 138****5678 token=*** ok
[PASS] (200 ms) [pay] retry order=42
[DROP] (300 ms) [pay] rate limited in same window
[PASS] (1200 ms) [pay] new window passed
```

在 `src/filter.cpp` 里，真正的 `makeRedactFilter()` 还多了一层守卫：如果整条消息没有手机号和 `token=`，**直接 `return true` 不调用 `rec.setMessage(...)`**，配合第 3 篇的 SBO 缓冲区保证干净日志全程零堆分配。

## 3. 内置 Sink：NVI 模板方法 + 滚动策略 + 工厂注册表

### 3.1 `BaseSink`：把锁和格式化收进基类非虚函数

所有内置 Sink（控制台 `ConsoleSink`、带 64 KB 缓冲的 `FileSink`、滚动文件 `RotatingFileSink`、测试用的 `MemorySink`）都要做一模一样的前置动作：**按级别过滤 → 加互斥锁 → 用 `PatternFormatter` 格式化成带换行的字符串 → 交给具体后端写出**。

按照第 4 章的 NVI（非虚接口）模板方法模式，`include/minilog/sink.h` 把公共流程固定在 `public` 非虚函数 `log()` / `flush()` 里，子类只需重写 `private` 虚函数 `writeImpl()` 与 `flushImpl()`——哪怕写新 Sink 的同事忘加锁、忘做级别判断，也不可能写出线程不安全的 Sink。

### 3.2 `RotationPolicy`：把切分条件从文件 I/O 里剥离

滚动文件本身只关心「重命名 `.log` → `.log.1` → `.log.2` 并重开新文件」，至于**什么时候该切分**则交给独立的策略接口 `RotationPolicy`：

- `SizeRotation(maxBytes)`：按写入字节数；
- `LineCountRotation(maxLines)`：按写入行数；
- `TimeRotation(intervalMs)`：按时间窗口间隔。

下面这段纯标准库演示在浏览器里直接模拟三种 `RotationPolicy` 下的文件归档过程：

```cpp
// 《实战（二）》：模板方法模拟 BaseSink + 策略模式 RotationPolicy 切分文件
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class RotationPolicy {
 public:
  virtual ~RotationPolicy() = default;
  virtual bool shouldRotate(std::size_t nextBytes, std::size_t lines,
                            uint64_t nowMs, uint64_t openedMs) const = 0;
};

class SizeRotation final : public RotationPolicy {
 public:
  explicit SizeRotation(std::size_t maxB) : maxB_(maxB) {}
  bool shouldRotate(std::size_t nextBytes, std::size_t, uint64_t, uint64_t) const override {
    return nextBytes > maxB_;
  }
 private:
  std::size_t maxB_;
};

class LineRotation final : public RotationPolicy {
 public:
  explicit LineRotation(std::size_t maxL) : maxL_(maxL) {}
  bool shouldRotate(std::size_t, std::size_t lines, uint64_t, uint64_t) const override {
    return lines >= maxL_;
  }
 private:
  std::size_t maxL_;
};

class SimRotatingSink {
 public:
  explicit SimRotatingSink(std::unique_ptr<RotationPolicy> p)
      : policy_(std::move(p)) {
    files_.emplace_back();
  }
  void write(const std::string& line, uint64_t nowMs) {
    if (openedMs_ == 0) openedMs_ = nowMs;
    if (lines_ > 0 && policy_->shouldRotate(bytes_ + line.size(), lines_, nowMs, openedMs_)) {
      files_.emplace_back();
      bytes_ = 0; lines_ = 0; openedMs_ = nowMs;
    }
    files_.back().push_back(line);
    bytes_ += line.size();
    ++lines_;
  }
  void dump(const char* title) const {
    std::printf("[%s] total files = %zu\n", title, files_.size());
    for (std::size_t i = 0; i < files_.size(); ++i) {
      std::printf("  segment#%zu: %zu lines\n", i, files_[i].size());
    }
  }
 private:
  std::unique_ptr<RotationPolicy> policy_;
  std::vector<std::vector<std::string>> files_;
  std::size_t bytes_ = 0, lines_ = 0;
  uint64_t openedMs_ = 0;
};

int main() {
  SimRotatingSink byLine(std::make_unique<LineRotation>(2));
  SimRotatingSink bySize(std::make_unique<SizeRotation>(16));
  for (int i = 0; i < 5; ++i) {
    byLine.write("12345678", static_cast<uint64_t>(i * 100));
    bySize.write("12345678", static_cast<uint64_t>(i * 100));
  }
  byLine.dump("LineRotation(2)");
  bySize.dump("SizeRotation(16B)");
}
```

```text
[LineRotation(2)] total files = 3
  segment#0: 2 lines
  segment#1: 2 lines
  segment#2: 1 lines
[SizeRotation(16B)] total files = 3
  segment#0: 2 lines
  segment#1: 2 lines
  segment#2: 1 lines
```

同时，`SinkFactory`（`src/sinks.cpp`） 作为按字符串注册的**简单工厂注册表**，内置 `"console"`、`"file"`、`"rotating"`、`"memory"`，并允许业务侧通过 `factory.registerType(...)` 无入侵扩展新的配置类型。

## 4. 非侵入式扩展：`AnySink`（48 B SBO 类型擦除）

`BaseSink` 给内置带互斥锁、文件句柄的重型 Sink 提供了统一骨架，但面对第 3 个需求——**「监控组已有 `third_party::AlertWebhook`，不能改它源码也不想强制继承 `BaseSink`」**——传统继承就显得太重了：

- 要么写个 `AlertWebhookAdapter : public BaseSink` 专门套一层；
- 要么容器被迫写成 `std::vector<std::unique_ptr<ISink>>`，哪怕一个只有 8 字节计数器的小 Sink 也要单独在堆上 `new` 一次。

`minilog` 在 `include/minilog/sink.h` 引入了第 5 章学过的 **48 字节 SBO 类型擦除包装器 `AnySink`**：

1. **三层优先级定制点（`PriorityTag<2/1/0>`）**：
   - 先找 ADL 自由函数 `write_sink(obj, rec)`（第三方库只需在自己命名空间补一个自由函数，原结构体一行不改）；
   - 再找成员 `obj.log(rec)`（同时为 `std::shared_ptr<BaseSink>` 提供了转发，所以内置 `MemorySink`/`FileSink` 的 `shared_ptr` 只有 16 B，直接放进 `AnySink` 栈内缓冲，不产生二次堆分配）；
   - 最后找成员 `obj.write(rec)`（极简结构体直接就能用）。
2. **两道工程防护**：
   - 模板构造函数加 `std::enable_if_t<!std::is_same_v<std::decay_t<T>, AnySink>>` 约束，防止劫持移动构造；
   - `kFitsInSbo<T>` 同时检查 `sizeof(T) <= 48`、`alignof(T) <= alignof(std::max_align_t)` 以及 **`std::is_nothrow_move_constructible_v<T>`**，不满足的自动透明落堆。

下面这段代码可在浏览器里直接运行，验证第三方结构体（ADL 定制点）、极简成员 `.write()` 结构体和超大 96 B 结构体的内联/落堆分流：

```cpp
// 《实战（二）》：48B SBO 类型擦除 AnySink —— 零继承接入第三方结构体
#include <cstddef>
#include <cstdio>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct LogRecord { std::string msg; };

namespace detail {
template <int N> struct Tag : Tag<N - 1> {};
template <> struct Tag<0> {};

template <typename T>
auto callWrite(T& o, const LogRecord& r, Tag<1>) -> decltype(write_sink(o, r), void()) {
  write_sink(o, r);
}
template <typename T>
auto callWrite(T& o, const LogRecord& r, Tag<0>) -> decltype(o.write(r), void()) {
  o.write(r);
}
}  // namespace detail

class AnySink {
  static constexpr std::size_t kCap = 48;
  struct VTable {
    void (*log)(void*, const LogRecord&);
    void (*move)(void*, void*) noexcept;
    void (*destroy)(void*) noexcept;
    bool isInline;
  };
  template <typename T>
  static constexpr bool kFits =
      sizeof(T) <= kCap && alignof(T) <= alignof(std::max_align_t) &&
      std::is_nothrow_move_constructible_v<T>;

  template <typename T>
  static const VTable* vtFor() noexcept {
    if constexpr (kFits<T>) {
      static const VTable t{
          [](void* p, const LogRecord& r) { detail::callWrite(*std::launder(reinterpret_cast<T*>(p)), r, detail::Tag<1>{}); },
          [](void* s, void* d) noexcept { ::new (d) T(std::move(*std::launder(reinterpret_cast<T*>(s)))); },
          [](void* p) noexcept { std::launder(reinterpret_cast<T*>(p))->~T(); },
          true};
      return &t;
    } else {
      static const VTable t{
          [](void* p, const LogRecord& r) { detail::callWrite(**std::launder(reinterpret_cast<T**>(p)), r, detail::Tag<1>{}); },
          [](void* s, void* d) noexcept { auto*& sp = *std::launder(reinterpret_cast<T**>(s)); ::new (d) T*(sp); sp = nullptr; },
          [](void* p) noexcept { delete *std::launder(reinterpret_cast<T**>(p)); },
          false};
      return &t;
    }
  }
  alignas(std::max_align_t) std::byte buf_[kCap]{};
  const VTable* vptr_ = nullptr;

 public:
  template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, AnySink>>>
  AnySink(T x) : vptr_(vtFor<std::decay_t<T>>()) {
    using U = std::decay_t<T>;
    if constexpr (kFits<U>) ::new (buf_) U(std::move(x));
    else ::new (buf_) U*(new U(std::move(x)));
  }
  AnySink(AnySink&& o) noexcept : vptr_(o.vptr_) {
    if (vptr_) { vptr_->move(o.buf_, buf_); vptr_->destroy(o.buf_); o.vptr_ = nullptr; }
  }
  ~AnySink() { if (vptr_) vptr_->destroy(buf_); }
  bool isInline() const noexcept { return vptr_ && vptr_->isInline; }
  void log(const LogRecord& r) { if (vptr_) vptr_->log(buf_, r); }
};

namespace third_party {
struct AlertWebhook { int prefixLen; };  // 第三方库结构体：无虚基类、源码不可改
void write_sink(AlertWebhook& w, const LogRecord& r) {
  std::printf("  [AlertWebhook] prefix=%d msg=%s\n", w.prefixLen, r.msg.c_str());
}
}  // namespace third_party

struct TinyCounter {
  int n = 0;
  void write(const LogRecord&) { ++n; std::printf("  [TinyCounter] count=%d\n", n); }
};

struct OversizedSink {
  char buf[96]{};
  void write(const LogRecord& r) { std::printf("  [OversizedSink] msg=%s\n", r.msg.c_str()); }
};

int main() {
  std::vector<AnySink> sinks;
  sinks.emplace_back(third_party::AlertWebhook{4});
  sinks.emplace_back(TinyCounter{});
  sinks.emplace_back(OversizedSink{});

  std::printf("inline flags: [%d, %d, %d] (sizeof AnySink=%zu B)\n",
              sinks[0].isInline(), sinks[1].isInline(), sinks[2].isInline(), sizeof(AnySink));
  LogRecord r{"disk usage 91%"};
  for (auto& s : sinks) s.log(r);
}
```

```text
inline flags: [1, 1, 0] (sizeof AnySink=64 B)
  [AlertWebhook] prefix=4 msg=disk usage 91%
  [TinyCounter] count=1
  [OversizedSink] msg=disk usage 91%
```

> 浏览器沙箱运行在 32 位 `wasm32`（`alignof(std::max_align_t) = 16`、`vptr` 4 B），因此 `sizeof(AnySink)` 为 `64 B`；在 64 位 macOS / Linux 本机编译运行则为 `56 B`（`48 B + 8 B`）。

## 5. 实测：传统虚基类 vs `AnySink` 的分派与堆分配

用 `projects/minilog/bench/bench_sink_dispatch.cpp` 实测 100 万次三种异构小 Sink（Apple clang 21 `-O2`，7 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 700 166" role="img" aria-label="100 万次异构 Sink 构造加调用加析构对比：传统 unique_ptr 触发 100 万次堆分配耗时 7.02 ns，热循环 0.91 ns；AnySink 48B SBO 触发 0 次堆分配耗时 1.40 ns，热循环 0.64 ns"><g style="font-family:var(--mono)"><text x="150" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">100 万次「构造 + log() + 析构」单次耗时（Clang -O2）</text><text x="142" y="50" text-anchor="end" style="fill:var(--fg);font-size:11px">unique_ptr&lt;ISink&gt;</text><rect x="150" y="34" width="320" height="22" rx="2" style="fill:var(--danger)"/><text x="477" y="49" text-anchor="start" style="fill:var(--danger);font-size:11px;font-weight:700">7.02 ns（1,000,000 次堆分配，热循环 0.91 ns）</text><text x="142" y="88" text-anchor="end" style="fill:var(--fg);font-size:11px">AnySink (48B SBO)</text><rect x="150" y="72" width="64" height="22" rx="2" style="fill:var(--accent)"/><text x="221" y="87" text-anchor="start" style="fill:var(--accent);font-size:11px;font-weight:700">1.40 ns（0 次堆分配，快 5.0×，热循环 0.64 ns）</text><text x="10" y="128" text-anchor="start" style="fill:var(--muted);font-size:10.5px">元素尺寸：unique_ptr&lt;ISink&gt; = 8 B（外加堆节点），AnySink = 56 B（48 B 栈内联缓冲 + 8 B VTable 指针）</text><text x="10" y="146" text-anchor="start" style="fill:var(--muted);font-size:10.5px">完整压测源码见 projects/minilog/bench/bench_sink_dispatch.cpp</text></g></svg>
</figure>

| 方案 | 元素 `sizeof` | 100 万次堆分配 | 构造+调用+析构 | 容器热循环分派 | 扩展第三方类 |
| --- | :---: | :---: | :---: | :---: | :---: |
| **`std::unique_ptr<ISink>`** | 8 B | 1,000,000 | 7.02 ns | 0.91 ns | 必须手写适配器子类 |
| **`AnySink`（48 B SBO）** | 56 B | **0** | **1.40 ns（快 5.0×）** | **0.64 ns** | **ADL 自由函数，零继承** |

更重要的是扩展代价：当后续要往 `minilog` 再加第 5 种自定义脱敏器、按时间切分的 `TimeRotation` 策略，或者接一个全新的内存环形告警 Sink 时，**`minilog` 已有核心文件改动行数为 0**——只需要写新策略/新结构体并注册给 `LoggerBuilder` 或 `SinkFactory` 即可。

## 6. 小结

- **`FilterPipeline`**：用 `std::vector<FilterFn>` 扁平顺序链替代指针链表责任链，同时干掉成环风险与每节点堆分配；
- **`BaseSink`（NVI 模板方法）+ `RotationPolicy`（策略）**：加锁与格式化骨架写死在基类，文件切分条件与具体落盘正交解耦；
- **`AnySink`（48 B SBO 类型擦除）**：兼顾值语义、零侵入扩展和零堆分配（100 万次构造+调用耗时 **1.40 ns vs 7.02 ns**，快 **5.0×**）。

到现在为止，`minilog` 还是同步写盘的：只要某一次业务日志刚好撞上页缓存回写或并发线程争锁，业务线程延迟就会被拉高。下一篇进入第三轮需求：**用命令队列 + SBO 零堆分配 `LogRecord` + 优雅停机**把它改造成高吞吐异步日志引擎。

## 7. 练习

1. **为什么 `makeRateLimitFilter` 要把计数状态包进 `std::shared_ptr<State>`？** 如果直接把 `std::mutex mtx;` 写进 lambda 捕获或结构体里再塞进 `std::function<bool(LogRecord&)>`，会发生什么？
2. **`RotatingFileSink` 的切分时机**：如果在 `writeImpl` 里改成「先写完这一行，再检查 `shouldRotate()` 并立刻重开新文件」，而不是「写入下一行前检查」，最后一个文件刚好写满 `maxLines` 时磁盘上会出现什么现象？
3. **SBO 退化边界**：`std::shared_ptr<MemorySink>` 为什么能直接放进 48 字节的 `AnySink` SBO 缓冲区而不会在 `AnySink` 层再次触发堆分配？如果换成捕获了 64 字节外部状态的 lambda，会走哪条分支？

<details>
<summary>点击查看答案</summary>

1. `std::function` 要求可调用对象满足**可拷贝构造（CopyConstructible）**，而 `std::mutex` 既不可拷贝也不可移动，直接按值放进可调用对象会导致编译失败；用 `std::shared_ptr<State>` 持有互斥锁与窗口状态，既让 lambda 保持可拷贝，又让拷贝出的过滤器副本共享同一份计数状态。
2. 刚写完第 `maxLines` 行时就会立刻把当前文件轮转成 `.1` 并创建一个 0 字节的空新 `app.log`。而在「写入下一行前检查 `linesWritten_ >= maxLines`」的写法下，第 `maxLines` 行留在当前文件里，直到第 `maxLines + 1` 行到来才轮转，确保活跃日志文件不会出现无意义的空文件。
3. 64 位平台上 `sizeof(std::shared_ptr<MemorySink>) == 16 B <= 48 B`，且移动构造标记了 `noexcept`，满足 `kFitsInSbo`，因此 `AnySink` 直接把它放进栈内缓冲区（`isInline() == true`，0 次额外 `new`）。如果传入一个 64 字节的大捕获结构体，超过 `kSboSize (48 B)`，编译期 `if constexpr (kFitsInSbo<U>)` 会透明切到堆分配分支（`isInline() == false`），对外接口与析构释放完全一致。

</details>
