---
title: 转封装与转码
date: 2026-11-01 12:00:00
updated: 2026-11-01 12:00:00
categories:
  - 音视频开发
  - FFmpeg编程
tags:
  - 音视频
  - FFmpeg
  - 转码
  - C
order: 4
description: 用 libavformat 写文件：转封装只要为每路流复制编码参数、把每个包的时间戳从输入的时间基换算到输出的时间基，测试视频转成 FLV、TS、MKV，流的哈希值和命令行 -c copy 完全相同。忘了换算时间戳，FLV 的时长变成 481 秒，MP4 的视频只剩 1.37 秒。再把解码和编码连起来做转码：视频用 x264 重新编码、音频直接复制，最后的结果和命令行 ffmpeg -c:v libx264 -crf 28 -c:a copy 的视频流逐字节相同。中间踩了三个坑：编码器输出的包没有时长，MP4 自己选了 1/90000 的时间基，以及沿用了原视频的帧类型，多出 4 个 I 帧，文件大了 13%。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 libavformat 创建输出文件、写入文件头、包和文件尾；会在输入和输出的时间基之间换算时间戳；会写转封装程序；会把解码、编码、封装连成一个转码程序，正确地冲刷和处理时间戳；知道 `AV_CODEC_FLAG_GLOBAL_HEADER` 的作用。
>
> **前置知识**：本章前三篇；第 2 章的 FLV、MP4、TS 和时间戳。
>
> **实验环境**：macOS 27.0（Apple M6），Apple Clang 21，FFmpeg 9.0.2。

## 1. 写文件的流程

读文件用 `avformat_open_input` + `av_read_frame`；写文件则分成三步：

<figure class="diagram">
<svg viewBox="0 0 680 150" role="img" aria-label="写文件的三个步骤：创建输出上下文和流，复制编码参数；avformat_write_header 写入文件头；循环 av_interleaved_write_frame 写入每个包；最后 av_write_trailer 写入文件尾，MP4 的 moov 就在这一步写出">
  <defs>
    <marker id="mux-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="20" width="160" height="60" rx="6" style="fill:var(--panel-2)"/>
      <rect x="195" y="20" width="140" height="60" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="365" y="20" width="160" height="60" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="555" y="20" width="120" height="60" rx="6" style="fill:var(--accent-soft)"/>
    </g>
    <text x="15" y="42" style="fill:var(--fg);font-size:11px">alloc_output_context2</text>
    <text x="15" y="60" style="fill:var(--fg);font-size:11px">new_stream</text>
    <text x="15" y="74" style="fill:var(--muted);font-size:10px">设置每路流的参数</text>
    <text x="205" y="45" style="fill:var(--fg);font-size:11px">write_header</text>
    <text x="205" y="66" style="fill:var(--muted);font-size:10px">文件头；确定时间基</text>
    <text x="375" y="45" style="fill:var(--fg);font-size:11px">interleaved_write_frame</text>
    <text x="375" y="66" style="fill:var(--muted);font-size:10px">每个包，按时间交错</text>
    <text x="565" y="45" style="fill:var(--fg);font-size:11px">write_trailer</text>
    <text x="565" y="66" style="fill:var(--muted);font-size:10px">文件尾（moov）</text>
    <g style="stroke:var(--muted)" marker-end="url(#mux-arrow)"><line x1="165" y1="50" x2="193" y2="50"/><line x1="335" y1="50" x2="363" y2="50"/><line x1="525" y1="50" x2="553" y2="50"/></g>
    <path d="M445,82 C445,110 420,110 420,96 C420,86 440,86 445,84" fill="none" style="stroke:var(--muted)" marker-end="url(#mux-arrow)"/>
    <text x="10" y="130" style="fill:var(--muted);font-size:11px">每个包写入前：pts、dts、duration 从「来源的时间基」换算到「输出流的时间基」</text>
  </g>
</svg>
</figure>

