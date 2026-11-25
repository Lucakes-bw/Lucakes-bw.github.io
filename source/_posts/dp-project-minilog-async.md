---
title: 设计模式实战（三）：异步队列、零堆分配与高并发压测
date: 2026-11-25 23:00:00
updated: 2026-11-25 23:00:00
categories:
  - 设计模式
  - 设计模式实战
tags:
  - 设计模式
  - 设计模式实战
  - 命令模式
  - 策略模式
  - CRTP
  - 小对象优化
  - 异步日志
order: 3
description: 高并发场景下，同步写盘让多个业务线程挤在同一把格式化与文件锁上：Apple clang 21 -O2 实测 4 线程写 40 万条 50B 日志，同步缓冲耗时 89.22 ms（448 万 ops/s），同步无缓冲直写更是跌到 1039.52 ms。第三阶段把每条日志封装成带 128 字节 SBO 内联缓冲的 LogRecord 命令对象，通过有界环形队列、三种可切换背压策略（Block / DropNewest / OverwriteOldest）和后台单线程批量消费重构为异步日志，配合预编译 std::variant 格式串与 RAII 优雅停机：4 线程吞吐提升至 960 万 ops/s（41.67 ms，比同步缓冲快 2.1×、比直写快 25×），1 万条 50B 热路径堆分配从 std::string 队列的 10,001 次降为 1 次（生产者侧 0 次），并通过 ASan/UBSan 与 TSan 零竞争验证。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：把 `minilog` 改造成高吞吐异步日志引擎：用 **128 B SBO `LogRecord`（命令对象）**消灭生产侧每条日志的堆分配；用**有界环形队列 + 可切换背压策略（`Block` / `DropNewest` / `OverwriteOldest`）**隔离业务线程与磁盘 I/O；用**预编译 `std::variant` 格式串**提速渲染热路径；用实测看清异步什么时候真正快、以及为什么外层 Sink 没强行上 CRTP。
>
> **前置知识**：《命令模式》《策略模式》《CRTP 奇异递归模板》《RAII 与 Scope Guard》。
>
> **编译环境**：`g++ / clang++ -std=c++17 -pthread`，完整实现与测试压测见 `projects/minilog/`（`src/async_worker.cpp`、`tests/test_async.cpp`、`bench/bench_async_throughput.cpp`）。

## 1. 瓶颈：多线程下为什么「同步写盘」顶不住

前两篇里，`Logger::log()` 已经把逻辑理得很干净了，但在高并发服务里还有最后一堵墙：**调用 `logger->info(...)` 的业务线程，亲自做格式化并拿着锁去写盘**。

1. **锁竞争**：4 个工作线程同时打日志，哪怕文件开启了 64 KB 用户态缓冲，四条线也必须排队抢 `syncMtx` 和 `BaseSink::mtx_`，在锁内做格式拼接、`memcpy` 甚至触发系统的 `write(2)`；
2. **尾延迟毛刺**：一旦某个请求刚好赶上 64 KB 缓冲区满刷新或者操作系统的页缓存回写（脏页回刷），这条请求线程就会被磁盘 I/O 卡住几百微秒甚至几毫秒；
3. **堆分配风暴**：如果为了解耦直接把 `std::string` 扔进异步队列，50 字节左右的常见日志会超出 `libc++` 的 22 字节 SSO 阈值——每秒 100 万条日志就意味着 **每秒 100 万次 `new` 和 `delete`**，跨线程分配释放还会进一步拖垮分配器。

解法是典型的**命令模式 + 生产者/消费者异步工作器**：前端线程只把日志打包成值对象放进预分配的环形队列就立刻返回；格式化和落盘全部交给唯一的后台消费线程批量处理。

