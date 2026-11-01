---
title: Qt 播放器：解码线程与 OpenGL 显示
date: 2026-11-04 09:00:00
updated: 2026-11-04 09:00:00
categories:
  - 音视频开发
  - 音视频实战
tags:
  - 音视频
  - Qt
  - OpenGL
  - FFmpeg
  - C++
order: 1
description: 用 Qt 6 和 FFmpeg 写一个有界面的播放器：解码放在 QThread 里，解出的帧经过一个有界的帧队列交给界面线程；显示用 QOpenGLWidget，把 Y、U、V 三个平面上传成三张单通道纹理，在片元着色器里按 BT.709 转成 RGB。实测 720p 时着色器每帧 2.8 毫秒，用 swscale 转 RGB 再交给 QPainter 要 4.2 毫秒；到了 4K 结果却反过来，着色器 7.1 毫秒，QPainter 只要 5.5 毫秒。追下去发现，在这台 Mac 上同样一块 Y 平面，按单通道纹理上传要 3.49 毫秒，按 BGRA 上传只要 1.02 毫秒：macOS 的 OpenGL 早已被弃用，单通道纹理的上传走的是慢路径。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：把第 4 章的播放器搬进 Qt：理解解码线程和界面线程怎样分工、怎样通过帧队列和信号槽交换数据；会用 QOpenGLWidget 和着色器显示 YUV 画面；会测量显示环节的开销，知道「交给 GPU」不一定更快。
>
> **前置知识**：第 4 章《多线程与包队列》《音视频同步》；客户端专栏《界面卡顿与 QThread》《跨线程的信号与槽》《自定义控件与绘图》。
>
> **实验环境**：macOS 27.0（Apple M6），Qt 6.12.0，FFmpeg 9.0.2，Apple Clang 21，Release 构建。

## 1. 为什么要自己写

Qt 自带了播放器：Qt Multimedia 的 `QMediaPlayer` 加 `QVideoWidget`，十几行代码就能播放文件（客户端专栏的读者可以试试）。它在底层调用系统的多媒体框架（macOS 上是 AVFoundation，Windows 上是 Media Foundation），新版本的 Qt 也可以用 FFmpeg 做后端。那为什么还要自己写？

- **控制权**：自带的播放器是个黑盒。直播要低延迟、要追帧，监控要同时播十几路，剪辑软件要逐帧定位，这些都需要自己掌握解码、缓冲和同步。
- **把前面学的串起来**：第 3 章的解码、第 4 章的同步和跳转、第 5 章的直播协议，在一个真正有界面的程序里各就各位。

本章的播放器叫 `qplayer`，一共五个源文件（加上头文件），连注释约 1000 行：

