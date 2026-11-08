---
title: 抽象工厂
date: 2026-11-08 16:00:00
updated: 2026-11-08 16:00:00
categories:
  - 设计模式
  - 创建型模式
tags:
  - 设计模式
  - 抽象工厂
  - 硬件解码
  - 模板
order: 3
description: 有些对象必须成套使用：硬件解码出来的帧留在 GPU 上，只有同一族的渲染器和滤镜才能处理。FFmpeg 实测：VideoToolbox 硬解接 CPU 的 scale 滤镜直接报错，换成同族的 scale_vt 就正常。解码器和渲染器分别配置时，9 种组合只有 3 种能用；抽象工厂让一个工厂负责创建一整套产品，按运行环境选一套，就不可能混用。再用模板把产品族搬到编译期，混用两族的产品直接编译失败。最后说明抽象工厂的弱点：加一种新产品，要改所有工厂。
top_group_list: true
disableNunjucks: true
viz: [dp-abfactory]
---

> **本文目标**：理解「产品族」的概念，知道什么时候需要抽象工厂；会写运行时选择的抽象工厂，以及用模板实现的编译期产品族；分清抽象工厂和工厂方法；知道抽象工厂在「加新产品」方向上的代价。
>
> **前置知识**：上一篇《简单工厂与工厂方法》；最好读过音视频专栏的《硬件解码》，不读也不影响理解。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/08-abstract-factory/` 目录。

## 1. 问题：有些对象必须成套使用

### 1.1 一个真实的例子：硬件解码

解码视频有两种方式：用 CPU 软解，或者交给 GPU 里专门的硬件解码单元（macOS 上是 VideoToolbox，Linux 上常见的是 VA-API）。用 `ffmpeg -benchmark` 解码专栏的测试视频 `bbb-10s.mp4`（720p H.264，240 帧），各运行 3 次：

<figure class="diagram">
<svg viewBox="0 0 720 170" role="img" aria-label="解码 10 秒的 720p 测试视频：软解 CPU 时间 0.43 秒、总耗时 0.08 秒；硬解 CPU 时间 0.018 到 0.027 秒、总耗时 0.27 到 0.28 秒"><g style="font-family:var(--mono)"><text x="10.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">CPU 时间（用户态，秒）</text><text x="380.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">总耗时（秒）</text><text x="160.0" y="51.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">软解</text><rect x="168.0" y="36.0" width="172.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="346.0" y="51.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.43</text><rect x="380.0" y="36.0" width="64.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="450.0" y="51.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.08</text><text x="160.0" y="89.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">硬解，帧下载回内存</text><rect x="168.0" y="74.0" width="10.8" height="20.0" rx="2" style="fill:var(--link)"/><text x="184.8" y="89.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.027</text><rect x="380.0" y="74.0" width="224.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="610.0" y="89.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.28</text><text x="160.0" y="127.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">硬解，帧留在 GPU</text><rect x="168.0" y="112.0" width="7.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="181.2" y="127.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.018</text><rect x="380.0" y="112.0" width="216.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="602.0" y="127.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.27</text><text x="10.0" y="160.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">ffmpeg -benchmark 解码 bbb-10s.mp4（720p H.264，240 帧），各 3 次取中间值；Apple M6</text></g></svg>
</figure>

| 解码方式 | CPU 时间（用户态） | 总耗时 |
| --- | --- | --- |
| 软解 | 0.42–0.43 s | 0.08 s |
| 硬解，帧下载回内存 | 0.027 s | 0.28 s |
| 硬解，帧留在 GPU 上 | 0.016–0.019 s | 0.26–0.28 s |

这段视频只有 10 秒，硬件解码器的初始化开销占了大头，这台机器的 CPU 解 720p 又很快，所以硬解的总耗时反而更长。但硬解占用的 CPU 时间只有软解的 1/16 到 1/25：播放长视频时，这意味着 CPU 几乎空闲、风扇不转、电池更耐用。所以播放器都会优先使用硬件解码。

问题在于：硬解出来的帧**留在 GPU 的显存里**（FFmpeg 里的像素格式是 `videotoolbox_vld`），不是普通的内存。后面处理这些帧的组件，也必须是「懂 GPU 帧」的那一套。在本机用 FFmpeg 9.0 实测：

<!-- run: off -->
```bash
# 硬解，帧留在 GPU 上，接一个普通的 CPU 缩放滤镜
ffmpeg -hwaccel videotoolbox -hwaccel_output_format videotoolbox_vld -i bbb-10s.mp4 -vf scale=640:360 -f null -
```

```text
Impossible to convert between the formats supported by the filter 'graph -1 input from stream 0:0' and the filter 'auto_scale_0'
Link 'graph -1 input from stream 0:0.default' -> 'auto_scale_0.default':
  Pixel formats:
    src: videotoolbox_vld
    dst: yuv420p yuyv422 rgb24 bgr24 yuv422p ……（全是内存里的像素格式）
