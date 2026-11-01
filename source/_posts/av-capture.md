---
title: 采集：屏幕、摄像头与麦克风
date: 2026-11-03 09:00:00
updated: 2026-11-03 09:00:00
categories:
  - 音视频开发
  - 采集与流媒体
tags:
  - 音视频
  - 采集
  - libavdevice
  - FFmpeg
  - C
order: 1
description: 在 FFmpeg 中，采集设备就是一种特殊的「输入格式」：用 libavdevice 打开 macOS 的 avfoundation，读出来的是未压缩的 rawvideo 包，后面的处理和读文件完全一样。实测 4K 屏幕采集：默认的 yuv420p 不被支持，自动换成 uyvy422，每秒 499 MB 的原始数据，缩放成 720p 编码后只有 0.41 Mbps；换成 nv12 后数据量少四分之一，转换也更快。处理不过来时，设备不排队而是丢帧：每帧多耗 50 毫秒，10 秒里少了 151 帧。再用一个 SDL 窗口在屏幕上画出当前时间，测得从画上屏幕到采集程序拿到这一帧，要 96～103 毫秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 libavdevice 怎样把采集设备当作输入格式；会枚举设备，会用 C 采集屏幕并编码保存；理解采集设备的像素格式、帧率和时间戳；知道处理不过来时采集设备的行为；测量采集本身带来的延迟。
>
> **前置知识**：第 3 章《解封装与解码》《编码》《像素格式转换与重采样》；第 4 章《最简单的视频播放器》中的 SDL 窗口。
>
> **实验环境**：macOS 27.0（Apple M6），Mi Monitor 4K 显示器（3840×2160，按 1920×1080 缩放，60 Hz），Apple Clang 21，FFmpeg 9.0.2，SDL 3.4。

## 1. 采集也是一种输入

前四章的数据都来自文件。直播的第一步是**采集**：从摄像头取画面，从麦克风取声音，或者把屏幕录下来。这些设备各个系统的接口完全不同：macOS 是 AVFoundation，Windows 是 DirectShow、Media Foundation，Linux 是 V4L2（摄像头）、ALSA 或 PulseAudio（声音）、X11 或 KMS（屏幕）。

FFmpeg 的 **libavdevice** 把它们都包装成了**输入格式**（AVInputFormat）。打开一个摄像头，和打开一个 MP4 文件用的是同一个 `avformat_open_input`，只是多指定一个格式名，「文件名」换成设备名：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="采集的数据流：屏幕、摄像头、麦克风经过系统的采集接口 AVFoundation，由 libavdevice 的 avfoundation 输入格式包装，av_read_frame 读出 rawvideo 包，经过 rawvideo 解码器变成 AVFrame，再经过 swscale 缩放、转换像素格式，送给编码器；和读文件相比，只是最前面的输入格式不同">
  <defs>
    <marker id="cp-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="20" width="95" height="110" rx="6" style="fill:var(--panel-2)"/>
      <rect x="130" y="45" width="115" height="60" rx="6" style="fill:var(--mark)"/>
      <rect x="275" y="45" width="100" height="60" rx="6" style="fill:var(--panel-2)"/>
      <rect x="405" y="45" width="80" height="60" rx="6" style="fill:var(--panel-2)"/>
      <rect x="515" y="45" width="70" height="60" rx="6" style="fill:var(--panel-2)"/>
      <rect x="610" y="45" width="65" height="60" rx="6" style="fill:var(--panel-2)"/>
    </g>
    <text x="15" y="45" style="fill:var(--fg)">屏幕</text><text x="15" y="75" style="fill:var(--fg)">摄像头</text><text x="15" y="105" style="fill:var(--fg)">麦克风</text>
    <text x="140" y="70" style="fill:var(--fg)">avfoundation</text><text x="140" y="88" style="fill:var(--muted);font-size:10px">libavdevice</text>
    <text x="285" y="70" style="fill:var(--fg)">rawvideo</text><text x="285" y="88" style="fill:var(--muted);font-size:10px">AVPacket</text>
    <text x="415" y="70" style="fill:var(--fg)">解码器</text><text x="415" y="88" style="fill:var(--muted);font-size:10px">AVFrame</text>
    <text x="525" y="70" style="fill:var(--fg)">swscale</text><text x="525" y="88" style="fill:var(--muted);font-size:10px">缩放</text>
    <text x="620" y="70" style="fill:var(--fg)">编码器</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#cp-arrow)">
      <line x1="100" y1="75" x2="128" y2="75"/><line x1="245" y1="75" x2="273" y2="75"/><line x1="375" y1="75" x2="403" y2="75"/><line x1="485" y1="75" x2="513" y2="75"/><line x1="585" y1="75" x2="608" y2="75"/>
    </g>
    <text x="130" y="135" style="fill:var(--muted);font-size:11px">└ 只有这一格和读文件不同，后面的处理完全一样 ┘</text>
    <text x="5" y="170" style="fill:var(--muted);font-size:11px">Linux 换成 v4l2、alsa、x11grab，Windows 换成 dshow、gdigrab，程序的其余部分不用改</text>
  </g>
