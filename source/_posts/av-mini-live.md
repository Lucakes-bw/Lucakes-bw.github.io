---
title: 迷你直播系统
date: 2026-11-04 11:00:00
updated: 2026-11-04 11:00:00
categories:
  - 音视频开发
  - 音视频实战
tags:
  - 音视频
  - 直播
  - HTTP-FLV
  - GOP 缓存
  - C
order: 3
description: 把整个专栏串起来，自己搭一套直播系统：推流端采集屏幕、零延迟编码、封装成 FLV 推出去；用 C 写一个不到 250 行的服务器，接收 FLV、解析出一个个 Tag，用 HTTP-FLV 转发给任意多个观众，支持 GOP 缓存和慢观众丢帧；观众用上两篇的 Qt 播放器。实测 GOP 缓存让首帧从 0.34～0.82 秒缩短到 10 毫秒以内，代价是 33～773 毫秒的额外延迟，播放器加上追帧后又降到 12～24 毫秒。5 个观众同时看，服务器 CPU 占用 0.3%。限速 200 KB/s 的慢观众，服务器丢帧也没能阻止延迟涨到 6.9 秒，原来积压的数据大都躺在内核的 TCP 缓冲区里；把缓冲区调小后，丢帧才把延迟稳定在 3 秒左右。最后测量从屏幕上画出画面到播放器显示的全程：中位数约 0.1 秒，其中绝大部分花在采集上。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解一套直播系统的完整链路；会用 C 写一个支持多观众的 HTTP-FLV 直播服务器；理解 GOP 缓存、播放器追帧、服务器丢帧各自解决什么问题、付出什么代价；认识内核 TCP 缓冲区对直播延迟的影响；测量从屏幕到播放器的端到端延迟。
>
> **前置知识**：本章前两篇；第 2 章《FLV 封装格式》；第 5 章全章；网络专栏《TCP Socket 入门》《流量控制与拥塞控制》、多路复用（poll）。
>
> **实验环境**：macOS 27.0（Apple M6），Qt 6.12.0，FFmpeg 9.0.2，SDL 3.4，Apple Clang 21。

## 1. 整体结构

<figure class="diagram">
<svg viewBox="0 0 680 230" role="img" aria-label="迷你直播系统：推流端 screen_push 采集屏幕、编码、封装成 FLV，通过 TCP 推到服务器的 9000 端口；服务器 live_server 解析 FLV Tag，保存元数据、序列头和 GOP 缓存，通过 8080 端口的 HTTP-FLV 把 Tag 转发给多个观众：Qt 播放器、latency_probe、ffplay 等">
  <defs>
    <marker id="ml-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="60" width="150" height="100" rx="6" style="fill:var(--panel-2)"/>
      <rect x="250" y="40" width="190" height="140" rx="6" style="fill:var(--mark)"/>
      <rect x="530" y="20" width="145" height="44" rx="6" style="fill:var(--panel-2)"/>
      <rect x="530" y="88" width="145" height="44" rx="6" style="fill:var(--panel-2)"/>
      <rect x="530" y="156" width="145" height="44" rx="6" style="fill:var(--panel-2)"/>
    </g>
    <text x="15" y="82" style="fill:var(--fg)">screen_push</text>
    <text x="15" y="102" style="fill:var(--muted);font-size:10px">采集屏幕</text>
    <text x="15" y="118" style="fill:var(--muted);font-size:10px">x264 零延迟编码</text>
    <text x="15" y="134" style="fill:var(--muted);font-size:10px">封装成 FLV</text>
    <text x="15" y="150" style="fill:var(--muted);font-size:10px">（或 live_push 推文件）</text>
    <text x="262" y="62" style="fill:var(--fg)">live_server</text>
    <text x="262" y="82" style="fill:var(--muted);font-size:10px">解析 FLV Tag</text>
    <text x="262" y="98" style="fill:var(--muted);font-size:10px">保存元数据、序列头</text>
    <text x="262" y="114" style="fill:var(--muted);font-size:10px">GOP 缓存</text>
    <text x="262" y="130" style="fill:var(--muted);font-size:10px">每个观众一个发送队列</text>
    <text x="262" y="146" style="fill:var(--muted);font-size:10px">太慢就丢帧</text>
    <text x="262" y="166" style="fill:var(--muted);font-size:10px">单线程 + poll</text>
    <text x="540" y="40" style="fill:var(--fg)">qplayer</text><text x="540" y="56" style="fill:var(--muted);font-size:10px">本章的 Qt 播放器</text>
    <text x="540" y="108" style="fill:var(--fg)">latency_probe</text><text x="540" y="124" style="fill:var(--muted);font-size:10px">第 5 章</text>
    <text x="540" y="176" style="fill:var(--fg)">ffplay、VLC…</text><text x="540" y="192" style="fill:var(--muted);font-size:10px">任何 HTTP-FLV 播放器</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#ml-arrow)">
      <line x1="155" y1="110" x2="248" y2="110"/>
      <path d="M440,95 L490,42 L528,42"/><line x1="440" y1="110" x2="528" y2="110"/><path d="M440,125 L490,178 L528,178"/>
    </g>
    <text x="165" y="100" style="fill:var(--muted);font-size:10px">TCP :9000</text><text x="165" y="128" style="fill:var(--muted);font-size:10px">裸 FLV</text>
    <text x="455" y="218" style="fill:var(--muted);font-size:10px">HTTP :8080</text>
  </g>
