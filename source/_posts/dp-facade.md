---
title: 外观模式
date: 2026-11-11 16:00:00
updated: 2026-11-11 16:00:00
categories:
  - 设计模式
  - 结构型模式
tags:
  - 设计模式
  - 外观
  - FFmpeg
  - RAII
order: 5
description: 把视频的第 5 秒存成一张缩略图，直接用 FFmpeg 的 C API 要写 73 行、调用 4 个库里的 26 个函数，而且顺序不能错、出错时要按相反的顺序释放；每个调用方各写一遍，就会各错各的：只解码一帧就停，要第 5 秒却拿到第 3.458 秒的画面（seek 只能落到关键帧上），错误分支提前返回，漏关了 2 个句柄。外观模式用一个 Thumbnailer 类把这串调用收起来，调用方只写 1 行、不 include 任何 FFmpeg 头文件。实测外观的额外开销可以忽略（每张缩略图 32.32 ms 对 32.35 ms），复用同一个对象反而快 15%；但外观没有让编译变快，main.cpp 反而从 45 ms 变成 148 ms。再讲外观不禁止直接使用子系统、外观会不会变成上帝类，以及外观和适配器、中介者、单例的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-facade]
---

> **本文目标**：理解外观模式解决的问题：一个子系统要按固定顺序调用很多函数才能用；会写一个外观类，把调用顺序、错误处理和资源释放收进去；知道外观不禁止直接使用子系统，会按使用场景划分外观，避免它变成上帝类；分清外观和适配器、中介者、单例。
>
> **前置知识**：上一篇《装饰器模式》；《适配器模式》（同样用 FFmpeg 举例）、《桥接模式》里的 Pimpl；C/C++ 专栏的《智能指针：unique_ptr、shared_ptr 与 weak_ptr》；音视频专栏里解封装、解码、关键帧的概念。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/15-facade/` 目录，FFmpeg 的部分在 `ffmpeg/` 子目录（`./build.sh` 编译）。

## 1. 问题：存一张缩略图，要调 26 个函数

### 1.1 直接用 FFmpeg

视频网站的列表页、播放器的进度条预览、分享出去的卡片，都要从视频里截一张图。需求只有一句话：**把视频的第 N 秒存成一张 320 像素宽的 PNG**。

用 FFmpeg 的 C API 做这件事，要经过 4 个子系统：

<figure class="diagram">
<svg viewBox="0 0 570 450" role="img" aria-label="一张缩略图要依次经过解封装、解码、缩放、编码 PNG 4 个子系统，每个子系统有自己的打开、使用、释放函数，共 26 个函数"><g style="font-family:var(--mono)"><rect x="10" y="10" width="250" height="156" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="22.0" y="32.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">① libavformat 解封装</text><text x="22.0" y="56.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avformat_open_input</text><text x="22.0" y="75.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avformat_find_stream_info</text><text x="22.0" y="94.0" text-anchor="start" style="fill:var(--fg);font-size:12px">av_find_best_stream</text><text x="22.0" y="113.0" text-anchor="start" style="fill:var(--link);font-size:12px">av_seek_frame</text><text x="22.0" y="132.0" text-anchor="start" style="fill:var(--fg);font-size:12px">av_read_frame</text><text x="22.0" y="151.0" text-anchor="start" style="fill:var(--danger);font-size:12px">avformat_close_input</text><rect x="310" y="10" width="250" height="156" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="322.0" y="32.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">② libavcodec 解码</text><text x="322.0" y="56.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_alloc_context3</text><text x="322.0" y="75.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_parameters_to_context</text><text x="322.0" y="94.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_open2</text><text x="322.0" y="113.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_send_packet</text><text x="322.0" y="132.0" text-anchor="start" style="fill:var(--link);font-size:12px">avcodec_receive_frame</text><text x="322.0" y="151.0" text-anchor="start" style="fill:var(--danger);font-size:12px">avcodec_free_context</text><rect x="310" y="202" width="250" height="156" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="322.0" y="224.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">③ libswscale 缩放</text><text x="322.0" y="248.0" text-anchor="start" style="fill:var(--fg);font-size:12px">sws_getContext</text><text x="322.0" y="267.0" text-anchor="start" style="fill:var(--fg);font-size:12px">sws_scale</text><text x="322.0" y="286.0" text-anchor="start" style="fill:var(--danger);font-size:12px">sws_freeContext</text><rect x="10" y="202" width="250" height="156" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="22.0" y="224.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">④ libavcodec 编码 PNG</text><text x="22.0" y="248.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_find_encoder</text><text x="22.0" y="267.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_alloc_context3</text><text x="22.0" y="286.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_open2</text><text x="22.0" y="305.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_send_frame</text><text x="22.0" y="324.0" text-anchor="start" style="fill:var(--fg);font-size:12px">avcodec_receive_packet</text><text x="22.0" y="343.0" text-anchor="start" style="fill:var(--danger);font-size:12px">avcodec_free_context</text><line x1="264" y1="88.0" x2="306" y2="88.0" style="stroke:var(--muted);stroke-width:1.5"/><path d="M306,88.0 L299,83.0 L299,93.0 z" style="fill:var(--muted)"/><line x1="435.0" y1="170" x2="435.0" y2="198" style="stroke:var(--muted);stroke-width:1.5"/><path d="M435.0,198 L430.0,191 L440.0,191 z" style="fill:var(--muted)"/><line x1="306" y1="280.0" x2="264" y2="280.0" style="stroke:var(--muted);stroke-width:1.5"/><path d="M264,280.0 L271,275.0 L271,285.0 z" style="fill:var(--muted)"/><text x="10.0" y="382.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">④ 沿用 alloc_context3 / open2 / free_context；另有 av_packet_alloc/free/unref、</text><text x="10.0" y="400.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">av_frame_alloc/free/get_buffer/unref、av_q2d 8 个公用函数，共 26 个不同的函数。</text><text x="10.0" y="418.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">蓝色：seek 只能落到关键帧，要循环 receive 到目标时间。</text><text x="10.0" y="436.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">红色：释放函数，按打开的相反顺序调用；任何一步失败，都要释放前面已经打开的。</text></g></svg>
</figure>

每个子系统都有自己的「打开、使用、释放」三步，后一步依赖前一步的结果：解码器要用解封装器找到的流参数来打开；缩放要知道解码出来的帧的尺寸和像素格式；PNG 编码器要知道缩放后的尺寸。写出来是这样的：

<!-- run: off -->
```cpp
// 《外观模式》：不用外观，直接调 FFmpeg 的 C API 把视频第 N 秒存成 PNG 缩略图
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <cerrno>
#include <cstdio>
#include <cstdlib>

// 把 in 的第 sec 秒缩放到宽 width，存成 PNG；成功返回 0，失败返回 FFmpeg 的错误码
int save_thumbnail_raw(const char* in, double sec, int width, const char* out) {
    AVFormatContext* fmt = nullptr;
    AVCodecContext *dec = nullptr, *enc = nullptr;
    SwsContext* sws = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb = av_frame_alloc();
    FILE* fp = nullptr;
    const AVCodec *codec, *png;
    AVStream* st;
    int ret, vs, height;
    int64_t target;
    bool got = false;
    if (!pkt || !frame || !rgb) { ret = AVERROR(ENOMEM); goto end; }

    // 1. 解封装：打开文件，读流信息，找到视频流
    if ((ret = avformat_open_input(&fmt, in, nullptr, nullptr)) < 0) goto end;
    if ((ret = avformat_find_stream_info(fmt, nullptr)) < 0) goto end;
    if ((ret = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0)) < 0) goto end;
    vs = ret;
    st = fmt->streams[vs];

    // 2. 打开解码器
    if (!(dec = avcodec_alloc_context3(codec))) { ret = AVERROR(ENOMEM); goto end; }
    if ((ret = avcodec_parameters_to_context(dec, st->codecpar)) < 0) goto end;
    if ((ret = avcodec_open2(dec, codec, nullptr)) < 0) goto end;

    // 3. seek 只能落到目标之前的关键帧，所以要一直解码到 pts >= 目标
    target = (int64_t)(sec / av_q2d(st->time_base));
    if ((ret = av_seek_frame(fmt, vs, target, AVSEEK_FLAG_BACKWARD)) < 0) goto end;
    while (!got) {
        ret = av_read_frame(fmt, pkt);
        if (ret == AVERROR_EOF) {
            ret = avcodec_send_packet(dec, nullptr);           // 读完了：冲刷解码器里剩下的帧
        } else if (ret < 0) {
            goto end;
        } else {
            ret = pkt->stream_index == vs ? avcodec_send_packet(dec, pkt) : 0;
            av_packet_unref(pkt);
        }
        if (ret < 0) goto end;
        while ((ret = avcodec_receive_frame(dec, frame)) >= 0) {
            if (frame->best_effort_timestamp >= target) { got = true; break; }
            av_frame_unref(frame);
        }
        if (!got && ret != AVERROR(EAGAIN)) goto end;          // AVERROR_EOF：要的时间超出了视频长度
    }

    // 4. 缩放并转换成 RGB24
    height = width * frame->height / frame->width / 2 * 2;
    sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                         width, height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) { ret = AVERROR(EINVAL); goto end; }
    rgb->format = AV_PIX_FMT_RGB24;
    rgb->width = width;
    rgb->height = height;
    if ((ret = av_frame_get_buffer(rgb, 0)) < 0) goto end;
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, rgb->data, rgb->linesize);

    // 5. 编码成 PNG
    png = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!png || !(enc = avcodec_alloc_context3(png))) { ret = AVERROR(ENOMEM); goto end; }
    enc->width = width;
    enc->height = height;
    enc->pix_fmt = AV_PIX_FMT_RGB24;
    enc->time_base = AVRational{1, 25};
    if ((ret = avcodec_open2(enc, png, nullptr)) < 0) goto end;
    if ((ret = avcodec_send_frame(enc, rgb)) < 0) goto end;
    if ((ret = avcodec_receive_packet(enc, pkt)) < 0) goto end;

    // 6. 写文件
    if (!(fp = std::fopen(out, "wb"))) { ret = AVERROR(errno); goto end; }
    std::fwrite(pkt->data, 1, pkt->size, fp);
    ret = 0;

