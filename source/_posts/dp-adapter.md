---
title: 适配器模式
date: 2026-11-10 10:00:00
updated: 2026-11-10 10:00:00
categories:
  - 设计模式
  - 结构型模式
tags:
  - 设计模式
  - 适配器
  - RAII
  - FFmpeg
order: 1
description: 播放器只认识一个 C++ 风格的 MediaSource 接口，FFmpeg 却是 C API：负数错误码、每条返回路径都要手动释放。对象适配器把 avformat_open_input、av_read_frame 翻译成 next()，读测试视频得到 240 个视频包、470 个音频包，和 ffprobe 一致。实测提前返回的场景：直接调用 C API 的版本 10 次调用泄漏了 5 MB（820 处），通过适配器的版本 0 处。再写一个私有继承的类适配器、一个把 C 回调「推」来的数据转成 next()「拉」取的适配器，最后看看标准库里的 stack、back_inserter 这些适配器，并分清适配器、外观、装饰器和代理。
top_group_list: true
disableNunjucks: true
viz: [dp-adapter]
---

> **本文目标**：理解适配器模式解决的问题；会写对象适配器和类适配器，知道两者的区别；会把 C 风格的 API（错误码、手动释放、回调）适配成 C++ 接口，并用 RAII 管理资源；分清适配器和外观、装饰器、代理。
>
> **前置知识**：第 2 章（创建型模式）；C/C++ 专栏的《智能指针：unique_ptr、shared_ptr 与 weak_ptr》；最好读过音视频专栏的《FFmpeg 的库与开发环境》《解封装与解码》，知道 `avformat_open_input` 和 `av_read_frame` 是做什么的。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/11-adapter/` 目录；用到 FFmpeg 的程序在 `ffmpeg/` 子目录，用 `build.sh` 编译（需要安装 FFmpeg 的开发库）。

## 1. 结构型模式

从这一篇开始是第 3 章**结构型模式**。创建型模式关心「对象怎么创建」，结构型模式关心「类和对象怎么组合成更大的结构」。这一章的七个模式里，有好几个长得很像：都是一个对象包着另一个对象、把调用转发给它。区别在于**为什么要包这一层**：适配器是为了转换接口，装饰器是为了添加功能，代理是为了控制访问，外观是为了简化接口……读完这一章，你会对这些「包一层」的手法分得很清楚。

## 2. 问题：接口对不上

播放器的代码已经写好了，它通过一个 C++ 风格的接口读取媒体数据：

```cpp
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct Packet {
    int stream;              // 流的编号
    bool video;              // 是不是视频流
    double time;             // 时间戳，单位是秒
    bool keyframe;
    std::vector<uint8_t> data;
};

class MediaSource {
public:
    virtual ~MediaSource() = default;
    virtual std::string describe() const = 0;
    virtual std::optional<Packet> next() = 0;     // 没有更多数据时返回空
};
```

现在要用 FFmpeg 来读真正的视频文件。FFmpeg 是一个 C 库，它的接口和 `MediaSource` 处处对不上：

| | 播放器期望的（`MediaSource`） | FFmpeg 提供的 |
| --- | --- | --- |
| 读一个包 | `std::optional<Packet> next()` | `int av_read_frame(AVFormatContext*, AVPacket*)` |
| 出错 / 结束 | 返回空的 `optional` | 返回负数错误码，用 `av_strerror` 翻译 |
| 资源释放 | 对象析构时自动完成 | 每条返回路径都要手动 `av_packet_unref`、`av_packet_free`、`avformat_close_input` |
| 时间戳 | 秒 | `pts` 乘以每路流的 `time_base` |

两边都不能改：播放器的代码已经有很多地方依赖 `MediaSource`，FFmpeg 是第三方库。这时就需要一个**适配器**。

## 3. 对象适配器：把 FFmpeg 适配成 MediaSource

### 3.1 意图

**适配器模式**（Adapter）：将一个类的接口转换成客户希望的另一个接口，使得原本由于接口不兼容而不能一起工作的类可以一起工作。

就像电源转接头：墙上的插座（FFmpeg）和电器的插头（播放器）形状不同，转接头一头插进插座，另一头提供电器需要的插孔。适配器实现**目标接口**（`MediaSource`），内部持有**被适配者**（FFmpeg 的 `AVFormatContext`），把每个调用翻译成被适配者能听懂的形式：

```cpp
#pragma once
#include "media_source.h"

