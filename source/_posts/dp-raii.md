---
title: RAII 与 Scope Guard
date: 2026-11-20 11:00:00
updated: 2026-11-20 11:00:00
categories:
  - 设计模式
  - C++惯用法
tags:
  - 设计模式
  - C++惯用法
  - RAII
  - Scope Guard
  - 资源管理
  - Rule of Zero
order: 1
description: 在视频解码管线里手动管理 C 风格的 Demuxer、Decoder、FrameBuf 句柄时，只要函数里多出几条提前 return 路径，就极易漏调释放函数造成句柄泄漏。用 C++ 核心惯用法 RAII 把资源生命周期绑死在栈对象上：对象构造即获取资源、离开作用域时编译器按构造逆序（LIFO）自动析构释放；对一次性的事务回滚或临时状态恢复，则用通用 ScopeGuard + dismiss() 兜底。Apple clang 21 -O2 实测 5000 万次句柄开关循环：裸指针手动释放 14.72 ms（0.29 ns/次，sizeof=8 B），手写只移动 RAII Handle 14.31 ms（0.29 ns/次，8 B），std::unique_ptr 空函数对象删除器 14.29 ms（0.29 ns/次，8 B），真正零开销；而函数指针删除器因无法空基类优化与间接调用，体积翻倍到 16 B、耗时升至 31.61 ms（0.63 ns/次）。
top_group_list: true
disableNunjucks: true
viz: [dp-raii]
---

> **本文目标**：理解为什么 **RAII（Resource Acquisition Is Initialization，资源获取即初始化）** 是 C++ 最重要、最基础的惯用法；掌握用 **`std::unique_ptr` 空删除器（Rule of 0）** 与 **手写只移动 Handle（Rule of 5）** 封装 C 风格资源的标准写法；学会用通用 **`ScopeGuard`（含 `dismiss()`）** 处理事务回滚与临时状态恢复；通过实测看清 RAII 真正的「零开销抽象」与函数指针删除器的隐性代价；分清 RAII、Scope Guard、`defer`/`finally` 与 GC 的本质区别。
>
> **前置知识**：熟悉 C++ 构造/析构函数、移动语义（`std::move` / `std::exchange`）与 `std::unique_ptr`。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整实验、压测与练习代码见 `projects/dp-lab/29-raii/`。

## 1. 问题：多条出口让手动释放防不胜防

### 1.1 解码管线的 C 风格句柄与提前返回

音视频工程里充斥着大量 C 风格资源接口（如 FFmpeg 的 `AVFormatContext`、`AVCodecContext`、`AVFrame`，以及 POSIX 文件描述符、GPU 纹理句柄、互斥锁）：获取资源调用 `open_*` / `alloc_*`，释放资源必须配对调用 `close_*` / `free_*`，而且通常要求**按获取的逆序释放**（先释放帧缓冲，再关解码器，最后关解封装器）。

把打开解封装器 `Demuxer` → 打开解码器 `Decoder` → 分配帧缓冲 `FrameBuf` → 解码首帧写进一个函数 `decodeFirstFrame()`，如果靠程序员手工在每条出口补释放代码，很快就长成这样：

```cpp
// 《RAII 与 Scope Guard》：问题版——手动释放 C 风格句柄，多条提前 return 路径导致资源泄漏
#include <cstdio>

struct Demuxer  { int id; };
struct Decoder  { int id; };
struct FrameBuf { int id; };

static int g_live = 0;

Demuxer*  open_demuxer(const char*)  { ++g_live; std::printf("  [+] open  Demuxer\n");  return new Demuxer{1}; }
void      close_demuxer(Demuxer* p)  { if (p) { --g_live; std::printf("  [-] close Demuxer\n");  delete p; } }
Decoder*  open_decoder(Demuxer*)     { ++g_live; std::printf("  [+] open  Decoder\n");  return new Decoder{2}; }
void      close_decoder(Decoder* p)  { if (p) { --g_live; std::printf("  [-] close Decoder\n");  delete p; } }
FrameBuf* alloc_frame(int)           { ++g_live; std::printf("  [+] alloc FrameBuf\n"); return new FrameBuf{3}; }
void      free_frame(FrameBuf* p)    { if (p) { --g_live; std::printf("  [-] free  FrameBuf\n"); delete p; } }

// failStep: 0=正常, 2=打开解码器后初始化失败, 4=解码帧失败
int decodeFirstFrame(const char* url, int failStep) {
  Demuxer* dm = open_demuxer(url);
  if (!dm) return -1;

  Decoder* dec = open_decoder(dm);
  if (!dec || failStep == 2) {
    // 坑 ①：初始化失败时只释放了 dec，忘了 close_demuxer(dm)！
    close_decoder(dec);
    return -2;
  }

  FrameBuf* fb = alloc_frame(1920 * 1080);
  if (!fb) {
    close_decoder(dec);
    close_demuxer(dm);
    return -3;
  }

  if (failStep == 4) {
    // 坑 ②：后来维护者新增提前返回分支，直接 return -4，三个句柄全部泄漏！
    return -4;
  }

  free_frame(fb);
  close_decoder(dec);
  close_demuxer(dm);
  return 0;
}

int main() {
  std::printf("== 场景 1：正常路径 ==\n");
  int rc1 = decodeFirstFrame("clip.mp4", 0);
  std::printf("rc=%d, 未释放句柄=%d\n", rc1, g_live);

  std::printf("== 场景 2：解码器初始化失败 ==\n");
  int rc2 = decodeFirstFrame("clip.mp4", 2);
  std::printf("rc=%d, 未释放句柄=%d\n", rc2, g_live);

  std::printf("== 场景 3：解码帧失败 ==\n");
  int rc3 = decodeFirstFrame("clip.mp4", 4);
  std::printf("rc=%d, 未释放句柄=%d\n", rc3, g_live);
}
```