```

FFmpeg 直接报错退出（退出码 178）。把滤镜换成同一族的 `scale_vt`（在 GPU 上缩放 VideoToolbox 的帧），或者先用 `hwdownload` 把帧下载回内存再交给 `scale`，都能正常运行：

<figure class="diagram">
<svg viewBox="0 0 740 230" role="img" aria-label="FFmpeg 实测：VideoToolbox 硬解的帧留在 GPU 上，接 CPU 的 scale 滤镜报错；接同族的 scale_vt，或者先用 hwdownload 下载回内存再接 scale，都能正常运行"><g style="font-family:var(--mono)"><rect x="10.0" y="20.0" width="170.0" height="40.0" rx="4" style="fill:var(--accent-soft);stroke:var(--link);stroke-width:1.4"/><text x="95.0" y="37.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">VideoToolbox 硬解</text><text x="95.0" y="53.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">帧格式 videotoolbox_vld</text><path d="M180,40 L212,40" style="fill:none;stroke:var(--danger);stroke-width:1.5"/><path d="M211,35 L220,40 L211,45 z" style="fill:var(--danger)"/><rect x="220.0" y="20.0" width="120.0" height="40.0" rx="4" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.6"/><text x="280.0" y="37.0" text-anchor="middle" style="fill:var(--danger);font-size:12px;font-weight:700">scale</text><text x="280.0" y="53.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">处理内存里的帧</text><text x="355.0" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px">✗ Impossible to convert between the formats</text><rect x="10.0" y="90.0" width="170.0" height="40.0" rx="4" style="fill:var(--accent-soft);stroke:var(--link);stroke-width:1.4"/><text x="95.0" y="107.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">VideoToolbox 硬解</text><text x="95.0" y="123.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">帧格式 videotoolbox_vld</text><path d="M180,110 L212,110" style="fill:none;stroke:var(--accent);stroke-width:1.5"/><path d="M211,105 L220,110 L211,115 z" style="fill:var(--accent)"/><rect x="220.0" y="90.0" width="120.0" height="40.0" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.6"/><text x="280.0" y="107.0" text-anchor="middle" style="fill:var(--accent);font-size:12px;font-weight:700">scale_vt</text><text x="280.0" y="123.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">处理 GPU 上的帧</text><text x="355.0" y="115.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px">✓ 同一族的 GPU 缩放</text><rect x="10.0" y="160.0" width="170.0" height="40.0" rx="4" style="fill:var(--accent-soft);stroke:var(--link);stroke-width:1.4"/><text x="95.0" y="177.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">VideoToolbox 硬解</text><text x="95.0" y="193.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">帧格式 videotoolbox_vld</text><path d="M180,180 L212,180" style="fill:none;stroke:var(--fg-2);stroke-width:1.5"/><path d="M211,175 L220,180 L211,185 z" style="fill:var(--fg-2)"/><rect x="220.0" y="160.0" width="130.0" height="40.0" rx="4" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.4"/><text x="285.0" y="177.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">hwdownload</text><text x="285.0" y="193.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">GPU → 内存（nv12）</text><path d="M350,180 L382,180" style="fill:none;stroke:var(--fg-2);stroke-width:1.5"/><path d="M381,175 L390,180 L381,185 z" style="fill:var(--fg-2)"/><rect x="390.0" y="160.0" width="120.0" height="40.0" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.6"/><text x="450.0" y="177.0" text-anchor="middle" style="fill:var(--accent);font-size:12px;font-weight:700">scale</text><text x="450.0" y="193.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">处理内存里的帧</text><text x="525.0" y="185.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px">✓ 先下载回内存，再交给 CPU 滤镜</text><text x="10.0" y="222.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">实测：ffmpeg -hwaccel videotoolbox -hwaccel_output_format videotoolbox_vld … -vf …（FFmpeg 9.0）</text></g></svg>
</figure>

解码器、滤镜、渲染器，在这里组成了一个**产品族**：VideoToolbox 一族、VA-API 一族、软件一族。同一族的产品可以互相配合，不同族的产品混在一起就不能工作。

### 1.2 分别创建：9 种组合只有 3 种能用

如果解码器和渲染器各用一个上一篇的简单工厂，按两个独立的配置项创建，就很容易混用：

```cpp
#include <iostream>
#include <memory>
#include <string>