</svg>
</figure>

- **推流端**：采集屏幕（第 5 章《采集》），缩放、零延迟编码，封装成 FLV，直接写到一个 TCP 连接上。为了让服务器尽量简单，没有用 RTMP（第 5 章看到，RTMP 要握手、分块、AMF 命令），FLV 本身就是一个可以「流式」写的格式：一个文件头，后面是一个接一个的 Tag。
- **服务器**：接收 FLV，切成 Tag，转发给所有观众。观众用 HTTP GET 请求 `/live.flv`，服务器回复一个 HTTP 头，然后不停地发 FLV，永不结束，这就是 **HTTP-FLV**。国内的直播平台曾大量使用它：延迟和 RTMP 一样低，又走 HTTP，能穿过防火墙、能用 CDN；浏览器里用 flv.js 也能播。
- **观众**：任何能播 HTTP-FLV 的播放器，本篇主要用上两篇的 `qplayer` 和第 5 章的 `latency_probe`。

## 2. 服务器

```c
// live_server.c：最简单的直播服务器
//   推流端用 TCP 把 FLV 推到 9000 端口；观众用 HTTP 访问 8080 端口，拿到的是 HTTP-FLV
//   单线程，用 poll 同时处理所有连接；支持 GOP 缓存，观众太慢时丢帧
// 用法：live_server [gop_cache=1] [max_pending=KB，0 表示不丢帧] [sndbuf=KB，观众连接的内核发送缓冲区，0 表示系统默认]
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct { uint8_t *data; size_t len, cap; } Buf;

static void buf_add(Buf *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}
static void buf_drop(Buf *b, size_t n) {   // 扔掉开头 n 个字节
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
}

#define MAX_VIEWERS 64
typedef struct {
    int fd;
    int ready;          // 收到 HTTP 请求、发过头部了
    int skipping;       // 太慢了，正在丢帧，等下一个关键帧
    Buf in, out;        // 收到的请求，待发送的数据
    long sent, dropped;
} Viewer;

static int pub_fd = -1;
static Buf pub_in;                      // 推流端发来、还没解析完的数据
static int header_done;
static Buf flv_header, meta_tag, video_seq, audio_seq, gop;   // 新观众需要的东西
static Viewer viewers[MAX_VIEWERS];
static int gop_cache = 1;
static size_t max_pending = 0;
static int sndbuf = 0;

static int listen_on(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    // 只监听本机。要给局域网里的其他电脑看，改成 INADDR_ANY（注意推上来的内容会被同一网络中的任何人看到）
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (bind(fd, (void *)&a, sizeof(a)) || listen(fd, 16)) { perror("bind"); exit(1); }
    return fd;
}
static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

// 一个 Tag：类型 1 字节、数据长度 3 字节、时间戳 4 字节、流 ID 3 字节，然后是数据和 4 字节的 PreviousTagSize
static int is_video_key(const uint8_t *tag) { return tag[0] == 9 && (tag[11] >> 4) == 1; }
static int is_seq_header(const uint8_t *tag) {
    return (tag[0] == 9 && (tag[11] & 0x0f) == 7 && tag[12] == 0) ||     // AVC 序列头（SPS、PPS）
           (tag[0] == 8 && (tag[11] >> 4) == 10 && tag[12] == 0);        // AAC 序列头
}

// 给一个观众发一个 Tag；它积压得太多就开始丢，直到下一个关键帧
static void send_tag(Viewer *v, const uint8_t *tag, size_t n) {
    if (max_pending && v->out.len > max_pending) v->skipping = 1;
    if (v->skipping) {
        // 从关键帧重新开始；如果是因为太慢才丢的，还要等积压消化掉一半
        if (is_video_key(tag) && (!max_pending || v->out.len <= max_pending / 2)) v->skipping = 0;
        else { if (tag[0] == 9) v->dropped++; return; }
    }
    buf_add(&v->out, tag, n);
    if (tag[0] == 9) v->sent++;
}

static void on_tag(const uint8_t *tag, size_t n) {
    if (tag[0] == 18) { meta_tag.len = 0; buf_add(&meta_tag, tag, n); }
    else if (is_seq_header(tag)) {
        Buf *b = tag[0] == 9 ? &video_seq : &audio_seq;
        b->len = 0;
        buf_add(b, tag, n);
    } else if (gop_cache) {
        // GOP 缓存：遇到关键帧就清空，只保留从最近一个关键帧开始的所有 Tag
        if (is_video_key(tag)) gop.len = 0;
        if (gop.len || is_video_key(tag)) buf_add(&gop, tag, n);
    }
    for (int i = 0; i < MAX_VIEWERS; i++)
        if (viewers[i].fd >= 0 && viewers[i].ready) send_tag(&viewers[i], tag, n);
}

static void on_publisher_data(void) {
    if (!header_done) {
        if (pub_in.len < 13) return;                    // FLV 文件头 9 字节 + PreviousTagSize0
        flv_header.len = 0;
        buf_add(&flv_header, pub_in.data, 13);
        buf_drop(&pub_in, 13);
        header_done = 1;
    }
    while (pub_in.len >= 11) {
        const uint8_t *p = pub_in.data;
        size_t size = (size_t)p[1] << 16 | p[2] << 8 | p[3];
        size_t total = 11 + size + 4;
        if (pub_in.len < total) return;
        on_tag(p, total);
        buf_drop(&pub_in, total);
    }
}

// 观众的 HTTP 请求收齐了：回复头部，再把文件头、元数据、序列头和 GOP 缓存发过去
static void start_viewer(Viewer *v) {
    const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: video/x-flv\r\nConnection: close\r\n"
                       "Access-Control-Allow-Origin: *\r\n\r\n";
    buf_add(&v->out, resp, strlen(resp));
    buf_add(&v->out, flv_header.data, flv_header.len);
    buf_add(&v->out, meta_tag.data, meta_tag.len);
    buf_add(&v->out, video_seq.data, video_seq.len);
    buf_add(&v->out, audio_seq.data, audio_seq.len);
    if (gop_cache) buf_add(&v->out, gop.data, gop.len);
    else v->skipping = 1;                               // 没有缓存：等下一个关键帧
    v->ready = 1;
}

static void close_viewer(Viewer *v) {
    fprintf(stderr, "观众 %d 断开：发出 %ld 帧，丢掉 %ld 帧\n", v->fd, v->sent, v->dropped);
    close(v->fd);
    free(v->in.data); free(v->out.data);
    memset(v, 0, sizeof(*v));
    v->fd = -1;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "gop_cache=", 10)) gop_cache = atoi(argv[i] + 10);
        if (!strncmp(argv[i], "max_pending=", 12)) max_pending = (size_t)atoi(argv[i] + 12) * 1024;
        if (!strncmp(argv[i], "sndbuf=", 7)) sndbuf = atoi(argv[i] + 7) * 1024;
    }
    signal(SIGPIPE, SIG_IGN);                           // 观众断开时 send 会触发 SIGPIPE，忽略它
    int pub_listen = listen_on(9000), http_listen = listen_on(8080);
    for (int i = 0; i < MAX_VIEWERS; i++) viewers[i].fd = -1;
    fprintf(stderr, "推流 tcp://127.0.0.1:9000，观看 http://127.0.0.1:8080/live.flv，GOP 缓存 %s，积压上限 %zu KB\n",
            gop_cache ? "开" : "关", max_pending / 1024);
    time_t last_report = time(NULL);
    for (;;) {
        struct pollfd pf[3 + MAX_VIEWERS];
        int n = 0;
        pf[n++] = (struct pollfd){pub_listen, POLLIN, 0};
        pf[n++] = (struct pollfd){http_listen, POLLIN, 0};
        pf[n++] = (struct pollfd){pub_fd, POLLIN, 0};   // fd 为 -1 时 poll 会忽略
        for (int i = 0; i < MAX_VIEWERS; i++)
            pf[n++] = (struct pollfd){viewers[i].fd, (short)(POLLIN | (viewers[i].out.len ? POLLOUT : 0)), 0};
        poll(pf, n, 1000);

        if (pf[0].revents & POLLIN) {                   // 新的推流端（只允许一个，新的替换旧的）
            if (pub_fd >= 0) close(pub_fd);
            pub_fd = accept(pub_listen, NULL, NULL);
            nonblock(pub_fd);
            pub_in.len = 0; header_done = 0; gop.len = 0;
            fprintf(stderr, "推流端连上了\n");
        }
        if (pf[1].revents & POLLIN) {                   // 新的观众
            int fd = accept(http_listen, NULL, NULL);
            int i = 0;
            while (i < MAX_VIEWERS && viewers[i].fd >= 0) i++;
            if (i == MAX_VIEWERS) close(fd);
            else {
                // 内核的发送缓冲区设小一点，观众慢下来时数据才会积压在我们自己的队列里，丢帧策略才管得到
                if (sndbuf) setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
                nonblock(fd);
                viewers[i].fd = fd;
                fprintf(stderr, "观众 %d 连上了\n", fd);
            }
        }
        if (pf[2].revents & (POLLIN | POLLHUP)) {
            uint8_t tmp[65536];
            ssize_t r = recv(pub_fd, tmp, sizeof(tmp), 0);
            if (r <= 0) { close(pub_fd); pub_fd = -1; fprintf(stderr, "推流端断开\n"); }
            else { buf_add(&pub_in, tmp, r); on_publisher_data(); }
        }
        for (int i = 0; i < MAX_VIEWERS; i++) {
            Viewer *v = &viewers[i];
            short ev = pf[3 + i].revents;
            if (v->fd < 0 || !ev) continue;
            if (ev & (POLLIN | POLLHUP | POLLERR)) {
                char tmp[2048];
                ssize_t r = recv(v->fd, tmp, sizeof(tmp), 0);
                if (r <= 0) { close_viewer(v); continue; }
                buf_add(&v->in, tmp, r);
                if (!v->ready && v->in.len >= 4 && memmem(v->in.data, v->in.len, "\r\n\r\n", 4)) {
                    if (!header_done) { close_viewer(v); continue; }   // 还没有人推流
                    start_viewer(v);
                }
            }
            if ((ev & POLLOUT) && v->out.len) {
                ssize_t w = send(v->fd, v->out.data, v->out.len, 0);
                if (w > 0) buf_drop(&v->out, w);
                else if (w < 0 && errno != EAGAIN) close_viewer(v);
            }
        }
        if (time(NULL) - last_report >= 5) {            // 每 5 秒报告一次每个观众的积压
            last_report = time(NULL);
            for (int i = 0; i < MAX_VIEWERS; i++)
                if (viewers[i].fd >= 0 && viewers[i].ready)
                    fprintf(stderr, "  观众 %d：积压 %zu KB，发出 %ld 帧，丢掉 %ld 帧\n", viewers[i].fd,
                            viewers[i].out.len / 1024, viewers[i].sent, viewers[i].dropped);
        }
    }
}
```