<figure class="diagram">
<svg viewBox="0 0 680 230" role="img" aria-label="qplayer 的结构：解码线程 Decoder 读包、解码视频和音频；视频帧经过最多 12 帧的帧队列交给界面线程，界面线程中的 MainWindow 每 5 毫秒检查一次队头的帧是否到了显示时间，到了就交给 VideoWidget 用 OpenGL 显示；音频转换成 PCM 后写进 AudioOutput 的缓冲区，QAudioSink 从中拉取数据送给声卡，AudioOutput 同时提供声音时钟">
  <defs>
    <marker id="qp-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--muted)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <rect x="5" y="15" width="200" height="200" rx="8" style="fill:none;stroke:var(--border-2);stroke-dasharray:4 3"/>
    <text x="15" y="33" style="fill:var(--muted);font-size:10px">解码线程（QThread）</text>
    <rect x="300" y="15" width="375" height="125" rx="8" style="fill:none;stroke:var(--border-2);stroke-dasharray:4 3"/>
    <text x="310" y="33" style="fill:var(--muted);font-size:10px">界面线程（主线程）</text>
    <g style="stroke:var(--border-2)">
      <rect x="20" y="70" width="170" height="80" rx="6" style="fill:var(--panel-2)"/>
      <rect x="220" y="55" width="65" height="40" rx="6" style="fill:var(--mark)"/>
      <rect x="320" y="50" width="160" height="50" rx="6" style="fill:var(--panel-2)"/>
      <rect x="510" y="50" width="150" height="50" rx="6" style="fill:var(--panel-2)"/>
      <rect x="320" y="165" width="160" height="50" rx="6" style="fill:var(--mark)"/>
      <rect x="510" y="165" width="150" height="50" rx="6" style="fill:var(--panel-2)"/>
    </g>
    <text x="30" y="95" style="fill:var(--fg)">Decoder</text><text x="30" y="113" style="fill:var(--muted);font-size:10px">读包、解码</text><text x="30" y="129" style="fill:var(--muted);font-size:10px">swscale、swresample</text>
    <text x="226" y="73" style="fill:var(--fg);font-size:11px">帧队列</text><text x="226" y="88" style="fill:var(--muted);font-size:10px">≤12 帧</text>
    <text x="330" y="72" style="fill:var(--fg)">MainWindow</text><text x="330" y="89" style="fill:var(--muted);font-size:10px">每 5 ms 看一次队头</text>
    <text x="520" y="72" style="fill:var(--fg)">VideoWidget</text><text x="520" y="89" style="fill:var(--muted);font-size:10px">OpenGL 着色器</text>
    <text x="330" y="187" style="fill:var(--fg)">AudioOutput</text><text x="330" y="204" style="fill:var(--muted);font-size:10px">PCM 缓冲 + 声音时钟</text>
    <text x="520" y="187" style="fill:var(--fg)">QAudioSink</text><text x="520" y="204" style="fill:var(--muted);font-size:10px">拉数据，送声卡</text>
    <g style="stroke:var(--muted)" fill="none" marker-end="url(#qp-arrow)">
      <line x1="190" y1="90" x2="218" y2="78"/><line x1="285" y1="75" x2="318" y2="75"/><line x1="480" y1="75" x2="508" y2="75"/>
      <path d="M190,135 L250,190 L318,190"/><line x1="508" y1="190" x2="482" y2="190"/>
      <line x1="400" y1="163" x2="400" y2="102" stroke-dasharray="3 3"/>
    </g>
    <text x="406" y="140" style="fill:var(--muted);font-size:10px">声音时钟</text>
  </g>
</svg>
</figure>

和第 4 章的四线程结构相比，这里简化成一个解码线程：它同时解码视频和音频，视频帧放进帧队列，音频直接写进声音缓冲区。界面线程用一个 `QTimer` 每 5 毫秒检查一次队头的帧该不该显示了。本篇讲解码线程和视频显示，下一篇讲声音、同步和播放控制。

## 2. 工程

用 CMake 构建（客户端专栏《环境与第一个 Qt 程序》）。FFmpeg 是 Homebrew 装的，没有 pkg-config 时，让 `brew --prefix` 告诉我们头文件和库在哪里：

```cmake
cmake_minimum_required(VERSION 3.16)
project(qplayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia)

# FFmpeg 用 Homebrew 安装的，位置交给 brew 告诉我们
execute_process(COMMAND brew --prefix ffmpeg OUTPUT_VARIABLE FFMPEG_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)

add_executable(qplayer
    main.cpp mainwindow.cpp decoder.cpp videowidget.cpp audiooutput.cpp)
target_include_directories(qplayer PRIVATE ${FFMPEG_DIR}/include)
target_link_directories(qplayer PRIVATE ${FFMPEG_DIR}/lib)
target_link_libraries(qplayer PRIVATE
    Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia
    avformat avcodec swscale swresample avutil)
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
./build/qplayer bbb-10s.mp4
```

<figure>
<img src="/img/av/qt-player-file.jpg" alt="qplayer 播放测试视频的截图：上面是画面，下面一行是暂停按钮、进度条、时间 00:02 / 00:10 和音量滑块" loading="lazy">
<figcaption>qplayer 播放测试视频。截图是程序自己用 <code>QWidget::grab()</code> 保存的</figcaption>
</figure>

## 3. 解码线程