enum class Memory { Cpu, VideoToolbox, Vaapi };     // 解码出来的帧放在哪里
const char* where(Memory m) { return m == Memory::Cpu ? "内存" : m == Memory::VideoToolbox ? "GPU（VideoToolbox）" : "GPU（VA-API）"; }

struct Frame { Memory memory; int index; };

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual Frame decode(int i) = 0;
};
class SoftwareDecoder : public Decoder { public: Frame decode(int i) override { return {Memory::Cpu, i}; } };
class VideoToolboxDecoder : public Decoder { public: Frame decode(int i) override { return {Memory::VideoToolbox, i}; } };   // macOS 硬解
class VaapiDecoder : public Decoder { public: Frame decode(int i) override { return {Memory::Vaapi, i}; } };                 // Linux 硬解

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual Memory accepts() const = 0;               // 能直接画哪里的帧
    bool draw(const Frame& f) { return f.memory == accepts(); }
};
class SdlRenderer : public Renderer { public: Memory accepts() const override { return Memory::Cpu; } };
class MetalRenderer : public Renderer { public: Memory accepts() const override { return Memory::VideoToolbox; } };
class EglRenderer : public Renderer { public: Memory accepts() const override { return Memory::Vaapi; } };

// 没有抽象工厂：解码器和渲染器各自按一个配置项创建，谁也不知道对方是谁
std::unique_ptr<Decoder> makeDecoder(const std::string& name) {
    if (name == "videotoolbox") return std::make_unique<VideoToolboxDecoder>();
    if (name == "vaapi") return std::make_unique<VaapiDecoder>();
    return std::make_unique<SoftwareDecoder>();
}
std::unique_ptr<Renderer> makeRenderer(const std::string& name) {
    if (name == "metal") return std::make_unique<MetalRenderer>();
    if (name == "egl") return std::make_unique<EglRenderer>();
    return std::make_unique<SdlRenderer>();
}

