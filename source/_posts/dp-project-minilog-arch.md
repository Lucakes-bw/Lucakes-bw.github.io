---
title: 设计模式实战（一）：从全局单例到干净的库边界
date: 2026-11-25 22:00:00
updated: 2026-11-25 22:00:00
categories:
  - 设计模式
  - 设计模式实战
tags:
  - 设计模式
  - 设计模式实战
  - 单例
  - 外观
  - 建造者
  - Pimpl
  - RAII
order: 1
description: 前五章逐个拆解了 23 种 GoF 模式和 4 个 C++ 惯用法，第六章把它们收进一个零外部依赖的现代 C++17 日志引擎 minilog。本篇从一份常见的全局单例大泥球 MonoLogger 起步，实测它在多模块工程里的三大病灶：头文件污染导致改 1 个私有字段触发 8/8 文件重编（1536.6 ms）并破坏 ABI（728 B 变成 736 B）、全局状态导致单元测试串扰、提前 return 漏掉 flush 丢失日志。随后用「依赖注入 + 可注入注册表」替换全局单例，用「外观 + Pimpl 防火墙」将公开头文件与标准库重型实现隔离（sizeof 恒定 8 B，增量重编仅 1 文件 340.2 ms，快 4.5 倍），用「建造者模式」在 build() 统一拦截非法参数，最后用 RAII 与 ScopeGuard 兜底退出刷盘。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：把前五章学过的模式放进一个真实可工程化的 C++17 基础库 **`minilog`** 里，跑通第一阶段：用**可注入注册表 + 依赖注入**淘汰全局单例；用**外观（Facade）+ Pimpl** 建起严格的编译防火墙与稳定 ABI；用**建造者（Builder）**收拢多参数配置与统一校验；用 **RAII + ScopeGuard** 保证多出口路径下零泄漏、零丢日志。
>
> **前置知识**：《单例模式》《外观模式》《建造者模式》《RAII 与 Scope Guard》《Pimpl 惯用法》。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整代码、6 模块编译测试与单测见 `projects/minilog/`（`bash measure.sh` 一键复现）。

## 1. 起点：一份常见的「全局单例」大泥球

几乎每个 C++ 项目初期都会随手写一个不到一百行的日志类：公开头文件里 `#include <fstream>`、`<mutex>`、`<sstream>`，配一个 `MonoLogger::instance()` 全局单例，用起来极其顺手：

```cpp
// projects/minilog/legacy/monolithic_logger.h（节选）
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class MonoLogger {
 public:
  static MonoLogger& instance() {
    static MonoLogger inst;
    return inst;
  }
  void setLevel(Level l);
  void setOutputPath(const std::string& path);
  void log(Level l, const std::string& module, const std::string& msg);
  // ...
 private:
  Level minLevel_ = Info;
  bool consoleOn_ = false;
  std::string outputPath_;
  std::ofstream file_;
  mutable std::mutex mtx_;
  std::vector<std::string> memBuffer_;
};
```

当 `auth`、`order`、`payment`、`inventory`、`notify`、`metrics` 六个业务模块全包含这个头文件后，只要一进真实迭代，三个工程问题就会同时炸开：

1. **测试彼此污染**：进程里只有一个全局实例，测试 A 把全局级别调成 `Error` 或忘了清空缓冲，紧接着跑的测试 B 就会莫名其妙失败；
2. **编译雪崩与 ABI 破碎**：仅仅在 `MonoLogger` 的 `private` 区加一个 `int retryCount_ = 0;`，所有包含它的 `.cpp` 必须全部重编，动态库暴露的 `sizeof(MonoLogger)` 也会跟着变化；
3. **异常或提前返回丢日志**：业务函数半路 `return -1`，既可能忘了写回滚日志，也可能在异常退出前没把缓冲刷新出去。

下面这段精简代码可以在浏览器里直接运行，复现第一个病灶——**全局单例如何污染单元测试**：

