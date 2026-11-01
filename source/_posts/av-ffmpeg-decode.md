---
title: 解封装与解码
date: 2026-11-01 10:00:00
updated: 2026-11-01 10:00:00
categories:
  - 音视频开发
  - FFmpeg编程
tags:
  - 音视频
  - FFmpeg
  - 解码
  - C
order: 2
description: 用 FFmpeg 9 的 API 解码测试视频：av_read_frame 读出一个个包，avcodec_send_packet 送进解码器，avcodec_receive_frame 取出画面，按 linesize 一行行写成 YUV，结果和命令行解出的文件 MD5 完全相同。解码器有延迟：单线程时送入 3 个包才出第一帧，最后要送入 NULL 冲刷出剩下的 2 帧；自动开 13 个线程后速度从 816 帧/秒提高到 3239 帧/秒，延迟却变成 15 个包。宽度改成 1000 时 linesize 是 1024，把整行都写出去，画面就碎成一片斜条纹。音频解码出的是平面格式 fltp，自己交错成 f32le，也和命令行逐字节相同。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `av_read_frame` 读包、用 `avcodec_send_packet` / `avcodec_receive_frame` 解码；理解 `AVPacket` 和 `AVFrame`；理解解码器的延迟和冲刷；会正确处理 `linesize`；会解码音频，理解平面格式；了解多线程解码的速度和代价。
>
> **前置知识**：上一篇《FFmpeg 的库与开发环境》；第 1 章的 YUV、PCM；第 2 章的 PTS、DTS 和 B 帧。
>
> **实验环境**：macOS 27.0（Apple M6，12 核），Apple Clang 21，FFmpeg 9.0.2。编译命令同上一篇，后面不再重复：

```bash
FF=$(brew --prefix ffmpeg)
clang -O2 ff_decode.c -o ff_decode -I$FF/include -L$FF/lib -lavformat -lavcodec -lavutil
```

## 1. 包和帧

解码的过程就是第 1 章第 4 篇那张图的前半段：解封装得到**包**，解码得到**帧**。FFmpeg 中对应两个结构体：

| | `AVPacket`（包） | `AVFrame`（帧） |
|---|---|---|
| 内容 | 压缩后的数据：一帧 H.264、一个 AAC 帧 | 解码后的原始数据：YUV 图像、PCM 采样 |
| 来自 | 解封装器（`av_read_frame`），或编码器的输出 | 解码器的输出，或编码器的输入 |
| 主要字段 | `data`、`size`、`pts`、`dts`、`stream_index`、`flags`（关键帧） | `data[]`、`linesize[]`、`width`、`height`、`format`、`pts`、`nb_samples` |
| 大小 | 几百字节到几百 KB | 一帧 720p YUV 是 1.38 MB |

两者的数据都是**引用计数**的：`av_packet_unref`、`av_frame_unref` 释放对数据的引用，结构体本身可以反复使用，最后用 `av_packet_free`、`av_frame_free` 释放。这样做的好处是包和帧在各个环节之间传递时不需要复制数据。

### 1.1 send 和 receive

解码 API 是两个函数：

- `avcodec_send_packet(dec, pkt)`：把一个包送进解码器；
- `avcodec_receive_frame(dec, frame)`：从解码器取出一帧。

为什么要拆成两个？因为包和帧不是一一对应的。上一章看到，有 B 帧时，送入 I 帧和 P 帧之后，要等 B 帧也到了，才能按显示顺序输出 I、B、B、P；送进去一个包，可能得到 0 帧，也可能得到好几帧。新 API 把这件事交给调用者：送一个包，然后循环取帧，直到解码器说「暂时没有了」（返回 `AVERROR(EAGAIN)`）。

