---
title: H.264 码流
date: 2026-10-31 09:00:00
updated: 2026-10-31 09:00:00
categories:
  - 音视频开发
  - 码流与封装格式
tags:
  - 音视频
  - H.264
  - C
order: 1
description: H.264 裸流是一串用起始码 00 00 01 隔开的 NAL 单元。用 C 把测试视频的码流切开：255 个 NAL 单元中，233 个普通片、7 个 IDR 片，SPS 和 PPS 在每个 IDR 前重复一次，开头还有一段 x264 写入的编码参数。再写一个按位读取的解析器，用指数哥伦布码解出 SPS 中的 80×45 个宏块，以及每个片的类型和 POC：I 帧 8 个，但其中一个不是 IDR，所以 MP4 中只标了 7 个关键帧。最后从码流中删掉一个 P 帧，前后 38 帧全部花屏，直到下一个 IDR 才恢复；删掉一个不被参考的 B 帧，其他帧一点都不受影响。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 H.264 码流的结构：NAL 单元、起始码、NAL 头；知道 SPS、PPS、SEI、IDR 片、普通片分别是什么；理解防竞争字节和指数哥伦布编码，会按位解析 SPS 和片头；分清 I 帧和 IDR 帧；理解参考帧丢失后错误如何传播；分清 Annex B 和 AVCC 两种格式。
>
> **前置知识**：第 1 章，尤其是《为什么必须压缩》中的 I/P/B 帧和 GOP；C 语言的位运算（C/C++ 专栏《运算符》）。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0。测试视频见第 1 章第 1 篇。

第 1 章的最后，`whatfmt` 认出了 `bbb.h264` 是「H.264 裸流（Annex B）」，开头是 `00 00 00 01`。这一章把各种格式一层层拆开，先从最里面的视频码流开始。准备好裸流（第 1 章第 4 篇已经做过）：

```bash
ffmpeg -i bbb-10s.mp4 -c:v copy -an -bsf:v h264_mp4toannexb bbb.h264
```

## 1. 码流的结构

### 1.1 从帧到 NAL 单元

H.264 编码器的输出是一串字节，这串字节按层次组织：

- **序列**（sequence）：从一个 IDR 帧开始的一组帧，共用一套参数（分辨率、档次等）；
- **图像**（picture）：一帧画面；
- **片**（slice）：一帧可以分成一个或多个片，每个片可以独立解码，一个片丢了不影响同一帧中的其他片；
- **宏块**（macroblock）：16×16 像素的块，是预测和变换的基本单位。1280×720 的画面是 80×45 = 3600 个宏块。

<figure class="diagram">
<svg viewBox="0 0 680 170" role="img" aria-label="H.264 的层次：一帧图像分成若干个片，每个片由若干个 16x16 的宏块组成；1280x720 的画面是 80 乘 45 个宏块">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(20,20)">
      <rect x="0" y="0" width="240" height="135" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <rect x="0" y="0" width="240" height="60" style="fill:var(--accent);opacity:.25"/>
      <rect x="0" y="60" width="240" height="75" style="fill:var(--link);opacity:.2"/>
      <g style="stroke:var(--border-2)">
        <line x1="0" y1="15" x2="240" y2="15"/><line x1="0" y1="30" x2="240" y2="30"/><line x1="0" y1="45" x2="240" y2="45"/><line x1="0" y1="60" x2="240" y2="60"/><line x1="0" y1="75" x2="240" y2="75"/><line x1="0" y1="90" x2="240" y2="90"/><line x1="0" y1="105" x2="240" y2="105"/><line x1="0" y1="120" x2="240" y2="120"/>
        <line x1="15" y1="0" x2="15" y2="135"/><line x1="30" y1="0" x2="30" y2="135"/><line x1="45" y1="0" x2="45" y2="135"/><line x1="60" y1="0" x2="60" y2="135"/><line x1="75" y1="0" x2="75" y2="135"/><line x1="90" y1="0" x2="90" y2="135"/><line x1="105" y1="0" x2="105" y2="135"/><line x1="120" y1="0" x2="120" y2="135"/><line x1="135" y1="0" x2="135" y2="135"/><line x1="150" y1="0" x2="150" y2="135"/><line x1="165" y1="0" x2="165" y2="135"/><line x1="180" y1="0" x2="180" y2="135"/><line x1="195" y1="0" x2="195" y2="135"/><line x1="210" y1="0" x2="210" y2="135"/><line x1="225" y1="0" x2="225" y2="135"/>
      </g>
      <rect x="0" y="0" width="15" height="15" style="fill:var(--warn);opacity:.85"/>
      <text x="80" y="35" style="fill:var(--fg)">片 0</text>
      <text x="80" y="102" style="fill:var(--fg)">片 1</text>
    </g>
    <g transform="translate(290,20)">
      <text x="0" y="12" style="fill:var(--fg)">一帧 = 若干个片（slice）</text>
      <text x="0" y="32" style="fill:var(--muted);font-size:11px">每个片可以独立解码；x264 默认一帧一个片</text>
      <rect x="0" y="48" width="12" height="12" style="fill:var(--warn);opacity:.85"/>
      <text x="20" y="58" style="fill:var(--fg)">一个宏块：16×16 像素</text>
      <text x="0" y="82" style="fill:var(--muted);font-size:11px">1280×720 = 80 × 45 = 3600 个宏块</text>
      <text x="0" y="102" style="fill:var(--muted);font-size:11px">（示意图没有按比例画）</text>
      <text x="0" y="128" style="fill:var(--fg)">每个片 → 一个 NAL 单元</text>
    </g>
  </g>
</svg>
</figure>

编码后的每个片，加上解码需要的参数，都被包装成 **NAL 单元**（Network Abstraction Layer Unit，网络抽象层单元）。NAL 单元是 H.264 码流的基本单位：存文件、走网络、装进 MP4 或 TS，都是以 NAL 单元为单位处理的。

### 1.2 起始码和 NAL 头

裸流中，NAL 单元一个接一个地排列，用**起始码**（start code）隔开：`00 00 01`（3 字节）或 `00 00 00 01`（4 字节）。这种格式定义在 H.264 标准的附录 B 中，所以叫 **Annex B** 格式。每个 NAL 单元的第 1 个字节是 **NAL 头**：

