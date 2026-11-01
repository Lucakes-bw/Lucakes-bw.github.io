---
title: RTP 与 RTSP
date: 2026-11-03 10:00:00
updated: 2026-11-03 10:00:00
categories:
  - 音视频开发
  - 采集与流媒体
tags:
  - 音视频
  - 流媒体
  - RTSP
  - RTP
  - H.264
  - C
order: 2
description: 不用 FFmpeg，用 200 行 C 代码写一个 RTSP 客户端：OPTIONS、DESCRIBE、SETUP、PLAY 四步建立会话，从 SDP 中取出 Base64 编码的 SPS 和 PPS，再从 TCP 连接上收 RTP 包，按 RFC 6184 把单个 NAL、STAP-A 和 FU-A 还原成 Annex B 码流。实测 12 秒收到 2320 个 RTP 包，96% 是 FU-A 分片，最大 1452 字节；RTP 时间戳就是 PTS，遇到 B 帧会往回走。还原出的 276 帧和原文件解码结果逐帧相同。模拟丢包 0.5%，210 帧里只有 59 帧正确，一个包丢了，错误一直延续到下一个关键帧。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：了解流媒体系统的基本结构；理解 RTSP 怎样建立会话、SDP 描述了什么；理解 RTP 头每个字段的作用；掌握 H.264 的 RTP 打包方式（单个 NAL、STAP-A、FU-A），能自己把 RTP 包还原成 H.264 码流；理解丢包对视频的影响。
>
> **前置知识**：上一篇；第 2 章《H.264 码流》中的 NAL 单元、Annex B、IDR 和错误传播；网络专栏中的《UDP》和《TCP Socket 入门》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，MediaMTX 1.21.3。

## 1. 流媒体系统的结构

前四章处理的都是本地文件，上一篇采集的画面也只是存进了文件。直播不一样，数据是一边产生一边送出去的。一个直播系统通常有三个角色：

<figure class="diagram">
<svg viewBox="0 0 680 200" role="img" aria-label="流媒体系统的结构：推流端（摄像头、编码器）把流推给服务器，常用 RTMP、RTSP、SRT、WebRTC；服务器转发给多个拉流端（播放器），拉流可以用 RTMP、RTSP、HLS、SRT、WebRTC，服务器在中间转换协议">
  <defs>
    <marker id="rt-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="70" width="140" height="60" rx="6" style="fill:var(--panel-2)"/>
      <rect x="270" y="55" width="140" height="90" rx="6" style="fill:var(--mark)"/>
      <rect x="535" y="10" width="140" height="44" rx="6" style="fill:var(--panel-2)"/>
      <rect x="535" y="78" width="140" height="44" rx="6" style="fill:var(--panel-2)"/>
      <rect x="535" y="146" width="140" height="44" rx="6" style="fill:var(--panel-2)"/>
    </g>
    <text x="15" y="95" style="fill:var(--fg)">推流端</text><text x="15" y="113" style="fill:var(--muted);font-size:10px">采集 + 编码</text>
    <text x="280" y="82" style="fill:var(--fg)">流媒体服务器</text><text x="280" y="100" style="fill:var(--muted);font-size:10px">MediaMTX、SRS</text><text x="280" y="116" style="fill:var(--muted);font-size:10px">转发、转协议</text><text x="280" y="132" style="fill:var(--muted);font-size:10px">切片、录制</text>
    <text x="545" y="30" style="fill:var(--fg)">播放器 1</text><text x="545" y="46" style="fill:var(--muted);font-size:10px">RTSP</text>
    <text x="545" y="98" style="fill:var(--fg)">浏览器</text><text x="545" y="114" style="fill:var(--muted);font-size:10px">HLS、WebRTC</text>
    <text x="545" y="166" style="fill:var(--fg)">播放器 2</text><text x="545" y="182" style="fill:var(--muted);font-size:10px">RTMP、SRT</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#rt-arrow)">
      <line x1="145" y1="100" x2="268" y2="100"/>
      <path d="M410,85 L470,32 L533,32"/><line x1="410" y1="100" x2="533" y2="100"/><path d="M410,115 L470,168 L533,168"/>
    </g>
    <text x="160" y="90" style="fill:var(--muted);font-size:10px">推流（上行）</text>
    <text x="160" y="120" style="fill:var(--muted);font-size:10px">RTMP、RTSP</text><text x="160" y="134" style="fill:var(--muted);font-size:10px">SRT、WebRTC</text>
    <text x="478" y="150" style="fill:var(--muted);font-size:10px">拉流（下行）</text>
  </g>
</svg>
</figure>

- **推流端**：采集画面和声音，编码，按某种协议发给服务器。手机直播 App、OBS、监控摄像头都是推流端。
- **服务器**：接收推上来的流，转发给所有观看的人。它往往还要**转换协议**：推流用 RTMP，观众用浏览器看 HLS 或 WebRTC。
- **拉流端**：播放器，从服务器拉流、解码、显示。