<figure class="diagram">
<svg viewBox="0 0 680 230" role="img" aria-label="解码循环：av_read_frame 读出一个包，avcodec_send_packet 送进解码器，然后循环调用 avcodec_receive_frame 取出所有可以取出的帧，直到返回 EAGAIN，再读下一个包；文件读完后送入 NULL 冲刷，循环取帧直到返回 EOF">
  <defs>
    <marker id="dec-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="10" y="20" width="140" height="40" rx="6" style="fill:var(--panel-2)"/>
      <rect x="200" y="20" width="170" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="420" y="20" width="200" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="10" y="140" width="140" height="40" rx="6" style="fill:var(--mark)"/>
      <rect x="200" y="140" width="170" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="420" y="140" width="200" height="40" rx="6" style="fill:var(--accent-soft)"/>
    </g>
    <text x="22" y="45" style="fill:var(--fg)">av_read_frame</text>
    <text x="212" y="45" style="fill:var(--fg)">send_packet(pkt)</text>
    <text x="432" y="45" style="fill:var(--fg)">receive_frame(frame)</text>
    <text x="30" y="165" style="fill:var(--fg)">文件读完</text>
    <text x="212" y="165" style="fill:var(--fg)">send_packet(NULL)</text>
    <text x="432" y="165" style="fill:var(--fg)">receive_frame(frame)</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#dec-arrow)">
      <line x1="150" y1="40" x2="198" y2="40"/><line x1="370" y1="40" x2="418" y2="40"/>
      <path d="M620,32 C660,10 660,60 622,50"/>
      <path d="M520,62 C520,100 80,100 80,62"/>
      <line x1="80" y1="62" x2="80" y2="138"/>
      <line x1="150" y1="160" x2="198" y2="160"/><line x1="370" y1="160" x2="418" y2="160"/>
      <path d="M620,152 C660,130 660,180 622,170"/>
    </g>
    <text x="596" y="14" style="fill:var(--muted);font-size:11px">得到一帧</text>
    <text x="250" y="96" style="fill:var(--muted);font-size:11px">返回 EAGAIN：需要下一个包</text>
    <text x="88" y="120" style="fill:var(--muted);font-size:11px">返回 EOF</text>
    <text x="540" y="128" style="fill:var(--muted);font-size:11px">得到剩下的帧</text>
    <text x="440" y="205" style="fill:var(--muted);font-size:11px">返回 EOF：全部结束</text>
  </g>
</svg>
</figure>

文件读完之后，还要送入一个 `NULL` 包，告诉解码器「没有更多的数据了」，这叫**冲刷**（flush）。解码器会把内部还没输出的帧全部吐出来，最后返回 `AVERROR_EOF`。忘了这一步，视频最后几帧就丢了。

## 2. 解码视频

### 2.1 程序

```c
// ff_decode.c：解封装并解码视频，把每一帧存成 YUV420P
#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>

static long packets_in = 0, frames_out = 0, first_out = -1;

// 把解码器中所有已经可以取出的帧都取出来
static int drain(AVCodecContext *dec, AVFrame *frame, FILE *out, int show)
{
    int ret;
    while ((ret = avcodec_receive_frame(dec, frame)) >= 0) {
        if (first_out < 0) first_out = packets_in;
        if (frames_out < show)
            printf("帧 %3ld：送入 %3ld 个包后得到，pts=%6lld，%c 帧%s，linesize = %d/%d/%d\n",
                   frames_out, packets_in, (long long)frame->pts, av_get_picture_type_char(frame->pict_type),
                   (frame->flags & AV_FRAME_FLAG_KEY) ? "（关键帧）" : "",
                   frame->linesize[0], frame->linesize[1], frame->linesize[2]);
        // 一行一行地写：每行的有效数据是 width 字节，但每行实际占 linesize 字节
        for (int p = 0; p < 3; p++) {
            int w = p ? frame->width / 2 : frame->width, h = p ? frame->height / 2 : frame->height;
            for (int y = 0; y < h; y++)
                fwrite(frame->data[p] + (long)y * frame->linesize[p], 1, w, out);
        }
        frames_out++;
        av_frame_unref(frame);
    }
    return ret;     // AVERROR(EAGAIN)：需要更多的包；AVERROR_EOF：冲刷完毕
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "用法: %s 输入 输出.yuv [线程数] [显示几帧]\n", argv[0]); return 1; }
    int threads = argc > 3 ? atoi(argv[3]) : 0, show = argc > 4 ? atoi(argv[4]) : 8;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);

    // 找到「最好的」视频流，同时得到对应的解码器
    const AVCodec *codec = NULL;
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (vs < 0) { fprintf(stderr, "没有视频流\n"); return 1; }

    // 创建解码器上下文，把流的参数（宽高、extradata 等）复制进去，然后打开
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[vs]->codecpar);
    dec->pkt_timebase = fmt->streams[vs]->time_base;
    dec->thread_count = threads;                     // 0 表示自动选择
    if (avcodec_open2(dec, codec, NULL) < 0) return 1;
    printf("解码器 %s，%dx%d，%s，线程 %d\n", codec->name, dec->width, dec->height,
           av_get_pix_fmt_name(dec->pix_fmt), dec->thread_count);

    FILE *out = fopen(argv[2], "wb");
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int64_t t0 = av_gettime_relative();
    while (av_read_frame(fmt, pkt) >= 0) {           // 读出一个包（一帧压缩数据）
        if (pkt->stream_index == vs) {
            avcodec_send_packet(dec, pkt);             // 送进解码器
            packets_in++;
            drain(dec, frame, out, show);              // 能取出几帧就取几帧
        }
        av_packet_unref(pkt);                          // 不管是不是视频，都要释放
    }
    long before_flush = frames_out;
    avcodec_send_packet(dec, NULL);                    // 送入 NULL：没有更多的包了，把剩下的帧都吐出来
    drain(dec, frame, out, show);
    double sec = (av_gettime_relative() - t0) / 1e6;

    printf("...\n送入 %ld 个包，得到 %ld 帧；第一帧在送入第 %ld 个包后才出来，冲刷时又得到 %ld 帧\n",
           packets_in, frames_out, first_out, frames_out - before_flush);
    printf("用时 %.3f 秒，%.0f 帧/秒\n", sec, frames_out / sec);

    fclose(out);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return 0;
}
```