end:  // 7. 不管从哪一步失败跳过来，都按相反的顺序释放
    if (fp) std::fclose(fp);
    avcodec_free_context(&enc);
    sws_freeContext(sws);
    av_frame_free(&rgb);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return ret;
}

#ifndef NO_MAIN
int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "用法：%s 视频 秒 输出.png\n", argv[0]); return 1; }
    int ret = save_thumbnail_raw(argv[1], std::atof(argv[2]), 320, argv[3]);
    if (ret < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, msg, sizeof msg);
        std::fprintf(stderr, "失败：%s\n", msg);
        return 1;
    }
    std::printf("写出 %s\n", argv[3]);
}
#endif
```

`ffmpeg/measure.sh` 统计了这个函数：去掉空行和注释还有 **73 行，用到 26 个不同的 FFmpeg 函数**，调用它的文件要 include 4 个 FFmpeg 头文件、链接 4 个库。其中有两处很容易写错：

- **seek 只能落到关键帧上**。`av_seek_frame` 加 `AVSEEK_FLAG_BACKWARD` 会跳到目标时间之前最近的关键帧，因为解码必须从关键帧开始。`bbb-10s.mp4` 的关键帧在 0、1.292、3.292、3.458、5.458、7.458、9.458 秒，要第 5 秒，seek 之后读到的第一帧是第 3.458 秒的，必须继续解码 38 帧，直到时间戳 ≥ 5 秒。
- **释放的顺序和时机**。任何一步失败都要释放前面已经打开的东西。C 里通常用 `goto end` 跳到函数末尾统一释放，所以所有变量都要在第一个 `goto` 之前声明好（C++ 不允许 `goto` 跳过带初始化的声明）。

### 1.2 每个调用方各写一遍

这 73 行不会只出现一次：列表页的预览图、分享卡片、后台的封面生成，每个要截图的地方都要写一遍。下面用一个**简化的子系统**模拟这个情况（和 FFmpeg 一样是 C 风格的接口：4 种句柄、15 个函数，关键帧的位置和 `bbb-10s.mp4` 相同，`g_live` 记录还没关闭的句柄数），两位同事各写了一个调用方：

```cpp
// 《外观模式》：没有外观——每个调用方都自己按顺序调用一串子系统函数，各错各的
#include <cstdio>
#include <string>

// ---------- 子系统：仿照 FFmpeg 的 C 接口写的简化版 ----------
int g_live = 0, g_opened = 0, g_decoded = 0;          // 没关闭的句柄、打开过几次文件、解码了几个包
const int kKey[] = {0, 31, 79, 83, 131, 179, 227};    // bbb-10s.mp4 的关键帧（第几帧，24 帧/秒）
struct Packet { int n = 0; double pts = 0; bool key = false; };
struct Frame  { double pts = -1; int w = 1280, h = 720; };
struct Demuxer { int next = 0; };
struct Decoder { Packet in; bool has = false; };
struct Scaler  { int w, h; };
struct Encoder { int w, h; };

Demuxer* demux_open(const std::string& path) { if (path.empty()) return nullptr; ++g_live; ++g_opened; return new Demuxer; }
int  demux_find_video(Demuxer*) { return 0; }
void demux_seek(Demuxer* d, double t) {               // 只能跳到 t 之前最近的关键帧
    for (int k : kKey) if (k / 24.0 <= t + 1e-9) d->next = k;
}
bool demux_read(Demuxer* d, Packet* p) {
    if (d->next >= 240) return false;
    p->n = d->next++; p->pts = p->n / 24.0; p->key = false;
    for (int k : kKey) if (k == p->n) p->key = true;
    return true;
}
void demux_close(Demuxer* d) { if (d) { --g_live; delete d; } }

Decoder* dec_open(Demuxer*, int stream) { if (stream < 0) return nullptr; ++g_live; return new Decoder; }
void dec_send(Decoder* c, const Packet& p) { c->in = p; c->has = true; ++g_decoded; }
bool dec_receive(Decoder* c, Frame* f) { if (!c->has) return false; c->has = false; f->pts = c->in.pts; return true; }
void dec_close(Decoder* c) { if (c) { --g_live; delete c; } }

Scaler* scale_open(int, int, int w, int h) { if (w <= 0 || h <= 0) return nullptr; ++g_live; return new Scaler{w, h}; }
void scale_run(Scaler* s, const Frame& in, Frame* out) { *out = in; out->w = s->w; out->h = s->h; }
void scale_close(Scaler* s) { if (s) { --g_live; delete s; } }

Encoder* png_open(int w, int h) { ++g_live; return new Encoder{w, h}; }
bool png_write(Encoder*, const Frame& f, const std::string& path) {
    std::printf("  写出 %s：%dx%d，第 %.3f 秒的画面\n", path.c_str(), f.w, f.h, f.pts);
    return true;
}
void png_close(Encoder* e) { if (e) { --g_live; delete e; } }

// ---------- 调用方 1：视频列表的预览图（同事 A 写的） ----------
bool preview(const std::string& in, double t, const std::string& out) {
    Demuxer* dm = demux_open(in);
    if (!dm) return false;
    Decoder* dec = dec_open(dm, demux_find_video(dm));
    if (!dec) { demux_close(dm); return false; }
    demux_seek(dm, t);
    Packet pkt; Frame f;
    demux_read(dm, &pkt);                 // 读一个包、解一帧就当成第 t 秒的画面
    dec_send(dec, pkt);
    dec_receive(dec, &f);
    Scaler* sc = scale_open(f.w, f.h, 320, 180);
    Frame small;
    scale_run(sc, f, &small);
    Encoder* enc = png_open(320, 180);
    bool ok = png_write(enc, small, out);
    png_close(enc); scale_close(sc); dec_close(dec); demux_close(dm);
    return ok;
}

// ---------- 调用方 2：分享卡片（同事 B 写的） ----------
bool share_card(const std::string& in, double t, int width, const std::string& out) {
    Demuxer* dm = demux_open(in);
    if (!dm) return false;
    Decoder* dec = dec_open(dm, demux_find_video(dm));
    if (!dec) { demux_close(dm); return false; }
    demux_seek(dm, t);                    // seek 只能落到关键帧，要一直解码到 pts >= t
    Packet pkt; Frame f; bool got = false;
    while (!got && demux_read(dm, &pkt)) {
        dec_send(dec, pkt);
        while (dec_receive(dec, &f)) if (f.pts >= t - 1e-9) { got = true; break; }
    }
    if (!got) { dec_close(dec); demux_close(dm); return false; }
    Scaler* sc = scale_open(f.w, f.h, width, width * 9 / 16);
    if (!sc) return false;                // 宽度不合法时提前返回……忘了关 dec 和 dm
    Frame small;
    scale_run(sc, f, &small);
    Encoder* enc = png_open(small.w, small.h);
    bool ok = png_write(enc, small, out);
    png_close(enc); scale_close(sc); dec_close(dec); demux_close(dm);
    return ok;
}

int main() {
    std::printf("预览图，要第 5 秒：\n");
    preview("bbb-10s.mp4", 5.0, "preview.png");
    std::printf("分享卡片，要第 5 秒，宽 640：\n");
    share_card("bbb-10s.mp4", 5.0, 640, "card.png");
    std::printf("分享卡片，宽度配置成了 0：%s\n", share_card("bbb-10s.mp4", 5.0, 0, "card.png") ? "成功" : "失败");
    std::printf("还没关闭的句柄：%d\n", g_live);
}
```

```
预览图，要第 5 秒：
  写出 preview.png：320x180，第 3.458 秒的画面
分享卡片，要第 5 秒，宽 640：
  写出 card.png：640x360，第 5.000 秒的画面
分享卡片，宽度配置成了 0：失败
还没关闭的句柄：2
```

两份代码各错了一处：

- **同事 A 不知道 seek 只能落到关键帧**，读一个包、解一帧就停。要第 5 秒，拿到的是第 3.458 秒。用真正的 FFmpeg 也是这样：把 `raw_thumb.cpp` 改成「收到第一帧就停」，要第 5、2.5、9 秒，拿到的分别是第 3.458、1.292、7.458 秒的画面——都是前面那个关键帧：

<img src="/img/dp/facade-seek-keyframe.jpg" alt="左边是 seek 后解出的第一帧（3.458 秒，大兔子在花丛前）；右边是一直解码到 5 秒的画面（大兔子转过脸来），两张图明显不同" width="660">

- **同事 B 在错误分支里提前返回**，忘了关闭已经打开的解码器和解封装器，泄漏了 2 个句柄。正常路径测不出来，只有宽度配置错了才会走到这里。

这两个错误都不是因为同事不认真，而是因为**每个调用方都要知道子系统的全部细节**：4 种句柄、15 个函数、它们的调用顺序、关键帧的规则、每条错误路径上要释放什么。知道的越多，可能错的地方就越多；同样的细节散落在多个地方，改一处还要找到所有的副本。

## 2. 外观模式

### 2.1 意图

GoF 的定义：

> 为子系统中的一组接口提供一个一致的界面。外观模式定义了一个高层接口，这个接口使得这一子系统更加容易使用。

大白话：**子系统用起来很麻烦时，写一个类替调用方把麻烦事做完，调用方只跟这个类打交道**。就像去医院看病，挂号、分诊、缴费、取药要跑好几个窗口；如果有一个「陪诊员」，你只需要告诉他你哪里不舒服。

外观（Facade，原意是建筑的正面）这个名字很形象：从街上只能看到房子的正面，背后有多少房间、管道怎么走，路人不需要知道。

### 2.2 用外观重构

子系统一行都不改，在它前面加一个 `Thumbnailer` 类：

```cpp
// 《外观模式》：Thumbnailer 外观——调用方只写一行，调用顺序和释放顺序都由外观负责
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

