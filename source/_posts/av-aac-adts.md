---
title: AAC 码流
date: 2026-10-31 10:00:00
updated: 2026-10-31 10:00:00
categories:
  - 音视频开发
  - 码流与封装格式
tags:
  - 音视频
  - AAC
  - ADTS
  - C
order: 2
description: AAC 裸流由一个个 ADTS 帧组成，每帧开头是 7 字节的头，写着采样率、声道数和帧长。用 C 逐帧解析测试视频的音轨：470 帧，每帧 1024 个采样，帧长在 281 到 415 字节之间变化，第一帧里还藏着编码器的名字。把头中的采样率从 48000 改成 44100，声音变慢变低；改成 24000，470 帧中有 468 帧根本解不出来，因为 AAC 的频带划分取决于采样率。从文件中间任意位置开始，靠同步字和帧长就能重新找到帧。最后看看 MP4 中的 AAC 为什么没有 ADTS 头，以及裸流解码出来为什么比 MP4 多了 1280 个采样。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：知道 AAC 的档次和帧的概念；读懂 ADTS 头的每个字段，会用 C 逐帧解析 AAC 裸流；理解采样率索引、帧长、同步字的作用；知道 ADTS 和 MP4 中的 AudioSpecificConfig 有什么区别；理解编码器延迟（priming）。
>
> **前置知识**：上一篇《H.264 码流》（按位读取）；第 1 章《音频与 PCM》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0。

## 1. AAC 与 ADTS

**AAC**（Advanced Audio Coding，高级音频编码）是 MP3 的后继者，同样码率下音质更好，是目前视频文件、直播、流媒体中最常用的音频编码。它有几个**档次**（profile），最常见的是：

| 档次 | Audio Object Type | 用途 |
|---|---|---|
| **AAC-LC**（低复杂度） | 2 | 绝大多数场合，测试视频用的就是它 |
| HE-AAC（AAC+） | 5 | 在 LC 的基础上加了 SBR（频带复制），64 kbps 以下的低码率 |
| HE-AACv2 | 29 | 再加上 PS（参数立体声），32 kbps 左右的极低码率 |

AAC 把音频切成**帧**来编码，AAC-LC 每帧固定是 **1024 个采样**（每个声道）。48 kHz 时一帧是 1024 / 48000 = 21.3 毫秒。编码后每帧的字节数不固定：复杂的声音用得多，简单的用得少。

压缩后的一帧数据本身不包含采样率、声道数这些信息。要单独存放或者传输 AAC，就需要在每帧前面加一个头，这就是 **ADTS**（Audio Data Transport Stream，音频数据传输流）。第 1 章第 4 篇中，从 MP4 中取出 AAC 时得到的 `bbb.aac` 就是 ADTS 格式，TS 中的 AAC 每帧也有 ADTS 头（第 1 章第 4 篇中 TS 的音频哈希值不同，就是因为每帧多了这 7 个字节）。

<figure class="diagram">
<svg viewBox="0 0 680 130" role="img" aria-label="ADTS 流的结构：每个 AAC 帧前面有一个 7 字节的 ADTS 头，帧长包括头本身；每帧对应 1024 个采样，48 kHz 时是 21.3 毫秒">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(10,20)">
      <rect x="0" y="0" width="40" height="34" style="fill:var(--warn);opacity:.8"/><text x="6" y="21" style="fill:var(--bg);font-size:11px">头 7</text>
      <rect x="40" y="0" width="150" height="34" style="fill:var(--link);opacity:.6"/><text x="60" y="21" style="fill:var(--bg)">AAC 帧数据 294</text>
      <rect x="190" y="0" width="40" height="34" style="fill:var(--warn);opacity:.8"/><text x="196" y="21" style="fill:var(--bg);font-size:11px">头 7</text>
      <rect x="230" y="0" width="166" height="34" style="fill:var(--link);opacity:.6"/><text x="252" y="21" style="fill:var(--bg)">AAC 帧数据 312</text>
      <rect x="396" y="0" width="40" height="34" style="fill:var(--warn);opacity:.8"/><text x="402" y="21" style="fill:var(--bg);font-size:11px">头 7</text>
      <rect x="436" y="0" width="140" height="34" style="fill:var(--link);opacity:.6"/><text x="456" y="21" style="fill:var(--bg)">AAC 帧数据 278</text>
      <rect x="576" y="0" width="84" height="34" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="600" y="21" style="fill:var(--muted)">…</text>
      <path d="M0,40 L0,48 L190,48 L190,40" fill="none" style="stroke:var(--fg)"/><text x="40" y="66" style="fill:var(--fg)">第 0 帧：301 字节</text>
      <path d="M190,40 L190,48 L396,48 L396,40" fill="none" style="stroke:var(--fg)"/><text x="230" y="66" style="fill:var(--fg)">第 1 帧：319 字节</text>
      <text x="0" y="96" style="fill:var(--muted)">每帧 = 1024 个采样 = 21.3 毫秒（48 kHz）；aac_frame_length 包括头本身</text>
    </g>
  </g>