几个要点：

- **`av_find_best_stream`** 找到视频流，同时返回对应的解码器。一个文件中可能有多路视频（例如封面图也是一路视频流），它会选「最好的」那一路。
- **解码器上下文**（`AVCodecContext`）是自己创建的：`avcodec_alloc_context3` 分配，`avcodec_parameters_to_context` 把流的参数复制进去（宽高、像素格式、extradata 中的 SPS/PPS……），`avcodec_open2` 打开。上一篇说过，旧 API 直接用 `stream->codec`，已经删除了。
- **`pkt_timebase`** 告诉解码器包的时间戳用的是什么时间基，解码器输出的帧的 `pts` 也是这个时间基。
- **每个包都要 `av_packet_unref`**，包括不是视频的那些（音频包），否则内存会一直涨。

### 2.2 运行

```bash
./ff_decode bbb-10s.mp4 out.yuv 0 8
md5 -q out.yuv bbb.yuv
```

```
解码器 h264，1280x720，yuv420p，线程 13
帧   0：送入  15 个包后得到，pts=     0，I 帧（关键帧），linesize = 1280/640/640
帧   1：送入  16 个包后得到，pts=   512，B 帧，linesize = 1280/640/640
帧   2：送入  17 个包后得到，pts=  1024，B 帧，linesize = 1280/640/640
帧   3：送入  18 个包后得到，pts=  1536，P 帧，linesize = 1280/640/640
帧   4：送入  19 个包后得到，pts=  2048，B 帧，linesize = 1280/640/640
帧   5：送入  20 个包后得到，pts=  2560，B 帧，linesize = 1280/640/640
帧   6：送入  21 个包后得到，pts=  3072，P 帧，linesize = 1280/640/640
帧   7：送入  22 个包后得到，pts=  3584，B 帧，linesize = 1280/640/640
...
送入 240 个包，得到 240 帧；第一帧在送入第 15 个包后才出来，冲刷时又得到 14 帧
用时 0.335 秒，716 帧/秒
20d9118c9b177896234cec92e2b4e86e
20d9118c9b177896234cec92e2b4e86e
```

`out.yuv` 和第 1 章用 `ffmpeg -i bbb-10s.mp4 -pix_fmt yuv420p bbb.yuv` 解出来的文件 MD5 完全相同：命令行的 `ffmpeg` 也是这样调用这些库的。

输出的帧已经是**显示顺序**了：I、B、B、P、B、B、P……，`pts` 依次是 0、512、1024……解码器在内部完成了第 2 章最后一篇讲的重排序。`pict_type` 告诉我们每一帧原来是什么类型，`AV_FRAME_FLAG_KEY` 标记关键帧。

## 3. 解码器的延迟

### 3.1 先进去，晚出来

输出中最值得注意的是：「第一帧在送入第 15 个包后才出来」。为什么要等这么久？把线程数固定下来，分别运行：

```bash
for t in 1 2 4 0; do ./ff_decode bbb-10s.mp4 /dev/null $t 1; done
```

