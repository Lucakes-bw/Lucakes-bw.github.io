---
title: 中介者模式
date: 2026-11-19 12:00:00
updated: 2026-11-19 12:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 中介者模式
  - 回调
  - 重入
  - ASan
order: 9
description: 视频导出对话框里有格式选择、视频面板、音频面板、导出按钮四个控件，彼此互相联动。让控件两两直接持有指针，再加一个「只导出片段」面板：4 个旧控件全部要改，控件间直接持有的邻居指针涨到 17 个，还漏了两处联动，48 种组合里有 10 种和规格不一致。改成中介者模式（每个控件只认识中介者，联动规则集中在 ExportDialog::changed() 一处），邻居指针降到 0 个，加片段面板时 4 个已有控件一行不改，48 种组合全对。接着实测中介者的两个常见坑：进度条滑块与时间输入框互相通知引起的乒乓递归（-O0 下直接栈溢出，退出码 139），用 syncing_ 守卫拦住；同事析构忘了注销导致的悬空指针，用 ASan 抓到 heap-use-after-free。最后写出现代 C++ 的 std::function 回调中介者，实测四种写法的协调开销与对象大小，并分清中介者和观察者、事件总线、外观、责任链。
top_group_list: true
disableNunjucks: true
viz: [dp-mediator]
---

> **本文目标**：理解中介者模式（把多对多网状交互收进星形调度中心）；量出新增控件时改动处数与邻居指针的变化；解决中介者三大工程问题（乒乓重入栈溢出、悬空同事指针 ASan 报错、防上帝类）；掌握零侵入的现代 C++ `std::function` 回调中介者与性能实测结论；分清中介者与观察者、事件总线、外观、责任链。
>
> **前置知识**：《观察者模式》《外观模式》；可结合 Qt 的对话框与信号槽对照阅读。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整 48 组合校验、压测、栈溢出与 ASan 复现脚本见 `projects/dp-lab/26-mediator/`。

## 1. 问题：互相牵扯的导出对话框

### 1.1 四个控件，两两互指

视频导出对话框里有 4 个互相联动的控件：

- **格式选择（`FormatSelect`）**：`mp4` / `gif` / `wav`。选 `gif` 禁用音频、锁死 `gif` 编码且禁用 CRF；选 `wav` 禁用视频、音频切 `pcm`；切回 `mp4` 恢复 `h264` / `aac`。
- **视频面板（`VideoPanel`）**：选 `copy`（流拷贝）时禁用 CRF；`mp4 + h264` 下要求分辨率宽高都是偶数。
- **音频面板（`AudioPanel`）**：编码器 `aac` / `pcm` / `copy`。
- **导出按钮（`ExportButton`）**：参数合法才能点，否则禁用并显示原因。

最直接的写法是**谁要改谁，就持有谁的指针**：

```cpp
// 《中介者模式》：问题版——四个控件两两直接持有邻居指针，联动规则散落各处
#include <cstdio>
#include <string>

struct FormatSelect; struct VideoPanel; struct AudioPanel; struct ExportButton;

struct FormatSelect {
  std::string fmt = "mp4";
  VideoPanel* video = nullptr; AudioPanel* audio = nullptr; ExportButton* btn = nullptr;
  void choose(std::string f);
};
struct VideoPanel {
  bool enabled = true, crfEnabled = true;
  std::string codec = "h264";
  int width = 1280, height = 720;
  FormatSelect* format = nullptr; ExportButton* btn = nullptr;
  void chooseCodec(std::string c);
  void setSize(int w, int h);
};
struct AudioPanel {
  bool enabled = true; std::string codec = "aac";
  FormatSelect* format = nullptr; VideoPanel* video = nullptr; ExportButton* btn = nullptr;
};
struct ExportButton {
  bool enabled = true; std::string hint = "就绪";
  FormatSelect* format = nullptr; VideoPanel* video = nullptr; AudioPanel* audio = nullptr;
  void refresh() {
    if (video->enabled && (video->width <= 0 || video->height <= 0)) { enabled = false; hint = "分辨率必须大于 0"; return; }
    if (format->fmt == "mp4" && video->enabled && video->codec != "copy" &&
        (video->width % 2 != 0 || video->height % 2 != 0)) { enabled = false; hint = "h264 编码要求宽高都是偶数"; return; }
    enabled = true; hint = "就绪";
  }
};

void FormatSelect::choose(std::string f) {
  fmt = std::move(f);
  if (fmt == "mp4") {
    video->enabled = true; if (video->codec == "gif") video->codec = "h264";
    video->crfEnabled = (video->codec != "copy");
    audio->enabled = true; if (audio->codec == "pcm") audio->codec = "aac";
  } else if (fmt == "gif") {
    video->enabled = true; video->codec = "gif"; video->crfEnabled = false; audio->enabled = false;
  } else if (fmt == "wav") {
    video->enabled = false; video->crfEnabled = false; audio->enabled = true; audio->codec = "pcm";
  }
  btn->refresh();
}
void VideoPanel::chooseCodec(std::string c) {
  if (!enabled) return;
  codec = (format->fmt == "gif") ? "gif" : (c == "gif" ? "h264" : std::move(c));
  crfEnabled = (codec == "h264"); btn->refresh();
}
void VideoPanel::setSize(int w, int h) { width = w; height = h; btn->refresh(); }

int main() {
  FormatSelect fmt; VideoPanel video; AudioPanel audio; ExportButton btn;
  // 构造后必须手工接上 11 条邻居指针
  fmt.video = &video; fmt.audio = &audio; fmt.btn = &btn;
  video.format = &fmt; video.btn = &btn;
  audio.format = &fmt; audio.video = &video; audio.btn = &btn;
  btn.format = &fmt; btn.video = &video; btn.audio = &audio;

  auto dump = [&](const char* step) {
    std::printf("%-14s → fmt=%-3s video=%s(%s,crf=%s) audio=%s(%s) btn=%s(%s)\n",
                step, fmt.fmt.c_str(), video.enabled ? "开" : "关", video.codec.c_str(),
                video.crfEnabled ? "开" : "关", audio.enabled ? "开" : "关",
                audio.codec.c_str(), btn.enabled ? "可点" : "禁用", btn.hint.c_str());
  };
  fmt.choose("gif");      dump("切到 gif");
  fmt.choose("mp4");      dump("切回 mp4");
  video.setSize(641, 360); dump("改奇数宽 641");
  video.chooseCodec("copy"); dump("改流拷贝 copy");
}
```

