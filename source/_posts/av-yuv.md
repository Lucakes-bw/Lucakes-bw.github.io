---
title: 像素与 YUV
date: 2026-10-28 09:00:00
updated: 2026-10-28 09:00:00
categories:
  - 音视频开发
  - 音视频基础
tags:
  - 音视频
  - YUV
  - C
order: 1
description: 视频的每一帧就是一张由像素组成的图片，但播放器拿到的通常不是 RGB，而是 YUV。用 C 把测试视频的一帧 YUV420P 拆成 Y、U、V 三个分量，看看它们分别长什么样；把 U、V 改成 128，画面就变成黑白；自己写 YUV 到 RGB 的转换并保存成 BMP。宽度写错一个像素，画面会斜成一片条纹；把 NV12 当成 I420 读，颜色全乱；用 BT.601 的公式转高清视频，颜色会偏。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：知道一张图片在内存中怎么存：像素、RGB、每行的字节数；理解 YUV 是什么、为什么视频用 YUV 而不用 RGB；理解 4:2:0 色度抽样，会算一帧 YUV 有多大；分清 I420、NV12、YUYV 几种内存布局；会用 C 读写原始 YUV 数据，自己把 YUV 转成 RGB。
>
> **前置知识**：C 语言的数组、指针和文件读写（C/C++ 专栏《C语言基础》）。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，Homebrew 安装的 FFmpeg 9.0。本文的程序只用 C 标准库，在 Linux 和 Windows 上同样可以编译。FFmpeg 在本章只当作工具，用来准备数据和查看结果。

## 0. 本专栏的测试视频

整个专栏都用同一段测试视频：开源动画短片 **Big Buck Bunny**（大雄兔）中的 10 秒。它是 Blender 基金会 2008 年制作的，以 CC BY 3.0 协议发布，可以自由使用，是音视频领域最常用的测试素材之一。

<img src="/img/av/bbb-poster.jpg" alt="测试视频的一帧：大兔子站在草地上" width="640">

**下载**：[bbb-10s.mp4](/media/bbb-10s.mp4)（3.0 MB），参数如下：