| 线程数 | 第一帧在第几个包之后 | 冲刷时得到 | 速度 |
|---|---|---|---|
| 1 | 3 | 2 帧 | 816 帧/秒 |
| 2 | 4 | 3 帧 | 1289 帧/秒 |
| 4 | 6 | 5 帧 | 1922 帧/秒 |
| 0（自动，13 个） | 15 | 14 帧 | 3239 帧/秒 |

（速度是把输出写到 `/dev/null` 时测的，只包括解封装和解码。）

**单线程时延迟是 3 个包**，来自 B 帧：码流中是 I、P、B、B……，第 3 个包（第一个 B 帧）送进去之后，才能确定显示顺序中的第 0 帧（I 帧）后面紧跟着谁，I 帧这时才可以输出。第 1 章 `ffprobe` 显示的 `has_b_frames=2`，就是解码器需要攒下的帧数。所以最后冲刷时，解码器手里还有 2 帧。

**多线程时，延迟随线程数增加**，每多一个线程，延迟多一个包。FFmpeg 的 H.264 解码器默认用**帧级多线程**：N 个线程同时解码 N 个不同的帧，每个线程从前面的帧已经解出的部分开始工作。代价是解码器手中同时有 N 帧在处理，第一帧要等 N 个包都送进去才能出来。

速度提高了 4 倍，延迟从 3 帧变成了 15 帧，24 帧/秒时是 0.6 秒。对播放本地文件来说无所谓，但对视频会议、云游戏这类低延迟的场景就不能接受了。另一种是**片级多线程**（`thread_type = FF_THREAD_SLICE`），一帧中的多个片由不同的线程解码，不增加延迟；但它要求编码时把一帧切成多个片，测试视频每帧只有一个片，用片级多线程就和单线程一样（练习 1）。

### 3.2 冲刷

冲刷的那 2～14 帧，是视频的最后几帧。如果去掉 `avcodec_send_packet(dec, NULL)` 这一步，单线程时会少 2 帧，13 个线程时会少 14 帧，超过半秒。这是自己写解码程序时最常见的错误之一，而且在长视频中几乎察觉不到。

## 4. linesize

### 4.1 每一行不一定只有 width 个字节

输出中还有一个细节：`linesize = 1280/640/640`。`linesize[i]` 是第 i 个平面**每一行实际占用的字节数**，第 1 章第 5.1 节提到过的 stride。为了让 SIMD 指令（一次处理 16、32、64 字节）能对齐地访问每一行，FFmpeg 分配图像内存时会把每行的长度向上取整，行尾多出来的部分是填充。

1280 恰好是 64 的倍数，所以 linesize 等于宽度。换一个宽度试试：

```bash
ffmpeg -i bbb-10s.mp4 -t 1 -vf scale=1000:562 -c:v libx264 -an w1000.mp4
./ff_decode w1000.mp4 w1000.yuv 1 1
```

```
解码器 h264，1000x562，yuv420p，线程 1
帧   0：送入   3 个包后得到，pts=     0，I 帧（关键帧），linesize = 1024/512/512
```

宽 1000 像素，每行却占 1024 字节。所以程序中写文件时，必须按行写，每行只写 `width` 个字节，然后跳到下一行（`data + y * linesize`）。

### 4.2 写错了会怎样

如果偷懒，把每行都按 `linesize` 整行写出去：

```c
fwrite(frame->data[p] + (long)y * frame->linesize[p], 1, frame->linesize[p], out);   // 错误：把填充也写进去了
```

文件变成了 20717568 字节，比正确的 20232000 字节多了 2.4%。按 1000×562 去看它：

<img src="/img/av/ff-bad-stride.jpg" alt="把每行的填充也写进文件后，按 1000x562 显示，画面碎成一片斜条纹" width="640">

每一行多了 24 个字节，误差逐行累积，562 行累计错开 13000 多个像素，画面完全碎成了斜条纹。这就是第 1 章第 5.1 节「宽度错了一个像素」的放大版。反过来，从文件读 YUV 填进 `AVFrame` 时也一样，要一行行地复制，或者用 `av_image_copy` 之类的函数。

## 5. 解码音频

音频的解码过程完全相同，只是帧中装的是采样：

