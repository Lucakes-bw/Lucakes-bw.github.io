---
title: 观察者模式
date: 2026-11-11 21:00:00
updated: 2026-11-11 21:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 观察者
  - 信号槽
  - weak_ptr
  - RAII
  - 事件总线
order: 2
description: 播放器的进度一变，进度条、字幕、歌词、统计上报都要跟着动。硬编码版让 Player 挨个调用每个模块：加一个弹幕要改 3 个已有文件，Player 自己增删 12 行。观察者模式让 Player 只认识一个接口，加弹幕时 Player 一行不改。C++ 里真正难的是生命周期：观察者析构了没退订、通知途中有人退订或订阅，用 ASan 都能抓到 heap-use-after-free 或 container-overflow（GCC 要加宏才报）；回调里再改状态会重入，按钮最后显示「播放」而播放器已经暂停。解决办法是 weak_ptr、RAII 的订阅句柄 Connection、延迟删除和排队通知。实测 8 个观察者每次通知：虚函数 5.07 ns，std::function 8.69 ns，带 shared_ptr 的 Signal 14.06 ns，硬编码 0.80 ns。多线程下锁住整个通知，2 个线程反而比 1 个慢一倍，写时复制才能并行。最后讲推模型和拉模型，以及观察者和事件总线、中介者、Qt 信号槽的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-observer]
---

> **本文目标**：理解观察者模式：被观察者只认识一个观察者接口，状态变化时逐个通知，订阅者自己决定订不订；会写经典的 `attach / detach / notify`，也会写 C++ 里更常用的「信号 + 订阅句柄」；用 ASan 复现并解决 C++ 特有的几个坑：悬空的观察者、通知途中增删观察者、通知顺序、重入；知道推模型和拉模型怎么选；量出几种写法的通知开销，以及多线程下加锁的代价；分清观察者、发布-订阅（事件总线）、中介者。
>
> **前置知识**：上一篇《策略模式》；《SOLID 原则（下）：里氏替换、接口隔离与依赖倒置》（观察者是依赖倒置的典型用法）；C/C++ 专栏的《智能指针：unique_ptr、shared_ptr 与 weak_ptr》；Qt 专栏的《信号与槽》可以对照着看。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/19-observer/` 目录，`sh measure.sh` 用 `-O2` 跑一遍本文的实测，并用 AddressSanitizer 跑几个有 bug 的版本；多文件的依赖对比在 `deps/` 子目录（`sh deps/measure_deps.sh`）。

## 1. 问题：进度一变，一堆模块都要跟着动

### 1.1 硬编码版：播放器自己挨个调用

播放器播放 `bbb-10s.mp4`（10 秒）的时候，播放位置每变一次，屏幕上好几个地方都要更新：

- **进度条**：显示「5.0 / 10.0 s」，滑块挪到一半；
- **字幕**：找到 5.0 秒该显示的那一句；
- **歌词**（音乐模式）：高亮当前唱到的那一行；
- **统计上报**：记下看了多久，播放、暂停时上报给服务器。

最直接的写法是让 `Player` 持有这几个模块的引用，播放位置变了就挨个调用：

```cpp
// 《观察者模式》：硬编码版——播放器自己挨个调用进度条、字幕、歌词、统计上报
#include <cstdio>

enum class PlayState { Stopped, Playing, Paused };
const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }

class ProgressBar {
public:
  void update(double pos, double dur) { std::printf("  [进度条] %4.1f / %.1f s\n", pos, dur); }
};
class SubtitleView {
public:
  void seek(double pos) { std::printf("  [字幕]   找 %.1f s 的字幕\n", pos); }
};
class LyricsView {
public:
  void highlight(double pos) { std::printf("  [歌词]   高亮 %.1f s 那一句\n", pos); }
};
class StatsReporter {
public:
  void onTick(double) { ++ticks_; }
  void onState(PlayState s) { std::printf("  [统计]   上报：%s（已收到 %d 次进度）\n", name(s), ticks_); }
private:
  int ticks_ = 0;
};

class Player {
public:
  Player(ProgressBar& bar, SubtitleView& sub, LyricsView& lyrics, StatsReporter& stats)
      : bar_(bar), sub_(sub), lyrics_(lyrics), stats_(stats) {}

  void tick(double pos) {          // 播放推进到 pos 秒：要更新的模块全写死在这里
    bar_.update(pos, dur_);
    sub_.seek(pos);
    lyrics_.highlight(pos);
    stats_.onTick(pos);
  }
  void setState(PlayState s) {
    state_ = s;
    stats_.onState(s);
  }

private:
  ProgressBar& bar_;
  SubtitleView& sub_;
  LyricsView& lyrics_;
  StatsReporter& stats_;
  double dur_ = 10.0;              // bbb-10s.mp4 时长 10 秒
  PlayState state_ = PlayState::Stopped;
};

int main() {
  ProgressBar bar;
  SubtitleView sub;
  LyricsView lyrics;
  StatsReporter stats;
  Player player(bar, sub, lyrics, stats);
  player.setState(PlayState::Playing);
  for (double t = 0; t <= 10; t += 5) player.tick(t);
  player.setState(PlayState::Paused);
}
```

```text
  [统计]   上报：播放（已收到 0 次进度）
  [进度条]  0.0 / 10.0 s
  [字幕]   找 0.0 s 的字幕
  [歌词]   高亮 0.0 s 那一句
  [进度条]  5.0 / 10.0 s
  [字幕]   找 5.0 s 的字幕
  [歌词]   高亮 5.0 s 那一句
  [进度条] 10.0 / 10.0 s
  [字幕]   找 10.0 s 的字幕
  [歌词]   高亮 10.0 s 那一句
  [统计]   上报：暂停（已收到 3 次进度）
```

能跑，输出也对。问题出在下一个需求上。

### 1.2 加一个弹幕要改几处

产品说要加弹幕：弹幕也要跟着播放进度滚动。真实项目里每个模块都有自己的 `.h` 和 `.cpp`，`deps/coupled/` 就是这样拆开的版本（7 个 `.cpp`），`deps/coupled_plus/` 是加了 `DanmakuView` 以后的样子。用 `diff` 比一下两个目录：

<!-- run: off -->
```diff
--- coupled/player.h
+++ coupled_plus/player.h
 class StatsReporter;