int main() {
    int ok = 0;
    for (const char* dec : {"software", "videotoolbox", "vaapi"})
        for (const char* ren : {"sdl", "metal", "egl"}) {
            auto d = makeDecoder(dec);
            auto r = makeRenderer(ren);
            Frame f = d->decode(1);
            bool good = r->draw(f);
            ok += good;
            std::cout << "decoder=" << dec << " renderer=" << ren << "  "
                      << (good ? "✓" : std::string("✗ 帧在") + where(f.memory) + "，渲染器只能画" + where(r->accepts()) + "的帧") << '\n';
        }
    std::cout << "9 种组合里能用的：" << ok << " 种\n";
}
```

```text
decoder=software renderer=sdl  ✓
decoder=software renderer=metal  ✗ 帧在内存，渲染器只能画GPU（VideoToolbox）的帧
decoder=software renderer=egl  ✗ 帧在内存，渲染器只能画GPU（VA-API）的帧
decoder=videotoolbox renderer=sdl  ✗ 帧在GPU（VideoToolbox），渲染器只能画内存的帧
decoder=videotoolbox renderer=metal  ✓
decoder=videotoolbox renderer=egl  ✗ 帧在GPU（VideoToolbox），渲染器只能画GPU（VA-API）的帧
decoder=vaapi renderer=sdl  ✗ 帧在GPU（VA-API），渲染器只能画内存的帧
decoder=vaapi renderer=metal  ✗ 帧在GPU（VA-API），渲染器只能画GPU（VideoToolbox）的帧
decoder=vaapi renderer=egl  ✓
9 种组合里能用的：3 种
```

两个工厂各自都没有错，错在**它们互相不知道对方选了什么**。用户为了省电把 `decoder` 改成 `videotoolbox`，却忘了 `renderer` 还是默认的 `sdl`，播放器就黑屏了。产品种类越多（再加上滤镜、音频输出……），能用的组合占的比例越小。

## 2. 抽象工厂

### 2.1 意图

**抽象工厂模式**（Abstract Factory）：提供一个接口，用来创建一系列相关或相互依赖的对象，而不必指定它们的具体类。

做法是：不再为每种产品各设一个工厂，而是定义一个**能创建整族产品**的工厂接口，每个产品族实现一个具体工厂。程序在启动时选定一个工厂，之后所有产品都向它要。只要产品出自同一个工厂，就一定是配套的：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <vector>

enum class Memory { Cpu, VideoToolbox, Vaapi };
struct Frame { Memory memory; int index; };

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual Frame decode(int i) = 0;
};
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual bool draw(const Frame& f) = 0;
};

// ---------- 抽象工厂：规定「一套后端」要提供哪些产品 ----------
struct Env { bool hasVideoToolbox; bool hasVaapi; };   // 运行环境：有哪些硬件加速可用

class MediaBackend {
public:
    virtual ~MediaBackend() = default;
    virtual std::string name() const = 0;
    virtual bool available(const Env& env) const = 0;
    virtual std::unique_ptr<Decoder> createDecoder() const = 0;
    virtual std::unique_ptr<Renderer> createRenderer() const = 0;
};

// ---------- 三套具体的后端，每套内部的产品是配套的 ----------
class SoftwareBackend : public MediaBackend {
    struct Dec : Decoder { Frame decode(int i) override { return {Memory::Cpu, i}; } };
    struct Ren : Renderer { bool draw(const Frame& f) override { return f.memory == Memory::Cpu; } };
public:
    std::string name() const override { return "软件（FFmpeg 软解 + SDL）"; }
    bool available(const Env&) const override { return true; }                 // 软件后端哪里都能用
    std::unique_ptr<Decoder> createDecoder() const override { return std::make_unique<Dec>(); }
    std::unique_ptr<Renderer> createRenderer() const override { return std::make_unique<Ren>(); }
};

class VideoToolboxBackend : public MediaBackend {
    struct Dec : Decoder { Frame decode(int i) override { return {Memory::VideoToolbox, i}; } };
    struct Ren : Renderer { bool draw(const Frame& f) override { return f.memory == Memory::VideoToolbox; } };
public:
    std::string name() const override { return "VideoToolbox 硬解 + Metal"; }
    bool available(const Env& e) const override { return e.hasVideoToolbox; }
    std::unique_ptr<Decoder> createDecoder() const override { return std::make_unique<Dec>(); }
    std::unique_ptr<Renderer> createRenderer() const override { return std::make_unique<Ren>(); }
};

class VaapiBackend : public MediaBackend {
    struct Dec : Decoder { Frame decode(int i) override { return {Memory::Vaapi, i}; } };
    struct Ren : Renderer { bool draw(const Frame& f) override { return f.memory == Memory::Vaapi; } };
public:
    std::string name() const override { return "VA-API 硬解 + EGL"; }
    bool available(const Env& e) const override { return e.hasVaapi; }
    std::unique_ptr<Decoder> createDecoder() const override { return std::make_unique<Dec>(); }
    std::unique_ptr<Renderer> createRenderer() const override { return std::make_unique<Ren>(); }
};

// 按优先级挑第一套可用的后端：硬件优先，软件兜底
const MediaBackend& chooseBackend(const Env& env) {
    static const VideoToolboxBackend vt;
    static const VaapiBackend vaapi;
    static const SoftwareBackend sw;
    for (const MediaBackend* b : std::vector<const MediaBackend*>{&vt, &vaapi, &sw})
        if (b->available(env)) return *b;
    return sw;
}

// 播放器只认识抽象工厂和抽象产品；两个产品出自同一个工厂，一定配套
void play(const MediaBackend& backend, int frames) {
    auto decoder = backend.createDecoder();
    auto renderer = backend.createRenderer();
    int drawn = 0;
    for (int i = 1; i <= frames; ++i) drawn += renderer->draw(decoder->decode(i));
    std::cout << "  后端：" << backend.name() << "，画出 " << drawn << " / " << frames << " 帧\n";
}

int main() {
    struct { const char* desc; Env env; } machines[] = {
        {"MacBook", {true, false}},
        {"带 Intel 核显的 Linux 台式机", {false, true}},
        {"没有 GPU 的 Linux 服务器", {false, false}},
    };
    for (const auto& m : machines) {
        std::cout << m.desc << '\n';
        play(chooseBackend(m.env), 240);
    }
}
```

