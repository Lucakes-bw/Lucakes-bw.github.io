---
title: FLV 封装格式
date: 2026-10-31 11:00:00
updated: 2026-10-31 11:00:00
categories:
  - 音视频开发
  - 码流与封装格式
tags:
  - 音视频
  - FLV
  - RTMP
  - C
order: 3
description: FLV 是最简单的封装格式之一：9 字节的头，然后是一个接一个的 tag，每个 tag 11 字节的头加数据，后面跟着 4 字节的「上一个 tag 的长度」。用 C 解析测试视频转成的 FLV：开头的 onMetaData 用 AMF0 编码，记录了时长、分辨率和码率；视频 tag 中有 AVC 序列头和合成时间偏移 cts，音频 tag 的采样率字段对 AAC 永远是 44 kHz。再从 FLV 中把 H.264 和 AAC 取出来，长度前缀换成起始码、AudioSpecificConfig 换成 ADTS 头：AAC 与 FFmpeg 的结果逐字节相同，H.264 解码后的画面也完全相同。最后看看新版 FLV 怎样装下 H.265。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：读懂 FLV 文件的结构：文件头、tag、PreviousTagSize；会解析 AMF0 编码的 onMetaData；理解视频 tag 和音频 tag 的头，尤其是 AVC 序列头、合成时间偏移和 AAC 序列头；会从 FLV 中提取 H.264 和 AAC，在 AVCC 与 Annex B、AudioSpecificConfig 与 ADTS 之间转换。
>
> **前置知识**：本章前两篇《H.264 码流》《AAC 码流》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0。

## 1. FLV 是什么

**FLV**（Flash Video）是 Adobe 为 Flash Player 设计的视频格式。Flash 早已退出历史舞台，但 FLV 没有：直播推流最常用的 **RTMP** 协议传输的就是 FLV 的 tag，国内很多直播平台的播放地址也是「HTTP-FLV」，即通过 HTTP 不断地下载一个没有结尾的 FLV 文件。原因很简单：FLV 的结构是所有封装格式中最简单的之一，非常适合一边产生一边传输。

准备测试文件（第 1 章第 4 篇已经做过）：

```bash
ffmpeg -i bbb-10s.mp4 -c copy bbb.flv
```

## 2. 文件结构

FLV 文件由一个 9 字节的**文件头**和一串 **tag** 组成。每个 tag 是一个音频帧、一个视频帧，或者一段脚本数据；每个 tag 后面跟着 4 个字节，记录这个 tag 的总长度（PreviousTagSize）：