// ---------- 子系统：仿照 FFmpeg 的 C 接口写的简化版 ----------
int g_live = 0, g_opened = 0, g_decoded = 0;          // 没关闭的句柄、打开过几次文件、解码了几个包
const int kKey[] = {0, 31, 79, 83, 131, 179, 227};    // bbb-10s.mp4 的关键帧（第几帧，24 帧/秒）
struct Packet { int n = 0; double pts = 0; bool key = false; };
struct Frame  { double pts = -1; int w = 1280, h = 720; };
struct Demuxer { int next = 0; };
struct Decoder { Packet in; bool has = false; };
struct Scaler  { int w, h; };
struct Encoder { int w, h; };

Demuxer* demux_open(const std::string& path) { if (path.empty()) return nullptr; ++g_live; ++g_opened; return new Demuxer; }
int  demux_find_video(Demuxer*) { return 0; }
void demux_seek(Demuxer* d, double t) {               // 只能跳到 t 之前最近的关键帧
    for (int k : kKey) if (k / 24.0 <= t + 1e-9) d->next = k;
}
bool demux_read(Demuxer* d, Packet* p) {
    if (d->next >= 240) return false;
    p->n = d->next++; p->pts = p->n / 24.0; p->key = false;
    for (int k : kKey) if (k == p->n) p->key = true;
    return true;
}
void demux_close(Demuxer* d) { if (d) { --g_live; delete d; } }

Decoder* dec_open(Demuxer*, int stream) { if (stream < 0) return nullptr; ++g_live; return new Decoder; }
void dec_send(Decoder* c, const Packet& p) { c->in = p; c->has = true; ++g_decoded; }
bool dec_receive(Decoder* c, Frame* f) { if (!c->has) return false; c->has = false; f->pts = c->in.pts; return true; }
void dec_close(Decoder* c) { if (c) { --g_live; delete c; } }

Scaler* scale_open(int, int, int w, int h) { if (w <= 0 || h <= 0) return nullptr; ++g_live; return new Scaler{w, h}; }
void scale_run(Scaler* s, const Frame& in, Frame* out) { *out = in; out->w = s->w; out->h = s->h; }
void scale_close(Scaler* s) { if (s) { --g_live; delete s; } }

Encoder* png_open(int w, int h) { ++g_live; return new Encoder{w, h}; }
bool png_write(Encoder*, const Frame& f, const std::string& path) {
    std::printf("  写出 %s：%dx%d，第 %.3f 秒的画面\n", path.c_str(), f.w, f.h, f.pts);
    return true;
}
void png_close(Encoder* e) { if (e) { --g_live; delete e; } }

// ---------- 外观：把上面那串调用和释放顺序收进一个类 ----------
class Thumbnailer {
public:
    explicit Thumbnailer(const std::string& path) : dm_(demux_open(path), demux_close) {
        if (dm_) dec_.reset(dec_open(dm_.get(), demux_find_video(dm_.get())));
    }
    bool ok() const { return dm_ && dec_; }

    // 第 t 秒的画面：seek 只能落到前面的关键帧，所以要一直解码到 pts >= t
    std::optional<Frame> grab(double t) {
        if (!ok()) return std::nullopt;
        demux_seek(dm_.get(), t);
        Packet pkt; Frame f;
        while (demux_read(dm_.get(), &pkt)) {
            dec_send(dec_.get(), pkt);
            while (dec_receive(dec_.get(), &f)) if (f.pts >= t - 1e-9) return f;
        }
        return std::nullopt;
    }

    // 第 t 秒的画面缩放到宽 width，存成 PNG
    bool save(double t, const std::string& out, int width = 320) {
        auto f = grab(t);
        if (!f || width <= 0) return false;
        Handle<Scaler> sc(scale_open(f->w, f->h, width, width * f->h / f->w), scale_close);
        if (!sc) return false;            // 提前返回也没关系：所有句柄都由析构函数关闭
        Frame small;
        scale_run(sc.get(), *f, &small);
        Handle<Encoder> enc(png_open(small.w, small.h), png_close);
        return png_write(enc.get(), small, out);
    }

private:
    template <class T> using Handle = std::unique_ptr<T, void (*)(T*)>;
    Handle<Demuxer> dm_;
    Handle<Decoder> dec_{nullptr, dec_close};   // 后声明的先析构：先关解码器，再关文件
};

// 只要一张图时，一个函数就够了
bool save_thumbnail(const std::string& in, double t, const std::string& out, int width = 320) {
    return Thumbnailer(in).save(t, out, width);
}

int main() {
    std::printf("预览图，要第 5 秒：\n");
    save_thumbnail("bbb-10s.mp4", 5.0, "preview.png");
    std::printf("分享卡片，要第 5 秒，宽 640：\n");
    save_thumbnail("bbb-10s.mp4", 5.0, "card.png", 640);
    std::printf("分享卡片，宽度配置成了 0：%s\n", save_thumbnail("bbb-10s.mp4", 5.0, "card.png", 0) ? "成功" : "失败");
    std::printf("还没关闭的句柄：%d\n", g_live);
}
```

```
预览图，要第 5 秒：
  写出 preview.png：320x180，第 5.000 秒的画面
分享卡片，要第 5 秒，宽 640：
  写出 card.png：640x360，第 5.000 秒的画面
分享卡片，宽度配置成了 0：失败
还没关闭的句柄：0
```

两个错误都消失了，原因各不相同：

- 「解码到 pts ≥ t」这件事只在 `grab()` 里写一次。写对一次，所有调用方都对；将来要改（比如换成精确到帧的 `AVSEEK_FLAG_ANY` 策略），也只改这一处。
- 句柄放在 `unique_ptr` 里，析构函数负责关闭。`save()` 中途 `return false`，已经打开的 `Scaler` 照样会被关闭；`dm_` 和 `dec_` 在 `Thumbnailer` 析构时关闭，先关解码器、再关文件（成员按声明的相反顺序析构）。

调用方从 18～21 行变成 1 行，接触的函数从 15 个变成 1 个。下面的演示对比两种写法里调用方要做的事，第三个按钮是第 3.1 节要讲的「绕过外观」：

<div class="dsviz" data-viz="dp-facade"></div>

### 2.3 类图和时序图

```uml
label: 外观模式：调用方只依赖 Thumbnailer；Thumbnailer 组合了解封装、解码、缩放、编码四个子系统对象；调用方也可以绕过外观直接使用 Demuxer
classes:
  - { id: Client, name: 调用方, x: 20, y: 120, w: 100 }
  - { id: Thumbnailer, x: 210, y: 96, w: 210, ops: ['+ grab(t) : optional<Frame>', '+ save(t, out, width) : bool'] }
  - { id: Demuxer, x: 540, y: 10, w: 170, ops: ['open / seek / read / close'] }
  - { id: Decoder, x: 540, y: 86, w: 170, ops: ['open / send / receive / close'] }
  - { id: Scaler, x: 540, y: 162, w: 170, ops: ['open / run / close'] }
  - { id: PngEncoder, x: 540, y: 238, w: 170, ops: ['open / write / close'] }
rels:
  - { from: Client, to: Thumbnailer, type: assoc }
  - { from: Thumbnailer, to: Demuxer, type: compose, fdx: -18, mid: 470 }
  - { from: Thumbnailer, to: Decoder, type: compose, fdx: -6, mid: 495 }
  - { from: Thumbnailer, to: Scaler, type: depend, fdx: 6, mid: 495 }
  - { from: Thumbnailer, to: PngEncoder, type: depend, fdx: 18, mid: 470, label: 用时创建 }
  - { from: Client, to: Demuxer, type: depend, label: 也可以直接用, points: [[70, 120], [70, 30], [540, 30]] }
notes:
  - { x: 200, y: 200, w: 250, text: ['知道调用顺序和释放规则', '子系统不知道外观的存在'], to: Thumbnailer }
```

类图里要注意三点：

- **子系统不知道外观**。箭头都从外观指向子系统，`Demuxer`、`Decoder` 里没有任何一行代码提到 `Thumbnailer`。去掉外观，子系统照样能用。
- **外观持有子系统的对象**。解封装器和解码器在 `grab()` 之间要保留（组合，实心菱形），缩放器和编码器每次 `save()` 时临时创建（依赖，虚线）。
- **外观不挡路**。调用方仍然可以直接用子系统（左上那条虚线），第 3.1 节会看到什么时候需要这样做。

一次 `save()` 的调用顺序：

```uml
label: 时序图：调用方调用 save，Thumbnailer 依次调用 Demuxer 的 seek、循环读包解码、Scaler 缩放、PngEncoder 写文件，最后返回
participants:
  - { id: c, name: 调用方 }
  - { id: t, name: Thumbnailer }
  - { id: dm, name: Demuxer }
  - { id: dec, name: Decoder }
  - { id: sc, name: Scaler }
  - { id: enc, name: PngEncoder }
gap: 40
messages:
  - { from: c, to: t, text: 'save(5.0, "thumb.png")' }
  - { from: t, to: dm, text: 'seek(5.0)', note: '落到 3.458 s 的关键帧' }
  - { from: t, to: dm, text: 'read(&pkt)' }
  - { from: t, to: dec, text: 'send(pkt) / receive(&f)' }
  - { from: t, to: sc, text: 'open / run', type: create }
  - { from: t, to: enc, text: 'open / write', type: create }
  - { from: t, to: c, text: 'true', type: reply }
frags:
  - { from: 2, to: 3, label: 'loop [f.pts < 5.0]' }
