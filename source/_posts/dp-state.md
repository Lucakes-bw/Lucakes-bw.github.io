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

> **本文目标**：理解状态模式：对象的行为随内部状态变化时，把每个状态的行为放进一个状态类，由状态对象处理事件、决定下一个状态；会把到处判断 `state_` 的代码重构成状态模式；会用进入 / 退出动作管理资源；量出加一个状态要改几处、漏改会造成哪些非法转换；知道状态对象什么时候可以共享、在成员函数里把自己换掉为什么会出事；会写表驱动和 `std::variant` 的状态机，知道几种写法的开销和取舍；分清状态和策略、命令。
>
> **前置知识**：上一篇《迭代器模式》；《策略模式》（第 5 节从策略那边比较过状态，本文接着讲）、《享元模式》（状态对象可以共享）、《命令模式》；音视频专栏里播放器的结构（解封装、解码、音频输出）。
>
> **编译环境**：g++/clang++ -std=c++17，完整代码在 projects/dp-lab/23-state/

## 1. 问题：到处判断状态

### 1.1 六个状态的播放器

一个播放器至少有这几个状态：**空闲**（什么也没打开）、**加载中**（打开文件、解封装、找解码器）、**播放**、**暂停**、**结束**（播到了文件末尾）、**出错**。界面上的按钮和解码线程会给它发事件：打开、加载完、出错、播放、暂停、停止、播完。

同一个事件在不同状态下的效果完全不同：「播放」在暂停时是继续，在结束时是从头重播，在空闲时什么也不做。进出状态时还要管设备：进入播放要打开音频设备，离开播放要关掉；进入空闲、出错要关闭文件。

最直接的写法是用一个枚举记下当前状态，每个函数开头判断它。下面的 `Player` 就是这样写的，`Devices` 里的三个布尔变量代表文件、音频设备和缓冲时界面上的转圈图标。`main` 后半段是测试：把每个状态和每个事件的组合都试一遍，和一张**规格表**（这个状态下来了这个事件，下一个状态应该是什么）逐格对照，同时检查设备开关对不对。

```cpp
// 《状态模式》：到处判断 state_ 的播放器（6 个状态），main 逐格对照规格表检查
#include <cstdio>
#include <string>

enum class State { Idle, Loading, Playing, Paused, Ended, Error };
const char* kNames[] = {"空闲", "加载中", "播放", "暂停", "结束", "出错"};

struct Devices { bool file = false, audio = false, spinner = false; };

class Player {
 public:
  Devices dev;            // 文件、音频设备、转圈图标：进出状态时要打开 / 关闭
  bool verbose = false;

  void open(const std::string& url) {
    if (state_ == State::Idle || state_ == State::Ended || state_ == State::Error) {
      url_ = url;
      dev.file = true;
      to(State::Loading);
    }
  }
  void onLoaded() {
    if (state_ != State::Loading) return;
    dev.audio = true;
    to(State::Playing);
  }
  void onFail() {
    if (state_ == State::Loading || state_ == State::Playing || state_ == State::Paused) {
      dev.audio = false;
      dev.file = false;
      to(State::Error);
    }
  }
  void play() {
    switch (state_) {
      case State::Idle: case State::Loading: case State::Playing: case State::Error:
        break;                                  // 这几个状态下按「播放」没有反应
      default:                                  // 其余的（暂停、结束）：开始播放
        if (state_ == State::Ended) seek0();
        dev.audio = true;
        to(State::Playing);
    }
  }
  void pause() {
    if (state_ == State::Playing) {
      dev.audio = false;
      to(State::Paused);
    }
  }
  void stop() {
    switch (state_) {
      case State::Playing:
        dev.audio = false;
        [[fallthrough]];
      case State::Loading: case State::Paused: case State::Ended:
        dev.file = false;
        to(State::Idle);
        break;
      case State::Error:
        to(State::Idle);
        break;
      case State::Idle:
        break;
    }
  }
  void onEof() {
    if (state_ == State::Playing) {
      dev.audio = false;
      to(State::Ended);
    }
  }

  State state() const { return state_; }
  void force(State s) { state_ = s; }   // 测试用：直接换成某个状态，不执行进入动作

 private:
  void to(State next) {
    if (verbose) std::printf("  %s → %s\n", kNames[int(state_)], kNames[int(next)]);
    state_ = next;
  }
  void seek0() {}
  State state_ = State::Idle;
  std::string url_;
};

// ---- 以下是测试：6 个状态 × 7 个事件，逐格和规格表对照 ----
const char* kEvents[] = {"open", "loaded", "fail", "play", "pause", "stop", "eof"};
void fire(Player& p, int e) {
  switch (e) {
    case 0: p.open("bbb-10s.mp4"); break;
    case 1: p.onLoaded(); break;
    case 2: p.onFail(); break;
    case 3: p.play(); break;
    case 4: p.pause(); break;
    case 5: p.stop(); break;
    case 6: p.onEof(); break;
  }
}

int main() {
  Player p;
  p.verbose = true;
  std::printf("演示：打开、播放、暂停、继续、播完、重播、停止\n");
  p.open("bbb-10s.mp4"); p.onLoaded(); p.pause(); p.play(); p.onEof(); p.play(); p.stop();

  enum { I, L, P, Z, E, X, _ = -1 };   // 规格表：spec[状态][事件] = 下一个状态，_ 表示不变
  const int spec[6][7] = {
      // open loaded fail play pause stop eof
      {L, _, _, _, _, _, _},   // 空闲
      {_, P, X, _, _, I, _},   // 加载中
      {_, _, X, _, Z, I, E},   // 播放
      {_, _, X, P, _, I, _},   // 暂停
      {L, _, _, P, _, I, _},   // 结束
      {L, _, _, _, _, I, _},   // 出错
  };
  int bad = 0;
  for (int s = 0; s < 6; ++s)
    for (int e = 0; e < 7; ++e) {
      Player q;
      q.force(State(s));
      q.dev = {s != I && s != X, s == P, false};
      fire(q, e);
      int want = spec[s][e] == _ ? s : spec[s][e], got = int(q.state());
      bool devOk = q.dev.audio == (got == P) && q.dev.spinner == false &&
                   q.dev.file == (got != I && got != X);
      if (got != want || !devOk) {
        ++bad;
        std::printf("  ✗ %s + %s：得到 %s，应该是 %s%s\n", kNames[s], kEvents[e], kNames[got],
                    kNames[want], devOk ? "" : "（设备状态也不对）");
      }
    }
  std::printf("6 个状态 × 7 个事件 = 42 格，和规格不一致的：%d 格\n", bad);
}
```

输出：

```
演示：打开、播放、暂停、继续、播完、重播、停止
  空闲 → 加载中
  加载中 → 播放
  播放 → 暂停
  暂停 → 播放
  播放 → 结束
  结束 → 播放
  播放 → 空闲
6 个状态 × 7 个事件 = 42 格，和规格不一致的：0 格
```

现在它是对的。注意几个函数的写法各不相同：`open()` 和 `onFail()` 用 `if` 列出有效的状态，`onLoaded()` 反过来排除无效的状态，`play()` 用 `switch` 加 `default`，`stop()` 用不带 `default` 的 `switch`。真实项目里的代码就是这样，由不同的人在不同的时候写成。

### 1.2 加一个「缓冲中」

播放网络视频时，缓冲区会读空：这时要停下音频，显示转圈图标，等数据够了再继续。于是加一个**缓冲中**状态和两个事件：**卡顿**（`onStall()`，缓冲区读空了）和**缓冲好**（`onBuffered()`）。规格是：

- 播放时卡顿 → 缓冲中；缓冲中缓冲好 → 播放；
- 缓冲中可以暂停（→ 暂停）、停止（→ 空闲），出错也要进入出错；
- 缓冲中按「播放」没有反应（本来就要播，只是在等数据）。

改代码的人做了这些修改（完整的文件是 `switch_player_buffering.cpp`）：

<!-- run: off -->
```diff
-enum class State { Idle, Loading, Playing, Paused, Ended, Error };
+enum class State { Idle, Loading, Playing, Paused, Buffering, Ended, Error };
 
   void pause() {
     if (state_ == State::Playing) {
       dev.audio = false;
       to(State::Paused);
+    } else if (state_ == State::Buffering) {
+      dev.spinner = false;
+      to(State::Paused);
     }
   }
   void stop() {
     switch (state_) {
       ...
+      case State::Buffering:
+        dev.spinner = false;
+        dev.file = false;
+        to(State::Idle);
+        break;
 
+  void onStall() { ... }      // 新增：播放 → 缓冲中
+  void onBuffered() { ... }   // 新增：缓冲中 → 播放
```

`stop()` 那一处是编译器提醒的：只加枚举值、别的都不改时，`-Wall` 报了一个警告，Clang 和 GCC 都一样：

```
warning: enumeration value 'Buffering' not handled in switch [-Wswitch]
```

只有一个，因为只有 `stop()` 用的是不带 `default` 的 `switch`。`play()` 的 `switch` 有 `default`，其他函数用的是 `if`，编译器都看不出来它们也需要考虑新状态。

### 1.3 逐格对照规格表

跑一下新版本：

```
演示：打开、播放、卡顿、缓冲中按「播放」
  空闲 → 加载中
  加载中 → 播放
  播放 → 缓冲中
  缓冲中 → 播放
  音频设备 开，转圈图标 显示
  ✗ 缓冲中 + fail：得到 缓冲中，应该是 出错
  ✗ 缓冲中 + play：得到 播放，应该是 缓冲中（设备状态也不对）
7 个状态 × 9 个事件 = 63 格，和规格不一致的：2 格
```

两个漏改，都是**非法的状态转换**：

- **缓冲中按「播放」**：`play()` 的 `default` 本来是给暂停和结束写的，新加的缓冲中也掉了进去，于是直接进入播放：音频设备打开了，转圈图标却还在转，缓冲区里根本没有数据。
- **缓冲中出错**：`onFail()` 只列出了加载中、播放、暂停三个状态。网络恰恰最容易在缓冲时断开，这时播放器什么也不做，一直停在缓冲中，转圈图标永远转下去。

把规格表画出来，就是下面这张 7 × 9 的表。加一个状态等于加一行，加两个事件等于加两列；每个事件函数负责一列。新加的那一行分散在 9 个函数里，改代码的人得逐个函数检查「缓冲中来了这个事件该怎么办」，漏掉哪个，编译器多半不会提醒。