```uml
label: minilog 异步管线时序：生产者写 SBO 槽位（仅从空转非空时唤醒），后台线程出锁批量落盘，析构时排空退出
participants:
  - { id: p, name: 'Producer (业务线程)' }
  - { id: q, name: 'AsyncWorker (环形队列)' }
  - { id: w, name: 'WorkerThread (后台)' }
  - { id: s, name: 'FileSink (64KB缓冲)' }
messages:
  - { from: p, to: q, text: 'enqueue(LogRecord SBO)', n: 1 }
  - { from: q, to: p, text: 'if count==1 notify_one()', type: reply }
  - { from: w, to: q, text: 'pop batch (<=64)', n: 2 }
  - { from: q, to: w, text: 'release lock', type: reply }
  - { from: w, to: s, text: 'log(rec) × batch', n: 3 }
  - { from: p, to: q, text: '~AsyncWorker() stopping_=true', n: 4 }
  - { from: w, to: q, text: 'drain remaining', n: 5 }
  - { from: w, to: s, text: 'flush() + join()', n: 6 }
frags:
  - { from: 2, to: 4, label: 'loop [批处理在锁外执行]' }
  - { from: 5, to: 7, label: 'RAII 优雅停机（零丢失）' }
```

## 2. 零堆分配的消息对象：`LogRecord`（128 B SBO）

`include/minilog/record.h` 定义了在队列里传递的命令对象 `LogRecord`：
- **内联名缓冲** `nameBuf_[32]` 和 **128 字节内联消息缓冲** `inlineBuf_[128]`；
- 消息长度 `<= 128 B` 时直接拷入 `inlineBuf_`，**0 次堆分配**；超过 128 B 时透明分配 `heapBuf_`；
- 关键性能细节：**手写 `noexcept` 移动构造与移动赋值只拷贝 `msgLen_` 字节**（而不是编译器默认生成的那样傻拷贝整块 128 B + 32 B 数组），且构造时不把 `inlineBuf_` 整体清零。

下面这段完整代码可以在浏览器里直接运行，观察 50 B 短消息和 180 B 长消息在 SBO 与移动语义下的行为差异：

```cpp
// 《实战（三）》：128B SBO 日志条目 LogRecord（小消息 0 堆分配，只搬运有效长度）
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

struct LogRecord {
  static constexpr std::size_t kNameCap   = 32;
  static constexpr std::size_t kInlineCap = 128;

  uint64_t timestampMs = 0;
  uint8_t  nameLen_    = 0;
  uint32_t msgLen_     = 0;
  char nameBuf_[kNameCap];
  char inlineBuf_[kInlineCap];
  std::unique_ptr<char[]> heapBuf_;

  LogRecord() noexcept = default;

  LogRecord(LogRecord&& other) noexcept
      : timestampMs(other.timestampMs),
        nameLen_(other.nameLen_),
        msgLen_(other.msgLen_),
        heapBuf_(std::move(other.heapBuf_)) {
    if (nameLen_ > 0) std::memcpy(nameBuf_, other.nameBuf_, nameLen_);
    if (!heapBuf_ && msgLen_ > 0) std::memcpy(inlineBuf_, other.inlineBuf_, msgLen_);
    other.nameLen_ = 0;
    other.msgLen_ = 0;
  }

  void setLoggerName(std::string_view name) noexcept {
    nameLen_ = static_cast<uint8_t>(std::min(name.size(), kNameCap - 1));
    if (nameLen_ > 0) std::memcpy(nameBuf_, name.data(), nameLen_);
  }

  void setMessage(std::string_view msg) {
    msgLen_ = static_cast<uint32_t>(msg.size());
    if (msg.size() <= kInlineCap) {
      heapBuf_.reset();
      if (!msg.empty()) std::memcpy(inlineBuf_, msg.data(), msg.size());
      return;
    }
    heapBuf_ = std::make_unique<char[]>(msg.size() + 1);
    std::memcpy(heapBuf_.get(), msg.data(), msg.size());
    heapBuf_[msg.size()] = '\0';
  }

  bool isHeap() const noexcept { return static_cast<bool>(heapBuf_); }
  std::string_view message() const noexcept {
    return heapBuf_ ? std::string_view(heapBuf_.get(), msgLen_)
                    : std::string_view(inlineBuf_, msgLen_);
  }
};

int main() {
  LogRecord small;
  small.setLoggerName("pay");
  small.setMessage("order=842103 status=paid latency=12ms sku=gpu-a100");

  LogRecord big;
  big.setLoggerName("audit");
  big.setMessage(std::string(180, 'x'));

  LogRecord moved = std::move(small);
  std::printf("sizeof(LogRecord)=%zu B\n", sizeof(LogRecord));
  std::printf("moved small: len=%zu isHeap=%d msg=%.*s\n",
              moved.message().size(), moved.isHeap(),
              static_cast<int>(moved.message().size()), moved.message().data());
  std::printf("big record : len=%zu isHeap=%d\n", big.message().size(), big.isHeap());
}
```

