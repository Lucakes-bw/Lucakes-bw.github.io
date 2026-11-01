---
title: 硬件解码
date: 2026-11-02 14:00:00
updated: 2026-11-02 14:00:00
categories:
  - 音视频开发
  - 播放器
tags:
  - 音视频
  - FFmpeg
  - 硬件解码
  - VideoToolbox
  - C
order: 6
description: 用 FFmpeg 的 hwcontext 接口调用 macOS 的 VideoToolbox：创建硬件设备，在 get_format 回调中选择硬件像素格式，解码器输出的帧就留在 GPU 那边。解码 4K H.264，开满 12 个线程的软件解码反而更快（607 帧/秒对 197 帧/秒），但要用 3.18 秒 CPU，硬件解码只用 0.08 秒；把画面复制回内存又要多花 0.36 秒。接进播放器后，播放 10 秒 4K HEVC：软件解码用了 4.0 秒 CPU，硬件解码加复制 2.1 秒，用 SDL3 直接拿解码器的 CVPixelBuffer 创建 Metal 纹理、画面完全不经过内存，只用 0.64 秒。三种方式渲染出的画面完全相同。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解硬件解码是什么，它的优点和限制；会用 FFmpeg 的 hwcontext 接口开启 VideoToolbox 硬件解码；理解硬件帧和软件帧的区别，会把硬件帧复制到内存；理解零拷贝渲染；会根据实测选择解码方式。
>
> **前置知识**：本章前几篇；第 3 章《编码》中的 VideoToolbox 硬件编码；第 1 章《像素与 YUV》中的 NV12。
>
> **实验环境**：macOS 27.0（Apple M6，12 核），Apple Clang 21，FFmpeg 9.0.2，SDL 3.4。

## 1. 硬件解码

第 3 章第 3 篇用过 VideoToolbox 的硬件**编码**器：编码工作在芯片中专门的电路里完成，几乎不占 CPU。解码也一样，现在的 CPU、GPU 中几乎都有视频解码电路，支持 H.264、HEVC，新一些的还支持 AV1。

和硬件编码不同的是，硬件解码出的画面和软件解码**完全相同**：第 3 章第 1 篇练习 2 说过，解码的结果由标准唯一规定，所以硬件解码没有画质的取舍，只有速度、功耗和兼容性的取舍。

各平台的硬件解码接口：

| 平台 | 接口 | FFmpeg 中的设备类型 |
|---|---|---|
| macOS、iOS | VideoToolbox | `AV_HWDEVICE_TYPE_VIDEOTOOLBOX` |
| Windows | D3D11VA、DXVA2 | `AV_HWDEVICE_TYPE_D3D11VA`、`DXVA2` |
| Linux | VAAPI（Intel、AMD）、VDPAU | `AV_HWDEVICE_TYPE_VAAPI` |
| NVIDIA 显卡 | NVDEC / CUDA | `AV_HWDEVICE_TYPE_CUDA` |
| Android | MediaCodec | `AV_HWDEVICE_TYPE_MEDIACODEC` |

FFmpeg 把它们统一成 **hwcontext** 接口：解码器还是原来的 `h264`、`hevc` 解码器，只是加上一个硬件设备，由它把繁重的部分交给硬件。所以代码在各平台上几乎一样，只是设备类型不同。

## 2. 程序

先写一个只解码、不显示的程序，比较软件和硬件解码：

