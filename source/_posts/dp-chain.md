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

> **本文目标**：理解责任链模式：把能处理请求的对象连成一条链，请求沿链传递，直到有对象处理它；会把「一个函数判断到底」的分发代码重构成责任链；量出加一个处理者要改几处；会写回退链（硬解失败退到软解）和不纯的责任链（日志过滤）；知道链尾没人处理、链接成环、悬空指针这几个坑；会用 `std::function`、中间件、折叠表达式写链，知道它们的开销；分清责任链和装饰器、观察者。
>
> **前置知识**：上一篇《状态模式》；《装饰器模式》（第 5 节比较）、《模板方法模式》（第 3.3 节用它保证转发不被忘掉）；音视频专栏里 FFmpeg 的解码流程（`avcodec_send_packet` / `avcodec_receive_frame`）。
>
> **编译环境**：g++/clang++ -std=c++17，完整代码在 projects/dp-lab/24-chain/

## 1. 问题：一个函数管所有按键

### 1.1 三层界面抢按键

播放器全屏时，屏幕上可能同时有好几层界面：

- 最上面是**对话框**，比如打开一个看过一半的视频时问「是否从上次的位置继续播放？」。它是模态的：开着时，所有按键都归它。
- 中间是**字幕菜单**，用 ↑ ↓ 选字幕、回车确认、Esc 关闭。别的键（空格暂停、← → 快进快退）它不管，照常交给播放器。
- 最下面是**播放器**本身：空格播放 / 暂停，← → 快退 / 快进 5 秒，↑ ↓ 调音量，Esc 退出全屏。

同一个 ↑，菜单开着时是「上移」，菜单关着时是「音量 +5」。最直接的写法是一个 `onKey`：先按键分支，每个分支里再判断哪一层开着。

```cpp
// 《责任链模式》：问题版——一个 onKey 按键分支，每个分支里再判断对话框、字幕菜单开没开
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

struct Player {
  bool dialogOpen = false;   // 模态对话框：「是否从上次的位置继续播放？」
  bool menuOpen = false;     // 字幕菜单
  bool playing = false, fullscreen = true;
  int volume = 50, menuSel = 0;
  double pos = 0;

  std::string onKey(Key k) {
    if (dialogOpen) {                       // 模态：对话框开着时，别的层都不响应
      if (k == Key::Enter) { dialogOpen = false; return "对话框：继续播放"; }
      if (k == Key::Esc) { dialogOpen = false; return "对话框：从头播放"; }
      return "对话框：忽略";
    }
    switch (k) {
      case Key::Space:
        playing = !playing;
        return playing ? "播放器：播放" : "播放器：暂停";
      case Key::Esc:
        if (menuOpen) { menuOpen = false; return "字幕菜单：关闭"; }
        fullscreen = false;
        return "播放器：退出全屏";
      case Key::Up:
        if (menuOpen) { --menuSel; return "字幕菜单：上移"; }
        volume += 5;
        return "播放器：音量 +5";
      case Key::Down:
        if (menuOpen) { ++menuSel; return "字幕菜单：下移"; }
        volume -= 5;
        return "播放器：音量 -5";
      case Key::Left: pos -= 5; return "播放器：快退 5 秒";
      case Key::Right: pos += 5; return "播放器：快进 5 秒";
      case Key::Enter:
        if (menuOpen) { menuOpen = false; return "字幕菜单：选中"; }
        return "没人处理";
    }
    return "没人处理";
  }
};

// ---- 以下是测试：各层开关的每种组合 × 7 个键，看是哪一层处理的，和规格逐格对照
const char* kKeys[] = {"空格", "Esc", "↑", "↓", "←", "→", "回车"};

std::string spec(bool dlg, bool menu, bool list, Key k) {   // 规格：从上往下，第一个开着、又关心这个键的层处理
  bool nav = k == Key::Up || k == Key::Down || k == Key::Enter || k == Key::Esc;
  if (dlg) return "对话框";
  if (menu && nav) return "字幕菜单";
  if (list && nav) return "播放列表";
  return k == Key::Enter ? "没人处理" : "播放器";
}

int main() {
  int cells = 0, bad = 0;
  for (int c = 0; c < 4; ++c)
    for (int k = 0; k < 7; ++k) {
      Player p;
      p.dialogOpen = c & 1; p.menuOpen = c & 2;
      std::string r = p.onKey(Key(k));
      std::string who = r.substr(0, r.find("："));
      std::string want = spec(c & 1, c & 2, false, Key(k));
      ++cells;
      if (who != want) { ++bad; std::printf("  ✗ 对话框%s 菜单%s 按 %s：%s，规格是 %s\n", c & 1 ? "开" : "关", c & 2 ? "开" : "关", kKeys[k], r.c_str(), want.c_str()); }
    }
  std::printf("%d 格里 %d 格和规格不一致\n", cells, bad);
}
```

`main` 里的测试把「对话框开 / 关 × 菜单开 / 关」4 种组合和 7 个键两两配对，看每一格是哪一层处理的，再和规格对照。规格只有一句话：**从上往下，第一个开着、又关心这个键的层处理它**。这一版 28 格全对。

### 1.2 加一个「播放列表」

现在要加一个播放列表面板，夹在字幕菜单和播放器之间，也是 ↑ ↓ 选、回车播放、Esc 关闭。播放列表的逻辑在 `onKey` 里没有一个属于自己的地方，只能拆成四份，塞进 Esc、↑、↓、回车四个分支：

```diff
   bool menuOpen = false;     // 字幕菜单
+  bool listOpen = false;     // 播放列表（新增）
...
       case Key::Esc:
         if (menuOpen) { menuOpen = false; return "字幕菜单：关闭"; }
+        if (listOpen) { listOpen = false; return "播放列表：关闭"; }
         fullscreen = false;
       case Key::Up:
+        if (listOpen) { --listSel; return "播放列表：上移"; }
         if (menuOpen) { --menuSel; return "字幕菜单：上移"; }
...
       case Key::Down:
         if (menuOpen) { ++menuSel; return "字幕菜单：下移"; }
         volume -= 5;
...
       case Key::Enter:
         if (menuOpen) { menuOpen = false; return "字幕菜单：选中"; }
+        if (listOpen) { listOpen = false; return "播放列表：播放选中项"; }
```

这是一次很像真实情况的修改，也犯了两个很像真实情况的错：

- ↑ 分支里，把 `listOpen` 的判断写在了 `menuOpen` 前面。四个分支各自决定先判断谁，「菜单在列表上面」这条规则没有写在任何一个地方，全靠每个分支自觉。
- ↓ 分支忘了改。

完整代码是 `key_switch_playlist.cpp`，测试部分和上面一样，组合变成 8 种。运行结果：

```text
  ✗ 对话框关 菜单关 列表开 按 ↓：播放器：音量 -5，规格是 播放列表
  ✗ 对话框关 菜单开 列表开 按 ↑：播放列表：上移，规格是 字幕菜单
56 格里 2 格和规格不一致
```

### 1.3 问题出在哪

`measure.sh` 用 `diff` 统计这次修改（只比较类的部分）：改了 **5 处**；`onKey` 里判断面板开关的 `if` 从 5 个变成 **8 个**。每加一层，这个数大约就要加上「这一层关心几个键」。

根子在于 `onKey` 按**键**组织代码，而变化是按**层**来的：

- 一层的逻辑被拆散到好几个分支里，加一层、删一层都要改好几处；
- 层与层的先后顺序（谁先拿到按键）散落在每个分支的 `if` 顺序里，没有一处能看出完整的优先级；
- 每层都要知道别的层：`Player` 必须认识对话框、菜单、列表的全部细节。

想要的是：每一层只管自己的键，管不了就往下传；层的顺序在一个地方写清楚。这就是责任链。

## 2. 责任链模式

### 2.1 意图

GoF 的定义：

> 使多个对象都有机会处理请求，从而避免请求的发送者和接收者之间的耦合关系。将这些对象连成一条链，并沿着这条链传递该请求，直到有一个对象处理它为止。

大白话：**把能处理请求的对象排成一队，请求从队头开始问，「这个你管不管？」管就处理掉，不管就问下一个。** 发请求的人只认识队头，不知道最后是谁处理的。

参与者：

- **Handler（处理者）**：定义处理请求的接口，保存下一个处理者（后继）。默认行为是把请求转给后继。
- **ConcreteHandler（具体处理者）**：能处理就处理，不能就调用基类的默认行为转给后继。
- **Client（客户）**：把请求交给链头。

### 2.2 每层一个处理者