+class DanmakuView;

 class Player {
 public:
-  Player(ProgressBar& bar, SubtitleView& sub, LyricsView& lyrics, StatsReporter& stats);
+  Player(ProgressBar& bar, SubtitleView& sub, LyricsView& lyrics, StatsReporter& stats,
+         DanmakuView& danmaku);
 ...
   StatsReporter& stats_;
+  DanmakuView& danmaku_;

--- coupled/player.cpp
+++ coupled_plus/player.cpp
 #include "stats_reporter.h"
+#include "danmaku_view.h"

-Player::Player(ProgressBar& bar, SubtitleView& sub, LyricsView& lyrics, StatsReporter& stats)
-    : bar_(bar), sub_(sub), lyrics_(lyrics), stats_(stats) {}
+Player::Player(ProgressBar& bar, SubtitleView& sub, LyricsView& lyrics, StatsReporter& stats,
+               DanmakuView& danmaku)
+    : bar_(bar), sub_(sub), lyrics_(lyrics), stats_(stats), danmaku_(danmaku) {}
 ...
   stats_.onTick(pos);
+  danmaku_.scrollTo(pos);
```

`deps/measure_deps.sh` 把这些数一遍（Clang 和 GCC 结果一样）：

```text
== coupled
加弹幕：改动已有文件 3 个（main.cpp player.cpp player.h ），新增文件 2 个，已有文件里增删 16 行
其中 Player 的两个文件增删 12 行
player.cpp 依赖的模块头文件：4 个，加弹幕后 5 个
  改 danmaku_view.h 要重编：danmaku_view.cpp
  改 danmaku_view.h 要重编：main.cpp
  改 danmaku_view.h 要重编：player.cpp
改 danmaku_view.h：7 个 .cpp 里要重编 3 个
```

### 1.3 问题在哪

- **Player 依赖每一个具体模块**。播放器是最核心、最稳定的类，进度条、弹幕这些界面模块却天天在变。现在依赖方向反了：界面改个函数名，播放器就要跟着改、跟着重编。
- **加模块要改 Player**。加弹幕，Player 的构造函数、成员、`tick()`、`#include` 四个地方都动了，违反开闭原则。去掉歌词也一样要改它。
- **没法在运行时增减**。用户在设置里关掉歌词面板、打开字幕面板，`Player` 的构造函数参数是固定的，表达不了「现在有谁在听」。
- **Player 被迫知道每个模块的接口**：进度条叫 `update`，字幕叫 `seek`，歌词叫 `highlight`，统计又要两个函数。

我们想要的是：Player 只管说「我的进度变成 5.0 秒了」，至于谁在听、听到以后干什么，它不关心。

## 2. 观察者模式

### 2.1 意图

GoF 的定义：

> 定义对象间的一种一对多的依赖关系，当一个对象的状态发生改变时，所有依赖于它的对象都得到通知并被自动更新。

大白话：**被观察的对象（Subject）手里有一张名单，名单上的人都实现了同一个接口；它一有变化，就照着名单挨个打电话。想听的人自己把名字写上去（attach），不想听了自己划掉（detach）。** Subject 只认识「名单上的人都有 `update()`」这件事，不认识具体是谁。

它在很多地方有别的名字：GUI 里的事件监听器（listener）、Qt 的信号与槽、JavaScript 的 `addEventListener`、MVC 里模型通知视图、SDL 的 `SDL_AddEventWatch`（可以挂多个事件监视回调），本质上都是观察者。

### 2.2 用观察者模式重构

先定义一个观察者接口 `PlayerObserver`，`Player` 只存一个 `vector<PlayerObserver*>`：

```cpp
// 《观察者模式》：Player 只认识 PlayerObserver 接口，各个模块自己订阅、自己取消
#include <algorithm>
#include <cstdio>
#include <vector>

enum class PlayState { Stopped, Playing, Paused };
const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }

class PlayerObserver {                       // 观察者接口：想知道播放器变化的模块都实现它
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double /*pos*/, double /*dur*/) {}
  virtual void onStateChanged(PlayState) {}
};

class Player {                               // 被观察者（Subject）
public:
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  void detach(PlayerObserver* o) {
    auto it = std::find(observers_.begin(), observers_.end(), o);
    if (it != observers_.end()) observers_.erase(it);
  }
  void tick(double pos) {
    for (auto* o : observers_) o->onProgress(pos, dur_);
  }
  void setState(PlayState s) {
    state_ = s;
    for (auto* o : observers_) o->onStateChanged(s);
  }

private:
  std::vector<PlayerObserver*> observers_;
  double dur_ = 10.0;
  PlayState state_ = PlayState::Stopped;
};

class ProgressBar : public PlayerObserver {
public:
  void onProgress(double pos, double dur) override { std::printf("  [进度条] %4.1f / %.1f s\n", pos, dur); }
};
class SubtitleView : public PlayerObserver {
public:
  void onProgress(double pos, double) override { std::printf("  [字幕]   找 %.1f s 的字幕\n", pos); }
};
class LyricsView : public PlayerObserver {
public:
  void onProgress(double pos, double) override { std::printf("  [歌词]   高亮 %.1f s 那一句\n", pos); }
};
class StatsReporter : public PlayerObserver {
public:
  void onProgress(double, double) override { ++ticks_; }
  void onStateChanged(PlayState s) override { std::printf("  [统计]   上报：%s（已收到 %d 次进度）\n", name(s), ticks_); }
private:
  int ticks_ = 0;
};

int main() {
  ProgressBar bar;
  SubtitleView sub;
  LyricsView lyrics;
  StatsReporter stats;
  Player player;
  player.attach(&stats);
  player.attach(&bar);
  player.attach(&sub);
  player.attach(&lyrics);

  player.setState(PlayState::Playing);
  player.tick(0);
  std::puts("-- 切到视频模式，歌词面板取消订阅");
  player.detach(&lyrics);
  player.tick(5);
  player.setState(PlayState::Paused);
}
```

```text
  [统计]   上报：播放（已收到 0 次进度）
  [进度条]  0.0 / 10.0 s
  [字幕]   找 0.0 s 的字幕
  [歌词]   高亮 0.0 s 那一句
-- 切到视频模式，歌词面板取消订阅
  [进度条]  5.0 / 10.0 s
  [字幕]   找 5.0 s 的字幕
  [统计]   上报：暂停（已收到 2 次进度）
```

几处值得注意：

- 接口的两个函数给了**空的默认实现**：字幕只关心进度、不关心播放状态，就不用写一个空的 `onStateChanged`。如果接口里事件很多，这能省不少样板代码；代价是忘了 override 也不会报错（所以要写 `override`，拼错函数名时编译器才会提醒）。
- `detach` 先 `find` 再 `erase`，找不到时什么也不做。直接 `erase(find(...))` 在找不到时会 `erase(end())`，这是未定义行为。
- 第二次 `tick(5)` 时歌词已经退订，`Player` 的代码一行没变。这就是「运行时增减」。

### 2.3 类图和时序图

```uml
label: 观察者模式：Player 聚合任意多个 PlayerObserver；进度条、字幕、歌词、统计上报都实现 PlayerObserver；Player 不依赖任何具体模块
classes:
  - { id: Player, x: 20, y: 20, w: 220, attrs: ['- observers_ : vector<PlayerObserver*>'], ops: ['+ attach(o)', '+ detach(o)', '+ tick(pos)', '+ setState(s)'] }
  - { id: PlayerObserver, stereo: interface, x: 440, y: 42.5, w: 230, ops: ['+ onProgress(pos, dur)', '+ onStateChanged(s)'] }
  - { id: ProgressBar, x: 20, y: 250, w: 150, ops: ['+ onProgress(pos, dur)'] }
  - { id: SubtitleView, x: 215, y: 250, w: 150, ops: ['+ onProgress(pos, dur)'] }
  - { id: LyricsView, x: 410, y: 250, w: 150, ops: ['+ onProgress(pos, dur)'] }
  - { id: StatsReporter, x: 605, y: 250, w: 170, attrs: ['- ticks_ : int'], ops: ['+ onProgress(pos, dur)', '+ onStateChanged(s)'] }
rels:
  - { from: Player, to: PlayerObserver, type: aggregate, tm: '*', label: '通知' }
  - { from: ProgressBar, to: PlayerObserver, type: realize, points: [[109.5, 250], [109.5, 190], [465, 190], [465, 128.5]] }
  - { from: SubtitleView, to: PlayerObserver, type: realize, mid: 190, tdx: -30 }
  - { from: LyricsView, to: PlayerObserver, type: realize, mid: 190, tdx: 30 }
  - { from: StatsReporter, to: PlayerObserver, type: realize, mid: 190, tdx: 90 }
notes:
  - { x: 20, y: 380, w: 300, text: ['Player::tick(pos) {', '  for (auto* o : observers_)', '    o->onProgress(pos, dur_);', '}'] }
```

和类图对照着看：

- **Subject（`Player`）**：持有观察者名单，提供 `attach / detach`，状态变化时 `notify`（这里是 `tick` 和 `setState` 里的循环）。
- **Observer（`PlayerObserver`）**：观察者接口，Subject 只依赖它。
- **ConcreteObserver（`ProgressBar` 等）**：真正干活的模块，各自决定收到通知后做什么。

一次 `tick(5.0)` 的时序：

```uml
label: 时序图：解码线程调用 Player 的 tick(5.0)，Player 在一个循环里依次调用进度条、字幕、统计的 onProgress，每个返回后再调用下一个，最后 tick 返回
participants:
  - { id: d, name: '解码循环', x: 10, w: 100 }
  - { id: p, name: 'Player', x: 170, w: 100 }
  - { id: b, name: 'ProgressBar', x: 340, w: 120 }
  - { id: s, name: 'SubtitleView', x: 510, w: 120 }
  - { id: r, name: 'StatsReporter', x: 680, w: 130 }
messages:
  - { from: d, to: p, text: 'tick(5.0)', n: 1 }
  - { from: p, to: b, text: 'onProgress(5.0, 10.0)' }
  - { from: b, to: p, text: '', type: reply }
  - { from: p, to: s, text: 'onProgress(5.0, 10.0)' }
  - { from: s, to: p, text: '', type: reply }
  - { from: p, to: r, text: 'onProgress(5.0, 10.0)' }
  - { from: r, to: p, text: '', type: reply }
  - { from: p, to: d, text: '', type: reply }
frags:
  - { from: 1, to: 6, label: 'loop [observers_ 里的每一个]' }
```

这张图里藏着这一篇后面所有的坑：

1. 通知是**同步**的：`tick` 要等所有观察者都处理完才返回。哪个观察者慢（比如统计上报去发网络请求），解码循环就被拖慢。
2. 循环在**遍历 Subject 自己的名单**。如果某个观察者在 `onProgress` 里 attach / detach，就是边遍历边修改容器（第 3.2、3.3 节）。
3. 观察者在 `onProgress` 里可以**反过来调用 Player**，引发嵌套的通知（第 3.5 节的重入）。
4. 名单里存的是裸指针。观察者被销毁时如果没把自己划掉，名单上就留下一个悬空指针（第 3.1 节）。

### 2.4 实测：解耦换来了什么

同样加一个弹幕，观察者版（`deps/observer/` → `deps/observer_plus/`）的改动只有 `main.cpp` 的 3 行，外加新写的 `danmaku_view.h/.cpp`：

<!-- run: off -->
```diff
 #include "stats_reporter.h"
+#include "danmaku_view.h"
 ...
   StatsReporter stats;
+  DanmakuView danmaku;
 ...
   player.attach(&lyrics);
+  player.attach(&danmaku);
```

```text
== observer
加弹幕：改动已有文件 1 个（main.cpp ），新增文件 2 个，已有文件里增删 3 行
其中 Player 的两个文件增删 0 行
player.cpp 依赖的模块头文件：0 个，加弹幕后 0 个
  改 danmaku_view.h 要重编：danmaku_view.cpp
  改 danmaku_view.h 要重编：main.cpp
改 danmaku_view.h：7 个 .cpp 里要重编 2 个
```

<figure class="diagram">
<svg viewBox="0 0 820 126" role="img" aria-label="加一个弹幕模块的改动量：硬编码版改动已有文件 3 个、Player 增删 12 行、改弹幕头文件要重编 3 个 .cpp；观察者版分别是 1 个、0 行、2 个"><g style="font-family:var(--mono)"><text x="98.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">改动已有文件（个）</text><rect x="98.0" y="30.0" width="150.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="254.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3</text><rect x="98.0" y="60.0" width="50.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="154.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1</text><text x="338.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Player 的两个文件增删（行）</text><rect x="338.0" y="30.0" width="150.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="494.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">12</text><rect x="338.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="345.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0</text><text x="578.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">改 danmaku_view.h 要重编（个 .cpp）</text><rect x="578.0" y="30.0" width="150.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="734.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3</text><rect x="578.0" y="60.0" width="100.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="684.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2</text><text x="90.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">硬编码版</text><text x="90.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">观察者版</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">deps/measure_deps.sh：diff 两个版本加弹幕前后的目录，c++ -MM 查头文件依赖。Clang 和 GCC 结果相同</text></g></svg>
</figure>

依赖方向的变化画出来是这样：

<figure class="diagram">
<svg viewBox="0 0 730 300" role="img" aria-label="两张依赖图。左边硬编码版：Player 直接指向进度条、字幕、歌词、统计上报，新加的弹幕也要从 Player 连一条线过去。右边观察者版：Player 只指向 PlayerObserver 接口，五个模块都实现这个接口，箭头从模块指向接口，新加弹幕不碰 Player"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--warn);font-size:12.5px;font-weight:700">硬编码版：Player 认识每一个模块</text><text x="390.0" y="20.0" text-anchor="start" style="fill:var(--accent);font-size:12.5px;font-weight:700">观察者版：Player 只认识一个接口</text><rect x="20" y="120" width="90" height="36" rx="5" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.4"/><text x="65.0" y="142.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Player</text><rect x="200" y="40" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="265.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">ProgressBar</text><line x1="110.0" y1="138.0" x2="191.4" y2="62.1" style="stroke:var(--warn);stroke-width:1.4"/><path d="M198.0,56.0 L188.0,58.5 L194.8,65.8 z" style="fill:var(--warn);stroke:var(--warn);stroke-width:1.2"/><rect x="200" y="84" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="265.0" y="104.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">SubtitleView</text><line x1="110.0" y1="138.0" x2="189.7" y2="103.6" style="stroke:var(--warn);stroke-width:1.4"/><path d="M198.0,100.0 L187.8,99.0 L191.7,108.2 z" style="fill:var(--warn);stroke:var(--warn);stroke-width:1.2"/><rect x="200" y="128" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="265.0" y="148.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">LyricsView</text><line x1="110.0" y1="138.0" x2="189.0" y2="143.4" style="stroke:var(--warn);stroke-width:1.4"/><path d="M198.0,144.0 L189.4,138.4 L188.7,148.4 z" style="fill:var(--warn);stroke:var(--warn);stroke-width:1.2"/><rect x="200" y="172" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="265.0" y="192.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">StatsReporter</text><line x1="110.0" y1="138.0" x2="190.2" y2="183.6" style="stroke:var(--warn);stroke-width:1.4"/><path d="M198.0,188.0 L192.6,179.2 L187.7,187.9 z" style="fill:var(--warn);stroke:var(--warn);stroke-width:1.2"/><rect x="200" y="216" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.4"/><text x="265.0" y="236.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px">DanmakuView</text><line x1="110.0" y1="138.0" x2="191.8" y2="225.4" style="stroke:var(--danger);stroke-width:1.4"/><path d="M198.0,232.0 L195.5,222.0 L188.2,228.8 z" style="fill:var(--danger);stroke:var(--danger);stroke-width:1.2"/><text x="200.0" y="275.0" text-anchor="start" style="fill:var(--danger);font-size:10.5px">加弹幕：Player 的构造函数、成员、</text><text x="200.0" y="290.0" text-anchor="start" style="fill:var(--danger);font-size:10.5px">tick() 和 #include 都要改</text><rect x="390" y="40" width="90" height="36" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="435.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Player</text><rect x="540" y="36" width="170" height="44" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="625.0" y="52.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">«interface»</text><text x="625.0" y="70.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">PlayerObserver</text><line x1="480.0" y1="58.0" x2="529.0" y2="58.0" style="stroke:var(--accent);stroke-width:1.4"/><path d="M538.0,58.0 L529.0,53.0 L529.0,63.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><text x="509.0" y="50.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">*</text><rect x="390" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="420.0" y="219.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Progress</text><line x1="420.0" y1="190.0" x2="552.9" y2="86.5" style="stroke:var(--muted);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M560.0,81.0 L549.8,82.6 L556.0,90.5 z" style="fill:var(--bg);stroke:var(--muted);stroke-width:1.2"/><rect x="456" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="486.0" y="219.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Subtitle</text><line x1="486.0" y1="190.0" x2="586.7" y2="87.4" style="stroke:var(--muted);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M593.0,81.0 L583.1,83.9 L590.3,90.9 z" style="fill:var(--bg);stroke:var(--muted);stroke-width:1.2"/><rect x="522" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="552.0" y="219.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Lyrics</text><line x1="552.0" y1="190.0" x2="620.9" y2="88.4" style="stroke:var(--muted);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M626.0,81.0 L616.8,85.6 L625.1,91.3 z" style="fill:var(--bg);stroke:var(--muted);stroke-width:1.2"/><rect x="588" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="618.0" y="219.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Stats</text><line x1="618.0" y1="190.0" x2="655.8" y2="89.4" style="stroke:var(--muted);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M659.0,81.0 L651.2,87.7 L660.5,91.2 z" style="fill:var(--bg);stroke:var(--muted);stroke-width:1.2"/><rect x="654" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="684.0" y="219.0" text-anchor="middle" style="fill:var(--accent);font-size:11.5px">Danmaku</text><line x1="684.0" y1="190.0" x2="691.3" y2="90.0" style="stroke:var(--accent);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M692.0,81.0 L686.4,89.6 L696.3,90.3 z" style="fill:var(--bg);stroke:var(--accent);stroke-width:1.2"/><text x="390.0" y="275.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">加弹幕：新写一个类实现接口，</text><text x="390.0" y="290.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">main() 里 attach 一下；Player 不动</text></g></svg>
</figure>

