---
title: MPEG-TS 封装格式
date: 2026-10-31 13:00:00
updated: 2026-10-31 13:00:00
categories:
  - 音视频开发
  - 码流与封装格式
tags:
  - 音视频
  - MPEG-TS
  - HLS
  - C
order: 5
description: MPEG-TS 把一切都切成 188 字节的小包，每个包以 0x47 开头，用 13 位的 PID 区分是哪一路数据。用 C 解析测试视频转成的 TS：先从 PID 0 的 PAT 找到 PMT，再从 PMT 找到视频和音频的 PID；视频的每一帧装进一个 PES，PES 头中带着 90 kHz 的 PTS 和 DTS；PCR 每 80 毫秒出现一次，告诉接收方现在几点。16642 个包中，额外开销 141959 字节，逐项拆开是包头、调整字段、节目表、PES 头和 ADTS 头。再模拟网络丢包：丢掉一个 188 字节的包，连续计数器立刻发现；丢在不被参考的 B 帧上只坏 1 帧，丢在 I 帧上，46 帧变成竖条纹。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 MPEG-TS 的分层：ES、PES、TS 包；读懂 TS 包头的每个字段，会用 C 解析 PAT、PMT、PES 头和 PCR；理解 90 kHz 时钟、PTS/DTS 与 PCR 的关系；知道 TS 的开销来自哪里；理解连续计数器和丢包对画面的影响。
>
> **前置知识**：本章前四篇，尤其是《H.264 码流》中的 IDR、参考帧和错误传播。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0。

## 1. 为传输而生的格式

前面的 FLV 和 MP4 都是为「文件」设计的：从头开始顺序读，或者先读索引再跳着读。**MPEG-TS**（Transport Stream，传输流）则是为**不可靠的传输**设计的：数字电视的卫星和地面广播、IPTV，以及现在最常用的直播协议 HLS 的切片，都是 TS。在这些场合中：

- 接收方随时可能开机、换台，从数据流的**中间**开始接收；
- 传输中会丢失或损坏数据，不能因为一点错误就整个崩掉；
- 一路信号中常常同时有好几个电视节目。

TS 的解决办法是把一切都切成固定大小的**小包**：每个包 **188 字节**，开头是同步字节 `0x47`。接收方只要找到 `0x47`，并确认 188 字节之后还是 `0x47`，就同步上了；某个包坏了，丢掉它，下一个包照常处理。188 这个数字来自早期的 ATM 网络：一个 ATM 信元能装 47 字节的数据，188 = 47 × 4。

准备测试文件（第 1 章第 4 篇已经做过）：

```bash
ffmpeg -i bbb-10s.mp4 -c copy bbb.ts
```

## 2. 三层结构

TS 中的音视频数据经过了三层包装：

