---
title: 责任链模式
date: 2026-11-12 10:00:00
updated: 2026-11-12 10:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 责任链模式
  - 中间件
  - FFmpeg
  - 硬件解码
order: 7
description: 播放器的按键要在对话框、字幕菜单、播放器之间分配，全写在一个 onKey 里，每个按键分支都要判断哪些面板开着。加一个「播放列表」面板，这个版本改了 5 处，判断面板开关的 if 从 5 个变成 8 个，还改错了两处：把各面板开关组合 × 7 个键的 56 格逐格对照规格，有 2 格不对。改成责任链（每一层一个处理者，处理不了就交给下一个）后，加播放列表只写一个新类、改一行组装代码，56 格全对。再用 FFmpeg 写一条解码器回退链 VideoToolbox → VAAPI → CUDA → 软解：macOS 第一环就用上硬解，解码 240 帧的 CPU 时间 31.6 毫秒（软解 283 毫秒）；Linux 容器里前三环都失败，交到链尾软解。还实测了链接成环（-O0 栈溢出，-O2 下 Clang 直接返回「没人处理」、GCC 死循环）、链里的悬空指针，以及四种写法的开销（Clang 0.29～8.78 纳秒，GCC 3.77～9.26 纳秒）。最后比较责任链和装饰器、观察者等模式的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-chain]
---

> **本文目标**：理解责任链模式——把能处理请求的对象连成一条链，请求沿链传递直到有对象处理；会把「一个函数判断到底」的分发代码重构成责任链；分清纯与不纯的责任链，掌握回退链（FFmpeg 硬解回退）和三大坑（链尾、成环、悬空指针）；会用现代 C++ 的 `vector<std::function>` 与洋葱中间件写法，了解四种写法的开销对比；分清责任链与装饰器、观察者、状态的区别。
>
> **前置知识**：上一篇《状态模式》；《装饰器模式》《模板方法模式》；FFmpeg 解码基本流程。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整规格测试、FFmpeg 硬解回退与压测代码见 `projects/dp-lab/24-chain/`。

## 1. 问题：一个函数管所有按键

### 1.1 三层界面抢按键

播放器全屏时可能同时叠着三层界面：最上面的**模态对话框**（开着时吞掉所有按键）、中间的**字幕菜单**（只管 ↑ ↓ 回车 Esc，空格和 ← → 照常给播放器）、最底下的**播放器**本身（空格播停、← → 快进快退、↑ ↓ 调音量、Esc 退全屏）。

最直接的写法是一个 `onKey` 函数：先按键分支，每个分支里再判断哪些层开着：

```cpp
// 《责任链模式》：问题版——一个 onKey 按键分支，每个分支里再判断各层开关
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

struct Player {
  bool dialogOpen = false, menuOpen = false, playing = false, fullscreen = true;
  int volume = 50, menuSel = 0;
  double pos = 0;

  std::string onKey(Key k) {
    if (dialogOpen) {  // 模态对话框：开着时吞掉所有按键
      if (k == Key::Enter) { dialogOpen = false; return "对话框：继续播放"; }
      if (k == Key::Esc) { dialogOpen = false; return "对话框：从头播放"; }
      return "对话框：忽略";
    }
    switch (k) {
      case Key::Space: playing = !playing; return playing ? "播放器：播放" : "播放器：暂停";
      case Key::Esc:
        if (menuOpen) { menuOpen = false; return "字幕菜单：关闭"; }
        fullscreen = false; return "播放器：退出全屏";
      case Key::Up:
        if (menuOpen) { --menuSel; return "字幕菜单：上移"; }
        volume += 5; return "播放器：音量 +5";
      case Key::Down:
        if (menuOpen) { ++menuSel; return "字幕菜单：下移"; }
        volume -= 5; return "播放器：音量 -5";
      case Key::Left: pos -= 5; return "播放器：快退 5 秒";
      case Key::Right: pos += 5; return "播放器：快进 5 秒";
      case Key::Enter:
        if (menuOpen) { menuOpen = false; return "字幕菜单：选中"; }
        return "没人处理";
    }
    return "没人处理";
  }
};

int main() {
  Player p;
  p.dialogOpen = true;
  std::printf("对话框开 按空格：%s\n", p.onKey(Key::Space).c_str());
  std::printf("对话框开 按回车：%s\n", p.onKey(Key::Enter).c_str());
  p.menuOpen = true;
  std::printf("菜单开   按 ↑  ：%s\n", p.onKey(Key::Up).c_str());
  std::printf("菜单开   按空格：%s\n", p.onKey(Key::Space).c_str());
  std::printf("菜单开   按 Esc：%s\n", p.onKey(Key::Esc).c_str());
  std::printf("菜单关   按 ↑  ：%s\n", p.onKey(Key::Up).c_str());
}
```

