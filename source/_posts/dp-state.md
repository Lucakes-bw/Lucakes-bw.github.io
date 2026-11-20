---
title: 状态模式
date: 2026-11-11 23:50:00
updated: 2026-11-11 23:50:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 状态模式
  - 状态机
  - std::variant
  - ASan
order: 6
description: 播放器有空闲、加载中、播放、暂停、结束、出错 6 个状态，每个函数开头都在判断 state_。加一个「缓冲中」状态，到处判断 state_ 的版本改了 4 处，编译器只提醒了其中 1 处，还漏了 2 处：把 7 个状态 × 9 个事件的 63 格逐格对照规格表，有 2 格不对（缓冲中按「播放」直接开始播放、缓冲中出错没有反应）。改成状态模式（每个状态一个类，由状态对象决定下一个状态，进入 / 退出动作管设备）后，同样加「缓冲中」，63 格全对。再实测几个问题：状态对象在自己的成员函数里把自己换掉，ASan 报 heap-use-after-free，不开 ASan 时 Clang 打印出 0.0 秒、GCC 打印出一个 200 多位的数；状态对象带成员又被共享，两个播放器互相篡改重试次数；enum + switch、共享状态对象、每次 new 状态对象、表驱动、std::variant 五种写法分发一个事件分别要 1.48～6.30 纳秒（Clang）和 0.39～6.46 纳秒（GCC）。最后比较状态和策略、命令的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-state]
---

> **本文目标**：理解状态模式——把每个状态的行为放进独立的状态类，由状态对象处理事件并决定下一个状态；会把到处判断 `state_` 的代码重构成状态模式，并用进入 / 退出动作管理资源；掌握状态对象的两个经典坑（带成员被共享、成员函数里换掉自己导致 use-after-free）；了解表驱动与现代 C++ `std::variant` + `std::visit` 状态机及其开销对比；分清状态与策略、命令的区别。
>
> **前置知识**：《策略模式》《享元模式》《命令模式》；音视频专栏里播放器的基本结构。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整规格测试与压测代码见 `projects/dp-lab/23-state/`。

## 1. 问题：到处判断状态

### 1.1 六个状态的播放器

一个播放器至少有 6 个状态：**空闲**、**加载中**、**播放**、**暂停**、**结束**、**出错**，需要响应打开、加载完、出错、播放、暂停、停止、播完 7 个事件。同一个事件在不同状态下效果截然不同（「播放」在暂停时是继续，在结束时是重播，在空闲时什么也不做），进出状态还要同步开关设备（文件、音频设备、转圈图标）。

最直接的写法是用 `enum class State` 记下当前状态，每个事件函数开头判断它：

```cpp
// 《状态模式》：问题版——到处判断 state_ 的播放器（6 个状态）
#include <cstdio>
#include <string>

enum class State { Idle, Loading, Playing, Paused, Ended, Error };
const char* kNames[] = {"空闲", "加载中", "播放", "暂停", "结束", "出错"};
struct Devices { bool file = false, audio = false, spinner = false; };

class Player {
 public:
  Devices dev;
  void open(const std::string& url) {
    if (state_ == State::Idle || state_ == State::Ended || state_ == State::Error) {
      url_ = url; dev.file = true; to(State::Loading);
    }
  }
  void onLoaded() {
    if (state_ != State::Loading) return;
    dev.audio = true; to(State::Playing);
  }
  void onFail() {
    if (state_ == State::Loading || state_ == State::Playing || state_ == State::Paused) {
      dev.audio = false; dev.file = false; to(State::Error);
    }
  }
  void play() {
    switch (state_) {
      case State::Idle: case State::Loading: case State::Playing: case State::Error: break;
      default:  // 暂停、结束：开始播放
        dev.audio = true; to(State::Playing);
    }
  }
  void pause() {
    if (state_ == State::Playing) { dev.audio = false; to(State::Paused); }
  }
  void stop() {
    switch (state_) {
      case State::Playing: dev.audio = false; [[fallthrough]];
      case State::Loading: case State::Paused: case State::Ended:
        dev.file = false; to(State::Idle); break;
      case State::Error: to(State::Idle); break;
      case State::Idle: break;
    }
  }
  void onEof() {
    if (state_ == State::Playing) { dev.audio = false; to(State::Ended); }
  }
 private:
  void to(State next) {
    std::printf("  %s → %s\n", kNames[int(state_)], kNames[int(next)]);
    state_ = next;
  }
  State state_ = State::Idle;
  std::string url_;
};

int main() {
  Player p;
  std::printf("演示：打开、播放、暂停、继续、播完、重播、停止\n");
  p.open("bbb-10s.mp4"); p.onLoaded(); p.pause(); p.play(); p.onEof(); p.play(); p.stop();
  std::printf("设备：文件=%d 音频=%d\n", p.dev.file, p.dev.audio);
}
```

```text
演示：打开、播放、暂停、继续、播完、重播、停止
  空闲 → 加载中
  加载中 → 播放
  播放 → 暂停
  暂停 → 播放
  播放 → 结束
  结束 → 播放
  播放 → 空闲
设备：文件=0 音频=0
```