```text
== 场景 1：正常路径 ==
  [+] open  Demuxer
  [+] open  Decoder
  [+] alloc FrameBuf
  [-] free  FrameBuf
  [-] close Decoder
  [-] close Demuxer
rc=0, 未释放句柄=0
== 场景 2：解码器初始化失败 ==
  [+] open  Demuxer
  [+] open  Decoder
  [-] close Decoder
rc=-2, 未释放句柄=1
== 场景 3：解码帧失败 ==
  [+] open  Demuxer
  [+] open  Decoder
  [+] alloc FrameBuf
rc=-4, 未释放句柄=4
```

### 1.2 手动释放的三个结构性缺陷

三次调用后，`g_live` 已经悄悄涨到了 **4**（场景 2 漏了 1 个 `Demuxer`，场景 3 漏了整整 3 个句柄）。手动配对释放的病根不在「程序员粗心」，而在于它有三个结构性缺陷：

1. **出口数量与清理代码成乘法爆炸**：函数里有 $n$ 个资源、$m$ 条错误返回路径，就必须在 $m$ 处重复写清理逻辑，而且每一处该释放哪几个句柄、按什么顺序释放各不相同；日后只要有人在中间多加一句 `if (...) return -4;`，就会瞬间埋下泄漏。
2. **获取与释放物理分离**：`open_demuxer()` 写在函数开头，`close_demuxer()` 散落在几十行外的多个分支里，代码审查时很难一眼确认「每条路径都成对闭环」。
3. **异常路径完全失守**：即便在不支持或禁用异常的 C 代码里可以用 `goto cleanup;` 勉强收敛，但一旦进入标准 C++ 环境，中间任何一行 `std::vector` 扩容或业务逻辑抛出异常，都会直接跳过函数尾部的手动 `close_*` / `free_*` 语句。

<figure class="diagram">
<svg viewBox="0 0 700 184" role="img" aria-label="左边是手动清理：多条提前 return 分支散落着重复的 close 调用，容易漏放或被异常跳过；右边是 RAII：资源绑定到栈对象，无论正常返回、提前 return 还是异常展开，编译器都按构造逆序自动析构释放"><g style="font-family:var(--mono)"><text x="168" y="20" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">手动释放（多出口易漏）</text><rect x="20" y="34" width="130" height="30" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="85" y="53" text-anchor="middle" style="fill:var(--fg);font-size:11px">open dm → dec → fb</text><line x1="150" y1="49" x2="188" y2="49" style="stroke:var(--danger);stroke-width:1.4"/><rect x="188" y="34" width="134" height="28" rx="4" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.3"/><text x="255" y="52" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">return -2 (漏 dm)</text><line x1="85" y1="64" x2="85" y2="92" style="stroke:var(--border-2);stroke-width:1.4"/><line x1="85" y1="92" x2="188" y2="92" style="stroke:var(--danger);stroke-width:1.4"/><rect x="188" y="78" width="134" height="28" rx="4" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.3"/><text x="255" y="96" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">return -4 (全漏!)</text><line x1="85" y1="92" x2="85" y2="136" style="stroke:var(--border-2);stroke-width:1.4"/><line x1="85" y1="136" x2="188" y2="136" style="stroke:var(--border-2);stroke-width:1.4"/><rect x="188" y="122" width="134" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="255" y="140" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">return 0 (手写 3 句)</text><text x="168" y="170" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">每加一条出口都要人工补释放</text><text x="528" y="20" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">RAII（编译器栈展开兜底）</text><rect x="372" y="34" width="146" height="96" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.5"/><text x="445" y="54" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">函数栈帧（LIFO）</text><text x="386" y="74" style="fill:var(--accent);font-size:10.5px">3. FrameHandle fb</text><text x="386" y="94" style="fill:var(--accent);font-size:10.5px">2. DecoderPtr dec</text><text x="386" y="114" style="fill:var(--accent);font-size:10.5px">1. DemuxerPtr dm</text><line x1="518" y1="82" x2="548" y2="82" style="stroke:var(--accent);stroke-width:1.5"/><rect x="548" y="42" width="132" height="80" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="614" y="64" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">任意出口离开</text><text x="614" y="84" text-anchor="middle" style="fill:var(--accent);font-size:10.5px">return / break / 异常</text><text x="614" y="104" text-anchor="middle" style="fill:var(--muted);font-size:10px">自动 ~fb→~dec→~dm</text><text x="528" y="170" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">只析构已构造对象，零泄漏、零重复</text></g></svg>
</figure>

**破局思路**：把「记得释放资源」这份责任，从**人脑记忆**彻底转移给**编译器**。

## 2. RAII：把资源生命周期绑死在栈对象上

### 2.1 意图与核心机制