<figure class="diagram">
<svg viewBox="0 0 656 258" role="img" aria-label="7 个状态乘 9 个事件的规格表；缓冲中那一行和卡顿、缓冲好两列是新增的；switch 版有两格出错：缓冲中按播放跳到了播放，缓冲中出错没有反应"><g style="font-family:var(--mono)"><text x="102.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">打开</text><text x="166.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">加载完</text><text x="230.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">出错</text><text x="294.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">播放</text><text x="358.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">暂停</text><text x="422.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">停止</text><text x="486.0" y="21.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">播完</text><text x="550.0" y="21.0" text-anchor="middle" style="fill:var(--link);font-size:11px;font-weight:700">卡顿</text><text x="614.0" y="21.0" text-anchor="middle" style="fill:var(--link);font-size:11px;font-weight:700">缓冲好</text><text x="62.0" y="47.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">空闲</text><rect x="70" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="47.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">加载中</text><rect x="134" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="262" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="326" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="454" y="30" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="30" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="30" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="47.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="73.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">加载中</text><rect x="70" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="134" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="73.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">播放</text><rect x="198" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="73.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">出错</text><rect x="262" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="326" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="73.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="56" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="56" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="56" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="73.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="99.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">播放</text><rect x="70" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="99.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="134" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="99.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">出错</text><rect x="262" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="99.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="326" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">暂停</text><rect x="390" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="82" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">结束</text><rect x="518" y="82" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="99.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">缓冲中</text><rect x="582" y="82" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="99.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="125.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">暂停</text><rect x="70" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="134" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="125.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">出错</text><rect x="262" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="125.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">播放</text><rect x="326" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="125.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="108" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="108" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="108" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="125.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="151.0" text-anchor="end" style="fill:var(--link);font-size:11px;font-weight:700">缓冲中</text><rect x="70" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="151.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="134" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="151.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="134" width="64" height="26" style="fill:var(--danger);fill-opacity:.18;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="151.0" text-anchor="middle" style="fill:var(--danger);font-size:11px;font-weight:700">✗ 不变</text><rect x="262" y="134" width="64" height="26" style="fill:var(--danger);fill-opacity:.18;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="151.0" text-anchor="middle" style="fill:var(--danger);font-size:11px;font-weight:700">✗ 播放</text><rect x="326" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="151.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">暂停</text><rect x="390" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="151.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="151.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="151.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="134" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="151.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">播放</text><text x="62.0" y="177.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">结束</text><rect x="70" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="177.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">加载中</text><rect x="134" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="262" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="177.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">播放</text><rect x="326" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="177.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="160" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="160" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="160" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="177.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="62.0" y="203.0" text-anchor="end" style="fill:var(--fg);font-size:11px;font-weight:700">出错</text><rect x="70" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="102.0" y="203.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">加载中</text><rect x="134" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="166.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="198" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="230.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="262" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="294.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="326" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="358.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="390" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="422.0" y="203.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">空闲</text><rect x="454" y="186" width="64" height="26" style="fill:none;stroke:var(--muted);stroke-width:.6"/><text x="486.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="518" y="186" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="550.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><rect x="582" y="186" width="64" height="26" style="fill:var(--link);fill-opacity:.10;stroke:var(--muted);stroke-width:.6"/><text x="614.0" y="203.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">·</text><text x="10.0" y="232.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">每格是「这个状态下来了这个事件，下一个状态是什么」，· 表示不变。蓝底：加「缓冲中」新增的一行和两列（18 格）。</text><text x="10.0" y="248.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">红底：switch 版和规格不一致的两格。缓冲中按「播放」应该不变，却跳到了播放；缓冲中「出错」应该进入出错，却没反应。</text></g></svg>
</figure>

问题不在于写代码的人粗心，而在于结构：**同一个状态的行为分散在所有的事件函数里**。状态越多、事件越多，每个函数里的判断越长，加一个状态要检查的地方也越多。

## 2. 状态模式

### 2.1 意图

GoF 的定义：

> 允许一个对象在其内部状态改变时改变它的行为。对象看起来似乎修改了它的类。

大白话：**每个状态写成一个类**，同一个状态的所有行为放在一起。`Player` 不再判断自己处于什么状态，只持有一个指向当前状态对象的指针，把每个事件原样转交给它。状态对象处理事件，并且**决定下一个状态是谁**，让 `Player` 换上那个状态对象。从外面看，同一个 `Player` 对象在不同时候的行为不同，就像换了一个类。

### 2.2 每个状态一个类

<!-- run: off -->
```cpp
class State {
 public:
  virtual void pause(Player&) {}   // 每个事件一个虚函数，默认什么也不做
  ...
};
class Playing : public State {
 public:
  void pause(Player& p) override { p.transitionTo(paused()); }   // 播放时暂停 → 暂停
};
void Player::pause() { state_->pause(*this); }                   // Player 不判断，直接转交
```

几个设计决定：

- **默认什么也不做**：一个状态只需要重写它关心的事件。规格表里大部分格子是「不变」，这些格子一行代码都不用写。
- **`fail()` 和 `stop()` 是纯虚函数**：这两个事件每个状态都得表态（第 1.3 节漏改的就是 `fail`），写成纯虚函数后，新加的状态类不写它们就编译不过。
- **进入动作和退出动作**：`enter()` 和 `exit()` 也是虚函数，由 `transitionTo()` 在换状态时调用。音频设备「进入播放时打开、离开播放时关闭」，只写在 `Playing` 一个类里（第 2.6 节细说）。
- **状态对象没有成员变量**：所有数据（文件名、设备）都在 `Player` 里，状态对象只有行为，所以每个状态只需要一个对象，所有播放器共用（第 3.2 节细说）。`idle()`、`playing()` 这些函数返回各自的静态对象；先在前面声明它们，`Playing` 才能引用后面才定义的 `Paused`。

完整的代码，`main` 里的测试和 switch 版一样：

```cpp
// 《状态模式》：每个状态一个类，由状态对象决定下一个状态（6 个状态），main 逐格对照规格表检查
#include <cstdio>
#include <string>

struct Devices { bool file = false, audio = false, spinner = false; };
class Player;

class State {   // 每个事件一个虚函数：默认什么也不做；fail 和 stop 每个状态都必须自己写
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void enter(Player&) {}   // 进入动作
  virtual void exit(Player&) {}    // 退出动作
  virtual void open(Player&, const std::string&) {}
  virtual void loaded(Player&) {}
  virtual void fail(Player&) = 0;
  virtual void play(Player&) {}
  virtual void pause(Player&) {}
  virtual void stop(Player&) = 0;
  virtual void eof(Player&) {}
};
// 状态对象没有成员变量，所有播放器共用一份
State& idle(); State& loading(); State& playing(); State& paused(); State& ended(); State& error();

class Player {
 public:
  Devices dev;
  bool verbose = false;
  std::string url;

  void open(const std::string& u) { state_->open(*this, u); }
  void onLoaded() { state_->loaded(*this); }
  void onFail() { state_->fail(*this); }
  void play() { state_->play(*this); }
  void pause() { state_->pause(*this); }
  void stop() { state_->stop(*this); }
  void onEof() { state_->eof(*this); }

  void transitionTo(State& next) {   // 换状态：先退出旧的，再进入新的
    state_->exit(*this);
    if (verbose) std::printf("  %s → %s\n", state_->name(), next.name());
    state_ = &next;
    state_->enter(*this);
  }
  const State& state() const { return *state_; }
  void force(State& s) { state_ = &s; }   // 测试用：直接换成某个状态，不执行进入动作

 private:
  State* state_ = &idle();
};

class Idle : public State {
 public:
  const char* name() const override { return "空闲"; }
  void enter(Player& p) override { p.dev.file = false; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void stop(Player&) override {}
};
State& idle() { static Idle s; return s; }

class Loading : public State {
 public:
  const char* name() const override { return "加载中"; }
  void enter(Player& p) override { p.dev.file = true; }
  void loaded(Player& p) override { p.transitionTo(playing()); }
  void fail(Player& p) override { p.transitionTo(error()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& loading() { static Loading s; return s; }

class Playing : public State {
 public:
  const char* name() const override { return "播放"; }
  void enter(Player& p) override { p.dev.audio = true; }
  void exit(Player& p) override { p.dev.audio = false; }
  void fail(Player& p) override { p.transitionTo(error()); }
  void pause(Player& p) override { p.transitionTo(paused()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
  void eof(Player& p) override { p.transitionTo(ended()); }
};
State& playing() { static Playing s; return s; }

class Paused : public State {
 public:
  const char* name() const override { return "暂停"; }
  void fail(Player& p) override { p.transitionTo(error()); }
  void play(Player& p) override { p.transitionTo(playing()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& paused() { static Paused s; return s; }

class Ended : public State {
 public:
  const char* name() const override { return "结束"; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void play(Player& p) override { p.transitionTo(playing()); }   // 从头重播
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& ended() { static Ended s; return s; }

class Error : public State {
 public:
  const char* name() const override { return "出错"; }
  void enter(Player& p) override { p.dev.file = false; }
  void open(Player& p, const std::string& u) override { p.url = u; p.transitionTo(loading()); }
  void fail(Player&) override {}
  void stop(Player& p) override { p.transitionTo(idle()); }
};
State& error() { static Error s; return s; }

// ---- 以下是测试：6 个状态 × 7 个事件，逐格和规格表对照 ----
const char* kEvents[] = {"open", "loaded", "fail", "play", "pause", "stop", "eof"};
void fire(Player& p, int e) {
  switch (e) {
    case 0: p.open("bbb-10s.mp4"); break;
    case 1: p.onLoaded(); break;
    case 2: p.onFail(); break;
    case 3: p.play(); break;
    case 4: p.pause(); break;
    case 5: p.stop(); break;
    case 6: p.onEof(); break;
  }
}

int main() {
  Player p;
  p.verbose = true;
  std::printf("演示：打开、播放、暂停、继续、播完、重播、停止\n");
  p.open("bbb-10s.mp4"); p.onLoaded(); p.pause(); p.play(); p.onEof(); p.play(); p.stop();

  State* states[] = {&idle(), &loading(), &playing(), &paused(), &ended(), &error()};
  enum { I, L, P, Z, E, X, _ = -1 };   // 规格表：spec[状态][事件] = 下一个状态，_ 表示不变
  const int spec[6][7] = {
      // open loaded fail play pause stop eof
      {L, _, _, _, _, _, _},   // 空闲
      {_, P, X, _, _, I, _},   // 加载中
      {_, _, X, _, Z, I, E},   // 播放
      {_, _, X, P, _, I, _},   // 暂停
      {L, _, _, P, _, I, _},   // 结束
      {L, _, _, _, _, I, _},   // 出错
  };
  int bad = 0;
  for (int s = 0; s < 6; ++s)
    for (int e = 0; e < 7; ++e) {
      Player q;
      q.force(*states[s]);
      q.dev = {s != I && s != X, s == P, false};
      fire(q, e);
      int want = spec[s][e] == _ ? s : spec[s][e], got = 0;
      while (states[got] != &q.state()) ++got;
      bool devOk = q.dev.audio == (got == P) && q.dev.spinner == false &&
                   q.dev.file == (got != I && got != X);
      if (got != want || !devOk) {
        ++bad;
        std::printf("  ✗ %s + %s：得到 %s，应该是 %s%s\n", states[s]->name(), kEvents[e],
                    states[got]->name(), states[want]->name(), devOk ? "" : "（设备状态也不对）");
      }
    }
  std::printf("6 个状态 × 7 个事件 = 42 格，和规格不一致的：%d 格\n", bad);
}
```

输出和 switch 版完全一样，42 格全对。

### 2.3 类图和时序图

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

`Playing` 指向 `Player` 的虚线是这个模式的关键：**状态类知道 `Player`，也知道别的状态**。这和策略不一样（第 5 节）。

