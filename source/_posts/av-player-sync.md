---
title: 音视频同步
date: 2026-11-02 12:00:00
updated: 2026-11-02 12:00:00
categories:
  - 音视频开发
  - 播放器
tags:
  - 音视频
  - 播放器
  - 音视频同步
  - C
order: 4
description: 上一篇的画面按系统时钟显示，声音按声卡的时钟播放，两个时钟只要走得不一样快，画面和声音就会慢慢错开。用 SDL 的 disk 音频驱动模拟一块偏慢 5% 的声卡：视频按系统时钟走，10 秒后画面比声音超前了 512 毫秒；改成以音频为主时钟，视频每一帧都对准「声卡此刻正在播的那个时刻」，偏差始终在 0 到 −21 毫秒之间。音频时钟由三部分算出：推给 SDL 的最后一个采样的时间，减去还在队列里的，再减去设备缓冲里正在播的。机器太慢、每帧渲染要 60 毫秒时，不丢帧的话 10 秒的视频要放 17 秒，画面落后 1.1 秒；晚了一帧以上就丢掉，丢 100 帧，偏差保持在 42 毫秒以内。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解为什么画面和声音会不同步；理解主时钟的概念和为什么通常以音频为主时钟；会计算音频时钟：推进去的数据、队列中的数据、设备缓冲的延迟；会根据画面与主时钟的差决定等待、显示还是丢帧。
>
> **前置知识**：本章前三篇；第 2 章《时间戳：PTS、DTS 与时间基》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，SDL 3.4。

## 1. 两个时钟

上一篇的播放器里，画面和声音是各自独立播放的：

- **画面**：主线程看系统时钟，第 n 帧的 PTS 到了就显示。
- **声音**：声卡按自己的晶振，每秒消耗 48000 个采样，消耗完就向程序要新的。

只要声卡的晶振和电脑的系统时钟走得完全一样快，两者就是同步的。但现实中没有两个完全一样的时钟：声卡标称 48000 Hz，实际可能是 48003 Hz 或者 47995 Hz，误差在万分之几的量级。万分之一看起来很小，播放一部两小时的电影就是 0.72 秒的偏差，嘴型和声音明显对不上了。用蓝牙耳机时，声音还要经过编码、无线传输、解码，延迟可能有一两百毫秒。

人对音画不同步的容忍度不对称：声音比画面**早**到时更容易察觉（现实中光比声音快，我们从不会先听到声音），广播电视的规范（ITU-R BT.1359）给出的可察觉门限大约是声音超前 45 毫秒、落后 125 毫秒。

解决办法是选一个时钟作为**主时钟**，另一路追着它走：

| 主时钟 | 做法 | 问题 |
|---|---|---|
| **音频**（最常用） | 声音照常播放，画面看音频时钟决定显示时刻：早了就等，晚了就丢帧 | 偶尔丢帧或重复一帧，眼睛几乎察觉不到 |
| 视频 | 画面照常显示，声音追画面：用重采样微调播放速度，或者删掉、插入一些采样 | 调整声音很容易被听出来（音调变化、咔嗒声） |
| 外部时钟 | 两路都追一个外部时钟（例如直播中服务器的时间） | 两路都要调整 |

以音频为主时钟的理由很简单：耳朵比眼睛挑剔。画面多停留一帧或少显示一帧，看不出来；声音快一点、慢一点、断一下，马上就能听出来。所以让声音自由地按声卡的节奏播放，画面去迁就声音。`ffplay` 默认也是这样（`-sync audio`）。

## 2. 音频时钟

以音频为主时钟，首先要知道：**声卡此刻正在播放的，是第几秒的声音？**

音频解码线程知道自己推给 SDL 的最后一个采样是第几秒的（帧的 PTS 加上它的时长），但这些采样并没有马上播放出来，中间还隔着两段缓冲：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="音频时钟的计算：推给 SDL 的最后一个采样的时间，减去 SDL 队列中还没被取走的数据的时长，再减去设备缓冲中正在播放的数据的时长，就是此刻正在播放的声音的时间">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(20,40)">
      <rect x="0" y="0" width="380" height="40" style="fill:var(--link);opacity:.25;stroke:var(--link)"/>
      <rect x="380" y="0" width="100" height="40" style="fill:var(--warn);opacity:.35;stroke:var(--warn)"/>
      <rect x="480" y="0" width="160" height="40" style="fill:var(--panel-2);stroke:var(--border-2);stroke-dasharray:4 3"/>
      <text x="120" y="25" style="fill:var(--fg)">SDL 音频流的队列</text>
      <text x="392" y="18" style="fill:var(--fg);font-size:11px">设备缓冲</text>
      <text x="392" y="33" style="fill:var(--muted);font-size:10px">1024 采样</text>
      <text x="505" y="25" style="fill:var(--muted)">已经播放过的</text>
      <line x1="0" y1="-10" x2="0" y2="52" style="stroke:var(--fg)"/>
      <text x="-10" y="-16" style="fill:var(--fg);font-size:11px">audio_end_pts</text>
      <line x1="480" y1="-10" x2="480" y2="52" style="stroke:var(--danger)" stroke-width="2"/>
      <text x="440" y="-16" style="fill:var(--danger);font-size:11px">此刻正在播放</text>
      <text x="460" y="72" style="fill:var(--muted);font-size:10px">←── 时间（声音的 PTS）递减</text>
    </g>
    <text x="20" y="148" style="fill:var(--fg)">音频时钟 = audio_end_pts − 队列中的数据 / (48000 × 4) − 设备缓冲 1024 / 48000</text>
    <text x="20" y="172" style="fill:var(--muted);font-size:11px">队列中的数据用 SDL_GetAudioStreamQueued 得到（字节数），设备缓冲用 SDL_GetAudioDeviceFormat 得到（采样数）</text>
  </g>
