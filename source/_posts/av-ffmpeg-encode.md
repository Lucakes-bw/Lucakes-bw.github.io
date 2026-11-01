---
title: 编码
date: 2026-11-01 11:00:00
updated: 2026-11-01 11:00:00
categories:
  - 音视频开发
  - FFmpeg编程
tags:
  - 音视频
  - FFmpeg
  - H.264
  - 编码
  - C
order: 3
description: 把第 1 章解出来的 YUV 读进 AVFrame，用 avcodec_send_frame / avcodec_receive_packet 编码成 H.264。x264 的 medium 预设要先吃进 62 帧才吐出第一个包，最后冲刷出 61 个：它要往后看 40 帧来分配码率；换成 zerolatency 并关掉 B 帧，送一帧出一个包。输出的包按解码顺序排列，pts 是 0、3、1、2……。再和 macOS 的硬件编码器 VideoToolbox 比较：VideoToolbox 只用了 0.07 秒 CPU 时间，x264 用了 5.3 秒；同样 1 Mbps 时 VideoToolbox 的 PSNR 低了 4 dB，草地明显糊了。最后看一个容易犯的错误：忘了给帧设置 pts，x264 每帧都报警，画质掉了 5 dB。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `avcodec_send_frame` / `avcodec_receive_packet` 编码视频；会设置编码器的参数，区分通用选项和编码器私有选项；理解编码器的延迟从哪里来，怎样做到零延迟；看懂编码器输出的包的时间戳；了解软件编码和硬件编码的取舍。
>
> **前置知识**：上一篇《解封装与解码》；第 1 章《为什么必须压缩》中的 CRF、preset、GOP。
>
> **实验环境**：macOS 27.0（Apple M6，12 核），Apple Clang 21，FFmpeg 9.0.2（libx264 core 165）。输入是第 1 章解出的 `bbb.yuv`（1280×720，240 帧）。

## 1. 编码的流程

编码是解码的反过程，API 也是对称的：

| 解码 | 编码 |
|---|---|
| `avcodec_find_decoder` | `avcodec_find_encoder` / `avcodec_find_encoder_by_name` |
| 参数从流的 `codecpar` 复制而来 | 参数要**自己设置**：宽高、像素格式、时间基、码率…… |
| `avcodec_send_packet` → `avcodec_receive_frame` | `avcodec_send_frame` → `avcodec_receive_packet` |
| 冲刷：送入 `NULL` 包 | 冲刷：送入 `NULL` 帧 |

最大的区别是参数：解码时，码流里已经写明了一切；编码时，所有决定都要由我们来做。第 1 章第 3 篇用命令行试过的那些参数（CRF、preset、GOP、B 帧），现在都要在代码中设置。

## 2. 程序