struct AVFormatContext;
struct AVPacket;

class FfmpegSource : public MediaSource {
public:
    // 打开失败时返回空指针，并在 error 里写明原因（FFmpeg 的错误码翻译成文字）
    static std::unique_ptr<FfmpegSource> open(const std::string& path, std::string& error);

    std::string describe() const override;
    std::optional<Packet> next() override;

private:
    struct FormatCloser { void operator()(AVFormatContext* c) const; };
    struct PacketFreer { void operator()(AVPacket* p) const; };

    FfmpegSource() = default;
    std::unique_ptr<AVFormatContext, FormatCloser> fmt_;   // 被适配的对象，析构时自动 avformat_close_input
    std::unique_ptr<AVPacket, PacketFreer> pkt_;           // 复用的 AVPacket，析构时自动 av_packet_free
};
```

```cpp
extern "C" {
#include <libavformat/avformat.h>
}
#include "ffmpeg_source.h"

void FfmpegSource::FormatCloser::operator()(AVFormatContext* c) const { avformat_close_input(&c); }
void FfmpegSource::PacketFreer::operator()(AVPacket* p) const { av_packet_free(&p); }

static std::string errorText(int code) {          // FFmpeg 返回负数错误码，翻译成文字
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(code, buf, sizeof buf);
    return buf;
}

std::unique_ptr<FfmpegSource> FfmpegSource::open(const std::string& path, std::string& error) {
    AVFormatContext* raw = nullptr;
    if (int rc = avformat_open_input(&raw, path.c_str(), nullptr, nullptr); rc < 0) {
        error = "打开 " + path + " 失败：" + errorText(rc);
        return nullptr;
    }
    std::unique_ptr<FfmpegSource> src(new FfmpegSource);
    src->fmt_.reset(raw);                          // 从这里开始，任何一条返回路径都会自动关闭文件
    if (int rc = avformat_find_stream_info(raw, nullptr); rc < 0) {
        error = "读取流信息失败：" + errorText(rc);
        return nullptr;
    }
    src->pkt_.reset(av_packet_alloc());
    return src;
}

std::string FfmpegSource::describe() const {
    return std::string(fmt_->iformat->name) + "，" + std::to_string(fmt_->nb_streams) + " 路流，时长 " +
           std::to_string(fmt_->duration / 1000) + " ms";
}

std::optional<Packet> FfmpegSource::next() {
    if (av_read_frame(fmt_.get(), pkt_.get()) < 0) return std::nullopt;   // 文件结束或出错
    AVPacket* p = pkt_.get();
    AVStream* st = fmt_->streams[p->stream_index];
    Packet out{p->stream_index, st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO, p->pts * av_q2d(st->time_base),
               (p->flags & AV_PKT_FLAG_KEY) != 0, std::vector<uint8_t>(p->data, p->data + p->size)};
    av_packet_unref(p);                            // C API 要求每读一个包都手动释放
    return out;
}
```

```uml
label: 对象适配器：FfmpegSource 实现 MediaSource 接口，内部用 unique_ptr 持有 FFmpeg 的 AVFormatContext 和 AVPacket，把 next 翻译成 av_read_frame
classes:
  - { id: P, name: 播放器, x: 20, y: 40, w: 110 }
  - { id: M, name: MediaSource, stereo: interface, x: 200, y: 16, w: 230, ops: ['*+ describe() : string', '*+ next() : optional<Packet>'] }
  - { id: F, name: FfmpegSource, x: 200, y: 190, w: 230, attrs: ['- fmt_ : unique_ptr<AVFormatContext>', '- pkt_ : unique_ptr<AVPacket>'], ops: ['+ next()  // av_read_frame + 复制'] }
  - { id: A, name: 'AVFormatContext（C）', x: 570, y: 194, w: 180, ops: ['avformat_open_input()', 'av_read_frame()', 'avformat_close_input()'] }
