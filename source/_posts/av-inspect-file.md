---
title: 看懂一个视频文件
date: 2026-10-28 12:00:00
updated: 2026-10-28 12:00:00
categories:
  - 音视频开发
  - 音视频基础
tags:
  - 音视频
  - FFmpeg
  - 封装格式
  - C
order: 4
description: 视频文件是一个容器，里面装着一条 H.264 视频流和一条 AAC 音频流。用 ffprobe 逐项读懂测试视频的信息：编码、分辨率、像素格式、时间基、码率。把同样的两条流不重新编码装进 MP4、MKV、FLV、TS 四种容器，只用 0.02 秒；前三种的流哈希值完全相同，TS 却不同，原因是每个视频包多了 6 个字节、每个音频包多了 7 个字节；把 .mp4 改名成 .avi，播放器照样能放。用 C 写一个只看文件开头几个字节的格式识别程序。最后看看文件中音频包和视频包怎样交错排列，以及为什么 PTS 和 DTS 不一样。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：分清封装格式（容器）和编码格式；会用 `ffprobe` 查看一个媒体文件的流、编码、分辨率、码率、时长等信息；理解流、包（packet）、帧（frame）的关系；理解时间基和 PTS、DTS；知道常见封装格式的特点和用途。
>
> **前置知识**：前三篇《像素与 YUV》《音频与 PCM》《为什么必须压缩》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0。

## 1. 容器和编码

上一篇把视频和音频分别压缩成了 H.264 和 AAC，它们是两条独立的数据流。一个视频文件要做的，是把它们装在一起，并记下播放需要的信息：每一帧什么时候显示、视频多长、有几条流、每条流是什么编码……这个「装」的格式就叫**封装格式**，也叫**容器**（container）。

<figure class="diagram">
<svg viewBox="0 0 680 220" role="img" aria-label="封装格式示意图：MP4 文件是一个容器，里面有文件头信息、一条 H.264 视频流和一条 AAC 音频流，两条流的数据包交错存放">
  <g style="font-family:var(--mono);font-size:12px">
    <rect x="10" y="10" width="660" height="200" rx="10" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="24" y="34" style="fill:var(--fg)">bbb-10s.mp4（封装格式：MP4）</text>
    <rect x="24" y="48" width="200" height="60" rx="6" style="fill:var(--mark);stroke:var(--border-2)"/>
    <text x="36" y="70" style="fill:var(--fg)">头信息</text>
    <text x="36" y="90" style="fill:var(--muted);font-size:11px">时长、流的列表、索引</text>
    <rect x="240" y="48" width="200" height="28" rx="4" style="fill:var(--accent);opacity:.75"/>
    <text x="250" y="67" style="fill:var(--bg)">流 0：视频 H.264</text>
    <rect x="240" y="80" width="200" height="28" rx="4" style="fill:var(--warn);opacity:.75"/>
    <text x="250" y="99" style="fill:var(--bg)">流 1：音频 AAC</text>
    <text x="458" y="70" style="fill:var(--muted);font-size:11px">编码格式：怎样压缩</text>
    <text x="458" y="90" style="fill:var(--muted);font-size:11px">封装格式：怎样装起来</text>
    <text x="24" y="138" style="fill:var(--fg)">数据区：两条流的包交错存放</text>
    <g transform="translate(24,150)">
      <rect x="0" y="0" width="70" height="36" style="fill:var(--accent);opacity:.75"/>
      <rect x="72" y="0" width="22" height="36" style="fill:var(--warn);opacity:.75"/>
      <rect x="96" y="0" width="22" height="36" style="fill:var(--warn);opacity:.75"/>
      <rect x="120" y="0" width="30" height="36" style="fill:var(--accent);opacity:.75"/>
      <rect x="152" y="0" width="22" height="36" style="fill:var(--warn);opacity:.75"/>
      <rect x="176" y="0" width="16" height="36" style="fill:var(--accent);opacity:.75"/>
      <rect x="194" y="0" width="22" height="36" style="fill:var(--warn);opacity:.75"/>
      <rect x="218" y="0" width="18" height="36" style="fill:var(--accent);opacity:.75"/>
      <rect x="238" y="0" width="22" height="36" style="fill:var(--warn);opacity:.75"/>
      <rect x="262" y="0" width="30" height="36" style="fill:var(--accent);opacity:.75"/>
      <rect x="294" y="0" width="22" height="36" style="fill:var(--warn);opacity:.75"/>
      <rect x="318" y="0" width="22" height="36" style="fill:var(--warn);opacity:.75"/>
      <rect x="342" y="0" width="16" height="36" style="fill:var(--accent);opacity:.75"/>
      <rect x="360" y="0" width="270" height="36" style="fill:var(--panel);stroke:var(--border-2)"/>
      <text x="22" y="23" style="fill:var(--bg)">I</text><text x="130" y="23" style="fill:var(--bg)">P</text>
      <text x="380" y="23" style="fill:var(--muted)">…… 每个方块是一个包（packet）</text>
    </g>
  </g>
