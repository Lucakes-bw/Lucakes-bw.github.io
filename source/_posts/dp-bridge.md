---
title: 桥接模式
date: 2026-11-10 16:00:00
updated: 2026-11-10 16:00:00
categories:
  - 设计模式
  - 结构型模式
tags:
  - 设计模式
  - 桥接
  - Pimpl
  - SDL
order: 2
description: 播放器的画面要出现在主窗口、缩略图、画中画三种视图里，又要支持软件、SDL、Metal 三种渲染后端。用继承把两个维度绑在一起，3 × 3 要 9 个具体类，实测加一种视图要新增 45 行、加一种后端要新增 58 行，而且后端是类型的一部分，运行时换后端只能换一个新对象，画中画的位置随之丢失。桥接把两个维度拆成两棵继承树、用一个指针连起来，只要 3 + 3 个类，同样的两次改动只要 10 行和 12 行，运行时换后端也不丢状态。再用 SDL 的渲染驱动看一座真实的桥，实测多一层虚函数调用的开销（每帧 5 次调用约 11～20 ns，逐像素调用时 1080p 每帧约 1.3 ms），最后讲编译期的桥接、Pimpl，并分清桥接和适配器、策略、抽象工厂。
top_group_list: true
disableNunjucks: true
viz: [dp-bridge]
---

> **本文目标**：理解桥接模式解决的问题：两个独立变化的维度，用继承会得到 M × N 个类；会把一个类层次拆成「抽象」和「实现」两个层次，知道两边的接口该怎么划分；知道运行时的桥接和编译期的桥接（模板）各自的代价；认识 Pimpl 是只有一种实现的桥接；分清桥接和适配器、策略、抽象工厂。
>
> **前置知识**：上一篇《适配器模式》；第 1 章的《组合优于继承与 C++ 的四种多态》；最好读过音视频专栏的《最简单的视频播放器》，知道 SDL 的渲染器和纹理，不读也不影响理解。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/12-bridge/` 目录；用到 SDL3 的程序在 `sdl/` 子目录，用 `run.sh` 编译运行。

## 1. 问题：两个维度一起变化

### 1.1 视图和渲染后端

播放器的画面不只出现在一个地方：

- **主窗口**：大画面，下面是进度条和时间；
- **缩略图**：拖动进度条时弹出的小图，带一个时间戳；
- **画中画**：浮在桌面上的小窗，用户可以把它拖到任意位置。

画面又可以用不同的方式画到屏幕上，也就是不同的**渲染后端**：

- **软件**：CPU 把像素算好，写进内存里的一块画布；
- **SDL**：音视频专栏一直在用的 `SDL_Renderer`，上传纹理、让 GPU 去画；
- **Metal**：直接调用 macOS 的图形 API。

这是两个**独立变化的维度**：「画什么、怎么排版」由视图决定，「怎么把像素送到屏幕上」由后端决定。主窗口用哪种后端，和它要画进度条没有关系；Metal 后端怎么上传纹理，和它画的是主窗口还是缩略图也没有关系。以后两个维度都还会增加：视图可能多一个「多画面」，后端可能多一个 Vulkan。

### 1.2 用继承：每种组合一个类

最直接的写法是继承：视图的基类里写好排版，画图的几个基本操作声明成纯虚函数，再按后端派生出具体类。下面是 `inherit_v1.cpp` 的主要部分（完整代码约 200 行）：

<!-- run: off -->
```cpp
class VideoView {
public:
    virtual ~VideoView() = default;
    virtual std::string name() const = 0;
    virtual std::string backend() const = 0;
    virtual void draw(double t) = 0;
protected:
    virtual void begin(int w, int h) = 0;           // 画图的 5 个基本操作，留给具体类
    virtual void fill(Rect r, char c) = 0;
    virtual void frame(Rect r, double t) = 0;
    virtual void text(int x, int y, const std::string& s) = 0;
    virtual void present() = 0;
};

class MainWindow : public VideoView {               // 排版写在视图的基类里
public:
    std::string name() const override { return "主窗口"; }
    void draw(double t) override {
        begin(30, 7);
        frame({0, 0, 30, 5}, t);
        fill({0, 5, 30, 1}, '-');
        fill({0, 5, int(30 * t / 10), 1}, '=');
        text(0, 6, clock(t) + " / 00:10");
        present();
    }
};
// Thumbnail、PictureInPicture 同理