<figure class="diagram">
<svg viewBox="0 0 680 200" role="img" aria-label="Annex B 码流的结构：起始码 00 00 00 01 后面是 NAL 单元，每个 NAL 单元第一个字节是 NAL 头，包含 1 位 forbidden_zero_bit、2 位 nal_ref_idc、5 位 nal_unit_type">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(10,20)">
      <rect x="0" y="0" width="70" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="6" y="19" style="fill:var(--muted);font-size:11px">00000001</text>
      <rect x="70" y="0" width="60" height="30" style="fill:var(--warn);opacity:.7"/><text x="85" y="19" style="fill:var(--bg)">SPS</text>
      <rect x="130" y="0" width="70" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="136" y="19" style="fill:var(--muted);font-size:11px">00000001</text>
      <rect x="200" y="0" width="45" height="30" style="fill:var(--warn);opacity:.7"/><text x="208" y="19" style="fill:var(--bg)">PPS</text>
      <rect x="245" y="0" width="55" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="251" y="19" style="fill:var(--muted);font-size:11px">000001</text>
      <rect x="300" y="0" width="200" height="30" style="fill:var(--danger);opacity:.7"/><text x="345" y="19" style="fill:var(--bg)">IDR 片（95825 字节）</text>
      <rect x="500" y="0" width="70" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="506" y="19" style="fill:var(--muted);font-size:11px">00000001</text>
      <rect x="570" y="0" width="90" height="30" style="fill:var(--accent);opacity:.7"/><text x="584" y="19" style="fill:var(--bg)">P 片 …</text>
      <path d="M70,32 L70,60 L10,80" fill="none" style="stroke:var(--muted)"/><path d="M78,32 L180,80" fill="none" style="stroke:var(--muted)"/>
    </g>
    <g transform="translate(20,100)">
      <text x="0" y="-4" style="fill:var(--fg)">NAL 头（1 字节），SPS 的是 0x67 = 0110 0111：</text>
      <rect x="0" y="6" width="40" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="16" y="26" style="fill:var(--fg)">0</text>
      <rect x="40" y="6" width="80" height="30" style="fill:var(--link);opacity:.6"/><text x="74" y="26" style="fill:var(--bg)">11</text>
      <rect x="120" y="6" width="200" height="30" style="fill:var(--accent);opacity:.6"/><text x="200" y="26" style="fill:var(--bg)">00111</text>
      <text x="0" y="54" style="fill:var(--muted);font-size:11px">forbidden</text><text x="0" y="68" style="fill:var(--muted);font-size:11px">必须为 0</text>
      <text x="44" y="54" style="fill:var(--muted);font-size:11px">nal_ref_idc = 3</text><text x="44" y="68" style="fill:var(--muted);font-size:11px">重要程度</text>
      <text x="124" y="54" style="fill:var(--muted);font-size:11px">nal_unit_type = 7</text><text x="124" y="68" style="fill:var(--muted);font-size:11px">类型：SPS</text>
      <text x="350" y="20" style="fill:var(--fg)">type = header &amp; 0x1F</text>
      <text x="350" y="42" style="fill:var(--fg)">ref_idc = (header &gt;&gt; 5) &amp; 3</text>
    </g>
  </g>
</svg>
</figure>

- **forbidden_zero_bit**（1 位）：必须是 0。
- **nal_ref_idc**（2 位）：这个 NAL 单元有多重要。0 表示它不会被其他帧参考，丢了也不影响别的帧；非 0 表示会被参考。SPS、PPS、IDR 一般是 3。
- **nal_unit_type**（5 位）：类型。常见的有：

| 类型 | 名称 | 内容 |
|---|---|---|
| 1 | 非 IDR 图像的片 | P 帧、B 帧，以及不是 IDR 的 I 帧 |
| 5 | IDR 图像的片 | 一个序列的第一帧，第 3.3 节详细讲 |
| 6 | SEI | 补充增强信息：解码不需要的附加数据，例如编码器信息、HDR 元数据、字幕 |
| 7 | SPS | 序列参数集：档次、级别、分辨率、参考帧个数等 |
| 8 | PPS | 图像参数集：熵编码方式（CAVLC 还是 CABAC）、量化参数初值等 |
| 9 | AUD | 访问单元分隔符：标记一帧的开始，TS 中要求有（第 1 章第 4 篇见过） |

解码器必须先拿到 SPS 和 PPS 才能解码片，因为片中的很多字段要根据 SPS、PPS 中的参数才能读出来。

### 1.3 防竞争字节

用起始码分隔有一个问题：如果片的数据中恰好出现了 `00 00 01`，解析程序会误以为一个新的 NAL 单元开始了。H.264 的办法是：编码器在输出时检查数据，只要遇到连续两个 `00` 后面跟着 `00`、`01`、`02`、`03` 中的一个，就在中间插入一个 `03`：

```
原始数据：00 00 01   →   写入码流：00 00 03 01
原始数据：00 00 00   →   写入码流：00 00 03 00
```

插入的 `03` 叫**防竞争字节**（emulation prevention byte）。这样码流中就不可能出现 `00 00 00`、`00 00 01`、`00 00 02`，起始码是唯一的。解析 NAL 单元的内容之前，要先把这些 `03` 删掉，还原出原始数据（标准中叫 RBSP）。测试视频的 SPS 中就有两个：

```bash
xxd -s 689 -l 33 bbb.h264
```

```
000002b1: 0000 0001 6764 001f acd9 4050 05ba 6a02  ....gd....@P..j.
000002c1: 0203 6e00 0003 0002 0000 0300 601e 3063  ..n.........`.0c
000002d1: 2c                                       ,
```

`00 00 03 00 02` 和 `00 00 03 00 60` 中的 `03` 都是插入的（`02 02 03 6e` 中的 `03` 前面不是两个 `00`，是真实数据）。

## 2. 切分 NAL 单元

第一个程序在码流中查找起始码，两个起始码之间就是一个 NAL 单元，打印出每个单元的偏移、起始码长度、大小、`nal_ref_idc` 和类型，最后按类型统计：

```c
// h264_nalu.c：把 H.264 Annex B 裸流切分成 NAL 单元，打印每个单元的类型和大小
#include <stdio.h>
#include <stdlib.h>