```text
MacBook
  后端：VideoToolbox 硬解 + Metal，画出 240 / 240 帧
带 Intel 核显的 Linux 台式机
  后端：VA-API 硬解 + EGL，画出 240 / 240 帧
没有 GPU 的 Linux 服务器
  后端：软件（FFmpeg 软解 + SDL），画出 240 / 240 帧
```

```uml
label: 抽象工厂的结构：MediaBackend 接口声明 createDecoder 和 createRenderer；SoftwareBackend 和 VideoToolboxBackend 各自创建同一族的解码器和渲染器；播放器只依赖三个接口
classes:
  - { id: Client, name: 'play()', x: 20, y: 30, w: 100 }
  - { id: F, name: MediaBackend, stereo: interface, x: 200, y: 16, w: 250, ops: ['*+ available(env) : bool', '*+ createDecoder() : unique_ptr<Decoder>', '*+ createRenderer() : unique_ptr<Renderer>'] }
  - { id: SW, name: SoftwareBackend, x: 120, y: 200, w: 160 }
  - { id: VT, name: VideoToolboxBackend, x: 330, y: 200, w: 170 }
  - { id: D, name: Decoder, stereo: interface, x: 560, y: 16, w: 160 }
  - { id: R, name: Renderer, stereo: interface, x: 560, y: 110, w: 160 }
  - { id: SWP, name: '软解 + SDL', x: 120, y: 290, w: 160 }
  - { id: VTP, name: 'VideoToolbox + Metal', x: 330, y: 290, w: 170 }
rels:
  - { from: Client, to: F, type: depend, fdx: -3 }
  - { from: SW, to: F, type: realize, mid: 170 }
  - { from: VT, to: F, type: realize, mid: 170 }
  - { from: SW, to: SWP, type: depend, label: '创建' }
  - { from: VT, to: VTP, type: depend, label: '创建' }
  - { from: F, to: D, type: depend, fdx: -20 }
  - { from: F, to: R, type: depend, points: [[450, 90], [505, 90], [505, 136], [560, 136]] }
texts:
  - { x: 520, y: 210, text: '每个具体工厂只创建', muted: true }
  - { x: 520, y: 228, text: '自己这一族的产品', muted: true }
```