```c
// ff_encode.c：读入 YUV420P，编码成 H.264 裸流
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>

static long frames_in = 0, packets_out = 0, first_out = -1, bytes = 0;

// 取出编码器中所有已经可以取出的包，写到文件
static void drain(AVCodecContext *enc, AVPacket *pkt, FILE *out, int show)
{
    while (avcodec_receive_packet(enc, pkt) >= 0) {
        if (first_out < 0) first_out = frames_in;
        if (packets_out < show)
            printf("包 %3ld：送入 %3ld 帧后得到，pts=%3lld dts=%3lld，%6d 字节%s\n", packets_out, frames_in,
                   (long long)pkt->pts, (long long)pkt->dts, pkt->size, (pkt->flags & AV_PKT_FLAG_KEY) ? "，关键帧" : "");
        fwrite(pkt->data, 1, pkt->size, out);
        bytes += pkt->size;
        packets_out++;
        av_packet_unref(pkt);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 4) { fprintf(stderr, "用法: %s 输入.yuv 输出.h264 编码器 [选项=值 ...]\n", argv[0]); return 1; }
    const int W = 1280, H = 720, FPS = 24;
    const AVCodec *codec = avcodec_find_encoder_by_name(argv[3]);
    if (!codec) { fprintf(stderr, "找不到编码器 %s\n", argv[3]); return 1; }

    AVCodecContext *enc = avcodec_alloc_context3(codec);
    enc->width = W;
    enc->height = H;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = (AVRational){1, FPS};           // 输入帧的 pts 就是帧号
    enc->framerate = (AVRational){FPS, 1};
    enc->gop_size = 48;                              // 最长 2 秒一个关键帧
    enc->max_b_frames = 2;
    // 其余参数用「名字=值」的形式传入，例如 preset=medium crf=23 b=2000000
    for (int i = 4; i < argc; i++) {
        char key[64], *val = strchr(argv[i], '=');
        snprintf(key, sizeof key, "%.*s", (int)(val - argv[i]), argv[i]);
        // 先当作编码器私有的选项（如 x264 的 preset），不行再当作通用选项（如 b、g）
        if (av_opt_set(enc->priv_data, key, val + 1, 0) < 0 && av_opt_set(enc, key, val + 1, 0) < 0)
            fprintf(stderr, "未知选项 %s\n", key);
    }
    if (avcodec_open2(enc, codec, NULL) < 0) { fprintf(stderr, "打开编码器失败\n"); return 1; }

    AVFrame *frame = av_frame_alloc();
    frame->width = W;
    frame->height = H;
    frame->format = AV_PIX_FMT_YUV420P;
    av_frame_get_buffer(frame, 0);                   // 分配图像内存，linesize 由 FFmpeg 决定
    AVPacket *pkt = av_packet_alloc();

    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    int64_t t0 = av_gettime_relative();
    for (;;) {
        av_frame_make_writable(frame);               // 编码器可能还引用着上一帧的数据
        int ok = 1;
        for (int p = 0; p < 3 && ok; p++) {          // 按行读入，每行 width 字节，存到 linesize 的位置
            int w = p ? W / 2 : W, h = p ? H / 2 : H;
            for (int y = 0; y < h && ok; y++)
                ok = fread(frame->data[p] + (long)y * frame->linesize[p], 1, w, in) == (size_t)w;
        }
        if (!ok) break;
        frame->pts = frames_in++;
        avcodec_send_frame(enc, frame);
        drain(enc, pkt, out, 8);
    }
    long before_flush = packets_out;
    avcodec_send_frame(enc, NULL);                    // 冲刷
    drain(enc, pkt, out, 8);
    double sec = (av_gettime_relative() - t0) / 1e6;

    printf("...\n送入 %ld 帧，得到 %ld 个包；第一个包在送入第 %ld 帧后才出来，冲刷时又得到 %ld 个\n",
           frames_in, packets_out, first_out, packets_out - before_flush);
    printf("%ld 字节，%.0f kbps；用时 %.2f 秒，%.0f 帧/秒\n", bytes, bytes * 8.0 / (frames_in / (double)FPS) / 1000,
           sec, frames_in / sec);

    fclose(in); fclose(out);
    av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&enc);
    return 0;
}
```

### 2.1 设置参数

- **宽高、像素格式**：输入是 1280×720 的 YUV420P。
- **`time_base`**：输入帧的 `pts` 用什么单位。这里设为 1/24，帧号就是 `pts`，最简单。
- **`framerate`**：帧率，编码器用它来计算码率、写入 SPS 中的 VUI。
- **`gop_size`、`max_b_frames`**：最长 48 帧一个关键帧，最多 2 个连续的 B 帧，和测试视频的设置一样。

其他参数从命令行用「名字=值」的形式传入。FFmpeg 的选项分两类：

- **通用选项**属于 `AVCodecContext`，对所有编码器都有意义，例如 `b`（码率）、`g`（GOP）、`bf`（B 帧数）；
- **私有选项**属于具体的编码器，存在 `enc->priv_data` 中，例如 x264 的 `preset`、`crf`、`tune`，VideoToolbox 的 `realtime`。