注意几个函数的写法各不相同：`open()` / `onFail()` 用 `if` 列出有效状态，`play()` 用带 `default` 的 `switch`，`stop()` 用不带 `default` 的 `switch`。真实项目里由不同的人在不同时候写成，往往正是这样。

### 1.2 加一个「缓冲中」：漏改与非法转换

网络视频缓冲区读空时，要停下音频、显示转圈图标，等数据够了再继续。于是新增**缓冲中**状态和 `onStall()` / `onBuffered()` 两个事件，规格要求：缓冲中可以暂停、停止、出错，但按「播放」应无反应。

改代码时（完整程序见 `projects/dp-lab/23-state/switch_player_buffering.cpp`）：

<!-- run: off -->
```diff
-enum class State { Idle, Loading, Playing, Paused, Ended, Error };
+enum class State { Idle, Loading, Playing, Paused, Buffering, Ended, Error };
   void pause() {
     if (state_ == State::Playing) { dev.audio = false; to(State::Paused); }
+    else if (state_ == State::Buffering) { dev.spinner = false; to(State::Paused); }
   }
   void stop() {
     switch (state_) { ...
+      case State::Buffering: dev.spinner = false; dev.file = false; to(State::Idle); break;
   }
+  void onStall() { ... }      // 播放 → 缓冲中
+  void onBuffered() { ... }   // 缓冲中 → 播放
```

打开 `-Wall`，Clang 和 GCC 都只报了一处 `-Wswitch` 警告（`stop()`），因为只有它是不带 `default` 的 `switch`。用完整规格测试逐格检查 **7 个状态 × 9 个事件 = 63 格**，立刻暴露出 **2 处漏改造成的非法转换**：

- **缓冲中按「播放」**：落进了 `play()` 的 `default`，直接开始播放，音频设备开了但转圈图标还在转，缓冲区根本没数据；
- **缓冲中「出错」**：`onFail()` 的 `if` 里没加上 `Buffering`，断网后播放器永远卡在转圈。

<figure class="diagram">
<svg viewBox="0 0 656 258" role="img" aria-label="7 个状态乘 9 个事件的规格表；缓冲中那一行和卡顿、缓冲好两列是新增的；switch 版有两格出错：缓冲中按播放跳到了播放，缓冲中出错没有反应"><g style="font-family:var(--mono)"><text x="102.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">打开</text><text x="166.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">加载完</text><text x="230.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">出错</text><text x="294.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">播放</text><text x="358.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">暂停</text><text x="422.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">停止</text><text x="486.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">播完</text><text x="550.0" y="21.0" text-anchor="middle" style="fill:var(--link);font-size:11px;font-weight:700">卡顿</text><text x="614.0" y="21.0" text-anchor="middle" style="fill:var(--link);font-size:11px;font-weight:700">缓冲好</text><text x="62.0" y="47.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">空闲</text><rect x="70" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="47.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">加载中</text><rect x="134" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="262" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="326" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="454" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="30" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="30" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="73.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">加载中</text><rect x="70" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="134" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="73.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">播放</text><rect x="198" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="73.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">出错</text><rect x="262" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="326" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="73.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="56" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="56" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="99.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">播放</text><rect x="70" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="99.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="134" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="99.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">出错</text><rect x="262" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="99.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="326" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">暂停</text><rect x="390" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">结束</text><rect x="518" y="82" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">缓冲中</text><rect x="582" y="82" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="99.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="125.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">暂停</text><rect x="70" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="134" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="125.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">出错</text><rect x="262" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="125.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">播放</text><rect x="326" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="125.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="108" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="108" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="151.0" text-anchor="end" style="fill:var(--link);font-size:11px;font-weight:700">缓冲中</text><rect x="70" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="151.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="134" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="151.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="134" width="64" height="26" style="fill:var(--danger);fill-opacity:.18;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="151.0" text-anchor="middle" style="fill:var(--danger);font-size:11px;font-weight:700">✗ 不变</text><rect x="262" y="134" width="64" height="26" style="fill:var(--danger);fill-opacity:.18;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="151.0" text-anchor="middle" style="fill:var(--danger);font-size:11px;font-weight:700">✗ 播放</text><rect x="326" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="151.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">暂停</text><rect x="390" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="151.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="151.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="151.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="151.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">播放</text><text x="62.0" y="177.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">结束</text><rect x="70" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="177.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">加载中</text><rect x="134" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="262" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="177.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">播放</text><rect x="326" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="177.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="160" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="160" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="203.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">出错</text><rect x="70" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="203.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">加载中</text><rect x="134" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="262" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="326" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="203.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="186" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="186" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="10.0" y="232.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">每格是「这个状态下来了这个事件，下一个状态是什么」，· 表示不变。蓝底：加「缓冲中」新增的一行和两列（18 格）。</text><text x="10.0" y="248.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">红底：switch 版和规格不一致的两格。缓冲中按「播放」应该不变，却跳到了播放；缓冲中「出错」应该进入出错，却没反应。</text></g></svg>
</figure>

根源在于结构：**同一个状态的行为分散在所有的事件函数里**。加一个状态等于在规格表里加一行，却要钻进全部事件函数里逐个检查。

## 2. 状态模式

### 2.1 意图与设计