```c
// hw_decode.c：用 VideoToolbox 硬件解码，和软件解码比较速度与 CPU 占用
#include <stdio.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>

static enum AVPixelFormat hw_pix_fmt;

// 解码器初始化时会调用这个函数，让我们从它支持的输出格式中挑一个：选硬件格式
static enum AVPixelFormat get_format(AVCodecContext *ctx, const enum AVPixelFormat *fmts)
{
    (void)ctx;
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++)
        if (*p == hw_pix_fmt) return *p;
    fprintf(stderr, "解码器不支持硬件格式，退回软件解码\n");
    return fmts[0];
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "用法: %s 文件 sw|hw|hw-copy [线程数]\n", argv[0]); return 1; }
    int hw = strncmp(argv[2], "hw", 2) == 0, copy = strcmp(argv[2], "hw-copy") == 0;
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    const AVCodec *codec = NULL;
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[vs]->codecpar);
    dec->pkt_timebase = fmt->streams[vs]->time_base;
    if (argc > 3) dec->thread_count = atoi(argv[3]);

    AVBufferRef *hw_dev = NULL;
    if (hw) {
        // 1. 创建 VideoToolbox 设备（Windows 上换成 AV_HWDEVICE_TYPE_D3D11VA，Linux 上是 VAAPI、CUDA 等）
        if (av_hwdevice_ctx_create(&hw_dev, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0) < 0) {
            fprintf(stderr, "创建硬件设备失败\n");
            return 1;
        }
        // 2. 找到这个解码器在 VideoToolbox 上输出的像素格式
        for (int i = 0;; i++) {
            const AVCodecHWConfig *cfg = avcodec_get_hw_config(codec, i);
            if (!cfg) { fprintf(stderr, "%s 不支持 VideoToolbox\n", codec->name); return 1; }
            if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && cfg->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX) {
                hw_pix_fmt = cfg->pix_fmt;
                break;
            }
        }
        // 3. 告诉解码器用这个设备，并在协商格式时选硬件格式
        dec->hw_device_ctx = av_buffer_ref(hw_dev);
        dec->get_format = get_format;
    }
    avcodec_open2(dec, codec, NULL);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc(), *sw = av_frame_alloc();
    long frames = 0;
    int eof = 0, printed = 0;
    int64_t t0 = av_gettime_relative();
    while (1) {
        int ret = avcodec_receive_frame(dec, frame);
        if (ret == AVERROR_EOF) break;
        if (ret == AVERROR(EAGAIN)) {
            if (eof) break;
            if (av_read_frame(fmt, pkt) < 0) { avcodec_send_packet(dec, NULL); eof = 1; continue; }
            if (pkt->stream_index == vs) avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            continue;
        }
        if (!printed) {
            printf("%s，%dx%d，解码输出的格式：%s", codec->name, frame->width, frame->height, av_get_pix_fmt_name(frame->format));
            printed = 1;
        }
        // 硬件帧的数据在显存（或硬件的缓冲区）中，CPU 要用就得复制出来
        if (copy && frame->format == hw_pix_fmt) {
            av_hwframe_transfer_data(sw, frame, 0);
            if (printed == 1) { printf("，复制出来是 %s", av_get_pix_fmt_name(sw->format)); printed = 2; }
            av_frame_unref(sw);
        }
        frames++;
        av_frame_unref(frame);
    }
    double sec = (av_gettime_relative() - t0) / 1e6;
    printf("\n%ld 帧，%.2f 秒，%.0f 帧/秒\n", frames, sec, frames / sec);
    av_buffer_unref(&hw_dev);
    return 0;
}
```

开启硬件解码只要三步：

1. **`av_hwdevice_ctx_create`** 创建一个硬件设备（VideoToolbox）。
2. **`avcodec_get_hw_config`** 查出这个解码器在该设备上输出什么像素格式，VideoToolbox 是 `AV_PIX_FMT_VIDEOTOOLBOX`。
3. 把设备交给解码器（`hw_device_ctx`），并设置 **`get_format`** 回调。解码器初始化时（读到 SPS 时）会把它能输出的格式列给回调，回调从中选出硬件格式。如果硬件不支持这个视频（比如档次太高、分辨率太大），列表里就没有硬件格式，回调返回第一个，也就是退回软件解码。

解码出的帧，`format` 是 `AV_PIX_FMT_VIDEOTOOLBOX`。这种**硬件帧**的数据不在普通的内存中：`data[3]` 是一个 `CVPixelBufferRef`（Apple 的图像缓冲区对象），CPU 不能直接按 `data[0]`、`linesize[0]` 读取像素。要在 CPU 上使用，就用 **`av_hwframe_transfer_data`** 复制成普通的帧，得到的格式是 **NV12**：第 1 章讲过，NV12 是硬件编解码最常用的格式，Y 一个平面，U、V 交错存放在另一个平面。

## 3. 解码速度和 CPU 占用

除了测试视频，再准备两段 4K 的版本（把测试视频放大到 3840×2160 后，分别编码成 H.264 和 HEVC）：

```bash
ffmpeg -i bbb-10s.mp4 -vf scale=3840:2160:flags=lanczos -c:v libx264 -preset veryfast -crf 20 -g 48 -c:a copy bbb4k.mp4
ffmpeg -i bbb-10s.mp4 -vf scale=3840:2160:flags=lanczos -c:v hevc_videotoolbox -b:v 20M -tag:v hvc1 -g 48 -c:a copy bbb4k_hevc.mp4
```

用 `/usr/bin/time` 测量每种方式的实际耗时和 CPU 时间（用户态）：

```bash
/usr/bin/time -p ./hw_decode bbb4k.mp4 sw 0     # 软件解码，自动线程数
/usr/bin/time -p ./hw_decode bbb4k.mp4 hw       # 硬件解码，帧留在硬件中
/usr/bin/time -p ./hw_decode bbb4k.mp4 hw-copy  # 硬件解码，再复制到内存
```

```
h264，3840x2160，解码输出的格式：videotoolbox_vld，复制出来是 nv12
240 帧，1.56 秒，154 帧/秒
```