<figure class="diagram">
<svg viewBox="0 0 680 250" role="img" aria-label="MPEG-TS 的三层结构：ES 是 H.264 或 AAC 的码流；每一帧加上 PES 头成为一个 PES 包，PES 头中有 PTS 和 DTS；PES 包被切成若干段，每段加上 4 字节的 TS 包头成为 188 字节的 TS 包，最后一个包不够时用调整字段填充">
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="22" style="fill:var(--fg)">ES</text>
    <g transform="translate(70,8)">
      <rect x="0" y="0" width="560" height="26" style="fill:var(--danger);opacity:.55"/><text x="10" y="18" style="fill:var(--bg)">一帧 H.264（Annex B：AUD + SEI + SPS + PPS + IDR 片 …）</text>
    </g>
    <text x="10" y="72" style="fill:var(--fg)">PES</text>
    <g transform="translate(70,58)">
      <rect x="0" y="0" width="90" height="26" style="fill:var(--warn);opacity:.8"/><text x="6" y="18" style="fill:var(--bg);font-size:11px">PES 头 19</text>
      <rect x="90" y="0" width="500" height="26" style="fill:var(--danger);opacity:.55"/><text x="100" y="18" style="fill:var(--bg)">同一帧的数据</text>
      <text x="0" y="44" style="fill:var(--muted);font-size:11px">00 00 01 + stream_id（视频 E0，音频 C0）+ 长度 + 标志 + PTS（5 字节）+ DTS（5 字节）</text>
    </g>
    <text x="10" y="152" style="fill:var(--fg)">TS</text>
    <g transform="translate(70,130)">
      <rect x="0" y="0" width="28" height="36" style="fill:var(--link);opacity:.7"/><text x="3" y="22" style="fill:var(--bg);font-size:10px">头4</text>
      <rect x="28" y="0" width="22" height="36" style="fill:var(--accent);opacity:.6"/><text x="30" y="22" style="fill:var(--bg);font-size:9px">PCR</text>
      <rect x="50" y="0" width="100" height="36" style="fill:var(--warn);opacity:.4"/><text x="56" y="22" style="fill:var(--fg);font-size:11px">PES 头+数据</text>
      <rect x="154" y="0" width="28" height="36" style="fill:var(--link);opacity:.7"/><text x="157" y="22" style="fill:var(--bg);font-size:10px">头4</text>
      <rect x="182" y="0" width="122" height="36" style="fill:var(--danger);opacity:.4"/><text x="200" y="22" style="fill:var(--fg);font-size:11px">数据 184</text>
      <text x="312" y="22" style="fill:var(--muted)">…</text>
      <rect x="330" y="0" width="28" height="36" style="fill:var(--link);opacity:.7"/><text x="333" y="22" style="fill:var(--bg);font-size:10px">头4</text>
      <rect x="358" y="0" width="70" height="36" style="fill:var(--panel-2);stroke:var(--border-2);stroke-dasharray:3 2"/><text x="364" y="22" style="fill:var(--muted);font-size:10px">填充 FF…</text>
      <rect x="428" y="0" width="52" height="36" style="fill:var(--danger);opacity:.4"/><text x="432" y="22" style="fill:var(--fg);font-size:10px">剩余</text>
      <path d="M0,40 L0,48 L150,48 L150,40" fill="none" style="stroke:var(--fg)"/><text x="30" y="64" style="fill:var(--fg);font-size:11px">188 字节</text>
      <text x="330" y="64" style="fill:var(--muted);font-size:11px">最后一个包用调整字段填满</text>
    </g>
    <text x="10" y="232" style="fill:var(--muted);font-size:11px">视频、音频、节目表各自用不同的 PID，它们的 TS 包交错排列在同一个流中</text>
  </g>
</svg>
</figure>

1. **ES**（Elementary Stream，基本流）：编码器输出的码流，即 H.264 的 Annex B 码流、带 ADTS 头的 AAC。
2. **PES**（Packetized Elementary Stream，打包的基本流）：每一帧（音频可以是几帧）加上一个 PES 头，PES 头中最重要的是这一帧的 **PTS** 和 **DTS**。
3. **TS 包**：把 PES 切成 184 字节一段，每段加上 4 字节的 TS 包头，正好 188 字节。最后一段不够 184 字节时，用**调整字段**（adaptation field）中的填充字节补齐。

所以第 1 章第 4 篇中，TS 里的视频流要转成 Annex B 并加上 AUD，音频要加上 ADTS 头：TS 中没有地方存 SPS、PPS 或 AudioSpecificConfig，每一路流必须是「自描述」的，从任何一个 IDR 开始都能解码。

## 3. TS 包头

<figure class="diagram">
<svg viewBox="0 0 680 180" role="img" aria-label="TS 包头 4 字节的字段：8 位同步字节 0x47，1 位传输错误指示，1 位负载单元开始指示，1 位优先级，13 位 PID，2 位加扰控制，2 位调整字段控制，4 位连续计数器">
  <g style="font-family:var(--mono);font-size:12px">
    <g transform="translate(10,20)">
      <rect x="0" y="0" width="160" height="34" style="fill:var(--panel-2);stroke:var(--border-2)"/><text x="40" y="21" style="fill:var(--fg)">sync 0x47</text>
      <rect x="160" y="0" width="20" height="34" style="fill:var(--danger);opacity:.5"/>
      <rect x="180" y="0" width="20" height="34" style="fill:var(--warn);opacity:.8"/>
      <rect x="200" y="0" width="20" height="34" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <rect x="220" y="0" width="260" height="34" style="fill:var(--link);opacity:.6"/><text x="320" y="21" style="fill:var(--bg)">PID（13 位）</text>
      <rect x="480" y="0" width="40" height="34" style="fill:var(--panel-2);stroke:var(--border-2)"/>
      <rect x="520" y="0" width="40" height="34" style="fill:var(--accent);opacity:.6"/>
      <rect x="560" y="0" width="80" height="34" style="fill:var(--warn);opacity:.5"/><text x="588" y="21" style="fill:var(--fg)">cc</text>
      <g style="fill:var(--muted);font-size:10px">
        <text x="0" y="50">8 位</text>
        <text transform="translate(166,40) rotate(60)">传输错误</text>
        <text transform="translate(186,40) rotate(60)" style="fill:var(--fg)">负载开始（PUSI）</text>
        <text transform="translate(206,40) rotate(60)">优先级</text>
        <text x="300" y="50">区分是哪一路数据</text>
        <text transform="translate(486,40) rotate(60)">加扰</text>
        <text transform="translate(526,40) rotate(60)" style="fill:var(--fg)">调整字段控制</text>
        <text x="560" y="50">连续计数器 4 位</text>
      </g>
    </g>
  </g>
