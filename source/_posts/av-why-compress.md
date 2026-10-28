---
title: 为什么必须压缩
date: 2026-10-28 11:00:00
updated: 2026-10-28 11:00:00
categories:
  - 音视频开发
  - 音视频基础
tags:
  - 音视频
  - 视频编码
  - H.264
  - C
order: 3
description: 10 秒 720p 的测试视频解码后有 332 MB，而 MP4 文件只有 3.0 MB，压缩了 111 倍。用 C 写两个小程序找出压缩的来源：相邻像素的差值在 ±2 以内的占 71.6%，存差值比存像素本身少用 3.7 位；相邻两帧平均 69% 的像素几乎没变，镜头切换时差值突然跳到 75.7。再用 ffprobe 统计 I、P、B 帧：一个 I 帧平均 110 KB，B 帧只有 4.5 KB。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会算原始音视频的码率，理解为什么不压缩就无法存储和传输；理解视频中的三种冗余：空间冗余、时间冗余、视觉冗余，以及编码器分别怎样利用它们；知道 I 帧、P 帧、B 帧和 GOP 是什么；理解码率、画质和编码速度三者之间的取舍。
>
> **前置知识**：前两篇《像素与 YUV》《音频与 PCM》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0（libx264）。

## 1. 原始音视频有多大

前两篇已经算过一帧 YUV 和一秒 PCM 的大小，把它们放在一起，算算常见规格的原始码率（视频按 8 位 YUV420，音频按 48 kHz 16 位立体声）：

| 规格 | 每帧 | 每秒 | 1 小时 |
|---|---|---|---|
| 720p，24 帧/秒 | 1.38 MB | 33 MB（265 Mbps） | 119 GB |
| 1080p，30 帧/秒 | 3.11 MB | 93 MB（746 Mbps） | 336 GB |
| 4K，60 帧/秒 | 12.4 MB | 746 MB（5.97 Gbps） | 2.7 TB |
| 音频 48 kHz 立体声 | | 192 KB（1.5 Mbps） | 0.69 GB |

对比一下现实中的条件：

- 家里的宽带 100~1000 Mbps，手机 4G 实际几十 Mbps，**1080p 的原始视频一路都传不动**；
- 一部 2 小时的 1080p 电影原始数据 670 GB，而一个蓝光碟是 25~50 GB，网络视频通常只有 1~4 GB；
- 视频网站推荐的 1080p 码率在 5~8 Mbps，是原始数据的 **1/100 左右**。

在测试视频上实测：

```bash
ffmpeg -i bbb-10s.mp4 -pix_fmt yuv420p -f rawvideo bbb.yuv
ffmpeg -i bbb-10s.mp4 -vn -f s16le -ar 48000 -ac 2 bbb.pcm
ls -l bbb-10s.mp4 bbb.yuv bbb.pcm
ffprobe -v error -show_entries stream=codec_type,bit_rate -of csv=p=0 bbb-10s.mp4
```

```
-rw-r--r--  1 lucky  staff    2994711 Oct 28 11:00 bbb-10s.mp4
-rw-r--r--  1 lucky  staff    1920000 Oct 28 11:00 bbb.pcm
-rw-r--r--  1 lucky  staff  331776000 Oct 28 11:00 bbb.yuv
video,2261404
audio,127712
```

原始数据 331776000 + 1920000 字节，约 334 MB；MP4 文件 2994711 字节，约 3.0 MB，**只剩不到 1%**。分开来看，视频从 265 Mbps 压到了 2.26 Mbps，压缩了 117 倍；音频从 1536 kbps 压到了 128 kbps，压缩了 12 倍。而这个视频是用 x264 的默认画质（CRF 23）编码的，看起来和原片没有明显区别。

那么，压缩掉的 99% 是什么？如果每个像素都是随机的，信息就无从压缩。视频之所以能压缩，是因为其中有大量**冗余**：重复的、可以推算出来的、或者人根本看不出来的信息。下面分三种来看。

## 2. 空间冗余：相邻的像素很像

看上一篇中的 Y 分量：天空是一大片平滑的渐变，草地、兔子的皮毛也是大面积相近的颜色。一个像素和它左边的像素，大多数时候只差一点点。

既然如此，与其存每个像素的值，不如存它和左边像素的**差值**。差值大多是 0、±1、±2 这样的小数，出现得非常集中，就可以用更少的位来表示：常见的值用短编码，罕见的值用长编码（类似摩尔斯电码中 E 只用一个点）。

用**信息熵**来衡量能省多少：熵是按出现频率编码时，平均每个符号至少需要多少位。值越集中，熵越小。下面的程序分别统计像素值和差值的分布，计算熵：

```c
// spatial.c：比较「直接存像素」和「存与左边像素的差」各需要多少位
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// 信息熵：按出现频率编码时，平均每个符号最少需要多少位
static double entropy(const long *count, int n, long total)
{
    double h = 0;
    for (int i = 0; i < n; i++)
        if (count[i]) {
            double p = (double)count[i] / total;
            h -= p * log2(p);
        }
    return h;
}

int main(int argc, char *argv[])
{
    if (argc != 4) { fprintf(stderr, "用法: %s 输入.yuv 宽 高\n", argv[0]); return 1; }
    int w = atoi(argv[2]), h = atoi(argv[3]);
    unsigned char *y = malloc((size_t)w * h);
    FILE *in = fopen(argv[1], "rb");
    if (!in || fread(y, 1, (size_t)w * h, in) != (size_t)w * h) { fprintf(stderr, "读取失败\n"); return 1; }
    fclose(in);

    static long raw[256], diff[511];       // 差值范围 -255~255，加 255 作为下标
    long small = 0, total = (long)w * h;
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++) {
            int cur = y[i * w + j];
            int left = j > 0 ? y[i * w + j - 1] : 128;   // 每行第一个像素和 128 比
            raw[cur]++;
            diff[cur - left + 255]++;
            if (abs(cur - left) <= 2) small++;
        }
    printf("直接存像素：熵 %.2f 位/像素\n", entropy(raw, 256, total));
    printf("存左右差值：熵 %.2f 位/像素\n", entropy(diff, 511, total));
    printf("差值在 ±2 以内的像素：%.1f%%\n", 100.0 * small / total);
    free(y);
    return 0;
}
```

```bash
clang -O2 -o spatial spatial.c -lm
./spatial frame.yuv 1280 720
```

```
直接存像素：熵 7.08 位/像素
存左右差值：熵 3.34 位/像素
差值在 ±2 以内的像素：71.6%
```

像素值本身的熵是 7.08 位，和直接用 8 位存差不多，说明像素值分布得很散，单独看一个像素，几乎无法压缩。换成差值后，熵降到 3.34 位，少了一半多；71.6% 的像素和左边相差不超过 2。仅仅换了一种表示方法，同样的信息就只需要不到一半的空间，而且完全无损。

把像素值和差值的分布画出来，差别一目了然：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="直方图对比：像素值分布在 0 到 255 的很宽范围内；与左边像素的差值高度集中在 0 附近">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(20,36)">
      <text x="0" y="-14" style="fill:var(--fg)">像素值的分布（0~255）</text>