```text
sizeof(LogRecord)=184 B
moved small: len=50 isHeap=0 msg=order=842103 status=paid latency=12ms sku=gpu-a100
big record : len=180 isHeap=1
```

## 3. 环形队列、三种背压策略与按需唤醒

队列满了怎么办？不同业务对**零丢失**与**低延迟**的偏好完全不同，因此 `include/minilog/async.h` 用**策略枚举 `OverflowPolicy`** 把溢出行为开放给用户选择：

| 策略 | 队列满时动作 | 适用场景 |
| --- | --- | --- |
| **`Block`**（默认） | `notFull_` 条件变量阻塞等待消费 | 账务、审计等「宁可慢一点、绝不能丢」的场景 |
| **`DropNewest`** | 立即返回 `false` 丢弃新日志并原子计数 | 核心交易热路径「绝不能被磁盘反压卡住延迟」 |
| **`OverwriteOldest`** | 覆写队头最旧槽位并推进头尾指针 | 崩溃排查「只关心最后 N 条最新现场」 |

`src/async_worker.cpp` 里还有两个决定吞吐的关键细节：

1. **按需唤醒（减少 99% 条件变量开销）**：只有队列从空变非空（`count_ == 0 -> 1`）时才调用 `notEmpty_.notify_one()`；如果队列里已经有待处理条目，后台消费线程本来就在跑批循环里，生产者写完槽位直接撤退，避免每条日志都陷入内核态发信号。
2. **批处理出锁 + RAII 优雅停机**：消费者一次最多批量搬走 64 个 `LogRecord` 到复用 `batch` 容器后立刻解锁并通知 `notFull_`，真正的格式化与文件写入全部在锁外进行；`~AsyncWorker()` 析构时置 `stopping_ = true`、唤醒两侧等待并 `worker_.join()`，保证队列里残余条目全部刷进磁盘后才关闭文件。

下面这段无锁模拟代码可以在浏览器里直接运行，对比三种溢出策略在 4 槽位队列面对 7 条突发请求时的不同取舍：

```cpp
// 《实战（三）》：有界环形队列的三种溢出背压策略对比（单线程模拟）
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

enum class OverflowPolicy { BlockDropFallback, DropNewest, OverwriteOldest };

class RingQueueSim {
 public:
  RingQueueSim(std::size_t cap, OverflowPolicy p)
      : cap_(cap), policy_(p), ring_(cap) {}

  void push(std::string item) {
    if (count_ < cap_) {
      ring_[tail_] = std::move(item);
      tail_ = (tail_ + 1) % cap_;
      ++count_;
      return;
    }
    if (policy_ == OverflowPolicy::DropNewest) {
      ++dropped_;
    } else if (policy_ == OverflowPolicy::OverwriteOldest) {
      ring_[head_] = std::move(item);
      head_ = (head_ + 1) % cap_;
      tail_ = head_;
      ++overwritten_;
    } else {
      // 模拟 Block：等待消费者先腾出 1 个空位后再放入新元素
      consumed_.push_back(std::move(ring_[head_]));
      head_ = (head_ + 1) % cap_;
      --count_;
      ring_[tail_] = std::move(item);
      tail_ = (tail_ + 1) % cap_;
      ++count_;
    }
  }

  void drainAll() {
    while (count_ > 0) {
      consumed_.push_back(std::move(ring_[head_]));
      head_ = (head_ + 1) % cap_;
      --count_;
    }
  }

  void print(const char* label) const {
    std::printf("%-16s | dropped=%zu overwritten=%zu received=[",
                label, dropped_, overwritten_);
    for (std::size_t i = 0; i < consumed_.size(); ++i) {
      std::printf("%s%s", i ? "," : "", consumed_[i].c_str());
    }
    std::printf("]\n");
  }

 private:
  std::size_t cap_, head_ = 0, tail_ = 0, count_ = 0;
  std::size_t dropped_ = 0, overwritten_ = 0;
  OverflowPolicy policy_;
  std::vector<std::string> ring_, consumed_;
};

int main() {
  for (auto [name, pol] : {
           std::pair{"Block", OverflowPolicy::BlockDropFallback},
           std::pair{"DropNewest", OverflowPolicy::DropNewest},
           std::pair{"OverwriteOldest", OverflowPolicy::OverwriteOldest}}) {
    RingQueueSim q(4, pol);
    for (int i = 1; i <= 7; ++i) q.push("L" + std::to_string(i));
    q.drainAll();
    q.print(name);
  }
}
```