```c
// ff_decode_audio.c：解码音频，把平面格式（fltp）交错成 f32le 写出
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

int main(int argc, char *argv[])
{
    if (argc != 3) { fprintf(stderr, "用法: %s 输入 输出.pcm\n", argv[0]); return 1; }
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    const AVCodec *codec = NULL;
    int as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[as]->codecpar);
    dec->pkt_timebase = fmt->streams[as]->time_base;
    avcodec_open2(dec, codec, NULL);

    FILE *out = fopen(argv[2], "wb");
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    long frames = 0, samples = 0;
    int eof = 0;
    while (!eof) {
        if (av_read_frame(fmt, pkt) < 0) { avcodec_send_packet(dec, NULL); eof = 1; }
        else if (pkt->stream_index != as) { av_packet_unref(pkt); continue; }
        else { avcodec_send_packet(dec, pkt); av_packet_unref(pkt); }
        while (avcodec_receive_frame(dec, frame) >= 0) {
            int ch = frame->ch_layout.nb_channels;
            if (frames < 3)
                printf("帧 %ld：pts=%lld，%d 个采样，格式 %s，%d 声道\n", frames, (long long)frame->pts,
                       frame->nb_samples, av_get_sample_fmt_name(frame->format), ch);
            // fltp：每个声道一个平面，data[c] 指向第 c 个声道的 nb_samples 个 float
            for (int i = 0; i < frame->nb_samples; i++)
                for (int c = 0; c < ch; c++)
                    fwrite((float *)frame->data[c] + i, sizeof(float), 1, out);
            samples += frame->nb_samples;
            frames++;
            av_frame_unref(frame);
        }
    }
    printf("...\n共 %ld 帧，%ld 个采样（%.3f 秒）\n", frames, samples, (double)samples / dec->sample_rate);
    fclose(out);
    av_frame_free(&frame); av_packet_free(&pkt);
    avcodec_free_context(&dec); avformat_close_input(&fmt);
    return 0;
}
```

程序的读包循环换了一种写法：读到文件末尾时送入 `NULL`，然后照常取帧，这样冲刷不需要单独的代码。

```bash
./ff_decode_audio bbb-10s.mp4 a.pcm
ffmpeg -i bbb-10s.mp4 -vn -f f32le ref.pcm
md5 -q a.pcm ref.pcm
```

```
帧 0：pts=0，1024 个采样，格式 fltp，2 声道
帧 1：pts=1024，1024 个采样，格式 fltp，2 声道
帧 2：pts=2048，1024 个采样，格式 fltp，2 声道
...
共 469 帧，480000 个采样（10.000 秒）
bf1df8a78804167e1ba888947f2b07c4
bf1df8a78804167e1ba888947f2b07c4
```

和命令行输出的 32 位浮点 PCM 逐字节相同。

- **AAC 解码器输出的是 `fltp`**：32 位浮点、平面格式，第 1 章第 2 篇讲过。`frame->data[0]` 是左声道的 1024 个 `float`，`data[1]` 是右声道。要写成 WAV 常用的交错格式，就要像程序中那样交替地取两个声道的采样；更方便的做法是用下一篇之后会讲的 libswresample。
- **只有 469 帧，480000 个采样**，正好 10.000 秒。第 2 章第 2 篇解码 ADTS 裸流时是 470 帧、481280 个采样。这一次解码器从 MP4 的编辑列表中得知了「跳过开头 1024 个采样、结尾只用 768 个」，第一个预热帧整个丢掉了，最后一帧也被截短了。FFmpeg 是通过包的附加数据（side data）把这个信息传给解码器的，调用者不需要做任何事情。

## 6. 小结

- 解码循环：`av_read_frame` 读包 → `avcodec_send_packet` → 循环 `avcodec_receive_frame` 直到 `EAGAIN`；文件结束后送入 `NULL` 冲刷，取到 `EOF` 为止。
- 解码器上下文自己创建，从流的 `codecpar` 复制参数；设置 `pkt_timebase`，输出帧的 `pts` 就有了正确的时间基。
- 解码器有延迟：B 帧带来 `has_b_frames` 帧的延迟，帧级多线程每个线程再加一帧。忘了冲刷会丢掉最后几帧。
- 帧的每一行占 `linesize` 字节，可能大于宽度，读写图像数据都要按行处理。
- 音频解码器常输出平面格式（`fltp`），各声道分开存放。
- 包和帧都是引用计数的，用完要 `unref`。

