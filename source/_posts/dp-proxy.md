---
title: 代理模式
date: 2026-11-11 20:00:00
updated: 2026-11-11 20:00:00
categories:
  - 设计模式
  - 结构型模式
tags:
  - 设计模式
  - 代理
  - 延迟加载
  - 缓存
  - 智能指针
  - FFmpeg
order: 7
description: 媒体库一打开就把 120 个视频的缩略图全部解码，要 94.48 ms、占 6.6 MB 内存，可屏幕上一次只显示 12 张。虚代理先放一个只记住路径的替身，第一次显示时才解码：打开变成 0.003 ms，显示第一屏 9.43 ms，内存 681 KB；Gallery 的代码一行不用改。用 FFmpeg 真正解码 20 张时间轴缩略图，第一屏从 639.0 ms 降到 65.7 ms。再讲保护代理（缓存代理要套在它里面，否则游客能拿到会员的片源）、LRU 缓存代理（容量 20 时命中 56.2%，读流信息平均每次从 5100 µs 降到 2212 µs）、远程代理、shared_ptr 和 vector<bool>::reference 这样的智能引用，以及 auto 接住代理对象时的悬空坑，最后和装饰器、适配器、外观对比。
top_group_list: true
disableNunjucks: true
viz: [dp-proxy]
---

> **本文目标**：理解代理模式：替身和真实对象实现同一个接口，调用方分不出来，替身在转发前后控制访问；会写虚代理、保护代理、缓存代理，知道远程代理和智能引用也是代理；量出代理省了什么、花了什么；认识 `vector<bool>::reference` 这类代理对象和 `auto` 的坑；分清代理和装饰器、适配器、外观。
>
> **前置知识**：上一篇《享元模式》；《装饰器模式》（结构和代理几乎一样）、《外观模式》（同样用 FFmpeg 解码缩略图）；C/C++ 专栏的《智能指针：unique_ptr、shared_ptr 与 weak_ptr》；音视频专栏里解封装、解码、关键帧的概念。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/17-proxy/` 目录，`sh measure.sh` 用 `-O2` 跑一遍本文的实测；FFmpeg 的部分在 `ffmpeg/` 子目录（`./build.sh && ./run.sh`）。

## 1. 问题：一打开就解码 120 张缩略图

### 1.1 先把真实对象全建出来

播放器的媒体库里有 120 个视频，每个显示一张缩略图。缩略图要从视频里解码一帧、缩放成 160×90，这一步很慢（《外观模式》里实测过，用 FFmpeg 存一张缩略图约 32 ms）。屏幕上一次只放得下 12 张：

<figure class="diagram">
<svg viewBox="0 0 668 360" role="img" aria-label="媒体库有 120 个视频，排成 10 行 12 列；屏幕上一次只显示第一行的 12 张，其余 108 张在用户翻到之前都用不着"><g style="font-family:var(--mono)"><text x="10.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">媒体库里的 120 个视频（每格一张缩略图）</text><rect x="10" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="42" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="74" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="106" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="138" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="170" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="202" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="234" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="266" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="298" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="330" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="362" y="34" width="26" height="26" rx="3" style="fill:var(--accent);opacity:0.85"/><rect x="10" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="66" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="10" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="98" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="10" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="130" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="10" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="162" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="10" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="194" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="10" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="226" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="10" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="258" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="10" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="290" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="10" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="42" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="74" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="106" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="138" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="170" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="202" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="234" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="266" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="298" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="330" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="362" y="322" width="26" height="26" rx="3" style="fill:var(--warn);opacity:0.35"/><rect x="6" y="30" width="386" height="34" rx="5" style="fill:none;stroke:var(--accent);stroke-width:2"/><text x="418.0" y="52.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">← 屏幕上一次只显示这 12 张</text><text x="418.0" y="144.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">其余 108 张：</text><text x="418.0" y="164.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">用户翻到之前用不着，</text><text x="418.0" y="184.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">可能永远用不着</text><text x="418.0" y="234.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">全部解码：打开就要解码 120 次</text></g></svg>
</figure>

最直接的写法：`Gallery` 构造时给每个视频创建一个 `VideoThumb`，`VideoThumb` 的构造函数里解码。下面用一段计算模拟解码的开销，再重载全局的 `operator new` 数一数堆内存：

```cpp
// 《代理模式》：不用代理——媒体库一打开就把 120 个视频的缩略图全部解码，可屏幕上只显示 12 个
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

// ---- 统计堆内存：当前占用 g_live（和代理模式无关，只为了量出内存）----
static std::size_t g_live = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) std::abort();
    *p = n;
    g_live += n;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    auto* p = reinterpret_cast<std::size_t*>(static_cast<char*>(q) - 16);
    g_live -= *p;
    std::free(p);
}
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }
// ---------------------------------------------------------------------

constexpr int kW = 160, kH = 90;          // 缩略图 160×90，RGBA，每张 57600 字节
constexpr int kPerPage = 12;              // 一屏显示 12 张（3 行 × 4 列）
static int g_decodes = 0;
static std::uint32_t g_screen = 0;        // 「屏幕」：draw 往这里写，防止被优化掉

struct Thumbnail {
    virtual ~Thumbnail() = default;
    virtual const std::string& title() const = 0;
    virtual void draw(int slot) = 0;      // 画到屏幕的第 slot 格
};

// 真实对象：构造时就解码视频的一帧（这里用一段计算模拟解码的开销）
class VideoThumb final : public Thumbnail {
public:
    explicit VideoThumb(std::string path) : path_(std::move(path)), pixels_(kW * kH * 4) {
        ++g_decodes;
        std::uint32_t h = 2166136261u;
        for (char c : path_) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        for (int pass = 0; pass < 16; ++pass)
            for (auto& b : pixels_) { h = h * 1664525u + 1013904223u; b ^= static_cast<std::uint8_t>(h >> 24); }
    }
    const std::string& title() const override { return path_; }
    void draw(int slot) override { g_screen += pixels_[slot * 4] + static_cast<std::uint32_t>(slot); }

private:
    std::string path_;
    std::vector<std::uint8_t> pixels_;
};

class Gallery {
public:
    explicit Gallery(int n) {
        for (int i = 0; i < n; ++i)
            items_.push_back(std::make_unique<VideoThumb>("clip" + std::to_string(i) + ".mp4"));
    }
    void show_page(int page) {
        for (int slot = 0; slot < kPerPage; ++slot) {
            std::size_t i = static_cast<std::size_t>(page * kPerPage + slot);
            if (i < items_.size()) items_[i]->draw(slot);
        }
    }

private:
    std::vector<std::unique_ptr<Thumbnail>> items_;
};

int main() {
    Gallery g(120);
    std::printf("打开媒体库：解码 %3d 次，堆内存 %5zu KB\n", g_decodes, g_live / 1024);
    g.show_page(0);
    std::printf("显示第 1 屏：解码 %3d 次，堆内存 %5zu KB（屏幕上只有 %d 张）\n", g_decodes, g_live / 1024, kPerPage);
}
```

输出：

```text
打开媒体库：解码 120 次，堆内存  6757 KB
显示第 1 屏：解码 120 次，堆内存  6757 KB（屏幕上只有 12 张）
```

### 1.2 问题在哪

- **启动慢**：打开媒体库的耗时和视频总数成正比，和屏幕上显示几张无关。`-O2` 实测，打开要 94.48 ms（Clang）/ 94.94 ms（GCC），这段时间界面是卡住的。真正用 FFmpeg 解码时更慢，见 2.5 节。
- **内存大**：120 张全部常驻，6757 KB；有 1000 个视频就是 55 MB，用户可能只看了第一屏就关掉了。
- **很多时候根本不需要像素**：按标题搜索、按时长排序、显示总数，只用到路径和元数据，却也得等全部解码完。

想要的是：**还没显示的缩略图先不解码**，但不想为此改 `Gallery` 的代码。`Gallery` 只认识 `Thumbnail` 接口，调用 `draw()`、`title()`；它不应该关心一张缩略图有没有解码过。

## 2. 代理模式

### 2.1 意图

GoF 的定义：

> 为其他对象提供一种代理以控制对这个对象的访问。

大白话：**做一个替身，和真实对象长得一模一样（实现同一个接口），调用方拿着替身用，分不出来；替身在把调用转给真实对象之前或之后，插一点自己的逻辑**——推迟创建、检查权限、记住结果、跨网络转发、计数……插的逻辑不同，就是不同种类的代理：

<figure class="diagram">
<svg viewBox="0 0 680 290" role="img" aria-label="五种代理：虚代理推迟创建，保护代理检查权限，缓存代理记住结果，远程代理跨进程转发，智能引用在访问时计数加锁；结构都是调用方、代理、真实对象，代理和真实对象实现同一个接口"><g style="font-family:var(--mono)"><text x="10.0" y="16.0" text-anchor="start" style="fill:var(--muted);font-size:11px">调用方</text><text x="140.0" y="16.0" text-anchor="start" style="fill:var(--muted);font-size:11px">代理（和真实对象同一个接口）在转发前后做什么</text><text x="560.0" y="16.0" text-anchor="start" style="fill:var(--muted);font-size:11px">真实对象</text><rect x="10" y="30" width="90" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="55.0" y="55.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">调用方</text><line x1="100" y1="50.0" x2="134" y2="50.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M140,50.0 L132,45.0 L132,55.0 z" style="fill:var(--muted)"/><rect x="140" y="30" width="380" height="40" rx="4" style="fill:none;stroke:var(--accent);stroke-width:1.2"/><text x="152.0" y="47.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">虚代理</text><text x="240.0" y="47.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">第一次用到时才创建真实对象</text><text x="152.0" y="63.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">例：缩略图、大文件、字幕</text><line x1="520" y1="50.0" x2="554" y2="50.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M560,50.0 L552,45.0 L552,55.0 z" style="fill:var(--muted)"/><rect x="560" y="30" width="110" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="615.0" y="55.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">RealSubject</text><rect x="10" y="82" width="90" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="55.0" y="107.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">调用方</text><line x1="100" y1="102.0" x2="134" y2="102.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M140,102.0 L132,97.0 L132,107.0 z" style="fill:var(--muted)"/><rect x="140" y="82" width="380" height="40" rx="4" style="fill:none;stroke:var(--accent);stroke-width:1.2"/><text x="152.0" y="99.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">保护代理</text><text x="240.0" y="99.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">先检查权限，没有就拒绝</text><text x="152.0" y="115.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">例：按角色限制播放、删除</text><line x1="520" y1="102.0" x2="554" y2="102.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M560,102.0 L552,97.0 L552,107.0 z" style="fill:var(--muted)"/><rect x="560" y="82" width="110" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="615.0" y="107.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">RealSubject</text><rect x="10" y="134" width="90" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="55.0" y="159.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">调用方</text><line x1="100" y1="154.0" x2="134" y2="154.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M140,154.0 L132,149.0 L132,159.0 z" style="fill:var(--muted)"/><rect x="140" y="134" width="380" height="40" rx="4" style="fill:none;stroke:var(--accent);stroke-width:1.2"/><text x="152.0" y="151.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">缓存代理</text><text x="240.0" y="151.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">查过的结果记住，下次直接返回</text><text x="152.0" y="167.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">例：视频元数据、签名后的地址</text><line x1="520" y1="154.0" x2="554" y2="154.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M560,154.0 L552,149.0 L552,159.0 z" style="fill:var(--muted)"/><rect x="560" y="134" width="110" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="615.0" y="159.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">RealSubject</text><rect x="10" y="186" width="90" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="55.0" y="211.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">调用方</text><line x1="100" y1="206.0" x2="134" y2="206.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M140,206.0 L132,201.0 L132,211.0 z" style="fill:var(--muted)"/><rect x="140" y="186" width="380" height="40" rx="4" style="fill:none;stroke:var(--accent);stroke-width:1.2"/><text x="152.0" y="203.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">远程代理</text><text x="240.0" y="203.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">把调用打包成消息发到另一个进程</text><text x="152.0" y="219.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">例：gRPC stub、浏览器 GPU 进程</text><line x1="520" y1="206.0" x2="554" y2="206.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M560,206.0 L552,201.0 L552,211.0 z" style="fill:var(--muted)"/><rect x="560" y="186" width="110" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="615.0" y="211.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">RealSubject</text><rect x="10" y="238" width="90" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="55.0" y="263.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">调用方</text><line x1="100" y1="258.0" x2="134" y2="258.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M140,258.0 L132,253.0 L132,263.0 z" style="fill:var(--muted)"/><rect x="140" y="238" width="380" height="40" rx="4" style="fill:none;stroke:var(--accent);stroke-width:1.2"/><text x="152.0" y="255.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">智能引用</text><text x="240.0" y="255.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">访问时顺便计数、加锁、记录</text><text x="152.0" y="271.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">例：shared_ptr、vector&lt;bool&gt;::reference</text><line x1="520" y1="258.0" x2="554" y2="258.0" style="stroke:var(--muted);stroke-width:1.4"/><path d="M560,258.0 L552,253.0 L552,263.0 z" style="fill:var(--muted)"/><rect x="560" y="238" width="110" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="615.0" y="263.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">RealSubject</text></g></svg>
</figure>

这几种代理的共同点是**接口不变、一对一**：一个代理代表一个真实对象，调用方用的还是原来那个接口。

### 2.2 用虚代理重构

`ThumbProxy` 和 `VideoThumb` 实现同一个 `Thumbnail` 接口。它只记住路径，`real_` 一开始是空的；第一次有人要像素（`draw()`）时才创建 `VideoThumb`。`title()` 不需要像素，代理自己就能回答：

```cpp
// 《代理模式》：虚代理——ThumbProxy 先占位，第一次 draw 时才创建真正的 VideoThumb
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

