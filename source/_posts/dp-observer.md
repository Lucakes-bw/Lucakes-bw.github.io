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

> **本文目标**：理解观察者模式——被观察者只依赖抽象接口，状态变化时通知所有订阅者；掌握 C++ 里观察者模式的两大坑（悬空观察者、通知途中增删/重入）及解法；会写现代 C++ 的 `Signal + Connection`（RAII 自动退订）；了解通知开销与多线程锁策略；分清观察者、发布-订阅（事件总线）与中介者。
>
> **前置知识**：上一篇《策略模式》；《SOLID 原则（下）》（依赖倒置）；《智能指针：unique_ptr、shared_ptr 与 weak_ptr》。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整多文件依赖对比、ASan 复现、多线程与压测代码见 `projects/dp-lab/19-observer/`。

## 1. 问题：进度一变，一堆模块都要跟着动

### 1.1 硬编码版：播放器自己挨个调用

播放器播放 `bbb-10s.mp4`（10 秒）时，播放位置每变一次，进度条、字幕、歌词、统计上报都要跟着更新。最直接的写法是让 `Player` 持有这些模块的引用，进度一变就挨个调用：

```cpp
// 《观察者模式》：硬编码版——Player 直接依赖并挨个调用每个界面/统计模块
#include <cstdio>

enum class PlayState { Stopped, Playing, Paused };
const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }

struct ProgressBar   { void update(double pos, double dur) { std::printf("  [进度条] %4.1f / %.1f s\n", pos, dur); } };
struct SubtitleView  { void seek(double pos) { std::printf("  [字幕]   找 %.1f s 的字幕\n", pos); } };
struct LyricsView    { void highlight(double pos) { std::printf("  [歌词]   高亮 %.1f s 那一句\n", pos); } };
struct StatsReporter {
  int ticks = 0;
  void onTick(double) { ++ticks; }
  void onState(PlayState s) { std::printf("  [统计]   上报：%s（已收到 %d 次进度）\n", name(s), ticks); }
};

class Player {
public:
  Player(ProgressBar& b, SubtitleView& s, LyricsView& l, StatsReporter& r)
      : bar_(b), sub_(s), lyrics_(l), stats_(r) {}

  void tick(double pos) {                         // 要更新的模块全写死在这里
    bar_.update(pos, dur_);
    sub_.seek(pos);
    lyrics_.highlight(pos);
    stats_.onTick(pos);
  }
  void setState(PlayState s) { state_ = s; stats_.onState(s); }

private:
  ProgressBar& bar_;
  SubtitleView& sub_;
  LyricsView& lyrics_;
  StatsReporter& stats_;
  double dur_ = 10.0;
  PlayState state_ = PlayState::Stopped;
};

int main() {
  ProgressBar bar; SubtitleView sub; LyricsView lyrics; StatsReporter stats;
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

### 1.2 加一个弹幕要改几处

如果产品要加弹幕模块 `DanmakuView`，拆成多文件（`deps/coupled/`）后用 `deps/measure_deps.sh` 统计改动量：

<figure class="diagram">
<svg viewBox="0 0 820 126" role="img" aria-label="加一个弹幕模块的改动量：硬编码版改动已有文件 3 个、Player 增删 12 行、改弹幕头文件要重编 3 个 .cpp；观察者版分别是 1 个、0 行、2 个"><g style="font-family:var(--mono)"><text x="98.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">改动已有文件（个）</text><rect x="98.0" y="30.0" width="150.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="254.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3</text><rect x="98.0" y="60.0" width="50.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="154.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1</text><text x="338.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Player 的两个文件增删（行）</text><rect x="338.0" y="30.0" width="150.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="494.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">12</text><rect x="338.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="345.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0</text><text x="578.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">改 danmaku_view.h 要重编（个 .cpp）</text><rect x="578.0" y="30.0" width="150.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="734.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3</text><rect x="578.0" y="60.0" width="100.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="684.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2</text><text x="90.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">硬编码版</text><text x="90.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">观察者版</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">deps/measure_deps.sh：diff 两个版本加弹幕前后的目录，c++ -MM 查头文件依赖。Clang 和 GCC 结果相同</text></g></svg>
</figure>

硬编码版的问题集中在四点：
- **依赖方向反了**：稳定的核心类 `Player` 依赖天天在变的界面模块，界面改头文件，`player.cpp` 也要重编；
- **违反开闭原则**：增删任何模块都要改 `Player` 的构造函数、成员、`tick()` 和 `#include`；
- **没法运行时增减**：用户在设置里关掉歌词面板，`Player` 的固定引用列表表达不了「现在有谁在听」；
- **接口不统一**：`Player` 被迫记住 `update / seek / highlight / onTick` 各自的名字。

## 2. 观察者模式

### 2.1 意图与角色