```text
切到 gif     → fmt=gif video=开(gif,crf=关) audio=关(aac) btn=可点(就绪)
切回 mp4     → fmt=mp4 video=开(h264,crf=开) audio=开(aac) btn=可点(就绪)
改奇数宽 641 → fmt=mp4 video=开(h264,crf=开) audio=开(aac) btn=禁用(h264 编码要求宽高都是偶数)
改流拷贝 copy → fmt=mp4 video=开(copy,crf=关) audio=开(aac) btn=可点(就绪)
```

### 1.2 加一个 `RangePanel`：改动爆炸与漏改

新需求：加一个「只导出片段」面板 `RangePanel`，规则有三条：① `end <= start` 禁用导出；② `gif` 片段超过 15 秒禁用导出；③ 开启片段且视频编码为 `copy` 时提示「流拷贝只能从关键帧切」，否则提示「精确裁剪」。

在网状写法里（完整复现见 `projects/dp-lab/26-mediator/mesh_dialog_range.cpp`），4 个旧控件几乎全要加 `RangePanel*` 并改逻辑，而且极易漏改：
- **漏改 1**：在 `ExportButton::refresh()` 忘了补「GIF 片段 > 15 秒」检查；
- **漏改 2**：在 `RangePanel::setRange()` 更新了提示语，却忘了另一条路径——用户**先勾片段再切 `copy`** 走的是 `VideoPanel::chooseCodec()`，提示语停留在「精确裁剪」。

`measure.sh` 实测结果：**8 处改动**（4 个旧控件全动），控件间邻居指针涨到 **17 个**，48 种「格式 × 片段 × 编码 × 分辨率」组合里有 **10 种与规格不一致**。

<figure class="diagram">
<svg viewBox="0 0 700 232" role="img" aria-label="左边是 5 个控件两两相连的网状结构，有 17 个邻居指针；右边是星形结构，5 个控件都只连接中间的 ExportDialog，彼此之间没有连线"><g style="font-family:var(--mono)"><text x="160.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">网状互指（多对多）</text><line x1="160" y1="54" x2="62" y2="114" style="stroke:var(--danger);stroke-width:1.3"/><line x1="160" y1="54" x2="258" y2="114" style="stroke:var(--danger);stroke-width:1.3"/><line x1="160" y1="54" x2="96" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="160" y1="54" x2="224" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="62" y1="114" x2="258" y2="114" style="stroke:var(--danger);stroke-width:1.3"/><line x1="62" y1="114" x2="96" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="62" y1="114" x2="224" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="258" y1="114" x2="96" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="258" y1="114" x2="224" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="96" y1="196" x2="224" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><rect x="115" y="38" width="90" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="160.0" y="56.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Format</text><rect x="18" y="100" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="62.0" y="118.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Video</text><rect x="214" y="100" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="258.0" y="118.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Audio</text><rect x="52" y="182" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="96.0" y="200.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Range</text><rect x="180" y="182" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="224.0" y="200.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Button</text><text x="160.0" y="226.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">最多 n(n−1)/2 条连线，本例 17 个邻居指针</text><text x="530.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">星形结构（中介者）</text><line x1="530" y1="124" x2="530" y2="54" style="stroke:var(--accent);stroke-width:1.6"/><line x1="530" y1="124" x2="412" y2="114" style="stroke:var(--accent);stroke-width:1.6"/><line x1="530" y1="124" x2="648" y2="114" style="stroke:var(--accent);stroke-width:1.6"/><line x1="530" y1="124" x2="446" y2="196" style="stroke:var(--accent);stroke-width:1.6"/><line x1="530" y1="124" x2="614" y2="196" style="stroke:var(--accent);stroke-width:1.6"/><rect x="485" y="38" width="90" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="530.0" y="56.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Format</text><rect x="368" y="100" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="412.0" y="118.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Video</text><rect x="604" y="100" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="648.0" y="118.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Audio</text><rect x="402" y="182" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="446.0" y="200.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Range</text><rect x="570" y="182" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="614.0" y="200.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Button</text><rect x="476" y="106" width="108" height="36" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.8"/><text x="530.0" y="128.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">ExportDialog</text><text x="530.0" y="226.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">n 条连线，控件间 0 个邻居指针</text></g></svg>
</figure>