1. **准备**：`avformat_alloc_output_context2` 根据文件名（或者指定的格式名）选择封装器；`avformat_new_stream` 为每一路流创建输出流，并填好它的编码参数（`codecpar`）。
2. **写文件头**：`avformat_write_header`。FLV 的文件头、onMetaData、TS 的 PAT/PMT 都在这时写出。**写完文件头后，封装器才最终确定每路流的时间基**，例如 FLV 一律改成 1/1000，不管你之前设了什么。
3. **写包**：`av_interleaved_write_frame`。它会把不同流的包按 DTS 排好顺序再写，保证音视频交错存放（第 2 章看到的那种交错）。
4. **写文件尾**：`av_write_trailer`。MP4 的 `moov` 就是在这一步写的，所以程序中途崩溃时，第 2 章第 4 篇说的「moov 在末尾，文件打不开」就会发生。

## 2. 转封装

### 2.1 程序

转封装不解码也不编码，只是把包从一个容器搬到另一个容器：

```c
// ff_remux.c：转封装，不解码也不编码，把每个包换个容器写出去
#include <stdio.h>
#include <libavformat/avformat.h>

int main(int argc, char *argv[])
{
    if (argc != 3) { fprintf(stderr, "用法: %s 输入 输出（格式由扩展名决定）\n", argv[0]); return 1; }
    AVFormatContext *in = NULL, *out = NULL;
    if (avformat_open_input(&in, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(in, NULL);
    avformat_alloc_output_context2(&out, NULL, NULL, argv[2]);   // 根据文件名选择封装格式
    if (!out) { fprintf(stderr, "不认识的输出格式\n"); return 1; }

    // 为每一路输入流创建一路输出流，复制编码参数
    for (unsigned i = 0; i < in->nb_streams; i++) {
        AVStream *ist = in->streams[i], *ost = avformat_new_stream(out, NULL);
        avcodec_parameters_copy(ost->codecpar, ist->codecpar);
        ost->codecpar->codec_tag = 0;          // 各容器对同一编码的标记不同（MP4 中是 avc1），让封装器自己填
    }
    if (!(out->oformat->flags & AVFMT_NOFILE) && avio_open(&out->pb, argv[2], AVIO_FLAG_WRITE) < 0) return 1;
    if (avformat_write_header(out, NULL) < 0) { fprintf(stderr, "写文件头失败\n"); return 1; }

    // 写完文件头之后，封装器才确定每路流的时间基
    for (unsigned i = 0; i < in->nb_streams; i++)
        printf("流 %u：时间基 %d/%d → %d/%d\n", i, in->streams[i]->time_base.num, in->streams[i]->time_base.den,
               out->streams[i]->time_base.num, out->streams[i]->time_base.den);

    AVPacket *pkt = av_packet_alloc();
    long n = 0;
    while (av_read_frame(in, pkt) >= 0) {
        AVStream *ist = in->streams[pkt->stream_index], *ost = out->streams[pkt->stream_index];
        av_packet_rescale_ts(pkt, ist->time_base, ost->time_base);   // pts、dts、duration 都要换算
        pkt->pos = -1;
        av_interleaved_write_frame(out, pkt);      // 按时间交错写入，并接管 pkt 中的数据
        n++;
    }
    av_write_trailer(out);                         // MP4 的 moov 就是在这里写的
    printf("写入 %ld 个包\n", n);

    av_packet_free(&pkt);
    if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
    avformat_free_context(out);
    avformat_close_input(&in);
    return 0;
}
```

两个要点：

- **`codec_tag = 0`**：`codec_tag` 是编码在容器中的标记，MP4 中 H.264 的标记是 `avc1`（第 1 章 `ffprobe` 中的 `avc1 / 0x31637661`），FLV 中是数字 7，MKV 中是字符串 `V_MPEG4/ISO/AVC`。从 MP4 复制来的标记在别的容器中没有意义，清零后让封装器自己填。
- **`av_packet_rescale_ts`**：把包的 `pts`、`dts`、`duration` 从输入流的时间基换算到输出流的时间基。它内部就是第 2 章最后一篇写的那个用 128 位整数、四舍五入的换算函数。