本章用开源的 [MediaMTX](https://github.com/bluenviron/mediamtx) 做服务器，它一个程序就支持 RTSP、RTMP、HLS、SRT、WebRTC，直接 `brew install mediamtx` 安装，用默认配置启动即可：

```bash
mediamtx                     # RTSP 8554，RTMP 1935，HLS 8888，WebRTC 8889，SRT 8890
```

本篇先看最早的流媒体协议之一：**RTSP 加 RTP**。监控摄像头几乎都支持它，`rtsp://` 开头的地址大家可能都见过。先用 FFmpeg 把测试视频循环推到服务器上（`-re` 表示按实际速度读，模拟直播）：

```bash
ffmpeg -re -stream_loop -1 -i bbb-10s.mp4 -an -c copy -f rtsp rtsp://127.0.0.1:8554/file
```

## 2. RTSP：建立会话

RTSP（Real Time Streaming Protocol，RFC 2326）只负责「控制」：告诉服务器我要哪路流、用什么方式传、开始播放、暂停、结束。真正的音视频数据走 **RTP**（Real-time Transport Protocol，RFC 3550）。打个比方，RTSP 是遥控器，RTP 是电视信号。

RTSP 的格式和 HTTP 很像：文本的请求行和头部，每个请求带一个递增的 `CSeq`，响应用同样的 `CSeq` 对应起来。下面是第 5 节的程序和 MediaMTX 之间真实的对话（`>>>` 是客户端发出的，`<<<` 是服务器的响应）：

```text
>>> OPTIONS rtsp://127.0.0.1:8554/file RTSP/1.0
CSeq: 1

<<< RTSP/1.0 200 OK
CSeq: 1
Public: DESCRIBE, ANNOUNCE, SETUP, PLAY, RECORD, PAUSE, GET_PARAMETER, TEARDOWN
Server: gortsplib

>>> DESCRIBE rtsp://127.0.0.1:8554/file RTSP/1.0
CSeq: 2
Accept: application/sdp

<<< RTSP/1.0 200 OK
CSeq: 2
Content-Base: rtsp://127.0.0.1:8554/file/
Content-Length: 263
Content-Type: application/sdp
Server: gortsplib

v=0
o=- 0 0 IN IP4 127.0.0.1
s=No Name
c=IN IP4 0.0.0.0
t=0 0
m=video 0 RTP/AVP 96
a=control:trackID=0
a=rtpmap:96 H264/90000
a=fmtp:96 packetization-mode=1; profile-level-id=64001F; sprop-parameter-sets=Z2QAH6zZQFAFumoCAgNuAAADAAIAAAMAYB4wYyw=,aOvjyyLA

>>> SETUP rtsp://127.0.0.1:8554/file/trackID=0 RTSP/1.0
CSeq: 3
Transport: RTP/AVP/TCP;unicast;interleaved=0-1

<<< RTSP/1.0 200 OK
CSeq: 3
Server: gortsplib
Session: 85936dd093ab4dada744bda26f712dea;timeout=55
Transport: RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=225AC35E

>>> PLAY rtsp://127.0.0.1:8554/file RTSP/1.0
CSeq: 4
Session: 85936dd093ab4dada744bda26f712dea
Range: npt=0.000-

<<< RTSP/1.0 200 OK
CSeq: 4
RTP-Info: url=rtsp://127.0.0.1:8554/file/trackID=0;seq=42135;rtptime=2281317670
Server: gortsplib
Session: 85936dd093ab4dada744bda26f712dea;timeout=55
```

四个请求各有分工：

1. **OPTIONS**：问服务器支持哪些方法。可以省略，但大多数客户端都会先发。
2. **DESCRIBE**：取回这路流的描述。正文是 **SDP**（Session Description Protocol，RFC 8866）格式，一行一个 `类型=值`：
   - `m=video 0 RTP/AVP 96`：有一路视频，用 RTP 传，**负载类型**（payload type）是 96。96～127 是动态类型，具体是什么编码由下一行说明。
   - `a=rtpmap:96 H264/90000`：96 号是 H.264，RTP 时间戳的时钟频率是 90000 Hz（视频统一用 90 kHz）。
   - `a=fmtp:96 ...`：H.264 的参数。`packetization-mode=1` 表示会用到第 4 节的 STAP-A 和 FU-A；`profile-level-id=64001F` 是档次和级别（64 是 High，1F 是 3.1）；`sprop-parameter-sets` 是 Base64 编码的 SPS 和 PPS，用逗号隔开。播放器不用等码流中的 SPS、PPS，拿到 SDP 就能初始化解码器。
   - `a=control:trackID=0`：这路流的控制地址，相对于响应头中的 `Content-Base`，所以完整地址是 `rtsp://127.0.0.1:8554/file/trackID=0`。
3. **SETUP**：对每一路流（视频、音频分别 SETUP）约定传输方式，服务器返回一个 `Session`，之后的请求都要带上它。
4. **PLAY**：开始发数据。响应中的 `RTP-Info` 告诉客户端第一个 RTP 包的序号和时间戳，方便把 RTP 时间换算成播放位置。

结束时发 **TEARDOWN**，暂停用 **PAUSE**。

### 2.1 RTP 走 UDP 还是 TCP

`SETUP` 中的 `Transport` 头决定 RTP 怎么传，有两种常见的方式：

- **UDP**：`Transport: RTP/AVP;unicast;client_port=5000-5001`。客户端开两个 UDP 端口，偶数端口收 RTP，下一个奇数端口收 **RTCP**（RTP 的控制协议，用来报告收发统计、做音视频同步）。延迟低，但会丢包，还常常被 NAT 和防火墙挡住。
- **TCP 交错**（interleaved）：`Transport: RTP/AVP/TCP;unicast;interleaved=0-1`。RTP 和 RTCP 都塞进 RTSP 这个 TCP 连接，每个包前面加 4 个字节：`$`、通道号（这里 RTP 是 0，RTCP 是 1）、2 字节的长度。不丢包，穿透性好，但网络差的时候会因为重传而积累延迟（下一篇详细测）。

FFmpeg 默认先尝试 UDP，不通再换 TCP；本篇的程序为了简单只用 TCP。

## 3. RTP 头

不管走 UDP 还是 TCP，每个 RTP 包都以一个 12 字节的固定头开始：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="RTP 固定头的格式，每行 32 位：第一行依次是 2 位版本号 V、1 位填充 P、1 位扩展 X、4 位 CSRC 个数 CC、1 位标记 M、7 位负载类型 PT、16 位序号；第二行是 32 位时间戳；第三行是 32 位同步源标识 SSRC">
  <g style="font-family:var(--mono);font-size:12px">
    <g style="fill:var(--muted);font-size:10px">
      <text x="40" y="14">0</text><text x="195" y="14">8</text><text x="350" y="14">16</text><text x="505" y="14">24</text><text x="650" y="14">31</text>
    </g>
    <g style="stroke:var(--border-2)">
      <rect x="40" y="20" width="38.75" height="40" style="fill:var(--mark)"/>
      <rect x="78.75" y="20" width="19.4" height="40" style="fill:var(--panel-2)"/>
      <rect x="98.1" y="20" width="19.4" height="40" style="fill:var(--panel-2)"/>
      <rect x="117.5" y="20" width="77.5" height="40" style="fill:var(--panel-2)"/>
      <rect x="195" y="20" width="19.4" height="40" style="fill:var(--accent-soft)"/>
      <rect x="214.4" y="20" width="135.6" height="40" style="fill:var(--mark)"/>
      <rect x="350" y="20" width="310" height="40" style="fill:var(--accent-soft)"/>
      <rect x="40" y="60" width="620" height="40" style="fill:var(--mark)"/>
      <rect x="40" y="100" width="620" height="40" style="fill:var(--panel-2)"/>
    </g>
    <text x="50" y="45" style="fill:var(--fg)">V=2</text>
    <text x="83" y="45" style="fill:var(--fg)">P</text><text x="102" y="45" style="fill:var(--fg)">X</text>
    <text x="140" y="45" style="fill:var(--fg)">CC</text>
    <text x="199" y="45" style="fill:var(--fg)">M</text>
    <text x="230" y="45" style="fill:var(--fg)">PT 负载类型</text>
    <text x="440" y="45" style="fill:var(--fg)">序号（sequence）</text>
    <text x="270" y="85" style="fill:var(--fg)">时间戳（timestamp）</text>
    <text x="250" y="125" style="fill:var(--fg)">同步源（SSRC）</text>
    <text x="40" y="160" style="fill:var(--muted);font-size:11px">之后：CC 个 CSRC（每个 4 字节）、扩展头（X=1 时）、负载，最后是填充（P=1 时）</text>
    <text x="40" y="178" style="fill:var(--muted);font-size:11px">所有多字节字段都是大端序</text>
  </g>
</svg>
</figure>

- **V**：版本，固定是 2。
- **P、X、CC**：是否有填充、是否有扩展头、CSRC 的个数。混音的时候才会有 CSRC，通常 CC 是 0。
- **M**：标记位。对视频，表示这是**一帧的最后一个包**；收到 M=1，就知道一帧收齐了。
- **PT**：负载类型，和 SDP 中的 96 对应。
- **序号**：每发一个包加 1，到 65535 后回到 0。接收端靠它发现丢包和乱序（UDP 不保证顺序）。
- **时间戳**：这个包里的数据的**采样时间**，单位由 SDP 中的时钟频率决定，视频是 1/90000 秒。同一帧拆成的多个包，时间戳相同。起始值是随机的，只有差值有意义。
- **SSRC**：同步源标识，一路流一个随机数。服务器在 `SETUP` 的响应中也告诉了我们：`ssrc=225AC35E`。

和第 2 章的封装格式比一比：FLV 和 MP4 的时间戳以毫秒或者 time_base 为单位，记录 DTS 和 PTS；RTP 只有一个时间戳，它表示的是**采样时间，也就是 PTS**。没有 DTS，解码顺序就靠序号：包按解码顺序发送。

## 4. H.264 怎样装进 RTP

RTP 包要装进一个 UDP 数据报，不能超过网络的 MTU。以太网的 MTU 是 1500 字节，减去 IPv6 头 40 字节、UDP 头 8 字节，剩下 1452 字节，这就是 MediaMTX 默认的 RTP 包大小上限。而一个 720p 的 I 帧有几十 KB，一个 P 帧可能只有几百字节，所以 H.264 的 RTP 打包规范（RFC 6184）定义了三种方式。它们都借用了 NAL 头的格式：负载的第一个字节，低 5 位是「类型」，1～23 是普通的 NAL 类型，24 以上是打包方式。

- **单个 NAL 单元**（类型 1～23）：一个 NAL 正好装得下，就原样放进去，不带起始码。
- **STAP-A**（类型 24）：把几个小 NAL 装进一个包，每个前面加 2 字节的长度。SPS、PPS 这种几十字节的 NAL 常这样发。
- **FU-A**（类型 28）：一个 NAL 太大，拆成多个分片，每个分片前面有 2 个字节：

<figure class="diagram">
<svg viewBox="0 0 680 200" role="img" aria-label="FU-A 分片：原 NAL 的头是 F、NRI 和类型 5；拆分后每个分片的第一个字节是 FU 指示，保留原来的 F 和 NRI，类型改成 28；第二个字节是 FU 头，S 位表示第一个分片，E 位表示最后一个分片，低 5 位是原来的类型 5；之后是原 NAL 去掉头之后的数据片段">
  <defs>
    <marker id="fu-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <text x="5" y="30" style="fill:var(--fg)">原 NAL</text>
    <g style="stroke:var(--border-2)">
      <rect x="80" y="12" width="90" height="28" style="fill:var(--mark)"/>
      <rect x="170" y="12" width="500" height="28" style="fill:var(--panel-2)"/>
    </g>
    <text x="86" y="31" style="fill:var(--fg);font-size:11px">0 11 00101</text>
    <text x="300" y="31" style="fill:var(--muted)">NAL 数据（IDR 的切片，几十 KB）</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#fu-arrow)" stroke-dasharray="3 3">
      <line x1="250" y1="42" x2="200" y2="74"/><line x1="420" y1="42" x2="420" y2="74"/><line x1="600" y1="42" x2="620" y2="74"/>
    </g>
    <g style="stroke:var(--border-2)">
      <rect x="20" y="78" width="70" height="28" style="fill:var(--accent-soft)"/><rect x="90" y="78" width="70" height="28" style="fill:var(--accent-soft)"/><rect x="160" y="78" width="110" height="28" style="fill:var(--panel-2)"/>
      <rect x="290" y="78" width="70" height="28" style="fill:var(--accent-soft)"/><rect x="360" y="78" width="70" height="28" style="fill:var(--accent-soft)"/><rect x="430" y="78" width="80" height="28" style="fill:var(--panel-2)"/>
      <rect x="530" y="78" width="45" height="28" style="fill:var(--accent-soft)"/><rect x="575" y="78" width="45" height="28" style="fill:var(--accent-soft)"/><rect x="620" y="78" width="50" height="28" style="fill:var(--panel-2)"/>
    </g>
    <text x="25" y="97" style="font-size:9px;fill:var(--fg)">0 11 11100</text><text x="94" y="97" style="font-size:9px;fill:var(--fg)">1 0 0 00101</text><text x="175" y="97" style="fill:var(--muted);font-size:11px">片段 1</text>
    <text x="295" y="97" style="font-size:9px;fill:var(--fg)">0 11 11100</text><text x="364" y="97" style="font-size:9px;fill:var(--fg)">0 0 0 00101</text><text x="440" y="97" style="fill:var(--muted);font-size:11px">片段 2</text>
    <text x="533" y="97" style="font-size:9px;fill:var(--fg)">…11100</text><text x="578" y="97" style="font-size:9px;fill:var(--fg)">01…</text><text x="625" y="97" style="fill:var(--muted);font-size:10px">片段 n</text>
    <text x="20" y="124" style="fill:var(--muted);font-size:10px">FU 指示</text><text x="95" y="124" style="fill:var(--muted);font-size:10px">FU 头 S=1</text>
    <text x="290" y="124" style="fill:var(--muted);font-size:10px">FU 指示</text><text x="365" y="124" style="fill:var(--muted);font-size:10px">中间分片</text>
    <text x="580" y="124" style="fill:var(--muted);font-size:10px">E=1</text>
    <text x="20" y="152" style="fill:var(--fg);font-size:11px">FU 指示 = 原 NAL 头的 F、NRI（高 3 位）+ 类型 28</text>
    <text x="20" y="172" style="fill:var(--fg);font-size:11px">FU 头   = S（开始）、E（结束）、R（保留）+ 原 NAL 的类型（低 5 位）</text>
    <text x="20" y="192" style="fill:var(--muted);font-size:11px">接收端：S=1 时用 (FU 指示 &amp; 0xE0) | (FU 头 &amp; 0x1F) 还原 NAL 头，把片段依次拼起来，E=1 时一个 NAL 就完整了</text>
  </g>
</svg>
</figure>

注意原 NAL 的头不单独传：它的 F、NRI 放在 FU 指示里，类型放在 FU 头里，接收端拼回去就行，每个分片因此省下一个字节。

## 5. 自己写一个 RTSP 客户端

下面的程序不依赖任何库，只用 socket：完成第 2 节的四步，然后在 TCP 连接上收交错帧，解析 RTP 头，按第 4 节的规则把 H.264 还原成 Annex B 格式（每个 NAL 前面加起始码 `00 00 00 01`）写进文件。可以给它一个丢包率，模拟网络丢包。

```c
// rtsp_h264.c：最简单的 RTSP 客户端，不依赖 FFmpeg
// 用 TCP 交错模式拉流，解析 RTP 头，把 H.264 的 RTP 包还原成 Annex B 码流
// 用法：rtsp_h264 rtsp://127.0.0.1:8554/live/test out.h264 [秒数] [丢包率%]
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int sock, cseq = 0;
static char session[128] = "", url[512], base[512];
static FILE *out;

// 发一个请求，读回响应头（以及正文），打印出来；wait 为 0 时只发不收
static int request(const char *method, const char *uri, const char *extra, char *body, int bodycap, int wait) {
    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "%s %s RTSP/1.0\r\nCSeq: %d\r\n", method, uri, ++cseq);
    if (session[0]) n += snprintf(buf + n, sizeof(buf) - n, "Session: %s\r\n", session);
    n += snprintf(buf + n, sizeof(buf) - n, "%s\r\n", extra);  // extra 是额外的头，每行以 \r\n 结尾
    printf(">>> %s", buf);
    send(sock, buf, n, 0);
    if (!wait) return 0;

    // 响应头以空行结束
    int len = 0;
    while (len < (int)sizeof(buf) - 1 && recv(sock, buf + len, 1, 0) == 1) {
        len++;
        if (len >= 4 && !memcmp(buf + len - 4, "\r\n\r\n", 4)) break;
    }
    buf[len] = 0;
    printf("<<< %s", buf);
    int code = 0;
    sscanf(buf, "RTSP/1.0 %d", &code);
    char *p = strcasestr(buf, "\nSession:");
    if (p) sscanf(p + 9, " %127[^;\r\n]", session);
    p = strcasestr(buf, "\nContent-Base:");
    if (p) sscanf(p + 14, " %511[^\r\n]", base);
    p = strcasestr(buf, "\nContent-Length:");
    int clen = p ? atoi(p + 16) : 0;
    if (body) {
        int got = 0;
        while (got < clen && got < bodycap - 1) {
            int r = recv(sock, body + got, clen - got, 0);
            if (r <= 0) break;
            got += r;
        }
        body[got] = 0;
        printf("%s\n", body);
    }
    return code;
}

// SDP 中的 sprop-parameter-sets 是 Base64 编码的 SPS 和 PPS
static int b64(const char *in, uint8_t *o) {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int v = 0, bits = 0, n = 0;
    for (; *in && *in != ',' && *in != ';' && *in != '\r'; in++) {
        const char *c = strchr(t, *in);
        if (!c || *in == '=') continue;
        v = (v << 6) | (int)(c - t);
        bits += 6;
        if (bits >= 8) { bits -= 8; o[n++] = (v >> bits) & 0xff; }
    }
    return n;
}

static const uint8_t sc[4] = {0, 0, 0, 1};
static void put_nal(const uint8_t *p, int n) { fwrite(sc, 1, 4, out); fwrite(p, 1, n, out); }

// 统计
static long pkts, single, stapa, fua, nals, dropped_nal, lost, maxsize, frames;

// FU-A 分片的重组缓冲区
static uint8_t fu[4 << 20];
static int fu_len = -1;  // -1 表示当前没有在拼的 NAL

static void depacketize(const uint8_t *p, int n, int gap) {
    if (gap && fu_len >= 0) { dropped_nal++; fu_len = -1; }  // 中间丢了包，正在拼的 NAL 不完整，扔掉
    int type = p[0] & 0x1f;
    if (type >= 1 && type <= 23) {          // 单个 NAL 单元，原样输出
        single++; nals++;
        put_nal(p, n);
    } else if (type == 24) {                // STAP-A：一个包里装了几个小 NAL
        stapa++;
        for (int i = 1; i + 2 <= n;) {
            int sz = p[i] << 8 | p[i + 1];
            i += 2;
            if (i + sz > n) break;
            put_nal(p + i, sz);
            nals++;
            i += sz;
        }
    } else if (type == 28) {                // FU-A：一个大 NAL 拆成多个包
        fua++;
        int start = p[1] & 0x80, end = p[1] & 0x40;
        if (start) {
            fu[0] = (p[0] & 0xe0) | (p[1] & 0x1f);  // 用 FU 指示字节的高 3 位和 FU 头的低 5 位还原 NAL 头
            fu_len = 1;
        }
        if (fu_len < 0) return;             // 没收到开头的分片，整个 NAL 都没法用
        memcpy(fu + fu_len, p + 2, n - 2);
        fu_len += n - 2;
        if (end) { put_nal(fu, fu_len); nals++; fu_len = -1; }
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "用法: %s rtsp://主机:端口/路径 out.h264 [秒数] [丢包率%%]\n", argv[0]); return 1; }
    int seconds = argc > 3 ? atoi(argv[3]) : 10;
    double loss = argc > 4 ? atof(argv[4]) / 100 : 0;
    snprintf(url, sizeof(url), "%s", argv[1]);
    char host[256], port[16] = "554";
    if (sscanf(url, "rtsp://%255[^:/]:%15[0-9]", host, port) < 1) return 1;
    out = fopen(argv[2], "wb");

    struct addrinfo hints = {.ai_socktype = SOCK_STREAM}, *ai;
    if (getaddrinfo(host, port, &hints, &ai) || (sock = socket(ai->ai_family, SOCK_STREAM, 0)) < 0 ||
        connect(sock, ai->ai_addr, ai->ai_addrlen) < 0) { perror("connect"); return 1; }

    // 1. OPTIONS：服务器支持哪些方法
    request("OPTIONS", url, "", NULL, 0, 1);
    // 2. DESCRIBE：取回 SDP，知道有几路流、什么编码
    char sdp[8192];
    if (request("DESCRIBE", url, "Accept: application/sdp\r\n", sdp, sizeof(sdp), 1) != 200) return 1;
    int pt = 96;
    char *m = strstr(sdp, "m=video");
    if (m) sscanf(m, "m=video %*d %*s %d", &pt);
    char ctrl[256] = "", track[600];
    char *c = m ? strstr(m, "a=control:") : NULL;
    if (c) sscanf(c + 10, "%255[^\r\n]", ctrl);
    // a=control 可能是完整 URL，也可能是相对于 Content-Base 的路径
    if (!base[0]) snprintf(base, sizeof(base), "%s/", url);
    if (!strncmp(ctrl, "rtsp://", 7)) snprintf(track, sizeof(track), "%s", ctrl);
    else snprintf(track, sizeof(track), "%s%s", base, ctrl);
    char *sp = m ? strstr(m, "sprop-parameter-sets=") : NULL;
    if (sp) {                               // 先把 SPS、PPS 写进输出文件
        uint8_t ps[512];
        sp += 21;
        int k = b64(sp, ps); put_nal(ps, k);
        sp = strchr(sp, ',');
        if (sp) { k = b64(sp + 1, ps); put_nal(ps, k); }
    }
    // 3. SETUP：约定传输方式。这里让 RTP 和 RTCP 走同一个 TCP 连接，通道号 0 和 1
    request("SETUP", track, "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n", NULL, 0, 1);
    // 4. PLAY：开始发数据
    request("PLAY", url, "Range: npt=0.000-\r\n", NULL, 0, 1);

    srand(1);
    time_t t_end = time(NULL) + seconds;
    int last_seq = -1, printed = 0;
    uint32_t last_ts = 0;
    uint8_t pkt[65536];
    while (time(NULL) < t_end) {
        // 交错帧：'$'、通道号（1 字节）、长度（2 字节），后面是一个 RTP 或 RTCP 包
        uint8_t h[4];
        if (recv(sock, h, 1, MSG_WAITALL) != 1) break;
        if (h[0] != '$') continue;
        if (recv(sock, h + 1, 3, MSG_WAITALL) != 3) break;
        int ch = h[1], len = h[2] << 8 | h[3];
        if (recv(sock, pkt, len, MSG_WAITALL) != len) break;
        if (ch != 0 || len < 12) continue;  // 通道 1 是 RTCP，这里不处理

        // RTP 固定头 12 字节
        int ver = pkt[0] >> 6, padding = pkt[0] & 0x20, ext = pkt[0] & 0x10, cc = pkt[0] & 0x0f;
        int marker = pkt[1] >> 7, ptype = pkt[1] & 0x7f;
        int seq = pkt[2] << 8 | pkt[3];
        uint32_t ts = (uint32_t)pkt[4] << 24 | pkt[5] << 16 | pkt[6] << 8 | pkt[7];
        uint32_t ssrc = (uint32_t)pkt[8] << 24 | pkt[9] << 16 | pkt[10] << 8 | pkt[11];
        int off = 12 + cc * 4;
        if (ext) off += 4 + (pkt[off + 2] << 8 | pkt[off + 3]) * 4;
        int end = len - (padding ? pkt[len - 1] : 0);
        if (ver != 2 || ptype != pt || off >= end) continue;

        pkts++;
        if (len > maxsize) maxsize = len;
        if (loss > 0 && rand() < loss * RAND_MAX) continue;   // 模拟丢包
        int gap = last_seq >= 0 && seq != ((last_seq + 1) & 0xffff);
        if (gap) lost += (seq - last_seq - 1) & 0xffff;
        if (printed < 12) {
            int nt = pkt[off] & 0x1f;
            printf("seq=%5d ts=%10u(%+6d) M=%d ssrc=%08x 长度=%5d 类型=%2d%s\n", seq, ts,
                   last_seq >= 0 ? (int32_t)(ts - last_ts) : 0, marker, ssrc, len, nt,
                   nt == 28 ? ((pkt[off + 1] & 0x80) ? " FU-A 开始" : (pkt[off + 1] & 0x40) ? " FU-A 结束" : " FU-A 中间") :
                   nt == 24 ? " STAP-A" : "");
            printed++;
        }
        last_seq = seq;
        last_ts = ts;
        depacketize(pkt + off, end - off, gap);
        if (marker) frames++;               // M 位：一帧的最后一个包
    }
    request("TEARDOWN", url, "", NULL, 0, 0);  // 数据还在源源不断地来，不等响应了
    fclose(out);
    printf("RTP 包 %ld 个（最大 %ld 字节）：单个 NAL %ld、STAP-A %ld、FU-A %ld；"
           "还原出 NAL %ld 个、帧 %ld 帧；丢包 %ld 个，扔掉不完整的 NAL %ld 个\n",
           pkts, maxsize, single, stapa, fua, nals, frames, lost, dropped_nal);
    return 0;
}
```

几个要点：

- **SPS、PPS 来自 SDP**：程序先把 `sprop-parameter-sets` 解码后写在文件开头。很多摄像头只在 SDP 中给出 SPS、PPS，码流里不再重复，如果不写进去，解码器就无法开始解码。
- **FU-A 丢了一片，整个 NAL 都要扔掉**：拼了一半的 NAL 送给解码器，只会得到更糟的结果。程序用序号发现丢包（`gap`），扔掉正在拼的 NAL；如果丢的是开头的分片，后面的分片也没法用。
- **TCP 上的接收**：交错帧是连续的字节流，一次 `recv` 不一定正好收到一个完整的包，所以每次都用 `MSG_WAITALL` 收够指定的字节数。
- 为了让代码短一些，程序没有处理 RTCP、多路流、认证和 UDP 传输，也没有给乱序的包重新排序（TCP 不会乱序；用 UDP 时就必须按序号排序）。

编译运行：

```bash
clang -O2 rtsp_h264.c -o rtsp_h264
./rtsp_h264 rtsp://127.0.0.1:8554/file got.h264 12
```

除了前面的 RTSP 对话，程序还打印了前 12 个 RTP 包：

```text
seq=42135 ts=2281325178(    +0) M=1 ssrc=225ac35e 长度=  693 类型= 1
seq=42136 ts=2281321428( -3750) M=1 ssrc=225ac35e 长度=  518 类型= 1
seq=42137 ts=2281336428(+15000) M=1 ssrc=225ac35e 长度=  676 类型= 1
seq=42138 ts=2281328928( -7500) M=1 ssrc=225ac35e 长度=  557 类型= 1
seq=42139 ts=2281332678( +3750) M=1 ssrc=225ac35e 长度=  288 类型= 1
seq=42140 ts=2281347678(+15000) M=0 ssrc=225ac35e 长度= 1452 类型=28 FU-A 开始
seq=42141 ts=2281347678(    +0) M=0 ssrc=225ac35e 长度= 1452 类型=28 FU-A 中间
seq=42142 ts=2281347678(    +0) M=1 ssrc=225ac35e 长度=  283 类型=28 FU-A 结束
seq=42143 ts=2281340178( -7500) M=1 ssrc=225ac35e 长度=  921 类型= 1
seq=42144 ts=2281343928( +3750) M=1 ssrc=225ac35e 长度=  517 类型= 1
seq=42145 ts=2281358928(+15000) M=1 ssrc=225ac35e 长度= 1123 类型= 1
seq=42146 ts=2281351428( -7500) M=1 ssrc=225ac35e 长度=  699 类型= 1
RTP 包 2320 个（最大 1452 字节）：单个 NAL 82、STAP-A 8、FU-A 2230；还原出 NAL 305 个、帧 288 帧；丢包 0 个，扔掉不完整的 NAL 0 个
```

从中可以读出不少东西：

- **序号连续加 1**，TCP 上没有丢包。
- **小的帧一个包就装下了**（类型 1，非 IDR 的切片），M 都是 1：一个包就是一帧。大一些的帧拆成 FU-A，只有最后一个分片 M=1。
- **时间戳一会儿加、一会儿减**。测试视频是 24 帧/秒，一帧是 90000 / 24 = 3750 个单位。+15000 是往后跳了 4 帧，-7500、+3750 又回来：这是 B 帧。包按解码顺序发送，而时间戳是 PTS，所以会往回走。第 2 章《时间戳》中看到的 DTS、PTS 的关系，在这里完全一样，只是接收端要自己从这些时间戳中恢复出顺序。
- 12 秒收到 2320 个包，**96% 是 FU-A 分片**；STAP-A 有 8 个，和这段时间里关键帧的个数相同。打印出来看，每个 STAP-A 里都是一个 29 字节的 SPS 和一个 6 字节的 PPS（有的还带着一个 SEI），放在关键帧前面。

还原出的码流能直接播放。为了验证程序是否正确，用 FFmpeg 解码，把每一帧的 MD5 和原文件解码出的每一帧比较：

```bash
ffmpeg -i bbb-10s.mp4 -an -f framemd5 src.md5
ffmpeg -i got.h264 -f framemd5 got.md5
```

`got.md5` 中的 276 帧，每一帧都能在 `src.md5` 中找到相同的 MD5：解出的画面和原文件**逐字节相同**。收到了 288 帧却只解出 276 帧，是因为我们连上服务器的时候，直播正进行到一个 GOP 的中间，服务器从当前位置开始发，解码器要等到第一个关键帧才能开始输出。这也是直播「打开慢」的原因之一，下一篇会测量它。

## 6. 丢包

RTP 通常跑在 UDP 上，丢包是常态。程序的第 4 个参数让它随机扔掉一部分收到的包，看看丢包率为 0.5% 和 2% 时会怎样：

```text
$ ./rtsp_h264 rtsp://127.0.0.1:8554/file loss.h264 12 0.5
RTP 包 2538 个（最大 1452 字节）：... 丢包 17 个，扔掉不完整的 NAL 14 个
$ ./rtsp_h264 rtsp://127.0.0.1:8554/file loss.h264 12 2
RTP 包 2285 个（最大 1452 字节）：... 丢包 42 个，扔掉不完整的 NAL 29 个
```

解码后逐帧和原文件比较：

<figure class="diagram">
<svg viewBox="0 0 680 150" role="img" aria-label="逐帧对比：不丢包时 247 帧全部和原文件相同；丢包 0.5% 时，解码出 210 帧，只有 59 帧相同，一出错就一直错到下一个关键帧；丢包 2% 时，解码出 110 帧，只有 21 帧相同">
  <g style="font-family:var(--mono);font-size:12px">
<text x="0" y="23" style="fill:var(--fg);font-size:11px">不丢包</text>
<rect x="70.0" y="10" width="543.4" height="18" style="fill:var(--accent-soft)"/>
<text x="619.4" y="23" style="fill:var(--muted);font-size:10px">247/247</text>
<text x="0" y="57" style="fill:var(--fg);font-size:11px">丢 0.5%</text>
<rect x="70.0" y="44" width="2.2" height="18" style="fill:var(--accent-soft)"/>
<rect x="72.2" y="44" width="2.2" height="18" style="fill:var(--danger)"/>
<rect x="74.4" y="44" width="2.2" height="18" style="fill:var(--accent-soft)"/>
<rect x="76.6" y="44" width="17.6" height="18" style="fill:var(--danger)"/>
<rect x="94.2" y="44" width="35.2" height="18" style="fill:var(--accent-soft)"/>
<rect x="129.4" y="44" width="30.8" height="18" style="fill:var(--danger)"/>
<rect x="160.2" y="44" width="35.2" height="18" style="fill:var(--accent-soft)"/>
<rect x="195.4" y="44" width="68.2" height="18" style="fill:var(--danger)"/>
<rect x="263.6" y="44" width="52.8" height="18" style="fill:var(--accent-soft)"/>
<rect x="316.4" y="44" width="202.4" height="18" style="fill:var(--danger)"/>
<rect x="518.8" y="44" width="2.2" height="18" style="fill:var(--accent-soft)"/>
<rect x="521.0" y="44" width="11.0" height="18" style="fill:var(--danger)"/>
<text x="538.0" y="57" style="fill:var(--muted);font-size:10px">59/210</text>
<text x="0" y="91" style="fill:var(--fg);font-size:11px">丢 2%</text>
<rect x="70.0" y="78" width="4.4" height="18" style="fill:var(--accent-soft)"/>
<rect x="74.4" y="78" width="149.6" height="18" style="fill:var(--danger)"/>
<rect x="224.0" y="78" width="41.8" height="18" style="fill:var(--accent-soft)"/>
<rect x="265.8" y="78" width="46.2" height="18" style="fill:var(--danger)"/>
<text x="318.0" y="91" style="fill:var(--muted);font-size:10px">21/110</text>
<rect x="70" y="118" width="12" height="12" style="fill:var(--accent-soft)"/><text x="88" y="128" style="fill:var(--muted);font-size:11px">和原文件逐字节相同</text>
<rect x="260" y="118" width="12" height="12" style="fill:var(--danger)"/><text x="278" y="128" style="fill:var(--muted);font-size:11px">和原文件不同（花屏）</text>
<text x="440" y="128" style="fill:var(--muted);font-size:11px">每一格是一帧，按显示顺序</text>
  </g>
</svg>
</figure>

- **丢包 0.5%**：只丢了 17 个包，解码出的 210 帧中却只有 59 帧和原文件相同。
- **丢包 2%**：解码出 110 帧，只有 21 帧相同。12 秒里本应有 8 个关键帧，完整收到的只剩 2 个，没有关键帧，后面的帧大多无法解码。

图中红色都是一段一段的：一旦某一帧丢了数据，参考它的 P 帧、B 帧跟着出错，错误一直延续到下一个关键帧才消失。这正是第 2 章《H.264 码流》中「错误传播」的实测。0.5% 的丢包率在网络上很常见，可见直接用 UDP 传视频是不行的，必须有应对丢包的办法：

- **重传**：发现丢包后请发送端重发。TCP 会自动重传（代价是延迟），RTP 可以用 RTCP 的 NACK 请求重传，SRT 和 WebRTC 都有重传机制（第 5 篇实测）。
- **前向纠错**（FEC）：多发一些冗余数据，丢了能直接算出来。
- **缩短 GOP、请求关键帧**：出错后让编码器立即发一个关键帧（RTCP 的 PLI、FIR 消息），缩短错误持续的时间。

## 7. 小结

- 流媒体系统有推流端、服务器、拉流端三个角色，服务器负责转发和转换协议。
- RTSP 负责控制：OPTIONS、DESCRIBE（取回 SDP）、SETUP（约定传输方式，拿到 Session）、PLAY、TEARDOWN。数据走 RTP，可以用 UDP，也可以交错在 RTSP 的 TCP 连接中。
- SDP 描述了有哪些流、编码、RTP 时钟频率；H.264 的 SPS、PPS 可以放在 `sprop-parameter-sets` 中。
- RTP 头 12 字节：序号用来发现丢包和乱序，时间戳是 PTS（视频的单位是 1/90000 秒），M 位标记一帧的最后一个包。
- H.264 有三种打包方式：单个 NAL、STAP-A（多个小 NAL 合在一起）、FU-A（一个大 NAL 拆成多片）。
- 丢一个包可能毁掉一个 GOP。UDP 上传视频，必须配合重传、纠错或者请求关键帧。

下一篇看直播推流最常用的 RTMP，并且开始测量延迟：推上去的画面，要多久才能在播放器里看到。

## 练习

1. SDP 中的 SPS 解码后的前 4 个字节是 `67 64 00 1f`，它们分别表示什么？和 `profile-level-id=64001F` 有什么关系？
2. 收到一个 FU-A 分片，负载的前两个字节是 `7C 85`。这是第几个分片？还原出的 NAL 头是多少，它是什么类型的 NAL？
3. 一路 30 帧/秒的视频，相邻两帧的 RTP 时间戳相差多少？如果是 29.97 帧/秒（30000/1001）呢？音频的 RTP 时间戳一般用什么时钟频率？

<details>
<summary>点击查看答案</summary>

1. 用 `base64 -d` 解码就能看到：`67 64 00 1f ac d9 40 50 ...`。`0x67` 是 NAL 头：最高位 F=0，接下来两位 NRI=3，低 5 位类型是 7，也就是 SPS。后面三个字节是 SPS 的开头：`profile_idc=0x64`（100，High 档次），`constraint_set` 标志全是 0，`level_idc=0x1f`（31，级别 3.1）。`profile-level-id` 正是把 SPS 的这三个字节写成了十六进制，播放器不用解析 SPS，看 SDP 就知道自己能不能解码。

2. FU 头 `0x85` 是 `1000 0101`：S=1、E=0，所以是第一个分片；低 5 位是 5。FU 指示 `0x7C` 是 `0111 1100`：F=0，NRI=3，类型 28（FU-A）。还原出的 NAL 头是 `(0x7C & 0xE0) | (0x85 & 0x1F)` = `0x60 | 0x05` = `0x65`：NRI=3，类型 5，IDR 切片。用第 2 章的方法查看测试视频的裸流，IDR 切片前面也正是 `00 00 01 65`（3 字节的起始码加 NAL 头）。

3. 30 帧/秒：90000 / 30 = 3000。29.97 帧/秒：90000 × 1001 / 30000 = 3003，正好是整数，这也是视频选 90 kHz 的原因之一：常见的帧率（24、25、30、29.97、50、60）都能整除。音频的时钟频率一般就是采样率，比如 48000 Hz 的 AAC 或者 Opus 用 48000，每个包的时间戳加上这个包里的采样数（AAC 一帧 1024 个采样，就加 1024）。

</details>