```bash
clang -O2 live_server.c -o live_server
./live_server gop_cache=1
```

### 2.1 一个观众要什么

新观众连上来时，不能从直播当前的那一个 Tag 开始发，它需要：

1. **FLV 文件头**（9 字节加 4 字节的 PreviousTagSize0）：否则播放器连格式都认不出来。
2. **元数据**（Script Tag，`onMetaData`）：宽、高、编码等。
3. **序列头**：AVC 序列头里是 SPS 和 PPS，AAC 序列头里是 AudioSpecificConfig（第 2 章《FLV 封装格式》）。没有它们，解码器无法初始化。推流端只在最开始发一次，服务器必须把它们存下来，发给每一个后来的观众。
4. **从关键帧开始的视频**：从 P 帧开始，解码器解不了（第 5 章 RTMP 篇看到 MediaMTX 会等下一个关键帧）。

服务器在解析推流的数据时，把前三样存下来；第四样有两种做法：等下一个关键帧（`gop_cache=0`），或者缓存最近一个 GOP，新观众一来就把整个 GOP 发过去（`gop_cache=1`）。

### 2.2 单线程处理所有连接

服务器只有一个线程，用 `poll` 同时等待推流端、所有观众的连接（网络专栏的多路复用）：

- **推流端可读**：收到的数据追加到缓冲区，按 Tag 的长度字段切出完整的 Tag，交给 `on_tag`：更新缓存，再追加到每个观众的发送队列。
- **观众可写**：把发送队列里的数据尽量发出去，发出多少就从队列里删掉多少。所有 socket 都是非阻塞的，一个观众卡住了，不会影响其他人。
- **观众可读**：只在一开始读 HTTP 请求，收到空行（`\r\n\r\n`）就开始发数据。