### 2.2 运行

```bash
./ff_remux bbb-10s.mp4 r.flv
```

```
流 0：时间基 1/12288 → 1/1000
流 1：时间基 1/48000 → 1/1000
写入 710 个包
```

封装器在写文件头时把两路流的时间基都改成了 1/1000（FLV 只支持毫秒）。分别转成 FLV、TS、MKV，和命令行 `ffmpeg -c copy` 的结果比较流的哈希值：

| 格式 | 我们的程序 | `ffmpeg -c copy` | 流的哈希值 |
|---|---|---|---|
| FLV | 2999939 字节 | 3000038 字节 | 相同 |
| TS | 3164792 字节 | 3128696 字节 | 相同 |
| MKV | 2992743 字节 | 2992938 字节 | 相同 |

流的内容完全相同。文件大小的差别来自容器层面：FLV 和 MKV 小了一二百字节，因为命令行会把原文件的元数据（`major_brand`、`encoder` 等）复制过去，我们的程序没有复制；TS 大了 36 KB，原因见练习 1。

### 2.3 忘了换算时间戳

把 `av_packet_rescale_ts` 那一行去掉，直接把 MP4 中的时间戳写进去：

```bash
./ex_norescale bbb-10s.mp4 nr.flv
ffprobe -v error -show_entries format=duration -of csv=p=0 nr.flv
./ex_norescale bbb-10s.mp4 nr.mp4
ffprobe -v error -show_entries stream=codec_type,duration -of csv=p=0 nr.mp4
```

```
481.024000
video,1.365333
audio,10.000000
```

- **FLV 的时长变成了 481 秒**：音频最后一个包的时间戳是 481024（以 1/48000 为单位），FLV 把它当成了 481024 毫秒。
- **MP4 的视频只有 1.37 秒**：MP4 封装器为视频选的时间基是 1/90000，原来以 1/12288 为单位的 122880 被当成了 122880 / 90000 = 1.37 秒。音频的输出时间基恰好还是 1/48000，所以碰巧没事。

程序没有任何报错，文件也能打开，只是时间全乱了。**只要包在两个时间基之间传递，就要换算时间戳**，这是用 FFmpeg 写程序时最常见的错误。

## 3. 转码

### 3.1 流水线

转码把前面几篇的内容连起来：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="转码的流水线：视频包经过解码器得到帧，再经过编码器得到新的包，时间戳从编码器的时间基换算到输出流的时间基后写入；音频包不解码，时间戳从输入流换算到输出流后直接写入；结束时依次冲刷解码器和编码器">
  <defs>
    <marker id="tc-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <g style="stroke:var(--border-2)">
      <rect x="5" y="60" width="100" height="44" rx="6" style="fill:var(--panel-2)"/>
      <rect x="160" y="20" width="110" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="320" y="20" width="110" height="40" rx="6" style="fill:var(--accent-soft)"/>
      <rect x="565" y="60" width="110" height="44" rx="6" style="fill:var(--panel-2)"/>
    </g>
    <text x="16" y="80" style="fill:var(--fg)">输入文件</text><text x="16" y="96" style="fill:var(--muted);font-size:10px">av_read_frame</text>
    <text x="180" y="45" style="fill:var(--fg)">H.264 解码</text>
    <text x="335" y="45" style="fill:var(--fg)">x264 编码</text>
    <text x="576" y="80" style="fill:var(--fg)">输出文件</text><text x="576" y="96" style="fill:var(--muted);font-size:10px">interleaved_write</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#tc-arrow)">
      <path d="M105,72 L130,40 L158,40"/>
      <line x1="270" y1="40" x2="318" y2="40"/>
      <path d="M430,40 L540,40 L563,72"/>
      <path d="M105,92 L130,140 L540,140 L563,92"/>
    </g>
    <text x="124" y="16" style="fill:var(--muted);font-size:10px">视频包</text>
    <text x="282" y="32" style="fill:var(--muted);font-size:10px">帧</text>
    <text x="445" y="32" style="fill:var(--muted);font-size:10px">新的包 + 换算时间戳</text>
    <text x="250" y="132" style="fill:var(--muted);font-size:10px">音频包：不解码，只换算时间戳</text>
    <text x="5" y="178" style="fill:var(--muted);font-size:11px">结束时：先冲刷解码器（送 NULL 包），把剩下的帧送进编码器；再冲刷编码器（送 NULL 帧）；最后写文件尾</text>
  </g>
