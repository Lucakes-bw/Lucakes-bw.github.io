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

> **本文目标**：理解中介者模式：把一组对象之间网状的双向交互收进一个中介对象，变成星形结构；会把互相持有指针的控件重构成中介者，并量出新增控件时改动处数与邻居指针的变化；复现并解决中介者里的两个典型坑：双向联动引起的乒乓重入（栈溢出）和同事析构后的悬空指针（ASan `heap-use-after-free`）；知道怎么防止中介者长成上帝类；会用 `std::function` 写出对控件零侵入的现代 C++ 中介者，并了解几种写法的开销；分清中介者和观察者、发布-订阅、外观、责任链。
>
> **前置知识**：上一篇《访问者模式》；《观察者模式》（第 5 节重点对比两者的区别与联系）、《外观模式》（第 3.3 节讨论「上帝类」）；Qt 专栏里的对话框和信号槽可以对照着看。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/26-mediator/` 目录，`sh measure.sh` 用 `-O2` 跑一遍本文的实测，并复现栈溢出和 ASan 报告。

## 1. 问题：互相牵扯的导出对话框

### 1.1 四个控件，两两联动

视频软件里最常见的一类界面是**导出设置对话框**。这里有四个控件，但它们不是各自独立的，改一个，另外几个常常要跟着变：

- **格式选择**（`FormatSelect`）：`mp4`、`gif`、`wav`。
  - 选 `gif` 时：GIF 没有音频轨，音频面板要禁用；视频编码锁成 `gif`，不需要设画质参数 CRF；
  - 选 `wav` 时：WAV 只有音频，视频面板禁用，音频编码默认切到未压缩的 `pcm`；
  - 切回 `mp4` 时：视频和音频面板恢复启用，编码恢复 `h264` / `aac`。
- **视频面板**（`VideoPanel`）：选编码器（`h264` / `copy` / `gif`）、设分辨率宽高。
  - 选 `copy`（流拷贝）时：不重新编码，CRF 输入框禁用；
  - 改分辨率时：如果格式是 `mp4` 且不是流拷贝，`h264`（YUV420 色度抽样）要求宽和高都是偶数，否则禁用导出按钮。
- **音频面板**（`AudioPanel`）：选编码器（`aac` / `pcm` / `copy`）。
- **导出按钮**（`ExportButton`）：根据前面三个控件的当前状态决定自己能不能点，不能点时显示原因。

最直接的写法是：**谁需要改谁，就持有谁的指针**。`FormatSelect` 拿着 `VideoPanel*`、`AudioPanel*`、`ExportButton*`；`VideoPanel` 拿着 `FormatSelect*`、`ExportButton*`；`ExportButton` 拿着另外三个的指针：

```cpp
// 《中介者模式》：问题版——四个控件两两直接持有指针，联动规则散落在每个控件里
#include <cstdio>
#include <string>

struct FormatSelect;
struct VideoPanel;
struct AudioPanel;
struct ExportButton;

struct FormatSelect {                 // 容器格式：mp4 / gif / wav
  std::string fmt = "mp4";
  VideoPanel* video = nullptr;
  AudioPanel* audio = nullptr;
  ExportButton* btn = nullptr;
  void choose(std::string f);
};

struct VideoPanel {                   // 视频编码参数：gif 固定用 gif 编码，wav 没有视频轨
  bool enabled = true;
  std::string codec = "h264";         // h264 / copy / gif
  int width = 1280, height = 720, crf = 23;
  bool crfEnabled = true;
  FormatSelect* format = nullptr;
  ExportButton* btn = nullptr;
  void chooseCodec(std::string c);
  void setSize(int w, int h);
};

struct AudioPanel {                   // 音频编码参数：gif 没有音频轨
  bool enabled = true;
  std::string codec = "aac";          // aac / pcm / copy
  FormatSelect* format = nullptr;
  VideoPanel* video = nullptr;
  ExportButton* btn = nullptr;
};

struct ExportButton {                 // 导出按钮：所有开启的面板参数合法时才能点
  bool enabled = true;
  std::string hint = "就绪";
  FormatSelect* format = nullptr;
  VideoPanel* video = nullptr;
  AudioPanel* audio = nullptr;
  void refresh() {
    if (video->enabled && (video->width <= 0 || video->height <= 0)) {
      enabled = false; hint = "分辨率必须大于 0"; return;
    }
    if (format->fmt == "mp4" && video->enabled && video->codec != "copy" &&
        (video->width % 2 != 0 || video->height % 2 != 0)) {
      enabled = false; hint = "h264 编码要求宽高都是偶数"; return;
    }
    enabled = true; hint = "就绪";
  }
};

void FormatSelect::choose(std::string f) {
  fmt = std::move(f);
  if (fmt == "mp4") {
    video->enabled = true;
    if (video->codec == "gif") video->codec = "h264";
    video->crfEnabled = (video->codec != "copy");
    audio->enabled = true;
    if (audio->codec == "pcm") audio->codec = "aac";
  } else if (fmt == "gif") {          // GIF 没有音频轨，视频固定用 gif 编码、不用 CRF
    video->enabled = true;
    video->codec = "gif";
    video->crfEnabled = false;
    audio->enabled = false;
  } else if (fmt == "wav") {          // WAV 只有音频轨，默认用未压缩的 pcm
    video->enabled = false;
    video->crfEnabled = false;
    audio->enabled = true;
    audio->codec = "pcm";
  }
  btn->refresh();
}

void VideoPanel::chooseCodec(std::string c) {
  if (!enabled) return;
  if (format->fmt == "gif") c = "gif";
  else if (c == "gif") c = "h264";
  codec = std::move(c);
  crfEnabled = (codec == "h264");     // copy 和 gif 都不需要设 CRF
  btn->refresh();
}

void VideoPanel::setSize(int w, int h) {
  width = w; height = h;
  btn->refresh();
}

// ---- 以下是测试：遍历「格式 × 视频编码 × 分辨率」的每种操作序列，和规格逐项对照
struct Spec {
  bool vEn, crfEn, aEn, btnEn;
  std::string vCodec, aCodec;
};

Spec expected(const std::string& fmt, const std::string& reqCodec, int w, int h) {
  Spec s{true, true, true, true, "h264", "aac"};
  if (fmt == "gif") {
    s.vCodec = "gif"; s.crfEn = false; s.aEn = false;
  } else if (fmt == "wav") {
    s.vEn = false; s.crfEn = false; s.aCodec = "pcm";
  } else {
    s.vCodec = (reqCodec == "gif") ? "h264" : reqCodec;
    s.crfEn = (s.vCodec == "h264");
  }
  if (s.vEn && (w <= 0 || h <= 0)) s.btnEn = false;
  if (fmt == "mp4" && s.vEn && s.vCodec != "copy" && (w % 2 != 0 || h % 2 != 0)) s.btnEn = false;
  return s;
}

int main() {
  const char* fmts[] = {"mp4", "gif", "wav"};
  const char* codecs[] = {"h264", "copy", "gif"};
  const int sizes[][2] = {{1280, 720}, {641, 360}, {0, 360}};
  int total = 0, bad = 0;
  for (const char* f : fmts)
    for (const char* c : codecs)
      for (auto& sz : sizes) {
        FormatSelect fmt; VideoPanel video; AudioPanel audio; ExportButton btn;
        fmt.video = &video; fmt.audio = &audio; fmt.btn = &btn;
        video.format = &fmt; video.btn = &btn;
        audio.format = &fmt; audio.video = &video; audio.btn = &btn;
        btn.format = &fmt; btn.video = &video; btn.audio = &audio;

        fmt.choose(f);
        video.chooseCodec(c);
        video.setSize(sz[0], sz[1]);

        Spec w = expected(f, c, sz[0], sz[1]);
        ++total;
        bool ok = (video.enabled == w.vEn && video.crfEnabled == w.crfEn &&
                   audio.enabled == w.aEn && btn.enabled == w.btnEn &&
                   (!w.vEn || video.codec == w.vCodec) && (!w.aEn || audio.codec == w.aCodec));
        if (!ok) {
          ++bad;
          std::printf("  ✗ fmt=%s codec=%s %dx%d\n", f, c, sz[0], sz[1]);
        }
      }
  std::printf("%d 种组合里 %d 种和规格不一致\n", total, bad);
}
```

```text
27 种组合里 0 种和规格不一致
```

四个控件时，测试的 27 种组合全部通过。只是构造完对象之后，还要手工把 11 条指针一根根接上，而且你已经没法把 `VideoPanel` 单独拿去别的对话框复用——它紧紧绑着 `FormatSelect` 和 `ExportButton`。

### 1.2 加一个「只导出片段」面板

新需求来了：加一个 `RangePanel`，让用户勾选「只导出 `[start, end]` 这一段」。它和已有控件有三条联动规则：

1. `end <= start` 时禁用导出按钮；
2. 选了 `gif` 格式且开启片段时，时长超过 15 秒禁用导出按钮（GIF 太长文件会爆）；
3. 开启片段时，如果视频编码是 `copy`（流拷贝），提示「流拷贝只能从关键帧切」，否则提示「精确裁剪」。

在网状写法里，新面板要认识 `FormatSelect`、`VideoPanel`、`ExportButton`，反过来 `FormatSelect`、`VideoPanel`、`ExportButton` 也都要加上 `RangePanel*` 成员并修改自己的函数（完整文件是 `mesh_dialog_range.cpp`）：

<!-- run: off -->
```diff
 struct FormatSelect {
   VideoPanel* video = nullptr;
   AudioPanel* audio = nullptr;
+  RangePanel* range = nullptr;
   ExportButton* btn = nullptr;
 };
 struct VideoPanel {
   FormatSelect* format = nullptr;
+  RangePanel* range = nullptr;
   ExportButton* btn = nullptr;
 };
