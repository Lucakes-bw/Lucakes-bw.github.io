---
title: 多线程与包队列
date: 2026-11-02 11:00:00
updated: 2026-11-02 11:00:00
categories:
  - 音视频开发
  - 播放器
tags:
  - 音视频
  - 播放器
  - 多线程
  - SDL
  - C
order: 3
description: 前两篇的播放器都在一个线程里读文件、解码、显示，任何一步慢了，后面的都得等。照着 ffplay 的结构拆成四个线程：读文件线程把包分进视频、音频两个包队列，视频解码线程把帧放进只有 3 个位置的帧队列，音频解码线程把 PCM 推给声卡，主线程只管处理事件和按时显示。队列用互斥锁加条件变量实现，满了让生产者等，空了让消费者等。模拟网络每秒卡顿 600 毫秒：包队列最多缓冲 0.2 秒时，81 帧画面迟到、声音断了 4 次；缓冲 0.5 秒以上，一帧都不迟到。去掉条件变量的一次通知，程序就卡死不动了。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解播放器为什么要用多个线程；会用互斥锁和条件变量实现线程安全的包队列和帧队列；理解队列的缓冲作用和背压（满了就等）；理解缓冲大小怎样决定播放器能承受多长的卡顿。
>
> **前置知识**：本章前两篇；线程、互斥锁和条件变量（Linux 基础专栏《进程与线程》，或 C/C++ 专栏《C++11》中的线程与条件变量）。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，SDL 3.4。

## 1. 为什么要多线程

第 1 篇的视频播放器在一个循环里依次做：读包、解码、等待、显示、处理事件。上一篇看到，只要某一步慢了一点，后面的事情都会被拖住。实际的播放器中，慢的原因很多：

- **读数据**：网络视频要从服务器下载，网速有波动，偶尔还会卡住几百毫秒；
- **视频解码**：I 帧比其他帧慢得多，高分辨率的视频解码一帧可能要几十毫秒；
- **显示**：窗口被拖动、系统忙的时候，渲染也会变慢；
- **音频**：设备按自己的节奏要数据，晚一点就欠载。

如果这些事情都在一个线程中排队，一处卡住，处处卡住。解决办法是把它们分给不同的线程，线程之间用**队列**连接：上游把结果放进队列就去做下一件事，下游从队列里取；队列中攒着的数据，就是应付上游临时变慢的**缓冲**。

`ffplay` 就是这样的结构，本篇的播放器照着它的思路，分成四个线程：

<figure class="diagram">
<svg viewBox="0 0 680 230" role="img" aria-label="多线程播放器的结构：读文件线程读出包，分别放进视频包队列和音频包队列；视频解码线程从视频包队列取包，解码后放进帧队列；主线程从帧队列取帧，按时显示；音频解码线程从音频包队列取包，解码后推进 SDL 的音频流，声卡从中取数据">
  <defs>
    <marker id="th-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="85" width="110" height="50" rx="6" style="fill:var(--panel-2)"/>
      <rect x="150" y="30" width="120" height="40" rx="6" style="fill:var(--mark)"/>
      <rect x="150" y="150" width="120" height="40" rx="6" style="fill:var(--mark)"/>
      <rect x="305" y="30" width="110" height="40" rx="6" style="fill:var(--panel-2)"/>
      <rect x="305" y="150" width="110" height="40" rx="6" style="fill:var(--panel-2)"/>
      <rect x="450" y="30" width="100" height="40" rx="6" style="fill:var(--mark)"/>
      <rect x="450" y="150" width="100" height="40" rx="6" style="fill:var(--mark)"/>
      <rect x="580" y="30" width="95" height="40" rx="6" style="fill:var(--panel-2)"/>
      <rect x="580" y="150" width="95" height="40" rx="6" style="fill:var(--accent-soft)"/>
    </g>
    <text x="14" y="106" style="fill:var(--fg)">读文件线程</text><text x="14" y="124" style="fill:var(--muted);font-size:10px">av_read_frame</text>
    <text x="160" y="47" style="fill:var(--fg)">视频包队列</text><text x="160" y="63" style="fill:var(--muted);font-size:10px">最多 N 秒</text>
    <text x="160" y="167" style="fill:var(--fg)">音频包队列</text>
    <text x="314" y="47" style="fill:var(--fg)">视频解码</text><text x="314" y="63" style="fill:var(--muted);font-size:10px">线程</text>
    <text x="314" y="167" style="fill:var(--fg)">音频解码</text><text x="314" y="183" style="fill:var(--muted);font-size:10px">线程 + swr</text>
    <text x="460" y="47" style="fill:var(--fg)">帧队列</text><text x="460" y="63" style="fill:var(--muted);font-size:10px">3 帧</text>
    <text x="460" y="167" style="fill:var(--fg)">SDL 音频流</text><text x="460" y="183" style="fill:var(--muted);font-size:10px">≤ 0.2 秒</text>
    <text x="590" y="47" style="fill:var(--fg)">主线程</text><text x="590" y="63" style="fill:var(--muted);font-size:10px">事件 + 显示</text>
    <text x="590" y="167" style="fill:var(--fg)">声卡</text><text x="590" y="183" style="fill:var(--muted);font-size:10px">音频线程</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#th-arrow)">
      <path d="M115,100 L130,50 L148,50"/><path d="M115,120 L130,170 L148,170"/>
      <line x1="270" y1="50" x2="303" y2="50"/><line x1="270" y1="170" x2="303" y2="170"/>
      <line x1="415" y1="50" x2="448" y2="50"/><line x1="415" y1="170" x2="448" y2="170"/>
      <line x1="550" y1="50" x2="578" y2="50"/><line x1="550" y1="170" x2="578" y2="170"/>
    </g>
    <text x="5" y="218" style="fill:var(--muted);font-size:11px">黄色的是队列：生产者放、消费者取；满了生产者等，空了消费者等。每个队列都是一段缓冲</text>
  </g>