> **RAII（Resource Acquisition Is Initialization，Bjarne Stroustrup 提出）**：将任意有限资源（内存、文件句柄、套接字、互斥锁、GPU 纹理、C 解码器上下文）的生命周期，与一个**栈对象**的生命周期严格绑定——**在构造函数中获取资源，在析构函数中释放资源**。

之所以在 C++ 里威力无比，全靠语言标准的两条铁律：

1. **确定性析构（Deterministic Destruction）**：只要控制流离开作用域——无论是正常执行到 `}`、中途 `return` / `break` / `continue`，还是发生异常触发**栈展开（Stack Unwinding）**——编译器都保证立刻、同步地调用当前作用域内所有**已构造完成**的局部对象的析构函数。
2. **严格逆序（LIFO）**：多个栈对象按构造顺序的**精确逆序**析构。先构造 `dm`，再构造 `dec`，再构造 `fb`，离开作用域时必然按 `~fb()` → `~dec()` → `~dm()` 释放，天然满足底层资源的依赖关系。

> [!NOTE]
> 本文可运行代码块面向浏览器 WASM Clang 环境，使用提前 `return` 与错误码演示确定性清理；在支持异常的原生 C++ 环境中，当某行代码 `throw` 异常时，编译器执行完全相同的栈展开逻辑——所有已构造的栈对象同样按逆序析构，这就是 C++ 异常安全（Exception Safety）的基石。

### 2.2 用 RAII 重构解码管线：`unique_ptr` 空删除器 vs 手写只移动 Handle

封装 C 风格句柄有两种最常用的标准手法：

- **方式 A（推荐，Rule of 0）**：给 `std::unique_ptr<T, Deleter>` 配一个**无状态空结构体删除器**（`struct DemuxerDeleter { void operator()(Demuxer* p) const noexcept { close_demuxer(p); } };`）。几行代码就能免费白嫖 `unique_ptr` 写好的移动语义、空指针检查、`get()`/`reset()`/`release()`，而且零额外体积开销。
- **方式 B（Rule of 5）**：当句柄不是指针（例如 Linux 文件描述符 `int fd`、OpenGL 纹理 `GLuint`），或者需要附加业务方法时，手写一个**只移动（move-only）**的 RAII 类：析构释放，禁用拷贝（`= delete`），实现 `noexcept` 移动构造和移动赋值（用 `std::exchange(o.ptr_, nullptr)` 接管所有权并把源对象置空）。

把两者放进同一个 `decodeFirstFrame()` 看看效果：

```cpp
// 《RAII 与 Scope Guard》：标准 RAII 封装——unique_ptr 空删除器（Rule of 0）与手写只移动 Handle（Rule of 5）
#include <cstdio>
#include <memory>
#include <utility>

struct Demuxer  { int id; };
struct Decoder  { int id; };
struct FrameBuf { int id; };

static int g_live = 0;

Demuxer*  open_demuxer(const char*)  { ++g_live; std::printf("  [+] open  Demuxer\n");  return new Demuxer{1}; }
void      close_demuxer(Demuxer* p)  { if (p) { --g_live; std::printf("  [-] close Demuxer\n");  delete p; } }
Decoder*  open_decoder(Demuxer*)     { ++g_live; std::printf("  [+] open  Decoder\n");  return new Decoder{2}; }
void      close_decoder(Decoder* p)  { if (p) { --g_live; std::printf("  [-] close Decoder\n");  delete p; } }
FrameBuf* alloc_frame(int)           { ++g_live; std::printf("  [+] alloc FrameBuf\n"); return new FrameBuf{3}; }
void      free_frame(FrameBuf* p)    { if (p) { --g_live; std::printf("  [-] free  FrameBuf\n"); delete p; } }

// 方式 A：空函数对象删除器 + std::unique_ptr（零额外空间开销，推荐用于 C 指针句柄）
struct DemuxerDeleter { void operator()(Demuxer* p) const noexcept { close_demuxer(p); } };
struct DecoderDeleter { void operator()(Decoder* p) const noexcept { close_decoder(p); } };
using DemuxerPtr = std::unique_ptr<Demuxer, DemuxerDeleter>;
using DecoderPtr = std::unique_ptr<Decoder, DecoderDeleter>;

// 方式 B：手写只移动 RAII Handle（Rule of 5：禁拷贝、支持移动、析构自动释放）
class FrameHandle {
 public:
  explicit FrameHandle(FrameBuf* p = nullptr) noexcept : ptr_(p) {}
  ~FrameHandle() { free_frame(ptr_); }
  FrameHandle(const FrameHandle&) = delete;
  FrameHandle& operator=(const FrameHandle&) = delete;
  FrameHandle(FrameHandle&& o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {}
  FrameHandle& operator=(FrameHandle&& o) noexcept {
    if (this != &o) {
      free_frame(ptr_);
      ptr_ = std::exchange(o.ptr_, nullptr);
    }
    return *this;
  }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }
  FrameBuf* get() const noexcept { return ptr_; }
 private:
  FrameBuf* ptr_;
};

int decodeFirstFrame(const char* url, int failStep) {
  DemuxerPtr dm(open_demuxer(url));
  if (!dm) return -1;

  DecoderPtr dec(open_decoder(dm.get()));
  if (!dec || failStep == 2) return -2;      // 提前返回：dec → dm 自动逆序释放

  FrameHandle fb(alloc_frame(1920 * 1080));
  if (!fb) return -3;

  if (failStep == 4) return -4;              // 提前返回：fb → dec → dm 自动逆序释放
  return 0;                                  // 正常返回：fb → dec → dm 自动逆序释放
}

int main() {
  for (int step : {0, 2, 4}) {
    std::printf("== failStep=%d ==\n", step);
    int rc = decodeFirstFrame("clip.mp4", step);
    std::printf("rc=%d, 未释放句柄=%d\n", rc, g_live);
  }
}
```