</svg>
</figure>

要分清两个概念：

- **编码格式**（codec）：数据是**怎样压缩**的。视频有 H.264、H.265（HEVC）、AV1、VP9，音频有 AAC、MP3、Opus。
- **封装格式**（format / container）：压缩好的数据是**怎样装起来**的。有 MP4、MKV、FLV、TS、AVI、WebM。

文件的扩展名一般表示封装格式。同一种编码可以装进不同的容器，同一种容器也可以装不同的编码：MP4 里可以是 H.264 也可以是 H.265，H.264 可以装在 MP4 里也可以装在 MKV 里。所以「这是一个 MP4 视频」并不能说明它是什么编码，有的播放器能放一个 MP4 却放不了另一个，往往是因为里面的编码不同。

播放一个文件的过程，就是把这个结构一层层拆开：

<figure class="diagram">
<svg viewBox="0 0 680 140" role="img" aria-label="播放流程：读取文件，解封装分出视频包和音频包，分别解码成 YUV 和 PCM，同步后送到屏幕和声卡">
  <defs>
    <marker id="pl-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="48" width="80" height="40" rx="6" style="fill:var(--panel-2)"/>
      <rect x="115" y="48" width="90" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="250" y="10" width="110" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="250" y="86" width="110" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="405" y="48" width="100" height="40" rx="6" style="fill:var(--mark)"/>
      <rect x="545" y="10" width="110" height="40" rx="6" style="fill:var(--panel-2)"/>
      <rect x="545" y="86" width="110" height="40" rx="6" style="fill:var(--panel-2)"/>
    </g>
    <text x="22" y="73" style="fill:var(--fg)">文件</text>
    <text x="130" y="73" style="fill:var(--fg)">解封装</text>
    <text x="262" y="35" style="fill:var(--fg)">视频解码</text>
    <text x="262" y="111" style="fill:var(--fg)">音频解码</text>
    <text x="418" y="73" style="fill:var(--fg)">音视频同步</text>
    <text x="580" y="35" style="fill:var(--fg)">屏幕</text>
    <text x="580" y="111" style="fill:var(--fg)">声卡</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#pl-arrow)">
      <line x1="85" y1="68" x2="113" y2="68"/>
      <path d="M205,62 L228,30 L248,30"/><path d="M205,74 L228,106 L248,106"/>
      <path d="M360,30 L385,30 L403,60"/><path d="M360,106 L385,106 L403,76"/>
      <path d="M505,60 L525,30 L543,30"/><path d="M505,76 L525,106 L543,106"/>
    </g>
    <text x="208" y="20" style="fill:var(--muted);font-size:10px">H.264 包</text>
    <text x="208" y="128" style="fill:var(--muted);font-size:10px">AAC 包</text>
    <text x="364" y="20" style="fill:var(--muted);font-size:10px">YUV</text>
    <text x="364" y="128" style="fill:var(--muted);font-size:10px">PCM</text>
  </g>
</svg>
</figure>

这张图就是整个专栏的路线图：第 2 章拆开封装格式和码流，第 3 章用 FFmpeg 做解封装和解码，第 4 章把它们拼成一个播放器。

## 2. 用 ffprobe 查看文件信息

`ffprobe` 是 FFmpeg 自带的分析工具。最简单的用法是直接跟文件名：

```bash
ffprobe bbb-10s.mp4
```

```
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'bbb-10s.mp4':
  Metadata:
    major_brand     : isom
    minor_version   : 512
    compatible_brands: isomiso2avc1mp41
    encoder         : Lavf63.1.102
  Duration: 00:00:10.00, start: 0.000000, bitrate: 2395 kb/s
  Stream #0:0[0x1](und): Video: h264 (High) (avc1 / 0x31637661), yuv420p(tv, bt709, progressive), 1280x720, 2261 kb/s, 24 fps, 24 tbr, 12288 tbn (default)
    Metadata:
      handler_name    : VideoHandler
      encoder         : Lavc63.1.102 libx264
  Stream #0:1[0x2](und): Audio: aac (LC) (mp4a / 0x6134706D), 48000 Hz, stereo, fltp, 127 kb/s (default)
    Metadata:
      handler_name    : SoundHandler
```

第一行 `Input #0, mov,mp4,m4a,3gp,3g2,mj2` 是 FFmpeg 识别出的封装格式。MP4 和苹果的 MOV 同源，FFmpeg 用同一个模块处理它们。往下依次是：