static const char *nal_type_name(int t)
{
    switch (t) {
    case 1:  return "非 IDR 图像的片";
    case 5:  return "IDR 图像的片";
    case 6:  return "SEI 补充增强信息";
    case 7:  return "SPS 序列参数集";
    case 8:  return "PPS 图像参数集";
    case 9:  return "AUD 访问单元分隔符";
    default: return "其他";
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 输入.h264 [显示前几个]\n", argv[0]); return 1; }
    int show = argc > 2 ? atoi(argv[2]) : 20;

    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    long count[32] = {0}, bytes[32] = {0}, total = 0;
    long pos = 0;
    // 找第一个起始码
    while (pos + 3 < size && !(buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)) pos++;
    printf("  序号      偏移  起始码    大小  ref  类型\n");
    while (pos + 3 < size) {
        long start = pos + 3;                      // NAL 单元从起始码 00 00 01 之后开始
        int sc_len = (pos > 0 && buf[pos - 1] == 0) ? 4 : 3;
        // 找下一个起始码，两者之间就是一个 NAL 单元
        long next = start;
        while (next + 2 < size && !(buf[next] == 0 && buf[next + 1] == 0 && buf[next + 2] == 1)) next++;
        if (next + 2 >= size) next = size;
        long end = next;
        if (end < size && buf[end - 1] == 0) end--;  // 4 字节起始码的第一个 0 属于下一个
        int header = buf[start];
        int ref_idc = (header >> 5) & 3, type = header & 31;
        long len = end - start;
        if (total < show)
            printf("%6ld  %8ld  %6d  %6ld  %3d  %2d %s\n", total, start - sc_len, sc_len, len, ref_idc, type, nal_type_name(type));
        count[type]++; bytes[type] += len; total++;
        pos = next;
    }
    printf("...\n共 %ld 个 NAL 单元\n", total);
    for (int t = 0; t < 32; t++)
        if (count[t])
            printf("类型 %2d：%4ld 个，%8ld 字节（%4.1f%%）  %s\n", t, count[t], bytes[t], 100.0 * bytes[t] / size, nal_type_name(t));
    free(buf);
    return 0;
}
```

4 字节起始码 `00 00 00 01` 可以看作「一个 `00` + 3 字节起始码」，所以程序只查找 `00 00 01`，再看前面一个字节是不是 `00`。

```bash
clang -O2 -o h264_nalu h264_nalu.c
./h264_nalu bbb.h264 12
```

```
  序号      偏移  起始码    大小  ref  类型
     0         0       4     685    0   6 SEI 补充增强信息
     1       689       4      29    3   7 SPS 序列参数集
     2       722       4       6    3   8 PPS 图像参数集
     3       732       3   95825    3   5 IDR 图像的片
     4     96560       4    2272    2   1 非 IDR 图像的片
     5     98836       4     689    2   1 非 IDR 图像的片
     6     99529       4     493    0   1 非 IDR 图像的片
     7    100026       4    2229    2   1 非 IDR 图像的片
     8    102259       4     646    2   1 非 IDR 图像的片
     9    102909       4     491    0   1 非 IDR 图像的片
    10    103404       4    1743    2   1 非 IDR 图像的片
    11    105151       4     545    2   1 非 IDR 图像的片
...
共 255 个 NAL 单元
类型  1： 233 个， 2100064 字节（74.3%）  非 IDR 图像的片
类型  5：   7 个，  725043 字节（25.6%）  IDR 图像的片
类型  6：   1 个，     685 字节（ 0.0%）  SEI 补充增强信息
类型  7：   7 个，     203 字节（ 0.0%）  SPS 序列参数集
类型  8：   7 个，      42 字节（ 0.0%）  PPS 图像参数集
```

几个观察：

- **开头是一个 SEI**，685 字节，内容是 x264 写入的版本和全部编码参数，可以直接用 `strings` 看：

  ```bash
  head -c 689 bbb.h264 | strings -n 20 | tr ' ' '\n' | grep -E "cabac|^ref=|bframes|b_pyramid|keyint|scenecut|crf="
  ```

  ```
  cabac=1
  ref=3
  bframes=2
  b_pyramid=2
  keyint=48
  keyint_min=4
  scenecut=40
  crf=23.0
  ```

  `keyint=48` 就是 `-g 48`，`keyint_min=4` 第 3.3 节会用到。

- **SPS、PPS 各有 7 个**，每个 IDR 片前面都重复了一次。参数集只有 29 和 6 字节，重复几次几乎不占空间，好处是从任何一个 IDR 开始都能解码：直播中途加入的观众、拖动进度条，都需要这一点。

- **片的数据占了 99.9%**。IDR 片只有 7 个，却占了 25.6% 的数据，平均每个 104 KB；普通片平均 9 KB。

- **`nal_ref_idc` 揭示了参考关系**。P 片都是 2；B 片有的是 2、有的是 0。SEI 中 `b_pyramid=2` 表示打开了 **B 帧金字塔**：两个连续的 B 帧中，第一个也会被第二个参考，所以它的 `ref_idc` 不为 0；第二个没有任何帧参考它，`ref_idc` 是 0。第 4 节会利用这一点做实验。

- **起始码有 3 字节也有 4 字节**。标准规定：参数集和每一帧的第一个 NAL 单元前面用 4 字节，其余的可以用 3 字节。IDR 片前面已经有了同一帧的 SEI、SPS、PPS，所以它的起始码是 3 字节。

## 3. 按位解析 SPS 和片头

NAL 单元内部的字段大多不是按字节对齐的，而是一个接一个的比特，很多字段的长度还不固定。要读懂它们，需要先会读两种编码。

### 3.1 指数哥伦布编码

SPS 和片头中的大部分整数用**指数哥伦布编码**（Exp-Golomb）表示，标准中记作 `ue(v)`：小的数用短编码，大的数用长编码。读法是：

1. 数前导 0 的个数，记为 n；
2. 跳过后面的 1；
3. 再读 n 位，得到 x；结果是 2ⁿ − 1 + x。

| 值 | 编码 | 值 | 编码 |
|---|---|---|---|
| 0 | `1` | 5 | `00110` |
| 1 | `010` | 6 | `00111` |
| 2 | `011` | 7 | `0001000` |
| 3 | `00100` | 8 | `0001001` |
| 4 | `00101` | 79 | `0000001010000` |

例如 `00101`：前面 2 个 0，跳过 1，再读 2 位 `01` = 1，结果是 2² − 1 + 1 = 4。编码的规律是：值加 1 写成二进制，有几位，前面就补几减一个 0（4 + 1 = 5 = `101`，3 位，前面补 2 个 0）。

这种编码不需要事先知道长度，而且参数大多是 0、1、2 这样的小数，只占 1～3 位，比固定用 8 位或 32 位省得多。另有一种 `se(v)` 表示有符号数，把 1、2、3、4…… 依次对应到 1、−1、2、−2……

### 3.2 解析程序

程序由三部分组成：按位读取（`read_bits` 读 n 位，`read_ue` 读指数哥伦布码）、去掉防竞争字节、按照标准中规定的字段顺序依次读出 SPS 和片头：

```c
// h264_parse.c：解析 H.264 裸流中的 SPS 和每个片的片头
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------- 按位读取 ----------
typedef struct {
    const unsigned char *data;
    long size, pos;            // pos 以位为单位
} BitReader;

static int read_bit(BitReader *br)
{
    if (br->pos >= br->size * 8) return 0;
    int bit = (br->data[br->pos / 8] >> (7 - br->pos % 8)) & 1;
    br->pos++;
    return bit;
}

static unsigned read_bits(BitReader *br, int n)        // u(n)：n 位无符号整数
{
    unsigned v = 0;
    while (n--) v = (v << 1) | read_bit(br);
    return v;
}

static unsigned read_ue(BitReader *br)                 // ue(v)：无符号指数哥伦布码
{
    int zeros = 0;
    while (read_bit(br) == 0) zeros++;                 // 数前导 0 的个数
    return (1u << zeros) - 1 + read_bits(br, zeros);   // 再读同样多位
}

// 去掉防竞争字节：00 00 03 中的 03 是编码器插进去的，解析前要删掉
static long unescape(const unsigned char *src, long n, unsigned char *dst, long *removed)
{
    long j = 0, zeros = 0;
    for (long i = 0; i < n; i++) {
        if (zeros >= 2 && src[i] == 3) { zeros = 0; (*removed)++; continue; }
        zeros = src[i] == 0 ? zeros + 1 : 0;
        dst[j++] = src[i];
    }
    return j;
}

// ---------- SPS ----------
typedef struct {
    int log2_max_frame_num, poc_type, log2_max_poc_lsb;
} Sps;

static void parse_sps(BitReader *br, Sps *sps)
{
    int profile = read_bits(br, 8);
    int constraints = read_bits(br, 8);
    int level = read_bits(br, 8);
    int id = read_ue(br);
    int chroma_format = 1, bit_depth = 8;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244) {
        chroma_format = read_ue(br);
        if (chroma_format == 3) read_bit(br);          // separate_colour_plane_flag
        bit_depth = read_ue(br) + 8;                   // bit_depth_luma_minus8
        read_ue(br);                                   // bit_depth_chroma_minus8
        read_bit(br);                                  // qpprime_y_zero_transform_bypass_flag
        if (read_bit(br)) { printf("（不支持缩放矩阵）\n"); exit(1); }
    }
    sps->log2_max_frame_num = read_ue(br) + 4;
    sps->poc_type = read_ue(br);
    if (sps->poc_type == 0) sps->log2_max_poc_lsb = read_ue(br) + 4;
    int max_refs = read_ue(br);
    read_bit(br);                                      // gaps_in_frame_num_value_allowed_flag
    int width_mbs = read_ue(br) + 1;
    int height_mbs = read_ue(br) + 1;
    int frame_mbs_only = read_bit(br);
    if (!frame_mbs_only) read_bit(br);
    read_bit(br);                                      // direct_8x8_inference_flag
    int crop_l = 0, crop_r = 0, crop_t = 0, crop_b = 0;
    if (read_bit(br)) {                                // frame_cropping_flag
        crop_l = read_ue(br); crop_r = read_ue(br); crop_t = read_ue(br); crop_b = read_ue(br);
    }
    int vui = read_bit(br);

    printf("profile_idc = %d（%s），constraint_flags = 0x%02X，level_idc = %d（%d.%d）\n", profile,
           profile == 66 ? "Baseline" : profile == 77 ? "Main" : profile == 100 ? "High" : "其他",
           constraints, level, level / 10, level % 10);
    printf("seq_parameter_set_id = %d，chroma_format_idc = %d（%s），位深 %d\n", id, chroma_format,
           chroma_format == 1 ? "4:2:0" : chroma_format == 2 ? "4:2:2" : "其他", bit_depth);
    printf("log2_max_frame_num = %d，pic_order_cnt_type = %d，log2_max_poc_lsb = %d，max_num_ref_frames = %d\n",
           sps->log2_max_frame_num, sps->poc_type, sps->log2_max_poc_lsb, max_refs);
    printf("宽 %d 个宏块，高 %d 个宏块 -> %dx%d，裁剪 (%d,%d,%d,%d)，frame_mbs_only = %d，VUI = %d\n",
           width_mbs, height_mbs, width_mbs * 16 - (crop_l + crop_r) * 2,
           height_mbs * 16 - (crop_t + crop_b) * 2, crop_l, crop_r, crop_t, crop_b, frame_mbs_only, vui);
}