```text
== failStep=0 ==
  [+] open  Demuxer
  [+] open  Decoder
  [+] alloc FrameBuf
  [-] free  FrameBuf
  [-] close Decoder
  [-] close Demuxer
rc=0, 未释放句柄=0
== failStep=2 ==
  [+] open  Demuxer
  [+] open  Decoder
  [-] close Decoder
  [-] close Demuxer
rc=-2, 未释放句柄=0
== failStep=4 ==
  [+] open  Demuxer
  [+] open  Decoder
  [+] alloc FrameBuf
  [-] free  FrameBuf
  [-] close Decoder
  [-] close Demuxer
rc=-4, 未释放句柄=0
```

注意函数体本身的变化：`decodeFirstFrame()` 里**一行 `close_*` / `free_*` 都没有**，无论正常返回还是任何一步提前 `return`，所有已打开句柄都按严格逆序关闭，`g_live` 恒为 `0`。

### 2.3 UML 类图、时序图与交互演示

```uml
label: RAII 与 Scope Guard 结构图：资源句柄通过 unique_ptr 空删除器或只移动 Handle 绑定到栈帧，临时清理与事务回滚由模板 ScopeGuard<F> 托管
classes:
  - { id: DemuxerPtr, x: 20, y: 20, w: 205, attrs: ['- ptr_ : Demuxer*', '- (EBO) DemuxerDeleter'], ops: ['+ ~unique_ptr() { close_demuxer }', '+ get() : Demuxer*'] }
  - { id: FrameHandle, x: 255, y: 20, w: 215, attrs: ['- ptr_ : FrameBuf*'], ops: ['+ FrameHandle(FrameBuf*)', '+ ~FrameHandle() { free_frame }', '+ FrameHandle(FrameHandle&&)'] }
  - { id: ScopeGuard, x: 500, y: 20, w: 220, attrs: ['- fn_ : F', '- active_ : bool'], ops: ['+ ScopeGuard(F&& fn)', '+ ~ScopeGuard() { if(active_) fn_() }', '+ dismiss() noexcept'] }
rels:
  - { from: DemuxerPtr, to: FrameHandle, type: depend, label: '同一栈帧 LIFO 析构' }
  - { from: FrameHandle, to: ScopeGuard, type: depend, label: '配合事务回滚' }
```

```uml
label: 时序图：decodeFirstFrame 依次构造 dm、dec、fb 与 ScopeGuard；若解码失败提前 return，栈帧按 LIFO 逆序自动触发 ScopeGuard 回滚并释放全部句柄
participants:
  - { id: fn, name: 'decodeFirstFrame', x: 10, w: 125 }
  - { id: dm, name: 'dm (Demuxer)', x: 165, w: 105 }
  - { id: dec, name: 'dec (Decoder)', x: 295, w: 105 }
  - { id: fb, name: 'fb (FrameBuf)', x: 425, w: 105 }
  - { id: gd, name: 'ScopeGuard', x: 555, w: 100 }
messages:
  - { from: fn, to: dm, text: '构造: open_demuxer()', n: 1 }
  - { from: fn, to: dec, text: '构造: open_decoder()', n: 2 }
  - { from: fn, to: fb, text: '构造: alloc_frame()', n: 3 }
  - { from: fn, to: gd, text: '登记回滚 lambda', n: 4 }
  - { from: fn, to: gd, text: '提前 return -4 → ~ScopeGuard() 执行回滚', n: 5 }
  - { from: fn, to: fb, text: '~FrameHandle() → free_frame()', n: 6 }
  - { from: fn, to: dec, text: '~DecoderPtr() → close_decoder()', n: 7 }
  - { from: fn, to: dm, text: '~DemuxerPtr() → close_demuxer()', n: 8 }
```

下面的交互演示展示了栈帧中 `DemuxerPtr` → `DecoderPtr` → `FrameHandle` → `ScopeGuard` 在「正常返回 + `dismiss()`」「中途失败提前 `return`」「解码失败触发 `ScopeGuard` 回滚」三种路径下的构造与逆序析构过程：

<div class="dsviz" data-viz="dp-raii"></div>

## 3. Scope Guard：一次性清理与事务回滚

### 3.1 不值得单独写类的临时清理

RAII 包装类很适合反复复用的资源类型（文件、锁、解码器句柄），但业务代码里经常遇到**只在这一个函数里用一次**的善后逻辑：

- 临时把解码器日志级别从 `1` 调高到 `3`，离开函数时**无条件**恢复回 `1`；
- 切换编码器配置的事务：先改配置、生成临时校验文件，再跑编码测试——如果中途任何一步失败，要回滚旧配置并删临时文件；只有最后一步提交成功，才**取消**这次回滚。