左边是 Player 指向每个模块；右边 Player 只指向接口，模块的箭头反过来指向接口——这就是《SOLID 原则（下）》里说的**依赖倒置**：高层的 Player 和低层的界面模块都依赖抽象。还要注意，`main.cpp` 照样要认识所有模块，「组装」这件事没有消失，只是从 Player 挪到了组装代码里，Player 自己干净了。

省下的不是免费的。观察者版的通知多了一层虚函数调用（第 4.3 节实测：8 个观察者每次通知 5.07 ns 对 0.80 ns），而且「谁会被调用」从代码里看不出来了：硬编码版读 `tick()` 就知道会调用哪几个模块，观察者版要到运行时才知道名单上有谁。调试时只能下断点看 `observers_`。

## 3. C++ 里的坑

在 Java、C# 里，观察者模式最多忘了退订导致内存泄漏。在 C++ 里，观察者模式是悬空指针和迭代器失效的高发区。下面每个坑都有能复现的程序，用 AddressSanitizer（`-fsanitize=address`）能抓到。

### 3.1 观察者先析构了，没有退订

用户打开字幕面板，面板订阅了进度；关掉面板，面板对象析构了，可是没人调用 `detach`：

<!-- run: off -->
```cpp
// 《观察者模式》：字幕面板关掉（析构）时忘了 detach，Player 手里留下悬空指针；用 -fsanitize=address 编译能抓到
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double pos) = 0;
};

class Player {
public:
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  void detach(PlayerObserver* o) {
    auto it = std::find(observers_.begin(), observers_.end(), o);
    if (it != observers_.end()) observers_.erase(it);
  }
  void tick(double pos) {
    for (auto* o : observers_) o->onProgress(pos);
  }
private:
  std::vector<PlayerObserver*> observers_;
};

class ProgressBar : public PlayerObserver {
public:
  void onProgress(double pos) override { std::printf("  [进度条] %.1f s\n", pos); }
};

class SubtitlePanel : public PlayerObserver {
public:
  void onProgress(double pos) override { std::printf("  [字幕]   %.1f s：%s\n", pos, line_.c_str()); }
private:
  std::string line_ = "Big Buck Bunny wakes up in the meadow.";
};

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);           // 不缓冲：崩溃前的输出也能看到
  Player player;
  ProgressBar bar;
  player.attach(&bar);
  {
    auto panel = std::make_unique<SubtitlePanel>();   // 用户打开字幕面板
    player.attach(panel.get());
    player.tick(1.0);
  }                                                   // 用户关掉面板：panel 析构了，没有 detach
  std::puts("-- 字幕面板已经关掉");
  player.tick(2.0);                                   // 对已经释放的对象调用虚函数：未定义行为
}
```

`-O2` 不加 ASan 运行，在 macOS 和 Ubuntu 上都是段错误：

```text
  [进度条] 1.0 s
  [字幕]   1.0 s：Big Buck Bunny wakes up in the meadow.
-- 字幕面板已经关掉
  [进度条] 2.0 s
Segmentation fault                       （退出码 139，Clang 和 GCC 都一样）
```

加上 `-fsanitize=address`，ASan 直接指出是哪一行读了已经释放的内存，以及这块内存是在哪里释放的（Clang 的输出，删掉了标准库内部的栈帧）：

```text
==7214==ERROR: AddressSanitizer: heap-use-after-free on address 0x603000000fa0
READ of size 8 at 0x603000000fa0 thread T0
    #0 in Player::tick(double) dangling.cpp:22
    #1 in main dangling.cpp:51
0x603000000fa0 is located 0 bytes inside of 32-byte region [0x603000000fa0,0x603000000fc0)
freed by thread T0 here:
    #0 in operator delete(void*)
    #5 in main dangling.cpp:49
previously allocated by thread T0 here:
    #0 in operator new(unsigned long)
    #2 in main dangling.cpp:46
SUMMARY: AddressSanitizer: heap-use-after-free dangling.cpp:22 in Player::tick(double)
```

「READ of size 8」读的是对象开头的 8 个字节——**虚函数表指针**。`o->onProgress(pos)` 要先从对象里取出虚表指针，再从虚表里取函数地址；对象已经释放，这块内存可能被别的分配重用了，取出来的「函数地址」是什么都有可能。这次是段错误，换个时机就可能跳到一个真实的函数里，错得悄无声息。

最朴素的修法是**谁订阅、谁退订**，在析构函数里 detach：

<!-- run: off -->
```cpp
class SubtitlePanel : public PlayerObserver {
public:
  explicit SubtitlePanel(Player& p) : player_(p) { player_.attach(this); }
  ~SubtitlePanel() override { player_.detach(this); }
  // ...
private:
  Player& player_;          // 反过来：如果 Player 先析构，这里又悬空了
};
```

这把问题换成了另一个：观察者要记住 Subject，Subject 又不能比观察者先死。两边的生命周期互相牵制，第 4 节的 `weak_ptr` 和 `Connection` 就是来解决这个的。

### 3.2 通知途中有观察者退订自己

很常见的需求：「首帧统计」只关心第一次进度回调（算出从点击播放到出第一帧花了多久），上报之后就退订。它在 `onProgress` 里调用 `detach(this)`——而 `Player` 此刻正在用 range-for 遍历同一个 `vector`：

```cpp
// 《观察者模式》：通知过程中有观察者 detach 自己——range-for 的迭代器失效，后面的观察者被跳过或收到两次
#include <algorithm>
#include <cstdio>
#include <vector>

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double pos) = 0;
};

class Player {
public:
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  void detach(PlayerObserver* o) {
    auto it = std::find(observers_.begin(), observers_.end(), o);
    if (it != observers_.end()) observers_.erase(it);
  }
  void tick(double pos) {
    for (auto* o : observers_) o->onProgress(pos);   // 边遍历，回调里边 erase
  }
private:
  std::vector<PlayerObserver*> observers_;
};

class Logger : public PlayerObserver {
public:
  explicit Logger(const char* name) : name_(name) {}
  void onProgress(double pos) override { std::printf("  [%s] %.1f s\n", name_, pos); }
private:
  const char* name_;
};

class FirstFrameStats : public PlayerObserver {        // 只关心第一次进度：上报首帧耗时后就退订
public:
  explicit FirstFrameStats(Player& p) : player_(p) {}
  void onProgress(double pos) override {
    std::printf("  [首帧统计] %.1f s，上报后退订\n", pos);
    player_.detach(this);
  }
private:
  Player& player_;
};

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);           // 不缓冲：崩溃前的输出也能看到
  Player player;
  FirstFrameStats first(player);
  Logger bar("进度条"), sub("字幕"), lyrics("歌词");
  player.attach(&first);
  player.attach(&bar);
  player.attach(&sub);
  player.attach(&lyrics);
  std::puts("tick(0)：");
  player.tick(0);
  std::puts("tick(1)：");
  player.tick(1);
}
```

```text
tick(0)：
  [首帧统计] 0.0 s，上报后退订
  [字幕] 0.0 s
  [歌词] 0.0 s
  [歌词] 0.0 s
tick(1)：
  [进度条] 1.0 s
  [字幕] 1.0 s
  [歌词] 1.0 s
```

第一次通知里，**进度条被跳过了，歌词收到了两次**。Clang 和 GCC 的输出一样。原因画出来是这样：

<figure class="diagram">
<svg viewBox="0 0 640 276" role="img" aria-label="vector 里有首帧统计、进度条、字幕、歌词四个指针，迭代器指向第 0 个。首帧统计在回调里 erase 自己之后，后三个前移一格，迭代器 ++ 后指向第 1 个「字幕」，进度条被跳过；range-for 缓存的 end 仍在旧位置，第 3 格残留的歌词又被调用一次"><g style="font-family:var(--mono)"><text x="138.0" y="62.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px;font-weight:700">erase 之前</text><rect x="150" y="40" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--link);stroke-width:1.5"/><text x="193.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">首帧统计</text><text x="193.0" y="35.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[0]</text><rect x="242" y="40" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="285.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">进度条</text><text x="285.0" y="35.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[1]</text><rect x="334" y="40" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="377.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">字幕</text><text x="377.0" y="35.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[2]</text><rect x="426" y="40" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="469.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">歌词</text><text x="469.0" y="35.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[3]</text><text x="193.0" y="96.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">↑ it</text><text x="516.0" y="96.0" text-anchor="start" style="fill:var(--muted);font-size:11px">↑ 缓存的 end</text><text x="138.0" y="162.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px;font-weight:700">erase 之后</text><rect x="150" y="140" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="193.0" y="162.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">进度条</text><text x="193.0" y="135.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[0]</text><rect x="242" y="140" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="285.0" y="162.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">字幕</text><text x="285.0" y="135.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[1]</text><rect x="334" y="140" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="377.0" y="162.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">歌词</text><text x="377.0" y="135.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[2]</text><rect x="426" y="140" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.5;stroke-dasharray:4 3"/><text x="469.0" y="162.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px">歌词</text><text x="469.0" y="135.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[3]</text><text x="285.0" y="196.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">↑ ++it</text><text x="516.0" y="196.0" text-anchor="start" style="fill:var(--danger);font-size:11px">↑ 缓存的 end 没变</text><text x="469.0" y="214.0" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">size 之外的旧值</text><text x="150.0" y="244.0" text-anchor="start" style="fill:var(--fg-2);font-size:11px">后面的元素整体前移一格，it 回到循环里马上 ++，「进度条」被跳过；</text><text x="150.0" y="262.0" text-anchor="start" style="fill:var(--fg-2);font-size:11px">range-for 一开始就记下了 end，还会多走一格，读到 size 之外残留的「歌词」。</text></g></svg>
</figure>

`for (auto* o : observers_)` 展开后，`begin` 和 `end` 在循环开始前就各取一次。`erase` 把后面的元素往前挪一格，迭代器 `it` 原地不动，回到循环里一 `++` 就越过了挪上来的「进度条」；`end` 是循环开始时记下的旧位置，于是还会多走一格，读到 `size` 之外、`capacity` 之内残留的旧值「歌词」。按标准，`erase` 之后被删位置及其后面的迭代器全部失效，再用就是未定义行为，上面的输出只是这一次碰巧的样子。

下面的演示一步步走一遍这个过程，再看第 4.2 节的「延迟删除」是怎么避开它的：

<div class="dsviz" data-viz="dp-observer"></div>

ASan 能不能抓到这个 bug，Clang 和 GCC 不一样：

| | Clang / macOS（libc++） | GCC 13 / Ubuntu（libstdc++） |
|---|---|---|
| `-fsanitize=address` | `container-overflow`，指向 `tick` 那一行 | **什么也不报**，程序正常退出 |
| 再加 `-D_GLIBCXX_SANITIZE_VECTOR` | 同上 | `container-overflow` |

读 `size` 之外、`capacity` 之内的元素，内存本身是分配了的，普通的 ASan 检查不出来。要标准库主动告诉 ASan「这一段不能读」（container annotation）：libc++ 在 ASan 构建里默认开着，libstdc++ 要定义 `_GLIBCXX_SANITIZE_VECTOR` 才开。在 Linux 上用 ASan 查容器越界时，记得加上这个宏。

### 3.3 通知途中有观察者订阅新的

反过来，观察者在回调里 `attach` 一个新的：统计模块第一次收到进度时，顺手挂上一个「卡顿检测」：

<!-- run: off -->
```cpp
// 《观察者模式》：通知过程中有观察者 attach 新观察者——push_back 扩容，正在用的迭代器指向已经释放的内存
#include <cstdio>
#include <vector>

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double pos) = 0;
};

class Player {
public:
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  void tick(double pos) {
    for (auto* o : observers_) o->onProgress(pos);
  }
  std::size_t capacity() const { return observers_.capacity(); }
private:
  std::vector<PlayerObserver*> observers_;
};

class Logger : public PlayerObserver {
public:
  explicit Logger(const char* name) : name_(name) {}
  void onProgress(double pos) override { std::printf("  [%s] %.1f s\n", name_, pos); }
private:
  const char* name_;
};

class StallDetector : public PlayerObserver {          // 第一次收到进度时，顺手再挂一个「卡顿检测」
public:
  StallDetector(Player& p, PlayerObserver& extra) : player_(p), extra_(extra) {}
  void onProgress(double pos) override {
    std::printf("  [统计] %.1f s，开始监听卡顿\n", pos);
    if (!armed_) { armed_ = true; player_.attach(&extra_); }
  }
private:
  Player& player_;
  PlayerObserver& extra_;
  bool armed_ = false;
};

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);           // 不缓冲：崩溃前的输出也能看到
  Player player;
  Logger bar("进度条"), sub("字幕"), lyrics("歌词"), stall("卡顿检测");
  StallDetector stats(player, stall);
  player.attach(&stats);
  player.attach(&bar);
  player.attach(&sub);
  player.attach(&lyrics);
  std::printf("4 个观察者，capacity = %zu\n", player.capacity());
  player.tick(0);
  std::printf("capacity = %zu\n", player.capacity());
}
```

