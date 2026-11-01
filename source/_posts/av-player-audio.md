---
title: 音频播放
date: 2026-11-02 10:00:00
updated: 2026-11-02 10:00:00
categories:
  - 音视频开发
  - 播放器
tags:
  - 音视频
  - SDL
  - 播放器
  - 音频
  - C
order: 2
description: 视频是程序「推」给屏幕的，音频则是设备「拉」的：声卡按自己的节奏，每 21.3 毫秒向程序要 1024 个采样，程序必须按时交出来。用 SDL3 的音频回调加一个环形缓冲区播放测试视频的声音：解码在主线程，回调在 SDL 的音频线程，两者之间用互斥锁保护。用 disk 驱动把播放出的声音写成文件，除了开头 21.3 毫秒的静音，和解码结果逐字节相同。再让解码每帧多等 20 毫秒，只比设备慢一点点，10 秒的声音中就有 80 次断档、共 1.69 秒是补上的静音，波形图上全是缺口。推模式更省事，但不加控制时，10 秒的声音 5 毫秒就全塞进了队列。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解音频设备「拉数据」的工作方式和回调函数；会用 SDL3 打开音频设备，用回调和环形缓冲区播放解码出的 PCM；理解欠载（underrun）为什么会造成卡顿和爆音；知道设备缓冲大小与延迟的关系；了解推模式。
>
> **前置知识**：上一篇《最简单的视频播放器》；第 1 章《音频与 PCM》；第 3 章《像素格式转换与重采样》中的 libswresample；多线程与互斥锁（Linux 基础专栏《进程与线程》）。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，SDL 3.4。

## 1. 设备在拉数据

上一篇播放视频时，是程序决定什么时候显示下一帧：时间到了，就把画面「推」给屏幕；晚了一点，画面多停留一会儿，眼睛几乎察觉不到。

音频完全不同。声卡里有一个按 48000 Hz 走的时钟，每过一个采样周期就要输出一个采样，一刻也不能停。它不会等程序，而是**按自己的节奏向程序要数据**：缓冲区快空了，就请求下一批。程序必须在这一批播完之前把数据准备好，否则声卡没有东西可放，只能输出静音（或者重复上一段数据），耳朵立刻就能听出来：声音断了一下，或者「啪」的一声。这叫**欠载**（underrun）。

所以播放音频的核心问题是：**解码的速度必须跟上设备消耗的速度，而且要提前准备好一些数据作为缓冲**。

SDL 中，设备要数据时会调用程序提供的**回调函数**。这个函数不是在主线程中调用的，而是在 SDL 专门的音频线程中，所以回调和主线程（解码）之间共享的数据要加锁。

<figure class="diagram">
<svg viewBox="0 0 680 200" role="img" aria-label="音频播放的结构：主线程解码 AAC，用 libswresample 转成 16 位交错 PCM，写入环形缓冲区；SDL 的音频线程每 21.3 毫秒调用一次回调，从环形缓冲区取出 1024 个采样交给设备；缓冲区空了时只能补静音">
  <defs>
    <marker id="au-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <rect x="5" y="20" width="250" height="110" rx="8" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="15" y="40" style="fill:var(--fg)">主线程</text>
    <rect x="20" y="52" width="100" height="34" rx="5" style="fill:var(--accent-soft);stroke:var(--border-2)"/><text x="30" y="74" style="fill:var(--fg)">AAC 解码</text>
    <rect x="140" y="52" width="100" height="34" rx="5" style="fill:var(--accent-soft);stroke:var(--border-2)"/><text x="150" y="74" style="fill:var(--fg)">swr → s16</text>
    <text x="20" y="112" style="fill:var(--muted);font-size:10px">缓冲区满了就等一等</text>
    <g transform="translate(290,40)">
      <circle cx="60" cy="40" r="40" style="fill:none;stroke:var(--border-2);stroke-width:14"/>
      <path d="M60,0 A40,40 0 0 1 100,40" fill="none" style="stroke:var(--accent);stroke-width:14;opacity:.75"/>
      <path d="M100,40 A40,40 0 0 1 60,80" fill="none" style="stroke:var(--accent);stroke-width:14;opacity:.45"/>
      <text x="22" y="110" style="fill:var(--fg)">环形缓冲区</text>
      <text x="8" y="126" style="fill:var(--muted);font-size:10px">互斥锁保护，1 秒</text>
    </g>
    <rect x="430" y="20" width="245" height="110" rx="8" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="440" y="40" style="fill:var(--fg)">SDL 的音频线程</text>
    <rect x="445" y="52" width="100" height="34" rx="5" style="fill:var(--mark);stroke:var(--border-2)"/><text x="455" y="74" style="fill:var(--fg)">回调函数</text>
    <rect x="565" y="52" width="95" height="34" rx="5" style="fill:var(--panel);stroke:var(--border-2)"/><text x="580" y="74" style="fill:var(--fg)">声卡</text>
    <text x="445" y="112" style="fill:var(--muted);font-size:10px">每 21.3 毫秒要 1024 个采样</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#au-arrow)">
      <line x1="120" y1="69" x2="138" y2="69"/>
      <path d="M240,69 L300,69"/>
      <path d="M400,69 L443,69"/>
      <line x1="545" y1="69" x2="563" y2="69"/>
    </g>
    <text x="5" y="165" style="fill:var(--muted);font-size:11px">回调时缓冲区中的数据不够：只能补静音交给设备 → 欠载，听到断续、爆音</text>
    <text x="5" y="185" style="fill:var(--muted);font-size:11px">缓冲区越大越不容易欠载，但声音从解码到播放出来的延迟也越大</text>
  </g>
