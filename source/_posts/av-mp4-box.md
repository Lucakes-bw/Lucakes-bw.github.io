---
title: MP4 封装格式
date: 2026-10-31 12:00:00
updated: 2026-10-31 12:00:00
categories:
  - 音视频开发
  - 码流与封装格式
tags:
  - 音视频
  - MP4
  - C
order: 4
description: MP4 由一层套一层的 box 组成，每个 box 先是 4 字节长度、再是 4 字节类型。用 C 递归打印测试视频的 box 树：音视频数据全在 mdat 中，7926 字节的 moov 里存着每一帧的位置、大小和时间，分散在 stts、ctts、stss、stsc、stsz、stco 六张表中。再写一个程序，用这六张表算出每一帧在文件中的偏移和 PTS、DTS，视频 240 帧、音频 470 帧与 ffprobe 逐个对得上。编辑列表 elst 跳过了 AAC 的 1024 个预热采样，音频最后一帧只有 768 个采样。把文件截掉一半：moov 在末尾时完全打不开，moov 在开头时还能解出 158 帧，分段 MP4 也能解出 156 帧。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 MP4 的 box 结构，会用 C 递归解析 box 树；知道 moov、trak、mdia、stbl 等主要 box 的作用；读懂样本表（stts、ctts、stss、stsc、stsz、stco），会根据它们定位任意一帧；理解编辑列表；理解 moov 位置对播放和容错的影响，知道分段 MP4 是什么。
>
> **前置知识**：本章前三篇；第 1 章《看懂一个视频文件》中的时间基和 PTS、DTS。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0。

## 1. box

**MP4** 的正式名称是 MPEG-4 Part 14，它基于 **ISO 基础媒体文件格式**（ISO BMFF，ISO/IEC 14496-12），而后者又源自苹果的 QuickTime（MOV）格式，所以 FFmpeg 用同一个模块处理 MP4 和 MOV。手机拍的视频、视频网站的点播、各种 `.m4a` 音乐文件，几乎都是这种格式。

MP4 文件由 **box**（QuickTime 中叫 atom）组成。每个 box 的结构都一样：

<figure class="diagram">
<svg viewBox="0 0 680 120" role="img" aria-label="box 的结构：4 字节大小（包括头本身），4 字节类型，然后是内容；内容可以是数据，也可以是更多的 box">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(10,15)">
      <rect x="0" y="0" width="120" height="34" style="fill:var(--link);opacity:.6"/><text x="12" y="21" style="fill:var(--bg)">size（4 字节）</text>
      <rect x="120" y="0" width="120" height="34" style="fill:var(--warn);opacity:.75"/><text x="132" y="21" style="fill:var(--bg)">type（4 字节）</text>
      <rect x="240" y="0" width="420" height="34" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="252" y="21" style="fill:var(--fg)">内容：数据，或者若干个子 box</text>
      <path d="M0,40 L0,48 L660,48 L660,40" fill="none" style="stroke:var(--fg)"/>
      <text x="250" y="66" style="fill:var(--fg)">size = 整个 box 的字节数（包括头）</text>
      <text x="0" y="92" style="fill:var(--muted);font-size:11px">size = 1：真正的大小在后面的 8 字节中（超过 4 GB 的 box）；size = 0：一直到文件末尾</text>
    </g>
  </g>
</svg>
</figure>

和 FLV 的 tag 很像，区别在于 box 可以**嵌套**：有些 box 的内容就是更多的 box，于是整个文件形成一棵树。第 1 章第 4 篇练习 3 中，我们已经用这个结构跳过了顶层的 box，找到了 `moov` 的位置。

## 2. box 树

### 2.1 程序

下面的程序递归地解析 box：遇到容器类的 box（`moov`、`trak` 等）就进入它的内容继续解析，遇到其他 box 就打印它的一些内容：