</svg>
</figure>

- **PUSI**（payload_unit_start_indicator）：为 1 表示这个包的负载是一个 PES 或一张表的**开头**。接收方从这里开始拼接，直到下一个 PUSI 为 1 的包。
- **PID**（13 位）：这个包属于哪一路数据。0 固定是 PAT，其余的由节目表分配。
- **调整字段控制**：1 只有负载，2 只有调整字段，3 两者都有。调整字段可以携带 PCR、随机访问标志，或者只是填充。
- **连续计数器**（cc，4 位）：同一个 PID 的包，每个包加 1，0～15 循环。接收方用它发现丢包，第 6 节会用到。

## 4. 解析 TS

### 4.1 PAT 和 PMT

接收方拿到一个 TS 流，怎么知道哪个 PID 是视频、哪个是音频？靠两张表（统称 PSI，节目特定信息）：

- **PAT**（Program Association Table，节目关联表）：PID 固定为 0，列出流中有哪些节目，以及每个节目的 PMT 在哪个 PID。
- **PMT**（Program Map Table，节目映射表）：列出这个节目由哪几路流组成，每一路的**流类型**和 PID。流类型 `0x1B` 是 H.264，`0x0F` 是 ADTS 格式的 AAC，`0x24` 是 H.265。

这两张表会周期性地重复（FFmpeg 默认每 0.1 秒左右一次，测试视频中各出现了 82 次），这样从任何地方开始接收，最多等一会儿就能拿到。

### 4.2 程序

下面的程序逐个读取 188 字节的包，解析包头和调整字段（PCR），遇到 PAT、PMT 就解析出 PID 的对应关系，遇到 PES 的开头就读出 PTS 和 DTS，同时检查连续计数器：