</svg>
</figure>

## 2. ADTS 头

ADTS 头固定 7 个字节（如果带 CRC 校验则是 9 个字节，很少见）。把测试视频第一帧的头 `FF F1 4C 80 25 BF FC` 逐位展开：

<figure class="diagram">
<svg viewBox="0 0 680 254" role="img" aria-label="测试视频第一个 ADTS 头 FF F1 4C 80 25 BF FC 的逐位解析：同步字 FFF，MPEG-4，无 CRC，LC 档次，采样率索引 3 即 48000 Hz，2 声道，帧长 301 字节，缓冲区 0x7FF">
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="14" style="fill:var(--muted);font-size:11px">bbb.aac 的前 7 个字节：第一帧的 ADTS 头</text>
<rect x="10" y="24" width="88" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="54" y="37" text-anchor="middle" style="fill:var(--fg)">FF</text>
<rect x="98" y="24" width="88" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="142" y="37" text-anchor="middle" style="fill:var(--fg)">F1</text>
<rect x="186" y="24" width="88" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="230" y="37" text-anchor="middle" style="fill:var(--fg)">4C</text>
<rect x="274" y="24" width="88" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="318" y="37" text-anchor="middle" style="fill:var(--fg)">80</text>
<rect x="362" y="24" width="88" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="406" y="37" text-anchor="middle" style="fill:var(--fg)">25</text>
<rect x="450" y="24" width="88" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="494" y="37" text-anchor="middle" style="fill:var(--fg)">BF</text>
<rect x="538" y="24" width="88" height="18" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="582" y="37" text-anchor="middle" style="fill:var(--fg)">FC</text>
<rect x="10" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="15.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="21" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="26.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="32" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="37.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="43" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="48.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="54" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="59.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="65" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="70.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="76" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="81.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="87" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="92.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="98" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="103.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="109" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="114.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="120" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="125.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="131" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="136.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="75.5" y="78" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">1</text>
<rect x="142" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="147.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="147.0" y="88" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">2</text>
<rect x="153" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="158.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="164" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="169.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="163.5" y="78" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">3</text>
<rect x="175" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="180.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="180.0" y="88" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">4</text>
<rect x="186" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="191.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="197" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="202.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="196.5" y="78" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">5</text>
<rect x="208" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="213.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="219" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="224.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="230" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="235.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="241" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="246.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="229.5" y="78" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">6</text>
<rect x="252" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="257.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="257.0" y="78" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">7</text>
<rect x="263" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="268.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="274" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="279.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="285" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="290.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="279.0" y="78" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">8</text>
<rect x="296" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="301.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="301.0" y="78" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">9</text>
<rect x="307" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="312.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="312.0" y="88" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">10</text>
<rect x="318" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="323.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="323.0" y="78" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">11</text>
<rect x="329" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="334.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="334.0" y="88" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">12</text>
<rect x="340" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="345.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="351" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="356.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="362" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="367.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="373" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="378.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="384" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="389.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="395" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="400.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="406" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="411.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="417" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="422.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="428" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="433.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="439" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="444.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="450" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="455.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="461" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="466.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="472" y="46" width="10" height="20" style="fill:var(--accent);opacity:.75"/><text x="477.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="411.0" y="78" text-anchor="middle" style="fill:var(--accent);font-size:10px;font-weight:bold">13</text>
<rect x="483" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="488.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="494" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="499.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="505" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="510.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="516" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="521.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="527" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="532.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="538" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="543.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="549" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="554.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="560" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="565.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="571" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="576.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="582" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="587.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<rect x="593" y="46" width="10" height="20" style="fill:var(--link);opacity:.75"/><text x="598.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">1</text>
<text x="543.0" y="78" text-anchor="middle" style="fill:var(--link);font-size:10px;font-weight:bold">14</text>
<rect x="604" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="609.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<rect x="615" y="46" width="10" height="20" style="fill:var(--warn);opacity:.75"/><text x="620.0" y="60" text-anchor="middle" style="fill:var(--bg);font-size:11px">0</text>
<text x="614.5" y="78" text-anchor="middle" style="fill:var(--warn);font-size:10px;font-weight:bold">15</text>
<text x="10" y="110" style="fill:var(--accent);font-size:11px;font-weight:bold">1</text><text x="28" y="110" style="fill:var(--fg);font-size:11px">syncword = 0xFFF</text>
<text x="10" y="128" style="fill:var(--link);font-size:11px;font-weight:bold">2</text><text x="28" y="128" style="fill:var(--fg);font-size:11px">ID = 0（MPEG-4）</text>
<text x="10" y="146" style="fill:var(--warn);font-size:11px;font-weight:bold">3</text><text x="28" y="146" style="fill:var(--fg);font-size:11px">layer = 0</text>
<text x="10" y="164" style="fill:var(--accent);font-size:11px;font-weight:bold">4</text><text x="28" y="164" style="fill:var(--fg);font-size:11px">protection_absent = 1（无 CRC）</text>
<text x="10" y="182" style="fill:var(--link);font-size:11px;font-weight:bold">5</text><text x="28" y="182" style="fill:var(--fg);font-size:11px">profile = 1 → LC</text>
<text x="10" y="200" style="fill:var(--warn);font-size:11px;font-weight:bold">6</text><text x="28" y="200" style="fill:var(--fg);font-size:11px">sampling_frequency_index = 3 → 48000</text>
<text x="10" y="218" style="fill:var(--accent);font-size:11px;font-weight:bold">7</text><text x="28" y="218" style="fill:var(--fg);font-size:11px">private = 0</text>
<text x="10" y="236" style="fill:var(--link);font-size:11px;font-weight:bold">8</text><text x="28" y="236" style="fill:var(--fg);font-size:11px">channel_configuration = 2 → 立体声</text>
<text x="340" y="110" style="fill:var(--warn);font-size:11px;font-weight:bold">9</text><text x="358" y="110" style="fill:var(--fg);font-size:11px">original = 0</text>
<text x="340" y="128" style="fill:var(--accent);font-size:11px;font-weight:bold">10</text><text x="358" y="128" style="fill:var(--fg);font-size:11px">home = 0</text>
<text x="340" y="146" style="fill:var(--link);font-size:11px;font-weight:bold">11</text><text x="358" y="146" style="fill:var(--fg);font-size:11px">copyright_bit = 0</text>
<text x="340" y="164" style="fill:var(--warn);font-size:11px;font-weight:bold">12</text><text x="358" y="164" style="fill:var(--fg);font-size:11px">copyright_start = 0</text>
<text x="340" y="182" style="fill:var(--accent);font-size:11px;font-weight:bold">13</text><text x="358" y="182" style="fill:var(--fg);font-size:11px">aac_frame_length = 301</text>
<text x="340" y="200" style="fill:var(--link);font-size:11px;font-weight:bold">14</text><text x="358" y="200" style="fill:var(--fg);font-size:11px">adts_buffer_fullness = 0x7FF</text>
<text x="340" y="218" style="fill:var(--warn);font-size:11px;font-weight:bold">15</text><text x="358" y="218" style="fill:var(--fg);font-size:11px">raw_data_blocks−1 = 0</text>
  </g>
