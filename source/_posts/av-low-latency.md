---
title: 低延迟方案对比
date: 2026-11-03 13:00:00
updated: 2026-11-03 13:00:00
categories:
  - 音视频开发
  - 采集与流媒体
tags:
  - 音视频
  - 流媒体
  - SRT
  - WebRTC
  - LL-HLS
  - 延迟
  - C
order: 5
description: 同一路 RTMP 推流，经 MediaMTX 转换后分别用 RTMP、RTSP、SRT、WebRTC、LL-HLS、HLS 播放，用画面里的时间条码测出每一帧的延迟：RTMP 17 毫秒、WebRTC 43 毫秒、RTSP 55～59 毫秒、SRT 244 毫秒、LL-HLS 0.73 秒、HLS 3.2～7.8 秒。RTSP 比 RTMP 多出的正好是一帧，换成 48 帧/秒后差距减半；SRT 的延迟等于 latency 参数加约 110 毫秒。再用一个 C 写的 UDP 转发器模拟 2% 的丢包：普通 UDP 上的 MPEG-TS，480 帧里一半出错或丢失；SRT 在 latency 为 120 毫秒时重传补回 82 个丢包中的 78 个，只剩 4 帧出错，latency 加到 500 毫秒后一帧不错。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：用同一路流、同一种方法比较主流直播协议的延迟；理解每种协议的延迟从哪里来；理解 SRT 怎样用固定的延迟换取丢包重传；了解 WebRTC 的特点；能根据场景选择协议。
>
> **前置知识**：本章前两篇（RTMP 篇的推流和测延迟程序，HLS 篇）；网络专栏《UDP》《流量控制与拥塞控制》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，MediaMTX 1.21.3，SRT 1.5.7，hls.js 1.5.17，浏览器为 Claude 桌面应用内置的 Chromium。

## 1. 实验方法

用 RTMP 篇的 `live_push` 推一路流到 MediaMTX：720p、24 帧/秒、2 Mbps、GOP 1 秒、没有 B 帧、`tune=zerolatency`，每一帧顶部画着生成它的时刻。MediaMTX 会把它转换成各种协议，于是同一路流可以同时用七八种方式播放：

<figure class="diagram">
<svg viewBox="0 0 680 220" role="img" aria-label="实验的结构：live_push 用 RTMP 把流推给 MediaMTX；MediaMTX 输出 RTMP、RTSP、SRT 给 C 写的 latency_probe，SRT 经过 srt-live-transmit 转成管道；输出 HLS、LL-HLS、WebRTC 给浏览器中的测试页；两边都读取画面中的时间条码，用当前时间减去它得到延迟">
  <defs>
    <marker id="ll-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="85" width="110" height="50" rx="6" style="fill:var(--panel-2)"/>
      <rect x="200" y="60" width="130" height="100" rx="6" style="fill:var(--mark)"/>
      <rect x="440" y="15" width="235" height="80" rx="6" style="fill:var(--panel-2)"/>
      <rect x="440" y="125" width="235" height="80" rx="6" style="fill:var(--panel-2)"/>
    </g>
    <text x="15" y="106" style="fill:var(--fg)">live_push</text><text x="15" y="123" style="fill:var(--muted);font-size:10px">画时间条码</text>
    <text x="212" y="90" style="fill:var(--fg)">MediaMTX</text><text x="212" y="110" style="fill:var(--muted);font-size:10px">转换协议</text><text x="212" y="126" style="fill:var(--muted);font-size:10px">切 HLS 切片</text>
    <text x="452" y="37" style="fill:var(--fg)">latency_probe（C）</text>
    <text x="452" y="57" style="fill:var(--muted);font-size:10px">RTMP、RTSP（TCP/UDP）</text>
    <text x="452" y="73" style="fill:var(--muted);font-size:10px">SRT（srt-live-transmit 转成管道）</text>
    <text x="452" y="88" style="fill:var(--muted);font-size:10px">解码后读条码</text>
    <text x="452" y="147" style="fill:var(--fg)">浏览器测试页（JS）</text>
    <text x="452" y="167" style="fill:var(--muted);font-size:10px">WebRTC（WHEP）</text>
    <text x="452" y="183" style="fill:var(--muted);font-size:10px">HLS、LL-HLS（hls.js）</text>
    <text x="452" y="198" style="fill:var(--muted);font-size:10px">从 video 元素读条码</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#ll-arrow)">
      <line x1="115" y1="110" x2="198" y2="110"/>
      <path d="M330,95 L390,55 L438,55"/><path d="M330,125 L390,165 L438,165"/>
    </g>
    <text x="135" y="100" style="fill:var(--muted);font-size:10px">RTMP</text>
  </g>
