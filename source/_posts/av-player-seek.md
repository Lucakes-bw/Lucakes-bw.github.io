---
title: 暂停与跳转
date: 2026-11-02 13:00:00
updated: 2026-11-02 13:00:00
categories:
  - 音视频开发
  - 播放器
tags:
  - 音视频
  - 播放器
  - seek
  - C
order: 5
description: 以音频为主时钟之后，暂停只要停住声卡：音频时钟不走了，画面自然也停了。跳转则要动整条流水线：读文件线程用 avformat_seek_file 跳到目标之前最近的关键帧，清空两个包队列；每个包、每个帧都带上序号，跳转一次序号加 1，解码线程遇到新序号就冲刷解码器，主线程丢掉旧序号的帧，音频线程清空 SDL 的队列。从第 2 秒跳到第 7 秒：按关键帧跳，35 毫秒后显示的是 5.458 秒的画面；精确跳转要先解码并丢掉 37 帧，104 毫秒后显示正好 7.000 秒。不冲刷解码器，跳转后会先冒出两帧跳转前的旧画面。写程序时还踩了两个坑：跳转还没执行就显示了旧帧，以及冲刷的顺序错了，最后两帧丢失。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会实现暂停；理解跳转时流水线上每一个环节要做什么：定位、清空队列、冲刷解码器、清空音频、重置时钟；理解用序号（serial）区分跳转前后的数据；分清按关键帧跳转和精确跳转，知道它们的速度差别。
>
> **前置知识**：本章前四篇；第 2 章《H.264 码流》中的 IDR 与随机访问；第 3 章《解封装与解码》中的 `av_seek_frame` 和冲刷。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，SDL 3.4。

## 1. 暂停

上一篇以音频为主时钟之后，暂停变得非常简单：调用 `SDL_PauseAudioStreamDevice` 停住声卡。

- 声卡不再取数据，SDL 队列中的数据量不变，音频时钟（最后推进去的时间 − 队列中的时长 − 设备缓冲）也就停在原地；
- 主线程看到下一帧的 PTS 总是比音频时钟晚，就一直等着，画面停在当前帧；
- 音频解码线程发现队列满了（超过 0.2 秒），也停下来等；视频解码线程在帧队列满了以后停下来；读文件线程在包队列满了以后停下来。

整条流水线都因为背压自然地停住了，不需要通知每个线程。继续播放时调用 `SDL_ResumeAudioStreamDevice`，一切又自然地动起来。用自动测试的方式在第 3 秒暂停 2 秒：

```bash
PAUSE_AT=3 MUTE=1 ./player5 bbb-10s.mp4
```

```
第 3.00 秒：暂停 2 秒
结束：画面 9.958 秒，用时 11.96 秒
```

播放 10 秒的视频用了 12 秒，正好多出暂停的 2 秒，暂停前后画面和声音都是连续的。

如果是以系统时钟为主时钟，暂停就麻烦得多：要记下暂停的时刻，恢复时把「开始播放的时刻」往后挪暂停的时长，否则一恢复，所有的帧都「晚了」，全部被丢掉。这也是以音频为主时钟的一个好处。

## 2. 跳转要做哪些事

跳转（seek，拖动进度条）就复杂多了。流水线上每一个环节都存着「跳转之前」的数据，全部要处理掉：