为了这种一次性逻辑专门写一个 `LogLevelRestorer` 类或 `CodecRollback` 类太过笨重。**Scope Guard**（由 Andrei Alexandrescu 与 Petru Marginean 提出，C++ Library Fundamentals TS 里的 `std::experimental::scope_exit` / `scope_fail`）正是为此而生：**用一个泛型的栈对象，把任意清理 lambda 塞进析构函数里**，并提供 `dismiss()`（提交后解除守卫）。

### 3.2 通用 `ScopeGuard`（含 `dismiss()` 提交机制）

不到 20 行就能写出一个生产可用的 `ScopeGuard`：

```cpp
// 《RAII 与 Scope Guard》：通用 ScopeGuard（含 dismiss()）——事务回滚与临时状态恢复
#include <cstdio>
#include <string>
#include <utility>

template <typename F>
class ScopeGuard {
 public:
  explicit ScopeGuard(F&& fn) noexcept : fn_(std::forward<F>(fn)), active_(true) {}
  ~ScopeGuard() { if (active_) fn_(); }

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&& o) noexcept
      : fn_(std::move(o.fn_)), active_(std::exchange(o.active_, false)) {}

  void dismiss() noexcept { active_ = false; }

 private:
  F fn_;
  bool active_;
};

template <typename F>
ScopeGuard<std::decay_t<F>> makeScopeGuard(F&& fn) {
  return ScopeGuard<std::decay_t<F>>(std::forward<F>(fn));
}

struct PipelineConfig {
  std::string codec = "H264";
  int logLevel = 1;
  bool tempFileExists = false;
};

int switchCodecTransaction(PipelineConfig& cfg, const std::string& nextCodec, bool encodeOk) {
  // 1. 无条件恢复临时状态（永远不 dismiss）
  int savedLog = cfg.logLevel;
  cfg.logLevel = 3;
  auto restoreLog = makeScopeGuard([&] {
    cfg.logLevel = savedLog;
    std::printf("  [guard] 恢复 logLevel=%d\n", cfg.logLevel);
  });

  // 2. 事务性回滚：只有全部成功才 dismiss()
  std::string oldCodec = cfg.codec;
  cfg.codec = nextCodec;
  cfg.tempFileExists = true;
  auto rollback = makeScopeGuard([&] {
    cfg.codec = oldCodec;
    cfg.tempFileExists = false;
    std::printf("  [guard] 失败回滚：codec=%s, 删除临时文件\n", cfg.codec.c_str());
  });

  if (!encodeOk) {
    std::printf("  [work]  编码校验失败，提前 return -1\n");
    return -1;                       // 未 dismiss：先执行 rollback，再执行 restoreLog
  }

  cfg.tempFileExists = false;
  rollback.dismiss();                // 提交成功：取消回滚，仅保留 restoreLog
  std::printf("  [work]  切换成功，已 dismiss 回滚守卫\n");
  return 0;
}

int main() {
  PipelineConfig cfg;
  std::printf("== 事务 1：失败路径 ==\n");
  switchCodecTransaction(cfg, "AV1", false);
  std::printf("结果：codec=%s logLevel=%d tempFile=%d\n",
              cfg.codec.c_str(), cfg.logLevel, cfg.tempFileExists);

  std::printf("== 事务 2：成功路径 ==\n");
  switchCodecTransaction(cfg, "HEVC", true);
  std::printf("结果：codec=%s logLevel=%d tempFile=%d\n",
              cfg.codec.c_str(), cfg.logLevel, cfg.tempFileExists);
}
```

```text
== 事务 1：失败路径 ==
  [work]  编码校验失败，提前 return -1
  [guard] 失败回滚：codec=H264, 删除临时文件
  [guard] 恢复 logLevel=1
结果：codec=H264 logLevel=1 tempFile=0
== 事务 2：成功路径 ==
  [work]  切换成功，已 dismiss 回滚守卫
  [guard] 恢复 logLevel=1
结果：codec=HEVC logLevel=1 tempFile=0
```

注意两个细节：
- `ScopeGuard` 本身用模板参数 `F` 直接内联存储 lambda，**没有任何堆分配**（不像 `std::function<void()>` 可能触发类型擦除堆分配与间接调用）；
- **千万别漏写变量名**：`makeScopeGuard([&]{ ... });` 如果不赋给一个具名局部变量（如 `auto rollback = ...`），返回的临时 `ScopeGuard` 会在**当前语句分号处立刻析构**，清理代码还没干活就先执行了！C++17 可以给 `makeScopeGuard` 加上 `[[nodiscard]]` 让编译器直接报错拦截。

### 3.3 工程落地：Rule of 0、Rule of 5 与异常安全

在工程里写 RAII 类，记住三条铁律就能避开 95% 的坑：

| 原则 | 核心要求 | 适用场景 |
| --- | --- | --- |
| **Rule of 0（零法则，首选）** | 业务类不自己管裸资源，成员全用 `std::unique_ptr` / `std::vector` / `std::string` / RAII Handle，析构、拷贝、移动全部不手写 | 95% 的业务类（如 `DecodeSession` 直接组合 `DemuxerPtr` + `DecoderPtr`） |
| **Rule of 5（五法则）** | 只要手写了析构函数管理底层资源，就必须显式声明拷贝构造、拷贝赋值、移动构造、移动赋值（通常拷贝 `= delete`，移动 `noexcept`） | 底层薄包装类（`FrameHandle`、`FdHandle`、`GlTexture`） |
| **析构函数绝不抛异常** | C++11 起析构函数默认 `noexcept`；如果栈展开过程中析构函数再抛异常，程序会直接调用 `std::terminate()` 崩溃 | 所有 RAII 类的 `~T()` 与 `ScopeGuard` 清理回调 |