</svg>
</figure>

两种播放端：

- **C 程序**：RTMP 篇的 `latency_probe`，加上 `play=1`（按 PTS 匀速播放，测的是观众实际看到的延迟）、`analyzeduration=0 probesize=32`（去掉探测带来的积压）。FFmpeg 不支持 SRT（Homebrew 的 FFmpeg 没有编译 libsrt），就用 SRT 官方的 `srt-live-transmit` 收下来，经管道交给 `latency_probe`。
- **浏览器**：FFmpeg 播不了 WebRTC，也不支持 LL-HLS，这两种在浏览器里测。测试页用 WHEP（WebRTC 的 HTTP 拉流协议）连接 MediaMTX，或者用 hls.js 播 HLS；每 10 毫秒把 `<video>` 当前的画面画到 canvas 上，读出条码，条码变了就说明换了一帧，用 `Date.now()` 减去它就是延迟。JavaScript 的 `Date.now()` 和 C 程序的 `CLOCK_REALTIME` 是同一个时钟。

<details>
<summary>probe.html</summary>

```html
<!doctype html>
<html>
<head><meta charset="utf-8"><title>latency probe</title>
<style>body{font:14px monospace;background:#111;color:#eee}video{width:640px}</style></head>
<body>
<video id="v" muted autoplay playsinline></video>
<pre id="out">waiting</pre>
<canvas id="c" width="1280" height="32" style="display:none"></canvas>
<script src="https://cdn.jsdelivr.net/npm/hls.js@1.5.17/dist/hls.min.js"></script>
<script>
const q = new URLSearchParams(location.search);
const mode = q.get('mode') || 'webrtc';
const secs = +(q.get('secs') || 20);
const v = document.getElementById('v'), out = document.getElementById('out');
const ctx = document.getElementById('c').getContext('2d', {willReadFrequently: true});
const t0 = performance.now();
let first = 0, lat = [], bad = 0, done = false;

function readCode() {
  ctx.drawImage(v, 0, 0, 1280, 720, 0, 0, 1280, 720);
  const d = ctx.getImageData(0, 0, 1280, 32).data;
  let hi = 0, lo = 0;   // 40 位：前 32 位时间，后 8 位校验
  for (let b = 0; b < 40; b++) {
    let s = 0;
    for (let y = 12; y < 20; y++) for (let x = 12; x < 20; x++) s += d[(y * 1280 + b * 32 + x) * 4];
    const bit = s / 64 > 128 ? 1 : 0;
    if (b < 32) hi = (hi * 2 + bit) >>> 0; else lo = (lo << 1) | bit;
  }
  const sum = ((hi & 255) + ((hi >>> 8) & 255) + ((hi >>> 16) & 255) + (hi >>> 24)) & 255;
  return ((~sum) & 255) === lo ? hi : -1;
}

// 每 10 毫秒读一次画面上的条码，条码变了就说明换了一帧
let prev = -1;
function poll() {
  if (done || v.readyState < 2) return;
  const t = readCode();
  if (t < 0) { bad++; return; }
  if (t === prev) return;
  prev = t;
  let l = Date.now() % 4294967296 - t; if (l < -2147483648) l += 4294967296;
  if (!first) first = performance.now();
  lat.push(l);
  if (performance.now() - first > secs * 1000) { done = true; report(); return; }
  out.textContent = `${mode} frames ${lat.length} last ${l} ms`;
}
setInterval(poll, 10);

function report() {
  const s = [...lat].sort((a, b) => a - b);
  const extra = window.hls ? ` latencyHls ${hls.latency.toFixed(2)} s` : '';
  out.textContent = `RESULT ${mode} | first ${Math.round(first - t0)} ms | frames ${s.length} bad ${bad} | ` +
    `min ${s[0]} med ${s[s.length >> 1]} max ${s[s.length - 1]} last ${lat[lat.length - 1]}${extra}`;
}

async function webrtc() {
  const pc = new RTCPeerConnection();
  pc.addTransceiver('video', {direction: 'recvonly'});
  pc.ontrack = e => { v.srcObject = e.streams[0]; };
  await pc.setLocalDescription(await pc.createOffer());
  await new Promise(r => { if (pc.iceGatheringState === 'complete') r();
    pc.onicegatheringstatechange = () => pc.iceGatheringState === 'complete' && r(); });
  const res = await fetch(q.get('url') || 'http://127.0.0.1:8889/live/test/whep',
    {method: 'POST', headers: {'Content-Type': 'application/sdp'}, body: pc.localDescription.sdp});
  await pc.setRemoteDescription({type: 'answer', sdp: await res.text()});
  window.pc = pc;
}

function hlsPlay() {
  const ll = q.get('ll') !== '0';
  window.hls = new Hls({lowLatencyMode: ll});
  hls.loadSource(q.get('url') || 'http://127.0.0.1:8888/live/test/index.m3u8');
  hls.attachMedia(v);
}

(mode === 'webrtc' ? webrtc() : hlsPlay());
</script>
</body>
</html>
```