```text
对话框开 按空格：对话框：忽略
对话框开 按回车：对话框：继续播放
菜单开   按 ↑  ：字幕菜单：上移
菜单开   按空格：播放器：播放
菜单开   按 Esc：字幕菜单：关闭
菜单关   按 ↑  ：播放器：音量 +5
```

### 1.2 加一个「播放列表」：散落修改与优先级错位

现在在字幕菜单和播放器之间插入一个**播放列表**面板（同样管 ↑ ↓ 回车 Esc）。在 `onKey` 里，播放列表没有属于自己的地方，只能拆成四份塞进四个 `case`（完整程序见 `projects/dp-lab/24-chain/key_switch_playlist.cpp`）：

<!-- run: off -->
```diff
   bool menuOpen = false;
+  bool listOpen = false;
       case Key::Esc:
         if (menuOpen) { menuOpen = false; return "字幕菜单：关闭"; }
+        if (listOpen) { listOpen = false; return "播放列表：关闭"; }
       case Key::Up:
+        if (listOpen) { --listSel; return "播放列表：上移"; }     // 误写到了 menuOpen 前面
         if (menuOpen) { --menuSel; return "字幕菜单：上移"; }
       case Key::Down:                                              // 漏改了 ↓ 分支
         if (menuOpen) { ++menuSel; return "字幕菜单：下移"; }
       case Key::Enter:
         if (menuOpen) { menuOpen = false; return "字幕菜单：选中"; }
+        if (listOpen) { listOpen = false; return "播放列表：播放选中项"; }
```

`measure.sh` 实测统计：这次改动动了 **5 处**，判断面板开关的 `if` 从 5 个涨到 **8 个**；用 8 种开关组合 × 7 个键 = **56 格**逐格对照规格表，出现 **2 格错误**（菜单和列表同时开时按 ↑ 被列表抢走；列表开时按 ↓ 漏给了播放器调音量）。

根源在于 `onKey` 按**键**组织，而需求变化是按**层**来的：层的逻辑散落在各个分支里，层的优先级没有在一处写清。

## 2. 责任链模式

### 2.1 意图与角色

> **GoF**：使多个对象都有机会处理请求，从而避免请求的发送者和接收者之间的耦合关系。将这些对象连成一条链，并沿着这条链传递该请求，直到有一个对象处理它为止。

大白话：**把能处理请求的对象排成一队，请求从队头依次询问「归不归你管？」——管就处理并停下，不管就交给下一个。**

三个角色：
- **Handler（处理者）**：定义处理接口，保存下一个处理者 `next_`，默认实现是转发给 `next_`；
- **ConcreteHandler（具体处理者）**：能处理就处理，不能就调用基类默认转发；
- **Client（客户）**：只认识链头，不知道最终由谁处理。

### 2.2 每层一个处理者

```cpp
// 《责任链模式》：按键沿「对话框 → 字幕菜单 → 播放器」传递，每层只管自己的键
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

class KeyHandler {
 public:
  virtual ~KeyHandler() = default;
  KeyHandler* setNext(KeyHandler* next) { next_ = next; return next; }
  virtual std::string handle(Key k) { return next_ ? next_->handle(k) : "没人处理"; }
 private:
  KeyHandler* next_ = nullptr;
};

class Dialog : public KeyHandler {
 public:
  bool open = false;
  std::string handle(Key k) override {
    if (!open) return KeyHandler::handle(k);
    if (k == Key::Enter) { open = false; return "对话框：继续播放"; }
    if (k == Key::Esc) { open = false; return "对话框：从头播放"; }
    return "对话框：忽略";
  }
};

class SubtitleMenu : public KeyHandler {
 public:
  bool open = false;
  int sel = 0;
  std::string handle(Key k) override {
    if (open) switch (k) {
      case Key::Up: --sel; return "字幕菜单：上移";
      case Key::Down: ++sel; return "字幕菜单：下移";
      case Key::Enter: open = false; return "字幕菜单：选中";
      case Key::Esc: open = false; return "字幕菜单：关闭";
      default: break;
    }
    return KeyHandler::handle(k);
  }
};

class PlayerKeys : public KeyHandler {
 public:
  bool playing = false, fullscreen = true;
  int volume = 50;
  double pos = 0;
  std::string handle(Key k) override {
    switch (k) {
      case Key::Space: playing = !playing; return playing ? "播放器：播放" : "播放器：暂停";
      case Key::Esc: fullscreen = false; return "播放器：退出全屏";
      case Key::Up: volume += 5; return "播放器：音量 +5";
      case Key::Down: volume -= 5; return "播放器：音量 -5";
      case Key::Left: pos -= 5; return "播放器：快退 5 秒";
      case Key::Right: pos += 5; return "播放器：快进 5 秒";
      default: return KeyHandler::handle(k);
    }
  }
};

int main() {
  Dialog dialog; SubtitleMenu menu; PlayerKeys player;
  dialog.setNext(&menu)->setNext(&player);  // 层级顺序只写在这一行
  dialog.open = true;
  std::printf("对话框开 按空格：%s\n", dialog.handle(Key::Space).c_str());
  std::printf("对话框开 按回车：%s\n", dialog.handle(Key::Enter).c_str());
  menu.open = true;
  std::printf("菜单开   按 ↑  ：%s\n", dialog.handle(Key::Up).c_str());
  std::printf("菜单开   按空格：%s\n", dialog.handle(Key::Space).c_str());
  std::printf("菜单开   按 Esc：%s\n", dialog.handle(Key::Esc).c_str());
  std::printf("菜单关   按 ↑  ：%s\n", dialog.handle(Key::Up).c_str());
}
```