<figure class="diagram">
<svg viewBox="0 0 680 270" role="img" aria-label="FLV 文件结构：9 字节文件头，4 字节 PreviousTagSize0，然后是脚本 tag、视频 tag、音频 tag 交替出现，每个 tag 后面跟着 4 字节的 PreviousTagSize；每个 tag 的头 11 字节，包括类型、数据长度、时间戳、扩展时间戳和流 ID">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(10,20)">
      <rect x="0" y="0" width="80" height="34" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="8" y="15" style="fill:var(--fg)">FLV 头</text><text x="8" y="29" style="fill:var(--muted);font-size:10px">9 字节</text>
      <rect x="80" y="0" width="28" height="34" style="fill:var(--border-2)"/><text x="85" y="21" style="fill:var(--fg);font-size:10px">P0</text>
      <rect x="108" y="0" width="100" height="34" style="fill:var(--warn);opacity:.7"/><text x="116" y="15" style="fill:var(--bg)">脚本 tag</text><text x="116" y="29" style="fill:var(--bg);font-size:10px">onMetaData</text>
      <rect x="208" y="0" width="28" height="34" style="fill:var(--border-2)"/><text x="213" y="21" style="fill:var(--fg);font-size:10px">P1</text>
      <rect x="236" y="0" width="110" height="34" style="fill:var(--accent);opacity:.7"/><text x="244" y="15" style="fill:var(--bg)">视频 tag</text><text x="244" y="29" style="fill:var(--bg);font-size:10px">AVC 序列头</text>
      <rect x="346" y="0" width="28" height="34" style="fill:var(--border-2)"/><text x="351" y="21" style="fill:var(--fg);font-size:10px">P2</text>
      <rect x="374" y="0" width="100" height="34" style="fill:var(--link);opacity:.7"/><text x="382" y="15" style="fill:var(--bg)">音频 tag</text><text x="382" y="29" style="fill:var(--bg);font-size:10px">AAC 序列头</text>
      <rect x="474" y="0" width="28" height="34" style="fill:var(--border-2)"/><text x="479" y="21" style="fill:var(--fg);font-size:10px">P3</text>
      <rect x="502" y="0" width="120" height="34" style="fill:var(--accent);opacity:.7"/><text x="510" y="15" style="fill:var(--bg)">视频 tag</text><text x="510" y="29" style="fill:var(--bg);font-size:10px">第一帧（IDR）</text>
      <text x="630" y="21" style="fill:var(--muted)">…</text>
      <text x="0" y="56" style="fill:var(--muted);font-size:11px">P = PreviousTagSize（4 字节）= 前一个 tag 的总长度 = 11 + 数据长度；P0 = 0</text>
      <path d="M236,36 L236,74 L10,92" fill="none" style="stroke:var(--muted)"/><path d="M346,36 L346,74 L640,92" fill="none" style="stroke:var(--muted)"/>
    </g>
    <g transform="translate(20,115)">
      <text x="0" y="-2" style="fill:var(--fg)">tag 头（11 字节）</text>
      <rect x="0" y="8" width="60" height="32" style="fill:var(--warn);opacity:.7"/><text x="8" y="28" style="fill:var(--bg);font-size:11px">类型 1</text>
      <rect x="60" y="8" width="130" height="32" style="fill:var(--link);opacity:.55"/><text x="72" y="28" style="fill:var(--bg);font-size:11px">数据长度 3</text>
      <rect x="190" y="8" width="130" height="32" style="fill:var(--accent);opacity:.65"/><text x="204" y="28" style="fill:var(--bg);font-size:11px">时间戳 3</text>
      <rect x="320" y="8" width="60" height="32" style="fill:var(--accent);opacity:.4"/><text x="326" y="28" style="fill:var(--fg);font-size:11px">扩展 1</text>
      <rect x="380" y="8" width="130" height="32" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="392" y="28" style="fill:var(--muted);font-size:11px">流 ID 3（总是 0）</text>
      <rect x="510" y="8" width="140" height="32" style="fill:var(--panel);stroke:var(--border-2);stroke-dasharray:4 3"/><text x="522" y="28" style="fill:var(--muted);font-size:11px">数据（数据长度）</text>
      <text x="0" y="62" style="fill:var(--muted);font-size:11px">类型：8 音频，9 视频，18 脚本。时间戳单位是毫秒：低 24 位 + 扩展的高 8 位 = 32 位</text>
      <text x="0" y="82" style="fill:var(--muted);font-size:11px">所有多字节整数都是大端序（和 MP4、H.264 一样，和 WAV 相反）</text>
    </g>
  </g>
</svg>
</figure>

**文件头**只有 9 个字节：

```bash
xxd -l 16 bbb.flv
```

```
00000000: 464c 5601 0500 0000 0900 0000 0012 0001  FLV.............
```

`46 4C 56` 是 "FLV"，`01` 是版本，`05` = `0000 0101`，第 0 位表示有视频、第 2 位表示有音频，`00 00 00 09` 是文件头的长度。接着的 `00 00 00 00` 是 PreviousTagSize0，固定为 0。然后第一个 tag 开始：`12` = 18，脚本 tag，数据长度 `00 01 73` = 371。

**PreviousTagSize** 看起来是多余的，它的用处是**从后往前**遍历：从文件末尾读 4 个字节，就知道最后一个 tag 有多长，跳过去就是它的开头。播放器拖动进度条、往回找最近的关键帧时会用到。

**时间戳**以毫秒为单位，32 位（低 24 位在前，扩展的高 8 位在后，这个奇怪的顺序是历史原因：最早只有 24 位，最多表示 4.66 小时，后来不够用了才加上扩展字节）。

## 3. 解析 FLV

下面的程序依次解析文件头、脚本 tag 中的 onMetaData，以及每个音视频 tag 的头和数据的前几个字节，并检查每个 PreviousTagSize 是否正确：