| 视频 | 方式 | 速度 | CPU 时间 |
|---|---|---|---|
| 720p H.264 | 软件，1 线程 | 815 帧/秒 | 0.33 秒 |
| | 软件，自动（13 线程） | 3333 帧/秒 | 0.43 秒 |
| | 硬件 | 929 帧/秒 | **0.02 秒** |
| | 硬件 + 复制 | 918 帧/秒 | 0.03 秒 |
| 4K H.264 | 软件，1 线程 | 115 帧/秒 | 2.09 秒 |
| | 软件，自动 | **607 帧/秒** | 3.18 秒 |
| | 硬件 | 197 帧/秒 | **0.08 秒** |
| | 硬件 + 复制 | 154 帧/秒 | 0.44 秒 |
| 4K HEVC | 软件，1 线程 | 77 帧/秒 | 3.12 秒 |
| | 软件，自动 | 364 帧/秒 | 4.25 秒 |
| | 硬件 | 231 帧/秒 | **0.07 秒** |
| | 硬件 + 复制 | 179 帧/秒 | 0.38 秒 |

几个可能出乎意料的结果：

- **硬件解码不一定更快**。4K H.264，开满 12 个核心的软件解码每秒 607 帧，硬件只有 197 帧。M6 的 CPU 很强，而解码电路的吞吐量是固定的。
- **但硬件解码几乎不占 CPU**：同样解码 240 帧 4K，软件要 3.18 秒 CPU 时间（12 个核心一起忙了 0.4 秒），硬件只要 0.08 秒。对笔记本来说，这意味着风扇不转、电池耐用；对播放器来说，CPU 可以留给别的事情（比如字幕渲染、滤镜、界面）。
- **HEVC 的软件解码更慢**，比 H.264 慢了 40%～50%：HEVC 压缩率更高，计算也更复杂。这正是硬件解码最能体现价值的地方。
- **复制回内存是有代价的**：4K 的一帧 NV12 有 12 MB，复制 240 帧要多花 0.3 秒左右的 CPU，速度也降到每秒 154 帧。

播放只需要每秒 24 帧，197 帧/秒的硬件解码绰绰有余。**播放时比较的不是谁更快，而是谁更省**。

## 4. 接进播放器

把硬件解码接进第 4 篇的同步播放器（完整的代码在下面的折叠区）：

- 打开视频解码器时，如果设置了环境变量 `HW`，就创建 VideoToolbox 设备并设置 `get_format`。
- 视频解码线程拿到硬件帧后，用 `av_hwframe_transfer_data` 复制成 NV12 的普通帧，再放进帧队列。
- 主线程根据第一帧的像素格式创建纹理：软件解码是 `SDL_PIXELFORMAT_IYUV`，硬件解码是 `SDL_PIXELFORMAT_NV12`，上传时用 `SDL_UpdateNVTexture`（两个平面）。
- **零拷贝**（环境变量 `ZEROCOPY`）：视频解码线程不复制，硬件帧直接放进帧队列；主线程用 SDL3 的 `SDL_PROP_TEXTURE_CREATE_METAL_PIXELBUFFER_POINTER` 属性，拿解码器的 `CVPixelBufferRef`（`f->data[3]`）直接创建 Metal 纹理。

<details>
<summary>展开完整的 player6.c</summary>

