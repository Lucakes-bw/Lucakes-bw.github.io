---
title: 简单工厂与工厂方法
date: 2026-11-08 15:00:00
updated: 2026-11-08 15:00:00
categories:
  - 设计模式
  - 创建型模式
tags:
  - 设计模式
  - 工厂方法
  - 注册表
  - 音视频
order: 2
description: 播放器要根据文件格式创建不同的解析器，new 具体类的代码散落各处。简单工厂把创建集中到一个函数里；工厂方法让框架定好流程、由子类决定创建什么，顺便留出了测试的接缝；注册表工厂学 FFmpeg 的格式探测，每种格式登记一个打分函数，加格式只需加一个文件。用测试视频提取出的真实 MP4、H.264、AAC 文件实测：识别正确，帧数和 ffprobe 完全一致。但把解析器打包成静态库以后，自动注册全部失效，一种格式都认不出来。
top_group_list: true
disableNunjucks: true
viz: [dp-factory]
---

> **本文目标**：分清简单工厂、工厂方法和注册表工厂，知道各自适合什么场合；会写一个能自动注册的工厂，并避开静态初始化顺序和静态库丢失注册这两个坑；会用工厂方法给代码留出测试的接缝。
>
> **前置知识**：上一篇《单例模式》；第 1 章《SOLID 原则（上）》（开闭原则）；最好读过音视频专栏的《MP4 封装格式》《H.264 码流》《AAC 码流》，不读也不影响理解本文。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/07-factory/` 目录，多文件的注册表项目在 `registry/` 子目录，`run.sh` 会用 ffmpeg 从专栏的测试视频 `bbb-10s.mp4` 提取样本。

## 1. 问题：new 散落在各处

一个播放器要支持多种格式：MP4 文件、H.264 裸流、AAC 裸流……每种格式有自己的解析器类。最直接的写法，是在需要解析器的地方按格式 `new` 出对应的类：

<!-- run: off -->
```cpp
// 打开文件时
if (ext == "mp4") parser = new Mp4Parser;
else if (ext == "h264") parser = new H264Parser;
else if (ext == "aac") parser = new AacParser;

// 生成缩略图时，又写了一遍
if (ext == "mp4") parser = new Mp4Parser;
else if (ext == "h264") parser = new H264Parser;
// ……忘了 aac
```

这是第 1 章说过的散弹式修改：每加一种格式，就要找出所有这样的地方各改一遍；每个地方都依赖着所有具体的解析器类。**创建型模式**要解决的就是这个问题：把「创建哪个具体类」的知识集中到一个地方，让其余代码只依赖接口。

## 2. 简单工厂

最简单的办法，是把创建的代码收拢到一个函数里：

```cpp
#include <iostream>
#include <memory>
#include <string>

class Parser {
public:
    virtual ~Parser() = default;
    virtual std::string describe() const = 0;
};
class Mp4Parser : public Parser { public: std::string describe() const override { return "MP4：逐个读取 box"; } };
class H264Parser : public Parser { public: std::string describe() const override { return "H.264：按起始码切分 NAL"; } };
class AacParser : public Parser { public: std::string describe() const override { return "AAC：逐帧读取 ADTS 头"; } };

// 简单工厂：调用者只给出格式名，拿到一个 Parser 接口；具体创建哪个类，只有这里知道
std::unique_ptr<Parser> createParser(const std::string& format) {
    if (format == "mp4") return std::make_unique<Mp4Parser>();
    if (format == "h264") return std::make_unique<H264Parser>();
    if (format == "aac") return std::make_unique<AacParser>();
    return nullptr;
}

int main() {
    for (const char* name : {"mp4", "h264", "aac", "flv"}) {
        auto p = createParser(name);
        std::cout << name << " → " << (p ? p->describe() : "不支持的格式") << '\n';
    }
}
```

```text
mp4 → MP4：逐个读取 box
h264 → H.264：按起始码切分 NAL
aac → AAC：逐帧读取 ADTS 头
flv → 不支持的格式
```

```uml
label: 简单工厂：调用者只依赖 Parser 接口和 createParser 函数；只有 createParser 知道三个具体的解析器类
classes:
  - { id: Client, name: 调用者, x: 20, y: 30, w: 110 }
  - { id: F, name: 'createParser(format)', x: 200, y: 30, w: 200 }
  - { id: P, name: Parser, stereo: interface, x: 470, y: 16, w: 170, ops: ['*+ describe() : string'] }
  - { id: A, name: Mp4Parser, x: 380, y: 180, w: 110 }
  - { id: B, name: H264Parser, x: 500, y: 180, w: 110 }
  - { id: C, name: AacParser, x: 620, y: 180, w: 110 }