// ---- 统计堆内存：当前占用 g_live（和代理模式无关，只为了量出内存）----
static std::size_t g_live = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) std::abort();
    *p = n;
    g_live += n;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    auto* p = reinterpret_cast<std::size_t*>(static_cast<char*>(q) - 16);
    g_live -= *p;
    std::free(p);
}
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }
// ---------------------------------------------------------------------

constexpr int kW = 160, kH = 90;          // 缩略图 160×90，RGBA，每张 57600 字节
constexpr int kPerPage = 12;              // 一屏显示 12 张（3 行 × 4 列）
static int g_decodes = 0;
static std::uint32_t g_screen = 0;        // 「屏幕」：draw 往这里写，防止被优化掉

struct Thumbnail {
    virtual ~Thumbnail() = default;
    virtual const std::string& title() const = 0;
    virtual void draw(int slot) = 0;      // 画到屏幕的第 slot 格
};

// 真实对象：构造时就解码视频的一帧（这里用一段计算模拟解码的开销）
class VideoThumb final : public Thumbnail {
public:
    explicit VideoThumb(std::string path) : path_(std::move(path)), pixels_(kW * kH * 4) {
        ++g_decodes;
        std::uint32_t h = 2166136261u;
        for (char c : path_) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        for (int pass = 0; pass < 16; ++pass)
            for (auto& b : pixels_) { h = h * 1664525u + 1013904223u; b ^= static_cast<std::uint8_t>(h >> 24); }
    }
    const std::string& title() const override { return path_; }
    void draw(int slot) override { g_screen += pixels_[slot * 4] + static_cast<std::uint32_t>(slot); }

private:
    std::string path_;
    std::vector<std::uint8_t> pixels_;
};

// 虚代理：和 VideoThumb 实现同一个接口，只记住路径；第一次需要像素时才创建真实对象
class ThumbProxy final : public Thumbnail {
public:
    explicit ThumbProxy(std::string path) : path_(std::move(path)) {}
    const std::string& title() const override { return path_; }   // 不用解码就能回答
    void draw(int slot) override { real().draw(slot); }

private:
    VideoThumb& real() {
        if (!real_) real_ = std::make_unique<VideoThumb>(path_);
        return *real_;
    }
    std::string path_;
    std::unique_ptr<VideoThumb> real_;   // 还没创建时是空的
};

class Gallery {
public:
    explicit Gallery(int n) {
        for (int i = 0; i < n; ++i)
            items_.push_back(std::make_unique<ThumbProxy>("clip" + std::to_string(i) + ".mp4"));
    }
    void show_page(int page) {
        for (int slot = 0; slot < kPerPage; ++slot) {
            std::size_t i = static_cast<std::size_t>(page * kPerPage + slot);
            if (i < items_.size()) items_[i]->draw(slot);
        }
    }
    int count_title(const std::string& key) const {   // 按标题搜索：只用到 title()
        int n = 0;
        for (auto& t : items_) n += t->title().find(key) != std::string::npos;
        return n;
    }

private:
    std::vector<std::unique_ptr<Thumbnail>> items_;
};

int main() {
    Gallery g(120);   // 唯一的改动在 Gallery 的构造函数里：make_unique<ThumbProxy>
    std::printf("打开媒体库：解码 %3d 次，堆内存 %5zu KB\n", g_decodes, g_live / 1024);
    int n = g.count_title("clip1");
    std::printf("搜索 clip1：命中 %2d 个，解码 %3d 次\n", n, g_decodes);
    g.show_page(0);
    std::printf("显示第 1 屏：解码 %3d 次，堆内存 %5zu KB\n", g_decodes, g_live / 1024);
    g.show_page(1);
    std::printf("翻到第 2 屏：解码 %3d 次，堆内存 %5zu KB\n", g_decodes, g_live / 1024);
    g.show_page(0);
    std::printf("翻回第 1 屏：解码 %3d 次，堆内存 %5zu KB\n", g_decodes, g_live / 1024);
    for (int p = 0; p < 10; ++p) g.show_page(p);
    std::printf("翻完 10 屏 ：解码 %3d 次，堆内存 %5zu KB\n", g_decodes, g_live / 1024);
}
```

输出：

```text
打开媒体库：解码   0 次，堆内存     5 KB
搜索 clip1：命中 31 个，解码   0 次
显示第 1 屏：解码  12 次，堆内存   681 KB
翻到第 2 屏：解码  24 次，堆内存  1357 KB
翻回第 1 屏：解码  24 次，堆内存  1357 KB
翻完 10 屏 ：解码 120 次，堆内存  6762 KB
```

和第 1 节比，`Gallery` 的 `show_page()` 一行没改，只是构造函数里 `make_unique<VideoThumb>` 换成了 `make_unique<ThumbProxy>`（真实项目里，这一行通常在工厂里）。几个值得注意的地方：

- **打开时不解码**：120 个代理一共 5 KB（每个代理一个虚表指针、一个短字符串、一个空的 `unique_ptr`）。
- **搜索不解码**：`title()` 由代理直接回答。代理能自己回答的问题越多，真实对象被创建的机会就越少。
- **翻回去不再解码**：`real_` 已经有了，代理只是转发。
- **翻完 10 屏，内存和全部解码一样**：虚代理只是把解码从「打开时」挪到了「第一次用到时」，没有减少总工作量。想让内存有上限，代理还要能卸载，见练习 2。

下面的演示把两种写法放在一起，6 张缩略图、一屏 3 张。点「全部解码」「虚代理」比较解码发生在什么时候；「缓存代理」在第 3.2 节用到：

<div class="dsviz" data-viz="dp-proxy"></div>

### 2.3 类图和时序图

```uml
label: 代理模式：Gallery 只依赖 Thumbnail 接口；ThumbProxy 和 VideoThumb 都实现 Thumbnail；ThumbProxy 持有 0 或 1 个 VideoThumb，第一次 draw 时才创建
classes:
  - { id: Gallery, x: 20, y: 40, w: 150, ops: ['+ show_page(page)'] }
  - { id: Thumbnail, stereo: interface, x: 330, y: 20, w: 190, ops: ['*+ title() : string', '*+ draw(slot)'] }
  - { id: ThumbProxy, x: 120, y: 200, w: 210, attrs: ['- path_ : string', '- real_ : unique_ptr<VideoThumb>'], ops: ['+ title() : string', '+ draw(slot)', '- real() : VideoThumb&'] }
  - { id: VideoThumb, x: 600, y: 200, w: 190, attrs: ['- path_ : string', '- pixels_ : vector<uint8_t>'], ops: ['+ VideoThumb(path)', '+ title() : string', '+ draw(slot)'] }
rels:
  - { from: Gallery, to: Thumbnail, type: aggregate, tm: '*' }
  - { from: ThumbProxy, to: Thumbnail, type: realize, mid: 160, tdx: -40 }
  - { from: VideoThumb, to: Thumbnail, type: realize, mid: 160, tdx: 40 }
  - { from: ThumbProxy, to: VideoThumb, type: compose, tm: '0..1', label: '第一次 draw 才创建', fdx: -20, tdx: -20 }
notes:
  - { x: 20, y: 380, w: 300, text: ['draw(slot) {', '  if (!real_) real_ = make_unique<VideoThumb>(path_);', '  real_->draw(slot);', '}'], to: ThumbProxy }
```

和类图对照着看：

- **Subject（`Thumbnail`）**：调用方依赖的接口，代理和真实对象都实现它。正因为接口相同，`Gallery` 才分不出拿到的是谁。
- **RealSubject（`VideoThumb`）**：真正干活的对象，它不知道代理的存在。
- **Proxy（`ThumbProxy`）**：持有（或者知道怎么得到）真实对象，把调用转过去，转发前后做自己的事。

同一个代理被画两次，时序是这样的：

```uml
label: 时序图：第一次 draw 时代理创建 VideoThumb、解码，再转发；第二次 draw 直接转发
participants:
  - { id: g, name: 'Gallery', x: 20, w: 110 }
  - { id: p, name: 'ThumbProxy', x: 230, w: 130 }
  - { id: r, name: 'VideoThumb', x: 540, w: 130 }