```c
// mp4_boxes.c：递归打印 MP4 的 box 树，并显示几个重要 box 的内容
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned char *buf;
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

// 这些 box 的内容就是子 box，需要继续往下解析
static int is_container(const char *t)
{
    static const char *list[] = {"moov", "trak", "mdia", "minf", "stbl", "dinf", "edts", "mvex", "moof", "traf", NULL};
    for (int i = 0; list[i]; i++) if (memcmp(t, list[i], 4) == 0) return 1;
    return 0;
}

// 显示某些 box 的内容。p 指向 box 的内容（跳过了 8 字节的 size 和 type），n 是内容的长度
static void show(const char *t, const unsigned char *p, uint64_t n)
{
    // 大部分 box 的内容以 1 字节版本号和 3 字节标志开头（叫 full box）
    int ver = p[0];
    if (!memcmp(t, "ftyp", 4)) {
        printf("  主品牌 %.4s，兼容：", p);
        for (uint64_t i = 8; i < n; i += 4) printf("%.4s ", p + i);
    } else if (!memcmp(t, "mvhd", 4) || !memcmp(t, "mdhd", 4)) {
        uint32_t scale = ver ? be32(p + 20) : be32(p + 12);
        uint64_t dur = ver ? be64(p + 24) : be32(p + 16);
        printf("  timescale=%u，duration=%llu（%.3f 秒）", scale, (unsigned long long)dur, (double)dur / scale);
    } else if (!memcmp(t, "tkhd", 4)) {
        const unsigned char *q = p + (ver ? 88 : 76);      // 宽高是 16.16 定点数，在 box 的最后
        printf("  track_id=%u，%ux%u", ver ? be32(p + 20) : be32(p + 12), be32(q) >> 16, be32(q + 4) >> 16);
    } else if (!memcmp(t, "hdlr", 4)) {
        printf("  类型 %.4s", p + 8);
    } else if (!memcmp(t, "stsd", 4)) {
        printf("  %u 个描述，第一个是 %.4s", be32(p + 4), p + 12);
    } else if (!memcmp(t, "elst", 4)) {
        printf("  %u 项：", be32(p + 4));
        for (uint32_t i = 0; i < be32(p + 4); i++) {
            const unsigned char *e = p + 8 + i * (ver ? 20 : 12);
            printf("[时长 %u，从媒体时间 %d 开始] ", ver ? (uint32_t)be64(e) : be32(e), (int32_t)(ver ? be64(e + 8) : be32(e + 4)));
        }
    } else if (!memcmp(t, "stts", 4) || !memcmp(t, "ctts", 4) || !memcmp(t, "stsc", 4)) {
        uint32_t cnt = be32(p + 4);
        int cols = memcmp(t, "stsc", 4) ? 2 : 3;
        printf("  %u 项：", cnt);
        for (uint32_t i = 0; i < cnt && i < 4; i++) {
            printf("(");
            for (int c = 0; c < cols; c++) printf(c ? ",%d" : "%d", (int32_t)be32(p + 8 + (i * cols + c) * 4));
            printf(") ");
        }
        if (cnt > 4) printf("…");
    } else if (!memcmp(t, "stss", 4)) {
        printf("  %u 个关键帧：", be32(p + 4));
        for (uint32_t i = 0; i < be32(p + 4); i++) printf("%u ", be32(p + 8 + i * 4));
    } else if (!memcmp(t, "stsz", 4)) {
        printf("  %u 个样本，", be32(p + 8));
        if (be32(p + 4)) printf("大小都是 %u", be32(p + 4));    // 所有样本一样大时只存一个值
        else for (uint32_t i = 0; i < 4 && i < be32(p + 8); i++) printf(i ? "%u " : "前几个大小：%u ", be32(p + 12 + i * 4));
    } else if (!memcmp(t, "stco", 4)) {
        printf("  %u 个块，前几个偏移：", be32(p + 4));
        for (int i = 0; i < 4 && i < (int)be32(p + 4); i++) printf("%u ", be32(p + 8 + i * 4));
    }
}

static void parse(uint64_t start, uint64_t end, int depth)
{
    uint64_t pos = start;
    while (pos + 8 <= end) {
        uint64_t size = be32(buf + pos);
        char type[5] = {0};
        memcpy(type, buf + pos + 4, 4);
        int header = 8;
        if (size == 1) { size = be64(buf + pos + 8); header = 16; }   // 64 位的大小，用于超过 4 GB 的 box
        else if (size == 0) size = end - pos;                         // 一直到文件末尾
        printf("%*s%-4s %8llu 字节  @%llu", depth * 2, "", type, (unsigned long long)size, (unsigned long long)pos);
        if (is_container(type)) {
            printf("\n");
            parse(pos + header, pos + size, depth + 1);
        } else {
            show(type, buf + pos + header, size - header);
            printf("\n");
        }
        pos += size;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2) { fprintf(stderr, "用法: %s 输入.mp4\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);
    parse(0, size, 0);
    free(buf);
    return 0;
}
```

很多 box 是 **full box**：内容的前 4 个字节是 1 字节的版本号和 3 字节的标志。版本 0 时时间、时长等字段是 32 位，版本 1 时是 64 位，程序中的 `ver ? … : …` 就是在处理这个区别。

```bash
clang -O2 -o mp4_boxes mp4_boxes.c
./mp4_boxes bbb-10s.mp4
```

```
ftyp       32 字节  @0  主品牌 isom，兼容：isom iso2 avc1 mp41 
moov     7926 字节  @32
  mvhd      108 字节  @40  timescale=48000，duration=480000（10.000 秒）
  trak     4024 字节  @148
    tkhd       92 字节  @156  track_id=1，1280x720
    edts       36 字节  @248
      elst       28 字节  @256  1 项：[时长 480000，从媒体时间 1024 开始] 
    mdia     3888 字节  @284
      mdhd       32 字节  @292  timescale=12288，duration=122880（10.000 秒）
      hdlr       45 字节  @324  类型 vide
      minf     3803 字节  @369
        vmhd       20 字节  @377
        dinf       36 字节  @397
          dref       28 字节  @405
        stbl     3739 字节  @433
          stsd      199 字节  @441  1 个描述，第一个是 avc1
          stts       24 字节  @640  1 项：(240,512) 
          stss       44 字节  @664  7 个关键帧：1 32 80 84 132 180 228 
          ctts     1472 字节  @708  182 项：(1,1024) (1,2048) (2,512) (1,2048) …
          stsc       40 字节  @2180  2 项：(1,2,1) (2,1,1) 
          stsz      980 字节  @2220  240 个样本，前几个大小：96518 2276 693 497 
          stco      972 字节  @3200  239 个块，前几个偏移：7974 107062 108345 109398 
  trak     3689 字节  @4172
    tkhd       92 字节  @4180  track_id=2，0x0
    edts       36 字节  @4272
      elst       28 字节  @4280  1 项：[时长 480000，从媒体时间 1024 开始] 
    mdia     3553 字节  @4308
      mdhd       32 字节  @4316  timescale=48000，duration=481024（10.021 秒）
      hdlr       45 字节  @4348  类型 soun
      minf     3468 字节  @4393
        smhd       16 字节  @4401
        dinf       36 字节  @4417
          dref       28 字节  @4425
        stbl     3408 字节  @4453
          stsd      126 字节  @4461  1 个描述，第一个是 mp4a
          stts       32 字节  @4587  2 项：(469,1024) (1,768) 
          stsc      316 字节  @4619  25 项：(1,1,1) (2,2,1) (23,1,1) (24,2,1) …
          stsz     1900 字节  @4935  470 个样本，前几个大小：294 312 278 274 
          stco      972 字节  @6835  239 个块，前几个偏移：106768 107755 108842 111631 
          sgpd       26 字节  @7807
          sbgp       28 字节  @7833
  udta       97 字节  @7861
free        8 字节  @7958
mdat  2986745 字节  @7966
```