</svg>
</figure>

设备没有压缩，读出来的包是 **rawvideo**（一帧未压缩的画面）或 PCM（一段未压缩的声音），编码器的 ID 分别是 `AV_CODEC_ID_RAWVIDEO` 和 `AV_CODEC_ID_PCM_*`。所以第 3 章的整套流程原封不动：读包、「解码」（rawvideo 解码器只是把包里的数据按像素格式填进 AVFrame）、转换、编码、封装或推流。

## 2. 有哪些设备

libavdevice 提供了 `avdevice_list_input_sources` 用来枚举设备，但不是每种设备都实现了它：

```c
// list_dev.c：列出 avfoundation 的采集设备
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <stdio.h>

int main(void) {
    avdevice_register_all();
    const AVInputFormat *ifmt = av_find_input_format("avfoundation");
    AVDeviceInfoList *list = NULL;
    int n = avdevice_list_input_sources(ifmt, NULL, NULL, &list);
    if (n < 0) {
        printf("avdevice_list_input_sources 返回 %d（%s），改用 list_devices 选项：\n", n, av_err2str(n));
        AVDictionary *opts = NULL;
        av_dict_set(&opts, "list_devices", "true", 0);
        AVFormatContext *ctx = NULL;
        avformat_open_input(&ctx, "", ifmt, &opts);   // 设备列表打印在日志里，打开本身会失败
        return 0;
    }
    for (int i = 0; i < n; i++) {
        AVDeviceInfo *d = list->devices[i];
        printf("[%d] %s（%s）", i, d->device_description, d->device_name);
        for (int k = 0; k < d->nb_media_types; k++)
            printf(" %s", av_get_media_type_string(d->media_types[k]));
        printf("\n");
    }
    avdevice_free_list_devices(&list);
    return 0;
}
```

```text
avdevice_list_input_sources 返回 -78（Function not implemented），改用 list_devices 选项：
[AVFoundation indev] AVFoundation video devices:
[AVFoundation indev] [0] “iPhone”的桌上视角相机
[AVFoundation indev] [1] “iPhone”的相机
[AVFoundation indev] [2] Capture screen 0
[AVFoundation indev] AVFoundation audio devices:
[AVFoundation indev] [0] “iPhone”的麦克风
```

（设备名中的手机名称换成了「iPhone」。）avfoundation 没有实现这个函数，返回 `ENOSYS`，只能用它的 `list_devices` 选项把设备列表打印到日志里，这也是命令行 `ffmpeg -f avfoundation -list_devices true -i ""` 的做法。

这台 Mac mini 本身没有摄像头和麦克风，列出来的相机和麦克风来自旁边的 iPhone：macOS 的「连续互通相机」可以把 iPhone 当作 Mac 的摄像头。`Capture screen 0` 是屏幕，每个显示器一个。

avfoundation 的设备名写成 `视频:音频`，可以用编号，也可以用名字，`none` 表示不要：`"2:none"` 或 `"Capture screen 0"` 只录屏幕，`"1:0"` 同时采集相机和麦克风，`"none:0"` 只录声音。

**权限**：macOS 上采集屏幕、摄像头、麦克风都要用户授权（「系统设置 → 隐私与安全性」）。没有授权时，程序并不报错，`avformat_open_input` 照样成功，只是一帧画面都收不到，`av_read_frame` 永远等不到数据。第一次在没有录屏权限的情况下运行本文的程序，就是这样卡住的，打开权限后才正常。

## 3. 采集屏幕并编码

下面的程序采集屏幕，用 swscale 缩放成 720p，用 x264 编码保存成 MP4，同时统计帧间隔、数据量和各步骤的耗时。