</svg>
</figure>

| 字段 | 位数 | 含义 |
|---|---|---|
| syncword | 12 | 同步字，固定为 `1111 1111 1111`，用来找帧的开头 |
| ID | 1 | 0 表示 MPEG-4，1 表示 MPEG-2（两者的 AAC 几乎一样） |
| layer | 2 | 固定为 0 |
| protection_absent | 1 | 1 表示没有 CRC，头是 7 字节 |
| profile | 2 | 档次**减 1**：1 表示 Audio Object Type 2，即 LC |
| sampling_frequency_index | 4 | 采样率的**索引**，查表得到采样率，3 是 48000 |
| channel_configuration | 3 | 声道配置：1 单声道，2 立体声，6 是 5.1 声道 |
| aac_frame_length | 13 | 这一帧的总长度，**包括 ADTS 头** |
| adts_buffer_fullness | 11 | 解码缓冲区的状态，`0x7FF` 表示可变码率 |
| number_of_raw_data_blocks | 2 | 这个 ADTS 帧中有几个 AAC 帧，减 1；几乎总是 0，即 1 个 |

中间还有 4 个与版权有关的位，一般都是 0。采样率索引表：

| 索引 | 0 | 1 | 2 | **3** | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 采样率 | 96000 | 88200 | 64000 | **48000** | 44100 | 32000 | 24000 | 22050 | 16000 | 12000 | 11025 | 8000 | 7350 |

