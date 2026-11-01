---
title: RTMP 推流与延迟测量
date: 2026-11-03 11:00:00
updated: 2026-11-03 11:00:00
categories:
  - 音视频开发
  - 采集与流媒体
tags:
  - 音视频
  - 流媒体
  - RTMP
  - FFmpeg
  - 延迟
  - C
order: 3
description: 用 FFmpeg 的 API 写一个推流程序，把测试视频当作摄像头实时编码推到 RTMP 服务器，每一帧顶部画一条记录生成时刻的「时间条码」；拉流端解码后读出条码，就能算出每一帧的端到端延迟。再写一个夹在中间的代理，边转发边解析握手、块和 AMF0 命令，看清 connect、createStream、publish、play 的全过程。实测：FFmpeg 默认参数拉 RTMP，延迟一直停在 5.2 秒，调小探测参数后只有 17 毫秒；加上常被推荐的 nobuffer，第一个关键帧反而被丢掉，打开时间多出一个 GOP；编码器不用 zerolatency，延迟变成 1.2 秒；网络变慢 5 秒，延迟涨到 2.5 秒后再也降不下来。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：了解 RTMP 协议的结构：握手、块、消息、AMF0 命令；会用 FFmpeg 的 API 实时编码并推流；学会测量直播的端到端延迟；知道播放端的探测参数、编码器参数、GOP 长度怎样影响延迟和打开速度；理解基于 TCP 的协议在网络变差时为什么会积累延迟。
>
> **前置知识**：上一篇；第 2 章《FLV 封装格式》；第 3 章《编码》中 x264 的编码延迟和 zerolatency。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，MediaMTX 1.21.3。

## 1. RTMP

RTMP（Real-Time Messaging Protocol）是 Adobe 为 Flash 设计的协议，跑在 TCP 上，默认端口 1935。Flash 早已退出历史舞台，但 RTMP 作为**推流**协议一直沿用至今：OBS 等推流软件、各家直播平台给主播的推流地址，绝大多数都是 `rtmp://`。它简单、延迟低、几乎所有服务器都支持。原来的 RTMP 只能传 H.264 和 AAC，2023 年发布的 Enhanced RTMP 扩展加入了 HEVC、AV1 等编码，FFmpeg 6.1 起已经支持。

RTMP 里传的音视频数据，就是第 2 章讲过的 **FLV 的 Tag**：去掉 FLV 文件头和 Tag 头，Tag 的正文原样装进 RTMP 消息。所以 FFmpeg 推 RTMP 用的是 `flv` 封装格式，URL 写成 `rtmp://...` 就行。

本篇先写推流程序和测延迟的工具，然后用一个自己写的代理看看 RTMP 里到底传了什么，最后测量各种因素对延迟的影响。

## 2. 推流程序

真实的推流端从摄像头取画面（本章第 1 篇），这里用测试视频代替：解码出一帧，就当作摄像头拍下了一帧，按帧率的节奏送给编码器，编码后立刻推出去。文件播完了就从头再来，可以一直推下去。

为了测量延迟，每一帧在送进编码器之前，在画面顶部画一条**时间条码**：把此刻的系统时间（毫秒，取低 32 位）画成 32 个黑白方块，再加 8 位校验，一共 40 个 32×32 的方块，正好铺满 1280 像素宽。拉流端解码出这一帧，读出条码，用当前时间减去它，就是这一帧从「拍下」到「解码出来」经过的时间。推流端和拉流端在同一台电脑上，用的是同一个时钟，不需要对时。

<figure>
<img src="/img/av/live-barcode.jpg" alt="从 RTMP 流中截下的一帧：顶部是一排黑白方块组成的时间条码，下面是测试视频的画面" loading="lazy">
<figcaption>从直播流中截下的一帧。顶部的黑白方块就是时间条码，每个方块 32×32 像素，压缩以后依然很容易识别</figcaption>
</figure>

方块要做得大一些：编码会把细节压掉，一个像素宽的线条可能就糊了。32×32 的纯黑纯白方块，编码后读每个方块中心 8×8 的平均亮度，从来没有读错过（后面的实验中，「坏帧」一项统计的就是读不出条码的帧）。

```c
// live_push.c：把测试视频当作「摄像头」，实时编码并推流
// 每一帧在画面顶部画一条「时间条码」：生成这一帧时的系统时间（毫秒）
// 用法：live_push <输入文件> <输出 URL> <封装格式> [GOP 秒数] [选项...]
//   封装格式：flv（RTMP）、rtsp、mpegts（SRT/UDP）、whip（WebRTC）
//   选项：zerolatency=0 关闭零延迟调优；seconds=N 推流时长；fps=N 改变帧率；其他 key=value 交给封装层
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define BITS 40          // 32 位时间 + 8 位校验
#define BLK  32          // 每一位是 32×32 的方块，40 位正好铺满 1280 宽

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// 在 YUV420P 帧的顶部画条码：白块是 1，黑块是 0
static void draw_code(AVFrame *f, uint32_t ms) {
    uint8_t sum = (ms & 0xff) + ((ms >> 8) & 0xff) + ((ms >> 16) & 0xff) + (ms >> 24);
    uint64_t v = ((uint64_t)ms << 8) | (uint8_t)~sum;
    for (int b = 0; b < BITS; b++) {
        int bit = (v >> (BITS - 1 - b)) & 1;
        for (int y = 0; y < BLK; y++)
            memset(f->data[0] + y * f->linesize[0] + b * BLK, bit ? 235 : 16, BLK);
        for (int y = 0; y < BLK / 2; y++) {
            memset(f->data[1] + y * f->linesize[1] + b * BLK / 2, 128, BLK / 2);
            memset(f->data[2] + y * f->linesize[2] + b * BLK / 2, 128, BLK / 2);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "用法: %s 输入 输出URL 封装格式 [GOP秒数] [key=value...]\n", argv[0]);
        return 1;
    }
    double gop_sec = argc > 4 ? atof(argv[4]) : 1.0;
    int zerolatency = 1, seconds = 30, force_fps = 0;
    AVDictionary *mux_opts = NULL;
    for (int i = 5; i < argc; i++) {
        char key[64], val[256];
        if (sscanf(argv[i], "%63[^=]=%255s", key, val) != 2) continue;
        if (!strcmp(key, "zerolatency")) zerolatency = atoi(val);
        else if (!strcmp(key, "seconds")) seconds = atoi(val);
        else if (!strcmp(key, "fps")) force_fps = atoi(val);
        else av_dict_set(&mux_opts, key, val, 0);
    }

    // 1. 打开测试视频，当作画面来源
    AVFormatContext *in = NULL;
    if (avformat_open_input(&in, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(in, NULL);
    int vi = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    AVStream *ist = in->streams[vi];
    const AVCodec *dec = avcodec_find_decoder(ist->codecpar->codec_id);
    AVCodecContext *dctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dctx, ist->codecpar);
    avcodec_open2(dctx, dec, NULL);
    AVRational fps = force_fps ? (AVRational){force_fps, 1} : av_guess_frame_rate(in, ist, NULL);

    // 2. 编码器：x264，没有 B 帧，GOP 固定
    const AVCodec *enc = avcodec_find_encoder_by_name("libx264");
    AVCodecContext *ectx = avcodec_alloc_context3(enc);
    ectx->width = dctx->width;
    ectx->height = dctx->height;
    ectx->pix_fmt = AV_PIX_FMT_YUV420P;
    ectx->time_base = av_inv_q(fps);
    ectx->framerate = fps;
    ectx->gop_size = (int)(gop_sec * av_q2d(fps) + 0.5);
    ectx->keyint_min = ectx->gop_size;
    ectx->max_b_frames = 0;
    ectx->bit_rate = 2000000;
    ectx->color_primaries = AVCOL_PRI_BT709;
    ectx->color_trc = AVCOL_TRC_BT709;
    ectx->colorspace = AVCOL_SPC_BT709;
    av_opt_set(ectx->priv_data, "preset", "veryfast", 0);
    av_opt_set(ectx->priv_data, "profile", "baseline", 0);
    av_opt_set(ectx->priv_data, "x264-params", "scenecut=0", 0);
    if (zerolatency) av_opt_set(ectx->priv_data, "tune", "zerolatency", 0);

    // 3. 输出：封装格式决定协议上跑的是什么
    AVFormatContext *out = NULL;
    avformat_alloc_output_context2(&out, NULL, argv[3], argv[2]);
    if (!out) { fprintf(stderr, "不支持的格式 %s\n", argv[3]); return 1; }
    if (out->oformat->flags & AVFMT_GLOBALHEADER)
        ectx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(ectx, enc, NULL) < 0) return 1;
    AVStream *ost = avformat_new_stream(out, NULL);
    avcodec_parameters_from_context(ost->codecpar, ectx);
    ost->time_base = ectx->time_base;
    if (!(out->oformat->flags & AVFMT_NOFILE) &&
        avio_open2(&out->pb, argv[2], AVIO_FLAG_WRITE, NULL, &mux_opts) < 0) {
        fprintf(stderr, "连接 %s 失败\n", argv[2]);
        return 1;
    }
    if (avformat_write_header(out, &mux_opts) < 0) {
        fprintf(stderr, "write_header 失败\n");
        return 1;
    }
    fprintf(stderr, "推流到 %s，%dx%d %.2f fps，GOP %d 帧，zerolatency=%d\n",
            argv[2], ectx->width, ectx->height, av_q2d(fps), ectx->gop_size, zerolatency);

    AVPacket *pkt = av_packet_alloc(), *opkt = av_packet_alloc();
    AVFrame *frm = av_frame_alloc(), *pic = av_frame_alloc();
    pic->format = AV_PIX_FMT_YUV420P;
    pic->width = ectx->width;
    pic->height = ectx->height;
    av_frame_get_buffer(pic, 0);

    int64_t start = av_gettime_relative();
    int64_t n = 0, total = (int64_t)seconds * fps.num / fps.den;
    while (n < total) {
        // 取下一帧画面，文件读完就从头再来
        int got = 0;
        while (!got) {
            if (avcodec_receive_frame(dctx, frm) == 0) { got = 1; break; }
            if (av_read_frame(in, pkt) < 0) {
                av_seek_frame(in, vi, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(dctx);
                continue;
            }
            if (pkt->stream_index == vi) avcodec_send_packet(dctx, pkt);
            av_packet_unref(pkt);
        }
        // 按帧率等到这一帧「被拍下」的时刻
        int64_t due = start + n * 1000000 * fps.den / fps.num;
        int64_t wait = due - av_gettime_relative();
        if (wait > 0) av_usleep(wait);

        av_frame_make_writable(pic);
        av_frame_copy(pic, frm);
        av_frame_unref(frm);
        draw_code(pic, now_ms());
        pic->pts = n++;
        pic->pict_type = AV_PICTURE_TYPE_NONE;

        avcodec_send_frame(ectx, pic);
        while (avcodec_receive_packet(ectx, opkt) == 0) {
            opkt->stream_index = 0;
            av_packet_rescale_ts(opkt, ectx->time_base, ost->time_base);
            if (av_interleaved_write_frame(out, opkt) < 0) {
                fprintf(stderr, "发送失败，停止推流\n");
                n = total;
                break;
            }
        }
    }
    avcodec_send_frame(ectx, NULL);
    while (avcodec_receive_packet(ectx, opkt) == 0) {
        opkt->stream_index = 0;
        av_packet_rescale_ts(opkt, ectx->time_base, ost->time_base);
        av_interleaved_write_frame(out, opkt);
    }
    av_write_trailer(out);
    if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
    avformat_free_context(out);
    avcodec_free_context(&ectx);
    avcodec_free_context(&dctx);
    avformat_close_input(&in);
    av_frame_free(&frm); av_frame_free(&pic);
    av_packet_free(&pkt); av_packet_free(&opkt);
    return 0;
}
```