### 2.2 树的结构

把主要的 box 画成一棵树：

<figure class="diagram">
<svg viewBox="0 0 680 420" role="img" aria-label="MP4 的 box 树：顶层是 ftyp、moov、free、mdat；moov 中有 mvhd 和两个 trak；trak 中有 tkhd、edts、mdia；mdia 中有 mdhd、hdlr、minf；minf 中的 stbl 包含 stsd、stts、ctts、stss、stsc、stsz、stco 等样本表；音视频数据都在 mdat 中">
  <g style="font-family:var(--mono);font-size:12px">
    <rect x="10" y="10" width="660" height="26" rx="4" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="20" y="28" style="fill:var(--fg)">ftyp</text><text x="120" y="28" style="fill:var(--muted);font-size:11px">文件类型：isom，兼容哪些规范（32 字节）</text>
    <rect x="10" y="42" width="660" height="316" rx="4" style="fill:var(--accent);opacity:.08;stroke:var(--accent)"/>
    <text x="20" y="60" style="fill:var(--fg)">moov</text><text x="120" y="60" style="fill:var(--muted);font-size:11px">所有的元数据（7926 字节）</text>
    <rect x="30" y="68" width="630" height="22" rx="3" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="40" y="83" style="fill:var(--fg)">mvhd</text><text x="140" y="83" style="fill:var(--muted);font-size:11px">整个影片：timescale、时长</text>
    <rect x="30" y="96" width="630" height="226" rx="3" style="fill:var(--link);opacity:.08;stroke:var(--link)"/>
    <text x="40" y="113" style="fill:var(--fg)">trak</text><text x="140" y="113" style="fill:var(--muted);font-size:11px">一个轨道（视频）；另一个 trak 是音频，结构相同</text>
    <rect x="50" y="120" width="290" height="22" rx="3" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="60" y="135" style="fill:var(--fg)">tkhd</text><text x="110" y="135" style="fill:var(--muted);font-size:11px">轨道 ID、宽高</text>
    <rect x="350" y="120" width="300" height="22" rx="3" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="360" y="135" style="fill:var(--fg)">edts/elst</text><text x="440" y="135" style="fill:var(--muted);font-size:11px">编辑列表</text>
    <rect x="50" y="148" width="600" height="166" rx="3" style="fill:var(--warn);opacity:.08;stroke:var(--warn)"/>
    <text x="60" y="165" style="fill:var(--fg)">mdia</text>
    <rect x="110" y="152" width="250" height="20" rx="3" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="118" y="166" style="fill:var(--fg);font-size:11px">mdhd：轨道的 timescale、时长</text>
    <rect x="370" y="152" width="270" height="20" rx="3" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="378" y="166" style="fill:var(--fg);font-size:11px">hdlr：vide 或 soun</text>
    <text x="70" y="192" style="fill:var(--fg)">minf / stbl（样本表）</text>
    <g style="font-size:11px">
      <rect x="70" y="200" width="185" height="22" rx="3" style="fill:var(--panel);stroke:var(--border-2)"/><text x="78" y="215" style="fill:var(--fg)">stsd 编码参数（avcC）</text>
      <rect x="262" y="200" width="185" height="22" rx="3" style="fill:var(--panel);stroke:var(--border-2)"/><text x="270" y="215" style="fill:var(--fg)">stts 每帧时长 → DTS</text>
      <rect x="454" y="200" width="185" height="22" rx="3" style="fill:var(--panel);stroke:var(--border-2)"/><text x="462" y="215" style="fill:var(--fg)">ctts PTS − DTS</text>
      <rect x="70" y="228" width="185" height="22" rx="3" style="fill:var(--panel);stroke:var(--border-2)"/><text x="78" y="243" style="fill:var(--fg)">stss 哪些是关键帧</text>
      <rect x="262" y="228" width="185" height="22" rx="3" style="fill:var(--panel);stroke:var(--border-2)"/><text x="270" y="243" style="fill:var(--fg)">stsc 每块几个样本</text>
      <rect x="454" y="228" width="185" height="22" rx="3" style="fill:var(--panel);stroke:var(--border-2)"/><text x="462" y="243" style="fill:var(--fg)">stsz 每个样本的大小</text>
      <rect x="70" y="256" width="185" height="22" rx="3" style="fill:var(--panel);stroke:var(--border-2)"/><text x="78" y="271" style="fill:var(--fg)">stco 每块的文件偏移</text>
      <text x="270" y="271" style="fill:var(--muted)">合起来：每一帧在哪、多大、什么时候</text>
    </g>
    <text x="70" y="302" style="fill:var(--muted);font-size:11px">样本（sample）= 一帧视频或一个 AAC 帧；块（chunk）= 连续存放的若干个样本</text>
    <rect x="30" y="328" width="630" height="22" rx="3" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="40" y="343" style="fill:var(--fg)">udta</text><text x="140" y="343" style="fill:var(--muted);font-size:11px">用户数据：encoder=Lavf63.1.102 等</text>
    <rect x="10" y="364" width="660" height="22" rx="4" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="20" y="379" style="fill:var(--fg)">free</text><text x="120" y="379" style="fill:var(--muted);font-size:11px">空白，可以忽略（8 字节）</text>
    <rect x="10" y="392" width="660" height="22" rx="4" style="fill:var(--danger);opacity:.15;stroke:var(--danger)"/><text x="20" y="407" style="fill:var(--fg)">mdat</text><text x="120" y="407" style="fill:var(--muted);font-size:11px">媒体数据：所有的音视频帧，一个挨一个（2986745 字节）</text>
  </g>