```c
// capture.c：用 libavdevice 采集屏幕，缩放成 720p，用 x264 编码保存成 MP4
// 用法：capture <设备名> <输出.mp4> <秒数> [key=value...]
//   设备名：macOS 上是 "Capture screen 0"、"0:none"（视频设备 0，不要音频）这样的写法
//   preset=x264 档次  size=WxH 输出尺寸（0 表示不缩放）  slow=N 每帧额外耗时 N 毫秒（模拟处理太慢）
//   其他 key=value 交给采集设备
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libavutil/parseutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "用法: %s 设备名 输出.mp4 秒数 [key=value...]\n", argv[0]); return 1; }
    int seconds = atoi(argv[3]), ow = 1280, oh = 720;
    AVRational rate = {30000, 1001};          // avfoundation 默认的帧率（ntsc）
    const char *preset = "veryfast";
    int slow = 0;
    AVDictionary *dev_opts = NULL;
    for (int i = 4; i < argc; i++) {
        char key[64], val[128];
        if (sscanf(argv[i], "%63[^=]=%127s", key, val) != 2) continue;
        if (!strcmp(key, "preset")) preset = argv[i] + 7;
        else if (!strcmp(key, "slow")) slow = atoi(val);
        else if (!strcmp(key, "size")) { if (sscanf(val, "%dx%d", &ow, &oh) != 2) ow = oh = 0; }
        else {
            if (!strcmp(key, "framerate")) av_parse_video_rate(&rate, val);
            av_dict_set(&dev_opts, key, val, 0);
        }
    }

    // 1. 打开采集设备：和打开文件一样，只是多指定一个「输入格式」
    avdevice_register_all();
    const AVInputFormat *ifmt = av_find_input_format("avfoundation");
    AVFormatContext *in = NULL;
    if (avformat_open_input(&in, argv[1], ifmt, &dev_opts) < 0) {
        fprintf(stderr, "打开设备 %s 失败\n", argv[1]);
        return 1;
    }
    AVStream *ist = in->streams[0];
    AVCodecParameters *par = ist->codecpar;
    fprintf(stderr, "设备输出：%s %s %dx%d，时间基 %d/%d\n",
            avcodec_get_name(par->codec_id), av_get_pix_fmt_name(par->format),
            par->width, par->height, ist->time_base.num, ist->time_base.den);
    if (!ow) { ow = par->width; oh = par->height; }

    // 2. 设备给出的是未压缩的画面（rawvideo），也要经过「解码器」变成 AVFrame
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    AVCodecContext *dctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dctx, par);
    avcodec_open2(dctx, dec, NULL);

    // 3. 编码器和输出文件
    AVFormatContext *out = NULL;
    avformat_alloc_output_context2(&out, NULL, NULL, argv[2]);
    const AVCodec *enc = avcodec_find_encoder_by_name("libx264");
    AVCodecContext *ectx = avcodec_alloc_context3(enc);
    ectx->width = ow;
    ectx->height = oh;
    ectx->pix_fmt = AV_PIX_FMT_YUV420P;
    ectx->time_base = ist->time_base;          // 直接沿用设备的时间戳（微秒）
    ectx->framerate = rate;
    ectx->gop_size = 60;
    av_opt_set(ectx->priv_data, "preset", preset, 0);
    av_opt_set(ectx->priv_data, "crf", "23", 0);
    if (out->oformat->flags & AVFMT_GLOBALHEADER) ectx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(ectx, enc, NULL) < 0) return 1;
    AVStream *ost = avformat_new_stream(out, NULL);
    avcodec_parameters_from_context(ost->codecpar, ectx);
    ost->time_base = ectx->time_base;
    avio_open(&out->pb, argv[2], AVIO_FLAG_WRITE);
    if (avformat_write_header(out, NULL) < 0) return 1;

    struct SwsContext *sws = NULL;
    AVFrame *raw = av_frame_alloc(), *yuv = av_frame_alloc();
    yuv->format = AV_PIX_FMT_YUV420P;
    yuv->width = ow;
    yuv->height = oh;
    av_frame_get_buffer(yuv, 0);
    AVPacket *pkt = av_packet_alloc(), *opkt = av_packet_alloc();

    int cap = 100000, n = 0;
    int *dpts = malloc(cap * sizeof(int)), *darr = malloc(cap * sizeof(int));
    int64_t first_pts = AV_NOPTS_VALUE, last_pts = 0, last_arr = 0, t_start = 0;
    double t_conv = 0, t_enc = 0;
    long bytes = 0;
    for (;;) {
        int ret = av_read_frame(in, pkt);
        if (ret == AVERROR(EAGAIN)) { av_usleep(1000); continue; }  // 设备还没有新的画面
        if (ret < 0) break;
        int64_t arr = av_gettime_relative();
        if (first_pts == AV_NOPTS_VALUE) { first_pts = pkt->pts; t_start = arr; }
        if (n > 0 && n < cap) {
            dpts[n - 1] = (int)(pkt->pts - last_pts);   // 设备时间戳的间隔（微秒）
            darr[n - 1] = (int)(arr - last_arr);        // 程序实际收到的间隔
        }
        if (n < 3)
            fprintf(stderr, "包 %d：%d 字节，pts=%lld，此刻单调时钟=%lld\n", n, pkt->size,
                    (long long)pkt->pts, (long long)arr);
        last_pts = pkt->pts;
        last_arr = arr;
        bytes += pkt->size;
        n++;

        avcodec_send_packet(dctx, pkt);
        av_packet_unref(pkt);
        while (avcodec_receive_frame(dctx, raw) == 0) {
            int64_t t0 = av_gettime_relative();
            sws = sws_getCachedContext(sws, raw->width, raw->height, raw->format,
                                       ow, oh, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
            av_frame_make_writable(yuv);
            sws_scale(sws, (const uint8_t *const *)raw->data, raw->linesize, 0, raw->height,
                      yuv->data, yuv->linesize);
            yuv->pts = raw->pts - first_pts;
            int64_t t1 = av_gettime_relative();
            avcodec_send_frame(ectx, yuv);
            while (avcodec_receive_packet(ectx, opkt) == 0) {
                av_packet_rescale_ts(opkt, ectx->time_base, ost->time_base);
                av_interleaved_write_frame(out, opkt);
            }
            t_conv += (t1 - t0) / 1000.0;
            t_enc += (av_gettime_relative() - t1) / 1000.0;
            av_frame_unref(raw);
            if (slow) av_usleep(slow * 1000);
        }
        if (arr - t_start > (int64_t)seconds * 1000000) break;
    }
    avcodec_send_frame(ectx, NULL);
    while (avcodec_receive_packet(ectx, opkt) == 0) {
        av_packet_rescale_ts(opkt, ectx->time_base, ost->time_base);
        av_interleaved_write_frame(out, opkt);
    }
    av_write_trailer(out);
    long file = avio_size(out->pb);
    avio_closep(&out->pb);

    // 统计：帧间隔的分布，间隔超过 1.5 倍标称值的算作丢了帧
    int m = n - 1 < cap ? n - 1 : cap, nominal = (int)(1e6 / av_q2d(rate));
    long lost = 0;
    for (int i = 0; i < m; i++)
        if (dpts[i] > nominal * 3 / 2) lost += (dpts[i] + nominal / 2) / nominal - 1;
    qsort(dpts, m, sizeof(int), cmp);
    qsort(darr, m, sizeof(int), cmp);
    double dur = (last_pts - first_pts) / 1e6;
    printf("采集 %d 帧，%.2f 秒，平均 %.2f 帧/秒；原始数据 %.1f MB/s，编码后 %.2f Mbps\n",
           n, dur, (n - 1) / dur, bytes / dur / 1e6, file * 8 / dur / 1e6);
    printf("时间戳间隔（毫秒）：最小 %.1f / 中位 %.1f / 最大 %.1f；收到的间隔：最小 %.1f / 中位 %.1f / 最大 %.1f\n",
           dpts[0] / 1e3, dpts[m / 2] / 1e3, dpts[m - 1] / 1e3, darr[0] / 1e3, darr[m / 2] / 1e3, darr[m - 1] / 1e3);
    printf("按时间戳推算少了 %ld 帧；每帧平均：转换 %.1f ms，编码 %.1f ms\n", lost, t_conv / n, t_enc / n);

    sws_freeContext(sws);
    av_frame_free(&raw); av_frame_free(&yuv);
    av_packet_free(&pkt); av_packet_free(&opkt);
    avcodec_free_context(&dctx); avcodec_free_context(&ectx);
    avformat_free_context(out);
    avformat_close_input(&in);
    return 0;
}
```