根源在于 $n$ 个对象两两互指会形成最多 $n(n-1)/2$ 条边：操作顺序不同走不同函数极易遗漏，控件绑死邻居无法复用，加一个控件波及所有旧类。

## 2. 中介者模式

### 2.1 意图与角色

> **GoF**：用一个中介对象来封装一系列的对象交互。中介者使各对象不需要显式地相互引用，从而使其耦合松散，而且可以独立地改变它们之间的交互。

大白话：**一群对象不要私下互相打电话，统一跟中间的「塔台」说话。** 三个角色：
- **Mediator（中介者接口）**：定义同事汇报变化的入口（`DialogMediator::changed(Widget*)`）；
- **ConcreteMediator（具体中介者）**：持有各同事，集中维护协作规则（`ExportDialog`）；
- **Colleague（同事）**：只持有中介者指针，互不知道对方，变化时调 `notify()`。

### 2.2 用中介者重构导出对话框

所有控件继承极轻量的 `Widget`，四条联动规则全部收进 `ExportDialog::changed()` 统一重算：

```cpp
// 《中介者模式》：控件只认识 DialogMediator，联动规则集中在 ExportDialog::changed() 一处
#include <cstdio>
#include <string>

struct Widget;
struct DialogMediator {
  virtual ~DialogMediator() = default;
  virtual void changed(Widget* sender) = 0;
};
struct Widget {
  DialogMediator* med = nullptr;
  explicit Widget(DialogMediator* m) : med(m) {}
  void notify() { if (med) med->changed(this); }
};

struct FormatSelect : Widget {
  std::string fmt = "mp4";
  using Widget::Widget;
  void choose(std::string f) { fmt = std::move(f); notify(); }
};
struct VideoPanel : Widget {
  bool enabled = true, crfEnabled = true;
  std::string codec = "h264";
  int width = 1280, height = 720;
  using Widget::Widget;
  void chooseCodec(std::string c) { if (enabled) { codec = std::move(c); notify(); } }
  void setSize(int w, int h) { width = w; height = h; notify(); }
};
struct AudioPanel : Widget { bool enabled = true; std::string codec = "aac"; using Widget::Widget; };
struct ExportButton : Widget { bool enabled = true; std::string hint = "就绪"; using Widget::Widget; };

struct ExportDialog : DialogMediator {
  FormatSelect format{this};
  VideoPanel video{this};
  AudioPanel audio{this};
  ExportButton btn{this};

  void changed(Widget*) override {            // 统一按当前状态重算，与操作顺序无关
    if (format.fmt == "mp4") {
      video.enabled = true; if (video.codec == "gif") video.codec = "h264";
      video.crfEnabled = (video.codec == "h264");
      audio.enabled = true; if (audio.codec == "pcm") audio.codec = "aac";
    } else if (format.fmt == "gif") {
      video.enabled = true; video.codec = "gif"; video.crfEnabled = false; audio.enabled = false;
    } else if (format.fmt == "wav") {
      video.enabled = false; video.crfEnabled = false; audio.enabled = true; audio.codec = "pcm";
    }

    if (video.enabled && (video.width <= 0 || video.height <= 0)) {
      btn.enabled = false; btn.hint = "分辨率必须大于 0";
    } else if (format.fmt == "mp4" && video.enabled && video.codec != "copy" &&
               (video.width % 2 != 0 || video.height % 2 != 0)) {
      btn.enabled = false; btn.hint = "h264 编码要求宽高都是偶数";
    } else {
      btn.enabled = true; btn.hint = "就绪";
    }
  }
};

int main() {
  ExportDialog dlg;
  auto dump = [&](const char* step) {
    std::printf("%-14s → fmt=%-3s video=%s(%s,crf=%s) audio=%s(%s) btn=%s(%s)\n",
                step, dlg.format.fmt.c_str(), dlg.video.enabled ? "开" : "关",
                dlg.video.codec.c_str(), dlg.video.crfEnabled ? "开" : "关",
                dlg.audio.enabled ? "开" : "关", dlg.audio.codec.c_str(),
                dlg.btn.enabled ? "可点" : "禁用", dlg.btn.hint.c_str());
  };
  dlg.format.choose("gif");      dump("切到 gif");
  dlg.format.choose("mp4");      dump("切回 mp4");
  dlg.video.setSize(641, 360);   dump("改奇数宽 641");
  dlg.video.chooseCodec("copy"); dump("改流拷贝 copy");
}
```