每个观众有自己的发送队列，因为每个人的网速不同，进度也不同。这和第 4 章播放器中「队列连接各个线程」是一个道理，只不过这里的消费者是网络。

### 2.3 只监听本机

服务器绑定的地址是 `127.0.0.1`。如果改成 `INADDR_ANY`（`0.0.0.0`），同一个局域网里的任何人都能连上 8080 端口观看，也能连上 9000 端口推流，把别人的直播顶掉。本篇推的是自己的屏幕，这样做尤其危险。真正的直播服务器要有鉴权（推流和播放的地址里带上签名、有效期），本篇的玩具服务器只给本机用。

## 3. 推文件、看延迟

先用第 5 章的 `live_push` 推测试视频（它的封装格式参数直接写 `flv`，URL 写成 `tcp://`），用 `latency_probe` 测延迟：

```bash
./live_push bbb-10s.mp4 tcp://127.0.0.1:9000 flv 1 seconds=2000
./latency_probe http://127.0.0.1:8080/live.flv 10 analyzeduration=0 probesize=32
```

```text
打开到第一帧 11 ms
帧数 248  坏帧 0  卡顿 0 次  到达延迟 最小 2 / 中位 8 / 最大 290 / 最后 2 ms
```

到达延迟只有 8 毫秒，比第 5 章经过 MediaMTX 的 RTMP（17 毫秒）还低：FLV 直接从推流端原样转发到观众，中间没有任何协议转换。打开到第一帧只要 11 毫秒，是 GOP 缓存的功劳；最大延迟 290 毫秒，也是它的「副作用」，下一节细看。

### 3.1 GOP 缓存的代价

分别打开、关闭 GOP 缓存，各在随机的时刻连接 6 次，按 PTS 匀速播放（`play=1`），记录打开到第一帧的时间和延迟的中位数：

| | 打开到第一帧 | 播放延迟 |
|---|---|---|
| GOP 缓存开 | 7～10 ms | 33～773 ms |
| GOP 缓存关 | 341～816 ms | 15～25 ms |

开了 GOP 缓存，画面几乎立刻出来，但延迟变得很随机：GOP 是 1 秒，缓存里是从最近一个关键帧到现在的所有帧，连上来时这个 GOP 已经过去了多久，播放器就落后多久，而且像第 5 章讲的，按 PTS 匀速播放的播放器永远追不回来。关掉 GOP 缓存，要等下一个关键帧才能看到画面，平均半个 GOP，但延迟始终很低。

### 3.2 播放器追帧

鱼和熊掌能不能兼得？可以：先用 GOP 缓存让画面马上出来，再让播放器把落后的部分「追」回来。`qplayer` 的追帧逻辑（上一篇 `onTick` 中）是：没有声音的直播流，帧队列里持续 1 秒以上积压着 3 帧或更多，就把队列丢到只剩 1 帧，时钟对齐到新的队头。