</svg>
</figure>

## 2. 程序

```c
// player_audio.c：播放音频。设备通过回调函数向程序要数据，程序在主线程中解码，放进一个缓冲区
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>

// 一个简单的环形缓冲区：解码线程往里写，音频回调从里面读
#define BUF_SIZE (48000 * 4)                         // 1 秒的 48 kHz 16 位立体声
static Uint8 ring[BUF_SIZE];
static int rpos = 0, wpos = 0, filled = 0;
static SDL_Mutex *lock;

static long callbacks = 0, underrun_bytes = 0, underruns = 0, requested = 0;
static SDL_AtomicInt decoding_done;              // 解码结束后再缺数据，就是正常的播放完毕
static Uint64 last_cb = 0, min_gap = ~0ull, max_gap = 0;

// SDL 在自己的音频线程中调用：设备还需要 additional_amount 字节
static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    (void)userdata; (void)total_amount;
    Uint64 now = SDL_GetTicksNS();
    if (last_cb) {
        Uint64 gap = now - last_cb;
        if (gap < min_gap) min_gap = gap;
        if (gap > max_gap) max_gap = gap;
    }
    last_cb = now;
    callbacks++;
    requested += additional_amount;

    Uint8 tmp[16384];
    while (additional_amount > 0) {
        int n = SDL_min(additional_amount, (int)sizeof tmp);
        SDL_LockMutex(lock);
        int got = SDL_min(n, filled);
        for (int i = 0; i < got; i++) { tmp[i] = ring[rpos]; rpos = (rpos + 1) % BUF_SIZE; }
        filled -= got;
        SDL_UnlockMutex(lock);
        if (got < n) {                               // 数据不够：只能用静音补上，这就是「卡顿」
            memset(tmp + got, 0, n - got);
            if (!SDL_GetAtomicInt(&decoding_done)) { underrun_bytes += n - got; underruns++; }
        }
        SDL_PutAudioStreamData(stream, tmp, n);
        additional_amount -= n;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 文件 [每帧额外延时毫秒]\n", argv[0]); return 1; }
    int slow_ms = argc > 2 ? atoi(argv[2]) : 0;      // 模拟解码太慢

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    const AVCodec *codec = NULL;
    int as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[as]->codecpar);
    dec->pkt_timebase = fmt->streams[as]->time_base;
    avcodec_open2(dec, codec, NULL);

    // 解码器输出 fltp（平面浮点），SDL 要交错格式：用 libswresample 转成 16 位交错（第 3 章第 5 篇）
    SwrContext *swr = NULL;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, 48000, &dec->ch_layout, dec->sample_fmt, dec->sample_rate, 0, NULL);
    swr_init(swr);

    SDL_Init(SDL_INIT_AUDIO);
    lock = SDL_CreateMutex();
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, 48000};
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
    if (!stream) { fprintf(stderr, "打开音频设备失败：%s\n", SDL_GetError()); return 1; }
    if (getenv("MUTE")) SDL_SetAudioStreamGain(stream, 0.0f);        // 只想测时序、不想出声时
    SDL_AudioSpec dev;
    int dev_frames;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &dev, &dev_frames);
    printf("音频驱动 %s，设备格式 %d Hz、%d 声道，每次处理 %d 个采样（%.1f 毫秒）\n", SDL_GetCurrentAudioDriver(),
           dev.freq, dev.channels, dev_frames, dev_frames * 1000.0 / dev.freq);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    Uint8 out[8192 * 4];
    int started = 0, eof = 0;
    Uint64 t0 = SDL_GetTicksNS();
    while (!eof) {
        if (av_read_frame(fmt, pkt) < 0) { avcodec_send_packet(dec, NULL); eof = 1; }
        else if (pkt->stream_index != as) { av_packet_unref(pkt); continue; }
        else { avcodec_send_packet(dec, pkt); av_packet_unref(pkt); }
        while (avcodec_receive_frame(dec, frame) >= 0) {
            uint8_t *dst[1] = {out};
            int n = swr_convert(swr, dst, 8192, (const uint8_t **)frame->extended_data, frame->nb_samples);
            int bytes = n * 4;
            // 缓冲区满了就等一等，等回调把数据取走
            for (;;) {
                SDL_LockMutex(lock);
                int room = BUF_SIZE - filled;
                if (room >= bytes) {
                    for (int i = 0; i < bytes; i++) { ring[wpos] = out[i]; wpos = (wpos + 1) % BUF_SIZE; }
                    filled += bytes;
                    SDL_UnlockMutex(lock);
                    break;
                }
                SDL_UnlockMutex(lock);
                SDL_Delay(5);
            }
            if (!started && filled >= BUF_SIZE / 10) {   // 先攒 0.1 秒再开始播放
                SDL_ResumeAudioStreamDevice(stream);
                started = 1;
            }
            av_frame_unref(frame);
            if (slow_ms) SDL_Delay(slow_ms);
        }
    }
    SDL_SetAtomicInt(&decoding_done, 1);
    // 等缓冲区中剩下的数据播放完
    while (1) {
        SDL_LockMutex(lock);
        int left = filled;
        SDL_UnlockMutex(lock);
        if (left == 0) break;
        SDL_Delay(10);
    }
    SDL_Delay(100);
    double total = (SDL_GetTicksNS() - t0) / 1e9;
    printf("用时 %.2f 秒；回调 %ld 次，间隔 %.1f～%.1f 毫秒，共要了 %ld 字节；缺数据 %ld 次，%ld 字节（%.3f 秒，用静音补上）\n",
           total, callbacks, min_gap / 1e6, max_gap / 1e6, requested, underruns, underrun_bytes, underrun_bytes / 4.0 / 48000);

    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    return 0;
}
```