</svg>
</figure>

MP4 的设计思想是**数据和索引分开**：`mdat` 中只是一帧接一帧的原始数据，没有任何分隔或标记；关于这些数据的一切信息都在 `moov` 中。这和 FLV 正好相反：FLV 中每一帧都带着自己的头（类型、大小、时间戳），而 MP4 把所有帧的这些信息集中成表，放在一起。

几个值得一看的 box：

- **mvhd 和 mdhd 的 timescale 不同**。整个影片用 48000，视频轨道用 12288，音频轨道用 48000。每个轨道可以选最适合自己的时间基：视频 24 帧/秒，12288 / 24 = 512，每帧正好是整数；音频 48000 Hz，时间戳就是第几个采样。这就是第 1 章中 `ffprobe` 显示的 `time_base=1/12288`。
- **stsd** 是样本描述，`avc1` 里面包含了上一篇看到的 `avcC`（SPS、PPS），`mp4a` 里面包含 AudioSpecificConfig（`11 90 56 E5 00`）。解码器的初始化参数都在这里，所以 MP4 中的码流不需要重复 SPS、PPS。
- **stss** 列出了 7 个关键帧：1、32、80、84、132、180、228（序号从 1 开始），即第 0、31、79、83、131、179、227 帧。第 1 篇中那个不是 IDR 的第 133 帧，确实不在其中。
- **样本表占了 moov 的大部分**：视频的 ctts 1472 字节、stsz 980 字节、stco 972 字节，音频的 stsz 1900 字节、stco 972 字节。一部两小时的电影有十几万帧，moov 会有几 MB 大。

## 3. 定位每一帧

样本表是 MP4 中最精巧的部分，也最难懂。我们的目标是：给定一个轨道的第 i 个样本，求出它在文件中的**偏移**、**大小**、**DTS**、**PTS**，以及**是不是关键帧**。

### 3.1 六张表

| 表 | 每一项 | 测试视频的视频轨道 | 求什么 |
|---|---|---|---|
| stts | （样本数，每个样本的时长） | 1 项：(240, 512) | 每个样本的 DTS |
| ctts | （样本数，PTS − DTS） | 182 项：(1,1024) (1,2048) (2,512)… | PTS |
| stss | 关键帧的样本序号 | 7 项 | 是不是关键帧 |
| stsz | 每个样本的大小 | 240 项 | 大小 |
| stsc | （第一个块的序号，每块样本数，描述序号） | 2 项：(1,2,1) (2,1,1) | 样本在哪个块 |
| stco | 每个块的文件偏移 | 239 项 | 块在哪里 |

为了让表尽量小，MP4 用了几种压缩手段：

- **游程编码**：stts、ctts 中，连续相同的值合并成一项「（个数，值）」。视频每帧时长都是 512，整个 stts 只有 1 项；音频前 469 帧都是 1024，最后一帧是 768，stts 有 2 项。
- **块**（chunk）：连续存放的几个样本组成一块，stco 只记录每块的起始偏移，块内的样本紧挨着，用 stsz 中的大小依次累加就能算出偏移。
- **stsc 也是游程编码**：每一项表示「从第几个块开始，每块有几个样本」，一直适用到下一项。视频的 (1,2,1) (2,1,1) 表示：第 1 块有 2 个样本，从第 2 块开始每块 1 个样本。