</svg>
</figure>

## 2. 线程安全的队列

### 2.1 互斥锁和条件变量

多个线程同时访问一个队列，必须用**互斥锁**保护，否则两个线程同时修改链表的指针，队列就坏了。除此之外还有一个问题：队列空了，消费者该怎么办？

- 不停地检查（`while (empty) ;`）：白白占满一个 CPU 核心。
- 每隔几毫秒检查一次（上一篇推模式中的 `SDL_Delay(5)`）：能用，但有延迟，也不优雅。
- 用**条件变量**：消费者调用 `SDL_WaitCondition` 睡下去，同时释放锁；生产者放入数据后调用 `SDL_SignalCondition` 把它叫醒。

队列满的时候也一样，生产者睡下去，等消费者取走数据后叫醒它。这种「下游处理不过来，上游就停下来等」的机制叫**背压**（backpressure），它保证了内存不会无限增长：读文件线程不会把整部电影都读进内存，只会比播放的位置提前 N 秒。

### 2.2 程序

完整的程序比较长，分几部分看：

```c
// player3.c：多线程播放器——读文件线程、视频解码线程、音频解码线程，主线程负责显示
#include <stdio.h>
#include <stdlib.h>
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
    if (argc < 2) { fprintf(stderr, "用法: %s 文件 [最大缓冲秒数] [每秒卡顿毫秒]\n", argv[0]); return 1; }
    if (argc > 2) max_buffer = atof(argv[2]);
    if (argc > 3) stall_ms = atoi(argv[3]);
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

    pq_init(&videoq);
    pq_init(&audioq);
    for (int i = 0; i < FQ_SIZE; i++) frameq.frames[i] = av_frame_alloc();
    frameq.mutex = SDL_CreateMutex();
    frameq.cond = SDL_CreateCondition();
    qlog = fopen("queue.log", "w");

    t_start = SDL_GetTicksNS();
    SDL_Thread *td = SDL_CreateThread(demux_thread, "demux", NULL);
    SDL_Thread *tv = SDL_CreateThread(video_thread, "video", NULL);
    SDL_Thread *ta = SDL_CreateThread(audio_thread, "audio", NULL);

    // 主线程：处理事件，按 PTS 显示帧
    int frames = 0, late = 0, quit = 0;
    double max_late = 0, last_log = -1;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) quit = 1;
        double t = now_s();
        if (t - last_log >= 0.05) {                  // 每 50 毫秒记录一次队列的状态
            SDL_LockMutex(videoq.mutex);
            fprintf(qlog, "%.3f %.3f %d\n", t, videoq.duration, frameq.count);
            SDL_UnlockMutex(videoq.mutex);
            last_log = t;
        }
        SDL_LockMutex(frameq.mutex);
        if (frameq.count == 0) {
            int done = frameq.eof;
            SDL_UnlockMutex(frameq.mutex);
            if (done) break;
            SDL_Delay(1);
            continue;
        }
        AVFrame *f = frameq.frames[frameq.rindex];
        SDL_UnlockMutex(frameq.mutex);

        double pts = f->pts * av_q2d(fmt->streams[vs]->time_base);
        if (pts > t) { SDL_Delay(1); continue; }     // 还没到时间，先去处理事件
        if (t - pts > max_late) max_late = t - pts;
        if (t - pts > 1.0 / 24) late++;               // 晚了超过一帧
        SDL_UpdateYUVTexture(tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        frames++;

        SDL_LockMutex(frameq.mutex);
        av_frame_unref(f);
        frameq.rindex = (frameq.rindex + 1) % FQ_SIZE;
        frameq.count--;
        SDL_SignalCondition(frameq.cond);            // 叫醒可能因为帧队列满了而在等的视频解码线程
        SDL_UnlockMutex(frameq.mutex);
    }
    printf("缓冲 %.2f 秒，每秒卡顿 %d 毫秒：显示 %d 帧，晚了一帧以上的 %d 帧，最晚 %.0f 毫秒；音频断档 %d 次；用时 %.2f 秒\n",
           max_buffer, stall_ms, frames, late, max_late * 1000, SDL_GetAtomicInt(&underruns), now_s());

    SDL_WaitThread(td, NULL); SDL_WaitThread(tv, NULL); SDL_WaitThread(ta, NULL);
    fclose(qlog);
    SDL_Quit();
    return 0;
}
```