在播放中按「暂停」，调用顺序是这样的：

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

注意第 3 步：调用 `transitionTo()` 的时候，`Playing::pause()` 还没有返回。`transitionTo()` 返回以后，控制又回到 `Playing::pause()` 里，这时 `Player` 的当前状态已经是 `Paused` 了。状态对象是共享的静态对象，所以这没问题；如果状态对象是 `new` 出来、换状态时就 `delete` 的，回到 `Playing::pause()` 时 `this` 已经没了（第 3.3 节）。

### 2.4 状态图和演示

状态模式的代码几乎可以直接从**状态图**翻译过来：每个圆角框是一个状态类，每条从框里出去的边是那个类里重写的一个事件函数，边上的字是事件名。

<figure class="diagram">
<svg viewBox="0 0 720 356" role="img" aria-label="播放器的状态图：空闲打开后进入加载中，加载完进入播放；播放和暂停、播放和缓冲中之间可以来回切换，缓冲中也能暂停；播放完进入结束，结束可以重播；加载中出错进入出错，出错可以重新打开"><g style="font-family:var(--mono)"><defs><marker id="st-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--link)"/></marker></defs><circle cx="12" cy="168" r="5" style="fill:var(--fg)"/><path d="M17,168 L28,168" style="stroke:var(--fg);stroke-width:1.3"/><path d="M126,168 L188,168" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="157.0" y="161.0" text-anchor="middle" style="fill:var(--link);font-size:11px">打开</text><path d="M286,168 L378,168" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="332.0" y="161.0" text-anchor="middle" style="fill:var(--link);font-size:11px">加载完</text><path d="M418,186 L418,260" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="412.0" y="228.0" text-anchor="end" style="fill:var(--link);font-size:11px">暂停</text><path d="M438,260 L438,188" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="444.0" y="228.0" text-anchor="start" style="fill:var(--link);font-size:11px">播放</text><path d="M418,150 L418,78" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="412.0" y="118.0" text-anchor="end" style="fill:var(--link);font-size:11px">卡顿</text><path d="M438,78 L438,148" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="444.0" y="118.0" text-anchor="start" style="fill:var(--link);font-size:11px">缓冲好</text><path d="M476,52 C556,52 556,286 478,286" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="548.0" y="238.0" text-anchor="start" style="fill:var(--link);font-size:11px">暂停</text><path d="M476,160 L588,160" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="532.0" y="154.0" text-anchor="middle" style="fill:var(--link);font-size:11px">播完</text><path d="M590,178 L478,178" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="532.0" y="194.0" text-anchor="middle" style="fill:var(--link);font-size:11px">重播</text><path d="M228,186 L228,260" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="222.0" y="228.0" text-anchor="end" style="fill:var(--link);font-size:11px">出错</text><path d="M248,260 L248,188" style="fill:none;stroke:var(--link);stroke-width:1.4" marker-end="url(#st-arr)"/><text x="254.0" y="228.0" text-anchor="start" style="fill:var(--link);font-size:11px">打开</text><rect x="30" y="150" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="78.0" y="173.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">空闲</text><rect x="190" y="150" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="238.0" y="173.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">加载中</text><rect x="380" y="150" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="428.0" y="173.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">播放</text><rect x="380" y="40" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="428.0" y="63.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">缓冲中</text><rect x="380" y="262" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="428.0" y="285.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">暂停</text><rect x="590" y="150" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="638.0" y="173.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">结束</text><rect x="190" y="262" width="96" height="36" rx="10" style="fill:var(--bg);stroke:var(--fg);stroke-width:1.3"/><text x="238.0" y="285.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">出错</text><text x="30.0" y="330.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">图上省略了两类边：除「空闲」外，任何状态按「停止」都回到空闲；播放、暂停、缓冲中「出错」也进入出错。</text><text x="30.0" y="346.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">另外「结束」按「打开」进入加载中。完整的 21 条边见第 3.5 节的转换表。</text></g></svg>
</figure>

下面的演示用的就是这张图。点事件按钮，看当前状态对象怎么处理它：有的事件让它换状态，换的时候先执行旧状态的退出动作，再执行新状态的进入动作；有的事件它没有重写，用的是基类的默认版本，什么也不做。可以试试在不同状态下点同一个按钮，比如「播放」或「暂停」。

<div class="dsviz" data-viz="dp-state"></div>

### 2.5 再加「缓冲中」

同样的需求，状态模式版要做的是：基类加两个事件、`Player` 加两个转发函数、`Playing` 加一行（播放时卡顿进入缓冲中），再新写一个 `Buffering` 类：

<!-- run: off -->
```cpp
class Buffering : public State {
 public:
  const char* name() const override { return "缓冲中"; }
  void enter(Player& p) override { p.dev.spinner = true; }
  void exit(Player& p) override { p.dev.spinner = false; }
  void fail(Player& p) override { p.transitionTo(error()); }
  void pause(Player& p) override { p.transitionTo(paused()); }
  void stop(Player& p) override { p.transitionTo(idle()); }
  void buffered(Player& p) override { p.transitionTo(playing()); }
};
```

完整的文件是 `state_player_buffering.cpp`，测试结果：

```
演示：打开、播放、卡顿、缓冲中按「播放」
  空闲 → 加载中
  加载中 → 播放
  播放 → 缓冲中
  音频设备 关，转圈图标 显示
7 个状态 × 9 个事件 = 63 格，和规格不一致的：0 格
```

缓冲中按「播放」没有反应：`Buffering` 没有重写 `play()`，基类的默认版本什么也不做，这正是规格要的。出错也对了：`fail()` 是纯虚函数，不写 `Buffering` 就编译不过，写的时候自然会想「缓冲中出错该去哪」。

`measure.sh` 用 diff 统计了两个版本的改动（只比较类的部分，不含 `main` 里的测试，Clang 和 GCC 上结果相同）：

| | 到处判断 state_ | 状态模式 |
| --- | --- | --- |
| diff 统计的改动处数 | 4 | 5 |
| 删除 / 改写的旧代码行 | 2 | 1（前置声明那一行） |
| 新增的行 | 24 | 17 |
| 要逐个检查的已有代码 | 全部 7 个事件函数 | 只有 `Playing`（只有它会转到缓冲中） |
| 该改却漏改的地方 | 2 处（`play()`、`onFail()`） | 0 |
| 63 格里和规格不一致的 | 2 | 0 |

<figure class="diagram">
<svg viewBox="0 0 698 126" role="img" aria-label="加缓冲中状态的改动：到处判断状态的版本改了 4 处，漏改 2 处，63 格里 2 格不一致；状态模式改了 5 处，没有漏改，0 格不一致"><g style="font-family:var(--mono)"><text x="138.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">diff 统计的改动处数</text><rect x="138.0" y="30.0" width="66.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="210.7" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">4 处</text><rect x="138.0" y="60.0" width="83.3" height="20.0" rx="2" style="fill:var(--accent)"/><text x="227.3" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5 处</text><text x="324.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">该改却漏改的地方</text><rect x="324.0" y="30.0" width="33.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="363.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2 处</text><rect x="324.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="331.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 处</text><text x="510.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">63 格里和规格不一致</text><rect x="510.0" y="30.0" width="33.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="549.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2 格</text><rect x="510.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="517.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 格</text><text x="130.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">到处判断 state_</text><text x="130.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">状态模式</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：只比较类的部分（不含 main 里的测试），Clang 和 GCC 结果相同；漏改和不一致由 main 逐格对照规格表得出</text></g></svg>
</figure>

改动的处数差不多，状态模式甚至多一处。区别在**改动落在哪里**：switch 版要钻进已有的函数里，在一串判断中间插一个分支，要改对其实需要 6 处，漏了 2 处也能编译通过；状态模式版的改动几乎都是新增，缓冲中的全部行为都在 `Buffering` 一个类里，从上往下读一遍就能对着规格表的那一行核对。

### 2.6 进入动作和退出动作

再看一眼 switch 版：`dev.audio = false` 出现在 `onFail()`、`pause()`、`stop()`、`onEof()`、`onStall()` 五个地方，每条离开播放的路都得记得关音频设备；`dev.spinner = false` 在三个地方。状态模式版里，`Playing::exit()` 和 `Buffering::exit()` 各写一次，不管从哪条边离开都会执行。

这就是**进入动作**和**退出动作**（entry / exit action）：和「处于某个状态」绑定的资源，在进入时获取，在离开时释放。和 RAII 的思路一样：构造时获取，析构时释放，不管从哪条路离开作用域。设计状态时可以问一句：这个资源是属于**状态**的，还是属于**某次转换**的？

- 属于状态：播放状态下音频设备开着，缓冲状态下转圈图标显示着。放进 `enter()` / `exit()`。
- 属于转换：从结束重播要跳回开头，从暂停继续播放不用。放在那条边对应的事件函数里，比如 `Ended::play()`。

还有一种情况要分清：缓冲进度从 40% 变成 100%，状态还是缓冲中。如果写成「缓冲中 → 缓冲中」的**自转换**，会先执行退出动作（隐藏转圈）再执行进入动作（显示转圈），图标闪一下；这种只改数据、不出不进的叫**内部转换**，应该直接改数据，不调 `transitionTo()`（第 4.1 节的 variant 版就会遇到）。

## 3. 深入状态模式

### 3.1 谁决定下一个状态

GoF 讨论过两种做法：

1. **状态对象决定**（本文的做法）：`Playing::pause()` 里写 `transitionTo(paused())`。转换逻辑分散在各个状态类里，加一个状态只需新写一个类、再改几个会转到它的状态。代价是状态类之间互相知道：`Playing` 知道 `Paused`、`Buffering`、`Ended`、`Error`、`Idle`。
2. **Context 决定**：状态对象只负责行为，`Player` 里有一张表决定下一个状态。转换集中在一处，一眼能看到整个状态图；但是转换带条件（比如「重试次数到了才进入出错」）时，表就写不下了。第 3.5 节的表驱动就是这种做法。

**状态自己决定下一个状态**，也是状态和策略在意图上最大的区别。《策略模式》第 5 节说过：策略的几个实现是平等的选项，互不知道，由外面的人来挑；状态的几个实现是一张图上的节点，转换本身就是行为的一部分，`Player` 的使用者既不需要知道、也不应该去设置当前是哪个状态。如果让界面代码来写 `player.setState(paused)`，那就又回到了第 1 节：每个调用者都要自己判断现在能不能换、该换成什么。

### 3.2 共享还是新建

本文的状态对象都没有成员变量，所以每个状态只需要一个对象，所有播放器共用，这是《享元模式》：不变的部分（行为）共享，可变的部分（文件名、设备、播放位置）放在 `Player` 里，作为参数传进状态对象。好处是换状态不分配内存，状态对象的生命周期也不用管。

另一种做法是**每次转换新建一个状态对象**，`Player` 用 `unique_ptr<State>` 持有它，状态的数据可以放进状态对象里：缓冲中的重试次数放在 `Buffering` 里，离开缓冲中时随对象一起销毁，不会留在 `Player` 里占地方。代价是每次转换一次 `new`、一次 `delete`（第 4.2 节量过），还有下一节的坑。