```

### 2.4 真正的 FFmpeg 外观

模拟的子系统说明了道理，下面把 `raw_thumb.cpp` 的 73 行真正包成一个外观。先看头文件，这是调用方唯一需要看的东西：

<!-- run: off -->
```cpp
// 《外观模式》：Thumbnailer 外观的接口——头文件里没有任何 FFmpeg 的类型
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct Image {
    int width = 0, height = 0;
    double time = 0;                  // 这一帧实际的时间（秒）
    std::vector<uint8_t> rgb;         // RGB24，每行 width * 3 字节
};

class Thumbnailer {
public:
    explicit Thumbnailer(const std::string& video);   // 打开失败抛 std::runtime_error
    ~Thumbnailer();
    Thumbnailer(Thumbnailer&&) noexcept;
    Thumbnailer& operator=(Thumbnailer&&) noexcept;

    double duration() const;                           // 视频长度（秒）
    Image grab(double sec, int width = 320);           // 第 sec 秒的画面，缩放到宽 width
    void save(double sec, const std::string& png, int width = 320);

private:
    struct Impl;                                       // FFmpeg 的东西都在 Impl 里
    std::unique_ptr<Impl> impl_;
};

// 只要一张图时，一个函数就够了
void save_thumbnail(const std::string& video, double sec, const std::string& png, int width = 320);
void save_png(const Image& img, const std::string& png);
```

头文件里**没有任何 FFmpeg 的类型**：FFmpeg 的句柄都藏在 `Impl` 里（这就是《桥接模式》里讲过的 Pimpl）。接口按使用场景分了三层：

- `save_thumbnail()`：只要一张图，一个函数搞定；
- `Thumbnailer` 对象：要从同一个视频取很多张图，打开一次、`grab()` 多次；
- `Image`：拿到像素自己处理（比如拼成缩略图墙），再用 `save_png()` 存盘。

调用方是这样的：

<!-- run: off -->
```cpp
// 《外观模式》：调用方只认识 Thumbnailer，不 include 任何 FFmpeg 的头文件，也不链接 FFmpeg 的库
#include "thumbnailer.h"
#include <cstdio>
#include <cstdlib>
#include <exception>

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "用法：%s 视频 秒 输出.png\n", argv[0]); return 1; }
    try {
        save_thumbnail(argv[1], std::atof(argv[2]), argv[3]);
        std::printf("写出 %s\n", argv[3]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "失败：%s\n", e.what());
        return 1;
    }
}
```

实现文件把第 1 节的那串调用重新组织了一遍：句柄交给带删除器的 `unique_ptr`，错误码统一转成异常，`AVFrame`、`SwsContext` 在多次 `grab()` 之间复用：

<details>
<summary>展开 thumbnailer.cpp（121 行）</summary>

<!-- run: off -->
```cpp
// 《外观模式》：Thumbnailer 外观的实现——FFmpeg 的调用顺序和释放顺序都关在这一个文件里
#include "thumbnailer.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include <cstdio>
#include <stdexcept>

namespace {
// 每种 FFmpeg 对象配一个删除器，交给 unique_ptr：释放顺序由成员的声明顺序决定，提前 return 或抛异常都不会漏
struct FmtDel   { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct CodecDel { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct FrameDel { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct PktDel   { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwsDel   { void operator()(SwsContext* p) const { sws_freeContext(p); } };
struct FileDel  { void operator()(FILE* p) const { std::fclose(p); } };
using FmtPtr   = std::unique_ptr<AVFormatContext, FmtDel>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDel>;
using FramePtr = std::unique_ptr<AVFrame, FrameDel>;
using PktPtr   = std::unique_ptr<AVPacket, PktDel>;
using SwsPtr   = std::unique_ptr<SwsContext, SwsDel>;

void check(int ret, const char* what) {           // FFmpeg 的错误码 → 异常
    if (ret >= 0) return;
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, msg, sizeof msg);
    throw std::runtime_error(std::string(what) + "：" + msg);
}
}  // namespace

struct Thumbnailer::Impl {
    FmtPtr fmt;
    CodecPtr dec;
    int vs = -1;
    PktPtr pkt{av_packet_alloc()};
    FramePtr frame{av_frame_alloc()};
    SwsPtr sws;

    explicit Impl(const std::string& path) {
        AVFormatContext* f = nullptr;
        check(avformat_open_input(&f, path.c_str(), nullptr, nullptr), "打开文件");
        fmt.reset(f);
        check(avformat_find_stream_info(f, nullptr), "读取流信息");
        const AVCodec* codec = nullptr;
        check(vs = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0), "查找视频流");
        dec.reset(avcodec_alloc_context3(codec));
        if (!dec || !pkt || !frame) throw std::bad_alloc();
        check(avcodec_parameters_to_context(dec.get(), f->streams[vs]->codecpar), "复制解码参数");
        check(avcodec_open2(dec.get(), codec, nullptr), "打开解码器");
    }

    AVStream* stream() const { return fmt->streams[vs]; }