- **包队列**是一个链表，记录包的个数和总时长。`pq_put` 用 `av_packet_move_ref` 接管包的数据（第 3 章说过，包的数据是引用计数的，移动不复制），放到末尾，叫醒等数据的线程。`pq_get` 在队列空、又没有读完时等待；取出一个包后，也叫醒可能因为队列太满而在等的读文件线程。
- **读文件线程**只做一件事：读包，按流分进两个队列。视频包队列中的总时长超过 `max_buffer` 秒，就等。读完以后设置 `eof`，用 `SDL_BroadcastCondition` 叫醒所有等待的线程，让它们知道不会再有数据了。
- **帧队列**是一个只有 3 个位置的环形数组。视频解码线程解出一帧就放进去，满了就等；主线程取出队首的帧，到时间就显示，然后释放这个位置。为什么只有 3 帧？解码后的帧很大（720p 一帧 1.38 MB），没必要多存，提前解好两三帧就够应付解码时间的波动了；要缓冲时间，在包队列里缓冲（压缩的数据小几十倍）。
- **音频**沿用上一篇的推模式：音频解码线程把 PCM 推进 SDL 的音频流，队列超过 0.2 秒就等。推之前如果发现音频流已经空了，说明设备把数据放完了而新的还没来，记为一次断档。
- **主线程**不再做任何耗时的事：处理事件，看看帧队列的第一帧是不是到了显示时间，到了就显示，没到就睡 1 毫秒再看。窗口始终能及时响应。

## 3. 缓冲能扛住多长的卡顿

为了模拟不稳定的网络，读文件线程每过 1 秒就卡住一段时间（`SDL_Delay`），其余时间全速读取。分别设置不同的包队列大小，记录有多少帧晚了一帧以上（42 毫秒）、音频断档几次：

```bash
./player3 bbb-10s.mp4 1.0 0        # 不卡顿
./player3 bbb-10s.mp4 0.2 600      # 最多缓冲 0.2 秒，每秒卡 600 毫秒
```

| 最多缓冲 | 每秒卡 300 毫秒 | 每秒卡 600 毫秒 |
|---|---|---|
| 不卡顿（对照） | 0 帧迟到，0 次断档 | |
| 0.05 秒 | 9 帧迟到（最晚 69 毫秒），断档 3 次 | 81 帧迟到（最晚 370 毫秒），断档 8 次 |
| 0.2 秒 | 0 帧，0 次 | 54 帧迟到（最晚 257 毫秒），断档 4 次 |
| 0.5 秒 | 0 帧，0 次 | 0 帧，0 次 |
| 1.0 秒 | 0 帧，0 次 | 0 帧，0 次 |