两种做法不能混：**带成员变量的状态对象不能共享**。练习 4 就是这个错误：重试次数放进了共享的 `Buffering` 对象，两个播放器同时缓冲时，互相篡改对方的计数。

选哪种看状态数据多不多：数据少，放进 `Player`，状态对象共享；某个状态有一堆只属于它的数据，就每次新建，或者用第 4.1 节的 `std::variant`，让状态直接带着自己的数据。

### 3.3 在成员函数里换掉自己

每次新建状态对象时，最容易犯的错是这样的：

```cpp
// 《状态模式》：状态对象在自己的成员函数里把自己换掉（unique_ptr 被重置），之后再用 this 就是释放后使用
#include <cstdio>
#include <memory>

class Player;
class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void pause(Player&) {}
};

class Player {
 public:
  Player();
  void pause() { state_->pause(*this); }
  void setState(std::unique_ptr<State> s) { state_ = std::move(s); }   // 旧状态对象在这里被 delete
  const State& state() const { return *state_; }

 private:
  std::unique_ptr<State> state_;
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
  void pause(Player& p) override {
    p.setState(std::make_unique<Paused>(pos_));   // 这一行返回时，*this 已经被 delete 了
    std::printf("  在 %.1f 秒处暂停\n", pos_);    // 读的是已经释放的内存
  }

 private:
  double pos_;
};

Player::Player() : state_(std::make_unique<Playing>(3.5)) {}

int main() {
  Player p;
  p.pause();
  auto& z = static_cast<const Paused&>(p.state());
  std::printf("现在的状态：%s，停在 %.1f 秒\n", z.name(), z.pos());
}
```

`p.setState(...)` 把 `state_` 换成新的 `Paused` 对象，`unique_ptr` 随即 `delete` 了旧的 `Playing` 对象，而这时 `Playing::pause()` 还没有返回：下一行读 `pos_`，读的是 `this->pos_`，是刚刚释放的内存。这和 `delete this` 之后再用成员是一回事。

不开 ASan 时，`-O2` 编译的结果：

| | Clang / macOS | GCC / Ubuntu |
| --- | --- | --- |
| 打印出的暂停位置 | `0.0` 秒 | `5277529222…`（一个 200 多位的数）秒 |
| `main` 里再看状态 | `暂停，停在 3.5 秒` | `暂停，停在 3.5 秒` |

两边都没有崩溃，只是打印了一个错的数字。释放的内存被分配器拿去记录空闲链表了，内容被改写，但还没有还给系统，所以读它不会出段错误。这种 bug 在测试里很容易被当成「显示的小问题」放过去。加上 `-fsanitize=address` 再跑，两边都直接报错，指出了释放和使用的两个位置：

```
ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000958
READ of size 8 at 0x602000000958 thread T0
    #0 in Playing::pause(Player&) self_replace.cpp:40
    #1 in Player::pause() self_replace.cpp:16
freed by thread T0 here:
    #4 in Player::setState(...) self_replace.cpp:17
    #5 in Playing::pause(Player&) self_replace.cpp:39
```

改法有三种：

1. **换状态永远是最后一步**：`transitionTo()` 之后不再碰 `this`。靠自觉，下一个改代码的人在后面加一行日志就又错了。
2. **状态对象共享**（第 3.2 节）：没有对象会被销毁，自然没有这个问题。本文的主版本就是这样。
3. **事件函数只返回下一个状态，由 `Player` 在调用返回之后替换**：状态对象没有机会在自己还在运行时被销毁。

```cpp
// 《状态模式》：改法：事件处理函数只返回下一个状态，由 Player 在调用返回之后再替换
#include <cstdio>
#include <memory>

class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual std::unique_ptr<State> pause() { return nullptr; }   // 空指针：状态不变
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
    std::printf("  在 %.1f 秒处暂停\n", pos_);    // this 还活着
    return std::make_unique<Paused>(pos_);
  }

 private:
  double pos_;
};

class Player {
 public:
  void pause() { replace(state_->pause()); }
  const State& state() const { return *state_; }

 private:
  void replace(std::unique_ptr<State> next) {    // 状态的成员函数已经返回，这时候 delete 旧状态是安全的
    if (next) state_ = std::move(next);
  }
  std::unique_ptr<State> state_ = std::make_unique<Playing>(3.5);
};

int main() {
  Player p;
  p.pause();
  auto& z = static_cast<const Paused&>(p.state());
  std::printf("现在的状态：%s，停在 %.1f 秒\n", z.name(), z.pos());
}
```

第 3 种还有一个好处：状态对象连 `Player` 都不用知道了，事件函数变成「当前状态 + 事件 → 下一个状态」的纯函数，可以单独测试。第 4.1 节的 variant 版就是这种形式。

### 3.4 加事件比加状态难

回到第 1.3 节那张 7 × 9 的表。两种写法其实是对同一张表的两种切法：

- **switch 版按列组织**：每个事件函数是一列。加一个**事件**容易（新写一个函数），加一个**状态**难（每个函数里都要加一个分支）。
- **状态模式按行组织**：每个状态类是一行。加一个**状态**容易（新写一个类），加一个**事件**难：基类加虚函数、`Player` 加转发函数，还要逐个状态类考虑要不要重写它。

这是很多设计里都会遇到的取舍（「表达式问题」）：两个维度只有一个能做到「加东西只需新增代码」。所以要看哪个维度变得多。播放器的事件（播放、暂停、停止、跳转）比较稳定，状态会随着功能增加（缓冲中、跳转中、投屏中、试看结束……），用状态模式合适。如果是一个状态很少、但操作经常增加的对象，switch 反而更好改。

### 3.5 表驱动的状态机

第 1 节的测试里那张 `spec` 表，本身就是一台状态机：给定当前状态和事件，查表得到下一个状态。把它当成实现，就是**表驱动的状态机**：

```cpp
// 《状态模式》：表驱动的状态机：转换写成一张表，进入 / 退出动作按状态查表
#include <array>
#include <cstdio>

enum S : unsigned char { Idle, Loading, Playing, Paused, Buffering, Ended, Error, kStates };
enum E { Open, Loaded, Fail, Play, Pause, Stop, Eof, Stall, Buffered, kEvents };
const char* kNames[] = {"空闲", "加载中", "播放", "暂停", "缓冲中", "结束", "出错"};

struct Devices { bool file = false, audio = false, spinner = false; };
struct Transition { S from; E event; S to; };

constexpr Transition kTransitions[] = {   // 这张表就是状态图：一行一条边
    {Idle, Open, Loading},
    {Loading, Loaded, Playing}, {Loading, Fail, Error},   {Loading, Stop, Idle},
    {Playing, Pause, Paused},   {Playing, Stall, Buffering}, {Playing, Eof, Ended},
    {Playing, Fail, Error},     {Playing, Stop, Idle},
    {Paused, Play, Playing},    {Paused, Fail, Error},    {Paused, Stop, Idle},
    {Buffering, Buffered, Playing}, {Buffering, Pause, Paused},
    {Buffering, Fail, Error},   {Buffering, Stop, Idle},
    {Ended, Play, Playing},     {Ended, Open, Loading},   {Ended, Stop, Idle},
    {Error, Open, Loading},     {Error, Stop, Idle},
};

// 编译期把边的列表展开成 [状态][事件] 的二维数组，运行时一次下标就查到
constexpr auto buildTable() {
  std::array<std::array<S, kEvents>, kStates> t{};
  for (int s = 0; s < kStates; ++s)
    for (int e = 0; e < kEvents; ++e) t[s][e] = S(s);   // 默认：不变
  for (const auto& tr : kTransitions) t[tr.from][tr.event] = tr.to;
  return t;
}
constexpr auto kNext = buildTable();

using Action = void (*)(Devices&);
const Action kEnter[kStates] = {
    [](Devices& d) { d.file = false; },    // 空闲
    [](Devices& d) { d.file = true; },     // 加载中
    [](Devices& d) { d.audio = true; },    // 播放
    nullptr,                               // 暂停
    [](Devices& d) { d.spinner = true; },  // 缓冲中
    nullptr,                               // 结束
    [](Devices& d) { d.file = false; },    // 出错
};
const Action kExit[kStates] = {
    nullptr, nullptr,
    [](Devices& d) { d.audio = false; },   // 离开播放
    nullptr,
    [](Devices& d) { d.spinner = false; }, // 离开缓冲中
    nullptr, nullptr,
};

struct Player {
  S state = Idle;
  Devices dev;
  void fire(E e) {
    S next = kNext[state][e];
    if (next == state) return;
    if (kExit[state]) kExit[state](dev);
    std::printf("  %s → %s\n", kNames[state], kNames[next]);
    state = next;
    if (kEnter[state]) kEnter[state](dev);
  }
};

int main() {
  Player p;
  for (E e : {Open, Loaded, Stall, Play, Buffered, Pause, Play, Eof, Play, Stop}) p.fire(e);
  std::printf("转换表 %zu 条边，展开后 %d × %d = %d 格，占 %zu 字节\n",
              sizeof kTransitions / sizeof kTransitions[0], kStates, kEvents, kStates * kEvents,
              sizeof kNext);
}
```

输出：

```
  空闲 → 加载中
  加载中 → 播放
  播放 → 缓冲中
  缓冲中 → 播放
  播放 → 暂停
  暂停 → 播放
  播放 → 结束
  结束 → 播放
  播放 → 空闲
转换表 21 条边，展开后 7 × 9 = 63 格，占 63 字节
```

转换写成一个「边的列表」，读起来就是状态图；`buildTable()` 在编译期把它展开成二维数组，运行时一次下标就查到。进入、退出动作也按状态查表。表驱动的好处：

- 整个状态图在一处，可以审查、可以打印成图（遍历 `kTransitions` 输出 Graphviz 的 dot 文件），也可以从配置文件读进来；
- 查表很快，表也很小（第 4.2 节）。

缺点是**表里只能放数据**：「重试 3 次以后才进入出错」这样带条件的转换（UML 叫守卫条件），或者「从结束重播要先跳回开头」这样的转换动作，要么给表加一列函数指针，要么在表外面另写代码。条件和动作一多，表就变成了一堆函数指针，可读性还不如状态类。

### 3.6 真实项目里的状态机