第一版的条件是「积压 8 帧以上」，结果一次都没触发：打印队列长度一看，GOP 缓存带来的落后，在队列里表现为稳定地积压 6～7 帧，从来没到过 8 帧。改成「持续积压 3 帧以上」后：

| | 丢掉的帧 | 延迟（中位数） |
|---|---|---|
| 不追帧 | 0 | 242 ms、763 ms（两次） |
| 追帧 | 7～15 | 12～24 ms（三次） |

连上后大约 1 秒，播放器一次性丢掉 7～15 帧（观众会看到画面跳一下），之后延迟就和关掉 GOP 缓存时一样低了。各家直播 App 的播放器基本都是这样做的，只是有声音时不能直接丢，要用变速播放来追。

### 3.3 多个观众

同时启动 5 个 `latency_probe` 观看：

```text
观众1 | 打开到第一帧 11 ms 帧数 361  坏帧 0  卡顿 0 次  播放延迟 最小 506 / 中位 516 / 最大 526 / 最后 514 ms
观众2 | 打开到第一帧 11 ms 帧数 361  坏帧 0  卡顿 0 次  播放延迟 最小 506 / 中位 516 / 最大 526 / 最后 523 ms
...
观众5 | 打开到第一帧 8 ms 帧数 361  坏帧 0  卡顿 0 次  播放延迟 最小 505 / 中位 515 / 最大 525 / 最后 521 ms
```

5 个观众几乎同时连上，拿到的是同一份 GOP 缓存，延迟完全一样。服务器的 CPU 占用是 0.3%：它只是在搬运字节，不解码、不转码。一路 2 Mbps 的流，1000 个观众就是 2 Gbps，真正的瓶颈是带宽，这也是直播要用 CDN 的原因。

## 4. 慢观众

第 5 章 RTMP 篇看到，网速跟不上时，TCP 上的延迟会越积越大。服务器能不能帮忙？`live_server` 的 `max_pending` 参数给每个观众的发送队列设了上限：积压超过上限，就开始丢帧，一直丢到下一个关键帧，并且积压降到上限的一半以下才恢复发送。

用 `curl --limit-rate 200k` 模拟一个网速只有 200 KB/s 的观众（推流的码率约 276 KB/s），接给 `latency_probe`。先看不丢帧和丢帧（`max_pending=300`，约 1 秒的数据）的对比：

| | 30 秒时的延迟 | 服务器队列中的积压 | 丢掉的帧 |
|---|---|---|---|
| 不丢帧 | 8.3 s | 788 KB | 0 |
| 丢帧，上限 300 KB | 7.8 s | 18 KB | 81 |

丢帧几乎没有效果，延迟照样一路涨到 7.8 秒，可服务器队列里只积压了 18 KB。数据去哪里了？算一下：延迟 7.8 秒，码率 276 KB/s，路上应该有 2 MB 左右的数据，服务器队列里却几乎没有。它们在**内核的 TCP 缓冲区**里：服务器 socket 的发送缓冲区，加上 curl 那一端的接收缓冲区。macOS 会根据连接的情况自动调大这两个缓冲区，可以装下好几秒的视频。服务器调用 `send` 时，数据只是被复制进了内核的发送缓冲区，`send` 立刻返回成功，服务器以为发出去了，其实还在本机排队。只有内核缓冲区满了，积压才会出现在服务器自己的队列里，丢帧策略才看得到。

要验证这一点，需要能控制两端的缓冲区大小。`live_server` 的 `sndbuf` 参数用 `setsockopt(SO_SNDBUF)` 设置观众连接的发送缓冲区；curl 不能设置接收缓冲区，就自己写一个慢观众：用 `SO_RCVBUF` 设置接收缓冲区，按限定的速度读，读到的 FLV 写到标准输出，接给 `latency_probe`：

<details>
<summary>slow_viewer.c</summary>

```c
// slow_viewer.c：一个网速很慢的观众。用很小的接收缓冲区连上直播服务器，按限定的速度读，
// 读到的 FLV（去掉 HTTP 头）写到标准输出，可以接给 latency_probe
// 用法：slow_viewer 端口 KB/s [接收缓冲区KB]
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "用法: %s 端口 KB/s [接收缓冲区KB]\n", argv[0]); return 1; }
    double rate = atof(argv[2]) * 1024;
    int rcvbuf = argc > 3 ? atoi(argv[3]) * 1024 : 0;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (rcvbuf) setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));   // 要在 connect 之前设置
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(atoi(argv[1])),
                            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (connect(s, (void *)&a, sizeof(a))) { perror("connect"); return 1; }
    const char *req = "GET /live.flv HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(s, req, strlen(req), 0);
    char buf[4096];
    int header = 1;
    double t0 = now(), got = 0;
    for (;;) {
        double due = t0 + got / rate;               // 按限定的速度：读了多少字节，就要过了多少时间
        if (due > now()) usleep((useconds_t)((due - now()) * 1e6));
        ssize_t n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        got += n;
        char *p = buf;
        if (header) {                               // 跳过 HTTP 响应头
            char *e = memmem(buf, n, "\r\n\r\n", 4);
            if (!e) continue;
            p = e + 4;
            header = 0;
        }
        if (fwrite(p, 1, n - (p - buf), stdout) == 0) break;
        fflush(stdout);
    }
    return 0;
}
```