<rect x="0.00" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="1.17" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="2.34" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="3.52" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="4.69" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="5.86" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="7.03" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="8.20" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="9.38" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="10.55" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="11.72" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="12.89" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="14.06" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="15.23" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="16.41" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="17.58" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="18.75" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="19.92" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="21.09" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="22.27" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="23.44" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="24.61" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="25.78" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="26.95" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="28.12" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="29.30" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="30.47" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="31.64" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="32.81" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="33.98" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="35.16" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="36.33" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="37.50" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="38.67" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="39.84" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="41.02" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="42.19" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="43.36" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="44.53" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="45.70" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="46.88" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="48.05" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="49.22" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="50.39" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="51.56" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="52.73" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="53.91" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="55.08" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="56.25" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="57.42" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="58.59" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="59.77" y="119.9" width="1.17" height="0.1" style="fill:var(--link)"/>
<rect x="60.94" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="62.11" y="119.9" width="1.17" height="0.1" style="fill:var(--link)"/>
<rect x="63.28" y="119.9" width="1.17" height="0.1" style="fill:var(--link)"/>
<rect x="64.45" y="119.9" width="1.17" height="0.1" style="fill:var(--link)"/>
<rect x="65.62" y="119.9" width="1.17" height="0.1" style="fill:var(--link)"/>
<rect x="66.80" y="119.9" width="1.17" height="0.1" style="fill:var(--link)"/>
<rect x="67.97" y="119.8" width="1.17" height="0.2" style="fill:var(--link)"/>
<rect x="69.14" y="119.8" width="1.17" height="0.2" style="fill:var(--link)"/>
<rect x="70.31" y="119.8" width="1.17" height="0.2" style="fill:var(--link)"/>
<rect x="71.48" y="119.7" width="1.17" height="0.3" style="fill:var(--link)"/>
<rect x="72.66" y="119.7" width="1.17" height="0.3" style="fill:var(--link)"/>
<rect x="73.83" y="119.6" width="1.17" height="0.4" style="fill:var(--link)"/>
<rect x="75.00" y="119.4" width="1.17" height="0.6" style="fill:var(--link)"/>
<rect x="76.17" y="119.1" width="1.17" height="0.9" style="fill:var(--link)"/>
<rect x="77.34" y="118.8" width="1.17" height="1.2" style="fill:var(--link)"/>
<rect x="78.52" y="118.5" width="1.17" height="1.5" style="fill:var(--link)"/>
<rect x="79.69" y="117.9" width="1.17" height="2.1" style="fill:var(--link)"/>
<rect x="80.86" y="117.7" width="1.17" height="2.3" style="fill:var(--link)"/>
<rect x="82.03" y="117.0" width="1.17" height="3.0" style="fill:var(--link)"/>
<rect x="83.20" y="116.5" width="1.17" height="3.5" style="fill:var(--link)"/>
<rect x="84.38" y="115.9" width="1.17" height="4.1" style="fill:var(--link)"/>
<rect x="85.55" y="114.7" width="1.17" height="5.3" style="fill:var(--link)"/>
<rect x="86.72" y="113.4" width="1.17" height="6.6" style="fill:var(--link)"/>
<rect x="87.89" y="112.1" width="1.17" height="7.9" style="fill:var(--link)"/>
<rect x="89.06" y="110.5" width="1.17" height="9.5" style="fill:var(--link)"/>
<rect x="90.23" y="108.2" width="1.17" height="11.8" style="fill:var(--link)"/>
<rect x="91.41" y="106.8" width="1.17" height="13.2" style="fill:var(--link)"/>
<rect x="92.58" y="105.9" width="1.17" height="14.1" style="fill:var(--link)"/>
<rect x="93.75" y="104.8" width="1.17" height="15.2" style="fill:var(--link)"/>
<rect x="94.92" y="104.4" width="1.17" height="15.6" style="fill:var(--link)"/>
<rect x="96.09" y="103.6" width="1.17" height="16.4" style="fill:var(--link)"/>
<rect x="97.27" y="102.5" width="1.17" height="17.5" style="fill:var(--link)"/>
<rect x="98.44" y="101.7" width="1.17" height="18.3" style="fill:var(--link)"/>
<rect x="99.61" y="99.1" width="1.17" height="20.9" style="fill:var(--link)"/>
<rect x="100.78" y="95.5" width="1.17" height="24.5" style="fill:var(--link)"/>
<rect x="101.95" y="93.9" width="1.17" height="26.1" style="fill:var(--link)"/>
<rect x="103.12" y="92.0" width="1.17" height="28.0" style="fill:var(--link)"/>
<rect x="104.30" y="89.3" width="1.17" height="30.7" style="fill:var(--link)"/>
<rect x="105.47" y="89.6" width="1.17" height="30.4" style="fill:var(--link)"/>
<rect x="106.64" y="86.6" width="1.17" height="33.4" style="fill:var(--link)"/>
<rect x="107.81" y="87.0" width="1.17" height="33.0" style="fill:var(--link)"/>
<rect x="108.98" y="86.0" width="1.17" height="34.0" style="fill:var(--link)"/>
<rect x="110.16" y="86.9" width="1.17" height="33.1" style="fill:var(--link)"/>
<rect x="111.33" y="86.8" width="1.17" height="33.2" style="fill:var(--link)"/>
<rect x="112.50" y="85.7" width="1.17" height="34.3" style="fill:var(--link)"/>
<rect x="113.67" y="85.3" width="1.17" height="34.7" style="fill:var(--link)"/>
<rect x="114.84" y="84.3" width="1.17" height="35.7" style="fill:var(--link)"/>
<rect x="116.02" y="84.6" width="1.17" height="35.4" style="fill:var(--link)"/>
<rect x="117.19" y="84.5" width="1.17" height="35.5" style="fill:var(--link)"/>
<rect x="118.36" y="85.8" width="1.17" height="34.2" style="fill:var(--link)"/>
<rect x="119.53" y="86.4" width="1.17" height="33.6" style="fill:var(--link)"/>
<rect x="120.70" y="86.9" width="1.17" height="33.1" style="fill:var(--link)"/>
<rect x="121.88" y="86.5" width="1.17" height="33.5" style="fill:var(--link)"/>
<rect x="123.05" y="86.8" width="1.17" height="33.2" style="fill:var(--link)"/>
<rect x="124.22" y="86.3" width="1.17" height="33.7" style="fill:var(--link)"/>
<rect x="125.39" y="88.9" width="1.17" height="31.1" style="fill:var(--link)"/>
<rect x="126.56" y="90.0" width="1.17" height="30.0" style="fill:var(--link)"/>
<rect x="127.73" y="89.1" width="1.17" height="30.9" style="fill:var(--link)"/>
<rect x="128.91" y="90.9" width="1.17" height="29.1" style="fill:var(--link)"/>
<rect x="130.08" y="92.1" width="1.17" height="27.9" style="fill:var(--link)"/>
<rect x="131.25" y="93.8" width="1.17" height="26.2" style="fill:var(--link)"/>
<rect x="132.42" y="94.0" width="1.17" height="26.0" style="fill:var(--link)"/>
<rect x="133.59" y="94.7" width="1.17" height="25.3" style="fill:var(--link)"/>
<rect x="134.77" y="95.9" width="1.17" height="24.1" style="fill:var(--link)"/>
<rect x="135.94" y="96.9" width="1.17" height="23.1" style="fill:var(--link)"/>
<rect x="137.11" y="96.7" width="1.17" height="23.3" style="fill:var(--link)"/>
<rect x="138.28" y="97.6" width="1.17" height="22.4" style="fill:var(--link)"/>
<rect x="139.45" y="98.6" width="1.17" height="21.4" style="fill:var(--link)"/>
<rect x="140.62" y="98.9" width="1.17" height="21.1" style="fill:var(--link)"/>
<rect x="141.80" y="98.9" width="1.17" height="21.1" style="fill:var(--link)"/>
<rect x="142.97" y="98.3" width="1.17" height="21.7" style="fill:var(--link)"/>
<rect x="144.14" y="98.7" width="1.17" height="21.3" style="fill:var(--link)"/>
<rect x="145.31" y="99.4" width="1.17" height="20.6" style="fill:var(--link)"/>
<rect x="146.48" y="99.3" width="1.17" height="20.7" style="fill:var(--link)"/>
<rect x="147.66" y="99.4" width="1.17" height="20.6" style="fill:var(--link)"/>
<rect x="148.83" y="99.4" width="1.17" height="20.6" style="fill:var(--link)"/>
<rect x="150.00" y="99.2" width="1.17" height="20.8" style="fill:var(--link)"/>
<rect x="151.17" y="100.2" width="1.17" height="19.8" style="fill:var(--link)"/>
<rect x="152.34" y="101.1" width="1.17" height="18.9" style="fill:var(--link)"/>
<rect x="153.52" y="102.1" width="1.17" height="17.9" style="fill:var(--link)"/>
<rect x="154.69" y="102.4" width="1.17" height="17.6" style="fill:var(--link)"/>
<rect x="155.86" y="103.5" width="1.17" height="16.5" style="fill:var(--link)"/>
<rect x="157.03" y="104.5" width="1.17" height="15.5" style="fill:var(--link)"/>
<rect x="158.20" y="104.5" width="1.17" height="15.5" style="fill:var(--link)"/>
<rect x="159.38" y="105.8" width="1.17" height="14.2" style="fill:var(--link)"/>
<rect x="160.55" y="105.1" width="1.17" height="14.9" style="fill:var(--link)"/>
<rect x="161.72" y="104.9" width="1.17" height="15.1" style="fill:var(--link)"/>
<rect x="162.89" y="105.3" width="1.17" height="14.7" style="fill:var(--link)"/>
<rect x="164.06" y="105.2" width="1.17" height="14.8" style="fill:var(--link)"/>
<rect x="165.23" y="105.4" width="1.17" height="14.6" style="fill:var(--link)"/>
<rect x="166.41" y="104.9" width="1.17" height="15.1" style="fill:var(--link)"/>
<rect x="167.58" y="105.1" width="1.17" height="14.9" style="fill:var(--link)"/>
<rect x="168.75" y="105.9" width="1.17" height="14.1" style="fill:var(--link)"/>
<rect x="169.92" y="105.8" width="1.17" height="14.2" style="fill:var(--link)"/>
<rect x="171.09" y="106.8" width="1.17" height="13.2" style="fill:var(--link)"/>
<rect x="172.27" y="107.3" width="1.17" height="12.7" style="fill:var(--link)"/>
<rect x="173.44" y="107.3" width="1.17" height="12.7" style="fill:var(--link)"/>
<rect x="174.61" y="106.0" width="1.17" height="14.0" style="fill:var(--link)"/>
<rect x="175.78" y="105.7" width="1.17" height="14.3" style="fill:var(--link)"/>
<rect x="176.95" y="106.6" width="1.17" height="13.4" style="fill:var(--link)"/>
<rect x="178.12" y="106.5" width="1.17" height="13.5" style="fill:var(--link)"/>
<rect x="179.30" y="104.3" width="1.17" height="15.7" style="fill:var(--link)"/>
<rect x="180.47" y="105.4" width="1.17" height="14.6" style="fill:var(--link)"/>
<rect x="181.64" y="106.9" width="1.17" height="13.1" style="fill:var(--link)"/>
<rect x="182.81" y="107.9" width="1.17" height="12.1" style="fill:var(--link)"/>
<rect x="183.98" y="108.2" width="1.17" height="11.8" style="fill:var(--link)"/>
<rect x="185.16" y="108.5" width="1.17" height="11.5" style="fill:var(--link)"/>
<rect x="186.33" y="108.3" width="1.17" height="11.7" style="fill:var(--link)"/>
<rect x="187.50" y="107.2" width="1.17" height="12.8" style="fill:var(--link)"/>
<rect x="188.67" y="105.6" width="1.17" height="14.4" style="fill:var(--link)"/>
<rect x="189.84" y="104.9" width="1.17" height="15.1" style="fill:var(--link)"/>
<rect x="191.02" y="103.1" width="1.17" height="16.9" style="fill:var(--link)"/>
<rect x="192.19" y="103.4" width="1.17" height="16.6" style="fill:var(--link)"/>
<rect x="193.36" y="103.5" width="1.17" height="16.5" style="fill:var(--link)"/>
<rect x="194.53" y="100.8" width="1.17" height="19.2" style="fill:var(--link)"/>
<rect x="195.70" y="101.2" width="1.17" height="18.8" style="fill:var(--link)"/>
<rect x="196.88" y="100.6" width="1.17" height="19.4" style="fill:var(--link)"/>
<rect x="198.05" y="100.1" width="1.17" height="19.9" style="fill:var(--link)"/>
<rect x="199.22" y="99.3" width="1.17" height="20.7" style="fill:var(--link)"/>
<rect x="200.39" y="94.0" width="1.17" height="26.0" style="fill:var(--link)"/>
<rect x="201.56" y="96.4" width="1.17" height="23.6" style="fill:var(--link)"/>
<rect x="202.73" y="96.4" width="1.17" height="23.6" style="fill:var(--link)"/>
<rect x="203.91" y="88.5" width="1.17" height="31.5" style="fill:var(--link)"/>
<rect x="205.08" y="87.8" width="1.17" height="32.2" style="fill:var(--link)"/>
<rect x="206.25" y="79.5" width="1.17" height="40.5" style="fill:var(--link)"/>
<rect x="207.42" y="77.9" width="1.17" height="42.1" style="fill:var(--link)"/>
<rect x="208.59" y="53.8" width="1.17" height="66.2" style="fill:var(--link)"/>
<rect x="209.77" y="67.8" width="1.17" height="52.2" style="fill:var(--link)"/>
<rect x="210.94" y="68.3" width="1.17" height="51.7" style="fill:var(--link)"/>
<rect x="212.11" y="64.6" width="1.17" height="55.4" style="fill:var(--link)"/>
<rect x="213.28" y="62.0" width="1.17" height="58.0" style="fill:var(--link)"/>
<rect x="214.45" y="61.7" width="1.17" height="58.3" style="fill:var(--link)"/>
<rect x="215.62" y="58.8" width="1.17" height="61.2" style="fill:var(--link)"/>
<rect x="216.80" y="38.2" width="1.17" height="81.8" style="fill:var(--link)"/>
<rect x="217.97" y="66.4" width="1.17" height="53.6" style="fill:var(--link)"/>
<rect x="219.14" y="70.7" width="1.17" height="49.3" style="fill:var(--link)"/>
<rect x="220.31" y="75.2" width="1.17" height="44.8" style="fill:var(--link)"/>
<rect x="221.48" y="74.6" width="1.17" height="45.4" style="fill:var(--link)"/>
<rect x="222.66" y="77.7" width="1.17" height="42.3" style="fill:var(--link)"/>
<rect x="223.83" y="61.4" width="1.17" height="58.6" style="fill:var(--link)"/>
<rect x="225.00" y="78.6" width="1.17" height="41.4" style="fill:var(--link)"/>
<rect x="226.17" y="80.1" width="1.17" height="39.9" style="fill:var(--link)"/>
<rect x="227.34" y="77.3" width="1.17" height="42.7" style="fill:var(--link)"/>
<rect x="228.52" y="69.9" width="1.17" height="50.1" style="fill:var(--link)"/>
<rect x="229.69" y="70.1" width="1.17" height="49.9" style="fill:var(--link)"/>
<rect x="230.86" y="27.3" width="1.17" height="92.7" style="fill:var(--link)"/>
<rect x="232.03" y="72.9" width="1.17" height="47.1" style="fill:var(--link)"/>
<rect x="233.20" y="76.7" width="1.17" height="43.3" style="fill:var(--link)"/>
<rect x="234.38" y="83.2" width="1.17" height="36.8" style="fill:var(--link)"/>
<rect x="235.55" y="85.6" width="1.17" height="34.4" style="fill:var(--link)"/>
<rect x="236.72" y="82.0" width="1.17" height="38.0" style="fill:var(--link)"/>
<rect x="237.89" y="68.3" width="1.17" height="51.7" style="fill:var(--link)"/>
<rect x="239.06" y="86.8" width="1.17" height="33.2" style="fill:var(--link)"/>
<rect x="240.23" y="92.4" width="1.17" height="27.6" style="fill:var(--link)"/>
<rect x="241.41" y="91.7" width="1.17" height="28.3" style="fill:var(--link)"/>
<rect x="242.58" y="96.4" width="1.17" height="23.6" style="fill:var(--link)"/>
<rect x="243.75" y="77.6" width="1.17" height="42.4" style="fill:var(--link)"/>
<rect x="244.92" y="99.2" width="1.17" height="20.8" style="fill:var(--link)"/>
<rect x="246.09" y="102.1" width="1.17" height="17.9" style="fill:var(--link)"/>
<rect x="247.27" y="100.7" width="1.17" height="19.3" style="fill:var(--link)"/>
<rect x="248.44" y="100.3" width="1.17" height="19.7" style="fill:var(--link)"/>
<rect x="249.61" y="91.9" width="1.17" height="28.1" style="fill:var(--link)"/>
<rect x="250.78" y="99.5" width="1.17" height="20.5" style="fill:var(--link)"/>
<rect x="251.95" y="100.3" width="1.17" height="19.7" style="fill:var(--link)"/>
<rect x="253.12" y="103.0" width="1.17" height="17.0" style="fill:var(--link)"/>
<rect x="254.30" y="104.2" width="1.17" height="15.8" style="fill:var(--link)"/>
<rect x="255.47" y="83.8" width="1.17" height="36.2" style="fill:var(--link)"/>
<rect x="256.64" y="78.4" width="1.17" height="41.6" style="fill:var(--link)"/>
<rect x="257.81" y="9.3" width="1.17" height="110.7" style="fill:var(--link)"/>
<rect x="258.98" y="44.8" width="1.17" height="75.2" style="fill:var(--link)"/>
<rect x="260.16" y="0.0" width="1.17" height="120.0" style="fill:var(--link)"/>
<rect x="261.33" y="40.1" width="1.17" height="79.9" style="fill:var(--link)"/>
<rect x="262.50" y="85.8" width="1.17" height="34.2" style="fill:var(--link)"/>
<rect x="263.67" y="72.2" width="1.17" height="47.8" style="fill:var(--link)"/>
<rect x="264.84" y="7.0" width="1.17" height="113.0" style="fill:var(--link)"/>
<rect x="266.02" y="78.1" width="1.17" height="41.9" style="fill:var(--link)"/>
<rect x="267.19" y="90.9" width="1.17" height="29.1" style="fill:var(--link)"/>
<rect x="268.36" y="115.4" width="1.17" height="4.6" style="fill:var(--link)"/>
<rect x="269.53" y="117.7" width="1.17" height="2.3" style="fill:var(--link)"/>
<rect x="270.70" y="118.0" width="1.17" height="2.0" style="fill:var(--link)"/>
<rect x="271.88" y="117.7" width="1.17" height="2.3" style="fill:var(--link)"/>
<rect x="273.05" y="117.3" width="1.17" height="2.7" style="fill:var(--link)"/>
<rect x="274.22" y="118.6" width="1.17" height="1.4" style="fill:var(--link)"/>
<rect x="275.39" y="119.4" width="1.17" height="0.6" style="fill:var(--link)"/>
<rect x="276.56" y="119.8" width="1.17" height="0.2" style="fill:var(--link)"/>
<rect x="277.73" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="278.91" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="280.08" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="281.25" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="282.42" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="283.59" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="284.77" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="285.94" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="287.11" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="288.28" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="289.45" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="290.62" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="291.80" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="292.97" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="294.14" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="295.31" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="296.48" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="297.66" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<rect x="298.83" y="120.0" width="1.17" height="0.0" style="fill:var(--link)"/>
<line x1="0" y1="120" x2="300" y2="120" style="stroke:var(--border-2)"/>
<text x="0.6" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">0</text>
<text x="75.6" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">64</text>
<text x="150.6" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">128</text>
<text x="225.6" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">192</text>
<text x="299.4" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">255</text>
    </g>
    <g transform="translate(360,36)">
      <text x="0" y="-14" style="fill:var(--fg)">差值的分布（只画 −32~32）</text>