```bash
FF=$(brew --prefix ffmpeg)
clang -O2 capture.c -o capture -I$FF/include -L$FF/lib -lavdevice -lavformat -lavcodec -lswscale -lavutil
./capture "Capture screen 0" screen.mp4 10
```

```text
设备输出：rawvideo uyvy422 3840x2160，时间基 1/1000000
包 0：16588800 字节，pts=297385114167，此刻单调时钟=1111352718777
包 1：16588800 字节，pts=297385147500，此刻单调时钟=1111352750732
包 2：16588800 字节，pts=297385180833，此刻单调时钟=1111352782606
采集 301 帧，10.00 秒，平均 30.00 帧/秒；原始数据 499.3 MB/s，编码后 0.41 Mbps
时间戳间隔（毫秒）：最小 33.3 / 中位 33.3 / 最大 33.3；收到的间隔：最小 31.1 / 中位 33.4 / 最大 38.5
按时间戳推算少了 0 帧；每帧平均：转换 8.0 ms，编码 0.2 ms
```

### 3.1 要注意的几个地方

- **`EAGAIN`**：读文件时，`av_read_frame` 要么返回数据，要么出错或结束。avfoundation 在设备还没产生新画面时会返回 `AVERROR(EAGAIN)`，意思是「现在没有，待会儿再来」。程序最初没处理它，一启动就把 `EAGAIN` 当作出错退出了，一帧都没采到。
- **像素格式**：avfoundation 的 `pixel_format` 默认是 `yuv420p`，可屏幕并不支持，日志里会打印一段警告，然后自动换成支持列表中的第一个：

  ```text
  Selected pixel format (yuv420p) is not supported by the input device.
  Supported pixel formats:
    uyvy422
    yuyv422
    nv12
    0rgb
    bgr0
  Overriding selected pixel format to use uyvy422 instead.
  ```

  uyvy422 是打包格式的 4:2:2，每个像素 2 字节，一帧 4K 就是 3840 × 2160 × 2 = 16588800 字节，和输出一致。