</details>

浏览器中的测量还包括了浏览器自己解码、合成画面的时间，以及最多 10 毫秒的轮询间隔，和 C 程序测的不完全是一回事，比较时要记住这一点。所有测量都在一台电脑上进行，没有真实的网络传输，测到的是**协议和软件本身带来的延迟**。真实网络上还要加上传输时间和各种排队。

## 2. 结果

每种方式测 20 秒（C 程序 15 秒），取延迟的中位数：

<figure class="diagram">
<svg viewBox="0 0 680 300" role="img" aria-label="同一路流用各种协议播放的延迟，对数坐标：RTMP 17 毫秒，WebRTC 43 毫秒，RTSP 55 到 59 毫秒，SRT 244 毫秒，LL-HLS 0.73 秒，HLS 1 秒切片 3.2 秒，2 秒切片 6.1 到 7.8 秒">
  <g style="font-family:var(--mono);font-size:12px">
<line x1="210.0" y1="10" x2="210.0" y2="255" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="210.0" y="270" text-anchor="middle" style="fill:var(--muted);font-size:10px">10ms</text>
<line x1="353.3" y1="10" x2="353.3" y2="255" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="353.3" y="270" text-anchor="middle" style="fill:var(--muted);font-size:10px">100ms</text>
<line x1="496.7" y1="10" x2="496.7" y2="255" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="496.7" y="270" text-anchor="middle" style="fill:var(--muted);font-size:10px">1s</text>
<line x1="640.0" y1="10" x2="640.0" y2="255" style="stroke:var(--border);stroke-dasharray:2 3"/><text x="640.0" y="270" text-anchor="middle" style="fill:var(--muted);font-size:10px">10s</text>
<text x="202" y="27" text-anchor="end" style="fill:var(--fg);font-size:11px">RTMP</text>
<rect x="210" y="16" width="33.0" height="16" style="fill:var(--accent);opacity:0.85"/>
<text x="248.0" y="28" style="fill:var(--muted);font-size:10px">17 ms</text>
<text x="202" y="54" text-anchor="end" style="fill:var(--fg);font-size:11px">WebRTC</text>
<rect x="210" y="43" width="90.8" height="16" style="fill:var(--link);opacity:0.85"/>
<text x="305.8" y="55" style="fill:var(--muted);font-size:10px">43 ms</text>
<text x="202" y="81" text-anchor="end" style="fill:var(--fg);font-size:11px">RTSP / UDP</text>
<rect x="210" y="70" width="106.1" height="16" style="fill:var(--accent);opacity:0.85"/>
<text x="321.1" y="82" style="fill:var(--muted);font-size:10px">55 ms</text>
<text x="202" y="108" text-anchor="end" style="fill:var(--fg);font-size:11px">RTSP / TCP</text>
<rect x="210" y="97" width="110.5" height="16" style="fill:var(--accent);opacity:0.85"/>
<text x="325.5" y="109" style="fill:var(--muted);font-size:10px">59 ms</text>
<text x="202" y="135" text-anchor="end" style="fill:var(--fg);font-size:11px">SRT（latency 120）</text>
<rect x="210" y="124" width="198.9" height="16" style="fill:var(--accent);opacity:0.85"/>
<text x="413.9" y="136" style="fill:var(--muted);font-size:10px">244 ms</text>
<text x="202" y="162" text-anchor="end" style="fill:var(--fg);font-size:11px">LL-HLS（hls.js）</text>
<rect x="210" y="151" width="267.3" height="16" style="fill:var(--link);opacity:0.85"/>
<text x="482.3" y="163" style="fill:var(--muted);font-size:10px">733 ms</text>
<text x="202" y="189" text-anchor="end" style="fill:var(--fg);font-size:11px">HLS 1 秒切片（hls.js）</text>
<rect x="210" y="178" width="358.9" height="16" style="fill:var(--link);opacity:0.85"/>
<text x="573.9" y="190" style="fill:var(--muted);font-size:10px">3.19 s</text>
<text x="202" y="216" text-anchor="end" style="fill:var(--fg);font-size:11px">HLS 2 秒切片（hls.js）</text>
<rect x="210" y="205" width="398.9" height="16" style="fill:var(--link);opacity:0.85"/>
<text x="613.9" y="217" style="fill:var(--muted);font-size:10px">6.07 s</text>
<text x="202" y="243" text-anchor="end" style="fill:var(--fg);font-size:11px">HLS 2 秒切片（FFmpeg）</text>
<rect x="210" y="232" width="414.7" height="16" style="fill:var(--accent);opacity:0.85"/>
<text x="629.7" y="244" style="fill:var(--muted);font-size:10px">7.82 s</text>
<rect x="210" y="282" width="12" height="12" style="fill:var(--accent);opacity:0.85"/><text x="228" y="292" style="fill:var(--muted);font-size:11px">C 程序 latency_probe</text>
<rect x="410" y="282" width="12" height="12" style="fill:var(--link);opacity:0.85"/><text x="428" y="292" style="fill:var(--muted);font-size:11px">浏览器测试页</text>
  </g>