```cpp
// 《实战（一）》：问题版——全局单例导致测试用例之间状态串扰
#include <cstdio>
#include <string>
#include <vector>

enum class Level { Debug, Info, Error };

class GlobalLogger {
 public:
  static GlobalLogger& instance() {
    static GlobalLogger inst;
    return inst;
  }
  void setLevel(Level l) { minLevel_ = l; }
  void log(Level l, const std::string& msg) {
    if (static_cast<int>(l) >= static_cast<int>(minLevel_)) {
      lines_.push_back(msg);
    }
  }
  const std::vector<std::string>& lines() const { return lines_; }

 private:
  GlobalLogger() = default;
  Level minLevel_ = Level::Info;
  std::vector<std::string> lines_;
};

void testSilenceDebug() {
  GlobalLogger::instance().setLevel(Level::Error);  // 测试 A 改了全局状态，忘了恢复
  GlobalLogger::instance().log(Level::Info, "A: ignored info");
  std::printf("[Test A] recorded = %zu\n", GlobalLogger::instance().lines().size());
}

void testNormalInfo() {
  // 测试 B 以为默认级别是 Info，结果受测试 A 残留状态影响，一条也没记下来！
  GlobalLogger::instance().log(Level::Info, "B: payment ok");
  std::printf("[Test B] recorded = %zu (expected 1)\n",
              GlobalLogger::instance().lines().size());
}

int main() {
  testSilenceDebug();
  testNormalInfo();
}
```

```text
[Test A] recorded = 0
[Test B] recorded = 0 (expected 1)
```

一旦把测试拆进不同的 `.cpp` 文件或开启并发测试框架，哪怕每个测试末尾都记得调 `resetForTest()`，只要中间提前断言失败或换个执行顺序，全局单例的幽灵状态还是会把整个测试集拖垮。

## 2. 第一步改造：淘汰单例，用 Pimpl + 外观立住边界

### 2.1 去单例：局部变量 + 可注入 `LoggerRegistry`

《单例模式》一章给出的准则同样适用于 `minilog`：**对象确实需要按名字查找时，把「按名索引的容器」做成普通类 `LoggerRegistry`，而不是把 `Logger` 本身做成全局单例**。

- 单元测试或短生命周期模块：直接用 `LoggerBuilder` 构造局部 `std::unique_ptr<Logger>`，用完析构即销毁，天然隔离；
- 多模块共享同一个命名日志器（如 `"auth"`、`"audit"`）：由程序入口创建一个 `LoggerRegistry` 实例，再按引用注入给需要它的服务。测试时各自创建独立的 `regA`、`regB`，零全局共享状态。

### 2.2 编译防火墙：`minilog/logger.h` 里什么都不暴露

作为全项目几乎每个 `.cpp` 都会 `#include` 的公开头文件，`include/minilog/logger.h` 只依赖极轻量的标准头文件（`<memory>`、`<string>`、`<string_view>`、`<vector>`、`<functional>`），**刻意禁止引入 `<fstream>`、`<mutex>`、`<thread>`、`<condition_variable>`、`<sstream>`、`<iostream>`**：

```cpp
// projects/minilog/include/minilog/logger.h（关键骨架）
namespace minilog {

struct LogRecord;
class AnySink;

class Logger {
 public:
  ~Logger();
  Logger(Logger&&) noexcept;
  Logger& operator=(Logger&&) noexcept;

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void log(Level lvl, std::string_view msg);
  void info(std::string_view msg);
  void flush();
  // ...
 private:
  friend class LoggerBuilder;
  struct Impl;                   // 前向声明，内部状态全部藏进 src/logger.cpp
  explicit Logger(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace minilog
```

这里同时落了两个模式和一个 C++ 关键细节：
- **外观模式（Facade）**：调用方只需要认识 `Logger::info(...)`、`Logger::flush()`，不需要知道背后挂了多少个过滤规则、同步还是异步队列、缓冲文件还是滚动策略；
- **Pimpl 惯用法**：`Logger::Impl`（持有过滤器链、同步/异步工作器、原子计数器、互斥锁）与 `LoggerBuilder::State`、`LoggerRegistry::Impl` 全部封在 `src/logger.cpp`；
- **不完整类型析构坑**：`~Logger()`、移动构造、移动赋值**绝不能在头文件里写 `= default`**，必须只在 `logger.h` 声明，然后放到 `Impl` 已完整定义的 `src/logger.cpp` 里写 `Logger::~Logger() = default;`。