+struct RangePanel {
+  bool clipOnly = false;
+  double start = 0.0, end = 10.0;
+  std::string note = "完整视频";
+  FormatSelect* format = nullptr;
+  VideoPanel* video = nullptr;
+  ExportButton* btn = nullptr;
+  void setRange(bool on, double s, double e);
+};
 struct ExportButton {
   ...
+  RangePanel* range = nullptr;
   void refresh() {
     ...
+    if (range->clipOnly && range->end <= range->start) {
+      enabled = false; hint = "结束时间必须大于开始时间"; return;
+    }
+    // 漏改 1：忘了检查 gif 片段超过 15 秒
   }
 };
```

写完 `RangePanel::setRange()` 之后，很容易犯两个真实项目里天天发生的错：

- **漏改 1**：在 `ExportButton::refresh()` 里加了 `end <= start` 检查，忘了加「GIF 片段不能超过 15 秒」；
- **漏改 2**：在 `RangePanel::setRange()` 里根据 `video->codec` 更新了 `note`，但忘了另一条路径——如果用户**先**勾选片段，**再**把视频编码切到 `copy`，走的是 `VideoPanel::chooseCodec()`，那里没有去更新 `range->note`。

把「格式 × 片段区间 × 编码 × 分辨率」共 48 种组合逐一跑一遍，输出：

```text
  ✗ fmt=mp4 range=2~8s codec=copy 1280x720：btn=1(期望1) note="精确裁剪"(期望"流拷贝只能从关键帧切")
  ✗ fmt=mp4 range=2~8s codec=copy 641x360：btn=1(期望1) note="精确裁剪"(期望"流拷贝只能从关键帧切")
  ✗ fmt=mp4 range=0~20s codec=copy 1280x720：btn=1(期望1) note="精确裁剪"(期望"流拷贝只能从关键帧切")
  ✗ fmt=mp4 range=0~20s codec=copy 641x360：btn=1(期望1) note="精确裁剪"(期望"流拷贝只能从关键帧切")
48 种组合里 10 种和规格不一致
```

### 1.3 问题出在哪

`measure.sh` 用 `diff` 统计了这次修改（只比对类与函数定义，不含 `main`）：

- **8 处改动**，新增 29 行，几乎所有已有控件都被动了；
- 5 个控件之间直接持有的邻居指针一共 **17 个**。

$n$ 个对象两两互相引用，连接数最多是 $n(n - 1) / 2$，随对象数量呈平方增长。更麻烦的是**联动逻辑没有统一的家**：

<figure class="diagram">
<svg viewBox="0 0 700 232" role="img" aria-label="左边是 5 个控件两两相连的网状结构，有 17 个邻居指针；右边是星形结构，5 个控件都只连接中间的 ExportDialog，彼此之间没有连线"><g style="font-family:var(--mono)"><text x="160.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">网状互指（多对多）</text><line x1="160" y1="54" x2="62" y2="114" style="stroke:var(--danger);stroke-width:1.3"/><line x1="160" y1="54" x2="258" y2="114" style="stroke:var(--danger);stroke-width:1.3"/><line x1="160" y1="54" x2="96" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="160" y1="54" x2="224" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="62" y1="114" x2="258" y2="114" style="stroke:var(--danger);stroke-width:1.3"/><line x1="62" y1="114" x2="96" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="62" y1="114" x2="224" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="258" y1="114" x2="96" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="258" y1="114" x2="224" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><line x1="96" y1="196" x2="224" y2="196" style="stroke:var(--danger);stroke-width:1.3"/><rect x="115" y="38" width="90" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="160.0" y="56.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Format</text><rect x="18" y="100" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="62.0" y="118.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Video</text><rect x="214" y="100" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="258.0" y="118.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Audio</text><rect x="52" y="182" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="96.0" y="200.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Range</text><rect x="180" y="182" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="224.0" y="200.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Button</text><text x="160.0" y="226.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">最多 n(n−1)/2 条连线，本例 17 个邻居指针</text><text x="530.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">星形结构（中介者）</text><line x1="530" y1="124" x2="530" y2="54" style="stroke:var(--accent);stroke-width:1.6"/><line x1="530" y1="124" x2="412" y2="114" style="stroke:var(--accent);stroke-width:1.6"/><line x1="530" y1="124" x2="648" y2="114" style="stroke:var(--accent);stroke-width:1.6"/><line x1="530" y1="124" x2="446" y2="196" style="stroke:var(--accent);stroke-width:1.6"/><line x1="530" y1="124" x2="614" y2="196" style="stroke:var(--accent);stroke-width:1.6"/><rect x="485" y="38" width="90" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="530.0" y="56.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Format</text><rect x="368" y="100" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="412.0" y="118.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Video</text><rect x="604" y="100" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="648.0" y="118.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Audio</text><rect x="402" y="182" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="446.0" y="200.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Range</text><rect x="570" y="182" width="88" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="614.0" y="200.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">Button</text><rect x="476" y="106" width="108" height="36" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.8"/><text x="530.0" y="128.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">ExportDialog</text><text x="530.0" y="226.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">n 条连线，控件间 0 个邻居指针</text></g></svg>
</figure>

- **操作顺序不同，走的代码路径不同**：先勾片段再切 `copy`，和先切 `copy` 再勾片段，进了两个完全不同的函数，必须两边都记得同步；
- **控件无法复用**：`VideoPanel` 里写死了「导出对话框里还有个 `FormatSelect` 和 `RangePanel`」，换个只需要选编码器的地方就没法直接用它；
- **改动波及面大**：加一个控件，每个可能和它联动的旧控件都要加指针、加头文件、改逻辑。

把中间那张蜘蛛网抽出来，放进一个专门的协调对象，就是中介者模式。

## 2. 中介者模式

### 2.1 意图

GoF 的定义：

> 用一个中介对象来封装一系列的对象交互。中介者使各对象不需要显式地相互引用，从而使其耦合松散，而且可以独立地改变它们之间的交互。

大白话：**一群对象不要私下互相打电话，统一跟中间的「调度员」说话。** 机场里的飞机不会每两架之间互相无线电确认「你先降还是我先降」，所有飞机只跟塔台通话，由塔台掌握全局并下达指令。

参与者：

- **Mediator（中介者接口）**：定义同事向中介者汇报变化的接口，比如 `changed(Widget* sender)`。
- **ConcreteMediator（具体中介者）**：持有各个同事对象，集中实现它们之间的协作规则（`ExportDialog`）。
- **Colleague（同事）**：只持有中介者指针，不知道其他同事的存在；自己状态变了就调用 `med->changed(this)`。

### 2.2 用中介者重构导出对话框

所有控件继承一个极轻量的基类 `Widget`，里面只有一个 `DialogMediator*` 和一句 `notify()`；四条联动规则全部搬进 `ExportDialog::changed()`：

```cpp
// 《中介者模式》：每个控件只认识 DialogMediator，联动规则集中在 ExportDialog::changed() 一处
#include <cstdio>
#include <string>

struct Widget;
struct DialogMediator {
  virtual ~DialogMediator() = default;
  virtual void changed(Widget* sender) = 0;
};

struct Widget {                               // 同事基类：只保存中介者指针
  DialogMediator* med = nullptr;
  explicit Widget(DialogMediator* m) : med(m) {}
  void notify() { if (med) med->changed(this); }
};

struct FormatSelect : Widget {                // 不认识 VideoPanel、AudioPanel、ExportButton
  std::string fmt = "mp4";
  using Widget::Widget;
  void choose(std::string f) { fmt = std::move(f); notify(); }
};

struct VideoPanel : Widget {
  bool enabled = true, crfEnabled = true;
  std::string codec = "h264";
  int width = 1280, height = 720, crf = 23;
  using Widget::Widget;
  void chooseCodec(std::string c) { if (enabled) { codec = std::move(c); notify(); } }
  void setSize(int w, int h) { width = w; height = h; notify(); }
};