**单一职责原则在 RAII 里的体现**：一个 RAII 底层类**只管一个资源**。如果一个类同时持有 `Demuxer*` 和 `Decoder*` 两个裸指针，在构造函数里先打开 `Demuxer` 成功、再打开 `Decoder` 失败并抛异常时，C++ **不会**调用该类的析构函数（因为对象尚未构造完成），第一个 `Demuxer*` 就会泄漏！而如果把它们拆成 `DemuxerPtr` 和 `DecoderPtr` 两个独立成员，C++ 保证**已构造完成的成员会按逆序自动析构**。

在原生 C++ 异常环境下，RAII 与 Scope Guard 正好对应异常安全的三级保证：

- **基本保证（Basic Guarantee）**：失败后不泄漏资源、对象仍处于合法状态——把所有句柄放进 RAII 成员即可自动达成；
- **强保证（Strong Guarantee / 提交或回滚）**：失败后状态完全回退到调用前——由 `ScopeGuard` + `dismiss()` 或「先在临时副本上算完再 `noexcept` `swap`」达成；
- **不抛保证（Nothrow Guarantee）**：`noexcept` 析构函数、移动操作与 `dismiss()`，是前两级保证能够落地的底座。

## 4. 现代 C++ 写法与零开销实测

### 4.1 Rule of 0 组合与删除器体积陷阱

很多教程在示范用 `std::unique_ptr` 托管 C 句柄时，图省事会写成函数指针删除器 `std::unique_ptr<Demuxer, void (*)(Demuxer*)>`。这种写法有两个隐性代价：

1. **体积翻倍（`16 B` vs `8 B`）**：函数指针是一个运行时变量，即使每次传的都是 `&close_demuxer`，`unique_ptr` 也必须在对象里额外存一份 8 字节的指针；而空函数对象 `struct DemuxerDeleter` 没有成员变量，`std::unique_ptr` 借助**空基类优化（EBO / `[[no_unique_address]]`）**把它压缩成 0 字节，整个 `unique_ptr` 和裸指针一样大（`8 B`）。
2. **失去内联机会（间接调用）**：函数指针在析构时往往变成一次通过指针的间接调用（`call reg`），而空函数对象在编译期就锁死了调用目标，编译器可以直接内联释放代码。

一旦底层句柄都封装成了零开销 RAII 类型，上层业务类（如 `DecodeSession`）只需按值组合它们，**析构、移动、禁拷贝全部由编译器自动生成**——这就是现代 C++ 推崇的 **Rule of 0**：

```cpp
// 《RAII 与 Scope Guard》：Rule of 0 组合与删除器 sizeof 实测（EBO 零空间开销）
#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>

struct Demuxer { int id; };
struct Decoder { int id; };

void close_demuxer(Demuxer* p) { if (p) { std::printf("  [-] close Demuxer #%d\n", p->id); delete p; } }
void close_decoder(Decoder* p) { if (p) { std::printf("  [-] close Decoder #%d\n", p->id); delete p; } }

struct DemuxerDeleter { void operator()(Demuxer* p) const noexcept { close_demuxer(p); } };
struct DecoderDeleter { void operator()(Decoder* p) const noexcept { close_decoder(p); } };

using DemuxerPtr   = std::unique_ptr<Demuxer, DemuxerDeleter>;
using DecoderPtr   = std::unique_ptr<Decoder, DecoderDeleter>;
using DemuxerFnPtr = std::unique_ptr<Demuxer, void (*)(Demuxer*)>;

// Rule of 0：不写析构/拷贝/移动，编译器自动生成“只移动 + 成员逆序析构”
struct DecodeSession {
  DemuxerPtr dm;
  DecoderPtr dec;
};

int main() {
  std::printf("空删除器 unique_ptr 体积 = %zux 裸指针（EBO 零开销）\n",
              sizeof(DemuxerPtr) / sizeof(Demuxer*));
  std::printf("函数指针 unique_ptr 体积 = %zux 裸指针（体积翻倍）\n",
              sizeof(DemuxerFnPtr) / sizeof(Demuxer*));
  std::printf("DecodeSession 可拷贝=%d, 可移动=%d\n",
              std::is_copy_constructible_v<DecodeSession>,
              std::is_move_constructible_v<DecodeSession>);

  DecodeSession s1{DemuxerPtr(new Demuxer{1}), DecoderPtr(new Decoder{2})};
  DecodeSession s2 = std::move(s1);   // 零手写代码，所有权安全转移
  std::printf("移动后：s1.dm=%s, s2.dm=#%d\n", s1.dm ? "非空" : "空", s2.dm->id);
}                                     // 离开作用域：自动按 dec → dm 逆序释放
```

```text
空删除器 unique_ptr 体积 = 1x 裸指针（EBO 零开销）
函数指针 unique_ptr 体积 = 2x 裸指针（体积翻倍）
DecodeSession 可拷贝=0, 可移动=1
移动后：s1.dm=空, s2.dm=#1
  [-] close Decoder #2
  [-] close Demuxer #1
```

