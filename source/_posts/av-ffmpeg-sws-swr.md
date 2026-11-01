---
title: 像素格式转换与重采样
date: 2026-11-01 13:00:00
updated: 2026-11-01 13:00:00
categories:
  - 音视频开发
  - FFmpeg编程
tags:
  - 音视频
  - FFmpeg
  - swscale
  - swresample
  - C
order: 5
description: 第 1 章自己写过 YUV 转 RGB 和丢采样降低采样率，这一篇交给 FFmpeg 的两个库。libswscale 把一帧 YUV420P 转成 RGB24 只要 95 微秒，和第 1 章按 BT.709 自己写的结果相差 53 dB，几乎看不出区别；比较 5 种缩放算法，缩小再放大回去的 PSNR 只差 0.5 dB，但放大的画面一眼就能分出最近邻的方块和 lanczos 的锐利，而在 Apple 芯片上 fast_bilinear 反而比 bilinear 慢。libswresample 把 48 kHz 浮点立体声转成 44.1 kHz 的 16 位整数，冲刷时还能多取出 16 个采样；用它把 15 kHz 的声音降到 16 kHz，混叠被压到了 −62.9 dB。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 libswscale 做像素格式转换和缩放，知道怎样指定色彩标准和范围；了解几种缩放算法的速度和画质；会用 libswresample 改变采样率、采样格式和声道布局，理解它的延迟和冲刷。
>
> **前置知识**：第 1 章《像素与 YUV》《音频与 PCM》；本章前几篇。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2。

上一篇的转码程序有一个限制：输出和输入的分辨率、像素格式必须相同，音频只能原样复制。实际的转码几乎总要改变这些：把 1080p 转成 720p 给手机看，把 48 kHz 的音频转成 44.1 kHz 给 CD 用，把解码出的 YUV 转成 RGB 给屏幕显示。这些工作由两个库负责：

- **libswscale**（sws，software scale）：像素格式转换和缩放；
- **libswresample**（swr，software resample）：采样格式转换、重采样、声道混合。

第 1 章我们自己写过这两件事的最简单的版本：`yuv2bmp.c` 的 YUV 转 RGB，`alias.c` 的丢采样。现在看看专业的实现做得怎样。

## 1. libswscale

### 1.1 用法

libswscale 的用法只有三步：

```c
// 1. 创建：源的宽高和像素格式，目标的宽高和像素格式，缩放算法
struct SwsContext *sws = sws_getContext(src_w, src_h, src_fmt, dst_w, dst_h, dst_fmt, SWS_BICUBIC, NULL, NULL, NULL);
// 2. 转换：每个平面的数据指针和 linesize
sws_scale(sws, src_data, src_linesize, 0, src_h, dst_data, dst_linesize);
// 3. 释放
sws_freeContext(sws);
```

一个 `SwsContext` 对应一种固定的转换（尺寸和格式都确定），创建时会预先计算好滤波器的系数，之后每一帧只要调用 `sws_scale`。所以创建一次，反复使用。

### 1.2 程序

下面的程序读入第 1 章的 `frame.yuv`，先不缩放、只转成 RGB24，保存成 PPM 图片；再用 5 种缩放算法把它缩小到 640×360、再放大回 1280×720，比较速度和画质：