- **Metadata**：容器级别的元数据。`major_brand: isom` 是 MP4 文件开头 `ftyp` 中的品牌，`encoder` 说明这个文件是 FFmpeg 的格式库 libavformat（Lavf）写的。
- **Duration: 00:00:10.00**：时长 10 秒；`start: 0.000000` 是第一个时间戳；`bitrate: 2395 kb/s` 是**整个文件**的平均码率：文件大小 × 8 / 时长 = 2994711 × 8 / 10 ≈ 2396 kbps，包括了视频、音频和容器本身的开销。
- **Stream #0:0**：第 0 个输入文件的第 0 条流，视频。
  - `h264 (High)`：编码是 H.264，档次 High；`avc1` 是这种编码在 MP4 中的标识。
  - `yuv420p(tv, bt709, progressive)`：解码后是 YUV420P，有限范围，BT.709，逐行扫描。第 1 篇的知识全在这一行里。
  - `1280x720, 2261 kb/s, 24 fps`：分辨率、视频码率、帧率。
  - `12288 tbn`：时间基的倒数，见第 3 节。
- **Stream #0:1**：音频流。`aac (LC)` 是 AAC 的 LC（低复杂度）档次，最常用的一种；`48000 Hz, stereo, fltp` 是采样率、立体声、平面浮点格式（解码后的 PCM 格式，第 2 篇讲过）；码率 127 kb/s。

视频码率 2261 + 音频 127 = 2388 kbps，比整个文件的 2395 kbps 少了 7 kbps，这部分就是容器自身的开销，约占 0.3%。

### 2.1 更详细的信息

加上 `-show_streams` 可以看到每条流的全部字段。字段很多，只挑视频流中重要的几个：

```bash
ffprobe -v error -select_streams v:0 -show_entries \
  stream=codec_name,profile,level,width,height,pix_fmt,color_range,color_space,r_frame_rate,avg_frame_rate,time_base,nb_frames,has_b_frames,bit_rate \
  -of default=nw=1 bbb-10s.mp4
```

```
codec_name=h264
profile=High
width=1280
height=720
has_b_frames=2
pix_fmt=yuv420p
level=31
color_range=tv
color_space=bt709
r_frame_rate=24/1
avg_frame_rate=24/1
time_base=1/12288
bit_rate=2261404
nb_frames=240
```

| 字段 | 含义 |
|---|---|
| `profile`、`level` | H.264 的**档次**和**级别**：档次决定能用哪些压缩工具（High 比 Baseline 多了 B 帧、CABAC 等），级别限制了分辨率和码率的上限。老设备或者硬件解码器可能不支持高档次 |
| `pix_fmt` | 解码后的像素格式，`yuv420p` 就是第 1 篇的 I420 |
| `color_range=tv` | 有限范围（16～235），第 1 篇讲过 |
| `color_space=bt709` | 用 BT.709 的公式转 RGB |
| `r_frame_rate` | 帧率，用分数表示：24/1。有些视频是 24000/1001 ≈ 23.976，这是从 NTSC 电视继承下来的 |
| `time_base` | **时间基**，下一节详细讲 |
| `has_b_frames` | 有 B 帧，解码顺序和显示顺序不同 |

音频流：

```bash
ffprobe -v error -select_streams a:0 -show_entries \
  stream=codec_name,profile,sample_fmt,sample_rate,channels,channel_layout,time_base,nb_frames,bit_rate \
  -of default=nw=1 bbb-10s.mp4
```

```
codec_name=aac
profile=LC
sample_fmt=fltp
sample_rate=48000
channels=2
channel_layout=stereo
time_base=1/48000
bit_rate=127712
nb_frames=470
```

AAC 解码后的格式是 `fltp`（平面浮点），所以第 2 篇取 PCM 时要用 `-f s16le` 让 FFmpeg 转换一下。`nb_frames=470` 是 AAC 帧（包）的个数：每个 AAC 帧固定包含 **1024 个采样**，10 秒 × 48000 / 1024 = 468.75，向上取整是 469，再加上编码器开头多出来的 1 帧（下一节会看到），正好 470。

> 另一个常用的工具是 **MediaInfo**（`brew install mediainfo`，Windows 上有图形界面版本），显示的信息更适合人读，例如会直接写出「Format profile: High@L3.1」「Writing library: x264 core 165」。

## 3. 时间基与时间戳

### 3.1 时间基

视频中的每一帧都有一个时间戳，表示它应该在什么时候显示。时间戳不是用秒表示的，而是一个整数，乘以**时间基**（time base）才是秒：