```c
// flv_parse.c：解析 FLV 文件的头、onMetaData 和每一个 tag
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned be16(const unsigned char *p) { return p[0] << 8 | p[1]; }
static unsigned be24(const unsigned char *p) { return p[0] << 16 | p[1] << 8 | p[2]; }
static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// ---------- AMF0：只处理 onMetaData 中会出现的几种类型 ----------
static const unsigned char *amf_value(const unsigned char *p, const char *key)
{
    int type = *p++;
    if (type == 0) {                                   // Number：8 字节大端 double
        uint64_t bits = (uint64_t)be32(p) << 32 | be32(p + 4);
        double d;
        memcpy(&d, &bits, 8);
        printf("  %-16s = %.15g\n", key, d);
        return p + 8;
    }
    if (type == 1) {                                   // Boolean
        printf("  %-16s = %s\n", key, *p ? "true" : "false");
        return p + 1;
    }
    if (type == 2) {                                   // String：2 字节长度 + 内容
        unsigned len = be16(p);
        printf("  %-16s = \"%.*s\"\n", key, (int)len, p + 2);
        return p + 2 + len;
    }
    if (type == 8) {                                   // ECMA 数组：4 字节个数，然后是若干个「键 + 值」
        p += 4;
        while (!(p[0] == 0 && p[1] == 0 && p[2] == 9)) {   // 00 00 09 是结束标记
            char name[64];
            unsigned len = be16(p);
            snprintf(name, sizeof name, "%.*s", (int)len, p + 2);
            p = amf_value(p + 2 + len, name);
        }
        return p + 3;
    }
    printf("  %-16s = （不支持的 AMF 类型 %d）\n", key, type);
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 输入.flv [显示前几个 tag]\n", argv[0]); return 1; }
    int show = argc > 2 ? atoi(argv[2]) : 12;
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    // FLV 头：9 字节
    if (memcmp(buf, "FLV", 3) != 0) { fprintf(stderr, "不是 FLV 文件\n"); return 1; }
    printf("FLV 版本 %d，%s%s，头长 %u 字节\n", buf[3], buf[4] & 4 ? "有音频" : "", buf[4] & 1 ? " 有视频" : "", be32(buf + 5));

    long pos = be32(buf + 5) + 4;                      // 跳过 FLV 头和 PreviousTagSize0
    long count[32] = {0}, bytes[32] = {0}, n = 0;
    while (pos + 11 <= size) {
        const unsigned char *t = buf + pos;
        int type = t[0] & 0x1F;
        unsigned data_size = be24(t + 1);
        unsigned ts = be24(t + 4) | (unsigned)t[7] << 24;      // 低 24 位 + 扩展的高 8 位，单位毫秒
        const unsigned char *d = t + 11;
        if (type == 18) {                              // 脚本 tag
            printf("脚本 tag（偏移 %ld，%u 字节）：\n", pos, data_size);
            unsigned len = be16(d + 1);                // 第一个值是字符串 "onMetaData"
            printf("  %.*s:\n", (int)len, d + 3);
            amf_value(d + 3 + len, "");
        } else if (n < show) {
            printf("%7ld  %-4s %6u  %5u ms  ", pos, type == 8 ? "音频" : type == 9 ? "视频" : "?", data_size, ts);
            if (type == 9) {
                int frame_type = d[0] >> 4, codec = d[0] & 0xF, pkt = d[1];
                int cts = (int)(be24(d + 2) << 8) >> 8;    // 24 位有符号数
                printf("%s codec=%d %s cts=%d\n", frame_type == 1 ? "关键帧" : "非关键帧", codec,
                       pkt == 0 ? "AVC 序列头" : pkt == 1 ? "NALU" : "结束", cts);
            } else if (type == 8) {
                printf("format=%d rate=%d size=%d type=%d %s\n", d[0] >> 4, (d[0] >> 2) & 3, (d[0] >> 1) & 1,
                       d[0] & 1, d[1] == 0 ? "AAC 序列头" : "AAC 数据");
            } else printf("\n");
        }
        if (type != 18) n++;
        count[type]++; bytes[type] += data_size;
        unsigned prev = be32(t + 11 + data_size);          // 每个 tag 后面是它自己的总长度
        if (prev != 11 + data_size) { printf("偏移 %ld：PreviousTagSize 不对\n", pos); break; }
        pos += 11 + data_size + 4;
    }
    printf("...\n音频 tag %ld 个（%ld 字节），视频 tag %ld 个（%ld 字节），脚本 tag %ld 个\n",
           count[8], bytes[8], count[9], bytes[9], count[18]);
    printf("tag 头和 PreviousTagSize 共 %ld 字节，占文件的 %.2f%%\n",
           (count[8] + count[9] + count[18]) * 15, 100.0 * (count[8] + count[9] + count[18]) * 15 / size);
    free(buf);
    return 0;
}
```

```bash
clang -O2 -o flv_parse flv_parse.c
./flv_parse bbb.flv 12
```