要点：

- **按时间送帧**：第 n 帧在 `start + n / fps` 时送出。这是在模拟摄像头：摄像头每 1/24 秒才产生一帧，不会因为编码器空闲就提前给你。去掉这个等待，程序会以几百帧每秒的速度推流，服务器和播放器都会乱套。FFmpeg 命令行的 `-re` 参数做的是同样的事情。
- **编码器设置**：没有 B 帧，GOP 固定（关掉场景切换检测 `scenecut=0`，否则 x264 会在镜头切换处插入额外的关键帧），默认用 `tune=zerolatency`。为了后面测 WebRTC 方便，用了 Baseline 档次。
- **`AVFMT_GLOBALHEADER`**：FLV 需要把 SPS、PPS 放在序列头里，所以要让编码器把它们放进 `extradata`（第 3 章《转封装与转码》中讲过）。
- **封装格式由参数决定**：`flv` 推 RTMP，`rtsp` 推 RTSP，`mpegts` 推 UDP 或 SRT，`hls` 直接生成切片。同一个程序，后面几篇都会用到。

推流：

```bash
FF=$(brew --prefix ffmpeg)
clang -O2 live_push.c -o live_push -I$FF/include -L$FF/lib -lavformat -lavcodec -lavutil
./live_push bbb-10s.mp4 rtmp://127.0.0.1/live/test flv 1 seconds=3000
```

推到 MediaMTX 上的流，可以用任何协议拉取：`rtmp://127.0.0.1/live/test`、`rtsp://127.0.0.1:8554/live/test`、`http://127.0.0.1:8888/live/test/index.m3u8`……服务器会自动转换。

## 3. 测量延迟的程序

拉流端：打开 URL，解码，读出每一帧的条码，统计延迟。