- **libVLC**：3.x 版的 `libvlc_state_t` 有 `libvlc_NothingSpecial`、`libvlc_Opening`、`libvlc_Buffering`、`libvlc_Playing`、`libvlc_Paused`、`libvlc_Stopped`、`libvlc_Ended`、`libvlc_Error` 这几个值，和本文的播放器几乎一样。
- **Android `MediaPlayer`**：文档里画着一张完整的状态图（Idle、Initialized、Preparing、Prepared、Started、Paused、Stopped、PlaybackCompleted、Error、End），在错误的状态下调用方法（比如 Idle 时调 `start()`）会进入 Error 状态或者抛 `IllegalStateException`。规格表里「不变」以外的那些无效格子，它选择了报错而不是忽略。
- **ffplay**：`VideoState` 里没有一个统一的状态变量，而是一组标志：`paused`、`eof`、`seek_req`、`abort_request`……几个布尔变量的组合有很多种，哪些组合有意义、该怎么处理，散落在用到它们的各个地方。这是比 `enum` 更早的一个阶段：先把这些组合整理成有限的几个状态，才谈得上状态模式。
- **Linux 内核的 TCP**：`sk->sk_state` 是 `TCP_ESTABLISHED`、`TCP_SYN_SENT` 这样的枚举，`tcp_rcv_state_process()` 里是一个大 `switch`。GoF 书里讲状态模式用的正是 TCP 连接的例子（`TCPEstablished`、`TCPListen`、`TCPClosed`），而真实的内核用的是 switch：TCP 的状态是协议规定的、几十年不变，而处理的事件（各种报文、定时器）很多，按第 3.4 节的分析，switch 正合适。
- **生成的解析器**：Node.js 的 HTTP 解析器 llhttp 是由 llparse 从一份状态机描述**生成**的 C 代码，每读一个字节就可能转换一次，生成的代码是一个巨大的 `switch`。这种每秒转换上亿次的地方，分发开销才真正重要（第 4.2 节）。
- **状态机库**：Boost.MSM、Boost.Statechart、boost-ext/sml 这些库提供声明式的写法（转换表写成类型），支持守卫条件、转换动作、层次状态（「播放、暂停、缓冲中」都属于「已打开」，出错时统一处理）。状态很多、层次很深时可以考虑。

## 4. 现代 C++ 写法

### 4.1 std::variant + std::visit

C++17 的 `std::variant` 很适合写状态机：每个状态是一个普通的结构体，可以**各带各的数据**（加载中带文件名，缓冲中带进度，出错带原因）；当前状态就是一个 `variant`，换状态就是赋值，旧状态的数据自动销毁，不需要 `new`，也不会有第 3.3 节的问题。事件也做成 `variant`，`std::visit` 同时对两个 `variant` 分发，按「（状态，事件）」的类型组合挑重载：

```cpp
// 《状态模式》：std::variant + std::visit 写状态机：状态、事件都是普通结构体，可以各带各的数据
#include <cstdio>
#include <optional>
#include <string>
#include <variant>

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct Idle {};                                   // 状态
struct Loading { std::string url; };
struct Playing {};
struct Paused {};
struct Buffering { int percent = 0; };
struct Ended {};
struct Error { std::string why; };
using State = std::variant<Idle, Loading, Playing, Paused, Buffering, Ended, Error>;

struct Open { std::string url; };                 // 事件
struct Loaded {};
struct Fail { std::string why; };
struct Play {};
struct Pause {};
struct Stop {};
struct Eof {};
struct Stall {};
struct Progress { int percent; };
struct Buffered {};
using Event = std::variant<Open, Loaded, Fail, Play, Pause, Stop, Eof, Stall, Progress, Buffered>;

using Next = std::optional<State>;   // 空：状态不变

// 转换函数：按 (状态, 事件) 的类型重载；没列出的组合落到最后的泛型版本
Next transition(const State& s, const Event& e) {
  return std::visit(overloaded{
      [](const Idle&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Ended&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Error&, const Open& o) -> Next { return Loading{o.url}; },
      [](const Loading&, const Loaded&) -> Next { return Playing{}; },
      [](const Playing&, const Pause&) -> Next { return Paused{}; },
      [](const Playing&, const Stall&) -> Next { return Buffering{}; },
      [](const Playing&, const Eof&) -> Next { return Ended{}; },
      [](const Paused&, const Play&) -> Next { return Playing{}; },
      [](const Ended&, const Play&) -> Next { return Playing{}; },
      [](const Buffering&, const Pause&) -> Next { return Paused{}; },
      [](const Buffering&, const Progress& p) -> Next { return Buffering{p.percent}; },
      [](const Buffering&, const Buffered&) -> Next { return Playing{}; },
      [](const Idle&, const Fail&) -> Next { return {}; },
      [](const Ended&, const Fail&) -> Next { return {}; },
      [](const Error&, const Fail&) -> Next { return {}; },
      [](const auto&, const Fail& f) -> Next { return Error{f.why}; },   // 其余状态出错都进 Error
      [](const Idle&, const Stop&) -> Next { return {}; },
      [](const auto&, const Stop&) -> Next { return Idle{}; },           // 其余状态都能停止
      [](const auto&, const auto&) -> Next { return {}; },
  }, s, e);
}

struct Devices { bool file = false, audio = false, spinner = false; };

void enter(const State& s, Devices& d) {
  std::visit(overloaded{
      [&](const Idle&) { d.file = false; },
      [&](const Loading&) { d.file = true; },
      [&](const Playing&) { d.audio = true; },
      [&](const Buffering&) { d.spinner = true; },
      [&](const Error&) { d.file = false; },
      [](const auto&) {},
  }, s);
}
void exit(const State& s, Devices& d) {
  std::visit(overloaded{
      [&](const Playing&) { d.audio = false; },
      [&](const Buffering&) { d.spinner = false; },
      [](const auto&) {},
  }, s);
}

const char* name(const State& s) {
  static const char* n[] = {"空闲", "加载中", "播放", "暂停", "缓冲中", "结束", "出错"};
  return n[s.index()];
}

class Player {
 public:
  Devices dev;
  void fire(const Event& e) {
    Next n = transition(state_, e);
    if (!n) return;
    exit(state_, dev);
    std::printf("  %s → %s", name(state_), name(*n));
    state_ = std::move(*n);
    enter(state_, dev);
    if (auto* b = std::get_if<Buffering>(&state_)) std::printf("（%d%%）", b->percent);
    if (auto* x = std::get_if<Error>(&state_)) std::printf("（%s）", x->why.c_str());
    std::printf("\n");
  }

 private:
  State state_;
};

int main() {
  Player p;
  for (const Event& e : {Event{Open{"bbb-10s.mp4"}}, Event{Loaded{}}, Event{Stall{}},
                         Event{Progress{40}}, Event{Play{}}, Event{Progress{100}}, Event{Buffered{}},
                         Event{Stall{}}, Event{Fail{"网络断开"}}, Event{Open{"bbb-10s.mp4"}}})
    p.fire(e);
  std::printf("sizeof(State) = %zu，sizeof(Event) = %zu，sizeof(Player) = %zu\n",
              sizeof(State), sizeof(Event), sizeof(Player));
}
```

Clang（libc++）的输出：

```
  空闲 → 加载中
  加载中 → 播放
  播放 → 缓冲中（0%）
  缓冲中 → 缓冲中（40%）
  缓冲中 → 缓冲中（100%）
  缓冲中 → 播放
  播放 → 缓冲中（0%）
  缓冲中 → 出错（网络断开）
  出错 → 加载中
sizeof(State) = 32，sizeof(Event) = 32，sizeof(Player) = 40
```

GCC（libstdc++）上前面一样，最后一行是 `sizeof(State) = 40，sizeof(Event) = 40，sizeof(Player) = 48`：`variant` 和 `union` 一样，大小取最大的那个成员，这里是带 `std::string` 的 `Loading` 和 `Error`，而 `std::string` 在 libc++ 里是 24 字节，在 libstdc++ 里是 32 字节。

几个要注意的地方：

- **最后那个泛型重载 `(auto, auto)` 是一把双刃剑**：它让没列出的组合都「不变」，省掉了几十个重载；但它和第 1 节的 `default` 一样，会默默吞掉漏写的组合。去掉它，每个漏写的组合都会编译报错，代价是 7 个状态 × 10 个事件 = 70 个组合都得写出来（可以用 `(const auto&, const Stop&)` 这样的半泛型重载一次处理一整列）。
- **重载解析**：`(const Idle&, const Stop&)` 比 `(const auto&, const Stop&)` 更匹配，后者又比 `(const auto&, const auto&)` 更匹配，所以「空闲时停止不变、其他状态停止回到空闲」可以这样分层写。
- **自转换**：输出里「缓冲中 → 缓冲中（40%）」是第 2.6 节说的自转换，转圈图标会被隐藏再显示一次。只更新进度时，应该在 `fire()` 里特判，直接改 `std::get<Buffering>(state_).percent`，不执行退出、进入动作。
- **没有虚函数，状态之间互不知道**：转换函数集中在 `transition()` 一处，更接近第 3.1 节的「Context 决定」。状态类型是封闭的（加一个状态要改 `State` 的定义），但加状态、加事件都只需在 `transition()` 里加几行重载。

### 4.2 五种写法的开销

`bench.cpp` 用五种写法实现同一台 7 个状态、9 个事件的状态机，分发同一串伪随机事件（每次测量 1966 万 个，其中 424 万 个引起转换，五种写法的转换次数和最后的状态都一样），`-O2`，7 次取中位数：

<figure class="diagram">
<svg viewBox="0 0 720 216" role="img" aria-label="五种写法分发一个事件的平均时间：Clang 上 switch 1.48、共享状态对象 3.33、每次 new 5.21、表驱动 1.91、variant 6.30 纳秒；GCC 上分别是 0.39、5.05、6.46、0.54、5.85 纳秒"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="178.0" y="30.0" width="38.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="222.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">1.48 ns</text><rect x="178.0" y="60.0" width="85.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="269.6" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">3.33 ns</text><rect x="178.0" y="90.0" width="134.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="318.0" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">5.21 ns</text><rect x="178.0" y="120.0" width="49.1" height="20.0" rx="2" style="fill:var(--link)"/><text x="233.1" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1.91 ns</text><rect x="178.0" y="150.0" width="162.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="346.0" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">6.3 ns</text><text x="448.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="448.0" y="30.0" width="10.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="464.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.39 ns</text><rect x="448.0" y="60.0" width="129.9" height="20.0" rx="2" style="fill:var(--accent)"/><text x="583.9" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.05 ns</text><rect x="448.0" y="90.0" width="166.1" height="20.0" rx="2" style="fill:var(--danger)"/><text x="620.1" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">6.46 ns</text><rect x="448.0" y="120.0" width="13.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="467.9" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.54 ns</text><rect x="448.0" y="150.0" width="150.4" height="20.0" rx="2" style="fill:var(--link)"/><text x="604.4" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">5.85 ns</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">enum + switch</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">状态模式（共享对象）</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">状态模式（每次 new）</text><text x="170.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">表驱动</text><text x="170.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">variant + visit</text><text x="10.0" y="208.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp，-O2：同一串伪随机事件（每次测量 1966 万个，其中 424 万个引起转换），7 次取中位数；GCC 跑在 colima 虚拟机里</text></g></svg>
</figure>

| 写法 | Clang ns/事件 | GCC ns/事件 | 记录当前状态占 | 每次转换的堆分配 |
| --- | --- | --- | --- | --- |
| enum + switch | 1.48 | 0.39 | 1 字节 | 0 |
| 状态模式（共享状态对象） | 3.33 | 5.05 | 8 字节（指针） | 0 |
| 状态模式（每次 new） | 5.21 | 6.46 | 8 字节 + 堆上 8 字节 | 1 次 |
| 表驱动 | 1.91 | 0.54 | 1 字节 + 63 字节的表 | 0 |
| variant + visit | 6.30 | 5.85 | 8 字节（Clang）/ 2 字节（GCC） | 0 |

<details>
<summary>展开 bench.cpp</summary>