```c
// ts_parse.c：解析 MPEG-TS：包头、PAT、PMT、PES 头中的 PTS/DTS、PCR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define TS 188

static int pmt_pid = -1;
static int stream_type[8192];            // 每个 PID 对应的流类型，从 PMT 中得到
static int last_cc[8192];

// 读 PES 头中 5 字节的时间戳：33 位分散在 5 个字节中，中间夹着标记位
static uint64_t read_ts(const unsigned char *p)
{
    return (uint64_t)(p[0] >> 1 & 7) << 30 | (uint64_t)p[1] << 22 | (uint64_t)(p[2] >> 1) << 15 |
           (uint64_t)p[3] << 7 | p[4] >> 1;
}

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "用法: %s 输入.ts [显示前几个包]\n", argv[0]); return 1; }
    int show = argc > 2 ? atoi(argv[2]) : 12;
    FILE *f = fopen(argv[1], "rb");
    unsigned char p[TS];
    long n = 0, count[8192] = {0}, payload_bytes = 0, stuffing = 0, cc_errors = 0, pes_count = 0;
    memset(last_cc, -1, sizeof last_cc);

    while (fread(p, 1, TS, f) == TS) {
        if (p[0] != 0x47) { printf("第 %ld 个包：同步字节不是 0x47\n", n); break; }
        int pusi = p[1] >> 6 & 1;                          // 一个 PES 或表从这个包开始
        int pid = (p[1] & 0x1F) << 8 | p[2];
        int afc = p[3] >> 4 & 3;                           // 1 只有负载，2 只有调整字段，3 两者都有
        int cc = p[3] & 0xF;                               // 连续计数器，每个 PID 各自 0～15 循环
        count[pid]++;

        // 连续计数器：有负载的包，cc 应该比上一个大 1（PID 0x1FFF 是空包，不检查）
        if ((afc & 1) && pid != 0x1FFF && last_cc[pid] >= 0 && cc != ((last_cc[pid] + 1) & 0xF)) {
            printf("第 %ld 个包（PID 0x%04X）：连续计数器跳变 %d → %d，丢包了\n", n, pid, last_cc[pid], cc);
            cc_errors++;
        }
        if (afc & 1) last_cc[pid] = cc;

        int pos = 4;
        char info[160] = "";
        if (afc & 2) {                                     // 调整字段
            int len = p[4];
            if (len > 0 && (p[5] & 0x10)) {                // 有 PCR
                uint64_t base = (uint64_t)p[6] << 25 | p[7] << 17 | p[8] << 9 | p[9] << 1 | p[10] >> 7;
                int ext = (p[10] & 1) << 8 | p[11];
                snprintf(info, sizeof info, "PCR=%.6f 秒 ", (base * 300 + ext) / 27000000.0);
            }
            if (len > 0 && (p[5] & 0x40)) strcat(info, "随机访问点 ");
            // 调整字段中 PCR 等信息之后的部分都是填充的 0xFF
            for (int i = 6; i < 5 + len; i++) if (p[i] == 0xFF) stuffing++;
            pos += 1 + len;
        }
        int payload = (afc & 1) ? TS - pos : 0;
        payload_bytes += payload;
        const unsigned char *d = p + pos;
        char *e = info + strlen(info);
        size_t room = sizeof info - strlen(info);

        if (payload && pusi && (pid == 0 || pid == pmt_pid)) {
            d += 1 + d[0];                                 // 表的开头有 1 字节的 pointer_field
            int section_len = (d[1] & 0xF) << 8 | d[2];
            if (pid == 0) {                                // PAT：节目号 → PMT 的 PID
                pmt_pid = (d[10] & 0x1F) << 8 | d[11];
                snprintf(e, room, "PAT：节目 %d 的 PMT 在 PID 0x%04X", d[8] << 8 | d[9], pmt_pid);
            } else {                                       // PMT：每路流的类型和 PID
                int info_len = (d[10] & 0xF) << 8 | d[11];
                const unsigned char *s = d + 12 + info_len, *end = d + 3 + section_len - 4;   // 最后 4 字节是 CRC
                int k = snprintf(e, room, "PMT：PCR 在 PID 0x%04X", (d[8] & 0x1F) << 8 | d[9]);
                while (s < end) {
                    int type = s[0], epid = (s[1] & 0x1F) << 8 | s[2], es_len = (s[3] & 0xF) << 8 | s[4];
                    stream_type[epid] = type;
                    k += snprintf(e + k, room - k, "；类型 0x%02X（%s）在 PID 0x%04X", type,
                                  type == 0x1B ? "H.264" : type == 0x0F ? "AAC" : type == 0x24 ? "H.265" : "?", epid);
                    s += 5 + es_len;
                }
            }
        } else if (payload && pusi && d[0] == 0 && d[1] == 0 && d[2] == 1) {   // PES 头
            int sid = d[3], flags = d[7] >> 6, pes_len = d[4] << 8 | d[5];
            pes_count++;
            int k = snprintf(e, room, "PES 开始：stream_id=0x%02X 长度=%d", sid, pes_len);
            if (flags & 2) k += snprintf(e + k, room - k, " PTS=%llu", (unsigned long long)read_ts(d + 9));
            if (flags == 3) snprintf(e + k, room - k, " DTS=%llu", (unsigned long long)read_ts(d + 14));
        }
        if (n < show)
            printf("%5ld  PID 0x%04X  %s cc=%2d  负载 %3d  %s\n", n, pid, pusi ? "开始" : "    ", cc, payload, info);
        n++;
    }
    fclose(f);
    printf("...\n共 %ld 个包，%ld 字节\n", n, n * TS);
    for (int pid = 0; pid < 8192; pid++)
        if (count[pid])
            printf("PID 0x%04X：%5ld 个包%s\n", pid, count[pid],
                   pid == 0 ? "（PAT）" : pid == pmt_pid ? "（PMT）" : stream_type[pid] == 0x1B ? "（视频）" :
                   stream_type[pid] == 0x0F ? "（音频）" : pid == 0x1FFF ? "（空包）" : "");
    printf("PES %ld 个；负载 %ld 字节，填充 %ld 字节，包头和调整字段 %ld 字节\n",
           pes_count, payload_bytes, stuffing, n * TS - payload_bytes);
    printf("连续计数器错误 %ld 次\n", cc_errors);
    return 0;
}
```