<figure class="diagram">
<svg viewBox="0 0 680 260" role="img" aria-label="跳转时流水线上每个环节要做的事：读文件线程用 avformat_seek_file 定位到关键帧，清空两个包队列，序号加 1；视频和音频解码线程遇到新序号的包，用 avcodec_flush_buffers 冲刷解码器；音频线程还要清空 SDL 音频流；主线程丢掉旧序号的帧，精确跳转时还要丢掉目标之前的帧">
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="20" width="160" height="200" rx="8" style="fill:var(--panel-2)"/>
      <rect x="180" y="20" width="160" height="200" rx="8" style="fill:var(--panel-2)"/>
      <rect x="355" y="20" width="160" height="200" rx="8" style="fill:var(--panel-2)"/>
      <rect x="530" y="20" width="145" height="200" rx="8" style="fill:var(--panel-2)"/>
    </g>
    <text x="15" y="42" style="fill:var(--fg)">读文件线程</text>
    <text x="15" y="70" style="fill:var(--muted);font-size:11px">① 定位到目标之前</text>
    <text x="15" y="86" style="fill:var(--muted);font-size:11px">   最近的关键帧</text>
    <text x="15" y="110" style="fill:var(--muted);font-size:11px">② 清空视频、音频</text>
    <text x="15" y="126" style="fill:var(--muted);font-size:11px">   两个包队列</text>
    <text x="15" y="150" style="fill:var(--muted);font-size:11px">③ 序号加 1</text>
    <text x="15" y="190" style="fill:var(--accent);font-size:10px">avformat_seek_file</text>
    <text x="190" y="42" style="fill:var(--fg)">视频解码线程</text>
    <text x="190" y="70" style="fill:var(--muted);font-size:11px">取到新序号的包时：</text>
    <text x="190" y="94" style="fill:var(--muted);font-size:11px">④ 冲刷解码器，扔掉</text>
    <text x="190" y="110" style="fill:var(--muted);font-size:11px">   积压的旧帧和参考帧</text>
    <text x="190" y="190" style="fill:var(--accent);font-size:10px">avcodec_flush_buffers</text>
    <text x="365" y="42" style="fill:var(--fg)">音频解码线程</text>
    <text x="365" y="70" style="fill:var(--muted);font-size:11px">取到新序号的包时：</text>
    <text x="365" y="94" style="fill:var(--muted);font-size:11px">⑤ 冲刷解码器</text>
    <text x="365" y="118" style="fill:var(--muted);font-size:11px">⑥ 清空 SDL 音频流</text>
    <text x="365" y="134" style="fill:var(--muted);font-size:11px">   中的旧声音</text>
    <text x="365" y="190" style="fill:var(--accent);font-size:10px">SDL_ClearAudioStream</text>
    <text x="540" y="42" style="fill:var(--fg)">主线程</text>
    <text x="540" y="70" style="fill:var(--muted);font-size:11px">⑦ 丢掉帧队列中</text>
    <text x="540" y="86" style="fill:var(--muted);font-size:11px">   旧序号的帧</text>
    <text x="540" y="110" style="fill:var(--muted);font-size:11px">⑧ 精确跳转：丢掉</text>
    <text x="540" y="126" style="fill:var(--muted);font-size:11px">   目标之前的帧</text>
    <text x="540" y="150" style="fill:var(--muted);font-size:11px">⑨ 第一帧立刻显示</text>
    <text x="5" y="248" style="fill:var(--muted);font-size:11px">每个包、每个帧都记着自己的序号：序号对不上的，就是跳转之前的数据，一律作废</text>
  </g>
</svg>
</figure>

难点在于这些事情分散在四个线程中，而且跳转随时可能发生：某个线程可能正在解码一个旧的包，可能正在等队列，可能刚刚把一帧旧画面放进帧队列。`ffplay` 的办法是**序号**（serial）：包队列有一个当前序号，每放入一个包，就把当前序号记在包上；跳转时清空队列，序号加 1。之后每个环节只需要比较序号：解码线程发现取到的包序号变了，就冲刷解码器；解出的帧也带上序号，主线程看到序号和当前的不一样，就直接丢掉。旧数据不管还在流水线的哪个位置，都会被识别出来。

## 3. 程序

