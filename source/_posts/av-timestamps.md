---
title: 时间戳：PTS、DTS 与时间基
date: 2026-10-31 14:00:00
updated: 2026-10-31 14:00:00
categories:
  - 音视频开发
  - 码流与封装格式
tags:
  - 音视频
  - 时间戳
  - C
order: 6
description: 同一帧画面，在 MP4 中的时间戳是 1536，在 FLV 中是 208，在 TS 中是 144750，在 MKV 中是 125，因为四种容器的时间基分别是 1/12288、1/1000、1/90000、1/1000。把本章出现过的时间戳放在一起：DTS 决定解码顺序，PTS 决定显示顺序，B 帧让两者分开。用 C 写一个用 128 位整数计算的时间戳换算函数，看看误差从哪里来：毫秒时间基表示不了 1/24 秒；把每帧时长取整后累加，1 小时后 AAC 音频会慢 56 秒。最后把测试视频 MP4 → FLV → MP4 来回转一次，视频的 stts 从 1 项变成 161 项，音频帧的时长出现了 1016、1008，AAC 预热采样的信息也丢了。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解时间基和时间戳的关系；理解 PTS、DTS 为什么不同，以及解码器为什么有延迟；知道常见容器的时间基；会正确地在时间基之间换算时间戳，避免累积误差和溢出；理解转封装时时间戳精度的损失。
>
> **前置知识**：本章前五篇。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0。

本章每一篇都遇到了时间戳：H.264 的 POC、FLV 的毫秒时间戳和 cts、MP4 的 stts 和 ctts、TS 的 90 kHz PTS 和 PCR。这一篇把它们放在一起，看清楚它们之间的关系。

## 1. 时间基

第 1 章第 4 篇说过：时间戳是一个整数，乘以**时间基**才是秒。同一帧画面在不同的容器中，时间戳完全不同：

| 容器 | 视频时间基 | 音频时间基 | 显示顺序第 3 帧的 PTS | 换算成秒 |
|---|---|---|---|---|
| MP4 | 1/12288 | 1/48000 | 1536 | 0.125 |
| FLV | 1/1000 | 1/1000 | 208 | 0.208 |
| MPEG-TS | 1/90000 | 1/90000 | 144750 | 1.608 |
| MKV | 1/1000 | 1/1000 | 125 | 0.125 |

（FLV 整体后移了 83 毫秒，TS 中第一帧的 PTS 是 133500，即 1.483 秒，减去之后都是 0.125 秒。）

每种容器选择时间基的考虑不同：

- **MP4** 可以为每个轨道自由选择，FFmpeg 会挑一个让每帧时长为整数的值。24 帧/秒选 12288（每帧 512），音频直接用采样率。
- **FLV、MKV** 固定用毫秒，简单直观，代价是表示不了 1/24 秒这样的时长，第 3 节会看到这带来的问题。
- **MPEG-TS** 固定用 90 kHz。90000 能被 24、25、30、50、60 整除，常见帧率的每帧时长都是整数（24 帧/秒是 3750，25 帧/秒是 3600）；29.97 帧/秒（30000/1001）的一帧是 3003，也是整数。这个数字是精心挑选的。

## 2. PTS 与 DTS

### 2.1 为什么需要两个时间戳

- **DTS**（Decoding Time Stamp）：这一帧什么时候**解码**。
- **PTS**（Presentation Time Stamp）：这一帧什么时候**显示**。

没有 B 帧时，帧按顺序解码、按顺序显示，两者相同。有 B 帧时，B 帧要参考后面的 P 帧，P 帧必须先解码，码流中的顺序（解码顺序）和显示顺序就不同了。把测试视频前 7 帧在 MP4 中的时间戳画出来（时间基 1/12288，每帧 512）：