class MainWindowSoft : public MainWindow {          // 主窗口 × 软件
    SoftCanvas c_;                                  // 软件渲染的代码
public:
    std::string backend() const override { return "软件"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};
// MainWindowSdl、MainWindowMetal、ThumbnailSoft、ThumbnailSdl……一共 9 个，只有后端那一行不同

// 按配置创建：视图和后端的每一种组合都要列出来
std::unique_ptr<VideoView> makeView(const std::string& view, const std::string& backend) {
    if (view == "主窗口" && backend == "软件") return std::make_unique<MainWindowSoft>();
    if (view == "主窗口" && backend == "SDL") return std::make_unique<MainWindowSdl>();
    // …… 一共 9 行
    return nullptr;
}
```

为了公平，三个后端真正干活的代码已经各自抽成了一个类（`SoftCanvas`、`SdlApi`、`MetalApi`），具体类只是把 5 个基本操作转发过去。如果不抽出来，每个后端的代码要在 3 个具体类里各抄一遍。SDL 和 Metal 后端在这里只打印它们会调用的 API；软件后端是真的在画，画在一块字符画布上，用移动的明暗条纹代替视频画面。

```uml
label: 继承版：VideoView 下有三个视图基类，每个视图基类下又按后端派生出三个具体类，一共 9 个具体类
classes:
  - { id: V, name: VideoView, stereo: abstract, x: 188, y: 10, w: 344, ops: ['*+ draw(t)', '*# begin() fill() frame() text() present()'] }
  - { id: MW, name: MainWindow, x: 10, y: 140, w: 210, ops: ['+ draw(t)  // 排版'] }
  - { id: TH, name: Thumbnail, x: 255, y: 140, w: 210, ops: ['+ draw(t)  // 排版'] }
  - { id: PP, name: PictureInPicture, x: 500, y: 140, w: 210, ops: ['+ draw(t)  // 排版'] }
  - { id: MW1, name: MainWindowSoft, x: 40, y: 222, w: 180 }
  - { id: MW2, name: MainWindowSdl, x: 40, y: 264, w: 180 }
  - { id: MW3, name: MainWindowMetal, x: 40, y: 306, w: 180 }
  - { id: TH1, name: ThumbnailSoft, x: 285, y: 222, w: 180 }
  - { id: TH2, name: ThumbnailSdl, x: 285, y: 264, w: 180 }
  - { id: TH3, name: ThumbnailMetal, x: 285, y: 306, w: 180 }
  - { id: PP1, name: PictureInPictureSoft, x: 530, y: 222, w: 190 }
  - { id: PP2, name: PictureInPictureSdl, x: 530, y: 264, w: 190 }
  - { id: PP3, name: PictureInPictureMetal, x: 530, y: 306, w: 190 }
rels:
  - { from: MW, to: V, type: inherit, mid: 112 }
  - { from: TH, to: V, type: inherit, mid: 112 }
  - { from: PP, to: V, type: inherit, mid: 112 }
  - { from: MW1, to: MW, type: inherit, points: [[40, 237], [24, 237], [24, 195]] }
  - { from: MW2, to: MW, type: inherit, points: [[40, 279], [24, 279], [24, 195]] }
  - { from: MW3, to: MW, type: inherit, points: [[40, 321], [24, 321], [24, 195]] }
  - { from: TH1, to: TH, type: inherit, points: [[285, 237], [269, 237], [269, 195]] }
  - { from: TH2, to: TH, type: inherit, points: [[285, 279], [269, 279], [269, 195]] }
  - { from: TH3, to: TH, type: inherit, points: [[285, 321], [269, 321], [269, 195]] }
  - { from: PP1, to: PP, type: inherit, points: [[530, 237], [514, 237], [514, 195]] }
  - { from: PP2, to: PP, type: inherit, points: [[530, 279], [514, 279], [514, 195]] }
  - { from: PP3, to: PP, type: inherit, points: [[530, 321], [514, 321], [514, 195]] }
texts:
  - { x: 10, y: 362, text: '每个具体类里还有一份后端代码（SoftCanvas / SdlApi / MetalApi），把 5 个基本操作转发过去', muted: true }
```

### 1.3 实测：类的数量相乘，换后端只能换对象

`main` 里按配置创建三个视图：主窗口用软件渲染，缩略图用 SDL，画中画用 Metal，用户把画中画拖到了右上角。然后画中画被拖到一个外接显示器上，那里 Metal 不可用，要换成软件渲染：

<!-- run: off -->
```cpp
int main() {
    auto main = makeView("主窗口", "软件");
    auto thumb = makeView("缩略图", "SDL");
    auto pip = makeView("画中画", "Metal");
    static_cast<PictureInPicture&>(*pip).moveTo(14, 0);    // 用户把画中画拖到了右上角

    for (auto* v : {main.get(), thumb.get(), pip.get()}) {
        std::printf("== %s（%s）\n", v->name().c_str(), v->backend().c_str());
        v->draw(3.5);
    }
    std::printf("-- 画中画拖到外接显示器上，Metal 不可用，换成软件渲染 --\n");
    pip = makeView("画中画", "软件");                        // 后端是类型的一部分，只能换一个新对象
    std::printf("== %s（%s）\n", pip->name().c_str(), pip->backend().c_str());
    pip->draw(3.75);
}
```

```text
== 主窗口（软件）
|*% .:-=+*% .:-=+*% .:-=+*% .:-|
|% .:-=+*% .:-=+*% .:-=+*% .:-=|
| .:-=+*% .:-=+*% .:-=+*% .:-=+|
|.:-=+*% .:-=+*% .:-=+*% .:-=+*|
|:-=+*% .:-=+*% .:-=+*% .:-=+*%|
|==========--------------------|
|00:03 / 00:10                 |
== 缩略图（SDL）
SDL_RenderClear UpdateTexture RenderTexture TTF_RenderText SDL_RenderPresent
== 画中画（Metal）
renderCommandEncoder drawPrimitives replaceRegion drawPrimitives presentDrawable commit
-- 画中画拖到外接显示器上，Metal 不可用，换成软件渲染 --
== 画中画（软件）
|                        |
|##########              |
|#:-=+*% .#              |
|#-=+*% .:#              |
|##########              |
```

能画出来，但有两个问题：

1. **类的数量是相乘的**。M 种视图、N 种后端，就要 M × N 个具体类。现在是 3 × 3 = 9 个；视图和后端各加一种，就是 4 × 4 = 16 个，`makeView` 也要列 16 行。加一种后端，要给每种视图各写一个类；加一种视图，要给每种后端各写一个类。
2. **后端是类型的一部分**。一个 `PictureInPictureMetal` 对象永远是 Metal 的，要换成软件渲染，只能销毁它、新建一个 `PictureInPictureSoft`。新对象里的状态都是默认值：上面的输出里，画中画从右上角**跳回了左下角**。要避免这一点，就得在 `makeView` 之后把旧对象的每一个状态（位置、大小、音量、字幕……）都手动拷过去，每种视图的状态还不一样。

问题的根源是：继承把两个维度绑在了同一棵继承树上。「主窗口」和「Metal」本来是两件独立的事，继承却要求为它们的每一种搭配写一个类。

## 2. 桥接模式

### 2.1 意图

**桥接模式**（Bridge）：将抽象部分与它的实现部分分离，使它们都可以独立地变化。

这里的「抽象」和「实现」不是 C++ 里抽象类和派生类的意思，容易误解：

- **抽象部分**（Abstraction）：使用者直接面对的那一层，负责高层的逻辑。这里是视图：画什么、怎么排版。
- **实现部分**（Implementor）：抽象部分用来完成工作的底层操作，通常和平台有关。这里是渲染后端：填一个矩形、贴一帧画面、显示出来。

做法是把一棵继承树拆成两棵：视图一棵、后端一棵。视图里持有一个指向后端接口的指针，所有的绘制都通过这个指针完成。这个指针就是连接两边的**桥**。

### 2.2 用桥接重构

后端的 5 个基本操作变成一个独立的接口 `Renderer`，三个后端各实现一次；视图持有一个 `std::unique_ptr<Renderer>`，排版的代码一行没变，只是 `fill(...)` 变成了 `r().fill(...)`：

```cpp
// 《桥接模式》：视图（主窗口、缩略图、画中画）× 渲染后端（SDL、Metal、软件），桥接只要 3 + 3 个类
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Rect { int x, y, w, h; };

std::string clock(double t) {                       // 3.5 → "00:03"
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", int(t) / 60, int(t) % 60);
    return buf;
}

// ---------- 实现部分：渲染后端只提供几个基本操作 ----------
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual std::string name() const = 0;
    virtual void begin(int w, int h) = 0;                       // 开始画一帧，画布 w × h
    virtual void fill(Rect r, char c) = 0;                      // 填充矩形（边框、进度条）
    virtual void frame(Rect r, double t) = 0;                   // 把 t 秒处的视频帧缩放画进 r
    virtual void text(int x, int y, const std::string& s) = 0;  // 写一行字
    virtual void present() = 0;                                 // 把这一帧显示出来
};

// 软件渲染：画在内存里的字符画布上，present 时打印出来
class SoftRenderer : public Renderer {
public:
    std::string name() const override { return "软件"; }
    void begin(int w, int h) override { rows_.assign(h, std::string(w, ' ')); }
    void fill(Rect r, char c) override { each(r, [&](int, int) { return c; }); }
    void frame(Rect r, double t) override {                     // 用移动的明暗条纹代替画面
        each(r, [&](int x, int y) { return " .:-=+*%"[(x + y + int(t * 4)) % 8]; });
    }
    void text(int x, int y, const std::string& s) override { rows_[y].replace(x, s.size(), s); }
    void present() override { for (const auto& row : rows_) std::printf("|%s|\n", row.c_str()); }
private:
    template <class F> void each(Rect r, F pixel) {
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) rows_[y][x] = pixel(x, y);
    }
    std::vector<std::string> rows_;
};