`av_opt_set` 可以按名字设置这两类选项，程序先试私有选项，不行再试通用选项。命令行的 `ffmpeg -c:v libx264 -preset slow -crf 20` 也是这样把参数传给编码器的。用 `ffmpeg -h encoder=libx264` 可以列出 x264 的所有私有选项。

### 2.2 准备帧

- **`av_frame_get_buffer`** 按照宽高和像素格式分配图像内存，`linesize` 由 FFmpeg 决定（可能大于宽度），所以读入时也要按行读，和上一篇写出时一样。
- **`av_frame_make_writable`**：送进编码器的帧，编码器可能会保留对它的引用（数据是引用计数的）；下一次往同一块内存写数据之前，调用它确保没有别人在用，必要时它会分配一块新的内存。
- **`frame->pts`**：每一帧的时间戳，以 `time_base` 为单位。第 5 节会看到忘了设置它的后果。

### 2.3 运行

```bash
clang -O2 ff_encode.c -o ff_encode -I$FF/include -L$FF/lib -lavcodec -lavutil
./ff_encode bbb.yuv x264.h264 libx264 preset=medium crf=23
```

```
包   0：送入  62 帧后得到，pts=  0 dts= -2， 94213 字节，关键帧
包   1：送入  63 帧后得到，pts=  3 dts= -1，  2237 字节
包   2：送入  64 帧后得到，pts=  1 dts=  0，   623 字节
包   3：送入  65 帧后得到，pts=  2 dts=  1，   440 字节
包   4：送入  66 帧后得到，pts=  6 dts=  2，  2039 字节
包   5：送入  67 帧后得到，pts=  4 dts=  3，   577 字节
包   6：送入  68 帧后得到，pts=  5 dts=  4，   422 字节
包   7：送入  69 帧后得到，pts=  9 dts=  5，  1621 字节
...
送入 240 帧，得到 240 个包；第一个包在送入第 62 帧后才出来，冲刷时又得到 61 个
2609424 字节，2088 kbps；用时 0.63 秒，380 帧/秒
```

（x264 还会在标准错误上打印一段统计，和命令行编码时看到的一样，这里省略了。）

输出的 `x264.h264` 是 Annex B 格式的裸流，可以直接用第 2 章的 `h264_nalu` 分析，或者用 `ffplay` 播放（裸流没有帧率信息，要加 `-framerate 24`）。和原始的 `bbb.yuv` 比较画质：

```bash
ffmpeg -r 24 -i x264.h264 -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 24 -i bbb.yuv -lavfi "[0][1]psnr" -f null -
```

```
PSNR …… average:47.766961
```

> 这里的两个 `-r 24` 不能省。我第一次算出来只有 20.7 dB，吓了一跳，原因是裸流没有帧率，FFmpeg 默认按 25 帧/秒读它，`psnr` 滤镜按时间戳把两路输入对齐，结果拿第 n 帧和别的帧比较了。比较画质之前，先确认两边的帧是对齐的。

## 3. 编码器的延迟

### 3.1 62 帧之后才有输出

上一篇解码时，第一帧在送入 3 个包后出来；编码更夸张：**送入 62 帧之后，才得到第一个包**，最后冲刷时又吐出 61 个。

x264 默认有一个**前瞻**（lookahead）机制：决定当前帧怎么编码之前，先往后看若干帧，看看画面接下来怎样变化，再决定哪些帧做 B 帧、哪里需要插入 I 帧、哪些区域会被后面的帧大量参考而值得多给一些码率（这就是 x264 著名的 MB-tree）。medium 预设的前瞻是 40 帧，再加上帧级多线程（SEI 中记录的 `threads=18`）同时在编码的帧，就有了 62 帧的延迟。

对文件编码来说，这完全没有问题，只是要记得冲刷，否则最后 61 帧（2.5 秒）就丢了。但对直播、视频通话来说，62 帧就是 2.6 秒的延迟，这还只是编码这一个环节。