<figure class="diagram">
<svg viewBox="0 0 680 270" role="img" aria-label="解码顺序与显示顺序：码流中依次是 I、P、B、B、P、B、B，DTS 每帧递增 512；按 PTS 排序后显示顺序是 I、B、B、P、B、B、P；P 帧在码流中被提前，B 帧排在它参考的 P 帧之后">
  <defs>
    <marker id="ts-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="20" style="fill:var(--fg)">码流中（解码顺序）</text>
    <g transform="translate(10,30)">
      <rect x="0" y="0" width="80" height="44" style="fill:var(--danger);opacity:.7"/><text x="34" y="19" style="fill:var(--bg)">I</text><text x="6" y="36" style="fill:var(--bg);font-size:10px">D-1024 P0</text>
      <rect x="92" y="0" width="80" height="44" style="fill:var(--accent);opacity:.7"/><text x="126" y="19" style="fill:var(--bg)">P</text><text x="96" y="36" style="fill:var(--bg);font-size:10px">D-512 P1536</text>
      <rect x="184" y="0" width="80" height="44" style="fill:var(--link);opacity:.6"/><text x="218" y="19" style="fill:var(--bg)">B</text><text x="194" y="36" style="fill:var(--bg);font-size:10px">D0 P512</text>
      <rect x="276" y="0" width="80" height="44" style="fill:var(--link);opacity:.6"/><text x="310" y="19" style="fill:var(--bg)">B</text><text x="280" y="36" style="fill:var(--bg);font-size:10px">D512 P1024</text>
      <rect x="368" y="0" width="80" height="44" style="fill:var(--accent);opacity:.7"/><text x="402" y="19" style="fill:var(--bg)">P</text><text x="372" y="36" style="fill:var(--bg);font-size:10px">D1024 P3072</text>
      <rect x="460" y="0" width="80" height="44" style="fill:var(--link);opacity:.6"/><text x="494" y="19" style="fill:var(--bg)">B</text><text x="464" y="36" style="fill:var(--bg);font-size:10px">D1536 P2048</text>
      <rect x="552" y="0" width="80" height="44" style="fill:var(--link);opacity:.6"/><text x="586" y="19" style="fill:var(--bg)">B</text><text x="556" y="36" style="fill:var(--bg);font-size:10px">D2048 P2560</text>
    </g>
    <g fill="none" style="stroke:var(--muted)" marker-end="url(#ts-arrow)">
      <path d="M50,76 L50,176"/><path d="M142,76 L326,176"/><path d="M234,76 L142,176"/><path d="M326,76 L234,176"/>
      <path d="M418,76 L602,176"/><path d="M510,76 L418,176"/><path d="M602,76 L510,176"/>
    </g>
    <text x="10" y="132" style="fill:var(--muted);font-size:11px">按 PTS 重新排序</text>
    <g transform="translate(10,180)">
      <rect x="0" y="0" width="80" height="34" style="fill:var(--danger);opacity:.7"/><text x="34" y="22" style="fill:var(--bg)">I</text>
      <rect x="92" y="0" width="80" height="34" style="fill:var(--link);opacity:.6"/><text x="126" y="22" style="fill:var(--bg)">B</text>
      <rect x="184" y="0" width="80" height="34" style="fill:var(--link);opacity:.6"/><text x="218" y="22" style="fill:var(--bg)">B</text>
      <rect x="276" y="0" width="80" height="34" style="fill:var(--accent);opacity:.7"/><text x="310" y="22" style="fill:var(--bg)">P</text>
      <rect x="368" y="0" width="80" height="34" style="fill:var(--link);opacity:.6"/><text x="402" y="22" style="fill:var(--bg)">B</text>
      <rect x="460" y="0" width="80" height="34" style="fill:var(--link);opacity:.6"/><text x="494" y="22" style="fill:var(--bg)">B</text>
      <rect x="552" y="0" width="80" height="34" style="fill:var(--accent);opacity:.7"/><text x="586" y="22" style="fill:var(--bg)">P</text>
      <g style="fill:var(--muted);font-size:10px"><text x="30" y="50">0</text><text x="118" y="50">512</text><text x="208" y="50">1024</text><text x="300" y="50">1536</text><text x="392" y="50">2048</text><text x="484" y="50">2560</text><text x="576" y="50">3072</text></g>
    </g>
    <text x="10" y="262" style="fill:var(--fg)">显示顺序（PTS）</text>
  </g>
</svg>
</figure>

几个规律：

- **DTS 总是递增的**，每帧加一个固定值（512），因为解码就是按码流顺序一帧接一帧进行的。
- **PTS 不一定递增**，但 PTS ≥ DTS 永远成立：一帧必须先解码，才能显示。
- 为了保证 PTS ≥ DTS，整个序列的 DTS 要比 PTS 提前一段时间，这里是 2 帧（1024），也就是 B 帧的最大个数。MP4 中 DTS 从 0 开始、PTS 从 1024 开始，再用编辑列表去掉这段延迟（第 4 篇第 4 节）；FFmpeg 读出来时让 PTS 从 0 开始，DTS 就从 −1024 开始了。