输出与第 1.1 节完全一致，但控件间零邻居指针，且统一重算让「先勾片段再切 `copy`」与反向操作走同一段代码，消灭路径遗漏。

### 2.3 类图、时序图与演示

```uml
label: 中介者模式的类图：DialogMediator 是中介者接口，ExportDialog 实现它并组合四个控件；Widget 持有 DialogMediator 指针，四个控件继承 Widget，控件之间没有连线
classes:
  - { id: DialogMediator, stereo: interface, x: 20, y: 20, w: 220, ops: ['*+ changed(sender: Widget*)'] }
  - { id: Widget, x: 380, y: 20, w: 220, attrs: ['# med : DialogMediator*'], ops: ['+ notify()'] }
  - { id: ExportDialog, x: 20, y: 175, w: 220, ops: ['+ changed(sender: Widget*)'] }
  - { id: FormatSelect, x: 300, y: 180, w: 115 }
  - { id: VideoPanel, x: 430, y: 180, w: 105 }
  - { id: AudioPanel, x: 550, y: 180, w: 105 }
  - { id: ExportButton, x: 670, y: 180, w: 115 }
rels:
  - { from: Widget, to: DialogMediator, type: assoc, label: 'med' }
  - { from: ExportDialog, to: DialogMediator, type: realize }
  - { from: FormatSelect, to: Widget, type: inherit, mid: 140 }
  - { from: VideoPanel, to: Widget, type: inherit, mid: 140 }
  - { from: AudioPanel, to: Widget, type: inherit, mid: 140 }
  - { from: ExportButton, to: Widget, type: inherit, mid: 140 }
  - { from: ExportDialog, to: FormatSelect, type: compose, points: [[130, 232], [130, 260], [357, 260], [357, 210]] }
  - { from: ExportDialog, to: VideoPanel, type: compose, points: [[130, 232], [130, 260], [482, 260], [482, 210]] }
  - { from: ExportDialog, to: AudioPanel, type: compose, points: [[130, 232], [130, 260], [602, 260], [602, 210]] }
  - { from: ExportDialog, to: ExportButton, type: compose, points: [[130, 232], [130, 260], [727, 260], [727, 210]] }
```

```uml
label: 时序图：用户调用 FormatSelect 的 choose("gif")，FormatSelect 改完自身字段后调用 ExportDialog 的 changed()，ExportDialog 依次更新 VideoPanel、AudioPanel 和 ExportButton
participants:
  - { id: u, name: '用户', x: 10, w: 60 }
  - { id: f, name: 'FormatSelect', x: 115, w: 110 }
  - { id: d, name: 'ExportDialog', x: 275, w: 110 }
  - { id: v, name: 'VideoPanel', x: 435, w: 100 }
  - { id: a, name: 'AudioPanel', x: 575, w: 100 }
  - { id: b, name: 'ExportButton', x: 715, w: 110 }
messages:
  - { from: u, to: f, text: 'choose("gif")', n: 1 }
  - { from: f, to: d, text: 'changed(this)', n: 2 }
  - { from: d, to: v, text: 'codec="gif", crf=false', n: 3 }
  - { from: d, to: a, text: 'enabled=false', n: 4 }
  - { from: d, to: b, text: 'enabled=true, "就绪"', n: 5 }
  - { from: d, to: f, text: '', type: reply }
  - { from: f, to: u, text: '', type: reply }
```

下面的演示展示了四个场景的星形协调过程：

<div class="dsviz" data-viz="dp-mediator"></div>

### 2.4 再加 `RangePanel`：数据对比

给中介者版加上 `RangePanel`（见 `projects/dp-lab/26-mediator/mediator_dialog_range.cpp`）：**4 个旧控件一行不改**，只需新增 `RangePanel` 并在 `ExportDialog::changed()` 补上规则，48 种组合 0 处不一致：