rels:
  - { from: P, to: M, type: depend, fdx: 6 }
  - { from: F, to: M, type: realize }
  - { from: F, to: A, type: compose, label: '被适配者' }
```

几个值得注意的翻译：

- **资源管理**：`AVFormatContext*` 和 `AVPacket*` 一拿到就放进带自定义删除器的 `std::unique_ptr`。从这一刻起，不管 `open` 从哪条路径返回、播放器什么时候丢掉这个对象，`avformat_close_input` 和 `av_packet_free` 都一定会被调用。
- **错误处理**：负数错误码在 `open` 里翻译成文字，在 `next` 里翻译成空的 `optional`。播放器根本看不到 FFmpeg 的错误码。
- **构造函数私有、通过静态函数创建**：打开文件可能失败，构造函数没法返回失败（除非抛异常），所以用一个返回 `unique_ptr` 的静态函数，失败时返回空指针并说明原因。

### 3.2 实测：读测试视频

播放器这边的代码只依赖 `MediaSource`：

<!-- run: off -->
```cpp
#include <cstdio>
#include <map>
#include "ffmpeg_source.h"

// 只依赖 MediaSource 接口的代码：统计每路流的包数、关键帧数和字节数
void summarize(MediaSource& src) {
    std::printf("%s\n", src.describe().c_str());
    struct Stat { int packets = 0, keyframes = 0; size_t bytes = 0; };
    std::map<int, Stat> stats;
    while (auto pkt = src.next()) {
        auto& s = stats[pkt->stream];
        ++s.packets;
        s.keyframes += pkt->keyframe;
        s.bytes += pkt->data.size();
    }
    for (const auto& [i, s] : stats)
        std::printf("  流 %d：%d 个包，其中关键帧 %d 个，共 %zu KB\n", i, s.packets, s.keyframes, s.bytes / 1024);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string err;
        auto src = FfmpegSource::open(argv[i], err);
        if (!src) { std::printf("%s\n", err.c_str()); continue; }
        summarize(*src);
    }
}
```

用 `ffmpeg/build.sh` 编译（macOS 上用 Homebrew 的 FFmpeg 9.0），读专栏的测试视频：

```text
mov,mp4,m4a,3gp,3g2,mj2，2 路流，时长 10000 ms
  流 0：240 个包，其中关键帧 7 个，共 2760 KB
  流 1：470 个包，其中关键帧 470 个，共 156 KB
```

和 `ffprobe -count_packets` 的结果一致：视频 240 个包、音频 470 个包。AAC 的每一帧都能独立解码，所以音频的 470 个包都是「关键帧」。

## 4. 实测：适配器挡住了泄漏

适配器的价值不只是「接口好看了」。下面这个函数要找到某个时间点之后的第一个视频关键帧（比如每秒生成一张缩略图），直接用 C API 写：

<!-- run: off -->
```cpp
extern "C" {
#include <libavformat/avformat.h>
}
#include <cstdio>

// 找到 sec 秒之后的第一个视频关键帧（比如生成缩略图），返回它的时间；找不到返回 -1
double keyframeAfter(const char* path, double sec) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) return -1;
    if (avformat_find_stream_info(fmt, nullptr) < 0) return -1;            // 漏了 avformat_close_input
    int v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == v && (pkt->flags & AV_PKT_FLAG_KEY)) {
            double t = pkt->pts * av_q2d(fmt->streams[v]->time_base);
            if (t >= sec) return t;                // 找到了就返回：忘了 av_packet_unref、av_packet_free、avformat_close_input
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return -1;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "bbb-10s.mp4";
    for (int s = 0; s < 10; ++s) std::printf("%.2f ", keyframeAfter(path, s));   // 每秒生成一张缩略图
    std::printf("\n");
}
```

逻辑完全正确，10 次调用的结果是 `0.00 1.29 3.29 3.29 5.46 5.46 7.46 7.46 9.46 9.46`。问题在 `return t;` 这一行：找到关键帧就直接返回，释放资源的代码写在循环后面，这条路径没有经过它。同样的逻辑通过适配器写：

<!-- run: off -->
```cpp
#include <cstdio>
#include "ffmpeg_source.h"

