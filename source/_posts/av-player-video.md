---
title: 最简单的视频播放器
date: 2026-11-02 09:00:00
updated: 2026-11-02 09:00:00
categories:
  - 音视频开发
  - 播放器
tags:
  - 音视频
  - FFmpeg
  - SDL
  - 播放器
  - C
order: 1
description: 第 3 章把画面解码出来存成了文件，这一章要把它放到屏幕上。用 SDL3 创建窗口、渲染器和 YUV 纹理，解码出的帧不用转成 RGB，三个平面连同 linesize 直接上传，由渲染器完成颜色转换。最朴素的做法是每帧固定等 1/24 秒，10 秒的视频播了 13.1 秒，因为解码和渲染本身也要时间；改成按每一帧的 PTS 安排显示时刻，播放时间是 9.97 秒，最晚的一帧只迟到 9 毫秒。纹理不指定色彩空间时 SDL 默认按 BT.601 转换，截图和 FFmpeg 的结果只有 38.8 dB；指定 BT.709 后是 50 dB。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 SDL3 创建窗口、渲染器和纹理，把解码出的 YUV 帧显示出来；理解为什么播放器要按 PTS 安排每一帧的显示时间，而不是固定延时；会处理窗口事件；知道 SDL 中的色彩空间设置。
>
> **前置知识**：第 3 章，尤其是《解封装与解码》；第 1 章《像素与 YUV》中的 BT.709。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，SDL 3.4。

前三章所有的结果都是文件：YUV、PCM、H.264、MP4。这一章写一个真正的播放器：画面显示在窗口里，声音从扬声器放出来，两者对得上，能暂停、能拖动进度条。最后得到的是一个简化版的 `ffplay`，它的结构和 FFmpeg 自带的 `ffplay.c` 一样，只是去掉了大部分细节。

本章分六步：

1. 只播放视频，按时间显示每一帧（本篇）；
2. 只播放音频，理解音频设备怎样要数据；
3. 用多个线程和队列，把读文件、解码、显示分开；
4. 音视频同步；
5. 暂停与跳转；
6. 硬件解码。

## 1. SDL

FFmpeg 只负责解码，不负责显示和播放。把画面画到窗口里、把声音送到扬声器，要用操作系统的接口：macOS 上是 Metal 和 Core Audio，Windows 上是 Direct3D 和 WASAPI，Linux 上是 OpenGL/Vulkan 和 ALSA/PulseAudio。**SDL**（Simple DirectMedia Layer）把这些包装成同一套 C 接口，`ffplay` 用的就是它，很多游戏也用它。

SDL 目前的主版本是 **SDL3**（2025 年发布），本章使用它。网上的资料和雷霄骅的示例大多用 SDL2（或者更早的 SDL 1.2），两者的思路相同，函数名有一些变化，见第 5 节的对照表。`ffplay` 目前仍然用 SDL2 的接口：Homebrew 安装的 `ffplay` 链接的是 `sdl2-compat`，一个把 SDL2 的接口转给 SDL3 的兼容层。