下一篇反过来：把 YUV 送进编码器，编码成 H.264。

## 练习

1. 在 `ff_decode.c` 中设置 `dec->thread_type = FF_THREAD_SLICE`，用自动线程数运行，延迟和速度各是多少？为什么？
2. 设置 `dec->skip_frame = AVDISCARD_NONKEY`，让解码器只解码关键帧。得到多少帧？用时是多少？这个功能可以用来做什么？
3. 在读包之前调用 `av_seek_frame(fmt, vs, 5 * 12288, AVSEEK_FLAG_BACKWARD)`，跳到 5 秒，解码出的第一帧的 `pts` 是多少？一共得到多少帧？
4. 修改 `ff_decode_audio.c`，输出 16 位整数的 PCM（`s16le`）：把每个 `float` 乘以 32767，限制在 −32768～32767 之间，四舍五入。和 `ffmpeg -i bbb-10s.mp4 -vn -f s16le ref16.pcm` 的结果比较，相同吗？

<details>
<summary>点击查看答案</summary>

1. 输出：

   ```
   送入 240 个包，得到 240 帧；第一帧在送入第 3 个包后才出来，冲刷时又得到 2 帧
   用时 0.298 秒，805 帧/秒
   ```

   延迟回到了 3 个包，速度也回到了单线程的水平（805 帧/秒）。片级多线程是把一帧中的不同片分给不同的线程，而测试视频每帧只有 1 个片（第 2 章第 1 篇看到 `first_mb_in_slice` 全是 0），没有可以并行的东西。要用片级多线程降低延迟，编码时就要把一帧分成多个片（x264 的 `slices` 参数，或者 `sliced-threads`），实时通信的编码器通常会这样做。

2. 输出：

   ```
   帧   0：送入  92 个包后得到，pts=     0，I 帧（关键帧）
   帧   1：送入  96 个包后得到，pts= 15872，I 帧（关键帧）
   ……
   送入 240 个包，得到 7 帧；……
   用时 0.047 秒，149 帧/秒
   ```

   得到 7 帧，正好是 7 个 IDR（`pts` 除以 12288 就是第 2 章看到的 0、1.292、3.292……秒）；第 133 帧那个不是 IDR 的 I 帧被跳过了。用时 0.047 秒，比完整解码的 0.074 秒少，但没有少到 7/240，因为所有的包仍然要读出来、送进解码器解析片头，才能判断是不是关键帧。播放器拖动进度条时显示的预览缩略图、视频网站给每个视频生成的封面候选，常常就是这样快速取出关键帧来做的。

3. 第一帧的 `pts` 是 42496，即 42496 / 12288 = 3.458 秒，是 5 秒之前最近的关键帧（第 83 帧），和第 2 章第 4 篇练习 1 的结果一样。之后一共解码出 157 帧（第 83～239 帧）。`AVSEEK_FLAG_BACKWARD` 表示找目标时间之前的关键帧；从这里开始解码，要先解码并丢弃 3.458～5 秒之间的 37 帧，才能显示第 5 秒的画面。播放器要做精确跳转，就是这样「跳到前一个关键帧，再解码丢弃」。

4. 转换的代码：

   ```c
   float v = ((float *)frame->data[c])[i] * 32767.0f;
   int16_t s = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)lrintf(v);
   fwrite(&s, 2, 1, out);
   ```

   和 `ffmpeg -f s16le` 的结果比较，长度相同，但 960000 个采样中有 48 万个差 1，差不多是一半。乘以 32768 再四舍五入，结果也一样，还是一半不同。

   把命令行的 f32le 输出和 s16le 输出逐个对照，试了几种公式，发现 FFmpeg 的结果恰好等于 **v × 32768 向下取整**（再限制在 −32768～32767）：

   ```
   四舍五入（× 32767）：前 20 万个采样中 99939 个不同
   四舍五入（× 32768）：99904 个不同
   向零截断（× 32768）：100073 个不同
   向下取整（× 32768）：0 个不同
   ```

   命令行用的是 libswresample 做格式转换，它采用了这种取法。各种方法的差别都在最低一位，相当于满刻度的 −90 dB，完全听不出来。这个练习想说明的是：同一件事有多种「正确」的做法，结果不一定逐位相同；如果需要和 FFmpeg 的输出完全一致，就直接用 libswresample，第 5 篇会讲。

</details>