规律很清楚：**缓冲的时长大于卡顿的时长，卡顿就完全看不出来**。0.2 秒的包队列能扛住 300 毫秒的卡顿，是因为帧队列（3 帧，0.125 秒）和音频流（0.2 秒）里还各有一点余量，加起来超过了 0.3 秒；扛 600 毫秒就不够了。

把视频包队列中缓冲的时长画出来（每 50 毫秒记录一次）：

<figure class="diagram">
<svg viewBox="0 0 680 220" role="img" aria-label="每秒卡顿 600 毫秒时视频包队列中缓冲的时长：最大缓冲 1 秒时，每次卡顿队列下降约 0.6 秒，随后很快补满，始终没有见底；最大缓冲 0.2 秒时，每次卡顿队列都降到 0">
  <g style="font-family:var(--mono);font-size:12px">
<line x1="50" y1="170.0" x2="650" y2="170.0" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="44" y="174.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0</text>
<line x1="50" y1="142.7" x2="650" y2="142.7" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="44" y="146.7" text-anchor="end" style="fill:var(--muted);font-size:10px">0.2</text>
<line x1="50" y1="101.8" x2="650" y2="101.8" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="44" y="105.8" text-anchor="end" style="fill:var(--muted);font-size:10px">0.5</text>
<line x1="50" y1="33.6" x2="650" y2="33.6" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="44" y="37.6" text-anchor="end" style="fill:var(--muted);font-size:10px">1</text>
<text x="50.0" y="185" text-anchor="middle" style="fill:var(--muted);font-size:10px">0s</text>
<text x="167.6" y="185" text-anchor="middle" style="fill:var(--muted);font-size:10px">2s</text>
<text x="285.3" y="185" text-anchor="middle" style="fill:var(--muted);font-size:10px">4s</text>
<text x="402.9" y="185" text-anchor="middle" style="fill:var(--muted);font-size:10px">6s</text>
<text x="520.6" y="185" text-anchor="middle" style="fill:var(--muted);font-size:10px">8s</text>
<text x="638.2" y="185" text-anchor="middle" style="fill:var(--muted);font-size:10px">10s</text>
<path d="M50.0,158.7 L53.0,27.9 L55.9,27.9 L58.9,27.9 L61.8,27.9 L65.0,27.9 L68.0,27.9 L71.0,27.9 L73.9,27.9 L76.9,27.9 L79.9,27.9 L82.9,27.9 L85.8,27.9 L88.8,27.9 L92.0,27.9 L94.9,27.9 L97.9,27.9 L100.9,27.9 L103.9,27.9 L106.9,27.9 L109.9,33.6 L112.8,39.4 L115.8,45.0 L118.9,50.7 L121.8,62.0 L124.8,67.7 L127.8,73.5 L130.8,79.0 L133.8,90.5 L136.8,96.1 L139.7,101.8 L142.7,107.5 L145.9,27.9 L148.9,27.9 L151.9,27.9 L154.9,27.9 L157.9,27.9 L160.9,27.9 L163.8,27.9 L166.8,27.9 L169.8,33.6 L172.8,39.4 L175.8,50.7 L178.8,56.4 L181.8,62.0 L184.7,67.7 L187.7,79.0 L190.7,84.8 L193.7,90.5 L196.7,96.1 L199.9,101.8 L202.8,113.1 L205.8,27.9 L208.8,27.9 L211.8,27.9 L214.8,27.9 L217.8,27.9 L220.7,27.9 L223.7,27.9 L226.8,27.9 L229.8,39.4 L232.8,45.0 L235.7,50.7 L238.7,56.4 L241.6,67.7 L244.6,73.5 L247.5,79.0 L250.5,84.8 L253.5,90.5 L256.4,101.8 L259.5,107.5 L262.4,27.9 L265.4,27.9 L268.5,27.9 L271.4,27.9 L274.4,27.9 L277.4,27.9 L280.3,27.9 L283.2,27.9 L286.2,33.6 L289.2,39.4 L292.1,45.0 L295.1,50.7 L298.1,62.0 L301.1,67.7 L304.1,73.5 L307.1,79.0 L310.1,84.8 L313.1,96.1 L316.1,101.8 L319.1,107.5 L322.0,27.9 L324.9,27.9 L328.0,27.9 L331.0,27.9 L333.9,27.9 L337.1,27.9 L340.1,27.9 L343.1,27.9 L346.1,33.6 L349.0,39.4 L352.0,50.7 L355.0,56.4 L357.9,62.0 L360.9,67.7 L363.9,73.5 L366.9,84.8 L369.9,90.5 L372.9,96.1 L375.8,101.8 L378.8,107.5 L381.8,27.9 L384.8,27.9 L387.7,27.9 L390.7,27.9 L393.6,27.9 L396.6,27.9 L399.6,27.9 L402.6,27.9 L405.7,33.6 L408.7,45.0 L411.6,50.7 L414.6,56.4 L417.6,62.0 L420.5,73.5 L423.5,79.0 L426.4,84.8 L429.5,90.5 L432.6,96.1 L435.6,107.5 L438.6,113.1 L441.6,27.9 L444.6,27.9 L447.5,27.9 L450.5,27.9 L453.5,27.9 L456.5,27.9 L459.6,27.9 L462.6,33.6 L465.6,39.4 L468.6,45.0 L471.5,50.7 L474.5,62.0 L477.5,67.7 L480.5,73.5 L483.5,79.0 L486.6,84.8 L489.6,96.1 L492.6,101.8 L495.6,107.5 L498.6,27.9 L501.5,27.9 L504.5,27.9 L507.5,27.9 L510.5,27.9 L513.5,27.9 L516.6,27.9 L519.5,27.9 L522.5,33.6 L525.5,39.4 L528.5,50.7 L531.4,56.4 L534.4,62.0 L537.4,67.7 L540.5,73.5 L543.4,84.8 L546.4,90.5 L549.4,96.1 L552.4,101.8 L555.3,113.1 L558.3,27.9 L561.2,27.9 L564.2,33.6 L567.1,39.4 L570.1,50.7 L573.1,56.4 L576.0,62.0 L578.9,67.7 L581.9,73.5 L584.9,84.8 L587.8,90.5 L590.8,96.1 L593.8,101.8 L596.9,107.5 L599.8,118.9 L602.8,124.6 L605.8,130.2 L608.8,135.9 L611.8,147.2 L614.7,153.0 L617.7,158.7 L620.6,164.3 L623.9,170.0 L626.9,170.0 L629.8,170.0 L632.8,170.0 L635.7,170.0" fill="none" style="stroke:var(--accent)" stroke-width="1.6"/>
<path d="M50.0,170.0 L53.0,141.6 L56.0,141.6 L59.0,141.6 L61.9,141.6 L65.0,141.6 L67.9,141.6 L70.9,141.6 L73.9,141.6 L76.9,141.6 L79.8,141.6 L82.8,141.6 L85.8,141.6 L88.8,141.6 L91.7,141.6 L94.6,141.6 L97.6,141.6 L100.6,141.6 L103.6,141.6 L106.7,141.6 L109.6,147.2 L112.6,153.0 L115.6,158.7 L118.6,164.3 L121.5,170.0 L124.5,170.0 L127.5,170.0 L130.5,170.0 L133.5,170.0 L136.4,170.0 L139.4,170.0 L142.4,170.0 L145.3,141.6 L148.2,141.6 L151.2,141.6 L154.2,141.6 L157.2,141.6 L160.2,141.6 L163.2,141.6 L166.1,141.6 L169.1,147.2 L172.0,153.0 L175.0,158.7 L177.9,170.0 L180.9,170.0 L183.9,170.0 L186.9,170.0 L189.8,170.0 L192.9,170.0 L195.8,170.0 L198.8,170.0 L201.7,170.0 L204.8,141.6 L207.8,141.6 L210.8,141.6 L213.8,141.6 L216.7,141.6 L219.7,141.6 L222.6,141.6 L225.6,141.6 L228.6,147.2 L231.6,153.0 L234.6,164.3 L237.5,170.0 L240.5,170.0 L243.5,170.0 L246.5,170.0 L249.5,170.0 L252.5,170.0 L255.4,170.0 L258.4,170.0 L261.4,170.0 L264.4,141.6 L267.3,141.6 L270.3,141.6 L273.4,141.6 L276.3,141.6 L279.2,141.6 L282.2,141.6 L285.2,141.6 L288.2,153.0 L291.2,158.7 L294.2,164.3 L297.2,170.0 L300.4,170.0 L303.4,170.0 L306.4,170.0 L309.3,170.0 L312.2,170.0 L315.2,170.0 L318.2,170.0 L321.1,141.6 L324.1,141.6 L327.2,141.6 L330.2,141.6 L333.2,141.6 L336.2,141.6 L339.1,141.6 L342.1,141.6 L345.1,147.2 L348.1,153.0 L351.0,158.7 L354.0,164.3 L356.9,170.0 L359.9,170.0 L362.9,170.0 L365.9,170.0 L368.8,170.0 L371.8,170.0 L374.8,170.0 L377.8,170.0 L380.7,141.6 L383.7,141.6 L386.7,141.6 L389.7,141.6 L392.6,141.6 L395.6,141.6 L398.6,141.6 L401.5,141.6 L404.5,147.2 L407.5,153.0 L410.6,158.7 L413.5,170.0 L416.5,170.0 L419.5,170.0 L422.5,170.0 L425.5,170.0 L428.4,170.0 L431.4,170.0 L434.3,170.0 L437.3,170.0 L440.4,141.6 L443.3,141.6 L446.3,141.6 L449.2,141.6 L452.2,141.6 L455.2,141.6 L458.2,141.6 L461.2,141.6 L464.2,147.2 L467.2,158.7 L470.2,164.3 L473.2,170.0 L476.2,170.0 L479.3,170.0 L482.3,170.0 L485.2,170.0 L488.2,170.0 L491.2,170.0 L494.1,170.0 L497.1,170.0 L500.1,141.6 L503.1,141.6 L506.1,141.6 L509.1,141.6 L512.1,141.6 L515.0,141.6 L518.0,141.6 L520.9,141.6 L523.9,153.0 L526.9,158.7 L529.8,164.3 L532.9,170.0 L535.9,170.0 L538.8,170.0 L541.8,170.0 L544.8,170.0 L547.8,170.0 L550.8,170.0 L553.7,170.0 L556.6,141.6 L559.6,141.6 L562.6,141.6 L565.5,141.6 L568.6,141.6 L571.5,141.6 L574.5,141.6 L577.5,141.6 L580.5,147.2 L583.4,153.0 L586.4,158.7 L589.5,164.3 L592.5,170.0 L595.5,170.0 L598.5,170.0 L601.5,170.0 L604.4,170.0 L607.4,170.0 L610.4,170.0 L613.3,170.0 L616.3,141.6 L619.2,164.3 L622.2,170.0 L625.2,170.0 L628.2,170.0 L631.2,170.0 L634.2,170.0" fill="none" style="stroke:var(--danger)" stroke-width="1.6"/>
    <line x1="60" y1="208" x2="90" y2="208" style="stroke:var(--accent)" stroke-width="2"/><text x="96" y="212" style="fill:var(--muted)">最多缓冲 1 秒</text>
    <line x1="230" y1="208" x2="260" y2="208" style="stroke:var(--danger)" stroke-width="2"/><text x="266" y="212" style="fill:var(--muted)">最多缓冲 0.2 秒</text>
    <text x="420" y="212" style="fill:var(--muted)">纵轴：视频包队列中的时长（秒）</text>
  </g>