### 4.2 实测对比：裸指针 vs 手写 Handle vs `unique_ptr`

用 `projects/dp-lab/29-raii/bench.cpp` 实测 **5000 万次句柄获取→处理→释放循环**（静态对象池剥离 `malloc` 噪声，专注测量 RAII 抽象层本身开销；Apple clang 21，`-O2`，预热后 7 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 680 184" role="img" aria-label="5000万次句柄开关循环实测对比：裸指针 14.72 ms (8 B)、手写 RAII Handle 14.31 ms (8 B)、unique_ptr 空删除器 14.29 ms (8 B) 三者几乎持平；unique_ptr 函数指针删除器体积翻倍到 16 B，耗时升至 31.61 ms"><g style="font-family:var(--mono)"><text x="215" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">5000 万次开关循环耗时（Clang -O2，越短越好）</text><text x="205" y="45" text-anchor="end" style="fill:var(--fg);font-size:11px">裸指针手动释放 (8 B)</text><rect x="215" y="31" width="144" height="19" rx="2" style="fill:var(--border-2)"/><text x="366" y="45" text-anchor="start" style="fill:var(--fg);font-size:11px;font-weight:700">14.72 ms (0.29 ns/次)</text><text x="205" y="73" text-anchor="end" style="fill:var(--fg);font-size:11px">手写 RAII Handle (8 B)</text><rect x="215" y="59" width="140" height="19" rx="2" style="fill:var(--accent)"/><text x="362" y="73" text-anchor="start" style="fill:var(--accent);font-size:11px;font-weight:700">14.31 ms (0.29 ns/次)</text><text x="205" y="101" text-anchor="end" style="fill:var(--fg);font-size:11px">unique_ptr 空删除器 (8 B)</text><rect x="215" y="87" width="140" height="19" rx="2" style="fill:var(--accent)"/><text x="362" y="101" text-anchor="start" style="fill:var(--accent);font-size:11px;font-weight:700">14.29 ms (0.29 ns/次)</text><text x="205" y="129" text-anchor="end" style="fill:var(--fg);font-size:11px">unique_ptr 函数指针 (16 B)</text><rect x="215" y="115" width="310" height="19" rx="2" style="fill:var(--danger)"/><text x="532" y="129" text-anchor="start" style="fill:var(--danger);font-size:11px;font-weight:700">31.61 ms (2.2×)</text><text x="10" y="156" text-anchor="start" style="fill:var(--muted);font-size:10.5px">结论：空删除器 unique_ptr 与手写 RAII 的体积和耗时与裸指针完全等价（零开销抽象）</text><text x="10" y="173" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp：Apple clang 21，-O2，5000 万次循环，7 轮中位数；脚本见 projects/dp-lab/29-raii/</text></g></svg>
</figure>

| 方案 | `sizeof` | 5000 万次总耗时 | 单次耗时 | 适用场景 |
| --- | :---: | :---: | :---: | --- |
| **裸指针手动释放** | 8 B | 14.72 ms | 0.29 ns | 仅作性能基线，工程里不推荐 |
| **手写 RAII Handle（Rule of 5）** | **8 B** | **14.31 ms** | **0.29 ns** | 非指针句柄（`int fd`、`GLuint`）或需自定义成员方法 |
| **`unique_ptr<T, EmptyDeleter>`** | **8 B** | **14.29 ms** | **0.29 ns** | **托管 C 指针句柄首选**：零空间与时间开销、Rule of 0 |
| **`unique_ptr<T, void(*)(T*)>`** | 16 B | 31.61 ms | 0.63 ns | 仅当释放函数需在**运行时动态切换**时才用 |

实测结果清晰地印证了 C++「**零开销抽象（Zero-overhead Abstraction）**」的承诺：在 `-O2` 下，空删除器 `std::unique_ptr` 和手写 RAII Handle 编译出来的机器码与手写裸指针几乎完全一致——你换来了编译器强制的确定性释放和零泄漏保证，付出的运行时代价是 **0 字节、0 纳秒**。

## 5. RAII / Scope Guard 与相似机制

| 机制 | 代表语言 | 释放时机 | 核心特点与局限 |
| --- | --- | --- | --- |
| **RAII** | C++ / Rust | 离开作用域瞬间（**确定性 LIFO**） | 统一管理**所有资源**（内存、文件、锁、GPU 句柄），零运行时开销，靠类型系统防泄漏 |
| **Scope Guard** | C++（`ScopeGuard`） | 离开作用域瞬间（**确定性 LIFO**） | RAII 的轻量变体，无需为一次性清理或事务回滚新建类，支持 `dismiss()` 取消 |
| **`defer` / `try-finally` / `with`** | Go / Java / Python | 离开函数（`defer`）或代码块（`finally`/`with`） | Go `defer` 按函数而非块作用域延迟执行，且有轻微运行时登记开销；Java `try-with-resources` 需调用方显式包一层 `try` |
| **垃圾回收（GC）** | Java / Go / C# | GC 周期触发（**非确定性**） | **只管内存，不管非内存资源**（文件句柄、锁、显存仍须手动 `close()`/`finally`），且可能引入停顿 |

这也是为什么哪怕到了今天，C++ 程序员依然把 RAII 视为最不可替代的惯用法——它不是只解决内存泄漏，而是把**内存、锁、文件、网络连接、事务状态**全部统一进了同一套零开销的作用域机制。