<!-- run: off -->
```cpp
// 《状态模式》：五种写法分发同一串事件的开销、状态占多大、堆分配几次（请用 -O2 编译）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <variant>
#include <vector>

static long g_allocs = 0;
void* operator new(std::size_t n) { ++g_allocs; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

enum S : unsigned char { Idle, Loading, Playing, Paused, Buffering, Ended, Error, kStates };
enum E : unsigned char { Open, Loaded, Fail, Play, Pause, Stop, Eof, Stall, Buffered, kEvents };
struct Devices { bool file = false, audio = false, spinner = false; };

// 进入 / 退出动作，五种写法共用
inline void enterS(S s, Devices& d) {
  switch (s) {
    case Idle: case Error: d.file = false; break;
    case Loading: d.file = true; break;
    case Playing: d.audio = true; break;
    case Buffering: d.spinner = true; break;
    default: break;
  }
}
inline void exitS(S s, Devices& d) {
  if (s == Playing) d.audio = false;
  if (s == Buffering) d.spinner = false;
}

// ---- 1. enum + switch ----
struct SwitchPlayer {
  S s = Idle; Devices dev; long n = 0;
  void go(S to) { exitS(s, dev); s = to; enterS(s, dev); ++n; }
  void fire(E e) {
    switch (s) {
      case Idle: if (e == Open) go(Loading); break;
      case Loading: if (e == Loaded) go(Playing); else if (e == Fail) go(Error); else if (e == Stop) go(Idle); break;
      case Playing:
        switch (e) {
          case Pause: go(Paused); break; case Stall: go(Buffering); break; case Eof: go(Ended); break;
          case Fail: go(Error); break; case Stop: go(Idle); break; default: break;
        }
        break;
      case Paused: if (e == Play) go(Playing); else if (e == Fail) go(Error); else if (e == Stop) go(Idle); break;
      case Buffering:
        if (e == Buffered) go(Playing); else if (e == Pause) go(Paused);
        else if (e == Fail) go(Error); else if (e == Stop) go(Idle);
        break;
      case Ended: if (e == Play) go(Playing); else if (e == Open) go(Loading); else if (e == Stop) go(Idle); break;
      case Error: if (e == Open) go(Loading); else if (e == Stop) go(Idle); break;
      default: break;
    }
  }
  S state() const { return s; }
};

// ---- 2 / 3. 状态模式：共享的状态对象，或每次转换 new 一个 ----
struct VPlayer;
struct VState {
  virtual ~VState() = default;
  virtual S id() const = 0;
  virtual void open(VPlayer&) {} virtual void loaded(VPlayer&) {} virtual void fail(VPlayer&) {}
  virtual void play(VPlayer&) {} virtual void pause(VPlayer&) {} virtual void stop(VPlayer&) {}
  virtual void eof(VPlayer&) {} virtual void stall(VPlayer&) {} virtual void buffered(VPlayer&) {}
};
struct VPlayer {
  explicit VPlayer(bool alloc);
  bool alloc; VState* st; std::unique_ptr<VState> owned; Devices dev; long n = 0; int next = -1;
  void go(S to) { next = to; }   // 状态对象只登记下一个状态，由 fire() 在调用返回后替换
  void fire(E e) {
    switch (e) {
      case Open: st->open(*this); break; case Loaded: st->loaded(*this); break;
      case Fail: st->fail(*this); break; case Play: st->play(*this); break;
      case Pause: st->pause(*this); break; case Stop: st->stop(*this); break;
      case Eof: st->eof(*this); break; case Stall: st->stall(*this); break;
      case Buffered: st->buffered(*this); break; default: break;
    }
    if (next >= 0) { exitS(st->id(), dev); replace(S(next)); enterS(st->id(), dev); ++n; next = -1; }
  }
  void replace(S to);
  S state() const { return st->id(); }
};
struct VIdle : VState { S id() const override { return Idle; } void open(VPlayer& p) override { p.go(Loading); } };
struct VLoading : VState {
  S id() const override { return Loading; }
  void loaded(VPlayer& p) override { p.go(Playing); } void fail(VPlayer& p) override { p.go(Error); }
  void stop(VPlayer& p) override { p.go(Idle); }
};
struct VPlaying : VState {
  S id() const override { return Playing; }
  void pause(VPlayer& p) override { p.go(Paused); } void stall(VPlayer& p) override { p.go(Buffering); }
  void eof(VPlayer& p) override { p.go(Ended); } void fail(VPlayer& p) override { p.go(Error); }
  void stop(VPlayer& p) override { p.go(Idle); }
};
struct VPaused : VState {
  S id() const override { return Paused; }
  void play(VPlayer& p) override { p.go(Playing); } void fail(VPlayer& p) override { p.go(Error); }
  void stop(VPlayer& p) override { p.go(Idle); }
};
struct VBuffering : VState {
  S id() const override { return Buffering; }
  void buffered(VPlayer& p) override { p.go(Playing); } void pause(VPlayer& p) override { p.go(Paused); }
  void fail(VPlayer& p) override { p.go(Error); } void stop(VPlayer& p) override { p.go(Idle); }
};
struct VEnded : VState {
  S id() const override { return Ended; }
  void play(VPlayer& p) override { p.go(Playing); } void open(VPlayer& p) override { p.go(Loading); }
  void stop(VPlayer& p) override { p.go(Idle); }
};
struct VError : VState {
  S id() const override { return Error; }
  void open(VPlayer& p) override { p.go(Loading); } void stop(VPlayer& p) override { p.go(Idle); }
};
VIdle gIdle; VLoading gLoading; VPlaying gPlaying; VPaused gPaused; VBuffering gBuffering; VEnded gEnded; VError gError;
VState* const kShared[kStates] = {&gIdle, &gLoading, &gPlaying, &gPaused, &gBuffering, &gEnded, &gError};
std::unique_ptr<VState> make(S s) {
  switch (s) {
    case Idle: return std::make_unique<VIdle>(); case Loading: return std::make_unique<VLoading>();
    case Playing: return std::make_unique<VPlaying>(); case Paused: return std::make_unique<VPaused>();
    case Buffering: return std::make_unique<VBuffering>(); case Ended: return std::make_unique<VEnded>();
    default: return std::make_unique<VError>();
  }
}
VPlayer::VPlayer(bool a) : alloc(a), st(&gIdle) { if (alloc) { owned = make(Idle); st = owned.get(); } }
void VPlayer::replace(S to) {
  if (alloc) { owned = make(to); st = owned.get(); } else st = kShared[to];
}

// ---- 4. 表驱动 ----
struct TablePlayer {
  static constexpr S _ = kStates;   // 不变
  static constexpr S kNext[kStates][kEvents] = {
      // Open Loaded Fail Play Pause Stop Eof Stall Buffered
      {Loading, _, _, _, _, _, _, _, _},
      {_, Playing, Error, _, _, Idle, _, _, _},
      {_, _, Error, _, Paused, Idle, Ended, Buffering, _},
      {_, _, Error, Playing, _, Idle, _, _, _},
      {_, _, Error, _, Paused, Idle, _, _, Playing},
      {Loading, _, _, Playing, _, Idle, _, _, _},
      {Loading, _, _, _, _, Idle, _, _, _},
  };
  S s = Idle; Devices dev; long n = 0;
  void fire(E e) {
    S to = kNext[s][e];
    if (to == _) return;
    exitS(s, dev); s = to; enterS(s, dev); ++n;
  }
  S state() const { return s; }
};

// ---- 5. std::variant + std::visit ----
namespace v {
struct Idle {}; struct Loading {}; struct Playing {}; struct Paused {}; struct Buffering {}; struct Ended {}; struct Error {};
using State = std::variant<Idle, Loading, Playing, Paused, Buffering, Ended, Error>;
struct Open {}; struct Loaded {}; struct Fail {}; struct Play {}; struct Pause {}; struct Stop {}; struct Eof {};
struct Stall {}; struct Buffered {};
using Event = std::variant<Open, Loaded, Fail, Play, Pause, Stop, Eof, Stall, Buffered>;
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
using Next = const State*;   // 空指针：不变（状态没有数据，指向常量对象即可）
const State kIdle = Idle{}, kLoading = Loading{}, kPlaying = Playing{}, kPaused = Paused{},
            kBuffering = Buffering{}, kEnded = Ended{}, kError = Error{};
struct Player {
  State s; Devices dev; long n = 0;
  void fire(const Event& e) {
    Next to = std::visit(overloaded{
        [](Idle, Open) -> Next { return &kLoading; }, [](Ended, Open) -> Next { return &kLoading; },
        [](Error, Open) -> Next { return &kLoading; }, [](Loading, Loaded) -> Next { return &kPlaying; },
        [](Playing, Pause) -> Next { return &kPaused; }, [](Playing, Stall) -> Next { return &kBuffering; },
        [](Playing, Eof) -> Next { return &kEnded; }, [](Paused, Play) -> Next { return &kPlaying; },
        [](Ended, Play) -> Next { return &kPlaying; }, [](Buffering, Pause) -> Next { return &kPaused; },
        [](Buffering, Buffered) -> Next { return &kPlaying; },
        [](Idle, Fail) -> Next { return nullptr; }, [](Ended, Fail) -> Next { return nullptr; },
        [](Error, Fail) -> Next { return nullptr; }, [](auto, Fail) -> Next { return &kError; },
        [](Idle, Stop) -> Next { return nullptr; }, [](auto, Stop) -> Next { return &kIdle; },
        [](auto, auto) -> Next { return nullptr; }}, s, e);
    if (!to) return;
    exitS(S(s.index()), dev); s = *to; enterS(S(s.index()), dev); ++n;
  }
  S state() const { return S(s.index()); }
};
const Event kEv[kEvents] = {Open{}, Loaded{}, Fail{}, Play{}, Pause{}, Stop{}, Eof{}, Stall{}, Buffered{}};
}  // namespace v

// ---- 测量 ----
template <class F>
double median_ns(F&& run, long events) {
  double t[7];
  for (double& x : t) {
    auto a = std::chrono::steady_clock::now();
    run();
    auto b = std::chrono::steady_clock::now();
    x = std::chrono::duration<double, std::nano>(b - a).count() / events;
  }
  std::sort(t, t + 7);
  return t[3];
}

int main() {
  // 事件串：伪随机，偏向「当前状态下有效」的事件，让一半左右的事件真的引起转换
  const int N = 1 << 16;
  std::vector<E> evs(N);
  unsigned x = 12345;
  for (auto& e : evs) { x = x * 1103515245u + 12345u; e = E((x >> 16) % kEvents); }
  const int R = 300;   // 每次测量分发 N × R ≈ 2000 万个事件
  const long total = long(N) * R;
  std::vector<v::Event> vevs(N);
  for (int i = 0; i < N; ++i) vevs[i] = v::kEv[evs[i]];

  long chk[5]; int fin[5]; long allocs[5];
  double ns[5];
  { SwitchPlayer p; long a0 = g_allocs;
    ns[0] = median_ns([&] { for (int r = 0; r < R; ++r) for (E e : evs) p.fire(e); }, total);
    chk[0] = p.n; fin[0] = p.state(); allocs[0] = g_allocs - a0; }
  { VPlayer p(false); long a0 = g_allocs;
    ns[1] = median_ns([&] { for (int r = 0; r < R; ++r) for (E e : evs) p.fire(e); }, total);
    chk[1] = p.n; fin[1] = p.state(); allocs[1] = g_allocs - a0; }
  { VPlayer p(true); long a0 = g_allocs;
    ns[2] = median_ns([&] { for (int r = 0; r < R; ++r) for (E e : evs) p.fire(e); }, total);
    chk[2] = p.n; fin[2] = p.state(); allocs[2] = g_allocs - a0; }
  { TablePlayer p; long a0 = g_allocs;
    ns[3] = median_ns([&] { for (int r = 0; r < R; ++r) for (E e : evs) p.fire(e); }, total);
    chk[3] = p.n; fin[3] = p.state(); allocs[3] = g_allocs - a0; }
  { v::Player p; long a0 = g_allocs;
    ns[4] = median_ns([&] { for (int r = 0; r < R; ++r) for (const auto& e : vevs) p.fire(e); }, total);
    chk[4] = p.n; fin[4] = p.state(); allocs[4] = g_allocs - a0; }

  const char* names[] = {"enum + switch", "状态模式（共享状态对象）", "状态模式（每次 new）", "表驱动", "variant + visit"};
  std::size_t holder[] = {sizeof(S), sizeof(VState*), sizeof(std::unique_ptr<VState>), sizeof(S), sizeof(v::State)};
  std::size_t heap[] = {0, 0, sizeof(VIdle), 0, 0};
  std::printf("每次测量分发 %ld 个事件，其中 %ld 个引起转换；7 次取中位数\n", total, chk[0] / 7);
  std::printf("  ns/事件  当前状态占  堆上  每次测量的堆分配  写法\n");
  for (int i = 0; i < 5; ++i) {
    if (chk[i] != chk[0] || fin[i] != fin[0]) std::printf("  结果不一致！\n");
    std::printf("  %6.2f   %4zu 字节  %2zu 字节  %10ld  %s\n", ns[i], holder[i], heap[i], allocs[i] / 7, names[i]);
  }
}
```