### 3.2 降低延迟

几种配置的对比：

| 配置 | 第一个包在第几帧之后 | 冲刷时得到 | 码率 | PSNR | 速度 |
|---|---|---|---|---|---|
| x264 medium，CRF 23 | 62 | 61 | 2088 kbps | 47.8 dB | 379 帧/秒 |
| x264 veryfast，CRF 23 | 32 | 31 | 1887 kbps | 43.9 dB | 981 帧/秒 |
| x264 medium，CRF 23，`tune=zerolatency` | 3 | 2 | 2295 kbps | 45.6 dB | 295 帧/秒 |
| 同上，再加 `bf=0` | **1** | **0** | 2570 kbps | 45.1 dB | 293 帧/秒 |
| VideoToolbox，2.6 Mbps | 5 | 14 | 2610 kbps | 43.6 dB | 626 帧/秒 |

- **veryfast** 的前瞻只有 10 帧，延迟减少到 32 帧，速度快了 2.6 倍，但画质低了 3.9 dB。
- **`tune=zerolatency`** 关掉了前瞻和帧级多线程（改用片级多线程），延迟从 62 帧降到 3 帧。剩下的 3 帧来自 B 帧：zerolatency 本来也会关掉 B 帧，但程序中明确设置了 `max_b_frames = 2`，FFmpeg 把这个设置传给 x264 时覆盖了 tune 的选择。
- **再加 `bf=0`**，送入 1 帧就得到 1 个包，冲刷时什么也没有，这才是真正的零延迟。代价是同样的 CRF 下码率高了 23%，画质反而低了 2.7 dB：前瞻和 B 帧本来就是提高压缩率的手段。

这和上一篇解码器的延迟是同一个道理：**延迟和效率是一对矛盾**。实时通信的编码器都是 zerolatency 这一类配置，用码率换延迟。

## 4. 输出包的时间戳

编码器输出的包是**解码顺序**：

```
包   0：pts=  0 dts= -2，关键帧
包   1：pts=  3 dts= -1
包   2：pts=  1 dts=  0
包   3：pts=  2 dts=  1
包   4：pts=  6 dts=  2
```

这正是第 2 章看到的 I、P、B、B 结构：第 3 帧（P）在第 1、2 帧（B）之前编码。编码器把输入帧的 `pts` 带到对应的包上，并算出 `dts`：DTS 依次递增，从 −2 开始，保证每个包的 DTS ≤ PTS，和第 2 章 MP4 中的 −1024（2 帧 × 512）是同一回事，只是单位不同。

裸流中并不保存这些时间戳，`fwrite` 写出的只是包的数据。下一篇把编码的结果写进 MP4 时，就必须把这些时间戳换算成容器的时间基并写进去。

## 5. 软件编码和硬件编码

### 5.1 VideoToolbox

Apple 芯片中有专门的视频编码电路，macOS 通过 VideoToolbox 框架提供给应用程序使用，FFmpeg 把它包装成 `h264_videotoolbox` 编码器。Windows 和 Linux 上对应的有 NVIDIA 的 NVENC（`h264_nvenc`）、Intel 的 QSV（`h264_qsv`）等。代码完全不用改，只要换一个编码器的名字：

```bash
./ff_encode bbb.yuv vt.h264 h264_videotoolbox b=2600000
```

VideoToolbox 不支持 CRF，这里按 x264 medium 的结果（约 2.6 MB）指定了 2.6 Mbps 的码率。用 `/usr/bin/time` 看一下各自用了多少 CPU：

| 编码器 | 实际耗时 | CPU 时间（用户态） |
|---|---|---|
| x264 medium | 0.65 秒 | **5.30 秒** |
| x264 veryfast | 0.26 秒 | 1.84 秒 |
| VideoToolbox | 0.44 秒 | **0.07 秒** |

