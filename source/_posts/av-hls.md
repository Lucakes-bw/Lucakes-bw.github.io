---
title: HLS：切片与延迟
date: 2026-11-03 12:00:00
updated: 2026-11-03 12:00:00
categories:
  - 音视频开发
  - 采集与流媒体
tags:
  - 音视频
  - 流媒体
  - HLS
  - LL-HLS
  - 延迟
  - C
order: 4
description: HLS 把直播切成几秒一段的小文件，用 m3u8 播放列表告诉播放器有哪些切片，全程只用普通的 HTTP，可以直接放上 CDN。用 FFmpeg 的 hls 封装生成直播切片，再用 C 写一个自己发 HTTP 请求的观察程序：切片在它的第一帧之后 2.04 秒、最后一帧之后 84 毫秒才出现在列表里。播放器又要从倒数第 3 个切片开始播：切片 1、2、6 秒时，FFmpeg 播放的延迟分别是 3.7、7.8、20.0 秒，从最后一个切片开始播，延迟减半却卡顿了 4～12 次。LL-HLS 把切片再分成 0.2 秒的部分切片，同一路流，hls.js 打开低延迟模式是 0.73 秒，关掉是 3.19 秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 HLS 的工作方式：播放列表、切片、滑动窗口；会用 FFmpeg 生成直播切片；理解切片只能在关键帧处切开；搞清楚 HLS 的延迟由哪几部分组成，以及缩短切片、改变起播位置、LL-HLS 各自的效果和代价。
>
> **前置知识**：上一篇（推流程序和测延迟的方法）；第 2 章《MPEG-TS 封装格式》；网络专栏《HTTP》。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2，MediaMTX 1.21.3，hls.js 1.5.17。

## 1. HLS 是什么

HLS（HTTP Live Streaming）是 Apple 在 2009 年推出的协议，现在的标准是 RFC 8216。它的思路非常朴素：

- 把直播流切成一段一段的小文件（**切片**，segment），每段几秒；
- 用一个文本文件（**播放列表**，`.m3u8`）列出最近的几个切片；
- 播放器用普通的 HTTP 下载播放列表，再按顺序下载切片播放；隔一会儿重新下载播放列表，看看有没有新切片。

<figure class="diagram">
<svg viewBox="0 0 680 210" role="img" aria-label="HLS 的结构：推流端把流推给服务器，服务器切成一个个切片文件，同时维护播放列表 live.m3u8，只列出最近的几个切片；播放器反复下载播放列表，按顺序下载新出现的切片；所有文件都可以经过 CDN 缓存">
  <defs>
    <marker id="hl-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="80" width="100" height="44" rx="6" style="fill:var(--panel-2)"/>
      <rect x="150" y="20" width="250" height="170" rx="6" style="fill:var(--panel)"/>
      <rect x="165" y="45" width="110" height="34" rx="4" style="fill:var(--mark)"/>
      <rect x="165" y="100" width="44" height="30" style="fill:var(--accent-soft)"/><rect x="213" y="100" width="44" height="30" style="fill:var(--accent-soft)"/><rect x="261" y="100" width="44" height="30" style="fill:var(--accent-soft)"/><rect x="309" y="100" width="44" height="30" style="fill:var(--accent-soft)"/>
      <rect x="357" y="100" width="34" height="30" style="fill:var(--panel-2);stroke-dasharray:3 3"/>
      <rect x="450" y="80" width="90" height="44" rx="6" style="fill:var(--panel-2)"/>
      <rect x="580" y="80" width="95" height="44" rx="6" style="fill:var(--panel-2)"/>
    </g>
    <text x="15" y="100" style="fill:var(--fg)">推流端</text><text x="15" y="116" style="fill:var(--muted);font-size:10px">RTMP</text>
    <text x="160" y="37" style="fill:var(--fg)">HLS 服务器（HTTP）</text>
    <text x="173" y="66" style="fill:var(--fg)">live.m3u8</text>
    <text x="285" y="60" style="fill:var(--muted);font-size:10px">只列出最近</text><text x="285" y="74" style="fill:var(--muted);font-size:10px">的几个切片</text>
    <text x="171" y="120" style="fill:var(--fg);font-size:11px">16.ts</text><text x="219" y="120" style="fill:var(--fg);font-size:11px">17.ts</text><text x="267" y="120" style="fill:var(--fg);font-size:11px">18.ts</text><text x="315" y="120" style="fill:var(--fg);font-size:11px">19.ts</text>
    <text x="358" y="120" style="fill:var(--muted);font-size:10px">20…</text>
    <text x="165" y="152" style="fill:var(--muted);font-size:10px">正在写的切片还不在列表里，</text><text x="165" y="168" style="fill:var(--muted);font-size:10px">写完才加进去，最老的被移出</text>
    <text x="462" y="100" style="fill:var(--fg)">CDN</text><text x="462" y="116" style="fill:var(--muted);font-size:10px">缓存文件</text>
    <text x="590" y="100" style="fill:var(--fg)">播放器</text><text x="590" y="116" style="fill:var(--muted);font-size:10px">HTTP GET</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#hl-arrow)">
      <line x1="105" y1="102" x2="148" y2="102"/><line x1="400" y1="102" x2="448" y2="102"/><line x1="540" y1="102" x2="578" y2="102"/>
    </g>
  </g>