### 2.2 不同容器怎样存这两个时间戳

| 容器 | 存什么 |
|---|---|
| MP4 | stts 存每帧的时长（累加得到 DTS），ctts 存 PTS − DTS |
| FLV | tag 头存 DTS，视频 tag 中的 cts 存 PTS − DTS |
| TS | PES 头中直接存 PTS 和 DTS（两者相同时只存 PTS） |
| MKV | **只存 PTS** |

MKV 只存 PTS，DTS 要由解封装器推算：

```bash
ffprobe -v error -select_streams v -show_entries packet=pts,dts -of csv=p=0 bbb.mkv | head -6
```

```
0,N/A
125,N/A
42,0
83,42
250,83
167,125
```

前两个包的 DTS 是 `N/A`，因为 FFmpeg 要看到后面几个包，才能把 PTS 排序，推算出 DTS。这一点在大多数情况下没有影响，但如果从 MKV 转封装到需要 DTS 的格式（如 FLV、TS），就依赖这个推算是否正确。

### 2.3 解码器的延迟

解码顺序和显示顺序不同，还带来另一个后果：解码器不能解码一帧就输出一帧。解码了 I 帧，可以输出它；接着解码了 P 帧，它要在第 3 帧显示，现在还不能输出；然后解码 B 帧，才能输出 B 帧……解码器内部要有一个**重排序缓冲区**，暂存已经解码、但还没轮到显示的帧。第 1 章 `ffprobe` 显示的 `has_b_frames=2` 就是这个缓冲区的深度：解码器最多要攒 2 帧。

所以把码流送进解码器后，前几个包送进去没有输出，最后还要「冲刷」（flush）解码器，把缓冲区里剩下的帧取出来，否则会少掉最后几帧。第 3 章用 FFmpeg 的 API 解码时，会亲眼看到这一点。

## 3. 换算

### 3.1 换算公式与溢出

把时间基 a 下的时间戳 t 换算成时间基 b：

```
t' = t × a / b = t × (a.num × b.den) / (a.den × b.num)
```

要注意两点：

- **先乘后除**，否则整数除法会把小数部分丢掉；结果要四舍五入，而不是截断。
- **乘法可能溢出**。时间戳本身可能很大（TS 的 PTS 有 33 位，以微秒计的时间戳一天就有 864 亿），再乘以一个几万、几十万的数，64 位整数可能不够。FFmpeg 的 `av_rescale_q` 内部专门处理了这个问题；下面的程序用 128 位整数（GCC 和 Clang 支持 `__int128`）来计算。

### 3.2 程序