```
时间（秒）= 时间戳 × 时间基
```

测试视频的视频流时间基是 1/12288，即一秒分成 12288 份。每帧间隔 1/24 秒，时间戳就每帧增加 12288 / 24 = 512。音频流的时间基是 1/48000，正好等于采样率，时间戳就是「第几个采样」。

为什么不直接用秒？因为浮点数有误差，累加几小时后会出现偏差；而整数时间戳可以精确表示每一帧的时刻。不同的封装格式有自己习惯的时间基，FLV 固定是 1/1000（毫秒），MPEG-TS 固定是 1/90000；MP4 和 MKV 可以自定义。在不同容器之间转换时，时间戳要按时间基换算，第 3 章写代码时会处理这件事。

### 3.2 包：PTS 与 DTS

文件中的数据以**包**（packet）为单位存放：视频一个包通常是一帧压缩数据，音频一个包是一个 AAC 帧（1024 个采样）。用 `-show_packets` 列出前 16 个包：

```bash
ffprobe -v error -show_entries packet=stream_index,pts,dts,pts_time,size,flags \
  -of compact=p=0:nk=0 bbb-10s.mp4 | head -16
```

```
stream_index=0|pts=0|pts_time=0.000000|dts=-1024|size=96518|flags=K__
stream_index=0|pts=1536|pts_time=0.125000|dts=-512|size=2276|flags=___
stream_index=1|pts=-1024|pts_time=-0.021333|dts=-1024|size=294|flags=KD_|
stream_index=0|pts=512|pts_time=0.041667|dts=0|size=693|flags=___
stream_index=1|pts=0|pts_time=0.000000|dts=0|size=312|flags=K__
stream_index=1|pts=1024|pts_time=0.021333|dts=1024|size=278|flags=K__
stream_index=0|pts=1024|pts_time=0.083333|dts=512|size=497|flags=___
stream_index=1|pts=2048|pts_time=0.042667|dts=2048|size=274|flags=K__
stream_index=1|pts=3072|pts_time=0.064000|dts=3072|size=282|flags=K__
stream_index=0|pts=3072|pts_time=0.250000|dts=1024|size=2233|flags=___
stream_index=1|pts=4096|pts_time=0.085333|dts=4096|size=291|flags=K__
stream_index=1|pts=5120|pts_time=0.106667|dts=5120|size=318|flags=K__
stream_index=0|pts=2048|pts_time=0.166667|dts=1536|size=650|flags=___
stream_index=1|pts=6144|pts_time=0.128000|dts=6144|size=314|flags=K__
stream_index=1|pts=7168|pts_time=0.149333|dts=7168|size=311|flags=K__
stream_index=0|pts=2560|pts_time=0.208333|dts=2048|size=495|flags=___
```

几个观察：

- **音视频交错存放**。视频包（`stream_index=0`）和音频包（`stream_index=1`）大致按时间顺序穿插排列，播放时顺序读文件，就能同时拿到差不多时刻的视频和音频，不需要在文件中来回跳。
- **视频包的 PTS 不是递增的**。第一个视频包 PTS 是 0（第 0 帧），第二个是 1536（1536 / 512 = 第 3 帧），然后才是 512、1024（第 1、2 帧）。这正是上一篇的 I B B P 结构：P 帧（第 3 帧）被两个 B 帧参考，必须先解码，所以在文件中排在它们前面。
- **DTS 是递增的**：−1024、−512、0、512……每个包加 512。**DTS**（Decoding Time Stamp）表示解码顺序，**PTS**（Presentation Time Stamp）表示显示顺序。没有 B 帧时两者相同；有 B 帧时，包按 DTS 的顺序存放和解码，解码出来的帧再按 PTS 排序后显示。DTS 从负数开始，是为了保证每一帧的 DTS 都不大于 PTS（必须先解码才能显示）。
- **第一个视频包是关键帧**：`flags=K__` 中的 K 表示 keyframe，也就是 I 帧，seek 时只能从这样的包开始解码。
- **音频包的 PTS 每次加 1024**：时间基是 1/48000，1024 就是 1024 个采样，约 21.3 毫秒。第一个音频包 PTS 是 −1024，标志中的 D 表示 discard：AAC 编码器开头需要 1024 个采样的「预热」，解码出来的这一帧应该丢掉。这就是上面多出来的那 1 帧。

## 4. 换个容器，内容一样吗

### 4.1 转封装

既然编码和封装是两回事，就可以只换容器、不动里面的数据。`-c copy` 表示直接复制流，不解码也不重新编码：

```bash
ffmpeg -i bbb-10s.mp4 -c copy bbb.mkv
ffmpeg -i bbb-10s.mp4 -c copy bbb.flv
ffmpeg -i bbb-10s.mp4 -c copy bbb.ts
ls -l bbb-10s.mp4 bbb.mkv bbb.flv bbb.ts
```