只用 4 位就能表示常见的 13 种采样率，这和 H.264 用宏块数而不直接存像素数是同样的思路：头越小越好，因为每一帧都要重复一遍。

## 3. 逐帧解析

有了帧长，解析就很简单：读 7 字节的头，取出各个字段，然后跳过 `aac_frame_length` 个字节，下一帧的头就在那里。

```c
// aac_adts.c：逐帧解析 AAC 裸流的 ADTS 头
#include <stdio.h>
#include <stdlib.h>

static const int sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000, 7350, 0, 0, 0
};
static const char *profiles[4] = {"Main", "LC", "SSR", "LTP"};

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 输入.aac [显示前几帧]\n", argv[0]); return 1; }
    int show = argc > 2 ? atoi(argv[2]) : 10;
    FILE *f = fopen(argv[1], "rb");
    unsigned char h[7];
    long offset = 0, frames = 0, total = 0, min = 1 << 30, max = 0;
    int rate = 0, channels = 0;
    printf(" 帧号    偏移  长度  档次  采样率  声道  缓冲区\n");
    while (fread(h, 1, 7, f) == 7) {
        if (h[0] != 0xFF || (h[1] & 0xF0) != 0xF0) {       // 同步字：12 个 1
            printf("偏移 %ld 处不是 ADTS 头\n", offset);
            break;
        }
        int protection_absent = h[1] & 1;
        int profile = h[2] >> 6;                            // 2 位，加 1 才是 Audio Object Type
        int rate_index = (h[2] >> 2) & 0xF;
        channels = ((h[2] & 1) << 2) | (h[3] >> 6);
        int length = ((h[3] & 3) << 11) | (h[4] << 3) | (h[5] >> 5);   // 13 位，包含头本身
        int fullness = ((h[5] & 0x1F) << 6) | (h[6] >> 2);              // 11 位
        rate = sample_rates[rate_index];
        if (frames < show)
            printf("%5ld  %6ld  %4d  %4s  %6d  %4d  0x%03X%s\n", frames, offset, length,
                   profiles[profile], rate, channels, fullness, protection_absent ? "" : "（有 CRC）");
        if (length < min) min = length;
        if (length > max) max = length;
        total += length;
        frames++;
        offset += length;
        fseek(f, offset, SEEK_SET);                         // 跳到下一帧
    }
    fclose(f);
    double seconds = frames * 1024.0 / rate;                // 每帧 1024 个采样
    printf("...\n共 %ld 帧，%ld 字节，时长 %.3f 秒，平均码率 %.1f kbps\n",
           frames, total, seconds, total * 8 / seconds / 1000);
    printf("帧长：最小 %ld，最大 %ld，平均 %.1f 字节（其中 ADTS 头 7 字节）\n", min, max, (double)total / frames);
    return 0;
}
```