```cpp
// 《责任链模式》：按键沿「对话框 → 字幕菜单 → 播放器」传递，每一层只管自己的键
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

class KeyHandler {                          // 处理者：保存下一个处理者
 public:
  virtual ~KeyHandler() = default;
  KeyHandler* setNext(KeyHandler* next) { next_ = next; return next; }
  virtual std::string handle(Key k) {      // 默认实现：自己不处理，交给下一个
    return next_ ? next_->handle(k) : "没人处理";
  }
 private:
  KeyHandler* next_ = nullptr;
};

class Dialog : public KeyHandler {          // 模态对话框：开着时吞掉所有按键
 public:
  bool open = false;
  std::string handle(Key k) override {
    if (!open) return KeyHandler::handle(k);
    if (k == Key::Enter) { open = false; return "对话框：继续播放"; }
    if (k == Key::Esc) { open = false; return "对话框：从头播放"; }
    return "对话框：忽略";
  }
};

class SubtitleMenu : public KeyHandler {    // 字幕菜单：只管 ↑ ↓ 回车 Esc
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
    return KeyHandler::handle(k);          // 不归我管：交给下一个
  }
};

class PlayerKeys : public KeyHandler {      // 播放器本身：链的最后一环
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
      default: return KeyHandler::handle(k);   // 回车：播放器也不管，交给下一个（没有了）
    }
  }
};

// ---- 以下是测试：各层开关的每种组合 × 7 个键，看是哪一层处理的，和规格逐格对照
const char* kKeys[] = {"空格", "Esc", "↑", "↓", "←", "→", "回车"};

std::string spec(bool dlg, bool menu, bool list, Key k) {   // 规格：从上往下，第一个开着、又关心这个键的层处理
  bool nav = k == Key::Up || k == Key::Down || k == Key::Enter || k == Key::Esc;
  if (dlg) return "对话框";
  if (menu && nav) return "字幕菜单";
  if (list && nav) return "播放列表";
  return k == Key::Enter ? "没人处理" : "播放器";
}

int main() {
  int cells = 0, bad = 0;
  for (int c = 0; c < 4; ++c)
    for (int k = 0; k < 7; ++k) {
      Dialog dialog; SubtitleMenu menu; PlayerKeys player;
      dialog.setNext(&menu)->setNext(&player);          // 组装链：对话框 → 字幕菜单 → 播放器
      dialog.open = c & 1; menu.open = c & 2;
      std::string r = dialog.handle(Key(k));
      std::string who = r.substr(0, r.find("："));
      std::string want = spec(c & 1, c & 2, c & 4, Key(k));
      ++cells;
      if (who != want) { ++bad; std::printf("  ✗ 组合 %d 按 %s：%s，规格是 %s\n", c, kKeys[k], r.c_str(), want.c_str()); }
    }
  std::printf("%d 格里 %d 格和规格不一致\n", cells, bad);
}
```

和第 1 节比：

- `onKey` 没有了。每一层的逻辑集中在自己的类里：`SubtitleMenu` 里只有菜单的 4 个键，看不到对话框，也看不到播放器。
- 每个处理者的结构都一样：开着、而且这个键归我管，就处理并 `return`；否则调用 `KeyHandler::handle(k)`，也就是交给 `next_`。
- 层的顺序只在一处：`dialog.setNext(&menu)->setNext(&player);`。`setNext` 返回参数本身，所以可以连着写。
- 对话框是模态的，就是「开着时什么键都处理」，后面的层自然收不到。不需要任何特殊的 `if (dialogOpen)`。

### 2.3 类图和时序图

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

类图里的链只有一条自关联 `next_`：每个处理者最多认识一个后继。运行时的链是对象之间的：

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

### 2.4 演示：按键怎么走

下面的演示里，同一条链收到不同的按键。注意两点：处理了就直接返回，后面的层收不到；一路走到链尾都没人管时，结果是「没人处理」。

<div class="dsviz" data-viz="dp-chain"></div>

### 2.5 再加「播放列表」

用责任链加播放列表，写一个新的处理者：

<!-- run: off -->
```cpp
class Playlist : public KeyHandler {        // 播放列表（新增）：也只管 ↑ ↓ 回车 Esc
 public:
  bool open = false;
  int sel = 0;
  std::string handle(Key k) override {
    if (open) switch (k) {
      case Key::Up: --sel; return "播放列表：上移";
      case Key::Down: ++sel; return "播放列表：下移";
      case Key::Enter: open = false; return "播放列表：播放选中项";
      case Key::Esc: open = false; return "播放列表：关闭";
      default: break;
    }
    return KeyHandler::handle(k);
  }
};
```

再把它接进链里，「菜单在列表上面」写在这一行，而且只写在这一行：

<!-- run: off -->
```cpp
dialog.setNext(&menu)->setNext(&list)->setNext(&player);   // 播放列表插在字幕菜单和播放器之间
```

`Dialog`、`SubtitleMenu`、`PlayerKeys` 一个字都没改。完整代码是 `key_chain_playlist.cpp`，56 格全对：

<figure class="diagram">
<svg viewBox="0 0 730 126" role="img" aria-label="加播放列表面板：一个 onKey 判断到底的版本改了 5 处，判断面板开关的 if 从 5 个变成 8 个，56 格里 2 格和规格不一致；责任链改了 1 处（新增一个类）加 1 行组装代码，没有 if，0 格不一致"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">diff 统计的改动处数</text><rect x="158.0" y="30.0" width="83.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="247.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5 处</text><rect x="158.0" y="60.0" width="16.7" height="20.0" rx="2" style="fill:var(--accent)"/><text x="180.7" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1 处</text><text x="348.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">onKey 里判断面板开关的 if</text><rect x="348.0" y="30.0" width="88.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="442.9" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8 个</text><rect x="348.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="355.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 个</text><text x="538.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">56 格里和规格不一致</text><rect x="538.0" y="30.0" width="66.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="610.7" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2 格</text><rect x="538.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="545.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 格</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">一个 onKey 判断到底</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">责任链</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh 只比较类的部分，责任链另需改 main 里 1 行组装代码；Clang、GCC 结果相同</text></g></svg>
</figure>

这里的「改了 1 处」是新增一个类；责任链还要在 `main` 里改一行组装代码，统计里没算进去，因为第 1 节版本的测试代码也要跟着改。另外，责任链的新增行数（15 行）比 `onKey` 版（5 行）多，这些行都在一个新类里，老代码没有被改动。

## 3. 深入责任链

### 3.1 纯的和不纯的

GoF 描述的是**纯的**责任链：请求最终只被一个处理者处理，处理了就停。按键分发就是这样。

实际中更常见的是**不纯的**责任链：每个处理者都可以对请求做点什么，再决定截住还是继续传。FFmpeg 的日志就是个好例子：`av_log` 打出的一条消息，默认要经过等级过滤（`av_log_set_level`）、重复消息折叠（连续相同的消息只打一次，后面补一句 `Last message repeated N times`）、加上 `[h264 @ 0x...]` 这样的前缀，最后才写到 stderr。FFmpeg 是在一个函数 `av_log_default_callback` 里依次做这几件事的；拆成一条链是这样：

```cpp
// 《责任链模式》：不纯的责任链——日志沿「等级过滤 → 重复折叠 → 加前缀 → 输出」传递，每一环可以截住，也可以加工后继续传
#include <cstdio>
#include <string>

enum Level { kError = 16, kWarning = 24, kInfo = 32, kDebug = 48 };   // 数值和 FFmpeg 的 AV_LOG_* 一样
struct LogMsg { int level; std::string module, text; };

class LogHandler {
 public:
  virtual ~LogHandler() = default;
  LogHandler* setNext(LogHandler* next) { next_ = next; return next; }
  virtual void log(LogMsg& m) { if (next_) next_->log(m); }
 private:
  LogHandler* next_ = nullptr;
};

class LevelFilter : public LogHandler {     // 比设定等级啰嗦的消息直接截住
 public:
  explicit LevelFilter(int level) : level_(level) {}
  int dropped = 0;
  void log(LogMsg& m) override {
    if (m.level > level_) { ++dropped; return; }
    LogHandler::log(m);
  }
 private:
  int level_;
};

class RepeatFolder : public LogHandler {    // 连续重复的消息只放过第一条，换了内容再补一句「重复了 N 次」
 public:
  int dropped = 0;
  void log(LogMsg& m) override {
    if (m.text == last_) { ++repeat_; ++dropped; return; }
    flush();
    last_ = m.text;
    LogHandler::log(m);
  }
  void flush() {
    if (repeat_ == 0) return;
    LogMsg note{kInfo, "", "    Last message repeated " + std::to_string(repeat_) + " times"};
    repeat_ = 0;
    LogHandler::log(note);
  }
 private:
  std::string last_;
  int repeat_ = 0;
};

class Prefixer : public LogHandler {        // 加工后继续传：在前面加上 [模块名]
 public:
  void log(LogMsg& m) override {
    if (!m.module.empty()) m.text = "[" + m.module + "] " + m.text;
    LogHandler::log(m);
  }
};

class ConsoleSink : public LogHandler {     // 链尾：真正输出
 public:
  int printed = 0;
  void log(LogMsg& m) override { ++printed; std::printf("%s\n", m.text.c_str()); }
};

int main() {
  LevelFilter filter(kWarning);
  RepeatFolder folder;
  Prefixer prefix;
  ConsoleSink sink;
  filter.setNext(&folder)->setNext(&prefix)->setNext(&sink);

  LogMsg msgs[] = {
    {kInfo, "mov", "Stream #0:0: Video: h264, 1280x720"},
    {kWarning, "h264", "corrupted macroblock 12 34"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kError, "h264", "error while decoding MB 40 7"},
    {kDebug, "h264", "nal_unit_type: 1, nal_ref_idc: 2"},
    {kDebug, "h264", "nal_unit_type: 1, nal_ref_idc: 0"},
    {kWarning, "h264", "concealing 1532 DC, 1532 AC, 1532 MV errors"},
    {kInfo, "aac", "Input buffer exhausted before END element found"},
  };
  for (LogMsg& m : msgs) filter.log(m);
  folder.flush();
  std::printf("-- 共 %zu 条：等级过滤截住 %d 条，重复折叠截住 %d 条，输出 %d 行\n",
              sizeof msgs / sizeof msgs[0], filter.dropped, folder.dropped, sink.printed);
}
```