```c
// ff_scale.c：用 libswscale 把一帧 YUV420P 缩放并转换成 RGB24，比较几种缩放算法
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>

#define W 1280
#define H 720

// 把 src（W×H 的 yuv420p）缩放成 dw×dh 的 dst_fmt，返回每次转换的平均用时（微秒）
static double convert(int flags, const uint8_t *const src[4], const int src_ls[4],
                      int dw, int dh, enum AVPixelFormat dst_fmt, uint8_t *dst[4], int dst_ls[4])
{
    struct SwsContext *sws = sws_getContext(W, H, AV_PIX_FMT_YUV420P, dw, dh, dst_fmt, flags, NULL, NULL, NULL);
    // 输入是 BT.709 有限范围（第 1 章第 4 节），输出 RGB 是全范围
    const int *coef = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(sws, coef, 0, coef, 1, 0, 1 << 16, 1 << 16);
    int64_t t0 = av_gettime_relative();
    for (int i = 0; i < 500; i++)
        sws_scale(sws, src, src_ls, 0, H, dst, dst_ls);
    double us = (av_gettime_relative() - t0) / 500.0;
    sws_freeContext(sws);
    return us;
}

static double psnr(const uint8_t *a, const uint8_t *b, int n)
{
    double mse = 0;
    for (int i = 0; i < n; i++) mse += (double)(a[i] - b[i]) * (a[i] - b[i]);
    mse /= n;
    return mse == 0 ? 99 : 10 * log10(255.0 * 255 / mse);
}

int main(void)
{
    // 读入第 1 章的 frame.yuv，放到 FFmpeg 分配的、linesize 对齐的内存中
    uint8_t *src[4], *rgb[4], *small[4], *back[4];
    int src_ls[4], rgb_ls[4], small_ls[4], back_ls[4];
    av_image_alloc(src, src_ls, W, H, AV_PIX_FMT_YUV420P, 32);
    FILE *f = fopen("frame.yuv", "rb");
    for (int p = 0; p < 3; p++)
        for (int y = 0; y < (p ? H / 2 : H); y++)
            fread(src[p] + y * src_ls[p], 1, p ? W / 2 : W, f);
    fclose(f);

    // 1. 不缩放，只转成 RGB24，保存成 PPM（最简单的图片格式：一行文本头 + RGB 数据）
    av_image_alloc(rgb, rgb_ls, W, H, AV_PIX_FMT_RGB24, 32);
    double us = convert(SWS_BICUBIC, (const uint8_t *const *)src, src_ls, W, H, AV_PIX_FMT_RGB24, rgb, rgb_ls);
    f = fopen("sws.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) fwrite(rgb[0] + y * rgb_ls[0], 1, W * 3, f);
    fclose(f);
    printf("YUV420P → RGB24（%dx%d）：%.0f 微秒/帧\n\n", W, H, us);

    // 2. 缩小到 640x360 再放大回 1280x720，和原图比较亮度的 PSNR
    struct { const char *name; int flag; } algos[] = {
        {"point", SWS_POINT}, {"fast_bilinear", SWS_FAST_BILINEAR}, {"bilinear", SWS_BILINEAR},
        {"bicubic", SWS_BICUBIC}, {"lanczos", SWS_LANCZOS},
    };
    av_image_alloc(small, small_ls, 640, 360, AV_PIX_FMT_YUV420P, 32);
    av_image_alloc(back, back_ls, W, H, AV_PIX_FMT_YUV420P, 32);
    printf("算法            缩小（微秒）  放大（微秒）  往返 PSNR\n");
    for (int k = 0; k < 5; k++) {
        double t1 = convert(algos[k].flag, (const uint8_t *const *)src, src_ls, 640, 360, AV_PIX_FMT_YUV420P, small, small_ls);
        struct SwsContext *up = sws_getContext(640, 360, AV_PIX_FMT_YUV420P, W, H, AV_PIX_FMT_YUV420P,
                                               algos[k].flag, NULL, NULL, NULL);
        int64_t t0 = av_gettime_relative();
        for (int i = 0; i < 500; i++) sws_scale(up, (const uint8_t *const *)small, small_ls, 0, 360, back, back_ls);
        double t2 = (av_gettime_relative() - t0) / 500.0;
        sws_freeContext(up);
        // 只比较 Y 平面，逐行比较（两边的 linesize 可能不同）
        double sum = 0;
        for (int y = 0; y < H; y++) sum += pow(10, -psnr(src[0] + y * src_ls[0], back[0] + y * back_ls[0], W) / 10);
        printf("%-14s  %10.0f  %12.0f  %8.2f dB\n", algos[k].name, t1, t2, 10 * log10(1 / (sum / H)));
    }
    av_freep(&src[0]); av_freep(&rgb[0]); av_freep(&small[0]); av_freep(&back[0]);
    return 0;
}
```

- **`av_image_alloc`** 按宽高和像素格式分配一帧图像的内存，返回每个平面的指针和 `linesize`（最后一个参数是对齐的字节数）。和上一篇的 `av_frame_get_buffer` 作用相同，只是不需要 `AVFrame`。
- **PPM** 是最简单的图片格式：一行文本 `P6 宽 高 255`，后面直接是 RGB 数据，没有 BMP 那样的倒序、补齐和 BGR 顺序。macOS 的「预览」可以直接打开。