x264 用 12 个核心一起算，0.65 秒用掉了 5.3 秒的 CPU 时间；VideoToolbox 几乎不占 CPU，工作都在编码电路中完成，CPU 只是送数据进去、取数据出来。笔记本上录屏、视频通话时风扇不转、电池耐用，靠的就是硬件编码。

### 5.2 同样的码率，画质如何

把码率都设成 1 Mbps：

| 编码器 | 实际码率 | PSNR |
|---|---|---|
| x264（`b=1000000`，medium） | 1274 kbps | 41.8 dB |
| VideoToolbox（`b=1000000`） | 1024 kbps | 37.7 dB |

（x264 按平均码率编码时，在这么短的视频上超出了目标码率不少，它的码率控制需要更长的时间才能收敛。）

<figure>
<img src="/img/av/ff-x264-vs-vt.jpg" alt="第 160 帧蝴蝶附近的局部对比：左边 x264，草地的纹理清楚；右边 VideoToolbox，草地明显模糊" width="960">
<figcaption>第 160 帧的局部，左：x264（1.27 Mbps）；右：VideoToolbox（1.02 Mbps）。右边的草地明显糊了</figcaption>
</figure>

码率差了 25%，但 PSNR 差了 4 dB，草地的纹理肉眼可见地变糊了。硬件编码器为了速度和功耗，搜索范围、模式选择都比 x264 简单得多，同样的码率下画质通常不如好的软件编码器。

所以两者的分工是：**实时、省电的场合用硬件编码**（录屏、视频通话、手机拍摄），**追求画质和压缩率的场合用软件编码**（视频网站的转码、电影压制）。

## 6. 忘了设置 pts

最后看一个容易犯的错误。把程序中的 `frame->pts = frames_in++` 改成只计数、不设置 `pts`：

```
[libx264 @ 0x78fb00c000] non-strictly-monotonic PTS
[libx264 @ 0x78fb00c000] non-strictly-monotonic PTS
……（共 240 行）
包   0：送入  62 帧后得到，pts=-9223372036854775808 dts=-9223372036854775808， 56589 字节，关键帧
1540847 字节，1233 kbps
```

x264 每一帧都发出警告：时间戳不是严格递增的。`av_frame_alloc` 分配的帧，`pts` 的初始值是 `AV_NOPTS_VALUE`（int64 的最小值），每一帧都一样。输出的包也没有有效的时间戳。

更糟的是，文件小了 41%，PSNR 从 47.8 dB 降到了 **42.3 dB**。编码程序没有报错，输出的裸流也能正常播放，很容易以为一切正常。原因是 x264 的码率控制要根据时间戳推算每一帧的时长，用来分配码率；所有帧的时间戳都相同，它对每帧时长的估计就全错了。

这类错误很隐蔽：程序能跑、结果能播，只是画质莫名其妙地差了。所以编码器的警告不要忽视，尤其是和时间戳有关的。

## 7. 小结

- 编码：`avcodec_send_frame` → 循环 `avcodec_receive_packet`；结束时送入 `NULL` 冲刷。
- 编码参数要自己设置：宽高、像素格式、`time_base`、`framerate`、GOP、B 帧；编码器私有的参数（x264 的 preset、crf）在 `priv_data` 中，用 `av_opt_set` 设置。
- 帧用 `av_frame_get_buffer` 分配，按 `linesize` 填入数据，写之前 `av_frame_make_writable`，每一帧设置递增的 `pts`。
- 编码器有延迟：x264 medium 的前瞻和多线程带来 62 帧延迟；`tune=zerolatency` 加上不用 B 帧，可以做到送一帧出一包，代价是压缩率。
- 输出的包按解码顺序排列，带有 PTS 和 DTS。
- 硬件编码（VideoToolbox、NVENC、QSV）几乎不占 CPU，速度快，同等码率下画质不如 x264。

下一篇把解码和编码连起来，再写进容器：转封装与转码。