</svg>
</figure>

1. **SDL 音频流的队列**：推进去还没被设备取走的数据，`SDL_GetAudioStreamQueued` 返回字节数，除以每秒的字节数（48000 × 4）就是时长；
2. **设备缓冲**：设备已经取走、正在播放的那一块，大小是设备每次处理的采样数（上一篇看到是 1024 个，21.3 毫秒）。

所以：

```
音频时钟 = 最后推进去的采样的时间 − 队列中数据的时长 − 设备缓冲的时长
```

这和 `ffplay` 的做法一样（它还会补偿从上次更新时钟到现在过去的时间）。用蓝牙耳机时，系统还会报告额外的输出延迟，也应该减去，否则画面会比声音早一两百毫秒。

## 3. 程序

在上一篇的多线程播放器上修改，这里只列出改动的部分，完整的程序放在下面的折叠区中：

```c
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

// 音频解码线程中，每推一帧数据，就更新 audio_end_pts
            SDL_PutAudioStreamData(astream, out, n * 4);
            SDL_LockMutex(clock_mutex);
            audio_end_pts = frame->pts * av_q2d(fmt->streams[as]->time_base) + n / 48000.0;
            SDL_UnlockMutex(clock_mutex);

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
            SDL_UpdateYUVTexture(tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
            SDL_RenderClear(ren);
            SDL_RenderTexture(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            if (render_ms) SDL_Delay(render_ms);               // 模拟机器太慢：每帧渲染额外花这么久
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
```

<details>
<summary>展开完整的 player4.c</summary>