> **GoF**：允许一个对象在其内部状态改变时改变它的行为。对象看起来似乎修改了它的类。

大白话：**每个状态写成一个类**，同一个状态的所有行为收拢在一起。`Player` 只持有一个指向当前状态对象的指针，把事件原样转交给它；状态对象处理事件并**决定下一个状态是谁**。

四个关键设计：
- **默认空实现**：普通事件默认什么也不做，状态类只需重写它关心的事件；
- **必答事件设为纯虚**：`fail()` 和 `stop()` 每个状态都必须表态，漏写直接编译失败；
- **进入 / 退出动作**：`enter()` / `exit()` 由 `transitionTo()` 统一调度，把设备开关绑定到状态本身；
- **无成员状态共享**：数据全留在 `Player` 里，状态类无成员变量，用函数内 `static` 实例共享。

### 2.2 每个状态一个类

```cpp
// 《状态模式》：每个状态一个类，由状态对象决定下一个状态，enter/exit 统一管理设备
#include <cstdio>
#include <string>

struct Devices { bool file = false, audio = false, spinner = false; };
class Player;

class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void enter(Player&) {}
  virtual void exit(Player&) {}
  virtual void open(Player&, const std::string&) {}
  virtual void loaded(Player&) {}
  virtual void fail(Player&) = 0;  // 纯虚：每个状态必须显式表态
  virtual void play(Player&) {}
  virtual void pause(Player&) {}
  virtual void stop(Player&) = 0;
  virtual void eof(Player&) {}
};
State& idle(); State& loading(); State& playing(); State& paused(); State& ended(); State& error();

class Player {
 public:
  Devices dev;
  std::string url;
  void open(const std::string& u) { state_->open(*this, u); }
  void onLoaded() { state_->loaded(*this); }
  void onFail() { state_->fail(*this); }
  void play() { state_->play(*this); }
  void pause() { state_->pause(*this); }
  void stop() { state_->stop(*this); }
  void onEof() { state_->eof(*this); }
  void transitionTo(State& next) {  // 先退出旧状态，再进入新状态
    state_->exit(*this);
    std::printf("  %s → %s\n", state_->name(), next.name());
    state_ = &next;
    state_->enter(*this);
  }
 private:
  State* state_ = &idle();
};

struct Idle : State {
  const char* name() const override { return "空闲"; }
  void enter(Player& p) override { p.dev.file = false; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void stop(Player&) override {}
};
struct Loading : State {
  const char* name() const override { return "加载中"; }
  void enter(Player& p) override { p.dev.file = true; }
  void loaded(Player& p) override { p.transitionTo(playing()); }
  void fail(Player& p) override { p.transitionTo(error()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
};
struct Playing : State {
  const char* name() const override { return "播放"; }
  void enter(Player& p) override { p.dev.audio = true; }
  void exit(Player& p) override { p.dev.audio = false; }
  void fail(Player& p) override { p.transitionTo(error()); }
  void pause(Player& p) override { p.transitionTo(paused()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
  void eof(Player& p) override { p.transitionTo(ended()); }
};
struct Paused : State {
  const char* name() const override { return "暂停"; }
  void fail(Player& p) override { p.transitionTo(error()); }
  void play(Player& p) override { p.transitionTo(playing()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
};
struct Ended : State {
  const char* name() const override { return "结束"; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void play(Player& p) override { p.transitionTo(playing()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
};
struct Error : State {
  const char* name() const override { return "出错"; }
  void enter(Player& p) override { p.dev.file = false; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& idle() { static Idle s; return s; }
State& loading() { static Loading s; return s; }
State& playing() { static Playing s; return s; }
State& paused() { static Paused s; return s; }
State& ended() { static Ended s; return s; }
State& error() { static Error s; return s; }

int main() {
  Player p;
  std::printf("演示：打开、播放、暂停、继续、播完、重播、停止\n");
  p.open("bbb-10s.mp4"); p.onLoaded(); p.pause(); p.play(); p.onEof(); p.play(); p.stop();
  std::printf("设备：文件=%d 音频=%d\n", p.dev.file, p.dev.audio);
}
```

输出与第 1 节完全一致。

### 2.3 类图、时序图与状态图

```uml
label: 状态模式的类图：Player 持有一个 State 指针，把事件转交给它；State 是接口，有 enter、exit 和各个事件的虚函数，fail 和 stop 是纯虚函数；Idle、Playing、Paused、Buffering 等状态类实现它，状态类之间互相知道，Playing 会让 Player 换成 Paused
classes:
  - { id: Player, x: 20, y: 30, w: 230, attrs: ['+ dev : Devices', '- state_ : State*'], ops: ['+ play()', '+ pause()', '+ onFail()', '+ transitionTo(next : State&)'] }
  - { id: State, stereo: interface, x: 420, y: 20, w: 200, ops: ['+ enter(p) / exit(p)', '+ play(p)', '+ pause(p)', '*+ fail(p)', '*+ stop(p)'] }
  - { id: Idle, x: 300, y: 250, w: 80 }
  - { id: Playing, x: 400, y: 250, w: 90 }
  - { id: Paused, x: 510, y: 250, w: 90 }
  - { id: Buffering, x: 620, y: 250, w: 100 }
rels:
  - { from: Player, to: State, type: assoc, label: 'state_', tm: '1' }
  - { from: Idle, to: State, type: realize, mid: 215 }
  - { from: Playing, to: State, type: realize, mid: 215 }
  - { from: Paused, to: State, type: realize, mid: 215 }
  - { from: Buffering, to: State, type: realize, mid: 215 }
  - { from: Playing, to: Player, type: depend, label: 'transitionTo(paused())', points: [[445, 280], [445, 308], [135, 308], [135, 178]] }
notes:
  - { x: 520, y: 300, w: 200, text: ['图里只画了 4 个状态类，', '加载中、结束、出错同理'] }
```