</svg>
</figure>

- **最多缓冲 1 秒**（绿线）：平时队列是满的（1.04 秒）。每次卡顿开始，读文件线程不再放入新的包，队列随着播放一点点下降，600 毫秒后降到 0.42 秒左右；卡顿一结束，读文件线程全速读取，队列几乎瞬间又满了。队列从来没有见底，播放完全不受影响。
- **最多缓冲 0.2 秒**（红线）：每次卡顿不到 0.2 秒，队列就空了，剩下的 0.4 秒里视频解码线程无包可解，画面停住、声音中断；卡顿结束后，积压的帧一下子涌来，显示时都已经晚了。

这就是看网络视频时「缓冲中」的原因，也是播放器启动时要先缓冲一段的原因：缓冲越多，越能扛住网络的波动，但开始播放前要等得越久，占的内存也越多。直播还多一层顾虑：缓冲多少秒，就比真实情况晚多少秒，第 5 章会讨论这个取舍。

## 4. 两个容易出错的地方

### 4.1 总缓冲是整条流水线的缓冲

把帧队列从 3 帧改成 1 帧（练习 1），用 0.2 秒的包队列、每秒卡 300 毫秒再运行：

```
缓冲 0.20 秒，每秒卡顿 300 毫秒：显示 240 帧，晚了一帧以上的 0 帧，最晚 29 毫秒；音频断档 7 次
```