messages:
  - { from: g, to: p, text: 'draw(0)', n: 1 }
  - { from: p, to: r, text: 'new VideoThumb（解码）', type: create }
  - { from: p, to: r, text: 'draw(0)' }
  - { from: r, to: p, text: '', type: reply }
  - { from: p, to: g, text: '', type: reply }
  - { from: g, to: p, text: 'draw(0)', n: 2 }
  - { from: p, to: r, text: 'draw(0)', note: 'real_ 已经有了' }
  - { from: r, to: p, text: '', type: reply }
  - { from: p, to: g, text: '', type: reply }
```

### 2.4 实测：省了多少，花了多少

`bench_lazy.cpp` 量两件事：打开媒体库并显示第一屏要多久；解码之后，每次 `draw()` 经过代理比直接调真实对象多花多少：

<details>
<summary>展开 bench_lazy.cpp</summary>

<!-- run: off -->
```cpp
// 《代理模式》：实测虚代理——打开媒体库并显示第一屏的耗时，以及解码之后每次 draw 多绕一层代理的开销（请用 -O2）
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

constexpr int kW = 160, kH = 90, kPerPage = 12, kItems = 120;
static int g_decodes = 0;
static std::uint32_t g_screen = 0;

struct Thumbnail {
    virtual ~Thumbnail() = default;
    virtual void draw(int slot) = 0;
};

class VideoThumb final : public Thumbnail {
public:
    explicit VideoThumb(std::string path) : path_(std::move(path)), pixels_(kW * kH * 4) {
        ++g_decodes;
        std::uint32_t h = 2166136261u;
        for (char c : path_) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        for (int pass = 0; pass < 16; ++pass)
            for (auto& b : pixels_) { h = h * 1664525u + 1013904223u; b ^= static_cast<std::uint8_t>(h >> 24); }
    }
    void draw(int slot) override { g_screen += pixels_[static_cast<std::size_t>(slot) * 4 % pixels_.size()] + static_cast<std::uint32_t>(slot); }

private:
    std::string path_;
    std::vector<std::uint8_t> pixels_;
};

class ThumbProxy final : public Thumbnail {
public:
    explicit ThumbProxy(std::string path) : path_(std::move(path)) {}
    void draw(int slot) override {
        if (!real_) real_ = std::make_unique<VideoThumb>(path_);
        real_->draw(slot);
    }

private:
    std::string path_;
    std::unique_ptr<VideoThumb> real_;
};

template <class T>
std::vector<std::unique_ptr<Thumbnail>> open_library() {
    std::vector<std::unique_ptr<Thumbnail>> v;
    for (int i = 0; i < kItems; ++i) v.push_back(std::make_unique<T>("clip" + std::to_string(i) + ".mp4"));
    return v;
}

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

template <class T>
void startup(const char* name) {
    std::vector<double> open_ms, first_ms;
    for (int r = 0; r < 7; ++r) {
        g_decodes = 0;
        auto t0 = Clock::now();
        auto lib = open_library<T>();
        open_ms.push_back(ms_since(t0));
        for (int s = 0; s < kPerPage; ++s) lib[static_cast<std::size_t>(s)]->draw(s);
        first_ms.push_back(ms_since(t0));
    }
    std::printf("%s 打开 %8.3f ms   打开并显示第一屏 %7.2f ms   解码 %3d 次\n", name, median(open_ms), median(first_ms), g_decodes);
}

// 解码完成之后，每次 draw 经过代理比直接调真实对象多花多少
void per_call() {
    constexpr int kCalls = 20'000'000;
    VideoThumb real("clip0.mp4");
    ThumbProxy proxy("clip0.mp4");
    std::vector<Thumbnail*> objs{&real, &proxy};
    objs[1]->draw(0);   // 先让代理把真实对象创建出来
    for (int k = 0; k < 2; ++k) {
        std::vector<double> ns;
        for (int r = 0; r < 7; ++r) {
            Thumbnail* t = objs[static_cast<std::size_t>(k)];
            auto t0 = Clock::now();
            for (int i = 0; i < kCalls; ++i) t->draw(i & 1023);
            ns.push_back(ms_since(t0) * 1e6 / kCalls);
        }
        std::printf("%s 每次 draw %.2f ns\n", k == 0 ? "直接调  ：" : "经过代理：", median(ns));
    }
}

int main() {
    startup<VideoThumb>("全部解码：");
    startup<ThumbProxy>("虚代理  ：");
    per_call();
    std::printf("(校验和 %u)\n", g_screen);
}
```

</details>

<figure class="diagram">
<svg viewBox="0 0 700 126" role="img" aria-label="打开媒体库并显示第一屏的耗时：全部解码 Clang 94.48 毫秒、GCC 94.94 毫秒；虚代理 Clang 9.43 毫秒、GCC 9.49 毫秒"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="158.0" y="30.0" width="170.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="334.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">94.48 ms</text><rect x="158.0" y="60.0" width="17.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="181.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">9.43 ms</text><text x="428.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="428.0" y="30.0" width="170.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="604.9" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">94.94 ms</text><rect x="428.0" y="60.0" width="17.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="451.1" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">9.49 ms</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">全部解码（120 次）</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚代理（12 次）</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_lazy.cpp：打开媒体库并显示第一屏（12 张），-O2，7 次取中位数。虚代理只打开不显示时是 0.003 / 0.004 ms</text></g></svg>
</figure>

<figure class="diagram">
<svg viewBox="0 0 730 186" role="img" aria-label="堆内存：全部解码 6757 KB；虚代理显示第一屏 681 KB，翻完 10 屏 6762 KB；加上卸载（练习 2）翻完 10 屏 1361 KB。GCC 相差 1 到 3 KB"><g style="font-family:var(--mono)"><text x="188.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="188.0" y="30.0" width="173.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="367.8" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">6757 KB</text><rect x="188.0" y="60.0" width="17.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="211.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">681 KB</text><rect x="188.0" y="90.0" width="173.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="367.9" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">6762 KB</text><rect x="188.0" y="120.0" width="35.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="229.0" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1361 KB</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="458.0" y="30.0" width="173.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="637.8" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">6758 KB</text><rect x="458.0" y="60.0" width="17.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="481.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">682 KB</text><rect x="458.0" y="90.0" width="173.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="637.9" y="105.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">6764 KB</text><rect x="458.0" y="120.0" width="34.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="498.9" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1358 KB</text><text x="180.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">全部解码</text><text x="180.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚代理·第一屏</text><text x="180.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚代理·翻完 10 屏</text><text x="180.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚代理+卸载·翻完 10 屏</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">no_proxy / virtual_proxy / ex2_unload：重载全局 operator new 统计当前占用的堆内存</text></g></svg>
</figure>

- **启动**：全部解码要 94.48 / 94.94 ms；虚代理打开只要 0.003 / 0.004 ms（120 次 `make_unique` 和字符串拼接），显示第一屏 9.43 / 9.49 ms，正好是 12 / 120 = 1/10。
- **内存**：第一屏 681 KB，是全部解码的 1/10。两个编译器得到的字节数只差 1 KB 左右：libc++ 的 std::string 是 24 字节，libstdc++ 是 32 字节，每个对象多 8 字节。
- **每次调用**：解码完之后，经过代理每次 `draw()` 多一次判空和一次函数调用，0.64 → 1.08 ns（Clang）、0.64 → 1.61 ns（GCC），多出来的不到 1 ns。和解码（每张约 0.79 ms）比可以忽略；但如果真实对象的操作本身就只要几纳秒、又在最内层循环里被调用上亿次，就要掂量一下了。

### 2.5 用 FFmpeg 真正解码

模拟的解码说明了道理，下面换成真的：给 `bbb-10s.mp4` 的时间轴做 20 张缩略图（0、0.5、1……9.5 秒），每张都要打开文件、seek 到目标之前的关键帧、一直解码到目标时间、缩放成 320×180。屏幕上一次只看得到 4 张：

<details>
<summary>展开 ffmpeg/lazy_strip.cpp</summary>

<!-- run: off -->
```cpp
// 《代理模式》：用 FFmpeg 真正解码——时间轴上 20 张缩略图，全部解码 vs 虚代理（只解码屏幕上的 4 张）
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

constexpr int kW = 320, kH = 180;     // 缩略图 320×180 RGB24
constexpr int kCount = 20;            // 时间轴：0、0.5、1……9.5 秒，共 20 张
constexpr int kVisible = 4;           // 屏幕上一次只看得到 4 张
static int g_decodes = 0;

struct Image { std::vector<std::uint8_t> rgb; };

struct Thumb {
    virtual ~Thumb() = default;
    virtual double time() const = 0;
    virtual const Image& image() = 0;
};

// 真实对象：构造时打开文件、seek、解码到目标时间、缩放成 320×180
class DecodedThumb final : public Thumb {
public:
    DecodedThumb(const std::string& path, double sec) : sec_(sec) {
        ++g_decodes;
        AVFormatContext* fmt = nullptr;
        if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return;
        std::unique_ptr<AVFormatContext*, void (*)(AVFormatContext**)> fmt_guard(&fmt, avformat_close_input);
        const AVCodec* codec = nullptr;
        if (avformat_find_stream_info(fmt, nullptr) < 0) return;
        int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (vs < 0) return;
        AVStream* st = fmt->streams[vs];
        AVCodecContext* dec = avcodec_alloc_context3(codec);
        std::unique_ptr<AVCodecContext*, void (*)(AVCodecContext**)> dec_guard(&dec, avcodec_free_context);
        avcodec_parameters_to_context(dec, st->codecpar);
        if (avcodec_open2(dec, codec, nullptr) < 0) return;
        AVPacket* pkt = av_packet_alloc();
        std::unique_ptr<AVPacket*, void (*)(AVPacket**)> pkt_guard(&pkt, av_packet_free);
        AVFrame* frame = av_frame_alloc();
        std::unique_ptr<AVFrame*, void (*)(AVFrame**)> frame_guard(&frame, av_frame_free);

        auto target = static_cast<std::int64_t>(sec / av_q2d(st->time_base));
        av_seek_frame(fmt, vs, target, AVSEEK_FLAG_BACKWARD);
        bool got = false;
        while (!got) {
            int ret = av_read_frame(fmt, pkt);
            if (ret < 0) avcodec_send_packet(dec, nullptr);
            else if (pkt->stream_index == vs) avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            while (!got && avcodec_receive_frame(dec, frame) >= 0) got = frame->best_effort_timestamp >= target;
            if (ret < 0 && !got) return;
        }
        SwsContext* sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                         kW, kH, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
        img_.rgb.resize(kW * kH * 3);
        std::uint8_t* dst[1] = {img_.rgb.data()};
        int stride[1] = {kW * 3};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
        sws_freeContext(sws);
    }
    double time() const override { return sec_; }
    const Image& image() override { return img_; }

private:
    double sec_;
    Image img_;
};

// 虚代理：只记住路径和时间，第一次要图像时才解码
class LazyThumb final : public Thumb {
public:
    LazyThumb(std::string path, double sec) : path_(std::move(path)), sec_(sec) {}
    double time() const override { return sec_; }            // 画时间刻度不需要解码
    const Image& image() override {
        if (!real_) real_ = std::make_unique<DecodedThumb>(path_, sec_);
        return real_->image();
    }

private:
    std::string path_;
    double sec_;
    std::unique_ptr<DecodedThumb> real_;
};

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
double peak_rss_mb() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return ru.ru_maxrss / 1048576.0;   // macOS 单位是字节
#else
    return ru.ru_maxrss / 1024.0;      // Linux 单位是 KB