```bash
clang -O2 -o aac_adts aac_adts.c
./aac_adts bbb.aac 8
```

```
 帧号    偏移  长度  档次  采样率  声道  缓冲区
    0       0   301    LC   48000     2  0x7FF
    1     301   319    LC   48000     2  0x7FF
    2     620   285    LC   48000     2  0x7FF
    3     905   281    LC   48000     2  0x7FF
    4    1186   289    LC   48000     2  0x7FF
    5    1475   298    LC   48000     2  0x7FF
    6    1773   325    LC   48000     2  0x7FF
    7    2098   321    LC   48000     2  0x7FF
...
共 470 帧，163271 字节，时长 10.027 秒，平均码率 130.3 kbps
帧长：最小 281，最大 415，平均 347.4 字节（其中 ADTS 头 7 字节）
```

- **470 帧**，和第 1 章用 `ffprobe` 看到的 `nb_frames=470` 一致。时长 470 × 1024 / 48000 = 10.027 秒，比视频的 10 秒多出一点，第 6 节解释原因。
- **帧长在 281 到 415 字节之间变化**，平均 347.4 字节。虽然我们指定了 128 kbps，AAC 编码器会根据声音的复杂程度在帧之间调配码率，`adts_buffer_fullness = 0x7FF` 也表示这是可变码率。
- **平均码率 130.3 kbps**，比 `ffprobe` 显示的 127.7 kbps 高了 2.6 kbps，就是 470 个 7 字节的 ADTS 头：470 × 7 × 8 / 10.027 ≈ 2.6 kbps。所以 ADTS 头的开销约 2%。

## 4. 改错采样率会怎样

既然采样率只是头中的 4 位，那把它改掉，解码器会怎样？下面的程序把每一帧的采样率索引改成指定的值，其他什么都不动：

```c
// aac_setrate.c：把每个 ADTS 头中的采样率索引改成另一个值
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc != 4) { fprintf(stderr, "用法: %s 输入.aac 输出.aac 采样率索引\n", argv[0]); return 1; }
    int index = atoi(argv[3]);
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    unsigned char buf[8192];
    long frames = 0;
    while (fread(buf, 1, 7, in) == 7) {
        int length = ((buf[3] & 3) << 11) | (buf[4] << 3) | (buf[5] >> 5);
        fread(buf + 7, 1, length - 7, in);
        buf[2] = (buf[2] & ~0x3C) | (index << 2);   // 第 3 个字节的中间 4 位
        fwrite(buf, 1, length, out);
        frames++;
    }
    printf("改了 %ld 帧\n", frames);
    fclose(in); fclose(out);
    return 0;
}
```

### 4.1 改成 44100

```bash
clang -O2 -o aac_setrate aac_setrate.c
./aac_setrate bbb.aac rate44k.aac 4
ffmpeg -i rate44k.aac -f s16le r44.pcm
```

解码没有任何错误，解出来的 PCM 和原文件大小相同（1925120 字节，481280 个采样帧），只是解码器认为它们是 44100 Hz 的。播放 `rate44k.aac`，时长变成 481280 / 44100 = 10.91 秒，声音变慢、音调降低了约 1.5 个半音（48000 / 44100 ≈ 1.088，log₂(1.088) × 12 ≈ 1.47）。和第 1 章中播放 PCM 时把采样率写错是一回事。

### 4.2 改成 24000

```bash
./aac_setrate bbb.aac rate24k.aac 6
ffmpeg -i rate24k.aac -f s16le r24.pcm
```

这次满屏都是错误，统计一下：

```
 468 Error submitting packet to decoder: Invalid data found when processing input
 247 [aac] Number of bands (N) exceeds limit (42).
 118 [aac] Number of bands (N) exceeds limit (41).
 112 [aac] invalid band type
  52 [aac] Number of bands (N) exceeds limit (40).
  12 [aac] Input buffer exhausted before END element found
```