</details>

几点观察：

- **switch 和表驱动最快**，在 GCC 上快得惊人（0.4～0.5 纳秒，不到两个时钟周期）：状态就是寄存器里的一个小整数，没有间接调用，编译器能把整个循环优化得很紧凑。
- **状态模式（共享对象）** 多了一次虚函数调用，加上 `fire()` 里按事件分发的 `switch`，3～5 纳秒。
- **每次 new** 比共享平均每个事件多 1.4～1.9 纳秒。只有约 21.5% 的事件引起转换，摊到每次转换上，一次 `new` 加一次 `delete` 大约 6～9 纳秒；每次测量 424 万次转换，就是 424 万次堆分配。
- **variant + visit 最慢**：两个 `variant` 一起 `visit`，要查一张 7 × 9 的函数指针表，再间接调用一次，编译器很难内联。
- **大小**：GCC 的 `variant` 只多 1 字节记下标，加上 1 字节的空结构体，共 2 字节；libc++ 记下标用的是 4 字节的 `unsigned int`，于是 8 字节。

最慢的也只要 6 纳秒多。播放器一秒钟最多处理几百个事件，这点开销完全可以忽略：写播放器、连接、下载任务这类状态机，按可读性和好不好改来选。只有每个字节都要转换一次的地方（第 3.6 节的 HTTP 解析器、词法分析器、协议解码），才值得用 switch 或表驱动去抠这几纳秒。

### 4.3 怎么选

| 情况 | 推荐 |
| --- | --- |
| 状态少（三四个）、事件少、以后也不太会加 | `enum` + `switch`，加 `-Wall`，`switch` 不写 `default` |
| 状态多，会继续加状态，每个状态的行为复杂 | 状态模式，状态对象共享，数据放在 Context |
| 某些状态有自己的数据，想让编译器管生命周期 | `std::variant` + `std::visit` |
| 状态图要审查、要画出来、要从配置读 | 表驱动 |
| 每秒转换上亿次（解析器） | switch 或表驱动，甚至生成代码 |
| 有层次状态、守卫条件、历史状态 | 状态机库（Boost.MSM、boost-ext/sml） |

用 `switch` 时有一个小技巧：`-Wall` 里的 `-Wswitch` 只检查不带 `default` 的 `switch`；GCC 和 Clang 都还有一个 `-Wswitch-enum`，带 `default` 的也检查。对第 1.2 节只加了枚举值的版本打开它，`play()` 那个 `switch` 也报了警告，但报的是 `Paused`、`Buffering`、`Ended` 三个值都没有显式处理：它要求每个枚举值都写出 `case`，`default` 不算数。想用它，就得放弃 `default`，把每个状态都列出来。

## 5. 状态和相似的模式

《策略模式》第 5 节从策略那边比较过状态，这里从状态这边补充：

| 模式 | 把什么做成对象 | 谁来换 | 实现之间知不知道彼此 |
| --- | --- | --- | --- |
| **状态** | 对象在某个状态下的全部行为 | 状态对象自己，在处理事件时 | 知道：`Playing` 会换成 `Paused` |
| 策略 | 做一件事的一种算法 | 外面的使用者，或上下文按配置 | 不知道，互相平等、可以互换 |
| 命令 | 一个要执行的请求 | 不换，执行完就结束（可以撤销、排队） | 不知道 |
| 表驱动状态机 | 不做成对象，转换是表里的数据 | Context 查表 | 状态只是编号 |

- **状态和策略**：类图几乎一样，区别在意图。一个简单的判断方法是看「`setState` / `setStrategy` 是谁调的」：如果是使用者根据配置、用户选择来调的，是策略（缩放算法、码率自适应算法）；如果是对象在处理事件的过程中自己调的，是状态。还有一个区别：策略对象通常只有一两个方法，各个策略实现的都是同样的事；状态对象要对**所有**事件表态，大部分方法在大部分状态下什么也不做。
- **状态和命令**：都是「一个事件对应一个对象」的感觉，但方向相反。命令是把**事件**（一次操作）做成对象，可以排队、记录、撤销；状态是把**状态**做成对象，事件是它的方法。两者常常一起用：界面把用户操作封装成命令，命令执行时调用 `player.pause()`，`Player` 再交给当前状态处理。
- **状态和享元、单例**：没有成员的状态对象可以共享（享元），本文用函数里的静态对象实现，每个状态一个实例，看起来像单例。但它不需要单例的全局访问点，只是一个实现上的选择。

## 6. 小结

- 状态模式把对象在每个状态下的行为放进一个状态类，Context 只持有当前状态，把事件转交给它；状态对象处理事件并决定下一个状态，所以看起来像对象换了一个类。
- 它解决的是「同一个状态的行为分散在所有事件函数里」：本文加一个「缓冲中」，到处判断 `state_` 的版本改了 4 处、漏了 2 处，63 格里 2 格是非法转换，编译器只提醒了 1 处；状态模式的改动集中在新的 `Buffering` 类里，63 格全对。
- 进入 / 退出动作把「属于某个状态的资源」管起来，不管从哪条边离开都会释放，和 RAII 一个思路。
- 状态对象没有成员时可以共享（享元），换状态不分配内存；带成员的状态对象不能共享。每次新建状态对象时，不要在成员函数里换掉自己之后再碰 `this`（本文 ASan 实测 heap-use-after-free），让事件函数返回下一个状态，由 Context 替换。
- 状态模式按「行」组织状态表，加状态容易、加事件难；switch 按「列」组织，正好相反。表驱动把整张表放在一处，`std::variant` 让状态各带数据、由编译器管生命周期。
- 五种写法分发一个事件 0.4～6.5 纳秒，对播放器这类状态机可以忽略，按可读性选。
- 状态和策略的区别：谁来换。状态自己决定下一个状态，策略由外面来选。

## 7. 练习

1. **辨析**：下面几个需求，分别更适合用状态、策略还是命令？
   - (a) 播放器支持几种码率自适应算法（按吞吐量、按缓冲区长度），用户可以在设置里切换；
   - (b) 字幕下载任务：连接中、下载中、等待重试、完成、失败；
   - (c) 视频编辑器的「剪切、粘贴、调色」要支持撤销；
   - (d) 播放器界面上那个按钮，播放时显示「暂停」图标，暂停时显示「播放」图标，点它的效果也跟着变。
2. **动手**：给状态模式的播放器加一个「跳转中」状态：播放或暂停时拖动进度条进入跳转中，跳转完成后**回到跳转前的状态**（播放就继续播放，暂停就还是暂停）；跳转中又拖了一次就更新目标位置，跳转中按播放 / 暂停就改变「跳完以后去哪」。状态对象是共享的，「跳转前的状态」该存在哪里？
3. **设计**：用 `std::variant` 写一个分片下载任务的状态机：连接中、下载中、等待重试、完成、失败。网络出错后等一会儿重试，最多试 3 次，每次等待的时间翻倍（500 毫秒、1000 毫秒），重新连上后从断点继续下载。哪些数据应该放在哪个状态里？
4. **找问题**：下面的代码里，两个播放器先后进入缓冲中，缓冲超时就重试，最多 3 次。可 B 第一次超时就打印「第 2 次重试」，第二次超时就报错了，而 A 反而多试了一次。问题在哪？怎么改？

   ```cpp
   // 《状态模式》练习 4：找问题：两个播放器同时缓冲，B 第一次超时就是「第 2 次重试」，第二次超时就报错了
   #include <cstdio>

   class Player;
   class State {
    public:
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
     void transitionTo(State& next) {
       std::printf("  [%s] %s → %s\n", name, state_->name(), next.name());
       state_ = &next;
       state_->enter(*this);
     }
     void startBuffering() { transitionTo(buffering()); }

    private:
     State* state_ = &idle();
   };

   class Idle : public State {
    public:
     const char* name() const override { return "空闲"; }
   };
   State& idle() { static Idle s; return s; }

   class Buffering : public State {   // 缓冲超时就重试，最多 3 次
    public:
     const char* name() const override { return "缓冲中"; }
     void enter(Player&) override { retries_ = 0; }
     void timeout(Player& p) override {
       if (++retries_ >= 3) p.transitionTo(error());
       else std::printf("  [%s] 超时，第 %d 次重试\n", p.name, retries_);
     }

    private:
     int retries_ = 0;
   };
   State& buffering() { static Buffering s; return s; }

   class Error : public State {
    public:
     const char* name() const override { return "出错"; }
   };
   State& error() { static Error s; return s; }

   int main() {
     Player a("A"), b("B");
     a.startBuffering();
     a.onTimeout(); a.onTimeout();
     b.startBuffering();
     a.onTimeout();
     b.onTimeout();
     b.onTimeout();
   }
   ```

   输出：

   ```
     [A] 空闲 → 缓冲中
     [A] 超时，第 1 次重试
     [A] 超时，第 2 次重试
     [B] 空闲 → 缓冲中
     [A] 超时，第 1 次重试
     [B] 超时，第 2 次重试
     [B] 缓冲中 → 出错
   ```

<details>
<summary>点击查看答案</summary>