### 1.3 色彩标准和范围

`sws_setColorspaceDetails` 那一行不能省。第 1 章第 4 节讲过，高清视频用 BT.709 的公式，Y 的范围是 16～235。libswscale **默认按 BT.601 处理 YUV**，也就是说，不告诉它的话，它会犯第 1 章第 5.3 节那个「用错标准」的错误。参数依次是：源的系数、源是否全范围（0：有限范围）、目标的系数、目标是否全范围（1：RGB 是全范围）、亮度、对比度、饱和度（`1 << 16` 表示不调整）。

转出的 `sws.ppm` 和第 1 章自己写的 `yuv2bmp` 得到的 `frame.bmp` 比较：

```bash
ffmpeg -i sws.ppm -i frame.bmp -lavfi "[0]format=rgb24[a];[1]format=rgb24[b];[a][b]psnr" -f null -
```

```
PSNR r:54.374465 g:51.040582 b:54.349831 average:52.954331
```

53 dB，肉眼完全看不出区别。不是更高，是因为 libswscale 对色度做了插值，而第 1 章的程序直接复制（第 1 章第 4.2 节的 PSNR 实验说过这个差别）。

### 1.4 缩放算法

运行结果：

```bash
clang -O2 ff_scale.c -o ff_scale -I$FF/include -L$FF/lib -lswscale -lavutil
./ff_scale
```

```
YUV420P → RGB24（1280x720）：95 微秒/帧

算法            缩小（微秒）  放大（微秒）  往返 PSNR
point                  105           208     26.34 dB
fast_bilinear          270           300     26.81 dB
bilinear               217           237     26.74 dB
bicubic                304           289     26.85 dB
lanczos                437           460     26.86 dB
```

（每个数字是 500 次的平均值，多次运行会有 10% 左右的波动。）

- **速度**：缩放一帧 720p 只要 0.1～0.5 毫秒，只转格式不缩放时更快（95 微秒），一秒能处理几千帧，对实时处理来说绰绰有余。libswscale 为常见的格式组合写了大量手工优化的汇编（x86 的 SSE/AVX，ARM 的 NEON）。
- **`fast_bilinear` 反而比 `bilinear` 慢**。它当年是为 x86 优化的「快速」版本，在 Apple 芯片上 `bilinear` 有 NEON 优化的代码，`fast_bilinear` 却没有。所谓「快」，取决于你在什么硬件上运行。
- **往返的 PSNR 只差 0.5 dB**。缩小到四分之一时，大量的细节（草叶的纹理）已经丢掉了，不管用什么算法放大回来，都找不回这些细节，各种算法的误差主要来自这一步。

PSNR 差得不多，但眼睛看起来差别很明显。截取第 0 帧左下角的一小块（120×90），分别用三种算法放大 3 倍：

<figure>
<img src="/img/av/ff-sws-upscale.jpg" alt="三种算法放大 3 倍的对比：最近邻有明显的方块和锯齿，双线性比较模糊，lanczos 最清晰" width="1092">
<figcaption>左：point（最近邻），中：bilinear（双线性），右：lanczos</figcaption>
</figure>

- **最近邻**（point）：每个输出像素直接取最近的一个输入像素，速度最快，但会出现方块和锯齿（花瓣的边缘）。
- **双线性**（bilinear）：取周围 2×2 个像素加权平均，平滑但模糊。
- **双三次**（bicubic，FFmpeg 命令行的默认值）、**lanczos**：取周围更多的像素，用更复杂的权重，边缘更锐利，速度更慢。

缩小时，最近邻还有一个问题：直接隔几个像素取一个，就是第 1 章第 4 节说的「图像的混叠」，细密的纹理会变成摩尔纹。其他算法缩小时都会先做低通滤波（取周围像素的平均），这和音频降采样前要先滤掉高频是同一个道理。

一般的建议：**缩小用 bicubic 或 bilinear，放大用 lanczos 或 bicubic，追求速度用 bilinear**；最近邻只适合像素画之类需要保留方块的场合。

## 2. libswresample

### 2.1 用法

libswresample 也是三步：用 `swr_alloc_set_opts2` 指定输入、输出的声道布局、采样格式、采样率，`swr_init` 初始化；然后反复调用 `swr_convert`；最后冲刷、释放。下面的程序把 48 kHz 的 32 位浮点 PCM 转成指定采样率、16 位整数、立体声：