// SDL 和 Metal：这里只记下会调用哪些 API，present 时打印一行
class SdlRenderer : public Renderer {
public:
    std::string name() const override { return "SDL"; }
    void begin(int, int) override { log_ = "SDL_RenderClear"; }
    void fill(Rect, char) override { log_ += " RenderFillRect"; }
    void frame(Rect, double) override { log_ += " UpdateTexture RenderTexture"; }
    void text(int, int, const std::string&) override { log_ += " TTF_RenderText"; }
    void present() override { std::printf("%s SDL_RenderPresent\n", log_.c_str()); }
private:
    std::string log_;
};

class MetalRenderer : public Renderer {
public:
    std::string name() const override { return "Metal"; }
    void begin(int, int) override { log_ = "renderCommandEncoder"; }
    void fill(Rect, char) override { log_ += " drawPrimitives"; }
    void frame(Rect, double) override { log_ += " replaceRegion drawPrimitives"; }
    void text(int, int, const std::string&) override { log_ += " drawGlyphs"; }
    void present() override { std::printf("%s presentDrawable commit\n", log_.c_str()); }
private:
    std::string log_;
};

// ---------- 抽象部分：视图决定画什么、怎么排版，通过 Renderer 去画 ----------
class VideoView {
public:
    explicit VideoView(std::unique_ptr<Renderer> r) : r_(std::move(r)) {}
    virtual ~VideoView() = default;
    virtual std::string name() const = 0;
    virtual void draw(double t) = 0;
    void setRenderer(std::unique_ptr<Renderer> r) { r_ = std::move(r); }   // 运行时换后端，视图的状态不变
    std::string backend() const { return r_->name(); }
protected:
    Renderer& r() { return *r_; }
private:
    std::unique_ptr<Renderer> r_;                                          // 桥：视图只通过它使用后端
};

class MainWindow : public VideoView {                // 画面 + 进度条 + 时间
public:
    using VideoView::VideoView;
    std::string name() const override { return "主窗口"; }
    void draw(double t) override {
        r().begin(30, 7);
        r().frame({0, 0, 30, 5}, t);
        r().fill({0, 5, 30, 1}, '-');
        r().fill({0, 5, int(30 * t / 10), 1}, '=');  // 测试视频 10 秒
        r().text(0, 6, clock(t) + " / 00:10");
        r().present();
    }
};

class Thumbnail : public VideoView {                 // 小图 + 时间戳
public:
    using VideoView::VideoView;
    std::string name() const override { return "缩略图"; }
    void draw(double t) override {
        r().begin(10, 4);
        r().frame({0, 0, 10, 3}, t);
        r().text(0, 3, clock(t));
        r().present();
    }
};

class PictureInPicture : public VideoView {          // 屏幕上一个带边框的小窗，可以拖动
public:
    using VideoView::VideoView;
    std::string name() const override { return "画中画"; }
    void moveTo(int x, int y) { x_ = x; y_ = y; }
    void draw(double t) override {
        r().begin(24, 5);                            // 整个屏幕
        r().fill({x_, y_, 10, 4}, '#');              // 边框
        r().frame({x_ + 1, y_ + 1, 8, 2}, t);
        r().present();
    }
private:
    int x_ = 0, y_ = 1;                              // 默认在左下角
};

// 按配置创建后端：每种后端一行；视图直接构造，把后端传进去
std::unique_ptr<Renderer> makeRenderer(const std::string& name) {
    if (name == "软件") return std::make_unique<SoftRenderer>();
    if (name == "SDL") return std::make_unique<SdlRenderer>();
    if (name == "Metal") return std::make_unique<MetalRenderer>();
    return nullptr;
}

int main() {
    MainWindow main(makeRenderer("软件"));
    Thumbnail thumb(makeRenderer("SDL"));
    PictureInPicture pip(makeRenderer("Metal"));
    pip.moveTo(14, 0);                               // 用户把画中画拖到了右上角

    std::vector<VideoView*> views = {&main, &thumb, &pip};
    for (auto* v : views) {
        std::printf("== %s（%s）\n", v->name().c_str(), v->backend().c_str());
        v->draw(3.5);
    }
    std::printf("-- 画中画拖到外接显示器上，Metal 不可用，换成软件渲染 --\n");
    pip.setRenderer(makeRenderer("软件"));                   // 同一个对象，换一个后端
    std::printf("== %s（%s）\n", pip.name().c_str(), pip.backend().c_str());
    pip.draw(3.75);
}
```

```text
== 主窗口（软件）
|*% .:-=+*% .:-=+*% .:-=+*% .:-|
|% .:-=+*% .:-=+*% .:-=+*% .:-=|
| .:-=+*% .:-=+*% .:-=+*% .:-=+|
|.:-=+*% .:-=+*% .:-=+*% .:-=+*|
|:-=+*% .:-=+*% .:-=+*% .:-=+*%|
|==========--------------------|
|00:03 / 00:10                 |
== 缩略图（SDL）
SDL_RenderClear UpdateTexture RenderTexture TTF_RenderText SDL_RenderPresent
== 画中画（Metal）
renderCommandEncoder drawPrimitives replaceRegion drawPrimitives presentDrawable commit
-- 画中画拖到外接显示器上，Metal 不可用，换成软件渲染 --
== 画中画（软件）
|              ##########|
|              #% .:-=+*#|
|              # .:-=+*%#|
|              ##########|
|                        |
```

前面三个视图的输出和继承版完全一样。区别在最后：桥接版换后端时，`pip` 还是原来那个对象，只是 `setRenderer` 换掉了它指向的后端。位置、播放进度这些状态都在视图里，原样保留，画中画**还在右上角**。

### 2.3 类图

```uml
label: 桥接：VideoView 持有一个 Renderer（桥）；MainWindow、Thumbnail、PictureInPicture 继承 VideoView，SoftRenderer、SdlRenderer、MetalRenderer 实现 Renderer，两边各自扩展
classes:
  - { id: V, name: VideoView, stereo: abstract, x: 20, y: 20, w: 260, attrs: ['- r_ : unique_ptr<Renderer>'], ops: ['*+ draw(t)', '+ setRenderer(r)'] }
  - { id: R, name: Renderer, stereo: interface, x: 430, y: 0, w: 210, ops: ['*+ begin(w, h)', '*+ fill(r, c)', '*+ frame(r, t)', '*+ text(x, y, s)', '*+ present()'] }
  - { id: MW, name: MainWindow, x: 0, y: 190, w: 98 }
  - { id: TH, name: Thumbnail, x: 108, y: 190, w: 90 }
  - { id: PP, name: PictureInPicture, x: 208, y: 190, w: 145 }
  - { id: SR, name: SoftRenderer, x: 405, y: 190, w: 114 }
  - { id: DR, name: SdlRenderer, x: 529, y: 190, w: 106 }
  - { id: MR, name: MetalRenderer, x: 645, y: 190, w: 121 }