```text
4 个观察者，capacity = 4
  [统计] 0.0 s，开始监听卡顿
Segmentation fault                       （退出码 139，Clang 和 GCC 都一样）
```

名单里正好 4 个，`capacity` 也是 4，`push_back` 第 5 个时 `vector` 扩容：分配一块新内存、把指针搬过去、释放旧内存。外面 range-for 的迭代器还指着旧内存，下一次 `*it` 就是 heap-use-after-free，ASan 在两个平台上都报：

```text
SUMMARY: AddressSanitizer: heap-use-after-free iterate_attach.cpp:15 in Player::tick(double)
```

这个 bug 最阴险的地方是**它只在名单正好满的时候出现**。名单里有 3 个观察者时 `capacity` 是 4，`push_back` 不扩容，程序看起来一切正常；哪天多订阅了一个模块，就开始随机崩溃。

### 3.4 通知顺序

观察者模式不承诺通知的顺序。用 `vector` 时顺序是订阅的顺序；但有人为了防止重复订阅会改用 `std::set<PlayerObserver*>`，这时顺序就变成了**指针地址的大小**：

```cpp
// 《观察者模式》：通知顺序——用 std::set 存观察者（为了去重），顺序取决于对象的地址，换个创建顺序就变了
#include <cstdio>
#include <memory>
#include <set>
#include <vector>

class PlayerObserver {
public:
  explicit PlayerObserver(const char* name) : name_(name) {}
  virtual ~PlayerObserver() = default;
  void onProgress() { std::printf(" %s", name_); }
private:
  const char* name_;
};

void notifyAll(const std::set<PlayerObserver*>& observers) {
  for (auto* o : observers) o->onProgress();       // 按指针大小排序遍历
  std::puts("");
}

int main() {
  std::set<PlayerObserver*> a, b;
  // 第一次：按 进度条、字幕、统计 的顺序创建，按同样的顺序订阅
  auto bar1 = std::make_unique<PlayerObserver>("进度条");
  auto sub1 = std::make_unique<PlayerObserver>("字幕");
  auto stats1 = std::make_unique<PlayerObserver>("统计");
  a.insert(bar1.get()); a.insert(sub1.get()); a.insert(stats1.get());
  std::printf("创建顺序 进度条→字幕→统计，通知顺序：");
  notifyAll(a);

  // 第二次：订阅顺序不变，只是对象创建的先后变了
  auto stats2 = std::make_unique<PlayerObserver>("统计");
  auto sub2 = std::make_unique<PlayerObserver>("字幕");
  auto bar2 = std::make_unique<PlayerObserver>("进度条");
  b.insert(bar2.get()); b.insert(sub2.get()); b.insert(stats2.get());
  std::printf("创建顺序 统计→字幕→进度条，通知顺序：");
  notifyAll(b);
}
```

| | Clang / macOS | GCC 13 / Ubuntu |
|---|---|---|
| 创建顺序 进度条→字幕→统计 | 字幕 统计 进度条 | 进度条 字幕 统计 |
| 创建顺序 统计→字幕→进度条 | 统计 字幕 进度条 | 统计 字幕 进度条 |

订阅顺序两次完全一样，只是对象创建的先后变了，通知顺序就跟着变；同一份代码，macOS 的内存分配器和 glibc 给出的地址先后又不一样。如果统计模块要读「进度条当前显示的值」，它就隐式地依赖了「进度条先被通知」，这种依赖在测试环境里可能一直成立，换个平台就不成立了。

原则是：**观察者之间不应该互相依赖通知顺序**。真有先后要求时，要么显式地规定顺序（Qt 保证按 `connect` 的先后调用槽，Boost.Signals2 可以给槽分组排序），要么说明这几个模块之间有协作关系，应该由一个中介者来协调（第 5 节）。

### 3.5 重入：回调里又改了 Subject 的状态

付费片的「试看结束」逻辑：用户点播放，`TrialGuard` 收到「播放」通知，发现试看时间用完了，立刻调用 `player.setState(暂停)`。这一调用发生在外层 `setState(播放)` 的通知循环**里面**：

```cpp
// 《观察者模式》：重入——回调里又改了 Player 的状态，嵌套的通知先到，外层的旧通知后到，界面最后显示的状态是错的
#include <cstdio>
#include <deque>
#include <vector>

enum class PlayState { Stopped, Playing, Paused };
const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onStateChanged(PlayState s) = 0;
};

class Player {
public:
  explicit Player(bool queued) : queued_(queued) {}
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  PlayState state() const { return state_; }

  void setState(PlayState s) {
    state_ = s;
    if (!queued_) {                                // 直接通知：回调里再 setState 会嵌套通知
      for (auto* o : observers_) o->onStateChanged(s);
      return;
    }
    pending_.push_back(s);                         // 排队通知：正在通知时只入队，由最外层按顺序发
    if (notifying_) return;
    notifying_ = true;
    while (!pending_.empty()) {
      PlayState e = pending_.front();
      pending_.pop_front();
      for (auto* o : observers_) o->onStateChanged(e);
    }
    notifying_ = false;
  }

private:
  bool queued_;
  bool notifying_ = false;
  std::deque<PlayState> pending_;
  std::vector<PlayerObserver*> observers_;
  PlayState state_ = PlayState::Stopped;
};

class TrialGuard : public PlayerObserver {          // 试看结束：一开始播放就暂停，弹出开通会员
public:
  explicit TrialGuard(Player& p) : player_(p) {}
  void onStateChanged(PlayState s) override {
    std::printf("  [试看] 收到「%s」\n", name(s));
    if (s == PlayState::Playing) player_.setState(PlayState::Paused);   // 回调里改状态：重入
  }
private:
  Player& player_;
};

class PlayButton : public PlayerObserver {          // 界面上的播放 / 暂停按钮
public:
  void onStateChanged(PlayState s) override {
    shown_ = s;
    std::printf("  [按钮] 显示「%s」\n", name(s));
  }
  PlayState shown_ = PlayState::Stopped;
};

void run(bool queued) {
  std::printf("%s：\n", queued ? "排队通知" : "直接通知");
  Player player(queued);
  TrialGuard trial(player);
  PlayButton button;
  player.attach(&trial);
  player.attach(&button);
  player.setState(PlayState::Playing);
  std::printf("  结果：播放器是「%s」，按钮显示「%s」%s\n\n", name(player.state()), name(button.shown_),
              player.state() == button.shown_ ? "" : "  ← 不一致");
}

int main() {
  run(false);
  run(true);
}
```

```text
直接通知：
  [试看] 收到「播放」
  [试看] 收到「暂停」
  [按钮] 显示「暂停」
  [按钮] 显示「播放」
  结果：播放器是「暂停」，按钮显示「播放」  ← 不一致

排队通知：
  [试看] 收到「播放」
  [按钮] 显示「播放」
  [试看] 收到「暂停」
  [按钮] 显示「暂停」
  结果：播放器是「暂停」，按钮显示「暂停」
```

直接通知时，按钮**先收到「暂停」、后收到「播放」**，最后停在「播放」，而播放器实际是暂停的：

```uml
label: 时序图：main 调用 setState(播放)；Player 通知 TrialGuard；TrialGuard 在回调里调用 setState(暂停)，嵌套的通知先把「暂停」发给 TrialGuard 和按钮；嵌套返回后，外层循环继续，把旧的「播放」发给按钮，按钮最后显示播放
participants:
  - { id: m, name: 'main', x: 10, w: 80 }
  - { id: p, name: 'Player', x: 170, w: 100 }
  - { id: t, name: 'TrialGuard', x: 360, w: 120 }
  - { id: b, name: 'PlayButton', x: 570, w: 120 }
messages:
  - { from: m, to: p, text: 'setState(播放)', n: 1 }
  - { from: p, to: t, text: 'onStateChanged(播放)' }
  - { from: t, to: p, text: 'setState(暂停)', n: 2, note: '回调里改状态：重入' }
  - { from: p, to: t, text: 'onStateChanged(暂停)' }
  - { from: t, to: p, text: '', type: reply }
  - { from: p, to: b, text: 'onStateChanged(暂停)' }
  - { from: b, to: p, text: '', type: reply }
  - { from: p, to: t, text: '', type: reply }
  - { from: t, to: p, text: '', type: reply }
  - { from: p, to: b, text: 'onStateChanged(播放)', note: '旧通知后到，按钮停在「播放」', hl: warn }
  - { from: b, to: p, text: '', type: reply }
  - { from: p, to: m, text: '', type: reply }
frags:
  - { from: 2, to: 7, label: '嵌套的 setState(暂停)' }
```

这和第 3.2 节的迭代器失效不一样：程序没有任何未定义行为，ASan 也不会报，每一行代码单看都没错，错的是**事件到达的顺序**。在 GUI 和播放器里很常见：A 的回调改了状态，触发 B 的回调，B 又改了状态……

排队通知（`queued = true` 那条路径）的做法是：如果已经在通知中，新的事件只入队，不立刻发；最外层的循环发完当前事件，再按顺序发队列里的下一个。这样每个观察者收到的事件顺序和状态真实变化的顺序一致。代价是嵌套的 `setState` 返回时，观察者们**还没收到**这个变化，调用方不能假设「我改了状态，界面就已经更新了」。

很多框架把这个做成「投递到事件循环」：Qt 的 `Qt::QueuedConnection`、浏览器的 `queueMicrotask`，都是把通知推迟到当前回调结束以后。

### 3.6 推模型和拉模型

上面的 `onProgress(pos, dur)` 把数据随通知一起**推**给观察者。另一种做法是只告诉观察者「哪方面变了」，观察者需要什么自己回来**拉**：

```cpp
// 《观察者模式》：推模型把数据随通知一起发出去；拉模型只说「哪方面变了」，观察者自己回来取需要的数据
#include <cstdio>
#include <vector>

// ---------- 推模型：通知里直接带上数据 ----------
class PushObserver {
public:
  virtual ~PushObserver() = default;
  virtual void onProgress(double pos, double dur) = 0;   // 想多推一个「已缓冲到哪」，这里的签名和所有实现都要改
};

// ---------- 拉模型：通知只说哪方面变了，数据由观察者向 Player 要 ----------
class Player;
enum class Changed { Progress, Buffer };
class PullObserver {
public:
  virtual ~PullObserver() = default;
  virtual void onChanged(const Player& p, Changed what) = 0;
};

class Player {
public:
  void attach(PushObserver* o) { push_.push_back(o); }
  void attach(PullObserver* o) { pull_.push_back(o); }
  double position() const { return pos_; }
  double duration() const { return dur_; }
  double buffered() const { return buffered_; }               // 拉模型：加数据只要加一个查询函数

  void tick(double pos) {
    pos_ = pos;
    for (auto* o : push_) o->onProgress(pos_, dur_);
    for (auto* o : pull_) o->onChanged(*this, Changed::Progress);
  }
  void onDownloaded(double until) {
    buffered_ = until;
    for (auto* o : pull_) o->onChanged(*this, Changed::Buffer);
  }

private:
  std::vector<PushObserver*> push_;
  std::vector<PullObserver*> pull_;
  double pos_ = 0, dur_ = 10.0, buffered_ = 0;
};

class PushBar : public PushObserver {
public:
  void onProgress(double pos, double dur) override { std::printf("  [推·进度条] %.1f / %.1f s\n", pos, dur); }
};

class PullBar : public PullObserver {                       // 进度条要两样：播放位置和缓冲位置
public:
  void onChanged(const Player& p, Changed) override {
    std::printf("  [拉·进度条] %.1f / %.1f s，缓冲到 %.1f s\n", p.position(), p.duration(), p.buffered());
  }
};

class PullSubtitle : public PullObserver {                  // 字幕只关心播放位置，缓冲变化直接忽略
public:
  void onChanged(const Player& p, Changed what) override {
    if (what == Changed::Progress) std::printf("  [拉·字幕]   找 %.1f s 的字幕\n", p.position());
  }
};

int main() {
  Player player;
  PushBar pushBar;
  PullBar pullBar;
  PullSubtitle pullSub;
  player.attach(&pushBar);
  player.attach(&pullBar);
  player.attach(&pullSub);
  player.onDownloaded(4.0);
  player.tick(2.0);
}
```