```c
// ff_resample.c：用 libswresample 改变采样率、采样格式和声道布局
// 输入：48 kHz、float、单声道或立体声的原始 PCM（f32le 交错）；输出：目标采样率、s16、立体声
#include <stdio.h>
#include <stdlib.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

int main(int argc, char *argv[])
{
    if (argc != 5) { fprintf(stderr, "用法: %s 输入.f32 输入声道数 输出.s16 输出采样率\n", argv[0]); return 1; }
    int in_ch = atoi(argv[2]), out_rate = atoi(argv[4]), in_rate = 48000;
    AVChannelLayout in_layout, out_layout = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_default(&in_layout, in_ch);

    SwrContext *swr = NULL;
    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, out_rate,    // 输出
                        &in_layout, AV_SAMPLE_FMT_FLT, in_rate, 0, NULL);  // 输入
    if (!swr || swr_init(swr) < 0) { fprintf(stderr, "初始化失败\n"); return 1; }

    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[3], "wb");
    enum { N = 1024 };                                  // 每次处理 1024 个采样，和 AAC 一帧一样
    float buf[N * 8];
    int16_t obuf[N * 8];
    long in_total = 0, out_total = 0;
    size_t got;
    while ((got = fread(buf, sizeof(float) * in_ch, N, in)) > 0) {
        const uint8_t *src[1] = {(const uint8_t *)buf};
        uint8_t *dst[1] = {(uint8_t *)obuf};
        // 输出缓冲区要够大：输入 got 个采样，最多输出 got × 输出采样率 / 输入采样率，再加上内部积压的
        int max_out = (int)av_rescale_rnd(swr_get_delay(swr, in_rate) + got, out_rate, in_rate, AV_ROUND_UP);
        int n = swr_convert(swr, dst, max_out, src, (int)got);
        fwrite(obuf, sizeof(int16_t) * 2, n, out);
        in_total += got;
        out_total += n;
    }
    long before_flush = out_total;
    uint8_t *dst[1] = {(uint8_t *)obuf};
    int n = swr_convert(swr, dst, N * 4, NULL, 0);    // 冲刷：取出滤波器中积压的采样
    fwrite(obuf, sizeof(int16_t) * 2, n, out);
    out_total += n;
    printf("输入 %ld 个采样（%d Hz），输出 %ld 个采样（%d Hz），其中冲刷得到 %ld 个\n",
           in_total, in_rate, out_total, out_rate, out_total - before_flush);
    fclose(in); fclose(out);
    swr_free(&swr);
    return 0;
}
```

一次调用就完成了三种转换：

- **采样格式**：float → s16。上一篇第 2 节练习 4 自己写过，这里交给 libswresample。
- **采样率**：48000 → 44100。
- **声道布局**：输入是单声道时，复制到左右两个声道（练习 1）；输入是 5.1 声道时，按标准的系数混合成立体声，第 1 章准备测试视频时把 5.1 声道的原片转成立体声，就是这样做的。

### 2.2 输出多少个采样

重采样时，输入和输出的采样数不是一一对应的：48000 Hz 转 44100 Hz，每输入 160 个采样，输出 147 个。而且重采样用的滤波器需要看到前后若干个采样才能算出一个输出，所以内部总有一些**积压**的采样还没输出，`swr_get_delay` 返回积压的多少。程序中按「积压 + 本次输入」计算输出缓冲区最多需要多大，再交给 `swr_convert`，它返回实际输出了多少。

和编解码器一样，最后要**冲刷**：`swr_convert` 的输入传 `NULL`，把积压的采样全部取出来。

```bash
./ff_resample ref.pcm 2 r44.s16 44100
```

```
输入 480000 个采样（48000 Hz），输出 441000 个采样（44100 Hz），其中冲刷得到 16 个
```

（`ref.pcm` 是第 2 篇用命令行解码出的 32 位浮点立体声 PCM。）10 秒的音频，44100 × 10 = 441000 个采样，一个不差，其中最后 16 个是冲刷得到的，忘了冲刷就会少 16 个采样（0.36 毫秒）。比编码器冲刷少得多，但如果把很多段音频分别处理再拼接，每段少一点，接缝处就会出现微小的空隙。