PES 头中的 PTS、DTS 各占 5 个字节，33 位的时间戳被拆成 3 + 15 + 15 位，中间夹着 3 个固定为 1 的标记位（防止出现连续的 0，避免被误认为起始码），所以 `read_ts` 看起来有点乱。

```bash
clang -O2 -o ts_parse ts_parse.c
./ts_parse bbb.ts 14
```

```
    0  PID 0x0011  开始 cc= 0  负载 184  
    1  PID 0x0000  开始 cc= 0  负载 184  PAT：节目 1 的 PMT 在 PID 0x1000
    2  PID 0x1000  开始 cc= 0  负载 184  PMT：PCR 在 PID 0x0100；类型 0x1B（H.264）在 PID 0x0100；类型 0x0F（AAC）在 PID 0x0101
    3  PID 0x0100  开始 cc= 0  负载 176  PCR=0.700000 秒 随机访问点 PES 开始：stream_id=0xE0 长度=0 PTS=133500 DTS=126000
    4  PID 0x0100       cc= 1  负载 184  
    5  PID 0x0100       cc= 2  负载 184  
    6  PID 0x0100       cc= 3  负载 184  
    7  PID 0x0100       cc= 4  负载 184  
    8  PID 0x0100       cc= 5  负载 184  
    9  PID 0x0100       cc= 6  负载 184  
   10  PID 0x0100       cc= 7  负载 184  
   11  PID 0x0100       cc= 8  负载 184  
   12  PID 0x0100       cc= 9  负载 184  
   13  PID 0x0100       cc=10  负载 184  
...
共 16642 个包，3128696 字节
PID 0x0000：   82 个包（PAT）
PID 0x0011：   20 个包
PID 0x0100：15526 个包（视频）
PID 0x0101：  932 个包（音频）
PID 0x1000：   82 个包（PMT）
PES 299 个；负载 3031003 字节，填充 29405 字节，包头和调整字段 97693 字节
连续计数器错误 0 次
```

- **第 0 个包是 PID 0x0011**，这是 SDT（服务描述表），记录节目的名字和提供者，是 DVB 数字电视标准中的表，FFmpeg 默认会写入。
- **第 1 个包是 PAT**：节目 1 的 PMT 在 PID 0x1000。
- **第 2 个包是 PMT**：视频（H.264）在 PID 0x0100，音频（AAC）在 PID 0x0101，PCR 也在视频的 PID 上。
- **第 3 个包开始是第一帧视频**：PUSI 为 1，调整字段中有 PCR 和随机访问标志，负载是一个 PES，后面一连串 cc 递增的包都是这一帧的数据。第一帧有 96 KB，要切成 500 多个 TS 包。

### 4.3 90 kHz 的时间戳

PES 头中的 PTS、DTS 以 **90 kHz** 为单位（时间基 1/90000），这是 MPEG-2 标准规定的，TS 中所有流都一样。把前几个 PES 的时间戳列出来：

```
包号   PID     PES 头
   3  0x0100  PTS=133500 DTS=126000
 528  0x0100  PTS=144750 DTS=129750
 541  0x0100  PTS=137250 DTS=133500
 547  0x0100  PTS=141000 DTS=137250
 550  0x0100  PTS=156000 DTS=141000
 ...
 572  0x0101  PTS=131580                 （音频，长度 2745）
```

- 视频每帧 90000 / 24 = **3750**，正好是整数。DTS 依次是 126000、129750、133500……，每次加 3750；PTS 的顺序则是 I、P、B、B，和前几篇看到的完全一样。
- 第一帧的 DTS 是 126000 = 1.4 秒，不是 0。FFmpeg 写 TS 时默认从 1.4 秒开始，原因见下一节的 PCR。
- 视频 PES 的长度是 0，表示「不限长度，直到下一个 PUSI」，因为一帧视频可能超过 PES 长度字段 16 位能表示的 65535 字节。
- 音频 PES 的长度是 2745 字节，装了 8 个 AAC 帧：音频帧很小，每帧一个 PES 太浪费，FFmpeg 把几帧合在一个 PES 中（一共 59 个音频 PES，装下 470 帧）。