```
FLV 版本 1，有音频 有视频，头长 9 字节
脚本 tag（偏移 13，371 字节）：
  onMetaData:
  duration         = 10.083
  width            = 1280
  height           = 720
  videodatarate    = 2208.40234375
  framerate        = 24
  videocodecid     = 7
  audiodatarate    = 124.71875
  audiosamplerate  = 48000
  audiosamplesize  = 16
  stereo           = true
  audiocodecid     = 10
  major_brand      = "isom"
  minor_version    = "512"
  compatible_brands = "isomiso2avc1mp41"
  encoder          = "Lavf63.1.102"
  filesize         = 3000038
    399  视频     55      0 ms  关键帧 codec=7 AVC 序列头 cts=0
    469  音频      7      0 ms  format=10 rate=3 size=1 type=1 AAC 序列头
    491  视频  96523      0 ms  关键帧 codec=7 NALU cts=83
  97029  视频   2281     41 ms  非关键帧 codec=7 NALU cts=167
  99325  音频    296     62 ms  format=10 rate=3 size=1 type=1 AAC 数据
  99636  视频    698     83 ms  非关键帧 codec=7 NALU cts=42
 100349  音频    314     83 ms  format=10 rate=3 size=1 type=1 AAC 数据
 100678  音频    280    104 ms  format=10 rate=3 size=1 type=1 AAC 数据
 100973  视频    502    125 ms  非关键帧 codec=7 NALU cts=41
 101490  音频    276    126 ms  format=10 rate=3 size=1 type=1 AAC 数据
 101781  音频    284    147 ms  format=10 rate=3 size=1 type=1 AAC 数据
 102080  视频   2238    166 ms  非关键帧 codec=7 NALU cts=167
...
音频 tag 471 个（160928 字节），视频 tag 242 个（2828016 字节），脚本 tag 1 个
tag 头和 PreviousTagSize 共 10710 字节，占文件的 0.36%
```

下面分别看三种 tag。

### 3.1 脚本 tag：onMetaData

脚本 tag 的数据用 **AMF0**（Action Message Format）编码，这是 Flash 中 ActionScript 的序列化格式：每个值以 1 字节的类型开头，0 是 64 位浮点数，1 是布尔值，2 是字符串（2 字节长度 + 内容），8 是 ECMA 数组（一组「名字 + 值」，以 `00 00 09` 结束）。onMetaData 就是一个字符串 `"onMetaData"` 加一个 ECMA 数组。

从 `xxd` 的输出中可以直接看到第一项：`00 08` 是名字的长度，`64 75 72 61 74 69 6F 6E` 是 "duration"，`00` 是类型（数字），后面 8 个字节 `40 24 2A 7E F9 DB 22 D1` 是 IEEE 754 双精度浮点数 10.083。

几个值得注意的地方：

- **数字一律是浮点数**：宽度 1280、文件大小 3000038 字节，都存成 double。
- **duration = 10.083**，比 10 秒多了 83 毫秒。第 1 章第 4 篇练习 2 讲过：FLV 的时间戳不能是负数，FFmpeg 把所有时间戳整体后移了 83 毫秒。
- **videocodecid = 7** 是 H.264（AVC），**audiocodecid = 10** 是 AAC。这两个编号来自 Flash 时代的定义，后面的 tag 中也用同样的编号。
- 后半部分是 FFmpeg 把 MP4 中的元数据原样搬了过来（`major_brand`、`encoder` 等）。

onMetaData 只是给播放器的参考信息，不影响解码。直播时推流软件在开始时写入 onMetaData，那时还不知道总时长，`duration` 通常是 0。

### 3.2 视频 tag

视频 tag 的数据前面还有几个字节的头：

| 字节 | 内容 |
|---|---|
| 第 1 字节 | 高 4 位：帧类型（1 关键帧，2 非关键帧）；低 4 位：编码 ID（7 = AVC） |
| 第 2 字节 | AVCPacketType：0 序列头，1 NAL 单元，2 序列结束 |
| 第 3～5 字节 | CompositionTime（cts）：24 位有符号数，单位毫秒 |
| 之后 | 序列头时是 AVCDecoderConfigurationRecord；否则是 AVCC 格式的 NAL 单元 |

**第一个视频 tag 是 AVC 序列头**，55 字节，内容就是上一篇第 5 节说的 `avcC`：档次、级别、NAL 长度字段占几个字节，以及 SPS 和 PPS。解码器必须先收到它才能解码后面的帧。直播中观众随时可能加入，所以 RTMP 服务器会把序列头缓存下来，每当有新观众连接，先发给他序列头，再发最近的关键帧。

**后面的视频 tag 每个是一帧**，数据是 AVCC 格式：每个 NAL 单元前面是 4 字节的长度。第一帧 96523 字节，比 MP4 中同一帧的 96518 字节多了 5 个字节，就是上表中的视频 tag 头。

**cts 就是 PTS − DTS**。tag 头中的时间戳是 **DTS**，加上 cts 才是 PTS：

| tag | 时间戳（DTS） | cts | PTS | 帧 |
|---|---|---|---|---|
| 第 1 帧 | 0 | 83 | 83 | I，显示顺序第 0 帧 |
| 第 2 帧 | 41 | 167 | 208 | P，显示顺序第 3 帧 |
| 第 3 帧 | 83 | 42 | 125 | B，显示顺序第 1 帧 |
| 第 4 帧 | 125 | 41 | 166 | B，显示顺序第 2 帧 |