```
-rw-r--r--  1 lucky  staff  2994711 Oct 28 12:00 bbb-10s.mp4
-rw-r--r--  1 lucky  staff  3000038 Oct 28 12:00 bbb.flv
-rw-r--r--  1 lucky  staff  2992938 Oct 28 12:00 bbb.mkv
-rw-r--r--  1 lucky  staff  3128696 Oct 28 12:00 bbb.ts
```

每个命令只用了 0.02～0.03 秒。转封装不解码、不编码，只是把包从一种结构搬到另一种结构，所以和文件的读写速度差不多快，画质也没有任何损失。相比之下，上一篇用 x264 重新编码这段视频，最快的 preset 也要 0.1 秒，`medium` 要 0.65 秒，而且每编码一次画质都会再损失一点。**能转封装解决的问题，就不要重新编码。**

MKV 比 MP4 还小 1.7 KB，FLV 大了 5.3 KB，TS 大了 134 KB（4.5%）。

四个文件大小不同。里面的流是不是真的没变？FFmpeg 的 `streamhash` 可以对每条流的压缩数据计算哈希值：

```bash
for f in bbb-10s.mp4 bbb.mkv bbb.flv bbb.ts; do
  echo "== $f"; ffmpeg -v error -i $f -map 0 -c copy -f streamhash -hash md5 -
done
```

```
== bbb-10s.mp4
0,v,MD5=b4702ed77eebc7ab12f8fa426267d636
1,a,MD5=8485f1ce251d0748bfb841ebf05c007a
== bbb.mkv
0,v,MD5=b4702ed77eebc7ab12f8fa426267d636
1,a,MD5=8485f1ce251d0748bfb841ebf05c007a
== bbb.flv
0,v,MD5=b4702ed77eebc7ab12f8fa426267d636
1,a,MD5=8485f1ce251d0748bfb841ebf05c007a
== bbb.ts
0,v,MD5=510f02c34884fe4a3e8d0a390f5820a3
1,a,MD5=78fac3d4cfa9471847b8158b7ab10a95
```

MP4、MKV、FLV 中的两条流一个字节都没变。TS 的哈希值却不同，难道 `-c copy` 改了数据？比较一下每个包的大小：

```bash
ffprobe -v error -select_streams v -show_entries packet=size -of csv=p=0 bbb-10s.mp4 | head -3
ffprobe -v error -select_streams v -show_entries packet=size -of csv=p=0 bbb.ts | head -3
```

```
96518
2276
693
96566
2282
699
```

TS 中的每个视频包都比 MP4 多了 6 个字节（第一个多了 48 字节），音频包则每个多 7 个字节。这是因为 H.264 和 AAC 的数据在不同容器中有不同的「包装方式」：

- MP4、MKV、FLV 中，H.264 的每个 NAL 单元（码流的基本单位）前面是 4 字节的**长度**；TS 中则是 4 字节的**起始码** `00 00 00 01`，并且每帧前面要加一个 6 字节的「访问单元分隔符」（AUD）`00 00 00 01 09 F0`，关键帧前还要重复一遍 SPS、PPS 参数集，所以第一个包多了 48 字节。
- AAC 在 MP4 中是裸的数据，解码参数写在容器的头信息中；TS 中每个 AAC 帧前面要加一个 7 字节的 **ADTS 头**，自带采样率、声道数等参数。

去掉这些区别后再比较：把 MP4 中的视频转成起始码格式，把 TS 中的 AUD 去掉，两者的哈希值就一样了：

```bash
ffmpeg -v error -i bbb-10s.mp4 -c copy -bsf:v h264_mp4toannexb -f streamhash -hash md5 -
ffmpeg -v error -i bbb.ts -c copy -bsf:v h264_metadata=aud=remove -f streamhash -hash md5 -
```

```
0,v,MD5=8150d3de9d75b1f5d3d851efb3dacd56
1,a,MD5=8485f1ce251d0748bfb841ebf05c007a
0,v,MD5=8150d3de9d75b1f5d3d851efb3dacd56
1,a,MD5=78fac3d4cfa9471847b8158b7ab10a95
```

视频流一致了（音频的 ADTS 头没有去掉，所以还不一样）。压缩后的图像数据确实一点没变，变的只是外面的包装。`-bsf` 指定的是**码流过滤器**（bitstream filter），专门做这种包装格式的转换。起始码、NAL 单元、ADTS 头这些概念，第 2 章手写解析器时会一个字节一个字节地看。

### 4.2 常见的封装格式