```cpp
// decoder.h：解码线程。读包、解码视频和音频；视频帧放进帧队列，音频交给 AudioOutput
#pragma once
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

class AudioOutput;

struct VideoFrame {
    AVFrame *frame = nullptr;   // YUV420P
    double pts = 0;             // 秒
    int serial = 0;             // 跳转一次加一，用来认出跳转之前的旧帧
};

class Decoder : public QThread {
    Q_OBJECT
public:
    Decoder(const QString &url, AudioOutput *audio, QObject *parent = nullptr);
    ~Decoder() override;

    void stop();
    void seek(double seconds);

    // 主线程从帧队列取帧：先看一眼队头，决定显示以后再取走
    bool peek(VideoFrame &f);
    bool peekNext(VideoFrame &f);   // 队列中的第二帧，用来判断要不要丢帧
    void pop();
    int queued();

    std::atomic<int> serial{0};
    double duration = 0;         // 直播流没有时长，为 0
    bool live = false;

signals:
    void opened(double duration, int width, int height, bool hasAudio);
    void failed(const QString &message);
    void endOfStream();

protected:
    void run() override;

private:
    bool open();
    void decodeVideo(AVPacket *pkt);
    void decodeAudio(AVPacket *pkt);
    void pushFrame(AVFrame *src);
    void clearQueue();

    QString m_url;
    AudioOutput *m_audio;
    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_vdec = nullptr, *m_adec = nullptr;
    int m_vidx = -1, m_aidx = -1;
    SwsContext *m_sws = nullptr;
    SwrContext *m_swr = nullptr;
    AVFrame *m_frame = nullptr;

    std::atomic<bool> m_stop{false};
    std::atomic<double> m_seekTo{-1};
    double m_dropBefore = -1;    // 精确跳转：丢掉目标时刻之前的帧

    QMutex m_mutex;
    QWaitCondition m_notFull;
    QQueue<VideoFrame> m_queue;
    static constexpr int kMaxFrames = 12;
};
```