    // seek 到 sec 之前的关键帧，再解码到 pts >= sec
    AVFrame* decodeAt(double sec) {
        int64_t target = int64_t(sec / av_q2d(stream()->time_base));
        check(av_seek_frame(fmt.get(), vs, target, AVSEEK_FLAG_BACKWARD), "seek");
        avcodec_flush_buffers(dec.get());              // 丢掉上一次 grab 留在解码器里的帧
        for (;;) {
            int ret = avcodec_receive_frame(dec.get(), frame.get());
            if (ret >= 0) {
                if (frame->best_effort_timestamp >= target) return frame.get();
                continue;                              // 下一次 receive 会先 unref
            }
            if (ret == AVERROR_EOF) throw std::runtime_error("超出了视频长度");
            if (ret != AVERROR(EAGAIN)) check(ret, "解码");
            ret = av_read_frame(fmt.get(), pkt.get());
            if (ret == AVERROR_EOF) {                  // 读完了：冲刷解码器里剩下的帧
                check(avcodec_send_packet(dec.get(), nullptr), "冲刷解码器");
                continue;
            }
            check(ret, "读包");
            ret = pkt->stream_index == vs ? avcodec_send_packet(dec.get(), pkt.get()) : 0;
            av_packet_unref(pkt.get());
            check(ret, "送包");
        }
    }
};

Thumbnailer::Thumbnailer(const std::string& video) : impl_(std::make_unique<Impl>(video)) {}
Thumbnailer::~Thumbnailer() = default;
Thumbnailer::Thumbnailer(Thumbnailer&&) noexcept = default;
Thumbnailer& Thumbnailer::operator=(Thumbnailer&&) noexcept = default;

double Thumbnailer::duration() const { return impl_->fmt->duration / double(AV_TIME_BASE); }

Image Thumbnailer::grab(double sec, int width) {
    if (width <= 0) throw std::invalid_argument("宽度必须大于 0");
    AVFrame* f = impl_->decodeAt(sec);
    Image img;
    img.width = width;
    img.height = width * f->height / f->width / 2 * 2;
    img.time = f->best_effort_timestamp * av_q2d(impl_->stream()->time_base);
    SwsContext* s = sws_getCachedContext(impl_->sws.release(), f->width, f->height, AVPixelFormat(f->format),
                                         img.width, img.height, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                                         nullptr, nullptr, nullptr);
    impl_->sws.reset(s);                               // 尺寸不变时复用上一次的 SwsContext
    if (!s) throw std::runtime_error("创建 SwsContext 失败");
    img.rgb.resize(size_t(img.width) * img.height * 3);
    uint8_t* dst[4] = {img.rgb.data()};
    int stride[4] = {img.width * 3};
    sws_scale(s, f->data, f->linesize, 0, f->height, dst, stride);
    return img;
}

void Thumbnailer::save(double sec, const std::string& png, int width) { save_png(grab(sec, width), png); }

void save_png(const Image& img, const std::string& path) {
    const AVCodec* png = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!png) throw std::runtime_error("没有 PNG 编码器");
    CodecPtr enc(avcodec_alloc_context3(png));
    FramePtr f(av_frame_alloc());
    PktPtr pkt(av_packet_alloc());
    if (!enc || !f || !pkt) throw std::bad_alloc();
    enc->width = img.width;
    enc->height = img.height;
    enc->pix_fmt = AV_PIX_FMT_RGB24;
    enc->time_base = AVRational{1, 25};
    check(avcodec_open2(enc.get(), png, nullptr), "打开 PNG 编码器");
    f->format = AV_PIX_FMT_RGB24;
    f->width = img.width;
    f->height = img.height;
    f->data[0] = const_cast<uint8_t*>(img.rgb.data());   // 借用 Image 的内存，编码器会自己复制
    f->linesize[0] = img.width * 3;
    check(avcodec_send_frame(enc.get(), f.get()), "编码");
    check(avcodec_receive_packet(enc.get(), pkt.get()), "取出 PNG");
    std::unique_ptr<FILE, FileDel> fp(std::fopen(path.c_str(), "wb"));
    if (!fp) throw std::runtime_error("无法写入 " + path);
    std::fwrite(pkt->data, 1, pkt->size, fp.get());
}

void save_thumbnail(const std::string& video, double sec, const std::string& png, int width) {
    Thumbnailer(video).save(sec, png, width);
}
```

</details>

`build.sh` 把外观编译成动态库 `libthumbnailer`，调用方只链接它。验证一下两种写法的结果完全一样：

```bash
$ cd projects/dp-lab/15-facade/ffmpeg && ./build.sh
$ ./build/raw_thumb ../../../../source/media/bbb-10s.mp4 5 build/r.png
$ ./build/thumb     ../../../../source/media/bbb-10s.mp4 5 build/f.png
$ cmp build/r.png build/f.png && echo 相同
相同
$ ./build/thumb ../../../../source/media/bbb-10s.mp4 11 build/x.png
失败：超出了视频长度
$ ./build/thumb nothing.mp4 1 build/x.png
失败：打开文件：No such file or directory
```

两个 PNG 逐字节相同（Clang/macOS 上的 FFmpeg 9.0 和 GCC/Ubuntu 上的 FFmpeg 6.1 都是）。`sheet.cpp` 用同一个 `Thumbnailer` 取 9 张图，拼成一张缩略图墙，每张都拿到了要的时间（或者紧随其后的那一帧）：

<img src="/img/dp/facade-sheet.jpg" alt="3×3 的缩略图墙：bbb-10s.mp4 在 0.6、1.7、2.8、3.9、5.0、6.1、7.3、8.3、9.5 秒的 9 个画面，有大兔子、松鼠、花和紫色蝴蝶" width="680">

```
要第 0.556 秒，拿到第 0.583 秒
要第 1.667 秒，拿到第 1.667 秒
……
要第 9.444 秒，拿到第 9.458 秒
写出 build/sheet.png：976x556
```

### 2.5 实测：调用方省了多少，外观花了多少

`measure.sh` 统计调用方要写的代码和依赖，两个编译器结果相同：

<figure class="diagram">
<svg viewBox="0 0 700 126" role="img" aria-label="调用方的代价：不用外观要写 73 行、调用 26 个 FFmpeg 函数、直接链接 4 个 FFmpeg 库；用外观只要 1 行、0 个函数、0 个库"><g style="font-family:var(--mono)"><text x="98.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">调用方的代码（行）</text><rect x="98.0" y="30.0" width="109.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="213.5" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">73</text><rect x="98.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="105.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1</text><text x="298.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">用到的 FFmpeg 函数</text><rect x="298.0" y="30.0" width="104.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="408.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">26</text><rect x="298.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="305.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0</text><text x="498.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">直接链接的 FFmpeg 库</text><rect x="498.0" y="30.0" width="96.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="600.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">4</text><rect x="498.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="505.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0</text><text x="90.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">不用外观</text><text x="90.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">用外观</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">ffmpeg/measure.sh：不算空行和注释；函数按名字去重；库数用 otool -L / readelf -d 看直接依赖。Clang 和 GCC 结果相同</text></g></svg>
</figure>

调用方的 1 行背后，外观自己有 121 行，同样用到 26 个 FFmpeg 函数——**复杂度没有消失，只是被集中到了一个地方**。这正是外观的价值：这 121 行只写一次、测一次，第 1.2 节那样的错误只会出现在一个地方，修一次就全修好了。

外观多了一层函数调用、一次异常检查、一个 `std::string` 参数，会不会变慢？`bench` 用三种写法各取 10 张缩略图：

<figure class="diagram">
<svg viewBox="0 0 690 156" role="img" aria-label="每张缩略图的耗时：直接调 C API 和一次性的 save_thumbnail 几乎一样，约 32.3 到 32.6 毫秒；复用同一个 Thumbnailer 约 26.5 到 27.6 毫秒"><g style="font-family:var(--mono)"><text x="148.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="148.0" y="30.0" width="145.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="299.4" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">32.32 ms</text><rect x="148.0" y="60.0" width="145.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="299.6" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">32.35 ms</text><rect x="148.0" y="90.0" width="124.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="278.0" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">27.56 ms</text><text x="418.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="418.0" y="30.0" width="146.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="570.5" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">32.56 ms</text><rect x="418.0" y="60.0" width="146.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="570.1" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">32.46 ms</text><rect x="418.0" y="90.0" width="119.4" height="20.0" rx="2" style="fill:var(--link)"/><text x="543.4" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">26.53 ms</text><text x="140.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">直接调 C API</text><text x="140.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">save_thumbnail()</text><text x="140.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">复用 Thumbnailer</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">ffmpeg/bench：bbb-10s.mp4 取 0.5～9.5 秒的 10 张 320×180 PNG，-O2，7 次取中位数，每轮轮换三种写法的顺序</text></g></svg>
</figure>

- 直接调 C API 和一次性的 `save_thumbnail()` 相差不到 0.4%，在测量误差以内。和打开文件、解码几十帧、编码 PNG 相比，外观多出来的那点开销看不见。
- 复用同一个 `Thumbnailer` 对象快了 15%（Clang）～18%（GCC）：只打开一次文件，`avformat_find_stream_info` 也只做一次（它要读一些包、甚至解码几帧来探测流的参数）。外观把「打开」和「取图」分成两步，反而让调用方更容易写出快的代码。

## 3. 外观的边界

### 3.1 外观不禁止直接使用子系统

GoF 特别强调：外观**不阻止**客户直接使用子系统类。外观照顾常见的用法，少见的用法仍然可以绕过它。

比如进度条拖动时要吸附到关键帧，需要列出所有关键帧的时间。`Thumbnailer` 没有这个功能，但也没必要为了这一个需求去改外观——直接用 `Demuxer` 就行，而且只读包、不解码：

```cpp
// 《外观模式》：外观不禁止直接使用子系统——一次性函数、可复用的对象、绕过外观三种用法的代价
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

// ---------- 子系统：仿照 FFmpeg 的 C 接口写的简化版 ----------
int g_live = 0, g_opened = 0, g_decoded = 0;          // 没关闭的句柄、打开过几次文件、解码了几个包
const int kKey[] = {0, 31, 79, 83, 131, 179, 227};    // bbb-10s.mp4 的关键帧（第几帧，24 帧/秒）
struct Packet { int n = 0; double pts = 0; bool key = false; };
struct Frame  { double pts = -1; int w = 1280, h = 720; };
struct Demuxer { int next = 0; };
struct Decoder { Packet in; bool has = false; };
struct Scaler  { int w, h; };
struct Encoder { int w, h; };

Demuxer* demux_open(const std::string& path) { if (path.empty()) return nullptr; ++g_live; ++g_opened; return new Demuxer; }
int  demux_find_video(Demuxer*) { return 0; }
void demux_seek(Demuxer* d, double t) {               // 只能跳到 t 之前最近的关键帧
    for (int k : kKey) if (k / 24.0 <= t + 1e-9) d->next = k;
}
bool demux_read(Demuxer* d, Packet* p) {
    if (d->next >= 240) return false;
    p->n = d->next++; p->pts = p->n / 24.0; p->key = false;
    for (int k : kKey) if (k == p->n) p->key = true;
    return true;
}
void demux_close(Demuxer* d) { if (d) { --g_live; delete d; } }

Decoder* dec_open(Demuxer*, int stream) { if (stream < 0) return nullptr; ++g_live; return new Decoder; }
void dec_send(Decoder* c, const Packet& p) { c->in = p; c->has = true; ++g_decoded; }
bool dec_receive(Decoder* c, Frame* f) { if (!c->has) return false; c->has = false; f->pts = c->in.pts; return true; }
void dec_close(Decoder* c) { if (c) { --g_live; delete c; } }

Scaler* scale_open(int, int, int w, int h) { if (w <= 0 || h <= 0) return nullptr; ++g_live; return new Scaler{w, h}; }
void scale_run(Scaler* s, const Frame& in, Frame* out) { *out = in; out->w = s->w; out->h = s->h; }
void scale_close(Scaler* s) { if (s) { --g_live; delete s; } }

Encoder* png_open(int w, int h) { ++g_live; return new Encoder{w, h}; }
bool png_write(Encoder*, const Frame& f, const std::string& path) {
    std::printf("  写出 %s：%dx%d，第 %.3f 秒的画面\n", path.c_str(), f.w, f.h, f.pts);
    return true;
}
void png_close(Encoder* e) { if (e) { --g_live; delete e; } }

// ---------- 外观：把上面那串调用和释放顺序收进一个类 ----------
class Thumbnailer {
public:
    explicit Thumbnailer(const std::string& path) : dm_(demux_open(path), demux_close) {
        if (dm_) dec_.reset(dec_open(dm_.get(), demux_find_video(dm_.get())));
    }
    bool ok() const { return dm_ && dec_; }

    // 第 t 秒的画面：seek 只能落到前面的关键帧，所以要一直解码到 pts >= t
    std::optional<Frame> grab(double t) {
        if (!ok()) return std::nullopt;
        demux_seek(dm_.get(), t);
        Packet pkt; Frame f;
        while (demux_read(dm_.get(), &pkt)) {
            dec_send(dec_.get(), pkt);
            while (dec_receive(dec_.get(), &f)) if (f.pts >= t - 1e-9) return f;
        }
        return std::nullopt;
    }