<rect x="0.00" y="120.0" width="4.62" height="0.0" style="fill:var(--accent)"/>
<rect x="4.62" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="9.23" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="13.85" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="18.46" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="23.08" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="27.69" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="32.31" y="119.8" width="4.62" height="0.2" style="fill:var(--accent)"/>
<rect x="36.92" y="119.8" width="4.62" height="0.2" style="fill:var(--accent)"/>
<rect x="41.54" y="119.8" width="4.62" height="0.2" style="fill:var(--accent)"/>
<rect x="46.15" y="119.7" width="4.62" height="0.3" style="fill:var(--accent)"/>
<rect x="50.77" y="119.7" width="4.62" height="0.3" style="fill:var(--accent)"/>
<rect x="55.38" y="119.6" width="4.62" height="0.4" style="fill:var(--accent)"/>
<rect x="60.00" y="119.6" width="4.62" height="0.4" style="fill:var(--accent)"/>
<rect x="64.62" y="119.5" width="4.62" height="0.5" style="fill:var(--accent)"/>
<rect x="69.23" y="119.4" width="4.62" height="0.6" style="fill:var(--accent)"/>
<rect x="73.85" y="119.4" width="4.62" height="0.6" style="fill:var(--accent)"/>
<rect x="78.46" y="119.2" width="4.62" height="0.8" style="fill:var(--accent)"/>
<rect x="83.08" y="119.1" width="4.62" height="0.9" style="fill:var(--accent)"/>
<rect x="87.69" y="119.0" width="4.62" height="1.0" style="fill:var(--accent)"/>
<rect x="92.31" y="118.8" width="4.62" height="1.2" style="fill:var(--accent)"/>
<rect x="96.92" y="118.6" width="4.62" height="1.4" style="fill:var(--accent)"/>
<rect x="101.54" y="118.4" width="4.62" height="1.6" style="fill:var(--accent)"/>
<rect x="106.15" y="118.2" width="4.62" height="1.8" style="fill:var(--accent)"/>
<rect x="110.77" y="117.9" width="4.62" height="2.1" style="fill:var(--accent)"/>
<rect x="115.38" y="117.5" width="4.62" height="2.5" style="fill:var(--accent)"/>
<rect x="120.00" y="117.1" width="4.62" height="2.9" style="fill:var(--accent)"/>
<rect x="124.62" y="116.6" width="4.62" height="3.4" style="fill:var(--accent)"/>
<rect x="129.23" y="115.9" width="4.62" height="4.1" style="fill:var(--accent)"/>
<rect x="133.85" y="114.7" width="4.62" height="5.3" style="fill:var(--accent)"/>
<rect x="138.46" y="112.4" width="4.62" height="7.6" style="fill:var(--accent)"/>
<rect x="143.08" y="101.8" width="4.62" height="18.2" style="fill:var(--accent)"/>
<rect x="147.69" y="0.0" width="4.62" height="120.0" style="fill:var(--accent)"/>
<rect x="152.31" y="102.5" width="4.62" height="17.5" style="fill:var(--accent)"/>
<rect x="156.92" y="112.5" width="4.62" height="7.5" style="fill:var(--accent)"/>
<rect x="161.54" y="114.5" width="4.62" height="5.5" style="fill:var(--accent)"/>
<rect x="166.15" y="115.7" width="4.62" height="4.3" style="fill:var(--accent)"/>
<rect x="170.77" y="116.4" width="4.62" height="3.6" style="fill:var(--accent)"/>
<rect x="175.38" y="117.0" width="4.62" height="3.0" style="fill:var(--accent)"/>
<rect x="180.00" y="117.5" width="4.62" height="2.5" style="fill:var(--accent)"/>
<rect x="184.62" y="117.8" width="4.62" height="2.2" style="fill:var(--accent)"/>
<rect x="189.23" y="118.1" width="4.62" height="1.9" style="fill:var(--accent)"/>
<rect x="193.85" y="118.4" width="4.62" height="1.6" style="fill:var(--accent)"/>
<rect x="198.46" y="118.6" width="4.62" height="1.4" style="fill:var(--accent)"/>
<rect x="203.08" y="118.8" width="4.62" height="1.2" style="fill:var(--accent)"/>
<rect x="207.69" y="118.9" width="4.62" height="1.1" style="fill:var(--accent)"/>
<rect x="212.31" y="119.1" width="4.62" height="0.9" style="fill:var(--accent)"/>
<rect x="216.92" y="119.2" width="4.62" height="0.8" style="fill:var(--accent)"/>
<rect x="221.54" y="119.3" width="4.62" height="0.7" style="fill:var(--accent)"/>
<rect x="226.15" y="119.4" width="4.62" height="0.6" style="fill:var(--accent)"/>
<rect x="230.77" y="119.5" width="4.62" height="0.5" style="fill:var(--accent)"/>
<rect x="235.38" y="119.6" width="4.62" height="0.4" style="fill:var(--accent)"/>
<rect x="240.00" y="119.6" width="4.62" height="0.4" style="fill:var(--accent)"/>
<rect x="244.62" y="119.7" width="4.62" height="0.3" style="fill:var(--accent)"/>
<rect x="249.23" y="119.7" width="4.62" height="0.3" style="fill:var(--accent)"/>
<rect x="253.85" y="119.8" width="4.62" height="0.2" style="fill:var(--accent)"/>
<rect x="258.46" y="119.8" width="4.62" height="0.2" style="fill:var(--accent)"/>
<rect x="263.08" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="267.69" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="272.31" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="276.92" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="281.54" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="286.15" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="290.77" y="119.9" width="4.62" height="0.1" style="fill:var(--accent)"/>
<rect x="295.38" y="120.0" width="4.62" height="0.0" style="fill:var(--accent)"/>
<line x1="0" y1="120" x2="300" y2="120" style="stroke:var(--border-2)"/>
<text x="2.3" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">-32</text>
<text x="76.2" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">-16</text>
<text x="150.0" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">0</text>
<text x="223.8" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">16</text>
<text x="297.7" y="136" text-anchor="middle" style="fill:var(--muted);font-size:10px">32</text>
    </g>
    <text x="20" y="186" style="fill:var(--muted);font-size:11px">纵轴：出现的比例（两张图各自缩放）；差值最高的一根（0）占 50.3%</text>
  </g>