```cpp
#include "decoder.h"
#include "audiooutput.h"

extern "C" {
#include <libavutil/opt.h>
}

Decoder::Decoder(const QString &url, AudioOutput *audio, QObject *parent)
    : QThread(parent), m_url(url), m_audio(audio) {}

Decoder::~Decoder() {
    stop();
    wait();
    clearQueue();
    av_frame_free(&m_frame);
    sws_freeContext(m_sws);
    swr_free(&m_swr);
    avcodec_free_context(&m_vdec);
    avcodec_free_context(&m_adec);
    avformat_close_input(&m_fmt);
}

void Decoder::stop() {
    m_stop = true;
    m_notFull.wakeAll();
}

void Decoder::seek(double seconds) {
    m_seekTo = seconds;
    m_notFull.wakeAll();        // 解码线程可能正卡在「队列满了」上，叫醒它
}

bool Decoder::peek(VideoFrame &f) {
    QMutexLocker lock(&m_mutex);
    if (m_queue.isEmpty()) return false;
    f = m_queue.head();
    return true;
}

bool Decoder::peekNext(VideoFrame &f) {
    QMutexLocker lock(&m_mutex);
    if (m_queue.size() < 2) return false;
    f = m_queue.at(1);
    return true;
}

void Decoder::pop() {
    QMutexLocker lock(&m_mutex);
    if (m_queue.isEmpty()) return;
    VideoFrame f = m_queue.dequeue();
    av_frame_free(&f.frame);
    m_notFull.wakeOne();
}

int Decoder::queued() {
    QMutexLocker lock(&m_mutex);
    return m_queue.size();
}

void Decoder::clearQueue() {
    QMutexLocker lock(&m_mutex);
    while (!m_queue.isEmpty()) {
        VideoFrame f = m_queue.dequeue();
        av_frame_free(&f.frame);
    }
    m_notFull.wakeAll();
}

bool Decoder::open() {
    // 网络流可能是直播：不要花几秒钟分析流信息（第 5 章 RTMP 篇）
    live = m_url.contains("://");
    AVDictionary *opts = nullptr;
    if (live) {
        av_dict_set(&opts, "analyzeduration", "0", 0);
        av_dict_set(&opts, "probesize", "32", 0);
    }
    QByteArray url = m_url.toUtf8();
    int ret = avformat_open_input(&m_fmt, url.constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) { emit failed(QString("打开失败：%1").arg(av_err2str(ret))); return false; }
    avformat_find_stream_info(m_fmt, nullptr);
    if (m_fmt->duration > 0) { duration = m_fmt->duration / (double)AV_TIME_BASE; live = false; }
    else live = true;           // 没有时长的就是直播

    m_vidx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m_aidx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    auto openCodec = [&](int idx) -> AVCodecContext * {
        if (idx < 0) return nullptr;
        AVCodecParameters *par = m_fmt->streams[idx]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if (!codec) return nullptr;
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, par);
        ctx->pkt_timebase = m_fmt->streams[idx]->time_base;
        if (live) ctx->thread_type = FF_THREAD_SLICE;   // 帧级多线程会多攒几帧，直播时不用
        if (avcodec_open2(ctx, codec, nullptr) < 0) avcodec_free_context(&ctx);
        return ctx;
    };
    m_vdec = openCodec(m_vidx);
    m_adec = openCodec(m_aidx);
    if (!m_vdec && !m_adec) { emit failed("没有可以解码的音视频流"); return false; }

    if (m_adec) {
        // 声音统一转换成 48000 Hz、立体声、16 位整数，交给 QAudioSink
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        swr_alloc_set_opts2(&m_swr, &stereo, AV_SAMPLE_FMT_S16, 48000,
                            &m_adec->ch_layout, m_adec->sample_fmt, m_adec->sample_rate, 0, nullptr);
        swr_init(m_swr);
    }
    m_frame = av_frame_alloc();
    emit opened(duration, m_vdec ? m_vdec->width : 0, m_vdec ? m_vdec->height : 0, m_adec != nullptr);
    return true;
}

void Decoder::run() {
    if (!open()) return;
    AVPacket *pkt = av_packet_alloc();
    bool eof = false;
    while (!m_stop) {
        double target = m_seekTo.exchange(-1);
        if (target >= 0) {
            // 跳转：先找到目标之前的关键帧，再清空解码器、队列和声音缓冲区，序号加一
            int64_t ts = (int64_t)(target * AV_TIME_BASE);
            avformat_seek_file(m_fmt, -1, INT64_MIN, ts, ts, 0);
            if (m_vdec) avcodec_flush_buffers(m_vdec);
            if (m_adec) avcodec_flush_buffers(m_adec);
            serial++;
            clearQueue();
            if (m_audio) m_audio->clear(m_vidx >= 0);
            m_dropBefore = target;
            eof = false;
        }
        if (eof) { msleep(10); continue; }   // 播完了，等用户跳转或者关闭

        int ret = av_read_frame(m_fmt, pkt);
        if (ret < 0) {
            // 文件读完：送一个空包，把解码器里攒着的帧都取出来
            if (m_vdec) decodeVideo(nullptr);
            if (m_adec) decodeAudio(nullptr);
            eof = true;
            emit endOfStream();
            continue;
        }
        if (pkt->stream_index == m_vidx) decodeVideo(pkt);
        else if (pkt->stream_index == m_aidx) decodeAudio(pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

void Decoder::decodeVideo(AVPacket *pkt) {
    if (avcodec_send_packet(m_vdec, pkt) < 0) return;
    while (avcodec_receive_frame(m_vdec, m_frame) == 0) {
        pushFrame(m_frame);
        av_frame_unref(m_frame);
    }
    if (!pkt) avcodec_flush_buffers(m_vdec);   // 冲刷完以后要重置，跳转回来才能继续解码
}

void Decoder::pushFrame(AVFrame *src) {
    AVRational tb = m_fmt->streams[m_vidx]->time_base;
    double pts = src->best_effort_timestamp == AV_NOPTS_VALUE ? 0 : src->best_effort_timestamp * av_q2d(tb);
    if (m_dropBefore >= 0 && pts < m_dropBefore - 0.001) return;   // 精确跳转：目标之前的帧不要

    // 显示控件只认 YUV420P，其他格式先转换
    AVFrame *dst = av_frame_alloc();
    if (src->format == AV_PIX_FMT_YUV420P) {
        av_frame_ref(dst, src);
    } else {
        dst->format = AV_PIX_FMT_YUV420P;
        dst->width = src->width;
        dst->height = src->height;
        av_frame_get_buffer(dst, 0);
        m_sws = sws_getCachedContext(m_sws, src->width, src->height, (AVPixelFormat)src->format,
                                     src->width, src->height, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
        sws_scale(m_sws, src->data, src->linesize, 0, src->height, dst->data, dst->linesize);
    }
    VideoFrame f{dst, pts, serial.load()};

    // 队列满了就等：这是背压，解码最多比显示超前 kMaxFrames 帧
    QMutexLocker lock(&m_mutex);
    while (m_queue.size() >= kMaxFrames && !m_stop && m_seekTo < 0)
        m_notFull.wait(&m_mutex);
    if (m_stop || m_seekTo >= 0) { av_frame_free(&dst); return; }
    m_queue.enqueue(f);
}

void Decoder::decodeAudio(AVPacket *pkt) {
    if (avcodec_send_packet(m_adec, pkt) < 0) return;
    AVRational tb = m_fmt->streams[m_aidx]->time_base;
    while (avcodec_receive_frame(m_adec, m_frame) == 0) {
        double pts = m_frame->best_effort_timestamp * av_q2d(tb);
        double dur = (double)m_frame->nb_samples / m_frame->sample_rate;
        if (m_dropBefore >= 0 && pts + dur < m_dropBefore) { av_frame_unref(m_frame); continue; }
        int out = swr_get_out_samples(m_swr, m_frame->nb_samples);
        QByteArray buf(out * 4, Qt::Uninitialized);
        uint8_t *dst = (uint8_t *)buf.data();
        int n = swr_convert(m_swr, &dst, out, (const uint8_t **)m_frame->extended_data, m_frame->nb_samples);
        if (n > 0 && m_audio) m_audio->write(buf.constData(), n * 4, pts + dur);
        av_frame_unref(m_frame);
    }
    if (!pkt) avcodec_flush_buffers(m_adec);
    // 只有声音的文件：声音缓冲超过 1 秒就歇一会儿，免得一口气把整个文件解码进内存
    while (m_vidx < 0 && m_audio && m_audio->bufferedSeconds() > 1.0 && !m_stop && m_seekTo < 0)
        msleep(10);
}
```