rels:
  - { from: Client, to: F, type: depend }
  - { from: F, to: A, type: depend, label: '创建', fdx: -30, mid: 135 }
  - { from: A, to: P, type: realize, mid: 145 }
  - { from: B, to: P, type: realize, mid: 145 }
  - { from: C, to: P, type: realize, mid: 145 }
```

这叫**简单工厂**（simple factory）。严格说它不在 GoF 的 23 种模式里，只是一个很常用的编程习惯。它的好处是：调用者只依赖 `Parser` 接口，具体类的名字只出现在 `createParser` 里；加一种格式时，只改这一个函数。不足之处是 `createParser` 本身仍然是一串 `if-else`，每加一种格式都要修改它，不符合开闭原则。但所有修改都集中在一处，格式种类不多、很少变化时，这完全够用。

## 3. 工厂方法

### 3.1 意图

**工厂方法模式**（Factory Method）：定义一个创建对象的接口，让子类决定实例化哪一个类。

它解决的问题和简单工厂不一样：不是「按名字选一个类」，而是**框架已经写好了流程，但流程里要用到的某个对象，框架自己不知道该创建哪一种**。下面的播放器，播放流程是固定的（创建解码器、逐帧解码），但用硬件解码还是软件解码，要由具体的产品决定：

```cpp
#include <iostream>
#include <memory>
#include <string>

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual std::string decode(int frame) = 0;
};

class SoftwareDecoder : public Decoder {
public:
    std::string decode(int frame) override { return "CPU 解码第 " + std::to_string(frame) + " 帧"; }
};
class HardwareDecoder : public Decoder {
public:
    std::string decode(int frame) override { return "GPU 解码第 " + std::to_string(frame) + " 帧"; }
};

// 创建者：播放流程是固定的，但它不知道该用哪种解码器
class Player {
public:
    virtual ~Player() = default;
    void play(int frames) {                        // 固定的流程
        auto decoder = createDecoder();            // ← 工厂方法：由子类决定创建什么
        for (int i = 1; i <= frames; ++i) std::cout << "  " << decoder->decode(i) << '\n';
    }

protected:
    virtual std::unique_ptr<Decoder> createDecoder() = 0;
};

class DesktopPlayer : public Player {              // 桌面版：用 GPU 硬解
protected:
    std::unique_ptr<Decoder> createDecoder() override { return std::make_unique<HardwareDecoder>(); }
};
class ServerPlayer : public Player {               // 服务器上没有 GPU：用软解
protected:
    std::unique_ptr<Decoder> createDecoder() override { return std::make_unique<SoftwareDecoder>(); }
};

// 测试：换一个返回假解码器的子类，就能测试播放流程本身，不需要真的解码
class FakeDecoder : public Decoder {
public:
    std::string decode(int frame) override { return "假帧 " + std::to_string(frame); }
};
class TestPlayer : public Player {
protected:
    std::unique_ptr<Decoder> createDecoder() override { return std::make_unique<FakeDecoder>(); }
};

int main() {
    std::cout << "DesktopPlayer:\n";
    DesktopPlayer().play(2);
    std::cout << "ServerPlayer:\n";
    ServerPlayer().play(2);
    std::cout << "TestPlayer:\n";
    TestPlayer().play(2);
}
```

```text
DesktopPlayer:
  GPU 解码第 1 帧
  GPU 解码第 2 帧
ServerPlayer:
  CPU 解码第 1 帧
  CPU 解码第 2 帧
TestPlayer:
  假帧 1
  假帧 2
```

```uml
label: 工厂方法：Player 的 play 调用虚函数 createDecoder；DesktopPlayer 创建 HardwareDecoder，ServerPlayer 创建 SoftwareDecoder
classes:
  - { id: P, name: Player, stereo: abstract, x: 20, y: 16, w: 250, ops: ['+ play(frames)', '*# createDecoder() : unique_ptr<Decoder>'] }
  - { id: D, name: Decoder, stereo: interface, x: 450, y: 16, w: 180, ops: ['*+ decode(frame) : string'] }
  - { id: DP, name: DesktopPlayer, x: 20, y: 180, w: 120 }
  - { id: SP, name: ServerPlayer, x: 160, y: 180, w: 120 }
  - { id: HD, name: HardwareDecoder, x: 400, y: 180, w: 140 }
  - { id: SD, name: SoftwareDecoder, x: 560, y: 180, w: 140 }