| 格式 | 扩展名 | 特点 | 常见用途 |
|---|---|---|---|
| MP4 | .mp4 .m4a .mov | 索引集中存放在 moov 中，支持随机访问；兼容性最好 | 视频文件、点播、手机拍摄 |
| MKV | .mkv .webm | 几乎能装任何编码，支持多音轨、多字幕 | 电影资源、WebM 网页视频 |
| FLV | .flv | 结构简单，一个 tag 接一个 tag，适合流式传输 | RTMP 直播推流 |
| MPEG-TS | .ts | 固定 188 字节的小包，从任意位置都能开始解析，抗丢包 | 数字电视广播、HLS 直播 |
| AVI | .avi | 微软早期的格式，对 B 帧支持不好 | 老视频 |

（第 2 章会逐个手写这些格式的解析器。）

TS 文件最大，因为每 188 字节就有 4 字节的包头，音视频数据还要再套一层 PES 头，并且要周期性地插入节目表。这些冗余换来的是：TS 流从中间任何位置开始都能解析，丢了一部分数据也不影响后面，适合广播和直播。MP4 恰好相反：所有帧的位置和时间都集中写在 `moov` 中，开销最小，但缺了 `moov` 整个文件就打不开。

> 测试视频编码时加了 `-movflags +faststart`。默认情况下，MP4 的 `moov` 写在文件**末尾**（编码完成才知道所有帧的位置），网页播放时必须下载完整个文件才能开始播放；`faststart` 编码完成后把 `moov` 挪到文件开头，就可以边下载边播放。

## 5. 扩展名不等于格式

### 5.1 改扩展名

把 MP4 文件改名成 `.avi`：

```bash
cp bbb-10s.mp4 fake.avi
ffprobe -v error -show_entries format=format_name -of default=nw=1 fake.avi
```

```
format_name=mov,mp4,m4a,3gp,3g2,mj2
```

FFmpeg 照样认出它是 MP4，正常播放。播放器判断格式时，主要看的不是扩展名，而是文件开头的内容：大多数格式在开头都有固定的「魔数」（magic number）。FFmpeg 会让每个解封装器检查文件开头，给出一个可能性的分数，选分数最高的。

### 5.2 自己写一个格式识别程序

只读每个文件开头的 16 个字节，根据魔数判断格式：

```c
// whatfmt.c：读文件开头的几个字节，判断真正的封装格式
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    for (int k = 1; k < argc; k++) {
        unsigned char b[16] = {0};
        FILE *f = fopen(argv[k], "rb");
        if (!f) { perror(argv[k]); continue; }
        size_t n = fread(b, 1, sizeof b, f);
        fclose(f);

        const char *fmt = "未知";
        if (n >= 12 && memcmp(b + 4, "ftyp", 4) == 0)           fmt = "MP4 / MOV（ISO BMFF）";
        else if (n >= 3 && memcmp(b, "FLV", 3) == 0)            fmt = "FLV";
        else if (n >= 4 && memcmp(b, "\x1A\x45\xDF\xA3", 4) == 0) fmt = "Matroska（MKV / WebM）";
        else if (n >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "AVI ", 4) == 0) fmt = "AVI";
        else if (n >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0) fmt = "WAV";
        else if (n >= 1 && b[0] == 0x47)                         fmt = "MPEG-TS（还要检查第 188 字节）";
        else if (n >= 4 && memcmp(b, "\0\0\0\1", 4) == 0)        fmt = "H.264/H.265 裸流（Annex B）";
        else if (n >= 2 && b[0] == 0xFF && (b[1] & 0xF6) == 0xF0) fmt = "AAC 裸流（ADTS）";

        printf("%-14s", argv[k]);
        for (size_t i = 0; i < 12 && i < n; i++) printf(" %02X", b[i]);
        printf("  -> %s\n", fmt);
    }
    return 0;
}
```

再准备几个文件：上面转出来的四种容器，改了名的 `fake.avi`，从 MP4 中取出来的 H.264 和 AAC 裸流，以及上一篇的 WAV：

```bash
ffmpeg -i bbb-10s.mp4 -c:v copy -an -bsf:v h264_mp4toannexb bbb.h264
ffmpeg -i bbb-10s.mp4 -c:a copy -vn bbb.aac
clang -O2 -o whatfmt whatfmt.c
./whatfmt bbb-10s.mp4 bbb.mkv bbb.flv bbb.ts fake.avi bbb.h264 bbb.aac sine440.wav
```