<figure class="diagram">
<svg viewBox="0 0 680 240" role="img" aria-label="用样本表定位帧：stsc 说明第 1 块有 2 个样本，之后每块 1 个；stco 给出每块的偏移；块内样本的偏移等于块的偏移加上前面样本的大小；视频块和音频块在 mdat 中交替排列">
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="18" style="fill:var(--fg)">mdat 中的布局（视频块和音频块交错存放）</text>
    <g transform="translate(10,28)">
      <rect x="0" y="0" width="230" height="34" style="fill:var(--danger);opacity:.55"/><text x="8" y="15" style="fill:var(--bg);font-size:11px">样本 0（I）96518</text><text x="8" y="29" style="fill:var(--bg);font-size:10px">@7974</text>
      <rect x="230" y="0" width="40" height="34" style="fill:var(--accent);opacity:.7"/><text x="234" y="15" style="fill:var(--bg);font-size:10px">样本1</text><text x="234" y="29" style="fill:var(--bg);font-size:9px">2276</text>
      <rect x="270" y="0" width="60" height="34" style="fill:var(--link);opacity:.6"/><text x="276" y="15" style="fill:var(--bg);font-size:10px">音频块 1</text><text x="276" y="29" style="fill:var(--bg);font-size:9px">@106768</text>
      <rect x="330" y="0" width="30" height="34" style="fill:var(--accent);opacity:.5"/><text x="332" y="21" style="fill:var(--bg);font-size:9px">2</text>
      <rect x="360" y="0" width="60" height="34" style="fill:var(--link);opacity:.6"/><text x="366" y="15" style="fill:var(--bg);font-size:10px">音频块 2</text><text x="366" y="29" style="fill:var(--bg);font-size:9px">2 个样本</text>
      <rect x="420" y="0" width="30" height="34" style="fill:var(--accent);opacity:.5"/><text x="422" y="21" style="fill:var(--bg);font-size:9px">3</text>
      <rect x="450" y="0" width="60" height="34" style="fill:var(--link);opacity:.6"/><text x="456" y="21" style="fill:var(--bg);font-size:10px">音频块 3</text>
      <rect x="510" y="0" width="150" height="34" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="530" y="21" style="fill:var(--muted)">…</text>
      <path d="M0,38 L0,46 L270,46 L270,38" fill="none" style="stroke:var(--fg)"/><text x="60" y="62" style="fill:var(--fg)">视频块 1（2 个样本）</text>
    </g>
    <g transform="translate(10,120)" style="font-size:11px">
      <text x="0" y="0" style="fill:var(--fg);font-size:12px">求视频样本 1 的偏移：</text>
      <text x="0" y="22" style="fill:var(--muted)">① stsc：第 1 块有 2 个样本 → 样本 0、1 在第 1 块，样本 1 是块中的第 2 个</text>
      <text x="0" y="42" style="fill:var(--muted)">② stco：第 1 块的偏移 = 7974</text>
      <text x="0" y="62" style="fill:var(--muted)">③ stsz：样本 0 的大小 = 96518 → 样本 1 的偏移 = 7974 + 96518 = 104492</text>
      <text x="0" y="82" style="fill:var(--muted)">④ stts：DTS = 1 × 512 = 512；ctts：第 2 项 2048 → PTS = 512 + 2048 = 2560</text>
      <text x="0" y="102" style="fill:var(--muted)">⑤ stss 中没有 2（序号从 1 开始）→ 不是关键帧</text>
    </g>
  </g>
</svg>
</figure>

### 3.2 程序

把上面的过程写成程序：按块遍历，从 stsc 得到每块的样本数，从 stco 得到块的偏移，块内用 stsz 累加；同时用 stts、ctts 累加时间，用 stss 判断关键帧：

```c
// mp4_samples.c：根据 stbl 中的表，算出某个轨道每个样本（帧）的位置、大小和时间
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned char *buf;
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// 在 [start, end) 范围内找类型为 type 的第 index 个 box，返回内容的起点，*len 是内容长度
static unsigned char *find(unsigned char *start, unsigned char *end, const char *type, int index, uint32_t *len)
{
    for (unsigned char *p = start; p + 8 <= end; p += be32(p)) {
        if (memcmp(p + 4, type, 4) == 0 && index-- == 0) {
            *len = be32(p) - 8;
            return p + 8;
        }
        if (be32(p) < 8) break;
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "用法: %s 输入.mp4 轨道序号(0/1) [显示几个]\n", argv[0]); return 1; }
    int track = atoi(argv[2]), show = argc > 3 ? atoi(argv[3]) : 10;
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    // moov → trak → mdia → minf → stbl，一层层往下找
    uint32_t n;
    unsigned char *moov = find(buf, buf + size, "moov", 0, &n);
    unsigned char *trak = find(moov, moov + n, "trak", track, &n);
    unsigned char *mdia = find(trak, trak + n, "mdia", 0, &n);
    unsigned char *mdhd = find(mdia, mdia + n, "mdhd", 0, &(uint32_t){0});
    unsigned char *minf = find(mdia, mdia + n, "minf", 0, &n);
    unsigned char *stbl = find(minf, minf + n, "stbl", 0, &n);
    uint32_t len;
    unsigned char *stts = find(stbl, stbl + n, "stts", 0, &len);
    unsigned char *ctts = find(stbl, stbl + n, "ctts", 0, &len);   // 没有 B 帧时可能没有
    unsigned char *stss = find(stbl, stbl + n, "stss", 0, &len);   // 音频没有：每个样本都是关键帧
    unsigned char *stsc = find(stbl, stbl + n, "stsc", 0, &len);
    unsigned char *stsz = find(stbl, stbl + n, "stsz", 0, &len);
    unsigned char *stco = find(stbl, stbl + n, "stco", 0, &len);
    uint32_t timescale = be32(mdhd + 12);

    uint32_t count = be32(stsz + 8), chunks = be32(stco + 4);
    // 按块（chunk）遍历：stsc 说明每个块里有几个样本，stco 给出每个块的偏移
    uint32_t sample = 0, stsc_n = be32(stsc + 4), stsc_i = 0;
    uint32_t stts_i = 0, stts_left = be32(stts + 8), ctts_i = 0, ctts_left = ctts ? be32(ctts + 8) : 0;
    uint32_t stss_i = 0;
    uint64_t dts = 0;
    printf("timescale = %u，%u 个样本，%u 个块\n", timescale, count, chunks);
    printf(" 样本  块      偏移    大小     DTS     PTS  关键帧\n");
    for (uint32_t c = 1; c <= chunks; c++) {
        // stsc 的每一项是（第一个块的序号，每块样本数，描述序号），一直适用到下一项的第一个块之前
        if (stsc_i + 1 < stsc_n && c >= be32(stsc + 8 + (stsc_i + 1) * 12)) stsc_i++;
        uint32_t per_chunk = be32(stsc + 8 + stsc_i * 12 + 4);
        uint64_t offset = be32(stco + 8 + (c - 1) * 4);
        for (uint32_t k = 0; k < per_chunk; k++, sample++) {
            uint32_t sz = be32(stsz + 4) ? be32(stsz + 4) : be32(stsz + 12 + sample * 4);   // 大小都相同时只存一个
            int32_t cts = ctts ? (int32_t)be32(ctts + 8 + ctts_i * 8 + 4) : 0;
            int key = !stss || (stss_i < be32(stss + 4) && be32(stss + 8 + stss_i * 4) == sample + 1);
            if (key && stss) stss_i++;
            if (sample < (uint32_t)show)
                printf("%5u %4u %9llu %7u %7llu %7lld  %s\n", sample, c, (unsigned long long)offset, sz,
                       (unsigned long long)dts, (long long)dts + cts, key ? "是" : "");
            offset += sz;                              // 同一块中的样本紧挨着
            // stts：（样本数，每个样本的时长），游程编码
            dts += be32(stts + 8 + stts_i * 8 + 4);
            if (--stts_left == 0 && ++stts_i < be32(stts + 4)) stts_left = be32(stts + 8 + stts_i * 8);
            if (ctts && --ctts_left == 0 && ++ctts_i < be32(ctts + 4)) ctts_left = be32(ctts + 8 + ctts_i * 8);
        }
    }
    printf("...\n最后一个样本之后 DTS = %llu（%.3f 秒）\n", (unsigned long long)dts, (double)dts / timescale);
    free(buf);
    return 0;
}
```