double keyframeAfter(const char* path, double sec) {
    std::string err;
    auto src = FfmpegSource::open(path, err);
    if (!src) return -1;
    while (auto pkt = src->next())
        if (pkt->video && pkt->keyframe && pkt->time >= sec) return pkt->time;   // 直接返回，src 析构时自动关闭文件
    return -1;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "bbb-10s.mp4";
    for (int s = 0; s < 10; ++s) std::printf("%.2f ", keyframeAfter(path, s));
    std::printf("\n");
}
```

结果完全相同。用 macOS 自带的 `leaks --atExit -- ./build/raw_c bbb-10s.mp4` 检查程序退出时还有多少内存没有释放；再在 Ubuntu 24.04 上用 GCC 13 加 `-fsanitize=address` 编译（FFmpeg 6.1），用 AddressSanitizer 的泄漏检测复核一遍：

| 版本 | macOS（leaks，FFmpeg 9.0） | Linux（AddressSanitizer，FFmpeg 6.1） |
| --- | --- | --- |
| 直接用 C API | **820 处，共 5226720 字节**（约 5 MB） | **810 次分配，共 3726279 字节**（约 3.6 MB） |
| 通过适配器 | **0 处** | **没有泄漏** |

两个平台的字节数不同，是因为 FFmpeg 的版本和内存分配器不一样，结论是相同的。在 macOS 上，每次调用漏掉约 510 KB，主要是 `AVFormatContext` 和 FFmpeg 为解析 MP4 分配的各种缓冲区。缩略图功能要处理成百上千个文件时，这就是一个会慢慢耗光内存的 bug。下面的演示对比了两条路径上资源的去向：

<div class="dsviz" data-viz="dp-adapter"></div>

这就是把 C 库适配成 C++ 接口时最重要的一点：**让资源的释放跟着对象的生命周期走**（RAII），而不是依赖调用者在每条返回路径上记得释放。适配器是把 C 的「手动管理」转换成 C++ 的「自动管理」的边界。

## 5. 类适配器

适配器还有另一种写法：不持有被适配者，而是**继承**它。GoF 把前一种叫**对象适配器**，这一种叫**类适配器**。在 C++ 里，类适配器用公有继承目标接口、**私有继承**被适配的类：私有继承表示「用它来实现」，被适配者的接口不会暴露给外面。

下面的程序里有两个接口各不相同的数据源：一个旧的 C++ 类 `LegacyWavReader`（`readChunk` 返回读到的字节数，0 表示结束），用类适配器适配；一个 C 风格的采集 API（每采到一帧就调用一次回调），用第 6 节的推转拉适配器适配。最后都交给同一个只认识 `MediaSource` 的 `summarize` 函数：

```cpp
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ---------- 目标接口：播放器只认识它 ----------
struct Packet {
    double time;
    std::vector<unsigned char> data;
};

class MediaSource {
public:
    virtual ~MediaSource() = default;
    virtual std::string describe() const = 0;
    virtual std::optional<Packet> next() = 0;
};

// 只依赖目标接口的代码
void summarize(MediaSource& src) {
    int n = 0;
    size_t bytes = 0;
    double last = 0;
    while (auto p = src.next()) { ++n; bytes += p->data.size(); last = p->time; }
    std::cout << src.describe() << "：" << n << " 个包，" << bytes << " 字节，最后一个在 " << last << " 秒\n";
}