```c
// player6.c：在 player4 的基础上加入 VideoToolbox 硬件解码（环境变量 HW=1）
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/hwcontext.h>

// ---------------- 包队列：多个线程共享，用互斥锁和条件变量保护 ----------------
typedef struct PktNode { AVPacket *pkt; struct PktNode *next; } PktNode;
typedef struct {
    PktNode *first, *last;
    int count;
    double duration;                 // 队列中所有包的总时长（秒）
    int eof;                         // 读文件线程已经读完了
    SDL_Mutex *mutex;
    SDL_Condition *cond;
} PacketQueue;

static void pq_init(PacketQueue *q) { SDL_zerop(q); q->mutex = SDL_CreateMutex(); q->cond = SDL_CreateCondition(); }

static void pq_put(PacketQueue *q, AVPacket *pkt, double dur)
{
    PktNode *n = av_malloc(sizeof *n);
    n->pkt = av_packet_alloc();
    av_packet_move_ref(n->pkt, pkt);             // 接管数据，不复制
    n->next = NULL;
    SDL_LockMutex(q->mutex);
    if (q->last) q->last->next = n; else q->first = n;
    q->last = n;
    q->count++;
    q->duration += dur;
    SDL_SignalCondition(q->cond);                // 叫醒可能在等数据的解码线程
    SDL_UnlockMutex(q->mutex);
}

// 取出一个包；队列空时等待。返回 1 表示取到，0 表示已经读完
static int pq_get(PacketQueue *q, AVPacket *pkt, double dur_of(const AVPacket *))
{
    SDL_LockMutex(q->mutex);
    while (!q->first && !q->eof) SDL_WaitCondition(q->cond, q->mutex);
    int ret = 0;
    if (q->first) {
        PktNode *n = q->first;
        q->first = n->next;
        if (!q->first) q->last = NULL;
        q->count--;
        q->duration -= dur_of(n->pkt);
        av_packet_move_ref(pkt, n->pkt);
        av_packet_free(&n->pkt);
        av_free(n);
        ret = 1;
    }
    SDL_SignalCondition(q->cond);                // 叫醒可能因为队列太满而在等的读文件线程
    SDL_UnlockMutex(q->mutex);
    return ret;
}

// ---------------- 帧队列：视频解码线程放入，主线程取出显示 ----------------
#define FQ_SIZE 3
typedef struct {
    AVFrame *frames[FQ_SIZE];
    int rindex, windex, count, eof;
    SDL_Mutex *mutex;
    SDL_Condition *cond;
} FrameQueue;

// ---------------- 全局状态 ----------------
static AVFormatContext *fmt;
static int vs, as;
static AVCodecContext *vdec, *adec;
static PacketQueue videoq, audioq;
static FrameQueue frameq;
static SDL_AudioStream *astream;
static double max_buffer = 1.0;                  // 包队列最多缓冲多少秒
static int stall_ms = 0;                         // 模拟网络：每秒卡顿多少毫秒
static Uint64 t_start;
static SDL_AtomicInt underruns;
static FILE *qlog;

// ---------------- 音频时钟 ----------------
static SDL_Mutex *clock_mutex;
static double audio_end_pts = -1;                // 最后推给 SDL 的那个采样之后的时间（秒）
static double device_latency;                    // 设备缓冲的延迟：设备每次处理的采样数 / 采样率

// 声卡此刻正在播放的是第几秒的声音：
// 推进去的数据的末尾，减去还在 SDL 队列里没被取走的，再减去设备缓冲里正在播的
static double audio_clock(void)
{
    SDL_LockMutex(clock_mutex);
    double end = audio_end_pts;
    SDL_UnlockMutex(clock_mutex);
    if (end < 0) return -1;
    double queued = SDL_GetAudioStreamQueued(astream) / (48000.0 * 4);
    return end - queued - device_latency;
}

static double vdur(const AVPacket *p) { return p->duration * av_q2d(fmt->streams[vs]->time_base); }
static double adur(const AVPacket *p) { return p->duration * av_q2d(fmt->streams[as]->time_base); }
static double now_s(void) { return (SDL_GetTicksNS() - t_start) / 1e9; }

// 读文件线程：读出包，分到两个队列；队列太满就等
static int SDLCALL demux_thread(void *arg)
{
    (void)arg;
    AVPacket *pkt = av_packet_alloc();
    double next_stall = 1.0;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (stall_ms && now_s() >= next_stall) {    // 模拟网络卡顿：每过 1 秒，卡住 stall_ms 毫秒
            SDL_Delay(stall_ms);
            next_stall += 1.0;
        }
        PacketQueue *q = pkt->stream_index == vs ? &videoq : pkt->stream_index == as ? &audioq : NULL;
        if (!q) { av_packet_unref(pkt); continue; }
        // 视频队列的总时长超过 max_buffer 就等解码线程取走一些
        SDL_LockMutex(videoq.mutex);
        while (videoq.duration > max_buffer) SDL_WaitCondition(videoq.cond, videoq.mutex);
        SDL_UnlockMutex(videoq.mutex);
        pq_put(q, pkt, q == &videoq ? vdur(pkt) : adur(pkt));
    }
    for (PacketQueue *q = &videoq; q; q = q == &videoq ? &audioq : NULL) {
        SDL_LockMutex(q->mutex); q->eof = 1; SDL_BroadcastCondition(q->cond); SDL_UnlockMutex(q->mutex);
    }
    av_packet_free(&pkt);
    return 0;
}

// 视频解码线程：包 → 帧，放进帧队列；帧队列满了就等
static int SDLCALL video_thread(void *arg)
{
    (void)arg;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int eof = 0;
    while (1) {
        int ret = avcodec_receive_frame(vdec, frame);
        if (ret == AVERROR_EOF) break;
        if (ret == AVERROR(EAGAIN)) {
            if (eof) break;
            if (pq_get(&videoq, pkt, vdur)) { avcodec_send_packet(vdec, pkt); av_packet_unref(pkt); }
            else { avcodec_send_packet(vdec, NULL); eof = 1; }
            continue;
        }
        if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX && !getenv("ZEROCOPY")) {   // 硬件帧：复制到内存中，得到 NV12
            AVFrame *sw = av_frame_alloc();
            av_hwframe_transfer_data(sw, frame, 0);
            sw->pts = frame->pts;
            av_frame_unref(frame);
            av_frame_move_ref(frame, sw);
            av_frame_free(&sw);
        }
        SDL_LockMutex(frameq.mutex);
        while (frameq.count == FQ_SIZE) SDL_WaitCondition(frameq.cond, frameq.mutex);
        av_frame_move_ref(frameq.frames[frameq.windex], frame);
        frameq.windex = (frameq.windex + 1) % FQ_SIZE;
        frameq.count++;
        SDL_SignalCondition(frameq.cond);
        SDL_UnlockMutex(frameq.mutex);
    }
    SDL_LockMutex(frameq.mutex); frameq.eof = 1; SDL_SignalCondition(frameq.cond); SDL_UnlockMutex(frameq.mutex);
    av_frame_free(&frame); av_packet_free(&pkt);
    return 0;
}

// 音频解码线程：包 → PCM，推进 SDL 的音频流（推模式，队列最多 0.2 秒）
static int SDLCALL audio_thread(void *arg)
{
    (void)arg;
    SwrContext *swr = NULL;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, 48000, &adec->ch_layout, adec->sample_fmt, adec->sample_rate, 0, NULL);
    swr_init(swr);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    uint8_t out[8192 * 4];
    int started = 0;
    while (pq_get(&audioq, pkt, adur)) {
        avcodec_send_packet(adec, pkt);
        av_packet_unref(pkt);
        while (avcodec_receive_frame(adec, frame) >= 0) {
            while (SDL_GetAudioStreamQueued(astream) > 48000 * 4 / 5) SDL_Delay(2);
            if (started && SDL_GetAudioStreamQueued(astream) == 0) SDL_AddAtomicInt(&underruns, 1);   // 设备把数据放完了
            uint8_t *dst[1] = {out};
            int n = swr_convert(swr, dst, 8192, (const uint8_t **)frame->extended_data, frame->nb_samples);
            SDL_PutAudioStreamData(astream, out, n * 4);
            SDL_LockMutex(clock_mutex);
            audio_end_pts = frame->pts * av_q2d(fmt->streams[as]->time_base) + n / 48000.0;
            SDL_UnlockMutex(clock_mutex);
            if (!started) { SDL_ResumeAudioStreamDevice(astream); started = 1; }
            av_frame_unref(frame);
        }
    }
    av_frame_free(&frame); av_packet_free(&pkt); swr_free(&swr);
    return 0;
}

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *fmts)
{
    (void)ctx;
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++)
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX) return *p;
    return fmts[0];                                  // 不支持就退回软件解码
}

static AVCodecContext *open_decoder(int idx)
{
    const AVCodec *c = avcodec_find_decoder(fmt->streams[idx]->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar);
    ctx->pkt_timebase = fmt->streams[idx]->time_base;
    if (idx == vs && getenv("HW")) {                 // 视频用硬件解码
        av_hwdevice_ctx_create(&ctx->hw_device_ctx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0);
        ctx->get_format = get_hw_format;
    }
    avcodec_open2(ctx, c, NULL);
    return ctx;
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 文件 [audio|wall]\n", argv[0]); return 1; }
    int sync_audio = !(argc > 2 && SDL_strcmp(argv[2], "wall") == 0);
    int allow_drop = getenv("NODROP") == NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    vdec = open_decoder(vs);
    adec = open_decoder(as);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_CreateWindowAndRenderer("player6", vdec->width, vdec->height,
                                getenv("HIDDEN") ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE, &win, &ren);
    printf("渲染器 %s，%s解码\n", SDL_GetRendererName(ren), getenv("HW") ? "硬件" : "软件");
    SDL_Texture *tex = NULL;                         // 等第一帧出来、知道像素格式之后再创建
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, 48000};
    astream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (getenv("MUTE")) SDL_SetAudioStreamGain(astream, 0.0f);
    SDL_AudioSpec dev;
    int dev_frames;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(astream), &dev, &dev_frames);
    device_latency = (double)dev_frames / dev.freq;
    clock_mutex = SDL_CreateMutex();
    printf("音频驱动 %s，设备缓冲 %.1f 毫秒；视频跟随%s\n", SDL_GetCurrentAudioDriver(), device_latency * 1000,
           sync_audio ? "音频时钟" : "系统时钟");

    pq_init(&videoq);
    pq_init(&audioq);
    for (int i = 0; i < FQ_SIZE; i++) frameq.frames[i] = av_frame_alloc();
    frameq.mutex = SDL_CreateMutex();
    frameq.cond = SDL_CreateCondition();
    qlog = fopen("sync.log", "w");

    t_start = SDL_GetTicksNS();
    SDL_Thread *td = SDL_CreateThread(demux_thread, "demux", NULL);
    SDL_Thread *tv = SDL_CreateThread(video_thread, "video", NULL);
    SDL_Thread *ta = SDL_CreateThread(audio_thread, "audio", NULL);

    // 主线程：处理事件；按主时钟决定每一帧是等待、显示还是丢掉
    int frames = 0, dropped = 0, quit = 0;
    double max_err = 0, frame_dur = av_q2d(av_inv_q(fmt->streams[vs]->avg_frame_rate));
    double wall_base = -1;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) quit = 1;
        double ac = audio_clock();
        if (ac < 0) { SDL_Delay(1); continue; }               // 声音还没开始，画面也先等着
        if (wall_base < 0) wall_base = now_s() - ac;          // 系统时钟从声音开始的那一刻起算
        double master = sync_audio ? ac : now_s() - wall_base;

        SDL_LockMutex(frameq.mutex);
        if (frameq.count == 0) {
            int done = frameq.eof;
            SDL_UnlockMutex(frameq.mutex);
            if (done) break;
            SDL_Delay(1);
            continue;
        }
        AVFrame *f = frameq.frames[frameq.rindex];
        int has_next = frameq.count > 1;
        SDL_UnlockMutex(frameq.mutex);

        double pts = f->pts * av_q2d(fmt->streams[vs]->time_base);
        double diff = pts - master;
        if (diff > 0.001) { SDL_Delay(1); continue; }         // 还没到时间
        if (diff < -frame_dur && has_next && allow_drop) {    // 已经晚了一帧以上，后面还有帧：丢掉这一帧追上去
            dropped++;
        } else {
            if (f->format == AV_PIX_FMT_VIDEOTOOLBOX) {      // 零拷贝：直接用解码器的 CVPixelBuffer 创建纹理
                SDL_PropertiesID props = SDL_CreateProperties();
                SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_METAL_PIXELBUFFER_POINTER, f->data[3]);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_NV12);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, f->width);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, f->height);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_LIMITED);
                SDL_Texture *t = SDL_CreateTextureWithProperties(ren, props);
                SDL_DestroyProperties(props);
                SDL_RenderClear(ren);
                SDL_RenderTexture(ren, t, NULL, NULL);
                SDL_RenderPresent(ren);
                SDL_DestroyTexture(t);
                goto shown;
            }
            if (!tex) {
                SDL_PropertiesID props = SDL_CreateProperties();
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                                      f->format == AV_PIX_FMT_NV12 ? SDL_PIXELFORMAT_NV12 : SDL_PIXELFORMAT_IYUV);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, f->width);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, f->height);
                SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_LIMITED);
                tex = SDL_CreateTextureWithProperties(ren, props);
                SDL_DestroyProperties(props);
                printf("纹理格式 %s\n", f->format == AV_PIX_FMT_NV12 ? "NV12" : "IYUV");
            }
            if (f->format == AV_PIX_FMT_NV12)            // NV12：Y 平面 + 交错的 UV 平面
                SDL_UpdateNVTexture(tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1]);
            else
                SDL_UpdateYUVTexture(tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
            SDL_RenderClear(ren);
            SDL_RenderTexture(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
        shown:
            frames++;
            double err = pts - ac;                              // 画面和此刻正在播放的声音差多少（正：画面超前）
            if (fabs(err) > fabs(max_err)) max_err = err;
            fprintf(qlog, "%.3f %.4f\n", now_s(), err);
        }
        SDL_LockMutex(frameq.mutex);
        av_frame_unref(f);
        frameq.rindex = (frameq.rindex + 1) % FQ_SIZE;
        frameq.count--;
        SDL_SignalCondition(frameq.cond);
        SDL_UnlockMutex(frameq.mutex);
    }
    printf("显示 %d 帧，丢掉 %d 帧；画面与声音的偏差最大 %+.0f 毫秒；用时 %.2f 秒\n",
           frames, dropped, max_err * 1000, now_s());

    SDL_WaitThread(td, NULL); SDL_WaitThread(tv, NULL); SDL_WaitThread(ta, NULL);
    fclose(qlog);
    SDL_Quit();
    return 0;
}
```