## 练习

1. 用 `ff_encode` 的输出 `x264.h264` 运行第 2 章的 `h264_nalu`，开头有哪些 NAL 单元？SEI 中记录的 `rc_lookahead` 是多少？
2. 用 `crf=18` 和 `crf=28` 各编码一次，记录文件大小和 PSNR，和第 1 章第 3 篇的表对比，规律一样吗？
3. 把 `gop_size` 改成 240（10 秒的视频只在开头有一个定期的关键帧），用 `tune=zerolatency bf=0` 编码，再用第 2 章的 `h264_drop` 丢掉第 10 帧的一个片，错误会传到哪里为止？这对实时通信意味着什么？（提示：先用 `h264_parse` 看看这个码流有多少个片。）

<details>
<summary>点击查看答案</summary>

1. 开头和测试视频一样：SEI（x264 的版本和参数）、SPS、PPS、IDR 片。用 `strings` 查看 SEI：

   ```bash
   head -c 800 x264.h264 | strings -n 20 | tr ' ' '\n' | grep -E "rc_lookahead|threads|bframes|keyint"
   ```

   ```
   threads=18
   lookahead_threads=3
   bframes=2
   keyint=48
   rc_lookahead=40
   ```

   `rc_lookahead=40` 就是第 3.1 节说的前瞻帧数，`threads=18` 是编码线程数（x264 默认用 CPU 核心数的 1.5 倍）。`bframes=2`、`keyint=48` 是程序中设置的 `max_b_frames` 和 `gop_size`。

2. 实测：

   | CRF | 文件大小 | PSNR |
   |---|---|---|
   | 18 | 3.57 MB | 49.5 dB |
   | 23 | 2.61 MB | 47.8 dB |
   | 28 | 1.53 MB | 42.2 dB |

   规律一样：CRF 越大，文件越小，画质越差；从 23 到 28，文件缩小到 59%，PSNR 降了 5.5 dB，比从 18 到 23 降得多，CRF 越大，每一步丢掉的东西越多。具体的数值和第 1 章不同，因为第 1 章的命令行编码用的是 x264 的默认参数（3 个 B 帧、GOP 250），这里是 2 个 B 帧、GOP 48。

3. 先用第 2 章的 `h264_parse` 看看这个码流，有两个意外：

   ```
   共 2640 个片：I 55，P 2585，B 0
   ```

   - 2640 个片，每帧 11 个。`zerolatency` 不用帧级多线程（会增加延迟），改用片级多线程，每帧切成多个片，由不同的线程同时编码。
   - I 片 55 个，即 5 帧：第 0、31、83、133、239 帧，而且都是 IDR。GOP 设成了 240，但 x264 在镜头切换处仍然会插入 IDR，只要离上一个 IDR 不少于 `keyint_min`（这里是 24 帧）。

   丢掉第 110 个片，也就是第 10 帧的第一个片（59 字节，画面顶部的一条）：

   ```
   第 10～30 帧出错，PSNR 79～81 dB；第 31 帧起完全相同
   ```

   错误从第 10 帧一直传到第 30 帧，第 31 帧的镜头切换处有一个 IDR，到这里才恢复。PSNR 很高（误差很小），是因为丢的只是一个片，而且是几乎静止的天空；同一帧中其他 10 个片都完好无损，这就是片的意义。如果这个镜头一直不切换，错误就会一直传到视频结束。

   实时通信中不能指望镜头切换：要么用较短的 GOP，要么像第 2 章第 1 篇说的那样，接收方发现丢包后请求发送方立即编码一个 IDR（WebRTC 中的 PLI/FIR）。还有一种做法是**帧内刷新**（x264 的 `intra-refresh` 选项）：不发完整的 IDR，而是每一帧用帧内编码刷新画面的一列，一段时间后整个画面都刷新过，既能从错误中恢复，又避免了 IDR 带来的码率突增。

</details>