- **帧率**：`framerate` 默认是 `ntsc`（30000/1001，即 29.97），屏幕实际按 30 帧/秒给出，时间戳的间隔是整整 33.333 毫秒。流信息里的 `r_frame_rate` 是 0/0，程序要自己记住设置的帧率。
- **时间戳**：时间基是 1/1000000，pts 以微秒为单位，来自系统的采集时钟（约 82.6 小时，接近开机以来的时间），和程序里 `av_gettime_relative` 用的单调时钟不是同一个起点（约 308.7 小时），不能直接相减。不过 pts 的**间隔**是准确的：时间戳间隔始终是 33.3 毫秒，而程序收到包的间隔在 31.1～38.5 毫秒之间抖动。编码时应该用设备的时间戳，而不是收到包的时刻。
- **数据量**：每秒 499 MB 的原始数据，编码后只有 0.41 Mbps（每秒 51 KB），差了约一万倍。屏幕大部分时间是静止的，x264 的 P 帧中 99% 以上的宏块都直接跳过了（`skip:99.4%`），这是录屏和拍摄自然画面的一大区别。

### 3.2 像素格式和帧率的影响

换几种设置，各采集 10 秒：

| 设置 | 原始数据 | 实际帧率 | 转换成 720p 的耗时 | 编码后 |
|---|---|---|---|---|
| uyvy422（默认），30 帧/秒 | 499 MB/s | 30.00 | 8.0 ms | 0.41 Mbps |
| nv12，30 帧/秒 | 375 MB/s | 30.00 | 5.2 ms | 0.42 Mbps |
| bgr0，30 帧/秒 | 999 MB/s | 30.00 | 6.9 ms | 0.42 Mbps |
| nv12，60 帧/秒 | 748 MB/s | 60.00 | 3.0 ms | 0.70 Mbps |
| nv12，60 帧/秒，不缩放，preset=medium | 748 MB/s | 60.00 | 0.4 ms | 3.60 Mbps |

- **nv12** 本来就是 4:2:0，每个像素 1.5 字节，数据量比 uyvy422 少四分之一，转换成 yuv420p 也只要分离 U、V，不用对色度做垂直方向的缩减。bgr0 是每像素 4 字节的 RGB，数据量翻倍，转换时还要做 RGB 到 YUV 的矩阵运算。采集时选和编码器最接近的格式，能省下不少内存带宽和 CPU。
- **60 帧/秒**：屏幕能按 60 帧/秒给出，帧数翻倍，数据量也翻倍。60 帧/秒那一组的转换反而更快，这类几毫秒的数字受 CPU 频率调度的影响很大，只能看个大概。
- **不缩放**：4K 直接编码，`size=0` 时 swscale 只做 nv12 到 yuv420p 的重排，0.4 毫秒。码率涨到 3.6 Mbps，依然很低，因为画面基本是静止的。

## 4. 处理不过来的时候

上面的转换加编码每帧只要几毫秒，远小于帧间隔。如果处理一帧比帧间隔还慢呢？程序的 `slow` 参数让每帧额外睡 50 毫秒，模拟编码太慢：

```text
$ ./capture "Capture screen 0" screen.mp4 10 pixel_format=nv12 slow=50
采集 151 帧，10.03 秒，平均 14.95 帧/秒；原始数据 187.4 MB/s，编码后 0.25 Mbps
时间戳间隔（毫秒）：最小 33.3 / 中位 66.7 / 最大 100.0；收到的间隔：最小 58.3 / 中位 67.7 / 最大 72.0
按时间戳推算少了 151 帧；每帧平均：转换 10.8 ms，编码 0.7 ms
```

10 秒只拿到 151 帧，时间戳的间隔大多是 66.7 毫秒（隔一帧取一帧），偶尔 100 毫秒（隔两帧）。程序处理一帧要 60 多毫秒，等它回来读下一帧时，设备已经又产生了一两帧，**旧的那些被丢掉了，程序只能拿到最新的一帧**。把 avfoundation 的 `drop_late_frames` 关掉，结果一样（152 帧，少了 148 帧）：FFmpeg 的 avfoundation 实现里只保存「当前帧」，新画面到来就替换掉旧的，不会排队。

