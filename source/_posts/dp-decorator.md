---
title: 装饰器模式
date: 2026-11-11 10:00:00
updated: 2026-11-11 10:00:00
categories:
  - 设计模式
  - 结构型模式
tags:
  - 设计模式
  - 装饰器
  - mixin
  - std::function
  - FFmpeg
order: 4
description: 给数据源加日志、统计、校验和，用继承就得给每种功能组合写一个子类：3 种功能 7 个子类，再加一种「丢掉坏包」变成 15 个，新增 88 行。装饰器模式让每种功能成为一层包装，和数据源实现同一个接口，想要哪几样就套哪几层：同样加一种功能只新增 13 行。实测包装顺序会改变结果——Stats 放在 Validate 里外，数出 240 和 236 个包；先放大再限幅没有爆音，先限幅再放大有 260 个采样越界；给 FFmpeg 的 AVIOContext 套两层计数装饰器，缓冲区大的放外层读 76 次，放内层读 312 次。每多套一层虚函数装饰器约多 0.5 ns，模板 mixin 套 16 层也几乎不花时间。
top_group_list: true
disableNunjucks: true
viz: [dp-decorator]
---

> **本文目标**：理解装饰器模式解决的问题（功能组合导致子类爆炸）；会写「同一个接口 + 持有被装饰对象 + 转发」的装饰器，并在运行时一层层组合；知道包装顺序会影响结果；会用模板 mixin、`std::function` 和 lambda 写装饰器，知道它们各自的代价；分清装饰器和适配器、代理、组合、责任链。
>
> **前置知识**：上一篇《组合模式》（同样是「持有一个同类型的对象」）；第 1 章的《组合优于继承与 C++ 的四种多态》《SOLID 原则（上）：单一职责与开闭》；音视频专栏里的解封装（`av_read_frame` 读出一个个包）。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/14-decorator/` 目录。

## 1. 问题：每种功能组合都要一个子类

播放器从解封装器里一个一个地读出包（`Packet`）。读包的代码很简单，麻烦的是各种「顺便要做的事」：

- 调试时想打日志，看看关键帧在哪里；
- 想统计一共读了多少个包、多少字节，算码率；
- 想对所有数据算一个校验和，和别的工具的结果对一对，确认没有读错；
- 网络流里偶尔有坏包（空包），想在送进解码器之前丢掉。

这些功能互相独立，每种场合要的组合也不一样：线上只要统计，调试时全都要，单元测试里一样都不要。

最直接的想法是继承：写一个 `MemorySource`（这里用内存里生成的 240 个包模拟测试视频 `bbb-10s.mp4` 的视频流，第 59、119、179、239 个是空包），要日志就派生一个 `LoggedSource`，要统计就派生一个 `StatsSource`……可是「既要日志又要统计」怎么办？C++ 不能让一个对象同时是 `LoggedSource` 和 `StatsSource`，只能再写一个 `StatsLoggedSource`。3 种功能，每种组合一个子类，就是 2³ − 1 = 7 个：

```cpp
// 《装饰器模式》：用继承给数据源加功能，3 种功能的每种组合都要一个子类（7 个）
#include <cstdio>
#include <memory>
#include <vector>

struct Packet {
    int index = 0;
    long long pts = 0;                 // 时间戳，单位 1/12288 秒（和 bbb-10s.mp4 的视频流一样，每帧 512）
    bool key = false;                  // 关键帧
    std::vector<unsigned char> data;
};

// ---------- 基类：模拟测试视频的 240 个视频包 ----------
class MemorySource {
public:
    explicit MemorySource(int n = 240) : n_(n) {}
    virtual ~MemorySource() = default;
    virtual bool read(Packet& p) {
        if (i_ >= n_) return false;
        p.index = i_;
        p.pts = i_ * 512LL;
        p.key = i_ % 48 == 0;                    // 每 48 帧一个关键帧
        int size = p.key ? 9000 : 1200 + i_ * 37 % 800;
        if (i_ % 60 == 59) size = 0;             // 第 59、119、179、239 个包是坏的（空包）
        p.data.assign(size, static_cast<unsigned char>(i_));
        ++i_;
        return true;
    }
private:
    int n_, i_ = 0;
};

// ---------- 每种功能组合一个子类（类名从外往里读） ----------
class LoggedSource : public MemorySource {
public:
    bool read(Packet& p) override {
        if (!MemorySource::read(p)) return false;
        if (p.key) std::printf("  [log] #%d 关键帧 pts=%.2fs %zu 字节\n", p.index, p.pts / 12288.0, p.data.size());
        return true;
    }
};

class ChecksumSource : public MemorySource {
public:
    bool read(Packet& p) override {
        if (!MemorySource::read(p)) { std::printf("  [adler32] %08x\n", (b_ << 16) | a_); return false; }
        for (unsigned char c : p.data) { a_ = (a_ + c) % 65521; b_ = (b_ + a_) % 65521; }
        return true;
    }
private:
    unsigned a_ = 1, b_ = 0;
};