</details>

```bash
./live_server gop_cache=1 max_pending=300 sndbuf=32
./slow_viewer 8080 200 32 | ./latency_probe pipe:0 30 analyzeduration=0 probesize=32
```

四种组合，各测 30 秒（每 5 秒取一个延迟值）：

| 发送、接收缓冲区 | 服务器丢帧 | 延迟随时间的变化（0、5、10……30 秒） | 服务器队列的积压 | 丢掉的帧 |
|---|---|---|---|---|
| 系统默认 | 否 | 2.7 → 4.4 → 4.5 → 6.1 → 6.9 → 7.8 → 7.8 s | 602 KB | 0 |
| 32 KB | 否 | 3.1 → 5.1 → 5.6 → 6.5 → 7.7 → 8.4 → 8.4 s | 1782 KB | 0 |
| 32 KB | 是 | 3.1 → 2.5 → 3.1 → 2.7 → 3.2 → 2.7 → 3.0 s | 135 KB | 121 |
| 系统默认 | 是 | 3.1 → 5.1 → 5.6 → 6.5 → 5.7 → 6.9 → 6.9 s | 3 KB | 59 |

- 缓冲区调小、不丢帧：延迟照样增长，但积压现在出现在服务器的队列里了（1782 KB），服务器「看得见」了。
- 缓冲区调小、丢帧：延迟稳定在 3 秒左右，不再增长，代价是丢了 121 帧，大约每 5 帧丢 1 帧，观众看到的是卡顿的画面，但始终是「直播」。
- 默认缓冲区、丢帧：服务器队列几乎总是空的，丢帧只偶尔触发，延迟依然增长到 6.9 秒。

剩下的约 3 秒延迟，是丢帧上限（300 KB，约 1 秒）、两端各 32 KB 的缓冲区，加上开头 GOP 缓存一次性涌入造成的积压。上限设得越小，延迟越低，但网速稍有波动就会丢帧。

这个实验说明：**直播服务器想控制延迟，必须知道数据真正发出去了多少**，只看自己的队列是不够的。实际的服务器会调小发送缓冲区，或者用 `TCP_NOTSENT_LOWAT` 这类选项（macOS 和 Linux 都支持）限制内核中尚未发送的数据量，或者根据观众的 ACK 进度估计带宽。WebRTC、SRT 这些基于 UDP 的协议则干脆自己管理所有缓冲，不交给内核（第 5 章《低延迟方案对比》）。

## 5. 从屏幕到播放器

最后把推流端换成真正的采集：`screen_push` 采集屏幕，裁出一块缩放成 1280 宽，零延迟编码，封装成 FLV 推给服务器。