### 4.4 PCR：现在几点

TS 是为广播设计的，发送方按固定的速率不停地发送，接收方没有办法要求「慢一点」或「重发」。接收方必须让自己的时钟和发送方**同步**，否则时间一长，缓冲区要么溢出，要么被取空。为此，TS 中每隔一段时间（标准要求不超过 100 毫秒）在调整字段中插入一个 **PCR**（Program Clock Reference，节目时钟参考），它是发送方 27 MHz 系统时钟的采样值，意思是「这个包到达的时刻，发送方的时钟是多少」。

测试视频中有 126 个 PCR，第一个是 0.700000 秒，之后每两帧一个（0.783333、0.866667……），最后一个是 10.616667 秒，平均间隔约 80 毫秒。

PCR 和 PTS、DTS 是同一个时钟：第一个 PCR 是 0.7 秒，第一帧的 DTS 是 1.4 秒，说明这一帧的数据到达后，要等 0.7 秒才解码。这 0.7 秒是给解码器留的**缓冲时间**（FFmpeg 的 `muxdelay` 参数，默认 0.7 秒），让数据有时间到齐。直播中这个值直接影响延迟。

### 4.5 开销

程序最后的统计：

```
共 16642 个包，3128696 字节
PID 0x0000：   82 个包（PAT）
PID 0x0011：   20 个包
PID 0x0100：15526 个包（视频）
PID 0x0101：  932 个包（音频）
PID 0x1000：   82 个包（PMT）
```

第 1 章第 4 篇中 TS 比 MP4 大了 134 KB（4.5%），现在可以精确地拆开这些开销了。用一个小脚本把每个包的各部分分别加起来：

| 部分 | 字节 | 说明 |
|---|---|---|
| 音视频数据 | 2986737 | 和 MP4 的 `mdat` 中完全相同的帧数据 |
| TS 包头 | 65832 | 音视频的 16458 个包，每个 4 字节 |
| 调整字段 | 31125 | 其中 29405 字节是填充，其余是 PCR 等 |
| PAT、PMT、SDT | 34592 | 184 个包，每张表都很小，但各占一整个包 |
| PES 头 | 5386 | 视频 240 × 19，音频 59 × 14 |
| ADTS 头 | 3290 | 470 × 7 |
| 视频码流的变化 | 1734 | 240 个 AUD（每个 6 字节）+ 7 份 SPS、PPS − 7 个 3 字节起始码省下的字节 |
| **合计** | **3128696** | 开销 141959 字节，占 4.5% |

开销最大的是 TS 包头和填充。码率越低，每帧越小，最后一个包的填充占比就越大；音频帧很小，把多帧合在一个 PES 中正是为了减少填充。节目表的开销则是固定的：每秒十几个包，和码率无关，所以低码率的 TS 流开销比例会明显增大。

## 5. 同步：从中间开始

TS 流可以从任何一个包开始解析。如果是从任意一个字节开始（例如网络传输中接收到的数据是半截的），先查找 `0x47`，再检查 188 字节、376 字节之后是不是也是 `0x47`，连续几次都对，就认为同步上了。这和上一篇 ADTS 用同步字加帧长的做法一样。

同步之后，还要等到下一个 PAT、PMT，才知道各个 PID 是什么；再等到视频的下一个随机访问点（调整字段中的随机访问标志，对应 IDR），才能开始解码画面。数字电视换台时黑屏一两秒，就是在等这些东西。

## 6. 丢包

### 6.1 连续计数器

写一个程序，复制 TS 文件时丢掉第 n 个包：

```c
// ts_drop.c：复制 TS 文件，丢掉第 n 个包（模拟网络丢包）
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc != 4) { fprintf(stderr, "用法: %s 输入.ts 输出.ts n\n", argv[0]); return 1; }
    long drop = atol(argv[3]), n = 0;
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    unsigned char p[188];
    while (fread(p, 1, 188, in) == 188) {
        if (n != drop) fwrite(p, 1, 188, out);
        else printf("丢掉第 %ld 个包：PID 0x%04X，cc=%d\n", n, (p[1] & 0x1F) << 8 | p[2], p[3] & 0xF);
        n++;
    }
    fclose(in); fclose(out);
    return 0;
}
```