和上一篇用 POC 推出来的顺序完全一致：码流中是 I、P、B、B，显示时是 I、B、B、P。这里也能看到毫秒精度的问题：每帧 41.67 毫秒，时间戳只能是 0、41、83、125、166……，间隔在 41 和 42 之间跳动。本章最后一篇会专门讨论时间戳。

### 3.3 音频 tag

音频 tag 的数据前面有 1 个字节的头（AAC 时还多 1 个字节）：

| 位 | 内容 | 测试视频 |
|---|---|---|
| 高 4 位 | 声音格式：2 MP3，10 AAC | 10 |
| 2 位 | 采样率：0 5.5 kHz，1 11 kHz，2 22 kHz，3 44 kHz | 3 |
| 1 位 | 采样位数：0 8 位，1 16 位 | 1 |
| 1 位 | 声道：0 单声道，1 立体声 | 1 |
| 第 2 字节（AAC） | AACPacketType：0 序列头，1 AAC 数据 | |

奇怪的是，采样率字段是 3（44 kHz），而测试视频是 48 kHz。这个 2 位的字段只能表示 4 种采样率，连 48 kHz 都没有，所以 FLV 规范规定：**AAC 的这个字段总是 3**，真正的参数在 AAC 序列头里。第一个音频 tag 就是 AAC 序列头，7 字节的数据中，后 2 字节 `11 90` 正是上一篇第 6 节的 AudioSpecificConfig。

后面每个音频 tag 是一个 AAC 帧，没有 ADTS 头：第一帧 296 字节，比 MP4 中的 294 字节多了 2 个字节的音频 tag 头。

### 3.4 统计

音频 tag 471 个（1 个序列头 + 470 帧），视频 tag 242 个（1 个序列头 + 240 帧 + 1 个序列结束）。所有 tag 头和 PreviousTagSize 一共 10710 字节，只占文件的 0.36%。加上 onMetaData、两个序列头，以及每个音视频 tag 数据前面那几个字节的头，FLV 中除了音视频数据本身，总共有 13.3 KB 的开销；MP4 的开销是 8 KB 左右（主要是下一篇要讲的索引），这就是第 1 章第 4 篇中 FLV 比 MP4 大了 5.3 KB 的原因。

## 4. 从 FLV 中取出音视频

第 1 章用 `ffmpeg -c copy` 把 MP4 中的流取出来存成裸流。现在自己来做：从 FLV 中取出 H.264 存成 Annex B 格式，取出 AAC 存成 ADTS 格式。这正是雷霄骅「最简单的视音频数据处理示例」中 FLV 分离器的现代版本，需要做两件事：

<figure class="diagram">
<svg viewBox="0 0 680 210" role="img" aria-label="格式转换：AVCC 格式的 NAL 单元前面是 4 字节长度，转换成 Annex B 时长度换成起始码 00 00 00 01，并在关键帧前插入序列头中的 SPS 和 PPS；AAC 数据前面加上根据 AudioSpecificConfig 生成的 7 字节 ADTS 头">
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="18" style="fill:var(--fg)">视频：AVCC → Annex B</text>
    <g transform="translate(10,28)">
      <rect x="0" y="0" width="70" height="28" style="fill:var(--border-2)"/><text x="6" y="18" style="fill:var(--fg);font-size:11px">00 01 76 51</text>
      <rect x="70" y="0" width="190" height="28" style="fill:var(--danger);opacity:.65"/><text x="120" y="18" style="fill:var(--bg)">IDR 片 95825</text>
      <text x="275" y="18" style="fill:var(--muted)">长度前缀（大端）</text>
    </g>
    <g transform="translate(10,68)">
      <rect x="0" y="0" width="70" height="28" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="6" y="18" style="fill:var(--muted);font-size:11px">00000001</text>
      <rect x="70" y="0" width="45" height="28" style="fill:var(--warn);opacity:.7"/><text x="78" y="18" style="fill:var(--bg)">SPS</text>
      <rect x="115" y="0" width="70" height="28" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="121" y="18" style="fill:var(--muted);font-size:11px">00000001</text>
      <rect x="185" y="0" width="40" height="28" style="fill:var(--warn);opacity:.7"/><text x="191" y="18" style="fill:var(--bg)">PPS</text>
      <rect x="225" y="0" width="70" height="28" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="231" y="18" style="fill:var(--muted);font-size:11px">00000001</text>
      <rect x="295" y="0" width="190" height="28" style="fill:var(--danger);opacity:.65"/><text x="345" y="18" style="fill:var(--bg)">IDR 片 95825</text>
      <text x="495" y="18" style="fill:var(--muted)">SPS/PPS 来自序列头</text>
    </g>
    <text x="10" y="128" style="fill:var(--fg)">音频：裸 AAC → ADTS</text>
    <g transform="translate(10,138)">
      <rect x="0" y="0" width="160" height="28" style="fill:var(--link);opacity:.6"/><text x="30" y="18" style="fill:var(--bg)">AAC 帧 294</text>
      <text x="175" y="18" style="fill:var(--muted)">→</text>
      <rect x="200" y="0" width="50" height="28" style="fill:var(--warn);opacity:.8"/><text x="205" y="18" style="fill:var(--bg);font-size:11px">ADTS 7</text>
      <rect x="250" y="0" width="160" height="28" style="fill:var(--link);opacity:.6"/><text x="280" y="18" style="fill:var(--bg)">AAC 帧 294</text>
      <text x="425" y="18" style="fill:var(--muted)">头由 11 90 推出</text>
    </g>
    <text x="10" y="196" style="fill:var(--muted);font-size:11px">AVCC 格式也用于 MP4 和 MKV；Annex B 用于裸流、TS 和 RTP</text>
  </g>