这和网络流正好相反。RTMP 那一篇会看到，TCP 上的数据一帧都不会丢，处理不过来就排队，延迟越积越大；采集设备则是丢帧保实时，延迟不会增长，但录下来的视频会跳帧。好在时间戳是准确的，用设备的时间戳编码，播放时画面虽然卡顿，时间轴却不会错乱。如果用「第几帧 × 帧间隔」来生成时间戳，丢了一半的帧后，10 秒的录像就只剩 5 秒长，而且播放速度变成两倍。

FFmpeg 命令行在输入和编码之间有一个线程和队列（`-thread_queue_size`），编码一时跟不上时可以缓冲几帧，队列满了会提示 `Thread message queue blocking; consider raising the thread_queue_size`。长期跟不上，最终还是会丢帧。

## 5. 采集的延迟

从屏幕上出现一个画面，到程序拿到这一帧，要多久？办法和本章后面几篇测延迟的一样：让屏幕上显示当前时间的条码，采集后读出来。先写一个 SDL 程序，在屏幕左上角开一个无边框窗口，每次刷新屏幕都画出当前时间：

```c
// clock_window.c：在屏幕左上角开一个无边框窗口，每次刷新都画出当前时间的条码
// 用法：clock_window [秒数]
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    SDL_Init(SDL_INIT_VIDEO);
    // 40 个 24×24 的方块（单位是「点」，Retina 屏上一个点是 2×2 个像素）
    SDL_Window *win = SDL_CreateWindow("clock", 960, 24, SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);
    SDL_SetWindowPosition(win, 0, 100);
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    SDL_SetRenderVSync(ren, 1);                     // 跟着显示器的刷新走
    Uint64 end = SDL_GetTicks() + (Uint64)seconds * 1000;
    while (SDL_GetTicks() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT) end = 0;
        uint32_t ms = now_ms();
        uint8_t sum = (ms & 0xff) + ((ms >> 8) & 0xff) + ((ms >> 16) & 0xff) + (ms >> 24);
        uint64_t v = ((uint64_t)ms << 8) | (uint8_t)~sum;
        for (int b = 0; b < 40; b++) {
            int bit = (v >> (39 - b)) & 1;
            SDL_SetRenderDrawColor(ren, bit ? 255 : 0, bit ? 255 : 0, bit ? 255 : 0, 255);
            SDL_FRect r = {b * 24.0f, 0, 24, 24};
            SDL_RenderFillRect(ren, &r);
        }
        SDL_RenderPresent(ren);
    }
    SDL_Quit();
    return 0;
}
```

再写一个采集程序，在采集到的画面中，窗口所在的位置读出条码，用收到这一帧的时间减去它：

```c
// cap_latency.c：采集屏幕，读出 clock_window 画在屏幕上的时间条码，算出「画上屏幕」到「程序拿到这一帧」的时间
// 用法：cap_latency [帧率] [秒数]
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(int argc, char **argv) {
    const char *rate = argc > 1 ? argv[1] : "60";
    int seconds = argc > 2 ? atoi(argv[2]) : 10;
    avdevice_register_all();
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "framerate", rate, 0);
    av_dict_set(&opts, "pixel_format", "nv12", 0);
    AVFormatContext *in = NULL;
    if (avformat_open_input(&in, "Capture screen 0", av_find_input_format("avfoundation"), &opts) < 0) return 1;
    int w = in->streams[0]->codecpar->width;

    // 窗口在 (0, 100) 点处，Retina 屏上是 (0, 200) 像素；每个方块 24 点 = 48 像素
    const int y0 = 200, blk = 48;
    int n = 0, bad = 0, *lat = malloc(100000 * sizeof(int));
    uint32_t prev = 0;
    int same = 0;
    AVPacket *pkt = av_packet_alloc();
    int64_t end = av_gettime_relative() + (int64_t)seconds * 1000000;
    while (av_gettime_relative() < end) {
        int ret = av_read_frame(in, pkt);
        if (ret == AVERROR(EAGAIN)) { av_usleep(1000); continue; }
        if (ret < 0) break;
        uint32_t now = now_ms();
        // NV12 的前 w×h 个字节就是 Y 平面
        uint64_t v = 0;
        for (int b = 0; b < 40; b++) {
            int sum = 0;
            for (int y = 20; y < 28; y++)
                for (int x = 20; x < 28; x++) sum += pkt->data[(y0 + y) * w + b * blk + x];
            v = (v << 1) | (sum / 64 > 128);
        }
        av_packet_unref(pkt);
        uint32_t t = (uint32_t)(v >> 8);
        uint8_t s = (t & 0xff) + ((t >> 8) & 0xff) + ((t >> 16) & 0xff) + (t >> 24);
        if ((uint8_t)~s != (uint8_t)(v & 0xff)) { bad++; continue; }
        if (t == prev) same++;              // 和上一帧是同一个画面
        prev = t;
        lat[n++] = (int32_t)(now - t);
    }
    qsort(lat, n, sizeof(int), cmp);
    printf("帧率 %s：读到 %d 帧（其中 %d 帧和上一帧相同），读不出 %d 帧；延迟 最小 %d / 中位 %d / 最大 %d ms\n",
           rate, n, same, bad, lat[0], lat[n / 2], lat[n - 1]);
    avformat_close_input(&in);
    return 0;
}
```