// ---------- 被适配者一：一个旧的 C++ 类，接口风格完全不同 ----------
class LegacyWavReader {
public:
    bool openFile(const char* name) { name_ = name; pos_ = 0; return true; }
    int readChunk(unsigned char* buf, int max) {          // 返回读到的字节数，0 表示读完
        int n = pos_ + max <= kTotal ? max : kTotal - pos_;
        std::memset(buf, 0x80, n);
        pos_ += n;
        return n;
    }
    int sampleRate() const { return 48000; }
private:
    static constexpr int kTotal = 48000 * 2 * 2;          // 1 秒、48 kHz、16 位立体声
    std::string name_;
    int pos_ = 0;
};

// 类适配器：公有继承目标接口，私有继承被适配的类（「用它来实现」，不对外暴露）
class WavSource : public MediaSource, private LegacyWavReader {
public:
    explicit WavSource(const char* name) { openFile(name); }
    std::string describe() const override { return "WAV（类适配器）"; }
    std::optional<Packet> next() override {
        Packet p{time_, std::vector<unsigned char>(4096)};
        int n = readChunk(p.data.data(), 4096);          // 直接调用继承来的函数
        if (n == 0) return std::nullopt;
        p.data.resize(n);
        time_ += n / 4.0 / sampleRate();                  // 每个采样 2 声道 × 2 字节
        return p;
    }
private:
    double time_ = 0;
};

// ---------- 被适配者二：C 风格的回调 API，数据由它「推」过来 ----------
extern "C" {
typedef void (*frame_callback)(void* user, const unsigned char* data, int len, double time);
// 模拟一个采集设备：采集 frames 帧，每采到一帧就调用一次回调
int capture_run(int frames, frame_callback cb, void* user) {
    unsigned char buf[1000];
    for (int i = 0; i < frames; ++i) {
        std::memset(buf, i, sizeof buf);
        cb(user, buf, sizeof buf, i / 30.0);              // 30 fps
    }
    return 0;
}
}

// 推转拉适配器：把回调推来的数据存进队列，next() 再一个个拉出去
class CaptureSource : public MediaSource {
public:
    explicit CaptureSource(int frames) {
        // C 函数只接受普通的函数指针。用不捕获变量的 lambda（可以转换成函数指针），
        // 把 this 通过 void* user 传过去，回调里再转回来，就接回了 C++ 对象
        capture_run(frames, [](void* user, const unsigned char* d, int len, double t) {
            static_cast<CaptureSource*>(user)->queue_.push_back({t, std::vector<unsigned char>(d, d + len)});
        }, this);
    }
    std::string describe() const override { return "采集设备（推转拉适配器）"; }
    std::optional<Packet> next() override {
        if (queue_.empty()) return std::nullopt;
        Packet p = std::move(queue_.front());
        queue_.pop_front();
        return p;
    }
private:
    std::deque<Packet> queue_;
};

int main() {
    std::vector<std::unique_ptr<MediaSource>> sources;
    sources.push_back(std::make_unique<WavSource>("voice.wav"));
    sources.push_back(std::make_unique<CaptureSource>(90));
    for (auto& s : sources) summarize(*s);                // 同一个函数，处理两种完全不同的数据源
}
```

```text
WAV（类适配器）：47 个包，192000 字节，最后一个在 0.981333 秒
采集设备（推转拉适配器）：90 个包，90000 字节，最后一个在 2.96667 秒
```

```uml
label: 类适配器：WavSource 公有继承 MediaSource、私有继承 LegacyWavReader，直接调用继承来的 readChunk
classes:
  - { id: M, name: MediaSource, stereo: interface, x: 20, y: 16, w: 220, ops: ['*+ next() : optional<Packet>'] }
  - { id: L, name: LegacyWavReader, x: 380, y: 16, w: 220, ops: ['+ openFile(name) : bool', '+ readChunk(buf, max) : int'] }
  - { id: W, name: WavSource, x: 200, y: 190, w: 220, ops: ['+ next()  // 调用 readChunk'] }