丢掉第 9000 个包，再用 `ts_parse` 检查：

```bash
clang -O2 -o ts_drop ts_drop.c
./ts_drop bbb.ts lost.ts 9000
./ts_parse lost.ts 0 | grep -E "跳变|错误"
```

```
丢掉第 9000 个包：PID 0x0100，cc=14
第 9000 个包（PID 0x0100）：连续计数器跳变 13 → 15，丢包了
连续计数器错误 1 次
```

PID 0x0100 的包，计数器从 13 直接跳到 15，中间的 14 不见了。连续计数器只有 4 位，一次丢 16 个包就发现不了，但对于偶尔丢一两个包的情况，它足够了。

### 6.2 对画面的影响

解码这个文件：

```bash
ffmpeg -i lost.ts -map 0:v -f rawvideo -pix_fmt yuv420p lostv.yuv
```

```
[in#0/mpegts] Packet corrupt (stream = 0, dts = 748500).
[h264] concealing 99 DC, 99 AC, 99 MV errors in B frame
```

FFmpeg 的 TS 解封装器同样发现了计数器跳变，把这一帧标记为损坏；H.264 解码器在一个 B 帧中「掩盖」了 99 个宏块。逐帧和完整文件解码的结果比较，只有第 165 帧有差别（PSNR 40.7 dB），因为这是一个不被参考的 B 帧，错误不会传播。

丢包的位置不同，后果天差地别。换几个位置试试：

| 丢掉的包 | 属于 | 受影响的帧数 |
|---|---|---|
| 第 4000 个 | 音频 | 0（音频少了 2 个 AAC 帧，约 43 毫秒） |
| 第 9000 个 | 不被参考的 B 帧 | 1 |
| 第 8500 个 | 第 163 帧（P 帧） | 18（第 161～178 帧） |
| 第 6000 个 | 第 133 帧（I 帧） | **46** |

丢掉第 6000 个包时：

```
[h264] error while decoding MB 75 10, bytestream 138744
[h264] concealing 2774 DC, 2774 AC, 2774 MV errors in I frame
```

这个包恰好属于第 133 帧，也就是第 1 篇中那个不是 IDR 的 I 帧。解码器从第 10 行第 75 个宏块开始无法继续，后面的 2774 个宏块（占全帧的 77%）全靠掩盖。由于后面的帧都直接或间接地参考它，从第 133 帧到第 178 帧，46 帧全部出错，直到第 179 帧的 IDR 才恢复：

<figure>
<img src="/img/av/ts-loss.jpg" alt="第 136 帧的对比：左边正常，右边丢掉一个 TS 包后，画面上部正常，下部大片竖条纹" width="640">
<figcaption>第 136 帧，左：完整文件；右：丢掉第 6000 个 TS 包（188 字节）之后。PSNR 只有 15.9 dB</figcaption>
</figure>

画面上部是正常的，往下突然变成竖条纹：解码器没有这些宏块的数据，就把上方最后一行正确的像素往下复制（同时参考前一帧，但第 133 帧是镜头切换处的 I 帧，前一帧是另一个镜头，没法用）。这就是数字电视信号不好时常见的马赛克和拖影。

这也说明了为什么 TS 常和**前向纠错**（FEC）一起使用：发送方多发一些冗余数据，接收方丢了少量包时可以自己恢复，不需要重传。广播没有回传通道，只能这样做；网络直播则有更多选择，第 5 章会讲到。

## 7. 小结

- MPEG-TS 为不可靠的传输而设计：188 字节的固定包，同步字节 `0x47`，从任何位置都能开始接收。
- 三层结构：ES（码流）→ PES（加上带 PTS/DTS 的头）→ TS 包（4 字节头 + 184 字节负载）。
- PID 区分不同的数据；PAT（PID 0）指向 PMT，PMT 列出每一路流的类型和 PID，两者周期性重复。
- 时间戳以 90 kHz 为单位；PCR 携带发送方的 27 MHz 时钟，让接收方同步时钟；第一个 PCR 和第一个 DTS 之间是解码缓冲时间。
- TS 的开销约 4%～5%，主要是包头、填充和节目表，码率越低比例越高。
- 连续计数器用来发现丢包。丢一个包的影响取决于它属于哪一帧：不被参考的 B 帧只坏一帧，I 帧或 P 帧会一直坏到下一个 IDR。