</svg>
</figure>

这就是**帧内预测**（intra prediction）的思路：先用已经编码过的相邻像素「猜」当前像素，只存猜错的部分（残差）。H.264 的猜法比「等于左边」聪明得多：它把画面分成小块，每个块可以从左边、上边、左上等 9 个方向中选一个最准的来预测。

此外，编码器还会把残差做一次**变换**（DCT，离散余弦变换），把一块像素变成「平均亮度 + 各种频率的起伏」。平滑的区域变换后只剩少数几个非零的数，进一步方便压缩。这部分在第 2 章讲 H.264 时再展开。

## 3. 时间冗余：相邻的帧很像

视频每秒 24 帧，相邻两帧只隔 42 毫秒，画面大部分是一样的：背景没动，只有角色移动了一点。下面的程序逐帧比较相邻两帧的 Y 分量，统计平均差值和「几乎没变」（差值不超过 2）的像素比例：

```c
// temporal.c：逐帧比较相邻两帧的亮度，看画面变了多少
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc < 4) { fprintf(stderr, "用法: %s 输入.yuv 宽 高 [保存差值的帧号]\n", argv[0]); return 1; }
    int w = atoi(argv[2]), h = atoi(argv[3]);
    int dump = argc > 4 ? atoi(argv[4]) : -1;
    size_t ysz = (size_t)w * h, fsz = ysz * 3 / 2;
    unsigned char *prev = malloc(fsz), *cur = malloc(fsz);
    FILE *in = fopen(argv[1], "rb");
    if (fread(prev, 1, fsz, in) != fsz) return 1;

    int n = 1;
    double sum_mad = 0, sum_still = 0;
    while (fread(cur, 1, fsz, in) == fsz) {
        long sad = 0, still = 0;
        for (size_t i = 0; i < ysz; i++) {
            int d = abs(cur[i] - prev[i]);
            sad += d;
            if (d <= 2) still++;               // 几乎没变的像素
        }
        double mad = (double)sad / ysz, pct = 100.0 * still / ysz;
        sum_mad += mad; sum_still += pct;
        if (mad > 20) printf("第 %3d 帧：平均差 %5.1f，几乎没变 %5.1f%%  <- 镜头切换\n", n, mad, pct);
        if (n == dump) {                       // 把差值 +128 存成一张灰度图
            FILE *f = fopen("diff.y", "wb");
            for (size_t i = 0; i < ysz; i++) {
                int d = cur[i] - prev[i] + 128;
                fputc(d < 0 ? 0 : d > 255 ? 255 : d, f);
            }
            fclose(f);
            printf("第 %3d 帧：平均差 %5.1f，几乎没变 %5.1f%%（差值已保存到 diff.y）\n", n, mad, pct);
        }
        unsigned char *t = prev; prev = cur; cur = t;
        n++;
    }
    printf("共 %d 帧，相邻帧平均差 %.2f，平均 %.1f%% 的像素几乎没变\n",
           n, sum_mad / (n - 1), sum_still / (n - 1));
    return 0;
}
```

```bash
clang -O2 -o temporal temporal.c
./temporal bbb.yuv 1280 720 160
```

```
第  31 帧：平均差  75.7，几乎没变   1.6%  <- 镜头切换
第  83 帧：平均差  57.7，几乎没变   3.5%  <- 镜头切换
第 133 帧：平均差  51.0，几乎没变   2.8%  <- 镜头切换
第 160 帧：平均差   6.0，几乎没变  42.4%（差值已保存到 diff.y）
第 239 帧：平均差  61.3，几乎没变   2.5%  <- 镜头切换
共 240 帧，相邻帧平均差 4.65，平均 69.3% 的像素几乎没变
```

240 帧中，相邻两帧平均有 69% 的像素几乎没变，亮度的平均差只有 4.65（满分 255）。如果去掉 4 次镜头切换，平均差只有 3.7。镜头切换时就完全不同了：平均差跳到 50 以上，几乎没变的像素不到 4%。第 1 帧到第 82 帧的那个镜头几乎是静止的（第 20、60 帧都有 99% 的像素没变），第 133 帧以后的镜头在跟着蝴蝶移动，变化最大。

把第 160 帧和前一帧的差值保存成图片（灰色表示没变，越亮或越暗表示变化越大）：

```bash
ffmpeg -f rawvideo -pix_fmt gray -s 1280x720 -i diff.y diff.jpg
```

<figure>
<img src="/img/av/diff-prev.jpg" alt="前一帧" width="320"> <img src="/img/av/diff-cur.jpg" alt="当前帧" width="320">
<figcaption>前一帧（左）和当前帧（右）</figcaption>
</figure>

<figure>
<img src="/img/av/diff.jpg" alt="两帧的差值：大部分是均匀的灰色，只有运动的物体边缘有亮暗的轮廓" width="640">
<figcaption>两帧之差：只有动的地方有内容</figcaption>
</figure>

第 160 帧是变化比较大的一帧：镜头在缓慢移动，蝴蝶在扇动翅膀。即便如此，差值图中绝大部分区域是平的灰色：兔子的身体、草地都只在边缘处有一圈轮廓，这是整个画面平移了一两个像素造成的；只有扇动的蝴蝶翅膀才有大面积的变化。

所以，大部分帧不需要完整地存，只要存「和前面某一帧相比，哪里变了、怎么变的」。这叫**帧间预测**（inter prediction）。