</svg>
</figure>

### 3.2 程序

```c
// ff_transcode.c：视频解码后用 libx264 重新编码，音频直接复制，写成新的文件
#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>

static AVFormatContext *in, *out;
static AVCodecContext *dec, *enc;
static int vs;                                   // 视频流的序号（输入和输出相同）

// 取出编码器输出的所有包，换算时间戳后写入文件
static void write_encoded(AVPacket *pkt)
{
    while (avcodec_receive_packet(enc, pkt) >= 0) {
        pkt->stream_index = vs;
        if (pkt->duration == 0)                      // x264 输出的包没有时长，按帧率补上，否则最后一帧的时长是 0
            pkt->duration = av_rescale_q(1, av_inv_q(enc->framerate), enc->time_base);
        av_packet_rescale_ts(pkt, enc->time_base, out->streams[vs]->time_base);
        av_interleaved_write_frame(out, pkt);
    }
}

// 取出解码器输出的所有帧，送进编码器
static void encode_decoded(AVFrame *frame, AVPacket *pkt)
{
    while (avcodec_receive_frame(dec, frame) >= 0) {
        frame->pict_type = AV_PICTURE_TYPE_NONE;   // 不沿用原来的帧类型，由编码器自己决定
        avcodec_send_frame(enc, frame);
        av_frame_unref(frame);
        write_encoded(pkt);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "用法: %s 输入 输出 [crf]\n", argv[0]); return 1; }
    const char *crf = argc > 3 ? argv[3] : "28";
    if (avformat_open_input(&in, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(in, NULL);
    avformat_alloc_output_context2(&out, NULL, NULL, argv[2]);

    const AVCodec *dcodec = NULL;
    vs = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, &dcodec, 0);
    for (unsigned i = 0; i < in->nb_streams; i++) {
        AVStream *ist = in->streams[i], *ost = avformat_new_stream(out, NULL);
        if ((int)i != vs) {                          // 其他流：直接复制
            avcodec_parameters_copy(ost->codecpar, ist->codecpar);
            ost->codecpar->codec_tag = 0;
            continue;
        }
        // 视频：解码器
        dec = avcodec_alloc_context3(dcodec);
        avcodec_parameters_to_context(dec, ist->codecpar);
        dec->pkt_timebase = ist->time_base;
        avcodec_open2(dec, dcodec, NULL);
        // 视频：编码器，参数大多沿用输入
        const AVCodec *ecodec = avcodec_find_encoder_by_name("libx264");
        enc = avcodec_alloc_context3(ecodec);
        enc->width = dec->width;
        enc->height = dec->height;
        enc->pix_fmt = dec->pix_fmt;
        enc->sample_aspect_ratio = dec->sample_aspect_ratio;
        enc->time_base = ist->time_base;             // 解码出来的帧的 pts 就是这个时间基，直接用
        enc->framerate = ist->avg_frame_rate;
        enc->color_range = dec->color_range;         // 色彩信息也要带过去（第 1 章的 BT.709、有限范围）
        enc->colorspace = dec->colorspace;
        enc->color_primaries = dec->color_primaries;
        enc->color_trc = dec->color_trc;
        av_opt_set(enc->priv_data, "crf", crf, 0);
        // MP4 要求 SPS/PPS 放在文件头（avcC）中，而不是码流里
        if (out->oformat->flags & AVFMT_GLOBALHEADER)
            enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(enc, ecodec, NULL) < 0) return 1;
        avcodec_parameters_from_context(ost->codecpar, enc);   // 编码器的参数（含 extradata）写进输出流
        ost->time_base = enc->time_base;             // 建议封装器使用的时间基，不设置时 MP4 会自己选 1/90000
    }
    avio_open(&out->pb, argv[2], AVIO_FLAG_WRITE);
    if (avformat_write_header(out, NULL) < 0) { fprintf(stderr, "写文件头失败\n"); return 1; }

    AVPacket *pkt = av_packet_alloc(), *opkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int64_t t0 = av_gettime_relative();
    while (av_read_frame(in, pkt) >= 0) {
        if (pkt->stream_index == vs) {
            avcodec_send_packet(dec, pkt);
            encode_decoded(frame, opkt);
            av_packet_unref(pkt);
        } else {                                     // 复制的流：只换算时间戳
            av_packet_rescale_ts(pkt, in->streams[pkt->stream_index]->time_base,
                                 out->streams[pkt->stream_index]->time_base);
            pkt->pos = -1;
            av_interleaved_write_frame(out, pkt);
        }
    }
    avcodec_send_packet(dec, NULL);                  // 先冲刷解码器
    encode_decoded(frame, opkt);
    avcodec_send_frame(enc, NULL);                   // 再冲刷编码器
    write_encoded(opkt);
    av_write_trailer(out);
    printf("用时 %.2f 秒\n", (av_gettime_relative() - t0) / 1e6);

    av_frame_free(&frame); av_packet_free(&pkt); av_packet_free(&opkt);
    avcodec_free_context(&dec); avcodec_free_context(&enc);
    avio_closep(&out->pb);
    avformat_free_context(out);
    avformat_close_input(&in);
    return 0;
}
```