</svg>
</figure>

全程都是普通的 HTTP 文件下载，这带来了 HLS 最大的优点：

- **能用 CDN**：切片就是静态文件，CDN 可以把它们缓存在离观众最近的节点上，一百万人同时看，源站也只被请求几次。RTMP、RTSP 这种长连接的协议做不到。
- **穿透性好**：80、443 端口的 HTTP 请求几乎不会被防火墙拦截。
- **自适应码率**：同一路直播可以准备几档码率，播放器根据网速随时切换（下面的主播放列表）。
- iPhone 的 Safari 原生支持，其他浏览器用 hls.js 这样的库也能播。

代价是**延迟大**，一般在 10 秒以上。本篇的重点就是搞清楚这个延迟是怎么来的。

### 1.1 播放列表

下面是 FFmpeg 生成的一个直播播放列表，切片 2 秒：

```text
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:2
#EXT-X-MEDIA-SEQUENCE:16
#EXTINF:2.000000,
live16.ts
#EXTINF:2.000000,
live17.ts
#EXTINF:2.000000,
live18.ts
#EXTINF:2.000000,
live19.ts
#EXTINF:2.000000,
live20.ts
#EXTINF:2.000000,
live21.ts
```

- `#EXTM3U`：文件头，表示这是扩展的 M3U 格式。
- `#EXT-X-TARGETDURATION`：切片的最大时长（向上取整到秒）。播放器据此决定多久刷新一次列表。
- `#EXT-X-MEDIA-SEQUENCE`：列表中第一个切片的序号，后面的依次加 1。播放器靠序号判断哪些切片是新的。
- `#EXTINF:2.000000,` 加下一行的 URI：一个切片的时长和地址，地址相对于播放列表。

直播时，列表是一个**滑动窗口**：新切片写完就加到末尾，最老的切片移出列表（`-hls_flags delete_segments` 还会删掉文件），`MEDIA-SEQUENCE` 随之加 1。点播（VOD）的列表则包含全部切片，最后有一行 `#EXT-X-ENDLIST`，表示不会再有新的切片了。

要提供多档码率，还要一个**主播放列表**（master playlist），列出各档的媒体播放列表。MediaMTX 生成的主播放列表只有一档：

```text
#EXTM3U
#EXT-X-VERSION:10
#EXT-X-INDEPENDENT-SEGMENTS

#EXT-X-STREAM-INF:BANDWIDTH=2521496,AVERAGE-BANDWIDTH=2521496,CODECS="avc1.42c01f",RESOLUTION=1280x720,FRAME-RATE=24.000
video1_stream.m3u8
```

`BANDWIDTH` 是码率，`CODECS` 是编码（`avc1.42c01f`：H.264，`42` 是 Baseline，`1f` 是级别 3.1，和上一篇推流程序的设置一致），播放器根据这些信息选择合适的一档。

切片最早只能是 MPEG-TS（第 2 章讲过，它本来就是为广播设计的，任何位置都能开始解码）。2016 年起也可以用 fMP4（分片的 MP4），这时列表中会多一行 `#EXT-X-MAP` 指向初始化片段（`moov`），每个切片只有 `moof` 加 `mdat`。

## 2. 生成直播切片

上一篇的推流程序把封装格式换成 `hls`，输出路径写成 `.m3u8` 文件，就能直接生成切片。再随便起一个 HTTP 服务器，就是一个 HLS 直播服务了：