输出与第 1 节完全一致。每一层的逻辑收拢在自己的类里，层级优先级只写在 `dialog.setNext(&menu)->setNext(&player)` 这一行，模态对话框自然截断后续传递。

### 2.3 类图、时序图与交互演示

```uml
label: 责任链的类图：PlayerWindow 只依赖 KeyHandler；KeyHandler 保存下一个 KeyHandler（next_，0 或 1 个）；Dialog、SubtitleMenu、Playlist、PlayerKeys 继承 KeyHandler
classes:
  - { id: PlayerWindow, x: 20, y: 46, w: 120 }
  - { id: KeyHandler, stereo: abstract, x: 220, y: 10, w: 290, attrs: ['- next_ : KeyHandler*'], ops: ['+ setNext(n: KeyHandler*) : KeyHandler*', '+ handle(k: Key) : string'] }
  - { id: Dialog, x: 60, y: 200, w: 110, attrs: ['+ open : bool'] }
  - { id: SubtitleMenu, x: 200, y: 200, w: 120, attrs: ['+ open : bool'] }
  - { id: Playlist, x: 350, y: 200, w: 110, attrs: ['+ open : bool'] }
  - { id: PlayerKeys, x: 490, y: 200, w: 120, attrs: ['+ volume : int'] }
rels:
  - { from: PlayerWindow, to: KeyHandler, type: depend, label: '链头' }
  - { from: KeyHandler, to: KeyHandler, type: assoc, tm: '0..1', label: 'next_', points: [[521, 40], [580, 40], [580, 80], [521, 80]] }
  - { from: Dialog, to: KeyHandler, type: inherit, mid: 160 }
  - { from: SubtitleMenu, to: KeyHandler, type: inherit, mid: 160 }
  - { from: Playlist, to: KeyHandler, type: inherit, mid: 160 }
  - { from: PlayerKeys, to: KeyHandler, type: inherit, mid: 160 }
```

```uml
label: 字幕菜单开着时按空格的时序图：窗口调用 dialog.handle，对话框关着，转给 menu；菜单不管空格，转给 list；列表关着，转给 player；播放器处理空格并返回「播放器：播放」，结果沿原路返回
participants:
  - { id: w, name: 'PlayerWindow', x: 10, w: 110 }
  - { id: d, name: 'dialog', x: 170, w: 80 }
  - { id: m, name: 'menu', x: 300, w: 80 }
  - { id: l, name: 'list', x: 430, w: 80 }
  - { id: p, name: 'player', x: 560, w: 80 }
messages:
  - { from: w, to: d, text: 'handle(空格)', n: 1 }
  - { from: d, to: m, text: 'handle(空格)', n: 2, note: '关着，交给下一个' }
  - { from: m, to: l, text: 'handle(空格)', n: 3, note: '空格不归菜单管' }
  - { from: l, to: p, text: 'handle(空格)', n: 4, note: '关着，交给下一个' }
  - { from: p, to: l, text: '"播放器：播放"', type: reply }
  - { from: l, to: m, text: '', type: reply }
  - { from: m, to: d, text: '', type: reply }
  - { from: d, to: w, text: '', type: reply }
```

点下面演示里的按键，看请求怎么沿链传递或被截停：

<div class="dsviz" data-viz="dp-chain"></div>

### 2.4 再加「播放列表」

责任链加播放列表只需新写一个 `Playlist` 类，再改一行组装代码 `dialog.setNext(&menu)->setNext(&list)->setNext(&player);`，原有三个类一行不动（完整代码见 `projects/dp-lab/24-chain/key_chain_playlist.cpp`），56 格全对：

<figure class="diagram">
<svg viewBox="0 0 730 126" role="img" aria-label="加播放列表面板：一个 onKey 判断到底的版本改了 5 处，判断面板开关的 if 从 5 个变成 8 个，56 格里 2 格和规格不一致；责任链改了 1 处（新增一个类）加 1 行组装代码，没有 if，0 格不一致"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">diff 统计的改动处数</text><rect x="158.0" y="30.0" width="83.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="247.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5 处</text><rect x="158.0" y="60.0" width="16.7" height="20.0" rx="2" style="fill:var(--accent)"/><text x="180.7" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1 处</text><text x="348.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">onKey 里判断面板开关的 if</text><rect x="348.0" y="30.0" width="88.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="442.9" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8 个</text><rect x="348.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="355.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 个</text><text x="538.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">56 格里和规格不一致</text><rect x="538.0" y="30.0" width="66.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="610.7" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2 格</text><rect x="538.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="545.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 格</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">一个 onKey 判断到底</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">责任链</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh 只比较类的部分，责任链另需改 main 里 1 行组装代码；Clang、GCC 结果相同</text></g></svg>
</figure>