和前几篇相比，新的要点有：

- **编码器的参数大多沿用解码器的**：宽高、像素格式、像素宽高比，还有色彩信息。第 1 章讲过 BT.709 和有限范围，如果不把 `color_range`、`colorspace` 这几项带过去，新文件中就没有这些标记，播放器只能去猜，颜色可能会偏。
- **编码器的 `time_base` 直接用输入流的时间基**（1/12288）。解码出的帧的 `pts` 就是这个单位，送进编码器时不需要换算；编码器输出的包再换算到输出流的时间基。
- **冲刷的顺序**：先冲刷解码器，它吐出的最后几帧还要送进编码器；然后才冲刷编码器。顺序反了，最后几帧就丢了。
- **`AV_CODEC_FLAG_GLOBAL_HEADER`**：第 2 章讲过，MP4 中的 SPS、PPS 放在文件头的 `avcC` 中，而不是码流里。打开编码器之前设置这个标志，x264 就会把参数集放到 `enc->extradata` 中，`avcodec_parameters_from_context` 再把它复制给输出流。封装器的 `AVFMT_GLOBALHEADER` 标志说明它需要这样做，MP4、FLV、MKV 都有，TS 没有。

### 3.3 运行

```bash
./ff_transcode bbb-10s.mp4 t28.mp4 28
ffmpeg -i bbb-10s.mp4 -c:v libx264 -crf 28 -c:a copy cli28.mp4
ffmpeg -v error -i t28.mp4 -map 0:v -c copy -f streamhash -hash md5 -
ffmpeg -v error -i cli28.mp4 -map 0:v -c copy -f streamhash -hash md5 -
```

```
用时 0.62 秒
0,v,MD5=7bb260c1c3ad2ede6e510514f9bd6270
0,v,MD5=7bb260c1c3ad2ede6e510514f9bd6270
```