下面的演示对比了两种做法：分别配置时，解码器和渲染器可能来自不同的族；用抽象工厂时，程序按运行环境依次检查哪个后端可用，选中一族以后，两个产品都出自它：

<div class="dsviz" data-viz="dp-abfactory"></div>

几个要点：

- **选择工厂的逻辑只出现一次**（这里是 `chooseBackend`），通常在程序启动时、在 `main` 附近完成。其余代码只拿到一个 `const MediaBackend&`。这正是第 1 章说的组合根和依赖注入。
- **按优先级探测、软件兜底**是这类代码的常见写法：硬件不可用（驱动没装、显卡不支持这种编码）时，自动退回到哪里都能用的软件后端。
- 每个具体工厂通常整个程序只需要一个，用上一篇的函数内静态变量就够了，不必做成单例类。

### 2.2 和工厂方法的区别

两个模式的名字很像，区别在于：

| | 工厂方法 | 抽象工厂 |
| --- | --- | --- |
| 创建什么 | **一种**产品 | **一族**相关的产品 |
| 怎么扩展 | 继承创建者，重写一个虚函数 | 实现一个新的工厂类，提供整族产品 |
| 关注点 | 框架的流程里，某一步用哪种对象 | 多种对象之间的一致性 |

两者也经常一起出现：抽象工厂接口里的每一个 `createXxx()`，本身就是一个工厂方法。

## 3. 编译期的产品族

上面的抽象工厂在运行时选择后端，产品之间是否配套，靠的是「它们出自同一个工厂」这个约定。帧本身的类型还是同一个 `Frame`，如果有人绕过工厂、手动拼了一个错误的组合，编译器发现不了。

如果产品族在编译期就能确定，C++ 可以把这个约定交给类型系统：每个族是一个类型，族里的解码器、渲染器、帧都是它的**成员类型**，不同族的帧是不同的类型：

```cpp
#include <iostream>

// ---------- 软件族：帧在内存里 ----------
struct SoftwareFamily {
    struct Frame { int index; const unsigned char* pixels; };
    struct Decoder { Frame decode(int i) { return {i, nullptr}; } };
    struct Renderer { void draw(const Frame& f) { std::cout << "  SDL 画第 " << f.index << " 帧（内存）\n"; } };
};

// ---------- VideoToolbox 族：帧在 GPU 上 ----------
struct VideoToolboxFamily {
    struct Frame { int index; void* pixelBuffer; };    // 真实代码里是 CVPixelBufferRef
    struct Decoder { Frame decode(int i) { return {i, nullptr}; } };
    struct Renderer { void draw(const Frame& f) { std::cout << "  Metal 画第 " << f.index << " 帧（GPU）\n"; } };
};

// 播放器按「族」实例化：解码器和渲染器只能来自同一个族
template <typename Family>
void play(int frames) {
    typename Family::Decoder decoder;
    typename Family::Renderer renderer;
    for (int i = 1; i <= frames; ++i) renderer.draw(decoder.decode(i));
}

int main() {
    std::cout << "play<SoftwareFamily>\n";
    play<SoftwareFamily>(2);
    std::cout << "play<VideoToolboxFamily>\n";
    play<VideoToolboxFamily>(2);
}
```

```text
play<SoftwareFamily>
  SDL 画第 1 帧（内存）
  SDL 画第 2 帧（内存）
play<VideoToolboxFamily>
  Metal 画第 1 帧（GPU）
  Metal 画第 2 帧（GPU）
```

这时如果把两个族的产品混在一起用（`family_mix_ce.cpp`）：

<!-- run: off -->
```cpp
VideoToolboxFamily::Decoder decoder;   // 硬解
SoftwareFamily::Renderer renderer;     // 却用了软件的渲染器
renderer.draw(decoder.decode(1));
```

就是编译错误。GCC 13 的报错直接写明了是哪两个族的帧不匹配：

```text
error: cannot convert 'VideoToolboxFamily::Frame' to 'const SoftwareFamily::Frame&'
```

Clang 21 的报错是 `no viable conversion from 'Frame' to 'const Frame'`，信息少一些，但同样拦住了错误。