```uml
label: 时序图：界面调用 Player 的 pause；Player 转交给当前状态 Playing 的 pause；Playing 调用 Player 的 transitionTo，传入 Paused；transitionTo 先调用 Playing 的 exit 关闭音频设备，再把 state_ 换成 Paused，最后调用 Paused 的 enter
participants:
  - { id: ui, name: '界面', x: 20, w: 70 }
  - { id: p, name: 'Player', x: 170, w: 90 }
  - { id: s, name: 'Playing', x: 360, w: 90 }
  - { id: z, name: 'Paused', x: 540, w: 90 }
messages:
  - { from: ui, to: p, text: 'pause()', n: 1 }
  - { from: p, to: s, text: 'pause(*this)', n: 2 }
  - { from: s, to: p, text: 'transitionTo(paused())', n: 3 }
  - { from: p, to: s, text: 'exit(*this)', n: 4, note: '关闭音频设备' }
  - { from: s, to: p, text: '', type: reply }
  - { from: p, to: p, text: 'state_ = &next', n: 5 }
  - { from: p, to: z, text: 'enter(*this)', n: 6, note: 'Paused 没有进入动作' }
  - { from: z, to: p, text: '', type: reply }
  - { from: p, to: s, text: '', type: reply }
  - { from: s, to: p, text: '', type: reply }
```

注意第 3 步：`transitionTo()` 返回后，控制流又回到 `Playing::pause()` 里。共享静态状态对象对此无碍；若状态对象是 `new` 出来且换状态时立刻 `delete` 的，回到 `Playing::pause()` 时 `this` 已经悬空（第 3.2 节）。

状态模式几乎可以直接从状态图翻译过来：圆角框是状态类，出边是重写的事件函数：

<figure class="diagram">
<svg viewBox="0 0 720 356" role="img" aria-label="播放器的状态图：空闲打开后进入加载中，加载完进入播放；播放和暂停、播放和缓冲中之间可以来回切换，缓冲中也能暂停；播放完进入结束，结束可以重播；加载中出错进入出错，出错可以重新打开"><g style="font-family:var(--mono)"><defs><marker id="st-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--link)"/></marker></defs><circle cx="12" cy="168" r="5" style="fill:var(--fg)"/><path d="M17,168 L28,168" style="stroke:var(--fg);stroke-width:1.3"/><path d="M126,168 L188,168" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="157.0" y="161.0" text-anchor="middle" style="fill:var(--link);font-size:11px">打开</text><path d="M286,168 L378,168" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="332.0" y="161.0" text-anchor="middle" style="fill:var(--link);font-size:11px">加载完</text><path d="M418,186 L418,260" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="412.0" y="228.0" text-anchor="end" style="fill:var(--link);font-size:11px">暂停</text><path d="M438,260 L438,188" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="444.0" y="228.0" text-anchor="start" style="fill:var(--link);font-size:11px">播放</text><path d="M418,150 L418,78" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="412.0" y="118.0" text-anchor="end" style="fill:var(--link);font-size:11px">卡顿</text><path d="M438,78 L438,148" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="444.0" y="118.0" text-anchor="start" style="fill:var(--link);font-size:11px">缓冲好</text><path d="M476,52 C556,52 556,286 478,286" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="548.0" y="238.0" text-anchor="start" style="fill:var(--link);font-size:11px">暂停</text><path d="M476,160 L588,160" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="532.0" y="154.0" text-anchor="middle" style="fill:var(--link);font-size:11px">播完</text><path d="M590,178 L478,178" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="532.0" y="194.0" text-anchor="middle" style="fill:var(--link);font-size:11px">重播</text><path d="M228,186 L228,260" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="222.0" y="228.0" text-anchor="end" style="fill:var(--link);font-size:11px">出错</text><path d="M248,260 L248,188" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="254.0" y="228.0" text-anchor="start" style="fill:var(--link);font-size:11px">打开</text><rect x="30" y="150" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="78.0" y="173.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">空闲</text><rect x="190" y="150" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="238.0" y="173.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">加载中</text><rect x="380" y="150" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="428.0" y="173.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">播放</text><rect x="380" y="40" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="428.0" y="63.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">缓冲中</text><rect x="380" y="262" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="428.0" y="285.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">暂停</text><rect x="590" y="150" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="638.0" y="173.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">结束</text><rect x="190" y="262" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="238.0" y="285.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">出错</text><text x="30.0" y="330.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">图上省略了两类边：除「空闲」外，任何状态按「停止」都回到空闲；播放、暂停、缓冲中「出错」也进入出错。</text><text x="30.0" y="346.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">另外「结束」按「打开」进入加载中。完整的 21 条边见 projects/dp-lab/23-state/table_player.cpp。</text></g></svg>
</figure>