```c
// latency_probe.c：拉流、解码，读出每一帧顶部的时间条码，算出端到端延迟
// 用法：latency_probe <URL> [秒数] [key=value...]   key=value 交给 avformat_open_input
// 默认测到达延迟：帧一解码出来就算；play=1 测播放延迟：像播放器那样按 PTS 匀速显示
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BITS 40
#define BLK  32

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// 取每个方块中间 8×8 的平均亮度，大于 128 算 1；校验不对返回 -1
static int read_code(const AVFrame *f, uint32_t *ms) {
    uint64_t v = 0;
    for (int b = 0; b < BITS; b++) {
        int sum = 0;
        for (int y = 12; y < 20; y++)
            for (int x = 12; x < 20; x++)
                sum += f->data[0][y * f->linesize[0] + b * BLK + x];
        v = (v << 1) | (sum / 64 > 128);
    }
    uint32_t t = (uint32_t)(v >> 8);
    uint8_t s = (t & 0xff) + ((t >> 8) & 0xff) + ((t >> 16) & 0xff) + (t >> 24);
    if ((uint8_t)~s != (uint8_t)(v & 0xff)) return -1;
    *ms = t;
    return 0;
}

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "用法: %s URL [秒数] [key=value...]\n", argv[0]); return 1; }
    int seconds = argc > 2 ? atoi(argv[2]) : 10;
    AVDictionary *opts = NULL;
    int pace = 0, fps = 24;
    FILE *log = NULL;
    for (int i = 3; i < argc; i++) {
        char key[64], val[256];
        if (sscanf(argv[i], "%63[^=]=%255s", key, val) != 2) continue;
        if (!strcmp(key, "play")) pace = atoi(val);
        else if (!strcmp(key, "fps")) fps = atoi(val);        // 推流端的帧率，用来判断是否缺帧
        else if (!strcmp(key, "log")) log = fopen(val, "w");   // 每一帧的延迟写进文件
        else av_dict_set(&opts, key, val, 0);
    }
    avformat_network_init();

    int64_t t_open = av_gettime_relative();
    AVFormatContext *in = NULL;
    if (avformat_open_input(&in, argv[1], NULL, &opts) < 0) {
        fprintf(stderr, "打开 %s 失败\n", argv[1]);
        return 1;
    }
    avformat_find_stream_info(in, NULL);
    int vi = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    const AVCodec *dec = avcodec_find_decoder(in->streams[vi]->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, in->streams[vi]->codecpar);
    ctx->thread_type = FF_THREAD_SLICE;  // 帧级多线程会让解码器多攒几帧，测延迟时关掉
    avcodec_open2(ctx, dec, NULL);

    int cap = 100000, n = 0, bad = 0, stalls = 0, corrupt = 0, skipped = 0;
    uint32_t prev_sent = 0;
    int *lat = malloc(cap * sizeof(int));
    AVRational tb = in->streams[vi]->time_base;
    int64_t clock0 = 0, pts0 = 0;   // 播放时钟：clock0 时刻显示 pts0
    int64_t t_first = 0, t_end = 0;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frm = av_frame_alloc();
    while (av_read_frame(in, pkt) >= 0) {
        if (pkt->stream_index == vi && avcodec_send_packet(ctx, pkt) >= 0) {
            while (avcodec_receive_frame(ctx, frm) == 0) {
                uint32_t sent, now = now_ms();
                if (read_code(frm, &sent) < 0) { bad++; continue; }
                // 解码器发现参考帧缺失、数据出错时会给帧打上标记；条码时间跳了一大截说明中间少了帧
                if ((frm->flags & AV_FRAME_FLAG_CORRUPT) || frm->decode_error_flags) corrupt++;
                int gap = prev_sent ? (int32_t)(sent - prev_sent) : 0;
                if (gap * fps > 1500) skipped += (gap * fps + 500) / 1000 - 1;
                prev_sent = sent;
                if (!t_first) {
                    t_first = av_gettime_relative();
                    t_end = t_first + (int64_t)seconds * 1000000;
                    fprintf(stderr, "打开到第一帧 %.0f ms\n", (t_first - t_open) / 1000.0);
                }
                // 播放：第一帧立即显示，之后按 PTS 的间隔显示；帧来晚了就卡住，时钟往后推
                if (pace) {
                    int64_t pts = av_rescale_q(frm->best_effort_timestamp, tb, AV_TIME_BASE_Q);
                    int64_t t = av_gettime_relative();
                    if (!clock0) { clock0 = t; pts0 = pts; }
                    int64_t due = clock0 + (pts - pts0);
                    if (due > t) av_usleep(due - t);
                    else if (t - due > 20000) { clock0 += t - due; stalls++; }
                    now = now_ms();
                }
                if (n < cap) lat[n++] = (int32_t)(now - sent);
                if (log) fprintf(log, "%.3f %d\n", (av_gettime_relative() - t_first) / 1e6, (int32_t)(now - sent));
                if (n % 24 == 0) fprintf(stderr, "\r延迟 %4d ms  ", lat[n - 1]);
            }
        }
        av_packet_unref(pkt);
        if (t_first && av_gettime_relative() > t_end) break;
    }
    fprintf(stderr, "\n");
    if (!n) { printf("没有读到条码\n"); return 1; }
    int last = lat[n - 1];
    qsort(lat, n, sizeof(int), cmp);
    if (corrupt || skipped) printf("出错帧 %d  缺帧 %d  ", corrupt, skipped);
    printf("帧数 %d  坏帧 %d  卡顿 %d 次  %s延迟 最小 %d / 中位 %d / 最大 %d / 最后 %d ms\n",
           n, bad, stalls, pace ? "播放" : "到达", lat[0], lat[n / 2], lat[n - 1], last);
    if (log) fclose(log);
    avcodec_free_context(&ctx);
    avformat_close_input(&in);
    return 0;
}
```

它测两种延迟：

- **到达延迟**（默认）：一帧解码出来就算，测的是「最快能多快拿到这一帧」。
- **播放延迟**（`play=1`）：像真正的播放器那样，第一帧马上显示，之后严格按 PTS 的间隔显示（第 4 章的做法）；帧来晚了就卡住等它，然后接着按 PTS 的间隔走。观众实际看到的是这个延迟。

这两个数在网络平稳时差不多，第 6 节会看到它们在网络变差时的差别。

## 4. RTMP 里传了什么

用 Wireshark 抓包当然可以，不过自己写一个代理更有意思：它在 19350 端口等推流端连接，再连上真正的服务器，双向转发数据，同时按 RTMP 的格式解析出每一条消息。程序有 200 多行，放在折叠块里：

<details>
<summary>rtmp_sniff.c</summary>