下一篇把本章出现过的时间戳放在一起：PTS、DTS、各种时间基，以及它们之间的换算。

## 练习

1. 修改 `ts_parse.c`，只统计视频 PID 上每个随机访问点（调整字段中的随机访问标志）对应的 PTS，换算成秒，和第 1 篇的 IDR 位置对比。
2. 用 `ffmpeg -i bbb-10s.mp4 -c copy -mpegts_flags +resend_headers -f mpegts -muxrate 4000000 cbr.ts` 生成一个固定码率（4 Mbps）的 TS，用 `ts_parse` 统计各 PID 的包数，多出来的是什么？文件比 `bbb.ts` 大多少？
3. PTS 只有 33 位，以 90 kHz 计，多长时间会回绕到 0？对 24 小时不间断的电视直播意味着什么？
4. 修改 `ts_drop.c`，不丢包，而是把第 9000 个包的连续计数器改成错误的值（数据不变）。FFmpeg 会怎样处理？画面有没有受到影响？

<details>
<summary>点击查看答案</summary>

1. 在解析调整字段时，如果随机访问标志（`p[5] & 0x40`）为 1，并且同一个包是 PES 的开头，就把 PES 头中的 PTS 记下来：

   ```c
   if (random_access && pusi && pid == video_pid)
       printf("随机访问点：PTS=%llu，%.3f 秒\n", pts, (pts - 133500) / 90000.0);
   ```

   （减去第一帧的 PTS 133500，换算成从 0 开始的时间。）注意要限定视频 PID：FFmpeg 给每个音频 PES 也设置了随机访问标志（每个 AAC 帧都能独立解码）。视频的随机访问点一共 7 个，时间是 0、1.292、3.292、3.458、5.458、7.458、9.458 秒，对应第 0、31、79、83、131、179、227 帧，正好是 7 个 IDR；第 133 帧的 I 帧没有被标记为随机访问点，和 MP4 中的 `stss` 一致。

2. `ts_parse cbr.ts` 的统计：

   ```
   共 26763 个包，5031444 字节
   PID 0x0000：  103 个包（PAT）
   PID 0x0011：   21 个包
   PID 0x0100：15735 个包（视频）
   PID 0x0101：  932 个包（音频）
   PID 0x1000：  103 个包（PMT）
   PID 0x1FFF： 9869 个包（空包）
   ```

   多出来的主要是 PID `0x1FFF` 的**空包**，9869 个，1.86 MB。文件 5031444 字节，正好是 4 Mbps × 10 秒 ≈ 5 MB，比 `bbb.ts` 大了 61%。另外 PAT、PMT 从 82 个增加到 103 个，视频包也多了 209 个（固定码率时 PCR 要按时插入，调整字段更多）。空包的内容没有任何意义，接收方直接丢弃；它的连续计数器也没有意义（一直是 0），所以 `ts_parse` 检查计数器时要跳过 `0x1FFF`，否则会报出几千次「丢包」。卫星和有线电视的信道带宽是固定的，必须用固定码率发送，码流的码率不够时就用空包填满。

3. 2³³ / 90000 ≈ 95444 秒，约 **26.5 小时**。也就是说，一个不间断播出的电视频道，每 26.5 小时时间戳就会从最大值回到 0。接收方必须能够处理这种回绕：比较两个时间戳的先后时，不能直接比大小，而要考虑回绕（例如差值超过一半范围时认为发生了回绕）。FFmpeg 的 TS 解封装器会自动处理，把回绕后的时间戳继续往上加。自己写播放器或者服务器时，这是一个容易忽略的问题，往往在连续运行一天之后才暴露出来。

4. 把计数器改成跳变的值，比如原来是 14，改成 3。FFmpeg 的解封装器会报告 `Packet corrupt`，把这一帧标记为损坏（`AV_PKT_FLAG_CORRUPT`）。但数据本身是完整的，H.264 解码器解码时不会出错，画面和原来完全相同。也就是说，连续计数器只是一个「可能出问题」的提示，真正判断数据是否可用，要看解码器能不能解。反过来，如果丢了正好 16 个包，计数器转了一圈，看起来完全正常，但数据已经缺了一大块，这时只能靠解码器发现。

</details>