</svg>
</figure>

| 协议 | 播放端 | 延迟（中位数） | 打开到第一帧 |
|---|---|---|---|
| RTMP | C | 17 ms | 0.79 s |
| WebRTC | 浏览器 | 20～43 ms（3 次） | 0.67～1.56 s |
| RTSP，RTP 走 UDP | C | 55 ms | 0.96 s |
| RTSP，RTP 走 TCP | C | 59 ms | 0.99 s |
| SRT，latency 120 ms | C | 244 ms | 1.15 s |
| LL-HLS | 浏览器 | 683～733 ms（2 次） | |
| HLS，1 秒切片（LL-HLS 关掉低延迟模式） | 浏览器 | 3.19 s | |
| HLS，2 秒切片 | 浏览器 | 6.07 s | |
| HLS，2 秒切片 | C（FFmpeg） | 7.82 s | |

HLS 和 LL-HLS 在上一篇已经分析过了。下面看其余几种。

### 2.1 RTMP 和 RTSP：差了一帧

RTMP 17 毫秒，RTSP 却要 55～59 毫秒，多出约 40 毫秒，差不多正好是 24 帧/秒的一帧（41.7 毫秒）。为了验证，把推流改成 48 帧/秒（一帧 20.8 毫秒），用到达延迟各测两次：