```text
  [拉·进度条] 0.0 / 10.0 s，缓冲到 4.0 s
  [推·进度条] 2.0 / 10.0 s
  [拉·进度条] 2.0 / 10.0 s，缓冲到 4.0 s
  [拉·字幕]   找 2.0 s 的字幕
```

拉模型的一次通知：

```uml
label: 时序图（拉模型）：Player 调用进度条的 onChanged(*this, Progress)，进度条回头调用 Player 的 position() 和 buffered() 取数据，最后返回
participants:
  - { id: p, name: 'Player', x: 20, w: 100 }
  - { id: b, name: 'PullBar', x: 300, w: 110 }
messages:
  - { from: p, to: b, text: 'onChanged(*this, Progress)', n: 1 }
  - { from: b, to: p, text: 'position()' }
  - { from: p, to: b, text: '2.0', type: reply }
  - { from: b, to: p, text: 'buffered()' }
  - { from: p, to: b, text: '4.0', type: reply }
  - { from: b, to: p, text: '', type: reply }
```

| | 推模型 | 拉模型 |
|---|---|---|
| 通知里带什么 | 观察者可能需要的数据 | 只有「谁变了、哪方面变了」 |
| 观察者认识谁 | 只认识数据类型 | 要认识 Subject 的查询接口 |
| 加一项数据（比如缓冲进度） | 接口签名变了，**所有**观察者都要改 | Subject 加一个查询函数，用得着的观察者去调 |
| 数据一致性 | 通知时的快照，不会变 | 观察者回来拉的时候，状态**可能已经又变了**（多线程、重入时） |
| 适合 | 数据少而固定；跨线程、跨进程（只能推） | 数据多、每个观察者只要其中一部分 |

折中的做法是推一个**事件结构体**：`onProgress(const ProgressEvent& e)`，以后加字段不用改签名。第 5 节事件总线里的事件就是这种写法。

### 3.7 设计上的取舍

- **同步还是异步**：同步通知简单、可预测，但慢观察者会拖慢 Subject；异步（投递到队列或线程池）不阻塞，但观察者看到的状态会滞后，还要处理线程安全。播放器的进度回调通常同步发给界面线程，统计上报则异步。
- **通知的粒度**：每帧都通知（24 次/秒）还是每秒一次？进度条要每帧，统计只要每秒，不要让 Subject 去适配每个观察者，在观察者那边节流（练习 3）。
- **名单谁来管**：裸指针的名单要求观察者一定在析构前退订；`weak_ptr` 要求观察者用 `shared_ptr` 管理；`Connection` 句柄把订阅本身变成一个对象。下一节一个个看。

## 4. 现代 C++ 写法

### 4.1 `weak_ptr`：观察者析构后自动失效

如果观察者本来就用 `shared_ptr` 管理，Subject 可以只存 `weak_ptr`：它不延长观察者的寿命，通知时 `lock()` 一下，失败就说明观察者已经没了：

```cpp
// 《观察者模式》：Player 存 weak_ptr，观察者析构后自动失效；通知时先复制一份快照，回调里增删观察者也安全
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double pos) = 0;
};

class Player {
public:
  void attach(const std::shared_ptr<PlayerObserver>& o) { observers_.push_back(o); }
  void tick(double pos) {
    auto snapshot = observers_;                    // 快照：回调里 attach / detach 不影响这一轮的遍历
    for (auto& w : snapshot)
      if (auto o = w.lock()) o->onProgress(pos);   // lock 成功期间，观察者不会被析构
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                    [](const std::weak_ptr<PlayerObserver>& w) { return w.expired(); }),
                     observers_.end());            // 顺手清掉已经析构的
  }
  std::size_t size() const { return observers_.size(); }
private:
  std::vector<std::weak_ptr<PlayerObserver>> observers_;
};

class Logger : public PlayerObserver {
public:
  explicit Logger(const char* name) : name_(name) {}
  ~Logger() override { std::printf("  （%s 析构）\n", name_); }
  void onProgress(double pos) override { std::printf("  [%s] %.1f s\n", name_, pos); }
private:
  const char* name_;
};

int main() {
  Player player;
  auto bar = std::make_shared<Logger>("进度条");
  auto panel = std::make_shared<Logger>("字幕面板");
  player.attach(bar);
  player.attach(panel);
  player.tick(1.0);
  panel.reset();                                   // 关掉字幕面板，不用 detach
  std::printf("面板关掉后，列表里还有 %zu 项\n", player.size());
  player.tick(2.0);
  std::printf("通知一轮后，列表里剩 %zu 项\n", player.size());
}
```

```text
  [进度条] 1.0 s
  [字幕面板] 1.0 s
  （字幕面板 析构）
面板关掉后，列表里还有 2 项
  [进度条] 2.0 s
通知一轮后，列表里剩 1 项
  （进度条 析构）
```

两个细节：

- `lock()` 返回的 `shared_ptr` 在回调期间一直持有观察者，**回调执行时观察者不会被析构**，哪怕别的代码在回调里 `reset` 了最后一个外部引用。
- 先复制一份快照再遍历，回调里 attach / detach 改的是 `observers_`，不影响这一轮的 `snapshot`；第 3.2、3.3 节的两个坑一起解决了。代价是每次通知都复制整个 `vector`（第 4.3 节实测，这是最慢的一种）。

缺点也明显：强迫观察者必须由 `shared_ptr` 管理。栈上的对象、作为成员的对象都订阅不了。

### 4.2 RAII 订阅句柄：`Signal` + `Connection`

更通用的做法是让「订阅」本身成为一个对象：`connect()` 返回一个 `Connection`，它析构时自动断开。观察者不用继承任何接口，传一个 lambda 就行——这就是 Boost.Signals2、sigslot 这类库的做法：

```cpp
// 《观察者模式》：Signal + RAII 的 Connection——订阅返回一个句柄，句柄析构就自动退订；通知中途增删也安全
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Connection {                                 // 订阅句柄：只能移动，析构时退订
public:
  Connection() = default;
  explicit Connection(std::function<void()> off) : off_(std::move(off)) {}
  Connection(Connection&& o) noexcept : off_(std::exchange(o.off_, nullptr)) {}
  Connection& operator=(Connection&& o) noexcept {
    if (this != &o) { disconnect(); off_ = std::exchange(o.off_, nullptr); }
    return *this;
  }
  ~Connection() { disconnect(); }
  void disconnect() {
    if (auto f = std::exchange(off_, nullptr)) f();
  }
private:
  std::function<void()> off_;
};

template <class... Args>
class Signal {
public:
  [[nodiscard]] Connection connect(std::function<void(Args...)> fn) {
    auto slot = std::make_shared<Slot>(Slot{std::move(fn), true});
    st_->slots.push_back(slot);
    std::weak_ptr<State> ws = st_;                 // 句柄只拿弱引用：Signal 先析构也没关系
    std::weak_ptr<Slot> wslot = slot;
    return Connection([ws, wslot] {
      auto st = ws.lock();
      auto sl = wslot.lock();
      if (!st || !sl) return;
      sl->alive = false;                           // 先标记，不马上从 vector 里删
      st->compact();
    });
  }

  void emit(Args... args) {
    auto st = st_;
    ++st->depth;
    const std::size_t n = st->slots.size();        // 这一轮只通知已有的；回调里新连上的下一轮才收到
    for (std::size_t i = 0; i < n; ++i) {          // 用下标：push_back 扩容也不会让它失效
      auto slot = st->slots[i];                    // 复制 shared_ptr：回调里断开自己，function 也不会被析构
      if (slot->alive) slot->fn(args...);
    }
    --st->depth;
    st->compact();
  }

  std::size_t size() const { return st_->slots.size(); }

private:
  struct Slot {
    std::function<void(Args...)> fn;
    bool alive;
  };
  struct State {
    std::vector<std::shared_ptr<Slot>> slots;
    int depth = 0;                                 // 正在 emit 的层数（重入时大于 1）
    void compact() {                               // 延迟删除：没人在遍历时才真正删
      if (depth > 0) return;
      slots.erase(std::remove_if(slots.begin(), slots.end(), [](auto& s) { return !s->alive; }), slots.end());
    }
  };
  std::shared_ptr<State> st_ = std::make_shared<State>();
};

// ---------------- 用起来 ----------------

class Player {
public:
  Signal<double, double> progressChanged;          // (pos, dur)
  void tick(double pos) { progressChanged.emit(pos, 10.0); }
};

class SubtitlePanel {                              // 面板持有自己的 Connection：面板析构，订阅自动断开
public:
  explicit SubtitlePanel(Player& p)
      : conn_(p.progressChanged.connect([this](double pos, double) { show(pos); })) {}
private:
  void show(double pos) { std::printf("  [字幕] %.1f s：%s\n", pos, line_.c_str()); }
  std::string line_ = "Big Buck Bunny wakes up.";
  Connection conn_;                                // 放在最后：最先析构，先断开再销毁 line_
};

int main() {
  Player player;
  Connection bar = player.progressChanged.connect([](double pos, double dur) {
    std::printf("  [进度条] %.1f / %.1f s\n", pos, dur);
  });

  Connection first;                                // 只要第一次进度：回调里断开自己
  first = player.progressChanged.connect([&first](double pos, double) {
    std::printf("  [首帧统计] %.1f s，上报后退订\n", pos);
    first.disconnect();
  });

  std::optional<SubtitlePanel> panel;
  panel.emplace(player);
  std::printf("tick(0)，%zu 个订阅：\n", player.progressChanged.size());
  player.tick(0);

  panel.reset();                                   // 关掉字幕面板：不用手动退订
  std::printf("tick(1)，%zu 个订阅：\n", player.progressChanged.size());
  player.tick(1);

  {
    Signal<int> temp;
    Connection c = temp.connect([](int) {});
  }                                                // 先析构 c（断开），再析构 temp
  Connection late;
  {
    Signal<int> temp;
    late = temp.connect([](int) {});
  }                                                // temp 先没了，late 还在
  late.disconnect();                               // 弱引用 lock 失败，什么也不做
  std::puts("Signal 先析构，Connection 后析构：没有问题");
}
```

```text
tick(0)，3 个订阅：
  [进度条] 0.0 / 10.0 s
  [首帧统计] 0.0 s，上报后退订
  [字幕] 0.0 s：Big Buck Bunny wakes up.
tick(1)，1 个订阅：
  [进度条] 1.0 / 10.0 s
Signal 先析构，Connection 后析构：没有问题
```

前面每个坑在这里是怎么处理的：

| 坑 | 处理 |
|---|---|
| 观察者析构没退订（3.1） | `SubtitlePanel` 持有 `Connection` 成员，面板析构时句柄跟着析构，自动断开 |
| Subject 先析构（3.1 的反面） | 句柄只持有 `weak_ptr<State>`，lock 失败就什么也不做 |
| 通知途中退订（3.2） | **延迟删除**：只把 `alive` 标成 `false`，`depth > 0` 时不动 `vector`，等最外层 `emit` 结束再 `compact` |
| 通知途中订阅（3.3） | 用**下标**遍历，`push_back` 扩容后 `slots[i]` 照样有效；只通知开始时就有的 `n` 个 |
| 回调里断开自己 | 每个槽复制一份 `shared_ptr<Slot>`，回调执行期间 `std::function` 不会被销毁 |

还有两个容易忽略的细节：

- `connect` 标了 `[[nodiscard]]`。`Connection` 是 RAII 句柄，如果调用方不接住返回值，临时对象当场析构，订阅当场断开，回调一次也收不到。练习 4 就是这个 bug。
- `SubtitlePanel` 里 `conn_` 放在**最后一个成员**。成员按声明的逆序析构，`conn_` 最先析构：先断开订阅，再销毁回调里要用的 `line_`。反过来放，`line_` 已经析构、订阅还在的那一小段时间里如果有通知进来（多线程时），回调就会读到已经销毁的字符串。