几个要点：

- **`Decoder` 继承 `QThread`，重写 `run()`**。`run()` 中的代码在新线程里执行，其余成员函数（`peek`、`pop`、`seek`、`stop`）由界面线程调用。两边共享的东西只有两个：帧队列，用 `QMutex` 保护；跳转、停止的请求，用 `std::atomic`。
- **帧队列是有界的**：最多 12 帧，满了解码线程就在 `QWaitCondition` 上等，界面线程取走一帧后 `wakeOne()`。这是第 4 章讲过的背压：解码最多比显示超前 0.5 秒，不会把整部电影解码进内存。跳转和停止时也要叫醒它，否则解码线程卡在「队列满了」上，永远看不到新的请求。
- **跨线程的信号**：`opened`、`failed` 是在解码线程里 `emit` 的，`MainWindow` 中的槽函数却在界面线程执行。`connect` 时不需要额外指定，Qt 发现发送者和接收者在不同的线程，自动使用排队连接（客户端专栏《跨线程的信号与槽》）。界面控件只能在界面线程中操作，这一点很重要。
- **帧的所有权**：队列中的每一帧都是 `av_frame_alloc` 出来的独立引用，`pop` 时释放。界面线程显示一帧时，`VideoWidget` 用 `av_frame_ref` 再引用一次，所以显示之后马上 `pop` 也没关系，数据要等两个引用都释放才会被回收（第 3 章讲过 AVFrame 的引用计数）。
- **直播流**：URL 带 `://` 的先按直播处理，把 `analyzeduration`、`probesize` 调小（第 5 章 RTMP 篇的 5 秒积压）；打开后有时长的再改回点播。直播时解码器只用片级多线程，帧级多线程会让解码器多攒好几帧。

## 4. 用 OpenGL 显示 YUV

解码出来的是 YUV420P，屏幕要的是 RGB。可以让 CPU 用 swscale 转换（第 4 章用 SDL 时，SDL 内部也是交给 GPU 做的），也可以把 Y、U、V 原样上传给 GPU，在着色器里转换。`VideoWidget` 两种都实现了，用命令行参数切换，方便比较：

```cpp
// videowidget.h：显示 YUV420P 的画面
//   GL 模式：三个平面各上传成一张纹理，在片元着色器中转换成 RGB（GPU 做）
//   Image 模式：用 swscale 转成 RGB 的 QImage，再用 QPainter 画（CPU 做），用来对比
#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QImage>

extern "C" {
#include <libavutil/frame.h>
struct SwsContext;
}

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum Mode { GL, Image };
    explicit VideoWidget(Mode mode, QWidget *parent = nullptr);
    ~VideoWidget() override;
    void setFrame(const AVFrame *frame);   // 引用这一帧，下次重绘时显示
    int paints = 0;                         // 统计：画了多少次、上传和转换一共花了多少毫秒
    double uploadMs = 0;

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    QRect fitRect() const;                  // 保持宽高比，四周留黑边

    Mode m_mode;
    AVFrame *m_frame = nullptr;
    bool m_dirty = false;
    QOpenGLShaderProgram m_prog;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    GLuint m_tex[3] = {0, 0, 0};
    QSize m_texSize;                        // 纹理当前的尺寸，变了才重新分配
    SwsContext *m_sws = nullptr;
    QImage m_image;
};
```