```c
// ts_rescale.c：在不同的时间基之间换算时间戳，看看误差从哪里来
#include <stdio.h>
#include <stdint.h>

// 时间基用分数 num/den 表示，例如 1/12288
typedef struct { int64_t num, den; } Rational;

// 把时间基 from 下的时间戳 ts 换算成时间基 to 下的值，四舍五入。
// ts × from.num × to.den 可能很大，用 128 位整数计算，避免溢出
static int64_t rescale(int64_t ts, Rational from, Rational to)
{
    __int128 a = (__int128)ts * from.num * to.den;
    __int128 b = (__int128)from.den * to.num;
    if (a >= 0) return (int64_t)((a + b / 2) / b);
    return -(int64_t)((-a + b / 2) / b);
}

int main(void)
{
    Rational mp4 = {1, 12288}, flv = {1, 1000}, ts = {1, 90000};

    printf("1. 24 帧/秒的前 8 帧：MP4（1/12288）→ FLV（1/1000）→ 再换回 MP4\n");
    printf(" 帧    MP4    FLV    TS     换回 MP4   误差\n");
    for (int i = 0; i < 8; i++) {
        int64_t t = i * 512;                         // 每帧 512 个单位
        int64_t f = rescale(t, mp4, flv), back = rescale(f, flv, mp4);
        printf("%3d  %5lld  %5lld  %6lld  %8lld  %+5lld\n", i, (long long)t, (long long)f,
               (long long)rescale(t, mp4, ts), (long long)back, (long long)(back - t));
    }

    printf("\n2. 每帧时长取整后累加，1 小时后差多少\n");
    Rational ntsc = {1001, 30000};                   // 29.97 帧/秒的一帧
    struct { const char *name; Rational frame; int64_t frames; } cases[] = {
        {"24 帧/秒", {1, 24}, 24 * 3600},
        {"29.97 帧/秒", {1001, 30000}, 107892},      // 1 小时约 107892 帧
        {"AAC 48 kHz", {1024, 48000}, 168750},       // 1 小时 168750 帧
    };
    for (int k = 0; k < 3; k++) {
        int64_t step = rescale(1, cases[k].frame, flv);           // 每帧取整成毫秒
        int64_t exact = rescale(cases[k].frames, cases[k].frame, flv);
        printf("%-12s 每帧 %lld 毫秒（精确值 %.3f），累加 %lld 帧 = %lld 毫秒，精确值 %lld 毫秒，差 %+lld 毫秒\n",
               cases[k].name, (long long)step, 1000.0 * cases[k].frame.num / cases[k].frame.den,
               (long long)cases[k].frames, (long long)(step * cases[k].frames), (long long)exact,
               (long long)(step * cases[k].frames - exact));
    }

    printf("\n3. 正确的做法：每一帧都从帧号直接换算，而不是累加取整后的时长\n");
    int64_t prev = 0, min = 1 << 30, max = 0;
    for (int64_t i = 1; i <= 107892; i++) {
        int64_t t = rescale(i, ntsc, flv);           // 第 i 帧的时间 = i × 1001/30000 秒
        int64_t d = t - prev;
        if (d < min) min = d;
        if (d > max) max = d;
        prev = t;
    }
    printf("29.97 帧/秒：相邻两帧的间隔在 %lld～%lld 毫秒之间，1 小时后的时间戳 %lld 毫秒，误差不超过 0.5 毫秒\n",
           (long long)min, (long long)max, (long long)prev);
    return 0;
}
```

```bash
clang -O2 -o ts_rescale ts_rescale.c
./ts_rescale
```

```
1. 24 帧/秒的前 8 帧：MP4（1/12288）→ FLV（1/1000）→ 再换回 MP4
 帧    MP4    FLV    TS     换回 MP4   误差
  0      0      0       0         0     +0
  1    512     42    3750       516     +4
  2   1024     83    7500      1020     -4
  3   1536    125   11250      1536     +0
  4   2048    167   15000      2052     +4
  5   2560    208   18750      2556     -4
  6   3072    250   22500      3072     +0
  7   3584    292   26250      3588     +4

2. 每帧时长取整后累加，1 小时后差多少
24 帧/秒   每帧 42 毫秒（精确值 41.667），累加 86400 帧 = 3628800 毫秒，精确值 3600000 毫秒，差 +28800 毫秒
29.97 帧/秒 每帧 33 毫秒（精确值 33.367），累加 107892 帧 = 3560436 毫秒，精确值 3599996 毫秒，差 -39560 毫秒
AAC 48 kHz   每帧 21 毫秒（精确值 21.333），累加 168750 帧 = 3543750 毫秒，精确值 3600000 毫秒，差 -56250 毫秒

3. 正确的做法：每一帧都从帧号直接换算，而不是累加取整后的时长
29.97 帧/秒：相邻两帧的间隔在 33～34 毫秒之间，1 小时后的时间戳 3599996 毫秒，误差不超过 0.5 毫秒
```

### 3.3 三种误差

**一、精度不够。** 24 帧/秒的一帧是 41.667 毫秒，毫秒时间基只能取 42 或 41。从 1/12288 换算到毫秒再换回来，误差在 ±4 之间（±0.33 毫秒）。单次误差很小，人感觉不到，但信息确实丢了：换回来的时间戳不再是 512 的整数倍，相邻两帧的间隔也不再相等。换算到 90 kHz 则完全没有误差（3750 的整数倍）。

**二、累积误差。** 如果每帧的时长先取整，再一帧帧累加，误差会越积越大：24 帧/秒每帧按 42 毫秒算，1 小时后时间戳比实际多了 28.8 秒；AAC 每帧按 21 毫秒算，1 小时后少了 56 秒。音频和视频往不同的方向偏，音画会严重不同步。这类错误在自己写的推流程序中非常常见：用一个变量 `ts += 1000 / fps`，短时间测试看不出问题，直播几个小时后才发现声音和画面对不上。