| | 24 帧/秒 | 48 帧/秒 |
|---|---|---|
| RTMP | 3、5 ms | 2、2 ms |
| RTSP（TCP） | 45、46 ms | 23、23 ms |
| 差值 | 约 42 ms，一帧 | 约 21 ms，一帧 |

差值随帧率减半，说明多出来的正好是一帧的时间。原因在 FFmpeg 的 RTSP 解封装：RTMP 的每条视频消息就是完整的一帧，收到就能交给解码器；RTSP 收到的是 RTP 包，FFmpeg 把它们还原成一个个 NAL 单元后，交给 H.264 解析器（parser）组装成帧，而解析器要看到**下一帧的第一个 NAL**，才能确定当前这一帧已经结束了。于是每一帧都要等下一帧到来，平白多了一帧的延迟。

其实 RTP 头里有标记位 M，一帧的最后一个包 M=1，看到它就知道一帧收齐了（第 2 篇的程序就是这样数帧的）。自己写 RTSP 客户端时利用 M 位，可以省掉这一帧。MPEG-TS（下面的 SRT）也有同样的问题：TS 包里只有 PES，没有「帧结束」的标记，也要靠解析器等下一帧。

RTSP 用 UDP 和 TCP 差不多，因为本机没有丢包，TCP 也不用重传。

### 2.2 SRT：可以调的固定延迟

SRT（Secure Reliable Transport）是 Haivision 公司开源的协议，跑在 UDP 上，传的一般是 MPEG-TS。它的核心机制叫 **TSBPD**（Timestamp-Based Packet Delivery）：发送端给每个包打上发送时间，接收端收到后先不交出去，一直等到「发送时间 + latency」那一刻才交给应用。这段等待就是留给重传的时间：发现丢包后，接收端立即请求重传（NAK），只要重传的包在 latency 之内赶到，应用就感觉不到丢过包。

所以 SRT 的延迟基本上是**固定的，等于 latency 参数**，和网络好坏无关（网络差到 latency 都不够重传的时候，来不及的包直接丢弃，而不是让延迟增长）。改变接收端的 latency，测到达延迟：

| latency 参数 | 20 ms | 120 ms | 200 ms | 300 ms | 500 ms |
|---|---|---|---|---|---|
| 延迟（中位数） | 233 ms | 233 ms | 315 ms | 402 ms | 608 ms |

200 毫秒以上，延迟 ≈ latency + 110 毫秒左右；20 和 120 的结果相同，因为 SRT 连接两端各有一个 latency，实际生效的是**两端中较大的那个**，MediaMTX 那一端是 120 毫秒，接收端设得再小也没用。多出的约 110 毫秒来自其他环节：MediaMTX 把流重新封装成 MPEG-TS、上面说的解析器等一帧、`srt-live-transmit` 和管道的转发等。

latency 设多大合适？它必须够一次（最好几次）重传往返，SRT 的文档建议至少是 RTT（往返时间）的 4 倍，丢包严重的网络还要更大。同城网络 RTT 十几毫秒，120 毫秒的默认值绰绰有余；跨国 RTT 200 毫秒，latency 就得设到 800 毫秒以上。第 3 节用丢包实验验证这一点。

### 2.3 WebRTC

WebRTC 是浏览器内置的实时音视频技术，本来为视频通话设计，延迟目标是几百毫秒以内。它的媒体传输也是 RTP，跑在 UDP 上，并且做了很多事情来对付糟糕的网络：

- **NACK 重传**和 **FEC 前向纠错**，丢包率不高时可以完全恢复；
- **抖动缓冲**（jitter buffer）的大小是动态的：网络平稳时缩到很小，抖动大了才加大，不像 SRT 那样固定；
- **拥塞控制**：根据丢包和延迟的变化估计带宽，实时调整编码器的码率和分辨率，宁可画质下降也不卡顿、不积累延迟；
- 必须加密（DTLS-SRTP），建立连接要经过 ICE（找到双方能互通的地址，必要时借助 STUN、TURN 服务器）。