470 帧中有 468 帧解码失败，`r24.pcm` 只有 8192 字节。为什么 44100 没事，24000 就不行？

AAC 编码时把频谱分成若干个**比例因子频带**（scalefactor band），每个频带用一个比例因子控制精度，频带怎么划分，由采样率决定（标准中每种采样率有一张表）。44100 和 48000 恰好共用同一张表，所以解码器按 44100 去解，频带划分完全相同，只是最后的采样率不同；24000 用的是另一张表，频带的个数和边界都不一样，码流中「第几个频带」对应的位置全乱了，解码器很快就发现数据不合法（「频带个数 49 超过了上限 42」）。

这说明 ADTS 头中的采样率不只是「播放时用多快的速度」，它还是解码本身所需要的参数。H.264 的 SPS 也是一样：SPS 错了，片就无法解析。

## 5. 从中间开始：同步字

ADTS 是为「流」设计的：广播、网络传输时，接收方可能从任何地方开始收到数据，必须能找到下一帧的开头。办法是查找同步字 `FFF`：

```c
// aac_resync.c：从任意偏移开始，找到下一个 ADTS 帧
#include <stdio.h>
#include <stdlib.h>

static int frame_len(const unsigned char *p)
{
    return ((p[3] & 3) << 11) | (p[4] << 3) | (p[5] >> 5);
}

int main(int argc, char *argv[])
{
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    long pos = atol(argv[2]), tried = 0;
    for (; pos + 7 <= size; pos++) {
        if (buf[pos] != 0xFF || (buf[pos + 1] & 0xF6) != 0xF0) continue;   // 同步字和 layer=00
        tried++;
        int len = frame_len(buf + pos);
        long next = pos + len;
        // 只看同步字可能是巧合：再检查下一帧的位置上是不是也是同步字
        if (len >= 7 && next + 2 <= size && buf[next] == 0xFF && (buf[next + 1] & 0xF6) == 0xF0) break;
    }
    long frames = 0;
    for (long p = pos; p + 7 <= size; p += frame_len(buf + p)) frames++;
    printf("从偏移 %s 开始，检查了 %ld 个候选同步字，在偏移 %ld 找到帧，之后还有 %ld 帧\n", argv[2], tried, pos, frames);
    free(buf);
    return 0;
}
```

AAC 的数据中也可能碰巧出现 `FF F`，只看同步字可能找错。程序多做了一步检查：按这个「头」中的帧长往后跳，如果那里也是同步字，才认为找对了。

```bash
clang -O2 -o aac_resync aac_resync.c
./aac_resync bbb.aac 100000
```

```
从偏移 100000 开始，检查了 1 个候选同步字，在偏移 100153 找到帧，之后还有 181 帧
```

从偏移 100000 开始，在偏移 100153 找到了帧，对照 `aac_adts` 的完整输出，这正是第 289 帧的开头，后面还有 181 帧（289 + 181 = 470）。这一次第一个候选就对了，数据中出现假同步字的概率并不高，但长时间运行的程序一定会遇到，所以这一步检查不能省。

MPEG-TS 也是同样的思路：每个包都以 `0x47` 开头，第 5 篇会看到。

## 6. MP4 中的 AAC：AudioSpecificConfig

MP4、MKV、FLV 中的 AAC **没有** ADTS 头：容器已经记录了每一帧的位置和长度，采样率、声道数这些对所有帧都一样的信息，只需要在头信息中存一份。这份信息叫 **AudioSpecificConfig**，FFmpeg 中存在 extradata 里：

```bash
ffprobe -v error -select_streams a -show_entries stream=extradata_size,extradata -show_data bbb-10s.mp4
```

```
extradata=
00000000: 1190 56e5 00                             ..V..

extradata_size=5
```

前两个字节 `11 90` 就是最基本的 AudioSpecificConfig：

```
0x11 0x90 = 00010 0011 0010 000
            │     │    │    └ 其他标志：0
            │     │    └ 声道配置 2：立体声
            │     └ 采样率索引 3：48000
            └ Audio Object Type 2：AAC-LC
```