```bash
./clock_window 40 &
./cap_latency 60 10
./cap_latency 30 10
```

```text
帧率 60：读到 600 帧（其中 1 帧和上一帧相同），读不出 0 帧；延迟 最小 77 / 中位 96 / 最大 105 ms
帧率 30：读到 299 帧（其中 0 帧和上一帧相同），读不出 0 帧；延迟 最小 98 / 中位 103 / 最大 121 ms
```

<figure class="diagram">
<svg viewBox="0 0 680 150" role="img" aria-label="采集延迟的组成：程序画好一帧并提交，等到下一次垂直同步才显示，最多 16.7 毫秒；窗口服务器合成整个屏幕；屏幕采集再把合成好的画面复制出来交给采集程序；从画上屏幕到采集程序拿到，实测中位数 96 毫秒">
  <defs>
    <marker id="cl-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="20" width="120" height="50" rx="6" style="fill:var(--panel-2)"/>
      <rect x="165" y="20" width="120" height="50" rx="6" style="fill:var(--panel-2)"/>
      <rect x="325" y="20" width="140" height="50" rx="6" style="fill:var(--panel-2)"/>
      <rect x="505" y="20" width="170" height="50" rx="6" style="fill:var(--mark)"/>
    </g>
    <text x="15" y="42" style="fill:var(--fg)">画出条码</text><text x="15" y="60" style="fill:var(--muted);font-size:10px">记下当前时间</text>
    <text x="175" y="42" style="fill:var(--fg)">等垂直同步</text><text x="175" y="60" style="fill:var(--muted);font-size:10px">0～16.7 ms</text>
    <text x="335" y="42" style="fill:var(--fg)">窗口服务器合成</text><text x="335" y="60" style="fill:var(--muted);font-size:10px">整个屏幕</text>
    <text x="515" y="42" style="fill:var(--fg)">屏幕采集交付</text><text x="515" y="60" style="fill:var(--muted);font-size:10px">av_read_frame 返回</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#cl-arrow)">
      <line x1="125" y1="45" x2="163" y2="45"/><line x1="285" y1="45" x2="323" y2="45"/><line x1="465" y1="45" x2="503" y2="45"/>
    </g>
    <line x1="10" y1="100" x2="670" y2="100" style="stroke:var(--danger)" stroke-width="2" marker-end="url(#cl-arrow)"/>
    <text x="230" y="125" style="fill:var(--danger);font-size:11px">实测：60 帧/秒 中位 96 ms，30 帧/秒 中位 103 ms</text>
  </g>
</svg>
</figure>

延迟接近 0.1 秒，比后面 RTMP 篇测到的本机推拉流的全部延迟（17 毫秒）还大得多。画面要先等垂直同步显示出来，再经过窗口服务器合成、屏幕采集接口复制和交付，每一环都有缓冲。采集帧率从 60 降到 30，延迟又多了几毫秒，因为每一帧要等的采集时刻更稀疏了。

摄像头的延迟构成不同（曝光、传感器读出、ISP 处理、USB 或无线传输），但同样不可忽视。直播的「端到端延迟」是从镜头前发生的事情算起的，采集这一段经常就要几十到上百毫秒，后面几篇测的协议延迟都要加在它上面。

另外，`AVCaptureScreenInput`（FFmpeg 的 avfoundation 录屏用的接口）是比较老的接口，Apple 现在推荐用 ScreenCaptureKit 录屏，它可以只录某个窗口、自带音频，性能也更好。OBS 在 macOS 上也提供了基于 ScreenCaptureKit 的采集源。

## 6. 摄像头和麦克风

摄像头、麦克风和屏幕用同一套 API，只是设备名和选项不同：