- **`SDL_OpenAudioDeviceStream`** 打开默认的播放设备，同时创建一个音频流（`SDL_AudioStream`）。`spec` 是我们给出的数据格式：16 位、立体声、48000 Hz。如果设备的格式不同，音频流会自动转换。设备开始时是暂停的，`SDL_ResumeAudioStreamDevice` 开始播放。
- **回调函数**的参数 `additional_amount` 是这一次至少需要多少字节。回调从环形缓冲区取出数据，用 `SDL_PutAudioStreamData` 交给音频流；数据不够时补 0（静音），并记下来。
- **环形缓冲区**：一块 1 秒大小的内存，写的位置和读的位置各自往前走，走到末尾绕回开头。主线程写、回调读，`filled` 记录里面有多少数据，三个变量都在锁的保护下修改。
- **起播前先攒 0.1 秒**：设备一启动就会要数据，如果这时一点数据都没有，开头就是一次欠载。
- **libswresample**：AAC 解码器输出 `fltp`（平面浮点），SDL 只接受交错格式，用第 3 章第 5 篇的方法转成 16 位交错。
- **`MUTE` 环境变量**：设置后用 `SDL_SetAudioStreamGain` 把音量设成 0。下面测真实设备的时序时用它，不让扬声器出声。

编译、运行：

```bash
FF=$(brew --prefix ffmpeg); S3=$(brew --prefix sdl3)
clang -O2 player_audio.c -o player_audio -I$FF/include -I$S3/include -L$FF/lib -L$S3/lib \
      -lavformat -lavcodec -lavutil -lswresample -lSDL3
./player_audio bbb-10s.mp4
```

## 3. 设备的节奏

用真实的声卡（Core Audio），静音运行：

```bash
MUTE=1 ./player_audio bbb-10s.mp4
```