```bash
./live_push bbb-10s.mp4 www/a/live.m3u8 hls 1 hls_time=2 hls_list_size=6 hls_flags=delete_segments seconds=1500
cd www && python3 -m http.server 8000
```

`hls_time` 是切片的目标时长，`hls_list_size` 是列表中保留几个切片。GOP 是 1 秒，切片 2 秒，于是每个切片正好是 48 帧，两个 GOP。

### 2.1 切片只能在关键帧处切开

如果 GOP 比切片还长会怎样？把 GOP 改成 2 秒，切片设成 1 秒：

```text
#EXT-X-TARGETDURATION:2
#EXTINF:2.000000,
live1.ts
#EXTINF:2.000000,
live2.ts
...
```

切片变成了 2 秒。每个切片都必须以关键帧开头，否则播放器从这个切片开始播时无法解码，所以切片只能在关键帧处切开，**切片的时长至少是一个 GOP**。想要短切片，必须先缩短 GOP，而 GOP 越短，关键帧越多，同样码率下画质越差（第 1 章讲过，I 帧比 P 帧大得多）。

## 3. 切片什么时候出现

一个切片要等它的最后一帧编码完、整个文件写完，才会被加进播放列表。为了看清这个时间，写一个观察程序：自己用 socket 发 HTTP 请求，每 0.1 秒刷新一次播放列表，发现新切片就下载下来，用 FFmpeg 解码出第一帧和最后一帧，读出上一篇画在画面里的时间条码。

```c
// hls_watch.c：盯着一个直播 m3u8，看每个切片是什么时候出现的
// 自己用 socket 发 HTTP 请求；新切片出现时下载下来，用 FFmpeg 解码第一帧和最后一帧，读出时间条码
// 用法：hls_watch http://127.0.0.1:8000/a/live.m3u8 [秒数]
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// 最简单的 HTTP GET：HTTP/1.0，服务器发完就关连接；返回正文长度
static char host[256], port[16] = "80";
static long http_get(const char *path, char **body) {
    struct addrinfo hints = {.ai_socktype = SOCK_STREAM}, *ai;
    if (getaddrinfo(host, port, &hints, &ai)) return -1;
    int s = socket(ai->ai_family, SOCK_STREAM, 0);
    if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) { close(s); freeaddrinfo(ai); return -1; }
    freeaddrinfo(ai);
    char req[1024];
    int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    send(s, req, n, 0);
    size_t cap = 1 << 16, len = 0;
    char *buf = malloc(cap);
    ssize_t r;
    while ((r = recv(s, buf + len, cap - len - 1, 0)) > 0) {
        len += r;
        if (len + 1 == cap) buf = realloc(buf, cap *= 2);
    }
    close(s);
    buf[len] = 0;
    char *p = strstr(buf, "\r\n\r\n");
    if (!p || strncmp(buf + 9, "200", 3)) { free(buf); return -1; }
    p += 4;
    long blen = len - (p - buf);
    memmove(buf, p, blen + 1);
    *body = buf;
    return blen;
}

// 和 latency_probe 相同的条码
static int read_code(const AVFrame *f, uint32_t *ms) {
    uint64_t v = 0;
    for (int b = 0; b < 40; b++) {
        int sum = 0;
        for (int y = 12; y < 20; y++)
            for (int x = 12; x < 20; x++) sum += f->data[0][y * f->linesize[0] + b * 32 + x];
        v = (v << 1) | (sum / 64 > 128);
    }
    uint32_t t = (uint32_t)(v >> 8);
    uint8_t s = (t & 0xff) + ((t >> 8) & 0xff) + ((t >> 16) & 0xff) + (t >> 24);
    if ((uint8_t)~s != (uint8_t)(v & 0xff)) return -1;
    *ms = t;
    return 0;
}

// 解码一个切片文件，取第一帧和最后一帧的条码
static int segment_times(const char *file, uint32_t *first, uint32_t *last, int *frames) {
    AVFormatContext *in = NULL;
    if (avformat_open_input(&in, file, NULL, NULL) < 0) return -1;
    avformat_find_stream_info(in, NULL);
    int vi = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    const AVCodec *dec = avcodec_find_decoder(in->streams[vi]->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, in->streams[vi]->codecpar);
    avcodec_open2(ctx, dec, NULL);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frm = av_frame_alloc();
    *frames = 0;
    for (int eof = 0; !eof;) {
        if (av_read_frame(in, pkt) < 0) { eof = 1; avcodec_send_packet(ctx, NULL); }
        else if (pkt->stream_index == vi) avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);
        uint32_t t;
        while (avcodec_receive_frame(ctx, frm) == 0)
            if (read_code(frm, &t) == 0) { if (!(*frames)++) *first = t; *last = t; }
    }
    av_frame_free(&frm);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&in);
    return *frames ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "用法: %s http://主机:端口/路径.m3u8 [秒数]\n", argv[0]); return 1; }
    int seconds = argc > 2 ? atoi(argv[2]) : 20;
    char path[512], dir[512];
    if (sscanf(argv[1], "http://%255[^:/]:%15[0-9]%511s", host, port, path) != 3) return 1;
    snprintf(dir, sizeof(dir), "%s", path);
    *strrchr(dir, '/') = 0;

    long last_seq = -1;
    int count = 0, first_fetch = 1;
    double sum_first = 0, sum_last = 0;
    time_t end = time(NULL) + seconds;
    while (time(NULL) < end) {
        char *m3u8;
        uint32_t t_list = now_ms();
        if (http_get(path, &m3u8) < 0) { usleep(100000); continue; }
        // 媒体序号：列表中第一个切片的序号，后面的依次加一
        long seq = 0;
        char *p = strstr(m3u8, "#EXT-X-MEDIA-SEQUENCE:");
        if (p) seq = atol(p + 22);
        for (char *line = strtok(m3u8, "\r\n"); line; line = strtok(NULL, "\r\n")) {
            if (line[0] == '#') continue;
            long cur = seq++;
            if (cur <= last_seq) continue;
            last_seq = cur;
            if (first_fetch) continue;  // 第一次取到的列表中已有的切片，不知道它们是什么时候出现的
            char seg[1024], *data;
            snprintf(seg, sizeof(seg), "%s/%s", dir, line);
            long n = http_get(seg, &data);
            if (n < 0) continue;
            FILE *f = fopen("seg.tmp", "wb");
            fwrite(data, 1, n, f);
            fclose(f);
            free(data);
            uint32_t a, b;
            int frames;
            if (segment_times("seg.tmp", &a, &b, &frames) < 0) continue;
            printf("切片 %-10s %3d 帧 %7ld 字节  出现时：第一帧已过去 %5d ms，最后一帧已过去 %4d ms\n",
                   line, frames, n, (int32_t)(t_list - a), (int32_t)(t_list - b));
            sum_first += (int32_t)(t_list - a);
            sum_last += (int32_t)(t_list - b);
            count++;
        }
        free(m3u8);
        first_fetch = 0;
        usleep(100000);  // 每 0.1 秒刷新一次列表
    }
    if (count) printf("平均：第一帧已过去 %.0f ms，最后一帧已过去 %.0f ms\n", sum_first / count, sum_last / count);
    return 0;
}
```