```cpp
#include "videowidget.h"
#include <QPainter>
#include <QElapsedTimer>

extern "C" {
#include <libswscale/swscale.h>
}

// 顶点着色器：一个铺满视口的矩形，纹理坐标的 y 翻过来（图像第一行在上面）
static const char *kVertex = R"(#version 330 core
layout(location = 0) in vec2 pos;
out vec2 uv;
void main() {
    uv = vec2((pos.x + 1.0) * 0.5, (1.0 - pos.y) * 0.5);
    gl_Position = vec4(pos, 0.0, 1.0);
})";

// 片元着色器：BT.709 有限范围的 YUV 转 RGB（第 1 章《YUV》中的公式）
static const char *kFragment = R"(#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D texY, texU, texV;
void main() {
    float y = (texture(texY, uv).r - 16.0 / 255.0) * (255.0 / 219.0);
    float u = (texture(texU, uv).r - 128.0 / 255.0) * (255.0 / 224.0);
    float v = (texture(texV, uv).r - 128.0 / 255.0) * (255.0 / 224.0);
    color = vec4(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u, 1.0);
})";

VideoWidget::VideoWidget(Mode mode, QWidget *parent) : QOpenGLWidget(parent), m_mode(mode) {}

VideoWidget::~VideoWidget() {
    makeCurrent();
    if (m_tex[0]) glDeleteTextures(3, m_tex);
    doneCurrent();
    av_frame_free(&m_frame);
    sws_freeContext(m_sws);
}

void VideoWidget::setFrame(const AVFrame *frame) {
    if (!m_frame) m_frame = av_frame_alloc();
    av_frame_unref(m_frame);
    av_frame_ref(m_frame, frame);
    m_dirty = true;
    update();                       // 只是安排一次重绘，真正的绘制在 paintGL 中
}

void VideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertex);
    m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragment);
    m_prog.link();
    static const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(quad, sizeof(quad));
    m_prog.enableAttributeArray(0);
    m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    m_vao.release();
    glGenTextures(3, m_tex);
    for (GLuint t : m_tex) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

QRect VideoWidget::fitRect() const {
    QSize area = size() * devicePixelRatio();
    if (!m_frame) return QRect(QPoint(0, 0), area);
    QSize s(m_frame->width, m_frame->height);
    s.scale(area, Qt::KeepAspectRatio);
    return QRect((area.width() - s.width()) / 2, (area.height() - s.height()) / 2, s.width(), s.height());
}

void VideoWidget::paintGL() {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!m_frame) return;
    QRect r = fitRect();
    QElapsedTimer timer;
    timer.start();
    paints++;

    if (m_mode == Image) {
        if (m_dirty) {
            // CPU 把整帧转换成 RGB
            if (m_image.size() != QSize(m_frame->width, m_frame->height))
                m_image = QImage(m_frame->width, m_frame->height, QImage::Format_RGB32);
            m_sws = sws_getCachedContext(m_sws, m_frame->width, m_frame->height, AV_PIX_FMT_YUV420P,
                                         m_frame->width, m_frame->height, AV_PIX_FMT_BGRA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
            const int *bt709 = sws_getCoefficients(SWS_CS_ITU709);   // 和着色器一样按 BT.709 转换
            sws_setColorspaceDetails(m_sws, bt709, 0, bt709, 1, 0, 1 << 16, 1 << 16);
            uint8_t *dst[1] = {m_image.bits()};
            int dstStride[1] = {(int)m_image.bytesPerLine()};
            sws_scale(m_sws, m_frame->data, m_frame->linesize, 0, m_frame->height, dst, dstStride);
            m_dirty = false;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        qreal dpr = devicePixelRatio();
        p.drawImage(QRectF(r.x() / dpr, r.y() / dpr, r.width() / dpr, r.height() / dpr), m_image);
        p.end();
        glFinish();                 // 等 GPU 做完，计时才包括真正的上传
        uploadMs += timer.nsecsElapsed() / 1e6;
        return;
    }

    // GL 模式：Y、U、V 三个平面原样上传，宽高分别是 w×h、w/2×h/2
    if (m_dirty) {
        bool realloc = m_texSize != QSize(m_frame->width, m_frame->height);
        m_texSize = QSize(m_frame->width, m_frame->height);
        for (int i = 0; i < 3; i++) {
            int w = i ? (m_frame->width + 1) / 2 : m_frame->width;
            int h = i ? (m_frame->height + 1) / 2 : m_frame->height;
            glBindTexture(GL_TEXTURE_2D, m_tex[i]);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, m_frame->linesize[i]);   // 每行有填充，告诉 GL 真正的行宽
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            // 尺寸变了才重新分配纹理，平时只更新内容
            if (realloc) glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, m_frame->data[i]);
            else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, m_frame->data[i]);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        m_dirty = false;
    }
    glViewport(r.x(), r.y(), r.width(), r.height());
    m_prog.bind();
    for (int i = 0; i < 3; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_tex[i]);
    }
    m_prog.setUniformValue("texY", 0);
    m_prog.setUniformValue("texU", 1);
    m_prog.setUniformValue("texV", 2);
    m_vao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao.release();
    m_prog.release();
    glFinish();
    uploadMs += timer.nsecsElapsed() / 1e6;
}
```

