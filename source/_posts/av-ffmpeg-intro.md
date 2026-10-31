---
title: FFmpeg 的库与开发环境
date: 2026-11-01 09:00:00
updated: 2026-11-01 09:00:00
categories:
  - 音视频开发
  - FFmpeg编程
tags:
  - 音视频
  - FFmpeg
  - C
order: 1
description: ffmpeg 命令行背后是 7 个 C 库：libavformat 管封装，libavcodec 管编解码，libavutil 是公共工具，另外还有缩放、重采样、滤镜和设备。用 C 写第一个 FFmpeg 程序：打印各个库的版本，统计出 359 个解封装器、527 个解码器，再像 ffprobe 一样打开测试视频，列出每路流的编码、时间基和参数。顺便把 2014 年前后网上流行的示例代码拿来编译：av_register_all、AVStream 的 codec 字段、avcodec_decode_video2 全都不存在了，4 个错误。最后对照一张新旧 API 表，后面几篇都用 FFmpeg 9 的写法。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：知道 FFmpeg 由哪些库组成、各自负责什么；会在 C 程序中引用 FFmpeg 的头文件并链接它的库；会用 `AVFormatContext` 打开文件、读取流的信息；会处理 FFmpeg 的错误码；知道新旧 API 的区别，能看出一份网上的示例代码是不是已经过时。
>
> **前置知识**：第 1、2 章；C 语言的指针、结构体和多文件编译（C/C++ 专栏《C语言基础》）。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，Homebrew 安装的 FFmpeg 9.0.2。

前两章我们只把 FFmpeg 当作命令行工具，用来准备数据和检查结果；所有的解析都是自己写的 C 代码。从这一章开始，我们直接调用 FFmpeg 的库：解封装、解码、编码、缩放、重采样、滤镜，这些前两章看过原理的事情，都交给经过十几年打磨的代码去做。

## 1. FFmpeg 的组成

FFmpeg 项目包含三个命令行工具和一组 C 库。`ffmpeg`、`ffprobe`、`ffplay` 都是用这些库写的「示例程序」，命令行能做的，用库都能做，而且能做得更灵活。

<figure class="diagram">
<svg viewBox="0 0 680 290" role="img" aria-label="FFmpeg 的库：ffmpeg、ffprobe、ffplay 三个工具建立在 7 个库之上；libavformat 负责封装与解封装和协议，libavcodec 负责编解码，libavfilter 负责滤镜，libswscale 负责像素格式转换和缩放，libswresample 负责音频重采样，libavdevice 负责采集和播放设备，它们都依赖 libavutil；libavcodec 又依赖 x264、x265、dav1d 等第三方库">
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="150" y="10" width="110" height="30" rx="5" style="fill:var(--panel-2)"/><rect x="285" y="10" width="110" height="30" rx="5" style="fill:var(--panel-2)"/><rect x="420" y="10" width="110" height="30" rx="5" style="fill:var(--panel-2)"/>
    </g>
    <text x="180" y="30" style="fill:var(--fg)">ffmpeg</text><text x="312" y="30" style="fill:var(--fg)">ffprobe</text><text x="452" y="30" style="fill:var(--fg)">ffplay</text>
    <text x="545" y="30" style="fill:var(--muted);font-size:11px">命令行工具</text>
    <g style="stroke:var(--border-2)">
      <rect x="10" y="70" width="150" height="56" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="175" y="70" width="150" height="56" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="340" y="70" width="150" height="56" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="505" y="70" width="165" height="56" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="10" y="140" width="150" height="56" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="175" y="140" width="150" height="56" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="340" y="140" width="150" height="56" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="10" y="210" width="480" height="34" rx="6" style="fill:var(--mark)"/>
      <rect x="505" y="140" width="165" height="104" rx="6" style="fill:var(--panel-2);stroke-dasharray:4 3"/>
    </g>
    <text x="20" y="92" style="fill:var(--fg)">libavformat</text><text x="20" y="112" style="fill:var(--muted);font-size:11px">封装、解封装、协议</text>
    <text x="185" y="92" style="fill:var(--fg)">libavcodec</text><text x="185" y="112" style="fill:var(--muted);font-size:11px">编码、解码</text>
    <text x="350" y="92" style="fill:var(--fg)">libavfilter</text><text x="350" y="112" style="fill:var(--muted);font-size:11px">滤镜</text>
    <text x="515" y="92" style="fill:var(--fg)">libavdevice</text><text x="515" y="112" style="fill:var(--muted);font-size:11px">摄像头、麦克风、屏幕</text>
    <text x="20" y="162" style="fill:var(--fg)">libswscale</text><text x="20" y="182" style="fill:var(--muted);font-size:11px">像素格式转换、缩放</text>
    <text x="185" y="162" style="fill:var(--fg)">libswresample</text><text x="185" y="182" style="fill:var(--muted);font-size:11px">音频重采样</text>
    <text x="350" y="162" style="fill:var(--muted)">（还有 libpostproc，</text><text x="350" y="182" style="fill:var(--muted)">已很少使用）</text>
    <text x="20" y="232" style="fill:var(--fg)">libavutil</text><text x="110" y="232" style="fill:var(--muted);font-size:11px">公共工具：内存、错误码、时间基、像素格式、字典、日志……</text>
    <text x="515" y="162" style="fill:var(--fg)">第三方库</text>
    <text x="515" y="182" style="fill:var(--muted);font-size:11px">libx264、libx265</text>
    <text x="515" y="200" style="fill:var(--muted);font-size:11px">dav1d、SVT-AV1</text>
    <text x="515" y="218" style="fill:var(--muted);font-size:11px">opus、lame、VideoToolbox</text>
    <text x="515" y="236" style="fill:var(--muted);font-size:11px">……（编译时选择）</text>
    <text x="10" y="272" style="fill:var(--muted);font-size:11px">上层的库依赖下层：例如 libavformat 依赖 libavcodec 和 libavutil；libavcodec 通过第三方库提供 x264 等编码器</text>
  </g>