画面没有迟到，声音却断了 7 次，而 3 帧时一次都没有。读文件线程只看视频包队列满没满，帧队列变小后，视频解码线程更早地被堵住，视频包队列更快地被填满，读文件线程就更早地停下来等，**音频包**也就跟着断了供应。队列之间是联动的：能扛多长的卡顿，取决于整条流水线上所有缓冲的总和，以及它们之间怎样互相牵制。`ffplay` 对包队列的限制同时考虑了音频、视频的包数、总字节数和总时长，就是为了避免这类问题。

### 4.2 少一次通知，程序就卡死

`pq_get` 取出一个包后会调用 `SDL_SignalCondition`，叫醒可能在等的读文件线程。去掉这一行（练习 2）：

```
15 秒后仍未结束（卡死）
```

看队列的记录：视频包队列一开始就被填满，然后一路下降，到第 0.96 秒降到 0，帧队列也空了。画面播放了不到 1 秒就停住，之后程序永远卡在那里，不报任何错误。读文件线程把视频包队列填满后开始等待，但再也没有人叫醒它；视频解码线程取完了队列里已有的包，之后就等新的包，而新的包永远不会来，因为读文件线程还在睡。两个线程互相等待，这就是**死锁**。

多线程的错误往往不会立刻暴露：这个程序如果队列一直没满（比如文件很短、或者缓冲设得很大），就能「正常」运行，换一个文件才突然卡死。写队列时要对照着检查：每一处 `Wait` 等待的条件，在别的线程中改变时，是不是都有对应的 `Signal`。