本机测得 20～43 毫秒，和 RTMP 相当。打开要 0.67～1.56 秒，其中 ICE 收集地址、DTLS 握手占了不少（测试页等 ICE 收集完才发出请求，没有用更快的 trickle ICE）。

WebRTC 原本只用于点对点，推流和拉流都没有标准的信令。近几年的 **WHIP**（推流）和 **WHEP**（拉流）用一个 HTTP POST 交换 SDP，解决了这个问题，测试页就是用 WHEP 连接 MediaMTX 的。FFmpeg 8 起也有了 WHIP 封装，可以直接把流推给 WebRTC 服务器。

## 3. 丢包的时候

本机上没有丢包，所有基于 UDP 的协议都显得很完美。为了模拟真实的网络，写一个 UDP 转发器：客户端发给它的包转给服务器，服务器回来的包再转给客户端，两个方向都按一定的概率丢包，并加上固定的延迟。

```c
// udp_lossy.c：UDP 转发器，模拟一条会丢包、有延迟的网络
// 客户端发到「监听端口」的包转给「目标端口」，回来的包再转给客户端；两个方向都按概率丢包、加固定延迟
// 用法：udp_lossy 监听端口 目标端口 丢包率% 单程延迟ms
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define QN 65536
typedef struct { double due; int dir, len; unsigned char data[1500]; } Item;
static Item q[QN];
static int qh, qt;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "用法: %s 监听端口 目标端口 丢包率%% 单程延迟ms\n", argv[0]); return 1; }
    double loss = atof(argv[3]) / 100, delay = atof(argv[4]) / 1000;
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    int front = socket(AF_INET, SOCK_DGRAM, 0), back = socket(AF_INET, SOCK_DGRAM, 0);
    a.sin_port = htons(atoi(argv[1]));
    if (bind(front, (void *)&a, sizeof(a))) { perror("bind"); return 1; }
    a.sin_port = htons(atoi(argv[2]));
    connect(back, (void *)&a, sizeof(a));
    struct sockaddr_in client;
    socklen_t clen = 0;
    long pass[2] = {0}, drop[2] = {0};
    srand(1);
    for (;;) {
        double t = now();
        while (qh != qt && q[qh].due <= t) {           // 到时间的包发出去
            Item *it = &q[qh];
            if (it->dir == 0) send(back, it->data, it->len, 0);
            else if (clen) sendto(front, it->data, it->len, 0, (void *)&client, clen);
            qh = (qh + 1) % QN;
        }
        int wait = qh != qt ? (int)((q[qh].due - t) * 1000) + 1 : 1000;
        struct pollfd pf[2] = {{front, POLLIN, 0}, {back, POLLIN, 0}};
        if (poll(pf, 2, wait) <= 0) {
            static long shown;
            if (pass[0] + pass[1] != shown) {
                shown = pass[0] + pass[1];
                fprintf(stderr, "去程 转发 %ld 丢 %ld；回程 转发 %ld 丢 %ld\n", pass[0], drop[0], pass[1], drop[1]);
            }
            continue;
        }
        for (int d = 0; d < 2; d++) {
            if (!(pf[d].revents & POLLIN)) continue;
            Item *it = &q[qt];
            socklen_t l = sizeof(client);
            it->len = d == 0 ? recvfrom(front, it->data, sizeof(it->data), 0, (void *)&client, &l)
                             : recv(back, it->data, sizeof(it->data), 0);
            if (d == 0) clen = l;
            if (it->len <= 0) continue;
            if (rand() < loss * RAND_MAX) { drop[d]++; continue; }
            pass[d]++;
            it->dir = d;
            it->due = now() + delay;
            qt = (qt + 1) % QN;
        }
    }
}
```

### 3.1 普通 UDP 上的 MPEG-TS

先看没有任何保护的情况：推流程序直接把 MPEG-TS 发到 UDP，经过转发器（丢包 2%，单程延迟 25 毫秒），`latency_probe` 接收：