```c
// rtmp_sniff.c：夹在 RTMP 客户端和服务器中间的代理，边转发边解析
// 打印双方的握手和每一条 RTMP 消息；可以给「服务器→客户端」方向限速，模拟网络变差
// 用法：rtmp_sniff 监听端口 服务器端口 [限速KB/s 开始秒 结束秒]
#include <arpa/inet.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static double t0;

// 每个方向一个解析器
typedef struct { uint32_t ts, len, type, sid, got; uint8_t *buf; } ChunkStream;
typedef struct {
    const char *name;
    uint8_t *data; size_t size, pos;   // 收到但还没解析完的字节
    int handshake;                     // 握手还剩多少字节
    uint32_t chunk_size;
    ChunkStream cs[65600];
    long count[32], bytes[32];
    int shown;
} Parser;

static uint32_t be(const uint8_t *p, int n) { uint32_t v = 0; while (n--) v = v << 8 | *p++; return v; }

// 打印 AMF0 编码的值，返回用掉的字节数
static int amf(const uint8_t *p, int n, int depth) {
    if (n < 1) return 0;
    switch (p[0]) {
    case 0x00: { uint64_t u = 0; for (int i = 1; i <= 8; i++) u = u << 8 | p[i];
                 double d; memcpy(&d, &u, 8); printf("%g", d); return 9; }
    case 0x01: printf(p[1] ? "true" : "false"); return 2;
    case 0x02: { int l = be(p + 1, 2); printf("\"%.*s\"", l, p + 3); return 3 + l; }
    case 0x05: printf("null"); return 1;
    case 0x03: case 0x08: {            // 对象、ECMA 数组：一串「键：值」，以 00 00 09 结束
        int i = p[0] == 0x08 ? 5 : 1;
        printf("{");
        while (i + 3 <= n && !(p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 9)) {
            int l = be(p + i, 2);
            printf("%s%.*s: ", i > 5 ? ", " : "", l, p + i + 2);
            i += 2 + l;
            int u = amf(p + i, n - i, depth + 1);
            if (!u) break;
            i += u;
        }
        printf("}");
        return i + 3;
    }
    }
    printf("(类型 %d)", p[0]);
    return n;
}

static const char *type_name(int t) {
    switch (t) {
    case 1: return "Set Chunk Size"; case 3: return "Acknowledgement"; case 4: return "User Control";
    case 5: return "Window Ack Size"; case 6: return "Set Peer Bandwidth"; case 8: return "Audio";
    case 9: return "Video"; case 18: return "Data(AMF0)"; case 20: return "Command(AMF0)";
    }
    return "?";
}

static void on_message(Parser *ps, ChunkStream *c, int csid) {
    ps->count[c->type & 31]++;
    ps->bytes[c->type & 31] += c->len;
    if (c->type == 1) ps->chunk_size = be(c->buf, 4) & 0x7fffffff;  // 之后这个方向的块按新大小切
    int media = c->type == 8 || c->type == 9;
    if (media && ps->count[c->type] > 3) return;   // 音视频只打印前几条
    if (ps->shown++ > 40) return;
    printf("[%6.3f] %s csid=%d %-15s 时间戳=%u 长度=%u 流=%u", now_s() - t0, ps->name, csid,
           type_name(c->type), c->ts, c->len, c->sid);
    if (c->type == 20 || c->type == 18) {
        printf("  ");
        for (uint32_t i = 0; i < c->len;) {
            int u = amf(c->buf + i, c->len - i, 0);
            if (!u) break;
            i += u;
            printf(" ");
        }
    } else if (c->type == 9) {
        // FLV 的 VideoTagHeader：帧类型、编码；AVC 再跟一个包类型和 CTS
        printf("  %s, AVC %s", (c->buf[0] >> 4) == 1 ? "关键帧" : "非关键帧",
               c->buf[1] == 0 ? "序列头(SPS/PPS)" : "NALU");
    } else if (c->type == 1 || c->type == 5 || c->type == 3) {
        printf("  %u", be(c->buf, 4));
    }
    printf("\n");
}

// 尽可能多地解析出完整的块
static void parse(Parser *ps) {
    for (;;) {
        uint8_t *p = ps->data + ps->pos;
        size_t n = ps->size - ps->pos;
        if (ps->handshake) {          // 握手：C0/S0 一个字节的版本号，C1/S1、C2/S2 各 1536 字节
            size_t k = n < (size_t)ps->handshake ? n : (size_t)ps->handshake;
            if (!k) return;
            if (ps->handshake == 1 + 1536 * 2)
                printf("[%6.3f] %s 握手开始 版本=%d\n", now_s() - t0, ps->name, p[0]);
            ps->handshake -= k;
            if (!ps->handshake) printf("[%6.3f] %s 握手完成，共 %d 字节\n", now_s() - t0, ps->name, 1 + 1536 * 2);
            ps->pos += k;
            continue;
        }
        if (n < 1) return;
        int fmt = p[0] >> 6, csid = p[0] & 0x3f, h = 1;
        if (csid == 0) { if (n < 2) return; csid = 64 + p[1]; h = 2; }
        else if (csid == 1) { if (n < 3) return; csid = 64 + p[1] + p[2] * 256; h = 3; }
        static const int mh[4] = {11, 7, 3, 0};
        if (n < (size_t)(h + mh[fmt])) return;
        ChunkStream *c = &ps->cs[csid];
        const uint8_t *m = p + h;
        uint32_t ts = fmt < 3 ? be(m, 3) : 0;
        int ext = fmt < 3 && ts == 0xffffff;
        if (n < (size_t)(h + mh[fmt] + ext * 4)) return;
        if (ext) ts = be(m + mh[fmt], 4);
        uint32_t len = c->len, type = c->type, sid = c->sid;
        if (fmt <= 1) { len = be(m + 3, 3); type = m[6]; }
        if (fmt == 0) sid = m[7] | m[8] << 8 | m[9] << 16 | (uint32_t)m[10] << 24;  // 流 ID 是小端
        uint32_t got = c->got;
        if (fmt < 3) got = 0;          // fmt 0/1/2 开始一条新消息，fmt 3 是上一条的续块
        uint32_t take = len - got < ps->chunk_size ? len - got : ps->chunk_size;
        size_t need = h + mh[fmt] + ext * 4 + take;
        if (n < need) return;
        if (fmt < 3) {
            c->ts = fmt == 0 ? ts : c->ts + ts;   // fmt 0 是绝对时间戳，1、2 是增量
            c->len = len; c->type = type; c->sid = sid;
            c->buf = realloc(c->buf, len ? len : 1);
        }
        memcpy(c->buf + got, p + need - take, take);
        c->got = got + take;
        ps->pos += need;
        if (c->got == c->len) { on_message(ps, c, csid); c->got = 0; }
    }
}

static void feed(Parser *ps, const uint8_t *d, size_t n) {
    ps->data = realloc(ps->data, ps->size + n);
    memcpy(ps->data + ps->size, d, n);
    ps->size += n;
    parse(ps);
    memmove(ps->data, ps->data + ps->pos, ps->size - ps->pos);
    ps->size -= ps->pos;
    ps->pos = 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "用法: %s 监听端口 服务器端口 [限速KB/s 开始秒 结束秒]\n", argv[0]); return 1; }
    double limit = argc > 5 ? atof(argv[3]) * 1024 : 0, lim_a = argc > 5 ? atof(argv[4]) : 0,
           lim_b = argc > 5 ? atof(argv[5]) : 0;
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);  // 按行输出，重定向到文件时也能及时看到
    int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(atoi(argv[1])),
                            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (bind(ls, (void *)&a, sizeof(a)) || listen(ls, 1)) { perror("bind"); return 1; }
    int cl = accept(ls, NULL, NULL);
    int sv = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_port = htons(atoi(argv[2]));
    if (connect(sv, (void *)&a, sizeof(a))) { perror("connect"); return 1; }
    t0 = now_s();

    static Parser up = {.name = "客户端→", .handshake = 1 + 1536 * 2, .chunk_size = 128};
    static Parser down = {.name = "←服务器", .handshake = 1 + 1536 * 2, .chunk_size = 128};
    uint8_t buf[65536];
    double budget = 0, last = now_s();
    for (;;) {
        // 限速：每过一段时间才允许转发相应数量的字节；不读 socket，数据就堵在服务器那边
        double t = now_s(), el = t - t0;
        int limited = limit > 0 && el >= lim_a && el < lim_b;
        budget += (t - last) * limit;
        if (budget > limit / 10) budget = limit / 10;
        last = t;
        struct pollfd pf[2] = {{cl, POLLIN, 0}, {sv, limited && budget < 1 ? 0 : POLLIN, 0}};
        poll(pf, 2, 5);
        if (pf[0].revents & (POLLIN | POLLHUP)) {
            ssize_t r = recv(cl, buf, sizeof(buf), 0);
            if (r <= 0) break;
            send(sv, buf, r, 0);
            feed(&up, buf, r);
        }
        if (pf[1].revents & (POLLIN | POLLHUP)) {
            size_t want = limited ? (size_t)budget : sizeof(buf);
            if (want > sizeof(buf)) want = sizeof(buf);
            ssize_t r = recv(sv, buf, want, 0);
            if (r <= 0) break;
            if (limited) budget -= r;
            send(cl, buf, r, 0);
            feed(&down, buf, r);
        }
    }
    Parser *all[2] = {&up, &down};
    for (int i = 0; i < 2; i++) {
        printf("%s 共计：", all[i]->name);
        for (int k = 0; k < 32; k++)
            if (all[i]->count[k]) printf(" %s %ld 条 %ld 字节;", type_name(k), all[i]->count[k], all[i]->bytes[k]);
        printf(" 块大小 %u\n", all[i]->chunk_size);
    }
    return 0;
}
```

</details>

把推流地址的端口改成 19350，让数据经过代理：

```bash
./rtmp_sniff 19350 1935 &
./live_push bbb-10s.mp4 rtmp://127.0.0.1:19350/live/sniff flv 1 seconds=5
```

代理的输出（方括号中是距离连接建立的秒数）：

```text
[ 0.000] 客户端→ 握手开始 版本=3
[ 0.000] ←服务器 握手开始 版本=3
[ 0.000] ←服务器 握手完成，共 3073 字节
[ 0.000] 客户端→ 握手完成，共 3073 字节
[ 0.000] 客户端→ csid=3 Command(AMF0)   时间戳=0 长度=139 流=0  "connect" 1 {app: "live", type: "nonprivate", flashVer: "FMLE/3.0 (compatible; Lavf63.1.102)", tcUrl: "rtmp://127.0.0.1:19350/live"}
[ 0.000] ←服务器 csid=2 Window Ack Size 时间戳=0 长度=4 流=0  2500000
[ 0.000] ←服务器 csid=2 Set Peer Bandwidth 时间戳=0 长度=5 流=0
[ 0.000] ←服务器 csid=2 Set Chunk Size  时间戳=0 长度=4 流=0  65536
[ 0.000] ←服务器 csid=3 Command(AMF0)   时间戳=0 长度=190 流=0  "_result" 1 {fmsVer: "LNX 9,0,124,2", capabilities: 31} {level: "status", code: "NetConnection.Connect.Success", description: "Connection succeeded.", objectEncoding: 0}
[ 0.000] 客户端→ csid=2 Set Chunk Size  时间戳=0 长度=4 流=0  65536
[ 0.000] 客户端→ csid=3 Command(AMF0)   时间戳=0 长度=34 流=0  "releaseStream" 2 null "sniff"
[ 0.000] 客户端→ csid=3 Command(AMF0)   时间戳=0 长度=30 流=0  "FCPublish" 3 null "sniff"
[ 0.000] 客户端→ csid=3 Command(AMF0)   时间戳=0 长度=25 流=0  "createStream" 4 null
[ 0.000] ←服务器 csid=3 Command(AMF0)   时间戳=0 长度=29 流=0  "_result" 4 null 1
[ 0.001] 客户端→ csid=8 Command(AMF0)   时间戳=0 长度=35 流=1  "publish" 5 null "sniff" "live"
[ 0.001] ←服务器 csid=5 Command(AMF0)   时间戳=0 长度=102 流=1  "onStatus" 5 null {level: "status", code: "NetStream.Publish.Start", description: "publish start"}
[ 0.001] 客户端→ csid=4 Data(AMF0)      时间戳=0 长度=179 流=1  "@setDataFrame" "onMetaData" {duration: 0, width: 1280, height: 720, videodatarate: 1953.12, videocodecid: 7, encoder: "Lavf63.1.102", filesize: 0}
[ 0.001] 客户端→ csid=6 Video           时间戳=0 长度=47 流=1  关键帧, AVC 序列头(SPS/PPS)
[ 0.009] 客户端→ csid=6 Video           时间戳=0 长度=37775 流=1  关键帧, AVC NALU
[ 0.053] 客户端→ csid=6 Video           时间戳=42 长度=337 流=1  非关键帧, AVC NALU
[ 4.972] 客户端→ csid=3 Command(AMF0)   时间戳=0 长度=32 流=0  "FCUnpublish" 6 null "sniff"
[ 4.972] 客户端→ csid=3 Command(AMF0)   时间戳=0 长度=34 流=0  "deleteStream" 7 null 1
客户端→ 共计： Set Chunk Size 1 条 4 字节; Video 122 条 1455943 字节; Data(AMF0) 1 条 179 字节; Command(AMF0) 7 条 329 字节; 块大小 65536
```