```c
// screen_push.c：采集屏幕，裁出一块缩放成 1280 宽，用 x264 零延迟编码，封装成 FLV 推出去
// 用法：screen_push <输出URL> [y=裁剪起点（像素）] [h=裁剪高度] [fps=30] [seconds=60]
//   输出URL：tcp://127.0.0.1:9000（本篇的服务器）、rtmp://...（MediaMTX 等）
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "用法: %s 输出URL [y=0] [h=0] [fps=30] [seconds=60]\n", argv[0]); return 1; }
    int y0 = 0, ch = 0, seconds = 60;
    const char *fps = "30";
    for (int i = 2; i < argc; i++) {
        if (!strncmp(argv[i], "y=", 2)) y0 = atoi(argv[i] + 2) & ~1;   // NV12 的色度是隔行的，起点取偶数
        else if (!strncmp(argv[i], "h=", 2)) ch = atoi(argv[i] + 2) & ~1;
        else if (!strncmp(argv[i], "fps=", 4)) fps = argv[i] + 4;
        else if (!strncmp(argv[i], "seconds=", 8)) seconds = atoi(argv[i] + 8);
    }

    // 1. 采集：屏幕，NV12
    avdevice_register_all();
    AVDictionary *dev = NULL;
    av_dict_set(&dev, "pixel_format", "nv12", 0);
    av_dict_set(&dev, "framerate", fps, 0);
    AVFormatContext *in = NULL;
    if (avformat_open_input(&in, "Capture screen 0", av_find_input_format("avfoundation"), &dev) < 0) return 1;
    AVCodecParameters *par = in->streams[0]->codecpar;
    AVRational dev_tb = in->streams[0]->time_base;
    if (!ch || y0 + ch > par->height) ch = par->height - y0;
    int ow = 1280, oh = (int)((double)ch * ow / par->width) & ~1;

    // 2. 编码：零延迟、没有 B 帧、GOP 1 秒
    const AVCodec *enc = avcodec_find_encoder_by_name("libx264");
    AVCodecContext *ectx = avcodec_alloc_context3(enc);
    ectx->width = ow;
    ectx->height = oh;
    ectx->pix_fmt = AV_PIX_FMT_YUV420P;
    ectx->time_base = dev_tb;                         // 用设备的时间戳（微秒）
    ectx->framerate = (AVRational){atoi(fps), 1};
    ectx->gop_size = atoi(fps);
    ectx->max_b_frames = 0;
    ectx->bit_rate = 3000000;
    av_opt_set(ectx->priv_data, "preset", "veryfast", 0);
    av_opt_set(ectx->priv_data, "tune", "zerolatency", 0);

    // 3. 封装成 FLV，写到网络
    AVFormatContext *out = NULL;
    avformat_alloc_output_context2(&out, NULL, "flv", argv[1]);
    ectx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;        // FLV 要把 SPS、PPS 放进序列头
    if (avcodec_open2(ectx, enc, NULL) < 0) return 1;
    AVStream *ost = avformat_new_stream(out, NULL);
    avcodec_parameters_from_context(ost->codecpar, ectx);
    ost->time_base = (AVRational){1, 1000};
    if (avio_open(&out->pb, argv[1], AVIO_FLAG_WRITE) < 0) { fprintf(stderr, "连接 %s 失败\n", argv[1]); return 1; }
    if (avformat_write_header(out, NULL) < 0) return 1;
    fprintf(stderr, "采集 %dx%d %s，取 y=%d 起的 %d 行，缩放成 %dx%d，推到 %s\n", par->width, par->height,
            av_get_pix_fmt_name(par->format), y0, ch, ow, oh, argv[1]);
    if (par->format != AV_PIX_FMT_NV12) { fprintf(stderr, "设备没有给出 NV12\n"); return 1; }

    // 裁剪不用拷贝：把 Y、UV 两个平面的起始指针往下挪 y0 行就行
    struct SwsContext *sws = sws_getContext(par->width, ch, AV_PIX_FMT_NV12, ow, oh, AV_PIX_FMT_YUV420P,
                                            SWS_BILINEAR, NULL, NULL, NULL);
    AVFrame *yuv = av_frame_alloc();
    yuv->format = AV_PIX_FMT_YUV420P;
    yuv->width = ow;
    yuv->height = oh;
    av_frame_get_buffer(yuv, 0);
    AVPacket *pkt = av_packet_alloc(), *opkt = av_packet_alloc();
    int64_t first = AV_NOPTS_VALUE, end = av_gettime_relative() + (int64_t)seconds * 1000000;
    long frames = 0;
    while (av_gettime_relative() < end) {
        int ret = av_read_frame(in, pkt);
        if (ret == AVERROR(EAGAIN)) { av_usleep(1000); continue; }
        if (ret < 0) break;
        if (first == AV_NOPTS_VALUE) first = pkt->pts;
        int w = par->width, h = par->height;
        const uint8_t *src[2] = {pkt->data + (size_t)y0 * w, pkt->data + (size_t)w * h + (size_t)(y0 / 2) * w};
        const int stride[2] = {w, w};                 // 设备给出的 NV12 是紧密排列的：Y 平面之后紧跟 UV 平面
        av_frame_make_writable(yuv);
        sws_scale(sws, src, stride, 0, ch, yuv->data, yuv->linesize);
        yuv->pts = pkt->pts - first;
        av_packet_unref(pkt);
        avcodec_send_frame(ectx, yuv);
        while (avcodec_receive_packet(ectx, opkt) == 0) {
            av_packet_rescale_ts(opkt, ectx->time_base, ost->time_base);
            if (av_interleaved_write_frame(out, opkt) < 0) { fprintf(stderr, "发送失败\n"); end = 0; }
        }
        frames++;
    }
    av_write_trailer(out);
    avio_closep(&out->pb);
    fprintf(stderr, "推了 %ld 帧\n", frames);
    return 0;
}
```

裁剪不需要复制数据：设备给出的 NV12 是紧密排列的，把 Y 平面和 UV 平面的起始指针各往下挪 `y0` 行（UV 平面挪 `y0 / 2` 行，所以 `y0` 必须是偶数），交给 swscale 的就是裁剪后的画面了。

为了测量从屏幕到播放器的延迟，再用第 5 章《采集》的 `clock_window` 在屏幕上画时间条码，这次把方块放大到 48 点（Retina 屏上是 96 像素），裁剪、缩小到 1280 宽以后正好是 32 像素，和 `qplayer` 读条码的格式一致：

```bash
./live_server gop_cache=1
./clock_window 80 48 &
./screen_push tcp://127.0.0.1:9000 y=200 h=1080 fps=60 seconds=70 &
./build/qplayer --mute --barcode --quit 15 http://127.0.0.1:8080/live.flv
```

`qplayer` 三次的结果（条码延迟是从画上屏幕的时刻，到播放器把这一帧交给显示控件的时刻）：

```text
显示 896 帧，丢弃 63 帧，重绘 896 次、每次 1.17 ms，条码延迟 最小 90 / 中位 95 / 最大 1194 ms
显示 875 帧，丢弃 22 帧，重绘 875 次、每次 1.25 ms，条码延迟 最小 117 / 中位 120 / 最大 509 ms
显示 876 帧，丢弃 22 帧，重绘 875 次、每次 0.78 ms，条码延迟 最小 111 / 中位 115 / 最大 501 ms
```

再用 `latency_probe` 只测到解码出来为止：

```text
打开到第一帧 13 ms
帧数 605  坏帧 0  卡顿 0 次  到达延迟 最小 82 / 中位 87 / 最大 142 / 最后 86 ms
```