`LevelFilter` 和 `RepeatFolder` 会**截住**请求（返回，不调用后继），`Prefixer` 会**加工后继续传**。`Prefixer` 修改了请求本身，所以链的顺序很重要：要是把 `Prefixer` 放在 `RepeatFolder` 前面，折叠比较的就是带前缀的文本，结果一样；但要是放在 `LevelFilter` 前面，被过滤掉的消息也会白白拼一次字符串。

FFmpeg 允许用 `av_log_set_callback` 换掉整个日志回调，但只有**一个**槽位，新回调会把默认的替换掉。想在默认行为前面加一环（比如统计错误条数），就得在自己的回调里手动调用 `av_log_default_callback`，自己把链接上：

<!-- run: off -->
```cpp
static std::atomic<int> g_errors{0};
void countingCallback(void* avcl, int level, const char* fmt, va_list vl) {
  if (level <= AV_LOG_ERROR) ++g_errors;          // 这一环：统计
  av_log_default_callback(avcl, level, fmt, vl);  // 交给下一环：FFmpeg 的默认输出
}
// av_log_set_callback(countingCallback);
```

### 3.2 解码器回退链

音视频里最典型的责任链是**回退链**：按优先级排好几种方案，前一种用不了就试下一种。播放器打开视频时先试硬件解码，硬件不支持或者设备打不开，就退回软件解码。不同平台的硬解接口不同（macOS 是 VideoToolbox，Linux 上 Intel / AMD 是 VAAPI、NVIDIA 是 CUDA），把它们排成一条链，每一环只关心「我能不能打开」：

<!-- run: off -->
```cpp
// 《责任链模式》：解码器回退链——VideoToolbox → VAAPI → CUDA → 软解，第一个能打开解码器的处理者负责解码
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/resource.h>

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
static double cpuMs() {             // 本进程所有线程用掉的 CPU 时间（用户态 + 内核态）
  rusage u; getrusage(RUSAGE_SELF, &u);
  return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3 + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3;
}
static std::string errStr(int err) { char b[128]; av_strerror(err, b, sizeof b); return b; }

class DecoderHandler {                     // 处理者：试着打开解码器，打不开就交给下一个
 public:
  virtual ~DecoderHandler() = default;
  DecoderHandler* setNext(DecoderHandler* next) { next_ = next; return next; }
  AVCodecContext* open(const AVCodecParameters* par) {
    auto t = Clock::now();
    std::string why;
    AVCodecContext* ctx = tryOpen(par, why);
    std::printf("  %-12s %s（%.2f ms）\n", name(), ctx ? "✓ 打开成功" : ("✗ " + why).c_str(), msSince(t));
    if (ctx) return ctx;
    return next_ ? next_->open(par) : nullptr;   // 这一环打不开：交给下一环
  }
  virtual bool hwFrame(const AVFrame*) const { return false; }
 protected:
  virtual const char* name() const = 0;
  virtual AVCodecContext* tryOpen(const AVCodecParameters* par, std::string& why) = 0;
 private:
  DecoderHandler* next_ = nullptr;
};

class HwDecoder : public DecoderHandler {  // 硬解：设备类型不同，代码相同
 public:
  explicit HwDecoder(AVHWDeviceType t) : type_(t) {}
  bool hwFrame(const AVFrame* f) const override { return f->format == hwFmt_; }
 protected:
  const char* name() const override { return av_hwdevice_get_type_name(type_); }
  AVCodecContext* tryOpen(const AVCodecParameters* par, std::string& why) override {
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    for (int i = 0;; ++i) {                // 1. 这个解码器支不支持这种硬件
      const AVCodecHWConfig* cfg = avcodec_get_hw_config(dec, i);
      if (!cfg) { why = "解码器不支持这种硬件"; return nullptr; }
      if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && cfg->device_type == type_) { hwFmt_ = cfg->pix_fmt; break; }
    }
    AVBufferRef* dev = nullptr;            // 2. 能不能创建硬件设备
    if (int err = av_hwdevice_ctx_create(&dev, type_, nullptr, nullptr, 0); err < 0) { why = "创建设备失败：" + errStr(err); return nullptr; }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, par);
    ctx->hw_device_ctx = dev;
    ctx->opaque = this;
    ctx->get_format = pickFormat;
    if (int err = avcodec_open2(ctx, dec, nullptr); err < 0) { why = errStr(err); avcodec_free_context(&ctx); return nullptr; }
    return ctx;
  }
 private:
  static AVPixelFormat pickFormat(AVCodecContext* c, const AVPixelFormat* fmts) {
    auto self = static_cast<HwDecoder*>(c->opaque);
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
      if (*p == self->hwFmt_) return *p;
    return avcodec_default_get_format(c, fmts);   // FFmpeg 内部也有一条回退：硬件格式不可用就用软件格式
  }
  AVHWDeviceType type_;
  AVPixelFormat hwFmt_ = AV_PIX_FMT_NONE;
};

class SwDecoder : public DecoderHandler {  // 软解：链尾，几乎总能打开
 protected:
  const char* name() const override { return "software"; }
  AVCodecContext* tryOpen(const AVCodecParameters* par, std::string& why) override {
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (!dec) { why = "没有这种解码器"; return nullptr; }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, par);
    if (int err = avcodec_open2(ctx, dec, nullptr); err < 0) { why = errStr(err); avcodec_free_context(&ctx); return nullptr; }
    return ctx;
  }
};

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "用法：%s 视频文件 [sw]\n", argv[0]); return 1; }
  bool swOnly = argc > 2 && std::strcmp(argv[2], "sw") == 0;
  av_log_set_level(AV_LOG_QUIET);
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, argv[1], nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0) return 1;
  int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

  HwDecoder vt(AV_HWDEVICE_TYPE_VIDEOTOOLBOX), vaapi(AV_HWDEVICE_TYPE_VAAPI), cuda(AV_HWDEVICE_TYPE_CUDA);
  SwDecoder sw;
  DecoderHandler* head = &sw;
  if (!swOnly) { vt.setNext(&vaapi)->setNext(&cuda)->setNext(&sw); head = &vt; }
  std::printf("%s：\n", swOnly ? "只用软解" : "回退链 videotoolbox → vaapi → cuda → software");
  AVCodecContext* ctx = head->open(fmt->streams[vs]->codecpar);
  if (!ctx) return 1;
  DecoderHandler* owner = ctx->hw_device_ctx ? static_cast<DecoderHandler*>(ctx->opaque) : &sw;

  auto t = Clock::now();
  double cpu0 = cpuMs();
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrame* cpu = av_frame_alloc();
  int frames = 0, hw = 0;
  auto drain = [&] {
    while (avcodec_receive_frame(ctx, frame) == 0) {
      ++frames;
      if (owner->hwFrame(frame)) { ++hw; av_hwframe_transfer_data(cpu, frame, 0); av_frame_unref(cpu); }  // 硬解的帧在显存里，拷回内存才算数
      av_frame_unref(frame);
    }
  };
  while (av_read_frame(fmt, pkt) >= 0) {
    if (pkt->stream_index == vs && avcodec_send_packet(ctx, pkt) >= 0) drain();
    av_packet_unref(pkt);
  }
  avcodec_send_packet(ctx, nullptr);
  drain();
  std::printf("解码 %d 帧（其中硬解 %d 帧），用时 %.1f ms，CPU 时间 %.1f ms\n", frames, hw, msSince(t), cpuMs() - cpu0);
  av_frame_free(&cpu); av_frame_free(&frame); av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
}
```

几个细节：

- `HwDecoder` 先查解码器支不支持这种硬件（`avcodec_get_hw_config`），再试着创建设备（`av_hwdevice_ctx_create`），两步都过了才算「能处理」。
- 这里的转发写在基类的 `open` 里，子类只实现 `tryOpen`，见第 3.3 节。
- FFmpeg 内部其实还有一条更小的回退链：解码器每次要确定输出格式时会调用 `get_format`，传进来一串候选格式，硬件格式在前、软件格式在后。`pickFormat` 找不到硬件格式时调用 `avcodec_default_get_format`，选一个软件格式。

用 `build.sh` 编译、`run.sh` 运行（视频是音视频专栏统一用的 `bbb-10s.mp4`，1280×720 H.264，240 帧）。同一份代码在两边走到了链的不同位置：