rels:
  - { from: DP, to: P, type: inherit, mid: 140 }
  - { from: SP, to: P, type: inherit, mid: 140 }
  - { from: HD, to: D, type: realize, mid: 140 }
  - { from: SD, to: D, type: realize, mid: 140 }
  - { from: P, to: D, type: depend, fdx: -10 }
notes:
  - { x: 300, y: 270, text: 'DesktopPlayer 创建 HardwareDecoder，ServerPlayer 创建 SoftwareDecoder' }
```

`Player::play` 只依赖 `Decoder` 接口，它调用的 `createDecoder()` 就是**工厂方法**。每个子类重写这个函数，决定流程里用哪种解码器。注意结构上的特点：创建者（`Player` 一族）和产品（`Decoder` 一族）是两个平行的类层次，每个具体创建者对应一种具体产品。

### 3.2 一个很实用的副作用：测试接缝

上面的 `TestPlayer` 展示了工厂方法的一个常见用途：**给测试留出接缝**。想测试 `play` 的流程（比如是不是每一帧都调用了解码、出错时有没有正确处理），却不想真的去解码视频，只要写一个子类，让工厂方法返回一个假的解码器就行了，`Player` 的代码一行都不用改。

### 3.3 和模板方法的关系

`play()` 是一个固定的流程，其中一步（创建解码器）由子类决定。这正是后面要讲的**模板方法模式**：工厂方法常常就是模板方法里「负责创建对象」的那一步。它也有继承的缺点：每多一种产品，就多一个创建者子类。如果只是想换一个对象，而不需要改变其他行为，第 1 章的依赖注入（把解码器从构造函数传进来）往往更简单。

## 4. 注册表工厂：加格式只需加一个文件

### 4.1 学 FFmpeg：让每种格式自己判断

回到第 2 节的简单工厂。它要求调用者先知道格式名，可文件扩展名经常是错的，甚至根本没有扩展名。FFmpeg 的做法是**看文件内容**：每种封装格式都提供一个探测函数（`read_probe`），读一读文件开头的字节，返回一个 0 到 100 的分数，表示「这个文件有多像我」；FFmpeg 把所有格式都问一遍，选分数最高的那个。

把这个思路做成工厂：注册表里的每一项登记三样东西：格式名、探测函数、创建函数。工厂不再有 `if-else`，只是遍历注册表：

```cpp
// 三段字节是 registry/run.sh 从测试视频提取出的 MP4、H.264、AAC 文件的前 16 个字节
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

struct Format {
    std::string name;
    std::function<int(const Bytes&)> probe;        // 返回 0～100：有多大把握是这种格式
};

std::vector<Format>& registry() {                  // Meyers 单例，保证注册之前已经构造好
    static std::vector<Format> r;
    return r;
}

struct Registrar {
    Registrar(std::string name, std::function<int(const Bytes&)> probe) { registry().push_back({std::move(name), std::move(probe)}); }
};

// 每种格式一个静态对象，main 之前完成注册；加一种格式就是再加一个这样的对象
Registrar mp4("mp4", [](const Bytes& d) { return d.size() >= 8 && d[4] == 'f' && d[5] == 't' && d[6] == 'y' && d[7] == 'p' ? 100 : 0; });
Registrar h264("h264", [](const Bytes& d) {
    if (d.size() < 5 || d[0] || d[1] || d[2] || d[3] != 1) return 0;
    return (d[4] & 0x1F) == 7 ? 80 : 40;           // 第一个 NAL 是 SPS 才有八成把握
});
Registrar aac("aac", [](const Bytes& d) { return d.size() >= 2 && d[0] == 0xFF && (d[1] & 0xF6) == 0xF0 ? 60 : 0; });

const Format* best(const Bytes& d) {
    const Format* best = nullptr;
    int bestScore = 0;
    for (const auto& f : registry()) {
        int s = f.probe(d);
        std::cout << "    " << f.name << " 打分 " << s << '\n';
        if (s > bestScore) { bestScore = s; best = &f; }
    }
    return best;
}