实际的编码器比直接相减更进一步：物体移动时，与其存差值，不如说「这一块等于前一帧中往左偏 5 个像素、往上偏 2 个像素的那一块」，只存这个偏移量（**运动矢量**，motion vector）和一点残差。寻找最佳偏移量的过程叫**运动估计**，是编码器中最耗时的部分。

镜头切换时，新画面和前一帧毫无关系，帧间预测就失效了，编码器会改用帧内预测，这一帧的数据量也会骤增，第 5 节会看到。

## 4. 视觉冗余：人看不出来的就不存

前两种冗余的压缩是**无损**的：差值和预测都可以精确还原原图。但只靠无损压缩，压缩比很有限（下面会实测：不到 10 倍）。剩下的十几倍甚至上百倍，来自**有损压缩**：丢掉人眼不敏感的信息。

已经见过的一个例子是 4:2:0 色度抽样，直接丢掉四分之三的颜色信息。更主要的手段是**量化**：DCT 变换后的系数除以一个数再取整，细节越多的高频系数越容易变成 0。除的数越大，丢的越多，文件越小，画质越差。

x264 用 **CRF**（恒定质量因子）控制量化的程度，范围 0~51，数值越大压缩越狠，默认 23。用不同的 CRF 编码测试视频，比较大小和画质（PSNR 越大越接近原始画面）：

```bash
for crf in 0 18 23 28 35 45; do
  ffmpeg -v error -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 24 -i bbb.yuv \
    -c:v libx264 -preset medium -crf $crf crf$crf.mp4
done
```

| CRF | 文件大小 | 码率 | 压缩比 | PSNR |
|---|---|---|---|---|
| 0（无损） | 41.5 MB | 33.2 Mbps | 8 倍 | ∞ |
| 18 | 3.88 MB | 3.10 Mbps | 86 倍 | 48.0 dB |
| 23（默认） | 2.37 MB | 1.89 Mbps | 140 倍 | 45.7 dB |
| 28 | 1.34 MB | 1.07 Mbps | 247 倍 | 41.9 dB |
| 35 | 0.64 MB | 0.51 Mbps | 517 倍 | 37.7 dB |
| 45 | 0.23 MB | 0.18 Mbps | 1459 倍 | 32.3 dB |

（压缩比是相对于 332 MB 的原始 YUV；PSNR 是和原始 YUV 比较，计算方法同第 1 篇。）

CRF 0 是**无损**编码，解码出来和原始数据一模一样，PSNR 是无穷大，但文件有 41.5 MB，只压缩了 8 倍：这就是空间冗余和时间冗余能带来的全部。从 CRF 0 到 CRF 18，文件一下子缩小到十分之一，PSNR 仍然有 48 dB，人眼看不出区别：这十倍完全来自丢掉「看不出来的东西」。此后 CRF 每增加 6，文件大约减半。截取第 160 帧中蝴蝶的一角（320×240），放大 1.5 倍：

<figure>
<img src="/img/av/crf-compare.png" alt="不同 CRF 下的画面局部放大对比：CRF 23 和原图几乎一样，CRF 35 草地纹理被抹平，CRF 45 出现明显的方块" width="640">
<figcaption>左上：原始；右上：CRF 23；左下：CRF 35；右下：CRF 45。CRF 35 的草地纹理已经被抹平，CRF 45 出现了明显的方块</figcaption>
</figure>

**无损压缩只能利用冗余，有损压缩才能获得上百倍的压缩比**。几乎所有的视频编码都是有损的，编码器要解决的问题就是：在给定的码率下，丢掉哪些信息最不容易被发现。

## 5. I 帧、P 帧、B 帧

利用时间冗余之后，视频中的帧就分成了几类：

- **I 帧**（Intra，帧内编码帧）：只用帧内预测，不依赖其他帧，可以单独解码出一张完整的图。数据量最大。
- **P 帧**（Predicted，前向预测帧）：参考**前面**已解码的帧，只存差别。
- **B 帧**（Bi-directional，双向预测帧）：同时参考**前面和后面**的帧。例如物体从左移到右，中间那帧可以取前后两帧的平均。数据量最小。

从一个 I 帧开始，到下一个 I 帧之前的这一组帧，叫一个 **GOP**（Group of Pictures，图像组）：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="GOP 结构示意图：I B B P B B P B B P，然后是下一个 I 帧；P 帧参考前面的 I 或 P 帧，B 帧同时参考前后两帧">
  <defs>
    <marker id="gop-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:13px">
    <g transform="translate(15,70)">
      <rect x="0" y="0" width="46" height="60" style="fill:var(--danger);opacity:.8"/><text x="18" y="35" fill="#fff">I</text>
      <rect x="56" y="15" width="46" height="45" style="fill:var(--link);opacity:.55"/><text x="74" y="43" fill="#fff">B</text>
      <rect x="112" y="15" width="46" height="45" style="fill:var(--link);opacity:.55"/><text x="130" y="43" fill="#fff">B</text>
      <rect x="168" y="5" width="46" height="55" style="fill:var(--accent);opacity:.8"/><text x="186" y="38" fill="#fff">P</text>
      <rect x="224" y="15" width="46" height="45" style="fill:var(--link);opacity:.55"/><text x="242" y="43" fill="#fff">B</text>
      <rect x="280" y="15" width="46" height="45" style="fill:var(--link);opacity:.55"/><text x="298" y="43" fill="#fff">B</text>
      <rect x="336" y="5" width="46" height="55" style="fill:var(--accent);opacity:.8"/><text x="354" y="38" fill="#fff">P</text>
      <rect x="392" y="15" width="46" height="45" style="fill:var(--link);opacity:.55"/><text x="410" y="43" fill="#fff">B</text>
      <rect x="448" y="15" width="46" height="45" style="fill:var(--link);opacity:.55"/><text x="466" y="43" fill="#fff">B</text>
      <rect x="504" y="5" width="46" height="55" style="fill:var(--accent);opacity:.8"/><text x="522" y="38" fill="#fff">P</text>
      <rect x="570" y="0" width="46" height="60" style="fill:var(--danger);opacity:.8;stroke:var(--fg);stroke-dasharray:4 3"/><text x="588" y="35" fill="#fff">I</text>
      <g fill="none" style="stroke:var(--muted)" stroke-width="1.3" marker-end="url(#gop-arrow)">
        <path d="M191,5 C150,-40 60,-40 28,-4"/>
        <path d="M359,5 C320,-40 230,-40 196,1"/>
        <path d="M527,5 C490,-40 400,-40 364,1"/>
        <path d="M79,62 C70,85 40,85 26,64"/>
        <path d="M79,62 C100,90 170,90 188,64"/>
      </g>
      <text x="100" y="105" style="fill:var(--muted);font-size:11px">B 帧参考前后两帧</text>
      <text x="230" y="-45" style="fill:var(--muted);font-size:11px">P 帧参考前面的 I 或 P 帧</text>
      <line x1="0" y1="118" x2="566" y2="118" style="stroke:var(--fg)"/>
      <text x="230" y="112" style="fill:var(--fg);font-size:12px">一个 GOP</text>
    </g>
  </g>
</svg>
</figure>

（块的高度示意数据量的大小。）

用 `ffprobe` 列出测试视频每一帧的类型和大小，再用 `awk` 统计：

```bash
ffprobe -v error -select_streams v -show_frames -show_entries frame=pict_type,pkt_size \
  -of csv=p=0 bbb-10s.mp4 | cut -d, -f1,2 > frames.csv
head -12 frames.csv | tr '\n' ' '; echo
awk -F, '{n[$2]++; s[$2]+=$1} END {for (t in n) printf "%s 帧 %3d 个，平均 %6.1f KB\n", t, n[t], s[t]/n[t]/1024}' frames.csv
```

```
96518,I 693,B 497,B 2276,P 650,B 495,B 2233,P 549,B 460,B 1747,P 675,B 304,B 
B 帧 142 个，平均    4.5 KB
I 帧   8 个，平均  110.5 KB
P 帧  90 个，平均   13.8 KB
```

（每行是「大小,类型」，按显示顺序排列。）开头就是一个典型的 GOP 结构：I B B P B B P……。一个 I 帧平均 110 KB，P 帧 13.8 KB，B 帧只有 4.5 KB，相差 25 倍。如果每一帧都编码成 I 帧，文件会大十几倍。

把每一帧的大小画成柱状图，I、P、B 帧的差别和 GOP 的周期就很明显了：