### 4.1 握手

连接建立后，双方各发 1 + 1536 + 1536 个字节：客户端发 C0（1 字节的版本号 3）、C1（1536 字节，含时间戳和随机数），服务器回 S0、S1、S2，客户端再发 C2。S2 是 C1 的回声，C2 是 S1 的回声，用来确认对方收到了。这些字节里没有任何音视频信息，握手完成后，才开始传 RTMP 消息。

### 4.2 块和消息

RTMP 的基本单位是**消息**（message），每条消息有类型、长度、时间戳和所属的流 ID。为了让大的视频消息不堵住小的控制消息，消息在发送时被切成**块**（chunk），不同消息的块可以交替发送。每个块前面有一个块头：

<figure class="diagram">
<svg viewBox="0 0 680 200" role="img" aria-label="RTMP 块的结构：块基本头 1 到 3 字节，包含 2 位格式 fmt 和块流 ID；然后是消息头，fmt 为 0 时 11 字节，含时间戳、消息长度、消息类型和流 ID，fmt 为 1 时 7 字节，fmt 为 2 时 3 字节只有时间戳增量，fmt 为 3 时没有消息头；之后是最多一个块大小的数据">
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="10" y="20" width="110" height="40" style="fill:var(--mark)"/>
      <rect x="120" y="20" width="90" height="40" style="fill:var(--accent-soft)"/>
      <rect x="210" y="20" width="90" height="40" style="fill:var(--accent-soft)"/>
      <rect x="300" y="20" width="60" height="40" style="fill:var(--accent-soft)"/>
      <rect x="360" y="20" width="90" height="40" style="fill:var(--accent-soft)"/>
      <rect x="450" y="20" width="220" height="40" style="fill:var(--panel-2)"/>
    </g>
    <text x="18" y="38" style="fill:var(--fg)">基本头</text><text x="18" y="53" style="fill:var(--muted);font-size:10px">fmt + 块流 ID</text>
    <text x="128" y="38" style="fill:var(--fg)">时间戳</text><text x="128" y="53" style="fill:var(--muted);font-size:10px">3 字节</text>
    <text x="218" y="38" style="fill:var(--fg)">消息长度</text><text x="218" y="53" style="fill:var(--muted);font-size:10px">3 字节</text>
    <text x="308" y="38" style="fill:var(--fg)">类型</text><text x="308" y="53" style="fill:var(--muted);font-size:10px">1</text>
    <text x="368" y="38" style="fill:var(--fg)">流 ID</text><text x="368" y="53" style="fill:var(--muted);font-size:10px">4，小端</text>
    <text x="460" y="38" style="fill:var(--fg)">数据</text><text x="460" y="53" style="fill:var(--muted);font-size:10px">最多「块大小」个字节</text>
    <text x="120" y="80" style="fill:var(--muted);font-size:11px">└──────── 消息头：fmt=0 时 11 字节 ────────┘</text>
    <text x="10" y="112" style="fill:var(--fg);font-size:11px">fmt=0：完整的消息头，新消息的第一个块</text>
    <text x="10" y="132" style="fill:var(--fg);font-size:11px">fmt=1：7 字节，省掉流 ID（和上一条消息相同），时间戳改成增量</text>
    <text x="10" y="152" style="fill:var(--fg);font-size:11px">fmt=2：3 字节，只有时间戳增量（长度、类型也和上一条相同）</text>
    <text x="10" y="172" style="fill:var(--fg);font-size:11px">fmt=3：没有消息头，是上一个块的续块，或者和上一条消息完全一样的新消息</text>
    <text x="10" y="192" style="fill:var(--muted);font-size:11px">时间戳超过 0xFFFFFF（约 4.6 小时）时写 0xFFFFFF，后面再跟 4 字节的扩展时间戳</text>
  </g>
</svg>
</figure>

块流 ID（csid）区分不同的「块流」，每个块流记住上一条消息的头，后面的块就可以省略相同的部分。从输出看，控制消息在 csid 2，命令在 3（`publish` 用了 8），元数据在 4，视频在 6。

块大小默认是 128 字节，一个 37775 字节的关键帧要切成近 300 块。所以连接一建立，双方都用 **Set Chunk Size** 把块大小改成了 65536，此后一个关键帧通常一个块就装下了（超过 65536 字节的大关键帧切成两块）。

### 4.3 命令

消息类型 20 是 **AMF0 命令**。AMF0 是 Flash 的序列化格式：一个类型字节加数据，`0x02` 是字符串、`0x00` 是 8 字节的 double、`0x03` 是对象、`0x05` 是 null。代理把它们解码出来，推流的过程一目了然：

1. **connect**：连接到应用 `live`（URL 中的第一段路径）。服务器回复窗口大小、带宽限制，最后用 `_result` 表示连接成功。
2. **releaseStream、FCPublish**：Flash 时代的遗留命令，很多服务器会忽略。
3. **createStream**：创建一个流，服务器返回流 ID 1，之后的音视频消息都属于流 1。
4. **publish**：开始推流，流名 `sniff`，类型 `live`。服务器回 `onStatus`，`NetStream.Publish.Start`。
5. 然后是数据：**@setDataFrame/onMetaData**（类型 18，FLV 的 Script Tag），**AVC 序列头**（47 字节，里面是 SPS 和 PPS，就是 FLV 的 AVCDecoderConfigurationRecord），接着是一帧一帧的视频（类型 9）。
6. 结束时 **FCUnpublish、deleteStream**。

时间戳一项：第一帧 0，第二帧 42，就是第 2 章讲过的 FLV 时间戳（毫秒）。5 秒推了 120 帧，加上序列头一共 122 条视频消息，1.46 MB。

拉流的过程类似，`publish` 换成 `play`。把 `latency_probe` 也接到代理上，可以看到：

```text
[ 0.001] 客户端→ csid=8 Command(AMF0)   时间戳=0 长度=33 流=1  "play" 4 null "test" -2000
[ 0.001] ←服务器 csid=5 Command(AMF0)   时间戳=0 长度=96 流=1  "onStatus" 4 null {level: "status", code: "NetStream.Play.Start", description: "play start"}
[ 0.001] ←服务器 csid=4 Data(AMF0)      时间戳=0 长度=127 流=1  "@setDataFrame" "onMetaData" {videocodecid: 7, videodatarate: 0, audiocodecid: 0, audiodatarate: 0}
[ 0.001] ←服务器 csid=6 Video           时间戳=0 长度=47 流=1  关键帧, AVC 序列头(SPS/PPS)
[ 0.519] ←服务器 csid=6 Video           时间戳=1350000 长度=114987 流=1  关键帧, AVC NALU
[ 0.557] ←服务器 csid=6 Video           时间戳=1350042 长度=252 流=1  非关键帧, AVC NALU
```