```c
// player5.c：在 player4 的基础上加入暂停（空格）和跳转（← → 各 5 秒）
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>

// ---------------- 包队列：每个包带一个序号，跳转一次序号加 1 ----------------
typedef struct PktNode { AVPacket *pkt; int serial; struct PktNode *next; } PktNode;
typedef struct {
    PktNode *first, *last;
    double duration;
    int serial;                      // 当前的序号，跳转时加 1
    int eof;
    SDL_Mutex *mutex;
    SDL_Condition *cond;
} PacketQueue;

static void pq_init(PacketQueue *q) { SDL_zerop(q); q->mutex = SDL_CreateMutex(); q->cond = SDL_CreateCondition(); }

static void pq_put(PacketQueue *q, AVPacket *pkt, double dur)
{
    PktNode *n = av_malloc(sizeof *n);
    n->pkt = av_packet_alloc();
    av_packet_move_ref(n->pkt, pkt);
    n->next = NULL;
    SDL_LockMutex(q->mutex);
    n->serial = q->serial;
    if (q->last) q->last->next = n; else q->first = n;
    q->last = n;
    q->duration += dur;
    SDL_SignalCondition(q->cond);
    SDL_UnlockMutex(q->mutex);
}

// 清空队列，序号加 1：之前的包全部作废
static void pq_flush(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    for (PktNode *n = q->first, *next; n; n = next) {
        next = n->next;
        av_packet_free(&n->pkt);
        av_free(n);
    }
    q->first = q->last = NULL;
    q->duration = 0;
    q->eof = 0;
    q->serial++;
    SDL_BroadcastCondition(q->cond);
    SDL_UnlockMutex(q->mutex);
}

static int pq_get(PacketQueue *q, AVPacket *pkt, int *serial, double dur_of(const AVPacket *))
{
    SDL_LockMutex(q->mutex);
    while (!q->first && !q->eof) SDL_WaitCondition(q->cond, q->mutex);
    int ret = 0;
    if (q->first) {
        PktNode *n = q->first;
        q->first = n->next;
        if (!q->first) q->last = NULL;
        q->duration -= dur_of(n->pkt);
        av_packet_move_ref(pkt, n->pkt);
        *serial = n->serial;
        av_packet_free(&n->pkt);
        av_free(n);
        ret = 1;
    }
    SDL_SignalCondition(q->cond);
    SDL_UnlockMutex(q->mutex);
    return ret;
}

// ---------------- 帧队列 ----------------
#define FQ_SIZE 3
typedef struct {
    AVFrame *frames[FQ_SIZE];
    int serials[FQ_SIZE];
    int rindex, windex, count, eof;
    SDL_Mutex *mutex;
    SDL_Condition *cond;
} FrameQueue;

static AVFormatContext *fmt;
static int vs, as;
static AVCodecContext *vdec, *adec;
static PacketQueue videoq, audioq;
static FrameQueue frameq;
static SDL_AudioStream *astream;
static Uint64 t_start;
static int no_flush;                             // 实验用：跳转时不冲刷解码器

// 跳转请求：主线程设置，读文件线程执行
static SDL_AtomicInt seek_req;
static double seek_target;

static SDL_Mutex *clock_mutex;
static double audio_end_pts = -1;
static int audio_serial = -1;                    // 音频时钟属于哪个序号
static double device_latency;

static double vdur(const AVPacket *p) { return p->duration * av_q2d(fmt->streams[vs]->time_base); }
static double adur(const AVPacket *p) { return p->duration * av_q2d(fmt->streams[as]->time_base); }
static double now_s(void) { return (SDL_GetTicksNS() - t_start) / 1e9; }

static double audio_clock(int serial)
{
    SDL_LockMutex(clock_mutex);
    double end = audio_end_pts;
    int s = audio_serial;
    SDL_UnlockMutex(clock_mutex);
    if (end < 0 || s != serial) return -1;       // 跳转后新的声音还没推进去
    return end - SDL_GetAudioStreamQueued(astream) / (48000.0 * 4) - device_latency;
}

static int SDLCALL demux_thread(void *arg)
{
    (void)arg;
    AVPacket *pkt = av_packet_alloc();
    int eof = 0;
    for (;;) {
        if (SDL_GetAtomicInt(&seek_req)) {
            // 跳到目标时间之前最近的关键帧（第 2 章、第 3 章：只能从关键帧开始解码）
            int64_t ts = (int64_t)(seek_target * AV_TIME_BASE);
            if (avformat_seek_file(fmt, -1, INT64_MIN, ts, ts, 0) >= 0) {
                pq_flush(&videoq);                   // 清空队列，序号加 1
                pq_flush(&audioq);
            }
            eof = 0;
            SDL_SetAtomicInt(&seek_req, 0);
        }
        if (eof) { SDL_Delay(10); continue; }        // 读完了也不退出：用户还可能往回跳
        SDL_LockMutex(videoq.mutex);
        int full = videoq.duration > 1.0;
        SDL_UnlockMutex(videoq.mutex);
        if (full) { SDL_Delay(2); continue; }
        if (av_read_frame(fmt, pkt) < 0) {
            for (PacketQueue *q = &videoq; q; q = q == &videoq ? &audioq : NULL) {
                SDL_LockMutex(q->mutex); q->eof = 1; SDL_BroadcastCondition(q->cond); SDL_UnlockMutex(q->mutex);
            }
            eof = 1;
            continue;
        }
        if (pkt->stream_index == vs) pq_put(&videoq, pkt, vdur(pkt));
        else if (pkt->stream_index == as) pq_put(&audioq, pkt, adur(pkt));
        else av_packet_unref(pkt);
    }
    return 0;
}

static int SDLCALL video_thread(void *arg)
{
    (void)arg;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int serial = 0, cur_serial = 0;
    for (;;) {
        int got = pq_get(&videoq, pkt, &serial, vdur);
        if (got) {
            if (serial != cur_serial) {                  // 跳转之后的第一个包
                if (!no_flush) avcodec_flush_buffers(vdec);   // 丢掉解码器里积压的旧帧和参考帧
                cur_serial = serial;
            }
            avcodec_send_packet(vdec, pkt);
            av_packet_unref(pkt);
        } else {
            avcodec_send_packet(vdec, NULL);             // 读完了：冲刷，取出最后几帧
        }
        while (avcodec_receive_frame(vdec, frame) >= 0) {
            SDL_LockMutex(frameq.mutex);
            while (frameq.count == FQ_SIZE) SDL_WaitCondition(frameq.cond, frameq.mutex);
            av_frame_move_ref(frameq.frames[frameq.windex], frame);
            frameq.serials[frameq.windex] = cur_serial;
            frameq.windex = (frameq.windex + 1) % FQ_SIZE;
            frameq.count++;
            SDL_SignalCondition(frameq.cond);
            SDL_UnlockMutex(frameq.mutex);
        }
        if (!got) {                                      // 冲刷完了，重置解码器，等待（用户可能还会往回跳）
            avcodec_flush_buffers(vdec);
            SDL_LockMutex(videoq.mutex);
            while (videoq.eof && !videoq.first) SDL_WaitCondition(videoq.cond, videoq.mutex);
            SDL_UnlockMutex(videoq.mutex);
        }
    }
    return 0;
}

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
    int serial = 0, cur_serial = 0, started = 0;
    for (;;) {
        if (!pq_get(&audioq, pkt, &serial, adur)) { SDL_Delay(10); continue; }
        if (serial != cur_serial) {                      // 跳转了：丢掉解码器和 SDL 队列中的旧声音
            avcodec_flush_buffers(adec);
            SDL_ClearAudioStream(astream);
            cur_serial = serial;
        }
        avcodec_send_packet(adec, pkt);
        av_packet_unref(pkt);
        while (avcodec_receive_frame(adec, frame) >= 0) {
            while (SDL_GetAudioStreamQueued(astream) > 48000 * 4 / 5 && serial == audioq.serial) SDL_Delay(2);
            if (serial != audioq.serial) { av_frame_unref(frame); break; }   // 等的时候又跳转了
            uint8_t *dst[1] = {out};
            int n = swr_convert(swr, dst, 8192, (const uint8_t **)frame->extended_data, frame->nb_samples);
            SDL_PutAudioStreamData(astream, out, n * 4);
            SDL_LockMutex(clock_mutex);
            audio_end_pts = frame->pts * av_q2d(fmt->streams[as]->time_base) + n / 48000.0;
            audio_serial = cur_serial;
            SDL_UnlockMutex(clock_mutex);
            if (!started) { SDL_ResumeAudioStreamDevice(astream); started = 1; }
            av_frame_unref(frame);
        }
    }
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

// 发起跳转：记下目标，序号的递增由读文件线程完成
static void request_seek(double target)
{
    seek_target = target < 0 ? 0 : target;
    SDL_SetAtomicInt(&seek_req, 1);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2) { fprintf(stderr, "用法: %s 文件\n", argv[0]); return 1; }
    // 自动测试用：在第 SEEK_AT 秒跳到 SEEK_TO 秒；ACCURATE=1 表示精确跳转；NOFLUSH=1 表示不冲刷解码器
    double seek_at = getenv("SEEK_AT") ? atof(getenv("SEEK_AT")) : -1;
    double seek_to = getenv("SEEK_TO") ? atof(getenv("SEEK_TO")) : 0;
    int accurate = getenv("ACCURATE") != NULL;
    double pause_at = getenv("PAUSE_AT") ? atof(getenv("PAUSE_AT")) : -1;
    no_flush = getenv("NOFLUSH") != NULL;

    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    vdec = open_decoder(vs);
    adec = open_decoder(as);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_CreateWindowAndRenderer("player5", vdec->width, vdec->height, SDL_WINDOW_RESIZABLE, &win, &ren);
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

    pq_init(&videoq);
    pq_init(&audioq);
    for (int i = 0; i < FQ_SIZE; i++) frameq.frames[i] = av_frame_alloc();
    frameq.mutex = SDL_CreateMutex();
    frameq.cond = SDL_CreateCondition();

    t_start = SDL_GetTicksNS();
    SDL_CreateThread(demux_thread, "demux", NULL);
    SDL_CreateThread(video_thread, "video", NULL);
    SDL_CreateThread(audio_thread, "audio", NULL);

    int paused = 0, quit = 0, serial = 0, seeking = 0, shown_after_seek = 0, dropped_after_seek = 0, stale = 0;
    double frame_dur = av_q2d(av_inv_q(fmt->streams[vs]->avg_frame_rate)), seek_time = 0, last_pts = -1;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit = 1;
            if (e.type != SDL_EVENT_KEY_DOWN) continue;
            if (e.key.key == SDLK_ESCAPE) quit = 1;
            if (e.key.key == SDLK_SPACE) {               // 暂停：停住声卡，音频时钟随之停住，画面自然也停了
                paused = !paused;
                if (paused) SDL_PauseAudioStreamDevice(astream); else SDL_ResumeAudioStreamDevice(astream);
            }
            if (e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT) {
                request_seek(last_pts + (e.key.key == SDLK_RIGHT ? 5 : -5));
                seeking = 1; seek_time = now_s();
            }
        }
        if (seek_at >= 0 && now_s() >= seek_at) {        // 自动测试
            printf("第 %.2f 秒（画面 %.3f 秒）：跳到 %.1f 秒（%s）\n", now_s(), last_pts, seek_to, accurate ? "精确" : "关键帧");
            request_seek(seek_to);
            seeking = 1; seek_time = now_s(); seek_at = -1;
        }
        if (pause_at >= 0 && now_s() >= pause_at) {
            printf("第 %.2f 秒：暂停 2 秒\n", now_s());
            SDL_PauseAudioStreamDevice(astream);
            SDL_Delay(2000);                             // 暂停期间什么也不显示
            SDL_ResumeAudioStreamDevice(astream);
            pause_at = -1;
        }
        if (paused) { SDL_Delay(10); continue; }
        if (seeking) {
            if (SDL_GetAtomicInt(&seek_req)) { SDL_Delay(1); continue; }   // 读文件线程还没执行跳转，先等着
            serial = videoq.serial;                      // 跳转已执行：从现在起只认新序号的帧
        }

        double ac = audio_clock(serial);
        SDL_LockMutex(frameq.mutex);
        if (frameq.count == 0) { SDL_UnlockMutex(frameq.mutex); SDL_Delay(1); continue; }
        AVFrame *f = frameq.frames[frameq.rindex];
        int fserial = frameq.serials[frameq.rindex], has_next = frameq.count > 1;
        SDL_UnlockMutex(frameq.mutex);

        double pts = f->pts * av_q2d(fmt->streams[vs]->time_base);
        int drop = 0, show = 0;
        if (fserial != serial) drop = 1;                     // 跳转之前解出来的旧帧
        else if (seeking && accurate && pts < seek_target - frame_dur / 2) { drop = 1; dropped_after_seek++; }   // 精确跳转：目标之前的帧不显示
        else if (seeking) show = 1;                          // 跳转后的第一帧：不等音频时钟，马上显示
        else if (ac < 0) { SDL_Delay(1); continue; }
        else if (pts - ac > 0.001) { SDL_Delay(1); continue; }
        else if (pts - ac < -frame_dur && has_next) drop = 1;
        else show = 1;

        if (show) {
            SDL_UpdateYUVTexture(tex, NULL, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2], f->linesize[2]);
            SDL_RenderClear(ren);
            SDL_RenderTexture(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            if (seeking) {
                printf("  跳转后 %.0f 毫秒显示第一帧：%.3f 秒（目标 %.1f 秒），之前丢掉了 %d 帧\n",
                       (now_s() - seek_time) * 1000, pts, seek_target, dropped_after_seek);
                seeking = 0; shown_after_seek = 0;
            } else if (shown_after_seek < 6 && seek_time > 0) {
                printf("  接着显示：%.3f 秒%s\n", pts, pts < last_pts ? "  ← 时间倒退了" : "");
                if (pts < last_pts) stale++;
                shown_after_seek++;
            }
            last_pts = pts;
        }
        if (drop || show) {
            SDL_LockMutex(frameq.mutex);
            av_frame_unref(f);
            frameq.rindex = (frameq.rindex + 1) % FQ_SIZE;
            frameq.count--;
            SDL_SignalCondition(frameq.cond);
            SDL_UnlockMutex(frameq.mutex);
        }
        if (!seeking && last_pts > fmt->duration / (double)AV_TIME_BASE - 1.5 * frame_dur) quit = 1;   // 最后一帧显示完
    }
    printf("结束：画面 %.3f 秒，用时 %.2f 秒%s\n", last_pts, now_s(), stale ? "，出现了时间倒退" : "");
    SDL_Quit();
    _exit(0);                                            // 线程都在无限循环中，直接退出
}
```