从屏幕上画出一个画面，经过采集、缩放、编码、FLV 封装、TCP、服务器转发、HTTP、解码，到播放器显示，中位数约 0.1 秒。最大值的 0.5～1.2 秒是刚连上时 GOP 缓存带来的，追帧丢掉 22～63 帧以后就降下来了。

这 0.1 秒里绝大部分是**采集**：第 5 章《采集》单独测过，从画上屏幕到采集程序拿到这一帧就要 96～103 毫秒（这次测得的全程比它还略小一点，屏幕采集本身的延迟每次运行会有几十毫秒的波动）。上一节推文件时，从编码到解码出来的全部传输只要 8 毫秒。也就是说，在本机上，编码、传输、服务器、解码加起来，还不到采集的十分之一。真实的网络上要再加上传输时间和排队，但「采集和显示这两头的延迟不可忽视」这一点不会变：优化直播延迟，不能只盯着协议。

还要说明，播放器的测量点是把帧交给 `VideoWidget` 的时刻，之后还要等下一次重绘、窗口服务器合成、显示器刷新，真正被眼睛看到还要再晚十几到几十毫秒。要测这一段，就得用高速摄像机同时拍下屏幕上的时钟和播放器窗口，这就是业界常说的「glass-to-glass」（从镜头玻璃到屏幕玻璃）的测法。

## 6. 回顾

六章下来，这个小系统用到了专栏的每一部分：

| 环节 | 用到的知识 | 章节 |
|---|---|---|
| 采集屏幕 | libavdevice、NV12、设备时间戳 | 第 5 章《采集》 |
| 缩放、转换 | swscale、BT.709 | 第 1 章《YUV》、第 3 章《像素格式转换与重采样》 |
| 编码 | x264、zerolatency、GOP | 第 1 章《为什么必须压缩》、第 3 章《编码》 |
| 封装 | FLV 的文件头和 Tag、AVC 序列头 | 第 2 章《FLV 封装格式》 |
| 传输 | TCP、HTTP、GOP 缓存、丢帧、内核缓冲区 | 第 5 章、本篇 |
| 解码、同步、显示 | 解码线程、帧队列、声音时钟、OpenGL | 第 3、4 章，本章前两篇 |

它当然只是一个玩具：没有声音的采集、没有鉴权、只有一路流、观众数受 `MAX_VIEWERS` 限制、没有录制和转码。但真正的直播系统，每一个环节做的正是这些事情，只是做得更健壮、规模更大。读懂了这几百行代码，再去看 SRS、MediaMTX 这样的开源服务器，或者 ffplay、ijkplayer 这样的播放器，就知道它们在做什么了。

## 练习

1. 推流端断开后重新连上，服务器会把新的 FLV 文件头、元数据和序列头存下来。已经连着的观众会怎样？应该怎么处理？
2. `send_tag` 中，观众太慢时丢帧要一直丢到下一个**关键帧**才恢复，为什么不能只丢掉积压的那几帧，从下一帧马上恢复？音频 Tag 应该怎么处理？
3. 第 3.3 节中，1000 个观众每人 2 Mbps，服务器每秒要发出 2 Gbps。如果用第 5 章的 HLS 代替 HTTP-FLV，服务器的负担会有什么变化？

<details>
<summary>点击查看答案</summary>

1. 已经连着的观众会继续收到新推流的 Tag，但它们的解码器还用着旧的序列头。如果新的推流参数（分辨率、SPS、PPS）和原来一样，解码器能继续工作，只是时间戳会突然跳回 0（新推流从 0 开始），播放器可能把这些帧当成「过期」的帧丢掉，或者卡住等时间追上来；如果参数变了，解码会出错，画面花掉。正确的做法是：推流端重连后，给已连接的观众先发一次新的元数据和序列头（FLV 允许中途出现新的序列头，解码器会重新初始化），再从关键帧开始发；或者干脆断开所有观众，让播放器重新连接。播放器那一侧也要能处理时间戳跳变，比如发现时间戳不连续时重新对齐时钟。

2. 丢掉的帧可能是后面帧的参考帧。直播流没有 B 帧，每个 P 帧都参考前一帧（第 1 章、第 2 章），中间丢了任何一帧，后面直到下一个关键帧的所有帧都无法正确解码，会花屏（第 5 章 RTP 丢包实验中错误一直延续到下一个关键帧）。所以要么不丢，要么丢到下一个关键帧为止。音频帧之间没有依赖，每个 AAC 帧都能独立解码，可以只丢掉积压的部分；但为了让声音和画面一起恢复，通常和视频一起丢弃、一起恢复，否则画面停着、声音还在继续，同步也会乱。本篇的服务器在丢帧期间音频和视频一起丢。

3. HLS 的切片是普通的文件，服务器只需要把每个切片生成一次，1000 个观众下载的是同一批文件，可以交给 CDN 缓存和分发：源站只要把每个切片发给 CDN 一次，CDN 的各个节点再发给附近的观众，源站的带宽和观众数几乎无关。HTTP-FLV 是长连接，每个观众一个永不结束的响应，源站要为每个人单独维护连接、队列和发送进度，CDN 也要专门支持这种长连接的转发（国内的 CDN 大多支持 HTTP-FLV，但成本更高）。代价是第 5 章测到的：HLS 的延迟是几秒到几十秒，HTTP-FLV 是几十毫秒。

</details>