// ---------- 片头 ----------
static void parse_slice(BitReader *br, const Sps *sps, int nal_type, int ref_idc, long index, int verbose, long *types)
{
    static const char *names = "PBIpi";                // slice_type % 5：0 P，1 B，2 I，3 SP，4 SI
    unsigned first_mb = read_ue(br);
    unsigned slice_type = read_ue(br);
    read_ue(br);                                       // pic_parameter_set_id
    unsigned frame_num = read_bits(br, sps->log2_max_frame_num);
    if (nal_type == 5) read_ue(br);                    // idr_pic_id
    unsigned poc_lsb = sps->poc_type == 0 ? read_bits(br, sps->log2_max_poc_lsb) : 0;
    char t = names[slice_type % 5];
    types[slice_type % 5]++;
    if (verbose)
        printf("%4ld  %c%s  first_mb=%u  frame_num=%2u  poc_lsb=%3u  ref_idc=%d\n", index, t,
               nal_type == 5 ? "(IDR)" : "     ", first_mb, frame_num, poc_lsb, ref_idc);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 输入.h264 [显示前几个片]\n", argv[0]); return 1; }
    int show = argc > 2 ? atoi(argv[2]) : 16;
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(size), *rbsp = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    Sps sps = {0};
    long slices = 0, removed = 0, types[5] = {0}, sps_count = 0;
    long pos = 0;
    while (pos + 3 < size && !(buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)) pos++;
    while (pos + 3 < size) {
        long start = pos + 3, next = start;
        while (next + 2 < size && !(buf[next] == 0 && buf[next + 1] == 0 && buf[next + 2] == 1)) next++;
        if (next + 2 >= size) next = size;
        long end = (next < size && buf[next - 1] == 0) ? next - 1 : next;

        int type = buf[start] & 31, ref_idc = (buf[start] >> 5) & 3;
        long n = unescape(buf + start + 1, end - start - 1, rbsp, &removed);   // 跳过 1 字节的 NAL 头
        BitReader br = {rbsp, n, 0};
        if (type == 7) {
            if (sps_count++ == 0) parse_sps(&br, &sps);   // 每个 IDR 前都重复一份 SPS，只解析第一个
        } else if (type == 1 || type == 5) {
            parse_slice(&br, &sps, type, ref_idc, slices, slices < show, types);
            slices++;
        }
        pos = next;
    }
    printf("...\n共 %ld 个片：I %ld，P %ld，B %ld；去掉了 %ld 个防竞争字节\n",
           slices, types[2], types[0], types[1], removed);
    free(buf); free(rbsp);
    return 0;
}
```

SPS 中有很多字段，这里只解析到 `vui_parameters_present_flag`，后面的 VUI（色彩空间、帧率等）就不读了。High 档次多出来的几个字段（色度格式、位深等）要单独判断。片头只需要读到 POC。

```bash
clang -O2 -o h264_parse h264_parse.c
./h264_parse bbb.h264 13
```

```
profile_idc = 100（High），constraint_flags = 0x00，level_idc = 31（3.1）
seq_parameter_set_id = 0，chroma_format_idc = 1（4:2:0），位深 8
log2_max_frame_num = 4，pic_order_cnt_type = 0，log2_max_poc_lsb = 6，max_num_ref_frames = 4
宽 80 个宏块，高 45 个宏块 -> 1280x720，裁剪 (0,0,0,0)，frame_mbs_only = 1，VUI = 1
   0  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
   1  P       first_mb=0  frame_num= 1  poc_lsb=  6  ref_idc=2
   2  B       first_mb=0  frame_num= 2  poc_lsb=  2  ref_idc=2
   3  B       first_mb=0  frame_num= 3  poc_lsb=  4  ref_idc=0
   4  P       first_mb=0  frame_num= 3  poc_lsb= 12  ref_idc=2
   5  B       first_mb=0  frame_num= 4  poc_lsb=  8  ref_idc=2
   6  B       first_mb=0  frame_num= 5  poc_lsb= 10  ref_idc=0
   7  P       first_mb=0  frame_num= 5  poc_lsb= 18  ref_idc=2
   8  B       first_mb=0  frame_num= 6  poc_lsb= 14  ref_idc=2
   9  B       first_mb=0  frame_num= 7  poc_lsb= 16  ref_idc=0
  10  P       first_mb=0  frame_num= 7  poc_lsb= 24  ref_idc=2
  11  B       first_mb=0  frame_num= 8  poc_lsb= 20  ref_idc=2
  12  B       first_mb=0  frame_num= 9  poc_lsb= 22  ref_idc=0