和上一篇相比的主要改动：

- **包队列的节点多了 `serial`**，`pq_flush` 清空队列并把序号加 1。
- **读文件线程**检查跳转请求：`avformat_seek_file` 把读取位置移到目标时间之前最近的关键帧（`max_ts` 设为目标时间，表示不能超过它），然后清空两个队列。读到文件末尾后不退出，因为用户还可能往回跳。
- **视频解码线程**：取到的包序号变了，调用 `avcodec_flush_buffers`；解出的帧带上序号放进帧队列。
- **音频解码线程**：序号变了，冲刷解码器，并用 `SDL_ClearAudioStream` 扔掉还没播放的旧声音；音频时钟也记下自己属于哪个序号，跳转后、新的声音推进去之前，音频时钟是无效的。
- **主线程**：按键 ← → 发起跳转（空格暂停）；跳转请求被执行之后，只认新序号的帧；跳转后的第一帧不等音频时钟，立刻显示，让用户马上看到结果。精确跳转时，PTS 早于目标的帧都丢掉。
- 用环境变量 `SEEK_AT`、`SEEK_TO`、`ACCURATE`、`NOFLUSH`、`PAUSE_AT` 做自动测试。

## 4. 两种跳转

### 4.1 按关键帧跳转

在第 2 秒时跳到第 7 秒：