    // 第 t 秒的画面缩放到宽 width，存成 PNG
    bool save(double t, const std::string& out, int width = 320) {
        auto f = grab(t);
        if (!f || width <= 0) return false;
        Handle<Scaler> sc(scale_open(f->w, f->h, width, width * f->h / f->w), scale_close);
        if (!sc) return false;            // 提前返回也没关系：所有句柄都由析构函数关闭
        Frame small;
        scale_run(sc.get(), *f, &small);
        Handle<Encoder> enc(png_open(small.w, small.h), png_close);
        return png_write(enc.get(), small, out);
    }

private:
    template <class T> using Handle = std::unique_ptr<T, void (*)(T*)>;
    Handle<Demuxer> dm_;
    Handle<Decoder> dec_{nullptr, dec_close};   // 后声明的先析构：先关解码器，再关文件
};

// 只要一张图时，一个函数就够了
bool save_thumbnail(const std::string& in, double t, const std::string& out, int width = 320) {
    return Thumbnailer(in).save(t, out, width);
}

int main() {
    const double ts[] = {0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5};
    std::printf("10 张缩略图，每次新建一个 Thumbnailer（和 save_thumbnail() 一样）：\n");
    for (double t : ts) Thumbnailer("bbb-10s.mp4").grab(t);
    std::printf("  打开文件 %d 次，解码 %d 个包\n", g_opened, g_decoded);

    g_opened = g_decoded = 0;
    std::printf("10 张缩略图，用同一个 Thumbnailer 对象：\n");
    Thumbnailer th("bbb-10s.mp4");
    for (double t : ts) th.grab(t);
    std::printf("  打开文件 %d 次，解码 %d 个包\n", g_opened, g_decoded);

    // 外观没提供的功能，直接用子系统：列出所有关键帧（拖动进度条时吸附到关键帧），不需要解码
    g_opened = g_decoded = 0;
    std::printf("绕过外观，直接用 Demuxer 列出关键帧：\n ");
    Demuxer* dm = demux_open("bbb-10s.mp4");
    Packet pkt;
    while (demux_read(dm, &pkt)) if (pkt.key) std::printf(" %.3f", pkt.pts);
    demux_close(dm);
    std::printf("\n  打开文件 %d 次，解码 %d 个包\n", g_opened, g_decoded);
}
```

```
10 张缩略图，每次新建一个 Thumbnailer（和 save_thumbnail() 一样）：
  打开文件 10 次，解码 135 个包
10 张缩略图，用同一个 Thumbnailer 对象：
  打开文件 1 次，解码 135 个包
绕过外观，直接用 Demuxer 列出关键帧：
  0.000 1.292 3.292 3.458 5.458 7.458 9.458
  打开文件 1 次，解码 0 个包
```

这也是外观和「封装」的区别：封装把细节藏起来，外面**不能**访问；外观只是提供一条捷径，外面**不必**访问。FFmpeg 自己就是这样分层的：

| 层次 | 例子 | 谁用 |
|---|---|---|
| 命令行 | `ffmpeg -ss 5 -i bbb-10s.mp4 -frames:v 1 -vf scale=320:-2 thumb.png` | 只想要结果的人 |
| 高层 API | `avformat_open_input()` 一个函数里完成探测格式、打开 I/O、创建解封装器、读文件头 | 大多数程序 |
| 底层 API | `av_probe_input_format()`、`avio_alloc_context()`、自定义 `AVIOContext` 读内存里的数据 | 有特殊需求的程序 |

每一层都是下一层的外观，但下一层始终是公开的。`Thumbnailer` 也是这样：调用方需要 `Image` 的像素就用 `grab()`，需要 FFmpeg 的 `AVFrame` 就自己去调 FFmpeg。

真实世界里还有很多外观：libcurl 的 easy 接口（`curl_easy_perform()` 一个函数完成连接、发送、接收，内部其实是用更底层的 multi 接口实现的）、`std::filesystem::copy_file()`（一个函数背后是打开、读写、关闭两个文件，标准库在不同平台上还会用 `sendfile`、`copy_file_range`、`fcopyfile` 这类专门的系统调用）、SDL3 的 `SDL_CreateWindowAndRenderer()`。

### 3.2 编译依赖：外观没有让编译变快

常听到的一个说法是「外观减少了编译依赖，编译会变快」。实测一下：`raw_thumb.cpp` include 了 4 个 FFmpeg 头文件，`main.cpp` 只 include 了 `thumbnailer.h`：

<figure class="diagram">
<svg viewBox="0 0 800 126" role="img" aria-label="编译时间：Clang 上 raw_thumb.cpp 45 毫秒、只 include 外观头文件的 main.cpp 148 毫秒；GCC 上 59 和 74 毫秒"><g style="font-family:var(--mono)"><text x="258.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="258.0" y="30.0" width="50.6" height="20.0" rx="2" style="fill:var(--warn)"/><text x="314.6" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">45 ms</text><rect x="258.0" y="60.0" width="166.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="430.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">148 ms</text><text x="528.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="528.0" y="30.0" width="66.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="600.4" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">59 ms</text><rect x="528.0" y="60.0" width="83.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="617.2" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">74 ms</text><text x="250.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">raw_thumb.cpp（4 个 FFmpeg 头文件）</text><text x="250.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">main.cpp（只有 thumbnailer.h）</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">ffmpeg/measure.sh：-std=c++17 -O2 -c 编译成 .o，7 次取中位数</text></g></svg>
</figure>

结果反过来了：**只 include 外观头文件的 `main.cpp` 编译得更慢**（Clang 上 45 → 148 ms，GCC 上 59 → 74 ms）。预处理后的行数说明了原因：

| | Clang / macOS | GCC 13 / Ubuntu |
|---|---|---|
| FFmpeg 的 4 个头文件单独预处理 | 14510 行 | 28882 行 |
| `<memory>` `<string>` `<vector>` 单独预处理 | 53545 行 | 37762 行 |
| `raw_thumb.cpp` 预处理后 | 14762 行 | 29128 行 |
| `main.cpp` 预处理后 | 53591 行 | 37882 行 |

FFmpeg 的头文件是 C 写的，只有声明和结构体；`thumbnailer.h` 为了用 `std::string`、`std::vector`、`std::unique_ptr` 带进来的 C++ 标准库头文件反而更大（libc++ 的这三个头文件比 libstdc++ 的还大 40%）。在真实的项目里，调用方本来就会 include 这些标准库头文件，这部分差别会被摊掉；但这个例子说明，**外观的价值不在编译速度**。

外观在编译上真正带来的是**隔离**：

- 调用方的源码里没有 FFmpeg 的头文件，编译调用方时不需要装 FFmpeg 的开发包；
- FFmpeg 升级、换一个版本的头文件，只有 `thumbnailer.cpp` 要重新编译；如果 `libthumbnailer` 的接口不变，调用方甚至不用重新链接；
- `otool -L` / `readelf -d` 看，`build/thumb` 直接依赖的 FFmpeg 库是 0 个（运行时这些库还是会作为 `libthumbnailer` 的依赖被加载）。

FFmpeg 的 API 每隔几个大版本就会有不兼容的变化（比如 FFmpeg 5 删掉了旧的 `avcodec_decode_video2`，FFmpeg 7 删掉了旧的声道布局 API `AVCodecContext::channels` / `channel_layout`，改用 `ch_layout`），只有一个文件直接用 FFmpeg，升级时要改的地方就只有一处。

### 3.3 外观会不会变成上帝类

外观很容易越长越大。播放器项目里常见这样的演变：一开始 `Player` 只有 `open()`、`play()`、`pause()`、`seek()`；后来进度条要预览图，加了 `thumbnail()`；产品要录屏，加了 `startRecord()`、`stopRecord()`；字幕、音轨切换、统计信息、画质切换……一年后 `Player` 有 60 个公开函数、4000 行，什么都知道、什么都管，成了**上帝类**（God Class）：任何需求都要改它，任何人改它都可能影响别人。

几条判断和应对的办法：

- **一个外观对应一种使用场景，而不是一个子系统**。截图、播放、录制都用 FFmpeg，但它们是三种场景，应该是 `Thumbnailer`、`Player`、`Recorder` 三个外观。它们可以共享底层的子系统对象，但互相不知道。
- **外观只做协调，不做业务**。`Thumbnailer` 知道「先 seek、再解码到目标时间、再缩放」，但不该知道「VIP 用户的缩略图要加水印」。业务规则放在调用方或者别的类里，外观只负责把子系统组织起来。
- **外观可以分层**。高层外观调用低层外观，比如 `VideoCard`（生成分享卡片：缩略图 + 标题 + 二维码）使用 `Thumbnailer`，而不是把这些功能都加进 `Thumbnailer`。
- **看改动的理由**。如果一个外观因为三种不同的原因被修改（截图的需求变了、录制的需求变了、统计的需求变了），它就违反了《SOLID 原则（上）：单一职责与开闭》里的单一职责，该拆了。

## 4. 现代 C++ 写法

### 4.1 用 unique_ptr 管理 C 句柄

外观最常见的用途之一，就是把一个 C 库包成 C++ 接口。C 库的每种句柄都有自己的释放函数，用 `unique_ptr` 加一个删除器，就能让编译器替我们按顺序释放：

<!-- run: off -->
```cpp
struct FmtDel   { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct CodecDel { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
using FmtPtr   = std::unique_ptr<AVFormatContext, FmtDel>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDel>;

struct Thumbnailer::Impl {
    FmtPtr fmt;          // 先声明的后析构：解码器先释放，文件后关闭
    CodecPtr dec;
    // ...
};
```

和 `raw_thumb.cpp` 末尾的 `goto end` 相比：

| | `goto end` 统一释放 | `unique_ptr` + 删除器 |
|---|---|---|
| 变量声明 | 都要放在第一个 `goto` 之前 | 用到时再声明 |
| 释放顺序 | 手写，写反了编译器不管 | 按成员声明的相反顺序，自动 |
| 提前返回、抛异常 | 必须跳到 `end` | 怎么离开都会释放 |
| 额外开销 | 无 | 无（删除器是空的类，`sizeof(FmtPtr) == sizeof(void*)`） |

删除器写成空的结构体，而不是 `std::unique_ptr<T, void(*)(T*)>`（简化版子系统里为了少写几行用的就是这种）：函数指针要存在 `unique_ptr` 里，占两个指针的大小，调用时也是间接调用；空结构体不占空间，调用可以内联。

### 4.2 错误：错误码、异常、optional

FFmpeg 用负数的错误码报告错误，调用方每一步都要检查。外观是转换错误风格的好地方：

- `ffmpeg/thumbnailer.cpp` 的 `check()` 把错误码转成 `std::runtime_error`，带上是哪一步失败的（「打开文件：No such file or directory」）。调用方只在最外层 `catch` 一次。
- 简化版的 `Thumbnailer` 用 `std::optional<Frame>` 和 `bool`（本文能在浏览器里运行的代码不能用异常）。适合「失败很常见、调用方必须处理」的情况，比如要的时间超出了视频长度。
- C++23 的 `std::expected<Image, Error>` 可以同时带回结果和失败原因，介于两者之间。

不管选哪一种，**外观对外只用一种错误风格**，不要让 FFmpeg 的 `AVERROR(EAGAIN)` 漏到调用方那里去——那等于把子系统的细节又暴露了出来。

### 4.3 函数、对象，还是两者都给

外观不一定是一个类。只有一个操作、没有状态时，一个自由函数就是最简单的外观；需要在多次调用之间保留状态（打开的文件、解码器、缓存的 `SwsContext`）时，才需要对象。`thumbnailer.h` 两者都提供：

- `save_thumbnail(video, sec, png)`：内部就是 `Thumbnailer(video).save(sec, png)`；
- `Thumbnailer`：同一个视频取多张图时复用，实测快 15%～18%。

外观对象持有资源，**不应该能被复制**：复制以后两个对象指向同一组句柄，析构时会关闭两次（练习 4）。用 `unique_ptr` 持有句柄，外观自动就是只能移动、不能复制的。

## 5. 外观和相似的模式

| 模式 | 目的 | 接口从哪来 | 和子系统的关系 |
|---|---|---|---|
| **外观** | 让一组复杂的接口**更容易用** | 新设计一个更简单的接口 | 单向：外观调用子系统，子系统不知道外观；子系统仍然可以直接用 |
| 适配器 | 让一个已有的类**符合**另一个已有的接口 | 接口是现成的（调用方已经在用） | 通常只包一个类；被适配的类一般不再直接用 |
| 中介者 | 让一组对象之间**不直接互相调用** | 同事对象和中介者之间的接口 | 双向：同事对象知道中介者，通过它和别人通信 |
| 单例 | 保证一个类**只有一个实例** | 不关心 | 外观常常只需要一个，但没必要做成单例，按需创建、按参数传入更好测试 |
| 代理 | **控制对一个对象的访问** | 和被代理的对象完全相同 | 一对一，接口不变 |

最容易混的是外观和适配器。两者都是「在别人的代码前面包一层」，区别在于**接口是谁定的**：

- 《适配器模式》里，`MediaSource` 接口先存在，播放器已经在用它了，`FfmpegSource` 把 FFmpeg 改造成这个接口的样子。没有适配器，FFmpeg 就接不进来。
- 本文的 `Thumbnailer` 接口是新设计的，只为了好用。没有外观，调用方照样可以直接用 FFmpeg，只是麻烦。

外观和中介者的区别在方向上：外观里信息是单向的，子系统不知道外观；中介者里，各个同事对象都知道中介者，会主动通知它（第 4 章会讲）。

## 6. 小结

1. **外观给一个复杂的子系统提供一个简单的接口**：调用顺序、错误处理、资源释放都收进外观，调用方只关心「要什么」，不关心「怎么做」。实测调用方从 73 行、26 个 FFmpeg 函数变成 1 行、0 个函数。
2. **复杂度没有消失，而是被集中了**：外观自己有 121 行。好处是这些细节只写一次、测一次、改一处，不会像第 1.2 节那样每个调用方各错各的。
3. **外观几乎没有运行时开销**：一次性的 `save_thumbnail()` 和直接调 C API 相差不到 0.4%；按场景设计的接口（打开一次、取多张）反而快 15%～18%。
4. **外观不是封装**：子系统仍然公开，少见的需求可以绕过外观直接用子系统。
5. **外观的价值不在编译速度**：只 include 外观头文件的 `main.cpp` 反而编译得更慢；真正的收益是隔离，FFmpeg 只出现在一个文件里。
6. **按使用场景划分外观**，外观只做协调、不做业务，防止它长成上帝类。

## 7. 练习

1. 下面哪些是外观？如果不是，更像什么？
   - (a) `std::filesystem::copy_file(from, to)`
   - (b) 《适配器模式》里的 `FfmpegSource`
   - (c) libcurl 的 `curl_easy_perform()`
   - (d) 一个 `Player` 类，每个函数都只是原样调用 `Decoder` 的同名函数

2. 第 3.1 节实测，用同一个 `Thumbnailer` 取 10 张缩略图要解码 135 个包，因为每次 `grab()` 都要 seek。给它加一个批量接口 `grabMany(times)`：先把时间排序；如果 seek 会落到已经解码过的位置之前，就不 seek，接着往后解码。要解码多少个包？

3. 你们的播放器 SDK 里有一个 `Player` 外观，已经有 60 个公开函数：播放控制、进度条预览图、录制、字幕、音轨切换、统计上报、画质切换……几乎每个需求都要改它，经常改出冲突。你会怎么拆？

4. 下面这个外观有什么问题？`make_cards(th)` 返回后会发生什么？

   <!-- run: off -->
   ```cpp
   class Thumbnailer {
   public:
       explicit Thumbnailer(const std::string& path) : dm_(demux_open(path)) {
           if (dm_) dec_ = dec_open(dm_, demux_find_video(dm_));
       }
       ~Thumbnailer() { dec_close(dec_); demux_close(dm_); }
       bool ok() const { return dm_ && dec_; }
   private:
       Demuxer* dm_ = nullptr;
       Decoder* dec_ = nullptr;
   };