int main() {
    std::vector<std::pair<std::string, Bytes>> files = {
        {"bbb.mp4", {0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x69, 0x73, 0x6f, 0x6d, 0x00, 0x00, 0x02, 0x00}},
        {"bbb.h264", {0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0xff, 0xff, 0xa9, 0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48}},
        {"bbb.aac", {0xff, 0xf1, 0x4c, 0x80, 0x25, 0xbf, 0xfc, 0xdc, 0x00, 0x4c, 0x61, 0x76, 0x63, 0x36, 0x33, 0x2e}},
        {"readme.txt", {'H', 'e', 'l', 'l', 'o'}},
    };
    for (const auto& [name, bytes] : files) {
        std::cout << name << '\n';
        const Format* f = best(bytes);
        std::cout << "  → " << (f ? f->name : std::string("无法识别")) << '\n';
    }
}
```

```text
bbb.mp4
    mp4 打分 100
    h264 打分 0
    aac 打分 0
  → mp4
bbb.h264
    mp4 打分 0
    h264 打分 40
    aac 打分 0
  → h264
bbb.aac
    mp4 打分 0
    h264 打分 0
    aac 打分 60
  → aac
readme.txt
    mp4 打分 0
    h264 打分 0
    aac 打分 0
  → 无法识别
```

程序里的三段字节，是从专栏测试视频提取出的三个文件真实的前 16 个字节。下面的演示可以看到每个探测函数各检查了哪些字节：

<div class="dsviz" data-viz="dp-factory"></div>

H.264 只得了 40 分：真实的 `bbb.h264` 第一个 NAL 单元是 SEI（类型 6，x264 把自己的编码参数写在了最前面），而不是探测函数期待的 SPS。不过它仍然是最高分，所以被正确识别了。练习 2 会改进这个探测函数。

### 4.2 自动注册

注册表工厂最大的好处，在于**注册可以分散到每种格式自己的源文件里**。`registry/` 子目录是一个多文件的项目：`parser.h` 定义了 `Parser` 接口、`Registry` 注册表和一个辅助类 `Registrar`：

<!-- run: off -->
```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

class Parser {
public:
    virtual ~Parser() = default;
    virtual void parse(const Bytes& data) = 0;       // 解析整个文件，打印摘要
};

// 注册表：每种格式登记三样东西——名字、探测函数（看文件开头的字节，给出 0～100 的把握）、创建函数
struct Format {
    std::string name;
    std::function<int(const Bytes&)> probe;
    std::function<std::unique_ptr<Parser>()> create;
};

class Registry {
public:
    static Registry& instance();                     // Meyers 单例：保证在任何注册之前就已经构造好
    void add(Format f) { formats_.push_back(std::move(f)); }
    const std::vector<Format>& formats() const { return formats_; }
    const Format* best(const Bytes& data) const;     // 探测得分最高的格式

private:
    std::vector<Format> formats_;
};

// 每个格式的源文件里定义一个 Registrar 静态对象，它的构造函数在 main 之前运行，完成注册
struct Registrar {
    explicit Registrar(Format f) { Registry::instance().add(std::move(f)); }
};

inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
```

每种格式一个源文件，在文件末尾定义一个 `Registrar` 静态对象。它的构造函数在 `main` 之前运行，把这种格式登记到注册表里。以 AAC 为例：

<!-- run: off -->
```cpp
#include <cstdio>
#include "parser.h"

namespace {
const int kRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};

bool isSync(const Bytes& d, size_t p) { return p + 7 <= d.size() && d[p] == 0xFF && (d[p + 1] & 0xF6) == 0xF0; }
size_t frameLen(const Bytes& d, size_t p) { return (d[p + 3] & 0x03) << 11 | d[p + 4] << 3 | d[p + 5] >> 5; }

class AacParser : public Parser {
public:
    void parse(const Bytes& d) override {
        size_t pos = 0;
        int frames = 0;
        while (isSync(d, pos)) {
            size_t len = frameLen(d, pos);
            if (len < 7) break;
            ++frames;
            pos += len;
        }
        int rate = kRates[(d[2] >> 2) & 0x0F];
        int channels = (d[2] & 0x01) << 2 | d[3] >> 6;
        std::printf("  %d 帧，采样率 %d Hz，%d 声道，时长约 %.2f 秒\n", frames, rate, channels, frames * 1024.0 / rate);
    }
};

int probe(const Bytes& d) {                          // 同步字 0xFFF，并且按帧长跳过去，下一帧也是同步字
    if (!isSync(d, 0)) return 0;
    return isSync(d, frameLen(d, 0)) ? 90 : 30;
}