```text
Block            | dropped=0 overwritten=0 received=[L1,L2,L3,L4,L5,L6,L7]
DropNewest       | dropped=3 overwritten=0 received=[L1,L2,L3,L4]
OverwriteOldest  | dropped=0 overwritten=3 received=[L4,L5,L6,L7]
```

## 4. 后台渲染：`PatternFormatter`（预编译 `std::variant`）与 CRTP 的取舍

消息进入后台线程后，还要经历「把 `"[%T] [%L] [%n] %v"` 格式化成字符串再交给 Sink」的热循环。

1. **构造期预编译格式串**：`src/pattern.cpp` 在初始化时就把格式串编译成一段连续的 `std::vector<Token>`（其中 `Token = std::variant<LiteralTok, TimeTok, LevelTok, NameTok, ThreadTok, MsgTok>`），打日志时只需顺序 `std::visit` 并用纯整数算术格式化时间戳直接追加进复用的 `std::string scratch_`，全程零解析、零临时字符串、零 `snprintf`；
2. **为什么外层 Sink 不强行用 CRTP**：在 `bench_async_throughput.cpp` 里我们顺便测了纯内存批量计数循环：CRTP 静态多态跑 100 万条耗时 **0.22 ms**，虚函数 Sink 耗时 **0.45 ms**（相差仅 **0.23 ns/条**）。一旦放到磁盘文件写场景，这点差异立刻被微秒级的缓冲区拷贝和系统调用淹没，而保留运行期 `BaseSink` / `AnySink` 却能换来配置文件动态组装 Sink 的能力——**把 CRTP 留给真正 CPU 密集的像素/DSP 热循环，不拿来强拧日志 Sink**。

下面这段预编译格式串演示可以直接在浏览器里运行：

```cpp
// 《实战（三）》：构造期预编译格式串成 std::variant Token 数组，热路径零解析追加进复用缓冲
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

struct Record {
  uint64_t tsMs;
  const char* level;
  std::string_view name;
  std::string_view msg;
};

class PatternFormatter {
  struct Lit { std::string text; };
  struct Time {};
  struct Level {};
  struct Name {};
  struct Msg {};
  using Tok = std::variant<Lit, Time, Level, Name, Msg>;
  std::vector<Tok> toks_;

 public:
  explicit PatternFormatter(std::string_view pat) {
    std::string lit;
    auto flush = [&]() { if (!lit.empty()) { toks_.emplace_back(Lit{std::move(lit)}); lit.clear(); } };
    for (std::size_t i = 0; i < pat.size(); ++i) {
      if (pat[i] == '%' && i + 1 < pat.size()) {
        flush();
        char c = pat[++i];
        if (c == 'T') toks_.emplace_back(Time{});
        else if (c == 'L') toks_.emplace_back(Level{});
        else if (c == 'n') toks_.emplace_back(Name{});
        else if (c == 'v') toks_.emplace_back(Msg{});
      } else {
        lit.push_back(pat[i]);
      }
    }
    flush();
  }

  void format(const Record& r, std::string& out) const {
    for (const auto& t : toks_) {
      std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, Lit>) out.append(node.text);
        else if constexpr (std::is_same_v<T, Time>) {
          uint32_t s = static_cast<uint32_t>((r.tsMs / 1000) % 86400);
          char b[13];
          std::snprintf(b, sizeof(b), "%02u:%02u:%02u.%03u",
                        s / 3600, (s % 3600) / 60, s % 60, static_cast<unsigned>(r.tsMs % 1000));
          out.append(b, 12);
        } else if constexpr (std::is_same_v<T, Level>) out.append(r.level);
        else if constexpr (std::is_same_v<T, Name>) out.append(r.name);
        else if constexpr (std::is_same_v<T, Msg>) out.append(r.msg);
      }, t);
    }
  }
};

int main() {
  PatternFormatter fmt("[%T] [%L] [%n] %v");
  std::string scratch;
  scratch.reserve(128);
  for (uint64_t ts : {3661042ULL, 3662125ULL}) {
    scratch.clear();
    fmt.format({ts, "INFO", "auth", "token verified"}, scratch);
    std::printf("%s\n", scratch.c_str());
  }
}
```