</details>

三种方式的数据路径：

<figure class="diagram">
<svg viewBox="0 0 680 230" role="img" aria-label="三种方式的数据路径：软件解码时，CPU 解码到内存，再上传到 GPU 的纹理；硬件解码加复制时，解码电路输出到硬件缓冲区，复制到内存，再上传回 GPU；零拷贝时，解码电路输出的缓冲区直接作为纹理交给 GPU 渲染，画面不经过 CPU">
  <defs>
    <marker id="hw-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <text x="5" y="35" style="fill:var(--fg)">软件解码</text>
    <g style="stroke:var(--border-2)">
      <rect x="110" y="16" width="120" height="30" rx="5" style="fill:var(--danger);opacity:.25"/>
      <rect x="300" y="16" width="120" height="30" rx="5" style="fill:var(--panel-2)"/>
      <rect x="490" y="16" width="120" height="30" rx="5" style="fill:var(--accent-soft)"/>
    </g>
    <text x="120" y="36" style="fill:var(--fg)">CPU 解码</text><text x="310" y="36" style="fill:var(--fg)">内存 YUV</text><text x="500" y="36" style="fill:var(--fg)">GPU 纹理</text>
    <text x="5" y="105" style="fill:var(--fg)">硬件 + 复制</text>
    <g style="stroke:var(--border-2)">
      <rect x="110" y="86" width="120" height="30" rx="5" style="fill:var(--accent-soft)"/>
      <rect x="300" y="86" width="120" height="30" rx="5" style="fill:var(--panel-2)"/>
      <rect x="490" y="86" width="120" height="30" rx="5" style="fill:var(--accent-soft)"/>
    </g>
    <text x="120" y="106" style="fill:var(--fg)">解码电路</text><text x="310" y="106" style="fill:var(--fg)">内存 NV12</text><text x="500" y="106" style="fill:var(--fg)">GPU 纹理</text>
    <text x="5" y="175" style="fill:var(--fg)">零拷贝</text>
    <g style="stroke:var(--border-2)">
      <rect x="110" y="156" width="120" height="30" rx="5" style="fill:var(--accent-soft)"/>
      <rect x="490" y="156" width="120" height="30" rx="5" style="fill:var(--accent-soft)"/>
    </g>
    <text x="120" y="176" style="fill:var(--fg)">解码电路</text><text x="500" y="176" style="fill:var(--fg)">GPU 纹理</text>
    <g style="stroke:var(--muted)" marker-end="url(#hw-arrow)">
      <line x1="230" y1="31" x2="298" y2="31"/><line x1="420" y1="31" x2="488" y2="31"/>
      <line x1="230" y1="101" x2="298" y2="101"/><line x1="420" y1="101" x2="488" y2="101"/>
      <line x1="230" y1="171" x2="488" y2="171"/>
    </g>
    <text x="236" y="62" style="fill:var(--muted);font-size:10px">CPU 计算</text><text x="426" y="62" style="fill:var(--muted);font-size:10px">上传 12 MB/帧</text>
    <text x="236" y="132" style="fill:var(--muted);font-size:10px">复制 12 MB/帧</text><text x="426" y="132" style="fill:var(--muted);font-size:10px">再上传回去</text>
    <text x="270" y="200" style="fill:var(--muted);font-size:10px">同一块 CVPixelBuffer，直接作为纹理</text>
    <text x="5" y="222" style="fill:var(--muted);font-size:11px">（Apple 芯片的 CPU 和 GPU 共用内存，「上传」「复制」仍然是实实在在的内存拷贝，4K 每秒约 300 MB）</text>
  </g>