...
共 240 个片：I 8，P 90，B 142；去掉了 26 个防竞争字节
```

先看 SPS。`profile_idc = 100` 是 High 档次，`level_idc = 31` 是 3.1 级，和第 1 章 `ffprobe` 显示的 `profile=High`、`level=31` 一致。分辨率不是直接存的，而是存**宏块数减 1**：宽 79 + 1 = 80 个宏块，高 44 + 1 = 45 个，乘以 16 得到 1280×720。把 SPS 的这几个字节逐位对照字段画出来：

<figure class="diagram">
<svg viewBox="0 0 680 218" role="img" aria-label="SPS 的第 4 到第 9 个字节 AC D9 40 50 05 BA 逐位解析：按指数哥伦布码依次读出 seq_parameter_set_id、chroma_format_idc 等字段，宽 79 加 1 个宏块，高 44 加 1 个宏块">
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="14" style="fill:var(--muted);font-size:11px">SPS 第 4～9 字节（前 3 字节是 profile_idc、constraint_flags、level_idc），按指数哥伦布码和固定位数依次读出：</text>
<rect x="10" y="24" width="104" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="62" y="37" text-anchor="middle" style="fill:var(--fg)">AC</text>
<rect x="114" y="24" width="104" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="166" y="37" text-anchor="middle" style="fill:var(--fg)">D9</text>
<rect x="218" y="24" width="104" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="270" y="37" text-anchor="middle" style="fill:var(--fg)">40</text>
<rect x="322" y="24" width="104" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="374" y="37" text-anchor="middle" style="fill:var(--fg)">50</text>
<rect x="426" y="24" width="104" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="478" y="37" text-anchor="middle" style="fill:var(--fg)">05</text>
<rect x="530" y="24" width="104" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="582" y="37" text-anchor="middle" style="fill:var(--fg)">BA</text>
<rect x="10" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="16.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="16.0" y="78" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">1</text>
<rect x="23" y="46" width="12" height="20" style="fill:var(--link);opacity:.75"/><text x="29.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="36" y="46" width="12" height="20" style="fill:var(--link);opacity:.75"/><text x="42.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="49" y="46" width="12" height="20" style="fill:var(--link);opacity:.75"/><text x="55.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="42.0" y="78" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">2</text>
<rect x="62" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="68.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="68.0" y="78" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">3</text>
<rect x="75" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="81.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="81.0" y="88" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">4</text>
<rect x="88" y="46" width="12" height="20" style="fill:var(--link);opacity:.75"/><text x="94.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="94.0" y="78" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">5</text>
<rect x="101" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="107.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="107.0" y="88" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">6</text>
<rect x="114" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="120.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="120.0" y="78" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">7</text>
<rect x="127" y="46" width="12" height="20" style="fill:var(--link);opacity:.75"/><text x="133.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="133.0" y="88" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">8</text>
<rect x="140" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="146.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="153" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="159.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="166" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="172.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="159.0" y="78" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">9</text>
<rect x="179" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="185.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="192" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="198.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="205" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="211.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="218" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="224.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="231" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="237.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="211.0" y="78" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">10</text>
<rect x="244" y="46" width="12" height="20" style="fill:var(--link);opacity:.75"/><text x="250.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="250.0" y="78" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">11</text>
<rect x="257" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="263.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="270" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="276.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="283" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="289.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="296" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="302.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="309" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="315.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="322" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="328.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="335" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="341.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="348" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="354.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="361" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="367.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="374" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="380.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="387" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="393.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="400" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="406.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="413" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="419.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="341.0" y="78" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">12</text>
<rect x="426" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="432.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="439" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="445.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="452" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="458.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="465" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="471.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="478" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="484.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="491" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="497.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="504" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="510.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="517" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="523.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="530" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="536.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="543" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="549.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="556" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="562.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="497.0" y="78" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">13</text>
<rect x="569" y="46" width="12" height="20" style="fill:var(--link);opacity:.75"/><text x="575.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="575.0" y="88" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">14</text>
<rect x="582" y="46" width="12" height="20" style="fill:var(--warn);opacity:.75"/><text x="588.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="588.0" y="78" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">15</text>
<rect x="595" y="46" width="12" height="20" style="fill:var(--accent);opacity:.75"/><text x="601.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="601.0" y="88" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">16</text>
<rect x="608" y="46" width="12" height="20" style="fill:var(--link);opacity:.75"/><text x="614.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="614.0" y="78" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">17</text>
<rect x="621" y="46" width="12" height="20" style="fill:var(--panel-2)"/><text x="627.0" y="60" text-anchor="middle" style="fill:var(--muted);font-size:11px">0</text>
<text x="10" y="110" style="fill:var(--accent);font-size:11px;font-weight:bold">1</text><text x="28" y="110" style="fill:var(--fg);font-size:11px">seq_parameter_set_id = 0</text>
<text x="10" y="128" style="fill:var(--link);font-size:11px;font-weight:bold">2</text><text x="28" y="128" style="fill:var(--fg);font-size:11px">chroma_format_idc = 1</text>
<text x="10" y="146" style="fill:var(--warn);font-size:11px;font-weight:bold">3</text><text x="28" y="146" style="fill:var(--fg);font-size:11px">bit_depth_luma−8 = 0</text>
<text x="10" y="164" style="fill:var(--accent);font-size:11px;font-weight:bold">4</text><text x="28" y="164" style="fill:var(--fg);font-size:11px">bit_depth_chroma−8 = 0</text>
<text x="10" y="182" style="fill:var(--link);font-size:11px;font-weight:bold">5</text><text x="28" y="182" style="fill:var(--fg);font-size:11px">qpprime_y_zero… = 0</text>
<text x="10" y="200" style="fill:var(--warn);font-size:11px;font-weight:bold">6</text><text x="28" y="200" style="fill:var(--fg);font-size:11px">缩放矩阵 = 0</text>
<text x="230" y="110" style="fill:var(--accent);font-size:11px;font-weight:bold">7</text><text x="248" y="110" style="fill:var(--fg);font-size:11px">log2_max_frame_num−4 = 0</text>
<text x="230" y="128" style="fill:var(--link);font-size:11px;font-weight:bold">8</text><text x="248" y="128" style="fill:var(--fg);font-size:11px">pic_order_cnt_type = 0</text>
<text x="230" y="146" style="fill:var(--warn);font-size:11px;font-weight:bold">9</text><text x="248" y="146" style="fill:var(--fg);font-size:11px">log2_max_poc_lsb−4 = 2</text>
<text x="230" y="164" style="fill:var(--accent);font-size:11px;font-weight:bold">10</text><text x="248" y="164" style="fill:var(--fg);font-size:11px">max_num_ref_frames = 4</text>
<text x="230" y="182" style="fill:var(--link);font-size:11px;font-weight:bold">11</text><text x="248" y="182" style="fill:var(--fg);font-size:11px">gaps_in_frame_num = 0</text>
<text x="230" y="200" style="fill:var(--warn);font-size:11px;font-weight:bold">12</text><text x="248" y="200" style="fill:var(--fg);font-size:11px">pic_width_in_mbs−1 = 79</text>
<text x="450" y="110" style="fill:var(--accent);font-size:11px;font-weight:bold">13</text><text x="468" y="110" style="fill:var(--fg);font-size:11px">pic_height_in_map_units−1 = 44</text>
<text x="450" y="128" style="fill:var(--link);font-size:11px;font-weight:bold">14</text><text x="468" y="128" style="fill:var(--fg);font-size:11px">frame_mbs_only = 1</text>
<text x="450" y="146" style="fill:var(--warn);font-size:11px;font-weight:bold">15</text><text x="468" y="146" style="fill:var(--fg);font-size:11px">direct_8x8_inference = 1</text>
<text x="450" y="164" style="fill:var(--accent);font-size:11px;font-weight:bold">16</text><text x="468" y="164" style="fill:var(--fg);font-size:11px">frame_cropping = 0</text>
<text x="450" y="182" style="fill:var(--link);font-size:11px;font-weight:bold">17</text><text x="468" y="182" style="fill:var(--fg);font-size:11px">vui_parameters_present = 1</text>
  </g>
</svg>
</figure>

如果分辨率不是 16 的倍数呢？例如 1080 = 67.5 × 16，编码器只能编码 68 个宏块高（1088 行），再用 SPS 中的**裁剪**字段（frame_cropping）告诉解码器把下面 8 行裁掉，练习 3 会验证。

### 3.3 片头：类型、frame_num 和 POC

片头的第一个字段 `first_mb_in_slice` 是这个片从第几个宏块开始。所有片都是 0，说明每帧只有一个片（SEI 中的 `sliced_threads=0`）。接下来几个字段：

- **slice_type**：片的类型，0 是 P，1 是 B，2 是 I（5～9 是同样的含义，表示整帧都是这种类型）。统计结果 I 8 个、P 90 个、B 142 个，和第 1 章用 `ffprobe` 统计的帧类型完全一致。
- **frame_num**：参考帧的编号，每遇到一个参考帧加 1，解码器用它发现参考帧丢失。
- **pic_order_cnt_lsb**（POC）：显示顺序。码流中的顺序是**解码顺序**，POC 才是显示的顺序。

把前几个片按 POC 排一下：码流中是 I(0)、P(6)、B(2)、B(4)、P(12)……，POC 除以 2 就是显示顺序中的位置：0、3、1、2、6……。也就是说，第 3 帧（P）在码流中排在第 1、2 帧（B）之前，因为这两个 B 帧要参考它。这正是第 1 章看到的 PTS 和 DTS 不同的根源，本章最后一篇会专门讨论时间戳。

### 3.4 I 帧不一定是 IDR

8 个 I 片中，类型 5（IDR）只有 7 个。把所有 I 片找出来（序号是在码流中的顺序）：

```bash
./h264_parse bbb.h264 240 | grep " I"
```

```
   0  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
  31  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
  79  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
  83  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
 131  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
 133  I       first_mb=0  frame_num= 2  poc_lsb=  4  ref_idc=2
 179  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
 227  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