**三、正确的做法**是每一帧都从帧号（或采样数）直接换算：第 i 帧的时间 = i × 帧时长，只在最后换算成目标时间基时取整一次。这样每一帧的误差不超过半个单位，也不会累积。程序的第 3 部分就是这样做的：29.97 帧/秒的相邻两帧间隔在 33 和 34 毫秒之间跳动，但 1 小时后的时间戳和精确值的差不超过 0.5 毫秒。

上一篇 FLV 的练习 2 中，FFmpeg 写出的视频时间戳间隔是 41、42、42、41、42、42……，就是用这种方法算出来的。

## 4. 转封装会丢掉什么

`-c copy` 转封装不会改变音视频数据，但时间戳要在不同的时间基之间换算。把测试视频从 MP4 转成 FLV，再从 FLV 转回 MP4：

```bash
ffmpeg -i bbb-10s.mp4 -c copy bbb.flv
ffmpeg -i bbb.flv -c copy rt.mp4
./mp4_boxes rt.mp4 | grep -E "stts|ctts|mdhd|elst"
```

```
      elst       40 字节  @2987009  2 项：[时长 21，从媒体时间 -1 开始] [时长 9999，从媒体时间 1328 开始] 
      mdhd       32 字节  @2987057  timescale=16000，duration=159984（9.999 秒）
          stts     1304 字节  @2987405  161 项：(1,656) (2,672) (1,656) (2,672) …
          ctts     1736 字节  @2988753  215 项：(1,1328) (1,2672) (1,672) (1,656) …
      elst       28 字节  @2992589  1 项：[时长 10027，从媒体时间 0 开始] 
      mdhd       32 字节  @2992625  timescale=48000，duration=481256（10.026 秒）
          stts       48 字节  @2992896  4 项：(1,1024) (1,1016) (467,1024) (1,1008) 
```

和原来的 MP4 对比：

| | 原始 MP4 | MP4 → FLV → MP4 |
|---|---|---|
| 视频 timescale | 12288 | 16000 |
| 视频 stts | 1 项：(240, 512)，24 字节 | 161 项：(1,656) (2,672) …，1304 字节 |
| 视频 ctts | 182 项 | 215 项 |
| 音频 stts | 2 项：(469,1024) (1,768) | 4 项：(1,1024) (1,1016) (467,1024) (1,1008) |
| 音频编辑列表 | 从媒体时间 1024 开始 | 从媒体时间 0 开始 |

- **视频帧的时长不再相等**。FLV 中的时间戳只有毫秒精度，转回 MP4 时 FFmpeg 选了 16000 作为 timescale（毫秒的 16 倍），每帧的时长变成了 656（41 毫秒）和 672（42 毫秒）交替，stts 从 1 项变成了 161 项，大了 50 多倍。
- **音频帧的时长也乱了**。AAC 每帧本来是 1024 个采样（21.333 毫秒），经过毫秒取整再换回来，出现了 1016、1008 这样的值。实际上每帧的数据还是 1024 个采样，时间戳和数据对不上，播放器只能按时间戳去对齐，可能会在这里插入或者丢掉几个采样。
- **AAC 预热的信息丢了**。原来的编辑列表告诉播放器跳过开头 1024 个预热采样，FLV 中没有地方存这个信息（只能把时间戳整体后移），转回 MP4 后编辑列表变成「从 0 开始」，预热的那 21 毫秒会被播放出来，音频比原来早了 21 毫秒。

单看每一项，误差都只有零点几毫秒，播放时几乎察觉不到。但它说明了一个原则：**时间基精度高的格式转到精度低的格式，信息会丢失，转回来也找不回来**。在需要反复转换的系统中（比如直播的录制、转码、切片），尽量让中间环节使用精度高的时间基，例如 90 kHz 或者原始的采样率。

## 5. 小结