</svg>
</figure>

| 库 | 负责 | 对应前两章的内容 | 本章 |
|---|---|---|---|
| libavformat | 封装、解封装、网络协议（HTTP、RTMP……） | 第 2 章的 FLV、MP4、TS 解析 | 第 2、4 篇 |
| libavcodec | 编码、解码 | 第 2 章的 H.264、AAC 码流 | 第 2、3、4 篇 |
| libavutil | 公共工具：内存分配、错误码、时间基换算、像素格式描述…… | 第 2 章最后一篇的时间戳换算 | 每一篇 |
| libswscale | 像素格式转换、缩放 | 第 1 章的 YUV 转 RGB | 第 5 篇 |
| libswresample | 采样格式转换、重采样、声道混合 | 第 1 章的 PCM、混叠 | 第 5 篇 |
| libavfilter | 滤镜：加水印、裁剪、调色、混音…… | | 第 6 篇 |
| libavdevice | 采集和播放设备 | | 第 5 章 |

FFmpeg 自己实现了绝大多数格式和解码器（H.264 解码器就是它自己写的），但很多**编码器**来自第三方库：`libx264` 编码器调用的是 x264 项目，`libx265` 调用 x265。编译 FFmpeg 时可以选择链接哪些第三方库，所以不同人装的 FFmpeg，支持的编码器可能不一样。Homebrew 的版本用下面的命令就能看到它的 libavcodec 依赖了哪些库：

```bash
otool -L $(brew --prefix ffmpeg)/lib/libavcodec.dylib    # Linux 上用 ldd
```

```
libswresample.7.dylib libavutil.61.dylib libvpx.12.dylib libdav1d.7.dylib AudioToolbox
libmp3lame.0.dylib libopus.0.dylib libSvtAv1Enc.4.dylib libx264.165.dylib libx265.217.dylib
VideoToolbox CoreFoundation CoreMedia CoreVideo libssl.3.dylib libcrypto.3.dylib ……
```

`AudioToolbox` 和 `VideoToolbox` 是 macOS 的系统框架，FFmpeg 通过它们使用 Apple 芯片的硬件编解码器，第 4 章会用到。

> **许可证**：FFmpeg 本身是 LGPL 2.1 或更高版本，但如果编译时启用了 GPL 的组件（例如 x264、x265），整个库就变成了 GPL。下面的程序会打印出 Homebrew 版本的许可证：GPL 3。学习、写开源程序没有问题；如果要发布闭源的商业软件，需要自己编译一份不含 GPL 组件的 FFmpeg，并动态链接。

## 2. 第一个程序

### 2.1 安装与编译