```bash
clang -O2 -o mp4_samples mp4_samples.c
./mp4_samples bbb-10s.mp4 0 8
```

```
timescale = 12288，240 个样本，239 个块
 样本  块      偏移    大小     DTS     PTS  关键帧
    0    1      7974   96518       0    1024  是
    1    1    104492    2276     512    2560  
    2    2    107062     693    1024    1536  
    3    3    108345     497    1536    2048  
    4    4    109398    2233    2048    4096  
    5    5    112240     650    2560    3072  
    6    6    113515     495    3072    3584  
    7    7    114626    1747    3584    5632  
...
最后一个样本之后 DTS = 122880（10.000 秒）
```

```bash
./mp4_samples bbb-10s.mp4 1 4
```

```
timescale = 48000，470 个样本，239 个块
 样本  块      偏移    大小     DTS     PTS  关键帧
    0    1    106768     294       0       0  是
    1    2    107755     312    1024    1024  是
    2    2    108067     278    2048    2048  是
    3    3    108842     274    3072    3072  是
...
最后一个样本之后 DTS = 481024（10.021 秒）
```

和 `ffprobe` 的输出对照（`pts,dts,size,pos`）：

```bash
ffprobe -v error -select_streams v -show_entries packet=pts,dts,size,pos -of csv=p=0 bbb-10s.mp4 | head -4
```

```
0,-1024,96518,7974
2560,-512,2276,104492
1536,0,693,107062
2048,512,497,108345
```

偏移和大小完全一致，PTS、DTS 都比我们算的小 1024，这是下一节要讲的编辑列表。把所有样本都按「减去 1024」后与 `ffprobe` 逐行比较，视频 240 个、音频 470 个样本全部一致。

> 写这个程序时我犯了一个错误：stts 的每一项是「（样本数，时长）」，我一开始把计数器初始化成了第一项的**时长**而不是**样本数**。视频的 stts 只有一项，看不出问题，但 PTS 全错了（ctts 有 182 项），音频的总时长也不对。和 `ffprobe` 逐个比较才发现。解析二进制格式时，找一个可靠的工具对照，是最有效的检查办法。

### 3.3 从结果中能看出什么

- **视频块和音频块交错存放**。视频第 1 块从 7974 开始，有 2 帧（I 帧和第一个 P 帧）；音频第 1 块在 106768，紧跟在视频第 1 块之后；视频第 2 块在 107062……。和第 1 章看到的「包交错排列」是同一件事，只是 MP4 里交错的单位是块。
- **DTS 从 0 开始，每帧加 512**；PTS = DTS + ctts，第一帧的 PTS 是 1024。音频没有 ctts（AAC 没有 B 帧之类的重排），PTS 等于 DTS；也没有 stss，因为每个 AAC 帧都可以独立解码。
- **视频总时长 122880 / 12288 = 10.000 秒；音频总时长 481024 / 48000 = 10.021 秒**：469 × 1024 + 768。最后一帧只有 768 个采样，和 AAC 那篇算出来的「结尾补了 256 个采样」吻合（1024 − 256 = 768）。

## 4. 编辑列表

`ffprobe` 显示的时间戳比样本表中的小 1024，是因为两个轨道都有一个**编辑列表**（edts/elst）：

```
elst  1 项：[时长 480000，从媒体时间 1024 开始]
```

编辑列表把轨道的「媒体时间」映射到影片的「播放时间」：这一项的意思是，播放时间的 0 对应媒体时间 1024，一共播放 480000（以 mvhd 的 timescale 48000 为单位，即 10 秒）。