和命令行 `ffmpeg -f f32le -ar 48000 -ac 2 -i ref.pcm -ar 44100 -f s16le cli44.s16` 的结果比较：大小相同，882000 个采样中 99.4% 完全相同，其余的只差 1。命令行是先用 float 完成重采样，最后才转成 s16；我们的程序让 libswresample 一次完成，舍入的时机不同。

### 2.3 混叠

第 1 章第 4 节用 15 kHz 的正弦波演示了混叠：直接丢采样降到 16 kHz，得到一个 1 kHz 的声音，RMS 一点没减少（−9.0 dB）；用 FFmpeg 命令行重采样，只剩 −59.9 dB。现在用自己的程序调用 libswresample：

```bash
ffmpeg -i tone15k_48k.wav -f f32le -ac 1 tone.f32
./ff_resample tone.f32 1 tone16k.s16 16000
ffmpeg -f s16le -ar 16000 -ac 2 -i tone16k.s16 -af astats -f null - 2>&1 | grep "RMS level"
```

```
输入 48000 个采样（48000 Hz），输出 16000 个采样（16000 Hz），其中冲刷得到 16 个
RMS level dB: -62.909932
```

15 kHz 的声音被压到了 −62.9 dB，比原来的 −9.0 dB 低了 54 dB，基本听不见了（第 1 章命令行的结果是 −59.9 dB，差别在于命令行用的是浮点，这里最后转成了 16 位整数）。libswresample 在降低采样率之前，先用一个很陡的低通滤波器把 8 kHz 以上的部分滤掉，这正是第 1 章说的「先滤波，再抽取」，只是滤波器的设计比我们的「3 点平均」精细得多。

滤波器的长度和陡峭程度可以调整（`filter_size`、`cutoff` 等选项），还可以换成 SoX 的重采样引擎（`resampler=soxr`，需要编译时启用）。默认设置对大多数用途已经够好了。

## 3. 小结

- libswscale：`sws_getContext` 指定源和目标的尺寸、像素格式、算法，`sws_scale` 转换，一个上下文对应一种固定的转换，反复使用。
- libswscale 默认按 BT.601 处理 YUV，处理高清视频时用 `sws_setColorspaceDetails` 指定 BT.709 和范围。
- 缩放算法：point 最快但有锯齿，bilinear 平滑偏模糊，bicubic、lanczos 更锐利更慢。哪个更快取决于硬件上有没有对应的优化。
- libswresample：一次调用完成采样格式、采样率、声道布局的转换。输出采样数用 `swr_get_delay` 估算，最后要冲刷。
- 降低采样率时，libswresample 会先做低通滤波，避免混叠。

下一篇介绍 FFmpeg 中最灵活的部分：滤镜图（libavfilter），用一行描述串起缩放、裁剪、加水印等一系列处理。

## 练习

1. 用 `ff_resample` 把单声道的 `tone.f32` 转成立体声（`./ff_resample tone.f32 1 x.s16 48000`），左右声道的数据一样吗？音量和原来相比有没有变化？
2. 修改 `ff_scale.c`，把第 1 步的 `sws_setColorspaceDetails` 去掉，转出的图片和 `frame.bmp` 的 PSNR 是多少？和第 1 章第 5.3 节的结果对比。

<details>
<summary>点击查看答案</summary>

1. 左右声道的数据完全相同，但音量变小了：原来的 RMS 是 −9.03 dB，每个声道变成了 −12.04 dB，正好低了 3 dB，即每个声道乘以 0.707（1/√2）。libswresample 按「总功率不变」的原则分配：一个声道的声音分到两个音箱中，每个音箱的功率各占一半，两个音箱加起来听起来和原来一样响。如果想让每个声道都保持原来的音量，可以用 `swr_set_matrix` 自己指定混合系数（例如都设为 1.0）。

2. 去掉之后，libswscale 按 BT.601 转换，和按 BT.709 转换的 `frame.bmp` 比较，PSNR 从 53.0 dB 降到了 39.2 dB。这和第 1 章第 4.2 节的现象一样：用 FFmpeg 命令行比较时，不指定 `in_color_matrix=bt709`，PSNR 也是 39 dB。颜色的偏差就是第 1 章第 5.3 节那张对比图展示的：差别不大，但确实存在，同一个视频在不同的播放器中颜色不同，往往就是这个原因。

</details>