rels:
  - { from: W, to: M, type: realize, fdx: -50, mid: 150 }
  - { from: W, to: L, type: inherit, fdx: 50, label: 'private', mid: 130 }
```

两种适配器的区别：

| | 对象适配器（组合） | 类适配器（私有继承） |
| --- | --- | --- |
| 被适配者从哪来 | 持有一个对象（或指针） | 继承一个类 |
| 能适配被适配者的子类吗 | 能：持有一个基类指针，运行时传入任何子类 | 不能：继承的是一个固定的类 |
| 能重写被适配者的虚函数吗 | 不能 | 能 |
| 能访问被适配者的 `protected` 成员吗 | 不能 | 能 |
| 被适配者是 C API 时 | 适用 | 不适用（C 结构体没法有意义地继承） |

按照第 1 章「组合优于继承」的原则，**优先用对象适配器**。只有需要重写被适配者的虚函数、或者访问它的 `protected` 成员时，才考虑类适配器。

## 6. 推转拉：适配回调式的 API

很多 C 库（采集设备、网络库、解析器）不是让你去「拉」数据，而是在数据到来时调用你注册的**回调函数**，把数据「推」给你。可是播放器希望的是 `next()` 一个一个拉取。`adapters.cpp` 里的 `CaptureSource` 就是一个**推转拉**的适配器：回调把推来的数据存进队列，`next()` 再从队列里取。

这里有一个把 C 回调接回 C++ 对象的常用技巧。C 函数只接受普通的函数指针，不能直接传成员函数或者捕获了变量的 lambda。好在 C 风格的回调 API 几乎都会多带一个 `void* user` 参数，原样传回给回调。于是：

<!-- run: off -->
```cpp
capture_run(frames, [](void* user, const unsigned char* d, int len, double t) {   // 不捕获变量的 lambda，可以转换成函数指针
    static_cast<CaptureSource*>(user)->queue_.push_back(...);                    // 把 void* 转回对象指针
}, this);                                                                         // this 作为 user 传进去
```

本例的采集是同步完成的（`capture_run` 返回时所有帧都已经推完了）。真实的采集设备通常在另一个线程里不断推送数据，这时队列就要加锁，`next()` 在队列为空时要等待（练习 3）。

## 7. 标准库里的适配器

C++ 标准库里有大量适配器，你可能每天都在用：

```cpp
#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <stack>
#include <vector>

// 一个旧函数：参数顺序和单位都和我们需要的不一样
double legacyVolume(int channel, double percent) { return percent / 100.0 + channel * 0; }