<figure class="diagram">
<svg viewBox="0 0 690 215" role="img" aria-label="测试视频 240 帧的大小柱状图，I 帧最高，P 帧次之，B 帧最矮">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(40,15)">
<line x1="0" y1="150.0" x2="640" y2="150.0" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="154.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0K</text>
<line x1="0" y1="116.5" x2="640" y2="116.5" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="120.5" text-anchor="end" style="fill:var(--muted);font-size:10px">40K</text>
<line x1="0" y1="83.1" x2="640" y2="83.1" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="87.1" text-anchor="end" style="fill:var(--muted);font-size:10px">80K</text>
<line x1="0" y1="49.6" x2="640" y2="49.6" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="53.6" text-anchor="end" style="fill:var(--muted);font-size:10px">120K</text>
<line x1="0" y1="16.2" x2="640" y2="16.2" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="20.2" text-anchor="end" style="fill:var(--muted);font-size:10px">160K</text>
<text x="0.0" y="166" text-anchor="middle" style="fill:var(--muted);font-size:10px">0s</text>
<text x="128.0" y="166" text-anchor="middle" style="fill:var(--muted);font-size:10px">2s</text>
<text x="256.0" y="166" text-anchor="middle" style="fill:var(--muted);font-size:10px">4s</text>
<text x="384.0" y="166" text-anchor="middle" style="fill:var(--muted);font-size:10px">6s</text>
<text x="512.0" y="166" text-anchor="middle" style="fill:var(--muted);font-size:10px">8s</text>
<text x="640.0" y="166" text-anchor="middle" style="fill:var(--muted);font-size:10px">10s</text>
<rect x="0.00" y="71.2" width="2.13" height="78.8" style="fill:var(--danger)"/>
<rect x="2.67" y="149.4" width="2.13" height="0.6" style="fill:var(--link)"/>
<rect x="5.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="8.00" y="148.1" width="2.13" height="1.9" style="fill:var(--accent)"/>
<rect x="10.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="13.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="16.00" y="148.2" width="2.13" height="1.8" style="fill:var(--accent)"/>
<rect x="18.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="21.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="24.00" y="148.6" width="2.13" height="1.4" style="fill:var(--accent)"/>
<rect x="26.67" y="149.4" width="2.13" height="0.6" style="fill:var(--link)"/>
<rect x="29.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="32.00" y="148.6" width="2.13" height="1.4" style="fill:var(--accent)"/>
<rect x="34.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="37.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="40.00" y="148.6" width="2.13" height="1.4" style="fill:var(--accent)"/>
<rect x="42.67" y="149.4" width="2.13" height="0.6" style="fill:var(--link)"/>
<rect x="45.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="48.00" y="148.8" width="2.13" height="1.2" style="fill:var(--accent)"/>
<rect x="50.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="53.33" y="149.4" width="2.13" height="0.6" style="fill:var(--accent)"/>
<rect x="56.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="58.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="61.33" y="149.5" width="2.13" height="0.5" style="fill:var(--accent)"/>
<rect x="64.00" y="149.3" width="2.13" height="0.7" style="fill:var(--link)"/>
<rect x="66.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="69.33" y="147.4" width="2.13" height="2.6" style="fill:var(--accent)"/>
<rect x="72.00" y="149.4" width="2.13" height="0.6" style="fill:var(--link)"/>
<rect x="74.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="77.33" y="149.1" width="2.13" height="0.9" style="fill:var(--accent)"/>
<rect x="80.00" y="149.4" width="2.13" height="0.6" style="fill:var(--accent)"/>
<rect x="82.67" y="0.0" width="2.13" height="150.0" style="fill:var(--danger)"/>
<rect x="85.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="88.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="90.67" y="148.7" width="2.13" height="1.3" style="fill:var(--accent)"/>
<rect x="93.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="96.00" y="148.8" width="2.13" height="1.2" style="fill:var(--accent)"/>
<rect x="98.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="101.33" y="148.8" width="2.13" height="1.2" style="fill:var(--accent)"/>
<rect x="104.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="106.67" y="148.4" width="2.13" height="1.6" style="fill:var(--accent)"/>
<rect x="109.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="112.00" y="148.3" width="2.13" height="1.7" style="fill:var(--accent)"/>
<rect x="114.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="117.33" y="148.0" width="2.13" height="2.0" style="fill:var(--accent)"/>
<rect x="120.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="122.67" y="147.9" width="2.13" height="2.1" style="fill:var(--accent)"/>
<rect x="125.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="128.00" y="149.3" width="2.13" height="0.7" style="fill:var(--link)"/>
<rect x="130.67" y="141.5" width="2.13" height="8.5" style="fill:var(--accent)"/>
<rect x="133.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="136.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="138.67" y="145.6" width="2.13" height="4.4" style="fill:var(--accent)"/>
<rect x="141.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="144.00" y="146.3" width="2.13" height="3.7" style="fill:var(--accent)"/>
<rect x="146.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="149.33" y="146.4" width="2.13" height="3.6" style="fill:var(--accent)"/>
<rect x="152.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="154.67" y="146.4" width="2.13" height="3.6" style="fill:var(--accent)"/>
<rect x="157.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="160.00" y="146.6" width="2.13" height="3.4" style="fill:var(--accent)"/>
<rect x="162.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="165.33" y="146.6" width="2.13" height="3.4" style="fill:var(--accent)"/>
<rect x="168.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="170.67" y="146.7" width="2.13" height="3.3" style="fill:var(--accent)"/>
<rect x="173.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="176.00" y="146.5" width="2.13" height="3.5" style="fill:var(--accent)"/>
<rect x="178.67" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="181.33" y="146.9" width="2.13" height="3.1" style="fill:var(--accent)"/>
<rect x="184.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="186.67" y="147.3" width="2.13" height="2.7" style="fill:var(--accent)"/>
<rect x="189.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="192.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="194.67" y="142.8" width="2.13" height="7.2" style="fill:var(--accent)"/>
<rect x="197.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="200.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="202.67" y="147.2" width="2.13" height="2.8" style="fill:var(--accent)"/>
<rect x="205.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="208.00" y="148.5" width="2.13" height="1.5" style="fill:var(--accent)"/>
<rect x="210.67" y="56.8" width="2.13" height="93.2" style="fill:var(--danger)"/>
<rect x="213.33" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="216.00" y="149.5" width="2.13" height="0.5" style="fill:var(--link)"/>
<rect x="218.67" y="149.4" width="2.13" height="0.6" style="fill:var(--accent)"/>
<rect x="221.33" y="106.1" width="2.13" height="43.9" style="fill:var(--danger)"/>
<rect x="224.00" y="148.3" width="2.13" height="1.7" style="fill:var(--link)"/>
<rect x="226.67" y="148.4" width="2.13" height="1.6" style="fill:var(--link)"/>
<rect x="229.33" y="143.8" width="2.13" height="6.2" style="fill:var(--accent)"/>
<rect x="232.00" y="148.2" width="2.13" height="1.8" style="fill:var(--link)"/>
<rect x="234.67" y="148.6" width="2.13" height="1.4" style="fill:var(--link)"/>
<rect x="237.33" y="144.3" width="2.13" height="5.7" style="fill:var(--accent)"/>
<rect x="240.00" y="148.0" width="2.13" height="2.0" style="fill:var(--link)"/>
<rect x="242.67" y="148.2" width="2.13" height="1.8" style="fill:var(--link)"/>
<rect x="245.33" y="143.7" width="2.13" height="6.3" style="fill:var(--accent)"/>
<rect x="248.00" y="148.4" width="2.13" height="1.6" style="fill:var(--link)"/>
<rect x="250.67" y="144.0" width="2.13" height="6.0" style="fill:var(--accent)"/>
<rect x="253.33" y="148.5" width="2.13" height="1.5" style="fill:var(--link)"/>
<rect x="256.00" y="148.2" width="2.13" height="1.8" style="fill:var(--link)"/>
<rect x="258.67" y="141.6" width="2.13" height="8.4" style="fill:var(--accent)"/>
<rect x="261.33" y="147.6" width="2.13" height="2.4" style="fill:var(--link)"/>
<rect x="264.00" y="148.2" width="2.13" height="1.8" style="fill:var(--link)"/>
<rect x="266.67" y="142.9" width="2.13" height="7.1" style="fill:var(--accent)"/>
<rect x="269.33" y="148.2" width="2.13" height="1.8" style="fill:var(--link)"/>
<rect x="272.00" y="144.0" width="2.13" height="6.0" style="fill:var(--accent)"/>
<rect x="274.67" y="147.7" width="2.13" height="2.3" style="fill:var(--link)"/>
<rect x="277.33" y="141.4" width="2.13" height="8.6" style="fill:var(--accent)"/>
<rect x="280.00" y="146.6" width="2.13" height="3.4" style="fill:var(--link)"/>
<rect x="282.67" y="138.7" width="2.13" height="11.3" style="fill:var(--accent)"/>
<rect x="285.33" y="146.0" width="2.13" height="4.0" style="fill:var(--link)"/>
<rect x="288.00" y="138.5" width="2.13" height="11.5" style="fill:var(--accent)"/>
<rect x="290.67" y="146.0" width="2.13" height="4.0" style="fill:var(--link)"/>
<rect x="293.33" y="138.3" width="2.13" height="11.7" style="fill:var(--accent)"/>
<rect x="296.00" y="145.9" width="2.13" height="4.1" style="fill:var(--link)"/>
<rect x="298.67" y="138.1" width="2.13" height="11.9" style="fill:var(--accent)"/>
<rect x="301.33" y="145.7" width="2.13" height="4.3" style="fill:var(--link)"/>
<rect x="304.00" y="138.1" width="2.13" height="11.9" style="fill:var(--accent)"/>
<rect x="306.67" y="145.7" width="2.13" height="4.3" style="fill:var(--link)"/>
<rect x="309.33" y="137.9" width="2.13" height="12.1" style="fill:var(--accent)"/>
<rect x="312.00" y="145.8" width="2.13" height="4.2" style="fill:var(--link)"/>
<rect x="314.67" y="137.7" width="2.13" height="12.3" style="fill:var(--accent)"/>
<rect x="317.33" y="143.9" width="2.13" height="6.1" style="fill:var(--link)"/>
<rect x="320.00" y="144.7" width="2.13" height="5.3" style="fill:var(--link)"/>
<rect x="322.67" y="131.9" width="2.13" height="18.1" style="fill:var(--accent)"/>
<rect x="325.33" y="145.2" width="2.13" height="4.8" style="fill:var(--link)"/>
<rect x="328.00" y="147.2" width="2.13" height="2.8" style="fill:var(--link)"/>
<rect x="330.67" y="134.9" width="2.13" height="15.1" style="fill:var(--accent)"/>
<rect x="333.33" y="147.8" width="2.13" height="2.2" style="fill:var(--link)"/>
<rect x="336.00" y="142.9" width="2.13" height="7.1" style="fill:var(--accent)"/>
<rect x="338.67" y="147.6" width="2.13" height="2.4" style="fill:var(--link)"/>
<rect x="341.33" y="142.4" width="2.13" height="7.6" style="fill:var(--accent)"/>
<rect x="344.00" y="147.7" width="2.13" height="2.3" style="fill:var(--link)"/>
<rect x="346.67" y="141.8" width="2.13" height="8.2" style="fill:var(--accent)"/>
<rect x="349.33" y="121.4" width="2.13" height="28.6" style="fill:var(--danger)"/>
<rect x="352.00" y="146.6" width="2.13" height="3.4" style="fill:var(--accent)"/>
<rect x="354.67" y="3.4" width="2.13" height="146.6" style="fill:var(--danger)"/>
<rect x="357.33" y="147.1" width="2.13" height="2.9" style="fill:var(--link)"/>
<rect x="360.00" y="147.0" width="2.13" height="3.0" style="fill:var(--link)"/>
<rect x="362.67" y="140.7" width="2.13" height="9.3" style="fill:var(--accent)"/>
<rect x="365.33" y="145.2" width="2.13" height="4.8" style="fill:var(--link)"/>
<rect x="368.00" y="145.9" width="2.13" height="4.1" style="fill:var(--link)"/>
<rect x="370.67" y="136.8" width="2.13" height="13.2" style="fill:var(--accent)"/>
<rect x="373.33" y="143.9" width="2.13" height="6.1" style="fill:var(--link)"/>
<rect x="376.00" y="145.0" width="2.13" height="5.0" style="fill:var(--link)"/>
<rect x="378.67" y="129.1" width="2.13" height="20.9" style="fill:var(--accent)"/>
<rect x="381.33" y="145.3" width="2.13" height="4.7" style="fill:var(--link)"/>
<rect x="384.00" y="146.3" width="2.13" height="3.7" style="fill:var(--link)"/>
<rect x="386.67" y="126.2" width="2.13" height="23.8" style="fill:var(--accent)"/>
<rect x="389.33" y="145.9" width="2.13" height="4.1" style="fill:var(--link)"/>
<rect x="392.00" y="146.4" width="2.13" height="3.6" style="fill:var(--link)"/>
<rect x="394.67" y="131.5" width="2.13" height="18.5" style="fill:var(--accent)"/>
<rect x="397.33" y="145.2" width="2.13" height="4.8" style="fill:var(--link)"/>
<rect x="400.00" y="145.7" width="2.13" height="4.3" style="fill:var(--link)"/>
<rect x="402.67" y="132.0" width="2.13" height="18.0" style="fill:var(--accent)"/>
<rect x="405.33" y="145.0" width="2.13" height="5.0" style="fill:var(--link)"/>
<rect x="408.00" y="146.6" width="2.13" height="3.4" style="fill:var(--link)"/>
<rect x="410.67" y="131.1" width="2.13" height="18.9" style="fill:var(--accent)"/>
<rect x="413.33" y="145.1" width="2.13" height="4.9" style="fill:var(--link)"/>
<rect x="416.00" y="145.7" width="2.13" height="4.3" style="fill:var(--link)"/>
<rect x="418.67" y="133.0" width="2.13" height="17.0" style="fill:var(--accent)"/>
<rect x="421.33" y="144.8" width="2.13" height="5.2" style="fill:var(--link)"/>
<rect x="424.00" y="146.0" width="2.13" height="4.0" style="fill:var(--link)"/>
<rect x="426.67" y="129.0" width="2.13" height="21.0" style="fill:var(--accent)"/>
<rect x="429.33" y="143.5" width="2.13" height="6.5" style="fill:var(--link)"/>
<rect x="432.00" y="143.9" width="2.13" height="6.1" style="fill:var(--link)"/>
<rect x="434.67" y="127.1" width="2.13" height="22.9" style="fill:var(--accent)"/>
<rect x="437.33" y="143.3" width="2.13" height="6.7" style="fill:var(--link)"/>
<rect x="440.00" y="144.9" width="2.13" height="5.1" style="fill:var(--link)"/>
<rect x="442.67" y="126.7" width="2.13" height="23.3" style="fill:var(--accent)"/>
<rect x="445.33" y="143.6" width="2.13" height="6.4" style="fill:var(--link)"/>
<rect x="448.00" y="143.7" width="2.13" height="6.3" style="fill:var(--link)"/>
<rect x="450.67" y="122.9" width="2.13" height="27.1" style="fill:var(--accent)"/>
<rect x="453.33" y="141.5" width="2.13" height="8.5" style="fill:var(--link)"/>
<rect x="456.00" y="143.1" width="2.13" height="6.9" style="fill:var(--link)"/>
<rect x="458.67" y="125.1" width="2.13" height="24.9" style="fill:var(--accent)"/>
<rect x="461.33" y="141.4" width="2.13" height="8.6" style="fill:var(--link)"/>
<rect x="464.00" y="143.2" width="2.13" height="6.8" style="fill:var(--link)"/>
<rect x="466.67" y="125.3" width="2.13" height="24.7" style="fill:var(--accent)"/>
<rect x="469.33" y="141.7" width="2.13" height="8.3" style="fill:var(--link)"/>
<rect x="472.00" y="143.2" width="2.13" height="6.8" style="fill:var(--link)"/>
<rect x="474.67" y="123.3" width="2.13" height="26.7" style="fill:var(--accent)"/>
<rect x="477.33" y="34.8" width="2.13" height="115.2" style="fill:var(--danger)"/>
<rect x="480.00" y="143.2" width="2.13" height="6.8" style="fill:var(--link)"/>
<rect x="482.67" y="143.8" width="2.13" height="6.2" style="fill:var(--link)"/>
<rect x="485.33" y="128.5" width="2.13" height="21.5" style="fill:var(--accent)"/>
<rect x="488.00" y="142.4" width="2.13" height="7.6" style="fill:var(--link)"/>
<rect x="490.67" y="142.9" width="2.13" height="7.1" style="fill:var(--link)"/>
<rect x="493.33" y="129.3" width="2.13" height="20.7" style="fill:var(--accent)"/>
<rect x="496.00" y="141.7" width="2.13" height="8.3" style="fill:var(--link)"/>
<rect x="498.67" y="143.1" width="2.13" height="6.9" style="fill:var(--link)"/>
<rect x="501.33" y="127.0" width="2.13" height="23.0" style="fill:var(--accent)"/>
<rect x="504.00" y="141.9" width="2.13" height="8.1" style="fill:var(--link)"/>
<rect x="506.67" y="143.5" width="2.13" height="6.5" style="fill:var(--link)"/>
<rect x="509.33" y="128.3" width="2.13" height="21.7" style="fill:var(--accent)"/>
<rect x="512.00" y="141.3" width="2.13" height="8.7" style="fill:var(--link)"/>
<rect x="514.67" y="143.9" width="2.13" height="6.1" style="fill:var(--link)"/>
<rect x="517.33" y="120.8" width="2.13" height="29.2" style="fill:var(--accent)"/>
<rect x="520.00" y="141.8" width="2.13" height="8.2" style="fill:var(--link)"/>
<rect x="522.67" y="143.6" width="2.13" height="6.4" style="fill:var(--link)"/>
<rect x="525.33" y="126.2" width="2.13" height="23.8" style="fill:var(--accent)"/>
<rect x="528.00" y="142.2" width="2.13" height="7.8" style="fill:var(--link)"/>
<rect x="530.67" y="144.1" width="2.13" height="5.9" style="fill:var(--link)"/>
<rect x="533.33" y="126.9" width="2.13" height="23.1" style="fill:var(--accent)"/>
<rect x="536.00" y="142.9" width="2.13" height="7.1" style="fill:var(--link)"/>
<rect x="538.67" y="144.1" width="2.13" height="5.9" style="fill:var(--link)"/>
<rect x="541.33" y="128.7" width="2.13" height="21.3" style="fill:var(--accent)"/>
<rect x="544.00" y="142.6" width="2.13" height="7.4" style="fill:var(--link)"/>
<rect x="546.67" y="143.4" width="2.13" height="6.6" style="fill:var(--link)"/>
<rect x="549.33" y="129.5" width="2.13" height="20.5" style="fill:var(--accent)"/>
<rect x="552.00" y="140.7" width="2.13" height="9.3" style="fill:var(--link)"/>
<rect x="554.67" y="141.9" width="2.13" height="8.1" style="fill:var(--link)"/>
<rect x="557.33" y="127.7" width="2.13" height="22.3" style="fill:var(--accent)"/>
<rect x="560.00" y="141.1" width="2.13" height="8.9" style="fill:var(--link)"/>
<rect x="562.67" y="144.0" width="2.13" height="6.0" style="fill:var(--link)"/>
<rect x="565.33" y="125.7" width="2.13" height="24.3" style="fill:var(--accent)"/>
<rect x="568.00" y="141.5" width="2.13" height="8.5" style="fill:var(--link)"/>
<rect x="570.67" y="142.9" width="2.13" height="7.1" style="fill:var(--link)"/>
<rect x="573.33" y="125.7" width="2.13" height="24.3" style="fill:var(--accent)"/>
<rect x="576.00" y="140.8" width="2.13" height="9.2" style="fill:var(--link)"/>
<rect x="578.67" y="144.2" width="2.13" height="5.8" style="fill:var(--link)"/>
<rect x="581.33" y="121.6" width="2.13" height="28.4" style="fill:var(--accent)"/>
<rect x="584.00" y="142.9" width="2.13" height="7.1" style="fill:var(--link)"/>
<rect x="586.67" y="144.1" width="2.13" height="5.9" style="fill:var(--link)"/>
<rect x="589.33" y="128.6" width="2.13" height="21.4" style="fill:var(--accent)"/>
<rect x="592.00" y="142.1" width="2.13" height="7.9" style="fill:var(--link)"/>
<rect x="594.67" y="143.6" width="2.13" height="6.4" style="fill:var(--link)"/>
<rect x="597.33" y="127.7" width="2.13" height="22.3" style="fill:var(--accent)"/>
<rect x="600.00" y="143.6" width="2.13" height="6.4" style="fill:var(--link)"/>
<rect x="602.67" y="138.0" width="2.13" height="12.0" style="fill:var(--accent)"/>
<rect x="605.33" y="67.0" width="2.13" height="83.0" style="fill:var(--danger)"/>
<rect x="608.00" y="142.7" width="2.13" height="7.3" style="fill:var(--link)"/>
<rect x="610.67" y="143.6" width="2.13" height="6.4" style="fill:var(--link)"/>
<rect x="613.33" y="129.7" width="2.13" height="20.3" style="fill:var(--accent)"/>
<rect x="616.00" y="141.0" width="2.13" height="9.0" style="fill:var(--link)"/>
<rect x="618.67" y="142.3" width="2.13" height="7.7" style="fill:var(--link)"/>
<rect x="621.33" y="128.0" width="2.13" height="22.0" style="fill:var(--accent)"/>
<rect x="624.00" y="141.7" width="2.13" height="8.3" style="fill:var(--link)"/>
<rect x="626.67" y="144.1" width="2.13" height="5.9" style="fill:var(--link)"/>
<rect x="629.33" y="132.4" width="2.13" height="17.6" style="fill:var(--accent)"/>
<rect x="632.00" y="145.1" width="2.13" height="4.9" style="fill:var(--link)"/>
<rect x="634.67" y="139.4" width="2.13" height="10.6" style="fill:var(--accent)"/>
<rect x="637.33" y="126.6" width="2.13" height="23.4" style="fill:var(--accent)"/>
    </g>
    <rect x="40" y="198" width="12" height="12" style="fill:var(--danger)"/><text x="58" y="208" style="fill:var(--muted)">I 帧</text>
    <rect x="120" y="198" width="12" height="12" style="fill:var(--accent)"/><text x="138" y="208" style="fill:var(--muted)">P 帧</text>
    <rect x="200" y="198" width="12" height="12" style="fill:var(--link)"/><text x="218" y="208" style="fill:var(--muted)">B 帧</text>
    <text x="300" y="208" style="fill:var(--muted)">横轴：显示顺序；纵轴：压缩后的字节数</text>
  </g>