序列头马上就来了，第一帧视频却等了 0.519 秒，而且是关键帧：**MediaMTX 从下一个关键帧开始发**，在此之前的非关键帧发了也没用，解码器解不了。它的时间戳是 1350000，说明这时距离推流开始已经过去 22.5 分钟了。

## 5. 延迟测量

推流程序一直推着，用 `latency_probe` 从各个角度测一测。

### 5.1 播放器的探测参数

先用 FFmpeg 的默认参数拉流：

```text
$ ./latency_probe rtmp://127.0.0.1/live/test 15 play=1
打开到第一帧 5220 ms
帧数 361  坏帧 0  卡顿 0 次  播放延迟 最小 5167 / 中位 5177 / 最大 5189 / 最后 5186 ms
```

本机上推拉流，延迟竟然有 **5.2 秒**，而且一直保持着。用到达延迟看得更清楚：

```text
$ ./latency_probe rtmp://127.0.0.1/live/test 10
打开到第一帧 5643 ms
帧数 366  坏帧 0  卡顿 0 次  到达延迟 最小 1 / 中位 4 / 最大 5172 / 最后 4 ms
```

最大 5172 毫秒，中位数却只有 4 毫秒：开头的一批帧延迟很大，后来就正常了。原因在 `avformat_find_stream_info`：它要读一段数据，分析出每一路流的编码参数和帧率才返回，FLV 默认要分析 5 秒（`analyzeduration` 默认 5000000 微秒）。直播流是实时到来的，读 5 秒的数据就得等 5 秒。读到的包被缓存起来，返回后先交给我们。按到达时间算，程序很快把这 5 秒的积压解码完，延迟就降下来了；可播放器要按 PTS 一帧一帧地播，**这 5 秒的积压永远追不回来**。用 `ffplay` 直接播 RTMP 直播流，常常延迟好几秒，就是这个原因。

调小探测参数：

```text
$ ./latency_probe rtmp://127.0.0.1/live/test 15 play=1 analyzeduration=0 probesize=32
打开到第一帧 785 ms
帧数 361  坏帧 0  卡顿 0 次  播放延迟 最小 10 / 中位 17 / 最大 28 / 最后 12 ms
```

延迟降到 **17 毫秒**。代价是 FFmpeg 拿不到完整的流信息，比如帧率可能算不出来（日志会提示 `not enough frames to estimate rate`），播放器要能应付这种情况。

很多文章推荐「低延迟播放」时再加上 `-fflags nobuffer`。实测它有副作用：

| 参数 | GOP 1 秒：打开到第一帧（毫秒） | GOP 4 秒：打开到第一帧（毫秒） |
|---|---|---|
| `analyzeduration=0 probesize=32` | 169～1008，平均 520 | 695～3397，平均 1900 |
| 再加 `fflags=nobuffer` | 1078～1549，平均 1330 | 4551～7569，平均 6580 |

每组随机挑时间连接 8 次。不加 `nobuffer` 时，打开时间平均是半个 GOP：上面看到了，服务器要等下一个关键帧才开始发，等待时间在 0 到一个 GOP 之间。加了 `nobuffer`，打开时间**多出整整一个 GOP**。原因是 `nobuffer` 的意思是「分析阶段读到的包不缓存」，直接扔掉：`avformat_find_stream_info` 读到的第一个包正是那个关键帧，它被扔掉了，解码器只好等下一个关键帧。在程序里打印每个包的标志可以证实这一点：不加 `nobuffer` 时，`find_stream_info` 返回后读到的第一个包就是关键帧；加了以后，要再过一个 GOP 才出现关键帧。省掉的那点缓冲（几毫秒）和多等的一个 GOP 相比，得不偿失。

### 5.2 GOP 的影响

上表已经能看出：**GOP 越长，打开越慢**。直播常用 1～2 秒的 GOP，就是为了让观众点进来很快就能看到画面。

有些服务器（比如 SRS 的 `gop_cache`）会缓存最近的一个 GOP，新观众一连上就把整个 GOP 发过去，画面立刻出来。但这样做是用延迟换打开速度：播放器从 GOP 的开头播起，比直播落后了最多一个 GOP，而且像 5.1 节一样，这段落后不会自己追回来（除非播放器会「追帧」，第 6 节讲）。MediaMTX 不缓存 GOP，所以打开慢一点，延迟低。

### 5.3 编码器的延迟

推流端关掉 `tune=zerolatency` 再测：

```text
$ ./live_push bbb-10s.mp4 rtmp://127.0.0.1/live/nz flv 1 zerolatency=0
$ ./latency_probe rtmp://127.0.0.1/live/nz 10 play=1 analyzeduration=0 probesize=32
帧数 241  坏帧 0  卡顿 0 次  播放延迟 最小 1220 / 中位 1224 / 最大 1229 / 最后 1224 ms
```

延迟从 17 毫秒变成 **1224 毫秒**。推流程序的日志中，x264 的参数是 `threads=18`、`rc_lookahead=10`，B 帧已经关掉了：帧级多线程要攒 18 帧才能让所有线程都忙起来，码率控制还要往前看 10 帧，加起来约 28 帧，正好 28 / 24 ≈ 1.17 秒。第 3 章测过开着 B 帧的默认参数，延迟高达 62 帧。`zerolatency` 把这些都关掉了，改用片级多线程（`sliced_threads=1`）：一帧切成 11 个片（slice），由多个线程同时编码。

### 5.4 本机上的延迟由什么组成

17 毫秒里有：等待到下一个推流时刻的误差、编码一帧（720p，veryfast 档，几毫秒）、TCP 传给服务器、服务器转发、TCP 传给播放器、解码一帧。在本机上它们都很小。真实的网络上要加上**传输时间**（同城几毫秒，跨国一两百毫秒），以及下一节的**排队时间**，后者往往是大头。

## 6. 网络变差时

TCP 不丢数据，网速跟不上的时候，数据就在发送端的缓冲区、服务器的队列里排队，排队就是延迟。代理程序有一个限速功能：在指定的时间段里，把「服务器→客户端」方向的速度限制住。推流的码率约 2.2 Mbps（276 KB/s），把拉流限速到 150 KB/s，持续 5 秒，看看两种延迟怎样变化：

```bash
./rtmp_sniff 19351 1935 150 5 10 > /dev/null &
./latency_probe rtmp://127.0.0.1:19351/live/test 25 play=1 log=play.log analyzeduration=0 probesize=32
```

<figure class="diagram">
<svg viewBox="0 0 680 230" role="img" aria-label="限速期间延迟的变化：限速 5 秒，按到达时间算的延迟升到约 2.2 秒，限速一解除就回到十几毫秒；按 PTS 匀速播放时延迟升到 2.5 秒后一直保持，再也降不下来">
  <g style="font-family:var(--mono);font-size:12px">