class StatsSource : public MemorySource {
public:
    bool read(Packet& p) override {
        if (!MemorySource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class ChecksumLoggedSource : public LoggedSource {
public:
    bool read(Packet& p) override {
        if (!LoggedSource::read(p)) { std::printf("  [adler32] %08x\n", (b_ << 16) | a_); return false; }
        for (unsigned char c : p.data) { a_ = (a_ + c) % 65521; b_ = (b_ + a_) % 65521; }
        return true;
    }
private:
    unsigned a_ = 1, b_ = 0;
};

class StatsLoggedSource : public LoggedSource {
public:
    bool read(Packet& p) override {
        if (!LoggedSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class StatsChecksumSource : public ChecksumSource {
public:
    bool read(Packet& p) override {
        if (!ChecksumSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class StatsChecksumLoggedSource : public ChecksumLoggedSource {
public:
    bool read(Packet& p) override {
        if (!ChecksumLoggedSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

int main() {
    // 想要「统计 + 校验 + 日志」：只能用正好有这几样功能的那个子类
    StatsChecksumLoggedSource src;
    Packet p;
    while (src.read(p)) {}
}
```

输出：

```
  [log] #0 关键帧 pts=0.00s 9000 字节
  [log] #48 关键帧 pts=2.00s 9000 字节
  [log] #96 关键帧 pts=4.00s 9000 字节
  [log] #144 关键帧 pts=6.00s 9000 字节
  [log] #192 关键帧 pts=8.00s 9000 字节
  [adler32] d4b57bdf
  [stats] 240 个包，413948 字节
```

能用，但问题很明显：

1. **代码重复**。每个组合类只能沿一条父类链复用：`StatsChecksumLoggedSource` 继承 `ChecksumLoggedSource`，日志和校验和继承来了，统计的代码只能再写一遍。结果统计的代码（连同两个计数成员）在 7 个类里出现了 4 次，校验和出现了 2 次。要改统计的方式，得改 4 个地方，漏改一处，不同组合的统计结果就对不上。
2. **加一种功能，类的个数翻倍**。现在加上「丢掉坏包」（`inherit_v2.cpp`），除了 `ValidatedSource` 本身，它和原来 7 个类的每一种组合都要一个新类，一共新增 8 个：

   ```
   ValidatedSource  LoggedValidatedSource  ChecksumValidatedSource  StatsValidatedSource
   ChecksumLoggedValidatedSource  StatsLoggedValidatedSource  StatsChecksumValidatedSource
   StatsChecksumLoggedValidatedSource
   ```

   统计的代码从 4 份变成 8 份，校验和从 2 份变成 4 份。n 种功能要 2ⁿ − 1 个子类。数据源如果不止一种（文件、网络、内存），还要再乘上数据源的种数。
3. **组合在编译时就定死了**。想根据命令行参数（`--log --stats`）决定加哪些功能，就得写一个分支选出 8 个类中的一个；用户换个组合，就是换一个类。
4. **顺序也定死了**。`StatsChecksumLoggedValidatedSource` 里，丢坏包一定在最里面。想把统计放在丢坏包之前（统计原始的包数），又是另一个类。

用 `measure.sh` 统计（不算第一行注释、`main` 和空行），继承的写法从 3 种功能的 8 个类、105 行，变成 4 种功能的 16 个类、193 行，**加一种功能新增 88 行**。

## 2. 装饰器模式

### 2.1 意图

GoF 的定义：

> 动态地给一个对象添加一些额外的职责。就增加功能来说，装饰器比生成子类更为灵活。

大白话：**给对象套壳**。每个壳只做一件事，壳和里面的对象长得一模一样（实现同一个接口），所以壳外面还能再套壳，使用者分不出手里拿的是原来的对象还是套了几层壳的对象。

做法有三个要点：

1. 装饰器和被装饰的对象**实现同一个接口**（这里是 `PacketSource`）；
2. 装饰器**持有**一个被装饰的对象（`std::unique_ptr<PacketSource> inner_`），注意它的类型也是接口，所以里面可以是原始的数据源，也可以是另一个装饰器；
3. 装饰器的 `read()` **先转发给里面的对象，再加上自己的功能**（也可以先做自己的事再转发，或者前后都做）。

### 2.2 重构

把 3 种功能各写成一个装饰器：

```cpp
// 《装饰器模式》：一个数据源 + 三个装饰器（日志、统计、校验和），想要哪几样功能就套哪几层
#include <cstdio>
#include <memory>
#include <vector>

struct Packet {
    int index = 0;
    long long pts = 0;                 // 时间戳，单位 1/12288 秒（和 bbb-10s.mp4 的视频流一样，每帧 512）
    bool key = false;                  // 关键帧
    std::vector<unsigned char> data;
};

// ---------- 抽象接口：能一个一个地读出包 ----------
class PacketSource {
public:
    virtual ~PacketSource() = default;
    virtual bool read(Packet& out) = 0;          // 读到返回 true，读完返回 false
};

// ---------- 被装饰的对象：模拟测试视频的 240 个视频包 ----------
class MemorySource : public PacketSource {
public:
    explicit MemorySource(int n = 240) : n_(n) {}
    bool read(Packet& p) override {
        if (i_ >= n_) return false;
        p.index = i_;
        p.pts = i_ * 512LL;
        p.key = i_ % 48 == 0;                    // 每 48 帧一个关键帧
        int size = p.key ? 9000 : 1200 + i_ * 37 % 800;
        if (i_ % 60 == 59) size = 0;             // 第 59、119、179、239 个包是坏的（空包）
        p.data.assign(size, static_cast<unsigned char>(i_));
        ++i_;
        return true;
    }
private:
    int n_, i_ = 0;
};

// ---------- 装饰器的基类：持有一个 PacketSource，默认原样转发 ----------
class SourceDecorator : public PacketSource {
public:
    explicit SourceDecorator(std::unique_ptr<PacketSource> inner) : inner_(std::move(inner)) {}
    bool read(Packet& p) override { return inner_->read(p); }
protected:
    std::unique_ptr<PacketSource> inner_;
};

class Logging : public SourceDecorator {         // 读到关键帧时打一行日志
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        if (!inner_->read(p)) return false;
        if (p.key) std::printf("  [log] #%d 关键帧 pts=%.2fs %zu 字节\n", p.index, p.pts / 12288.0, p.data.size());
        return true;
    }
};

class Stats : public SourceDecorator {           // 统计包数和字节数
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        if (!inner_->read(p)) { report(); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    void report() const { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); }
    int packets_ = 0;
    size_t bytes_ = 0;
};

class Checksum : public SourceDecorator {        // 对所有字节算 Adler-32
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        if (!inner_->read(p)) { std::printf("  [adler32] %08x\n", (b_ << 16) | a_); return false; }
        for (unsigned char c : p.data) { a_ = (a_ + c) % 65521; b_ = (b_ + a_) % 65521; }
        return true;
    }
private:
    unsigned a_ = 1, b_ = 0;
};

int main() {
    // 想要「统计 + 校验 + 日志」：从里往外一层层套上去
    std::unique_ptr<PacketSource> src = std::make_unique<MemorySource>();
    src = std::make_unique<Logging>(std::move(src));
    src = std::make_unique<Checksum>(std::move(src));
    src = std::make_unique<Stats>(std::move(src));

    Packet p;
    while (src->read(p)) {}                      // 使用者只认识 PacketSource
}
```

输出和继承版完全一样。`SourceDecorator` 是所有装饰器的基类，它默认把 `read()` 原样转发给 `inner_`。这个例子里接口只有一个函数，看不出它的用处；接口有很多函数时，每个装饰器只重写自己关心的那几个，其余的由基类统一转发（第 3.4 节会看到漏转发的后果）。

`main` 里从里往外一层层套上去：先是 `MemorySource`，套上 `Logging`，再套 `Checksum`，最后是 `Stats`。最后得到的 `src` 是一个 `PacketSource` 指针，使用者照常调用 `read()`，不知道也不关心里面有几层。

加上「丢掉坏包」时，只要新写一个装饰器（`decorator_v2.cpp`）：

<!-- run: off -->
```cpp
class Validate : public SourceDecorator {        // 丢掉空包（坏包）
public:
    using SourceDecorator::SourceDecorator;
    bool read(Packet& p) override {
        while (inner_->read(p)) {
            if (!p.data.empty()) return true;
            ++dropped_;
        }
        return false;
    }
private:
    int dropped_ = 0;
};
```

再在 `main` 里多套一层：

<!-- run: off -->
```cpp
    std::unique_ptr<PacketSource> src = std::make_unique<MemorySource>();
    src = std::make_unique<Validate>(std::move(src));      // 新增这一行
    src = std::make_unique<Logging>(std::move(src));
```

原来的类一行都没改，这正是开闭原则说的「对扩展开放，对修改关闭」。两种写法的实测对比：

<figure class="diagram">
<svg viewBox="0 0 720 186" role="img" aria-label="继承和装饰器两种写法的实测：3 种功能时继承 8 个类、105 行，装饰器 6 个类、75 行；再加一种「丢掉坏包」，继承变成 16 个类、新增 88 行，装饰器 7 个类、新增 13 行"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">类的个数</text><rect x="198.0" y="30.0" width="85.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="289.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8 个</text><rect x="198.0" y="60.0" width="170.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="374.0" y="75.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">16 个</text><rect x="198.0" y="90.0" width="63.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="267.8" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6 个</text><rect x="198.0" y="120.0" width="74.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="278.4" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">7 个</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">代码行数（不含 main）</text><rect x="458.0" y="30.0" width="92.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="556.5" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">105 行</text><rect x="458.0" y="60.0" width="170.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="634.0" y="75.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">193 行</text><rect x="458.0" y="90.0" width="66.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="530.1" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">75 行</text><rect x="458.0" y="120.0" width="77.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="541.5" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">88 行</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">继承 v1（3 种功能）</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">继承 v2（4 种功能）</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">装饰器 v1（3 种功能）</text><text x="190.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">装饰器 v2（4 种功能）</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">sh measure.sh：不算第一行注释、main 和空行；v1 → v2 新增：继承 +88 行，装饰器 +13 行</text></g></svg>
</figure>

| | 继承 | 装饰器 |
| --- | --- | --- |
| 3 种功能 | 8 个类，105 行 | 6 个类，75 行 |
| 4 种功能 | 16 个类，193 行 | 7 个类，88 行 |
| 加一种功能 | 新增 8 个类、88 行 | 新增 1 个类、13 行 |
| n 种功能需要的类 | 2ⁿ − 1 个组合子类 | n 个装饰器 |
| 运行时选择功能 | 要在 2ⁿ − 1 个类里挑一个 | 每种功能一个 `if` |

最后一行值得展开说。功能由命令行参数决定时，装饰器的写法是这样的：

<!-- run: off -->
```cpp
std::unique_ptr<PacketSource> open(const Options& opt) {
    std::unique_ptr<PacketSource> src = std::make_unique<MemorySource>();
    if (opt.dropBad)  src = std::make_unique<Validate>(std::move(src));
    if (opt.log)      src = std::make_unique<Logging>(std::move(src));
    if (opt.checksum) src = std::make_unique<Checksum>(std::move(src));
    if (opt.stats)    src = std::make_unique<Stats>(std::move(src));
    return src;
}
```

每种功能一行，互不相干。继承的写法要把 4 个布尔值的 16 种情况一一对应到 16 个类上。

### 2.3 类图

```uml
label: 装饰器模式：MemorySource 和 SourceDecorator 都实现 PacketSource；SourceDecorator 持有一个 PacketSource（可以是数据源，也可以是另一个装饰器）；Stats、Logging、Checksum、Validate 继承 SourceDecorator
classes:
  - { id: C, name: 客户代码, x: 20, y: 30, w: 100 }
  - { id: P, name: PacketSource, stereo: interface, x: 250, y: 16, w: 220, ops: ['*+ read(p : Packet&) : bool'] }
  - { id: M, name: MemorySource, x: 20, y: 170, w: 180, attrs: ['- i_ : int'], ops: ['+ read(p)  // 生成包'] }
  - { id: D, name: SourceDecorator, stereo: abstract, x: 330, y: 170, w: 290, attrs: ['# inner_ : unique_ptr<PacketSource>'], ops: ['+ read(p)  // 转发给 inner_'] }
  - { id: S, name: Stats, x: 200, y: 330, w: 110, ops: ['+ read(p)'] }
  - { id: L, name: Logging, x: 330, y: 330, w: 110, ops: ['+ read(p)'] }
  - { id: K, name: Checksum, x: 460, y: 330, w: 110, ops: ['+ read(p)'] }
  - { id: V, name: Validate, x: 590, y: 330, w: 110, ops: ['+ read(p)'] }
rels:
  - { from: C, to: P, type: depend }
  - { from: M, to: P, type: realize, mid: 130, tdx: -60 }
  - { from: D, to: P, type: realize, mid: 130, fdx: -60, tdx: 40 }
  - { from: D, to: P, type: compose, tm: '1', points: [[620, 200], [680, 200], [680, 48], [470, 48]] }
  - { from: S, to: D, type: inherit, mid: 295 }
  - { from: L, to: D, type: inherit, mid: 295 }
  - { from: K, to: D, type: inherit, mid: 295 }
  - { from: V, to: D, type: inherit, mid: 295 }
```

关键是右边那条带菱形的线：`SourceDecorator` **既是**一个 `PacketSource`（空心三角），**又有**一个 `PacketSource`（菱形）。「是」让它能被当成数据源使用、能被别的装饰器包住；「有」让它能包住别的数据源。上一篇的组合模式也是这个结构，区别是 `Folder` 有很多个子节点，装饰器只有一个（第 5 节）。

### 2.4 一次调用怎样穿过每一层

`decorator_v2.cpp` 去掉 `Checksum` 后，套好的对象像一颗洋葱：

<figure class="diagram">
<svg viewBox="0 0 800 264" role="img" aria-label="装饰器像洋葱：Stats 包着 Logging，Logging 包着 Validate，Validate 包着 MemorySource；调用从外往里一层层转发，包从里往外，每层加工一次"><g style="font-family:var(--mono)"><rect x="20" y="20" width="520" height="210" rx="6" style="fill:var(--bg);stroke:var(--accent);stroke-width:1.5"/><text x="30.0" y="38.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">Stats</text><text x="530.0" y="38.0" text-anchor="end" style="fill:var(--muted);font-size:10.5px">数包数和字节数</text><rect x="46" y="50" width="468" height="160" rx="6" style="fill:var(--bg);stroke:var(--link);stroke-width:1.5"/><text x="56.0" y="68.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">Logging</text><text x="504.0" y="68.0" text-anchor="end" style="fill:var(--muted);font-size:10.5px">关键帧打日志</text><rect x="72" y="80" width="416" height="110" rx="6" style="fill:var(--bg);stroke:var(--warn);stroke-width:1.5"/><text x="82.0" y="98.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">Validate</text><text x="478.0" y="98.0" text-anchor="end" style="fill:var(--muted);font-size:10.5px">丢掉空包</text><rect x="98" y="110" width="364" height="60" rx="6" style="fill:var(--code-bg);stroke:var(--fg);stroke-width:1.5"/><text x="280.0" y="138.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">MemorySource</text><text x="280.0" y="155.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">真正生成包</text><text x="560.0" y="40.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px;font-weight:700">使用者</text><text x="560.0" y="58.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">src-&gt;read(p)</text><line x1="600" y1="72" x2="600" y2="190" style="stroke:var(--link);stroke-width:1.5"/><path d="M600,190 L597.3,183.6 L602.7,183.6 Z" style="fill:var(--link)"/><text x="610.0" y="120.0" text-anchor="start" style="fill:var(--link);font-size:10.5px">调用从外往里</text><text x="610.0" y="136.0" text-anchor="start" style="fill:var(--link);font-size:10.5px">一层层转发</text><line x1="690" y1="190" x2="690" y2="72" style="stroke:var(--accent);stroke-width:1.5"/><path d="M690,72 L692.7,78.4 L687.3,78.4 Z" style="fill:var(--accent)"/><text x="700.0" y="120.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">包从里往外</text><text x="700.0" y="136.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">每层加工一次</text><text x="20.0" y="252.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">每一层的类型都是 PacketSource：使用者手里只有最外面那层的指针，看不出里面套了几层</text></g></svg>
</figure>

使用者调用最外层的 `read()`，调用一层层往里转发，直到 `MemorySource` 真正生成一个包；包再一层层往外返回，每层做一次自己的事：

```uml
label: 时序图：使用者调用 Stats::read，Stats 转发给 Logging，Logging 转发给 Validate，Validate 转发给 MemorySource；MemorySource 返回第 48 个包，Validate 检查不是空包，Logging 打日志，Stats 计数，最后返回给使用者
participants:
  - { id: c, name: 使用者, w: 80 }
  - { id: s, name: Stats, w: 90 }
  - { id: l, name: Logging, w: 90 }
  - { id: v, name: Validate, w: 90 }
  - { id: m, name: MemorySource, w: 120 }
messages:
  - { from: c, to: s, text: 'read(p)' }
  - { from: s, to: l, text: 'read(p)' }
  - { from: l, to: v, text: 'read(p)' }
  - { from: v, to: m, text: 'read(p)' }
  - { from: m, to: v, text: 'true（#48）', type: reply }
  - { from: v, to: v, text: '不是空包' }
  - { from: v, to: l, text: 'true', type: reply }
  - { from: l, to: l, text: '关键帧：打日志' }
  - { from: l, to: s, text: 'true', type: reply }
  - { from: s, to: s, text: '++packets_' }
  - { from: s, to: c, text: 'true', type: reply }
```

下面的演示一步步走一遍。点「遇到坏包」看 `Validate` 怎样在中间把空包吞掉、再向里要一个包；点「换个顺序」看把 `Validate` 换到最外面以后，`Stats` 的计数为什么变了：

<div class="dsviz" data-viz="dp-decorator"></div>

## 3. 包装的顺序会改变结果

装饰器可以随意组合，但组合的**顺序**不是无关紧要的。同样几层，套的顺序不同，结果可能完全不同。

### 3.1 先统计还是先丢坏包

`decorator_v2.cpp` 里 `Validate` 在最里面，`Stats` 只看得到好包，数出 236 个。把 `main` 里的顺序换一下，让 `Validate` 套在 `Stats` 外面，`Stats` 就会把 4 个坏包也数进去，数出 240 个（演示的「换个顺序」）。

两种结果都「对」，只是回答的问题不同：236 是送进解码器的包数，240 是文件里原有的包数。要两个数都要，就套两个 `Stats`，一个在 `Validate` 里面，一个在外面（练习 3）。

日志和统计的顺序就无所谓：它们都不改包的内容，也不丢包。**改数据、丢数据、提前结束**的装饰器（`Validate`、限幅、只读前 n 个）放在哪里，才需要仔细想。

### 3.2 先放大还是先限幅

音频滤镜是装饰器最常见的用武之地：每个滤镜从里面一层读采样，处理后交给外面一层。下面用浮点采样（正常范围 −1～1）做两个滤镜：`Gain` 把音量乘以 4，`Clip` 把超出 ±1 的部分削掉：

```cpp
// 《装饰器模式》：包装的顺序会改变结果——先放大再限幅，和先限幅再放大
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

class AudioSource {                              // 一个一个地读出采样（浮点，正常范围是 -1～1）
public:
    virtual ~AudioSource() = default;
    virtual bool read(float& s) = 0;
    virtual std::string name() const = 0;
};

class Sine : public AudioSource {                // 1 kHz 正弦波，48 kHz 采样，10 毫秒，幅度 0.4
public:
    bool read(float& s) override {
        if (i_ == 480) return false;
        s = 0.4f * static_cast<float>(std::sin(2 * 3.141592653589793 * 1000 * i_++ / 48000));
        return true;
    }
    std::string name() const override { return "Sine"; }
private:
    int i_ = 0;
};

class Filter : public AudioSource {              // 装饰器基类：持有里面那一层
public:
    explicit Filter(std::unique_ptr<AudioSource> in) : in_(std::move(in)) {}
protected:
    std::unique_ptr<AudioSource> in_;
};

class Gain : public Filter {                     // 音量 × k
public:
    Gain(std::unique_ptr<AudioSource> in, float k) : Filter(std::move(in)), k_(k) {}
    bool read(float& s) override {
        if (!in_->read(s)) return false;
        s *= k_;
        return true;
    }
    std::string name() const override { return "Gain(" + in_->name() + ")"; }
private:
    float k_;
};

class Clip : public Filter {                     // 限幅：超出 ±1 的部分削掉
public:
    using Filter::Filter;
    bool read(float& s) override {
        if (!in_->read(s)) return false;
        s = std::clamp(s, -1.0f, 1.0f);
        return true;
    }
    std::string name() const override { return "Clip(" + in_->name() + ")"; }
};

void report(AudioSource& src) {
    float s, peak = 0;
    int n = 0, over = 0;
    while (src.read(s)) {
        peak = std::max(peak, std::fabs(s));
        if (std::fabs(s) > 1.0f) ++over;         // 超出范围：转成 16 位整数时会溢出，听起来是爆音
        ++n;
    }
    std::printf("%-16s %d 个采样，峰值 %.3f，超出范围 %3d 个\n", src.name().c_str(), n, peak, over);
}

int main() {
    report(*std::make_unique<Sine>());
    report(*std::make_unique<Clip>(std::make_unique<Gain>(std::make_unique<Sine>(), 4.0f)));
    report(*std::make_unique<Gain>(std::make_unique<Clip>(std::make_unique<Sine>()), 4.0f));
}
```

输出（Clang 和 GCC 相同）：

```
Sine             480 个采样，峰值 0.400，超出范围   0 个
Clip(Gain(Sine)) 480 个采样，峰值 1.000，超出范围   0 个
Gain(Clip(Sine)) 480 个采样，峰值 1.600，超出范围 260 个
```

`Clip(Gain(Sine))` 先放大再限幅：放大到 1.6 的部分被削到 1.0，波形顶部被削平（有失真，但不会越界）。`Gain(Clip(Sine))` 先限幅再放大：限幅时原始信号只有 0.4，什么都没削，放大后 480 个采样里有 260 个超出范围。这样的数据转成 16 位整数输出时会溢出回绕，听起来就是「啪啪」的爆音。

用 FFmpeg 命令行时也一样：`-af "volume=4,alimiter"` 和 `-af "alimiter,volume=4"` 是两种不同的结果。滤镜链里的每个滤镜都是一层装饰，书写顺序就是包装顺序。

### 3.3 真实的例子：给 FFmpeg 的 AVIOContext 套装饰器

FFmpeg 的解封装器不直接读文件，而是通过 `AVIOContext` 读字节。`avio_alloc_context` 可以用我们自己的 `read` / `seek` 回调创建一个 `AVIOContext`，而回调里又可以去读另一个 `AVIOContext`——这就是装饰器：外面的 `AVIOContext` 和里面的接口一样，读的时候转发给里面，顺便做点别的事。

下面套两层「计数」装饰器，数一数解封装器读了多少次、多少字节、跳转了几次：

<!-- run: off -->
```cpp
// 《装饰器模式》：给 FFmpeg 的 AVIOContext 套上「计数」装饰器——解封装器照常读文件，我们在中间数它读了几次、读了多少字节、跳了几次
extern "C" {
#include <libavformat/avformat.h>
}
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

// 一个计数装饰器：自己也是一个 AVIOContext，读写和跳转都转发给里面那个 AVIOContext
struct Counter {
    const char* name;
    int bufSize;
    AVIOContext* inner;
    long reads = 0, bytes = 0, seeks = 0;

    static int read(void* opaque, uint8_t* buf, int size) {
        auto* c = static_cast<Counter*>(opaque);
        int n = avio_read(c->inner, buf, size);
        if (n <= 0) return n == 0 ? AVERROR_EOF : n;
        ++c->reads;
        c->bytes += n;
        return n;
    }
    static int64_t seek(void* opaque, int64_t offset, int whence) {
        auto* c = static_cast<Counter*>(opaque);
        if (whence & AVSEEK_SIZE) return avio_size(c->inner);       // 问文件大小：直接转发，不算跳转
        ++c->seeks;
        return avio_seek(c->inner, offset, whence & ~AVSEEK_FORCE);
    }
};

AVIOContext* wrap(Counter& c) {                  // 返回包装好的 AVIOContext，它有自己的缓冲区
    auto* buf = static_cast<unsigned char*>(av_malloc(c.bufSize));
    return avio_alloc_context(buf, c.bufSize, 0, &c, &Counter::read, nullptr, &Counter::seek);
}

void freeWrapped(AVIOContext* pb) {
    av_freep(&pb->buffer);
    avio_context_free(&pb);
}

// 用法：./avio_count 文件 [外层缓冲字节数] [内层缓冲字节数]
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "../../../../source/media/bbb-10s.mp4";
    int outerBuf = argc > 2 ? std::atoi(argv[2]) : 32768, innerBuf = argc > 3 ? std::atoi(argv[3]) : 4096;
    AVIOContext* file = nullptr;
    if (avio_open(&file, path, AVIO_FLAG_READ) < 0) { std::fprintf(stderr, "打不开 %s\n", path); return 1; }

    Counter inner{"内层", innerBuf, file};           // 文件 ← 内层计数 ← 外层计数 ← 解封装器
    AVIOContext* pbInner = wrap(inner);
    Counter outer{"外层", outerBuf, pbInner};
    AVIOContext* pbOuter = wrap(outer);

    AVFormatContext* fmt = avformat_alloc_context();
    fmt->pb = pbOuter;                           // 解封装器只看见最外面的 AVIOContext
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) { std::fprintf(stderr, "解封装失败\n"); return 1; }
    avformat_find_stream_info(fmt, nullptr);

    int packets[2] = {0, 0};
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index < 2) ++packets[pkt->stream_index];
        av_packet_unref(pkt);
    }
    std::printf("文件 %lld 字节；读出 %d 个视频包、%d 个音频包\n", (long long)avio_size(file), packets[0], packets[1]);
    for (Counter* c : {&outer, &inner})
        std::printf("  %s（缓冲 %5d 字节）：read %4ld 次，共 %ld 字节，seek %ld 次\n", c->name, c->bufSize, c->reads, c->bytes, c->seeks);

    av_packet_free(&pkt);
    avformat_close_input(&fmt);                  // 自定义 IO：AVIOContext 要自己释放，从外往里
    freeWrapped(pbOuter);
    freeWrapped(pbInner);
    avio_closep(&file);
}
```

`ffmpeg/build.sh` 会编译并跑两次：一次外层缓冲 32 KB、内层 4 KB，一次反过来：

```
文件 2994711 字节；读出 240 个视频包、470 个音频包
  外层（缓冲 32768 字节）：read   76 次，共 2994711 字节，seek 0 次
  内层（缓冲  4096 字节）：read   76 次，共 2994711 字节，seek 0 次
文件 2994711 字节；读出 240 个视频包、470 个音频包
  外层（缓冲  4096 字节）：read  312 次，共 2994711 字节，seek 0 次
  内层（缓冲 32768 字节）：read   76 次，共 2994711 字节，seek 0 次
```

macOS 上的 FFmpeg 9.0 和 Ubuntu 24.04 上的 FFmpeg 6.1 结果完全相同。几个观察：

- **包数和不套装饰器时一样**（240 个视频包、470 个音频包），解封装器完全不知道中间多了两层。
- **每层都有自己的缓冲区，顺序决定了谁的缓冲起作用**。外层缓冲大时，外层每次向内层要 32 KB，比内层的 4 KB 缓冲区还大，`avio_read` 就绕过内层的缓冲直接读，两层都是 76 次。外层缓冲小时，外层要读 312 次，但内层每次从文件读满 32 KB，又合成了 76 次。真正的文件读取次数由最大的那块缓冲决定，放在哪一层，上面那层看到的读取次数就不一样。
- **seek 0 次**：这个 MP4 的 `moov` box 在文件开头（紧跟在 `ftyp` 后面），解封装器从头读到尾，不用跳。`moov` 在末尾的文件会多出几次 seek。

FFmpeg 自己也大量使用这种结构。它的 URL 协议可以一层套一层：`async:file:bbb-10s.mp4` 是在 `file` 协议外面套了一层后台预读，`cache:` 把里面协议读到的数据存进临时文件以便来回跳转，`crypto:` 边读边解密，`subfile:` 只露出文件的一段。`ffprobe -count_packets async:file:bbb-10s.mp4` 和 `cache:file:...` 数出的包数都和直接读一样（240 和 470）。

其他常见的装饰器：

- Java 的 I/O：`new BufferedReader(new InputStreamReader(new FileInputStream("a.txt")))`，GoF 之后讲装饰器最常用的例子。
- C++17 的 `std::pmr`：`monotonic_buffer_resource`、`unsynchronized_pool_resource` 都是 `memory_resource`，构造时可以传入另一个 `memory_resource` 作为「上游」，自己分配不出来时向上游要。
- Python 的 `@decorator` 语法：把一个函数包成另一个函数，参数和返回值不变，前后加上计时、缓存、重试。它和 GoF 的装饰器是同一个思路，作用对象从对象换成了函数（第 4.2 节的 C++ 写法）。

### 3.4 装饰器的几个坑

**接口大，转发就麻烦**。接口有 10 个虚函数时，每个装饰器都得把 10 个函数转发一遍——所以要有 `SourceDecorator` 这样的基类统一转发。但基类也只能转发它在写的时候存在的函数：接口后来加了 `seek()`，基类忘了转发，所有装饰器的 `seek()` 就都落到了基类的默认实现上。接口越小，装饰器越好写。

**被包住的对象「变了身份」**。包上一层之后，`dynamic_cast<MemorySource*>` 就认不出来了，按具体类型做的判断全部失效（练习 4）。

**装饰器自己的接口被藏起来了**。`Stats` 统计出的包数，包上一层 `Logging` 以后就拿不到了：外面只有一个 `PacketSource` 指针（练习 3）。

**对象多、调用栈深**。每一层是一个独立的对象（一次堆分配），调用时多一次虚函数调用。出了问题，调试器里的调用栈也多出好几层。开销有多大，下一节实测。

## 4. 现代 C++ 写法

### 4.1 模板 mixin：在编译期一层层继承

既然包装的顺序和层数常常在编译时就知道，可以把「里面那一层」当成模板参数，**继承**它而不是持有它：

```cpp
// 《装饰器模式》：模板 mixin——在编译期一层层继承，类型里写清楚套了哪几层，没有虚函数，也不用分配内存
#include <cstdio>

struct Packet { int index = 0; bool key = false; int size = 0; };

class MemorySource {                             // 不需要虚函数，也不需要共同的基类
public:
    bool read(Packet& p) {
        if (i_ >= 240) return false;
        p = {i_, i_ % 48 == 0, i_ % 60 == 59 ? 0 : i_ % 48 == 0 ? 9000 : 1200 + i_ * 37 % 800};
        ++i_;
        return true;
    }
private:
    int i_ = 0;
};

template <class Inner> class Validate : public Inner {      // 丢掉空包
public:
    bool read(Packet& p) {
        while (Inner::read(p)) if (p.size > 0) return true;
        return false;
    }
};

template <class Inner> class Logging : public Inner {
public:
    bool read(Packet& p) {
        if (!Inner::read(p)) return false;
        if (p.key) std::printf("  [log] #%d 关键帧\n", p.index);
        return true;
    }
};

template <class Inner> class Stats : public Inner {
public:
    bool read(Packet& p) {
        if (!Inner::read(p)) return false;
        ++packets_;
        bytes_ += p.size;
        return true;
    }
    int packets() const { return packets_; }    // 最外层的类型知道每一层，额外的接口可以直接调用
    long bytes() const { return bytes_; }
private:
    int packets_ = 0;
    long bytes_ = 0;
};

int main() {
    Stats<Logging<Validate<MemorySource>>> src;  // 读法和对象版一样：从右往左是从里往外
    Packet p;
    while (src.read(p)) {}
    std::printf("%d 个包，%ld 字节；sizeof(src) = %zu 字节\n", src.packets(), src.bytes(), sizeof src);

    Validate<Stats<MemorySource>> other;         // 顺序换一下：Stats 在里面，坏包也被数进去了
    while (other.read(p)) {}
    std::printf("Validate<Stats<...>> 数到 %d 个包\n", other.packets());
}
```

输出：

```
  [log] #0 关键帧
  [log] #48 关键帧
  [log] #96 关键帧
  [log] #144 关键帧
  [log] #192 关键帧
236 个包，413948 字节；sizeof(src) = 16 字节
Validate<Stats<...>> 数到 240 个包
```

`Stats<Logging<Validate<MemorySource>>>` 读起来和对象版的嵌套一样，从右往左是从里往外，顺序同样会影响结果（换个顺序就数到 240）。不同的是：

- **没有虚函数，也没有堆分配**。整个对象只有 16 字节（`MemorySource` 的 `i_`，`Stats` 的两个计数），四层的 `read()` 会被内联成一个函数。
- **最外层的类型知道每一层**，`src.packets()` 可以直接调用，不存在「被藏起来」的问题。
- **组合在编译时就定死了**。不能根据命令行参数决定套哪几层（除非把每种组合都实例化出来，又回到了 2ⁿ 种）；不同组合是不同的类型，不能放进同一个容器。

这种写法叫 **mixin**（混入）：每个类模板把一种功能「混」进它的基类里。第 5 章的 CRTP 会再讲到它的变体。

### 4.2 `std::function` 和 lambda：函数包函数

如果接口只有一个函数，干脆不定义类，用 `std::function` 表示「数据源」，装饰器就是一个「接收一个函数、返回一个新函数」的函数：

```cpp
// 《装饰器模式》：用 std::function 和 lambda 写装饰器——函数包函数，不需要定义类
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

struct Packet { int index = 0; bool key = false; int size = 0; };
using Reader = std::function<bool(Packet&)>;    // 「数据源」就是一个函数：读到返回 true

Reader memorySource() {
    return [i = 0](Packet& p) mutable {
        if (i >= 240) return false;
        p = {i, i % 48 == 0, i % 60 == 59 ? 0 : i % 48 == 0 ? 9000 : 1200 + i * 37 % 800};
        ++i;
        return true;
    };
}

Reader validate(Reader in) {                     // 每个装饰器是一个函数：拿一个 Reader，返回一个新的 Reader
    return [in = std::move(in)](Packet& p) {
        while (in(p)) if (p.size > 0) return true;
        return false;
    };
}

Reader limit(Reader in, int n) {                 // 只读前 n 个包
    return [in = std::move(in), n](Packet& p) mutable { return n-- > 0 && in(p); };
}

// 通用的函数装饰器：给任何可调用对象加上「调用计数」，参数和返回值原样转发
template <class F>
auto counted(std::string name, F f) {
    return [name = std::move(name), f = std::move(f), calls = 0](auto&&... args) mutable -> decltype(auto) {
        if (++calls % 100 == 0) std::printf("  [%s] 第 %d 次调用\n", name.c_str(), calls);
        return f(std::forward<decltype(args)>(args)...);
    };
}

int main() {
    Reader src = limit(validate(counted("source", memorySource())), 200);
    Packet p;
    int n = 0;
    while (src(p)) ++n;
    std::printf("读到 %d 个包，最后一个是 #%d\n", n, p.index);

    auto add = counted("add", [](int a, int b) { return a + b; });   // 普通函数也能装饰
    int sum = 0;
    for (int i = 0; i < 100; ++i) sum = add(sum, i);
    std::printf("sum = %d\n", sum);
}
```

输出：

```
  [source] 第 100 次调用
  [source] 第 200 次调用
读到 200 个包，最后一个是 #202
  [add] 第 100 次调用
sum = 4950
```

`limit(validate(counted("source", memorySource())), 200)` 读到 200 个好包，为此向源头要了 203 次（中间丢掉了 #59、#119、#179 三个坏包）。`counted` 是一个通用的函数装饰器，用泛型 lambda 和完美转发包装任意可调用对象，最后对一个普通的加法 lambda 也能用，这和 Python 的 `@decorator` 是一回事。

lambda 写法很短，但 `std::function` 每一层也是一次间接调用，捕获的东西放不下时还要堆分配；状态（如 `calls`）藏在 lambda 里，外面同样拿不到。

### 4.3 每多一层，多花多少时间

`bench_layers.cpp` 比较三种写法：每层只做「转发 + 加 1」，套 0～16 层，每种 1000 万次调用，重复 7 次取中位数：

<!-- run: off -->
```cpp
// 《装饰器模式》：套 N 层装饰器，每次 read() 多花多少时间（虚函数 / 模板 mixin / std::function 三种写法，请用 -O2 编译）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

constexpr int kCalls = 10'000'000;
volatile long long sink;
volatile long long step = 1;                     // 从 volatile 变量读步长：不让编译器把整个循环算成一个公式

// ---------- 1. 虚函数：每层一个对象，read() 转发给里面一层再加 1 ----------
struct Source { virtual ~Source() = default; virtual long long read() = 0; };
struct Counter : Source { long long i = 0; long long read() override { return i += step; } };
struct AddOne : Source {
    explicit AddOne(std::unique_ptr<Source> in) : in_(std::move(in)) {}
    long long read() override { return in_->read() + 1; }
    std::unique_ptr<Source> in_;
};
[[gnu::noinline]] std::unique_ptr<Source> wrap(int layers) {     // 层数在运行时才知道，编译器没法去掉虚调用
    std::unique_ptr<Source> s = std::make_unique<Counter>();
    for (int i = 0; i < layers; ++i) s = std::make_unique<AddOne>(std::move(s));
    return s;
}

// ---------- 2. 模板 mixin：AddOneT<AddOneT<...<CounterT>>>，层数是类型的一部分 ----------
struct CounterT { long long i = 0; long long read() { return i += step; } };
template <class Base> struct AddOneT : Base { long long read() { return Base::read() + 1; } };
template <int N> struct Nest { using type = AddOneT<typename Nest<N - 1>::type>; };
template <> struct Nest<0> { using type = CounterT; };

// ---------- 3. std::function：每层一个 lambda，捕获里面一层 ----------
[[gnu::noinline]] std::function<long long()> wrapFn(int layers) {
    auto c = std::make_shared<long long>(0);
    std::function<long long()> f = [c] { return *c += step; };
    for (int i = 0; i < layers; ++i) f = [g = std::move(f)] { return g() + 1; };
    return f;
}

template <class F> double nsPerCall(F&& read) {  // 重复 7 次取中位数
    std::vector<double> t;
    for (int r = 0; r < 7; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        long long acc = 0;
        for (int i = 0; i < kCalls; ++i) acc += read();
        sink = acc;
        auto t1 = std::chrono::steady_clock::now();
        t.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / kCalls);
    }
    std::sort(t.begin(), t.end());
    return t[3];
}

template <int N> double mixin() { typename Nest<N>::type s; return nsPerCall([&] { return s.read(); }); }

int main() {
    std::printf("层数   虚函数    模板mixin  std::function  (ns/次)\n");
    const int layers[] = {0, 1, 2, 4, 8, 16};
    const double mix[] = {mixin<0>(), mixin<1>(), mixin<2>(), mixin<4>(), mixin<8>(), mixin<16>()};
    for (int k = 0; k < 6; ++k) {
        auto v = wrap(layers[k]);
        auto f = wrapFn(layers[k]);
        double a = nsPerCall([&] { return v->read(); });
        double c = nsPerCall([&] { return f(); });
        std::printf("%4d %8.2f %10.2f %12.2f\n", layers[k], a, mix[k], c);
    }
    // 参照：一次真正的「读包」至少要复制数据。复制一个 1.7 KB 的包要多久
    std::vector<unsigned char> from(1700, 7), to;
    std::printf("参照：复制 1.7 KB 的包 %.2f ns/次\n", nsPerCall([&] { to.assign(from.begin(), from.end()); return (long long)to[0]; }));
}
```

结果（ns/次，Clang 21 / macOS 在前，GCC 13 / Ubuntu 在后）：

| 层数 | 虚函数 | 模板 mixin | `std::function` |
| --- | --- | --- | --- |
| 0 | 0.63 / 0.48 | 0.21 / 0.25 | 0.52 / 0.67 |
| 1 | 0.96 / 1.09 | 0.44 / 0.28 | 1.11 / 1.10 |
| 2 | 1.52 / 1.57 | 0.43 / 0.27 | 1.53 / 1.56 |
| 4 | 2.44 / 2.45 | 0.43 / 0.26 | 2.48 / 2.42 |
| 8 | 4.39 / 4.86 | 0.43 / 0.26 | 4.42 / 4.24 |
| 16 | 8.58 / 9.25 | 0.44 / 0.25 | 8.56 / 8.61 |

<figure class="diagram">
<svg viewBox="0 0 730 246" role="img" aria-label="每次 read() 的耗时：不套装饰器约 0.5 ns；虚函数套 4 层约 2.4 ns，套 16 层 Clang 8.58 ns、GCC 9.25 ns；std::function 16 层约 8.6 ns；模板 mixin 16 层 Clang 0.44 ns、GCC 0.25 ns；作为参照，复制一个 1.7 KB 的包要 13 ns 多"><g style="font-family:var(--mono)"><text x="188.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang 21 / macOS</text><rect x="188.0" y="30.0" width="8.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="202.1" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.63 ns</text><rect x="188.0" y="60.0" width="31.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="225.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2.44 ns</text><rect x="188.0" y="90.0" width="110.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="304.3" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8.58 ns</text><rect x="188.0" y="120.0" width="110.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="304.1" y="135.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8.56 ns</text><rect x="188.0" y="150.0" width="5.7" height="20.0" rx="2" style="fill:var(--link)"/><text x="199.7" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.44 ns</text><rect x="188.0" y="180.0" width="174.9" height="20.0" rx="2" style="fill:var(--muted)"/><text x="368.9" y="195.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px;font-weight:700">13.6 ns</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="458.0" y="30.0" width="6.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="470.2" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.48 ns</text><rect x="458.0" y="60.0" width="31.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="495.5" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2.45 ns</text><rect x="458.0" y="90.0" width="118.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="582.9" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">9.25 ns</text><rect x="458.0" y="120.0" width="110.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="574.7" y="135.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8.61 ns</text><rect x="458.0" y="150.0" width="3.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="467.2" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.25 ns</text><rect x="458.0" y="180.0" width="169.1" height="20.0" rx="2" style="fill:var(--muted)"/><text x="633.1" y="195.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px;font-weight:700">13.15 ns</text><text x="180.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">不套（0 层，虚函数）</text><text x="180.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数 4 层</text><text x="180.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数 16 层</text><text x="180.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::function 16 层</text><text x="180.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">模板 mixin 16 层</text><text x="180.0" y="195.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">参照：复制 1.7 KB</text><text x="10.0" y="238.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_layers，-O2，每种 1000 万次调用，重复 7 次取中位数；每层做的事只是「转发 + 加 1」</text></g></svg>
</figure>

- **虚函数和 `std::function` 几乎一样**，每多一层约多 0.50 ns（Clang）/ 0.55 ns（GCC），套 16 层是 8.58 / 9.25 ns。两者都是「一层一次间接调用」，编译器看不穿，没法内联。
- **模板 mixin 套多少层都一样**（16 层 0.44 / 0.25 ns），所有层都被内联成了一个循环体。
- **和真正的工作比，这点开销可以忽略**。仅仅复制一个 1.7 KB 的包就要 13.60 / 13.15 ns，相当于套了 25 层虚函数装饰器；解码一帧视频要几毫秒，是这个的几十万倍。装饰器加在「每个包」这样的粒度上没问题，加在「每个采样」「每个像素」这样极细的粒度上，才值得换成 mixin。

（GCC 的数字是在 Apple 芯片上的 colima 虚拟机里跑的，同一颗 CPU，所以两边很接近。）

### 4.4 怎么选

| | 虚函数 + `unique_ptr` | 模板 mixin | `std::function` + lambda |
| --- | --- | --- | --- |
| 运行时决定套哪几层 | 可以 | 不行 | 可以 |
| 每层的开销 | 一次虚调用 + 一次堆分配 | 0（内联） | 一次间接调用，可能堆分配 |
| 访问某一层自己的接口 | 拿不到（要另想办法） | 直接调用 | 拿不到 |
| 接口有多个函数 | 基类统一转发 | 继承自动「转发」 | 不适合 |
| 适合 | 插件式的功能开关、滤镜链 | 编译期确定的组合、热点路径 | 只有一个函数的小接口、回调 |

## 5. 装饰器和相似模式的区别

| 模式 | 结构 | 目的 | 和装饰器的区别 |
| --- | --- | --- | --- |
| 适配器 | 包住一个对象 | 把接口 A 转成接口 B | 适配器**改变接口**，装饰器接口不变、只加功能。装饰器能一层套一层，适配器一般只有一层 |
| 代理 | 包住一个对象，接口相同 | 控制访问：延迟创建、权限检查、远程调用 | 结构几乎一样，区别在意图和谁来组装：代理通常自己创建、管理被代理的对象，使用者不知道它存在；装饰器由使用者在外面一层层套上去 |
| 组合 | 容器持有很多个子节点 | 把一组对象当成一个对象 | 组合有**很多个**子节点，结果是汇总；装饰器只有**一个**，结果是加工 |
| 责任链 | 每个处理者持有下一个 | 找到能处理请求的那个 | 责任链的处理者可以**不往下传**（处理了就结束）；装饰器一般**总是转发**，只是前后加点事。`Validate` 丢包、`Limit` 提前结束，已经带一点责任链的味道 |
| 策略 | 持有一个算法对象 | 替换算法 | GoF 的说法：装饰器改变对象的「外表」（skin），策略改变对象的「内核」（guts） |

## 6. 小结

1. 功能可以自由组合时，用继承要给每种组合写一个子类：n 种功能 2ⁿ − 1 个。实测 3 种功能 7 个子类，加一种变成 15 个，新增 88 行；装饰器只新增 1 个类、13 行。
2. 装饰器的三个要点：和被装饰的对象**实现同一个接口**；**持有**一个接口类型的对象；**转发**调用，并在前后加上自己的功能。可以在运行时一层层套。
3. **包装顺序会影响结果**：统计在丢坏包里面还是外面（236 和 240）、先放大还是先限幅（0 和 260 个越界采样）、大缓冲在外层还是内层（76 和 312 次读取）。改数据、丢数据、提前结束的装饰器要想清楚放在哪一层。
4. 坑：接口大时转发麻烦（用基类统一转发）；被包住的对象认不出具体类型；装饰器自己的额外接口被藏起来。
5. 每层虚函数装饰器约 0.5 ns，和复制一个包（13 ns）相比可以忽略。组合在编译期就确定、又在热点路径上时，用模板 mixin，套多少层都不花时间；只有一个函数的小接口，用 `std::function` 和 lambda 最简单。

## 7. 练习

1. 下面哪些是装饰器？说明理由。
   ① `std::stack<T, std::deque<T>>`；② FFmpeg 的 `async:` 协议（`async:file:a.mp4`）；③ 一个 `TimedSource`，接口和 `PacketSource` 一样，但它在构造函数里自己 `new` 一个 `MemorySource`，使用者不能换成别的数据源；④ Java 的 `new BufferedInputStream(new FileInputStream("a.mp4"))`。

2. 给第 2 节的代码加一个装饰器 `Limit`，只读前 n 个包（预览时用）。`Limit(Validate(src), 100)` 和 `Validate(Limit(src, 100))` 各读出几个包？为什么？

3. `Stats` 被别的装饰器包在里面以后，外面只有一个 `PacketSource` 指针，拿不到它的统计结果。设计一种办法，让使用者既能拿到「丢坏包之前」的包数，也能拿到「丢坏包之后」的包数。

4. 下面的代码想在开始读之前打印文件一共有几个包。没包装时能打印出 240，包上一层 `Validate` 以后就打印「不知道」了。问题在哪里？怎么改？

   <!-- run: off -->
   ```cpp
   class FileInfo : public MemorySource {
   public:
       int packetCount() const { return 240; }
   };

   void describe(PacketSource& src) {
       if (auto* f = dynamic_cast<FileInfo*>(&src))
           std::printf("共 %d 个包\n", f->packetCount());
       else
           std::printf("不知道\n");
   }

   auto src = std::make_unique<Validate>(std::make_unique<FileInfo>());
   describe(*src);                                  // 不知道
   ```

<details>
<summary>点击查看答案</summary>

1. **② 和 ④ 是装饰器，① 是适配器，③ 更像代理**。
   - ① `std::stack` 把 `deque` 的接口（`push_back`、`back`、`pop_back`）换成了 `push`、`top`、`pop`，接口变了，这是适配器（《适配器模式》讲过的容器适配器）。
   - ② `async:` 本身是一个协议，和 `file:` 实现同样的协议接口（`url_read`、`url_seek`……），里面包着另一个协议，读的时候转发给它，只是多了后台预读。还可以换成包住 `http:` 等别的协议，是典型的装饰器。
   - ③ 接口相同、也在转发，但被包的对象是它自己创建的，使用者不能决定包住谁，也就没法一层层自由组合。这种「自己管理被包的对象、控制对它的访问」的结构是代理（本章后面的《代理模式》）。
   - ④ `BufferedInputStream` 和 `FileInputStream` 都是 `InputStream`，前者包住后者，加上缓冲，是 Java I/O 里的经典装饰器。

2. 实现时要注意**先判断还能不能读，再向里面要**，否则第 n + 1 次调用会多读一个包，被吞掉：

   ```cpp
   // 《装饰器模式》练习 2：加一个 Limit 装饰器（只读前 n 个包），它和 Validate 谁在外面，结果不一样
   #include <cstdio>
   #include <memory>

   struct Packet { int index = 0; bool key = false; int size = 0; };

   class PacketSource {
   public:
       virtual ~PacketSource() = default;
       virtual bool read(Packet& out) = 0;
   };

   class MemorySource : public PacketSource {       // 240 个包，第 59、119、179、239 个是空包
   public:
       bool read(Packet& p) override {
           if (i_ >= 240) return false;
           p = {i_, i_ % 48 == 0, i_ % 60 == 59 ? 0 : i_ % 48 == 0 ? 9000 : 1200 + i_ * 37 % 800};
           ++i_;
           return true;
       }
   private:
       int i_ = 0;
   };

   class SourceDecorator : public PacketSource {
   public:
       explicit SourceDecorator(std::unique_ptr<PacketSource> in) : inner_(std::move(in)) {}
       bool read(Packet& p) override { return inner_->read(p); }
   protected:
       std::unique_ptr<PacketSource> inner_;
   };

   class Validate : public SourceDecorator {        // 丢掉空包
   public:
       using SourceDecorator::SourceDecorator;
       bool read(Packet& p) override {
           while (inner_->read(p)) if (p.size > 0) return true;
           return false;
       }
   };

   class Limit : public SourceDecorator {
   public:
       Limit(std::unique_ptr<PacketSource> in, int n) : SourceDecorator(std::move(in)), left_(n) {}
       bool read(Packet& p) override {
           if (left_ == 0) return false;            // 先判断还能不能读，不要多读一个包
           --left_;
           return inner_->read(p);
       }
   private:
       int left_;
   };

   int count(PacketSource& src) {
       Packet p;
       int n = 0;
       while (src.read(p)) ++n;
       return n;
   }

   int main() {
       auto a = std::make_unique<Limit>(std::make_unique<Validate>(std::make_unique<MemorySource>()), 100);
       auto b = std::make_unique<Validate>(std::make_unique<Limit>(std::make_unique<MemorySource>(), 100));
       std::printf("Limit(Validate(src), 100)：%d 个包\n", count(*a));
       std::printf("Validate(Limit(src, 100))：%d 个包\n", count(*b));
   }
   ```

   输出：

   ```
   Limit(Validate(src), 100)：100 个包
   Validate(Limit(src, 100))：99 个包
   ```

   `Limit` 在外面时，它数的是 `Validate` 交出来的好包，读满 100 个好包为止（实际向源头要了 101 个，中间丢了 #59）。`Limit` 在里面时，它只放出源头的前 100 个包，其中 #59 是坏包，被外面的 `Validate` 丢掉，只剩 99 个。预览要「前 100 个能播的包」，`Limit` 应该放在外面。

3. 办法之一：**让 `Stats` 把结果写到外面的对象里**，装饰器只持有它的引用。同一条链上套两个 `Stats`，一个在 `Validate` 里面，一个在外面：

   ```cpp
   // 《装饰器模式》练习 3：Stats 被别的装饰器包在里面以后，怎么拿到它的统计结果——让它把结果写到外面的对象里
   #include <cstdio>
   #include <memory>

   struct Packet { int index = 0; bool key = false; int size = 0; };

   class PacketSource {
   public:
       virtual ~PacketSource() = default;
       virtual bool read(Packet& out) = 0;
   };

   class MemorySource : public PacketSource {       // 240 个包，第 59、119、179、239 个是空包
   public:
       bool read(Packet& p) override {
           if (i_ >= 240) return false;
           p = {i_, i_ % 48 == 0, i_ % 60 == 59 ? 0 : i_ % 48 == 0 ? 9000 : 1200 + i_ * 37 % 800};
           ++i_;
           return true;
       }
   private:
       int i_ = 0;
   };

   class SourceDecorator : public PacketSource {
   public:
       explicit SourceDecorator(std::unique_ptr<PacketSource> in) : inner_(std::move(in)) {}
       bool read(Packet& p) override { return inner_->read(p); }
   protected:
       std::unique_ptr<PacketSource> inner_;
   };

   class Validate : public SourceDecorator {        // 丢掉空包
   public:
       using SourceDecorator::SourceDecorator;
       bool read(Packet& p) override {
           while (inner_->read(p)) if (p.size > 0) return true;
           return false;
       }
   };

   struct StatsResult { int packets = 0; long bytes = 0; };

   class Stats : public SourceDecorator {
   public:
       Stats(std::unique_ptr<PacketSource> in, StatsResult& out) : SourceDecorator(std::move(in)), out_(out) {}
       bool read(Packet& p) override {
           if (!inner_->read(p)) return false;
           ++out_.packets;
           out_.bytes += p.size;
           return true;
       }
   private:
       StatsResult& out_;                           // 结果放在装饰器外面，谁都能看
   };

   int main() {
       StatsResult raw, valid;                      // 同一条链上可以放两个 Stats：坏包丢掉前、丢掉后各统计一次
       std::unique_ptr<PacketSource> src = std::make_unique<MemorySource>();
       src = std::make_unique<Stats>(std::move(src), raw);
       src = std::make_unique<Validate>(std::move(src));
       src = std::make_unique<Stats>(std::move(src), valid);

       Packet p;
       while (src->read(p)) {}
       std::printf("丢包前 %d 个包，丢包后 %d 个包，丢掉 %d 个\n", raw.packets, valid.packets, raw.packets - valid.packets);
   }
   ```

   输出：`丢包前 240 个包，丢包后 236 个包，丢掉 4 个`。

   其他办法：在 `std::move` 进去之前先留一个裸指针（`Stats* stats = p.get();`），只要整条链活着，这个指针就有效，但要小心生命周期；或者改用第 4.1 节的模板 mixin，最外层的类型知道每一层，可以直接调用 `src.packets()`。不推荐的办法是用 `dynamic_cast` 一层层往里找，这要给每个装饰器加一个「取出里面那一层」的函数，把封装全打破了。

4. **装饰器改变了对象的「身份」**：`src` 的动态类型是 `Validate`，`dynamic_cast<FileInfo*>` 当然失败，按具体类型做的判断在包上一层之后全部失效。修正的办法是**把要问的问题放进接口**，由装饰器基类转发：

   ```cpp
   // 《装饰器模式》练习 4：包上装饰器以后，dynamic_cast 就认不出原来的对象了
   #include <cstdio>
   #include <memory>

   struct Packet { int index = 0; bool key = false; int size = 0; };

   class PacketSource {
   public:
       virtual ~PacketSource() = default;
       virtual bool read(Packet& out) = 0;
       virtual int total() const { return -1; }     // 修正：把要问的问题放进接口，-1 表示不知道
   };

   class MemorySource : public PacketSource {       // 240 个包，第 59、119、179、239 个是空包
   public:
       bool read(Packet& p) override {
           if (i_ >= 240) return false;
           p = {i_, i_ % 48 == 0, i_ % 60 == 59 ? 0 : i_ % 48 == 0 ? 9000 : 1200 + i_ * 37 % 800};
           ++i_;
           return true;
       }
   private:
       int i_ = 0;
   };

   class SourceDecorator : public PacketSource {
   public:
       explicit SourceDecorator(std::unique_ptr<PacketSource> in) : inner_(std::move(in)) {}
       bool read(Packet& p) override { return inner_->read(p); }
       int total() const override { return inner_->total(); }   // 修正：装饰器把问题转给里面一层
   protected:
       std::unique_ptr<PacketSource> inner_;
   };

   class Validate : public SourceDecorator {        // 丢掉空包
   public:
       using SourceDecorator::SourceDecorator;
       bool read(Packet& p) override {
           while (inner_->read(p)) if (p.size > 0) return true;
           return false;
       }
   };

   class FileInfo : public MemorySource {           // 原来的写法：只有 FileInfo 知道总包数
   public:
       int packetCount() const { return 240; }
       int total() const override { return 240; }
   };

   void describe(const char* what, PacketSource& src) {
       if (auto* f = dynamic_cast<FileInfo*>(&src))   // 有问题的写法：按具体类型去问
           std::printf("%s：dynamic_cast 得到 %d 个包", what, f->packetCount());
       else
           std::printf("%s：dynamic_cast 失败", what);
       std::printf("；total()：%d\n", src.total());
   }

   int main() {
       FileInfo plain;
       describe("没包装", plain);
       auto wrapped = std::make_unique<Validate>(std::make_unique<FileInfo>());
       describe("包了一层 Validate", *wrapped);
   }
   ```

   输出：

   ```
   没包装：dynamic_cast 得到 240 个包；total()：240
   包了一层 Validate：dynamic_cast 失败；total()：240
   ```

   `total()` 在 `PacketSource` 里有默认实现（返回 −1，表示不知道），`SourceDecorator` 把它转发给里面一层，于是不管套了几层，都能问到最里面的 `FileInfo`。严格说来，`Validate` 丢掉坏包以后，能读出的包不再是 240 个，它应该重写 `total()` 返回「不知道」或者改过的数——这正是装饰器要想清楚的地方：每加一个接口函数，都要想想每个装饰器该原样转发，还是该改一改。

</details>