rels:
  - { from: V, to: R, type: aggregate, label: '桥' }
  - { from: MW, to: V, type: inherit, mid: 162 }
  - { from: TH, to: V, type: inherit, mid: 162 }
  - { from: PP, to: V, type: inherit, mid: 162 }
  - { from: SR, to: R, type: realize, mid: 162 }
  - { from: DR, to: R, type: realize, mid: 162 }
  - { from: MR, to: R, type: realize, mid: 162 }
texts:
  - { x: 0, y: 250, text: '抽象部分：画什么、怎么排版', muted: true }
  - { x: 405, y: 250, text: '实现部分：怎么把像素送到屏幕上', muted: true }
```

| 角色 | 本例 | 职责 |
| --- | --- | --- |
| 抽象（Abstraction） | `VideoView` | 定义使用者看到的接口，持有一个实现接口的指针 |
| 扩充抽象（Refined Abstraction） | `MainWindow`、`Thumbnail`、`PictureInPicture` | 用实现接口提供的基本操作，组合出各自的功能 |
| 实现接口（Implementor） | `Renderer` | 定义基本操作，不必和抽象的接口一致 |
| 具体实现（Concrete Implementor） | `SoftRenderer`、`SdlRenderer`、`MetalRenderer` | 在某个平台上实现这些基本操作 |

### 2.4 实测：两个维度各加一种

现在让两个维度各增加一种：先加一种视图「多画面」（2 × 2 四个画面，预览不同的时间点），再加一种后端 Vulkan。两种写法各做一遍，得到 `inherit_v2/v3.cpp` 和 `bridge_v2/v3.cpp`，用 `measure.sh` 统计（不算第一行注释、空行和 `main` 函数）：

```text
== inherit
  inherit_v1.cpp：16 个类，207 行
  inherit_v2.cpp：20 个类，252 行
  inherit_v3.cpp：25 个类，310 行
  v1 → v2（加一种视图）：+45 -0
  v2 → v3（加一种后端）：+58 -0
== bridge
  bridge_v1.cpp：8 个类，121 行
  bridge_v2.cpp：9 个类，131 行
  bridge_v3.cpp：10 个类，143 行
  v1 → v2（加一种视图）：+10 -0
  v2 → v3（加一种后端）：+12 -0
```

<figure class="diagram">
<svg viewBox="0 0 720 186" role="img" aria-label="两个维度各加一种时的改动：加一种视图，继承版新增 4 个类、45 行，桥接版 1 个类、10 行；加一种后端，继承版新增 5 个类、58 行，桥接版 1 个类、12 行"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">新增的类</text><rect x="178.0" y="30.0" width="144.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="328.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">4 个</text><rect x="178.0" y="60.0" width="36.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="220.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1 个</text><rect x="178.0" y="90.0" width="180.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="364.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5 个</text><rect x="178.0" y="120.0" width="36.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="220.0" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1 个</text><text x="448.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">新增的代码行</text><rect x="448.0" y="30.0" width="139.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="593.7" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">45 行</text><rect x="448.0" y="60.0" width="31.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="485.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">10 行</text><rect x="448.0" y="90.0" width="180.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="634.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">58 行</text><rect x="448.0" y="120.0" width="37.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="491.2" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">12 行</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">继承：加一种视图</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">桥接：加一种视图</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">继承：加一种后端</text><text x="170.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">桥接：加一种后端</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">sh measure.sh：diff 统计，不算空行和 main；macOS 和 Ubuntu 上结果相同</text></g></svg>
</figure>

| | 继承 | 桥接 |
| --- | --- | --- |
| v1：3 种视图 × 3 种后端 | 16 个类，207 行 | 8 个类，121 行 |
| v2：加一种视图「多画面」 | 20 个类，252 行（**+4 个类，+45 行**） | 9 个类，131 行（**+1 个类，+10 行**） |
| v3：再加一种后端 Vulkan | 25 个类，310 行（**+5 个类，+58 行**） | 10 个类，143 行（**+1 个类，+12 行**） |

类的个数可以直接算出来。继承版是 1 个 `VideoView` + M 个视图基类 + N 份后端代码 + **M × N** 个具体类，3 × 3 时是 1 + 3 + 3 + 9 = 16；桥接版是 2 个接口 + **M + N** 个类，3 + 3 时是 8。到 4 × 4 时是 25 比 10；如果两个维度都到 10 种，就是 121 比 22。

改动的位置也不一样。继承版加 Vulkan 要动三处：新的 `VulkanApi`、4 个新的具体类、`makeView` 里 4 个新分支；桥接版只加一个 `VulkanRenderer` 类和 `makeRenderer` 里的一行，视图一行都不用改。下面的演示可以一步步看这个过程：

<div class="dsviz" data-viz="dp-bridge"></div>

### 2.5 实测：运行时换后端

把两个版本最后一步的输出放在一起：

```text
继承版：换了一个新对象        桥接版：同一个对象，换了后端
|                        |    |              ##########|
|##########              |    |              #% .:-=+*#|
|#:-=+*% .#              |    |              # .:-=+*%#|
|#-=+*% .:#              |    |              ##########|
|##########              |    |                        |
```

继承版换的是**对象**，桥接版换的是对象里的**一个成员**。这是「组合优于继承」在这里的具体体现：继承关系在编译时就定死了，组合关系可以在运行时改变。

真实的播放器里，换后端的场景并不少见：窗口拖到另一块显示器上、显卡驱动出错（GPU 设备丢失）后降级到软件渲染、笔记本在独立显卡和集成显卡之间切换、用户在设置里改了渲染方式。

## 3. 抽象和实现怎么划分

### 3.1 实现接口只放基本操作

桥接模式里最关键的设计决定，是 `Renderer` 这个接口里放什么。本例放的是 5 个**基本操作**：开始一帧、填矩形、贴一帧画面、写字、显示。它们有两个特点：

- **每个后端都做得到**，而且和视图无关。不管画的是主窗口还是缩略图，填一个矩形就是填一个矩形；
- **视图用它们组合出自己的功能**。进度条是两次 `fill`，画中画的边框是一次 `fill` 加一次 `frame`。

GoF 特意指出，实现接口**不必和抽象的接口一致**：抽象这边是 `draw(t)` 这样的高层操作，实现那边是 `fill` 这样的基本操作。如果反过来，在 `Renderer` 里写 `drawMainWindow()`、`drawThumbnail()`，那么每加一种视图，每个后端都要多实现一个函数，又回到了 M × N（练习 4）。

一个简单的检查方法：**实现接口里不应该出现抽象那一边的子类名字**。

### 3.2 一座真实的桥：SDL 的渲染驱动

音视频专栏的播放器里一直在用的 `SDL_Renderer`，本身就是一座桥。我们调用的 `SDL_RenderTexture`、`SDL_RenderFillRect` 是抽象这一侧；SDL 内部为每种图形 API 写了一个渲染驱动（Metal、OpenGL、Direct3D、Vulkan、软件……），它们实现同一组基本操作：创建和更新纹理、执行一批绘制命令、读回像素、显示。创建渲染器时可以按名字选择驱动（`SDL_CreateRenderer(win, "software")`），也可以用环境变量 `SDL_RENDER_DRIVER` 指定，音视频专栏的《最简单的视频播放器》就是用软件驱动做测量的。

`sdl/sdl_drivers.cpp` 把同一段绘制代码依次跑在每一个驱动上：上传一帧 720p 的 YUV 画面，缩放到 640 × 360 的窗口里，再画一条进度条，然后把画面读回来，看看各个驱动画的是否一样：

<!-- run: off -->
```cpp
// 只依赖 SDL_Renderer 这个「抽象」的代码：上传一帧 YUV、缩放画到窗口、画进度条。它不知道背后是哪个驱动
void drawFrame(SDL_Renderer* r, SDL_Texture* tex, const std::vector<Uint8>& yuv, double progress) {
    SDL_UpdateTexture(tex, nullptr, yuv.data(), VW);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_FRect dst{0, 0, 640, 360};
    SDL_RenderTexture(r, tex, nullptr, &dst);
    SDL_SetRenderDrawColor(r, 255, 128, 0, 255);
    SDL_FRect bar{0, 350, float(640 * progress), 10};
    SDL_RenderFillRect(r, &bar);
}