和 ADTS 头中的字段几乎一一对应，只是档次直接写 Audio Object Type（2），不用减 1。后面的 `56 E5 00` 是一个扩展：同步标记 `0x2B7`，加上「Audio Object Type 5（SBR）：不使用」，明确告诉解码器这不是 HE-AAC，免得解码器去猜。

所以从 MP4 中提取 AAC 存成 `.aac` 文件时，FFmpeg 要根据 AudioSpecificConfig 给每一帧生成一个 ADTS 头；反过来，把 ADTS 流装进 MP4，要去掉每帧的头，生成一份 AudioSpecificConfig。下一篇从 FLV 中提取音频时，我们自己来做这件事。

### 6.1 多出来的 1280 个采样

第 1 章从 MP4 中解出的 PCM 是 1920000 字节（480000 个采样，正好 10 秒）。直接解码 `bbb.aac`：

```bash
ffmpeg -i bbb.aac -f s16le r48.pcm
ls -l r48.pcm
```

```
-rw-r--r--  1 lucky  staff  1925120 Oct 31 10:00 r48.pcm
```

1925120 字节 = 481280 个采样 = 470 帧 × 1024，比 MP4 多了 1280 个。

AAC 编码器有**延迟**：它对每一帧做的变换（MDCT）要用到前后相邻的数据，第一帧输出之前必须先「预热」，FFmpeg 的 AAC 编码器会在最前面多编码 1024 个采样（第 1 章第 4 篇看到的第一个音频包 PTS 是 −1024，带有 discard 标记，就是它）。最后一帧也不一定刚好填满 1024 个采样，不够的用静音补齐，这里补了 256 个。

MP4 中记录了这些信息（第一个包的时间戳是负数、编辑列表），播放器解码后会把多出来的部分丢掉，所以得到的正好是 480000 个采样。ADTS 中没有地方记录这件事，解码器只能把所有的采样都输出。音乐播放时开头多了 21 毫秒的静音，一般听不出来；但如果把几段 AAC 首尾拼接（例如无缝播放的专辑），接缝处就会出现一小段空白。

## 7. 小结

- AAC-LC 每帧 1024 个采样，帧长可变。ADTS 在每帧前加 7 字节的头，让 AAC 可以单独存放和传输。
- ADTS 头的主要字段：同步字 `FFF`、档次（减 1）、采样率索引、声道配置、帧长（含头）。
- 采样率不只决定播放速度，还决定频带划分，写错了可能根本无法解码。
- 同步字 + 帧长校验，可以从任意位置重新找到帧。
- MP4 等容器中的 AAC 没有 ADTS 头，参数集中放在 AudioSpecificConfig（通常 2 字节）中。
- AAC 编码器有 1024 个采样的延迟，ADTS 裸流解码会多出开头的预热采样和结尾的填充。

下一篇解析第一种封装格式：FLV，并从中取出 H.264 和 AAC。

## 练习

1. 测试视频第一帧的 AAC 数据中，有一段可读的文字（用 `head -c 301 bbb.aac | strings` 查看），是什么？
2. 写一个函数，根据采样率索引、声道数和 AAC 数据的长度生成 7 字节的 ADTS 头。用第一帧的参数（索引 3、2 声道、数据 294 字节）验证，结果应该是 `FF F1 4C 80 25 BF FC`。下一篇会用到它。
3. 为什么 `ffprobe bbb.aac` 显示的时长是 10.35 秒，而不是 10.03 秒？（提示：ADTS 中有没有记录总时长？）
4. 修改 `aac_adts.c`，统计每秒（每 47 帧左右）的码率，看看码率是怎样随时间变化的。
5. 用 `ffmpeg -i bbb-10s.mp4 -vn -c:a aac -ac 1 -b:a 64k mono.aac` 生成一个单声道的 AAC，用 `aac_adts` 查看，哪些字段变了？平均帧长是多少？

<details>
<summary>点击查看答案</summary>