Qt 的信号槽也是这个思路，只是 `Connection` 藏起来了：`connect` 时如果接收者是 `QObject`，Qt 会在接收者析构时自动断开它的所有连接（Qt 专栏的《信号与槽》《对象树与内存管理》），所以 Qt 程序很少遇到第 3.1 节的悬空问题；但连接到 lambda 又不传上下文对象时，lambda 里捕获的 `this` 照样会悬空。

### 4.3 开销实测

8 个观察者，每个只做一次加法，比较各种写法每次通知的耗时：

<figure class="diagram">
<svg viewBox="0 0 740 246" role="img" aria-label="8 个观察者，每次通知的耗时。Clang：硬编码 0.80 ns，虚函数 5.07，std::function 8.69，Signal 14.06，weak_ptr::lock 12.17，weak_ptr 加每次复制快照 35.03。GCC：0.84、5.03、7.20、14.30、11.76、20.37"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="198.0" y="30.0" width="4.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="208.0" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.80 ns</text><rect x="198.0" y="60.0" width="25.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="229.3" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.07 ns</text><rect x="198.0" y="90.0" width="43.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="247.4" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">8.69 ns</text><rect x="198.0" y="120.0" width="70.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="274.3" y="135.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">14.06 ns</text><rect x="198.0" y="150.0" width="60.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="264.9" y="165.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">12.17 ns</text><rect x="198.0" y="180.0" width="175.2" height="20.0" rx="2" style="fill:var(--danger)"/><text x="379.1" y="195.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">35.03 ns</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="468.0" y="30.0" width="4.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="478.2" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.84 ns</text><rect x="468.0" y="60.0" width="25.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="499.1" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.03 ns</text><rect x="468.0" y="90.0" width="36.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="510.0" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">7.20 ns</text><rect x="468.0" y="120.0" width="71.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="545.5" y="135.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">14.30 ns</text><rect x="468.0" y="150.0" width="58.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="532.8" y="165.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">11.76 ns</text><rect x="468.0" y="180.0" width="101.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="575.9" y="195.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">20.37 ns</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">硬编码直接调用</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数 vector&lt;T*&gt;</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::function</text><text x="190.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Signal（复制 shared_ptr）</text><text x="190.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">weak_ptr::lock</text><text x="190.0" y="195.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">weak_ptr + 复制快照</text><text x="10.0" y="238.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_notify：-O2，每轮 200 万次通知，7 轮取中位数；每个观察者只做一次加法</text></g></svg>
</figure>

| 写法（8 个观察者，每次通知，ns） | Clang / macOS | GCC 13 / Ubuntu |
|---|---|---|
| 硬编码直接调用 | 0.80 | 0.84 |
| 虚函数接口 `vector<T*>` | 5.07 | 5.03 |
| `vector<std::function>` | 8.69 | 7.20 |
| `Signal`（每个槽复制一次 `shared_ptr`） | 14.06 | 14.30 |
| `weak_ptr::lock` | 12.17 | 11.76 |
| `weak_ptr` + 每次复制快照 | 35.03 | 20.37 |

- **硬编码最快**，因为 8 个调用全被内联了，一次通知不到 1 ns。观察者模式用间接调用换解耦，代价是每个观察者 0.6 ns 左右（虚函数）到 1 ns 左右（`std::function`）。
- **`Signal` 和 `weak_ptr` 慢在原子操作**：复制 `shared_ptr` 和 `lock()` 都要对引用计数做原子加一、原子减一。这是「回调期间保证对象活着」的代价。
- **每次复制快照最贵**，Clang 上 35.03 ns：一次堆分配 + 8 次原子加减。GCC 上明显快（20.37 ns），可能是因为 glibc 的 `malloc` 用 tcache 处理这种小块内存的反复分配、释放，比 macOS 的分配器快。
- 放到播放器里看：进度回调每秒几十次，就算最慢的写法，每秒也只多花 1 µs。**这些开销在界面通知里完全可以忽略**，选哪种写法应该看生命周期好不好管，而不是看这几个纳秒。只有每秒几百万次的事件（比如每个音频采样、每个网络包）才值得用硬编码或模板把间接调用去掉。

### 4.4 多线程下的通知

解码线程更新进度，界面线程订阅、退订，名单就要加锁。这部分代码要用线程，只放在 `dp-lab/19-observer/threaded.cpp` 里，这里说结论。

**第一个坑：在锁内调用回调。** 最直接的写法是 `notify` 拿着锁遍历名单，`subscribe` 也拿同一把锁。只要某个回调里（直接或间接地）又去订阅，就会卡住：

<!-- run: off -->
```cpp
void notify(double pos) {
  std::lock_guard<std::mutex> lk(m_);              // 整个通知过程都拿着锁
  for (auto& f : fns_) f(pos);                      // 回调里如果等别的线程 subscribe……
}
```

`threaded.cpp` 的第 1 部分模拟了一个常见的形式：回调里把「订阅一个新模块」交给 UI 线程做，并同步等它完成。UI 线程要拿名单的锁，锁在通知线程手里；通知线程在等 UI 线程。程序用 `wait_for(300ms)` 超时来证明它卡住了，两个平台的输出一样：

```text
1. 在锁内回调：
   等了 300 ms，UI 线程还拿不到锁：它在等通知线程放锁，通知线程在等它——如果用 get() 一直等，就是死锁
   notify 返回、放锁之后，UI 线程才订阅成功
```

如果回调在**同一个线程**里直接调用 `subscribe`，对 `std::mutex` 重复加锁是未定义行为，实际上就是当场死锁。换成 `std::recursive_mutex` 能让它不卡，但又回到了第 3.3 节：边遍历边 `push_back`。规则只有一条：**不要在持有锁的时候调用你不认识的代码**（回调就是你不认识的代码）。

**第二个问题：锁的范围决定了能不能并行。** 三种做法，2 个线程同时通知：

- **锁住整个通知**：上面那种，回调都在锁里执行；
- **锁内复制快照**：锁里只把名单复制一份，放锁以后再逐个回调；
- **写时复制**（copy-on-write）：名单是一个 `shared_ptr<const vector<...>>`，订阅、退订时复制出新的一份再替换；通知时锁里只复制一个 `shared_ptr`。

<figure class="diagram">
<svg viewBox="0 0 864 156" role="img" aria-label="多线程通知，平均每次通知占用的时间。锁住整个通知：Clang 1 线程 285.5 ns、2 线程 574.7 ns；GCC 396.8、807.0。锁内复制快照：Clang 360.0、437.4；GCC 454.1、442.7。写时复制：Clang 294.0、224.4；GCC 410.0、232.7"><g style="font-family:var(--mono)"><text x="118.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang · 1 线程</text><rect x="118.0" y="30.0" width="33.6" height="20.0" rx="2" style="fill:var(--danger)"/><text x="157.6" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">285.5</text><rect x="118.0" y="60.0" width="42.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="166.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">360.0</text><rect x="118.0" y="90.0" width="34.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="158.6" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">294.0</text><text x="304.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang · 2 线程</text><rect x="304.0" y="30.0" width="67.6" height="20.0" rx="2" style="fill:var(--danger)"/><text x="377.6" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">574.7</text><rect x="304.0" y="60.0" width="51.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="361.5" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">437.4</text><rect x="304.0" y="90.0" width="26.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="336.4" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">224.4</text><text x="490.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC · 1 线程</text><rect x="490.0" y="30.0" width="46.7" height="20.0" rx="2" style="fill:var(--danger)"/><text x="542.7" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">396.8</text><rect x="490.0" y="60.0" width="53.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="549.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">454.1</text><rect x="490.0" y="90.0" width="48.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="544.2" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">410.0</text><text x="676.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC · 2 线程</text><rect x="676.0" y="30.0" width="94.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="776.9" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">807.0</text><rect x="676.0" y="60.0" width="52.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="734.1" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">442.7</text><rect x="676.0" y="90.0" width="27.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="709.4" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">232.7</text><text x="110.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">锁住整个通知</text><text x="110.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">锁内复制快照</text><text x="110.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">写时复制</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">threaded：8 个观察者各做约 100 ns 的计算；总耗时 ÷ 总通知次数（ns），越短越好；7 轮中位数</text></g></svg>
</figure>

| 每次通知占用的时间（ns） | Clang 1 线程 | Clang 2 线程 | GCC 1 线程 | GCC 2 线程 |
|---|---|---|---|---|
| 锁住整个通知 | 285.5 | 574.7 | 396.8 | 807.0 |
| 锁内复制快照 | 360.0 | 437.4 | 454.1 | 442.7 |
| 写时复制 | 294.0 | 224.4 | 410.0 | 232.7 |

（「每次通知占用的时间」= 总耗时 ÷ 两个线程加起来的通知次数，越短说明吞吐越高。GCC 的数字在 colima 虚拟机里测，只有 2 个 CPU。）

- 锁住整个通知时，**2 个线程比 1 个线程还慢一倍**（Clang 285.5 → 574.7 ns）：回调全在锁里，两个线程只能排队，还要加上抢锁的开销。
- 锁内复制快照能并行执行回调，但每次通知都要分配内存、做 8 次原子加减，两个线程还会抢同一个引用计数的缓存行，只比单线程略好。
- 写时复制每次通知只复制一个指针，2 个线程时每次通知降到 224.4 ns（GCC 232.7 ns），差不多是单线程的一半，真正并行了。代价挪到了订阅、退订那边：每次都要复制整张表。名单很少变、通知很频繁时，这是最合适的。

**第三个问题：锁外回调的代价。** 只要回调在锁外执行，就会有这种情况：线程 A 拿到了快照，正准备调用 X；线程 B 此时 `unsubscribe(X)` 返回了。A 手里的快照里还有 X，**X 在退订返回之后仍会被调用一次**。`threaded.cpp` 第 3 部分订阅、退订 1000 次，Clang 上退订返回之后又被调用了 1～4 次（每次运行不同），GCC 上 3 次。快照里存的是 `shared_ptr`，所以 X 不会是悬空的，但 X 的逻辑必须能容忍「退订后还收到一次通知」。如果不能容忍（比如 X 马上要析构了），退订就得等其他线程里正在进行的通知结束，这又要额外的同步。这是锁外回调的通病，多线程的信号槽库都要在「可能死锁」和「退订后还会被调用」之间做取舍。

## 5. 观察者和相似的模式

### 5.1 发布-订阅：中间多了一个事件总线

观察者模式里，订阅者要拿到 Subject 本身才能 `attach`：字幕面板得认识 `Player`。如果模块很多、互相订阅，依赖关系还是会织成一张网。**发布-订阅**（publish-subscribe）在中间加一个事件总线：发布者只管把事件扔给总线，订阅者只管向总线订阅某种事件，双方互相不知道对方存在：

```cpp
// 《观察者模式》：事件总线（发布-订阅）——发布者和订阅者只认识总线和事件类型，互相不知道对方是谁
#include <cstdio>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

class EventBus {
public:
  template <class E>
  void subscribe(std::function<void(const E&)> fn) {
    handlers_[std::type_index(typeid(E))].push_back(
        [fn = std::move(fn)](const void* e) { fn(*static_cast<const E*>(e)); });
  }
  template <class E>
  void publish(const E& e) {
    auto it = handlers_.find(std::type_index(typeid(E)));
    if (it == handlers_.end()) return;               // 没人订阅：发出去也没事
    auto copy = it->second;                          // 快照：回调里再订阅也安全
    for (auto& h : copy) h(&e);
  }
private:
  std::unordered_map<std::type_index, std::vector<std::function<void(const void*)>>> handlers_;
};

// 事件只是普通的结构体
struct ProgressEvent { double pos; };
struct NetworkSlowEvent { int kbps; };

// 发布者：只依赖 EventBus 和事件类型
class Player {
public:
  explicit Player(EventBus& bus) : bus_(bus) {}
  void tick(double pos) { bus_.publish(ProgressEvent{pos}); }
private:
  EventBus& bus_;
};
class Downloader {
public:
  explicit Downloader(EventBus& bus) : bus_(bus) {}
  void measure(int kbps) { if (kbps < 800) bus_.publish(NetworkSlowEvent{kbps}); }
private:
  EventBus& bus_;
};

int main() {
  EventBus bus;
  // 订阅者：同样只认识总线和事件类型
  bus.subscribe<ProgressEvent>([](const ProgressEvent& e) { std::printf("  [弹幕] 滚到 %.1f s\n", e.pos); });
  bus.subscribe<ProgressEvent>([](const ProgressEvent& e) { std::printf("  [统计] 进度 %.1f s\n", e.pos); });
  bus.subscribe<NetworkSlowEvent>([](const NetworkSlowEvent& e) {
    std::printf("  [清晰度] 网速 %d kbps，切到 480p\n", e.kbps);
  });

  Player player(bus);
  Downloader downloader(bus);
  player.tick(3.0);
  downloader.measure(650);
  downloader.measure(2400);                          // 网速够：不发事件
}
```