struct AudioPanel : Widget {
  bool enabled = true;
  std::string codec = "aac";
  using Widget::Widget;
};

struct ExportButton : Widget {
  bool enabled = true;
  std::string hint = "就绪";
  using Widget::Widget;
};

// 具体中介者：唯一知道所有控件的地方，集中维护对话框的联动规则
struct ExportDialog : DialogMediator {
  FormatSelect format{this};
  VideoPanel video{this};
  AudioPanel audio{this};
  ExportButton btn{this};

  void changed(Widget*) override {            // 无论哪个控件变了，都按同一套规则算出一致的状态
    if (format.fmt == "mp4") {
      video.enabled = true;
      if (video.codec == "gif") video.codec = "h264";
      video.crfEnabled = (video.codec == "h264");
      audio.enabled = true;
      if (audio.codec == "pcm") audio.codec = "aac";
    } else if (format.fmt == "gif") {
      video.enabled = true;
      video.codec = "gif";
      video.crfEnabled = false;
      audio.enabled = false;
    } else if (format.fmt == "wav") {
      video.enabled = false;
      video.crfEnabled = false;
      audio.enabled = true;
      audio.codec = "pcm";
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

// ---- 以下是测试：遍历「格式 × 视频编码 × 分辨率」的每种操作序列，和规格逐项对照
struct Spec {
  bool vEn, crfEn, aEn, btnEn;
  std::string vCodec, aCodec;
};

Spec expected(const std::string& fmt, const std::string& reqCodec, int w, int h) {
  Spec s{true, true, true, true, "h264", "aac"};
  if (fmt == "gif") {
    s.vCodec = "gif"; s.crfEn = false; s.aEn = false;
  } else if (fmt == "wav") {
    s.vEn = false; s.crfEn = false; s.aCodec = "pcm";
  } else {
    s.vCodec = (reqCodec == "gif") ? "h264" : reqCodec;
    s.crfEn = (s.vCodec == "h264");
  }
  if (s.vEn && (w <= 0 || h <= 0)) s.btnEn = false;
  if (fmt == "mp4" && s.vEn && s.vCodec != "copy" && (w % 2 != 0 || h % 2 != 0)) s.btnEn = false;
  return s;
}

int main() {
  const char* fmts[] = {"mp4", "gif", "wav"};
  const char* codecs[] = {"h264", "copy", "gif"};
  const int sizes[][2] = {{1280, 720}, {641, 360}, {0, 360}};
  int total = 0, bad = 0;
  for (const char* f : fmts)
    for (const char* c : codecs)
      for (auto& sz : sizes) {
        ExportDialog dlg;
        dlg.format.choose(f);
        dlg.video.chooseCodec(c);
        dlg.video.setSize(sz[0], sz[1]);

        Spec w = expected(f, c, sz[0], sz[1]);
        ++total;
        bool ok = (dlg.video.enabled == w.vEn && dlg.video.crfEnabled == w.crfEn &&
                   dlg.audio.enabled == w.aEn && dlg.btn.enabled == w.btnEn &&
                   (!w.vEn || dlg.video.codec == w.vCodec) && (!w.aEn || dlg.audio.codec == w.aCodec));
        if (!ok) {
          ++bad;
          std::printf("  ✗ fmt=%s codec=%s %dx%d\n", f, c, sz[0], sz[1]);
        }
      }
  std::printf("%d 种组合里 %d 种和规格不一致\n", total, bad);
}
```

```text
27 种组合里 0 种和规格不一致
```

注意两点变化：

1. **控件之间零依赖**：`FormatSelect`、`VideoPanel`、`AudioPanel`、`ExportButton` 的定义里一个邻居指针都没有；哪怕放到别的对话框里，只要传另一个 `DialogMediator*` 就能用。
2. **统一重算取代零散修补**：`ExportDialog::changed()` 不按「谁触发的」去打补丁，而是每次都从当前所有控件的值把约束统一算一遍。这样「先勾片段再切 `copy`」和「先切 `copy` 再勾片段」走的是同一段代码，不可能出现一条路径忘了更新。

### 2.3 类图和时序图

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

类图上左边是中介者，右边是同事；**同事之间没有任何连线**，所有箭头都在 `ExportDialog` 汇聚。

用户把格式切到 `gif` 时，调用顺序是这样的：

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

### 2.4 演示

下面的演示展示了四个场景的星形协调过程，重点看「先选片段再切 copy」和「GIF 选 20 秒片段」两步：

<div class="dsviz" data-viz="dp-mediator"></div>

### 2.5 再加「只导出片段」面板

给中介者版加上同样的 `RangePanel`（完整文件是 `mediator_dialog_range.cpp`）：四个已有控件**一行不改**，只需新增 `RangePanel`，并在 `ExportDialog` 里加成员和对应规则：

<!-- run: off -->
```diff
+struct RangePanel : Widget {                  // 新增同事：也只认识 DialogMediator
+  bool clipOnly = false;
+  double start = 0.0, end = 10.0;
+  std::string note = "完整视频";
+  using Widget::Widget;
+  void setRange(bool on, double s, double e) {
+    clipOnly = on; start = s; end = e;
+    notify();
+  }
+};
 struct ExportDialog : DialogMediator {
   FormatSelect format{this};
   VideoPanel video{this};
   AudioPanel audio{this};
+  RangePanel range{this};
   ExportButton btn{this};
   void changed(Widget*) override {
     ...
+    if (!range.clipOnly) { range.note = "完整视频"; }
+    else if (video.enabled && video.codec == "copy") { range.note = "流拷贝只能从关键帧切"; }
+    else { range.note = "精确裁剪"; }
     ...
+    } else if (range.clipOnly && range.end <= range.start) {
+      btn.enabled = false; btn.hint = "结束时间必须大于开始时间";
+    } else if (format.fmt == "gif" && range.clipOnly && (range.end - range.start) > 15.0) {
+      btn.enabled = false; btn.hint = "GIF 片段不能超过 15 秒";
```

用 `measure.sh` 对比两种写法加 `RangePanel` 的数据：

<figure class="diagram">
<svg viewBox="0 0 680 144" role="img" aria-label="加 RangePanel 的改动对比：网状互指改了 8 处、4 个旧控件全动了、控件间有 17 个邻居指针；中介者改了 4 处、0 个旧控件被改、0 个邻居指针"><g style="font-family:var(--mono)"><text x="108.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">改动处数（diff hunk）</text><rect x="108.0" y="30.0" width="128.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="242.0" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">8 处（4 个旧控件全改）</text><rect x="108.0" y="60.0" width="64.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="178.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">4 处（0 个旧控件改动）</text><text x="418.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">控件间直接持有的邻居指针</text><rect x="418.0" y="30.0" width="170.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="594.0" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">17 个</text><rect x="418.0" y="60.0" width="2.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="426.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 个</text><text x="100.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">网状互指</text><text x="100.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">中介者</text><text x="10.0" y="114.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">48 种组合逐项对照规格：网状版 10 种不一致，中介者版 0 种不一致</text><text x="10.0" y="132.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：diff 只比较类与函数定义（不含 main 里的测试）</text></g></svg>
</figure>

中介者并没有让联动规则消失——规则依然存在，只是从四个控件里全部收拢到了 `ExportDialog::changed()` 这一处，读代码、加规则、查 bug 都只需要看这一个地方。

## 3. 深入中介者：重入、生命周期与上帝类

中介者结构简单，真正写进项目里时，有三个最常踩的坑。

### 3.1 乒乓重入：滑块和输入框来回通知

假设时间栏里有两个联动控件：一个按**帧号**拖动的滑块 `Slider`（24 fps，整数帧 0～240），一个显示**秒数**的输入框 `SpinBox`（保留两位小数）。

- 拖滑块到第 `1` 帧 → 中介者把输入框设成 `1 / 24.0 = 0.041666…`，四舍五入显示 `0.04`；
- 输入框的值变了，调用 `med->onChanged(this)` → 中介者反算回帧号 `int(0.04 * 24) = 0`，去改滑块；
- 滑块从 `1` 变成 `0`，又调用 `med->onChanged(this)` → 输入框变成 `0.00` → 滑块变成 `0` → 无限来回乒乓。

注意：即使没有舍入误差，只要控件的 `setXxx()` 不做「值没变就不通知」判断（或者有浮点/整数转换误差），就会在两个控件之间无限递归：

```cpp
// 《中介者模式》：中介者更新同事时，同事又通知中介者——用 command line args 切换：默认带重入保护，传 nbl 复现无限递归
#include <cmath>
#include <cstdio>
#include <cstring>

struct Slider;
struct SpinBox;
struct TimebarMediator {
  virtual ~TimebarMediator() = default;
  virtual void onChanged(void* sender) = 0;
};

struct Slider {                       // 进度条滑块：整数帧号 [0, 240]
  TimebarMediator* med = nullptr;
  int frame = 0;
  int updates = 0;
  void setFrame(int f) {
    frame = f;
    ++updates;
    if (med) med->onChanged(this);    // 只要被调用就通知中介者（Qt 的 setValue 遇到微小差值时就是这样）
  }
};

struct SpinBox {                      // 时间输入框：秒数（保留 2 位小数，写回时有舍入误差）
  TimebarMediator* med = nullptr;
  double sec = 0.0;
  int updates = 0;
  void setSec(double s) {
    sec = std::round(s * 100.0) / 100.0;  // 输入框只保留两位小数：1 / 24.0 = 0.041666... → 0.04
    ++updates;
    if (med) med->onChanged(this);
  }
};

// 写法 1：没有重入保护。滑块改到第 1 帧 → 输入框算得 0.04 秒 → 反算回帧号 int(0.04 * 24) = 0 ≠ 1 → 滑块又改回 0……
struct NaiveBar : TimebarMediator {
  Slider slider{this};
  SpinBox spin{this};
  int depth = 0, maxDepth = 0;
  bool limit;
  explicit NaiveBar(bool lim) : limit(lim) {}

  void onChanged(void* sender) override {
    if (limit && depth >= 6) return;  // 演示模式：到 6 层停下，看它是怎么来回乒乓的
    ++depth;
    if (depth > maxDepth) maxDepth = depth;
    if (sender == &slider) {
      spin.setSec(slider.frame / 24.0);
    } else if (sender == &spin) {
      slider.setFrame(static_cast<int>(spin.sec * 24.0));
    }
    --depth;
  }
};

// 写法 2：中介者用 syncing_ 守卫（类似 Qt 的 QSignalBlocker）——正在同步引起的 set 不再二次触发联动
struct SafeBar : TimebarMediator {
  Slider slider{this};
  SpinBox spin{this};
  bool syncing_ = false;

  void onChanged(void* sender) override {
    if (syncing_) return;
    syncing_ = true;
    if (sender == &slider) {
      spin.setSec(slider.frame / 24.0);
    } else if (sender == &spin) {
      slider.setFrame(static_cast<int>(spin.sec * 24.0));
    }
    syncing_ = false;
  }
};

int main(int argc, char** argv) {
  bool crash = (argc > 1 && std::strcmp(argv[1], "nbl") == 0);
  if (crash) {
    NaiveBar b(false);
    b.slider.setFrame(1);             // 无限递归直到栈溢出
    return 0;
  }
  NaiveBar b1(true);
  b1.slider.setFrame(1);
  std::printf("无重入保护（拦在第 6 层）：拖到第 1 帧后，slider.frame=%d spin.sec=%.2f，"
              "slider 被改 %d 次、spin 被改 %d 次\n",
              b1.slider.frame, b1.spin.sec, b1.slider.updates, b1.spin.updates);

  SafeBar b2;
  b2.slider.setFrame(1);
  std::printf("带 syncing_ 保护        ：拖到第 1 帧后，slider.frame=%d spin.sec=%.2f，"
              "slider 被改 %d 次、spin 被改 %d 次\n",
              b2.slider.frame, b2.spin.sec, b2.slider.updates, b2.spin.updates);
}
```

```text
无重入保护（拦在第 6 层）：拖到第 1 帧后，slider.frame=0 spin.sec=0.00，slider 被改 4 次、spin 被改 3 次
带 syncing_ 保护        ：拖到第 1 帧后，slider.frame=1 spin.sec=0.04，slider 被改 1 次、spin 被改 1 次
```

不仅被来回改了好几次，还把用户刚拖到的第 `1` 帧悄悄改回了第 `0` 帧。如果把 `depth >= 6` 的演示上限去掉（`./pingpong nbl`），用 `-O0` 编译运行，直接栈溢出崩溃：

```text
== pingpong nbl：无重入保护时来回触发直到栈溢出
   -O0 退出码 139（139 = 段错误/栈溢出）
```

解决办法是**区分「外部发起的修改」和「中介者同步引起的修改」**：

- 最简单是在中介者里放一个 `bool syncing_` 守卫（如果函数里可能提前 `return` 或抛异常，用 RAII 包装成 `ScopeGuard` 自动复位）；
- Qt 专门为此提供了 `QSignalBlocker blocker(spinBox)`，在作用域内屏蔽该控件发出的信号；
- 第 2 节的 `ExportDialog` 之所以没触发重入，是因为 `ExportDialog` 直接修改控件的数据成员（或者给控件提供「不发通知的内部 setter」），而不经过会调 `notify()` 的公有方法。

### 3.2 同事析构了，中介者还留着它的指针

`ExportDialog` 的四个控件都是它的值成员，生命周期和中介者完全绑定，所以很安全。但在聊天室、会议室、动态插拔面板这类场景里，同事是**动态加入和离开**的，中介者通常存一个 `std::vector<Peer*>`。如果某个同事在局部作用域里析构了，却忘了从队列里注销：

<!-- run: off -->
```cpp
// 《中介者模式》：中介者持有同事的裸指针，同事在局部作用域析构时没注销，下次广播时发生 heap-use-after-free
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Peer;
struct RoomMediator {
  std::vector<Peer*> peers;
  void join(Peer* p) { peers.push_back(p); }
  void broadcast(const Peer* from, const std::string& msg);
};

struct Peer {
  std::string name;
  RoomMediator& room;
  Peer(std::string n, RoomMediator& r) : name(std::move(n)), room(r) {
    room.join(this);                  // 构造时注册，但析构函数忘了从 room 注销
  }
  void say(const std::string& msg) const { room.broadcast(this, msg); }
  void onMessage(const std::string& from, const std::string& msg) const {
    std::printf("  [%s 收到 %s] %s\n", name.c_str(), from.c_str(), msg.c_str());
  }
};

void RoomMediator::broadcast(const Peer* from, const std::string& msg) {
  for (Peer* p : peers)
    if (p != from) p->onMessage(from->name, msg);
}

int main() {
  RoomMediator room;
  auto decoder = std::make_unique<Peer>("解码器", room);
  {
    auto osd = std::make_unique<Peer>("OSD浮层", room);
    decoder->say("第 1 帧就绪");
  }                                   // osd 已销毁，但 room.peers[1] 还是指向它的悬空指针
  decoder->say("第 2 帧就绪");        // 读写已释放的 Peer::name
}
```

用 `-fsanitize=address` 编译运行（`measure.sh`）：

```text
== dangling_colleague (ASan)
==27333==ERROR: AddressSanitizer: heap-use-after-free on address 0x603000000fe7
READ of size 1 at 0x603000000fe7 thread T0
    #4 in Peer::onMessage(...) dangling_colleague.cpp:22
    #5 in RoomMediator::broadcast(...) dangling_colleague.cpp:28
    #6 in Peer::say(...) dangling_colleague.cpp:20
    #7 in main dangling_colleague.cpp:38
```

这和《观察者模式》里的悬空观察者是同一个根源，三种治法也相通：

1. **优先让中介者直接拥有同事**（像 `ExportDialog` 那样把控件做成值成员或 `unique_ptr`），从结构上消灭生命周期错位；
2. **动态加入的同事，在 `~Peer()` 里自动注销**，并且禁止拷贝和移动（或者在移动时更新中介者里的指针），防止临时副本析构把原对象也注销掉；
3. 如果同事和中介者的销毁顺序不确定，用 `std::weak_ptr` 存同事，广播时 `lock()`，或者发回一个 RAII 句柄（参考《观察者模式》的 `Connection`）。

### 3.3 中介者变胖：防止长成上帝类

中介者最大的风险，GoF 原书自己就警告过：**它把网状的复杂度从同事身上移走了，但并没有消灭复杂度——复杂度全集中到了中介者身上。**

随着对话框越加越多（滤镜、水印、字幕烧录、硬件编码、上传参数……），`ExportDialog` 很容易从 60 行膨胀成 2000 行的**上帝类**，最后没人敢动。防止它失控有三条实用的边界：

1. **把纯逻辑抽成无状态函数**：像「检查参数合不合法」「根据参数拼 FFmpeg 命令行」不需要认识任何 UI 控件，只收一个普通结构体 `ExportConfig` 返回结果。中介者只负责「从控件读值 → 调纯函数 → 把结果写回控件」；
2. **按页签或子区域拆成多层中介者**：视频页签有一个 `VideoTabMediator`，音频页签有一个 `AudioTabMediator`，顶层 `ExportDialog` 只协调页签之间的少量联动，不插手页签内部细节；
3. **只放「跨同事的规则」**：只属于单个控件自己的校验（比如 CRF 必须在 0～51 之间）留在控件自己里面，不要什么都往中介者塞。

### 3.4 真实项目里的中介者

- **Qt 的 `QDialog` / `QMainWindow`**：这就是教科书级的中介者。按钮、下拉框、输入框（`QPushButton`、`QComboBox`、`QSpinBox`）彼此完全不认识，只对外发信号；对话框在构造函数里用 `connect` 把它们的信号都接到自己的槽函数，在对话框类里决定「选了 A 就禁用 B、更新 C」。
- **音视频会议与聊天室服务端**：客户端之间不直接建连接（否则 100 个人要建 4950 条连接），所有人都只连房间服务器 `Room`，由 `Room` 转发消息、协调谁在发言、谁在共享屏幕。
- **空中交通管制（ATC）**：每架飞机只和塔台通信，塔台掌握所有飞机的高度、航线和跑道占用，决定起降顺序。

## 4. 现代 C++ 写法与开销实测

### 4.1 去掉基类：用 `std::function` 回调连接

经典 GoF 写法要求每个控件都继承 `Widget` 并认识 `DialogMediator`。如果你用的是第三方 UI 控件，或者希望控件完全不知道「中介者」这回事，可以去掉 `Widget` 基类和虚函数接口：控件只留一个 `std::function<void()> onChange` 回调槽，中介者在构造函数里用 lambda 把它们接起来：

```cpp
// 《中介者模式》：现代 C++ 写法——控件不继承基类，只暴露 std::function 回调；中介者拥有控件或在装配处用 lambda 连接
#include <cstdio>
#include <functional>
#include <string>

struct FormatSelect {                 // 纯独立组件：不知道任何中介者基类，只提供一个 onChange 回调槽
  std::string fmt = "mp4";
  std::function<void()> onChange;
  void choose(std::string f) {
    fmt = std::move(f);
    if (onChange) onChange();
  }
};

struct VideoPanel {
  bool enabled = true, crfEnabled = true;
  std::string codec = "h264";
  int width = 1280, height = 720, crf = 23;
  std::function<void()> onChange;
  void chooseCodec(std::string c) {
    if (!enabled) return;
    codec = std::move(c);
    if (onChange) onChange();
  }
  void setSize(int w, int h) {
    width = w; height = h;
    if (onChange) onChange();
  }
};

struct AudioPanel {
  bool enabled = true;
  std::string codec = "aac";
};

struct ExportButton {
  bool enabled = true;
  std::string hint = "就绪";
};

struct ExportDialog {                 // 中介者：用组合直接拥有 4 个控件，构造函数里把回调绑到 sync()
  FormatSelect format;
  VideoPanel video;
  AudioPanel audio;
  ExportButton btn;

  ExportDialog() {
    format.onChange = [this] { sync(); };
    video.onChange  = [this] { sync(); };
  }
  ExportDialog(const ExportDialog&) = delete;
  ExportDialog& operator=(const ExportDialog&) = delete;

  void sync() {
    if (format.fmt == "mp4") {
      video.enabled = true;
      if (video.codec == "gif") video.codec = "h264";
      video.crfEnabled = (video.codec == "h264");
      audio.enabled = true;
      if (audio.codec == "pcm") audio.codec = "aac";
    } else if (format.fmt == "gif") {
      video.enabled = true;
      video.codec = "gif";
      video.crfEnabled = false;
      audio.enabled = false;
    } else if (format.fmt == "wav") {
      video.enabled = false;
      video.crfEnabled = false;
      audio.enabled = true;
      audio.codec = "pcm";
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
    std::printf("%-18s → fmt=%-3s video=%s(%s,crf=%s) audio=%s(%s) btn=%s(%s)\n",
                step, format.fmt.c_str(),
                video.enabled ? "开" : "关", video.codec.c_str(), video.crfEnabled ? "开" : "关",
                audio.enabled ? "开" : "关", audio.codec.c_str(),
                btn.enabled ? "可点" : "禁用", btn.hint.c_str());
  }
};

int main() {
  ExportDialog dlg;
  dlg.dump("1. 初始状态");
  dlg.format.choose("gif");
  dlg.dump("2. 格式切到 gif");
  dlg.format.choose("mp4");
  dlg.dump("3. 格式切回 mp4");
  dlg.video.chooseCodec("copy");
  dlg.dump("4. 视频选 copy");
  dlg.video.setSize(641, 360);
  dlg.dump("5. 改奇数宽(copy)");
  dlg.video.chooseCodec("h264");
  dlg.dump("6. 切回 h264");
}
```

```text
1. 初始状态    → fmt=mp4 video=开(h264,crf=开) audio=开(aac) btn=可点(就绪)
2. 格式切到 gif → fmt=gif video=开(gif,crf=关) audio=关(aac) btn=可点(就绪)
3. 格式切回 mp4 → fmt=mp4 video=开(h264,crf=开) audio=开(aac) btn=可点(就绪)
4. 视频选 copy  → fmt=mp4 video=开(copy,crf=关) audio=开(aac) btn=可点(就绪)
5. 改奇数宽(copy) → fmt=mp4 video=开(copy,crf=关) audio=开(aac) btn=可点(就绪)
6. 切回 h264     → fmt=mp4 video=开(h264,crf=开) audio=开(aac) btn=禁用(h264 编码要求宽高都是偶数)
```

有一个必须注意的细节：**`ExportDialog` 删除了拷贝构造和拷贝赋值**（移动也会隐式删除）。因为 `format.onChange` 里的 lambda 捕获了 `this`；如果不禁拷贝，把 `dlg` 拷贝或移动到新地址后，新对象的 `format.onChange` 里抓着的还是**旧对象**的 `this`，旧对象一析构就悬空了。

### 4.2 开销与对象大小实测

比较四种写法执行 419 万次控件联动的单次耗时和单个同事对象的大小（Apple clang 21，`-O2`，7 轮取中位数）：

<figure class="diagram">
<svg viewBox="0 0 680 184" role="img" aria-label="四种中介者写法的耗时与同事对象大小：网状直接调用 1.81 ns（32 字节），虚函数中介者 Widget* 0.90 ns（16 字节），虚函数中介者枚举事件 0.74 ns（16 字节），std::function 回调 0.78 ns（40 字节）"><g style="font-family:var(--mono)"><text x="188.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">每次联动耗时（Clang -O2）</text><rect x="188.0" y="30.0" width="162.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="356.9" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">1.81 ns</text><rect x="188.0" y="60.0" width="81.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="275.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.90 ns</text><rect x="188.0" y="90.0" width="66.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="260.6" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.74 ns</text><rect x="188.0" y="120.0" width="70.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="264.2" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.78 ns</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">单个同事 sizeof</text><rect x="458.0" y="30.0" width="96.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="560.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">32 B（3 个邻居指针）</text><rect x="458.0" y="60.0" width="48.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="512.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">16 B（1 个中介指针）</text><rect x="458.0" y="90.0" width="48.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="512.0" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">16 B（1 个中介指针）</text><rect x="458.0" y="120.0" width="120.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="584.0" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">40 B（含 std::function）</text><text x="180.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">网状直接调用</text><text x="180.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数中介者 (Widget*)</text><text x="180.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数中介者 (枚举事件)</text><text x="180.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::function 回调</text><text x="10.0" y="174.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp：Apple clang 21，-O2，4194304 次联动，7 轮取中位数</text></g></svg>
</figure>

| 写法 | 每次联动（ns） | 单个同事 `sizeof` | 特点 |
|---|---|---|---|
| 网状直接调用 | 1.81 | 32 字节（随邻居数增长） | 控件彼此解引用多份指针；对象越多指针越多 |
| 虚函数中介者（`Widget*`） | 0.90 | 16 字节 | 中介者用值成员把四个同事紧挨着放在一块连续内存里，缓存局部性更好 |
| 虚函数中介者（枚举事件） | 0.74 | 16 字节 | 不用比较 `sender == &a`，意图更直观 |
| `std::function` 回调 | 0.78 | 40 字节 | 零侵入，不需基类；lambda 只捕获一个 `this` 指针，走小对象优化（SBO）零堆分配 |

有趣的是，在这个测例里三种中介者反而比网状版略快，一方面是网状版要多追几次邻居指针、对象也更大，另一方面中介者把同事作为连续成员放在同一个结构体里，访问全命中同一条缓存行。当然，所有写法都在 **1～2 纳秒**以内，对于界面操作或房间消息分发来说完全可以忽略；选写法看的是耦合和可维护性。

<details>
<summary>展开看 bench.cpp 源码</summary>

<!-- run: off -->
```cpp
// 《中介者模式》：实测网状直接调用、虚函数中介者、枚举事件中介者、std::function 回调中介者的协调开销和同事对象大小
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <vector>

// ---- 1. 网状直接调用：每个控件持有另外 3 个控件的指针
struct MB; struct MC; struct MD;
struct MA { int v = 0; MB* b; MC* c; MD* d; __attribute__((noinline)) void fire(int x); };
struct MB { int v = 0; MA* a; MC* c; MD* d; };
struct MC { int v = 0; MA* a; MB* b; MD* d; };
struct MD { int v = 0; MA* a; MB* b; MC* c; void check() { v = (a->v + b->v + c->v) & 1; } };
__attribute__((noinline)) void MA::fire(int x) {
  v = x;
  b->v = x + 1;
  c->v = (x & 1) ? 2 : 3;
  d->check();
}

// ---- 2. 经典虚函数中介者：同事只持有 1 个 Mediator*，通过 sender 指针判断是谁
struct VCol;
struct VMed {
  virtual ~VMed() = default;
  virtual void changed(VCol* s) = 0;
};
struct VCol {
  VMed* m;
  int v = 0;
  explicit VCol(VMed* m) : m(m) {}
  __attribute__((noinline)) void fire(int x) { v = x; m->changed(this); }
};
struct VDialog : VMed {
  VCol a{this}, b{this}, c{this}, d{this};
  void changed(VCol* s) override {
    if (s == &a) {
      b.v = a.v + 1;
      c.v = (a.v & 1) ? 2 : 3;
    }
    d.v = (a.v + b.v + c.v) & 1;
  }
};

// ---- 3. 枚举事件中介者：用事件 ID 代替 Widget*，省掉指针比较或 dynamic_cast
enum class Ev { AChanged, BChanged };
struct EMed {
  virtual ~EMed() = default;
  virtual void onEvent(Ev e) = 0;
};
struct ECol {
  EMed* m;
  int v = 0;
  explicit ECol(EMed* m) : m(m) {}
  __attribute__((noinline)) void fire(int x) { v = x; m->onEvent(Ev::AChanged); }
};
struct EDialog : EMed {
  ECol a{this}, b{this}, c{this}, d{this};
  void onEvent(Ev e) override {
    if (e == Ev::AChanged) {
      b.v = a.v + 1;
      c.v = (a.v & 1) ? 2 : 3;
    }
    d.v = (a.v + b.v + c.v) & 1;
  }
};

// ---- 4. std::function 回调中介者：同事不认识中介者接口
struct FCol {
  int v = 0;
  std::function<void()> onChange;
  __attribute__((noinline)) void fire(int x) { v = x; onChange(); }
};
struct FDialog {
  FCol a, b, c, d;
  FDialog() {
    a.onChange = [this] {
      b.v = a.v + 1;
      c.v = (a.v & 1) ? 2 : 3;
      d.v = (a.v + b.v + c.v) & 1;
    };
  }
};

using Clock = std::chrono::steady_clock;
template <class F>
double median7(F fn, int n, long& sink) {
  std::vector<double> t;
  for (int r = 0; r < 7; ++r) {
    auto t0 = Clock::now();
    sink += fn();
    t.push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / n);
  }
  std::sort(t.begin(), t.end());
  return t[3];
}

int main() {
  const int N = 1 << 22;
  long sink = 0;

  MA a; MB b; MC c; MD d;
  a.b = &b; a.c = &c; a.d = &d;
  b.a = &a; b.c = &c; b.d = &d;
  c.a = &a; c.b = &b; c.d = &d;
  d.a = &a; d.b = &b; d.c = &c;

  VDialog vd;
  EDialog ed;
  FDialog fd;

  double t1 = median7([&] { long s = 0; for (int i = 0; i < N; ++i) { a.fire(i); s += d.v; } return s; }, N, sink);
  double t2 = median7([&] { long s = 0; for (int i = 0; i < N; ++i) { vd.a.fire(i); s += vd.d.v; } return s; }, N, sink);
  double t3 = median7([&] { long s = 0; for (int i = 0; i < N; ++i) { ed.a.fire(i); s += ed.d.v; } return s; }, N, sink);
  double t4 = median7([&] { long s = 0; for (int i = 0; i < N; ++i) { fd.a.fire(i); s += fd.d.v; } return s; }, N, sink);

  std::printf("%d 次控件联动，7 轮取中位数，每次联动平均耗时：\n", N);
  std::printf("  %5.2f ns  网状直接调用         （单个同事 sizeof = %2zu，存 3 个邻居指针）\n", t1, sizeof(MA));
  std::printf("  %5.2f ns  虚函数中介者(Widget*)（单个同事 sizeof = %2zu，只存 1 个中介者指针）\n", t2, sizeof(VCol));
  std::printf("  %5.2f ns  虚函数中介者(枚举事件)（单个同事 sizeof = %2zu，只存 1 个中介者指针）\n", t3, sizeof(ECol));
  std::printf("  %5.2f ns  std::function 回调   （单个同事 sizeof = %2zu，含一个 std::function）\n", t4, sizeof(FCol));
  std::printf("（校验和 %ld）\n", sink);
}
```

</details>

### 4.3 怎么选

| 写法 | 同事需要继承基类？ | 适合场景 |
|---|---|---|
| 经典虚函数中介者（`Widget*`） | 需要 | 同事本来就有公共基类（如自研 UI 控件体系），并且可能接入不同的中介者 |
| 枚举事件中介者 | 需要 | 同事少、事件种类清晰，不想靠指针地址比较区分来源 |
| `std::function` / 信号槽中介者 | **不需要** | 现代 C++ 首选：控件完全独立，可复用、易单测；记得禁止中介者拷贝或用安全连接管理生命周期 |

## 5. 中介者和相似的模式

| 模式 | 拓扑与通信方向 | 核心区别 |
|---|---|---|
| 观察者 | 一对多，单向（Subject → 观察者） | Subject 只广播「我变了」，**没有协调逻辑**，不关心观察者之间怎么配合；中介者是有主见的调度中心，知道每个同事并主动指挥它们 |
| 发布-订阅（事件总线） | 多对多，单向（发布者 → 总线 → 订阅者） | 总线只按事件类型做无脑转发，业务逻辑分散在各个订阅者里；中介者把协作规则**集中**在自己身上 |
| 外观 | 单向（外部调用方 → 外观 → 子系统） | 外观给**外部**提供一个简单入口，子系统不知道外观的存在；中介者协调**内部**同事之间的双向交互，同事会主动通知中介者 |
| 责任链 | 链式单向（A → B → C） | 请求沿着一条线找「谁来处理」；中介者是一张星形网，由中心同时协调多个对象 |

常把中介者和观察者**混在一起用**：控件用观察者（信号槽、`std::function`）向外报告变化，对话框本身充当中介者，把所有信号接到自己身上集中调度。这也是 Qt 和现代前端框架里最常见的形态。

## 6. 小结

1. **中介者模式**把一组对象之间多对多的网状交互收进一个中介对象，变成一对多的星形结构：同事只认识中介者，彼此之间不直接持有指针。
2. 实测加一个 `RangePanel`：网状版改了 **8 处**（4 个旧控件全动），控件间邻居指针 **17 个**，且容易漏掉反向路径（48 种组合里 10 种出错）；中介者版改了 **4 处**（0 个旧控件改动），邻居指针 **0 个**，48 种组合全对。
3. **乒乓重入**：中介者更新同事 B 时，B 又通知中介者去更新 A，遇到舍入误差就会无限递归直到栈溢出（`-O0` 退出码 139）。用 `syncing_` 守卫、`QSignalBlocker` 或不发通知的内部赋值拦住。
4. **生命周期**：动态加入的同事如果析构时忘了从介质注销，下次协调就会触发 `heap-use-after-free`。优先让中介者直接拥有同事，否则在析构里自动注销或用 `weak_ptr`。
5. **警惕上帝类**：把纯校验 / 计算逻辑抽成无状态函数，复杂界面按区域拆成子中介者，中介者只放跨同事的协作规则。
6. 现代 C++ 里，用 `std::function`（或信号槽）让同事零侵入地接入中介者最干净，联动耗时仅 0.78 ns，但要注意禁止中介者拷贝以免 lambda 里捕获的 `this` 悬空。

## 7. 练习

1. **辨析**：下面四种情况，哪些适合用中介者模式？不适合的应该用什么？
   - (a) 一个音频播放器，播放进度每变一次，进度条、歌词、字幕、统计上报都要各自更新，彼此之间没有先后依赖；
   - (b) 一个账号注册表单，有「手机号 / 邮箱切换」「国家区号」「验证码按钮」「密码强度条」「同意协议勾选框」「提交按钮」，互相决定启用状态和校验规则；
   - (c) 把 FFmpeg 的解封装、解码、缩放、PNG 编码 26 个函数包成一个 `Thumbnailer::save(sec, path)` 给业务代码调用；
   - (d) 多人在线白板房间，A 锁定画布时要禁止 B、C 编辑，有人举手发言时要把他的画面切到主窗口并暂停其他人的屏幕共享。
2. **动手**：给第 2.2 节的 `ExportDialog` 加一个 `PresetSelect`（预设下拉框），支持 `"custom"`（自定义）、`"moments"`（朋友圈小视频：mp4、720×1280、h264、crf=25、aac）、`"audio_only"`（仅提取音频：wav）。要求：选预设时自动填好各控件参数；用户手动改了任何参数（例如改 CRF）后，预设自动跳回 `"custom"`。想一想：这里能直接忽略 `changed(Widget* sender)` 的 `sender` 参数吗？为什么？
3. **设计**：设计一个在线会议室中介者 `MeetingRoom`，协调 5 个同事：`Camera`（用户开关、实际推流开关、帧率）、`ScreenShare`（是否共享中）、`Microphone`（静音开关、按住空格发言 PTT）、`Recorder`（录制开关、主画面来源、顶部提示横幅）、`NetMonitor`（是否低带宽）。规则如下：
   - 低带宽且正在共享屏幕时，自动暂停摄像头保屏幕流，否则按用户开关；
   - 屏幕共享或低带宽时，开着的摄像头降到 15 fps，否则 30 fps（关着则是 0 fps）；
   - 录制主画面优先取「屏幕共享」，其次「摄像头」，都没有则是「仅音频」；
   - 录制开启时横幅显示当前是否收音（`!muted || pttDown`）。
4. **找问题**：下面是一段多轨道音频混音器的「独奏（Solo）」联动代码：任意时刻最多一条轨道独奏，开启某条轨道独奏时，其余轨道自动取消独奏并静音。可是运行后有两个严重问题：① `voice.setSolo(true)` 之后，`背景音乐` 并没有静音，独奏也变成了「无」；② 用 ASan 编译运行会直接报 `heap-use-after-free`。找出这两个 bug 并修复。

   <!-- run: off -->
   ```cpp
   // 《中介者模式》练习 4（有 bug）：两个问题——① 中介者改同事时重入，把刚设好的状态冲掉；② 临时同事析构后留下悬空指针
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   struct Track;
   struct MixerMediator {
     std::vector<Track*> tracks;
     Track* soloTrack = nullptr;         // 当前独奏的轨道（最多一个）

     void add(Track* t) { tracks.push_back(t); }
     void onSoloChanged(Track* sender);
     void renderFrame();
   };

   struct Track {
     std::string name;
     MixerMediator& mixer;
     bool solo = false;
     bool muted = false;

     Track(std::string n, MixerMediator& m) : name(std::move(n)), mixer(m) {
       mixer.add(this);                  // Bug ②：构造时注册进 mixer.tracks，析构时没有移除
     }
     void setSolo(bool s) {
       solo = s;
       mixer.onSoloChanged(this);        // Bug ①：中介者把别的轨道设成 setSolo(false) 时，又会进来通知中介者
     }
   };

   void MixerMediator::onSoloChanged(Track* sender) {
     if (sender->solo) {
       soloTrack = sender;
       for (Track* t : tracks) {
         if (t != sender) {
           t->setSolo(false);            // 这里调 setSolo(false) 会再次进入 onSoloChanged，进到 else 分支把所有 muted 清掉！
           t->muted = true;
         } else {
           t->muted = false;
         }
       }
     } else {
       soloTrack = nullptr;
       for (Track* t : tracks) t->muted = false;
     }
   }

   void MixerMediator::renderFrame() {
     std::printf("当前混音输出：");
     for (Track* t : tracks)
       if (!t->muted) std::printf("[%s] ", t->name.c_str());
     std::printf("(独奏=%s)\n", soloTrack ? soloTrack->name.c_str() : "无");
   }

   int main() {
     MixerMediator mixer;
     Track bgm("背景音乐", mixer);
     Track voice("人声旁白", mixer);
     {
       auto preview = std::make_unique<Track>("试听音效", mixer);
       voice.setSolo(true);              // 期望：只有「人声旁白」发声，其余静音
       mixer.renderFrame();
     }                                   // preview 析构了，但 mixer.tracks[2] 还是悬空指针
     mixer.renderFrame();                // 读已释放的 preview->muted 和 preview->name
   }
   ```

<details>
<summary>点击查看答案</summary>

1. - (a) **不适合中介者，用《观察者模式》**。一处变化通知多处，观察者彼此独立、没有互相制约的规则，播放器不需要知道具体有哪些观察者。
   - (b) **适合中介者**。6 个表单控件互相影响启用状态和校验规则，用表单类做中介者集中管理最清晰。
   - (c) **不适合中介者，用《外观模式》**。调用方向是单向的「外部 → 外观 → FFmpeg 子系统」，子系统之间不需要通过外观来双向通信。
   - (d) **适合中介者**。白板房间协调多个用户的互斥权限和画面切换，典型的星形调度中心。

2. 这里**不能**忽略 `sender` 参数：因为「用户选了预设」和「用户手动改了 CRF / 格式」要走不同分支——前者按预设覆盖各控件的值，后者把预设退回 `"custom"`。之后的格式联动和按钮检查依然统一跑一遍（完整代码在 `ex2_preset.cpp`）：

   ```cpp
   // 《中介者模式》练习 2：给 ExportDialog 加一个 PresetSelect（「自定义 / 朋友圈小视频 / 仅提取音频」）
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

   struct PresetSelect : Widget {                // 新增控件：选预设一键改好其它控件，手动改参数时自动退回「自定义」
     std::string preset = "custom";              // custom / moments / audio_only
     using Widget::Widget;
     void choose(std::string p) { preset = std::move(p); notify(); }
   };

   struct FormatSelect : Widget {
     std::string fmt = "mp4";
     using Widget::Widget;
     void choose(std::string f) { fmt = std::move(f); notify(); }
   };

   struct VideoPanel : Widget {
     bool enabled = true, crfEnabled = true;
     std::string codec = "h264";
     int width = 1280, height = 720, crf = 23;
     using Widget::Widget;
     void chooseCodec(std::string c) { if (enabled) { codec = std::move(c); notify(); } }
     void setSize(int w, int h) { width = w; height = h; notify(); }
     void setCrf(int q) { crf = q; notify(); }
   };

   struct AudioPanel : Widget {
     bool enabled = true;
     std::string codec = "aac";
     using Widget::Widget;
   };

   struct ExportButton : Widget {
     bool enabled = true;
     std::string hint = "就绪";
     using Widget::Widget;
   };

   struct ExportDialog : DialogMediator {
     PresetSelect preset{this};
     FormatSelect format{this};
     VideoPanel video{this};
     AudioPanel audio{this};
     ExportButton btn{this};

     void changed(Widget* sender) override {
       if (sender == &preset) {                  // 用户选了预设：由中介者填入一整套参数
         if (preset.preset == "moments") {
           format.fmt = "mp4";
           video.codec = "h264"; video.width = 720; video.height = 1280; video.crf = 25;
           audio.codec = "aac";
         } else if (preset.preset == "audio_only") {
           format.fmt = "wav";
         }
       } else {                                  // 用户手动改了任何参数：预设退回「自定义」
         preset.preset = "custom";
       }

       if (format.fmt == "mp4") {
         video.enabled = true;
         if (video.codec == "gif") video.codec = "h264";
         video.crfEnabled = (video.codec == "h264");
         audio.enabled = true;
         if (audio.codec == "pcm") audio.codec = "aac";
       } else if (format.fmt == "gif") {
         video.enabled = true;
         video.codec = "gif";
         video.crfEnabled = false;
         audio.enabled = false;
       } else if (format.fmt == "wav") {
         video.enabled = false;
         video.crfEnabled = false;
         audio.enabled = true;
         audio.codec = "pcm";
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

     void dump(const char* tag) const {
       std::printf("%-16s → preset=%-10s fmt=%-3s %dx%d crf=%d audio=%s(%s)\n",
                   tag, preset.preset.c_str(), format.fmt.c_str(),
                   video.width, video.height, video.crf,
                   audio.enabled ? "开" : "关", audio.codec.c_str());
     }
   };

   int main() {
     ExportDialog dlg;
     dlg.dump("1. 默认");
     dlg.preset.choose("moments");
     dlg.dump("2. 选朋友圈预设");
     dlg.video.setCrf(20);                       // 手动调了一项参数，预设自动跳回 custom
     dlg.dump("3. 手动改 CRF=20");
     dlg.preset.choose("audio_only");
     dlg.dump("4. 选仅提取音频");
   }
   ```

   ```text
   1. 默认        → preset=custom     fmt=mp4 1280x720 crf=23 audio=开(aac)
   2. 选朋友圈预设 → preset=moments    fmt=mp4 720x1280 crf=25 audio=开(aac)
   3. 手动改 CRF=20 → preset=custom     fmt=mp4 720x1280 crf=20 audio=开(aac)
   4. 选仅提取音频 → preset=audio_only fmt=wav 720x1280 crf=20 audio=开(pcm)
   ```

3. 把 5 个模块做成 `MeetingRoom` 的成员，所有跨模块联动都写在 `MeetingRoom::notify()` 里，按「摄像头开关 → 摄像头帧率 → 录制主画面 → 录制横幅」的依赖顺序统一计算，这样任何模块变化都不会漏掉连锁反应（完整代码在 `ex3_room.cpp`）：

   ```cpp
   // 《中介者模式》练习 3：会议室的音视频协调器——屏幕共享、摄像头、麦克风、录制、低带宽监测 5 个同事的联动
   #include <cstdio>
   #include <string>

   struct RoomMediator {
     virtual ~RoomMediator() = default;
     virtual void notify(const std::string& who) = 0;
   };

   struct Camera {
     RoomMediator& m;
     bool userWantsOn = true;            // 用户自己想不想开摄像头
     bool active = true;                 // 实际有没有推摄像头流
     int fps = 30;
     void setUserOn(bool v) { userWantsOn = v; m.notify("camera"); }
   };

   struct ScreenShare {
     RoomMediator& m;
     bool sharing = false;
     void setSharing(bool s) { sharing = s; m.notify("screen"); }
   };

   struct Microphone {
     RoomMediator& m;
     bool muted = true;
     bool pttDown = false;               // 按住空格发言（Push-to-Talk）
     void setMuted(bool v) { muted = v; m.notify("mic"); }
     void setPtt(bool down) { pttDown = down; m.notify("mic"); }
   };

   struct Recorder {
     RoomMediator& m;
     bool recording = false;
     std::string mainSource = "摄像头";
     std::string banner = "";
     void setRecording(bool r) { recording = r; m.notify("rec"); }
   };

   struct NetMonitor {
     RoomMediator& m;
     bool lowBw = false;
     void setLowBandwidth(bool low) { lowBw = low; m.notify("net"); }
   };

   struct MeetingRoom : RoomMediator {
     Camera cam{*this};
     ScreenShare screen{*this};
     Microphone mic{*this};
     Recorder rec{*this};
     NetMonitor net{*this};

     void notify(const std::string&) override {
       // 1. 低带宽且正在共享屏幕时，自动暂停摄像头保屏幕流；否则尊重用户开关
       cam.active = cam.userWantsOn && !(net.lowBw && screen.sharing);
       // 2. 屏幕共享开启时，摄像头降到 15 fps 省编码算力；低带宽时也降到 15 fps
       cam.fps = (!cam.active) ? 0 : (screen.sharing || net.lowBw ? 15 : 30);
       // 3. 录制的主画面优先取屏幕共享，没有共享时取摄像头，都没有则是音频模式
       rec.mainSource = screen.sharing ? "屏幕共享" : (cam.active ? "摄像头" : "仅音频");
       // 4. 录制开启时显示横幅，并提示当前麦克风是否打开
       bool micLive = !mic.muted || mic.pttDown;
       rec.banner = rec.recording ? (micLive ? "● 录制中（已收音）" : "● 录制中（当前静音）") : "";
     }

     void dump(const char* step) const {
       bool micLive = !mic.muted || mic.pttDown;
       std::printf("%-18s → 摄像头:%s(%2dfps) 屏幕:%s 麦克风:%s 录制主画面:%-8s %s\n",
                   step, cam.active ? "开" : "关", cam.fps,
                   screen.sharing ? "共享中" : "未共享",
                   micLive ? "收音" : "静音",
                   rec.mainSource.c_str(), rec.banner.c_str());
     }
   };

   int main() {
     MeetingRoom room;
     room.notify("init");
     room.dump("1. 进入会议");
     room.screen.setSharing(true);
     room.dump("2. 开始共享屏幕");
     room.rec.setRecording(true);
     room.dump("3. 开启录制");
     room.mic.setPtt(true);
     room.dump("4. 按住空格发言");
     room.net.setLowBandwidth(true);
     room.dump("5. 网络带宽不足");
     room.net.setLowBandwidth(false);
     room.screen.setSharing(false);
     room.dump("6. 恢复网络并停共享");
   }
   ```

   ```text
   1. 进入会议    → 摄像头:开(30fps) 屏幕:未共享 麦克风:静音 录制主画面:摄像头 
   2. 开始共享屏幕 → 摄像头:开(15fps) 屏幕:共享中 麦克风:静音 录制主画面:屏幕共享 
   3. 开启录制    → 摄像头:开(15fps) 屏幕:共享中 麦克风:静音 录制主画面:屏幕共享 ● 录制中（当前静音）
   4. 按住空格发言 → 摄像头:开(15fps) 屏幕:共享中 麦克风:收音 录制主画面:屏幕共享 ● 录制中（已收音）
   5. 网络带宽不足 → 摄像头:关( 0fps) 屏幕:共享中 麦克风:收音 录制主画面:屏幕共享 ● 录制中（已收音）
   6. 恢复网络并停共享 → 摄像头:开(30fps) 屏幕:未共享 麦克风:收音 录制主画面:摄像头 ● 录制中（已收音）
   ```

4. 两个 bug 对应第 3.1 和 3.2 节：
   - **Bug ①（重入冲掉状态）**：`onSoloChanged` 在把其他轨道设为 `t->setSolo(false)` 时，会再次触发 `onSoloChanged(t)`，走进 `else` 分支把 `soloTrack` 清空、把所有轨道的 `muted` 全改回 `false`！加上 `syncing_` 守卫（同时 `setSolo` 在值未变时直接返回）即可解决。
   - **Bug ②（悬空同事指针）**：`preview` 离开局部作用域析构后，`mixer.tracks` 里还留着它的裸指针，第二次 `renderFrame()` 读写已释放的内存。给 `Track` 加上 `~Track()` 调用 `mixer.remove(this)`（顺便如果注销的正好是当前 `soloTrack`，解除独奏恢复其余轨道发声），并禁用拷贝。完整修复版（`ex4_fixed.cpp`）：

   <!-- run: on -->
   ```cpp
   // 《中介者模式》练习 4（修复版）：① syncing_ 守卫防止重入冲掉状态；② ~Track() 在析构时向中介者注销
   #include <algorithm>
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   struct Track;
   struct MixerMediator {
     std::vector<Track*> tracks;
     Track* soloTrack = nullptr;
     bool syncing_ = false;              // 修复 ①：正在由中介者批量同步时，忽略由此触发的二次通知

     void add(Track* t) { tracks.push_back(t); }
     void remove(Track* t);
     void onSoloChanged(Track* sender);
     void renderFrame();
   };

   struct Track {
     std::string name;
     MixerMediator& mixer;
     bool solo = false;
     bool muted = false;

     Track(std::string n, MixerMediator& m) : name(std::move(n)), mixer(m) {
       mixer.add(this);
     }
     ~Track() {                          // 修复 ②：析构时把自己从 mixer 里注销
       mixer.remove(this);
     }
     Track(const Track&) = delete;
     Track& operator=(const Track&) = delete;

     void setSolo(bool s) {
       if (solo == s) return;
       solo = s;
       mixer.onSoloChanged(this);
     }
   };

   void MixerMediator::remove(Track* t) {
     tracks.erase(std::remove(tracks.begin(), tracks.end(), t), tracks.end());
     if (soloTrack == t) {
       soloTrack = nullptr;
       for (Track* other : tracks) other->muted = false;
     }
   }

   void MixerMediator::onSoloChanged(Track* sender) {
     if (syncing_) return;
     syncing_ = true;
     if (sender->solo) {
       soloTrack = sender;
       for (Track* t : tracks) {
         if (t != sender) {
           t->setSolo(false);
           t->muted = true;
         } else {
           t->muted = false;
         }
       }
     } else {
       soloTrack = nullptr;
       for (Track* t : tracks) t->muted = false;
     }
     syncing_ = false;
   }

   void MixerMediator::renderFrame() {
     std::printf("当前混音输出：");
     for (Track* t : tracks)
       if (!t->muted) std::printf("[%s] ", t->name.c_str());
     std::printf("(独奏=%s)\n", soloTrack ? soloTrack->name.c_str() : "无");
   }

   int main() {
     MixerMediator mixer;
     Track bgm("背景音乐", mixer);
     Track voice("人声旁白", mixer);
     {
       auto preview = std::make_unique<Track>("试听音效", mixer);
       voice.setSolo(true);              // 只有「人声旁白」发声，背景音乐和试听音效都静音
       mixer.renderFrame();
       preview->setSolo(true);           // 切到「试听音效」独奏
       mixer.renderFrame();
     }                                   // preview 析构并自动注销，独奏解除，剩下两条轨道恢复发声
     mixer.renderFrame();
   }
   ```

   ```text
   当前混音输出：[人声旁白] (独奏=人声旁白)
   当前混音输出：[试听音效] (独奏=试听音效)
   当前混音输出：[背景音乐] [人声旁白] (独奏=无)
   ```

</details>