```
bbb-10s.mp4    00 00 00 20 66 74 79 70 69 73 6F 6D  -> MP4 / MOV（ISO BMFF）
bbb.mkv        1A 45 DF A3 A3 42 86 81 01 42 F7 81  -> Matroska（MKV / WebM）
bbb.flv        46 4C 56 01 05 00 00 00 09 00 00 00  -> FLV
bbb.ts         47 40 11 10 00 42 F0 25 00 01 C1 00  -> MPEG-TS（还要检查第 188 字节）
fake.avi       00 00 00 20 66 74 79 70 69 73 6F 6D  -> MP4 / MOV（ISO BMFF）
bbb.h264       00 00 00 01 06 05 FF FF A9 DC 45 E9  -> H.264/H.265 裸流（Annex B）
bbb.aac        FF F1 4C 80 25 BF FC DC 00 4C 61 76  -> AAC 裸流（ADTS）
sine440.wav    52 49 46 46 24 DC 05 00 57 41 56 45  -> WAV
```

- MP4 的第 5～8 个字节是 `ftyp`（66 74 79 70）：MP4 由一个个「盒子」（box）组成，每个盒子先是 4 字节长度，再是 4 字节类型，第一个盒子是 `ftyp`；后面的 `isom` 就是 ffprobe 显示的 `major_brand`。
- MKV 以 `1A 45 DF A3` 开头，这是 EBML 格式的标识；FLV 以 "FLV" 三个字符开头；WAV 以 "RIFF" 开头，第 9～12 字节是 "WAVE"（第 2 篇自己写过）。
- TS 以 `0x47` 开头，但一个字节太容易碰巧出现了，所以还要检查第 188 字节、第 376 字节……是不是也是 `0x47`。`xxd -s 188 -l 4 bbb.ts` 看到的确实是 `47 40 00 10`。
- `fake.avi` 的开头和 MP4 一模一样，被正确地识别为 MP4。
- 裸流没有容器，开头就是码流本身：H.264 Annex B 裸流以起始码 `00 00 00 01` 开头；AAC 裸流以 ADTS 头的同步字 `FFF` 开头（前 12 位全是 1）。

FFmpeg 的格式探测本质上就是这样做的，只是更仔细：每种格式检查得更多，最后选得分最高的那个。

## 6. 小结

- 视频文件是一个**容器**，里面装着一条或多条**流**；扩展名一般表示容器，不表示编码。
- 播放 = 解封装 → 解码 → 同步 → 渲染，这也是本专栏后面几章的顺序。
- `ffprobe` 查看文件信息：`-show_format`、`-show_streams`、`-show_packets`、`-show_frames`，配合 `-show_entries` 和 `-of` 选择字段和输出格式。
- 时间戳是整数，乘以时间基才是秒；不同容器的时间基不同，转换时要换算。
- 有 B 帧时，包按解码顺序存放，DTS 递增而 PTS 不是。
- `-c copy` 转封装不改变流的内容，只改变外层结构，速度极快；不同容器的开销和特点不同。
- 判断文件格式要看开头的魔数，而不是扩展名。

第 1 章到这里结束。第 2 章打开这些盒子：手写 H.264、AAC 码流解析器，以及 FLV、MP4、TS 封装格式的解析器。

## 练习

1. 用 `ffprobe -show_format` 查看测试视频，找出 `encoder` 字段，它是哪个程序写的这个 MP4？
2. 把 4.1 中的 `bbb.flv` 用 `ffprobe -show_packets` 查看，时间基是多少？和 MP4 中同一个包的时间戳怎样换算？
3. 用 `ffmpeg -i bbb-10s.mp4 -c copy -movflags +faststart` 和不加 `faststart` 各生成一个文件，用 `xxd -l 64` 查看开头，`moov` 在哪里？
4. 给 `whatfmt.c` 加上 MP3（开头是 `ID3` 或 `FF FB`）和 PNG、JPEG 图片的识别。
5. 用 `ffprobe -show_packets` 统计测试视频中音频包和视频包各有多少个，和时长、帧率、采样率对得上吗？


<details>
<summary>点击查看答案</summary>

1. 容器的元数据中 `encoder=Lavf63.1.102`，是 FFmpeg 的封装库 libavformat 写的这个 MP4；视频流的元数据中还有 `encoder: Lavc63.1.102 libx264`，说明视频是 libavcodec 调用 x264 编码的。所以第 0 节的那条 `ffmpeg` 命令在文件中留下了自己的「签名」。用别的软件导出的视频，这里会是 `HandBrake`、`Adobe Premiere` 之类。