## 5. 小结

- 播放器把读文件、视频解码、音频解码、显示分到不同的线程，用队列连接，一处变慢不会拖住全部。
- 线程安全的队列：互斥锁保护数据，条件变量让空队列的消费者、满队列的生产者睡下去等待，状态改变时叫醒对方。
- 背压：队列满了生产者就停下来，保证内存有上限。
- 缓冲要放在压缩数据（包）上；解码后的帧很大，帧队列只留几帧。
- 缓冲的时长大于卡顿的时长，卡顿就不会被察觉；缓冲越大越稳，但启动越慢、延迟越大。
- 能扛多长的卡顿取决于整条流水线的缓冲；少一次条件变量的通知就可能死锁。

现在画面和声音各自按时播放了，但两者是各走各的：画面按系统时钟，声音按声卡的时钟。下一篇让它们对齐。

## 练习

1. 把帧队列的大小改成 1，分别在不卡顿和「缓冲 0.2 秒、每秒卡 300 毫秒」的条件下运行，结果和 3 帧时有什么不同？
2. 去掉 `pq_get` 中的 `SDL_SignalCondition`，运行会怎样？为什么？

<details>
<summary>点击查看答案</summary>

1. 不卡顿时没有区别：240 帧都按时显示，音频没有断档，帧队列只要能放下一帧，解码就跟得上。缓冲 0.2 秒、每秒卡 300 毫秒时，画面仍然没有迟到，但音频断档 7 次（3 帧时是 0 次）。原因见第 4.1 节：帧队列变小，整条流水线的缓冲变少，读文件线程更早地被视频包队列堵住，连带着音频包也供应不上。改进的办法是读文件线程分别判断两个队列，任何一个队列太空时都要继续读，而不是只看视频队列。

2. 画面播放不到 1 秒就停住，程序卡死，15 秒后仍然没有结束，只能强行结束进程。读文件线程在视频包队列满（超过 1 秒）后调用 `SDL_WaitCondition` 睡下，叫醒它的只有 `pq_get` 中的那次通知；去掉之后，视频解码线程取走了包也不会叫醒它。视频解码线程取完队列中已有的包后，又在 `pq_get` 中等新的包，两个线程互相等待，形成死锁。`pq_put` 中的通知还在，但那是读文件线程自己发出的，它睡着了就不会再发。

</details>