> **GoF**：定义对象间的一种一对多的依赖关系，当一个对象的状态发生改变时，所有依赖于它的对象都得到通知并被自动更新。

大白话：**被观察者（Subject）手里有一张名单，名单上的人都实现同一个接口；状态一变就照名单挨个通知。想听的自己 `attach`，不想听的自己 `detach`。** GUI 的事件监听器、Qt 信号槽、JS 的 `addEventListener` 本质上都是观察者。

三个角色：
- **Subject（被观察者）**：持有观察者名单，提供 `attach / detach` 并在变化时通知（`Player`）；
- **Observer（观察者接口）**：统一的回调接口（`PlayerObserver`）；
- **ConcreteObserver（具体观察者）**：收到通知后各自干活的模块（`ProgressBar`、`SubtitleView` 等）。

### 2.2 用观察者模式重构

定义 `PlayerObserver` 接口，`Player` 只维护一个 `std::vector<PlayerObserver*>`：

```cpp
// 《观察者模式》：Player 只认识 PlayerObserver 接口，各模块可随时订阅或退订
#include <algorithm>
#include <cstdio>
#include <vector>

enum class PlayState { Stopped, Playing, Paused };
const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }

class PlayerObserver {                            // 观察者接口：给空默认实现，子类只重写关心的事件
public:
  virtual ~PlayerObserver() = default;
  virtual void onProgress(double /*pos*/, double /*dur*/) {}
  virtual void onStateChanged(PlayState) {}
};

class Player {                                    // 被观察者（Subject）
public:
  void attach(PlayerObserver* o) { observers_.push_back(o); }
  void detach(PlayerObserver* o) {
    auto it = std::find(observers_.begin(), observers_.end(), o);
    if (it != observers_.end()) observers_.erase(it);   // 先判 end() 再 erase
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

struct ProgressBar  : PlayerObserver { void onProgress(double p, double d) override { std::printf("  [进度条] %4.1f / %.1f s\n", p, d); } };
struct SubtitleView : PlayerObserver { void onProgress(double p, double)   override { std::printf("  [字幕]   找 %.1f s 的字幕\n", p); } };
struct LyricsView   : PlayerObserver { void onProgress(double p, double)   override { std::printf("  [歌词]   高亮 %.1f s 那一句\n", p); } };
struct StatsReporter : PlayerObserver {
  int ticks = 0;
  void onProgress(double, double) override { ++ticks; }
  void onStateChanged(PlayState s) override { std::printf("  [统计]   上报：%s（已收到 %d 次进度）\n", name(s), ticks); }
};

int main() {
  ProgressBar bar; SubtitleView sub; LyricsView lyrics; StatsReporter stats;
  Player player;
  player.attach(&stats); player.attach(&bar); player.attach(&sub); player.attach(&lyrics);

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

重构后加弹幕只需新增 `DanmakuView` 并在 `main.cpp` 里 `attach` 一行，`Player` 一行不动，依赖方向彻底倒置：

<figure class="diagram">
<svg viewBox="0 0 730 300" role="img" aria-label="两张依赖图。左边硬编码版：Player 直接指向进度条、字幕、歌词、统计上报，新加的弹幕也要从 Player 连一条线过去。右边观察者版：Player 只指向 PlayerObserver 接口，五个模块都实现这个接口，箭头从模块指向接口，新加弹幕不碰 Player"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--warn);font-size:12.5px;font-weight:700">硬编码版：Player 认识每一个模块</text><text x="390.0" y="20.0" text-anchor="start" style="fill:var(--accent);font-size:12.5px;font-weight:700">观察者版：Player 只认识一个接口</text><rect x="20" y="120" width="90" height="36" rx="5" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.4"/><text x="65.0" y="142.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Player</text><rect x="200" y="40" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="265.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">ProgressBar</text><line x1="110.0" y1="138.0" x2="191.4" y2="62.1" style="stroke:var(--warn);stroke-width:1.4"/><path d="M198.0,56.0 L188.0,58.5 L194.8,65.8 z" style="fill:var(--warn);stroke:var(--warn);stroke-width:1.2"/><rect x="200" y="84" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="265.0" y="104.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">SubtitleView</text><line x1="110.0" y1="138.0" x2="189.7" y2="103.6" style="stroke:var(--warn);stroke-width:1.4"/><path d="M198.0,100.0 L187.8,99.0 L191.7,108.2 z" style="fill:var(--warn);stroke:var(--warn);stroke-width:1.2"/><rect x="200" y="128" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="265.0" y="148.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">LyricsView</text><line x1="110.0" y1="138.0" x2="189.0" y2="143.4" style="stroke:var(--warn);stroke-width:1.4"/><path d="M198.0,144.0 L189.4,138.4 L188.7,148.4 z" style="fill:var(--warn);stroke:var(--warn);stroke-width:1.2"/><rect x="200" y="172" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="265.0" y="192.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">StatsReporter</text><line x1="110.0" y1="138.0" x2="190.2" y2="183.6" style="stroke:var(--warn);stroke-width:1.4"/><path d="M198.0,188.0 L192.6,179.2 L187.7,187.9 z" style="fill:var(--warn);stroke:var(--warn);stroke-width:1.2"/><rect x="200" y="216" width="130" height="32" rx="5" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.4"/><text x="265.0" y="236.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px">DanmakuView</text><line x1="110.0" y1="138.0" x2="191.8" y2="225.4" style="stroke:var(--danger);stroke-width:1.4"/><path d="M198.0,232.0 L195.5,222.0 L188.2,228.8 z" style="fill:var(--danger);stroke:var(--danger);stroke-width:1.2"/><text x="200.0" y="275.0" text-anchor="start" style="fill:var(--danger);font-size:10.5px">加弹幕：Player 的构造函数、成员、</text><text x="200.0" y="290.0" text-anchor="start" style="fill:var(--danger);font-size:10.5px">tick() 和 #include 都要改</text><rect x="390" y="40" width="90" height="36" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="435.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Player</text><rect x="540" y="36" width="170" height="44" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="625.0" y="52.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">«interface»</text><text x="625.0" y="70.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">PlayerObserver</text><line x1="480.0" y1="58.0" x2="529.0" y2="58.0" style="stroke:var(--accent);stroke-width:1.4"/><path d="M538.0,58.0 L529.0,53.0 L529.0,63.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><text x="509.0" y="50.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">*</text><rect x="390" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="420.0" y="219.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Progress</text><line x1="420.0" y1="190.0" x2="552.9" y2="86.5" style="stroke:var(--muted);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M560.0,81.0 L549.8,82.6 L556.0,90.5 z" style="fill:var(--bg);stroke:var(--muted);stroke-width:1.2"/><rect x="456" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="486.0" y="219.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Subtitle</text><line x1="486.0" y1="190.0" x2="586.7" y2="87.4" style="stroke:var(--muted);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M593.0,81.0 L583.1,83.9 L590.3,90.9 z" style="fill:var(--bg);stroke:var(--muted);stroke-width:1.2"/><rect x="522" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="552.0" y="219.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Lyrics</text><line x1="552.0" y1="190.0" x2="620.9" y2="88.4" style="stroke:var(--muted);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M626.0,81.0 L616.8,85.6 L625.1,91.3 z" style="fill:var(--bg);stroke:var(--muted);stroke-width:1.2"/><rect x="588" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="618.0" y="219.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Stats</text><line x1="618.0" y1="190.0" x2="655.8" y2="89.4" style="stroke:var(--muted);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M659.0,81.0 L651.2,87.7 L660.5,91.2 z" style="fill:var(--bg);stroke:var(--muted);stroke-width:1.2"/><rect x="654" y="190" width="60" height="50" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="684.0" y="219.0" text-anchor="middle" style="fill:var(--accent);font-size:11.5px">Danmaku</text><line x1="684.0" y1="190.0" x2="691.3" y2="90.0" style="stroke:var(--accent);stroke-width:1.4;stroke-dasharray:5 4"/><path d="M692.0,81.0 L686.4,89.6 L696.3,90.3 z" style="fill:var(--bg);stroke:var(--accent);stroke-width:1.2"/><text x="390.0" y="275.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">加弹幕：新写一个类实现接口，</text><text x="390.0" y="290.0" text-anchor="start" style="fill:var(--accent);font-size:10.5px">main() 里 attach 一下；Player 不动</text></g></svg>
</figure>

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

解耦的代价是「谁会被调用」只有运行时才知道，调试时要打断点看名单；另外上面这张同步循环时序图里，直接藏着 C++ 里最容易踩的两个大坑。

## 3. C++ 里的两大坑与设计取舍

### 3.1 坑一：悬空观察者（析构没退订）

在带 GC 的语言里忘了退订只是内存泄漏；在 C++ 里，名单存的是裸指针，面板关掉析构了却没 `detach`，下次 `tick()` 就会对已释放对象调虚函数：

<!-- run: off -->
```cpp
{
  auto panel = std::make_unique<SubtitlePanel>();
  player.attach(panel.get());
  player.tick(1.0);
}                              // panel 析构了，没调用 detach
player.tick(2.0);              // 读取已释放内存的 8 字节虚表指针 → 段错误 / heap-use-after-free
```

用 `-fsanitize=address` 跑 `projects/dp-lab/19-observer/dangling.cpp`，Clang 和 GCC 都会直接报 `heap-use-after-free`（`READ of size 8`，即虚表指针）。

如果在 `~SubtitlePanel()` 里写 `player_.detach(this)`，又会反过来埋下另一个坑：一旦 `Player` 比面板先析构，`player_` 引用照样悬空。彻底解决双方生命周期牵制，需要第 4 节的 `weak_ptr` 或 RAII `Connection`。

### 3.2 坑二：通知途中增删观察者与重入

**1）通知途中 `detach` / `attach`（迭代器失效）**：比如「首帧统计」在 `onProgress` 里上报完就 `detach(this)`，或者「统计模块」第一次收到进度时顺手 `attach(&stallDetector)`。`Player` 此时正用 range-for 遍历 `observers_`：

<figure class="diagram">
<svg viewBox="0 0 640 276" role="img" aria-label="vector 里有首帧统计、进度条、字幕、歌词四个指针，迭代器指向第 0 个。首帧统计在回调里 erase 自己之后，后三个前移一格，迭代器 ++ 后指向第 1 个「字幕」，进度条被跳过；range-for 缓存的 end 仍在旧位置，第 3 格残留的歌词又被调用一次"><g style="font-family:var(--mono)"><text x="138.0" y="62.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px;font-weight:700">erase 之前</text><rect x="150" y="40" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--link);stroke-width:1.5"/><text x="193.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">首帧统计</text><text x="193.0" y="35.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[0]</text><rect x="242" y="40" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="285.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">进度条</text><text x="285.0" y="35.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[1]</text><rect x="334" y="40" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="377.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">字幕</text><text x="377.0" y="35.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[2]</text><rect x="426" y="40" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="469.0" y="62.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">歌词</text><text x="469.0" y="35.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[3]</text><text x="193.0" y="96.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">↑ it</text><text x="516.0" y="96.0" text-anchor="start" style="fill:var(--muted);font-size:11px">↑ 缓存的 end</text><text x="138.0" y="162.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px;font-weight:700">erase 之后</text><rect x="150" y="140" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="193.0" y="162.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">进度条</text><text x="193.0" y="135.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[0]</text><rect x="242" y="140" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="285.0" y="162.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">字幕</text><text x="285.0" y="135.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[1]</text><rect x="334" y="140" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.5"/><text x="377.0" y="162.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">歌词</text><text x="377.0" y="135.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[2]</text><rect x="426" y="140" width="86" height="34" rx="4" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.5;stroke-dasharray:4 3"/><text x="469.0" y="162.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px">歌词</text><text x="469.0" y="135.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[3]</text><text x="285.0" y="196.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">↑ ++it</text><text x="516.0" y="196.0" text-anchor="start" style="fill:var(--danger);font-size:11px">↑ 缓存的 end 没变</text><text x="469.0" y="214.0" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">size 之外的旧值</text><text x="150.0" y="244.0" text-anchor="start" style="fill:var(--fg-2);font-size:11px">后面的元素整体前移一格，it 回到循环里马上 ++，「进度条」被跳过；</text><text x="150.0" y="262.0" text-anchor="start" style="fill:var(--fg-2);font-size:11px">range-for 一开始就记下了 end，还会多走一格，读到 size 之外残留的「歌词」。</text></g></svg>
</figure>

- **途中 `erase`**（`iterate_detach.cpp`）：进度条被跳过、歌词收到两次。Clang/libc++ 开 ASan 会报 `container-overflow`；GCC/libstdc++ 默认不报，要加 `-D_GLIBCXX_SANITIZE_VECTOR` 才抓得到。
- **途中 `push_back`**（`iterate_attach.cpp`）：名单刚好满（如 `size == capacity == 4`）时扩容搬家，外层迭代器指着已释放的旧内存，直接 `heap-use-after-free` 段错误。

下面的交互演示一步步走一遍 `erase` 失效和第 4 节的「延迟删除」：

<div class="dsviz" data-viz="dp-observer"></div>

**2）重入（回调里又改 Subject 状态）**：没有任何未定义行为，但事件顺序会乱。比如付费片试看结束：`TrialGuard` 收到「播放」通知，立刻调 `player.setState(暂停)`，结果按钮先收到「暂停」再收到外层迟到的「播放」，最后界面停在「播放」而播放器实际已经暂停（完整复现见 `reentrant.cpp`）：

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

解法是**排队通知**（类似 Qt 的 `Qt::QueuedConnection`）：如果正在通知中，新事件只入队，等最外层循环按顺序派发。

### 3.3 通知顺序、推模型与拉模型

- **不要依赖通知顺序**：用 `std::set<PlayerObserver*>` 去重时顺序取决于对象指针地址，换个创建顺序或平台就会变（`order.cpp`）。观察者之间有先后依赖，通常说明应该换中介者。
- **推模型 vs 拉模型**（完整对比见 `push_pull.cpp`）：

| | 推模型（`onProgress(pos, dur)`） | 拉模型（`onChanged(const Player&, Changed)`） |
| --- | --- | --- |
| 耦合与扩展 | 观察者不认识 Subject；但加字段要改接口签名 | 观察者依赖 Subject 查询接口；加数据只加查询函数 |
| 数据一致性 | 通知时的快照，适合跨线程/跨进程 | 拉取时状态可能已被重入或其他线程改掉 |

折中做法是推一个**事件结构体** `onProgress(const ProgressEvent&)`，加字段不用改签名。

## 4. 现代 C++ 写法与性能

### 4.1 `weak_ptr` 与 `Signal` + RAII `Connection`

如果观察者全由 `shared_ptr` 管理，Subject 可以存 `weak_ptr` 并先复制快照再 `lock()` 调用（`weak_observer.cpp`）：

<!-- run: off -->
```cpp
void tick(double pos) {
  auto snapshot = observers_;                     // 快照：回调里增删不影响本轮遍历
  for (auto& w : snapshot)
    if (auto o = w.lock()) o->onProgress(pos);    // lock 成功期间观察者不会被析构
  observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
      [](const auto& w) { return w.expired(); }), observers_.end());
}
```

但 `weak_ptr` 强迫所有观察者必须堆分配并由 `shared_ptr` 管理，栈对象和成员对象都用不了。更通用的做法是把「订阅」做成 RAII 句柄 `Connection`（类似 Boost.Signals2 / sigslot），析构自动退订，同时用**下标遍历 + 延迟删除**一次解决两大坑：

```cpp
// 《观察者模式》：Signal + RAII Connection——自动退订、Subject 先析构安全、通知中途增删安全
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Connection {                                // 订阅句柄：仅可移动，析构自动退订
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

template <class... Args>
class Signal {
public:
  [[nodiscard]] Connection connect(std::function<void(Args...)> fn) {
    auto slot = std::make_shared<Slot>(Slot{std::move(fn), true});
    st_->slots.push_back(slot);
    std::weak_ptr<State> ws = st_;                // 弱引用：Signal 先析构也没事
    std::weak_ptr<Slot> wsl = slot;
    return Connection([ws, wsl] {
      if (auto st = ws.lock(); auto sl = wsl.lock()) { sl->alive = false; st->compact(); }
    });
  }
  void emit(Args... args) {
    auto st = st_;
    ++st->depth;
    for (std::size_t i = 0, n = st->slots.size(); i < n; ++i) {   // 下标抗扩容，新订阅下轮生效
      auto slot = st->slots[i];                   // 延长 slot 生命，防回调里断开自己时析构 fn
      if (slot->alive) slot->fn(args...);
    }
    --st->depth;
    st->compact();
  }
  std::size_t size() const { return st_->slots.size(); }
private:
  struct Slot { std::function<void(Args...)> fn; bool alive; };
  struct State {
    std::vector<std::shared_ptr<Slot>> slots;
    int depth = 0;
    void compact() {                              // 延迟删除：最外层 emit 结束才真正 erase
      if (depth > 0) return;
      slots.erase(std::remove_if(slots.begin(), slots.end(), [](auto& s) { return !s->alive; }), slots.end());
    }
  };
  std::shared_ptr<State> st_ = std::make_shared<State>();
};

class SubtitlePanel {
public:
  explicit SubtitlePanel(Signal<double>& sig)
      : conn_(sig.connect([this](double pos) { std::printf("  [字幕] %.1f s：%s\n", pos, line_.c_str()); })) {}
private:
  std::string line_ = "Big Buck Bunny wakes up.";
  Connection conn_;                               // 放最后：最先析构，先退订再销毁 line_
};

int main() {
  Signal<double> progress;
  Connection bar = progress.connect([](double pos) { std::printf("  [进度条] %.1f s\n", pos); });
  Connection first;
  first = progress.connect([&first](double pos) { std::printf("  [首帧] %.1f s，上报后退订\n", pos); first.disconnect(); });

  std::optional<SubtitlePanel> panel;
  panel.emplace(progress);
  progress.emit(0.0);
  panel.reset();                                  // 面板析构，自动退订
  progress.emit(1.0);
}
```

```text
  [进度条] 0.0 s
  [首帧] 0.0 s，上报后退订
  [字幕] 0.0 s：Big Buck Bunny wakes up.
  [进度条] 1.0 s
```

两个关键细节：
- `connect` 必须标 `[[nodiscard]]`：不接住返回值，临时 `Connection` 当场析构，订阅立刻失效；
- `Connection` 成员要放在类的**最后**：按声明逆序析构，确保先断开订阅、再销毁回调会用到的其他成员。Qt 的 `QObject` 在析构时自动断开所有连接，本质上也是这个机制。

### 4.2 开销实测与多线程注意

8 个观察者（每个只做一次加法），`projects/dp-lab/19-observer/bench_notify.cpp` 在 `-O2` 下跑 200 万次通知（7 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 740 246" role="img" aria-label="8 个观察者，每次通知的耗时。Clang：硬编码 0.80 ns，虚函数 5.07，std::function 8.69，Signal 14.06，weak_ptr::lock 12.17，weak_ptr 加每次复制快照 35.03。GCC：0.84、5.03、7.20、14.30、11.76、20.37"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="198.0" y="30.0" width="4.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="208.0" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.80 ns</text><rect x="198.0" y="60.0" width="25.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="229.3" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.07 ns</text><rect x="198.0" y="90.0" width="43.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="247.4" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">8.69 ns</text><rect x="198.0" y="120.0" width="70.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="274.3" y="135.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">14.06 ns</text><rect x="198.0" y="150.0" width="60.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="264.9" y="165.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">12.17 ns</text><rect x="198.0" y="180.0" width="175.2" height="20.0" rx="2" style="fill:var(--danger)"/><text x="379.1" y="195.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">35.03 ns</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="468.0" y="30.0" width="4.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="478.2" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.84 ns</text><rect x="468.0" y="60.0" width="25.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="499.1" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.03 ns</text><rect x="468.0" y="90.0" width="36.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="510.0" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">7.20 ns</text><rect x="468.0" y="120.0" width="71.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="545.5" y="135.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">14.30 ns</text><rect x="468.0" y="150.0" width="58.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="532.8" y="165.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">11.76 ns</text><rect x="468.0" y="180.0" width="101.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="575.9" y="195.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">20.37 ns</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">硬编码直接调用</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数 vector&lt;T*&gt;</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::function</text><text x="190.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Signal（复制 shared_ptr）</text><text x="190.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">weak_ptr::lock</text><text x="190.0" y="195.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">weak_ptr + 复制快照</text><text x="10.0" y="238.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_notify：-O2，每轮 200 万次通知，7 轮取中位数；每个观察者只做一次加法</text></g></svg>
</figure>

`Signal` 和 `weak_ptr` 慢在引用计数的原子操作，每次复制快照最慢（多了堆分配）：

| 写法（8 个观察者，每次通知，ns） | Clang / macOS | GCC 13 / Ubuntu | 说明 |
| --- | ---: | ---: | --- |
| 硬编码直接调用 | 0.80 | 0.84 | 全部内联，无间接调用 |
| 虚函数接口 `vector<T*>` | 5.07 | 5.03 | 每观察者约 0.6 ns |
| `vector<std::function>` | 8.69 | 7.20 | 类型擦除间接调用 |
| `weak_ptr::lock` | 12.17 | 11.76 | 每次 `lock` 伴随原子加减 |
| `Signal`（复制 `shared_ptr<Slot>`） | 14.06 | 14.30 | 回调期间保证 `Slot` 存活 |
| `weak_ptr` + 每次复制快照 | 35.03 | 20.37 | 额外一次堆分配 + 8 次原子加减 |

界面进度每秒只发几十次，哪怕最慢的写法每秒也只多花 1 µs——**选哪种看生命周期好不好管，不必纠结这几个纳秒**。只有每秒几百万次的热路径（如逐音频采样）才应避开观察者。

**多线程下的三条结论**（完整压测见 `projects/dp-lab/19-observer/threaded.cpp`）：
1. **绝不在锁内调用回调**：回调里若直接或经由 UI 线程再次 `subscribe`，会立刻死锁。
2. **名单少变、通知频繁时用写时复制（COW）**：把名单存成 `shared_ptr<const vector<...>>`，通知时锁内只复制一个指针；实测 8 个观察者各算 ~100 ns，平均每次通知耗时：

| 策略（ns/次，总耗时 ÷ 总通知数） | Clang 1 线程 | Clang 2 线程 | GCC 1 线程 | GCC 2 线程 |
| --- | ---: | ---: | ---: | ---: |
| 锁住整个通知 | 285.5 | 574.7 | 396.8 | 807.0 |
| 锁内复制快照 | 360.0 | 437.4 | 454.1 | 442.7 |
| **写时复制（COW）** | **294.0** | **224.4** | **410.0** | **232.7** |

3. **锁外回调的代价**：另一线程手里可能刚拿到旧快照，所以观察者**在退订返回后仍可能被调用一次**，逻辑必须能容忍，或用额外同步等待进行中的通知结束。

## 5. 观察者和相似的模式

<figure class="diagram">
<svg viewBox="0 0 860 244" role="img" aria-label="三张小图。观察者：Player 直接指向进度条、字幕、统计。发布-订阅：Player 和 Downloader 把事件发给 Bus，Bus 转给弹幕、统计、清晰度，双方互不认识。中介者：播放按钮、进度条、音量、字幕都和中间的 Mediator 双向连线，彼此之间不直接通信"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">观察者</text><text x="20.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">被观察者直接持有观察者列表</text><rect x="20" y="100" width="80" height="34" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="60.0" y="121.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Player</text><rect x="160" y="56" width="70" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="195.0" y="75.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">进度条</text><line x1="100.0" y1="117.0" x2="150.9" y2="76.6" style="stroke:var(--accent);stroke-width:1.4"/><path d="M158.0,71.0 L147.8,72.7 L154.1,80.5 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><rect x="160" y="108" width="70" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="195.0" y="127.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">字幕</text><line x1="100.0" y1="117.0" x2="149.0" y2="122.1" style="stroke:var(--accent);stroke-width:1.4"/><path d="M158.0,123.0 L149.6,117.1 L148.5,127.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><rect x="160" y="160" width="70" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="195.0" y="179.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">统计</text><line x1="100.0" y1="117.0" x2="151.6" y2="168.6" style="stroke:var(--accent);stroke-width:1.4"/><path d="M158.0,175.0 L155.2,165.1 L148.1,172.2 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><text x="20.0" y="230.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">一对多；订阅者知道 Player</text><text x="270.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">发布-订阅（事件总线）</text><text x="270.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">双方只认识总线和事件类型</text><rect x="270" y="70" width="84" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="312.0" y="89.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Player</text><line x1="354.0" y1="85.0" x2="381.4" y2="110.8" style="stroke:var(--link);stroke-width:1.4"/><path d="M388.0,117.0 L384.9,107.2 L378.0,114.5 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><rect x="270" y="130" width="84" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="312.0" y="149.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Downloader</text><line x1="354.0" y1="145.0" x2="381.1" y2="122.7" style="stroke:var(--link);stroke-width:1.4"/><path d="M388.0,117.0 L377.9,118.9 L384.2,126.6 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><rect x="390" y="96" width="60" height="42" rx="5" style="fill:var(--panel);stroke:var(--link);stroke-width:1.4"/><text x="420.0" y="121.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Bus</text><rect x="500" y="56" width="60" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="530.0" y="75.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">弹幕</text><line x1="450.0" y1="117.0" x2="491.5" y2="77.2" style="stroke:var(--link);stroke-width:1.4"/><path d="M498.0,71.0 L488.0,73.6 L495.0,80.8 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><rect x="500" y="108" width="60" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="530.0" y="127.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">统计</text><line x1="450.0" y1="117.0" x2="489.1" y2="121.9" style="stroke:var(--link);stroke-width:1.4"/><path d="M498.0,123.0 L489.7,116.9 L488.4,126.8 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><rect x="500" y="160" width="60" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="530.0" y="179.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">清晰度</text><line x1="450.0" y1="117.0" x2="492.3" y2="168.1" style="stroke:var(--link);stroke-width:1.4"/><path d="M498.0,175.0 L496.1,164.9 L488.4,171.3 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><text x="270.0" y="230.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">多对多；发布者不知道谁在听</text><text x="600.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">中介者</text><text x="600.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">同事之间的协作逻辑集中在中介者里</text><rect x="670" y="98" width="84" height="38" rx="5" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.4"/><text x="712.0" y="121.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">Mediator</text><rect x="600" y="56" width="74" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="637.0" y="75.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">播放按钮</text><line x1="637.0" y1="86.0" x2="712.0" y2="98.0" style="stroke:var(--warn);stroke-width:1.4"/><rect x="750" y="56" width="74" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="787.0" y="75.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">进度条</text><line x1="787.0" y1="86.0" x2="712.0" y2="98.0" style="stroke:var(--warn);stroke-width:1.4"/><rect x="600" y="160" width="74" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="637.0" y="179.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">音量</text><line x1="637.0" y1="160.0" x2="712.0" y2="136.0" style="stroke:var(--warn);stroke-width:1.4"/><rect x="750" y="160" width="74" height="30" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="787.0" y="179.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">字幕</text><line x1="787.0" y1="160.0" x2="712.0" y2="136.0" style="stroke:var(--warn);stroke-width:1.4"/><text x="600.0" y="230.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">多对多的双向调用，中介者知道每个同事</text></g></svg>
</figure>

| 模式 | 关系与谁认识谁 | 业务在哪 / 典型场景 |
| --- | --- | --- |
| **观察者** | 一对多；观察者认识 Subject，Subject 只认识接口 | 在各观察者里；一处变化多处更新 |
| **发布-订阅** | 多对多；双方只认识 `EventBus` 和事件类型 | 在各订阅者里；跨层/插件彻底解耦（见 `event_bus.cpp`） |
| **中介者** | 多对多双向；中介者认识所有同事，同事只认识中介者 | **集中在中介者里**；复杂协作规则（如拖动时先暂停再恢复） |
| **策略** | 一对一；Context 调一个策略完成自己的工作 | 在策略里；同一件事的多种可互换算法 |

发布-订阅（事件总线）的核心只需十几行——按事件类型 `std::type_index` 路由，发布者与订阅者互相零 `#include`（代价是调用关系藏在运行时，排查全靠日志）：

<!-- run: off -->
```cpp
class EventBus {
public:
  template <class E> void subscribe(std::function<void(const E&)> fn) {
    handlers_[typeid(E)].push_back([fn = std::move(fn)](const void* e) { fn(*static_cast<const E*>(e)); });
  }
  template <class E> void publish(const E& e) {
    auto it = handlers_.find(typeid(E));
    if (it == handlers_.end()) return;
    auto copy = it->second;                       // 快照遍历，防止回调里再订阅导致失效
    for (auto& h : copy) h(&e);
  }
private:
  std::unordered_map<std::type_index, std::vector<std::function<void(const void*)>>> handlers_;
};
```

## 6. 小结

- 观察者模式用依赖倒置把 Subject 和具体模块解耦：加弹幕时 `Player` 零改动，代价是一层间接调用（8 个观察者 5.07 ns vs 硬编码 0.80 ns）和调试间接性。
- C++ 里最容易踩两个坑：**悬空观察者**（`heap-use-after-free`）和**通知途中增删/重入**（迭代器失效、事件乱序）。
- 现代 C++ 推荐 `Signal + RAII Connection`：弱引用防 Subject 先死、下标遍历抗扩容、延迟删除防跳漏、`[[nodiscard]]` 防临时句柄失效。
- 多线程下绝不在锁内调回调；读多写少用写时复制（COW），同时容忍退订后可能再收到一次通知。

## 7. 练习

1. **辨析**：下面四种设计分别更像观察者、发布-订阅、中介者还是策略？
   - (a) 解码器只有 `setFrameSink(FrameSink*)`，每次解出一帧就交给这一个 sink；
   - (b) 下载器每下完一个分片，调用所有注册过的 `ProgressListener::onChunk(done, total)`；
   - (c) `PlayerController` 接收按钮、进度条、音量条操作，统一决定「拖动进度条时先暂停、松手后 seek 再恢复」；
   - (d) 日志模块向全局 `EventBus` 订阅 `ErrorEvent`，任意模块出错都往总线发该事件。
2. **设计**：播放器每帧都发进度通知（24 fps）。进度条每帧都要；统计只要每秒一次；字幕只在跨过下一句时间戳时才需要。如何在**不改动 `Player`** 的前提下满足这三种频率？
3. **找问题**：下面这段基于 `Signal + Connection` 的代码本应输出一行进度条和两行字幕，实际进度条一次没输出，第一行字幕还读到了已释放内存。两个 bug 分别在哪？怎么改？

<!-- run: off -->
```cpp
class SubtitlePanel {
public:
  SubtitlePanel(Player& p, std::string lang)
      : lang_(std::move(lang)),
        conn_(p.progressChanged.connect([this](double pos) { std::printf("  [字幕·%s] %.1f s\n", lang_.c_str(), pos); })) {}
private:
  std::string lang_;
  Connection conn_;
};

int main() {
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

1. (a) **策略/单回调**（一对一，而非一对多名单）；
   (b) **观察者**（推模型）；
   (c) **中介者**（协作规则集中在 `PlayerController`）；
   (d) **发布-订阅**（事件总线，发布者与订阅者互不认识）。

2. 把节流和边界过滤做成包在回调外面的高阶函数（装饰器思路），`Player` 照旧每帧通知即可：

   <!-- run: off -->
   ```cpp
   using ProgressFn = std::function<void(double)>;
   ProgressFn throttle(double interval, ProgressFn f) {
     return [interval, f = std::move(f), last = -1e9](double pos) mutable {
       if (pos - last >= interval - 1e-9) { last = pos; f(pos); }
     };
   }
   ```

   完整 72 帧模拟程序（含 `on_crossing`）见 `projects/dp-lab/19-observer/ex3_throttle.cpp`；通知途中安全增删的 `Player` 修复版见 `ex2_fixed.cpp`。

3. 两个 bug：
   - **进度条没接住 `connect()` 返回的 `Connection`**：临时句柄在该行末尾析构，订阅当场断开。改为保存 `Connection bar = ...`；
   - **`SubtitlePanel` 捕获了 `this` 却放进 `std::vector`**：第二次 `emplace_back` 扩容把第一个面板搬到新内存，旧地址析构，但 lambda 里捕获的 `this` 仍指向旧内存（`heap-use-after-free`）。改为禁止 `SubtitlePanel` 拷贝/移动，并用 `std::vector<std::unique_ptr<SubtitlePanel>>` 存放（完整修复版见 `projects/dp-lab/19-observer/ex4_fixed.cpp`）。

</details>