</svg>
</figure>

为了让测量接近实际，这次窗口用 `SDL_WINDOW_HIDDEN` 隐藏起来，渲染器仍然是真正的 Metal（不再用前几篇的 dummy 驱动和软件渲染器）。播放完整的 10 秒（声音静音），记录整个播放器的 CPU 时间：

```bash
MUTE=1 HIDDEN=1 /usr/bin/time -p ./player6 bbb4k_hevc.mp4
MUTE=1 HIDDEN=1 HW=1 /usr/bin/time -p ./player6 bbb4k_hevc.mp4
MUTE=1 HIDDEN=1 HW=1 ZEROCOPY=1 /usr/bin/time -p ./player6 bbb4k_hevc.mp4
```

| 视频 | 软件解码 | 硬件解码 + 复制 | 硬件解码 + 零拷贝 |
|---|---|---|---|
| 720p H.264 | 2.03 秒 | 0.86 秒 | 0.60 秒 |
| 4K H.264 | 3.14 秒 | 2.13 秒 | 0.65 秒 |
| 4K HEVC | 4.00 秒 | 2.12 秒 | **0.64 秒** |

（10 秒播放过程中消耗的 CPU 时间。三种方式都是 240 帧全部按时显示，没有丢帧，音画偏差都在 21 毫秒以内。）