Registrar reg({"aac", probe, [] { return std::make_unique<AacParser>(); }});
}  // namespace
```

`main` 读入文件，问注册表哪种格式得分最高，用它登记的创建函数造一个解析器：

<!-- run: off -->
```cpp
#include <cstdio>
#include <fstream>
#include <iterator>
#include "parser.h"

int main(int argc, char** argv) {
    std::printf("已注册的格式：");
    for (const auto& f : Registry::instance().formats()) std::printf("%s ", f.name.c_str());
    std::printf("（共 %zu 种）\n", Registry::instance().formats().size());

    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const Format* f = Registry::instance().best(data);
        std::printf("%s（%zu 字节）：%s\n", argv[i], data.size(), f ? f->name.c_str() : "无法识别");
        if (f) f->create()->parse(data);             // 工厂：调用者只拿到 Parser 接口，不知道具体是哪个类
    }
}
```

`registry/run.sh` 先用 ffmpeg 从 `bbb-10s.mp4` 提取出三个样本（MP4 截取前 2 秒，H.264 和 AAC 是完整的裸流），再编译运行：

```text
已注册的格式：mp4 h264 aac （共 3 种）
build/samples/bbb.aac（163271 字节）：aac
  470 帧，采样率 48000 Hz，2 声道，时长约 10.03 秒
build/samples/bbb.h264（2827050 字节）：h264
  NAL 类型  1 非 IDR 片   233 个
  NAL 类型  5 IDR 片        7 个
  NAL 类型  6 SEI            1 个
  NAL 类型  7 SPS            7 个
  NAL 类型  8 PPS            7 个
build/samples/bbb.mp4（369814 字节）：mp4
  box ftyp  32 字节
  box free  8 字节
  box mdat  367036 字节
  box moov  2738 字节
```

和 ffprobe 的结果对照：测试视频有 470 个 AAC 帧、240 个视频帧，这里数出的 AAC 帧是 470，IDR 片加非 IDR 片正好是 233 + 7 = 240。

```uml
label: 注册表工厂：Registry 保存多个 Format，每个 Format 有名字、探测函数和创建函数；每个解析器源文件里的 Registrar 静态对象在 main 之前完成注册
classes:
  - { id: Main, name: main, x: 20, y: 45.5, w: 90 }
  - { id: R, name: Registry, x: 170, y: 16, w: 220, ops: ['$+ instance() : Registry&', '+ add(f: Format)', '+ best(data) : const Format*'] }
  - { id: F, name: Format, x: 470, y: 16, w: 250, attrs: ['+ name : string', '+ probe : function<int(const Bytes&)>', '+ create : function<unique_ptr<Parser>()>'] }
  - { id: Reg, name: 'Registrar（每个 .cpp 一个）', x: 170, y: 190, w: 220 }
rels:
  - { from: Main, to: R, type: depend }
  - { from: R, to: F, type: compose, tm: '*' }
  - { from: Reg, to: R, type: depend, label: 'add()  // 在 main 之前' }