#endif
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "用法：%s 视频 eager|lazy\n", argv[0]); return 1; }
    std::string path = argv[1];
    bool lazy = std::strcmp(argv[2], "lazy") == 0;
    std::vector<double> open_ms, screen_ms, all_ms;
    std::uint32_t sum = 0;
    double screen_mb = 0;
    for (int r = 0; r < 7; ++r) {
        g_decodes = 0;
        auto t0 = Clock::now();
        std::vector<std::unique_ptr<Thumb>> strip;
        for (int i = 0; i < kCount; ++i) {
            if (lazy) strip.push_back(std::make_unique<LazyThumb>(path, i * 0.5));
            else strip.push_back(std::make_unique<DecodedThumb>(path, i * 0.5));
        }
        open_ms.push_back(ms_since(t0));
        for (int i = 0; i < kVisible; ++i) sum += strip[static_cast<std::size_t>(i)]->image().rgb.at(0);
        screen_ms.push_back(ms_since(t0));
        int first_screen = g_decodes;
        if (r == 0) screen_mb = peak_rss_mb();                // 第一次显示完第一屏时的峰值内存
        for (auto& t : strip) sum += t->image().rgb.at(0);    // 把时间轴从头拖到尾
        all_ms.push_back(ms_since(t0));
        if (r == 6) std::printf("%s：第一屏解码 %d 次，拖到底一共解码 %d 次\n", lazy ? "虚代理" : "全部解码", first_screen, g_decodes);
    }
    std::printf("打开 %.3f ms，显示第一屏 %.1f ms，拖到底 %.1f ms\n", median(open_ms), median(screen_ms), median(all_ms));
    std::printf("峰值内存：显示完第一屏时 %.1f MB，拖到底之后 %.1f MB（校验 %u）\n", screen_mb, peak_rss_mb(), sum);
}
```

</details>

`LazyThumb` 和 `DecodedThumb` 的关系，和上面的 `ThumbProxy`、`VideoThumb` 完全一样。`run.sh` 两种方式各跑一个进程，各 7 次取中位数：

<figure class="diagram">
<svg viewBox="0 0 700 156" role="img" aria-label="用 FFmpeg 真正解码 20 张时间轴缩略图：全部解码第一屏 Clang 639.0 毫秒、GCC 722.5 毫秒；虚代理第一屏 65.7 和 76.8 毫秒；虚代理拖到底 636.4 和 723.9 毫秒"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="158.0" y="30.0" width="151.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="315.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">639.0 ms</text><rect x="158.0" y="60.0" width="15.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="179.6" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">65.7 ms</text><rect x="158.0" y="90.0" width="150.7" height="20.0" rx="2" style="fill:var(--link)"/><text x="314.7" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">636.4 ms</text><text x="428.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="428.0" y="30.0" width="171.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="605.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">722.5 ms</text><rect x="428.0" y="60.0" width="18.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="452.2" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">76.8 ms</text><rect x="428.0" y="90.0" width="171.4" height="20.0" rx="2" style="fill:var(--link)"/><text x="605.5" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">723.9 ms</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">全部解码·第一屏</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚代理·第一屏</text><text x="150.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚代理·拖到底</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">ffmpeg/lazy_strip：bbb-10s.mp4，0～9.5 秒每 0.5 秒一张 320×180，屏幕上 4 张；-O2，7 次取中位数</text></g></svg>
</figure>

- **第一屏**：全部解码要 639.0 / 722.5 ms，虚代理只解码屏幕上的 4 张，65.7 / 76.8 ms。4 张只占全部 20 张的约 1/10，比 4/20 = 1/5 还少：前 4 张在 0～1.5 秒，离开头的关键帧近，要解码的帧少；越往后，seek 之后要多解码的帧越多。
- **拖到底**：把时间轴从头拖到尾，20 张都解码了，两种写法总耗时一样（639.0 和 636.4 ms）。再说一遍：**虚代理省的是启动时间，不是总工作量**。
- **峰值内存**差得不多（Clang：29.7 MB 对 25.1 MB；GCC：45.0 MB 对 42.3 MB）：20 张 320×180 的 RGB 一共才 3.3 MB，大头是 FFmpeg 的库和解码器本身。要省内存，得像练习 2 那样卸载。

还有一个代价：**第一次用到时会卡一下**。用户翻到新的一屏，要等这一屏解码完才看得到。常见的办法是在代理外面再加一点调度：先画一个灰色的占位框，后台线程解码好了再刷新；或者趁空闲预先解码下一屏。这些都不用改调用方，因为调用方只认识接口。

## 3. 代理的几种用途

虚代理是最常见的一种。同样的结构，在转发前后做的事不同，就成了别的代理。

### 3.1 保护代理：转发之前先检查权限

媒体库服务端：游客只能看列表，会员能播放，管理员才能删除。把权限检查写进 `LocalLibrary` 的每个函数里也行，但那样业务代码和权限混在一起，换一套权限规则（比如按地区、按时段）就要改业务类。保护代理把检查单独放在一层：

```cpp
// 《代理模式》：保护代理——GuardedLibrary 在转发之前检查角色，没有权限就拒绝并记一条审计日志
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

enum class Role { Guest, Member, Admin };
const char* name(Role r) { return r == Role::Guest ? "游客" : r == Role::Member ? "会员" : "管理员"; }

struct MediaLibrary {
    virtual ~MediaLibrary() = default;
    virtual int size() const = 0;
    virtual std::string play(int id) = 0;      // 返回播放结果；空字符串表示失败
    virtual bool erase(int id) = 0;
};

// 真实对象：只管业务，不知道有权限这回事
class LocalLibrary final : public MediaLibrary {
public:
    LocalLibrary() : titles_{"bbb-10s.mp4", "sintel.mkv", "tears.mp4"} {}
    int size() const override { return static_cast<int>(titles_.size()); }
    std::string play(int id) override {
        if (id < 0 || id >= size()) return "";
        return "正在播放 " + titles_[static_cast<std::size_t>(id)];
    }
    bool erase(int id) override {
        if (id < 0 || id >= size()) return false;
        titles_.erase(titles_.begin() + id);
        return true;
    }

private:
    std::vector<std::string> titles_;
};

// 保护代理：同样的接口，先检查再转发
class GuardedLibrary final : public MediaLibrary {
public:
    GuardedLibrary(MediaLibrary& real, Role role, std::vector<std::string>& audit)
        : real_(real), role_(role), audit_(audit) {}
    int size() const override { return real_.size(); }                   // 谁都能看
    std::string play(int id) override {
        if (role_ < Role::Member) { deny("play", id); return ""; }
        return real_.play(id);
    }
    bool erase(int id) override {
        if (role_ < Role::Admin) { deny("erase", id); return false; }
        return real_.erase(id);
    }

private:
    void deny(const char* op, int id) {
        audit_.push_back(std::string(name(role_)) + " 试图 " + op + "(" + std::to_string(id) + ")，已拒绝");
    }
    MediaLibrary& real_;
    Role role_;
    std::vector<std::string>& audit_;
};

// 调用方拿不到 LocalLibrary，只能通过 login 拿到一个代理
class Server {
public:
    std::unique_ptr<MediaLibrary> login(Role role) { return std::make_unique<GuardedLibrary>(lib_, role, audit_); }
    const std::vector<std::string>& audit() const { return audit_; }

private:
    LocalLibrary lib_;
    std::vector<std::string> audit_;
};

int main() {
    Server server;
    for (Role r : {Role::Guest, Role::Member, Role::Admin}) {
        auto lib = server.login(r);
        std::string played = lib->play(0);
        bool removed = lib->erase(2);
        std::printf("%s：size() → %d，play(0) → %s，erase(2) → %s\n", name(r), lib->size(),
                    played.empty() ? "拒绝" : played.c_str(), removed ? "成功" : "拒绝");
    }
    std::printf("审计日志：\n");
    for (auto& line : server.audit()) std::printf("  %s\n", line.c_str());
}
```

输出：

```text
游客：size() → 3，play(0) → 拒绝，erase(2) → 拒绝
会员：size() → 3，play(0) → 正在播放 bbb-10s.mp4，erase(2) → 拒绝
管理员：size() → 2，play(0) → 正在播放 bbb-10s.mp4，erase(2) → 成功
审计日志：
  游客 试图 play(0)，已拒绝
  游客 试图 erase(2)，已拒绝
  会员 试图 erase(2)，已拒绝
```

保护代理有一个前提：**调用方必须拿不到真实对象**。`LocalLibrary` 是 `Server` 的私有成员，外面只能通过 `login()` 拿到代理；如果哪里把 `LocalLibrary&` 传了出去，调用方绕过代理直接调，检查就形同虚设。这和《外观模式》正好相反：外观不禁止直接使用子系统，保护代理必须禁止。

### 3.2 缓存代理：记住结果，下次直接返回

播放列表每次刷新都要显示每个视频的分辨率和时长。读这些信息要打开文件、解析容器头，FFmpeg 的 `avformat_find_stream_info` 还会解码几帧来确认参数。同一个文件被反复查询时，可以让代理记住结果。缓存不能无限大，这里用 LRU（最近最少使用）：满了就扔掉最久没被查过的那一条。演示里点「缓存代理」可以一步步看命中和淘汰。

```cpp
// 《代理模式》：缓存代理——CachingProbe 记住最近查过的视频信息（LRU），实测不同容量下的命中率
#include <cstdint>
#include <cstdio>
#include <list>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct MediaInfo { int width = 0, height = 0, duration_ms = 0; };

struct MetadataService {
    virtual ~MetadataService() = default;
    virtual MediaInfo probe(const std::string& path) = 0;
};

// 真实对象：每次都要打开文件、解析容器头（慢）。这里只数调用次数
class FileProbe final : public MetadataService {
public:
    MediaInfo probe(const std::string& path) override {
        ++calls;
        return {1280, 720, 10000 + static_cast<int>(path.size())};
    }
    int calls = 0;
};