- 时间戳 × 时间基 = 秒。MP4 每个轨道自选时间基，FLV、MKV 固定毫秒，TS 固定 90 kHz。
- DTS 决定解码顺序，始终递增；PTS 决定显示顺序，PTS ≥ DTS。B 帧让两者分开，整个序列的 DTS 要提前，提前量等于最大的 B 帧个数。
- 各容器存法不同：MP4 用 stts + ctts，FLV 用 DTS + cts，TS 直接存两者，MKV 只存 PTS。
- 解码器有重排序缓冲区，前几帧没有输出，最后要冲刷。
- 换算时间戳要先乘后除、四舍五入、防止溢出；每帧时间要从帧号直接换算，不能累加取整后的时长。
- 转封装到低精度的时间基会丢失信息：帧时长不再均匀、编辑列表等信息可能丢失。

第 2 章到这里结束。我们已经能手写 H.264、AAC 码流和 FLV、MP4、TS 三种封装格式的解析器。第 3 章开始使用 FFmpeg 的库，这些格式的解析都会交给 libavformat 和 libavcodec，但它们做的事情，就是本章一个字节一个字节看过的这些。

## 练习

1. 修改 `ts_rescale.c`，把第 1 部分的「换算到毫秒」改成「截断」（直接整数除法，不四舍五入），往返误差的范围是多少？
2. 29.97 帧/秒的视频，在 MP4 中 FFmpeg 会选什么 timescale？在 TS 中每帧是多少？（可以用 `ffmpeg -f lavfi -i testsrc2=rate=30000/1001 -t 1` 生成一段测试。）
3. 第 3.1 节说乘法可能溢出。如果不用 `__int128`，用普通的 `int64_t` 计算 `ts * from.num * to.den / (from.den * to.num)`，把以微秒为单位（1/1000000）的时间戳换算到 1/90000，时间戳超过多少时会溢出？对应多长时间？
4. 用 `ffmpeg -i bbb-10s.mp4 -c copy bbb.ts` 和 `ffmpeg -i bbb.ts -c copy rt2.mp4` 做一次 MP4 → TS → MP4 的往返，用 `mp4_boxes` 查看视频的 stts，和 FLV 往返的结果比较，为什么不同？

<details>
<summary>点击查看答案</summary>

1. 把 `rescale` 中的 `(a + b / 2) / b` 改成 `a / b`（两个方向的换算都截断）：

   ```
    帧    MP4    FLV    TS     换回 MP4   误差
     0      0      0       0         0     +0
     1    512     41    3750       503     -9
     2   1024     83    7500      1019     -5
     3   1536    125   11250      1536     +0
   ```

   误差变成了 −9～0，全部偏向一边：每次截断都往小的方向取，换算两次就偏两次。四舍五入时误差是 −4～+4，正负相抵，平均为 0。偏向一边的误差在累加或多次转换时会越积越多，所以换算时间戳要用四舍五入，这也是 FFmpeg 的 `av_rescale_q` 的默认行为。

2. 生成一段 29.97 帧/秒的视频，用 `mp4_boxes` 查看 mdhd 和 stts：timescale 是 **30000**，每帧时长 **1001**，stts 只有 1 项，完全精确。FFmpeg 直接用帧率的分母和分子：一帧 = 1001/30000 秒。转成 TS 后，每帧是 90000 × 1001 / 30000 = **3003**，也是精确的整数。只有毫秒时间基的 FLV、MKV 表示不了，每帧会是 33、33、34 交替。

3. `from.num × to.den` = 1 × 90000，当 `ts × 90000` 超过 int64 的最大值 9.22 × 10¹⁸ 时溢出，也就是 ts > 1.02 × 10¹⁴ 微秒，约 **3.2 年**。看起来很长，但如果换算的目标是 1/1536000 这样的时间基（上一篇第 5.3 节的分段 MP4，mvhd 的 timescale 就是 1536000），上限就降到约 69 天；如果再乘以 TS 的 27 MHz 时钟，几天就溢出了。时间戳往往从某个很大的起点开始（例如 1970 年以来的微秒数，已经超过 1.7 × 10¹⁵），这时一开始就会溢出。用 128 位整数或者 FFmpeg 的 `av_rescale_q` 才可靠。

4. TS 往返后，视频 stts 只有 1 项，每帧时长相同，和原始 MP4 几乎一样（timescale 变成了 90000，每帧 3750）。原因是 90 kHz 能精确表示 1/24 秒，时间戳在 12288 和 90000 之间换算没有误差。而 FLV 的毫秒表示不了 1/24 秒，信息在第一步就丢了。这正是第 4 节的结论：中间环节用高精度的时间基，往返转换就不会损失。

</details>