```uml
label: minilog 第一阶段架构：Logger（外观 + Pimpl）、LoggerBuilder（建造者）与 LoggerRegistry（可注入注册表）
classes:
  - { id: Client, x: 20, y: 50, w: 120 }
  - { id: LoggerRegistry, x: 200, y: 20, w: 230, attrs: ['- impl_ : unique_ptr<Impl>'], ops: ['+ registerLogger(l) : bool', '+ get(name) : shared_ptr<Logger>', '+ flushAll()'] }
  - { id: Logger, x: 200, y: 180, w: 230, attrs: ['- impl_ : unique_ptr<Impl>'], ops: ['+ log(lvl, msg)', '+ info(msg)', '+ flush()'] }
  - { id: Impl, name: 'Logger::Impl', x: 510, y: 180, w: 200, attrs: ['- filters_ : FilterPipeline', '- syncSinks_ : vector<AnySink>', '- asyncWorker_ : unique_ptr<...>'] }
  - { id: LoggerBuilder, x: 510, y: 20, w: 210, attrs: ['- state_ : unique_ptr<State>'], ops: ['+ name(s) : LoggerBuilder&', '+ level(l) : LoggerBuilder&', '+ build() : BuildResult'] }
rels:
  - { from: Client, to: LoggerBuilder, type: depend, label: '装配' }
  - { from: Client, to: LoggerRegistry, type: depend }
  - { from: LoggerRegistry, to: Logger, type: aggregate, tm: '*', label: '按名管理' }
  - { from: Logger, to: Impl, type: compose, tm: '1', label: 'Pimpl 边界' }
  - { from: LoggerBuilder, to: Logger, type: depend, label: 'build()' }
```

### 2.3 实测：改 1 个私有字段后的编译代价与 ABI 稳定性

用 `projects/minilog/legacy/compile_bench/run_compile_bench.sh` 在真实多模块工程（`auth.cpp`、`order.cpp`、`payment.cpp`、`inventory.cpp`、`notify.cpp`、`metrics.cpp`、`main.cpp` + 库实现）上对比 Apple clang 21 `-O2` 五轮中位数：

<figure class="diagram">
<svg viewBox="0 0 700 176" role="img" aria-label="在日志类私有区新增一个 int 字段后增量重编耗时对比：普通头文件 MonoLogger 需要重编全部 8 个 cpp 耗时 1536.6 ms 且 sizeof 从 728 B 变成 736 B；Pimpl 头文件 minilog 只需重编 1 个 logger.cpp 耗时 340.2 ms 且 sizeof 恒定 8 B"><g style="font-family:var(--mono)"><text x="155" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">私有区新增 1 个 int 字段后的增量重编耗时（5 轮中位数）</text><text x="147" y="50" text-anchor="end" style="fill:var(--fg);font-size:11px">MonoLogger (普通头文件)</text><rect x="155" y="34" width="320" height="22" rx="2" style="fill:var(--danger)"/><text x="482" y="49" text-anchor="start" style="fill:var(--danger);font-size:11px;font-weight:700">1536.6 ms（重编 8/8 文件，ABI 728→736 B）</text><text x="147" y="88" text-anchor="end" style="fill:var(--fg);font-size:11px">minilog (Pimpl 防火墙)</text><rect x="155" y="72" width="71" height="22" rx="2" style="fill:var(--accent)"/><text x="233" y="87" text-anchor="start" style="fill:var(--accent);font-size:11px;font-weight:700">340.2 ms（仅重编 1 个 cpp，快 4.5×，ABI 恒为 8 B）</text><text x="10" y="128" text-anchor="start" style="fill:var(--muted);font-size:10.5px">全量冷编（所有 cpp）：MonoLogger 8 文件 1556.8 ms；minilog 12 文件（含异步与过滤完整实现）2311.0 ms</text><text x="10" y="146" text-anchor="start" style="fill:var(--muted);font-size:10.5px">实验环境：Apple clang 21.0.0 -O2，6 个业务模块 + main.cpp，完整脚本见 projects/minilog/legacy/compile_bench/</text></g></svg>
</figure>