// 缓存代理：最多记住 capacity 条，满了就扔掉最久没用过的那条
class CachingProbe final : public MetadataService {
public:
    CachingProbe(MetadataService& real, std::size_t capacity) : real_(real), capacity_(capacity) {}
    MediaInfo probe(const std::string& path) override {
        if (auto it = index_.find(path); it != index_.end()) {
            ++hits;
            order_.splice(order_.begin(), order_, it->second);   // 挪到最前面：刚用过
            return it->second->second;
        }
        ++misses;
        MediaInfo info = real_.probe(path);
        if (capacity_ == 0) return info;
        if (order_.size() == capacity_) {                        // 满了：淘汰最后一条
            index_.erase(order_.back().first);
            order_.pop_back();
        }
        order_.emplace_front(path, info);
        index_[path] = order_.begin();
        return info;
    }
    int hits = 0, misses = 0;

private:
    using Entry = std::pair<std::string, MediaInfo>;
    MetadataService& real_;
    std::size_t capacity_;
    std::list<Entry> order_;                                     // 最近用过的在前面
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

// 请求序列：100 个文件，热门的文件被查得多（Zipf 分布，第 k 热门的概率正比于 1/k）
// 只用 mt19937 的原始输出（标准规定了它的序列），保证 libc++ 和 libstdc++ 得到同一串请求
std::vector<std::string> make_requests(int files, int n) {
    std::vector<double> cdf;
    double sum = 0;
    for (int k = 1; k <= files; ++k) cdf.push_back(sum += 1.0 / k);
    std::mt19937 rng(42);
    std::vector<std::string> req;
    for (int i = 0; i < n; ++i) {
        double u = rng() / 4294967296.0 * sum;
        int k = 0;
        while (cdf[static_cast<std::size_t>(k)] <= u) ++k;
        req.push_back("clip" + std::to_string(k) + ".mp4");
    }
    return req;
}

int main() {
    auto req = make_requests(100, 10000);
    std::printf("容量  命中率   真正打开文件的次数\n");
    for (std::size_t cap : {0, 5, 10, 20, 50, 100}) {
        FileProbe real;
        CachingProbe cache(real, cap);
        for (auto& p : req) cache.probe(p);
        std::printf("%4zu  %5.1f%%   %5d\n", cap, 100.0 * cache.hits / static_cast<double>(req.size()), real.calls);
    }
}
```

请求序列故意不均匀：100 个文件，第 k 热门的文件被查的概率正比于 1/k（Zipf 分布，热门内容就是这样分布的）。为了两个编译器拿到同一串请求，没有用 `std::uniform_real_distribution`（标准没规定它的算法，libc++ 和 libstdc++ 的结果不一样），只用了标准规定了输出序列的 `mt19937`。命中率两个编译器完全一样：

<figure class="diagram">
<svg viewBox="0 0 550 246" role="img" aria-label="缓存命中率：容量 0 是 0%，5 是 24.6%，10 是 39.7%，20 是 57.1%，50 是 81.4%，100 是 99.0%；Clang 和 GCC 完全相同"><g style="font-family:var(--mono)"><text x="148.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">命中率（Clang、GCC 相同）</text><rect x="148.0" y="30.0" width="1.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="155.5" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.0%</text><rect x="148.0" y="60.0" width="76.3" height="20.0" rx="2" style="fill:var(--link)"/><text x="230.3" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">24.6%</text><rect x="148.0" y="90.0" width="123.1" height="20.0" rx="2" style="fill:var(--link)"/><text x="277.1" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">39.7%</text><rect x="148.0" y="120.0" width="177.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="331.0" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">57.1%</text><rect x="148.0" y="150.0" width="252.3" height="20.0" rx="2" style="fill:var(--link)"/><text x="406.3" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">81.4%</text><rect x="148.0" y="180.0" width="306.9" height="20.0" rx="2" style="fill:var(--accent)"/><text x="460.9" y="195.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">99.0%</text><text x="140.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 0（不缓存）</text><text x="140.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 5</text><text x="140.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 10</text><text x="140.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 20</text><text x="140.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 50</text><text x="140.0" y="195.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 100</text><text x="10.0" y="238.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">cache_proxy.cpp：100 个文件、10000 次请求，第 k 热门的文件被查的概率正比于 1/k，LRU 淘汰</text></g></svg>
</figure>

命中率不是和容量成正比的：容量 10（只占文件数的 10%）就命中了 39.7%，因为热门的那几个文件占了大部分请求；容量 100 时剩下的 1% 是每个文件第一次被查（冷启动，躲不掉）。

把真实对象换成 FFmpeg 读流信息（`ffmpeg/probe_cache.cpp`，100 个名字都映射到 `bbb-10s.mp4`，1000 次请求，7 次取中位数），每次请求的平均耗时：

<figure class="diagram">
<svg viewBox="0 0 710 216" role="img" aria-label="缓存代理每次请求的平均耗时：Clang 上容量 0、10、20、50、100 分别是 5100、3046、2212、1036、500 微秒；GCC 上是 6602、4030、2956、1363、664 微秒"><g style="font-family:var(--mono)"><text x="168.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="168.0" y="30.0" width="131.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="305.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5100 µs</text><rect x="168.0" y="60.0" width="78.3" height="20.0" rx="2" style="fill:var(--link)"/><text x="252.3" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">3046 µs</text><rect x="168.0" y="90.0" width="56.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="230.9" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2212 µs</text><rect x="168.0" y="120.0" width="26.6" height="20.0" rx="2" style="fill:var(--link)"/><text x="200.6" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1036 µs</text><rect x="168.0" y="150.0" width="12.9" height="20.0" rx="2" style="fill:var(--accent)"/><text x="186.9" y="165.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">500 µs</text><text x="438.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="438.0" y="30.0" width="169.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="613.8" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">6602 µs</text><rect x="438.0" y="60.0" width="103.6" height="20.0" rx="2" style="fill:var(--link)"/><text x="547.6" y="75.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">4030 µs</text><rect x="438.0" y="90.0" width="76.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="520.0" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2956 µs</text><rect x="438.0" y="120.0" width="35.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="479.0" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1363 µs</text><rect x="438.0" y="150.0" width="17.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="461.1" y="165.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">664 µs</text><text x="160.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 0（命中 0%）</text><text x="160.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 10（39.7%）</text><text x="160.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 20（56.2%）</text><text x="160.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 50（79.8%）</text><text x="160.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">容量 100（90.1%）</text><text x="10.0" y="208.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">ffmpeg/probe_cache：真实对象用 avformat_open_input + avformat_find_stream_info；1000 次请求（所以容量 100 也有 9.9% 冷启动未命中），-O2，7 次取中位数</text></g></svg>
</figure>

没有缓存时每次 5100 / 6602 µs，几乎全花在 `avformat_find_stream_info` 上；命中一次只是查一下哈希表、挪一下链表节点，不到 1 µs，所以平均耗时基本等于「未命中率 × 一次真实查询」。

缓存代理最难的不是缓存，是**失效**：文件被替换了，缓存里还是旧的分辨率。常见的办法是把文件的修改时间、大小放进缓存的键里，或者给每条加一个过期时间。另一个容易忽略的问题是**键要包含所有影响结果的参数**：如果结果和当前用户有关，而键只有文件名，一个用户的结果就会被另一个用户拿到——练习 3 就是这个问题。

### 3.3 远程代理：调用方以为是本地调用

解码器在另一个进程里（浏览器把解码放在独立的 GPU 进程或沙箱进程里），或者在另一台机器上。远程代理在本地实现同一个接口，把每次调用打包成一条消息发出去，等回复，再把结果解出来：

```cpp
// 《代理模式》：远程代理——RemoteDecoder 把每次调用打包成一条消息发给「另一台机器」，调用方看到的还是 Decoder 接口
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

struct Decoder {
    virtual ~Decoder() = default;
    virtual int width() = 0;
    virtual int height() = 0;
    virtual int fps() = 0;
};

// 真实对象：运行在服务端
class HwDecoder final : public Decoder {
public:
    int width() override { return 1280; }
    int height() override { return 720; }
    int fps() override { return 24; }
};

// 服务端：收到一条请求消息，调真实对象，把结果编成回复消息
std::string serve(Decoder& real, const std::string& request) {
    if (request == "width") return std::to_string(real.width());
    if (request == "height") return std::to_string(real.height());
    if (request == "fps") return std::to_string(real.fps());
    if (request == "info")
        return std::to_string(real.width()) + "," + std::to_string(real.height()) + "," + std::to_string(real.fps());
    return "error";
}

// 网络：数一数往返了几次、传了多少字节（真实网络里每次往返要几百微秒到几十毫秒）
struct Channel {
    std::function<std::string(const std::string&)> remote;
    int round_trips = 0, bytes = 0;
    std::string call(const std::string& request) {
        ++round_trips;
        std::string reply = remote(request);
        bytes += static_cast<int>(request.size() + reply.size());
        return reply;
    }
};

// 远程代理：实现同一个接口，每个函数都变成一次网络往返
class RemoteDecoder final : public Decoder {
public:
    explicit RemoteDecoder(Channel& ch) : ch_(ch) {}
    int width() override { return std::atoi(ch_.call("width").c_str()); }
    int height() override { return std::atoi(ch_.call("height").c_str()); }
    int fps() override { return std::atoi(ch_.call("fps").c_str()); }
    // 远程接口通常要「粗粒度」：一次往返把常用的信息都带回来
    void info(int& w, int& h, int& f) {
        std::string r = ch_.call("info");
        std::sscanf(r.c_str(), "%d,%d,%d", &w, &h, &f);
    }

private:
    Channel& ch_;
};

int main() {
    HwDecoder real;
    Channel ch{[&](const std::string& req) { return serve(real, req); }};
    RemoteDecoder dec(ch);

    Decoder& d = dec;                  // 调用方只认识 Decoder
    int w = d.width(), h = d.height(), f = d.fps();
    std::printf("逐个调用：%dx%d@%d，往返 %d 次，%d 字节\n", w, h, f, ch.round_trips, ch.bytes);

    ch.round_trips = ch.bytes = 0;
    dec.info(w, h, f);
    std::printf("一次 info：%dx%d@%d，往返 %d 次，%d 字节\n", w, h, f, ch.round_trips, ch.bytes);
}
```

输出：

```text
逐个调用：1280x720@24，往返 3 次，23 字节
一次 info：1280x720@24，往返 1 次，15 字节
```

远程代理让网络调用**看起来**像本地调用，但每次调用实际是一次往返：同一台机器上的进程间通信要几微秒到几十微秒，跨机房要几毫秒到几十毫秒。为本地调用设计的「细粒度」接口（一个属性一个 getter），搬到网络上就会变成一连串往返。所以远程接口一般设计成**粗粒度**的：一次调用把常用的东西都带回来，像上面的 `info()`。gRPC、Thrift 这类框架根据接口描述生成的客户端 stub，就是远程代理；Chromium 的 Mojo、Windows 的 COM 跨进程调用也是这样。

远程代理还要面对本地调用没有的问题：网络会超时、会断，对方进程会崩溃。这些失败没法完全藏在接口后面，所以远程代理的接口通常要能表达「调用失败」，调用方也得处理。

### 3.4 智能引用：shared_ptr 本身就是代理

`std::shared_ptr<Player>` 用起来和 `Player*` 一样（`->`、`*`），背后多做了一件事：维护引用计数，最后一个 `shared_ptr` 消失时 `delete` 对象。GoF 把这类代理叫**智能引用**（smart reference）。

C++ 的 `operator->` 有一个特别的规则：如果它返回的不是原始指针，而是一个对象，编译器会接着对这个对象调用 `operator->`，一直「钻」到得到原始指针为止。利用这一点，`operator->` 返回一个临时对象，就能在每次成员函数调用的前后各做一件事（比如加锁、解锁）：

```cpp
// 《代理模式》：智能引用——shared_ptr 本身就是代理；operator-> 返回一个临时对象，就能在每次调用前后做事
#include <cstdio>
#include <memory>
#include <string>

struct Player {
    void play() { std::printf("    Player::play()\n"); }
    void seek(double sec) { std::printf("    Player::seek(%.1f)\n", sec); }
};

// 「调用前后各做一件事」的代理：operator-> 返回一个临时的 Guard
// Guard 构造时打印「进入」，它的 operator-> 再返回真实指针，表达式结束时 Guard 析构，打印「离开」
template <class T>
class Traced {
public:
    explicit Traced(std::shared_ptr<T> p) : p_(std::move(p)) {}
    class Guard {
    public:
        explicit Guard(T* p) : p_(p) { std::printf("  [进入] 加锁、计时、记日志……\n"); }
        ~Guard() { std::printf("  [离开] 解锁\n"); }
        T* operator->() const { return p_; }
    private:
        T* p_;
    };
    Guard operator->() const { return Guard(p_.get()); }

private:
    std::shared_ptr<T> p_;
};

int main() {
    // 1. shared_ptr：和 Player* 用法一样（-> 和 *），背后多做了引用计数，最后一个 shared_ptr 消失时自动 delete
    auto p = std::make_shared<Player>();
    {
        std::shared_ptr<Player> q = p;
        q->play();
        std::printf("use_count = %ld\n", p.use_count());
    }
    std::printf("use_count = %ld\n", p.use_count());

    // 2. operator-> 会一直「往下钻」，直到得到一个原始指针：Traced -> Guard -> Player*
    Traced<Player> t(p);
    t->seek(5);
    t->play();
}
```

输出：

```text
    Player::play()
use_count = 2
use_count = 1
  [进入] 加锁、计时、记日志……
    Player::seek(5.0)
  [离开] 解锁
  [进入] 加锁、计时、记日志……
    Player::play()
  [离开] 解锁
```

`t->seek(5)` 展开就是 `t.operator->().operator->()->seek(5)`：第一步得到临时的 `Guard`（打印「进入」），第二步得到 `Player*`，调用 `seek`，整个表达式结束时临时的 `Guard` 析构（打印「离开」）。这种写法叫 execute-around pointer，适合给一个现成的类「每次调用都加锁」，而不用改这个类。

### 3.5 代理对象的坑：`vector<bool>::reference`

标准库为了省内存，把 `std::vector<bool>` 特化成**每个元素只占 1 位**。问题是，C++ 没有指向「一个二进制位」的引用，`operator[]` 没法返回 `bool&`。于是它返回一个代理对象 `vector<bool>::reference`：里面存着「哪个字」和「哪一位」，赋值给它时修改那一位，转换成 `bool` 时读出那一位。

<figure class="diagram">
<svg viewBox="0 0 680 220" role="img" aria-label="vector&lt;bool&gt; 把 64 个元素压在一个 64 位的字里；key[3] 返回的代理对象存着指向这个字的指针和掩码 1 左移 3 位，赋值时置位，转换成 bool 时读出这一位；容器销毁后指针就悬空了"><g style="font-family:var(--mono)"><text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">vector&lt;bool&gt; key 的存储：一个 64 位的字存 64 个元素（只画了前 16 位）</text><rect x="20" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="36.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="36.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0</text><rect x="54" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="70.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="70.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1</text><rect x="88" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="104.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="104.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">2</text><rect x="122" y="40" width="32" height="30" rx="3" style="fill:var(--accent);stroke:var(--border);opacity:0.9"/><text x="138.0" y="60.0" text-anchor="middle" style="fill:var(--bg);font-size:11.5px;font-weight:700">1</text><text x="138.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">3</text><rect x="156" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="172.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="172.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">4</text><rect x="190" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="206.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="206.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">5</text><rect x="224" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="240.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="240.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">6</text><rect x="258" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="274.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="274.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">7</text><rect x="292" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="308.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="308.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">8</text><rect x="326" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="342.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="342.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">9</text><rect x="360" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="376.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="376.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10</text><rect x="394" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="410.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="410.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">11</text><rect x="428" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="444.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="444.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">12</text><rect x="462" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="478.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="478.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">13</text><rect x="496" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="512.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="512.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">14</text><rect x="530" y="40" width="32" height="30" rx="3" style="fill:none;stroke:var(--border);opacity:1"/><text x="546.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">0</text><text x="546.0" y="86.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">15</text><text x="572.0" y="60.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">……</text><rect x="20" y="140" width="350" height="64" rx="4" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="32.0" y="160.0" text-anchor="start" style="fill:var(--link);font-size:11px;font-weight:700">auto a = key[3];   // vector&lt;bool&gt;::reference</text><text x="32.0" y="180.0" text-anchor="start" style="fill:var(--fg);font-size:11px">word_ : 指向上面那个字</text><text x="32.0" y="197.0" text-anchor="start" style="fill:var(--fg);font-size:11px">mask_ : 1 &lt;&lt; 3</text><line x1="170" y1="140" x2="139.0" y2="92" style="stroke:var(--link);stroke-width:1.4"/><path d="M139.0,92 l-2,9 l7,-4 z" style="fill:var(--link)"/><text x="395.0" y="154.0" text-anchor="start" style="fill:var(--fg);font-size:11px">a = true  → *word_ |= mask_</text><text x="395.0" y="172.0" text-anchor="start" style="fill:var(--fg);font-size:11px">bool(a)   → (*word_ &amp; mask_) != 0</text><text x="395.0" y="190.0" text-anchor="start" style="fill:var(--danger);font-size:11px">key 销毁或重新分配后，word_ 就悬空了</text></g></svg>
</figure>

平时 `key[3] = true;`、`if (key[3])` 都感觉不到代理的存在。可一旦用 `auto` 接住它：

```cpp
// 《代理模式》：vector<bool>::reference 是一个代理对象——auto 推导出来的不是 bool
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <vector>

static std::size_t g_bytes = 0;                       // 统计 new 出去的总字节数
void* operator new(std::size_t n) { g_bytes += n; if (void* p = std::malloc(n)) return p; std::abort(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main() {
    std::vector<bool> key(8);                         // 8 个包是不是关键帧
    std::vector<int> pts(8);

    auto a = key[3];                                  // 推导出 vector<bool>::reference，不是 bool
    auto b = pts[3];                                  // 推导出 int，是一份拷贝
    a = true;
    b = 100;
    std::printf("a = %d，key[3] = %d\n", static_cast<int>(a), static_cast<int>(key[3]));   // key[3] 跟着变了
    std::printf("b = %d，pts[3] = %d\n", b, pts[3]);                                     // pts[3] 没变
    static_assert(std::is_same_v<decltype(a), std::vector<bool>::reference>);
    static_assert(!std::is_same_v<decltype(key[3]), bool&>);
    std::printf("sizeof(a) = %zu，sizeof(bool) = %zu\n", sizeof(a), sizeof(bool));

    bool c = key[4];                                  // 想要一份拷贝，就写明 bool
    c = true;
    std::printf("c = %d，key[4] = %d\n", static_cast<int>(c), static_cast<int>(key[4]));

    // 代理换来的好处：每个元素只占 1 位
    g_bytes = 0;
    std::vector<bool> bits(1'000'000);
    std::size_t bool_bytes = g_bytes;
    g_bytes = 0;
    std::vector<char> chars(1'000'000);
    std::printf("100 万个元素：vector<bool> %zu 字节，vector<char> %zu 字节\n", bool_bytes, g_bytes);
}
```

输出：

```text
a = 1，key[3] = 1
b = 100，pts[3] = 0
sizeof(a) = 16，sizeof(bool) = 1
c = 1，key[4] = 0
100 万个元素：vector<bool> 125000 字节，vector<char> 1000000 字节
```

- `auto b = pts[3]` 得到一份 `int` 拷贝，改 `b` 不影响 `pts`；**`auto a = key[3]` 得到的是代理，改 `a` 就改了 `key[3]`**。同样写 `auto`，行为却不一样。
- 代理对象有自己的大小：`vector<bool>::reference` 是一个指针加一个掩码，16 字节（Clang 和 GCC 都是），比它代表的那一位大 128 倍。
- 好处是实打实的：100 万个元素只要 125000 字节，是 `vector<char>` 的 1/8。

更麻烦的是，代理对象指向容器内部，**容器销毁或者重新分配之后，代理就悬空了**。用 `auto` 做返回类型时最容易踩到，练习 4 就是这样一个例子：程序不崩溃，只是悄悄算错。另外，按引用遍历在 `vector<bool>` 上编译不过：

<!-- run: off -->
```cpp
// 《代理模式》：编译不过——vector<bool> 的元素不是 bool，不能绑定到 bool&（故意的反例，不参与构建）
#include <vector>

template <class T>
void clear_all(std::vector<T>& v) {
    for (auto& x : v) x = T{};     // T = bool 时：*it 是一个临时的代理对象，不能绑定到 auto&
}

int main() {
    std::vector<int> a(4, 1);
    clear_all(a);                  // 没问题
    std::vector<bool> b(4, true);
    clear_all(b);                  // 编译错误
    bool& r = b[0];                // 编译错误：b[0] 不是 bool
    (void)r;
}
```

Clang 的报错：

```text
error: non-const lvalue reference to type 'bool' cannot bind to a temporary of type 'reference'
```

`*it` 返回的是一个临时的代理对象，不能绑定到非 `const` 的左值引用上。写泛型代码时要记得：`std::vector<T>` 的元素不一定是 `T&`。

类似的代理对象还有 `std::bitset::reference`；Eigen 之类的矩阵库更进一步，`a + b` 不立刻计算，而是返回一个代表「a 加 b」的表达式对象（表达式模板），等赋值时才一次算完——用 `auto` 接住它，同样可能留下指向临时对象的悬空引用。经验规则：**右边可能是代理对象时，不要用 `auto`，写明想要的类型**（`bool c = key[4];`），或者用 `static_cast<bool>(key[4])` 表明「我要的是值」。

### 3.6 设计上的取舍

- **代理要和真实对象的接口一模一样**。接口变了，代理就要跟着改；接口很大时，每个函数都要写一遍转发（C++ 没有自动转发，这是写代理最烦的地方）。
- **对象身份**：代理和真实对象是两个对象，地址不同。如果调用方会比较指针、或者真实对象会把 `this` 交给别人，就会露馅。
- **延迟初始化和多线程**：`if (!real_) real_ = ...` 在两个线程同时第一次调用时会创建两次（数据竞争）。多线程下要用 `std::call_once`、或者加锁，见 4.2 节。
- **失败推迟了**：真实对象创建失败（文件不存在、格式不支持），本来在打开时就报错，现在变成第一次显示时才报错。调用方要能处理这种「迟到的失败」。

## 4. 现代 C++ 写法

### 4.1 通用的 `Lazy<T>`

每个类写一个虚代理，转发函数要一个个写。如果调用方愿意写 `->`，可以用一个模板代替：把「怎么创建」交给一个 lambda，第一次 `get()` 时才调用它：

```cpp
// 《代理模式》：不为每个类写一个代理——通用的 Lazy<T>，把「怎么创建」交给一个 lambda
#include <cstdio>
#include <functional>
#include <optional>
#include <string>

template <class T>
class Lazy {
public:
    explicit Lazy(std::function<T()> make) : make_(std::move(make)) {}
    T& get() {
        if (!value_) value_.emplace(make_());   // 第一次 get 时才创建，之后一直复用
        return *value_;
    }
    T* operator->() { return &get(); }          // 像指针一样用
    bool ready() const { return value_.has_value(); }

private:
    std::function<T()> make_;
    std::optional<T> value_;                    // 就地存放，不额外分配堆内存
};

struct Subtitle {
    explicit Subtitle(const std::string& path) : path(path) { std::printf("  解析字幕 %s（很慢）\n", path.c_str()); }
    int lines() const { return 1420; }
    std::string path;
};

int main() {
    Lazy<Subtitle> sub([] { return Subtitle("bbb.zh.srt"); });
    std::printf("打开视频，字幕已加载：%s\n", sub.ready() ? "是" : "否");
    std::printf("用户打开字幕：\n");
    std::printf("  共 %d 行\n", sub->lines());
    std::printf("再看一次：%d 行，已加载：%s\n", sub->lines(), sub.ready() ? "是" : "否");
    std::printf("sizeof(Lazy<Subtitle>) = %zu，sizeof(Subtitle) = %zu\n", sizeof(Lazy<Subtitle>), sizeof(Subtitle));
}
```

输出：

```text
打开视频，字幕已加载：否
用户打开字幕：
  解析字幕 bbb.zh.srt（很慢）
  共 1420 行
再看一次：1420 行，已加载：是
sizeof(Lazy<Subtitle>) = 64，sizeof(Subtitle) = 24
```

（GCC 上是 72 和 32：libstdc++ 的 `std::string` 是 32 字节，libc++ 是 24 字节。）

和 `ThumbProxy` 比：

- **不用写转发函数**，任何类型都能用。代价是它**不再实现原来的接口**：调用方要写 `sub->lines()`、拿到的是 `Lazy<Subtitle>`，而不是一个 `Subtitle`。所以它更像一个「智能引用」，不是一个能替换真实对象的代理。
- `std::optional<T>` 就地存放对象，不额外分配堆内存；`ThumbProxy` 用 `unique_ptr`，创建时多一次 `new`，但没创建时只占一个指针。真实对象很大（几十 KB 的像素）时，用 `unique_ptr` 更合适，免得 120 个空代理也占着 120 份空间。
- `std::function` 本身占 32 字节（两个库都是），捕获的东西大了还会分配堆内存。如果创建的方法是固定的，可以把工厂作为模板参数，省掉 `std::function`。

### 4.2 线程安全的延迟初始化

多个线程可能同时第一次调用代理时，用 `std::call_once` 保证真实对象只创建一次：

<!-- run: off -->
```cpp
class ThumbProxy final : public Thumbnail {
public:
    void draw(int slot) override {
        std::call_once(once_, [this] { real_ = std::make_unique<VideoThumb>(path_); });
        real_->draw(slot);
    }
    // ...
private:
    std::string path_;
    std::once_flag once_;
    std::unique_ptr<VideoThumb> real_;
};
```

如果整个程序只需要一个延迟创建的对象，函数内的 `static` 局部变量（《单例模式》里的 Meyers 单例）就是最简单的线程安全虚代理：第一次调用函数时才构造，C++11 保证只构造一次。

### 4.3 unique_ptr 和 shared_ptr 当「代理的零件」

上面的代理都用 `std::unique_ptr` 持有真实对象：它的空状态正好表示「还没创建」，`reset()` 就是「卸载」（练习 2），析构时自动释放，不用自己写析构函数和拷贝控制。需要多个代理共享同一个真实对象时（比如同一个视频在两个播放列表里各有一个缩略图），用 `shared_ptr`；需要「真实对象还在就用，不在了就重新创建」的缓存时，缓存里存 `weak_ptr`，代理拿到时 `lock()`。

## 5. 代理和相似的模式

代理、装饰器、适配器、外观都是「在一个对象前面再包一层」，类图可以画得几乎一样，区别在**意图**和**接口**：

| 模式 | 意图 | 包装后的接口 | 包几个对象、几层 | 真实对象由谁创建 |
|---|---|---|---|---|
| **代理** | **控制访问**：推迟、检查、缓存、远程、计数 | 和真实对象**完全相同** | 一对一，通常一层 | 常常是代理自己（虚代理）；调用方往往不知道真实对象存在 |
| 装饰器 | **增加功能**：日志、校验、统计 | 和被装饰对象完全相同 | 一对一，可以**一层层叠** | 调用方创建好再交给装饰器 |
| 适配器 | **转换接口**：让已有的类符合另一个接口 | 和被适配的类**不同**（是调用方要的那个） | 通常包一个 | 调用方或工厂 |
| 外观 | **简化**一组复杂的接口 | 新设计的、更简单的接口 | 包一整个子系统 | 外观自己 |

代理和装饰器最难分，因为代码结构真的一样：同一个接口、持有一个同类型的对象、转发调用。几个判断办法：

- **它改变了「做什么」，还是改变了「能不能做、什么时候做」？** 装饰器给每次调用加上新的行为（压缩、加密、统计），调用方要的就是这个新行为；代理不加新功能，它决定这次调用要不要、什么时候、在哪里交给真实对象。
- **调用方知不知道有这一层？** 装饰器通常是调用方自己套上去的（`Logging(Checksum(source))`）；代理通常是别人给的，调用方以为自己拿到的就是真实对象。
- **能不能叠很多层？** 装饰器就是为了叠；代理一般一层（叠也可以，比如保护代理套缓存代理，但这时顺序很重要，见练习 3）。

模式的名字是用来交流意图的。同一段代码，你叫它「缓存代理」，读代码的人马上知道它不改变结果、只是让它更快；叫它「缓存装饰器」也不算错，只是传达的意思不一样。

## 6. 小结

- 代理模式：**替身和真实对象实现同一个接口**，调用方分不出来；替身在转发前后控制访问。
- 虚代理推迟创建开销大的对象：本文媒体库打开从 94.48 ms 降到 0.003 ms，第一屏 9.43 ms，内存降到 1/10；用 FFmpeg 真解码，第一屏从 639.0 ms 降到 65.7 ms。它省的是启动时间，不是总工作量；全部用到之后，内存和全部加载一样，要卸载才能封顶。
- 保护代理只有在调用方拿不到真实对象时才有用；缓存代理用内存换时间，难点是容量、失效和键；远程代理让网络调用看起来像本地调用，接口要设计成粗粒度的。
- `shared_ptr`、`vector<bool>::reference` 都是代理（智能引用）。代理对象指向别的东西，用 `auto` 接住它时得到的是代理本身，可能悬空；右边可能是代理时，写明想要的类型。
- 每次调用多绕一层代理，开销不到 1 ns；真正的代价在于：接口要逐个转发、对象身份不同、多线程下的延迟初始化、失败被推迟。
- 代理和装饰器结构一样、意图不同：装饰器加功能、可以叠；代理控制访问、通常一层。

第 3 章的结构型模式到这里就讲完了。下一章开始讲行为型模式：对象之间怎么分工、怎么通信。

## 7. 练习

1. **辨析**：下面四个类各是什么模式（代理、装饰器、适配器、外观）？说出理由。
   - (a) `GzipSource`：实现 `ByteSource` 接口，构造时接收另一个 `ByteSource`，`read()` 时把读到的数据解压后返回。
   - (b) `LazyFont`：实现 `Font` 接口，构造时只记住字体文件路径，第一次 `measure()` 或 `render()` 时才加载字形。
   - (c) `MediaPlayer`：提供 `open(path)`、`play()`、`pause()` 三个函数，内部协调解封装、解码、音频输出、视频渲染四个模块。
   - (d) `RateLimitedApi`：实现 `TranscodeApi` 接口，每个用户每分钟最多转发 10 次请求，超过就直接返回「请稍后再试」。
2. **动手**：第 2.2 节的虚代理翻完 10 屏后，内存回到了 6762 KB。给 `ThumbProxy` 加一个 `unload()`，让 `Gallery` 只保留最近显示过的 24 张（两屏），超出的卸载掉。翻完 10 屏后内存是多少？翻回第 9 屏、第 1 屏时各要不要重新解码？
3. **设计**：视频网站有一个 `UrlService`，`url(id)` 返回片源的播放地址。付费片（`id` 以 `vip-` 开头）只给会员。你要给它加两个代理：一个保护代理 `Guard` 检查当前请求的用户是不是会员，一个所有用户共用的缓存代理 `Cache`（拿播放地址要签名，比较慢）。`Cache(Guard(real))` 和 `Guard(Cache(real))` 选哪个？如果一定要把缓存放在外面，要怎么改？
4. **找问题**：下面的程序应该输出「关键帧 5 个」，用 Clang `-O2` 编译却输出「关键帧 0 个」（GCC：54 个）。问题出在哪？怎么改？

   <!-- run: off -->
   ```cpp
   // 《代理模式》：练习 4——找问题：auto 返回类型推导出 vector<bool>::reference，指向已经销毁的局部 vector
   #include <cstdio>
   #include <vector>

   // 扫描一遍，记下每个包是不是关键帧（这里用每 48 个包一个关键帧模拟）
   std::vector<bool> scan_keyframes(int packets) {
       std::vector<bool> key(static_cast<std::size_t>(packets));
       for (int i = 0; i < packets; i += 48) key[static_cast<std::size_t>(i)] = true;
       return key;
   }

   auto is_keyframe(int packets, int i) {
       std::vector<bool> key = scan_keyframes(packets);
       return key[static_cast<std::size_t>(i)];       // 返回的是代理对象，指向 key 里的一个字
   }                                                  // key 在这里析构

   int main() {
       int n = 0;
       for (int i = 0; i < 240; ++i) n += is_keyframe(240, i);   // 应该是 5
       std::printf("关键帧 %d 个\n", n);
   }
   ```

<details>
<summary>点击查看答案</summary>

1. - (a) **装饰器**：接口不变，给读到的数据**加了一个功能**（解压）。调用方要的就是解压后的数据，而且可以继续叠（`GzipSource(Decrypt(File))`）。
   - (b) **虚代理**：接口不变，不加功能，只是把加载推迟到第一次用到时。
   - (c) **外观**：新设计了一个更简单的接口，背后协调一整个子系统。
   - (d) **保护代理**：接口不变，决定这次请求**能不能**交给真实对象。（说它是装饰器也说得通——「限流」也可以看成一个附加功能。判断的关键是：它不改变请求的结果，只决定要不要转发，所以更接近代理。）

2. `ThumbProxy` 加 `unload()`（`real_.reset()`），`Gallery` 用一个 `deque` 记住最近显示过的下标，超过 24 个就卸载最前面的。`Gallery` 要调用 `unload()`，所以它存的是 `ThumbProxy`，不再是 `Thumbnail`——卸载是代理特有的能力，接口里没有：

   ```cpp
   // 《代理模式》：练习 2——给虚代理加上 unload()，媒体库只保留最近显示过的 24 张，翻再多屏内存也不再涨
   #include <algorithm>
   #include <cstdint>
   #include <cstdio>
   #include <cstdlib>
   #include <deque>
   #include <memory>
   #include <new>
   #include <string>
   #include <vector>

   // ---- 统计堆内存：当前占用 g_live（和代理模式无关，只为了量出内存）----
   static std::size_t g_live = 0;
   void* operator new(std::size_t n) {
       auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
       if (!p) std::abort();
       *p = n;
       g_live += n;
       return reinterpret_cast<char*>(p) + 16;
   }
   void operator delete(void* q) noexcept {
       if (!q) return;
       auto* p = reinterpret_cast<std::size_t*>(static_cast<char*>(q) - 16);
       g_live -= *p;
       std::free(p);
   }
   void operator delete(void* q, std::size_t) noexcept { operator delete(q); }
   // ---------------------------------------------------------------------

   constexpr int kW = 160, kH = 90;          // 缩略图 160×90，RGBA，每张 57600 字节
   constexpr int kPerPage = 12;              // 一屏显示 12 张（3 行 × 4 列）
   static int g_decodes = 0;
   static std::uint32_t g_screen = 0;        // 「屏幕」：draw 往这里写，防止被优化掉

   struct Thumbnail {
       virtual ~Thumbnail() = default;
       virtual const std::string& title() const = 0;
       virtual void draw(int slot) = 0;      // 画到屏幕的第 slot 格
   };

   // 真实对象：构造时就解码视频的一帧（这里用一段计算模拟解码的开销）
   class VideoThumb final : public Thumbnail {
   public:
       explicit VideoThumb(std::string path) : path_(std::move(path)), pixels_(kW * kH * 4) {
           ++g_decodes;
           std::uint32_t h = 2166136261u;
           for (char c : path_) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
           for (int pass = 0; pass < 16; ++pass)
               for (auto& b : pixels_) { h = h * 1664525u + 1013904223u; b ^= static_cast<std::uint8_t>(h >> 24); }
       }
       const std::string& title() const override { return path_; }
       void draw(int slot) override { g_screen += pixels_[slot * 4] + static_cast<std::uint32_t>(slot); }

   private:
       std::string path_;
       std::vector<std::uint8_t> pixels_;
   };

   // 虚代理：和 VideoThumb 实现同一个接口，只记住路径；第一次需要像素时才创建真实对象
   class ThumbProxy final : public Thumbnail {
   public:
       explicit ThumbProxy(std::string path) : path_(std::move(path)) {}
       const std::string& title() const override { return path_; }   // 不用解码就能回答
       void draw(int slot) override { real().draw(slot); }
       bool loaded() const { return real_ != nullptr; }
       void unload() { real_.reset(); }      // 扔掉像素，下次 draw 时再解码

   private:
       VideoThumb& real() {
           if (!real_) real_ = std::make_unique<VideoThumb>(path_);
           return *real_;
       }
       std::string path_;
       std::unique_ptr<VideoThumb> real_;   // 还没创建时是空的
   };

   class Gallery {
   public:
       Gallery(int n, std::size_t keep) : keep_(keep) {
           for (int i = 0; i < n; ++i)
               items_.push_back(std::make_unique<ThumbProxy>("clip" + std::to_string(i) + ".mp4"));
       }
       void show_page(int page) {
           for (int slot = 0; slot < kPerPage; ++slot) {
               std::size_t i = static_cast<std::size_t>(page * kPerPage + slot);
               if (i >= items_.size()) continue;
               items_[i]->draw(slot);
               recent_.erase(std::remove(recent_.begin(), recent_.end(), i), recent_.end());
               recent_.push_back(i);                       // 最近显示的放在最后
           }
           while (recent_.size() > keep_) {                // 超出上限：卸载最久没显示的
               items_[recent_.front()]->unload();
               recent_.pop_front();
           }
       }

   private:
       std::vector<std::unique_ptr<ThumbProxy>> items_;   // Gallery 要调用 unload，所以存代理类型
       std::size_t keep_;
       std::deque<std::size_t> recent_;
   };

   int main() {
       Gallery g(120, 24);
       for (int p = 0; p < 10; ++p) g.show_page(p);
       std::printf("翻完 10 屏：解码 %3d 次，堆内存 %5zu KB\n", g_decodes, g_live / 1024);
       g.show_page(8);
       std::printf("翻回第 9 屏：解码 %3d 次（还在内存里）\n", g_decodes);
       g.show_page(0);
       std::printf("翻回第 1 屏：解码 %3d 次（已经卸载，重新解码）\n", g_decodes);
   }
   ```

   输出：

   ```text
   翻完 10 屏：解码 120 次，堆内存  1361 KB
   翻回第 9 屏：解码 120 次（还在内存里）
   翻回第 1 屏：解码 132 次（已经卸载，重新解码）
   ```

   内存封顶在 24 张（1361 KB，是不卸载时的 1/5）。翻回第 9 屏不用解码（最近两屏还在），翻回第 1 屏要重新解码 12 次。这是一个 LRU 缓存：用一部分重复解码，换内存有上限。上限设多大，取决于内存和「来回翻」的频率。

3. 选 **`Guard(Cache(real))`**：保护代理在最外面，每次请求都先检查权限，缓存里有没有都一样。反过来 `Cache(Guard(real))` 的话，会员查过一次后，地址进了共用的缓存；游客再查同一个 `id`，缓存直接返回，根本走不到 `Guard`：

   ```cpp
   // 《代理模式》：练习 3——缓存代理和保护代理谁在外面？缓存套在外面，会员查过的付费片源，游客直接从缓存里拿到
   #include <cstdio>
   #include <map>
   #include <string>

   struct Session { bool vip = false; };            // 当前请求是谁发的（服务端每个请求更新一次）

   struct UrlService {
       virtual ~UrlService() = default;
       virtual std::string url(const std::string& id) = 0;   // 返回播放地址；空字符串表示拒绝
   };

   class RealUrlService final : public UrlService {
   public:
       std::string url(const std::string& id) override { ++calls; return "https://cdn.example/" + id + "?token=…"; }
       int calls = 0;
   };

   class Guard final : public UrlService {           // 保护代理：付费片（id 以 vip- 开头）只给会员
   public:
       Guard(UrlService& inner, const Session& s) : inner_(inner), s_(s) {}
       std::string url(const std::string& id) override {
           if (id.rfind("vip-", 0) == 0 && !s_.vip) return "";
           return inner_.url(id);
       }
   private:
       UrlService& inner_;
       const Session& s_;
   };

   class Cache final : public UrlService {           // 缓存代理：所有用户共用一份
   public:
       explicit Cache(UrlService& inner) : inner_(inner) {}
       std::string url(const std::string& id) override {
           auto it = memo_.find(id);
           if (it != memo_.end()) return it->second;
           std::string u = inner_.url(id);
           if (!u.empty()) memo_[id] = u;
           return u;
       }
   private:
       UrlService& inner_;
       std::map<std::string, std::string> memo_;
   };

   void run(const char* title, UrlService& front, Session& s, RealUrlService& real) {
       s.vip = true;
       std::string a = front.url("vip-movie");
       s.vip = false;
       std::string b = front.url("vip-movie");
       std::printf("%s\n  会员：%s\n  游客：%s\n  真实服务被调用 %d 次\n", title, a.empty() ? "拒绝" : a.c_str(),
                   b.empty() ? "拒绝" : b.c_str(), real.calls);
   }

   int main() {
       {
           Session s; RealUrlService real;
           Guard guard(real, s);
           Cache cache(guard);                       // 错：Cache(Guard(Real))
           run("Cache 在外：", cache, s, real);
       }
       {
           Session s; RealUrlService real;
           Cache cache(real);
           Guard guard(cache, s);                    // 对：Guard(Cache(Real))，每次都先检查权限
           run("Guard 在外：", guard, s, real);
       }
   }
   ```

   输出：

   ```text
   Cache 在外：
     会员：https://cdn.example/vip-movie?token=…
     游客：https://cdn.example/vip-movie?token=…
     真实服务被调用 1 次
   Guard 在外：
     会员：https://cdn.example/vip-movie?token=…
     游客：拒绝
     真实服务被调用 1 次
   ```

   两种顺序真实服务都只被调用 1 次，缓存的效果一样，区别只在安全。如果一定要把缓存放在外面（比如缓存在 CDN 边缘节点上），就要**把影响结果的一切都放进缓存的键**：键不能只有 `id`，还要有「是不是会员」（或者用户 ID），让会员和游客的结果分开存。一般规律：缓存代理的结果和谁有关，键里就要有谁。

4. `is_keyframe` 的返回类型是 `auto`，`key[i]` 在非 `const` 的 `vector<bool>` 上返回的是 `vector<bool>::reference`，所以 `auto` 推导出的返回类型就是这个代理对象，而不是 `bool`。代理里存着指向 `key` 内部存储的指针，函数返回时 `key` 已经析构了，`main` 再把它转换成 `bool`，读的是已经释放的内存——未定义行为。Clang 下读到了被覆盖的内容，输出 0；换一个编译器、换一个优化级别，结果可能又不一样。打开 AddressSanitizer 一跑就看出来了：

   ```text
   ==ERROR: AddressSanitizer: heap-use-after-free on address ...
   READ of size 8 at ... thread T0
       #0 ... in std::__1::__bit_reference<...>::operator bool() const
       #1 ... in main ex4_bug.cpp:19
   ```

   GCC 的 ASan 同样报 `heap-use-after-free`。如果 `key` 是 `vector<int>`，`auto` 推导出 `int`，就不会有这个问题——这正是代理对象和 `auto` 的坑。改法是**写明返回类型**，在 `key` 还活着的时候就转换成 `bool`：

   ```cpp
   // 《代理模式》：练习 4 的答案：返回类型写明 bool，代理对象在 return 时就转换成 bool 值
   #include <cstdio>
   #include <vector>

   // 扫描一遍，记下每个包是不是关键帧（这里用每 48 个包一个关键帧模拟）
   std::vector<bool> scan_keyframes(int packets) {
       std::vector<bool> key(static_cast<std::size_t>(packets));
       for (int i = 0; i < packets; i += 48) key[static_cast<std::size_t>(i)] = true;
       return key;
   }

   bool is_keyframe(int packets, int i) {
       std::vector<bool> key = scan_keyframes(packets);
       return key[static_cast<std::size_t>(i)];       // 在 key 还活着的时候转换成 bool
   }

   int main() {
       int n = 0;
       for (int i = 0; i < 240; ++i) n += is_keyframe(240, i);   // 应该是 5
       std::printf("关键帧 %d 个\n", n);
   }
   ```

   输出「关键帧 5 个」。一般规律：函数返回 `auto`、或者用 `auto` 接住 `operator[]` 的结果时，想一想右边是不是代理对象。

</details>