// main 里：对每一个驱动
SDL_Renderer* r = SDL_CreateRenderer(win, SDL_GetRenderDriver(d));    // 按名字选择驱动
SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, VW, VH);
drawFrame(r, tex, yuv, 0.35);
SDL_Surface* shot = SDL_RenderReadPixels(r, nullptr);                // 读回画面中心和进度条上的像素
// ……
SDL_RenderTexture(r, first, nullptr, nullptr);                       // 再试试用第一个驱动创建的纹理
```

macOS 上用 Homebrew 的 SDL 3 运行 `sdl/run.sh`：

```text
视频驱动 cocoa，可用的渲染驱动 6 个
  metal      画面中心 (127,127,127)，进度条 (255,128,0)
  opengl     画面中心 (127,127,127)，进度条 (255,128,0)
             用 metal 创建的纹理：Texture was not created with this renderer
  opengles2  创建失败：Could not initialize OpenGL / GLES library
  vulkan     创建失败：Failed to load Vulkan Portability library
  gpu        画面中心 (127,127,127)，进度条 (255,128,0)
             用 metal 创建的纹理：Texture was not created with this renderer
  software   画面中心 (127,127,127)，进度条 (255,128,0)
             用 metal 创建的纹理：Texture was not created with this renderer
```

可以看到三件事：

- **同一段代码，四个驱动画出来的像素完全一样**。`drawFrame` 不知道、也不需要知道背后是 Metal 还是软件。
- **有些实现在这台机器上不可用**：macOS 没有 OpenGL ES 库，也没有装 Vulkan 的移植层 MoltenVK。所以桥接常常和工厂一起出现，由工厂按运行环境选一个能用的实现（上一章的《抽象工厂》就是这么做的）。
- **纹理属于具体的驱动**。Metal 驱动创建的纹理在显存里，是 Metal 的对象，交给 OpenGL 驱动去画，SDL 直接拒绝。

第三点说明了运行时换后端的真实代价：**属于实现的资源**（纹理、着色器、留在显存里的帧）要在新的后端上重新创建；**属于抽象的状态**（画中画的位置、播放进度）不受影响。设计接口时要想清楚每样东西属于哪一边：本例的 `SoftRenderer` 把画布放在自己里面，换后端时画布跟着旧的后端一起销毁，下一帧会在新的后端上重新画出来。

### 3.3 实测：多一层间接调用的开销

桥接版的每次绘制都要经过 `r_` 这个指针，再调用一个虚函数。这会不会慢？`bench_dispatch.cpp` 用同样的画面（一块 30 × 7 的字符画布，每帧 5 个矩形）比较几种写法：

<!-- run: off -->
```cpp
// 继承：基类的 draw 调用虚函数 fill，具体类实现它
void ViewI::draw(int t) { layout(t, [this](Rect r, char c) { fill(r, c); }); }
// 桥接：通过 unique_ptr<Renderer> 调用虚函数 fill
void ViewB::draw(int t) { layout(t, [this](Rect r, char c) { r_->fill(r, c); }); }
// 桥接，但接口是逐像素的：每帧调用 30 × 7 = 210 次虚函数 setPixel
void ViewB::drawPixels(int t) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) r_->setPixel(x, y, pixelAt(t, x, y));
}
// 模板：后端是模板参数，没有虚函数（第 4.1 节）
template <class Backend> void ViewT<Backend>::draw(int t) { layout(t, [this](Rect r, char c) { be_.fill(r, c); }); }
```

后端在运行时才确定（编译器没法把虚函数调用优化掉），用 `-O2` 编译，每种写法画 200 万帧、跑 7 次取中位数：

<figure class="diagram">
<svg viewBox="0 0 740 216" role="img" aria-label="画一帧的耗时：粗粒度接口每帧 5 次调用，继承和桥接都约 20 纳秒（Clang）或 10 纳秒（GCC），模板约 2 纳秒；逐像素接口每帧 210 次虚函数调用约 130 纳秒，模板约 3 纳秒"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang 21（macOS）</text><rect x="198.0" y="30.0" width="26.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="230.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">20.2 ns</text><rect x="198.0" y="60.0" width="26.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="230.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">20.2 ns</text><rect x="198.0" y="90.0" width="3.2" height="20.0" rx="2" style="fill:var(--link)"/><text x="207.2" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2.5 ns</text><rect x="198.0" y="120.0" width="171.6" height="20.0" rx="2" style="fill:var(--danger)"/><text x="375.6" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">133.5 ns</text><rect x="198.0" y="150.0" width="3.7" height="20.0" rx="2" style="fill:var(--link)"/><text x="207.7" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2.9 ns</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13（Ubuntu 24.04）</text><rect x="468.0" y="30.0" width="14.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="488.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">11.1 ns</text><rect x="468.0" y="60.0" width="14.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="488.8" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">11.5 ns</text><rect x="468.0" y="90.0" width="2.7" height="20.0" rx="2" style="fill:var(--link)"/><text x="476.7" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2.1 ns</text><rect x="468.0" y="120.0" width="165.5" height="20.0" rx="2" style="fill:var(--danger)"/><text x="639.5" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">128.7 ns</text><rect x="468.0" y="150.0" width="3.3" height="20.0" rx="2" style="fill:var(--link)"/><text x="477.3" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2.6 ns</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">继承 · fill × 5</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">桥接 · fill × 5</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">模板 · fill × 5</text><text x="190.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">桥接 · setPixel × 210</text><text x="190.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">模板 · setPixel × 210</text><text x="10.0" y="208.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_dispatch -O2：画 200 万帧，7 次取中位数，单位 ns/帧；同一台机器，Linux 跑在 colima 虚拟机里</text></g></svg>
</figure>

| 写法 | Clang 21（macOS） | GCC 13（Ubuntu 24.04） |
| --- | --- | --- |
| 继承，每帧 5 次虚调用 `fill` | 20.2 ns | 11.1 ns |
| 桥接，每帧 5 次虚调用 `fill` | 20.2 ns | 11.5 ns |
| 模板，没有虚函数 | 2.5 ns | 2.1 ns |
| 桥接，每帧 210 次虚调用 `setPixel` | 133.5 ns | 128.7 ns |
| 模板，逐像素，没有虚函数 | 2.9 ns | 2.6 ns |

几个结论：

- **继承和桥接一样快**。两种写法都要调用虚函数，桥接只是多读一次 `r_` 指针，测不出差别。两个编译器生成的代码不同，绝对值差了一倍，但在同一个编译器下，结论是一样的。
- **虚函数比模板慢，但对播放器无关紧要**。每帧多出不到 20 ns，按 60 帧每秒算，每秒多花约 1 微秒，而一帧的时间预算是 16.7 毫秒。模板版快，是因为编译器看得见要调用的函数，可以把它内联进来，再和循环一起优化。
- **真正要紧的是接口的粒度**。同样是桥接，逐像素的接口每帧要 133.5 ns，平均每次虚调用约 0.64 ns；照这个速度，一帧 1920 × 1080 = 207 万个像素要约 1.3 毫秒，占掉 60 帧每秒预算的 8%。而模板版的逐像素循环可以向量化，210 个像素只要 2.9 ns。

所以实现接口要**粗粒度**：「填一个矩形」「上传一整帧」，而不是「设一个像素」。SDL 也是这样设计的：它虽然有画单个点的 `SDL_RenderPoint`，但同时提供一次画一批的 `SDL_RenderPoints`、`SDL_RenderFillRects`，视频画面则用 `SDL_UpdateTexture` 一次上传一整帧。

## 4. 现代 C++ 写法

### 4.1 编译期的桥接：模板

如果后端在编译时就能确定（比如 Metal 只在苹果的系统上有），可以把后端变成模板参数：

```cpp
// 《桥接模式》：编译期的桥接，后端是模板参数。写的是 M + N 个类，组合由编译器按需要生成
#include <cstdio>
#include <string>