点下面演示里的事件按钮，观察当前状态对象如何处理并触发退出 / 进入动作：

<div class="dsviz" data-viz="dp-state"></div>

### 2.4 再加「缓冲中」与进入 / 退出动作

在状态模式里加「缓冲中」（`projects/dp-lab/23-state/state_player_buffering.cpp`），只需在 `Playing` 加一行 `stall`，再新增一个 `Buffering` 类：

<!-- run: off -->
```cpp
struct Buffering : State {
  const char* name() const override { return "缓冲中"; }
  void enter(Player& p) override { p.dev.spinner = true; }
  void exit(Player& p) override { p.dev.spinner = false; }
  void fail(Player& p) override { p.transitionTo(error()); }
  void pause(Player& p) override { p.transitionTo(paused()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
  void buffered(Player& p) override { p.transitionTo(playing()); }
};
```

`Buffering` 没重写 `play()`，按播放天然无反应；`fail()` 是纯虚函数，逼着作者必须表态。63 格逐格测试 **0 格不一致**：

<figure class="diagram">
<svg viewBox="0 0 698 126" role="img" aria-label="加缓冲中状态的改动：到处判断状态的版本改了 4 处，漏改 2 处，63 格里 2 格不一致；状态模式改了 5 处，没有漏改，0 格不一致"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">diff 统计的改动处数</text><rect x="138.0" y="30.0" width="66.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="210.7" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">4 处</text><rect x="138.0" y="60.0" width="83.3" height="20.0" rx="2" style="fill:var(--accent)"/><text x="227.3" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5 处</text><text x="324.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">该改却漏改的地方</text><rect x="324.0" y="30.0" width="33.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="363.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2 处</text><rect x="324.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="331.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 处</text><text x="510.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">63 格里和规格不一致</text><rect x="510.0" y="30.0" width="33.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="549.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2 格</text><rect x="510.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="517.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 格</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">到处判断 state_</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">状态模式</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：只比较类的部分（不含 main 里的测试），Clang 和 GCC 结果相同；漏改和不一致由 main 逐格对照规格表得出</text></g></svg>
</figure>

**进入 / 退出动作**带来的收益同样显著：在 switch 版里 `dev.audio = false` 散落在 5 个函数里；状态模式里只写在 `Playing::exit()` 一处，不管从哪条边离开都会关闭（和 RAII 同理）。设计时注意区分三类动作：
- **属于状态**（如音频设备、转圈图标）：放进 `enter()` / `exit()`；
- **属于转换**（如从结束重播要跳回开头）：放在该边对应的事件函数（如 `Ended::play()`）；
- **内部转换**（如更新缓冲进度 40% → 100%）：直接改数据，**不要**走「缓冲中 → 缓冲中」自转换，否则会触发 `exit()` 再 `enter()` 让图标闪烁。

## 3. 深入状态模式

### 3.1 共享还是新建

- **无成员状态 → 共享（享元）**：所有数据放进 `Player`，每个状态一个静态对象，零堆分配、无生命周期问题。
- **有成员状态 → 每次新建**：`Player` 用 `std::unique_ptr<State>` 持有，状态数据随对象销毁自动清理，但每次转换一次 `new / delete`，且要防下一节的坑。

铁律：**带成员变量的状态对象绝对不能共享**，否则多个播放器会互相篡改计数（见练习 3）。

### 3.2 坑：在成员函数里换掉自己（use-after-free）

如果采用 `unique_ptr<State>` 每次新建状态，在 `Playing::pause(Player& p)` 里先调 `p.setState(make_unique<Paused>(pos_))`、下一行再打印 `pos_`，`setState` 会当场 `delete` 旧的 `Playing`，随后的 `this->pos_` 就是**释放后使用**（完整复现见 `projects/dp-lab/23-state/self_replace.cpp`）。实测 `-O2` 下竟然两边都不崩溃：

| | Clang / macOS | GCC / Ubuntu |
| --- | --- | --- |
| 不开 ASan 打印的暂停位置 | `0.0` 秒 | `5277529222…`（200 多位乱数）秒 |
| 开启 `-fsanitize=address` | `ERROR: AddressSanitizer: heap-use-after-free` | `ERROR: AddressSanitizer: heap-use-after-free` |

最稳妥的改法不是靠人记住「换状态放最后一行」，而是**让事件函数只返回下一个状态，由 `Player` 在函数返回之后再替换**：