- **摄像头**：`"1:none"`。摄像头只支持特定的分辨率和帧率组合，`video_size` 和 `framerate` 必须是它支持的一种，否则打开失败，日志里会列出所有支持的模式。
- **麦克风**：`"none:0"`。读出来的包是 PCM，用第 3 章的 swresample 转换成编码器需要的格式（比如 AAC 要的 fltp），再按 1024 个采样一帧送给编码器。
- **同时采集**：`"1:0"`，一个 AVFormatContext 里有两路流，`av_read_frame` 交替返回视频包和音频包，各自带着设备时钟的时间戳。两路的时间戳来自同一个时钟，音视频同步就靠它。

本文没有实测摄像头和麦克风：这台 Mac mini 本身没有，列表里的是 iPhone 的连续互通相机和麦克风。

## 7. 小结

- libavdevice 把采集设备包装成输入格式：`avformat_open_input` 加上格式名和设备名，读出的是 rawvideo 或 PCM 包，之后和处理文件一样。
- avfoundation 不支持 `avdevice_list_input_sources`，要用 `list_devices` 选项枚举设备；没有授权时程序不会报错，只是收不到画面。
- 读设备要处理 `EAGAIN`；像素格式要选设备支持的，nv12 比默认的 uyvy422 少四分之一的数据，也更容易转换；时间戳以微秒为单位，编码时应当使用它。
- 4K 屏幕每秒几百 MB 的原始数据，静止的屏幕编码后不到 1 Mbps。
- 处理不过来时，采集设备丢帧而不排队，延迟不增长，但录像会跳帧。
- 从画上屏幕到采集程序拿到这一帧，实测约 0.1 秒，这是端到端延迟的第一段。

下一篇开始把采集、编码好的流送到网络上，先看 RTSP 和 RTP。

## 练习

1. 4K、60 帧/秒的屏幕采集，分别用 nv12 和 bgr0，每秒的原始数据各是多少？和实测对得上吗？
2. 第 4 节每帧多耗 50 毫秒时，为什么实际帧率是 15 帧/秒左右，而不是 1000 / (50 + 11.5) ≈ 16 帧/秒？
3. 采集设备给出的包是 rawvideo，`cap_latency` 没有经过解码器，直接从 `pkt->data` 里读 Y 平面。这样做在什么情况下会出错？

<details>
<summary>点击查看答案</summary>

1. nv12 每像素 1.5 字节：3840 × 2160 × 1.5 × 60 = 746496000 字节/秒，约 746.5 MB/s；实测 748.3 MB/s，差的一点是因为 10.02 秒里采了 602 帧，比整 60 帧/秒略多一帧（统计的时长按第一帧到最后一帧算）。bgr0 每像素 4 字节：3840 × 2160 × 4 × 60 ≈ 1990.7 MB/s，接近每秒 2 GB。30 帧/秒的 bgr0 实测 999 MB/s，正好是它的一半。这么大的数据量，用 CPU 逐像素处理都很吃力，所以录屏软件会尽量让 GPU 完成缩放和格式转换，甚至直接把 GPU 上的画面交给硬件编码器（第 4 章《硬件解码》中零拷贝的思路）。

2. 50 + 11.5 只算了睡眠、转换和编码，循环里还有别的开销：每帧 12 MB 的包要从设备复制出来，rawvideo 解码器还要把它填进 AVFrame。实测程序两次收到包的间隔中位数是 67.7 毫秒，比 61.5 毫秒多了 6 毫秒左右，所以每秒只能读 15 次左右。另一方面，设备每 33.3 毫秒产生一帧，只保留最新的一帧，程序每次拿到的都是最新的那一帧，相邻两次拿到的帧，时间戳必然相差 33.3 毫秒的整数倍：读取间隔在 67 毫秒上下波动，多数时候正好跨过两帧（66.7 毫秒，中间丢一帧），偶尔跨过一帧或三帧（33.3、100 毫秒）。实测时间戳间隔的最小值、中位数、最大值正是 33.3、66.7、100 毫秒。

3. 直接读 `pkt->data` 有两个前提：一是像素格式确实是 nv12（Y 平面在最前面），所以程序明确设置了 `pixel_format=nv12`，换成 uyvy422 或 bgr0，同样的位置读到的就不是亮度了；二是每行的字节数正好等于宽度，没有填充。rawvideo 包里的数据是紧密排列的，这台机器上 3840 宽的 Y 平面每行正好 3840 字节，所以没问题。但设备给出的每行字节数可能为了对齐而大于宽度（比如某些宽度不是 16 或 64 的倍数的分辨率），或者 Y、UV 两个平面之间有空隙，直接按 `y * w` 计算位置就会错位。经过 rawvideo 解码器得到 AVFrame 后，用 `data[0]` 和 `linesize[0]` 访问，就不用关心这些细节。

</details>