</svg>
</figure>

8 个 I 帧出现在第 0、31、79、83、131、133、179、227 帧。其中 31、83、133 正好是上一节 `temporal` 找到的镜头切换：x264 发现新画面和前面的帧毫无关系，就提前插入了一个 I 帧；另外几个则是因为距离上一个 I 帧满了 48 帧（`-g 48`）。第 239 帧也是镜头切换，但它是视频的最后一帧，x264 把它编成了一个 28 KB 的 P 帧（大部分块实际用的是帧内预测），是普通 P 帧的两倍。

GOP 的长度是一个重要的取舍：

- **GOP 越长**，I 帧越少，文件越小；
- **GOP 越短**，拖动进度条（seek）越快，因为解码必须从 I 帧开始；网络丢包后也恢复得越快，因为错误会一直传到下一个 I 帧。

所以本地视频文件的 GOP 常设为几秒甚至十几秒，直播一般设为 1~2 秒。测试视频是用 `-g 48` 编码的，也就是最长 2 秒一个 I 帧。

B 帧还带来一个麻烦：它参考了后面的帧，所以后面的帧必须**先解码**。于是**解码顺序和显示顺序不一样**，每一帧需要两个时间戳：解码时间 DTS 和显示时间 PTS。第 2 章分析 H.264 码流时会详细讲。