```text
  [弹幕] 滚到 3.0 s
  [统计] 进度 3.0 s
  [清晰度] 网速 650 kbps，切到 480p
```

`Player` 和弹幕、`Downloader` 和清晰度切换之间没有任何 `#include` 关系，只共享 `ProgressEvent`、`NetworkSlowEvent` 这两个结构体。代价是：谁发、谁收，从代码里完全看不出来了，排查「这个事件为什么没人处理」「这个回调是谁触发的」要靠日志；事件类型一多，总线会变成全局变量一样的东西。

### 5.2 三种结构对比

<figure class="diagram">
<svg viewBox="0 0 860 244" role="img" aria-label="三张小图。观察者：Player 直接指向进度条、字幕、统计。发布-订阅：Player 和 Downloader 把事件发给 Bus，Bus 转给弹幕、统计、清晰度，双方互不认识。中介者：播放按钮、进度条、音量、字幕都和中间的 Mediator 双向连线，彼此之间不直接通信"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">观察者</text><text x="20.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">被观察者直接持有观察者列表</text><rect x="20" y="100" width="80" height="34" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="60.0" y="121.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Player</text><rect x="160" y="56" width="70" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="195.0" y="75.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">进度条</text><line x1="100.0" y1="117.0" x2="150.9" y2="76.6" style="stroke:var(--accent);stroke-width:1.4"/><path d="M158.0,71.0 L147.8,72.7 L154.1,80.5 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><rect x="160" y="108" width="70" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="195.0" y="127.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">字幕</text><line x1="100.0" y1="117.0" x2="149.0" y2="122.1" style="stroke:var(--accent);stroke-width:1.4"/><path d="M158.0,123.0 L149.6,117.1 L148.5,127.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><rect x="160" y="160" width="70" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="195.0" y="179.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">统计</text><line x1="100.0" y1="117.0" x2="151.6" y2="168.6" style="stroke:var(--accent);stroke-width:1.4"/><path d="M158.0,175.0 L155.2,165.1 L148.1,172.2 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><text x="20.0" y="230.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">一对多；订阅者知道 Player</text><text x="270.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">发布-订阅（事件总线）</text><text x="270.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">双方只认识总线和事件类型</text><rect x="270" y="70" width="84" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="312.0" y="89.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Player</text><line x1="354.0" y1="85.0" x2="381.4" y2="110.8" style="stroke:var(--link);stroke-width:1.4"/><path d="M388.0,117.0 L384.9,107.2 L378.0,114.5 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><rect x="270" y="130" width="84" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="312.0" y="149.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Downloader</text><line x1="354.0" y1="145.0" x2="381.1" y2="122.7" style="stroke:var(--link);stroke-width:1.4"/><path d="M388.0,117.0 L377.9,118.9 L384.2,126.6 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><rect x="390" y="96" width="60" height="42" rx="5" style="fill:var(--panel);stroke:var(--link);stroke-width:1.4"/><text x="420.0" y="121.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Bus</text><rect x="500" y="56" width="60" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="530.0" y="75.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">弹幕</text><line x1="450.0" y1="117.0" x2="491.5" y2="77.2" style="stroke:var(--link);stroke-width:1.4"/><path d="M498.0,71.0 L488.0,73.6 L495.0,80.8 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><rect x="500" y="108" width="60" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="530.0" y="127.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">统计</text><line x1="450.0" y1="117.0" x2="489.1" y2="121.9" style="stroke:var(--link);stroke-width:1.4"/><path d="M498.0,123.0 L489.7,116.9 L488.4,126.8 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><rect x="500" y="160" width="60" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="530.0" y="179.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">清晰度</text><line x1="450.0" y1="117.0" x2="492.3" y2="168.1" style="stroke:var(--link);stroke-width:1.4"/><path d="M498.0,175.0 L496.1,164.9 L488.4,171.3 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><text x="270.0" y="230.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">多对多；发布者不知道谁在听</text><text x="600.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">中介者</text><text x="600.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">同事之间的协作逻辑集中在中介者里</text><rect x="670" y="98" width="84" height="38" rx="5" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.4"/><text x="712.0" y="121.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Mediator</text><rect x="600" y="56" width="74" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="637.0" y="75.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">播放按钮</text><line x1="637.0" y1="86.0" x2="712.0" y2="98.0" style="stroke:var(--warn);stroke-width:1.4"/><rect x="750" y="56" width="74" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="787.0" y="75.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">进度条</text><line x1="787.0" y1="86.0" x2="712.0" y2="98.0" style="stroke:var(--warn);stroke-width:1.4"/><rect x="600" y="160" width="74" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="637.0" y="179.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">音量</text><line x1="637.0" y1="160.0" x2="712.0" y2="136.0" style="stroke:var(--warn);stroke-width:1.4"/><rect x="750" y="160" width="74" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="787.0" y="179.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">字幕</text><line x1="787.0" y1="160.0" x2="712.0" y2="136.0" style="stroke:var(--warn);stroke-width:1.4"/><text x="600.0" y="230.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">多对多的双向调用，中介者知道每个同事</text></g></svg>
</figure>

| | 观察者 | 发布-订阅（事件总线） | 中介者 | 策略 |
|---|---|---|---|---|
| 关系 | 一个 Subject 对多个观察者 | 多个发布者对多个订阅者 | 多个同事经由中介者互相协作 | 一个上下文用一个策略 |
| 谁认识谁 | 观察者认识 Subject；Subject 只认识接口 | 双方只认识总线和事件类型 | 中介者认识每个同事；同事只认识中介者 | 上下文只认识策略接口 |
| 调用方向 | 单向：Subject → 观察者 | 单向：发布者 → 总线 → 订阅者 | 双向：同事 ↔ 中介者 | 上下文调用策略 |
| 业务逻辑在哪 | 各个观察者里 | 各个订阅者里 | **集中在中介者里** | 策略里 |
| 什么时候用 | 一处变化，多处跟着更新 | 模块多、跨层、希望彻底解耦（插件、微服务的消息队列） | 几个对象之间的协作规则复杂，比如「拖动进度条时暂停字幕、松手后恢复」 | 同一件事有几种做法，运行时选一种 |

- **观察者 vs 中介者**：观察者里的 Subject 只广播「我变了」，不关心观察者怎么反应；中介者是有主见的协调者，它知道「按钮按下时要让进度条做 A、让字幕做 B」。第 3.4 节说的「观察者之间有顺序依赖」，往往就该换成中介者。
- **观察者 vs 策略**（上一篇）：两者都是「依赖一个接口，运行时换实现」。策略是**一个**，Context 调用它来**完成自己的工作**，要用它的返回值；观察者是**任意多个**，Subject 只是**告知**，不关心返回值，也不关心有没有人在听。
- **Qt 的信号与槽**：本质是观察者模式，`connect` 就是 `attach`。它额外做了三件事：接收者析构时自动断开（第 4.2 节的 Connection）、跨线程时自动排队（第 3.5 节的排队通知，见《跨线程的信号与槽》）、用 moc 生成信号函数的实现和元信息（新式的函数指针 `connect` 写法还能在编译期检查参数类型）。

## 6. 小结

- 观察者模式让 Subject 只依赖一个观察者接口，状态变化时逐个通知。实测加一个弹幕模块：硬编码版改 3 个已有文件、Player 增删 12 行；观察者版只改 `main.cpp` 3 行，Player 不动。
- 代价是一层间接调用（8 个观察者每次通知 5.07 ns，硬编码 0.80 ns），以及「谁会被调用」只有运行时才知道。界面通知里这点开销可以忽略。
- C++ 里的坑都和生命周期、遍历有关：观察者析构没退订 → heap-use-after-free；通知途中退订 → 漏通知、重复通知；通知途中订阅 → 扩容后迭代器悬空，名单正好满时才崩。用 ASan 都能抓到，但 libstdc++ 要加 `-D_GLIBCXX_SANITIZE_VECTOR` 才报容器越界。
- 不要依赖通知顺序；回调里改 Subject 的状态会重入，事件顺序会乱，需要时用排队通知。
- 现代 C++ 写法：`weak_ptr` 名单（观察者必须用 `shared_ptr` 管理）、`Signal` + RAII 的 `Connection`（下标遍历、延迟删除、`[[nodiscard]]`）。
- 多线程：不要在锁内调用回调；锁住整个通知时 2 个线程反而更慢，名单少变、通知频繁时用写时复制；锁外回调意味着退订之后还可能被调用一次。
- 推模型简单、数据一致；拉模型灵活、加数据不改接口。发布-订阅在中间加一个总线让双方互不认识；中介者把协作逻辑集中起来。

## 7. 练习

1. **辨析**：下面四种情况，哪些是观察者模式，哪些不是（或者更像别的模式）？说出理由。
   - (a) 解码器有一个 `setFrameSink(FrameSink*)`，每解出一帧就调用 `sink->onFrame(frame)`，只能设一个 sink。
   - (b) 下载器每下载完一个分片，就调用所有注册过的 `ProgressListener` 的 `onChunk(done, total)`。
   - (c) 播放器有一个 `PlayerController`，按钮、进度条、音量条有操作时都告诉它，由它决定「拖动进度条时先暂停、松手后 seek 再恢复播放」。
   - (d) 日志模块向一个全局的 `EventBus` 订阅 `ErrorEvent`，任何模块出错时都往总线上发一个 `ErrorEvent`。
2. **动手**：第 3.2、3.3 节的 `FirstFrameStats`（回调里 detach 自己）和 `StallDetector`（回调里 attach 新观察者）一个都不许改，**只改 `Player`**，让两种情况都正确：退订自己时不漏掉别人，订阅新的时不崩溃。新订阅的观察者从下一次通知开始收到。
3. **设计**：播放器每帧都发进度通知（24 次/秒）。进度条每帧都要；统计上报只要每秒一次；字幕只在跨过下一句字幕的开始时间时才需要。怎么设计，才能不让 `Player` 知道这些差别？写出代码，模拟播放 3 秒（72 帧），数一数三个模块各收到几次。
4. **找问题**：下面的程序本来应该输出一行进度条和两行字幕。实际上进度条一次也没输出，字幕的第一行乱了（Clang 上输出「[字幕·] 1.0 s」，GCC 上直接段错误）。有两个 bug，分别在哪？怎么改？

   <!-- run: off -->
   ```cpp
   // 《观察者模式》练习 4：找问题——进度条一次也没更新，字幕面板读到了已经释放的内存
   #include <cstdio>
   #include <functional>
   #include <map>
   #include <memory>
   #include <string>
   #include <utility>
   #include <vector>

   class Connection {                                 // 订阅句柄：析构时退订
   public:
     Connection() = default;
     explicit Connection(std::function<void()> off) : off_(std::move(off)) {}
     Connection(Connection&& o) noexcept : off_(std::exchange(o.off_, nullptr)) {}
     Connection& operator=(Connection&& o) noexcept {
       if (this != &o) { disconnect(); off_ = std::exchange(o.off_, nullptr); }
       return *this;
     }
     ~Connection() { disconnect(); }
     void disconnect() { if (auto f = std::exchange(off_, nullptr)) f(); }
   private:
     std::function<void()> off_;
   };

   class ProgressSignal {                             // 简化版 Signal：通知时复制一份槽表
   public:
     using Map = std::map<int, std::function<void(double)>>;
     [[nodiscard]] Connection connect(std::function<void(double)> f) {
       int id = next_++;
       slots_->emplace(id, std::move(f));
       std::weak_ptr<Map> w = slots_;
       return Connection([w, id] { if (auto m = w.lock()) m->erase(id); });
     }
     void emit(double pos) {
       auto copy = *slots_;
       for (auto& kv : copy) kv.second(pos);
     }
   private:
     std::shared_ptr<Map> slots_ = std::make_shared<Map>();
     int next_ = 0;
   };

   class Player {
   public:
     ProgressSignal progressChanged;
     void tick(double pos) { progressChanged.emit(pos); }
   };

   class SubtitlePanel {
   public:
     SubtitlePanel(Player& p, std::string lang)
         : lang_(std::move(lang)), conn_(p.progressChanged.connect([this](double pos) { show(pos); })) {}
   private:
     void show(double pos) { std::printf("  [字幕·%s] %.1f s\n", lang_.c_str(), pos); }
     std::string lang_;
     Connection conn_;
   };

   int main() {
     std::setvbuf(stdout, nullptr, _IONBF, 0);
     Player player;
     player.progressChanged.connect([](double pos) { std::printf("  [进度条] %.1f s\n", pos); });

     std::vector<SubtitlePanel> panels;
     panels.emplace_back(player, "中文");
     panels.emplace_back(player, "English");
     player.tick(1.0);
   }
   ```