struct SoftBackend {
    static const char* name() { return "软件"; }
    void frame(int w, int h) { std::printf("    画 %d×%d 的字符画\n", w, h); }
};
struct MetalBackend {
    static const char* name() { return "Metal"; }
    void frame(int w, int h) { std::printf("    drawPrimitives %d×%d\n", w, h); }
};

template <class Backend> class MainWindow {           // 没有虚函数，后端是成员，不是指针
public:
    void draw() { std::printf("  主窗口（%s）\n", Backend::name()); be_.frame(30, 5); }
private:
    Backend be_;
};
template <class Backend> class Thumbnail {
public:
    void draw() { std::printf("  缩略图（%s）\n", Backend::name()); be_.frame(10, 3); }
private:
    Backend be_;
};

#ifdef __APPLE__
using PlatformBackend = MetalBackend;                 // 后端在编译时按平台选定
#else
using PlatformBackend = SoftBackend;
#endif

int main() {
    MainWindow<PlatformBackend> main;
    Thumbnail<SoftBackend> thumb;
    main.draw();
    thumb.draw();
    // thumb = Thumbnail<MetalBackend>();             // 编译错误：Thumbnail<SoftBackend> 和 Thumbnail<MetalBackend> 是两个不同的类型
}
```

```text
  主窗口（Metal）
    drawPrimitives 30×5
  缩略图（软件）
    画 10×3 的字符画
```

这是 macOS 上的输出；在 Linux 和浏览器里运行时没有定义 `__APPLE__`，主窗口用的是软件后端。

编译期的桥接仍然只写 M + N 个类，`MainWindow<MetalBackend>` 这样的组合由编译器按需要生成（第 1.2 节那 9 个转发类，其实就是手写的模板实例）。好处是没有虚函数，调用可以内联，第 3.3 节的实测里快了 4 到 8 倍；代价是后端又成了**类型的一部分**：`Thumbnail<SoftBackend>` 和 `Thumbnail<MetalBackend>` 是两个不相关的类型，不能放进同一个容器，也不能在运行时互换。

| | 运行时的桥接（指针 + 虚函数） | 编译期的桥接（模板） |
| --- | --- | --- |
| 手写的类 | M + N | M + N |
| 运行时换后端 | 能 | 不能 |
| 不同后端的视图放进同一个容器 | 能 | 不能（除非再包一层，见第 5 章的类型擦除） |
| 调用开销 | 每次一个虚函数调用 | 可以内联 |
| 适合 | 后端在运行时才能确定（看硬件、看配置） | 后端由平台决定，编译时就知道 |

### 4.2 Pimpl：只有一种实现的桥接

C++ 里有一个非常常见的惯用法叫 **Pimpl**（pointer to implementation）：类的头文件里只放一个指向「实现」的指针，所有的成员变量都藏在 `.cpp` 文件里：

```cpp
// 《桥接模式》：Pimpl 是只有一种实现的桥接（为了放在一个文件里，头文件和实现文件用注释分开）
#include <cstdio>
#include <memory>
#include <string>

// ===== player.h：使用者只能看到这些 =====
class Player {
public:
    Player();
    ~Player();                                       // 必须在 Impl 完整的地方定义（见下面）
    void open(const std::string& path);
    void play();
private:
    struct Impl;                                     // 只声明，不定义：实现细节一个都不出现在头文件里
    std::unique_ptr<Impl> impl_;                     // 桥：抽象这边只有一个指针
};

// ===== player.cpp：改这里的成员，包含 player.h 的文件都不用重新编译 =====
struct Player::Impl {
    std::string path;
    int decodedFrames = 0;
    // 真实的播放器这里还有 FFmpeg 的上下文、解码线程、音频设备……
    void decodeSome() { decodedFrames += 24; }
};

Player::Player() : impl_(std::make_unique<Impl>()) {}
Player::~Player() = default;                         // 这里 Impl 已经完整，unique_ptr 知道怎么删除它
void Player::open(const std::string& path) { impl_->path = path; }
void Player::play() {
    impl_->decodeSome();
    std::printf("播放 %s，已解码 %d 帧\n", impl_->path.c_str(), impl_->decodedFrames);
}