```cpp
// 《状态模式》：安全改法——事件函数只返回下一个状态，由 Player 在调用返回后再替换
#include <cstdio>
#include <memory>

class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual std::unique_ptr<State> pause() { return nullptr; }  // 空指针：状态不变
};

class Paused : public State {
 public:
  explicit Paused(double pos) : pos_(pos) {}
  const char* name() const override { return "暂停"; }
  double pos() const { return pos_; }
 private:
  double pos_;
};

class Playing : public State {
 public:
  explicit Playing(double pos) : pos_(pos) {}
  const char* name() const override { return "播放"; }
  std::unique_ptr<State> pause() override {
    std::printf("  在 %.1f 秒处暂停\n", pos_);  // this 依然存活
    return std::make_unique<Paused>(pos_);
  }
 private:
  double pos_;
};

class Player {
 public:
  void pause() { if (auto next = state_->pause()) state_ = std::move(next); }  // 返回后再 delete 旧状态
  const State& state() const { return *state_; }
 private:
  std::unique_ptr<State> state_ = std::make_unique<Playing>(3.5);
};

int main() {
  Player p;
  p.pause();
  auto& z = static_cast<const Paused&>(p.state());
  std::printf("现在的状态：%s，停在 %.1f 秒\n", z.name(), z.pos());
}
```

```text
  在 3.5 秒处暂停
现在的状态：暂停，停在 3.5 秒
```

### 3.3 维度取舍、表驱动与真实工程

- **加事件比加状态难（表达式问题）**：`switch` 按「列」组织（加事件易、加状态难）；状态模式按「行」组织（加状态易、加事件难）。播放器事件稳定、状态常增，适合状态模式；协议如 Linux 内核 TCP 状态几十年不变、报文事件极多，内核源码 `tcp_rcv_state_process()` 用的正是 `switch`。
- **表驱动状态机**：把全部转换写成 `constexpr Transition kTransitions[]`（21 条边），编译期展开为 `7 × 9 = 63` 字节的二维数组 `kNext[state][event]`，运行时 O(1) 查表（完整实现见 `projects/dp-lab/23-state/table_player.cpp`）。优点是整张状态图集中在一处、可导出为 Graphviz、速度极快；缺点是带守卫条件（如「重试 3 次才失败」）或边动作时表会退化成一堆函数指针。
- **真实工程**：libVLC（`libvlc_state_t`）和 Android `MediaPlayer` 都是标准有限状态机；Node.js 的 `llhttp` 为极致吞吐从状态机描述生成巨型 `switch`；状态极多且有层次状态时可用 Boost.MSM 或 `boost-ext/sml`。

## 4. 现代 C++ 写法与性能

### 4.1 `std::variant` + `std::visit` 状态机

C++17 的 `std::variant` 让每个状态成为普通结构体、**各带各的数据**（`Loading` 带 `url`、`Buffering` 带进度、`Error` 带原因），换状态即赋值，旧数据自动析构，零堆分配也无 `use-after-free`：

```cpp
// 《状态模式》：std::variant + std::visit 状态机——各状态按值带独立数据，零堆分配
#include <cstdio>
#include <optional>
#include <string>
#include <variant>

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct Idle {}; struct Loading { std::string url; }; struct Playing {};
struct Paused {}; struct Buffering { int percent = 0; }; struct Ended {};
struct Error { std::string why; };
using State = std::variant<Idle, Loading, Playing, Paused, Buffering, Ended, Error>;

struct Open { std::string url; }; struct Loaded {}; struct Fail { std::string why; };
struct Play {}; struct Pause {}; struct Stop {}; struct Stall {};
struct Progress { int percent; }; struct Buffered {};
using Event = std::variant<Open, Loaded, Fail, Play, Pause, Stop, Stall, Progress, Buffered>;
using Next = std::optional<State>;

Next transition(const State& s, const Event& e) {
  return std::visit(overloaded{
      [](const Idle&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Ended&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Error&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Loading&, const Loaded&) -> Next { return Playing{}; },
      [](const Playing&, const Pause&) -> Next { return Paused{}; },
      [](const Playing&, const Stall&) -> Next { return Buffering{}; },
      [](const Paused&, const Play&) -> Next { return Playing{}; },
      [](const Ended&, const Play&) -> Next { return Playing{}; },
      [](const Buffering&, const Pause&) -> Next { return Paused{}; },
      [](const Buffering&, const Buffered&) -> Next { return Playing{}; },
      [](const Idle&, const Fail&) -> Next { return {}; },
      [](const Ended&, const Fail&) -> Next { return {}; },
      [](const Error&, const Fail&) -> Next { return {}; },
      [](const auto&, const Fail& f) -> Next { return Error{f.why}; },  // 其余状态出错进 Error
      [](const Idle&, const Stop&) -> Next { return {}; },
      [](const auto&, const Stop&) -> Next { return Idle{}; },          // 其余状态停止回 Idle
      [](const auto&, const auto&) -> Next { return {}; },
  }, s, e);
}

const char* stateName(const State& s) {
  static const char* k[] = {"空闲", "加载中", "播放", "暂停", "缓冲中", "结束", "出错"};
  return k[s.index()];
}

class Player {
 public:
  void fire(const Event& e) {
    if (auto* pr = std::get_if<Progress>(&e)) {  // 内部转换：直接更新进度，不触发 exit/enter
      if (auto* b = std::get_if<Buffering>(&state_)) {
        b->percent = pr->percent;
        std::printf("  缓冲进度 %d%%\n", b->percent);
      }
      return;
    }
    Next n = transition(state_, e);
    if (!n) return;
    std::printf("  %s → %s", stateName(state_), stateName(*n));
    state_ = std::move(*n);
    if (auto* x = std::get_if<Error>(&state_)) std::printf("（%s）", x->why.c_str());
    std::printf("\n");
  }
 private:
  State state_;
};

int main() {
  Player p;
  for (const Event& e : {Event{Open{"bbb-10s.mp4"}}, Event{Loaded{}}, Event{Stall{}},
                         Event{Progress{40}}, Event{Play{}}, Event{Progress{100}},
                         Event{Buffered{}}, Event{Stall{}}, Event{Fail{"网络断开"}},
                         Event{Open{"bbb-10s.mp4"}}})
    p.fire(e);
}
```