- **硬件解码 + 复制**：4K 时只省了一半。解码的 CPU 省下来了，但每帧 12 MB 的数据要从硬件缓冲区复制到内存，再上传到纹理，两次大块的内存拷贝成了新的瓶颈。
- **零拷贝**：画面从解码电路出来，直接变成 GPU 的纹理，CPU 完全不碰像素数据。4K HEVC 播放 10 秒只用了 0.64 秒 CPU 时间，相当于一个核心 6% 的占用，和播放 720p 差不多。剩下的 CPU 时间主要花在读文件、音频解码、线程之间的调度和主循环上。

最后确认画面是对的：用 `SDL_RenderReadPixels` 把三种方式渲染出的第 160 帧读回来，和 FFmpeg 转出的画面比较，PSNR **都是 47.94 dB**，三种方式的渲染结果完全相同。硬件解码出的画面和软件解码一模一样。

mpv、IINA、VLC 这些播放器在 macOS 上用硬件解码时，走的也是这种「解码器的缓冲区直接交给 GPU」的路径。

## 5. 硬件解码的限制

既然这么好，为什么不总是用硬件解码？

- **格式支持有限**：硬件只支持特定的编码、档次和分辨率。H.264 的 High 4:4:4 档次、10 位的 H.264、某些 HEVC 的扩展档次，很多硬件都不支持；老设备可能连 HEVC 都不支持。程序必须准备好退回软件解码（`get_format` 中的那一步）。
- **数据在硬件那边**：要在 CPU 上处理画面（加滤镜、截图、送给 AI 模型分析），就得复制回来，硬件的优势就打了折扣。
- **初始化开销、数量有限**：创建硬件解码会话比打开一个软件解码器更费事，同时能开的解码会话数量也有限，不适合同时解码大量的小视频（比如批量生成缩略图）。
- **出错处理**：遇到损坏的码流，FFmpeg 的软件解码器会尽力掩盖错误继续解码（第 2 章的丢包实验），硬件解码器的容错能力取决于硬件和驱动，可能直接报错，也可能输出明显的花屏。