```bash
FF=$(brew --prefix ffmpeg)
clang -O2 hls_watch.c -o hls_watch -I$FF/include -L$FF/lib -lavformat -lavcodec -lavutil
./hls_watch http://127.0.0.1:8000/a/live.m3u8 13
```

```text
切片 live26.ts   48 帧  455148 字节  出现时：第一帧已过去  2023 ms，最后一帧已过去   69 ms
切片 live27.ts   48 帧  554036 字节  出现时：第一帧已过去  2010 ms，最后一帧已过去   61 ms
切片 live28.ts   48 帧  674920 字节  出现时：第一帧已过去  2106 ms，最后一帧已过去  144 ms
切片 live29.ts   48 帧  601788 字节  出现时：第一帧已过去  2026 ms，最后一帧已过去   61 ms
切片 live30.ts   48 帧  393296 字节  出现时：第一帧已过去  2043 ms，最后一帧已过去   80 ms
切片 live31.ts   48 帧  394236 字节  出现时：第一帧已过去  2048 ms，最后一帧已过去   88 ms
平均：第一帧已过去 2043 ms，最后一帧已过去 84 ms
```

切片出现的时候，它的最后一帧平均已经过去了 84 毫秒，第一帧已经过去了 2 秒多。这就是 HLS 延迟的第一部分：**一个切片的长度**。直播画面最新的那几秒，还在服务器上「攒」着，播放器根本拿不到。