## 6. 编码器做的取舍

到这里可以把视频编码器的工作概括成一条流水线：

<figure class="diagram">
<svg viewBox="0 0 680 120" role="img" aria-label="视频编码流程：原始 YUV 经过预测（帧内或帧间）得到残差，残差经过变换、量化、熵编码，得到压缩后的码流">
  <defs>
    <marker id="enc-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="30" width="90" height="44" rx="6" style="fill:var(--panel-2)"/>
      <rect x="130" y="30" width="100" height="44" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="265" y="30" width="90" height="44" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="390" y="30" width="90" height="44" rx="6" style="fill:var(--mark)"/>
      <rect x="515" y="30" width="90" height="44" rx="6" style="fill:var(--accent-soft)"/>
    </g>
    <text x="22" y="56" style="fill:var(--fg)">原始 YUV</text>
    <text x="150" y="50" style="fill:var(--fg)">预测</text><text x="140" y="66" style="fill:var(--muted);font-size:10px">帧内 / 帧间</text>
    <text x="290" y="50" style="fill:var(--fg)">变换</text><text x="292" y="66" style="fill:var(--muted);font-size:10px">DCT</text>
    <text x="415" y="50" style="fill:var(--fg)">量化</text><text x="408" y="66" style="fill:var(--muted);font-size:10px">唯一有损</text>
    <text x="530" y="50" style="fill:var(--fg)">熵编码</text><text x="530" y="66" style="fill:var(--muted);font-size:10px">CAVLC/CABAC</text>
    <g style="stroke:var(--muted)" marker-end="url(#enc-arrow)"><line x1="95" y1="52" x2="128" y2="52"/><line x1="230" y1="52" x2="263" y2="52"/><line x1="355" y1="52" x2="388" y2="52"/><line x1="480" y1="52" x2="513" y2="52"/><line x1="605" y1="52" x2="640" y2="52"/></g>
    <text x="612" y="44" style="fill:var(--muted);font-size:10px">码流</text>
    <text x="130" y="100" style="fill:var(--muted)">空间 / 时间冗余</text>
    <text x="390" y="100" style="fill:var(--muted)">视觉冗余</text>
    <text x="515" y="100" style="fill:var(--muted)">统计冗余</text>
  </g>
</svg>
</figure>

每一步都有很多选择：预测时搜索多大范围的运动矢量、用几个参考帧、块分得多细……搜索得越仔细，压缩得越好，但也越慢。x264 用 **preset** 打包了这些选项，从 `ultrafast` 到 `veryslow`。固定 CRF 23，比较几个 preset：

```bash
for p in ultrafast veryfast medium slow veryslow; do
  /usr/bin/time -p ffmpeg -v error -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 24 -i bbb.yuv \
    -c:v libx264 -preset $p -crf 23 preset-$p.mp4
done
```

| preset | 文件大小 | 编码耗时 | 速度 | PSNR |
|---|---|---|---|---|
| ultrafast | 5.56 MB | 0.11 秒 | 2200 帧/秒 | 42.1 dB |
| veryfast | 2.07 MB | 0.28 秒 | 860 帧/秒 | 43.3 dB |
| medium（默认） | 2.37 MB | 0.65 秒 | 370 帧/秒 | 45.7 dB |
| slow | 2.30 MB | 1.06 秒 | 230 帧/秒 | 45.6 dB |
| veryslow | 2.15 MB | 3.22 秒 | 75 帧/秒 | 45.8 dB |

（Apple M6，12 个核心，x264 默认使用全部核心。）

- `ultrafast` 关掉了 B 帧、CABAC 熵编码和大部分搜索，速度是 `medium` 的 6 倍，但文件大了 1.3 倍，画质还更差；
- 从 `medium` 到 `veryslow`，时间多了 5 倍，文件只小了 9%，画质几乎一样，越往后收益越小；
- `veryfast` 的文件反而比 `medium` 小，但 PSNR 低了 2.4 dB：同样的 CRF 在不同 preset 下画质并不完全相同，比较时要同时看大小和画质。

这就是编码的基本取舍：**码率、画质、速度，三者只能顾两头**。录好的视频可以慢慢编码，选 `slow` 节省带宽；直播必须实时编码，只能选 `veryfast` 甚至 `ultrafast`，用更高的码率换速度。

最后看一眼音频：PCM 1536 kbps，测试视频中的 AAC 只有 128 kbps，压缩了 12 倍。音频编码同样利用了人的感知特点：例如一个响亮的声音会「掩盖」频率相近的轻声，被掩盖的部分可以不存。

## 7. 小结

- 原始视频的数据量巨大：1080p30 每秒 93 MB（746 Mbps），不压缩就无法存储和传输；实际的视频码率只有原始数据的 1% 左右。
- **空间冗余**：相邻像素相似，用帧内预测 + 变换去除。
- **时间冗余**：相邻帧相似，用帧间预测（运动估计 + 运动补偿）去除，这是压缩比最大的来源。
- **视觉冗余**：人眼看不出的细节用量化丢掉，这是有损的一步；CRF 越大，丢得越多。
- I 帧独立解码、最大；P 帧参考前面；B 帧参考前后、最小。GOP 越长文件越小，但 seek 和抗丢包能力越差。
- 码率、画质、编码速度三者需要取舍：preset 越慢压缩越好，CRF 越大文件越小。

下一篇看看压缩好的音视频是怎么装进一个文件的：封装格式、流、时间基，以及「扩展名不等于格式」。

## 练习

1. 修改 `spatial.c`，改用「上方像素」或「左边和上方的平均值」来预测，熵分别是多少？哪种预测最好？
2. 用 `spatial.c` 分析 U 分量（640×360，从 `frame.yuv` 的第 921600 字节开始），熵比 Y 分量大还是小？为什么？
3. 用 `-g 240` 和 `-g 12` 分别编码测试视频（CRF 相同），比较文件大小和 I 帧数量。
4. 用 `-bf 0` 编码（不用 B 帧），文件比默认大多少？
5. 1080p30 的视频按 6 Mbps 编码，平均每个像素用了多少位？和原始的 12 位/像素相比压缩了多少倍？