```

现在加一种格式，**只需要新加一个源文件**（练习 3 加的是 FLV），已有的代码，包括 `main` 和注册表，一行都不用改。这正是开闭原则：对扩展开放，对修改关闭。

## 5. 自动注册的两个坑

自动注册依赖「全局对象在 `main` 之前构造」，这里藏着两个坑。

### 5.1 注册表本身的初始化顺序

如果注册表是一个普通的全局变量（比如在 `registry.cpp` 里定义 `std::vector<Format> g_formats;`），就会遇到上一篇的静态初始化顺序问题：`aac_parser.cpp` 里的 `Registrar` 构造时，`g_formats` 可能还没构造，往一个没构造好的容器里插入元素是未定义行为；即使没崩溃，`g_formats` 随后构造时也会把它清空。所以 `Registry::instance()` 用的是 Meyers 单例：谁先调用，谁就触发构造，保证注册之前注册表已经准备好。

### 5.2 静态库：注册悄悄消失了

第二个坑更隐蔽。项目大了以后，通常会把解析器打包成一个静态库，再链接到程序里。`run.sh` 用三种方式构建同一个程序：

```text
== 1. 直接链接所有 .o
已注册的格式：mp4 h264 aac （共 3 种）
……（识别和解析都正常）
== 2. 链接静态库 libparsers.a
已注册的格式：（共 0 种）
build/samples/bbb.aac（163271 字节）：无法识别
build/samples/bbb.h264（2827050 字节）：无法识别
build/samples/bbb.mp4（369814 字节）：无法识别
== 3. 静态库 + 强制链接整个库
已注册的格式：mp4 h264 aac （共 3 种）
……（识别和解析都正常）
```

同样的源文件，打包成静态库以后，**一种格式都没有注册上**。编译、链接都没有任何警告。macOS 上的 Clang 和 Ubuntu 上的 GCC 结果完全相同。

<figure class="diagram">
<svg viewBox="0 0 740 250" role="img" aria-label="链接静态库时，链接器只取出能解决未定义符号的目标文件。main.o 和 registry.o 没有引用解析器目标文件里的任何符号，所以三个解析器的目标文件都没有被链接进来，它们的注册对象也就不存在"><g style="font-family:var(--mono)"><text x="10.0" y="18.0" text-anchor="start" style="fill:var(--muted);font-size:11px">链接器处理静态库：只取出「能解决当前未定义符号」的 .o 文件</text><rect x="10.0" y="34.0" width="230.0" height="70.0" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="22.0" y="54.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">main.o</text><text x="22.0" y="72.0" text-anchor="start" style="fill:var(--fg-2);font-size:11px">引用：Registry::instance()</text><text x="22.0" y="90.0" text-anchor="start" style="fill:var(--fg-2);font-size:11px">      Registry::best() …</text><rect x="10.0" y="120.0" width="230.0" height="52.0" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="22.0" y="140.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">registry.o</text><text x="22.0" y="158.0" text-anchor="start" style="fill:var(--fg-2);font-size:11px">定义：Registry::instance() …</text><path d="M125,104 V120" style="stroke:var(--accent);stroke-width:1.5"/><rect x="300.0" y="34.0" width="430.0" height="150.0" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 3"/><text x="312.0" y="54.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">libparsers.a</text><rect x="312.0" y="68.0" width="128.0" height="100.0" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2;opacity:.6"/><text x="376.0" y="88.0" text-anchor="middle" style="fill:var(--muted);font-size:11px;font-weight:700">mp4_parser.o</text><text x="376.0" y="108.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">只有一个匿名</text><text x="376.0" y="124.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">命名空间里的</text><text x="376.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">Registrar 对象</text><text x="376.0" y="160.0" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">✗ 没有被引用</text><rect x="452.0" y="68.0" width="128.0" height="100.0" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2;opacity:.6"/><text x="516.0" y="88.0" text-anchor="middle" style="fill:var(--muted);font-size:11px;font-weight:700">h264_parser.o</text><text x="516.0" y="108.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">只有一个匿名</text><text x="516.0" y="124.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">命名空间里的</text><text x="516.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">Registrar 对象</text><text x="516.0" y="160.0" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">✗ 没有被引用</text><rect x="592.0" y="68.0" width="128.0" height="100.0" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2;opacity:.6"/><text x="656.0" y="88.0" text-anchor="middle" style="fill:var(--muted);font-size:11px;font-weight:700">aac_parser.o</text><text x="656.0" y="108.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">只有一个匿名</text><text x="656.0" y="124.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">命名空间里的</text><text x="656.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">Registrar 对象</text><text x="656.0" y="160.0" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">✗ 没有被引用</text><text x="10.0" y="206.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px">没有任何符号被 main.o 或 registry.o 引用 → 三个 .o 都不会被取出 → 它们的 Registrar 构造函数从来没有运行</text><text x="10.0" y="230.0" text-anchor="start" style="fill:var(--muted);font-size:11px">修复：--whole-archive / -force_load、CMake 的 OBJECT 库或 WHOLE_ARCHIVE，或者改成显式注册</text></g></svg>
</figure>

原因在于链接器处理静态库的方式：静态库只是一堆 `.o` 文件的集合，链接器**只取出那些能解决当前未定义符号的 `.o` 文件**。`main.o` 和 `registry.o` 没有引用解析器源文件里的任何符号（`Registrar` 对象在匿名命名空间里，外面根本看不到它），所以链接器认为这三个 `.o` 文件「用不上」，一个都没有链接进来，它们的 `Registrar` 构造函数自然也从来没有运行。

修复方法有几种：

| 方法 | 写法 | 说明 |
| --- | --- | --- |
| 强制链接整个静态库 | Linux：`-Wl,--whole-archive libparsers.a -Wl,--no-whole-archive`；macOS：`-Wl,-force_load,libparsers.a` | 本文 `run.sh` 的第 3 种方式 |
| CMake 的 OBJECT 库 | `add_library(parsers OBJECT ...)` | 直接链接目标文件，不经过静态库（练习 4） |
| CMake 3.24 起的生成器表达式 | `target_link_libraries(app PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,parsers>")` | CMake 替你选择各平台对应的选项（练习 4） |
| 显式注册 | 每种格式提供一个 `registerAac()` 函数，`main` 里依次调用 | 不再「自动」，但最可靠，也不依赖初始化顺序 |

很多大型项目（包括 FFmpeg 自己）选择的是最后一种：FFmpeg 用一个自动生成的列表，把所有格式集中登记在一个地方。牺牲一点点「加格式只需加文件」的便利，换来确定性：注册了哪些格式、按什么顺序，一眼就能看清楚。

## 6. 三种写法怎么选

| | 简单工厂 | 工厂方法 | 注册表工厂 |
| --- | --- | --- | --- |
| 解决的问题 | 按名字（或类型码）选一个类创建 | 框架定好流程，由子类决定用哪种对象 | 产品种类多、经常增加，或由不同的人提供 |
| 加一种产品 | 修改工厂函数 | 新写一个创建者子类 | 新加一个源文件（自动注册）或一行（显式注册） |
| 实现方式 | 一个函数 + `if-else` / `switch` | 虚函数 + 继承 | `std::map` / `std::vector` + `std::function` |
| 适合 | 种类少、很少变化 | 框架、库、需要测试接缝的地方 | 格式、插件、命令、协议处理器 |

几个现代 C++ 的细节：

- 工厂函数应该返回 `std::unique_ptr<接口>`，让调用者明确地拥有返回的对象，而不是返回裸指针、让调用者记得 `delete`。需要共享时，调用者可以再把它转成 `std::shared_ptr`。
- 注册表里的创建函数用 `std::function` 存储，可以是 lambda、普通函数，也可以是捕获了配置参数的闭包。
- 如果产品的构造函数需要参数，可以把创建函数的签名改成 `std::function<std::unique_ptr<Parser>(const Options&)>`，所有产品统一接收一个配置结构体。

## 7. 小结

- 创建型模式把「创建哪个具体类」集中到一处，其余代码只依赖接口
- **简单工厂**：一个函数按名字创建对象。不是 GoF 模式，但很常用；加产品要修改它，但修改集中在一处
- **工厂方法**：框架定好流程，用一个虚函数把「创建哪种对象」交给子类决定。常用来给测试留接缝；它往往是模板方法中的一步
- **注册表工厂**：每种产品登记名字、探测函数和创建函数，工厂只负责查表。像 FFmpeg 一样按文件内容打分选格式，加格式只需加一个文件
- 实测：用测试视频提取的真实样本，识别全部正确，AAC 470 帧、视频 240 帧，与 ffprobe 一致；H.264 裸流的第一个 NAL 是 SEI，探测只得了 40 分但仍被选中
- 自动注册的坑：注册表要用 Meyers 单例；**打包成静态库后注册会全部丢失**（实测 0 种格式），要用 whole-archive / force_load、OBJECT 库，或改成显式注册

## 8. 练习

1. 下面三个场景，分别用简单工厂、工厂方法还是注册表工厂？
   ① 一个日志库要根据配置文件里的 `"console"`、`"file"`、`"syslog"` 创建输出目标，这三种很多年都没变过；
   ② 一个测试框架提供 `TestCase` 基类，它的 `run()` 会先调用 `createFixture()` 准备测试数据，再执行测试，不同的测试用例需要不同的数据；
   ③ 一个图片查看器要支持几十种图片格式，新格式由社区以单独的源文件贡献。

2. 4.1 节的 H.264 探测函数只看第一个 NAL 单元，真实的 `bbb.h264` 因为以 SEI 开头只得了 40 分。改进它：在文件的前 1 KB 里查找 SPS，找到了就给 80 分。

3. 给 `registry/` 项目加上 FLV 格式：探测函数检查文件开头的 `"FLV"` 和版本号 1；解析时统计音频 tag、视频 tag 和脚本 tag 各有多少个。FLV 文件可以用 `ffmpeg -i bbb-10s.mp4 -c copy -f flv bbb.flv` 生成。数出来的 tag 数和 ffprobe 统计的帧数一样吗？

4. 项目改用 CMake 构建后，有人把解析器写成了 `add_library(parsers STATIC ...)`，再 `target_link_libraries(app PRIVATE parsers)`，结果所有格式都认不出来了。在 CMake 里怎么修复？给出两种写法。

<details>
<summary>点击查看答案</summary>

1. ① 简单工厂：只有三种，很少变化，一个函数加 `if-else` 最清楚。
   ② 工厂方法：`TestCase::run()` 是固定的流程，`createFixture()` 由每个测试子类重写，正是「框架定流程、子类决定创建什么」。
   ③ 注册表工厂：格式多、经常增加，而且由不同的人分别贡献，每种格式自己一个源文件、自己注册，互不影响。

2. 在前 1 KB 里查找起始码 `00 00 01` 后面跟着类型为 7 的 NAL 单元（文件开头的 4 字节起始码也包含了 `00 00 01`）：

   <!-- run: off -->
   ```cpp
   int probe(const Bytes& d) {
       if (d.size() < 5 || d[0] || d[1] || d[2] || d[3] != 1) return 0;
       size_t limit = d.size() < 1024 ? d.size() : 1024;          // 只看前 1 KB
       for (size_t i = 0; i + 3 < limit; ++i)
           if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1F) == 7) return 80;
       return 40;
   }
   ```

   实测在 `bbb.h264` 的第 693 字节找到了 SPS（前面是 SEI），得分从 40 变成 80。只看前 1 KB 是为了让探测足够快：FFmpeg 也是先读一小段数据来探测，分数太低时才读更多。

3. 新加一个 `flv_parser.cpp`（仓库里的 `registry/exercise/flv_parser.cpp`），其余文件都不用改：

   <!-- run: off -->
   ```cpp
   #include <cstdio>
   #include "../parser.h"

   namespace {
   class FlvParser : public Parser {
   public:
       void parse(const Bytes& d) override {
           int audio = 0, video = 0, script = 0;
           size_t pos = be32(&d[5]) + 4;                  // 跳过 FLV 头（长度记在第 5～8 字节）和第一个 PreviousTagSize
           while (pos + 11 <= d.size()) {
               int type = d[pos] & 0x1F;
               size_t size = d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3];
               if (type == 8) ++audio;
               else if (type == 9) ++video;
               else if (type == 18) ++script;
               pos += 11 + size + 4;                      // tag 头 11 字节 + 数据 + PreviousTagSize
           }
           std::printf("  音频 tag %d 个，视频 tag %d 个，脚本 tag %d 个\n", audio, video, script);
       }
   };

   int probe(const Bytes& d) {                            // 文件以 "FLV" 开头，版本号为 1
       return d.size() >= 9 && d[0] == 'F' && d[1] == 'L' && d[2] == 'V' && d[3] == 1 ? 100 : 0;
   }

   Registrar reg({"flv", probe, [] { return std::make_unique<FlvParser>(); }});
   }  // namespace
   ```

   把它一起编译链接后，用转出来的 FLV 运行：

   ```text
   已注册的格式：mp4 h264 aac flv （共 4 种）
   bbb.flv（3000038 字节）：flv
     音频 tag 471 个，视频 tag 242 个，脚本 tag 1 个
   ```

   和 ffprobe 统计的帧数（音频 470、视频 240）不完全一样：FLV 在第一个音频 tag 里存放 AAC 的序列头（AudioSpecificConfig），在第一个视频 tag 里存放 AVC 的序列头（SPS / PPS），视频的最后还有一个「序列结束」tag。去掉这几个，正好是 470 和 240。脚本 tag 是开头的 `onMetaData`，存放时长、分辨率等信息。

4. 两种写法都实测过（CMake 4.4）：

   ```cmake
   # 写法一：OBJECT 库，直接链接目标文件
   add_library(parsers OBJECT mp4_parser.cpp h264_parser.cpp aac_parser.cpp)
   target_link_libraries(app PRIVATE parsers)

   # 写法二：仍然是静态库，但链接时强制包含整个库（CMake 3.24 起）
   add_library(parsers STATIC mp4_parser.cpp h264_parser.cpp aac_parser.cpp)
   target_link_libraries(app PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,parsers>")
   ```

   原来的写法注册了 0 种格式，两种修复后都注册了 3 种。写法二由 CMake 自动选择各平台对应的链接选项（Linux 上是 `--whole-archive`，macOS 上是 `-force_load`）。

</details>