FFmpeg 什么时候结束一个切片？它要看到下一个关键帧，才知道当前切片该结束了，所以切片总是在**下一个切片的第一帧**到来时才写完。最后一帧之后的那几十毫秒，一部分是在等下一个切片的第一帧（它在 42 毫秒后才生成），一部分是编码、写文件的时间，还有一部分是观察程序刷新列表的间隔（最多 0.1 秒）带来的误差。

## 4. 播放器从哪里开始播

拿到播放列表，播放器从哪个切片开始播？最新的那个？RFC 8216 规定：**播放器不应该从距离列表末尾不足 3 个目标时长的切片开始播放**。原因是网络有波动，下载一个切片的时间不固定，如果从最新的切片开始播，播完它时下一个切片可能还没生成，或者还没下载完，就会卡住。离末尾留 3 个切片，就有 3 个切片的缓冲。

于是延迟的第二部分是**起播位置**：约 3 个切片的时长。用上一篇的 `latency_probe` 测一下，FFmpeg 的 hls 解封装器默认从倒数第 3 个切片开始（`live_start_index` 默认是 -3），也可以改成 -1，从最后一个切片开始。三路推流的 GOP 和切片时长不同，每组测 25 秒：

| 切片时长 | 从倒数第 3 个开始（默认） | 从最后一个开始 | 卡顿次数（从最后一个开始） |
|---|---|---|---|
| 1 秒（GOP 1 秒） | 3.70 秒 | 2.14 秒 | 4 次 |
| 2 秒（GOP 1 秒） | 7.82 秒 | 3.39 秒 | 12 次 |
| 6 秒（GOP 2 秒） | 19.96 秒 | 9.26 秒 | 4 次 |

（表中是播放延迟的中位数，`play=1`。）

<figure class="diagram">
<svg viewBox="0 0 680 210" role="img" aria-label="HLS 延迟的组成：时间轴上依次是切片 16 到 21，每个 2 秒；切片 21 还在生成，不在列表里；列表的末尾是切片 20；播放器从倒数第 3 个切片 18 的开头开始播，此时画面比直播落后 3 个切片再加上切片 21 已经生成的部分，大约 6 到 8 秒">
  <defs>
    <marker id="hd-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="20" y="50" width="96" height="40" style="fill:var(--panel-2)"/>
      <rect x="116" y="50" width="96" height="40" style="fill:var(--panel-2)"/>
      <rect x="212" y="50" width="96" height="40" style="fill:var(--accent-soft)"/>
      <rect x="308" y="50" width="96" height="40" style="fill:var(--accent-soft)"/>
      <rect x="404" y="50" width="96" height="40" style="fill:var(--accent-soft)"/>
      <rect x="500" y="50" width="70" height="40" style="fill:var(--mark);stroke-dasharray:3 3"/>
    </g>
    <text x="40" y="75" style="fill:var(--fg)">16.ts</text><text x="136" y="75" style="fill:var(--fg)">17.ts</text><text x="232" y="75" style="fill:var(--fg)">18.ts</text><text x="328" y="75" style="fill:var(--fg)">19.ts</text><text x="424" y="75" style="fill:var(--fg)">20.ts</text><text x="506" y="75" style="fill:var(--fg)">21 生成中</text>
    <text x="20" y="40" style="fill:var(--muted);font-size:10px">← 列表中的切片（每个 2 秒）</text>
    <line x1="570" y1="20" x2="570" y2="135" style="stroke:var(--danger)" stroke-width="2"/>
    <text x="578" y="30" style="fill:var(--danger);font-size:11px">现在</text><text x="578" y="45" style="fill:var(--danger);font-size:10px">（直播画面）</text>
    <line x1="212" y1="40" x2="212" y2="135" style="stroke:var(--accent)" stroke-width="2"/>
    <text x="150" y="108" style="fill:var(--accent);font-size:11px">播放器从这里开始</text>
    <g style="stroke:var(--muted)" fill="none">
      <line x1="215" y1="125" x2="567" y2="125" marker-end="url(#hd-arrow)"/>
      <line x1="215" y1="160" x2="497" y2="160" marker-end="url(#hd-arrow)"/>
      <line x1="503" y1="160" x2="567" y2="160" marker-end="url(#hd-arrow)"/>
    </g>
    <text x="330" y="120" style="fill:var(--fg);font-size:11px">延迟</text>
    <text x="270" y="178" style="fill:var(--muted);font-size:11px">起播位置：3 个切片</text>
    <text x="500" y="178" style="fill:var(--muted);font-size:11px">还在攒的</text><text x="500" y="192" style="fill:var(--muted);font-size:11px">0～1 个切片</text>
  </g>