| 指标 | 普通头文件 `MonoLogger` | Pimpl 头文件 `minilog::Logger` |
| --- | :---: | :---: |
| **公开头文件暴露的重型标准库** | `<fstream>` `<mutex>` `<sstream>` `<thread>` `<iostream>` | **0 个**（仅 `<memory>` `<string_view>` 等） |
| **全量编译耗时** | 1556.8 ms（8/8 文件） | 2311.0 ms（12/12 文件，含完整引擎） |
| **改 1 个私有字段后增量重编数** | **8 / 8**（全部业务模块牵连重编） | **1**（仅 `src/logger.cpp`，业务模块 0 重编） |
| **改 1 个私有字段后增量耗时** | 1536.6 ms | **340.2 ms（快 4.5×）** |
| **对象尺寸 `sizeof`（ABI）** | 728 B → **736 B（ABI 破坏）** | **8 B → 8 B（ABI 恒定）** |

## 3. 第二步改造：建造者收拢参数并集中校验

日志器初始化参数一多（名字、最低级别、格式串、多个输出目标、是否异步、队列容量、溢出策略），如果直接写成一个 8 参数构造函数，不仅调用方一眼看不出每个 `true`/`4096` 代表什么，更麻烦的是**参数之间的合法性校验散落在各处**。

在 `minilog` 里，`LoggerBuilder` 负责两件事：
1. 提供流畅的链式调用（`.name(...)` / `.level(...)` / `.pattern(...)` / `.addConsoleSink(...)` / `.async(...)`）；
2. 在 `build()` 出口处**一次性拦截非法配置**（空名字、没有配置任何 Sink、格式串里有未识别的占位符如 `%Q`、开启异步但队列容量设为 `0`、文件路径为空等），返回显式 `BuildResult{logger, error}`，**不靠异常也能安全报错**。

下面的简化版可在浏览器里直接运行验证建造者校验与可注入 `LoggerRegistry`：

```cpp
// 《实战（一）》：建造者校验 + 可注入 LoggerRegistry（消除测试串扰）
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class Level { Trace, Debug, Info, Warn, Error };

bool validatePattern(std::string_view pat, std::string& err) {
  if (pat.empty()) { err = "pattern must not be empty"; return false; }
  for (std::size_t i = 0; i < pat.size(); ++i) {
    if (pat[i] != '%') continue;
    if (i + 1 >= pat.size()) { err = "dangling '%'"; return false; }
    char c = pat[++i];
    if (c != 'T' && c != 'L' && c != 'n' && c != 'v' && c != '%') {
      err = std::string("unknown pattern specifier '%") + c + "'";
      return false;
    }
  }
  return true;
}

class Logger {
 public:
  Logger(std::string name, Level lvl, std::string pat)
      : name_(std::move(name)), level_(lvl), pattern_(std::move(pat)) {}
  const std::string& name() const { return name_; }
  void log(Level l, std::string_view msg) {
    if (static_cast<int>(l) >= static_cast<int>(level_)) {
      records_.push_back("[" + name_ + "] " + std::string(msg));
    }
  }
  std::size_t count() const { return records_.size(); }

 private:
  std::string name_;
  Level level_;
  std::string pattern_;
  std::vector<std::string> records_;
};

struct BuildResult {
  std::unique_ptr<Logger> logger;
  std::string error;
  explicit operator bool() const { return logger != nullptr; }
};

class LoggerBuilder {
 public:
  LoggerBuilder& name(std::string n) { name_ = std::move(n); return *this; }
  LoggerBuilder& level(Level l) { level_ = l; return *this; }
  LoggerBuilder& pattern(std::string p) { pattern_ = std::move(p); return *this; }
  LoggerBuilder& addMemorySink() { hasSink_ = true; return *this; }
  LoggerBuilder& async(bool on, std::size_t cap = 1024) {
    asyncOn_ = on; queueCap_ = cap; return *this;
  }

  BuildResult build() {
    if (name_.empty()) return {nullptr, "logger name must not be empty"};
    std::string patErr;
    if (!validatePattern(pattern_, patErr)) return {nullptr, patErr};
    if (!hasSink_) return {nullptr, "logger requires at least one sink"};
    if (asyncOn_ && queueCap_ == 0) return {nullptr, "async queueCapacity must be > 0"};
    return {std::make_unique<Logger>(name_, level_, pattern_), ""};
  }

 private:
  std::string name_;
  Level level_ = Level::Info;
  std::string pattern_ = "[%T] [%L] [%n] %v";
  bool hasSink_ = false;
  bool asyncOn_ = false;
  std::size_t queueCap_ = 0;
};

class LoggerRegistry {
 public:
  bool registerLogger(std::shared_ptr<Logger> l) {
    if (!l) return false;
    return map_.emplace(l->name(), std::move(l)).second;
  }
  std::shared_ptr<Logger> get(const std::string& name) const {
    auto it = map_.find(name);
    return it == map_.end() ? nullptr : it->second;
  }
 private:
  std::unordered_map<std::string, std::shared_ptr<Logger>> map_;
};

int main() {
  auto bad1 = LoggerBuilder().name("app").pattern("[%T] %Q %v").addMemorySink().build();
  auto bad2 = LoggerBuilder().name("app").addMemorySink().async(true, 0).build();
  std::printf("bad1: %s\n", bad1.error.c_str());
  std::printf("bad2: %s\n", bad2.error.c_str());

  // 两个隔离的注册表，测试 A 和测试 B 互不干扰
  LoggerRegistry regA, regB;
  regA.registerLogger(LoggerBuilder().name("svc").level(Level::Error).addMemorySink().build().logger);
  regB.registerLogger(LoggerBuilder().name("svc").level(Level::Info).addMemorySink().build().logger);

  regA.get("svc")->log(Level::Info, "filtered in A");
  regB.get("svc")->log(Level::Info, "recorded in B");
  std::printf("regA count = %zu, regB count = %zu\n",
              regA.get("svc")->count(), regB.get("svc")->count());
}
```