int main() {
    // 容器适配器：std::stack 把 vector 的接口（push_back / back / pop_back）适配成栈的接口（push / top / pop）
    std::stack<int, std::vector<int>> undo;
    for (int i = 1; i <= 3; ++i) undo.push(i);
    std::cout << "栈顶 " << undo.top() << '\n';

    // 迭代器适配器：back_inserter 把「赋值给迭代器」适配成「push_back」
    std::vector<int> src = {3, 1, 2}, dst;
    std::copy(src.begin(), src.end(), std::back_inserter(dst));
    // reverse_iterator 把「往前走」适配成「往后走」
    std::cout << "倒着输出 ";
    std::copy(dst.rbegin(), dst.rend(), std::ostream_iterator<int>(std::cout, " "));
    std::cout << '\n';

    // 函数适配器：新代码需要 function<double(double)>（输入 0～1 的音量），旧函数是 (声道, 百分比)
    std::function<double(double)> setVolume = [](double v) { return legacyVolume(0, v * 100); };
    std::cout << "音量 " << setVolume(0.5) << '\n';
}
```

```text
栈顶 3
倒着输出 2 1 3 
音量 0.5
```

- **容器适配器**：`std::stack`、`std::queue`、`std::priority_queue` 本身不存储数据，而是把底层容器（默认是 `std::deque`）的接口适配成栈、队列、堆的接口。
- **迭代器适配器**：`std::back_inserter` 把「通过迭代器赋值」适配成 `push_back`；`reverse_iterator` 把「往前走」适配成「往后走」；C++20 的 ranges 里还有一整套视图适配器（`views::filter`、`views::transform`……）。
- **函数适配器**：用 lambda 把参数顺序、单位不同的旧函数，适配成新代码需要的签名。早年的 `std::bind1st`、`std::mem_fun` 就是做这件事的，现在都被 lambda 取代了。

## 8. 适配器和它的「亲戚」

结构型模式里，有好几个模式都是「一个对象包着另一个对象」：

| 模式 | 为什么包这一层 | 接口变了吗 |
| --- | --- | --- |
| **适配器** | 让接口不兼容的对象能一起工作 | **变了**：从被适配者的接口变成目标接口 |
| 外观 | 给一个复杂的子系统提供一个简单的入口 | 新定义一个更简单的接口，包的是一整个子系统 |
| 装饰器 | 在不改变接口的前提下增加功能 | 不变 |
| 代理 | 控制对对象的访问（延迟创建、权限、远程） | 不变 |

一个简单的判断方法：**如果包了一层之后接口没变，就不是适配器**。上一章《抽象工厂》里提到的 `hwdownload`（把 GPU 帧转换成内存帧，让 VideoToolbox 族的解码器能接软件族的滤镜）就是一个典型的适配器。

## 9. 小结

- **适配器**：把一个类的接口转换成客户需要的另一个接口。目标接口和被适配者都不能改时使用
- **对象适配器**：实现目标接口，内部持有被适配者，把调用翻译过去。优先使用
- **类适配器**：公有继承目标接口，私有继承被适配者。只在需要重写被适配者的虚函数、或访问其 `protected` 成员时使用
- 把 C API 适配成 C++ 接口：资源交给 `unique_ptr` 加自定义删除器，错误码翻译成 `optional` 或错误信息。实测：读测试视频得到 240 个视频包、470 个音频包，与 ffprobe 一致
- 实测：提前返回的场景，直接用 C API 10 次调用泄漏了约 5 MB（macOS）/ 3.6 MB（Linux），通过适配器没有泄漏
- 推转拉适配器：回调把数据放进队列，`next()` 从队列取；用 `void* user` 把 C 回调接回 C++ 对象
- 标准库里的 `stack`、`back_inserter`、`reverse_iterator` 都是适配器
- 包了一层之后接口变了才是适配器；不变的是装饰器或代理，包一整个子系统的是外观

## 10. 练习

1. 下面的「包一层」分别是适配器、外观、装饰器还是代理？
   ① `std::queue<int>`，底层用 `std::deque<int>`；
   ② 给一个 `Logger` 包一层，每条日志前面自动加上时间戳，接口还是 `log(msg)`；
   ③ `Player::play(url)` 内部调用了解封装、解码、音视频同步、渲染等十几个类；
   ④ `std::shared_ptr<T>`。

2. 给 `FfmpegSource` 加一个 `bool seek(double seconds)`，跳转到指定的时间。FFmpeg 对应的函数是 `av_seek_frame(AVFormatContext*, int stream_index, int64_t timestamp, int flags)`，`stream_index` 传 -1 时 `timestamp` 的单位是 `AV_TIME_BASE`（微秒），返回负数表示失败。

3. 第 6 节的推转拉适配器里，采集是同步完成的。如果真实的采集库在自己的线程里一直调用回调（`capture_start(cb, user)` 立即返回，之后在后台线程推数据，`capture_stop()` 停止），`CaptureSource` 要怎么改？

4. `LegacyWavReader` 后来有了一个子类 `LegacyFlacReader`，接口完全相同。第 5 节的类适配器 `WavSource` 能直接用来适配 FLAC 吗？如果要让同一个适配器能适配 `LegacyWavReader` 的任何子类，该怎么写？

<details>
<summary>点击查看答案</summary>

1. ① 适配器（容器适配器）：把 `deque` 的接口转换成队列的 `push` / `front` / `pop`。
   ② 装饰器：接口不变，增加了「加时间戳」的功能。
   ③ 外观：给一整个子系统提供一个简单的入口。
   ④ 代理：接口和裸指针一样（`*`、`->`），增加的是生命周期的控制（引用计数）。

2. 在 `FfmpegSource` 里加一个函数，把秒转换成微秒，再把错误码翻译成 `bool`：

   <!-- run: off -->
   ```cpp
   bool FfmpegSource::seek(double seconds) {
       int64_t ts = static_cast<int64_t>(seconds * AV_TIME_BASE);
       if (av_seek_frame(fmt_.get(), -1, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;   // 跳到 ts 之前最近的关键帧
       return true;
   }
   ```

   `AVSEEK_FLAG_BACKWARD` 表示跳到目标时间之前最近的关键帧，这样之后解码出来的第一帧是完整的。音视频专栏的《暂停与跳转》讲过为什么要这样做。注意 `seek` 应该同时加进 `MediaSource` 接口（如果所有数据源都支持跳转），或者单独定义一个 `SeekableSource` 接口（接口隔离原则：不是所有数据源都能跳转，比如采集设备）。

3. 回调在采集库的线程里执行，`next()` 在播放器的线程里执行，队列被两个线程同时访问，要加锁；`next()` 在队列为空时要等待，而不是直接返回空：

   <!-- run: off -->
   ```cpp
   class CaptureSource : public MediaSource {
   public:
       CaptureSource() { capture_start(&CaptureSource::onFrame, this); }
       ~CaptureSource() override { capture_stop(); }        // 停止之后，回调不会再被调用，才能安全析构

       std::optional<Packet> next() override {
           std::unique_lock<std::mutex> lock(mu_);
           cv_.wait(lock, [this] { return !queue_.empty() || stopped_; });   // 没有数据就等
           if (queue_.empty()) return std::nullopt;                          // 已经停止，且数据取完了
           Packet p = std::move(queue_.front());
           queue_.pop_front();
           return p;
       }
       void stop() { { std::lock_guard<std::mutex> lock(mu_); stopped_ = true; } cv_.notify_all(); }

   private:
       static void onFrame(void* user, const unsigned char* d, int len, double t) {   // 在采集线程里执行
           auto* self = static_cast<CaptureSource*>(user);
           { std::lock_guard<std::mutex> lock(self->mu_); self->queue_.push_back({t, {d, d + len}}); }
           self->cv_.notify_one();
       }
       std::mutex mu_;
       std::condition_variable cv_;
       std::deque<Packet> queue_;
       bool stopped_ = false;
   };
   ```

   这就是 C/C++ 专栏《条件变量与 async / future》里的生产者-消费者模型。真实代码还要考虑队列的上限：如果播放器取得比采集慢，队列会无限增长，常见的做法是限制长度，满了就丢弃最旧的帧。

4. 不能：`WavSource` 私有继承的是 `LegacyWavReader` 这个固定的类，要适配 FLAC 就得再写一个继承 `LegacyFlacReader` 的类适配器。改成对象适配器，持有一个基类指针，就能在运行时传入任何子类：

   <!-- run: off -->
   ```cpp
   class LegacyAudioSource : public MediaSource {
   public:
       explicit LegacyAudioSource(std::unique_ptr<LegacyWavReader> reader) : reader_(std::move(reader)) {}
       std::optional<Packet> next() override { /* 调用 reader_->readChunk(...) */ }
   private:
       std::unique_ptr<LegacyWavReader> reader_;   // 可以是 LegacyWavReader，也可以是 LegacyFlacReader
   };
   ```

   前提是 `readChunk` 等函数是虚函数，`LegacyFlacReader` 重写了它们。这也是第 5 节表格里「对象适配器能适配被适配者的子类」的含义。

</details>