```bash
SEEK_AT=2 SEEK_TO=7 MUTE=1 ./player5 bbb-10s.mp4
```

```
第 2.00 秒（画面 1.958 秒）：跳到 7.0 秒（关键帧）
  跳转后 35 毫秒显示第一帧：5.458 秒（目标 7.0 秒），之前丢掉了 0 帧
  接着显示：5.500 秒
  接着显示：5.542 秒
  ……
结束：画面 9.958 秒，用时 6.59 秒
```

35 毫秒后就显示出了画面，但显示的是 **5.458 秒**，不是 7 秒。第 2 章说过，只有关键帧（IDR）才能作为解码的起点，7 秒之前最近的关键帧在 5.458 秒（第 131 帧），`avformat_seek_file` 就定位到了那里。

### 4.2 精确跳转

```bash
SEEK_AT=2 SEEK_TO=7 ACCURATE=1 MUTE=1 ./player5 bbb-10s.mp4
```

```
第 2.00 秒（画面 1.958 秒）：跳到 7.0 秒（精确）
  跳转后 104 毫秒显示第一帧：7.000 秒（目标 7.0 秒），之前丢掉了 37 帧
  接着显示：7.042 秒
  ……
```

显示的正是 7.000 秒的画面，但要等 104 毫秒：从 5.458 秒的关键帧开始，解码出 37 帧再丢掉，才能得到第 7 秒的画面（第 3 章第 2 篇练习 3 算过同样的数字）。跳到不同的位置：