**1.** (a) **策略**：几种算法做的是同一件事（选下一个分片的码率），平等、可互换，由用户在设置里选，算法之间互不知道。(b) **状态**：任务在不同阶段对同样的事件（网络出错、定时器到了）反应不同，下一个状态由当前状态决定，比如「下载中出错」进入等待重试，「等待重试时定时器到了」进入连接中。(c) **命令**：每次操作做成一个对象，记下撤销需要的信息，放进历史栈。(d) **状态**：按钮的外观和行为都由播放器的状态决定；界面可以用观察者订阅播放器的状态变化来更新图标，点击时调用 `player.play()` 或 `player.pause()`，由当前状态决定效果。不要在界面里再写一遍 `if (playing) ... else ...`。

**2.** 状态对象是共享的，不能有成员变量，所以「跳转前的状态」和目标位置都要放在 `Player` 里。`Playing` 和 `Paused` 的 `seek()` 都调用 `Player::beginSeek()`：记下当前状态、目标位置，再进入跳转中。`Seeking` 收到跳转完成时，换回记下的那个状态。这在 UML 状态图里叫**历史状态**。

```cpp
// 《状态模式》练习 2：加「跳转中」状态，跳转完回到跳转前的状态（播放或暂停）
#include <cstdio>

class Player;
class State {
 public:
  virtual ~State() = default;
  virtual const char* name() const = 0;
  virtual void play(Player&) {}
  virtual void pause(Player&) {}
  virtual void seek(Player&, double) {}
  virtual void seekDone(Player&) {}
};
State& playing(); State& paused(); State& seeking();

class Player {
 public:
  double pos = 0;
  State* resumeTo = nullptr;   // 跳转完回到哪个状态：状态对象是共享的，这份数据要放在 Player 里
  void play() { state_->play(*this); }
  void pause() { state_->pause(*this); }
  void seek(double t) { state_->seek(*this, t); }
  void onSeekDone() { state_->seekDone(*this); }
  void transitionTo(State& next) {
    std::printf("  %s → %s\n", state_->name(), next.name());
    state_ = &next;
  }
  // 播放和暂停都可以跳转：记下当前状态，再进入「跳转中」
  void beginSeek(double t) { pos = t; resumeTo = state_; transitionTo(seeking()); }

 private:
  State* state_ = &paused();
};

class Playing : public State {
 public:
  const char* name() const override { return "播放"; }
  void pause(Player& p) override { p.transitionTo(paused()); }
  void seek(Player& p, double t) override { p.beginSeek(t); }
};
State& playing() { static Playing s; return s; }

class Paused : public State {
 public:
  const char* name() const override { return "暂停"; }
  void play(Player& p) override { p.transitionTo(playing()); }
  void seek(Player& p, double t) override { p.beginSeek(t); }
};
State& paused() { static Paused s; return s; }

class Seeking : public State {
 public:
  const char* name() const override { return "跳转中"; }
  void play(Player& p) override { p.resumeTo = &playing(); }   // 跳转中按播放 / 暂停：只改「跳完去哪」
  void pause(Player& p) override { p.resumeTo = &paused(); }
  void seek(Player& p, double t) override { p.pos = t; }        // 连续拖动进度条：只更新目标位置
  void seekDone(Player& p) override { p.transitionTo(*p.resumeTo); }
};
State& seeking() { static Seeking s; return s; }

int main() {
  Player p;
  std::printf("暂停时跳转：\n");
  p.seek(3); p.onSeekDone();
  std::printf("播放时跳转，跳转中又拖了一次、按了暂停：\n");
  p.play(); p.seek(5); p.seek(7.5); p.pause(); p.onSeekDone();
  std::printf("最后停在 %.1f 秒\n", p.pos);
}
```

输出：

```
暂停时跳转：
  暂停 → 跳转中
  跳转中 → 暂停
播放时跳转，跳转中又拖了一次、按了暂停：
  暂停 → 播放
  播放 → 跳转中
  跳转中 → 暂停
最后停在 7.5 秒
```

跳转中按播放、暂停都没有离开跳转中，只是改了 `resumeTo`：如果在跳转中直接切到暂停，跳转完成的事件就会被暂停状态忽略，解码器停在一个半途的位置。

**3.** 尝试次数在连接中、下载中、等待重试三个状态里都要用，跟着状态一路传下去；已下载的字节数也一样（断点续传）；等待时间只属于等待重试；完成只需要总字节数，失败只需要试了几次。每个状态只带自己需要的数据，离开时自动销毁：

```cpp
// 《状态模式》练习 3：用 std::variant 写分片下载任务：失败后等一会儿重试，最多 3 次，等待时间翻倍，重连后从断点继续
#include <cstdio>
#include <optional>
#include <variant>

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct Connecting { int attempt; long bytes; };   // 第几次尝试（从 1 开始），已经下载了多少
struct Downloading { int attempt; long bytes; };
struct Waiting { int attempt; int delayMs; long bytes; };   // 失败后等多久再连
struct Done { long bytes; };
struct Failed { int attempts; };
using Task = std::variant<Connecting, Downloading, Waiting, Done, Failed>;

struct Connected {};
struct Data { long n; };
struct NetError {};
struct Timer {};
struct Finished {};
using Event = std::variant<Connected, Data, NetError, Timer, Finished>;

constexpr int kMaxAttempts = 3;

Task retryOrFail(int attempt, long bytes) {
  if (attempt >= kMaxAttempts) return Failed{attempt};
  return Waiting{attempt, 500 << (attempt - 1), bytes};   // 500、1000 毫秒
}

std::optional<Task> next(const Task& t, const Event& e) {
  return std::visit(overloaded{
      [](const Connecting& c, const Connected&) -> std::optional<Task> { return Downloading{c.attempt, c.bytes}; },
      [](const Connecting& c, const NetError&) -> std::optional<Task> { return retryOrFail(c.attempt, c.bytes); },
      [](const Downloading& d, const Data& x) -> std::optional<Task> { return Downloading{d.attempt, d.bytes + x.n}; },
      [](const Downloading& d, const NetError&) -> std::optional<Task> { return retryOrFail(d.attempt, d.bytes); },
      [](const Downloading& d, const Finished&) -> std::optional<Task> { return Done{d.bytes}; },
      [](const Waiting& w, const Timer&) -> std::optional<Task> { return Connecting{w.attempt + 1, w.bytes}; },
      [](const auto&, const auto&) -> std::optional<Task> { return std::nullopt; },
  }, t, e);
}

void show(const Task& t) {
  std::visit(overloaded{
      [](const Connecting& c) { std::printf("连接中（第 %d 次，从 %ld 字节处开始）\n", c.attempt, c.bytes); },
      [](const Downloading& d) { std::printf("下载中（第 %d 次，%ld 字节）\n", d.attempt, d.bytes); },
      [](const Waiting& w) { std::printf("等待 %d 毫秒后重试\n", w.delayMs); },
      [](const Done& d) { std::printf("完成，共 %ld 字节\n", d.bytes); },
      [](const Failed& f) { std::printf("失败，试了 %d 次\n", f.attempts); },
  }, t);
}

void run(const char* title, std::initializer_list<Event> events) {
  std::printf("%s\n", title);
  Task t = Connecting{1, 0};
  std::printf("  "); show(t);
  for (const Event& e : events)
    if (auto n = next(t, e)) { t = *n; std::printf("  "); show(t); }
}

int main() {
  run("断了一次，重试成功：", {Connected{}, Data{4096}, NetError{}, Timer{}, Connected{}, Data{8192}, Finished{}});
  run("一直连不上：", {NetError{}, Timer{}, NetError{}, Timer{}, NetError{}, Timer{}});
  std::printf("sizeof(Task) = %zu\n", sizeof(Task));
}
```

输出：

```
断了一次，重试成功：
  连接中（第 1 次，从 0 字节处开始）
  下载中（第 1 次，0 字节）
  下载中（第 1 次，4096 字节）
  等待 500 毫秒后重试
  连接中（第 2 次，从 4096 字节处开始）
  下载中（第 2 次，4096 字节）
  下载中（第 2 次，12288 字节）
  完成，共 12288 字节
一直连不上：
  连接中（第 1 次，从 0 字节处开始）
  等待 500 毫秒后重试
  连接中（第 2 次，从 0 字节处开始）
  等待 1000 毫秒后重试
  连接中（第 3 次，从 0 字节处开始）
  失败，试了 3 次
sizeof(Task) = 24
```

「重试还是失败」是一个带条件的转换，写成普通函数 `retryOrFail()`，两个出错的转换共用。如果用表驱动，这个条件就放不进表里（第 3.5 节）。

**4.** `retries_` 是 `Buffering` 的成员变量，但 `Buffering` 对象是共享的（`buffering()` 返回同一个静态对象），两个播放器用的是**同一个计数器**。B 进入缓冲中时，`enter()` 把计数清零，A 的计数也被清零了，所以 A 又多试了一次；之后 A 超时一次、计数变成 1，B 再超时，计数变成 2，B 就以为自己已经重试过一次了。

规则是：**带成员变量的状态对象不能共享**。改法有两种：把重试次数放进 `Player`（每个播放器自己的数据），状态对象仍然共享；或者改成每次进入缓冲中都新建一个 `Buffering` 对象。第一种改动最小：

```cpp
// 《状态模式》练习 4 的改法：重试次数是每个播放器自己的数据，放进 Player；状态对象仍然没有成员、可以共享
#include <cstdio>

class Player;
class State {
 public:
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
  int retries = 0;   // 原来在 Buffering 里，所有播放器共用了一份
  void onTimeout() { state_->timeout(*this); }
  void transitionTo(State& next) {
    std::printf("  [%s] %s → %s\n", name, state_->name(), next.name());
    state_ = &next;
    state_->enter(*this);
  }
  void startBuffering() { transitionTo(buffering()); }

 private:
  State* state_ = &idle();
};

class Idle : public State {
 public:
  const char* name() const override { return "空闲"; }
};
State& idle() { static Idle s; return s; }

class Buffering : public State {   // 缓冲超时就重试，最多 3 次
 public:
  const char* name() const override { return "缓冲中"; }
  void enter(Player& p) override { p.retries = 0; }
  void timeout(Player& p) override {
    if (++p.retries >= 3) p.transitionTo(error());
    else std::printf("  [%s] 超时，第 %d 次重试\n", p.name, p.retries);
  }
};
State& buffering() { static Buffering s; return s; }

class Error : public State {
 public:
  const char* name() const override { return "出错"; }
};
State& error() { static Error s; return s; }

int main() {
  Player a("A"), b("B");
  a.startBuffering();
  a.onTimeout(); a.onTimeout();
  b.startBuffering();
  a.onTimeout();
  b.onTimeout();
  b.onTimeout();
}
```

输出：

```
  [A] 空闲 → 缓冲中
  [A] 超时，第 1 次重试
  [A] 超时，第 2 次重试
  [B] 空闲 → 缓冲中
  [A] 缓冲中 → 出错
  [B] 超时，第 1 次重试
  [B] 超时，第 2 次重试
```

A 第 3 次超时进入出错，B 的计数从 1 开始，互不影响。

</details>