安装：macOS 上 `brew install sdl3`；较新的发行版（如 Debian 13、Ubuntu 25.04）有 `libsdl3-dev`，更早的版本需要从源码编译；Windows 从 [GitHub](https://github.com/libsdl-org/SDL/releases) 下载开发包。

显示一帧画面要用到 SDL 的三个对象：

<figure class="diagram">
<svg viewBox="0 0 680 160" role="img" aria-label="显示一帧画面：解码得到的 YUV 帧通过 SDL_UpdateYUVTexture 上传到纹理，SDL_RenderTexture 把纹理画到渲染器的后台缓冲上，SDL_RenderPresent 把后台缓冲显示到窗口中">
  <defs>
    <marker id="sdl-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="40" width="120" height="50" rx="6" style="fill:var(--panel-2)"/>
      <rect x="180" y="40" width="130" height="50" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="365" y="40" width="130" height="50" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="550" y="40" width="125" height="50" rx="6" style="fill:var(--mark)"/>
    </g>
    <text x="16" y="62" style="fill:var(--fg)">AVFrame</text><text x="16" y="80" style="fill:var(--muted);font-size:10px">YUV420P，内存中</text>
    <text x="190" y="62" style="fill:var(--fg)">SDL_Texture</text><text x="190" y="80" style="fill:var(--muted);font-size:10px">纹理，通常在显存中</text>
    <text x="375" y="62" style="fill:var(--fg)">SDL_Renderer</text><text x="375" y="80" style="fill:var(--muted);font-size:10px">后台缓冲</text>
    <text x="560" y="62" style="fill:var(--fg)">SDL_Window</text><text x="560" y="80" style="fill:var(--muted);font-size:10px">屏幕上的窗口</text>
    <g style="stroke:var(--muted)" marker-end="url(#sdl-arrow)"><line x1="125" y1="65" x2="178" y2="65"/><line x1="310" y1="65" x2="363" y2="65"/><line x1="495" y1="65" x2="548" y2="65"/></g>
    <text x="120" y="30" style="fill:var(--muted);font-size:10px">UpdateYUVTexture</text>
    <text x="318" y="30" style="fill:var(--muted);font-size:10px">RenderTexture</text>
    <text x="490" y="30" style="fill:var(--muted);font-size:10px">RenderPresent</text>
    <text x="180" y="118" style="fill:var(--muted);font-size:11px">YUV → RGB、缩放到窗口大小：都由渲染器完成（macOS 上用 Metal，在 GPU 上做）</text>
    <text x="180" y="138" style="fill:var(--muted);font-size:11px">先画在看不见的后台缓冲上，画完一次性显示，避免画到一半被看到（双缓冲）</text>
  </g>
</svg>
</figure>

- **窗口**（`SDL_Window`）：屏幕上的一个窗口。
- **渲染器**（`SDL_Renderer`）：负责往窗口上画东西。macOS 上默认用 Metal，在 GPU 上完成。
- **纹理**（`SDL_Texture`）：一张图像，通常存放在显存中。纹理可以直接是 **YUV 格式**，第 1 章 YUV 转 RGB 的工作就交给 GPU 去做了，CPU 不需要像第 3 章那样调用 libswscale。

## 2. 程序

```c
// player1.c：最简单的视频播放器——FFmpeg 解码，SDL3 显示
#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 文件 [naive|pts] [截图的帧号]\n", argv[0]); return 1; }
    int use_pts = !(argc > 2 && SDL_strcmp(argv[2], "naive") == 0);
    int shot = argc > 3 ? atoi(argv[3]) : -1;

    // ---------- FFmpeg：打开文件，准备视频解码器（和第 3 章第 2 篇相同） ----------
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    const AVCodec *codec = NULL;
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    AVStream *st = fmt->streams[vs];
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->pkt_timebase = st->time_base;
    avcodec_open2(dec, codec, NULL);

    // ---------- SDL：窗口、渲染器、纹理 ----------
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_CreateWindowAndRenderer("player1", dec->width, dec->height, SDL_WINDOW_RESIZABLE, &win, &ren);
    // 纹理直接用 YUV420P（SDL 中叫 IYUV），转 RGB 的工作交给显卡（或者渲染器）去做。
    // 色彩空间在创建时指定：第 1 章说过，高清视频用 BT.709、有限范围
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_IYUV);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, dec->width);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, dec->height);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_LIMITED);
    SDL_Texture *tex = SDL_CreateTextureWithProperties(ren, props);
    SDL_DestroyProperties(props);
    printf("视频驱动 %s，渲染器 %s，%dx%d，模式 %s\n", SDL_GetCurrentVideoDriver(), SDL_GetRendererName(ren),
           dec->width, dec->height, use_pts ? "按 PTS" : "固定延时");

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    double frame_dur = av_q2d(av_inv_q(st->avg_frame_rate));       // 每帧 1/24 秒
    Uint64 start = SDL_GetTicksNS();
    int quit = 0, frames = 0, eof = 0;
    double max_late = 0;

    while (!quit) {
        // 处理窗口事件：关闭窗口或按 Esc 退出
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) quit = 1;

        // 解码出一帧（读包、送包，直到取出一帧或文件结束）
        int got = 0;
        while (!got && !quit) {
            int ret = avcodec_receive_frame(dec, frame);
            if (ret >= 0) { got = 1; break; }
            if (ret == AVERROR_EOF) { quit = 1; break; }
            if (eof) continue;
            if (av_read_frame(fmt, pkt) < 0) { avcodec_send_packet(dec, NULL); eof = 1; continue; }
            if (pkt->stream_index == vs) avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
        }
        if (!got) break;

        // 等到这一帧该显示的时刻
        double now = (SDL_GetTicksNS() - start) / 1e9;
        if (use_pts) {
            double pts = frame->pts * av_q2d(st->time_base);        // 这一帧应该在第几秒显示
            if (pts > now) SDL_DelayNS((Uint64)((pts - now) * 1e9));
            else if (now - pts > max_late) max_late = now - pts;
        } else {
            SDL_DelayNS((Uint64)(frame_dur * 1e9));                  // 不管别的，每帧都等 1/24 秒
        }

        // 把 YUV 的三个平面（连同各自的 linesize）上传到纹理，再画到窗口上
        SDL_UpdateYUVTexture(tex, NULL, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1],
                             frame->data[2], frame->linesize[2]);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);                     // 缩放到整个窗口
        if (frames == shot) {                                        // 把渲染结果读回来，存成 BMP
            SDL_Surface *s = SDL_RenderReadPixels(ren, NULL);
            SDL_SaveBMP(s, "shot.bmp");
            SDL_DestroySurface(s);
        }
        SDL_RenderPresent(ren);
        frames++;
        av_frame_unref(frame);
    }
    double total = (SDL_GetTicksNS() - start) / 1e9;
    printf("播放了 %d 帧，用时 %.3f 秒（视频本身 %.3f 秒）", frames, total, frames * frame_dur);
    if (use_pts) printf("，最晚的一帧迟到 %.1f 毫秒", max_late * 1000);
    printf("\n");

    av_frame_free(&frame); av_packet_free(&pkt);
    avcodec_free_context(&dec); avformat_close_input(&fmt);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
```

编译：

```bash
FF=$(brew --prefix ffmpeg); S3=$(brew --prefix sdl3)
clang -O2 player1.c -o player1 -I$FF/include -I$S3/include -L$FF/lib -L$S3/lib \
      -lavformat -lavcodec -lavutil -lSDL3
./player1 bbb-10s.mp4
```

要点：

- **纹理是 `SDL_PIXELFORMAT_IYUV`**，就是 I420（YUV420P）。`SDL_UpdateYUVTexture` 分别接收三个平面的指针和 linesize，第 3 章第 2 篇讲的 linesize 在这里直接传进去，不需要先去掉每行的填充。如果解码器输出的是 NV12（硬件解码常见，第 6 篇），就用 `SDL_PIXELFORMAT_NV12` 和 `SDL_UpdateNVTexture`。
- **纹理要指定色彩空间**：BT.709、有限范围。第 4 节看不指定会怎样。
- **事件循环**：`SDL_PollEvent` 取出窗口事件（关闭、按键、改变大小……）。即使什么都不处理，也必须定期调用它，否则操作系统会认为程序「没有响应」，macOS 上会出现转圈的彩球。
- `SDL_RenderTexture` 的目标矩形传 `NULL`，表示拉伸到整个窗口；窗口被拖得很宽时画面会变形，练习 2 改成保持宽高比。

> **关于实验方法**：为了能重复地测量，也为了不在屏幕上弹出窗口，本章的测量都是用 SDL 的 `dummy` 视频驱动加软件渲染器完成的（环境变量 `SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software`）：窗口不显示出来，但渲染照常进行，可以用 `SDL_RenderReadPixels` 把画面读回来存成图片。平时运行时不设这两个变量，就是正常的窗口和 Metal 渲染器。软件渲染器要用 CPU 做 YUV 转 RGB 和缩放，比 Metal 慢得多，下面的「固定延时」实验中它会把问题放大，但问题本身在 Metal 上同样存在。

## 3. 每一帧什么时候显示

视频是 24 帧/秒，每帧应该显示 1/24 秒。最直接的想法是：显示一帧，等 1/24 秒，再显示下一帧。程序的 `naive` 模式就是这样做的：

```bash
./player1 bbb-10s.mp4 naive
./player1 bbb-10s.mp4 pts
```

```
视频驱动 dummy，渲染器 software，1280x720，模式 固定延时
播放了 240 帧，用时 13.085 秒（视频本身 10.000 秒）
视频驱动 dummy，渲染器 software，1280x720，模式 按 PTS
播放了 240 帧，用时 9.966 秒（视频本身 10.000 秒），最晚的一帧迟到 9.1 毫秒
```

**固定延时**播放 10 秒的视频用了 13.1 秒，慢了 31%。原因很简单：每一帧除了等待的 41.7 毫秒，还要花时间解码、上传纹理、渲染，这里平均每帧多花了 12.9 毫秒（主要是软件渲染器做颜色转换和缩放）。换成 Metal 渲染器，这个时间会小得多，但只要大于 0，误差就会一帧帧累积：每帧多 1 毫秒，一小时的电影就慢了 86 秒。

**按 PTS** 的做法是：记下开始播放的时刻，每一帧的 PTS 换算成秒，就是它**应该显示的时刻**；如果还没到，就等到那个时刻，如果已经过了，就马上显示。每一帧的等待时间不是固定的，解码慢的帧（比如 I 帧）等得少，解码快的帧等得多，总的节奏始终和时间对齐：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="两种定时方式的对比：固定延时时，每一帧的处理时间加上固定的等待时间，误差逐帧累积；按 PTS 时，每一帧都对准自己的显示时刻，等待时间随处理时间变化，误差不累积">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(10,0)">
      <line x1="90" y1="30" x2="660" y2="30" style="stroke:var(--border-2)"/>
      <g style="fill:var(--muted);font-size:10px">
        <text x="86" y="22">0</text><text x="200" y="22">1/24</text><text x="318" y="22">2/24</text><text x="436" y="22">3/24</text><text x="554" y="22">4/24 秒</text>
      </g>
      <g style="stroke:var(--fg);stroke-dasharray:2 3"><line x1="90" y1="26" x2="90" y2="180"/><line x1="208" y1="26" x2="208" y2="180"/><line x1="326" y1="26" x2="326" y2="180"/><line x1="444" y1="26" x2="444" y2="180"/><line x1="562" y1="26" x2="562" y2="180"/></g>
      <text x="0" y="72" style="fill:var(--fg)">固定延时</text>
      <g transform="translate(90,55)">
        <rect x="0" y="0" width="36" height="26" style="fill:var(--danger);opacity:.7"/><rect x="36" y="0" width="118" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
        <rect x="154" y="0" width="36" height="26" style="fill:var(--danger);opacity:.7"/><rect x="190" y="0" width="118" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
        <rect x="308" y="0" width="36" height="26" style="fill:var(--danger);opacity:.7"/><rect x="344" y="0" width="118" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
        <rect x="462" y="0" width="36" height="26" style="fill:var(--danger);opacity:.7"/>
        <text x="60" y="18" style="fill:var(--muted);font-size:10px">等 1/24 秒</text>
        <text x="300" y="44" style="fill:var(--danger);font-size:11px">第 3 帧已经晚了 3 × 处理时间</text>
      </g>
      <text x="0" y="142" style="fill:var(--fg)">按 PTS</text>
      <g transform="translate(90,125)">
        <rect x="0" y="0" width="36" height="26" style="fill:var(--accent);opacity:.75"/><rect x="36" y="0" width="82" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
        <rect x="118" y="0" width="50" height="26" style="fill:var(--accent);opacity:.75"/><rect x="168" y="0" width="68" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
        <rect x="236" y="0" width="30" height="26" style="fill:var(--accent);opacity:.75"/><rect x="266" y="0" width="88" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
        <rect x="354" y="0" width="36" height="26" style="fill:var(--accent);opacity:.75"/><rect x="390" y="0" width="82" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
        <text x="40" y="44" style="fill:var(--muted);font-size:11px">等到下一帧的时刻为止，每帧都准时</text>
      </g>
    </g>
  </g>
</svg>
</figure>

（色块是解码和渲染的时间，灰色是等待。示意图没有按比例画。）

播放 240 帧用了 9.966 秒，最后一帧的 PTS 是 9.958 秒，正好对上。「最晚的一帧迟到 9.1 毫秒」：有些帧解码和渲染花的时间比较长（比如数据量大的 I 帧），到它该显示的时候还没准备好，就晚了一点；但下一帧的时刻是固定的，误差不会传下去。

这就是播放器定时的基本原则：**每一帧的显示时刻由时间戳决定，而不是由上一帧的时刻加一个固定值决定**。和第 2 章最后一篇「不要累加取整后的帧时长」是同一个道理。第 4 篇会把「开始播放的时刻」换成音频的时钟，这就是音视频同步。

## 4. 色彩空间

把第 160 帧的渲染结果读回来：

<img src="/img/av/player1-shot.jpg" alt="播放器渲染出的第 160 帧：草地上的兔子和蝴蝶，颜色正常" width="640">

和用 FFmpeg 把同一帧转成 PNG 的结果比较，PSNR 是 **50.0 dB**，颜色是对的。

如果创建纹理时不指定色彩空间，再比较一次：**38.8 dB**。SDL3 的头文件中写着：

```c
SDL_COLORSPACE_YUV_DEFAULT = SDL_COLORSPACE_BT601_LIMITED  /**< The default colorspace for YUV surfaces if no colorspace is specified */
```

YUV 纹理默认按 **BT.601** 转换。这和第 3 章第 5 篇 libswscale 默认用 BT.601 是同一个问题，第 1 章第 5.3 节那种「颜色稍微偏一点」的错误，在播放器这一环又出现了一次。实际的播放器应该根据解码出的帧上的 `colorspace`、`color_range` 字段选择纹理的色彩空间，而不是写死。

## 5. SDL2 与 SDL3

读别的资料时用得上：

| SDL2 | SDL3 |
|---|---|
| `SDL_Init` 成功返回 0 | 成功返回 `true` |
| `SDL_CreateWindow(title, x, y, w, h, flags)` | `SDL_CreateWindow(title, w, h, flags)`，或 `SDL_CreateWindowAndRenderer` |
| `SDL_CreateRenderer(win, -1, flags)` | `SDL_CreateRenderer(win, NULL)`（按名字选择渲染器） |
| `SDL_RenderCopy(ren, tex, src, dst)`，矩形是 `SDL_Rect`（整数） | `SDL_RenderTexture`，矩形是 `SDL_FRect`（浮点） |
| `SDL_Delay(ms)`，`SDL_GetTicks` 返回毫秒 | 另有 `SDL_DelayNS`、`SDL_GetTicksNS`（纳秒） |
| 事件 `SDL_QUIT`、`SDL_KEYDOWN`，`e.key.keysym.sym` | `SDL_EVENT_QUIT`、`SDL_EVENT_KEY_DOWN`，`e.key.key` |
| `SDL_OpenAudioDevice` + 回调函数 | `SDL_OpenAudioDeviceStream` + 音频流（下一篇） |
| 设置 YUV 转换标准：`SDL_SetYUVConversionMode`（全局） | 每个纹理单独指定色彩空间 |

## 6. 小结

- SDL 提供跨平台的窗口、渲染和音频接口；显示一帧：`SDL_UpdateYUVTexture` 上传 → `SDL_RenderTexture` 画到后台缓冲 → `SDL_RenderPresent` 显示。
- 纹理可以直接是 YUV 格式，颜色转换和缩放由渲染器（GPU）完成；上传时直接传入每个平面的 linesize。
- 必须定期处理事件，否则窗口会被认为没有响应。
- 每一帧按 PTS 决定显示时刻，误差不会累积；固定延时会把解码和渲染的时间一帧帧累积下来。
- YUV 纹理默认按 BT.601 转换，高清视频要指定 BT.709。

下一篇只播放音频：音频设备怎样向程序要数据，数据给慢了会发生什么。

## 练习

1. 去掉创建纹理时的色彩空间属性，截图和 FFmpeg 转出的画面比较，PSNR 是多少？为什么？
2. 修改程序，让画面在窗口中保持原来的宽高比，多出来的地方留黑边。用 `SDL_SetWindowSize` 把窗口设成 1280×1000 测试。
3. 怎样做两倍速播放？播放 10 秒的视频需要多久？

<details>
<summary>点击查看答案</summary>

1. 38.8 dB。SDL3 中 YUV 纹理默认的色彩空间是 `SDL_COLORSPACE_YUV_DEFAULT`，即 BT.601 有限范围，而测试视频是 BT.709。第 4 节说过，按错误的标准转换，PSNR 就从 50 dB 降到了 39 dB 左右，这和第 1 章、第 3 章用 BT.601 转换 BT.709 视频时的结果一致。

2. 每次渲染前取得窗口的实际大小，按较小的缩放比例计算目标矩形，居中放置：

   ```c
   int ww, wh;
   SDL_GetCurrentRenderOutputSize(ren, &ww, &wh);
   float scale = SDL_min((float)ww / frame->width, (float)wh / frame->height);
   SDL_FRect dst = {(ww - frame->width * scale) / 2, (wh - frame->height * scale) / 2,
                    frame->width * scale, frame->height * scale};
   SDL_RenderTexture(ren, tex, NULL, &dst);
   ```

   窗口 1280×1000 时，缩放比例是 min(1280/1280, 1000/720) = 1，画面 1280×720 居中，上下各留 140 像素的黑边（`SDL_RenderClear` 清成了黑色）。用 `SDL_GetCurrentRenderOutputSize` 而不是窗口的大小，是因为在 Retina 屏幕上，渲染输出的像素数是窗口「点数」的 2 倍。如果视频的像素不是正方形（`sample_aspect_ratio` 不是 1:1，一些 DV 和电视节目是这样），计算宽高比时还要乘上它。

3. 把每一帧应该显示的时刻减半：`pts = frame->pts * av_q2d(st->time_base) / 2`。实测播放 240 帧用了 4.984 秒。按 PTS 定时，改变播放速度只需要改变时间的换算，不用改别的地方。但这只是视频；有了声音以后，两倍速还要处理音频：直接把采样率翻倍，声音会变尖（音调升高一个八度），实际的播放器会用 `atempo` 之类的滤镜在不改变音调的情况下加速音频。

</details>