<figure class="diagram">
<svg viewBox="0 0 680 144" role="img" aria-label="加 RangePanel 的改动对比：网状互指改了 8 处、4 个旧控件全动了、控件间有 17 个邻居指针；中介者改了 4 处、0 个旧控件被改、0 个邻居指针"><g style="font-family:var(--mono)"><text x="108.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">改动处数（diff hunk）</text><rect x="108.0" y="30.0" width="128.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="242.0" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">8 处（4 个旧控件全改）</text><rect x="108.0" y="60.0" width="64.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="178.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">4 处（0 个旧控件改动）</text><text x="418.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">控件间直接持有的邻居指针</text><rect x="418.0" y="30.0" width="170.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="594.0" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">17 个</text><rect x="418.0" y="60.0" width="2.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="426.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 个</text><text x="100.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">网状互指</text><text x="100.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">中介者</text><text x="10.0" y="114.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">48 种组合逐项对照规格：网状版 10 种不一致，中介者版 0 种不一致</text><text x="10.0" y="132.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：diff 只比较类与函数定义（不含 main 里的测试）</text></g></svg>
</figure>

## 3. 深入中介者：重入、生命周期与上帝类

### 3.1 乒乓重入与 `syncing_` 守卫

时间栏里有帧号滑块 `Slider`（24 fps 整数帧）与秒数输入框 `SpinBox`（保留 2 位小数）：拖滑块到第 `1` 帧 → 输入框四舍五入成 `0.04` 秒 → 输入框通知中介者反算帧号 `int(0.04 * 24) = 0` → 滑块又改回 `0` 并再次通知……不加保护时在 `-O0` 下直接栈溢出（`projects/dp-lab/26-mediator/pingpong.cpp` 传 `nbl` 退出码 **139**）：

```cpp
// 《中介者模式》：滑块与输入框双向联动——无重入保护会来回乒乓，用 syncing_ 守卫拦住
#include <cmath>
#include <cstdio>

struct TimebarMediator { virtual ~TimebarMediator() = default; virtual void onChanged(void* sender) = 0; };

struct Slider {
  TimebarMediator* med = nullptr;
  int frame = 0, updates = 0;
  void setFrame(int f) { frame = f; ++updates; if (med) med->onChanged(this); }
};
struct SpinBox {
  TimebarMediator* med = nullptr;
  double sec = 0.0; int updates = 0;
  void setSec(double s) { sec = std::round(s * 100.0) / 100.0; ++updates; if (med) med->onChanged(this); }
};

struct NaiveBar : TimebarMediator {           // 无重入保护（这里用 depth<6 拦住以便打印）
  Slider slider{this}; SpinBox spin{this}; int depth = 0;
  void onChanged(void* s) override {
    if (depth >= 6) return;
    ++depth;
    if (s == &slider) spin.setSec(slider.frame / 24.0);
    else if (s == &spin) slider.setFrame(static_cast<int>(spin.sec * 24.0));
    --depth;
  }
};

struct SafeBar : TimebarMediator {            // syncing_ 守卫：正在同步时忽略二次通知
  Slider slider{this}; SpinBox spin{this}; bool syncing_ = false;
  void onChanged(void* s) override {
    if (syncing_) return;
    syncing_ = true;
    if (s == &slider) spin.setSec(slider.frame / 24.0);
    else if (s == &spin) slider.setFrame(static_cast<int>(spin.sec * 24.0));
    syncing_ = false;
  }
};

int main() {
  NaiveBar b1; b1.slider.setFrame(1);
  std::printf("无重入保护（拦在 6 层）：frame=%d sec=%.2f，修改次数 slider=%d spin=%d\n",
              b1.slider.frame, b1.spin.sec, b1.slider.updates, b1.spin.updates);
  SafeBar b2;  b2.slider.setFrame(1);
  std::printf("带 syncing_ 守卫       ：frame=%d sec=%.2f，修改次数 slider=%d spin=%d\n",
              b2.slider.frame, b2.spin.sec, b2.slider.updates, b2.spin.updates);
}
```

```text
无重入保护（拦在 6 层）：frame=0 sec=0.00，修改次数 slider=4 spin=3
带 syncing_ 守卫       ：frame=1 sec=0.04，修改次数 slider=1 spin=1
```

解决重入的三种做法：在中介者放 `bool syncing_`（有提前 `return` / 异常时用 RAII 守卫自动复位）；使用框架提供的信号屏蔽器（如 Qt 的 `QSignalBlocker`）；或像第 2 节那样，中介者直接改控件内部字段 / 调用不发通知的内部 setter。

### 3.2 同事析构后的悬空指针（ASan 实测）

聊天室、动态插拔面板里，中介者常存 `std::vector<Peer*>`。若局部同事 `osd` 构造时注册、析构时忘了注销，下次广播就会读写已释放内存（`projects/dp-lab/26-mediator/dangling_colleague.cpp` 用 `-fsanitize=address` 实测）：

```text
==27333==ERROR: AddressSanitizer: heap-use-after-free on address 0x603000000fe7
READ of size 1 at 0x603000000fe7 thread T0
    #4 in Peer::onMessage(...) dangling_colleague.cpp:22
    #5 in RoomMediator::broadcast(...) dangling_colleague.cpp:28
```