| 目标 | 起点的关键帧 | 丢掉的帧 | 显示第一帧用时 | 显示的画面 |
|---|---|---|---|---|
| 3.3 秒 | 3.292 秒 | 0 | 25 毫秒 | 3.292 秒 |
| 3.4 秒 | 3.292 秒 | 3 | 33 毫秒 | 3.417 秒 |
| 7.0 秒 | 5.458 秒 | 37 | 104 毫秒 | 7.000 秒 |
| 9.4 秒 | 7.458 秒 | 47 | 127 毫秒 | 9.417 秒 |

（目标和帧的 PTS 相差不到半帧时，就显示那一帧。）用时取决于目标离前一个关键帧有多远，最坏的情况是要解码将近一个 GOP。GOP 越长，精确跳转越慢，这是第 1 章就提到的取舍。

两种方式各有用处：拖动进度条的过程中，画面要跟着鼠标快速变化，用关键帧跳转；松开鼠标、或者点击「前进 10 秒」时，用户期望准确的位置，用精确跳转。很多播放器的做法是两者结合：先显示关键帧的画面让用户有反馈，同时在后台解码到准确的位置。

### 4.3 声音

跳转后声音也要从对应的位置开始：音频的包同样从关键帧附近开始读（AAC 的每一帧都可以独立解码），旧的声音被 `SDL_ClearAudioStream` 扔掉，新的声音推进去后，音频时钟重新生效，画面再跟着它走。