所以播放器通常的策略是：**优先用硬件解码，不支持或者出错时自动退回软件解码**。

## 6. 小结

- 硬件解码把解码交给专门的电路，画面和软件解码完全相同，几乎不占 CPU。
- FFmpeg 的 hwcontext：`av_hwdevice_ctx_create` 创建设备，`hw_device_ctx` 交给解码器，`get_format` 选择硬件像素格式；硬件不支持时退回软件解码。
- 硬件帧的数据在硬件缓冲区中，`av_hwframe_transfer_data` 复制回内存（NV12）。
- 硬件解码的吞吐量不一定比多线程的软件解码高，它的价值是省 CPU、省电。
- 零拷贝：解码器的缓冲区直接作为 GPU 纹理，4K HEVC 播放的 CPU 时间从 4.0 秒降到 0.64 秒。
- 硬件解码受格式、数量、出错处理的限制，要准备好退回软件解码。

第 4 章到这里结束。我们的播放器已经能播放本地文件：多线程、音视频同步、暂停、跳转、硬件解码，这些都是 `ffplay` 中最核心的东西。第 5 章离开本地文件，看看音视频是怎样采集、怎样通过网络传输的：推流、拉流和各种流媒体协议。

## 练习

1. 用 `hw_decode` 的 `hw-copy` 模式解码，复制出的 NV12 帧，和软件解码出的 YUV420P 帧，内容一样吗？怎样验证？
2. 程序中的 `get_format` 在什么时候被调用？如果视频中途分辨率变了（有些直播流会这样），会发生什么？

<details>
<summary>点击查看答案</summary>

1. 一样。最简单的验证是用命令行：

   ```bash
   ffmpeg -hwaccel videotoolbox -i bbb-10s.mp4 -an -f rawvideo -pix_fmt yuv420p - | md5
   ffmpeg -i bbb-10s.mp4 -an -f rawvideo -pix_fmt yuv420p - | md5
   ```

   两次的结果都是 `20d9118c9b177896234cec92e2b4e86e`，和第 3 章软件解码出的 YUV 的 MD5 相同。第一条命令用硬件解码，复制出 NV12 后再转成 YUV420P。自己写程序验证时，可以用 libswscale 把 NV12 转成 YUV420P（这只是重新排列 U、V 的存放方式，不做任何计算，所以是无损的），再和软件解码的结果逐字节比较，或者像第 4 节那样，比较渲染后的画面。第 4 节中三种方式的渲染结果 PSNR 完全相同，就是这个结论的旁证。这再次说明：解码的结果是标准规定的，硬件和软件、FFmpeg 和别的解码器，只要实现正确，结果就逐位相同。编码则不然，第 3 章看到 VideoToolbox 和 x264 编码出的画质差了 4 dB。

2. `get_format` 在解码器需要确定输出格式时调用：通常是解码第一帧之前（读到 SPS 时），以及码流的参数发生变化、需要重新初始化时，例如分辨率、档次、像素格式变了。分辨率变化时，解码器会再次调用 `get_format`，硬件解码会话会被重新创建，之后输出的帧尺寸就是新的分辨率。播放器这边也要应付：程序中的纹理是按第一帧的尺寸创建的，帧的尺寸变了，就要销毁旧纹理、按新的尺寸重新创建，否则上传时会出错或者画面错乱。`ffplay` 每次显示前都会检查帧的宽高和格式是否和纹理一致，不一致就重新创建纹理。

</details>