   void make_cards(Thumbnailer th) { /* ... */ }

   int main() {
       Thumbnailer th("bbb-10s.mp4");
       make_cards(th);
       // 继续用 th ……
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 
   - (a) **是外观**。一个函数背后是打开源文件和目标文件、查询属性、循环读写、关闭两个文件；标准库在不同平台上还会选用更快的系统调用。调用方也完全可以不用它，自己用 `std::ifstream` / `std::ofstream` 复制——外观不挡路。
   - (b) **是适配器**。`MediaSource` 接口是播放器先定好的，`FfmpegSource` 的任务是让 FFmpeg 符合这个接口。它也顺便把 FFmpeg 变简单了，但判断的标准是「接口是谁定的」：接口由调用方事先定好，就是适配器。
   - (c) **是外观**。easy 接口一个函数完成 DNS 解析、连接、TLS 握手、发送请求、接收响应；libcurl 同时公开了更底层的 multi 接口（一个线程里同时处理很多传输），easy 接口内部就是用 multi 接口实现的。
   - (d) **不是外观**。它只包了一个子系统，也没有把任何东西变简单，只是多了一层转发。如果它的目的是控制访问（比如检查权限、延迟创建 `Decoder`），那是代理；如果没有目的，就是多余的一层，应该删掉。

2. 135 → 123 个包。时间排序后，如果下一个时间点之前最近的关键帧不在已经解码到的位置之后，seek 只会让解码器退回去重解一遍，不如接着往后解。FFmpeg 里查关键帧位置可以用 `av_index_search_timestamp()`，简化版的子系统里加了一个 `demux_key_before()`：

   ```cpp
   // 《外观模式》：练习 2——给外观加一个批量接口 grabMany()，少解码一些包
   #include <algorithm>
   #include <cstdio>
   #include <memory>
   #include <optional>
   #include <string>
   #include <vector>

   // ---------- 子系统：仿照 FFmpeg 的 C 接口写的简化版 ----------
   int g_live = 0, g_opened = 0, g_decoded = 0;          // 没关闭的句柄、打开过几次文件、解码了几个包
   const int kKey[] = {0, 31, 79, 83, 131, 179, 227};    // bbb-10s.mp4 的关键帧（第几帧，24 帧/秒）
   struct Packet { int n = 0; double pts = 0; bool key = false; };
   struct Frame  { double pts = -1; int w = 1280, h = 720; };
   struct Demuxer { int next = 0; };
   struct Decoder { Packet in; bool has = false; };
   struct Scaler  { int w, h; };
   struct Encoder { int w, h; };

   Demuxer* demux_open(const std::string& path) { if (path.empty()) return nullptr; ++g_live; ++g_opened; return new Demuxer; }
   int  demux_find_video(Demuxer*) { return 0; }
   void demux_seek(Demuxer* d, double t) {               // 只能跳到 t 之前最近的关键帧
       for (int k : kKey) if (k / 24.0 <= t + 1e-9) d->next = k;
   }
   int  demux_key_before(Demuxer*, double t) {            // t 之前最近的关键帧是第几帧（查索引，不读文件）
       int key = 0;
       for (int k : kKey) if (k / 24.0 <= t + 1e-9) key = k;
       return key;
   }
   bool demux_read(Demuxer* d, Packet* p) {
       if (d->next >= 240) return false;
       p->n = d->next++; p->pts = p->n / 24.0; p->key = false;
       for (int k : kKey) if (k == p->n) p->key = true;
       return true;
   }
   void demux_close(Demuxer* d) { if (d) { --g_live; delete d; } }

   Decoder* dec_open(Demuxer*, int stream) { if (stream < 0) return nullptr; ++g_live; return new Decoder; }
   void dec_send(Decoder* c, const Packet& p) { c->in = p; c->has = true; ++g_decoded; }
   bool dec_receive(Decoder* c, Frame* f) { if (!c->has) return false; c->has = false; f->pts = c->in.pts; return true; }
   void dec_close(Decoder* c) { if (c) { --g_live; delete c; } }

   Scaler* scale_open(int, int, int w, int h) { if (w <= 0 || h <= 0) return nullptr; ++g_live; return new Scaler{w, h}; }
   void scale_run(Scaler* s, const Frame& in, Frame* out) { *out = in; out->w = s->w; out->h = s->h; }
   void scale_close(Scaler* s) { if (s) { --g_live; delete s; } }

   Encoder* png_open(int w, int h) { ++g_live; return new Encoder{w, h}; }
   bool png_write(Encoder*, const Frame& f, const std::string& path) {
       std::printf("  写出 %s：%dx%d，第 %.3f 秒的画面\n", path.c_str(), f.w, f.h, f.pts);
       return true;
   }
   void png_close(Encoder* e) { if (e) { --g_live; delete e; } }

   // ---------- 外观：把上面那串调用和释放顺序收进一个类 ----------
   class Thumbnailer {
   public:
       explicit Thumbnailer(const std::string& path) : dm_(demux_open(path), demux_close) {
           if (dm_) dec_.reset(dec_open(dm_.get(), demux_find_video(dm_.get())));
       }
       bool ok() const { return dm_ && dec_; }

       // 第 t 秒的画面：seek 只能落到前面的关键帧，所以要一直解码到 pts >= t
       std::optional<Frame> grab(double t) {
           if (!ok()) return std::nullopt;
           demux_seek(dm_.get(), t);
           Packet pkt; Frame f;
           while (demux_read(dm_.get(), &pkt)) {
               dec_send(dec_.get(), pkt);
               while (dec_receive(dec_.get(), &f)) if (f.pts >= t - 1e-9) return f;
           }
           return std::nullopt;
       }

       // 一次取多张：按时间排序；如果 seek 会落到已经解码过的位置之前，就不 seek，接着往后解码
       std::vector<Frame> grabMany(std::vector<double> ts) {
           std::vector<Frame> out;
           if (!ok()) return out;
           std::sort(ts.begin(), ts.end());
           int pos = -1;                                   // 已经解码到第几帧
           for (double t : ts) {
               if (demux_key_before(dm_.get(), t) > pos) demux_seek(dm_.get(), t);
               Packet pkt; Frame f; bool got = false;
               while (!got && demux_read(dm_.get(), &pkt)) {
                   dec_send(dec_.get(), pkt);
                   pos = pkt.n;
                   while (dec_receive(dec_.get(), &f)) if (f.pts >= t - 1e-9) { got = true; break; }
               }
               if (got) out.push_back(f);
           }
           return out;
       }

       // 第 t 秒的画面缩放到宽 width，存成 PNG
       bool save(double t, const std::string& out, int width = 320) {
           auto f = grab(t);
           if (!f || width <= 0) return false;
           Handle<Scaler> sc(scale_open(f->w, f->h, width, width * f->h / f->w), scale_close);
           if (!sc) return false;            // 提前返回也没关系：所有句柄都由析构函数关闭
           Frame small;
           scale_run(sc.get(), *f, &small);
           Handle<Encoder> enc(png_open(small.w, small.h), png_close);
           return png_write(enc.get(), small, out);
       }

   private:
       template <class T> using Handle = std::unique_ptr<T, void (*)(T*)>;
       Handle<Demuxer> dm_;
       Handle<Decoder> dec_{nullptr, dec_close};   // 后声明的先析构：先关解码器，再关文件
   };

   // 只要一张图时，一个函数就够了
   bool save_thumbnail(const std::string& in, double t, const std::string& out, int width = 320) {
       return Thumbnailer(in).save(t, out, width);
   }

   int main() {
       std::vector<double> ts = {9.5, 0.5, 5.5, 1.5, 6.5, 2.5, 7.5, 3.5, 8.5, 4.5};
       Thumbnailer th("bbb-10s.mp4");
       for (double t : ts) th.grab(t);
       std::printf("逐个 grab()：     解码 %d 个包\n", g_decoded);
       g_decoded = 0;
       auto frames = th.grabMany(ts);
       std::printf("一次 grabMany()：解码 %d 个包，取到 %zu 张：", g_decoded, frames.size());
       for (auto& f : frames) std::printf(" %.1f", f.pts);
       std::printf("\n");
   }
   ```

   ```
   逐个 grab()：     解码 135 个包
   一次 grabMany()：解码 123 个包，取到 10 张： 0.5 1.5 2.5 3.5 4.5 5.5 6.5 7.5 8.5 9.5
   ```

   省得不多（9%），因为 `bbb-10s.mp4` 的关键帧间隔只有 2 秒左右；关键帧间隔越大（直播录像常见 4～10 秒一个关键帧），时间点越密，省得越多。注意「接着往后解」也不总是更好：如果不查关键帧，一律接着往后解，从第 0.5 秒一直解到第 9.5 秒，要解码 229 个包，反而更多。这种「什么时候 seek」的取舍正应该藏在外观里，调用方只管说要哪几个时间。

3. 按**使用场景**拆，而不是按子系统拆：
   - `Player`：只留播放控制（打开、播放、暂停、seek、音量）和播放状态的回调；
   - `Thumbnailer`：进度条预览图，和播放互不影响（可以在另一个线程里打开同一个文件）；
   - `Recorder`：录制；
   - 字幕、音轨、画质切换是播放时才有意义的功能，可以作为 `Player` 的几个小接口（`player.subtitles()` 返回一个 `SubtitleControl&`），而不是 `Player` 上的 20 个函数；
   - 统计上报不属于任何一个外观：让 `Player` 发出事件（观察者模式，第 4 章），统计模块自己订阅。

   拆完以后检查：每个外观只因为一种需求变化而修改；外观里只有「怎么安排子系统」的代码，没有业务规则；几个外观共享的底层对象（比如网络缓存）作为参数传进去，而不是每个外观各建一份，也不做成单例。迁移时可以先保留旧的 `Player`，让它的 60 个函数转发给新的几个外观，调用方逐步改过去。

4. **复制外观会让同一组句柄被关闭两次**。`make_cards(th)` 按值传参，编译器生成的拷贝构造函数逐个复制两个裸指针，`th` 和参数指向同一个解封装器和解码器。`make_cards()` 返回时参数析构，把句柄关了；`main` 里的 `th` 还以为自己的句柄是好的，继续用就是使用已释放的内存，`main` 结束时再关一次就是重复释放。实测（`ex4_copy_bug.cpp`）：

   ```
   打开后，还没关闭的句柄：2
     生成卡片：可以
   make_cards() 返回后，还没关闭的句柄：0
   ```

   `th` 还没析构，句柄数已经是 0 了。接下来的重复释放：GCC/Ubuntu 上 glibc 检测到了，程序以 `free(): double free detected in tcache 2` 终止；macOS 上的 Clang 版本悄无声息地正常退出了；加上 `-fsanitize=address` 后两边都会报 `attempting double-free`。

   改法：句柄交给 `unique_ptr`，外观自动变成只能移动、不能复制的，按值传参直接编译失败（`ex4_copy_ce.cpp`：`call to implicitly-deleted copy constructor of 'Thumbnailer'`），只能改成按引用传：

   ```cpp
   // 《外观模式》：练习 4 的答案——用 unique_ptr 保存句柄，复制外观会编译失败，只能按引用传或者移动
   #include <cstdio>
   #include <memory>
   #include <string>

   // ---------- 子系统：仿照 FFmpeg 的 C 接口写的简化版 ----------
   int g_live = 0, g_opened = 0, g_decoded = 0;          // 没关闭的句柄、打开过几次文件、解码了几个包
   const int kKey[] = {0, 31, 79, 83, 131, 179, 227};    // bbb-10s.mp4 的关键帧（第几帧，24 帧/秒）
   struct Packet { int n = 0; double pts = 0; bool key = false; };
   struct Frame  { double pts = -1; int w = 1280, h = 720; };
   struct Demuxer { int next = 0; };
   struct Decoder { Packet in; bool has = false; };
   struct Scaler  { int w, h; };
   struct Encoder { int w, h; };

   Demuxer* demux_open(const std::string& path) { if (path.empty()) return nullptr; ++g_live; ++g_opened; return new Demuxer; }
   int  demux_find_video(Demuxer*) { return 0; }
   void demux_seek(Demuxer* d, double t) {               // 只能跳到 t 之前最近的关键帧
       for (int k : kKey) if (k / 24.0 <= t + 1e-9) d->next = k;
   }
   bool demux_read(Demuxer* d, Packet* p) {
       if (d->next >= 240) return false;
       p->n = d->next++; p->pts = p->n / 24.0; p->key = false;
       for (int k : kKey) if (k == p->n) p->key = true;
       return true;
   }
   void demux_close(Demuxer* d) { if (d) { --g_live; delete d; } }

   Decoder* dec_open(Demuxer*, int stream) { if (stream < 0) return nullptr; ++g_live; return new Decoder; }
   void dec_send(Decoder* c, const Packet& p) { c->in = p; c->has = true; ++g_decoded; }
   bool dec_receive(Decoder* c, Frame* f) { if (!c->has) return false; c->has = false; f->pts = c->in.pts; return true; }
   void dec_close(Decoder* c) { if (c) { --g_live; delete c; } }

   Scaler* scale_open(int, int, int w, int h) { if (w <= 0 || h <= 0) return nullptr; ++g_live; return new Scaler{w, h}; }
   void scale_run(Scaler* s, const Frame& in, Frame* out) { *out = in; out->w = s->w; out->h = s->h; }
   void scale_close(Scaler* s) { if (s) { --g_live; delete s; } }

   Encoder* png_open(int w, int h) { ++g_live; return new Encoder{w, h}; }
   bool png_write(Encoder*, const Frame& f, const std::string& path) {
       std::printf("  写出 %s：%dx%d，第 %.3f 秒的画面\n", path.c_str(), f.w, f.h, f.pts);
       return true;
   }
   void png_close(Encoder* e) { if (e) { --g_live; delete e; } }

   // ---------- 练习 4 的答案：句柄交给 unique_ptr，外观自动变成只能移动、不能复制 ----------
   class Thumbnailer {
   public:
       explicit Thumbnailer(const std::string& path) : dm_(demux_open(path), demux_close) {
           if (dm_) dec_.reset(dec_open(dm_.get(), demux_find_video(dm_.get())));
       }
       bool ok() const { return dm_ && dec_; }
   private:
       std::unique_ptr<Demuxer, void (*)(Demuxer*)> dm_;
       std::unique_ptr<Decoder, void (*)(Decoder*)> dec_{nullptr, dec_close};
   };

   void make_cards(Thumbnailer& th) {         // 按引用传参；写成按值传参会编译失败
       std::printf("  生成卡片：%s\n", th.ok() ? "可以" : "不行");
   }

   int main() {
       Thumbnailer th("bbb-10s.mp4");
       std::printf("打开后，还没关闭的句柄：%d\n", g_live);
       make_cards(th);
       std::printf("make_cards() 返回后，还没关闭的句柄：%d\n", g_live);
   }
   ```

   ```
   打开后，还没关闭的句柄：2
     生成卡片：可以
   make_cards() 返回后，还没关闭的句柄：2
   ```

   如果坚持用裸指针，就要遵守三/五法则：自己写（或者 `= delete`）拷贝构造、拷贝赋值，再写移动构造、移动赋值。比起手写这四个函数，用 `unique_ptr` 让编译器替你做对更省事。

</details>