```bash
./udp_lossy 9100 9101 2 25 &
./live_push bbb-10s.mp4 'udp://127.0.0.1:9100?pkt_size=1316' mpegts 1 seconds=40 &
./latency_probe udp://127.0.0.1:9101 20 analyzeduration=0 probesize=32
```

```text
出错帧 57  缺帧 100  帧数 388  坏帧 93  卡顿 0 次  到达延迟 最小 101 / 中位 114 / 最大 166 / 最后 166 ms
```

20 秒本应有 480 帧：100 帧根本没解出来（缺帧），93 帧连顶部的条码都花了（坏帧），剩下 388 帧中又有 57 帧被解码器标记为出错。**一半以上的画面是坏的**，和第 2 篇的丢包实验一致。同样的设置，丢包率改成 0 时，480 帧全部完好，延迟 112 毫秒。

### 3.2 SRT

SRT 拉流时经过转发器：`srt-live-transmit` 连接转发器的 9000 端口，转发器连 MediaMTX 的 8890 端口。丢包 2%，单程延迟 25 毫秒（RTT 约 50 毫秒）：

```bash
./udp_lossy 9000 8890 2 25 &
srt-live-transmit -s:2000 -pf:csv -f -statsout:srt.csv \
    'srt://127.0.0.1:9000?streamid=read:live/test&latency=120' file://con |
    ./latency_probe pipe:0 20 analyzeduration=0 probesize=32
```

`srt-live-transmit` 的统计里有丢包和重传的计数：

| latency | 收到的包 | 发现丢包 | 重传收到 | 放弃 | 出错帧 | 延迟（中位数） |
|---|---|---|---|---|---|---|
| 120 ms | 4032 | 82 | 78 | 4 | 4 | 268 ms |
| 500 ms | 4105 | 76 | 76 | 0 | 0 | 644 ms |

latency 为 120 毫秒时，82 个丢包重传补回了 78 个，有 4 个在 latency 到期前没能补回，被放弃了，对应 4 帧出错。latency 加到 500 毫秒，76 个丢包全部补回，**一帧都没错**，代价是延迟从 268 毫秒涨到 644 毫秒。为什么 RTT 只有 50 毫秒、latency 是它的两倍多，还会有包补不回来？因为重传的请求和重传的包本身也可能丢（转发器两个方向都丢 2%），丢了就要再来一轮；另外 SRT 发现丢包也需要时间（要等到收到后面的包，看到序号不连续）。这就是建议 latency 至少取 4 倍 RTT 的原因。

把单程延迟加到 50 毫秒（RTT 约 100 毫秒）再测：latency 120 毫秒时仍有 4 个包放弃、4 帧出错；500 毫秒时依然全部补回，一帧不错。

和 TCP 上的协议比一比：RTMP 篇限速时，TCP 不会丢数据，但延迟涨上去就再也下不来；SRT 的延迟始终固定在 latency 附近，网络太差时丢掉补不回来的包。**一个保延迟，一个保数据**，SRT 还可以通过 latency 参数在两者之间调节。

## 4. 怎样选择

| 协议 | 传输层 | 本机实测延迟 | 抗丢包 | 浏览器播放 | 典型用途 |
|---|---|---|---|---|---|
| RTMP | TCP | 17 ms | TCP 重传，延迟会积累 | 不支持 | 主播推流 |
| RTSP | UDP 或 TCP | 55～59 ms | 基本没有（UDP） | 不支持 | 监控摄像头、局域网 |
| SRT | UDP | latency + 约 110 ms | 重传，延迟固定 | 不支持 | 专业远程制作、跨地区传输 |
| WebRTC | UDP | 20～43 ms | 重传、纠错、自适应码率 | 原生支持 | 视频通话、连麦、互动直播 |
| LL-HLS | TCP（HTTP） | 0.7 s | TCP 重传 | hls.js、Safari | 大规模低延迟分发 |
| HLS | TCP（HTTP） | 3～4 个切片 | TCP 重传 | hls.js、Safari | 大规模分发、点播 |