## 3. 深入责任链

### 3.1 纯与不纯：日志过滤链

按键分发是**纯责任链**（只被一环处理，处理完即停）。工程里更常见的是**不纯责任链**：每一环既可以截断请求，也可以加工后再传给下一环。例如模仿 FFmpeg `av_log` 的日志管线：**等级过滤 → 重复折叠 → 加模块前缀 → 输出**：

```cpp
// 《责任链模式》：不纯的责任链——日志沿「等级过滤 → 重复折叠 → 加前缀 → 输出」传递
#include <cstdio>
#include <string>

enum Level { kError = 16, kWarning = 24, kInfo = 32, kDebug = 48 };
struct LogMsg { int level; std::string module, text; };

class LogHandler {
 public:
  virtual ~LogHandler() = default;
  LogHandler* setNext(LogHandler* n) { next_ = n; return n; }
  virtual void log(LogMsg& m) { if (next_) next_->log(m); }
 private:
  LogHandler* next_ = nullptr;
};

struct LevelFilter : LogHandler {
  int level, dropped = 0;
  explicit LevelFilter(int lv) : level(lv) {}
  void log(LogMsg& m) override { if (m.level > level) { ++dropped; return; } LogHandler::log(m); }
};

struct RepeatFolder : LogHandler {
  std::string last;
  int repeat = 0, dropped = 0;
  void log(LogMsg& m) override {
    if (m.text == last) { ++repeat; ++dropped; return; }
    flush(); last = m.text; LogHandler::log(m);
  }
  void flush() {
    if (!repeat) return;
    LogMsg note{kInfo, "", "    Last message repeated " + std::to_string(repeat) + " times"};
    repeat = 0; LogHandler::log(note);
  }
};

struct Prefixer : LogHandler {
  void log(LogMsg& m) override {
    if (!m.module.empty()) m.text = "[" + m.module + "] " + m.text;
    LogHandler::log(m);
  }
};

struct ConsoleSink : LogHandler {
  int printed = 0;
  void log(LogMsg& m) override { ++printed; std::printf("%s\n", m.text.c_str()); }
};

int main() {
  LevelFilter filter(kWarning); RepeatFolder folder; Prefixer prefix; ConsoleSink sink;
  filter.setNext(&folder)->setNext(&prefix)->setNext(&sink);
  LogMsg msgs[] = {
    {kInfo, "mov", "Stream #0:0: Video: h264"},
    {kWarning, "h264", "corrupted macroblock 12 34"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kDebug, "h264", "nal_unit_type: 1"},
    {kWarning, "h264", "concealing 1532 errors"},
  };
  for (LogMsg& m : msgs) filter.log(m);
  folder.flush();
  std::printf("-- 共 7 条：过滤 %d，折叠 %d，输出 %d 行\n", filter.dropped, folder.dropped, sink.printed);
}
```

```text
[h264] corrupted macroblock 12 34
[h264] error while decoding MB 40 7
    Last message repeated 2 times
[h264] concealing 1532 errors
-- 共 7 条：过滤 2，折叠 2，输出 4 行
```

不纯责任链对顺序很敏感：把 `Prefixer` 放到 `LevelFilter` 前面，被丢弃的日志也会白白拼接字符串。

### 3.2 解码器回退链（FFmpeg 实测）

音视频最典型的责任链是**回退链**：把硬件解码器与软解排成 `VideoToolbox → VAAPI → CUDA → software`，前一个打不开就顺延试下一个（完整 FFmpeg 源码见 `projects/dp-lab/24-chain/ffmpeg/`）。

顺带一提，纯责任链的转发建议用**模板方法**：基类非虚函数 `open()` 固定「先调 `tryOpen()`，返回空就交给 `next_`」，子类只需实现 `tryOpen()`，从机制上杜绝子类忘记转发把请求吞掉（练习 3 演示了这个 bug）。

用 `bbb-10s.mp4`（720p H.264，240 帧，硬解帧拷回内存，`-O2`，7 次取中位数）在 macOS 与无 GPU 的 Linux 容器上实测同一条链：