</svg>
</figure>

- **视频**：解析 AVC 序列头，取出 SPS、PPS 和「NAL 长度字段占几个字节」；之后每个视频 tag，把每个 NAL 单元前的长度换成起始码，关键帧前面再插入一份 SPS 和 PPS（Annex B 的习惯，否则只有文件开头有参数集，从中间开始就无法解码）。
- **音频**：解析 AAC 序列头（AudioSpecificConfig），得到档次、采样率索引、声道数；之后每个音频 tag，用上一篇练习 2 的函数生成 ADTS 头，加在 AAC 数据前面。

```c
// flv_extract.c：从 FLV 中取出 H.264（存成 Annex B）和 AAC（存成 ADTS）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned be16(const unsigned char *p) { return p[0] << 8 | p[1]; }
static unsigned be24(const unsigned char *p) { return p[0] << 16 | p[1] << 8 | p[2]; }
static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static const unsigned char start_code[4] = {0, 0, 0, 1};

// 上一篇练习 2 的函数：生成 7 字节的 ADTS 头
static void make_adts_header(unsigned char h[7], int profile, int rate_index, int channels, int payload_len)
{
    int len = payload_len + 7;
    h[0] = 0xFF;
    h[1] = 0xF1;
    h[2] = ((profile - 1) << 6) | (rate_index << 2) | (channels >> 2);
    h[3] = ((channels & 3) << 6) | (len >> 11);
    h[4] = (len >> 3) & 0xFF;
    h[5] = ((len & 7) << 5) | 0x1F;
    h[6] = 0xFC;
}

int main(int argc, char *argv[])
{
    if (argc != 4) { fprintf(stderr, "用法: %s 输入.flv 输出.h264 输出.aac\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);
    FILE *fv = fopen(argv[2], "wb"), *fa = fopen(argv[3], "wb");

    unsigned char sps[256], pps[256];
    unsigned sps_len = 0, pps_len = 0;
    int nal_len_size = 4, aot = 2, rate_index = 3, channels = 2;
    long video_frames = 0, audio_frames = 0;

    long pos = be32(buf + 5) + 4;
    while (pos + 11 <= size) {
        const unsigned char *t = buf + pos;
        int type = t[0] & 0x1F;
        unsigned data_size = be24(t + 1);
        const unsigned char *d = t + 11, *end = d + data_size;

        if (type == 9 && (d[0] & 0xF) == 7) {          // H.264 视频
            int keyframe = (d[0] >> 4) == 1;
            if (d[1] == 0) {
                // AVCDecoderConfigurationRecord：取出 NAL 长度字段的字节数、SPS 和 PPS
                const unsigned char *p = d + 5;
                nal_len_size = (p[4] & 3) + 1;
                int num_sps = p[5] & 0x1F;
                p += 6;
                for (int i = 0; i < num_sps; i++) {
                    sps_len = be16(p);
                    memcpy(sps, p + 2, sps_len);
                    p += 2 + sps_len;
                }
                int num_pps = *p++;
                for (int i = 0; i < num_pps; i++) {
                    pps_len = be16(p);
                    memcpy(pps, p + 2, pps_len);
                    p += 2 + pps_len;
                }
                printf("AVC 序列头：profile %d，level %d，NAL 长度 %d 字节，SPS %u 字节，PPS %u 字节\n",
                       d[6], d[8], nal_len_size, sps_len, pps_len);
            } else if (d[1] == 1) {
                const unsigned char *p = d + 5;            // 跳过 1 字节帧信息、1 字节包类型、3 字节 cts
                int first = 1;
                while (p + nal_len_size <= end) {
                    unsigned len = 0;
                    for (int i = 0; i < nal_len_size; i++) len = len << 8 | p[i];   // 长度前缀，大端
                    p += nal_len_size;
                    int nal_type = p[0] & 0x1F;
                    if (first && keyframe && nal_type != 7) {   // 关键帧前插入 SPS、PPS
                        fwrite(start_code, 1, 4, fv); fwrite(sps, 1, sps_len, fv);
                        fwrite(start_code, 1, 4, fv); fwrite(pps, 1, pps_len, fv);
                    }
                    fwrite(start_code, 1, 4, fv);               // 长度换成起始码
                    fwrite(p, 1, len, fv);
                    p += len;
                    first = 0;
                }
                video_frames++;
            }
        } else if (type == 8 && (d[0] >> 4) == 10) {   // AAC 音频
            if (d[1] == 0) {
                // AudioSpecificConfig：5 位 Audio Object Type，4 位采样率索引，4 位声道配置
                aot = d[2] >> 3;
                rate_index = ((d[2] & 7) << 1) | (d[3] >> 7);
                channels = (d[3] >> 3) & 0xF;
                printf("AAC 序列头：%02X %02X → Audio Object Type %d，采样率索引 %d，声道 %d\n",
                       d[2], d[3], aot, rate_index, channels);
            } else {
                unsigned char h[7];
                make_adts_header(h, aot, rate_index, channels, data_size - 2);
                fwrite(h, 1, 7, fa);
                fwrite(d + 2, 1, data_size - 2, fa);
                audio_frames++;
            }
        }
        pos += 11 + data_size + 4;
    }
    printf("写出视频 %ld 帧，音频 %ld 帧\n", video_frames, audio_frames);
    fclose(fv); fclose(fa); free(buf);
    return 0;
}
```