实际的直播系统往往组合使用：

- **推流**：主播用 RTMP（兼容性最好）或 SRT（网络差、要求稳定时）推到服务器。
- **分发**：观众多、能接受几秒延迟，用 HLS 或 LL-HLS 加 CDN，成本最低；要互动（连麦、带货、竞拍），用 WebRTC，但服务器的成本高得多，每个观众都要服务器维持一个加密的 UDP 会话、做拥塞控制。
- **监控**：摄像头出 RTSP，服务器转成 HLS 或 WebRTC 给浏览器看。

## 5. 小结

- 用画面里的时间条码，可以在同一台电脑上公平地比较所有协议的延迟。
- 协议和软件本身的延迟：RTMP、WebRTC 最低（几十毫秒），RTSP 因为 FFmpeg 要等下一帧来确定帧边界多了一帧，SRT 等于 latency 参数加上一些转换开销，LL-HLS 约 0.7 秒，HLS 是 3～4 个切片。
- 没有保护的 UDP 上，2% 的丢包让一半的画面出错。
- SRT 用固定的延迟换重传的时间，latency 要大于几倍 RTT；WebRTC 用重传、纠错和自适应码率对付丢包；TCP 上的协议不丢数据，但延迟会积累。
- 选协议要看延迟、规模、网络和播放端：推流 RTMP/SRT，大规模分发 HLS/LL-HLS，互动 WebRTC，监控 RTSP。

## 练习

1. 本文 SRT 的实验中，接收端把 latency 设成 20 毫秒，实测延迟和 120 毫秒一样。如果想把 SRT 的延迟降到 120 毫秒以下，应该怎么做？
2. 丢包 2% 时，普通 UDP 上的 MPEG-TS 延迟中位数是 114 毫秒，没有丢包时是 112 毫秒，几乎没变。为什么丢包没有让延迟增加？
3. 同一场直播，同时有 10 个连麦的嘉宾和 10 万个普通观众。应该怎样设计协议的组合？

<details>
<summary>点击查看答案</summary>

1. SRT 连接实际使用的 latency 是两端设置中较大的那个，接收端设得再小，发送端（MediaMTX）的 120 毫秒也会生效。要降低延迟，必须在发送端也把 latency 调小。在 FFmpeg 或者 `srt-live-transmit` 作为发送端时，URL 中的 `latency` 参数（或者分别设置 `rcvlatency`、`peerlatency`）就可以控制；MediaMTX 作为服务器时，要看它是否提供了相应的配置。另外，延迟中还有约 110 毫秒不是 SRT 造成的（重新封装成 TS、解析器等一帧等），要降低这部分，就要从整个链路入手。当然，latency 降低以后，能用来重传的时间也少了，要先确认网络的 RTT 和丢包情况允许这样做。

2. 因为普通 UDP 上没有重传，丢了的包就是丢了，不会有任何一方停下来等它，后面的包照常到达、照常解码。代价全部体现在画面质量上（一半的帧出错）。这和 TCP 正好相反：TCP 丢包后要等重传，后面的数据都得排队等着，延迟上升，画面却是完好的。SRT 介于两者之间：在 latency 之内等重传，超过了就放弃。

3. 嘉宾之间需要实时对话，延迟必须在几百毫秒以内，用 WebRTC：嘉宾都连到一台 WebRTC 服务器（SFU，选择性转发单元），各自推一路、拉其他人的流。服务器（或者一个专门的合流程序）把主持人和嘉宾的画面合成一路，再转成 RTMP 推给直播 CDN，生成 HLS 或 LL-HLS 分发给 10 万个观众。观众晚几秒看到没有关系，HLS 加 CDN 的成本比给每个观众维持一个 WebRTC 会话低得多。如果观众也要参与互动（比如抢答、竞拍），对延迟的要求高一些，可以用 LL-HLS，或者采用支持大规模分发的 WebRTC 方案。

</details>