<figure class="diagram">
<svg viewBox="0 0 730 144" role="img" aria-label="解码器回退链：macOS 上第一环 videotoolbox 就打开了解码器，后面三环没被问到；Linux 容器里 videotoolbox、vaapi、cuda 都失败，最后由 software 软解处理"><g style="font-family:var(--mono)"><defs><marker id="ch-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--link)"/></marker></defs><text x="10.0" y="38.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">macOS</text><rect x="150" y="14" width="120" height="38" rx="8" style="fill:var(--accent);fill-opacity:.16;stroke:var(--accent);stroke-width:1.6"/><text x="210.0" y="31.0" text-anchor="middle" style="fill:var(--accent);font-size:11.5px;font-weight:700">videotoolbox</text><text x="210.0" y="45.0" text-anchor="middle" style="fill:var(--accent);font-size:10px">✓ 打开，硬解</text><rect x="300" y="14" width="120" height="38" rx="8" style="fill:none;stroke:var(--muted);stroke-width:1;stroke-dasharray:4 3"/><text x="360.0" y="31.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px;font-weight:700">vaapi</text><text x="360.0" y="45.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">没被问到</text><path d="M270,33.0 L298,33.0" style="stroke:var(--muted);stroke-width:1.3" marker-end="url(#ch-arr)"/><rect x="450" y="14" width="120" height="38" rx="8" style="fill:none;stroke:var(--muted);stroke-width:1;stroke-dasharray:4 3"/><text x="510.0" y="31.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px;font-weight:700">cuda</text><text x="510.0" y="45.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">没被问到</text><path d="M420,33.0 L448,33.0" style="stroke:var(--muted);stroke-width:1.3" marker-end="url(#ch-arr)"/><rect x="600" y="14" width="120" height="38" rx="8" style="fill:none;stroke:var(--muted);stroke-width:1;stroke-dasharray:4 3"/><text x="660.0" y="31.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px;font-weight:700">software</text><text x="660.0" y="45.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">没被问到</text><path d="M570,33.0 L598,33.0" style="stroke:var(--muted);stroke-width:1.3" marker-end="url(#ch-arr)"/><text x="10.0" y="98.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Linux 容器</text><rect x="150" y="74" width="120" height="38" rx="8" style="fill:var(--danger);fill-opacity:.10;stroke:var(--danger);stroke-width:1.2"/><text x="210.0" y="91.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px;font-weight:700">videotoolbox</text><text x="210.0" y="105.0" text-anchor="middle" style="fill:var(--danger);font-size:10px">✗ 解码器不支持</text><rect x="300" y="74" width="120" height="38" rx="8" style="fill:var(--danger);fill-opacity:.10;stroke:var(--danger);stroke-width:1.2"/><text x="360.0" y="91.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px;font-weight:700">vaapi</text><text x="360.0" y="105.0" text-anchor="middle" style="fill:var(--danger);font-size:10px">✗ 没有 /dev/dri</text><path d="M270,93.0 L298,93.0" style="stroke:var(--link);stroke-width:1.3" marker-end="url(#ch-arr)"/><rect x="450" y="74" width="120" height="38" rx="8" style="fill:var(--danger);fill-opacity:.10;stroke:var(--danger);stroke-width:1.2"/><text x="510.0" y="91.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px;font-weight:700">cuda</text><text x="510.0" y="105.0" text-anchor="middle" style="fill:var(--danger);font-size:10px">✗ 没有 NVIDIA</text><path d="M420,93.0 L448,93.0" style="stroke:var(--link);stroke-width:1.3" marker-end="url(#ch-arr)"/><rect x="600" y="74" width="120" height="38" rx="8" style="fill:var(--accent);fill-opacity:.16;stroke:var(--accent);stroke-width:1.6"/><text x="660.0" y="91.0" text-anchor="middle" style="fill:var(--accent);font-size:11.5px;font-weight:700">software</text><text x="660.0" y="105.0" text-anchor="middle" style="fill:var(--accent);font-size:10px">✓ 打开，软解</text><path d="M570,93.0 L598,93.0" style="stroke:var(--link);stroke-width:1.3" marker-end="url(#ch-arr)"/><text x="10.0" y="134.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">同一段程序、同一条链：macOS 第一环就处理了；容器里前三环都打不开，一路交到链尾的软解。三次失败的尝试加起来不到 0.1 ms。</text></g></svg>
</figure>

<figure class="diagram">
<svg viewBox="0 0 680 156" role="img" aria-label="解码 bbb-10s.mp4 的 240 帧：macOS 上回退链选中 VideoToolbox 硬解，用时 260 毫秒、CPU 时间 31.6 毫秒；只用软解用时 284 毫秒、CPU 时间 283 毫秒；Linux 容器里回退到软解，用时和 CPU 时间都是 302 毫秒"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">墙钟时间</text><rect x="178.0" y="30.0" width="130.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="314.0" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">260 ms</text><rect x="178.0" y="60.0" width="142.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="326.0" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">284 ms</text><rect x="178.0" y="90.0" width="151.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="335.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">302 ms</text><text x="428.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">CPU 时间</text><rect x="428.0" y="30.0" width="15.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="449.8" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">31.6 ms</text><rect x="428.0" y="60.0" width="141.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="575.5" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">283 ms</text><rect x="428.0" y="90.0" width="151.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="585.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">302 ms</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">macOS：回退链 → 硬解</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">macOS：只用软解</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Linux：回退链 → 软解</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">decoder_chain.cpp，-O2，720p H.264，硬解帧拷回内存，单线程软解，7 次取中位数</text></g></svg>
</figure>

实测结论：macOS 第一环即用上 VideoToolbox，**CPU 时间仅为软解的 1/9（31.6 vs 283 ms）**；Linux 容器里三次失败的硬解尝试加起来**不到 0.1 ms**，平滑回退到链尾软解。

### 3.3 三个常见坑：链尾、成环、悬空指针

1. **链尾没人处理**：设计时要明确是静默丢弃（按键冒泡）、报错（解码器全失败），还是在链尾放一个**兜底处理者**（软解、`404 Not Found`）。
2. **链接成环**：运行时拼指针一旦成环（如 `player.next = &menu`），无人认领的请求会触发未定义行为（复现见 `projects/dp-lab/24-chain/cycle.cpp`）：

   | 编译器 | `-O0` | `-O2`（尾调用转循环） |
   | --- | --- | --- |
   | Clang / macOS | 段错误（栈溢出） | 直接打印「没人处理」并正常退出（利用无副作用死循环 UB 优化） |
   | GCC / Ubuntu | 段错误（栈溢出） | 死循环直到被超时杀掉 |

   防法：在 `link(n)` 时顺着 `n` 遍历一遍，走回 `this` 直接拒绝成环。
3. **悬空指针**：裸指针 `next_` 不拥有处理者；中间面板销毁时若忘了把前驱重连到后继，下一次分发直接段错误，ASan 报 `heap-use-after-free`（复现见 `projects/dp-lab/24-chain/dangling.cpp`）。解法是统一管理生命周期，或改用下一节「链拥有处理者」的容器写法。

真实项目里的责任链随处可见：C++ `try / catch` 沿调用栈匹配、Qt / DOM 事件冒泡（沿组合树父指针）、Windows `SetWindowsHookEx` 钩子链、HTTP 中间件。

## 4. 现代 C++ 写法与性能

### 4.1 `vector<std::function>` 焦点栈

面板是动态打开关闭的，不如直接用 `std::vector` 存 lambda 处理者：打开压栈、关闭移除，从栈顶往下问。成环和悬空指针两个坑一起消失；唯一要注意的是**处理函数里移除自己**时不能当场 `erase`，应先打标、等分发循环结束后统一清理：

```cpp
// 《责任链模式》：vector<std::function> 焦点栈——动态压栈与延迟删除，避免迭代器失效
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

enum class Key { Space, Esc, Up, Enter };
using Handler = std::function<std::string(Key)>;  // 返回空串表示「不归我管」

class KeyRouter {
 public:
  void push(std::string name, Handler h) { layers_.push_back({std::move(name), std::move(h)}); }
  void close(const std::string& name) {  // 只做标记：处理函数可能正在执行
    for (Layer& l : layers_) if (l.name == name) l.alive = false;
  }
  std::string dispatch(Key k) {
    std::string r = "没人处理";
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
      if (it->alive) if (std::string s = it->handle(k); !s.empty()) { r = s; break; }
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
                                 [](const Layer& l) { return !l.alive; }), layers_.end());
    return r;
  }
 private:
  struct Layer { std::string name; Handler handle; bool alive = true; };
  std::vector<Layer> layers_;
};

int main() {
  KeyRouter router;
  int volume = 50, sel = 3;
  router.push("播放器", [&](Key k) -> std::string {
    if (k == Key::Up) { volume += 5; return "播放器：音量 " + std::to_string(volume); }
    if (k == Key::Space) return "播放器：暂停";
    return "";
  });
  std::printf("%s\n", router.dispatch(Key::Up).c_str());

  router.push("字幕菜单", [&](Key k) -> std::string {
    if (k == Key::Up) return "字幕菜单：选中第 " + std::to_string(--sel) + " 项";
    if (k == Key::Esc) { router.close("字幕菜单"); return "字幕菜单：关闭"; }
    return "";
  });
  for (Key k : {Key::Up, Key::Space, Key::Esc, Key::Up, Key::Enter})
    std::printf("%s\n", router.dispatch(k).c_str());
}
```

```text
播放器：音量 55
字幕菜单：选中第 2 项
播放器：暂停
字幕菜单：关闭
播放器：音量 60
没人处理
```

### 4.2 洋葱中间件（拿到 `next`）

中间件把「交给下一环」变成参数 `next` 传入，于是每一环可以在调用 `next` **之前和之后**各做一件事（如日志、计时），也可以截断不调用（如缓存命中、参数越界）：

```cpp
// 《责任链模式》：洋葱中间件——每一环拿到 next，可在前后处理或直接截断
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

using Next = std::function<std::string(int)>;
using Middleware = std::function<std::string(int, const Next&)>;

Next build(const std::vector<Middleware>& mws, Next final) {
  for (auto it = mws.rbegin(); it != mws.rend(); ++it)
    final = [mw = *it, next = std::move(final)](int frame) { return mw(frame, next); };
  return final;
}

int main() {
  int decoded = 0;
  std::map<int, std::string> cache;
  std::vector<Middleware> mws = {
    [](int f, const Next& next) {
      std::printf("→ 日志：请求第 %d 帧\n", f);
      std::string r = next(f);
      std::printf("← 日志：第 %d 帧返回 %s\n", f, r.c_str());
      return r;
    },
    [&](int f, const Next& next) {
      if (auto it = cache.find(f); it != cache.end()) { std::printf("  → 缓存：命中\n"); return it->second; }
      std::printf("  → 缓存：未命中\n");
      return cache[f] = next(f);
    },
    [](int f, const Next& next) {
      return (f < 0 || f >= 240) ? std::string("越界") : next(f);
    },
  };
  Next pipeline = build(mws, [&](int f) {
    ++decoded;
    std::printf("    解码第 %d 帧\n", f);
    return "帧" + std::to_string(f);
  });
  pipeline(3); pipeline(3); pipeline(300);
  std::printf("解码器一共被调用 %d 次\n", decoded);
}
```

```text
→ 日志：请求第 3 帧
  → 缓存：未命中
    解码第 3 帧
← 日志：第 3 帧返回 帧3
→ 日志：请求第 3 帧
  → 缓存：命中
← 日志：第 3 帧返回 帧3
→ 日志：请求第 300 帧
  → 缓存：未命中
← 日志：第 300 帧返回 越界
解码器一共被调用 1 次
```

若链在编译期固定，还可以用 C++17 折叠表达式 `(rule<I>(r) || ...)` 展开成短路 `||`，零间接调用且全部内联。

### 4.3 四种写法的开销实测与选型

8 个处理者各接 1/8 随机请求（104 万请求 × 20 轮，`-O2`，7 次取中位数；完整压测见 `projects/dp-lab/24-chain/bench.cpp`）：

<figure class="diagram">
<svg viewBox="0 0 720 186" role="img" aria-label="四种写法把一个请求交给 8 个处理者：Clang 上虚函数加 next_ 6.17、vector 存 unique_ptr 7.08、vector 存 std::function 8.78、编译期折叠表达式 0.29 纳秒；GCC 上分别是 5.60、7.31、9.26、3.77 纳秒"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="178.0" y="30.0" width="111.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="295.1" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6.17 ns</text><rect x="178.0" y="60.0" width="127.4" height="20.0" rx="2" style="fill:var(--link)"/><text x="311.4" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">7.08 ns</text><rect x="178.0" y="90.0" width="158.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="342.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8.78 ns</text><rect x="178.0" y="120.0" width="5.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="189.2" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.29 ns</text><text x="448.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="448.0" y="30.0" width="100.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="554.8" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.6 ns</text><rect x="448.0" y="60.0" width="131.6" height="20.0" rx="2" style="fill:var(--link)"/><text x="585.6" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">7.31 ns</text><rect x="448.0" y="90.0" width="166.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="620.7" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">9.26 ns</text><rect x="448.0" y="120.0" width="67.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="521.9" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">3.77 ns</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">GoF：虚函数 + next_</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">vector&lt;unique_ptr&gt;</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">vector&lt;std::function&gt;</text><text x="170.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">编译期折叠表达式</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp，-O2：104 万个随机请求 × 20 轮，每个处理者接 1/8，7 次取中位数；GCC 跑在 colima 虚拟机里</text></g></svg>
</figure>

三种运行时链都在 **5～10 纳秒**，对按键、打开解码器完全可忽略；折叠表达式因编译期全可见可跨环优化（Clang 甚至消除了分支降到 0.29 ns）。选型看链要不要运行时变、谁拥有处理者：

| 写法 | 运行时可改 | 拥有处理者 | 适用场景 |
| --- | :---: | --- | --- |
| GoF 虚函数 + `next_` | ✓ | 链外部（防悬空） | 处理者是现成对象，链很少变动 |
| 基类固定转发（模板方法） | ✓ | 链外部 | 纯责任链、回退链，杜绝忘记转发 |
| `vector<std::function>` | ✓ | 链自己 | 界面焦点栈、动态增删过滤规则 |
| 洋葱中间件（传 `next`） | 构建期 | 链自己 | 请求前后都要做事（日志、计时、缓存） |
| 编译期折叠表达式 | ✗ | 编译期 | 链固定且在极致热路径上 |

## 5. 责任链和相似的模式

| 模式 | 请求被几个对象处理 | 关键区别 |
| --- | --- | --- |
| **责任链** | 通常 1 个（纯的），处理即停 | 每一环都可以**截断**请求，发送者不知谁处理 |
| **装饰器** | 每一层都执行且一定往里传 | 装饰器**不截断**调用，只增强功能；中间件介于两者之间 |
| **观察者** | 所有观察者都收到 | 广播语义，无顺序依赖、无截断 |
| **组合** | 视操作而定 | 事件冒泡常沿组合树的父指针走（责任链建在组合上） |
| **命令 / 状态** | 1 个接收者 / 当前状态 | 命令封装请求本身；状态只问当前状态，责任链依次问各环 |

一句话区分责任链与装饰器：**装饰器保证调用一定到达核心对象，责任链保证请求最多被处理一次。**

## 6. 小结

- 责任链把各处理者连成一条链，解决单函数分发时逻辑散落、优先级错位的痛点（加播放列表从改 5 处漏错 2 格降为新增 1 类 + 1 行组装、0 错误）。
- 音视频的硬解回退链（VideoToolbox → VAAPI → CUDA → software）在 macOS 省掉 9 倍 CPU 时间，无 GPU 容器里失败尝试不到 0.1 ms 即平滑回退软解。
- 纯责任链优先用模板方法固定转发；注意链尾兜底、组装防成环与指针生命周期（或直接用 `vector<std::function>` 让链拥有处理者）。
- 洋葱中间件让每环拿到 `next`，可在调用前后包裹逻辑或中途短路。

## 7. 练习

1. **辨析**：下面哪些是责任链？不是的话属于什么模式？
   - (a) 播放器音量变化同时通知界面、OSD、快捷键提示三个组件；
   - (b) 请求先查内存缓存，未命中查磁盘缓存，再未命中查服务器；
   - (c) 给解码器包一层计时、再包一层统计帧数，每次 `decode()` 两层都执行；
   - (d) C++ 的 `try / catch` 异常捕获。
2. **设计**：解码途中可能出错：`EAGAIN`（最多重试 2 次）、数据损坏（跳到下一关键帧）、硬件设备丢失（切软解）、内存不足（放弃报错）。用责任链设计错误处理，要求：**子类不能忘记转发**，且同一条链可被多个解码器共用（重试计数该放哪？）。
3. **找问题**：下面新增的 `Playlist` 有个 bug——播放列表开着时按空格无法暂停、按 → 无法快进，关掉列表就恢复正常。问题在哪？如何从设计上彻底避免？

```cpp
// 《责任链模式》练习 3：播放列表开着时，按空格不能暂停、按 → 不能快进，问题在哪
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Down, Right };

class KeyHandler {
 public:
  virtual ~KeyHandler() = default;
  KeyHandler* setNext(KeyHandler* n) { next_ = n; return n; }
  virtual std::string handle(Key k) { return next_ ? next_->handle(k) : "没人处理"; }
 private:
  KeyHandler* next_ = nullptr;
};

struct Playlist : KeyHandler {
  bool open = false;
  std::string handle(Key k) override {
    if (!open) return KeyHandler::handle(k);
    switch (k) {
      case Key::Down: return "播放列表：下移";
      case Key::Esc: open = false; return "播放列表：关闭";
      default: return "";  // 其他键不归播放列表管
    }
  }
};

struct PlayerKeys : KeyHandler {
  std::string handle(Key k) override {
    if (k == Key::Space) return "播放器：暂停";
    if (k == Key::Right) return "播放器：快进 5 秒";
    return KeyHandler::handle(k);
  }
};

int main() {
  Playlist list; PlayerKeys player;
  list.setNext(&player);
  const char* names[] = {"空格", "Esc", "↓", "→"};
  list.open = true;
  for (Key k : {Key::Down, Key::Space, Key::Right, Key::Esc, Key::Space}) {
    std::string r = list.handle(k);
    std::printf("按 %s：%s\n", names[int(k)], r.empty() ? "（没有反应）" : r.c_str());
  }
}
```

<details>
<summary>点击查看答案</summary>

1. (a) **观察者**（广播通知所有人）；(b) **责任链**（回退链，服务器兜底）；(c) **装饰器**（每层都执行且必往里传）；(d) **责任链**（沿调用栈匹配第一个 `catch`，链尾无匹配则 `std::terminate`）。

2. 用**模板方法**写法：基类非虚函数 `handle(DecodeError& e)` 固定「调纯虚 `tryHandle(e)`，返回空则转给 `next_`，链尾返回放弃」，子类无法忘记转发；**重试次数 `retries` 存在请求对象 `DecodeError` 里**（而不是 `Retry` 处理者成员里），这样一条无状态链可被多个解码器共用。完整实现见 `projects/dp-lab/24-chain/ex3_error_chain.cpp`。

3. `Playlist::handle` 的 `default` 返回了 `""`，而 GoF 写法里「不归我管」必须调用 `KeyHandler::handle(k)` 转发，返回空串直接把空格和 → 吞掉了。从设计上彻底避免：改用模板方法（基类固定 `if (r = tryHandle(k); !r.empty()) return r; return next_ ? next_->handle(k) : ...`）或 `vector` 循环分发（第 4.1 节），让处理者只返回结果、无权吞掉转发。完整修复版见 `projects/dp-lab/24-chain/ex4_fixed.cpp`。

</details>