- **三张单通道纹理**：Y 平面是 w×h，U、V 是 (w/2)×(h/2)，各上传成一张 `GL_R8` 纹理。每行末尾可能有填充（linesize 大于宽度），用 `GL_UNPACK_ROW_LENGTH` 告诉 OpenGL 真正的行宽，不用先把数据挪紧凑。
- **片元着色器**：每个像素取出 Y、U、V，先去掉有限范围的偏移（Y 减 16、乘 255/219，UV 减 128、乘 255/224），再乘 BT.709 的矩阵。这就是第 1 章《YUV》中的公式，只是变成了 GPU 上每个像素并行执行。Image 模式用 swscale 时，也要用 `sws_setColorspaceDetails` 指定 BT.709，否则默认按 BT.601 转换，颜色会偏（第 4 章《最简单的视频播放器》）。
- **保持宽高比**：`fitRect` 算出画面在窗口中的位置，GL 模式用 `glViewport` 只画在这块区域里，四周是 `glClear` 留下的黑边。注意 Retina 屏上窗口的像素尺寸是逻辑尺寸乘以 `devicePixelRatio()`（这里是 2）。
- **`setFrame` 只是 `update()`**：真正的绘制在 Qt 下一次重绘时进行。连续调用几次 `setFrame`，Qt 会合并成一次重绘，只显示最后一帧。

还有一个容易漏掉的地方：macOS 上的 OpenGL 默认是 2.1 的兼容模式，不支持 `#version 330` 的着色器，必须在创建 `QApplication` **之前**申请 Core Profile（完整的 `main.cpp` 在下一篇）：

```cpp
QSurfaceFormat fmt;
fmt.setVersion(4, 1);
fmt.setProfile(QSurfaceFormat::CoreProfile);
QSurfaceFormat::setDefaultFormat(fmt);
QApplication app(argc, argv);
```

## 5. GPU 一定更快吗

直觉上，在着色器里转换颜色应该比 CPU 快得多：swscale 要处理每一个像素，GPU 则是成千上万个核心并行。`VideoWidget` 在 `paintGL` 里计时（包括上传、转换和绘制，最后 `glFinish()` 等 GPU 做完），播放 10 秒，统计平均每次重绘的时间：

| 视频 | GL 模式（着色器转换） | Image 模式（swscale + QPainter） |
|---|---|---|
| 720p H.264 | 2.77 ms | 4.15 ms |
| 4K H.264 | 7.09 ms | 5.52 ms |

720p 时着色器快，符合预期。4K 时却反过来了：Image 模式要用 CPU 转换 830 万个像素，再上传 33 MB 的 RGB 数据，竟然比只上传 12 MB 的 YUV 还快。

先怀疑是每帧都用 `glTexImage2D` 重新分配纹理。改成尺寸不变时用 `glTexSubImage2D` 只更新内容（现在的代码就是这样），4K 是 7.33 毫秒，没有变化。再把上传单独计时：4K 的三张纹理上传就要 7.6 毫秒，几乎就是全部时间，着色器转换和绘制本身很快。

最后做一个对照：同一块 4K 的 Y 平面（830 万字节），分别当作单通道纹理（`GL_R8`、`GL_RED`）和宽度为 1/4 的 BGRA 纹理上传，字节数完全一样：

| 上传方式 | 每次耗时 |
|---|---|
| `GL_R8` 单通道纹理 | 3.49 ms |
| `GL_RGBA8` + `GL_BGRA`，宽度 1/4 | 1.02 ms |