AVCDecoderConfigurationRecord 的格式是：1 字节版本、3 字节档次信息（profile、兼容性、level）、1 字节（低 2 位是「NAL 长度字段字节数 − 1」）、1 字节（低 5 位是 SPS 个数），然后每个 SPS 是 2 字节长度加内容，PPS 同理。

```bash
clang -O2 -o flv_extract flv_extract.c
./flv_extract bbb.flv out.h264 out.aac
```

```
AVC 序列头：profile 100，level 31，NAL 长度 4 字节，SPS 29 字节，PPS 6 字节
AAC 序列头：11 90 → Audio Object Type 2，采样率索引 3，声道 2
写出视频 240 帧，音频 470 帧
```

和第 1 章用 FFmpeg 从 MP4 中取出的 `bbb.aac`、`bbb.h264` 比较：

```bash
md5 -q out.aac bbb.aac
ls -l out.h264 bbb.h264
ffmpeg -v error -i out.h264 -f md5 -
ffmpeg -v error -i bbb.h264 -f md5 -
```

```
78fac3d4cfa9471847b8158b7ab10a95
78fac3d4cfa9471847b8158b7ab10a95
-rw-r--r--  1 lucky  staff  2827057 Oct 31 11:00 out.h264
-rw-r--r--  1 lucky  staff  2827050 Oct 31 11:00 bbb.h264
MD5=20d9118c9b177896234cec92e2b4e86e
MD5=20d9118c9b177896234cec92e2b4e86e
```

- **AAC 逐字节相同**。（这个 MD5 也和第 1 章第 4 篇中 TS 里音频流的哈希值一样：TS 中的 AAC 带的也是 ADTS 头。）
- **H.264 文件大了 7 个字节**，但解码出来的 240 帧画面完全相同（`-f md5` 计算的是解码后所有像素的哈希值）。区别在起始码：FFmpeg 在 IDR 片前用 3 字节起始码（上一篇第 2 节说过，它前面已经有 SPS、PPS，不是这一帧的第一个 NAL 单元），而我们一律用 4 字节，7 个 IDR 正好多 7 个字节。两种写法都符合标准。

## 5. 小结

- FLV = 9 字节文件头 + 一串 tag；每个 tag = 11 字节的头 + 数据，后面跟 4 字节的 PreviousTagSize。
- tag 有三种：音频（8）、视频（9）、脚本（18）。时间戳是 32 位毫秒，视频 tag 的时间戳是 DTS，PTS = DTS + cts。
- onMetaData 用 AMF0 编码，记录时长、分辨率、码率等参考信息。
- 视频和音频的第一个 tag 是序列头：AVCDecoderConfigurationRecord（SPS、PPS）和 AudioSpecificConfig。直播服务器会缓存序列头，发给每个新加入的观众。
- FLV 中的 H.264 是 AVCC 格式，AAC 没有 ADTS 头；提取时要转换成 Annex B 和 ADTS。
- FLV 结构简单、可以边产生边传输，所以至今仍是 RTMP 推流和 HTTP-FLV 直播的格式。

下一篇解析结构复杂得多、但也用得最广的 MP4。

## 练习