```text
[01:01:01.042] [INFO] [auth] token verified
[01:01:02.125] [INFO] [auth] token verified
```

## 5. 实测：4 条路径吞吐、P50/P99 延迟、堆分配与 Sanitizer

用 `projects/minilog/bench/bench_async_throughput.cpp` 实测一条 50 B 消息（恰好超过 `libc++` 22 B `std::string` SSO、但完全落入 `LogRecord` 128 B SBO）在**单线程 10 万条**与**4 线程并发 40 万条**下的真实表现（Apple clang 21 `-O2`，5 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="4 线程并发写 40 万条 50B 日志吞吐量对比：同步无缓冲 38.5 万 ops/s，同步 64KB 缓冲 448.3 万 ops/s，异步堆 string 队列 885.1 万 ops/s，异步 minilog SBO 队列 960.0 万 ops/s"><g style="font-family:var(--mono)"><text x="175" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">4 线程写 40 万条 50B 日志吞吐量（Clang -O2）</text><text x="167" y="49" text-anchor="end" style="fill:var(--fg);font-size:11px">1. Sync unbuffered</text><rect x="175" y="34" width="14" height="20" rx="2" style="fill:var(--danger)"/><text x="196" y="48" text-anchor="start" style="fill:var(--danger);font-size:11px;font-weight:700">0.38M ops/s（1039.52 ms，每次写盘刷新）</text><text x="167" y="83" text-anchor="end" style="fill:var(--fg);font-size:11px">2. Sync buffered (64KB)</text><rect x="175" y="68" width="154" height="20" rx="2" style="fill:var(--warn)"/><text x="336" y="82" text-anchor="start" style="fill:var(--warn);font-size:11px;font-weight:700">4.48M ops/s（89.22 ms，四线程争同一把锁）</text><text x="167" y="117" text-anchor="end" style="fill:var(--fg);font-size:11px">3. Async std::string</text><rect x="175" y="102" width="304" height="20" rx="2" style="fill:var(--link)"/><text x="486" y="116" text-anchor="start" style="fill:var(--link);font-size:11px;font-weight:700">8.85M ops/s（45.19 ms，10k 条触发 10001 次堆分配）</text><text x="167" y="151" text-anchor="end" style="fill:var(--fg);font-size:11px">4. Async minilog SBO</text><rect x="175" y="136" width="330" height="20" rx="2" style="fill:var(--accent)"/><text x="512" y="150" text-anchor="start" style="fill:var(--accent);font-size:11px;font-weight:700">9.60M ops/s（41.67 ms，快同步缓冲 2.1×，生产侧 0 堆分配）</text><text x="10" y="186" text-anchor="start" style="fill:var(--muted);font-size:10.5px">单线程 10 万条对比：Sync buffered 7.23 ms (13.83M ops/s) 略快于 Async SBO 8.17 ms (12.23M ops/s)，因单线程无锁争用</text><text x="10" y="204" text-anchor="start" style="fill:var(--muted);font-size:10.5px">完整压测与 10k 堆分配统计见 projects/minilog/bench/bench_async_throughput.cpp</text></g></svg>
</figure>

| 写入路径 | 单线程 10 万条 | 4 线程 40 万条 | 4 线程吞吐 | 前端 P50 / P99（单线程采样） | 1 万条热路径堆分配 |
| --- | :---: | :---: | :---: | :---: | :---: |
| **1. 同步无缓冲直写** | 121.93 ms | 1039.52 ms | 384,793 ops/s | 1125 / 2375 ns | 0 |
| **2. 同步 64 KB 缓冲** | **7.23 ms** | 89.22 ms | 4,483,342 ops/s | 42 / 84 ns | 0 |
| **3. 异步 `std::string` 队列** | 8.77 ms | 45.19 ms | 8,851,393 ops/s | 42 / 2208 ns | **10,001** |
| **4. 异步 `minilog` SBO 队列** | 8.17 ms | **41.67 ms** | **9,599,568 ops/s** | 41 / 1916 ns | **1**（仅后台 `batch.reserve` 1 次） |