- **视频轨道**中，媒体时间 1024（12288 为单位，83 毫秒）是第一帧的 PTS。有 B 帧时，第一帧的 PTS 必须比 DTS 晚（要先解码后面的 P 帧），样本表中的 DTS 从 0 开始，PTS 就从 1024 开始。编辑列表把这 83 毫秒去掉，让第一帧在播放时间 0 显示。FFmpeg 的做法是把所有时间戳减去 1024，于是 DTS 变成了负数，这就是第 1 章中看到的 `dts=-1024`。
- **音频轨道**中，媒体时间 1024（48000 为单位）是 AAC 的 1024 个**预热采样**。编辑列表说「从第 1024 个采样开始播放，播放 10 秒」，前面的 1024 个和最后多出来的 256 个采样都不播放。上一篇第 6.1 节中，MP4 解码出 480000 个采样、ADTS 裸流解码出 481280 个，差别就在这里。

编辑列表还可以做更多的事，例如只播放中间的某一段、把两段拼起来，但大多数播放器只支持最简单的这种用法。

## 5. moov 的位置

### 5.1 faststart

默认情况下，FFmpeg 写 MP4 时先写 `mdat`，等所有帧都写完、知道了每一帧的位置，再把 `moov` 写在文件**末尾**。`-movflags +faststart` 则在写完之后把 `moov` 挪到 `mdat` 前面（同时把 stco 中的所有偏移加上 moov 的大小）。测试视频就是这样编码的，所以 `moov` 在偏移 32。

第 1 章说过，`moov` 在末尾的文件，网页播放时要下载完才能开始播放。实际上浏览器会先读开头，发现没有 `moov`，再用 HTTP Range 请求去读文件末尾，这需要服务器支持 Range，而且多一次往返；在不支持随机访问的场合（比如管道），就只能读完整个文件。

### 5.2 文件不完整会怎样

更严重的问题是**容错**。录像时程序崩溃、断电、存储空间满了，文件没写完，`moov` 也就没写出来。模拟一下，把文件截到 1.5 MB（大约一半）：

```bash
ffmpeg -i bbb-10s.mp4 -c copy nofs.mp4                         # moov 在末尾
ffmpeg -i bbb-10s.mp4 -c copy -movflags +faststart fs.mp4       # moov 在开头
head -c 1500000 nofs.mp4 > cut_nofs.mp4
head -c 1500000 fs.mp4 > cut_fs.mp4
ffmpeg -i cut_nofs.mp4 -f null -
ffmpeg -i cut_fs.mp4 -f null -
```

```
== cut_nofs.mp4
[in#0] moov atom not found
Error opening input files: Invalid data found when processing input
== cut_fs.mp4
[h264] Invalid NAL unit size (25691 > 15973).
frame=  158 fps=0.0 q=-0.0 Lsize=N/A time=00:00:06.58
```

- **moov 在末尾**：文件直接打不开。1.5 MB 的音视频数据完好地躺在 `mdat` 中，但没有索引，播放器不知道哪里是一帧、哪一帧是什么时间。
- **moov 在开头**：前 158 帧（6.58 秒）都能正常解码，读到截断处才报错。

实际的录像软件不会等到最后才写 `moov`，常见的做法是使用下面的分段 MP4。

### 5.3 分段 MP4

**分段 MP4**（fragmented MP4，fMP4）把一个大的 `moov` 拆开：开头的 `moov` 中只有编码参数，没有样本表；之后是一个个「`moof` + `mdat`」，每个 `moof`（movie fragment）带着这一段的样本表：

```bash
ffmpeg -i bbb-10s.mp4 -c copy -movflags frag_keyframe+empty_moov frag.mp4
./mp4_boxes frag.mp4
```

```
ftyp       36 字节  @0  主品牌 isom，兼容：isom iso6 iso2 avc1 mp41
moov     1256 字节  @36
  mvhd      108 字节  @44  timescale=1536000，duration=0（0.000 秒）
  trak      524 字节  @152
  trak      447 字节  @676
  mvex       72 字节  @1123
  udta       97 字节  @1195
moof      908 字节  @1292
  mfhd       16 字节  @1300
  traf      336 字节  @1316
  traf      548 字节  @1652
mdat   143511 字节  @2200
moof      956 字节  @145711
……
```

（只显示了前两层。）`moov` 中的样本表全是空的，时长是 0；`frag_keyframe` 让每个关键帧开始一个新的分段，一共 7 个 `moof`，对应 7 个关键帧。同样截掉一半，分段 MP4 能解出 156 帧：已经写完的分段都是完整的。

分段 MP4 的另一个重要用途是**流媒体**：每个分段可以单独传输，HLS（新版）和 DASH 都用它作为切片格式，第 5 章会用到。

## 6. 小结

- MP4 由 box 组成：4 字节大小 + 4 字节类型 + 内容，容器 box 的内容是子 box，形成一棵树。
- 数据和索引分开：`mdat` 中只有一帧接一帧的数据，`moov` 中有所有的元数据。
- 每个轨道（`trak`）有自己的 timescale；`stsd` 中有解码参数（avcC、AudioSpecificConfig）。
- 样本表：stts → DTS，ctts → PTS，stss → 关键帧，stsz → 大小，stsc + stco → 偏移。为了压缩，大量使用游程编码和「块」。
- 编辑列表把媒体时间映射到播放时间：去掉 B 帧带来的起始延迟，跳过 AAC 的预热采样。
- `moov` 在末尾时文件不完整就无法播放；faststart 把它挪到开头；分段 MP4 把索引分散到每个分段，既容错又适合流媒体。