我们的程序和命令行编码出的视频流**逐字节相同**，用时也一样（命令行 0.61 秒）。两个文件只差 2 个字节，是容器的元数据。

## 4. 三个坑

上面的程序是改了三次才和命令行完全一致的，每一次都是一个容易踩的坑。

### 4.1 包没有时长

第一版程序编码出的视频只有 9.958 秒，比原来少了一帧的时长。用第 2 章的 `mp4_boxes` 一看：

```
stts  2 项：(239,3750) (1,0)
```

最后一帧的时长是 0。x264 输出的包，`duration` 字段是 0，MP4 封装器只能用相邻两个包的 DTS 之差作为每一帧的时长，最后一帧后面没有包了，时长就成了 0。补上的办法是按帧率计算：`av_rescale_q(1, av_inv_q(framerate), time_base)`，即 1/24 秒换算成 1/12288 为单位，等于 512。

### 4.2 封装器自己选的时间基

同一个 `stts` 中，每帧的时长是 3750，而不是 512：MP4 封装器为视频选了 1/90000 的时间基。输出流的时间基如果不设置，封装器会按自己的习惯选一个；对 MP4 来说，用 1/90000 并没有错（第 2 章最后一篇讲过，90 kHz 能精确表示常见的帧率），但和原文件不同，编辑列表、时间戳都会跟着变。设置 `ost->time_base = enc->time_base`，封装器就会尽量采用这个值（它只是一个建议，FLV 这样的格式还是会改成 1/1000）。

### 4.3 沿用了原来的帧类型

解码器输出的帧，`pict_type` 记录着它原来是 I、P 还是 B 帧。如果原样送进编码器，libx264 会把原来是 I 帧的位置**强制编码成 I 帧**。去掉 `frame->pict_type = AV_PICTURE_TYPE_NONE` 这一行再转码：

| | I 帧的位置 | 文件大小 |
|---|---|---|
| 清除 `pict_type` | 0、31、83、133 | 1507646 字节 |
| 沿用 `pict_type` | 0、31、79、83、131、133、179、227 | 1699334 字节 |

沿用原来的帧类型，I 帧从 4 个变成了 8 个，原视频按 `-g 48` 每 2 秒插入的 I 帧都被保留了下来，文件大了 13%。清除之后，x264 只在镜头切换处放 I 帧（转码时没有设置 GOP，x264 默认最长 250 帧一个关键帧）。

> 有时候沿用帧类型正是我们想要的，比如希望转码后的关键帧和原视频的位置对齐，方便两个版本之间切换（视频网站的多码率就有这个要求）。关键是知道有这回事，然后有意识地选择。

### 4.4 不设置 GLOBAL_HEADER 会怎样

还剩一个疑问：如果不设置 `AV_CODEC_FLAG_GLOBAL_HEADER` 呢？试了一下，MP4 文件照样能播放，只是大了 164 字节：x264 把 SPS、PPS 和 SEI 放进了第一个包里，FFmpeg 的 MP4 封装器又从第一个包里把参数集取出来，生成了 `avcC`，于是参数集存了两份。

FLV 就不那么顺利了：

```
    300  视频      5      0 ms  关键帧 codec=7 AVC 序列头 cts=0
    342  视频     53      0 ms  关键帧 codec=7 AVC 序列头 cts=0
```

写文件头时编码器还没有输出任何东西，封装器只能先写一个只有 5 个字节的**空的** AVC 序列头，拿到第一个包后再补一个真正的序列头（53 字节）。FFmpeg 自己读这个文件没有问题，但第 2 章第 3 篇说过，直播服务器会缓存序列头发给新加入的观众，缓存的如果是那个空的，观众就看不了。所以，输出格式要求全局头时，就设置这个标志，不要依赖封装器的补救。

## 5. 小结