<rect x="153.2" y="15" width="136.4" height="170" style="fill:var(--mark);opacity:0.6"/>
<text x="221.4" y="29" text-anchor="middle" style="fill:var(--muted);font-size:10px">限速 150 KB/s</text>
<line x1="55" y1="185.0" x2="655" y2="185.0" style="stroke:var(--border);"/><text x="49" y="189.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0</text>
<line x1="55" y1="156.7" x2="655" y2="156.7" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="160.7" text-anchor="end" style="fill:var(--muted);font-size:10px">500</text>
<line x1="55" y1="128.3" x2="655" y2="128.3" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="132.3" text-anchor="end" style="fill:var(--muted);font-size:10px">1000</text>
<line x1="55" y1="100.0" x2="655" y2="100.0" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="104.0" text-anchor="end" style="fill:var(--muted);font-size:10px">1500</text>
<line x1="55" y1="71.7" x2="655" y2="71.7" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="75.7" text-anchor="end" style="fill:var(--muted);font-size:10px">2000</text>
<line x1="55" y1="43.3" x2="655" y2="43.3" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="47.3" text-anchor="end" style="fill:var(--muted);font-size:10px">2500</text>
<line x1="55" y1="15.0" x2="655" y2="15.0" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="49" y="19.0" text-anchor="end" style="fill:var(--muted);font-size:10px">3000</text>
<text x="55.0" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">0s</text>
<text x="109.5" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">2s</text>
<text x="164.1" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">4s</text>
<text x="218.6" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">6s</text>
<text x="273.2" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">8s</text>
<text x="327.7" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">10s</text>
<text x="382.3" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">12s</text>
<text x="436.8" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">14s</text>
<text x="491.4" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">16s</text>
<text x="545.9" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">18s</text>
<text x="600.5" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">20s</text>
<text x="655.0" y="199" text-anchor="middle" style="fill:var(--muted);font-size:10px">22s</text>
<path d="M55.0,184.5 L57.7,184.8 L60.5,184.9 L63.2,184.7 L65.9,184.7 L68.6,184.7 L71.4,184.8 L74.1,184.5 L76.8,184.7 L79.5,184.7 L82.3,184.4 L85.0,184.8 L87.7,184.8 L90.5,184.8 L93.2,184.8 L95.9,184.8 L98.6,184.8 L101.4,184.8 L104.1,184.8 L106.8,184.7 L109.5,184.3 L112.3,184.7 L115.0,184.7 L117.7,184.8 L120.5,184.7 L123.2,184.7 L125.9,184.8 L128.6,184.8 L131.4,184.9 L134.1,184.7 L136.8,184.6 L139.5,184.8 L142.3,184.8 L145.0,184.4 L147.7,184.8 L150.5,184.8 L153.2,184.7 L155.9,184.7 L158.6,184.3 L161.4,184.7 L164.1,184.7 L169.5,172.2 L172.3,171.9 L175.0,171.4 L177.7,169.7 L180.5,167.3 L183.2,166.1 L185.9,163.7 L188.6,161.9 L191.4,161.4 L194.1,162.2 L196.8,162.4 L199.5,162.6 L207.7,148.5 L224.1,113.8 L226.8,114.5 L229.5,114.4 L232.3,113.2 L235.0,110.4 L237.7,108.9 L240.5,107.4 L243.2,107.4 L245.9,106.1 L248.6,104.4 L251.4,102.5 L254.1,101.4 L256.8,99.9 L259.5,98.7 L273.2,70.5 L275.9,70.9 L278.6,69.4 L281.4,68.1 L284.1,65.4 L286.8,63.5 L289.5,61.8 L292.3,128.7 L295.0,184.7 L297.7,184.8 L300.5,184.8 L303.2,184.7 L305.9,184.8 L308.6,184.8 L311.4,184.8 L314.1,184.6 L316.8,184.8 L319.5,184.6 L322.3,184.8 L325.0,184.8 L327.7,184.5 L330.5,184.9 L333.2,184.7 L335.9,184.8 L338.6,184.7 L341.4,184.8 L344.1,184.7 L346.8,184.7 L349.5,184.2 L352.3,184.7 L355.0,184.7 L357.7,184.6 L360.5,184.7 L363.2,184.9 L365.9,184.4 L368.6,184.8 L371.4,184.8 L374.1,184.7 L376.8,184.7 L379.5,184.7 L382.3,184.8 L385.0,184.8 L387.7,184.8 L390.5,184.3 L393.2,184.8 L395.9,184.4 L398.6,184.8 L401.4,184.7 L404.1,184.5 L406.8,184.7 L409.5,184.6 L412.3,184.8 L415.0,184.8 L417.7,184.9 L420.5,184.8 L423.2,184.8 L425.9,184.7 L428.6,184.6 L431.4,184.5 L434.1,184.6 L436.8,184.5 L439.5,184.7 L442.3,184.8 L445.0,184.7 L447.7,184.6 L450.5,184.8 L453.2,184.7 L455.9,184.7 L458.6,184.8 L461.4,184.4 L464.1,184.5 L466.8,184.6 L469.5,184.4 L472.3,184.7 L475.0,184.7 L477.7,184.7 L480.5,184.8 L483.2,184.5 L485.9,184.5 L488.6,184.5 L491.4,184.8 L494.1,184.7 L496.8,184.7 L499.5,184.5 L502.3,184.7 L505.0,184.7 L507.7,184.5 L510.5,184.5 L513.2,184.5 L515.9,184.5 L518.6,184.6 L521.4,184.8 L524.1,184.5 L526.8,184.6 L529.5,184.6 L532.3,184.8 L535.0,184.5 L537.7,184.6 L540.5,184.6 L543.2,184.7 L545.9,184.4 L548.6,184.7 L551.4,184.8 L554.1,184.7 L556.8,184.7 L559.5,184.6 L562.3,184.5 L565.0,184.6 L567.7,184.8 L570.5,184.4 L573.2,184.4 L575.9,184.7 L578.6,184.5 L581.4,184.8 L584.1,184.4 L586.8,184.7 L589.5,184.8 L592.3,184.6 L595.0,184.8 L597.7,184.5 L600.5,184.7 L603.2,184.8 L605.9,184.8 L608.6,184.8 L611.4,184.7 L614.1,184.8 L616.8,184.8 L619.5,184.6 L622.3,184.7 L625.0,184.8 L627.7,184.0 L630.5,184.8 L633.2,184.8 L635.9,184.3 L638.6,184.8 L641.4,184.8 L644.1,184.8 L646.8,184.8 L649.5,184.7 L652.3,184.7 L655.0,184.7" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
<path d="M55.0,184.3 L57.7,184.2 L60.5,184.2 L63.2,183.9 L65.9,183.4 L68.6,184.3 L71.4,183.5 L74.1,183.8 L76.8,184.0 L79.5,183.6 L82.3,183.3 L85.0,184.1 L87.7,183.6 L90.5,184.1 L93.2,183.9 L95.9,183.4 L98.6,184.2 L101.4,184.2 L104.1,183.2 L106.8,184.1 L109.5,184.1 L112.3,184.1 L115.0,183.8 L117.7,184.0 L120.5,183.5 L123.2,184.2 L125.9,184.3 L128.6,183.9 L131.4,184.3 L134.1,184.1 L136.8,184.0 L139.5,184.1 L142.3,183.7 L145.0,184.1 L147.7,183.8 L150.5,184.2 L153.2,183.1 L155.9,182.1 L158.6,180.5 L161.4,179.3 L164.1,177.5 L166.8,176.4 L183.2,146.2 L185.9,146.3 L188.6,146.7 L191.4,144.5 L194.1,142.5 L196.8,141.1 L199.5,138.6 L202.3,137.4 L205.0,135.1 L207.7,133.4 L210.5,129.6 L213.2,127.1 L215.9,124.7 L218.6,123.0 L221.4,118.4 L235.0,93.8 L237.7,91.6 L240.5,89.8 L243.2,88.9 L245.9,85.7 L248.6,83.9 L251.4,80.8 L254.1,79.2 L256.8,74.8 L259.5,73.1 L262.3,71.7 L265.0,67.3 L267.7,66.3 L270.5,64.1 L273.2,60.4 L275.9,58.2 L278.6,56.8 L286.8,43.3 L289.5,43.1 L292.3,43.0 L295.0,43.1 L297.7,43.1 L300.5,42.4 L303.2,43.0 L305.9,42.9 L308.6,42.7 L311.4,42.8 L314.1,42.9 L316.8,42.9 L319.5,42.8 L322.3,42.8 L325.0,42.8 L327.7,42.9 L330.5,42.3 L333.2,43.4 L335.9,42.6 L338.6,43.1 L341.4,43.0 L344.1,43.1 L346.8,43.1 L349.5,42.7 L352.3,42.7 L355.0,42.7 L357.7,42.8 L360.5,42.7 L363.2,42.8 L365.9,42.6 L368.6,42.5 L371.4,43.1 L374.1,42.5 L376.8,43.0 L379.5,42.8 L382.3,42.9 L385.0,42.3 L387.7,42.7 L390.5,42.9 L393.2,43.1 L395.9,42.8 L398.6,42.7 L401.4,42.7 L404.1,42.6 L406.8,43.2 L409.5,42.8 L412.3,43.1 L415.0,43.1 L417.7,42.8 L420.5,42.9 L423.2,42.4 L425.9,42.5 L428.6,42.4 L431.4,42.7 L434.1,42.8 L436.8,43.0 L439.5,43.3 L442.3,42.5 L445.0,42.9 L447.7,42.9 L450.5,42.8 L453.2,42.6 L455.9,42.5 L458.6,43.0 L461.4,42.9 L464.1,42.4 L466.8,43.1 L469.5,43.3 L472.3,43.0 L475.0,43.2 L477.7,43.1 L480.5,42.9 L483.2,42.5 L485.9,42.6 L488.6,43.2 L491.4,42.8 L494.1,43.1 L496.8,43.0 L499.5,43.1 L502.3,43.3 L505.0,43.2 L507.7,43.4 L510.5,43.1 L513.2,42.9 L515.9,43.0 L518.6,42.5 L521.4,43.3 L524.1,43.1 L526.8,43.1 L529.5,42.9 L532.3,42.8 L535.0,43.1 L537.7,42.5 L540.5,43.0 L543.2,42.9 L545.9,42.9 L548.6,43.2 L551.4,42.2 L554.1,42.6 L556.8,43.2 L559.5,42.6 L562.3,43.0 L565.0,42.7 L567.7,42.7 L570.5,42.8 L573.2,42.8 L575.9,42.8 L578.6,43.1 L581.4,43.0 L584.1,42.8 L586.8,43.1 L589.5,43.1 L592.3,42.9 L595.0,43.1 L597.7,43.2 L600.5,42.7 L603.2,42.9 L605.9,42.9 L608.6,42.8 L611.4,42.6 L614.1,42.9 L616.8,43.1 L619.5,42.8 L622.3,42.6 L625.0,43.2 L627.7,42.4 L630.5,43.1 L633.2,42.7 L635.9,43.1 L638.6,42.8 L641.4,42.4 L644.1,42.7 L646.8,42.8 L649.5,42.6 L652.3,43.0 L655.0,42.5" fill="none" style="stroke:var(--danger)" stroke-width="2"/>
    <text x="60" y="12" style="fill:var(--muted);font-size:10px">延迟（毫秒）</text>