下一篇解析为广播和传输设计的 MPEG-TS。

## 练习

1. 修改 `mp4_samples.c`，输入一个时间（秒），找到这个时间之前最近的关键帧，打印它的样本序号、偏移和 PTS。这就是播放器拖动进度条时做的事情。试试 5 秒和 7 秒。
2. 用 `mp4_samples` 统计视频轨道中，样本的 PTS − DTS 有哪几种值，各出现多少次？它们分别对应什么类型的帧？
3. 修改 `mp4_boxes.c`，解析 `udta` 中的内容（它包含一个 `meta` box，里面有 `ilst`，存放着 `©too` 等标签）。提示：`meta` 是 full box，内容的前 4 个字节是版本和标志。
4. 第 5.3 节的分段 MP4 中，样本信息在 `moof` 里。用 `mp4_boxes` 查看 `frag.mp4` 中第一个 `moof` 的完整结构，`traf` 中有哪些 box？它们分别取代了 `stbl` 中的哪些表？

<details>
<summary>点击查看答案</summary>

1. 先把 PTS 换算成秒（减去编辑列表的 1024：`(pts - 1024) / 12288.0`），遍历时记住「最后一个 PTS 不超过目标时间的关键帧」，在输出的地方加一个判断即可：

   ```c
   double t = (double)(dts + cts - 1024) / timescale;
   if (key && t <= target) { best = sample; best_offset = offset; best_t = t; }
   ```

   7 个关键帧的时间分别是 0、1.292、3.292、3.458、5.458、7.458、9.458 秒。所以 5 秒对应样本 83（3.458 秒，偏移 575998）；7 秒对应样本 131（5.458 秒，偏移 992737）。也就是说，拖到第 7 秒时，播放器要从 5.458 秒处的关键帧开始解码，先解码 37 帧再丢弃，才能显示第 7 秒（第 168 帧）的画面。拖动进度条时画面会顿一下，就是这个原因；GOP 越长，最坏情况下要多解码的帧越多。有些播放器为了快，会直接显示关键帧的画面，所以拖动后的位置和点击的位置不完全一样。

2. 统计视频轨道 240 个样本的 `PTS − DTS`，再按帧类型分组：

   ```
    512：142 个，全部是 B 帧
   1024：11 个，其中 I 帧 8 个、P 帧 3 个
   1536：32 个，全部是 P 帧
   2048：55 个，全部是 P 帧
   ```

   可以这样理解：整体有 2 帧（1024）的起始延迟；在此基础上，一帧在码流中每提前一个位置，PTS − DTS 就多 512，每推后一个位置就少 512。B 帧排在它参考的 P 帧后面，比显示顺序晚一个位置，所以是 1024 − 512 = 512。后面跟着两个 B 帧的 P 帧，要提前两个位置，是 1024 + 1024 = 2048，这是最常见的 I B B P 结构；后面只跟一个 B 帧的 P 帧提前一个位置，是 1536；I 帧和后面没有 B 帧的 P 帧不需要提前，是 1024。

3. 用 `xxd -s 7861 -l 97 bbb-10s.mp4` 看 `udta` 的内容：

   ```
   0000 0061 7564 7461  0000 0059 6d65 7461  0000 0000 ...   udta → meta（full box）
   0000 0021 6864 6c72 ... 6d64 6972 6170 706c ...          hdlr：mdir appl
   0000 002c 696c 7374                                     ilst
   0000 0024 a974 6f6f                                     ©too
   0000 001c 6461 7461 0000 0001 0000 0000 4c61 7666 ...   data：类型 1（UTF-8），然后是 Lavf63.1.102
   ```

   结构是 `meta`（full box，内容先跳过 4 字节版本和标志）→ `hdlr` + `ilst` → `©too` → `data`。`data` 的内容是 4 字节类型（1 表示 UTF-8 文本）、4 字节语言，然后是文本。`©` 是一个字节 `0xA9`，这是 QuickTime 留下来的规矩：苹果定义的标签都以 `©` 开头（`©nam` 标题、`©ART` 艺术家等，音乐文件的标签就存在这里）。`ffprobe` 显示的 `encoder: Lavf63.1.102` 就来自 `©too`。程序中要把 `udta`、`meta`、`ilst`、`©too` 都当作容器，`meta` 进入时多跳 4 个字节。

4. `moof` 和 `traf` 已经在程序的容器列表中了，直接运行就能看到：

   ```
   moof      908 字节  @1292
     mfhd       16 字节  @1300
     traf      336 字节  @1316
       tfhd       36 字节  @1324
       tfdt       20 字节  @1360
       trun      272 字节  @1380
     traf      548 字节  @1652
       tfhd       36 字节  @1660
       tfdt       20 字节  @1696
       trun      484 字节  @1716
   mdat   143511 字节  @2200
   ```

   每个 `moof` 中有 `mfhd`（分段序号）和每个轨道一个 `traf`。`traf` 中：`tfhd` 是轨道 ID 和默认的样本时长、大小、标志；`tfdt` 是这个分段第一个样本的解码时间；`trun` 逐个列出这个分段中每个样本的大小、时长、标志（是不是关键帧）和 PTS − DTS 偏移，相当于把 stsz、stts、stss、ctts 合在一起。偏移不需要 stco：分段的数据就在紧跟着的 `mdat` 中，`trun` 记录数据相对 `moof` 起点的偏移即可。

</details>