- 写文件：`avformat_alloc_output_context2` → `avformat_new_stream` 设置参数 → `avformat_write_header` → 循环 `av_interleaved_write_frame` → `av_write_trailer`。
- 写文件头后，封装器才确定每路流的时间基；每个包写入前都要用 `av_packet_rescale_ts` 换算时间戳。
- 转封装：复制 `codecpar`，清零 `codec_tag`，逐个包换算时间戳后写入。
- 转码：解码 → 编码 → 换算时间戳 → 写入；先冲刷解码器，再冲刷编码器。
- 编码器的参数和色彩信息从解码器继承；输出格式要求全局头时设置 `AV_CODEC_FLAG_GLOBAL_HEADER`。
- 注意包的 `duration`、输出流的 `time_base` 和帧的 `pict_type`，它们都可能让结果和预期不同，而程序不会报任何错误。

下一篇处理转码中经常需要的另外两件事：改变分辨率和像素格式（libswscale），以及改变音频的采样率和格式（libswresample）。

## 练习

1. 第 2.2 节中，我们的程序转出的 TS 比命令行大了 36 KB。用第 2 章的 `ts_parse` 分别统计两个文件，差别在哪里？在 `avformat_write_header` 之前设置 `out->max_delay = 700000` 再试一次。
2. 修改 `ff_transcode.c`，只转码前 5 秒：读到时间戳超过 5 秒的包就停止读取（注意时间戳要按各自流的时间基换算成秒），然后照常冲刷。输出文件的视频和音频各有多少帧？

<details>
<summary>点击查看答案</summary>

1. 用 `ts_parse` 统计：

   ```
   我们的程序：PID 0x0101： 1109 个包（音频），PES 710 个，填充 54425 字节
   命令行：    PID 0x0101：  932 个包（音频），PES 299 个，填充 29405 字节
   ```

   差别在音频：我们的程序为每个 AAC 帧写了一个 PES（470 个），每个 PES 的最后一个 TS 包都要填充；命令行把多个 AAC 帧合在一个 PES 中（59 个），这正是第 2 章第 5 篇看到的现象。TS 封装器合并音频帧的依据是 `max_delay`：音频数据最多可以在封装器中攒多久。命令行的 `ffmpeg` 默认把它设成 0.7 秒（`-muxdelay` 选项），而用库的时候默认是 0，于是每帧都单独成为一个 PES。设置 `out->max_delay = 700000`（单位是微秒）后，文件是 3129448 字节，和命令行只差 752 字节（4 个 TS 包，节目表重复的次数稍有不同）。这说明命令行工具在库的默认值之上还做了不少设置，自己写程序时要留意。

2. 在读包的循环中判断：

   ```c
   AVStream *st = in->streams[pkt->stream_index];
   if (pkt->pts != AV_NOPTS_VALUE && pkt->pts * av_q2d(st->time_base) > 5.0) {
       av_packet_unref(pkt);
       break;
   }
   ```

   `av_q2d` 把时间基换算成浮点数。实测输出：

   ```
   video,4.958333,119
   audio,4.885333,229
   ```

   视频 119 帧（4.958 秒），音频 229 帧（4.885 秒），都不到 5 秒，而且两路流差了 73 毫秒。原因有两个：

   - 包是按 **DTS** 交错读出的，而判断用的是 **PTS**。有 B 帧时，P 帧的 PTS 比 DTS 晚 2 帧，第一个 PTS 超过 5 秒的视频包出现时，DTS 才 4.9 秒左右，循环就停了，后面 PTS 不到 5 秒的 B 帧还没读到。
   - 一停就是所有流一起停。这时音频只读到 4.885 秒。

   命令行 `ffmpeg -t 5` 处理得更精细：它对每一路流分别判断，按解码后帧的时间截断，所以能得到正好 5 秒（120 帧视频）。这又是一个「命令行替我们做了很多事」的例子。

</details>