```
音频驱动 coreaudio，设备格式 48000 Hz、2 声道，每次处理 1024 个采样（21.3 毫秒）
用时 10.13 秒；回调 474 次，间隔 21.2～21.5 毫秒，共要了 1941504 字节；缺数据 0 次，0 字节（0.000 秒，用静音补上）
```

- 设备每次处理 **1024 个采样**，正好 21.3 毫秒（巧合的是，和 AAC 的一帧一样大）。回调的间隔非常稳定，在 21.2 到 21.5 毫秒之间，这就是声卡的时钟。
- 回调了 474 次，一共要了 1941504 字节，比 10 秒的数据（1920000 字节）多一点：播放完以后设备还在继续要，程序给的是静音。
- **没有欠载**：解码 AAC 非常快（第 3 章说过，解码整个 10 秒的音频只要几毫秒），缓冲区总是满的。

### 3.1 播放出的数据对不对

用 SDL 的 `disk` 驱动代替声卡，它按照和声卡一样的节奏调用回调，但把收到的数据写进文件：

```bash
SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=disk0.raw ./player_audio bbb-10s.mp4
```

```
ERROR: You are using the SDL disk i/o audio driver!
ERROR:  Writing to file [disk0.raw], format=S16LE channels=2 freq=48000.
```

（这两行「ERROR」只是 SDL 的提醒，不是错误。）把 `disk0.raw` 和第 3 章用命令行解码出的 16 位 PCM 比较：文件开头有 4096 字节（1024 个采样，21.3 毫秒）的静音，那是程序调用 `SDL_ResumeAudioStreamDevice` 之前设备已经在输出的一段；从第 4096 字节开始，1920000 字节和解码结果**逐字节相同**。从解码到声卡，数据一个字节都没有变。

## 4. 欠载

现在模拟解码太慢的情况：每解码一帧，额外等一会儿。AAC 的一帧是 21.3 毫秒的声音，如果处理一帧花的时间超过 21.3 毫秒，生产就跟不上消费：

```bash
MUTE=1 ./player_audio bbb-10s.mp4 20
MUTE=1 ./player_audio bbb-10s.mp4 25
```

| 每帧额外等待 | 用时 | 欠载次数 | 补的静音 |
|---|---|---|---|
| 0 | 10.13 秒 | 0 | 0 |
| 20 毫秒 | 11.86 秒 | 78 | 1.648 秒 |
| 25 毫秒 | 15.17 秒 | 232 | 4.933 秒 |

只等 20 毫秒，看起来比 21.3 毫秒还少，但 `SDL_Delay` 实际睡眠的时间会稍长一点，再加上解码本身的时间，每帧花的时间刚好超过了 21.3 毫秒。生产只比消费慢了一点点，缓冲区里那 0.1 秒的余量很快就耗光了，之后就是不断地断档：10 秒的声音中断了 78 次，一共插进了 1.65 秒的静音，播放时间也拉长到了 11.9 秒。

用 disk 驱动把「等 20 毫秒」时播放出的声音录下来，和正常的对比：

<figure>
<img src="/img/av/audio-underrun.png" alt="两段音频的波形：上面是正常播放，连续；下面是解码太慢时，波形被切成一段一段，中间有很多空白" width="640">
<figcaption>上：正常播放（10 秒）；下：每帧多等 20 毫秒（11.9 秒），一个个缺口都是补上的静音</figcaption>
</figure>

听起来就是声音一卡一卡的。每一个缺口的边缘，波形从某个值突然跳到 0，又从 0 突然跳回去，这种突变就是「啪啪」的爆音。

实际的播放器中，解码 AAC 这样的音频不会这么慢，欠载通常是因为别的原因：解码线程被视频解码抢走了 CPU；读网络数据时卡住了；主线程在做别的耗时的事情，没顾上往缓冲区里送数据。所以下一篇要把读文件、视频解码、音频解码放进不同的线程，互不阻塞。

## 5. 缓冲与延迟

设备每次处理多少个采样，可以通过 SDL 的提示变量修改：

```bash
SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256 MUTE=1 ./player_audio bbb-10s.mp4
```

```
音频驱动 coreaudio，设备格式 48000 Hz、2 声道，每次处理 256 个采样（5.3 毫秒）
用时 10.11 秒；回调 1896 次，间隔 0.0～10.8 毫秒，……
```

回调次数是原来的 4 倍，平均每 5.3 毫秒一次，但间隔不太均匀（0 到 10.8 毫秒）：系统有时会把两次请求合在一起处理。