```text
bad1: unknown pattern specifier '%Q'
bad2: async queueCapacity must be > 0
regA count = 0, regB count = 1
```

## 4. 第三步改造：RAII + ScopeGuard 保证退前必刷盘

即便把接口包好了，还有一个坑每天都在生产代码里发生：函数里有三条提前 `return` 路径，或者某条支路忘掉调 `flush()`，一旦进程崩在前面或作用域结束未刷盘，最关键的现场错误日志就丢在内存缓冲区里。

`minilog` 靠两道防线彻底消灭这类人为疏忽：
1. **`Logger::Impl` 析构（RAII）**：同步模式下逐个调用所有 Sink 的 `flush()`（`FileSink` 析构也会把用户态缓冲刷入文件并 `fclose`）；异步模式下析构 `AsyncWorker` 会把队列排空再 `join()` 线程。
2. **`ScopeGuard` 事务日志兜底**：针对一次性操作，用 `include/minilog/scope_guard.h` 在进入关键区时挂好失败回滚日志，正常提交后调 `guard.dismiss()` 撤销：

```cpp
// 《实战（一）》：RAII 自动刷盘 + ScopeGuard 保证提前返回必记回滚并落盘
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

template <typename F>
class ScopeGuard {
 public:
  explicit ScopeGuard(F&& fn) : fn_(std::move(fn)), active_(true) {}
  ~ScopeGuard() { if (active_) fn_(); }
  void dismiss() noexcept { active_ = false; }
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
 private:
  F fn_;
  bool active_;
};

template <typename F>
ScopeGuard<std::decay_t<F>> makeScopeGuard(F&& fn) {
  return ScopeGuard<std::decay_t<F>>(std::forward<F>(fn));
}

class BufferedLogger {
 public:
  explicit BufferedLogger(std::vector<std::string>& disk) : disk_(disk) {}
  ~BufferedLogger() { flush(); }  // 离开作用域无论从哪条 return 退出都自动刷盘
  void log(std::string line) { buf_.push_back(std::move(line)); }
  void flush() {
    for (auto& s : buf_) disk_.push_back(std::move(s));
    buf_.clear();
  }
 private:
  std::vector<std::string>& disk_;
  std::vector<std::string> buf_;
};

int runTx(std::vector<std::string>& disk, bool failStep2) {
  BufferedLogger logger(disk);
  auto rollbackLog = makeScopeGuard([&]() { logger.log("ERROR tx rolled back"); });

  logger.log("INFO  step 1 ok");
  if (failStep2) return -1;  // 提前返回：先触发 rollbackLog，再触发 ~BufferedLogger 刷盘

  logger.log("INFO  step 2 committed");
  rollbackLog.dismiss();
  return 0;
}

int main() {
  std::vector<std::string> disk1, disk2;
  runTx(disk1, true);
  runTx(disk2, false);
  std::printf("fail path disk lines (%zu):\n", disk1.size());
  for (const auto& s : disk1) std::printf("  %s\n", s.c_str());
  std::printf("ok   path disk lines (%zu):\n", disk2.size());
  for (const auto& s : disk2) std::printf("  %s\n", s.c_str());
}
```