<figure class="diagram">
<svg viewBox="0 0 730 144" role="img" aria-label="解码器回退链：macOS 上第一环 videotoolbox 就打开了解码器，后面三环没被问到；Linux 容器里 videotoolbox、vaapi、cuda 都失败，最后由 software 软解处理"><g style="font-family:var(--mono)"><defs><marker id="ch-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--link)"/></marker></defs><text x="10.0" y="38.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">macOS</text><rect x="150" y="14" width="120" height="38" rx="8" style="fill:var(--accent);fill-opacity:.16;stroke:var(--accent);stroke-width:1.6"/><text x="210.0" y="31.0" text-anchor="middle" style="fill:var(--accent);font-size:11.5px;font-weight:700">videotoolbox</text><text x="210.0" y="45.0" text-anchor="middle" style="fill:var(--accent);font-size:10px">✓ 打开，硬解</text><rect x="300" y="14" width="120" height="38" rx="8" style="fill:none;stroke:var(--muted);stroke-width:1;stroke-dasharray:4 3"/><text x="360.0" y="31.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px;font-weight:700">vaapi</text><text x="360.0" y="45.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">没被问到</text><path d="M270,33.0 L298,33.0" style="stroke:var(--muted);stroke-width:1.3" marker-end="url(#ch-arr)"/><rect x="450" y="14" width="120" height="38" rx="8" style="fill:none;stroke:var(--muted);stroke-width:1;stroke-dasharray:4 3"/><text x="510.0" y="31.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px;font-weight:700">cuda</text><text x="510.0" y="45.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">没被问到</text><path d="M420,33.0 L448,33.0" style="stroke:var(--muted);stroke-width:1.3" marker-end="url(#ch-arr)"/><rect x="600" y="14" width="120" height="38" rx="8" style="fill:none;stroke:var(--muted);stroke-width:1;stroke-dasharray:4 3"/><text x="660.0" y="31.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px;font-weight:700">software</text><text x="660.0" y="45.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">没被问到</text><path d="M570,33.0 L598,33.0" style="stroke:var(--muted);stroke-width:1.3" marker-end="url(#ch-arr)"/><text x="10.0" y="98.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Linux 容器</text><rect x="150" y="74" width="120" height="38" rx="8" style="fill:var(--danger);fill-opacity:.10;stroke:var(--danger);stroke-width:1.2"/><text x="210.0" y="91.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px;font-weight:700">videotoolbox</text><text x="210.0" y="105.0" text-anchor="middle" style="fill:var(--danger);font-size:10px">✗ 解码器不支持</text><rect x="300" y="74" width="120" height="38" rx="8" style="fill:var(--danger);fill-opacity:.10;stroke:var(--danger);stroke-width:1.2"/><text x="360.0" y="91.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px;font-weight:700">vaapi</text><text x="360.0" y="105.0" text-anchor="middle" style="fill:var(--danger);font-size:10px">✗ 没有 /dev/dri</text><path d="M270,93.0 L298,93.0" style="stroke:var(--link);stroke-width:1.3" marker-end="url(#ch-arr)"/><rect x="450" y="74" width="120" height="38" rx="8" style="fill:var(--danger);fill-opacity:.10;stroke:var(--danger);stroke-width:1.2"/><text x="510.0" y="91.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px;font-weight:700">cuda</text><text x="510.0" y="105.0" text-anchor="middle" style="fill:var(--danger);font-size:10px">✗ 没有 NVIDIA</text><path d="M420,93.0 L448,93.0" style="stroke:var(--link);stroke-width:1.3" marker-end="url(#ch-arr)"/><rect x="600" y="74" width="120" height="38" rx="8" style="fill:var(--accent);fill-opacity:.16;stroke:var(--accent);stroke-width:1.6"/><text x="660.0" y="91.0" text-anchor="middle" style="fill:var(--accent);font-size:11.5px;font-weight:700">software</text><text x="660.0" y="105.0" text-anchor="middle" style="fill:var(--accent);font-size:10px">✓ 打开，软解</text><path d="M570,93.0 L598,93.0" style="stroke:var(--link);stroke-width:1.3" marker-end="url(#ch-arr)"/><text x="10.0" y="134.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">同一段程序、同一条链：macOS 第一环就处理了；容器里前三环都打不开，一路交到链尾的软解。三次失败的尝试加起来不到 0.1 ms。</text></g></svg>
</figure>

macOS（Apple Silicon）上的输出：

```text
回退链 videotoolbox → vaapi → cuda → software：
  videotoolbox ✓ 打开成功（0.01 ms）
解码 240 帧（其中硬解 240 帧），用时 260.2 ms，CPU 时间 31.6 ms
只用软解：
  software     ✓ 打开成功（0.01 ms）
解码 240 帧（其中硬解 0 帧），用时 284.1 ms，CPU 时间 283.4 ms
```

Ubuntu 容器（GCC，没有 GPU）里的输出：

```text
回退链 videotoolbox → vaapi → cuda → software：
  videotoolbox ✗ 解码器不支持这种硬件（0.00 ms）
  vaapi        ✗ 创建设备失败：Generic error in an external library（0.01 ms）
  cuda         ✗ 创建设备失败：Operation not permitted（0.02 ms）
  software     ✓ 打开成功（0.07 ms）
解码 240 帧（其中硬解 0 帧），用时 302.2 ms，CPU 时间 302.2 ms
```

<figure class="diagram">
<svg viewBox="0 0 680 156" role="img" aria-label="解码 bbb-10s.mp4 的 240 帧：macOS 上回退链选中 VideoToolbox 硬解，用时 260 毫秒、CPU 时间 31.6 毫秒；只用软解用时 284 毫秒、CPU 时间 283 毫秒；Linux 容器里回退到软解，用时和 CPU 时间都是 302 毫秒"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">墙钟时间</text><rect x="178.0" y="30.0" width="130.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="314.0" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">260 ms</text><rect x="178.0" y="60.0" width="142.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="326.0" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">284 ms</text><rect x="178.0" y="90.0" width="151.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="335.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">302 ms</text><text x="428.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">CPU 时间</text><rect x="428.0" y="30.0" width="15.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="449.8" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">31.6 ms</text><rect x="428.0" y="60.0" width="141.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="575.5" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">283 ms</text><rect x="428.0" y="90.0" width="151.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="585.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">302 ms</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">macOS：回退链 → 硬解</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">macOS：只用软解</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Linux：回退链 → 软解</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">decoder_chain.cpp，-O2，720p H.264，硬解帧拷回内存，单线程软解，7 次取中位数</text></g></svg>
</figure>

能看到：

- **墙钟时间差得不多**（260 对 284 毫秒），因为硬解的帧在显存里，这里每帧都用 `av_hwframe_transfer_data` 拷回内存，而且 720p 对软解来说也不算重。
- **CPU 时间差了 9 倍**（31.6 对 283 毫秒）。硬解时 CPU 基本只在搬数据，省下来的是电量和留给其他工作的 CPU。软解的 CPU 时间约等于墙钟时间，因为 FFmpeg 的 API 默认单线程解码（`thread_count` 默认是 1）。
- **失败的环很便宜**：容器里三次失败的尝试加起来不到 0.1 毫秒。回退链可以放心地把「最好但不一定有」的方案排在前面。
- 在 macOS 上 `videotoolbox` 「打开成功」只用了 0.01 毫秒：创建 VideoToolbox 设备几乎什么都不做，真正的解码会话要等解码器读到 SPS、调用 `get_format` 时才创建。所以「打开成功」不等于「一定能解」：要做得更稳，解码途中硬件出错时也应该回退到软解，练习 3 就是这样的错误处理链。

### 3.3 谁负责转发

转发有两种写法：

1. **GoF 的写法**（第 2 节的按键链）：`handle` 是虚函数，基类的默认实现是转发；子类覆盖 `handle`，自己不处理时调用 `KeyHandler::handle(k)`。灵活，子类可以先转发、再加工结果；但**每个子类都必须记得转发**，忘了就会把请求吞掉，练习 4 就是这样的 bug。
2. **模板方法的写法**（解码器链）：基类的 `open` 不是虚函数，固定了流程「先 `tryOpen`，失败就交给 `next_`」；子类只实现 `tryOpen`，回答「能不能处理」。子类没有机会忘记转发，代价是子类不能干预转发本身（比如「处理了也继续传」的不纯链就不好写）。

纯的责任链优先用第 2 种。这和《模板方法模式》里说的「好莱坞原则」一样：流程由基类控制，子类只填空。

### 3.4 链尾、成环和悬空指针

**没人处理。** 请求走到链尾时，`next_` 是空指针。按键链返回「没人处理」，回车键就这样静悄悄地没了。要不要这样，是设计时要决定的：

- 按键、事件冒泡：没人处理很正常，丢掉即可；
- 解码器回退链：没人处理就是打开失败，必须报错；
- 常见做法是在链尾放一个**兜底处理者**（软解、默认路由、`404 Not Found`），保证一定有人处理。

**成环。** 链是运行时用指针接起来的，接错了就可能成环：

```cpp
// 《责任链模式》：链接成了环，没人处理的请求会一直转下去；接链时检查一下就能拦住
#include <cstdio>
#include <cstring>

struct Handler {
  const char* name;
  Handler* next = nullptr;
  bool handle(int key) {
    if (key == 'q') { std::printf("%s 处理了 q\n", name); return true; }
    return next ? next->handle(key) : false;
  }
  bool link(Handler* n) {                   // 接链前沿着 n 往后走一遍，走回自己就说明会成环
    for (Handler* h = n; h; h = h->next)
      if (h == this) { std::printf("拒绝：%s → %s 会让链成环\n", name, n->name); return false; }
    next = n;
    return true;
  }
};

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);   // 不缓冲：程序崩溃前的输出也能看到
  bool check = !(argc > 1 && std::strcmp(argv[1], "nocheck") == 0);
  Handler menu{"字幕菜单"}, list{"播放列表"}, player{"播放器"};
  menu.link(&list);
  list.link(&player);
  if (check) player.link(&menu);            // 手误：本想接「截图层」，接成了 menu
  else player.next = &menu;                 // 不检查，直接接上
  std::printf("按 q：%s\n", menu.handle('q') ? "有人处理" : "没人处理");
  std::printf("按 x：%s\n", menu.handle('x') ? "有人处理" : "没人处理");   // 没人处理 x：成环时在环里一直转
}
```

默认运行时 `link` 会检查，拒绝接成环。传 `nocheck` 参数时不检查，`player.next = &menu` 让链成了环，没人处理的 x 会一直转下去。实测结果在两个编译器、两个优化级别下各不相同：

| | -O0 | -O2 |
|---|---|---|
| Clang / macOS | 段错误（递归太深，栈溢出） | 打印「按 x：没人处理」，正常退出 |
| GCC / Ubuntu | 段错误（栈溢出） | 死循环，3 秒后被超时杀掉 |

-O2 下的两种结果都来自同一件事：`return next->handle(key)` 是尾调用，编译器把递归改成了循环，于是不再消耗栈。GCC 生成的就是一个真的死循环。Clang 走得更远：C++ 标准允许编译器假设「没有副作用的循环最终会结束」，这个循环里没有输入输出、没有 `volatile`，Clang 就认定它会结束，而结束时只能返回 `false`，于是直接返回了 `false`。这是未定义行为，三种表现都「合法」。链要在组装时检查，不能指望运行时的表现。

**悬空指针。** 按键链里 `next_` 是裸指针，链不拥有处理者。面板关闭时删除了处理者，却忘了把前一环重新接到后一环：

<!-- run: off -->
```cpp
// 《责任链模式》：链里存的是裸指针——播放列表关闭时对象删掉了，链还指着它
#include <cstdio>
#include <memory>
#include <string>

class KeyHandler {
 public:
  virtual ~KeyHandler() = default;
  KeyHandler* setNext(KeyHandler* next) { next_ = next; return next; }
  virtual std::string handle(char k) { return next_ ? next_->handle(k) : "没人处理"; }
 private:
  KeyHandler* next_ = nullptr;
};
struct Menu : KeyHandler {};                // 字幕菜单（关着，什么都交给下一个）
struct Playlist : KeyHandler {
  std::string title = "Big Buck Bunny";
  std::string handle(char k) override { return k == 'n' ? "播放列表：下一首 " + title : KeyHandler::handle(k); }
};
struct Player : KeyHandler {
  std::string handle(char k) override { return k == ' ' ? "播放器：暂停" : KeyHandler::handle(k); }
};

int main() {
  Menu menu; Player player;
  auto list = std::make_unique<Playlist>();       // 打开播放列表：new 一个，接进链
  menu.setNext(list.get())->setNext(&player);
  std::printf("%s\n", menu.handle('n').c_str());
  list.reset();                                    // 关闭播放列表：删掉了对象，却忘了把 menu 重新接到 player
  std::printf("%s\n", menu.handle(' ').c_str());  // menu 还指着已经释放的 Playlist
}
```

在 Clang 和 GCC 的 -O2 下都是段错误（访问已释放对象的虚函数表）。加上 `-fsanitize=address`，ASan 准确地指出了问题：

```text
ERROR: AddressSanitizer: heap-use-after-free on address 0x604000000390
READ of size 8 at 0x604000000390 thread T0
    #0 in KeyHandler::handle(char) dangling.cpp:10
    #1 in main dangling.cpp:29
```

解决办法有两种：处理者的生命周期比链长（像第 2 节那样都是 `main` 里的局部对象，或者由同一个对象统一拥有）；或者干脆让链拥有处理者，增删都通过链来做，第 4.1 节的 `KeyRouter` 就是这样。

### 3.5 真实项目里的责任链

- **C++ 异常**：`throw` 之后，运行时沿着调用栈一层层往外找，第一个类型匹配的 `catch` 处理它；都不匹配就 `std::terminate`。这就是一条由调用栈组成的责任链。
- **GUI 事件冒泡**：Qt 里子控件不处理的事件（调用 `event->ignore()`）会交给父控件；浏览器 DOM 事件从目标元素冒泡到 `document`，`stopPropagation()` 就是「处理了，到此为止」。这里的链是沿着组合模式的父指针走的。
- **Windows 钩子**：`SetWindowsHookEx` 装的钩子组成一条链，每个钩子处理完要调用 `CallNextHookEx`，不调用就把消息吞了，正是第 3.3 节说的第 1 种写法的风险。
- **HTTP 中间件**：Express、Koa、ASP.NET Core 的请求处理是一串中间件，见第 4.2 节。
- **FFmpeg**：除了上面的硬解回退和 `get_format`，打开文件时 `av_probe_input_format` 会让每个解封装器给文件打分，取最高分的那个。这不是责任链（所有人都要回答，选最好的），而是它的近亲。区别在于：责任链问到第一个「能」就停。

## 4. 现代 C++ 写法

### 4.1 用 vector 存处理者

真实的界面里，面板是动态打开、关闭的。与其让每个处理者带着 `open` 标志永远待在链里，不如面板打开时把它压进链、关上时移出去。链用 `std::vector` 存，处理者用 lambda 写：

```cpp
// 《责任链模式》：用 vector<std::function> 当链——面板打开时压进去、关上时弹出来，从最上面一层往下问
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };
using Handler = std::function<std::string(Key)>;   // 返回空串表示「不归我管」

class KeyRouter {
 public:
  void push(std::string name, Handler h) { layers_.push_back({std::move(name), std::move(h)}); }
  void remove(const std::string& name) {     // 只做标记：处理函数可能正在执行，这时不能把它从 vector 里删掉
    for (Layer& l : layers_) if (l.name == name) l.alive = false;
  }
  std::string dispatch(Key k) {
    std::string r = "没人处理";
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)   // 后打开的在上面，先问它
      if (it->alive) if (std::string s = it->handle(k); !s.empty()) { r = s; break; }
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [](const Layer& l) { return !l.alive; }), layers_.end());
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

  router.push("字幕菜单", [&](Key k) -> std::string {   // 打开字幕菜单：压一层
    if (k == Key::Up) return "字幕菜单：选中第 " + std::to_string(--sel) + " 项";
    if (k == Key::Esc) { router.remove("字幕菜单"); return "字幕菜单：关闭"; }
    return "";
  });
  std::printf("%s\n", router.dispatch(Key::Up).c_str());
  std::printf("%s\n", router.dispatch(Key::Space).c_str());   // 菜单不管空格，落到播放器
  std::printf("%s\n", router.dispatch(Key::Esc).c_str());     // 菜单在处理函数里把自己移除
  std::printf("%s\n", router.dispatch(Key::Up).c_str());      // 又回到调音量
  std::printf("%s\n", router.dispatch(Key::Enter).c_str());
}
```

几点变化：

- 处理者之间不再互相指着，`next_` 没了，由 `dispatch` 的循环负责「问下一个」。成环、悬空指针这两个坑一起消失了，因为链（`vector`）拥有处理者。
- 顺序就是压栈顺序：后打开的面板在上面，先拿到按键。这正是窗口管理器里「焦点栈」的做法。
- 有一个新坑：字幕菜单在**自己的处理函数里**把自己移出链（按 Esc 时）。如果 `remove` 当场从 `vector` 里删除，正在执行的 `std::function` 对象就被销毁了，`dispatch` 的迭代器也失效了。这和《状态模式》第 3.3 节「在成员函数里换掉自己」是同一类问题。这里的做法是 `remove` 只做标记，等 `dispatch` 问完一轮再统一删除。

### 4.2 中间件：拿到 next

GoF 的处理者要么处理、要么转发。中间件把「转发」变成一个参数 `next` 交给处理者自己调用，于是一个处理者可以在调用 `next` **之前和之后**各做一些事，也可以干脆不调用：

```cpp
// 《责任链模式》：中间件写法——每一环拿到 next，可以在调用 next 之前和之后各做一些事，也可以不调用
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

using Next = std::function<std::string(int)>;                    // 「交给后面」
using Middleware = std::function<std::string(int, const Next&)>;

// 把中间件一层套一层地包起来：mws[0] 在最外层，final 在最里面
Next build(const std::vector<Middleware>& mws, Next final) {
  for (auto it = mws.rbegin(); it != mws.rend(); ++it)
    final = [mw = *it, next = std::move(final)](int frame) { return mw(frame, next); };
  return final;
}

int main() {
  int decoded = 0;
  std::map<int, std::string> cache;
  std::vector<Middleware> mws = {
    [](int f, const Next& next) {                                // 1. 日志：前后各打一行
      std::printf("→ 日志：请求第 %d 帧\n", f);
      std::string r = next(f);
      std::printf("← 日志：第 %d 帧返回 %s\n", f, r.c_str());
      return r;
    },
    [&](int f, const Next& next) {                               // 2. 缓存：命中就不往后传
      if (auto it = cache.find(f); it != cache.end()) { std::printf("  → 缓存：命中\n"); return it->second; }
      std::printf("  → 缓存：没命中\n");
      return cache[f] = next(f);
    },
    [](int f, const Next& next) {                                // 3. 参数检查：越界直接拒绝
      if (f < 0 || f >= 240) return std::string("越界");
      return next(f);
    },
  };
  Next pipeline = build(mws, [&](int f) {                         // 最里面：真正解码
    ++decoded;
    std::printf("    解码第 %d 帧\n", f);
    return "帧" + std::to_string(f);
  });
  pipeline(3);
  pipeline(3);
  pipeline(300);
  std::printf("解码器一共被调用 %d 次\n", decoded);
}
```

输出的缩进显示了请求的路径：从外往里，结果从里往外，像剥洋葱，所以也叫**洋葱模型**。日志中间件在前后各打一行；缓存中间件命中时不调用 `next`，后面的参数检查和解码器都不会执行；第三次请求被参数检查拒绝，解码器一共只被调用了 1 次。

中间件和装饰器很像（都是一层套一层），区别在于中间件可以不调用 `next`，第 5 节再比较。

### 4.3 编译期的链

如果链的组成在编译期就确定、运行时不变，可以把处理者放进模板参数，用 C++17 的折叠表达式展开成一串 `||`，短路求值正好就是「处理了就停」：

<!-- run: off -->
```cpp
template <int... I> bool foldChain(Req& r, std::integer_sequence<int, I...>) { return (rule<I>(r) || ...); }
```

没有虚函数，没有指针，编译器能把所有处理者内联到一起。

### 4.4 四种写法的开销

`bench.cpp` 让 8 个处理者处理同一串随机请求，每个处理者接 1/8，平均每个请求要问 4.5 个处理者：

<!-- run: off -->
```cpp
// 《责任链模式》：四种写法把同一串请求交给 8 个处理者，平均每个请求要多少纳秒（请用 -O2 编译）
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

struct Req { int code; long out; };
template <int I> bool rule(Req& r) {        // 第 I 个处理者只管 code == I 的请求
  if (r.code != I) return false;
  r.out += I * 3 + 1;
  return true;
}
constexpr int kN = 8;

// ---- 1. GoF：虚函数 + next_ 指针
struct Linked {
  virtual ~Linked() = default;
  Linked* next = nullptr;
  virtual bool handle(Req& r) = 0;
};
template <int I> struct LinkedH : Linked {
  bool handle(Req& r) override { return rule<I>(r) || (next && next->handle(r)); }
};

// ---- 2. 处理者放进 vector，由外面的循环依次调用
struct Base { virtual ~Base() = default; virtual bool handle(Req& r) = 0; };
template <int I> struct VecH : Base { bool handle(Req& r) override { return rule<I>(r); } };

// ---- 3. vector<std::function>
using Fn = std::function<bool(Req&)>;

// ---- 4. 编译期的链：处理者的类型在模板参数里，折叠表达式展开成一串 ||
template <int... I> bool foldChain(Req& r, std::integer_sequence<int, I...>) { return (rule<I>(r) || ...); }

template <class F> double bench(const std::vector<int>& codes, F&& f) {
  std::vector<double> t;
  long sink = 0;
  for (int rep = 0; rep < 7; ++rep) {
    Req r{0, 0};
    auto t0 = std::chrono::steady_clock::now();
    for (int round = 0; round < 20; ++round)
      for (int c : codes) { r.code = c; f(r); }
    auto t1 = std::chrono::steady_clock::now();
    t.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / (20.0 * codes.size()));
    sink += r.out;
  }
  std::sort(t.begin(), t.end());
  if (sink == 42) std::puts("");
  return t[3];
}

int main() {
  std::vector<int> codes(1 << 20);
  unsigned s = 12345;
  for (int& c : codes) { s = s * 1103515245u + 12345u; c = (s >> 16) % kN; }   // 每个处理者各接到 1/8 的请求

  LinkedH<0> l0; LinkedH<1> l1; LinkedH<2> l2; LinkedH<3> l3; LinkedH<4> l4; LinkedH<5> l5; LinkedH<6> l6; LinkedH<7> l7;
  Linked* ls[] = {&l0, &l1, &l2, &l3, &l4, &l5, &l6, &l7};
  for (int i = 0; i + 1 < kN; ++i) ls[i]->next = ls[i + 1];
  Linked* head = ls[0];

  std::vector<std::unique_ptr<Base>> vec;
  vec.push_back(std::make_unique<VecH<0>>()); vec.push_back(std::make_unique<VecH<1>>());
  vec.push_back(std::make_unique<VecH<2>>()); vec.push_back(std::make_unique<VecH<3>>());
  vec.push_back(std::make_unique<VecH<4>>()); vec.push_back(std::make_unique<VecH<5>>());
  vec.push_back(std::make_unique<VecH<6>>()); vec.push_back(std::make_unique<VecH<7>>());

  std::vector<Fn> fns{rule<0>, rule<1>, rule<2>, rule<3>, rule<4>, rule<5>, rule<6>, rule<7>};

  std::printf("8 个处理者，%zu 个请求 × 20 轮，7 次取中位数\n", codes.size());
  std::printf("GoF 虚函数 + next_     %.2f ns\n", bench(codes, [&](Req& r) { head->handle(r); }));
  std::printf("vector<unique_ptr>    %.2f ns\n", bench(codes, [&](Req& r) { for (auto& h : vec) if (h->handle(r)) break; }));
  std::printf("vector<std::function> %.2f ns\n", bench(codes, [&](Req& r) { for (auto& f : fns) if (f(r)) break; }));
  std::printf("编译期折叠表达式       %.2f ns\n", bench(codes, [&](Req& r) { foldChain(r, std::make_integer_sequence<int, kN>{}); }));
}
```

<figure class="diagram">
<svg viewBox="0 0 720 186" role="img" aria-label="四种写法把一个请求交给 8 个处理者：Clang 上虚函数加 next_ 6.17、vector 存 unique_ptr 7.08、vector 存 std::function 8.78、编译期折叠表达式 0.29 纳秒；GCC 上分别是 5.60、7.31、9.26、3.77 纳秒"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="178.0" y="30.0" width="111.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="295.1" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6.17 ns</text><rect x="178.0" y="60.0" width="127.4" height="20.0" rx="2" style="fill:var(--link)"/><text x="311.4" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">7.08 ns</text><rect x="178.0" y="90.0" width="158.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="342.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8.78 ns</text><rect x="178.0" y="120.0" width="5.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="189.2" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.29 ns</text><text x="448.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC / Ubuntu</text><rect x="448.0" y="30.0" width="100.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="554.8" y="45.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.6 ns</text><rect x="448.0" y="60.0" width="131.6" height="20.0" rx="2" style="fill:var(--link)"/><text x="585.6" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">7.31 ns</text><rect x="448.0" y="90.0" width="166.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="620.7" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">9.26 ns</text><rect x="448.0" y="120.0" width="67.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="521.9" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">3.77 ns</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">GoF：虚函数 + next_</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">vector&lt;unique_ptr&gt;</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">vector&lt;std::function&gt;</text><text x="170.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">编译期折叠表达式</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp，-O2：104 万个随机请求 × 20 轮，每个处理者接 1/8，7 次取中位数；GCC 跑在 colima 虚拟机里</text></g></svg>
</figure>

- 三种运行时的链差不多，都在 5～10 纳秒：每问一个处理者就是一次间接调用，加上随机请求造成的分支预测失败。`std::function` 最慢，因为它的调用多一层间接跳转。
- 编译期折叠表达式在两个编译器上差得很远：GCC 3.77 纳秒，Clang 只要 0.29 纳秒。看汇编，GCC 把 8 个处理者内联成一串比较和跳转；Clang 发现这 8 条规则合起来就是「`code` 小于 8 时 `out += code * 3 + 1`」，生成了一段完全没有分支的代码。这是测试里的规则太规整造成的，真实的处理者不会这么巧，但它说明了一点：只有处理者全部在编译期可见，编译器才有机会做这种跨处理者的优化。
- 和按键、打开解码器这些操作比，几纳秒的分发开销可以忽略。选哪种写法看的是**链要不要在运行时变**，不是速度。

### 4.5 怎么选

| 写法 | 链能在运行时改吗 | 谁拥有处理者 | 适合 |
|---|---|---|---|
| GoF：虚函数 + `next_` | 能，改指针 | 链外面（要小心悬空） | 处理者是现成的对象，链很少变 |
| 基类固定转发（模板方法） | 能 | 链外面 | 纯的责任链、回退链，防止忘记转发 |
| `vector<std::function>` | 能，压入 / 移除 | 链自己 | 处理者动态增删：界面层、事件过滤 |
| 中间件（传 `next`） | 构建时确定 | 链自己 | 处理前后都要做事：日志、计时、缓存 |
| 折叠表达式 | 不能 | 编译期 | 链固定、在热路径上 |

## 5. 责任链和相似的模式

| 模式 | 结构 | 请求 / 调用会被几个对象处理 | 关键区别 |
|---|---|---|---|
| 责任链 | 一串处理者，每个指向下一个 | 通常 1 个（纯的），处理了就停 | 每一环都可以**截住**请求，发送者不知道谁处理 |
| 装饰器 | 一层包一层，每层指向被包的对象 | 每一层都处理，并且一定往里传 | 装饰器**不会截住**调用，只是加功能；中间件介于两者之间 |
| 观察者 | 一个主题，一组观察者 | 所有观察者都收到 | 广播，没有「处理了就停」，观察者之间没有顺序依赖 |
| 组合 | 树 | 看操作 | 事件冒泡常常沿组合树的父指针走，这时责任链是建在组合上的 |
| 命令 | 把请求封装成对象 | 一个接收者 | 命令关心「请求是什么、能不能撤销」，责任链关心「谁来处理」；两者常一起用：命令对象沿链传递 |
| 状态 | 上下文持有当前状态 | 当前状态 1 个 | 状态模式里只问当前状态，由状态决定下一个状态；责任链里依次问每个处理者 |

一句话区分责任链和装饰器：**装饰器保证调用一定会到达被装饰的对象，责任链保证请求最多被处理一次。**

## 6. 小结

- 责任链把能处理请求的对象排成一条链，请求从链头开始传，每个处理者要么处理、要么交给下一个。发送者只认识链头。
- 按键分发写在一个函数里时，一层的逻辑散在好几个分支里、层的优先级散在每个分支的 `if` 顺序里。加一个播放列表，这个版本改了 5 处、改错 2 处；责任链只加一个类、改一行组装代码。
- 回退链是音视频里最常见的责任链：VideoToolbox → VAAPI → CUDA → 软解。实测 macOS 第一环就用上硬解，CPU 时间只有软解的九分之一；容器里三次失败的尝试一共不到 0.1 毫秒。
- 不纯的责任链里每一环都可以加工请求再传，顺序很重要；FFmpeg 的日志过滤、Web 中间件都是这种。
- 转发最好由基类负责（模板方法），子类只回答「能不能处理」，免得忘记转发把请求吞掉。
- 链尾要想清楚：没人处理是正常还是错误，需不需要兜底处理者。
- 链是运行时用指针接起来的：接成环是未定义行为（实测 -O0 栈溢出，-O2 下 Clang 直接返回、GCC 死循环），处理者被删除后链还指着它就是悬空指针。让链拥有处理者（`vector`）可以一起避免。
- 中间件让处理者拿到 `next`，可以在前后各做一些事；链固定时可以用折叠表达式在编译期展开。

## 7. 练习

1. **辨析**：下面哪些是责任链？不是的话是什么模式？
   (a) 播放器的音量变化通知界面、OSD、快捷键提示三个组件；
   (b) 网络请求先查内存缓存，没有再查磁盘缓存，再没有才请求服务器；
   (c) 给解码器包一层计时，再包一层统计帧数，每次 `decode()` 两层都要执行；
   (d) C++ 的 `try` / `catch` 嵌套。
2. **动手**：在 `key_chain.cpp` 的基础上加一个「截图标注」层：开着时 ← → 移动光标、回车保存截图、Esc 退出；空格、↑ ↓ 照常交给后面的层。它应该放在链的哪个位置？要改哪些代码？
3. **设计**：解码过程中可能出各种错：`EAGAIN`（暂时没数据，重试即可）、数据损坏（跳到下一个关键帧）、硬件设备丢失（比如显卡驱动重置，要换成软解继续）、内存不足（只能放弃）。用责任链设计错误处理，要求：重试最多 2 次；子类不能忘记转发。
4. **找问题**：同事写的播放列表（`ex4_bug.cpp`）有个 bug：播放列表开着时，按空格不能暂停、按 → 不能快进，关掉列表就好了。问题在哪？怎么从设计上避免？

   ```cpp
   // 《责任链模式》练习 4：播放列表开着时，按空格不能暂停、按 → 不能快进，问题在哪
   #include <cstdio>
   #include <string>

   enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

   class KeyHandler {                          // 处理者：保存下一个处理者
    public:
     virtual ~KeyHandler() = default;
     KeyHandler* setNext(KeyHandler* next) { next_ = next; return next; }
     virtual std::string handle(Key k) {      // 默认实现：自己不处理，交给下一个
       return next_ ? next_->handle(k) : "没人处理";
     }
    private:
     KeyHandler* next_ = nullptr;
   };

   class Dialog : public KeyHandler {          // 模态对话框：开着时吞掉所有按键
    public:
     bool open = false;
     std::string handle(Key k) override {
       if (!open) return KeyHandler::handle(k);
       if (k == Key::Enter) { open = false; return "对话框：继续播放"; }
       if (k == Key::Esc) { open = false; return "对话框：从头播放"; }
       return "对话框：忽略";
     }
   };

   class SubtitleMenu : public KeyHandler {    // 字幕菜单：只管 ↑ ↓ 回车 Esc
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
       return KeyHandler::handle(k);          // 不归我管：交给下一个
     }
   };

   class Playlist : public KeyHandler {        // 播放列表（新增）：也只管 ↑ ↓ 回车 Esc
    public:
     bool open = false;
     int sel = 0;
     std::string handle(Key k) override {
       if (!open) return KeyHandler::handle(k);
       switch (k) {
         case Key::Up: --sel; return "播放列表：上移";
         case Key::Down: ++sel; return "播放列表：下移";
         case Key::Enter: open = false; return "播放列表：播放选中项";
         case Key::Esc: open = false; return "播放列表：关闭";
         default: return "";                   // 其他键不归播放列表管
       }
     }
   };

   class PlayerKeys : public KeyHandler {      // 播放器本身：链的最后一环
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
         default: return KeyHandler::handle(k);   // 回车：播放器也不管，交给下一个（没有了）
       }
     }
   };


   int main() {
     Dialog dialog; SubtitleMenu menu; Playlist list; PlayerKeys player;
     dialog.setNext(&menu)->setNext(&list)->setNext(&player);
     const char* names[] = {"空格", "Esc", "↑", "↓", "←", "→", "回车"};
     list.open = true;                         // 打开播放列表，边选边看
     for (Key k : {Key::Down, Key::Space, Key::Right, Key::Esc, Key::Space}) {
       std::string r = dialog.handle(k);
       std::printf("按 %s：%s\n", names[int(k)], r.empty() ? "（没有反应）" : r.c_str());
     }
   }
   ```

<details>
<summary>点击查看答案</summary>

**1.**

- (a) 不是。三个组件**都要**收到通知，是观察者。
- (b) 是，而且是回退链：内存缓存 → 磁盘缓存 → 服务器，前一个命中就停。服务器是兜底处理者。
- (c) 不是。每一层都执行、调用一定往里传，是装饰器。
- (d) 是。异常沿着调用栈向外传，第一个类型匹配的 `catch` 处理它，处理了就停；都不匹配时调用 `std::terminate`，相当于链尾没人处理时报错。

**2.** 标注层开着时要拿到 ← → 和回车，而播放器也管 ← →，所以标注层要在播放器前面；它和字幕菜单不抢键，放在菜单前后都行，这里放在对话框后面。只需要写一个新类、改一行组装代码，其他处理者不用动：

```cpp
// 《责任链模式》练习 2：加一个「截图标注」层，只写一个新类、改一行组装代码
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

class KeyHandler {                          // 处理者：保存下一个处理者
 public:
  virtual ~KeyHandler() = default;
  KeyHandler* setNext(KeyHandler* next) { next_ = next; return next; }
  virtual std::string handle(Key k) {      // 默认实现：自己不处理，交给下一个
    return next_ ? next_->handle(k) : "没人处理";
  }
 private:
  KeyHandler* next_ = nullptr;
};

class Dialog : public KeyHandler {          // 模态对话框：开着时吞掉所有按键
 public:
  bool open = false;
  std::string handle(Key k) override {
    if (!open) return KeyHandler::handle(k);
    if (k == Key::Enter) { open = false; return "对话框：继续播放"; }
    if (k == Key::Esc) { open = false; return "对话框：从头播放"; }
    return "对话框：忽略";
  }
};

class SubtitleMenu : public KeyHandler {    // 字幕菜单：只管 ↑ ↓ 回车 Esc
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
    return KeyHandler::handle(k);          // 不归我管：交给下一个
  }
};

class PlayerKeys : public KeyHandler {      // 播放器本身：链的最后一环
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
      default: return KeyHandler::handle(k);   // 回车：播放器也不管，交给下一个（没有了）
    }
  }
};

class Annotator : public KeyHandler {       // 练习 2 新增：截图标注层，开着时 ← → 移动光标，回车保存，Esc 退出
 public:
  bool open = false;
  int x = 0;
  std::string handle(Key k) override {
    if (open) switch (k) {
      case Key::Left: --x; return "标注：光标左移到 " + std::to_string(x);
      case Key::Right: ++x; return "标注：光标右移到 " + std::to_string(x);
      case Key::Enter: open = false; return "标注：保存截图";
      case Key::Esc: open = false; return "标注：退出";
      default: break;                       // 空格、↑ ↓ 不管：暂停、调音量照常
    }
    return KeyHandler::handle(k);
  }
};

int main() {
  Dialog dialog; Annotator note; SubtitleMenu menu; PlayerKeys player;
  dialog.setNext(&note)->setNext(&menu)->setNext(&player);   // 只改了这一行：标注层插在对话框后面
  const char* names[] = {"空格", "Esc", "↑", "↓", "←", "→", "回车"};
  note.open = true;
  for (Key k : {Key::Right, Key::Right, Key::Space, Key::Up, Key::Enter, Key::Right})
    std::printf("按 %s：%s\n", names[int(k)], dialog.handle(k).c_str());
}
```

输出（空格和 ↑ 穿过了标注层，标注层保存截图后关闭，最后的 → 回到了播放器）：

```text
按 →：标注：光标右移到 1
按 →：标注：光标右移到 2
按 空格：播放器：播放
按 ↑：播放器：音量 +5
按 回车：标注：保存截图
按 →：播放器：快进 5 秒
```

**3.** 用第 3.3 节的模板方法写法：基类的 `handle` 固定「先问自己，不行就交给下一个」，子类只实现 `tryHandle`。重试次数记在请求（`DecodeError`）里，而不是处理者里，这样同一条链可以被多个解码器共用。内存不足没有处理者认领，走到链尾，由基类返回「放弃」。

```cpp
// 《责任链模式》练习 3：解码出错时的处理链——重试 → 跳到下一个关键帧 → 换成软解 → 放弃
#include <cstdio>
#include <string>

enum class Err { Again, Corrupt, HwLost, NoMemory };
struct DecodeError { Err kind; int pts; int retries = 0; bool onHw = false; };

class ErrorHandler {
 public:
  virtual ~ErrorHandler() = default;
  ErrorHandler* setNext(ErrorHandler* next) { next_ = next; return next; }
  std::string handle(DecodeError& e) {       // 模板方法：子类只回答「能不能处理」，转发由基类负责，不会忘
    if (std::string r = tryHandle(e); !r.empty()) return r;
    return next_ ? next_->handle(e) : "放弃：停止播放并报错";
  }
 protected:
  virtual std::string tryHandle(DecodeError& e) = 0;
 private:
  ErrorHandler* next_ = nullptr;
};

class Retry : public ErrorHandler {         // 暂时性错误：最多重试 2 次
  std::string tryHandle(DecodeError& e) override {
    if (e.kind != Err::Again || e.retries >= 2) return "";
    return "重试第 " + std::to_string(++e.retries) + " 次";
  }
};
class SkipToKeyframe : public ErrorHandler {   // 数据损坏：丢掉到下一个关键帧之前的包
  std::string tryHandle(DecodeError& e) override {
    if (e.kind != Err::Corrupt) return "";
    return "跳到 pts " + std::to_string((e.pts / 48 + 1) * 48) + " 的关键帧";   // 假设每 48 帧一个关键帧
  }
};
class FallbackToSoftware : public ErrorHandler {   // 硬件设备丢了（比如显卡驱动重置）：换软解
  std::string tryHandle(DecodeError& e) override {
    if (e.kind != Err::HwLost || !e.onHw) return "";
    e.onHw = false;
    return "关掉硬解，换成软解从 pts " + std::to_string(e.pts) + " 继续";
  }
};

int main() {
  Retry retry; SkipToKeyframe skip; FallbackToSoftware fallback;
  retry.setNext(&skip)->setNext(&fallback);
  DecodeError again{Err::Again, 10};
  for (int i = 0; i < 3; ++i) std::printf("EAGAIN  pts 10：%s\n", retry.handle(again).c_str());
  DecodeError corrupt{Err::Corrupt, 61};
  std::printf("损坏    pts 61：%s\n", retry.handle(corrupt).c_str());
  DecodeError lost{Err::HwLost, 130, 0, true};
  std::printf("设备丢失 pts 130：%s\n", retry.handle(lost).c_str());
  DecodeError oom{Err::NoMemory, 200};
  std::printf("内存不足 pts 200：%s\n", retry.handle(oom).c_str());
}
```

```text
EAGAIN  pts 10：重试第 1 次
EAGAIN  pts 10：重试第 2 次
EAGAIN  pts 10：放弃：停止播放并报错
损坏    pts 61：跳到 pts 96 的关键帧
设备丢失 pts 130：关掉硬解，换成软解从 pts 130 继续
内存不足 pts 200：放弃：停止播放并报错
```

**4.** `Playlist::handle` 的 `default` 分支 `return "";`：作者以为返回空串就是「不归我管」，但在 GoF 写法里，「不归我管」的意思是**调用 `KeyHandler::handle(k)` 交给下一个**。返回空串等于把空格和 → 吞掉了，后面的播放器根本收不到。输出：

```text
按 ↓：播放列表：下移
按 空格：（没有反应）
按 →：（没有反应）
按 Esc：播放列表：关闭
按 空格：播放器：播放
```

修正就是把 `default` 分支改成转发：

```cpp
// 《责任链模式》练习 4 的修正：不处理的键交给下一个处理者
#include <cstdio>
#include <string>

enum class Key { Space, Esc, Up, Down, Left, Right, Enter };

class KeyHandler {                          // 处理者：保存下一个处理者
 public:
  virtual ~KeyHandler() = default;
  KeyHandler* setNext(KeyHandler* next) { next_ = next; return next; }
  virtual std::string handle(Key k) {      // 默认实现：自己不处理，交给下一个
    return next_ ? next_->handle(k) : "没人处理";
  }
 private:
  KeyHandler* next_ = nullptr;
};

class Dialog : public KeyHandler {          // 模态对话框：开着时吞掉所有按键
 public:
  bool open = false;
  std::string handle(Key k) override {
    if (!open) return KeyHandler::handle(k);
    if (k == Key::Enter) { open = false; return "对话框：继续播放"; }
    if (k == Key::Esc) { open = false; return "对话框：从头播放"; }
    return "对话框：忽略";
  }
};

class SubtitleMenu : public KeyHandler {    // 字幕菜单：只管 ↑ ↓ 回车 Esc
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
    return KeyHandler::handle(k);          // 不归我管：交给下一个
  }
};

class Playlist : public KeyHandler {        // 播放列表（新增）：也只管 ↑ ↓ 回车 Esc
 public:
  bool open = false;
  int sel = 0;
  std::string handle(Key k) override {
    if (!open) return KeyHandler::handle(k);
    switch (k) {
      case Key::Up: --sel; return "播放列表：上移";
      case Key::Down: ++sel; return "播放列表：下移";
      case Key::Enter: open = false; return "播放列表：播放选中项";
      case Key::Esc: open = false; return "播放列表：关闭";
      default: return KeyHandler::handle(k);   // 修正：不归我管的键要交给下一个，而不是返回空串
    }
  }
};

class PlayerKeys : public KeyHandler {      // 播放器本身：链的最后一环
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
      default: return KeyHandler::handle(k);   // 回车：播放器也不管，交给下一个（没有了）
    }
  }
};


int main() {
  Dialog dialog; SubtitleMenu menu; Playlist list; PlayerKeys player;
  dialog.setNext(&menu)->setNext(&list)->setNext(&player);
  const char* names[] = {"空格", "Esc", "↑", "↓", "←", "→", "回车"};
  list.open = true;                         // 打开播放列表，边选边看
  for (Key k : {Key::Down, Key::Space, Key::Right, Key::Esc, Key::Space}) {
    std::string r = dialog.handle(k);
    std::printf("按 %s：%s\n", names[int(k)], r.empty() ? "（没有反应）" : r.c_str());
  }
}
```

从设计上避免：用第 3.3 节的第 2 种写法，基类的 `handle` 不是虚函数，固定「子类的 `tryHandle` 返回空串就交给下一个」，子类只回答能不能处理，想吞掉请求也吞不掉（练习 3 就是这么写的）。或者像第 4.1 节那样，由链（`dispatch` 的循环）负责问下一个，处理者只返回结果。

</details>