三条治法：① **优先让中介者直接拥有同事**（值成员或 `unique_ptr`），从结构上消灭生命周期错位；② 动态同事在 `~Peer()` 自动注销，并禁止拷贝/移动（或移动时更新注册表）；③ 销毁顺序不确定时用 `std::weak_ptr` 或 RAII 连接句柄。

### 3.3 防止中介者长成上帝类

中介者并没有消灭复杂度，只是把复杂度从网状搬到了中心。防它膨胀成 2000 行上帝类的三条边界：
1. **纯逻辑抽成无状态函数**：参数校验、拼 FFmpeg 命令行只收纯数据 `ExportConfig`，中介者只负责「读控件 → 调纯函数 → 写控件」；
2. **按页签拆分层中介者**：`VideoTabMediator`、`AudioTabMediator` 管各自内部，顶层 `ExportDialog` 只管跨页签联动；
3. **只放跨同事规则**：单控件自身约束（如 CRF ∈ [0, 51]）留在控件内部。

真实工程里，**Qt 的 `QDialog` / `QMainWindow`**（控件发信号，对话框集中 `connect` 调度）、**音视频会议室服务端 `Room`**、**空中交通管制塔台**都是典型中介者。

## 4. 现代 C++ 写法与开销实测

### 4.1 零侵入：`std::function` 回调中介者

经典 GoF 写法要求控件继承 `Widget`。在现代 C++ 里，控件只需暴露 `std::function<void()> onChange`，完全不知道中介者的存在；中介者在构造函数里用 lambda 绑定：

```cpp
// 《中介者模式》：现代 C++——控件零侵入，只暴露 std::function 回调；中介者用 lambda 连接
#include <cstdio>
#include <functional>
#include <string>

struct FormatSelect {
  std::string fmt = "mp4";
  std::function<void()> onChange;
  void choose(std::string f) { fmt = std::move(f); if (onChange) onChange(); }
};
struct VideoPanel {
  bool enabled = true, crfEnabled = true;
  std::string codec = "h264";
  int width = 1280, height = 720;
  std::function<void()> onChange;
  void chooseCodec(std::string c) { if (enabled) { codec = std::move(c); if (onChange) onChange(); } }
  void setSize(int w, int h) { width = w; height = h; if (onChange) onChange(); }
};
struct AudioPanel { bool enabled = true; std::string codec = "aac"; };
struct ExportButton { bool enabled = true; std::string hint = "就绪"; };

struct ExportDialog {
  FormatSelect format; VideoPanel video; AudioPanel audio; ExportButton btn;

  ExportDialog() {
    format.onChange = [this] { sync(); };
    video.onChange  = [this] { sync(); };
  }
  ExportDialog(const ExportDialog&) = delete;            // 必删！防止 lambda 捕获的 this 悬空
  ExportDialog& operator=(const ExportDialog&) = delete;

  void sync() {
    if (format.fmt == "mp4") {
      video.enabled = true; if (video.codec == "gif") video.codec = "h264";
      video.crfEnabled = (video.codec == "h264");
      audio.enabled = true; if (audio.codec == "pcm") audio.codec = "aac";
    } else if (format.fmt == "gif") {
      video.enabled = true; video.codec = "gif"; video.crfEnabled = false; audio.enabled = false;
    } else if (format.fmt == "wav") {
      video.enabled = false; video.crfEnabled = false; audio.enabled = true; audio.codec = "pcm";
    }
    if (video.enabled && (video.width <= 0 || video.height <= 0)) {
      btn.enabled = false; btn.hint = "分辨率必须大于 0";
    } else if (format.fmt == "mp4" && video.enabled && video.codec != "copy" &&
               (video.width % 2 != 0 || video.height % 2 != 0)) {
      btn.enabled = false; btn.hint = "h264 编码要求宽高都是偶数";
    } else {
      btn.enabled = true; btn.hint = "就绪";
    }
  }
  void dump(const char* step) const {
    std::printf("%-16s → fmt=%-3s video=%s(%s,crf=%s) audio=%s(%s) btn=%s(%s)\n",
                step, format.fmt.c_str(), video.enabled ? "开" : "关", video.codec.c_str(),
                video.crfEnabled ? "开" : "关", audio.enabled ? "开" : "关",
                audio.codec.c_str(), btn.enabled ? "可点" : "禁用", btn.hint.c_str());
  }
};

int main() {
  ExportDialog dlg;
  dlg.dump("1. 初始状态");
  dlg.format.choose("gif");      dlg.dump("2. 切到 gif");
  dlg.format.choose("mp4");      dlg.dump("3. 切回 mp4");
  dlg.video.chooseCodec("copy"); dlg.dump("4. 选 copy");
  dlg.video.setSize(641, 360);   dlg.dump("5. 奇数宽(copy)");
  dlg.video.chooseCodec("h264"); dlg.dump("6. 切回 h264");
}
```