| 平台 | 安装开发库 |
|---|---|
| macOS | `brew install ffmpeg`（头文件和库都包含在内） |
| Ubuntu / Debian | `sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev libavfilter-dev` |
| Windows | 从 [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) 或 [BtbN](https://github.com/BtbN/FFmpeg-Builds/releases) 下载 shared 版本，里面有 `include`、`lib` 和 `bin`（DLL） |

注意版本：Ubuntu 24.04 的软件源中是 FFmpeg 6.1，本专栏用的是 9.0。本专栏的代码只使用 FFmpeg 5.0 之后的 API，在 6.x、7.x 上同样能编译，个别差异会单独说明。

编译时要告诉编译器头文件和库在哪里。Linux 上一般用 `pkg-config`：

```bash
gcc ff_info.c -o ff_info $(pkg-config --cflags --libs libavformat libavcodec libavutil)
```

macOS 上没有装 `pkg-config` 时，也可以直接写出路径：

```bash
FF=$(brew --prefix ffmpeg)
clang ff_info.c -o ff_info -I$FF/include -L$FF/lib -lavformat -lavcodec -lavutil -lswscale -lswresample -lavfilter
```

### 2.2 程序

第一个程序做三件事：打印各个库的版本；遍历 FFmpeg 支持的所有格式和编解码器，数一数有多少；像 `ffprobe` 一样打开一个文件，列出它的流。

```c
// ff_info.c：打印 FFmpeg 各个库的版本，统计支持的格式和编解码器，再像 ffprobe 一样查看一个文件
#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>

// 版本号是一个整数：主版本 << 16 | 次版本 << 8 | 修订号
static void print_version(const char *name, unsigned v)
{
    printf("%-14s %u.%u.%u\n", name, v >> 16, (v >> 8) & 0xFF, v & 0xFF);
}

int main(int argc, char *argv[])
{
    printf("FFmpeg %s\n", av_version_info());
    print_version("libavutil", avutil_version());
    print_version("libavcodec", avcodec_version());
    print_version("libavformat", avformat_version());
    print_version("libavfilter", avfilter_version());
    print_version("libswscale", swscale_version());
    print_version("libswresample", swresample_version());
    printf("许可证：%s\n\n", avcodec_license());

    // 遍历所有注册的解封装器、封装器、解码器、编码器
    void *it = NULL;
    int demuxers = 0, muxers = 0, decoders = 0, encoders = 0;
    while (av_demuxer_iterate(&it)) demuxers++;
    it = NULL;
    while (av_muxer_iterate(&it)) muxers++;
    it = NULL;
    const AVCodec *c;
    while ((c = av_codec_iterate(&it))) {
        if (av_codec_is_decoder(c)) decoders++;
        if (av_codec_is_encoder(c)) encoders++;
    }
    printf("解封装器 %d 个，封装器 %d 个，解码器 %d 个，编码器 %d 个\n\n", demuxers, muxers, decoders, encoders);

    if (argc < 2) return 0;

    // 打开文件，读取流的信息
    AVFormatContext *fmt = NULL;
    int ret = avformat_open_input(&fmt, argv[1], NULL, NULL);
    if (ret < 0) {
        printf("打开 %s 失败：%s\n", argv[1], av_err2str(ret));
        return 1;
    }
    avformat_find_stream_info(fmt, NULL);
    printf("%s：格式 %s（%s），时长 %.3f 秒，码率 %lld bps，%u 路流\n", argv[1], fmt->iformat->name,
           fmt->iformat->long_name, fmt->duration / (double)AV_TIME_BASE, (long long)fmt->bit_rate, fmt->nb_streams);
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        AVStream *st = fmt->streams[i];
        AVCodecParameters *par = st->codecpar;          // 流的编码参数
        printf("  流 %u：%s，%s，时间基 %d/%d，", i, av_get_media_type_string(par->codec_type),
               avcodec_get_name(par->codec_id), st->time_base.num, st->time_base.den);
        if (par->codec_type == AVMEDIA_TYPE_VIDEO)
            printf("%dx%d，%s，帧率 %d/%d\n", par->width, par->height,
                   av_get_pix_fmt_name(par->format), st->avg_frame_rate.num, st->avg_frame_rate.den);
        else if (par->codec_type == AVMEDIA_TYPE_AUDIO)
            printf("%d Hz，%d 声道，%s\n", par->sample_rate, par->ch_layout.nb_channels,
                   av_get_sample_fmt_name(par->format));
        else
            printf("\n");
    }
    avformat_close_input(&fmt);
    return 0;
}
```

FFmpeg 的头文件按库分目录：`libavformat/avformat.h`、`libavcodec/avcodec.h`……一个函数在哪个头文件中，看函数名的前缀大致就能猜到，但不总是对的。我第一次编译这个程序时就漏了一个头文件：

```
ff_info.c:60:20: error: call to undeclared function 'av_get_pix_fmt_name'
```

`av_get_pix_fmt_name` 在 libavutil 的 `pixdesc.h` 中（像素格式的描述），而不在 `avutil.h` 中。遇到这种情况，在 FFmpeg 的头文件目录中 `grep` 一下函数名就能找到。

```bash
./ff_info bbb-10s.mp4
```

```
FFmpeg 9.0.2
libavutil      61.1.102
libavcodec     63.1.102
libavformat    63.1.102
libavfilter    12.1.102
libswscale     10.1.102
libswresample  7.1.102
许可证：GPL version 3 or later

解封装器 359 个，封装器 184 个，解码器 527 个，编码器 190 个

bbb-10s.mp4：格式 mov,mp4,m4a,3gp,3g2,mj2（QuickTime / MOV），时长 10.000 秒，码率 2395768 bps，2 路流
  流 0：video，h264，时间基 1/12288，1280x720，yuv420p，帧率 24/1
  流 1：audio，aac，时间基 1/48000，48000 Hz，2 声道，fltp
```

### 2.3 程序做了什么

**版本号。** 每个库有自己的版本号，和 FFmpeg 的版本（9.0.2）不是一回事。主版本号变化表示有不兼容的修改：删掉了旧的 API，或者结构体的布局变了。用 libavcodec 62 编译的程序，不能在只装了 libavcodec 63 的机器上运行，动态库的文件名中就带着主版本号（`libavcodec.63.dylib`）。

**格式和编解码器的数量。** 359 个解封装器、184 个封装器、527 个解码器、190 个编码器。解码器比编码器多得多：FFmpeg 能解码几乎所有出现过的格式，包括很多上世纪的游戏和专有格式，但只为常用的格式提供编码器。

**`AVFormatContext`** 是 libavformat 中最重要的结构体，代表一个打开的媒体文件（或网络流）：

- `avformat_open_input` 打开文件，读取文件头，选择合适的解封装器。它就是第 1 章第 4 篇说的「看文件开头的几个字节，给每种格式打分」，选中的格式在 `fmt->iformat` 中。
- `avformat_find_stream_info` 读取文件开头的一些数据包，必要时还会尝试解码，把流的信息补充完整。对 MP4 来说，`moov` 中已经有了几乎所有信息；对 TS、裸流这类没有头信息的格式，就必须靠读一些数据来推测。
- `fmt->streams[i]` 是每一路流（`AVStream`），`codecpar` 是这路流的**编码参数**（`AVCodecParameters`）：编码 ID、宽高、像素格式、采样率、声道布局、extradata（第 2 章的 avcC 和 AudioSpecificConfig）……
- 每路流有自己的 `time_base`，就是第 2 章看到的 1/12288 和 1/48000。`fmt->duration` 则以 `AV_TIME_BASE`（1/1000000，微秒）为单位。

**内存管理。** FFmpeg 的结构体都用对应的函数分配和释放：`avformat_open_input` 分配的上下文要用 `avformat_close_input` 释放。这类函数通常接收指针的地址（`&fmt`），释放后把指针置为 `NULL`，避免野指针。

### 2.4 错误处理

FFmpeg 的函数大多返回 `int`：大于等于 0 表示成功，负数是错误码。错误码可以用 `av_err2str`（或者 `av_strerror`）转成文字：

```bash
./ff_info nosuch.mp4 | tail -1
./ff_info ff_info.c | tail -1
```

```
打开 nosuch.mp4 失败：No such file or directory
打开 ff_info.c 失败：Invalid data found when processing input
```

前者是系统的错误（`ENOENT`，FFmpeg 返回 `AVERROR(ENOENT)`），后者是 FFmpeg 自己定义的错误码 `AVERROR_INVALIDDATA`：文件存在，但没有哪种格式认得它。还有两个以后会经常遇到的特殊值：

- `AVERROR(EAGAIN)`：现在做不了，需要先做别的事（下一篇解码时会看到）；
- `AVERROR_EOF`：数据已经读完了。

为了让代码短一些，本专栏的示例只在关键的地方检查返回值。实际的程序中，每一个返回 `int` 的 FFmpeg 函数都应该检查。

## 3. 网上的旧代码为什么编译不过

学习 FFmpeg 时，网上能找到大量示例代码，其中很多写于 2012～2018 年，包括雷霄骅那一系列经典的「最简单的」示例。它们讲解的原理至今仍然正确，但代码已经无法用新版的 FFmpeg 编译了。下面是当年一个典型的解码程序的骨架：

```c
// old_api.c：2014 年前后的 FFmpeg 写法（摘自当年常见的「最简单的播放器」类示例）
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

int main(int argc, char *argv[])
{
    AVFormatContext *fmt = NULL;
    av_register_all();                                         // 注册所有格式和编解码器
    avformat_open_input(&fmt, argv[1], NULL, NULL);
    avformat_find_stream_info(fmt, NULL);
    AVCodecContext *ctx = fmt->streams[0]->codec;              // 直接使用流里的解码器上下文
    AVCodec *codec = avcodec_find_decoder(ctx->codec_id);
    avcodec_open2(ctx, codec, NULL);
    AVPacket pkt;
    av_init_packet(&pkt);
    AVFrame *frame = av_frame_alloc();
    int got_picture;
    while (av_read_frame(fmt, &pkt) >= 0) {
        avcodec_decode_video2(ctx, frame, &got_picture, &pkt); // 一次调用：送入一个包，可能得到一帧
        av_free_packet(&pkt);
    }
    return 0;
}
```

用 FFmpeg 9 编译：

```bash
clang -c old_api.c -I$FF/include
```

```
8:5: error: call to undeclared function 'av_register_all'
11:44: error: no member named 'codec' in 'struct AVStream'
12:14: warning: initializing 'AVCodec *' with an expression of type 'const AVCodec *' discards qualifiers
15:5: warning: 'av_init_packet' is deprecated
19:9: error: call to undeclared function 'avcodec_decode_video2'
20:9: error: call to undeclared function 'av_free_packet'
2 warnings and 4 errors generated.
```

短短 20 行，4 个错误、2 个警告。对照新的写法：

| 旧的写法 | 新的写法 | 变化的原因 |
|---|---|---|
| `av_register_all()` | 不需要了（4.0 起废弃，5.0 删除） | 格式和编解码器改成静态注册，不再需要初始化 |
| `stream->codec` | 自己创建 `AVCodecContext`，用 `avcodec_parameters_to_context` 从 `stream->codecpar` 复制参数 | 流的参数和解码器的状态分开：一路流可以用多个解码器，也可以不解码 |
| `avcodec_decode_video2`、`avcodec_decode_audio4` | `avcodec_send_packet` + `avcodec_receive_frame` | 一个包不一定对应一帧（B 帧重排、一个包中有多帧音频），新 API 把「送入」和「取出」分开 |
| `avcodec_encode_video2` | `avcodec_send_frame` + `avcodec_receive_packet` | 同上，编码也有延迟 |
| `AVPacket pkt; av_init_packet(&pkt);` | `AVPacket *pkt = av_packet_alloc();` | `AVPacket` 的大小不再是公开 API 的一部分，不能在栈上定义 |
| `av_free_packet(&pkt)` | `av_packet_unref(pkt)` / `av_packet_free(&pkt)` | 引用计数：解除引用和释放结构体分开 |
| `AVCodec *codec = avcodec_find_decoder(…)` | `const AVCodec *codec = …` | 编解码器的描述是只读的 |
| `avpicture_fill`、`avpicture_get_size` | `av_image_fill_arrays`、`av_image_get_buffer_size` | `AVPicture` 被删除 |
| `channels`、`channel_layout`（整数） | `ch_layout`（`AVChannelLayout` 结构体） | 支持任意的声道布局（7.0 删除了旧字段） |

所以读旧代码时，看它讲的**思路**，代码要自己用新的 API 改写；判断一份示例是不是过时，看有没有 `av_register_all`、`->codec`、`decode_video2` 这几个特征就行了。FFmpeg 源码中的 `doc/examples` 目录始终和当前版本保持一致，是最可靠的参考，Homebrew 的版本把它们放在 `$(brew --prefix ffmpeg)/share/ffmpeg/examples`：`decode_video.c`、`encode_video.c`、`remux.c`、`transcode.c`……本章后面几篇写的程序，和这些官方示例做的是同样的事情。

## 4. 小结

- FFmpeg 由 7 个库组成：libavformat（封装）、libavcodec（编解码）、libavutil（工具）、libswscale（缩放）、libswresample（重采样）、libavfilter（滤镜）、libavdevice（设备）。很多编码器来自第三方库，许可证可能因此变成 GPL。
- 编译时指定头文件路径和要链接的库，Linux 上用 `pkg-config` 最方便。
- `avformat_open_input` + `avformat_find_stream_info` 打开文件、读取流的信息；`AVStream` 的 `codecpar` 中是编码参数，`time_base` 是时间基。
- 函数返回负数表示错误，用 `av_err2str` 转成文字；`EAGAIN` 和 `EOF` 是两个特殊的返回值。
- 2018 年以前的示例代码大多用了已删除的 API，看思路，代码按新 API 改写；官方的 `doc/examples` 是最可靠的参考。

下一篇用新的 API 解封装、解码测试视频，把每一帧存成 YUV，并亲眼看看解码器的延迟。

## 练习

1. libavformat 提供了 `av_dump_format(fmt, 0, filename, 0)`，打印的内容和 `ffprobe` 一样。把它加到 `ff_info.c` 中，和程序自己打印的信息对比。它输出到哪里，标准输出还是标准错误？
2. 修改 `ff_info.c`，列出所有编码 ID 是 `AV_CODEC_ID_H264` 的编解码器，以及它们是编码器还是解码器。为什么编码器有好几个，解码器只有一个？
3. 编译 `ff_info.c` 时去掉 `-lavutil`，会得到什么错误？为什么 `ff_info` 直接调用了 libavutil 的函数，就必须链接它，即使 libavformat 本身已经依赖了 libavutil？

<details>
<summary>点击查看答案</summary>

1. `av_dump_format` 的输出：

   ```
   Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'bbb-10s.mp4':
     Metadata:
       major_brand     : isom
       ……
     Duration: 00:00:10.00, start: 0.000000, bitrate: 2395 kb/s
     Stream #0:0[0x1](und): Video: h264 (High) (avc1 / 0x31637661), yuv420p(tv, bt709, progressive), 1280x720, 2261 kb/s, 24 fps, 24 tbr, 12288 tbn (default)
     Stream #0:1[0x2](und): Audio: aac (LC) (mp4a / 0x6134706D), 48000 Hz, stereo, fltp, 127 kb/s (default)
   ```

   和第 1 章 `ffprobe` 的输出一模一样，`ffprobe` 默认的那一段输出就是调用这个函数打印的。它和 FFmpeg 所有的日志一样，通过 `av_log` 输出到**标准错误**：用 `./ex_dump bbb-10s.mp4 > out.txt` 重定向，文件是空的，内容还是显示在终端上。这也是为什么前两章经常要写 `2>&1 | grep`。日志的级别和输出位置可以用 `av_log_set_level`、`av_log_set_callback` 修改。

2. 遍历时判断 `c->id == AV_CODEC_ID_H264`：

   ```
   编码器 libx264              libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
   编码器 libx264rgb           libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 RGB
   编码器 h264_videotoolbox    VideoToolbox H.264 Encoder
   解码器 h264                 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
   ```

   解码的结果由标准唯一规定：同一个码流，任何正确的解码器都必须解出完全相同的画面，所以 FFmpeg 自己实现一个就够了（硬件解码是作为这个解码器的「加速」接入的，不是单独的解码器）。编码则没有唯一的答案，标准只规定码流的格式，不规定怎样编码，不同的编码器在速度、画质、硬件上各有取舍：x264 是软件编码，画质最好；VideoToolbox 用的是芯片中的硬件编码器，速度快、耗电少。第 3 篇会比较它们。

3. 链接时报错：

   ```
   Undefined symbols for architecture arm64:
     "_av_version_info", referenced from: _main in ff_info-6925bf.o
     "_avutil_version", referenced from: _main in ff_info-6925bf.o
   ld: symbol(s) not found for architecture arm64
   ```

   链接器只在命令行列出的库中查找程序**直接**引用的符号。libavformat 依赖 libavutil，这个依赖关系记录在 libavformat 的动态库里，运行时会自动加载 libavutil；但编译我们的程序时，链接器不会去 libavformat 的依赖中帮我们找 `av_version_info`。程序直接用到了哪个库的函数，就要链接哪个库。

</details>