```text
  空闲 → 加载中
  加载中 → 播放
  播放 → 缓冲中
  缓冲进度 40%
  缓冲进度 100%
  缓冲中 → 播放
  播放 → 缓冲中
  缓冲中 → 出错（网络断开）
  出错 → 加载中
```

注意两点：一是 `(const auto&, const auto&)` 虽然省代码，但也像 `default` 一样会吞掉漏写组合；二是 `sizeof(State)` 取决于最大成员（本例因 `std::string`，libc++ 占 32 字节、libstdc++ 占 40 字节）。

### 4.2 五种写法的开销实测

用五种写法实现同一台 7 状态 × 9 事件状态机，分发 1966 万个伪随机事件（其中 424 万个触发转换，`-O2`，7 次取中位数；完整压测见 `projects/dp-lab/23-state/bench.cpp`）：

<figure class="diagram">
<svg viewBox="0 0 720 216" role="img" aria-label="五种写法分发一个事件的平均时间：Clang 上 switch 1.48、共享状态对象 3.33、每次 new 5.21、表驱动 1.91、variant 6.30 纳秒；GCC 上分别是 0.39、5.05、6.46、0.54、5.85 纳秒"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="178.0" y="30.0" width="38.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="222.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">1.48 ns</text><rect x="178.0" y="60.0" width="85.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="269.6" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">3.33 ns</text><rect x="178.0" y="90.0" width="134.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="318.0" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">5.21 ns</text><rect x="178.0" y="120.0" width="49.1" height="20.0" rx="2" style="fill:var(--link)"/><text x="233.1" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1.91 ns</text><rect x="178.0" y="150.0" width="162.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="346.0" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">6.3 ns</text><text x="448.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="448.0" y="30.0" width="10.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="464.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.39 ns</text><rect x="448.0" y="60.0" width="129.9" height="20.0" rx="2" style="fill:var(--accent)"/><text x="583.9" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.05 ns</text><rect x="448.0" y="90.0" width="166.1" height="20.0" rx="2" style="fill:var(--danger)"/><text x="620.1" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">6.46 ns</text><rect x="448.0" y="120.0" width="13.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="467.9" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.54 ns</text><rect x="448.0" y="150.0" width="150.4" height="20.0" rx="2" style="fill:var(--link)"/><text x="604.4" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">5.85 ns</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">enum + switch</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">状态模式（共享对象）</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">状态模式（每次 new）</text><text x="170.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">表驱动</text><text x="170.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">variant + visit</text><text x="10.0" y="208.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp，-O2：同一串伪随机事件（每次测量 1966 万个，其中 424 万个引起转换），7 次取中位数；GCC 跑在 colima 虚拟机里</text></g></svg>
</figure>

| 写法 | Clang (ns/事件) | GCC (ns/事件) | 当前状态占用 | 每次转换堆分配 |
| --- | ---: | ---: | --- | :---: |
| `enum + switch` | 1.48 | 0.39 | 1 字节 | 0 |
| 状态模式（共享对象） | 3.33 | 5.05 | 8 字节（指针） | 0 |
| 状态模式（每次 new） | 5.21 | 6.46 | 8 字节 + 堆 8 字节 | 1 次 |
| 表驱动 | 1.91 | 0.54 | 1 字节 + 63 字节表 | 0 |
| `variant + visit` | 6.30 | 5.85 | 8 字节（Clang）/ 2 字节（GCC） | 0 |

结论：`switch` 和表驱动最快；双 `variant` 的 `std::visit` 要查 7×9 函数指针表所以最慢，但也只需 6 纳秒。对播放器这类每秒几百次事件的状态机，纳秒级差异可以忽略，按可读性与扩展性选择；只有逐字节解析器才需要抠这几纳秒。

### 4.3 怎么选

| 场景 | 推荐写法 |
| --- | --- |
| 状态少（3～4 个）、几乎不扩展 | `enum + switch`（开 `-Wall`，不写 `default`） |
| 状态多、持续新增、各状态行为复杂 | 状态模式（无成员共享，数据放 Context） |
| 不同状态各带独立数据、想让编译器管生命周期 | `std::variant` + `std::visit` |
| 状态图要审查、导出或配置化 | 表驱动 |
| 每秒上亿次转换（词法 / 协议解析） | `switch` 或表驱动（甚至代码生成） |

## 5. 状态和相似的模式