2. FLV 的时间基固定是 1/1000，时间戳以毫秒为单位：

   ```
   pts=83|pts_time=0.083000|dts=0
   pts=208|pts_time=0.208000|dts=41
   pts=125|pts_time=0.125000|dts=83
   pts=166|pts_time=0.166000|dts=125
   ```

   和 MP4 对照，第一个包在 MP4 中是 PTS 0、DTS −1024，在 FLV 中却是 PTS 83、DTS 0：**FLV 的时间戳不能是负数**，FFmpeg 写 FLV 时把所有时间戳整体往后挪了 1024 / 12288 秒 = 83 毫秒。所以换算关系是：FLV 时间戳 = (MP4 时间戳 + 1024) × 1000 / 12288。例如 MP4 中 PTS 1536 的包：(1536 + 1024) × 1000 / 12288 = 208.3，取整为 208。

   还能看出另一个问题：毫秒精度表示不了 1/24 秒（41.67 毫秒），DTS 只能是 0、41、83、125……间隔 41 和 42 交替。这就是为什么 MP4 要用 1/12288 这样精细的时间基。

3. 不加 `faststart` 时文件开头的 64 字节：

   ```
   00000000: 0000 0020 6674 7970 6973 6f6d 0000 0200  ... ftypisom....
   00000010: 6973 6f6d 6973 6f32 6176 6331 6d70 3431  isomiso2avc1mp41
   00000020: 0000 0008 6672 6565 002d 92f9 6d64 6174  ....free.-..mdat
   00000030: 0000 02ad 0605 ffff a9dc 45e9 bde6 d948  ..........E....H
   ```

   `ftyp`（32 字节）、`free`（8 字节）之后直接是 `mdat`（媒体数据），长度 0x002d92f9 = 2986745 字节。按「4 字节长度 + 4 字节类型」逐个跳过所有顶层盒子，两个文件的结构是：

   ```
   不加 faststart：ftyp 32 | free 8 | mdat 2986745 | moov 7926（在偏移 2986785，文件末尾）
   加 faststart：  ftyp 32 | moov 7926 | free 8 | mdat 2986745
   ```

   两个文件一样大，只是 `moov` 的位置不同。7926 字节的 `moov` 中存着每一帧的位置和时间，不加 `faststart` 时，播放器要先读到文件末尾拿到它，才能开始播放。

4. 在判断 ADTS 的那一行后面加上：

   ```c
   else if (n >= 3 && memcmp(b, "ID3", 3) == 0)             fmt = "MP3（带 ID3 标签）";
   else if (n >= 2 && b[0] == 0xFF && (b[1] & 0xE6) == 0xE2) fmt = "MP3（Layer III）";
   else if (n >= 8 && memcmp(b, "\x89PNG\r\n\x1A\n", 8) == 0) fmt = "PNG 图片";
   else if (n >= 3 && memcmp(b, "\xFF\xD8\xFF", 3) == 0)   fmt = "JPEG 图片";
   ```

   MP3 帧头和 ADTS 一样以 11 个 1 开头，区别在后面的「层」字段：ADTS 是 00，MP3（Layer III）是 01，所以用掩码 `0xE6` 取出同步位和层，等于 `0xE2` 才是 MP3。很多 MP3 文件开头是 ID3 标签（歌名、歌手、封面），真正的音频帧在标签后面。用测试视频的音轨生成两个 MP3 测试：

   ```bash
   ffmpeg -i bbb-10s.mp4 -vn -c:a libmp3lame a.mp3
   ffmpeg -i bbb-10s.mp4 -vn -c:a libmp3lame -id3v2_version 0 -write_xing 0 a_noid3.mp3
   ./whatfmt a.mp3 a_noid3.mp3 frame-u.png frame-color.jpg bbb.aac
   ```

   ```
   a.mp3          49 44 33 04 00 00 00 00 01 09 54 58  -> MP3（带 ID3 标签）
   a_noid3.mp3    FF FB 94 44 00 00 02 0B 09 CF 9D 24  -> MP3（Layer III）
   frame-u.png    89 50 4E 47 0D 0A 1A 0A 00 00 00 0D  -> PNG 图片
   frame-color.jpg FF D8 FF FE 00 0F 4C 61 76 63 36 33  -> JPEG 图片
   bbb.aac        FF F1 4C 80 25 BF FC DC 00 4C 61 76  -> AAC 裸流（ADTS）
   ```

5. 统计包数：

   ```bash
   ffprobe -v error -select_streams v -count_packets -show_entries stream=nb_read_packets -of csv=p=0 bbb-10s.mp4
   ffprobe -v error -select_streams a -count_packets -show_entries stream=nb_read_packets -of csv=p=0 bbb-10s.mp4
   ```

   视频 240 个包：10 秒 × 24 帧/秒 = 240，一帧一个包。音频 470 个包：每个 AAC 包 1024 个采样，10 秒 × 48000 / 1024 = 468.75，向上取整 469，再加上开头那个标记为 discard 的预热包，正好 470。

</details>