编译期产品族的代价和第 1 章讲过的模板多态一样：不能在运行时切换（`play<VideoToolboxFamily>` 和 `play<SoftwareFamily>` 是两份不同的代码），实现要放在头文件里。两者也可以结合：运行时探测出可用的后端，再用一个 `switch` 分派到对应的模板实例（练习 4）。

## 4. 抽象工厂的弱点：加一种新产品

抽象工厂让「加一个产品族」变得很容易：新写一个工厂类，实现所有的 `createXxx()`，已有的代码都不用改。但「加一种新产品」就麻烦了。比如播放器还需要音频输出：

| 要改的地方 | 加一个产品族（比如 Windows 的 D3D11） | 加一种产品（比如音频输出） |
| --- | --- | --- |
| 工厂接口 `MediaBackend` | 不用改 | 加一个 `createAudioOutput()` |
| 已有的具体工厂 | 不用改 | **每一个**都要实现 `createAudioOutput()` |
| 新增的代码 | 一个工厂类 + 它的几个产品 | 每个族各一个音频输出类 |

又是《为什么需要设计模式》里那张「格式 × 操作」的表：抽象工厂按**族**切分代码，加一行（新族）只需新增，加一列（新产品）就要改每一行。所以抽象工厂适合**产品种类稳定、产品族会增加**的场合。如果产品种类本身还在频繁变化，先别急着引入它。

现实中也常常需要在两个族之间「搭桥」：1.1 节的 `hwdownload` 就是一个把 GPU 帧转换成内存帧的组件，让 VideoToolbox 族的解码器可以接软件族的滤镜。这种「把一个族的接口转换成另一个族能用的接口」的组件，就是结构型模式那一章的**适配器**。

## 5. 现实中的抽象工厂

- **图形界面库**是抽象工厂最经典的应用：一个程序要在不同的平台上使用风格一致的控件。Qt 的平台抽象层（QPA）里，每个平台插件提供一个 `QPlatformIntegration`，由它创建这个平台的窗口（`createPlatformWindow()`）、绘图后端（`createPlatformBackingStore()`）、字体数据库等一整套平台相关的对象。
- **FFmpeg 的硬件加速框架**：每种硬件加速类型（`AVHWDeviceType`，比如 VideoToolbox、VA-API、CUDA）都有自己的设备上下文、帧格式和一组配套的滤镜（`scale_vt`、`scale_vaapi`、`scale_cuda`……），本质上就是一个个产品族。
- **测试**：给整个后端换一个「假的产品族」（假解码器 + 假渲染器），就能在没有 GPU 的持续集成机器上测试播放器的逻辑（练习 3）。

## 6. 小结

- 一组对象必须配套使用时，它们构成一个**产品族**。实测：VideoToolbox 硬解的帧留在 GPU 上，接 CPU 的 `scale` 滤镜报错，接同族的 `scale_vt` 正常
- 分别创建每种产品，容易拼出不配套的组合：3 种解码器 × 3 种渲染器，只有 3 种组合能用
- **抽象工厂**：一个接口负责创建整族产品，每个族一个具体工厂。程序启动时选一个工厂，之后的产品都出自它，一定配套
- 和工厂方法的区别：工厂方法创建一种产品，抽象工厂创建一族；抽象工厂里的每个创建函数通常就是一个工厂方法
- **编译期产品族**：每个族是一个类型，产品是成员类型，混用两族的产品直接编译失败
- 抽象工厂方便加新的产品族，不方便加新的产品种类（要改接口和所有工厂）；两个族之间的转换组件是适配器

## 7. 练习

1. 播放器要加上音频输出：软件族用 SDL 音频，VideoToolbox 族用 CoreAudio，VA-API 族用 PulseAudio。在 2.1 节的代码里，需要修改哪些类、新增哪些类？如果有人在第三方插件里实现了自己的 `MediaBackend`，这次修改对他有什么影响？