<details>
<summary>点击查看答案</summary>

**1.**

- (a) **不算典型的观察者**。只能设一个 sink，是「一对一的回调」，更像策略或者依赖注入：解码器把「解出的帧交给谁」这一步交给外部决定。观察者的关键是「一对多、名单可以随时增减」。不过这种单回调在 C 库里很常见（FFmpeg 的 `AVIOInterruptCB`、各种 `set_callback`），可以看成只有一个观察者的退化情况。
- (b) **是观察者**。下载器是 Subject，`ProgressListener` 是观察者接口，推模型（把 `done`、`total` 推过去）。
- (c) **是中介者**，不是观察者。按钮、进度条、音量条之间的协作规则（先暂停、seek、再恢复）集中在 `PlayerController` 里，它知道每个同事并主动指挥它们。如果改成「进度条拖动时广播一个事件，谁关心谁处理」，暂停和恢复的逻辑就会散落在各个观察者里，还要依赖通知顺序。
- (d) **是发布-订阅**（事件总线）。日志模块不认识任何发布者，发布者也不知道有日志模块，双方只共享 `ErrorEvent` 这个类型。

**2.** 两个坑的原因不同，修法也不同：退订用**延迟删除**（通知中只把指针置空，通知结束再统一清理），订阅用**下标遍历**（扩容后 `observers_[i]` 照样能用），并只遍历开始时就有的 `n` 个：

```cpp
// 《观察者模式》练习 2 答案：只改 Player——下标遍历 + 延迟删除，回调里 detach 自己、attach 新观察者都正确
#include <algorithm>
#include <cstdio>
#include <vector>

class PlayerObserver {
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double pos) = 0;
};

class Player {
public:
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  void detach(PlayerObserver* o) {
    auto it = std::find(observers_.begin(), observers_.end(), o);
    if (it == observers_.end()) return;
    if (depth_ > 0) *it = nullptr;                 // 正在通知：只置空，不动 vector 的结构
    else observers_.erase(it);
  }
  void tick(double pos) {
    ++depth_;
    const std::size_t n = observers_.size();       // 新 attach 的下一轮才通知
    for (std::size_t i = 0; i < n; ++i)            // 下标：扩容后 observers_[i] 照样有效
      if (auto* o = observers_[i]) o->onProgress(pos);
    if (--depth_ == 0)
      observers_.erase(std::remove(observers_.begin(), observers_.end(), nullptr), observers_.end());
  }
private:
  std::vector<PlayerObserver*> observers_;
  int depth_ = 0;
};

class Logger : public PlayerObserver {
public:
  explicit Logger(const char* name) : name_(name) {}
  void onProgress(double pos) override { std::printf("  [%s] %.1f s\n", name_, pos); }
private:
  const char* name_;
};

class FirstFrameStats : public PlayerObserver {    // 回调里 detach 自己
public:
  explicit FirstFrameStats(Player& p) : player_(p) {}
  void onProgress(double pos) override {
    std::printf("  [首帧统计] %.1f s，上报后退订\n", pos);
    player_.detach(this);
  }
private:
  Player& player_;
};

class StallDetector : public PlayerObserver {      // 回调里 attach 新观察者
public:
  StallDetector(Player& p, PlayerObserver& extra) : player_(p), extra_(extra) {}
  void onProgress(double) override {
    if (!armed_) { armed_ = true; player_.attach(&extra_); }
  }
private:
  Player& player_;
  PlayerObserver& extra_;
  bool armed_ = false;
};

int main() {
  Player player;
  FirstFrameStats first(player);
  Logger bar("进度条"), sub("字幕"), stall("卡顿检测");
  StallDetector detector(player, stall);
  player.attach(&first);
  player.attach(&bar);
  player.attach(&sub);
  player.attach(&detector);                        // 4 个，capacity 正好用满
  std::puts("tick(0)：");
  player.tick(0);
  std::puts("tick(1)：");
  player.tick(1);
}
```

```text
tick(0)：
  [首帧统计] 0.0 s，上报后退订
  [进度条] 0.0 s
  [字幕] 0.0 s
tick(1)：
  [进度条] 1.0 s
  [字幕] 1.0 s
  [卡顿检测] 1.0 s
```

第一次通知里进度条、字幕都收到了，没有重复；卡顿检测在 `tick(0)` 里订阅，从 `tick(1)` 开始收到。用 ASan 跑也没有报错。`depth_` 记录嵌套的层数：重入时（回调里又触发了 `tick`）内层结束不能清理，要等最外层结束。

**3.** 「每秒一次」「跨过边界时才要」是观察者自己的需求，不该让 `Player` 为每个观察者提供不同的频率。在回调外面包一层**过滤**：`throttle` 和 `on_crossing` 接收一个回调，返回一个新的回调，`Player` 看到的还是普通的进度回调（这其实是装饰器的思路，用在了 `std::function` 上）：

```cpp
// 《观察者模式》练习 3 答案：Player 每帧都通知；节流、只在跨过边界时转发，都做成包在回调外面的一层，Player 不用知道
#include <cstdio>
#include <functional>
#include <utility>
#include <vector>

using ProgressFn = std::function<void(double)>;

class Player {
public:
  void onProgress(ProgressFn f) { fns_.push_back(std::move(f)); }
  void tick(double pos) { for (auto& f : fns_) f(pos); }
private:
  std::vector<ProgressFn> fns_;
};

// 每隔 interval 秒最多转发一次
ProgressFn throttle(double interval, ProgressFn f) {
  return [interval, f = std::move(f), last = -1e9](double pos) mutable {
    if (pos - last >= interval - 1e-9) { last = pos; f(pos); }
  };
}

// 只在跨过某个边界（比如下一句字幕的开始时间）时转发
ProgressFn on_crossing(std::vector<double> marks, ProgressFn f) {
  return [marks = std::move(marks), f = std::move(f), next = std::size_t{0}](double pos) mutable {
    bool crossed = false;
    while (next < marks.size() && pos >= marks[next]) { ++next; crossed = true; }
    if (crossed) f(pos);
  };
}

int main() {
  Player player;
  int bar = 0, stats = 0, subtitle = 0;
  player.onProgress([&](double) { ++bar; });                                   // 进度条：每帧都要
  player.onProgress(throttle(1.0, [&](double) { ++stats; }));                  // 统计：每秒一次
  player.onProgress(on_crossing({0.0, 1.2, 2.5}, [&](double pos) {            // 字幕：换句时才要
    ++subtitle;
    std::printf("  [字幕] %.3f s 换下一句\n", pos);
  }));
  for (int frame = 0; frame < 72; ++frame) player.tick(frame / 24.0);          // 24 fps 播 3 秒
  std::printf("72 帧：进度条收到 %d 次，统计收到 %d 次，字幕收到 %d 次\n", bar, stats, subtitle);
}
```

```text
  [字幕] 0.000 s 换下一句
  [字幕] 1.208 s 换下一句
  [字幕] 2.500 s 换下一句
72 帧：进度条收到 72 次，统计收到 3 次，字幕收到 3 次
```

`Player` 仍然每帧通知 3 个回调，只是两个回调大部分时候直接返回。如果过滤之后被调用的次数远少于通知次数、而通知本身很贵（比如跨线程投递），就该反过来让 `Player` 提供不同频率的信号（`progressChanged` 每帧、`secondElapsed` 每秒）。字幕的第二次是 1.208 秒：24 fps 下第 29 帧才第一次越过 1.2 秒。

**4.** 两个 bug：

- **进度条那一行没有接住 `connect` 的返回值。** `Connection` 是 RAII 句柄，临时对象在这一行结束时就析构了，订阅当场断开。`connect` 标了 `[[nodiscard]]`，编译器其实已经警告了：

  ```text
  ex4_bug.cpp:62:3: warning: ignoring return value of function declared with 'nodiscard' attribute [-Wunused-result]
  ```

  改成 `Connection bar = player.progressChanged.connect(...)`，并让 `bar` 活得和需要收到通知的时间一样长。

- **`SubtitlePanel` 的 lambda 捕获了 `this`，而对象被搬家了。** `panels` 先放进「中文」面板，`capacity` 是 1；`emplace_back` 第二个时扩容，第一个面板被**移动**到新内存，旧对象析构。但订阅里那个 lambda 捕获的还是旧对象的地址（`Connection` 被移动走了，订阅没有断开），通知时 `show()` 读的是已经析构的 `lang_`。ASan 报：

  ```text
  SUMMARY: AddressSanitizer: heap-use-after-free ... in std::basic_string<...>::...
  ```

  修法：捕获了 `this` 的对象不能搬家。删掉拷贝构造（移动构造也就不会自动生成），用 `unique_ptr` 放进 `vector`，扩容只搬指针：

```cpp
// 《观察者模式》练习 4 答案：保存 Connection；面板禁止拷贝和移动，用 unique_ptr 放进 vector，地址不再变
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Connection {                                 // 订阅句柄：析构时退订
public:
  Connection() = default;
  explicit Connection(std::function<void()> off) : off_(std::move(off)) {}
  Connection(Connection&& o) noexcept : off_(std::exchange(o.off_, nullptr)) {}
  Connection& operator=(Connection&& o) noexcept {
    if (this != &o) { disconnect(); off_ = std::exchange(o.off_, nullptr); }
    return *this;
  }
  ~Connection() { disconnect(); }
  void disconnect() { if (auto f = std::exchange(off_, nullptr)) f(); }
private:
  std::function<void()> off_;
};

class ProgressSignal {                             // 简化版 Signal：通知时复制一份槽表
public:
  using Map = std::map<int, std::function<void(double)>>;
  [[nodiscard]] Connection connect(std::function<void(double)> f) {
    int id = next_++;
    slots_->emplace(id, std::move(f));
    std::weak_ptr<Map> w = slots_;
    return Connection([w, id] { if (auto m = w.lock()) m->erase(id); });
  }
  void emit(double pos) {
    auto copy = *slots_;
    for (auto& kv : copy) kv.second(pos);
  }
private:
  std::shared_ptr<Map> slots_ = std::make_shared<Map>();
  int next_ = 0;
};

class Player {
public:
  ProgressSignal progressChanged;
  void tick(double pos) { progressChanged.emit(pos); }
};

class SubtitlePanel {
public:
  SubtitlePanel(Player& p, std::string lang)
      : lang_(std::move(lang)), conn_(p.progressChanged.connect([this](double pos) { show(pos); })) {}
  SubtitlePanel(const SubtitlePanel&) = delete;            // lambda 捕获了 this：对象不能搬家
  SubtitlePanel& operator=(const SubtitlePanel&) = delete;
private:
  void show(double pos) { std::printf("  [字幕·%s] %.1f s\n", lang_.c_str(), pos); }
  std::string lang_;
  Connection conn_;                                        // 最后一个成员：最先析构，先断开再销毁 lang_
};

int main() {
  Player player;
  Connection bar = player.progressChanged.connect([](double pos) { std::printf("  [进度条] %.1f s\n", pos); });

  std::vector<std::unique_ptr<SubtitlePanel>> panels;      // 扩容只搬指针，面板本身不动
  panels.push_back(std::make_unique<SubtitlePanel>(player, "中文"));
  panels.push_back(std::make_unique<SubtitlePanel>(player, "English"));
  player.tick(1.0);
}
```

```text
  [进度条] 1.0 s
  [字幕·中文] 1.0 s
  [字幕·English] 1.0 s
```

这是 C++ 观察者实现里一个通用的教训：**回调捕获了谁，谁的地址就不能变，生命周期也不能比订阅短**。Qt 要求 `QObject` 不可拷贝，部分原因就在这里。

</details>