</svg>
</figure>

延迟 ≈ 3 个切片 + 正在生成的那个切片已经攒下的部分（0 到 1 个切片，取决于播放器连上来的时机）+ 下载时间。2 秒的切片：6 到 8 秒，实测 7.82 秒；6 秒的切片：18 到 24 秒，实测 19.96 秒。很多直播平台的 HLS 用 6～10 秒的切片，延迟 20～30 秒就是这样来的。

浏览器中常用的 hls.js 算法略有不同：它用 `liveSyncDurationCount`（默认 3）个目标时长，从列表的末尾往前推算起播的时间点，不必对齐到切片的开头。同样是 2 秒的切片，在浏览器中测得 6.07 秒，hls.js 自己估计的延迟是 6.01 秒，正好 3 个切片。浏览器中的测法下一篇详细介绍。

### 4.1 从最后一个切片开始播

从最后一个切片开始，延迟确实减少了一半多，代价是卡顿：刚开始只有一个切片的缓冲，网速稍有波动、或者新切片晚出现一会儿，播放器就没东西可播了。2 秒切片的那一组卡了 12 次。每卡一次，播放时钟就往后推一点，延迟又涨回去一些（上一篇讲过：落下的时间是追不回来的）。所以表中「从最后一个开始」的延迟比 1 个切片要大不少。

缩短切片同样有代价：GOP 必须跟着缩短，画质下降；切片越多，HTTP 请求越多，CDN 的压力越大；播放列表也要刷新得更勤。实践中 HLS 的切片很少短于 2 秒。

## 5. LL-HLS

为了降低 HLS 的延迟，Apple 在 2019 年推出了 LL-HLS（Low-Latency HLS），它已经写进了 HLS 规范第二版的草案（RFC 8216bis）。核心思路是：**切片照旧，但切片还没写完时就把它的一部分发出去**。MediaMTX 默认就输出 LL-HLS，它的媒体播放列表是这样的（节选）：

```text
#EXTM3U
#EXT-X-VERSION:10
#EXT-X-TARGETDURATION:1
#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES,PART-HOLD-BACK=0.52250,CAN-SKIP-UNTIL=6.00000
#EXT-X-PART-INF:PART-TARGET=0.20900
#EXT-X-MEDIA-SEQUENCE:1
#EXT-X-MAP:URI="7dd7e783f189_video1_init.mp4"
...
#EXT-X-PROGRAM-DATE-TIME:2026-11-01T14:10:53.928+08:00
#EXT-X-PART:DURATION=0.20800,URI="7dd7e783f189_video1_part0.mp4",INDEPENDENT=YES
#EXT-X-PART:DURATION=0.20900,URI="7dd7e783f189_video1_part1.mp4"
#EXT-X-PART:DURATION=0.20800,URI="7dd7e783f189_video1_part2.mp4"
#EXT-X-PART:DURATION=0.20800,URI="7dd7e783f189_video1_part3.mp4"
#EXT-X-PART:DURATION=0.16700,URI="7dd7e783f189_video1_part4.mp4"
#EXTINF:1.00000,
7dd7e783f189_video1_seg7.mp4
#EXT-X-PRELOAD-HINT:TYPE=PART,URI="7dd7e783f189_video1_part5.mp4"
```

- **部分切片**（`EXT-X-PART`）：每个 1 秒的切片又分成 5 个约 0.2 秒的部分切片，每写完一个就加进列表。`INDEPENDENT=YES` 表示这个部分切片以关键帧开头，可以从它开始播放。部分切片只是普通切片的一部分，播放器可以下载部分切片，也可以等整个切片写完再下载。
- **阻塞式刷新**（`CAN-BLOCK-RELOAD=YES`）：播放器请求播放列表时，可以带上参数说「我要序号 8 的切片的第 2 个部分出现以后的列表」，服务器在它出现之前不回复，一出现就立刻回复。播放器不必傻等刷新间隔，也不会白白多刷新。
- **预加载提示**（`EXT-X-PRELOAD-HINT`）：告诉播放器下一个部分切片的地址，播放器可以提前发出请求，服务器等它写完就立刻发出。
- **`PART-HOLD-BACK`**：播放器应当离列表末尾至少多远，这里是 0.52 秒，大约 2.5 个部分切片，替代了原来的「3 个切片」。