```text
1. 初始状态  → fmt=mp4 video=开(h264,crf=开) audio=开(aac) btn=可点(就绪)
2. 切到 gif    → fmt=gif video=开(gif,crf=关) audio=关(aac) btn=可点(就绪)
3. 切回 mp4    → fmt=mp4 video=开(h264,crf=开) audio=开(aac) btn=可点(就绪)
4. 选 copy      → fmt=mp4 video=开(copy,crf=关) audio=开(aac) btn=可点(就绪)
5. 奇数宽(copy) → fmt=mp4 video=开(copy,crf=关) audio=开(aac) btn=可点(就绪)
6. 切回 h264   → fmt=mp4 video=开(h264,crf=开) audio=开(aac) btn=禁用(h264 编码要求宽高都是偶数)
```

务必记住：`ExportDialog` 必须删除拷贝和移动，因为 lambda 捕获了 `this`；一旦对象搬家，旧 `this` 立即悬空。

### 4.2 开销、大小实测与选型

用 `projects/dp-lab/26-mediator/bench.cpp` 实测 419 万次联动（Apple clang 21，`-O2`，7 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 680 184" role="img" aria-label="四种中介者写法的耗时与同事对象大小：网状直接调用 1.81 ns（32 字节），虚函数中介者 Widget* 0.90 ns（16 字节），虚函数中介者枚举事件 0.74 ns（16 字节），std::function 回调 0.78 ns（40 字节）"><g style="font-family:var(--mono)"><text x="188.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">每次联动耗时（Clang -O2）</text><rect x="188.0" y="30.0" width="162.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="356.9" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">1.81 ns</text><rect x="188.0" y="60.0" width="81.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="275.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.90 ns</text><rect x="188.0" y="90.0" width="66.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="260.6" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.74 ns</text><rect x="188.0" y="120.0" width="70.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="264.2" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.78 ns</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">单个同事 sizeof</text><rect x="458.0" y="30.0" width="96.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="560.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">32 B（3 个邻居指针）</text><rect x="458.0" y="60.0" width="48.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="512.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">16 B（1 个中介指针）</text><rect x="458.0" y="90.0" width="48.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="512.0" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">16 B（1 个中介指针）</text><rect x="458.0" y="120.0" width="120.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="584.0" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">40 B（含 std::function）</text><text x="180.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">网状直接调用</text><text x="180.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数中介者 (Widget*)</text><text x="180.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数中介者 (枚举事件)</text><text x="180.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::function 回调</text><text x="10.0" y="174.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp：Apple clang 21，-O2，4194304 次联动，7 轮取中位数</text></g></svg>
</figure>

| 写法 | 每次联动 | 同事 `sizeof` | 需要基类？ | 适合场景 |
| --- | :---: | :---: | :---: | --- |
| 网状直接调用 | 1.81 ns | 32 B（随邻居增长） | 否 | 仅 2 个对象简单互调；多对象极易漏改 |
| 虚函数中介者（`Widget*`） | 0.90 ns | 16 B | 是 | 自研控件已有公共基类，需接入不同中介者 |
| 虚函数中介者（枚举事件） | 0.74 ns | 16 B | 是 | 同事少、事件清晰，省掉指针地址比较 |
| `std::function` 回调 | 0.78 ns | 40 B | **否** | **现代 C++ 首选**：零侵入、lambda 捕获 `this` 走 SBO 零堆分配 |

所有写法都在 1～2 ns 以内（中介者反而因值成员缓存局部性更好而略快），选型优先看解耦与复用。

## 5. 中介者和相似的模式

| 模式 | 拓扑与通信方向 | 核心区别 |
| --- | --- | --- |
| **观察者** | 一对多，单向 | Subject 只广播「我变了」，**没有协调逻辑**；中介者主动指挥各同事配合 |
| **发布-订阅（事件总线）** | 多对多，单向 | 总线只做类型转发，规则散在各订阅者里；中介者把协作规则**集中**在一处 |
| **外观** | 单向（外部 → 外观 → 子系统） | 外观给**外部**提供简化入口，子系统不知道外观；中介者协调**内部**双向交互 |
| **责任链** | 链式单向（A → B → C） | 请求沿链找谁处理；中介者是星形网，由中心同时协调多方 |

工程里常把**观察者 + 中介者**搭配：控件用回调 / 信号槽向外报告变化，对话框本身充当中介者集中调度。

## 6. 小结