设备缓冲越小，声音从送进去到放出来的**延迟**越小，但留给程序准备数据的时间也越少，越容易欠载。这是一个取舍：

- 音乐、视频播放：延迟几十毫秒到几百毫秒都无所谓（下一篇会讲，视频同步时会把这段延迟算进去），缓冲可以大一些，求稳。
- 游戏音效、乐器、视频通话：需要低延迟，缓冲要尽量小，对程序的实时性要求高得多。

环形缓冲区的大小（这里是 1 秒）也是同样的道理：它决定了程序可以「卡住」多久而不出问题。

## 6. 推模式

SDL3 还提供了一种更简单的用法：打开设备时不提供回调函数，程序自己调用 `SDL_PutAudioStreamData` 往音频流里放数据，SDL 内部有一个队列，设备从队列里取。这叫**推模式**，程序不需要自己写环形缓冲区和锁。

但推模式要自己控制节奏。不加控制，把解码出的数据一股脑放进去（练习 1 的程序）：

```
解码全部放进去用了 0.005 秒，此时队列中有 1920000 字节（10.00 秒）
```

5 毫秒就把 10 秒的声音全放进了队列。10 秒还好，一部两小时的电影就是 1.3 GB 的 PCM 全堆在内存里。所以推模式下也要检查 `SDL_GetAudioStreamQueued`，队列中的数据超过一定量（比如 0.2 秒）就先不解码，等一等。另外，队列中的数据越多，现在解码的声音要越久才能播放出来，这对音视频同步也很重要，第 4 篇会用到这个数字。

## 7. 小结

- 音频设备按自己的时钟拉数据：Core Audio 默认每 21.3 毫秒要 1024 个采样；程序必须按时交出来。
- SDL3：`SDL_OpenAudioDeviceStream` 打开设备，回调函数在 SDL 的音频线程中被调用，用 `SDL_PutAudioStreamData` 交出数据。
- 解码和回调在不同线程，共享的缓冲区要加锁；起播前先攒一些数据。
- 生产慢于消费就会欠载：设备只能输出静音，听到断续和爆音。生产只要慢一点点，缺口就会持续出现。
- 设备缓冲越小，延迟越低，越容易欠载。
- 推模式不用写回调，但要自己控制队列的长度。

下一篇用多个线程和队列，把读文件、视频解码、音频解码、显示分开。

## 练习

1. 用推模式改写程序：不提供回调，解码出的数据直接用 `SDL_PutAudioStreamData` 放进音频流。不加任何控制，解码完的时候队列中有多少数据？怎样限制队列的长度？
2. 程序起播前先攒了 0.1 秒的数据。为什么在「每帧额外等 20 毫秒」的实验中，这 0.1 秒并没有避免欠载？要多大的缓冲才能完整地播放这 10 秒而不欠载？

<details>
<summary>点击查看答案</summary>

1. 去掉回调、环形缓冲区和锁，解码循环中直接放数据：

   ```c
   SDL_AudioStream *s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
   SDL_ResumeAudioStreamDevice(s);
   ……
   int n = swr_convert(swr, dst, 8192, (const uint8_t **)f->extended_data, f->nb_samples);
   SDL_PutAudioStreamData(s, out, n * 4);
   ```

   实测解码全部放进去只用了 0.005 秒，队列中有 1920000 字节，即整整 10 秒的声音。限制长度的办法是每放一次就检查队列：

   ```c
   while (SDL_GetAudioStreamQueued(s) > 48000 * 4 / 5)   // 超过 0.2 秒就等一等
       SDL_Delay(5);
   ```

   这样内存中最多只有 0.2 秒的数据，效果和回调加环形缓冲区一样，代码却短得多。SDL3 的推模式正是为了简化这种常见的用法；回调模式则适合需要精确控制每一批数据的场合，比如合成器、低延迟的音效。

2. 欠载的本质是**平均速度**不够：生产每帧慢一点，缺口就一点点累积，0.1 秒的余量很快就会被耗光，之后每一帧都会缺数据。缓冲只能应付**短时间的波动**（比如偶尔有一帧解码慢了），不能弥补持续的速度差。要完整播放这 10 秒不欠载，缓冲要大到能装下整个过程中缺少的数据，也就是实测的 1.65 秒以上，而且要在播放之前就攒好，开头就要等将近 2 秒。显然这不是办法：正确的做法是让生产的速度跟上来，比如把解码放到单独的线程中，不让别的事情拖慢它。

</details>