同一路流（上一篇推到 MediaMTX 的那一路，GOP 1 秒），分别用三种方式播放：

| 播放器 | 延迟（中位数） |
|---|---|
| hls.js，打开低延迟模式（`lowLatencyMode: true`，默认） | 0.73 秒 |
| hls.js，关闭低延迟模式 | 3.19 秒 |
| FFmpeg 的 hls 解封装（不支持部分切片） | 3.34 秒 |

不认识部分切片的播放器会忽略它们，按普通的 1 秒切片、从倒数第 3 个开始播，延迟 3 秒多，所以 LL-HLS 向下兼容。支持 LL-HLS 的播放器延迟降到了 0.73 秒，已经和很多「低延迟」协议差不多了。不过它对服务器和 CDN 的要求也高得多：要支持阻塞请求、及时更新播放列表，每秒的请求数也多了好几倍。

## 6. 小结

- HLS 把直播切成小文件，用 m3u8 播放列表列出最近的切片，全程只用 HTTP，能用 CDN，支持自适应码率，代价是延迟大。
- 切片必须以关键帧开头，切片时长至少是一个 GOP。
- 切片要写完才能出现在列表里：延迟至少一个切片。
- 播放器从倒数第 3 个切片附近开始播：再加约 3 个切片。HLS 的延迟大约是 3～4 个切片的时长。
- 从最后一个切片开始播、缩短切片都能降低延迟，代价分别是卡顿和画质、请求数。
- LL-HLS 用 0.2 秒左右的部分切片、阻塞式刷新和预加载提示，把延迟降到 1 秒以内，而且兼容旧的播放器。

下一篇把前面所有的协议放在一起，用同一路流比较它们的延迟，再看看在丢包的网络上它们各自的表现。

## 练习

1. 推流的 GOP 是 3 秒，`hls_time` 设成 4 秒，FFmpeg 生成的切片各是多长？
2. `hls_list_size` 是 6，打开了 `delete_segments`，切片文件是在它刚被移出列表时立刻删除的吗？为什么不能立刻删除？
3. 一个 HLS 播放器每隔多久刷新一次播放列表比较合适？刷新太慢、太快各有什么问题？

<details>
<summary>点击查看答案</summary>

1. 实测（推 26 秒）的切片时长是 6、3、3、6、3、3 秒（最后一个不满）。不是每个都等于「4 秒之后的第一个关键帧」那样的 6 秒。FFmpeg 的切片目标是**累计**的：第 n 个切片应当在 n × 4 秒处结束。关键帧在 0、3、6、9……秒，第 1 个切片要到 4 秒以后的第一个关键帧 6 秒才能结束；第 2 个的目标是 8 秒，到 9 秒结束，只有 3 秒；第 3 个的目标是 12 秒，正好在 12 秒结束；第 4 个的目标是 16 秒，要到 18 秒……这样切片的总时长始终贴着 4 秒 × 个数，不会越来越偏。`TARGETDURATION` 写的是最长的切片，6 秒。这个例子也说明：GOP 和切片时长不成整数倍时，切片长短不一，最好让切片时长是 GOP 的整数倍。

2. 不是。FFmpeg 的 `hls_delete_threshold`（默认 1）表示在列表之外再保留几个切片才删除。原因是播放器拿到的播放列表可能是几秒钟前的：它刚从列表里看到一个切片，正准备下载，服务器那边这个切片已经被移出了列表，如果立刻删掉，播放器就会下载失败（404）。有 CDN 时更是如此，CDN 上缓存的播放列表可能更旧。所以要多留一会儿，保证「列表中出现过的切片，在一段时间内都能下载到」。

3. RFC 8216 规定：第一次加载、或者刷新后发现播放列表变了，播放器至少要等一个目标时长再刷新；刷新后发现列表没变，等半个目标时长再试。刷新太慢，新切片出现后要过很久才能发现，延迟增加；刷新太快，大量请求拿回的都是没变化的列表，浪费流量，给服务器和 CDN 增加压力。第 3 节的观察程序每 0.1 秒刷新一次是为了测量，正常的播放器不应该这样做。LL-HLS 的阻塞式刷新解决了这个矛盾：请求发出去以后，服务器等到有新内容才回复，既不白刷新，也不会晚发现。

</details>