1. 是 `Lavc63.1.102`，FFmpeg 编解码库 libavcodec 的版本号。FFmpeg 的 AAC 编码器在第一帧中插入了一个「填充元素」（fill element），内容就是编码器的名字和版本，解码器会忽略它。和 H.264 码流开头的 SEI 一样，编码器喜欢在码流中留下自己的签名。

2. 照着第 2 节的字段表，一个字节一个字节地拼：

   ```c
   // 生成 7 字节的 ADTS 头（AAC LC，无 CRC）
   static void make_adts_header(unsigned char h[7], int rate_index, int channels, int payload_len)
   {
       int len = payload_len + 7;                       // 帧长包括头本身
       h[0] = 0xFF;                                     // 同步字的前 8 位
       h[1] = 0xF1;                                     // 同步字后 4 位，ID=0，layer=00，protection_absent=1
       h[2] = (1 << 6) | (rate_index << 2) | (channels >> 2);   // profile=1（LC），采样率索引，声道数最高位
       h[3] = ((channels & 3) << 6) | (len >> 11);
       h[4] = (len >> 3) & 0xFF;
       h[5] = ((len & 7) << 5) | 0x1F;                  // 帧长低 3 位，缓冲区高 5 位（0x7FF）
       h[6] = 0xFC;                                     // 缓冲区低 6 位，raw_data_blocks=0
   }
   ```

   `make_adts_header(h, 3, 2, 294)` 输出 `FF F1 4C 80 25 BF FC`，和文件中的完全一样。声道数有 3 位，跨在第 3、4 两个字节之间；帧长 13 位跨了 3 个字节，是最容易写错的地方。

3. ADTS 中没有记录总时长，也没有帧数，要知道准确的时长只能把文件从头读到尾数帧（`ffprobe -count_frames` 就是这样做的，得到 470 帧）。`ffprobe` 默认不这样做，而是读开头的一些帧，估算一个码率（显示为 `bit_rate=126256`），再用文件大小除以码率：163271 × 8 / 126256 ≈ 10.35 秒。开头几帧的码率和整体平均值有差别，估算就不准。MP4 中有每一帧的时间信息，时长是精确的；FLV 头信息中的 `duration` 也是写入时记录的。这也是为什么裸流适合传输，但不适合当作存放用的文件格式。

4. 在循环中按帧累加，每 47 帧（约 1.003 秒）输出一次：

   ```c
   sec_bytes += length;
   if (++sec_frames == 47) {
       printf("第 %2ld 秒：%.1f kbps\n", frames / 47, sec_bytes * 8 / (47 * 1024.0 / rate) / 1000);
       sec_bytes = 0; sec_frames = 0;
   }
   ```

   ```
   第  0 秒：125.8 kbps
   第  1 秒：131.0 kbps
   第  2 秒：130.7 kbps
   ……
   第  9 秒：130.4 kbps
   ```

   除了第 0 秒稍低（编码器刚开始时比较保守），其余每秒都在 130.4～131.3 kbps 之间（含 ADTS 头，去掉头约 128 kbps），非常稳定。单帧的长度从 281 到 415 字节相差将近一半，但按秒统计几乎是恒定的：AAC 编码器用一个「比特池」（bit reservoir）在相邻帧之间调剂，复杂的帧多用一点，简单的帧省下来，长时间的平均值保持在设定的 128 kbps。

5. 输出：

   ```
       0       0   212    LC   48000     1  0x7FF
   ...
   共 470 帧，83885 字节，时长 10.027 秒，平均码率 66.9 kbps
   帧长：最小 149，最大 214，平均 178.5 字节（其中 ADTS 头 7 字节）
   ```

   只有声道配置从 2 变成了 1，采样率索引仍然是 3（48000），帧数也还是 470（帧数只取决于时长和采样率）。估算一下：64 kbps 时每秒约 46.9 帧，每帧 64000 / 8 / 46.9 ≈ 171 字节，加上 7 字节的头是 178 字节，和实测的平均 178.5 字节一致。帧长在 149～214 字节之间，波动范围也比立体声小。单声道只编码一个声道，用一半的码率就能得到和立体声相近的音质，语音通话常用单声道。

</details>