| 项目 | 值 |
|---|---|
| 时长 | 10 秒 |
| 视频 | H.264，1280×720，24 帧/秒，YUV420P |
| 音频 | AAC，48000 Hz，双声道 |
| 来源 | Big Buck Bunny 720p 版（© Blender Foundation，[peach.blender.org](https://peach.blender.org)，CC BY 3.0） |

它是从 Blender 官网的 720p 原片（[big_buck_bunny_720p_h264.mov.zip](https://download.blender.org/peach/bigbuckbunny_movies/)，417 MB）中 1 分 10 秒起截出来的，原片的音频是 5.1 声道，这里混成了立体声。命令如下，以后的文章会逐个解释这些参数，现在照抄即可：

```bash
ffmpeg -ss 00:01:10 -i big_buck_bunny_720p_h264.mov -t 10 -map 0:v:0 -map 0:a:0 -map_metadata -1 \
  -c:v libx264 -profile:v high -crf 23 -g 48 -bf 2 -pix_fmt yuv420p \
  -c:a aac -b:a 128k -ar 48000 -ac 2 -write_tmcd 0 -movflags +faststart bbb-10s.mp4
```

FFmpeg 的安装方式：macOS 上 `brew install ffmpeg`，Ubuntu 上 `sudo apt install ffmpeg`，Windows 上从 [ffmpeg.org](https://ffmpeg.org/download.html) 下载编译好的程序，把 `bin` 目录加入 `PATH`。

本章的程序不用 FFmpeg 的库，只处理**原始数据**：没有压缩、没有文件头，一个字节一个字节排好的像素和采样点。先用 FFmpeg 命令行把它们从视频中解出来：

```bash
# 第 1 帧，原始 YUV420P
ffmpeg -i bbb-10s.mp4 -frames:v 1 -pix_fmt yuv420p -f rawvideo frame.yuv
```

```bash
ls -l frame.yuv
```

```
-rw-r--r--  1 lucky  staff  1382400 Oct 28 09:00 frame.yuv
```

1382400 字节，没有任何文件头。这个数字是怎么来的，是本文要讲清楚的事情。

## 1. 图片在内存中怎么存

### 1.1 像素和 RGB

把一张图片放大，会看到它是由一个个小方块组成的，每个方块只有一种颜色，这就是**像素**（pixel）。1280×720 的图片，每行 1280 个像素，共 720 行，一共 921600 个像素。

一个像素的颜色怎么用数字表示？屏幕上每个像素由红、绿、蓝三个发光点组成，调节三者的亮度就能混出各种颜色。所以最直接的方法是存三个数：R、G、B，每个 0～255，各占 1 字节。这种格式叫 **RGB24**，一个像素 3 字节：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="RGB24 的内存布局：4x2 的图片，每个像素 3 字节，按行从左到右、从上到下依次存放">
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="20" style="fill:var(--fg)">4×2 的图片</text>
    <g transform="translate(10,32)">
      <rect x="0" y="0" width="40" height="40" fill="#e53935"/><rect x="40" y="0" width="40" height="40" fill="#43a047"/>
      <rect x="80" y="0" width="40" height="40" fill="#1e88e5"/><rect x="120" y="0" width="40" height="40" fill="#ffffff" style="stroke:var(--border-2)"/>
      <rect x="0" y="40" width="40" height="40" fill="#fdd835"/><rect x="40" y="40" width="40" height="40" fill="#000000"/>
      <rect x="80" y="40" width="40" height="40" fill="#8e24aa"/><rect x="120" y="40" width="40" height="40" fill="#808080"/>
      <text x="14" y="25" fill="#fff">0</text><text x="54" y="25" fill="#fff">1</text><text x="94" y="25" fill="#fff">2</text><text x="134" y="25" fill="#000">3</text>
      <text x="14" y="65" fill="#000">4</text><text x="54" y="65" fill="#fff">5</text><text x="94" y="65" fill="#fff">6</text><text x="134" y="65" fill="#fff">7</text>
    </g>
    <text x="200" y="20" style="fill:var(--fg)">在内存中（每格 1 字节）</text>
    <g transform="translate(200,32)">
      <rect x="0" y="0" width="22" height="26" fill="#e53935" opacity=".85"/><rect x="22" y="0" width="22" height="26" fill="#e53935" opacity=".55"/><rect x="44" y="0" width="22" height="26" fill="#e53935" opacity=".3"/>
      <rect x="66" y="0" width="22" height="26" fill="#43a047" opacity=".85"/><rect x="88" y="0" width="22" height="26" fill="#43a047" opacity=".55"/><rect x="110" y="0" width="22" height="26" fill="#43a047" opacity=".3"/>
      <rect x="132" y="0" width="22" height="26" fill="#1e88e5" opacity=".85"/><rect x="154" y="0" width="22" height="26" fill="#1e88e5" opacity=".55"/><rect x="176" y="0" width="22" height="26" fill="#1e88e5" opacity=".3"/>
      <rect x="198" y="0" width="66" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <text x="6" y="17" style="fill:var(--fg)">R</text><text x="28" y="17" style="fill:var(--fg)">G</text><text x="50" y="17" style="fill:var(--fg)">B</text>
      <text x="72" y="17" style="fill:var(--fg)">R</text><text x="94" y="17" style="fill:var(--fg)">G</text><text x="116" y="17" style="fill:var(--fg)">B</text>
      <text x="138" y="17" style="fill:var(--fg)">R</text><text x="160" y="17" style="fill:var(--fg)">G</text><text x="182" y="17" style="fill:var(--fg)">B</text>
      <text x="210" y="17" style="fill:var(--muted)">像素 3</text>
      <text x="270" y="17" style="fill:var(--muted)">→ 第 0 行，4×3 = 12 字节</text>
      <text x="22" y="44" style="fill:var(--muted)">像素 0</text><text x="88" y="44" style="fill:var(--muted)">像素 1</text><text x="154" y="44" style="fill:var(--muted)">像素 2</text>
      <rect x="0" y="54" width="264" height="26" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <text x="70" y="71" style="fill:var(--muted)">像素 4 ～ 像素 7</text>
      <text x="270" y="71" style="fill:var(--muted)">→ 第 1 行紧接在后面</text>
    </g>
    <text x="10" y="150" style="fill:var(--fg)">第 i 行第 j 个像素的 R 在 data[(i × 宽 + j) × 3]，G、B 依次在后面两个字节</text>
    <text x="10" y="172" style="fill:var(--muted)">整张图片 = 宽 × 高 × 3 字节；1280×720 → 2764800 字节</text>
  </g>
</svg>
</figure>

像素按行存放：先存第 0 行从左到右的所有像素，再存第 1 行，依此类推。这个顺序叫**光栅扫描**顺序，几乎所有图像格式都这样存。所以只要知道宽度，就能算出任意一个像素的位置。反过来，**原始图像数据中没有宽高信息**，读的人必须事先知道，第 5 节会看到宽度写错的后果。

一张 1280×720 的 RGB24 图片是 1280 × 720 × 3 = 2764800 字节。而前面解出来的 YUV 帧只有 1382400 字节，正好一半。省下来的一半是怎么来的？

### 1.2 YUV：把亮度和颜色分开

YUV 换了一种方式描述颜色：

- **Y**：亮度（luma），这个像素有多亮。只看 Y，就是一张黑白照片。
- **U**（也写作 Cb）：蓝色色度，颜色偏蓝还是偏黄。
- **V**（也写作 Cr）：红色色度，颜色偏红还是偏青。

U、V 合称**色度**（chroma）。没有颜色的灰色像素，U、V 都是 128；偏离 128 越远，颜色越浓。

它和 RGB 可以用公式互相换算（第 4 节会写代码），表示的是同样的颜色，只是坐标系不同。为什么视频要用这种坐标系？有两个原因：

1. **历史原因**：彩色电视出现时，大量黑白电视还在使用。把信号分成亮度和色度，黑白电视只接收 Y，彩色电视再加上 U、V，一套信号两种电视都能看。
2. **人眼的特点**：人眼对亮度的变化很敏感，对颜色的细节却很迟钝。既然亮度和颜色已经分开，就可以**少存一些颜色**，而人几乎看不出来。

第二点就是 YUV 帧比 RGB 小一半的原因。

### 1.3 色度抽样：4:4:4、4:2:2、4:2:0

「少存颜色」的具体做法是：每个像素都有自己的 Y，但相邻的几个像素**共用一组 U、V**。共用的方式用三个数字表示：

<figure class="diagram">
<svg viewBox="0 0 690 210" role="img" aria-label="色度抽样示意图：4:4:4 每个像素都有 U、V；4:2:2 水平方向每 2 个像素共用一组 U、V；4:2:0 每 2x2 个像素共用一组 U、V">
  <defs>
    <g id="yuvgrid">
      <rect x="0" y="0" width="30" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><rect x="30" y="0" width="30" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><rect x="60" y="0" width="30" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><rect x="90" y="0" width="30" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <rect x="0" y="30" width="30" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><rect x="30" y="30" width="30" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><rect x="60" y="30" width="30" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/><rect x="90" y="30" width="30" height="30" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <g style="fill:var(--muted)"><circle cx="15" cy="15" r="3"/><circle cx="45" cy="15" r="3"/><circle cx="75" cy="15" r="3"/><circle cx="105" cy="15" r="3"/><circle cx="15" cy="45" r="3"/><circle cx="45" cy="45" r="3"/><circle cx="75" cy="45" r="3"/><circle cx="105" cy="45" r="3"/></g>
    </g>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(20,30)">
      <text x="0" y="-10" style="fill:var(--fg)">4:4:4</text>
      <use href="#yuvgrid"/>
      <g fill="none" style="stroke:var(--warn)" stroke-width="2">
        <rect x="4" y="4" width="22" height="22"/><rect x="34" y="4" width="22" height="22"/><rect x="64" y="4" width="22" height="22"/><rect x="94" y="4" width="22" height="22"/>
        <rect x="4" y="34" width="22" height="22"/><rect x="34" y="34" width="22" height="22"/><rect x="64" y="34" width="22" height="22"/><rect x="94" y="34" width="22" height="22"/>
      </g>
      <text x="0" y="84" style="fill:var(--muted)">8 个 Y，8 组 UV</text>
      <text x="0" y="102" style="fill:var(--muted)">每像素 3 字节</text>
      <text x="0" y="120" style="fill:var(--muted)">专业制作、屏幕录制</text>
    </g>
    <g transform="translate(250,30)">
      <text x="0" y="-10" style="fill:var(--fg)">4:2:2</text>
      <use href="#yuvgrid"/>
      <g fill="none" style="stroke:var(--warn)" stroke-width="2">
        <rect x="4" y="4" width="52" height="22"/><rect x="64" y="4" width="52" height="22"/>
        <rect x="4" y="34" width="52" height="22"/><rect x="64" y="34" width="52" height="22"/>
      </g>
      <text x="0" y="84" style="fill:var(--muted)">8 个 Y，4 组 UV</text>
      <text x="0" y="102" style="fill:var(--muted)">每像素 2 字节</text>
      <text x="0" y="120" style="fill:var(--muted)">广播级摄像机、采集卡</text>
    </g>
    <g transform="translate(480,30)">
      <text x="0" y="-10" style="fill:var(--fg)">4:2:0</text>
      <use href="#yuvgrid"/>
      <g fill="none" style="stroke:var(--warn)" stroke-width="2">
        <rect x="4" y="4" width="52" height="52"/><rect x="64" y="4" width="52" height="52"/>
      </g>
      <text x="0" y="84" style="fill:var(--muted)">8 个 Y，2 组 UV</text>
      <text x="0" y="102" style="fill:var(--muted)">每像素 1.5 字节</text>
      <text x="0" y="120" style="fill:var(--muted)">几乎所有的视频文件</text>
    </g>
    <circle cx="26" cy="190" r="3" style="fill:var(--muted)"/><text x="36" y="194" style="fill:var(--muted)">一个 Y</text>
    <rect x="110" y="182" width="16" height="16" fill="none" style="stroke:var(--warn)" stroke-width="2"/><text x="134" y="194" style="fill:var(--muted)">框住的像素共用一组 U、V</text>
  </g>
</svg>
</figure>

- **4:4:4**：不抽样，每个像素都有完整的 Y、U、V，和 RGB 一样每像素 3 字节。
- **4:2:2**：水平方向每 2 个像素共用一组 U、V，每像素 2 字节。
- **4:2:0**：水平、垂直方向都减半，每 2×2 = 4 个像素共用一组 U、V，每像素 1.5 字节。

（这三个数字的含义来自一个 4×2 像素的参考块，记住上面的图比记住数字的定义有用。）

我们日常看到的视频文件、网络直播、视频会议，**几乎全都是 4:2:0**。于是一帧 1280×720 的 YUV420 有多大：

| 分量 | 尺寸 | 字节数 |
|---|---|---|
| Y | 1280 × 720 | 921600 |
| U | 640 × 360 | 230400 |
| V | 640 × 360 | 230400 |
| 合计 | | **1382400** = 宽 × 高 × 1.5 |

正好是开头 `frame.yuv` 的大小。一下子省掉一半的数据，代价是颜色的分辨率只有原来的四分之一，第 3 节会实际看看这个代价有多大。

## 2. YUV 在内存中的排列

知道了每个分量有多少字节，还要知道它们怎么排列。同样是 4:2:0，常见的就有好几种排法：

<figure class="diagram">
<svg viewBox="0 0 690 220" role="img" aria-label="三种 YUV 内存布局：I420 先存所有 Y，再存所有 U，再存所有 V；NV12 先存所有 Y，再交错存 UV；YUYV 是 4:2:2 打包格式，按 Y U Y V 交错存放">
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="22" style="fill:var(--fg)">I420（YUV420P，平面格式）</text>
    <g transform="translate(10,32)">
      <rect x="0" y="0" width="400" height="28" style="fill:var(--accent);opacity:.7"/><text x="180" y="18" style="fill:var(--bg)">Y（w×h）</text>
      <rect x="400" y="0" width="100" height="28" fill="#1e88e5" opacity=".75"/><text x="420" y="18" fill="#fff">U（w×h/4）</text>
      <rect x="500" y="0" width="100" height="28" fill="#e53935" opacity=".75"/><text x="520" y="18" fill="#fff">V（w×h/4）</text>
    </g>
    <text x="10" y="92" style="fill:var(--fg)">NV12（YUV420SP，半平面格式）</text>
    <g transform="translate(10,102)">
      <rect x="0" y="0" width="400" height="28" style="fill:var(--accent);opacity:.7"/><text x="180" y="18" style="fill:var(--bg)">Y（w×h）</text>
      <rect x="400" y="0" width="25" height="28" fill="#1e88e5" opacity=".75"/><rect x="425" y="0" width="25" height="28" fill="#e53935" opacity=".75"/>
      <rect x="450" y="0" width="25" height="28" fill="#1e88e5" opacity=".75"/><rect x="475" y="0" width="25" height="28" fill="#e53935" opacity=".75"/>
      <rect x="500" y="0" width="100" height="28" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <text x="407" y="18" fill="#fff">U</text><text x="432" y="18" fill="#fff">V</text><text x="457" y="18" fill="#fff">U</text><text x="482" y="18" fill="#fff">V</text>
      <text x="520" y="18" style="fill:var(--muted)">UVUV…</text>
    </g>
    <text x="10" y="162" style="fill:var(--fg)">YUYV（YUY2，4:2:2 打包格式）</text>
    <g transform="translate(10,172)">
      <rect x="0" y="0" width="25" height="28" style="fill:var(--accent);opacity:.7"/><rect x="25" y="0" width="25" height="28" fill="#1e88e5" opacity=".75"/>
      <rect x="50" y="0" width="25" height="28" style="fill:var(--accent);opacity:.7"/><rect x="75" y="0" width="25" height="28" fill="#e53935" opacity=".75"/>
      <rect x="100" y="0" width="25" height="28" style="fill:var(--accent);opacity:.7"/><rect x="125" y="0" width="25" height="28" fill="#1e88e5" opacity=".75"/>
      <rect x="150" y="0" width="25" height="28" style="fill:var(--accent);opacity:.7"/><rect x="175" y="0" width="25" height="28" fill="#e53935" opacity=".75"/>
      <rect x="200" y="0" width="400" height="28" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <text x="7" y="18" style="fill:var(--bg)">Y</text><text x="32" y="18" fill="#fff">U</text><text x="57" y="18" style="fill:var(--bg)">Y</text><text x="82" y="18" fill="#fff">V</text>
      <text x="107" y="18" style="fill:var(--bg)">Y</text><text x="132" y="18" fill="#fff">U</text><text x="157" y="18" style="fill:var(--bg)">Y</text><text x="182" y="18" fill="#fff">V</text>
      <text x="220" y="18" style="fill:var(--muted)">每两个像素 4 字节，整帧 w×h×2</text>
    </g>
    <text x="625" y="50" style="fill:var(--muted)">软件</text><text x="625" y="66" style="fill:var(--muted)">解码</text>
    <text x="625" y="120" style="fill:var(--muted)">硬件</text><text x="625" y="136" style="fill:var(--muted)">解码</text>
    <text x="625" y="190" style="fill:var(--muted)">摄像头</text>
  </g>
</svg>
</figure>

| 名称 | FFmpeg 中的名字 | 排列方式 | 常见于 |
|---|---|---|---|
| I420 | `yuv420p` | 三个平面：全部 Y，全部 U，全部 V | 软件解码器的输出，x264 的输入 |
| YV12 | `yuv420p`（U、V 交换） | 同 I420，但 V 在 U 前面 | 部分 Android 接口 |
| NV12 | `nv12` | 两个平面：全部 Y，然后 UV 交错 | 硬件编解码（macOS VideoToolbox、Intel、NVIDIA） |
| NV21 | `nv21` | 同 NV12，但 VU 交错 | Android 摄像头 |
| YUYV | `yuyv422` | 一个平面，Y U Y V 交错 | USB 摄像头 |

- **平面格式**（planar，名字带 P）：同一个分量连续存放。处理某一个分量很方便，例如只取 Y 就能得到黑白图像。
- **打包格式**（packed）：一个像素的各个分量挨着存，和 RGB24 类似。
- **半平面格式**（semi-planar）：Y 单独一个平面，U、V 交错存在另一个平面。

这些格式的数据本身**都没有标记**，一段 1382400 字节的数据，既可以是 1280×720 的 I420，也可以是 NV12，甚至是 960×960 的 I420。读数据的人必须事先知道格式和宽高，第 5 节会看到格式弄错的后果。

本专栏最常打交道的是 `yuv420p`：FFmpeg 软件解码 H.264 的输出就是它。

## 3. 拆开一帧 YUV

### 3.1 把 Y、U、V 分别写成文件

按照 I420 的布局，前 w×h 字节是 Y，接下来 w×h/4 字节是 U，再接下来 w×h/4 字节是 V。把它们分别写到三个文件中：

```c
// yuv_split.c：把一帧 YUV420P 拆成 Y、U、V 三个文件
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "用法: %s 输入.yuv 宽 高\n", argv[0]);
        return 1;
    }
    int w = atoi(argv[2]), h = atoi(argv[3]);
    size_t y_size = (size_t)w * h;          // Y：每个像素 1 字节
    size_t uv_size = y_size / 4;            // U、V：宽高各减半
    size_t frame_size = y_size + uv_size * 2;

    unsigned char *buf = malloc(frame_size);
    FILE *in = fopen(argv[1], "rb");
    if (!in || fread(buf, 1, frame_size, in) != frame_size) {
        fprintf(stderr, "读取失败\n");
        return 1;
    }
    fclose(in);

    FILE *fy = fopen("out_y.y", "wb");
    FILE *fu = fopen("out_u.y", "wb");
    FILE *fv = fopen("out_v.y", "wb");
    fwrite(buf, 1, y_size, fy);                       // [0, w*h)
    fwrite(buf + y_size, 1, uv_size, fu);             // 紧跟着是 U
    fwrite(buf + y_size + uv_size, 1, uv_size, fv);   // 最后是 V
    fclose(fy); fclose(fu); fclose(fv);

    printf("帧大小 %zu 字节：Y %zu，U %zu，V %zu\n",
           frame_size, y_size, uv_size, uv_size);
    free(buf);
    return 0;
}
```

```bash
clang -O2 -o yuv_split yuv_split.c
./yuv_split frame.yuv 1280 720
```

```
帧大小 1382400 字节：Y 921600，U 230400，V 230400
```

三个文件都是单通道的灰度数据，每个字节就是一个像素的灰度。用 FFmpeg 把它们转成 PNG 看一看（`-pix_fmt gray` 告诉 FFmpeg 输入是每像素 1 字节的灰度，`-s` 给出宽高，因为原始数据中没有这个信息）：

```bash
ffmpeg -f rawvideo -pix_fmt gray -s 1280x720 -i out_y.y y.png
ffmpeg -f rawvideo -pix_fmt gray -s 640x360  -i out_u.y u.png
ffmpeg -f rawvideo -pix_fmt gray -s 640x360  -i out_v.y v.png
```

<figure>
<img src="/img/av/frame-color.jpg" alt="原始画面" width="640">
<figcaption>原始画面</figcaption>
</figure>

<figure>
<img src="/img/av/frame-y.jpg" alt="Y 分量：一张完整清晰的黑白照片" width="640">
<figcaption>Y 分量（1280×720）</figcaption>
</figure>

<figure>
<img src="/img/av/frame-u.png" alt="U 分量：大部分是接近 128 的灰色，偏蓝的区域更亮" width="320"> <img src="/img/av/frame-v.png" alt="V 分量：大部分是接近 128 的灰色，偏红的区域更亮" width="320">
<figcaption>U 分量（左）和 V 分量（右），各 640×360</figcaption>
</figure>

几个观察：

- **Y 就是一张黑白照片**，画面中所有的细节、轮廓、纹理都在这里。
- **U、V 看起来模糊、发灰**。大部分区域接近 128 的中灰色（没什么颜色），颜色明显的地方才偏离 128：天空是淡蓝色，在 U 中偏亮（约 154），在 V 中偏暗（约 103）；草地是黄绿色，在 U 中偏暗（约 114）。整个 U 平面的值只在 68～188 之间，V 在 72～169 之间，远没有用满 0～255。
- U、V 的尺寸只有 Y 的四分之一，放大到同样尺寸也看不出多少细节，因为颜色本来就没有多少细节。这正是 4:2:0 能把色度砍掉四分之三的原因。

### 3.2 去掉颜色：U、V 全部改成 128

既然 U = V = 128 表示「没有颜色」，那把所有的 U、V 都改成 128，画面就应该变成黑白。下面的程序处理整个文件，一帧一帧地改：

```c
// yuv_gray.c：把 YUV420P 变成黑白：U、V 全部改成 128
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr, "用法: %s 输入.yuv 输出.yuv 宽 高\n", argv[0]);
        return 1;
    }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    size_t y_size = (size_t)w * h, frame_size = y_size * 3 / 2;
    unsigned char *buf = malloc(frame_size);

    FILE *in = fopen(argv[1], "rb");
    FILE *out = fopen(argv[2], "wb");
    int frames = 0;
    while (fread(buf, 1, frame_size, in) == frame_size) {   // 一帧一帧处理
        memset(buf + y_size, 128, y_size / 2);   // U 和 V 连在一起，共 w*h/2 字节
        fwrite(buf, 1, frame_size, out);
        frames++;
    }
    printf("处理了 %d 帧\n", frames);
    fclose(in); fclose(out); free(buf);
    return 0;
}
```

U 和 V 两个平面在 I420 中是连续的，所以一次 `memset` 就能把它们全部改掉。

```bash
ffmpeg -i bbb-10s.mp4 -pix_fmt yuv420p -f rawvideo bbb.yuv    # 整个视频
ls -l bbb.yuv
./yuv_gray bbb.yuv gray.yuv 1280 720
ffplay -f rawvideo -pixel_format yuv420p -video_size 1280x720 -framerate 24 gray.yuv
```

```
-rw-r--r--  1 lucky  staff  331776000 Oct 28 09:00 bbb.yuv
处理了 240 帧
```

10 秒的视频解出来有 331776000 字节（332 MB），共 240 帧，每帧 1382400 字节，一秒钟 33 MB，而 MP4 文件总共才 3.0 MB。这个惊人的数字留到第 3 篇讨论。`ffplay` 是 FFmpeg 自带的播放器，同样需要告诉它格式、宽高和帧率，播放出来是黑白的：

<img src="/img/av/frame-gray.jpg" alt="U、V 改成 128 之后变成黑白画面" width="640">

## 4. 自己把 YUV 转成 RGB

屏幕显示需要 RGB，所以播放器最后总要做一次 YUV → RGB 的转换（通常在显卡上做）。下面自己写一遍，并把结果保存成 BMP 图片：BMP 几乎就是「加了文件头的 RGB 数据」，所有看图软件都能打开。

### 4.1 转换公式

高清视频（720p、1080p）使用 **BT.709** 标准规定的公式：

```
R = Y' + 1.5748 V'
G = Y' − 0.1873 U' − 0.4681 V'
B = Y' + 1.8556 U'
```

其中 Y'、U'、V' 是先做过一步换算的值。视频中的 YUV 通常不用满 0～255 的范围，Y 只用 **16～235**，U、V 只用 **16～240**，这叫**有限范围**（limited range，也叫 TV range）；0～255 都用的叫**全范围**（full range，PC range）。所以要先把 Y 从 16～235 拉伸到 0～255，U、V 从 16～240 映射到 −128～127：

```
Y' = (Y − 16) × 255 / 219
U' = (U − 128) × 255 / 224
V' = (V − 128) × 255 / 224
```

标清视频（480p 及以下）用的是另一个标准 **BT.601**，系数不同。用错了标准会怎样，第 5.3 节实测。

> 为什么要留出 0～15 和 236～255 不用？这是模拟电视时代留下的：信号会有过冲，需要留出余量，同时把一部分电平用作同步信号。数字视频沿用了这个约定。

### 4.2 程序

4:2:0 中，第 i 行第 j 个像素的 U、V 在色度平面的第 i/2 行、第 j/2 列，宽度是 w/2。BMP 有几个容易踩的地方：像素行**从下往上**存、每个像素的顺序是 **B、G、R**、每行的字节数必须是 **4 的倍数**，程序中都有注释。

```c
// yuv2bmp.c：把一帧 YUV420P 转成 RGB24，保存成 BMP
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned char clamp(double v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (unsigned char)(v + 0.5);
}

// 按 BT.709（高清视频）或 BT.601（标清视频）把一个像素从 YUV 转成 RGB
// Y 的有效范围是 16~235，U、V 是 16~240，中心 128（有限范围）
static void yuv_to_rgb(int Y, int U, int V, int bt709, unsigned char rgb[3])
{
    double y = (Y - 16) * 255.0 / 219;
    double u = (U - 128) * 255.0 / 224;
    double v = (V - 128) * 255.0 / 224;
    if (bt709) {
        rgb[0] = clamp(y + 1.5748 * v);
        rgb[1] = clamp(y - 0.1873 * u - 0.4681 * v);
        rgb[2] = clamp(y + 1.8556 * u);
    } else {
        rgb[0] = clamp(y + 1.4020 * v);
        rgb[1] = clamp(y - 0.3441 * u - 0.7141 * v);
        rgb[2] = clamp(y + 1.7720 * u);
    }
}

static void put_le32(unsigned char *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

int main(int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "用法: %s 输入.yuv 输出.bmp 宽 高 [601]\n", argv[0]);
        return 1;
    }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    int bt709 = !(argc > 5 && strcmp(argv[5], "601") == 0);
    size_t y_size = (size_t)w * h;
    unsigned char *yuv = malloc(y_size * 3 / 2);
    FILE *in = fopen(argv[1], "rb");
    if (!in || fread(yuv, 1, y_size * 3 / 2, in) != y_size * 3 / 2) {
        fprintf(stderr, "读取失败\n");
        return 1;
    }
    fclose(in);
    unsigned char *Yp = yuv, *Up = yuv + y_size, *Vp = Up + y_size / 4;

    // BMP 每行字节数必须是 4 的倍数，不够的在行尾补 0
    int row_bytes = (w * 3 + 3) & ~3;
    uint32_t data_size = (uint32_t)row_bytes * h;
    unsigned char header[54] = {'B', 'M'};
    put_le32(header + 2, 54 + data_size);   // 文件总大小
    put_le32(header + 10, 54);              // 像素数据的偏移
    put_le32(header + 14, 40);              // 信息头大小
    put_le32(header + 18, w);
    put_le32(header + 22, h);               // 正数：像素行从下往上存
    header[26] = 1;                         // 平面数
    header[28] = 24;                        // 每像素 24 位
    put_le32(header + 34, data_size);

    unsigned char *row = calloc(1, row_bytes);
    FILE *out = fopen(argv[2], "wb");
    fwrite(header, 1, 54, out);
    for (int i = h - 1; i >= 0; i--) {      // 从最后一行写起
        for (int j = 0; j < w; j++) {
            int Y = Yp[i * w + j];
            int U = Up[(i / 2) * (w / 2) + j / 2];   // 2x2 个像素共用一组 U、V
            int V = Vp[(i / 2) * (w / 2) + j / 2];
            unsigned char rgb[3];
            yuv_to_rgb(Y, U, V, bt709, rgb);
            row[j * 3 + 0] = rgb[2];        // BMP 的顺序是 B、G、R
            row[j * 3 + 1] = rgb[1];
            row[j * 3 + 2] = rgb[0];
        }
        fwrite(row, 1, row_bytes, out);
    }
    fclose(out);
    printf("%s：%dx%d，%s，每行 %d 字节，文件 %u 字节\n", argv[2], w, h,
           bt709 ? "BT.709" : "BT.601", row_bytes, 54 + data_size);
    free(row); free(yuv);
    return 0;
}
```

```bash
clang -O2 -o yuv2bmp yuv2bmp.c
./yuv2bmp frame.yuv frame.bmp 1280 720
```

```
frame.bmp：1280x720，BT.709，每行 3840 字节，文件 2764854 字节
```

文件大小 = 54 字节的文件头 + 3840 × 720 字节的像素。打开 `frame.bmp`，和原视频截图对比，肉眼看不出区别。再精确一点，让 FFmpeg 也把 `frame.yuv` 转成 RGB，计算两者的差别。衡量的指标是 **PSNR**（峰值信噪比），单位 dB，越大越接近，完全相同时是无穷大，一般超过 40 dB 人眼就看不出差别了。参数中 `in_color_matrix=bt709` 指定输入按 BT.709 解释，`neighbor` 让 FFmpeg 和我们一样，直接让 2×2 个像素共用同一组 U、V：

```bash
ffmpeg -i frame.bmp -f rawvideo -pix_fmt yuv420p -s 1280x720 -i frame.yuv -lavfi \
  "[1]scale=in_color_matrix=bt709:flags=neighbor+accurate_rnd+full_chroma_int,format=rgb24[ref];[0][ref]psnr" \
  -f null - 2>&1 | grep -o "PSNR.*"
```

```
PSNR r:72.802784 g:69.100964 b:71.962421 average:70.985585 min:70.985585 max:70.985585
```

71 dB，说明两者只差在个别像素的舍入上（±1），我们的转换是对的。如果去掉 `neighbor`，PSNR 会降到 46 dB 左右：FFmpeg 默认会在相邻的色度采样之间做插值，让颜色的边缘更平滑，而我们是直接复制，这也是一个可以改进的地方（练习 6）。如果把 `in_color_matrix` 去掉，FFmpeg 会按 BT.601 解释，PSNR 只有 39 dB，下一节就来看这个错误。

## 5. 三个常见的错误

原始数据没有任何自我描述，参数错一个，结果就全错。下面故意犯三个常见的错误，看看会得到什么，以后遇到类似的画面就知道该查哪里。

### 5.1 宽度错了：画面变斜

把宽度写成 1279，少了一个像素：

```bash
./yuv2bmp frame.yuv wrong-width.bmp 1279 720
```

<img src="/img/av/wrong-width.jpg" alt="宽度写错一个像素，画面向一边倾斜，出现彩色条纹" width="640">

程序以为每行 1279 个像素，于是第 1 行比实际少读 1 个，第 2 行少读 2 个……误差逐行累积，画面就斜了，而且色度平面的错位和亮度平面不一样，颜色也错开了。**画面倾斜、出现斜向的条纹，几乎一定是宽度或者每行的字节数（stride）弄错了。**

这个错误在实际开发中很常见，原因通常不是把宽度写错，而是忽略了 **stride**：为了内存对齐，解码器给每行分配的字节数往往比宽度大，行尾有一些不用的填充字节，和 BMP 每行补齐到 4 的倍数是一个道理。FFmpeg 中每行的实际字节数存在 `linesize` 中，第 3 章用 FFmpeg 解码时会遇到。

### 5.2 格式错了：NV12 当成 I420

先把这一帧转成 NV12，再故意按 I420 去读：

```bash
ffmpeg -f rawvideo -pix_fmt yuv420p -s 1280x720 -i frame.yuv -f rawvideo -pix_fmt nv12 frame_nv12.yuv
./yuv2bmp frame_nv12.yuv wrong-format.bmp 1280 720
```

<img src="/img/av/wrong-format.jpg" alt="NV12 按 I420 读取，轮廓正常，天空变成粉红色，颜色像另一张错位的图叠在上面" width="640">

两个文件大小相同，Y 平面也完全一样，所以画面的轮廓是对的；但 NV12 的色度平面是 UVUV 交错的，被当成「前一半全是 U、后一半全是 V」读，原来的一行 UV（1280 字节）被当成了两行 U，U 平面中装的其实是前半部分的 U、V 混合，V 平面中则是后半部分。结果就像在正确的轮廓上叠了另一张错位的彩色图片：天空变成了粉红色，花的颜色出现在不该出现的地方。 **轮廓正确、颜色错乱，一般是 YUV 的排列格式弄错了**；如果只是红蓝颜色互换（人脸发蓝），通常是 U、V 的顺序反了，例如把 YV12 当成 I420，或者把 BGR 当成 RGB。

### 5.3 标准错了：用 BT.601 转高清视频

用 BT.601 的系数转换同一帧：

```bash
./yuv2bmp frame.yuv bt601.bmp 1280 720 601
```

<figure>
<img src="/img/av/bt709-vs-601.jpg" alt="同一块区域放大：左边 BT.709 转换，右边 BT.601 转换，草地的绿色和紫花的颜色略有不同" width="640">
<figcaption>左下角的草地和紫花放大 2.4 倍。左：BT.709（正确）；右：BT.601（错误）</figcaption>
</figure>

两张图乍看一样，放大才能看出草地的绿色和紫花的颜色略有不同。逐像素统计：66% 的像素差别在 5 以内，30% 在 5～9，3.6% 在 10～14，最大的差别 21 出现在那朵紫花上，BT.709 算出的 RGB 是 (186, 81, 216)，BT.601 算出的是 (178, 60, 212)，绿色少了 21。 这种错误不明显，不放在一起对比很难察觉，所以在实际项目中很常见：同一个视频在两个播放器中颜色不一样，往往就是其中一个用错了标准。视频文件中一般会标明自己用的是哪个标准，第 4 篇用 `ffprobe` 查看时会看到 `color_space=bt709` 这样的字段。

## 6. 小结

- 图像由像素组成，按行从上到下、每行从左到右存放；原始像素数据中没有宽高和格式信息。
- RGB24 每像素 3 字节；YUV 把亮度（Y）和色度（U、V）分开，没有颜色时 U = V = 128。
- 人眼对颜色细节不敏感，所以视频几乎都用 4:2:0：每 2×2 个像素共用一组 U、V，一帧 = 宽 × 高 × 1.5 字节，比 RGB 少一半。
- 同是 4:2:0，有 I420（三个平面）、NV12（Y 加交错的 UV）等不同排列；软件解码常输出 I420，硬件解码常输出 NV12。
- YUV 转 RGB 要选对标准（高清用 BT.709，标清用 BT.601）和范围（视频通常是有限范围 16～235）。
- 宽度或 stride 错：画面倾斜；排列格式错：轮廓对、颜色乱；标准错：颜色轻微偏差。

下一篇处理音频：声音是怎样变成一串数字的，PCM 数据怎么存，并自己生成一个 WAV 文件。

## 练习

1. 写一个程序，只保留 Y 平面上半部分的亮度，下半部分的 Y 全部改成 16（黑色），U、V 不变，看看画面是什么样子。
2. 把 Y 平面的每个值减半（`y / 2`），画面会怎样？如果把 U、V 也减半呢？想想为什么 U、V 不能直接减半。
3. 修改 `yuv_split.c`，让它支持 NV12 输入：从交错的 UV 平面中拆出 U 和 V。
4. 写一个程序，把 RGB24 转换成 YUV420P（BT.709，有限范围）。提示：先算出每个像素的 Y、U、V，U、V 取 2×2 个像素的平均值。
5. 1920×1080 的 YUV420P 一帧多大？4:2:2 呢？如果每个分量用 10 位（存放时占 2 字节），又是多大？
6. 改进 `yuv2bmp.c` 的色度取值：不直接复制，而是用相邻几个色度采样的加权平均（双线性插值），再用第 4.2 节去掉 `neighbor` 的命令计算 PSNR，看看能不能接近 FFmpeg 的结果。


<details>
<summary>点击查看答案</summary>

1. 在 I420 中，Y 平面的前一半字节就是上半部分的行，所以只要从第 w×h/2 个字节开始改：

   ```c
   memset(buf + y_size / 2, 16, y_size / 2);   // Y 平面的后一半 = 下半部分
   ```

   <img src="/img/av/ex-bottom-black.jpg" alt="下半部分的亮度改成 16 之后几乎全黑" width="640">

   下半部分几乎全黑，但不是纯黑：用 `yuv2bmp` 转换后取几个点，草地变成了 (0, 8, 0)，兔子身上是 (0, 7, 19)，带一点点颜色。因为 U、V 没变，仍然偏离 128，而 Y = 16 时公式算出的 R、G、B 有的是负数（被截成 0），有的是很小的正数。要得到纯黑，U、V 也要改成 128。

2. 只把 Y 减半，画面变暗，但颜色显得更浓了：天空从 (139, 193, 240) 变成 (36, 91, 138)，亮度降了，色度却没变，颜色占的比重就变大了。另外 Y 的有效范围是 16～235，减半后最暗的像素会到 8，低于「黑」的电平 16。

   如果 U、V 也直接除以 2，整个画面变成一片绿色（下图中间）：U、V 是**以 128 为中心**的，直接除以 2 后都变成了 64 左右，相当于给每个像素加上了很强的「负蓝、负红」，也就是绿色。正确的做法是让它们离 128 的距离减半：`(u - 128) / 2 + 128`，画面变暗，颜色也变淡（下图右边）。

   <figure>
   <img src="/img/av/ex-half.jpg" alt="三种减半的结果：只改 Y，变暗；U、V 直接除以 2，变成一片绿色；U、V 离 128 的距离减半，变暗变淡" width="640">
   <figcaption>左：只把 Y 减半；中：U、V 直接除以 2；右：U、V 离 128 的距离减半</figcaption>
   </figure>

3. NV12 的 Y 平面和 I420 一样；后面的 UV 平面中，偶数位置是 U，奇数位置是 V：

   ```c
   unsigned char *uv = buf + y_size;        // UVUVUV…，共 uv_size 对
   for (size_t i = 0; i < uv_size; i++) {
       u[i] = uv[2 * i];                    // 偶数位置是 U
       v[i] = uv[2 * i + 1];                // 奇数位置是 V
   }
   ```

   用 5.2 节转出来的 `frame_nv12.yuv` 测试，拆出来的三个文件和 3.1 节从 I420 拆出来的逐字节相同（`cmp` 没有输出）。

4. 公式是 4.1 节的反过来：先按 BT.709 的权重算出亮度 Y = 0.2126 R + 0.7152 G + 0.0722 B，再由 B − Y、R − Y 得到 U、V，最后压缩到有限范围。读写 BMP 的部分和 `yuv2bmp.c` 对称（从最后一行读起，B、G、R 的顺序），核心的转换如下：

   ```c
       for (int i = 0; i < h; i++)
           for (int j = 0; j < w; j++) {
               unsigned char *p = rgb + (i * w + j) * 3;
               double y = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];   // BT.709 的亮度
               yuv[i * w + j] = clamp(16 + y * 219 / 255);
           }
       for (int i = 0; i < h / 2; i++)                // 每 2x2 个像素算一组 U、V
           for (int j = 0; j < w / 2; j++) {
               double r = 0, g = 0, b = 0;
               for (int di = 0; di < 2; di++)
                   for (int dj = 0; dj < 2; dj++) {
                       unsigned char *p = rgb + ((2 * i + di) * w + 2 * j + dj) * 3;
                       r += p[0]; g += p[1]; b += p[2];
                   }
               r /= 4; g /= 4; b /= 4;
               double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
               Up[i * (w / 2) + j] = clamp(128 + (b - y) / 1.8556 * 224 / 255);
               Vp[i * (w / 2) + j] = clamp(128 + (r - y) / 1.5748 * 224 / 255);
           }
   ```

   把 4.2 节得到的 `frame.bmp` 转回 YUV，和原来的 `frame.yuv` 比较，PSNR 是 59.5 dB（Y 61.9 dB，U 57.1 dB，V 56.6 dB），只有舍入误差。U、V 稍差一些，因为 `yuv2bmp` 把一组 U、V 复制给了 4 个像素，这里再取平均，经过两次取整。

5. 1920 × 1080 × 1.5 = **3110400 字节**（约 3 MB）；4:2:2 每像素 2 字节，**4147200 字节**；10 位 4:2:0 每个分量占 2 字节，是 8 位的两倍，**6220800 字节**。FFmpeg 中 10 位 4:2:0 的格式叫 `yuv420p10le`，HDR 视频常用它。

6. 双线性插值：取离当前像素最近的 4 个色度采样，按距离加权平均。关键是色度采样的**位置**，下面把每个色度采样看作位于它负责的 2×2 块的中心：

   ```c
   static int chroma(const unsigned char *c, int cw, int ch, int i, int j)
   {
       double x = (j - 0.5) / 2.0, y = (i - 0.5) / 2.0;   // 换算成色度平面中的坐标
       if (x < 0) x = 0;
       if (y < 0) y = 0;
       if (x > cw - 1) x = cw - 1;
       if (y > ch - 1) y = ch - 1;
       int x0 = (int)x, y0 = (int)y;
       int x1 = x0 + 1 < cw ? x0 + 1 : x0, y1 = y0 + 1 < ch ? y0 + 1 : y0;
       double fx = x - x0, fy = y - y0;
       double top = c[y0 * cw + x0] * (1 - fx) + c[y0 * cw + x1] * fx;
       double bot = c[y1 * cw + x0] * (1 - fx) + c[y1 * cw + x1] * fx;
       return (int)(top * (1 - fy) + bot * fy + 0.5);
   }
   ```

   然后把 `main` 中取 U、V 的两行改成 `chroma(Up, w / 2, h / 2, i, j)` 和 `chroma(Vp, w / 2, h / 2, i, j)`。和 FFmpeg 默认（双线性插值）的结果比较，PSNR 从直接复制时的 46.4 dB 提高到了 **51.3 dB**。

   色度采样的位置很重要：如果按 H.264 标准中默认的位置（水平方向和偶数列对齐，即 `x = j / 2.0`），PSNR 只有 47.3 dB，说明 FFmpeg 在没有特别指定时，也是按块中心来插值的。剩下的差别来自 FFmpeg 内部的定点运算和舍入方式。

</details>