## 5. 为什么要冲刷解码器

如果跳转时不冲刷视频解码器（`NOFLUSH=1`）：

```bash
SEEK_AT=2 SEEK_TO=7 NOFLUSH=1 MUTE=1 ./player5 bbb-10s.mp4
```

```
第 2.00 秒（画面 1.958 秒）：跳到 7.0 秒（关键帧）
  跳转后 16 毫秒显示第一帧：2.167 秒（目标 7.0 秒），之前丢掉了 0 帧
  接着显示：2.250 秒
  接着显示：5.458 秒
  接着显示：5.500 秒
```

跳转之后，先显示了 **2.167 秒和 2.250 秒的两帧旧画面**，然后才是 5.458 秒。第 3 章讲过，解码器内部会积压几帧（B 帧的重排序、多线程），跳转时这些旧帧还在解码器里。不冲刷的话，它们在新的包送进去之后被吐出来，而且被标上了新的序号，主线程识别不出来，就显示了，用户会看到画面先往回闪一下再跳过去。

精确跳转时，这两帧的 PTS 早于目标，碰巧被「丢掉目标之前的帧」这一步过滤了（丢了 39 帧而不是 37 帧），看不出问题。但这只是碰巧：如果是往回跳，旧帧的 PTS 比目标大，就过滤不掉了。而且解码器里留着的旧参考帧，还可能被新的帧错误地引用。所以跳转时一定要冲刷解码器。