<line x1="55" y1="218" x2="81" y2="218" style="stroke:var(--danger)" stroke-width="2"/><text x="87" y="222" style="fill:var(--muted);font-size:11px">按 PTS 匀速播放</text>
<line x1="355" y1="218" x2="381" y2="218" style="stroke:var(--accent)" stroke-width="2"/><text x="387" y="222" style="fill:var(--muted);font-size:11px">收到就算（到达延迟）</text>
  </g>
</svg>
</figure>

- **到达延迟**（绿线）：限速期间数据只能慢慢过来，延迟涨到 2.2 秒；限速一解除，积压的数据一口气涌过来，延迟马上回到十几毫秒。
- **播放延迟**（红线）：限速期间画面卡顿了 43 次，延迟涨到 2.5 秒。限速解除后，数据虽然到了，但播放器按 PTS 一帧一帧地播，播放速度和直播的速度一样，**落下的 2.5 秒永远追不回来**。网络每抖动一次，延迟就增加一截。

这是基于 TCP 的直播协议（RTMP、HTTP-FLV、HLS）的共同问题。常见的对策：

- **播放器追帧**：发现缓冲的数据太多（延迟太大），就加快播放速度（比如 1.1 倍速，声音做变速不变调），或者直接丢掉一些帧，把延迟追回来。各家直播 App 的播放器基本都会这样做。
- **服务器丢帧**：发给某个观众的队列太长时，丢掉一部分非关键帧，甚至整个 GOP。
- **降低码率**：网速不够，就换一档低码率的流（自适应码率，下一篇的 HLS 天生支持）。
- **换用 UDP 上的协议**：SRT、WebRTC 自己控制重传，网络差时宁可丢掉来不及的数据，也不让延迟无限增长（第 5 篇）。

## 7. 小结

- RTMP 跑在 TCP 上：握手（C0～C2、S0～S2）之后传消息，消息切成块发送，块头可以省略和上一条相同的字段。音视频消息的内容就是 FLV 的 Tag。
- 推流的命令序列：connect、createStream、publish，然后是元数据、AVC 序列头和视频帧；拉流用 play。
- 测延迟的好办法：推流端把时间画进画面，拉流端读出来。同一台电脑上不需要对时。
- 播放端：默认的 `analyzeduration` 会带来 5 秒的固定延迟，要调小；`fflags nobuffer` 会扔掉第一个关键帧，让打开时间多一个 GOP。
- GOP 决定打开速度，服务器的 GOP 缓存用延迟换打开速度。
- 编码器不用 zerolatency，多线程和前瞻会带来 1 秒以上的延迟。
- TCP 上网络一抖动，延迟就积累起来，播放器要靠追帧把它追回来。

## 练习

1. 拉流时客户端发出的 `play` 命令带了一个参数 `-2000`，它是什么意思？
2. 如果双方都不修改块大小（保持默认的 128 字节），推一个 37775 字节的关键帧，要切成多少个块？块头一共多占多少字节？
3. 用 `ffplay` 播放 RTMP 直播流，想要延迟低、打开又快，应该加哪些参数？
4. 推流程序按帧率等待时用的是 `av_gettime_relative()`，写进条码的却是 `CLOCK_REALTIME`，为什么不统一用一个？

<details>
<summary>点击查看答案</summary>

1. `play` 命令的参数依次是：命令名、事务 ID（4）、null、流名、开始位置。RTMP 规范中，开始位置 -2 表示「先找同名的直播流，没有的话播放同名的录像」，-1 表示「只播直播流」，0 或正数表示从录像的这个位置开始播。FFmpeg 的 `rtmp_live` 选项默认是 `any`，对应 -2；设成 `live` 对应 -1，设成 `recorded` 对应 0。FFmpeg 发送时把这个值乘了 1000，所以代理看到的是 -2000。

2. 第一个块带 fmt=0 的完整块头：1 字节基本头 + 11 字节消息头，装 128 字节数据；之后每个块是 fmt=3，只有 1 字节的基本头。块数是 ⌈37775 / 128⌉ = 296，块头一共 12 + 295 = 307 字节，约占 0.8%。改成 65536 后只有 1 个块，块头 12 字节。开销其实不大，改大块大小更主要的好处是减少了收发两端拆块、拼块的次数。

3. `ffplay -analyzeduration 0 -probesize 32 -flags low_delay -framedrop rtmp://...`。前两个参数去掉 5 秒的探测积压；`-flags low_delay` 让解码器不为 B 帧的重排序多等（流里没有 B 帧时才可以用）；`-framedrop` 允许落后时丢帧。不要加 `-fflags nobuffer`，它会让打开时间多出一个 GOP。即便如此，ffplay 也不会像直播 App 那样主动追帧，网络抖动后积累的延迟仍然会保留。

4. 两个时钟用途不同。`av_gettime_relative()` 是单调时钟，只会匀速往前走，适合计算「还要等多久」：如果系统时间被 NTP 校正、或者被人手动改了，单调时钟不受影响，推流的节奏不会乱。条码要拿到另一个程序里比较，两边必须用同一个起点的时钟；`CLOCK_REALTIME` 是从 1970 年开始的系统时间，所有程序都一样，浏览器里 JavaScript 的 `Date.now()` 也是它，第 5 篇在浏览器中读条码时正好用上。代价是系统时间可能被校正，测量过程中如果恰好发生一次校正，那一段的延迟数字就不准了。在同一台电脑上做短时间的测量，这个风险可以忽略。

</details>