2. 在 Linux 上用 VA-API 硬解，并且让帧留在 GPU 上，再给视频叠加一个台标（`overlay` 滤镜）。参考 1.1 节，如果直接用 CPU 的 `overlay` 会怎样？有哪两种解决办法？

3. 写一个用于测试的产品族 `FakeBackend`：它的解码器每次返回一个编号递增的帧，渲染器把收到的帧编号记录下来。用它检查 `play(backend, 240)` 确实按顺序把 240 帧都交给了渲染器。

4. 编译期产品族不能在运行时切换。写一个函数 `void playWith(const std::string& name, int frames)`，根据运行时传入的名字（`"software"` 或 `"videotoolbox"`）调用对应的 `play<Family>(frames)`。

<details>
<summary>点击查看答案</summary>

1. 需要修改的：抽象工厂接口 `MediaBackend` 加一个纯虚函数 `createAudioOutput()`；三个具体工厂 `SoftwareBackend`、`VideoToolboxBackend`、`VaapiBackend` 都要实现它；`play()` 里多创建一个音频输出。需要新增的：音频输出的接口 `AudioOutput`，以及 `SdlAudio`、`CoreAudioOutput`、`PulseAudioOutput` 三个实现。
   对第三方插件来说，这是一次不兼容的修改：`MediaBackend` 多了一个纯虚函数，他们的工厂类不实现它就编译不过（如果是已经编译好的动态库，甚至会在运行时出错）。这就是第 4 节说的「加新产品要改所有工厂」。缓解的办法是给新函数提供一个默认实现（比如返回软件族的音频输出），但这样又要小心默认的产品和其他产品不配套。

2. 直接用 CPU 的 `overlay` 会和 1.1 节一样报错：VA-API 的帧（像素格式 `vaapi`）不能直接交给只接受内存帧的滤镜。两种办法：
   - 用同一族的滤镜 `overlay_vaapi`，叠加在 GPU 上完成，帧不用离开显存，最快；
   - 先用 `hwdownload` 把帧下载回内存，再用 CPU 的 `overlay`。简单通用，但每一帧都要从显存拷贝到内存，失去了硬解的大部分好处。

3. 参考代码：

   <!-- run: off -->
   ```cpp
   class FakeBackend : public MediaBackend {
   public:
       struct Dec : Decoder {
           int next = 0;
           Frame decode(int) override { return {Memory::Cpu, ++next}; }
       };
       struct Ren : Renderer {
           std::vector<int>* seen;
           explicit Ren(std::vector<int>* s) : seen(s) {}
           bool draw(const Frame& f) override { seen->push_back(f.index); return true; }
       };
       std::string name() const override { return "测试"; }
       bool available(const Env&) const override { return true; }
       std::unique_ptr<Decoder> createDecoder() const override { return std::make_unique<Dec>(); }
       std::unique_ptr<Renderer> createRenderer() const override { return std::make_unique<Ren>(&seen); }
       mutable std::vector<int> seen;                 // 渲染器收到的帧编号
   };

   FakeBackend fake;
   play(fake, 240);
   assert(fake.seen.size() == 240);
   for (int i = 0; i < 240; ++i) assert(fake.seen[i] == i + 1);
   ```

   测试不需要 GPU，也不需要视频文件，在任何持续集成机器上都能运行。

4. 用一个 `if` 或 `switch` 把运行时的值映射到编译期的类型，两个模板实例都会被编译进程序：

   <!-- run: off -->
   ```cpp
   void playWith(const std::string& name, int frames) {
       if (name == "videotoolbox") play<VideoToolboxFamily>(frames);
       else play<SoftwareFamily>(frames);              // 默认用软件族
   }
   ```

   分派只在入口处发生一次，进入 `play<Family>` 以后，解码器、渲染器和帧的类型都是确定的，混用依然是编译错误。族比较多时，可以把每个族的 `play<Family>` 放进一个 `std::map<std::string, void(*)(int)>` 里查表，这又是上一篇的注册表工厂。

</details>