- 中介者把多对多网状交互变成一对多星形结构：加 `RangePanel` 时旧控件改动从 4 个降到 0 个，邻居指针从 17 个降到 0 个，48 种组合 0 误差。
- 双向联动谨防**乒乓重入**（`-O0` 栈溢出退出码 139），用 `syncing_` 守卫、信号屏蔽器或不发通知的内部赋值拦住。
- 动态同事谨防**悬空指针**（ASan `heap-use-after-free`）：优先由中介者直接拥有同事，否则析构自动注销或用 `weak_ptr`。
- 防中介者变成**上帝类**：抽纯逻辑、按区域分层、只放跨同事规则。
- 现代 C++ 首选 `std::function` 回调中介者（0.78 ns，零侵入），但必须禁止中介者拷贝/移动。

## 7. 练习

1. **辨析**：下面四个场景，哪些适合用中介者？不适合的用什么？
   - (a) 音频播放器进度每变一次，进度条、歌词、字幕、统计上报各自独立更新；
   - (b) 账号注册表单的「手机/邮箱切换」「国家区号」「验证码按钮」「密码强度条」「同意协议」「提交按钮」互相制约；
   - (c) 把 FFmpeg 26 个调用包成一个 `Thumbnailer::save(sec, path)` 给业务层用；
   - (d) 多人在线白板房间：A 锁定画布时禁止 B/C 编辑，有人举手发言时切主画面并暂停他人共享。
2. **改写**：给第 2.2 节的 `ExportDialog` 加一个预设下拉框 `PresetSelect`（`"custom"` / `"moments"` 朋友圈 720×1280 mp4 / `"audio_only"` 仅音频 wav）：选预设时自动填好参数，手动改任意参数后预设自动跳回 `"custom"`。这里还能忽略 `changed(Widget* sender)` 的 `sender` 吗？为什么？
3. **找坑**：下面的多轨道混音器「独奏（Solo）」代码有两个严重 bug：① `voice.setSolo(true)` 后，`背景音乐` 没静音且独奏变成「无」；② ASan 报 `heap-use-after-free`。找出原因并说明怎么改。

<!-- run: off -->
```cpp
// 《中介者模式》练习 3（有 bug）：① 重入把状态冲掉；② 悬空同事指针
struct Track;
struct MixerMediator {
  std::vector<Track*> tracks;
  Track* soloTrack = nullptr;
  void add(Track* t) { tracks.push_back(t); }
  void onSoloChanged(Track* sender);
  void renderFrame();
};
struct Track {
  std::string name; MixerMediator& mixer;
  bool solo = false, muted = false;
  Track(std::string n, MixerMediator& m) : name(std::move(n)), mixer(m) { mixer.add(this); }
  void setSolo(bool s) { solo = s; mixer.onSoloChanged(this); }
};
void MixerMediator::onSoloChanged(Track* sender) {
  if (sender->solo) {
    soloTrack = sender;
    for (Track* t : tracks) {
      if (t != sender) { t->setSolo(false); t->muted = true; }   // 坑 ①
      else t->muted = false;
    }
  } else {
    soloTrack = nullptr;
    for (Track* t : tracks) t->muted = false;
  }
}
int main() {
  MixerMediator mixer;
  Track bgm("背景音乐", mixer), voice("人声旁白", mixer);
  {
    auto preview = std::make_unique<Track>("试听音效", mixer);
    voice.setSolo(true);
    mixer.renderFrame();
  }                                   // 坑 ②：preview 析构后仍留在 mixer.tracks
  mixer.renderFrame();
}
```

<details>
<summary>点击查看答案</summary>

1. (a) **观察者**：一对多独立广播，无互相制约；
   (b) **中介者**：6 个表单控件互相决定启用和校验；
   (c) **外观**：单向简化子系统入口；
   (d) **中介者**：星形房间协调互斥权限和画面切换。

2. **不能**忽略 `sender`：`sender == &preset` 时要按预设覆盖其他控件的值；否则（用户手动改了格式、分辨率或 CRF）要把 `preset.preset` 退回 `"custom"`，之后再统一跑一遍格式联动与按钮校验。完整可运行版见 `projects/dp-lab/26-mediator/ex2_preset.cpp`（另有 5 同事在线会议室设计题见 `ex3_room.cpp`）。

3. 两个 bug 正对应第 3.1 与 3.2 节：
   - **坑 ①（重入冲掉状态）**：`onSoloChanged` 里调 `t->setSolo(false)` 会再次进入 `onSoloChanged(t)` 并落入 `else` 分支，把刚设好的 `soloTrack` 清空、所有轨道取消静音。加 `bool syncing_` 守卫（同时 `setSolo` 在 `solo == s` 时直接返回）即可解决。
   - **坑 ②（悬空指针）**：`preview` 离开作用域析构却没注销。给 `Track` 加 `~Track() { mixer.remove(this); }`（顺便在移除当前 `soloTrack` 时解除独奏恢复其余轨道发声），并删除拷贝构造/赋值。完整修复版见 `projects/dp-lab/26-mediator/ex4_fixed.cpp`。

</details>