```

第 133 帧是镜头切换处的 I 帧，但不是 IDR。它和上一个 IDR（第 131 帧）只隔了 2 帧，小于 SEI 中的 `keyint_min=4`（x264 默认取 GOP 长度的十分之一），x264 不允许两个 IDR 靠得这么近，就只插入了一个普通的 I 帧。

两者的区别在于：

- **I 帧**只是说这一帧自己不参考别的帧；它**后面**的帧仍然可以参考它**前面**的帧。
- **IDR 帧**（Instantaneous Decoding Refresh，即时解码刷新）除了是 I 帧之外，还要求解码器清空参考帧列表：IDR 之后的帧绝不会参考 IDR 之前的任何帧。

所以只有 IDR 才是真正的「随机访问点」：从 IDR 开始解码一定能得到正确的画面，从普通 I 帧开始则不一定。MP4 中的关键帧列表（第 4 篇讲的 `stss`）只记录了 IDR：

```bash
ffprobe -v error -select_streams v -show_entries packet=pts,flags -of csv=p=0 bbb-10s.mp4 | awk -F, '$2 ~ /K/ {print $1/512}' | tr '\n' ' '
```

```
0 31 79 83 131 179 227
```

只有 7 个，第 133 帧不在其中。拖动进度条时，播放器只会跳到这 7 个位置。

## 4. 丢掉一个片会怎样

P 帧和 B 帧都只存了「和参考帧相比的差别」，如果参考帧丢了，会发生什么？写一个程序，复制码流时丢掉第 n 个片：

```c
// h264_drop.c：复制 H.264 裸流，但丢掉第 n 个片（按码流中的顺序，从 0 数起）
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc != 4) { fprintf(stderr, "用法: %s 输入.h264 输出.h264 n\n", argv[0]); return 1; }
    long drop = atol(argv[3]);
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    FILE *out = fopen(argv[2], "wb");
    long pos = 0, slice = 0;
    while (pos + 3 < size && !(buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)) pos++;
    while (pos + 3 < size) {
        long next = pos + 3;
        while (next + 2 < size && !(buf[next] == 0 && buf[next + 1] == 0 && buf[next + 2] == 1)) next++;
        if (next + 2 >= size) next = size;
        int type = buf[pos + 3] & 31;
        int is_slice = type == 1 || type == 5;
        if (!(is_slice && slice == drop))
            fwrite(buf + pos, 1, next - pos, out);    // 原样写出起始码和 NAL 单元
        else
            printf("丢掉第 %ld 个片：%ld 字节\n", slice, next - pos);
        if (is_slice) slice++;
        pos = next;
    }
    fclose(out);
    free(buf);
    return 0;
}
```

### 4.1 丢掉一个 P 帧

选蝴蝶飞过的那个镜头，丢掉码流中的第 140 个片。它是一个 P 片，POC 是 22，在显示顺序中是第 131 + 11 = 142 帧：

```bash
clang -O2 -o h264_drop h264_drop.c
./h264_drop bbb.h264 drop.h264 140
ffmpeg -i drop.h264 -f rawvideo -pix_fmt yuv420p drop.yuv
```

```
丢掉第 140 个片：25629 字节
[h264 @ 0x7ce6c23b80] reference picture missing during reorder
[h264 @ 0x7ce6c23b80] Missing reference picture, default is 65554
[h264 @ 0x7ce6c21f80] mmco: unref short failure
```

解码器发现了参考帧丢失（就是靠 `frame_num` 的跳变），但它没有办法恢复数据，只能拿别的帧顶替，继续解码。逐帧和完整码流解码的结果比较 PSNR（丢掉的那一帧跳过不比）：

<figure class="diagram">
<svg viewBox="0 0 680 215" role="img" aria-label="丢掉第 142 帧后各帧的 PSNR：第 140 帧到第 178 帧都只有 21 到 27 dB，第 179 帧的 IDR 之后恢复为与原始完全相同">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(50,30)">
<line x1="0" y1="128.6" x2="600" y2="128.6" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="132.6" text-anchor="end" style="fill:var(--muted);font-size:10px">20</text>
<line x1="0" y1="85.7" x2="600" y2="85.7" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="89.7" text-anchor="end" style="fill:var(--muted);font-size:10px">30</text>
<line x1="0" y1="42.9" x2="600" y2="42.9" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="46.9" text-anchor="end" style="fill:var(--muted);font-size:10px">40</text>
<line x1="0" y1="0.0" x2="600" y2="0.0" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="-6" y="4.0" text-anchor="end" style="fill:var(--muted);font-size:10px">50</text>
<rect x="0.0" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="10.2" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="20.3" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="30.5" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="40.7" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="50.8" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="61.0" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="71.2" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="81.4" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="91.5" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="101.7" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="111.9" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="122.0" y="97.3" width="7.6" height="52.7" style="fill:var(--danger);opacity:.8"/>
<rect x="132.2" y="121.7" width="7.6" height="28.3" style="fill:var(--danger);opacity:.8"/>
<rect x="152.5" y="123.2" width="7.6" height="26.8" style="fill:var(--danger);opacity:.8"/>
<rect x="162.7" y="122.5" width="7.6" height="27.5" style="fill:var(--danger);opacity:.8"/>
<rect x="172.9" y="122.5" width="7.6" height="27.5" style="fill:var(--danger);opacity:.8"/>
<rect x="183.1" y="122.1" width="7.6" height="27.9" style="fill:var(--danger);opacity:.8"/>
<rect x="193.2" y="121.2" width="7.6" height="28.8" style="fill:var(--danger);opacity:.8"/>
<rect x="203.4" y="121.1" width="7.6" height="28.9" style="fill:var(--danger);opacity:.8"/>
<rect x="213.6" y="119.8" width="7.6" height="30.2" style="fill:var(--danger);opacity:.8"/>
<rect x="223.7" y="118.4" width="7.6" height="31.6" style="fill:var(--danger);opacity:.8"/>
<rect x="233.9" y="116.7" width="7.6" height="33.3" style="fill:var(--danger);opacity:.8"/>
<rect x="244.1" y="115.9" width="7.6" height="34.1" style="fill:var(--danger);opacity:.8"/>
<rect x="254.2" y="116.6" width="7.6" height="33.4" style="fill:var(--danger);opacity:.8"/>
<rect x="264.4" y="118.2" width="7.6" height="31.8" style="fill:var(--danger);opacity:.8"/>
<rect x="274.6" y="117.0" width="7.6" height="33.0" style="fill:var(--danger);opacity:.8"/>
<rect x="284.7" y="115.8" width="7.6" height="34.2" style="fill:var(--danger);opacity:.8"/>
<rect x="294.9" y="114.7" width="7.6" height="35.3" style="fill:var(--danger);opacity:.8"/>
<rect x="305.1" y="113.6" width="7.6" height="36.4" style="fill:var(--danger);opacity:.8"/>
<rect x="315.3" y="113.4" width="7.6" height="36.6" style="fill:var(--danger);opacity:.8"/>
<rect x="325.4" y="114.0" width="7.6" height="36.0" style="fill:var(--danger);opacity:.8"/>
<rect x="335.6" y="113.0" width="7.6" height="37.0" style="fill:var(--danger);opacity:.8"/>
<rect x="345.8" y="112.0" width="7.6" height="38.0" style="fill:var(--danger);opacity:.8"/>
<rect x="355.9" y="111.5" width="7.6" height="38.5" style="fill:var(--danger);opacity:.8"/>
<rect x="366.1" y="111.0" width="7.6" height="39.0" style="fill:var(--danger);opacity:.8"/>
<rect x="376.3" y="110.9" width="7.6" height="39.1" style="fill:var(--danger);opacity:.8"/>
<rect x="386.4" y="110.9" width="7.6" height="39.1" style="fill:var(--danger);opacity:.8"/>
<rect x="396.6" y="110.7" width="7.6" height="39.3" style="fill:var(--danger);opacity:.8"/>
<rect x="406.8" y="110.4" width="7.6" height="39.6" style="fill:var(--danger);opacity:.8"/>
<rect x="416.9" y="110.4" width="7.6" height="39.6" style="fill:var(--danger);opacity:.8"/>
<rect x="427.1" y="110.1" width="7.6" height="39.9" style="fill:var(--danger);opacity:.8"/>
<rect x="437.3" y="109.9" width="7.6" height="40.1" style="fill:var(--danger);opacity:.8"/>
<rect x="447.5" y="109.9" width="7.6" height="40.1" style="fill:var(--danger);opacity:.8"/>
<rect x="457.6" y="109.8" width="7.6" height="40.2" style="fill:var(--danger);opacity:.8"/>
<rect x="467.8" y="109.6" width="7.6" height="40.4" style="fill:var(--danger);opacity:.8"/>
<rect x="478.0" y="109.6" width="7.6" height="40.4" style="fill:var(--danger);opacity:.8"/>
<rect x="488.1" y="109.3" width="7.6" height="40.7" style="fill:var(--danger);opacity:.8"/>
<rect x="498.3" y="109.1" width="7.6" height="40.9" style="fill:var(--danger);opacity:.8"/>
<rect x="508.5" y="109.1" width="7.6" height="40.9" style="fill:var(--danger);opacity:.8"/>
<rect x="518.6" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="528.8" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="539.0" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="549.2" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="559.3" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="569.5" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="579.7" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="589.8" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<rect x="600.0" y="0.0" width="7.6" height="150.0" style="fill:var(--accent);opacity:.8"/>
<line x1="33.5" y1="-8" x2="33.5" y2="150" style="stroke:var(--fg);stroke-dasharray:3 2"/><text x="36.5" y="-10" style="fill:var(--fg);font-size:11px">131 IDR</text>
<line x1="145.4" y1="-8" x2="145.4" y2="150" style="stroke:var(--fg);stroke-dasharray:3 2"/><text x="148.4" y="-10" style="fill:var(--fg);font-size:11px">142 丢失</text>
<line x1="521.6" y1="-8" x2="521.6" y2="150" style="stroke:var(--fg);stroke-dasharray:3 2"/><text x="524.6" y="-10" style="fill:var(--fg);font-size:11px">179 IDR</text>
<text x="23.3" y="165" text-anchor="middle" style="fill:var(--muted);font-size:10px">130</text>
<text x="125.0" y="165" text-anchor="middle" style="fill:var(--muted);font-size:10px">140</text>
<text x="226.7" y="165" text-anchor="middle" style="fill:var(--muted);font-size:10px">150</text>
<text x="328.4" y="165" text-anchor="middle" style="fill:var(--muted);font-size:10px">160</text>
<text x="430.1" y="165" text-anchor="middle" style="fill:var(--muted);font-size:10px">170</text>
<text x="531.8" y="165" text-anchor="middle" style="fill:var(--muted);font-size:10px">180</text>
    </g>
    <rect x="50" y="198" width="12" height="12" style="fill:var(--accent);opacity:.8"/><text x="68" y="208" style="fill:var(--muted)">与原始解码结果完全相同（画到 50 dB 封顶）</text>
    <rect x="390" y="198" width="12" height="12" style="fill:var(--danger);opacity:.8"/><text x="408" y="208" style="fill:var(--muted)">有错误（PSNR，dB）</text>
  </g>
</svg>
</figure>

- 第 140、141 帧是两个 B 帧，在码流中排在丢失的 P 帧后面，要参考它，所以也出错了。
- 第 143～178 帧，**36 帧全部有错误**，PSNR 只有 21～24 dB。因为每一个 P 帧都参考前一个 P 帧，错误像接力一样一直传下去。
- 第 179 帧是 IDR，解码器清空参考帧，从这里开始和原始结果**完全相同**。

<figure>
<img src="/img/av/h264-drop.jpg" alt="第 160 帧的对比：左边是正常解码的结果，右边是丢掉第 142 帧后的结果，蝴蝶周围出现大片花屏" width="640">
<figcaption>第 160 帧，左：完整码流；右：丢掉第 142 帧之后。蝴蝶周围大片花屏，而几乎没动的兔子和草地看不出问题</figcaption>
</figure>

丢的明明是第 142 帧，第 160 帧却还是花的。错误集中在运动的地方：静止的区域每一帧都是「和前一帧一样」，参考帧错了一点影响不大；运动的区域要从参考帧的其他位置搬运像素，错误会被搬到更多的地方。

这就是网络直播和视频会议中常见的「花屏」：丢了一个包，画面一直花到下一个 IDR。所以实时通信的 GOP 通常很短，或者在检测到丢包后，由接收方请求发送方立即发送一个 IDR（WebRTC 中叫 PLI，第 5 章会讲）。

### 4.2 丢掉一个不被参考的 B 帧

再丢第 142 个片：一个 `nal_ref_idc = 0` 的 B 片，显示顺序第 141 帧。同样逐帧比较：

```bash
./h264_drop bbb.h264 dropb.h264 142
```

```
丢掉第 142 个片：6062 字节
```

除了少了这一帧，其余 239 帧和原始结果**全部逐字节相同**。`nal_ref_idc = 0` 说明没有任何帧参考它，丢了就是丢了，不会传播。网络拥塞时，有些系统会优先丢弃这类帧，就是根据 NAL 头中的这两位来判断的。

## 5. Annex B 和 AVCC

本文用的是 Annex B 格式的裸流。但 MP4、MKV、FLV 中的 H.264 并不是这样存的，它们用的是 **AVCC** 格式（也叫 avc1 格式）：

| | Annex B | AVCC |
|---|---|---|
| NAL 单元之间 | 起始码 `00 00 01` 或 `00 00 00 01` | 每个 NAL 单元前面是它的长度（通常 4 字节，大端序） |
| SPS、PPS | 和片一起放在码流中，通常每个 IDR 前重复 | 集中放在容器的头信息中（`avcC` 盒子，FFmpeg 中叫 extradata） |
| 用于 | 裸流文件、MPEG-TS、RTP | MP4、MKV、FLV |

AVCC 有了长度就不需要查找起始码，读取更快；参数集放在头信息中，解码器一开始就能拿到。Annex B 则适合「流」：不需要任何头信息，从中间的任何一个起始码开始都能同步。第 1 章第 4 篇中，同一路视频在 MP4 和 TS 中的哈希值不同，就是因为两种格式之间做了这种转换。从 MP4 中取 H.264 时加的 `-bsf:v h264_mp4toannexb`，作用就是把长度换成起始码，并把 extradata 中的 SPS、PPS 插到每个 IDR 前面。

第 3 篇解析 FLV、第 4 篇解析 MP4 时，会看到 AVCC 格式的码流和 `avcC` 中的参数集。

## 6. 小结

- H.264 码流由 NAL 单元组成；Annex B 格式中，NAL 单元用起始码 `00 00 01` / `00 00 00 01` 隔开。
- NAL 头 1 字节：`nal_ref_idc`（是否被参考）和 `nal_unit_type`（类型）。常见类型：1 普通片、5 IDR 片、6 SEI、7 SPS、8 PPS。
- 编码器在 `00 00` 后面插入防竞争字节 `03`，保证数据中不出现起始码；解析前要先删掉。
- SPS、片头中的字段按位存放，整数多用指数哥伦布编码。分辨率以宏块数存放，不是 16 的倍数时用裁剪字段修正。
- 码流按解码顺序排列，POC 表示显示顺序。
- IDR 是清空参考帧的 I 帧，是真正的随机访问点；普通 I 帧不是。
- 丢失参考帧，错误会一直传到下一个 IDR；丢失不被参考的帧，不影响其他帧。
- MP4 等容器中用 AVCC 格式：长度前缀代替起始码，参数集放在头信息中。

下一篇解析音频码流：AAC 的 ADTS 格式。

## 练习

1. 用 `h264_nalu` 的输出验证：哪些 NAL 单元前面是 4 字节起始码，哪些是 3 字节？和第 2 节说的规则一致吗？
2. 给 `h264_parse.c` 加上 PPS（类型 8）的解析，读出前三个字段：`pic_parameter_set_id`（ue）、`seq_parameter_set_id`（ue）、`entropy_coding_mode_flag`（1 位）。测试视频用的是 CAVLC 还是 CABAC？
3. 用 `ffmpeg -f lavfi -i testsrc2=size=1920x1080:rate=24 -t 1 -c:v libx264 -f h264 t1080.h264` 生成一段 1080p 的码流，用 `h264_parse` 查看 SPS 中的宏块数和裁剪字段。
4. 用 `-bf 0` 重新编码测试视频的前 30 帧，用 `h264_parse` 查看片头，POC 有什么变化？
5. 修改 `h264_drop.c`，不丢掉片，而是把第 140 个片从第 100 个字节起的数据全部改成随机数（模拟传输中的数据损坏），解码器会报告什么？结果和直接丢掉相比有什么不同？

<details>
<summary>点击查看答案</summary>

1. 从输出的「起始码」一列看：SEI（第一个）、每一帧的第一个片都是 4 字节；SPS、PPS 也是 4 字节；只有紧跟在 PPS 后面的 IDR 片是 3 字节。规则是：SPS、PPS，以及每一帧（标准中叫访问单元）的第一个 NAL 单元前面必须用 4 字节起始码（多出来的 `00` 叫 zero_byte），其余的可以用 3 字节。IDR 片前面已经有同一帧的 SEI/SPS/PPS，它不是这一帧的第一个 NAL 单元，所以用 3 字节；P 片、B 片自己就是一帧的第一个 NAL 单元，所以是 4 字节。

2. 在 `main` 中加一个分支：

   ```c
   } else if (type == 8 && pps_count++ == 0) {
       unsigned pps_id = read_ue(&br), sps_id = read_ue(&br);
       int cabac = read_bit(&br);
       printf("PPS：pps_id = %u，sps_id = %u，entropy_coding_mode_flag = %d（%s）\n",
              pps_id, sps_id, cabac, cabac ? "CABAC" : "CAVLC");
   }
   ```

   输出 `entropy_coding_mode_flag = 1（CABAC）`。PPS 去掉 NAL 头后的第一个字节是 `EB` = `1110 1011`，前三位 `1`、`1`、`1` 依次是 ue 编码的 0、ue 编码的 0 和标志位 1。这和 SEI 中的 `cabac=1` 一致。CABAC（基于上下文的自适应二进制算术编码）比 CAVLC 压缩率高 10%～15%，但解码更费 CPU，Baseline 档次不支持它，High 档次默认使用。

3. 输出：

   ```
   profile_idc = 100（High），constraint_flags = 0x00，level_idc = 40（4.0）
   宽 120 个宏块，高 68 个宏块 -> 1920x1080，裁剪 (0,0,0,4)，frame_mbs_only = 1，VUI = 1
   ```

   编码的实际尺寸是 120 × 16 = 1920、68 × 16 = 1088，`frame_crop_bottom_offset = 4`。4:2:0 中裁剪值的单位是 2 行（色度的一行对应亮度的两行），所以裁掉 8 行，得到 1080。级别也从 3.1 升到了 4.0，因为 3.1 级最大只支持 1280×720 左右的分辨率。

4. 输出：

   ```
   log2_max_frame_num = 4，pic_order_cnt_type = 2，log2_max_poc_lsb = 0，max_num_ref_frames = 3
      0  I(IDR)  first_mb=0  frame_num= 0  poc_lsb=  0  ref_idc=3
      1  P       first_mb=0  frame_num= 1  poc_lsb=  0  ref_idc=2
      2  P       first_mb=0  frame_num= 2  poc_lsb=  0  ref_idc=2
   ```

   `pic_order_cnt_type` 从 0 变成了 2。类型 2 表示「显示顺序就是解码顺序」，POC 由 `frame_num` 直接推算，片头中不再有 `pic_order_cnt_lsb` 字段，所以程序打印的都是 0（程序在 `poc_type != 0` 时不读这个字段）。没有 B 帧，就不需要单独记录显示顺序，片头还省了几位。

5. 把丢片的分支改成写出一份改坏的数据：

   ```c
   if (is_slice && slice == drop)
       for (long i = pos + 100; i < next; i++)
           buf[i] = rand() & 0xFF;
   fwrite(buf + pos, 1, next - pos, out);
   ```

   （严格来说，随机数据中可能恰好出现 `00 00 01`，被当成一个新的 NAL 单元。这一次 25 KB 的随机数据中没有出现，用 `h264_nalu` 检查仍然是 255 个 NAL 单元。正常的码流有防竞争字节保护，绝不会出现这种情况。）

   解码时的报错：

   ```
   [h264] error while decoding MB 22 0, bytestream 25512
   [h264] concealing 3600 DC, 3600 AC, 3600 MV errors in P frame
   ```

   解码器读到第 0 行第 22 个宏块时发现数据不对，放弃了这个片，用前一帧把整帧 3600 个宏块全部「掩盖」（conceal）了。CABAC 是算术编码，一旦有一位错了，后面的数据就全乱了，无法在片的中间重新同步，所以虽然前 100 个字节是好的，也只救回了 22 个宏块的位置信息，实际效果和整帧丢失差不多。和直接丢掉相比的区别是：这一帧还在（解码出 240 帧，而不是 239 帧），时间上不会跳一下。逐帧 PSNR 和 4.1 节几乎一样，第 140～178 帧在 20～28 dB，第 179 帧的 IDR 之后恢复正常。

   如果把一帧分成多个片，坏掉的只是其中一个片，其他片还能正确解码。这就是第 1 节说「每个片可以独立解码」的意义，抗丢包要求高的场景会故意把一帧切成多个片。

</details>