```text
fail path disk lines (2):
  INFO  step 1 ok
  ERROR tx rolled back
ok   path disk lines (2):
  INFO  step 1 ok
  INFO  step 2 committed
```

注意这里利用了 C++ 栈对象的**构造逆序析构规则（LIFO）**：`logger` 先构造、`rollbackLog` 后构造，因此提前 `return -1` 时一定先执行 `rollbackLog` 的析构（把回滚记录推进缓冲区），随后再执行 `logger` 的析构（把缓冲里的两条记录一起刷进磁盘）。在 `projects/minilog/tests/test_arch.cpp` 中，同样场景配合真实 `FileSink` 已通过 Release 与 ASan/UBSan 验证。

## 5. 小结

第一阶段我们把一个「越改越不敢动」的全局单例改造出了干净的工程边界：
- **去单例**：用局部 `Logger` 与可注入 `LoggerRegistry` 代替全局唯一实例，测试与多租户实例完全隔离；
- **外观 + Pimpl**：`minilog/logger.h` 零重型标准库头文件泄漏，`sizeof(Logger)` 恒为 8 B，改内部实现仅重编 1 个 `.cpp`（340.2 ms vs 1536.6 ms，快 **4.5×**）；
- **建造者**：把多选项装配与参数校验集中在 `build()`，拦截无效格式串、空路径、非法队列容量；
- **RAII + ScopeGuard**：让退出刷盘和异常回滚日志不依赖程序员记性。

下一篇进入第二轮需求演进：怎么给日志加上**限流 / 脱敏过滤管线**、**按大小/行数/时间切分的滚动文件**，以及让**第三方结构体零继承直接接入**的 SBO 类型擦除 `AnySink`。

## 6. 练习

1. **Pimpl 编译错定位**：如果在 `include/minilog/logger.h` 中把 `~Logger();` 删掉（依赖编译器自动生成隐式内联析构函数），在调用方 `main.cpp` 只 `#include "minilog/logger.h"` 编译时会报什么错？为什么？
2. **注册表线程安全**：`projects/minilog/src/logger.cpp` 里 `LoggerRegistry::flushAll()` 为什么先在锁内把 `shared_ptr<Logger>` 拷贝到局部 `vector`，释放锁后再逐个调用 `l->flush()`？如果直接持锁循环调用会有什么风险？
3. **Builder 扩展校验**：如果要在 `LoggerBuilder` 新增 `.maxMessageBytes(std::size_t n)` 选项并禁止设置为 `0`，需要改动多少个文件？调用方的 `auth.cpp` 等 6 个业务模块需要重新编译吗？

<details>
<summary>点击查看答案</summary>

1. 会报错 `invalid application of 'sizeof' to an incomplete type 'minilog::Logger::Impl'`（在 `std::default_delete<Logger::Impl>` 内部）。因为编译器在未见 `Logger::Impl` 完整定义的调用方 `.cpp` 里生成默认析构函数，实例化 `std::unique_ptr<Impl>::~unique_ptr()` 时必须知道 `Impl` 的完整大小与析构函数，因此析构与移动函数必须在看见 `Impl` 定义的 `src/logger.cpp` 中显式定义。
2. `l->flush()` 可能会等待异步工作线程完成或获取 Sink 内部锁；如果 `flushAll()` 一边持着 `LoggerRegistry` 的互斥锁一边调用外部可阻塞的 `flush()`，一旦某个回调或工作线程反过来访问注册表就会形成锁序死锁，也会长时间阻塞其他线程查表。拷贝一份 `shared_ptr` 既能延长对象生命周期避免悬空，又能把锁粒度缩到最小。
3. 只需要改公开头文件 `logger.h`（加一个声明）和 `src/logger.cpp`（改 `State` 与 `build()` 校验）。因为改变了公开头文件 `logger.h`，依赖它的业务模块这次需要重编；但之后如果在 `Logger::Impl` 内部调整校验细节或截断实现，业务模块就不再重编。

</details>