// ===== main.cpp =====
int main() {
    Player p;
    p.open("bbb-10s.mp4");
    p.play();
    std::printf("sizeof(Player) = %zu（只有一个指针）\n", sizeof(Player));
}
```

```text
播放 bbb-10s.mp4，已解码 24 帧
sizeof(Player) = 8（只有一个指针）
```

结构上，它就是一座桥：`Player` 是抽象部分，`Player::Impl` 是实现部分，`impl_` 是桥。只是实现部分只有一种，没有「两个维度」可言。它的目的也不同：

- 桥接模式是为了让**两边各自变化**，避免 M × N；
- Pimpl 是为了**隔离编译依赖**：`Impl` 的成员怎么改，`player.h` 都不变，包含它的文件都不用重新编译；库的二进制接口（ABI）也保持稳定，`sizeof(Player)` 永远是一个指针的大小。

《SOLID 原则（上）》里提到过「改一个头文件，所有包含它的文件都要重新编译」，Pimpl 就是专门解决这个问题的。第 5 章（C++ 惯用法）会专门讲 Pimpl：重新编译时间的实测、为什么析构函数必须写在 `.cpp` 里、拷贝和移动怎么处理。

## 5. 桥接和适配器、策略、抽象工厂

| 模式 | 结构 | 解决什么问题 | 什么时候用上 |
| --- | --- | --- | --- |
| **桥接** | 抽象持有实现接口的指针，两边各自有子类 | 两个维度独立变化，避免 M × N 个类 | 设计之初就把两个维度分开 |
| 适配器 | 包一个已有的类，转换它的接口 | 接口不兼容的类一起工作 | 事后补救：两边都已经存在、不能改 |
| 策略 | 上下文持有策略接口的指针，可以替换 | 一个算法有多种实现，可以互换 | 一个「可替换的行为」 |
| 抽象工厂 | 一个工厂创建一整套配套的产品 | 保证产品成套使用 | 创建对象时 |

- **桥接和适配器**：两者都是「一个对象通过另一个对象完成工作」，但时机不同。适配器是事后补救，被适配的类早就写好了，接口对不上；桥接是事先设计，`Renderer` 接口本来就是为视图准备的。两者也经常一起出现：`SdlRenderer` 要把 `Renderer` 的调用翻译成 SDL 的 C 函数，它本身就是一个适配器。
- **桥接和策略**：类图几乎一模一样，都是「持有一个接口的指针，运行时可以换」。区别在意图：策略替换的是**一个算法**（比如缩放用双线性还是 Lanczos），持有它的上下文通常只有一个类；桥接的实现部分是一整套**基本操作**（相当于一个平台），而抽象那一边本身也是一个继承体系，是第二个变化的维度。一个简单的判断：如果持有指针的那一边没有子类，它更像策略。
- **桥接和抽象工厂**：它们解决的问题不同，但经常配合：桥接只规定了视图通过 `Renderer` 去画，**用哪个** `Renderer` 要有人决定。本文的 `makeRenderer` 是一个简单工厂；如果解码器、渲染器、滤镜必须成套使用，就用《抽象工厂》里的 `MediaBackend` 创建一整套，再把其中的渲染器交给视图。

## 6. 小结

- **桥接模式**：把抽象部分和实现部分分成两个继承体系，抽象持有一个指向实现接口的指针（桥），两边可以各自扩展
- 适用场景：两个独立变化的维度。用继承会得到 M × N 个类，用桥接是 M + N 个
- 实测：3 种视图 × 3 种后端，继承版 16 个类、207 行，桥接版 8 个类、121 行；加一种视图，继承版新增 45 行，桥接版 10 行；加一种后端，继承版新增 58 行，桥接版 12 行
- 实测：运行时换后端，继承版只能换一个新对象，画中画的位置丢了；桥接版只换一个成员，状态都在
- 实现接口只放粗粒度的基本操作，不要出现抽象那一边的子类名字；属于实现的资源（纹理）换后端时要重建，属于抽象的状态不受影响
- SDL 的渲染器就是一座桥：同一段代码在 metal、opengl、gpu、software 四个驱动上画出完全一样的像素，但纹理不能跨驱动使用
- 实测：桥接和继承一样快（同一编译器下相差不到 1 ns/帧）；虚函数每帧多花不到 20 ns，可以忽略；逐像素的虚调用在 1080p 下每帧约 1.3 ms，不能忽略
- 编译期的桥接（模板）没有虚函数开销，但不能在运行时换后端；Pimpl 是只有一种实现的桥接，目的是隔离编译依赖
- 和适配器比，桥接是事先设计；和策略比，桥接的两边都是继承体系；和抽象工厂经常一起用，由工厂决定用哪个实现

## 7. 练习

1. 下面的设计分别是桥接、适配器、策略还是抽象工厂？
   ① 播放器的音频输出接口 `AudioOutput` 有 CoreAudio、ALSA、WASAPI 三种实现；播放器有 `MusicPlayer`（无缝衔接下一首）和 `PodcastPlayer`（倍速、跳过静音）两种，都通过一个 `AudioOutput` 指针出声；
   ② 把字幕渲染库 libass 的 C 接口包装成项目里的 `SubtitleRenderer` 接口；
   ③ 画面缩放可以在设置里选双线性、双三次或 Lanczos，`Scaler` 只有一个类，持有一个 `ScaleAlgorithm` 指针；
   ④ 按运行平台一次创建配套的解码器、渲染器和滤镜。

2. 要给所有视图加一个「截图」功能 `snapshot(t)`，返回画好的画面。需要改哪些类？软件后端和 SDL 后端各自怎么实现？

3. 真实的播放器里，同一个窗口中的多个视图共用一个 GPU 设备，不会每个视图各建一个。把 `unique_ptr<Renderer>` 改成 `shared_ptr<Renderer>`，写一个 `PlayerWindow` 管理窗口里的所有视图，要求：换后端时所有视图一起换；旧的设备在最后一个视图放开它时自动释放。

4. 有人这样设计渲染后端的接口，问题在哪？加一种视图「多画面」时要改几处？

   <!-- run: off -->
   ```cpp
   class Renderer {
   public:
       virtual ~Renderer() = default;
       virtual void drawMainWindow(double t) = 0;
       virtual void drawThumbnail(double t) = 0;
       virtual void drawPictureInPicture(int x, int y, double t) = 0;
   };
   ```

<details>
<summary>点击查看答案</summary>

1. ① 桥接：两个维度（播放器的种类、音频输出平台）各自有一个继承体系，用一个指针连起来。
   ② 适配器：libass 是现成的 C 库，事后把它的接口转换成项目需要的接口。
   ③ 策略：只有一个维度在变（缩放算法），`Scaler` 没有子类。
   ④ 抽象工厂：一次创建一整套配套的产品。

2. 「把画面读回来」是一个新的基本操作，要加进实现接口 `Renderer`，**每个后端都要实现**：软件后端的画面本来就在内存里，直接返回；GPU 后端要把显存里的画面拷回来（SDL 里是 `SDL_RenderReadPixels`）。视图这边只在基类 `VideoView` 里加一个函数，**所有视图就都有了**：

   ```cpp
   // 《桥接模式》练习 2：给所有视图加「截图」。视图这边只改基类，后端这边每个都要实现「把画面读回来」
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   struct Rect { int x, y, w, h; };
   using Image = std::vector<std::string>;

   class Renderer {
   public:
       virtual ~Renderer() = default;
       virtual void begin(int w, int h) = 0;
       virtual void fill(Rect r, char c) = 0;
       virtual void present() = 0;
       virtual Image readback() = 0;                     // 新的基本操作：把刚画好的这一帧读回内存
   };

   class SoftRenderer : public Renderer {
   public:
       void begin(int w, int h) override { rows_.assign(h, std::string(w, ' ')); }
       void fill(Rect r, char c) override {
           for (int y = r.y; y < r.y + r.h; ++y) rows_[y].replace(r.x, r.w, std::string(r.w, c));
       }
       void present() override {}
       Image readback() override { return rows_; }       // 画面本来就在内存里
   private:
       Image rows_;
   };

   class SdlRenderer : public Renderer {
   public:
       void begin(int w, int h) override { w_ = w; h_ = h; }
       void fill(Rect, char) override {}
       void present() override {}
       Image readback() override {                       // 真实代码：SDL_RenderReadPixels 把 GPU 上的画面拷回来
           std::printf("  SDL_RenderReadPixels(%d×%d)\n", w_, h_);
           return Image(h_, std::string(w_, '?'));
       }
   private:
       int w_ = 0, h_ = 0;
   };

   class VideoView {
   public:
       explicit VideoView(std::unique_ptr<Renderer> r) : r_(std::move(r)) {}
       virtual ~VideoView() = default;
       virtual void draw(double t) = 0;
       Image snapshot(double t) { draw(t); return r_->readback(); }   // 只在基类里写一次，所有视图都有了
   protected:
       Renderer& r() { return *r_; }
   private:
       std::unique_ptr<Renderer> r_;
   };

   class Thumbnail : public VideoView {
   public:
       using VideoView::VideoView;
       void draw(double t) override {
           r().begin(8, 2);
           r().fill({0, 0, int(8 * t / 10), 2}, '#');
           r().present();
       }
   };

   int main() {
       Thumbnail soft(std::make_unique<SoftRenderer>()), sdl(std::make_unique<SdlRenderer>());
       for (auto* v : {&soft, &sdl})
           for (const auto& row : v->snapshot(5)) std::printf("  [%s]\n", row.c_str());
   }
   ```

   ```text
     [####    ]
     [####    ]
     SDL_RenderReadPixels(8×2)
     [????????]
     [????????]
   ```

   可以看出桥接两个方向的代价不同：加一种视图、加一种后端都只加一个类；但给**实现接口**加一个基本操作，要改所有的后端（N 处），和《抽象工厂》里「加一种新产品要改所有工厂」是同一类问题。所以实现接口要在一开始就想清楚，尽量稳定。

3. 视图改成持有 `shared_ptr`；窗口持有所有视图，换后端时逐个 `setRenderer`。旧设备的引用计数在最后一个视图换走时归零，自动释放：

   ```cpp
   // 《桥接模式》练习 3：同一个窗口里的视图共用一个后端（一个 GPU 设备），换后端时一起换
   #include <cstdio>
   #include <memory>
   #include <string>
   #include <vector>

   class Renderer {
   public:
       virtual ~Renderer() = default;
       virtual std::string name() const = 0;
       virtual void frame(const std::string& who, double t) = 0;
   };

   class MetalRenderer : public Renderer {
   public:
       MetalRenderer() { std::printf("  创建 Metal 设备\n"); }
       ~MetalRenderer() override { std::printf("  释放 Metal 设备\n"); }
       std::string name() const override { return "Metal"; }
       void frame(const std::string& who, double t) override { std::printf("  Metal 画 %s @%.1f\n", who.c_str(), t); }
   };

   class SoftRenderer : public Renderer {
   public:
       SoftRenderer() { std::printf("  创建软件画布\n"); }
       ~SoftRenderer() override { std::printf("  释放软件画布\n"); }
       std::string name() const override { return "软件"; }
       void frame(const std::string& who, double t) override { std::printf("  软件 画 %s @%.1f\n", who.c_str(), t); }
   };

   class VideoView {
   public:
       VideoView(std::string name, std::shared_ptr<Renderer> r) : name_(std::move(name)), r_(std::move(r)) {}
       void setRenderer(std::shared_ptr<Renderer> r) { r_ = std::move(r); }
       void draw(double t) { r_->frame(name_, t); }
   private:
       std::string name_;
       std::shared_ptr<Renderer> r_;                     // 多个视图共用一个后端
   };

   class PlayerWindow {                                  // 窗口拥有所有视图，负责「一起换」
   public:
       explicit PlayerWindow(std::shared_ptr<Renderer> r) : r_(r) {
           views_.emplace_back("主窗口", r);
           views_.emplace_back("缩略图", r);
       }
       void switchRenderer(std::shared_ptr<Renderer> r) {
           r_ = r;
           for (auto& v : views_) v.setRenderer(r);      // 最后一个引用放开时，旧设备自动释放
       }
       void draw(double t) { for (auto& v : views_) v.draw(t); }
       long users() const { return r_.use_count(); }
   private:
       std::shared_ptr<Renderer> r_;
       std::vector<VideoView> views_;
   };

   int main() {
       PlayerWindow w(std::make_shared<MetalRenderer>());
       std::printf("后端被 %ld 处引用\n", w.users());
       w.draw(1.0);
       std::printf("-- 换成软件渲染 --\n");
       w.switchRenderer(std::make_shared<SoftRenderer>());
       w.draw(1.1);
   }
   ```

   ```text
     创建 Metal 设备
   后端被 3 处引用
     Metal 画 主窗口 @1.0
     Metal 画 缩略图 @1.0
   -- 换成软件渲染 --
     创建软件画布
     释放 Metal 设备
     软件 画 主窗口 @1.1
     软件 画 缩略图 @1.1
     释放软件画布
   ```

   引用计数是 3：窗口自己一份，两个视图各一份。「释放 Metal 设备」出现在新的软件画布创建之后、两个视图都换完之后。GoF 在桥接模式里也提到过这种做法：多个抽象对象可以共享同一个实现对象。注意一个前提：Metal 设备上创建的纹理等资源不能带到新的后端上（第 3.2 节），真实代码里换后端之后要重新上传。

4. 实现接口里出现了抽象那一边的子类名字（主窗口、缩略图、画中画），两个维度又绑在了一起。加「多画面」要在 `Renderer` 里加一个 `drawMultiView`，再在每个后端里实现它：3 个后端就是 1 + 3 = 4 处，以后每加一种视图都是 N + 1 处；同时每个后端里都有一份「主窗口怎么排版」的代码，排版改一次要改 N 处。这和继承版的 M × N 是一回事，只是从「类」换成了「函数」。改法就是本文的做法：`Renderer` 只提供 `fill`、`frame`、`text` 这样的基本操作，排版留在视图里。

</details>