实测读出三个非常接地气的工程结论：
1. **单线程别迷信异步**：没有线程争锁、用户态又开了 64 KB 缓冲时，同步写只要 `7.23 ms`、P99 仅 `84 ns`；异步队列反倒多了跨线程搬运与条件变量唤醒，P99 在 `1916 ns`。**异步的真正舞台在「多线程并发 + 隔离刷盘毛刺」**。
2. **4 线程下异步立刻翻盘**：4 线程全部同步格式化抢同一把写锁时耗时暴涨到 `89.22 ms`，而异步 `minilog` 跑出 **960 万 ops/s（`41.67 ms`，比同步缓冲快 2.1×、比同步直写快 25×）**。
3. **SBO 消除了队列里的碎片堆分配**：50 B 消息让 `std::string` 队列在 1 万条里触发了 `10,001` 次堆分配；`minilog` 整个热路径仅后台线程建局部 `batch` 时分配过 `1` 次，生产者侧为 **0 次**。

同时在 `projects/minilog/measure.sh` 中对 `test_async`（4 线程 × 1 万条离开作用域不手动 `flush` 直接 RAII 析构，以及 `DropNewest`/`OverwriteOldest` 溢出测试）全量跑通了 **ASan + UBSan** 与 **ThreadSanitizer（TSan）**，40,000 条消息 100% 完整落盘，0 数据竞争、0 内存泄漏。

## 6. 小结

- **`LogRecord` 128 B SBO**：把常见短日志留在值对象内部并手写局部移动，消除生产侧每条日志一次 `new/delete`；
- **命令队列 + `OverflowPolicy`**：把「何时丢、何时等、何时覆盖」策略化，配合按需唤醒与锁外批处理拉高并发吞吐；
- **`PatternFormatter`**：用构造期预编译 `std::variant` Token 替代每次日志重新解析格式串；
- **ASan / TSan 兜底**：并发组件不能只靠单测「偶尔能过」，必须上 TSan 与 RAII 析构排空断言验证边界。

到这里，`minilog` 已经能打能扛了。收官篇我们反过来算一笔账：**哪些 GoF 模式在 `minilog` 里故意没用、如果硬套会多出多少烂代码，以及整套专栏 23 个模式 + 4 个惯用法的终极选型表**。

## 7. 练习

1. **为什么异步压测里 `10k allocs = 1` 而不是 `0`？** 请追踪 `projects/minilog/src/async_worker.cpp` 与 `bench_async_throughput.cpp`，指出这唯一的 1 次堆分配发生在哪一行、能不能也消掉？
2. **背压策略选型**：一个电商支付服务要把关键对账事件写进异步日志，另一边要把接口耗时监控打进异步日志。两条路径分别该配哪种 `OverflowPolicy`？为什么？
3. **无锁队列还是互斥锁 + 批处理？** 为什么 `minilog` 用「单互斥锁 + 64 条批量搬运 + 128 B SBO」就能跑出近千万 ops/s，而不是一上来就手写多生产者无锁环形队列？

<details>
<summary>点击查看答案</summary>

1. 这唯一的 1 次分配发生在后台线程刚进入 `AsyncWorker::workerLoop()` 时对局部变量执行 `batch.reserve(64)`；生产者线程在 1 万次 `logAt()` 调用里完成了 **0 次**堆分配。如果把 `batch` 改成栈上固定容量 `std::array<LogRecord, 64>`（或预初始化的成员缓冲），连这 1 次分配也能彻底归零。
2. 对账关键日志必须选 **`OverflowPolicy::Block`**（绝不丢账，哪怕触发短暂阻塞也要落盘，同时配合合理的队列容量如 `16384` 和监控告警）；耗时监控日志应选 **`OverflowPolicy::DropNewest`**（核心交易响应延迟优先级高于监控完整性，队列打满时只丢采样并上报 `droppedCount()` 告警）。
3. 多生产者无锁队列（MPMC）不仅要处理 ABA、伪共享（False Sharing）、多核缓存行乒乓与 `memory_order` 精细同步，而且一旦涉及 `Block` 阻塞等待时最终还是得落回信号量/条件变量。而带批处理的互斥锁每次加锁直接搬走最多 64 条，锁持有时间极短、临界区外完全并行，代码不到 120 行且 TSan 极易验证，在绝大多数工程里性价比最高。

</details>