## 6. 写程序时踩的两个坑

这个程序是改了两次才正常工作的，两次都是很典型的错误。

**跳转还没执行，就显示了旧帧。**第一版中，主线程发出跳转请求后，立刻把「下一帧」当作跳转后的第一帧显示。但读文件线程可能还没来得及执行跳转，帧队列里排着的仍然是旧帧（2.000 秒），于是显示了一帧旧画面；更糟的是，主线程以为跳转已经完成，没有更新自己的序号，之后所有新序号的帧都被当成旧帧丢掉，画面就永远停住了。修正的办法是：跳转请求还没被执行时，主线程什么也不显示，先等着。多线程程序中，「我发出了请求」和「请求已经被执行」是两件事。

**冲刷的顺序错了，最后两帧丢失。**第一版的视频解码线程在取完最后一个包后，发现队列已经读完，就调用了 `avcodec_flush_buffers` 准备等待跳转，结果把解码器里还没吐出来的最后 2 帧（9.917 秒、9.958 秒）也一起扔掉了，播放永远停在 9.875 秒。正确的顺序是：先送入 `NULL` 冲刷，把剩下的帧全部取出来，再清空解码器。`avcodec_send_packet(NULL)` 是「把剩下的都给我」，`avcodec_flush_buffers` 是「剩下的都不要了」，两者的意思完全相反。

## 7. 小结

- 以音频为主时钟时，暂停只要停住声卡，整条流水线会因为背压自然停住。
- 跳转：读文件线程定位到目标之前的关键帧、清空包队列；解码线程冲刷解码器；音频线程清空 SDL 的队列；主线程丢掉旧帧。
- 用序号区分跳转前后的数据：包、帧、音频时钟都带序号，对不上的一律作废。
- 按关键帧跳转快，但位置不准；精确跳转要解码并丢掉关键帧和目标之间的帧，慢一些。GOP 越长，精确跳转越慢。
- 跳转时必须用 `avcodec_flush_buffers` 冲刷解码器；播放到结尾时则要先送 `NULL` 取出最后几帧。

下一篇换一种解码方式：用 GPU 中的硬件解码器。

## 练习

1. 精确跳转到 3.3 秒和 3.4 秒，分别显示的是哪一帧？丢掉了几帧？为什么 3.3 秒一帧都没丢？
2. 暂停时，为什么音频解码线程、视频解码线程、读文件线程都会停下来？如果读文件线程在暂停期间继续读，会怎样？

<details>
<summary>点击查看答案</summary>

1. 跳到 3.3 秒：显示 3.292 秒的帧，丢掉 0 帧，用时 25 毫秒；跳到 3.4 秒：显示 3.417 秒的帧，丢掉 3 帧，用时 33 毫秒。3.292 秒正好是一个关键帧（第 79 帧），和目标 3.3 秒只差 8 毫秒，不到半帧（20.8 毫秒），程序就直接显示它。3.4 秒之前最近的关键帧也是 3.292 秒，要丢掉 3.292、3.333、3.375 三帧，显示 3.417 秒（3.375 比 3.4 早了 25 毫秒，超过半帧，被丢掉了）。可见精确跳转的代价完全取决于目标离前一个关键帧有多远。

2. 暂停时声卡不再取数据，SDL 队列的长度保持在 0.2 秒以上，音频解码线程在「队列超过 0.2 秒就等」处停下，不再从音频包队列取包；主线程不再显示帧，帧队列满了，视频解码线程在「帧队列满了就等」处停下，不再取视频包；两个包队列不再变短，视频包队列超过 1 秒后，读文件线程也停下了。这就是第 3 篇讲的背压：最下游停了，上游一级一级停下来。如果读文件线程不受限制地继续读，暂停的时间越长，内存中积压的包越多，暂停一小时就可能把整部电影读进内存；网络视频还会白白消耗流量。所以「队列满了就等」不只是为了播放流畅，也是暂停能够正确工作的前提。

</details>