同样的数据，单通道的上传慢了 3.4 倍。Apple 从 macOS 10.14 起就弃用了 OpenGL，现在的 OpenGL 是实现在 Metal 之上的一个兼容层，BGRA 是 Metal 最常用的格式，走的是优化过的路径，单通道纹理显然没有享受到这个待遇。QPainter 上传的正是 BGRA 的 QImage，所以 Image 模式反而占了便宜。

这个结果有几点启发：

- **性能要测，不要猜**。「GPU 更快」只在数据已经在 GPU 上、或者上传不是瓶颈时成立。
- **同一套代码在不同平台上的表现可能完全不同**。在 Windows、Linux 的 OpenGL 驱动上，单通道纹理的上传通常没有这个问题。
- **在 macOS 上要追求性能，应该用 Metal**。Qt 6 提供了跨平台的图形抽象层 QRhi（Qt Multimedia 的 `QVideoWidget` 就用它，在 macOS 上走 Metal）；也可以像第 4 章《硬件解码》那样，让解码器输出 GPU 上的帧，直接交给 Metal 渲染，完全省掉上传。
- 对 720p、1080p 来说，3～4 毫秒的上传完全可以接受，本章的播放器继续用 OpenGL，代码简单，到处都能跑。

## 6. 小结

- 播放器的界面线程只做显示和响应操作，读包、解码放在 QThread 中；两者通过有界的帧队列交换数据，满了解码线程就等，这就是背压。
- 跨线程发信号时 Qt 自动排队到接收者的线程，界面控件只在界面线程中操作。
- QOpenGLWidget 显示 YUV：三个平面各传一张单通道纹理，在片元着色器中按 BT.709 转 RGB；macOS 上要申请 Core Profile。
- 实测 720p 时着色器比 swscale 加 QPainter 快，4K 时反而慢：macOS 上 OpenGL 的单通道纹理上传比 BGRA 慢 3.4 倍。性能要实测。

## 练习

1. `Decoder::pushFrame` 在队列满时等待，等待的条件是 `m_queue.size() >= kMaxFrames && !m_stop && m_seekTo < 0`。如果去掉后两个条件，会出现什么问题？
2. `VideoWidget::setFrame` 被调用了 240 次，统计显示重绘也是 240 次左右。如果把显示控件改成每次 `setFrame` 都同步地画一次（比如调用 `repaint()`），会有什么不同？
3. 片元着色器中的 `(texture(texY, uv).r - 16.0 / 255.0) * (255.0 / 219.0)` 这一步如果漏掉了，画面会怎样？

<details>
<summary>点击查看答案</summary>

1. 用户跳转或者关闭窗口时，解码线程可能正卡在 `m_notFull.wait()` 上：队列是满的，界面线程又因为要跳转而不再取帧（或者窗口正在关闭，根本没人取帧）。如果等待条件里没有 `!m_stop && m_seekTo < 0`，即使 `seek()`、`stop()` 里调用了 `wakeAll()`，解码线程醒来一看队列还是满的，又睡回去了，永远不会去处理跳转请求，关闭窗口时 `Decoder` 的析构函数在 `wait()` 上等它结束，界面就卡死了。第 4 章《多线程与包队列》中少一次通知就死锁，也是同一类问题：等待的线程必须能被所有「需要它醒来」的事件叫醒，而且醒来后要重新检查所有条件。

2. `update()` 只是标记控件需要重绘，Qt 在事件循环的下一轮统一绘制，而且会把多次请求合并成一次；`repaint()` 则立即同步绘制。在本程序中，每 5 毫秒的定时器最多显示一帧，两者的次数差不多。但同步绘制会让 `onTick` 等着绘制完成（4K 时 7 毫秒，加上和屏幕刷新同步的等待可能更久），这段时间界面线程什么都做不了，下一次检查队头也被推迟，可能导致帧显示得晚、被当作迟到的帧丢掉。一般应该用 `update()`，让 Qt 安排绘制的时机。另外，QOpenGLWidget 的内容最终要合成到窗口里，并跟随屏幕的垂直同步显示，画得再快也要等到下一次刷新才能看到。

3. 漏掉这一步，就是把有限范围（16～235）的 Y 当成了全范围（0～255）：黑色的 Y=16 会显示成 16/255 的深灰，白色的 Y=235 显示成 235/255 的浅灰，画面整体发灰、对比度下降，像蒙了一层雾。UV 不做 255/224 的缩放，颜色的饱和度也会略微下降。第 1 章《YUV》讲过有限范围和全范围，第 3 章用 swscale 时这件事由 `srcRange` 参数控制，写着色器时就要自己处理了。

</details>