1. 写一个程序，从 FLV 文件的**末尾**开始，利用 PreviousTagSize 从后往前遍历所有 tag，验证得到的 tag 个数和从前往后相同。
2. 修改 `flv_parse.c`，统计相邻两个视频 tag 的时间戳之差，分别有哪几种值，各出现多少次？
3. 用 `ffmpeg -i bbb-10s.mp4 -t 1 -c:v libx265 -c:a copy hevc.flv` 生成一个 H.265 的 FLV，用 `flv_parse` 解析，视频 tag 的输出有什么问题？用 `xxd` 看看视频 tag 数据的前几个字节，和 H.264 时有什么不同？
4. 修改 `flv_extract.c`，不在关键帧前插入 SPS 和 PPS，只在文件开头写一次。用 `ffmpeg -ss 5 -i out.h264` 从中间开始解码，会怎样？（提示：裸流没有索引，`-ss` 是从头解码再丢弃前面的帧。）换一种方法：用 `tail -c 1000000 out.h264 > tail.h264` 截取后半部分再解码呢？

<details>
<summary>点击查看答案</summary>

1. 从文件末尾读 4 个字节得到最后一个 tag 的总长度 L，这个 tag 从 `pos - 4 - L` 开始；读出它的类型，再从它前面的 4 个字节读出前一个 tag 的长度，依此类推，直到到达文件头（偏移 13）：

   ```c
   long pos = size, n = 0;                      // pos 指向某个 PreviousTagSize 之后
   while (pos > 13) {
       unsigned prev = be32(buf + pos - 4);     // 前一个 tag 的总长度
       long tag = pos - 4 - prev;               // 前一个 tag 的开头
       n++;
       pos = tag;
   }
   printf("从后往前共 %ld 个 tag，回到偏移 %ld\n", n, pos);
   ```

   输出 714 个 tag（471 个音频 + 242 个视频 + 1 个脚本），最后停在偏移 13，正好是第一个 tag 的位置，和从前往后的统计一致。如果某个 tag 损坏、长度不对，从两个方向遍历得到的结果就会不同，这也可以用来检查文件是否完整。

2. 记下上一个视频 tag 的时间戳，计算差值后用一个小数组计数。跳过序列头和序列结束两个 tag 后，239 个差值中只有两种：42 出现 159 次，41 出现 80 次，正好 2 比 1（41.67 × 3 = 125 = 42 + 42 + 41），累计下来正好是 24 帧/秒。如果每帧都按 41 毫秒算，一分钟（1440 帧）后会慢出 0.96 秒；按 42 毫秒算会快出 0.48 秒，音画明显不同步。所以写 FLV 时时间戳必须由精确的时间换算得来，不能用固定的增量累加，下一篇的 MP4 和本章最后一篇会再讨论。

3. `flv_parse` 的输出：

   ```
       399  视频   2476      0 ms  非关键帧 codec=0 结束 cts=7758641
      2912  视频    126      0 ms  非关键帧 codec=4 结束 cts=7758641
      3053  视频  50728      0 ms  非关键帧 codec=1 结束 cts=7758641
   ```

   帧类型、编码 ID、包类型全都不对。看第一个视频 tag 的数据：

   ```
   90 68 76 63 31 01 01 60 ...
   ```

   第一个字节 `0x90` = `1001 0000`：最高位是 1，这是 2023 年发布的 **Enhanced RTMP / FLV** 规范新增的「扩展头」标志。之后 3 位是帧类型（001，关键帧），低 4 位不再是编码 ID，而是包类型（0 = 序列开始）；接下来 4 个字节 `68 76 63 31` 是 FourCC "hvc1"，表示 H.265。原来的 4 位编码 ID 最多只能表示 16 种编码，早已用完，新规范改用 FourCC，这样 H.265、AV1、VP9 也能放进 FLV 和 RTMP 了。我们的程序按旧格式解释，把 `0x9` 当成帧类型、`0x0` 当成编码 ID，所以全错了。

4. 只在开头写一次参数集：`ffmpeg -ss 5 -i out.h264` 仍然能正常解码，因为对裸流来说 `-ss` 是从文件开头解码，再丢弃 5 秒之前的帧，开头的 SPS、PPS 已经读到了。但 `tail -c 1000000` 截取的后半部分中没有任何 SPS、PPS，FFmpeg 会报出大量 `non-existing PPS 0 referenced` 错误，一帧也解不出来。这就是 Annex B 裸流、TS 流以及直播流中参数集要随 IDR 重复的原因：接收方可能从任何位置开始收数据。插入了参数集的 `out.h264` 截取同样的后半部分，就能从截取后遇到的第一个 IDR（第 227 帧）开始正常解码，解出到结尾的 13 帧（IDR 之前的那些帧缺少参考帧，被丢弃了）。

</details>