| 模式 | 把什么做成对象 | 谁来切换 | 实现之间是否知道彼此 |
| --- | --- | --- | --- |
| **状态** | 某状态下的全部行为 | 状态对象自己（处理事件时） | **知道**：`Playing` 切到 `Paused` |
| **策略** | 做一件事的一种算法 | 外部调用者或 Context 按配置 | **不知道**，彼此平等、可互换 |
| **命令** | 一次要执行的请求 | 不切换（可排队、撤销） | 不知道 |
| **表驱动** | 不做成对象（纯数据表） | Context 查表 | 状态只是整数编号 |

一句话分清状态和策略：看 `setState` / `setStrategy` **由谁调用**——外部选算法是策略，对象内部处理事件时自己切是状态。状态和命令方向相反（命令封装事件，状态封装状态），两者常配合使用。

## 6. 小结

- 状态模式把同一状态的所有行为收拢进独立类，解决 `switch` 版加状态要改遍所有函数且极易漏改的痛点（63 格测试从 2 处非法转换降为 0）。
- 进入 / 退出动作（`enter` / `exit`）像 RAII 一样管理属于状态的资源，内部转换不要误写成自转换。
- 无成员状态对象可共享；有成员状态对象绝不能共享。采用 `unique_ptr` 每次新建时，让事件函数返回下一状态，避免在成员函数里 `delete this`。
- 状态模式按「行」组织（加状态易），`switch` 按「列」组织（加事件易）；`std::variant` 适合状态各带独立数据的场景，五种写法都在 0.4～6.5 ns/事件。

## 7. 练习

1. **辨析**：以下需求分别更适合状态、策略还是命令？
   - (a) 播放器支持按吞吐量、按缓冲区长度两种码率自适应算法，用户在设置里切换；
   - (b) 字幕下载任务：连接中、下载中、等待重试、完成、失败；
   - (c) 视频编辑器的剪切、粘贴、调色需要支持撤销；
   - (d) 播放/暂停按钮在播放时显示暂停图标、暂停时显示播放图标，点击效果随状态变化。
2. **设计**：用 `std::variant` 设计一个分片下载任务的状态机（连接中、下载中、等待重试、完成、失败）：网络出错后等待重试，最多试 3 次，等待时间翻倍（500 ms、1000 ms），重连后从断点继续。哪些数据应放进哪个状态？
3. **找问题**：下面两个播放器 A、B 先后进入缓冲中，超时重试最多 3 次。结果 B 第一次超时就打印「第 2 次重试」、第二次就报错，A 反而多试了一次。问题在哪？怎么改？

```cpp
// 《状态模式》练习 3：找问题——两个播放器同时缓冲，重试计数互相串台
#include <cstdio>

class Player;
struct State {
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void enter(Player&) {}
  virtual void timeout(Player&) {}
};
State& idle(); State& buffering(); State& error();

class Player {
 public:
  explicit Player(const char* n) : name(n) {}
  const char* name;
  void onTimeout() { state_->timeout(*this); }
  void startBuffering() { transitionTo(buffering()); }
  void transitionTo(State& next) {
    std::printf("  [%s] %s → %s\n", name, state_->name(), next.name());
    state_ = &next;
    state_->enter(*this);
  }
 private:
  State* state_ = &idle();
};

struct Idle : State { const char* name() const override { return "空闲"; } };
struct Error : State { const char* name() const override { return "出错"; } };
struct Buffering : State {
  const char* name() const override { return "缓冲中"; }
  void enter(Player&) override { retries_ = 0; }
  void timeout(Player& p) override {
    if (++retries_ >= 3) p.transitionTo(error());
    else std::printf("  [%s] 超时，第 %d 次重试\n", p.name, retries_);
  }
 private:
  int retries_ = 0;
};
State& idle() { static Idle s; return s; }
State& buffering() { static Buffering s; return s; }
State& error() { static Error s; return s; }

int main() {
  Player a("A"), b("B");
  a.startBuffering(); a.onTimeout(); a.onTimeout();
  b.startBuffering(); a.onTimeout(); b.onTimeout(); b.onTimeout();
}
```

<details>
<summary>点击查看答案</summary>

1. (a) **策略**（外部切换、平等互换）；(b) **状态**（当前状态决定下一状态）；(c) **命令**（封装操作以支持撤销）；(d) **状态**（由播放器状态决定行为，界面订阅状态更新图标即可，不要再写一遍 `if (playing)`）。

2. 数据按生命周期归属到对应状态：`Connecting{attempt, bytes}`、`Downloading{attempt, bytes}`、`Waiting{attempt, delayMs, bytes}`、`Done{bytes}`、`Failed{attempts}`。出错时的带条件转换抽成辅助函数 `retryOrFail(attempt, bytes)`，离开状态时不需要的数据自动销毁。完整实现见 `projects/dp-lab/23-state/ex3_download.cpp`（历史状态「跳转中」示例见 `ex2_seeking.cpp`）。

3. `Buffering` 是共享的静态对象，却带了成员变量 `retries_`：B 进入缓冲时 `enter()` 把共享计数清零，导致 A 多试一次，随后 A、B 互相递增同一个计数器。改法：把 `int retries = 0` 挪进 `Player`，`Buffering` 保持无状态（或改用 `unique_ptr` / `std::variant` 独占状态数据）。完整修复版见 `projects/dp-lab/23-state/ex4_fixed.cpp`。

</details>