## 6. 小结

- 手动配对释放资源在多条提前 `return` 与异常路径下必然防不胜防；**RAII** 把资源生命周期绑死在栈对象上，由编译器保证离开作用域时按**严格逆序（LIFO）**自动释放。
- 托管 C 风格指针句柄首选 **`std::unique_ptr<T, EmptyDeleter>`**（Rule of 0，体积 `8 B`、耗时 `0.29 ns/次`，与裸指针零开销等价）；避免无谓使用函数指针删除器（体积翻倍至 `16 B`，耗时 `2.2×`）。
- 封装非指针句柄（如 `int fd`）时手写只移动 Handle，严格遵守 **Rule of 5**（禁拷贝 + `noexcept` 移动），并用 `std::exchange` 把源对象置空。
- 面对一次性的临时状态恢复或事务回滚，用通用 **`ScopeGuard` + `dismiss()`**，把任意清理 lambda 纳入栈展开保护。

## 7. 练习

1. **辨析**：下面三个场景，分别应该选 `unique_ptr<T, EmptyDeleter>`、手写只移动 RAII Handle、还是 `ScopeGuard`？
   - (a) 封装 FFmpeg 的 `AVFormatContext*`（用 `avformat_open_input` 打开、`avformat_close_input` 关闭）；
   - (b) 封装 POSIX 文件描述符 `int fd`（无效值是 `-1` 而非 `nullptr`，需要 `read()`/`write()` 方法）；
   - (c) 在一次批量转码函数里，临时把全局 `g_hwAccelEnabled` 设为 `false`，函数无论从哪条路径退出都要恢复原值。
2. **找坑**：下面的 `BadFrameHandle` 想手写一个 RAII 类来自动释放 `FrameBuf*`，但它只写了析构函数，运行后却触发了 `double-free`。指出根本原因并写出符合 Rule of 5 的修复代码。

<!-- run: off -->
```cpp
// 《RAII 与 Scope Guard》练习 2（有 bug）：只写析构函数、未禁用拷贝导致 double-free
class BadFrameHandle {
 public:
  explicit BadFrameHandle(FrameBuf* p) : ptr_(p) {}
  ~BadFrameHandle() { free_frame_checked(ptr_); }
  // 坑：未声明 = delete 的拷贝构造/赋值，编译器自动生成浅拷贝！
 private:
  FrameBuf* ptr_;
};

int main() {
  FrameBuf* raw = alloc_frame(1);
  {
    BadFrameHandle a(raw);
    BadFrameHandle b = a;   // a.ptr_ 与 b.ptr_ 指向同一份资源！
  }                         // b 先析构释放 raw，a 再析构触发 double-free！
}
```

3. **设计/改写**：回头看第 3.2 节的 `switchCodecTransaction()`。如果我们在函数里把 `auto rollback = makeScopeGuard(...);` 错写成 `makeScopeGuard(...);`（忘了接返回值），程序行为会发生什么变化？如果再把两个守卫的定义顺序对调（先定义 `rollback`，再定义 `restoreLog`），失败路径下的执行顺序会变成什么？

<details>
<summary>点击查看答案</summary>

1. (a) **`unique_ptr<T, EmptyDeleter>`**：底层是标准指针句柄，写一个 3 行的空删除器结构体即可复用 `unique_ptr` 的全部只移动语义，零开销且遵循 Rule of 0；
   (b) **手写只移动 RAII Handle（Rule of 5）**：`int fd` 不是指针且哨兵值是 `-1`（`0` 是合法的 `stdin`），还要挂 `read()`/`write()` 等成员函数，手写类最清晰；
   (c) **`ScopeGuard`**：一次性的局部状态恢复，不必专门建一个类，一行 `auto restore = makeScopeGuard([&]{ g_hwAccelEnabled = old; });` 即可。

2. **违反 Rule of 3/5**：手写了析构函数释放 `ptr_`，却没有禁用拷贝构造和拷贝赋值，编译器会自动生成按成员逐字节浅拷贝的默认拷贝构造，`b = a` 后 `a` 和 `b` 同时拥有 `raw`，离开作用域时先后析构触发 `double-free`。
   **修复**：禁用拷贝（`BadFrameHandle(const BadFrameHandle&) = delete;` 与拷贝赋值 `= delete;`），并实现 `noexcept` 移动构造和移动赋值，用 `std::exchange(o.ptr_, nullptr)` 把被移动对象的 `ptr_` 置空。完整 bug 版与修复版见 `projects/dp-lab/29-raii/ex2_move_bug.cpp` 与 `ex2_move_fixed.cpp`。

3. ① 忘了接返回值时，`makeScopeGuard(...)` 返回的是**匿名临时对象**，它会在**当前语句末尾的分号处立刻析构**——也就是说回滚 lambda 还没等后面的业务逻辑执行就先跑掉了！给 `makeScopeGuard` 加上 `[[nodiscard]]` 可在编译期直接拦住这种手误。
   ② 若先定义 `rollback` 再定义 `restoreLog`，按 LIFO 逆序析构规则，失败路径会变成**先执行 `restoreLog`（把日志级别降回 1），再执行 `rollback`**；如果回滚过程本身需要高日志级别打印诊断信息，就会丢失日志——因此守卫的声明顺序必须与期望的清理顺序正好相反。

</details>