```c
// player4.c：在 player3 的基础上做音视频同步：以音频为主时钟，视频追着音频走
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>

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

static AVCodecContext *open_decoder(int idx)
{
    const AVCodec *c = avcodec_find_decoder(fmt->streams[idx]->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar);
    ctx->pkt_timebase = fmt->streams[idx]->time_base;
    avcodec_open2(ctx, c, NULL);
    return ctx;
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 文件 [audio|wall]\n", argv[0]); return 1; }
    int sync_audio = !(argc > 2 && SDL_strcmp(argv[2], "wall") == 0);
    int render_ms = getenv("RENDER_MS") ? atoi(getenv("RENDER_MS")) : 0;
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
    SDL_CreateWindowAndRenderer("player3", vdec->width, vdec->height, SDL_WINDOW_RESIZABLE, &win, &ren);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_IYUV);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, vdec->width);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, vdec->height);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_LIMITED);
    SDL_Texture *tex = SDL_CreateTextureWithProperties(ren, props);
    SDL_DestroyProperties(props);
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
            SDL_UpdateYUVTexture(tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
            SDL_RenderClear(ren);
            SDL_RenderTexture(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            if (render_ms) SDL_Delay(render_ms);               // 模拟机器太慢：每帧渲染额外花这么久
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

主线程每次循环：

- 读出音频时钟作为主时钟（`wall` 模式下用系统时钟，作为对比）；声音还没开始播放时，画面也先等着。
- 取帧队列的第一帧，算出它的 PTS 和主时钟的差 `diff`：
  - `diff > 0`：还没到时间，等一会儿；
  - `diff < −一帧的时长`，而且后面还有帧：已经晚了一帧以上，**丢掉**这一帧，直接看下一帧；
  - 否则：显示。
- 每显示一帧，记录这一刻**画面和声音的偏差**：这一帧的 PTS 减去音频时钟。正数表示画面超前于声音。

两个环境变量用于实验：`RENDER_MS` 让每帧渲染后额外等一段时间，模拟机器太慢；`NODROP` 关掉丢帧。

## 4. 实验

### 4.1 时钟一致时

用真实的声卡（静音），两种模式都试一下：

```bash
MUTE=1 ./player4 bbb-10s.mp4 audio
MUTE=1 ./player4 bbb-10s.mp4 wall
```

```
音频驱动 coreaudio，设备缓冲 21.3 毫秒；视频跟随音频时钟
显示 240 帧，丢掉 0 帧；画面与声音的偏差最大 -21 毫秒；用时 10.00 秒
音频驱动 coreaudio，设备缓冲 21.3 毫秒；视频跟随系统时钟
显示 240 帧，丢掉 0 帧；画面与声音的偏差最大 +23 毫秒；用时 10.00 秒
```

10 秒内，Core Audio 和系统时钟走得几乎一样快，两种方式都没有问题。以音频为主时钟时，偏差在 0 到 −21 毫秒之间，平均 −9.5 毫秒：设备每次从队列中取走 1024 个采样，队列的长度是一块一块减少的，音频时钟也就每 21.3 毫秒跳一次，画面要等时钟跳过自己的 PTS 才显示，所以总是晚一点点，最多晚一个设备缓冲。这个偏差远在人能察觉的范围之内。

### 4.2 声卡的时钟偏慢

上一篇说过，SDL 的 `disk` 驱动按「模拟的」时间消耗数据，它的计时本身就不准：10 秒的声音要 10.5 秒左右才放完，相当于一块慢了 5% 的声卡。这正好可以用来做实验：

```bash
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/dev/null ./player4 bbb-10s.mp4 wall
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/dev/null ./player4 bbb-10s.mp4 audio
```

```
音频驱动 disk，设备缓冲 21.3 毫秒；视频跟随系统时钟
显示 240 帧，丢掉 0 帧；画面与声音的偏差最大 +512 毫秒；用时 9.99 秒
音频驱动 disk，设备缓冲 21.3 毫秒；视频跟随音频时钟
显示 240 帧，丢掉 0 帧；画面与声音的偏差最大 -21 毫秒；用时 10.51 秒
```

- **视频按系统时钟**：画面准时在 10 秒内放完，声音却慢慢落后，到最后画面比声音超前了 **512 毫秒**，远远超过可察觉的门限。
- **视频按音频时钟**：画面跟着声音变慢，用了 10.51 秒放完，和声音一起结束；偏差始终在 −21 毫秒以内。

### 4.3 机器太慢

另一种不同步的原因是画面跟不上：解码或渲染一帧的时间比帧间隔（41.7 毫秒）还长。让每帧渲染后多等 60 毫秒：

```bash
RENDER_MS=60 NODROP=1 MUTE=1 ./player4 bbb-10s.mp4 audio
RENDER_MS=60 MUTE=1 ./player4 bbb-10s.mp4 audio
```

```
显示 240 帧，丢掉 0 帧；画面与声音的偏差最大 -1145 毫秒；用时 17.28 秒
显示 140 帧，丢掉 100 帧；画面与声音的偏差最大 -42 毫秒；用时 10.10 秒
```

- **不丢帧**：每一帧都要显示，画面越来越落后于声音。声音 10 秒就放完了，画面放了 17.3 秒，最大偏差 1.1 秒。
- **丢帧**：晚了一帧以上的帧直接丢掉，240 帧中丢了 100 帧，显示的 140 帧和声音的偏差不超过 42 毫秒，10.1 秒放完。画面变得不那么流畅（每秒只有 14 帧），但声音和嘴型是对的。

### 4.4 偏差随时间的变化

把四种情况下每一帧的偏差画出来：

<figure class="diagram">
<svg viewBox="0 0 680 240" role="img" aria-label="画面与声音的偏差随时间的变化：声卡偏慢而视频按系统时钟时，画面越来越超前，10 秒时超前约 0.5 秒；视频按音频时钟时，偏差始终在 0 附近；渲染太慢又不丢帧时，画面越来越落后，最后落后 1.1 秒；丢帧时偏差保持在几十毫秒以内">
  <g style="font-family:var(--mono);font-size:12px">
<line x1="55" y1="15.0" x2="655" y2="15.0" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="19.0" text-anchor="end" style="fill:var(--muted);font-size:10px">+600</text>
<line x1="55" y1="43.3" x2="655" y2="43.3" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="47.3" text-anchor="end" style="fill:var(--muted);font-size:10px">+300</text>
<line x1="55" y1="71.7" x2="655" y2="71.7" style="stroke:var(--border);"/><text x="49" y="75.7" text-anchor="end" style="fill:var(--muted);font-size:10px">+0</text>
<line x1="55" y1="100.0" x2="655" y2="100.0" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="104.0" text-anchor="end" style="fill:var(--muted);font-size:10px">-300</text>
<line x1="55" y1="128.3" x2="655" y2="128.3" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="132.3" text-anchor="end" style="fill:var(--muted);font-size:10px">-600</text>
<line x1="55" y1="156.7" x2="655" y2="156.7" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="160.7" text-anchor="end" style="fill:var(--muted);font-size:10px">-900</text>
<line x1="55" y1="185.0" x2="655" y2="185.0" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="189.0" text-anchor="end" style="fill:var(--muted);font-size:10px">-1200</text>
<text x="55.0" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">0s</text>
<text x="123.6" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">2s</text>
<text x="192.1" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">4s</text>
<text x="260.7" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">6s</text>
<text x="329.3" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">8s</text>
<text x="397.9" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">10s</text>
<text x="466.4" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">12s</text>
<text x="535.0" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">14s</text>
<text x="603.6" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">16s</text>
<path d="M56.5,73.7 L57.2,69.7 L58.7,69.8 L60.1,69.9 L61.5,70.0 L63.0,70.1 L64.5,70.2 L65.9,70.3 L67.4,68.4 L68.8,68.5 L70.2,68.6 L71.7,68.7 L73.0,68.8 L74.5,68.9 L75.9,69.0 L77.4,67.0 L78.8,67.1 L80.2,67.2 L81.7,67.3 L83.1,67.4 L84.5,67.5 L86.0,67.6 L87.4,65.7 L88.8,65.8 L90.2,65.9 L91.7,66.0 L93.0,66.1 L94.4,66.2 L95.9,64.2 L97.4,64.3 L98.8,64.4 L100.2,64.5 L101.7,64.6 L103.1,64.7 L104.5,62.8 L105.9,62.9 L107.4,63.0 L108.8,63.1 L110.2,63.2 L111.7,63.3 L113.1,63.4 L114.5,63.5 L115.9,61.5 L117.4,61.6 L118.8,61.7 L120.3,61.8 L121.7,61.9 L123.1,62.0 L124.5,62.1 L125.9,60.2 L127.4,60.3 L128.8,60.4 L130.3,60.5 L131.7,60.6 L133.1,60.6 L134.5,60.7 L136.0,60.8 L137.4,58.9 L138.8,59.0 L140.2,59.1 L141.6,59.2 L143.1,59.3 L144.5,59.4 L146.0,57.5 L147.4,57.6 L148.8,57.7 L150.2,57.8 L151.5,57.8 L153.0,57.9 L154.4,56.0 L155.9,56.1 L157.3,56.2 L158.7,56.3 L160.2,56.4 L161.6,56.5 L163.0,54.6 L164.5,54.7 L165.9,54.8 L167.3,54.9 L168.8,55.0 L170.2,55.0 L171.7,53.1 L173.1,53.2 L174.5,53.3 L175.9,53.4 L177.4,53.5 L178.8,53.6 L180.2,53.7 L181.7,51.8 L183.1,51.9 L184.5,52.0 L186.0,52.1 L187.4,52.1 L188.8,52.2 L190.2,50.3 L191.7,50.4 L193.1,50.5 L194.5,50.6 L195.9,50.7 L197.3,50.8 L198.8,50.9 L200.2,49.0 L201.7,49.1 L203.1,49.2 L204.5,49.3 L206.0,49.3 L207.4,49.4 L208.8,47.5 L210.2,47.6 L211.7,47.7 L213.1,47.8 L214.5,47.9 L216.0,48.0 L217.4,48.1 L218.8,46.2 L220.2,46.3 L221.7,46.4 L223.1,46.5 L224.5,46.5 L225.9,46.6 L227.4,46.7 L228.8,44.8 L230.2,44.9 L231.7,45.0 L233.1,45.1 L234.5,45.2 L236.0,45.3 L237.4,43.4 L238.7,43.5 L240.2,43.6 L241.7,43.6 L243.1,43.7 L244.5,43.8 L246.0,43.9 L247.3,44.0 L248.7,42.1 L250.2,42.2 L251.7,42.3 L253.1,42.4 L254.4,42.5 L255.9,42.6 L257.3,42.7 L258.8,42.8 L260.1,42.9 L261.6,40.9 L263.0,41.0 L264.5,41.1 L265.9,41.2 L267.4,41.3 L268.8,41.4 L270.2,41.5 L271.7,41.6 L273.1,39.7 L274.5,39.8 L275.9,39.9 L277.4,40.0 L278.8,40.1 L280.2,40.2 L281.7,40.2 L283.1,38.3 L284.5,38.4 L286.0,38.5 L287.4,38.6 L288.7,38.7 L290.2,38.8 L291.6,36.9 L293.0,37.0 L294.5,37.1 L295.9,37.2 L297.3,37.3 L298.8,37.4 L300.2,37.4 L301.7,37.5 L303.1,35.6 L304.5,35.7 L305.9,35.8 L307.3,35.9 L308.8,36.0 L310.2,36.1 L311.6,34.2 L313.1,34.3 L314.5,34.4 L315.9,34.5 L317.4,34.5 L318.8,34.6 L320.2,32.7 L321.6,32.8 L323.0,32.9 L324.5,33.0 L325.9,33.1 L327.4,33.2 L328.8,33.3 L330.2,31.4 L331.7,31.5 L333.1,31.6 L334.5,31.7 L335.9,31.7 L337.4,31.8 L338.8,31.9 L340.2,30.0 L341.7,30.1 L343.1,30.2 L344.5,30.3 L345.9,30.4 L347.3,30.5 L348.8,30.6 L350.2,28.7 L351.6,28.8 L353.1,28.9 L354.5,28.9 L356.0,29.0 L357.3,29.1 L358.8,27.2 L360.2,27.3 L361.6,27.4 L363.1,27.5 L364.5,27.6 L365.9,27.7 L367.3,27.8 L368.8,27.9 L370.2,28.0 L371.7,26.1 L373.0,26.1 L374.5,26.2 L375.9,26.3 L377.4,26.4 L378.8,26.5 L380.2,26.6 L381.6,24.7 L383.1,24.8 L384.5,24.9 L385.9,25.0 L387.4,25.1 L388.8,25.2 L390.2,25.3 L391.6,23.3 L393.1,23.4 L394.5,23.5 L395.9,23.6 L397.3,23.7" fill="none" style="stroke:var(--danger)" stroke-width="1.6"/>
<path d="M56.6,73.7 L57.4,71.8 L59.0,71.9 L60.5,72.0 L62.1,72.0 L63.7,72.1 L65.2,72.2 L66.7,72.3 L68.2,72.4 L69.6,72.5 L71.3,72.6 L72.8,72.7 L74.2,72.8 L75.9,72.9 L77.4,73.0 L78.9,73.1 L80.5,73.2 L82.0,73.3 L83.6,73.4 L85.2,73.5 L86.6,73.6 L87.5,71.6 L89.0,71.7 L90.6,71.8 L92.1,71.9 L93.6,72.0 L95.0,72.1 L96.6,72.2 L98.1,72.3 L99.7,72.4 L101.1,72.5 L102.8,72.6 L104.3,72.7 L105.8,72.8 L107.4,72.9 L108.9,73.0 L110.5,73.1 L112.0,73.1 L113.5,73.2 L115.1,73.3 L116.6,73.4 L118.2,73.5 L119.0,71.6 L120.6,71.7 L122.1,71.8 L123.6,71.9 L125.2,72.0 L126.7,72.1 L128.2,72.2 L129.7,72.3 L131.2,72.4 L132.8,72.5 L134.3,72.5 L135.9,72.6 L137.4,72.7 L139.0,72.8 L140.6,72.9 L142.2,73.0 L143.6,73.1 L145.2,73.2 L146.7,73.3 L148.2,73.4 L149.8,73.5 L150.6,71.6 L152.2,71.7 L153.6,71.8 L155.2,71.9 L156.8,72.0 L158.3,72.0 L159.8,72.1 L161.4,72.2 L163.0,72.3 L164.5,72.4 L166.1,72.5 L167.7,72.6 L169.2,72.7 L170.8,72.8 L172.3,72.9 L173.8,73.0 L175.4,73.1 L177.0,73.2 L178.5,73.3 L180.0,73.4 L181.5,73.5 L183.1,73.6 L183.7,71.6 L185.4,71.7 L186.9,71.8 L188.3,71.9 L189.9,72.0 L191.5,72.1 L193.0,72.2 L194.6,72.3 L196.1,72.4 L197.7,72.5 L199.2,72.6 L200.7,72.7 L202.3,72.8 L203.7,72.9 L205.1,73.0 L206.7,73.1 L208.1,73.1 L209.7,73.2 L211.2,73.3 L212.7,73.4 L214.3,73.5 L215.0,71.6 L216.6,71.7 L218.1,71.8 L219.7,71.9 L221.3,72.0 L222.8,72.1 L224.3,72.2 L225.8,72.3 L227.4,72.4 L229.0,72.5 L230.5,72.5 L232.1,72.6 L233.6,72.7 L235.2,72.8 L236.7,72.9 L238.2,73.0 L239.9,73.1 L241.4,73.2 L242.9,73.3 L244.5,73.4 L246.0,73.5 L246.8,71.6 L248.3,71.7 L249.8,71.8 L251.4,71.9 L253.0,72.0 L254.6,72.0 L256.1,72.1 L257.6,72.2 L259.1,72.3 L260.6,72.4 L262.1,72.5 L263.7,72.6 L265.2,72.7 L266.8,72.8 L268.3,72.9 L269.9,73.0 L271.4,73.1 L273.0,73.2 L274.5,73.3 L276.1,73.4 L277.5,73.5 L279.2,73.6 L279.8,71.6 L281.4,71.7 L283.0,71.8 L284.4,71.9 L285.9,72.0 L287.5,72.1 L289.1,72.2 L290.6,72.3 L292.1,72.4 L293.8,72.5 L295.3,72.6 L296.9,72.7 L298.4,72.8 L299.9,72.9 L301.5,73.0 L303.1,73.1 L304.6,73.1 L306.1,73.2 L307.7,73.3 L309.2,73.4 L310.7,73.5 L311.4,71.6 L312.9,71.7 L314.5,71.8 L316.1,71.9 L317.5,72.0 L319.1,72.1 L320.7,72.2 L322.3,72.3 L323.8,72.4 L325.3,72.5 L326.9,72.5 L328.4,72.6 L329.9,72.7 L331.3,72.8 L333.0,72.9 L334.5,73.0 L336.0,73.1 L337.5,73.2 L339.1,73.3 L340.6,73.4 L342.1,73.5 L342.9,71.6 L344.3,71.7 L345.9,71.8 L347.5,71.9 L349.0,72.0 L350.6,72.0 L352.2,72.1 L353.8,72.2 L355.3,72.3 L356.9,72.4 L358.5,72.5 L360.0,72.6 L361.5,72.7 L363.1,72.8 L364.7,72.9 L366.2,73.0 L367.8,73.1 L369.3,73.2 L370.9,73.3 L372.5,73.4 L373.9,73.5 L375.4,73.6 L376.2,71.6 L377.7,71.7 L379.2,71.8 L380.7,71.9 L382.3,72.0 L383.7,72.1 L385.1,72.2 L386.7,72.3 L388.3,72.4 L389.7,72.5 L391.2,72.6 L392.9,72.7 L394.5,72.8 L396.0,72.9 L397.5,73.0 L399.1,73.1 L400.7,73.1 L402.2,73.2 L403.8,73.3 L405.4,73.4 L406.8,73.5 L407.6,71.6 L409.0,71.7 L410.6,71.8 L412.2,71.9 L413.7,72.0 L415.3,72.1" fill="none" style="stroke:var(--accent)" stroke-width="1.6"/>
<path d="M59.3,73.7 L61.7,75.8 L64.1,77.9 L66.5,82.0 L69.0,84.1 L71.5,86.2 L74.0,88.4 L76.3,92.5 L78.8,94.6 L81.4,98.7 L84.0,100.8 L86.6,104.9 L89.0,107.1 L91.5,109.2 L94.1,113.3 L96.6,115.4 L99.0,119.5 L101.3,121.6 L103.8,123.7 L106.3,127.9 L108.5,130.0 L111.0,132.1 L113.5,136.2 L116.1,138.3 L118.6,142.4 L121.2,144.5 L123.6,148.7 L126.0,150.8 L128.2,152.9 L130.8,155.0 L133.2,159.1 L135.5,161.2 L138.1,163.3 L140.6,167.5 L143.1,169.6 L145.6,171.7 L148.0,175.8 L150.3,175.9 L152.6,176.0 L155.2,176.1 L157.8,176.2 L160.3,176.3 L162.7,176.4 L165.2,176.5 L167.7,176.6 L170.3,176.7 L172.8,176.8 L175.3,176.8 L177.9,176.9 L180.5,177.0 L183.1,177.1 L185.5,177.2 L188.0,177.3 L190.3,177.4 L192.6,177.5 L195.2,177.6 L197.6,177.7 L200.0,177.8 L202.3,177.9 L204.9,176.0 L207.4,176.1 L210.0,176.2 L212.5,176.2 L215.1,176.3 L217.6,176.4 L220.2,176.5 L222.5,176.6 L224.9,176.7 L227.5,176.8 L230.1,176.9 L232.6,177.0 L235.1,177.1 L237.6,177.2 L240.0,177.3 L242.3,177.4 L244.6,177.5 L246.8,177.6 L249.4,177.7 L252.0,177.8 L254.3,177.9 L256.8,175.9 L259.3,176.0 L261.7,176.1 L264.0,176.2 L266.4,176.3 L269.0,176.4 L271.4,176.5 L273.7,176.6 L276.3,176.7 L278.5,176.8 L280.9,176.9 L283.2,177.0 L285.7,177.1 L288.0,177.2 L290.5,177.3 L293.1,177.4 L295.5,177.4 L297.8,177.5 L300.4,177.6 L302.9,177.7 L305.5,177.8 L307.8,175.9 L310.2,176.0 L312.7,176.1 L315.2,176.2 L317.6,176.3 L320.0,176.4 L322.5,176.5 L325.0,176.6 L327.5,176.7 L329.8,176.8 L332.3,176.8 L334.6,176.9 L336.9,177.0 L339.2,177.1 L341.7,177.2 L344.1,177.3 L346.4,177.4 L348.9,177.5 L351.4,177.6 L354.0,177.7 L356.5,177.8 L358.9,177.9 L361.4,176.0 L363.9,176.1 L366.3,176.2 L368.9,176.2 L371.2,176.3 L373.5,176.4 L376.0,176.5 L378.5,176.6 L381.1,176.7 L383.6,176.8 L386.2,176.9 L388.5,177.0 L390.9,177.1 L393.4,177.2 L396.0,177.3 L398.3,177.4 L400.7,177.5 L402.9,177.6 L405.1,177.7 L407.5,177.8 L409.8,177.9 L412.1,175.9 L414.6,176.0 L417.2,176.1 L419.6,176.2 L422.2,176.3 L424.7,176.4 L427.3,176.5 L429.9,176.6 L432.5,176.7 L434.9,176.8 L437.4,176.9 L440.0,177.0 L442.6,177.1 L444.9,177.2 L447.4,177.3 L450.0,177.4 L452.6,177.4 L454.9,177.5 L457.2,177.6 L459.5,177.7 L462.0,177.8 L464.5,175.9 L466.9,176.0 L469.3,176.1 L471.7,176.2 L474.1,176.3 L476.7,176.4 L479.2,176.5 L481.8,176.6 L484.3,176.7 L486.8,176.8 L489.2,176.8 L491.6,176.9 L494.2,177.0 L496.6,177.1 L499.0,177.2 L501.6,177.3 L504.0,177.4 L506.6,177.5 L508.8,177.6 L511.1,177.7 L513.6,177.8 L516.1,177.9 L518.5,176.0 L520.9,176.1 L523.5,176.2 L525.8,176.2 L528.2,176.3 L530.5,176.4 L533.0,176.5 L535.4,176.6 L538.0,176.7 L540.6,176.8 L543.1,176.9 L545.7,177.0 L548.3,177.1 L550.9,177.2 L553.2,177.3 L555.4,177.4 L558.0,177.5 L560.6,177.6 L562.9,177.7 L565.5,177.8 L568.1,177.9 L570.7,175.9 L573.3,176.0 L575.6,176.1 L578.2,178.2 L580.6,179.8 L583.1,175.9 L585.6,172.0 L588.2,168.0 L590.7,164.1 L593.0,160.2 L595.5,156.2 L597.8,152.3 L600.3,148.4 L602.6,144.4 L605.1,140.5 L607.4,136.6 L609.8,132.6 L612.3,128.7 L614.9,124.7 L617.5,120.8 L620.0,116.9 L622.3,112.9 L624.9,109.0 L627.5,105.1 L630.0,101.1 L632.5,97.2 L634.9,93.3 L637.2,89.3 L639.7,85.4 L642.3,81.5 L644.9,77.5 L647.4,73.6" fill="none" style="stroke:var(--warn)" stroke-width="1.6"/>
<path d="M59.3,73.7 L61.7,71.9 L64.2,74.0 L66.7,74.2 L69.2,72.3 L71.7,74.4 L74.2,74.6 L76.6,72.8 L79.0,73.0 L81.5,75.1 L83.8,73.3 L86.2,75.4 L88.6,75.6 L91.1,73.7 L93.7,71.9 L96.0,72.1 L98.3,74.2 L100.7,72.4 L103.3,74.5 L105.8,74.7 L108.4,74.9 L110.6,73.1 L113.1,75.2 L115.6,73.3 L118.2,73.5 L120.5,71.7 L123.1,71.9 L125.5,74.0 L128.0,72.2 L130.4,72.4 L132.7,74.5 L135.3,72.6 L137.8,72.8 L140.3,74.9 L142.8,75.1 L145.3,73.3 L147.7,75.4 L150.2,75.6 L152.8,73.8 L155.1,74.0 L157.4,72.1 L159.9,74.2 L162.5,72.4 L164.9,72.6 L167.3,74.7 L169.7,72.9 L172.2,73.1 L174.7,75.2 L177.1,75.4 L179.4,73.6 L181.8,71.7 L184.3,73.8 L186.7,74.0 L189.3,72.2 L191.7,72.4 L194.2,74.5 L196.7,72.7 L199.1,72.9 L201.6,75.0 L204.1,75.2 L206.3,73.3 L208.8,75.4 L211.3,73.6 L213.7,73.8 L216.2,72.0 L218.8,74.1 L221.1,74.3 L223.4,72.5 L226.0,74.6 L228.6,74.8 L231.1,72.9 L233.5,73.1 L236.0,75.2 L238.4,73.4 L240.8,73.6 L243.4,71.8 L245.7,72.0 L248.2,74.1 L250.8,72.2 L253.3,72.4 L255.7,74.5 L258.3,72.7 L260.6,72.9 L263.1,75.0 L265.7,75.2 L268.2,73.4 L270.8,73.6 L273.4,71.7 L275.9,71.9 L278.4,74.0 L281.0,72.2 L283.4,72.4 L285.8,74.5 L288.4,74.7 L290.9,72.9 L293.5,75.0 L295.9,75.2 L298.3,73.3 L300.6,73.5 L303.1,71.7 L305.7,73.8 L308.2,74.0 L310.7,72.2 L313.0,72.4 L315.5,74.5 L318.1,72.6 L320.6,72.8 L323.0,74.9 L325.3,73.1 L327.8,73.3 L330.4,75.4 L333.0,75.6 L335.5,73.8 L337.9,72.0 L340.2,72.1 L342.8,74.2 L345.1,72.4 L347.7,72.6 L350.2,74.7 L352.7,74.9 L355.2,73.1 L357.7,75.2 L360.2,75.4 L362.7,73.6 L365.0,73.7 L367.5,71.9 L369.9,74.0 L372.4,74.2 L374.9,72.4 L377.2,74.5 L379.4,72.7 L381.7,72.9 L384.2,75.0 L386.7,73.1 L389.1,75.3 L391.6,75.4 L393.9,73.6 L396.5,71.8 L398.9,72.0 L401.1,73.6" fill="none" style="stroke:var(--link)" stroke-width="1.6"/>
    <text x="60" y="12" style="fill:var(--muted);font-size:10px">偏差（毫秒，正数表示画面超前于声音）</text>
<line x1="55" y1="208" x2="81" y2="208" style="stroke:var(--danger)" stroke-width="2"/><text x="87" y="212" style="fill:var(--muted);font-size:11px">声卡偏慢，视频按系统时钟</text>
<line x1="355" y1="208" x2="381" y2="208" style="stroke:var(--accent)" stroke-width="2"/><text x="387" y="212" style="fill:var(--muted);font-size:11px">声卡偏慢，视频按音频时钟</text>
<line x1="55" y1="226" x2="81" y2="226" style="stroke:var(--warn)" stroke-width="2"/><text x="87" y="230" style="fill:var(--muted);font-size:11px">渲染太慢，不丢帧</text>
<line x1="355" y1="226" x2="381" y2="226" style="stroke:var(--link)" stroke-width="2"/><text x="387" y="230" style="fill:var(--muted);font-size:11px">渲染太慢，丢帧</text>
  </g>
</svg>
</figure>

- 红线：声卡偏慢、视频按系统时钟，偏差线性增长，这就是两个时钟的速度差，一直累积。
- 绿线：视频按音频时钟，偏差贴着 0。
- 黄线：渲染太慢、不丢帧，偏差一路往下走，而且越来越快（声音放完以后，音频时钟就停住了）。
- 蓝线：丢帧，偏差在 0 到 −42 毫秒之间来回，丢掉一帧就追回来一些。

**同步的本质，就是不让偏差累积。**无论原因是时钟的速度差，还是机器跟不上，只要有一个基准，每一帧都和它比较、纠正，误差就不会越积越大。和第 1 篇的「按 PTS 显示而不是固定延时」、第 2 章的「从帧号换算时间而不是累加时长」是同一个思想。

## 5. 小结

- 画面和声音各自按不同的时钟播放，时钟的速度差会让它们越来越不同步。
- 选一个主时钟，另一路追着它走；通常以音频为主时钟，因为调整画面（丢帧、等待）比调整声音更不容易被察觉。
- 音频时钟 = 最后推给设备的采样的时间 − 队列中数据的时长 − 设备缓冲的时长。
- 画面比主时钟早就等，晚了一帧以上就丢帧；机器太慢时，丢帧比放慢整个画面好。
- 偏差的上限大约是设备缓冲的时长（这里是 21 毫秒），远低于人能察觉的门限。

下一篇加上暂停和拖动进度条。

## 练习

1. 计算音频时钟时，如果不减去设备缓冲的时长，会有什么影响？程序报告的「偏差」会变吗？为什么？
2. 如果以视频为主时钟，声音要怎样追画面？`ffplay` 是怎么做的？

<details>
<summary>点击查看答案</summary>

1. 音频时钟会比真实情况快 21.3 毫秒（把正在设备缓冲里播放的那一块当成了已经播完的），画面也就会比对应的声音早 21.3 毫秒显示。画面早到，相当于声音落后了 21 毫秒，在可察觉的门限（声音落后 125 毫秒）之内，单独看问题不大；但如果用的是蓝牙耳机，设备的延迟可能有 150～250 毫秒，不减就明显不同步了。

   有意思的是，程序报告的偏差**不会变**：实测仍然是「最大 −21 毫秒，平均 −9.6 毫秒」。因为偏差也是拿画面的 PTS 和这个（错误的）音频时钟比较的，尺子本身歪了，量出来的结果当然看不出来。真正的音画偏差只能从外部测量：比如播放一段专门的测试视频（画面闪白的同时发出「嘀」的一声），用摄像机同时录下屏幕和声音，再比较两者出现的时刻。专业的播放设备、电视机都会用这种方法校准。

2. 声音不能像画面那样随便丢一帧、等一会儿，否则会听到断续和咔嗒声。`ffplay` 的做法（`synchronize_audio`）是：比较音频时钟和主时钟的差，差别超过一个阈值时，在把这一帧交给声卡之前，按比例增加或减少这一帧的采样数（最多改变 10%），再用 libswresample 的补偿功能（`swr_set_compensation`）把采样数平滑地调整过去，相当于把这一小段声音轻微地拉长或压缩。这样调整很缓慢，一般听不出来，但如果偏差很大，追上来需要很长时间。所以除非有特殊需要（比如只有画面没有声音，或者要和外部的时钟同步），播放器都以音频为主时钟。

</details>
